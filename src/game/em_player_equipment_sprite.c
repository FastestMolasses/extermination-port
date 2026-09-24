/* em_player_equipment_sprite.c - 001CD520 (see em_player_equipment_sprite.h,
 * docs/PLAYER_EQUIPMENT.md section 5).
 *
 * Read from the split listing of 001CD520 (0x001CD520..0x001CD93C). The
 * addresses in the comments are the instructions each step translates.
 * VU0 registers are modelled as 4-lane raw-bit arrays (index 0 = x). */
#include "game/em_player_equipment_sprite.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

#define ONE UINT32_C(0x3F800000)
#define HALF UINT32_C(0x3F000000)

static int fail(const EmPlayerEquipmentSprite *s, uint32_t address, int32_t code)
{
    if (s && s->fault && s->fault->code == EM_PLAYER_EQUIPMENT_FAULT_NONE) {
        s->fault->address = address;
        s->fault->code = code;
    }
    return -1;
}

#define CALL(addr, expr) do { if ((expr) < 0) return fail(s, (addr), EM_PLAYER_EQUIPMENT_FAULT_WORKER_FAILED); } while (0)
#define VU(addr, expr) do { if ((expr) != EM_EE_FLOAT_OK) return fail(s, (addr), EM_PLAYER_EQUIPMENT_FAULT_UNMEASURED); } while (0)

static const uint32_t VF0[4] = {0, 0, 0, ONE};

/* acc = row0 * v.x + row1 * v.y + row2 * v.z; out = acc + row3 * vf0.w
 * (the four-instruction transform at 001CD584, 001CD5F8 and 001CD6A0). */
static int transform(const EmPlayerEquipmentSprite *s, uint32_t at, const uint32_t m[16], const uint32_t v[4],
                     uint32_t acc[4], uint32_t out[4])
{
    VU(at, em_vu_vec_bits(EM_VU_MULABC, 15, 0, m + 0, v, 0, NULL, acc));
    VU(at, em_vu_vec_bits(EM_VU_MADDABC, 15, 1, m + 4, v, 0, acc, acc));
    VU(at, em_vu_vec_bits(EM_VU_MADDABC, 15, 2, m + 8, v, 0, acc, acc));
    VU(at, em_vu_vec_bits(EM_VU_MADDBC, 15, 3, m + 12, VF0, 0, acc, out));
    return 0;
}

/* The clip test of v.xyz against |v.w| (the vclipw at 001CD594): bits 0/1
 * for x above +|w| / below -|w|, 2/3 for y, 4/5 for z. Not in the measured
 * float model: DAZ'd finite operands only, compared exactly on their bit
 * patterns (sign-magnitude order). */
static int64_t key(uint32_t b)
{
    uint32_t e = (b >> 23) & 0xFFu;
    uint32_t mag = e == 0 ? 0 : b & UINT32_C(0x7FFFFFFF);
    return (b & UINT32_C(0x80000000)) ? -(int64_t)mag : (int64_t)mag;
}

static int clip_flags(const EmPlayerEquipmentSprite *s, const uint32_t v[4], unsigned *flags)
{
    for (unsigned k = 0; k < 4; k++)
        if (((v[k] >> 23) & 0xFFu) == 0xFFu)
            return fail(s, 0x001CD594u, EM_PLAYER_EQUIPMENT_FAULT_UNMEASURED);
    int64_t w = key(v[3]);
    if (w < 0) w = -w;
    unsigned f = 0;
    for (unsigned k = 0; k < 3; k++) {
        int64_t c = key(v[k]);
        if (c > w) f |= 1u << (2 * k);
        if (c < -w) f |= 2u << (2 * k);
    }
    *flags = f;
    return 0;
}

static void put32(uint8_t *p, unsigned at, uint32_t v) { memcpy(p + at, &v, 4); }

int em_player_equipment_001CD520(const EmPlayerEquipmentSprite *s, int32_t bucket, int32_t mode,
                                 const uint32_t point[4], uint64_t giftag, uint32_t w, uint32_t h,
                                 uint32_t zbias, uint64_t rgba, int32_t *result)
{
    if (!s || !s->fault || s->fault->code != EM_PLAYER_EQUIPMENT_FAULT_NONE) return -1;
    if (!s->workers || !s->world || !point || !result)
        return fail(s, 0x001CD520u, EM_PLAYER_EQUIPMENT_FAULT_NULL_WORKER);
    const EmPlayerEquipmentSpriteWorkers *wk = s->workers;
    const EmPlayerEquipmentSpriteWorld *d = s->world;
    if (!wk->w_001CD370 || !wk->w_001CB5F0 || !wk->w_001CB6B0 || !wk->w_001CB900)
        return fail(s, 0x001CD520u, EM_PLAYER_EQUIPMENT_FAULT_NULL_WORKER);
    if (!d->fog || !d->s3A40 || !d->s3AC0 || !d->s3600)
        return fail(s, 0x001CD520u, EM_PLAYER_EQUIPMENT_FAULT_NULL_WORKER);

    uint32_t v1[4], vf2[4], acc[4] = {0, 0, 0, 0}, q, m[16];
    memcpy(v1, point, 16);                                   /* 001CD580: vf1 = *a2 */

    /* 1. Cull (001CD568..001CD5C4). */
    CALL(0x001CD370u, wk->w_001CD370(wk->ctx, 0, m));
    if (transform(s, 0x001CD584u, m, v1, acc, vf2) < 0) return -1;
    unsigned flags;
    if (clip_flags(s, vf2, &flags) < 0) return -1;
    if (flags & 0x3Fu) {
        *result = EM_PLAYER_EQUIPMENT_SPRITE_CULLED;
        return 0;
    }

    /* 2. Screen point, depth and fog (001CD5C8..001CD648). */
    uint32_t fog[4];
    memcpy(fog, d->fog, 16);                                 /* vf23 */
    memcpy(m, d->s3AC0, 64);                                 /* vf28..vf31 */
    if (transform(s, 0x001CD5F8u, m, v1, acc, vf2) < 0) return -1;
    VU(0x001CD608u, em_vu_div_bits(VF0[3], vf2[3], 3, 3, &q));
    uint32_t clip_w = vf2[3];                                /* stacked at sp + 0x7C */
    VU(0x001CD614u, em_vu_vec_bits(EM_VU_MULQ, 12, EM_VU_NO_BC, vf2, NULL, q, NULL, vf2));
    uint32_t vf3[4] = {zbias, 0, 0, 0};                      /* only the x lane is read */
    VU(0x001CD624u, em_vu_vec_bits(EM_VU_SUBBC, 1, 0, vf2, vf3, 0, NULL, vf2));
    VU(0x001CD628u, em_vu_div_bits(VF0[3], vf2[3], 3, 3, &q));
    VU(0x001CD630u, em_vu_vec_bits(EM_VU_MULQ, 2, EM_VU_NO_BC, vf2, NULL, q, NULL, vf2));
    VU(0x001CD634u, em_vu_vec_bits(EM_VU_MULABC, 1, 2, VF0, fog, 0, NULL, acc));
    VU(0x001CD638u, em_vu_vec_bits(EM_VU_MADDBC, 1, 3, fog, vf2, 0, acc, vf2));
    vf2[3] = em_vu_min_bits(vf2[3], fog[0]);                 /* 001CD63C */
    vf2[3] = em_vu_max_bits(vf2[3], VF0[0]);                 /* 001CD640 */
    for (unsigned k = 0; k < 4; k++)
        d->s3600[k] = em_vu_ftoi4_bits(vf2[k]);              /* 0x70003600..0C */

    /* 3. The half-extent at the point's depth (001CD64C..001CD6C4). */
    d->s3600[4] = em_ee_mul_bits(HALF, w);                   /* 0x70003610 */
    d->s3600[5] = em_ee_mul_bits(HALF, h);                   /* 0x70003614 */
    d->s3600[6] = clip_w;                                    /* 0x70003618 */
    memcpy(v1, d->s3600 + 4, 16);                            /* vf1 = 0x70003610 */
    memcpy(m, d->s3A40, 64);                                 /* vf16..vf19 */
    VU(0x001CD69Cu, em_vu_vec_bits(EM_VU_SUB, 12, EM_VU_NO_BC, m + 8, m + 8, 0, NULL, m + 8));
    if (transform(s, 0x001CD6A0u, m, v1, acc, vf2) < 0) return -1;
    VU(0x001CD6B0u, em_vu_div_bits(VF0[3], vf2[3], 3, 3, &q));
    VU(0x001CD6B8u, em_vu_vec_bits(EM_VU_MULQ, 12, EM_VU_NO_BC, vf2, NULL, q, NULL, vf2));
    vf2[0] = em_vu_ftoi4_bits(vf2[0]);                       /* 001CD6BC: x and y lanes only */
    vf2[1] = em_vu_ftoi4_bits(vf2[1]);
    memcpy(d->s3600 + 4, vf2, 16);                           /* 001CD6C4: all four words */

    /* 4. Depth fade of the colour (001CD6C8..001CD79C). Only the low 32
     * bits of t0 reach a later use. */
    uint32_t c = (uint32_t)rgba;
    if (mode != 0) {
        int32_t fog8 = (int32_t)d->s3600[3] >> 4;
        if (fog8 >= 0x100) fog8 = 0xFF;
        if (fog8 < 0) fog8 = 0;
        uint32_t f = (uint32_t)fog8;
        if (mode == 1) {
            uint32_t a = (c >> 24) & 0xFFu;
            c = (c & UINT32_C(0x00FFFFFF)) | (((a * f) >> 8) << 24);
        } else if (mode == 4 || mode == 3 || mode == 2) {
            uint32_t r = ((c & 0xFFu) * f) >> 8;
            uint32_t g = (((c >> 8) & 0xFFu) * f) >> 8;
            uint32_t b = (((c >> 16) & 0xFFu) * f) >> 8;
            d->s3600[3] = 0xFF0u;                            /* 0x7000360C */
            c = (c & UINT32_C(0xFF000000)) | r | (g << 8) | (b << 16);
        }
    }

    /* 5. The primitive (001CD7A0..001CD8C8). */
    uint32_t chain = UINT32_C(0x0028F700) + ((uint32_t)bucket << 15) + UINT32_C(0x004D3EC0);
    uint8_t *prim = NULL;
    CALL(0x001CB5F0u, wk->w_001CB5F0(wk->ctx, chain, (int32_t)d->s3600[2], 6, &prim));
    if (!prim) return fail(s, 0x001CB5F0u, EM_PLAYER_EQUIPMENT_FAULT_BAD_RESULT);
    memcpy(prim + 0x00, &giftag, 8);
    put32(prim, 0x10, c & 0xFFu);
    put32(prim, 0x14, (c >> 8) & 0xFFu);
    put32(prim, 0x18, (c >> 16) & 0xFFu);
    put32(prim, 0x1C, (c >> 24) & 0xFFu);
    put32(prim, 0x20, 0);
    put32(prim, 0x24, 0);
    put32(prim, 0x28, ONE);
    put32(prim, 0x30, d->s3600[0] + d->s3600[4]);
    put32(prim, 0x34, d->s3600[1] + d->s3600[5]);
    put32(prim, 0x38, d->s3600[2]);
    put32(prim, 0x3C, d->s3600[3]);
    put32(prim, 0x40, ONE);
    put32(prim, 0x44, ONE);
    put32(prim, 0x48, ONE);
    put32(prim, 0x50, d->s3600[0] - d->s3600[4]);
    put32(prim, 0x54, d->s3600[1] - d->s3600[5]);
    put32(prim, 0x58, d->s3600[2]);
    put32(prim, 0x5C, d->s3600[3]);

    /* 6. Close (001CD8CC..001CD910). */
    CALL(0x001CB6B0u, wk->w_001CB6B0(wk->ctx, chain, (int32_t)d->s3600[2], 2, EM_PLAYER_EQUIPMENT_SPRITE_STATE));
    CALL(0x001CB900u, wk->w_001CB900(wk->ctx, chain, (int32_t)d->s3600[2], mode));
    *result = (int32_t)d->s3600[2];
    return 0;
}
