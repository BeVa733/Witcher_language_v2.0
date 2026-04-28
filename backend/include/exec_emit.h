#ifndef EXEC_EMIT_H
#define EXEC_EMIT_H

#include <stdio.h>

bool emit_blank_line(FILE* out);
bool emit_comment(FILE* out, const char* comment);
bool emit_label(FILE* out, const char* label);
bool emit_line(FILE* out, const char* format, ...);

#endif
