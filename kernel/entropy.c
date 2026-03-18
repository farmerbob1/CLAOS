/*
 * CLAOS — Claude Assisted Operating System
 * entropy.c — Entropy pool for TLS randomness
 *
 * Collects entropy from hardware sources (timer ticks, RDTSC, keyboard
 * timing) and provides pseudo-random bytes for TLS key generation.
 *
 * This isn't cryptographically rigorous, but it's non-deterministic
 * enough for a toy OS. We mix hardware entropy into a simple state
 * using xorshift and RDTSC.
 */

#include "entropy.h"
#include "timer.h"
#include "io.h"
#include "string.h"

/* Entropy pool state */
static uint32_t pool[8];
static uint32_t pool_index = 0;
static uint32_t mix_counter = 0;

/* Read the CPU's Time Stamp Counter (RDTSC) — counts CPU cycles since reset.
 * This is our best source of high-resolution non-determinism. */
static uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* xorshift32 PRNG — fast, decent statistical properties */
static uint32_t xorshift32(uint32_t state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void entropy_init(void) {
    /* Seed the pool with RDTSC and timer ticks */
    uint64_t tsc = rdtsc();
    pool[0] = (uint32_t)tsc;
    pool[1] = (uint32_t)(tsc >> 32);
    pool[2] = timer_get_ticks();
    pool[3] = 0xDEADBEEF;  /* Constant salt */
    pool[4] = (uint32_t)tsc ^ timer_get_ticks();
    pool[5] = 0xCAFEBABE;
    pool[6] = (uint32_t)(tsc >> 16);
    pool[7] = 0x12345678;

    /* Mix it up */
    for (int i = 0; i < 32; i++) {
        entropy_add((uint32_t)rdtsc());
    }

    serial_print("[ENTROPY] Pool initialized\n");
}

void entropy_add(uint32_t value) {
    /* Mix the new value into the pool using xorshift */
    pool_index = (pool_index + 1) & 7;
    pool[pool_index] ^= value;
    pool[pool_index] = xorshift32(pool[pool_index] + mix_counter);
    mix_counter++;

    /* Cross-mix adjacent entries */
    pool[(pool_index + 3) & 7] ^= pool[pool_index];
}

void entropy_get_bytes(void* buf, size_t len) {
    uint8_t* out = (uint8_t*)buf;

    for (size_t i = 0; i < len; i++) {
        /* Stir in fresh RDTSC on every 4th byte */
        if ((i & 3) == 0) {
            entropy_add((uint32_t)rdtsc());
        }

        /* Generate a byte from the pool */
        uint32_t val = pool[i & 7] ^ pool[(i + 3) & 7];
        val = xorshift32(val + mix_counter + i);
        pool[i & 7] = val;

        out[i] = (uint8_t)(val & 0xFF);
    }
}
