;
; CLAOS — Claude Assisted Operating System
; entry.asm — Kernel entry stub
;
; This MUST be the first thing in the kernel binary.
; Zeroes BSS then calls kernel_main.
;

[BITS 32]

section .text.entry

global _entry
extern kernel_main
extern _bss_start
extern _bss_end

_entry:
    ; Zero BSS. Stage2 copies ~576KB from disk but BSS may extend beyond.
    ; Static variables (like use_framebuffer in vga.c) must be zero.
    cld
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi
    shr ecx, 2
    xor eax, eax
    rep stosd

    call kernel_main
    cli
    hlt
    jmp $
