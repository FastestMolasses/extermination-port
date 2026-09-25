/* em_shadow_decal_original.c - 001CE300, 001CF470, 001CF870, 001CF970 and
 * 001CB950 (see em_shadow_decal_original.h and docs/SHADOW_DECAL.md).
 *
 * Read from the original instructions (build/asm of the decomp). Where the
 * readable 001CE300 C differs from its instructions:
 *   - z is multiplied by the VU reciprocal of (w - 1) (a division into Q,
 *     then a Q multiply of the z lane), not divided by (w - 1);
 *   - the fog lane is bias + scale * (w - 1) (the w lane of the clip vector
 *     after the 1.0 subtraction), clamped by the w-lane minimum against
 *     fog.x and the maximum against 0; it is not a separate "f" formula over
 *     fog.z / fog.w with float compares;
 *   - x / y are multiplied by the VU reciprocal of w, and the 12.4 words
 *     come from one float-to-fixed conversion of all four lanes;
 *   - the only COP1 division is 1.0 / w for S, T and Q; the pos quadword's w
 *     is never read (the clip transform weights row 3 by 1.0).
 * 001CF470 / 001CF870 / 001CF970 are hand-written asm in the decomp.
 * Every address in a comment is the original instruction translated there. */
#include "game/em_shadow_decal_original.h"

#include "game/em_ee_float.h"
#include "game/em_sdk_math_original.h"

#include <string.h>

#define F_ZERO    UINT32_C(0x00000000)
#define F_ONE     UINT32_C(0x3F800000)   /* 001CE3CC, 001CE528, 001CF638 */
#define F_MINUS1  UINT32_C(0xBF800000)   /* 001CF658 */

#define REC(d, i) ((d)->stage + (size_t)(i) * EM_SHADOW_DECAL_RECORD_WORDS)

static const uint32_t kVF0[4] = { F_ZERO, F_ZERO, F_ZERO, F_ONE };

/* ---- faults ------------------------------------------------------------------ */

static int fail(EmShadowDecal *d, uint32_t address, int32_t code)
{
    if (d->fault.code == EM_SHADOW_DECAL_FAULT_NONE) {
        d->fault.address = address;
        d->fault.code = code;
    }
    return -1;
}

#define WORKER(d, address, expr) \
    do { if ((expr) < 0) return fail((d), (address), EM_SHADOW_DECAL_FAULT_WORKER); } while (0)
#define VU(d, address, expr) \
    do { if ((expr) != EM_EE_FLOAT_OK) return fail((d), (address), EM_SHADOW_DECAL_FAULT_FLOAT); } while (0)
#define RECORD(d, address, index) \
    do { if ((index) < 0 || (index) >= (int32_t)EM_SHADOW_DECAL_STAGE_RECORDS) \
             return fail((d), (address), EM_SHADOW_DECAL_FAULT_RANGE); } while (0)

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

/* The four-step VU0 row transform: ACC = m0 * v.x; ACC += m1 * v.y;
 * ACC += m2 * v.z; out = ACC + m3 * 1.0 (the w weight is vf0.w), all four
 * lanes. 001CF4B8..001CF4C4 and 001CE554..001CE560. */
static int vu_transform(uint32_t out[4], const uint32_t m[16], const uint32_t v[4])
{
    uint32_t acc[4] = { 0, 0, 0, 0 }, r[4] = { 0, 0, 0, 0 };
    int st = em_vu_vec_bits(EM_VU_MULABC, 15, 0, m + 0, v, 0, NULL, acc);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDABC, 15, 1, m + 4, v, 0, acc, acc);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDABC, 15, 2, m + 8, v, 0, acc, acc);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDBC, 15, 3, m + 12, kVF0, 0, acc, r);
    if (st == EM_EE_FLOAT_OK) memcpy(out, r, sizeof r);
    return st;
}

/* ---- 001CF870 --------------------------------------------------------------------- */

static uint32_t fabs_0011DF78(uint32_t x)
{
    return em_ee_bits(em_sdk_math_original_0011DF78(em_ee_float(x)));
}

int32_t em_shadow_decal_001CF870(const uint32_t a[20], const uint32_t b[20], int32_t axis)
{
    if (!a || !b || axis < 0 || axis > 3) return -1;
    uint32_t code = 0;
    /* 001CF88C..001CF8C4: a's lane above |a.w| (the c.le test fails) -> 1 */
    uint32_t wa = fabs_0011DF78(a[EM_SHADOW_DECAL_REC_CLIP + 3]);
    uint32_t xa = a[EM_SHADOW_DECAL_REC_CLIP + (uint32_t)axis];
    if (!em_ee_c_le_bits(xa, wa)) code |= 0x01u;
    /* 001CF8C8..001CF8EC: a's lane below -|a.w| -> 2 */
    wa = em_ee_neg_bits(fabs_0011DF78(a[EM_SHADOW_DECAL_REC_CLIP + 3]));
    if (em_ee_c_lt_bits(xa, wa)) code |= 0x02u;
    /* 001CF8F0..001CF93C: the same two tests on b -> 0x10 / 0x20 */
    uint32_t wb = fabs_0011DF78(b[EM_SHADOW_DECAL_REC_CLIP + 3]);
    uint32_t xb = b[EM_SHADOW_DECAL_REC_CLIP + (uint32_t)axis];
    if (!em_ee_c_le_bits(xb, wb)) code |= 0x10u;
    wb = em_ee_neg_bits(fabs_0011DF78(b[EM_SHADOW_DECAL_REC_CLIP + 3]));
    if (em_ee_c_lt_bits(xb, wb)) code |= 0x20u;
    return (int32_t)code;
}

/* ---- 001CF970 --------------------------------------------------------------------- */

int em_shadow_decal_001CF970(uint32_t out[20], const uint32_t a[20], const uint32_t b[20], int32_t axis,
                             uint32_t sign)
{
    if (!out || !a || !b || axis < 0) return -1;
    uint32_t ra[20], rb[20];
    memcpy(ra, a, sizeof ra);                        /* 001CF970..001CF994: both records loaded */
    memcpy(rb, b, sizeof rb);
    const uint32_t *ca = ra + EM_SHADOW_DECAL_REC_CLIP, *cb = rb + EM_SHADOW_DECAL_REC_CLIP;
    uint32_t delta[4] = { 0, 0, 0, 0 }, rows[16];
    int st = em_vu_vec_bits(EM_VU_SUB, 15, EM_VU_NO_BC, cb, ca, 0, NULL, delta);   /* 001CF998 */
    for (unsigned r = 0; r < 4 && st == EM_EE_FLOAT_OK; ++r) {                    /* 001CF99C..A8 */
        memcpy(rows + 4 * r, rb + 4 * r, 16);
        st = em_vu_vec_bits(EM_VU_SUB, 15, EM_VU_NO_BC, rows + 4 * r, ra + 4 * r, 0, NULL, rows + 4 * r);
    }
    /* 001CF9AC..001CF9D4: sign * w of each end, then |lane - sign * w| on
     * every lane of both ends. */
    const uint32_t s[4] = { sign, 0, 0, 0 };
    uint32_t wa[4] = { 0, 0, 0, 0 }, wb[4] = { 0, 0, 0, 0 }, da[4] = { 0, 0, 0, 0 }, db[4] = { 0, 0, 0, 0 };
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MULBC, 1, 0, ca, s, 0, NULL, wa);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MULBC, 1, 0, cb, s, 0, NULL, wb);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_SUBBC, 15, 3, ca, wa, 0, NULL, da);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_SUBBC, 15, 3, cb, wb, 0, NULL, db);
    if (st != EM_EE_FLOAT_OK) return -1;
    for (unsigned k = 0; k < 4; ++k) {
        da[k] = em_vu_abs_bits(da[k]);
        db[k] = em_vu_abs_bits(db[k]);
    }
    /* 001CF9D8..001CF9F4: the low three words of both 128-bit copies turn
     * once per axis step (word 0 takes word 1, word 1 word 2, word 2 word 0),
     * so lane x ends up holding lane[axis]. */
    for (int32_t n = axis; n != 0; --n) {
        uint32_t t = da[0];
        da[0] = da[1]; da[1] = da[2]; da[2] = t;
        t = db[0];
        db[0] = db[1]; db[1] = db[2]; db[2] = t;
    }
    /* 001CFA00..001CFA10: t = |da.x / (db.x + da.x)| through Q. */
    uint32_t sum[4] = { 0, 0, 0, 0 }, q = 0, t[4] = { 0, 0, 0, 0 };
    st = em_vu_vec_bits(EM_VU_ADD, 14, EM_VU_NO_BC, db, da, 0, NULL, sum);
    if (st == EM_EE_FLOAT_OK) st = em_vu_div_bits(da[0], sum[0], 0, 0, &q);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_ADDQ, 15, EM_VU_NO_BC, kVF0, NULL, q, NULL, t);
    if (st != EM_EE_FLOAT_OK) return -1;
    t[0] = em_vu_abs_bits(t[0]);
    /* 001CFA14..001CFA38: every difference times t, added back to a. */
    uint32_t clip[4] = { 0, 0, 0, 0 };
    st = em_vu_vec_bits(EM_VU_MULBC, 15, 0, delta, t, 0, NULL, delta);
    for (unsigned r = 0; r < 4 && st == EM_EE_FLOAT_OK; ++r)
        st = em_vu_vec_bits(EM_VU_MULBC, 15, 0, rows + 4 * r, t, 0, NULL, rows + 4 * r);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_ADD, 15, EM_VU_NO_BC, ca, delta, 0, NULL, clip);
    for (unsigned r = 0; r < 4 && st == EM_EE_FLOAT_OK; ++r)
        st = em_vu_vec_bits(EM_VU_ADD, 15, EM_VU_NO_BC, rows + 4 * r, ra + 4 * r, 0, NULL, rows + 4 * r);
    if (st != EM_EE_FLOAT_OK) return -1;
    memcpy(out, rows, sizeof rows);                                  /* 001CFA3C..001CFA48 */
    memcpy(out + EM_SHADOW_DECAL_REC_CLIP, clip, sizeof clip);       /* 001CFA50 */
    return 0;
}

/* ---- 001CF470 --------------------------------------------------------------------- */

static int reach_stage(const EmShadowDecal *d)
{
    return d && d->stage;
}

/* The clipper body over the stage (after the entry checks). */
static int clip_001CF470(EmShadowDecal *d, const uint32_t m[16], int32_t *out_count)
{
    /* 001CF4A0..001CF508: the three input records' clip positions. */
    for (unsigned v = 0; v < 3; ++v) {
        uint32_t *rec = REC(d, v), pos[4], clip[4];
        memcpy(pos, rec + EM_SHADOW_DECAL_REC_POS, sizeof pos);
        VU(d, 0x001CF4B8u + 0x20u * v, vu_transform(clip, m, pos));
        memcpy(rec + EM_SHADOW_DECAL_REC_CLIP, clip, sizeof clip);
    }
    /* 001CF50C..001CF51C: count 3; the first pass flips the bases to input
     * 0 (D_008112C0) and output 16 (D_008117C0). */
    int32_t count = 3, in_base = 16, out_base = 0;
    for (int32_t axis = 2; axis >= 0; --axis) {                      /* 001CF828: z, y, x */
        int32_t n = count;
        count = 0;
        in_base = 16 - in_base;                                      /* 001CF52C / 001CF530 */
        out_base = 16 - out_base;
        for (int32_t i = 0; i < n; ++i) {
            /* 001CF540..001CF57C: edge (i mod n) -> (i + 1 mod n) */
            int32_t ia = in_base + i % n, ib = in_base + (i + 1) % n;
            RECORD(d, 0x001CF560u, ia);
            RECORD(d, 0x001CF57Cu, ib);
            const uint32_t *a = REC(d, ia), *b = REC(d, ib);
            int32_t code = em_shadow_decal_001CF870(a, b, axis) & 0xFF;   /* 001CF580 / 001CF588 */
            int32_t o = out_base + count;
            switch (code) {
            case 0x22: /* both below */
            case 0x11: /* both above */
                break;
            case 0x21: /* a above, b below: the +w crossing, then the -w crossing */
                RECORD(d, 0x001CF7B0u, o);
                if (em_shadow_decal_001CF970(REC(d, o), a, b, axis, F_ONE) < 0)
                    return fail(d, 0x001CF970u, EM_SHADOW_DECAL_FAULT_FLOAT);
                RECORD(d, 0x001CF7ECu, o + 1);
                if (em_shadow_decal_001CF970(REC(d, o + 1), a, b, axis, F_MINUS1) < 0)
                    return fail(d, 0x001CF970u, EM_SHADOW_DECAL_FAULT_FLOAT);
                count += 2;
                break;
            case 0x12: /* a below, b above: -w, then +w */
                RECORD(d, 0x001CF74Cu, o);
                if (em_shadow_decal_001CF970(REC(d, o), a, b, axis, F_MINUS1) < 0)
                    return fail(d, 0x001CF970u, EM_SHADOW_DECAL_FAULT_FLOAT);
                RECORD(d, 0x001CF77Cu, o + 1);
                if (em_shadow_decal_001CF970(REC(d, o + 1), a, b, axis, F_ONE) < 0)
                    return fail(d, 0x001CF970u, EM_SHADOW_DECAL_FAULT_FLOAT);
                count += 2;
                break;
            case 0x20: /* a inside, b below: a, then the -w crossing */
                RECORD(d, 0x001CF6F4u, o);
                memmove(REC(d, o), a, EM_SHADOW_DECAL_RECORD_WORDS * 4u);    /* 001CF6FC block_copy */
                RECORD(d, 0x001CF718u, o + 1);
                if (em_shadow_decal_001CF970(REC(d, o + 1), a, b, axis, F_MINUS1) < 0)
                    return fail(d, 0x001CF970u, EM_SHADOW_DECAL_FAULT_FLOAT);
                count += 2;
                break;
            case 0x10: /* a inside, b above: a, then the +w crossing */
                RECORD(d, 0x001CF69Cu, o);
                memmove(REC(d, o), a, EM_SHADOW_DECAL_RECORD_WORDS * 4u);    /* 001CF6A4 block_copy */
                RECORD(d, 0x001CF6C0u, o + 1);
                if (em_shadow_decal_001CF970(REC(d, o + 1), a, b, axis, F_ONE) < 0)
                    return fail(d, 0x001CF970u, EM_SHADOW_DECAL_FAULT_FLOAT);
                count += 2;
                break;
            case 0x02: /* a below, b inside: the -w crossing */
                RECORD(d, 0x001CF670u, o);
                if (em_shadow_decal_001CF970(REC(d, o), a, b, axis, F_MINUS1) < 0)
                    return fail(d, 0x001CF970u, EM_SHADOW_DECAL_FAULT_FLOAT);
                count += 1;
                break;
            case 0x01: /* a above, b inside: the +w crossing */
                RECORD(d, 0x001CF634u, o);
                if (em_shadow_decal_001CF970(REC(d, o), a, b, axis, F_ONE) < 0)
                    return fail(d, 0x001CF970u, EM_SHADOW_DECAL_FAULT_FLOAT);
                count += 1;
                break;
            case 0x00: /* both inside: a */
                RECORD(d, 0x001CF60Cu, o);
                memmove(REC(d, o), a, EM_SHADOW_DECAL_RECORD_WORDS * 4u);    /* 001CF614 block_copy */
                count += 1;
                break;
            default:   /* 001CF5F4: any other code emits nothing (unreachable: a lane
                        * cannot be both above |w| and below -|w|) */
                break;
            }
        }
        if (count == 0) {                                            /* 001CF814: nothing left */
            *out_count = 0;
            return 0;
        }
    }
    *out_count = count;                                              /* 001CF830 */
    return 0;
}

int em_shadow_decal_001CF470(EmShadowDecal *d, const uint32_t m[16], int32_t *count)
{
    if (!d) return -1;
    if (d->fault.code) return -1;
    if (!reach_stage(d) || !m || !count) return fail(d, 0x001CF470u, EM_SHADOW_DECAL_FAULT_UNBOUND);
    return clip_001CF470(d, m, count);
}

/* ---- 001CB950 --------------------------------------------------------------------- */

static int tex0_001CB950(EmShadowDecal *d, uint32_t table, int32_t id, uint64_t tex0)
{
    const EmShadowDecalWorkers *w = d->workers;
    uint8_t *p = NULL;
    WORKER(d, 0x001CB5F0u, w->w_001CB5F0(w->ctx, table, id, 3, &p));   /* 001CB960 */
    if (!p) return fail(d, 0x001CB5F0u, EM_SHADOW_DECAL_FAULT_WORKER);
    memset(p, 0, 16);                                                   /* 001CB96C */
    wr32(p + 0x0C, 0x50000002u);                                        /* DIRECT, 2 quadwords */
    wr64(p + 0x10, UINT64_C(0x1000000000008001));                      /* NLOOP 1, EOP, A+D, NREG 1 */
    wr64(p + 0x18, UINT64_C(0xE));                                      /* REGS: A+D */
    wr64(p + 0x20, tex0);                                               /* 001CB998 */
    wr64(p + 0x28, UINT64_C(6));                                        /* TEX0_1 */
    return 0;
}

int em_shadow_decal_001CB950(EmShadowDecal *d, uint32_t table, int32_t id, uint64_t tex0)
{
    if (!d) return -1;
    if (d->fault.code) return -1;
    if (!d->workers || !d->workers->w_001CB5F0) return fail(d, 0x001CB950u, EM_SHADOW_DECAL_FAULT_UNBOUND);
    return tex0_001CB950(d, table, id, tex0);
}

/* ---- 001CE300 --------------------------------------------------------------------- */

static int reach_001CE300(const EmShadowDecal *d)
{
    const EmShadowDecalWorkers *w = d->workers;
    return reach_stage(d) && w && w->w_001CD370 && w->w_001CB5F0 && w->w_001CB900 && w->fog &&
           d->scratch.s3600 && d->scratch.s3AC0;
}

/* One fan vertex (001CE550..001CE5E0) from stage record `src` into the 0x30
 * packet bytes at `v`. */
static int vertex(EmShadowDecal *d, uint8_t *v, const uint32_t *src, const uint32_t cam[16],
                  const uint32_t fog[4])
{
    uint32_t pos[4], clip[4], x[4], q = 0, q2 = 0;
    memcpy(pos, src + EM_SHADOW_DECAL_REC_POS, sizeof pos);
    VU(d, 0x001CE554u, vu_transform(clip, cam, pos));
    VU(d, 0x001CE564u, em_vu_div_bits(kVF0[3], clip[3], 3, 3, &q));         /* Q = 1 / w */
    memcpy(x, clip, sizeof x);                                              /* the stack copy keeps w */
    VU(d, 0x001CE570u, em_vu_vec_bits(EM_VU_MULQ, 12, EM_VU_NO_BC, x, NULL, q, NULL, x));
    /* 001CE574..001CE580: the w lane minus 1.0 (the x lane of a register
     * loaded from the COP1 1.0). */
    const uint32_t one[4] = { F_ONE, 0, 0, 0 };
    VU(d, 0x001CE580u, em_vu_vec_bits(EM_VU_SUBBC, 1, 0, x, one, 0, NULL, x));
    VU(d, 0x001CE584u, em_vu_div_bits(kVF0[3], x[3], 3, 3, &q2));          /* Q = 1 / (w - 1) */
    VU(d, 0x001CE58Cu, em_vu_vec_bits(EM_VU_MULQ, 2, EM_VU_NO_BC, x, NULL, q2, NULL, x));
    /* 001CE590..001CE59C: fog lane = max(min(fog.z + fog.w * (w - 1), fog.x), 0) */
    uint32_t acc[4] = { 0, 0, 0, 0 };
    VU(d, 0x001CE590u, em_vu_vec_bits(EM_VU_MULABC, 1, 2, kVF0, fog, 0, NULL, acc));
    VU(d, 0x001CE594u, em_vu_vec_bits(EM_VU_MADDBC, 1, 3, fog, x, 0, acc, x));
    x[3] = em_vu_min_bits(x[3], fog[0]);
    x[3] = em_vu_max_bits(x[3], kVF0[0]);
    for (unsigned k = 0; k < 4; ++k)                                        /* 001CE5A0 / 001CE5A4 */
        wr32(v + 0x20 + 4u * k, em_vu_ftoi4_bits(x[k]));
    uint32_t invw = em_ee_div_bits(F_ONE, clip[3]);                         /* 001CE5B4 */
    for (unsigned k = 0; k < 4; ++k)                                        /* 001CE5AC / 001CE5B8 */
        wr32(v + 0x10 + 4u * k, d->scratch.s3600[k]);
    wr32(v + 0x00, em_ee_mul_bits(src[EM_SHADOW_DECAL_REC_S], invw));      /* 001CE5C0 */
    wr32(v + 0x04, em_ee_mul_bits(src[EM_SHADOW_DECAL_REC_T], invw));      /* 001CE5CC */
    wr32(v + 0x08, invw);
    wr32(v + 0x0C, F_ONE);
    return 0;
}

static int decal_001CE300(EmShadowDecal *d, int32_t tag, const uint32_t quad[16], uint64_t tex0,
                          uint32_t rgba)
{
    const EmShadowDecalWorkers *w = d->workers;
    /* 001CE320..001CE364: the colour bytes, widened. */
    for (unsigned k = 0; k < 4; ++k)
        d->scratch.s3600[k] = (rgba >> (8u * k)) & 0xFFu;
    for (int32_t pass = 0; pass < 2; ++pass) {                       /* 001CE600 */
        /* 001CE370..001CE474: triangle {pass, pass + 1, pass + 2} */
        for (int32_t j = 0; j < 3; ++j) {
            int32_t corner = pass + j;
            uint32_t *rec = REC(d, j);
            memcpy(rec + EM_SHADOW_DECAL_REC_POS, quad + 4 * corner, 16);
            switch (corner) {
            case 3: rec[EM_SHADOW_DECAL_REC_S] = F_ONE;  rec[EM_SHADOW_DECAL_REC_T] = F_ONE;  break;
            case 2: rec[EM_SHADOW_DECAL_REC_S] = F_ONE;  rec[EM_SHADOW_DECAL_REC_T] = F_ZERO; break;
            case 1: rec[EM_SHADOW_DECAL_REC_S] = F_ZERO; rec[EM_SHADOW_DECAL_REC_T] = F_ONE;  break;
            default: rec[EM_SHADOW_DECAL_REC_S] = F_ZERO; rec[EM_SHADOW_DECAL_REC_T] = F_ZERO; break;
            }
        }
        uint32_t m[16];
        WORKER(d, 0x001CD370u, w->w_001CD370(w->ctx, 0, m));         /* 001CE47C */
        int32_t count = 0;
        if (clip_001CF470(d, m, &count) < 0) return -1;             /* 001CE48C */
        if (count == 0) continue;                                    /* 001CE498 */
        int32_t qwc = (int32_t)((uint32_t)count * 3u + 2u);
        uint8_t *p = NULL;
        WORKER(d, 0x001CB5F0u, w->w_001CB5F0(w->ctx, EM_SHADOW_DECAL_PAGE, 0, qwc, &p));  /* 001CE4B8 */
        if (!p) return fail(d, 0x001CB5F0u, EM_SHADOW_DECAL_FAULT_WORKER);
        memset(p, 0, 16);                                            /* 001CE4C8 */
        wr32(p + 0x0C, 0x50000000u | ((uint32_t)qwc - 1u));        /* DIRECT, qwc - 1 quadwords */
        /* 001CE4D4..001CE4FC: NLOOP = count (sign-extended), EOP, PRE, PRIM
         * 0x7D (fan, Gouraud, textured, fog, blend), PACKED, 3 registers
         * ST / RGBAQ / XYZF2. */
        wr64(p + 0x10, (UINT64_C(0x303EC000) << 32) | UINT64_C(0x8000) | (uint64_t)(int64_t)count);
        wr64(p + 0x18, UINT64_C(0x412));
        uint32_t fog[4], cam[16];
        WORKER(d, 0x001CE50Cu, w->fog(w->ctx, fog));
        memcpy(cam, d->scratch.s3AC0, sizeof cam);                   /* 001CE518..001CE524 */
        for (int32_t k = 0; k < count; ++k) {
            RECORD(d, 0x001CE550u, 16 + k);
            if (vertex(d, p + 0x20 + 0x30 * (size_t)k, REC(d, 16 + k), cam, fog) < 0) return -1;
        }
    }
    if (tex0_001CB950(d, EM_SHADOW_DECAL_PAGE, 0, tex0) < 0) return -1;             /* 001CE618 */
    WORKER(d, 0x001CB900u, w->w_001CB900(w->ctx, EM_SHADOW_DECAL_PAGE, 0, tag));   /* 001CE62C */
    return 0;
}

int em_shadow_decal_001CE300(EmShadowDecal *d, int32_t tag, const uint32_t quad[16], uint64_t tex0,
                             uint32_t rgba)
{
    if (!d) return -1;
    if (d->fault.code) return -1;
    if (!reach_001CE300(d) || !quad) return fail(d, 0x001CE300u, EM_SHADOW_DECAL_FAULT_UNBOUND);
    return decal_001CE300(d, tag, quad, tex0, rgba);
}

int em_shadow_decal_w_001CE300(void *ctx, int32_t tag, const uint32_t corners[16], uint64_t tex0,
                               uint32_t rgba)
{
    return em_shadow_decal_001CE300(ctx, tag, corners, tex0, rgba);
}

/* ---- the packet-chain binding --------------------------------------------------------- */

static int chain_001CD370(void *ctx, int32_t a0, uint32_t m[16])
{
    EmPacketChain *pc = ctx;
    if (!pc || !m) return -1;
    uint32_t address = pc->d275670 + ((uint32_t)a0 << 6) + EM_SHADOW_DECAL_CLIP_OFFSET;
    const uint8_t *b = em_packet_chain_at(pc, address, 64);
    if (!b) return -1;
    for (unsigned i = 0; i < 16; ++i)
        m[i] = (uint32_t)b[4 * i] | (uint32_t)b[4 * i + 1] << 8 | (uint32_t)b[4 * i + 2] << 16 |
               (uint32_t)b[4 * i + 3] << 24;
    return 0;
}

static int chain_fog(void *ctx, uint32_t out[4])
{
    return em_packet_chain_fog(ctx, out);
}

void em_shadow_decal_bind_packet_chain(EmShadowDecalWorkers *w, EmPacketChain *pc)
{
    if (!w) return;
    w->ctx = pc;
    w->w_001CD370 = chain_001CD370;
    w->w_001CB5F0 = em_packet_chain_w_001CB5F0;
    w->w_001CB900 = em_packet_chain_w_001CB900;
    w->fog = chain_fog;
}
