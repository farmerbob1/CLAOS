/*
 * CLAOS 3D Engine — Renderer Public API
 *
 * BSP-based software 3D renderer. Renders directly into the
 * framebuffer backbuffer with a configurable viewport rectangle.
 * Works both fullscreen and inside desktop GUI windows.
 */

#ifndef CLAOS_RENDER_H
#define CLAOS_RENDER_H

#include "fixed.h"
#include "bsp.h"

#define MAX_RENDER_WIDTH  1024
#define MAX_RENDER_HEIGHT 768

/* Render state — global singleton */
typedef struct {
    /* Render target (framebuffer backbuffer) */
    uint32_t* target;
    int       target_pitch;    /* pixels per row */

    /* Viewport within the target */
    int vp_x, vp_y;
    int vp_w, vp_h;

    /* Per-column occlusion tracking */
    int16_t col_top[MAX_RENDER_WIDTH];   /* highest unfilled Y (starts at 0) */
    int16_t col_bot[MAX_RENDER_WIDTH];   /* lowest unfilled Y (starts at vp_h-1) */
    int     cols_filled;                 /* columns fully occluded */

    /* Camera */
    fixed_t cam_x, cam_y, cam_z;        /* position (z = eye height) */
    int     cam_angle;                   /* 0..4095 */

    /* Current level */
    level_t* level;

    /* Performance stats */
    uint32_t stat_walls;
    uint32_t stat_sprites;
    uint32_t stat_visplanes;
    uint32_t frame_start_tick;
    uint32_t last_frame_ms;
    uint32_t fps;
    uint32_t frame_count;
    uint32_t fps_timer;
} render_state_t;

extern render_state_t r_state;

/* Initialize the 3D renderer (call once at startup) */
void render_init(void);

/* Set the viewport rectangle for rendering */
void render_set_viewport(int x, int y, int w, int h);

/* Set camera position and angle */
void render_set_camera(fixed_t x, fixed_t y, fixed_t z, int angle);

/* Load a level for rendering */
void render_load_level(const char* path);

/* Unload the current level */
void render_unload_level(void);

/* Render one frame into the viewport */
void render_frame(void);

#endif /* CLAOS_RENDER_H */
