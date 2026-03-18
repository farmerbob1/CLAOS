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
#include "net.h"

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
    vga_print("\n  Claude Assisted Operating System v0.3\n");

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

    /* Step 9: Kernel heap — place it after the kernel in physical memory.
     * We give it 1MB of space, starting at the page after _kernel_end. */
    uint32_t heap_start_addr = ((uint32_t)&_kernel_end + PAGE_SIZE) & ~(PAGE_SIZE - 1);
    uint32_t heap_size = 1024 * 1024;   /* 1MB heap */
    heap_init(heap_start_addr, heap_size);
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

    /* Step 12: Network stack */
    arp_init();
    tcp_init();
    dns_init();

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

    /* Create background tasks */
    task_create("spinner", task_status_indicator);
    task_create("uptime", task_uptime_display);
    if (nic_ok) {
        task_create("net_poll", task_net_poll);
    }
    boot_msg("Background tasks", "OK");

    vga_print("\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  CLAOS v0.3 ready. Type something!\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("  (Shell coming in Phase 5)\n\n");

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("claos> ");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    /* Main loop: echo keyboard input (simple demo for Phase 1) */
    char line[256];
    while (1) {
        keyboard_readline(line, sizeof(line));

        /* Command handling */
        if (strcmp(line, "help") == 0) {
            vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
            vga_print("  Available commands:\n");
            vga_print("    help    - Show this message\n");
            vga_print("    clear   - Clear the screen\n");
            vga_print("    uptime  - Show system uptime\n");
            vga_print("    sysinfo - Show memory and task info\n");
            vga_print("    tasks   - Show running tasks\n");
            vga_print("    net     - Show network configuration\n");
            vga_print("    dns <h> - Resolve a hostname\n");
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
        } else if (strcmp(line, "sysinfo") == 0) {
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_print("  --- System Information ---\n");
            vga_print("  Physical RAM: ");
            vga_print_dec(pmm_get_total_memory() / 1024 / 1024);
            vga_print("MB\n");
            vga_print("  Free pages:   ");
            vga_print_dec(pmm_get_free_pages());
            vga_print(" (");
            vga_print_dec(pmm_get_free_pages() * 4);
            vga_print("KB)\n");
            vga_print("  Used pages:   ");
            vga_print_dec(pmm_get_used_pages());
            vga_print(" (");
            vga_print_dec(pmm_get_used_pages() * 4);
            vga_print("KB)\n");
            vga_print("  Heap used:    ");
            vga_print_dec(heap_get_used());
            vga_print(" bytes\n");
            vga_print("  Heap free:    ");
            vga_print_dec(heap_get_free());
            vga_print(" bytes\n");
            vga_print("  Tasks:        ");
            vga_print_dec(task_get_count());
            vga_print(" running\n");
            vga_print("  Uptime:       ");
            vga_print_dec(timer_get_uptime());
            vga_print(" seconds\n");
            vga_set_color(VGA_WHITE, VGA_BLACK);
        } else if (strcmp(line, "tasks") == 0) {
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_print("  --- Running Tasks ---\n");
            vga_print("  ID  STATE      NAME\n");
            for (int i = 0; i < MAX_TASKS; i++) {
                struct task* t = task_get(i);
                if (!t) continue;
                vga_print("  ");
                vga_print_dec(t->id);
                if (t->id < 10) vga_print(" ");
                vga_print("  ");
                switch (t->state) {
                    case TASK_RUNNING:  vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
                                       vga_print("running    "); break;
                    case TASK_READY:    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
                                       vga_print("ready      "); break;
                    case TASK_SLEEPING: vga_set_color(VGA_YELLOW, VGA_BLACK);
                                       vga_print("sleeping   "); break;
                    default:           vga_print("unknown    "); break;
                }
                vga_set_color(VGA_WHITE, VGA_BLACK);
                vga_print(t->name);
                vga_print("\n");
            }
            vga_set_color(VGA_DARK_GREY, VGA_BLACK);
            vga_print("  ");
            vga_print_dec(task_get_count());
            vga_print(" task(s) total\n");
            vga_set_color(VGA_WHITE, VGA_BLACK);
        } else if (strcmp(line, "net") == 0) {
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_print("  --- Network Configuration ---\n");
            vga_print("  IP:      10.0.2.15\n");
            vga_print("  Gateway: 10.0.2.2\n");
            vga_print("  DNS:     10.0.2.3\n");
            vga_print("  Subnet:  255.255.255.0\n");
            vga_print("  NIC:     Intel e1000 (82540EM)\n");

            uint8_t mac[6];
            e1000_get_mac(mac);
            vga_print("  MAC:     ");
            for (int i = 0; i < 6; i++) {
                if (i > 0) vga_putchar(':');
                const char hex[] = "0123456789ABCDEF";
                vga_putchar(hex[mac[i] >> 4]);
                vga_putchar(hex[mac[i] & 0xF]);
            }
            vga_print("\n");

            vga_print("  Link:    ");
            if (e1000_link_up()) {
                vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
                vga_print("UP\n");
            } else {
                vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                vga_print("DOWN\n");
            }
            vga_set_color(VGA_WHITE, VGA_BLACK);
        } else if (strncmp(line, "dns ", 4) == 0) {
            const char* hostname = line + 4;
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_print("  Resolving ");
            vga_print(hostname);
            vga_print("...\n");

            uint32_t ip = dns_resolve(hostname);
            if (ip) {
                vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
                vga_print("  ");
                vga_print(hostname);
                vga_print(" -> ");
                vga_print_dec((ip >> 24) & 0xFF); vga_putchar('.');
                vga_print_dec((ip >> 16) & 0xFF); vga_putchar('.');
                vga_print_dec((ip >> 8) & 0xFF);  vga_putchar('.');
                vga_print_dec(ip & 0xFF);
                vga_print("\n");
            } else {
                vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                vga_print("  DNS resolution failed.\n");
            }
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
