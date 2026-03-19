# CLAOS Phase 9: BSP Software 3D Renderer
## Engine Specification

---

## Overview

Add a Doom-style BSP (Binary Space Partition) software 3D rendering engine to CLAOS. The engine is written in C, compiled into the kernel, and exposed to Lua via `claos.gui3d.*` bindings. All rendering is pure software — no GPU acceleration, no shortcuts. Full screen resolution (whatever the user has set via VBE), fixed-point math, textured walls/floors/ceilings, sprites, and lighting.

**Design philosophy:** Honest rendering. Full resolution, every pixel calculated, no adaptive scaling. What you see is what the CPU can push.

---

## Architecture

```
┌─────────────────────────────────────────┐
│  Lua Game Logic                          │
│  Player movement, AI, triggers, HUD      │
├─────────────────────────────────────────┤
│  claos.gui3d.* Lua Bindings              │
│  Camera, map, textures, sprites, render  │
├─────────────────────────────────────────┤
│  BSP Engine (C)                          │
│  ├─ BSP Tree Traversal                   │
│  ├─ Wall Renderer (vertical spans)       │
│  ├─ Floor/Ceiling Renderer (visplanes)   │
│  ├─ Sprite Renderer (z-sorted billboards)│
│  ├─ Texture Mapper                       │
│  ├─ Lighting Engine                      │
│  └─ Fixed-Point Math Library             │
├─────────────────────────────────────────┤
│  Framebuffer (existing from Phase 8)     │
│  Back buffer → fb_swap() → screen        │
└─────────────────────────────────────────┘
```

---

## Fixed-Point Math Library (`engine3d/fixed.h`)

All 3D math uses 16.16 fixed-point. No floating point in the renderer hot path.

```c
typedef int32_t fixed_t;
typedef int64_t fixed64_t;  // for intermediate multiply results

#define FP_SHIFT      16
#define FP_ONE        (1 << FP_SHIFT)         // 1.0 = 65536
#define FP_HALF       (1 << (FP_SHIFT - 1))   // 0.5 = 32768

#define INT_TO_FP(x)  ((fixed_t)(x) << FP_SHIFT)
#define FP_TO_INT(x)  ((x) >> FP_SHIFT)
#define FLOAT_TO_FP(x) ((fixed_t)((x) * FP_ONE))  // compile-time only

#define FP_MUL(a, b)  ((fixed_t)(((fixed64_t)(a) * (b)) >> FP_SHIFT))
#define FP_DIV(a, b)  ((fixed_t)(((fixed64_t)(a) << FP_SHIFT) / (b)))

// Trig lookup tables (4096 entries = 360 degrees)
#define ANGLE_360     4096
#define ANGLE_180     2048
#define ANGLE_90      1024
#define ANGLE_MASK    4095

extern fixed_t sin_table[ANGLE_360];
extern fixed_t cos_table[ANGLE_360];
extern fixed_t tan_table[ANGLE_360];

void fixed_init_tables(void);  // call once at startup

static inline fixed_t fp_sin(int angle) { return sin_table[angle & ANGLE_MASK]; }
static inline fixed_t fp_cos(int angle) { return cos_table[angle & ANGLE_MASK]; }
```

---

## BSP Data Structures (`engine3d/bsp.h`)

### Map Geometry

The BSP engine works with 2D map geometry (like Doom — all walls are vertical, floors/ceilings are horizontal at defined heights).

```c
// A vertex in 2D map space
typedef struct {
    fixed_t x, y;
} vertex_t;

// A line segment (wall) between two vertices
typedef struct {
    uint16_t v1, v2;         // vertex indices
    uint16_t front_sector;   // sector on front side
    uint16_t back_sector;    // sector on back side (0xFFFF = solid wall)
    uint16_t front_upper_tex;
    uint16_t front_mid_tex;
    uint16_t front_lower_tex;
    uint16_t back_upper_tex;
    uint16_t back_mid_tex;
    uint16_t back_lower_tex;
    fixed_t  tex_offset_x;   // texture scroll offset
    fixed_t  tex_offset_y;
    uint16_t flags;           // transparent, impassable, etc.
} linedef_t;

// A convex region with a floor and ceiling height
typedef struct {
    fixed_t floor_height;
    fixed_t ceiling_height;
    uint16_t floor_tex;
    uint16_t ceiling_tex;
    uint8_t  light_level;     // 0-255
    uint16_t flags;           // damage, secret, etc.
} sector_t;

// A segment of a linedef, used in the BSP tree
typedef struct {
    uint16_t v1, v2;
    uint16_t linedef;
    uint16_t side;           // 0 = front, 1 = back
    fixed_t  offset;         // texture offset along line
} seg_t;

// A convex subspace containing segs
typedef struct {
    uint16_t first_seg;
    uint16_t num_segs;
    uint16_t sector;
} subsector_t;

// A BSP node — splits space with a partition line
typedef struct {
    fixed_t  x, y;           // partition line start
    fixed_t  dx, dy;         // partition line direction
    int16_t  bbox_right[4];  // bounding box: top, bottom, left, right
    int16_t  bbox_left[4];
    uint16_t child_right;    // index into nodes or subsectors
    uint16_t child_left;     // high bit set = subsector
} bspnode_t;
```

### Level Data Container

```c
typedef struct {
    vertex_t*     vertices;     uint16_t num_vertices;
    linedef_t*    linedefs;     uint16_t num_linedefs;
    sector_t*     sectors;      uint16_t num_sectors;
    seg_t*        segs;         uint16_t num_segs;
    subsector_t*  subsectors;   uint16_t num_subsectors;
    bspnode_t*    nodes;        uint16_t num_nodes;
    uint16_t      root_node;
} level_t;
```

---

## BSP Builder (`tools/bspbuild.py` — runs on host)

Building BSP trees at runtime is slow. Pre-compute them on the host.

- Takes a simple text/JSON map format as input
- Outputs a binary `.bsp` file that CLAOS loads from ChaosFS
- Recursively partitions the map using seg-aligned splitting planes
- Generates nodes, subsectors, and reordered segs
- Also pre-computes reject table (sector-to-sector visibility LUT) for fast culling

```bash
python tools/bspbuild.py maps/e1m1.map -o maps/e1m1.bsp
python tools/mkchaosfs.py claos.img --add /games/doom/maps/e1m1.bsp maps/e1m1.bsp
```

---

## BSP Renderer (`engine3d/render.c`)

### Core Rendering Loop

Per frame:
1. Clear the frame (or rely on full overdraw)
2. Traverse BSP tree front-to-back from camera position
3. For each subsector reached:
   a. For each seg in the subsector:
      - Project seg endpoints to screen X coordinates
      - Clip to screen bounds and already-drawn columns
      - Draw wall vertical spans (textured)
      - Record floor/ceiling visplanes
   b. Mark screen columns as filled
4. Render floor/ceiling visplanes (horizontal spans)
5. Render sprites (sorted back-to-front using BSP position)
6. Render HUD overlay (via Lua callback)
7. Blit render buffer to framebuffer

### Column Rendering (Walls)

The key insight: walls are always vertical on screen. For each visible wall segment, compute the screen X range it covers, then for each column X:

```c
void render_wall_column(int screen_x, int top_y, int bottom_y,
                        texture_t* tex, fixed_t tex_x,
                        fixed_t tex_y_start, fixed_t tex_y_step,
                        uint8_t light_level) {
    uint32_t* dst = &backbuffer[screen_x + top_y * screen_width];
    fixed_t tex_y = tex_y_start;
    
    for (int y = top_y; y <= bottom_y; y++) {
        int ty = FP_TO_INT(tex_y) & (tex->height - 1);  // wrap
        uint32_t texel = tex->pixels[FP_TO_INT(tex_x) + ty * tex->width];
        *dst = apply_light(texel, light_level);
        dst += screen_width;
        tex_y += tex_y_step;
    }
}
```

### Visplane Rendering (Floors/Ceilings)

Floors and ceilings are rendered as horizontal spans. During wall rendering, record the top/bottom Y for each X column where floor/ceiling is visible. Then render horizontal spans:

```c
typedef struct {
    fixed_t height;
    uint16_t texture;
    uint8_t light_level;
    int min_x, max_x;
    int top[MAX_SCREEN_WIDTH];    // top Y for each column
    int bottom[MAX_SCREEN_WIDTH]; // bottom Y for each column
} visplane_t;
```

For each visplane, walk columns left to right. When Y values form a continuous horizontal run, render that span with perspective-correct texture mapping.

### Sprite Rendering

Sprites (enemies, items, decorations) are billboards — always face the camera.

1. During BSP traversal, collect all sprites in visible subsectors
2. Transform sprite positions to camera space
3. Sort by distance (back to front)
4. For each sprite:
   - Project to screen coordinates
   - Clip against already-rendered wall columns (sprites behind walls are hidden)
   - Draw column by column with transparency (color key or alpha)

```c
typedef struct {
    fixed_t x, y, z;        // world position (z = floor offset)
    uint16_t texture;
    uint16_t flags;          // mirrored, fullbright, etc.
} sprite_t;
```

---

## Texture System (`engine3d/texture.c`)

```c
typedef struct {
    uint32_t* pixels;   // ARGB pixel data
    int width;          // must be power of 2 (64, 128, 256)
    int height;         // must be power of 2
    int width_mask;     // width - 1 (for fast wrapping)
    int height_mask;    // height - 1
} texture_t;

#define MAX_TEXTURES 256

texture_t textures[MAX_TEXTURES];
int num_textures;

// Load texture from ChaosFS (raw ARGB or simple TGA format)
int tex_load(const char* path);

// Load texture from raw pixel data (for Lua-generated textures)
int tex_create(int width, int height, const uint32_t* pixels);
```

Texture format on disk: simple header + raw ARGB pixels.

```c
// .ctx file format (CLAOS TeXture)
struct ctx_header {
    char magic[4];      // "CTX!"
    uint16_t width;
    uint16_t height;
    // followed by width * height * 4 bytes of ARGB pixel data
};
```

A Python tool on the host converts PNG/BMP to .ctx:
```bash
python tools/mkctx.py wall_brick.png -o wall_brick.ctx
```

---

## Lighting (`engine3d/light.c`)

Distance-based lighting with per-sector ambient light level.

```c
// Pre-compute 32 light levels (0 = dark, 31 = full bright)
// Each level is a 256-entry lookup table that dims a color channel
uint8_t light_lut[32][256];

void light_init(void) {
    for (int level = 0; level < 32; level++) {
        float factor = (float)level / 31.0f;
        for (int c = 0; c < 256; c++) {
            light_lut[level][c] = (uint8_t)(c * factor);
        }
    }
}

static inline uint32_t apply_light(uint32_t color, uint8_t light) {
    uint8_t level = light >> 3;  // 256 levels → 32 LUT entries
    uint8_t r = light_lut[level][(color >> 16) & 0xFF];
    uint8_t g = light_lut[level][(color >> 8) & 0xFF];
    uint8_t b = light_lut[level][color & 0xFF];
    return (r << 16) | (g << 8) | b;
}
```

Effects:
- Distance dimming: reduce light by distance from camera (lookup, no division)
- Sector ambient: each sector has a base light level
- Fullbright: certain textures/sprites ignore lighting (e.g., fire, screens)
- Optional: flickering lights (Lua toggles sector light per frame)

---

## Collision Detection (`engine3d/collision.c`)

Simple 2D collision against linedefs:

```c
// Try to move from (x,y) to (new_x, new_y)
// Returns adjusted position that doesn't cross solid walls
// Uses sliding along walls for smooth movement
void move_with_collision(fixed_t* x, fixed_t* y,
                         fixed_t new_x, fixed_t new_y,
                         fixed_t radius, level_t* level);

// Check if a point is inside a valid sector
uint16_t point_in_sector(fixed_t x, fixed_t y, level_t* level);

// Line of sight check between two points
int has_line_of_sight(fixed_t x1, fixed_t y1,
                      fixed_t x2, fixed_t y2, level_t* level);
```

Wall sliding: when the player hits a wall, project their movement vector onto the wall line and slide along it. This is what makes Doom movement feel smooth instead of just stopping dead.

---

## Lua 3D API (`lua/gui3d_lib.c`)

```lua
-- Level management
claos.gui3d.load_level("/games/doom/maps/e1m1.bsp")
claos.gui3d.unload_level()

-- Camera control
claos.gui3d.set_camera(x, y, z, angle)  -- z = eye height
claos.gui3d.get_camera()                 -- returns x, y, z, angle

-- Rendering
claos.gui3d.render()             -- render scene to internal buffer
claos.gui3d.blit(x, y, w, h)    -- copy to framebuffer region (full screen: 0,0,sw,sh)
claos.gui3d.render_to_screen()   -- shortcut: render + blit fullscreen

-- Textures
claos.gui3d.load_texture("/games/doom/textures/wall_brick.ctx")

-- Sprites
claos.gui3d.add_sprite(x, y, z, texture_id)
claos.gui3d.remove_sprite(id)
claos.gui3d.move_sprite(id, x, y, z)

-- Collision
claos.gui3d.move(x, y, dx, dy, radius)  -- returns new_x, new_y (with collision)
claos.gui3d.line_of_sight(x1, y1, x2, y2)  -- returns true/false

-- Sector info
claos.gui3d.floor_height(x, y)
claos.gui3d.ceiling_height(x, y)
claos.gui3d.set_sector_light(sector_id, level)

-- Performance stats
claos.gui3d.stats()  -- returns {fps, frame_ms, walls_drawn, sprites_drawn}
```

---

## Optimisation Techniques

**Must implement (critical for performance):**

1. **Fixed-point everything** — no floating point in the render loop
2. **Trig lookup tables** — 4096-entry sin/cos/tan, computed once at startup
3. **Column-based wall rendering** — one texture lookup per pixel, sequential memory writes
4. **Power-of-2 textures** — bitwise AND for wrapping instead of modulo
5. **BSP front-to-back traversal** — never draw a pixel that will be overdrawn
6. **Screen column occlusion** — track which columns are fully drawn, skip remaining segs
7. **Light LUT** — pre-computed multiply tables, no per-pixel multiply
8. **Sector reject table** — skip segs in sectors that can't possibly be visible

**Should implement (significant improvement):**

9. **Horizontal span merging** — for floor/ceiling, merge adjacent pixels into memset-like spans
10. **Sprite clipping against wall columns** — skip sprite pixels behind walls
11. **Subsector early-out** — if all columns in a subsector's screen range are filled, skip it
12. **Memory alignment** — align backbuffer rows to cache line boundaries

**Nice to have (diminishing returns):**

13. **Assembly inner loops** — hand-write the column renderer in x86 assembly using `rep stosd` and unrolled loops
14. **Mipmap textures** — smaller textures for distant walls (reduces cache misses)
15. **PVS (Potentially Visible Set)** — pre-compute which subsectors can see each other

---

## File Structure

```
engine3d/
├── fixed.h              # Fixed-point math types and macros
├── fixed.c              # Trig table initialization
├── bsp.h                # BSP data structures
├── bsp.c                # BSP tree traversal
├── render.c             # Main renderer (walls, floors, ceilings)
├── render.h             # Renderer public API
├── sprite.c             # Sprite sorting and rendering
├── texture.c            # Texture loading and management
├── texture.h            # Texture types
├── light.c              # Lighting LUT and application
├── collision.c          # 2D collision detection
└── gui3d_lua.c          # Lua API bindings

tools/
├── bspbuild.py          # BSP tree compiler (host-side)
└── mkctx.py             # PNG/BMP → .ctx texture converter
```

---

## Milestones

**M1 — Fixed-point math + trig tables work:**
Verify sin/cos/tan tables are accurate. Test FP_MUL/FP_DIV.

**M2 — Wireframe BSP rendering:**
Load a BSP level, traverse tree, project walls to screen columns, draw as coloured vertical lines. No textures yet.

**M3 — Solid-coloured walls:**
Fill wall columns with solid colours. Proper occlusion — closer walls hide farther ones.

**M4 — Textured walls:**
Load .ctx textures, apply perspective-correct texture mapping to wall columns.

**M5 — Floor and ceiling rendering:**
Visplane system working. Textured floors and ceilings.

**M6 — Lighting:**
Distance-based dimming + sector ambient light levels.

**M7 — Sprites:**
Billboard sprites rendered with z-sorting and wall clipping.

**M8 — Collision:**
Player can walk through the level with wall sliding. Can't walk through solid walls.

**M9 — Full integration:**
Lua game loop running, handling input, moving player, rendering each frame. FPS counter displayed.

---

## Build Integration

Add to Makefile:
```makefile
ENGINE3D_SRC = engine3d/fixed.c engine3d/bsp.c engine3d/render.c \
               engine3d/sprite.c engine3d/texture.c engine3d/light.c \
               engine3d/collision.c engine3d/gui3d_lua.c

KERNEL_OBJS += $(ENGINE3D_SRC:.c=.o)
```

The 3D engine compiles into the kernel alongside the 2D renderer. They share the same back buffer. A game can mix 2D (HUD, menus) and 3D (world) rendering freely.
