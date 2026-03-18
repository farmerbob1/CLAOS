/*
 * CLAOS — Claude Assisted Operating System
 * panic_handler.c — Panic → Claude Integration
 *
 * When a kernel panic occurs, this module formats the crash info
 * into a prompt and sends it to Claude for diagnosis. Claude's
 * response is displayed on the panic screen.
 *
 * "KERNEL PANIC: I have made a terrible mistake. Calling Claude for help..."
 */

#include "panic_handler.h"
#include "claude.h"
#include "vga.h"
#include "io.h"
#include "string.h"

/* Helper: append a hex value to a string buffer */
static int append_hex(char* buf, int pos, int max, uint32_t val) {
    const char hex[] = "0123456789ABCDEF";
    if (pos + 10 >= max) return pos;
    buf[pos++] = '0'; buf[pos++] = 'x';
    for (int i = 28; i >= 0; i -= 4) {
        buf[pos++] = hex[(val >> i) & 0xF];
    }
    return pos;
}

/* Helper: append a string */
static int append_str(char* buf, int pos, int max, const char* str) {
    while (*str && pos < max - 1) buf[pos++] = *str++;
    return pos;
}

void panic_ask_claude(const char* reason, struct registers* regs) {
    if (!claude_is_configured()) {
        vga_set_color(VGA_DARK_GREY, VGA_RED);
        vga_print("  [Claude not configured - edit claude/config.h]\n");
        return;
    }

    vga_set_color(VGA_LIGHT_CYAN, VGA_RED);
    vga_print("  [CLAOS -> Claude] Sending crash report via HTTPS...\n");

    /* Build the crash report prompt */
    static char prompt[1024];
    int pos = 0;

    pos = append_str(prompt, pos, sizeof(prompt),
        "CLAOS kernel panic. You are Claude, an AI integrated into the CLAOS "
        "operating system at the kernel level. A crash just occurred and I need "
        "your help diagnosing it. Keep your response short (2-3 sentences).\n\n"
        "Fault: ");
    pos = append_str(prompt, pos, sizeof(prompt), reason);

    if (regs) {
        pos = append_str(prompt, pos, sizeof(prompt), "\nRegisters: EIP=");
        pos = append_hex(prompt, pos, sizeof(prompt), regs->eip);
        pos = append_str(prompt, pos, sizeof(prompt), " EAX=");
        pos = append_hex(prompt, pos, sizeof(prompt), regs->eax);
        pos = append_str(prompt, pos, sizeof(prompt), " EBX=");
        pos = append_hex(prompt, pos, sizeof(prompt), regs->ebx);
        pos = append_str(prompt, pos, sizeof(prompt), " ECX=");
        pos = append_hex(prompt, pos, sizeof(prompt), regs->ecx);
        pos = append_str(prompt, pos, sizeof(prompt), " EDX=");
        pos = append_hex(prompt, pos, sizeof(prompt), regs->edx);
        pos = append_str(prompt, pos, sizeof(prompt), "\nESP=");
        pos = append_hex(prompt, pos, sizeof(prompt), regs->esp);
        pos = append_str(prompt, pos, sizeof(prompt), " EBP=");
        pos = append_hex(prompt, pos, sizeof(prompt), regs->ebp);
        pos = append_str(prompt, pos, sizeof(prompt), " EFLAGS=");
        pos = append_hex(prompt, pos, sizeof(prompt), regs->eflags);
        pos = append_str(prompt, pos, sizeof(prompt), " ERR=");
        pos = append_hex(prompt, pos, sizeof(prompt), regs->err_code);
    }

    prompt[pos] = '\0';

    /* Ask Claude */
    static char response[CLAUDE_RESPONSE_MAX];
    int resp_len = claude_ask(prompt, response, sizeof(response));

    /* Display Claude's response on the panic screen */
    vga_print("\n");
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_print("  [Claude] ");
    vga_set_color(VGA_LIGHT_GREEN, VGA_RED);

    if (resp_len > 0) {
        /* Word-wrap Claude's response to fit the screen */
        int col = 10;   /* Starting column (after "[Claude] ") */
        for (int i = 0; i < resp_len; i++) {
            if (response[i] == '\n') {
                vga_print("\n  ");
                col = 2;
            } else {
                if (col >= 76) {
                    vga_print("\n  ");
                    col = 2;
                }
                vga_putchar(response[i]);
                col++;
            }
        }
        vga_print("\n");
    } else {
        vga_print("Could not reach Claude. You're on your own.\n");
    }
}
