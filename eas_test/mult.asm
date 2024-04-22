section .data
    num1 dw 3          ; Define first number (word size)
    num2 dw 4          ; Define second number (word size)

section .text
    global _start

_start:
    mov eax, [num1]    ; Load the first number into EAX
    mov ebx, [num2]    ; Load the second number into EBX

infinite_loop:
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX
    mul ebx            ; Multiply EAX by EBX, result is stored in EDX:EAX

    jmp infinite_loop  ; Jump back to the start of the infinite loop

