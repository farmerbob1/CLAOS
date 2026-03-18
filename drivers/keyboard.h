/*
 * CLAOS — Claude Assisted Operating System
 * keyboard.h — PS/2 Keyboard driver interface
 */

#ifndef CLAOS_KEYBOARD_H
#define CLAOS_KEYBOARD_H

#include "types.h"

/* Size of the keyboard input buffer */
#define KB_BUFFER_SIZE 256

/* Initialize the keyboard driver (registers IRQ1 handler) */
void keyboard_init(void);

/* Check if a key is available in the buffer */
bool keyboard_has_key(void);

/* Get the next character from the buffer (blocks if empty) */
char keyboard_getchar(void);

/* Read a line of input into buf (up to max_len-1 chars). Echoes to screen.
 * Returns when Enter is pressed. Buffer is null-terminated. */
void keyboard_readline(char* buf, int max_len);

#endif /* CLAOS_KEYBOARD_H */
