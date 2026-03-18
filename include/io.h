/*
 * CLAOS — Claude Assisted Operating System
 * io.h — x86 port I/O helper functions
 *
 * These inline assembly wrappers let C code talk directly to hardware
 * via the x86 IN/OUT instructions. Every driver needs these.
 */

#ifndef CLAOS_IO_H
#define CLAOS_IO_H

#include "types.h"

/* Write a byte to an I/O port */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Read a byte from an I/O port */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Write a 16-bit word to an I/O port */
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* Read a 16-bit word from an I/O port */
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Write a 32-bit dword to an I/O port */
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* Read a 32-bit dword from an I/O port */
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Small delay — some hardware needs a brief pause between I/O ops.
 * Writing to port 0x80 (POST diagnostic port) is a safe ~1µs delay. */
static inline void io_wait(void) {
    outb(0x80, 0);
}

/* Serial port (COM1) debug output — writes to port 0x3F8.
 * Output is captured by QEMU's -serial flag. */
#define SERIAL_PORT 0x3F8

static inline void serial_init(void) {
    outb(SERIAL_PORT + 1, 0x00);  /* Disable interrupts */
    outb(SERIAL_PORT + 3, 0x80);  /* Enable DLAB */
    outb(SERIAL_PORT + 0, 0x03);  /* Divisor low byte (38400 baud) */
    outb(SERIAL_PORT + 1, 0x00);  /* Divisor high byte */
    outb(SERIAL_PORT + 3, 0x03);  /* 8 bits, no parity, 1 stop bit */
    outb(SERIAL_PORT + 2, 0xC7);  /* Enable FIFO */
    outb(SERIAL_PORT + 4, 0x0B);  /* IRQs enabled, RTS/DSR set */
}

static inline void serial_putchar(char c) {
    while ((inb(SERIAL_PORT + 5) & 0x20) == 0);  /* Wait for transmit empty */
    outb(SERIAL_PORT, c);
}

static inline void serial_print(const char* str) {
    while (*str) {
        if (*str == '\n') serial_putchar('\r');
        serial_putchar(*str++);
    }
}

#endif /* CLAOS_IO_H */
