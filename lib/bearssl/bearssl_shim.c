/*
 * CLAOS — Claude Assisted Operating System
 * bearssl_shim.c — BearSSL compatibility layer
 *
 * Provides the system entropy seeder that BearSSL needs for TLS.
 */

#include <stddef.h>
#include <stdint.h>
#include "bearssl.h"
#include "entropy.h"

/*
 * BearSSL calls br_prng_seeder_system() to get a function that seeds
 * its PRNG with system entropy. We provide our entropy pool.
 */
static int claos_seeder(const br_prng_class **ctx) {
    unsigned char seed[32];
    entropy_get_bytes(seed, sizeof(seed));

    /* Inject entropy into BearSSL's PRNG */
    if (ctx && *ctx) {
        (*ctx)->update(ctx, seed, sizeof(seed));
    }

    return 1;
}

br_prng_seeder br_prng_seeder_system(const char **name) {
    if (name) {
        *name = "CLAOS-entropy";
    }
    return claos_seeder;
}
