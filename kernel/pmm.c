/*
 * CLAOS — Claude Assisted Operating System
 * pmm.c — Physical Memory Manager
 *
 * Discovers physical RAM via the BIOS E820 memory map (stored by Stage 2
 * at address 0x8000) and manages it with a bitmap allocator.
 *
 * Each bit in the bitmap represents one 4KB page:
 *   bit = 0 → page is free
 *   bit = 1 → page is in use
 *
 * We mark the first 1MB and the kernel itself as used, since that memory
 * contains the bootloader, BIOS data, VGA buffer, and our own code.
 */

#include "pmm.h"
#include "string.h"
#include "io.h"
#include "vga.h"

/* E820 memory map is stored by Stage 2 at these addresses */
#define E820_COUNT_ADDR  0x8000     /* uint16_t: number of entries */
#define E820_ENTRIES_ADDR 0x8004    /* struct e820_entry[]: the entries */

/* Bitmap — we support up to 128MB of RAM (32768 pages = 4096 bytes of bitmap)
 * This could be made dynamic, but 128MB is plenty for a toy OS. */
#define MAX_PAGES 32768
static uint8_t bitmap[MAX_PAGES / 8];

/* Stats */
static uint32_t total_memory = 0;       /* Total detected RAM in bytes */
static uint32_t total_pages = 0;        /* Total manageable pages */
static uint32_t used_pages = 0;         /* Currently allocated pages */

/* Set a page as used in the bitmap */
static void pmm_set_page(uint32_t page) {
    if (page >= MAX_PAGES) return;
    bitmap[page / 8] |= (1 << (page % 8));
}

/* Set a page as free in the bitmap */
static void pmm_clear_page(uint32_t page) {
    if (page >= MAX_PAGES) return;
    bitmap[page / 8] &= ~(1 << (page % 8));
}

/* Check if a page is used */
static bool pmm_test_page(uint32_t page) {
    if (page >= MAX_PAGES) return true;  /* Out of range = treat as used */
    return bitmap[page / 8] & (1 << (page % 8));
}

void pmm_init(uint32_t kernel_end) {
    /* Read E820 memory map from where Stage 2 stored it */
    uint16_t entry_count = *(uint16_t*)E820_COUNT_ADDR;
    struct e820_entry* entries = (struct e820_entry*)E820_ENTRIES_ADDR;

    /* Start by marking ALL pages as used (safe default) */
    memset(bitmap, 0xFF, sizeof(bitmap));
    used_pages = MAX_PAGES;

    /* Find total memory and free usable regions */
    for (int i = 0; i < entry_count; i++) {
        uint32_t base = (uint32_t)entries[i].base;
        uint32_t length = (uint32_t)entries[i].length;
        uint32_t end = base + length;

        /* Track total memory (all types) */
        if (end > total_memory)
            total_memory = end;

        /* Only free pages in usable regions */
        if (entries[i].type != E820_USABLE)
            continue;

        /* Free each page in this usable region */
        uint32_t page_start = (base + PAGE_SIZE - 1) / PAGE_SIZE;  /* Round up */
        uint32_t page_end = end / PAGE_SIZE;                        /* Round down */

        for (uint32_t p = page_start; p < page_end && p < MAX_PAGES; p++) {
            pmm_clear_page(p);
            used_pages--;
        }
    }

    total_pages = total_memory / PAGE_SIZE;
    if (total_pages > MAX_PAGES)
        total_pages = MAX_PAGES;

    /* Reserve the first 1MB (pages 0-255).
     * This region contains: BIOS data, IVT, bootloader, VGA buffer,
     * our kernel code, and the E820 map itself. Don't touch it. */
    for (uint32_t p = 0; p < 256; p++) {
        if (!pmm_test_page(p)) {
            pmm_set_page(p);
            used_pages++;
        }
    }

    /* Also reserve everything up to kernel_end */
    uint32_t kernel_end_page = (kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t p = 256; p < kernel_end_page && p < MAX_PAGES; p++) {
        if (!pmm_test_page(p)) {
            pmm_set_page(p);
            used_pages++;
        }
    }

    serial_print("[PMM] Memory map parsed: ");
    /* Can't easily print numbers to serial without a helper, but VGA works */
}

uint32_t pmm_alloc_page(void) {
    /* Linear scan for the first free page */
    for (uint32_t i = 0; i < MAX_PAGES / 8; i++) {
        if (bitmap[i] == 0xFF)
            continue;   /* All 8 pages in this byte are used */

        /* Found a byte with at least one free bit */
        for (int bit = 0; bit < 8; bit++) {
            if (!(bitmap[i] & (1 << bit))) {
                uint32_t page = i * 8 + bit;
                pmm_set_page(page);
                used_pages++;
                return page * PAGE_SIZE;
            }
        }
    }

    /* Out of memory! */
    return 0;
}

void pmm_free_page(uint32_t phys_addr) {
    uint32_t page = phys_addr / PAGE_SIZE;
    if (page < MAX_PAGES && pmm_test_page(page)) {
        pmm_clear_page(page);
        used_pages--;
    }
}

bool pmm_reserve_page(uint32_t phys_addr) {
    uint32_t page = phys_addr / PAGE_SIZE;
    if (page >= MAX_PAGES) return false;
    if (pmm_test_page(page)) return false;  /* Already reserved */
    pmm_set_page(page);
    used_pages++;
    return true;
}

uint32_t pmm_get_total_memory(void) {
    return total_memory;
}

uint32_t pmm_get_free_pages(void) {
    return total_pages > used_pages ? total_pages - used_pages : 0;
}

uint32_t pmm_get_used_pages(void) {
    return used_pages;
}
