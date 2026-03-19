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
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "scheduler.h"
#include "e1000.h"
#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"
#include "udp.h"
#include "dns.h"
#include "tcp.h"
#include "tls_client.h"
#include "net.h"
#include "entropy.h"
#include "claude.h"
#include "ata.h"
#include "chaosfs.h"
#include "claos_lib.h"
#include "shell.h"
#include "fb.h"
#include "console.h"

/* IRQ handler functions defined in driver files */
extern void timer_handler(void);
extern void keyboard_handler(void);

/* Kernel end address from linker script — marks where free memory begins */
extern uint32_t _kernel_end;

/* Timer IRQ wrapper — ticks the timer AND runs the scheduler */
static void timer_irq_handler(struct registers* regs) {
    (void)regs;
    timer_handler();
    schedule();     /* Preemptive multitasking! */
}

/* Keyboard IRQ wrapper */
static void keyboard_irq_handler(struct registers* regs) {
    (void)regs;
    keyboard_handler();
}

/*
 * Demo task: blinks a character in the top-right corner of the screen.
 * This proves the scheduler is actually running multiple tasks.
 */
static void task_status_indicator(void) {
    if (fb_is_active()) { while(1) task_sleep(10000); }
    volatile uint16_t* vga = (uint16_t*)0xB8000;
    /* Position: row 0, col 79 (top-right corner) */
    int pos = 79;
    const char spinner[] = "|/-\\";
    int i = 0;

    while (1) {
        vga[pos] = (uint16_t)(0x0A00 | spinner[i % 4]); /* Green on black */
        i++;
        task_sleep(250);    /* Update 4 times per second */
    }
}

/*
 * Demo task: shows uptime counter in the top-right area.
 */
static void task_uptime_display(void) {
    if (fb_is_active()) { while(1) task_sleep(10000); }
    volatile uint16_t* vga = (uint16_t*)0xB8000;

    while (1) {
        uint32_t uptime = timer_get_uptime();

        /* Display "Up:XXXXs" at row 0, col 69 */
        int pos = 69;
        uint8_t color = 0x08;  /* Dark grey */
        const char* prefix = "Up:";
        for (int i = 0; prefix[i]; i++)
            vga[pos++] = (uint16_t)(color << 8 | prefix[i]);

        /* Print uptime digits */
        char buf[8];
        int len = 0;
        uint32_t v = uptime;
        if (v == 0) buf[len++] = '0';
        else {
            while (v > 0 && len < 6) {
                buf[len++] = '0' + (v % 10);
                v /= 10;
            }
        }
        for (int i = len - 1; i >= 0; i--)
            vga[pos++] = (uint16_t)(color << 8 | buf[i]);
        vga[pos++] = (uint16_t)(color << 8 | 's');

        /* Clear any leftover characters */
        while (pos < 78)
            vga[pos++] = (uint16_t)(color << 8 | ' ');

        task_sleep(1000);   /* Update once per second */
    }
}

/*
 * Background task: continuously polls the NIC for incoming packets.
 * Without this, ARP/TCP responses would never be processed.
 */
static void task_net_poll(void) {
    net_set_bg_poll(true);
    while (1) {
        net_poll();
        task_sleep(10);     /* Poll every ~10ms */
    }
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
    vga_print("\n  Claude Assisted Operating System v0.7\n");

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

    /* Step 7: Physical memory manager */
    pmm_init((uint32_t)&_kernel_end);
    boot_msg("Physical memory", "OK");
    serial_print("[CLAOS] PMM OK\n");

    /* Print memory stats */
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("         ");
    vga_print_dec(pmm_get_total_memory() / 1024 / 1024);
    vga_print("MB detected, ");
    vga_print_dec(pmm_get_free_pages() * 4);
    vga_print("KB free (");
    vga_print_dec(pmm_get_free_pages());
    vga_print(" pages)\n");

    /* Step 8: Virtual memory (paging) */
    vmm_init();
    boot_msg("Virtual memory (paging)", "OK");
    serial_print("[CLAOS] VMM OK\n");

    /* Step 8b: Map VESA framebuffer if stage2 set VBE mode.
     * Skip the NOCACHE remap — the 4MB PSE identity mapping already works
     * for framebuffer access. NOCACHE is ideal but not strictly required
     * since we flush the full buffer on each swap. */
    if (*(volatile uint8_t*)0x9000 == 1) {
        /* Framebuffer at 0xFD000000 is already identity-mapped via 4MB PSE pages.
         * We skip vmm_map_framebuffer for now to avoid page table splitting
         * that triggers a QEMU 10.2.1 TCG assertion bug. */
        serial_print("[CLAOS] Framebuffer identity-mapped via PSE\n");
    }

    /* Step 8c: Initialize framebuffer console */
    if (fb_init()) {
        console_init();
        vga_set_framebuffer_mode(true);
        console_set_batch(true);  /* Suppress flushing during boot messages */
        print_banner();
        boot_msg("VESA framebuffer", "OK");
        boot_msg("Global Descriptor Table", "OK");
        boot_msg("Interrupt Descriptor Table", "OK");
        boot_msg("Initializing consciousness", "done");
        boot_msg("PIT Timer (100 Hz)", "OK");
        boot_msg("PS/2 Keyboard", "OK");
        boot_msg("Physical memory", "OK");
        boot_msg("Virtual memory (paging)", "OK");
    }

    /* Step 9: Kernel heap — place it after the kernel in physical memory.
     * We give it 1MB of space, starting at the page after _kernel_end. */
    uint32_t heap_start_addr = ((uint32_t)&_kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t heap_size = 1024 * 1024;   /* 1MB heap */
    heap_init(heap_start_addr, heap_size);

    /* Reserve heap pages in the PMM so pmm_alloc_page() never returns
     * a page that the heap is using — this prevents silent corruption. */
    for (uint32_t addr = heap_start_addr; addr < heap_start_addr + heap_size; addr += PAGE_SIZE) {
        pmm_reserve_page(addr);
    }
    boot_msg("Kernel heap (1MB)", "OK");
    serial_print("[CLAOS] Heap OK\n");

    /* Step 10: Scheduler */
    scheduler_init();
    boot_msg("Task scheduler", "OK");
    serial_print("[CLAOS] Scheduler OK\n");

    /* Step 11: Enable interrupts — the system is now live! */
    __asm__ volatile ("sti");
    boot_msg("Interrupts", "ENABLED");
    serial_print("[CLAOS] Interrupts enabled\n");

    /* Step 12: Entropy pool (needed for TLS) */
    entropy_init();
    boot_msg("Entropy pool", "OK");

    /* Step 13: Network stack */
    arp_init();
    tcp_init();
    dns_init();
    tls_init();

    bool nic_ok = e1000_init();
    if (nic_ok) {
        boot_msg("e1000 NIC", "OK");

        /* Show NIC info */
        uint8_t mac[6];
        e1000_get_mac(mac);
        vga_set_color(VGA_DARK_GREY, VGA_BLACK);
        vga_print("         MAC: ");
        for (int i = 0; i < 6; i++) {
            if (i > 0) vga_putchar(':');
            const char hex[] = "0123456789ABCDEF";
            vga_putchar(hex[mac[i] >> 4]);
            vga_putchar(hex[mac[i] & 0xF]);
        }
        vga_print("  IP: 10.0.2.15\n");

        /* Resolve gateway MAC via ARP */
        boot_msg("Resolving gateway (ARP)", "...");
        uint8_t gw_mac[6];
        if (arp_get_gateway_mac(gw_mac)) {
            boot_msg("Gateway MAC resolved", "OK");
        } else {
            boot_msg("Gateway ARP", "TIMEOUT (network may not work)");
        }
    } else {
        boot_msg("e1000 NIC", "NOT FOUND (no network)");
    }

    /* Step 14: ATA disk driver */
    if (ata_init()) {
        boot_msg("ATA disk", "OK");

        /* Step 15: ChaosFS */
        if (chaosfs_init()) {
            boot_msg("Mounting consciousness storage", "done");
        } else {
            boot_msg("ChaosFS", "NOT FOUND (use mkchaosfs.py to format)");
        }
    } else {
        boot_msg("ATA disk", "NOT FOUND (no storage)");
    }

    /* Step 16: Lua scripting */
    lua_subsystem_init();
    boot_msg("Lua 5.5 awakened", "the scripting layer stirs");

    /* Step 17: Claude integration */
    claude_init();
    if (claude_is_configured()) {
        boot_msg("Claude AI", "CONNECTED");
    } else {
        boot_msg("Claude AI", "NOT CONFIGURED (edit claude/config.h)");
    }

    /* Create background tasks */
    task_create("spinner", task_status_indicator);
    task_create("uptime", task_uptime_display);
    if (nic_ok) {
        task_create("net_poll", task_net_poll);
    }
    boot_msg("Background tasks", "OK");

    vga_print("\n");

    /* End batch mode — flush all boot messages to screen at once */
    if (fb_is_active()) {
        console_set_batch(false);
    }

    /* Hand off to the ClaudeShell — this never returns */
    shell_run();
}
