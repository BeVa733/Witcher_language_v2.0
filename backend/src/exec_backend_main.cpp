#include <stdio.h>

#include "exec_codegen.h"
#include "program_symbols.h"
#include "tree_io.h"

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        printf("Usage: exec_backend <input_tree.txt> <output.asm>\n");
        return 1;
    }

    tree_read_result tree_result = {};
    tree_read_result_ctor(&tree_result);

    if (!tree_read_from_file(argv[1], &tree_result))
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

    FILE* out = fopen(argv[2], "w");
    if (out == NULL)
    {
        printf("Failed to open output file '%s'\n", argv[2]);
        program_symbols_reset(&symbols);
        tree_read_result_reset(&tree_result);
        return 1;
    }

    exec_result result = {};
    exec_result_ctor(&result);

    if (!exec_generate_program(tree_result.root, &symbols, out, &result))
    {
        fclose(out);
        printf("Code generation error: %s\n", result.error_text);
        remove(argv[2]);
        program_symbols_reset(&symbols);
        tree_read_result_reset(&tree_result);
        return 1;
    }

    fclose(out);
    program_symbols_reset(&symbols);
    tree_read_result_reset(&tree_result);

    printf("Code generation successful\n");
    return 0;
}
