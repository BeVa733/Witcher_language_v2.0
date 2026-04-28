#ifndef PROGRAM_SYMBOLS_H
#define PROGRAM_SYMBOLS_H

#include "tree.h"

const int PROGRAM_SYMBOLS_ERROR_TEXT_SIZE = 256;

struct variable_slot
{
    char* name;
    int offset;
    bool is_parameter;
};

struct function_symbol
{
    char* name;
    node_t* function_root;
    node_t* body_root;
    node_t* params_root;

    variable_slot* params;
    int param_count;

    variable_slot* locals;
    int local_count;

    int frame_size;
    int old_ex_offset;
};

struct program_symbols
{
    function_symbol* functions;
    int function_count;
    int entry_index;

    char error_text[PROGRAM_SYMBOLS_ERROR_TEXT_SIZE];
};

void program_symbols_ctor(program_symbols* symbols);
void program_symbols_reset(program_symbols* symbols);

bool collect_program_symbols(const node_t* program_root, program_symbols* symbols);

const function_symbol* find_function_symbol(const program_symbols* symbols, const char* name);
const variable_slot* find_param_slot(const function_symbol* function, const char* name);
const variable_slot* find_local_slot(const function_symbol* function, const char* name);
const variable_slot* find_any_slot(const function_symbol* function, const char* name);

#endif
