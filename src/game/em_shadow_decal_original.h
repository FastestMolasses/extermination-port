/* em_shadow_decal_original.h - the drop-shadow decal's GS packet: 001CE300
 * with its frustum clipper 001CF470 (and the clipper's two leaves 001CF870 /
 * 001CF970) and the TEX0 packet 001CB950 (docs/SHADOW_DECAL.md).
 *
 * 001F8D30 (em_shadow_actor_route, the +0x214 != 0 blob shadow; also the
 * decal of 001F77B0, not on the first-level route) hands 001CE300 four world
 * corners, a TEX0 word, a colour and a blend mode. 001CE300 draws the quad
 * as two triangles, corners {0, 1, 2} and {1, 2, 3}:
 *
 *   001CE300(tag, quad, tex0, rgba)
 *       0x70003600..0x7000360C = the four colour bytes, widened to words.
 *       For each triangle: its corners go to the clipper's input records
 *       (D_008112C0, three 0x50-byte records: position, and the texture
 *       coordinate of the corner: corner 0 (0,0), 1 (0,1), 2 (1,0),
 *       3 (1,1)); 001CF470 clips them against the 001CD370(0) matrix
 *       (render context + 0x2240) into a fan at D_008117C0. A fan of n > 0
 *       vertices opens a packet of 3n + 2 quadwords in the display-list page
 *       D_007635C0, depth key 0 (001CB5F0), and fills it with a DIRECT VIF
 *       code and one PACKED GIF triangle fan (ST, RGBAQ, XYZF2 per vertex):
 *       each vertex goes through the camera rows 0x70003AC0 (x, y times
 *       1 / w; z times 1 / (w - 1); the fog lane from the render context
 *       +0xA0 quadword), and S, T and Q are the texture coordinate times the
 *       COP1 reciprocal of w.
 *       Then 001CB950(page, 0, tex0) appends the TEX0_1 A+D packet and
 *       001CB900(page, 0, tag) the blend-state reference of `tag`.
 *   001CF470(tri, m)
 *       Transforms the three input records by m (+0x40 of each record),
 *       then clips the polygon against |z| <= |w|, |y| <= |w| and
 *       |x| <= |w| in that order, ping-ponging between D_008112C0 and
 *       D_008117C0 (16 records each). Returns the fan's vertex count
 *       (0 when a pass leaves nothing); the fan ends in D_008117C0.
 *   001CF870(a, b, axis)
 *       The outcode of the edge a -> b for one axis: 1 / 2 when a's clip
 *       coordinate is above |w| / below -|w|, 0x10 / 0x20 for b.
 *   001CF970(out, a, b, axis, sign)
 *       The record where the edge crosses the plane coordinate = sign * w:
 *       t = |a - sign a.w| / (|a - sign a.w| + |b - sign b.w|) on that axis,
 *       and every quadword of the record (position, the two unused
 *       quadwords, the texture coordinate, the clip position) is
 *       a + (b - a) * |t|.
 *   001CB950(page, id, tex0)
 *       A 3-quadword packet: a DIRECT VIF code for 2 quadwords, a GIF A+D
 *       tag of one register, and tex0 -> register 0x06 (TEX0_1).
 *
 * All five are read from the original instructions (001CE300 and 001CB950
 * NEARMISS / nonmatching, 001CF470 / 001CF870 / 001CF970 hand-written asm):
 * see docs/SHADOW_DECAL.md for where the readable C is wrong. Reused, not
 * translated again: 0011DF78 (fabsf, em_sdk_math_original), the page
 * builders 001CB5F0 / 001CB900 (em_packet_chain_original, through the
 * workers), and the C runtime copy 00121870 (block_copy) is a copy of one
 * whole record (source and destination are whole records, never partly
 * overlapping).
 *
 * Memory. The clipper buffers are original RAM D_008112C0..D_00811CBF (32
 * records of 0x50 bytes; the render context follows at 0x00811CC0). The
 * module works on a caller-owned copy of those 0xA00 bytes (`stage`), so
 * the words the original leaves there (stale fields interpolated along,
 * the clip positions) are kept exactly. A record index outside the 32
 * records would read or write the render context in the original; the
 * translation faults (EM_SHADOW_DECAL_FAULT_RANGE) before such an access.
 *
 * Fail-stop: a missing worker, scratch word, stage or argument latches
 * EM_SHADOW_DECAL_FAULT_UNBOUND at the entry point's own address before any
 * write; a worker returning a negative value (FAULT_WORKER at the worker's
 * address), a VU0 form the float model refuses (FAULT_FLOAT) or a record
 * index outside the stage (FAULT_RANGE) stops at once and leaves the writes
 * made before it, in the original order. A latched fault refuses every later
 * call.
 *
 * Arithmetic: every COP1 and VU0 macro operation goes through em_ee_float.h
 * on raw bit patterns.
 *
 * Oracle: tools/test_shadow_decal_reference.py executes the original
 * instructions (and every callee as original code) over the captured route
 * RAM and compares every byte of RAM and scratchpad afterwards. */
#ifndef EM_SHADOW_DECAL_ORIGINAL_H
#define EM_SHADOW_DECAL_ORIGINAL_H

#include <stdint.h>

#include "game/em_packet_chain_original.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- original addresses and layout ---------------------------------------- */
#define EM_SHADOW_DECAL_PAGE          0x007635C0u /* D_007635C0: the display-list page */
#define EM_SHADOW_DECAL_STAGE_ADDRESS 0x008112C0u /* D_008112C0: clipper input buffer */
#define EM_SHADOW_DECAL_FAN_ADDRESS   0x008117C0u /* D_008117C0: clipper output fan */
#define EM_SHADOW_DECAL_RECORD_WORDS  20u         /* 0x50 bytes per record */
#define EM_SHADOW_DECAL_BUFFER_RECORDS 16u        /* records per buffer */
#define EM_SHADOW_DECAL_STAGE_RECORDS 32u         /* both buffers, D_008112C0..D_00811CBF */
#define EM_SHADOW_DECAL_STAGE_WORDS   (EM_SHADOW_DECAL_STAGE_RECORDS * EM_SHADOW_DECAL_RECORD_WORDS)
#define EM_SHADOW_DECAL_CLIP_OFFSET   0x2240u     /* 001CD370(a0) = D_00275670 + a0 * 0x40 + 0x2240 */

/* Record words (0x50-byte record, word index). */
#define EM_SHADOW_DECAL_REC_POS   0u   /* +0x00: x, y, z, w */
#define EM_SHADOW_DECAL_REC_S     12u  /* +0x30 */
#define EM_SHADOW_DECAL_REC_T     13u  /* +0x34 */
#define EM_SHADOW_DECAL_REC_CLIP  16u  /* +0x40: the clip-space position */

/* The TEX0 word 001F8D30 passes (built at 001F90B0..001F90C0); the decal
 * texture the renderer must provide (docs/SHADOW_DECAL.md section 4):
 * TBP0 0x2469, TBW 8, PSMT8, 16 x 16, TCC 1, TFX modulate, CBP 0x2148,
 * CPSM PSMCT32, CSM1, CSA 0, CLD 1. */
#define EM_SHADOW_DECAL_TEX0      UINT64_C(0x2004290511322469)
#define EM_SHADOW_DECAL_TEX0_TBP0 0x2469u
#define EM_SHADOW_DECAL_TEX0_CBP  0x2148u

/* ---- faults ----------------------------------------------------------------- */
enum {
    EM_SHADOW_DECAL_FAULT_NONE = 0,
    EM_SHADOW_DECAL_FAULT_UNBOUND = 1, /* a worker, scratch word, stage or argument is missing */
    EM_SHADOW_DECAL_FAULT_WORKER = 2,  /* a worker returned a negative value */
    EM_SHADOW_DECAL_FAULT_FLOAT = 3,   /* the EE float model refused a VU0 form */
    EM_SHADOW_DECAL_FAULT_RANGE = 4    /* a clipper record outside D_008112C0..D_00811CBF */
};

typedef struct EmShadowDecalFault {
    uint32_t address;  /* the original function, callee or instruction address */
    int32_t code;      /* EM_SHADOW_DECAL_FAULT_* */
} EmShadowDecalFault;

/* ---- workers -------------------------------------------------------------------
 * Each returns 0, or a negative value on a fault. The signatures of
 * w_001CB5F0 / w_001CB900 are em_packet_chain_w_001CB5F0 / _w_001CB900;
 * em_shadow_decal_bind_packet_chain fills all four for one EmPacketChain. */
typedef struct EmShadowDecalWorkers {
    void *ctx;
    /* 001CD370(a0) and the 16 words at the address it returns
     * (D_00275670 + a0 * 0x40 + 0x2240: the clip matrix 001CF470 loads). */
    int (*w_001CD370)(void *ctx, int32_t a0, uint32_t m[16]);
    /* 001CB5F0(table, id, count): *bytes = the count * 16 writable packet
     * bytes. */
    int (*w_001CB5F0)(void *ctx, uint32_t table, int32_t id, int32_t count, uint8_t **bytes);
    /* 001CB900(table, id, mode). */
    int (*w_001CB900)(void *ctx, uint32_t table, int32_t id, int32_t mode);
    /* The render context's +0xA0 quadword (fog: max, -, bias, scale), read
     * at 001CE50C through D_00275670. */
    int (*fog)(void *ctx, uint32_t out[4]);
} EmShadowDecalWorkers;

/* The scratchpad words, as raw words in the ONE scratchpad image the other
 * translations share. 0x70003600.. is also EmShadowActorRouteScratch.s3600
 * (001F8D30 writes P0 there just before) and EmEffectOriginalGlobals.spad3600. */
typedef struct EmShadowDecalScratch {
    uint32_t *s3600;        /* 0x70003600..0x7000360C (4 words): the widened colour */
    const uint32_t *s3AC0;  /* 0x70003AC0..0x70003AFC (16 words, read): the camera rows */
} EmShadowDecalScratch;

typedef struct EmShadowDecal {
    uint32_t *stage;                    /* EM_SHADOW_DECAL_STAGE_WORDS words: D_008112C0.. */
    EmShadowDecalScratch scratch;
    const EmShadowDecalWorkers *workers;
    EmShadowDecalFault fault;           /* the first fault; latched */
} EmShadowDecal;

/* Fill `w` with the em_packet_chain adapters over `pc` (ctx = pc):
 * w_001CB5F0 / w_001CB900 = em_packet_chain_w_001CB5F0 / _w_001CB900, fog =
 * em_packet_chain_fog, and w_001CD370 reads the 16 words at
 * pc->d275670 + a0 * 0x40 + 0x2240 through the chain's regions (it fails
 * when they are not mapped). */
void em_shadow_decal_bind_packet_chain(EmShadowDecalWorkers *w, EmPacketChain *pc);

/* ---- the translations ------------------------------------------------------- */

/* 001CE300(tag, quad, tex0, rgba). quad: the four corners (x, y, z, w words
 * each; w is carried along but never used). Returns 0, or -1 on a fault. */
int em_shadow_decal_001CE300(EmShadowDecal *d, int32_t tag, const uint32_t quad[16], uint64_t tex0,
                             uint32_t rgba);

/* The EmShadowActorRouteWorkers.submit signature; ctx is the EmShadowDecal. */
int em_shadow_decal_w_001CE300(void *ctx, int32_t tag, const uint32_t corners[16], uint64_t tex0,
                               uint32_t rgba);

/* 001CF470(D_008112C0, m): the input triangle is stage records 0..2 (both
 * callers pass D_008112C0). *count = the fan's vertex count (stage records
 * 16 .. 16 + count - 1). Returns 0, or -1 on a fault. */
int em_shadow_decal_001CF470(EmShadowDecal *d, const uint32_t m[16], int32_t *count);

/* 001CF870(a, b, axis): the edge outcode (0x00..0x33) of the records a and
 * b (20 words each) on lane `axis` (0..3; the clipper passes 2, 1, 0).
 * Returns -1 for an axis outside 0..3 (the original would read past the
 * clip quadword). */
int32_t em_shadow_decal_001CF870(const uint32_t a[20], const uint32_t b[20], int32_t axis);

/* 001CF970(out, a, b, axis, sign): the crossing record (20 words) of the
 * edge a -> b with the plane lane[axis] = sign * w; sign is the raw f12
 * word (+1.0 or -1.0 from the clipper). out may alias a or b (both are read
 * whole first). Returns 0, or -1 when axis < 0 (the original loops forever)
 * or the float model refuses a form (it never does for these forms). */
int em_shadow_decal_001CF970(uint32_t out[20], const uint32_t a[20], const uint32_t b[20], int32_t axis,
                             uint32_t sign);

/* 001CB950(table, id, tex0) through the w_001CB5F0 worker. Returns 0, or
 * -1 on a fault (latched in d). */
int em_shadow_decal_001CB950(EmShadowDecal *d, uint32_t table, int32_t id, uint64_t tex0);

#ifdef __cplusplus
}
#endif

#endif /* EM_SHADOW_DECAL_ORIGINAL_H */
