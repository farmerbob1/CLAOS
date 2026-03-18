/*
 * CLAOS — Claude Assisted Operating System
 * shell.c — ClaudeShell Interactive Interface
 *
 * The main user-facing interface of CLAOS. Provides built-in commands
 * for system management and Claude interaction. Any unrecognized command
 * is automatically sent to Claude for interpretation.
 *
 * "claos> " is where the magic happens.
 */

#include "shell.h"
#include "types.h"
#include "string.h"
#include "io.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "pmm.h"
#include "heap.h"
#include "scheduler.h"
#include "idt.h"
#include "e1000.h"
#include "claude.h"
#include "tls_client.h"
#include "dns.h"
#include "net.h"

/* Shell input buffer */
#define SHELL_BUF_SIZE 1024

/* ─── Command Handlers ─── */

static void cmd_help(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("\n");
    vga_print("  ── ClaudeShell Commands ──\n\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  claude <msg> ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  Ask Claude a question\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  config       ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  Configure API key & model\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  sysinfo      ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  System information\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  tasks        ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  List running tasks\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  net          ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  Network configuration\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  dns <host>   ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  Resolve a hostname\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  tls          ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  Test TLS handshake\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  uptime       ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  Show system uptime\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  clear        ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  Clear the screen\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  panic        ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  Trigger a kernel panic\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  reboot       ");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  Reboot the system\n");

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("\n  Any other input is sent to Claude as a prompt.\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static void cmd_sysinfo(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("  ── System Information ──\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  RAM:      ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print_dec(pmm_get_total_memory() / 1024 / 1024);
    vga_print("MB (");
    vga_print_dec(pmm_get_free_pages() * 4);
    vga_print("KB free, ");
    vga_print_dec(pmm_get_used_pages() * 4);
    vga_print("KB used)\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Heap:     ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print_dec(heap_get_used());
    vga_print(" used, ");
    vga_print_dec(heap_get_free());
    vga_print(" free\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Tasks:    ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print_dec(task_get_count());
    vga_print(" active\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Uptime:   ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print_dec(timer_get_uptime());
    vga_print("s\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  NIC:      ");
    if (e1000_link_up()) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("e1000 (link up)\n");
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("e1000 (link down)\n");
    }

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Claude:   ");
    if (claude_is_configured()) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("configured (");
        vga_print(claude_get_model());
        vga_print(")\n");
    } else {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_print("not configured\n");
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static void cmd_tasks(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("  ── Tasks ──\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
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
    vga_print(" task(s)\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static void cmd_net(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("  ── Network ──\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    vga_print("  IP:      ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("10.0.2.15\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Gateway: ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("10.0.2.2\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  DNS:     ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("10.0.2.3\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  NIC:     ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("Intel e1000\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  MAC:     ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    uint8_t mac[6];
    e1000_get_mac(mac);
    const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        if (i) vga_putchar(':');
        vga_putchar(hex[mac[i] >> 4]);
        vga_putchar(hex[mac[i] & 0xF]);
    }
    vga_print("\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Link:    ");
    if (e1000_link_up()) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("UP\n");
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("DOWN\n");
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static void cmd_dns(const char* hostname) {
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
}

static void cmd_tls(void) {
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Testing TLS handshake with api.anthropic.com...\n");

    tls_connection_t* tls = tls_connect("api.anthropic.com", 443);
    if (tls) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("  TLS handshake SUCCEEDED!\n");
        tls_close(tls);
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  TLS handshake FAILED.\n");
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static void cmd_claude(const char* prompt) {
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  [CLAOS -> Claude] Sending...\n");

    static char response[CLAUDE_RESPONSE_MAX];
    int len = claude_ask(prompt, response, sizeof(response));

    if (len > 0) {
        vga_set_color(VGA_LIGHT_MAGENTA, VGA_BLACK);
        vga_print("  [Claude] ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        /* Word-wrap the response */
        int col = 10;
        for (int i = 0; i < len; i++) {
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
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  Failed to reach Claude.\n");
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

/* Send an unrecognized command to Claude with context */
static void cmd_unknown(const char* input) {
    /* Build a contextualized prompt for Claude */
    static char prompt[1200];
    prompt[0] = '\0';
    strcat(prompt,
        "You are Claude, the AI integrated into CLAOS (Claude Assisted Operating System). "
        "The user typed the following at the claos> shell prompt. "
        "If it looks like a question or conversation, respond naturally. "
        "If it looks like a command, explain what it would do or suggest alternatives. "
        "Keep your response concise (1-3 sentences).\n\n"
        "User typed: ");
    strcat(prompt, input);

    cmd_claude(prompt);
}

static void cmd_uptime(void) {
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    uint32_t up = timer_get_uptime();
    uint32_t mins = up / 60;
    uint32_t secs = up % 60;
    vga_print("  Uptime: ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    if (mins > 0) {
        vga_print_dec(mins);
        vga_print("m ");
    }
    vga_print_dec(secs);
    vga_print("s (");
    vga_print_dec(timer_get_ticks());
    vga_print(" ticks)\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static void cmd_reboot(void) {
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Rebooting...\n");
    struct idt_ptr null_idt = {0, 0};
    __asm__ volatile ("lidt %0" : : "m"(null_idt));
    __asm__ volatile ("int $0x03");
}

/* ─── Shell Main Loop ─── */

void shell_run(void) {
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  CLAOS v0.5 ready.\n");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  Type 'help' for commands, or just talk to Claude.\n\n");

    char line[SHELL_BUF_SIZE];

    while (1) {
        /* Print prompt */
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("claos> ");
        vga_set_color(VGA_WHITE, VGA_BLACK);

        /* Read input */
        keyboard_readline(line, sizeof(line));

        /* Skip empty lines */
        if (strlen(line) == 0)
            continue;

        /* Dispatch commands */
        if (strcmp(line, "help") == 0) {
            cmd_help();
        } else if (strcmp(line, "clear") == 0) {
            vga_clear();
        } else if (strcmp(line, "uptime") == 0) {
            cmd_uptime();
        } else if (strcmp(line, "sysinfo") == 0) {
            cmd_sysinfo();
        } else if (strcmp(line, "tasks") == 0) {
            cmd_tasks();
        } else if (strcmp(line, "net") == 0) {
            cmd_net();
        } else if (strncmp(line, "dns ", 4) == 0) {
            cmd_dns(line + 4);
        } else if (strcmp(line, "tls") == 0) {
            cmd_tls();
        } else if (strcmp(line, "config") == 0) {
            claude_interactive_setup();
        } else if (strncmp(line, "claude ", 7) == 0) {
            cmd_claude(line + 7);
        } else if (strcmp(line, "panic") == 0) {
            extern void kernel_panic(const char* message);
            kernel_panic("User-initiated panic. This is fine. Everything is fine.");
        } else if (strcmp(line, "reboot") == 0) {
            cmd_reboot();
        } else {
            /* Unrecognized command — send to Claude! */
            cmd_unknown(line);
        }
    }
}
