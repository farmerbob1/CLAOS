/*
 * CLAOS — Claude Assisted Operating System
 * fb.h — Framebuffer Driver (VESA VBE)
 *
 * Provides the core framebuffer abstraction: double-buffered rendering
 * with drawing primitives, font rendering, and buffer swap.
 * All drawing goes to the back buffer; call fb_swap() to display.
 */

#ifndef CLAOS_FB_H
#define CLAOS_FB_H

#include "types.h"

/* Framebuffer info */
typedef struct {
    uint32_t* framebuffer;      /* Physical framebuffer (MMIO, write-combined) */
    uint32_t* backbuffer;       /* RAM back buffer (fast reads/writes) */
    uint16_t  width;
    uint16_t  height;
    uint16_t  pitch;            /* Bytes per scan line (may be > width*4) */
    uint8_t   bpp;              /* Bits per pixel (32) */
    bool      active;           /* True if VESA mode is active */
} fb_info_t;

/* VBE info addresses (set by stage2 bootloader at 0x2000) */
#define VBE_INFO_ADDR       0x2000
#define VBE_STATUS_ADDR     0x2000
#define VBE_FB_ADDR         0x2004
#define VBE_WIDTH_ADDR      0x2008
#define VBE_HEIGHT_ADDR     0x200A
#define VBE_PITCH_ADDR      0x200C
#define VBE_BPP_ADDR        0x200E

/* Probe VBE info from stage2 data (does NOT switch video mode).
 * Returns true if VBE mode is available. */
bool fb_init(void);

/* Activate VESA mode (switches from text mode to graphical framebuffer).
 * Call this when launching the GUI, not at boot. Uses Bochs VBE dispi
 * interface to set mode from protected mode without real-mode INT 10h. */
bool fb_activate(void);

/* Get framebuffer info struct */
const fb_info_t* fb_get_info(void);

/* Is VESA mode active? */
bool fb_is_active(void);

/* Swap back buffer to screen (copy to framebuffer) */
void fb_swap(void);

/* Swap only a region of rows (for efficient partial updates) */
void fb_swap_region(int y_start, int y_end);

/* Clear back buffer to solid color */
void fb_clear(uint32_t color);

/* Set a single pixel */
void fb_pixel(int x, int y, uint32_t color);

/* Get a pixel from back buffer */
uint32_t fb_get_pixel(int x, int y);

/* Draw a filled rectangle */
void fb_rect(int x, int y, int w, int h, uint32_t color);

/* Draw a rectangle outline */
void fb_rect_outline(int x, int y, int w, int h, uint32_t color);

/* Draw a filled rounded rectangle */
void fb_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);

/* Optimized horizontal line */
void fb_hline(int x, int y, int w, uint32_t color);

/* Optimized vertical line */
void fb_vline(int x, int y, int h, uint32_t color);

/* Arbitrary line (Bresenham) */
void fb_line(int x0, int y0, int x1, int y1, uint32_t color);

/* Circle outline (Midpoint algorithm) */
void fb_circle(int cx, int cy, int r, uint32_t color);

/* Filled circle */
void fb_circle_filled(int cx, int cy, int r, uint32_t color);

/* Draw a single character at pixel position using embedded 8x16 font */
void fb_char(int x, int y, char c, uint32_t fg, uint32_t bg);

/* Draw a string. Returns number of pixels wide. */
int fb_text(int x, int y, const char* str, uint32_t fg, uint32_t bg);

/* Get pixel width of a string */
int fb_text_width(const char* str);

/* Draw bold text (renders twice with 1px offset for faux bold) */
int fb_text_bold(int x, int y, const char* str, uint32_t fg, uint32_t bg);

/* Draw 2x scaled text (each pixel doubled — 16x32 per char) */
int fb_text_2x(int x, int y, const char* str, uint32_t fg, uint32_t bg);

/* Get raw backbuffer pointer (for 3D engine direct rendering) */
uint32_t* fb_get_backbuffer(void);

/* Color helpers — ARGB format (alpha in high byte) */
#define FB_RGB(r, g, b)       ((uint32_t)(0xFF000000 | ((r) << 16) | ((g) << 8) | (b)))
#define FB_ARGB(a, r, g, b)   ((uint32_t)(((a) << 24) | ((r) << 16) | ((g) << 8) | (b)))

/* Standard colors */
#define FB_BLACK     FB_RGB(0, 0, 0)
#define FB_WHITE     FB_RGB(255, 255, 255)
#define FB_RED       FB_RGB(255, 0, 0)
#define FB_GREEN     FB_RGB(0, 255, 0)
#define FB_BLUE      FB_RGB(0, 0, 255)
#define FB_CYAN      FB_RGB(0, 255, 255)
#define FB_MAGENTA   FB_RGB(255, 0, 255)
#define FB_YELLOW    FB_RGB(255, 255, 0)
#define FB_GREY      FB_RGB(128, 128, 128)
#define FB_DARK_GREY FB_RGB(85, 85, 85)

#endif /* CLAOS_FB_H */
