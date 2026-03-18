/*
 * CLAOS — Claude Assisted Operating System
 * vga.c — VGA text mode driver
 *
 * Drives the 80x25 text mode display by writing directly to the VGA
 * text buffer at 0xB8000. Each cell is 2 bytes: character + attribute.
 *
 * Also controls the hardware cursor via VGA I/O ports 0x3D4/0x3D5.
 */

#include "vga.h"
#include "io.h"
#include "string.h"

/* Current cursor position and color state */
static uint16_t* vga_buffer = (uint16_t*)VGA_MEMORY;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);

/* Scroll the screen up by one line when we reach the bottom */
static void vga_scroll(void) {
    if (cursor_y >= VGA_HEIGHT) {
        /* Move all lines up by one */
        memmove(vga_buffer,
                vga_buffer + VGA_WIDTH,
                (VGA_HEIGHT - 1) * VGA_WIDTH * 2);

        /* Clear the last line */
        for (int i = 0; i < VGA_WIDTH; i++) {
            vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + i] =
                VGA_ENTRY(' ', current_color);
        }

        cursor_y = VGA_HEIGHT - 1;
    }
}

/* Move the blinking hardware cursor to match our software cursor */
void vga_update_cursor(void) {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;

    /* VGA cursor location is set via two I/O port writes:
     * Port 0x3D4 selects the register, 0x3D5 writes the value.
     * Register 14 = cursor high byte, Register 15 = cursor low byte. */
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
}

/* Initialize VGA: clear screen, reset cursor to top-left */
void vga_init(void) {
    current_color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

/* Fill the entire screen with spaces */
void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = VGA_ENTRY(' ', current_color);
    }
    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor();
}

/* Print a single character, handling special chars like \n and \t */
void vga_putchar(char c) {
    if (c == '\n') {
        /* Newline: move to start of next line */
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        /* Carriage return: move to start of current line */
        cursor_x = 0;
    } else if (c == '\t') {
        /* Tab: advance to next 8-column boundary */
        cursor_x = (cursor_x + 8) & ~7;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    } else if (c == '\b') {
        /* Backspace: move back one position and clear */
        if (cursor_x > 0) {
            cursor_x--;
            vga_buffer[cursor_y * VGA_WIDTH + cursor_x] =
                VGA_ENTRY(' ', current_color);
        }
    } else {
        /* Regular character: write to buffer and advance */
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] =
            VGA_ENTRY(c, current_color);
        cursor_x++;

        /* Wrap to next line if we hit the right edge */
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    /* Scroll if we went past the bottom */
    vga_scroll();
    vga_update_cursor();
}

/* Print a null-terminated string */
void vga_print(const char* str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

/* Print a 32-bit value in hexadecimal */
void vga_print_hex(uint32_t value) {
    const char hex_chars[] = "0123456789ABCDEF";
    vga_print("0x");
    for (int i = 28; i >= 0; i -= 4) {
        vga_putchar(hex_chars[(value >> i) & 0xF]);
    }
}

/* Print a decimal number */
void vga_print_dec(uint32_t value) {
    if (value == 0) {
        vga_putchar('0');
        return;
    }

    char buf[12];
    int i = 0;
    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    /* Print digits in reverse order */
    while (i > 0) {
        vga_putchar(buf[--i]);
    }
}

/* Set the text color for subsequent output */
void vga_set_color(uint8_t fg, uint8_t bg) {
    current_color = VGA_COLOR(fg, bg);
}
