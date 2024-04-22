section .data
    ; Define two arrays of floating-point numbers
    array1 dd 1.0, 2.0, 3.0, 4.0
    array2 dd 5.0, 6.0, 7.0, 8.0
    result dd 0.0, 0.0, 0.0, 0.0  ; Placeholder for the result

section .text
    global _start

_start:
    ; Load the two arrays into vector registers
    movups xmm0, [array1]  ; Load array1 into xmm0
    movups xmm1, [array2]  ; Load array2 into xmm1

    ; Perform SIMD addition
    addps xmm0, xmm1  ; xmm0 = xmm0 + xmm1

    ; Store the result back into memory
    movups [result], xmm0  ; Store the result into the result array

    ; Exit the program
    mov eax, 60        ; Exit syscall number
    xor edi, edi       ; Exit status 0
    syscall            ; Invoke syscall