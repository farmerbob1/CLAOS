/*
 * CLAOS — Claude Assisted Operating System
 * mouse.c — PS/2 Mouse Driver
 *
 * Handles the PS/2 auxiliary port mouse via IRQ12. The mouse sends
 * 3-byte packets containing button state and X/Y movement deltas.
 * We accumulate these into an absolute screen position clamped to
 * the screen bounds.
 *
 * PS/2 mouse protocol:
 *   Byte 1: [YO XO YS XS 1 MB RB LB]
 *     YO/XO = overflow, YS/XS = sign bits, MB/RB/LB = buttons
 *   Byte 2: X movement (signed, modified by XS)
 *   Byte 3: Y movement (signed, modified by YS)
 */

#include "mouse.h"
#include "io.h"
#include "input.h"

/* PS/2 controller ports */
#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

/* Mouse state */
static volatile int mouse_x_pos = 0;
static volatile int mouse_y_pos = 0;
static volatile uint8_t mouse_btns = 0;

/* Screen bounds */
static int bound_w = 1024;
static int bound_h = 768;

/* Packet assembly — mouse sends 3 bytes per event */
static uint8_t packet[3];
static int packet_idx = 0;

/* Wait for PS/2 controller input buffer to be ready */
static void ps2_wait_write(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(PS2_STATUS) & 0x02)) return;
    }
}

/* Wait for PS/2 controller output buffer to have data */
static void ps2_wait_read(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS) & 0x01) return;
    }
}

/* Send a command to the PS/2 controller */
static void ps2_cmd(uint8_t cmd) {
    ps2_wait_write();
    outb(PS2_CMD, cmd);
}

/* Send a byte to the mouse (via the PS/2 controller auxiliary port) */
static void mouse_write(uint8_t data) {
    ps2_cmd(0xD4);          /* Tell controller: next byte goes to mouse */
    ps2_wait_write();
    outb(PS2_DATA, data);
    ps2_wait_read();
    inb(PS2_DATA);          /* Read ACK (0xFA) */
}

void mouse_init(void) {
    /* Enable the auxiliary (mouse) PS/2 port */
    ps2_cmd(0xA8);

    /* Enable IRQ12 by modifying the PS/2 controller configuration byte */
    ps2_cmd(0x20);          /* Read controller config */
    ps2_wait_read();
    uint8_t config = inb(PS2_DATA);
    config |= 0x02;         /* Bit 1 = enable IRQ12 for mouse */
    config &= ~0x20;        /* Bit 5 = disable mouse clock inhibit */
    ps2_cmd(0x60);          /* Write controller config */
    ps2_wait_write();
    outb(PS2_DATA, config);

    /* Reset mouse to defaults */
    mouse_write(0xFF);      /* Reset */
    ps2_wait_read();
    inb(PS2_DATA);          /* Read extra bytes from reset */
    ps2_wait_read();
    inb(PS2_DATA);

    /* Set mouse to streaming mode and enable data reporting */
    mouse_write(0xF6);      /* Set defaults */
    mouse_write(0xF4);      /* Enable data reporting */

    /* Start in center of screen */
    mouse_x_pos = bound_w / 2;
    mouse_y_pos = bound_h / 2;
    packet_idx = 0;

    serial_print("[MOUSE] PS/2 mouse initialized\n");
}

void mouse_handler(void) {
    uint8_t status = inb(PS2_STATUS);

    /* Bit 0 = output buffer full, Bit 5 = mouse data (not keyboard) */
    if (!(status & 0x01)) return;
    if (!(status & 0x20)) return;

    uint8_t data = inb(PS2_DATA);

    /* Assemble 3-byte packet */
    packet[packet_idx] = data;

    if (packet_idx == 0) {
        /* First byte: verify bit 3 is always set (sync check) */
        if (!(data & 0x08)) {
            packet_idx = 0;  /* Out of sync — resync */
            return;
        }
    }

    packet_idx++;
    if (packet_idx < 3) return;

    /* Full packet received — process it */
    packet_idx = 0;

    /* Extract button state */
    mouse_btns = packet[0] & 0x07;  /* LB=bit0, RB=bit1, MB=bit2 */

    /* Extract movement deltas (signed 9-bit values) */
    int dx = (int)packet[1];
    int dy = (int)packet[2];

    /* Apply sign extension from byte 0 */
    if (packet[0] & 0x10) dx |= 0xFFFFFF00;  /* X sign bit */
    if (packet[0] & 0x20) dy |= 0xFFFFFF00;  /* Y sign bit */

    /* Discard if overflow */
    if (packet[0] & 0xC0) return;  /* X or Y overflow */

    /* Update position (PS/2 Y axis is inverted — up is positive) */
    mouse_x_pos += dx;
    mouse_y_pos -= dy;

    /* Clamp to screen bounds */
    if (mouse_x_pos < 0) mouse_x_pos = 0;
    if (mouse_y_pos < 0) mouse_y_pos = 0;
    if (mouse_x_pos >= bound_w) mouse_x_pos = bound_w - 1;
    if (mouse_y_pos >= bound_h) mouse_y_pos = bound_h - 1;

    /* Push events to the GUI event queue */
    if (input_is_gui_mode()) {
        static uint8_t prev_btns = 0;

        /* Mouse move event */
        if (dx != 0 || dy != 0) {
            input_event_t ev = { EVENT_MOUSE_MOVE, 0,
                (int16_t)mouse_x_pos, (int16_t)mouse_y_pos, mouse_btns };
            input_push(&ev);
        }

        /* Button press/release events */
        uint8_t changed = mouse_btns ^ prev_btns;
        if (changed) {
            for (int b = 0; b < 3; b++) {
                if (changed & (1 << b)) {
                    input_event_t ev = {
                        (mouse_btns & (1 << b)) ? EVENT_MOUSE_DOWN : EVENT_MOUSE_UP,
                        0, (int16_t)mouse_x_pos, (int16_t)mouse_y_pos,
                        (uint8_t)(1 << b)
                    };
                    input_push(&ev);
                }
            }
        }
        prev_btns = mouse_btns;
    }
}

int mouse_get_x(void) { return mouse_x_pos; }
int mouse_get_y(void) { return mouse_y_pos; }
uint8_t mouse_get_buttons(void) { return mouse_btns; }

void mouse_set_bounds(int width, int height) {
    bound_w = width;
    bound_h = height;
    if (mouse_x_pos >= width) mouse_x_pos = width - 1;
    if (mouse_y_pos >= height) mouse_y_pos = height - 1;
}
