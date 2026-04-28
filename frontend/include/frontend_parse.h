#ifndef FRONTEND_PARSE_H
#define FRONTEND_PARSE_H

#include "frontend_lex.h"

const int PARSER_ERROR_TEXT_SIZE = 256;

struct parser_result
{
    node_t* root;
    int error_line;
    int error_column;
    char error_text[PARSER_ERROR_TEXT_SIZE];
};

void parser_result_ctor(parser_result* result);
void parser_result_reset(parser_result* result);

bool parse_program(lexer_result* lexer, parser_result* result);

#endif
