/*
 * CLAOS — Claude Assisted Operating System
 * claude_tools.c — Tool Executor Dispatch
 *
 * Implements all tool handlers that Claude can invoke:
 *   Filesystem: read_file, write_file, list_dir, delete_file, file_info
 *   System:     sysinfo, list_tasks, kill_task, run_command
 *   GUI:        read_theme, set_theme, reload_gui, list_windows, screenshot
 *   Network:    net_status, dns_lookup, http_get
 *   Audio:      play_tone, play_sound, set_volume, stop_audio
 *   Lua:        run_lua
 *
 * "The AI doesn't just live in the OS. It runs the OS."
 */

#include "claude_tools.h"
#include "claude_system.h"
#include "json.h"
#include "string.h"
#include "stdio.h"
#include "io.h"
#include "chaosfs.h"
#include "scheduler.h"
#include "timer.h"
#include "pmm.h"
#include "heap.h"
#include "e1000.h"
#include "dns.h"
#include "net.h"
#include "ac97.h"
#include "fb.h"
#include "claos_lib.h"

/* ─── Helpers ─── */

static int tappend(char* buf, int pos, int max, const char* str) {
    while (*str && pos < max - 1) buf[pos++] = *str++;
    return pos;
}

static int tappend_int(char* buf, int pos, int max, uint32_t val) {
    char num[12]; int ni = 0;
    if (val == 0) { num[ni++] = '0'; }
    else {
        char rev[12]; int ri = 0;
        uint32_t tmp = val;
        while (tmp > 0) { rev[ri++] = '0' + tmp % 10; tmp /= 10; }
        while (ri > 0) num[ni++] = rev[--ri];
    }
    num[ni] = '\0';
    return tappend(buf, pos, max, num);
}

/* ─── Audit Log ─── */

/* Static log buffer — accumulates tool log entries */
static char log_buffer[4096];
static int log_len = 0;

void tool_log(const char* name, const char* args_summary, const char* status) {
    /* Format: [UPTIME] TOOL: name(args) → status */
    int pos = log_len;
    int max = sizeof(log_buffer);

    pos = tappend(log_buffer, pos, max, "[");
    uint32_t up = timer_get_uptime();
    uint32_t h = up / 3600, m = (up % 3600) / 60, s = up % 60;
    if (h < 10) pos = tappend(log_buffer, pos, max, "0");
    pos = tappend_int(log_buffer, pos, max, h);
    pos = tappend(log_buffer, pos, max, ":");
    if (m < 10) pos = tappend(log_buffer, pos, max, "0");
    pos = tappend_int(log_buffer, pos, max, m);
    pos = tappend(log_buffer, pos, max, ":");
    if (s < 10) pos = tappend(log_buffer, pos, max, "0");
    pos = tappend_int(log_buffer, pos, max, s);
    pos = tappend(log_buffer, pos, max, "] TOOL: ");
    pos = tappend(log_buffer, pos, max, name);
    pos = tappend(log_buffer, pos, max, "(");
    /* Truncate args to 60 chars for the log */
    int args_len = strlen(args_summary);
    if (args_len > 60) args_len = 60;
    for (int i = 0; i < args_len && pos < max - 1; i++)
        log_buffer[pos++] = args_summary[i];
    pos = tappend(log_buffer, pos, max, ") -> ");
    pos = tappend(log_buffer, pos, max, status);
    pos = tappend(log_buffer, pos, max, "\n");
    log_buffer[pos] = '\0';
    log_len = pos;

    /* Write log to ChaosFS */
    chaosfs_write("/logs/claude_tools.log", log_buffer, log_len);

    /* Also print to serial for debugging */
    serial_print("[TOOL] ");
    serial_print(name);
    serial_print(" -> ");
    serial_print(status);
    serial_putchar('\n');
}

/* ─── Filesystem Tools ─── */

static void tool_read_file(const char* args_json, char* result, int max_len) {
    char path[256];
    json_get_string(args_json, "path", path, sizeof(path));

    static char file_buf[4096];
    int len = chaosfs_read(path, file_buf, sizeof(file_buf) - 1);
    if (len < 0) {
        snprintf(result, max_len, "Error: File not found: %s", path);
        tool_log("read_file", path, "NOT FOUND");
    } else {
        file_buf[len] = '\0';
        /* Copy to result, truncate if needed */
        int copy_len = len;
        if (copy_len >= max_len) copy_len = max_len - 1;
        memcpy(result, file_buf, copy_len);
        result[copy_len] = '\0';

        char status[64];
        snprintf(status, sizeof(status), "OK (%d bytes)", len);
        tool_log("read_file", path, status);
    }
}

static void tool_write_file(const char* args_json, char* result, int max_len) {
    char path[256];
    static char content[8192]; /* 8KB max */
    json_get_string(args_json, "path", path, sizeof(path));
    int content_len = json_get_string(args_json, "content", content, sizeof(content));

    if (content_len < 0) content_len = 0;

    /* Safety: block writes to /system/kernel/ */
    if (strncmp(path, "/system/kernel/", 15) == 0) {
        snprintf(result, max_len, "Error: Cannot write to protected path %s", path);
        tool_log("write_file", path, "BLOCKED (protected)");
        return;
    }

    if (chaosfs_write(path, content, content_len) == 0) {
        snprintf(result, max_len, "Written %d bytes to %s", content_len, path);
        char status[64];
        snprintf(status, sizeof(status), "OK (wrote %d bytes)", content_len);
        tool_log("write_file", path, status);
    } else {
        snprintf(result, max_len, "Error: Failed to write to %s", path);
        tool_log("write_file", path, "FAILED");
    }
}

/* list_dir callback context */
struct list_dir_ctx {
    char* buf;
    int   pos;
    int   max;
};

static void list_dir_callback(const struct chaosfs_entry* entry, void* ctx) {
    struct list_dir_ctx* lc = (struct list_dir_ctx*)ctx;
    if (entry->flags & CHAOSFS_FLAG_DIR) {
        lc->pos = tappend(lc->buf, lc->pos, lc->max, "D ");
    } else {
        lc->pos = tappend(lc->buf, lc->pos, lc->max, "F ");
    }
    lc->pos = tappend(lc->buf, lc->pos, lc->max, entry->filename);
    if (!(entry->flags & CHAOSFS_FLAG_DIR)) {
        lc->pos = tappend(lc->buf, lc->pos, lc->max, " (");
        lc->pos = tappend_int(lc->buf, lc->pos, lc->max, entry->size);
        lc->pos = tappend(lc->buf, lc->pos, lc->max, "B)");
    }
    lc->pos = tappend(lc->buf, lc->pos, lc->max, "\n");
}

static void tool_list_dir(const char* args_json, char* result, int max_len) {
    char path[256];
    json_get_string(args_json, "path", path, sizeof(path));
    if (path[0] == '\0') strcpy(path, "/");

    struct list_dir_ctx ctx = { result, 0, max_len };
    chaosfs_list(path, list_dir_callback, &ctx);

    if (ctx.pos == 0) {
        snprintf(result, max_len, "No files found at %s", path);
    } else {
        result[ctx.pos] = '\0';
    }
    tool_log("list_dir", path, "OK");
}

static void tool_delete_file(const char* args_json, char* result, int max_len) {
    char path[256];
    json_get_string(args_json, "path", path, sizeof(path));

    if (chaosfs_delete(path) == 0) {
        snprintf(result, max_len, "Deleted %s", path);
        tool_log("delete_file", path, "OK");
    } else {
        snprintf(result, max_len, "Error: File not found: %s", path);
        tool_log("delete_file", path, "NOT FOUND");
    }
}

static void tool_file_info(const char* args_json, char* result, int max_len) {
    char path[256];
    json_get_string(args_json, "path", path, sizeof(path));

    uint32_t size;
    uint8_t flags;
    if (chaosfs_stat(path, &size, &flags) == 0) {
        const char* type = (flags & CHAOSFS_FLAG_DIR) ? "directory" : "file";
        snprintf(result, max_len, "File: %s, Size: %u bytes, Type: %s",
                 path, (unsigned)size, type);
        tool_log("file_info", path, "OK");
    } else {
        snprintf(result, max_len, "Not found: %s", path);
        tool_log("file_info", path, "NOT FOUND");
    }
}

/* ─── System Tools ─── */

static void tool_sysinfo(char* result, int max_len) {
    build_sysinfo_string(result, max_len);
    tool_log("sysinfo", "", "OK");
}

static void tool_list_tasks(char* result, int max_len) {
    int pos = 0;
    pos = tappend(result, pos, max_len, "ID  STATE      NAME\n");
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task* t = task_get(i);
        if (!t) continue;
        pos = tappend_int(result, pos, max_len, t->id);
        pos = tappend(result, pos, max_len, "   ");
        switch (t->state) {
            case TASK_RUNNING:  pos = tappend(result, pos, max_len, "running    "); break;
            case TASK_READY:    pos = tappend(result, pos, max_len, "ready      "); break;
            case TASK_SLEEPING: pos = tappend(result, pos, max_len, "sleeping   "); break;
            default:            pos = tappend(result, pos, max_len, "unknown    "); break;
        }
        pos = tappend(result, pos, max_len, t->name);
        pos = tappend(result, pos, max_len, "\n");
    }
    result[pos] = '\0';
    tool_log("list_tasks", "", "OK");
}

static void tool_kill_task(const char* args_json, char* result, int max_len) {
    int task_id = json_get_int(args_json, "task_id");

    /* Check protected tasks */
    struct task* t = NULL;
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task* check = task_get(i);
        if (check && (int)check->id == task_id) {
            t = check;
            break;
        }
    }

    if (!t) {
        snprintf(result, max_len, "Error: Task %d not found", task_id);
        tool_log("kill_task", "unknown", "NOT FOUND");
        return;
    }

    /* Protect critical tasks */
    if (strcmp(t->name, "kernel_idle") == 0 ||
        strcmp(t->name, "claude_shell") == 0 ||
        strcmp(t->name, "gui_desktop") == 0 ||
        strcmp(t->name, "net_listener") == 0) {
        snprintf(result, max_len, "Error: Cannot kill protected task '%s'", t->name);
        tool_log("kill_task", t->name, "BLOCKED (protected)");
        return;
    }

    snprintf(result, max_len, "Task kill not yet implemented. Task '%s' (id=%d) running.", t->name, task_id);
    tool_log("kill_task", t->name, "NOT IMPL");
}

static void tool_run_command(const char* args_json, char* result, int max_len) {
    char cmd[512];
    json_get_string(args_json, "command", cmd, sizeof(cmd));

    /* For now, redirect to specific tools rather than executing shell commands.
     * The shell writes directly to VGA, so capturing output is complex. */
    snprintf(result, max_len,
        "Shell output capture not available. Use specific tools instead. Command: %s", cmd);
    tool_log("run_command", cmd, "REDIRECTED");
}

/* ─── GUI Tools ─── */

static void tool_read_theme(char* result, int max_len) {
    /* Read the GUI init.lua or a dedicated theme config */
    static char theme_buf[4096];
    int len = chaosfs_read("/system/gui/init.lua", theme_buf, sizeof(theme_buf) - 1);
    if (len < 0) {
        snprintf(result, max_len, "No GUI theme file found at /system/gui/init.lua");
        tool_log("read_theme", "", "NOT FOUND");
        return;
    }
    theme_buf[len] = '\0';

    /* Look for theme-related content in the file */
    int copy_len = len;
    if (copy_len >= max_len) copy_len = max_len - 1;
    memcpy(result, theme_buf, copy_len);
    result[copy_len] = '\0';
    tool_log("read_theme", "", "OK");
}

static void tool_set_theme(const char* args_json, char* result, int max_len) {
    char theme[32];
    json_get_string(args_json, "theme", theme, sizeof(theme));

    if (strcmp(theme, "light") != 0 && strcmp(theme, "dark") != 0) {
        snprintf(result, max_len, "Error: Unknown theme '%s'. Available: light, dark", theme);
        tool_log("set_theme", theme, "INVALID");
        return;
    }

    /* Update theme by running Lua code to change the active theme */
    char lua_code[256];
    snprintf(lua_code, sizeof(lua_code),
        "if claos_set_theme then claos_set_theme('%s') "
        "else print('Theme function not available in current GUI') end", theme);
    lua_run_string(lua_code);

    snprintf(result, max_len, "Theme change to '%s' requested. "
             "Use reload_gui to apply changes.", theme);
    tool_log("set_theme", theme, "OK");
}

static void tool_reload_gui(char* result, int max_len) {
    if (!fb_is_active()) {
        snprintf(result, max_len, "GUI is not active (text mode). Launch GUI first.");
        tool_log("reload_gui", "", "NOT ACTIVE");
        return;
    }

    int status = lua_run_string("dofile('/system/gui/init.lua')");
    if (status == 0) {
        snprintf(result, max_len, "GUI reloaded successfully");
        tool_log("reload_gui", "", "OK");
    } else {
        snprintf(result, max_len, "Error reloading GUI — check serial log for details");
        tool_log("reload_gui", "", "ERROR");
    }
}

static void tool_list_windows(char* result, int max_len) {
    if (!fb_is_active()) {
        snprintf(result, max_len, "GUI is not active (text mode). No windows to list.");
        tool_log("list_windows", "", "NOT ACTIVE");
        return;
    }

    /* Query window state from Lua */
    snprintf(result, max_len, "Window listing requires GUI Lua state query. "
             "Use run_lua to inspect claos.gui state directly.");
    tool_log("list_windows", "", "OK");
}

static void tool_screenshot(const char* args_json, char* result, int max_len) {
    char path[256];
    json_get_string(args_json, "path", path, sizeof(path));

    if (!fb_is_active()) {
        snprintf(result, max_len, "Cannot screenshot — GUI not active (text mode).");
        tool_log("screenshot", path, "NOT ACTIVE");
        return;
    }

    const fb_info_t* info = fb_get_info();
    uint32_t* backbuf = fb_get_backbuffer();
    uint32_t size = info->width * info->height * 4;

    /* Save raw ARGB data to ChaosFS */
    if (size > 32768) {
        /* Too large for a single ChaosFS write — save a smaller region or report size */
        snprintf(result, max_len, "Screenshot too large (%u bytes) for ChaosFS. "
                 "Framebuffer is %ux%ux32.", (unsigned)size, info->width, info->height);
        tool_log("screenshot", path, "TOO LARGE");
    } else {
        if (chaosfs_write(path, backbuf, size) == 0) {
            snprintf(result, max_len, "Screenshot saved to %s (%ux%u, %u bytes)",
                     path, info->width, info->height, (unsigned)size);
            tool_log("screenshot", path, "OK");
        } else {
            snprintf(result, max_len, "Error: Failed to save screenshot to %s", path);
            tool_log("screenshot", path, "FAILED");
        }
    }
}

/* ─── Network Tools ─── */

static void tool_net_status(char* result, int max_len) {
    int pos = 0;
    pos = tappend(result, pos, max_len, "IP: 10.0.2.15\n");
    pos = tappend(result, pos, max_len, "Gateway: 10.0.2.2\n");
    pos = tappend(result, pos, max_len, "DNS: 10.0.2.3\n");
    pos = tappend(result, pos, max_len, "NIC: Intel e1000\n");
    pos = tappend(result, pos, max_len, "Link: ");
    if (e1000_link_up()) {
        pos = tappend(result, pos, max_len, "UP\n");
    } else {
        pos = tappend(result, pos, max_len, "DOWN\n");
    }

    /* MAC address */
    uint8_t mac[6];
    e1000_get_mac(mac);
    pos = tappend(result, pos, max_len, "MAC: ");
    const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        if (i) result[pos++] = ':';
        if (pos < max_len - 1) result[pos++] = hex[mac[i] >> 4];
        if (pos < max_len - 1) result[pos++] = hex[mac[i] & 0xF];
    }
    pos = tappend(result, pos, max_len, "\n");
    result[pos] = '\0';
    tool_log("net_status", "", "OK");
}

static void tool_dns_lookup(const char* args_json, char* result, int max_len) {
    char hostname[256];
    json_get_string(args_json, "hostname", hostname, sizeof(hostname));

    uint32_t ip = dns_resolve(hostname);
    if (ip) {
        snprintf(result, max_len, "%s -> %u.%u.%u.%u",
                 hostname,
                 (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                 (ip >> 8) & 0xFF, ip & 0xFF);
        tool_log("dns_lookup", hostname, "OK");
    } else {
        snprintf(result, max_len, "DNS lookup failed for %s", hostname);
        tool_log("dns_lookup", hostname, "FAILED");
    }
}

static void tool_http_get(const char* args_json, char* result, int max_len) {
    char url[512];
    json_get_string(args_json, "url", url, sizeof(url));

    /* For now, http_get is not fully implemented (we only have https_post).
     * Report the limitation. */
    snprintf(result, max_len, "HTTP GET not yet implemented. URL: %s", url);
    tool_log("http_get", url, "NOT IMPLEMENTED");
}

/* ─── Audio Tools ─── */

static void tool_play_tone(const char* args_json, char* result, int max_len) {
    int freq = json_get_int(args_json, "frequency");
    int dur = json_get_int(args_json, "duration");

    /* Clamp frequency */
    if (freq < 20) freq = 20;
    if (freq > 20000) freq = 20000;
    /* Clamp duration */
    if (dur < 1) dur = 1;
    if (dur > 10000) dur = 10000;

    ac97_play_tone((uint32_t)freq, (uint32_t)dur);
    snprintf(result, max_len, "Playing %dHz for %dms", freq, dur);

    char args_summary[64];
    snprintf(args_summary, sizeof(args_summary), "%dHz, %dms", freq, dur);
    tool_log("play_tone", args_summary, "OK");
}

static void tool_play_sound(const char* args_json, char* result, int max_len) {
    char path[256];
    json_get_string(args_json, "path", path, sizeof(path));

    /* WAV playback not yet implemented */
    snprintf(result, max_len, "WAV playback not yet supported. Use play_tone instead.");
    tool_log("play_sound", path, "NOT IMPLEMENTED");
}

static void tool_set_volume(const char* args_json, char* result, int max_len) {
    int level = json_get_int(args_json, "level");
    if (level < 0) level = 0;
    if (level > 100) level = 100;

    ac97_set_volume(level);
    snprintf(result, max_len, "Volume set to %d%%", level);

    char args_summary[32];
    snprintf(args_summary, sizeof(args_summary), "%d%%", level);
    tool_log("set_volume", args_summary, "OK");
}

static void tool_stop_audio(char* result, int max_len) {
    ac97_stop();
    snprintf(result, max_len, "Audio stopped");
    tool_log("stop_audio", "", "OK");
}

/* ─── Lua Tool ─── */

static void tool_run_lua(const char* args_json, char* result, int max_len) {
    char code[8192];
    json_get_string(args_json, "code", code, sizeof(code));

    /* Execute the Lua code */
    int status = lua_run_string(code);
    if (status == 0) {
        snprintf(result, max_len, "Lua executed successfully");
        /* Truncate code for log */
        char code_preview[64];
        int clen = strlen(code);
        if (clen > 60) clen = 60;
        memcpy(code_preview, code, clen);
        code_preview[clen] = '\0';
        tool_log("run_lua", code_preview, "OK");
    } else {
        snprintf(result, max_len, "Lua error — check serial log for details");
        tool_log("run_lua", code, "ERROR");
    }
}

/* ─── Main Dispatch ─── */

void execute_tool(const char* name, const char* args_json,
                  char* result, int max_len) {

    /* Filesystem tools */
    if (strcmp(name, "read_file") == 0) {
        tool_read_file(args_json, result, max_len);
    }
    else if (strcmp(name, "write_file") == 0) {
        tool_write_file(args_json, result, max_len);
    }
    else if (strcmp(name, "list_dir") == 0) {
        tool_list_dir(args_json, result, max_len);
    }
    else if (strcmp(name, "delete_file") == 0) {
        tool_delete_file(args_json, result, max_len);
    }
    else if (strcmp(name, "file_info") == 0) {
        tool_file_info(args_json, result, max_len);
    }

    /* System tools */
    else if (strcmp(name, "sysinfo") == 0) {
        tool_sysinfo(result, max_len);
    }
    else if (strcmp(name, "list_tasks") == 0) {
        tool_list_tasks(result, max_len);
    }
    else if (strcmp(name, "kill_task") == 0) {
        tool_kill_task(args_json, result, max_len);
    }
    else if (strcmp(name, "run_command") == 0) {
        tool_run_command(args_json, result, max_len);
    }

    /* GUI tools */
    else if (strcmp(name, "read_theme") == 0) {
        tool_read_theme(result, max_len);
    }
    else if (strcmp(name, "set_theme") == 0) {
        tool_set_theme(args_json, result, max_len);
    }
    else if (strcmp(name, "reload_gui") == 0) {
        tool_reload_gui(result, max_len);
    }
    else if (strcmp(name, "list_windows") == 0) {
        tool_list_windows(result, max_len);
    }
    else if (strcmp(name, "screenshot") == 0) {
        tool_screenshot(args_json, result, max_len);
    }

    /* Network tools */
    else if (strcmp(name, "net_status") == 0) {
        tool_net_status(result, max_len);
    }
    else if (strcmp(name, "dns_lookup") == 0) {
        tool_dns_lookup(args_json, result, max_len);
    }
    else if (strcmp(name, "http_get") == 0) {
        tool_http_get(args_json, result, max_len);
    }

    /* Audio tools */
    else if (strcmp(name, "play_tone") == 0) {
        tool_play_tone(args_json, result, max_len);
    }
    else if (strcmp(name, "play_sound") == 0) {
        tool_play_sound(args_json, result, max_len);
    }
    else if (strcmp(name, "set_volume") == 0) {
        tool_set_volume(args_json, result, max_len);
    }
    else if (strcmp(name, "stop_audio") == 0) {
        tool_stop_audio(result, max_len);
    }

    /* The nuclear option */
    else if (strcmp(name, "run_lua") == 0) {
        tool_run_lua(args_json, result, max_len);
    }

    else {
        snprintf(result, max_len, "Unknown tool: %s", name);
        tool_log(name, "", "UNKNOWN");
    }
}
