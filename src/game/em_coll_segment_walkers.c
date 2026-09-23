/* em_coll_segment_walkers.c - the segment and camera queries and their
 * walkers (see the header and docs/COLL_SEGMENT_WALKERS.md).
 *
 * Every routine names the original it translates; comments cite the
 * original address of each branch and store. Comparisons, their directions
 * and the operation order follow the .s. */
#include "game/em_coll_segment_walkers.h"

#include <string.h>

#include "game/em_ee_float.h"

/* ---- Byte access (little-endian original layout) ------------------------- */

static float rd_f(const uint8_t *p, size_t off) { float v; memcpy(&v, p + off, 4); return v; }
static uint32_t rd_u32(const uint8_t *p, size_t off) { uint32_t v; memcpy(&v, p + off, 4); return v; }
static uint16_t rd_u16(const uint8_t *p, size_t off) { uint16_t v; memcpy(&v, p + off, 2); return v; }
static int16_t rd_s16(const uint8_t *p, size_t off) { int16_t v; memcpy(&v, p + off, 2); return v; }

static float f_bits(uint32_t bits) { return em_ee_float(bits); }

#define ONE_BITS 0x3F800000u         /* 1.0: 0019A570 0x19A5C4, 001A50A0, 001A5C30 */
#define MINUS_ONE_BITS 0xBF800000u   /* -1.0 */
/* The double 1e-5 (0x3EE4F8B588E368F1, built at 0x1A5E38..0x1A5E50 and
 * twice more) lies strictly between these two floats. */
#define BELOW_1E5_MAX 0x3727C5ACu    /* the largest float below 1e-5 */
#define ABOVE_1E5_MIN 0x3727C5ADu    /* the smallest float above 1e-5 */

/* ---- 001A50A0 ------------------------------------------------------------- */

/* The segment 0x70003190 -> 0x700031A0 against one face of a box prim:
 * origin prim +4..+0xC, extents +0x10..+0x18 (either sign), face code
 * prim +2 (1/2 the x faces, 3/4 the y faces, 5/6 the z faces; the jump table
 * D_0026DA80 sends 0 to the miss return and codes of 7 or more miss at
 * 0x1A521C). */
int em_coll_segment_001A50A0(const uint8_t *prim, EmCollProbeState *s, EmCollSegmentFaceScratch *x)
{
    if (!prim || !s || !x) return -1;
    const unsigned face = prim[2];                                    /* 0x1A50A8 */
    const uint8_t *box = prim + 4;                                    /* 0x1A50C4 */
    for (unsigned i = 0; i < 3; ++i) {                                /* 0x1A50F4 .. 0x1A5214 */
        const float d = em_ee_sub(s->end[i], s->start[i]);            /* 0x1A50FC */
        x->delta[i] = d;                                              /* 0x1A510C: 0x70003620 + 4i */
        if (em_ee_c_lt(d, 0.0f)) {                                    /* 0x1A5100 */
            if (face == 2 * i + 2) return 0;                          /* 0x1A5118 / 0x1A5130 / 0x1A5148: faces 2, 4, 6 */
        } else {
            if (face == 2 * i + 1) return 0;                          /* 0x1A5160 / 0x1A5178 / 0x1A5190: faces 1, 3, 5 */
        }
        const float o = rd_f(box, 4u * i), e = rd_f(box, 0xCu + 4u * i);
        if (em_ee_c_le(e, 0.0f)) {                                    /* 0x1A51A4 */
            x->box_min[i] = em_ee_add(o, e);                          /* 0x1A51D4: 0x70003600 + 4i */
            x->box_max[i] = o;                                        /* 0x1A51E0: 0x70003610 + 4i */
        } else {
            x->box_min[i] = o;                                        /* 0x1A51B8 */
            x->box_max[i] = em_ee_add(o, e);                          /* 0x1A51C4 */
        }
        x->rel[i] = em_ee_sub(o, s->start[i]);                        /* 0x1A51FC: 0x70003630 + 4i */
    }
    if (face >= 7 || face == 0) return 0;                             /* 0x1A521C; D_0026DA80[0] = 0x1A5694 */
    /* The tested axis, and the two others in the order the original
     * interpolates them (x faces: y, z; y faces: x, z; z faces: x, y). */
    const unsigned axis = (face - 1) / 2;
    const unsigned a = axis == 0 ? 1 : 0, b = axis == 2 ? 1 : 2;
    const float rel = x->rel[axis];                                   /* 0x1A5248 / 0x1A53B4 / 0x1A552C */
    const float c = em_ee_mul(rel, em_ee_sub(x->box_min[axis], s->end[axis]));  /* 0x1A5260, 0x1A5264 */
    if (!em_ee_c_lt(c, 0.0f)) return 0;                               /* 0x1A5268 */
    const float t = em_ee_div(rel, x->delta[axis]);                   /* 0x1A5288 / 0x1A53F4 / 0x1A556C */
    const float u = em_ee_add(s->start[a], em_ee_mul(x->delta[a], t));   /* 0x1A52B4, 0x1A52B8 */
    s->ratio = t;                                                     /* 0x1A52C0: 0x70003680 */
    x->cross[0] = u;                                                  /* 0x1A52C8: 0x70003684 */
    const float v = em_ee_add(s->start[b], em_ee_mul(x->delta[b], t));   /* 0x1A52CC, 0x1A52D8 */
    x->cross[1] = v;                                                  /* 0x1A52EC: 0x70003688 */
    if (em_ee_c_le(u, x->box_min[a])) return 0;                       /* 0x1A52E0 */
    if (!em_ee_c_lt(u, x->box_max[a])) return 0;                      /* 0x1A52F8 */
    if (em_ee_c_le(v, x->box_min[b])) return 0;                       /* 0x1A5318 */
    if (!em_ee_c_lt(v, x->box_max[b])) return 0;                      /* 0x1A5330 */
    s->point[axis] = rd_f(box, 4u * axis);                            /* 0x1A5350 / 0x1A54C0 / 0x1A5644 */
    s->point[a] = u;
    s->point[b] = v;
    s->cell_normal[0] = 0.0f;
    s->cell_normal[1] = 0.0f;
    s->cell_normal[2] = 0.0f;
    const int positive = (face & 1) != 0;                             /* faces 1, 3, 5 */
    s->cell_normal[axis] = f_bits(positive ? ONE_BITS : MINUS_ONE_BITS);  /* 0x1A5370 / 0x1A54D0 / 0x1A5654 */
    if (axis == 1) s->cell_class = positive ? 0x4000 : 0x8000;        /* 0x1A54E4 / 0x1A5500 */
    else s->cell_class = 0x2000;                                      /* 0x1A536C / 0x1A5650 */
    return 1;
}

/* ---- 001A5C30 ------------------------------------------------------------- */

static int vu_mul(const float a[4], const float b[4], float out[4])
{
    uint32_t s[4], t[4], d[4] = { 0, 0, 0, 0 };
    memcpy(s, a, sizeof s);
    memcpy(t, b, sizeof t);
    if (em_vu_vec_bits(EM_VU_MUL, 0xF, EM_VU_NO_BC, s, t, 0, NULL, d) != EM_EE_FLOAT_OK) return -1;
    memcpy(out, d, sizeof d);
    return 0;
}

/* 0011DF78(v), 00128350 (float -> double), then 001000C0(d, 1e-5), which
 * returns 001274B0(d, 1e-5) < 0: the float compares exactly against the
 * double. 001274B0 returns 1 for a NaN operand, so a NaN is not below. */
static int below_1e5(float v)
{
    const uint32_t a = em_ee_bits(em_sdk_math_original_0011DF78(v));
    if (em_eei_is_nan(a)) return 0;
    return a <= BELOW_1E5_MAX;
}

/* The same chain through 00100110: 001274B0(d, 1e-5) > 0 (a NaN is above). */
static int above_1e5(float v)
{
    const uint32_t a = em_ee_bits(em_sdk_math_original_0011DF78(v));
    if (em_eei_is_nan(a)) return 1;
    return a >= ABOVE_1E5_MIN;
}

static int sdk_sqrt(const EmSdkMathContext *m, float x, float *out)
{
    uint32_t fault = 0;
    if (!m) return -1;
    return em_sdk_math_original_0011E748(m->tables, &m->world, &m->workers, x, out, &fault) < 0 ? -1 : 0;
}

/* The segment against a vertical cylinder: centre prim +4..+0xC, radius
 * +0x10, half height +0x14. */
int em_coll_segment_001A5C30(const EmSdkMathContext *math, const uint8_t *prim, EmCollProbeState *s)
{
    if (!prim || !s) return -1;
    const uint8_t *p = prim + 4;                                      /* 0x1A5C84 */
    const float px = rd_f(p, 0), py = rd_f(p, 4), pz = rd_f(p, 8);
    const float radius = rd_f(p, 0xC), half = rd_f(p, 0x10);
    const float r2 = em_ee_mul(radius, radius);                       /* 0x1A5C80 */
    const float dx = em_ee_sub(s->end[0], s->start[0]);               /* 0x1A5C8C */
    const float dz = em_ee_sub(s->end[2], s->start[2]);               /* 0x1A5CB4 */
    float rx = em_ee_sub(s->start[0], px);                            /* 0x1A5CCC */
    float rz = em_ee_sub(s->start[2], pz);                            /* 0x1A5CDC */
    /* 001028E8(sp+D0, sp+D0, sp+C0): the lanes {dx, dz, rx, rz} times
     * {dx, dz, dx, dz} (one VU0 multiply). */
    const float dir[4] = { dx, dz, dx, dz };
    float w[4] = { dx, dz, rx, rz };
    if (vu_mul(w, dir, w)) return -1;                                 /* 0x1A5CEC */
    const float num = em_ee_add(w[2], w[3]);                          /* 0x1A5D0C */
    const float den = em_ee_add(w[0], w[1]);                          /* 0x1A5D10 */
    const float t = em_ee_div(em_ee_neg(num), den);                   /* 0x1A5D14, 0x1A5D18 */
    rx = em_ee_add(rx, em_ee_mul(dx, t));                             /* 0x1A5D1C, 0x1A5D24 */
    rz = em_ee_add(rz, em_ee_mul(dz, t));                             /* 0x1A5D20, 0x1A5D28 */
    const float d2 = em_ee_madd(em_ee_mula(rx, rx), rz, rz);          /* 0x1A5D2C, 0x1A5D30 */
    if (em_ee_c_lt(r2, d2)) return 0;                                 /* 0x1A5D34 */
    float chord, len;
    if (sdk_sqrt(math, em_ee_sub(r2, d2), &chord)) return -1;         /* 0x1A5D40, 0x1A5D50 */
    if (sdk_sqrt(math, em_ee_madd(em_ee_mula(dx, dx), dz, dz), &len)) return -1;  /* 0x1A5D64..0x1A5D68 */
    const float ux = em_ee_div(dx, len);                              /* 0x1A5D88: sp+D0 */
    const float uz = em_ee_div(dz, len);                              /* 0x1A5DA0: sp+D4 */
    const float back = em_ee_neg(chord);                              /* 0x1A5D7C */
    float pts[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };           /* sp+A0..sp+B4 */
    pts[0] = em_ee_madd(em_ee_adda(rx, px), ux, back);                /* 0x1A5DB0, 0x1A5DB4 */
    pts[2] = em_ee_madd(em_ee_adda(rz, pz), uz, back);                /* 0x1A5DC4, 0x1A5DC8 */
    pts[3] = em_ee_madd(em_ee_adda(rx, px), ux, chord);               /* 0x1A5DD4, 0x1A5DD8 */
    pts[5] = em_ee_madd(em_ee_adda(rz, pz), uz, chord);               /* 0x1A5DE4, 0x1A5DE8 */
    const float dy = em_ee_sub(s->end[1], s->start[1]);               /* 0x1A5E00: sp+C4 */
    const float ylo = em_ee_sub(py, half);                            /* 0x1A5E10: sp+D0 */
    const float yhi = em_ee_add(py, half);                            /* 0x1A5E20: sp+D4 */

    if (below_1e5(dx) && below_1e5(dz)) {                             /* 0x1A5E28..0x1A5E98 */
        if (!above_1e5(dy)) return 0;                                 /* 0x1A5EA0..0x1A5ED4 */
        if (em_ee_c_le(s->start[1], s->end[1])) {                     /* 0x1A5EEC: upward */
            if (em_ee_c_le(s->end[1], ylo)) return 0;                 /* 0x1A5F7C */
            if (!em_ee_c_lt(s->start[1], ylo)) return 0;              /* 0x1A5F8C */
            s->cell_class = 0x8000;                                   /* 0x1A5FAC */
            s->cell_normal[1] = f_bits(MINUS_ONE_BITS);               /* 0x1A5FB8 */
            s->point[1] = ylo;                                        /* 0x1A5FF0 */
        } else {
            if (em_ee_c_le(s->start[1], yhi)) return 0;               /* 0x1A5F00 */
            if (!em_ee_c_lt(s->end[1], yhi)) return 0;                /* 0x1A5F10 */
            s->cell_class = 0x4000;                                   /* 0x1A5F30 */
            s->cell_normal[1] = f_bits(ONE_BITS);                     /* 0x1A5F3C */
            s->point[1] = yhi;                                        /* 0x1A5F74 */
        }
        s->point[0] = s->start[0];                                    /* 0x1A5F44 / 0x1A5FC0 */
        s->cell_normal[2] = 0.0f;                                     /* 0x1A5F4C / 0x1A5FC8 */
        s->cell_normal[0] = 0.0f;                                     /* 0x1A5F54 / 0x1A5FD0 */
        s->point[2] = s->start[2];                                    /* 0x1A5F68 / 0x1A5FE4 */
        return 1;
    }

    int side = 0;
    if (!em_ee_c_le(dz, dx)) {                                        /* 0x1A6008: z parameter */
        const float za = s->start[2], zb = s->end[2], ya = s->start[1];
        pts[1] = em_ee_add(ya, em_ee_div(em_ee_mul(em_ee_sub(pts[2], za), dy), dz));   /* 0x1A6030..0x1A6044 */
        if (!em_ee_c_le(pts[1], ylo) && em_ee_c_lt(pts[1], yhi)) {    /* 0x1A604C, 0x1A6060 */
            if (em_ee_c_lt(pts[2], zb) && !em_ee_c_le(pts[2], za)) side = 1;          /* 0x1A607C, 0x1A608C */
            else if (!em_ee_c_le(pts[2], zb) && em_ee_c_lt(pts[2], za)) side = 1;     /* 0x1A609C, 0x1A60AC */
        }
        if (!side)
            pts[4] = em_ee_add(ya, em_ee_div(em_ee_mul(em_ee_sub(pts[5], za), dy), dz));  /* 0x1A6140..0x1A615C */
    } else {                                                          /* 0x1A6160: x parameter */
        const float xa = s->start[0], xb = s->end[0], ya = s->start[1];
        pts[1] = em_ee_add(ya, em_ee_div(em_ee_mul(em_ee_sub(pts[0], xa), dy), dx));   /* 0x1A6178..0x1A6190 */
        int within = 0;
        if (em_ee_c_lt(pts[0], xb) && !em_ee_c_le(pts[0], xa)) within = 1;            /* 0x1A619C, 0x1A61AC */
        else if (!em_ee_c_le(pts[0], xb) && em_ee_c_lt(pts[0], xa)) within = 1;       /* 0x1A61BC, 0x1A61CC */
        if (within && !em_ee_c_le(pts[1], ylo) && em_ee_c_lt(pts[1], yhi)) side = 1;  /* 0x1A61E4, 0x1A61F8 */
        if (!side)
            pts[4] = em_ee_add(ya, em_ee_div(em_ee_mul(em_ee_sub(pts[3], xa), dy), dx));  /* 0x1A628C..0x1A62A0 */
    }
    if (side) {                                                       /* 0x1A60BC / 0x1A6208 */
        memcpy(s->point, pts, sizeof s->point);                       /* 0x1A60CC loop: 0x700031B0 = sp+A0 */
        s->cell_class = 0x2000;                                       /* 0x1A60F4 */
        s->cell_normal[0] = em_ee_div(em_ee_sub(pts[0], px), radius); /* 0x1A6108, 0x1A610C */
        s->cell_normal[1] = 0.0f;                                     /* 0x1A6118 */
        s->cell_normal[2] = em_ee_div(em_ee_sub(pts[2], pz), radius); /* 0x1A6128, 0x1A612C */
        return 1;
    }
    const float y1 = pts[1], y2 = pts[4];                             /* 0x1A62A4 */
    float cap;
    if (!em_ee_c_le(y1, y2)) {                                        /* 0x1A62AC: falling across the top */
        if (em_ee_c_le(y1, yhi)) return 0;                            /* 0x1A62C0 */
        if (!em_ee_c_lt(y2, yhi)) return 0;                           /* 0x1A62D0 */
        cap = yhi;
        s->cell_class = 0x4000;                                       /* 0x1A6324 */
        s->cell_normal[1] = f_bits(ONE_BITS);                         /* 0x1A632C */
    } else {                                                          /* 0x1A635C: rising across the bottom */
        if (!em_ee_c_lt(y1, ylo)) return 0;                           /* 0x1A6360 */
        if (em_ee_c_le(y2, ylo)) return 0;                            /* 0x1A6370 */
        cap = ylo;
        s->cell_class = 0x8000;                                       /* 0x1A63C4 */
        s->cell_normal[1] = f_bits(MINUS_ONE_BITS);                   /* 0x1A63CC */
    }
    const float span = em_ee_sub(y2, y1);                             /* 0x1A62E0 / 0x1A6380 */
    const float k = em_ee_div(em_ee_sub(cap, y1), span);              /* 0x1A62F0, 0x1A6300 */
    s->point[0] = em_ee_add(pts[0], em_ee_mul(k, em_ee_sub(pts[3], pts[0])));  /* 0x1A6304..0x1A6310 */
    s->cell_normal[2] = 0.0f;                                         /* 0x1A6338 / 0x1A63D8 */
    s->cell_normal[0] = 0.0f;                                         /* 0x1A6340 / 0x1A63E0 */
    s->point[2] = em_ee_add(pts[2], em_ee_mul(k, em_ee_sub(pts[5], pts[2])));  /* 0x1A6320..0x1A634C */
    s->point[1] = cap;                                                /* 0x1A6358 / 0x1A63F8 */
    return 1;
}

/* ---- The cell walkers 001A0B10 and 001A1390 -------------------------------- */

enum { WALK_SEGMENT, WALK_CAMERA };   /* 001A0B10, 001A1390 */

/* One prim of a hull: the test, then the cursor advance. The two walkers
 * dispatch identically (0x1A109C / 0x1A1124, 0x1A1594 / 0x1A191C): 0x1000
 * -> 001A4030, 0x2000 -> 001A50A0, 0x4000 -> 001A5C30, 0x8000 advances
 * without a call, any other type neither calls nor advances. *hit (the
 * walker's v0) is set only by a call. Returns 0, or -1 on a fault. */
static int prim_step(const EmActorCellTable *t, uint32_t *cursor, const EmSdkMathContext *math,
                     EmCollProbeState *s, EmCollSegmentFaceScratch *x, int *hit)
{
    const uint32_t at = *cursor;
    if ((uint64_t)at + 4 > t->size) return -1;
    const uint8_t *p = t->bytes + at;
    const uint16_t h = rd_u16(p, 0);
    uint32_t size;
    switch (h & 0xF000) {
    case 0x1000: size = (h & 0x800) ? 0x24u + 0x30u * p[2] : 0x14u + 0x18u * p[2]; break;
    case 0x2000: size = 0x1C; break;
    case 0x4000: size = (h & 0x800) ? 0x2C : 0x18; break;
    case 0x8000: size = (h & 0x800) ? 0x24 : 0x14; break;
    default: return 0;
    }
    if ((uint64_t)at + size > t->size) return -1;
    int r;
    switch (h & 0xF000) {
    case 0x1000: r = em_coll_probe_001A4030(p, s); break;            /* 0x1A1144 / 0x1A11CC */
    case 0x2000: r = em_coll_segment_001A50A0(p, s, x); break;       /* 0x1A1130 / 0x1A11B8 */
    case 0x4000: r = em_coll_segment_001A5C30(math, p, s); break;    /* 0x1A1100 / 0x1A1188 */
    default: *cursor = at + size; return 0;                           /* 0x1A10E0 / 0x1A1168: no call */
    }
    if (r < 0) return -1;
    *hit = r;
    *cursor = at + size;
    return 0;
}

/* The published class-4 entry j (D_00275B7C[j]), or NULL. */
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

/* Pass 1's hull offset keeps the low 30 bits of the word (the dsll32 /
 * dsrl32 pair by 2). In 001A0B10 a word may carry 0x20000000; tbl +
 * 0x20000000 + offset is then read through the EE's uncached main-RAM
 * mirror (0x20000000..0x21FFFFFF), the same bytes as tbl + offset.
 * 001A1390 skips such words. */
static uint32_t static_hull(uint32_t word)
{
    const uint32_t off = word & 0x3FFFFFFFu;
    return off & 0x20000000u ? off & 0x1FFFFFFFu : off;
}

/* 001A0B10's static kind gate (0x1A0F7C..0x1A0FFC): kinds below 0x50 pass;
 * 0x50 and 0x5A or more are skipped; 0x51 needs 0x7000324E == 0, 0x52 needs
 * 2, 0x53 is skipped when it is -1; 0x54..0x59 pass. 0019D330 applies the
 * same gate to the grid attribute (0x19D614..0x19D694). */
static int segment_kind_passes(int16_t kind, int16_t query_class)
{
    if (kind < 0x50) return 1;
    if (kind >= 0x5A) return 0;
    if (kind == 0x50) return 0;
    if (kind == 0x51 && query_class != 0) return 0;
    if (kind == 0x52 && query_class != 2) return 0;
    if (kind == 0x53 && query_class == -1) return 0;
    return 1;
}

typedef struct {
    float xmin, xmax, ymin, ymax, zmin, zmax;   /* f20 / f23, f21 / f24, f22 / f25 */
} Box;

static void box_init(Box *b, const EmCollProbeState *s)
{
    /* 0x1A0E60..0x1A0EEC (001A1390: 0x1A13E0..0x1A146C). */
    if (em_ee_c_le(s->start[0], s->end[0])) { b->xmin = s->start[0]; b->xmax = s->end[0]; }
    else { b->xmin = s->end[0]; b->xmax = s->start[0]; }
    if (em_ee_c_le(s->start[1], s->end[1])) { b->ymin = s->start[1]; b->ymax = s->end[1]; }
    else { b->ymin = s->end[1]; b->ymax = s->start[1]; }
    if (em_ee_c_le(s->start[2], s->end[2])) { b->zmin = s->start[2]; b->zmax = s->end[2]; }
    else { b->zmin = s->end[2]; b->zmax = s->start[2]; }
}

/* The re-clamp after a hit: only the bound on the end's side moves
 * (0x1A1244..0x1A12C0, 0x1A1298..0x1A1320). */
static void box_clamp(Box *b, const EmCollProbeState *s)
{
    if (em_ee_c_le(s->start[0], s->end[0])) b->xmax = s->end[0]; else b->xmin = s->end[0];
    if (em_ee_c_le(s->start[1], s->end[1])) b->ymax = s->end[1]; else b->ymin = s->end[1];
    if (em_ee_c_le(s->start[2], s->end[2])) b->zmax = s->end[2]; else b->zmin = s->end[2];
}

/* The hull box test in the walkers' order x, z, y (0x1A1010..0x1A1084). */
static int box_overlaps(const Box *b, const uint8_t *hull)
{
    if (em_ee_c_lt(b->xmax, rd_f(hull, 0))) return 0;
    if (!em_ee_c_le(b->xmin, rd_f(hull, 0xC))) return 0;
    if (em_ee_c_lt(b->zmax, rd_f(hull, 8))) return 0;
    if (!em_ee_c_le(b->zmin, rd_f(hull, 0x14))) return 0;
    if (em_ee_c_lt(b->ymax, rd_f(hull, 4))) return 0;
    if (!em_ee_c_le(b->ymin, rd_f(hull, 0x10))) return 0;
    return 1;
}

static int cell_walk(const EmCollProbeWorld *world, const EmSdkMathContext *math, EmCollProbeState *s,
                     EmCollSegmentFaceScratch *x, int walk)
{
    if (!world || !world->cells || !world->cells->table || !world->cells->table->bytes ||
        !world->cells->lists || !s || !x) return -1;
    const EmActorCollisionWorld *w = world->cells;
    const EmActorCellTable *t = w->table;
    s->record = EM_COLL_PROBE_RECORD_CELL;                            /* 0x1A0E5C / 0x1A13DC: 0x700031D0 = D_700030B0 */
    s->node = -1;
    Box box;
    box_init(&box, s);
    int result = 0;                                                   /* s0 */
    /* Pass 1: the static cells (0x1A0F00 / 0x1A1480). */
    for (int i = 0; i < t->count; ++i) {
        if ((uint64_t)4 + 4u * (unsigned)i + 4 > t->size) return -1;
        const uint32_t word = rd_u32(t->bytes, 4 + 4u * (unsigned)i);   /* 0x1A0F14 / 0x1A1494 */
        if (!(word & 0x80000000u)) break;                             /* 0x1A0F1C / 0x1A149C: -> pass 2 */
        uint8_t kind;
        if (walk == WALK_SEGMENT) {
            if (word & 0x40000000u) continue;                         /* 0x1A0F2C */
            if (static_kind(w, i, &kind)) return -1;                  /* 0x1A0F6C */
            s->span_hi = kind;                                        /* 0x1A0F70: 0x70003B88 */
            if (!segment_kind_passes(s->span_hi, s->query_class)) continue;   /* 0x1A0F7C..0x1A0FFC */
        } else {
            if (word & 0x60000000u) continue;                         /* 0x1A14AC */
            if (static_kind(w, i, &kind)) return -1;                  /* 0x1A14E8 */
            if (kind >= 0x51) continue;                               /* 0x1A14EC */
        }
        const uint32_t hull = static_hull(word);                      /* 0x1A1004 / 0x1A14FC */
        if ((uint64_t)hull + 0x18 > t->size) return -1;
        if (!box_overlaps(&box, t->bytes + hull)) continue;           /* 0x1A1010 / 0x1A1508 */
        if ((uint64_t)hull + 0x1A > t->size) return -1;
        const int16_t count = rd_s16(t->bytes, hull + 0x18);          /* 0x1A11A0: hull +0x18 */
        uint32_t cursor = hull + 0x1C;                                /* 0x1A108C */
        int hit = 0;                                                  /* 0x1A1090 */
        for (int16_t j = 0; j < count; ++j) {
            if (prim_step(t, &cursor, math, s, x, &hit)) return -1;
            if (hit) break;                                           /* 0x1A1190 */
        }
        if (!hit) continue;                                           /* 0x1A11B0 */
        memcpy(s->end, s->point, sizeof s->point);                    /* 0x1A11C8 loop: 0x700031A0 = 0x700031B0 */
        s->entity = NULL;                                             /* 0x1A11E8 */
        result = 1;                                                   /* 0x1A11FC */
        s->cell_class = (uint16_t)((s->cell_class & 0xFF00) | kind);  /* 0x1A1254: the kind re-read */
        box_clamp(&box, s);                                           /* 0x1A1244..0x1A12C0 */
    }
    /* Pass 2: the published class-4 owners (0x1A0FF0 / 0x1A17E8). */
    const EmActorClassList *list = &w->lists->list[EM_ACTOR_LIST_CLASS4];
    for (int j = 0; j < list->published; ++j) {
        const EmActor *a = owner_at(w, j);                            /* 0x1A0FFC */
        if (!a) return -1;
        if (a->status == 0) continue;                                 /* 0x1A1004 */
        if ((a->cls & 0x1F) != 4) continue;                           /* 0x1A1018 */
        if (s->self == (const void *)a) continue;                     /* 0x1A1028 */
        const unsigned uid = (a->uid >> 8) & 0xFF;                    /* 0x1A1030 */
        if (uid == 0xFF) continue;                                    /* 0x1A1040 */
        /* 0x1A1058 reads the word before the uid < count test at 0x1A106C;
         * a uid at or beyond the count is skipped whatever the word holds,
         * so the order is kept only where the word lies in the image. */
        if ((uint64_t)4 + 4u * uid + 4 > t->size) {
            if (!((int)uid < t->count)) continue;
            return -1;
        }
        const uint32_t word = rd_u32(t->bytes, 4 + 4u * uid);         /* 0x1A1058 */
        if (!word) continue;                                          /* 0x1A105C */
        if (!((int)uid < t->count)) continue;                         /* 0x1A106C */
        if (word & 0x80000000u) return -1;   /* hull = tbl + word unmasked (0x1A1080): outside the image */
        if ((uint64_t)word + 0x18 > t->size) return -1;
        if (!box_overlaps(&box, t->bytes + word)) continue;           /* 0x1A1084..0x1A10F4 */
        const uint8_t kind = (uint8_t)a->kind;                        /* 0x1A10FC: +0x54 byte */
        if (kind >= (walk == WALK_SEGMENT ? 0x50 : 0x51)) continue;   /* 0x1A1100 / 0x1A18F8 */
        if ((uint64_t)word + 0x1A > t->size) return -1;
        const int16_t count = rd_s16(t->bytes, word + 0x18);          /* 0x1A1280 */
        uint32_t cursor = word + 0x1C;                                /* 0x1A1110 */
        int found = 0, hit = 0;                                       /* s5, v0 (0x1A1114, 0x1A1120) */
        for (int16_t n = 0; n < count; ++n) {
            if (prim_step(t, &cursor, math, s, x, &hit)) return -1;
            if (!hit) continue;                                       /* 0x1A1218 */
            found = 1;                                                /* 0x1A1224 */
            memcpy(s->end, s->point, sizeof s->point);                /* 0x1A1230 loop */
            s->entity = a;                                            /* 0x1A1250: 0x700031D4 */
            result = 1;                                               /* 0x1A1260 */
            s->cell_class = (uint16_t)((s->cell_class & 0xFF00) | kind);  /* 0x1A1270 */
            /* v0 was the copy loop's counter and leaves it at 3, so a
             * following 0x8000 prim (no call) repeats these stores. */
            hit = 3;                                                  /* 0x1A122C..0x1A1244 */
        }
        if (!found) continue;                                         /* 0x1A1290 */
        box_clamp(&box, s);                                           /* 0x1A1298..0x1A1320 */
    }
    return result;
}

int em_coll_segment_001A0B10(const EmCollProbeWorld *world, const EmSdkMathContext *math,
                             EmCollProbeState *s, EmCollSegmentFaceScratch *x)
{
    return cell_walk(world, math, s, x, WALK_SEGMENT);
}

int em_coll_segment_001A1390(const EmCollProbeWorld *world, const EmSdkMathContext *math,
                             EmCollProbeState *s, EmCollSegmentFaceScratch *x)
{
    return cell_walk(world, math, s, x, WALK_CAMERA);
}

/* ---- The grid walkers 0019D330 and 0019D770 ---------------------------------- */

static int grid_walk(const EmCollProbeGrid *g, EmCollProbeState *s, int walk)
{
    if (!g || !g->tables || !g->words || !g->emcl || !s) return -1;
    unsigned mask_b, mask_a;                                          /* s5, s6 */
    if (em_ee_c_le(s->start[0], s->end[0])) { mask_b = 2; mask_a = 1; }      /* 0x19D364 */
    else { mask_b = 1; mask_a = 2; }
    if (em_ee_c_le(s->start[1], s->end[1])) { mask_b |= 8; mask_a |= 4; }    /* 0x19D398 */
    else { mask_b |= 4; mask_a |= 8; }
    if (em_ee_c_le(s->start[2], s->end[2])) { mask_b |= 0x20; mask_a |= 0x10; }  /* 0x19D3CC */
    else { mask_b |= 0x10; mask_a |= 0x20; }
    if (em_coll_probe_0019F1A0(g, s, s->start, mask_a)) return -1;    /* 0x19D3F8 */
    if (em_coll_probe_0019F1A0(g, s, s->end, mask_b)) return -1;      /* 0x19D408 */
    int32_t first[6];
    for (int i = 0; i < 6; ++i) first[i] = s->rank[i];                /* 0x19D424 loop: sp+80 */
    if (em_coll_probe_0019F1A0(g, s, s->start, mask_b)) return -1;    /* 0x19D448 */
    if (em_coll_probe_0019F1A0(g, s, s->end, mask_a)) return -1;      /* 0x19D458 */
    /* The span pick over all six directions (0x19D47C..0x19D534). */
    const int32_t n = (int32_t)g->count;
    int32_t best = n;                                                 /* 0x19D464: 0x7000320C */
    int32_t lo = 0, hi = 0;
    int cand = -1;                                                    /* s1, s2, s4 */
    for (int i = 0; i < 6; ++i) {
        const int16_t *helper = g->tables + (size_t)(6 + i) * (size_t)n;   /* *(0x70003228 + 4i) */
        const int32_t at = first[i];
        if (at < 0 || at >= n) return -1;
        if (i & 1) {
            s->span_lo = s->rank[i];                                  /* 0x19D490: 0x70003B86 */
            s->span_hi = helper[at];                                  /* 0x19D4B0: 0x70003B88 */
        } else {
            s->span_lo = helper[at];                                  /* 0x19D4CC */
            s->span_hi = s->rank[i];                                  /* 0x19D4D8 */
            s->span_hi = (int16_t)(s->span_hi + 1);                   /* 0x19D4EC */
        }
        const int32_t diff = (int32_t)s->span_hi - (int32_t)s->span_lo;   /* 0x19D500 */
        if (diff < best) {                                            /* 0x19D504 */
            best = diff;
            lo = s->span_lo;
            hi = s->span_hi;
            cand = i;
        }
    }
    /* With no direction under the node count, s1/s2/s4 are uninitialized
     * registers (0x19D540 indexes a table with s4). */
    if (cand < 0) return -1;
    if (lo < hi && (lo < 0 || hi > n)) return -1;   /* the walk would leave the table */
    const int16_t *table = g->tables + (size_t)cand * (size_t)n;      /* 0x19D548: *(0x70003210 + 4 s4) */
    int found = -1;                                                   /* s3 = 0 */
    float saved[3] = { 0.0f, 0.0f, 0.0f };                            /* sp+A0 */
    for (int32_t k = lo; k < hi; ++k) {                               /* 0x19D550, 0x19D6F4 */
        const int node = table[k];                                    /* 0x19D55C */
        if (node < 0 || (uint32_t)node >= g->count) return -1;
        const int16_t *wd = g->words + 12 * node;                     /* node +0x00.. */
        if (s->rank[0] < wd[6]) continue;                             /* 0x19D57C: +0x0C */
        if (wd[7] < s->rank[1]) continue;                             /* 0x19D594: +0x0E */
        if (s->rank[4] < wd[10]) continue;                            /* 0x19D5AC: +0x14 */
        if (wd[11] < s->rank[5]) continue;                            /* 0x19D5C4: +0x16 */
        if (s->rank[2] < wd[8]) continue;                             /* 0x19D5DC: +0x10 */
        if (wd[9] < s->rank[3]) continue;                             /* 0x19D5F4: +0x12 */
        const uint8_t attr = g->emcl->polys[g->first + (uint32_t)node].attr;   /* 0x19D600: +0x1A */
        s->span_hi = attr;                                            /* 0x19D608: 0x70003B88 */
        if (walk == WALK_SEGMENT) {
            if (!segment_kind_passes(s->span_hi, s->query_class)) continue;   /* 0x19D614..0x19D694 */
        } else {
            if (s->span_hi >= 0x51 && s->span_hi < 0x54) continue;    /* 0x19DA54, 0x19DA60 */
        }
        const int r = em_coll_probe_0019ED80(g, s, node);             /* 0x19D69C / 0x19DA74 */
        if (r < 0) return -1;
        if (!r) continue;
        memcpy(s->end, s->point, sizeof s->point);                    /* 0x19D6C0 loop: +0x10 = +0x20 */
        memcpy(saved, s->point, sizeof saved);                        /* and sp+A0 */
        found = node;                                                 /* 0x19D6E8: s3 = 0x700031D0 */
    }
    if (found < 0) return 0;                                          /* 0x19D700 */
    memcpy(s->point, saved, sizeof saved);                            /* 0x19D718 loop */
    s->record = EM_COLL_PROBE_RECORD_GRID;                            /* 0x19D740: 0x700031D0 = s3 */
    s->node = found;
    return 1;
}

int em_coll_segment_0019D330(const EmCollProbeGrid *grid, EmCollProbeState *s)
{
    return grid_walk(grid, s, WALK_SEGMENT);
}

int em_coll_segment_0019D770(const EmCollProbeGrid *grid, EmCollProbeState *s)
{
    return grid_walk(grid, s, WALK_CAMERA);
}

/* ---- The queries 0019A570 and 0019A910 ----------------------------------------- */

static int query(const EmCollSegment *seg, const float from[3], const float to[3], unsigned mask,
                 int id, int walk)
{
    if (!seg || !seg->world || !seg->state || !seg->face || !from || !to) return -1;
    EmCollProbeState s = *seg->state;
    EmCollSegmentFaceScratch x = *seg->face;
    float saved[3];
    for (int k = 0; k < 3; ++k) {                                     /* 0x19A59C / 0x19A93C loop */
        s.start[k] = from[k];
        saved[k] = to[k];                                             /* sp+40 */
        s.end[k] = to[k];
    }
    s.end[3] = f_bits(ONE_BITS);                                      /* 0x19A5CC / 0x19A974: 0x700031AC */
    s.start[3] = f_bits(ONE_BITS);                                    /* 0x19A5D4 / 0x19A97C: 0x7000319C */
    const unsigned m = mask & 0xFF;                                   /* 0x19A5DC / 0x19A984 */
    s.entity = NULL;                                                  /* 0x19A5EC / 0x19A994 */
    int r = 0;                                                        /* s0 */
    if (m & 1) {
        const EmCollSegmentWorkers *w = seg->workers;
        int locked = 0;
        if (walk == WALK_SEGMENT) {                                   /* 0x19A5F0: 001A6440(id & 0xFFFF) */
            if (!w || !w->lock_6440 || w->lock_6440(w->context, &s, id & 0xFFFF, &locked) < 0) return -1;
        } else {                                                      /* 0x19A998: 001A6AD0(0x40) */
            if (!w || !w->lock_6AD0 || w->lock_6AD0(w->context, &s, 0x40, &locked) < 0) return -1;
        }
        if (locked) {
            memcpy(s.end, s.point, sizeof s.point);                   /* 0x19A614 / 0x19A9B4 loop */
            r = 1;
        }
    }
    if (walk == WALK_SEGMENT) s.query_class = -1;                     /* 0x19A63C: 0x7000324E (0019A910 does not) */
    if (m & 2) {
        s.self = NULL;                                                /* 0x19A650 / 0x19A9E8: 0x70003254 */
        const int hit = walk == WALK_SEGMENT ? em_coll_segment_001A0B10(seg->world, seg->math, &s, &x)   /* 0x19A64C */
                                             : em_coll_segment_001A1390(seg->world, seg->math, &s, &x);  /* 0x19A9E4 */
        if (hit < 0) return -1;
        if (hit) r = 2;
    }
    if (m & 4) {
        const int hit = walk == WALK_SEGMENT ? em_coll_segment_0019D330(seg->world->grid, &s)   /* 0x19A670 */
                                             : em_coll_segment_0019D770(seg->world->grid, &s);  /* 0x19AA04 */
        if (hit < 0) return -1;
        if (hit) r = 4;
    }
    if (r) memcpy(s.end, saved, sizeof saved);                        /* 0x19A698 / 0x19AA2C loop */
    else { s.record = EM_COLL_PROBE_RECORD_NONE; s.node = -1; }       /* 0x19A6C0 / 0x19AA54 */
    s.kind = r;                                                       /* 0x19A6C8 / 0x19AA5C: 0x700031D8 */
    *seg->state = s;
    *seg->face = x;
    return r;
}

int em_coll_segment_0019A570(const EmCollSegment *seg, const float from[3], const float to[3],
                             unsigned mask, int id)
{
    return query(seg, from, to, mask, id, WALK_SEGMENT);
}

int em_coll_segment_0019A910(const EmCollSegment *seg, const float from[3], const float to[3],
                             unsigned mask)
{
    return query(seg, from, to, mask, 0, WALK_CAMERA);
}

/* ---- Binding adapters ------------------------------------------------------------ */

int em_coll_segment_face_worker(void *context, const uint8_t *prim, EmCollProbeState *state, int *hit)
{
    const EmCollSegment *seg = context;
    if (!seg || !seg->face || !hit) return -1;
    const int r = em_coll_segment_001A50A0(prim, state, seg->face);
    if (r < 0) return -1;
    *hit = r;
    return 0;
}

int em_coll_segment_round_worker(void *context, const uint8_t *prim, EmCollProbeState *state, int *hit)
{
    const EmCollSegment *seg = context;
    if (!seg || !hit) return -1;
    const int r = em_coll_segment_001A5C30(seg->math, prim, state);
    if (r < 0) return -1;
    *hit = r;
    return 0;
}

int em_coll_segment_query(void *context, const float from[4], const float to[4], unsigned mask, int id)
{
    return em_coll_segment_0019A570(context, from, to, mask, id);
}

int em_coll_segment_query_i32(void *context, const float from[3], const float to[3], int32_t mask,
                              int32_t id)
{
    return em_coll_segment_0019A570(context, from, to, (unsigned)mask, id);
}

int em_coll_segment_query_result(void *context, const float from[4], const float to[4], unsigned mask,
                                 int id, int *result)
{
    if (!result) return -1;
    const int r = em_coll_segment_0019A570(context, from, to, mask, id);
    if (r < 0) return -1;
    *result = r;
    return 0;
}

int em_coll_segment_hit(const EmCollSegment *seg, EmCollSegmentHit *out)
{
    if (!seg || !seg->state || !seg->world || !out) return -1;
    const EmCollProbeState *s = seg->state;
    memset(out, 0, sizeof *out);
    if (s->record == EM_COLL_PROBE_RECORD_CELL) {
        memcpy(out->record_normal, s->cell_normal, sizeof out->record_normal);   /* D_700030B0 +0x24 */
    } else if (s->record == EM_COLL_PROBE_RECORD_GRID) {
        const EmCollProbeGrid *g = seg->world->grid;
        if (!g || !g->emcl || s->node < 0 || (uint32_t)s->node >= g->count) return -1;
        memcpy(out->record_normal, g->emcl->polys[g->first + (uint32_t)s->node].plane,
               sizeof out->record_normal);                            /* node +0x24 */
    } else {
        return -1;
    }
    out->kind = s->kind;
    memcpy(out->point, s->point, sizeof out->point);
    out->record_node = em_coll_probe_record_node(seg->world->grid, s);
    out->entity = s->entity;
    if (s->entity) {
        out->entity_flags = s->entity->cls;                           /* *(0x700031D4) + 2 */
        out->entity_type = s->entity->model;                          /* *(0x700031D4) + 3 */
    }
    return 0;
}
