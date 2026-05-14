#ifndef MIDDLE_END_H
#define MIDDLE_END_H

#include "tree.h"

bool middle_end_simplify(node_t* root);
bool middle_end_process_file(const char* input_tree_filename,
                             const char* output_tree_filename,
                             char* error_text,
                             size_t error_text_size);

#endif
