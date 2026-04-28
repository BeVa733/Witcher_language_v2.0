#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "program_symbols.h"

static char* symbols_strdup(const char* source)
{
    if (source == NULL)
        return NULL;

    const size_t len = strlen(source);
    char* copy = (char*)calloc(len + 1, sizeof(char));
    if (copy == NULL)
        return NULL;

    memcpy(copy, source, len);
    copy[len] = '\0';
    return copy;
}

static void symbols_fail(program_symbols* symbols, const char* message)
{
    assert(symbols != NULL);

    if (symbols->error_text[0] != '\0')
        return;

    snprintf(symbols->error_text, PROGRAM_SYMBOLS_ERROR_TEXT_SIZE, "%s", message);
}

static bool grow_functions(program_symbols* symbols)
{
    assert(symbols != NULL);

    const int new_count = symbols->function_count + 1;
    function_symbol* resized = (function_symbol*)realloc(symbols->functions, (size_t)new_count * sizeof(function_symbol));
    if (resized == NULL)
    {
        symbols_fail(symbols, "out of memory while growing function symbol table");
        return false;
    }

    symbols->functions = resized;
    memset(&symbols->functions[symbols->function_count], 0, sizeof(function_symbol));
    symbols->function_count = new_count;
    return true;
}

static bool add_variable_slot(variable_slot** slots, int* slot_count, const char* name, int offset, bool is_parameter, program_symbols* symbols)
{
    assert(slots != NULL);
    assert(slot_count != NULL);
    assert(name != NULL);
    assert(symbols != NULL);

    variable_slot* resized = (variable_slot*)realloc(*slots, (size_t)(*slot_count + 1) * sizeof(variable_slot));
    if (resized == NULL)
    {
        symbols_fail(symbols, "out of memory while growing variable slot table");
        return false;
    }

    *slots = resized;
    (*slots)[*slot_count].name = symbols_strdup(name);
    if ((*slots)[*slot_count].name == NULL)
    {
        symbols_fail(symbols, "out of memory while copying variable name");
        return false;
    }

    (*slots)[*slot_count].offset = offset;
    (*slots)[*slot_count].is_parameter = is_parameter;
    ++(*slot_count);
    return true;
}

static const variable_slot* find_slot_in_array(const variable_slot* slots, int slot_count, const char* name)
{
    if (slots == NULL || name == NULL)
        return NULL;

    for (int index = 0; index < slot_count; ++index)
    {
        if (strcmp(slots[index].name, name) == 0)
            return &slots[index];
    }

    return NULL;
}

static bool is_assign_node(const node_t* node)
{
    return node != NULL && node->type == NODE_OPER && node->data.oper == OPER_ASSIGN;
}

static bool is_func_decl_node(const node_t* node)
{
    return node != NULL && node->type == NODE_OPER && node->data.oper == OPER_FUNC;
}

static bool collect_function_chain(const node_t* node, program_symbols* symbols)
{
    if (node == NULL)
        return true;

    if (node->type == NODE_GLUE)
    {
        return collect_function_chain(node->left, symbols) && collect_function_chain(node->right, symbols);
    }

    if (!is_func_decl_node(node))
    {
        symbols_fail(symbols, "program root must be a function or glue of functions");
        return false;
    }

    if (node->left == NULL || node->left->type != NODE_FUNC || node->left->data.string == NULL)
    {
        symbols_fail(symbols, "function declaration must have function name node on the left");
        return false;
    }

    if (find_function_symbol(symbols, node->left->data.string) != NULL)
    {
        symbols_fail(symbols, "duplicate function name in program");
        return false;
    }

    if (!grow_functions(symbols))
        return false;

    function_symbol* function = &symbols->functions[symbols->function_count - 1];
    function->name = symbols_strdup(node->left->data.string);
    if (function->name == NULL)
    {
        symbols_fail(symbols, "out of memory while copying function name");
        return false;
    }

    function->function_root = const_cast<node_t*>(node);
    function->body_root = node->right;
    function->params_root = node->left->right;
    function->old_ex_offset = 0;

    return true;
}

static bool collect_params(function_symbol* function, program_symbols* symbols)
{
    assert(function != NULL);
    assert(symbols != NULL);

    const node_t* current = function->params_root;
    int param_index = 0;

    while (current != NULL)
    {
        if (current->type != NODE_VAR || current->data.string == NULL)
        {
            symbols_fail(symbols, "function parameter list contains non-variable node");
            return false;
        }

        if (find_slot_in_array(function->params, function->param_count, current->data.string) != NULL)
        {
            symbols_fail(symbols, "duplicate parameter name inside function");
            return false;
        }

        const int offset = 1 + param_index;
        if (!add_variable_slot(&function->params, &function->param_count, current->data.string, offset, true, symbols))
            return false;

        ++param_index;
        current = current->right;
    }

    return true;
}

static bool ensure_local_slot(function_symbol* function, const char* name, program_symbols* symbols)
{
    assert(function != NULL);
    assert(name != NULL);
    assert(symbols != NULL);

    if (find_slot_in_array(function->params, function->param_count, name) != NULL)
        return true;

    if (find_slot_in_array(function->locals, function->local_count, name) != NULL)
        return true;

    const int offset = 1 + function->param_count + function->local_count;
    return add_variable_slot(&function->locals, &function->local_count, name, offset, false, symbols);
}

static bool collect_locals_from_subtree(const node_t* node, function_symbol* function, program_symbols* symbols)
{
    if (node == NULL)
        return true;

    if (is_assign_node(node))
    {
        const node_t* lvalue = node->left;
        if (lvalue == NULL || lvalue->type != NODE_VAR || lvalue->data.string == NULL)
        {
            symbols_fail(symbols, "assignment left side must be a variable node");
            return false;
        }

        if (!ensure_local_slot(function, lvalue->data.string, symbols))
            return false;
    }

    if (node->type == NODE_OPER && node->data.oper == OPER_SCAN)
    {
        const node_t* target = node->right;
        if (target == NULL || target->type != NODE_VAR || target->data.string == NULL)
        {
            symbols_fail(symbols, "scan target must be a variable node");
            return false;
        }

        if (!ensure_local_slot(function, target->data.string, symbols))
            return false;
    }

    if (!collect_locals_from_subtree(node->left, function, symbols))
        return false;
    if (!collect_locals_from_subtree(node->right, function, symbols))
        return false;

    return true;
}

void program_symbols_ctor(program_symbols* symbols)
{
    assert(symbols != NULL);

    symbols->functions = NULL;
    symbols->function_count = 0;
    symbols->entry_index = -1;
    symbols->error_text[0] = '\0';
}

void program_symbols_reset(program_symbols* symbols)
{
    assert(symbols != NULL);

    for (int function_index = 0; function_index < symbols->function_count; ++function_index)
    {
        function_symbol* function = &symbols->functions[function_index];
        free(function->name);

        for (int index = 0; index < function->param_count; ++index)
            free(function->params[index].name);
        free(function->params);

        for (int index = 0; index < function->local_count; ++index)
            free(function->locals[index].name);
        free(function->locals);
    }

    free(symbols->functions);
    program_symbols_ctor(symbols);
}

bool collect_program_symbols(const node_t* program_root, program_symbols* symbols)
{
    assert(symbols != NULL);

    program_symbols_reset(symbols);

    if (program_root == NULL)
    {
        symbols_fail(symbols, "program tree is empty");
        return false;
    }

    if (!collect_function_chain(program_root, symbols))
    {
        char error_copy[PROGRAM_SYMBOLS_ERROR_TEXT_SIZE] = "failed to collect functions";
        if (symbols->error_text[0] != '\0')
            snprintf(error_copy, sizeof(error_copy), "%s", symbols->error_text);
        program_symbols_reset(symbols);
        snprintf(symbols->error_text, PROGRAM_SYMBOLS_ERROR_TEXT_SIZE, "%s", error_copy);
        return false;
    }

    symbols->entry_index = 0;

    for (int function_index = 0; function_index < symbols->function_count; ++function_index)
    {
        function_symbol* function = &symbols->functions[function_index];

        if (!collect_params(function, symbols))
        {
            char error_copy[PROGRAM_SYMBOLS_ERROR_TEXT_SIZE] = "failed to collect function parameters";
            if (symbols->error_text[0] != '\0')
                snprintf(error_copy, sizeof(error_copy), "%s", symbols->error_text);
            program_symbols_reset(symbols);
            snprintf(symbols->error_text, PROGRAM_SYMBOLS_ERROR_TEXT_SIZE, "%s", error_copy);
            return false;
        }

        if (function_index == symbols->entry_index && function->param_count != 0)
        {
            char error_copy[PROGRAM_SYMBOLS_ERROR_TEXT_SIZE] = "entry function must not have parameters";
            symbols_fail(symbols, error_copy);
            if (symbols->error_text[0] != '\0')
                snprintf(error_copy, sizeof(error_copy), "%s", symbols->error_text);
            program_symbols_reset(symbols);
            snprintf(symbols->error_text, PROGRAM_SYMBOLS_ERROR_TEXT_SIZE, "%s", error_copy);
            return false;
        }

        if (!collect_locals_from_subtree(function->body_root, function, symbols))
        {
            char error_copy[PROGRAM_SYMBOLS_ERROR_TEXT_SIZE] = "failed to collect function locals";
            if (symbols->error_text[0] != '\0')
                snprintf(error_copy, sizeof(error_copy), "%s", symbols->error_text);
            program_symbols_reset(symbols);
            snprintf(symbols->error_text, PROGRAM_SYMBOLS_ERROR_TEXT_SIZE, "%s", error_copy);
            return false;
        }

        function->frame_size = 1 + function->param_count + function->local_count;
    }

    return true;
}

const function_symbol* find_function_symbol(const program_symbols* symbols, const char* name)
{
    if (symbols == NULL || name == NULL)
        return NULL;

    for (int index = 0; index < symbols->function_count; ++index)
    {
        if (strcmp(symbols->functions[index].name, name) == 0)
            return &symbols->functions[index];
    }

    return NULL;
}

const variable_slot* find_param_slot(const function_symbol* function, const char* name)
{
    return (function == NULL) ? NULL : find_slot_in_array(function->params, function->param_count, name);
}

const variable_slot* find_local_slot(const function_symbol* function, const char* name)
{
    return (function == NULL) ? NULL : find_slot_in_array(function->locals, function->local_count, name);
}

const variable_slot* find_any_slot(const function_symbol* function, const char* name)
{
    if (function == NULL)
        return NULL;

    const variable_slot* slot = find_param_slot(function, name);
    if (slot != NULL)
        return slot;

    return find_local_slot(function, name);
}
