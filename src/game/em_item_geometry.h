/* Original untextured status-menu primitives. Coordinates retain GS fixed16. */
#ifndef EM_ITEM_GEOMETRY_H
#define EM_ITEM_GEOMETRY_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t rgba;
    uint16_t x, y;
} EmItemVertex;

/* 002082B0 descriptor: center, angle interval, two ellipse radii, then
 * start/end RGBA for each side. Return the original triangle-strip vertices.
 * The caller owns blend/order, including the original untextured /255 color
 * conversion. Bounded to the finite UI domain; no large-angle approximation.
 * Returns1 on success,0 for invalid input or insufficient output capacity. */
int em_item_geometry_arc(const float descriptor[24], EmItemVertex *vertices, size_t capacity,
                         size_t *count);

#endif
