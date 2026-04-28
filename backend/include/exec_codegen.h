#ifndef EXEC_CODEGEN_H
#define EXEC_CODEGEN_H

#include <stdio.h>

#include "program_symbols.h"
#include "tree.h"

const int EXEC_ERROR_TEXT_SIZE = 256;

struct exec_result
{
    char error_text[EXEC_ERROR_TEXT_SIZE];
};

void exec_result_ctor(exec_result* result);

bool exec_generate_program(const node_t* program_root,
                           const program_symbols* symbols,
                           FILE* out,
                           exec_result* result);

#endif
