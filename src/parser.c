#include "parser.h"

typedef struct {
    lexer_t *L;
    token_t  cur;
    token_t  next;
    int      has_next;
} parser_t;

static int parse_expr(parser_t *P, expr_t **out);
static int parse_block(parser_t *P, stmt_t **out);
static int parse_stmt(parser_t *P, stmt_t **out);
static int parse_expr_stmt(parser_t *P, stmt_t **out);
static int parse_shift(parser_t *P, expr_t **out);
static int parse_bitand(parser_t *P, expr_t **out);
static int parse_bitxor(parser_t *P, expr_t **out);
static int parse_bitor(parser_t *P, expr_t **out);

static void p_advance(parser_t *P) {
    if (!P->has_next) {
        if (lex_next(P->L, &P->next) < 0) P->next.kind = TOK_EOF;
        P->has_next = 1;
    }
    P->cur = P->next;
    P->has_next = 0;
}

static void p_peek(parser_t *P) {
    if (!P->has_next) {
        if (lex_next(P->L, &P->next) < 0) P->next.kind = TOK_EOF;
        P->has_next = 1;
    }
}

static int p_err(parser_t *P, const char *msg) {
    fprintf(stderr, "%d:%d: 语法错误: %s\n", P->cur.line, P->cur.col, msg);
    return -1;
}

static int is_kw(parser_t *P, int kw) {
    return P->cur.kind == TOK_KEYWORD && P->cur.kw_id == kw;
}

static int is_op(parser_t *P, const char *op) {
    return P->cur.kind == TOK_OP
        && P->cur.len == (int)strlen(op)
        && memcmp(P->cur.start, op, P->cur.len) == 0;
}

static int is_punct(parser_t *P, int c) {
    return P->cur.kind == TOK_PUNCT
        && P->cur.len == 1
        && P->cur.start[0] == c;
}

static int expect_punct(parser_t *P, int c, const char *what) {
    if (!is_punct(P, c)) return p_err(P, what);
    p_advance(P);
    return 0;
}

static int expect_op(parser_t *P, const char *op, const char *what) {
    if (!is_op(P, op)) return p_err(P, what);
    p_advance(P);
    return 0;
}

static expr_t *new_expr(expr_kind_t k) {
    expr_t *e = calloc(1, sizeof(*e));
    if (!e) { fprintf(stderr, "out of memory\n"); exit(1); }
    e->kind = k;
    return e;
}

static stmt_t *new_stmt(stmt_kind_t k) {
    stmt_t *s = calloc(1, sizeof(*s));
    if (!s) { fprintf(stderr, "out of memory\n"); exit(1); }
    s->kind = k;
    return s;
}

static void add_stmt(stmt_t *blk, stmt_t *s) {
    blk->stmts = realloc(blk->stmts, sizeof(stmt_t *) * (blk->nstmts + 1));
    blk->stmts[blk->nstmts++] = s;
}

/* ============ 类型解析 ============ */

/* 读到一串关键字/标识符，遇到修饰符或终止符停止 */
static int parse_type(parser_t *P, type_desc_t *out) {
    memset(out, 0, sizeof(*out));

    /* 修饰符（前缀） */
    for (;;) {
        if (is_kw(P, KW_CONST))      { out->is_const = 1; p_advance(P); continue; }
        if (is_kw(P, KW_STATIC))     { out->is_static = 1; p_advance(P); continue; }
        if (is_kw(P, KW_LOCAL))      { out->is_local = 1; p_advance(P); continue; }
        if (is_kw(P, KW_GLOBAL))     { out->is_static = 1; p_advance(P); continue; }
        if (is_kw(P, KW_CONTAINER))  { out->is_container = 1; p_advance(P); continue; }
        break;
    }

    /* 基础类型 */
    if (P->cur.kind == TOK_KEYWORD || P->cur.kind == TOK_IDENT) {
        out->base = P->cur.start;
        out->base_len = P->cur.len;
        p_advance(P);
    } else {
        return p_err(P, "期望类型");
    }

    /* 修饰后缀（可叠加） */
    for (;;) {
        if (is_kw(P, KW_PTR))   { out->is_ptr = 1; p_advance(P); continue; }
        if (is_kw(P, KW_REF))   { out->is_ref = 1; p_advance(P); continue; }
        if (is_kw(P, KW_ARRAY)) { out->is_array = 1; p_advance(P); continue; }
        if (is_kw(P, KW_RANGE)) { out->is_range = 1; p_advance(P); continue; }
        break;
    }
    return 0;
}

/* ============ 表达式 ============ */

static int parse_primary(parser_t *P, expr_t **out) {
    p_peek(P);

    if (P->cur.kind == TOK_NUM_INT) {
        expr_t *e = new_expr(EX_INT);
        e->line = P->cur.line; e->col = P->cur.col;
        e->ival = P->cur.ival;
        p_advance(P); *out = e; return 0;
    }
    if (P->cur.kind == TOK_NUM_FLOAT) {
        expr_t *e = new_expr(EX_FLOAT);
        e->line = P->cur.line; e->col = P->cur.col;
        e->fval = P->cur.fval;
        p_advance(P); *out = e; return 0;
    }
    if (P->cur.kind == TOK_STRING) {
        expr_t *e = new_expr(EX_STRING);
        e->line = P->cur.line; e->col = P->cur.col;
        e->name = P->cur.start; e->name_len = P->cur.len;
        p_advance(P); *out = e; return 0;
    }
    if (P->cur.kind == TOK_CHAR) {
        expr_t *e = new_expr(EX_INT);
        e->line = P->cur.line; e->col = P->cur.col;
        e->ival = P->cur.ival;
        p_advance(P); *out = e; return 0;
    }
    if (P->cur.kind == TOK_IDENT) {
        expr_t *e = new_expr(EX_IDENT);
        e->line = P->cur.line; e->col = P->cur.col;
        e->name = P->cur.start; e->name_len = P->cur.len;
        p_advance(P); *out = e; return 0;
    }
    if (is_punct(P, '{')) {
        p_advance(P);
        expr_t *e = new_expr(EX_ARRAY_INIT);
        e->line = P->cur.line; e->col = P->cur.col;
        while (!is_punct(P, '}') && P->cur.kind != TOK_EOF) {
            expr_t *elem;
            if (parse_expr(P, &elem) < 0) return -1;
            e->args = realloc(e->args, sizeof(expr_t*) * (e->nargs + 1));
            e->args[e->nargs++] = elem;
            p_peek(P);
            if (is_punct(P, ',')) p_advance(P);
        }
        if (expect_punct(P, '}', "期望 '}'") < 0) return -1;
        *out = e; return 0;
    }
    if (is_punct(P, '(')) {
        p_advance(P);
        if (parse_expr(P, out) < 0) return -1;
        return expect_punct(P, ')', "期望 ')'");
    }
    return p_err(P, "期望表达式");
}

static int parse_postfix(parser_t *P, expr_t **out) {
    if (parse_primary(P, out) < 0) return -1;
    for (;;) {
        p_peek(P);
        if (is_punct(P, '(')) {
            p_advance(P);
            expr_t *call = new_expr(EX_CALL);
            call->line = (*out)->line; call->col = (*out)->col;
            call->operand = *out;
            while (!is_punct(P, ')') && P->cur.kind != TOK_EOF) {
                expr_t *arg;
                if (parse_expr(P, &arg) < 0) return -1;
                call->args = realloc(call->args, sizeof(expr_t*) * (call->nargs + 1));
                call->args[call->nargs++] = arg;
                p_peek(P);
                if (is_punct(P, ',')) p_advance(P);
            }
            if (expect_punct(P, ')', "期望 ')'") < 0) return -1;
            *out = call;
            continue;
        }
        if (is_punct(P, '[')) {
            p_advance(P);
            expr_t *idx = new_expr(EX_INDEX);
            idx->line = (*out)->line; idx->col = (*out)->col;
            idx->left = *out;
            if (parse_expr(P, &idx->right) < 0) return -1;
            if (expect_punct(P, ']', "期望 ']'") < 0) return -1;
            *out = idx;
            continue;
        }
        if (is_op(P, ".")) {
            p_advance(P);
            if (P->cur.kind != TOK_IDENT) return p_err(P, "期望字段名");
            expr_t *mem = new_expr(EX_MEMBER);
            mem->line = (*out)->line; mem->col = (*out)->col;
            mem->left = *out;
            mem->name = P->cur.start;
            mem->name_len = P->cur.len;
            p_advance(P);
            *out = mem;
            continue;
        }
        if (is_op(P, "->")) {
            p_advance(P);
            if (P->cur.kind != TOK_IDENT) return p_err(P, "期望字段名");
            expr_t *mem = new_expr(EX_ARROW);
            mem->line = (*out)->line; mem->col = (*out)->col;
            mem->left = *out;
            mem->name = P->cur.start;
            mem->name_len = P->cur.len;
            p_advance(P);
            *out = mem;
            continue;
        }
        break;
    }
    return 0;
}

static int parse_unary(parser_t *P, expr_t **out) {
    p_peek(P);
    if (is_op(P, "-") || is_op(P, "!") || is_op(P, "~") ||
        is_op(P, "*") || is_op(P, "&") ||
        is_op(P, "++") || is_op(P, "--")) {
        expr_t *e = new_expr(EX_UNARY);
        e->line = P->cur.line; e->col = P->cur.col;
        e->op_text = P->cur.start; e->op_len = P->cur.len;
        p_advance(P);
        if (parse_unary(P, &e->operand) < 0) return -1;
        *out = e; return 0;
    }
    return parse_postfix(P, out);
}

static int parse_pow(parser_t *P, expr_t **out) {
    if (parse_unary(P, out) < 0) return -1;
    p_peek(P);
    while (is_op(P, "**")) {
        expr_t *e = new_expr(EX_BINARY);
        e->line = P->cur.line; e->col = P->cur.col;
        e->op_text = P->cur.start; e->op_len = P->cur.len;
        p_advance(P);
        e->left = *out;
        if (parse_unary(P, &e->right) < 0) return -1;
        *out = e;
        p_peek(P);
    }
    return 0;
}

static int parse_mul(parser_t *P, expr_t **out) {
    if (parse_pow(P, out) < 0) return -1;
    p_peek(P);
    while (is_op(P, "*") || is_op(P, "/") || is_op(P, "%")) {
        expr_t *e = new_expr(EX_BINARY);
        e->line = P->cur.line; e->col = P->cur.col;
        e->op_text = P->cur.start; e->op_len = P->cur.len;
        p_advance(P);
        e->left = *out;
        if (parse_pow(P, &e->right) < 0) return -1;
        *out = e;
        p_peek(P);
    }
    return 0;
}

static int parse_add(parser_t *P, expr_t **out) {
    if (parse_mul(P, out) < 0) return -1;
    p_peek(P);
    while (is_op(P, "+") || is_op(P, "-")) {
        expr_t *e = new_expr(EX_BINARY);
        e->line = P->cur.line; e->col = P->cur.col;
        e->op_text = P->cur.start; e->op_len = P->cur.len;
        p_advance(P);
        e->left = *out;
        if (parse_mul(P, &e->right) < 0) return -1;
        *out = e;
        p_peek(P);
    }
    return 0;
}

static int parse_shift(parser_t *P, expr_t **out) {
    if (parse_add(P, out) < 0) return -1;
    p_peek(P);
    while (is_op(P, "<<") || is_op(P, ">>")) {
        expr_t *e = new_expr(EX_BINARY);
        e->line = P->cur.line; e->col = P->cur.col;
        e->op_text = P->cur.start; e->op_len = P->cur.len;
        p_advance(P);
        e->left = *out;
        if (parse_add(P, &e->right) < 0) return -1;
        *out = e;
        p_peek(P);
    }
    return 0;
}

static int parse_bitand(parser_t *P, expr_t **out) {
    if (parse_shift(P, out) < 0) return -1;
    p_peek(P);
    while (is_op(P, "&")) {
        expr_t *e = new_expr(EX_BINARY);
        e->line = P->cur.line; e->col = P->cur.col;
        e->op_text = P->cur.start; e->op_len = P->cur.len;
        p_advance(P);
        e->left = *out;
        if (parse_shift(P, &e->right) < 0) return -1;
        *out = e;
        p_peek(P);
    }
    return 0;
}

static int parse_bitxor(parser_t *P, expr_t **out) {
    if (parse_bitand(P, out) < 0) return -1;
    p_peek(P);
    while (is_op(P, "^")) {
        expr_t *e = new_expr(EX_BINARY);
        e->line = P->cur.line; e->col = P->cur.col;
        e->op_text = P->cur.start; e->op_len = P->cur.len;
        p_advance(P);
        e->left = *out;
        if (parse_bitand(P, &e->right) < 0) return -1;
        *out = e;
        p_peek(P);
    }
    return 0;
}

static int parse_bitor(parser_t *P, expr_t **out) {
    if (parse_bitxor(P, out) < 0) return -1;
    p_peek(P);
    while (is_op(P, "|")) {
        expr_t *e = new_expr(EX_BINARY);
        e->line = P->cur.line; e->col = P->cur.col;
        e->op_text = P->cur.start; e->op_len = P->cur.len;
        p_advance(P);
        e->left = *out;
        if (parse_bitxor(P, &e->right) < 0) return -1;
        *out = e;
        p_peek(P);
    }
    return 0;
}

static int parse_cmp(parser_t *P, expr_t **out) {
    if (parse_bitor(P, out) < 0) return -1;
    p_peek(P);
    while (is_op(P, "<") || is_op(P, ">") || is_op(P, "<=") ||
           is_op(P, ">=") || is_op(P, "==") || is_op(P, "\\=")) {
        expr_t *e = new_expr(EX_BINARY);
        e->line = P->cur.line; e->col = P->cur.col;
        e->op_text = P->cur.start; e->op_len = P->cur.len;
        p_advance(P);
        e->left = *out;
        if (parse_bitor(P, &e->right) < 0) return -1;
        *out = e;
        p_peek(P);
    }
    return 0;
}

static int parse_logic_and(parser_t *P, expr_t **out) {
    if (parse_cmp(P, out) < 0) return -1;
    p_peek(P);
    while (is_op(P, "&&")) {
        expr_t *e = new_expr(EX_BINARY);
        e->line = P->cur.line; e->col = P->cur.col;
        e->op_text = P->cur.start; e->op_len = P->cur.len;
        p_advance(P);
        e->left = *out;
        if (parse_cmp(P, &e->right) < 0) return -1;
        *out = e;
        p_peek(P);
    }
    return 0;
}

static int parse_logic_or(parser_t *P, expr_t **out) {
    if (parse_logic_and(P, out) < 0) return -1;
    p_peek(P);
    while (is_op(P, "||")) {
        expr_t *e = new_expr(EX_BINARY);
        e->line = P->cur.line; e->col = P->cur.col;
        e->op_text = P->cur.start; e->op_len = P->cur.len;
        p_advance(P);
        e->left = *out;
        if (parse_logic_and(P, &e->right) < 0) return -1;
        *out = e;
        p_peek(P);
    }
    return 0;
}

static int parse_assign(parser_t *P, expr_t **out) {
    if (parse_logic_or(P, out) < 0) return -1;
    p_peek(P);
    if (is_op(P, "=") || is_op(P, "+=") || is_op(P, "-=") ||
        is_op(P, "*=") || is_op(P, "/=") || is_op(P, "%=")) {
        expr_t *e = new_expr(EX_ASSIGN);
        e->line = P->cur.line; e->col = P->cur.col;
        e->op_text = P->cur.start; e->op_len = P->cur.len;
        p_advance(P);
        e->left = *out;
        if (parse_assign(P, &e->right) < 0) return -1;
        *out = e;
    }
    return 0;
}

static int parse_expr(parser_t *P, expr_t **out) {
    return parse_assign(P, out);
}

/* ============ 语句 ============ */

/* 变量名 为 类型 值; */
static int parse_struct(parser_t *P, struct_def_t *sd, int is_union) {
    memset(sd, 0, sizeof(*sd));
    sd->is_union = is_union;
    if (P->cur.kind != TOK_IDENT) return p_err(P, "期望结构名");
    sd->name = P->cur.start; sd->name_len = P->cur.len;
    p_advance(P);

    type_desc_t ty;
    if (parse_type(P, &ty) < 0) return -1;

    if (expect_punct(P, '(', "期望 '('") < 0) return -1;
    if (!is_punct(P, ')')) {
        for (;;) {
            if (P->cur.kind != TOK_IDENT) return p_err(P, "期望字段名");
            int fi = sd->nfields;
            sd->fields[fi].name = P->cur.start;
            sd->fields[fi].name_len = P->cur.len;
            sd->fields[fi].offset = is_union ? 0 : fi * 8;
            sd->nfields++;
            p_advance(P);
            p_peek(P);
            if (is_punct(P, ',')) { p_advance(P); continue; }
            break;
        }
    }
    if (expect_punct(P, ')', "期望 ')'") < 0) return -1;

    if (expect_punct(P, '{', "期望 '{'") < 0) return -1;
    while (!is_punct(P, '}') && P->cur.kind != TOK_EOF) {
        if (P->cur.kind != TOK_IDENT) return p_err(P, "期望实例名");
        int idx = sd->ninstances;
        sd->inst_name[idx] = P->cur.start;
        sd->inst_name_len[idx] = P->cur.len;
        sd->ninstances++;
        p_advance(P);

        if (expect_punct(P, '(', "期望 '('") < 0) return -1;
        int fi = 0;
        while (!is_punct(P, ')') && P->cur.kind != TOK_EOF) {
            if (P->cur.kind == TOK_NUM_INT) {
                sd->inst_init[idx][fi++] = P->cur.ival;
                p_advance(P);
            } else if (P->cur.kind == TOK_STRING) {
                sd->inst_init[idx][fi++] = 0;
                p_advance(P);
            } else return p_err(P, "期望初值");
            p_peek(P);
            if (is_punct(P, ',')) p_advance(P);
        }
        if (expect_punct(P, ')', "期望 ')'") < 0) return -1;
        p_peek(P);
        if (is_punct(P, ';')) p_advance(P);
    }
    if (expect_punct(P, '}', "期望 '}'") < 0) return -1;
    p_peek(P);
    if (is_punct(P, ';')) p_advance(P);
    return 0;
}

static int parse_enum(parser_t *P, program_t *out) {
    p_advance(P);
    if (P->cur.kind != TOK_IDENT) return p_err(P, "期望枚举名");
    p_advance(P);
    if (expect_punct(P, '{', "期望 '{'") < 0) return -1;
    int64_t next_val = 0;
    while (!is_punct(P, '}') && P->cur.kind != TOK_EOF) {
        if (P->cur.kind != TOK_IDENT) return p_err(P, "期望枚举项名");
        const char *nm = P->cur.start;
        int nml = P->cur.len;
        int64_t val = next_val;
        p_advance(P);
        p_peek(P);
        if (is_op(P, "=")) {
            p_advance(P);
            if (P->cur.kind != TOK_NUM_INT) return p_err(P, "期望数字");
            val = P->cur.ival;
            p_advance(P);
        }
        stmt_t *s = new_stmt(ST_LET);
        s->name = nm; s->name_len = nml;
        s->type.is_static = 1;
        s->type.base = "整数"; s->type.base_len = 6;
        expr_t *e = new_expr(EX_INT);
        e->ival = val;
        s->init = e;
        out->globals = realloc(out->globals, sizeof(stmt_t*) * (out->nglobals + 1));
        out->globals[out->nglobals++] = s;
        next_val = val + 1;
        p_peek(P);
        if (is_punct(P, ',')) p_advance(P);
    }
    if (expect_punct(P, '}', "期望 '}'") < 0) return -1;
    p_peek(P);
    if (is_punct(P, ';')) p_advance(P);
    return 0;
}

static int parse_modify(parser_t *P, stmt_t **out) {
    p_advance(P);
    if (P->cur.kind != TOK_IDENT) return p_err(P, "期望变量名");
    const char *name = P->cur.start;
    int nlen = P->cur.len;
    p_advance(P);

    /* 可选：常量 关键字 */
    p_peek(P);
    if (is_kw(P, KW_CONST)) {
        p_advance(P);
        /* 可选：类型 */
        p_peek(P);
        if (P->cur.kind == TOK_KEYWORD &&
            (P->cur.kw_id == KW_INT || P->cur.kw_id == KW_DOUBLE ||
             P->cur.kw_id == KW_FLOAT || P->cur.kw_id == KW_LONG)) {
            p_advance(P);
        }
        /* 可选：旧值 */
        p_peek(P);
        if (P->cur.kind == TOK_NUM_INT || P->cur.kind == TOK_NUM_FLOAT) {
            p_advance(P);
        }
    }

    if (!is_kw(P, KW_IS)) return p_err(P, "期望 '为'");
    p_advance(P);

    expr_t *val;
    if (parse_expr(P, &val) < 0) return -1;
    if (expect_punct(P, ';', "期望 ';'") < 0) return -1;

    stmt_t *s = new_stmt(ST_EXPR);
    expr_t *assign = new_expr(EX_ASSIGN);
    assign->op_text = "="; assign->op_len = 1;
    expr_t *lhs = new_expr(EX_IDENT);
    lhs->name = name; lhs->name_len = nlen;
    assign->left = lhs; assign->right = val;
    s->expr = assign;
    *out = s;
    return 0;
}

static int parse_let(parser_t *P, stmt_t **out) {
    stmt_t *s = new_stmt(ST_LET);
    s->line = P->cur.line; s->col = P->cur.col;

    /* 名 */
    if (P->cur.kind != TOK_IDENT) return p_err(P, "期望变量名");
    s->name = P->cur.start;
    s->name_len = P->cur.len;
    p_advance(P);

    /* 为 / is / 常量 */
    int was_const = 0;
    if (is_kw(P, KW_IS)) {
        p_advance(P);
    } else if (is_kw(P, KW_CONST)) {
        was_const = 1;
        p_advance(P);
    } else {
        return p_err(P, "期望 '为' 或 'is' 或 '常量'");
    }

    /* 类型（parse_type 会 memset，所以 flag 要在它之后再设） */
    if (parse_type(P, &s->type) < 0) return -1;

    if (was_const) {
        s->type.is_const  = 1;
        s->type.is_static = 1;
    }

    /* 值（可选） */
    p_peek(P);
    if (!is_punct(P, ';')) {
        if (parse_expr(P, &s->init) < 0) return -1;
    }

    if (expect_punct(P, ';', "期望 ';'") < 0) return -1;
    *out = s; return 0;
}

static int parse_return(parser_t *P, stmt_t **out) {
    stmt_t *s = new_stmt(ST_RETURN);
    s->line = P->cur.line; s->col = P->cur.col;
    p_advance(P);
    p_peek(P);
    if (!is_punct(P, ';')) {
        if (parse_expr(P, &s->expr) < 0) return -1;
    }
    if (expect_punct(P, ';', "期望 ';'") < 0) return -1;
    *out = s; return 0;
}

static int parse_if(parser_t *P, stmt_t **out) {
    stmt_t *s = new_stmt(ST_IF);
    s->line = P->cur.line; s->col = P->cur.col;
    p_advance(P);
    if (parse_expr(P, &s->cond) < 0) return -1;
    if (parse_block(P, &s->then_s) < 0) return -1;
    p_peek(P);
    if (is_kw(P, KW_ELSE)) {
        p_advance(P);
        p_peek(P);
        if (is_kw(P, KW_IF)) {
            if (parse_if(P, &s->else_s) < 0) return -1;
        } else {
            if (parse_block(P, &s->else_s) < 0) return -1;
        }
    }
    *out = s; return 0;
}

static int parse_while(parser_t *P, stmt_t **out) {
    stmt_t *s = new_stmt(ST_WHILE);
    s->line = P->cur.line; s->col = P->cur.col;
    p_advance(P);
    if (parse_expr(P, &s->cond) < 0) return -1;
    if (parse_block(P, &s->body) < 0) return -1;
    *out = s; return 0;
}

static int parse_for(parser_t *P, stmt_t **out) {
    stmt_t *s = new_stmt(ST_FOR);
    s->line = P->cur.line; s->col = P->cur.col;
    p_advance(P);

    /* init: 声明或空 */
    p_peek(P);
    if (is_punct(P, ';')) {
        p_advance(P);
    } else if (P->cur.kind == TOK_IDENT) {
    /* 向后偷看一个 token，判断是声明还是赋值 */
    token_t save = P->cur;
    p_peek(P);
    int next_is_is = (P->next.kind == TOK_KEYWORD && (P->next.kw_id == KW_IS || P->next.kw_id == KW_CONST));
    P->cur = save;

    if (next_is_is) {
        if (parse_let(P, &s->for_init) < 0) return -1;        /* i 为 整数 0; */
    } else {
        if (parse_expr_stmt(P, &s->for_init) < 0) return -1;  /* i = 0; */
    }
    } else {
        p_err(P, "期望 for 初始化");
        return -1;
    }

    /* cond */
    p_peek(P);
    if (!is_punct(P, ';')) {
        if (parse_expr(P, &s->for_cond) < 0) return -1;
    }
    if (expect_punct(P, ';', "期望 ';'") < 0) return -1;

    /* step */
    p_peek(P);
    if (!is_punct(P, '{')) {
        if (parse_expr(P, &s->for_step) < 0) return -1;
    }

    if (parse_block(P, &s->body) < 0) return -1;
    *out = s; return 0;
}

static int parse_expr_stmt(parser_t *P, stmt_t **out) {
    stmt_t *s = new_stmt(ST_EXPR);
    s->line = P->cur.line; s->col = P->cur.col;
    if (parse_expr(P, &s->expr) < 0) return -1;
    if (expect_punct(P, ';', "期望 ';'") < 0) return -1;
    *out = s; return 0;
}

static int parse_stmt(parser_t *P, stmt_t **out) {
    p_peek(P);

    if (is_kw(P, KW_RETURN))                       return parse_return(P, out);
    if (is_kw(P, KW_IF))                           return parse_if(P, out);
    if (is_kw(P, KW_WHILE))                        return parse_while(P, out);
    if (is_kw(P, KW_FOR))                          return parse_for(P, out);
    if (is_kw(P, KW_MODIFY))                       return parse_modify(P, out);
    if (is_kw(P, KW_BREAK)) {
        p_advance(P);
        if (expect_punct(P, ';', "期望 ';'") < 0) return -1;
        *out = new_stmt(ST_BREAK);
        return 0;
    }
    if (is_kw(P, KW_CONTINUE)) {
        p_advance(P);
        if (expect_punct(P, ';', "期望 ';'") < 0) return -1;
        *out = new_stmt(ST_CONTINUE);
        return 0;
    } // 这里现在专门处理“循环”或“for”

    /* 声明：IDENT 后面跟 '为' / 'is' */
    if (P->cur.kind == TOK_IDENT) {
        token_t save_cur = P->cur;
        p_peek(P);
        int next_is_is = (P->next.kind == TOK_KEYWORD && (P->next.kw_id == KW_IS || P->next.kw_id == KW_CONST));
        P->cur = save_cur;
        if (next_is_is) {
            return parse_let(P, out);
        }
    }

    if (is_punct(P, '{')) return parse_block(P, out);
    if (is_punct(P, ';')) {
        p_advance(P);
        *out = NULL;
        return 0;
    }

    return parse_expr_stmt(P, out);
}

static int parse_block(parser_t *P, stmt_t **out) {
    stmt_t *blk = new_stmt(ST_BLOCK);

    if (is_punct(P, '{')) {
        p_advance(P);
        for (;;) {
            p_peek(P);
            if (is_punct(P, '}')) break;
            if (P->cur.kind == TOK_EOF) return p_err(P, "未闭合的 '{'");
            stmt_t *s = NULL;
            if (parse_stmt(P, &s) < 0) return -1;
            if (s) add_stmt(blk, s);
        }
        p_advance(P);
    } else if (is_kw(P, KW_BEGIN)) {
        p_advance(P);
        for (;;) {
            p_peek(P);
            if (is_kw(P, KW_END)) break;
            if (P->cur.kind == TOK_EOF) return p_err(P, "未闭合的 '开始'");
            stmt_t *s = NULL;
            if (parse_stmt(P, &s) < 0) return -1;
            if (s) add_stmt(blk, s);
        }
        p_advance(P);
    } else {
        return p_err(P, "期望 '{' 或 '开始'");
    }

    *out = blk;
    return 0;
}

/* 函 名 (a: 类型, ...) -> 类型 { } */
static int parse_func(parser_t *P, func_t **out) {
    func_t *f = calloc(1, sizeof(*f));
    if (!f) { fprintf(stderr, "out of memory\n"); exit(1); }

    if (P->cur.kind != TOK_IDENT) return p_err(P, "期望函数名");
    f->name = P->cur.start;
    f->name_len = P->cur.len;
    p_advance(P);

    if (expect_punct(P, '(', "期望 '('") < 0) return -1;

    if (!is_punct(P, ')')) {
        for (;;) {
            p_peek(P);
            if (P->cur.kind != TOK_IDENT) return p_err(P, "期望参数名");
            const char *nm = P->cur.start;
            int nml = P->cur.len;
            p_advance(P);

            if (expect_op(P, ":", "期望 ':'") < 0) return -1;

            type_desc_t ty;
            if (parse_type(P, &ty) < 0) return -1;

            f->params = realloc(f->params, sizeof(param_t) * (f->nparams + 1));
            f->params[f->nparams].name = nm;
            f->params[f->nparams].name_len = nml;
            f->params[f->nparams].type = ty;
            f->nparams++;

            p_peek(P);
            if (is_punct(P, ',')) { p_advance(P); continue; }
            break;
        }
    }
    if (expect_punct(P, ')', "期望 ')'") < 0) return -1;

    p_peek(P);
    if (is_op(P, "->")) {
        p_advance(P);
        if (parse_type(P, &f->ret_type) < 0) return -1;
        f->has_ret = 1;
    }

    if (parse_block(P, &f->body) < 0) return -1;
    *out = f;
    return 0;
}

int parse_program(lexer_t *L, program_t *out) {
    parser_t P;
    memset(&P, 0, sizeof(P));
    P.L = L;
    if (lex_next(L, &P.cur) < 0) return -1;

    for (;;) {
        p_peek(&P);
        if (P.cur.kind == TOK_EOF) break;

        if (P.cur.kind == TOK_PREPROC) {
            out->includes = realloc(out->includes,
                                    sizeof(include_t) * (out->nincludes + 1));
            out->includes[out->nincludes].text = P.cur.start;
            out->includes[out->nincludes].len  = P.cur.len;
            out->nincludes++;
            p_advance(&P);
            continue;
        }

        if (is_kw(&P, KW_STRUCT)) {
            p_advance(&P);
            out->structs = realloc(out->structs, sizeof(struct_def_t) * (out->nstructs + 1));
            if (parse_struct(&P, &out->structs[out->nstructs], 0) < 0) return -1;
            out->nstructs++;
            continue;
        }
        if (is_kw(&P, KW_UNION)) {
            p_advance(&P);
            out->structs = realloc(out->structs, sizeof(struct_def_t) * (out->nstructs + 1));
            if (parse_struct(&P, &out->structs[out->nstructs], 1) < 0) return -1;
            out->nstructs++;
            continue;
        }
        if (is_kw(&P, KW_ENUM)) {
            if (parse_enum(&P, out) < 0) return -1;
            continue;
        }
        if (is_kw(&P, KW_FN)) {
            p_advance(&P);
            func_t *f = NULL;
            if (parse_func(&P, &f) < 0) return -1;
            out->funcs = realloc(out->funcs, sizeof(func_t*) * (out->nfuncs + 1));
            out->funcs[out->nfuncs++] = f;
            continue;
        }

        /* 顶层全局变量：IDENT 后面跟 '为' */
        if (P.cur.kind == TOK_IDENT) {
            token_t save = P.cur;
            p_peek(&P);
            int next_is_is = (P.next.kind == TOK_KEYWORD && (P.next.kw_id == KW_IS || P.next.kw_id == KW_CONST));
            P.cur = save;
            if (next_is_is) {
                stmt_t *s = NULL;
                if (parse_let(&P, &s) < 0) return -1;
                out->globals = realloc(out->globals, sizeof(stmt_t*) * (out->nglobals + 1));
                out->globals[out->nglobals++] = s;
                continue;
            }
        }

        return p_err(&P, "期望函数定义或预编译指令");
    }
    return 0;
}