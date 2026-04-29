#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exec_codegen.h"
#include "exec_emit.h"

const int LABEL_TEXT_SIZE = 64;
const int LOOP_STACK_CAPACITY = 64;
const int TEXT_BUFFER_SIZE = 256;

struct text_constant
{
    const char* text;
    int label_id;
    int length;
};

struct codegen_state
{
    const program_symbols* symbols;
    const function_symbol* current_function;
    FILE* out;
    exec_result* result;

    int next_label_id;
    int break_label_stack[LOOP_STACK_CAPACITY];
    int break_label_count;

    text_constant* text_constants;
    int text_constant_count;
    int text_constant_capacity;
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

static void get_function_label(const program_symbols* symbols,
                               const function_symbol* function,
                               char* label,
                               size_t label_size)
{
    assert(symbols    != NULL);
    assert(function   != NULL);
    assert(label      != NULL);
    assert(label_size > 0);

    const int function_index = get_function_index(symbols, function);
    if (function_index < 0)
    {
        snprintf(label, label_size, "WL_FUNC_INVALID");
        return;
    }

    snprintf(label, label_size, "WL_FUNC_%d", function_index);
}

static int reserve_label_id(codegen_state* state)
{
    assert(state != NULL);

    const int label_id = state->next_label_id;
    state->next_label_id += 1;
    return label_id;
}

static void format_label(char* label, size_t label_size, int label_id)
{
    assert(label != NULL);
    assert(label_size > 0);

    snprintf(label, label_size, "WL_LABEL_%d", label_id);
}

static bool emit_generated_label(FILE* out, int label_id)
{
    assert(out != NULL);

    char label[LABEL_TEXT_SIZE] = "";
    format_label(label, sizeof(label), label_id);
    return emit_label(out, label);
}

static bool emit_jump_to_label(FILE* out, const char* jump_name, int label_id)
{
    assert(out != NULL);
    assert(jump_name != NULL);

    char label[LABEL_TEXT_SIZE] = "";
    format_label(label, sizeof(label), label_id);
    return emit_line(out, "    %s %s", jump_name, label);
}

static bool push_break_label(codegen_state* state, int label_id)
{
    assert(state != NULL);

    if (state->break_label_count >= LOOP_STACK_CAPACITY)
    {
        codegen_fail(state, "loop nesting is too deep");
        return false;
    }

    state->break_label_stack[state->break_label_count] = label_id;
    state->break_label_count += 1;
    return true;
}

static void pop_break_label(codegen_state* state)
{
    assert(state != NULL);
    assert(state->break_label_count > 0);

    state->break_label_count -= 1;
}

static bool has_break_label(const codegen_state* state)
{
    assert(state != NULL);
    return state->break_label_count > 0;
}

static int peek_break_label(const codegen_state* state)
{
    assert(state != NULL);
    assert(state->break_label_count > 0);

    return state->break_label_stack[state->break_label_count - 1];
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

static const function_symbol* find_called_function(codegen_state* state, const char* name)
{
    assert(state != NULL);
    assert(state->symbols != NULL);
    assert(name != NULL);

    const function_symbol* function = find_function_symbol(state->symbols, name);
    if (function != NULL)
        return function;

    codegen_fail(state, "function '%s' was not found", name);
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

static bool grow_text_constants(codegen_state* state)
{
    assert(state != NULL);

    if (state->text_constant_count < state->text_constant_capacity)
        return true;

    const int old_capacity = state->text_constant_capacity;
    const int new_capacity = (old_capacity > 0) ? old_capacity * 2 : 8;

    text_constant* resized = (text_constant*)realloc(state->text_constants,
                                                     (size_t)new_capacity * sizeof(text_constant));
    if (resized == NULL)
    {
        codegen_fail(state, "out of memory while growing text constant table");
        return false;
    }

    state->text_constants = resized;
    memset(state->text_constants + old_capacity, 0, (size_t)(new_capacity - old_capacity) * sizeof(text_constant));
    state->text_constant_capacity = new_capacity;
    return true;
}

static bool add_text_constant(codegen_state* state, const char* text, int* label_id)
{
    assert(state != NULL);
    assert(text != NULL);
    assert(label_id != NULL);

    for (int index = 0; index < state->text_constant_count; ++index)
    {
        if (strcmp(state->text_constants[index].text, text) == 0)
        {
            *label_id = state->text_constants[index].label_id;
            return true;
        }
    }

    if (!grow_text_constants(state))
        return false;

    const int index = state->text_constant_count;
    state->text_constants[index].text = text;
    state->text_constants[index].label_id = reserve_label_id(state);
    state->text_constants[index].length = (int)strlen(text);

    *label_id = state->text_constants[index].label_id;
    state->text_constant_count += 1;
    return true;
}

static void get_text_label(char* label, size_t label_size, int label_id)
{
    assert(label != NULL);
    assert(label_size > 0);

    snprintf(label, label_size, "WL_TEXT_%d", label_id);
}

static int count_call_arguments(const node_t* argument_chain)
{
    if (argument_chain == NULL)
        return 0;

    if (argument_chain->type == NODE_GLUE)
        return count_call_arguments(argument_chain->left) + count_call_arguments(argument_chain->right);

    return 1;
}

static bool emit_expression(codegen_state* state, const node_t* expr);
static bool emit_statement(codegen_state* state, const node_t* stmt);

static bool emit_call_arguments_reverse(codegen_state* state, const node_t* argument_chain)
{
    assert(state != NULL);

    if (argument_chain == NULL)
        return true;

    if (argument_chain->type == NODE_GLUE)
    {
        return emit_call_arguments_reverse(state, argument_chain->right) &&
               emit_call_arguments_reverse(state, argument_chain->left);
    }

    return emit_expression(state, argument_chain) &&
           emit_line(state->out, "    push rax");
}

static bool emit_call_expression(codegen_state* state, const node_t* expr)
{
    assert(state != NULL);
    assert(expr  != NULL);

    if (expr->data.string == NULL)
    {
        codegen_fail(state, "function call node has null name");
        return false;
    }

    const function_symbol* called_function = find_called_function(state, expr->data.string);
    if (called_function == NULL)
        return false;

    const int argument_count = count_call_arguments(expr->right);
    if (argument_count != called_function->param_count)
    {
        codegen_fail(state,
                     "function '%s' expects %d argument(s), but %d provided",
                     called_function->name,
                     called_function->param_count,
                     argument_count);
        return false;
    }

    char function_label[LABEL_TEXT_SIZE] = "";
    get_function_label(state->symbols, called_function, function_label, sizeof(function_label));

    return emit_call_arguments_reverse(state, expr->right) &&
           emit_line(state->out, "    call %s", function_label) &&
           (argument_count == 0 || emit_line(state->out, "    add rsp, %d", argument_count * 8));
}

static bool emit_comparison(codegen_state* state, const node_t* expr, const char* setcc_mnemonic)
{
    assert(state != NULL);
    assert(expr != NULL);
    assert(setcc_mnemonic != NULL);

    return emit_expression(state, expr->left) &&
           emit_line(state->out, "    push rax") &&
           emit_expression(state, expr->right) &&
           emit_line(state->out, "    pop r10") &&
           emit_line(state->out, "    cmp r10, rax") &&
           emit_line(state->out, "    mov eax, 0") &&
           emit_line(state->out, "    %s al", setcc_mnemonic);
}

static bool emit_condition_jump_false(codegen_state* state, const node_t* condition, int false_label_id)
{
    assert(state != NULL);

    return emit_expression(state, condition) &&
           emit_line(state->out, "    test rax, rax") &&
           emit_jump_to_label(state->out, "jz", false_label_id);
}

static bool emit_if_statement(codegen_state* state, const node_t* stmt)
{
    assert(state != NULL);
    assert(stmt != NULL);

    const node_t* condition = stmt->left;
    const node_t* right_branch = stmt->right;

    if (condition == NULL)
    {
        codegen_fail(state, "if statement has null condition");
        return false;
    }

    if (right_branch == NULL)
    {
        codegen_fail(state, "if statement has null body");
        return false;
    }

    const bool has_else = right_branch->type == NODE_OPER && right_branch->data.oper == OPER_ELSE;

    if (!has_else)
    {
        const int end_label_id = reserve_label_id(state);
        return emit_condition_jump_false(state, condition, end_label_id) &&
               emit_statement(state, right_branch) &&
               emit_generated_label(state->out, end_label_id);
    }

    const node_t* then_branch = right_branch->left;
    const node_t* else_branch = right_branch->right;

    if (then_branch == NULL || else_branch == NULL)
    {
        codegen_fail(state, "else statement has null branch");
        return false;
    }

    const int else_label_id = reserve_label_id(state);
    const int end_label_id = reserve_label_id(state);

    return emit_condition_jump_false(state, condition, else_label_id) &&
           emit_statement(state, then_branch) &&
           emit_jump_to_label(state->out, "jmp", end_label_id) &&
           emit_generated_label(state->out, else_label_id) &&
           emit_statement(state, else_branch) &&
           emit_generated_label(state->out, end_label_id);
}

static bool emit_while_statement(codegen_state* state, const node_t* stmt)
{
    assert(state != NULL);
    assert(stmt != NULL);

    const node_t* condition = stmt->left;
    const node_t* body = stmt->right;

    if (condition == NULL)
    {
        codegen_fail(state, "while statement has null condition");
        return false;
    }

    if (body == NULL)
    {
        codegen_fail(state, "while statement has null body");
        return false;
    }

    const int begin_label_id = reserve_label_id(state);
    const int end_label_id = reserve_label_id(state);

    if (!push_break_label(state, end_label_id))
        return false;

    const bool ok = emit_generated_label(state->out, begin_label_id) &&
                    emit_condition_jump_false(state, condition, end_label_id) &&
                    emit_statement(state, body) &&
                    emit_jump_to_label(state->out, "jmp", begin_label_id) &&
                    emit_generated_label(state->out, end_label_id);

    pop_break_label(state);
    return ok;
}

static bool emit_break_statement(codegen_state* state)
{
    assert(state != NULL);

    if (!has_break_label(state))
    {
        codegen_fail(state, "break statement is not inside a loop");
        return false;
    }

    return emit_jump_to_label(state->out, "jmp", peek_break_label(state));
}

static bool emit_print_statement(codegen_state* state, const node_t* stmt)
{
    assert(state != NULL);
    assert(stmt != NULL);

    if (stmt->right == NULL)
    {
        codegen_fail(state, "print statement has null argument");
        return false;
    }

    return emit_expression(state, stmt->right) &&
           emit_line(state->out, "    mov rdi, rax") &&
           emit_line(state->out, "    call WL_RT_PRINT_NUM");
}

static bool emit_printc_item(codegen_state* state, const node_t* item)
{
    assert(state != NULL);
    assert(item != NULL);

    if (item->type == NODE_TEXT)
    {
        if (item->data.string == NULL)
        {
            codegen_fail(state, "printc text item has null data");
            return false;
        }

        int label_id = -1;
        if (!add_text_constant(state, item->data.string, &label_id))
            return false;

        char label[LABEL_TEXT_SIZE] = "";
        get_text_label(label, sizeof(label), label_id);

        return emit_line(state->out, "    lea rdi, [rel %s]", label) &&
               emit_line(state->out, "    mov rsi, %d", (int)strlen(item->data.string)) &&
               emit_line(state->out, "    call WL_RT_PRINT_TEXT");
    }

    if (item->type == NODE_NUM)
    {
        return emit_line(state->out, "    mov rdi, %lld", (long long)item->data.number) &&
               emit_line(state->out, "    call WL_RT_PRINT_CHAR");
    }

    codegen_fail(state, "printc item has unsupported node type %d", (int)item->type);
    return false;
}

static bool emit_printc_statement(codegen_state* state, const node_t* stmt)
{
    assert(state != NULL);
    assert(stmt != NULL);

    const node_t* current = stmt->right;
    while (current != NULL)
    {
        if (!emit_printc_item(state, current))
            return false;
        current = current->right;
    }

    return true;
}

static bool emit_scan_statement(codegen_state* state, const node_t* stmt)
{
    assert(state != NULL);
    assert(stmt != NULL);

    const node_t* target = stmt->right;
    if (target == NULL || target->type != NODE_VAR || target->data.string == NULL)
    {
        codegen_fail(state, "scan target must be a variable");
        return false;
    }

    const variable_slot* slot = find_variable_slot(state, target->data.string);
    if (slot == NULL)
        return false;

    return emit_line(state->out, "    call WL_RT_READ_NUM") &&
           emit_store_slot(state, slot);
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

    if (expr->type == NODE_FUNC)
        return emit_call_expression(state, expr);

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
                   emit_line(state->out, "    pop r10") &&
                   emit_line(state->out, "    add rax, r10");

        case OPER_SUB:
            return emit_expression(state, expr->left) &&
                   emit_line(state->out, "    push rax") &&
                   emit_expression(state, expr->right) &&
                   emit_line(state->out, "    pop r10") &&
                   emit_line(state->out, "    sub r10, rax") &&
                   emit_line(state->out, "    mov rax, r10");

        case OPER_MUL:
            return emit_expression(state, expr->left) &&
                   emit_line(state->out, "    push rax") &&
                   emit_expression(state, expr->right) &&
                   emit_line(state->out, "    pop r10") &&
                   emit_line(state->out, "    imul rax, r10");

        case OPER_DIV:
            return emit_expression(state, expr->left) &&
                   emit_line(state->out, "    push rax") &&
                   emit_expression(state, expr->right) &&
                   emit_line(state->out, "    mov rcx, rax") &&
                   emit_line(state->out, "    pop rax") &&
                   emit_line(state->out, "    cqo") &&
                   emit_line(state->out, "    idiv rcx");

        case OPER_EQ:
            return emit_comparison(state, expr, "sete");

        case OPER_NEQ:
            return emit_comparison(state, expr, "setne");

        case OPER_LT:
            return emit_comparison(state, expr, "setl");

        case OPER_GT:
            return emit_comparison(state, expr, "setg");

        case OPER_LE:
            return emit_comparison(state, expr, "setle");

        case OPER_GE:
            return emit_comparison(state, expr, "setge");

        case OPER_NEG:
            return emit_expression(state, expr->right) &&
                   emit_line(state->out, "    neg rax");

        case OPER_POS:
            return emit_expression(state, expr->right);

        default:
            codegen_fail(state,
                         "operator %d is not supported in the fifth backend commit",
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

    if (stmt->type == NODE_OPER && stmt->data.oper == OPER_IF)
        return emit_if_statement(state, stmt);

    if (stmt->type == NODE_OPER && stmt->data.oper == OPER_WHILE)
        return emit_while_statement(state, stmt);

    if (stmt->type == NODE_OPER && stmt->data.oper == OPER_BREAK)
        return emit_break_statement(state);

    if (stmt->type == NODE_OPER && stmt->data.oper == OPER_PRINT)
        return emit_print_statement(state, stmt);

    if (stmt->type == NODE_OPER && stmt->data.oper == OPER_PRINTC)
        return emit_printc_statement(state, stmt);

    if (stmt->type == NODE_OPER && stmt->data.oper == OPER_SCAN)
        return emit_scan_statement(state, stmt);

    if (stmt->type == NODE_OPER && stmt->data.oper == OPER_ELSE)
    {
        codegen_fail(state, "else node appeared outside of if statement");
        return false;
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

    char function_label[LABEL_TEXT_SIZE] = "";
    get_function_label(state->symbols, function, function_label, sizeof(function_label));

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

static bool emit_runtime_print_text(FILE* out)
{
    assert(out != NULL);

    return emit_comment(out, "write text: rdi = address, rsi = length") &&
           emit_label(out, "WL_RT_PRINT_TEXT") &&
           emit_line(out, "    mov rdx, rsi") &&
           emit_line(out, "    mov rsi, rdi") &&
           emit_line(out, "    mov rdi, 1") &&
           emit_line(out, "    mov rax, 1") &&
           emit_line(out, "    syscall") &&
           emit_line(out, "    ret") &&
           emit_blank_line(out);
}

static bool emit_runtime_print_char(FILE* out)
{
    assert(out != NULL);

    return emit_comment(out, "write one character from dil") &&
           emit_label(out, "WL_RT_PRINT_CHAR") &&
           emit_line(out, "    push rbp") &&
           emit_line(out, "    mov rbp, rsp") &&
           emit_line(out, "    sub rsp, 16") &&
           emit_line(out, "    mov byte [rbp - 1], dil") &&
           emit_line(out, "    lea rsi, [rbp - 1]") &&
           emit_line(out, "    mov rdi, 1") &&
           emit_line(out, "    mov rdx, 1") &&
           emit_line(out, "    mov rax, 1") &&
           emit_line(out, "    syscall") &&
           emit_line(out, "    mov rsp, rbp") &&
           emit_line(out, "    pop rbp") &&
           emit_line(out, "    ret") &&
           emit_blank_line(out);
}

static bool emit_runtime_print_num(FILE* out)
{
    assert(out != NULL);

    return emit_comment(out, "write signed decimal number from rdi and append newline") &&
           emit_label(out, "WL_RT_PRINT_NUM") &&
           emit_line(out, "    push rbp") &&
           emit_line(out, "    mov rbp, rsp") &&
           emit_line(out, "    sub rsp, 64") &&
           emit_line(out, "    mov r11, rdi") &&
           emit_line(out, "    lea r8, [rbp - 1]") &&
           emit_line(out, "    mov byte [r8], 10") &&
           emit_line(out, "    mov r9, 1") &&
           emit_line(out, "    xor r10d, r10d") &&
           emit_line(out, "    test r11, r11") &&
           emit_line(out, "    jns .print_num_sign_ready") &&
           emit_line(out, "    mov r10d, 1") &&
           emit_label(out, ".print_num_sign_ready") &&
           emit_line(out, "    cmp r11, 0") &&
           emit_line(out, "    jne .print_num_loop") &&
           emit_line(out, "    dec r8") &&
           emit_line(out, "    mov byte [r8], '0'") &&
           emit_line(out, "    inc r9") &&
           emit_line(out, "    jmp .print_num_after_digits") &&
           emit_label(out, ".print_num_loop") &&
           emit_line(out, "    mov rax, r11") &&
           emit_line(out, "    cqo") &&
           emit_line(out, "    mov rcx, 10") &&
           emit_line(out, "    idiv rcx") &&
           emit_line(out, "    mov r11, rax") &&
           emit_line(out, "    test rdx, rdx") &&
           emit_line(out, "    jge .print_num_digit_ready") &&
           emit_line(out, "    neg rdx") &&
           emit_label(out, ".print_num_digit_ready") &&
           emit_line(out, "    add dl, '0'") &&
           emit_line(out, "    dec r8") &&
           emit_line(out, "    mov byte [r8], dl") &&
           emit_line(out, "    inc r9") &&
           emit_line(out, "    test r11, r11") &&
           emit_line(out, "    jne .print_num_loop") &&
           emit_label(out, ".print_num_after_digits") &&
           emit_line(out, "    test r10d, r10d") &&
           emit_line(out, "    jz .print_num_write") &&
           emit_line(out, "    dec r8") &&
           emit_line(out, "    mov byte [r8], '-'") &&
           emit_line(out, "    inc r9") &&
           emit_label(out, ".print_num_write") &&
           emit_line(out, "    mov rax, 1") &&
           emit_line(out, "    mov rdi, 1") &&
           emit_line(out, "    mov rsi, r8") &&
           emit_line(out, "    mov rdx, r9") &&
           emit_line(out, "    syscall") &&
           emit_line(out, "    mov rsp, rbp") &&
           emit_line(out, "    pop rbp") &&
           emit_line(out, "    ret") &&
           emit_blank_line(out);
}

static bool emit_runtime_read_num(FILE* out)
{
    assert(out != NULL);

    return emit_comment(out, "read signed decimal number from stdin into rax") &&
           emit_label(out, "WL_RT_READ_NUM") &&
           emit_line(out, "    push rbp") &&
           emit_line(out, "    mov rbp, rsp") &&
           emit_line(out, "    sub rsp, 16") &&
           emit_line(out, "    xor r8, r8") &&
           emit_line(out, "    mov r9, 1") &&
           emit_line(out, "    xor r10d, r10d") &&
           emit_label(out, ".read_num_skip_space") &&
           emit_line(out, "    mov rax, 0") &&
           emit_line(out, "    mov rdi, 0") &&
           emit_line(out, "    lea rsi, [rbp - 1]") &&
           emit_line(out, "    mov rdx, 1") &&
           emit_line(out, "    syscall") &&
           emit_line(out, "    cmp rax, 1") &&
           emit_line(out, "    jne .read_num_finish") &&
           emit_line(out, "    movzx eax, byte [rbp - 1]") &&
           emit_line(out, "    cmp al, ' '") &&
           emit_line(out, "    je .read_num_skip_space") &&
           emit_line(out, "    cmp al, 10") &&
           emit_line(out, "    je .read_num_skip_space") &&
           emit_line(out, "    cmp al, 9") &&
           emit_line(out, "    je .read_num_skip_space") &&
           emit_line(out, "    cmp al, 13") &&
           emit_line(out, "    je .read_num_skip_space") &&
           emit_line(out, "    cmp al, '-'") &&
           emit_line(out, "    jne .read_num_check_plus") &&
           emit_line(out, "    mov r9, -1") &&
           emit_line(out, "    jmp .read_num_loop") &&
           emit_label(out, ".read_num_check_plus") &&
           emit_line(out, "    cmp al, '+'") &&
           emit_line(out, "    jne .read_num_first_digit") &&
           emit_line(out, "    jmp .read_num_loop") &&
           emit_label(out, ".read_num_first_digit") &&
           emit_line(out, "    cmp al, '0'") &&
           emit_line(out, "    jb .read_num_finish") &&
           emit_line(out, "    cmp al, '9'") &&
           emit_line(out, "    ja .read_num_finish") &&
           emit_line(out, "    sub al, '0'") &&
           emit_line(out, "    movzx rcx, al") &&
           emit_line(out, "    mov r8, rcx") &&
           emit_line(out, "    mov r10d, 1") &&
           emit_label(out, ".read_num_loop") &&
           emit_line(out, "    mov rax, 0") &&
           emit_line(out, "    mov rdi, 0") &&
           emit_line(out, "    lea rsi, [rbp - 1]") &&
           emit_line(out, "    mov rdx, 1") &&
           emit_line(out, "    syscall") &&
           emit_line(out, "    cmp rax, 1") &&
           emit_line(out, "    jne .read_num_finish") &&
           emit_line(out, "    movzx eax, byte [rbp - 1]") &&
           emit_line(out, "    cmp al, '0'") &&
           emit_line(out, "    jb .read_num_finish") &&
           emit_line(out, "    cmp al, '9'") &&
           emit_line(out, "    ja .read_num_finish") &&
           emit_line(out, "    imul r8, r8, 10") &&
           emit_line(out, "    sub al, '0'") &&
           emit_line(out, "    movzx rcx, al") &&
           emit_line(out, "    add r8, rcx") &&
           emit_line(out, "    mov r10d, 1") &&
           emit_line(out, "    jmp .read_num_loop") &&
           emit_label(out, ".read_num_finish") &&
           emit_line(out, "    mov rax, r8") &&
           emit_line(out, "    cmp r9, 1") &&
           emit_line(out, "    je .read_num_done") &&
           emit_line(out, "    neg rax") &&
           emit_label(out, ".read_num_done") &&
           emit_line(out, "    mov rsp, rbp") &&
           emit_line(out, "    pop rbp") &&
           emit_line(out, "    ret") &&
           emit_blank_line(out);
}

static bool emit_runtime_section(codegen_state* state)
{
    assert(state != NULL);
    assert(state->out != NULL);

    return emit_comment(state->out, "runtime helpers") &&
           emit_runtime_print_text(state->out) &&
           emit_runtime_print_char(state->out) &&
           emit_runtime_print_num(state->out) &&
           emit_runtime_read_num(state->out);
}

static bool emit_text_bytes(FILE* out, const char* text)
{
    assert(out != NULL);
    assert(text != NULL);

    if (fprintf(out, "    db ") < 0)
        return false;

    const unsigned char* ptr = (const unsigned char*)text;
    bool first = true;
    while (*ptr != '\0')
    {
        if (!first)
        {
            if (fprintf(out, ", ") < 0)
                return false;
        }

        if (fprintf(out, "%u", (unsigned int)*ptr) < 0)
            return false;

        first = false;
        ++ptr;
    }

    if (first)
    {
        if (fprintf(out, "0") < 0)
            return false;
    }

    return fprintf(out, "\n") >= 0;
}

static bool emit_data_section(codegen_state* state)
{
    assert(state != NULL);
    assert(state->out != NULL);

    if (state->text_constant_count <= 0)
        return true;

    if (!emit_line(state->out, "section .data") || !emit_blank_line(state->out))
        return false;

    for (int index = 0; index < state->text_constant_count; ++index)
    {
        char label[LABEL_TEXT_SIZE] = "";
        get_text_label(label, sizeof(label), state->text_constants[index].label_id);

        if (!emit_comment(state->out, state->text_constants[index].text) ||
            !emit_label(state->out, label) ||
            !emit_text_bytes(state->out, state->text_constants[index].text) ||
            !emit_blank_line(state->out))
        {
            return false;
        }
    }

    return true;
}

static bool emit_file_header(codegen_state* state, const program_symbols* symbols)
{
    assert(state   != NULL);
    assert(symbols != NULL);

    const function_symbol* entry_function = &symbols->functions[symbols->entry_index];

    char entry_label[LABEL_TEXT_SIZE] = "";
    get_function_label(symbols, entry_function, entry_label, sizeof(entry_label));

    return emit_comment(state->out, "Witcher Language backend, fifth commit") &&
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
    state.next_label_id = 0;
    state.break_label_count = 0;
    state.text_constants = NULL;
    state.text_constant_count = 0;
    state.text_constant_capacity = 0;

    bool ok = emit_file_header(&state, symbols);

    if (ok)
    {
        for (int index = 0; index < symbols->function_count; ++index)
        {
            if (!emit_function_definition(&state, &symbols->functions[index]))
            {
                if (result->error_text[0] == '\0')
                {
                    snprintf(result->error_text,
                             EXEC_ERROR_TEXT_SIZE,
                             "failed to emit function '%s'",
                             symbols->functions[index].name);
                }
                ok = false;
                break;
            }
        }
    }

    if (ok && !emit_runtime_section(&state))
    {
        if (result->error_text[0] == '\0')
            snprintf(result->error_text, EXEC_ERROR_TEXT_SIZE, "failed to emit runtime helpers");
        ok = false;
    }

    if (ok && !emit_data_section(&state))
    {
        if (result->error_text[0] == '\0')
            snprintf(result->error_text, EXEC_ERROR_TEXT_SIZE, "failed to emit data section");
        ok = false;
    }

    free(state.text_constants);
    return ok && result->error_text[0] == '\0';
}
