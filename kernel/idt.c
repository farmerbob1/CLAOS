/*
 * CLAOS — Claude Assisted Operating System
 * idt.c — Interrupt Descriptor Table setup and dispatch
 *
 * Sets up all 256 IDT entries and provides the common interrupt handler
 * that dispatches to registered C handlers.
 */

#include "idt.h"
#include "io.h"
#include "vga.h"
#include "string.h"

#define IDT_ENTRIES 256

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;

/* Array of registered handlers (one per interrupt number) */
static isr_handler_t interrupt_handlers[IDT_ENTRIES];

/* Defined in assembly — loads the IDT register */
extern void idt_flush(uint32_t idt_ptr_addr);

/* Set a single IDT gate */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags) {
    idt[num].base_low  = (uint16_t)(base & 0xFFFF);
    idt[num].base_high = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].selector  = selector;
    idt[num].always0   = 0;
    idt[num].flags     = flags;
}

/* Register a C function to handle a specific interrupt */
void register_interrupt_handler(uint8_t n, isr_handler_t handler) {
    interrupt_handlers[n] = handler;
}

/*
 * Common handler called from assembly stubs.
 * Dispatches to registered C handlers and sends EOI for IRQs.
 */
void isr_common_handler(struct registers* regs) {
    /* If there's a registered handler, call it */
    if (interrupt_handlers[regs->int_no]) {
        interrupt_handlers[regs->int_no](regs);
    } else if (regs->int_no < 32) {
        /* Unhandled CPU exception — this is bad */
        vga_set_color(VGA_WHITE, VGA_RED);
        vga_print("\n\n  !!! KERNEL PANIC: Unhandled exception !!!\n");
        vga_print("  Exception: ");
        vga_print_dec(regs->int_no);
        vga_print("  Error code: ");
        vga_print_hex(regs->err_code);
        vga_print("\n  EIP: ");
        vga_print_hex(regs->eip);
        vga_print("  ESP: ");
        vga_print_hex(regs->esp);
        vga_print("\n\n  System halted. Reboot to try again.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        __asm__ volatile ("cli; hlt");
    }

    /* Send End-of-Interrupt (EOI) to the PIC for hardware IRQs (32-47) */
    if (regs->int_no >= 32 && regs->int_no < 48) {
        /* If this came from the slave PIC (IRQs 8-15 = interrupts 40-47),
         * we need to send EOI to both slave and master */
        if (regs->int_no >= 40) {
            outb(0xA0, 0x20);  /* EOI to slave PIC */
        }
        outb(0x20, 0x20);      /* EOI to master PIC */
    }
}

/* ─── ISR stubs (defined in isr.asm) ─── */
extern void isr0(void);   extern void isr1(void);
extern void isr2(void);   extern void isr3(void);
extern void isr4(void);   extern void isr5(void);
extern void isr6(void);   extern void isr7(void);
extern void isr8(void);   extern void isr9(void);
extern void isr10(void);  extern void isr11(void);
extern void isr12(void);  extern void isr13(void);
extern void isr14(void);  extern void isr15(void);
extern void isr16(void);  extern void isr17(void);
extern void isr18(void);  extern void isr19(void);
extern void isr20(void);  extern void isr21(void);
extern void isr22(void);  extern void isr23(void);
extern void isr24(void);  extern void isr25(void);
extern void isr26(void);  extern void isr27(void);
extern void isr28(void);  extern void isr29(void);
extern void isr30(void);  extern void isr31(void);

/* ─── IRQ stubs (defined in irq.asm) ─── */
extern void irq0(void);   extern void irq1(void);
extern void irq2(void);   extern void irq3(void);
extern void irq4(void);   extern void irq5(void);
extern void irq6(void);   extern void irq7(void);
extern void irq8(void);   extern void irq9(void);
extern void irq10(void);  extern void irq11(void);
extern void irq12(void);  extern void irq13(void);
extern void irq14(void);  extern void irq15(void);

/*
 * Remap the PIC (Programmable Interrupt Controller).
 *
 * By default, IRQs 0-7 map to interrupts 8-15, which conflict with
 * CPU exception numbers. We remap them to 32-47 so they don't clash.
 */
static void pic_remap(void) {
    /* Save masks */
    uint8_t mask1 = inb(0x21);
    uint8_t mask2 = inb(0xA1);

    /* Start initialization sequence (ICW1) */
    outb(0x20, 0x11);  io_wait();
    outb(0xA0, 0x11);  io_wait();

    /* ICW2: Set interrupt vector offsets */
    outb(0x21, 0x20);  io_wait();  /* Master PIC: IRQs 0-7 → interrupts 32-39 */
    outb(0xA1, 0x28);  io_wait();  /* Slave PIC:  IRQs 8-15 → interrupts 40-47 */

    /* ICW3: Tell master about slave on IRQ2, tell slave its cascade identity */
    outb(0x21, 0x04);  io_wait();
    outb(0xA1, 0x02);  io_wait();

    /* ICW4: 8086 mode */
    outb(0x21, 0x01);  io_wait();
    outb(0xA1, 0x01);  io_wait();

    /* Restore saved masks */
    outb(0x21, mask1);
    outb(0xA1, mask2);
}

void idt_init(void) {
    /* Clear the IDT and handler table */
    memset(idt, 0, sizeof(idt));
    memset(interrupt_handlers, 0, sizeof(interrupt_handlers));

    /* Remap hardware IRQs away from CPU exception range */
    pic_remap();

    /* Install CPU exception handlers (ISRs 0-31)
     * Selector 0x08 = kernel code segment, Flags 0x8E = present, ring 0, 32-bit interrupt gate */
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    /* Install hardware IRQ handlers (IRQs 0-15 → interrupts 32-47) */
    idt_set_gate(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    /* Load the IDT */
    idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idtp.base  = (uint32_t)&idt;
    idt_flush((uint32_t)&idtp);
}
