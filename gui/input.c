/*
 * CLAOS — Claude Assisted Operating System
 * input.c — GUI Input Event Queue
 *
 * Lock-free SPSC ring buffer. The ISR pushes events, the main GUI
 * loop (Lua) polls them. Safe on single-core x86 with volatile
 * head/tail indices.
 */

#include "input.h"
#include "io.h"

#define EVENT_QUEUE_SIZE 256

static input_event_t queue[EVENT_QUEUE_SIZE];
static volatile int q_head = 0;   /* Write position (ISR) */
static volatile int q_tail = 0;   /* Read position (main) */
static bool gui_mode = false;

void input_init(void) {
    q_head = 0;
    q_tail = 0;
    gui_mode = false;
    serial_print("[INPUT] Event queue initialized\n");
}

void input_push(input_event_t* event) {
    int next = (q_head + 1) % EVENT_QUEUE_SIZE;
    if (next == q_tail) return;  /* Queue full — drop event */
    queue[q_head] = *event;
    q_head = next;
}

bool input_poll(input_event_t* out) {
    if (q_head == q_tail) return false;
    *out = queue[q_tail];
    q_tail = (q_tail + 1) % EVENT_QUEUE_SIZE;
    return true;
}

bool input_has_events(void) {
    return q_head != q_tail;
}

void input_set_gui_mode(bool enabled) {
    gui_mode = enabled;
}

bool input_is_gui_mode(void) {
    return gui_mode;
}
