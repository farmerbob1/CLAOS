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
#include "chaosfs.h"
#include "claos_lib.h"
#include "fb.h"
#include "input.h"
#include "mouse.h"
#include "console.h"

/* Shell input buffer */
#define SHELL_BUF_SIZE 1024

/* ─── Command Handlers ─── */

/* Helper to print a command entry in the help list */
static void help_cmd(const char* cmd, const char* desc) {
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("  ");
    vga_print(cmd);
    /* Pad to column 18 */
    int len = strlen(cmd) + 2;
    while (len < 18) { vga_putchar(' '); len++; }
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print(desc);
    vga_print("\n");
}

static void cmd_help(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("\n  ── ClaudeShell Commands ──\n");

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("\n  Claude\n");
    help_cmd("claude <msg>", "Ask Claude a question");
    help_cmd("config", "Configure API key & model");

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("\n  Files (ChaosFS)\n");
    help_cmd("dir [path]", "List files");
    help_cmd("read <file>", "Display file contents");
    help_cmd("write <f> <d>", "Write data to file");
    help_cmd("mkdir <path>", "Create directory");
    help_cmd("del <file>", "Delete file");
    help_cmd("disk", "Show disk usage");

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("\n  Lua Scripting\n");
    help_cmd("lua", "Open Lua REPL");
    help_cmd("lua <file>", "Run a Lua script");
    help_cmd("luarun <code>", "Execute inline Lua");

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("\n  System\n");
    help_cmd("sysinfo", "System information");
    help_cmd("tasks", "List running tasks");
    help_cmd("uptime", "Show system uptime");
    help_cmd("clear", "Clear the screen");

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("\n  Network\n");
    help_cmd("net", "Network configuration");
    help_cmd("dns <host>", "Resolve a hostname");
    help_cmd("tls", "Test TLS handshake");

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("\n  Danger Zone\n");
    help_cmd("panic", "Trigger a kernel panic");
    help_cmd("reboot", "Reboot the system");

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
    vga_print("  [CLAOS -> Claude] Sending (agent mode)...\n");

    static char response[CLAUDE_RESPONSE_MAX];
    int len = claude_ask_with_tools(prompt, response, sizeof(response));

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

/* ─── Filesystem Commands ─── */

/* Callback for ls — prints each file entry */
static void ls_print_entry(const struct chaosfs_entry* entry, void* ctx) {
    (void)ctx;
    vga_print("  ");
    if (entry->flags & CHAOSFS_FLAG_DIR) {
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("[DIR]  ");
    } else {
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vga_print_dec(entry->size);
        int digits = 0;
        uint32_t tmp = entry->size;
        do { digits++; tmp /= 10; } while (tmp > 0);
        for (int i = digits; i < 6; i++) vga_putchar(' ');
        vga_print(" ");
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print(entry->filename);
    vga_print("\n");
}

static void cmd_ls(const char* path) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("  ── Files ──\n");
    chaosfs_list(path, ls_print_entry, NULL);
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static void cmd_read(const char* path) {
    static char file_buf[4096];
    int len = chaosfs_read(path, file_buf, sizeof(file_buf) - 1);
    if (len < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  File not found: ");
        vga_print(path);
        vga_print("\n");
    } else {
        file_buf[len] = '\0';
        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_print(file_buf);
        if (len > 0 && file_buf[len - 1] != '\n')
            vga_print("\n");
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static void cmd_write(const char* args) {
    /* Parse: first word is path, rest is content */
    const char* p = args;
    while (*p && *p != ' ') p++;
    int path_len = p - args;
    if (path_len == 0 || *p == '\0') {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  Usage: write <path> <content>\n");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        return;
    }

    char path[CHAOSFS_MAX_FILENAME];
    if (path_len >= CHAOSFS_MAX_FILENAME) path_len = CHAOSFS_MAX_FILENAME - 1;
    memcpy(path, args, path_len);
    path[path_len] = '\0';

    const char* content = p + 1;  /* Skip the space */
    int content_len = strlen(content);

    if (chaosfs_write(path, content, content_len) == 0) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("  Written ");
        vga_print_dec(content_len);
        vga_print(" bytes to ");
        vga_print(path);
        vga_print("\n");
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  Failed to write to ");
        vga_print(path);
        vga_print("\n");
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static void cmd_mkdir(const char* path) {
    if (chaosfs_mkdir(path) == 0) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("  Created directory: ");
        vga_print(path);
        vga_print("\n");
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  Failed to create directory\n");
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static void cmd_del(const char* path) {
    if (chaosfs_delete(path) == 0) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("  Deleted: ");
        vga_print(path);
        vga_print("\n");
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  File not found: ");
        vga_print(path);
        vga_print("\n");
    }
    vga_set_color(VGA_WHITE, VGA_BLACK);
}

static void cmd_disk(void) {
    uint32_t total_blocks, used_blocks, file_count, block_size;
    chaosfs_disk_stats(&total_blocks, &used_blocks, &file_count, &block_size);

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("  ── ChaosFS Disk ──\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Filesystem:  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print("ChaosFS v1\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Block size:  ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print_dec(block_size);
    vga_print(" bytes\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Total:       ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print_dec(total_blocks * (block_size / 1024));
    vga_print("KB (");
    vga_print_dec(total_blocks);
    vga_print(" blocks)\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Used:        ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print_dec(used_blocks * (block_size / 1024));
    vga_print("KB (");
    vga_print_dec(used_blocks);
    vga_print(" blocks)\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Free:        ");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print_dec((total_blocks - used_blocks) * (block_size / 1024));
    vga_print("KB (");
    vga_print_dec(total_blocks - used_blocks);
    vga_print(" blocks)\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Files:       ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print_dec(file_count);
    vga_print("\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
}

/* Send an unrecognized command to Claude with context */
static void cmd_unknown(const char* input) {
    /* Build a contextualized prompt for Claude */
    static char prompt[1200];
    static const char prefix[] =
        "You are Claude, the AI integrated into CLAOS (Claude Assisted Operating System). "
        "The user typed the following at the claos> shell prompt. "
        "If it looks like a question or conversation, respond naturally. "
        "If it looks like a command, explain what it would do or suggest alternatives. "
        "Keep your response concise (1-3 sentences).\n\n"
        "User typed: ";

    size_t prefix_len = sizeof(prefix) - 1;
    size_t input_len = strlen(input);
    size_t max_input = sizeof(prompt) - prefix_len - 1;

    memcpy(prompt, prefix, prefix_len);
    if (input_len > max_input) input_len = max_input;
    memcpy(prompt + prefix_len, input, input_len);
    prompt[prefix_len + input_len] = '\0';

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
    vga_print("  CLAOS v0.9 ready. (Claude Agent Mode enabled)\n");
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
        } else if (strcmp(line, "dir") == 0 || strncmp(line, "dir ", 4) == 0) {
            const char* path = strlen(line) > 4 ? line + 4 : "/";
            cmd_ls(path);
        } else if (strcmp(line, "ls") == 0 || strncmp(line, "ls ", 3) == 0) {
            const char* path = strlen(line) > 3 ? line + 3 : "/";
            cmd_ls(path);  /* ls alias for dir */
        } else if (strncmp(line, "read ", 5) == 0) {
            cmd_read(line + 5);
        } else if (strncmp(line, "cat ", 4) == 0) {
            cmd_read(line + 4);  /* cat alias for read */
        } else if (strncmp(line, "write ", 6) == 0) {
            cmd_write(line + 6);
        } else if (strncmp(line, "mkdir ", 6) == 0) {
            cmd_mkdir(line + 6);
        } else if (strncmp(line, "del ", 4) == 0) {
            cmd_del(line + 4);
        } else if (strncmp(line, "rm ", 3) == 0) {
            cmd_del(line + 3);  /* rm alias for del */
        } else if (strcmp(line, "disk") == 0) {
            cmd_disk();
        } else if (strcmp(line, "lua") == 0) {
            lua_repl();
        } else if (strncmp(line, "lua ", 4) == 0) {
            lua_run_file(line + 4);
        } else if (strncmp(line, "luarun ", 7) == 0) {
            lua_run_string(line + 7);
        } else if (strcmp(line, "tls") == 0) {
            cmd_tls();
        } else if (strcmp(line, "config") == 0) {
            claude_interactive_setup();
        } else if (strncmp(line, "claude ", 7) == 0) {
            cmd_claude(line + 7);
        } else if (strcmp(line, "gui") == 0) {
            /* Launch GUI — activate VESA and run /system/gui/init.lua */
            if (*(volatile uint8_t*)0x2000 != 1) {
                vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                vga_print("  No VBE graphics available.\n");
            } else {
                vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
                vga_print("  Launching GUI...\n");
                /* Activate VESA mode from C */
                if (!fb_activate()) {
                    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                    vga_print("  Failed to activate VESA mode.\n");
                } else {
                    const fb_info_t* info = fb_get_info();
                    mouse_set_bounds(info->width, info->height);
                    input_set_gui_mode(true);
                    /* Run the GUI script — errors print to serial + VGA */
                    lua_run_file("/system/gui/init.lua");
                    /* If script returns (ESC pressed), switch back to text mode */
                    input_set_gui_mode(false);
                    vga_set_framebuffer_mode(false);
                }
            }
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
