/*
 * CLAOS 3D Engine — Sprite System (stub)
 *
 * Sprite rendering will be implemented in M8.
 */

#include "fixed.h"
#include "bsp.h"
#include "render.h"

/* Sprite data */
typedef struct {
    fixed_t x, y, z;
    uint16_t texture;
    uint16_t flags;
    bool active;
} sprite_instance_t;

#define MAX_SPRITES 256

static sprite_instance_t sprites[MAX_SPRITES];
static int num_sprites = 0;

int sprite_add(fixed_t x, fixed_t y, fixed_t z, uint16_t tex) {
    for (int i = 0; i < MAX_SPRITES; i++) {
        if (!sprites[i].active) {
            sprites[i].x = x;
            sprites[i].y = y;
            sprites[i].z = z;
            sprites[i].texture = tex;
            sprites[i].flags = 0;
            sprites[i].active = true;
            if (i >= num_sprites) num_sprites = i + 1;
            return i;
        }
    }
    return -1;
}

void sprite_remove(int id) {
    if (id >= 0 && id < MAX_SPRITES) {
        sprites[id].active = false;
    }
}

void sprite_move(int id, fixed_t x, fixed_t y, fixed_t z) {
    if (id >= 0 && id < MAX_SPRITES && sprites[id].active) {
        sprites[id].x = x;
        sprites[id].y = y;
        sprites[id].z = z;
    }
}

void sprite_render_all(void) {
    /* Stub — full implementation in M8 */
    (void)sprites;
    (void)num_sprites;
}
