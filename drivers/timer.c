/*
 * CLAOS — Claude Assisted Operating System
 * timer.c — Programmable Interval Timer (PIT) driver
 *
 * The PIT oscillates at 1,193,182 Hz. We program it to divide that
 * down to our desired tick rate. Each tick fires IRQ0.
 *
 * PIT I/O ports:
 *   0x40 = Channel 0 data (the one wired to IRQ0)
 *   0x43 = Command register
 */

#include "timer.h"
#include "io.h"
#include "vga.h"

#define PIT_FREQ     1193182     /* Base oscillator frequency in Hz */
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

/* Tick counter — incremented every IRQ0 */
static volatile uint32_t ticks = 0;

/* The configured tick rate (saved for time calculations) */
static uint32_t tick_frequency = 0;

/* Called from our IRQ0 handler (see irq.c) */
void timer_handler(void) {
    ticks++;
}

/* Configure the PIT to fire at `frequency` Hz */
void timer_init(uint32_t frequency) {
    tick_frequency = frequency;
    ticks = 0;

    /* Calculate the divisor: PIT_FREQ / desired_frequency */
    uint32_t divisor = PIT_FREQ / frequency;

    /* Command byte: Channel 0, lobyte/hibyte access, rate generator (mode 2) */
    outb(PIT_COMMAND, 0x36);

    /* Send the divisor (low byte first, then high byte) */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

/* Get raw tick count */
uint32_t timer_get_ticks(void) {
    return ticks;
}

/* Get approximate uptime in seconds */
uint32_t timer_get_uptime(void) {
    if (tick_frequency == 0) return 0;
    return ticks / tick_frequency;
}

/* Busy-wait for approximately `ms` milliseconds */
void timer_wait(uint32_t ms) {
    uint32_t target = ticks + (ms * tick_frequency) / 1000;
    while (ticks < target) {
        __asm__ volatile ("hlt");
    }
}
