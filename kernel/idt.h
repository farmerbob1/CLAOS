/*
 * CLAOS — Claude Assisted Operating System
 * idt.h — Interrupt Descriptor Table
 *
 * The IDT maps interrupt numbers (0-255) to handler functions.
 * Entries 0-31 are CPU exceptions (divide by zero, page fault, etc.)
 * Entries 32-47 are hardware IRQs (remapped from PIC)
 */

#ifndef CLAOS_IDT_H
#define CLAOS_IDT_H

#include "types.h"

/* IDT entry — describes one interrupt gate */
struct idt_entry {
    uint16_t base_low;      /* Lower 16 bits of handler address */
    uint16_t selector;      /* Kernel code segment selector */
    uint8_t  always0;       /* Always 0 */
    uint8_t  flags;         /* Type and attributes */
    uint16_t base_high;     /* Upper 16 bits of handler address */
} __attribute__((packed));

/* Pointer structure passed to LIDT instruction */
struct idt_ptr {
    uint16_t limit;         /* Size of IDT minus 1 */
    uint32_t base;          /* Base address of IDT */
} __attribute__((packed));

/* Registers pushed by our interrupt stubs */
struct registers {
    uint32_t ds;                                    /* Saved data segment */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* Pushed by pusha */
    uint32_t int_no, err_code;                      /* Interrupt number and error code */
    uint32_t eip, cs, eflags, useresp, ss;          /* Pushed by CPU */
};

/* Initialize the IDT (installs all exception and IRQ handlers) */
void idt_init(void);

/* Set a single IDT entry */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags);

/* Type for interrupt handlers */
typedef void (*isr_handler_t)(struct registers*);

/* Register a handler for a specific interrupt */
void register_interrupt_handler(uint8_t n, isr_handler_t handler);

#endif /* CLAOS_IDT_H */
