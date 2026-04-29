; Witcher Language backend, fifth commit
BITS 64
DEFAULT REL

global _start
section .text

_start:
; call the first language function and exit with its result
    call WL_FUNC_0
    mov rdi, rax
    mov rax, 60
    syscall

; function проклятое_дитя
WL_FUNC_0:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    call WL_RT_READ_NUM
    mov qword [rbp - 8], rax
    call WL_RT_READ_NUM
    mov qword [rbp - 16], rax
    call WL_RT_READ_NUM
    mov qword [rbp - 24], rax
    mov rax, qword [rbp - 8]
    push rax
    mov rax, 0
    pop rbx
    cmp rbx, rax
    xor eax, eax
    sete al
    test rax, rax
    jz WL_LABEL_0
    mov rax, qword [rbp - 16]
    push rax
    mov rax, 0
    pop rbx
    cmp rbx, rax
    xor eax, eax
    sete al
    test rax, rax
    jz WL_LABEL_1
    mov rax, qword [rbp - 24]
    push rax
    mov rax, 0
    pop rbx
    cmp rbx, rax
    xor eax, eax
    sete al
    test rax, rax
    jz WL_LABEL_3
    lea rdi, [rel WL_TEXT_5]
    mov rsi, 6
    call WL_RT_PRINT_TEXT
    lea rdi, [rel WL_TEXT_6]
    mov rsi, 12
    call WL_RT_PRINT_TEXT
    mov rdi, 10
    call WL_RT_PRINT_CHAR
    mov rax, 2
    neg rax
    mov rsp, rbp
    pop rbp
    ret
    jmp WL_LABEL_4
WL_LABEL_3:
    lea rdi, [rel WL_TEXT_7]
    mov rsi, 8
    call WL_RT_PRINT_TEXT
    mov rdi, 10
    call WL_RT_PRINT_CHAR
    mov rax, 0
    mov rdi, rax
    call WL_RT_PRINT_NUM
    mov rax, 0
    mov rsp, rbp
    pop rbp
    ret
WL_LABEL_4:
    jmp WL_LABEL_2
WL_LABEL_1:
    mov rax, qword [rbp - 24]
    neg rax
    push rax
    mov rax, qword [rbp - 16]
    mov rcx, rax
    pop rax
    cqo
    idiv rcx
    mov qword [rbp - 32], rax
    lea rdi, [rel WL_TEXT_8]
    mov rsi, 8
    call WL_RT_PRINT_TEXT
    lea rdi, [rel WL_TEXT_9]
    mov rsi, 12
    call WL_RT_PRINT_TEXT
    mov rdi, 10
    call WL_RT_PRINT_CHAR
    mov rax, qword [rbp - 32]
    mov rdi, rax
    call WL_RT_PRINT_NUM
    mov rdi, 10
    call WL_RT_PRINT_CHAR
    mov rax, 1
    mov rsp, rbp
    pop rbp
    ret
WL_LABEL_2:
WL_LABEL_0:
    mov rax, qword [rbp - 16]
    push rax
    mov rax, qword [rbp - 16]
    pop rbx
    imul rax, rbx
    push rax
    mov rax, 4
    push rax
    mov rax, qword [rbp - 8]
    pop rbx
    imul rax, rbx
    push rax
    mov rax, qword [rbp - 24]
    pop rbx
    imul rax, rbx
    pop rbx
    sub rbx, rax
    mov rax, rbx
    mov qword [rbp - 40], rax
    mov rax, qword [rbp - 40]
    push rax
    mov rax, 0
    pop rbx
    cmp rbx, rax
    xor eax, eax
    setl al
    test rax, rax
    jz WL_LABEL_10
    mov rax, 0
    mov rdi, rax
    call WL_RT_PRINT_NUM
    mov rax, 0
    mov rsp, rbp
    pop rbp
    ret
WL_LABEL_10:
    mov rax, qword [rbp - 40]
    push rax
    mov rax, 0
    pop rbx
    cmp rbx, rax
    xor eax, eax
    sete al
    test rax, rax
    jz WL_LABEL_11
    mov rax, qword [rbp - 16]
    push rax
    mov rax, 2
    push rax
    mov rax, qword [rbp - 8]
    pop rbx
    imul rax, rbx
    mov rcx, rax
    pop rax
    cqo
    idiv rcx
    mov qword [rbp - 32], rax
    lea rdi, [rel WL_TEXT_12]
    mov rsi, 8
    call WL_RT_PRINT_TEXT
    lea rdi, [rel WL_TEXT_9]
    mov rsi, 12
    call WL_RT_PRINT_TEXT
    mov rdi, 10
    call WL_RT_PRINT_CHAR
    mov rax, qword [rbp - 32]
    mov rdi, rax
    call WL_RT_PRINT_NUM
    mov rdi, 10
    call WL_RT_PRINT_CHAR
    mov rax, 1
    mov rsp, rbp
    pop rbp
    ret
WL_LABEL_11:
    mov rax, qword [rbp - 40]
    push rax
    call WL_FUNC_1
    add rsp, 8
    mov qword [rbp - 48], rax
    mov rax, qword [rbp - 16]
    neg rax
    push rax
    mov rax, qword [rbp - 48]
    pop rbx
    sub rbx, rax
    mov rax, rbx
    push rax
    mov rax, 2
    push rax
    mov rax, qword [rbp - 8]
    pop rbx
    imul rax, rbx
    mov rcx, rax
    pop rax
    cqo
    idiv rcx
    mov qword [rbp - 56], rax
    mov rax, qword [rbp - 16]
    neg rax
    push rax
    mov rax, qword [rbp - 48]
    pop rbx
    add rax, rbx
    push rax
    mov rax, 2
    push rax
    mov rax, qword [rbp - 8]
    pop rbx
    imul rax, rbx
    mov rcx, rax
    pop rax
    cqo
    idiv rcx
    mov qword [rbp - 64], rax
    lea rdi, [rel WL_TEXT_13]
    mov rsi, 6
    call WL_RT_PRINT_TEXT
    lea rdi, [rel WL_TEXT_14]
    mov rsi, 10
    call WL_RT_PRINT_TEXT
    mov rdi, 10
    call WL_RT_PRINT_CHAR
    mov rax, qword [rbp - 56]
    mov rdi, rax
    call WL_RT_PRINT_NUM
    mov rdi, 10
    call WL_RT_PRINT_CHAR
    mov rax, qword [rbp - 64]
    mov rdi, rax
    call WL_RT_PRINT_NUM
    mov rdi, 10
    call WL_RT_PRINT_CHAR
    mov rax, 2
    mov rsp, rbp
    pop rbp
    ret
    mov rax, 0
    mov rsp, rbp
    pop rbp
    ret

; function вилохвост
WL_FUNC_1:
    push rbp
    mov rbp, rsp
    sub rsp, 8
    mov rax, 0
    mov qword [rbp - 8], rax
WL_LABEL_15:
    mov rax, qword [rbp - 8]
    push rax
    mov rax, qword [rbp - 8]
    pop rbx
    imul rax, rbx
    push rax
    mov rax, qword [rbp + 16]
    pop rbx
    cmp rbx, rax
    xor eax, eax
    setl al
    test rax, rax
    jz WL_LABEL_16
    mov rax, qword [rbp - 8]
    push rax
    mov rax, 1
    pop rbx
    add rax, rbx
    mov qword [rbp - 8], rax
    jmp WL_LABEL_15
WL_LABEL_16:
    mov rax, qword [rbp - 8]
    mov rsp, rbp
    pop rbp
    ret
    mov rax, 0
    mov rsp, rbp
    pop rbp
    ret

; runtime helpers
; write text: rdi = address, rsi = length
WL_RT_PRINT_TEXT:
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, 1
    mov rax, 1
    syscall
    ret

; write one character from dil
WL_RT_PRINT_CHAR:
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

; write signed decimal number from rdi and append newline
WL_RT_PRINT_NUM:
    push rbp
    mov rbp, rsp
    sub rsp, 64
    mov r11, rdi
    lea r8, [rbp - 1]
    mov byte [r8], 10
    mov r9, 1
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

; read signed decimal number from stdin into rax
WL_RT_READ_NUM:
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

section .data

; нет
WL_TEXT_5:
    db 208, 189, 208, 181, 209, 130

; корней
WL_TEXT_6:
    db 208, 186, 208, 190, 209, 128, 208, 189, 208, 181, 208, 185

; попа
WL_TEXT_7:
    db 208, 191, 208, 190, 208, 191, 208, 176

; Один
WL_TEXT_8:
    db 208, 158, 208, 180, 208, 184, 208, 189

; корень
WL_TEXT_9:
    db 208, 186, 208, 190, 209, 128, 208, 181, 208, 189, 209, 140

; один
WL_TEXT_12:
    db 208, 190, 208, 180, 208, 184, 208, 189

; два
WL_TEXT_13:
    db 208, 180, 208, 178, 208, 176

; корня
WL_TEXT_14:
    db 208, 186, 208, 190, 209, 128, 208, 189, 209, 143

