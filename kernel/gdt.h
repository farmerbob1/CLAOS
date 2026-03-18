/*
 * CLAOS — Claude Assisted Operating System
 * gdt.h — Global Descriptor Table
 *
 * The GDT defines memory segments in protected mode. We re-initialize it
 * from C (the bootloader set up a temporary one) so we have full control.
 */

#ifndef CLAOS_GDT_H
#define CLAOS_GDT_H

#include "types.h"

/* GDT entry structure — packed to match the exact hardware format */
struct gdt_entry {
    uint16_t limit_low;     /* Lower 16 bits of the limit */
    uint16_t base_low;      /* Lower 16 bits of the base */
    uint8_t  base_middle;   /* Bits 16-23 of the base */
    uint8_t  access;        /* Access flags */
    uint8_t  granularity;   /* Granularity + upper 4 bits of limit */
    uint8_t  base_high;     /* Bits 24-31 of the base */
} __attribute__((packed));

/* Pointer structure passed to LGDT instruction */
struct gdt_ptr {
    uint16_t limit;         /* Size of GDT minus 1 */
    uint32_t base;          /* Base address of GDT */
} __attribute__((packed));

/* Initialize the GDT with our segments */
void gdt_init(void);

#endif /* CLAOS_GDT_H */
