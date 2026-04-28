#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include "exec_emit.h"

bool emit_blank_line(FILE* out)
{
    assert(out != NULL);

    return fprintf(out, "\n") >= 0;
}

bool emit_comment(FILE* out, const char* comment)
{
    assert(out != NULL);
    assert(comment != NULL);

    return fprintf(out, "; %s\n", comment) >= 0;
}

bool emit_label(FILE* out, const char* label)
{
    assert(out != NULL);
    assert(label != NULL);

    return fprintf(out, "%s:\n", label) >= 0;
}

bool emit_line(FILE* out, const char* format, ...)
{
    assert(out != NULL);
    assert(format != NULL);

    va_list args = {};
    va_start(args, format);
    const int written = vfprintf(out, format, args);
    va_end(args);

    if (written < 0)
        return false;

    return fprintf(out, "\n") >= 0;
}
