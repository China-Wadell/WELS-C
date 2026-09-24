#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

int parse_program(lexer_t *L, program_t *out);

#endif