#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend_parse.h"

struct function_decl
{
    char* name;
    int arity;
    int line;
    int column;
};

struct function_table
{
    function_decl* items;
    int count;
    int capacity;
};

struct parser_state
{
    lexer_result* lexer;
    parser_result* result;
    int index;
    function_table* functions;
};

static char* parser_strdup(const char* source)
{
    if (source == NULL)
        return NULL;

    const size_t length = strlen(source);
    char* copy = (char*)calloc(length + 1, sizeof(char));
    if (copy == NULL)
        return NULL;

    memcpy(copy, source, length);
    copy[length] = '\0';
    return copy;
}

static void function_table_ctor(function_table* table)
{
    if (table == NULL)
        return;

    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

static void function_table_dtor(function_table* table)
{
    if (table == NULL)
        return;

    if (table->items != NULL)
    {
        for (int index = 0; index < table->count; ++index)
            free(table->items[index].name);
        free(table->items);
    }

    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

static bool function_table_reserve(function_table* table, int min_capacity)
{
    if (table == NULL)
        return false;

    if (table->capacity >= min_capacity)
        return true;

    int new_capacity = (table->capacity > 0) ? table->capacity : 16;
    while (new_capacity < min_capacity)
        new_capacity *= 2;

    function_decl* new_items = (function_decl*)realloc(table->items, (size_t)new_capacity * sizeof(function_decl));
    if (new_items == NULL)
        return false;

    table->items = new_items;
    table->capacity = new_capacity;
    return true;
}

static int function_table_find(const function_table* table, const char* name)
{
    if (table == NULL || name == NULL)
        return -1;

    for (int index = 0; index < table->count; ++index)
    {
        if (table->items[index].name != NULL && strcmp(table->items[index].name, name) == 0)
            return index;
    }

    return -1;
}

static bool function_table_add(function_table* table, const char* name, int arity, int line, int column)
{
    if (table == NULL || name == NULL)
        return false;

    if (!function_table_reserve(table, table->count + 1))
        return false;

    function_decl* slot = &table->items[table->count];
    slot->name = parser_strdup(name);
    if (slot->name == NULL)
        return false;

    slot->arity = arity;
    slot->line = line;
    slot->column = column;
    ++table->count;
    return true;
}

static const function_decl* function_table_lookup(const function_table* table, const char* name)
{
    const int index = function_table_find(table, name);
    return (index >= 0) ? &table->items[index] : NULL;
}

static void parser_result_set_error(parser_result* result, int line, int column, const char* text)
{
    if (result == NULL)
        return;

    result->error_line = line;
    result->error_column = column;

    if (text == NULL)
    {
        result->error_text[0] = '\0';
        return;
    }

    snprintf(result->error_text, PARSER_ERROR_TEXT_SIZE, "%s", text);
}

void parser_result_ctor(parser_result* result)
{
    if (result == NULL)
        return;

    result->root = NULL;
    result->error_line = 0;
    result->error_column = 0;
    result->error_text[0] = '\0';
}

void parser_result_reset(parser_result* result)
{
    if (result == NULL)
        return;

    tree_dtor(result->root);
    parser_result_ctor(result);
}

static node_t* lexer_token_at(const lexer_result* lexer, int index)
{
    if (lexer == NULL || index < 0 || index >= lexer->token_count)
        return NULL;
    return lexer->tokens[index];
}

static node_t* lexer_next_non_null(const lexer_result* lexer, int start_index, int* found_index)
{
    if (found_index != NULL)
        *found_index = -1;

    if (lexer == NULL)
        return NULL;

    for (int index = start_index; index < lexer->token_count; ++index)
    {
        if (lexer->tokens[index] != NULL)
        {
            if (found_index != NULL)
                *found_index = index;
            return lexer->tokens[index];
        }
    }

    return NULL;
}

static bool collect_function_declarations(const lexer_result* lexer, parser_result* result, function_table* table)
{
    if (lexer == NULL || result == NULL || table == NULL)
        return false;

    for (int index = 0; index < lexer->token_count; ++index)
    {
        node_t* token = lexer_token_at(lexer, index);
        if (token == NULL || token->type != NODE_OPER || token->data.oper != OPER_FUNC)
            continue;

        int name_index = -1;
        node_t* name_token = lexer_next_non_null(lexer, index + 1, &name_index);
        if (name_token == NULL || name_token->type != NODE_ID)
        {
            parser_result_set_error(result,
                                    name_token != NULL ? name_token->line : token->line,
                                    name_token != NULL ? name_token->column : token->column,
                                    "expected function name after declaration keyword"
                                   );
            return false;
        }

        if (function_table_find(table, name_token->data.string) >= 0)
        {
            char message[PARSER_ERROR_TEXT_SIZE] = "";
            snprintf(message, sizeof(message), "function '%s' is declared more than once", name_token->data.string);
            parser_result_set_error(result, name_token->line, name_token->column, message);
            return false;
        }

        int lparen_index = -1;
        node_t* lparen = lexer_next_non_null(lexer, name_index + 1, &lparen_index);
        if (lparen == NULL || lparen->type != NODE_OPER || lparen->data.oper != OPER_LPAREN)
        {
            parser_result_set_error(result,
                                    lparen != NULL ? lparen->line : name_token->line,
                                    lparen != NULL ? lparen->column : name_token->column,
                                    "expected '(' after function name in declaration"
                                   );
            return false;
        }

        int arity = 0;
        int current_index = lparen_index + 1;
        node_t* current = lexer_next_non_null(lexer, current_index, &current_index);

        if (current == NULL)
        {
            parser_result_set_error(result, name_token->line, name_token->column, "unexpected end of input in parameter list");
            return false;
        }

        if (!(current->type == NODE_OPER && current->data.oper == OPER_RPAREN))
        {
            while (true)
            {
                if (current == NULL || current->type != NODE_ID)
                {
                    parser_result_set_error(result,
                                            current != NULL ? current->line : name_token->line,
                                            current != NULL ? current->column : name_token->column,
                                            "expected parameter name in function declaration"
                                           );
                    return false;
                }

                ++arity;

                current = lexer_next_non_null(lexer, current_index + 1, &current_index);
                if (current == NULL)
                {
                    parser_result_set_error(result, name_token->line, name_token->column, "unexpected end of input in parameter list");
                    return false;
                }

                if (current->type == NODE_OPER && current->data.oper == OPER_RPAREN)
                    break;

                if (!(current->type == NODE_OPER && current->data.oper == OPER_COMMA))
                {
                    parser_result_set_error(result, current->line, current->column, "expected ',' or ')' in parameter list");
                    return false;
                }

                current = lexer_next_non_null(lexer, current_index + 1, &current_index);
                if (current == NULL)
                {
                    parser_result_set_error(result, name_token->line, name_token->column, "unexpected end of input in parameter list");
                    return false;
                }
            }
        }

        if (!function_table_add(table, name_token->data.string, arity, name_token->line, name_token->column))
        {
            parser_result_set_error(result, name_token->line, name_token->column, "failed to store function declaration info");
            return false;
        }
    }

    return true;
}

static void parser_skip_nulls(parser_state* state)
{
    if (state == NULL || state->lexer == NULL)
        return;

    while (state->index < state->lexer->token_count && state->lexer->tokens[state->index] == NULL)
        ++state->index;
}

static node_t* parser_peek(parser_state* state)
{
    if (state == NULL || state->lexer == NULL)
        return NULL;

    parser_skip_nulls(state);

    if (state->index >= state->lexer->token_count)
        return NULL;

    return state->lexer->tokens[state->index];
}

static node_t* parser_peek_offset(parser_state* state, int offset)
{
    if (state == NULL || state->lexer == NULL || offset < 0)
        return NULL;

    parser_skip_nulls(state);

    int seen = 0;
    for (int index = state->index; index < state->lexer->token_count; ++index)
    {
        node_t* token = state->lexer->tokens[index];
        if (token == NULL)
            continue;

        if (seen == offset)
            return token;

        ++seen;
    }

    return NULL;
}

static bool parser_current_is_oper(parser_state* state, enum opers oper)
{
    node_t* token = parser_peek(state);
    return token != NULL && token->type == NODE_OPER && token->data.oper == oper;
}

static node_t* parser_steal_current(parser_state* state)
{
    node_t* token = parser_peek(state);
    if (token == NULL)
        return NULL;

    state->lexer->tokens[state->index] = NULL;
    ++state->index;
    return token;
}

static void parser_destroy_current(parser_state* state)
{
    node_t* token = parser_steal_current(state);
    tree_dtor(token);
}

static bool parser_expect_oper(parser_state* state, enum opers oper, const char* message)
{
    if (!parser_current_is_oper(state, oper))
    {
        node_t* token = parser_peek(state);
        parser_result_set_error(state->result,
                                token != NULL ? token->line : 0,
                                token != NULL ? token->column : 0,
                                message);
        return false;
    }

    parser_destroy_current(state);
    return true;
}

static node_t* parse_statement(parser_state* state);
static node_t* parse_printc_statement(parser_state* state);
static node_t* parse_block(parser_state* state);
static node_t* parse_condition(parser_state* state);
static node_t* parse_expression(parser_state* state);

static node_t* append_statement(node_t* list_root, node_t* statement)
{
    if (statement == NULL)
        return list_root;

    if (list_root == NULL)
        return statement;

    const int line = statement->line;
    const int column = statement->column;
    node_t* connector = node_create_oper(OPER_STMT_SEP, line, column);
    if (connector == NULL)
    {
        tree_dtor(list_root);
        tree_dtor(statement);
        return NULL;
    }

    node_set_left(connector, list_root);
    node_set_right(connector, statement);
    return connector;
}

static bool parse_parameter_list(parser_state* state, node_t* func_name)
{
    if (state == NULL || func_name == NULL)
        return false;

    if (parser_current_is_oper(state, OPER_RPAREN))
        return true;

    node_t* first_param = NULL;
    node_t* last_param = NULL;

    while (true)
    {
        node_t* token = parser_peek(state);
        if (token == NULL || token->type != NODE_ID)
        {
            parser_result_set_error(state->result,
                                    token != NULL ? token->line : 0,
                                    token != NULL ? token->column : 0,
                                    "expected parameter name");
            tree_dtor(first_param);
            return false;
        }

        node_t* param = parser_steal_current(state);
        if (!node_promote_id(param, NODE_VAR))
        {
            parser_result_set_error(state->result, param->line, param->column, "failed to convert parameter to variable node");
            tree_dtor(param);
            tree_dtor(first_param);
            return false;
        }

        if (first_param == NULL)
            first_param = param;
        else
            node_set_right(last_param, param);
        last_param = param;

        if (parser_current_is_oper(state, OPER_COMMA))
        {
            parser_destroy_current(state);
            continue;
        }

        break;
    }

    node_set_right(func_name, first_param);
    return true;
}

static bool parse_argument_list(parser_state* state, node_t* call_node, int* arg_count)
{
    if (state == NULL || call_node == NULL)
        return false;

    if (arg_count != NULL)
        *arg_count = 0;

    if (parser_current_is_oper(state, OPER_RPAREN))
        return true;

    node_t* argument_chain = NULL;
    int count = 0;

    while (true)
    {
        node_t* argument = parse_expression(state);
        if (argument == NULL)
        {
            tree_dtor(argument_chain);
            return false;
        }

        if (argument_chain == NULL)
        {
            argument_chain = argument;
        }
        else
        {
            node_t* glue = node_create_glue(argument->line, argument->column);
            if (glue == NULL)
            {
                tree_dtor(argument);
                tree_dtor(argument_chain);
                parser_result_set_error(state->result, 0, 0, "failed to allocate argument list node");
                return false;
            }

            node_set_left(glue, argument_chain);
            node_set_right(glue, argument);
            argument_chain = glue;
        }

        ++count;

        if (parser_current_is_oper(state, OPER_COMMA))
        {
            parser_destroy_current(state);
            continue;
        }

        break;
    }

    if (arg_count != NULL)
        *arg_count = count;

    node_set_right(call_node, argument_chain);
    return true;
}

static node_t* parse_primary(parser_state* state)
{
    node_t* token = parser_peek(state);
    if (token == NULL)
    {
        parser_result_set_error(state->result, 0, 0, "unexpected end of input in expression");
        return NULL;
    }

    if (token->type == NODE_NUM)
        return parser_steal_current(state);

    if (token->type == NODE_ID)
    {
        node_t* next = parser_peek_offset(state, 1);
        node_t* name = parser_steal_current(state);
        if (next != NULL && next->type == NODE_OPER && next->data.oper == OPER_LPAREN)
        {
            const function_decl* declaration = function_table_lookup(state->functions, name->data.string);
            if (declaration == NULL)
            {
                char message[PARSER_ERROR_TEXT_SIZE] = "";
                snprintf(message, sizeof(message), "call to undeclared function '%s'", name->data.string);
                parser_result_set_error(state->result, name->line, name->column, message);
                tree_dtor(name);
                return NULL;
            }

            if (!node_promote_id(name, NODE_FUNC))
            {
                parser_result_set_error(state->result, name->line, name->column, "failed to convert function call name");
                tree_dtor(name);
                return NULL;
            }

            if (!parser_expect_oper(state, OPER_LPAREN, "expected '(' after function name"))
            {
                tree_dtor(name);
                return NULL;
            }

            int actual_arity = 0;
            if (!parse_argument_list(state, name, &actual_arity))
            {
                tree_dtor(name);
                return NULL;
            }

            if (!parser_expect_oper(state, OPER_RPAREN, "expected ')' after function call arguments"))
            {
                tree_dtor(name);
                return NULL;
            }

            if (actual_arity != declaration->arity)
            {
                char message[PARSER_ERROR_TEXT_SIZE] = "";
                snprintf(message,
                         sizeof(message),
                         "function '%s' expects %d argument(s), but %d provided",
                         declaration->name,
                         declaration->arity,
                         actual_arity);
                parser_result_set_error(state->result, name->line, name->column, message);
                tree_dtor(name);
                return NULL;
            }

            return name;
        }

        if (!node_promote_id(name, NODE_VAR))
        {
            parser_result_set_error(state->result, name->line, name->column, "failed to convert identifier to variable");
            tree_dtor(name);
            return NULL;
        }

        return name;
    }

    if (token->type == NODE_OPER && token->data.oper == OPER_LPAREN)
    {
        parser_destroy_current(state);
        node_t* expression = parse_expression(state);
        if (expression == NULL)
            return NULL;

        if (!parser_expect_oper(state, OPER_RPAREN, "expected ')' after parenthesized expression"))
        {
            tree_dtor(expression);
            return NULL;
        }

        return expression;
    }

    parser_result_set_error(state->result, token->line, token->column, "expected primary expression");
    return NULL;
}

static node_t* parse_unary(parser_state* state)
{
    if (parser_current_is_oper(state, OPER_ADD) || parser_current_is_oper(state, OPER_SUB))
    {
        node_t* oper = parser_steal_current(state);
        oper->data.oper = (oper->data.oper == OPER_SUB) ? OPER_NEG : OPER_POS;

        node_t* operand = parse_unary(state);
        if (operand == NULL)
        {
            tree_dtor(oper);
            return NULL;
        }

        node_set_right(oper, operand);
        return oper;
    }

    return parse_primary(state);
}

static node_t* parse_term(parser_state* state)
{
    node_t* left = parse_unary(state);
    if (left == NULL)
        return NULL;

    while (parser_current_is_oper(state, OPER_MUL) || parser_current_is_oper(state, OPER_DIV))
    {
        node_t* oper = parser_steal_current(state);
        node_t* right = parse_unary(state);
        if (right == NULL)
        {
            tree_dtor(oper);
            tree_dtor(left);
            return NULL;
        }

        node_set_left(oper, left);
        node_set_right(oper, right);
        left = oper;
    }

    return left;
}

static node_t* parse_expression(parser_state* state)
{
    node_t* left = parse_term(state);
    if (left == NULL)
        return NULL;

    while (parser_current_is_oper(state, OPER_ADD) || parser_current_is_oper(state, OPER_SUB))
    {
        node_t* oper = parser_steal_current(state);
        node_t* right = parse_term(state);
        if (right == NULL)
        {
            tree_dtor(oper);
            tree_dtor(left);
            return NULL;
        }

        node_set_left(oper, left);
        node_set_right(oper, right);
        left = oper;
    }

    return left;
}

static bool is_relop(enum opers oper)
{
    return oper == OPER_EQ || oper == OPER_NEQ || oper == OPER_LT || oper == OPER_GT || oper == OPER_LE || oper == OPER_GE;
}

static node_t* parse_condition(parser_state* state)
{
    node_t* left = parse_expression(state);
    if (left == NULL)
        return NULL;

    node_t* token = parser_peek(state);
    if (token == NULL || token->type != NODE_OPER || !is_relop(token->data.oper))
        return left;

    node_t* oper = parser_steal_current(state);
    node_t* right = parse_expression(state);
    if (right == NULL)
    {
        tree_dtor(oper);
        tree_dtor(left);
        return NULL;
    }

    node_set_left(oper, left);
    node_set_right(oper, right);
    return oper;
}

static node_t* parse_assignment_statement(parser_state* state)
{
    node_t* name = parser_peek(state);
    node_t* assign = parser_peek_offset(state, 1);
    if (name == NULL || assign == NULL)
        return NULL;

    if (name->type != NODE_ID || assign->type != NODE_OPER || assign->data.oper != OPER_ASSIGN)
        return NULL;

    name = parser_steal_current(state);
    if (!node_promote_id(name, NODE_VAR))
    {
        parser_result_set_error(state->result, name->line, name->column, "failed to convert assignment target to variable");
        tree_dtor(name);
        return NULL;
    }

    node_t* assign_node = parser_steal_current(state);
    node_t* value = parse_expression(state);
    if (value == NULL)
    {
        tree_dtor(assign_node);
        tree_dtor(name);
        return NULL;
    }

    if (!parser_expect_oper(state, OPER_STMT_SEP, "expected ';' after assignment"))
    {
        tree_dtor(value);
        tree_dtor(assign_node);
        tree_dtor(name);
        return NULL;
    }

    node_set_left(assign_node, name);
    node_set_right(assign_node, value);
    return assign_node;
}

static node_t* parse_return_statement(parser_state* state)
{
    node_t* keyword = parser_steal_current(state);

    node_t* value = NULL;
    if (!parser_current_is_oper(state, OPER_STMT_SEP))
    {
        value = parse_expression(state);
        if (value == NULL)
        {
            tree_dtor(keyword);
            return NULL;
        }
    }

    if (!parser_expect_oper(state, OPER_STMT_SEP, "expected ';' after return statement"))
    {
        tree_dtor(value);
        tree_dtor(keyword);
        return NULL;
    }

    node_set_right(keyword, value);
    return keyword;
}

static node_t* parse_break_statement(parser_state* state)
{
    node_t* keyword = parser_steal_current(state);
    if (!parser_expect_oper(state, OPER_STMT_SEP, "expected ';' after break statement"))
    {
        tree_dtor(keyword);
        return NULL;
    }

    return keyword;
}

static node_t* parse_print_statement(parser_state* state)
{
    node_t* keyword = parser_steal_current(state);

    if (!parser_expect_oper(state, OPER_LPAREN, "expected '(' after print"))
    {
        tree_dtor(keyword);
        return NULL;
    }

    node_t* value = parse_expression(state);
    if (value == NULL)
    {
        tree_dtor(keyword);
        return NULL;
    }

    if (!parser_expect_oper(state, OPER_RPAREN, "expected ')' after print argument"))
    {
        tree_dtor(value);
        tree_dtor(keyword);
        return NULL;
    }

    if (!parser_expect_oper(state, OPER_STMT_SEP, "expected ';' after print statement"))
    {
        tree_dtor(value);
        tree_dtor(keyword);
        return NULL;
    }

    node_set_right(keyword, value);
    return keyword;
}


static node_t* parse_printc_statement(parser_state* state)
{
    node_t* keyword = parser_steal_current(state);

    if (!parser_expect_oper(state, OPER_LPAREN, "expected '(' after printc"))
    {
        tree_dtor(keyword);
        return NULL;
    }

    node_t* first_item = NULL;
    node_t* last_item = NULL;

    while (true)
    {
        node_t* token = parser_peek(state);
        if (token == NULL)
        {
            parser_result_set_error(state->result, keyword->line, keyword->column, "unexpected end of input inside printc(...) argument list");
            tree_dtor(first_item);
            tree_dtor(keyword);
            return NULL;
        }

        if (token->type == NODE_OPER && token->data.oper == OPER_RPAREN)
            break;

        node_t* item = NULL;
        if (token->type == NODE_NUM)
        {
            item = parser_steal_current(state);
        }
        else if (token->type == NODE_ID)
        {
            item = parser_steal_current(state);
            if (!node_promote_id(item, NODE_TEXT))
            {
                parser_result_set_error(state->result, item->line, item->column, "failed to convert printc text fragment");
                tree_dtor(item);
                tree_dtor(first_item);
                tree_dtor(keyword);
                return NULL;
            }
        }
        else
        {
            parser_result_set_error(state->result, token->line, token->column, "printc(...) accepts only raw text fragments and ASCII codes");
            tree_dtor(first_item);
            tree_dtor(keyword);
            return NULL;
        }

        if (first_item == NULL)
        {
            first_item = item;
            last_item = item;
        }
        else
        {
            node_set_right(last_item, item);
            last_item = item;
        }

        token = parser_peek(state);
        if (token != NULL && token->type == NODE_OPER && token->data.oper == OPER_COMMA)
            tree_dtor(parser_steal_current(state));
    }

    if (!parser_expect_oper(state, OPER_RPAREN, "expected ')' after printc arguments"))
    {
        tree_dtor(first_item);
        tree_dtor(keyword);
        return NULL;
    }

    if (!parser_expect_oper(state, OPER_STMT_SEP, "expected ';' after printc statement"))
    {
        tree_dtor(first_item);
        tree_dtor(keyword);
        return NULL;
    }

    node_set_right(keyword, first_item);
    return keyword;
}

static node_t* parse_scan_statement(parser_state* state)
{
    node_t* keyword = parser_steal_current(state);

    if (!parser_expect_oper(state, OPER_LPAREN, "expected '(' after scan"))
    {
        tree_dtor(keyword);
        return NULL;
    }

    node_t* token = parser_peek(state);
    if (token == NULL || token->type != NODE_ID)
    {
        parser_result_set_error(state->result,
                                token != NULL ? token->line : 0,
                                token != NULL ? token->column : 0,
                                "expected identifier inside scan(...)"
                               );
        tree_dtor(keyword);
        return NULL;
    }

    node_t* variable = parser_steal_current(state);
    if (!node_promote_id(variable, NODE_VAR))
    {
        parser_result_set_error(state->result, variable->line, variable->column, "failed to convert scan argument to variable");
        tree_dtor(variable);
        tree_dtor(keyword);
        return NULL;
    }

    if (!parser_expect_oper(state, OPER_RPAREN, "expected ')' after scan argument"))
    {
        tree_dtor(variable);
        tree_dtor(keyword);
        return NULL;
    }

    if (!parser_expect_oper(state, OPER_STMT_SEP, "expected ';' after scan statement"))
    {
        tree_dtor(variable);
        tree_dtor(keyword);
        return NULL;
    }

    node_set_right(keyword, variable);
    return keyword;
}

static node_t* parse_expression_statement(parser_state* state)
{
    node_t* expression = parse_expression(state);
    if (expression == NULL)
        return NULL;

    if (!parser_expect_oper(state, OPER_STMT_SEP, "expected ';' after expression statement"))
    {
        tree_dtor(expression);
        return NULL;
    }

    return expression;
}

static node_t* parse_if_statement(parser_state* state)
{
    node_t* if_node = parser_steal_current(state);

    if (!parser_expect_oper(state, OPER_LPAREN, "expected '(' after if"))
    {
        tree_dtor(if_node);
        return NULL;
    }

    node_t* condition = parse_condition(state);
    if (condition == NULL)
    {
        tree_dtor(if_node);
        return NULL;
    }

    if (!parser_expect_oper(state, OPER_RPAREN, "expected ')' after if condition"))
    {
        tree_dtor(condition);
        tree_dtor(if_node);
        return NULL;
    }

    node_t* then_branch = parse_statement(state);
    if (then_branch == NULL)
    {
        tree_dtor(condition);
        tree_dtor(if_node);
        return NULL;
    }

    node_set_left(if_node, condition);

    if (parser_current_is_oper(state, OPER_ELSE))
    {
        node_t* else_node = parser_steal_current(state);
        node_t* else_branch = parse_statement(state);
        if (else_branch == NULL)
        {
            tree_dtor(else_node);
            tree_dtor(if_node);
            return NULL;
        }

        node_set_left(else_node, then_branch);
        node_set_right(else_node, else_branch);
        node_set_right(if_node, else_node);
    }
    else
    {
        node_set_right(if_node, then_branch);
    }

    return if_node;
}

static node_t* parse_while_statement(parser_state* state)
{
    node_t* while_node = parser_steal_current(state);

    if (!parser_expect_oper(state, OPER_LPAREN, "expected '(' after while"))
    {
        tree_dtor(while_node);
        return NULL;
    }

    node_t* condition = parse_condition(state);
    if (condition == NULL)
    {
        tree_dtor(while_node);
        return NULL;
    }

    if (!parser_expect_oper(state, OPER_RPAREN, "expected ')' after while condition"))
    {
        tree_dtor(condition);
        tree_dtor(while_node);
        return NULL;
    }

    node_t* body = parse_statement(state);
    if (body == NULL)
    {
        tree_dtor(condition);
        tree_dtor(while_node);
        return NULL;
    }

    node_set_left(while_node, condition);
    node_set_right(while_node, body);
    return while_node;
}

static node_t* parse_block(parser_state* state)
{
    if (!parser_expect_oper(state, OPER_LBRACE, "expected '{' at block start"))
        return NULL;

    node_t* body = NULL;

    while (!parser_current_is_oper(state, OPER_RBRACE))
    {
        if (parser_peek(state) == NULL)
        {
            parser_result_set_error(state->result, 0, 0, "unexpected end of input inside block");
            tree_dtor(body);
            return NULL;
        }

        node_t* statement = parse_statement(state);
        if (statement == NULL)
        {
            tree_dtor(body);
            return NULL;
        }

        body = append_statement(body, statement);
        if (statement != NULL && body == NULL)
        {
            parser_result_set_error(state->result,
                                    statement->line,
                                    statement->column,
                                    "failed to build statement sequence"
                                   );
            return NULL;
        }
    }

    if (!parser_expect_oper(state, OPER_RBRACE, "expected '}' at block end"))
    {
        tree_dtor(body);
        return NULL;
    }

    return body;
}

static node_t* parse_statement(parser_state* state)
{
    node_t* token = parser_peek(state);
    if (token == NULL)
    {
        parser_result_set_error(state->result, 0, 0, "unexpected end of input while parsing statement");
        return NULL;
    }

    if (token->type == NODE_OPER)
    {
        switch (token->data.oper)
        {
            case OPER_LBRACE:  return parse_block(state);
            case OPER_IF:      return parse_if_statement(state);
            case OPER_WHILE:   return parse_while_statement(state);
            case OPER_RETURN:  return parse_return_statement(state);
            case OPER_BREAK:   return parse_break_statement(state);
            case OPER_PRINT:   return parse_print_statement(state);
            case OPER_PRINTC:  return parse_printc_statement(state);
            case OPER_SCAN:    return parse_scan_statement(state);
            default:           break;
        }
    }

    node_t* lookahead = parser_peek_offset(state, 1);
    if (token->type == NODE_ID && lookahead != NULL && lookahead->type == NODE_OPER && lookahead->data.oper == OPER_ASSIGN)
        return parse_assignment_statement(state);

    return parse_expression_statement(state);
}

static node_t* parse_function(parser_state* state)
{
    if (!parser_current_is_oper(state, OPER_FUNC))
    {
        node_t* token = parser_peek(state);
        parser_result_set_error(state->result,
                                token != NULL ? token->line : 0,
                                token != NULL ? token->column : 0,
                                "expected function declaration"
                               );
        return NULL;
    }

    node_t* func_node = parser_steal_current(state);

    node_t* name_token = parser_peek(state);
    if (name_token == NULL || name_token->type != NODE_ID)
    {
        parser_result_set_error(state->result,
                                name_token != NULL ? name_token->line : 0,
                                name_token != NULL ? name_token->column : 0,
                                "expected function name after declaration keyword"
                               );
        tree_dtor(func_node);
        return NULL;
    }

    node_t* func_name = parser_steal_current(state);
    if (!node_promote_id(func_name, NODE_FUNC))
    {
        parser_result_set_error(state->result, func_name->line, func_name->column, "failed to convert function name");
        tree_dtor(func_name);
        tree_dtor(func_node);
        return NULL;
    }

    if (!parser_expect_oper(state, OPER_LPAREN, "expected '(' after function name"))
    {
        tree_dtor(func_name);
        tree_dtor(func_node);
        return NULL;
    }

    if (!parse_parameter_list(state, func_name))
    {
        tree_dtor(func_name);
        tree_dtor(func_node);
        return NULL;
    }

    if (!parser_expect_oper(state, OPER_RPAREN, "expected ')' after parameter list"))
    {
        tree_dtor(func_name);
        tree_dtor(func_node);
        return NULL;
    }

    node_t* body = parse_block(state);
    if (body == NULL && state->result->error_text[0] == '\0')
    {
        parser_result_set_error(state->result, func_name->line, func_name->column, "expected function body");
    }

    if (body == NULL)
    {
        tree_dtor(func_name);
        tree_dtor(func_node);
        return NULL;
    }

    node_set_left(func_node, func_name);
    node_set_right(func_node, body);
    return func_node;
}

bool parse_program(lexer_result* lexer, parser_result* result)
{
    if (lexer == NULL || result == NULL)
        return false;

    parser_result_reset(result);

    function_table functions = {};
    function_table_ctor(&functions);

    if (!collect_function_declarations(lexer, result, &functions))
    {
        function_table_dtor(&functions);
        result->root = NULL;
        return false;
    }

    parser_state state = {};
    state.lexer = lexer;
    state.result = result;
    state.index = 0;
    state.functions = &functions;

    node_t* root = NULL;

    while (parser_peek(&state) != NULL)
    {
        node_t* function = parse_function(&state);
        if (function == NULL)
        {
            tree_dtor(root);
            function_table_dtor(&functions);
            result->root = NULL;
            return false;
        }

        if (root == NULL)
        {
            root = function;
        }
        else
        {
            node_t* glue = node_create_glue(function->line, function->column);
            if (glue == NULL)
            {
                tree_dtor(function);
                tree_dtor(root);
                function_table_dtor(&functions);
                parser_result_set_error(result, function->line, function->column, "failed to allocate glue node");
                result->root = NULL;
                return false;
            }

            node_set_left(glue, root);
            node_set_right(glue, function);
            root = glue;
        }
    }

    function_table_dtor(&functions);
    result->root = root;
    return root != NULL;
}
