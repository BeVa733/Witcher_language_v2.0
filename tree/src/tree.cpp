#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tree.h"

static char* tree_strdup(const char* source)
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

static node_t* node_create_empty(enum node_type type, enum node_data_type data_type, int line, int column)
{
    node_t* node = (node_t*)calloc(1, sizeof(node_t));
    if (node == NULL)
        return NULL;

    node->type = type;
    node->data_type = data_type;
    node->line = line;
    node->column = column;
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;

    return node;
}

static node_t* node_create_string(enum node_type type, const char* value, int line, int column)
{
    node_t* node = node_create_empty(type, DATA_STRING, line, column);
    if (node == NULL)
        return NULL;

    node->data.string = tree_strdup(value);
    if (value != NULL && node->data.string == NULL)
    {
        free(node);
        return NULL;
    }

    return node;
}

static void node_disconnect_from_parent(node_t* node)
{
    if (node == NULL || node->parent == NULL)
        return;

    if (node->parent->left == node)
        node->parent->left = NULL;
    else if (node->parent->right == node)
        node->parent->right = NULL;

    node->parent = NULL;
}

static void file_write_escaped(FILE* file, const char* text)
{
    assert(file != NULL);

    if (text == NULL)
        return;

    for (const char* current = text; *current != '\0'; ++current)
    {
        switch (*current)
        {
            case '\\': fputs("\\\\", file); break;
            case '"':  fputs("\\\"", file); break;
            case '\n': fputs("\\n", file); break;
            case '\r': fputs("\\r", file); break;
            case '\t': fputs("\\t", file); break;
            default:    fputc(*current, file); break;
        }
    }
}

static void file_write_dot_record_escaped(FILE* file, const char* text)
{
    assert(file != NULL);

    if (text == NULL)
        return;

    for (const char* current = text; *current != '\0'; ++current)
    {
        switch (*current)
        {
            case '\\': fputs("\\\\", file); break;
            case '"':  fputs("\\\"", file); break;
            case '\n': fputs("\\n", file); break;
            case '\r': fputs("\\r", file); break;
            case '\t': fputs("\\t", file); break;

            case '{':  fputs("\\{", file); break;
            case '}':  fputs("\\}", file); break;
            case '|':  fputs("\\|", file); break;
            case '<':  fputs("\\<", file); break;
            case '>':  fputs("\\>", file); break;

            default:   fputc(*current, file); break;
        }
    }
}

static const char* serialized_label_for_node(const node_t* node)
{
    assert(node != NULL);

    switch (node->type)
    {
        case NODE_OPER: return "oper";
        case NODE_NUM:  return "num";
        case NODE_ID:   return "id";
        case NODE_VAR:  return "var";
        case NODE_FUNC: return "func";
        case NODE_TEXT: return "text";
        case NODE_GLUE: return "glue";
        case NODE_NONE:
        default:        return "unknown";
    }
}

static bool tree_serialize_node(const node_t* node, FILE* out)
{
    assert(out != NULL);

    if (node == NULL)
    {
        fputs("nil", out);
        return !ferror(out);
    }

    fprintf(out, "(%s:\"", serialized_label_for_node(node));

    switch (node->type)
    {
        case NODE_OPER:
            file_write_escaped(out, oper_serialized_name(node->data.oper));
            break;

        case NODE_NUM:
            fprintf(out, "%d", node->data.number);
            break;

        case NODE_ID:
        case NODE_VAR:
        case NODE_FUNC:
        case NODE_TEXT:
            file_write_escaped(out, node->data.string);
            break;

        case NODE_GLUE:
        case NODE_NONE:
        default:
            break;
    }

    fputs("\" ", out);

    if (!tree_serialize_node(node->left, out))
        return false;

    fputc(' ', out);

    if (!tree_serialize_node(node->right, out))
        return false;

    fputc(')', out);
    return !ferror(out);
}

static void tree_dump_dot_node(const node_t* node, FILE* file)
{
    assert(node != NULL);
    assert(file != NULL);

    fprintf(file, "    node_%p [shape=Mrecord, label=\"{type=", (const void*)node);
    file_write_dot_record_escaped(file, node_type_name(node->type));
    fputs("|data_type=", file);
    file_write_dot_record_escaped(file, node_data_type_name(node->data_type));
    fputs("|value=", file);

    switch (node->type)
    {
        case NODE_OPER:
            file_write_dot_record_escaped(file, oper_debug_name(node->data.oper));
            break;

        case NODE_NUM:
            fprintf(file, "%d", node->data.number);
            break;

        case NODE_ID:
        case NODE_VAR:
        case NODE_FUNC:
        case NODE_TEXT:
            file_write_dot_record_escaped(file, node->data.string);
            break;

        case NODE_GLUE:
            fputs("glue", file);
            break;

        case NODE_NONE:
        default:
            fputs("<none>", file);
            break;
    }

    fprintf(file, "|line=%d|column=%d}\"]\n", node->line, node->column);

    if (node->left != NULL)
    {
        fprintf(file, "    node_%p -> node_%p [label=\"L\"]\n", (const void*)node, (const void*)node->left);
        tree_dump_dot_node(node->left, file);
    }

    if (node->right != NULL)
    {
        fprintf(file, "    node_%p -> node_%p [label=\"R\"]\n", (const void*)node, (const void*)node->right);
        tree_dump_dot_node(node->right, file);
    }
}

static char* build_png_path(const char* dot_path)
{
    assert(dot_path != NULL);

    const size_t dot_len = strlen(dot_path);
    const char* ext = strrchr(dot_path, '.');

    if (ext != NULL && strcmp(ext, ".dot") == 0)
    {
        const size_t prefix_len = (size_t)(ext - dot_path);
        char* png_path = (char*)calloc(prefix_len + 5, sizeof(char));
        if (png_path == NULL)
            return NULL;

        memcpy(png_path, dot_path, prefix_len);
        memcpy(png_path + prefix_len, ".png", 5);
        return png_path;
    }

    char* png_path = (char*)calloc(dot_len + 5, sizeof(char));
    if (png_path == NULL)
        return NULL;

    memcpy(png_path, dot_path, dot_len);
    memcpy(png_path + dot_len, ".png", 5);
    return png_path;
}

static char* shell_escape_double_quoted(const char* text)
{
    assert(text != NULL);

    size_t extra = 0;
    for (const char* cur = text; *cur != '\0'; ++cur)
    {
        if (*cur == '\\' || *cur == '"' || *cur == '$' || *cur == '`')
            ++extra;
    }

    const size_t len = strlen(text);
    char* escaped = (char*)calloc(len + extra + 1, sizeof(char));
    if (escaped == NULL)
        return NULL;

    char* out = escaped;
    for (const char* cur = text; *cur != '\0'; ++cur)
    {
        if (*cur == '\\' || *cur == '"' || *cur == '$' || *cur == '`')
            *out++ = '\\';
        *out++ = *cur;
    }
    *out = '\0';

    return escaped;
}

static bool tree_generate_png_from_dot(const char* dot_path)
{
    assert(dot_path != NULL);

    char* png_path = build_png_path(dot_path);
    if (png_path == NULL)
        return false;

    char* escaped_dot = shell_escape_double_quoted(dot_path);
    char* escaped_png = shell_escape_double_quoted(png_path);
    if (escaped_dot == NULL || escaped_png == NULL)
    {
        free(escaped_dot);
        free(escaped_png);
        free(png_path);
        return false;
    }

    const size_t command_len = strlen(escaped_dot) + strlen(escaped_png) + 32;
    char* command = (char*)calloc(command_len, sizeof(char));
    if (command == NULL)
    {
        free(escaped_dot);
        free(escaped_png);
        free(png_path);
        return false;
    }

    snprintf(command, command_len, "dot -Tpng \"%s\" -o \"%s\"", escaped_dot, escaped_png);
    const int exit_code = system(command);

    free(command);
    free(escaped_dot);
    free(escaped_png);
    free(png_path);

    return exit_code == 0;
}

const char* node_type_name(enum node_type type)
{
    switch (type)
    {
        case NODE_NONE: return "NONE";
        case NODE_OPER: return "OPER";
        case NODE_NUM:  return "NUM";
        case NODE_ID:   return "ID";
        case NODE_VAR:  return "VAR";
        case NODE_FUNC: return "FUNC";
        case NODE_TEXT: return "TEXT";
        case NODE_GLUE: return "GLUE";
        default:        return "UNKNOWN";
    }
}

const char* node_data_type_name(enum node_data_type type)
{
    switch (type)
    {
        case DATA_NONE:   return "NONE";
        case DATA_INT:    return "INT";
        case DATA_OPER:   return "OPER";
        case DATA_STRING: return "STRING";
        default:          return "UNKNOWN";
    }
}

node_t* node_create_num(int value, int line, int column)
{
    node_t* node = node_create_empty(NODE_NUM, DATA_INT, line, column);
    if (node == NULL)
        return NULL;

    node->data.number = value;
    return node;
}

node_t* node_create_oper(enum opers oper, int line, int column)
{
    node_t* node = node_create_empty(NODE_OPER, DATA_OPER, line, column);
    if (node == NULL)
        return NULL;

    node->data.oper = oper;
    return node;
}

node_t* node_create_id(const char* name, int line, int column)
{
    return node_create_string(NODE_ID, name, line, column);
}

node_t* node_create_var(const char* name, int line, int column)
{
    return node_create_string(NODE_VAR, name, line, column);
}

node_t* node_create_func(const char* name, int line, int column)
{
    return node_create_string(NODE_FUNC, name, line, column);
}

node_t* node_create_text(const char* text, int line, int column)
{
    return node_create_string(NODE_TEXT, text, line, column);
}

node_t* node_create_glue(int line, int column)
{
    return node_create_empty(NODE_GLUE, DATA_NONE, line, column);
}

bool node_promote_id(node_t* node, enum node_type target_type)
{
    if (node == NULL)
        return false;

    if (node->type != NODE_ID || node->data_type != DATA_STRING)
        return false;

    if (target_type != NODE_VAR && target_type != NODE_FUNC && target_type != NODE_TEXT)
        return false;

    node->type = target_type;
    return true;
}

void node_set_left(node_t* parent, node_t* child)
{
    if (parent == NULL)
        return;

    if (parent->left != NULL)
        parent->left->parent = NULL;

    if (child != NULL)
        node_disconnect_from_parent(child);

    parent->left = child;
    if (child != NULL)
        child->parent = parent;
}

void node_set_right(node_t* parent, node_t* child)
{
    if (parent == NULL)
        return;

    if (parent->right != NULL)
        parent->right->parent = NULL;

    if (child != NULL)
        node_disconnect_from_parent(child);

    parent->right = child;
    if (child != NULL)
        child->parent = parent;
}

node_t* node_detach_left(node_t* parent)
{
    if (parent == NULL)
        return NULL;

    node_t* detached = parent->left;
    if (detached != NULL)
        detached->parent = NULL;

    parent->left = NULL;
    return detached;
}

node_t* node_detach_right(node_t* parent)
{
    if (parent == NULL)
        return NULL;

    node_t* detached = parent->right;
    if (detached != NULL)
        detached->parent = NULL;

    parent->right = NULL;
    return detached;
}

node_t* node_clone(const node_t* root)
{
    if (root == NULL)
        return NULL;

    node_t* copy = NULL;

    switch (root->type)
    {
        case NODE_NUM:
            copy = node_create_num(root->data.number, root->line, root->column);
            break;

        case NODE_OPER:
            copy = node_create_oper(root->data.oper, root->line, root->column);
            break;

        case NODE_ID:
            copy = node_create_id(root->data.string, root->line, root->column);
            break;

        case NODE_VAR:
            copy = node_create_var(root->data.string, root->line, root->column);
            break;

        case NODE_FUNC:
            copy = node_create_func(root->data.string, root->line, root->column);
            break;

        case NODE_TEXT:
            copy = node_create_text(root->data.string, root->line, root->column);
            break;

        case NODE_GLUE:
            copy = node_create_glue(root->line, root->column);
            break;

        case NODE_NONE:
        default:
            copy = node_create_empty(root->type, root->data_type, root->line, root->column);
            break;
    }

    if (copy == NULL)
        return NULL;

    node_t* left_copy = node_clone(root->left);
    if (root->left != NULL && left_copy == NULL)
    {
        tree_dtor(copy);
        return NULL;
    }

    node_t* right_copy = node_clone(root->right);
    if (root->right != NULL && right_copy == NULL)
    {
        tree_dtor(left_copy);
        tree_dtor(copy);
        return NULL;
    }

    node_set_left(copy, left_copy);
    node_set_right(copy, right_copy);
    return copy;
}

bool node_verify(const node_t* root)
{
    if (root == NULL)
        return true;

    switch (root->type)
    {
        case NODE_NUM:
            if (root->data_type != DATA_INT)
                return false;
            break;

        case NODE_OPER:
            if (root->data_type != DATA_OPER)
                return false;
            if (!oper_is_indexed(root->data.oper))
                return false;
            break;

        case NODE_ID:
        case NODE_VAR:
        case NODE_FUNC:
        case NODE_TEXT:
            if (root->data_type != DATA_STRING)
                return false;
            break;

        case NODE_GLUE:
            if (root->data_type != DATA_NONE)
                return false;
            break;

        case NODE_NONE:
        default:
            break;
    }

    if (root->left != NULL && root->left->parent != root)
        return false;

    if (root->right != NULL && root->right->parent != root)
        return false;

    return node_verify(root->left) && node_verify(root->right);
}

void tree_dtor(node_t* root)
{
    if (root == NULL)
        return;

    tree_dtor(root->left);
    tree_dtor(root->right);

    if (root->data_type == DATA_STRING)
        free(root->data.string);

    free(root);
}

bool tree_dump_dot(const node_t* root, const char* dot_path)
{
    if (dot_path == NULL)
        return false;

    FILE* file = fopen(dot_path, "w");
    if (file == NULL)
        return false;

    fputs("digraph Tree {\n", file);
    fputs("    rankdir=TB;\n", file);
    fputs("    node [shape=Mrecord];\n", file);

    if (root == NULL)
        fputs("    empty [label=\"\\<empty tree\\>\"];\n", file);
    else
        tree_dump_dot_node(root, file);

    fputs("}\n", file);

    const bool ok = !ferror(file);
    fclose(file);

    if (!ok)
        return false;

    return tree_generate_png_from_dot(dot_path);
}

bool tree_serialize(const node_t* root, FILE* out)
{
    if (out == NULL)
        return false;

    return tree_serialize_node(root, out);
}
