/*
 * CLAOS — Claude Assisted Operating System
 * timer.h — Programmable Interval Timer (PIT) driver interface
 *
 * The PIT (Intel 8253/8254) generates periodic interrupts on IRQ0.
 * We use it for system timekeeping and (later) preemptive scheduling.
 */

#ifndef CLAOS_TIMER_H
#define CLAOS_TIMER_H

#include "types.h"

/* Initialize the PIT to fire at the given frequency (Hz) */
void timer_init(uint32_t frequency);

/* Get the number of ticks since boot */
uint32_t timer_get_ticks(void);

/* Get approximate seconds since boot */
uint32_t timer_get_uptime(void);

/* Busy-wait for approximately `ms` milliseconds */
void timer_wait(uint32_t ms);

#endif /* CLAOS_TIMER_H */
