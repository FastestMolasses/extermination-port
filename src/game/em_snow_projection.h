#ifndef EM_SNOW_PROJECTION_H
#define EM_SNOW_PROJECTION_H

#include "game/em_snow_particles.h"

/* Original VU6E..7C, in DMA upload order. The last qword is integer GIF
 * tag data. Matrices use column-major layout and original GS conventions. */
typedef struct EmSnowProjection {
    float extent_projection[16];
    float clip_from_world[16];
    float screen_from_world[16];
    float fog[4];
    float depth_bias[4];
    uint32_t gif_tag[4];
} EmSnowProjection;

typedef struct EmSnowProjected {
    /* Original FTOI4 qwords, in GIF order: plus extent, then minus extent.
     * PACKED XYZF2 takes X/Y low16, Z bits4..27, and F bits4..11. */
    uint32_t xyzf[2][4];
    uint32_t color[4];
    float st[2][2];             /* (0,0) at plus; (1,1) at minus. */
    float clip[4];              /* Original symmetric clip coordinates. */
} EmSnowProjected;

/* Rebuild the three frame matrices from original +Z-forward, Y-down view
 * coordinates and current camera zoom. Retains fog, depth_bias and gif_tag.
 * A native Y-up/-Z-forward view converts by negating rows1 and2. Scalar
 * construction uses EE rounded divisions and truncated products; matrix
 * composition uses VU truncation.
 */
void em_snow_projection_matrices(EmSnowProjection *projection,
                                const float original_view[16], float zoom);

/* Translate 00233FC0..00234270 for one particle. Returns1 when submitted,
 * 0 when the original center clip test rejects it. The caller supplies
 * frame matrices; using a frozen reference matrix for live rendering is
 * incorrect. Raster output is still in GS coordinates, before XYOFFSET.
 */
int em_snow_project(const EmSnowProjection *projection,
                    const EmSnowParticle *particle, EmSnowProjected *out);

#endif
