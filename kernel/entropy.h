/*
 * CLAOS — Claude Assisted Operating System
 * entropy.h — Entropy pool for TLS randomness
 */

#ifndef CLAOS_ENTROPY_H
#define CLAOS_ENTROPY_H

#include "types.h"

/* Initialize the entropy pool */
void entropy_init(void);

/* Add entropy from a hardware event (timer tick, key press, etc.) */
void entropy_add(uint32_t value);

/* Fill a buffer with pseudo-random bytes.
 * Uses collected entropy + RDTSC + a simple PRNG. */
void entropy_get_bytes(void* buf, size_t len);

#endif /* CLAOS_ENTROPY_H */
