/*
 * CLAOS — Claude Assisted Operating System
 * claude_system.c — System Prompt, Sysinfo, and Tool Definitions
 *
 * Provides:
 *   build_sysinfo_string()  — live system state snapshot
 *   build_system_prompt()   — full system prompt with sysinfo
 *   get_tools_json()        — JSON array of all tool definitions
 */

#include "claude_system.h"
#include "string.h"
#include "stdio.h"
#include "io.h"
#include "pmm.h"
#include "heap.h"
#include "timer.h"
#include "scheduler.h"
#include "e1000.h"
#include "ac97.h"
#include "chaosfs.h"
#include "fb.h"
#include "claude.h"

/* ─── Helper: append string ─── */
static int sappend(char* buf, int pos, int max, const char* str) {
    while (*str && pos < max - 1) buf[pos++] = *str++;
    return pos;
}

/* Helper: append decimal integer */
static int sappend_int(char* buf, int pos, int max, uint32_t val) {
    char num[12]; int ni = 0;
    if (val == 0) { num[ni++] = '0'; }
    else {
        char rev[12]; int ri = 0;
        uint32_t tmp = val;
        while (tmp > 0) { rev[ri++] = '0' + tmp % 10; tmp /= 10; }
        while (ri > 0) num[ni++] = rev[--ri];
    }
    num[ni] = '\0';
    return sappend(buf, pos, max, num);
}

/* Helper: format uptime as HH:MM:SS */
static int sappend_uptime(char* buf, int pos, int max, uint32_t seconds) {
    uint32_t h = seconds / 3600;
    uint32_t m = (seconds % 3600) / 60;
    uint32_t s = seconds % 60;
    if (h < 10) pos = sappend(buf, pos, max, "0");
    pos = sappend_int(buf, pos, max, h);
    pos = sappend(buf, pos, max, ":");
    if (m < 10) pos = sappend(buf, pos, max, "0");
    pos = sappend_int(buf, pos, max, m);
    pos = sappend(buf, pos, max, ":");
    if (s < 10) pos = sappend(buf, pos, max, "0");
    pos = sappend_int(buf, pos, max, s);
    return pos;
}

/* ─── Sysinfo Builder ─── */

void build_sysinfo_string(char* buf, int max_len) {
    int pos = 0;

    /* OS version */
    pos = sappend(buf, pos, max_len, "  OS: CLAOS v0.9 (i686)\n");

    /* Uptime */
    pos = sappend(buf, pos, max_len, "  Uptime: ");
    pos = sappend_uptime(buf, pos, max_len, timer_get_uptime());
    pos = sappend(buf, pos, max_len, "\n");

    /* Memory */
    uint32_t total_mb = pmm_get_total_memory() / (1024 * 1024);
    uint32_t used_pages = pmm_get_used_pages();
    uint32_t total_pages = pmm_get_total_memory() / 4096;
    uint32_t used_mb = (used_pages * 4) / 1024;
    uint32_t pct = total_pages > 0 ? (used_pages * 100) / total_pages : 0;
    pos = sappend(buf, pos, max_len, "  Memory: ");
    pos = sappend_int(buf, pos, max_len, used_mb);
    pos = sappend(buf, pos, max_len, " / ");
    pos = sappend_int(buf, pos, max_len, total_mb);
    pos = sappend(buf, pos, max_len, " MB used (");
    pos = sappend_int(buf, pos, max_len, pct);
    pos = sappend(buf, pos, max_len, "%)\n");

    /* Tasks */
    int task_count = task_get_count();
    pos = sappend(buf, pos, max_len, "  Tasks: ");
    pos = sappend_int(buf, pos, max_len, (uint32_t)task_count);
    pos = sappend(buf, pos, max_len, " active [");
    int first = 1;
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task* t = task_get(i);
        if (!t) continue;
        if (!first) pos = sappend(buf, pos, max_len, ", ");
        pos = sappend(buf, pos, max_len, t->name);
        first = 0;
    }
    pos = sappend(buf, pos, max_len, "]\n");

    /* ChaosFS */
    uint32_t total_blocks, used_blocks, file_count, block_size;
    chaosfs_disk_stats(&total_blocks, &used_blocks, &file_count, &block_size);
    uint32_t used_kb = used_blocks * (block_size / 1024);
    uint32_t free_kb = (total_blocks - used_blocks) * (block_size / 1024);
    pos = sappend(buf, pos, max_len, "  ChaosFS: mounted, ");
    if (used_kb >= 1024) {
        pos = sappend_int(buf, pos, max_len, used_kb / 1024);
        pos = sappend(buf, pos, max_len, ".");
        pos = sappend_int(buf, pos, max_len, (used_kb % 1024) / 100);
        pos = sappend(buf, pos, max_len, " MB used / ");
    } else {
        pos = sappend_int(buf, pos, max_len, used_kb);
        pos = sappend(buf, pos, max_len, " KB used / ");
    }
    if (free_kb >= 1024) {
        pos = sappend_int(buf, pos, max_len, free_kb / 1024);
        pos = sappend(buf, pos, max_len, ".");
        pos = sappend_int(buf, pos, max_len, (free_kb % 1024) / 100);
        pos = sappend(buf, pos, max_len, " MB free, ");
    } else {
        pos = sappend_int(buf, pos, max_len, free_kb);
        pos = sappend(buf, pos, max_len, " KB free, ");
    }
    pos = sappend_int(buf, pos, max_len, file_count);
    pos = sappend(buf, pos, max_len, " files\n");

    /* Network */
    pos = sappend(buf, pos, max_len, "  Network: e1000 @ 10.0.2.15, gateway 10.0.2.2, DNS 10.0.2.3 -- ");
    if (e1000_link_up()) {
        pos = sappend(buf, pos, max_len, "CONNECTED\n");
    } else {
        pos = sappend(buf, pos, max_len, "DISCONNECTED\n");
    }

    /* Audio */
    pos = sappend(buf, pos, max_len, "  Audio: AC97 ");
    if (ac97_is_playing()) {
        pos = sappend(buf, pos, max_len, "playing");
    } else {
        pos = sappend(buf, pos, max_len, "idle");
    }
    pos = sappend(buf, pos, max_len, ", volume ");
    pos = sappend_int(buf, pos, max_len, (uint32_t)ac97_get_volume());
    pos = sappend(buf, pos, max_len, "%\n");

    /* GUI */
    pos = sappend(buf, pos, max_len, "  GUI: ");
    if (fb_is_active()) {
        const fb_info_t* info = fb_get_info();
        pos = sappend(buf, pos, max_len, "running, resolution=");
        pos = sappend_int(buf, pos, max_len, info->width);
        pos = sappend(buf, pos, max_len, "x");
        pos = sappend_int(buf, pos, max_len, info->height);
        pos = sappend(buf, pos, max_len, "x32\n");
    } else {
        pos = sappend(buf, pos, max_len, "text mode (VGA)\n");
    }

    /* Claude status */
    pos = sappend(buf, pos, max_len, "  Claude: ");
    if (claude_is_configured()) {
        pos = sappend(buf, pos, max_len, "connected via HTTPS to api.anthropic.com, model=");
        pos = sappend(buf, pos, max_len, claude_get_model());
        pos = sappend(buf, pos, max_len, "\n");
    } else {
        pos = sappend(buf, pos, max_len, "not configured\n");
    }

    if (pos < max_len) buf[pos] = '\0';
}

/* ─── System Prompt Builder ─── */

void build_system_prompt(char* buf, int max_len) {
    int pos = 0;

    pos = sappend(buf, pos, max_len,
        "You are Claude, running natively inside CLAOS (Claude Assisted Operating System), "
        "an x86 OS built from scratch with custom TCP/IP, TLS, and HTTPS.\n"
        "This is YOUR home. Use tools to DO things, not just explain them.\n\n"
        "System state:\n");

    char sysinfo[1024];
    build_sysinfo_string(sysinfo, sizeof(sysinfo));
    pos = sappend(buf, pos, max_len, sysinfo);

    pos = sappend(buf, pos, max_len,
        "\nRules: Use tools proactively. Use run_lua for complex tasks. "
        "Call reload_gui after GUI file changes. Be conversational.\n");

    if (pos < max_len) buf[pos] = '\0';
}

/* ─── Tool Definitions JSON ─── */

const char* get_tools_json(void) {
    return
    "["
    "{\"name\":\"read_file\",\"description\":\"Read a file from ChaosFS.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
    "{\"name\":\"write_file\",\"description\":\"Write/create a file on ChaosFS.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}},"
    "{\"name\":\"list_dir\",\"description\":\"List files at a path.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
    "{\"name\":\"delete_file\",\"description\":\"Delete a file.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
    "{\"name\":\"file_info\",\"description\":\"Get file size and type.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
    "{\"name\":\"sysinfo\",\"description\":\"Get system info: memory, uptime, tasks, network.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"list_tasks\",\"description\":\"List running tasks.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"kill_task\",\"description\":\"Kill a task by ID.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"task_id\":{\"type\":\"integer\"}},\"required\":[\"task_id\"]}},"
    "{\"name\":\"run_command\",\"description\":\"Run a shell command.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\"}},\"required\":[\"command\"]}},"
    "{\"name\":\"read_theme\",\"description\":\"Read GUI theme.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"set_theme\",\"description\":\"Set theme: light or dark.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"theme\":{\"type\":\"string\"}},\"required\":[\"theme\"]}},"
    "{\"name\":\"reload_gui\",\"description\":\"Reload GUI from init.lua.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"list_windows\",\"description\":\"List open windows.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"screenshot\",\"description\":\"Save screen to file.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
    "{\"name\":\"net_status\",\"description\":\"Get network status.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"dns_lookup\",\"description\":\"Resolve hostname to IP.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"hostname\":{\"type\":\"string\"}},\"required\":[\"hostname\"]}},"
    "{\"name\":\"http_get\",\"description\":\"Fetch a URL (max 4KB).\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"url\":{\"type\":\"string\"}},\"required\":[\"url\"]}},"
    "{\"name\":\"play_tone\",\"description\":\"Play sine wave via AC97.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"frequency\":{\"type\":\"integer\"},\"duration\":{\"type\":\"integer\"}},\"required\":[\"frequency\",\"duration\"]}},"
    "{\"name\":\"play_sound\",\"description\":\"Play a WAV file.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}},"
    "{\"name\":\"set_volume\",\"description\":\"Set volume 0-100.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"level\":{\"type\":\"integer\"}},\"required\":[\"level\"]}},"
    "{\"name\":\"stop_audio\",\"description\":\"Stop audio playback.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"run_lua\",\"description\":\"Execute Lua code with full CLAOS API access.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"code\":{\"type\":\"string\"}},\"required\":[\"code\"]}}"
    "]";
}
