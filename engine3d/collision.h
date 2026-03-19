/*
 * CLAOS 3D Engine — Collision Detection
 */

#ifndef CLAOS_COLLISION_H
#define CLAOS_COLLISION_H

#include "fixed.h"
#include "bsp.h"

/* Try to move from (x,y) to (new_x, new_y) with collision.
 * Returns adjusted position in out_x, out_y. */
void collision_move(fixed_t x, fixed_t y, fixed_t new_x, fixed_t new_y,
                    fixed_t radius, level_t* level,
                    fixed_t* out_x, fixed_t* out_y);

/* Find which sector a point is in (uses BSP tree walk) */
uint16_t collision_point_in_sector(fixed_t x, fixed_t y, level_t* level);

/* Check line of sight between two points */
bool collision_line_of_sight(fixed_t x1, fixed_t y1,
                             fixed_t x2, fixed_t y2, level_t* level);

#endif /* CLAOS_COLLISION_H */
