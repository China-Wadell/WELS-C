#include "ast.h"

static void free_expr(expr_t *e) {
    if (!e) return;
    free_expr(e->left);
    free_expr(e->right);
    free_expr(e->operand);
    for (int i = 0; i < e->nargs; i++) free_expr(e->args[i]);
    free(e->args);
    free(e);
}

static void free_stmt(stmt_t *s) {
    if (!s) return;
    free_expr(s->init);
    free_expr(s->expr);
    free_expr(s->cond);
    free_expr(s->for_cond);
    free_expr(s->for_step);
    free_stmt(s->then_s);
    free_stmt(s->else_s);
    free_stmt(s->body);
    free_stmt(s->for_init);
    for (int i = 0; i < s->nstmts; i++) free_stmt(s->stmts[i]);
    free(s->stmts);
    free(s);
}

void ast_free_program(program_t *p) {
    if (!p) return;
    for (int i = 0; i < p->nfuncs; i++) {
        func_t *f = p->funcs[i];
        free(f->params);
        free_stmt(f->body);
        free(f);
    }
    free(p->funcs);
    free(p->includes);
}

static void indent(int n) {
    for (int i = 0; i < n; i++) fputs("  ", stdout);
}

static void print_expr(expr_t *e, int d) {
    indent(d);
    if (!e) { puts("<null>"); return; }
    switch (e->kind) {
        case EX_INT:    printf("INT %lld\n", (long long)e->ival); break;
        case EX_FLOAT:  printf("FLOAT %f\n", e->fval); break;
        case EX_STRING: printf("STRING \"%.*s\"\n", e->name_len, e->name); break;
        case EX_IDENT:  printf("ID(%.*s)\n", e->name_len, e->name); break;
        case EX_BINARY:
            printf("BIN(%.*s)\n", e->op_len, e->op_text);
            print_expr(e->left, d + 1);
            print_expr(e->right, d + 1);
            break;
        case EX_UNARY:
            printf("UN(%.*s)\n", e->op_len, e->op_text);
            print_expr(e->operand, d + 1);
            break;
        case EX_ASSIGN:
            printf("ASSIGN(%.*s)\n", e->op_len, e->op_text);
            print_expr(e->left, d + 1);
            print_expr(e->right, d + 1);
            break;
        case EX_CALL:
            printf("CALL\n");
            print_expr(e->operand, d + 1);
            for (int i = 0; i < e->nargs; i++) print_expr(e->args[i], d + 1);
            break;
        case EX_CONST_DECL:
            printf("CONST(%.*s)\n", e->name_len, e->name);
            break;
        case EX_ARRAY_INIT:
            puts("ARRAY_INIT");
            for (int i = 0; i < e->nargs; i++) print_expr(e->args[i], d + 1);
            break;
        case EX_INDEX:
            puts("INDEX");
            print_expr(e->left, d + 1);
            print_expr(e->right, d + 1);
            break;
    }
}

static void print_stmt(stmt_t *s, int d) {
    if (!s) return;
    switch (s->kind) {
        case ST_LET:
            indent(d);
            printf("LET %.*s : %.*s",
                   s->name_len, s->name,
                   s->type.base_len, s->type.base);
            if (s->type.is_ptr) fputs(" ptr", stdout);
            if (s->type.is_array) fputs(" array", stdout);
            if (s->type.is_ref) fputs(" ref", stdout);
            if (s->type.is_range) fputs(" range", stdout);
            if (s->type.is_const) fputs(" const", stdout);
            if (s->type.is_static) fputs(" static", stdout);
            if (s->type.is_local) fputs(" local", stdout);
            if (s->type.is_container) fputs(" container", stdout);
            putchar('\n');
            if (s->init) print_expr(s->init, d + 1);
            break;
        case ST_EXPR:
            indent(d); puts("EXPR");
            print_expr(s->expr, d + 1);
            break;
        case ST_RETURN:
            indent(d); puts("RETURN");
            if (s->expr) print_expr(s->expr, d + 1);
            break;
        case ST_IF:
            indent(d); puts("IF");
            indent(d + 1); puts("cond:");
            print_expr(s->cond, d + 2);
            indent(d + 1); puts("then:");
            print_stmt(s->then_s, d + 2);
            if (s->else_s) {
                indent(d + 1); puts("else:");
                print_stmt(s->else_s, d + 2);
            }
            break;
        case ST_WHILE:
            indent(d); puts("WHILE");
            indent(d + 1); puts("cond:");
            print_expr(s->cond, d + 2);
            indent(d + 1); puts("body:");
            print_stmt(s->body, d + 2);
            break;
        case ST_FOR:
            indent(d); puts("FOR");
            indent(d + 1); puts("init:");
            print_stmt(s->for_init, d + 2);
            indent(d + 1); puts("cond:");
            print_expr(s->for_cond, d + 2);
            indent(d + 1); puts("step:");
            print_expr(s->for_step, d + 2);
            indent(d + 1); puts("body:");
            print_stmt(s->body, d + 2);
            break;
        case ST_BLOCK:
            indent(d); puts("{");
            for (int i = 0; i < s->nstmts; i++) print_stmt(s->stmts[i], d + 1);
            indent(d); puts("}");
            break;
    }
}

void ast_print_program(program_t *p) {
    puts("=== 预编译 ===");
    for (int i = 0; i < p->nincludes; i++)
        printf("  #<%.*s>\n", p->includes[i].len, p->includes[i].text);

    for (int i = 0; i < p->nfuncs; i++) {
        func_t *f = p->funcs[i];
        printf("\n=== 函数 %.*s ===\n", f->name_len, f->name);
        if (f->has_ret)
            printf("  -> %.*s\n", f->ret_type.base_len, f->ret_type.base);
        for (int j = 0; j < f->nparams; j++) {
            printf("  参 %.*s : %.*s",
                   f->params[j].name_len, f->params[j].name,
                   f->params[j].type.base_len, f->params[j].type.base);
            if (f->params[j].type.is_ptr) fputs(" ptr", stdout);
            if (f->params[j].type.is_array) fputs(" array", stdout);
            putchar('\n');
        }
        print_stmt(f->body, 1);
    }
}