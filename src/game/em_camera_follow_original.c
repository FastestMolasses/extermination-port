/* em_camera_follow_original.c - the generic follow camera (see
 * em_camera_follow_original.h, docs/CAMERA_FOLLOW_ORIGINAL.md).
 *
 * Read from the original instructions (build/asm of the decomp), not from
 * the readable decompilation alone: 001921D0 and 00191D40 are NEARMISS C,
 * 00191390 / 0018C6A0 / 0018C4B0 / 00191120 / 00192010 and the SDK vector
 * leaves are asm-word files; 0018D7B0 and 0018D330 follow their
 * byte-matched C. Where the NEARMISS 001921D0 C and its instructions differ
 * in order the instructions win (the C reads the same values).
 * Every address in a comment is the original instruction or label
 * translated there. tools/test_camera_follow_original_reference.py executes
 * the original instructions and compares every record byte, global,
 * scratch word, return value and worker call. */
#include "game/em_camera_follow_original.h"
#include "game/em_ee_float.h"
#include "game/em_sdk_math_original.h"

#include <stddef.h>
#include <string.h>

#define CALL(expr) do { if ((expr) < 0) return -1; } while (0)

/* Float constants as the instructions build them (lui/ori). */
#define F_ZERO     UINT32_C(0x00000000)
#define F_ONE      UINT32_C(0x3F800000)
#define F_M1       UINT32_C(0xBF800000)
#define F_0_5      UINT32_C(0x3F000000)
#define F_0_8      UINT32_C(0x3F4CCCCD)
#define F_1_8      UINT32_C(0x3FE66666)
#define F_2        UINT32_C(0x40000000)
#define F_3        UINT32_C(0x40400000)
#define F_4        UINT32_C(0x40800000)
#define F_5        UINT32_C(0x40A00000)
#define F_6        UINT32_C(0x40C00000)
#define F_7_5      UINT32_C(0x40F00000)
#define F_8        UINT32_C(0x41000000)
#define F_9        UINT32_C(0x41100000)
#define F_10       UINT32_C(0x41200000)
#define F_11       UINT32_C(0x41300000)
#define F_15       UINT32_C(0x41700000)
#define F_17       UINT32_C(0x41880000)
#define F_19       UINT32_C(0x41980000)
#define F_20       UINT32_C(0x41A00000)
#define F_23       UINT32_C(0x41B80000)
#define F_25       UINT32_C(0x41C80000)
#define F_100      UINT32_C(0x42C80000)
#define F_180      UINT32_C(0x43340000)
#define F_200      UINT32_C(0x43480000)
#define F_250      UINT32_C(0x437A0000)
#define F_356      UINT32_C(0x43B20000)
#define F_M3       UINT32_C(0xC0400000)
#define F_M10      UINT32_C(0xC1200000)
#define F_M20      UINT32_C(0xC1A00000)
#define F_M30      UINT32_C(0xC1F00000)
#define F_M40      UINT32_C(0xC2200000)
#define F_M31_2    UINT32_C(0xC1F99999)   /* 00191390: the cam+64 test */
#define F_M46_8    UINT32_C(0xC23B3333)   /* 001921D0: the cam+64 test */
#define F_M1449    UINT32_C(0xC4B52000)
#define F_0_3      UINT32_C(0x3E99999A)
#define F_8_6      UINT32_C(0x4109999A)
#define F_23_3     UINT32_C(0x41BA6666)
#define F_PI       UINT32_C(0x40490FDB)
#define F_QPI      UINT32_C(0x3F490FDB)   /* pi / 4 */
#define F_1DEG     UINT32_C(0x3C8EFA35)   /* 0.017453292 */
#define F_2DEG     UINT32_C(0x3D0EFA35)   /* 0.034906585 */
#define F_3DEG     UINT32_C(0x3D567750)   /* 0.05235988 */
#define F_0_3DEG   UINT32_C(0x3BAB92A7)   /* 0.005235988 */
#define F_0_2DEG   UINT32_C(0x3B64C389)   /* 0.0034906587 */
#define F_RATE     UINT32_C(0x3CB60AE9)   /* 0.022222 */

/* ---- EE COP1 on raw words (em_ee_float.h) -------------------------------- */

static uint32_t add(uint32_t a, uint32_t b) { return em_ee_add_bits(a, b); }
static uint32_t sub(uint32_t a, uint32_t b) { return em_ee_sub_bits(a, b); }
static uint32_t mul(uint32_t a, uint32_t b) { return em_ee_mul_bits(a, b); }
static uint32_t divide(uint32_t a, uint32_t b) { return em_ee_div_bits(a, b); }
static uint32_t neg(uint32_t a) { return em_ee_neg_bits(a); }
static int eq(uint32_t a, uint32_t b) { return em_ee_c_eq_bits(a, b); }
static int lt(uint32_t a, uint32_t b) { return em_ee_c_lt_bits(a, b); }
static int le(uint32_t a, uint32_t b) { return em_ee_c_le_bits(a, b); }

/* 0011DF78 (fabsf), the em_sdk_math_original translation. */
static uint32_t fabs_bits(uint32_t x)
{
    return em_ee_bits(em_sdk_math_original_0011DF78(em_ee_float(x)));
}

/* ---- record access --------------------------------------------------------- */

static uint32_t cw(const EmCameraFollowRecord *c, unsigned at) { return em_camera_follow_word(c, at); }
static void cset(EmCameraFollowRecord *c, unsigned at, uint32_t v) { em_camera_follow_set_word(c, at, v); }
static uint8_t cb(const EmCameraFollowRecord *c, unsigned at) { return c->bytes[at]; }
static void cbset(EmCameraFollowRecord *c, unsigned at, unsigned v) { c->bytes[at] = (uint8_t)v; }
static int16_t ch(const EmCameraFollowRecord *c, unsigned at)
{
    uint16_t v; memcpy(&v, c->bytes + at, 2); return (int16_t)v;
}
static void chset(EmCameraFollowRecord *c, unsigned at, int v)
{
    uint16_t h = (uint16_t)v; memcpy(c->bytes + at, &h, 2);
}
static void cvec(const EmCameraFollowRecord *c, unsigned at, uint32_t out[4])
{
    memcpy(out, c->bytes + at, 16);
}
static void cvec_set(EmCameraFollowRecord *c, unsigned at, const uint32_t v[4])
{
    memcpy(c->bytes + at, v, 16);
}
static uint32_t pw(const EmPlayerLiveActor *p, unsigned at) { return em_live_u32(p, at); }

/* ---- The SDK vector leaves, as their VU0 macro ops --------------------------- */

#define VU(expr) do { if ((expr) != EM_EE_FLOAT_OK) return -1; } while (0)
static const uint32_t VF0[4] = { 0, 0, 0, F_ONE };

/* 001028D0(out, a, b): out = a - b, all four lanes. */
static int v_sub(uint32_t out[4], const uint32_t a[4], const uint32_t b[4])
{
    uint32_t r[4] = { 0, 0, 0, 0 };
    VU(em_vu_vec_bits(EM_VU_SUB, 15, EM_VU_NO_BC, a, b, 0, NULL, r));
    memcpy(out, r, sizeof r);
    return 0;
}

/* 001028B8(out, a, b): out = a + b, all four lanes. */
static int v_add(uint32_t out[4], const uint32_t a[4], const uint32_t b[4])
{
    uint32_t r[4] = { 0, 0, 0, 0 };
    VU(em_vu_vec_bits(EM_VU_ADD, 15, EM_VU_NO_BC, a, b, 0, NULL, r));
    memcpy(out, r, sizeof r);
    return 0;
}

/* 00102760(out, v): t.xyz = v * v; t.x += t.y; t.x += t.z; Q = sqrt(t.x);
 * t.x = 0 + Q; Q = 1.0 / t.x; r = (0, 0, 0, 0); r.xyz = v * Q. */
static int v_normalize(uint32_t out[4], const uint32_t in[4])
{
    uint32_t v[4], t[4], r[4] = { 0, 0, 0, 0 }, q;
    memcpy(v, in, sizeof v);
    memcpy(t, in, sizeof t);
    VU(em_vu_vec_bits(EM_VU_MUL, 14, EM_VU_NO_BC, v, v, 0, NULL, t));
    VU(em_vu_vec_bits(EM_VU_ADDBC, 8, 1, t, t, 0, NULL, t));
    VU(em_vu_vec_bits(EM_VU_ADDBC, 8, 2, t, t, 0, NULL, t));
    q = em_vu_sqrt_bits(t[0]);
    VU(em_vu_vec_bits(EM_VU_ADDQ, 8, EM_VU_NO_BC, VF0, NULL, q, NULL, t));
    VU(em_vu_div_bits(VF0[3], t[0], 3, 0, &q));
    VU(em_vu_vec_bits(EM_VU_SUB, 15, EM_VU_NO_BC, VF0, VF0, 0, NULL, r));
    VU(em_vu_vec_bits(EM_VU_MULQ, 14, EM_VU_NO_BC, v, NULL, q, NULL, r));
    memcpy(out, r, sizeof r);
    return 0;
}

/* 00103230(out, v, s): out.xyz = v.xyz * s; out.w = v.w. */
static int v_scale(uint32_t out[4], const uint32_t in[4], uint32_t s)
{
    uint32_t r[4], b[4] = { s, 0, 0, 0 };
    memcpy(r, in, sizeof r);
    VU(em_vu_vec_bits(EM_VU_MULBC, 14, 0, r, b, 0, NULL, r));
    memcpy(out, r, sizeof r);
    return 0;
}

/* 00102738(a, b): t.xyz = a * b (t.w = b.w); t.x += t.y; t.x += t.z;
 * returns t.x. */
static int v_dot(const uint32_t a[4], const uint32_t b[4], uint32_t *out)
{
    uint32_t t[4];
    memcpy(t, b, sizeof t);
    VU(em_vu_vec_bits(EM_VU_MUL, 14, EM_VU_NO_BC, a, t, 0, NULL, t));
    VU(em_vu_vec_bits(EM_VU_ADDBC, 8, 1, t, t, 0, NULL, t));
    VU(em_vu_vec_bits(EM_VU_ADDBC, 8, 2, t, t, 0, NULL, t));
    *out = t[0];
    return 0;
}

/* 001026A0(out, m, v): ACC = m.row0 * v.x; ACC += m.row1 * v.y; ACC +=
 * m.row2 * v.z; out = ACC + m.row3 * v.w, all four lanes. */
static int v_apply(uint32_t out[4], const uint32_t m[16], const uint32_t v[4])
{
    uint32_t acc[4] = { 0, 0, 0, 0 }, r[4] = { 0, 0, 0, 0 }, x[4];
    memcpy(x, v, sizeof x);
    VU(em_vu_vec_bits(EM_VU_MULABC, 15, 0, m, x, 0, NULL, acc));
    VU(em_vu_vec_bits(EM_VU_MADDABC, 15, 1, m + 4, x, 0, acc, acc));
    VU(em_vu_vec_bits(EM_VU_MADDABC, 15, 2, m + 8, x, 0, acc, acc));
    VU(em_vu_vec_bits(EM_VU_MADDBC, 15, 3, m + 12, x, 0, acc, r));
    memcpy(out, r, sizeof r);
    return 0;
}

/* ---- binding check ---------------------------------------------------------- */

static int workers_bound(const EmCameraFollowWorkers *w)
{
    return w && w->approach && w->wrap && w->heading && w->sine && w->cosine && w->tether &&
           w->solve && w->solve_aim && w->bounds && w->segment && w->ground && w->identity &&
           w->euler;
}

int em_camera_follow_bound(const EmCameraFollowWorld *world)
{
    const EmCameraFollowGlobals *g = world ? world->globals : NULL;
    return world && world->cam && world->player && g && g->eye && g->target && g->d690 &&
           g->d698 && g->d69C && g->area && g->d701 && g->d702 && world->scratch &&
           workers_bound(world->workers);
}

/* ---- 00191390 --------------------------------------------------------------- */

int em_camera_follow_00191390(EmCameraFollowRecord *c, const EmPlayerLiveActor *p)
{
    if (!c || !p) return -1;
    cset(c, 0x94, F_ZERO);                                             /* 00191390 */
    cset(c, 0x98, F_ZERO);                                             /* 00191394 */
    int32_t state = (int32_t)pw(p, 0x230);                             /* 00191398 */
    uint32_t height, param;
    switch (state) {
    case 0x13:                                                         /* 00191474 */
        height = F_11; param = F_2;
        break;
    case 8: case 9: case 7: case 6: case 0x2D: case 0x2C:              /* 00191464 */
        height = F_ZERO; param = F_2;
        break;
    case 0xF: case 4: case 2:                                          /* 00191450/54 */
        height = F_M3; param = F_ONE;
        break;
    default:                                                           /* 3, 1 and every other state: 00191408 */
        if (eq(F_M31_2, cw(c, 0x64))) {                                /* 00191420 */
            height = F_2; param = F_6;
        } else {
            height = F_6; param = F_2;                                 /* 00191440 */
        }
        break;
    }
    cset(c, 0x8C, height);
    cset(c, 0x5C, param);
    if ((int8_t)cb(c, 0x6D) != 0)                                      /* 00191480 */
        cset(c, 0x98, F_23);                                           /* 0019148C */
    return 0;
}

/* ---- 0018C6A0 / 0018C4B0 ---------------------------------------------------- */

/* One axis of 0018C6A0 (x at 0018C6BC.., z at 0018C778..). */
static int chase_axis(uint32_t src, uint32_t *dst, uint32_t max)
{
    uint32_t d = sub(src, *dst);
    uint32_t a = fabs_bits(d);
    if (le(a, F_ONE)) {                                                /* 0018C6F0 */
        *dst = add(*dst, divide(d, F_4));                              /* 0018C760.. */
        return 1;
    }
    uint32_t step = divide(a, F_6);                                    /* 0018C70C */
    if (le(max, step)) step = max;                                     /* 0018C71C */
    if (lt(d, F_ZERO)) step = neg(step);                               /* 0018C738 */
    *dst = add(*dst, step);                                            /* 0018C750 */
    return 0;
}

int em_camera_follow_0018C6A0(const uint32_t src[3], uint32_t dst[3], uint32_t max, int *result)
{
    if (!src || !dst) return -1;
    int bits = 0;
    if (chase_axis(src[0], &dst[0], max)) bits = 1;                    /* 0018C76C */
    if (chase_axis(src[2], &dst[2], max)) bits |= 2;                   /* 0018C818 */
    if (result) *result = bits;
    return 0;
}

int em_camera_follow_0018C4B0(uint32_t v[3], uint32_t y, uint32_t max, int *result)
{
    if (!v) return -1;
    uint32_t d = sub(y, v[1]);                                         /* 0018C4D0 */
    uint32_t a = fabs_bits(d);
    if (le(a, F_ONE)) {                                                /* 0018C4EC */
        v[1] = add(v[1], divide(d, F_4));                              /* 0018C55C.. */
        if (result) *result = 4;
        return 0;
    }
    uint32_t step = divide(a, F_8);                                    /* 0018C508 */
    if (le(max, step)) step = max;                                     /* 0018C518 */
    if (lt(d, F_ZERO)) step = neg(step);                               /* 0018C534 */
    v[1] = add(v[1], step);                                            /* 0018C54C */
    if (result) *result = 0;
    return 0;
}

/* ---- 00191D40 / 00192010 ---------------------------------------------------- */

int em_camera_follow_00191D40(EmCameraFollowRecord *c, const EmCameraFollowGlobals *g,
                              uint32_t y, uint32_t rate)
{
    if (!c || !g || !g->area || !g->d701 || !g->d702) return -1;
    uint32_t goal = add(y, cw(c, 0x98));                               /* 00191D60 */
    uint32_t cap = cw(c, 0x54);
    if (!le(goal, cap)) goal = cap;                                    /* 00191D64 */
    uint32_t d = sub(goal, cw(c, 0x14));                               /* 00191D7C */
    uint32_t a = fabs_bits(d);
    if (!le(d, F_ZERO)) {                                              /* 00191D90 */
        if (!(cb(c, 7) & 0x80)) {                                      /* 00191DA0 */
            if (le(a, F_ONE)) {                                        /* 00191DBC */
                cset(c, 0x14, add(cw(c, 0x14), divide(d, F_5)));       /* 00191E14.. */
            } else {
                uint32_t step = divide(a, F_10);                       /* 00191DD4 */
                if (le(rate, step)) step = rate;                       /* 00191DE4 */
                cset(c, 0x14, add(cw(c, 0x14), step));                 /* 00191E04 */
            }
        }
    } else if (!(cb(c, 7) & 0x40) && !(ch(c, 0x5A) & 1)) {             /* 00191E30..00191E44 */
        if (le(a, F_ONE)) {                                            /* 00191E60 */
            cset(c, 0x14, add(cw(c, 0x14), divide(d, F_5)));           /* 00191EB4.. */
        } else {
            uint32_t step = divide(a, F_10);                           /* 00191E78 */
            if (le(rate, step)) step = rate;                           /* 00191E88 */
            cset(c, 0x14, sub(cw(c, 0x14), step));                     /* 00191EA4 */
        }
    }
    /* The area clamps (00191EC8..). */
    if (*g->area == 0x10) {
        if (*g->d701 == 1 && (*g->d702 == 2 || *g->d702 == 4 || *g->d702 == 6)) { /* 00191EE4.. */
            uint32_t low = cw(c, 0x50);
            if (!le(low, F_100)) {                                     /* 00191F28 */
                uint32_t top = add(F_7_5, low);                        /* 00191F48 */
                if (!le(cw(c, 0x14), top)) cset(c, 0x14, top);         /* 00191F4C..00191F60 */
            }
        }
    } else if (*g->area == 3 && *g->d701 == 1) {                       /* 00191F68.. */
        uint32_t y14 = cw(c, 0x14);
        if (!le(y14, F_250) && lt(cw(c, 0x10), F_356)) {               /* 00191F94, 00191FB4 */
            uint32_t top = sub(cw(c, 0x54), F_2);                      /* 00191FD4 */
            if (!le(y14, top)) cset(c, 0x14, top);                     /* 00191FD8..00191FE8 */
        }
    }
    return 0;
}

int em_camera_follow_00192010(EmCameraFollowRecord *c, uint32_t y, uint32_t up, uint32_t down)
{
    if (!c) return -1;
    uint32_t goal = add(y, cw(c, 0x98));                               /* 00192040 */
    uint32_t d = sub(goal, cw(c, 0x14));                               /* 00192044 */
    uint32_t a = fabs_bits(d);
    if (!le(d, F_ZERO)) {                                              /* 00192058 */
        if (le(a, down)) return 0;                                     /* 00192068 */
        if (cb(c, 7) & 0x80) return 0;                                 /* 00192078 */
        if (le(a, F_ONE)) {                                            /* 00192094 */
            cset(c, 0x14, add(cw(c, 0x14), divide(d, F_5)));           /* 001920F0.. */
            return 0;
        }
        uint32_t step = divide(a, F_10);                               /* 001920AC */
        if (le(F_3, step)) step = F_3;                                 /* 001920C0 */
        cset(c, 0x14, add(cw(c, 0x14), step));                         /* 001920E0 */
        return 0;
    }
    if (le(a, up)) return 0;                                           /* 0019210C */
    if (cb(c, 7) & 0x40) return 0;                                     /* 0019211C */
    if (le(a, F_ONE)) {                                                /* 00192138 */
        cset(c, 0x14, add(cw(c, 0x14), divide(d, F_5)));               /* 00192198.. */
        return 0;
    }
    uint32_t step = divide(a, F_10);                                   /* 00192150 */
    if (le(F_3, step)) step = F_3;                                     /* 00192164 */
    cset(c, 0x14, sub(cw(c, 0x14), step));                             /* 00192188 */
    return 0;
}

/* ---- 00191120 --------------------------------------------------------------- */

int em_camera_follow_00191120(const EmCameraFollowWorkers *w, uint32_t goal, uint32_t current,
                              uint32_t rate, uint32_t limit, uint32_t *out)
{
    if (!w || !w->wrap || !out) return -1;
    uint32_t d;
    CALL(w->wrap(w->context, sub(goal, current), &d));                 /* 00191150 */
    uint32_t a = fabs_bits(d);                                         /* 00191160 */
    if (lt(a, limit))                                                  /* 00191168 */
        return w->wrap(w->context, current, out);                      /* 00191174 */
    if (le(d, F_ZERO)) {                                               /* 0019118C */
        if (le(neg(d), rate)) { *out = goal; return 0; }               /* 001911C0 */
        return w->wrap(w->context, sub(current, rate), out);           /* 001911D4, 001911E4 */
    }
    if (le(d, rate)) { *out = goal; return 0; }                        /* 00191194 */
    return w->wrap(w->context, add(current, rate), out);               /* 001911A4, 001911E4 */
}

/* ---- 0018D330 --------------------------------------------------------------- */

static int segment(const EmCameraFollowWorld *W, const uint32_t from[4], const uint32_t to[4],
                   int mask, EmCameraFollowHit *hit)
{
    const EmCameraFollowWorkers *w = W->workers;
    uint32_t a[4], b[4];
    memcpy(a, from, sizeof a);
    memcpy(b, to, sizeof b);
    memset(hit, 0, sizeof *hit);
    return w->segment(w->context, a, b, mask, hit);
}

int em_camera_follow_0018D330(const EmCameraFollowWorld *W, EmPlayerLiveActor *p, int style,
                              int mask)
{
    if (!em_camera_follow_bound(W) || !p) return -1;
    const EmCameraFollowWorkers *w = W->workers;
    EmCameraFollowRecord *c = W->cam;
    EmCameraFollowScratch *s = W->scratch;
    EmCameraFollowHit hit;
    int ground = 0;
    unsigned st = 0;

    s->s38A0[0] = pw(p, 0xB0);                                         /* 0018D35C */
    s->s38A0[1] = add(F_4, pw(p, 0xB4));                               /* 0018D384 */
    s->s38A0[2] = pw(p, 0xB8);
    s->s38A0[3] = F_ONE;                                               /* 0018D3AC */
    s->s38B0[0] = pw(p, 0xB0);
    s->s38B0[1] = sub(pw(p, 0xA4), F_2);                               /* 0018D3C0 */
    s->s38B0[2] = pw(p, 0xB8);
    s->s38B0[3] = F_ONE;                                               /* 0018D3E0 */
    {
        uint32_t a[4], b[4];
        memcpy(a, s->s38A0, sizeof a);
        memcpy(b, s->s38B0, sizeof b);
        CALL(w->ground(w->context, a, b, &ground));                    /* 0018D3DC */
    }
    cbset(c, 0x6D, ground != 0 ? 1 : 0);                               /* 0018D3F4 / 0018D3FC */

    s->s38A0[0] = pw(p, 0xB0);                                         /* 0018D418 */
    s->s38A0[1] = add(F_200, pw(p, 0xB4));                             /* 0018D42C */
    s->s38A0[2] = pw(p, 0xB8);
    s->s38A0[3] = F_ONE;                                               /* 0018D450 */
    {
        uint32_t from[4] = { pw(p, 0xB0), pw(p, 0xB4), pw(p, 0xB8), pw(p, 0xBC) };
        CALL(segment(W, from, s->s38A0, mask, &hit));                  /* 0018D44C */
    }
    if (hit.result != 0 && (hit.record_1A & 0x8800)) {                 /* 0018D454..0018D46C */
        st = 0x80;                                                     /* 0018D478 */
        cset(c, 0x60, hit.point_y);                                    /* 0018D47C */
    }

    if (style == 2) {                                                  /* 0018D484 */
        s->s38A0[0] = cw(c, 0x10);
        s->s38A0[1] = add(F_11, pw(p, 0xA4));                          /* 0018D4B0 */
        s->s38A0[2] = cw(c, 0x18);
        s->s38A0[3] = F_ONE;
        s->s38B0[0] = pw(p, 0xA0);
        s->s38B0[1] = add(F_11, pw(p, 0xA4));                          /* 0018D4EC */
        s->s38B0[2] = pw(p, 0xA8);
        s->s38B0[3] = F_ONE;
        CALL(v_sub(s->s3910, s->s38A0, s->s38B0));                     /* 0018D504 */
        CALL(v_normalize(s->s3910, s->s3910));                         /* 0018D514 */
        CALL(v_scale(s->s3910, s->s3910, F_9));                        /* 0018D52C */
        CALL(v_add(s->s38A0, s->s3910, s->s38B0));                     /* 0018D544 */
        CALL(segment(W, s->s38B0, s->s38A0, mask, &hit));              /* 0018D558 */
        if (hit.result != 0) {
            if (hit.record_1A & 0x2000) st |= 1;                       /* 0018D57C..0018D58C */
            else if (hit.record_1A & 0x8800) st |= 8;                  /* 0018D5A4 */
        }
        s->s38B0[0] = pw(p, 0xA0);                                     /* 0018D5C8 */
        s->s38B0[1] = add(F_6, pw(p, 0xA4));                           /* 0018D5E4 */
        s->s38B0[2] = pw(p, 0xA8);
        s->s38B0[3] = F_ONE;
        CALL(v_add(s->s38A0, s->s3910, s->s38B0));                     /* 0018D614 */
        s->s38A0[1] = add(F_6, pw(p, 0xA4));                           /* 0018D628, stored at 0018D640 */
        CALL(segment(W, s->s38B0, s->s38A0, mask, &hit));              /* 0018D63C */
        if (hit.result != 0 && (hit.record_1A & 0x2000)) st |= 0x10;   /* 0018D644..0018D65C */
    } else {
        s->s38A0[0] = cw(c, 0x10);                                     /* 0018D660 */
        s->s38A0[1] = add(F_11, pw(p, 0xA4));                          /* 0018D690 */
        s->s38A0[2] = cw(c, 0x18);
        s->s38A0[3] = F_ONE;
        s->s38B0[0] = pw(p, 0xA0);
        s->s38B0[1] = add(F_11, pw(p, 0xA4));                          /* 0018D6CC */
        s->s38B0[2] = pw(p, 0xA8);
        s->s38B0[3] = F_ONE;
        CALL(v_sub(s->s38C0, s->s38A0, s->s38B0));                     /* 0018D6E4 */
        CALL(v_normalize(s->s38C0, s->s38C0));                         /* 0018D6F4 */
        CALL(v_scale(s->s38C0, s->s38C0, F_20));                       /* 0018D70C */
        CALL(v_add(s->s38A0, s->s38C0, s->s38B0));                     /* 0018D724 */
        CALL(segment(W, s->s38B0, s->s38A0, mask, &hit));              /* 0018D738 */
        if (hit.result != 0) {
            if (hit.record_1A & 0x2000) st |= 1;                       /* 0018D75C..0018D76C */
            else if (hit.record_1A & 0x8800) st |= 8;                  /* 0018D778 */
        }
    }
    chset(c, 0x5A, (int)st);                                           /* 0018D784 */
    return 0;
}

/* ---- 0018D7B0 --------------------------------------------------------------- */

static int chase_eye(const EmCameraFollowWorld *W, uint32_t xz_max, uint32_t y_max)
{
    uint32_t src[3] = { cw(W->cam, 0x10), cw(W->cam, 0x14), cw(W->cam, 0x18) };
    CALL(em_camera_follow_0018C6A0(src, W->globals->eye, xz_max, NULL));
    CALL(em_camera_follow_0018C4B0(W->globals->eye, cw(W->cam, 0x14), y_max, NULL));
    return 0;
}

int em_camera_follow_0018D7B0(const EmCameraFollowWorld *W, int style, int *result)
{
    if (!em_camera_follow_bound(W)) return -1;
    const EmCameraFollowWorkers *w = W->workers;
    EmCameraFollowRecord *c = W->cam;
    int mask = style == 2 ? 7 : 6;                                     /* 0018D7C8 */
    int s0 = 0;
    CALL(em_camera_follow_0018D330(W, W->player, style, mask));        /* 0018D7F8 */
    if (style == 2 || style == 6) {
        CALL(w->solve_aim(w->context, c, W->player, style, mask, &s0)); /* 0018F870 */
    } else if (style == 5) {
        CALL(w->bounds(w->context, c, W->player, mask));               /* 0018D910 */
        s0 = 0;
    } else {
        CALL(w->solve(w->context, c, W->player, style, mask, &s0));    /* 0018DD20 */
    }
    cbset(c, 7, (unsigned)s0 & 0xFF);
    if (style == 1) {
        memcpy(W->globals->target, c->bytes + 0x20, 16);               /* 00102948(D_008105E0, cam+20) */
        memcpy(W->globals->eye, c->bytes + 0x10, 16);                  /* 00102948(D_008105D0, cam+10) */
    } else if (style == 0) {
        chase_eye(W, F_4, F_4);                                        /* 0018C6A0 / 0018C4B0 at 4.0 */
    }
    if (result) *result = s0;
    return 0;
}

/* ---- 001921D0 --------------------------------------------------------------- */

static int solve(const EmCameraFollowWorld *W, int style)
{
    return em_camera_follow_0018D7B0(W, style, NULL);
}

static int approach(const EmCameraFollowWorld *W, uint32_t target, uint32_t current, uint32_t rate,
                    uint32_t *out)
{
    const EmCameraFollowWorkers *w = W->workers;
    return w->approach(w->context, target, current, rate, out);
}

static int wrap(const EmCameraFollowWorld *W, uint32_t x, uint32_t *out)
{
    return W->workers->wrap(W->workers->context, x, out);
}

static int sine(const EmCameraFollowWorld *W, uint32_t x, uint32_t *out)
{
    return W->workers->sine(W->workers->context, x, out);
}

static int cosine(const EmCameraFollowWorld *W, uint32_t x, uint32_t *out)
{
    return W->workers->cosine(W->workers->context, x, out);
}

static int heading(const EmCameraFollowWorld *W, const uint32_t obj[3], uint32_t x, uint32_t z,
                   uint32_t *out)
{
    uint32_t copy[3] = { obj[0], obj[1], obj[2] };
    return W->workers->heading(W->workers->context, copy, x, z, out);
}

/* The eye from the yaw at the boom cam+C (cases 6, 7, 8/0x2C/0x2D):
 * cam+10 = E0 + C * sin(yaw); cam+18 = E8 + C * cos(cam+44). `yaw` is the
 * register the sine receives (always the word just stored at cam+44). */
static int eye_boom(const EmCameraFollowWorld *W, uint32_t yaw)
{
    EmCameraFollowRecord *c = W->cam;
    uint32_t t;
    CALL(sine(W, yaw, &t));
    cset(c, 0x10, add(W->globals->target[0], mul(cw(c, 0xC), t)));
    CALL(cosine(W, cw(c, 0x44), &t));
    cset(c, 0x18, add(W->globals->target[2], mul(cw(c, 0xC), t)));
    return 0;
}

/* The eye at the boom cam+C + cam+94 (cases 0x14, 0x15, 0xA/0x19). */
static int eye_boom94(const EmCameraFollowWorld *W, uint32_t yaw)
{
    EmCameraFollowRecord *c = W->cam;
    uint32_t t;
    CALL(sine(W, yaw, &t));
    cset(c, 0x10, add(W->globals->target[0], mul(add(cw(c, 0xC), cw(c, 0x94)), t)));
    CALL(cosine(W, cw(c, 0x44), &t));
    cset(c, 0x18, add(W->globals->target[2], mul(add(cw(c, 0xC), cw(c, 0x94)), t)));
    return 0;
}

/* The common close of the posing cases: 0018D7B0(style), then the actual
 * eye chased at 0.8 (x/z) and, when `with_y`, at 0.8 (y). */
static int pose_close(const EmCameraFollowWorld *W, int style, int with_y)
{
    CALL(solve(W, style));
    uint32_t src[3] = { cw(W->cam, 0x10), cw(W->cam, 0x14), cw(W->cam, 0x18) };
    CALL(em_camera_follow_0018C6A0(src, W->globals->eye, F_0_8, NULL));
    if (with_y) CALL(em_camera_follow_0018C4B0(W->globals->eye, cw(W->cam, 0x14), F_0_8, NULL));
    return 0;
}

/* The idle tail from 00192DDC (freelook 0) up to the 0018D7B0(cam, 0) call. */
static int follow_tail(const EmCameraFollowWorld *W, EmPlayerLiveActor *p, uint32_t sign)
{
    EmCameraFollowRecord *c = W->cam;
    EmCameraFollowGlobals *g = W->globals;
    EmCameraFollowScratch *s = W->scratch;

    uint32_t slack = sub(*g->d690, fabs_bits(cw(c, 0xC)));             /* 00192DE8..00192DFC */
    s->s3A20[0] = slack;                                               /* 00192E08 */
    if (!le(slack, F_ZERO)) {                                          /* 00192E04 */
        /* The pull-in along the eye-to-target direction. */
        uint32_t tgt[4], eye[4];
        cvec(c, 0x20, tgt);
        cvec(c, 0x10, eye);
        CALL(v_sub(s->s38A0, tgt, eye));                               /* 00192E1C */
        s->s38A0[1] = F_ZERO;                                          /* 00192E30 */
        s->s38A0[3] = F_ZERO;                                          /* 00192E3C */
        CALL(v_normalize(s->s38A0, s->s38A0));                         /* 00192E38 */
        cset(c, 0x10, add(cw(c, 0x10), mul(s->s38A0[0], s->s3A20[0]))); /* 00192E64..00192E70 */
        cset(c, 0x18, add(cw(c, 0x18), mul(s->s38A0[2], s->s3A20[0]))); /* 00192E88..00192E94 */
        CALL(em_camera_follow_00191D40(c, g, add(cw(c, 0x8C), add(F_11, add(cw(c, 0x5C), pw(p, 0xA4)))),
                                       F_4));                          /* 00192EB4 */
        cbset(c, 3, 0);                                                /* 00192EBC */
        return 0;
    }

    uint32_t lim = eq(F_M46_8, cw(c, 0x64)) ? F_M20 : F_M10;           /* 00192EC4..00192EFC */
    if (lt(slack, lim)) {                                              /* 00192F00 */
        /* The orbit push-out (00192F10..001932FC). */
        uint32_t t;
        CALL(sine(W, s->s3B50[1], &t));                                /* 00192F10 */
        s->s38B0[0] = t;
        CALL(cosine(W, s->s3B50[1], &t));                              /* 00192F28 */
        s->s38B0[2] = t;
        s->s38B0[1] = F_ZERO;                                          /* 00192F38 */
        s->s38B0[3] = F_ONE;                                           /* 00192F44 */
        CALL(v_normalize(s->s38B0, s->s38B0));                         /* 00192F54 */
        uint32_t tgt[4], eye[4];
        cvec(c, 0x20, tgt);
        cvec(c, 0x10, eye);
        CALL(v_sub(s->s38A0, tgt, eye));                               /* 00192F68 */
        s->s38A0[1] = F_ZERO;                                          /* 00192F74 */
        s->s38A0[3] = F_ONE;                                           /* 00192F80 */
        CALL(v_normalize(s->s38A0, s->s38A0));                         /* 00192F90 */
        CALL(v_dot(s->s38A0, s->s38B0, &t));                           /* 00192FA8 */
        s->s3A20[2] = t;                                               /* 00192FB4 */
        s->s3A20[1] = sub(s->s3A20[0], lim);                           /* 00192FD0, stored 00192FDC */
        if (le(*g->d690, F_ONE)) {                                     /* 00192FD4 */
            chset(c, 0x5A, ch(c, 0x5A) & 0xFE);                        /* 0019309C */
            cbset(c, 7, cb(c, 7) & 0xFE);                              /* 001930A8 */
        } else if (eq(F_ONE, sign)) {                                  /* 00192FE0 */
            if ((cb(c, 7) & 0x1F) || (ch(c, 0x5A) & 1))                /* 00192FF0, 00193008 */
                sign = F_M1;                                           /* 00193018 */
            else if (!lt(*g->d698, F_23_3))                            /* 00193024..0019303C */
                sign = F_M1;                                           /* 0019304C */
        } else if (!lt(s->s3A20[2], F_ZERO) && (cb(c, 7) & 0x1F)) {    /* 0019305C..00193080 */
            sign = F_M1;                                               /* 0019308C */
        }
        if (!eq(F_M1, sign)) {                                         /* 001930B8 */
            if (!le(*g->d69C, F_8_6) || !le(*g->d698, F_23_3)) {       /* 001930E0, 00193110 */
                if (!((ch(c, 0x5A) | cb(c, 7)) & 1)) {                 /* 00193120..00193130 */
                    cset(c, 0x10, add(cw(c, 0x10), mul(s->s38A0[0], s->s3A20[1]))); /* 00193140.. */
                    cset(c, 0x18, add(cw(c, 0x18), mul(s->s38A0[2], s->s3A20[1]))); /* 00193160.. */
                }
            } else {
                if (cb(c, 3) == 0) {                                   /* 0019317C */
                    uint32_t eye3[3] = { cw(c, 0x10), cw(c, 0x14), cw(c, 0x18) };
                    CALL(heading(W, eye3, cw(c, 0x20), cw(c, 0x28), &t)); /* 00193190 */
                    s->s3A20[2] = t;                                   /* 0019319C */
                    s->s3A20[3] = sub(cw(c, 0x90), s->s3A20[2]);       /* 001931B4, stored 001931C4 */
                    cbset(c, 3, le(s->s3A20[3], F_ZERO) ? 2 : 1);      /* 001931BC..001931D8 */
                }
                uint32_t step = divide(mul(F_PI, mul(F_0_3, s->s3A20[1])), F_180); /* 00193204.. */
                if (cb(c, 3) == 1)                                     /* 001931E4 */
                    s->s3A20[2] = add(s->s3A20[2], step);              /* 0019322C */
                else
                    s->s3A20[2] = sub(s->s3A20[2], step);              /* 00193278 */
                CALL(sine(W, s->s3A20[2], &t));                        /* 0019328C */
                s->s38A0[0] = t;
                s->s38A0[1] = F_ZERO;                                  /* 0019329C */
                CALL(cosine(W, s->s3A20[2], &t));                      /* 001932A4 */
                s->s38A0[2] = t;
                s->s38A0[3] = F_ZERO;                                  /* 001932B4 */
                cset(c, 0x10, add(cw(c, 0x10), mul(s->s38A0[0], s->s3A20[1]))); /* 001932C8.. */
                cset(c, 0x18, add(cw(c, 0x18), mul(s->s38A0[2], s->s3A20[1]))); /* 001932E8.. */
            }
        }
    }

    /* 00193300: the height under the remaining slack. */
    slack = sub(*g->d690, fabs_bits(cw(c, 0xC)));                      /* 00193300..00193314 */
    s->s3A20[0] = slack;                                               /* 00193324 */
    uint32_t height;
    if (lt(slack, lim)) {                                              /* 00193318 */
        if (eq(F_M20, lim)) {                                          /* 00193334 */
            s->s3A20[1] = mul(F_0_5, sub(slack, lim));                 /* 00193348..0019335C */
        } else {
            uint32_t d = sub(slack, lim);                              /* 00193344 (bc1fl delay slot) */
            s->s3A20[1] = d;                                           /* 00193384 */
            if (lt(d, F_M10)) s->s3A20[1] = F_M10;                     /* 00193378, 0019338C */
        }
        height = add(F_11, add(cw(c, 0x8C), add(pw(p, 0xA4), sub(cw(c, 0x5C), s->s3A20[1])))); /* 001933A8.. */
    } else {
        height = add(F_11, add(cw(c, 0x8C), add(cw(c, 0x5C), pw(p, 0xA4)))); /* 001933D4.. */
    }
    CALL(em_camera_follow_00191D40(c, g, height, F_4));                /* 001933E8 */
    if (*g->area == 0 && (*g->d702 == 5 || *g->d702 == 6)) {           /* 001933F0..00193414 */
        if (lt(cw(c, 0x18), F_M1449)) cset(c, 0x18, F_M1449);          /* 00193430, 00193444 */
    }
    return 0;
}

/* 00193448: the idle re-orbit timer. Every path ends at the 0018D7B0(cam, 0)
 * call (0019361C directly while the counter is below 0x1E1, else through
 * 00193618 after the counter reset). */
static int idle_timer(const EmCameraFollowWorld *W, const EmPlayerLiveActor *p)
{
    EmCameraFollowRecord *c = W->cam;
    if (cb(c, 7) & 9) {                                                /* 00193450 */
        chset(c, 8, 0);                                                /* 001935E4 */
        return 0;
    }
    int32_t state = (int32_t)pw(p, 0x230);
    if (state != 1 && state != 2) {                                    /* 00193464, 0019346C */
        chset(c, 8, 0);                                                /* 001935DC */
        return 0;
    }
    chset(c, 8, ch(c, 8) + 1);                                         /* 00193474..0019347C */
    if (ch(c, 8) < 0x1E1) return 0;                                    /* 00193484 */
    uint32_t d;
    CALL(wrap(W, sub(pw(p, 0xC4), cw(c, 0x44)), &d));                  /* 00193494 */
    if (!le(fabs_bits(d), F_3DEG)) {                                   /* 001934B4 */
        int clockwise = lt(d, F_ZERO);                                 /* 001934C8 */
        if (!(cb(c, 7) & (clockwise ? 4 : 2))) {                       /* 001934D4 / 0019355C */
            cset(c, 0x48, pw(p, 0xC4));                                /* 001934EC / 00193574 */
            cset(c, 0x4C, fabs_bits(*W->globals->d69C));               /* 00193504 / 0019358C */
            cbset(c, 1, 2);                                            /* 00193508 / 00193590 */
            cbset(c, 3, clockwise ? 0 : 1);                            /* 00193510 / 00193598 */
            uint32_t rate = mul(F_RATE, fabs_bits(d));                 /* 00193528 / 001935B0 */
            cset(c, 0x40, rate);                                       /* 00193544 / 001935CC */
            if (lt(rate, F_0_2DEG)) cset(c, 0x40, F_0_2DEG);           /* 0019353C, 00193550 / 001935D0 */
        }
    }
    chset(c, 8, 0);                                                    /* 001935D8 */
    return 0;
}

int em_camera_follow_001921D0(const EmCameraFollowWorld *W, EmPlayerLiveActor *p, int freelook)
{
    if (!em_camera_follow_bound(W) || !p) return -1;
    EmCameraFollowRecord *c = W->cam;
    EmCameraFollowGlobals *g = W->globals;
    uint32_t sign = F_ZERO;                                            /* 001921F0 */
    uint32_t t, u;
    int32_t state = (int32_t)pw(p, 0x230);                             /* 001921E8 */

    switch (state) {
    case 2: case 4: case 0xF:                                          /* 001922E0 / 001922E4 */
        return W->workers->tether(W->workers->context, c, p);          /* 00230000 */
    case 6:                                                            /* 001922F4 */
        CALL(approach(W, pw(p, 0xC4), cw(c, 0x44), F_1DEG, &t));
        cset(c, 0x44, t);
        cbset(c, 0x6C, 0);                                             /* 0019231C */
        CALL(eye_boom(W, cw(c, 0x44)));
        return pose_close(W, 3, 1);                                    /* 00192358.. */
    case 7:                                                            /* 001923A0 */
        if (*g->area == 0x13 && *g->d701 == 1) return 0;
        CALL(approach(W, add(F_PI, pw(p, 0xC4)), cw(c, 0x44), F_2DEG, &t)); /* 001923E4 */
        cset(c, 0x44, t);
        cbset(c, 0x6C, 0);                                             /* 001923F0 */
        CALL(eye_boom(W, cw(c, 0x44)));
        return pose_close(W, 3, 1);                                    /* 00192434.. */
    case 0x18:                                                         /* 00192480 */
        return pose_close(W, 4, 0);
    case 9:                                                            /* 001924AC */
        CALL(em_camera_follow_00191D40(c, g, add(cw(c, 0x8C), add(cw(c, 0x5C), pw(p, 0xB4))), F_ONE));
        return pose_close(W, 3, 1);                                    /* 001924D0.. */
    case 0x14:                                                         /* 00192518 */
        CALL(wrap(W, add(F_PI, pw(p, 0x218)), &u));                    /* 00192530 */
        CALL(approach(W, u, cw(c, 0x44), F_1DEG, &t));                 /* 00192548 */
        cset(c, 0x44, t);
        CALL(eye_boom94(W, t));                                        /* 00192554.. */
        CALL(em_camera_follow_00191D40(c, g, add(F_15, add(cw(c, 0x8C), add(F_11, add(cw(c, 0x5C),
                                       pw(p, 0xA4))))), F_ONE));       /* 001925D0 */
        return pose_close(W, 4, 1);                                    /* 001925DC.. */
    case 0x15:                                                         /* 00192624 */
        if (eq(pw(p, 0x38), sign)) {                                   /* 00192628 */
            CALL(em_camera_follow_00191120(W->workers, add(F_PI, pw(p, 0xC4)), cw(c, 0x44),
                                           F_2DEG, F_QPI, &t));        /* 00192754 */
            if (!eq(t, cw(c, 0x44))) {                                 /* 00192760 */
                cset(c, 0x44, t);
                CALL(eye_boom94(W, t));
            }
        } else {
            if (!lt(pw(p, 0x38), sign))                                /* 00192638 */
                CALL(wrap(W, sub(add(F_PI, pw(p, 0xC4)), F_QPI), &u)); /* 00192668 */
            else
                CALL(wrap(W, add(F_QPI, add(F_PI, pw(p, 0xC4))), &u)); /* 001926B0 */
            CALL(approach(W, u, cw(c, 0x44), F_0_3DEG, &t));
            cset(c, 0x44, t);
            CALL(eye_boom94(W, cw(c, 0x44)));                          /* 001926D4 */
        }
        CALL(em_camera_follow_00191D40(c, g, add(F_15, add(cw(c, 0x8C), add(F_11, add(cw(c, 0x5C),
                                       pw(p, 0xA4))))), F_ONE));       /* 001927F8 */
        return pose_close(W, 4, 1);                                    /* 00192804.. */
    case 0xA: case 0x19:                                               /* 0019284C */
        if (eq(pw(p, 0x38), F_ZERO)) {                                 /* 00192854 */
            CALL(em_camera_follow_00191120(W->workers, pw(p, 0xC4), cw(c, 0x44), F_2DEG,
                                           F_QPI, &t));                /* 00192954 */
            if (!eq(t, cw(c, 0x44))) {                                 /* 00192960 */
                cset(c, 0x44, t);
                CALL(eye_boom94(W, t));
            }
        } else {
            if (!lt(pw(p, 0x38), F_ZERO))                              /* 00192864 */
                CALL(wrap(W, add(F_QPI, pw(p, 0xC4)), &u));            /* 00192888 */
            else
                CALL(wrap(W, sub(pw(p, 0xC4), F_QPI), &u));            /* 001928C8 */
            CALL(approach(W, u, cw(c, 0x44), F_0_3DEG, &t));
            cset(c, 0x44, t);
            CALL(eye_boom94(W, cw(c, 0x44)));                          /* 001928E4 */
        }
        if (*g->area != 4)                                             /* 001929D0 */
            CALL(em_camera_follow_00192010(c, add(cw(c, 0x8C), add(cw(c, 0x5C), pw(p, 0xB4))),
                                           F_15, F_10));               /* 001929FC */
        return pose_close(W, 3, 1);                                    /* 00192A08.. */
    case 8:                                                            /* 00192A50 */
        if (*g->area == 0x13 && *g->d701 == 1) return 0;
        /* fall through */
    case 0x2C: case 0x2D:                                              /* 00192A70 */
        if ((int8_t)cb(c, 0x6C) == 0) {
            CALL(approach(W, pw(p, 0xC4), cw(c, 0x44), F_2DEG, &t));   /* 00192A8C */
            cset(c, 0x44, t);
            if (eq(t, pw(p, 0xC4))) cbset(c, 0x6C, 1);                 /* 00192A9C..00192AB0 */
            CALL(eye_boom(W, cw(c, 0x44)));                            /* 00192AB4 */
            return pose_close(W, 3, 1);                                /* 00192AF8.. */
        }
        CALL(approach(W, pw(p, 0xC4), cw(c, 0x44), F_0_2DEG, &t));     /* 00192B54 */
        cset(c, 0x44, t);
        CALL(eye_boom(W, t));                                          /* 00192B5C.. */
        CALL(em_camera_follow_00192010(c, add(cw(c, 0x8C), add(cw(c, 0x5C), pw(p, 0xB4))), F_25,
                                       F_20));                         /* 00192BC0 */
        return pose_close(W, 3, 1);                                    /* 00192BCC.. */
    case 0x13:                                                         /* 00192C14 */
        CALL(approach(W, pw(p, 0xC4), cw(c, 0x44), F_1DEG, &t));
        cset(c, 0x44, t);
        CALL(sine(W, t, &u));                                          /* 00192C38 */
        cset(c, 0x10, add(g->target[0], mul(F_M40, u)));
        CALL(cosine(W, cw(c, 0x44), &u));                              /* 00192C60 */
        cset(c, 0x18, add(g->target[2], mul(F_M40, u)));
        CALL(em_camera_follow_00192010(c, add(F_17, add(cw(c, 0x8C), add(cw(c, 0x5C), pw(p, 0xA4)))),
                                       F_25, sign));                   /* 00192CA4 */
        CALL(solve(W, 4));                                             /* 00192CB0 */
        {
            uint32_t src[3] = { cw(c, 0x10), cw(c, 0x14), cw(c, 0x18) };
            CALL(em_camera_follow_0018C6A0(src, g->eye, F_1_8, NULL)); /* 00192CD0 */
            CALL(em_camera_follow_0018C4B0(g->eye, cw(c, 0x14), F_ONE, NULL)); /* 00192CE8 */
        }
        return 0;
    case 0x2F: {                                                       /* 00192CF4 */
        EmCameraFollowScratch *s = W->scratch;
        const EmCameraFollowWorkers *w = W->workers;
        cvec_set(c, 0x30, s->s3B50);                                   /* 00102948(cam+30, 0x70003B50) */
        CALL(w->identity(w->context, s->s3400));                       /* 00192D08 */
        {
            uint32_t in[16], angles[4];
            memcpy(in, s->s3400, sizeof in);
            cvec(c, 0x30, angles);
            CALL(w->euler(w->context, s->s3400, in, angles));          /* 00102C58(0x70003400, 0x70003400, cam+30) */
        }
        s->s3600[0] = F_ZERO; s->s3600[1] = F_15; s->s3600[2] = F_6; s->s3600[3] = F_ONE; /* 00192D2C.. */
        s->s3400[12] = pw(p, 0xA0);                                    /* 001031E0(0x70003430, p+A0) */
        s->s3400[13] = pw(p, 0xA4);
        s->s3400[14] = pw(p, 0xA8);
        {
            uint32_t r[4];
            CALL(v_apply(r, s->s3400, s->s3600));                      /* 00192D78 */
            cvec_set(c, 0x20, r);
            s->s3600[0] = F_ZERO; s->s3600[1] = F_19; s->s3600[2] = F_M30; s->s3600[3] = F_ONE; /* 00192D80.. */
            CALL(v_apply(r, s->s3400, s->s3600));                      /* 00192DB8 */
            cvec_set(c, 0x10, r);
        }
        return solve(W, 1);                                            /* 00192DC4 */
    }
    case 1: case 0x26: case 0x27:                                      /* 00192DD4 / 00192DD8 */
        sign = F_ONE;
        break;
    default:                                                           /* 00192DDC */
        break;
    }

    if (freelook != 0) {                                               /* 00192DDC -> 001935EC */
        cbset(c, 1, 0);
        cbset(c, 3, 0);
        CALL(heading(W, g->eye, g->target[0], g->target[2], &t));      /* 00193608 */
        cset(c, 0x44, t);
        return 0;
    }
    CALL(follow_tail(W, p, sign));
    CALL(idle_timer(W, p));
    CALL(solve(W, 0));                                                 /* 0019361C */
    CALL(heading(W, g->eye, g->target[0], g->target[2], &t));          /* 00193638 */
    cset(c, 0x44, t);
    return 0;
}
