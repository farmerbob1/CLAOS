/*
 * CLAOS — Claude Assisted Operating System
 * gdt.c — Global Descriptor Table setup
 *
 * We define 3 segments: null (required), kernel code, kernel data.
 * Both code and data span the full 4GB address space (flat model).
 * This is the same setup as the bootloader, but now in C so we can
 * modify it later if needed (e.g., for TSS in the scheduler).
 */

#include "gdt.h"

/* Our GDT: 3 entries for now (null, code, data) */
#define GDT_ENTRIES 3
static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gp;

/* Defined in assembly — loads the GDT register and reloads segment registers */
extern void gdt_flush(uint32_t gdt_ptr_addr);

/* Set up a single GDT entry */
static void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t granularity) {
    gdt[num].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[num].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[num].base_high   = (uint8_t)((base >> 24) & 0xFF);

    gdt[num].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[num].granularity  = (uint8_t)((limit >> 16) & 0x0F);
    gdt[num].granularity |= (granularity & 0xF0);

    gdt[num].access = access;
}

void gdt_init(void) {
    /* Set up the GDT pointer */
    gp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gp.base  = (uint32_t)&gdt;

    /* Entry 0: Null segment (required by CPU) */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* Entry 1: Kernel code segment
     * Base=0, Limit=4GB, Access: present, ring 0, code, readable
     * Granularity: 4KB pages, 32-bit */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* Entry 2: Kernel data segment
     * Base=0, Limit=4GB, Access: present, ring 0, data, writable
     * Granularity: 4KB pages, 32-bit */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* Load the GDT and reload segment registers */
    gdt_flush((uint32_t)&gp);
}
