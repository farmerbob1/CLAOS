/*
 * CLAOS — Claude Assisted Operating System
 * vga.h — VGA text mode driver interface
 *
 * The VGA text buffer lives at physical address 0xB8000.
 * It's a 80x25 grid of 16-bit entries: low byte = ASCII char,
 * high byte = color attribute (4 bits bg + 4 bits fg).
 */

#ifndef CLAOS_VGA_H
#define CLAOS_VGA_H

#include "types.h"

/* VGA text mode dimensions */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

/* VGA color codes (4 bits each) */
enum vga_color {
    VGA_BLACK        = 0,
    VGA_BLUE         = 1,
    VGA_GREEN        = 2,
    VGA_CYAN         = 3,
    VGA_RED          = 4,
    VGA_MAGENTA      = 5,
    VGA_BROWN        = 6,
    VGA_LIGHT_GREY   = 7,
    VGA_DARK_GREY    = 8,
    VGA_LIGHT_BLUE   = 9,
    VGA_LIGHT_GREEN  = 10,
    VGA_LIGHT_CYAN   = 11,
    VGA_LIGHT_RED    = 12,
    VGA_LIGHT_MAGENTA= 13,
    VGA_YELLOW       = 14,
    VGA_WHITE        = 15,
};

/* Combine foreground and background into an attribute byte */
#define VGA_COLOR(fg, bg) ((uint8_t)((bg) << 4 | (fg)))

/* Combine a character and color attribute into a VGA entry */
#define VGA_ENTRY(c, color) ((uint16_t)((uint16_t)(color) << 8 | (uint16_t)(c)))

/* Initialize the VGA driver (clears screen, resets cursor) */
void vga_init(void);

/* Clear the entire screen */
void vga_clear(void);

/* Print a single character at the current cursor position */
void vga_putchar(char c);

/* Print a null-terminated string */
void vga_print(const char* str);

/* Print a 32-bit value in hexadecimal (e.g., "0xDEADBEEF") */
void vga_print_hex(uint32_t value);

/* Print a decimal number */
void vga_print_dec(uint32_t value);

/* Set the current text color */
void vga_set_color(uint8_t fg, uint8_t bg);

/* Move the hardware cursor to the current position */
void vga_update_cursor(void);

#endif /* CLAOS_VGA_H */
