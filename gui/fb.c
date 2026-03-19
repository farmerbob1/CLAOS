/*
 * CLAOS — Claude Assisted Operating System
 * fb.c — Framebuffer Driver (VESA VBE)
 *
 * Manages the VESA framebuffer with double buffering. All drawing
 * primitives write to the back buffer; fb_swap() copies it to the
 * hardware framebuffer for display.
 *
 * The back buffer is a static 3MB BSS array — zero cost in binary size,
 * guaranteed contiguous, and automatically reserved by the PMM since
 * it sits before _kernel_end.
 */

#include "fb.h"
#include "font.h"
#include "string.h"
#include "io.h"

/* Back buffer — 1024×768×4 = 3,145,728 bytes in BSS.
 * Not stored in the binary (BSS is zero-filled at load). */
static uint32_t back_buffer[1024 * 768] __attribute__((aligned(4096)));

/* Scratch buffer for 24bpp row conversion (max 1024 pixels × 3 bytes = 3072) */
static uint8_t row_buf_24[1024 * 3];

/* Framebuffer state — in .data section so fb.active is guaranteed false
 * before BSS zeroing completes (BSS may contain garbage at early boot). */
static fb_info_t fb __attribute__((section(".data"))) = { 0 };

/* Optimized 32-bit copy using x86 string instructions.
 * Copies 'count' dwords (4 bytes each). ~4x faster than byte copy. */
static inline void memcpy32(void* dest, const void* src, uint32_t count) {
    void* d = dest;
    const void* s = src;
    __asm__ volatile (
        "rep movsl"
        : "+D"(d), "+S"(s), "+c"(count)
        :: "memory"
    );
}

/* Optimized 32-bit fill — fill 'count' dwords with 'value' */
static inline void memset32(void* dest, uint32_t value, uint32_t count) {
    void* d = dest;
    __asm__ volatile (
        "rep stosl"
        : "+D"(d), "+c"(count)
        : "a"(value)
        : "memory"
    );
}

bool fb_init(void) {
    uint8_t vbe_status = *(volatile uint8_t*)VBE_STATUS_ADDR;
    if (vbe_status != 1) {
        fb.active = false;
        serial_print("[FB] No VESA mode (text mode fallback)\n");
        return false;
    }

    fb.framebuffer = (uint32_t*)(*(volatile uint32_t*)VBE_FB_ADDR);
    fb.backbuffer  = back_buffer;
    fb.width       = *(volatile uint16_t*)VBE_WIDTH_ADDR;
    fb.height      = *(volatile uint16_t*)VBE_HEIGHT_ADDR;
    fb.pitch       = *(volatile uint16_t*)VBE_PITCH_ADDR;
    fb.bpp         = *(volatile uint8_t*)VBE_BPP_ADDR;
    fb.active      = true;

    /* Clear to black and display */
    fb_clear(FB_BLACK);
    fb_swap();

    serial_print("[FB] VESA framebuffer initialized\n");
    return true;
}

const fb_info_t* fb_get_info(void) {
    return &fb;
}

bool fb_is_active(void) {
    return fb.active;
}

void fb_swap(void) {
    if (!fb.active) return;

    uint8_t* dst = (uint8_t*)fb.framebuffer;
    uint32_t* src = fb.backbuffer;

    if (fb.bpp == 32) {
        /* 32bpp: direct 4-byte pixel copy */
        for (int y = 0; y < fb.height; y++) {
            memcpy32(dst, src, fb.width);
            dst += fb.pitch;
            src += fb.width;
        }
    } else if (fb.bpp == 24) {
        /* 24bpp: convert ARGB→BGR into cached scratch buffer per row,
         * then bulk-copy to the uncached framebuffer. This is much faster
         * than writing 3 bytes at a time to MMIO memory. */
        for (int y = 0; y < fb.height; y++) {
            uint8_t* rp = row_buf_24;
            for (int x = 0; x < fb.width; x++) {
                uint32_t pixel = src[x];
                rp[0] = (uint8_t)(pixel);
                rp[1] = (uint8_t)(pixel >> 8);
                rp[2] = (uint8_t)(pixel >> 16);
                rp += 3;
            }
            /* Bulk copy the converted row to framebuffer */
            memcpy(dst, row_buf_24, fb.width * 3);
            dst += fb.pitch;
            src += fb.width;
        }
    }
}

void fb_clear(uint32_t color) {
    if (!fb.active) return;
    memset32(fb.backbuffer, color, fb.width * fb.height);
}

void fb_pixel(int x, int y, uint32_t color) {
    if (!fb.active) return;
    if (x < 0 || x >= fb.width || y < 0 || y >= fb.height) return;
    fb.backbuffer[y * fb.width + x] = color;
}

uint32_t fb_get_pixel(int x, int y) {
    if (!fb.active) return 0;
    if (x < 0 || x >= fb.width || y < 0 || y >= fb.height) return 0;
    return fb.backbuffer[y * fb.width + x];
}

void fb_hline(int x, int y, int w, uint32_t color) {
    if (!fb.active) return;
    if (y < 0 || y >= fb.height) return;

    /* Clamp to screen bounds */
    int x1 = x;
    int x2 = x + w;
    if (x1 < 0) x1 = 0;
    if (x2 > fb.width) x2 = fb.width;
    if (x1 >= x2) return;

    memset32(&fb.backbuffer[y * fb.width + x1], color, x2 - x1);
}

void fb_vline(int x, int y, int h, uint32_t color) {
    if (!fb.active) return;
    if (x < 0 || x >= fb.width) return;

    int y1 = y;
    int y2 = y + h;
    if (y1 < 0) y1 = 0;
    if (y2 > fb.height) y2 = fb.height;

    for (int py = y1; py < y2; py++) {
        fb.backbuffer[py * fb.width + x] = color;
    }
}

void fb_rect(int x, int y, int w, int h, uint32_t color) {
    if (!fb.active) return;

    /* Clamp to screen */
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = x + w > fb.width  ? fb.width  : x + w;
    int y2 = y + h > fb.height ? fb.height : y + h;
    if (x1 >= x2 || y1 >= y2) return;

    int row_w = x2 - x1;
    for (int py = y1; py < y2; py++) {
        memset32(&fb.backbuffer[py * fb.width + x1], color, row_w);
    }
}

void fb_rect_outline(int x, int y, int w, int h, uint32_t color) {
    if (!fb.active) return;
    fb_hline(x, y, w, color);              /* Top */
    fb_hline(x, y + h - 1, w, color);      /* Bottom */
    fb_vline(x, y, h, color);              /* Left */
    fb_vline(x + w - 1, y, h, color);      /* Right */
}

void fb_line(int x0, int y0, int x1, int y1, uint32_t color) {
    if (!fb.active) return;

    /* Bresenham's line algorithm */
    int dx = x1 - x0;
    int dy = y1 - y0;
    int sx = dx > 0 ? 1 : -1;
    int sy = dy > 0 ? 1 : -1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;

    int err = dx - dy;

    while (1) {
        fb_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void fb_circle(int cx, int cy, int r, uint32_t color) {
    if (!fb.active) return;

    /* Midpoint circle algorithm */
    int x = r, y = 0;
    int err = 1 - r;

    while (x >= y) {
        fb_pixel(cx + x, cy + y, color);
        fb_pixel(cx - x, cy + y, color);
        fb_pixel(cx + x, cy - y, color);
        fb_pixel(cx - x, cy - y, color);
        fb_pixel(cx + y, cy + x, color);
        fb_pixel(cx - y, cy + x, color);
        fb_pixel(cx + y, cy - x, color);
        fb_pixel(cx - y, cy - x, color);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void fb_circle_filled(int cx, int cy, int r, uint32_t color) {
    if (!fb.active) return;

    int x = r, y = 0;
    int err = 1 - r;

    while (x >= y) {
        fb_hline(cx - x, cy + y, 2 * x + 1, color);
        fb_hline(cx - x, cy - y, 2 * x + 1, color);
        fb_hline(cx - y, cy + x, 2 * y + 1, color);
        fb_hline(cx - y, cy - x, 2 * y + 1, color);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void fb_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (!fb.active) return;

    const uint8_t* glyph = font_8x16[(uint8_t)c];
    for (int row = 0; row < FONT_HEIGHT; row++) {
        int py = y + row;
        if (py < 0 || py >= fb.height) continue;

        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            int px = x + col;
            if (px < 0 || px >= fb.width) continue;
            fb.backbuffer[py * fb.width + px] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

int fb_text(int x, int y, const char* str, uint32_t fg, uint32_t bg) {
    int start_x = x;
    while (*str) {
        if (*str == '\n') {
            y += FONT_HEIGHT;
            x = start_x;
        } else {
            fb_char(x, y, *str, fg, bg);
            x += FONT_WIDTH;
        }
        str++;
    }
    return x - start_x;
}

int fb_text_width(const char* str) {
    int w = 0;
    while (*str) {
        if (*str != '\n') w += FONT_WIDTH;
        str++;
    }
    return w;
}
