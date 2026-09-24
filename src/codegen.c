#include "codegen.h"

static FILE *g_out = 0;
static int   g_label = 0;
static int   g_str_id = 0;
static int   g_target_windows = 0;

void codegen_set_target(int is_windows) {
    g_target_windows = is_windows;
}

#define MAX_LOCALS 128
typedef struct {
    const char *name;
    int         len;
    int         offset;
    int         is_array;
    int         arr_len;
} local_t;

static local_t g_locals[MAX_LOCALS];
static int     g_nlocals = 0;
static int     g_stack_used = 0;

static void emit(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_out, fmt, ap);
    va_end(ap);
}

static int new_label(void) { return g_label++; }

static int local_add_ex(const char *name, int len, int is_array, int arr_len) {
    int slots = is_array ? arr_len : 1;
    g_stack_used += 8 * slots;
    g_locals[g_nlocals].name     = name;
    g_locals[g_nlocals].len      = len;
    g_locals[g_nlocals].offset   = -g_stack_used;
    g_locals[g_nlocals].is_array = is_array;
    g_locals[g_nlocals].arr_len  = arr_len;
    g_nlocals++;
    return -g_stack_used;
}

static int local_add(const char *name, int len) {
    return local_add_ex(name, len, 0, 1);
}

static int local_find_idx(const char *name, int len) {
    for (int i = g_nlocals - 1; i >= 0; i--)
        if (g_locals[i].len == len && memcmp(g_locals[i].name, name, len) == 0)
            return i;
    fprintf(stderr, "错误: 未声明的变量 %.*s\n", len, name);
    exit(1);
}

static int local_find(const char *name, int len) {
    return g_locals[local_find_idx(name, len)].offset;
}

static int is_op_text(const char *t, int len, const char *s) {
    return (int)strlen(s) == len && memcmp(t, s, len) == 0;
}

static int str_add(const char *s, int len) {
    int id = g_str_id++;
    emit("    .section .rodata\n");
    emit(".Lstr%d:\n", id);
    emit("    .byte ");
    for (int i = 0; i < len; i++) emit("%d, ", (unsigned char)s[i]);
    emit("0\n");
    emit("    .text\n");
    return id;
}

static void gen_binop(const char *op, int len);
static void gen_expr(expr_t *e);

static void gen_call(expr_t *e) {
    int n = e->nargs;
    const char *fn = e->operand->name;
    int fnlen = e->operand->name_len;

    int is_print =
        (fnlen == 6 && memcmp(fn, "打印", 6) == 0) ||
        (fnlen == 5 && memcmp(fn, "print", 5) == 0);

    if (is_print) {
        for (int i = n - 1; i >= 0; i--) {
            gen_expr(e->args[i]);
            emit("    push %%rax\n");
        }
        for (int i = 0; i < n; i++) {
            if (g_target_windows) {
                emit("    pop %%rdx\n");
                emit("    lea .Lfmt_s(%%rip), %%rcx\n");
            } else {
                emit("    pop %%rsi\n");
                emit("    lea .Lfmt_s(%%rip), %%rdi\n");
            }
            emit("    movl $0, %%eax\n");
            emit("    call printf\n");
        }
        emit("    movq $0, %%rax\n");
        return;
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

    emit("    movl $0, %%eax\n");
    emit("    call %.*s\n", fnlen, fn);
}

static void gen_expr(expr_t *e) {
    if (!e) return;
    switch (e->kind) {
        case EX_INT:
            emit("    movq $%lld, %%rax\n", (long long)e->ival);
            break;
        case EX_FLOAT:
            emit("    movq $0, %%rax\n");
            break;
        case EX_STRING: {
            int id = str_add(e->name, e->name_len);
            emit("    lea .Lstr%d(%%rip), %%rax\n", id);
            break;
        }
        case EX_IDENT: {
            int i = local_find_idx(e->name, e->name_len);
            if (g_locals[i].is_array) {
                emit("    lea %d(%%rbp), %%rax\n", g_locals[i].offset);
            } else {
                emit("    movq %d(%%rbp), %%rax\n", g_locals[i].offset);
            }
            break;
        }
        case EX_UNARY:
            if (is_op_text(e->op_text, e->op_len, "&") &&
                e->operand->kind == EX_IDENT) {
                int off = local_find(e->operand->name, e->operand->name_len);
                emit("    lea %d(%%rbp), %%rax\n", off);
                break;
            }
            gen_expr(e->operand);
            if      (is_op_text(e->op_text, e->op_len, "-")) emit("    neg %%rax\n");
            else if (is_op_text(e->op_text, e->op_len, "!")) {
                emit("    test %%rax, %%rax\n");
                emit("    setz %%al\n");
                emit("    movzbq %%al, %%rax\n");
            }
            else if (is_op_text(e->op_text, e->op_len, "~")) emit("    not %%rax\n");
            else if (is_op_text(e->op_text, e->op_len, "*")) {
                emit("    mov (%%rax), %%rax\n");
            }
            break;
        case EX_BINARY:
            gen_expr(e->right);
            emit("    push %%rax\n");
            gen_expr(e->left);
            emit("    pop %%rcx\n");
            gen_binop(e->op_text, e->op_len);
            break;
        case EX_INDEX:
            gen_expr(e->left);
            emit("    push %%rax\n");
            gen_expr(e->right);
            emit("    mov %%rax, %%rcx\n");
            emit("    pop %%rax\n");
            emit("    mov (%%rax, %%rcx, 8), %%rax\n");
            break;
        case EX_ASSIGN:
            if (e->left->kind == EX_IDENT) {
                gen_expr(e->right);
                int off = local_find(e->left->name, e->left->name_len);
                emit("    movq %%rax, %d(%%rbp)\n", off);
            }
            else if (e->left->kind == EX_UNARY &&
                     is_op_text(e->left->op_text, e->left->op_len, "*") &&
                     e->left->operand->kind == EX_IDENT) {
                gen_expr(e->right);
                int off = local_find(e->left->operand->name, e->left->operand->name_len);
                emit("    movq %d(%%rbp), %%rcx\n", off);
                emit("    movq %%rax, (%%rcx)\n");
            }
            else if (e->left->kind == EX_INDEX) {
                gen_expr(e->left->left);
                emit("    push %%rax\n");
                gen_expr(e->left->right);
                emit("    push %%rax\n");
                gen_expr(e->right);
                emit("    pop %%rcx\n");
                emit("    pop %%rdx\n");
                emit("    movq %%rax, (%%rdx, %%rcx, 8)\n");
            }
            break;
        case EX_CALL:
            gen_call(e);
            break;
        default:
            break;
    }
}

static void gen_binop(const char *op, int len) {
    if      (is_op_text(op, len, "+"))  emit("    add %%rcx, %%rax\n");
    else if (is_op_text(op, len, "-"))  emit("    sub %%rcx, %%rax\n");
    else if (is_op_text(op, len, "*"))  emit("    imul %%rcx, %%rax\n");
    else if (is_op_text(op, len, "/")) { emit("    cqto\n"); emit("    idiv %%rcx\n"); }
    else if (is_op_text(op, len, "%")) {
        emit("    cqto\n"); emit("    idiv %%rcx\n"); emit("    mov %%rdx, %%rax\n");
    }
    else if (is_op_text(op, len, "==")) {
        emit("    cmp %%rcx, %%rax\n");
        emit("    sete %%al\n"); emit("    movzbq %%al, %%rax\n");
    }
    else if (is_op_text(op, len, "\\=")) {
        emit("    cmp %%rcx, %%rax\n");
        emit("    setne %%al\n"); emit("    movzbq %%al, %%rax\n");
    }
    else if (is_op_text(op, len, "<")) {
        emit("    cmp %%rcx, %%rax\n");
        emit("    setl %%al\n"); emit("    movzbq %%al, %%rax\n");
    }
    else if (is_op_text(op, len, ">")) {
        emit("    cmp %%rcx, %%rax\n");
        emit("    setg %%al\n"); emit("    movzbq %%al, %%rax\n");
    }
    else if (is_op_text(op, len, "<=")) {
        emit("    cmp %%rcx, %%rax\n");
        emit("    setle %%al\n"); emit("    movzbq %%al, %%rax\n");
    }
    else if (is_op_text(op, len, ">=")) {
        emit("    cmp %%rcx, %%rax\n");
        emit("    setge %%al\n"); emit("    movzbq %%al, %%rax\n");
    }
    else if (is_op_text(op, len, "&"))  emit("    and %%rcx, %%rax\n");
    else if (is_op_text(op, len, "|"))  emit("    or  %%rcx, %%rax\n");
    else if (is_op_text(op, len, "^"))  emit("    xor %%rcx, %%rax\n");
    else if (is_op_text(op, len, "<<")) emit("    shl %%cl, %%rax\n");
    else if (is_op_text(op, len, ">>")) emit("    sar %%cl, %%rax\n");
    else if (is_op_text(op, len, "&&")) emit("    and %%rcx, %%rax\n");
    else if (is_op_text(op, len, "||")) emit("    or  %%rcx, %%rax\n");
}

static void gen_stmt(stmt_t *s) {
    if (!s) return;
    switch (s->kind) {
        case ST_LET: {
            if (s->type.is_array && s->init && s->init->kind == EX_ARRAY_INIT) {
                int n = s->init->nargs;
                int base = local_add_ex(s->name, s->name_len, 1, n);
                for (int i = 0; i < n; i++) {
                    gen_expr(s->init->args[i]);
                    emit("    movq %%rax, %d(%%rbp)\n", base + i * 8);
                }
            } else {
                if (s->init) gen_expr(s->init);
                else emit("    movq $0, %%rax\n");
                int off = local_add(s->name, s->name_len);
                emit("    movq %%rax, %d(%%rbp)\n", off);
            }
            break;
        }
        case ST_EXPR:
            gen_expr(s->expr);
            break;
        case ST_RETURN:
            if (s->expr) gen_expr(s->expr);
            else emit("    movq $0, %%rax\n");
            emit("    leave\n");
            emit("    ret\n");
            break;
        case ST_IF: {
            int L_else = new_label();
            int L_end = new_label();
            gen_expr(s->cond);
            emit("    test %%rax, %%rax\n");
            emit("    jz .L%d\n", L_else);
            gen_stmt(s->then_s);
            emit("    jmp .L%d\n", L_end);
            emit(".L%d:\n", L_else);
            if (s->else_s) gen_stmt(s->else_s);
            emit(".L%d:\n", L_end);
            break;
        }
        case ST_WHILE: {
            int L_top = new_label();
            int L_end = new_label();
            emit(".L%d:\n", L_top);
            gen_expr(s->cond);
            emit("    test %%rax, %%rax\n");
            emit("    jz .L%d\n", L_end);
            gen_stmt(s->body);
            emit("    jmp .L%d\n", L_top);
            emit(".L%d:\n", L_end);
            break;
        }
        case ST_FOR: {
            int L_top = new_label();
            int L_end = new_label();
            if (s->for_init) gen_stmt(s->for_init);
            emit(".L%d:\n", L_top);
            if (s->for_cond) {
                gen_expr(s->for_cond);
                emit("    test %%rax, %%rax\n");
                emit("    jz .L%d\n", L_end);
            }
            gen_stmt(s->body);
            if (s->for_step) gen_expr(s->for_step);
            emit("    jmp .L%d\n", L_top);
            emit(".L%d:\n", L_end);
            break;
        }
        case ST_BLOCK:
            for (int i = 0; i < s->nstmts; i++) gen_stmt(s->stmts[i]);
            break;
        default:
            break;
    }
}

static void gen_func(func_t *f) {
    g_nlocals = 0;
    g_stack_used = 0;

    const char *name = f->name;
    int nlen = f->name_len;

    emit("    .globl main\n");
    if ((nlen == 3 && memcmp(name, "主", 3) == 0) ||
        (nlen == 4 && memcmp(name, "main", 4) == 0)) {
        emit("main:\n");
    } else {
        emit("%.*s:\n", nlen, name);
    }

    emit("    push %%rbp\n");
    emit("    mov %%rsp, %%rbp\n");
    emit("    sub $1024, %%rsp\n");

    static const char *pregs_linux[]   = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    static const char *pregs_windows[] = {"%rcx", "%rdx", "%r8",  "%r9",  "%r10", "%r11"};
    const char **pregs = g_target_windows ? pregs_windows : pregs_linux;

    for (int i = 0; i < f->nparams && i < 6; i++) {
        int off = local_add(f->params[i].name, f->params[i].name_len);
        emit("    movq %s, %d(%%rbp)\n", pregs[i], off);
    }

    gen_stmt(f->body);

    emit("    movq $0, %%rax\n");
    emit("    leave\n");
    emit("    ret\n");
}

int codegen_program(program_t *p, const char *out_path) {
    g_out = fopen(out_path, "w");
    if (!g_out) { perror(out_path); return -1; }

    emit("# 由 wescc 生成\n");
    emit("    .section .rodata\n");
    emit(".Lfmt_s:\n");
    emit("    .asciz \"%%s\"\n");
    emit("    .text\n");

    for (int i = 0; i < p->nfuncs; i++) gen_func(p->funcs[i]);

    emit("    .section .note.GNU-stack,\"\",@progbits\n");
    emit("\n");

    fclose(g_out);
    g_out = 0;
    return 0;
}
