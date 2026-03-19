/*
 * CLAOS 3D Engine — Lua API Bindings
 */

#ifndef CLAOS_GUI3D_LUA_H
#define CLAOS_GUI3D_LUA_H

#include "lua.h"

/* Create the claos.gui3d subtable and leave it on the Lua stack */
void gui3d_lua_open(lua_State* L);

#endif /* CLAOS_GUI3D_LUA_H */
