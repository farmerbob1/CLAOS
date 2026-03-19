/*
 * CLAOS — Claude Assisted Operating System
 * keyboard.c — PS/2 Keyboard driver
 *
 * Handles IRQ1 (keyboard interrupt). When a key is pressed, the keyboard
 * controller sends a scancode on port 0x60. We translate Set 1 scancodes
 * to ASCII and store them in a circular buffer.
 *
 * This is a simple US QWERTY layout. No fancy stuff.
 */

#include "keyboard.h"
#include "vga.h"
#include "io.h"
#include "input.h"

/* US QWERTY scancode-to-ASCII lookup table (Set 1, lowercase)
 * Index = scancode, value = ASCII character (0 = not a printable key) */
static const char scancode_ascii[128] = {
    0,   27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ', 0,
    /* F1-F10, Num Lock, Scroll Lock, etc. — all mapped to 0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0
};

/* Shifted versions of the same keys */
static const char scancode_ascii_shift[128] = {
    0,   27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0
};

/* Circular input buffer */
static char kb_buffer[KB_BUFFER_SIZE];
static volatile int kb_head = 0;     /* Write position */
static volatile int kb_tail = 0;     /* Read position */

/* Modifier key state */
static volatile bool shift_held = false;
static volatile bool ctrl_held = false;

/* Map scancodes to special key codes (arrow keys, Home, End, etc.) */
static uint16_t scancode_to_special(uint8_t scancode) {
    switch (scancode) {
        case 0x48: return KEY_ARROW_UP;
        case 0x50: return KEY_ARROW_DOWN;
        case 0x4B: return KEY_ARROW_LEFT;
        case 0x4D: return KEY_ARROW_RIGHT;
        case 0x47: return KEY_HOME;
        case 0x4F: return KEY_END;
        case 0x53: return KEY_DELETE;
        case 0x49: return KEY_PAGE_UP;
        case 0x51: return KEY_PAGE_DOWN;
        default:   return 0;
    }
}

/* Called from our IRQ1 handler (see irq.c) */
void keyboard_handler(void) {
    uint8_t scancode = inb(0x60);

    /* Key release events have bit 7 set */
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        /* Left shift = 0x2A, Right shift = 0x36 */
        if (released == 0x2A || released == 0x36)
            shift_held = false;
        /* Left Ctrl = 0x1D */
        if (released == 0x1D)
            ctrl_held = false;
        return;
    }

    /* Track modifier state */
    if (scancode == 0x2A || scancode == 0x36) {
        shift_held = true;
        return;
    }
    if (scancode == 0x1D) {
        ctrl_held = true;
        return;
    }

    /* Check for special keys (arrows, home, end, delete, etc.) */
    uint16_t special = scancode_to_special(scancode);
    if (special) {
        if (input_is_gui_mode()) {
            uint16_t key = special;
            if (ctrl_held)  key |= KEY_MOD_CTRL;
            if (shift_held) key |= KEY_MOD_SHIFT;
            input_event_t ev = { EVENT_KEY_DOWN, key, 0, 0, 0 };
            input_push(&ev);
        }
        return;
    }

    /* Translate scancode to ASCII */
    char c;
    if (shift_held)
        c = scancode_ascii_shift[scancode];
    else
        c = scancode_ascii[scancode];

    /* If it's a valid character, route it */
    if (c != 0) {
        if (input_is_gui_mode()) {
            /* GUI mode: push to event queue for Lua */
            uint16_t key = (uint16_t)(uint8_t)c;
            if (ctrl_held)  key |= KEY_MOD_CTRL;
            if (shift_held && c >= 'a' && c <= 'z') {
                /* Shift already handled by scancode_ascii_shift for printable chars,
                 * but add flag for special handling (e.g., shift+arrow = select) */
            }
            if (shift_held) key |= KEY_MOD_SHIFT;
            input_event_t ev = { EVENT_KEY_DOWN, key, 0, 0, 0 };
            input_push(&ev);
        } else {
            /* Text mode: add to keyboard line buffer */
            int next_head = (kb_head + 1) % KB_BUFFER_SIZE;
            if (next_head != kb_tail) {
                kb_buffer[kb_head] = c;
                kb_head = next_head;
            }
        }
    }
}

/* Initialize keyboard — nothing special needed beyond IRQ registration */
void keyboard_init(void) {
    kb_head = 0;
    kb_tail = 0;
    shift_held = false;
    /* IRQ1 handler is registered in irq_init() */
}

/* Check if there's input waiting */
bool keyboard_has_key(void) {
    return kb_head != kb_tail;
}

/* Get next character from buffer. Blocks (spins) until a key is available. */
char keyboard_getchar(void) {
    while (!keyboard_has_key()) {
        /* Halt until next interrupt to save CPU */
        __asm__ volatile ("hlt");
    }
    /* Disable interrupts while reading to prevent ISR from modifying
     * kb_head between our read and tail update (defensive — the SPSC
     * pattern is safe on single-core x86 but this costs nothing). */
    __asm__ volatile ("cli");
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
    __asm__ volatile ("sti");
    return c;
}

/* Read a line with echo. Returns on Enter. Handles backspace. */
void keyboard_readline(char* buf, int max_len) {
    int pos = 0;
    while (pos < max_len - 1) {
        char c = keyboard_getchar();

        if (c == '\n') {
            /* Enter pressed — finish the line */
            vga_putchar('\n');
            break;
        } else if (c == '\b') {
            /* Backspace — erase last character */
            if (pos > 0) {
                pos--;
                vga_putchar('\b');
            }
        } else {
            /* Regular character — echo and store */
            buf[pos++] = c;
            vga_putchar(c);
        }
    }
    buf[pos] = '\0';
}
