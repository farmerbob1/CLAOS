/*
 * CLAOS — Claude Assisted Operating System
 * mouse.h — PS/2 Mouse Driver
 */

#ifndef CLAOS_MOUSE_H
#define CLAOS_MOUSE_H

#include "types.h"

/* Initialize the PS/2 mouse (enables IRQ12) */
void mouse_init(void);

/* Called from IRQ12 handler */
void mouse_handler(void);

/* Get current mouse state */
int mouse_get_x(void);
int mouse_get_y(void);
uint8_t mouse_get_buttons(void);

/* Set screen bounds (call after VESA mode is known) */
void mouse_set_bounds(int width, int height);

/* Raw delta mode for FPS mouselook — accumulates dx/dy without moving cursor */
void mouse_set_raw_mode(bool enable);
void mouse_get_delta(int* dx, int* dy);  /* reads and resets accumulated deltas */
bool mouse_is_raw_mode(void);

#endif /* CLAOS_MOUSE_H */
