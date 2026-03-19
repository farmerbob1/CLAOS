/*
 * CLAOS 3D Engine — Lua API Bindings
 *
 * Exposes the 3D engine to Lua as claos.gui3d.*
 */

#include "gui3d_lua.h"
#include "lua.h"
#include "lauxlib.h"
#include "fixed.h"
#include "render.h"
#include "texture.h"
#include "collision.h"
#include "io.h"

/* Forward declarations for sprite functions */
extern int sprite_add(fixed_t x, fixed_t y, fixed_t z, uint16_t tex);
extern void sprite_remove(int id);
extern void sprite_move(int id, fixed_t x, fixed_t y, fixed_t z);

/* ─── Level Management ─── */

static int l_load_level(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    render_load_level(path);
    lua_pushboolean(L, r_state.level != NULL);
    return 1;
}

static int l_unload_level(lua_State* L) {
    (void)L;
    render_unload_level();
    return 0;
}

/* ─── Camera Control ─── */

static int l_set_camera(lua_State* L) {
    fixed_t x = (fixed_t)luaL_checkinteger(L, 1);
    fixed_t y = (fixed_t)luaL_checkinteger(L, 2);
    fixed_t z = (fixed_t)luaL_checkinteger(L, 3);
    int angle = (int)luaL_checkinteger(L, 4);
    render_set_camera(x, y, z, angle);
    return 0;
}

static int l_get_camera(lua_State* L) {
    lua_pushinteger(L, r_state.cam_x);
    lua_pushinteger(L, r_state.cam_y);
    lua_pushinteger(L, r_state.cam_z);
    lua_pushinteger(L, r_state.cam_angle);
    return 4;
}

/* ─── Rendering ─── */

static int l_set_viewport(lua_State* L) {
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    render_set_viewport(x, y, w, h);
    return 0;
}

static int l_render(lua_State* L) {
    (void)L;
    render_frame();
    return 0;
}

/* ─── Textures ─── */

static int l_load_texture(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    int idx = tex_load(path);
    lua_pushinteger(L, idx);
    return 1;
}

/* ─── Sprites ─── */

static int l_add_sprite(lua_State* L) {
    fixed_t x = (fixed_t)luaL_checkinteger(L, 1);
    fixed_t y = (fixed_t)luaL_checkinteger(L, 2);
    fixed_t z = (fixed_t)luaL_checkinteger(L, 3);
    uint16_t tex = (uint16_t)luaL_checkinteger(L, 4);
    int id = sprite_add(x, y, z, tex);
    lua_pushinteger(L, id);
    return 1;
}

static int l_remove_sprite(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    sprite_remove(id);
    return 0;
}

static int l_move_sprite(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    fixed_t x = (fixed_t)luaL_checkinteger(L, 2);
    fixed_t y = (fixed_t)luaL_checkinteger(L, 3);
    fixed_t z = (fixed_t)luaL_checkinteger(L, 4);
    sprite_move(id, x, y, z);
    return 0;
}

/* ─── Collision ─── */

static int l_move(lua_State* L) {
    fixed_t x = (fixed_t)luaL_checkinteger(L, 1);
    fixed_t y = (fixed_t)luaL_checkinteger(L, 2);
    fixed_t new_x = (fixed_t)luaL_checkinteger(L, 3);
    fixed_t new_y = (fixed_t)luaL_checkinteger(L, 4);
    fixed_t radius = (fixed_t)luaL_checkinteger(L, 5);
    fixed_t out_x, out_y;
    collision_move(x, y, new_x, new_y, radius, r_state.level, &out_x, &out_y);
    lua_pushinteger(L, out_x);
    lua_pushinteger(L, out_y);
    return 2;
}

static int l_line_of_sight(lua_State* L) {
    fixed_t x1 = (fixed_t)luaL_checkinteger(L, 1);
    fixed_t y1 = (fixed_t)luaL_checkinteger(L, 2);
    fixed_t x2 = (fixed_t)luaL_checkinteger(L, 3);
    fixed_t y2 = (fixed_t)luaL_checkinteger(L, 4);
    bool los = collision_line_of_sight(x1, y1, x2, y2, r_state.level);
    lua_pushboolean(L, los);
    return 1;
}

/* ─── Sector Info ─── */

static int l_floor_height(lua_State* L) {
    fixed_t x = (fixed_t)luaL_checkinteger(L, 1);
    fixed_t y = (fixed_t)luaL_checkinteger(L, 2);
    if (r_state.level) {
        uint16_t sec = collision_point_in_sector(x, y, r_state.level);
        if (sec < r_state.level->num_sectors) {
            lua_pushinteger(L, r_state.level->sectors[sec].floor_height);
            return 1;
        }
    }
    lua_pushinteger(L, 0);
    return 1;
}

static int l_ceiling_height(lua_State* L) {
    fixed_t x = (fixed_t)luaL_checkinteger(L, 1);
    fixed_t y = (fixed_t)luaL_checkinteger(L, 2);
    if (r_state.level) {
        uint16_t sec = collision_point_in_sector(x, y, r_state.level);
        if (sec < r_state.level->num_sectors) {
            lua_pushinteger(L, r_state.level->sectors[sec].ceiling_height);
            return 1;
        }
    }
    lua_pushinteger(L, 0);
    return 1;
}

static int l_set_sector_light(lua_State* L) {
    int sector_id = (int)luaL_checkinteger(L, 1);
    int level_val = (int)luaL_checkinteger(L, 2);
    if (r_state.level && sector_id >= 0 && sector_id < r_state.level->num_sectors) {
        r_state.level->sectors[sector_id].light_level = (uint8_t)(level_val & 0xFF);
    }
    return 0;
}

/* ─── Trig ─── */

static int l_sin(lua_State* L) {
    int angle = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, fp_sin(angle));
    return 1;
}

static int l_cos(lua_State* L) {
    int angle = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, fp_cos(angle));
    return 1;
}

/* ─── Stats ─── */

static int l_stats(lua_State* L) {
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, r_state.fps);
    lua_setfield(L, -2, "fps");
    lua_pushinteger(L, r_state.last_frame_ms);
    lua_setfield(L, -2, "frame_ms");
    lua_pushinteger(L, r_state.stat_walls);
    lua_setfield(L, -2, "walls_drawn");
    lua_pushinteger(L, r_state.stat_sprites);
    lua_setfield(L, -2, "sprites_drawn");
    return 1;
}

/* ─── Init ─── */

static int l_init(lua_State* L) {
    (void)L;
    render_init();
    return 0;
}

/* Function table */
static const luaL_Reg gui3d_funcs[] = {
    {"init",            l_init},
    {"load_level",      l_load_level},
    {"unload_level",    l_unload_level},
    {"set_camera",      l_set_camera},
    {"get_camera",      l_get_camera},
    {"set_viewport",    l_set_viewport},
    {"render",          l_render},
    {"load_texture",    l_load_texture},
    {"add_sprite",      l_add_sprite},
    {"remove_sprite",   l_remove_sprite},
    {"move_sprite",     l_move_sprite},
    {"move",            l_move},
    {"line_of_sight",   l_line_of_sight},
    {"floor_height",    l_floor_height},
    {"ceiling_height",  l_ceiling_height},
    {"set_sector_light",l_set_sector_light},
    {"sin",             l_sin},
    {"cos",             l_cos},
    {"stats",           l_stats},
    {NULL, NULL}
};

void gui3d_lua_open(lua_State* L) {
    luaL_newlibtable(L, gui3d_funcs);
    luaL_setfuncs(L, gui3d_funcs, 0);

    /* Constants */
    lua_pushinteger(L, FP_ONE);     lua_setfield(L, -2, "FP_ONE");
    lua_pushinteger(L, FP_HALF);    lua_setfield(L, -2, "FP_HALF");
    lua_pushinteger(L, ANGLE_360);  lua_setfield(L, -2, "ANGLE_360");
    lua_pushinteger(L, ANGLE_180);  lua_setfield(L, -2, "ANGLE_180");
    lua_pushinteger(L, ANGLE_90);   lua_setfield(L, -2, "ANGLE_90");
    lua_pushinteger(L, ANGLE_45);   lua_setfield(L, -2, "ANGLE_45");

    /* Table is left on the Lua stack for the caller to set as a field */
}
