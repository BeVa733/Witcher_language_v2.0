#ifndef LEXEMES_H
#define LEXEMES_H

#include <string.h>

enum opers
{
    OPER_INVALID = -2,
    OPER_EOF     = -1,

    OPER_NONE    = 0,

    OPER_ADD     = 1,
    OPER_SUB     = 2,
    OPER_MUL     = 3,
    OPER_DIV     = 4,
    OPER_NEG     = 5,
    OPER_POS     = 6,

    OPER_ASSIGN  = 7,

    OPER_EQ      = 8,
    OPER_NEQ     = 9,
    OPER_LT      = 10,
    OPER_GT      = 11,
    OPER_LE      = 12,
    OPER_GE      = 13,

    OPER_IF      = 14,
    OPER_ELSE    = 15,
    OPER_WHILE   = 16,
    OPER_RETURN  = 17,
    OPER_BREAK   = 18,

    OPER_PRINT   = 19,
    OPER_PRINTC  = 20,
    OPER_SCAN    = 21,

    OPER_FUNC     = 22,
    OPER_STMT_SEP = 23,

    OPER_LPAREN  = 24,
    OPER_RPAREN  = 25,
    OPER_LBRACE  = 26,
    OPER_RBRACE  = 27,
    OPER_COMMA   = 28,

    OPER_COUNT   = 29
};

enum lexeme_class
{
    LEXEME_CLASS_INVALID   = 0,
    LEXEME_CLASS_OPERATOR  = 1,
    LEXEME_CLASS_KEYWORD   = 2,
    LEXEME_CLASS_PUNCT     = 3,
    LEXEME_CLASS_SYNTHETIC = 4
};

struct lexeme_info
{
    const char* source_name;
    const char* debug_name;
    const char* serialized_name;
    enum lexeme_class lexeme_class;
};

const static lexeme_info LEXEME_TABLE[OPER_COUNT] =
{
    {"NONE",                          "<none>", "NONE",   LEXEME_CLASS_SYNTHETIC},
    {"+",                             "+",      "ADD",    LEXEME_CLASS_OPERATOR},
    {"-",                             "-",      "SUB",    LEXEME_CLASS_OPERATOR},
    {"*",                             "*",      "MUL",    LEXEME_CLASS_OPERATOR},
    {"/",                             "/",      "DIV",    LEXEME_CLASS_OPERATOR},
    {"NEG",                           "-",      "NEG",    LEXEME_CLASS_SYNTHETIC},
    {"POS",                           "+",      "POS",    LEXEME_CLASS_SYNTHETIC},
    {"заплатить_чеканной_монетой",    "=",      "=",      LEXEME_CLASS_OPERATOR},
    {"сыграем_в_Гвинт",               "==",     "==",     LEXEME_CLASS_OPERATOR},
    {"проиграл_в_Гвинт",              "!=",     "!=",     LEXEME_CLASS_OPERATOR},
    {"лень_делает_счастливым",        "<",      "<",      LEXEME_CLASS_OPERATOR},
    {"труд_облагораживает",           ">",      ">",      LEXEME_CLASS_OPERATOR},
    {"<=",                            "<=",     "<=",     LEXEME_CLASS_OPERATOR},
    {">=",                            ">=",     ">=",     LEXEME_CLASS_OPERATOR},
    {"обсудим_цену",                  "if",     "IF",     LEXEME_CLASS_KEYWORD},
    {"никак_вы_блять_не_научитесь",   "else",   "ELSE",   LEXEME_CLASS_KEYWORD},
    {"я_тут_осмотрюсь_пока",          "while",  "WHILE",  LEXEME_CLASS_KEYWORD},
    {"разойдись_свинопасы",           "return", "RETURN", LEXEME_CLASS_KEYWORD},
    {"хочешь_меня_нахер_послать?",    "break",  "BREAK",  LEXEME_CLASS_KEYWORD},
    {"Ламберт_Ламберт_хер_моржовый",  "print",  "PRINT",  LEXEME_CLASS_KEYWORD},
    {"Ламберт_Ламберт_вредный_хуй",   "printc", "PRINTC", LEXEME_CLASS_KEYWORD},
    {"аксий",                         "scan",   "SCAN",   LEXEME_CLASS_KEYWORD},
    {"заказ",                         "func",   "FUNC",   LEXEME_CLASS_KEYWORD},
    {";",                             ";",      ";",      LEXEME_CLASS_PUNCT},
    {"(",                             "(",      "(",      LEXEME_CLASS_PUNCT},
    {")",                             ")",      ")",      LEXEME_CLASS_PUNCT},
    {"ненавижу_порталлы",             "{",      "{",      LEXEME_CLASS_PUNCT},
    {"брр_ненавижу_порталлы",         "}",      "}",      LEXEME_CLASS_PUNCT},
    {",",                             ",",      ",",      LEXEME_CLASS_PUNCT}
};

inline bool oper_is_indexed(enum opers oper)
{
    return ((int)oper >= 0) && ((int)oper < OPER_COUNT);
}

inline const lexeme_info* lexeme_info_by_oper(enum opers oper)
{
    return oper_is_indexed(oper) ? &LEXEME_TABLE[(int)oper] : NULL;
}

inline const lexeme_info* lexeme_info_by_source_name(const char* source_name)
{
    if (source_name == NULL)
        return NULL;

    for (int oper = 0; oper < OPER_COUNT; ++oper)
    {
        const lexeme_info* info = &LEXEME_TABLE[oper];
        if (info->source_name != NULL && strcmp(info->source_name, source_name) == 0)
            return info;
    }

    return NULL;
}

inline const char* oper_debug_name(enum opers oper)
{
    const lexeme_info* info = lexeme_info_by_oper(oper);
    return (info != NULL && info->debug_name != NULL) ? info->debug_name : "<unknown-oper>";
}

inline const char* oper_serialized_name(enum opers oper)
{
    const lexeme_info* info = lexeme_info_by_oper(oper);
    return (info != NULL && info->serialized_name != NULL) ? info->serialized_name : "<unknown-oper>";
}

#endif
