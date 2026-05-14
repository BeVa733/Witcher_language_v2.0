#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "elf_write.h"

const uint64_t ELF_BASE_VADDR = 0x400000;
const uint64_t ELF_LOAD_ALIGN = 0x1000;

const uint16_t ELF_ET_EXEC = 2;
const uint16_t ELF_MACHINE_X64 = 62;
const uint32_t ELF_EV_CURRENT = 1;

const uint32_t ELF_PT_LOAD = 1;
const uint32_t ELF_PF_X = 1;
const uint32_t ELF_PF_R = 4;

struct elf64_ehdr
{
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr
{
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

static void write_fail(char* error_text, size_t error_size, const char* text)
{
    if (!error_text || error_size == 0 || !text)
        return;

    if (error_text[0] == '\0')
        snprintf(error_text, error_size, "%s", text);
}

static void fill_elf_header(elf64_ehdr* header, size_t payload_size, size_t entry_payload_offset)
{
    assert(header);

    memset(header, 0, sizeof(*header));

    header->e_ident[0] = 0x7F;
    header->e_ident[1] = 'E';
    header->e_ident[2] = 'L';
    header->e_ident[3] = 'F';
    header->e_ident[4] = 2;
    header->e_ident[5] = 1;
    header->e_ident[6] = 1;
    header->e_ident[7] = 0;

    const uint64_t payload_file_offset = sizeof(elf64_ehdr) + sizeof(elf64_phdr);

    header->e_type = ELF_ET_EXEC;
    header->e_machine = ELF_MACHINE_X64;
    header->e_version = ELF_EV_CURRENT;
    header->e_entry = ELF_BASE_VADDR + payload_file_offset + entry_payload_offset;
    header->e_phoff = sizeof(elf64_ehdr);
    header->e_shoff = 0;
    header->e_flags = 0;
    header->e_ehsize = sizeof(elf64_ehdr);
    header->e_phentsize = sizeof(elf64_phdr);
    header->e_phnum = 1;
    header->e_shentsize = 0;
    header->e_shnum = 0;
    header->e_shstrndx = 0;

    (void)payload_size;
}

static void fill_program_header(elf64_phdr* header, size_t payload_size)
{
    assert(header);

    memset(header, 0, sizeof(*header));

    const uint64_t file_size = sizeof(elf64_ehdr) + sizeof(elf64_phdr) + payload_size;

    header->p_type = ELF_PT_LOAD;
    header->p_flags = ELF_PF_R | ELF_PF_X;
    header->p_offset = 0;
    header->p_vaddr = ELF_BASE_VADDR;
    header->p_paddr = ELF_BASE_VADDR;
    header->p_filesz = file_size;
    header->p_memsz = file_size;
    header->p_align = ELF_LOAD_ALIGN;
}

bool write_min_elf64(FILE* out, const unsigned char* payload, size_t payload_size, size_t entry_payload_offset, char* error_text, size_t error_size)
{
    if (!out || (!payload && payload_size > 0))
    {
        write_fail(error_text, error_size, "invalid ELF writer arguments");
        return false;
    }

    if (entry_payload_offset >= payload_size)
    {
        write_fail(error_text, error_size, "ELF entry offset is outside payload");
        return false;
    }

    elf64_ehdr elf_header = {};
    elf64_phdr program_header = {};

    fill_elf_header(&elf_header, payload_size, entry_payload_offset);
    fill_program_header(&program_header, payload_size);

    if (fwrite(&elf_header, sizeof(elf_header), 1, out) != 1)
    {
        write_fail(error_text, error_size, "failed to write ELF header");
        return false;
    }

    if (fwrite(&program_header, sizeof(program_header), 1, out) != 1)
    {
        write_fail(error_text, error_size, "failed to write ELF program header");
        return false;
    }

    if (payload_size > 0 && fwrite(payload, payload_size, 1, out) != 1)
    {
        write_fail(error_text, error_size, "failed to write ELF payload");
        return false;
    }

    return true;
}
