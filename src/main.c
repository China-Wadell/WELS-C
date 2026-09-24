#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "codegen.h"

static char *read_file(const char *path, int *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    
    /* 修复警告：检查 fread 的返回值 */
    if (fread(buf, 1, size, f) != (size_t)size) {
        fprintf(stderr, "读取文件失败\n");
        fclose(f);
        free(buf);
        return NULL;
    }
    buf[size] = 0;
    fclose(f);

    *out_len = (int)size;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "用法: wescc <file.wec> [-o out.s] [-target windows|linux]\n");
        return 1;
    }

    const char *in_path  = argv[1];
    const char *out_path = "out.s";

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        }
        /* 新增：解析目标平台 */
        else if (strcmp(argv[i], "-target") == 0 && i + 1 < argc) {
            if (strcmp(argv[i+1], "windows") == 0) {
                codegen_set_target(1); /* Windows 平台 */
            } else {
                codegen_set_target(0); /* Linux 平台 */
            }
            i++;
        }
    }

    int len;
    char *src = read_file(in_path, &len);
    if (!src) return 1;

    lexer_t L;
    lex_init(&L, src, len);

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