#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "middle_end.h"
#include "tree_io.h"

static bool is_arith_oper(enum opers oper)
{
    return oper == OPER_ADD || oper == OPER_SUB || oper == OPER_MUL ||
           oper == OPER_DIV || oper == OPER_NEG || oper == OPER_POS;
}

static bool is_comparison_oper(enum opers oper)
{
    return oper == OPER_EQ || oper == OPER_NEQ || oper == OPER_LT ||
           oper == OPER_GT || oper == OPER_LE || oper == OPER_GE;
}

static void clear_node_payload(node_t* node)
{
    if (node == NULL)
        return;

    if (node->data_type == DATA_STRING)
    {
        free(node->data.string);
        node->data.string = NULL;
    }
}

static bool overwrite_node_with_clone(node_t* node, const node_t* replacement)
{
    assert(node != NULL);
    assert(replacement != NULL);

    node_t* clone = node_clone(replacement);
    if (clone == NULL)
        return false;

    node_t* old_left = node->left;
    node_t* old_right = node->right;
    node_t* parent = node->parent;

    clear_node_payload(node);

    node->type = clone->type;
    node->data_type = clone->data_type;
    node->data = clone->data;
    node->line = clone->line;
    node->column = clone->column;
    node->left = clone->left;
    node->right = clone->right;
    node->parent = parent;

    if (node->left != NULL)
        node->left->parent = node;
    if (node->right != NULL)
        node->right->parent = node;

    clone->left = NULL;
    clone->right = NULL;
    free(clone);

    tree_dtor(old_left);
    tree_dtor(old_right);
    return true;
}

static void node_to_number(node_t* node, int value)
{
    assert(node != NULL);

    clear_node_payload(node);
    tree_dtor(node->left);
    tree_dtor(node->right);

    node->type = NODE_NUM;
    node->data_type = DATA_INT;
    node->data.number = value;
    node->left = NULL;
    node->right = NULL;
}

static bool evaluate_const_expr(const node_t* node, int* out_value)
{
    assert(out_value != NULL);

    if (node == NULL)
        return false;

    if (node->type == NODE_NUM)
    {
        *out_value = node->data.number;
        return true;
    }

    if (node->type != NODE_OPER)
        return false;

    int left_value = 0;
    int right_value = 0;

    switch (node->data.oper)
    {
        case OPER_POS:
            if (!evaluate_const_expr(node->right, &right_value))
                return false;
            *out_value = right_value;
            return true;

        case OPER_NEG:
            if (!evaluate_const_expr(node->right, &right_value))
                return false;
            *out_value = -right_value;
            return true;

        case OPER_ADD:
            if (!evaluate_const_expr(node->left, &left_value) ||
                !evaluate_const_expr(node->right, &right_value))
                return false;
            *out_value = left_value + right_value;
            return true;

        case OPER_SUB:
            if (!evaluate_const_expr(node->left, &left_value) ||
                !evaluate_const_expr(node->right, &right_value))
                return false;
            *out_value = left_value - right_value;
            return true;

        case OPER_MUL:
            if (!evaluate_const_expr(node->left, &left_value) ||
                !evaluate_const_expr(node->right, &right_value))
                return false;
            *out_value = left_value * right_value;
            return true;

        case OPER_DIV:
            if (!evaluate_const_expr(node->left, &left_value) ||
                !evaluate_const_expr(node->right, &right_value))
                return false;
            if (right_value == 0)
                return false;
            *out_value = left_value / right_value;
            return true;

        default:
            return false;
    }
}

static bool simplify_local_expression(node_t* node)
{
    if (node == NULL || node->type != NODE_OPER)
        return false;

    int folded_value = 0;
    if (evaluate_const_expr(node, &folded_value))
    {
        node_to_number(node, folded_value);
        return true;
    }

    switch (node->data.oper)
    {
        case OPER_ADD:
            if (node->left != NULL && node->left->type == NODE_NUM && node->left->data.number == 0)
                return overwrite_node_with_clone(node, node->right);
            if (node->right != NULL && node->right->type == NODE_NUM && node->right->data.number == 0)
                return overwrite_node_with_clone(node, node->left);
            return false;

        case OPER_SUB:
            if (node->right != NULL && node->right->type == NODE_NUM && node->right->data.number == 0)
                return overwrite_node_with_clone(node, node->left);
            return false;

        case OPER_MUL:
            if ((node->left != NULL && node->left->type == NODE_NUM && node->left->data.number == 0) ||
                (node->right != NULL && node->right->type == NODE_NUM && node->right->data.number == 0))
            {
                node_to_number(node, 0);
                return true;
            }
            if (node->left != NULL && node->left->type == NODE_NUM && node->left->data.number == 1)
                return overwrite_node_with_clone(node, node->right);
            if (node->right != NULL && node->right->type == NODE_NUM && node->right->data.number == 1)
                return overwrite_node_with_clone(node, node->left);
            return false;

        case OPER_DIV:
            if (node->left != NULL && node->left->type == NODE_NUM && node->left->data.number == 0)
            {
                node_to_number(node, 0);
                return true;
            }
            if (node->right != NULL && node->right->type == NODE_NUM && node->right->data.number == 1)
                return overwrite_node_with_clone(node, node->left);
            return false;

        case OPER_POS:
            if (node->right != NULL)
                return overwrite_node_with_clone(node, node->right);
            return false;

        case OPER_NEG:
            if (node->right != NULL && node->right->type == NODE_OPER && node->right->data.oper == OPER_NEG && node->right->right != NULL)
                return overwrite_node_with_clone(node, node->right->right);
            return false;

        default:
            return false;
    }
}

static bool simplify_expression_tree(node_t* node);

static bool simplify_expression_list(node_t* node)
{
    if (node == NULL)
        return false;

    if (node->type == NODE_GLUE)
    {
        bool changed = false;
        if (node->left != NULL)
            changed |= simplify_expression_tree(node->left);
        if (node->right != NULL)
            changed |= simplify_expression_list(node->right);
        return changed;
    }

    return simplify_expression_tree(node);
}

static bool simplify_expression_tree(node_t* node)
{
    if (node == NULL)
        return false;

    bool made_change = false;
    bool changed = false;
    int iterations = 0;
    const int MAX_ITERATIONS = 128;

    do
    {
        changed = false;
        ++iterations;

        if (node->type == NODE_FUNC)
        {
            changed |= simplify_expression_list(node->right);
        }
        else if (node->type == NODE_GLUE)
        {
            changed |= simplify_expression_list(node);
        }
        else if (node->type == NODE_OPER)
        {
            if (is_arith_oper(node->data.oper) || is_comparison_oper(node->data.oper))
            {
                if (node->left != NULL)
                    changed |= simplify_expression_tree(node->left);
                if (node->right != NULL)
                    changed |= simplify_expression_tree(node->right);
            }

            if (is_arith_oper(node->data.oper))
                changed |= simplify_local_expression(node);
        }

        made_change |= changed;
    }
    while (changed && iterations < MAX_ITERATIONS);

    return made_change;
}

static bool simplify_ast(node_t* node)
{
    if (node == NULL)
        return false;

    bool changed = false;

    if (node->type == NODE_GLUE)
    {
        if (node->left != NULL)
            changed |= simplify_ast(node->left);
        if (node->right != NULL)
            changed |= simplify_ast(node->right);
        return changed;
    }

    if (node->type == NODE_FUNC)
    {
        changed |= simplify_expression_list(node->right);
        return changed;
    }

    if (node->type != NODE_OPER)
        return false;

    switch (node->data.oper)
    {
        case OPER_FUNC:
            if (node->right != NULL)
                changed |= simplify_ast(node->right);
            return changed;

        case OPER_STMT_SEP:
            if (node->left != NULL)
                changed |= simplify_ast(node->left);
            if (node->right != NULL)
                changed |= simplify_ast(node->right);
            return changed;

        case OPER_ASSIGN:
            if (node->right != NULL)
                changed |= simplify_expression_tree(node->right);
            return changed;

        case OPER_IF:
        case OPER_WHILE:
            if (node->left != NULL)
                changed |= simplify_expression_tree(node->left);
            if (node->right != NULL)
                changed |= simplify_ast(node->right);
            return changed;

        case OPER_ELSE:
            if (node->left != NULL)
                changed |= simplify_ast(node->left);
            if (node->right != NULL)
                changed |= simplify_ast(node->right);
            return changed;

        case OPER_RETURN:
        case OPER_PRINT:
            if (node->right != NULL)
                changed |= simplify_expression_tree(node->right);
            return changed;

        case OPER_PRINTC:
            if (node->right != NULL)
                changed |= simplify_expression_list(node->right);
            return changed;

        case OPER_SCAN:
        case OPER_BREAK:
            return false;

        default:
            return simplify_expression_tree(node);
    }
}

bool middle_end_simplify(node_t* root)
{
    return simplify_ast(root);
}

bool middle_end_process_file(const char* input_tree_filename,
                             const char* output_tree_filename,
                             char* error_text,
                             size_t error_text_size)
{
    if (error_text != NULL && error_text_size > 0)
        error_text[0] = '\0';

    tree_read_result read_result = {};
    tree_read_result_ctor(&read_result);

    if (!tree_read_from_file(input_tree_filename, &read_result))
    {
        if (error_text != NULL && error_text_size > 0)
            snprintf(error_text, error_text_size, "tree read error at offset %d: %s",
                     read_result.error_offset,
                     read_result.error_text[0] != '\0' ? read_result.error_text : "unknown error");
        tree_read_result_reset(&read_result);
        return false;
    }

    middle_end_simplify(read_result.root);

    FILE* out = fopen(output_tree_filename, "w");
    if (out == NULL)
    {
        if (error_text != NULL && error_text_size > 0)
            snprintf(error_text, error_text_size, "failed to open output file '%s'", output_tree_filename);
        tree_read_result_reset(&read_result);
        return false;
    }

    bool ok = tree_serialize(read_result.root, out);
    fclose(out);

    if (!ok)
    {
        if (error_text != NULL && error_text_size > 0)
            snprintf(error_text, error_text_size, "failed to serialize simplified tree");
        tree_read_result_reset(&read_result);
        return false;
    }

    tree_read_result_reset(&read_result);
    return true;
}
