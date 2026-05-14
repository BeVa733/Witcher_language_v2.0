#ifndef EXEC_EMIT_H
#define EXEC_EMIT_H

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

const int EMIT_ERROR_TEXT_SIZE = 256;

enum output_format
{
    OUT_FORMAT_NASM = 0,
    OUT_FORMAT_ELF  = 1,
};

enum reg
{
    REG_RAX = 0,
    REG_RCX = 1,
    REG_RDX = 2,
    REG_RBX = 3,
    REG_RSP = 4,
    REG_RBP = 5,
    REG_RSI = 6,
    REG_RDI = 7,

    REG_R8  = 8,
    REG_R9  = 9,
    REG_R10 = 10,
    REG_R11 = 11,
    REG_R12 = 12,
    REG_R13 = 13,
    REG_R14 = 14,
    REG_R15 = 15,
};

enum bin_op
{
    BIN_OP_ADD  = 0,
    BIN_OP_SUB  = 1,
    BIN_OP_IMUL = 2,
};

enum cond_code
{
    COND_E  = 0,
    COND_NE = 1,
    COND_L  = 2,
    COND_G  = 3,
    COND_LE = 4,
    COND_GE = 5,
    COND_Z  = 6,
    COND_NZ = 7,
};

struct emit_context;

struct emit_ops
{
    void (*destroy)          (emit_context* emit);

    bool (*blank_line)       (emit_context* emit);
    bool (*comment)          (emit_context* emit, const char* comment);
    bool (*label)            (emit_context* emit, const char* label);
    bool (*raw_line)         (emit_context* emit, const char* format, va_list args);
    bool (*bytes)            (emit_context* emit, const void* data, size_t size);
    bool (*align)            (emit_context* emit, size_t alignment, unsigned char value);

    bool (*mov_reg_imm)      (emit_context* emit, reg dst, int64_t imm);
    bool (*mov_reg_reg)      (emit_context* emit, reg dst, reg src);
    bool (*mov_reg_rbp_rel)  (emit_context* emit, reg dst, int32_t rbp_disp);
    bool (*mov_rbp_rel_reg)  (emit_context* emit, int32_t rbp_disp, reg src);
    bool (*lea_reg_label)    (emit_context* emit, reg dst, const char* label);

    bool (*push_reg)         (emit_context* emit, reg src);
    bool (*pop_reg)          (emit_context* emit, reg dst);

    bool (*add_reg_imm)      (emit_context* emit, reg dst, int32_t imm);
    bool (*sub_reg_imm)      (emit_context* emit, reg dst, int32_t imm);
    bool (*bin_reg_reg)      (emit_context* emit, bin_op operation, reg dst, reg src);

    bool (*cmp_reg_reg)      (emit_context* emit, reg left, reg right);
    bool (*test_reg_reg)     (emit_context* emit, reg left, reg right);
    bool (*setcc_reg)        (emit_context* emit, cond_code condition, reg dst);

    bool (*neg_reg)          (emit_context* emit, reg dst);
    bool (*cqo)              (emit_context* emit);
    bool (*idiv_reg)         (emit_context* emit, reg divisor);

    bool (*call_label)       (emit_context* emit, const char* label);
    bool (*jmp_label)        (emit_context* emit, const char* label);
    bool (*jcc_label)        (emit_context* emit, cond_code condition, const char* label);

    bool (*ret)              (emit_context* emit);
    bool (*syscall)          (emit_context* emit);
};

struct emit_context
{
    FILE* out;
    const emit_ops* ops;
    void* data;
    char error_text[EMIT_ERROR_TEXT_SIZE];
};

bool emit_context_ctor_nasm(emit_context* emit, FILE* out);
bool emit_context_ctor_bin (emit_context* emit);
void emit_context_reset    (emit_context* emit);

const unsigned char* emit_bin_data(const emit_context* emit);
size_t emit_bin_size              (const emit_context* emit);
bool emit_bin_resolve_fixups      (emit_context* emit);
bool emit_bin_label_offset        (const emit_context* emit, const char* label, size_t* offset);

const char* reg_name      (reg id);
const char* reg_byte_name (reg id);
const char* cond_set_name (cond_code condition);
const char* cond_jump_name(cond_code condition);

bool emit_blank_line      (emit_context* emit);
bool emit_comment         (emit_context* emit, const char* comment);
bool emit_label           (emit_context* emit, const char* label);
bool emit_line            (emit_context* emit, const char* format, ...);
bool emit_bytes           (emit_context* emit, const void* data, size_t size);
bool emit_align           (emit_context* emit, size_t alignment, unsigned char value);

bool emit_mov_reg_imm     (emit_context* emit, reg dst, int64_t imm);
bool emit_mov_reg_reg     (emit_context* emit, reg dst, reg src);
bool emit_mov_reg_rbp_rel (emit_context* emit, reg dst, int32_t rbp_disp);
bool emit_mov_rbp_rel_reg (emit_context* emit, int32_t rbp_disp, reg src);
bool emit_lea_reg_label   (emit_context* emit, reg dst, const char* label);

bool emit_push_reg        (emit_context* emit, reg src);
bool emit_pop_reg         (emit_context* emit, reg dst);

bool emit_add_reg_imm     (emit_context* emit, reg dst, int32_t imm);
bool emit_sub_reg_imm     (emit_context* emit, reg dst, int32_t imm);
bool emit_bin_reg_reg     (emit_context* emit, bin_op operation, reg dst, reg src);

bool emit_cmp_reg_reg     (emit_context* emit, reg left, reg right);
bool emit_test_reg_reg    (emit_context* emit, reg left, reg right);
bool emit_setcc_reg       (emit_context* emit, cond_code condition, reg dst);

bool emit_neg_reg         (emit_context* emit, reg dst);
bool emit_cqo             (emit_context* emit);
bool emit_idiv_reg        (emit_context* emit, reg divisor);

bool emit_call_label      (emit_context* emit, const char* label);
bool emit_jmp_label       (emit_context* emit, const char* label);
bool emit_jcc_label       (emit_context* emit, cond_code condition, const char* label);

bool emit_ret             (emit_context* emit);
bool emit_syscall         (emit_context* emit);

#endif
