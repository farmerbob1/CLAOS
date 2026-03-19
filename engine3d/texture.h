/*
 * CLAOS 3D Engine — Texture System
 *
 * Manages wall/floor/ceiling/sprite textures. Textures must be
 * power-of-2 dimensions for fast bitmask wrapping.
 */

#ifndef CLAOS_TEXTURE_H
#define CLAOS_TEXTURE_H

#include "types.h"

typedef struct {
    uint32_t* pixels;   /* ARGB pixel data */
    int width;          /* must be power of 2 */
    int height;         /* must be power of 2 */
    int width_mask;     /* width - 1 */
    int height_mask;    /* height - 1 */
    int width_shift;    /* log2(width) for fast multiply */
} texture_t;

#define MAX_TEXTURES 256

extern texture_t textures[MAX_TEXTURES];
extern int num_textures;

/* Initialize texture system with procedural fallback textures */
void tex_init(void);

/* Load texture from ChaosFS (.ctx format). Returns texture index or -1. */
int tex_load(const char* path);

/* Create texture from raw pixel data. Returns texture index or -1. */
int tex_create(int width, int height, const uint32_t* pixels);

/* Get texture by index (NULL if invalid) */
texture_t* tex_get(int index);

#endif /* CLAOS_TEXTURE_H */
