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

/* Key state array — true if key is currently held down (indexed by scancode) */
#define KEY_STATE_SIZE 128
static volatile bool key_state[KEY_STATE_SIZE];

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

        /* Update key state array */
        if (released < KEY_STATE_SIZE)
            key_state[released] = false;

        /* Track modifier state */
        if (released == 0x2A || released == 0x36)
            shift_held = false;
        if (released == 0x1D)
            ctrl_held = false;

        /* Push KEY_UP event to GUI queue (games need this) */
        if (input_is_gui_mode()) {
            uint16_t special = scancode_to_special(released);
            uint16_t key;
            if (special) {
                key = special;
            } else {
                char c = scancode_ascii[released];
                key = c ? (uint16_t)(uint8_t)c : 0;
            }
            if (key) {
                input_event_t ev = { EVENT_KEY_UP, key, 0, 0, 0 };
                input_push(&ev);
            }
        }
        return;
    }

    /* Update key state array */
    if (scancode < KEY_STATE_SIZE)
        key_state[scancode] = true;

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

/* Game input: poll whether a key is currently held (by scancode) */
bool keyboard_is_pressed(uint8_t scancode) {
    if (scancode >= KEY_STATE_SIZE) return false;
    return key_state[scancode];
}

/* Initialize keyboard — nothing special needed beyond IRQ registration */
void keyboard_init(void) {
    kb_head = 0;
    kb_tail = 0;
    shift_held = false;
    for (int i = 0; i < KEY_STATE_SIZE; i++) key_state[i] = false;

    /* Set fast typematic rate: 250ms delay, 30 chars/sec repeat.
     * PS/2 command 0xF3 sets typematic rate/delay.
     * Byte format: bits 6-5 = delay (00=250ms), bits 4-0 = rate (00=30/s) */
    {
        int timeout;
        /* Wait for input buffer clear, then send command */
        for (timeout = 10000; timeout > 0 && (inb(0x64) & 0x02); timeout--) io_wait();
        outb(0x60, 0xF3);
        /* Wait for ACK (0xFA) from keyboard */
        for (timeout = 10000; timeout > 0; timeout--) {
            io_wait();
            if (!(inb(0x64) & 0x02) && (inb(0x64) & 0x01)) {
                uint8_t ack = inb(0x60);
                if (ack == 0xFA) break;
            }
        }
        /* Send data byte */
        for (timeout = 10000; timeout > 0 && (inb(0x64) & 0x02); timeout--) io_wait();
        outb(0x60, 0x00);  /* 250ms delay, 30 chars/sec */
        /* Wait for ACK */
        for (timeout = 10000; timeout > 0; timeout--) {
            io_wait();
            if ((inb(0x64) & 0x01)) { inb(0x60); break; }
        }
    }
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
