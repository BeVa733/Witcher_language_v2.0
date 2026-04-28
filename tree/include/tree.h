#ifndef TREE_H
#define TREE_H

#include <stdio.h>

#include "lexemes.h"

enum node_data_type
{
    DATA_NONE   = 0,
    DATA_INT    = 1,
    DATA_OPER   = 2,
    DATA_STRING = 3
};

enum node_type
{
    NODE_NONE = 0,
    NODE_OPER = 1,
    NODE_NUM  = 2,
    NODE_ID   = 3,
    NODE_VAR  = 4,
    NODE_FUNC = 5,
    NODE_TEXT = 6,
    NODE_GLUE = 7
};

union data_member
{
    int number;
    enum opers oper;
    char* string;
};

struct node_t
{
    union data_member data;
    node_t* left;
    node_t* right;
    node_t* parent;
    enum node_type type;
    enum node_data_type data_type;
    int line;
    int column;
};

const char* node_type_name(enum node_type type);
const char* node_data_type_name(enum node_data_type type);

node_t* node_create_num(int value, int line, int column);
node_t* node_create_oper(enum opers oper, int line, int column);
node_t* node_create_id(const char* name, int line, int column);
node_t* node_create_var(const char* name, int line, int column);
node_t* node_create_func(const char* name, int line, int column);
node_t* node_create_text(const char* text, int line, int column);
node_t* node_create_glue(int line, int column);

bool node_promote_id(node_t* node, enum node_type target_type);

void node_set_left(node_t* parent, node_t* child);
void node_set_right(node_t* parent, node_t* child);
node_t* node_detach_left(node_t* parent);
node_t* node_detach_right(node_t* parent);

node_t* node_clone(const node_t* root);
bool    node_verify(const node_t* root);

void tree_dtor(node_t* root);

bool tree_dump_dot(const node_t* root, const char* dot_path);
bool tree_serialize(const node_t* root, FILE* out);

#endif
