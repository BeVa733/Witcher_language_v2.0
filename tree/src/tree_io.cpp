#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tree_io.h"

struct tree_reader
{
    const char* text;
    const char* cur;
    tree_read_result* result;
};

static void tree_read_fail(tree_reader* reader, const char* message)
{
    assert(reader != NULL);
    assert(reader->result != NULL);

    if (reader->result->error_text[0] != '\0')
        return;

    reader->result->error_offset = (int)(reader->cur - reader->text);
    snprintf(reader->result->error_text, TREE_IO_ERROR_TEXT_SIZE, "%s", message);
}

static void skip_spaces(tree_reader* reader)
{
    while (*reader->cur != '\0' && isspace((unsigned char)*reader->cur))
        ++reader->cur;
}

static bool consume_char(tree_reader* reader, char ch)
{
    skip_spaces(reader);
    if (*reader->cur != ch)
        return false;

    ++reader->cur;
    return true;
}

static bool read_until_char(tree_reader* reader, char terminal, char** out_string)
{
    assert(out_string != NULL);

    skip_spaces(reader);

    size_t capacity = 32;
    size_t size = 0;
    char* buffer = (char*)calloc(capacity, sizeof(char));
    if (buffer == NULL)
    {
        tree_read_fail(reader, "out of memory while reading serialized string");
        return false;
    }

    while (*reader->cur != '\0')
    {
        char current = *reader->cur++;
        if (current == terminal)
            break;

        if (current == '\\')
        {
            if (*reader->cur == '\0')
            {
                free(buffer);
                tree_read_fail(reader, "unfinished escape sequence");
                return false;
            }

            char escaped = *reader->cur++;
            switch (escaped)
            {
                case 'n': current = '\n'; break;
                case 'r': current = '\r'; break;
                case 't': current = '\t'; break;
                case '\\': current = '\\'; break;
                case '"': current = '"'; break;
                default: current = escaped; break;
            }
        }

        if (size + 1 >= capacity)
        {
            capacity *= 2;
            char* resized = (char*)realloc(buffer, capacity * sizeof(char));
            if (resized == NULL)
            {
                free(buffer);
                tree_read_fail(reader, "out of memory while growing serialized string buffer");
                return false;
            }
            buffer = resized;
        }

        buffer[size++] = current;
    }

    if (*(reader->cur - 1) != terminal)
    {
        free(buffer);
        tree_read_fail(reader, "unterminated string literal in serialized tree");
        return false;
    }

    buffer[size] = '\0';
    *out_string = buffer;
    return true;
}

static enum opers find_oper_by_serialized_name(const char* serialized_name)
{
    if (serialized_name == NULL)
        return OPER_INVALID;

    for (int oper_index = 0; oper_index < OPER_COUNT; ++oper_index)
    {
        enum opers oper = (enum opers)oper_index;
        const lexeme_info* info = lexeme_info_by_oper(oper);
        if (info != NULL && info->serialized_name != NULL && strcmp(info->serialized_name, serialized_name) == 0)
            return oper;
    }

    return OPER_INVALID;
}

static node_t* parse_node(tree_reader* reader)
{
    skip_spaces(reader);

    if (strncmp(reader->cur, "nil", 3) == 0)
    {
        reader->cur += 3;
        return NULL;
    }

    if (!consume_char(reader, '('))
    {
        tree_read_fail(reader, "expected '(' or 'nil' in serialized tree");
        return NULL;
    }

    char* label = NULL;
    if (!read_until_char(reader, ':', &label))
        return NULL;

    if (!consume_char(reader, '"'))
    {
        free(label);
        tree_read_fail(reader, "expected \" after node label");
        return NULL;
    }

    char* value = NULL;
    if (!read_until_char(reader, '"', &value))
    {
        free(label);
        return NULL;
    }

    node_t* node = NULL;

    if (strcmp(label, "oper") == 0)
    {
        enum opers oper = find_oper_by_serialized_name(value);
        if (oper == OPER_INVALID)
        {
            free(label);
            free(value);
            tree_read_fail(reader, "unknown serialized operation name");
            return NULL;
        }
        node = node_create_oper(oper, 0, 0);
    }
    else if (strcmp(label, "num") == 0)
    {
        char* end_ptr = NULL;
        long parsed = strtol(value, &end_ptr, 10);
        if (end_ptr == NULL || *end_ptr != '\0')
        {
            free(label);
            free(value);
            tree_read_fail(reader, "invalid integer literal in serialized tree");
            return NULL;
        }
        node = node_create_num((int)parsed, 0, 0);
    }
    else if (strcmp(label, "id") == 0)
    {
        node = node_create_id(value, 0, 0);
    }
    else if (strcmp(label, "var") == 0)
    {
        node = node_create_var(value, 0, 0);
    }
    else if (strcmp(label, "func") == 0)
    {
        node = node_create_func(value, 0, 0);
    }
    else if (strcmp(label, "text") == 0)
    {
        node = node_create_text(value, 0, 0);
    }
    else if (strcmp(label, "glue") == 0)
    {
        node = node_create_glue(0, 0);
    }
    else
    {
        free(label);
        free(value);
        tree_read_fail(reader, "unknown node label in serialized tree");
        return NULL;
    }

    free(label);
    free(value);

    if (node == NULL)
    {
        tree_read_fail(reader, "failed to allocate node while reading serialized tree");
        return NULL;
    }

    node_t* left = parse_node(reader);
    if (reader->result->error_text[0] != '\0')
    {
        tree_dtor(node);
        return NULL;
    }
    node_set_left(node, left);

    node_t* right = parse_node(reader);
    if (reader->result->error_text[0] != '\0')
    {
        tree_dtor(node);
        return NULL;
    }
    node_set_right(node, right);

    if (!consume_char(reader, ')'))
    {
        tree_dtor(node);
        tree_read_fail(reader, "expected ')' after serialized node");
        return NULL;
    }

    return node;
}

void tree_read_result_ctor(tree_read_result* result)
{
    assert(result != NULL);

    result->root = NULL;
    result->error_offset = -1;
    result->error_text[0] = '\0';
}

void tree_read_result_reset(tree_read_result* result)
{
    assert(result != NULL);

    if (result->root != NULL)
        tree_dtor(result->root);

    tree_read_result_ctor(result);
}

bool tree_read_from_text(const char* text, tree_read_result* result)
{
    assert(text != NULL);
    assert(result != NULL);

    tree_read_result_reset(result);

    tree_reader reader = {};
    reader.text = text;
    reader.cur = text;
    reader.result = result;

    result->root = parse_node(&reader);
    if (result->error_text[0] != '\0')
    {
        if (result->root != NULL)
        {
            tree_dtor(result->root);
            result->root = NULL;
        }
        return false;
    }

    skip_spaces(&reader);
    if (*reader.cur != '\0')
    {
        tree_read_result_reset(result);
        result->error_offset = (int)(reader.cur - reader.text);
        snprintf(result->error_text, TREE_IO_ERROR_TEXT_SIZE, "unexpected trailing data after serialized tree");
        return false;
    }

    if (result->root != NULL && !node_verify(result->root))
    {
        tree_read_result_reset(result);
        result->error_offset = -1;
        snprintf(result->error_text, TREE_IO_ERROR_TEXT_SIZE, "serialized tree was parsed but failed node_verify()");
        return false;
    }

    return true;
}

bool tree_read_from_file(const char* filename, tree_read_result* result)
{
    assert(filename != NULL);
    assert(result != NULL);

    tree_read_result_reset(result);

    FILE* file = fopen(filename, "rb");
    if (file == NULL)
    {
        result->error_offset = -1;
        snprintf(result->error_text, TREE_IO_ERROR_TEXT_SIZE, "failed to open serialized tree file '%s'", filename);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        result->error_offset = -1;
        snprintf(result->error_text, TREE_IO_ERROR_TEXT_SIZE, "failed to seek serialized tree file '%s'", filename);
        return false;
    }

    long file_size = ftell(file);
    if (file_size < 0)
    {
        fclose(file);
        result->error_offset = -1;
        snprintf(result->error_text, TREE_IO_ERROR_TEXT_SIZE, "failed to determine size of serialized tree file '%s'", filename);
        return false;
    }

    rewind(file);

    char* buffer = (char*)calloc((size_t)file_size + 1, sizeof(char));
    if (buffer == NULL)
    {
        fclose(file);
        result->error_offset = -1;
        snprintf(result->error_text, TREE_IO_ERROR_TEXT_SIZE, "out of memory while reading serialized tree file '%s'", filename);
        return false;
    }

    const size_t read_size = fread(buffer, sizeof(char), (size_t)file_size, file);
    fclose(file);

    buffer[read_size] = '\0';
    const bool parse_ok = tree_read_from_text(buffer, result);
    free(buffer);

    return parse_ok;
}
