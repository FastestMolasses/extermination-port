/* em_coll_list_passes_walkers.c - the lane-L08 collision walkers (see the
 * header and docs/COLL_LIST_PASSES.md).
 *
 * Every routine names the original it translates; comments cite the
 * original address of each branch, call and store. 0019E280 and 0019B7D0
 * follow their byte-matched C; 0019E930, 001A3980, 0019BA80 and 0019F330
 * follow the .s. */
#include "game/em_coll_list_passes_walkers.h"

#include <string.h>

#include "game/em_ee_float.h"

/* ---- Byte access (little-endian original layout) ------------------------- */

static float rd_f(const uint8_t *p, size_t off) { float v; memcpy(&v, p + off, 4); return v; }
static uint32_t rd_u32(const uint8_t *p, size_t off) { uint32_t v; memcpy(&v, p + off, 4); return v; }
static uint16_t rd_u16(const uint8_t *p, size_t off) { uint16_t v; memcpy(&v, p + off, 2); return v; }
static int16_t rd_s16(const uint8_t *p, size_t off) { int16_t v; memcpy(&v, p + off, 2); return v; }

static float f_bits(uint32_t bits) { return em_ee_float(bits); }

#define ONE_BITS 0x3F800000u
#define NUDGE_POS 0x3A83126Fu   /* +0.001: 0019BA80 0x19BB0C */
#define NUDGE_NEG 0xBA83126Fu   /* -0.001: 0019BA80 0x19BB20 */
#define EPS_EDGE 0x3727C5ACu    /* +1e-5: 0019F330 0x19F500 */
#define FLAT_LIMIT 0x38D1B717u  /* 1e-4: 0019F330 0x19F584 */
#define FLAT_RATIO 0x7F7FC99Eu  /* 0019F330 0x19F5A4: the ratio of a flat plane */
#define HALF_PI 0x3FC90FDBu     /* pi/2: 0019F330 0x19F5FC, 0x19F628 */

/* ---- The SDK vector routines 0019F330 calls (VU0 macro) ------------------ */

static int vu(em_vu_op op, unsigned dest, int bc, const float fs[4], const float ft[4], float dst[4])
{
    uint32_t s[4], t[4], d[4];
    memcpy(s, fs, sizeof s);
    memcpy(t, ft, sizeof t);
    memcpy(d, dst, sizeof d);
    if (em_vu_vec_bits(op, dest, bc, s, t, 0, NULL, d) != EM_EE_FLOAT_OK) return -1;
    memcpy(dst, d, sizeof d);
    return 0;
}

/* 001028D0(out, a, b): out = a - b, four lanes. */
static int sdk_sub(float out[4], const float a[4], const float b[4])
{
    float r[4] = { 0 };
    if (vu(EM_VU_SUB, 0xF, EM_VU_NO_BC, a, b, r)) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}

/* 001028B8(out, a, b): out = a + b, four lanes. */
static int sdk_add(float out[4], const float a[4], const float b[4])
{
    float r[4] = { 0 };
    if (vu(EM_VU_ADD, 0xF, EM_VU_NO_BC, a, b, r)) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}

/* 00102738(a, b): the xyz product, then x + y, then + z (the x lane). */
static int sdk_dot(float *out, const float a[4], const float b[4])
{
    float v[4];
    memcpy(v, b, sizeof v);
    if (vu(EM_VU_MUL, 0xE, EM_VU_NO_BC, a, v, v)) return -1;
    if (vu(EM_VU_ADDBC, 0x8, 1, v, v, v)) return -1;
    if (vu(EM_VU_ADDBC, 0x8, 2, v, v, v)) return -1;
    *out = v[0];
    return 0;
}

/* 00103230(out, v, t): out.xyz = v.xyz * t (out.w keeps v.w). */
static int sdk_scale(float out[4], const float v[4], float t)
{
    float r[4], q[4] = { t, t, t, t };
    memcpy(r, v, sizeof r);
    if (vu(EM_VU_MULBC, 0xE, 0, r, q, r)) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}

/* ---- The grid ------------------------------------------------------------- */

static int grid_ready(const EmCollProbeGrid *g)
{
    return g && g->emcl && g->tables && g->words && g->count;
}

static const EmCollPoly *grid_node(const EmCollProbeGrid *g, int node)
{
    return &g->emcl->polys[g->first + (uint32_t)node];
}

/* Entry `at` of rank table k (0..5: *(0x70003210 + 4k), 6..11: the helper
 * tables *(0x70003228 + 4(k - 6))), or -1 for an index outside it. */
static int table_entry(const EmCollProbeGrid *g, int k, int32_t at, int16_t *out)
{
    if (at < 0 || (uint32_t)at >= g->count) return -1;
    *out = g->tables[(size_t)k * g->count + (uint32_t)at];
    return 0;
}

/* A rank-table walk [lo, hi) must stay inside the table (the original
 * would read the neighbouring table). */
static int span_valid(const EmCollProbeGrid *g, int32_t lo, int32_t hi)
{
    return !(lo < hi && (lo < 0 || hi > (int32_t)g->count));
}

/* ---- 0019E280 ---------------------------------------------------------------- */

int em_coll_list_passes_0019E280(const EmCollProbeGrid *g, EmCollProbeState *s)
{
    if (!grid_ready(g) || !s) return -1;
    unsigned mask_a, mask_b;
    if (!em_ee_c_le(s->start[0], s->end[0])) { mask_a = 1; mask_b = 2; }
    else { mask_a = 2; mask_b = 1; }
    if (!em_ee_c_le(s->start[1], s->end[1])) { mask_a |= 4; mask_b |= 8; }
    else { mask_a |= 8; mask_b |= 4; }
    if (!em_ee_c_le(s->start[2], s->end[2])) { mask_a |= 0x10; mask_b |= 0x20; }
    else { mask_a |= 0x20; mask_b |= 0x10; }
    if (em_coll_probe_0019F1A0(g, s, s->start, mask_b)) return -1;   /* the D_70003190 pair, */
    if (em_coll_probe_0019F1A0(g, s, s->end, mask_a)) return -1;     /* then D_700031A0 */
    int32_t first[6];
    for (int i = 0; i < 6; ++i) first[i] = s->rank[i];                /* sp80 = D_70003240 */
    if (em_coll_probe_0019F1A0(g, s, s->start, mask_a)) return -1;
    if (em_coll_probe_0019F1A0(g, s, s->end, mask_b)) return -1;
    int32_t best = (int32_t)g->count;                                 /* *(0x7000320C) */
    int32_t lo = 0, hi = 0;
    int cand = 0, picked = 0;
    for (int i = 0; i < 6; ++i) {                                     /* all six directions */
        int16_t helper;
        if (table_entry(g, 6 + i, first[i], &helper)) return -1;      /* D_70003228[i][sp80[i]] */
        if (i & 1) {
            s->span_lo = s->rank[i];                                  /* 0x70003B86 */
            s->span_hi = helper;                                      /* 0x70003B88 */
        } else {
            s->span_lo = helper;
            s->span_hi = s->rank[i];
            s->span_hi = (int16_t)(s->span_hi + 1);
        }
        int32_t diff = (int32_t)s->span_hi - (int32_t)s->span_lo;
        if (diff < best) {
            best = diff;
            lo = s->span_lo;
            hi = s->span_hi;
            cand = i;
            picked = 1;
        }
    }
    /* No direction beat the node count: cand, lo and hi are uninitialized
     * registers in the original. */
    if (!picked || !span_valid(g, lo, hi)) return -1;
    int found = -1;                                                   /* hitfound = 0 */
    float saved[3] = { 0.0f, 0.0f, 0.0f };                            /* spA0 */
    for (int32_t k = lo; k < hi; ++k) {
        int16_t node;
        if (table_entry(g, cand, k, &node)) return -1;                /* D_70003210[cand][lo] */
        if (node < 0 || (uint32_t)node >= g->count) return -1;
        if (grid_node(g, node)->attr != 0x78) continue;               /* node +0x1A */
        const int16_t *w = g->words + 12 * node;
        if (!(w[6] <= s->rank[0])) continue;                          /* +0x0C <= 0x70003240 */
        if (!(w[7] >= s->rank[1])) continue;                          /* +0x0E >= 0x70003242 */
        if (!(w[10] <= s->rank[4])) continue;                         /* +0x14 <= 0x70003248 */
        if (!(w[11] >= s->rank[5])) continue;                         /* +0x16 >= 0x7000324A */
        if (!(w[8] <= s->rank[2])) continue;                          /* +0x10 <= 0x70003244 */
        if (!(w[9] >= s->rank[3])) continue;                          /* +0x12 >= 0x70003246 */
        int r = em_coll_probe_0019ED80(g, s, node);                   /* 0019ED80(D_70003190, node) */
        if (r < 0) return -1;
        if (!r) continue;
        for (int j = 0; j < 3; ++j) {                                 /* +0x10 = +0x20, spA0 = +0x20 */
            s->end[j] = s->point[j];
            saved[j] = s->point[j];
        }
        found = node;                                                 /* hitfound = *0x700031D0 */
    }
    if (found < 0) return 0;
    memcpy(s->point, saved, sizeof saved);                            /* +0x20 = spA0 */
    s->record = EM_COLL_PROBE_RECORD_GRID;                            /* *0x700031D0 = hitfound */
    s->node = found;
    return 1;
}

/* ---- 0019E930 ---------------------------------------------------------------- */

int em_coll_list_passes_0019E930(const EmCollProbeGrid *g, EmCollProbeState *s)
{
    if (!grid_ready(g) || !s) return -1;
    int32_t y_pair[2];                                                /* sp58 / sp5C */
    if (!em_ee_c_le(s->start[1], s->end[1])) {                        /* 0x19E958, 0x19E960 */
        if (em_coll_probe_0019F1A0(g, s, s->start, 8)) return -1;     /* 0x19E970 */
        if (em_coll_probe_0019F1A0(g, s, s->end, 4)) return -1;       /* 0x19E980 */
        y_pair[0] = s->rank[2];                                       /* 0x19E98C, 0x19E9A4 */
        y_pair[1] = s->rank[3];                                       /* 0x19E9A0, 0x19E9AC */
        if (em_coll_probe_0019F1A0(g, s, s->start, 0x37)) return -1;  /* 0x19E9A8 */
        if (em_coll_probe_0019F1A0(g, s, s->end, 8)) return -1;       /* 0x19E9B8 */
    } else {
        if (em_coll_probe_0019F1A0(g, s, s->start, 4)) return -1;     /* 0x19E9D0 */
        if (em_coll_probe_0019F1A0(g, s, s->end, 8)) return -1;       /* 0x19E9E0 */
        y_pair[0] = s->rank[2];                                       /* 0x19E9EC, 0x19EA04 */
        y_pair[1] = s->rank[3];                                       /* 0x19EA00, 0x19EA0C */
        if (em_coll_probe_0019F1A0(g, s, s->start, 8)) return -1;     /* 0x19EA08 */
        if (em_coll_probe_0019F1A0(g, s, s->end, 0x37)) return -1;    /* 0x19EA18 */
    }
    int32_t best = (int32_t)g->count;                                 /* 0x19EA24: 0x7000320C */
    int32_t lo = 0, hi = 0;
    int cand = 0, picked = 0;
    for (int i = 0; i < 6; ++i) {                                     /* 0x19EA40 .. 0x19EB88 */
        int16_t helper;
        if (i == 2 || i == 3) {                                       /* 0x19EA40, 0x19EA4C */
            /* The helper index is the rank the first 0019F1A0 pair left. */
            if (table_entry(g, 6 + i, y_pair[i - 2], &helper)) return -1;
            if (i & 1) {                                              /* 0x19EA5C */
                s->span_lo = s->rank[i];                              /* 0x19EA6C */
                s->span_hi = helper;                                  /* 0x19EA8C */
            } else {
                s->span_lo = helper;                                  /* 0x19EAA8 */
                s->span_hi = s->rank[i];                              /* 0x19EAB4 */
                s->span_hi = (int16_t)(s->span_hi + 1);               /* 0x19EACC */
            }
        } else if (i & 1) {                                           /* 0x19EAD4 */
            /* The helper index is the rank itself, re-read as stored. */
            s->span_lo = s->rank[i];                                  /* 0x19EAE4 */
            if (table_entry(g, 6 + i, s->span_lo, &helper)) return -1;
            s->span_hi = helper;                                      /* 0x19EB08 */
        } else {
            s->span_hi = s->rank[i];                                  /* 0x19EB14 */
            if (table_entry(g, 6 + i, s->span_hi, &helper)) return -1;
            s->span_lo = helper;                                      /* 0x19EB34 */
            s->span_hi = (int16_t)(s->span_hi + 1);                   /* 0x19EB40 */
        }
        int32_t diff = (int32_t)s->span_hi - (int32_t)s->span_lo;     /* 0x19EB58 */
        if (diff < best) {                                            /* 0x19EB5C, 0x19EB60 */
            best = diff;
            lo = s->span_lo;
            hi = s->span_hi;
            cand = i;
            picked = 1;
        }
    }
    if (s->rank[2] == -1) return 1;                                   /* 0x19EB94..0x19EBA8: halfword */
    if (!picked || !span_valid(g, lo, hi)) return -1;
    int found = -1;                                                   /* s1 = 0 */
    float saved[3] = { 0.0f, 0.0f, 0.0f };                            /* sp70 */
    for (int32_t k = lo; k < hi; ++k) {                               /* 0x19EBC8, 0x19ED08 */
        int16_t node;
        if (table_entry(g, cand, k, &node)) return -1;                /* 0x19EBD0 */
        if (node < 0 || (uint32_t)node >= g->count) return -1;
        const int16_t *w = g->words + 12 * node;
        if (s->rank[0] < w[6]) continue;                              /* 0x19EBF4: +0x0C */
        if (w[7] < s->rank[1]) continue;                              /* 0x19EC0C: +0x0E */
        if (s->rank[2] < w[8]) continue;                              /* 0x19EC24: +0x10 */
        if (w[9] < s->rank[3]) continue;                              /* 0x19EC3C: +0x12 */
        if (s->rank[4] < w[10]) continue;                             /* 0x19EC54: +0x14 */
        if (w[11] < s->rank[5]) continue;                             /* 0x19EC6C: +0x16 */
        const uint8_t attr = grid_node(g, node)->attr;                /* 0x19EC74: +0x1A */
        s->span_hi = attr;                                            /* 0x19EC7C: 0x70003B88 */
        if (attr < 0x1E || attr >= 0x5A) continue;                   /* 0x19EC8C, 0x19EC98 */
        int r = em_coll_probe_0019ED80(g, s, node);                   /* 0x19ECA4 */
        if (r < 0) return -1;
        if (!r) continue;                                             /* 0x19ECAC */
        s->end[1] = s->point[1];                                      /* 0x19ECD0: 0x700031A4 = 0x700031B4 */
        memcpy(saved, s->point, sizeof saved);                        /* 0x19ECD8 loop */
        found = node;                                                 /* 0x19ECF8: s1 = 0x700031D0 */
    }
    if (found < 0) return 1;                                          /* 0x19ED10 */
    s->record = EM_COLL_PROBE_RECORD_GRID;                            /* 0x19ED28 */
    s->node = found;
    memcpy(s->point, saved, sizeof saved);                            /* 0x19ED30 loop */
    return 0;
}

/* ---- 001A3980 ---------------------------------------------------------------- */

/* The prims of one hull (count at +0x18, prims from +0x1C), tested until
 * one hits. `pass2` sends 0x8000 prims to 001A44B0 (0x1A3E94); pass 1 only
 * steps over them (0x1A3B90). An unknown type nibble is neither tested nor
 * stepped over (0x1A3B84 / 0x1A3E88), so the same bytes are read again. */
static int hull_prims(const EmActorCellTable *t, uint32_t hull, int pass2, EmCollProbeState *s, int *hit)
{
    if ((uint64_t)hull + 0x1C > t->size) return -1;
    uint32_t at = hull + 0x1C;                                        /* 0x1A3B3C / 0x1A3E40 */
    *hit = 0;                                                         /* 0x1A3B40 / 0x1A3E44 */
    for (int16_t j = 0; j < rd_s16(t->bytes, hull + 0x18); ++j) {     /* 0x1A3C50 / 0x1A3F68 */
        if ((uint64_t)at + 4 > t->size) return -1;
        const uint8_t *p = t->bytes + at;
        const uint16_t h = rd_u16(p, 0);
        uint32_t size;
        switch (h & 0xF000) {
        case 0x1000: size = (h & 0x800) ? 0x24u + 0x30u * p[2] : 0x14u + 0x18u * p[2]; break;
        case 0x2000: size = 0x1C; break;
        case 0x4000: size = (h & 0x800) ? 0x2C : 0x18; break;
        case 0x8000: size = (h & 0x800) ? 0x24 : 0x14; break;
        default: continue;                                            /* no call, no step */
        }
        if ((uint64_t)at + size > t->size) return -1;
        int r = 0;
        switch (h & 0xF000) {
        case 0x1000: r = em_coll_probe_001A4030(p, s); break;         /* 0x1A3BF4 / 0x1A3F08 */
        case 0x2000: r = em_coll_probe_001A4650(p, s); break;         /* 0x1A3BE0 / 0x1A3EF4 */
        case 0x4000: r = em_coll_probe_001A44B0(p, s); break;         /* 0x1A3BB0 / 0x1A3EC4 */
        case 0x8000: if (pass2) r = em_coll_probe_001A44B0(p, s); break;  /* 0x1A3E94 */
        }
        if (r < 0) return -1;
        *hit = r;
        at += size;
        if (r) break;                                                 /* 0x1A3C40 / 0x1A3F58 */
    }
    return 0;
}

/* The hull AABB gate both passes apply (0x1A3AB0..0x1A3B30,
 * 0x1A3DB4..0x1A3E34). */
static int hull_gate(const uint8_t *hb, float ymin, float ymax, const EmCollProbeState *s)
{
    if (em_ee_c_lt(ymax, rd_f(hb, 4))) return 0;
    if (!em_ee_c_le(ymin, rd_f(hb, 0x10))) return 0;
    if (em_ee_c_lt(s->start[0], rd_f(hb, 0))) return 0;
    if (!em_ee_c_le(s->start[0], rd_f(hb, 0xC))) return 0;
    if (em_ee_c_lt(s->start[2], rd_f(hb, 8))) return 0;
    if (!em_ee_c_le(s->start[2], rd_f(hb, 0x14))) return 0;
    return 1;
}

/* Pass 1's hull offset: the low 30 bits of the word (0x1A3AA4); a
 * 0x20000000-flagged word reads through the EE's uncached main-RAM mirror,
 * the same bytes as tbl + offset. */
static uint32_t static_hull(uint32_t word)
{
    uint32_t off = word & 0x3FFFFFFFu;
    return off & 0x20000000u ? off & 0x1FFFFFFFu : off;
}

int em_coll_list_passes_001A3980(const EmCollProbeWorld *world, EmCollProbeState *s)
{
    if (!world || !world->cells || !world->cells->table || !world->cells->table->bytes ||
        !world->cells->lists || !s) return -1;
    const EmActorCollisionWorld *w = world->cells;
    const EmActorCellTable *t = w->table;
    s->record = EM_COLL_PROBE_RECORD_CELL;                            /* 0x1A39B8: 0x700031D0 = D_700030B0 */
    s->node = -1;
    float ymax = s->start[1], ymin;                                   /* 0x1A39C0: f21 */
    if (em_ee_c_le(s->start[1], s->end[1])) { ymin = s->start[1]; ymax = s->end[1]; }  /* 0x1A39E4 */
    else ymin = s->end[1];                                            /* 0x1A39E0 */
    int result = 1;                                                   /* 0x1A39D8: s2 = 1 */
    for (int i = 0; i < t->count; ++i) {                              /* pass 1, 0x1A3D08 */
        if ((uint64_t)4 + 4u * (unsigned)i + 4 > t->size) return -1;
        const uint32_t word = rd_u32(t->bytes, 4 + 4u * (unsigned)i);  /* 0x1A3A10 */
        if (!(word & 0x80000000u)) break;                             /* 0x1A3A18 */
        if (word & 0x40000000u) continue;                             /* 0x1A3A28 */
        if ((word & 0x20000000u) && s->query_class == 0) continue;    /* 0x1A3A48 */
        if (!w->static_kind || (unsigned)i >= w->static_kind_count) return -1;
        const uint8_t kind = w->static_kind[i];                       /* 0x1A3A84: D_0024D7C0[..][..] +8 */
        if (kind < 0x1E || kind >= 0x5A) continue;                    /* 0x1A3A8C, 0x1A3A98 */
        const uint32_t hull = static_hull(word);                      /* 0x1A3AAC */
        if ((uint64_t)hull + 0x18 > t->size) return -1;
        if (!hull_gate(t->bytes + hull, ymin, ymax, s)) continue;
        int hit;
        if (hull_prims(t, hull, 0, s, &hit)) return -1;
        if (!hit) continue;                                           /* 0x1A3C60 */
        result = 0;                                                   /* 0x1A3C78 */
        s->end[1] = s->point[1];                                      /* 0x1A3CA4 */
        s->entity = NULL;                                             /* 0x1A3CAC */
        s->cell_class = (uint16_t)((s->cell_class & 0xFF00) | kind);  /* 0x1A3CE8 */
        if (em_ee_c_le(s->start[1], s->end[1])) ymax = s->end[1];     /* 0x1A3CF4 */
        else ymin = s->end[1];                                        /* 0x1A3CF0 */
    }
    const EmActorClassList *list = &w->lists->list[EM_ACTOR_LIST_CLASS4];
    for (int j = 0; j < list->published; ++j) {                       /* pass 2, 0x1A3FE8 */
        const EmActor *a = em_actor_class_list_entry(w->lists, EM_ACTOR_LIST_CLASS4, j);  /* 0x1A3D2C */
        if (!a) return -1;
        if (a->status == 0) continue;                                 /* 0x1A3D34 */
        if ((a->cls & 0x1F) != 4) continue;                           /* 0x1A3D48 */
        if (s->self == (const void *)a) continue;                     /* 0x1A3D58 */
        const unsigned uid = (a->uid >> 8) & 0xFF;                    /* 0x1A3D60 */
        if (uid == 0xFF) continue;                                    /* 0x1A3D70 */
        if ((uint64_t)4 + 4u * uid + 4 > t->size) return -1;
        const uint32_t word = rd_u32(t->bytes, 4 + 4u * uid);         /* 0x1A3D88 */
        if (!word) continue;                                          /* 0x1A3D8C */
        if (!((int)uid < t->count)) continue;                         /* 0x1A3DA0 */
        if (word & 0x80000000u) return -1;   /* hull = tbl + word unmasked (0x1A3DB0): outside the image */
        if ((uint64_t)word + 0x18 > t->size) return -1;
        if (!hull_gate(t->bytes + word, ymin, ymax, s)) continue;
        int hit;
        if (hull_prims(t, word, 1, s, &hit)) return -1;
        if (!hit) continue;                                           /* 0x1A3F78 */
        result = 0;                                                   /* 0x1A3F88 */
        s->end[1] = s->point[1];                                      /* 0x1A3F98 */
        s->entity = a;                                                /* 0x1A3FA0 */
        s->cell_class = (uint16_t)((s->cell_class & 0xFF00) | (uint8_t)a->kind);  /* 0x1A3FAC..0x1A3FCC */
        if (em_ee_c_le(s->start[1], s->end[1])) ymax = s->end[1];     /* 0x1A3FD8 */
        else ymin = s->end[1];                                        /* 0x1A3FD0 */
    }
    return result;
}

/* ---- The queries ------------------------------------------------------------- */

int em_coll_list_passes_0019B7D0(const EmCollProbeGrid *grid, EmCollProbeState *state,
                                 const float from[3], const float to[3])
{
    if (!grid || !state || !from || !to) return -1;
    EmCollProbeState s = *state;
    float saved[3];
    for (int k = 0; k < 3; ++k) {                                     /* 0x19B7F8 loop */
        s.start[k] = from[k];
        saved[k] = to[k];
        s.end[k] = to[k];
    }
    s.end[3] = f_bits(ONE_BITS);                                      /* 0x19B830: 0x700031AC */
    s.start[3] = f_bits(ONE_BITS);                                    /* 0x19B838: 0x7000319C */
    s.entity = NULL;                                                  /* 0x19B848: 0x700031D4 */
    int hit = em_coll_list_passes_0019E280(grid, &s);                 /* 0x19B844 */
    if (hit < 0) return -1;
    const int r = hit ? 4 : 0;                                        /* 0x19B854 */
    if (r) memcpy(s.end, saved, sizeof saved);                        /* 0x19B86C loop */
    else { s.record = EM_COLL_PROBE_RECORD_NONE; s.node = -1; }       /* 0x19B894 */
    s.kind = r;                                                       /* 0x19B89C: 0x700031D8 */
    *state = s;
    return r;
}

int em_coll_list_passes_0019BA80(const EmCollProbeWorld *world, EmCollProbeState *state,
                                 const void *self, uint8_t cls, const float point[3],
                                 const float box[3], unsigned mask)
{
    if (!world || !state || !point || !box) return -1;
    if ((mask & 4) && !grid_ready(world->grid)) return -1;
    EmCollProbeState s = *state;
    float saved[3];
    for (int k = 0; k < 3; ++k) {                                     /* 0x19BAB4 loop */
        s.end[k] = point[k];
        s.start[k] = point[k];
        saved[k] = point[k];
    }
    s.start[1] = em_ee_sub(s.start[1], box[1]);                       /* 0x19BAF0 */
    const float nudge = em_ee_c_lt(box[1], 0.0f) ? f_bits(NUDGE_POS) : f_bits(NUDGE_NEG); /* 0x19BAFC */
    s.start[1] = em_ee_add(s.start[1], nudge);                        /* 0x19BB38 */
    s.end[3] = 0.0f;                                                  /* 0x19BB48: 0x700031AC = 0 */
    s.start[3] = 0.0f;                                                /* 0x19BB50: 0x7000319C = 0 */
    s.entity = NULL;                                                  /* 0x19BB58: 0x700031D4 */
    s.query_class = (int16_t)(cls & 0x1F);                            /* 0x19BB6C: 0x7000324E */
    int r = 0;
    if (mask & 2) {                                                   /* 0x19BB68 */
        s.self = self;                                                /* 0x19BB7C: 0x70003254 = +0x14 */
        int clear = em_coll_list_passes_001A3980(world, &s);          /* 0x19BB78 */
        if (clear < 0) return -1;
        if (!clear) r = 2;                                            /* 0x19BB88 */
    }
    if (mask & 4) {                                                   /* 0x19BB90 */
        int clear = em_coll_list_passes_0019E930(world->grid, &s);    /* 0x19BB98 */
        if (clear < 0) return -1;
        if (!clear) r = 4;                                            /* 0x19BBA8 */
    }
    s.start[1] = em_ee_sub(s.start[1], nudge);                        /* 0x19BBB4 */
    if (r) {
        for (int k = 0; k < 3; ++k) {                                 /* 0x19BBD0 loop */
            s.end[k] = saved[k];
            s.delta[k] = em_ee_sub(s.point[k], s.end[k]);             /* 0x19BBEC: 0x700031C0 */
        }
    } else {
        s.record = EM_COLL_PROBE_RECORD_NONE;                         /* 0x19BC08 */
        s.node = -1;
    }
    s.kind = r;                                                       /* 0x19BC10 */
    *state = s;
    return r;
}

/* ---- 0019F330 ---------------------------------------------------------------- */

int em_coll_list_passes_0019F330(const EmCollProbeGrid *g, const EmSdkMathContext *math,
                                 EmCollProbeState *s, float *s3684, const float a[3],
                                 const float b[3], int node, float q[4])
{
    if (!grid_ready(g) || !math || !math->tables || !s || !s3684 || !a || !b || !q) return -1;
    if (node < 0 || (uint32_t)node >= g->count) return -1;
    const EmCollision *c = g->emcl;
    const EmCollPoly *p = grid_node(g, node);
    float va[4] = { a[0], a[1], a[2], 0.0f };                         /* 0x19F374 loop, 0x19F3AC: spAC = 0 */
    float vb[4] = { b[0], b[1], b[2], 0.0f };                         /* 0x19F3B4: spBC = 0 */
    float dir[4];
    if (sdk_sub(dir, vb, va)) return -1;                              /* 0x19F3B0: spF0 = b - a */
    float n[4] = { p->plane[0], p->plane[1], p->plane[2], 0.0f };     /* 0x19F3C4 loop, 0x19F3E0 */
    const float d = p->plane[3];                                      /* 0x19F3E4: node +0x30 */
    float along, na;
    if (sdk_dot(&along, dir, n)) return -1;                           /* 0x19F3EC */
    if (sdk_dot(&na, n, va)) return -1;                               /* 0x19F3FC */
    const float t = em_ee_div(em_ee_sub(d, na), along);               /* 0x19F404, 0x19F410 */
    float hit[4];
    if (sdk_scale(hit, dir, t)) return -1;                            /* 0x19F41C: spC0 = dir * t */
    if (sdk_add(hit, va, hit)) return -1;                             /* 0x19F42C: spC0 = a + spC0 */
    /* The ring: node +0x1C indexes the vertex-index pool (0x70003204),
     * node +0x20 the edge-normal pool (0x70003200); the EMCL ring carries
     * the same values (tools/export_collision.py checks them). */
    for (unsigned k = 0; k < p->vcount; ++k) {                        /* 0x19F538: node +0x18 */
        const uint32_t vi = c->indices[p->first + k];
        if (vi >= c->vert_count) return -1;
        const float *v = c->verts + 3u * vi;                          /* 0x19F45C..0x19F4CC */
        const float *e = c->edge_n + 3u * (p->first + k);             /* 0x19F4D8..0x19F4F0 */
        float vv[4] = { v[0], v[1], v[2], 0.0f }, rel[4];             /* 0x19F4D4: spEC = 0 */
        float ee[4] = { e[0], e[1], e[2], 0.0f };                     /* 0x19F4FC: spEC = 0 */
        float dot;
        if (sdk_sub(rel, hit, vv)) return -1;                         /* 0x19F4D0: spD0 = spC0 - spE0 */
        if (sdk_dot(&dot, rel, ee)) return -1;                        /* 0x19F4F8 */
        if (!em_ee_c_le(dot, f_bits(EPS_EDGE))) return 0;             /* 0x19F510, 0x19F520 */
    }
    for (int k = 0; k < 3; ++k) q[k] = hit[k];                        /* 0x19F554 loop */
    const float r2 = em_ee_madd(em_ee_mula(n[0], n[0]), n[2], n[2]);  /* 0x19F578, 0x19F580 */
    float h;
    uint32_t fault = 0;
    if (em_sdk_math_original_0011E748(math->tables, &math->world, &math->workers, r2, &h, &fault) < 0)
        return -1;                                                    /* 0x19F57C */
    s->ratio = h;                                                     /* 0x19F5A0: 0x70003680 */
    if (em_ee_c_lt(h, f_bits(FLAT_LIMIT))) {                          /* 0x19F594 */
        *s3684 = f_bits(FLAT_RATIO);                                  /* 0x19F5B4: 0x70003684 */
    } else {
        const float ay = em_sdk_math_original_0011DF78(n[1]);         /* 0x19F5B8: fabsf(spF0 + 0x14) */
        *s3684 = em_ee_div(ay, s->ratio);                             /* 0x19F5C8, 0x19F5D0 */
    }
    float angle;
    if (em_sdk_math_original_0011DBB8(math->tables, *s3684, &angle, &fault) < 0) return -1;  /* 0x19F5F4 / 0x19F620 */
    const float slope = em_ee_sub(f_bits(HALF_PI), angle);            /* 0x19F60C / 0x19F638 */
    if (em_ee_c_lt(n[1], 0.0f)) q[3] = em_ee_neg(slope);              /* 0x19F5E0, 0x19F610 */
    else q[3] = slope;                                                /* 0x19F63C */
    return 1;
}

/* ---- Binding adapter ---------------------------------------------------------- */

int em_coll_list_passes_camera_ground(void *context, const uint32_t from[4], const uint32_t to[4],
                                      int *result)
{
    EmCollListPassesGround *g = context;
    if (!g || !g->grid || !g->state || !from || !to || !result) return -1;
    float a[3], b[3];
    for (int k = 0; k < 3; ++k) { a[k] = f_bits(from[k]); b[k] = f_bits(to[k]); }
    const int r = em_coll_list_passes_0019B7D0(g->grid, g->state, a, b);
    if (r < 0) return -1;
    *result = r;
    return 0;
}
