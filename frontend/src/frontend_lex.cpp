#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "frontend_lex.h"

static void lexer_set_error(lexer_result* result, int line, int column, const char* text)
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

    snprintf(result->error_text, LEXER_ERROR_TEXT_SIZE, "%s", text);
}

static bool lexer_reserve(lexer_result* result, int min_capacity)
{
    if (result == NULL)
        return false;

    if (result->capacity >= min_capacity)
        return true;

    int new_capacity = (result->capacity > 0) ? result->capacity : LEXER_START_CAPACITY;
    while (new_capacity < min_capacity)
        new_capacity *= 2;

    node_t** new_tokens = (node_t**)realloc(result->tokens, (size_t)new_capacity * sizeof(node_t*));
    if (new_tokens == NULL)
        return false;

    result->tokens = new_tokens;
    result->capacity = new_capacity;
    return true;
}

static bool lexer_push_token(lexer_result* result, node_t* token)
{
    if (result == NULL || token == NULL)
        return false;

    if (!lexer_reserve(result, result->token_count + 1))
        return false;

    result->tokens[result->token_count++] = token;
    return true;
}

static bool is_identifier_start_char(unsigned char symbol)
{
    return isalpha(symbol) || symbol == '_' || symbol >= 0x80;
}

static bool is_identifier_char(unsigned char symbol)
{
    return isalnum(symbol) || symbol == '_' || symbol == '?' || symbol >= 0x80;
}

static int cstr_length(const char* text)
{
    if (text == NULL)
        return 0;

    int length = 0;
    while (text[length] != '\0')
        ++length;

    return length;
}

static bool bytes_equal(const char* start, int length, const char* text)
{
    if (start == NULL || text == NULL)
        return false;

    int index = 0;
    while (index < length && text[index] != '\0')
    {
        if (start[index] != text[index])
            return false;
        ++index;
    }

    return index == length && text[index] == '\0';
}

static bool match_lexeme_variant(const char* start, int length, const char* variant)
{
    const int variant_length = cstr_length(variant);
    if (variant_length != length)
        return false;

    return bytes_equal(start, length, variant);
}

static bool match_oper_name(const char* start, int length, enum opers oper)
{
    if (!oper_is_indexed(oper))
        return false;

    const lexeme_info* info = &LEXEME_TABLE[(int)oper];
    return match_lexeme_variant(start, length, info->source_name);
}

static enum opers recognize_word_lexeme(const char* start, int length)
{
    if (start == NULL || length <= 0)
        return OPER_INVALID;

    switch (start[0])
    {
        case 'b':
            if (match_oper_name(start, length, OPER_BREAK))  return OPER_BREAK;
            break;
        case 'e':
            if (match_oper_name(start, length, OPER_ELSE))   return OPER_ELSE;
            break;
        case 'f':
            if (match_oper_name(start, length, OPER_FUNC))   return OPER_FUNC;
            break;
        case 'i':
            if (match_oper_name(start, length, OPER_IF))     return OPER_IF;
            break;
        case 'p':
            if (match_oper_name(start, length, OPER_PRINT))  return OPER_PRINT;
            if (match_oper_name(start, length, OPER_PRINTC)) return OPER_PRINTC;
            break;
        case 'r':
            if (match_oper_name(start, length, OPER_RETURN)) return OPER_RETURN;
            break;
        case 's':
            if (match_oper_name(start, length, OPER_SCAN))   return OPER_SCAN;
            break;
        case 'w':
            if (match_oper_name(start, length, OPER_WHILE))  return OPER_WHILE;
            break;
        default:
            break;
    }

    if (match_oper_name(start, length, OPER_IF))        return OPER_IF;
    if (match_oper_name(start, length, OPER_ELSE))      return OPER_ELSE;
    if (match_oper_name(start, length, OPER_WHILE))     return OPER_WHILE;
    if (match_oper_name(start, length, OPER_RETURN))    return OPER_RETURN;
    if (match_oper_name(start, length, OPER_BREAK))     return OPER_BREAK;
    if (match_oper_name(start, length, OPER_PRINT))     return OPER_PRINT;
    if (match_oper_name(start, length, OPER_PRINTC))    return OPER_PRINTC;
    if (match_oper_name(start, length, OPER_SCAN))      return OPER_SCAN;
    if (match_oper_name(start, length, OPER_FUNC))      return OPER_FUNC;

    if (match_oper_name(start, length, OPER_ASSIGN))    return OPER_ASSIGN;
    if (match_oper_name(start, length, OPER_EQ))        return OPER_EQ;
    if (match_oper_name(start, length, OPER_NEQ))       return OPER_NEQ;
    if (match_oper_name(start, length, OPER_LT))        return OPER_LT;
    if (match_oper_name(start, length, OPER_GT))        return OPER_GT;
    if (match_oper_name(start, length, OPER_LE))        return OPER_LE;
    if (match_oper_name(start, length, OPER_GE))        return OPER_GE;

    if (match_oper_name(start, length, OPER_LBRACE))    return OPER_LBRACE;
    if (match_oper_name(start, length, OPER_RBRACE))    return OPER_RBRACE;
    if (match_oper_name(start, length, OPER_LPAREN))    return OPER_LPAREN;
    if (match_oper_name(start, length, OPER_RPAREN))    return OPER_RPAREN;
    if (match_oper_name(start, length, OPER_COMMA))     return OPER_COMMA;
    if (match_oper_name(start, length, OPER_STMT_SEP))  return OPER_STMT_SEP;

    return OPER_INVALID;
}

static bool scan_number(const char*& current, int& column, int line, lexer_result* result)
{
    const char* start = current;
    char* end = NULL;
    const long value = strtol(start, &end, 10);

    if (end == start)
    {
        lexer_set_error(result, line, column, "failed to read integer literal");
        return false;
    }

    if (value < INT_MIN || value > INT_MAX)
    {
        lexer_set_error(result, line, column, "integer literal is out of int range");
        return false;
    }

    node_t* token = node_create_num((int)value, line, column);
    if (token == NULL)
    {
        lexer_set_error(result, line, column, "failed to allocate number token");
        return false;
    }

    if (!lexer_push_token(result, token))
    {
        tree_dtor(token);
        lexer_set_error(result, line, column, "failed to append number token");
        return false;
    }

    column += (int)(end - start);
    current = end;
    return true;
}

static char* copy_identifier(const char* start, int length)
{
    char* word = (char*)calloc((size_t)length + 1, sizeof(char));
    if (word == NULL)
        return NULL;

    for (int index = 0; index < length; ++index)
        word[index] = start[index];
    word[length] = '\0';
    return word;
}

static bool scan_identifier_or_keyword(const char*& current, int& column, int line, lexer_result* result)
{
    const char* start = current;
    while (is_identifier_char((unsigned char)*current))
        ++current;

    const int length = (int)(current - start);
    const enum opers oper = recognize_word_lexeme(start, length);

    node_t* token = NULL;
    if (oper != OPER_INVALID)
    {
        token = node_create_oper(oper, line, column);
    }
    else
    {
        char* word = copy_identifier(start, length);
        if (word == NULL)
        {
            lexer_set_error(result, line, column, "failed to allocate identifier buffer");
            return false;
        }

        token = node_create_id(word, line, column);
        free(word);
    }

    if (token == NULL)
    {
        lexer_set_error(result, line, column, "failed to allocate identifier token");
        return false;
    }

    if (!lexer_push_token(result, token))
    {
        tree_dtor(token);
        lexer_set_error(result, line, column, "failed to append identifier token");
        return false;
    }

    column += length;
    return true;
}

static bool scan_symbol_token(const char*& current, int& column, int line, lexer_result* result)
{
    enum opers oper = OPER_INVALID;

    if (current[0] == '=' && current[1] == '=')
        oper = OPER_EQ;
    else if (current[0] == '!' && current[1] == '=')
        oper = OPER_NEQ;
    else if (current[0] == '<' && current[1] == '=')
        oper = OPER_LE;
    else if (current[0] == '>' && current[1] == '=')
        oper = OPER_GE;

    if (oper != OPER_INVALID)
    {
        node_t* token = node_create_oper(oper, line, column);
        if (token == NULL)
        {
            lexer_set_error(result, line, column, "failed to allocate operator token");
            return false;
        }

        if (!lexer_push_token(result, token))
        {
            tree_dtor(token);
            lexer_set_error(result, line, column, "failed to append operator token");
            return false;
        }

        current += 2;
        column += 2;
        return true;
    }

    switch (*current)
    {
        case '+': oper = OPER_ADD;      break;
        case '-': oper = OPER_SUB;      break;
        case '*': oper = OPER_MUL;      break;
        case '/': oper = OPER_DIV;      break;
        case '=': oper = OPER_ASSIGN;   break;
        case '<': oper = OPER_LT;       break;
        case '>': oper = OPER_GT;       break;
        case ';': oper = OPER_STMT_SEP; break;
        case '(': oper = OPER_LPAREN;   break;
        case ')': oper = OPER_RPAREN;   break;
        case '{': oper = OPER_LBRACE;   break;
        case '}': oper = OPER_RBRACE;   break;
        case ',': oper = OPER_COMMA;    break;
        default:  oper = OPER_INVALID;  break;
    }

    if (oper == OPER_INVALID)
    {
        char message[LEXER_ERROR_TEXT_SIZE] = "";
        snprintf(message, sizeof(message), "unexpected symbol '%c'", *current);
        lexer_set_error(result, line, column, message);
        return false;
    }

    node_t* token = node_create_oper(oper, line, column);
    if (token == NULL)
    {
        lexer_set_error(result, line, column, "failed to allocate symbol token");
        return false;
    }

    if (!lexer_push_token(result, token))
    {
        tree_dtor(token);
        lexer_set_error(result, line, column, "failed to append symbol token");
        return false;
    }

    ++current;
    ++column;
    return true;
}

void lexer_result_ctor(lexer_result* result)
{
    if (result == NULL)
        return;

    result->tokens = NULL;
    result->token_count = 0;
    result->capacity = 0;
    result->error_line = 0;
    result->error_column = 0;
    result->error_text[0] = '\0';
}

void lexer_result_reset(lexer_result* result)
{
    if (!result)
        return;

    if (result->tokens != NULL)
    {
        for (int index = 0; index < result->token_count; ++index)
            tree_dtor(result->tokens[index]);

        free(result->tokens);
    }

    lexer_result_ctor(result);
}

void lexer_result_release_array(lexer_result* result)
{
    if (!result)
        return;

    free(result->tokens);
    result->tokens = NULL;
    result->token_count = 0;
    result->capacity = 0;
    result->error_line = 0;
    result->error_column = 0;
    result->error_text[0] = '\0';
}

char* read_source_file(const char* file_name)
{
    if (!file_name)
        return NULL;

    FILE* file = fopen(file_name, "rb");
    if (!file)
        return NULL;

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return NULL;
    }

    const long file_size = ftell(file);
    if (file_size < 0)
    {
        fclose(file);
        return NULL;
    }

    rewind(file);

    char* buffer = (char*)calloc((size_t)file_size + 1, sizeof(char));
    if (buffer == NULL)
    {
        fclose(file);
        return NULL;
    }

    const size_t bytes_read = fread(buffer, 1, (size_t)file_size, file);
    buffer[bytes_read] = '\0';

    fclose(file);
    return buffer;
}

bool lex_source(const char* source, lexer_result* result)
{
    if (source == NULL || result == NULL)
        return false;

    lexer_result_reset(result);

    const char* current = source;
    int line = 1;
    int column = 1;

    while (*current != '\0')
    {
        const unsigned char symbol = (unsigned char)*current;

        if (symbol == ' ' || symbol == '\t' || symbol == '\r')
        {
            ++current;
            ++column;
            continue;
        }

        if (symbol == '\n')
        {
            ++current;
            ++line;
            column = 1;
            continue;
        }

        if (isdigit(symbol))
        {
            if (!scan_number(current, column, line, result))
                return false;
            continue;
        }

        if (is_identifier_start_char(symbol))
        {
            if (!scan_identifier_or_keyword(current, column, line, result))
                return false;
            continue;
        }

        if (!scan_symbol_token(current, column, line, result))
            return false;
    }

    return true;
}

bool lex_file(const char* file_name, lexer_result* result)
{
    if (file_name == NULL || result == NULL)
        return false;

    char* source = read_source_file(file_name);
    if (source == NULL)
    {
        lexer_set_error(result, 0, 0, "failed to read input file");
        return false;
    }

    const bool ok = lex_source(source, result);
    free(source);
    return ok;
}

void dump_lexer_tokens(const lexer_result* result, FILE* out)
{
    if (result == NULL || out == NULL)
        return;

    for (int index = 0; index < result->token_count; ++index)
    {
        const node_t* token = result->tokens[index];
        if (token == NULL)
        {
            fprintf(out, "[%d] <null>\n", index);
            continue;
        }

        fprintf(out,
                "[%d] type=%s data_type=%s line=%d column=%d value=",
                index,
                node_type_name(token->type),
                node_data_type_name(token->data_type),
                token->line,
                token->column);

        switch (token->type)
        {
            case NODE_OPER:
                fprintf(out, "%s", oper_debug_name(token->data.oper));
                break;

            case NODE_NUM:
                fprintf(out, "%d", token->data.number);
                break;

            case NODE_ID:
            case NODE_VAR:
            case NODE_FUNC:
            case NODE_TEXT:
                fprintf(out, "%s", token->data.string != NULL ? token->data.string : "<null>");
                break;

            case NODE_GLUE:
                fprintf(out, "glue");
                break;

            case NODE_NONE:
            default:
                fprintf(out, "<none>");
                break;
        }

        fputc('\n', out);
    }
}
