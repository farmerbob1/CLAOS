/*
 * CLAOS 3D Engine — Visplane System (stub)
 *
 * Floor/ceiling rendering will be implemented in M6.
 * For now, just provides empty stubs.
 */

#include "visplane.h"
#include "string.h"

static visplane_t visplanes[MAX_VISPLANES];
static int num_active_visplanes = 0;

void visplane_clear(void) {
    num_active_visplanes = 0;
    for (int i = 0; i < MAX_VISPLANES; i++) {
        visplanes[i].used = false;
    }
}

visplane_t* visplane_find_or_create(fixed_t height, uint16_t tex, uint8_t light, int x) {
    /* Find existing */
    for (int i = 0; i < num_active_visplanes; i++) {
        if (visplanes[i].used &&
            visplanes[i].height == height &&
            visplanes[i].texture == tex &&
            visplanes[i].light_level == light) {
            if (x < visplanes[i].min_x) visplanes[i].min_x = x;
            if (x > visplanes[i].max_x) visplanes[i].max_x = x;
            return &visplanes[i];
        }
    }

    /* Create new */
    if (num_active_visplanes >= MAX_VISPLANES) return NULL;

    visplane_t* vp = &visplanes[num_active_visplanes++];
    vp->used = true;
    vp->height = height;
    vp->texture = tex;
    vp->light_level = light;
    vp->min_x = x;
    vp->max_x = x;

    /* Initialize top/bottom to invalid (will be filled during wall render) */
    for (int i = 0; i < MAX_RENDER_WIDTH; i++) {
        vp->top[i] = -1;
        vp->bottom[i] = -1;
    }

    return vp;
}

void visplane_render_all(void) {
    /* Stub — full implementation in M6 */
    (void)visplanes;
}
