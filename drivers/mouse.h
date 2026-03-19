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

#endif /* CLAOS_MOUSE_H */
