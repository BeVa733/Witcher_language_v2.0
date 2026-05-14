#ifndef RUNTIME_CODE_H
#define RUNTIME_CODE_H

#include <stddef.h>

const char RUNTIME_CODE_FILE[] = "backend/runtime/runtime_syscalls.bin";
const size_t RUNTIME_TRAMPOLINE_SIZE = 8;

extern const char* RUNTIME_ENTRY_LABELS[];
extern const int RUNTIME_ENTRY_COUNT;

bool runtime_code_read(const char* file_name, unsigned char** code, size_t* code_size);

#endif
