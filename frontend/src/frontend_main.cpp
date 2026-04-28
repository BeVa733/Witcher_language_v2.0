#include <stdio.h>
#include <stdlib.h>

#include "frontend_lex.h"
#include "frontend_parse.h"
#include "tree.h"

#ifndef NDEBUG
static bool write_debug_artifacts(const lexer_result* lexer,
                                  const parser_result* parser,
                                  const char* tokens_path,
                                  const char* serialized_path,
                                  const char* dot_path)
{
    FILE* tokens_file = fopen(tokens_path, "w");
    if (tokens_file != NULL)
    {
        dump_lexer_tokens(lexer, tokens_file);
        fclose(tokens_file);
    }
    else
    {
        fprintf(stderr, "Warning: failed to open token dump file '%s'\n", tokens_path);
    }

    FILE* serialized_file = fopen(serialized_path, "w");
    if (serialized_file == NULL)
    {
        fprintf(stderr, "Failed to open serialized output file '%s'\n", serialized_path);
        return false;
    }

    if (!tree_serialize(parser->root, serialized_file))
    {
        fprintf(stderr, "Failed to serialize AST to '%s'\n", serialized_path);
        fclose(serialized_file);
        return false;
    }

    fputc('\n', serialized_file);
    fclose(serialized_file);

    if (!tree_dump_dot(parser->root, dot_path))
    {
        fprintf(stderr, "Warning: failed to write dot dump to '%s'\n", dot_path);
    }

    return true;
}
#endif

static void print_usage(const char* program_name)
{
    fprintf(stderr,
            "Usage: %s <input_file> [serialized_tree.txt] [tree.dot] [tokens.txt]\n",
            program_name != NULL ? program_name : "frontend"
           );
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    const char* input_path      = argv[1];
    const char* serialized_path = (argc > 2) ? argv[2] : "tree_output.txt";
    const char* dot_path        = (argc > 3) ? argv[3] : "tree_output.dot";
    const char* tokens_path     = (argc > 4) ? argv[4] : "tokens_dump.txt";

    lexer_result lexer = {};
    parser_result parser = {};
    lexer_result_ctor(&lexer);
    parser_result_ctor(&parser);

    if (!lex_file(input_path, &lexer))
    {
        fprintf(stderr,
                "Lexer error at %d:%d: %s\n",
                lexer.error_line,
                lexer.error_column,
                lexer.error_text[0] != '\0' ? lexer.error_text : "unknown lexer error"
               );
        lexer_result_reset(&lexer);
        return 2;
    }

    if (!parse_program(&lexer, &parser))
    {
        fprintf(stderr,
                "Parser error at %d:%d: %s\n",
                parser.error_line,
                parser.error_column,
                parser.error_text[0] != '\0' ? parser.error_text : "unknown parser error"
               );
        lexer_result_reset(&lexer);
        parser_result_reset(&parser);
        return 3;
    }

#ifndef NDEBUG
    if (!write_debug_artifacts(&lexer, &parser, tokens_path, serialized_path, dot_path))
    {
        lexer_result_release_array(&lexer);
        parser_result_reset(&parser);
        return 4;
    }
#else
    (void)serialized_path;
    (void)dot_path;
    (void)tokens_path;
#endif

    lexer_result_release_array(&lexer);
    parser_result_reset(&parser);

    return 0;
}
