#include "codegen.h"

static FILE *g_out = 0;
static int   g_label = 0;
static int   g_target_windows = 0;
static int   g_cur_line = 0;
static int   g_binop_unsigned = 0;   /* 当前 gen_binop 的两个操作数是否无符号 */
static int   g_cur_float_is_4 = 0;   /* 当前浮点运算是否为 4 字节 float */

/* ---------- 模块表 ---------- */
#define MAX_MODULES_CG 32
typedef struct {
    char name[128];
    int  name_len;
    func_t **funcs;
    int      nfuncs;
} module_cg_t;
static module_cg_t g_modules_codegen[MAX_MODULES_CG];
static int g_nmodules_codegen = 0;

static const char *g_cur_module = 0;
static int         g_cur_module_len = 0;

void codegen_register_module(const char *name, int name_len, func_t **funcs, int nfuncs) {
    if (g_nmodules_codegen >= MAX_MODULES_CG) return;
    module_cg_t *m = &g_modules_codegen[g_nmodules_codegen++];
    int nl = name_len; if (nl >= 127) nl = 127;
    memcpy(m->name, name, nl); m->name[nl] = 0;
    m->name_len = nl;
    m->funcs = funcs;
    m->nfuncs = nfuncs;
}

static int find_module_func_idx(const char *modname, int modlen,
                                 const char *fnname, int fnlen) {
    for (int i = 0; i < g_nmodules_codegen; i++) {
        module_cg_t *m = &g_modules_codegen[i];
        if (m->name_len != modlen || memcmp(m->name, modname, modlen) != 0) continue;
        for (int j = 0; j < m->nfuncs; j++) {
            func_t *f = m->funcs[j];
            if (f->name_len == fnlen && memcmp(f->name, fnname, fnlen) == 0)
                return i;
        }
    }
    return -1;
}

#define MAX_FUNC_SIGS 64
typedef struct {
    const char *name; int len;
    int is_float_ret;
    int nparams;
    int param_is_float[8];
} func_sig_t;
static func_sig_t g_func_sigs[MAX_FUNC_SIGS];
static int        g_nfunc_sigs = 0;

static func_sig_t *find_func_sig(const char *name, int len) {
    for (int i = 0; i < g_nfunc_sigs; i++)
        if (g_func_sigs[i].len == len &&
            memcmp(g_func_sigs[i].name, name, len) == 0)
            return &g_func_sigs[i];
    return 0;
}
static program_t *g_prog = 0;

#define MAX_LOOPS 64
static int g_break_stack[MAX_LOOPS];
static int g_cont_stack[MAX_LOOPS];
static int g_loop_depth = 0;

void codegen_set_target(int is_windows) { g_target_windows = is_windows; }

#define MAX_LOCALS 128
typedef struct {
    const char *name; int len; int offset;
    int is_array; int arr_len; int is_float;
    int is_container;
    int cont_type;
    int is_range;
    int64_t range_lo, range_hi;
    int range_lo_open, range_hi_open;
    double range_lo_f, range_hi_f;
    int range_is_float;
    int is_ptr;
    int size;
    int is_unsigned;
    int float_size;   /* 4 或 8，仅 is_float 时有效 */
    int is_ref;       /* 引用 */
    int is_func_ptr;  /* 函数指针 */
} local_t;
static local_t g_locals[MAX_LOCALS];
static int     g_nlocals = 0;
static int     g_stack_used = 0;

#define MAX_GLOBALS 256
typedef struct {
    const char *name; int len;
    int is_array; int arr_len; int is_float;
    stmt_t *decl; int id;
    int is_struct_inst; int struct_idx; int inst_idx;
    int is_unsigned;
    int float_size;
    int is_extern;
} global_t;
static global_t g_globals[MAX_GLOBALS];
static int      g_nglobals = 0;

static void emit(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(g_out, fmt, ap); va_end(ap);
}
static int new_label(void) { return g_label++; }

static int local_add_ex(const char *name, int len, int is_array, int arr_len, int is_float) {
    int slots = is_array ? arr_len : 1;
    g_stack_used += 8 * slots;
    g_locals[g_nlocals].name = name; g_locals[g_nlocals].len = len;
    g_locals[g_nlocals].offset = -g_stack_used;
    g_locals[g_nlocals].is_array = is_array;
    g_locals[g_nlocals].arr_len = arr_len;
    g_locals[g_nlocals].is_float = is_float;
    g_locals[g_nlocals].is_container = 0;
    g_locals[g_nlocals].cont_type = 0;
    g_locals[g_nlocals].is_range = 0;
    g_locals[g_nlocals].range_is_float = 0;
    g_locals[g_nlocals].is_ptr = 0;
    g_locals[g_nlocals].size = 8;
    g_locals[g_nlocals].is_unsigned = 0;
    g_locals[g_nlocals].float_size = 0;
    g_locals[g_nlocals].is_ref = 0;
    g_locals[g_nlocals].is_func_ptr = 0;
    g_nlocals++;
    return -g_stack_used;
}

static int local_add_cont(const char *name, int len) {
    g_stack_used += 16;
    g_locals[g_nlocals].name = name; g_locals[g_nlocals].len = len;
    g_locals[g_nlocals].offset = -g_stack_used;
    g_locals[g_nlocals].is_array = 0;
    g_locals[g_nlocals].arr_len = 0;
    g_locals[g_nlocals].is_float = 0;
    g_locals[g_nlocals].is_container = 1;
    g_locals[g_nlocals].cont_type = 0;
    g_locals[g_nlocals].is_range = 0;
    g_nlocals++;
    return -g_stack_used;
}
static int local_add(const char *name, int len) { return local_add_ex(name, len, 0, 1, 0); }

static int find_local(const char *name, int len) {
    for (int i = g_nlocals - 1; i >= 0; i--)
        if (g_locals[i].len == len && memcmp(g_locals[i].name, name, len) == 0) return i;
    return -1;
}
static int find_global(const char *name, int len) {
    for (int i = 0; i < g_nglobals; i++)
        if (g_globals[i].len == len && memcmp(g_globals[i].name, name, len) == 0) return i;
    return -1;
}
static int find_struct_field(struct_def_t *sd, const char *name, int len) {
    for (int i = 0; i < sd->nfields; i++)
        if (sd->fields[i].name_len == len &&
            memcmp(sd->fields[i].name, name, len) == 0) return i;
    return -1;
}

static int is_op_text(const char *t, int len, const char *s) {
    return (int)strlen(s) == len && memcmp(t, s, len) == 0;
}
static int float_type_size(type_desc_t *t) {
    const char *b = t->base; int l = t->base_len;
    if (l == 6 && memcmp(b, "浮点", 6) == 0) return 4;
    if (l == 5 && memcmp(b, "float", 5) == 0) return 4;
    return 8;
}

static int type_size(type_desc_t *t) {
    if (t->is_ptr || t->is_array) return 8;
    /* 空 指针 = 8 字节，空 单独声明占 1 字节占位 */
    if (t->base_len == 3 && memcmp(t->base, "空", 3) == 0) return 1;
    if (t->base_len == 4 && memcmp(t->base, "void", 4) == 0) return 1;
    const char *b = t->base; int l = t->base_len;
    if (l == 6 && memcmp(b, "字节", 6) == 0) return 1;
    if (l == 4 && memcmp(b, "byte", 4) == 0) return 1;
    if (l == 6 && memcmp(b, "字符", 6) == 0) return 1;
    if (l == 4 && memcmp(b, "char", 4) == 0) return 1;
    if (l == 6 && memcmp(b, "短整", 6) == 0) return 2;
    if (l == 5 && memcmp(b, "short", 5) == 0) return 2;
    if (l == 6 && memcmp(b, "整数", 6) == 0) return 8;
    if (l == 3 && memcmp(b, "int", 3) == 0) return 8;
    if (l == 6 && memcmp(b, "无符", 6) == 0) return 8;
    if (l == 8 && memcmp(b, "unsigned", 8) == 0) return 8;
    if (l == 6 && memcmp(b, "浮点", 6) == 0) return 4;
    if (l == 5 && memcmp(b, "float", 5) == 0) return 4;
    if (l == 6 && memcmp(b, "双精", 6) == 0) return 8;
    if (l == 6 && memcmp(b, "double", 6) == 0) return 8;
    if (l == 6 && memcmp(b, "长整", 6) == 0) return 8;
    if (l == 4 && memcmp(b, "long", 4) == 0) return 8;
    return 8;
}

static int type_is_unsigned(type_desc_t *t) {
    if (t->is_unsigned) return 1;
    const char *b = t->base; int l = t->base_len;
    if (l == 6 && memcmp(b, "无符", 6) == 0) return 1;
    if (l == 8 && memcmp(b, "unsigned", 8) == 0) return 1;
    return 0;
}

static int is_func_ptr_type(type_desc_t *t) {
    if (!t->is_ptr) return 0;
    const char *b = t->base; int l = t->base_len;
    if (l == 3 && memcmp(b, "函", 3) == 0) return 1;
    if (l == 6 && memcmp(b, "函数", 6) == 0) return 1;
    if (l == 2 && memcmp(b, "fn", 2) == 0) return 1;
    return 0;
}

static int is_float_type(type_desc_t *t) {
    const char *b = t->base; int l = t->base_len;
    if (l == 6 && (memcmp(b, "双精", 6) == 0 || memcmp(b, "浮点", 6) == 0 ||
                   memcmp(b, "double", 6) == 0)) return 1;
    if (l == 5 && memcmp(b, "float", 5) == 0) return 1;
    return 0;
}

/* ---------- 字符串表（去重） ---------- */
#define MAX_STRS 1024
typedef struct { const char *s; int len; } str_entry_t;
static str_entry_t g_strs[MAX_STRS];
static int g_nstrs = 0;

static int str_add(const char *s, int len) {
    for (int i = 0; i < g_nstrs; i++)
        if (g_strs[i].len == len && memcmp(g_strs[i].s, s, len) == 0)
            return i;
    int id = g_nstrs++;
    g_strs[id].s = s;
    g_strs[id].len = len;
    return id;
}

static void emit_strings(void) {
    if (g_nstrs == 0) return;
    emit("    .section .rodata\n");
    for (int i = 0; i < g_nstrs; i++) {
        emit(".Lstr%d:\n    .byte ", i);
        for (int k = 0; k < g_strs[i].len; k++)
            emit("%d, ", (unsigned char)g_strs[i].s[k]);
        emit("0\n");
    }
    emit("    .text\n");
}

/* ---------- 浮点表（去重） ---------- */
#define MAX_FLOATS 256
static double g_floats[MAX_FLOATS];
static int    g_float_sizes[MAX_FLOATS];
static int    g_nfloats = 0;

static int float_add(double v, int size) {
    for (int i = 0; i < g_nfloats; i++)
        if (g_floats[i] == v && g_float_sizes[i] == size) return i;
    int id = g_nfloats++;
    g_floats[id] = v;
    g_float_sizes[id] = size;
    return id;
}

static void emit_floats(void) {
    if (g_nfloats == 0) return;
    emit("    .section .rodata\n");
    for (int i = 0; i < g_nfloats; i++) {
        if (g_float_sizes[i] == 4)
            emit(".Lfloat%d:\n    .float %g\n", i, (float)g_floats[i]);
        else
            emit(".Lfloat%d:\n    .double %.17g\n", i, g_floats[i]);
    }
    emit("    .text\n");
}

static int str_add(const char *s, int len);
static void gen_expr(expr_t *e);
static void gen_binop(const char *op, int len);
static void gen_binop_float(const char *op, int len);

static int expr_is_float(expr_t *e) {
    if (!e) return 0;
    switch (e->kind) {
        case EX_FLOAT: return 1;
        case EX_IDENT: {
            int li = find_local(e->name, e->name_len);
            if (li >= 0) {
                if (g_locals[li].cont_type == 3) return 1;
                return g_locals[li].is_float;
            }
            int gi = find_global(e->name, e->name_len);
            if (gi >= 0) return g_globals[gi].is_float;
            return 0;
        }
        case EX_BINARY: return expr_is_float(e->left) || expr_is_float(e->right);
        case EX_UNARY:  return expr_is_float(e->operand);
        case EX_MEMBER: {
            if (e->left->kind != EX_IDENT) return 0;
            int gi = find_global(e->left->name, e->left->name_len);
            if (gi < 0 || !g_globals[gi].is_struct_inst) return 0;
            struct_def_t *sd = &g_prog->structs[g_globals[gi].struct_idx];
            int fi = find_struct_field(sd, e->name, e->name_len);
            if (fi < 0) return 0;
            return is_float_type(&sd->fields[fi].type);
        }
        default: return 0;
    }
}

static int expr_float_size(expr_t *e) {
    if (!e) return 0;
    if (e->kind == EX_FLOAT) return 8;
    if (e->kind == EX_IDENT) {
        int li = find_local(e->name, e->name_len);
        if (li >= 0 && g_locals[li].is_float) return g_locals[li].float_size;
        int gi = find_global(e->name, e->name_len);
        if (gi >= 0 && g_globals[gi].is_float) return g_globals[gi].float_size;
        return 0;
    }
    if (e->kind == EX_TYPED) {
        if (is_float_type(&e->typed_type)) return float_type_size(&e->typed_type);
        return 0;
    }
    if (e->kind == EX_BINARY) {
        int ls = expr_float_size(e->left);
        int rs = expr_float_size(e->right);
        if (ls && rs) return (ls > rs) ? ls : rs;
        return ls ? ls : rs;
    }
    return 0;
}

static int expr_is_unsigned(expr_t *e) {
    if (!e) return 0;
    if (e->kind == EX_IDENT) {
        int li = find_local(e->name, e->name_len);
        if (li >= 0) return g_locals[li].is_unsigned;
        int gi = find_global(e->name, e->name_len);
        if (gi >= 0) return g_globals[gi].is_unsigned;
        return 0;
    }
    if (e->kind == EX_TYPED) return type_is_unsigned(&e->typed_type);
    if (e->kind == EX_BINARY)
        return expr_is_unsigned(e->left) || expr_is_unsigned(e->right);
    return 0;
}

static int expr_is_ptr(expr_t *e) {
    if (!e) return 0;
    if (e->kind == EX_IDENT) {
        int li = find_local(e->name, e->name_len);
        if (li >= 0) return g_locals[li].is_ptr;
        return 0;
    }
    if (e->kind == EX_UNARY &&
        is_op_text(e->op_text, e->op_len, "&")) return 1;
    return 0;
}

static int expr_is_string(expr_t *e) {
    if (!e) return 0;
    if (e->kind == EX_STRING) return 1;
    if (e->kind == EX_IDENT) {
        int li = find_local(e->name, e->name_len);
        if (li >= 0 && g_locals[li].cont_type == 2) return 1;
        return 0;
    }
    if (e->kind == EX_MEMBER) {
        if (e->left->kind != EX_IDENT) return 0;
        int gi = find_global(e->left->name, e->left->name_len);
        if (gi < 0 || !g_globals[gi].is_struct_inst) return 0;
        struct_def_t *sd = &g_prog->structs[g_globals[gi].struct_idx];
        int fi = find_struct_field(sd, e->name, e->name_len);
        if (fi < 0) return 0;
        type_desc_t *ft = &sd->fields[fi].type;
        if (ft->base_len == 9 && memcmp(ft->base, "字符串", 9) == 0) return 1;
        if (ft->base_len == 6 && memcmp(ft->base, "string", 6) == 0) return 1;
        return 0;
    }
    return 0;
}

static void emit_store_var(int li, int gi, __attribute__((unused)) const char *reg) {
    /* 按变量 size 存 %rax 或 %xmm0 到变量 */
    if (li >= 0) {
        int off = g_locals[li].offset;
        if (g_locals[li].is_float) {
            if (g_locals[li].float_size == 4) {
                emit("    cvtsd2ss %%xmm0, %%xmm1\n");
                emit("    movss %%xmm1, %d(%%rbp)\n", off);
            } else {
                emit("    movsd %%xmm0, %d(%%rbp)\n", off);
            }
        } else if (g_locals[li].size == 1) {
            emit("    movb %%al, %d(%%rbp)\n", off);
        } else if (g_locals[li].size == 2) {
            emit("    movw %%ax, %d(%%rbp)\n", off);
        } else if (g_locals[li].size == 4) {
            emit("    movl %%eax, %d(%%rbp)\n", off);
        } else {
            emit("    movq %%rax, %d(%%rbp)\n", off);
        }
    } else if (gi >= 0) {
        if (g_globals[gi].is_float) {
            if (g_globals[gi].float_size == 4) {
                emit("    cvtsd2ss %%xmm0, %%xmm1\n");
                emit("    movss %%xmm1, g%d(%%rip)\n", g_globals[gi].id);
            } else {
                emit("    movsd %%xmm0, g%d(%%rip)\n", g_globals[gi].id);
            }
        } else {
            emit("    movq %%rax, g%d(%%rip)\n", g_globals[gi].id);
        }
    }
}

static void gen_load_var(const char *name, int len) {
    int li = find_local(name, len);
    if (li >= 0) {
        if (g_locals[li].is_array)      emit("    lea %d(%%rbp), %%rax\n", g_locals[li].offset);
        else if (g_locals[li].is_ref) {
            emit("    movq %d(%%rbp), %%rax\n", g_locals[li].offset);
            if (g_locals[li].is_float) {
                if (g_locals[li].float_size == 4) {
                    emit("    movss (%%rax), %%xmm1\n");
                    emit("    cvtss2sd %%xmm1, %%xmm0\n");
                } else {
                    emit("    movsd (%%rax), %%xmm0\n");
                }
            } else if (g_locals[li].size == 1) {
                if (g_locals[li].is_unsigned) emit("    movzbq (%%rax), %%rax\n");
                else                          emit("    movsbq (%%rax), %%rax\n");
            } else if (g_locals[li].size == 2) {
                if (g_locals[li].is_unsigned) emit("    movzwq (%%rax), %%rax\n");
                else                          emit("    movswq (%%rax), %%rax\n");
            } else if (g_locals[li].size == 4) {
                if (g_locals[li].is_unsigned) emit("    movl (%%rax), %%eax\n");
                else                          emit("    movslq (%%rax), %%rax\n");
            } else {
                emit("    movq (%%rax), %%rax\n");
            }
        }
        else if (g_locals[li].is_float) {
            if (g_locals[li].float_size == 4) {
                emit("    movss %d(%%rbp), %%xmm1\n", g_locals[li].offset);
                emit("    cvtss2sd %%xmm1, %%xmm0\n");
            } else {
                emit("    movsd %d(%%rbp), %%xmm0\n", g_locals[li].offset);
            }
        }
        else if (g_locals[li].size == 1) {
            if (g_locals[li].is_unsigned) emit("    movzbq %d(%%rbp), %%rax\n", g_locals[li].offset);
            else                          emit("    movsbq %d(%%rbp), %%rax\n", g_locals[li].offset);
        }
        else if (g_locals[li].size == 2) {
            if (g_locals[li].is_unsigned) emit("    movzwq %d(%%rbp), %%rax\n", g_locals[li].offset);
            else                          emit("    movswq %d(%%rbp), %%rax\n", g_locals[li].offset);
        }
        else if (g_locals[li].size == 4) {
            if (g_locals[li].is_unsigned) emit("    movl %d(%%rbp), %%eax\n", g_locals[li].offset);
            else                          emit("    movslq %d(%%rbp), %%rax\n", g_locals[li].offset);
        }
        else                              emit("    movq %d(%%rbp), %%rax\n", g_locals[li].offset);
        return;
    }
    int gi = find_global(name, len);
    if (gi >= 0) {
        if (g_globals[gi].is_array)      emit("    lea g%d(%%rip), %%rax\n", g_globals[gi].id);
        else if (g_globals[gi].is_float) emit("    movsd g%d(%%rip), %%xmm0\n", g_globals[gi].id);
        else                             emit("    movq g%d(%%rip), %%rax\n", g_globals[gi].id);
        return;
    }
    fprintf(stderr, "%d: 错误: 未声明的变量 %.*s (g_nlocals=%d)\n",
                g_cur_line, len, name, g_nlocals);
    for (int _i = 0; _i < g_nlocals; _i++)
        fprintf(stderr, "  [%d] %.*s off=%d\n",
                _i, g_locals[_i].len, g_locals[_i].name, g_locals[_i].offset);
    exit(1);
}

static void collect_globals(stmt_t *s) {
    if (!s) return;
    if (s->kind == ST_LET && (s->type.is_static || s->type.is_extern)) {
        int is_f = is_float_type(&s->type);
        int is_arr = s->type.is_array && s->init && s->init->kind == EX_ARRAY_INIT;
        int n = is_arr ? s->init->nargs : 0;
        g_globals[g_nglobals].name = s->name;
        g_globals[g_nglobals].len = s->name_len;
        g_globals[g_nglobals].decl = s;
        g_globals[g_nglobals].is_float = is_f;
        g_globals[g_nglobals].is_array = is_arr;
        g_globals[g_nglobals].arr_len = n;
        g_globals[g_nglobals].id = g_nglobals;
        g_globals[g_nglobals].is_struct_inst = 0;
        g_globals[g_nglobals].is_unsigned = type_is_unsigned(&s->type);
        g_globals[g_nglobals].float_size = is_f ? float_type_size(&s->type) : 0;
        g_globals[g_nglobals].is_extern = s->type.is_extern;
        g_nglobals++;
    }
    switch (s->kind) {
        case ST_BLOCK: for (int i = 0; i < s->nstmts; i++) collect_globals(s->stmts[i]); break;
        case ST_IF: collect_globals(s->then_s); collect_globals(s->else_s); break;
        case ST_WHILE: case ST_FOR: collect_globals(s->body); break;
        default: break;
    }
}

static void collect_struct_insts(void) {
    if (!g_prog) return;
    for (int si = 0; si < g_prog->nstructs; si++) {
        struct_def_t *sd = &g_prog->structs[si];
        for (int ii = 0; ii < sd->ninstances; ii++) {
            if (g_nglobals >= MAX_GLOBALS) return;
            g_globals[g_nglobals].name = sd->inst_name[ii];
            g_globals[g_nglobals].len = sd->inst_name_len[ii];
            g_globals[g_nglobals].decl = 0;
            g_globals[g_nglobals].is_float = 0;
            g_globals[g_nglobals].is_array = 0;
            g_globals[g_nglobals].arr_len = 0;
            g_globals[g_nglobals].id = g_nglobals;
            g_globals[g_nglobals].is_struct_inst = 1;
            g_globals[g_nglobals].struct_idx = si;
            g_globals[g_nglobals].inst_idx = ii;
            g_nglobals++;
        }
    }
}

static void emit_globals(void) {
    if (g_nglobals == 0) return;
    emit("    .data\n");
    for (int i = 0; i < g_nglobals; i++) {
        global_t *g = &g_globals[i];
        if (g->is_extern) continue;
        emit("    .align 8\ng%d:\n", g->id);
        if (g->is_extern) continue;
        if (g->is_struct_inst) {
            struct_def_t *sd = &g_prog->structs[g->struct_idx];
            int inst = g->inst_idx;
            if (sd->is_union) {
                if (sd->inst_str[inst][0])
                    emit("    .quad .Lstr%d\n", str_add(sd->inst_str[inst][0],
                                                          sd->inst_str_len[inst][0]));
                else
                    emit("    .quad %lld\n", (long long)sd->inst_init[inst][0]);
            } else {
                for (int k = 0; k < sd->nfields; k++) {
                    if (sd->inst_str[inst][k])
                        emit("    .quad .Lstr%d\n", str_add(sd->inst_str[inst][k],
                                                              sd->inst_str_len[inst][k]));
                    else
                        emit("    .quad %lld\n", (long long)sd->inst_init[inst][k]);
                }
            }
        } else {
            stmt_t *d = g->decl;
            if (g->is_array) {
                for (int k = 0; k < g->arr_len; k++) {
                    expr_t *el = d->init->args[k];
                    if (el->kind == EX_INT) emit("    .quad %lld\n", (long long)el->ival);
                    else if (el->kind == EX_FLOAT) emit("    .double %.17g\n", el->fval);
                    else emit("    .quad 0\n");
                }
            } else if (g->is_float) {
                double v = (d->init && d->init->kind == EX_FLOAT) ? d->init->fval : 0.0;
                emit("    .double %.17g\n", v);
            } else {
                long long v = (d->init && d->init->kind == EX_INT) ? (long long)d->init->ival : 0;
                emit("    .quad %lld\n", v);
            }
        }
    }
    emit("    .text\n");
}

static void emit_func_symbol(const char *fn, int fnlen) {
    if (g_cur_module) {
        int idx = find_module_func_idx(g_cur_module, g_cur_module_len, fn, fnlen);
        if (idx >= 0) {
            emit("%.*s_%.*s", g_modules_codegen[idx].name_len,
                 g_modules_codegen[idx].name, fnlen, fn);
            return;
        }
    }
    int is_main = (fnlen == 3 && memcmp(fn, "主", 3) == 0) ||
                  (fnlen == 4 && memcmp(fn, "main", 4) == 0);
    if (is_main) emit("main");
    else         emit("%.*s", fnlen, fn);
}

static void gen_call(expr_t *e) {
    int n = e->nargs;

    /* 间接调用：(*f)(...) */
    if (e->operand->kind == EX_UNARY &&
        is_op_text(e->operand->op_text, e->operand->op_len, "*") &&
        e->operand->operand->kind == EX_IDENT) {
        const char *vn = e->operand->operand->name;
        int vnl = e->operand->operand->name_len;
        int vli = find_local(vn, vnl);
        int vgi = (vli < 0) ? find_global(vn, vnl) : -1;
        if (vli < 0 && vgi < 0) {
            fprintf(stderr, "%d: 错误: 未声明的函数指针 %.*s\n", g_cur_line, vnl, vn);
            exit(1);
        }
        static const char *regs_linux[]   = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
        static const char *regs_windows[] = {"%rcx", "%rdx", "%r8",  "%r9",  "%r10", "%r11"};
        const char **regs = g_target_windows ? regs_windows : regs_linux;
        for (int i = n - 1; i >= 0; i--) {
            gen_expr(e->args[i]);
            emit("    push %%rax\n");
        }
        for (int i = 0; i < n && i < 6; i++)
            emit("    pop %s\n", regs[i]);
        if (vli >= 0) emit("    movq %d(%%rbp), %%r11\n", g_locals[vli].offset);
        else          emit("    movq g%d(%%rip), %%r11\n", g_globals[vgi].id);
        emit("    movl $0, %%eax\n");
        emit("    call *%%r11\n");
        return;
    }

    const char *fn = e->operand->name;
    int fnlen = e->operand->name_len;
    int is_print = (fnlen == 6 && memcmp(fn, "打印", 6) == 0) ||
                   (fnlen == 5 && memcmp(fn, "print", 5) == 0);
    if (is_print) {
        for (int i = 0; i < n; i++) {
            int is_f = expr_is_float(e->args[i]);
            int is_str = expr_is_string(e->args[i]);
            gen_expr(e->args[i]);
            if (is_f) {
                if (g_target_windows) emit("    lea .Lfmt_f(%%rip), %%rcx\n");
                else                  emit("    lea .Lfmt_f(%%rip), %%rdi\n");
                emit("    movl $1, %%eax\n");
            } else if (is_str) {
                if (g_target_windows) { emit("    mov %%rax, %%rdx\n    lea .Lfmt_s(%%rip), %%rcx\n"); }
                else                  { emit("    mov %%rax, %%rsi\n    lea .Lfmt_s(%%rip), %%rdi\n"); }
                emit("    movl $0, %%eax\n");
            } else {
                int is_u = expr_is_unsigned(e->args[i]);
                const char *fmt = is_u ? ".Lfmt_u" : ".Lfmt_d";
                if (g_target_windows) { emit("    mov %%rax, %%rdx\n    lea %s(%%rip), %%rcx\n", fmt); }
                else                  { emit("    mov %%rax, %%rsi\n    lea %s(%%rip), %%rdi\n", fmt); }
                emit("    movl $0, %%eax\n");
            }
            emit("    call printf\n");
        }
        emit("    movq $0, %%rax\n");
        return;
    }
    static const char *regs_linux[]   = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    static const char *regs_windows[] = {"%rcx", "%rdx", "%r8",  "%r9",  "%r10", "%r11"};
    const char **regs = g_target_windows ? regs_windows : regs_linux;
    func_sig_t *sig = find_func_sig(fn, fnlen);

    /* 参数求值入栈暂存（从右到左） */
    for (int i = n - 1; i >= 0; i--) {
        int is_f = (sig && i < sig->nparams) ? sig->param_is_float[i]
                                             : expr_is_float(e->args[i]);
        gen_expr(e->args[i]);
        if (is_f) {
            emit("    sub $8, %%rsp\n");
            emit("    movsd %%xmm0, (%%rsp)\n");
        } else {
            emit("    push %%rax\n");
        }
    }

    /* 从左到右 pop 到对应寄存器 */
    int int_idx = 0, float_idx = 0;
    for (int i = 0; i < n; i++) {
        int is_f = (sig && i < sig->nparams) ? sig->param_is_float[i]
                                             : expr_is_float(e->args[i]);
        if (is_f) {
            emit("    movsd (%%rsp), %%xmm%d\n", float_idx);
            emit("    add $8, %%rsp\n");
            float_idx++;
        } else {
            emit("    pop %s\n", regs[int_idx]);
            int_idx++;
        }
    }

    emit("    movl $0, %%eax\n");
    emit("    call ");
    emit_func_symbol(fn, fnlen);
    emit("\n");
}

static void gen_expr(expr_t *e) {
    if (!e) return;
    if (e->line > 0) g_cur_line = e->line;
    switch (e->kind) {
        case EX_INT: emit("    movq $%lld, %%rax\n", (long long)e->ival); break;
        case EX_FLOAT: { int id = float_add(e->fval, expr_float_size(e)); emit("    movsd .Lfloat%d(%%rip), %%xmm0\n", id); break; }
        case EX_STRING: { int id = str_add(e->name, e->name_len); emit("    lea .Lstr%d(%%rip), %%rax\n", id); break; }
        case EX_IDENT: gen_load_var(e->name, e->name_len); break;
        case EX_MEMBER: {
            if (e->left->kind != EX_IDENT) { fprintf(stderr, "错误: . 左边须是实例名\n"); exit(1); }
            int gi = find_global(e->left->name, e->left->name_len);
            if (gi < 0 || !g_globals[gi].is_struct_inst) {
                fprintf(stderr, "错误: 未声明的实例 %.*s\n", e->left->name_len, e->left->name);
                exit(1);
            }
            struct_def_t *sd = &g_prog->structs[g_globals[gi].struct_idx];
            int fi = find_struct_field(sd, e->name, e->name_len);
            if (fi < 0) { fprintf(stderr, "错误: 结构无字段 %.*s\n", e->name_len, e->name); exit(1); }
            if (is_float_type(&sd->fields[fi].type)) {
                emit("    lea g%d(%%rip), %%rax\n", g_globals[gi].id);
                emit("    movsd %d(%%rax), %%xmm0\n", sd->fields[fi].offset);
            } else {
                emit("    lea g%d(%%rip), %%rax\n", g_globals[gi].id);
                emit("    mov %d(%%rax), %%rax\n", sd->fields[fi].offset);
            }
            break;
        }
        case EX_TYPED:
            gen_expr(e->operand);
            break;
        case EX_CONVERT: {
            gen_expr(e->left);
            int src_f = 0;
            /* 源类型：从 EX_TYPED 或已推 */
            if (e->left->kind == EX_TYPED)
                src_f = is_float_type(&e->left->typed_type);
            else
                src_f = expr_is_float(e->left);
            int dst_f = is_float_type(&e->typed_type);
            if (src_f && !dst_f) {
                emit("    cvttsd2si %%xmm0, %%rax\n");
            } else if (!src_f && dst_f) {
                emit("    pxor %%xmm0, %%xmm0\n");
                emit("    cvtsi2sd %%rax, %%xmm0\n");
            }
            break;
        }
        case EX_ARROW: {
            /* p->x：p 是指针，字段名全局搜索 */
            gen_expr(e->left);
            int fi = -1;
            for (int si = 0; si < g_prog->nstructs && fi < 0; si++) {
                fi = find_struct_field(&g_prog->structs[si], e->name, e->name_len);
                if (fi >= 0) {
                    int off = g_prog->structs[si].fields[fi].offset;
                    emit("    mov %d(%%rax), %%rax\n", off);
                    break;
                }
            }
            if (fi < 0) { fprintf(stderr, "错误: 无字段 %.*s\n", e->name_len, e->name); exit(1); }
            break;
        }
        case EX_UNARY:
            if ((is_op_text(e->op_text, e->op_len, "++") ||
                 is_op_text(e->op_text, e->op_len, "--")) &&
                e->operand->kind == EX_IDENT) {
                int delta = is_op_text(e->op_text, e->op_len, "++") ? 1 : -1;
                int li = find_local(e->operand->name, e->operand->name_len);
                int gi = (li < 0) ? find_global(e->operand->name, e->operand->name_len) : -1;
                if (li >= 0) {
                    int off = g_locals[li].offset;
                    if (e->is_postfix) {
                        emit("    movq %d(%%rbp), %%rax\n", off);
                        emit("    lea %d(%%rax), %%rcx\n", delta);
                        emit("    movq %%rcx, %d(%%rbp)\n", off);
                    } else {
                        emit("    addq $%d, %d(%%rbp)\n", delta, off);
                        emit("    movq %d(%%rbp), %%rax\n", off);
                    }
                } else if (gi >= 0) {
                    if (e->is_postfix) {
                        emit("    movq g%d(%%rip), %%rax\n", g_globals[gi].id);
                        emit("    lea %d(%%rax), %%rcx\n", delta);
                        emit("    movq %%rcx, g%d(%%rip)\n", g_globals[gi].id);
                    } else {
                        emit("    addq $%d, g%d(%%rip)\n", delta, g_globals[gi].id);
                        emit("    movq g%d(%%rip), %%rax\n", g_globals[gi].id);
                    }
                }
                break;
            }
            if (is_op_text(e->op_text, e->op_len, "&") && e->operand->kind == EX_IDENT) {
                int li = find_local(e->operand->name, e->operand->name_len);
                if (li >= 0) { emit("    lea %d(%%rbp), %%rax\n", g_locals[li].offset); break; }
                int gi = find_global(e->operand->name, e->operand->name_len);
                if (gi >= 0) { emit("    lea g%d(%%rip), %%rax\n", g_globals[gi].id); break; }
                fprintf(stderr, "错误: 未声明的变量\n"); exit(1);
            }
            gen_expr(e->operand);
            if (expr_is_float(e)) {
                if (is_op_text(e->op_text, e->op_len, "-")) {
                    emit("    xorpd %%xmm1, %%xmm1\n    subsd %%xmm0, %%xmm1\n    movapd %%xmm1, %%xmm0\n");
                }
            } else {
                if      (is_op_text(e->op_text, e->op_len, "-")) emit("    neg %%rax\n");
                else if (is_op_text(e->op_text, e->op_len, "!")) emit("    test %%rax, %%rax\n    setz %%al\n    movzbq %%al, %%rax\n");
                else if (is_op_text(e->op_text, e->op_len, "~")) emit("    not %%rax\n");
                else if (is_op_text(e->op_text, e->op_len, "*")) emit("    mov (%%rax), %%rax\n");
            }
            break;
        case EX_BINARY: {
            int is_f = expr_is_float(e->left) || expr_is_float(e->right);
            if (is_f) {
                gen_expr(e->right);
                emit("    sub $8, %%rsp\n    movsd %%xmm0, (%%rsp)\n");
                gen_expr(e->left);
                emit("    movsd (%%rsp), %%xmm1\n    add $8, %%rsp\n");
                g_cur_float_is_4 = 0;
                gen_binop_float(e->op_text, e->op_len);
            } else {
                int lp = expr_is_ptr(e->left);
                int rp = expr_is_ptr(e->right);
                int is_add = is_op_text(e->op_text, e->op_len, "+");
                int is_sub = is_op_text(e->op_text, e->op_len, "-");
                if ((lp || rp) && (is_add || is_sub)) {
                    /* 指针算术：把偏移量乘 8 */
                    gen_expr(e->right);
                    if (rp && !lp) {
                        /* n + p 或 n - p（少见），偏移量在左 */
                        emit("    movq %%rax, %%r15\n");
                        gen_expr(e->left);
                        emit("    shl $3, %%r15\n");
                        if (is_add) emit("    add %%r15, %%rax\n");
                        else        emit("    sub %%r15, %%rax\n");
                    } else {
                        emit("    shl $3, %%rax\n");
                        emit("    push %%rax\n");
                        gen_expr(e->left);
                        emit("    pop %%rcx\n");
                        if (is_add) emit("    add %%rcx, %%rax\n");
                        else        emit("    sub %%rcx, %%rax\n");
                    }
                } else {
                    gen_expr(e->right);
                    emit("    push %%rax\n");
                    gen_expr(e->left);
                    emit("    pop %%rcx\n");
                    g_binop_unsigned = expr_is_unsigned(e->left) ||
                                       expr_is_unsigned(e->right);
                    gen_binop(e->op_text, e->op_len);
                }
            }
            break;
        }
        case EX_INDEX:
            gen_expr(e->left);
            emit("    push %%rax\n");
            gen_expr(e->right);
            emit("    mov %%rax, %%rcx\n    pop %%rax\n");
            emit("    mov (%%rax, %%rcx, 8), %%rax\n");
            break;
        case EX_ASSIGN: {
            if (e->left->kind == EX_IDENT) {
                int li = find_local(e->left->name, e->left->name_len);
                int gi = (li < 0) ? find_global(e->left->name, e->left->name_len) : -1;
                if (li < 0 && gi < 0) { fprintf(stderr, "错误: 未声明的变量\n"); exit(1); }

                /* 左侧是引用：写到被引用变量 */
                if (li >= 0 && g_locals[li].is_ref) {
                    gen_expr(e->right);
                    emit("    movq %d(%%rbp), %%rcx\n", g_locals[li].offset);
                    if (g_locals[li].is_float) {
                        if (g_locals[li].float_size == 4) {
                            emit("    cvtsd2ss %%xmm0, %%xmm1\n");
                            emit("    movss %%xmm1, (%%rcx)\n");
                        } else {
                            emit("    movsd %%xmm0, (%%rcx)\n");
                        }
                    } else if (g_locals[li].size == 1) {
                        emit("    movb %%al, (%%rcx)\n");
                    } else if (g_locals[li].size == 2) {
                        emit("    movw %%ax, (%%rcx)\n");
                    } else if (g_locals[li].size == 4) {
                        emit("    movl %%eax, (%%rcx)\n");
                    } else {
                        emit("    movq %%rax, (%%rcx)\n");
                    }
                    break;
                }

                /* 右值是返回浮点的函数调用 */
                if (e->right->kind == EX_CALL) {
                    func_sig_t *sig = find_func_sig(e->right->operand->name,
                                                     e->right->operand->name_len);
                    if (sig && sig->is_float_ret) {
                        gen_expr(e->right);
                        if (li >= 0) {
                            if (g_locals[li].float_size == 4) {
                                emit("    cvtsd2ss %%xmm0, %%xmm1\n");
                                emit("    movss %%xmm1, %d(%%rbp)\n", g_locals[li].offset);
                            } else {
                                emit("    movsd %%xmm0, %d(%%rbp)\n", g_locals[li].offset);
                            }
                        } else {
                            if (g_globals[gi].float_size == 4) {
                                emit("    cvtsd2ss %%xmm0, %%xmm1\n");
                                emit("    movss %%xmm1, g%d(%%rip)\n", g_globals[gi].id);
                            } else {
                                emit("    movsd %%xmm0, g%d(%%rip)\n", g_globals[gi].id);
                            }
                        }
                        break;
                    }
                }

                /* 复合赋值 */
                if (e->op_len >= 2 && e->op_text[e->op_len-1] == '=' &&
                    !is_op_text(e->op_text, e->op_len, "==") &&
                    !is_op_text(e->op_text, e->op_len, "\\=") &&
                    !is_op_text(e->op_text, e->op_len, "<=") &&
                    !is_op_text(e->op_text, e->op_len, ">=")) {
                    if (li >= 0) emit("    movq %d(%%rbp), %%rax\n", g_locals[li].offset);
                    else         emit("    movq g%d(%%rip), %%rax\n", g_globals[gi].id);
                    emit("    push %%rax\n");
                    gen_expr(e->right);
                    emit("    mov %%rax, %%rcx\n    pop %%rax\n");
                    if      (is_op_text(e->op_text, e->op_len, "+="))  emit("    add %%rcx, %%rax\n");
                    else if (is_op_text(e->op_text, e->op_len, "-="))  emit("    sub %%rcx, %%rax\n");
                    else if (is_op_text(e->op_text, e->op_len, "*="))  emit("    imul %%rcx, %%rax\n");
                    else if (is_op_text(e->op_text, e->op_len, "/=")) { emit("    cqto\n    idiv %%rcx\n"); }
                    else if (is_op_text(e->op_text, e->op_len, "%=")) { emit("    cqto\n    idiv %%rcx\n    mov %%rdx, %%rax\n"); }
                    else if (is_op_text(e->op_text, e->op_len, "&="))  emit("    and %%rcx, %%rax\n");
                    else if (is_op_text(e->op_text, e->op_len, "|="))  emit("    or  %%rcx, %%rax\n");
                    else if (is_op_text(e->op_text, e->op_len, "^="))  emit("    xor %%rcx, %%rax\n");
                    else if (is_op_text(e->op_text, e->op_len, "<<=")) emit("    shl %%cl, %%rax\n");
                    else if (is_op_text(e->op_text, e->op_len, ">>=")) emit("    sar %%cl, %%rax\n");
                    emit_store_var(li, gi, "%%rax");
                    break;
                }

                gen_expr(e->right);
                if (li >= 0) {
                    if (g_locals[li].is_range && g_locals[li].range_is_float) {
                        /* 浮点区间检查：comisd */
                        int L_skip = new_label();
                        if (g_locals[li].range_lo_f != -1.0/0.0 || !g_locals[li].range_lo_open) {
                            int id = float_add(g_locals[li].range_lo_f, 8);
                            emit("    movsd .Lfloat%d(%%rip), %%xmm1\n", id);
                            emit("    comisd %%xmm1, %%xmm0\n");
                            if (g_locals[li].range_lo_open) emit("    jbe .L%d\n", L_skip);
                            else                            emit("    jb  .L%d\n", L_skip);
                        }
                        if (g_locals[li].range_hi_f != 1.0/0.0 || !g_locals[li].range_hi_open) {
                            int id = float_add(g_locals[li].range_hi_f, 8);
                            emit("    movsd .Lfloat%d(%%rip), %%xmm1\n", id);
                            emit("    comisd %%xmm1, %%xmm0\n");
                            if (g_locals[li].range_hi_open) emit("    jae .L%d\n", L_skip);
                            else                            emit("    ja  .L%d\n", L_skip);
                        }
                        emit("    movsd %%xmm0, %d(%%rbp)\n", g_locals[li].offset);
                        emit(".L%d:\n", L_skip);
                    }
                    else if (g_locals[li].is_range) {
                        int L_skip = new_label();
                        emit("    movq %%rax, %%rcx\n");
                        if (g_locals[li].range_lo != INT64_MIN || !g_locals[li].range_lo_open) {
                            emit("    movq $%lld, %%rdx\n", (long long)g_locals[li].range_lo);
                            emit("    cmp %%rdx, %%rcx\n");
                            if (g_locals[li].range_lo_open) emit("    jle .L%d\n", L_skip);
                            else                            emit("    jl  .L%d\n", L_skip);
                        }
                        if (g_locals[li].range_hi != INT64_MAX || !g_locals[li].range_hi_open) {
                            emit("    movq $%lld, %%rdx\n", (long long)g_locals[li].range_hi);
                            emit("    cmp %%rdx, %%rcx\n");
                            if (g_locals[li].range_hi_open) emit("    jge .L%d\n", L_skip);
                            else                            emit("    jg  .L%d\n", L_skip);
                        }
                        emit("    movq %%rcx, %d(%%rbp)\n", g_locals[li].offset);
                        emit(".L%d:\n", L_skip);
                    }
                    else if (g_locals[li].is_float) {
                        if (g_locals[li].float_size == 4) {
                            emit("    cvtsd2ss %%xmm0, %%xmm1\n");
                            emit("    movss %%xmm1, %d(%%rbp)\n", g_locals[li].offset);
                        } else {
                            emit("    movsd %%xmm0, %d(%%rbp)\n", g_locals[li].offset);
                        }
                    }
                    else {
                        if (g_locals[li].size == 1) emit("    movb %%al, %d(%%rbp)\n", g_locals[li].offset);
                        else if (g_locals[li].size == 2) emit("    movw %%ax, %d(%%rbp)\n", g_locals[li].offset);
                        else if (g_locals[li].size == 4) emit("    movl %%eax, %d(%%rbp)\n", g_locals[li].offset);
                        else emit("    movq %%rax, %d(%%rbp)\n", g_locals[li].offset);
                    }
                } else {
                    if (g_globals[gi].is_float) {
                        if (g_globals[gi].float_size == 4) {
                            emit("    cvtsd2ss %%xmm0, %%xmm1\n");
                            emit("    movss %%xmm1, g%d(%%rip)\n", g_globals[gi].id);
                        } else {
                            emit("    movsd %%xmm0, g%d(%%rip)\n", g_globals[gi].id);
                        }
                    }
                    else                        emit("    movq %%rax, g%d(%%rip)\n", g_globals[gi].id);
                }
            }
            else if (e->left->kind == EX_MEMBER) {
                if (e->left->left->kind != EX_IDENT) { fprintf(stderr, "错误\n"); exit(1); }
                int gi = find_global(e->left->left->name, e->left->left->name_len);
                struct_def_t *sd = &g_prog->structs[g_globals[gi].struct_idx];
                int fi = find_struct_field(sd, e->left->name, e->left->name_len);
                gen_expr(e->right);
                emit("    mov %%rax, %%rcx\n");
                emit("    lea g%d(%%rip), %%rax\n", g_globals[gi].id);
                emit("    mov %%rcx, %d(%%rax)\n", sd->fields[fi].offset);
            }
            else if (e->left->kind == EX_ARROW) {
                gen_expr(e->left->left);       /* rax = 指针 */
                emit("    push %%rax\n");
                gen_expr(e->right);            /* rax = 值 */
                emit("    pop %%rcx\n");       /* rcx = 指针 */
                int fi = -1;
                for (int si = 0; si < g_prog->nstructs && fi < 0; si++) {
                    fi = find_struct_field(&g_prog->structs[si], e->left->name, e->left->name_len);
                    if (fi >= 0) {
                        int off = g_prog->structs[si].fields[fi].offset;
                        emit("    mov %%rax, %d(%%rcx)\n", off);
                        break;
                    }
                }
                if (fi < 0) { fprintf(stderr, "错误\n"); exit(1); }
            }
            else if (e->left->kind == EX_UNARY &&
                     is_op_text(e->left->op_text, e->left->op_len, "*") &&
                     e->left->operand->kind == EX_IDENT) {
                int li2 = find_local(e->left->operand->name, e->left->operand->name_len);
                /* 函数指针绑定：*f = 函数名; */
                if (li2 >= 0 && g_locals[li2].is_func_ptr &&
                    e->right->kind == EX_IDENT) {
                    emit("    lea %.*s(%%rip), %%rax\n",
                         e->right->name_len, e->right->name);
                    emit("    movq %%rax, %d(%%rbp)\n", g_locals[li2].offset);
                    break;
                }
                /* 普通指针解引用写 */
                gen_expr(e->right);
                if (li2 >= 0) {
                    emit("    movq %d(%%rbp), %%rcx\n", g_locals[li2].offset);
                } else {
                    int gi2 = find_global(e->left->operand->name, e->left->operand->name_len);
                    if (gi2 >= 0) emit("    movq g%d(%%rip), %%rcx\n", g_globals[gi2].id);
                }
                emit("    movq %%rax, (%%rcx)\n");
            }
            else if (e->left->kind == EX_INDEX) {
                gen_expr(e->left->left);
                emit("    push %%rax\n");
                gen_expr(e->left->right);
                emit("    push %%rax\n");
                gen_expr(e->right);
                emit("    pop %%rcx\n    pop %%rdx\n    movq %%rax, (%%rdx, %%rcx, 8)\n");
            }
            break;
        }
        case EX_TERNARY: {
            int L_else = new_label();
            int L_end  = new_label();
            gen_expr(e->left);
            emit("    test %%rax, %%rax\n");
            emit("    jz .L%d\n", L_else);
            gen_expr(e->right);
            emit("    jmp .L%d\n", L_end);
            emit(".L%d:\n", L_else);
            gen_expr(e->operand);
            emit(".L%d:\n", L_end);
            break;
        }
        case EX_COMMA:
            gen_expr(e->left);
            gen_expr(e->right);
            break;
        case EX_SIZEOF: {
            int sz = type_size(&e->typed_type);
            emit("    movq $%d, %%rax\n", sz);
            break;
        }
        case EX_CALL: gen_call(e); break;
        default: break;
    }
}

static void gen_binop(const char *op, int len) {
    if      (is_op_text(op, len, "+")) emit("    add %%rcx, %%rax\n");
    else if (is_op_text(op, len, "-")) emit("    sub %%rcx, %%rax\n");
    else if (is_op_text(op, len, "*")) emit("    imul %%rcx, %%rax\n");
    else if (is_op_text(op, len, "**")) {
        /* 整数幂：输入 base=%rax, exp=%rcx，输出 %rax */
        int L_top = new_label();
        int L_end = new_label();
        emit("    mov %%rcx, %%r15\n");
        emit("    mov $1, %%r14\n");
        emit("    test %%r15, %%r15\n");
        emit("    jle .L%d\n", L_end);
        emit(".L%d:\n", L_top);
        emit("    imul %%rax, %%r14\n");
        emit("    dec %%r15\n");
        emit("    jnz .L%d\n", L_top);
        emit(".L%d:\n", L_end);
        emit("    mov %%r14, %%rax\n");
    }
    else if (is_op_text(op, len, "/")) {
        if (g_binop_unsigned) emit("    xor %%rdx, %%rdx\n    div %%rcx\n");
        else                  emit("    cqto\n    idiv %%rcx\n");
    }
    else if (is_op_text(op, len, "%")) {
        if (g_binop_unsigned) emit("    xor %%rdx, %%rdx\n    div %%rcx\n    mov %%rdx, %%rax\n");
        else                  emit("    cqto\n    idiv %%rcx\n    mov %%rdx, %%rax\n");
    }
    else if (is_op_text(op, len, "==")) emit("    cmp %%rcx, %%rax\n    sete %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, "\\=")) emit("    cmp %%rcx, %%rax\n    setne %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, "<")) {
        emit("    cmp %%rcx, %%rax\n");
        if (g_binop_unsigned) emit("    setb %%al\n");
        else                  emit("    setl %%al\n");
        emit("    movzbq %%al, %%rax\n");
    }
    else if (is_op_text(op, len, ">")) {
        emit("    cmp %%rcx, %%rax\n");
        if (g_binop_unsigned) emit("    seta %%al\n");
        else                  emit("    setg %%al\n");
        emit("    movzbq %%al, %%rax\n");
    }
    else if (is_op_text(op, len, "<=")) {
        emit("    cmp %%rcx, %%rax\n");
        if (g_binop_unsigned) emit("    setbe %%al\n");
        else                  emit("    setle %%al\n");
        emit("    movzbq %%al, %%rax\n");
    }
    else if (is_op_text(op, len, ">=")) {
        emit("    cmp %%rcx, %%rax\n");
        if (g_binop_unsigned) emit("    setae %%al\n");
        else                  emit("    setge %%al\n");
        emit("    movzbq %%al, %%rax\n");
    }
    else if (is_op_text(op, len, "&"))  emit("    and %%rcx, %%rax\n");
    else if (is_op_text(op, len, "|"))  emit("    or  %%rcx, %%rax\n");
    else if (is_op_text(op, len, "^"))  emit("    xor %%rcx, %%rax\n");
    else if (is_op_text(op, len, "<<")) emit("    shl %%cl, %%rax\n");
    else if (is_op_text(op, len, ">>")) {
        if (g_binop_unsigned) emit("    shr %%cl, %%rax\n");
        else                  emit("    sar %%cl, %%rax\n");
    }
    else if (is_op_text(op, len, "&&")) emit("    and %%rcx, %%rax\n");
    else if (is_op_text(op, len, "||")) emit("    or  %%rcx, %%rax\n");
}

static void gen_binop_float(const char *op, int len) {
    const char *suf = g_cur_float_is_4 ? "ss" : "sd";
    if      (is_op_text(op, len, "+")) emit("    add%s %%xmm1, %%xmm0\n", suf);
    else if (is_op_text(op, len, "-")) emit("    sub%s %%xmm1, %%xmm0\n", suf);
    else if (is_op_text(op, len, "*")) emit("    mul%s %%xmm1, %%xmm0\n", suf);
    else if (is_op_text(op, len, "/")) emit("    div%s %%xmm1, %%xmm0\n", suf);
    else if (is_op_text(op, len, "==")) emit("    comisd %%xmm1, %%xmm0\n    sete %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, "\\=")) emit("    comisd %%xmm1, %%xmm0\n    setne %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, "<"))  emit("    comisd %%xmm1, %%xmm0\n    setb %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, ">"))  emit("    comisd %%xmm1, %%xmm0\n    seta %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, "<=")) emit("    comisd %%xmm1, %%xmm0\n    setbe %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, ">=")) emit("    comisd %%xmm1, %%xmm0\n    setae %%al\n    movzbq %%al, %%rax\n");
}

/* 扫描函数体，统计需要的栈空间（字节） */
static int scan_frame(stmt_t *s) {
    if (!s) return 0;
    int bytes = 0;
    switch (s->kind) {
        case ST_LET: {
            if (s->type.is_static) break;
            if (s->type.is_array && s->init && s->init->kind == EX_ARRAY_INIT)
                bytes += 8 * s->init->nargs;
            else
                bytes += 8;
            break;
        }
        case ST_BLOCK:
            for (int i = 0; i < s->nstmts; i++) bytes += scan_frame(s->stmts[i]);
            break;
        case ST_IF:
            bytes += scan_frame(s->then_s);
            bytes += scan_frame(s->else_s);
            break;
        case ST_WHILE:
        case ST_FOR:
            bytes += scan_frame(s->body);
            if (s->kind == ST_FOR) bytes += scan_frame(s->for_init);
            break;
        default: break;
    }
    return bytes;
}

static void gen_stmt(stmt_t *s) {
    if (!s) return;
    if (s->line > 0) g_cur_line = s->line;
    switch (s->kind) {
        case ST_LET: {
            if (s->type.is_static) break;

            /* 引用声明：b 为 引用 a; */
            if (s->type.is_ref && s->init && s->init->kind == EX_IDENT) {
                int tli = find_local(s->init->name, s->init->name_len);
                int tgi = (tli < 0) ? find_global(s->init->name, s->init->name_len) : -1;
                if (tli < 0 && tgi < 0) {
                    fprintf(stderr, "%d: 错误: 被引用变量 %.*s 未声明\n",
                            g_cur_line, s->init->name_len, s->init->name);
                    exit(1);
                }
                if (tli >= 0) emit("    lea %d(%%rbp), %%rax\n", g_locals[tli].offset);
                else          emit("    lea g%d(%%rip), %%rax\n", g_globals[tgi].id);

                int off = local_add(s->name, s->name_len);
                int li = find_local(s->name, s->name_len);
                g_locals[li].is_ref = 1;
                g_locals[li].size = 8;
                if (tli >= 0) {
                    g_locals[li].is_float    = g_locals[tli].is_float;
                    g_locals[li].float_size  = g_locals[tli].float_size;
                    g_locals[li].is_unsigned = g_locals[tli].is_unsigned;
                    g_locals[li].size        = g_locals[tli].size;
                }
                emit("    movq %%rax, %d(%%rbp)\n", off);
                break;
            }

            /* 修改 + 改类型 */
            if (s->modify_retype) {
                int li = find_local(s->name, s->name_len);
                int gi = (li < 0) ? find_global(s->name, s->name_len) : -1;
                if (li < 0 && gi < 0) {
                    fprintf(stderr, "%d: 错误: 未声明的变量 %.*s\n",
                            g_cur_line, s->name_len, s->name);
                    exit(1);
                }
                int new_is_f = is_float_type(&s->modify_new_type);
                if (li >= 0) g_locals[li].is_float = new_is_f;
                else         g_globals[gi].is_float = new_is_f;

                if (s->init) {
                    gen_expr(s->init);
                    if (new_is_f) {
                        if (s->init->kind == EX_INT) {
                            emit("    pxor %%xmm0, %%xmm0\n");
                            emit("    cvtsi2sd %%rax, %%xmm0\n");
                        }
                        if (li >= 0) emit("    movsd %%xmm0, %d(%%rbp)\n", g_locals[li].offset);
                        else         emit("    movsd %%xmm0, g%d(%%rip)\n", g_globals[gi].id);
                    } else {
                        if (li >= 0) emit("    movq %%rax, %d(%%rbp)\n", g_locals[li].offset);
                        else         emit("    movq %%rax, g%d(%%rip)\n", g_globals[gi].id);
                    }
                }
                break;
            }

            int is_f = is_float_type(&s->type);
            int is_rng = s->type.is_range;
            int is_pt = s->type.is_ptr;
            if (s->type.is_array && s->init && s->init->kind == EX_ARRAY_INIT) {
                int n = s->init->nargs;
                int base = local_add_ex(s->name, s->name_len, 1, n, 0);
                for (int i = 0; i < n; i++) {
                    gen_expr(s->init->args[i]);
                    emit("    movq %%rax, %d(%%rbp)\n", base + i * 8);
                }
            } else if (is_f) {
                if (s->init) gen_expr(s->init);
                else emit("    pxor %%xmm0, %%xmm0\n");
                int off = local_add_ex(s->name, s->name_len, 0, 1, 1);
                int fs = float_type_size(&s->type);
                {
                    int li = find_local(s->name, s->name_len);
                    g_locals[li].size = type_size(&s->type);
                    g_locals[li].is_unsigned = type_is_unsigned(&s->type);
                    g_locals[li].float_size = fs;
                    if (is_rng) {
                        g_locals[li].is_range       = 1;
                        g_locals[li].range_lo       = s->type.range_lo;
                        g_locals[li].range_hi       = s->type.range_hi;
                        g_locals[li].range_lo_open  = s->type.range_lo_open;
                        g_locals[li].range_hi_open  = s->type.range_hi_open;
                        g_locals[li].range_lo_f     = s->type.range_lo_f;
                        g_locals[li].range_hi_f     = s->type.range_hi_f;
                        g_locals[li].range_is_float = s->type.range_is_float;
                    }
                }
                if (fs == 4) {
                    emit("    cvtsd2ss %%xmm0, %%xmm1\n");
                    emit("    movss %%xmm1, %d(%%rbp)\n", off);
                } else {
                    emit("    movsd %%xmm0, %d(%%rbp)\n", off);
                }
            } else {
                if (s->init) gen_expr(s->init);
                else emit("    movq $0, %%rax\n");
                int off = local_add(s->name, s->name_len);  (void)off;
                {
                    int li = find_local(s->name, s->name_len);
                    g_locals[li].size = type_size(&s->type);
                    g_locals[li].is_unsigned = type_is_unsigned(&s->type);
                    if (is_f) g_locals[li].float_size = float_type_size(&s->type);
                    if (is_rng) {
                        g_locals[li].is_range       = 1;
                        g_locals[li].range_lo       = s->type.range_lo;
                        g_locals[li].range_hi       = s->type.range_hi;
                        g_locals[li].range_lo_open  = s->type.range_lo_open;
                        g_locals[li].range_hi_open  = s->type.range_hi_open;
                        g_locals[li].range_lo_f     = s->type.range_lo_f;
                        g_locals[li].range_hi_f     = s->type.range_hi_f;
                        g_locals[li].range_is_float = s->type.range_is_float;
                    }
                    if (is_pt) g_locals[li].is_ptr = 1;
                    if (is_func_ptr_type(&s->type)) {
                        g_locals[li].is_func_ptr = 1;
                        g_locals[li].is_ptr = 1;
                        g_locals[li].size = 8;
                    }
                    emit_store_var(li, -1, "%%rax");
                }
            }
            break;
        }
        case ST_EXPR: gen_expr(s->expr); break;
        case ST_RETURN:
            if (s->expr) gen_expr(s->expr);
            else emit("    movq $0, %%rax\n");
            emit("    leave\n    ret\n");
            break;
        case ST_IF: {
            int L_else = new_label(), L_end = new_label();
            gen_expr(s->cond);
            emit("    test %%rax, %%rax\n    jz .L%d\n", L_else);
            gen_stmt(s->then_s);
            emit("    jmp .L%d\n.L%d:\n", L_end, L_else);
            if (s->else_s) gen_stmt(s->else_s);
            emit(".L%d:\n", L_end);
            break;
        }
        case ST_WHILE: {
            int L_top = new_label(), L_end = new_label();
            emit(".L%d:\n", L_top);
            gen_expr(s->cond);
            emit("    test %%rax, %%rax\n    jz .L%d\n", L_end);
            g_break_stack[g_loop_depth] = L_end;
            g_cont_stack[g_loop_depth]  = L_top;
            g_loop_depth++;
            gen_stmt(s->body);
            g_loop_depth--;
            emit("    jmp .L%d\n.L%d:\n", L_top, L_end);
            break;
        }
        case ST_FOR: {
            int saved = g_nlocals;
            int L_top = new_label(), L_step = new_label(), L_end = new_label();
            if (s->for_init) gen_stmt(s->for_init);
            emit(".L%d:\n", L_top);
            if (s->for_cond) {
                gen_expr(s->for_cond);
                emit("    test %%rax, %%rax\n    jz .L%d\n", L_end);
            }
            g_break_stack[g_loop_depth] = L_end;
            g_cont_stack[g_loop_depth]  = L_step;
            g_loop_depth++;
            gen_stmt(s->body);
            g_loop_depth--;
            emit(".L%d:\n", L_step);
            if (s->for_step) gen_expr(s->for_step);
            emit("    jmp .L%d\n.L%d:\n", L_top, L_end);
            g_nlocals = saved;
            break;
        }
        case ST_BLOCK: {
            const char *saved_mod = g_cur_module;
            int saved_mod_len = g_cur_module_len;
            if (s->call_module_name) {
                g_cur_module = s->call_module_name;
                g_cur_module_len = s->call_module_name_len;
            }
            if (s->no_scope) {
                for (int i = 0; i < s->nstmts; i++) gen_stmt(s->stmts[i]);
            } else {
                int saved = g_nlocals;
                for (int i = 0; i < s->nstmts; i++) gen_stmt(s->stmts[i]);
                g_nlocals = saved;
            }
            g_cur_module = saved_mod;
            g_cur_module_len = saved_mod_len;
            break;
        }
        case ST_MATCH: {
            int L_end = new_label();
            int *L_case = malloc(sizeof(int) * (s->ncases + 1));
            for (int i = 0; i < s->ncases; i++) L_case[i] = new_label();

            gen_expr(s->expr);
            emit("    mov %%rax, %%r15\n");
            for (int i = 0; i < s->ncases; i++) {
                gen_expr(s->case_values[i]);
                emit("    cmp %%rax, %%r15\n");
                emit("    je .L%d\n", L_case[i]);
            }
            if (s->default_s) gen_stmt(s->default_s);
            emit("    jmp .L%d\n", L_end);
            for (int i = 0; i < s->ncases; i++) {
                emit(".L%d:\n", L_case[i]);
                gen_stmt(s->stmts[i]);
                int fall = 0;
                if (s->stmts[i] && s->stmts[i]->kind == ST_BLOCK &&
                    s->stmts[i]->nstmts > 0) {
                    stmt_t *last = s->stmts[i]->stmts[s->stmts[i]->nstmts - 1];
                    if (last && last->kind == ST_FALLTHROUGH) fall = 1;
                }
                if (!fall) emit("    jmp .L%d\n", L_end);
            }
            emit(".L%d:\n", L_end);
            free(L_case);
            break;
        }
        case ST_CONT_DECL: {
            int off = local_add_cont(s->name, s->name_len);
            emit("    movq $0, %d(%%rbp)\n", off);
            emit("    movq $0, %d(%%rbp)\n", off + 8);
            break;
        }
        case ST_CONT_PUT: {
            int i = find_local(s->name, s->name_len);
            int off = g_locals[i].offset;
            expr_t *v = s->expr;
            if (v->kind == EX_INT) {
                g_locals[i].cont_type = 1;
                emit("    movq $1, %d(%%rbp)\n", off);
                emit("    movq $%lld, %d(%%rbp)\n", (long long)v->ival, off + 8);
            } else if (v->kind == EX_FLOAT) {
                g_locals[i].cont_type = 3;
                int id = float_add(v->fval, 8);
                emit("    movq $3, %d(%%rbp)\n", off);
                emit("    movsd .Lfloat%d(%%rip), %%xmm0\n", id);
                emit("    movsd %%xmm0, %d(%%rbp)\n", off + 8);
            } else if (v->kind == EX_STRING) {
                g_locals[i].cont_type = 2;
                int id = str_add(v->name, v->name_len);
                emit("    movq $2, %d(%%rbp)\n", off);
                emit("    lea .Lstr%d(%%rip), %%rax\n", id);
                emit("    movq %%rax, %d(%%rbp)\n", off + 8);
            } else {
                g_locals[i].cont_type = 1;
                gen_expr(v);
                emit("    movq $1, %d(%%rbp)\n", off);
                emit("    movq %%rax, %d(%%rbp)\n", off + 8);
            }
            break;
        }
        case ST_CONT_TAKE: {
            int i = find_local(s->expr->name, s->expr->name_len);
            int off = g_locals[i].offset;
            if (s->name) {
                int ct = g_locals[i].cont_type;
                int ti = find_local(s->name, s->name_len);
                int toff;
                if (ti < 0) {
                    toff = local_add(s->name, s->name_len);
                    ti = g_nlocals - 1;
                } else {
                    toff = g_locals[ti].offset;
                }
                g_locals[ti].cont_type = ct;
                emit("    movq %d(%%rbp), %%rax\n", off + 8);
                emit("    movq %%rax, %d(%%rbp)\n", toff);
            }
            break;
        }
        case ST_CONT_CLEAR: {
            int i = find_local(s->name, s->name_len);
            int off = g_locals[i].offset;
            emit("    movq $0, %d(%%rbp)\n", off);
            emit("    movq $0, %d(%%rbp)\n", off + 8);
            break;
        }
        case ST_ASM:
            for (int i = 0; i < s->nstmts; i++) {
                expr_t *e = s->stmts[i]->expr;
                emit("    %.*s\n", e->name_len, e->name);
            }
            break;
        case ST_LABEL:
            emit(".Luser_%.*s:\n", s->name_len, s->name);
            break;
        case ST_GOTO:
            emit("    jmp .Luser_%.*s\n", s->name_len, s->name);
            break;
        case ST_BREAK:
            if (g_loop_depth > 0)
                emit("    jmp .L%d\n", g_break_stack[g_loop_depth - 1]);
            break;
        case ST_CONTINUE:
            if (g_loop_depth > 0)
                emit("    jmp .L%d\n", g_cont_stack[g_loop_depth - 1]);
            break;
        default: break;
    }
}

static void gen_func(func_t *f) {
    if (g_nfunc_sigs < MAX_FUNC_SIGS) {
        func_sig_t *sig = &g_func_sigs[g_nfunc_sigs];
        sig->name = f->name;
        sig->len = f->name_len;
        sig->is_float_ret = f->has_ret && is_float_type(&f->ret_type);
        sig->nparams = f->nparams > 8 ? 8 : f->nparams;
        for (int i = 0; i < sig->nparams; i++)
            sig->param_is_float[i] = is_float_type(&f->params[i].type);
        g_nfunc_sigs++;
    }
    g_nlocals = 0; g_stack_used = 0;
    const char *name = f->name;
    int nlen = f->name_len;
    int is_main = (nlen == 3 && memcmp(name, "主", 3) == 0) ||
                  (nlen == 4 && memcmp(name, "main", 4) == 0);
    if (f->category == 1 && f->module_name) {
        emit("    .type %.*s_%.*s, @function\n",
             f->module_name_len, f->module_name, nlen, name);
        emit("%.*s_%.*s:\n",
             f->module_name_len, f->module_name, nlen, name);
    } else if (is_main) {
        emit("    .globl main\n");
        emit("main:\n");
    } else {
        emit("    .globl %.*s\n", nlen, name);
        emit("%.*s:\n", nlen, name);
    }
    emit("    push %%rbp\n    mov %%rsp, %%rbp\n");

    /* 动态栈帧大小：扫描 AST 算字节数，16 字节对齐 */
    int frame = scan_frame(f->body);
    frame += 8 * f->nparams;   /* 参数落栈 */
    frame = (frame + 15) & ~15;
    if (frame < 16) frame = 16;
    emit("    sub $%d, %%rsp\n", frame);
    static const char *pregs_linux[]   = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    static const char *pregs_windows[] = {"%rcx", "%rdx", "%r8",  "%r9",  "%r10", "%r11"};
    const char **pregs = g_target_windows ? pregs_windows : pregs_linux;
    int int_idx = 0, float_idx = 0;
    for (int i = 0; i < f->nparams && i < 6; i++) {
        int off = local_add_ex(f->params[i].name, f->params[i].name_len,
                               0, 1, is_float_type(&f->params[i].type));
        if (is_float_type(&f->params[i].type)) {
            emit("    movsd %%xmm%d, %d(%%rbp)\n", float_idx, off);
            float_idx++;
        } else {
            emit("    movq %s, %d(%%rbp)\n", pregs[int_idx], off);
            int_idx++;
        }
    }
    gen_stmt(f->body);
    emit("    movq $0, %%rax\n    leave\n    ret\n");
}

int codegen_program(program_t *p, const char *out_path) {
    g_prog = p;
    g_nglobals = 0;
    for (int i = 0; i < p->nglobals; i++) collect_globals(p->globals[i]);
    for (int i = 0; i < p->nfuncs; i++) collect_globals(p->funcs[i]->body);
    collect_struct_insts();

    g_out = fopen(out_path, "w");
    if (!g_out) { perror(out_path); return -1; }

    emit("# 由 wescc 生成\n");
    emit("    .section .rodata\n");
    emit(".Lfmt_s:\n    .asciz \"%%s\"\n");
    emit(".Lfmt_d:\n    .asciz \"%%ld\"\n");
    emit(".Lfmt_u:\n    .asciz \"%%lu\"\n");
    emit(".Lfmt_f:\n    .asciz \"%%f\"\n");
    emit("    .text\n");
    emit_globals();

    /* 主文件函数 */
    for (int i = 0; i < p->nfuncs; i++) gen_func(p->funcs[i]);

    /* 模块函数：切换到模块上下文后生成 */
    for (int mi = 0; mi < g_nmodules_codegen; mi++) {
        module_cg_t *m = &g_modules_codegen[mi];
        const char *saved_mod = g_cur_module;
        int saved_mod_len = g_cur_module_len;
        g_cur_module = m->name;
        g_cur_module_len = m->name_len;
        for (int j = 0; j < m->nfuncs; j++) {
            func_t *f = m->funcs[j];
            if (f->name_len == 3 && memcmp(f->name, "主", 3) == 0) continue;
            if (f->name_len == 4 && memcmp(f->name, "main", 4) == 0) continue;
            gen_func(f);
        }
        g_cur_module = saved_mod;
        g_cur_module_len = saved_mod_len;
    }

    emit_strings();
    emit_floats();
    emit("    .section .note.GNU-stack,\"\",@progbits\n\n");
    fclose(g_out); g_out = 0;
    return 0;
}
