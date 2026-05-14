BITS 64
DEFAULT REL

%define TRAMPOLINE_SIZE 8

%macro RUNTIME_TRAMPOLINE 2
%1:
            jmp near %2
            times TRAMPOLINE_SIZE - ($ - %1) db 0x90
%endmacro

RUNTIME_TRAMPOLINE WL_RT_EXIT,       WL_IMPL_EXIT
RUNTIME_TRAMPOLINE WL_RT_PRINT_TEXT, WL_IMPL_PRINT_TEXT
RUNTIME_TRAMPOLINE WL_RT_PRINT_CHAR, WL_IMPL_PRINT_CHAR
RUNTIME_TRAMPOLINE WL_RT_PRINT_NUM,  WL_IMPL_PRINT_NUM
RUNTIME_TRAMPOLINE WL_RT_READ_NUM,   WL_IMPL_READ_NUM

WL_IMPL_EXIT:
                mov rax, 60
                syscall
                ret

WL_IMPL_PRINT_TEXT:
                mov rdx, rsi
                mov rsi, rdi
                mov rdi, 1
                mov rax, 1
                syscall
                ret


WL_IMPL_PRINT_CHAR:
                push rbp
                mov rbp, rsp
                sub rsp, 16
                mov byte [rbp - 1], dil
                lea rsi, [rbp - 1]
                mov rdi, 1
                mov rdx, 1
                mov rax, 1
                syscall
                mov rsp, rbp
                pop rbp
                ret


WL_IMPL_PRINT_NUM:
                push rbp
                mov rbp, rsp
                sub rsp, 64
                mov r11, rdi
                lea r8, [rbp]
                xor r9, r9
                xor r10d, r10d
                test r11, r11
                jns .print_num_sign_ready
                mov r10d, 1

.print_num_sign_ready:
                cmp r11, 0
                jne .print_num_loop
                dec r8
                mov byte [r8], '0'
                inc r9
                jmp .print_num_after_digits

.print_num_loop:
                mov rax, r11
                cqo
                mov rcx, 10
                idiv rcx
                mov r11, rax
                test rdx, rdx
                jge .print_num_digit_ready
                neg rdx

.print_num_digit_ready:
                add dl, '0'
                dec r8
                mov byte [r8], dl
                inc r9
                test r11, r11
                jne .print_num_loop

.print_num_after_digits:
                test r10d, r10d
                jz .print_num_write
                dec r8
                mov byte [r8], '-'
                inc r9

.print_num_write:
                mov rax, 1
                mov rdi, 1
                mov rsi, r8
                mov rdx, r9
                syscall
                mov rsp, rbp
                pop rbp
                ret


WL_IMPL_READ_NUM:
                push rbp
                mov rbp, rsp
                sub rsp, 16
                xor r8, r8
                mov r9, 1
                xor r10d, r10d

.read_num_skip_space:
                mov rax, 0
                mov rdi, 0
                lea rsi, [rbp - 1]
                mov rdx, 1
                syscall
                cmp rax, 1
                jne .read_num_finish
                movzx eax, byte [rbp - 1]
                cmp al, ' '
                je .read_num_skip_space
                cmp al, 10
                je .read_num_skip_space
                cmp al, 9
                je .read_num_skip_space
                cmp al, 13
                je .read_num_skip_space
                cmp al, '-'
                jne .read_num_check_plus
                mov r9, -1
                jmp .read_num_loop

.read_num_check_plus:
                cmp al, '+'
                jne .read_num_first_digit
                jmp .read_num_loop

.read_num_first_digit:
                cmp al, '0'
                jb .read_num_finish
                cmp al, '9'
                ja .read_num_finish
                sub al, '0'
                movzx rcx, al
                mov r8, rcx
                mov r10d, 1

.read_num_loop:
                mov rax, 0
                mov rdi, 0
                lea rsi, [rbp - 1]
                mov rdx, 1
                syscall
                cmp rax, 1
                jne .read_num_finish
                movzx eax, byte [rbp - 1]
                cmp al, '0'
                jb .read_num_finish
                cmp al, '9'
                ja .read_num_finish
                imul r8, r8, 10
                sub al, '0'
                movzx rcx, al
                add r8, rcx
                mov r10d, 1
                jmp .read_num_loop

.read_num_finish:
                mov rax, r8
                cmp r9, 1
                je .read_num_done
                neg rax
.read_num_done:
                mov rsp, rbp
                pop rbp
                ret