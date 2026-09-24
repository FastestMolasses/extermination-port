/* em_coll_list_passes.c - the actor list passes of 001AAD00 (see the header
 * and docs/COLL_LIST_PASSES.md).
 *
 * Every routine names the original it translates; comments cite original
 * addresses. The byte-matched C is followed statement for statement (its
 * volatile scratchpad counters are EmCollListGlobals.s3B86 / s3B88, re-read
 * and re-written wherever the C accesses them); 001A7870 follows the .s. */
#include "game/em_coll_list_passes.h"

#include <string.h>

#include "game/em_ee_float.h"
#include "game/em_effect_original.h"
#include "game/em_sdk_soft_float.h"

typedef EmCollListPasses P;

#define TRY(x) do { if ((x) < 0) return -1; } while (0)

static int fail(P *p, uint32_t where)
{
    if (!p->fault) p->fault = where;
    return -1;
}

/* ---- Original-address access (little-endian original layout) ------------ */

static uint8_t *span(P *p, uint32_t address, uint32_t size, uint32_t where)
{
    uint8_t *b = p->memory.bytes(p->memory.context, address, size);
    if (!b) fail(p, where);
    return b;
}

static int rd_u8(P *p, uint32_t a, uint8_t *v, uint32_t where)
{
    const uint8_t *b = span(p, a, 1, where);
    if (!b) return -1;
    *v = b[0];
    return 0;
}

static int rd_s8(P *p, uint32_t a, int8_t *v, uint32_t where)
{
    uint8_t u;
    TRY(rd_u8(p, a, &u, where));
    *v = (int8_t)u;
    return 0;
}

static int rd_u16(P *p, uint32_t a, uint16_t *v, uint32_t where)
{
    const uint8_t *b = span(p, a, 2, where);
    if (!b) return -1;
    memcpy(v, b, 2);
    return 0;
}

static int rd_s16(P *p, uint32_t a, int16_t *v, uint32_t where)
{
    const uint8_t *b = span(p, a, 2, where);
    if (!b) return -1;
    memcpy(v, b, 2);
    return 0;
}

static int rd_u32(P *p, uint32_t a, uint32_t *v, uint32_t where)
{
    const uint8_t *b = span(p, a, 4, where);
    if (!b) return -1;
    memcpy(v, b, 4);
    return 0;
}

static int rd_f(P *p, uint32_t a, float *v, uint32_t where)
{
    uint32_t w;
    TRY(rd_u32(p, a, &w, where));
    *v = em_ee_float(w);
    return 0;
}

static int wr_u8(P *p, uint32_t a, uint8_t v, uint32_t where)
{
    uint8_t *b = span(p, a, 1, where);
    if (!b) return -1;
    b[0] = v;
    return 0;
}

static int wr_u16(P *p, uint32_t a, uint16_t v, uint32_t where)
{
    uint8_t *b = span(p, a, 2, where);
    if (!b) return -1;
    memcpy(b, &v, 2);
    return 0;
}

static int wr_f(P *p, uint32_t a, float v, uint32_t where)
{
    uint8_t *b = span(p, a, 4, where);
    if (!b) return -1;
    const uint32_t w = em_ee_bits(v);
    memcpy(b, &w, 4);
    return 0;
}

/* ---- Worker calls ----------------------------------------------------------- */

static int call_pair(P *p, EmCollListPairWorker w, uint32_t a, uint32_t b, uint32_t where)
{
    if (!w || w(p->workers.context, p, a, b) < 0) return fail(p, where);
    return 0;
}

static int ready(const P *p)
{
    return p && p->memory.bytes && p->globals;
}

/* The workers and data 001A8660 needs (and 001A8BE0 through it). */
static int bound_8660(const P *p)
{
    return p && p->memory.bytes && p->math && p->math->tables && p->data && p->data->d24A740 &&
           p->workers.behaviour && p->workers.w_0021BD10 && p->workers.normalize;
}

static int ready_8660(const P *p)
{
    return ready(p) && bound_8660(p);
}

/* ---- 001A9D20 ---------------------------------------------------------------- */

int em_coll_list_001A9D20(P *p)
{
    if (!ready(p) || !p->workers.w_001A9C40) return p ? fail(p, 0x1A9D20) : -1;
    EmCollListGlobals *g = p->globals;
    if (g->d275B98 == 0) return 0;
    int32_t outer_left = g->d275BB8;
    uint32_t outer_pp = g->d275BB0;
    if (outer_left < 0) return fail(p, 0x1A9D20);
    while (outer_left != 0) {
        uint32_t outer;
        uint8_t flags;
        TRY(rd_u32(p, outer_pp, &outer, 0x1A9D20));
        outer_left--;
        outer_pp += 4;
        TRY(rd_u8(p, outer, &flags, 0x1A9D20));
        if (!(flags & 1)) continue;
        int32_t inner_left = g->d275B98;                  /* re-read every outer entry */
        uint32_t inner_pp = g->d275B90;
        if (inner_left < 0) return fail(p, 0x1A9D20);
        while (inner_left != 0) {
            uint32_t inner;
            uint8_t iflags, type;
            TRY(rd_u32(p, inner_pp, &inner, 0x1A9D20));
            inner_left--;
            inner_pp += 4;
            TRY(rd_u8(p, inner, &iflags, 0x1A9D20));
            if (!(iflags & 1)) continue;
            TRY(rd_u8(p, inner + 3, &type, 0x1A9D20));
            switch (type) {
            case 0: case 1: case 4: case 5: case 6: case 7:
                TRY(call_pair(p, p->workers.w_001A9C40, outer, inner, 0x1A9DC0));
                break;
            default:
                break;
            }
        }
    }
    return 0;
}

/* ---- 001A8DA0 ---------------------------------------------------------------- */

int em_coll_list_001A8DA0(P *p)
{
    if (!ready(p) || !p->workers.w_001A8CE0) return p ? fail(p, 0x1A8DA0) : -1;
    EmCollListGlobals *g = p->globals;
    if (g->d275BA8 == 0) return 0;
    int32_t n = g->d275BB8;
    uint32_t q = g->d275BB0;
    if (n == 0) return 0;
    if (n < 0) return fail(p, 0x1A8DA0);
    do {
        uint32_t e;
        uint8_t flags;
        TRY(rd_u32(p, q, &e, 0x1A8DA0));
        q += 4;
        n--;
        TRY(rd_u8(p, e, &flags, 0x1A8DA0));
        if (flags & 1) {
            g->s3B86 = g->d275BA8;                        /* 0x70003B86 */
            uint32_t pp = g->d275BA0;
            while (g->s3B86 != 0) {
                uint32_t f;
                uint8_t fflags, type, param;
                TRY(rd_u32(p, pp, &f, 0x1A8DA0));
                pp += 4;
                g->s3B86 = (int16_t)(g->s3B86 - 1);
                TRY(rd_u8(p, f, &fflags, 0x1A8DA0));
                if (!(fflags & 1)) continue;
                TRY(rd_u8(p, f + 3, &type, 0x1A8DA0));
                if (type != 3) continue;
                TRY(rd_u8(p, f + 0xD, &param, 0x1A8DA0));
                if (param != 0) continue;
                TRY(call_pair(p, p->workers.w_001A8CE0, e, f, 0x1A8E40));
            }
        }
    } while (n != 0);
    return 0;
}

/* ---- 001A9F60 ---------------------------------------------------------------- */

int em_coll_list_001A9F60(P *p, uint32_t player)
{
    if (!ready(p) || !p->workers.w_001A9E00) return p ? fail(p, 0x1A9F60) : -1;
    EmCollListGlobals *g = p->globals;
    if (g->s3B8D != 0) return 0;                          /* 0x1A9F7C */
    if (g->d28A9A0 != 0) return 0;                        /* 0x1A9F8C */
    int32_t n = g->d275B98;                               /* 0x1A9F94 */
    uint32_t q = g->d275B90;
    if (n == 0) return 0;
    if (n < 0) return fail(p, 0x1A9F94);
    do {
        uint32_t e;
        uint8_t cls, flags, type;
        TRY(rd_u32(p, q, &e, 0x1A9FA0));                  /* the entry stays in a1 */
        n--;
        q += 4;
        TRY(rd_u8(p, e + 2, &cls, 0x1A9FAC));
        if ((cls & 0x1F) != 2) continue;
        TRY(rd_u8(p, e, &flags, 0x1A9FBC));
        if (!(flags & 1)) continue;
        TRY(rd_u8(p, e + 3, &type, 0x1A9FCC));
        if (type != 0) continue;
        TRY(call_pair(p, p->workers.w_001A9E00, player, e, 0x1A9FD8));
    } while (n != 0);
    return 0;
}

/* ---- 001AA140 ---------------------------------------------------------------- */

/* The four-part test both loops of 001AA140 apply. */
static int aa140_entry(P *p, uint32_t e, int *ok)
{
    uint8_t cls, type, status;
    uint32_t word;
    *ok = 0;
    TRY(rd_u8(p, e + 2, &cls, 0x1AA140));
    if ((cls & 0x1F) != 2) return 0;
    TRY(rd_u8(p, e + 3, &type, 0x1AA140));
    if (type != 0) return 0;
    TRY(rd_u32(p, e + 0x1F0 + 0xE4, &word, 0x1AA140));
    if (word & 0xF) return 0;
    TRY(rd_u8(p, e, &status, 0x1AA140));
    if (status == 2) return 0;
    *ok = 1;
    return 0;
}

int em_coll_list_001AA140(P *p)
{
    if (!ready(p) || !p->workers.w_001AA000) return p ? fail(p, 0x1AA140) : -1;
    EmCollListGlobals *g = p->globals;
    g->s3B88 = g->d275B98;                                /* 0x70003B88 */
    const int16_t v1 = g->s3B88;
    if (!(v1 > 1)) return 0;
    uint32_t s1 = g->d275B90;
    g->s3B88 = (int16_t)(v1 - 1);
    while (g->s3B88 != 0) {
        uint32_t s0;
        int ok;
        TRY(rd_u32(p, s1, &s0, 0x1AA140));
        s1 += 4;
        const uint32_t s3 = s0 + 0x1F0;
        TRY(aa140_entry(p, s0, &ok));
        if (ok) {
            g->s3B86 = g->s3B88;                          /* the value the loop test read */
            uint32_t s2 = s1;
            while (g->s3B86 != 0) {
                uint32_t a1p;
                int ok2;
                TRY(rd_u32(p, s2, &a1p, 0x1AA140));
                s2 += 4;
                g->s3B86 = (int16_t)(g->s3B86 - 1);
                const uint32_t a3p = a1p + 0x1F0;
                TRY(aa140_entry(p, a1p, &ok2));
                if (!ok2) continue;
                if (p->workers.w_001AA000(p->workers.context, p, s0, a1p, s3, a3p) < 0)
                    return fail(p, 0x1AA23C);
            }
        }
        g->s3B88 = (int16_t)(g->s3B88 - 1);
    }
    return 0;
}

/* ---- 001A7870 ---------------------------------------------------------------- */

/* The capsule of one class-2 entry: its +0x58 record `c` (c +7 the bone
 * slot, +0xC/+0x10/+0x14 the offset, +0x18 the radius, +0x1C the half
 * length) placed at the entry's +0x110[c +7] node (+0xC0/+0xC4/+0xC8). */
typedef struct {
    float off_c, off_10, off_14, radius, half;
    float node_c0, node_c4, node_c8;
} Capsule;

static int capsule(P *p, uint32_t e, Capsule *k, uint32_t where)
{
    uint32_t c, node;
    int8_t slot;
    TRY(rd_u32(p, e + 0x58, &c, where));
    TRY(rd_s8(p, c + 7, &slot, where));
    TRY(rd_f(p, c + 0x10, &k->off_10, where));
    TRY(rd_f(p, c + 0xC, &k->off_c, where));
    TRY(rd_f(p, c + 0x14, &k->off_14, where));
    TRY(rd_f(p, c + 0x1C, &k->half, where));
    TRY(rd_f(p, c + 0x18, &k->radius, where));
    TRY(rd_u32(p, e + (uint32_t)((int32_t)slot * 4) + 0x110, &node, where));
    TRY(rd_f(p, node + 0xC4, &k->node_c4, where));
    TRY(rd_f(p, node + 0xC0, &k->node_c0, where));
    TRY(rd_f(p, node + 0xC8, &k->node_c8, where));
    return 0;
}

int em_coll_list_001A7870(P *p)
{
    if (!ready(p) || !p->math || !p->math->tables) return p ? fail(p, 0x1A7870) : -1;
    EmCollListGlobals *g = p->globals;
    if (g->d275B98 < 2) return 0;                                     /* 0x1A78AC */
    /* Pass 1: +0x50 = 1 for an active entry whose +0x58 record is set,
     * holds a nonzero first word, has -2 at +0xA and shares a bit of its
     * +6 byte with the entry's +0x5E; 0 otherwise. */
    uint32_t t0 = g->d275B90;                                         /* 0x1A78B8 */
    int32_t a3 = 0;
    int16_t count;
    for (;;) {
        count = g->d275B98;                                           /* 0x1A7930: re-read each step */
        if (!(a3 < count)) break;
        uint32_t e, rec, first;
        uint8_t flags, bits6, mask5E;
        int16_t tag;
        int mark = 0;
        TRY(rd_u32(p, t0, &e, 0x1A78CC));
        TRY(rd_u8(p, e, &flags, 0x1A78D0));
        if (flags & 1) {
            TRY(rd_u32(p, e + 0x58, &rec, 0x1A78E0));
            if (rec) {
                TRY(rd_u32(p, rec, &first, 0x1A78EC));
                if (first) {
                    TRY(rd_s16(p, rec + 0xA, &tag, 0x1A78F8));
                    if (tag == -2) {
                        TRY(rd_u8(p, rec + 6, &bits6, 0x1A7904));
                        TRY(rd_u8(p, e + 0x5E, &mask5E, 0x1A7908));
                        mark = (bits6 & mask5E) != 0;
                    }
                }
            }
        }
        TRY(wr_u16(p, e + 0x50, (uint16_t)mark, mark ? 0x1A791C : 0x1A7920));
        t0 += 4;
        a3++;
    }
    g->s3B88 = count;                                                 /* 0x1A7944 */
    g->s3B88 = (int16_t)(g->s3B88 - 1);                               /* 0x1A7960 */
    uint32_t s1 = g->d275B90;                                         /* 0x1A7950 */
    /* Pass 2: each marked outer entry against the marked entries after it. */
    while (g->s3B88 != 0) {                                           /* 0x1A7B30 */
        const int16_t a1 = g->s3B88;
        uint32_t outer;
        uint16_t marked;
        TRY(rd_u32(p, s1, &outer, 0x1A7964));
        s1 += 4;
        TRY(rd_u16(p, outer + 0x50, &marked, 0x1A7968));
        if (marked) {
            g->s3B86 = a1;                                            /* 0x1A7978 */
            uint32_t s2 = s1;
            Capsule o;
            TRY(capsule(p, outer, &o, 0x1A797C));
            const float o_axis = em_ee_add(o.node_c4, o.off_10);      /* 0x1A79B4 */
            const float o_c0 = em_ee_add(o.node_c0, o.off_c);         /* 0x1A79B8 */
            const float o_c8 = em_ee_add(o.node_c8, o.off_14);        /* 0x1A79BC */
            const float lo = em_ee_sub(o_axis, o.half);               /* 0x1A79C0 */
            const float hi = em_ee_add(o_axis, o.half);               /* 0x1A79C8 */
            while (g->s3B86 != 0) {                                   /* 0x1A7B08 */
                uint32_t inner;
                uint16_t imarked;
                const int16_t left = g->s3B86;                        /* 0x1A79D0 */
                TRY(rd_u32(p, s2, &inner, 0x1A79D4));
                g->s3B86 = (int16_t)(left - 1);                       /* 0x1A79E0 */
                s2 += 4;
                TRY(rd_u16(p, inner + 0x50, &imarked, 0x1A79E4));
                if (!imarked) continue;
                Capsule k;
                TRY(capsule(p, inner, &k, 0x1A79F0));
                const float i_axis = em_ee_add(k.node_c4, k.off_10);  /* 0x1A7A24 */
                const float i_c8 = em_ee_add(k.node_c8, k.off_14);    /* 0x1A7A28 */
                const float glo = em_ee_sub(i_axis, k.half);          /* 0x1A7A2C */
                const float i_c0 = em_ee_add(k.node_c0, k.off_c);     /* 0x1A7A30 */
                const float ghi = em_ee_add(i_axis, k.half);          /* 0x1A7A40 */
                if (em_ee_c_lt(hi, glo)) continue;                    /* 0x1A7A34 */
                if (!em_ee_c_le(lo, ghi)) continue;                   /* 0x1A7A44 */
                const float d0 = em_ee_sub(i_c0, o_c0);               /* 0x1A7A54 */
                const float d8 = em_ee_sub(i_c8, o_c8);               /* 0x1A7A58 */
                const float sq = em_ee_madd(em_ee_mula(d0, d0), d8, d8);  /* 0x1A7A5C, 0x1A7A64 */
                float len;
                uint32_t fault = 0;
                if (em_sdk_math_original_0011E748(p->math->tables, &p->math->world, &p->math->workers,
                                                  sq, &len, &fault) < 0)
                    return fail(p, fault ? fault : 0x1A7A60);
                const float over = em_ee_sub(em_ee_add(o.radius, k.radius), len);  /* 0x1A7A68, 0x1A7A6C */
                if (em_ee_c_lt(over, 0.0f)) continue;                 /* 0x1A7A7C */
                uint16_t h52;
                TRY(rd_u16(p, inner + 0x52, &h52, 0x1A7A8C));
                if (h52 & 1) continue;                                /* 0x1A7A94 */
                /* 001000C0(00128350(len), 0.001): 001274B0 < 0. */
                const uint64_t wide = em_sdk_soft_float_00128350(em_ee_bits(len));   /* 0x1A7A9C */
                const int tiny = em_sdk_soft_float_001274B0(wide, UINT64_C(0x3F50624DD2F1A9FC)) < 0;  /* 0x1A7AC0 */
                const float push0 = em_ee_mul(d0, over);              /* 0x1A7ACC */
                float b0, b8;
                TRY(rd_f(p, inner + 0xB0, &b0, 0x1A7AD0));
                if (tiny) {
                    TRY(wr_f(p, inner + 0xB0, em_ee_add(b0, over), 0x1A7ADC));  /* 0x1A7AD4 */
                } else {
                    b0 = em_ee_add(b0, em_ee_div(push0, len));        /* 0x1A7AE8, 0x1A7AEC */
                    const float push8 = em_ee_mul(d8, over);          /* 0x1A7AF0 */
                    TRY(wr_f(p, inner + 0xB0, b0, 0x1A7AF4));
                    const float step8 = em_ee_div(push8, len);        /* 0x1A7AF8 */
                    TRY(rd_f(p, inner + 0xB8, &b8, 0x1A7AFC));
                    TRY(wr_f(p, inner + 0xB8, em_ee_add(b8, step8), 0x1A7B04));  /* 0x1A7B00 */
                }
            }
        }
        g->s3B88 = (int16_t)(g->s3B88 - 1);                           /* 0x1A7B18..0x1A7B28 */
    }
    return 0;
}

/* ---- 001A8660 ---------------------------------------------------------------- */

int em_coll_list_001A8660(P *p, uint32_t a0, uint32_t a1)
{
    if (!ready_8660(p)) return p ? fail(p, 0x1A8660) : -1;
    EmCollListGlobals *g = p->globals;
    float a0x, a0z, a1x, a1z;
    TRY(rd_f(p, a0 + 0xA0, &a0x, 0x1A8670));
    TRY(rd_f(p, a1 + 0xB0, &a1x, 0x1A8674));
    TRY(rd_f(p, a0 + 0xA8, &a0z, 0x1A8678));
    TRY(rd_f(p, a1 + 0xB8, &a1z, 0x1A867C));
    const float dx = em_ee_sub(a0x, a1x);                             /* 0x1A8688 */
    const float dz = em_ee_sub(a0z, a1z);                             /* 0x1A868C */
    float dist;
    uint32_t fault = 0;
    if (em_sdk_math_original_0011E748(p->math->tables, &p->math->world, &p->math->workers,
                                      em_ee_madd(em_ee_mula(dx, dx), dz, dz), &dist, &fault) < 0)
        return fail(p, fault ? fault : 0x1A8694);
    uint32_t va, vb;
    float ra, rb;
    TRY(rd_u32(p, a0 + 0x30, &va, 0x1A869C));
    TRY(rd_u32(p, a1 + 0x30, &vb, 0x1A86A0));
    TRY(rd_f(p, va, &ra, 0x1A86A4));
    TRY(rd_f(p, vb, &rb, 0x1A86A8));
    if (!em_ee_c_le(dist, em_ee_add(ra, rb))) return 0;               /* 0x1A86AC, 0x1A86B0 */
    float ha, a0y, a1y, hb;
    TRY(rd_f(p, va + 4, &ha, 0x1A86C0));
    TRY(rd_f(p, a0 + 0xA4, &a0y, 0x1A86C4));
    TRY(rd_f(p, a1 + 0xB4, &a1y, 0x1A86C8));
    const float half_a = em_ee_div(ha, 2.0f);                         /* 0x1A86DC */
    float gap = em_ee_sub(em_ee_add(a0y, half_a), a1y);               /* 0x1A86E0, 0x1A86E4 */
    if (em_ee_c_lt(gap, 0.0f)) gap = em_ee_neg(gap);                  /* 0x1A86E8, 0x1A86F8 */
    TRY(rd_f(p, vb + 4, &hb, 0x1A86FC));
    const float reach = em_ee_add(half_a, em_ee_div(hb, 2.0f));       /* 0x1A870C, 0x1A8714 */
    if (!em_ee_c_le(gap, reach)) return 0;                            /* 0x1A8718 */
    uint32_t fn;
    TRY(rd_u32(p, a1 + 0x34, &fn, 0x1A8728));
    if (p->workers.behaviour(p->workers.context, p, fn, a1, a0, a0 + 0xB0) < 0)   /* 0x1A8734 */
        return fail(p, 0x1A8734);
    uint8_t state;
    TRY(rd_u8(p, a0, &state, 0x1A873C));
    if (state == 1) {                                                 /* 0x1A8744 */
        uint8_t param;
        TRY(rd_u8(p, a1 + 0xD, &param, 0x1A874C));
        if (param == 0xB) {                                           /* 0x1A8754 */
            int r;
            if (p->workers.w_0021BD10(p->workers.context, p, &r) < 0) return fail(p, 0x1A875C);
            if (r) TRY(wr_u8(p, a0 + 0xF, 2, 0x1A8770));              /* 0x1A8764 */
        }
        const uint32_t table = g->d81070A == 0 ? 0 : 0x40;            /* 0x1A877C: D_0024A740 / D_0024A780 */
        TRY(rd_u8(p, a1 + 0xD, &param, 0x1A8798));
        const uint32_t at = table + 4u * param;
        if ((uint64_t)at + 4 > p->data->d24A740_size) return fail(p, 0x1A87C0);
        uint32_t word;
        memcpy(&word, p->data->d24A740 + at, 4);
        if (param == 3 || param == 4) TRY(wr_f(p, a0 + 0x22C, em_ee_float(word), 0x1A87C8));
        else TRY(wr_f(p, a0 + 0x224, em_ee_float(word), 0x1A87DC));
        TRY(wr_u8(p, a0, 3, 0x1A87E4));                               /* 0x1A87E4 */
        float from[4], to[4], d[4] = { 0 };
        for (int k = 0; k < 4; ++k) {
            TRY(rd_f(p, a0 + 0xA0 + 4u * (unsigned)k, &from[k], 0x1A87F4));
            TRY(rd_f(p, a1 + 0xB0 + 4u * (unsigned)k, &to[k], 0x1A87F4));
        }
        {   /* 001028D0(D_700038A0, a0 + 0xA0, a1 + 0xB0): four lanes. */
            uint32_t s[4], t[4], r[4];
            memcpy(s, from, sizeof s);
            memcpy(t, to, sizeof t);
            memcpy(r, g->s38A0, sizeof r);
            if (em_vu_vec_bits(EM_VU_SUB, 0xF, EM_VU_NO_BC, s, t, 0, NULL, r) != EM_EE_FLOAT_OK)
                return fail(p, 0x1A87F4);
            memcpy(g->s38A0, r, sizeof r);
        }
        g->s38A0[3] = 0x3F800000u;                                    /* 0x1A8804: 0x700038AC = 1.0 */
        memcpy(d, g->s38A0, sizeof d);
        float n[4];
        if (p->workers.normalize(p->workers.context, n, d) < 0) return fail(p, 0x1A8810);
        for (int k = 0; k < 4; ++k) TRY(wr_f(p, a0 + 0x70 + 4u * (unsigned)k, n[k], 0x1A8810));
    }
    g->s3B86 = 0;                                                     /* 0x1A881C */
    return 0;
}

/* ---- 001A8BE0 ---------------------------------------------------------------- */

int em_coll_list_001A8BE0(P *p, uint32_t player)
{
    if (!ready_8660(p) || !p->workers.w_001A8840 || !p->workers.w_001A8970)
        return p ? fail(p, 0x1A8BE0) : -1;
    EmCollListGlobals *g = p->globals;
    if (!(g->d28A9A0 == 0 && g->s3B8D == 0)) return 0;
    uint32_t q = g->d275BA0;
    g->s3B86 = g->d275BA8;
    while (g->s3B86 != 0) {
        uint32_t e;
        uint8_t flags, type;
        TRY(rd_u32(p, q, &e, 0x1A8BE0));
        g->s3B86 = (int16_t)(g->s3B86 - 1);
        q += 4;
        TRY(rd_u8(p, e, &flags, 0x1A8BE0));
        if (!(flags & 1)) continue;
        TRY(rd_u8(p, e + 3, &type, 0x1A8BE0));
        switch (type) {
        case 1: TRY(em_coll_list_001A8660(p, player, e)); break;          /* 0x1A8C80 */
        case 3: TRY(call_pair(p, p->workers.w_001A8840, player, e, 0x1A8C94)); break;
        case 5: TRY(call_pair(p, p->workers.w_001A8970, player, e, 0x1A8CA8)); break;
        default: break;
        }
    }
    return 0;
}

/* ---- 001A9000 ---------------------------------------------------------------- */

int em_coll_list_001A9000(P *p)
{
    if (!ready(p) || !p->workers.w_001A8E80 || !p->workers.w_001A8F40) return p ? fail(p, 0x1A9000) : -1;
    EmCollListGlobals *g = p->globals;
    int16_t n = g->d275BA8;
    g->s3B86 = n;
    if (n == 0) return 0;
    if (g->d275B88 == 0) return 0;
    uint32_t outer = g->d275BA0;
    while (g->s3B86 != 0) {
        uint32_t e;
        uint8_t type, param, status;
        n = g->s3B86;
        TRY(rd_u32(p, outer, &e, 0x1A9000));
        g->s3B86 = (int16_t)(n - 1);
        outer += 4;
        TRY(rd_u8(p, e + 3, &type, 0x1A9000));
        if (type != 5) continue;
        TRY(rd_u8(p, e + 0xD, &param, 0x1A9000));
        if (param == 0xB) continue;
        TRY(rd_u8(p, e, &status, 0x1A9000));
        if (status != 1) continue;
        uint32_t inner = g->d275B80;
        g->s3B88 = g->d275B88;
        while (g->s3B88 != 0) {
            uint32_t o;
            uint8_t ostatus, t;
            n = g->s3B88;
            TRY(rd_u32(p, inner, &o, 0x1A9000));
            g->s3B88 = (int16_t)(n - 1);
            inner += 4;
            TRY(rd_u8(p, o, &ostatus, 0x1A9000));
            if (ostatus != 1) continue;
            TRY(rd_u8(p, o + 3, &t, 0x1A9000));
            switch (t) {
            case 0xA: case 0xC: case 0x18: case 0x2A:
                TRY(call_pair(p, p->workers.w_001A8F40, e, o, 0x1A9138));
                break;
            case 0x1C: case 0x50: case 0x1F:
                /* Skipped only when D_00810700 == 0 and D_00810702 == 5. */
                if (g->d810700 == 0 && g->d810702 == 5) break;
                TRY(call_pair(p, p->workers.w_001A8E80, e, o, 0x1A9174));
                break;
            case 0x6: case 0x1E:
                TRY(call_pair(p, p->workers.w_001A8E80, e, o, 0x1A9174));
                break;
            default:
                break;
            }
        }
    }
    return 0;
}

/* ---- 001A97B0 ---------------------------------------------------------------- */

int em_coll_list_001A97B0(P *p)
{
    if (!ready(p) || !p->workers.w_001A9360 || !p->workers.w_001A96F0 || !p->workers.w_001A9480)
        return p ? fail(p, 0x1A97B0) : -1;
    EmCollListGlobals *g = p->globals;
    const int16_t n = g->d275BA8;
    g->s3B86 = n;
    if (!(n != 0 && g->d275B98 != 0)) return 0;
    uint32_t pa = g->d275BA0;
    while (g->s3B86 != 0) {
        uint32_t a;
        uint8_t status, type;
        int hit = 0;
        TRY(rd_u32(p, pa, &a, 0x1A97B0));
        g->s3B86 = (int16_t)(g->s3B86 - 1);
        pa += 4;
        TRY(rd_u8(p, a, &status, 0x1A97B0));
        if (status != 1) continue;
        TRY(rd_u8(p, a + 3, &type, 0x1A97B0));
        switch (type) {
        case 3: {
            uint8_t param;
            int16_t link;
            TRY(rd_u8(p, a + 0xD, &param, 0x1A97B0));
            if (param == 0) {
                TRY(rd_s16(p, a + 0x56, &link, 0x1A97B0));
                if (link != 0) hit = 1;
            }
            break;
        }
        case 5:
            hit = 1;
            break;
        case 6: {
            uint8_t param;
            TRY(rd_u8(p, a + 0xD, &param, 0x1A97B0));
            if (param == 2) hit = 1;
            break;
        }
        default:
            break;
        }
        if (!hit) continue;
        uint32_t pb = g->d275B90;
        g->s3B88 = g->d275B98;
        while (g->s3B88 != 0) {
            uint32_t b;
            uint8_t bstatus, bcls, btype;
            TRY(rd_u32(p, pb, &b, 0x1A97B0));
            g->s3B88 = (int16_t)(g->s3B88 - 1);
            pb += 4;
            TRY(rd_u8(p, b, &bstatus, 0x1A97B0));
            if (bstatus != 1) continue;
            TRY(rd_u8(p, b + 2, &bcls, 0x1A97B0));
            if ((bcls & 0x1F) == 0xA) continue;
            TRY(rd_u8(p, b + 3, &btype, 0x1A97B0));
            int interact = 0;
            switch (btype) {
            case 1: {
                uint8_t bparam;
                TRY(rd_u8(p, b + 0xD, &bparam, 0x1A97B0));
                interact = 1;
                if (bparam == 3) {
                    uint8_t b5;
                    float y;
                    TRY(rd_u8(p, b + 5, &b5, 0x1A97B0));
                    if (b5 == 9) { interact = 0; break; }
                    TRY(rd_f(p, 0x00810354u, &y, 0x1A97B0));      /* D_00810354: the player's +0xA4 */
                    if (em_ee_c_lt(y, 50.0f)) interact = 0;
                }
                break;
            }
            case 0: case 2: case 3: case 4: case 5: case 6: case 7:
            case 9: case 10: case 11: case 16: case 17: case 18: case 19:
                interact = 1;
                break;
            default:                                              /* 8, 12..15 and 20 on: skipped */
                break;
            }
            if (!interact) continue;
            uint8_t atype;
            TRY(rd_u8(p, a + 3, &atype, 0x1A9958));               /* re-read at the call */
            if (atype == 5) TRY(call_pair(p, p->workers.w_001A9360, a, b, 0x1A9968));
            else if (atype == 3) TRY(call_pair(p, p->workers.w_001A96F0, a, b, 0x1A9984));
            else TRY(call_pair(p, p->workers.w_001A9480, a, b, 0x1A9998));
        }
    }
    return 0;
}

/* ---- 001A9B10 ---------------------------------------------------------------- */

int em_coll_list_001A9B10(P *p)
{
    if (!ready(p) || !p->workers.w_001A99E0) return p ? fail(p, 0x1A9B10) : -1;
    EmCollListGlobals *g = p->globals;
    const int16_t n = g->d275B98;
    g->s3B86 = n;
    if (n == 0) return 0;
    if (g->d275B88 == 0) return 0;
    uint32_t q = g->d275B90;
    while (g->s3B86 != 0) {
        uint32_t e, word;
        uint8_t type;
        TRY(rd_u32(p, q, &e, 0x1A9B10));
        q += 4;
        g->s3B86 = (int16_t)(g->s3B86 - 1);
        TRY(rd_u8(p, e + 3, &type, 0x1A9B10));
        if (type != 0) continue;
        TRY(rd_u32(p, e + 0x2D4, &word, 0x1A9B10));
        const int32_t t = (int32_t)word >> 8;
        if (!(t == 1 || t == 2 || t == 3)) continue;
        g->s3B88 = g->d275B88;
        uint32_t pp = g->d275B80;
        while (g->s3B88 != 0) {
            uint32_t f;
            uint8_t ftype;
            float f38;
            TRY(rd_u32(p, pp, &f, 0x1A9B10));
            pp += 4;
            g->s3B88 = (int16_t)(g->s3B88 - 1);
            TRY(rd_u8(p, f + 3, &ftype, 0x1A9B10));
            if (ftype != 7) continue;
            TRY(rd_f(p, f + 0x38, &f38, 0x1A9BE4));
            if (em_ee_c_eq(0.0f, f38)) continue;                  /* 0x1A9BF0: the +0x38 float is 0 */
            TRY(call_pair(p, p->workers.w_001A99E0, e, f, 0x1A9C00));   /* a1 still holds f */
        }
    }
    return 0;
}

/* ---- 001AAD00's hooks --------------------------------------------------------- */

int em_coll_list_passes_bound(const EmCollListPasses *p)
{
    const EmCollListWorkers *w = p ? &p->workers : NULL;
    return p && bound_8660(p) && w->w_001A8840 && w->w_001A8970 && w->w_001A8CE0 && w->w_001A8E80 &&
           w->w_001A8F40 && w->w_001A9360 && w->w_001A96F0 && w->w_001A9480 && w->w_001A99E0 &&
           w->w_001A9C40 && w->w_001A9E00 && w->w_001AA000;
}

int em_coll_list_passes_001AAD00_hooks(EmCollListPasses *p, uint32_t player)
{
    if (!em_coll_list_passes_bound(p) || !p->globals) return p ? fail(p, 0x1AAD00) : -1;
    TRY(em_coll_list_001A9D20(p));
    TRY(em_coll_list_001A8DA0(p));
    TRY(em_coll_list_001A9F60(p, player));
    TRY(em_coll_list_001AA140(p));
    TRY(em_coll_list_001A7870(p));
    TRY(em_coll_list_001A8BE0(p, player));
    TRY(em_coll_list_001A9000(p));
    TRY(em_coll_list_001A97B0(p));
    TRY(em_coll_list_001A9B10(p));
    return 0;
}

/* ---- Worker adapters -------------------------------------------------------- */

/* The unported bindings return -1 and leave passes->fault alone: the
 * calling pass records the original call-site address of the faulting
 * call (call_pair, 0x1AA23C, 0x1A875C, 0x1A8734). */
int em_coll_list_passes_unported(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b)
{
    (void)context; (void)passes; (void)a; (void)b;
    return -1;
}

int em_coll_list_passes_unported_001AA000(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b,
                                          uint32_t a1f0, uint32_t b1f0)
{
    (void)context; (void)passes; (void)a; (void)b; (void)a1f0; (void)b1f0;
    return -1;
}

int em_coll_list_passes_unported_0021BD10(void *context, EmCollListPasses *passes, int *result)
{
    (void)context; (void)passes; (void)result;
    return -1;
}

int em_coll_list_passes_unported_behaviour(void *context, EmCollListPasses *passes, uint32_t fn,
                                           uint32_t entry, uint32_t player, uint32_t player_b0)
{
    (void)context; (void)passes; (void)fn; (void)entry; (void)player; (void)player_b0;
    return -1;
}

int em_coll_list_passes_normalize(void *context, float out[4], const float in[4])
{
    (void)context;
    if (!out || !in) return -1;
    em_effect_original_00102760(out, in);
    return 0;
}
