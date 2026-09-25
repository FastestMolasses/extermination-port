/* em_coll_probe_original.c - the floor service's surface and object probes
 * (see the header and docs/COLL_PROBES.md).
 *
 * Every routine names the original it translates; comments cite the
 * original address of each branch and store. The comparisons, their
 * directions and the operation order follow the .s (the readable C of the
 * NEARMISS files is not trusted: 001A2AE0's C inverts both kind gates and
 * sends 0x8000 prims to 001A44B0 in pass 1, and 0019DF10's C swaps the
 * masks of its first 0019F1A0 pair). */
#include "game/em_coll_probe_original.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_ee_float.h"

/* ---- Byte access (little-endian original layout) ------------------------- */

static float rd_f(const uint8_t *p, size_t off) { float v; memcpy(&v, p + off, 4); return v; }
static uint32_t rd_u32(const uint8_t *p, size_t off) { uint32_t v; memcpy(&v, p + off, 4); return v; }
static uint16_t rd_u16(const uint8_t *p, size_t off) { uint16_t v; memcpy(&v, p + off, 2); return v; }
static int16_t rd_s16(const uint8_t *p, size_t off) { int16_t v; memcpy(&v, p + off, 2); return v; }

static float f_bits(uint32_t bits) { return em_ee_float(bits); }

#define EPS_FRONT 0xB727C5ACu   /* -1e-5: 0019ED80 0x19EE3C, 001A4030 0x1A40E4 */
#define EPS_EDGE  0x3727C5ACu   /* +1e-5: 0019ED80 0x19F0F0, 001A4030 0x1A42C4 */
#define NUDGE_POS 0x3A83126Fu   /* +0.001: 0019B8C0 0x19B94C */
#define NUDGE_NEG 0xBA83126Fu   /* -0.001: 0019B8C0 0x19B960 */
#define RATIO_WALL 0x3EFB075Eu  /* 0.49029058: 001A4030 0x1A4378 */
#define RATIO_FLOOR 0x40400000u /* 3.0: 001A4030 0x1A4394 */
#define ONE_BITS 0x3F800000u
#define MINUS_ONE_BITS 0xBF800000u

/* ---- The SDK vector routines 0019ED80 and 001A4030 call (VU0 macro) ------- */

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

/* 001028D0(out, a, b): vsub.xyzw out = a - b. */
static int sdk_sub(float out[4], const float a[4], const float b[4])
{
    float r[4] = { 0 };
    if (vu(EM_VU_SUB, 0xF, EM_VU_NO_BC, a, b, r)) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}

/* 001028B8(out, a, b): vadd.xyzw out = a + b. */
static int sdk_add(float out[4], const float a[4], const float b[4])
{
    float r[4] = { 0 };
    if (vu(EM_VU_ADD, 0xF, EM_VU_NO_BC, a, b, r)) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}

/* 00102738(a, b): the three-lane product a * b, then its y and z lanes
 * summed into x; the x lane. */
static int sdk_dot(float *out, const float a[4], const float b[4])
{
    float v[4];
    memcpy(v, b, sizeof v);
    if (vu(EM_VU_MUL, 0xE, EM_VU_NO_BC, a, v, v)) return -1;   /* 0x102740 */
    if (vu(EM_VU_ADDBC, 0x8, 1, v, v, v)) return -1;            /* 0x102744 */
    if (vu(EM_VU_ADDBC, 0x8, 2, v, v, v)) return -1;            /* 0x102748 */
    *out = v[0];
    return 0;
}

/* 00103230(out, v, t): the three lanes of v scaled by t (w keeps v.w). */
static int sdk_scale(float out[4], const float v[4], float t)
{
    float r[4], q[4] = { t, t, t, t };   /* only lane x (the broadcast) is read */
    memcpy(r, v, sizeof r);
    if (vu(EM_VU_MULBC, 0xE, 0, r, q, r)) return -1;            /* 0x10323C */
    memcpy(out, r, sizeof r);
    return 0;
}

/* ---- The grid rank view --------------------------------------------------- */

int em_coll_probe_sdk_sub(float out[4], const float a[4], const float b[4]) { return sdk_sub(out, a, b); }
int em_coll_probe_sdk_add(float out[4], const float a[4], const float b[4]) { return sdk_add(out, a, b); }
int em_coll_probe_sdk_dot(float *out, const float a[4], const float b[4]) { return sdk_dot(out, a, b); }
int em_coll_probe_sdk_scale(float out[4], const float v[4], float t) { return sdk_scale(out, v, t); }

void em_coll_probe_grid_free(EmCollProbeGrid *grid)
{
    if (!grid) return;
    free(grid->blob);
    memset(grid, 0, sizeof *grid);
}

int em_coll_probe_grid_init(EmCollProbeGrid *out, const EmCollision *grid, const void *emcl,
                            size_t size)
{
    if (!out || !grid || !grid->blob || !emcl) return -1;
    memset(out, 0, sizeof *out);
    const uint8_t *b = emcl;
    if (size < 0x30 || memcmp(b, "EMCL", 4) || rd_u32(b, 4) != 1) return -1;
    uint32_t verts = rd_u32(b, 8), polys = rd_u32(b, 12), indices = rd_u32(b, 16), flags = rd_u32(b, 20);
    const uint32_t need = EM_COLL_FLAG_NODE_CLASS | EM_COLL_PROBE_FLAG_RANKS;
    if ((flags & need) != need || (grid->flags & need) != need) return -1;
    if (verts != grid->vert_count || polys != grid->poly_count || indices != grid->index_count) return -1;
    /* The section follows the edge normals (em_collision_load's layout). */
    uint64_t at = 0x30 + (uint64_t)verts * 12 + (uint64_t)polys * sizeof(EmCollPoly);
    at += (uint64_t)indices * 2;
    at = (at + 3) & ~(uint64_t)3;
    at += (uint64_t)indices * 12;
    if (at + 20 > size || memcmp(b + at, "EMRK", 4) || rd_u32(b, at + 4) != 1) return -1;
    uint32_t count = rd_u32(b, at + 8), first = rd_u32(b, at + 12), vcount = rd_u32(b, at + 16);
    if (count == 0 || count > 0x7FFF || vcount == 0 || vcount > 0x7FFF) return -1;
    if ((uint64_t)first + count > polys) return -1;
    uint64_t body = (uint64_t)vcount * 12 + (uint64_t)count * 24 + (uint64_t)count * 24;
    if (at + 20 + body > size) return -1;
    for (uint32_t i = 0; i < count; ++i)
        if (grid->polys[first + i].set != EM_COLL_SET_GRID) return -1;
    /* The axis section (flags bit 3) follows the rank section, which the
     * exporter pads to a 4-byte boundary. */
    uint64_t axis_at = (at + 20 + body + 3) & ~(uint64_t)3, axis_bytes = 0;
    if (flags & EM_COLL_PROBE_FLAG_AXIS) {
        axis_bytes = (uint64_t)count * 12;
        if (axis_at + 12 + axis_bytes > size || memcmp(b + axis_at, "EMAX", 4) ||
            rd_u32(b, axis_at + 4) != 1 || rd_u32(b, axis_at + 8) != count)
            return -1;
    }
    uint8_t *copy = malloc((size_t)(body + axis_bytes));
    if (!copy) return -1;
    memcpy(copy, b + at + 20, (size_t)body);
    if (axis_bytes) memcpy(copy + body, b + axis_at + 12, (size_t)axis_bytes);
    const float *vp = (const float *)copy;
    const int16_t *words = (const int16_t *)(copy + (size_t)vcount * 12);
    const int16_t *tables = words + (size_t)count * 12;
    for (uint32_t i = 0; i < count; ++i)
        for (unsigned k = 0; k < 6; ++k)
            if (words[12 * i + k] < 0 || (uint32_t)words[12 * i + k] >= vcount) { free(copy); return -1; }
    /* Tables 0..5 are permutations of the node indices; the span helpers
     * 6..11 hold table positions 0..N (N closes a span). */
    for (uint32_t i = 0; i < 12u * count; ++i) {
        uint32_t limit = i < 6u * count ? count - 1 : count;
        if (tables[i] < 0 || (uint32_t)tables[i] > limit) { free(copy); return -1; }
    }
    out->emcl = grid;
    out->count = count;
    out->first = first;
    out->vert_count = vcount;
    out->verts = vp;
    out->words = words;
    out->tables = tables;
    out->axis = axis_bytes ? (const uint32_t *)(copy + body) : NULL;
    out->blob = copy;
    return 0;
}

int em_coll_probe_grid_load(EmCollProbeGrid *out, const EmCollision *grid, const char *path)
{
    if (!out || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return -1; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return -1; }
    fclose(f);
    int r = em_coll_probe_grid_init(out, grid, buf, (size_t)sz);
    free(buf);
    return r;
}

static const EmCollPoly *grid_node(const EmCollProbeGrid *g, int node)
{
    return &g->emcl->polys[g->first + (uint32_t)node];
}

uint16_t em_coll_probe_record_node(const EmCollProbeGrid *grid, const EmCollProbeState *s)
{
    if (!s) return 0;
    if (s->record == EM_COLL_PROBE_RECORD_CELL) return s->cell_class;
    if (s->record == EM_COLL_PROBE_RECORD_GRID && grid && s->node >= 0 && (uint32_t)s->node < grid->count) {
        const EmCollPoly *p = grid_node(grid, s->node);
        return (uint16_t)(p->attr | p->pad << 8);   /* node +0x1A, +0x1B */
    }
    return 0;
}

/* ---- 0019F1A0 ---------------------------------------------------------------- */

int em_coll_probe_0019F1A0(const EmCollProbeGrid *g, EmCollProbeState *s, const float point[3],
                           unsigned mask)
{
    if (!g || !g->tables || !s || !point) return -1;
    const int32_t n = (int32_t)g->count;                        /* 0x19F1D4: 0x7000320C */
    for (int i = 0; i < 6; ++i, mask >>= 1) {                   /* 0x19F1FC .. 0x19F300 */
        if (!(mask & 1u)) continue;                             /* 0x19F200 beql */
        const int16_t *table = g->tables + (size_t)i * (size_t)n;   /* 0x19F20C: *(0x70003210 + 4i) */
        const int axis = i >> 1;                                /* 0x19F208 */
        const float key = point[axis];                          /* 0x19F218 */
        int32_t lo = 0, hi = n >> 1, lim = n;                   /* 0x19F21C / 0x19F220 */
        while (lo < hi) {                                       /* 0x19F224, 0x19F288 */
            int16_t word = g->words[12 * table[hi] + i];        /* 0x19F238..0x19F244: node +2i */
            float v = g->verts[3 * word + axis];                /* 0x19F248..0x19F258 */
            if (em_ee_c_lt(v, key)) lo = hi;                    /* 0x19F25C, 0x19F270 */
            else lim = hi;                                      /* 0x19F268 */
            hi = lo + ((lim - lo) >> 1);                        /* 0x19F278..0x19F284 (bytes & ~1) */
        }
        int16_t word = g->words[12 * table[lo] + i];            /* 0x19F298..0x19F2A8 */
        float v = g->verts[3 * word + axis];                    /* 0x19F2AC..0x19F2C0 */
        s->rank[i] = em_ee_c_le(v, key) ? (int16_t)lo : 0;      /* 0x19F2C4..0x19F2E4 */
    }
    return 0;
}

/* ---- 0019ED80 ----------------------------------------------------------------- */

int em_coll_probe_0019ED80(const EmCollProbeGrid *g, EmCollProbeState *s, int node)
{
    if (!g || !g->emcl || !s || node < 0 || (uint32_t)node >= g->count) return -1;
    const EmCollision *c = g->emcl;
    const EmCollPoly *p = grid_node(g, node);
    float qa[4] = { s->start[0], s->start[1], s->start[2], 0.0f };   /* 0x19EDC0 loop, 0x19EDFC */
    float qb[4] = { s->end[0], s->end[1], s->end[2], 0.0f };         /* 0x19EDF4 */
    float dir[4], n[4] = { p->plane[0], p->plane[1], p->plane[2], 0.0f }; /* 0x19EE0C, 0x19EE28 */
    if (sdk_sub(dir, qb, qa)) return -1;                              /* 0x19EDF8 */
    const float d = p->plane[3];                                      /* 0x19EE2C: node +0x30 */
    float along, nq;
    if (sdk_dot(&along, dir, n)) return -1;                           /* 0x19EE34 */
    if (!em_ee_c_le(along, f_bits(EPS_FRONT))) return 0;              /* 0x19EE50 */
    if (sdk_dot(&nq, n, qa)) return -1;                               /* 0x19EE6C */
    float t = em_ee_div(em_ee_sub(d, nq), along);                     /* 0x19EE74, 0x19EE80 */
    float hit[4];
    if (sdk_scale(hit, dir, t) || sdk_add(hit, qa, hit)) return -1;   /* 0x19EE8C, 0x19EE9C */
    for (int k = 0; k < 3; ++k) {                                     /* 0x19EEA4..0x19F020 */
        if (em_ee_c_le(qa[k], qb[k])) {                               /* 0x19EEAC bc1t */
            if (!em_ee_c_le(qa[k], hit[k])) return 0;                 /* 0x19EEF4 */
            if (em_ee_c_lt(qb[k], hit[k])) return 0;                  /* 0x19EF0C */
        } else {
            if (em_ee_c_lt(qa[k], hit[k])) return 0;                  /* 0x19EEC0 */
            if (!em_ee_c_le(qb[k], hit[k])) return 0;                 /* 0x19EED8 */
        }
    }
    hit[3] = 0.0f;                                                    /* 0x19F04C: spBC = 0 */
    /* The ring: node +0x1C indexes the vertex-index pool (0x70003204), node
     * +0x20 the edge-normal pool (0x70003200); the EMCL ring carries the
     * same values (tools/export_collision.py checks them bit for bit). */
    for (unsigned k = 0; k < p->vcount; ++k) {                        /* 0x19F128 */
        const float *v = c->verts + 3u * c->indices[p->first + k];    /* 0x19F058..0x19F0C8 */
        const float *e = c->edge_n + 3u * (p->first + k);            /* 0x19F0CC..0x19F0EC */
        float vv[4] = { v[0], v[1], v[2], 0.0f }, rel[4];             /* spDC = 0 */
        float ee[4] = { e[0], e[1], e[2], 0.0f };
        float dot;
        if (sdk_sub(rel, hit, vv) || sdk_dot(&dot, rel, ee)) return -1;  /* 0x19F0C4, 0x19F0E8 */
        if (!em_ee_c_le(dot, f_bits(EPS_EDGE))) return 0;             /* 0x19F100 */
    }
    memcpy(s->point, hit, sizeof s->point);                           /* 0x19F144 loop: 0x700031B0 */
    s->record = EM_COLL_PROBE_RECORD_GRID;                            /* 0x19F164: 0x700031D0 = node */
    s->node = node;
    return 1;
}

/* ---- The cell prim tests ---------------------------------------------------- */

/* 001A4030(prim): the convex n-gon test (prim header +2 = n; plane at +4,
 * d +0x10; n points from +0x14; n edge normals 12n further). */
int em_coll_probe_001A4030(const uint8_t *prim, EmCollProbeState *s)
{
    const uint8_t *plane = prim + 4;                                  /* 0x1A4058 */
    float qa[4] = { s->start[0], s->start[1], s->start[2], 0.0f };    /* 0x1A4080 loop, 0x1A40D0 */
    float qb[4] = { s->end[0], s->end[1], s->end[2], 0.0f };          /* 0x1A40C8 */
    float n[4] = { rd_f(plane, 0), rd_f(plane, 4), rd_f(plane, 8), 0.0f }; /* 0x1A40C4 */
    float dir[4], along, nq, hit[4];
    if (sdk_sub(dir, qb, qa)) return -1;                              /* 0x1A40CC */
    const float d = rd_f(plane, 0xC);                                 /* 0x1A40D4 */
    if (sdk_dot(&along, dir, n)) return -1;                           /* 0x1A40DC */
    if (!em_ee_c_le(along, f_bits(EPS_FRONT))) return 0;              /* 0x1A40F8 */
    if (sdk_dot(&nq, n, qa)) return -1;                               /* 0x1A4114 */
    float t = em_ee_div(em_ee_sub(d, nq), along);                     /* 0x1A411C, 0x1A4128 */
    if (sdk_scale(hit, dir, t) || sdk_add(hit, qa, hit)) return -1;   /* 0x1A4134, 0x1A4144 */
    for (int k = 0; k < 3; ++k) {                                     /* 0x1A414C..0x1A4238 */
        if (!em_ee_c_le(qa[k], hit[k]) && !em_ee_c_le(qb[k], hit[k])) return 0;
        if (em_ee_c_lt(qa[k], hit[k]) && em_ee_c_lt(qb[k], hit[k])) return 0;
    }
    const unsigned count = prim[2];                                   /* 0x1A4248 */
    const uint8_t *vert = plane + 0x10;                               /* 0x1A424C */
    const uint8_t *edge = plane + (count * 3 + 4) * 4;                /* 0x1A4254..0x1A4264 */
    for (unsigned k = 0; k < count; ++k) {                            /* 0x1A42F8 */
        float v[4] = { rd_f(vert, 0), rd_f(vert, 4), rd_f(vert, 8), 0.0f };  /* spFC = 0 */
        float e[4] = { rd_f(edge, 0), rd_f(edge, 4), rd_f(edge, 8), 0.0f };
        float rel[4], dot;
        if (sdk_sub(rel, hit, v) || sdk_dot(&dot, rel, e)) return -1;   /* 0x1A4298, 0x1A42BC */
        if (!em_ee_c_le(dot, f_bits(EPS_EDGE))) return 0;             /* 0x1A42D4 */
        vert += 12;
        edge += 12;
    }
    memcpy(s->point, hit, sizeof s->point);                           /* 0x1A4318 loop */
    float nx = rd_f(plane, 0), nz = rd_f(plane, 8), ny = rd_f(plane, 4);
    float acc = em_ee_mula(nx, nx);                                   /* 0x1A4348 */
    acc = em_ee_madd(acc, nz, nz);                                    /* 0x1A434C */
    s->ratio = em_ee_div(em_ee_mul(ny, ny), acc);                     /* 0x1A4350..0x1A4358 */
    if (!em_ee_c_lt(ny, 0.0f)) {                                      /* 0x1A4360 */
        if (em_ee_c_lt(s->ratio, f_bits(RATIO_WALL))) s->cell_class = 0x2000;       /* 0x1A43A4 */
        else if (em_ee_c_le(s->ratio, f_bits(RATIO_FLOOR))) s->cell_class = 0x1000; /* 0x1A43DC */
        else s->cell_class = 0x4000;                                                /* 0x1A43CC */
    } else {
        if (em_ee_c_lt(s->ratio, f_bits(RATIO_WALL))) s->cell_class = 0x2000;       /* 0x1A4418 */
        else if (em_ee_c_le(s->ratio, f_bits(RATIO_FLOOR))) s->cell_class = 0x0800; /* 0x1A4450 */
        else s->cell_class = 0x8000;                                                /* 0x1A4444 */
    }
    s->cell_normal[0] = nx;                                           /* 0x1A4460 loop: +0x24 */
    s->cell_normal[1] = ny;
    s->cell_normal[2] = nz;
    return 1;
}

/* 001A4650(prim): the vertical test of a 0x2000 face; only faces 3 (top)
 * and 4 (bottom). */
int em_coll_probe_001A4650(const uint8_t *prim, EmCollProbeState *s)
{
    const uint8_t *q = prim + 4;                                      /* 0x1A4650 */
    const unsigned face = prim[2];                                    /* 0x1A4654 */
    if (face - 3u >= 2u) return 0;                                    /* 0x1A465C */
    float lo, hi;
    if (em_ee_c_lt(s->start[1], s->end[1])) {                         /* 0x1A4680 */
        if (face == 3) return 0;                                      /* 0x1A4694 */
        lo = s->start[1]; hi = s->end[1];
    } else {
        if (face == 4) return 0;                                      /* 0x1A46B4 */
        lo = s->end[1]; hi = s->start[1];                             /* 0x1A46B8, 0x1A46C8 */
    }
    float ex = rd_f(q, 0xC), xlo, xhi;                                /* 0x1A46CC */
    if (em_ee_c_lt(ex, 0.0f)) { xhi = rd_f(q, 0); xlo = em_ee_add(xhi, ex); }  /* 0x1A46E8 */
    else { xlo = rd_f(q, 0); xhi = em_ee_add(xlo, ex); }                        /* 0x1A46F4 */
    float ez = rd_f(q, 0x14), zlo, zhi;                               /* 0x1A46FC */
    if (em_ee_c_lt(ez, 0.0f)) { zhi = rd_f(q, 8); zlo = em_ee_add(zhi, ez); }  /* 0x1A4718 */
    else { zlo = rd_f(q, 8); zhi = em_ee_add(zlo, ez); }                        /* 0x1A4724 */
    const float x = s->start[0], z = s->start[2];                     /* 0x1A4730, 0x1A4758 */
    if (em_ee_c_lt(x, xlo) || !em_ee_c_le(x, xhi)) return 0;          /* 0x1A4734, 0x1A4744 */
    if (em_ee_c_lt(z, zlo) || !em_ee_c_le(z, zhi)) return 0;          /* 0x1A475C, 0x1A476C */
    const float y = rd_f(q, 4);                                       /* 0x1A4788 */
    if (!em_ee_c_lt(lo, y)) return 0;                                 /* 0x1A478C */
    if (em_ee_c_le(hi, y)) return 0;                                  /* 0x1A479C */
    s->point[0] = x;                                                  /* 0x1A47B0 */
    s->point[1] = y;                                                  /* 0x1A47C0 */
    s->cell_normal[2] = 0.0f;                                         /* 0x1A47C8: 0x700030DC */
    s->cell_normal[0] = 0.0f;                                         /* 0x1A47D0: 0x700030D4 */
    s->point[2] = z;                                                  /* 0x1A47E4 */
    if (face == 3) { s->cell_class = 0x4000; s->cell_normal[1] = f_bits(ONE_BITS); }        /* 0x1A47F0 */
    else { s->cell_class = 0x8000; s->cell_normal[1] = f_bits(MINUS_ONE_BITS); }             /* 0x1A480C */
    return 1;
}

/* 001A44B0(prim): the vertical test of a 0x8000 (half height = radius +0xC)
 * or 0x4000 (half height +0x10) round prim; centre +4..+0xC. */
int em_coll_probe_001A44B0(const uint8_t *prim, EmCollProbeState *s)
{
    const uint8_t *q = prim + 4;                                      /* 0x1A44BC */
    const float half = (rd_u16(prim, 0) & 0x8000) ? rd_f(q, 0xC) : rd_f(q, 0x10);  /* 0x1A44B8 */
    float lo, hi;
    int upward;
    if (em_ee_c_le(s->start[1], s->end[1])) {                         /* 0x1A44E0 */
        upward = 1; lo = s->start[1]; hi = s->end[1];                 /* 0x1A4500 */
    } else {
        upward = 0; lo = s->end[1]; hi = s->start[1];                 /* 0x1A44F0 */
    }
    const float cy = rd_f(q, 4);
    float face = upward ? em_ee_sub(cy, half) : em_ee_add(cy, half);  /* 0x1A4514 / 0x1A4548 */
    if (em_ee_c_le(face, lo)) return 0;                               /* 0x1A4518 / 0x1A454C */
    if (!em_ee_c_lt(face, hi)) return 0;                              /* 0x1A4528 / 0x1A455C */
    const float x = s->start[0];                                      /* 0x1A457C */
    float dx = em_ee_sub(x, rd_f(q, 0));                              /* 0x1A4594 */
    float acc = em_ee_mula(dx, dx);                                   /* 0x1A4598 */
    float dz = em_ee_sub(s->start[2], rd_f(q, 8));                    /* 0x1A459C */
    float r2 = em_ee_mul(rd_f(q, 0xC), rd_f(q, 0xC));                 /* 0x1A45A0 */
    float d2 = em_ee_madd(acc, dz, dz);                               /* 0x1A45A4 */
    if (em_ee_c_lt(r2, d2)) return 0;                                 /* 0x1A45A8 */
    s->point[0] = x;                                                  /* 0x1A45BC */
    s->cell_normal[2] = 0.0f;                                         /* 0x1A45C4: 0x700030DC */
    s->cell_normal[0] = 0.0f;                                         /* 0x1A45CC: 0x700030D4 */
    s->point[2] = s->start[2];                                        /* 0x1A45E0 */
    if (upward) {
        s->cell_class = 0x8000;                                       /* 0x1A45F8 */
        s->cell_normal[1] = f_bits(MINUS_ONE_BITS);                   /* 0x1A4600 */
        s->point[1] = em_ee_sub(cy, half);                            /* 0x1A45F4, 0x1A460C */
    } else {
        s->cell_class = 0x4000;                                       /* 0x1A4624 */
        s->cell_normal[1] = f_bits(ONE_BITS);                         /* 0x1A462C */
        s->point[1] = em_ee_add(cy, half);                            /* 0x1A4620, 0x1A4634 */
    }
    return 1;
}

/* ---- The cell walkers ---------------------------------------------------- */

/* One prim of a walk: the test, then the cursor advance. `pass2` selects
 * 001A2AE0's second-pass tests (001A50A0 / 001A5C30, workers) or 001A32C0's
 * second pass (0x8000 -> 001A44B0). Returns 0, or -1 on a fault. */
enum { WALK_SURFACE_1, WALK_SURFACE_2, WALK_OBJECT_1, WALK_OBJECT_2 };

static int prim_step(const EmActorCellTable *t, uint32_t *cursor, int walk,
                     const EmCollProbeWorkers *wk, EmCollProbeState *s, int *hit)
{
    const uint8_t *base = t->bytes;
    uint32_t at = *cursor;
    if ((uint64_t)at + 4 > t->size) return -1;
    const uint8_t *p = base + at;
    const uint16_t h = rd_u16(p, 0);
    uint32_t size;
    switch (h & 0xF000) {
    case 0x1000: size = (h & 0x800) ? 0x24u + 0x30u * p[2] : 0x14u + 0x18u * p[2]; break;
    case 0x2000: size = 0x1C; break;
    case 0x4000: size = (h & 0x800) ? 0x2C : 0x18; break;
    case 0x8000: size = (h & 0x800) ? 0x24 : 0x14; break;
    default: return 0;   /* 0x1A2D38 etc.: no call, no advance; the hit stays 0 */
    }
    if ((uint64_t)at + size > t->size) return -1;
    int r = 0;
    switch (h & 0xF000) {
    case 0x1000:
        r = em_coll_probe_001A4030(p, s);                             /* 0x1A2DA8, 0x1A3110, 0x1A3528, 0x1A3850 */
        break;
    case 0x2000:
        if (walk == WALK_SURFACE_2) {                                 /* 0x1A30FC: 001A50A0 */
            if (!wk || !wk->face_segment || wk->face_segment(wk->context, p, s, &r) < 0) return -1;
        } else {
            r = em_coll_probe_001A4650(p, s);                         /* 0x1A2D94, 0x1A3514, 0x1A383C */
        }
        break;
    case 0x4000:
        if (walk == WALK_SURFACE_2) {                                 /* 0x1A30CC: 001A5C30 */
            if (!wk || !wk->round_segment || wk->round_segment(wk->context, p, s, &r) < 0) return -1;
        } else {
            r = em_coll_probe_001A44B0(p, s);                         /* 0x1A2D64, 0x1A34E4, 0x1A380C */
        }
        break;
    case 0x8000:
        if (walk == WALK_OBJECT_2) r = em_coll_probe_001A44B0(p, s);  /* 0x1A37DC */
        break;                                                        /* passes 1 and 0x1A30AC: no call */
    }
    if (r < 0) return -1;
    *hit = r;
    *cursor = at + size;
    return 0;
}

static int hull_prims(const EmActorCellTable *t, uint32_t hull, int walk, const EmCollProbeWorkers *wk,
                      EmCollProbeState *s, int *hit)
{
    if ((uint64_t)hull + 0x1C > t->size) return -1;
    const int16_t count = rd_s16(t->bytes, hull + 0x18);               /* 0x1A2E08: hull +0x18 */
    uint32_t cursor = hull + 0x1C;                                     /* 0x1A2CF0 */
    *hit = 0;
    for (int16_t j = 0; j < count; ++j) {
        if (prim_step(t, &cursor, walk, wk, s, hit)) return -1;
        if (*hit) break;                                               /* 0x1A2DF8 */
    }
    return 0;
}

/* Pass 1's hull offset: dsll32/dsrl32 by 2 keep the low 30 bits of the
 * word, so a 0x20000000-flagged word (gated on 0x7000324E) yields
 * tbl + 0x20000000 + offset, which the EE reads through its uncached
 * main-RAM mirror (0x20000000..0x21FFFFFF): the same bytes as tbl + offset. */
static uint32_t static_hull(uint32_t word)
{
    uint32_t off = word & 0x3FFFFFFFu;
    return off & 0x20000000u ? off & 0x1FFFFFFFu : off;
}

/* The published class-4 entry j (D_00275B7C[j]), or NULL for a fault. */
static const EmActor *owner_at(const EmActorCollisionWorld *w, int j)
{
    return em_actor_class_list_entry(w->lists, EM_ACTOR_LIST_CLASS4, j);
}

/* The static cell pass's kind byte D_0024D7C0[D_00810700][D_00810701][i].+8. */
static int static_kind(const EmActorCollisionWorld *w, int i, uint8_t *kind)
{
    if (!w->static_kind || i < 0 || (unsigned)i >= w->static_kind_count) return -1;
    *kind = w->static_kind[i];
    return 0;
}

/* 001A2AE0: the surface record's cell walk. Returns 1 on a hit, 0, -1. */
int em_coll_probe_001A2AE0(const EmCollProbeWorld *world, const EmCollProbeWorkers *wk,
                           EmCollProbeState *s)
{
    if (!world || !world->cells || !world->cells->table || !world->cells->table->bytes ||
        !world->cells->lists || !s) return -1;
    const EmActorCollisionWorld *w = world->cells;
    const EmActorCellTable *t = w->table;
    s->record = EM_COLL_PROBE_RECORD_CELL;                            /* 0x1A2B28: 0x700031D0 = D_700030B0 */
    s->node = -1;
    float xmax = s->start[0], xmin;                                   /* 0x1A2B30 f23 */
    if (em_ee_c_le(s->start[0], s->end[0])) { xmin = s->start[0]; xmax = s->end[0]; }  /* 0x1A2B54 */
    else xmin = s->end[0];                                            /* 0x1A2B50 */
    float ymax = s->start[1], ymin = s->start[1];                     /* 0x1A2B60, 0x1A2B78 */
    if (em_ee_c_le(s->start[1], s->end[1])) ymax = s->end[1];         /* 0x1A2B88 */
    else ymin = s->end[1];                                            /* 0x1A2B80 */
    float zmax = s->start[2], zmin = s->start[2];                     /* 0x1A2B90, 0x1A2BA8 */
    if (em_ee_c_le(s->start[2], s->end[2])) zmax = s->end[2];         /* 0x1A2BB8 */
    else zmin = s->end[2];                                            /* 0x1A2BB0 */
    int result = 0;                                                   /* 0x1A2B48: s2 = 0 */
    for (int i = 0; i < t->count; ++i) {                              /* pass 1, 0x1A2F20 */
        if ((uint64_t)4 + 4u * (unsigned)i + 4 > t->size) return -1;
        const uint32_t word = rd_u32(t->bytes, 4 + 4u * (unsigned)i);  /* 0x1A2BE0 */
        if (!(word & 0x80000000u)) break;                             /* 0x1A2BE8 -> pass 2 */
        if (word & 0x40000000u) continue;                             /* 0x1A2BF8 */
        if ((word & 0x20000000u) && s->query_class == 0) continue;    /* 0x1A2C18 */
        uint8_t kind;
        if (static_kind(w, i, &kind)) return -1;                      /* 0x1A2C54 */
        if (kind < 0x5A) continue;                                    /* 0x1A2C58 */
        const uint32_t hull = static_hull(word);                      /* 0x1A2C68 */
        if ((uint64_t)hull + 0x18 > t->size) return -1;
        const uint8_t *hb = t->bytes + hull;
        if (em_ee_c_lt(xmax, rd_f(hb, 0))) continue;                  /* 0x1A2C78 */
        if (!em_ee_c_le(xmin, rd_f(hb, 0xC))) continue;               /* 0x1A2C8C */
        if (em_ee_c_lt(ymax, rd_f(hb, 4))) continue;                  /* 0x1A2CA0 */
        if (!em_ee_c_le(ymin, rd_f(hb, 0x10))) continue;              /* 0x1A2CB4 */
        if (em_ee_c_lt(zmax, rd_f(hb, 8))) continue;                  /* 0x1A2CC8 */
        if (!em_ee_c_le(zmin, rd_f(hb, 0x14))) continue;              /* 0x1A2CDC */
        int hit;
        if (hull_prims(t, hull, WALK_SURFACE_1, wk, s, &hit)) return -1;
        if (!hit) continue;                                           /* 0x1A2E18 */
        result = 1;                                                   /* 0x1A2E30 */
        s->end[1] = s->point[1];                                      /* 0x1A2E5C: 0x700031A4 = 0x700031B4 */
        s->entity = NULL;                                             /* 0x1A2E64 */
        s->cell_class = (uint16_t)((s->cell_class & 0xFF00) | kind);  /* 0x1A2EA0 */
        if (em_ee_c_le(s->start[0], s->end[0])) xmax = s->end[0];     /* 0x1A2EAC */
        else xmin = s->end[0];                                        /* 0x1A2EA8 */
        if (em_ee_c_le(s->start[1], s->end[1])) ymax = s->end[1];     /* 0x1A2ECC */
        else ymin = s->end[1];                                        /* 0x1A2ED4 */
        if (em_ee_c_le(s->start[2], s->end[2])) zmax = s->end[2];     /* 0x1A2EFC */
        else zmin = s->end[2];                                        /* 0x1A2F04 */
    }
    const EmActorClassList *list = &w->lists->list[EM_ACTOR_LIST_CLASS4];
    for (int j = 0; j < list->published; ++j) {                       /* pass 2, 0x1A3268 */
        const EmActor *a = owner_at(w, j);                            /* 0x1A2F44 */
        if (!a) return -1;
        if (a->status == 0) continue;                                 /* 0x1A2F4C */
        if ((a->cls & 0x1F) != 4) continue;                           /* 0x1A2F60 */
        if (s->self == (const void *)a) continue;                     /* 0x1A2F70 */
        const uint8_t kind = (uint8_t)a->kind;                        /* 0x1A2F78: +0x54 byte */
        if (kind < 0x5A) continue;                                    /* 0x1A2F80 */
        const unsigned uid = (a->uid >> 8) & 0xFF;                    /* 0x1A2F88 */
        if (uid == 0xFF) continue;                                    /* 0x1A2F98 */
        if ((uint64_t)4 + 4u * uid + 4 > t->size) return -1;
        const uint32_t word = rd_u32(t->bytes, 4 + 4u * uid);         /* 0x1A2FB0 */
        if (!word) continue;                                          /* 0x1A2FB4 */
        if (!((int)uid < t->count)) continue;                         /* 0x1A2FC8 */
        if (word & 0x80000000u) return -1;   /* hull = tbl + word unmasked (0x1A2FD8): outside the image */
        if ((uint64_t)word + 0x18 > t->size) return -1;
        const uint8_t *hb = t->bytes + word;
        if (em_ee_c_lt(xmax, rd_f(hb, 0))) continue;                  /* 0x1A2FE0 */
        if (!em_ee_c_le(xmin, rd_f(hb, 0xC))) continue;               /* 0x1A2FF4 */
        if (em_ee_c_lt(ymax, rd_f(hb, 4))) continue;                  /* 0x1A3008 */
        if (!em_ee_c_le(ymin, rd_f(hb, 0x10))) continue;              /* 0x1A301C */
        if (em_ee_c_lt(zmax, rd_f(hb, 8))) continue;                  /* 0x1A3030 */
        if (!em_ee_c_le(zmin, rd_f(hb, 0x14))) continue;              /* 0x1A3044 */
        int hit;
        if (hull_prims(t, word, WALK_SURFACE_2, wk, s, &hit)) return -1;
        if (!hit) continue;                                           /* 0x1A3180 */
        memcpy(s->end, s->point, sizeof s->point);                    /* 0x1A3198 loop: 0x700031A0 = 0x700031B0 */
        s->entity = a;                                                /* 0x1A31B8 */
        result = 1;                                                   /* 0x1A31C8 */
        s->cell_class = (uint16_t)((s->cell_class & 0xFF00) | kind);  /* 0x1A31F0 */
        if (em_ee_c_le(s->start[0], s->end[0])) xmax = s->end[0];     /* 0x1A31FC */
        else xmin = s->end[0];                                        /* 0x1A31F8 */
        if (em_ee_c_le(s->start[1], s->end[1])) ymax = s->end[1];     /* 0x1A321C */
        else ymin = s->end[1];                                        /* 0x1A3224 */
        if (em_ee_c_le(s->start[2], s->end[2])) zmax = s->end[2];     /* 0x1A324C */
        else zmin = s->end[2];                                        /* 0x1A3254 */
    }
    return result;
}

/* 001A32C0: the object probe's cell walk. Returns 0 on a hit, 1, -1. */
int em_coll_probe_001A32C0(const EmCollProbeWorld *world, EmCollProbeState *s)
{
    if (!world || !world->cells || !world->cells->table || !world->cells->table->bytes ||
        !world->cells->lists || !s) return -1;
    const EmActorCollisionWorld *w = world->cells;
    const EmActorCellTable *t = w->table;
    s->record = EM_COLL_PROBE_RECORD_CELL;                            /* 0x1A32F8: 0x700031D0 = D_700030B0 */
    s->node = -1;
    float ymax = s->start[1], ymin;                                   /* 0x1A3300 f21 */
    if (em_ee_c_le(s->start[1], s->end[1])) { ymin = s->start[1]; ymax = s->end[1]; }  /* 0x1A3324 */
    else ymin = s->end[1];                                            /* 0x1A3320 */
    int result = 1;                                                   /* 0x1A3318: s2 = 1 */
    for (int i = 0; i < t->count; ++i) {                              /* pass 1, 0x1A3640 */
        if ((uint64_t)4 + 4u * (unsigned)i + 4 > t->size) return -1;
        const uint32_t word = rd_u32(t->bytes, 4 + 4u * (unsigned)i);  /* 0x1A3350 */
        if (!(word & 0x80000000u)) break;                             /* 0x1A3358 */
        if (word & 0x40000000u) continue;                             /* 0x1A3368 */
        if ((word & 0x20000000u) && s->query_class == 0) continue;    /* 0x1A3388 */
        uint8_t kind;
        if (static_kind(w, i, &kind)) return -1;                      /* 0x1A33C4 */
        if (kind >= 0x1E) continue;                                   /* 0x1A33C8 */
        const uint32_t hull = static_hull(word);                      /* 0x1A33D8 */
        if ((uint64_t)hull + 0x18 > t->size) return -1;
        const uint8_t *hb = t->bytes + hull;
        if (em_ee_c_lt(ymax, rd_f(hb, 4))) continue;                  /* 0x1A33E8 */
        if (!em_ee_c_le(ymin, rd_f(hb, 0x10))) continue;              /* 0x1A33FC */
        if (em_ee_c_lt(s->start[0], rd_f(hb, 0))) continue;           /* 0x1A3418 */
        if (!em_ee_c_le(s->start[0], rd_f(hb, 0xC))) continue;        /* 0x1A342C */
        if (em_ee_c_lt(s->start[2], rd_f(hb, 8))) continue;           /* 0x1A3448 */
        if (!em_ee_c_le(s->start[2], rd_f(hb, 0x14))) continue;       /* 0x1A345C */
        int hit;
        if (hull_prims(t, hull, WALK_OBJECT_1, NULL, s, &hit)) return -1;
        if (!hit) continue;                                           /* 0x1A3598 */
        result = 0;                                                   /* 0x1A35B0 */
        s->end[1] = s->point[1];                                      /* 0x1A35DC */
        s->entity = NULL;                                             /* 0x1A35E4 */
        s->cell_class = (uint16_t)((s->cell_class & 0xFF00) | kind);  /* 0x1A3620 */
        if (em_ee_c_le(s->start[1], s->end[1])) ymax = s->end[1];     /* 0x1A362C */
        else ymin = s->end[1];                                        /* 0x1A3628 */
    }
    const EmActorClassList *list = &w->lists->list[EM_ACTOR_LIST_CLASS4];
    for (int j = 0; j < list->published; ++j) {                       /* pass 2, 0x1A3930 */
        const EmActor *a = owner_at(w, j);                            /* 0x1A3664 */
        if (!a) return -1;
        if (a->status == 0) continue;                                 /* 0x1A366C */
        if ((a->cls & 0x1F) != 4) continue;                           /* 0x1A3680 */
        if (s->self == (const void *)a) continue;                     /* 0x1A3690 */
        const unsigned uid = (a->uid >> 8) & 0xFF;                    /* 0x1A3698 */
        if (uid == 0xFF) continue;                                    /* 0x1A36A8 */
        if ((uint64_t)4 + 4u * uid + 4 > t->size) return -1;
        const uint32_t word = rd_u32(t->bytes, 4 + 4u * uid);         /* 0x1A36C0 */
        if (!word) continue;                                          /* 0x1A36C4 */
        if (!((int)uid < t->count)) continue;                         /* 0x1A36D8 */
        if (word & 0x80000000u) return -1;   /* hull = tbl + word unmasked (0x1A36E8): outside the image */
        if ((uint64_t)word + 0x18 > t->size) return -1;
        const uint8_t *hb = t->bytes + word;
        if (em_ee_c_lt(ymax, rd_f(hb, 4))) continue;                  /* 0x1A36F0 */
        if (!em_ee_c_le(ymin, rd_f(hb, 0x10))) continue;              /* 0x1A3704 */
        if (em_ee_c_lt(s->start[0], rd_f(hb, 0))) continue;           /* 0x1A3720 */
        if (!em_ee_c_le(s->start[0], rd_f(hb, 0xC))) continue;        /* 0x1A3734 */
        if (em_ee_c_lt(s->start[2], rd_f(hb, 8))) continue;           /* 0x1A3750 */
        if (!em_ee_c_le(s->start[2], rd_f(hb, 0x14))) continue;       /* 0x1A3764 */
        const uint8_t kind = (uint8_t)a->kind;                        /* 0x1A3774: +0x54 byte */
        if (kind >= 0x1E) continue;                                   /* 0x1A3778 */
        int hit;
        if (hull_prims(t, word, WALK_OBJECT_2, NULL, s, &hit)) return -1;
        if (!hit) continue;                                           /* 0x1A38C0 */
        result = 0;                                                   /* 0x1A38D0 */
        s->end[1] = s->point[1];                                      /* 0x1A38E0 */
        s->entity = a;                                                /* 0x1A38E8 */
        s->cell_class = (uint16_t)((s->cell_class & 0xFF00) | kind);  /* 0x1A3914 */
        if (em_ee_c_le(s->start[1], s->end[1])) ymax = s->end[1];     /* 0x1A3920 */
        else ymin = s->end[1];                                        /* 0x1A391C */
    }
    return result;
}

/* ---- The grid walkers -------------------------------------------------------- */

/* The span pick 0019DF10 (0x19E028) and 0019E640 (0x19E6BC) share: for i in
 * 0, 1, 4, 5 (0x19E02C skips 2 and 3), the helper table *(0x70003228 + 4i)
 * at `helper_rank[i]`. Leaves 0x70003B86/88 as the last i left them. *found
 * is 0 when no direction beats the node count: the original's span
 * registers are then uninitialized, which the caller must not walk. -1 when
 * a helper index falls outside its table. */
static int span_pick(const EmCollProbeGrid *g, EmCollProbeState *s, const int32_t helper_rank[6],
                     int32_t *lo, int32_t *hi, int *cand, int *found)
{
    const int32_t n = (int32_t)g->count;
    int32_t best = n;                                                 /* 0x19E010: 0x7000320C */
    *found = 0;
    for (int i = 0; i < 6; ++i) {
        if ((unsigned)(i - 2) < 2u) continue;                         /* 0x19E02C */
        const int16_t *helper = g->tables + (size_t)(6 + i) * (size_t)n;
        int32_t at = helper_rank[i];
        if (at < 0 || at >= n) return -1;
        if (i & 1) {                                                  /* 0x19E03C */
            s->span_lo = s->rank[i];                                  /* 0x19E04C: 0x70003B86 */
            s->span_hi = helper[at];                                  /* 0x19E068: 0x70003B88 */
        } else {
            s->span_lo = helper[at];                                  /* 0x19E088 */
            s->span_hi = s->rank[i];                                  /* 0x19E094 */
            s->span_hi = (int16_t)(s->span_hi + 1);                   /* 0x19E0A8 */
        }
        int32_t diff = (int32_t)s->span_hi - (int32_t)s->span_lo;     /* 0x19E0C0 */
        if (diff < best) {                                            /* 0x19E0C4 */
            best = diff;
            *lo = s->span_lo;
            *hi = s->span_hi;
            *cand = i;
            *found = 1;
        }
    }
    return 0;
}

/* The walk's table range [lo, hi) must lie inside the table (the original
 * would read the neighbouring table). */
static int span_valid(const EmCollProbeGrid *g, int32_t lo, int32_t hi)
{
    return !(lo < hi && (lo < 0 || hi > (int32_t)g->count));
}

/* The node gate both grid walkers apply before 0019ED80 (0x19E138..0x19E188,
 * 0x19E7E0..0x19E830): the rank bounds +0x0C/+0x0E/+0x14/+0x16 against the
 * ranks of directions 0, 1, 4 and 5. */
static int rank_gate(const EmCollProbeGrid *g, const EmCollProbeState *s, int node)
{
    const int16_t *w = g->words + 12 * node;
    if (s->rank[0] < w[6]) return 0;                                  /* +0x0C */
    if (w[7] < s->rank[1]) return 0;                                  /* +0x0E */
    if (s->rank[4] < w[10]) return 0;                                 /* +0x14 */
    if (w[11] < s->rank[5]) return 0;                                 /* +0x16 */
    return 1;
}

/* 0019DF10: the surface record's grid walk (attr 0x5A..0x77). 1 on a hit, 0. */
int em_coll_probe_0019DF10(const EmCollProbeWorld *world, EmCollProbeState *s)
{
    if (!world || !world->grid || !world->grid->tables || !s) return -1;
    const EmCollProbeGrid *g = world->grid;
    unsigned mask_b, mask_a;                                          /* s5, s6 */
    if (em_ee_c_le(s->start[0], s->end[0])) { mask_b = 2; mask_a = 1; }  /* 0x19DF60 */
    else { mask_b = 1; mask_a = 2; }                                  /* 0x19DF54 */
    if (em_ee_c_le(s->start[2], s->end[2])) { mask_b |= 0x20; mask_a |= 0x10; }  /* 0x19DF84, 0x19DF98 */
    else { mask_b |= 0x10; mask_a |= 0x20; }                          /* 0x19DF88, 0x19DF90 */
    if (em_coll_probe_0019F1A0(g, s, s->start, mask_a)) return -1;    /* 0x19DFA4 */
    if (em_coll_probe_0019F1A0(g, s, s->end, mask_b)) return -1;      /* 0x19DFB4 */
    int32_t first[6];
    for (int i = 0; i < 6; ++i) first[i] = s->rank[i];                /* 0x19DFD0 loop: sp80 */
    if (em_coll_probe_0019F1A0(g, s, s->start, mask_b)) return -1;    /* 0x19DFF4 */
    if (em_coll_probe_0019F1A0(g, s, s->end, mask_a)) return -1;      /* 0x19E004 */
    int32_t lo = 0, hi = 0;
    int cand = 0, picked;
    if (span_pick(g, s, first, &lo, &hi, &cand, &picked)) return -1;
    if (!picked || !span_valid(g, lo, hi)) return -1;
    const int16_t *table = g->tables + (size_t)cand * g->count;       /* 0x19E0F8: *(0x70003210 + 4 cand) */
    int found = -1;                                                   /* s3 = 0 */
    float saved[3] = { 0.0f, 0.0f, 0.0f };
    for (int32_t k = lo; k < hi; ++k) {                               /* 0x19E110, 0x19E204 */
        const int node = table[k];                                    /* 0x19E11C */
        if (!rank_gate(g, s, node)) continue;
        const uint8_t attr = grid_node(g, node)->attr;                /* 0x19E190: +0x1A */
        if (attr < 0x5A || attr >= 0x78) continue;                    /* 0x19E194, 0x19E1A0 */
        int r = em_coll_probe_0019ED80(g, s, node);                   /* 0x19E1B0 */
        if (r < 0) return -1;
        if (!r) continue;
        memcpy(s->end, s->point, sizeof s->point);                    /* 0x19E1D0 loop: +0x10 = +0x20 */
        memcpy(saved, s->point, sizeof saved);                        /* spA0 */
        found = node;                                                 /* 0x19E1F8: s3 = 0x700031D0 */
    }
    if (found < 0) return 0;                                          /* 0x19E210 */
    memcpy(s->point, saved, sizeof saved);                            /* 0x19E228 loop */
    s->record = EM_COLL_PROBE_RECORD_GRID;                            /* 0x19E250 */
    s->node = found;
    return 1;
}

/* 0019E640: the object probe's grid walk (attr below 0x1E). 0 on a hit, 1. */
int em_coll_probe_0019E640(const EmCollProbeWorld *world, EmCollProbeState *s)
{
    if (!world || !world->grid || !world->grid->tables || !s) return -1;
    const EmCollProbeGrid *g = world->grid;
    const float *at = em_ee_c_le(s->start[1], s->end[1]) ? s->end : s->start;  /* 0x19E668 */
    if (em_coll_probe_0019F1A0(g, s, at, 0x33)) return -1;            /* 0x19E680 / 0x19E698 */
    int32_t same[6];
    for (int i = 0; i < 6; ++i) same[i] = s->rank[i];                 /* 0x19E6E8 / 0x19E718 re-read */
    int32_t lo = 0, hi = 0;
    int cand = 0, picked;
    if (span_pick(g, s, same, &lo, &hi, &cand, &picked)) return -1;
    if (s->rank[2] == -1) return 1;                                   /* 0x19E788: 0x70003244 */
    if (!picked || !span_valid(g, lo, hi)) return -1;
    const int16_t *table = g->tables + (size_t)cand * g->count;       /* 0x19E7A0 */
    int found = -1;                                                   /* s2 = 0 */
    float saved[3] = { 0.0f, 0.0f, 0.0f };
    for (int32_t k = lo; k < hi; ++k) {                               /* 0x19E7B8, 0x19E8BC */
        const int node = table[k];                                    /* 0x19E7C4 */
        if (!rank_gate(g, s, node)) continue;
        const uint8_t attr = grid_node(g, node)->attr;                /* 0x19E838 */
        s->span_hi = attr;                                            /* 0x19E840: 0x70003B88 */
        if (attr >= 0x1E) continue;                                   /* 0x19E84C */
        int r = em_coll_probe_0019ED80(g, s, node);                   /* 0x19E85C */
        if (r < 0) return -1;
        if (!r) continue;
        s->end[1] = s->point[1];                                      /* 0x19E888: 0x700031A4 = 0x700031B4 */
        memcpy(saved, s->point, sizeof saved);                        /* 0x19E890 loop: sp50 */
        found = node;                                                 /* 0x19E8B0 */
    }
    if (found < 0) return 1;                                          /* 0x19E8C8 */
    s->record = EM_COLL_PROBE_RECORD_GRID;                            /* 0x19E8E0 */
    s->node = found;
    memcpy(s->point, saved, sizeof saved);                            /* 0x19E8E8 loop */
    return 0;
}

/* 0019C830: 0019AB20's vertical grid walk (kind byte below 0x5A, with the
 * query-class filter). 0 on a hit, 1. */
int em_coll_probe_0019C830(const EmCollProbeGrid *g, EmCollProbeState *s)
{
    if (!g || !g->tables || !g->emcl || !s) return -1;
    const float *at = em_ee_c_le(s->start[1], s->end[1]) ? s->end : s->start;  /* 0x19C858 */
    if (em_coll_probe_0019F1A0(g, s, at, 0x33)) return -1;            /* 0x19C870 / 0x19C888 */
    int32_t same[6];
    for (int i = 0; i < 6; ++i) same[i] = s->rank[i];                 /* 0x19C8C8 / 0x19C8F8 re-read */
    int32_t lo = 0, hi = 0;
    int cand = 0, picked;
    if (span_pick(g, s, same, &lo, &hi, &cand, &picked)) return -1;   /* 0x19C8AC .. 0x19C970 */
    if (!picked || !span_valid(g, lo, hi)) return -1;
    const int16_t *table = g->tables + (size_t)cand * g->count;       /* 0x19C97C: *(0x70003210 + 4 cand) */
    int found = -1;                                                   /* s1 = 0 */
    float saved[3] = { 0.0f, 0.0f, 0.0f };
    for (int32_t k = lo; k < hi; ++k) {                               /* 0x19C998, 0x19CAF0 */
        const int node = table[k];                                    /* 0x19C998 */
        if (!rank_gate(g, s, node)) continue;                         /* 0x19C9B8..0x19CA04 */
        const uint8_t attr = grid_node(g, node)->attr;                /* 0x19CA0C: +0x1A */
        s->span_hi = attr;                                            /* 0x19CA14: 0x70003B88 */
        const int16_t kind = s->span_hi;                              /* 0x19CA1C */
        if (kind >= 0x5A) continue;                                   /* 0x19CA20 */
        if (kind == 0x51 && s->query_class != 0) continue;            /* 0x19CA30..0x19CA40 */
        if (kind == 0x52 && s->query_class != 2) continue;            /* 0x19CA4C..0x19CA60 */
        if (kind == 0x53 && s->query_class == -1) continue;           /* 0x19CA6C..0x19CA80 */
        int r = em_coll_probe_0019ED80(g, s, node);                   /* 0x19CA8C */
        if (r < 0) return -1;
        if (!r) continue;
        s->end[1] = s->point[1];                                      /* 0x19CAB8: 0x700031A4 = 0x700031B4 */
        memcpy(saved, s->point, sizeof saved);                        /* 0x19CAC0 loop: sp50 */
        found = node;                                                 /* 0x19CAE0: s1 = 0x700031D0 */
    }
    if (found < 0) return 1;                                          /* 0x19CAF8 */
    s->record = EM_COLL_PROBE_RECORD_GRID;                            /* 0x19CB10 */
    s->node = found;
    memcpy(s->point, saved, sizeof saved);                            /* 0x19CB18 loop */
    return 0;
}

/* ---- The probes -------------------------------------------------------------- */

int em_coll_probe_0019B6C0(const EmCollProbeWorld *world, const EmCollProbeWorkers *workers,
                           EmCollProbeState *state, const float top[3], const float bottom[3])
{
    if (!world || !state || !top || !bottom) return -1;
    EmCollProbeState s = *state;
    float saved[3];
    for (int k = 0; k < 3; ++k) {                                     /* 0x19B6E8 loop */
        s.start[k] = top[k];
        saved[k] = bottom[k];
        s.end[k] = bottom[k];
    }
    s.self = NULL;                                                    /* 0x19B71C: 0x70003254 = 0 */
    s.end[3] = f_bits(ONE_BITS);                                      /* 0x19B728: 0x700031AC */
    s.start[3] = f_bits(ONE_BITS);                                    /* 0x19B730: 0x7000319C */
    s.entity = NULL;                                                  /* 0x19B740: 0x700031D4 */
    int r = 0;
    int hit = em_coll_probe_001A2AE0(world, workers, &s);             /* 0x19B73C */
    if (hit < 0) return -1;
    if (hit) r = 2;                                                   /* 0x19B74C */
    hit = em_coll_probe_0019DF10(world, &s);                          /* 0x19B750 */
    if (hit < 0) return -1;
    if (hit) r = 4;                                                   /* 0x19B760 */
    if (r) memcpy(s.end, saved, sizeof saved);                        /* 0x19B778 loop */
    else { s.record = EM_COLL_PROBE_RECORD_NONE; s.node = -1; }       /* 0x19B7A0 */
    s.kind = r;                                                       /* 0x19B7A8: 0x700031D8 */
    *state = s;
    return r;
}

int em_coll_probe_0019B8C0(const EmCollProbeWorld *world, EmCollProbeState *state,
                           const void *self, uint8_t cls, const float at[3],
                           const float probe[3], unsigned mask)
{
    if (!world || !state || !at || !probe) return -1;
    EmCollProbeState s = *state;
    float saved[3];
    for (int k = 0; k < 3; ++k) {                                     /* 0x19B8F4 loop */
        s.end[k] = at[k];
        s.start[k] = at[k];
        saved[k] = at[k];
    }
    s.start[1] = em_ee_sub(s.start[1], probe[1]);                     /* 0x19B930 */
    const float nudge = em_ee_c_lt(probe[1], 0.0f) ? f_bits(NUDGE_POS) : f_bits(NUDGE_NEG); /* 0x19B93C */
    s.start[1] = em_ee_add(s.start[1], nudge);                        /* 0x19B978 */
    s.end[3] = 0.0f;                                                  /* 0x19B988: 0x700031AC = 0 */
    s.start[3] = 0.0f;                                                /* 0x19B990: 0x7000319C = 0 */
    s.entity = NULL;                                                  /* 0x19B998 */
    s.query_class = (int16_t)(cls & 0x1F);                            /* 0x19B9AC: 0x7000324E */
    int r = 0;
    if (mask & 2) {                                                   /* 0x19B9A8 */
        s.self = self;                                                /* 0x19B9BC: 0x70003254 = +0x14 */
        int clear = em_coll_probe_001A32C0(world, &s);                /* 0x19B9B8 */
        if (clear < 0) return -1;
        if (!clear) r = 2;                                            /* 0x19B9C8 */
    }
    if (mask & 4) {                                                   /* 0x19B9D0 */
        int clear = em_coll_probe_0019E640(world, &s);                /* 0x19B9D8 */
        if (clear < 0) return -1;
        if (!clear) r = 4;                                            /* 0x19B9E8 */
    }
    s.start[1] = em_ee_sub(s.start[1], nudge);                        /* 0x19B9F4 */
    if (r) {
        for (int k = 0; k < 3; ++k) {                                 /* 0x19BA10 loop */
            s.end[k] = saved[k];
            s.delta[k] = em_ee_sub(s.point[k], s.end[k]);             /* 0x19BA2C: 0x700031C0 */
        }
    } else {
        s.record = EM_COLL_PROBE_RECORD_NONE;                         /* 0x19BA48 */
        s.node = -1;
    }
    s.kind = r;                                                       /* 0x19BA50 */
    *state = s;
    return r;
}

/* ---- The player floor service's workers ------------------------------------- */

static void fill_hit(const EmCollProbePlayer *p, int kind, EmPlayerProbeHit *hit, int with_delta)
{
    const EmCollProbeState *s = p->state;
    memset(hit, 0, sizeof *hit);
    hit->kind = kind;
    if (!kind) return;
    hit->node = em_coll_probe_record_node(p->world->grid, s);          /* record +0x1A */
    if (s->record == EM_COLL_PROBE_RECORD_GRID)
        memcpy(hit->normal, grid_node(p->world->grid, s->node)->plane, sizeof hit->normal);
    else
        memcpy(hit->normal, s->cell_normal, sizeof hit->normal);
    memcpy(hit->point, s->point, sizeof hit->point);
    if (with_delta) memcpy(hit->delta, s->delta, sizeof hit->delta);
    hit->owner = s->entity;                                           /* 0x700031D4 */
    hit->entity = s->entity != NULL;
    if (s->entity) {
        hit->entity_flags = s->entity->cls;                           /* *(0x700031D4) + 2 */
        hit->entity_type = s->entity->model;                          /* *(0x700031D4) + 3 */
    }
}

int em_coll_probe_player_head(void *context, const float top[3], const float bottom[3],
                              EmPlayerProbeHit *hit)
{
    EmCollProbePlayer *p = context;
    if (!p || !p->world || !p->state || !hit) return -1;
    int kind = em_coll_probe_0019B6C0(p->world, p->workers, p->state, top, bottom);
    if (kind < 0) return -1;
    fill_hit(p, kind, hit, 0);
    return kind;
}

int em_coll_probe_player_object(void *context, const float at[3], const float probe[3],
                                unsigned mask, EmPlayerProbeHit *hit)
{
    EmCollProbePlayer *p = context;
    if (!p || !p->world || !p->state || !hit) return -1;
    int kind = em_coll_probe_0019B8C0(p->world, p->state, p->query.self, p->query.cls, at, probe, mask);
    if (kind < 0) return -1;
    fill_hit(p, kind, hit, 1);
    return kind;
}
