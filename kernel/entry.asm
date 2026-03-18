;
; CLAOS — Claude Assisted Operating System
; entry.asm — Kernel entry stub
;
; This MUST be the first thing in the kernel binary.
; It simply calls kernel_main in C and halts if it returns.
;

[BITS 32]

; This section is placed first by the linker script
section .text.entry

global _entry
extern kernel_main

_entry:
    call kernel_main
    cli
    hlt
    jmp $
