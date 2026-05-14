#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "exec_emit.h"

static bool check_emit(const emit_context* emit)
{
    return emit && emit->ops;
}

static void emit_fail(emit_context* emit, const char* text)
{
    if (!emit || !text)
        return;

    if (emit->error_text[0] == '\0')
        snprintf(emit->error_text, EMIT_ERROR_TEXT_SIZE, "%s", text);
}

const char* reg_name(reg id)
{
    static const char* REG_NAMES[] =
    {
        "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
    };

    if ((int)id < 0 || (int)id >= (int)(sizeof(REG_NAMES) / sizeof(REG_NAMES[0])))
        return "invalid_reg";

    return REG_NAMES[(int)id];
}

const char* reg_byte_name(reg id)
{
    static const char* REG_NAMES[] =
    {
        "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
        "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b",
    };

    if ((int)id < 0 || (int)id >= (int)(sizeof(REG_NAMES) / sizeof(REG_NAMES[0])))
        return "invalid_reg";

    return REG_NAMES[(int)id];
}

const char* cond_set_name(cond_code condition)
{
    switch (condition)
    {
        case COND_E:  return "sete";
        case COND_NE: return "setne";
        case COND_L:  return "setl";
        case COND_G:  return "setg";
        case COND_LE: return "setle";
        case COND_GE: return "setge";
        case COND_Z:  return "setz";
        case COND_NZ: return "setnz";
        default:      return "set_invalid";
    }
}

const char* cond_jump_name(cond_code condition)
{
    switch (condition)
    {
        case COND_E:  return "je";
        case COND_NE: return "jne";
        case COND_L:  return "jl";
        case COND_G:  return "jg";
        case COND_LE: return "jle";
        case COND_GE: return "jge";
        case COND_Z:  return "jz";
        case COND_NZ: return "jnz";
        default:      return "j_invalid";
    }
}

static bool emit_nasm_blank_line(emit_context* emit)
{
    assert(emit);
    assert(emit->out);

    return fprintf(emit->out, "\n") >= 0;
}

static bool emit_nasm_comment(emit_context* emit, const char* comment)
{
    assert(emit);
    assert(emit->out);
    assert(comment);

    return fprintf(emit->out, "; %s\n", comment) >= 0;
}

static bool emit_nasm_label(emit_context* emit, const char* label)
{
    assert(emit);
    assert(emit->out);
    assert(label);

    return fprintf(emit->out, "%s:\n", label) >= 0;
}

static bool emit_nasm_raw_line(emit_context* emit, const char* format, va_list args)
{
    assert(emit);
    assert(emit->out);
    assert(format);

    const int written = vfprintf(emit->out, format, args);
    if (written < 0)
        return false;

    return fprintf(emit->out, "\n") >= 0;
}

static bool emit_nasm_bytes(emit_context* emit, const void* bytes, size_t size)
{
    assert(emit);
    assert(bytes || size == 0);

    const unsigned char* data = (const unsigned char*)bytes;

    if (size == 0)
        return emit_line(emit, " db 0");

    if (fprintf(emit->out, " db ") < 0)
        return false;

    for (size_t index = 0; index < size; ++index)
    {
        if (index > 0 && fprintf(emit->out, ", ") < 0)
            return false;

        if (fprintf(emit->out, "%u", (unsigned int)data[index]) < 0)
            return false;
    }

    return fprintf(emit->out, "\n") >= 0;
}

static bool emit_nasm_align(emit_context* emit, size_t alignment, unsigned char value)
{
    assert(emit);

    if (alignment <= 1)
        return true;

    return emit_line(emit, " align %zu, db %u", alignment, (unsigned int)value);
}

static bool emit_nasm_mov_reg_imm(emit_context* emit, reg dst, int64_t imm)
{
    return emit_line(emit, " mov %s, %lld", reg_name(dst), (long long)imm);
}

static bool emit_nasm_mov_reg_reg(emit_context* emit, reg dst, reg src)
{
    return emit_line(emit, " mov %s, %s", reg_name(dst), reg_name(src));
}

static bool emit_nasm_mov_reg_rbp_rel(emit_context* emit, reg dst, int32_t rbp_disp)
{
    if (rbp_disp < 0)
        return emit_line(emit, " mov %s, qword [rbp - %d]", reg_name(dst), -rbp_disp);

    if (rbp_disp > 0)
        return emit_line(emit, " mov %s, qword [rbp + %d]", reg_name(dst), rbp_disp);

    return emit_line(emit, " mov %s, qword [rbp]", reg_name(dst));
}

static bool emit_nasm_mov_rbp_rel_reg(emit_context* emit, int32_t rbp_disp, reg src)
{
    if (rbp_disp < 0)
        return emit_line(emit, " mov qword [rbp - %d], %s", -rbp_disp, reg_name(src));

    if (rbp_disp > 0)
        return emit_line(emit, " mov qword [rbp + %d], %s", rbp_disp, reg_name(src));

    return emit_line(emit, " mov qword [rbp], %s", reg_name(src));
}

static bool emit_nasm_lea_reg_label(emit_context* emit, reg dst, const char* label)
{
    assert(label);
    return emit_line(emit, " lea %s, [rel %s]", reg_name(dst), label);
}

static bool emit_nasm_push_reg(emit_context* emit, reg src)
{
    return emit_line(emit, " push %s", reg_name(src));
}

static bool emit_nasm_pop_reg(emit_context* emit, reg dst)
{
    return emit_line(emit, " pop %s", reg_name(dst));
}

static bool emit_nasm_add_reg_imm(emit_context* emit, reg dst, int32_t imm)
{
    return emit_line(emit, " add %s, %d", reg_name(dst), imm);
}

static bool emit_nasm_sub_reg_imm(emit_context* emit, reg dst, int32_t imm)
{
    return emit_line(emit, " sub %s, %d", reg_name(dst), imm);
}

static bool emit_nasm_bin_reg_reg(emit_context* emit, bin_op operation, reg dst, reg src)
{
    switch (operation)
    {
        case BIN_OP_ADD:  return emit_line(emit, " add %s, %s", reg_name(dst), reg_name(src));
        case BIN_OP_SUB:  return emit_line(emit, " sub %s, %s", reg_name(dst), reg_name(src));
        case BIN_OP_IMUL: return emit_line(emit, " imul %s, %s", reg_name(dst), reg_name(src));
        default:
            emit_fail(emit, "unknown binary register operation");
            return false;
    }
}

static bool emit_nasm_cmp_reg_reg(emit_context* emit, reg left, reg right)
{
    return emit_line(emit, " cmp %s, %s", reg_name(left), reg_name(right));
}

static bool emit_nasm_test_reg_reg(emit_context* emit, reg left, reg right)
{
    return emit_line(emit, " test %s, %s", reg_name(left), reg_name(right));
}

static bool emit_nasm_setcc_reg(emit_context* emit, cond_code condition, reg dst)
{
    return emit_line(emit, " %s %s", cond_set_name(condition), reg_byte_name(dst));
}

static bool emit_nasm_neg_reg(emit_context* emit, reg dst)
{
    return emit_line(emit, " neg %s", reg_name(dst));
}

static bool emit_nasm_cqo(emit_context* emit)
{
    return emit_line(emit, " cqo");
}

static bool emit_nasm_idiv_reg(emit_context* emit, reg divisor)
{
    return emit_line(emit, " idiv %s", reg_name(divisor));
}

static bool emit_nasm_call_label(emit_context* emit, const char* label)
{
    assert(label);
    return emit_line(emit, " call %s", label);
}

static bool emit_nasm_jmp_label(emit_context* emit, const char* label)
{
    assert(label);
    return emit_line(emit, " jmp %s", label);
}

static bool emit_nasm_jcc_label(emit_context* emit, cond_code condition, const char* label)
{
    assert(label);
    return emit_line(emit, " %s %s", cond_jump_name(condition), label);
}

static bool emit_nasm_ret(emit_context* emit)
{
    return emit_line(emit, " ret");
}

static bool emit_nasm_syscall(emit_context* emit)
{
    return emit_line(emit, " syscall");
}

static const emit_ops NASM_OPS =
{
    NULL,

    emit_nasm_blank_line,
    emit_nasm_comment,
    emit_nasm_label,
    emit_nasm_raw_line,
    emit_nasm_bytes,
    emit_nasm_align,

    emit_nasm_mov_reg_imm,
    emit_nasm_mov_reg_reg,
    emit_nasm_mov_reg_rbp_rel,
    emit_nasm_mov_rbp_rel_reg,
    emit_nasm_lea_reg_label,

    emit_nasm_push_reg,
    emit_nasm_pop_reg,

    emit_nasm_add_reg_imm,
    emit_nasm_sub_reg_imm,
    emit_nasm_bin_reg_reg,

    emit_nasm_cmp_reg_reg,
    emit_nasm_test_reg_reg,
    emit_nasm_setcc_reg,

    emit_nasm_neg_reg,
    emit_nasm_cqo,
    emit_nasm_idiv_reg,

    emit_nasm_call_label,
    emit_nasm_jmp_label,
    emit_nasm_jcc_label,

    emit_nasm_ret,
    emit_nasm_syscall,
};

bool emit_context_ctor_nasm(emit_context* emit, FILE* out)
{
    if (!emit || !out)
        return false;

    memset(emit, 0, sizeof(*emit));
    emit->out = out;
    emit->ops = &NASM_OPS;
    return true;
}

void emit_context_reset(emit_context* emit)
{
    if (!emit)
        return;

    if (emit->ops && emit->ops->destroy)
        emit->ops->destroy(emit);

    emit->out = NULL;
    emit->ops = NULL;
    emit->data = NULL;
    emit->error_text[0] = '\0';
}

bool emit_blank_line(emit_context* emit)
{
    assert(check_emit(emit));
    return emit->ops->blank_line(emit);
}

bool emit_comment(emit_context* emit, const char* comment)
{
    assert(check_emit(emit));
    assert(comment);
    return emit->ops->comment(emit, comment);
}

bool emit_label(emit_context* emit, const char* label)
{
    assert(check_emit(emit));
    assert(label);
    return emit->ops->label(emit, label);
}

bool emit_line(emit_context* emit, const char* format, ...)
{
    assert(check_emit(emit));
    assert(format);

    va_list args = {};
    va_start(args, format);
    const bool ok = emit->ops->raw_line(emit, format, args);
    va_end(args);

    return ok;
}

bool emit_bytes(emit_context* emit, const void* data, size_t size)
{
    assert(check_emit(emit));
    assert(data || size == 0);

    if (!emit->ops->bytes)
    {
        emit_fail(emit, "emitter bytes operation is not supported");
        return false;
    }

    return emit->ops->bytes(emit, data, size);
}

bool emit_align(emit_context* emit, size_t alignment, unsigned char value)
{
    assert(check_emit(emit));

    if (!emit->ops->align)
    {
        emit_fail(emit, "emitter align operation is not supported");
        return false;
    }

    return emit->ops->align(emit, alignment, value);
}

#define EMIT_CALL(name, ...)                                      \
    do                                                           \
    {                                                            \
        assert(check_emit(emit));                                \
        if (!emit->ops->name)                          \
        {                                                        \
            emit_fail(emit, "emitter operation is not supported"); \
            return false;                                        \
        }                                                        \
        return emit->ops->name(emit, __VA_ARGS__);            \
    } while (0)

bool emit_mov_reg_imm(emit_context* emit, reg dst, int64_t imm)
{
    EMIT_CALL(mov_reg_imm, dst, imm);
}

bool emit_mov_reg_reg(emit_context* emit, reg dst, reg src)
{
    EMIT_CALL(mov_reg_reg, dst, src);
}

bool emit_mov_reg_rbp_rel(emit_context* emit, reg dst, int32_t rbp_disp)
{
    EMIT_CALL(mov_reg_rbp_rel, dst, rbp_disp);
}

bool emit_mov_rbp_rel_reg(emit_context* emit, int32_t rbp_disp, reg src)
{
    EMIT_CALL(mov_rbp_rel_reg, rbp_disp, src);
}

bool emit_lea_reg_label(emit_context* emit, reg dst, const char* label)
{
    EMIT_CALL(lea_reg_label, dst, label);
}

bool emit_push_reg(emit_context* emit, reg src)
{
    EMIT_CALL(push_reg, src);
}

bool emit_pop_reg(emit_context* emit, reg dst)
{
    EMIT_CALL(pop_reg, dst);
}

bool emit_add_reg_imm(emit_context* emit, reg dst, int32_t imm)
{
    EMIT_CALL(add_reg_imm, dst, imm);
}

bool emit_sub_reg_imm(emit_context* emit, reg dst, int32_t imm)
{
    EMIT_CALL(sub_reg_imm, dst, imm);
}

bool emit_bin_reg_reg(emit_context* emit, bin_op operation, reg dst, reg src)
{
    EMIT_CALL(bin_reg_reg, operation, dst, src);
}

bool emit_cmp_reg_reg(emit_context* emit, reg left, reg right)
{
    EMIT_CALL(cmp_reg_reg, left, right);
}

bool emit_test_reg_reg(emit_context* emit, reg left, reg right)
{
    EMIT_CALL(test_reg_reg, left, right);
}

bool emit_setcc_reg(emit_context* emit, cond_code condition, reg dst)
{
    EMIT_CALL(setcc_reg, condition, dst);
}

bool emit_neg_reg(emit_context* emit, reg dst)
{
    EMIT_CALL(neg_reg, dst);
}

bool emit_cqo(emit_context* emit)
{
    assert(check_emit(emit));
    return emit->ops->cqo(emit);
}

bool emit_idiv_reg(emit_context* emit, reg divisor)
{
    EMIT_CALL(idiv_reg, divisor);
}

bool emit_call_label(emit_context* emit, const char* label)
{
    EMIT_CALL(call_label, label);
}

bool emit_jmp_label(emit_context* emit, const char* label)
{
    EMIT_CALL(jmp_label, label);
}

bool emit_jcc_label(emit_context* emit, cond_code condition, const char* label)
{
    EMIT_CALL(jcc_label, condition, label);
}

bool emit_ret(emit_context* emit)
{
    assert(check_emit(emit));
    return emit->ops->ret(emit);
}

bool emit_syscall(emit_context* emit)
{
    assert(check_emit(emit));
    return emit->ops->syscall(emit);
}

#undef EMIT_CALL
