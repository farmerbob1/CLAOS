/*
 * CLAOS 3D Engine — Collision Detection
 *
 * Simple 2D collision against linedefs with wall sliding.
 * Uses axis-separated movement for robustness.
 */

#include "collision.h"
#include "string.h"

/* Check if a line segment (x1,y1)-(x2,y2) intersects circle at (cx,cy) with radius r */
static bool line_circle_intersect(fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2,
                                   fixed_t cx, fixed_t cy, fixed_t radius) {
    /* Vector from v1 to v2 */
    fixed_t dx = x2 - x1;
    fixed_t dy = y2 - y1;

    /* Vector from v1 to circle center */
    fixed_t fx = cx - x1;
    fixed_t fy = cy - y1;

    /* Segment length squared */
    fixed64_t len_sq = (fixed64_t)dx * dx + (fixed64_t)dy * dy;
    if (len_sq == 0) return false;

    /* Parameter t of closest point on line to circle center */
    fixed64_t dot = (fixed64_t)fx * dx + (fixed64_t)fy * dy;

    /* Clamp t to [0, len_sq] (we compare in scaled space) */
    if (dot < 0) dot = 0;
    if (dot > len_sq) dot = len_sq;

    /* Closest point */
    fixed_t closest_x = x1 + (fixed_t)((dot * dx) / len_sq);
    fixed_t closest_y = y1 + (fixed_t)((dot * dy) / len_sq);

    /* Distance from closest point to circle center */
    fixed_t dist_x = cx - closest_x;
    fixed_t dist_y = cy - closest_y;
    fixed64_t dist_sq = (fixed64_t)dist_x * dist_x + (fixed64_t)dist_y * dist_y;
    fixed64_t r_sq = (fixed64_t)radius * radius;

    return dist_sq < r_sq;
}

void collision_move(fixed_t x, fixed_t y, fixed_t new_x, fixed_t new_y,
                    fixed_t radius, level_t* level,
                    fixed_t* out_x, fixed_t* out_y) {
    if (!level || !level->linedefs || !level->vertices) {
        *out_x = new_x;
        *out_y = new_y;
        return;
    }

    /* Try X movement first, then Y (axis-separated) */
    fixed_t try_x = new_x;
    fixed_t try_y = y;  /* only X changed */

    /* Check X movement against all solid linedefs */
    for (int i = 0; i < level->num_linedefs; i++) {
        linedef_t* ld = &level->linedefs[i];

        /* Only collide with solid walls (one-sided) */
        if (ld->back_sector != 0xFFFF) continue;

        vertex_t* v1 = &level->vertices[ld->v1];
        vertex_t* v2 = &level->vertices[ld->v2];

        if (line_circle_intersect(v1->x, v1->y, v2->x, v2->y, try_x, try_y, radius)) {
            try_x = x;  /* Block X movement */
            break;
        }
    }

    /* Now try Y movement from the resulting X position */
    try_y = new_y;

    for (int i = 0; i < level->num_linedefs; i++) {
        linedef_t* ld = &level->linedefs[i];
        if (ld->back_sector != 0xFFFF) continue;

        vertex_t* v1 = &level->vertices[ld->v1];
        vertex_t* v2 = &level->vertices[ld->v2];

        if (line_circle_intersect(v1->x, v1->y, v2->x, v2->y, try_x, try_y, radius)) {
            try_y = y;  /* Block Y movement */
            break;
        }
    }

    *out_x = try_x;
    *out_y = try_y;
}

uint16_t collision_point_in_sector(fixed_t x, fixed_t y, level_t* level) {
    if (!level || !level->nodes || level->num_nodes == 0) {
        return 0;
    }

    /* Walk the BSP tree to find the subsector containing the point */
    uint16_t node_idx = level->root_node;

    while (!(node_idx & BSP_SUBSECTOR_FLAG)) {
        if (node_idx >= level->num_nodes) return 0;

        bspnode_t* node = &level->nodes[node_idx];

        /* Which side of the partition line? */
        fixed64_t cross = (fixed64_t)node->dx * (y - node->y) -
                          (fixed64_t)node->dy * (x - node->x);

        if (cross >= 0) {
            node_idx = node->child_right;
        } else {
            node_idx = node->child_left;
        }
    }

    uint16_t ss_idx = node_idx & ~BSP_SUBSECTOR_FLAG;
    if (ss_idx < level->num_subsectors) {
        return level->subsectors[ss_idx].sector;
    }
    return 0;
}

bool collision_line_of_sight(fixed_t x1, fixed_t y1,
                             fixed_t x2, fixed_t y2, level_t* level) {
    if (!level) return true;

    /* Check against all solid linedefs */
    for (int i = 0; i < level->num_linedefs; i++) {
        linedef_t* ld = &level->linedefs[i];
        if (ld->back_sector != 0xFFFF) continue;  /* Skip two-sided */

        vertex_t* v1 = &level->vertices[ld->v1];
        vertex_t* v2 = &level->vertices[ld->v2];

        /* Line-line intersection test */
        fixed64_t d1x = (fixed64_t)(x2 - x1);
        fixed64_t d1y = (fixed64_t)(y2 - y1);
        fixed64_t d2x = (fixed64_t)(v2->x - v1->x);
        fixed64_t d2y = (fixed64_t)(v2->y - v1->y);

        fixed64_t denom = d1x * d2y - d1y * d2x;
        if (denom == 0) continue;  /* Parallel */

        fixed64_t t_num = (fixed64_t)(v1->x - x1) * d2y - (fixed64_t)(v1->y - y1) * d2x;
        fixed64_t u_num = (fixed64_t)(v1->x - x1) * d1y - (fixed64_t)(v1->y - y1) * d1x;

        /* Both t and u must be in [0, 1] for intersection */
        if (denom > 0) {
            if (t_num < 0 || t_num > denom || u_num < 0 || u_num > denom) continue;
        } else {
            if (t_num > 0 || t_num < denom || u_num > 0 || u_num < denom) continue;
        }

        return false;  /* Intersection found — no line of sight */
    }

    return true;
}
