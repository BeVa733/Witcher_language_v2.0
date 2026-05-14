#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exec_codegen.h"
#include "exec_emit.h"
#include "elf_write.h"
#include "runtime_code.h"

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
    emit_context* emit;
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
    assert(state);
    assert(state->result);
    assert(format);

    if (state->result->error_text[0] != '\0')
        return;

    va_list args = {};
    va_start(args, format);
    vsnprintf(state->result->error_text, EXEC_ERROR_TEXT_SIZE, format, args);
    va_end(args);
}

static int get_function_index(const program_symbols* symbols, const function_symbol* function)
{
    assert(symbols);
    assert(function);

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
    assert(symbols);
    assert(function);
    assert(label);
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
    assert(state);

    const int label_id = state->next_label_id;
    state->next_label_id += 1;
    return label_id;
}

static void format_label(char* label, size_t label_size, int label_id)
{
    assert(label);
    assert(label_size > 0);

    snprintf(label, label_size, "WL_LABEL_%d", label_id);
}

static bool emit_generated_label(codegen_state* state, int label_id)
{
    assert(state);

    char label[LABEL_TEXT_SIZE] = "";
    format_label(label, sizeof(label), label_id);
    return emit_label(state->emit, label);
}

static bool emit_jump_to_label(codegen_state* state, int label_id)
{
    assert(state);

    char label[LABEL_TEXT_SIZE] = "";
    format_label(label, sizeof(label), label_id);
    return emit_jmp_label(state->emit, label);
}

static bool emit_cond_jump_to_label(codegen_state* state, cond_code condition, int label_id)
{
    assert(state);

    char label[LABEL_TEXT_SIZE] = "";
    format_label(label, sizeof(label), label_id);
    return emit_jcc_label(state->emit, condition, label);
}

static bool push_break_label(codegen_state* state, int label_id)
{
    assert(state);

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
    assert(state);
    assert(state->break_label_count > 0);

    state->break_label_count -= 1;
}

static bool has_break_label(const codegen_state* state)
{
    assert(state);

    return state->break_label_count > 0;
}

static int peek_break_label(const codegen_state* state)
{
    assert(state);
    assert(state->break_label_count > 0);

    return state->break_label_stack[state->break_label_count - 1];
}

static int get_slot_rbp_disp(const function_symbol* function, const variable_slot* slot)
{
    assert(function);
    assert(slot);

    if (slot->is_parameter)
        return (slot->offset + 1) * 8;

    return -(slot->offset - function->param_count) * 8;
}

static const variable_slot* find_variable_slot(codegen_state* state, const char* name)
{
    assert(state);
    assert(state->current_function);
    assert(name);

    const variable_slot* slot = find_any_slot(state->current_function, name);
    if (slot)
        return slot;

    codegen_fail(state,
                 "variable '%s' was not found in function '%s'",
                 name,
                 state->current_function->name);
    return NULL;
}

static const function_symbol* find_called_function(codegen_state* state, const char* name)
{
    assert(state);
    assert(state->symbols);
    assert(name);

    const function_symbol* function = find_function_symbol(state->symbols, name);
    if (function)
        return function;

    codegen_fail(state, "function '%s' was not found", name);
    return NULL;
}

static bool emit_load_slot(codegen_state* state, const variable_slot* slot)
{
    assert(state);
    assert(slot);

    return emit_mov_reg_rbp_rel(state->emit,
                                REG_RAX,
                                get_slot_rbp_disp(state->current_function, slot));
}

static bool emit_store_slot(codegen_state* state, const variable_slot* slot)
{
    assert(state);
    assert(slot);

    return emit_mov_rbp_rel_reg(state->emit,
                                get_slot_rbp_disp(state->current_function, slot),
                                REG_RAX);
}

static bool emit_function_epilogue(codegen_state* state)
{
    assert(state);

    return emit_mov_reg_reg(state->emit, REG_RSP, REG_RBP) &&
           emit_pop_reg(state->emit, REG_RBP) &&
           emit_ret(state->emit);
}

static bool grow_text_constants(codegen_state* state)
{
    assert(state);

    if (state->text_constant_count < state->text_constant_capacity)
        return true;

    const int old_capacity = state->text_constant_capacity;
    const int new_capacity = (old_capacity > 0) ? old_capacity * 2 : 8;

    text_constant* resized = (text_constant*)realloc(state->text_constants,
                                                     (size_t)new_capacity * sizeof(text_constant));
    if (!resized)
    {
        codegen_fail(state, "out of memory while growing text constant table");
        return false;
    }

    state->text_constants = resized;
    memset(state->text_constants + old_capacity,
           0,
           (size_t)(new_capacity - old_capacity) * sizeof(text_constant));
    state->text_constant_capacity = new_capacity;

    return true;
}

static bool add_text_constant(codegen_state* state, const char* text, int* label_id)
{
    assert(state);
    assert(text);
    assert(label_id);

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
    assert(label);
    assert(label_size > 0);

    snprintf(label, label_size, "WL_TEXT_%d", label_id);
}

static int count_call_arguments(const node_t* argument_chain)
{
    if (!argument_chain)
        return 0;

    if (argument_chain->type == NODE_GLUE)
        return count_call_arguments(argument_chain->left) +
               count_call_arguments(argument_chain->right);

    return 1;
}

static bool emit_expression(codegen_state* state, const node_t* expr);
static bool emit_statement(codegen_state* state, const node_t* stmt);

static bool emit_call_arguments_reverse(codegen_state* state, const node_t* argument_chain)
{
    assert(state);

    if (!argument_chain)
        return true;

    if (argument_chain->type == NODE_GLUE)
    {
        return emit_call_arguments_reverse(state, argument_chain->right) &&
               emit_call_arguments_reverse(state, argument_chain->left);
    }

    return emit_expression(state, argument_chain) &&
           emit_push_reg(state->emit, REG_RAX);
}

static bool emit_call_expression(codegen_state* state, const node_t* expr)
{
    assert(state);
    assert(expr);

    if (!expr->data.string)
    {
        codegen_fail(state, "function call node has null name");
        return false;
    }

    const function_symbol* called_function = find_called_function(state, expr->data.string);
    if (!called_function)
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
           emit_call_label(state->emit, function_label) &&
           (argument_count == 0 ||
            emit_add_reg_imm(state->emit, REG_RSP, argument_count * 8));
}

static bool emit_comparison(codegen_state* state, const node_t* expr, cond_code condition)
{
    assert(state);
    assert(expr);

    return emit_expression(state, expr->left) &&
           emit_push_reg(state->emit, REG_RAX) &&
           emit_expression(state, expr->right) &&
           emit_pop_reg(state->emit, REG_R10) &&
           emit_cmp_reg_reg(state->emit, REG_R10, REG_RAX) &&
           emit_mov_reg_imm(state->emit, REG_RAX, 0) &&
           emit_setcc_reg(state->emit, condition, REG_RAX);
}

static bool emit_condition_jump_false(codegen_state* state,
                                      const node_t* condition,
                                      int false_label_id)
{
    assert(state);

    return emit_expression(state, condition) &&
           emit_test_reg_reg(state->emit, REG_RAX, REG_RAX) &&
           emit_cond_jump_to_label(state, COND_Z, false_label_id);
}

static bool emit_if_statement(codegen_state* state, const node_t* stmt)
{
    assert(state);
    assert(stmt);

    const node_t* condition = stmt->left;
    const node_t* right_branch = stmt->right;

    if (!condition)
    {
        codegen_fail(state, "if statement has null condition");
        return false;
    }

    if (!right_branch)
    {
        codegen_fail(state, "if statement has null body");
        return false;
    }

    const bool has_else = right_branch->type == NODE_OPER &&
                          right_branch->data.oper == OPER_ELSE;

    if (!has_else)
    {
        const int end_label_id = reserve_label_id(state);
        return emit_condition_jump_false(state, condition, end_label_id) &&
               emit_statement(state, right_branch) &&
               emit_generated_label(state, end_label_id);
    }

    const node_t* then_branch = right_branch->left;
    const node_t* else_branch = right_branch->right;

    if (!then_branch || !else_branch)
    {
        codegen_fail(state, "else statement has null branch");
        return false;
    }

    const int else_label_id = reserve_label_id(state);
    const int end_label_id = reserve_label_id(state);

    return emit_condition_jump_false(state, condition, else_label_id) &&
           emit_statement(state, then_branch) &&
           emit_jump_to_label(state, end_label_id) &&
           emit_generated_label(state, else_label_id) &&
           emit_statement(state, else_branch) &&
           emit_generated_label(state, end_label_id);
}

static bool emit_while_statement(codegen_state* state, const node_t* stmt)
{
    assert(state);
    assert(stmt);

    const node_t* condition = stmt->left;
    const node_t* body = stmt->right;

    if (!condition)
    {
        codegen_fail(state, "while statement has null condition");
        return false;
    }

    if (!body)
    {
        codegen_fail(state, "while statement has null body");
        return false;
    }

    const int begin_label_id = reserve_label_id(state);
    const int end_label_id = reserve_label_id(state);

    if (!push_break_label(state, end_label_id))
        return false;

    const bool ok = emit_generated_label(state, begin_label_id) &&
                    emit_condition_jump_false(state, condition, end_label_id) &&
                    emit_statement(state, body) &&
                    emit_jump_to_label(state, begin_label_id) &&
                    emit_generated_label(state, end_label_id);

    pop_break_label(state);
    return ok;
}

static bool emit_break_statement(codegen_state* state)
{
    assert(state);

    if (!has_break_label(state))
    {
        codegen_fail(state, "break statement is not inside a loop");
        return false;
    }

    return emit_jump_to_label(state, peek_break_label(state));
}

static bool emit_print_statement(codegen_state* state, const node_t* stmt)
{
    assert(state);
    assert(stmt);

    if (!stmt->right)
    {
        codegen_fail(state, "print statement has null argument");
        return false;
    }

    return emit_expression(state, stmt->right) &&
           emit_mov_reg_reg(state->emit, REG_RDI, REG_RAX) &&
           emit_call_label(state->emit, "WL_RT_PRINT_NUM");
}

static bool emit_printc_item(codegen_state* state, const node_t* item)
{
    assert(state);
    assert(item);

    if (item->type == NODE_TEXT)
    {
        if (!item->data.string)
        {
            codegen_fail(state, "printc text item has null data");
            return false;
        }

        int label_id = -1;
        if (!add_text_constant(state, item->data.string, &label_id))
            return false;

        char label[LABEL_TEXT_SIZE] = "";
        get_text_label(label, sizeof(label), label_id);

        return emit_lea_reg_label(state->emit, REG_RDI, label) &&
               emit_mov_reg_imm(state->emit, REG_RSI, (int)strlen(item->data.string)) &&
               emit_call_label(state->emit, "WL_RT_PRINT_TEXT");
    }

    if (item->type == NODE_NUM)
    {
        return emit_mov_reg_imm(state->emit, REG_RDI, (long long)item->data.number) &&
               emit_call_label(state->emit, "WL_RT_PRINT_CHAR");
    }

    codegen_fail(state, "printc item has unsupported node type %d", (int)item->type);
    return false;
}

static bool emit_printc_statement(codegen_state* state, const node_t* stmt)
{
    assert(state);
    assert(stmt);

    const node_t* current = stmt->right;
    while (current)
    {
        if (!emit_printc_item(state, current))
            return false;

        current = current->right;
    }

    return true;
}

static bool emit_scan_statement(codegen_state* state, const node_t* stmt)
{
    assert(state);
    assert(stmt);

    const node_t* target = stmt->right;
    if (!target || target->type != NODE_VAR || !target->data.string)
    {
        codegen_fail(state, "scan target must be a variable");
        return false;
    }

    const variable_slot* slot = find_variable_slot(state, target->data.string);
    if (!slot)
        return false;

    return emit_call_label(state->emit, "WL_RT_READ_NUM") &&
           emit_store_slot(state, slot);
}

static bool emit_expression(codegen_state* state, const node_t* expr)
{
    assert(state);

    if (!expr)
    {
        codegen_fail(state, "expression node is null");
        return false;
    }

    if (expr->type == NODE_NUM)
        return emit_mov_reg_imm(state->emit, REG_RAX, (long long)expr->data.number);

    if (expr->type == NODE_VAR)
    {
        if (!expr->data.string)
        {
            codegen_fail(state, "variable node has null name");
            return false;
        }

        const variable_slot* slot = find_variable_slot(state, expr->data.string);
        if (!slot)
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
            if (!lvalue || lvalue->type != NODE_VAR || !lvalue->data.string)
            {
                codegen_fail(state, "assignment left side must be a variable");
                return false;
            }

            const variable_slot* slot = find_variable_slot(state, lvalue->data.string);
            if (!slot)
                return false;

            if (!emit_expression(state, expr->right))
                return false;

            return emit_store_slot(state, slot);
        }

        case OPER_ADD:
            return emit_expression(state, expr->left) &&
                   emit_push_reg(state->emit, REG_RAX) &&
                   emit_expression(state, expr->right) &&
                   emit_pop_reg(state->emit, REG_R10) &&
                   emit_bin_reg_reg(state->emit, BIN_OP_ADD, REG_RAX, REG_R10);

        case OPER_SUB:
            return emit_expression(state, expr->left) &&
                   emit_push_reg(state->emit, REG_RAX) &&
                   emit_expression(state, expr->right) &&
                   emit_pop_reg(state->emit, REG_R10) &&
                   emit_bin_reg_reg(state->emit, BIN_OP_SUB, REG_R10, REG_RAX) &&
                   emit_mov_reg_reg(state->emit, REG_RAX, REG_R10);

        case OPER_MUL:
            return emit_expression(state, expr->left) &&
                   emit_push_reg(state->emit, REG_RAX) &&
                   emit_expression(state, expr->right) &&
                   emit_pop_reg(state->emit, REG_R10) &&
                   emit_bin_reg_reg(state->emit, BIN_OP_IMUL, REG_RAX, REG_R10);

        case OPER_DIV:
            return emit_expression(state, expr->left) &&
                   emit_push_reg(state->emit, REG_RAX) &&
                   emit_expression(state, expr->right) &&
                   emit_mov_reg_reg(state->emit, REG_RCX, REG_RAX) &&
                   emit_pop_reg(state->emit, REG_RAX) &&
                   emit_cqo(state->emit) &&
                   emit_idiv_reg(state->emit, REG_RCX);

        case OPER_EQ:
            return emit_comparison(state, expr, COND_E);

        case OPER_NEQ:
            return emit_comparison(state, expr, COND_NE);

        case OPER_LT:
            return emit_comparison(state, expr, COND_L);

        case OPER_GT:
            return emit_comparison(state, expr, COND_G);

        case OPER_LE:
            return emit_comparison(state, expr, COND_LE);

        case OPER_GE:
            return emit_comparison(state, expr, COND_GE);

        case OPER_NEG:
            return emit_expression(state, expr->right) &&
                   emit_neg_reg(state->emit, REG_RAX);

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
    assert(state);

    if (!stmt)
        return true;

    if (stmt->type == NODE_OPER && stmt->data.oper == OPER_STMT_SEP)
        return emit_statement(state, stmt->left) &&
               emit_statement(state, stmt->right);

    if (stmt->type == NODE_OPER && stmt->data.oper == OPER_RETURN)
    {
        if (!stmt->right)
            return emit_mov_reg_imm(state->emit, REG_RAX, 0) &&
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
    assert(state);
    assert(function);

    if (!emit_push_reg(state->emit, REG_RBP) ||
        !emit_mov_reg_reg(state->emit, REG_RBP, REG_RSP))
        return false;

    if (function->local_count > 0)
        return emit_sub_reg_imm(state->emit, REG_RSP, function->local_count * 8);

    return true;
}

static bool emit_function_definition(codegen_state* state, const function_symbol* function)
{
    assert(state);
    assert(function);

    char function_label[LABEL_TEXT_SIZE] = "";
    get_function_label(state->symbols, function, function_label, sizeof(function_label));

    char comment[128] = "";
    snprintf(comment, sizeof(comment), "function %s", function->name);

    state->current_function = function;

    const bool ok = emit_comment(state->emit, comment) &&
                    emit_label(state->emit, function_label) &&
                    emit_function_prologue(state, function) &&
                    emit_statement(state, function->body_root) &&
                    emit_mov_reg_imm(state->emit, REG_RAX, 0) &&
                    emit_function_epilogue(state) &&
                    emit_blank_line(state->emit);

    state->current_function = NULL;
    return ok;
}

static bool emit_runtime_print_text(emit_context* emit)
{
    assert(emit);

    return emit_comment(emit, "write text: rdi = address, rsi = length") &&
           emit_label(emit, "WL_RT_PRINT_TEXT") &&
           emit_mov_reg_reg(emit, REG_RDX, REG_RSI) &&
           emit_mov_reg_reg(emit, REG_RSI, REG_RDI) &&
           emit_mov_reg_imm(emit, REG_RDI, 1) &&
           emit_mov_reg_imm(emit, REG_RAX, 1) &&
           emit_syscall(emit) &&
           emit_ret(emit) &&
           emit_blank_line(emit);
}

static bool emit_runtime_print_char(emit_context* emit)
{
    assert(emit);

    return emit_comment(emit, "write one character from dil") &&
           emit_label(emit, "WL_RT_PRINT_CHAR") &&
           emit_line(emit, " push rbp") &&
           emit_line(emit, " mov rbp, rsp") &&
           emit_line(emit, " sub rsp, 16") &&
           emit_line(emit, " mov byte [rbp - 1], dil") &&
           emit_line(emit, " lea rsi, [rbp - 1]") &&
           emit_line(emit, " mov rdi, 1") &&
           emit_line(emit, " mov rdx, 1") &&
           emit_line(emit, " mov rax, 1") &&
           emit_line(emit, " syscall") &&
           emit_line(emit, " mov rsp, rbp") &&
           emit_line(emit, " pop rbp") &&
           emit_line(emit, " ret") &&
           emit_blank_line(emit);
}

static bool emit_runtime_print_num(emit_context* emit)
{
    assert(emit);

    return emit_comment(emit, "write signed decimal number from rdi and append newline") &&
           emit_label(emit, "WL_RT_PRINT_NUM") &&
           emit_line(emit, " push rbp") &&
           emit_line(emit, " mov rbp, rsp") &&
           emit_line(emit, " sub rsp, 64") &&
           emit_line(emit, " mov r11, rdi") &&
           emit_line(emit, " lea r8, [rbp - 1]") &&
           emit_line(emit, " mov byte [r8], 10") &&
           emit_line(emit, " mov r9, 1") &&
           emit_line(emit, " xor r10d, r10d") &&
           emit_line(emit, " test r11, r11") &&
           emit_line(emit, " jns .print_num_sign_ready") &&
           emit_line(emit, " mov r10d, 1") &&
           emit_label(emit, ".print_num_sign_ready") &&
           emit_line(emit, " cmp r11, 0") &&
           emit_line(emit, " jne .print_num_loop") &&
           emit_line(emit, " dec r8") &&
           emit_line(emit, " mov byte [r8], '0'") &&
           emit_line(emit, " inc r9") &&
           emit_line(emit, " jmp .print_num_after_digits") &&
           emit_label(emit, ".print_num_loop") &&
           emit_line(emit, " mov rax, r11") &&
           emit_line(emit, " cqo") &&
           emit_line(emit, " mov rcx, 10") &&
           emit_line(emit, " idiv rcx") &&
           emit_line(emit, " mov r11, rax") &&
           emit_line(emit, " test rdx, rdx") &&
           emit_line(emit, " jge .print_num_digit_ready") &&
           emit_line(emit, " neg rdx") &&
           emit_label(emit, ".print_num_digit_ready") &&
           emit_line(emit, " add dl, '0'") &&
           emit_line(emit, " dec r8") &&
           emit_line(emit, " mov byte [r8], dl") &&
           emit_line(emit, " inc r9") &&
           emit_line(emit, " test r11, r11") &&
           emit_line(emit, " jne .print_num_loop") &&
           emit_label(emit, ".print_num_after_digits") &&
           emit_line(emit, " test r10d, r10d") &&
           emit_line(emit, " jz .print_num_write") &&
           emit_line(emit, " dec r8") &&
           emit_line(emit, " mov byte [r8], '-'") &&
           emit_line(emit, " inc r9") &&
           emit_label(emit, ".print_num_write") &&
           emit_line(emit, " mov rax, 1") &&
           emit_line(emit, " mov rdi, 1") &&
           emit_line(emit, " mov rsi, r8") &&
           emit_line(emit, " mov rdx, r9") &&
           emit_line(emit, " syscall") &&
           emit_line(emit, " mov rsp, rbp") &&
           emit_line(emit, " pop rbp") &&
           emit_line(emit, " ret") &&
           emit_blank_line(emit);
}

static bool emit_runtime_read_num(emit_context* emit)
{
    assert(emit);

    return emit_comment(emit, "read signed decimal number from stdin into rax") &&
           emit_label(emit, "WL_RT_READ_NUM") &&
           emit_line(emit, " push rbp") &&
           emit_line(emit, " mov rbp, rsp") &&
           emit_line(emit, " sub rsp, 16") &&
           emit_line(emit, " xor r8, r8") &&
           emit_line(emit, " mov r9, 1") &&
           emit_line(emit, " xor r10d, r10d") &&
           emit_label(emit, ".read_num_skip_space") &&
           emit_line(emit, " mov rax, 0") &&
           emit_line(emit, " mov rdi, 0") &&
           emit_line(emit, " lea rsi, [rbp - 1]") &&
           emit_line(emit, " mov rdx, 1") &&
           emit_line(emit, " syscall") &&
           emit_line(emit, " cmp rax, 1") &&
           emit_line(emit, " jne .read_num_finish") &&
           emit_line(emit, " movzx eax, byte [rbp - 1]") &&
           emit_line(emit, " cmp al, ' '") &&
           emit_line(emit, " je .read_num_skip_space") &&
           emit_line(emit, " cmp al, 10") &&
           emit_line(emit, " je .read_num_skip_space") &&
           emit_line(emit, " cmp al, 9") &&
           emit_line(emit, " je .read_num_skip_space") &&
           emit_line(emit, " cmp al, 13") &&
           emit_line(emit, " je .read_num_skip_space") &&
           emit_line(emit, " cmp al, '-'") &&
           emit_line(emit, " jne .read_num_check_plus") &&
           emit_line(emit, " mov r9, -1") &&
           emit_line(emit, " jmp .read_num_loop") &&
           emit_label(emit, ".read_num_check_plus") &&
           emit_line(emit, " cmp al, '+'") &&
           emit_line(emit, " jne .read_num_first_digit") &&
           emit_line(emit, " jmp .read_num_loop") &&
           emit_label(emit, ".read_num_first_digit") &&
           emit_line(emit, " cmp al, '0'") &&
           emit_line(emit, " jb .read_num_finish") &&
           emit_line(emit, " cmp al, '9'") &&
           emit_line(emit, " ja .read_num_finish") &&
           emit_line(emit, " sub al, '0'") &&
           emit_line(emit, " movzx rcx, al") &&
           emit_line(emit, " mov r8, rcx") &&
           emit_line(emit, " mov r10d, 1") &&
           emit_label(emit, ".read_num_loop") &&
           emit_line(emit, " mov rax, 0") &&
           emit_line(emit, " mov rdi, 0") &&
           emit_line(emit, " lea rsi, [rbp - 1]") &&
           emit_line(emit, " mov rdx, 1") &&
           emit_line(emit, " syscall") &&
           emit_line(emit, " cmp rax, 1") &&
           emit_line(emit, " jne .read_num_finish") &&
           emit_line(emit, " movzx eax, byte [rbp - 1]") &&
           emit_line(emit, " cmp al, '0'") &&
           emit_line(emit, " jb .read_num_finish") &&
           emit_line(emit, " cmp al, '9'") &&
           emit_line(emit, " ja .read_num_finish") &&
           emit_line(emit, " imul r8, r8, 10") &&
           emit_line(emit, " sub al, '0'") &&
           emit_line(emit, " movzx rcx, al") &&
           emit_line(emit, " add r8, rcx") &&
           emit_line(emit, " mov r10d, 1") &&
           emit_line(emit, " jmp .read_num_loop") &&
           emit_label(emit, ".read_num_finish") &&
           emit_line(emit, " mov rax, r8") &&
           emit_line(emit, " cmp r9, 1") &&
           emit_line(emit, " je .read_num_done") &&
           emit_line(emit, " neg rax") &&
           emit_label(emit, ".read_num_done") &&
           emit_line(emit, " mov rsp, rbp") &&
           emit_line(emit, " pop rbp") &&
           emit_line(emit, " ret") &&
           emit_blank_line(emit);
}

static bool emit_runtime_section(codegen_state* state)
{
    assert(state);

    return emit_comment(state->emit, "runtime helpers") &&
           emit_runtime_print_text(state->emit) &&
           emit_runtime_print_char(state->emit) &&
           emit_runtime_print_num(state->emit) &&
           emit_runtime_read_num(state->emit);
}

static bool emit_text_bytes(emit_context* emit, const char* text)
{
    assert(emit);
    assert(text);

    if (!emit->out)
        return false;

    if (fprintf(emit->out, " db ") < 0)
        return false;

    const unsigned char* ptr = (const unsigned char*)text;
    bool first = true;

    while (*ptr != '\0')
    {
        if (!first)
        {
            if (fprintf(emit->out, ", ") < 0)
                return false;
        }

        if (fprintf(emit->out, "%u", (unsigned int)*ptr) < 0)
            return false;

        first = false;
        ++ptr;
    }

    if (first)
    {
        if (fprintf(emit->out, "0") < 0)
            return false;
    }

    return fprintf(emit->out, "\n") >= 0;
}

static bool emit_data_section_nasm(codegen_state* state)
{
    assert(state);

    if (state->text_constant_count <= 0)
        return true;

    if (!emit_line(state->emit, "section .data") ||
        !emit_blank_line(state->emit))
    {
        return false;
    }

    for (int index = 0; index < state->text_constant_count; ++index)
    {
        char label[LABEL_TEXT_SIZE] = "";
        get_text_label(label, sizeof(label), state->text_constants[index].label_id);

        if (!emit_comment(state->emit, state->text_constants[index].text) ||
            !emit_label(state->emit, label) ||
            !emit_text_bytes(state->emit, state->text_constants[index].text) ||
            !emit_blank_line(state->emit))
        {
            return false;
        }
    }

    return true;
}

static bool emit_data_section_bin(codegen_state* state)
{
    assert(state);

    if (state->text_constant_count <= 0)
        return true;

    if (!emit_align(state->emit, 8, 0x90))
        return false;

    for (int index = 0; index < state->text_constant_count; ++index)
    {
        char label[LABEL_TEXT_SIZE] = "";
        get_text_label(label, sizeof(label), state->text_constants[index].label_id);

        const text_constant* constant = &state->text_constants[index];
        if (!emit_label(state->emit, label) ||
            !emit_bytes(state->emit, constant->text, (size_t)constant->length))
        {
            return false;
        }
    }

    return true;
}

static void get_entry_label(const program_symbols* symbols, char* entry_label, size_t label_size)
{
    assert(symbols);
    assert(entry_label);
    assert(label_size > 0);

    const function_symbol* entry_function = &symbols->functions[symbols->entry_index];
    get_function_label(symbols, entry_function, entry_label, label_size);
}

static bool emit_start_code_nasm(codegen_state* state, const program_symbols* symbols)
{
    assert(state);
    assert(symbols);

    char entry_label[LABEL_TEXT_SIZE] = "";
    get_entry_label(symbols, entry_label, sizeof(entry_label));

    return emit_label(state->emit, "_start") &&
           emit_comment(state->emit, "call the first language function and exit with its result") &&
           emit_call_label(state->emit, entry_label) &&
           emit_mov_reg_reg(state->emit, REG_RDI, REG_RAX) &&
           emit_mov_reg_imm(state->emit, REG_RAX, 60) &&
           emit_syscall(state->emit) &&
           emit_blank_line(state->emit);
}

static bool emit_start_code_elf(codegen_state* state, const program_symbols* symbols)
{
    assert(state);
    assert(symbols);

    char entry_label[LABEL_TEXT_SIZE] = "";
    get_entry_label(symbols, entry_label, sizeof(entry_label));

    return emit_label(state->emit, "_start") &&
           emit_comment(state->emit, "call the first language function and exit with its result") &&
           emit_call_label(state->emit, entry_label) &&
           emit_mov_reg_reg(state->emit, REG_RDI, REG_RAX) &&
           emit_call_label(state->emit, "WL_RT_EXIT") &&
           emit_blank_line(state->emit);
}

static bool emit_file_header_nasm(codegen_state* state, const program_symbols* symbols)
{
    assert(state);
    assert(symbols);

    return emit_comment(state->emit, "Witcher Language backend, semantic emitter stage") &&
           emit_line(state->emit, "BITS 64") &&
           emit_line(state->emit, "DEFAULT REL") &&
           emit_blank_line(state->emit) &&
           emit_line(state->emit, "global _start") &&
           emit_line(state->emit, "section .text") &&
           emit_blank_line(state->emit) &&
           emit_start_code_nasm(state, symbols);
}

static bool emit_runtime_code(codegen_state* state, const unsigned char* code, size_t code_size)
{
    assert(state);
    assert(code);

    const size_t table_size = (size_t)RUNTIME_ENTRY_COUNT * RUNTIME_TRAMPOLINE_SIZE;
    if (code_size < table_size)
    {
        codegen_fail(state, "runtime code is too small");
        return false;
    }

    for (int index = 0; index < RUNTIME_ENTRY_COUNT; ++index)
    {
        const size_t offset = (size_t)index * RUNTIME_TRAMPOLINE_SIZE;

        if (!emit_label(state->emit, RUNTIME_ENTRY_LABELS[index]) ||
            !emit_bytes(state->emit, code + offset, RUNTIME_TRAMPOLINE_SIZE))
        {
            return false;
        }
    }

    return emit_bytes(state->emit, code + table_size, code_size - table_size) &&
           emit_align(state->emit, 16, 0x90);
}

static bool emit_function_definitions(codegen_state* state)
{
    assert(state);
    assert(state->symbols);

    for (int index = 0; index < state->symbols->function_count; ++index)
    {
        if (!emit_function_definition(state, &state->symbols->functions[index]))
        {
            if (state->result->error_text[0] == '\0')
            {
                snprintf(state->result->error_text,
                         EXEC_ERROR_TEXT_SIZE,
                         "failed to emit function '%s'",
                         state->symbols->functions[index].name);
            }

            return false;
        }
    }

    return true;
}

static void init_codegen_state(codegen_state* state,
                               const program_symbols* symbols,
                               emit_context* emit,
                               exec_result* result)
{
    assert(state);
    assert(symbols);
    assert(emit);
    assert(result);

    memset(state, 0, sizeof(*state));
    state->symbols = symbols;
    state->current_function = NULL;
    state->emit = emit;
    state->result = result;
    state->next_label_id = 0;
    state->break_label_count = 0;
    state->text_constants = NULL;
    state->text_constant_count = 0;
    state->text_constant_capacity = 0;
}

static bool validate_codegen_input(const node_t* program_root,
                                   const program_symbols* symbols,
                                   exec_result* result)
{
    assert(symbols);
    assert(result);

    if (!program_root)
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

    return true;
}

static bool generate_nasm_program(const node_t* program_root,
                                  const program_symbols* symbols,
                                  FILE* out,
                                  exec_result* result)
{
    assert(symbols);
    assert(out);
    assert(result);
    (void)program_root;

    emit_context emit = {};
    if (!emit_context_ctor_nasm(&emit, out))
    {
        snprintf(result->error_text, EXEC_ERROR_TEXT_SIZE, "failed to create NASM emitter");
        return false;
    }

    codegen_state state = {};
    init_codegen_state(&state, symbols, &emit, result);

    bool ok = emit_file_header_nasm(&state, symbols) &&
              emit_function_definitions(&state) &&
              emit_runtime_section(&state) &&
              emit_data_section_nasm(&state);

    if (ok && emit.error_text[0] != '\0')
    {
        snprintf(result->error_text, EXEC_ERROR_TEXT_SIZE, "%s", emit.error_text);
        ok = false;
    }

    emit_context_reset(&emit);
    free(state.text_constants);

    return ok && result->error_text[0] == '\0';
}

static bool generate_elf_program(const node_t* program_root,
                                 const program_symbols* symbols,
                                 FILE* out,
                                 exec_result* result)
{
    assert(symbols);
    assert(out);
    assert(result);
    (void)program_root;

    emit_context emit = {};
    if (!emit_context_ctor_bin(&emit))
    {
        snprintf(result->error_text, EXEC_ERROR_TEXT_SIZE, "failed to create binary emitter");
        return false;
    }

    unsigned char* runtime_code = NULL;
    size_t runtime_code_size = 0;
    bool ok = runtime_code_read(RUNTIME_CODE_FILE, &runtime_code, &runtime_code_size);
    if (!ok)
        snprintf(result->error_text, EXEC_ERROR_TEXT_SIZE, "failed to read runtime code file '%s'", RUNTIME_CODE_FILE);

    codegen_state state = {};
    init_codegen_state(&state, symbols, &emit, result);

    if (ok)
        ok = emit_runtime_code(&state, runtime_code, runtime_code_size);

    size_t entry_offset = 0;
    if (ok)
    {
        entry_offset = emit_bin_size(&emit);
        ok = emit_start_code_elf(&state, symbols) &&
             emit_function_definitions(&state) &&
             emit_data_section_bin(&state);
    }

    if (ok && !emit_bin_resolve_fixups(&emit))
    {
        if (result->error_text[0] == '\0')
            snprintf(result->error_text, EXEC_ERROR_TEXT_SIZE, "%s", emit.error_text);
        ok = false;
    }

    if (ok && emit.error_text[0] != '\0')
    {
        snprintf(result->error_text, EXEC_ERROR_TEXT_SIZE, "%s", emit.error_text);
        ok = false;
    }

    if (ok && !write_min_elf64(out,
                               emit_bin_data(&emit),
                               emit_bin_size(&emit),
                               entry_offset,
                               result->error_text,
                               EXEC_ERROR_TEXT_SIZE))
    {
        ok = false;
    }

    emit_context_reset(&emit);
    free(runtime_code);
    free(state.text_constants);

    return ok && result->error_text[0] == '\0';
}

void exec_result_ctor(exec_result* result)
{
    if (!result)
        return;

    result->error_text[0] = '\0';
}

bool exec_generate_program(const node_t* program_root,
                           const program_symbols* symbols,
                           FILE* out,
                           output_format format,
                           exec_result* result)
{
    assert(symbols);
    assert(out);
    assert(result);

    exec_result_ctor(result);

    if (!validate_codegen_input(program_root, symbols, result))
        return false;

    switch (format)
    {
        case OUT_FORMAT_NASM:
            return generate_nasm_program(program_root, symbols, out, result);

        case OUT_FORMAT_ELF:
            return generate_elf_program(program_root, symbols, out, result);

        default:
            snprintf(result->error_text, EXEC_ERROR_TEXT_SIZE, "unknown output format");
            return false;
    }
}
