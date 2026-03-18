;
; CLAOS — setjmp/longjmp for Lua error handling
;
; setjmp saves registers and return address into jmp_buf.
; longjmp restores them, making setjmp "return again" with a value.
;

[BITS 32]

global setjmp
global longjmp

; int setjmp(jmp_buf buf)
; Saves EBX, ESI, EDI, EBP, ESP, EIP into buf[0..5]
; Returns 0 on initial call
setjmp:
    mov eax, [esp + 4]      ; EAX = pointer to jmp_buf
    mov [eax + 0],  ebx
    mov [eax + 4],  esi
    mov [eax + 8],  edi
    mov [eax + 12], ebp
    lea ecx, [esp + 4]      ; ESP at the time of the call (after return addr)
    mov [eax + 16], ecx
    mov ecx, [esp]           ; Return address
    mov [eax + 20], ecx
    xor eax, eax             ; Return 0
    ret

; void longjmp(jmp_buf buf, int val)
; Restores registers from buf, makes setjmp return val
longjmp:
    mov edx, [esp + 4]      ; EDX = pointer to jmp_buf
    mov eax, [esp + 8]      ; EAX = return value
    test eax, eax
    jnz .nonzero
    inc eax                  ; If val == 0, return 1 instead
.nonzero:
    mov ebx, [edx + 0]
    mov esi, [edx + 4]
    mov edi, [edx + 8]
    mov ebp, [edx + 12]
    mov esp, [edx + 16]
    jmp [edx + 20]           ; Jump to saved return address
