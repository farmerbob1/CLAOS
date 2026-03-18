/*
 * CLAOS — Claude Assisted Operating System
 * heap.c — Kernel Heap Allocator
 *
 * A simple first-fit free list allocator. Each block (free or allocated)
 * has a header recording its size and status. Free blocks are linked
 * together. When we allocate, we walk the free list for the first block
 * that fits. When we free, we merge adjacent free blocks to reduce
 * fragmentation.
 *
 * This isn't fast or fancy, but it's correct and simple — perfect for
 * a toy OS that just needs to allocate some buffers.
 */

#include "heap.h"
#include "string.h"
#include "io.h"

/* Block header — sits right before every allocation */
struct block_header {
    uint32_t size;              /* Size of the data area (not including header) */
    bool     is_free;
    struct block_header* next;  /* Next block in the list (by address) */
};

#define HEADER_SIZE sizeof(struct block_header)

/* Head of the block list */
static struct block_header* heap_start = NULL;
static uint32_t heap_total_size = 0;
static uint32_t heap_base_addr = 0;  /* Start address for bounds checking */

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
    heap_total_size = size;
    heap_base_addr = start;

    serial_print("[HEAP] Initialized\n");
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Align all allocations to 16 bytes for safety */
    size = (size + 15) & ~15;

    /* First-fit search */
    struct block_header* curr = heap_start;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            /* Found a fit! Split the block if there's enough leftover
             * for another header + at least 16 bytes of data. */
            if (curr->size >= size + HEADER_SIZE + 16) {
                /* Create a new free block after our allocation */
                struct block_header* new_block =
                    (struct block_header*)((uint8_t*)curr + HEADER_SIZE + size);
                new_block->size = curr->size - size - HEADER_SIZE;
                new_block->is_free = true;
                new_block->next = curr->next;

                curr->size = size;
                curr->next = new_block;
            }

            curr->is_free = false;
            /* Return pointer to the data area (right after the header) */
            return (void*)((uint8_t*)curr + HEADER_SIZE);
        }
        curr = curr->next;
    }

    /* No block big enough — out of heap memory */
    serial_print("[HEAP] kmalloc: out of memory!\n");
    return NULL;
}

void* kmalloc_aligned(size_t size, size_t align) {
    /* Over-allocate: we need room for the alignment padding plus a pointer
     * to store the original (unaligned) address so kfree can find it. */
    void* raw = kmalloc(size + align + sizeof(void*));
    if (!raw) return NULL;

    /* Reserve space for the hidden pointer, then align */
    uint32_t addr = (uint32_t)raw + sizeof(void*);
    uint32_t aligned = (addr + align - 1) & ~(align - 1);

    /* Store the original pointer just before the aligned address */
    ((void**)aligned)[-1] = raw;

    return (void*)aligned;
}

void kfree_aligned(void* ptr) {
    if (!ptr) return;
    /* Retrieve the original pointer stored just before the aligned address */
    void* raw = ((void**)ptr)[-1];
    kfree(raw);
}

void kfree(void* ptr) {
    if (!ptr) return;

    /* Validate that ptr is within heap bounds */
    uint32_t addr = (uint32_t)ptr;
    if (addr < heap_base_addr + HEADER_SIZE || addr >= heap_base_addr + heap_total_size) {
        serial_print("[HEAP] kfree: pointer outside heap bounds!\n");
        return;
    }

    /* The header is right before the data pointer */
    struct block_header* block =
        (struct block_header*)((uint8_t*)ptr - HEADER_SIZE);
    block->is_free = true;

    /* Merge with the next block if it's also free (coalesce forward) */
    if (block->next && block->next->is_free) {
        block->size += HEADER_SIZE + block->next->size;
        block->next = block->next->next;
    }

    /* Merge with the previous block if it's free (coalesce backward) */
    struct block_header* prev = heap_start;
    while (prev && prev->next != block) {
        prev = prev->next;
    }
    if (prev && prev->is_free && prev->next == block) {
        prev->size += HEADER_SIZE + block->size;
        prev->next = block->next;
    }
}

size_t kmalloc_usable_size(void* ptr) {
    if (!ptr) return 0;
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
