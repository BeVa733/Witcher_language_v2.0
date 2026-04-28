#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "exec_codegen.h"
#include "exec_emit.h"

struct codegen_state
{
    const program_symbols* symbols;
    const function_symbol* current_function;
    FILE* out;
    exec_result* result;
};

static void codegen_fail(codegen_state* state, const char* format, ...)
{
    assert(state != NULL);
    assert(state->result != NULL);
    assert(format != NULL);

    if (state->result->error_text[0] != '\0')
        return;

    va_list args = {};
    va_start(args, format);
    vsnprintf(state->result->error_text, EXEC_ERROR_TEXT_SIZE, format, args);
    va_end(args);
}

static int get_function_index(const program_symbols* symbols, const function_symbol* function)
{
    assert(symbols  != NULL);
    assert(function != NULL);

    for (int index = 0; index < symbols->function_count; ++index)
    {
        if (&symbols->functions[index] == function)
            return index;
    }

    return -1;
}

static int get_param_stack_offset(const variable_slot* slot)
{
    assert(slot != NULL);
    assert(slot->is_parameter);
    return (slot->offset + 1) * 8;
}

static int get_local_stack_offset(const function_symbol* function, const variable_slot* slot)
{
    assert(function != NULL);
    assert(slot != NULL);
    assert(!slot->is_parameter);
    return (slot->offset - function->param_count) * 8;
}

static const variable_slot* find_variable_slot(codegen_state* state, const char* name)
{
    assert(state != NULL);
    assert(state->current_function != NULL);
    assert(name != NULL);

    const variable_slot* slot = find_any_slot(state->current_function, name);
    if (slot != NULL)
        return slot;

    codegen_fail(state,
                 "variable '%s' was not found in function '%s'",
                 name,
                 state->current_function->name);
    return NULL;
}

static bool emit_load_slot(codegen_state* state, const variable_slot* slot)
{
    assert(state != NULL);
    assert(slot  != NULL);

    if (slot->is_parameter)
        return emit_line(state->out, "    mov rax, qword [rbp + %d]", get_param_stack_offset(slot));

    return emit_line(state->out, "    mov rax, qword [rbp - %d]", get_local_stack_offset(state->current_function, slot));
}

static bool emit_store_slot(codegen_state* state, const variable_slot* slot)
{
    assert(state != NULL);
    assert(slot  != NULL);

    if (slot->is_parameter)
        return emit_line(state->out, "    mov qword [rbp + %d], rax", get_param_stack_offset(slot));

    return emit_line(state->out, "    mov qword [rbp - %d], rax", get_local_stack_offset(state->current_function, slot));
}

static bool emit_function_epilogue(codegen_state* state)
{
    assert(state != NULL);
    assert(state->out != NULL);

    return emit_line(state->out, "    mov rsp, rbp") &&
           emit_line(state->out, "    pop rbp") &&
           emit_line(state->out, "    ret");
}

static bool emit_expression(codegen_state* state, const node_t* expr)
{
    assert(state != NULL);

    if (expr == NULL)
    {
        codegen_fail(state, "expression node is null");
        return false;
    }

    if (expr->type == NODE_NUM)
        return emit_line(state->out, "    mov rax, %lld", (long long)expr->data.number);

    if (expr->type == NODE_VAR)
    {
        if (expr->data.string == NULL)
        {
            codegen_fail(state, "variable node has null name");
            return false;
        }

        const variable_slot* slot = find_variable_slot(state, expr->data.string);
        if (slot == NULL)
            return false;

        return emit_load_slot(state, slot);
    }

    if (expr->type != NODE_OPER)
    {
        codegen_fail(state, "unsupported expression node type %d", (int)expr->type);
        return false;
    }

    switch (expr->data.oper)
    {
        case OPER_ASSIGN:
        {
            const node_t* lvalue = expr->left;
            if (lvalue == NULL || lvalue->type != NODE_VAR || lvalue->data.string == NULL)
            {
                codegen_fail(state, "assignment left side must be a variable");
                return false;
            }

            const variable_slot* slot = find_variable_slot(state, lvalue->data.string);
            if (slot == NULL)
                return false;

            if (!emit_expression(state, expr->right))
                return false;

            return emit_store_slot(state, slot);
        }

        case OPER_ADD:
            return emit_expression(state, expr->left) &&
                   emit_line(state->out, "    push rax") &&
                   emit_expression(state, expr->right) &&
                   emit_line(state->out, "    pop rbx") &&
                   emit_line(state->out, "    add rax, rbx");

        case OPER_SUB:
            return emit_expression(state, expr->left) &&
                   emit_line(state->out, "    push rax") &&
                   emit_expression(state, expr->right) &&
                   emit_line(state->out, "    pop rbx") &&
                   emit_line(state->out, "    sub rbx, rax") &&
                   emit_line(state->out, "    mov rax, rbx");

        case OPER_MUL:
            return emit_expression(state, expr->left) &&
                   emit_line(state->out, "    push rax") &&
                   emit_expression(state, expr->right) &&
                   emit_line(state->out, "    pop rbx") &&
                   emit_line(state->out, "    imul rax, rbx");

        case OPER_DIV:
            return emit_expression(state, expr->left) &&
                   emit_line(state->out, "    push rax") &&
                   emit_expression(state, expr->right) &&
                   emit_line(state->out, "    mov rcx, rax") &&
                   emit_line(state->out, "    pop rax") &&
                   emit_line(state->out, "    cqo") &&
                   emit_line(state->out, "    idiv rcx");

        case OPER_NEG:
            return emit_expression(state, expr->right) &&
                   emit_line(state->out, "    neg rax");

        case OPER_POS:
            return emit_expression(state, expr->right);

        default:
            codegen_fail(state,
                         "operator %d is not supported in the second backend commit",
                         (int)expr->data.oper);
            return false;
    }
}

static bool emit_statement(codegen_state* state, const node_t* stmt)
{
    assert(state != NULL);

    if (stmt == NULL)
        return true;

    if (stmt->type == NODE_OPER && stmt->data.oper == OPER_STMT_SEP)
    {
        return emit_statement(state, stmt->left) &&
               emit_statement(state, stmt->right);
    }

    if (stmt->type == NODE_OPER && stmt->data.oper == OPER_RETURN)
    {
        if (stmt->right == NULL)
            return emit_line(state->out, "    mov rax, 0") &&
                   emit_function_epilogue(state);

        return emit_expression(state, stmt->right) &&
               emit_function_epilogue(state);
    }

    return emit_expression(state, stmt);
}

static bool emit_function_prologue(codegen_state* state, const function_symbol* function)
{
    assert(state    != NULL);
    assert(function != NULL);

    if (!emit_line(state->out, "    push rbp") ||
        !emit_line(state->out, "    mov rbp, rsp"))
    {
        return false;
    }

    if (function->local_count > 0)
        return emit_line(state->out, "    sub rsp, %d", function->local_count * 8);

    return true;
}

static bool emit_function_definition(codegen_state* state, const function_symbol* function)
{
    assert(state    != NULL);
    assert(function != NULL);

    const int function_index = get_function_index(state->symbols, function);
    if (function_index < 0)
    {
        codegen_fail(state,
                     "internal error: failed to determine function index for '%s'",
                     function->name);
        return false;
    }

    char function_label[64] = "";
    snprintf(function_label, sizeof(function_label), "WL_FUNC_%d", function_index);

    char comment[128] = "";
    snprintf(comment, sizeof(comment), "function %s", function->name);

    state->current_function = function;

    const bool ok = emit_comment(state->out, comment) &&
                    emit_label(state->out, function_label) &&
                    emit_function_prologue(state, function) &&
                    emit_statement(state, function->body_root) &&
                    emit_line(state->out, "    mov rax, 0") &&
                    emit_function_epilogue(state) &&
                    emit_blank_line(state->out);

    state->current_function = NULL;
    return ok;
}

static bool emit_file_header(codegen_state* state, const program_symbols* symbols)
{
    assert(state   != NULL);
    assert(symbols != NULL);

    char entry_label[64] = "";
    snprintf(entry_label, sizeof(entry_label), "WL_FUNC_%d", symbols->entry_index);

    return emit_comment(state->out, "Witcher Language backend, second commit") &&
           emit_line(state->out, "BITS 64") &&
           emit_line(state->out, "DEFAULT REL") &&
           emit_blank_line(state->out) &&
           emit_line(state->out, "global _start") &&
           emit_line(state->out, "section .text") &&
           emit_blank_line(state->out) &&
           emit_label(state->out, "_start") &&
           emit_comment(state->out, "call the first language function and exit with its result") &&
           emit_line(state->out, "    call %s", entry_label) &&
           emit_line(state->out, "    mov rdi, rax") &&
           emit_line(state->out, "    mov rax, 60") &&
           emit_line(state->out, "    syscall") &&
           emit_blank_line(state->out);
}

void exec_result_ctor(exec_result* result)
{
    if (result == NULL)
        return;

    result->error_text[0] = '\0';
}

bool exec_generate_program(const node_t* program_root,
                           const program_symbols* symbols,
                           FILE* out,
                           exec_result* result)
{
    assert(symbols != NULL);
    assert(out     != NULL);
    assert(result  != NULL);

    exec_result_ctor(result);

    if (program_root == NULL)
    {
        snprintf(result->error_text, EXEC_ERROR_TEXT_SIZE, "program tree is empty");
        return false;
    }

    if (symbols->function_count <= 0)
    {
        snprintf(result->error_text,
                 EXEC_ERROR_TEXT_SIZE,
                 "program does not contain any functions");
        return false;
    }

    if (symbols->entry_index < 0 || symbols->entry_index >= symbols->function_count)
    {
        snprintf(result->error_text,
                 EXEC_ERROR_TEXT_SIZE,
                 "invalid entry function index %d",
                 symbols->entry_index);
        return false;
    }

    codegen_state state = {};
    state.symbols = symbols;
    state.current_function = NULL;
    state.out = out;
    state.result = result;

    if (!emit_file_header(&state, symbols))
    {
        if (result->error_text[0] == '\0')
            snprintf(result->error_text, EXEC_ERROR_TEXT_SIZE, "failed to write asm header");
        return false;
    }

    for (int function_index = 0; function_index < symbols->function_count; ++function_index)
    {
        if (!emit_function_definition(&state, &symbols->functions[function_index]))
        {
            if (result->error_text[0] == '\0')
            {
                snprintf(result->error_text,
                         EXEC_ERROR_TEXT_SIZE,
                         "failed to generate function '%s'",
                         symbols->functions[function_index].name);
            }
            return false;
        }
    }

    return true;
}
