/*
 * CLAOS — Claude Assisted Operating System
 * input.h — GUI Input Event Queue
 *
 * Ring buffer that collects keyboard and mouse events for the GUI
 * event loop. Events are pushed by ISR handlers and polled by Lua.
 */

#ifndef CLAOS_INPUT_H
#define CLAOS_INPUT_H

#include "types.h"

/* Event types */
#define EVENT_NONE        0
#define EVENT_KEY_DOWN    1
#define EVENT_MOUSE_MOVE  2
#define EVENT_MOUSE_DOWN  3
#define EVENT_MOUSE_UP    4
#define EVENT_KEY_UP      5

/* Input event structure */
typedef struct {
    uint8_t  type;
    uint16_t key;         /* ASCII code (for KEY_DOWN) */
    int16_t  mouse_x;     /* Absolute mouse X */
    int16_t  mouse_y;     /* Absolute mouse Y */
    uint8_t  mouse_btn;   /* Button mask: bit0=left, bit1=right, bit2=middle */
} input_event_t;

/* Initialize the event queue */
void input_init(void);

/* Push an event (called from ISR context) */
void input_push(input_event_t* event);

/* Pop the next event. Returns false if queue is empty. */
bool input_poll(input_event_t* out);

/* Check if events are waiting */
bool input_has_events(void);

/* Enable/disable GUI input mode.
 * When enabled, keyboard events go to the event queue instead of
 * the keyboard line buffer. */
void input_set_gui_mode(bool enabled);
bool input_is_gui_mode(void);

#endif /* CLAOS_INPUT_H */
