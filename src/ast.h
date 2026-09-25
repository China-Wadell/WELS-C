#ifndef AST_H
#define AST_H
#include "wescc.h"

#define MAX_STRUCT_FIELDS    32
#define MAX_STRUCT_INSTANCES 32

typedef struct {
    const char *base; int base_len;
    int is_ptr, is_ref, is_array, is_range;
    int is_const, is_static, is_local, is_container;
    int64_t range_lo, range_hi;
    int range_lo_open, range_hi_open;
} type_desc_t;

typedef enum {
    EX_INT, EX_FLOAT, EX_STRING, EX_IDENT,
    EX_BINARY, EX_UNARY, EX_CALL, EX_ASSIGN,
    EX_CONST_DECL, EX_ARRAY_INIT, EX_INDEX, EX_MEMBER, EX_ARROW,
    EX_TERNARY, EX_COMMA,
    EX_TYPED, EX_CONVERT,
} expr_kind_t;

typedef struct expr {
    expr_kind_t kind;
    int line, col;
    int64_t ival; double fval;
    const char *name; int name_len;
    const char *op_text; int op_len;
    struct expr *left, *right, *operand;
    struct expr **args; int nargs;
    type_desc_t typed_type;
    int is_postfix;
} expr_t;

typedef enum {
    ST_LET, ST_EXPR, ST_IF, ST_WHILE, ST_FOR, ST_RETURN, ST_BLOCK,
    ST_BREAK, ST_CONTINUE, ST_MATCH, ST_LABEL, ST_GOTO, ST_ASM,
    ST_CONT_DECL, ST_CONT_PUT, ST_CONT_TAKE, ST_CONT_CLEAR,
    ST_FALLTHROUGH,
} stmt_kind_t;

typedef struct stmt {
    stmt_kind_t kind;
    int line, col;
    type_desc_t type;
    const char *name; int name_len;
    expr_t *init, *expr, *cond, *for_cond, *for_step;
    struct stmt *then_s, *else_s, *body, *for_init;
    struct stmt **stmts; int nstmts;

    expr_t **case_values; int ncases;
    struct stmt *default_s;
} stmt_t;

typedef struct {
    const char *name; int name_len;
    type_desc_t type;
} param_t;

typedef struct {
    const char *name; int name_len;
    param_t *params; int nparams;
    type_desc_t ret_type; int has_ret;
    int has_varargs;
    stmt_t *body;
} func_t;

typedef struct { const char *text; int len; } include_t;

typedef struct {
    const char *name; int name_len;
    int offset;
    type_desc_t type;
} struct_field_t;

typedef struct {
    const char *name; int name_len;
    int is_union;
    struct_field_t fields[MAX_STRUCT_FIELDS];
    int nfields;
    const char *inst_name[MAX_STRUCT_INSTANCES];
    int inst_name_len[MAX_STRUCT_INSTANCES];
    int64_t inst_init[MAX_STRUCT_INSTANCES][MAX_STRUCT_FIELDS];
    const char *inst_str[MAX_STRUCT_INSTANCES][MAX_STRUCT_FIELDS];
    int         inst_str_len[MAX_STRUCT_INSTANCES][MAX_STRUCT_FIELDS];
    int ninstances;
} struct_def_t;

typedef struct {
    include_t *includes; int nincludes;
    func_t **funcs; int nfuncs;
    stmt_t **globals; int nglobals;
    struct_def_t *structs; int nstructs;
} program_t;

void ast_free_program(program_t *p);
void ast_print_program(program_t *p);
#endif
