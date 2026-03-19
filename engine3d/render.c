/*
 * CLAOS 3D Engine — BSP Renderer
 *
 * Renders the 3D scene directly into the framebuffer backbuffer.
 * Column-based wall rendering with occlusion tracking.
 *
 * Performance critical: all per-column work avoids 64-bit division.
 * Divisions are done once per seg, then columns use multiply+shift.
 */

#include "render.h"
#include "light.h"
#include "texture.h"
#include "bsp.h"
#include "fb.h"
#include "timer.h"
#include "string.h"
#include "io.h"

render_state_t r_state;
static bool r_initialized = false;

/* ─── Initialization ─── */

void render_init(void) {
    if (r_initialized) return;
    memset(&r_state, 0, sizeof(render_state_t));
    light_init();
    tex_init();
    r_initialized = true;
    serial_print("[3D] Renderer initialized\n");
}

void render_set_viewport(int x, int y, int w, int h) {
    if (w > MAX_RENDER_WIDTH)  w = MAX_RENDER_WIDTH;
    if (h > MAX_RENDER_HEIGHT) h = MAX_RENDER_HEIGHT;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    r_state.vp_x = x;
    r_state.vp_y = y;
    r_state.vp_w = w;
    r_state.vp_h = h;

    r_state.target = fb_get_backbuffer();
    const fb_info_t* info = fb_get_info();
    r_state.target_pitch = info->width;
}

void render_set_camera(fixed_t x, fixed_t y, fixed_t z, int angle) {
    r_state.cam_x = x;
    r_state.cam_y = y;
    r_state.cam_z = z;
    r_state.cam_angle = angle & ANGLE_MASK;
}

void render_load_level(const char* path) {
    if (r_state.level) {
        bsp_unload(r_state.level);
        r_state.level = NULL;
    }
    r_state.level = bsp_load(path);
}

void render_unload_level(void) {
    if (r_state.level) {
        bsp_unload(r_state.level);
        r_state.level = NULL;
    }
}

/* ─── Pixel writing helpers ─── */

static inline void fill_column(int local_x, int y1, int y2, uint32_t color) {
    if (y1 > y2) return;
    int pitch = r_state.target_pitch;
    uint32_t* p = &r_state.target[(r_state.vp_y + y1) * pitch + r_state.vp_x + local_x];
    for (int y = y1; y <= y2; y++) {
        *p = color;
        p += pitch;
    }
}

/* ─── Wall Column Rendering ─── */

static void render_wall_column_textured(int col, int top, int bot,
                                         texture_t* tex, int tex_x_int,
                                         fixed_t tex_y_start, fixed_t tex_y_step,
                                         uint8_t light_level) {
    if (top > bot || !tex || !tex->pixels) return;
    int pitch = r_state.target_pitch;
    int tx = tex_x_int & tex->width_mask;
    uint32_t* dst = &r_state.target[(r_state.vp_y + top) * pitch + r_state.vp_x + col];
    fixed_t tex_y = tex_y_start;

    for (int y = top; y <= bot; y++) {
        int ty = FP_TO_INT(tex_y) & tex->height_mask;
        uint32_t texel = tex->pixels[ty * tex->width + tx];
        *dst = apply_light(texel, light_level);
        dst += pitch;
        tex_y += tex_y_step;
    }
}

/* ─── Seg Rendering ─── */

/*
 * Project a wall segment to screen coordinates and draw columns.
 *
 * Key optimization: compute wall top/bottom Y at the two seg endpoints,
 * then linearly interpolate per column using integer stepping.
 * This replaces 6 divisions per column with 0 divisions per column.
 */
static void render_seg(seg_t* seg) {
    level_t* level = r_state.level;
    if (!level) return;
    if (seg->linedef >= level->num_linedefs) return;

    linedef_t* line = &level->linedefs[seg->linedef];
    if (seg->v1 >= level->num_vertices || seg->v2 >= level->num_vertices) return;
    vertex_t* v1 = &level->vertices[seg->v1];
    vertex_t* v2 = &level->vertices[seg->v2];

    /* Transform vertices to camera space */
    fixed_t cam_sin = fp_sin(r_state.cam_angle);
    fixed_t cam_cos = fp_cos(r_state.cam_angle);

    fixed_t dx1 = v1->x - r_state.cam_x;
    fixed_t dy1 = v1->y - r_state.cam_y;
    fixed_t dx2 = v2->x - r_state.cam_x;
    fixed_t dy2 = v2->y - r_state.cam_y;

    /* Rotate into view space (x = right, y = forward/depth) */
    fixed_t vx1 = FP_MUL(dx1, cam_cos) + FP_MUL(dy1, cam_sin);
    fixed_t vy1 = -FP_MUL(dx1, cam_sin) + FP_MUL(dy1, cam_cos);
    fixed_t vx2 = FP_MUL(dx2, cam_cos) + FP_MUL(dy2, cam_sin);
    fixed_t vy2 = -FP_MUL(dx2, cam_sin) + FP_MUL(dy2, cam_cos);

    /* Near plane clipping — track clip fraction for texture coordinate adjustment */
    #define NEAR_PLANE (FP_ONE / 4)
    if (vy1 < NEAR_PLANE && vy2 < NEAR_PLANE) return;

    fixed_t clip_frac_v1 = 0;  /* 0 = v1 not clipped, >0 = fraction along seg where clipped */
    fixed_t clip_frac_v2 = 0;  /* same for v2 */

    if (vy1 < NEAR_PLANE) {
        fixed_t denom = vy2 - vy1;
        if (denom == 0) return;
        fixed_t t = FP_DIV(NEAR_PLANE - vy1, denom);
        vx1 = vx1 + FP_MUL(t, vx2 - vx1);
        vy1 = NEAR_PLANE;
        clip_frac_v1 = t;  /* v1 was clipped this far toward v2 */
    } else if (vy2 < NEAR_PLANE) {
        fixed_t denom = vy1 - vy2;
        if (denom == 0) return;
        fixed_t t = FP_DIV(NEAR_PLANE - vy2, denom);
        vx2 = vx2 + FP_MUL(t, vx1 - vx2);
        vy2 = NEAR_PLANE;
        clip_frac_v2 = t;  /* v2 was clipped this far toward v1 */
    }

    /* Perspective projection to screen X — 2 divisions total for the seg */
    int half_w = r_state.vp_w / 2;
    int half_h = r_state.vp_h / 2;
    if (half_w == 0 || half_h == 0) return;

    int sx1 = half_w + (int)((int64_t)vx1 * half_w / vy1);
    int sx2 = half_w + (int)((int64_t)vx2 * half_w / vy2);

    if (sx1 == sx2) return;

    /* Track which endpoint is left/right for texture mapping */
    int left_is_v1 = 1;  /* 1 = v1 is left endpoint, 0 = v2 is left */

    /* Ensure sx1 < sx2 */
    if (sx1 > sx2) {
        int tmp = sx1; sx1 = sx2; sx2 = tmp;
        fixed_t ft;
        ft = vy1; vy1 = vy2; vy2 = ft;
        left_is_v1 = 0;
    }
    if (sx2 <= 0 || sx1 >= r_state.vp_w) return;

    /* Get sector info */
    uint16_t front_sec_idx = (seg->side == 0) ? line->front_sector : line->back_sector;
    if (front_sec_idx >= level->num_sectors) return;
    sector_t* front_sec = &level->sectors[front_sec_idx];

    bool is_portal = (line->back_sector != 0xFFFF);
    sector_t* back_sec = NULL;
    if (is_portal) {
        uint16_t back_sec_idx = (seg->side == 0) ? line->back_sector : line->front_sector;
        if (back_sec_idx < level->num_sectors)
            back_sec = &level->sectors[back_sec_idx];
    }

    /* Heights relative to camera */
    fixed_t front_floor = front_sec->floor_height - r_state.cam_z;
    fixed_t front_ceil  = front_sec->ceiling_height - r_state.cam_z;

    /*
     * Perspective-correct interpolation using 1/z.
     *
     * 1/vy is linear in screen space, so we precompute it at both endpoints
     * (2 divisions per seg) and step it per column with just addition.
     * Then: wall_y = half_h - height * half_h * inv_vy  (multiply, no divide)
     *
     * We use 12.20 fixed-point for inv_vy to keep precision for far walls.
     */
    #define INV_SHIFT 20
    #define INV_ONE   (1 << INV_SHIFT)
    fixed_t inv_vy1 = (int32_t)((int64_t)INV_ONE * FP_ONE / vy1);
    fixed_t inv_vy2 = (int32_t)((int64_t)INV_ONE * FP_ONE / vy2);

    /* Precompute height constants: ceil_h and floor_h in screen-scale.
     * wall_y = half_h - (height >> FP_SHIFT) * half_h * inv_vy >> INV_SHIFT
     * Simplify: precompute (height * half_h) >> FP_SHIFT as a 32-bit value */
    int32_t ceil_factor  = (int32_t)((int64_t)front_ceil * half_h >> FP_SHIFT);
    int32_t floor_factor = (int32_t)((int64_t)front_floor * half_h >> FP_SHIFT);

    int32_t back_ceil_factor = 0, back_floor_factor = 0;
    if (is_portal && back_sec) {
        fixed_t back_floor_h = back_sec->floor_height - r_state.cam_z;
        fixed_t back_ceil_h  = back_sec->ceiling_height - r_state.cam_z;
        back_ceil_factor  = (int32_t)((int64_t)back_ceil_h * half_h >> FP_SHIFT);
        back_floor_factor = (int32_t)((int64_t)back_floor_h * half_h >> FP_SHIFT);
    }

    /* Get texture */
    uint16_t mid_tex_idx = (seg->side == 0) ? line->front_mid_tex : line->back_mid_tex;
    texture_t* mid_tex = tex_get(mid_tex_idx);
    uint8_t light = front_sec->light_level;

    /* Compute seg length in world units for texture X mapping */
    fixed_t seg_adx = FP_ABS(v2->x - v1->x);
    fixed_t seg_ady = FP_ABS(v2->y - v1->y);
    fixed_t seg_len = (seg_adx > seg_ady) ? seg_adx + (seg_ady >> 1) : seg_ady + (seg_adx >> 1);
    if (seg_len < FP_ONE) seg_len = FP_ONE;

    /* Texture U at v1 and v2 endpoints, adjusted for near-plane clipping */
    fixed_t tex_u_start = seg->offset + line->tex_offset_x;
    fixed_t tex_u_end   = seg->offset + seg_len + line->tex_offset_x;

    /* If near-plane clipped, interpolate texture coordinate to clip point */
    if (clip_frac_v1 > 0) {
        tex_u_start = tex_u_start + FP_MUL(clip_frac_v1, tex_u_end - tex_u_start);
    }
    if (clip_frac_v2 > 0) {
        tex_u_end = tex_u_end + FP_MUL(clip_frac_v2, tex_u_start - tex_u_end);
    }

    int32_t tex_u_at_v1 = FP_TO_INT(tex_u_start);
    int32_t tex_u_at_v2 = FP_TO_INT(tex_u_end);

    /* Assign to left/right screen endpoints (respecting the swap) */
    int32_t tex_u_left  = left_is_v1 ? tex_u_at_v1 : tex_u_at_v2;
    int32_t tex_u_right = left_is_v1 ? tex_u_at_v2 : tex_u_at_v1;

    /* Perspective-correct texture X: interpolate (tex_u * inv_vy) linearly.
     * tex_u is in integer world units, inv_vy is 12.20. Product fits int32. */
    int32_t tu_over_z1 = tex_u_left  * (inv_vy1 >> 8);  /* scale inv_vy down to avoid overflow */
    int32_t tu_over_z2 = tex_u_right * (inv_vy2 >> 8);

    /* Column range */
    int x_start = sx1 < 0 ? 0 : sx1;
    int x_end = sx2 >= r_state.vp_w ? r_state.vp_w - 1 : sx2 - 1;
    int seg_width = sx2 - sx1;
    if (seg_width <= 0) return;

    /* Per-column exact interpolation (no stepping drift) */
    int32_t inv_vy_range = inv_vy2 - inv_vy1;
    int32_t tu_over_z_range = tu_over_z2 - tu_over_z1;

    for (int x = x_start; x <= x_end; x++) {
        /* Skip fully occluded columns */
        if (r_state.col_top[x] >= r_state.col_bot[x]) continue;

        /* Exact 1/z interpolation for this column */
        int col_offset = x - sx1;
        int32_t inv_vy_cur = inv_vy1 + (int32_t)(((int64_t)inv_vy_range * col_offset + seg_width / 2) / seg_width);

        /* Perspective-correct texture X: interpolate tex_u/z, then recover tex_u */
        int32_t tu_over_z_cur = tu_over_z1 + (int32_t)((int64_t)tu_over_z_range * col_offset / seg_width);
        int32_t inv_vy_scaled = inv_vy_cur >> 8;  /* same scale as tu_over_z */
        int tex_x_world = (inv_vy_scaled > 0) ? (tu_over_z_cur / inv_vy_scaled) : 0;

        /* wall_y with 8 extra bits of sub-pixel precision (24.8 format).
         * This prevents texture jitter when wall_top jumps by integer pixels. */
        int32_t wall_top_256 = (half_h << 8) - (int32_t)((int64_t)ceil_factor * inv_vy_cur >> (INV_SHIFT - 8));
        int32_t wall_bot_256 = (half_h << 8) - (int32_t)((int64_t)floor_factor * inv_vy_cur >> (INV_SHIFT - 8));
        int wall_top = wall_top_256 >> 8;
        int wall_bot = wall_bot_256 >> 8;

        int ct = r_state.col_top[x];
        int cb = r_state.col_bot[x];

        /* Same-height portals are invisible — skip entirely.
         * The back sector's solid walls will handle everything. */
        if (is_portal && back_sec &&
            front_sec->floor_height == back_sec->floor_height &&
            front_sec->ceiling_height == back_sec->ceiling_height) {
            continue;
        }

        /* Draw ceiling */
        if (wall_top > ct) {
            fill_column(x, ct, wall_top - 1, apply_light(0xFF3344AA, 200));
        }

        /* Draw floor */
        if (wall_bot < cb) {
            fill_column(x, wall_bot + 1, cb, apply_light(0xFF555555, 160));
        }

        /* Clamp wall to occlusion bounds */
        int draw_top = wall_top < ct ? ct : wall_top;
        int draw_bot = wall_bot > cb ? cb : wall_bot;

        if (draw_top <= draw_bot) {
            if (is_portal && back_sec) {
                int portal_top = half_h - (int)((int64_t)back_ceil_factor * inv_vy_cur >> INV_SHIFT);
                int portal_bot = half_h - (int)((int64_t)back_floor_factor * inv_vy_cur >> INV_SHIFT);

                /* Upper wall (from front ceiling to back ceiling) */
                if (portal_top > draw_top) {
                    int ut = draw_top;
                    int ub = portal_top - 1;
                    if (ub > draw_bot) ub = draw_bot;
                    if (ut <= ub) {
                        uint16_t tex_idx = (seg->side == 0) ? line->front_upper_tex : line->back_upper_tex;
                        texture_t* tex = tex_get(tex_idx);
                        int full_h = portal_top - wall_top;
                        if (full_h < 1) full_h = 1;
                        fixed_t step = INT_TO_FP(tex->height) / full_h;
                        fixed_t tstart = (ut > wall_top) ? step * (ut - wall_top) : 0;
                        render_wall_column_textured(x, ut, ub, tex, tex_x_world & tex->width_mask, tstart, step, light);
                    }
                }

                /* Lower wall (from back floor to front floor) */
                if (portal_bot < draw_bot) {
                    int lt = portal_bot + 1;
                    int lb = draw_bot;
                    if (lt < draw_top) lt = draw_top;
                    if (lt <= lb) {
                        uint16_t tex_idx = (seg->side == 0) ? line->front_lower_tex : line->back_lower_tex;
                        texture_t* tex = tex_get(tex_idx);
                        int full_h = wall_bot - portal_bot;
                        if (full_h < 1) full_h = 1;
                        fixed_t step = INT_TO_FP(tex->height) / full_h;
                        fixed_t tstart = (lt > portal_bot + 1) ? step * (lt - portal_bot - 1) : 0;
                        render_wall_column_textured(x, lt, lb, tex, tex_x_world & tex->width_mask, tstart, step, light);
                    }
                }

                /* Portal occlusion: narrow to the portal opening, not front sector */
                int new_top = (portal_top > wall_top) ? portal_top : wall_top;
                int new_bot = (portal_bot < wall_bot) ? portal_bot : wall_bot;
                if (new_top > ct) r_state.col_top[x] = (int16_t)new_top;
                if (new_bot < cb) r_state.col_bot[x] = (int16_t)new_bot;
            } else {
                /* Solid wall — sub-pixel precise texture mapping.
                 * Use 24.8 wall extents for smooth texture anchoring. */
                int32_t full_h_256 = wall_bot_256 - wall_top_256;
                if (full_h_256 < 256) full_h_256 = 256;
                fixed_t tex_y_step = (int32_t)((int64_t)INT_TO_FP(mid_tex->height) * 256 / full_h_256);
                /* Sub-pixel clip offset */
                int32_t clip_256 = (draw_top << 8) - wall_top_256;
                fixed_t tex_y_start = (clip_256 > 0) ? (int32_t)((int64_t)tex_y_step * clip_256 >> 8) : 0;
                int tex_x = tex_x_world & mid_tex->width_mask;
                render_wall_column_textured(x, draw_top, draw_bot, mid_tex, tex_x, tex_y_start, tex_y_step, light);

                /* Solid wall fully occludes */
                r_state.col_top[x] = r_state.col_bot[x];
                r_state.cols_filled++;
            }
        }

        r_state.stat_walls++;
    }
}

/* ─── BSP Traversal Callback ─── */

static void visit_subsector(subsector_t* ss, void* ctx) {
    (void)ctx;
    level_t* level = r_state.level;
    if (!level) return;

    /* Early exit if all columns filled */
    if (r_state.cols_filled >= r_state.vp_w) return;

    for (int i = 0; i < ss->num_segs; i++) {
        uint16_t seg_idx = ss->first_seg + i;
        if (seg_idx < level->num_segs) {
            render_seg(&level->segs[seg_idx]);
        }
        if (r_state.cols_filled >= r_state.vp_w) return;
    }
}

/* ─── Frame Rendering ─── */

void render_frame(void) {
    if (!r_state.target) return;

    r_state.frame_start_tick = timer_get_ticks();
    r_state.stat_walls = 0;
    r_state.stat_sprites = 0;

    /* Reset occlusion */
    for (int i = 0; i < r_state.vp_w; i++) {
        r_state.col_top[i] = 0;
        r_state.col_bot[i] = (int16_t)(r_state.vp_h - 1);
    }
    r_state.cols_filled = 0;

    if (r_state.level) {
        /* BSP traversal renders walls */
        bsp_traverse(r_state.level, r_state.cam_x, r_state.cam_y,
                     visit_subsector, NULL);

        /* Fill any remaining undrawn ceiling/floor columns */
        uint32_t sky = apply_light(0xFF3344AA, 200);
        uint32_t floor_c = apply_light(0xFF555555, 160);
        int mid = r_state.vp_h / 2;
        for (int x = 0; x < r_state.vp_w; x++) {
            if (r_state.col_top[x] < r_state.col_bot[x]) {
                if (r_state.col_top[x] < mid)
                    fill_column(x, r_state.col_top[x], mid - 1, sky);
                if (r_state.col_bot[x] >= mid)
                    fill_column(x, mid, r_state.col_bot[x], floor_c);
            }
        }
    } else {
        /* No level loaded — draw sky/floor gradient */
        int mid = r_state.vp_h / 2;
        uint32_t sky = apply_light(0xFF3344AA, 200);
        uint32_t floor_c = apply_light(0xFF555555, 160);
        for (int x = 0; x < r_state.vp_w; x++) {
            if (mid > 0)
                fill_column(x, 0, mid - 1, sky);
            fill_column(x, mid, r_state.vp_h - 1, floor_c);
        }
    }

    /* FPS calculation */
    uint32_t end_tick = timer_get_ticks();
    r_state.last_frame_ms = (end_tick - r_state.frame_start_tick) * 10;
    r_state.frame_count++;

    uint32_t elapsed = (end_tick - r_state.fps_timer) * 10;
    if (elapsed >= 1000) {
        r_state.fps = (r_state.frame_count * 1000) / (elapsed > 0 ? elapsed : 1);
        r_state.frame_count = 0;
        r_state.fps_timer = end_tick;
    }
}
