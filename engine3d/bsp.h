/*
 * CLAOS 3D Engine — BSP Data Structures
 *
 * Binary Space Partition tree for Doom-style 2.5D rendering.
 * All walls vertical, floors/ceilings horizontal at defined heights.
 */

#ifndef CLAOS_BSP_H
#define CLAOS_BSP_H

#include "fixed.h"

/* A vertex in 2D map space */
typedef struct {
    fixed_t x, y;
} vertex_t;

/* Wall flags */
#define LINE_FLAG_IMPASSABLE   0x0001
#define LINE_FLAG_TRANSPARENT  0x0002

/* A line segment (wall) between two vertices */
typedef struct {
    uint16_t v1, v2;           /* vertex indices */
    uint16_t front_sector;     /* sector on front side */
    uint16_t back_sector;      /* sector on back side (0xFFFF = solid wall) */
    uint16_t front_upper_tex;
    uint16_t front_mid_tex;
    uint16_t front_lower_tex;
    uint16_t back_upper_tex;
    uint16_t back_mid_tex;
    uint16_t back_lower_tex;
    fixed_t  tex_offset_x;     /* texture scroll offset */
    fixed_t  tex_offset_y;
    uint16_t flags;
    uint16_t pad;              /* alignment */
} linedef_t;

/* A convex region with a floor and ceiling height */
typedef struct {
    fixed_t  floor_height;
    fixed_t  ceiling_height;
    uint16_t floor_tex;
    uint16_t ceiling_tex;
    uint8_t  light_level;      /* 0-255 */
    uint8_t  pad[3];
    uint16_t flags;
    uint16_t pad2;
} sector_t;

/* A segment of a linedef, used in the BSP tree */
typedef struct {
    uint16_t v1, v2;           /* vertex indices */
    uint16_t linedef;          /* linedef this seg belongs to */
    uint16_t side;             /* 0 = front, 1 = back */
    fixed_t  offset;           /* texture offset along line */
} seg_t;

/* A convex subspace containing segs */
typedef struct {
    uint16_t first_seg;
    uint16_t num_segs;
    uint16_t sector;
    uint16_t pad;
} subsector_t;

/* A BSP node — splits space with a partition line */
typedef struct {
    fixed_t  x, y;            /* partition line start */
    fixed_t  dx, dy;          /* partition line direction */
    int16_t  bbox_right[4];   /* bounding box: top, bottom, left, right */
    int16_t  bbox_left[4];
    uint16_t child_right;     /* index into nodes or subsectors */
    uint16_t child_left;      /* high bit set = subsector index */
} bspnode_t;

#define BSP_SUBSECTOR_FLAG 0x8000

/* Level data container */
typedef struct {
    vertex_t*     vertices;     uint16_t num_vertices;
    linedef_t*    linedefs;     uint16_t num_linedefs;
    sector_t*     sectors;      uint16_t num_sectors;
    seg_t*        segs;         uint16_t num_segs;
    subsector_t*  subsectors;   uint16_t num_subsectors;
    bspnode_t*    nodes;        uint16_t num_nodes;
    uint16_t      root_node;
    uint16_t      pad;
} level_t;

/* BSP file format header */
typedef struct {
    char     magic[4];         /* "BSP!" */
    uint16_t num_vertices;
    uint16_t num_linedefs;
    uint16_t num_sectors;
    uint16_t num_segs;
    uint16_t num_subsectors;
    uint16_t num_nodes;
    uint16_t root_node;
    uint16_t reserved;
} bsp_file_header_t;

/* Load a BSP level from ChaosFS. Returns NULL on failure. */
level_t* bsp_load(const char* path);

/* Free a loaded level */
void bsp_unload(level_t* level);

/* Traverse BSP tree front-to-back from camera position.
 * Calls visit_subsector for each visible subsector. */
typedef void (*bsp_visit_fn)(subsector_t* ss, void* ctx);
void bsp_traverse(level_t* level, fixed_t cam_x, fixed_t cam_y,
                  bsp_visit_fn visit, void* ctx);

#endif /* CLAOS_BSP_H */
