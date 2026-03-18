;
; CLAOS — Claude Assisted Operating System
; gdt_flush.asm — Load GDT and reload segment registers
;
; After loading a new GDT, we must reload all segment registers.
; CS requires a far jump, the rest can be loaded with mov.
;

[BITS 32]

global gdt_flush
global idt_flush

; void gdt_flush(uint32_t gdt_ptr_addr)
; Load the GDT pointer and reload all segment registers
gdt_flush:
    mov eax, [esp + 4]     ; Get the GDT pointer argument
    lgdt [eax]              ; Load the GDT

    ; Reload data segment registers with selector 0x10 (kernel data)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Reload CS with a far jump to selector 0x08 (kernel code)
    jmp 0x08:.flush
.flush:
    ret

; void idt_flush(uint32_t idt_ptr_addr)
; Load the IDT pointer
idt_flush:
    mov eax, [esp + 4]
    lidt [eax]
    ret
