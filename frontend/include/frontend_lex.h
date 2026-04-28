#ifndef FRONTEND_LEX_H
#define FRONTEND_LEX_H

#include <stdio.h>

#include "tree.h"

const int LEXER_START_CAPACITY = 64;
const int LEXER_ERROR_TEXT_SIZE = 256;

struct lexer_result
{
    node_t** tokens;
    int token_count;
    int capacity;
    int error_line;
    int error_column;
    char error_text[LEXER_ERROR_TEXT_SIZE];
};

void lexer_result_ctor(lexer_result* result);
void lexer_result_reset(lexer_result* result);
void lexer_result_release_array(lexer_result* result);

char* read_source_file(const char* file_name);

bool lex_source(const char* source, lexer_result* result);
bool lex_file(const char* file_name, lexer_result* result);

void dump_lexer_tokens(const lexer_result* result, FILE* out);

#endif
