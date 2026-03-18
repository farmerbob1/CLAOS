/*
 * CLAOS — Claude Assisted Operating System
 * panic.h — Kernel panic handler
 */

#ifndef CLAOS_PANIC_H
#define CLAOS_PANIC_H

#include "idt.h"

/* Human-readable exception names */
extern const char* exception_names[32];

/* Initialize exception handlers that produce nice panic screens */
void panic_init(void);

/* Manually trigger a kernel panic with a message */
void kernel_panic(const char* message);

#endif /* CLAOS_PANIC_H */
