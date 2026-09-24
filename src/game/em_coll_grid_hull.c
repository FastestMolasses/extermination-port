/* em_coll_grid_hull.c - the move walkers' grid pass 0019CB60 and the hull
 * locks 001A6440 / 001A6AD0 / 001A7280 (see the header and
 * docs/COLL_GRID_HULL.md).
 *
 * Every routine names the original it translates; comments cite the
 * original address of each branch and store. Comparisons, their directions
 * and the operation order follow the .s. */
#include "game/em_coll_grid_hull.h"

#include <string.h>

#include "game/em_ee_float.h"

static int le(float a, float b) { return em_ee_c_le(a, b); }
static int lt(float a, float b) { return em_ee_c_lt(a, b); }
static float f_bits(uint32_t bits) { return em_ee_float(bits); }

#define ONE_BITS 0x3F800000u         /* 1.0: the w lane of a transformed point */
#define EPS_FACING 0xB727C5ACu       /* -1e-5: 0x1A66F8 / 0x1A6D94 */
#define EPS_EDGE 0x3727C5ACu         /* 1e-5: 0x1A6924 / 0x1A6FC4 / 0x1A7700 */
#define RATIO_WALL 0x3EFB075Eu       /* 0.49029058: 0x1A714C / 0x1A71B8 */
#define RATIO_STEEP 0x40400000u      /* 3.0: 0x1A717C / 0x1A71E8 */
#define NO_SLOT 0x270F               /* the bone-slot cache's start value */

/* ---- 0019CB60 ------------------------------------------------------------------ */

const void *em_coll_grid_hull_node_record(const EmCollProbeGrid *g, int node)
{
    if (!g || !g->words || node < 0 || (uint32_t)node >= g->count) return NULL;
    return (const void *)(g->words + 12 * (size_t)node);
}

int em_coll_grid_hull_node_index(const EmCollProbeGrid *g, const void *record)
{
    if (!g || !g->words || !record) return -1;
    const uintptr_t base = (uintptr_t)g->words, p = (uintptr_t)record;
    const uintptr_t stride = 12 * sizeof *g->words;
    if (p < base || (p - base) % stride) return -1;
    const uintptr_t i = (p - base) / stride;
    return i < g->count ? (int)i : -1;
}

/* The node attribute gate at 0x19CDF4..0x19CE5C (the static kind gate of
 * 0019FE50 / 001A0B10): 0x5A and above never pass; 0x51 needs query class
 * 0, 0x52 query class 2, and 0x53 is refused to query class -1. */
static int attribute_passes(int16_t attr, int16_t query_class)
{
    if (!(attr < 0x5A)) return 0;                                   /* 0x19CDF4 */
    if (attr == 0x51 && query_class != 0) return 0;                 /* 0x19CE04, 0x19CE14 */
    if (attr == 0x52 && query_class != 2) return 0;                 /* 0x19CE24, 0x19CE38 */
    if (attr == 0x53 && query_class == -1) return 0;                /* 0x19CE44, 0x19CE58 */
    return 1;
}

int em_coll_grid_hull_0019CB60(const EmCollProbeGrid *g, EmCollProbeState *s)
{
    if (!g || !g->tables || !g->words || !g->emcl || !s) return -1;
    /* The x and z direction masks (s5 for the end, s6 for the start). */
    unsigned s5, s6;
    if (le(s->start[0], s->end[0])) { s5 = 2; s6 = 1; }             /* 0x19CB94 */
    else { s5 = 1; s6 = 2; }
    if (le(s->start[2], s->end[2])) { s5 |= 0x20; s6 |= 0x10; }     /* 0x19CBC8 */
    else { s5 |= 0x10; s6 |= 0x20; }
    if (em_coll_probe_0019F1A0(g, s, s->start, s6)) return -1;      /* 0x19CBF4 */
    if (em_coll_probe_0019F1A0(g, s, s->end, s5)) return -1;        /* 0x19CC04 */
    int32_t first[6];
    for (int i = 0; i < 6; ++i) first[i] = s->rank[i];             /* 0x19CC20 loop: sp+80 */
    if (em_coll_probe_0019F1A0(g, s, s->start, s5)) return -1;      /* 0x19CC44 */
    if (em_coll_probe_0019F1A0(g, s, s->end, s6)) return -1;        /* 0x19CC54 */
    /* The span pick over directions 0, 1, 4 and 5 (0x19CC78..0x19CD44;
     * 2 and 3, the y pair, are skipped at 0x19CC80). */
    const int32_t n = (int32_t)g->count;
    int32_t best = n;                                               /* 0x19CC60: 0x7000320C */
    int32_t lo = 0, hi = 0;
    int cand = -1;                                                  /* s1, s2, s4 */
    for (int i = 0; i < 6; ++i) {
        if (i == 2 || i == 3) continue;                             /* 0x19CC7C */
        const int16_t *helper = g->tables + (size_t)(6 + i) * (size_t)n;   /* *(0x70003228 + 4i) */
        const int32_t at = first[i];
        if (at < 0 || at >= n) return -1;
        if (i & 1) {
            s->span_lo = s->rank[i];                                /* 0x19CC9C: 0x70003B86 */
            s->span_hi = helper[at];                                /* 0x19CCBC: 0x70003B88 */
        } else {
            s->span_lo = helper[at];                                /* 0x19CCD8 */
            s->span_hi = s->rank[i];                                /* 0x19CCE4 */
            s->span_hi = (int16_t)(s->span_hi + 1);                 /* 0x19CCF8 */
        }
        const int32_t diff = (int32_t)s->span_hi - (int32_t)s->span_lo;   /* 0x19CD10 */
        if (diff < best) {                                          /* 0x19CD14 */
            best = diff;
            lo = s->span_lo;
            hi = s->span_hi;
            cand = i;
        }
    }
    /* With no direction under the node count, s1/s2/s4 are uninitialized
     * registers (0x19CD50 indexes a table with s4). */
    if (cand < 0) return -1;
    if (lo < hi && (lo < 0 || hi > n)) return -1;   /* the walk would leave the table */
    const int16_t *table = g->tables + (size_t)cand * (size_t)n;    /* 0x19CD58: *(0x70003210 + 4 s4) */
    int found = -1;                                                 /* s3 = 0 */
    float saved[3] = { 0.0f, 0.0f, 0.0f };                          /* sp+A0 */
    for (int32_t k = lo; k < hi; ++k) {                             /* 0x19CD60, 0x19CED4 */
        const int node = table[k];                                  /* 0x19CD6C */
        if (node < 0 || (uint32_t)node >= g->count) return -1;
        const int16_t *wd = g->words + 12 * node;                   /* node +0x00.. (0x70003208 + 0x40 node) */
        if (s->rank[0] < wd[6]) continue;                           /* 0x19CD8C: +0x0C */
        if (wd[7] < s->rank[1]) continue;                           /* 0x19CDA4: +0x0E */
        if (s->rank[4] < wd[10]) continue;                          /* 0x19CDBC: +0x14 */
        if (wd[11] < s->rank[5]) continue;                          /* 0x19CDD4: +0x16 */
        s->span_hi = g->emcl->polys[g->first + (uint32_t)node].attr;   /* 0x19CDE0..0x19CDE8: +0x1A */
        if (!attribute_passes(s->span_hi, s->query_class)) continue;
        const int r = em_coll_probe_0019ED80(g, s, node);           /* 0x19CE64 */
        if (r < 0) return -1;
        if (!r) continue;
        s->end[0] = s->point[0];                                    /* 0x19CE8C: 0x700031A0 */
        s->end[2] = s->point[2];                                    /* 0x19CEA0: 0x700031A8 */
        memcpy(saved, s->point, sizeof saved);                      /* 0x19CEA8 loop: sp+A0 */
        found = node;                                               /* 0x19CEC8: s3 = 0x700031D0 */
    }
    if (found < 0) return 1;                                        /* 0x19CEE0 */
    s->record = EM_COLL_PROBE_RECORD_GRID;                          /* 0x19CEF8: 0x700031D0 = s3 */
    s->node = found;
    memcpy(s->point, saved, sizeof saved);                          /* 0x19CF00 loop */
    return 0;
}

/* ---- The SDK vector leaves, as their VU0 macro instructions --------------------- */

#define VU(call) do { if ((call) != EM_EE_FLOAT_OK) return -1; } while (0)

/* 001028D0(out, a, b): vsub.xyzw (fs = a, ft = b). */
static int vsub4(float out[4], const float a[4], const float b[4])
{
    float r[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    VU(em_vu_vec(EM_VU_SUB, 0xF, EM_VU_NO_BC, a, b, 0.0f, NULL, r));
    memcpy(out, r, sizeof r);
    return 0;
}

/* 001028B8(out, a, b): vadd.xyzw. */
static int vadd4(float out[4], const float a[4], const float b[4])
{
    float r[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    VU(em_vu_vec(EM_VU_ADD, 0xF, EM_VU_NO_BC, a, b, 0.0f, NULL, r));
    memcpy(out, r, sizeof r);
    return 0;
}

/* 00102738(a, b): vf5.xyz = a * b, then x += y, x += z; returns vf5.x. */
static int vdot(const float a[4], const float b[4], float *out)
{
    float v[4];
    memcpy(v, b, sizeof v);
    VU(em_vu_vec(EM_VU_MUL, 0xE, EM_VU_NO_BC, a, v, 0.0f, NULL, v));
    VU(em_vu_vec(EM_VU_ADDBC, 0x8, 1, v, v, 0.0f, NULL, v));
    VU(em_vu_vec(EM_VU_ADDBC, 0x8, 2, v, v, 0.0f, NULL, v));
    *out = v[0];
    return 0;
}

/* 00103230(out, v, s): v.xyz times vf5.x = s; w keeps v's. */
static int vscale(float out[4], const float v[4], float s)
{
    float r[4], b[4] = { s, 0.0f, 0.0f, 0.0f };
    memcpy(r, v, sizeof r);
    VU(em_vu_vec(EM_VU_MULBC, 0xE, 0, r, b, 0.0f, NULL, r));
    memcpy(out, r, sizeof r);
    return 0;
}

/* 001026A0(out, m, v): ACC = row0 * v.x, ACC += row1 * v.y, ACC += row2 * v.z,
 * out = ACC + row3 * v.w, all four lanes (the VU0 accumulate forms). */
static int vapply(float out[4], const float m[16], const float v[4])
{
    float x[4], acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, r[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    memcpy(x, v, sizeof x);
    VU(em_vu_vec(EM_VU_MULABC, 0xF, 0, m + 0, x, 0.0f, NULL, acc));
    VU(em_vu_vec(EM_VU_MADDABC, 0xF, 1, m + 4, x, 0.0f, acc, acc));
    VU(em_vu_vec(EM_VU_MADDABC, 0xF, 2, m + 8, x, 0.0f, acc, acc));
    VU(em_vu_vec(EM_VU_MADDBC, 0xF, 3, m + 12, x, 0.0f, acc, r));
    memcpy(out, r, sizeof r);
    return 0;
}

/* ---- Chain access (original byte layout, little-endian) ------------------------ */

/* The chain's base address is word aligned in the original (its count word
 * is read with a word load), so an offset's alignment is the address's: a
 * halfword read at an odd offset or a word read off a word boundary would
 * raise an EE address error. */
static int rd8(const EmCollHullChain *c, size_t off, uint8_t *v)
{
    if (off >= c->size) return -1;
    *v = c->bytes[off];
    return 0;
}

static int rd16(const EmCollHullChain *c, size_t off, uint16_t *v)
{
    if ((off & 1) || off > c->size || c->size - off < 2) return -1;
    memcpy(v, c->bytes + off, 2);
    return 0;
}

static int rd32(const EmCollHullChain *c, size_t off, uint32_t *v)
{
    if ((off & 3) || off > c->size || c->size - off < 4) return -1;
    memcpy(v, c->bytes + off, 4);
    return 0;
}

/* Three floats at `off` into lanes x/y/z, `w` into lane w. */
static int rd_vec(const EmCollHullChain *c, size_t off, uint32_t w, float out[4])
{
    uint32_t b[3];
    for (int k = 0; k < 3; ++k)
        if (rd32(c, off + 4u * (size_t)k, &b[k])) return -1;
    for (int k = 0; k < 3; ++k) out[k] = em_ee_float(b[k]);
    out[3] = em_ee_float(w);
    return 0;
}

/* ---- The shared record walk ------------------------------------------------------ */

enum { LOCK_6440, LOCK_6AD0, LOCK_7280 };

/* The lock's segment copies: qa (sp+150 / sp+130), qb (sp+160 / sp+140) and
 * dir = qb - qa (sp+170 / sp+150). */
typedef struct {
    float qa[4], qb[4], dir[4];
} Segment;

/* The copy loop at 0x1A64B8 / 0x1A6B48 / 0x1A72E8: qa = 0x70003190,
 * qb = 0x700031A0 (x/y/z), both w lanes 0, then dir = 001028D0(qb, qa). */
static int segment_of(const EmCollHullScratch *s, Segment *g)
{
    for (int k = 0; k < 3; ++k) {
        g->qa[k] = s->start[k];
        g->qb[k] = s->end[k];
    }
    g->qa[3] = 0.0f;                                                /* 0x1A64EC / 0x1A6B7C / 0x1A731C */
    g->qb[3] = 0.0f;                                                /* 0x1A64F4 / 0x1A6B84 / 0x1A7324 */
    return vsub4(g->dir, g->qb, g->qa);                             /* 0x1A64F0 / 0x1A6B80 / 0x1A7320 */
}

/* The hit point lies strictly between qa and qb on at least one axis (the
 * branch ladder 0x1A6760..0x1A6864 and its twins: a strict crossing on x
 * goes straight to the edge test, otherwise y, then z, and no strict
 * crossing rejects the record). */
static int strictly_between_any(const Segment *g, const float hit[4])
{
    for (int a = 0; a < 3; ++a) {
        if (!le(g->qa[a], hit[a]) && lt(g->qb[a], hit[a])) return 1;
        if (lt(g->qa[a], hit[a]) && !le(g->qb[a], hit[a])) return 1;
    }
    return 0;
}

/* One walk over one entity's chain. `face_mask` is the entity's
 * +0x5C..+0x5E, `arg` the lock's argument; `g` is the lock's segment,
 * shortened by every accepted record. Returns 1 when a record was
 * accepted, 0, or -1. */
static int walk_chain(int lock, const EmCollHullChain *c, const uint8_t face_mask[3], uint32_t arg,
                      Segment *g, EmCollHullScratch *s)
{
    if (!c->bytes) return -1;
    const unsigned want_x = arg & 0x10, want_y = arg & 0x20, want_z = arg & 0x40;   /* 0x1A6504.. */
    int32_t count;
    uint32_t word;
    int16_t first_n;
    uint16_t u;
    if (rd32(c, 0, &word)) return -1;                               /* 0x1A6558: node +0 */
    count = (int32_t)word;
    size_t rec = 4;                                                 /* s0 = node + 4 */
    if (rd16(c, 0xA, &u)) return -1;                                /* 0x1A6560: node +0xA */
    first_n = (int16_t)u;
    if (first_n == -2) {                                            /* 0x1A656C: skip the first record */
        if (rd16(c, rec + 4, &u)) return -1;
        rec += u;
        count -= 1;
    }
    int cached = NO_SLOT;                                           /* 0x1A6528: sp+D0 */
    float mat[16];
    int accepted = 0;                                               /* sp+E0 */
    for (int32_t j = 0; j < count; ++j) {                           /* 0x1A6588, 0x1A6A50 */
        unsigned mask = 0;                                          /* s7 */
        uint8_t b;
        if (want_x) {                                               /* 0x1A659C */
            if (rd8(c, rec + 0, &b)) return -1;
            if (b & face_mask[0]) mask |= 1;
        }
        if (want_y) {                                               /* 0x1A65C4 */
            if (rd8(c, rec + 1, &b)) return -1;
            if (b & face_mask[1]) mask |= 2;
        }
        if (want_z) {                                               /* 0x1A65EC */
            if (rd8(c, rec + 2, &b)) return -1;
            if (b & face_mask[2]) mask |= 4;
        }
        if (rd16(c, rec + 6, &u)) return -1;                        /* 0x1A6610 */
        const int16_t n = (int16_t)u;                               /* s3 */
        if (n < 0) mask = 0;                                        /* 0x1A6614 */
        if (mask) {
            if (rd8(c, rec + 3, &b)) return -1;                     /* 0x1A6628: the bone slot */
            if (b != cached) {                                      /* 0x1A6630 */
                cached = b;
                if (!c->slots || b >= c->slot_count) return -1;     /* *(e + 0x110 + 4m) + 0x90 */
                memcpy(mat, c->slots + 16 * (size_t)b, sizeof mat); /* 0x1A664C: copy_qw4 */
            }
            const size_t v0 = rec + 8;                              /* s1 */
            float normal[4], at[4], t, d, nq, hit[4];
            if (rd_vec(c, v0, 0u, normal)) return -1;               /* 0x1A6670 loop, 0x1A669C: w = 0 */
            if (vapply(normal, mat, normal)) return -1;             /* 0x1A6698 */
            if (rd_vec(c, v0 + 0x10, ONE_BITS, at)) return -1;      /* 0x1A66A0..0x1A66C8: w = 1 */
            if (vapply(at, mat, at)) return -1;                     /* 0x1A66C8 */
            if (vdot(normal, at, &t)) return -1;                    /* 0x1A66D4: f20 */
            if (vdot(g->dir, normal, &d)) return -1;                /* 0x1A66E4: f21 */
            const int facing = lock == LOCK_6AD0 || (lock == LOCK_6440 && (mask & 4));
            if (facing && !le(d, f_bits(EPS_FACING))) goto next;    /* 0x1A66F0..0x1A6710 / 0x1A6DA8 */
            if (vdot(normal, g->qa, &nq)) return -1;                /* 0x1A6728 */
            const float k = em_ee_div(em_ee_sub(t, nq), d);         /* 0x1A6730, 0x1A673C */
            if (vscale(hit, g->dir, k)) return -1;                  /* 0x1A6748 */
            if (vadd4(hit, g->qa, hit)) return -1;                  /* 0x1A6758 */
            if (!strictly_between_any(g, hit)) goto next;
            /* The edge test (0x1A686C..0x1A695C): n vertices from +0x18,
             * n edge normals 12n further; the point must lie within 1e-5
             * inside every edge. */
            const size_t verts = v0 + 0x10, edges = v0 + (size_t)(3 * n + 4) * 4;
            int16_t e;
            for (e = 0; e < n; ++e) {
                float vert[4], rel[4], en[4], dot;
                if (rd_vec(c, verts + 12u * (size_t)e, ONE_BITS, vert)) return -1;   /* w = 1 */
                if (vapply(vert, mat, vert)) return -1;             /* 0x1A68C4 */
                vert[3] = 0.0f;                                     /* 0x1A68DC */
                if (vsub4(rel, hit, vert)) return -1;               /* 0x1A68D8 */
                if (rd_vec(c, edges + 12u * (size_t)e, 0u, en)) return -1;   /* w = 0 (0x1A6910) */
                if (vapply(en, mat, en)) return -1;                 /* 0x1A690C */
                en[3] = 0.0f;                                       /* 0x1A6920 */
                if (vdot(rel, en, &dot)) return -1;                 /* 0x1A691C */
                if (!le(dot, f_bits(EPS_EDGE))) break;              /* 0x1A6934..0x1A693C */
            }
            if (e < n) goto next;                                   /* 0x1A695C */
            /* Accepted (0x1A6964..0x1A6A28). */
            accepted = 1;                                           /* sp+E0 (and sp+F0) */
            for (int q = 0; q < 3; ++q) {                           /* 0x1A6990 loop */
                s->point[q] = hit[q];                               /* 0x700031B0 */
                s->cell_normal[q] = normal[q];                      /* D_700030B0 +0x24 */
            }
            if (lock != LOCK_7280) {
                if (mask & 2) {                                     /* 0x1A69BC */
                    uint8_t y;
                    if (rd8(c, rec + 1, &y)) return -1;
                    s->word_1c = (uint32_t)(y & face_mask[1] & 0xFE);   /* 0x1A69DC: 0x700030CC */
                }
                if (rd32(c, rec, &word)) return -1;                 /* 0x1A69E0 */
                s->word_20 = word;                                  /* 0x1A69F8: 0x700030D0 */
            }
            for (int q = 0; q < 3; ++q) g->qb[q] = s->point[q];     /* 0x1A6A00 loop */
            if (vsub4(g->dir, g->qb, g->qa)) return -1;             /* 0x1A6A24 */
        }
next:
        if (rd16(c, rec + 4, &u)) return -1;                        /* 0x1A6A30 */
        rec += u;
    }
    return accepted;
}

static void face_mask_of(const EmActor *e, uint8_t out[3])
{
    out[0] = (uint8_t)(e->w5C & 0xFFu);                             /* +0x5C */
    out[1] = (uint8_t)(e->w5C >> 8 & 0xFFu);                        /* +0x5D */
    out[2] = (uint8_t)(e->w5C >> 16 & 0xFFu);                       /* +0x5E */
}

static int chain_of(const EmCollHullWorld *h, const EmActor *e, EmCollHullChain *c)
{
    memset(c, 0, sizeof *c);
    if (!h || !h->chain) return -1;
    if (h->chain(h->context, e, c) < 0 || !c->bytes) return -1;
    return 0;
}

/* The entry stores of 001A6440 / 001A6AD0 (0x1A6474..0x1A6498). */
static void clear_entry(EmCollHullScratch *s)
{
    s->cell_class = 0;                                              /* 0x700030CA */
    s->word_1c = 0;                                                 /* 0x700030CC */
    s->word_20 = 0;                                                 /* 0x700030D0 */
    s->record_cell = 1;                                             /* 0x700031D0 = D_700030B0 */
}

/* 001A6AD0's surface class from the last hit normal (0x1A710C..0x1A721C):
 * ratio = ny^2 / (nx^2 + nz^2) (a MULA/MADD pair, then MUL and DIV). */
static uint16_t surface_class(const EmCollHullScratch *s)
{
    const float nx = s->cell_normal[0], ny = s->cell_normal[1], nz = s->cell_normal[2];
    const float horiz = em_ee_madd(em_ee_mula(nx, nx), nz, nz);    /* 0x1A7120, 0x1A712C */
    const int down = lt(ny, 0.0f);                                  /* 0x1A7130 */
    const float ratio = em_ee_div(em_ee_mul(ny, ny), horiz);       /* 0x1A7134, 0x1A7138 */
    if (lt(ratio, f_bits(RATIO_WALL))) return 0x2000;              /* 0x1A715C / 0x1A71C8 */
    if (le(ratio, f_bits(RATIO_STEEP))) return down ? 0x0800 : 0x1000;   /* 0x1A7188 / 0x1A71F4 */
    return down ? 0x8000 : 0x4000;                                  /* 0x1A7198 / 0x1A7204 */
}

static int class2_lock(int lock, const EmActorClassLists *lists, const EmCollHullWorld *hulls,
                       EmCollHullScratch *s, uint32_t arg)
{
    if (!lists || !s) return -1;
    clear_entry(s);
    Segment g;
    if (segment_of(s, &g)) return -1;
    const EmActorClassList *l = &lists->list[EM_ACTOR_LIST_CLASS2];
    int result = 0;                                                 /* sp+F0 */
    for (int16_t j = 0; j < l->published; ++j) {                    /* 0x1A6A80: D_00275B94 */
        const EmActor *e = em_actor_class_list_entry(lists, EM_ACTOR_LIST_CLASS2, j);   /* D_00275B8C[j] */
        if (!e) return -1;
        if (!(e->status & 1)) continue;                             /* 0x1A6540 */
        if (!e->w58) continue;                                      /* 0x1A6550: +0x58 */
        if (lock == LOCK_6AD0) {
            const unsigned cls = e->cls & 0x1Fu;                    /* 0x1A6BEC */
            if (cls == 0 || cls == 2) continue;                     /* 0x1A6BF0, 0x1A6BFC */
        }
        EmCollHullChain c;
        if (chain_of(hulls, e, &c)) return -1;
        uint8_t mask[3];
        face_mask_of(e, mask);
        const int hit = walk_chain(lock, &c, mask, arg, &g, s);
        if (hit < 0) return -1;
        if (!hit) continue;
        result = 1;
        s->entity = e;                                              /* 0x1A6A68 / 0x1A7108: 0x700031D4 */
        if (lock == LOCK_6AD0) s->cell_class = surface_class(s);   /* 0x1A710C.. */
    }
    return result;
}

int em_coll_grid_hull_001A6440(const EmActorClassLists *lists, const EmCollHullWorld *hulls,
                               EmCollHullScratch *s, uint32_t arg)
{
    return class2_lock(LOCK_6440, lists, hulls, s, arg);
}

int em_coll_grid_hull_001A6AD0(const EmActorClassLists *lists, const EmCollHullWorld *hulls,
                               EmCollHullScratch *s, uint32_t arg)
{
    return class2_lock(LOCK_6AD0, lists, hulls, s, arg);
}

int em_coll_grid_hull_001A7280(const EmCollHullWorld *hulls, EmCollHullScratch *s, uint32_t arg)
{
    if (!s || !hulls || !hulls->player) return -1;
    s->cell_class = 0;                                              /* 0x1A72BC: 0x700030CA */
    s->record_cell = 1;                                             /* 0x1A72C8: 0x700031D0 = D_700030B0 */
    Segment g;
    if (segment_of(s, &g)) return -1;
    const EmActor *p = hulls->player;                               /* s7 = D_008102B0 */
    if (!p->w58) return 0;                                          /* 0x1A733C: +0x58 */
    if (!p->status) return 0;                                       /* 0x1A7348: +0x00 (any bit) */
    EmCollHullChain c;
    if (chain_of(hulls, p, &c)) return -1;
    uint8_t mask[3];
    face_mask_of(p, mask);
    const int hit = walk_chain(LOCK_7280, &c, mask, arg, &g, s);
    if (hit < 0) return -1;
    /* 0x1A77F8..0x1A7808: once a record was accepted, every later record
     * (the accepted one included) stores the player in 0x700031D4. */
    if (hit) s->entity = p;
    return hit;                                                     /* sp+E0 */
}
