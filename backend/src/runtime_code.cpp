#include "runtime_code.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

const char* RUNTIME_ENTRY_LABELS[] =
{
    "WL_RT_EXIT",
    "WL_RT_PRINT_TEXT",
    "WL_RT_PRINT_CHAR",
    "WL_RT_PRINT_NUM",
    "WL_RT_READ_NUM",
};

const int RUNTIME_ENTRY_COUNT = (int)(sizeof(RUNTIME_ENTRY_LABELS) / sizeof(RUNTIME_ENTRY_LABELS[0]));

bool runtime_code_read(const char* file_name, unsigned char** code, size_t* code_size)
{
    assert(file_name);
    assert(code);
    assert(code_size);

    *code = NULL;
    *code_size = 0;

    FILE* file = fopen(file_name, "rb");
    if (!file)
        return false;

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return false;
    }

    const long file_size = ftell(file);
    if (file_size <= 0)
    {
        fclose(file);
        return false;
    }

    rewind(file);

    unsigned char* buffer = (unsigned char*)calloc((size_t)file_size, sizeof(unsigned char));
    if (!buffer)
    {
        fclose(file);
        return false;
    }

    const size_t read_size = fread(buffer, sizeof(unsigned char), (size_t)file_size, file);
    fclose(file);

    if (read_size != (size_t)file_size)
    {
        free(buffer);
        return false;
    }

    *code = buffer;
    *code_size = (size_t)file_size;

    return true;
}
