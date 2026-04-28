#include <stdio.h>

#include "tree_io.h"
#include "reverse_end.h"

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        printf("Usage: reverse_end <serialized_tree.txt> <output_source.vdm>\n");
        return 1;
    }

    tree_read_result read_result = {};
    tree_read_result_ctor(&read_result);

    if (!tree_read_from_file(argv[1], &read_result))
    {
        printf("Reverse-end read error: %s", read_result.error_text);
        if (read_result.error_offset >= 0)
            printf(" (offset %d)", read_result.error_offset);
        printf("\n");
        tree_read_result_reset(&read_result);
        return 1;
    }

    reverse_end_options options = {};
    reverse_end_options_ctor(&options);

    if (!reverse_end_write_source_file(read_result.root, argv[2], &options))
    {
        printf("Reverse-end write error\n");
        tree_read_result_reset(&read_result);
        return 1;
    }

    tree_read_result_reset(&read_result);
    return 0;
}
