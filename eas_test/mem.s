section .data
    num1 dw 3          ; Define first number (word size)

section .text
    global _start

_start:
    mov eax, [num1]    ; Load the first number into EAX

infinite_loop:
    mov eax, [num1]    ; Load the first number into EAX

    jmp infinite_loop  ; Jump back to the start of the infinite loop

