#include <stdio.h>
#include <stdlib.h>

#include "middle_end.h"

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <input_tree.txt> <output_tree.txt>\n", argv[0]);
        return 1;
    }

    char error_text[512] = "";
    if (!middle_end_process_file(argv[1], argv[2], error_text, sizeof(error_text)))
    {
        fprintf(stderr, "Middle-end error: %s\n", error_text[0] != '\0' ? error_text : "unknown error");
        return 1;
    }

    printf("Middle-end simplification successful\n");
    return 0;
}
