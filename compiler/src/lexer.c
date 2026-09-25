#include "lexer.h"

typedef struct { const char *text; int len; int id; } kw_entry_t;

static const kw_entry_t g_kws[] = {
    /* 控制流 */
    { "if", 2, KW_IF }, { "若", 3, KW_IF },
    { "else", 4, KW_ELSE }, { "否则", 6, KW_ELSE },
    { "elif", 4, KW_ELIF }, { "否则若", 9, KW_ELIF },
    { "while", 5, KW_WHILE }, { "当", 3, KW_WHILE },
    { "for", 3, KW_FOR }, { "循环", 6, KW_FOR },
    { "break", 5, KW_BREAK }, { "跳", 3, KW_BREAK },
    { "continue", 8, KW_CONTINUE }, { "续", 3, KW_CONTINUE },
    { "return", 6, KW_RETURN }, { "返", 3, KW_RETURN },
    { "match", 5, KW_MATCH }, { "匹配", 6, KW_MATCH },
    { "case", 4, KW_CASE }, { "情形", 6, KW_CASE },
    { "default", 7, KW_DEFAULT }, { "默认", 6, KW_DEFAULT },
    { "fallthrough", 11, KW_FALLTHROUGH }, { "落", 3, KW_FALLTHROUGH },
    { "goto", 4, KW_GOTO }, { "转", 3, KW_GOTO },

    /* 声明 */
    { "is", 2, KW_IS }, { "为", 3, KW_IS },
    { "const", 5, KW_CONST }, { "常量", 6, KW_CONST },
    { "modify", 6, KW_MODIFY }, { "修改", 6, KW_MODIFY },

    /* 类型 */
    { "int", 3, KW_INT }, { "整数", 6, KW_INT },
    { "long", 4, KW_LONG }, { "长整", 6, KW_LONG },
    { "short", 5, KW_SHORT }, { "短整", 6, KW_SHORT },
    { "byte", 4, KW_BYTE }, { "字节", 6, KW_BYTE },
    { "unsigned", 8, KW_UNSIGNED }, { "无符", 6, KW_UNSIGNED },
    { "float", 5, KW_FLOAT }, { "浮点", 6, KW_FLOAT },
    { "double", 6, KW_DOUBLE }, { "双精", 6, KW_DOUBLE },
    { "char", 4, KW_CHAR }, { "字符", 6, KW_CHAR },
    { "bool", 4, KW_BOOL }, { "布尔", 6, KW_BOOL },
    { "void", 4, KW_VOID }, { "空", 3, KW_VOID },
    { "string", 6, KW_STRING }, { "字符串", 9, KW_STRING },
    { "true", 4, KW_TRUE }, { "真", 3, KW_TRUE },
    { "false", 5, KW_FALSE }, { "假", 3, KW_FALSE },

    /* 类型修饰 */
    { "ptr", 3, KW_PTR }, { "指针", 6, KW_PTR },
    { "ref", 3, KW_REF }, { "引用", 6, KW_REF },
    { "array", 5, KW_ARRAY }, { "数组", 6, KW_ARRAY },
    { "range", 5, KW_RANGE }, { "区间", 6, KW_RANGE },

    /* 复合 */
    { "struct", 6, KW_STRUCT }, { "结构", 6, KW_STRUCT },
    { "union", 5, KW_UNION }, { "联合", 6, KW_UNION },
    { "enum", 4, KW_ENUM }, { "枚举", 6, KW_ENUM },

    /* 函数 */
    { "fn", 2, KW_FN }, { "函", 3, KW_FN }, { "函数", 6, KW_FN },

    /* 类型转换 */
    { "convert", 7, KW_CONVERT }, { "转化为", 9, KW_CONVERT },

    /* 标签 */
    { "label", 5, KW_LABEL }, { "标", 3, KW_LABEL },

    /* 作用域 */
    { "global", 6, KW_GLOBAL }, { "全局", 6, KW_GLOBAL },
    { "local", 5, KW_LOCAL }, { "局部", 6, KW_LOCAL },
    { "static", 6, KW_STATIC }, { "静态", 6, KW_STATIC },

    /* 容器 */
    { "container", 9, KW_CONTAINER }, { "容器", 6, KW_CONTAINER },
    { "clear", 5, KW_CLEAR }, { "清", 3, KW_CLEAR },
    { "take", 4, KW_TAKE }, { "取", 3, KW_TAKE },

    /* 块 */
    { "begin", 5, KW_BEGIN }, { "开始", 6, KW_BEGIN },
    { "end", 3, KW_END }, { "结束", 6, KW_END },

    /* 汇编 */
    { "asm", 3, KW_ASM }, { "汇编", 6, KW_ASM },

    /* 大小 */
    { "sizeof", 6, KW_SIZEOF }, { "大小", 6, KW_SIZEOF },

    /* 空值 */
    { "∅", 3, KW_EMPTY },

    /* 声明前缀 */
    { "设", 3, KW_SET }, { "let", 3, KW_SET },

    /* 模块 */
    { "call", 4, KW_CALL }, { "调用", 6, KW_CALL },
    { "import", 6, KW_IMPORT }, { "导入", 6, KW_IMPORT },
    { "alias", 5, KW_ALIAS }, { "别名", 6, KW_ALIAS },
    { "extern", 6, KW_EXTERN }, { "外来", 6, KW_EXTERN },
};

#define KW_COUNT ((int)(sizeof(g_kws) / sizeof(g_kws[0])))

static int kw_lookup(const char *s, int len) {
    for (int i = 0; i < KW_COUNT; i++)
        if (g_kws[i].len == len && memcmp(g_kws[i].text, s, len) == 0)
            return g_kws[i].id;
    return KW_NONE;
}

typedef struct { const char *text; int len; } op_entry_t;

static const op_entry_t g_ops[] = {
    { "**", 2 }, { "<<=", 3 }, { ">>=", 3 }, { "...", 3 },
    { "==", 2 }, { "\\=", 2 }, { "<=", 2 }, { ">=", 2 },
    { "&&", 2 }, { "||", 2 }, { "<<", 2 }, { ">>", 2 },
    { "+=", 2 }, { "-=", 2 }, { "*=", 2 }, { "/=", 2 },
    { "%=", 2 }, { "&=", 2 }, { "|=", 2 }, { "^=", 2 },
    { "++", 2 }, { "--", 2 }, { "->", 2 },
    { "+", 1 }, { "-", 1 }, { "*", 1 }, { "/", 1 }, { "%", 1 },
    { "=", 1 }, { "<", 1 }, { ">", 1 }, { "!", 1 },
    { "&", 1 }, { "|", 1 }, { "^", 1 }, { "~", 1 },
    { "?", 1 }, { ":", 1 }, { ".", 1 },
};

#define OP_COUNT ((int)(sizeof(g_ops) / sizeof(g_ops[0])))

void lex_init(lexer_t *L, const char *src, int len) {
    L->src = src; L->len = len; L->pos = 0; L->line = 1; L->col = 1;
}

static int peek(lexer_t *L) {
    return (L->pos >= L->len) ? -1 : (unsigned char)L->src[L->pos];
}
static int peek_at(lexer_t *L, int off) {
    return (L->pos + off >= L->len) ? -1 : (unsigned char)L->src[L->pos + off];
}
static void advance(lexer_t *L) {
    if (L->pos >= L->len) return;
    char c = L->src[L->pos++];
    if (c == '\n') { L->line++; L->col = 1; } else L->col++;
}
static int is_utf8_lead(int b) { return b >= 0x80; }

static void skip_ws_and_comments(lexer_t *L) {
    for (;;) {
        int c = peek(L);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(L); continue; }

        /* 单行注释 _注释_ */
        if (c == '_' &&
            peek_at(L, 1) == 0xE6 && peek_at(L, 2) == 0xB3 && peek_at(L, 3) == 0xA8 &&
            peek_at(L, 4) == 0xE9 && peek_at(L, 5) == 0x87 && peek_at(L, 6) == 0x8A &&
            peek_at(L, 7) == '_') {
            advance(L); advance(L); advance(L); advance(L);
            advance(L); advance(L); advance(L); advance(L);
            while (peek(L) != -1 && peek(L) != '\n') advance(L);
            continue;
        }

        /* 多行注释 _/ ... /_ */
        if (c == '_' && peek_at(L, 1) == '/') {
            advance(L); advance(L);
            for (;;) {
                if (peek(L) == -1) return;
                if (peek(L) == '/' && peek_at(L, 1) == '_') { advance(L); advance(L); break; }
                advance(L);
            }
            continue;
        }
        break;
    }
}

static int is_ident_start_ascii(int b) {
    return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || b == '_';
}
static int is_ident_cont_ascii(int b) {
    return is_ident_start_ascii(b) || (b >= '0' && b <= '9');
}

static void read_ident(lexer_t *L, token_t *t) {
    t->start = L->src + L->pos;
    while (1) {
        int c = peek(L);
        if (c == -1) break;
        if (is_ident_cont_ascii(c)) { advance(L); continue; }
        if (is_utf8_lead(c)) { advance(L); continue; }
        break;
    }
    t->len = (int)(L->src + L->pos - t->start);
    int kw = kw_lookup(t->start, t->len);
    if (kw != KW_NONE) { t->kind = TOK_KEYWORD; t->kw_id = kw; return; }

    int n = t->len;
    if (n >= 2) {
        char last = t->start[n-1];
        char buf[64];
        int k = n - 1; if (k > 63) k = 63;
        memcpy(buf, t->start, k); buf[k] = 0;

        if (last == 'H' || last == 'h') {
            int ok = 1;
            for (int i = 0; i < n-1; i++)
                if (!isxdigit((unsigned char)t->start[i])) { ok = 0; break; }
            if (ok) {
                t->kind = TOK_NUM_INT;
                t->ival = strtoll(buf, NULL, 16);
                return;
            }
        }
        if (last == 'D' || last == 'd') {
            int ok = 1;
            for (int i = 0; i < n-1; i++)
                if (!isdigit((unsigned char)t->start[i])) { ok = 0; break; }
            if (ok) {
                t->kind = TOK_NUM_INT;
                t->ival = strtoll(buf, NULL, 10);
                return;
            }
        }
        if (last == 'B' || last == 'b') {
            int ok = 1;
            for (int i = 0; i < n-1; i++)
                if (t->start[i] != '0' && t->start[i] != '1') { ok = 0; break; }
            if (ok) {
                t->kind = TOK_NUM_INT;
                t->ival = strtoll(buf, NULL, 2);
                return;
            }
        }
    }
    t->kind = TOK_IDENT;
}

static void read_number(lexer_t *L, token_t *t) {
    t->start = L->src + L->pos;

    /* 0xFFFF */
    if (peek(L) == '0' && (peek_at(L, 1) == 'x' || peek_at(L, 1) == 'X')) {
        advance(L); advance(L);
        while (isxdigit(peek(L))) advance(L);
        t->len = (int)(L->src + L->pos - t->start);
        t->kind = TOK_NUM_INT;
        t->ival = strtoll(t->start, NULL, 16);
        return;
    }

    /* 扫描字母数字，尝试识别后缀 */
    int save_pos = L->pos;
    while (isalnum(peek(L))) advance(L);
    int alen = L->pos - save_pos;
    if (alen > 0) {
        char last = L->src[L->pos - 1];
        char buf[64];
        int n = alen - 1; if (n > 63) n = 63;
        memcpy(buf, t->start, n); buf[n] = 0;
        if (last == 'H' || last == 'h') {
            t->ival = strtoll(buf, NULL, 16);
            t->len = alen; t->kind = TOK_NUM_INT;
            return;
        }
        if (last == 'D' || last == 'd') {
            t->ival = strtoll(buf, NULL, 10);
            t->len = alen; t->kind = TOK_NUM_INT;
            return;
        }
        if (last == 'B' || last == 'b') {
            int ok = 1;
            for (int i = 0; i < alen - 1; i++)
                if (t->start[i] != '0' && t->start[i] != '1') { ok = 0; break; }
            if (ok) {
                t->ival = strtoll(buf, NULL, 2);
                t->len = alen; t->kind = TOK_NUM_INT;
                return;
            }
        }
    }
    L->pos = save_pos;

    while (isdigit(peek(L))) advance(L);

    /* 浮点 */
    int is_float = 0;
    if (peek(L) == '.' && isdigit(peek_at(L, 1))) {
        is_float = 1; advance(L);
        while (isdigit(peek(L))) advance(L);
    }
    if (peek(L) == 'e' || peek(L) == 'E') {
        is_float = 1; advance(L);
        if (peek(L) == '+' || peek(L) == '-') advance(L);
        while (isdigit(peek(L))) advance(L);
    }

    t->len = (int)(L->src + L->pos - t->start);
    if (is_float) { t->kind = TOK_NUM_FLOAT; t->fval = strtod(t->start, NULL); }
    else { t->kind = TOK_NUM_INT; t->ival = strtoll(t->start, NULL, 10); }
}

/* 字符串：从 " 开始，到"后面紧跟 , ) 空白 换行"的 " 为止 */
static int read_string(lexer_t *L, token_t *t) {
    int sl = L->line, sc = L->col;
    advance(L);
    t->start = L->src + L->pos;
    while (1) {
        int c = peek(L);
        if (c == -1) { fprintf(stderr, "%d:%d: 未闭合字符串\n", sl, sc); return -1; }
        if (c == '"') {
            int nc = peek_at(L, 1);
            if (nc == -1 || nc == ',' || nc == ')' ||
                nc == ' ' || nc == '\t' || nc == '\n' || nc == '\r' ||
                nc == ';' ||
                nc == '\\') {
                break;
            }
        }
        advance(L);
    }
    t->len = (int)(L->src + L->pos - t->start);
    advance(L);
    t->kind = TOK_STRING;
    return 0;
}

static int read_char(lexer_t *L, token_t *t) {
    advance(L);
    t->start = L->src + L->pos;
    if (peek(L) == -1) return -1;
    /* 简单取一个字节 */
    advance(L);
    t->len = (int)(L->src + L->pos - t->start);
    if (peek(L) != '\'') return -1;
    advance(L);
    t->kind = TOK_CHAR;
    t->ival = (unsigned char)t->start[0];
    return 0;
}

static int read_preproc(lexer_t *L, token_t *t) {
    advance(L);   /* # */
    if (peek(L) != '<') return 1;
    advance(L);
    t->start = L->src + L->pos;
    while (1) {
        int c = peek(L);
        if (c == -1 || c == '\n') return -1;
        if (c == '>') break;
        advance(L);
    }
    t->len = (int)(L->src + L->pos - t->start);
    advance(L);
    t->kind = TOK_PREPROC;
    return 0;
}

int lex_next(lexer_t *L, token_t *t) {
    memset(t, 0, sizeof(*t));
    skip_ws_and_comments(L);
    t->line = L->line; t->col = L->col;

    int c = peek(L);
    if (c == -1) { t->kind = TOK_EOF; return 0; }

    if (is_ident_start_ascii(c) || is_utf8_lead(c)) { read_ident(L, t); return 0; }
    if (isdigit(c)) { read_number(L, t); return 0; }
    if (c == '"') return read_string(L, t);
    if (c == '\'') return read_char(L, t);

    /* 反斜杠转义：字符串外只允许 \n 和 \t */
    if (c == '\\') {
    int n0 = peek_at(L, 1);
    /* \= 是不等于运算符，交给 g_ops 处理 */
    if (n0 != '=') {
    static char esc_buf[2];
    int n = n0;
    char v;
    if      (n == 'n')  v = '\n';
    else if (n == 't')  v = '\t';
    else {
        fprintf(stderr, "%d:%d: 未知转义 \\%c（字符串外只允许 \\n 和 \\t）\n",
                L->line, L->col, n);
        return -1;
    }
    advance(L); advance(L);
    esc_buf[0] = v;
    t->start = esc_buf;
    t->len = 1;
    t->kind = TOK_STRING;
    return 0;
    }
    }  /* end if n0 != '=' */

    if (c == '#') {
        int r = read_preproc(L, t);
        if (r == 0) return 0;
        if (r < 0) return -1;
    }

    if (c == '(' || c == ')' || c == '{' || c == '}' ||
        c == '[' || c == ']' || c == ',' || c == ';') {
        t->start = L->src + L->pos; t->len = 1; advance(L);
        t->kind = TOK_PUNCT; return 0;
    }

    for (int i = 0; i < OP_COUNT; i++) {
        if (L->pos + g_ops[i].len > L->len) continue;
        if (memcmp(L->src + L->pos, g_ops[i].text, g_ops[i].len) == 0) {
            t->start = L->src + L->pos; t->len = g_ops[i].len;
            for (int k = 0; k < g_ops[i].len; k++) advance(L);
            t->kind = TOK_OP; return 0;
        }
    }

    fprintf(stderr, "%d:%d: 非法字符 0x%02x\n", L->line, L->col, c);
    return -1;
}

const char *tok_kind_name(tok_kind_t k) {
    switch (k) {
        case TOK_EOF: return "EOF"; case TOK_IDENT: return "IDENT";
        case TOK_NUM_INT: return "INT"; case TOK_NUM_FLOAT: return "FLOAT";
        case TOK_STRING: return "STRING"; case TOK_CHAR: return "CHAR";
        case TOK_KEYWORD: return "KW"; case TOK_OP: return "OP";
        case TOK_PUNCT: return "PUNCT"; case TOK_PREPROC: return "PREPROC";
    }
    return "?";
}

const char *kw_name(int kw_id) {
    for (int i = 0; i < KW_COUNT; i++)
        if (g_kws[i].id == kw_id) return g_kws[i].text;
    return "?";
}