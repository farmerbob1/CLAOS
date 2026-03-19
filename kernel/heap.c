/*
 * CLAOS — Claude Assisted Operating System
 * heap.c — Kernel Heap Allocator
 *
 * Two-tier allocator:
 *   1. Slab allocator for small allocations (≤256 bytes) — O(1) alloc/free
 *   2. Doubly-linked free list for large allocations — next-fit search,
 *      O(1) coalescing via prev pointer
 *
 * Public API is unchanged: kmalloc(size) / kfree(ptr).
 */

#include "heap.h"
#include "pmm.h"
#include "string.h"
#include "io.h"

/* ═══════════════════════════════════════════════════════════════════
 * Block header — doubly linked for O(1) backward coalescing
 * ═══════════════════════════════════════════════════════════════════ */
struct block_header {
    uint32_t size;              /* Data area size (not including header) */
    bool     is_free;
    uint8_t  pad[3];            /* Alignment padding */
    struct block_header* next;  /* Next block by address */
    struct block_header* prev;  /* Previous block by address */
};

#define HEADER_SIZE sizeof(struct block_header)  /* 16 bytes */

static struct block_header* heap_start = NULL;
static struct block_header* next_fit_hint = NULL;  /* Start search from last success */
static uint32_t heap_total_size = 0;
static uint32_t heap_base_addr = 0;
static uint32_t heap_end_addr = 0;

/* ═══════════════════════════════════════════════════════════════════
 * Slab allocator for small allocations (≤256 bytes)
 * ═══════════════════════════════════════════════════════════════════ */

#define SLAB_NUM_CLASSES  5
#define SLAB_MAX_SIZE     256
#define SLAB_PAGE_SIZE    4096

/* Size classes: 16, 32, 64, 128, 256 bytes */
static const uint16_t slab_sizes[SLAB_NUM_CLASSES] = { 16, 32, 64, 128, 256 };

/* Slab page header — sits at the start of each 4KB slab page */
typedef struct slab_page {
    struct slab_page* next_page;  /* Next page in this size class */
    uint16_t size_class;          /* Size of each block */
    uint16_t free_count;          /* Number of free blocks */
    void*    free_list;           /* Head of free block chain */
} slab_page_t;

/* Per-class: linked list of slab pages */
static slab_page_t* slab_pages[SLAB_NUM_CLASSES] = { NULL };

/* Track slab page address range for kfree routing */
#define MAX_SLAB_PAGES 128
static uint32_t slab_page_addrs[MAX_SLAB_PAGES];
static int slab_page_count = 0;

/* Get size class index for a given size, or -1 if too large */
static int slab_class_for_size(size_t size) {
    for (int i = 0; i < SLAB_NUM_CLASSES; i++) {
        if (size <= slab_sizes[i]) return i;
    }
    return -1;
}

/* Check if a pointer belongs to a slab page */
static bool is_slab_ptr(void* ptr) {
    uint32_t addr = (uint32_t)ptr;
    uint32_t page_addr = addr & ~(SLAB_PAGE_SIZE - 1);
    for (int i = 0; i < slab_page_count; i++) {
        if (slab_page_addrs[i] == page_addr) return true;
    }
    return false;
}

/* Allocate a new slab page for a size class */
static slab_page_t* slab_new_page(int cls) {
    uint32_t page_phys = pmm_alloc_page();
    if (!page_phys) return NULL;

    /* Track this page address */
    if (slab_page_count < MAX_SLAB_PAGES) {
        slab_page_addrs[slab_page_count++] = page_phys;
    }

    slab_page_t* page = (slab_page_t*)page_phys;
    page->next_page = slab_pages[cls];
    page->size_class = slab_sizes[cls];
    page->free_list = NULL;
    page->free_count = 0;

    /* Carve blocks from the page (after the header) */
    uint32_t block_size = slab_sizes[cls];
    uint32_t data_start = page_phys + sizeof(slab_page_t);
    /* Align data_start to block_size */
    data_start = (data_start + block_size - 1) & ~(block_size - 1);

    uint32_t offset = data_start;
    while (offset + block_size <= page_phys + SLAB_PAGE_SIZE) {
        /* Each free block stores a pointer to the next free block */
        void** block = (void**)offset;
        *block = page->free_list;
        page->free_list = block;
        page->free_count++;
        offset += block_size;
    }

    slab_pages[cls] = page;
    return page;
}

/* Slab allocate */
static void* slab_alloc(int cls) {
    /* Find a page with free blocks */
    slab_page_t* page = slab_pages[cls];
    while (page && page->free_count == 0) {
        page = page->next_page;
    }

    /* No page with space — allocate a new one */
    if (!page) {
        page = slab_new_page(cls);
        if (!page) return NULL;
    }

    /* Pop from free list — O(1) */
    void** block = (void**)page->free_list;
    page->free_list = *block;
    page->free_count--;
    return (void*)block;
}

/* Slab free */
static void slab_free(void* ptr) {
    /* Find the page this block belongs to */
    uint32_t page_addr = (uint32_t)ptr & ~(SLAB_PAGE_SIZE - 1);
    slab_page_t* page = (slab_page_t*)page_addr;

    /* Push to free list — O(1) */
    void** block = (void**)ptr;
    *block = page->free_list;
    page->free_list = block;
    page->free_count++;
}

/* ═══════════════════════════════════════════════════════════════════
 * Main heap allocator (doubly-linked free list, next-fit)
 * ═══════════════════════════════════════════════════════════════════ */

void heap_init(uint32_t start, uint32_t size) {
    /* Align start to 16 bytes */
    if (start & 0xF) {
        uint32_t adjust = 16 - (start & 0xF);
        start += adjust;
        size -= adjust;
    }

    /* Create one big free block spanning the entire heap */
    heap_start = (struct block_header*)start;
    heap_start->size = size - HEADER_SIZE;
    heap_start->is_free = true;
    heap_start->next = NULL;
    heap_start->prev = NULL;
    heap_total_size = size;
    heap_base_addr = start;
    heap_end_addr = start + size;
    next_fit_hint = heap_start;

    serial_print("[HEAP] Initialized\n");
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Align to 16 bytes */
    size = (size + 15) & ~15;

    /* Small allocations → slab allocator (O(1)) */
    if (size <= SLAB_MAX_SIZE) {
        int cls = slab_class_for_size(size);
        if (cls >= 0) {
            void* p = slab_alloc(cls);
            if (p) return p;
            /* Fall through to free-list if slab fails */
        }
    }

    /* Large allocations → next-fit free list search */
    struct block_header* start = next_fit_hint ? next_fit_hint : heap_start;
    struct block_header* curr = start;
    bool wrapped = false;

    while (curr) {
        if (curr->is_free && curr->size >= size) {
            /* Split if enough leftover for header + 16 bytes */
            if (curr->size >= size + HEADER_SIZE + 16) {
                struct block_header* new_block =
                    (struct block_header*)((uint8_t*)curr + HEADER_SIZE + size);
                new_block->size = curr->size - size - HEADER_SIZE;
                new_block->is_free = true;
                new_block->next = curr->next;
                new_block->prev = curr;

                if (curr->next) curr->next->prev = new_block;
                curr->size = size;
                curr->next = new_block;
            }

            curr->is_free = false;
            next_fit_hint = curr->next ? curr->next : heap_start;
            return (void*)((uint8_t*)curr + HEADER_SIZE);
        }

        curr = curr->next;

        /* Wrap around once if we started mid-list */
        if (!curr && !wrapped && start != heap_start) {
            curr = heap_start;
            wrapped = true;
        }
        if (wrapped && curr == start) break;
    }

    serial_print("[HEAP] kmalloc: out of memory!\n");
    return NULL;
}

void* kmalloc_aligned(size_t size, size_t align) {
    void* raw = kmalloc(size + align + sizeof(void*));
    if (!raw) return NULL;

    uint32_t addr = (uint32_t)raw + sizeof(void*);
    uint32_t aligned = (addr + align - 1) & ~(align - 1);
    ((void**)aligned)[-1] = raw;
    return (void*)aligned;
}

void kfree_aligned(void* ptr) {
    if (!ptr) return;
    void* raw = ((void**)ptr)[-1];
    kfree(raw);
}

void kfree(void* ptr) {
    if (!ptr) return;

    /* Check if it's a slab allocation */
    if (is_slab_ptr(ptr)) {
        slab_free(ptr);
        return;
    }

    /* Validate within heap bounds */
    uint32_t addr = (uint32_t)ptr;
    if (addr < heap_base_addr + HEADER_SIZE || addr >= heap_end_addr) {
        serial_print("[HEAP] kfree: pointer outside heap bounds!\n");
        return;
    }

    struct block_header* block =
        (struct block_header*)((uint8_t*)ptr - HEADER_SIZE);
    block->is_free = true;

    /* Coalesce forward — O(1) via next pointer */
    if (block->next && block->next->is_free) {
        struct block_header* absorbed = block->next;
        block->size += HEADER_SIZE + absorbed->size;
        block->next = absorbed->next;
        if (absorbed->next) absorbed->next->prev = block;
        /* Fix hint if it pointed to the absorbed block */
        if (next_fit_hint == absorbed) next_fit_hint = block;
    }

    /* Coalesce backward — O(1) via prev pointer (was O(n) before!) */
    if (block->prev && block->prev->is_free) {
        struct block_header* absorber = block->prev;
        absorber->size += HEADER_SIZE + block->size;
        absorber->next = block->next;
        if (block->next) block->next->prev = absorber;
        /* Fix hint if it pointed to the absorbed block */
        if (next_fit_hint == block) next_fit_hint = absorber;
    }
}

size_t kmalloc_usable_size(void* ptr) {
    if (!ptr) return 0;
    if (is_slab_ptr(ptr)) {
        uint32_t page_addr = (uint32_t)ptr & ~(SLAB_PAGE_SIZE - 1);
        slab_page_t* page = (slab_page_t*)page_addr;
        return page->size_class;
    }
    struct block_header* block =
        (struct block_header*)((uint8_t*)ptr - HEADER_SIZE);
    return block->size;
}

uint32_t heap_get_used(void) {
    uint32_t used = 0;
    struct block_header* curr = heap_start;
    while (curr) {
        if (!curr->is_free)
            used += curr->size + HEADER_SIZE;
        curr = curr->next;
    }
    return used;
}

uint32_t heap_get_free(void) {
    uint32_t free_space = 0;
    struct block_header* curr = heap_start;
    while (curr) {
        if (curr->is_free)
            free_space += curr->size;
        curr = curr->next;
    }
    return free_space;
}
