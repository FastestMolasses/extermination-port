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
#include "game/em_sdk_math_original.h"
#include "game/em_sdk_soft_float.h"
#include "game/em_sdk_vu0.h"

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

/* ---- 001A9C40 ---------------------------------------------------------------- */

/* 001A9D20's pair callee (outer class 1, inner class 2): unless the inner
 * record's +0 has bit 1, the distance of the two +0xB0 points (0011E748
 * over dx dx + dy dy + dz dz, the sum in the accumulator) against the inner
 * record's radius by its +3 type (the jump table D_0026DB30: types 0 and 4
 * 15.0, types 1, 5, 6 and 7 20.0, every other type none); inside a radius
 * the inner record's +0x0A gets bit 0. */
int em_coll_list_001A9C40(P *p, uint32_t self, uint32_t other)
{
    if (!p || !p->memory.bytes || !p->math || !p->math->tables) return p ? fail(p, 0x1A9C40) : -1;
    uint8_t status;
    TRY(rd_u8(p, other, &status, 0x1A9C4C));
    if (status & 2) return 0;                                         /* 0x1A9C54 */
    float sy, oy, sx, ox, sz, oz;
    TRY(rd_f(p, self + 0xB4, &sy, 0x1A9C5C));
    TRY(rd_f(p, other + 0xB4, &oy, 0x1A9C60));
    TRY(rd_f(p, self + 0xB0, &sx, 0x1A9C64));
    TRY(rd_f(p, other + 0xB0, &ox, 0x1A9C68));
    TRY(rd_f(p, self + 0xB8, &sz, 0x1A9C6C));
    TRY(rd_f(p, other + 0xB8, &oz, 0x1A9C70));
    const float dy = em_ee_sub(sy, oy);                               /* 0x1A9C74 */
    const float dz = em_ee_sub(sz, oz);                               /* 0x1A9C78 */
    const float dx = em_ee_sub(sx, ox);                               /* 0x1A9C7C */
    const float acc = em_ee_adda(em_ee_mul(dx, dx), em_ee_mul(dy, dy)); /* 0x1A9C80..0x1A9C88 */
    float dist;
    uint32_t fault = 0;
    if (em_sdk_math_original_0011E748(p->math->tables, &p->math->world, &p->math->workers,
                                      em_ee_madd(acc, dz, dz), &dist, &fault) < 0)   /* 0x1A9C8C, 0x1A9C90 */
        return fail(p, fault ? fault : 0x1A9C8C);
    uint8_t type;
    TRY(rd_u8(p, other + 3, &type, 0x1A9C94));
    float radius = 0.0f;                                              /* 0x1A9C98 */
    switch (type) {                                                   /* 0x1A9C9C..0x1A9CBC */
    case 0: case 4: radius = 15.0f; break;                            /* 0x1A9CC4 */
    case 1: case 5: case 6: case 7: radius = 20.0f; break;            /* 0x1A9CD4 */
    default: break;
    }
    if (em_ee_c_eq(0.0f, radius)) return 0;                           /* 0x1A9CE4 */
    if (!em_ee_c_le(dist, radius)) return 0;                          /* 0x1A9CF4 */
    uint8_t flags;
    TRY(rd_u8(p, other + 0xA, &flags, 0x1A9D04));
    TRY(wr_u8(p, other + 0xA, (uint8_t)(flags | 1), 0x1A9D0C));
    return 0;
}

int em_coll_list_passes_001A9C40(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b)
{
    (void)context;
    return em_coll_list_001A9C40(passes, a, b);
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

/* ---- 001A8CE0 ---------------------------------------------------------------- */

/* 001A8DA0's pair callee (outer class 1, inner class-0xD type 3): the outer
 * record's +0xB0 point inside the inner record's box, whose half sizes are
 * the three words at the inner record's +0x30 pointer (re-read for each
 * axis): |dx| <= w[0], then |dz| <= w[2], then |dy| <= 0.5 + w[1], each
 * |d| through 0011DF78. Inside: the outer +0x0A = 1 and 0x70003B86 = 0
 * (001A8DA0's inner walk ends). */
int em_coll_list_001A8CE0(P *p, uint32_t self, uint32_t other)
{
    if (!ready(p)) return p ? fail(p, 0x1A8CE0) : -1;
    float a, b, half;
    uint32_t box;
    TRY(rd_f(p, self + 0xB0, &a, 0x1A8CF0));
    TRY(rd_f(p, other + 0xB0, &b, 0x1A8CF4));
    float d = em_sdk_math_original_0011DF78(em_ee_sub(a, b));          /* 0x1A8D00, 0x1A8D04 */
    TRY(rd_u32(p, other + 0x30, &box, 0x1A8D08));
    TRY(rd_f(p, box, &half, 0x1A8D0C));
    if (!em_ee_c_le(d, half)) return 0;                               /* 0x1A8D10, 0x1A8D18 */
    TRY(rd_f(p, self + 0xB8, &a, 0x1A8D20));
    TRY(rd_f(p, other + 0xB8, &b, 0x1A8D24));
    d = em_sdk_math_original_0011DF78(em_ee_sub(a, b));               /* 0x1A8D28, 0x1A8D2C */
    TRY(rd_u32(p, other + 0x30, &box, 0x1A8D30));
    TRY(rd_f(p, box + 8, &half, 0x1A8D34));
    if (!em_ee_c_le(d, half)) return 0;                               /* 0x1A8D38, 0x1A8D40 */
    TRY(rd_f(p, self + 0xB4, &a, 0x1A8D48));
    TRY(rd_f(p, other + 0xB4, &b, 0x1A8D4C));
    d = em_sdk_math_original_0011DF78(em_ee_sub(a, b));               /* 0x1A8D50, 0x1A8D54 */
    TRY(rd_u32(p, other + 0x30, &box, 0x1A8D58));
    TRY(rd_f(p, box + 4, &half, 0x1A8D64));
    if (!em_ee_c_le(d, em_ee_add(0.5f, half))) return 0;              /* 0x1A8D68..0x1A8D74 */
    TRY(wr_u8(p, self + 0xA, 1, 0x1A8D80));
    p->globals->s3B86 = 0;                                            /* 0x1A8D88 */
    return 0;
}

int em_coll_list_passes_001A8CE0(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b)
{
    (void)context;
    return em_coll_list_001A8CE0(passes, a, b);
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

/* ---- 001A99E0 ---------------------------------------------------------------- */

/* 001A9B10's pair callee (outer class-2 entry e, inner class-4 type-7 entry
 * f with a nonzero +0x38 spin): e rides f's rotation. dx, dz are e's +0xB0 /
 * +0xB8 minus f's; when 0011E748 (sqrtf) of dx dx + dz dz (the sum in the
 * accumulator) is <= 67.0 + the first float of e's +0x30 record, and the
 * SDK fabsf 0011DF78 of e.y - f.y is <= 8.0, e's x/z are rotated about f
 * by the spin angle a (f's +0x38, re-read for every call):
 *   e.x = f.x + (dx cos(a) + dz sin(a)),  e.z = f.z + (dx -sin(a) + dz cos(a))
 * with cosf 0011DE90 / sinf 0011E2A8 evaluated once per term in the
 * original's order (cos, sin; sin, cos) and f's x/z re-read after the
 * calls; then e's +0xC4 heading = 001B1470(heading + a) and the inner walk
 * counter 0x70003B88 = 0 (001A9B10's inner walk ends). */
int em_coll_list_001A99E0(P *p, uint32_t e, uint32_t f)
{
    if (!ready(p) || !p->math || !p->math->tables) return p ? fail(p, 0x1A99E0) : -1;
    const EmSdkMathContext *m = p->math;
    uint32_t fault = 0;
    float ex, fx, ez, fz;
    TRY(rd_f(p, e + 0xB0, &ex, 0x1A99FC));
    TRY(rd_f(p, f + 0xB0, &fx, 0x1A9A00));
    TRY(rd_f(p, e + 0xB8, &ez, 0x1A9A04));
    TRY(rd_f(p, f + 0xB8, &fz, 0x1A9A08));
    const float dx = em_ee_sub(ex, fx);                               /* 0x1A9A14 */
    const float dz = em_ee_sub(ez, fz);                               /* 0x1A9A18 */
    const float acc = em_ee_mula(dx, dx);                             /* 0x1A9A1C */
    float dist;
    if (em_sdk_math_original_0011E748(m->tables, &m->world, &m->workers, em_ee_madd(acc, dz, dz), &dist,
                                      &fault) < 0)                     /* 0x1A9A20, 0x1A9A24 */
        return fail(p, fault ? fault : 0x1A9A20);
    uint32_t range;
    float radius;
    TRY(rd_u32(p, e + 0x30, &range, 0x1A9A28));
    TRY(rd_f(p, range, &radius, 0x1A9A34));
    if (!em_ee_c_le(dist, em_ee_add(67.0f, radius))) return 0;        /* 0x1A9A38..0x1A9A44 */
    float ey, fy;
    TRY(rd_f(p, e + 0xB4, &ey, 0x1A9A4C));
    TRY(rd_f(p, f + 0xB4, &fy, 0x1A9A50));
    const float ady = em_sdk_math_original_0011DF78(em_ee_sub(ey, fy)); /* 0x1A9A54, 0x1A9A58 */
    if (!em_ee_c_le(ady, 8.0f)) return 0;                             /* 0x1A9A68, 0x1A9A70 */

    float spin, c, s, fx2, fz2, heading, wrapped;
    TRY(rd_f(p, f + 0x38, &spin, 0x1A9A7C));
    if (em_sdk_math_original_0011DE90(m->tables, spin, &c, &fault) < 0)   /* 0x1A9A78 */
        return fail(p, fault ? fault : 0x1A9A78);
    const float xc = em_ee_mul(dx, c);                                /* 0x1A9A88 */
    TRY(rd_f(p, f + 0x38, &spin, 0x1A9A80));
    if (em_sdk_math_original_0011E2A8(m->tables, spin, &s, &fault) < 0)   /* 0x1A9A84 */
        return fail(p, fault ? fault : 0x1A9A84);
    const float xs = em_ee_mul(dz, s);                                /* 0x1A9A8C */
    TRY(rd_f(p, f + 0xB0, &fx2, 0x1A9A90));
    TRY(wr_f(p, e + 0xB0, em_ee_add(fx2, em_ee_add(xc, xs)), 0x1A9A9C)); /* 0x1A9A94, 0x1A9A98 */

    TRY(rd_f(p, f + 0x38, &spin, 0x1A9AA4));
    if (em_sdk_math_original_0011E2A8(m->tables, spin, &s, &fault) < 0)   /* 0x1A9AA0 */
        return fail(p, fault ? fault : 0x1A9AA0);
    const float zs = em_ee_mul(dx, em_ee_neg(s));                     /* 0x1A9AA8, 0x1A9AB4 */
    TRY(rd_f(p, f + 0x38, &spin, 0x1A9AAC));
    if (em_sdk_math_original_0011DE90(m->tables, spin, &c, &fault) < 0)   /* 0x1A9AB0 */
        return fail(p, fault ? fault : 0x1A9AB0);
    const float zc = em_ee_mul(dz, c);                                /* 0x1A9AB8 */
    TRY(rd_f(p, f + 0xB8, &fz2, 0x1A9ABC));
    TRY(wr_f(p, e + 0xB8, em_ee_add(fz2, em_ee_add(zs, zc)), 0x1A9AC8)); /* 0x1A9AC0, 0x1A9AC4 */

    TRY(rd_f(p, f + 0x38, &spin, 0x1A9ACC));
    TRY(rd_f(p, e + 0xC4, &heading, 0x1A9AD0));
    if (em_effect_original_001B1470(em_ee_add(heading, spin), &wrapped) < 0)   /* 0x1A9AD4, 0x1A9AD8 */
        return fail(p, 0x1A9AD4);                                     /* a wrap that cannot end */
    TRY(wr_f(p, e + 0xC4, wrapped, 0x1A9ADC));
    p->globals->s3B88 = 0;                                            /* 0x1A9AE4 */
    return 0;
}

int em_coll_list_passes_001A99E0(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b)
{
    (void)context;
    return em_coll_list_001A99E0(passes, a, b);
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

/* ---- The pair callees of 001A8BE0, 001A9000, 001A97B0, 001A9F60, 001AA140
 * and 001A8660's 0021BD10 (port-stub translations, 2026-10-28) ------------
 *
 * Each follows its byte-matched decomp C (src/func_<address>.c) statement
 * for statement. Shared pieces:
 *   - quadword loads and stores (the SDK 001028D0 / 00102948 / 00102760 and
 *     001026A0 move whole quadwords) ignore the low four address bits, as
 *     the EE does;
 *   - 0x700038A0 is EmCollListGlobals.s38A0, 0x700038B0 s38B0 (00183C40's
 *     output), 0x70003A20 / 0x70003A24 s3A20 / s3A24 (raw bits);
 *   - 001028D0 is one four-lane VU0 subtraction and 00102738 the xyz dot
 *     (em_sdk_vu0.h); 00102760 is the bound `normalize` worker. */

static int wr_u32(P *p, uint32_t a, uint32_t v, uint32_t where)
{
    uint8_t *b = span(p, a, 4, where);
    if (!b) return -1;
    memcpy(b, &v, 4);
    return 0;
}

static int rd_q(P *p, uint32_t a, uint32_t q[4], uint32_t where)
{
    const uint8_t *b = span(p, a & ~0xFu, 16, where);
    if (!b) return -1;
    memcpy(q, b, 16);
    return 0;
}

static int wr_q(P *p, uint32_t a, const uint32_t q[4], uint32_t where)
{
    uint8_t *b = span(p, a & ~0xFu, 16, where);
    if (!b) return -1;
    memcpy(b, q, 16);
    return 0;
}

static int math_ready(const P *p)
{
    return ready(p) && p->math && p->math->tables;
}

/* 0011E748 (sqrtf). */
static int sqrt_1E748(P *p, float x, float *out, uint32_t site)
{
    uint32_t fault = 0;
    if (em_sdk_math_original_0011E748(p->math->tables, &p->math->world, &p->math->workers, x, out, &fault) < 0)
        return fail(p, fault ? fault : site);
    return 0;
}

/* 001028D0(0x700038A0, a, b): the four lanes a - b. */
static int vsub_38A0(P *p, const uint32_t a[4], const uint32_t b[4], uint32_t site)
{
    uint32_t r[4];
    memcpy(r, p->globals->s38A0, sizeof r);
    if (em_vu_vec_bits(EM_VU_SUB, 0xF, EM_VU_NO_BC, a, b, 0, NULL, r) != EM_EE_FLOAT_OK) return fail(p, site);
    memcpy(p->globals->s38A0, r, sizeof r);
    return 0;
}

/* 00102738(0x700038A0, 0x700038A0): the xyz dot of the scratch vector. */
static int dot_38A0(P *p, uint32_t *out, uint32_t site)
{
    if (em_sdk_vu0_00102738(out, p->globals->s38A0, p->globals->s38A0) != EM_EE_FLOAT_OK) return fail(p, site);
    return 0;
}

/* 00102760(dst, 0x700038A0) through the bound worker; dst gets the whole
 * quadword. */
static int normalize_38A0(P *p, uint32_t dst, uint32_t site)
{
    float in[4], out[4];
    uint32_t q[4];
    memcpy(in, p->globals->s38A0, sizeof in);
    if (!p->workers.normalize || p->workers.normalize(p->workers.context, out, in) < 0) return fail(p, site);
    memcpy(q, out, sizeof q);
    return wr_q(p, dst, q, site);
}

/* 0011DF78 over a - b. */
static float abs_diff(float a, float b)
{
    return em_sdk_math_original_0011DF78(em_ee_sub(a, b));
}

/* 00183C40(p, 0x700038A0 + 0x10): the anchor point of record p into
 * 0x700038B0. Unless p's +2 byte & 0x1F is 2, its +0xB0 / +0xB4 / +0xB8
 * words. For class 2 by its +3 byte: the +0xC0 / +0xC4 / +0xC8 words of the
 * node named by the word at +0x118 (types 0, 3, 4, 7, 8), +0x11C (1, 2),
 * +0x130 (5), +0x124 (6), +0x120 (10), +0x148 (11), re-read for each word;
 * type 16 the +0xB0 point with y = 6.0 + +0xB4; type 18 (0, 6.0, 0, 1.0)
 * transformed by the matrix at (+0x110 word) + 0x90 (001026A0, all four
 * words); every other type the +0xB0 point. Only type 18 writes the w
 * word. */
static int anchor_183C40(P *p, uint32_t rec)
{
    uint32_t *out = p->globals->s38B0;
    uint8_t cls, type;
    TRY(rd_u8(p, rec + 2, &cls, 0x183C40));
    if ((cls & 0x1F) == 2) {
        TRY(rd_u8(p, rec + 3, &type, 0x183C80));
        uint32_t slot = 0;
        switch (type) {
        case 0: case 3: case 4: case 7: case 8: slot = 0x118; break;
        case 1: case 2: slot = 0x11C; break;
        case 5: slot = 0x130; break;
        case 6: slot = 0x124; break;
        case 10: slot = 0x120; break;
        case 11: slot = 0x148; break;
        case 16: {
            float y;
            TRY(rd_u32(p, rec + 0xB0, &out[0], 0x183C40));
            TRY(rd_f(p, rec + 0xB4, &y, 0x183C40));
            out[1] = em_ee_bits(em_ee_add(6.0f, y));
            TRY(rd_u32(p, rec + 0xB8, &out[2], 0x183C40));
            return 0;
        }
        case 18: {
            uint32_t base, m[16];
            out[0] = 0;
            out[1] = 0x40C00000u;                                       /* 6.0 */
            out[2] = 0;
            out[3] = 0x3F800000u;                                       /* 1.0 */
            TRY(rd_u32(p, rec + 0x110, &base, 0x183C40));
            for (unsigned k = 0; k < 4; ++k) TRY(rd_q(p, base + 0x90 + 0x10 * k, m + 4 * k, 0x183EB0));
            float fm[16], v[4], r[4];
            memcpy(fm, m, sizeof fm);
            memcpy(v, out, sizeof v);
            em_effect_original_001026A0(r, fm, v);                      /* 0x183EB0 */
            memcpy(out, r, sizeof r);
            return 0;
        }
        default: break;
        }
        if (slot) {
            for (unsigned k = 0; k < 3; ++k) {
                uint32_t node;
                TRY(rd_u32(p, rec + slot, &node, 0x183C40));
                TRY(rd_u32(p, node + 0xC0 + 4 * k, &out[k], 0x183C40));
            }
            return 0;
        }
    }
    for (unsigned k = 0; k < 3; ++k) TRY(rd_u32(p, rec + 0xB0 + 4 * k, &out[k], 0x183C40));
    return 0;
}

/* 00187EC0(a, b): D_008102BB = 1, D_008104EA = a, D_008105CE = b (bytes of
 * the player record). */
static int notice_187EC0(P *p, uint8_t a, uint8_t b)
{
    TRY(wr_u8(p, 0x008102BBu, 1, 0x187EC0));
    TRY(wr_u8(p, 0x008104EAu, a, 0x187EC0));
    return wr_u8(p, 0x008105CEu, b, 0x187EC0);
}

/* ---- 001A8840 ---------------------------------------------------------------- */

/* 001A8BE0's type-3 callee (player a0, class-0xD entry a1): a0's +0xA0
 * point in a1's box (half sizes at a1's +0x30 record, re-read for each
 * axis): |dx| <= w[0], |dz| <= w[2], |dy| <= 1.5 + w[1] (0011DF78). Inside,
 * by a1's +0xD: 0 -> a1 +0x0A = 1, 00187EC0(6, a1 +0x56), and when a1's +0xB
 * is set, D_00810707 != 1 and a0's +0 is 1: a0 +0x22C = 5.0, a0 +0 = 3;
 * 1 -> 00187EC0(7, 0). Any contact zeroes 0x70003B86 (001A8BE0's walk
 * ends). */
int em_coll_list_001A8840(P *p, uint32_t a0, uint32_t a1)
{
    if (!ready(p)) return p ? fail(p, 0x1A8840) : -1;
    float x0, x1, w;
    uint32_t box;
    TRY(rd_f(p, a0 + 0xA0, &x0, 0x1A8840));
    TRY(rd_f(p, a1 + 0xB0, &x1, 0x1A8840));
    const float dx = abs_diff(x0, x1);                                /* 0x1A8860 */
    TRY(rd_u32(p, a1 + 0x30, &box, 0x1A8840));
    TRY(rd_f(p, box, &w, 0x1A8840));
    if (!em_ee_c_le(dx, w)) return 0;
    TRY(rd_f(p, a0 + 0xA8, &x0, 0x1A8840));
    TRY(rd_f(p, a1 + 0xB8, &x1, 0x1A8840));
    const float dz = abs_diff(x0, x1);                                /* 0x1A8888 */
    TRY(rd_u32(p, a1 + 0x30, &box, 0x1A8840));
    TRY(rd_f(p, box + 8, &w, 0x1A8840));
    if (!em_ee_c_le(dz, w)) return 0;
    TRY(rd_f(p, a0 + 0xA4, &x0, 0x1A8840));
    TRY(rd_f(p, a1 + 0xB4, &x1, 0x1A8840));
    const float dy = abs_diff(x0, x1);                                /* 0x1A88B0 */
    TRY(rd_u32(p, a1 + 0x30, &box, 0x1A8840));
    TRY(rd_f(p, box + 4, &w, 0x1A8840));
    if (!em_ee_c_le(dy, em_ee_add(1.5f, w))) return 0;
    uint8_t t;
    TRY(rd_u8(p, a1 + 0xD, &t, 0x1A8840));
    if (t == 0) {
        uint8_t link, armed, state;
        TRY(wr_u8(p, a1 + 0xA, 1, 0x1A8840));
        TRY(rd_u8(p, a1 + 0x56, &link, 0x1A8840));
        TRY(notice_187EC0(p, 6, link));                               /* 0x1A88F4 */
        TRY(rd_u8(p, a1 + 0xB, &armed, 0x1A8840));
        if (armed != 0 && p->globals->d810707 != 1) {
            TRY(rd_u8(p, a0, &state, 0x1A8840));
            if (state == 1) {
                TRY(wr_u32(p, a0 + 0x22C, 0x40A00000u, 0x1A8840));      /* 5.0 */
                TRY(wr_u8(p, a0, 3, 0x1A8840));
            }
        }
    } else if (t == 1) {
        TRY(notice_187EC0(p, 7, 0));                                  /* 0x1A894C */
    }
    p->globals->s3B86 = 0;
    return 0;
}

/* ---- 001A8970 ---------------------------------------------------------------- */

/* 001A8BE0's type-5 callee (player a0, class-0xD entry a1): the gates
 * |a0.x - a1.x| <= w1[0], |a0.z - a1.z| <= w1[2] and
 * |a0 +0xA4 + h - a1.y| <= h + w1[1] (h = w0[1] / 2; w0, w1 the records at
 * a0's and a1's +0x30), then a1's +0x34 behaviour(a1, a0, a0 + 0xB0). When
 * a1's +0xD is then 5: a0 +0xF = 0xA, a0 +0x224 = a0 +0x220, a0 +0 = 3 and
 * the direction normalize(a0 +0xA0 - a1 +0xB0, w = 1) into a0 +0x70.
 * Otherwise, with a0's +0 at 1 and a1's +0xD not 6 or 7: for +0xD 0xE,
 * 0021BD10 nonzero sets a0 +0xF = 2; the speed table D_0024A7C0 (D_0081070A
 * 0) or D_0024A800 by +0xD (re-read): for 9, 0xC and 0xD into +0x22C and
 * +0x224, else into +0x224; a0 +0 = 3 and the same direction. Both commits
 * zero 0x70003B86, and so does a0's +0 other than 1.
 *
 * With a0's +0 at 1 and a1's +0xD at 6 or 7 the original compares its
 * caller's f20 register (never written in 001A8970) with 0.8 times itself;
 * the port has no such register, so that path faults at 0x1A8AC4. The
 * speed tables are read from EmCollListData (D_0024A7C0 = D_0024A740 +
 * 0x80, D_0024A800 = + 0xC0); a read past it faults. */
int em_coll_list_001A8970(P *p, uint32_t a0, uint32_t a1)
{
    if (!ready(p) || !p->data || !p->data->d24A740 || !p->workers.behaviour || !p->workers.w_0021BD10 ||
        !p->workers.normalize)
        return p ? fail(p, 0x1A8970) : -1;
    EmCollListGlobals *g = p->globals;
    float x0, x1, w;
    uint32_t r0, r1;
    TRY(rd_f(p, a0 + 0xB0, &x0, 0x1A8970));
    TRY(rd_f(p, a1 + 0xB0, &x1, 0x1A8970));
    float d = abs_diff(x0, x1);                                       /* 0x1A8998 */
    TRY(rd_u32(p, a1 + 0x30, &r1, 0x1A8970));
    TRY(rd_f(p, r1, &w, 0x1A8970));
    if (!em_ee_c_le(d, w)) return 0;
    TRY(rd_f(p, a0 + 0xB8, &x0, 0x1A8970));
    TRY(rd_f(p, a1 + 0xB8, &x1, 0x1A8970));
    d = abs_diff(x0, x1);                                             /* 0x1A89C0 */
    TRY(rd_u32(p, a1 + 0x30, &r1, 0x1A8970));
    TRY(rd_f(p, r1 + 8, &w, 0x1A8970));
    if (!em_ee_c_le(d, w)) return 0;
    float h0, y0, y1, h1;
    TRY(rd_u32(p, a0 + 0x30, &r0, 0x1A8970));
    TRY(rd_f(p, r0 + 4, &h0, 0x1A8970));
    const float half = em_ee_div(h0, 2.0f);
    TRY(rd_f(p, a0 + 0xA4, &y0, 0x1A8970));
    TRY(rd_f(p, a1 + 0xB4, &y1, 0x1A8970));
    d = em_ee_sub(em_ee_add(y0, half), y1);
    TRY(rd_u32(p, a1 + 0x30, &r1, 0x1A8970));
    TRY(rd_f(p, r1 + 4, &h1, 0x1A8970));
    const float reach = em_ee_add(half, h1);
    if (!em_ee_c_le(em_sdk_math_original_0011DF78(d), reach)) return 0;   /* 0x1A8A08 */
    uint32_t fn;
    TRY(rd_u32(p, a1 + 0x34, &fn, 0x1A8970));
    if (p->workers.behaviour(p->workers.context, p, fn, a1, a0, a0 + 0xB0) < 0)   /* 0x1A8A2C */
        return fail(p, 0x1A8A2C);
    uint8_t react, state;
    TRY(rd_u8(p, a1 + 0xD, &react, 0x1A8A34));
    uint32_t from[4], to[4];
    if (react == 5) {
        uint32_t pose;
        TRY(wr_u8(p, a0 + 0xF, 0xA, 0x1A8A48));
        TRY(rd_u32(p, a0 + 0x220, &pose, 0x1A8A4C));
        TRY(wr_u32(p, a0 + 0x224, pose, 0x1A8A5C));
        TRY(wr_u8(p, a0, 3, 0x1A8A6C));
        TRY(rd_q(p, a0 + 0xA0, from, 0x1A8A68));
        TRY(rd_q(p, a1 + 0xB0, to, 0x1A8A68));
        TRY(vsub_38A0(p, from, to, 0x1A8A68));
        g->s38A0[3] = 0x3F800000u;                                    /* 0x700038AC = 1.0 */
        TRY(normalize_38A0(p, a0 + 0x70, 0x1A8A84));
        g->s3B86 = 0;                                                 /* 0x1A8BB8 */
        return 0;
    }
    TRY(rd_u8(p, a0, &state, 0x1A8A94));
    if (state != 1) {
        g->s3B86 = 0;
        return 0;
    }
    if ((uint8_t)(react - 6) < 2) return fail(p, 0x1A8AC4);          /* the caller's f20 */
    if (react == 0xE) {
        int r;
        if (p->workers.w_0021BD10(p->workers.context, p, &r) < 0) return fail(p, 0x1A8AE4);
        if (r != 0) TRY(wr_u8(p, a0 + 0xF, 2, 0x1A8970));
    }
    const uint32_t table = g->d81070A == 0 ? 0x80u : 0xC0u;           /* D_0024A7C0 / D_0024A800 */
    uint8_t pick, again;
    uint32_t word;
    TRY(rd_u8(p, a1 + 0xD, &pick, 0x1A8970));
    if (pick == 9 || (uint8_t)(pick - 0xC) < 2) {
        if ((uint64_t)table + 4u * pick + 4 > p->data->d24A740_size) return fail(p, 0x1A8970);
        memcpy(&word, p->data->d24A740 + table + 4u * pick, 4);
        TRY(wr_u32(p, a0 + 0x22C, word, 0x1A8970));
        TRY(rd_u8(p, a1 + 0xD, &again, 0x1A8970));                    /* re-read after the store */
        if ((uint64_t)table + 4u * again + 4 > p->data->d24A740_size) return fail(p, 0x1A8970);
        memcpy(&word, p->data->d24A740 + table + 4u * again, 4);
        TRY(wr_u32(p, a0 + 0x224, word, 0x1A8970));
    } else {
        if ((uint64_t)table + 4u * pick + 4 > p->data->d24A740_size) return fail(p, 0x1A8970);
        memcpy(&word, p->data->d24A740 + table + 4u * pick, 4);
        TRY(wr_u32(p, a0 + 0x224, word, 0x1A8970));
    }
    TRY(wr_u8(p, a0, 3, 0x1A8970));
    TRY(rd_q(p, a0 + 0xA0, from, 0x1A8B94));
    TRY(rd_q(p, a1 + 0xB0, to, 0x1A8B94));
    TRY(vsub_38A0(p, from, to, 0x1A8B94));
    g->s38A0[3] = 0x3F800000u;                                        /* 0x700038AC = 1.0 */
    TRY(normalize_38A0(p, a0 + 0x70, 0x1A8BB0));
    g->s3B86 = 0;                                                     /* 0x1A8BB8 */
    return 0;
}

/* ---- 001A8E80 ---------------------------------------------------------------- */

/* 001A9000's callee for class-4 types 6, 0x1C, 0x1E, 0x1F and 0x50 (outer a
 * class-0xD type 5, inner b): sqrt(dx dx + dz dz) of b - a (the sum in the
 * accumulator) <= 7.0 + a's radius (the first float of its +0x30 record),
 * and |(6.0 + b.y) - a.y| <= 8.0: b's +0x36 halfword = 0x14 and
 * 0x70003B88 = 0 (001A9000's inner walk ends). */
int em_coll_list_001A8E80(P *p, uint32_t a, uint32_t b)
{
    if (!math_ready(p)) return p ? fail(p, 0x1A8E80) : -1;
    float bx, ax, bz, az, by, ay, r, dist;
    uint32_t rec;
    TRY(rd_f(p, b + 0xB0, &bx, 0x1A8E80));
    TRY(rd_f(p, a + 0xB0, &ax, 0x1A8E80));
    TRY(rd_f(p, b + 0xB8, &bz, 0x1A8E80));
    TRY(rd_f(p, a + 0xB8, &az, 0x1A8E80));
    const float dx = em_ee_sub(bx, ax);
    const float dz = em_ee_sub(bz, az);
    TRY(sqrt_1E748(p, em_ee_madd(em_ee_mula(dx, dx), dz, dz), &dist, 0x1A8EB4));
    TRY(rd_u32(p, a + 0x30, &rec, 0x1A8E80));
    TRY(rd_f(p, rec, &r, 0x1A8E80));
    if (!em_ee_c_le(dist, em_ee_add(7.0f, r))) return 0;
    TRY(rd_f(p, b + 0xB4, &by, 0x1A8E80));
    TRY(rd_f(p, a + 0xB4, &ay, 0x1A8E80));
    const float dy = em_ee_sub(em_ee_add(6.0f, by), ay);
    if (!em_ee_c_le(em_sdk_math_original_0011DF78(dy), 8.0f)) return 0;   /* 0x1A8EF4 */
    TRY(wr_u16(p, b + 0x36, 0x14, 0x1A8E80));
    p->globals->s3B88 = 0;
    return 0;
}

/* ---- 001A8F40 ---------------------------------------------------------------- */

/* 001A9000's callee for class-4 types 0xA, 0xC, 0x18 and 0x2A (outer a0,
 * inner a1): 0x700038A0 = a1 +0xB0 - a0 +0xB0 (001028D0), its w word = 0,
 * 0x70003A20 = its xyz dot (00102738); 0x70003A24 = 3.0 + a0's radius, then
 * squared in place; when 0x70003A20 <= 0x70003A24: a1's +0x36 halfword =
 * 0x2014 and 0x70003B88 = 0. */
int em_coll_list_001A8F40(P *p, uint32_t a0, uint32_t a1)
{
    if (!ready(p)) return p ? fail(p, 0x1A8F40) : -1;
    EmCollListGlobals *g = p->globals;
    uint32_t qa[4], qb[4], rec;
    float r;
    TRY(rd_q(p, a1 + 0xB0, qa, 0x1A8F64));
    TRY(rd_q(p, a0 + 0xB0, qb, 0x1A8F64));
    TRY(vsub_38A0(p, qa, qb, 0x1A8F64));
    g->s38A0[3] = 0;                                                  /* 0x700038AC */
    TRY(dot_38A0(p, &g->s3A20, 0x1A8F80));
    TRY(rd_u32(p, a0 + 0x30, &rec, 0x1A8F40));
    TRY(rd_f(p, rec, &r, 0x1A8F40));
    g->s3A24 = em_ee_bits(em_ee_add(3.0f, r));
    g->s3A24 = em_ee_bits(em_ee_mul(em_ee_float(g->s3A24), em_ee_float(g->s3A24)));
    if (!em_ee_c_le(em_ee_float(g->s3A20), em_ee_float(g->s3A24))) return 0;
    TRY(wr_u16(p, a1 + 0x36, 0x2014, 0x1A8F40));
    g->s3B88 = 0;
    return 0;
}

/* ---- 001A91C0 (001A9360's response) -------------------------------------------- */

/* 001A91C0(e, out, t): out's +0x36 halfword by e's +0xD: 0..4 0x3C, 5 0x64,
 * 6 0x50 (t < 10.0) or 0x1E, 7 by e's +0x94 halfword (0 / 1 / other) 0x118 /
 * 0xB4 / 0x17C (t < 10.0) or 0x96 / 0x78 / 0xB4, 8, 9, 12, 13 5, 10 0x28;
 * 11 and 14 return at once; then (every other value too) +0x36 |= 0x2000. */
static int response_1A91C0(P *p, uint32_t e, uint32_t out, float t)
{
    uint8_t kind;
    TRY(rd_u8(p, e + 0xD, &kind, 0x1A91C0));
    uint16_t v = 0;
    int set = 1;
    switch (kind) {
    case 0: case 1: case 2: case 3: case 4: v = 0x3C; break;
    case 5: v = 0x64; break;
    case 6: v = em_ee_c_lt(t, 10.0f) ? 0x50 : 0x1E; break;
    case 7: {
        int16_t h;
        TRY(rd_s16(p, e + 0x94, &h, 0x1A91C0));
        if (em_ee_c_lt(t, 10.0f)) v = h == 0 ? 0x118 : h == 1 ? 0xB4 : 0x17C;
        else v = h == 0 ? 0x96 : h == 1 ? 0x78 : 0xB4;
        break;
    }
    case 8: case 9: case 12: case 13: v = 5; break;
    case 10: v = 0x28; break;
    case 11: case 14: return 0;
    default: set = 0; break;
    }
    if (set) TRY(wr_u16(p, out + 0x36, v, 0x1A91C0));
    uint16_t now;
    TRY(rd_u16(p, out + 0x36, &now, 0x1A9348));
    return wr_u16(p, out + 0x36, (uint16_t)(now | 0x2000), 0x1A9348);
}

/* ---- 001A9360 ---------------------------------------------------------------- */

/* 001A97B0's callee for an outer class-0xD type 5 (a0) and an inner class-2
 * entry (a1): 00183C40(a1, 0x700038B0); 0x700038A0 = that point - a0 +0xB0,
 * w = 0; 0x70003A20 = its xyz dot; 0x70003A24 = (r0 + r1) squared in place
 * (the radii at a0's and a1's +0x30 records). Within: 001A91C0(a0, a1,
 * sqrt(0x70003A20)), 0x700038A0 recomputed, w = 1.0, a1 +0x70 = its
 * normal (00102760) and 0x70003B88 = 0 (001A97B0's inner walk ends). */
int em_coll_list_001A9360(P *p, uint32_t a0, uint32_t a1)
{
    if (!math_ready(p) || !p->workers.normalize) return p ? fail(p, 0x1A9360) : -1;
    EmCollListGlobals *g = p->globals;
    uint32_t q[4], rec0, rec1;
    float r0, r1, len;
    TRY(anchor_183C40(p, a1));                                        /* 0x1A9380 */
    TRY(rd_q(p, a0 + 0xB0, q, 0x1A9398));
    TRY(vsub_38A0(p, g->s38B0, q, 0x1A9398));
    g->s38A0[3] = 0;
    TRY(dot_38A0(p, &g->s3A20, 0x1A93B4));
    TRY(rd_u32(p, a0 + 0x30, &rec0, 0x1A9360));
    TRY(rd_f(p, rec0, &r0, 0x1A9360));
    TRY(rd_u32(p, a1 + 0x30, &rec1, 0x1A9360));
    TRY(rd_f(p, rec1, &r1, 0x1A9360));
    g->s3A24 = em_ee_bits(em_ee_add(r0, r1));
    g->s3A24 = em_ee_bits(em_ee_mul(em_ee_float(g->s3A24), em_ee_float(g->s3A24)));
    if (!em_ee_c_le(em_ee_float(g->s3A20), em_ee_float(g->s3A24))) return 0;
    TRY(sqrt_1E748(p, em_ee_float(g->s3A20), &len, 0x1A940C));
    TRY(response_1A91C0(p, a0, a1, len));                             /* 0x1A941C */
    TRY(rd_q(p, a0 + 0xB0, q, 0x1A9434));
    TRY(vsub_38A0(p, g->s38B0, q, 0x1A9434));
    g->s38A0[3] = 0x3F800000u;
    TRY(normalize_38A0(p, a1 + 0x70, 0x1A9450));
    g->s3B88 = 0;
    return 0;
}

/* ---- 001A96F0 ---------------------------------------------------------------- */

/* 001A97B0's callee for an outer class-0xD type 3 (a) and an inner class-2
 * entry (b): b's +0xB0 point in a's box (half sizes at a's +0x30 record):
 * |dx| <= w[0], |dz| <= w[2], |dy| <= 1.0 + w[1] (0011DF78 of b - a): b's
 * +0x54 halfword = 1 and 0x70003B86 = 0 (001A97B0's outer walk ends). */
int em_coll_list_001A96F0(P *p, uint32_t a, uint32_t b)
{
    if (!ready(p)) return p ? fail(p, 0x1A96F0) : -1;
    float bv, av, w;
    uint32_t box;
    TRY(rd_f(p, b + 0xB0, &bv, 0x1A96F0));
    TRY(rd_f(p, a + 0xB0, &av, 0x1A96F0));
    float d = abs_diff(bv, av);                                       /* 0x1A9710 */
    TRY(rd_u32(p, a + 0x30, &box, 0x1A96F0));
    TRY(rd_f(p, box, &w, 0x1A96F0));
    if (!em_ee_c_le(d, w)) return 0;
    TRY(rd_f(p, b + 0xB8, &bv, 0x1A96F0));
    TRY(rd_f(p, a + 0xB8, &av, 0x1A96F0));
    d = abs_diff(bv, av);                                             /* 0x1A9738 */
    TRY(rd_u32(p, a + 0x30, &box, 0x1A96F0));
    TRY(rd_f(p, box + 8, &w, 0x1A96F0));
    if (!em_ee_c_le(d, w)) return 0;
    TRY(rd_f(p, b + 0xB4, &bv, 0x1A96F0));
    TRY(rd_f(p, a + 0xB4, &av, 0x1A96F0));
    d = abs_diff(bv, av);                                             /* 0x1A9760 */
    TRY(rd_u32(p, a + 0x30, &box, 0x1A96F0));
    TRY(rd_f(p, box + 4, &w, 0x1A96F0));
    if (!em_ee_c_le(d, em_ee_add(1.0f, w))) return 0;
    TRY(wr_u16(p, b + 0x54, 1, 0x1A96F0));
    p->globals->s3B86 = 0;
    return 0;
}

/* ---- 001A9480 ---------------------------------------------------------------- */

/* The squared distance of 0x700038B0 (or a node point) from a0's +0xB0
 * against (radius) squared, through the scratch words as the original
 * leaves them. */
static int near_scratch(P *p, const uint32_t point[4], uint32_t a0, float radius, int *inside, uint32_t sub,
                        uint32_t dot)
{
    EmCollListGlobals *g = p->globals;
    uint32_t q[4];
    TRY(rd_q(p, a0 + 0xB0, q, sub));
    TRY(vsub_38A0(p, point, q, sub));
    g->s38A0[3] = 0;
    TRY(dot_38A0(p, &g->s3A20, dot));
    g->s3A24 = em_ee_bits(radius);
    g->s3A24 = em_ee_bits(em_ee_mul(em_ee_float(g->s3A24), em_ee_float(g->s3A24)));
    *inside = em_ee_c_le(em_ee_float(g->s3A20), em_ee_float(g->s3A24));
    return 0;
}

/* 001A97B0's callee for every other outer class-0xD type (a0; type 6 with
 * +0xD 2) and an inner class-2 entry (a1): the 001A9360 distance test
 * (00183C40, radii r0 + r1). For a1's +3 type 0xB, when that test fails,
 * the twelve node indices of D_0024A840 in order: the node at a1's +0x110
 * + 4 index, its +0xC0 point against a0 +0xB0 with radius 6.0 + r0 (r0
 * re-read); the first inside counts. On a hit: a1's +0x36 halfword =
 * 0x400A, a1 +0x70 = a0 +0xC0 (00102948, one quadword), 0x70003B88 = 0. */
int em_coll_list_001A9480(P *p, uint32_t a0, uint32_t a1)
{
    if (!ready(p) || !p->data || !p->data->d24A740 || p->data->d24A740_size < 0x10C)
        return p ? fail(p, 0x1A9480) : -1;
    EmCollListGlobals *g = p->globals;
    uint8_t type;
    uint32_t rec0, rec1;
    float r0, r1;
    int inside = 0;
    TRY(rd_u8(p, a1 + 3, &type, 0x1A9480));
    const uint32_t sub = type == 0xB ? 0x1A94D4 : 0x1A9638;
    TRY(anchor_183C40(p, a1));                                        /* 0x1A94BC / 0x1A9620 */
    TRY(rd_u32(p, a0 + 0x30, &rec0, 0x1A9480));
    TRY(rd_f(p, rec0, &r0, 0x1A9480));
    TRY(rd_u32(p, a1 + 0x30, &rec1, 0x1A9480));
    TRY(rd_f(p, rec1, &r1, 0x1A9480));
    TRY(near_scratch(p, g->s38B0, a0, em_ee_add(r0, r1), &inside, sub, type == 0xB ? 0x1A94F0 : 0x1A9654));
    if (type == 0xB && !inside) {
        for (unsigned i = 0; i < 0xC; ++i) {
            const uint8_t index = p->data->d24A740[0x100 + i];        /* D_0024A840[i] */
            uint32_t node, point[4];
            TRY(rd_u32(p, a1 + 0x110 + 4u * index, &node, 0x1A9480));
            TRY(rd_q(p, node + 0xC0, point, 0x1A9570));
            TRY(rd_u32(p, a0 + 0x30, &rec0, 0x1A9480));
            TRY(rd_f(p, rec0, &r0, 0x1A9480));
            TRY(near_scratch(p, point, a0, em_ee_add(6.0f, r0), &inside, 0x1A9570, 0x1A958C));
            if (inside) break;
        }
    }
    if (!inside) return 0;
    uint32_t q[4];
    TRY(wr_u16(p, a1 + 0x36, 0x400A, 0x1A9480));
    TRY(rd_q(p, a0 + 0xC0, q, 0x1A96B8));
    TRY(wr_q(p, a1 + 0x70, q, 0x1A96B8));
    g->s3B88 = 0;
    return 0;
}

/* ---- 001A9E00 ---------------------------------------------------------------- */

/* 001A9F60's callee (the player a0 against a class-2 type-0 entry a1): dx,
 * dz = a0 +0xA0 / +0xA8 - a1 +0xB0 / +0xB8; when sqrt(dx dx + dz dz) (the
 * sum in the accumulator) <= sum = r0 + r1 (the first floats of the +0x30
 * records), h = r0[1] / 2, d = |a0 +0xA4 + h - a1 +0xB4| (negated when
 * below 0.0) <= h + r1[1] / 2 and a1's +3 is 0: ang = 001B1470(0011E620(dx,
 * dz)); a1 +0xB0 = a0 +0xA0 - sum 0011E2A8(ang), a1 +0xB8 = a0 +0xA8 -
 * sum 0011DE90(ang) (0011DE90 called first); a1 +0xB = 1 unless a0's +0
 * has bit 2. */
int em_coll_list_001A9E00(P *p, uint32_t a0, uint32_t a1)
{
    if (!math_ready(p)) return p ? fail(p, 0x1A9E00) : -1;
    const EmSdkMathContext *m = p->math;
    uint32_t fault = 0, rec0, rec1;
    float ax, bx, az, bz, dist, r0, r1, h0, h1, ay, by;
    TRY(rd_f(p, a0 + 0xA0, &ax, 0x1A9E00));
    TRY(rd_f(p, a1 + 0xB0, &bx, 0x1A9E00));
    TRY(rd_f(p, a0 + 0xA8, &az, 0x1A9E00));
    TRY(rd_f(p, a1 + 0xB8, &bz, 0x1A9E00));
    const float dx = em_ee_sub(ax, bx);
    const float dz = em_ee_sub(az, bz);
    TRY(sqrt_1E748(p, em_ee_madd(em_ee_mula(dx, dx), dz, dz), &dist, 0x1A9E40));
    TRY(rd_u32(p, a0 + 0x30, &rec0, 0x1A9E00));
    TRY(rd_u32(p, a1 + 0x30, &rec1, 0x1A9E00));
    TRY(rd_f(p, rec0, &r0, 0x1A9E00));
    TRY(rd_f(p, rec1, &r1, 0x1A9E00));
    const float sum = em_ee_add(r0, r1);
    if (!em_ee_c_le(dist, sum)) return 0;
    TRY(rd_f(p, rec0 + 4, &h0, 0x1A9E00));
    TRY(rd_f(p, a0 + 0xA4, &ay, 0x1A9E00));
    TRY(rd_f(p, a1 + 0xB4, &by, 0x1A9E00));
    const float h = em_ee_div(h0, 2.0f);
    float d = em_ee_sub(em_ee_add(ay, h), by);
    if (em_ee_c_lt(d, 0.0f)) d = em_ee_neg(d);
    TRY(rd_f(p, rec1 + 4, &h1, 0x1A9E00));
    if (!em_ee_c_le(d, em_ee_add(h, em_ee_div(h1, 2.0f)))) return 0;
    uint8_t busy, flags;
    TRY(rd_u8(p, a1 + 3, &busy, 0x1A9E00));
    if (busy != 0) return 0;
    float a, ang, c, s;
    if (em_sdk_math_original_0011E620(m->tables, &m->world, &m->workers, dx, dz, &a, &fault) < 0)
        return fail(p, fault ? fault : 0x1A9EE4);
    if (em_effect_original_001B1470(a, &ang) < 0) return fail(p, 0x1A9EEC);   /* a wrap that cannot end */
    if (em_sdk_math_original_0011DE90(m->tables, ang, &c, &fault) < 0) return fail(p, fault ? fault : 0x1A9EF8);
    const float s1 = em_ee_mul(sum, c);
    if (em_sdk_math_original_0011E2A8(m->tables, ang, &s, &fault) < 0) return fail(p, fault ? fault : 0x1A9F04);
    const float s2 = em_ee_mul(sum, s);
    TRY(rd_f(p, a0 + 0xA0, &ax, 0x1A9E00));
    TRY(wr_f(p, a1 + 0xB0, em_ee_sub(ax, s2), 0x1A9E00));
    TRY(rd_f(p, a0 + 0xA8, &az, 0x1A9E00));
    TRY(wr_f(p, a1 + 0xB8, em_ee_sub(az, s1), 0x1A9E00));
    TRY(rd_u8(p, a0, &flags, 0x1A9E00));
    if (!(flags & 4)) TRY(wr_u8(p, a1 + 0xB, 1, 0x1A9E00));
    return 0;
}

/* ---- 001AA000 ---------------------------------------------------------------- */

/* 001AA140's pair callee (a, b, ga = a + 0x1F0, gb = b + 0x1F0): nothing
 * unless the words ga +0xE4 and gb +0xE4 are equal; both 0x200 zero
 * 0x70003B86 and nothing else. Otherwise dx, dz = a - b at +0xB0 / +0xB8;
 * when sqrt(dx dx + dz dz) (the sum in the accumulator) <= r = ra[0] +
 * rb[0] and sqrt(dy dy) (dy = a.y - b.y, a plain product) <= ra[1] + rb[1]
 * (the +0x30 records re-read): ang = 001B1470(0011E620(dx, dz)), a +0xB0 =
 * b +0xB0 + r 0011E2A8(ang), a +0xB8 = b +0xB8 + r 0011DE90(ang) (0011DE90
 * called first), 0x70003B86 = 0. */
int em_coll_list_001AA000(P *p, uint32_t a, uint32_t b, uint32_t ga, uint32_t gb)
{
    if (!math_ready(p)) return p ? fail(p, 0x1AA000) : -1;
    const EmSdkMathContext *m = p->math;
    uint32_t ka, kb, ra, rb, fault = 0;
    TRY(rd_u32(p, ga + 0xE4, &ka, 0x1AA000));
    TRY(rd_u32(p, gb + 0xE4, &kb, 0x1AA000));
    if (ka != kb) return 0;
    if (ka == 0x200) {
        p->globals->s3B86 = 0;
        return 0;
    }
    float ax, bx, az, bz, ay, by, d, r0, r1;
    TRY(rd_f(p, a + 0xB0, &ax, 0x1AA000));
    TRY(rd_f(p, b + 0xB0, &bx, 0x1AA000));
    TRY(rd_f(p, a + 0xB8, &az, 0x1AA000));
    TRY(rd_f(p, b + 0xB8, &bz, 0x1AA000));
    const float dx = em_ee_sub(ax, bx);
    const float dz = em_ee_sub(az, bz);
    TRY(sqrt_1E748(p, em_ee_madd(em_ee_mula(dx, dx), dz, dz), &d, 0x1AA064));
    TRY(rd_u32(p, a + 0x30, &ra, 0x1AA000));
    TRY(rd_u32(p, b + 0x30, &rb, 0x1AA000));
    TRY(rd_f(p, ra, &r0, 0x1AA000));
    TRY(rd_f(p, rb, &r1, 0x1AA000));
    const float r = em_ee_add(r0, r1);
    if (!em_ee_c_le(d, r)) return 0;
    TRY(rd_f(p, a + 0xB4, &ay, 0x1AA000));
    TRY(rd_f(p, b + 0xB4, &by, 0x1AA000));
    const float dy = em_ee_sub(ay, by);
    TRY(sqrt_1E748(p, em_ee_mul(dy, dy), &d, 0x1AA09C));
    TRY(rd_u32(p, a + 0x30, &ra, 0x1AA000));
    TRY(rd_u32(p, b + 0x30, &rb, 0x1AA000));
    TRY(rd_f(p, ra + 4, &r0, 0x1AA000));
    TRY(rd_f(p, rb + 4, &r1, 0x1AA000));
    if (!em_ee_c_le(d, em_ee_add(r0, r1))) return 0;
    float t, ang, c, s;
    if (em_sdk_math_original_0011E620(m->tables, &m->world, &m->workers, dx, dz, &t, &fault) < 0)
        return fail(p, fault ? fault : 0x1AA0CC);
    if (em_effect_original_001B1470(t, &ang) < 0) return fail(p, 0x1AA0D4);    /* a wrap that cannot end */
    if (em_sdk_math_original_0011DE90(m->tables, ang, &c, &fault) < 0) return fail(p, fault ? fault : 0x1AA0E0);
    const float oz = em_ee_mul(r, c);
    if (em_sdk_math_original_0011E2A8(m->tables, ang, &s, &fault) < 0) return fail(p, fault ? fault : 0x1AA0EC);
    const float ox = em_ee_mul(r, s);
    TRY(rd_f(p, b + 0xB0, &bx, 0x1AA000));
    TRY(wr_f(p, a + 0xB0, em_ee_add(bx, ox), 0x1AA000));
    TRY(rd_f(p, b + 0xB8, &bz, 0x1AA000));
    TRY(wr_f(p, a + 0xB8, em_ee_add(bz, oz), 0x1AA000));
    p->globals->s3B86 = 0;
    return 0;
}

/* ---- 0021BD10 / 0021BB00 ------------------------------------------------------- */

/* 0021BB00(e): 1 when e's +0x1F0 byte k is 8, 9, 10, 0x11..0x16 (not 0x17),
 * 0x18, 0x19, 0x1B, 0x1C, 0x1E, 0x1F, 0x20, 0x23..0x26, 0x28, 0x2E, or 0x2C
 * with +0xD other than 2; for k 0xF, 0 when +4 is 1, +5 is 8 and +6 is 2, 4
 * or 5, else 1; 0 otherwise. (The live player's translation of the same
 * routine is em_player_0021BB00.) */
static int blocked_21BB00(P *p, uint32_t e, int *result)
{
    uint8_t k;
    TRY(rd_u8(p, e + 0x1F0, &k, 0x21BB00));
    *result = 0;
    switch (k) {
    case 8: case 9: case 10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16:
    case 0x18: case 0x19: case 0x1B: case 0x1C: case 0x1E: case 0x1F: case 0x20: case 0x23:
    case 0x24: case 0x25: case 0x26: case 0x28: case 0x2E:
        *result = 1;
        return 0;
    case 0x2C: {
        uint8_t d;
        TRY(rd_u8(p, e + 0xD, &d, 0x21BB00));
        *result = d != 2;
        return 0;
    }
    case 0xF: {
        uint8_t b4, b5, b6;
        TRY(rd_u8(p, e + 4, &b4, 0x21BB00));
        *result = 1;
        if (b4 != 1) return 0;
        TRY(rd_u8(p, e + 5, &b5, 0x21BB00));
        if (b5 != 8) return 0;
        TRY(rd_u8(p, e + 6, &b6, 0x21BB00));
        if (b6 == 2 || b6 == 4 || b6 == 5) *result = 0;
        return 0;
    }
    default:
        return 0;
    }
}

/* 0021BD10(): 1 when the player's +0 byte (D_008102B0) is 1 and
 * 0021BB00(D_008102B0) is 0, else 0. */
int em_coll_list_0021BD10(P *p, int *result)
{
    if (!ready(p) || !result) return p ? fail(p, 0x21BD10) : -1;
    uint8_t state;
    int blocked;
    *result = 0;
    TRY(rd_u8(p, EM_COLL_LIST_PLAYER, &state, 0x21BD10));
    if (state != 1) return 0;
    TRY(blocked_21BB00(p, EM_COLL_LIST_PLAYER, &blocked));            /* 0x21BD34 */
    *result = blocked == 0;
    return 0;
}

/* ---- Adapters for the translated pair callees ------------------------------ */

int em_coll_list_passes_001A8840(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b)
{
    (void)context;
    return em_coll_list_001A8840(passes, a, b);
}
int em_coll_list_passes_001A8970(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b)
{
    (void)context;
    return em_coll_list_001A8970(passes, a, b);
}
int em_coll_list_passes_001A8E80(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b)
{
    (void)context;
    return em_coll_list_001A8E80(passes, a, b);
}
int em_coll_list_passes_001A8F40(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b)
{
    (void)context;
    return em_coll_list_001A8F40(passes, a, b);
}
int em_coll_list_passes_001A9360(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b)
{
    (void)context;
    return em_coll_list_001A9360(passes, a, b);
}
int em_coll_list_passes_001A96F0(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b)
{
    (void)context;
    return em_coll_list_001A96F0(passes, a, b);
}
int em_coll_list_passes_001A9480(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b)
{
    (void)context;
    return em_coll_list_001A9480(passes, a, b);
}
int em_coll_list_passes_001A9E00(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b)
{
    (void)context;
    return em_coll_list_001A9E00(passes, a, b);
}
int em_coll_list_passes_001AA000(void *context, EmCollListPasses *passes, uint32_t a, uint32_t b,
                                 uint32_t a1f0, uint32_t b1f0)
{
    (void)context;
    return em_coll_list_001AA000(passes, a, b, a1f0, b1f0);
}
int em_coll_list_passes_0021BD10(void *context, EmCollListPasses *passes, int *result)
{
    (void)context;
    return em_coll_list_0021BD10(passes, result);
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
