/*
 * CLAOS — Claude Assisted Operating System
 * vmm.h — Virtual Memory Manager (Paging)
 *
 * Sets up x86 paging: a page directory with page tables that
 * identity-map the kernel and provide virtual address translation.
 * Each page is 4KB, each page table covers 4MB, the full page
 * directory covers 4GB.
 */

#ifndef CLAOS_VMM_H
#define CLAOS_VMM_H

#include "types.h"

/* Page directory/table entry flags */
#define PTE_PRESENT    0x01
#define PTE_WRITABLE   0x02
#define PTE_USER       0x04
#define PTE_WRITETHROUGH 0x08
#define PTE_NOCACHE    0x10
#define PTE_ACCESSED   0x20
#define PTE_DIRTY      0x40
#define PTE_4MB        0x80    /* Page Size bit (in page directory) */

/* Initialize paging: identity-map kernel space, enable CR0.PG */
void vmm_init(void);

/* Map a virtual address to a physical address */
void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags);

/* Unmap a virtual page */
void vmm_unmap_page(uint32_t virt);

/* Split a 4MB PSE page into 4KB pages (needed for cache control) */
void vmm_split_4mb_page(uint32_t virt_4mb_aligned);

/* Map the VESA framebuffer with NOCACHE flags. Returns 0 on success. */
int vmm_map_framebuffer(uint32_t fb_phys, uint32_t size);

#endif /* CLAOS_VMM_H */
