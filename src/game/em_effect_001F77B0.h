/* em_effect_001F77B0.h - the effect node behaviour 001F77B0: a ring of
 * floor quads drawn through the decal kernel 001CE300. In the first level
 * the player's death spawns it: 0021D2E0's skeleton step (em_player_fall)
 * calls 001EFD90(0x80000043, point, the record's +0xB0), and the entity
 * table's record 0x43 (class 0xC, subtype +0x0D = 2) names this callback.
 * Docs: docs/DAMAGE.md section 4.
 *
 * Translated from the instructions (decomp NEARMISS src/func_001F77B0.c,
 * whose logic the instructions confirm; the float operation order below is
 * the instructions'). The node record is an effect-pool record
 * (em_effects_live's KIND_OTHER node): its +0x04 state, +0x0D subtype,
 * +0xB0 / +0xC0 quadwords and the work block w = +0x1F0 (w+0x00.. the
 * particle offsets, w+0x80.. their sizes, w+0xA0..+0xC3 the parameters).
 *
 *   state 0  by the subtype: 2 -> 4 particles, w+0xA8 0, w+0xB8 210.0,
 *            w+0xBC 7.0, w+0xC0 2.0, TEX0 0x2004108555322080, colour
 *            0x80020220; 3 -> 8, 4, 1.0, 7.0, 5.0, the same TEX0, colour
 *            0x80200220; 4 and 5 -> 8, 8, 1.0, 15.0, 10.0, the same TEX0
 *            and colour as 3; any other subtype sets nothing. Then per
 *            particle i < w+0xA4: 0x70003A20 = 2 pi (rand() / 2^31) - pi,
 *            0x70003A24 = rand() / 2^31, w+0x10 i = 0x70003A24 * (w+0xC0 *
 *            sin(0x70003A20)), w+0x10 i + 8 = 0x70003A24 * (w+0xC0 *
 *            cos(0x70003A20)), w+0x80 + 4 i = 0. Then w+0xA0 = rand(),
 *            +0x04 = 1, and on into state 1 in the same call.
 *   state 1  d = D_00810360..68 - +0xC0..+0xC8 (D_00810360 is the player
 *            record's +0xB0: after 0015BCF0's tail the bone-1 position).
 *            Per particle: four corners (x, y, z, 1.0) = (+0xB0 + w+0x10 i,
 *            +0xB4, +0xB8 + w+0x10 i + 8), with subtype 2 shifted by (d.x,
 *            d.y - 1.5, d.z), then by s = w+0xBC * w+0x80 + 4 i:
 *            corner 0 (-s, -s), 1 (+s, -s), 2 (-s, +s), 3 (+s, +s) on x and
 *            z; 001CE300(1, the corners, TEX0, colour); then w+0x80 + 4 i
 *            += 1.0 / w+0xB8, capped at 1.0.
 *   2, 3     001AFC10(self).
 *   other    nothing.
 *
 * Every COP1 operation goes through em_ee_float.h on bit patterns. The
 * workers are the original callees; a NULL or failing one faults (-1) and
 * the routine stops there (fail-stop). stdint only. */
#ifndef EM_EFFECT_001F77B0_H
#define EM_EFFECT_001F77B0_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_EFFECT_001F77B0_CALLBACK 0x001F77B0u
#define EM_EFFECT_001F77B0_WORK 0xC4u /* w+0x00..w+0xC3 */

typedef struct {
    void *ctx;
    /* 00122BB8(): the shared rand(). */
    int (*w_00122BB8)(void *ctx, int32_t *value);
    /* 0011E2A8 sinf / 0011DE90 cosf (the SDK math), bit patterns. */
    int (*w_0011E2A8)(void *ctx, uint32_t x, uint32_t *result);
    int (*w_0011DE90)(void *ctx, uint32_t x, uint32_t *result);
    /* 001CE300(tag, corners, tex0, colour): the decal kernel. */
    int (*w_001CE300)(void *ctx, int32_t tag, const uint32_t corners[16], uint64_t tex0, uint32_t colour);
    /* 001AFC10(self): the pool's free. */
    int (*w_001AFC10)(void *ctx);
    /* D_00810360 / D_00810364 / D_00810368. */
    int (*r_00810360)(void *ctx, uint32_t out[3]);
} EmEffect001F77B0Workers;

typedef struct {
    uint8_t *state;        /* +0x04 */
    uint8_t subtype;       /* +0x0D */
    const uint32_t *b0;    /* +0xB0..+0xBC (4 words) */
    const uint32_t *c0;    /* +0xC0..+0xCC (4 words) */
    uint8_t *work;         /* +0x1F0.. (EM_EFFECT_001F77B0_WORK bytes) */
    uint32_t *spad3A20;    /* 0x70003A20 */
    uint32_t *spad3A24;    /* 0x70003A24 */
} EmEffect001F77B0Node;

/* One behaviour call. 0, or -1 on a fault (*fault = the callee's address
 * or 0x001F77B0 for a missing view). */
int em_effect_001F77B0(const EmEffect001F77B0Node *node, const EmEffect001F77B0Workers *w, uint32_t *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_EFFECT_001F77B0_H */
