;
; CLAOS — Claude Assisted Operating System
; isr.asm — Interrupt Service Routine assembly stubs
;
; Each CPU exception needs its own stub because some push an error code
; and some don't. We normalize them all to push a dummy error code (0)
; when the CPU doesn't, so the stack layout is consistent for our
; C handler.
;
; Stack layout when our C handler is called:
;   [SS, ESP, EFLAGS, CS, EIP]  ← pushed by CPU
;   [error_code]                 ← pushed by CPU or by us (dummy 0)
;   [int_no]                     ← pushed by our stub
;   [EAX..EDI]                   ← pushed by pusha
;   [DS]                         ← pushed by our common stub
;

[BITS 32]

; Import our C handler
extern isr_common_handler

; ──────────────────────────────────────
; Common ISR stub — saves state, calls C handler, restores state
; ──────────────────────────────────────
isr_common_stub:
    pusha                   ; Push EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI

    mov ax, ds
    push eax                ; Save data segment

    mov ax, 0x10            ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                ; Push pointer to registers struct
    call isr_common_handler ; Call C handler
    add esp, 4              ; Clean up pushed argument

    pop eax                 ; Restore data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa                    ; Restore registers
    add esp, 8              ; Clean up error code and interrupt number
    iret                    ; Return from interrupt

; ──────────────────────────────────────
; Macros for ISR stubs
; ──────────────────────────────────────

; ISR with no error code — push dummy 0
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push dword 0            ; Dummy error code
    push dword %1           ; Interrupt number
    jmp isr_common_stub
%endmacro

; ISR with error code (CPU pushes it for us)
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push dword %1           ; Interrupt number (error code already on stack)
    jmp isr_common_stub
%endmacro

; ──────────────────────────────────────
; CPU Exception stubs (0-31)
;
; Exceptions 8, 10-14, 17, 21, 29, 30 push an error code.
; All others need a dummy.
; ──────────────────────────────────────
ISR_NOERRCODE 0     ; #DE - Divide Error
ISR_NOERRCODE 1     ; #DB - Debug
ISR_NOERRCODE 2     ; NMI - Non-Maskable Interrupt
ISR_NOERRCODE 3     ; #BP - Breakpoint
ISR_NOERRCODE 4     ; #OF - Overflow
ISR_NOERRCODE 5     ; #BR - Bound Range Exceeded
ISR_NOERRCODE 6     ; #UD - Invalid Opcode
ISR_NOERRCODE 7     ; #NM - Device Not Available
ISR_ERRCODE   8     ; #DF - Double Fault
ISR_NOERRCODE 9     ; Coprocessor Segment Overrun (legacy)
ISR_ERRCODE   10    ; #TS - Invalid TSS
ISR_ERRCODE   11    ; #NP - Segment Not Present
ISR_ERRCODE   12    ; #SS - Stack-Segment Fault
ISR_ERRCODE   13    ; #GP - General Protection Fault
ISR_ERRCODE   14    ; #PF - Page Fault
ISR_NOERRCODE 15    ; Reserved
ISR_NOERRCODE 16    ; #MF - x87 Floating-Point Exception
ISR_ERRCODE   17    ; #AC - Alignment Check
ISR_NOERRCODE 18    ; #MC - Machine Check
ISR_NOERRCODE 19    ; #XM - SIMD Floating-Point Exception
ISR_NOERRCODE 20    ; #VE - Virtualization Exception
ISR_NOERRCODE 21    ; Reserved
ISR_NOERRCODE 22    ; Reserved
ISR_NOERRCODE 23    ; Reserved
ISR_NOERRCODE 24    ; Reserved
ISR_NOERRCODE 25    ; Reserved
ISR_NOERRCODE 26    ; Reserved
ISR_NOERRCODE 27    ; Reserved
ISR_NOERRCODE 28    ; Reserved
ISR_NOERRCODE 29    ; Reserved
ISR_NOERRCODE 30    ; Reserved
ISR_NOERRCODE 31    ; Reserved
