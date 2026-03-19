/*
 * CLAOS — Claude Assisted Operating System
 * vga.c — VGA text mode driver + framebuffer console bridge
 *
 * Drives the 80x25 text mode display by writing directly to the VGA
 * text buffer at 0xB8000. Each cell is 2 bytes: character + attribute.
 *
 * When VESA mode is active, all output is redirected to the software
 * console (gui/console.c) which renders text on the framebuffer.
 */

#include "vga.h"
#include "io.h"
#include "string.h"
#include "fb.h"
#include "console.h"

/* Current cursor position and color state */
static uint16_t* vga_buffer = (uint16_t*)VGA_MEMORY;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);

/* Framebuffer mode flag */
static bool use_framebuffer = false;

/* VGA 4-bit color → 32-bit ARGB palette */
static const uint32_t vga_palette[16] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
    0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
    0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF,
};

void vga_set_framebuffer_mode(bool enabled) {
    use_framebuffer = enabled;
}

static void vga_scroll(void) {
    if (cursor_y >= VGA_HEIGHT) {
        memmove(vga_buffer, vga_buffer + VGA_WIDTH,
                (VGA_HEIGHT - 1) * VGA_WIDTH * 2);
        for (int i = 0; i < VGA_WIDTH; i++)
            vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = VGA_ENTRY(' ', current_color);
        cursor_y = VGA_HEIGHT - 1;
    }
}

void vga_update_cursor(void) {
    if (use_framebuffer) { console_flush(); return; }
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 14); outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15); outb(0x3D5, (uint8_t)(pos & 0xFF));
}

void vga_init(void) {
    current_color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

void vga_clear(void) {
    if (use_framebuffer) { console_clear(); console_flush(); return; }
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga_buffer[i] = VGA_ENTRY(' ', current_color);
    cursor_x = 0; cursor_y = 0;
    vga_update_cursor();
}

void vga_putchar(char c) {
    if (use_framebuffer) {
        console_putchar(c);
        if (c == '\n') console_flush();
        return;
    }
    if (c == '\n') { cursor_x = 0; cursor_y++; }
    else if (c == '\r') { cursor_x = 0; }
    else if (c == '\t') { cursor_x = (cursor_x + 8) & ~7; if (cursor_x >= VGA_WIDTH) { cursor_x = 0; cursor_y++; } }
    else if (c == '\b') { if (cursor_x > 0) { cursor_x--; vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = VGA_ENTRY(' ', current_color); } }
    else { vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = VGA_ENTRY(c, current_color); cursor_x++; if (cursor_x >= VGA_WIDTH) { cursor_x = 0; cursor_y++; } }
    vga_scroll();
    vga_update_cursor();
}

void vga_print(const char* str) {
    if (use_framebuffer) { console_print(str); console_flush(); return; }
    while (*str) vga_putchar(*str++);
}

void vga_print_hex(uint32_t value) {
    const char hex_chars[] = "0123456789ABCDEF";
    vga_print("0x");
    for (int i = 28; i >= 0; i -= 4) vga_putchar(hex_chars[(value >> i) & 0xF]);
    if (use_framebuffer) console_flush();
}

void vga_print_dec(uint32_t value) {
    if (value == 0) { vga_putchar('0'); if (use_framebuffer) console_flush(); return; }
    char buf[12]; int i = 0;
    while (value > 0) { buf[i++] = '0' + (value % 10); value /= 10; }
    while (i > 0) vga_putchar(buf[--i]);
    if (use_framebuffer) console_flush();
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    current_color = VGA_COLOR(fg, bg);
    if (use_framebuffer) console_set_color(vga_palette[fg & 0x0F], vga_palette[bg & 0x0F]);
}
