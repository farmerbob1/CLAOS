/*
 * CLAOS — Claude Assisted Operating System
 * types.h — Standard type definitions for a freestanding environment
 *
 * Since we have no libc, we define our own fixed-width types.
 */

#ifndef CLAOS_TYPES_H
#define CLAOS_TYPES_H

/* Fixed-width integer types */
typedef unsigned char      uint8_t;
typedef signed char        int8_t;
typedef unsigned short     uint16_t;
typedef signed short       int16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;
typedef unsigned long long uint64_t;
typedef signed long long   int64_t;

/* Size type — 32-bit on i686 */
typedef uint32_t size_t;

/* Boolean — stdbool.h is available in freestanding mode */
#include <stdbool.h>

/* NULL pointer */
#define NULL ((void*)0)

#endif /* CLAOS_TYPES_H */
