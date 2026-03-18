/*
 * CLAOS — Claude Assisted Operating System
 * panic_handler.h — Panic → Claude Integration
 */

#ifndef CLAOS_PANIC_HANDLER_H
#define CLAOS_PANIC_HANDLER_H

#include "idt.h"

/* Send a crash report to Claude and display the diagnosis.
 * Called from the panic screen after displaying the register dump.
 * `reason` is the fault description, `regs` is the register state. */
void panic_ask_claude(const char* reason, struct registers* regs);

#endif /* CLAOS_PANIC_HANDLER_H */
