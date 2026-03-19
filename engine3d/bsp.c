/*
 * CLAOS 3D Engine — BSP Tree Loader & Traversal
 */

#include "bsp.h"
#include "heap.h"
#include "string.h"
#include "chaosfs.h"
#include "io.h"

/* File read buffer — shared, not reentrant */
static char bsp_file_buf[131072]; /* 128KB max BSP file */

level_t* bsp_load(const char* path) {
    int len = chaosfs_read(path, bsp_file_buf, sizeof(bsp_file_buf));
    if (len < (int)sizeof(bsp_file_header_t)) {
        serial_print("[3D] BSP load failed: ");
        serial_print(path);
        serial_print("\n");
        return NULL;
    }

    bsp_file_header_t* hdr = (bsp_file_header_t*)bsp_file_buf;
    if (hdr->magic[0] != 'B' || hdr->magic[1] != 'S' ||
        hdr->magic[2] != 'P' || hdr->magic[3] != '!') {
        serial_print("[3D] Invalid BSP magic\n");
        return NULL;
    }

    level_t* level = (level_t*)kmalloc(sizeof(level_t));
    if (!level) return NULL;
    memset(level, 0, sizeof(level_t));

    level->num_vertices   = hdr->num_vertices;
    level->num_linedefs   = hdr->num_linedefs;
    level->num_sectors    = hdr->num_sectors;
    level->num_segs       = hdr->num_segs;
    level->num_subsectors = hdr->num_subsectors;
    level->num_nodes      = hdr->num_nodes;
    level->root_node      = hdr->root_node;

    /* Parse data sections — they follow the header in order */
    uint8_t* ptr = (uint8_t*)(bsp_file_buf + sizeof(bsp_file_header_t));

    /* Vertices */
    int vsize = level->num_vertices * sizeof(vertex_t);
    level->vertices = (vertex_t*)kmalloc(vsize);
    if (!level->vertices) goto fail;
    memcpy(level->vertices, ptr, vsize);
    ptr += vsize;

    /* Linedefs */
    int lsize = level->num_linedefs * sizeof(linedef_t);
    level->linedefs = (linedef_t*)kmalloc(lsize);
    if (!level->linedefs) goto fail;
    memcpy(level->linedefs, ptr, lsize);
    ptr += lsize;

    /* Sectors */
    int ssize = level->num_sectors * sizeof(sector_t);
    level->sectors = (sector_t*)kmalloc(ssize);
    if (!level->sectors) goto fail;
    memcpy(level->sectors, ptr, ssize);
    ptr += ssize;

    /* Segs */
    int sgsize = level->num_segs * sizeof(seg_t);
    level->segs = (seg_t*)kmalloc(sgsize);
    if (!level->segs) goto fail;
    memcpy(level->segs, ptr, sgsize);
    ptr += sgsize;

    /* Subsectors */
    int sssize = level->num_subsectors * sizeof(subsector_t);
    level->subsectors = (subsector_t*)kmalloc(sssize);
    if (!level->subsectors) goto fail;
    memcpy(level->subsectors, ptr, sssize);
    ptr += sssize;

    /* Nodes */
    int nsize = level->num_nodes * sizeof(bspnode_t);
    level->nodes = (bspnode_t*)kmalloc(nsize);
    if (!level->nodes) goto fail;
    memcpy(level->nodes, ptr, nsize);

    serial_print("[3D] BSP loaded: ");
    serial_print(path);
    serial_print("\n");

    return level;

fail:
    bsp_unload(level);
    return NULL;
}

void bsp_unload(level_t* level) {
    if (!level) return;
    if (level->vertices)   kfree(level->vertices);
    if (level->linedefs)   kfree(level->linedefs);
    if (level->sectors)    kfree(level->sectors);
    if (level->segs)       kfree(level->segs);
    if (level->subsectors) kfree(level->subsectors);
    if (level->nodes)      kfree(level->nodes);
    kfree(level);
}

/*
 * BSP traversal — front-to-back from camera position.
 * The "which side is the camera on" test uses the cross product.
 */
static void bsp_traverse_node(level_t* level, uint16_t node_idx,
                               fixed_t cam_x, fixed_t cam_y,
                               bsp_visit_fn visit, void* ctx) {
    /* Check if it's a subsector leaf */
    if (node_idx & BSP_SUBSECTOR_FLAG) {
        uint16_t ss_idx = node_idx & ~BSP_SUBSECTOR_FLAG;
        if (ss_idx < level->num_subsectors) {
            visit(&level->subsectors[ss_idx], ctx);
        }
        return;
    }

    if (node_idx >= level->num_nodes) return;

    bspnode_t* node = &level->nodes[node_idx];

    /* Which side of the partition line is the camera on?
     * Python builder uses: (cam_x - x)*dy - (cam_y - y)*dx > 0 for "right".
     * Match that convention here so front-to-back order is correct. */
    fixed64_t cross = (fixed64_t)(cam_x - node->x) * node->dy -
                      (fixed64_t)(cam_y - node->y) * node->dx;

    if (cross >= 0) {
        /* Camera is on the right side — visit right first (front-to-back) */
        bsp_traverse_node(level, node->child_right, cam_x, cam_y, visit, ctx);
        bsp_traverse_node(level, node->child_left,  cam_x, cam_y, visit, ctx);
    } else {
        /* Camera is on the left side */
        bsp_traverse_node(level, node->child_left,  cam_x, cam_y, visit, ctx);
        bsp_traverse_node(level, node->child_right, cam_x, cam_y, visit, ctx);
    }
}

void bsp_traverse(level_t* level, fixed_t cam_x, fixed_t cam_y,
                  bsp_visit_fn visit, void* ctx) {
    if (!level || !level->nodes || level->num_nodes == 0) {
        /* No BSP tree — just visit all subsectors in order */
        for (int i = 0; i < level->num_subsectors; i++) {
            visit(&level->subsectors[i], ctx);
        }
        return;
    }
    bsp_traverse_node(level, level->root_node, cam_x, cam_y, visit, ctx);
}
