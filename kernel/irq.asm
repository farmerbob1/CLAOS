;
; CLAOS — Claude Assisted Operating System
; irq.asm — Hardware IRQ assembly stubs
;
; These are identical in structure to the ISR stubs, but they push
; interrupt numbers 32-47 (IRQs 0-15 remapped to avoid clash with
; CPU exceptions).
;

[BITS 32]

extern isr_common_handler

; Common IRQ stub — same as ISR stub, they share the C handler
irq_common_stub:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call isr_common_handler
    add esp, 4
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8
    iret

; Macro for IRQ stubs
%macro IRQ 2
global irq%1
irq%1:
    push dword 0            ; Dummy error code
    push dword %2           ; Remapped interrupt number
    jmp irq_common_stub
%endmacro

; IRQ 0-15 → Interrupts 32-47
IRQ 0,  32      ; PIT Timer
IRQ 1,  33      ; Keyboard
IRQ 2,  34      ; Cascade (used internally by PIC)
IRQ 3,  35      ; COM2
IRQ 4,  36      ; COM1
IRQ 5,  37      ; LPT2
IRQ 6,  38      ; Floppy
IRQ 7,  39      ; LPT1 / Spurious
IRQ 8,  40      ; CMOS RTC
IRQ 9,  41      ; Free
IRQ 10, 42      ; Free
IRQ 11, 43      ; Free
IRQ 12, 44      ; PS/2 Mouse
IRQ 13, 45      ; FPU
IRQ 14, 46      ; Primary ATA
IRQ 15, 47      ; Secondary ATA
