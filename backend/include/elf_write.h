#ifndef ELF_WRITE_H
#define ELF_WRITE_H

#include <stddef.h>
#include <stdio.h>

bool write_min_elf64(FILE* out,
                     const unsigned char* payload,
                     size_t payload_size,
                     size_t entry_payload_offset,
                     char* error_text,
                     size_t error_size);

#endif
