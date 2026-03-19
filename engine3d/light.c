/*
 * CLAOS 3D Engine — Lighting
 *
 * Initialize the light lookup table using integer math only.
 */

#include "light.h"

uint8_t light_lut[LIGHT_LEVELS][256];

void light_init(void) {
    for (int level = 0; level < LIGHT_LEVELS; level++) {
        for (int c = 0; c < 256; c++) {
            light_lut[level][c] = (uint8_t)((c * level) / 31);
        }
    }
}
