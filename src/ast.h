#ifndef AST_H
#define AST_H

#include "wescc.h"

typedef enum {
    EX_INT, EX_FLOAT, EX_STRING, EX_IDENT,
    EX_BINARY, EX_UNARY, EX_CALL, EX_ASSIGN,
    EX_CONST_DECL,
    EX_ARRAY_INIT,
    EX_INDEX,
} expr_kind_t;

typedef struct expr {
    expr_kind_t kind;
    int line, col;

    int64_t ival;
    double  fval;

    const char *name; int name_len;
    const char *op_text; int op_len;

    struct expr *left;
    struct expr *right;
    struct expr *operand;

    struct expr **args;
    int           nargs;
} expr_t;

typedef struct {
    const char *base;   int base_len;
    int         is_ptr;
    int         is_ref;
    int         is_array;
    int         is_range;
    int         is_const;
    int         is_static;
    int         is_local;
    int         is_container;
} type_desc_t;

typedef enum {
    ST_LET, ST_EXPR, ST_IF, ST_WHILE, ST_FOR, ST_RETURN, ST_BLOCK,
} stmt_kind_t;

typedef struct stmt {
    stmt_kind_t kind;
    int line, col;

    type_desc_t type;
    const char *name; int name_len;
    expr_t     *init;

    expr_t *expr;

    expr_t *cond;
    struct stmt *then_s;
    struct stmt *else_s;
    struct stmt *body;

    struct stmt *for_init;
    expr_t      *for_cond;
    expr_t      *for_step;

    struct stmt **stmts;
    int           nstmts;
} stmt_t;

typedef struct {
    const char *name; int name_len;
    type_desc_t type;
} param_t;

typedef struct {
    const char  *name; int name_len;
    param_t     *params; int nparams;
    type_desc_t  ret_type;
    int          has_ret;
    stmt_t      *body;
} func_t;

typedef struct {
    const char *text; int len;
} include_t;

typedef struct {
    include_t *includes; int nincludes;
    func_t   **funcs;    int nfuncs;
} program_t;

void ast_free_program(program_t *p);
void ast_print_program(program_t *p);

#endif
