/*
 * CLAOS 3D Engine — Texture System
 */

#include "texture.h"
#include "heap.h"
#include "string.h"
#include "chaosfs.h"
#include "io.h"

texture_t textures[MAX_TEXTURES];
int num_textures = 0;

/* CTX file format header */
struct ctx_header {
    char magic[4];      /* "CTX!" */
    uint16_t width;
    uint16_t height;
    /* followed by width * height * 4 bytes of ARGB pixel data */
};

static int log2i(int v) {
    int r = 0;
    while (v > 1) { v >>= 1; r++; }
    return r;
}

/* Generate a procedural checkerboard texture */
static int tex_create_checkerboard(int size, uint32_t c1, uint32_t c2) {
    if (num_textures >= MAX_TEXTURES) return -1;

    int idx = num_textures;
    texture_t* t = &textures[idx];
    t->width = size;
    t->height = size;
    t->width_mask = size - 1;
    t->height_mask = size - 1;
    t->width_shift = log2i(size);

    t->pixels = (uint32_t*)kmalloc(size * size * 4);
    if (!t->pixels) return -1;

    int check = size / 8;
    if (check < 1) check = 1;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            t->pixels[y * size + x] = ((x / check) + (y / check)) & 1 ? c1 : c2;
        }
    }

    num_textures++;
    return idx;
}

/* Generate a procedural brick texture */
static int tex_create_brick(int size) {
    if (num_textures >= MAX_TEXTURES) return -1;

    int idx = num_textures;
    texture_t* t = &textures[idx];
    t->width = size;
    t->height = size;
    t->width_mask = size - 1;
    t->height_mask = size - 1;
    t->width_shift = log2i(size);

    t->pixels = (uint32_t*)kmalloc(size * size * 4);
    if (!t->pixels) return -1;

    uint32_t mortar = 0xFF696969;  /* dim grey */

    int bh = size / 4;  /* brick height */
    int bw = size / 2;  /* brick width */
    if (bh < 2) bh = 2;
    if (bw < 2) bw = 2;

    for (int y = 0; y < size; y++) {
        int row = y / bh;
        int ymod = y % bh;
        int offset = (row & 1) ? bw / 2 : 0;

        for (int x = 0; x < size; x++) {
            int xmod = (x + offset) % bw;

            /* Mortar lines */
            if (ymod == 0 || xmod == 0) {
                t->pixels[y * size + x] = mortar;
            } else {
                /* Slight color variation */
                uint32_t variation = ((x * 7 + y * 13) & 0x0F);
                uint8_t r = 0x8B + variation;
                uint8_t g = 0x45 + (variation >> 1);
                uint8_t b = 0x13 + (variation >> 2);
                t->pixels[y * size + x] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }

    num_textures++;
    return idx;
}

/* Generate a stone floor texture */
static int tex_create_stone(int size) {
    if (num_textures >= MAX_TEXTURES) return -1;

    int idx = num_textures;
    texture_t* t = &textures[idx];
    t->width = size;
    t->height = size;
    t->width_mask = size - 1;
    t->height_mask = size - 1;
    t->width_shift = log2i(size);

    t->pixels = (uint32_t*)kmalloc(size * size * 4);
    if (!t->pixels) return -1;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            /* Simple hash for pseudo-random stone look */
            uint32_t hash = (x * 2654435761u + y * 40503u) >> 16;
            uint8_t base = 80 + (hash & 0x1F);
            t->pixels[y * size + x] = 0xFF000000 | ((uint32_t)base << 16) | ((uint32_t)base << 8) | base;
        }
    }

    num_textures++;
    return idx;
}

void tex_init(void) {
    num_textures = 0;

    /* Texture 0: default checkerboard (pink/black — unmapped texture indicator) */
    tex_create_checkerboard(64, 0xFFFF00FF, 0xFF000000);

    /* Texture 1: brick wall */
    tex_create_brick(64);

    /* Texture 2: stone */
    tex_create_stone(64);

    /* Texture 3: dark checkerboard (floor) */
    tex_create_checkerboard(64, 0xFF404040, 0xFF505050);

    /* Texture 4: light checkerboard (ceiling) */
    tex_create_checkerboard(64, 0xFF606060, 0xFF707070);

    serial_print("[3D] Textures initialized\n");
}

int tex_load(const char* path) {
    if (num_textures >= MAX_TEXTURES) return -1;

    /* Read file from ChaosFS */
    static char file_buf[131072]; /* 128KB max texture file */
    int len = chaosfs_read(path, file_buf, sizeof(file_buf));
    if (len < (int)sizeof(struct ctx_header)) return -1;

    struct ctx_header* hdr = (struct ctx_header*)file_buf;
    if (hdr->magic[0] != 'C' || hdr->magic[1] != 'T' ||
        hdr->magic[2] != 'X' || hdr->magic[3] != '!') {
        serial_print("[3D] Invalid CTX file: ");
        serial_print(path);
        serial_print("\n");
        return -1;
    }

    int w = hdr->width;
    int h = hdr->height;
    int expected_size = (int)sizeof(struct ctx_header) + w * h * 4;
    if (len < expected_size) return -1;

    /* Check power of 2 */
    if ((w & (w - 1)) != 0 || (h & (h - 1)) != 0) return -1;

    int idx = num_textures;
    texture_t* t = &textures[idx];
    t->width = w;
    t->height = h;
    t->width_mask = w - 1;
    t->height_mask = h - 1;
    t->width_shift = log2i(w);

    t->pixels = (uint32_t*)kmalloc(w * h * 4);
    if (!t->pixels) return -1;

    memcpy(t->pixels, file_buf + sizeof(struct ctx_header), w * h * 4);

    num_textures++;
    serial_print("[3D] Loaded texture: ");
    serial_print(path);
    serial_print("\n");
    return idx;
}

int tex_create(int width, int height, const uint32_t* pixels) {
    if (num_textures >= MAX_TEXTURES) return -1;
    if ((width & (width - 1)) != 0 || (height & (height - 1)) != 0) return -1;

    int idx = num_textures;
    texture_t* t = &textures[idx];
    t->width = width;
    t->height = height;
    t->width_mask = width - 1;
    t->height_mask = height - 1;
    t->width_shift = log2i(width);

    t->pixels = (uint32_t*)kmalloc(width * height * 4);
    if (!t->pixels) return -1;

    memcpy(t->pixels, pixels, width * height * 4);

    num_textures++;
    return idx;
}

texture_t* tex_get(int index) {
    if (index < 0 || index >= num_textures) return &textures[0]; /* fallback */
    return &textures[index];
}
