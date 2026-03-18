/*
 * CLAOS — Claude Assisted Operating System
 * panic.c — Kernel panic handler
 *
 * When the CPU faults, we display a dramatic error screen with
 * register state and fault details. In Phase 4, this will auto-send
 * the crash report to Claude for diagnosis.
 *
 * For now: red screen of death, witty messages, register dump.
 */

#include "panic.h"
#include "vga.h"
#include "io.h"
#include "timer.h"

/* Human-readable names for CPU exceptions 0-31 */
const char* exception_names[32] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"
};

/* The dramatic panic screen */
static void panic_screen(const char* reason, struct registers* regs) {
    /* Set up red-on-black color scheme for maximum drama */
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_clear();

    vga_print("\n");
    vga_print("  ============================================================\n");
    vga_print("                    CLAOS KERNEL PANIC\n");
    vga_print("       \"I have made a terrible mistake.\" - The Kernel\n");
    vga_print("  ============================================================\n\n");

    vga_set_color(VGA_YELLOW, VGA_RED);
    vga_print("  FAULT: ");
    vga_print(reason);
    vga_print("\n\n");

    if (regs) {
        vga_set_color(VGA_WHITE, VGA_RED);
        vga_print("  --- Register Dump ---\n");
        vga_print("  EAX="); vga_print_hex(regs->eax);
        vga_print("  EBX="); vga_print_hex(regs->ebx);
        vga_print("  ECX="); vga_print_hex(regs->ecx);
        vga_print("\n");
        vga_print("  EDX="); vga_print_hex(regs->edx);
        vga_print("  ESI="); vga_print_hex(regs->esi);
        vga_print("  EDI="); vga_print_hex(regs->edi);
        vga_print("\n");
        vga_print("  EBP="); vga_print_hex(regs->ebp);
        vga_print("  ESP="); vga_print_hex(regs->esp);
        vga_print("  EIP="); vga_print_hex(regs->eip);
        vga_print("\n");
        vga_print("  EFLAGS="); vga_print_hex(regs->eflags);
        vga_print("  ERR="); vga_print_hex(regs->err_code);
        vga_print("  INT="); vga_print_dec(regs->int_no);
        vga_print("\n\n");
    }

    vga_set_color(VGA_LIGHT_CYAN, VGA_RED);
    vga_print("  [CLAOS -> Claude] Crash report ready.\n");
    vga_print("  (Claude integration coming in Phase 4...)\n\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_RED);
    vga_print("  Press any key to reboot...\n");
}

/* Reboot by triple-faulting (load a null IDT and trigger an interrupt) */
static void reboot(void) {
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) null_idt = {0, 0};
    __asm__ volatile ("lidt %0" : : "m"(null_idt));
    __asm__ volatile ("int $0x03");
}

/* Poll the keyboard controller directly (no IRQs needed).
 * Wait for a key press event (scancode with bit 7 clear). */
static void wait_for_keypress_and_reboot(void) {
    /* Flush any pending scancodes */
    while (inb(0x64) & 1)
        inb(0x60);

    /* Poll until a key-down event */
    for (;;) {
        if (inb(0x64) & 1) {           /* Data available from keyboard? */
            uint8_t sc = inb(0x60);
            if (!(sc & 0x80))           /* Key press, not release */
                break;
        }
    }
    reboot();
}

/* Handler for CPU exceptions — registered for ISRs 0-31 */
static void exception_handler(struct registers* regs) {
    const char* name = "Unknown Exception";
    if (regs->int_no < 32)
        name = exception_names[regs->int_no];

    panic_screen(name, regs);

    /* Wait for a keypress, then reboot */
    __asm__ volatile ("cli");
    wait_for_keypress_and_reboot();
}

/* Register our exception handlers for all 32 CPU exceptions */
void panic_init(void) {
    for (int i = 0; i < 32; i++) {
        register_interrupt_handler(i, exception_handler);
    }
}

/* Trigger a panic manually (e.g., from `panic` shell command) */
void kernel_panic(const char* message) {
    panic_screen(message, NULL);
    __asm__ volatile ("cli");
    wait_for_keypress_and_reboot();
}
