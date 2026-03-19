/*
 * CLAOS — Claude Assisted Operating System
 * console.c — Software Text Console on Framebuffer
 *
 * Renders a text grid on the VESA framebuffer using the embedded 8x16
 * font. Provides the same interface as VGA text mode (putchar, scroll,
 * color) but with much more screen real estate: 128x48 vs 80x25.
 *
 * Uses dirty-line tracking to only re-render changed lines on flush,
 * keeping the 3MB framebuffer swap efficient for typical text output.
 */

#include "console.h"
#include "fb.h"
#include "font.h"
#include "string.h"
#include "io.h"

/* Console cell: one character with its colors */
typedef struct {
    char     ch;
    uint32_t fg;
    uint32_t bg;
} console_cell_t;

/* Console state */
static console_cell_t cells[CONSOLE_MAX_ROWS][CONSOLE_MAX_COLS];
static bool line_dirty[CONSOLE_MAX_ROWS];
static int cx = 0;             /* Cursor column */
static int cy = 0;             /* Cursor row */
static uint32_t cur_fg = FB_WHITE;
static uint32_t cur_bg = FB_BLACK;
static int num_cols = 80;      /* Actual columns (based on resolution) */
static int num_rows = 25;      /* Actual rows (based on resolution) */
static bool batch_mode = false; /* When true, console_flush is suppressed */

void console_init(void) {
    const fb_info_t* info = fb_get_info();
    if (!info || !info->active) return;

    num_cols = info->width / FONT_WIDTH;
    num_rows = info->height / FONT_HEIGHT;
    if (num_cols > CONSOLE_MAX_COLS) num_cols = CONSOLE_MAX_COLS;
    if (num_rows > CONSOLE_MAX_ROWS) num_rows = CONSOLE_MAX_ROWS;

    console_clear();
    serial_print("[CONSOLE] Software console initialized\n");
}

void console_clear(void) {
    for (int r = 0; r < num_rows; r++) {
        for (int c = 0; c < num_cols; c++) {
            cells[r][c].ch = ' ';
            cells[r][c].fg = cur_fg;
            cells[r][c].bg = cur_bg;
        }
        line_dirty[r] = true;
    }
    cx = 0;
    cy = 0;
}

void console_set_color(uint32_t fg, uint32_t bg) {
    cur_fg = fg;
    cur_bg = bg;
}

/* Scroll the console up by one line */
static void console_scroll(void) {
    /* Move all lines up by one */
    memmove(&cells[0], &cells[1], sizeof(console_cell_t) * num_cols * (num_rows - 1));

    /* Clear the bottom line */
    for (int c = 0; c < num_cols; c++) {
        cells[num_rows - 1][c].ch = ' ';
        cells[num_rows - 1][c].fg = cur_fg;
        cells[num_rows - 1][c].bg = cur_bg;
    }

    /* Mark all lines dirty after scroll */
    for (int r = 0; r < num_rows; r++) {
        line_dirty[r] = true;
    }
}

void console_putchar(char c) {
    switch (c) {
        case '\n':
            cx = 0;
            cy++;
            break;

        case '\r':
            cx = 0;
            break;

        case '\t':
            /* Tab to next 8-column boundary */
            cx = (cx + 8) & ~7;
            if (cx >= num_cols) {
                cx = 0;
                cy++;
            }
            break;

        case '\b':
            if (cx > 0) {
                cx--;
                cells[cy][cx].ch = ' ';
                cells[cy][cx].fg = cur_fg;
                cells[cy][cx].bg = cur_bg;
                line_dirty[cy] = true;
            }
            break;

        default:
            if (cx >= num_cols) {
                cx = 0;
                cy++;
            }
            if (cy >= num_rows) {
                console_scroll();
                cy = num_rows - 1;
            }
            cells[cy][cx].ch = c;
            cells[cy][cx].fg = cur_fg;
            cells[cy][cx].bg = cur_bg;
            line_dirty[cy] = true;
            cx++;
            break;
    }

    /* Handle scroll if cursor went past bottom */
    while (cy >= num_rows) {
        console_scroll();
        cy = num_rows - 1;
    }
}

void console_print(const char* str) {
    while (*str) {
        console_putchar(*str);
        str++;
    }
}

void console_set_batch(bool batch) {
    batch_mode = batch;
    /* When leaving batch mode, do a final flush */
    if (!batch) console_flush();
}

void console_flush(void) {
    if (!fb_is_active()) return;
    if (batch_mode) return;  /* Suppress during batch mode */

    for (int r = 0; r < num_rows; r++) {
        if (!line_dirty[r]) continue;

        for (int c = 0; c < num_cols; c++) {
            console_cell_t* cell = &cells[r][c];
            fb_char(c * FONT_WIDTH, r * FONT_HEIGHT, cell->ch, cell->fg, cell->bg);
        }
        line_dirty[r] = false;
    }

    fb_swap();
}

int console_get_cols(void) { return num_cols; }
int console_get_rows(void) { return num_rows; }
