#!/usr/bin/env python3
"""
patch_ptr.py - 为 WELS-C 编译器添加指针支持
用法: python3 tools/patch_ptr.py [codegen.c 路径]
默认: src/codegen.c
"""

import sys
import re
import shutil
from pathlib import Path

CODEGEN = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('src/codegen.c')
BACKUP  = CODEGEN.with_suffix(CODEGEN.suffix + '.bak')

NEW_UNARY = r'''        case EX_UNARY:
            /* 取地址：&a -> lea off(%rbp), %rax */
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
            break;'''

NEW_ASSIGN = r'''        case EX_ASSIGN:
            gen_expr(e->right);
            if (e->left->kind == EX_IDENT) {
                int off = local_find(e->left->name, e->left->name_len);
                emit("    movq %%rax, %d(%%rbp)\n", off);
            }
            else if (e->left->kind == EX_UNARY &&
                     is_op_text(e->left->op_text, e->left->op_len, "*") &&
                     e->left->operand->kind == EX_IDENT) {
                int off = local_find(e->left->operand->name, e->left->operand->name_len);
                emit("    movq %d(%%rbp), %%rcx\n", off);
                emit("    movq %%rax, (%%rcx)\n");
            }
            break;'''

def main():
    if not CODEGEN.exists():
        print(f"错误: 找不到 {CODEGEN}")
        sys.exit(1)

    src = CODEGEN.read_text(encoding='utf-8')
    shutil.copy(CODEGEN, BACKUP)

    pattern_unary  = re.compile(r'        case EX_UNARY:.*?break;',  re.DOTALL)
    pattern_assign = re.compile(r'        case EX_ASSIGN:.*?break;', re.DOTALL)

    new_src, n1 = pattern_unary.subn(NEW_UNARY, src, count=1)
    new_src, n2 = pattern_assign.subn(NEW_ASSIGN, new_src, count=1)

    if n1 == 0 or n2 == 0:
        print(f"!! 未匹配到 EX_UNARY ({n1}) 或 EX_ASSIGN ({n2})")
        print(f"   原文件未改动，备份在 {BACKUP}")
        sys.exit(1)

    CODEGEN.write_text(new_src, encoding='utf-8')
    print(f"OK: {CODEGEN} 已修改")
    print(f"    备份: {BACKUP}")

if __name__ == '__main__':
    main()