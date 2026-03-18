/*
 * CLAOS — Claude Assisted Operating System
 * vmm.c — Virtual Memory Manager (Paging)
 *
 * Sets up x86 paging with a page directory and page tables.
 * We identity-map the first 16MB so kernel addresses = physical addresses.
 * This keeps things simple — the kernel can keep using physical addresses
 * directly while having the paging hardware enabled.
 *
 * Page directory: 1024 entries, each pointing to a page table
 * Page table:     1024 entries, each mapping one 4KB page
 * Total coverage: 1024 * 1024 * 4KB = 4GB
 */

#include "vmm.h"
#include "pmm.h"
#include "string.h"
#include "io.h"
#include "vga.h"

/* Page directory — must be 4KB-aligned.
 * We statically allocate it in BSS so we don't need the heap yet. */
static uint32_t page_directory[1024] __attribute__((aligned(4096)));

/* Page tables for the first 16MB (4 tables × 4MB each = 16MB identity-mapped).
 * This covers the kernel, VGA buffer, and leaves room for the heap. */
static uint32_t page_tables[4][1024] __attribute__((aligned(4096)));

void vmm_init(void) {
    /* Clear the page directory */
    memset(page_directory, 0, sizeof(page_directory));

    /* Identity-map the first 16MB using our statically allocated page tables.
     * Each page table covers 4MB (1024 pages × 4KB). */
    for (int t = 0; t < 4; t++) {
        for (int p = 0; p < 1024; p++) {
            uint32_t phys = (t * 1024 + p) * PAGE_SIZE;
            page_tables[t][p] = phys | PTE_PRESENT | PTE_WRITABLE;
        }

        /* Install this page table in the page directory */
        page_directory[t] = (uint32_t)&page_tables[t] | PTE_PRESENT | PTE_WRITABLE;
    }

    /* Load the page directory into CR3 */
    __asm__ volatile ("mov %0, %%cr3" : : "r"(&page_directory));

    /* Enable paging by setting bit 31 (PG) in CR0 */
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

    serial_print("[VMM] Paging enabled, 16MB identity-mapped\n");
}

void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_index = virt >> 22;             /* Top 10 bits */
    uint32_t pt_index = (virt >> 12) & 0x3FF;   /* Middle 10 bits */

    /* Check if a page table exists for this directory entry */
    if (!(page_directory[pd_index] & PTE_PRESENT)) {
        /* Allocate a new page table */
        uint32_t pt_phys = pmm_alloc_page();
        if (!pt_phys) return;   /* Out of memory */

        memset((void*)pt_phys, 0, PAGE_SIZE);
        page_directory[pd_index] = pt_phys | PTE_PRESENT | PTE_WRITABLE | flags;
    }

    /* Get the page table and map the page */
    uint32_t* pt = (uint32_t*)(page_directory[pd_index] & 0xFFFFF000);
    pt[pt_index] = (phys & 0xFFFFF000) | (flags | PTE_PRESENT);

    /* Flush the TLB for this address */
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_unmap_page(uint32_t virt) {
    uint32_t pd_index = virt >> 22;
    uint32_t pt_index = (virt >> 12) & 0x3FF;

    if (!(page_directory[pd_index] & PTE_PRESENT))
        return;

    uint32_t* pt = (uint32_t*)(page_directory[pd_index] & 0xFFFFF000);
    pt[pt_index] = 0;

    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}
