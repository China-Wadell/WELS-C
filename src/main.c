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
    if (fread(buf, 1, size, f) != (size_t)size) {
        fclose(f); free(buf); return NULL;
    }
    buf[size] = 0;
    fclose(f);
    *out_len = (int)size;
    return buf;
}

/* ---------- 头文件预处理 ---------- */
#define MAX_INCLUDE 64
static char *g_included[MAX_INCLUDE];
static int   g_nincluded = 0;

static int already_included(const char *p) {
    for (int i = 0; i < g_nincluded; i++)
        if (strcmp(g_included[i], p) == 0) return 1;
    return 0;
}

typedef struct { char *buf; int len; int cap; } sb_t;
static void sb_init(sb_t *s) { s->cap = 256; s->len = 0; s->buf = malloc(s->cap); s->buf[0] = 0; }
static void sb_append(sb_t *s, const char *d, int n) {
    while (s->len + n + 1 > s->cap) { s->cap *= 2; s->buf = realloc(s->buf, s->cap); }
    memcpy(s->buf + s->len, d, n);
    s->len += n;
    s->buf[s->len] = 0;
}
static void sb_appendc(sb_t *s, char c) { sb_append(s, &c, 1); }

static char *pp_process(const char *src, int len, const char *base_dir, int depth) {
    if (depth > 16) { fprintf(stderr, "错误: 头文件嵌套过深\n"); exit(1); }

    sb_t out; sb_init(&out);
    int i = 0;
    while (i < len) {
        if (src[i] == '#' && i + 1 < len && src[i+1] == '<') {
            int j = i + 2;
            while (j < len && src[j] != '>' && src[j] != '\n') j++;
            if (j < len && src[j] == '>') {
                int plen = j - (i + 2);
                char path[512];
                if (plen >= 512) plen = 511;
                memcpy(path, src + i + 2, plen);
                path[plen] = 0;

                char full[1024];
                if (path[0] == '/' || path[0] == '\\') {
                    snprintf(full, sizeof(full), "%s", path);
                } else {
                    snprintf(full, sizeof(full), "%s/%s", base_dir, path);
                }

                if (already_included(full)) { i = j + 1; continue; }

                int hlen;
                char *hsrc = read_file(full, &hlen);
                if (!hsrc) {
                    fprintf(stderr, "错误: 无法打开头文件 %s\n", full);
                    exit(1);
                }
                if (g_nincluded < MAX_INCLUDE) g_included[g_nincluded++] = strdup(full);

                char *expanded = pp_process(hsrc, hlen, base_dir, depth + 1);
                free(hsrc);
                sb_append(&out, expanded, (int)strlen(expanded));
                free(expanded);

                i = j + 1;
                continue;
            }
        }
        sb_appendc(&out, src[i++]);
    }
    return out.buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: wescc <file.wec> [-o out.s] [-target windows|linux]\n");
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
        memcpy(dir, in_path, dlen);
        dir[dlen] = 0;
    } else {
        dir[0] = '.'; dir[1] = 0;
    }

    char *src = pp_process(raw, len, dir, 0);
    free(raw);

    lexer_t L;
    lex_init(&L, src, (int)strlen(src));

    program_t prog;
    memset(&prog, 0, sizeof(prog));

    if (parse_program(&L, &prog) < 0) {
        fprintf(stderr, "解析失败\n");
        free(src);
        return 1;
    }
    if (codegen_program(&prog, out_path) < 0) {
        fprintf(stderr, "代码生成失败\n");
        ast_free_program(&prog);
        free(src);
        return 1;
    }
    printf("生成: %s\n", out_path);
    ast_free_program(&prog);
    free(src);
    return 0;
}
