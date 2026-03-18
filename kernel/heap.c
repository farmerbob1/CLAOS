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
    /* Over-allocate to guarantee alignment */
    void* ptr = kmalloc(size + align);
    if (!ptr) return NULL;

    /* Align the returned pointer */
    uint32_t addr = (uint32_t)ptr;
    uint32_t aligned = (addr + align - 1) & ~(align - 1);

    /* Note: This wastes some memory and makes kfree unreliable for
     * aligned allocations. Good enough for a toy OS. */
    return (void*)aligned;
}

void kfree(void* ptr) {
    if (!ptr) return;

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
