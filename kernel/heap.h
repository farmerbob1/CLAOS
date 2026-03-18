/*
 * CLAOS — Claude Assisted Operating System
 * heap.h — Kernel Heap Allocator
 *
 * Provides kmalloc/kfree for dynamic kernel memory allocation.
 * Uses a simple first-fit free list allocator. Not fancy, but works.
 */

#ifndef CLAOS_HEAP_H
#define CLAOS_HEAP_H

#include "types.h"

/* Initialize the kernel heap starting at the given address */
void heap_init(uint32_t start, uint32_t size);

/* Allocate `size` bytes of kernel memory. Returns NULL on failure. */
void* kmalloc(size_t size);

/* Allocate `size` bytes aligned to `align`. */
void* kmalloc_aligned(size_t size, size_t align);

/* Free a previously allocated block */
void kfree(void* ptr);

/* Get heap usage stats */
uint32_t heap_get_used(void);
uint32_t heap_get_free(void);

#endif /* CLAOS_HEAP_H */
