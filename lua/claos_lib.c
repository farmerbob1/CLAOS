/*
 * CLAOS — Claude Assisted Operating System
 * claos_lib.c — CLAOS Lua API Bindings
 *
 * Registers the `claos` library in Lua with functions for:
 *   - Talking to Claude (claos.ask)
 *   - System info (claos.uptime, claos.mem_free, etc.)
 *   - Filesystem (claos.read, claos.write, claos.ls)
 *   - Display (claos.print, claos.clear, claos.set_color)
 *
 * "Lua 5.5 awakened. The scripting layer stirs."
 */

#include "claos_lib.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "pmm.h"
#include "heap.h"
#include "scheduler.h"
#include "claude.h"
#include "chaosfs.h"
#include "string.h"
#include "io.h"

/* Global Lua state */
static lua_State* global_L = NULL;

/* ─── claos.ask(prompt) → response ─── */
static int l_ask(lua_State* L) {
    const char* prompt = luaL_checkstring(L, 1);

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("  [CLAOS -> Claude] Sending...\n");

    static char response[CLAUDE_RESPONSE_MAX];
    int len = claude_ask(prompt, response, sizeof(response));

    if (len > 0) {
        lua_pushlstring(L, response, len);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

/* ─── claos.print(text) ─── */
static int l_print(lua_State* L) {
    int nargs = lua_gettop(L);
    for (int i = 1; i <= nargs; i++) {
        const char* s = luaL_tolstring(L, i, NULL);
        if (s) {
            vga_print(s);
            if (i < nargs) vga_putchar('\t');
        }
        lua_pop(L, 1);
    }
    vga_print("\n");
    return 0;
}

/* ─── claos.uptime() → seconds ─── */
static int l_uptime(lua_State* L) {
    lua_pushinteger(L, timer_get_uptime());
    return 1;
}

/* ─── claos.mem_free() → bytes ─── */
static int l_mem_free(lua_State* L) {
    lua_pushinteger(L, pmm_get_free_pages() * 4096);
    return 1;
}

/* ─── claos.mem_total() → bytes ─── */
static int l_mem_total(lua_State* L) {
    lua_pushinteger(L, pmm_get_total_memory());
    return 1;
}

/* ─── claos.read(path) → string or nil ─── */
static int l_read(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    static char buf[4096];
    int len = chaosfs_read(path, buf, sizeof(buf));
    if (len >= 0) {
        lua_pushlstring(L, buf, len);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

/* ─── claos.write(path, data) → bool ─── */
static int l_write(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    size_t len;
    const char* data = luaL_checklstring(L, 2, &len);
    int result = chaosfs_write(path, data, len);
    lua_pushboolean(L, result == 0);
    return 1;
}

/* ─── claos.ls(path) → table of filenames ─── */
struct ls_lua_ctx {
    lua_State* L;
    int index;
};

static void ls_lua_callback(const struct chaosfs_entry* entry, void* ctx) {
    struct ls_lua_ctx* lctx = (struct ls_lua_ctx*)ctx;
    lua_pushstring(lctx->L, entry->filename);
    lua_rawseti(lctx->L, -2, lctx->index++);
}

static int l_ls(lua_State* L) {
    const char* path = luaL_optstring(L, 1, "/");
    lua_newtable(L);
    struct ls_lua_ctx ctx = { L, 1 };
    chaosfs_list(path, ls_lua_callback, &ctx);
    return 1;
}

/* ─── claos.sleep(ms) ─── */
static int l_sleep(lua_State* L) {
    int ms = luaL_checkinteger(L, 1);
    task_sleep(ms);
    return 0;
}

/* ─── claos.set_color(fg, bg) ─── */
static int l_set_color(lua_State* L) {
    int fg = luaL_checkinteger(L, 1);
    int bg = luaL_checkinteger(L, 2);
    vga_set_color(fg, bg);
    return 0;
}

/* ─── claos.clear() ─── */
static int l_clear(lua_State* L) {
    (void)L;
    vga_clear();
    return 0;
}

/* ─── claos.input(prompt) → string ─── */
static int l_input(lua_State* L) {
    const char* prompt = luaL_optstring(L, 1, "");
    if (prompt[0]) vga_print(prompt);
    char buf[512];
    keyboard_readline(buf, sizeof(buf));
    lua_pushstring(L, buf);
    return 1;
}

/* Library function table */
static const luaL_Reg claos_funcs[] = {
    {"ask",       l_ask},
    {"print",     l_print},
    {"uptime",    l_uptime},
    {"mem_free",  l_mem_free},
    {"mem_total", l_mem_total},
    {"read",      l_read},
    {"write",     l_write},
    {"ls",        l_ls},
    {"sleep",     l_sleep},
    {"set_color", l_set_color},
    {"clear",     l_clear},
    {"input",     l_input},
    {NULL, NULL}
};

/* Register the claos library */
void claos_lua_register(lua_State* L) {
    serial_print("[LUA] Registering claos lib...\n");
    /* Use luaL_newlibtable + luaL_setfuncs instead of luaL_newlib
     * to avoid luaL_checkversion which can crash in freestanding. */
    luaL_newlibtable(L, claos_funcs);
    luaL_setfuncs(L, claos_funcs, 0);
    serial_print("[LUA] newlib OK, setting global...\n");
    lua_setglobal(L, "claos");
    serial_print("[LUA] claos global set\n");

    /* Override the global print with our VGA version */
    lua_pushcfunction(L, l_print);
    lua_setglobal(L, "print");
    serial_print("[LUA] print override set\n");
}

/* Initialize the Lua subsystem */
void lua_subsystem_init(void) {
    serial_print("[LUA] Creating Lua state...\n");

    global_L = luaL_newstate();
    if (!global_L) {
        serial_print("[LUA] Failed to create Lua state!\n");
        return;
    }

    serial_print("[LUA] State created, opening base lib...\n");

    /* Open only safe libraries one at a time with debug output */
    luaL_requiref(global_L, "_G", luaopen_base, 1);
    lua_pop(global_L, 1);
    serial_print("[LUA] base OK\n");

    luaL_requiref(global_L, "string", luaopen_string, 1);
    lua_pop(global_L, 1);
    serial_print("[LUA] string OK\n");

    luaL_requiref(global_L, "table", luaopen_table, 1);
    lua_pop(global_L, 1);
    serial_print("[LUA] table OK\n");

    luaL_requiref(global_L, "math", luaopen_math, 1);
    lua_pop(global_L, 1);
    serial_print("[LUA] math OK\n");

    /* Register CLAOS API */
    claos_lua_register(global_L);

    serial_print("[LUA] Lua 5.5 initialized\n");
}

/* Run a Lua script from ChaosFS */
int lua_run_file(const char* path) {
    if (!global_L) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  Lua not initialized.\n");
        return -1;
    }

    /* Read the file */
    static char script_buf[8192];
    int len = chaosfs_read(path, script_buf, sizeof(script_buf) - 1);
    if (len < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  File not found: ");
        vga_print(path);
        vga_print("\n");
        return -1;
    }
    script_buf[len] = '\0';

    /* Execute */
    int status = luaL_dostring(global_L, script_buf);
    if (status != 0) {
        const char* err = lua_tostring(global_L, -1);
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  Lua error: ");
        vga_print(err ? err : "(unknown)");
        vga_print("\n");
        lua_pop(global_L, 1);
        return -1;
    }

    return 0;
}

/* Run inline Lua code */
int lua_run_string(const char* code) {
    if (!global_L) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  Lua not initialized.\n");
        return -1;
    }

    int status = luaL_dostring(global_L, code);
    if (status != 0) {
        const char* err = lua_tostring(global_L, -1);
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  Lua error: ");
        vga_print(err ? err : "(unknown)");
        vga_print("\n");
        lua_pop(global_L, 1);
        return -1;
    }

    return 0;
}

/* Interactive Lua REPL */
void lua_repl(void) {
    if (!global_L) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_print("  Lua not initialized.\n");
        return;
    }

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("  Lua 5.5 REPL - type 'exit' to return to CLAOS shell\n\n");

    char line[512];
    while (1) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_print("lua> ");
        vga_set_color(VGA_WHITE, VGA_BLACK);

        keyboard_readline(line, sizeof(line));

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            break;
        }

        if (strlen(line) == 0) continue;

        /* Try as expression first (prefix with "return ") for convenience */
        char expr_buf[600];
        expr_buf[0] = '\0';
        strcat(expr_buf, "return ");
        strcat(expr_buf, line);

        int status = luaL_dostring(global_L, expr_buf);
        if (status != 0) {
            lua_pop(global_L, 1);
            /* Try as statement */
            status = luaL_dostring(global_L, line);
        }

        if (status == 0) {
            /* Print any return values */
            int nresults = lua_gettop(global_L);
            if (nresults > 0) {
                for (int i = 1; i <= nresults; i++) {
                    const char* s = luaL_tolstring(global_L, i, NULL);
                    if (s) {
                        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
                        vga_print("  ");
                        vga_print(s);
                        vga_print("\n");
                    }
                    lua_pop(global_L, 1);
                }
                lua_settop(global_L, 0);
            }
        } else {
            const char* err = lua_tostring(global_L, -1);
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_print("  ");
            vga_print(err ? err : "(error)");
            vga_print("\n");
            lua_pop(global_L, 1);
        }
    }

    vga_set_color(VGA_WHITE, VGA_BLACK);
}
