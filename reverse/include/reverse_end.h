#ifndef REVERSE_END_H
#define REVERSE_END_H

#include <stdio.h>

#include "tree.h"

struct reverse_end_options
{
    int indent_width;
    bool always_brace_blocks;
};

void reverse_end_options_ctor(reverse_end_options* options);

bool reverse_end_write_source(const node_t* root, FILE* out, const reverse_end_options* options);
bool reverse_end_write_source_file(const node_t* root, const char* filename, const reverse_end_options* options);

#endif
