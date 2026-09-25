#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

int codegen_program(program_t *p, const char *out_path);

/* 新增这一行 */
void codegen_set_target(int is_windows);

#endif