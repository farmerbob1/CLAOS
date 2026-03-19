/*
 * CLAOS 3D Engine — Visplane System
 *
 * Handles floor and ceiling rendering via horizontal spans.
 */

#ifndef CLAOS_VISPLANE_H
#define CLAOS_VISPLANE_H

#include "fixed.h"
#include "render.h"

#define MAX_VISPLANES 128

typedef struct {
    fixed_t  height;
    uint16_t texture;
    uint8_t  light_level;
    uint8_t  pad;
    int      min_x, max_x;
    int16_t  top[MAX_RENDER_WIDTH];
    int16_t  bottom[MAX_RENDER_WIDTH];
    bool     used;
} visplane_t;

/* Reset visplane pool for new frame */
void visplane_clear(void);

/* Find or create a visplane matching these parameters */
visplane_t* visplane_find_or_create(fixed_t height, uint16_t tex, uint8_t light, int x);

/* Render all active visplanes */
void visplane_render_all(void);

#endif /* CLAOS_VISPLANE_H */
