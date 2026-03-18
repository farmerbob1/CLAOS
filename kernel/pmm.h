/*
 * CLAOS — Claude Assisted Operating System
 * pmm.h — Physical Memory Manager
 *
 * Manages physical RAM using a bitmap allocator. Each bit represents
 * one 4KB page: 0 = free, 1 = used. Parses the BIOS E820 memory map
 * to discover available memory regions.
 */

#ifndef CLAOS_PMM_H
#define CLAOS_PMM_H

#include "types.h"

#define PAGE_SIZE 4096

/* E820 memory map entry (as stored by Stage 2 bootloader at 0x8004) */
struct e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;      /* 1 = usable, 2 = reserved, 3+ = other */
    uint32_t acpi;      /* ACPI extended attributes (unused) */
} __attribute__((packed));

/* E820 memory types */
#define E820_USABLE     1
#define E820_RESERVED   2
#define E820_ACPI_RECL  3
#define E820_ACPI_NVS   4
#define E820_BAD        5

/* Initialize the PMM using the E820 memory map from the bootloader */
void pmm_init(uint32_t kernel_end);

/* Allocate a single 4KB physical page. Returns physical address, or 0 on failure. */
uint32_t pmm_alloc_page(void);

/* Free a previously allocated page */
void pmm_free_page(uint32_t phys_addr);

/* Get total physical memory in bytes */
uint32_t pmm_get_total_memory(void);

/* Get number of free pages */
uint32_t pmm_get_free_pages(void);

/* Get number of used pages */
uint32_t pmm_get_used_pages(void);

#endif /* CLAOS_PMM_H */
