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

/* Page tables for fine-grained mapping of the first 16MB */
static uint32_t page_tables[4][1024] __attribute__((aligned(4096)));

void vmm_init(void) {
    /* Clear the page directory */
    memset(page_directory, 0, sizeof(page_directory));

    /* Identity-map the first 16MB using 4KB page tables
     * (fine-grained control for kernel space). */
    for (int t = 0; t < 4; t++) {
        for (int p = 0; p < 1024; p++) {
            uint32_t phys = (t * 1024 + p) * PAGE_SIZE;
            uint32_t flags = PTE_PRESENT | PTE_WRITABLE;

            /* VGA/MMIO region (0xA0000-0xBFFFF): disable caching.
             * This covers the VGA graphics buffer, monochrome buffer,
             * and VGA text buffer at 0xB8000. Without these flags,
             * CPU cache can buffer writes and the display won't update. */
            if (phys >= 0xA0000 && phys < 0xC0000) {
                flags |= PTE_WRITETHROUGH | PTE_NOCACHE;
            }

            page_tables[t][p] = phys | flags;
        }
        page_directory[t] = (uint32_t)&page_tables[t] | PTE_PRESENT | PTE_WRITABLE;
    }

    /* Identity-map the rest of the 4GB address space using 4MB pages (PSE).
     * This is needed because PCI devices (e1000 NIC) have MMIO registers
     * mapped at high physical addresses (e.g., 0xFEBC0000).
     * For a toy OS, identity-mapping everything is the simplest approach. */
    for (int i = 4; i < 1024; i++) {
        uint32_t phys = i * 0x400000;  /* 4MB per entry */
        page_directory[i] = phys | PTE_PRESENT | PTE_WRITABLE | PTE_4MB;
    }

    /* Enable PSE (Page Size Extension) in CR4 so 4MB pages work */
    uint32_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x10;   /* CR4.PSE = bit 4 */
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));

    /* Load the page directory into CR3 */
    __asm__ volatile ("mov %0, %%cr3" : : "r"(&page_directory));

    /* Enable paging by setting bit 31 (PG) in CR0 */
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

    serial_print("[VMM] Paging enabled, full 4GB identity-mapped\n");
}

void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_index = virt >> 22;             /* Top 10 bits */
    uint32_t pt_index = (virt >> 12) & 0x3FF;   /* Middle 10 bits */

    /* Check if a page table exists for this directory entry */
    if (!(page_directory[pd_index] & PTE_PRESENT)) {
        /* Allocate a new page table */
        uint32_t pt_phys = pmm_alloc_page();
        if (!pt_phys) return;   /* Out of memory */

        /* Safe because we identity-map. pt_phys < 128MB which is identity-mapped. */
        memset((void*)pt_phys, 0, PAGE_SIZE);
        /* Page directory entry gets PRESENT + WRITABLE only; the per-page
         * flags (NOCACHE, WRITETHROUGH, etc.) go on the page table entries. */
        page_directory[pd_index] = pt_phys | PTE_PRESENT | PTE_WRITABLE;
    } else if (page_directory[pd_index] & PTE_4MB) {
        /* This slot holds a 4MB page — we can't add a 4KB mapping into it
         * without splitting it first. For now, just return. */
        serial_print("[VMM] vmm_map_page: can't remap 4MB page\n");
        return;
    }

    /* Get the page table and map the page */
    uint32_t* pt = (uint32_t*)(page_directory[pd_index] & 0xFFFFF000);
    pt[pt_index] = (phys & 0xFFFFF000) | (flags | PTE_PRESENT);

    /* Flush the TLB for this address */
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_split_4mb_page(uint32_t virt_4mb_aligned) {
    uint32_t pd_index = virt_4mb_aligned >> 22;

    /* Only split if it's actually a 4MB PSE page */
    if (!(page_directory[pd_index] & PTE_4MB)) {
        serial_print("[VMM] split: not a 4MB page\n");
        return;
    }

    uint32_t base_phys = page_directory[pd_index] & 0xFFC00000;
    uint32_t old_flags = page_directory[pd_index] & 0x1F; /* P, W, U, WT, NC */

    /* Allocate a page for the new page table */
    uint32_t pt_phys = pmm_alloc_page();
    if (!pt_phys) {
        serial_print("[VMM] split: out of memory\n");
        return;
    }

    /* Fill 1024 entries preserving the original identity mapping */
    uint32_t* pt = (uint32_t*)pt_phys;
    for (int i = 0; i < 1024; i++) {
        pt[i] = (base_phys + i * PAGE_SIZE) | (old_flags & ~PTE_4MB);
    }

    /* Replace PDE: point to page table, remove PTE_4MB */
    page_directory[pd_index] = pt_phys | PTE_PRESENT | PTE_WRITABLE;

    /* Flush TLB by reloading CR3 */
    __asm__ volatile ("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax", "memory");
}

int vmm_map_framebuffer(uint32_t fb_phys, uint32_t size) {
    uint32_t fb_end = fb_phys + size;

    /* Split any 4MB PSE pages that overlap the framebuffer */
    uint32_t start_4mb = fb_phys & 0xFFC00000;
    uint32_t end_4mb = (fb_end + 0x3FFFFF) & 0xFFC00000;
    for (uint32_t addr = start_4mb; addr < end_4mb; addr += 0x400000) {
        uint32_t pd_index = addr >> 22;
        if (pd_index >= 4 && (page_directory[pd_index] & PTE_4MB)) {
            vmm_split_4mb_page(addr);
        }
    }

    /* Remap each 4KB page of the framebuffer with NOCACHE */
    uint32_t flags = PTE_PRESENT | PTE_WRITABLE | PTE_WRITETHROUGH | PTE_NOCACHE;
    for (uint32_t addr = fb_phys; addr < fb_end; addr += PAGE_SIZE) {
        vmm_map_page(addr, addr, flags);
    }

    serial_print("[VMM] Framebuffer mapped with NOCACHE\n");
    return 0;
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
