#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "exec_codegen.h"
#include "program_symbols.h"
#include "tree_io.h"

static bool parse_args(int argc,
                       char* argv[],
                       const char** input_name,
                       const char** output_name,
                       output_format* format)
{
    if (argc == 3)
    {
        *input_name = argv[1];
        *output_name = argv[2];
        *format = OUT_FORMAT_ELF;
        return true;
    }

    if (argc == 4 && strcmp(argv[1], "-S") == 0)
    {
        *input_name = argv[2];
        *output_name = argv[3];
        *format = OUT_FORMAT_NASM;
        return true;
    }

    return false;
}

int main(int argc, char* argv[])
{
    const char* input_name = NULL;
    const char* output_name = NULL;
    output_format format = OUT_FORMAT_ELF;

    if (!parse_args(argc, argv, &input_name, &output_name, &format))
    {
        printf("Usage: exec_backend [-S] <input_tree.txt> <output>\n");
        return 1;
    }

    tree_read_result tree_result = {};
    tree_read_result_ctor(&tree_result);

    if (!tree_read_from_file(input_name, &tree_result))
    {
        printf("Tree read error at offset %d: %s\n",
               tree_result.error_offset,
               tree_result.error_text);
        tree_read_result_reset(&tree_result);
        return 1;
    }

    program_symbols symbols = {};
    program_symbols_ctor(&symbols);

    if (!collect_program_symbols(tree_result.root, &symbols))
    {
        printf("Symbol collection error: %s\n", symbols.error_text);
        program_symbols_reset(&symbols);
        tree_read_result_reset(&tree_result);
        return 1;
    }

    const char* open_mode = (format == OUT_FORMAT_ELF) ? "wb" : "w";
    FILE* out = fopen(output_name, open_mode);

    if (!out)
    {
        printf("Failed to open output file '%s'\n", output_name);
        program_symbols_reset(&symbols);
        tree_read_result_reset(&tree_result);
        return 1;
    }

    exec_result result = {};
    exec_result_ctor(&result);

    if (!exec_generate_program(tree_result.root, &symbols, out, format, &result))
    {
        fclose(out);
        printf("Code generation error: %s\n", result.error_text);
        remove(output_name);
        program_symbols_reset(&symbols);
        tree_read_result_reset(&tree_result);
        return 1;
    }

    fclose(out);

    if (format == OUT_FORMAT_ELF)
        chmod(output_name, 0755);

    program_symbols_reset(&symbols);
    tree_read_result_reset(&tree_result);

    printf("Code generation successful\n");
    return 0;
}
