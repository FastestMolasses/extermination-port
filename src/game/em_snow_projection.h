#ifndef EM_SNOW_PROJECTION_H
#define EM_SNOW_PROJECTION_H

#include "game/em_snow_particles.h"

/* Original VU6E..7C, in DMA upload order. The last qword is integer GIF
 * tag data. Matrices use column-major layout and original GS conventions:
 * extent_projection is the render context's P (+0x2340), clip_from_world
 * its 001CD370(0) projection (+0x2240) and screen_from_world its K (+0x23C0),
 * which the runtimes take from em_rcl_frame_matrices (one owner, 001D2960). */
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

/* Translate 00233FC0..00234270 for one particle. Returns1 when submitted,
 * 0 when the original center clip test rejects it. The caller supplies
 * frame matrices; using a frozen reference matrix for live rendering is
 * incorrect. Raster output is still in GS coordinates, before XYOFFSET.
 */
int em_snow_project(const EmSnowProjection *projection,
                    const EmSnowParticle *particle, EmSnowProjected *out);

/* 00231770 sprite variant used by AREA11 owner008235F0: same GS
 * projection, without the snow program's extra near-camera attenuation. */
int em_effect_sprite_project(const EmSnowProjection *projection,
                             const EmSnowParticle *particle,
                             EmSnowProjected *out);

#endif
