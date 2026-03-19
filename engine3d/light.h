/*
 * CLAOS 3D Engine — Lighting
 *
 * Pre-computed light lookup table for fast per-pixel dimming.
 * 32 light levels x 256 color values = 8KB LUT.
 */

#ifndef CLAOS_LIGHT_H
#define CLAOS_LIGHT_H

#include "types.h"

#define LIGHT_LEVELS 32

extern uint8_t light_lut[LIGHT_LEVELS][256];

void light_init(void);

/* Apply lighting to a color. light = 0..255 (mapped to 32 LUT levels) */
static inline uint32_t apply_light(uint32_t color, uint8_t light) {
    uint8_t level = light >> 3;  /* 256 levels -> 32 LUT entries */
    uint8_t r = light_lut[level][(color >> 16) & 0xFF];
    uint8_t g = light_lut[level][(color >> 8) & 0xFF];
    uint8_t b = light_lut[level][color & 0xFF];
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

#endif /* CLAOS_LIGHT_H */
