/*
 * CLAOS — Claude Assisted Operating System
 * claos_lib.h — CLAOS Lua API Bindings
 */

#ifndef CLAOS_LUA_LIB_H
#define CLAOS_LUA_LIB_H

/* Forward declare Lua state */
struct lua_State;
typedef struct lua_State lua_State;

/* Register the claos.* Lua library */
void claos_lua_register(lua_State* L);

/* Initialize the Lua subsystem */
void lua_subsystem_init(void);

/* Run a Lua script from ChaosFS */
int lua_run_file(const char* path);

/* Run inline Lua code */
int lua_run_string(const char* code);

/* Open the interactive Lua REPL */
void lua_repl(void);

#endif /* CLAOS_LUA_LIB_H */
