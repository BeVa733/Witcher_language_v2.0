#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "reverse_end.h"
#include "lexemes.h"

struct reverse_writer
{
    FILE* out;
    reverse_end_options options;
};

static void write_indent(reverse_writer* writer, int indent_level)
{
    assert(writer != NULL);
    assert(writer->out != NULL);

    for (int i = 0; i < indent_level * writer->options.indent_width; ++i)
        fputc(' ', writer->out);
}

static bool is_binary_expr_oper(enum opers oper)
{
    return oper == OPER_ADD || oper == OPER_SUB || oper == OPER_MUL || oper == OPER_DIV ||
           oper == OPER_EQ  || oper == OPER_NEQ || oper == OPER_LT  || oper == OPER_GT  ||
           oper == OPER_LE  || oper == OPER_GE;
}

static int oper_precedence(enum opers oper)
{
    switch (oper)
    {
        case OPER_ASSIGN: return 1;
        case OPER_EQ:
        case OPER_NEQ:
        case OPER_LT:
        case OPER_GT:
        case OPER_LE:
        case OPER_GE:
            return 2;
        case OPER_ADD:
        case OPER_SUB:
            return 3;
        case OPER_MUL:
        case OPER_DIV:
            return 4;
        case OPER_NEG:
        case OPER_POS:
            return 5;
        default:
            return 100;
    }
}

static bool node_is_statement_block(const node_t* node)
{
    return node != NULL && node->type == NODE_OPER && node->data_type == DATA_OPER && node->data.oper == OPER_STMT_SEP;
}

static bool node_is_statement_form(const node_t* node)
{
    if (node == NULL)
        return false;

    if (node->type != NODE_OPER || node->data_type != DATA_OPER)
        return false;

    switch (node->data.oper)
    {
        case OPER_IF:
        case OPER_WHILE:
        case OPER_RETURN:
        case OPER_BREAK:
        case OPER_PRINT:
        case OPER_PRINTC:
        case OPER_SCAN:
        case OPER_ASSIGN:
        case OPER_STMT_SEP:
            return true;
        default:
            return false;
    }
}

static bool write_expression(reverse_writer* writer, const node_t* node, int parent_precedence);
static bool write_statement(reverse_writer* writer, const node_t* node, int indent_level);

static bool write_printc_items(reverse_writer* writer, const node_t* chain)
{
    bool first = true;
    const node_t* current = chain;

    while (current != NULL)
    {
        if (!first)
            fputc(' ', writer->out);

        if (current->type == NODE_TEXT)
        {
            if (current->data.string == NULL)
                return false;
            fputs(current->data.string, writer->out);
        }
        else if (current->type == NODE_NUM)
        {
            fprintf(writer->out, "%d", current->data.number);
        }
        else
        {
            return false;
        }

        current = current->right;
        first = false;
    }

    return true;
}


static bool write_identifier(const node_t* node, FILE* out)
{
    if (node == NULL || out == NULL)
        return false;

    if ((node->type != NODE_ID && node->type != NODE_VAR && node->type != NODE_FUNC) || node->data_type != DATA_STRING || node->data.string == NULL)
        return false;

    fputs(node->data.string, out);
    return true;
}

static bool write_argument_chain_items(reverse_writer* writer, const node_t* chain, bool* first)
{
    if (chain == NULL)
        return true;

    if (chain->type == NODE_GLUE)
        return write_argument_chain_items(writer, chain->left, first) &&
               write_argument_chain_items(writer, chain->right, first);

    if (!*first)
        fputs(", ", writer->out);

    if (!write_expression(writer, chain, 0))
        return false;

    *first = false;
    return true;
}

static bool write_argument_chain(reverse_writer* writer, const node_t* chain)
{
    bool first = true;
    return write_argument_chain_items(writer, chain, &first);
}

static bool write_parameter_chain(reverse_writer* writer, const node_t* chain)
{
    bool first = true;
    const node_t* current = chain;

    while (current != NULL)
    {
        if (!first)
            fputs(", ", writer->out);

        if (!write_identifier(current, writer->out))
            return false;

        current = current->right;
        first = false;
    }

    return true;
}

static bool write_expression(reverse_writer* writer, const node_t* node, int parent_precedence)
{
    assert(writer != NULL);
    assert(writer->out != NULL);

    if (node == NULL)
        return false;

    switch (node->type)
    {
        case NODE_NUM:
            fprintf(writer->out, "%d", node->data.number);
            return true;

        case NODE_VAR:
        case NODE_ID:
            return write_identifier(node, writer->out);

        case NODE_TEXT:
            if (node->data.string == NULL)
                return false;
            fputs(node->data.string, writer->out);
            return true;

        case NODE_FUNC:
            if (!write_identifier(node, writer->out))
                return false;
            fputc('(', writer->out);
            if (!write_argument_chain(writer, node->right))
                return false;
            fputc(')', writer->out);
            return true;

        case NODE_OPER:
        {
            enum opers oper = node->data.oper;

            if (oper == OPER_NEG || oper == OPER_POS)
            {
                const int precedence = oper_precedence(oper);
                const bool need_paren = precedence < parent_precedence;
                if (need_paren)
                    fputc('(', writer->out);

                fputs(lexeme_info_by_oper(oper)->source_name, writer->out);
                if (!write_expression(writer, node->right, precedence))
                    return false;

                if (need_paren)
                    fputc(')', writer->out);
                return true;
            }

            if (oper == OPER_ASSIGN || is_binary_expr_oper(oper))
            {
                const int precedence = oper_precedence(oper);
                const bool need_paren = precedence < parent_precedence;
                if (need_paren)
                    fputc('(', writer->out);

                if (!write_expression(writer, node->left, precedence))
                    return false;
                fprintf(writer->out, " %s ", lexeme_info_by_oper(oper)->source_name);
                if (!write_expression(writer, node->right, precedence + (oper == OPER_ASSIGN ? 0 : 1)))
                    return false;

                if (need_paren)
                    fputc(')', writer->out);
                return true;
            }

            return false;
        }

        default:
            return false;
    }
}

static bool write_block_body(reverse_writer* writer, const node_t* node, int indent_level)
{
    if (node == NULL)
        return true;

    if (node_is_statement_block(node))
    {
        if (!write_block_body(writer, node->left, indent_level))
            return false;
        if (!write_block_body(writer, node->right, indent_level))
            return false;
        return true;
    }

    return write_statement(writer, node, indent_level);
}

static bool write_statement_as_block(reverse_writer* writer, const node_t* node, int indent_level)
{
    write_indent(writer, indent_level);
    fputs("{\n", writer->out);
    if (!write_block_body(writer, node, indent_level + 1))
        return false;
    write_indent(writer, indent_level);
    fputs("}\n", writer->out);
    return true;
}

static bool write_if_statement(reverse_writer* writer, const node_t* node, int indent_level)
{
    const node_t* condition = node->left;
    const node_t* else_node = node->right;

    if (condition == NULL || else_node == NULL || else_node->type != NODE_OPER || else_node->data.oper != OPER_ELSE)
        return false;

    write_indent(writer, indent_level);
    fprintf(writer->out, "%s (", lexeme_info_by_oper(OPER_IF)->source_name);
    if (!write_expression(writer, condition, 0))
        return false;
    fputs(")\n", writer->out);

    if (!write_statement_as_block(writer, else_node->left, indent_level))
        return false;

    write_indent(writer, indent_level);
    fprintf(writer->out, "%s\n", lexeme_info_by_oper(OPER_ELSE)->source_name);
    return write_statement_as_block(writer, else_node->right, indent_level);
}

static bool write_while_statement(reverse_writer* writer, const node_t* node, int indent_level)
{
    write_indent(writer, indent_level);
    fprintf(writer->out, "%s (", lexeme_info_by_oper(OPER_WHILE)->source_name);
    if (!write_expression(writer, node->left, 0))
        return false;
    fputs(")\n", writer->out);
    return write_statement_as_block(writer, node->right, indent_level);
}

static bool write_statement(reverse_writer* writer, const node_t* node, int indent_level)
{
    assert(writer != NULL);
    assert(writer->out != NULL);

    if (node == NULL)
        return true;

    if (node_is_statement_block(node))
        return write_statement_as_block(writer, node, indent_level);

    if (node->type == NODE_OPER && node->data_type == DATA_OPER)
    {
        switch (node->data.oper)
        {
            case OPER_IF:
                return write_if_statement(writer, node, indent_level);

            case OPER_WHILE:
                return write_while_statement(writer, node, indent_level);

            case OPER_RETURN:
                write_indent(writer, indent_level);
                fprintf(writer->out, "%s ", lexeme_info_by_oper(OPER_RETURN)->source_name);
                if (node->right != NULL && !write_expression(writer, node->right, 0))
                    return false;
                fputs(";\n", writer->out);
                return true;

            case OPER_BREAK:
                write_indent(writer, indent_level);
                fprintf(writer->out, "%s;\n", lexeme_info_by_oper(OPER_BREAK)->source_name);
                return true;

            case OPER_PRINT:
                write_indent(writer, indent_level);
                fprintf(writer->out, "%s(", lexeme_info_by_oper(OPER_PRINT)->source_name);
                if (node->right != NULL && !write_expression(writer, node->right, 0))
                    return false;
                fputs(");\n", writer->out);
                return true;

            case OPER_PRINTC:
                write_indent(writer, indent_level);
                fprintf(writer->out, "%s(", lexeme_info_by_oper(OPER_PRINTC)->source_name);
                if (node->right != NULL && !write_printc_items(writer, node->right))
                    return false;
                fputs(");\n", writer->out);
                return true;

            case OPER_SCAN:
                write_indent(writer, indent_level);
                fprintf(writer->out, "%s(", lexeme_info_by_oper(OPER_SCAN)->source_name);
                if (node->right != NULL && !write_expression(writer, node->right, 0))
                    return false;
                fputs(");\n", writer->out);
                return true;

            case OPER_ASSIGN:
                write_indent(writer, indent_level);
                if (!write_expression(writer, node, 0))
                    return false;
                fputs(";\n", writer->out);
                return true;

            default:
                break;
        }
    }

    if (node->type == NODE_FUNC)
    {
        write_indent(writer, indent_level);
        if (!write_expression(writer, node, 0))
            return false;
        fputs(";\n", writer->out);
        return true;
    }

    return false;
}

static bool write_function(reverse_writer* writer, const node_t* node)
{
    if (node == NULL || node->type != NODE_OPER || node->data.oper != OPER_FUNC)
        return false;

    const node_t* func_name = node->left;
    const node_t* body = node->right;

    fprintf(writer->out, "%s ", lexeme_info_by_oper(OPER_FUNC)->source_name);
    if (!write_identifier(func_name, writer->out))
        return false;
    fputc('(', writer->out);
    if (!write_parameter_chain(writer, func_name != NULL ? func_name->right : NULL))
        return false;
    fputs(")\n", writer->out);
    if (!write_statement_as_block(writer, body, 0))
        return false;
    fputc('\n', writer->out);
    return true;
}

static bool write_program(reverse_writer* writer, const node_t* node)
{
    if (node == NULL)
        return false;

    if (node->type == NODE_GLUE)
    {
        if (!write_program(writer, node->left))
            return false;
        return write_program(writer, node->right);
    }

    return write_function(writer, node);
}

void reverse_end_options_ctor(reverse_end_options* options)
{
    assert(options != NULL);
    options->indent_width = 4;
    options->always_brace_blocks = true;
}

bool reverse_end_write_source(const node_t* root, FILE* out, const reverse_end_options* options)
{
    assert(root != NULL);
    assert(out != NULL);

    reverse_writer writer = {};
    writer.out = out;
    reverse_end_options_ctor(&writer.options);
    if (options != NULL)
        writer.options = *options;

    return write_program(&writer, root);
}

bool reverse_end_write_source_file(const node_t* root, const char* filename, const reverse_end_options* options)
{
    assert(root != NULL);
    assert(filename != NULL);

    FILE* file = fopen(filename, "wb");
    if (file == NULL)
        return false;

    const bool ok = reverse_end_write_source(root, file, options);
    fclose(file);
    return ok;
}
