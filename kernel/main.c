/*
 * CLAOS — Claude Assisted Operating System
 * main.c — Kernel entry point
 *
 * This is where it all begins (after the bootloader hands off control).
 * We initialize all subsystems and then sit in the main loop processing
 * keyboard input.
 *
 * "Initializing consciousness... done."
 */

#include "types.h"
#include "string.h"
#include "io.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "gdt.h"
#include "idt.h"
#include "panic.h"

/* IRQ handler functions defined in driver files */
extern void timer_handler(void);
extern void keyboard_handler(void);

/* Timer IRQ wrapper — called from the interrupt dispatcher */
static void timer_irq_handler(struct registers* regs) {
    (void)regs;  /* Unused */
    timer_handler();
}

/* Keyboard IRQ wrapper */
static void keyboard_irq_handler(struct registers* regs) {
    (void)regs;  /* Unused */
    keyboard_handler();
}

/* The CLAOS boot banner — displayed in glorious VGA text mode */
static void print_banner(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("\n");
    vga_print("   ####  ##        ##     ####    ####\n");
    vga_print("  ##     ##       ####   ##  ##  ##\n");
    vga_print("  ##     ##      ##  ##  ##  ##   ####\n");
    vga_print("  ##     ##      ######  ##  ##      ##\n");
    vga_print("   ####  ######  ##  ##   ####   ####\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("\n  Claude Assisted Operating System v0.1\n");

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  ========================================\n");

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("  \"I am the kernel now.\"\n\n");
}

/* Print witty boot messages as we initialize subsystems */
static void boot_msg(const char* component, const char* status) {
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  [");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("BOOT");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("] ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print(component);
    vga_print("... ");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print(status);
    vga_print("\n");
}

/*
 * kernel_main — The C entry point called by Stage 2
 *
 * This is where CLAOS comes to life. We initialize hardware in the
 * correct order and then enter an interactive loop.
 */
void kernel_main(void) {
    /* Initialize serial port for debug output (visible in QEMU terminal) */
    serial_init();
    serial_print("[CLAOS] kernel_main entered\n");

    /* Step 1: Initialize VGA so we can see what we're doing */
    vga_init();
    serial_print("[CLAOS] VGA initialized\n");
    print_banner();
    serial_print("[CLAOS] Banner printed\n");

    /* Step 2: Set up the GDT (replace the bootloader's temporary one) */
    gdt_init();
    boot_msg("Global Descriptor Table", "OK");
    serial_print("[CLAOS] GDT OK\n");

    /* Step 3: Set up the IDT (interrupt handlers) */
    idt_init();
    boot_msg("Interrupt Descriptor Table", "OK");
    serial_print("[CLAOS] IDT OK\n");

    /* Step 4: Register exception handlers for panic screens */
    panic_init();
    boot_msg("Initializing consciousness", "done");
    serial_print("[CLAOS] Panic handlers OK\n");

    /* Step 5: Set up the PIT timer at 100 Hz (10ms per tick) */
    timer_init(100);
    register_interrupt_handler(32, timer_irq_handler);
    boot_msg("PIT Timer (100 Hz)", "OK");
    serial_print("[CLAOS] Timer OK\n");

    /* Step 6: Initialize keyboard */
    keyboard_init();
    register_interrupt_handler(33, keyboard_irq_handler);
    boot_msg("PS/2 Keyboard", "OK");
    serial_print("[CLAOS] Keyboard OK\n");

    /* Step 7: Enable interrupts — the system is now live! */
    __asm__ volatile ("sti");
    boot_msg("Interrupts", "ENABLED");
    serial_print("[CLAOS] Interrupts enabled, entering main loop\n");

    vga_print("\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  CLAOS v0.1 ready. Type something!\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("  (Phase 1 — Shell coming in Phase 5)\n\n");

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("claos> ");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* Main loop: echo keyboard input (simple demo for Phase 1) */
    char line[256];
    while (1) {
        keyboard_readline(line, sizeof(line));

        /* Simple command handling for Phase 1 demo */
        if (strcmp(line, "help") == 0) {
            vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
            vga_print("  Available commands:\n");
            vga_print("    help    - Show this message\n");
            vga_print("    clear   - Clear the screen\n");
            vga_print("    uptime  - Show system uptime\n");
            vga_print("    panic   - Trigger a kernel panic (for fun)\n");
            vga_print("    reboot  - Reboot the system\n");
            vga_set_color(VGA_WHITE, VGA_BLACK);
        } else if (strcmp(line, "clear") == 0) {
            vga_clear();
        } else if (strcmp(line, "uptime") == 0) {
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_print("  Uptime: ");
            vga_print_dec(timer_get_uptime());
            vga_print(" seconds (");
            vga_print_dec(timer_get_ticks());
            vga_print(" ticks)\n");
            vga_set_color(VGA_WHITE, VGA_BLACK);
        } else if (strcmp(line, "panic") == 0) {
            kernel_panic("User-initiated panic. This is fine. Everything is fine.");
        } else if (strcmp(line, "reboot") == 0) {
            vga_print("  Rebooting...\n");
            /* Triple fault = instant reboot. Load a zero-length IDT and interrupt. */
            struct idt_ptr null_idt = {0, 0};
            __asm__ volatile ("lidt %0" : : "m"(null_idt));
            __asm__ volatile ("int $0x03");
        } else if (strlen(line) > 0) {
            vga_set_color(VGA_DARK_GREY, VGA_BLACK);
            vga_print("  Unknown command: '");
            vga_print(line);
            vga_print("' (Claude integration coming in Phase 4!)\n");
            vga_set_color(VGA_WHITE, VGA_BLACK);
        }

        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("claos> ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
    }
}
