/*
 * CLAOS — Claude Assisted Operating System
 * console.h — Software Text Console on Framebuffer
 *
 * Emulates a text-mode console on the VESA framebuffer using the
 * embedded 8x16 font. At 1024x768, this gives 128 columns × 48 rows —
 * much larger than VGA's 80×25.
 */

#ifndef CLAOS_CONSOLE_H
#define CLAOS_CONSOLE_H

#include "types.h"

/* Console dimensions (computed from screen size / font size) */
#define CONSOLE_MAX_COLS  128   /* 1024 / 8 */
#define CONSOLE_MAX_ROWS  48    /* 768 / 16 */

/* Initialize the software console (call after fb_init) */
void console_init(void);

/* Write a single character (handles \n, \r, \t, \b) */
void console_putchar(char c);

/* Write a null-terminated string */
void console_print(const char* str);

/* Clear the console */
void console_clear(void);

/* Set foreground and background colors (32-bit ARGB) */
void console_set_color(uint32_t fg, uint32_t bg);

/* Render dirty lines to back buffer and swap to screen */
void console_flush(void);

/* Suppress/allow flushing (for batch boot output) */
void console_set_batch(bool batch);

/* Get current console dimensions */
int console_get_cols(void);
int console_get_rows(void);

#endif /* CLAOS_CONSOLE_H */
