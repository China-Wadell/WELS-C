#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "codegen.h"

static char *read_file(const char *path, int *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, size, f) != (size_t)size) { fclose(f); free(buf); return NULL; }
    buf[size] = 0; fclose(f);
    *out_len = (int)size;
    return buf;
}

typedef struct { char *buf; int len; int cap; } sb_t;
static void sb_init(sb_t *s) { s->cap = 256; s->len = 0; s->buf = malloc(s->cap); s->buf[0] = 0; }
static void sb_append(sb_t *s, const char *d, int n) {
    while (s->len + n + 1 > s->cap) { s->cap *= 2; s->buf = realloc(s->buf, s->cap); }
    memcpy(s->buf + s->len, d, n); s->len += n; s->buf[s->len] = 0;
}
static void sb_appendc(sb_t *s, char c) { sb_append(s, &c, 1); }

#define MAX_MACROS 128
typedef struct { char *name; char *value; } macro_t;
static macro_t g_macros[MAX_MACROS];
static int     g_nmacros = 0;

static macro_t *find_macro(const char *name, int len) {
    for (int i = 0; i < g_nmacros; i++)
        if ((int)strlen(g_macros[i].name) == len && memcmp(g_macros[i].name, name, len) == 0)
            return &g_macros[i];
    return NULL;
}
static void add_macro(const char *name, int nlen, const char *val, int vlen) {
    macro_t *m = find_macro(name, nlen);
    if (!m) {
        if (g_nmacros >= MAX_MACROS) return;
        m = &g_macros[g_nmacros++];
        m->name = malloc(nlen + 1);
        memcpy(m->name, name, nlen); m->name[nlen] = 0; m->value = 0;
    }
    free(m->value);
    m->value = malloc(vlen + 1);
    memcpy(m->value, val, vlen); m->value[vlen] = 0;
}
static void del_macro(const char *name, int nlen) {
    for (int i = 0; i < g_nmacros; i++)
        if ((int)strlen(g_macros[i].name) == nlen && memcmp(g_macros[i].name, name, nlen) == 0) {
            free(g_macros[i].name); free(g_macros[i].value);
            for (int j = i; j < g_nmacros - 1; j++) g_macros[j] = g_macros[j+1];
            g_nmacros--; return;
        }
}
static int macro_defined(const char *name, int nlen) {
    return find_macro(name, nlen) != NULL;
}

#define MAX_INCLUDE 64
static char *g_included[MAX_INCLUDE];
static int   g_nincluded = 0;
static int already_included(const char *p) {
    for (int i = 0; i < g_nincluded; i++) if (strcmp(g_included[i], p) == 0) return 1;
    return 0;
}

static int starts_with(const char *s, int len, const char *prefix) {
    int plen = (int)strlen(prefix);
    if (len < plen) return 0;
    return memcmp(s, prefix, plen) == 0;
}
static int is_ident_ch(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}
static int skip_to_eol(const char *src, int len, int i) {
    while (i < len && src[i] != '\n') i++;
    if (i < len) i++;
    return i;
}
static int skip_spaces(const char *src, int len, int i) {
    while (i < len && (src[i] == ' ' || src[i] == '\t')) i++;
    return i;
}

/* 条件编译栈 */
#define MAX_COND 32
static int  g_cond_stack[MAX_COND];
static int  g_cond_else[MAX_COND];
static int  g_cond_matched[MAX_COND];
static int  g_cond_depth = 0;
static int  g_skipping = 0;

static char *pp_process(const char *src, int len, const char *base_dir, int depth) {
    if (depth > 16) { fprintf(stderr, "错误: 头文件嵌套过深\n"); exit(1); }

    sb_t out; sb_init(&out);
    int i = 0;
    int at_line_start = 1;

    while (i < len) {
        char c = src[i];

        if (at_line_start && c == '#') {
            /* #导入<path 模块名> / #import<path name> */
            if (starts_with(src + i, len - i, "#导入<") ||
                starts_with(src + i, len - i, "#import<")) {
                if (g_skipping) { i = skip_to_eol(src, len, i); at_line_start = 1; continue; }
                int k = starts_with(src + i, len - i, "#import<") ? i + 8 : i + 8;
                int j = k;
                while (j < len && src[j] != '>' && src[j] != '\n') j++;
                if (j < len && src[j] == '>') {
                    /* 路径与模块名以空格分隔；取第一段为路径 */
                    int plen = j - k;
                    while (plen > 0 && (src[k+plen-1] == ' ' || src[k+plen-1] == '\t')) plen--;
                    int path_end = 0;
                    while (path_end < plen && src[k+path_end] != ' ' && src[k+path_end] != '\t') path_end++;

                    char path[512];
                    if (path_end >= 512) path_end = 511;
                    memcpy(path, src + k, path_end);
                    path[path_end] = 0;

                    char full[1024];
                    if (path[0] == '/' || path[0] == '\\') snprintf(full, sizeof(full), "%s", path);
                    else                                     snprintf(full, sizeof(full), "%s/%s", base_dir, path);

                    if (!already_included(full)) {
                        int hlen;
                        char *hsrc = read_file(full, &hlen);
                        if (!hsrc) { fprintf(stderr, "错误: 无法打开模块 %s\n", full); exit(1); }
                        if (g_nincluded < MAX_INCLUDE) g_included[g_nincluded++] = strdup(full);
                        char *exp = pp_process(hsrc, hlen, base_dir, depth + 1);
                        free(hsrc);
                        sb_append(&out, exp, (int)strlen(exp));
                        free(exp);
                    }
                    i = j + 1; at_line_start = 0; continue;
                }
            }
            /* #<file> */
            if (i + 1 < len && src[i+1] == '<') {
                if (g_skipping) { i = skip_to_eol(src, len, i); at_line_start = 1; continue; }
                int j = i + 2;
                while (j < len && src[j] != '>' && src[j] != '\n') j++;
                if (j < len && src[j] == '>') {
                    int plen = j - (i + 2);
                    char path[512];
                    if (plen >= 512) plen = 511;
                    memcpy(path, src + i + 2, plen); path[plen] = 0;
                    char full[1024];
                    if (path[0] == '/' || path[0] == '\\') snprintf(full, sizeof(full), "%s", path);
                    else                                    snprintf(full, sizeof(full), "%s/%s", base_dir, path);
                    if (already_included(full)) { i = j + 1; at_line_start = 0; continue; }
                    int hlen;
                    char *hsrc = read_file(full, &hlen);
                    if (!hsrc) { fprintf(stderr, "错误: 无法打开 %s\n", full); exit(1); }
                    if (g_nincluded < MAX_INCLUDE) g_included[g_nincluded++] = strdup(full);
                    char *exp = pp_process(hsrc, hlen, base_dir, depth + 1);
                    free(hsrc);
                    sb_append(&out, exp, (int)strlen(exp));
                    free(exp);
                    i = j + 1; at_line_start = 0; continue;
                }
            }

            /* 条件编译：即使 skipping 也要处理 */
            if (starts_with(src + i, len - i, "#若定义") ||
                starts_with(src + i, len - i, "#ifdef")) {
                int k = starts_with(src + i, len - i, "#ifdef") ? i + 6 : i + 10;
                k = skip_spaces(src, len, k);
                int ns = k;
                while (k < len && is_ident_ch(src[k])) k++;
                int nl = k - ns;
                int cond = (nl > 0) ? macro_defined(src + ns, nl) : 0;
                if (g_cond_depth >= MAX_COND) { fprintf(stderr, "错误: 条件嵌套过深\n"); exit(1); }
                int parent_ok = !g_skipping;
                g_cond_stack[g_cond_depth] = (parent_ok && cond) ? 1 : 0;
                g_cond_else[g_cond_depth] = 0;
                g_cond_matched[g_cond_depth] = (parent_ok && cond) ? 1 : 0;
                g_cond_depth++;
                g_skipping = !(g_cond_stack[g_cond_depth-1]);
                i = skip_to_eol(src, len, k);
                at_line_start = 1;
                continue;
            }
            if (starts_with(src + i, len - i, "#若未定") ||
                starts_with(src + i, len - i, "#ifndef")) {
                int k = starts_with(src + i, len - i, "#ifndef") ? i + 7 : i + 10;
                k = skip_spaces(src, len, k);
                int ns = k;
                while (k < len && is_ident_ch(src[k])) k++;
                int nl = k - ns;
                int cond = (nl > 0) ? !macro_defined(src + ns, nl) : 0;
                if (g_cond_depth >= MAX_COND) { fprintf(stderr, "错误: 条件嵌套过深\n"); exit(1); }
                int parent_ok = !g_skipping;
                g_cond_stack[g_cond_depth] = (parent_ok && cond) ? 1 : 0;
                g_cond_else[g_cond_depth] = 0;
                g_cond_matched[g_cond_depth] = (parent_ok && cond) ? 1 : 0;
                g_cond_depth++;
                g_skipping = !(g_cond_stack[g_cond_depth-1]);
                i = skip_to_eol(src, len, k);
                at_line_start = 1;
                continue;
            }
            if (starts_with(src + i, len - i, "#否则若") ||
                starts_with(src + i, len - i, "#elif")) {
                if (g_cond_depth == 0) { i = skip_to_eol(src, len, i); at_line_start = 1; continue; }
                int k = starts_with(src + i, len - i, "#elif") ? i + 5 : i + 10;
                k = skip_spaces(src, len, k);
                int ns = k;
                while (k < len && is_ident_ch(src[k])) k++;
                int nl = k - ns;
                int cond = (nl > 0) ? macro_defined(src + ns, nl) : 0;
                int top = g_cond_depth - 1;
                int parent_ok = (top == 0) ? 1 : g_cond_stack[top - 1];
                if (!parent_ok || g_cond_matched[top]) {
                    g_cond_stack[top] = 0;
                } else if (cond) {
                    g_cond_stack[top] = 1;
                    g_cond_matched[top] = 1;
                } else {
                    g_cond_stack[top] = 0;
                }
                g_skipping = 0;
                for (int d = 0; d < g_cond_depth; d++)
                    if (!g_cond_stack[d]) { g_skipping = 1; break; }
                i = skip_to_eol(src, len, k); at_line_start = 1; continue;
            }
            if (starts_with(src + i, len - i, "#否则") ||
                starts_with(src + i, len - i, "#else")) {
                if (g_cond_depth == 0) { i = skip_to_eol(src, len, i); at_line_start = 1; continue; }
                int k = starts_with(src + i, len - i, "#else") ? i + 5 : i + 7;
                int top = g_cond_depth - 1;
                if (g_cond_else[top]) { i = skip_to_eol(src, len, k); at_line_start = 1; continue; }
                g_cond_else[top] = 1;
                int parent_ok = (top == 0) ? 1 : g_cond_stack[top - 1];
                if (!parent_ok || g_cond_matched[top]) {
                    g_cond_stack[top] = 0;
                } else {
                    g_cond_stack[top] = 1;
                    g_cond_matched[top] = 1;
                }
                g_skipping = 0;
                for (int d = 0; d < g_cond_depth; d++)
                    if (!g_cond_stack[d]) { g_skipping = 1; break; }
                i = skip_to_eol(src, len, k); at_line_start = 1; continue;
            }
            if (starts_with(src + i, len - i, "#结束若") ||
                starts_with(src + i, len - i, "#endif")) {
                int k = starts_with(src + i, len - i, "#endif") ? i + 6 : i + 10;
                if (g_cond_depth > 0) g_cond_depth--;
                g_skipping = 0;
                for (int d = 0; d < g_cond_depth; d++)
                    if (!g_cond_stack[d]) { g_skipping = 1; break; }
                i = skip_to_eol(src, len, k);
                at_line_start = 1;
                continue;
            }

            /* #define / #定宏 */
            if (starts_with(src + i, len - i, "#define") ||
                starts_with(src + i, len - i, "#定宏")) {
                int k = (starts_with(src + i, len - i, "#define")) ? i + 7 : i + 7;
                k = skip_spaces(src, len, k);
                int ns = k;
                while (k < len && is_ident_ch(src[k])) k++;
                int nl = k - ns;
                k = skip_spaces(src, len, k);
                int vs = k;
                while (k < len && src[k] != '\n') k++;
                int vl = k - vs;
                while (vl > 0 && (src[vs+vl-1] == ' ' || src[vs+vl-1] == '\t' || src[vs+vl-1] == '\r')) vl--;
                if (nl > 0 && !g_skipping) add_macro(src + ns, nl, src + vs, vl);
                if (k < len) k++;
                i = k; at_line_start = 1; continue;
            }
            /* #undef / #消宏 */
            if (starts_with(src + i, len - i, "#undef") ||
                starts_with(src + i, len - i, "#消宏")) {
                int k = (starts_with(src + i, len - i, "#undef")) ? i + 6 : i + 7;
                k = skip_spaces(src, len, k);
                int ns = k;
                while (k < len && is_ident_ch(src[k])) k++;
                int nl = k - ns;
                if (nl > 0 && !g_skipping) del_macro(src + ns, nl);
                i = skip_to_eol(src, len, k);
                at_line_start = 1; continue;
            }
            /* 其他 # 指令 */
            if (g_skipping) { i = skip_to_eol(src, len, i); at_line_start = 1; continue; }
        }

        /* 跳过状态：整行忽略 */
        if (g_skipping) {
            if (c == '\n') at_line_start = 1;
            i++;
            continue;
        }

        /* 字符串原样 */
        if (c == '"') {
            sb_appendc(&out, src[i++]);
            while (i < len && src[i] != '"') sb_appendc(&out, src[i++]);
            if (i < len) sb_appendc(&out, src[i++]);
            at_line_start = 0; continue;
        }

        /* 标识符：查宏表 */
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            int j = i;
            while (j < len && is_ident_ch(src[j])) j++;
            macro_t *m = find_macro(src + i, j - i);
            if (m && m->value) {
                sb_append(&out, m->value, (int)strlen(m->value));
                i = j; at_line_start = 0; continue;
            }
            sb_append(&out, src + i, j - i);
            i = j; at_line_start = 0; continue;
        }

        if (c == '\n') at_line_start = 1;
        else if (c != ' ' && c != '\t' && c != '\r') at_line_start = 0;

        sb_appendc(&out, c);
        i++;
    }
    return out.buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: wescc <file.wec> [-o out.s] [-target windows|linux] [-D NAME]\n");
        return 1;
    }
    const char *in_path = argv[1];
    const char *out_path = "out.s";

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "-target") == 0 && i + 1 < argc) {
            codegen_set_target(strcmp(argv[i+1], "windows") == 0);
            i++;
        } else if (strcmp(argv[i], "-D") == 0 && i + 1 < argc) {
            add_macro(argv[i+1], (int)strlen(argv[i+1]), "", 0);
            i++;
        }
    }

    int len;
    char *raw = read_file(in_path, &len);
    if (!raw) { perror(in_path); return 1; }

    char dir[512];
    const char *slash = strrchr(in_path, '/');
    if (!slash) slash = strrchr(in_path, '\\');
    if (slash) {
        int dlen = (int)(slash - in_path);
        if (dlen >= 512) dlen = 511;
        memcpy(dir, in_path, dlen); dir[dlen] = 0;
    } else { dir[0] = '.'; dir[1] = 0; }

    char *src = pp_process(raw, len, dir, 0);
    free(raw);

    lexer_t L;
    lex_init(&L, src, (int)strlen(src));

    program_t prog;
    memset(&prog, 0, sizeof(prog));

    if (parse_program(&L, &prog) < 0) {
        fprintf(stderr, "解析失败\n"); free(src); return 1;
    }
    if (codegen_program(&prog, out_path) < 0) {
        fprintf(stderr, "代码生成失败\n"); ast_free_program(&prog); free(src); return 1;
    }
    printf("生成: %s\n", out_path);
    ast_free_program(&prog); free(src);
    return 0;
}
