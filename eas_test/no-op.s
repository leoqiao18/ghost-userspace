section .text
    global _start

_start:
    ; Infinite loop doing nothing
infinite_loop:
    nop                 ; No operation performed
    jmp infinite_loop   ; Jump back to the start of the infinite loop
