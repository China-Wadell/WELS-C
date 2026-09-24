#ifndef LEXER_H
#define LEXER_H

#include "wescc.h"

typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_NUM_INT,
    TOK_NUM_FLOAT,
    TOK_STRING,
    TOK_CHAR,
    TOK_KEYWORD,
    TOK_OP,
    TOK_PUNCT,
    TOK_PREPROC,
} tok_kind_t;

typedef enum {
    KW_NONE = 0,

    /* 控制流 */
    KW_IF, KW_ELSE, KW_ELIF,
    KW_WHILE, KW_FOR, KW_BREAK, KW_CONTINUE, KW_RETURN,
    KW_MATCH, KW_CASE, KW_DEFAULT, KW_FALLTHROUGH, KW_GOTO,

    /* 声明 */
    KW_IS,       /* 为 / is */
    KW_CONST,    /* 常量 / const */

    /* 类型 */
    KW_INT, KW_LONG, KW_SHORT, KW_BYTE, KW_UNSIGNED,
    KW_FLOAT, KW_DOUBLE, KW_CHAR, KW_BOOL,
    KW_VOID, KW_STRING,
    KW_TRUE, KW_FALSE,

    /* 类型修饰 */
    KW_PTR, KW_REF, KW_ARRAY, KW_RANGE,

    /* 复合 */
    KW_STRUCT, KW_UNION, KW_ENUM,

    /* 函数 */
    KW_FN,

    /* 作用域 */
    KW_GLOBAL, KW_LOCAL, KW_STATIC,

    /* 容器 */
    KW_CONTAINER, KW_CLEAR, KW_TAKE,

    /* 块 */
    KW_BEGIN, KW_END,

    /* 汇编 */
    KW_ASM,

    /* 大小 */
    KW_SIZEOF,
} kw_id_t;

typedef struct {
    tok_kind_t  kind;
    const char *start;
    int         len;
    int         line;
    int         col;
    int64_t     ival;
    double      fval;
    int         kw_id;
} token_t;

typedef struct {
    const char *src;
    int         len;
    int         pos;
    int         line;
    int         col;
} lexer_t;

void lex_init(lexer_t *L, const char *src, int len);
int  lex_next(lexer_t *L, token_t *t);

const char *tok_kind_name(tok_kind_t k);
const char *kw_name(int kw_id);

#endif