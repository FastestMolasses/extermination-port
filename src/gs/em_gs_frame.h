/* em_gs_frame - the binding helpers of the CPU GS model (docs/GS_EXACT.md
 * section 9): replay the primitives the port's GS walkers record
 * (EmGfxGsPrim / EmGfxGsEnv, src/em_gfx.h) into em_gs_raster, and read a
 * PSMCT32 field back as RGBA8 rows for the platform layer.
 *
 * Nothing here decides how a 512x224 field is presented (line doubling,
 * field placement, combining): that is a pending user decision
 * (docs/LAUNCHER_OPTIONS.md, "Field presentation"). */
#ifndef EM_GS_FRAME_H
#define EM_GS_FRAME_H

#include <stdint.h>

#include "em_gfx.h"
#include "gs/em_gs_raster.h"

/* Register values the replay last wrote, so a state register is written
 * again only when it changes (a TEX0 write loads the CLUT when its CLD
 * says so, exactly as the GS does on every TEX0 write). */
typedef struct {
    uint64_t tex0, clamp, tex1, alpha, test, colclamp;
    uint64_t frame, zbuf, xyoffset, scissor, prmodecont, dthe, fba, pabe, texa, scanmsk, fogcol;
    uint32_t written;          /* bit per register above: written at least once */
} EmGsReplay;

void em_gs_replay_init(EmGsReplay *r);
/* Sets FOGCOL (the frame's fog colour, which the recorded page never holds). */
void em_gs_replay_fogcol(EmGs *gs, EmGsReplay *r, uint64_t fogcol);
/* Replays `count` primitives in order: the environment and state each one
 * recorded, then PRIM and its vertex registers (RGBAQ with Q, ST, UV, then
 * XYZF2 when the vertex carried F, XYZ2 otherwise). envs may be NULL.
 * Returns the number of primitives the model refused (0 when all drew). */
uint32_t em_gs_replay_prims(EmGs *gs, EmGsReplay *r, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs,
                            uint32_t count);
/* Ends the current span (em_gs_flush), then copies a PSMCT32 buffer (base
 * page fbp, width fbw * 64) of w x h pixels into rgba (w * h * 4 bytes, rows
 * top to bottom, bytes R, G, B, A as the GS stores them; A is the stored
 * alpha, not forced opaque). */
void em_gs_read_frame_rgba(EmGs *gs, uint32_t fbp, uint32_t fbw, uint32_t w, uint32_t h, uint8_t *rgba);

#endif
