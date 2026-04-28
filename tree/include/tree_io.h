#ifndef TREE_IO_H
#define TREE_IO_H

#include "tree.h"

const int TREE_IO_ERROR_TEXT_SIZE = 256;

struct tree_read_result
{
    node_t* root;
    int error_offset;
    char error_text[TREE_IO_ERROR_TEXT_SIZE];
};

void tree_read_result_ctor(tree_read_result* result);
void tree_read_result_reset(tree_read_result* result);

bool tree_read_from_text(const char* text, tree_read_result* result);
bool tree_read_from_file(const char* filename, tree_read_result* result);

#endif
