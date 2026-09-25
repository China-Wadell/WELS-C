#include "codegen.h"

static FILE *g_out = 0;
static int   g_label = 0;
static int   g_str_id = 0;
static int   g_target_windows = 0;
static int   g_cur_line = 0;

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
    int is_ptr;
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
    g_locals[g_nlocals].is_ptr = 0;
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
static int is_float_type(type_desc_t *t) {
    const char *b = t->base; int l = t->base_len;
    if (l == 6 && (memcmp(b, "双精", 6) == 0 || memcmp(b, "浮点", 6) == 0 ||
                   memcmp(b, "double", 6) == 0)) return 1;
    if (l == 5 && memcmp(b, "float", 5) == 0) return 1;
    return 0;
}

static int str_add(const char *s, int len) {
    int id = g_str_id++;
    emit("    .section .rodata\n.Lstr%d:\n    .byte ", id);
    for (int i = 0; i < len; i++) emit("%d, ", (unsigned char)s[i]);
    emit("0\n    .text\n");
    return id;
}
static int float_add(double v) {
    int id = g_str_id++;
    emit("    .section .rodata\n.Lfloat%d:\n    .double %.17g\n    .text\n", id, v);
    return id;
}

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
        default: return 0;
    }
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
    }
    return 0;
}

static void gen_load_var(const char *name, int len) {
    int li = find_local(name, len);
    if (li >= 0) {
        if (g_locals[li].is_array)      emit("    lea %d(%%rbp), %%rax\n", g_locals[li].offset);
        else if (g_locals[li].is_float) emit("    movsd %d(%%rbp), %%xmm0\n", g_locals[li].offset);
        else                             emit("    movq %d(%%rbp), %%rax\n", g_locals[li].offset);
        return;
    }
    int gi = find_global(name, len);
    if (gi >= 0) {
        if (g_globals[gi].is_array)      emit("    lea g%d(%%rip), %%rax\n", g_globals[gi].id);
        else if (g_globals[gi].is_float) emit("    movsd g%d(%%rip), %%xmm0\n", g_globals[gi].id);
        else                             emit("    movq g%d(%%rip), %%rax\n", g_globals[gi].id);
        return;
    }
    fprintf(stderr, "%d: 错误: 未声明的变量 %.*s\n", g_cur_line, len, name);
    exit(1);
}

static void collect_globals(stmt_t *s) {
    if (!s) return;
    if (s->kind == ST_LET && s->type.is_static) {
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
        emit("    .align 8\ng%d:\n", g->id);
        if (g->is_struct_inst) {
            struct_def_t *sd = &g_prog->structs[g->struct_idx];
            if (sd->is_union) {
                emit("    .quad %lld\n", (long long)sd->inst_init[g->inst_idx][0]);
            } else {
                for (int k = 0; k < sd->nfields; k++)
                    emit("    .quad %lld\n", (long long)sd->inst_init[g->inst_idx][k]);
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

static void gen_call(expr_t *e) {
    int n = e->nargs;
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
                if (g_target_windows) { emit("    mov %%rax, %%rdx\n    lea .Lfmt_d(%%rip), %%rcx\n"); }
                else                  { emit("    mov %%rax, %%rsi\n    lea .Lfmt_d(%%rip), %%rdi\n"); }
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

    emit("    movl $0, %%eax\n    call %.*s\n", fnlen, fn);
}

static void gen_expr(expr_t *e) {
    if (!e) return;
    if (e->line > 0) g_cur_line = e->line;
    switch (e->kind) {
        case EX_INT: emit("    movq $%lld, %%rax\n", (long long)e->ival); break;
        case EX_FLOAT: { int id = float_add(e->fval); emit("    movsd .Lfloat%d(%%rip), %%xmm0\n", id); break; }
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
            emit("    lea g%d(%%rip), %%rax\n", g_globals[gi].id);
            emit("    mov %d(%%rax), %%rax\n", sd->fields[fi].offset);
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

                /* 右值是返回浮点的函数调用 */
                if (e->right->kind == EX_CALL) {
                    func_sig_t *sig = find_func_sig(e->right->operand->name,
                                                     e->right->operand->name_len);
                    if (sig && sig->is_float_ret) {
                        gen_expr(e->right);
                        if (li >= 0) emit("    movsd %%xmm0, %d(%%rbp)\n", g_locals[li].offset);
                        else         emit("    movsd %%xmm0, g%d(%%rip)\n", g_globals[gi].id);
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
                    if (li >= 0) emit("    movq %%rax, %d(%%rbp)\n", g_locals[li].offset);
                    else         emit("    movq %%rax, g%d(%%rip)\n", g_globals[gi].id);
                    break;
                }

                gen_expr(e->right);
                if (li >= 0) {
                    if (g_locals[li].is_range) {
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
                    else if (g_locals[li].is_float) emit("    movsd %%xmm0, %d(%%rbp)\n", g_locals[li].offset);
                    else                            emit("    movq %%rax, %d(%%rbp)\n", g_locals[li].offset);
                } else {
                    if (g_globals[gi].is_float) emit("    movsd %%xmm0, g%d(%%rip)\n", g_globals[gi].id);
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
                gen_expr(e->right);
                int off = -1;
                int li2 = find_local(e->left->operand->name, e->left->operand->name_len);
                if (li2 >= 0) off = g_locals[li2].offset;
                if (off >= 0) emit("    movq %d(%%rbp), %%rcx\n", off);
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
        case EX_CALL: gen_call(e); break;
        default: break;
    }
}

static void gen_binop(const char *op, int len) {
    if      (is_op_text(op, len, "+")) emit("    add %%rcx, %%rax\n");
    else if (is_op_text(op, len, "-")) emit("    sub %%rcx, %%rax\n");
    else if (is_op_text(op, len, "*")) emit("    imul %%rcx, %%rax\n");
    else if (is_op_text(op, len, "/")) emit("    cqto\n    idiv %%rcx\n");
    else if (is_op_text(op, len, "%")) emit("    cqto\n    idiv %%rcx\n    mov %%rdx, %%rax\n");
    else if (is_op_text(op, len, "==")) emit("    cmp %%rcx, %%rax\n    sete %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, "\\=")) emit("    cmp %%rcx, %%rax\n    setne %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, "<"))  emit("    cmp %%rcx, %%rax\n    setl %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, ">"))  emit("    cmp %%rcx, %%rax\n    setg %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, "<=")) emit("    cmp %%rcx, %%rax\n    setle %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, ">=")) emit("    cmp %%rcx, %%rax\n    setge %%al\n    movzbq %%al, %%rax\n");
    else if (is_op_text(op, len, "&"))  emit("    and %%rcx, %%rax\n");
    else if (is_op_text(op, len, "|"))  emit("    or  %%rcx, %%rax\n");
    else if (is_op_text(op, len, "^"))  emit("    xor %%rcx, %%rax\n");
    else if (is_op_text(op, len, "<<")) emit("    shl %%cl, %%rax\n");
    else if (is_op_text(op, len, ">>")) emit("    sar %%cl, %%rax\n");
    else if (is_op_text(op, len, "&&")) emit("    and %%rcx, %%rax\n");
    else if (is_op_text(op, len, "||")) emit("    or  %%rcx, %%rax\n");
}

static void gen_binop_float(const char *op, int len) {
    if      (is_op_text(op, len, "+")) emit("    addsd %%xmm1, %%xmm0\n");
    else if (is_op_text(op, len, "-")) emit("    subsd %%xmm1, %%xmm0\n");
    else if (is_op_text(op, len, "*")) emit("    mulsd %%xmm1, %%xmm0\n");
    else if (is_op_text(op, len, "/")) emit("    divsd %%xmm1, %%xmm0\n");
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
                emit("    movsd %%xmm0, %d(%%rbp)\n", off);
            } else {
                if (s->init) gen_expr(s->init);
                else emit("    movq $0, %%rax\n");
                int off = local_add(s->name, s->name_len);
                if (is_rng || is_pt) {
                    int li = find_local(s->name, s->name_len);
                    if (is_rng) {
                        g_locals[li].is_range       = 1;
                        g_locals[li].range_lo       = s->type.range_lo;
                        g_locals[li].range_hi       = s->type.range_hi;
                        g_locals[li].range_lo_open  = s->type.range_lo_open;
                        g_locals[li].range_hi_open  = s->type.range_hi_open;
                    }
                    if (is_pt) g_locals[li].is_ptr = 1;
                }
                emit("    movq %%rax, %d(%%rbp)\n", off);
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
            int saved = g_nlocals;
            for (int i = 0; i < s->nstmts; i++) gen_stmt(s->stmts[i]);
            g_nlocals = saved;
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
                emit("    jmp .L%d\n", L_end);
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
                int id = float_add(v->fval);
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
    emit("    .globl main\n");
    if ((nlen == 3 && memcmp(name, "主", 3) == 0) ||
        (nlen == 4 && memcmp(name, "main", 4) == 0)) emit("main:\n");
    else emit("%.*s:\n", nlen, name);
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
    emit(".Lfmt_f:\n    .asciz \"%%f\"\n");
    emit("    .text\n");
    emit_globals();
    for (int i = 0; i < p->nfuncs; i++) gen_func(p->funcs[i]);
    emit("    .section .note.GNU-stack,\"\",@progbits\n\n");
    fclose(g_out); g_out = 0;
    return 0;
}
