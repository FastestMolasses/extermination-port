/* em_camera_leftovers_solver.c - the desired-eye solvers 0018DD20 and
 * 0018F870 (aim) and the bounds routines 0018CE60 / 0018D910 (see
 * em_camera_leftovers.h, docs/CAMERA_LEFTOVERS.md).
 *
 * All four are NEARMISS C in the decomp; they were translated from their
 * instructions (build/asm). Where the readable C differs, the instructions
 * win (docs/CAMERA_LEFTOVERS.md lists the places; for example 0018D910
 * keeps 0x70003A3C unchanged when its area-0x12 probe misses, where the C
 * writes cam+14 + 200). Every address in a comment is the original
 * instruction or label translated there.
 *
 * Scratchpad names: S(xxxx) is the word at 0x7000xxxx of the solvers'
 * window (EmCamLeftScratch); sp_a / sp_b are the two stack vectors (sp+A0,
 * sp+B0) of 0018DD20 and 0018F870. */
#include "game/em_camera_leftovers_internal.h"

#include <stddef.h>
#include <string.h>

#define S(a) em_camleft_spad(w->scratch, UINT32_C(0x70000000) | (a))

/* One 0019A910 call: *result its v0; the hit words update w->hit. */
static int segment(EmCamLeftWorld *w, const void *from, const void *to, int mask, int *result)
{
    uint32_t a[4], b[4];
    memcpy(a, from, 16);
    memcpy(b, to, 16);
    const EmCamLeftWorkers *k = w->workers;
    if (!k->segment || k->segment(k->context, a, b, mask, w->hit, result) < 0)
        return cl_fault(w, 0x0019A910);
    return 0;
}

#define SEG(from, to, out) do { if (segment(w, (from), (to), mask, (out)) < 0) return -1; } while (0)
#define VU(expr, address) do { if ((expr) < 0) return cl_fault(w, (address)); } while (0)
#define WRAP(x, out) CL_CALL(w, 0x001B1470, k->wrap, k->context, (x), (out))

/* The hit record's normal into v (xyz) with v.w = 1.0 (the x/y/z/w stores
 * the solvers repeat after a hit). */
static void normal_w1(const EmCamLeftWorld *w, uint32_t *v)
{
    v[0] = w->hit->normal[0];
    v[1] = w->hit->normal[1];
    v[2] = w->hit->normal[2];
    v[3] = CL_ONE;
}

/* ======================================================================
 * 0018CE60(cam, v, style)
 * ====================================================================== */
int em_camleft_0018CE60(EmCamLeftWorld *w, const void *v, int style)
{
    if (!cl_world_ok(w) || !v) return cl_fault(w, 0x0018CE60);
    CL_NEED(w, 0x0019A910, w->workers->segment);
    EmCameraFollowRecord *c = w->cam;
    int mask = style == 2 ? 7 : 6;                                     /* 0018CE80..94 */
    uint32_t *c0 = S(0x39C0), *d0 = S(0x39D0), *e0 = S(0x39E0);
    uint32_t *a34 = S(0x3A34), *a38 = S(0x3A38), *a3c = S(0x3A3C);
    int r;
    uint32_t dot;
    cl_v_copy(c0, v);                                                  /* 0018CE9C */
    d0[0] = c0[0];                                                     /* 0018CEEC.. */
    d0[1] = cl_sub(c0[1], CL_200);
    d0[2] = c0[2];
    d0[3] = CL_ONE;
    SEG(c0, d0, &r);                                                   /* 0018CF08 */
    if (r != 0) {
        *a38 = w->hit->point[1];                                       /* 0018CF20 */
        int take = 1;
        if ((w->hit->record_1A & 0x5000) == 0) {                       /* 0018CF28 */
            d0[0] = 0; d0[1] = CL_ONE; d0[2] = 0; d0[3] = CL_ONE;      /* 0018CFD0.. */
            e0[0] = w->hit->normal[0];
            e0[1] = w->hit->normal[1];
            e0[2] = w->hit->normal[2];
            e0[3] = CL_ONE;
            VU(cl_v_dot(d0, e0, &dot), 0x00102738);                    /* 0018D034 */
            *a34 = dot;
            if (cl_le(dot, CL_0_17)) take = 0;                         /* 0018D04C */
        }
        if (take) {
            if (style != 2)                                            /* 0018CF34 / 0018D058 */
                *a38 = cl_add(*a38, cl_eq(CL_ONE, cl_cw(c, 0x5C)) ? CL_6 : CL_17);
            else
                *a38 = cl_add(*a38, CL_2);
        }
    } else {
        *a38 = cl_sub(cl_cw(c, 0x50), CL_200);                         /* 0018D0EC */
    }
    d0[0] = c0[0];                                                     /* 0018D104.. */
    d0[1] = cl_add(CL_200, c0[1]);
    d0[2] = c0[2];
    d0[3] = CL_ONE;
    SEG(c0, d0, &r);                                                   /* 0018D14C */
    if (r != 0) {
        *a3c = w->hit->point[1];                                       /* 0018D168 */
        int take = 1;
        if ((w->hit->record_1A & 0x8800) == 0) {                       /* 0018D170 */
            d0[0] = 0; d0[1] = CL_M1; d0[2] = 0; d0[3] = CL_ONE;       /* 0018D1B8.. */
            e0[0] = w->hit->normal[0];
            e0[1] = w->hit->normal[1];
            e0[2] = w->hit->normal[2];
            e0[3] = CL_ONE;
            VU(cl_v_dot(d0, e0, &dot), 0x00102738);                    /* 0018D21C */
            *a34 = dot;
            if (cl_le(dot, CL_0_17)) take = 0;                         /* 0018D234 */
        }
        if (take) *a3c = cl_sub(*a3c, CL_ONE);                         /* 0018D184 / 0018D24C */
    } else {
        uint32_t y;
        memcpy(&y, (const uint8_t *)v + 4, 4);                         /* 0018D264: a1+4 read again */
        *a3c = cl_add(CL_200, y);
    }
    if (!cl_le(*a38, *a3c)) *a38 = cl_sub(*a3c, CL_3);                 /* 0018D27C..9C */
    cl_cset(c, 0x50, *a38);                                            /* 0018D2B8 */
    cl_cset(c, 0x54, *a3c);                                            /* 0018D2C4 */
    if (style != 5) {                                                  /* 0018D2C4 */
        if (cl_lt(cl_cw(c, 0x14), cl_cw(c, 0x50))) cl_cset(c, 0x14, cl_cw(c, 0x50)); /* 0018D2D0 */
        if (!cl_le(cl_cw(c, 0x14), cl_cw(c, 0x54))) cl_cset(c, 0x14, cl_cw(c, 0x54)); /* 0018D2EC */
    }
    return 0;
}

/* ======================================================================
 * 0018D910(cam, e, mask): the fixed-camera bounds.
 * ====================================================================== */
int em_camleft_0018D910(EmCamLeftWorld *w, EmPlayerLiveActor *e, int mask)
{
    if (!cl_world_ok(w) || !e || !w->globals->follow->area) return cl_fault(w, 0x0018D910);
    CL_NEED(w, 0x0019A910, w->workers->segment);
    EmCameraFollowRecord *c = w->cam;
    uint32_t *f0 = S(0x38F0), *a0 = S(0x38A0), *b0 = S(0x38B0);
    uint32_t *a38 = S(0x3A38), *a3c = S(0x3A3C);
    int r;
    uint32_t dot;
    cl_v_copy(f0, e->bytes + 0xA0);                                    /* 0018D938 */
    f0[1] = cl_add(f0[1], CL_11);                                      /* 0018D958 */
    VU(cl_v_sub(f0, c->bytes + 0x10, f0), 0x001028D0);                 /* 0018D964 */
    VU(cl_v_normalize(f0, f0), 0x00102760);                            /* 0018D974 */
    VU(cl_v_sub(f0, c->bytes + 0x10, f0), 0x001028D0);                 /* 0018D988 */
    a0[0] = f0[0];                                                     /* 0018D9CC.. */
    a0[1] = cl_sub(f0[1], CL_200);
    a0[2] = f0[2];
    a0[3] = CL_ONE;
    SEG(f0, a0, &r);                                                   /* 0018D9E4 */
    if (r != 0 && (w->hit->record_1A & 0x7000) != 0) {                 /* 0018D9EC..FC */
        *a38 = w->hit->point[1];                                       /* 0018DA14 */
        *a38 = cl_add(*a38, cl_eq(CL_ONE, cl_cw(c, 0x5C)) ? CL_6 : CL_17); /* 0018DA24..6C */
    } else {
        *a38 = cl_sub(cl_cw(c, 0x50), CL_200);                         /* 0018DA94 */
    }
    b0[0] = f0[0];                                                     /* 0018DAAC.. */
    b0[1] = cl_add(CL_200, f0[1]);
    b0[2] = f0[2];
    b0[3] = CL_ONE;
    SEG(f0, b0, &r);                                                   /* 0018DB00 */
    if (r != 0 && (w->hit->record_1A & 0x8800) != 0) {                 /* 0018DB08..24 */
        *a3c = w->hit->point[1];                                       /* 0018DB44 */
        *a3c = cl_sub(w->hit->point[1], CL_ONE);                       /* 0018DB54 */
    } else if (*w->globals->follow->area == 0x12) {                    /* 0018DB58..64 */
        b0[0] = cl_pw(e, 0xB0);                                        /* 0018DB80.. */
        b0[1] = cl_add(CL_200, cl_pw(e, 0xB4));
        b0[2] = cl_pw(e, 0xB8);
        b0[3] = CL_ONE;
        SEG(e->bytes + 0xB0, b0, &r);                                  /* 0018DBB4 */
        if (r != 0) {                                                  /* 0018DBBC: a miss keeps 0x70003A3C */
            b0[0] = 0; b0[1] = CL_M1; b0[2] = 0; b0[3] = CL_ONE;       /* 0018DBC4.. */
            normal_w1(w, f0);                                          /* 0018DC00.. */
            VU(cl_v_dot(b0, f0, &dot), 0x00102738);                    /* 0018DC34 */
            *a3c = dot;
            if (cl_le(dot, CL_0_2)) {                                  /* 0018DC4C */
                *a3c = cl_add(CL_200, cl_cw(c, 0x14));                 /* 0018DC80 */
            } else {
                *a3c = w->hit->point[1];                               /* 0018DC6C */
                *a3c = cl_sub(w->hit->point[1], CL_ONE);
            }
        }
    } else {
        *a3c = cl_add(CL_200, cl_cw(c, 0x14));                         /* 0018DC9C */
    }
    if (!cl_le(*a38, *a3c)) *a38 = cl_sub(*a3c, CL_3);                 /* 0018DCB4..D4 */
    cl_cset(c, 0x50, *a38);                                            /* 0018DCEC */
    cl_cset(c, 0x54, *a3c);                                            /* 0018DCF8 */
    return 0;
}

/* ======================================================================
 * 0018DD20(cam, e, style, mask): the desired-eye solver.
 * ====================================================================== */

/* The wall slide of one probe side (0018E938.. for side A, 0018EF28.. for
 * side B): the flag bits it may set are (both, wall, other) =
 * (0x2 / 0xA / 0x3) for A and (0x4 / 0x8 / 0x1) for B. `add` picks the
 * second pass's 001028B8 (side B) over the first pass's 001028D0. */
static int slide_side(EmCamLeftWorld *w, uint32_t *sp, uint32_t record, int side_b,
                      uint32_t *flags)
{
    uint32_t *c0 = S(0x38C0), *d0 = S(0x38D0), dot;
    VU(cl_v_sub(d0, S(0x38B0), S(0x3910)), 0x001028D0);                /* 0018E958 / 0018EF48 */
    if (side_b)
        VU(cl_v_add(c0, w->hit->point, S(0x38A0)), 0x001028B8);        /* 0018EF64 */
    else
        VU(cl_v_sub(c0, w->hit->point, S(0x38A0)), 0x001028D0);        /* 0018E974 */
    VU(cl_v_normalize(d0, d0), 0x00102760);                            /* 0018E988 / 0018EF78 */
    int copy_all;
    uint32_t bits;
    if (record & 0x8800) {                                             /* 0018E994 / 0018EF84 */
        uint32_t *down = side_b ? S(0x38B0) : S(0x38F0);
        const uint32_t *normal = side_b ? S(0x38F0) : S(0x3900);
        down[0] = 0; down[1] = CL_M1; down[2] = 0; down[3] = CL_ONE;   /* 0018E9A0.. / 0018EF90.. */
        VU(cl_v_dot(down, normal, &dot), 0x00102738);                  /* 0018E9D4 / 0018EFC4 */
        *S(0x3A3C) = dot;
        if (cl_lt(dot, CL_0_9)) {                                      /* 0018E9EC / 0018EFDC */
            copy_all = 0; bits = side_b ? 0x4 : 0x2;
        } else {
            copy_all = 1; bits = side_b ? 0x8 : 0xA;
        }
    } else if (record & 0x2000) {                                      /* 0018EAB4 / 0018F0A4 */
        copy_all = 0; bits = side_b ? 0x4 : 0x2;
    } else {
        copy_all = 1; bits = side_b ? 0x1 : 0x3;                       /* 0018EB1C / 0018F10C */
    }
    if (copy_all) {
        cl_v_copy(sp, c0);                                             /* 0018EA64 etc. */
    } else {
        sp[0] = c0[0];                                                 /* 0018EA20 etc. */
        sp[2] = c0[2];
    }
    *flags |= bits;
    sp[0] = cl_add(sp[0], cl_mul(CL_0_1, d0[0]));                      /* 0018EA34..44 */
    sp[2] = cl_add(sp[2], cl_mul(CL_0_1, d0[2]));                      /* 0018EA4C..58 */
    return 0;
}

/* The probe of one side (0018E68C.. for A, 0018EC74.. for B): the hit
 * record's normal into `normal`, the hit point into `point`, and the
 * rejections against the first probe's surface. *hit_out ends as the
 * side's s1 / s7; *steep may be cleared (side B only clears it). */
static int probe_side(EmCamLeftWorld *w, int mask, int first, uint32_t flags,
                      uint32_t *point, uint32_t *normal, int side_b, int reprobe, int *hit_out,
                      int *steep)
{
    uint32_t dot;
    int hit;
    SEG(S(0x3910), S(0x38B0), &hit);                                   /* 0018E68C / 0018EC74 */
    *hit_out = hit;
    if (hit == 0) return 1;                                            /* skip the side */
    cl_v_copy(point, w->hit->point);                                   /* 0018E6A8 / 0018EC90 */
    normal_w1(w, normal);
    if (!first) {                                                      /* 0018E6E4 / 0018ECCC */
        *S(0x3A3C) = 0;                                                /* 0018E8D8 / 0018EED0 */
        return 0;
    }
    int reject = 0;
    if (flags & 8) {                                                   /* 0018E6EC / 0018ECD4 */
        VU(cl_v_dot(normal, S(0x3960), &dot), 0x00102738);
        *S(0x3A3C) = dot;
        if (cl_lt(dot, CL_M0_08)) reject = 1;                          /* 0018E71C / 0018ED04 */
    } else {
        VU(cl_v_dot(normal, S(0x38E0), &dot), 0x00102738);
        *S(0x3A3C) = dot;
        if (cl_lt(dot, CL_M0_998) && !reprobe) {                        /* 001901BC / 001905FC */
            reject = 1;
        } else if (cl_lt(dot, CL_M0_998)) {                            /* 0018E764 / 0018ED50 */
            uint32_t *t = S(0x3910);
            VU(cl_v_sub(t, point, S(0x38B0)), 0x001028D0);             /* 0018E788 / 0018ED74 */
            VU(cl_v_dot(t, t, &dot), 0x00102738);
            *S(0x3A38) = dot;
            if (cl_lt(dot, CL_ONE)) {                                  /* 0018E7B0 / 0018ED9C */
                reject = 1;
            } else {
                t[0] = *S(0x3950);                                     /* 0018E7FC.. / 0018EDEC.. */
                t[1] = cl_cw(w->cam, 0x14);
                t[2] = *S(0x3958);
                t[3] = CL_ONE;
                SEG(t, S(0x38B0), &hit);                               /* 0018E818 / 0018EE08 */
                *hit_out = hit;
                if (hit == 0) {
                    reject = 1;                                        /* 0018E8C4 / 0018EEB8 */
                } else {
                    cl_v_copy(point, w->hit->point);                   /* 0018E838 / 0018EE28 */
                    normal_w1(w, normal);
                    VU(cl_v_dot(normal, S(0x38E0), &dot), 0x00102738); /* 0018E884 / 0018EE74 */
                    *S(0x3A3C) = dot;
                    if (cl_lt(dot, CL_M0_998)) reject = 1;             /* 0018E89C / 0018EE8C */
                }
            }
        }
    }
    if (reject) {
        *hit_out = 0;
        if (side_b) *steep = 0;
        *S(0x3A3C) = CL_M1;
    }
    return 0;
}

/* The wall slide of 0018DD20 (0018E4C8..0018F2AC, aim = 0) and of
 * 0018F870's second pass (0018FF78..00190A00, aim = 1): two side probes
 * 5.5 units either side of the view, each kept or rejected against the
 * first probe's surface and slid along its own, then the pick. The two
 * copies differ only where `aim` is tested. */
static int slide_block(EmCamLeftWorld *w, int mask, int first, uint32_t *flags, int *steep, int aim)
{
    const EmCamLeftWorkers *k = w->workers;
    EmCameraFollowRecord *c = w->cam;
    uint8_t *cb = c->bytes;
    uint32_t sp_a[4], sp_b[4], v;
    uint32_t obj[3], h;
    memcpy(obj, cb + 0x10, 12);
    CL_CALL(w, 0x001B1240, k->heading, k->context, obj, cl_cw(c, 0x20), cl_cw(c, 0x28), &h);
    uint32_t side;
    WRAP(cl_sub(h, CL_HALF_PI), &side);                                /* 0018E4E4 / 0018FF94 */
    uint32_t *a0 = S(0x38A0), *b0 = S(0x38B0), *p10 = S(0x3910);
    CL_CALL(w, 0x0011E2A8, k->sine, k->context, side, &v);
    a0[0] = cl_mul(CL_5_5, v);                                         /* 0018E508 / 0018FFB8 */
    CL_CALL(w, 0x0011DE90, k->cosine, k->context, side, &v);
    a0[2] = cl_mul(CL_5_5, v);                                         /* 0018E524 / 0018FFD4 */
    a0[1] = 0;                                                         /* 0018E548 / 0018FFF8 */
    if (first == 0) {
        VU(cl_v_add(b0, cb + 0x10, a0), 0x001028B8);                   /* 0018E544 / 0018FFF4 */
        cl_v_copy(p10, cb + 0x10);                                     /* 0018E554 / 00190004 */
    } else {
        if (aim) {
            VU(cl_v_add(b0, cb + 0x10, a0), 0x001028B8);               /* 00190064 */
        } else {
            VU(cl_v_sub(b0, cb + 0x10, cb + 0x20), 0x001028D0);        /* 0018E5B0 */
            VU(cl_v_normalize(b0, b0), 0x00102760);
            VU(cl_v_scale(b0, b0, CL_M1_5), 0x00103230);
            VU(cl_v_add(b0, b0, cb + 0x10), 0x001028B8);
            VU(cl_v_add(b0, b0, a0), 0x001028B8);                      /* 0018E614 */
        }
        VU(cl_v_normalize(p10, a0), 0x00102760);                       /* 0018E628 / 00190078 */
        VU(cl_v_scale(p10, p10, CL_M3), 0x00103230);
        VU(cl_v_add(p10, cb + 0x10, p10), 0x001028B8);                 /* 0018E65C / 001900AC */
    }
    cl_v_copy(sp_a, cb + 0x10);                                        /* 0018E668 / 001900B8 */
    cl_v_copy(sp_b, cb + 0x10);                                        /* 0018E674 / 001900C4 */

    /* Side A. */
    int hit_a;
    int skip = probe_side(w, mask, first != 0, *flags, S(0x3920), S(0x3900), 0, !aim, &hit_a, steep);
    if (skip < 0) return -1;
    if (skip == 0 && hit_a != 0) {                                     /* 0018E8DC / 001901E0 */
        uint32_t a3c = *S(0x3A3C);
        if (*steep == 1 || (cl_lt(a3c, CL_0_9) && !cl_le(a3c, CL_M0_3))) { /* 0018E8E8..930 */
            if (slide_side(w, sp_a, w->hit->record_1A, 0, flags) < 0) return -1;
        }
    }

    /* Side B (0018EB6C / 00190470). */
    if (first == 0) {
        VU(cl_v_sub(b0, cb + 0x10, a0), 0x001028D0);                   /* 0018EB84 / 00190488 */
        cl_v_copy(p10, cb + 0x10);                                     /* 0018EB94 / 00190498 */
    } else {
        if (aim) {
            VU(cl_v_sub(b0, cb + 0x10, a0), 0x001028D0);               /* 001904B8 */
        } else {
            VU(cl_v_sub(b0, cb + 0x10, cb + 0x20), 0x001028D0);        /* 0018EBB0 */
            VU(cl_v_normalize(b0, b0), 0x00102760);
            VU(cl_v_scale(b0, b0, CL_M1_5), 0x00103230);
            VU(cl_v_add(b0, b0, cb + 0x10), 0x001028B8);
            VU(cl_v_sub(b0, b0, a0), 0x001028D0);                      /* 0018EC14 */
        }
        VU(cl_v_normalize(p10, a0), 0x00102760);                       /* 0018EC28 / 001904CC */
        VU(cl_v_scale(p10, p10, CL_3), 0x00103230);
        VU(cl_v_add(p10, cb + 0x10, p10), 0x001028B8);                 /* 0018EC5C / 00190500 */
    }
    int hit_b;
    skip = probe_side(w, mask, first != 0, *flags, S(0x3930), S(0x38F0), 1, !aim, &hit_b, steep);
    if (skip < 0) return -1;
    if (skip == 0) {
        uint32_t a3c = *S(0x3A3C);
        if (*steep == 1 || (cl_lt(a3c, CL_0_9) && !cl_le(a3c, CL_M0_3))) { /* 0018EED4..F24 / 00190624.. */
            if (slide_side(w, sp_b, w->hit->record_1A, 1, flags) < 0) return -1;
        }
    }

    /* The pick (0018F160 / 001908B0). */
    int average = 0;
    const uint32_t *copy = NULL;
    if ((*flags & 6) == 6) average = 1;
    else if (*flags & 2) { if (hit_b) average = 1; else copy = sp_a; }  /* 0018F1C4 / 00190914 */
    else if (*flags & 4) { if (hit_a) average = 1; else copy = sp_b; }  /* 0018F240 / 00190990 */
    if (average) {
        uint32_t *m = S(0x3940);
        VU(cl_v_add(m, S(0x3920), S(0x3930)), 0x001028B8);             /* 0018F180 / 001908D0 */
        VU(cl_v_scale(m, m, CL_0_5), 0x00103230);
        cl_cset(c, 0x10, m[0]);
        cl_cset(c, 0x18, m[2]);
    } else if (copy) {
        cl_v_copy(cb + 0x10, copy);                                    /* 0018F22C / 0018F2A8 */
    }
    return 0;
}

/* The area-0x12 depth clamp and the height against the first probe's
 * point (0018F2B0..F390 / 00190A00..00190AE4). */
static int depth_and_first_height(EmCamLeftWorld *w, int first)
{
    EmCameraFollowRecord *c = w->cam;
    if (*w->globals->follow->area == 0x12) {
        uint32_t z = cl_cw(c, 0x18);
        if (cl_lt(z, CL_169_5)) cl_cset(c, 0x18, CL_169_5);            /* 0018F2D8..EC */
        else if (!cl_le(z, CL_230_6)) cl_cset(c, 0x18, CL_230_6);      /* 0018F300..10 */
    }
    if (first != 0) {
        uint32_t h = cl_ch(c, 0x58);
        if (!(h & 0x2000)) {
            if (h & 0x8800) {                                          /* 0018F338 / 00190A88 */
                uint32_t lim = cl_sub(*S(0x3954), CL_ONE);
                if (!cl_le(cl_cw(c, 0x14), lim)) cl_cset(c, 0x14, lim);
            } else {                                                   /* 0018F368 / 00190AB8 */
                uint32_t lim = cl_add(CL_ONE, *S(0x3954));
                if (cl_lt(cl_cw(c, 0x14), lim)) cl_cset(c, 0x14, lim);
            }
        }
    }
    return 0;
}

/* The floor and ceiling over the eye (0018F3CC..F7B8, aim = 0;
 * 00190B58..00190EE0, aim = 1): 0x70003A38 / 0x70003A3C, then cam+50 /
 * cam+54. The two copies differ in the 1.5 stretch (00102900, 0018DD20
 * only) and the floor lift (6 / 17 by cam+5C, or 2). */
static int floor_ceiling(EmCamLeftWorld *w, EmPlayerLiveActor *e, int mask, int aim)
{
    EmCameraFollowRecord *c = w->cam;
    uint8_t *cb = c->bytes;
    uint32_t *f0 = S(0x38F0), *a0 = S(0x38A0), *b0 = S(0x38B0);
    uint32_t *a38 = S(0x3A38), *a3c = S(0x3A3C);
    uint32_t dot;
    int r;
    cl_v_copy(f0, e->bytes + 0xA0);
    f0[1] = cl_add(f0[1], CL_11);                                      /* 0018F3F8 / 00190B84 */
    VU(cl_v_sub(f0, cb + 0x10, f0), 0x001028D0);
    VU(cl_v_normalize(f0, f0), 0x00102760);
    if (!aim) VU(cl_v_scale4(f0, f0, CL_1_5), 0x00102900);             /* 0018F438 */
    VU(cl_v_sub(f0, cb + 0x10, f0), 0x001028D0);                       /* 0018F450 / 00190BC0 */
    a0[0] = f0[0];
    a0[1] = cl_sub(f0[1], CL_200);
    a0[2] = f0[2];
    a0[3] = CL_ONE;
    SEG(f0, a0, &r);                                                   /* 0018F4B0 / 00190C20 */
    if (r != 0 && (w->hit->record_1A & 0x7000) != 0) {
        *a38 = w->hit->point[1];
        if (aim)
            *a38 = cl_add(w->hit->point[1], CL_2);                     /* 00190C5C */
        else
            *a38 = cl_add(*a38, cl_eq(CL_ONE, cl_cw(c, 0x5C)) ? CL_6 : CL_17); /* 0018F4F4..48 */
    } else {
        *a38 = cl_sub(cl_cw(c, 0x50), CL_200);                         /* 0018F54C / 00190C74 */
    }
    b0[0] = f0[0];
    b0[1] = cl_add(CL_200, f0[1]);
    b0[2] = f0[2];
    b0[3] = CL_ONE;
    SEG(f0, b0, &r);                                                   /* 0018F5BC / 00190CE4 */
    if (r != 0 && (w->hit->record_1A & 0x8800) != 0) {
        *a3c = w->hit->point[1];
        *a3c = cl_sub(w->hit->point[1], CL_ONE);
    } else if (*w->globals->follow->area == 0x12) {                    /* 0018F610 / 00190D38 */
        b0[0] = cl_pw(e, 0xB0);
        b0[1] = cl_add(CL_200, cl_pw(e, 0xB4));
        b0[2] = cl_pw(e, 0xB8);
        b0[3] = CL_ONE;
        SEG(e->bytes + 0xB0, b0, &r);                                  /* 0018F66C / 00190D94 */
        if (r != 0) {                                                  /* a miss keeps 0x70003A3C */
            b0[0] = 0; b0[1] = CL_M1; b0[2] = 0; b0[3] = CL_ONE;
            normal_w1(w, f0);
            VU(cl_v_dot(b0, f0, &dot), 0x00102738);                    /* 0018F6E4 / 00190E0C */
            *a3c = dot;
            if (cl_le(dot, CL_0_2)) {
                *a3c = cl_add(CL_200, cl_cw(c, 0x14));                 /* 0018F748 / 00190E70 */
            } else {
                *a3c = w->hit->point[1];
                *a3c = cl_sub(w->hit->point[1], CL_ONE);
            }
        }
    } else {
        *a3c = cl_add(CL_200, cl_cw(c, 0x14));                         /* 0018F764 / 00190E8C */
    }
    if (!cl_le(*a38, *a3c)) *a38 = cl_sub(*a3c, CL_3);                 /* 0018F77C / 00190EA4 */
    cl_cset(c, 0x50, *a38);
    cl_cset(c, 0x54, *a3c);
    return 0;
}

int em_camleft_0018DD20(EmCamLeftWorld *w, EmPlayerLiveActor *e, int style, int mask, int *result)
{
    if (!cl_world_ok(w) || !e) return cl_fault(w, 0x0018DD20);
    const EmCameraFollowGlobals *g = w->globals->follow;
    const EmCamLeftWorkers *k = w->workers;
    if (!g->d690 || !g->area) return cl_fault(w, 0x0018DD20);
    CL_NEED(w, 0x0019A910, k->segment);
    CL_NEED(w, 0x001B1470, k->wrap);
    CL_NEED(w, 0x0011E620, k->atan2);
    CL_NEED(w, 0x001B1240, k->heading);
    CL_NEED(w, 0x0011E2A8, k->sine);
    CL_NEED(w, 0x0011DE90, k->cosine);
    EmCameraFollowRecord *c = w->cam;
    uint8_t *cb = c->bytes;
    uint32_t flags = 0;      /* s2 */
    int steep = 0;           /* s3 */
    int resolved = 0;        /* s7 (first half) */
    int first;               /* s0 */
    uint32_t dot, v;
    int r;

    cl_v_copy(S(0x38B0), cb + 0x20);                                   /* 0018DD70 */
    VU(cl_v_sub(S(0x38A0), cb + 0x10, S(0x38B0)), 0x001028D0);         /* 0018DD88 */
    VU(cl_v_normalize(S(0x38A0), S(0x38A0)), 0x00102760);
    VU(cl_v_scale(S(0x38A0), S(0x38A0), CL_1_5), 0x00103230);
    VU(cl_v_add(S(0x38A0), S(0x38A0), cb + 0x10), 0x001028B8);         /* 0018DDD0 */
    SEG(S(0x38B0), S(0x38A0), &first);                                 /* 0018DDE8 */

    if (first != 0) {
        flags = 1;                                                     /* 0018DE14 */
        cl_chset(c, 0x58, w->hit->record_1A);                          /* 0018DE20 */
        cl_v_copy3(S(0x38C0), w->hit->point);                          /* 0018DE1C */
        cl_v_copy(S(0x3950), S(0x38C0));                               /* 0018DE30 */
        VU(cl_v_sub(S(0x38F0), S(0x38B0), S(0x38A0)), 0x001028D0);     /* 0018DE4C */
        *S(0x38F4) = 0;                                                /* 0018DE6C */
        VU(cl_v_normalize(S(0x38F0), S(0x38F0)), 0x00102760);
        cl_v_copy(S(0x3960), S(0x38F0));                               /* 0018DE7C */
        *S(0x38E0) = w->hit->normal[0];                                /* 0018DEA8 */
        *S(0x38E8) = w->hit->normal[2];
        *S(0x38E4) = 0;
        *S(0x38EC) = CL_ONE;
        VU(cl_v_dot(S(0x38F0), S(0x38E0), &dot), 0x00102738);          /* 0018DEC4 */
        *S(0x3A3C) = dot;
        if (cl_lt(dot, CL_0_707)) steep = 1;                           /* 0018DEE4 */
        *S(0x38E4) = w->hit->normal[1];                                /* 0018DF14 */
        VU(cl_v_normalize(S(0x38E0), S(0x38E0)), 0x00102760);
        CL_CALL(w, 0x0011E620, k->atan2, k->context, *S(0x38E0), *S(0x38E8), &v);
        WRAP(v, &v);
        cl_cset(c, 0x90, v);                                           /* 0018DF3C */
        if (style != 3) {
            uint32_t slack = cl_sub(*g->d690, cl_fabs(cl_cw(c, 0x0C))); /* 0018DF58 */
            *S(0x3A20) = slack;
            if (cl_le(slack, CL_ZERO)) {                               /* 0018DF60 */
                uint32_t lift = cl_eq(CL_ONE, cl_cw(c, 0x5C)) ? CL_13 : CL_17_5; /* 0018DF80 */
                uint32_t *p9 = S(0x3900);
                p9[0] = *S(0x38B0);                                    /* 0018DFAC.. */
                p9[1] = cl_add(lift, cl_pw(e, 0xA4));
                p9[2] = *S(0x38B8);
                p9[3] = CL_ONE;
                SEG(p9, S(0x38A0), &r);                                /* 0018E024 */
                if (r == 0) {
                    first = 0;                                         /* 0018E038 */
                } else if (w->hit->record_1A & 0x8800) {               /* 0018E03C..4C */
                    VU(cl_v_sub(p9, w->hit->point, S(0x38A0)), 0x001028D0); /* 0018E068 */
                    VU(cl_v_dot(p9, p9, &dot), 0x00102738);
                    *S(0x3A20) = dot;
                    if (cl_lt(dot, CL_ONE)) first = 0;                 /* 0018E090..A0 */
                }
            }
        }
        if (first != 0) {                                              /* 0018E0A4 */
            uint32_t h = cl_ch(c, 0x58);
            if (style == 3) {                                          /* 0018E0B8 */
                VU(cl_v_sub(S(0x38A0), S(0x38A0), S(0x38B0)), 0x001028D0);
                VU(cl_v_normalize(S(0x38A0), S(0x38A0)), 0x00102760);
                h = cl_ch(c, 0x58);                                    /* 0018E0E8 */
                if (h & 0xD800) {
                    uint32_t y = *S(0x38C4);
                    if (h & 0x8800) {                                  /* 0018E0F8 */
                        flags = 8;
                        if (cl_lt(y, cl_cw(c, 0x50))) cl_cset(c, 0x50, y); /* 0018E110..24 */
                    } else {
                        flags = 0x10;                                  /* 0018E128 */
                        if (!cl_le(y, cl_cw(c, 0x54))) cl_cset(c, 0x54, y); /* 0018E134..44 */
                    }
                    cl_v_copy(cb + 0x10, S(0x38C0));                   /* 0018E150 */
                    cl_cset(c, 0x10, cl_add(cl_cw(c, 0x10), cl_mul(CL_0_5, *S(0x38A0)))); /* 0018E16C */
                    cl_cset(c, 0x14, cl_add(cl_cw(c, 0x14), cl_mul(CL_0_5, *S(0x38A4))));
                    cl_cset(c, 0x18, cl_add(cl_cw(c, 0x18), cl_mul(CL_0_5, *S(0x38A8))));
                } else {
                    cl_cset(c, 0x10, *S(0x38C0));                      /* 0018E1BC */
                    cl_cset(c, 0x18, *S(0x38C8));
                    cl_cset(c, 0x10, cl_add(cl_cw(c, 0x10), cl_mul(CL_0_5, *S(0x38A0))));
                    cl_cset(c, 0x18, cl_add(cl_cw(c, 0x18), cl_mul(CL_0_5, *S(0x38A8))));
                }
            } else {
                if (h & 0x8800) {                                      /* 0018E208 */
                    flags = 8;
                    VU(cl_v_sub(S(0x38A0), S(0x38A0), S(0x38B0)), 0x001028D0);
                    VU(cl_v_normalize(S(0x38A0), S(0x38A0)), 0x00102760);
                    cl_v_copy(cb + 0x10, S(0x38C0));                   /* 0018E24C */
                    cl_cset(c, 0x14, cl_sub(cl_cw(c, 0x14), CL_ONE));  /* 0018E26C */
                    cl_cset(c, 0x10, cl_add(cl_cw(c, 0x10), cl_mul(CL_0_5, *S(0x38A0))));
                    cl_cset(c, 0x18, cl_add(cl_cw(c, 0x18), cl_mul(CL_0_5, *S(0x38A8))));
                    if (cl_lt(cl_cw(c, 0x14), cl_cw(c, 0x50))) cl_cset(c, 0x50, cl_cw(c, 0x14)); /* 0018E2A8 */
                    if (!cl_le(cl_cw(c, 0x14), cl_cw(c, 0x54))) cl_cset(c, 0x54, cl_cw(c, 0x14)); /* 0018E2C4 */
                    resolved = 1;
                } else if (h & 0x2000) {                               /* 0018E2E8 */
                    SEG(S(0x38A0), S(0x38B0), &r);                     /* 0018E300 */
                    if (r != 0 && (w->hit->record_1A & 0x2000)) {      /* 0018E308..20 */
                        uint32_t *d0 = S(0x38D0);
                        normal_w1(w, d0);                              /* 0018E328.. */
                        VU(cl_v_dot(d0, S(0x38E0), &dot), 0x00102738); /* 0018E364 */
                        *S(0x3A20) = dot;
                        if (!cl_le(dot, CL_M0_3)) {                    /* 0018E37C */
                            CL_CALL(w, 0x0011E620, k->atan2, k->context, d0[0], d0[2], &v);
                            WRAP(v, &v);
                            cl_v_copy3(S(0x38C0), w->hit->point);      /* 0018E3B8 */
                            cl_cset(c, 0x90, v);                       /* 0018E3BC */
                            VU(cl_v_scale(d0, d0, CL_4), 0x00103230);  /* 0018E3D4 */
                            VU(cl_v_add(S(0x38C0), S(0x38C0), d0), 0x001028B8);
                            resolved = 1;                              /* 0018E400 */
                            cl_cset(c, 0x10, *S(0x38C0));
                            cl_cset(c, 0x18, *S(0x38C8));
                        }
                    }
                }
                if (!resolved) {                                       /* 0018E414 */
                    VU(cl_v_sub(S(0x38A0), S(0x38A0), S(0x38B0)), 0x001028D0);
                    VU(cl_v_normalize(S(0x38A0), S(0x38A0)), 0x00102760);
                    cl_cset(c, 0x10, *S(0x38C0));                      /* 0018E45C */
                    cl_cset(c, 0x18, *S(0x38C8));
                    cl_cset(c, 0x10, cl_add(cl_cw(c, 0x10), cl_mul(CL_0_5, *S(0x38A0))));
                    cl_cset(c, 0x18, cl_add(cl_cw(c, 0x18), cl_mul(CL_0_5, *S(0x38A8))));
                }
            }
        }
    }

    /* 0018E4A0: the wall slide runs when the first probe missed or ended
     * dropped, or when its surface was steep. */
    if (steep == 1 || first == 0) {
        if (slide_block(w, mask, first, &flags, &steep, 0) < 0) return -1;
    }
    if (depth_and_first_height(w, first) < 0) return -1;              /* 0018F2B0..F394 */
    if (cl_le(cl_cw(c, 0x14), cl_cw(c, 0x50))) cl_cset(c, 0x14, cl_cw(c, 0x50)); /* 0018F394 */
    if (!cl_lt(cl_cw(c, 0x14), cl_cw(c, 0x54))) cl_cset(c, 0x14, cl_cw(c, 0x54)); /* 0018F3B0 */
    if (floor_ceiling(w, e, mask, 0) < 0) return -1;                   /* 0018F3CC..F7B8 */
    if (*g->area == 0x15 && !cl_le(cl_cw(c, 0x18), CL_260))            /* 0018F7C4..E4 */
        cl_cset(c, 0x50, CL_70);                                       /* 0018F7F0 */
    if (cl_le(cl_cw(c, 0x14), cl_cw(c, 0x50))) {                       /* 0018F7FC */
        cl_cset(c, 0x14, cl_cw(c, 0x50));
        flags |= 0x40;                                                 /* 0018F810 */
    }
    if (!cl_lt(cl_cw(c, 0x14), cl_cw(c, 0x54))) {                      /* 0018F81C */
        cl_cset(c, 0x14, cl_cw(c, 0x54));
        flags |= 0x80;                                                 /* 0018F830 */
    }
    if (result) *result = (int)flags;
    return 0;
}

/* ======================================================================
 * 0018F870(cam, e, style, mask): the aim solver (0018D7B0 styles 2 / 6).
 * ====================================================================== */
int em_camleft_0018F870(EmCamLeftWorld *w, EmPlayerLiveActor *e, int style, int mask, int *result)
{
    if (!cl_world_ok(w) || !e) return cl_fault(w, 0x0018F870);
    const EmCamLeftWorkers *k = w->workers;
    if (!w->globals->follow->area) return cl_fault(w, 0x0018F870);
    CL_NEED(w, 0x0019A910, k->segment);
    CL_NEED(w, 0x001B1470, k->wrap);
    CL_NEED(w, 0x0011E620, k->atan2);
    CL_NEED(w, 0x001B1240, k->heading);
    CL_NEED(w, 0x0011E2A8, k->sine);
    CL_NEED(w, 0x0011DE90, k->cosine);
    EmCameraFollowRecord *c = w->cam;
    uint8_t *cb = c->bytes;
    uint32_t flags = 0;      /* s2 */
    int steep = 0;           /* s3 */
    int first;               /* s0 */
    uint32_t dot, v;
    int r;

    cl_v_copy(S(0x38B0), e->bytes + 0xB0);                             /* 0018F8BC */
    VU(cl_v_sub(S(0x38A0), cb + 0x10, S(0x38B0)), 0x001028D0);         /* 0018F8D4 */
    VU(cl_v_normalize(S(0x38A0), S(0x38A0)), 0x00102760);
    VU(cl_v_scale(S(0x38A0), S(0x38A0), CL_1_5), 0x00103230);
    VU(cl_v_add(S(0x38A0), S(0x38A0), cb + 0x10), 0x001028B8);         /* 0018F91C */
    SEG(S(0x38B0), S(0x38A0), &first);                                 /* 0018F934 */

    if (first != 0) {
        flags = 1;                                                     /* 0018F960 */
        cl_chset(c, 0x58, w->hit->record_1A);                          /* 0018F96C */
        cl_v_copy3(S(0x38C0), w->hit->point);                          /* 0018F968 */
        cl_v_copy(S(0x3950), S(0x38C0));                               /* 0018F97C */
        VU(cl_v_sub(S(0x38F0), cb + 0x20, cb + 0x10), 0x001028D0);     /* 0018F990 */
        *S(0x38F4) = 0;                                                /* 0018F9B0 */
        VU(cl_v_normalize(S(0x38F0), S(0x38F0)), 0x00102760);
        VU(cl_v_sub(S(0x3960), e->bytes + 0xB0, cb + 0x10), 0x001028D0); /* 0018F9C0 */
        *S(0x3964) = 0;                                                /* 0018F9E0 */
        VU(cl_v_normalize(S(0x3960), S(0x3960)), 0x00102760);
        *S(0x38E0) = w->hit->normal[0];                                /* 0018FA08 */
        *S(0x38E8) = w->hit->normal[2];
        *S(0x38E4) = 0;
        *S(0x38EC) = CL_ONE;
        VU(cl_v_dot(S(0x38F0), S(0x38E0), &dot), 0x00102738);          /* 0018FA24 */
        *S(0x3A3C) = dot;
        if (cl_lt(dot, UINT32_C(0x3F7D70A4))) steep = 1;               /* 0018FA3C: 0.99 */
        *S(0x38E4) = w->hit->normal[1];                                /* 0018FA7C */
        VU(cl_v_sub(S(0x38A0), S(0x38A0), S(0x38B0)), 0x001028D0);     /* 0018FA78 */
        VU(cl_v_normalize(S(0x38A0), S(0x38A0)), 0x00102760);
        uint32_t h = cl_ch(c, 0x58);                                   /* 0018FA94 */
        if (h & 0xD800) {
            uint32_t y = *S(0x38C4);
            if (h & 0x8800) {                                          /* 0018FAA4 */
                flags = 8;
                if (cl_lt(y, cl_cw(c, 0x50))) cl_cset(c, 0x50, y);     /* 0018FABC..D0 */
            } else {
                flags = 0x10;                                          /* 0018FAD4 */
                if (!cl_le(y, cl_cw(c, 0x54))) cl_cset(c, 0x54, y);    /* 0018FAE0..F0 */
            }
            cl_v_copy(cb + 0x10, S(0x38C0));                           /* 0018FAFC */
            cl_cset(c, 0x10, cl_add(cl_cw(c, 0x10), cl_mul(CL_0_5, *S(0x38A0)))); /* 0018FB18 */
            cl_cset(c, 0x18, cl_add(cl_cw(c, 0x18), cl_mul(CL_0_5, *S(0x38A8))));
        } else {
            cl_cset(c, 0x10, *S(0x38C0));                              /* 0018FB54 */
            cl_cset(c, 0x18, *S(0x38C8));
            cl_cset(c, 0x10, cl_add(cl_cw(c, 0x10), cl_mul(CL_0_5, *S(0x38A0))));
            cl_cset(c, 0x18, cl_add(cl_cw(c, 0x18), cl_mul(CL_0_5, *S(0x38A8))));
            if (first == 4) {                                          /* 0018FB90 */
                cl_v_copy(S(0x38D0), S(0x38C0));                       /* 0018FBA4 */
                VU(cl_v_sub(S(0x38D0), S(0x38D0), S(0x38A0)), 0x001028D0);
                if (em_camleft_0018CE60(w, S(0x38D0), style) < 0) return -1; /* 0018FBD4 */
            } else {
                if (cl_le(cl_cw(c, 0x14), cl_cw(c, 0x50))) cl_cset(c, 0x14, cl_cw(c, 0x50)); /* 0018FBEC */
                if (!cl_lt(cl_cw(c, 0x14), cl_cw(c, 0x54))) cl_cset(c, 0x14, cl_cw(c, 0x54)); /* 0018FC08 */
            }
        }
    }

    /* 0018FC20: style 6 takes neither pass; the side push (0018FC60) runs
     * after a kept, not steep first probe; the wall slide (0018FF78)
     * after a steep or missed one. */
    int pass = style == 6 ? 0 : steep == 1 ? 1 : first != 0 ? 2 : 1;
    if (pass == 2) {
        CL_CALL(w, 0x0011E620, k->atan2, k->context, *S(0x38E0), *S(0x38E8), &v); /* 0018FC6C */
        uint32_t side;
        WRAP(cl_sub(v, CL_HALF_PI), &side);                            /* 0018FC80 */
        uint32_t *a0 = S(0x38A0), *b0 = S(0x38B0), *p10 = S(0x3910), *f9 = S(0x39F0);
        VU(cl_v_add(f9, cb + 0x10, S(0x38E0)), 0x001028B8);            /* 0018FC9C */
        CL_CALL(w, 0x0011E2A8, k->sine, k->context, side, &v);
        a0[0] = cl_mul(CL_5_5, v);                                     /* 0018FCB8 */
        CL_CALL(w, 0x0011DE90, k->cosine, k->context, side, &v);
        a0[2] = cl_mul(CL_5_5, v);                                     /* 0018FCDC */
        a0[1] = 0;                                                     /* 0018FCFC */
        VU(cl_v_add(b0, f9, a0), 0x001028B8);                          /* 0018FCF8 */
        VU(cl_v_normalize(p10, a0), 0x00102760);                       /* 0018FD0C */
        VU(cl_v_scale(p10, p10, CL_M3), 0x00103230);
        VU(cl_v_add(p10, f9, p10), 0x001028B8);                        /* 0018FD44 */
        SEG(p10, b0, &r);                                              /* 0018FD5C */
        if (r != 0 && (w->hit->record_1A & 0x2000)) {                  /* 0018FD64..7C */
            normal_w1(w, S(0x3900));                                   /* 0018FD84.. */
            VU(cl_v_dot(S(0x3900), S(0x38E0), &dot), 0x00102738);      /* 0018FDC0 */
            *S(0x3A3C) = dot;
            if (cl_lt(dot, CL_0_9)) {                                  /* 0018FDD8 */
                VU(cl_v_sub(S(0x38C0), w->hit->point, a0), 0x001028D0); /* 0018FDFC */
                flags |= 2;
                cl_cset(c, 0x10, *S(0x38C0));                          /* 0018FE10 */
                cl_cset(c, 0x18, *S(0x38C8));
            }
        }
        if (!(flags & 2)) {                                            /* 0018FE24 */
            VU(cl_v_sub(b0, f9, a0), 0x001028D0);                      /* 0018FE40 */
            VU(cl_v_normalize(p10, a0), 0x00102760);
            VU(cl_v_scale(p10, p10, CL_3), 0x00103230);
            VU(cl_v_add(p10, f9, p10), 0x001028B8);                    /* 0018FE8C */
            SEG(p10, b0, &r);                                          /* 0018FEA4 */
            if (r != 0 && (w->hit->record_1A & 0x2000)) {              /* 0018FEAC..C4 */
                normal_w1(w, S(0x3900));
                VU(cl_v_dot(S(0x3900), S(0x38E0), &dot), 0x00102738);  /* 0018FF08 */
                *S(0x3A3C) = dot;
                if (cl_lt(dot, CL_0_9)) {                              /* 0018FF20 */
                    VU(cl_v_add(S(0x38C0), w->hit->point, a0), 0x001028B8); /* 0018FF44 */
                    flags |= 4;
                    cl_cset(c, 0x10, *S(0x38C0));                      /* 0018FF58 */
                    cl_cset(c, 0x18, *S(0x38C8));
                }
            }
        }
    } else if (pass == 1) {
        if (slide_block(w, mask, first, &flags, &steep, 1) < 0) return -1;
    }

    if (depth_and_first_height(w, first) < 0) return -1;              /* 00190A00..00190AE4 */
    if (flags & 0x1F) {                                                /* 00190AE8 */
        cl_v_copy(S(0x38B0), e->bytes + 0xB0);                         /* 00190AF8 */
        cl_v_copy(S(0x38A0), cb + 0x10);                               /* 00190B08 */
        SEG(S(0x38B0), cb + 0x10, &r);                                 /* 00190B1C */
        if (r != 0) {
            cl_v_copy(S(0x38C0), w->hit->point);                       /* 00190B38 */
            cl_cset(c, 0x10, *S(0x38C0));                              /* 00190B48 */
            cl_cset(c, 0x18, *S(0x38C8));
        }
    }
    if (floor_ceiling(w, e, mask, 1) < 0) return -1;                   /* 00190B58..00190EE0 */
    if (result) *result = (int)flags;
    return 0;
}
