#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exec_emit.h"

const size_t BUFFER_START_CAPACITY = 256;
const size_t LABEL_START_CAPACITY  = 64;
const size_t FIXUP_START_CAPACITY  = 64;

struct byte_buffer
{
    unsigned char* data;
    size_t size;
    size_t capacity;
};

struct label_entry
{
    char* name;
    size_t offset;
    bool is_defined;
};

struct fixup_entry
{
    int label_index;
    size_t patch_offset;
    size_t next_instruction_offset;
};

struct bin_data
{
    byte_buffer code;

    label_entry* labels;
    int label_count;
    int label_capacity;

    fixup_entry* fixups;
    int fixup_count;
    int fixup_capacity;
};

static void bin_fail(emit_context* emit, const char* text)
{
    if (!emit || !text)
        return;

    if (emit->error_text[0] == '\0')
        snprintf(emit->error_text, EMIT_ERROR_TEXT_SIZE, "%s", text);
}

static bin_data* get_bin_data(emit_context* emit)
{
    assert(emit);
    assert(emit->data);
    return (bin_data*)emit->data;
}

static const bin_data* get_const_bin_data(const emit_context* emit)
{
    assert(emit);
    assert(emit->data);
    return (const bin_data*)emit->data;
}

static char* str_dup_local(const char* text)
{
    assert(text);

    const size_t length = strlen(text);
    char* copy = (char*)calloc(length + 1, sizeof(*copy));
    if (!copy)
        return NULL;

    memcpy(copy, text, length + 1);
    return copy;
}

static bool buffer_reserve(byte_buffer* buffer, size_t extra_size)
{
    assert(buffer);

    if (extra_size <= buffer->capacity - buffer->size)
        return true;

    size_t new_capacity = buffer->capacity;
    if (new_capacity == 0)
        new_capacity = BUFFER_START_CAPACITY;

    while (extra_size > new_capacity - buffer->size)
        new_capacity *= 2;

    unsigned char* new_data = (unsigned char*)realloc(buffer->data, new_capacity * sizeof(*new_data));
    if (!new_data)
        return false;

    buffer->data = new_data;
    buffer->capacity = new_capacity;
    return true;
}

static bool buffer_u8(byte_buffer* buffer, uint8_t value)
{
    assert(buffer);

    if (!buffer_reserve(buffer, 1))
        return false;

    buffer->data[buffer->size] = value;
    buffer->size += 1;
    return true;
}

static bool buffer_u32(byte_buffer* buffer, uint32_t value)
{
    assert(buffer);

    if (!buffer_reserve(buffer, 4))
        return false;

    for (int byte_index = 0; byte_index < 4; ++byte_index)
    {
        buffer->data[buffer->size + byte_index] = (unsigned char)((value >> (byte_index * 8)) & 0xFF);
    }

    buffer->size += 4;
    return true;
}

static bool buffer_u64(byte_buffer* buffer, uint64_t value)
{
    assert(buffer);

    if (!buffer_reserve(buffer, 8))
        return false;

    for (int byte_index = 0; byte_index < 8; ++byte_index)
    {
        buffer->data[buffer->size + byte_index] = (unsigned char)((value >> (byte_index * 8)) & 0xFF);
    }

    buffer->size += 8;
    return true;
}

static bool buffer_i8(byte_buffer* buffer, int8_t value)
{
    return buffer_u8(buffer, (uint8_t)value);
}

static bool buffer_i32(byte_buffer* buffer, int32_t value)
{
    return buffer_u32(buffer, (uint32_t)value);
}

static bool buffer_patch_i32(byte_buffer* buffer, size_t offset, int32_t value)
{
    assert(buffer);

    if (offset > buffer->size || buffer->size - offset < 4)
        return false;

    for (int byte_index = 0; byte_index < 4; ++byte_index)
    {
        buffer->data[offset + byte_index] = (unsigned char)(((uint32_t)value >> (byte_index * 8)) & 0xFF);
    }

    return true;
}

static bool ensure_label_capacity(bin_data* data)
{
    assert(data);

    if (data->label_count < data->label_capacity)
        return true;

    int new_capacity = data->label_capacity;
    if (new_capacity == 0)
        new_capacity = (int)LABEL_START_CAPACITY;
    else
        new_capacity *= 2;

    label_entry* new_labels = (label_entry*)realloc(data->labels, (size_t)new_capacity * sizeof(*new_labels));
    if (!new_labels)
        return false;

    for (int index = data->label_capacity; index < new_capacity; ++index)
    {
        new_labels[index].name = NULL;
        new_labels[index].offset = 0;
        new_labels[index].is_defined = false;
    }

    data->labels = new_labels;
    data->label_capacity = new_capacity;
    return true;
}

static bool ensure_fixup_capacity(bin_data* data)
{
    assert(data);

    if (data->fixup_count < data->fixup_capacity)
        return true;

    int new_capacity = data->fixup_capacity;
    if (new_capacity == 0)
        new_capacity = (int)FIXUP_START_CAPACITY;
    else
        new_capacity *= 2;

    fixup_entry* new_fixups = (fixup_entry*)realloc(data->fixups, (size_t)new_capacity * sizeof(*new_fixups));
    if (!new_fixups)
        return false;

    data->fixups = new_fixups;
    data->fixup_capacity = new_capacity;
    return true;
}

static int find_label_index(const bin_data* data, const char* label)
{
    assert(data);
    assert(label);

    for (int index = 0; index < data->label_count; ++index)
    {
        assert(data->labels[index].name);
        if (strcmp(data->labels[index].name, label) == 0)
            return index;
    }

    return -1;
}

static int get_or_add_label(emit_context* emit, const char* label)
{
    assert(emit);
    assert(label);

    bin_data* data = get_bin_data(emit);

    const int existing_index = find_label_index(data, label);
    if (existing_index >= 0)
        return existing_index;

    if (!ensure_label_capacity(data))
    {
        bin_fail(emit, "failed to allocate label table");
        return -1;
    }

    const int label_index = data->label_count;
    data->labels[label_index].name = str_dup_local(label);
    if (!data->labels[label_index].name)
    {
        bin_fail(emit, "failed to copy label name");
        return -1;
    }

    data->labels[label_index].offset = 0;
    data->labels[label_index].is_defined = false;
    data->label_count += 1;
    return label_index;
}

static bool add_fixup(emit_context* emit, const char* label, size_t patch_offset, size_t next_instruction_offset)
{
    assert(emit);
    assert(label);

    bin_data* data = get_bin_data(emit);
    const int label_index = get_or_add_label(emit, label);
    if (label_index < 0)
        return false;

    if (!ensure_fixup_capacity(data))
    {
        bin_fail(emit, "failed to allocate fixup table");
        return false;
    }

    fixup_entry* fixup = &data->fixups[data->fixup_count];
    fixup->label_index = label_index;
    fixup->patch_offset = patch_offset;
    fixup->next_instruction_offset = next_instruction_offset;
    data->fixup_count += 1;
    return true;
}

static bool fits_i8(int32_t value)
{
    return INT8_MIN <= value && value <= INT8_MAX;
}

static uint8_t low_reg(reg id)
{
    return (uint8_t)((int)id & 7);
}

static uint8_t high_reg(reg id)
{
    return (uint8_t)(((int)id >> 3) & 1);
}

static bool emit_rex(byte_buffer* code, bool w, reg reg_field, reg rm_field)
{
    assert(code);

    uint8_t rex = 0x40;
    if (w)
        rex |= 0x08;
    if (high_reg(reg_field))
        rex |= 0x04;
    if (high_reg(rm_field))
        rex |= 0x01;

    if (rex == 0x40)
        return true;

    return buffer_u8(code, rex);
}

static bool emit_rex_force(byte_buffer* code, bool w, reg reg_field, reg rm_field)
{
    assert(code);

    uint8_t rex = 0x40;
    if (w)
        rex |= 0x08;
    if (high_reg(reg_field))
        rex |= 0x04;
    if (high_reg(rm_field))
        rex |= 0x01;

    return buffer_u8(code, rex);
}

static uint8_t modrm(uint8_t mod, uint8_t reg_bits, uint8_t rm_bits)
{
    return (uint8_t)(((mod & 3) << 6) | ((reg_bits & 7) << 3) | (rm_bits & 7));
}

static bool emit_modrm_reg_reg(byte_buffer* code, uint8_t opcode, reg rm, reg reg_field)
{
    assert(code);

    return emit_rex_force(code, true, reg_field, rm) &&
           buffer_u8(code, opcode) &&
           buffer_u8(code, modrm(3, low_reg(reg_field), low_reg(rm)));
}

static bool emit_modrm_ext_reg(byte_buffer* code, uint8_t opcode, uint8_t extension, reg rm)
{
    assert(code);

    return emit_rex_force(code, true, REG_RAX, rm) &&
           buffer_u8(code, opcode) &&
           buffer_u8(code, modrm(3, extension, low_reg(rm)));
}

static bool emit_rbp_memory_modrm(byte_buffer* code, reg reg_field, int32_t rbp_disp)
{
    assert(code);

    const uint8_t mod = fits_i8(rbp_disp) ? 1 : 2;

    if (!buffer_u8(code, modrm(mod, low_reg(reg_field), 5)))
        return false;

    if (mod == 1)
        return buffer_i8(code, (int8_t)rbp_disp);

    return buffer_i32(code, rbp_disp);
}

static bool cond_opcode(cond_code condition, uint8_t* opcode)
{
    assert(opcode);

    switch (condition)
    {
        case COND_E:
        case COND_Z:
            *opcode = 0x04;
            return true;

        case COND_NE:
        case COND_NZ:
            *opcode = 0x05;
            return true;

        case COND_L:
            *opcode = 0x0C;
            return true;

        case COND_GE:
            *opcode = 0x0D;
            return true;

        case COND_LE:
            *opcode = 0x0E;
            return true;

        case COND_G:
            *opcode = 0x0F;
            return true;

        default:
            return false;
    }
}

static bool emit_bin_blank_line(emit_context*)
{
    return true;
}

static bool emit_bin_comment(emit_context*, const char*)
{
    return true;
}

static bool emit_bin_label(emit_context* emit, const char* label)
{
    assert(emit);
    assert(label);

    bin_data* data = get_bin_data(emit);
    const int label_index = get_or_add_label(emit, label);
    if (label_index < 0)
        return false;

    if (data->labels[label_index].is_defined)
    {
        bin_fail(emit, "label is already defined");
        return false;
    }

    data->labels[label_index].offset = data->code.size;
    data->labels[label_index].is_defined = true;
    return true;
}

static bool emit_bin_raw_line(emit_context* emit, const char*, va_list)
{
    assert(emit);

    bin_fail(emit, "raw assembly line is not supported by binary emitter");
    return false;
}

static bool emit_bin_bytes(emit_context* emit, const void* bytes, size_t size)
{
    assert(emit);
    assert(bytes || size == 0);

    byte_buffer* code = &get_bin_data(emit)->code;
    if (!buffer_reserve(code, size))
    {
        bin_fail(emit, "failed to append raw bytes");
        return false;
    }

    if (size > 0)
        memcpy(code->data + code->size, bytes, size);

    code->size += size;
    return true;
}

static bool emit_bin_align(emit_context* emit, size_t alignment, unsigned char value)
{
    assert(emit);

    if (alignment <= 1)
        return true;

    byte_buffer* code = &get_bin_data(emit)->code;
    const size_t misalignment = code->size % alignment;
    if (misalignment == 0)
        return true;

    const size_t padding = alignment - misalignment;
    if (!buffer_reserve(code, padding))
    {
        bin_fail(emit, "failed to append alignment padding");
        return false;
    }

    memset(code->data + code->size, value, padding);
    code->size += padding;
    return true;
}

static bool emit_bin_mov_reg_imm(emit_context* emit, reg dst, int64_t imm)
{
    assert(emit);

    byte_buffer* code = &get_bin_data(emit)->code;
    return emit_rex_force(code, true, REG_RAX, dst) &&
           buffer_u8(code, (uint8_t)(0xB8 + low_reg(dst))) &&
           buffer_u64(code, (uint64_t)imm);
}

static bool emit_bin_mov_reg_reg(emit_context* emit, reg dst, reg src)
{
    assert(emit);
    return emit_modrm_reg_reg(&get_bin_data(emit)->code, 0x89, dst, src);
}

static bool emit_bin_mov_reg_rbp_rel(emit_context* emit, reg dst, int32_t rbp_disp)
{
    assert(emit);

    byte_buffer* code = &get_bin_data(emit)->code;
    return emit_rex_force(code, true, dst, REG_RBP) &&
           buffer_u8(code, 0x8B) &&
           emit_rbp_memory_modrm(code, dst, rbp_disp);
}

static bool emit_bin_mov_rbp_rel_reg(emit_context* emit, int32_t rbp_disp, reg src)
{
    assert(emit);

    byte_buffer* code = &get_bin_data(emit)->code;
    return emit_rex_force(code, true, src, REG_RBP) &&
           buffer_u8(code, 0x89) &&
           emit_rbp_memory_modrm(code, src, rbp_disp);
}

static bool emit_bin_lea_reg_label(emit_context* emit, reg dst, const char* label)
{
    assert(emit);
    assert(label);

    byte_buffer* code = &get_bin_data(emit)->code;

    if (!emit_rex_force(code, true, dst, REG_RBP) ||
        !buffer_u8(code, 0x8D) ||
        !buffer_u8(code, modrm(0, low_reg(dst), 5)))
    {
        return false;
    }

    const size_t patch_offset = code->size;
    if (!buffer_i32(code, 0))
        return false;

    return add_fixup(emit, label, patch_offset, code->size);
}

static bool emit_bin_push_reg(emit_context* emit, reg src)
{
    assert(emit);

    byte_buffer* code = &get_bin_data(emit)->code;
    if (high_reg(src) && !buffer_u8(code, 0x41))
        return false;

    return buffer_u8(code, (uint8_t)(0x50 + low_reg(src)));
}

static bool emit_bin_pop_reg(emit_context* emit, reg dst)
{
    assert(emit);

    byte_buffer* code = &get_bin_data(emit)->code;
    if (high_reg(dst) && !buffer_u8(code, 0x41))
        return false;

    return buffer_u8(code, (uint8_t)(0x58 + low_reg(dst)));
}

static bool emit_bin_add_reg_imm(emit_context* emit, reg dst, int32_t imm)
{
    assert(emit);

    byte_buffer* code = &get_bin_data(emit)->code;
    return emit_rex_force(code, true, REG_RAX, dst) &&
           buffer_u8(code, 0x81) &&
           buffer_u8(code, modrm(3, 0, low_reg(dst))) &&
           buffer_i32(code, imm);
}

static bool emit_bin_sub_reg_imm(emit_context* emit, reg dst, int32_t imm)
{
    assert(emit);

    byte_buffer* code = &get_bin_data(emit)->code;
    return emit_rex_force(code, true, REG_RAX, dst) &&
           buffer_u8(code, 0x81) &&
           buffer_u8(code, modrm(3, 5, low_reg(dst))) &&
           buffer_i32(code, imm);
}

static bool emit_bin_bin_reg_reg(emit_context* emit, bin_op operation, reg dst, reg src)
{
    assert(emit);

    byte_buffer* code = &get_bin_data(emit)->code;

    switch (operation)
    {
        case BIN_OP_ADD:
            return emit_modrm_reg_reg(code, 0x01, dst, src);

        case BIN_OP_SUB:
            return emit_modrm_reg_reg(code, 0x29, dst, src);

        case BIN_OP_IMUL:
            return emit_rex_force(code, true, dst, src) &&
                   buffer_u8(code, 0x0F) &&
                   buffer_u8(code, 0xAF) &&
                   buffer_u8(code, modrm(3, low_reg(dst), low_reg(src)));

        default:
            bin_fail(emit, "unknown binary register operation");
            return false;
    }
}

static bool emit_bin_cmp_reg_reg(emit_context* emit, reg left, reg right)
{
    assert(emit);
    return emit_modrm_reg_reg(&get_bin_data(emit)->code, 0x39, left, right);
}

static bool emit_bin_test_reg_reg(emit_context* emit, reg left, reg right)
{
    assert(emit);
    return emit_modrm_reg_reg(&get_bin_data(emit)->code, 0x85, left, right);
}

static bool emit_bin_setcc_reg(emit_context* emit, cond_code condition, reg dst)
{
    assert(emit);

    byte_buffer* code = &get_bin_data(emit)->code;
    uint8_t code_suffix = 0;
    if (!cond_opcode(condition, &code_suffix))
    {
        bin_fail(emit, "unknown condition code");
        return false;
    }

    if ((int)dst >= 4 && (int)dst <= 7)
    {
        if (!emit_rex_force(code, false, REG_RAX, dst))
            return false;
    }
    else if (high_reg(dst))
    {
        if (!emit_rex(code, false, REG_RAX, dst))
            return false;
    }

    if (!buffer_u8(code, 0x0F) ||
        !buffer_u8(code, (uint8_t)(0x90 + code_suffix)) ||
        !buffer_u8(code, modrm(3, 0, low_reg(dst))))
    {
        return false;
    }

    return emit_rex_force(code, true, dst, dst) &&
           buffer_u8(code, 0x0F) &&
           buffer_u8(code, 0xB6) &&
           buffer_u8(code, modrm(3, low_reg(dst), low_reg(dst)));
}

static bool emit_bin_neg_reg(emit_context* emit, reg dst)
{
    assert(emit);
    return emit_modrm_ext_reg(&get_bin_data(emit)->code, 0xF7, 3, dst);
}

static bool emit_bin_cqo(emit_context* emit)
{
    assert(emit);

    byte_buffer* code = &get_bin_data(emit)->code;
    return buffer_u8(code, 0x48) &&
           buffer_u8(code, 0x99);
}

static bool emit_bin_idiv_reg(emit_context* emit, reg divisor)
{
    assert(emit);
    return emit_modrm_ext_reg(&get_bin_data(emit)->code, 0xF7, 7, divisor);
}

static bool emit_rel32_jump(emit_context* emit, const char* label, uint8_t opcode)
{
    assert(emit);
    assert(label);

    byte_buffer* code = &get_bin_data(emit)->code;

    if (!buffer_u8(code, opcode))
        return false;

    const size_t patch_offset = code->size;
    if (!buffer_i32(code, 0))
        return false;

    return add_fixup(emit, label, patch_offset, code->size);
}

static bool emit_bin_call_label(emit_context* emit, const char* label)
{
    return emit_rel32_jump(emit, label, 0xE8);
}

static bool emit_bin_jmp_label(emit_context* emit, const char* label)
{
    return emit_rel32_jump(emit, label, 0xE9);
}

static bool emit_bin_jcc_label(emit_context* emit, cond_code condition, const char* label)
{
    assert(emit);
    assert(label);

    byte_buffer* code = &get_bin_data(emit)->code;
    uint8_t code_suffix = 0;
    if (!cond_opcode(condition, &code_suffix))
    {
        bin_fail(emit, "unknown condition code");
        return false;
    }

    if (!buffer_u8(code, 0x0F) ||
        !buffer_u8(code, (uint8_t)(0x80 + code_suffix)))
    {
        return false;
    }

    const size_t patch_offset = code->size;
    if (!buffer_i32(code, 0))
        return false;

    return add_fixup(emit, label, patch_offset, code->size);
}

static bool emit_bin_ret(emit_context* emit)
{
    assert(emit);
    return buffer_u8(&get_bin_data(emit)->code, 0xC3);
}

static bool emit_bin_syscall(emit_context* emit)
{
    assert(emit);

    byte_buffer* code = &get_bin_data(emit)->code;
    return buffer_u8(code, 0x0F) &&
           buffer_u8(code, 0x05);
}

static void emit_bin_destroy(emit_context* emit)
{
    if (!emit || !emit->data)
        return;

    bin_data* data = (bin_data*)emit->data;

    free(data->code.data);

    for (int index = 0; index < data->label_count; ++index)
        free(data->labels[index].name);

    free(data->labels);
    free(data->fixups);
    free(data);
    emit->data = NULL;
}

static const emit_ops BIN_OPS =
{
    emit_bin_destroy,

    emit_bin_blank_line,
    emit_bin_comment,
    emit_bin_label,
    emit_bin_raw_line,
    emit_bin_bytes,
    emit_bin_align,

    emit_bin_mov_reg_imm,
    emit_bin_mov_reg_reg,
    emit_bin_mov_reg_rbp_rel,
    emit_bin_mov_rbp_rel_reg,
    emit_bin_lea_reg_label,

    emit_bin_push_reg,
    emit_bin_pop_reg,

    emit_bin_add_reg_imm,
    emit_bin_sub_reg_imm,
    emit_bin_bin_reg_reg,

    emit_bin_cmp_reg_reg,
    emit_bin_test_reg_reg,
    emit_bin_setcc_reg,

    emit_bin_neg_reg,
    emit_bin_cqo,
    emit_bin_idiv_reg,

    emit_bin_call_label,
    emit_bin_jmp_label,
    emit_bin_jcc_label,

    emit_bin_ret,
    emit_bin_syscall,
};

bool emit_context_ctor_bin(emit_context* emit)
{
    if (!emit)
        return false;

    memset(emit, 0, sizeof(*emit));

    bin_data* data = (bin_data*)calloc(1, sizeof(*data));
    if (!data)
        return false;

    emit->data = data;
    emit->ops = &BIN_OPS;
    return true;
}

const unsigned char* emit_bin_data(const emit_context* emit)
{
    if (!emit || emit->ops != &BIN_OPS || !emit->data)
        return NULL;

    return get_const_bin_data(emit)->code.data;
}

size_t emit_bin_size(const emit_context* emit)
{
    if (!emit || emit->ops != &BIN_OPS || !emit->data)
        return 0;

    return get_const_bin_data(emit)->code.size;
}

bool emit_bin_resolve_fixups(emit_context* emit)
{
    if (!emit || emit->ops != &BIN_OPS || !emit->data)
        return false;

    bin_data* data = get_bin_data(emit);

    for (int index = 0; index < data->fixup_count; ++index)
    {
        const fixup_entry* fixup = &data->fixups[index];
        assert(fixup->label_index >= 0);
        assert(fixup->label_index < data->label_count);

        const label_entry* label = &data->labels[fixup->label_index];
        if (!label->is_defined)
        {
            bin_fail(emit, "undefined label in binary emitter");
            return false;
        }

        const int64_t rel64 = (int64_t)label->offset - (int64_t)fixup->next_instruction_offset;
        if (rel64 < INT32_MIN || rel64 > INT32_MAX)
        {
            bin_fail(emit, "relative jump offset is out of int32 range");
            return false;
        }

        if (!buffer_patch_i32(&data->code, fixup->patch_offset, (int32_t)rel64))
        {
            bin_fail(emit, "failed to patch relative label reference");
            return false;
        }
    }

    return true;
}


bool emit_bin_label_offset(const emit_context* emit, const char* label, size_t* offset)
{
    if (!emit || emit->ops != &BIN_OPS || !emit->data || !label || !offset)
        return false;

    const bin_data* data = get_const_bin_data(emit);
    const int label_index = find_label_index(data, label);
    if (label_index < 0)
        return false;

    if (!data->labels[label_index].is_defined)
        return false;

    *offset = data->labels[label_index].offset;
    return true;
}
