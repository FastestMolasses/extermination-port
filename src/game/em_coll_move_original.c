/* em_coll_move_original.c - the original horizontal move/sweep walkers (see
 * the header and docs/COLL_MOVE.md).
 *
 * Every routine names the original it translates and the .s addresses of
 * its branches and stores. 0019AD00, 0019AFE0 and 0019FE50 follow their .s
 * (the readable C is NEARMISS); 001A4830 and 001A4D10 follow their .s (the
 * decomp file is asm words); 001A4030 follows its byte-matched C. */
#include "game/em_coll_move_original.h"

#include <string.h>

#include "game/em_ee_float.h"

const unsigned char em_coll_move_cell_record_tag = 0;

/* ---- EE COP1 (em_ee_float.h) -------------------------------------------- */

static float add(float a, float b) { return em_ee_add(a, b); }
static float sub(float a, float b) { return em_ee_sub(a, b); }
static float mul(float a, float b) { return em_ee_mul(a, b); }
static float divide(float a, float b) { return em_ee_div(a, b); }
static int lt(float a, float b) { return em_ee_c_lt(a, b); }
static int le(float a, float b) { return em_ee_c_le(a, b); }

/* ---- The SDK vector routines, as their VU0 macro ops -------------------- */

/* 001028D0(out, a, b): vsub.xyzw. */
static int vsub4(float out[4], const float a[4], const float b[4])
{
    return em_vu_vec(EM_VU_SUB, 0xF, EM_VU_NO_BC, a, b, 0.0f, NULL, out) ? -1 : 0;
}

/* 001028B8(out, a, b): vadd.xyzw. */
static int vadd4(float out[4], const float a[4], const float b[4])
{
    return em_vu_vec(EM_VU_ADD, 0xF, EM_VU_NO_BC, a, b, 0.0f, NULL, out) ? -1 : 0;
}

/* 001028E8(out, a, b): vmul.xyzw. */
static int vmul4(float out[4], const float a[4], const float b[4])
{
    return em_vu_vec(EM_VU_MUL, 0xF, EM_VU_NO_BC, a, b, 0.0f, NULL, out) ? -1 : 0;
}

/* 00102738(a, b): the x/y/z lane products, then x + y, then + z (the dot). */
static int vdot(const float a[4], const float b[4], float *out)
{
    float v[4];
    memcpy(v, b, sizeof v);
    if (em_vu_vec(EM_VU_MUL, 0xE, EM_VU_NO_BC, a, v, 0.0f, NULL, v)) return -1;
    if (em_vu_vec(EM_VU_ADDBC, 0x8, 1, v, v, 0.0f, NULL, v)) return -1;
    if (em_vu_vec(EM_VU_ADDBC, 0x8, 2, v, v, 0.0f, NULL, v)) return -1;
    *out = v[0];
    return 0;
}

/* 00103230(out, v, s): vmulx.xyz by vf5.x = s; w keeps v's. */
static int vscale(float out[4], const float v[4], float s)
{
    float r[4], b[4] = { s, 0.0f, 0.0f, 0.0f };
    memcpy(r, v, sizeof r);
    if (em_vu_vec(EM_VU_MULBC, 0xE, 0, r, b, 0.0f, NULL, r)) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}

/* 00102760(out, v): vf5.xyz = v*v; x += y; x += z; Q = sqrt(x);
 * vf5.x = vf0.x + Q; Q = vf0.w / vf5.x; vf6 = vf0 - vf0; vf6.xyz = v * Q. */
static int vnormalize(float out[4], const float v[4])
{
    static const float vf0[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float s[4], r[4], q;
    memcpy(s, v, sizeof s);
    if (em_vu_vec(EM_VU_MUL, 0xE, EM_VU_NO_BC, v, v, 0.0f, NULL, s)) return -1;
    if (em_vu_vec(EM_VU_ADDBC, 0x8, 1, s, s, 0.0f, NULL, s)) return -1;
    if (em_vu_vec(EM_VU_ADDBC, 0x8, 2, s, s, 0.0f, NULL, s)) return -1;
    q = em_vu_sqrt(s[0]);
    if (em_vu_vec(EM_VU_ADDQ, 0x8, EM_VU_NO_BC, vf0, NULL, q, NULL, s)) return -1;
    if (em_vu_div(vf0[3], s[0], 3, 0, &q)) return -1;
    if (em_vu_vec(EM_VU_SUB, 0xF, EM_VU_NO_BC, vf0, vf0, 0.0f, NULL, r)) return -1;
    if (em_vu_vec(EM_VU_MULQ, 0xE, EM_VU_NO_BC, v, NULL, q, NULL, r)) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}

/* ---- Original-layout byte access ------------------------------------------ */

static float rd_f(const uint8_t *p, unsigned off) { float v; memcpy(&v, p + off, 4); return v; }
static uint32_t rd_u32(const uint8_t *p, unsigned off) { uint32_t v; memcpy(&v, p + off, 4); return v; }
static uint16_t rd_u16(const uint8_t *p, unsigned off) { uint16_t v; memcpy(&v, p + off, 2); return v; }
static int16_t rd_s16(const uint8_t *p, unsigned off) { int16_t v; memcpy(&v, p + off, 2); return v; }

static float bits_float(uint32_t bits) { return em_ee_float(bits); }

/* ---- 001A4830: 0x8000 / 0x4000 prims -------------------------------------- */

/* The x (or z) half of 001A4830's accept test (0x001A4A64..0x001A4AB0 and
 * its z / second-candidate twins): the crossing lies strictly between the
 * segment ends, in either direction. */
static int strictly_between(float s, float e, float h)
{
    if (lt(s, h) && !le(e, h)) return 1;
    if (le(s, h)) return 0;
    return lt(e, h);
}

/* 0011E748 (sqrtf), the em_sdk_math_original translation over the
 * world's SDK context. */
static int sdk_sqrt(const EmCollMoveWorld *w, float x, float *out)
{
    uint32_t fault = 0;
    if (!w || !w->math) return -1;
    const EmSdkMathContext *m = w->math;
    return em_sdk_math_original_0011E748(m->tables, &m->world, &m->workers, x, out, &fault) < 0 ? -1 : 0;
}

static void round_hit(EmCollMoveScratch *s, const uint8_t *q, float hx, float hz)
{
    /* 0x001A4B10..0x001A4B8C (and 0x001A4C60..0x001A4CDC) */
    s->point[0] = hx;
    s->point[1] = s->end[1];
    s->point[2] = hz;
    s->cell_normal[0] = divide(sub(s->point[0], rd_f(q, 0)), rd_f(q, 0xC));
    s->cell_normal[1] = 0.0f;
    s->cell_normal[2] = divide(sub(s->point[2], rd_f(q, 8)), rd_f(q, 0xC));
    s->cell_class = 0x2000;
}

int em_coll_move_prim_001A4830(const EmCollMoveWorld *w, EmCollMoveScratch *s, const uint8_t *p)
{
    if (!s || !p || !w || !w->math) return -1;
    const uint8_t *q = p + 4;
    /* 0x001A4858: the half height is the radius for a 0x8000 prim. */
    float half = (rd_u16(p, 0) & 0x8000) ? rd_f(q, 0xC) : rd_f(q, 0x10);
    float cy = rd_f(q, 4);
    /* 0x001A4884..0x001A48A4: the end height inside [cy - half, cy + half]. */
    if (!le(sub(cy, half), s->end[1])) return 0;
    if (lt(add(cy, half), s->end[1])) return 0;
    float r = rd_f(q, 0xC);
    float r2 = mul(r, r);                                    /* 0x001A48D4 */
    float dx = sub(s->end[0], s->start[0]);                  /* 0x001A48E8 */
    float dz = sub(s->end[2], s->start[2]);                  /* 0x001A4908 */
    float cx = rd_f(q, 0), cz = rd_f(q, 8);
    float ox = sub(s->start[0], cx);                         /* 0x001A4920 */
    float oz = sub(s->start[2], cz);                         /* 0x001A492C */
    /* 0x001A4938: 001028E8 on (dx, dz, ox, oz) x (dx, dz, dx, dz). */
    const float a[4] = { dx, dz, ox, oz }, b[4] = { dx, dz, dx, dz };
    float prod[4];
    if (vmul4(prod, a, b)) return -1;
    /* 0x001A4958..0x001A4974: the closest point's offset from the centre. */
    float t = divide(em_ee_neg(add(prod[2], prod[3])), add(prod[0], prod[1]));
    float nx = add(ox, mul(dx, t));
    float nz = add(oz, mul(dz, t));
    float d2 = em_ee_madd(em_ee_mula(nx, nx), nz, nz);      /* 0x001A4978 */
    if (lt(r2, d2)) return 0;                                /* 0x001A4980 */
    /* 0x001A4998..0x001A49D8: a start inside the circle never hits. */
    float sx = sub(s->start[0], cx), sz = sub(s->start[2], cz);
    if (lt(em_ee_madd(em_ee_mula(sx, sx), sz, sz), r2)) return 0;
    float hc, len;
    if (sdk_sqrt(w, sub(r2, d2), &hc)) return -1;                                  /* 0x001A49E8 */
    if (sdk_sqrt(w, em_ee_madd(em_ee_mula(dx, dx), dz, dz), &len)) return -1;      /* 0x001A4A00 */
    float ux = divide(dx, len), uz = divide(dz, len);        /* 0x001A4A1C, 0x001A4A2C */
    float nhc = em_ee_neg(hc);
    /* First crossing: closest - u * hc (0x001A4A3C..0x001A4A58). */
    float hx = em_ee_madd(em_ee_adda(nx, cx), ux, nhc);
    float hz = em_ee_madd(em_ee_adda(nz, cz), uz, nhc);
    if (strictly_between(s->start[0], s->end[0], hx) || strictly_between(s->start[2], s->end[2], hz)) {
        round_hit(s, q, hx, hz);
        return 1;
    }
    /* Second crossing: closest + u * hc (0x001A4B90..0x001A4BB4). */
    hx = em_ee_madd(em_ee_adda(nx, cx), ux, hc);
    hz = em_ee_madd(em_ee_adda(nz, cz), uz, hc);
    if (strictly_between(s->start[0], s->end[0], hx) || strictly_between(s->start[2], s->end[2], hz)) {
        round_hit(s, q, hx, hz);
        return 1;
    }
    return 0;
}

/* ---- 001A4D10: 0x2000 face prims ---------------------------------------- */

int em_coll_move_prim_001A4D10(EmCollMoveScratch *s, const uint8_t *p)
{
    if (!s || !p) return -1;
    const uint8_t *q = p + 4;
    unsigned face = p[2];
    if (face - 3u < 2u) return 0;                            /* 0x001A4D18: y faces */
    /* 0x001A4D30..0x001A4D88: the end height inside the face's y extent. */
    float ext = rd_f(q, 0x10), y0 = rd_f(q, 4), lo, hi;
    if (lt(ext, 0.0f)) { lo = add(y0, ext); hi = y0; }
    else { lo = y0; hi = add(y0, ext); }
    float ey = s->end[1];
    if (lt(ey, lo)) return 0;
    if (!le(ey, hi)) return 0;
    /* 0x001A4D94..0x001A4DEC: a +x walk never meets face 1, a -x walk face 2. */
    float sx = s->start[0], ex = s->end[0], mnx, mxx;
    if (le(sx, ex)) { if (face == 1) return 0; mnx = sx; mxx = ex; }
    else { if (face == 2) return 0; mnx = ex; mxx = sx; }
    /* 0x001A4DF0..0x001A4E48: likewise faces 5 / 6 along z. */
    float sz = s->start[2], ez = s->end[2], mnz, mxz;
    if (le(sz, ez)) { if (face == 5) return 0; mnz = sz; mxz = ez; }
    else { if (face == 6) return 0; mnz = ez; mxz = sz; }
    if ((int)face < 3) {                                     /* 0x001A4E4C: x faces */
        float ez_ext = rd_f(q, 0x14), x0 = rd_f(q, 0), zlo, zhi;
        if (lt(ez_ext, 0.0f)) { zhi = rd_f(q, 8); zlo = add(zhi, ez_ext); }
        else { zlo = rd_f(q, 8); zhi = add(zlo, ez_ext); }
        if (!lt(mnx, x0)) return 0;                          /* 0x001A4E88 */
        if (le(mxx, x0)) return 0;                           /* 0x001A4E98 */
        s->work[0] = sub(ex, sx);                            /* 0x001A4EB0 */
        s->work[1] = sub(ez, sz);                            /* 0x001A4EC4 */
        s->work[2] = sub(x0, sx);                            /* 0x001A4ED8 */
        float hz = add(sz, divide(mul(s->work[1], s->work[2]), s->work[0]));
        s->work[3] = hz;                                     /* 0x001A4EFC */
        if (le(hz, zlo)) return 0;
        if (!lt(hz, zhi)) return 0;
        s->point[0] = x0;                                    /* 0x001A4F14.. */
        s->point[1] = ey;
        s->point[2] = hz;
        s->cell_normal[2] = 0.0f;
        s->cell_normal[1] = 0.0f;
        s->cell_class = 0x2000;
        s->cell_normal[0] = bits_float(face == 1 ? 0x3F800000u : 0xBF800000u);
        return 1;
    }
    /* 0x001A4F74: z faces (5, 6 and any other byte >= 5). */
    float ex_ext = rd_f(q, 0xC), z0 = rd_f(q, 8), xlo, xhi;
    if (lt(ex_ext, 0.0f)) { xhi = rd_f(q, 0); xlo = add(xhi, ex_ext); }
    else { xlo = rd_f(q, 0); xhi = add(xlo, ex_ext); }
    if (!lt(mnz, z0)) return 0;                              /* 0x001A4FA4 */
    if (le(mxz, z0)) return 0;                               /* 0x001A4FB4 */
    s->work[0] = sub(ex, sx);                                /* 0x001A4FCC */
    s->work[1] = sub(ez, sz);                                /* 0x001A4FE4 */
    s->work[2] = sub(z0, sz);                                /* 0x001A4FF4 */
    float hx = add(sx, divide(mul(s->work[0], s->work[2]), s->work[1]));
    s->work[3] = hx;                                         /* 0x001A5018 */
    if (le(hx, xlo)) return 0;
    if (!lt(hx, xhi)) return 0;
    s->point[0] = hx;                                        /* 0x001A5030.. */
    s->point[1] = ey;
    s->point[2] = z0;
    s->cell_normal[1] = 0.0f;
    s->cell_normal[0] = 0.0f;
    s->cell_class = 0x2000;
    s->cell_normal[2] = bits_float(face == 5 ? 0x3F800000u : 0xBF800000u);
    return 1;
}

/* ---- 001A4030: 0x1000 n-gon prims (byte-matched C) ---------------------- */

int em_coll_move_prim_001A4030(EmCollMoveScratch *s, const uint8_t *p)
{
    if (!s || !p) return -1;
    const float qa[4] = { s->start[0], s->start[1], s->start[2], 0.0f };
    const float qb[4] = { s->end[0], s->end[1], s->end[2], 0.0f };
    const float n[4] = { rd_f(p, 4), rd_f(p, 8), rd_f(p, 0xC), 0.0f };
    float dir[4], along, nqa, hit[4];
    if (vsub4(dir, qb, qa)) return -1;
    float d = rd_f(p, 0x10);
    if (vdot(dir, n, &along)) return -1;
    if (!le(along, bits_float(0xB727C5ACu))) return 0;       /* -1e-5: facing */
    if (vdot(n, qa, &nqa)) return -1;
    if (vscale(hit, dir, divide(sub(d, nqa), along))) return -1;
    if (vadd4(hit, qa, hit)) return -1;
    for (int k = 0; k < 3; ++k) {
        if (!((le(qa[k], hit[k]) || le(qb[k], hit[k])) && !(lt(qa[k], hit[k]) && lt(qb[k], hit[k]))))
            return 0;
    }
    unsigned count = p[2];
    const uint8_t *vert = p + 0x14;
    const uint8_t *edge = p + 0x14 + 12u * count;            /* (p + 4) + (3n + 4) * 4 */
    for (unsigned k = 0; k < count; ++k) {
        const float v[4] = { rd_f(vert, 0), rd_f(vert, 4), rd_f(vert, 8), 0.0f };
        const float e[4] = { rd_f(edge, 0), rd_f(edge, 4), rd_f(edge, 8), 0.0f };
        float rel[4], dot;
        if (vsub4(rel, hit, v)) return -1;
        if (vdot(rel, e, &dot)) return -1;
        if (!le(dot, bits_float(0x3727C5ACu))) return 0;     /* 1e-5: inside */
        vert += 12;
        edge += 12;
    }
    for (int k = 0; k < 3; ++k) s->point[k] = hit[k];        /* 0x001A4318 */
    /* 0x001A4348..0x001A4358: ny^2 / (nx^2 + nz^2) to 0x70003680. */
    float ratio = divide(mul(n[1], n[1]), em_ee_madd(em_ee_mula(n[0], n[0]), n[2], n[2]));
    s->work[0] = ratio;
    if (!lt(n[1], 0.0f)) {
        if (lt(ratio, 0.49029058f)) s->cell_class = 0x2000;
        else if (!le(ratio, 3.0f)) s->cell_class = 0x4000;
        else s->cell_class = 0x1000;
    } else {
        if (lt(ratio, 0.49029058f)) s->cell_class = 0x2000;
        else if (!le(ratio, 3.0f)) s->cell_class = 0x8000;
        else s->cell_class = 0x0800;
    }
    for (int k = 0; k < 3; ++k) s->cell_normal[k] = n[k];    /* 0x001A4460 */
    return 1;
}

/* ---- 0019FE50: the horizontal cell walker --------------------------------- */

/* One prim of a hull (0x001A0094..0x001A0194 / 0x001A046C..0x001A056C). The
 * hit flag is the last prim call's return: an unknown type calls nothing,
 * keeps it and does not advance. */
static int prim_step(const EmCollMoveWorld *w, EmCollMoveScratch *s, const uint8_t **cursor, int *hit)
{
    const uint8_t *p = *cursor;
    uint16_t h = rd_u16(p, 0);
    int r;
    switch (h & 0xF000) {
    case 0x8000:
        if ((r = em_coll_move_prim_001A4830(w, s, p)) < 0) return -1;
        *hit = r;
        *cursor = p + ((h & 0x800) ? 0x24 : 0x14);
        break;
    case 0x4000:
        if ((r = em_coll_move_prim_001A4830(w, s, p)) < 0) return -1;
        *hit = r;
        *cursor = p + ((h & 0x800) ? 0x2C : 0x18);
        break;
    case 0x2000:
        if ((r = em_coll_move_prim_001A4D10(s, p)) < 0) return -1;
        *hit = r;
        *cursor = p + 0x1C;
        break;
    case 0x1000:
        if ((r = em_coll_move_prim_001A4030(s, p)) < 0) return -1;
        *hit = r;
        *cursor = (h & 0x800) ? p + 0x24 + 0x30u * p[2] : p + 0x14 + 0x18u * p[2];
        break;
    default:
        break;
    }
    return 0;
}

typedef struct { float mnx, mxx, mnz, mxz; } Bounds;

/* The hull AABB gate (0x0019FFF0..0x001A007C / 0x001A03CC..0x001A0450). */
static int hull_admits(const uint8_t *hull, const Bounds *b, float y)
{
    if (lt(b->mxx, rd_f(hull, 0))) return 0;
    if (!le(b->mnx, rd_f(hull, 0xC))) return 0;
    if (lt(b->mxz, rd_f(hull, 8))) return 0;
    if (!le(b->mnz, rd_f(hull, 0x14))) return 0;
    if (lt(y, rd_f(hull, 4))) return 0;
    if (!le(y, rd_f(hull, 0x10))) return 0;
    return 1;
}

/* After a hit the far bound moves to the clamped end (0x001A0240..0x001A0288,
 * 0x001A05E8..0x001A0640). */
static void reclamp(const EmCollMoveScratch *s, Bounds *b)
{
    if (le(s->start[0], s->end[0])) b->mxx = s->end[0]; else b->mnx = s->end[0];
    if (le(s->start[2], s->end[2])) b->mxz = s->end[2]; else b->mnz = s->end[2];
}

static int kind_skips(int16_t kind, int16_t query_class)
{
    if (kind >= 0x5A) return 1;
    if (kind == 0x51 && query_class != 0) return 1;
    if (kind == 0x52 && query_class != 2) return 1;
    if (kind == 0x53 && query_class == -1) return 1;
    return 0;
}

static int cells_ready(const EmCollMoveWorld *w)
{
    return w && w->cells && w->cells->table && w->cells->table->bytes && w->cells->lists &&
           w->math;
}

int em_coll_move_walk_0019FE50(const EmCollMoveWorld *w, EmCollMoveScratch *s)
{
    if (!s || !cells_ready(w)) return -1;
    const EmActorCellTable *t = w->cells->table;
    s->record = EM_COLL_MOVE_CELL_RECORD;                    /* 0x0019FE94 */
    int ret = 1;
    Bounds b;
    if (le(s->start[0], s->end[0])) { b.mnx = s->start[0]; b.mxx = s->end[0]; }
    else { b.mnx = s->end[0]; b.mxx = s->start[0]; }
    if (le(s->start[2], s->end[2])) { b.mnz = s->start[2]; b.mxz = s->end[2]; }
    else { b.mnz = s->end[2]; b.mxz = s->start[2]; }

    /* Pass 1 (0x0019FF08..0x001A02B4): the static cells. */
    for (int i = 0; i < t->count; ++i) {
        uint32_t word = rd_u32(t->bytes, 4 + 4u * (unsigned)i);
        if (!(word & 0x80000000u)) break;                    /* 0x0019FF24 */
        if (word & 0x40000000u) continue;                    /* 0x0019FF34 */
        if (!w->cells->static_kind || (unsigned)i >= w->cells->static_kind_count) return -1;
        s->kind = w->cells->static_kind[i];                  /* 0x0019FF78: D_0024D7C0 byte */
        if (kind_skips(s->kind, s->query_class)) continue;
        const uint8_t *hull = t->bytes + (word & 0x3FFFFFFFu);
        if (!hull_admits(hull, &b, s->start[1])) continue;
        const uint8_t *p = hull + 0x1C;
        int hit = 0;
        for (int16_t j = 0; j < rd_s16(hull, 0x18); ++j) {
            if (prim_step(w, s, &p, &hit) < 0) return -1;
            if (hit) break;                                  /* 0x001A0198 */
        }
        if (!hit) continue;
        s->end[0] = s->point[0];                             /* 0x001A01FC */
        ret = 0;
        s->end[2] = s->point[2];                             /* 0x001A0210 */
        s->entity = NULL;                                    /* 0x001A0218 */
        s->cell_class = (uint16_t)((s->cell_class & 0xFF00) | w->cells->static_kind[i]);
        reclamp(s, &b);
    }

    /* Pass 2 (0x001A02B8..0x001A065C): the published class-4 owners. */
    const EmActorClassList *list = &w->cells->lists->list[EM_ACTOR_LIST_CLASS4];
    for (int j = 0; j < list->published; ++j) {
        const EmActor *a = list->slot[list->published - 1 - j];
        if (!a) return -1;
        if (a->status == 0) continue;                        /* 0x001A02CC */
        if ((a->cls & 0x1F) != 4) continue;                  /* 0x001A02E0 */
        if (s->self == (const void *)a) continue;            /* 0x001A02F0 */
        unsigned uid = (a->uid >> 8) & 0xFF;
        if (uid == 0xFF) continue;                           /* 0x001A0308 */
        /* The word is read before the count check (0x001A0324). */
        if (4u + 4u * uid + 4u > t->size) return -1;
        uint32_t word = rd_u32(t->bytes, 4 + 4 * uid);
        if (!word) continue;                                 /* 0x001A0328 */
        s->kind = (uint8_t)a->kind;                          /* 0x001A0338: byte +0x54 */
        if (kind_skips(s->kind, s->query_class)) continue;
        if (!((int)uid < t->count)) continue;                /* 0x001A03BC */
        if (word & 0x80000000u) return -1;                   /* a raw offset outside the image */
        const uint8_t *hull = t->bytes + word;
        if (!hull_admits(hull, &b, s->start[1])) continue;
        const uint8_t *p = hull + 0x1C;
        int hit = 0, found = 0;
        for (int16_t n = 0; n < rd_s16(hull, 0x18); ++n) {
            if (prim_step(w, s, &p, &hit) < 0) return -1;
            if (!hit) continue;                              /* 0x001A0570 */
            found = 1;
            ret = 0;
            s->end[0] = s->point[0];                         /* 0x001A0594 */
            s->end[2] = s->point[2];                         /* 0x001A05A8 */
            s->entity = a;                                   /* 0x001A05B0 */
            s->cell_class = (uint16_t)((s->cell_class & 0xFF00) | (uint8_t)a->kind);
        }
        if (found) reclamp(s, &b);
    }
    return ret;
}

/* ---- 0019AD00 / 0019AFE0 ------------------------------------------------- */

static int needs_ok(const EmCollMoveWorld *w, const EmCollMoveActor *a, uint32_t flags)
{
    if (!w) return 0;
    if ((flags & 1) && (a->status & 1)) {
        if (!(a->cls & 0x1F) ? !(w->cells && w->cells->lists) : !(w->hulls && w->hulls->player)) return 0;
    }
    if ((flags & 2) && !cells_ready(w)) return 0;
    if ((flags & 4) && !(w->grid && w->grid->tables && w->grid->words && w->grid->emcl)) return 0;
    return 1;
}

/* The scratchpad words the hull locks read and write, in and out. */
static EmCollHullScratch hull_in(const EmCollMoveScratch *s)
{
    EmCollHullScratch h;
    memset(&h, 0, sizeof h);
    for (int i = 0; i < 3; ++i) {
        h.start[i] = s->start[i];
        h.end[i] = s->end[i];
        h.point[i] = s->point[i];
        h.cell_normal[i] = s->cell_normal[i];
    }
    h.cell_class = s->cell_class;
    h.word_1c = s->cell_word_1c;
    h.word_20 = s->cell_word_20;
    h.entity = s->entity;
    return h;
}

static void hull_out(EmCollMoveScratch *s, const EmCollHullScratch *h)
{
    for (int i = 0; i < 3; ++i) {
        s->point[i] = h->point[i];
        s->cell_normal[i] = h->cell_normal[i];
    }
    s->cell_class = h->cell_class;
    s->cell_word_1c = h->word_1c;
    s->cell_word_20 = h->word_20;
    s->entity = h->entity;
    if (h->record_cell) s->record = EM_COLL_MOVE_CELL_RECORD;   /* 0x700031D0 = D_700030B0 */
}

/* 0019CB60 over this module's scratch: the probe state view carries the
 * words it reads and writes (segment, point, ranks, span words, query
 * class); a hit names the grid node with its +0x1A halfword and +0x24
 * normal. */
static int grid_pass(const EmCollMoveWorld *w, EmCollMoveScratch *s, int *result)
{
    EmCollProbeState p;
    memset(&p, 0, sizeof p);
    memcpy(p.start, s->start, sizeof p.start);
    memcpy(p.end, s->end, sizeof p.end);
    memcpy(p.point, s->point, sizeof p.point);
    p.record = EM_COLL_PROBE_RECORD_NONE;
    p.node = -1;
    p.query_class = s->query_class;
    memcpy(p.rank, s->rank, sizeof p.rank);
    p.span_lo = s->span_lo;
    p.span_hi = s->kind;
    const int r = em_coll_grid_hull_0019CB60(w->grid, &p);
    if (r < 0) return -1;
    memcpy(s->end, p.end, sizeof p.end);
    memcpy(s->point, p.point, sizeof p.point);
    memcpy(s->rank, p.rank, sizeof s->rank);
    s->span_lo = p.span_lo;
    s->kind = p.span_hi;
    if (r == 0) {
        const EmCollPoly *node = &w->grid->emcl->polys[w->grid->first + (uint32_t)p.node];
        s->record = em_coll_grid_hull_node_record(w->grid, p.node);
        if (!s->record) return -1;
        s->record_node = em_coll_probe_record_node(w->grid, &p);   /* node +0x1A, +0x1B */
        memcpy(s->record_normal, node->plane, sizeof s->record_normal);   /* node +0x24 */
        memset(s->record_axis, 0, sizeof s->record_axis);          /* node +0x34: not in the EMCL */
    }
    *result = r;
    return 0;
}

/* The shared body of 0019AD00 (sweep = 0) and 0019AFE0 (sweep = 1) after the
 * segment start is staged. */
static int walk(const EmCollMoveWorld *w, EmCollMoveScratch *s, EmCollMoveActor *actor,
                const float to[3], uint32_t flags, int sweep)
{
    float goal[3], dir[4];
    s->start[1] = to[1];                                     /* 0x0019AD48 */
    for (int i = 0; i < 3; ++i) {                            /* 0x0019AD60 */
        s->end[i] = to[i];
        goal[i] = to[i];
    }
    s->end[3] = 0.0f;                                        /* 0x0019AD90: 0x700031AC */
    s->start[3] = 0.0f;                                      /* 0x0019AD9C: 0x7000319C */
    s->entity = NULL;                                        /* 0x0019ADB0: 0x700031D4 */
    /* 0x0019ADAC..0x0019ADE8: end += 0.01 * normalize(end - start). */
    if (vsub4(dir, s->end, s->start)) return -1;
    if (vnormalize(dir, dir)) return -1;
    if (vscale(dir, dir, bits_float(0x3C23D70Au))) return -1;
    if (vadd4(s->end, s->end, dir)) return -1;
    int mode = 0;
    if ((flags & 1) && (actor->status & 1)) {                /* 0x0019ADF0..0x0019AE04 */
        int ok;
        EmCollHullScratch h = hull_in(s);
        if (!(actor->cls & 0x1F)) {
            /* 0x0019AE20: 001A6440(0x40); the locked entity's +0x52 bit 1 vetoes. */
            ok = em_coll_grid_hull_001A6440(w->cells->lists, w->hulls, &h, 0x40);
            if (ok < 0) return -1;
            hull_out(s, &h);
            if (ok) {
                if (!s->entity) return -1;                   /* the original reads *(0 + 0x52) */
                if (s->entity->h52 & 2) ok = 0;
            }
        } else {
            /* 0x0019AE60: 001A7280(0x40); the query actor's +0x52 bit 1 is required. */
            ok = em_coll_grid_hull_001A7280(w->hulls, &h, 0x40);
            if (ok < 0) return -1;
            hull_out(s, &h);
            if (ok && !(actor->h52 & 2)) ok = 0;
        }
        if (ok && !(actor->h52 & 1)) {                       /* 0x0019AE88..0x0019AEC4 */
            for (int i = 0; i < 3; ++i) s->end[i] = s->point[i];
            mode = 1;
        }
    }
    s->query_class = actor->cls & 0x1F;                     /* 0x0019AEDC */
    if (flags & 2) {
        s->self = actor->self;                               /* 0x0019AEEC */
        int r = em_coll_move_walk_0019FE50(w, s);
        if (r < 0) return -1;
        if (r == 0) mode = 2;
    }
    if (flags & 4) {                                         /* 0x0019AF08: 0019CB60 */
        int r = 1;
        if (grid_pass(w, s, &r) < 0) return -1;
        if (r == 0) mode = 4;
    }
    if (!sweep) {
        if (vsub4(s->end, s->end, dir)) return -1;           /* 0x0019AF2C (0019AD00) */
    } else {
        if (vadd4(s->start, s->start, dir)) return -1;       /* 0019AFE0: start += step */
    }
    if (mode) {
        for (int i = 0; i < 3; ++i) {                        /* 0x0019AF48 */
            s->end[i] = goal[i];
            s->delta[i] = sub(s->point[i], s->end[i]);
        }
        if (flags & 0x80000000u) {                           /* 0x0019AF84 */
            actor->position[0] = add(actor->position[0], s->delta[0]);
            actor->position[2] = add(actor->position[2], s->delta[2]);
        }
    } else {
        s->record = NULL;                                    /* 0x0019AFB0 */
    }
    s->mode = mode;                                          /* 0x0019AFB8 */
    return mode;
}

/* Run on copies; commit the scratch and the actor only on success. */
static int run(const EmCollMoveWorld *w, EmCollMoveScratch *s, EmCollMoveActor *actor, const float from[3],
               const float to[3], uint32_t flags, int sweep)
{
    EmCollMoveScratch t = *s;
    EmCollMoveActor a = *actor;
    t.start[0] = from[0];                                    /* 0x0019AD30 / 0019AFE0: +0xB0 or from.x */
    t.start[2] = from[2];                                    /* 0x0019AD5C: +0xB8 or from.z */
    const int mode = walk(w, &t, &a, to, flags, sweep);
    if (mode < 0) return -1;
    *s = t;
    *actor = a;
    return mode;
}

int em_coll_move_0019AD00(const EmCollMoveWorld *w, EmCollMoveScratch *s, EmCollMoveActor *actor,
                          const float target[3], uint32_t flags)
{
    if (!s || !actor || !target || !needs_ok(w, actor, flags)) return -1;
    const float from[3] = { actor->position[0], 0.0f, actor->position[2] };
    return run(w, s, actor, from, target, flags, 0);
}

int em_coll_move_sweep_0019AFE0(const EmCollMoveWorld *w, EmCollMoveScratch *s, EmCollMoveActor *actor,
                                const float from[3], const float to[3], uint32_t flags)
{
    if (!s || !actor || !from || !to || !needs_ok(w, actor, flags)) return -1;
    return run(w, s, actor, from, to, flags, 1);
}

/* ---- Adapters -------------------------------------------------------------- */

/* The EmPlayerProbeHit view of the scratch the call left (the consumers read
 * *(0x700031D0) +0x1A / +0x24 / +0x34, 0x700031D4 +2 / +3, 0x700031B0 and
 * 0x700031C0). */
static int fill_probe_hit(const EmCollMoveScratch *s, int kind, EmPlayerProbeHit *hit)
{
    memset(hit, 0, sizeof *hit);
    hit->kind = kind;
    if (s->record == EM_COLL_MOVE_CELL_RECORD) {
        hit->node = s->cell_class;
        memcpy(hit->normal, s->cell_normal, sizeof hit->normal);
    } else if (s->record) {
        hit->node = s->record_node;
        memcpy(hit->normal, s->record_normal, sizeof hit->normal);
        memcpy(hit->axis, s->record_axis, sizeof hit->axis);
    }
    /* D_700030B0 +0x34 is scratch no walker writes, and a grid node's +0x34
     * is not in the EMCL: neither is carried, so the surface byte whose
     * consumer reads it (00175CF0, surface 0x35) faults. */
    if (kind && s->record && (hit->node & 0xFF) == 0x35) return -1;
    if (s->entity) {
        hit->entity = 1;
        hit->entity_flags = s->entity->cls;
        hit->entity_type = s->entity->model;
        hit->owner = s->entity;
    }
    memcpy(hit->point, s->point, sizeof hit->point);
    memcpy(hit->delta, s->delta, sizeof hit->delta);
    return 0;
}

static EmCollMoveActor player_actor(const EmCollMovePlayer *p, const float position[3])
{
    EmCollMoveActor a;
    a.status = p->live->bytes[0];
    a.cls = p->live->bytes[2];
    a.h52 = em_live_u16(p->live, 0x52);
    a.self = p->self;
    memcpy(a.position, position, sizeof a.position);
    return a;
}

static int player_ready(const EmCollMovePlayer *p)
{
    return p && p->world && p->scratch && p->live;
}

int em_coll_move_player_move(void *player, const float position[3], const float target[3],
                             unsigned mask, EmPlayerProbeHit *hit)
{
    EmCollMovePlayer *p = player;
    if (!player_ready(p) || !position || !target || !hit || (mask & 0x80000000u)) return -1;
    EmCollMoveActor a = player_actor(p, position);
    int kind = em_coll_move_0019AD00(p->world, p->scratch, &a, target, mask);
    if (kind < 0) return -1;
    return fill_probe_hit(p->scratch, kind, hit) < 0 ? -1 : kind;
}

int em_coll_move_player_sweep(void *player, const float from[3], const float to[3], unsigned mask,
                              EmPlayerProbeHit *hit)
{
    EmCollMovePlayer *p = player;
    if (!player_ready(p) || !from || !to || !hit || (mask & 0x80000000u)) return -1;
    const float none[3] = { 0.0f, 0.0f, 0.0f };   /* +0xB0 is not read by 0019AFE0 */
    EmCollMoveActor a = player_actor(p, none);
    int kind = em_coll_move_sweep_0019AFE0(p->world, p->scratch, &a, from, to, mask);
    if (kind < 0) return -1;
    return fill_probe_hit(p->scratch, kind, hit) < 0 ? -1 : kind;
}

static int climb_hit(const EmCollMoveScratch *s, int kind, EmPlayerClimbHit *hit)
{
    if (fill_probe_hit(s, kind, &hit->probe) < 0) return -1;
    hit->pickup_box = s->entity && s->entity->callback == 0x00219550u;
    return kind;
}

int em_coll_move_climb_move(void *player, const float position[3], const float target[4],
                            unsigned mask, EmPlayerClimbHit *hit)
{
    EmCollMovePlayer *p = player;
    if (!player_ready(p) || !position || !target || !hit || (mask & 0x80000000u)) return -1;
    EmCollMoveActor a = player_actor(p, position);
    int kind = em_coll_move_0019AD00(p->world, p->scratch, &a, target, mask);
    if (kind < 0) return -1;
    return climb_hit(p->scratch, kind, hit);
}

int em_coll_move_climb_sweep(void *player, const float from[4], const float to[4], unsigned mask,
                             EmPlayerClimbHit *hit)
{
    EmCollMovePlayer *p = player;
    if (!player_ready(p) || !from || !to || !hit || (mask & 0x80000000u)) return -1;
    const float none[3] = { 0.0f, 0.0f, 0.0f };
    EmCollMoveActor a = player_actor(p, none);
    int kind = em_coll_move_sweep_0019AFE0(p->world, p->scratch, &a, from, to, mask);
    if (kind < 0) return -1;
    return climb_hit(p->scratch, kind, hit);
}

int em_coll_move_slide_move(void *player, float position[3], const float target[4], unsigned mask)
{
    EmCollMovePlayer *p = player;
    if (!player_ready(p) || !position || !target) return -1;
    EmCollMoveActor a = player_actor(p, position);
    int kind = em_coll_move_0019AD00(p->world, p->scratch, &a, target, mask);
    if (kind < 0) return -1;
    position[0] = a.position[0];
    position[2] = a.position[2];
    return kind;
}

int em_coll_move_slide_sweep(void *player, const float from[4], const float to[4], unsigned mask,
                             EmPlayerProbeHit *hit)
{
    return em_coll_move_player_sweep(player, from, to, mask, hit);
}

int em_coll_move_owner_move(void *owner, const float point[3], uint32_t mode)
{
    EmCollMoveOwner *o = owner;
    if (!o || !o->world || !o->scratch || !o->actor || !o->position || !point) return -1;
    EmCollMoveActor a;
    a.status = o->actor->status;
    a.cls = o->actor->cls;
    a.h52 = o->actor->h52;
    a.self = o->actor->self;
    memcpy(a.position, o->position, sizeof a.position);
    int kind = em_coll_move_0019AD00(o->world, o->scratch, &a, point, mode);
    if (kind < 0) return -1;
    o->position[0] = a.position[0];
    o->position[2] = a.position[2];
    return kind;
}
