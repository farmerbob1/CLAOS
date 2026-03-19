/*
 * CLAOS — Claude Assisted Operating System
 * keyboard.h — PS/2 Keyboard driver interface
 */

#ifndef CLAOS_KEYBOARD_H
#define CLAOS_KEYBOARD_H

#include "types.h"

/* Size of the keyboard input buffer */
#define KB_BUFFER_SIZE 256

/* Special key codes (>= 256, fit in uint16_t for input events) */
#define KEY_ARROW_UP     0x100
#define KEY_ARROW_DOWN   0x101
#define KEY_ARROW_LEFT   0x102
#define KEY_ARROW_RIGHT  0x103
#define KEY_HOME         0x104
#define KEY_END          0x105
#define KEY_DELETE        0x106
#define KEY_PAGE_UP      0x107
#define KEY_PAGE_DOWN    0x108

/* Modifier flags (ORed into key code bits 12-15) */
#define KEY_MOD_CTRL     0x1000
#define KEY_MOD_SHIFT    0x2000

/* Common scancodes for game input polling */
#define SC_ESC    0x01
#define SC_1      0x02
#define SC_2      0x03
#define SC_Q      0x10
#define SC_W      0x11
#define SC_E      0x12
#define SC_A      0x1E
#define SC_S      0x1F
#define SC_D      0x20
#define SC_SPACE  0x39
#define SC_LSHIFT 0x2A
#define SC_LCTRL  0x1D
#define SC_UP     0x48
#define SC_DOWN   0x50
#define SC_LEFT   0x4B
#define SC_RIGHT  0x4D

/* Initialize the keyboard driver (registers IRQ1 handler) */
void keyboard_init(void);

/* Check if a key is available in the buffer */
bool keyboard_has_key(void);

/* Get the next character from the buffer (blocks if empty) */
char keyboard_getchar(void);

/* Read a line of input into buf (up to max_len-1 chars). Echoes to screen.
 * Returns when Enter is pressed. Buffer is null-terminated. */
void keyboard_readline(char* buf, int max_len);

/* Game input: poll whether a key is currently held down (by scancode) */
bool keyboard_is_pressed(uint8_t scancode);

#endif /* CLAOS_KEYBOARD_H */
