/* em_camera_leftovers.c - the camera frame, the action dispatch, the
 * walking target placement, the orient-behind request and the locomotion
 * tether (see em_camera_leftovers.h, docs/CAMERA_LEFTOVERS.md).
 *
 * Read from the original instructions (build/asm of the decomp). 0018B9C0,
 * 0018BC20, 001916C0, 0022FCA0 and 0018C5A0 are NEARMISS C in the decomp,
 * so their readable C was only a guide; 00190F20, 0018C0C0, 001914A0,
 * 00191580, 00191000, 00230000 and 0015CBA0 are byte-matched C (the float
 * expression order below is the one their instructions evaluate). 00194D10
 * is NEARMISS C. Every address in a comment is the original instruction or
 * label translated there. tools/test_camera_leftovers_reference.py executes
 * the original instructions and compares every written byte, return value
 * and worker call. */
#include "game/em_camera_leftovers_internal.h"

#include <stddef.h>
#include <string.h>

/* The follow module's leaves take raw word vectors; the records keep their
 * bytes, so each call works on a copy and writes the words back. */
static void cl_words(uint32_t *out, const void *in, unsigned n) { memcpy(out, in, 4u * n); }
static void cl_put_words(void *out, const uint32_t *in, unsigned n) { memcpy(out, in, 4u * n); }

/* 0018C6A0(src, dst, max) over record bytes. */
static int cl_chase_xz(const void *src, void *dst, uint32_t max)
{
    uint32_t s[3], d[3];
    cl_words(s, src, 3);
    cl_words(d, dst, 3);
    if (em_camera_follow_0018C6A0(s, d, max, NULL) < 0) return -1;
    cl_put_words(dst, d, 3);
    return 0;
}

/* 0018C4B0(v, y, max) over record bytes. */
static int cl_chase_y(void *v, uint32_t y, uint32_t max)
{
    uint32_t d[3];
    cl_words(d, v, 3);
    if (em_camera_follow_0018C4B0(d, y, max, NULL) < 0) return -1;
    cl_put_words(v, d, 3);
    return 0;
}

/* ======================================================================
 * 0018C0C0(cam): D_008105E0 = cam+20 (00102948, 16 bytes).
 * ====================================================================== */
int em_camleft_0018C0C0(EmCamLeftWorld *w)
{
    if (!cl_world_ok(w) || !w->globals->follow->target) return cl_fault(w, 0x0018C0C0);
    cl_v_copy(w->globals->follow->target, w->cam->bytes + 0x20);
    return 0;
}

/* ======================================================================
 * 001916C0(cam, e, mode)
 * ====================================================================== */

/* The groups of the jump table at 0x26D990 (48 entries on e+230). */
enum { G_A = 1, G_B, G_C, G_D, G_E, G_F, G_DEFAULT };

static int placement_group(uint32_t state, int *flag)
{
    *flag = 0;
    if (state >= 0x30) return G_DEFAULT;                               /* 001916F0 */
    switch (state) {
    case 0: case 1:                           *flag = 1; return G_A;   /* 00191714 */
    case 3: case 0xE: case 0x14: case 0x15: case 0x16: return G_A;     /* 00191718 */
    case 0xA: case 0x19:                      return G_B;              /* 001918CC */
    case 0x13:                                return G_C;              /* 00191964 */
    case 2:                                   *flag = 1; return G_D;   /* 001919E4 */
    case 4: case 0xF:                         return G_D;              /* 001919E8 */
    case 8: case 0xC: case 0xD: case 0x26: case 0x27: case 0x29: case 0x2A: case 0x2F:
        return G_E;                                                    /* 00191A84 */
    case 0x1D: case 0x1E: case 0x1F: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
        return G_F;                                                    /* 00191B04 */
    default:                                  return G_DEFAULT;        /* 00191B9C */
    }
}

int em_camleft_001916C0(EmCamLeftWorld *w, EmPlayerLiveActor *e, int mode)
{
    if (!cl_world_ok(w) || !e) return cl_fault(w, 0x001916C0);
    const EmCameraFollowGlobals *g = w->globals->follow;
    EmCameraFollowRecord *c = w->cam;
    EmCamLeftScratch *s = w->scratch;
    int flag;
    int group = placement_group(cl_pw(e, 0x230), &flag);               /* 001916DC */
    if (group == G_A && mode == 2) CL_NEED(w, 0x0011E748, w->workers->sqrt);
    if (group == G_A && mode != 2 && !g->d690) return cl_fault(w, 0x001916C0);
    if (mode != 2 && !g->target) return cl_fault(w, 0x001916C0);

    /* The source point and its chase (every group). */
    unsigned src = group == G_DEFAULT ? 0xB0 : 0xA0;
    if (mode == 2) {
        cl_cset(c, 0x20, cl_pw(e, src));                               /* 00191724 */
        cl_cset(c, 0x28, cl_pw(e, src + 8));
    } else if (cl_chase_xz(e->bytes + src, c->bytes + 0x20,
                           group == G_DEFAULT ? CL_1_5 : CL_2) < 0) {  /* 00191744 */
        return cl_fault(w, 0x0018C6A0);
    }

    /* e+A4 / e+B4 / cam+8C are read where the instructions read them, after
     * the group's calls. */
#define A4 cl_pw(e, 0xA4)
#define B4 cl_pw(e, 0xB4)
#define H8C cl_cw(c, 0x8C)
    uint32_t y = 0;
    int have_y = 1;
    switch (group) {
    case G_A: {
        uint32_t *a20 = em_camleft_spad(s, 0x70003A20);
        if (mode == 2) {
            uint32_t *b0 = em_camleft_spad(s, 0x700038B0);
            if (cl_v_sub(b0, c->bytes + 0x20, c->bytes + 0x10) < 0)    /* 00191764 */
                return cl_fault(w, 0x001028D0);
            uint32_t sq = cl_madd(cl_mul(b0[0], b0[0]), b0[2], b0[2]); /* 00191770: mula, madd */
            uint32_t root;
            CL_CALL(w, 0x0011E748, w->workers->sqrt, w->workers->context, sq, &root);
            *a20 = root;                                               /* 00191790 */
        } else {
            *a20 = cl_sub(*g->d690, cl_fabs(cl_cw(c, 0x0C)));           /* 00191794..AC */
        }
        uint32_t lim = cl_eq(CL_M46_8, cl_cw(c, 0x64)) ? CL_M20 : CL_M10; /* 001917B0 */
        uint32_t t = *a20;
        if (cl_lt(t, lim)) {                                           /* 001917F4 */
            uint32_t u = cl_add(lim, cl_sub(lim, t));                  /* 00191804..10 */
            *a20 = u;
            if (!cl_le(u, CL_M7)) *a20 = CL_M7;                        /* 0019181C..30 */
        }
        /* ACC = A4 + 8C; y = 11 + (ACC + 0.3 * A20) (adda, madd). */
        y = cl_add(CL_11, cl_madd(cl_add(A4, H8C), CL_0_3, *a20));     /* 00191858..78 */
        break;
    }
    case G_B:
    case G_F:
        y = cl_add(cl_add(CL_11, A4), H8C);                            /* 00191924 / 00191B1C */
        break;
    case G_C:
        y = cl_add(A4, H8C);                                           /* 001919B8 */
        break;
    case G_D:
        y = cl_add(CL_11, cl_add(A4, H8C));                            /* 00191A48 */
        break;
    case G_E:
    case G_DEFAULT:
        y = cl_add(B4, H8C);                                           /* 00191AD8 / 00191BF8 */
        break;
    default:
        have_y = 0;
        break;
    }
#undef A4
#undef B4
#undef H8C
    if (have_y) {
        if (mode == 0) {
            if (cl_chase_y(c->bytes + 0x20, y, CL_4) < 0) return cl_fault(w, 0x0018C4B0);
        } else if (mode == 2) {
            cl_cset(c, 0x24, y);                                       /* e.g. 001918C8 */
        }
    }

    /* The tail (00191C18). */
    int16_t n = (int16_t)cl_ch(c, 0xA0);
    if (n != 0) cl_chset(c, 0xA0, (unsigned)(uint16_t)(n - 1));        /* 00191C24 */
    if (mode == 2) return 0;                                           /* 00191C30 */
    if (cl_ch(c, 0xA0) == 0)                                           /* 00191C38 */
        return em_camleft_0018C0C0(w);                                 /* 00191C40 */
    if (cl_chase_xz(c->bytes + 0x20, g->target, CL_ONE) < 0)           /* 00191C60 */
        return cl_fault(w, 0x0018C6A0);
    uint32_t d = cl_sub(cl_cw(c, 0x24), g->target[1]);                 /* 00191C74 */
    if (cl_le(cl_fabs(d), CL_0_15)) cl_chset(c, 0xA0, 0);              /* 00191C80..A4 */
    uint32_t rate = cl_div(cl_fabs(d), flag ? CL_20 : CL_4);           /* 00191CB8 / 00191CF8 */
    if (cl_chase_y(g->target, cl_cw(c, 0x24), rate) < 0) return cl_fault(w, 0x0018C4B0);
    return 0;
}

/* ======================================================================
 * 00193D90(cam, e): the idle auto orbit (00195130's camera state 2, armed
 * by 001921D0's idle timer).
 * ====================================================================== */
int em_camleft_00193D90(EmCamLeftWorld *w, EmPlayerLiveActor *e)
{
    if (!cl_world_ok(w) || !e || !w->globals->follow->target) return cl_fault(w, 0x00193D90);
    const EmCamLeftWorkers *k = w->workers;
    CL_NEED(w, 0x001B12B0, k->approach);
    CL_NEED(w, 0x0011E2A8, k->sine);
    CL_NEED(w, 0x0011DE90, k->cosine);
    EmCameraFollowRecord *c = w->cam;
    uint32_t v;
    if (em_camleft_001916C0(w, e, 1) < 0) return -1;                  /* 00193DA8: mode 1 */
    CL_CALL(w, 0x001B12B0, k->approach, k->context, cl_cw(c, 0x48), cl_cw(c, 0x44),
            UINT32_C(0x3B64C389), &v);                                 /* 00193DC0: 0.2 degree */
    cl_cset(c, 0x44, v);                                               /* 00193DC8 */
    CL_CALL(w, 0x0011E2A8, k->sine, k->context, v, &v);
    cl_cset(c, 0x10, cl_sub(cl_cw(c, 0x20), cl_mul(cl_cw(c, 0x4C), v))); /* 00193DDC..E4 */
    CL_CALL(w, 0x0011DE90, k->cosine, k->context, cl_cw(c, 0x44), &v);
    cl_cset(c, 0x18, cl_sub(cl_cw(c, 0x28), cl_mul(cl_cw(c, 0x4C), v))); /* 00193DF8..E00 */
    if (cl_eq(cl_cw(c, 0x44), cl_cw(c, 0x48))) {                       /* 00193E0C */
        cl_cbset(c, 1, 1);
        cl_cbset(c, 3, 0);
    }
    int32_t state = (int32_t)cl_pw(e, 0x230);                          /* 00193E28 */
    if (state != 1 && state != 2) {
        cl_cbset(c, 1, 1);                                             /* 00193E44 */
        cl_cbset(c, 3, 0);
    }
    if (cl_cb(c, 7) & (cl_cb(c, 3) == 0 ? 0xD : 0xB)) {                /* 00193E4C..80 */
        cl_cbset(c, 1, 1);
        cl_cbset(c, 3, 0);
    }
    return 0;
}

/* ======================================================================
 * 00191000(cam, e): the L1 orient-behind request. *result = 1 / 0.
 * ====================================================================== */
int em_camleft_00191000(EmCamLeftWorld *w, const EmPlayerLiveActor *e, int *result)
{
    if (!cl_world_ok(w) || !e) return cl_fault(w, 0x00191000);
    const EmCamLeftGlobals *gl = w->globals;
    if (!gl->dE74 || !gl->s3B80 || !gl->follow->d69C) return cl_fault(w, 0x00191000);
    CL_NEED(w, 0x001B1470, w->workers->wrap);
    EmCameraFollowRecord *c = w->cam;
    const EmCamLeftWorkers *k = w->workers;
    if (result) *result = 0;
    if ((*gl->dE74 & *gl->s3B80) == 0) return 0;                       /* 0019101C */
    if (em_live_u8(e, 0x1F0) == 6) {                                   /* 00191030 */
        uint32_t v;
        CL_CALL(w, 0x001B1470, k->wrap, k->context, cl_add(CL_PI, cl_pw(e, 0xC4)), &v);
        cl_cset(c, 0x48, v);                                           /* 00191054 */
    } else {
        cl_cset(c, 0x48, cl_pw(e, 0xC4));                              /* 0019105C */
    }
    uint32_t d;
    CL_CALL(w, 0x001B1470, k->wrap, k->context, cl_sub(cl_cw(c, 0x48), cl_cw(c, 0x44)), &d);
    if (cl_le(cl_fabs(d), CL_3DEG)) return 0;                          /* 00191088 */
    cl_cbset(c, 6, 3);                                                 /* 0019109C */
    cl_cbset(c, 1, 0);
    uint32_t v = cl_fabs(*gl->follow->d69C);                           /* 001910A8 */
    cl_cset(c, 0x4C, v);                                               /* 001910C8 */
    if (cl_lt(v, CL_7)) {
        cl_cset(c, 0x4C, CL_7);                                        /* 001910D0 */
    } else if (!cl_le(cl_cw(c, 0x4C), cl_fabs(cl_cw(c, 0x64)))) {      /* 001910DC..EC */
        cl_cset(c, 0x4C, cl_fabs(cl_cw(c, 0x64)));                     /* 001910F8 */
    }
    if (result) *result = 1;
    return 0;
}

/* ======================================================================
 * 0022FCA0(cam): the boom pull-in, the push-out and the orbit.
 * ====================================================================== */
static int boom_pull(EmCamLeftWorld *w, unsigned step_at)
{
    EmCameraFollowRecord *c = w->cam;
    uint32_t *a0 = em_camleft_spad(w->scratch, 0x700038A0);
    if (cl_v_sub(a0, c->bytes + 0x20, c->bytes + 0x10) < 0) return cl_fault(w, 0x001028D0);
    a0[1] = 0;                                                         /* 0022FD08 */
    a0[3] = 0;
    if (cl_v_normalize(a0, a0) < 0) return cl_fault(w, 0x00102760);
    uint32_t step = *em_camleft_spad(w->scratch, step_at);
    cl_cset(c, 0x10, cl_add(cl_cw(c, 0x10), cl_mul(a0[0], step)));     /* 0022FD30 */
    step = *em_camleft_spad(w->scratch, step_at);
    cl_cset(c, 0x18, cl_add(cl_cw(c, 0x18), cl_mul(a0[2], step)));     /* 0022FD50 */
    return 0;
}

int em_camleft_0022FCA0(EmCamLeftWorld *w)
{
    if (!cl_world_ok(w)) return cl_fault(w, 0x0022FCA0);
    const EmCameraFollowGlobals *g = w->globals->follow;
    if (!g->d690 || !g->d69C) return cl_fault(w, 0x0022FCA0);
    const EmCamLeftWorkers *k = w->workers;
    CL_NEED(w, 0x001B1240, k->heading);
    CL_NEED(w, 0x0011E2A8, k->sine);
    CL_NEED(w, 0x0011DE90, k->cosine);
    EmCameraFollowRecord *c = w->cam;
    EmCamLeftScratch *s = w->scratch;
    uint32_t *a20 = em_camleft_spad(s, 0x70003A20), *a24 = em_camleft_spad(s, 0x70003A24);
    uint32_t *a28 = em_camleft_spad(s, 0x70003A28), *a2c = em_camleft_spad(s, 0x70003A2C);
    uint32_t slack = cl_sub(*g->d690, cl_fabs(cl_cw(c, 0x0C)));        /* 0022FCC4 */
    *a20 = slack;
    if (!cl_le(slack, CL_ZERO)) {                                      /* 0022FCD8 */
        if (boom_pull(w, 0x70003A20) < 0) return -1;
        cl_cbset(c, 3, 0);                                             /* 0022FD5C */
        return 0;
    }
    uint32_t thresh = cl_eq(CL_M46_8, cl_cw(c, 0x64)) ? CL_M20 : CL_M10; /* 0022FD60 */
    if (!cl_lt(slack, thresh)) {                                       /* 0022FD9C */
        cl_cbset(c, 3, 0);                                             /* 0022FFE4 */
        return 0;
    }
    *a24 = cl_sub(slack, thresh);                                      /* 0022FDB8 */
    if (!cl_le(*g->d69C, CL_8_6))                                      /* 0022FDD0 */
        return boom_pull(w, 0x70003A24);
    if (cl_cb(c, 3) == 0) {                                            /* 0022FE5C */
        uint32_t obj[3], h;
        cl_words(obj, c->bytes + 0x10, 3);
        CL_CALL(w, 0x001B1240, k->heading, k->context, obj, cl_cw(c, 0x20), cl_cw(c, 0x28), &h);
        *a28 = h;                                                      /* 0022FE78 */
        *a2c = cl_sub(cl_cw(c, 0x90), *a28);                           /* 0022FE90 */
        cl_cbset(c, 3, cl_le(*a2c, CL_ZERO) ? 2 : 1);                  /* 0022FE98..B8 */
    }
    uint32_t turn = cl_div(cl_mul(CL_PI, cl_mul(CL_0_3, *a24)), CL_180); /* 0022FEE8..FF4C */
    if (cl_cb(c, 3) == 1) *a28 = cl_add(*a28, turn);                   /* 0022FF10 */
    else *a28 = cl_sub(*a28, turn);                                    /* 0022FF60 */
    uint32_t v;
    CL_CALL(w, 0x0011E2A8, k->sine, k->context, *a28, &v);
    *em_camleft_spad(s, 0x700038A0) = v;                               /* 0022FF70 */
    *em_camleft_spad(s, 0x700038A4) = 0;
    CL_CALL(w, 0x0011DE90, k->cosine, k->context, *a28, &v);
    *em_camleft_spad(s, 0x700038A8) = v;                               /* 0022FF88 */
    *em_camleft_spad(s, 0x700038AC) = 0;
    cl_cset(c, 0x10, cl_add(cl_cw(c, 0x10),
                            cl_mul(*em_camleft_spad(s, 0x700038A0), *a24))); /* 0022FFB0 */
    cl_cset(c, 0x18, cl_add(cl_cw(c, 0x18),
                            cl_mul(*em_camleft_spad(s, 0x700038A8), *a24))); /* 0022FFD4 */
    return 0;
}

/* ======================================================================
 * 00194D10(cam, e, index): *result 1 when e is inside region `index` of
 * D_0024A5F0 (001B1EA0 mode 0, 4 vertices) and |e+A4 - its height| < 4.
 * ====================================================================== */
int em_camleft_00194D10(EmCamLeftWorld *w, const EmPlayerLiveActor *e, int index, int *result)
{
    if (!cl_world_ok(w) || !e || index < 0) return cl_fault(w, 0x00194D10);
    const EmCamLeftGlobals *gl = w->globals;
    size_t word = (size_t)index * 16u + 1u;                            /* D_0024A5F4 + 0x40 * index */
    if (!gl->d24A5F0 || word >= gl->d24A5F0_words) return cl_fault(w, 0x0024A5F0);
    CL_NEED(w, 0x001B1EA0, w->workers->inside);
    const EmCamLeftWorkers *k = w->workers;
    uint32_t point[3];
    int r;
    cl_words(point, e->bytes + 0xA0, 3);
    CL_CALL(w, 0x001B1EA0, k->inside, k->context, 0, point,
                                     UINT32_C(0x0024A5F0) + 0x40u * (uint32_t)index, 4, &r); /* 00194D3C */
    int out = 0;
    if (r != 0) {                                                      /* 00194D44 */
        uint32_t d = cl_fabs(cl_sub(cl_pw(e, 0xA4), gl->d24A5F0[word])); /* 00194D5C..64 */
        out = cl_lt(d, CL_4) ? 1 : 0;                                  /* 00194D74 */
    }
    if (result) *result = out;
    return 0;
}

/* ======================================================================
 * 00230000(cam, e): the locomotion tether.
 * ====================================================================== */
static int tether_tail_calls(EmCamLeftWorld *w, EmPlayerLiveActor *e)
{
    int32_t state = (int32_t)cl_pw(e, 0x230);                          /* 002300BC / 002301C8 */
    if (state == 2 || state == 0xF) {
        if (em_camleft_00191000(w, e, NULL) < 0) return -1;
    }
    int r;
    const EmCamLeftWorkers *k = w->workers;
    CL_CALL(w, 0x0018D7B0, k->solve_dispatch, k->context, w->cam, 0, &r); /* 002300DC */
    return 0;
}

int em_camleft_00230000(EmCamLeftWorld *w, EmPlayerLiveActor *e)
{
    if (!cl_world_ok(w) || !e) return cl_fault(w, 0x00230000);
    const EmCameraFollowGlobals *g = w->globals->follow;
    const EmCamLeftGlobals *gl = w->globals;
    const EmCamLeftWorkers *k = w->workers;
    if (!g->area || !g->d701 || !g->d702 || !g->eye || !g->target || !g->d690 || !g->d69C ||
        !gl->dE74 || !gl->s3B80)
        return cl_fault(w, 0x00230000);
    CL_NEED(w, 0x001B1240, k->heading);
    CL_NEED(w, 0x0011E2A8, k->sine);
    CL_NEED(w, 0x0011DE90, k->cosine);
    CL_NEED(w, 0x001B1470, k->wrap);
    CL_NEED(w, 0x0018D7B0, k->solve_dispatch);
    if (*g->area == 0xB) {
        CL_NEED(w, 0x001B1EA0, k->inside);
        if (!gl->d24A5F0 || gl->d24A5F0_words <= 17) return cl_fault(w, 0x0024A5F0);
    }
    EmCameraFollowRecord *c = w->cam;
    uint8_t area = *g->area;                                           /* 00230010 */
    if (area == 0xB) {                                                 /* 00230038 */
        if (em_camleft_0022FCA0(w) < 0) return -1;
        int inside;
        if (em_camleft_00194D10(w, e, 1, &inside) < 0) return -1;     /* 0023004C */
        uint32_t y = cl_add(inside ? CL_6 : cl_cw(c, 0x8C),
                            cl_add(CL_11, cl_add(cl_cw(c, 0x5C), cl_pw(e, 0xA4)))); /* 00230078 / 0023009C */
        if (em_camera_follow_00191D40(c, g, y, CL_4) < 0) return cl_fault(w, 0x00191D40);
        if (tether_tail_calls(w, e) < 0) return -1;
    } else if (area == 0 && cl_lt(cl_pw(e, 0xA4), CL_M83)) {           /* 002300EC..FC */
        cl_cset(c, 0x98, 0);                                           /* 00230108 */
        cl_cset(c, 0x10, CL_120);
        cl_cset(c, 0x18, CL_M1590);
        if (em_camera_follow_00191D40(c, g, CL_M67_5, CL_4) < 0) return cl_fault(w, 0x00191D40);
        int r;
        CL_CALL(w, 0x0018D7B0, k->solve_dispatch, k->context, c, 5, &r); /* 00230144 */
        if (cl_chase_xz(c->bytes + 0x10, g->eye, CL_4) < 0) return cl_fault(w, 0x0018C6A0);
        if (cl_chase_y(g->eye, cl_cw(c, 0x14), CL_4) < 0) return cl_fault(w, 0x0018C4B0);
    } else {
        if (em_camleft_0022FCA0(w) < 0) return -1;                    /* 00230188 */
        uint32_t y = cl_add(cl_cw(c, 0x8C),
                            cl_add(CL_11, cl_add(cl_cw(c, 0x5C), cl_pw(e, 0xA4)))); /* 002301B4 */
        if (em_camera_follow_00191D40(c, g, y, CL_4) < 0) return cl_fault(w, 0x00191D40);
        if (tether_tail_calls(w, e) < 0) return -1;
    }
    uint32_t obj[3], h;
    cl_words(obj, g->eye, 3);
    CL_CALL(w, 0x001B1240, k->heading, k->context, obj, g->target[0], g->target[2], &h); /* 00230208 */
    cl_cset(c, 0x44, h);                                               /* 00230210 */
    return 0;
}

/* ======================================================================
 * 00190F20(cam, e): the area-transition trigger.
 * ====================================================================== */
static int trigger_ready(EmCamLeftWorld *w)
{
    const EmCamLeftGlobals *gl = w->globals;
    if (!gl->d6B8 || !gl->follow->area) return cl_fault(w, 0x00190F20);
    if (*gl->d6B8 != 0) return 0;
    uint8_t area = *gl->follow->area;
    if (area == 0x12 || area == 0xE) CL_NEED(w, 0x001B0C60, w->workers->w_001B0C60);
    if (area == 0xE) {
        if (!gl->s3B8D) return cl_fault(w, 0x00190F20);
        CL_NEED(w, 0x001B1EA0, w->workers->inside);
    }
    return 0;
}

int em_camleft_00190F20(EmCamLeftWorld *w, EmPlayerLiveActor *e)
{
    if (!cl_world_ok(w) || !e) return cl_fault(w, 0x00190F20);
    if (trigger_ready(w) < 0) return -1;
    const EmCamLeftGlobals *gl = w->globals;
    const EmCamLeftWorkers *k = w->workers;
    if (*gl->d6B8 != 0) return 0;                                      /* 00190F30 */
    uint8_t area = *gl->follow->area;
    if (area == 0x12) {                                                /* 00190F44 */
        if (cl_le(cl_pw(e, 0xA0), CL_285)) {                           /* 00190F60 */
            CL_CALL(w, 0x001B0C60, k->w_001B0C60, k->context, 0xE, 0, 1);
            cl_cbset(w->cam, 6, 7);                                    /* 00190F90 */
        }
    } else if (area == 0xE && *gl->s3B8D == 0) {                       /* 00190F94..A8 */
        uint32_t point[3];
        int r;
        cl_words(point, e->bytes + 0xA0, 3);
        CL_CALL(w, 0x001B1EA0, k->inside, k->context, 0, point, UINT32_C(0x0024A4B0), 4, &r);
        if (r != 0) {                                                  /* 00190FC4 */
            CL_CALL(w, 0x001B0C60, k->w_001B0C60, k->context, 0x12, 0, 0);
            cl_cbset(w->cam, 6, 7);                                    /* 00190FE0 */
        }
    }
    return 0;
}

/* ======================================================================
 * 0018C5A0(v, y, max): the height chase of 00191580.
 * ====================================================================== */
int em_camleft_0018C5A0(EmCamLeftWorld *w, uint32_t v[2], uint32_t y, uint32_t max, int *result)
{
    if (!cl_world_ok(w) || !v) return cl_fault(w, 0x0018C5A0);
    uint32_t goal = cl_add(y, cl_cw(w->cam, 0x98));                    /* 0018C5C8: D_00810278 = cam+98 */
    uint32_t diff = cl_sub(goal, v[1]);                                /* 0018C5CC */
    uint32_t t = cl_fabs(diff);
    if (!cl_le(t, CL_ONE)) {                                           /* 0018C5EC */
        uint32_t step = cl_div(t, CL_8);                               /* 0018C600 */
        if (cl_le(max, step)) step = max;                              /* 0018C60C: bc1tl */
        if (cl_lt(diff, CL_ZERO)) step = cl_neg(step);                 /* 0018C630 */
        v[1] = cl_add(v[1], step);                                     /* 0018C640 */
        if (result) *result = 0;
        return 0;
    }
    v[1] = cl_add(v[1], cl_div(diff, CL_4));                           /* 0018C660..70 */
    if (result) *result = 4;
    return 0;
}

/* ======================================================================
 * 00191580(cam, e) and 001914A0(cam, e): camera action 8.
 * ====================================================================== */
int em_camleft_00191580(EmCamLeftWorld *w, EmPlayerLiveActor *e)
{
    if (!cl_world_ok(w) || !e || !w->globals->follow->d69C) return cl_fault(w, 0x00191580);
    EmCameraFollowRecord *c = w->cam;
    EmCamLeftScratch *s = w->scratch;
    if (em_camleft_001916C0(w, e, 0) < 0) return -1;                  /* 00191598 */
    uint32_t *a20 = em_camleft_spad(s, 0x70003A20), *a24 = em_camleft_spad(s, 0x70003A24);
    *a20 = cl_sub(*w->globals->follow->d69C, cl_fabs(cl_cw(c, 0x0C))); /* 001915C4 */
    uint32_t lo = cl_eq(CL_M46_8, cl_cw(c, 0x64)) ? CL_M20 : CL_M10;  /* 001915D0 */
    uint32_t t = *a20, y;
    if (cl_lt(t, lo)) {                                                /* 00191600 */
        uint32_t d = cl_mul(CL_0_5, cl_sub(t, lo));                    /* 00191610..28 */
        *a24 = d;
        if (cl_lt(d, CL_M1_5)) *a24 = CL_M1_5;                         /* 0019162C..44 */
        y = cl_add(CL_11, cl_add(cl_cw(c, 0x8C),
                                 cl_add(cl_pw(e, 0xA4), cl_sub(cl_cw(c, 0x5C), *a24)))); /* 00191664..74 */
    } else {
        y = cl_add(CL_11, cl_add(cl_cw(c, 0x8C),
                                 cl_add(cl_cw(c, 0x5C), cl_pw(e, 0xA4)))); /* 0019168C..94 */
    }
    uint32_t v[2];
    cl_words(v, c->bytes + 0x10, 2);
    if (em_camleft_0018C5A0(w, v, y, CL_4, NULL) < 0) return -1;       /* 001916A0 */
    cl_put_words(c->bytes + 0x10, v, 2);
    return 0;
}

int em_camleft_001914A0(EmCamLeftWorld *w, EmPlayerLiveActor *e)
{
    if (!cl_world_ok(w) || !e || !w->globals->follow->eye || !w->globals->d28A9A0)
        return cl_fault(w, 0x001914A0);
    EmCameraFollowRecord *c = w->cam;
    uint32_t *eye = w->globals->follow->eye;
    if (em_camleft_00191580(w, e) < 0) return -1;                     /* 001914B8 */
    if (cl_chase_y(eye, cl_cw(c, 0x14), CL_0_4) < 0) return cl_fault(w, 0x0018C4B0); /* 001914D4 */
    if (cl_chase_xz(c->bytes + 0x10, eye, CL_0_4) < 0) return cl_fault(w, 0x0018C6A0); /* 001914F0 */
    if (*w->globals->d28A9A0 == 0 && em_live_u8(e, 4) != 5)            /* 001914FC..10 */
        cl_cbset(c, 6, 0);                                             /* 00191514 */
    return 0;
}

/* ======================================================================
 * 0018BC20(cam, e): the camera action dispatch.
 * ====================================================================== */

/* The handler of (mode, action) in the tables at 0x26D950 (mode 0) and
 * 0x26D910 (mode 1); 0 = none; 1 = mode 1's locomotion path. */
static uint32_t dispatch_target(unsigned mode, unsigned action)
{
    if (mode == 0) {
        if (action >= 16) return 0x00195130;                           /* 0018BC64 */
        static const uint32_t t0[16] = {
            0x00195130, 0x00197D20, 0x00198650, 0x001936E0, 0, 0x0018CA90, 0x0028A9A0, 0,
            0x001914A0, 0x00198CE0, 0x00198D90, 0x00198F10, 0x001963A0, 0x00196CE0,
            0x00198AF0, 0x00197390 };
        return t0[action];
    }
    if (action >= 16) return 1;                                        /* 0018BDD0 */
    static const uint32_t t1[16] = {
        1, 0x00197D20, 0x00198650, 0x001936E0, 0, 0x0018CA90, 0x0028A9A0, 0,
        0x001914A0, 1, 0x00198D90, 1, 0x001963A0, 0x00196CE0, 1, 0x00197390 };
    return t1[action];
}

typedef int (*cl_handler)(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e);

static cl_handler handler_of(const EmCamLeftWorkers *k, uint32_t address)
{
    switch (address) {
    case 0x00195130: return k->w_00195130;
    case 0x00197D20: return k->w_00197D20;
    case 0x00198650: return k->w_00198650;
    case 0x00198AF0: return k->w_00198AF0;
    case 0x001936E0: return k->w_001936E0;
    case 0x0018CA90: return k->w_0018CA90;
    case 0x00198CE0: return k->w_00198CE0;
    case 0x00198D90: return k->w_00198D90;
    case 0x00198F10: return k->w_00198F10;
    case 0x001963A0: return k->w_001963A0;
    case 0x00196CE0: return k->w_00196CE0;
    case 0x00197390: return k->w_00197390;
    default: return NULL;
    }
}

/* Everything the dispatch can reach from the current state, checked before
 * any write (00190F20 can only set the action to 7, which calls nothing). */
static int dispatch_ready(EmCamLeftWorld *w)
{
    const EmCamLeftWorkers *k = w->workers;
    const EmCamLeftGlobals *gl = w->globals;
    const EmCameraFollowGlobals *g = gl->follow;
    if (trigger_ready(w) < 0) return -1;
    unsigned mode = cl_cb(w->cam, 5), action = cl_cb(w->cam, 6);
    if (mode > 1) return 0;
    uint32_t target = dispatch_target(mode, action);
    if (target == 0) return 0;
    if (target == 0x0028A9A0) {
        if (!gl->d28A9A0) return cl_fault(w, 0x0018BC20);
        return 0;
    }
    if (target == 0x001914A0) {
        /* 001914A0 -> 00191580 -> 001916C0(mode 0) and 0018C5A0 */
        if (!g->eye || !g->target || !g->d690 || !g->d69C || !gl->d28A9A0)
            return cl_fault(w, 0x0018BC20);
        CL_NEED(w, 0x001DD980, k->w_001DD980);
        return 0;
    }
    if (target == 1) {
        if (!g->eye || !g->target) return cl_fault(w, 0x0018BC20);
        if (cl_cb(w->cam, 1) == 0) CL_NEED(w, 0x001B0300, k->w_001B0300);
        CL_NEED(w, 0x00193EB0, k->w_00193EB0);
        CL_NEED(w, 0x001B1240, k->heading);
        return 0;
    }
    CL_NEED(w, target, handler_of(k, target));
    if (target == 0x00198D90) CL_NEED(w, 0x001D2830, k->w_001D2830);
    return 0;
}

int em_camleft_0018BC20(EmCamLeftWorld *w, EmPlayerLiveActor *e)
{
    if (!cl_world_ok(w) || !e) return cl_fault(w, 0x0018BC20);
    if (dispatch_ready(w) < 0) return -1;
    EmCameraFollowRecord *c = w->cam;
    const EmCamLeftWorkers *k = w->workers;
    const EmCamLeftGlobals *gl = w->globals;
    if (em_camleft_00190F20(w, e) < 0) return -1;                     /* 0018BC34 */
    unsigned mode = cl_cb(c, 5);                                       /* 0018BC3C */
    if (mode > 1) return 0;                                            /* 0018BC54 */
    uint32_t target = dispatch_target(mode, cl_cb(c, 6));              /* 0018BC5C / 0018BDC8 */
    switch (target) {
    case 0:                                                            /* actions 4 and 7 */
        return 0;
    case 0x0028A9A0:                                                   /* action 6 */
        if (*gl->d28A9A0 == 0) cl_cbset(c, 6, 0);                      /* 0018BD0C */
        return 0;
    case 0x0018CA90:                                                   /* action 5 */
        CL_CALL(w, target, k->w_0018CA90, k->context, c, e);
        cl_cbset(c, 6, 7);                                             /* 0018BD00 */
        return 0;
    case 0x001914A0:                                                   /* action 8 */
        if (em_camleft_001914A0(w, e) < 0) return -1;
        CL_CALL(w, 0x001DD980, k->w_001DD980, k->context, gl->follow->eye, gl->follow->target);
        return 0;
    case 0x00198D90:                                                   /* action 10 */
        CL_CALL(w, target, k->w_00198D90, k->context, c, e);
        CL_CALL(w, 0x001D2830, k->w_001D2830, k->context, 3, 1);       /* 0018BD68 */
        return 0;
    case 1:
        break;                                                         /* mode 1's locomotion path */
    default: {
        cl_handler fn = handler_of(k, target);
        CL_CALL(w, target, fn, k->context, c, e);
        return 0;
    }
    }

    /* Mode 1, actions 0 / 9 / 11 / 14 / out of range (0018BDF4). */
    uint8_t u = cl_cb(c, 1);
    if (u != 1) {
        if (u != 0) return 0;                                          /* 0018BE0C */
        cl_cbset(c, 1, 1);                                             /* 0018BE1C */
        cl_cbset(c, 2, 0);
        cl_chset(c, 8, 0);                                             /* 0018BE24 */
        CL_CALL(w, 0x001B0300, k->w_001B0300, k->context);
    }
    if (cl_cb(c, 6) == 0xB && (int32_t)cl_pw(e, 0x230) != 0x12)        /* 0018BE28..44 */
        cl_cbset(c, 6, 0);
    int32_t state = (int32_t)cl_pw(e, 0x230);                          /* 0018BE4C */
    switch (state) {
    case 5:                                                            /* 0018BF14 */
        if (cl_chase_xz(e->bytes + 0xB0, c->bytes + 0x20, CL_0_8) < 0) return cl_fault(w, 0x0018C6A0);
        if (cl_chase_y(c->bytes + 0x20, cl_add(cl_pw(e, 0xB4), cl_cw(c, 0x8C)), CL_ONE) < 0)
            return cl_fault(w, 0x0018C4B0);
        break;
    case 8: case 9: case 7: case 6: case 0x2D: case 0x2C:              /* 0018BF84 */
        break;
    case 0x11:                                                         /* 0018BEC4 */
        cl_cbset(c, 6, 10);
        cl_cbset(c, 1, 0);
        break;
    default:                                                           /* 0xA (0018BED4) and the rest (0018BF50) */
        if (cl_chase_xz(e->bytes + 0xA0, c->bytes + 0x20, CL_0_8) < 0) return cl_fault(w, 0x0018C6A0);
        if (cl_chase_y(c->bytes + 0x20, cl_add(CL_15, cl_pw(e, 0xA4)), CL_ONE) < 0)
            return cl_fault(w, 0x0018C4B0);
        break;
    }
    if (em_camleft_0018C0C0(w) < 0) return -1;                         /* 0018BF88 */
    CL_CALL(w, 0x00193EB0, k->w_00193EB0, k->context, c, e, 0);        /* 0018BF98 */
    uint32_t obj[3], h;
    cl_words(obj, gl->follow->eye, 3);
    CL_CALL(w, 0x001B1240, k->heading, k->context, obj, gl->follow->target[0],
                                      gl->follow->target[2], &h);     /* 0018BFB4 */
    cl_cset(c, 0x44, h);                                               /* 0018BFC0 */
    return 0;
}

/* ======================================================================
 * 0018B9C0(cam): the camera frame.
 * ====================================================================== */
int em_camleft_0018B9C0(EmCamLeftWorld *w)
{
    if (!cl_world_ok(w) || !w->player) return cl_fault(w, 0x0018B9C0);
    const EmCamLeftGlobals *gl = w->globals;
    const EmCameraFollowGlobals *g = gl->follow;
    const EmCamLeftWorkers *k = w->workers;
    EmCameraFollowRecord *c = w->cam;
    EmPlayerLiveActor *base = w->player;
    if (!gl->d6EF || !gl->s31F0) return cl_fault(w, 0x0018B9C0);
    uint8_t st = cl_cb(c, 0);
    if (st == 0) {
        if (!gl->d5F0 || !g->eye || !g->target || !g->area || !g->d702)
            return cl_fault(w, 0x0018B9C0);
        CL_NEED(w, 0x001B1240, k->heading);
        CL_NEED(w, 0x0019A910, k->segment);
        CL_NEED(w, 0x0018D7B0, k->solve_dispatch);
        CL_NEED(w, 0x0018C0D0, k->commit);
    } else if (st == 1) {
        CL_NEED(w, 0x0018C0D0, k->commit);
        uint8_t sub = cl_cb(c, 4);
        if (sub == 3) CL_NEED(w, 0x0022EEF0, k->w_0022EEF0);
        if (sub == 0 && dispatch_ready(w) < 0) return -1;
    }

    if (*gl->d6EF != 0) *gl->d6EF = (uint8_t)(*gl->d6EF - 1);         /* 0018B9EC */
    cl_cbset(c, 0x8B, cl_cb(c, 0x8B) | *gl->s31F0);                    /* 0018BA00 */
    if (st == 1) {                                                     /* 0018BB7C */
        uint8_t sub = cl_cb(c, 4);
        if (sub == 0) {                                                /* 0018BBB4 */
            if (em_camera_follow_00191390(c, base) < 0) return cl_fault(w, 0x00191390);
            if (em_camleft_0018BC20(w, base) < 0) return -1;
            CL_CALL(w, 0x0018C0D0, k->commit, k->context, c, 1);
        } else if (sub == 3) {                                         /* 0018BBE0 */
            CL_CALL(w, 0x0022EEF0, k->w_0022EEF0, k->context, c, 1);
            CL_CALL(w, 0x0018C0D0, k->commit, k->context, c, 0);
        } else {                                                       /* 1, 2 and the rest */
            CL_CALL(w, 0x0018C0D0, k->commit, k->context, c, 1);
        }
        return 0;
    }
    if (st != 0) return 0;                                             /* 0018BA24 */

    /* State 0: the one-shot seat (0018BA2C). */
    cl_cbset(c, 0, 1);
    gl->d5F0[0] = 0;                                                   /* 0018BA34 */
    gl->d5F0[1] = CL_M1;
    gl->d5F0[2] = 0;
    gl->d5F0[3] = CL_ONE;
    cl_cset(c, 0x0C, cl_cw(c, 0x64));                                  /* 0018BA7C */
    cl_cbset(c, 4, 0);
    cl_cbset(c, 7, 0);
    cl_cset(c, 0x90, 0);
    cl_cset(c, 0x54, CL_1000);
    cl_cset(c, 0x50, CL_M200);
    cl_cset(c, 0x5C, CL_2);
    cl_cset(c, 0x8C, CL_6);
    cl_chset(c, 0x5A, 0);
    cl_cbset(c, 0x6D, 0);
    cl_cbset(c, 0x6C, 0);
    cl_chset(c, 0xA0, 0);                                              /* 0018BAAC */
    uint32_t obj[3], h;
    cl_words(obj, g->eye, 3);
    CL_CALL(w, 0x001B1240, k->heading, k->context, obj, g->target[0], g->target[2], &h);
    cl_cset(c, 0x44, h);                                               /* 0018BAD0 */
    if (em_camleft_0018CE60(w, base->bytes + 0xB0, 5) < 0) return -1; /* 0018BAD4 */
    if (cl_cb(c, 5) == 1) return 0;                                    /* 0018BAE4 */
    uint8_t a = cl_cb(c, 6);
    if (a == 0xA || a == 0xF || a == 0xD) return 0;                    /* 0018BAF0..0C */
    cl_cbset(c, 6, 8);                                                 /* 0018BB14 */
    if (*g->area == 0x12 && *g->d702 == 0) cl_cbset(c, 6, 0);          /* 0018BB18..30 */
    if (em_camleft_0018C0C0(w) < 0) return -1;                         /* 0018BB38 */
    int r;
    CL_CALL(w, 0x0018D7B0, k->solve_dispatch, k->context, c, 1, &r);   /* 0018BB44 */
    CL_CALL(w, 0x0018C0D0, k->commit, k->context, c, 1);               /* 0018BB50 */
    cl_words(obj, g->eye, 3);
    CL_CALL(w, 0x001B1240, k->heading, k->context, obj, g->target[0], g->target[2], &h);
    cl_cset(c, 0x44, h);                                               /* 0018BB74 */
    return 0;
}

/* ======================================================================
 * 0015CBA0(p): the state byte +1F0 -> action code +230 (byte-matched C;
 * the jump table at 0x26D4E0 has 71 entries, 0..0x46).
 * ====================================================================== */
int em_camleft_0015CBA0(EmPlayerLiveActor *p)
{
    if (!p) return -1;
    uint8_t st = em_live_u8(p, 0x1F0), sub = em_live_u8(p, 0x1F1);
    int flag = em_live_u8(p, 0x236) != 0;
    int32_t code;
    switch (st) {
    case 0: case 2: case 3: case 4: case 5: case 65: code = flag ? 2 : 1; break;
    case 1: case 6: case 7: case 15: case 58:        code = flag ? 4 : 3; break;
    case 8: case 9: case 11: case 12: case 20:       code = 5; break;
    case 13: case 19:                                code = 0x17; break;
    case 14:                                         code = 0x18; break;
    case 16: case 18:                                code = 0xA; break;
    case 17:                                         code = 0xB; break;
    case 10:                                         code = 0x2B; break;
    case 21: case 27:                                code = 6; break;
    case 22: case 28:                                code = 7; break;
    case 23: case 25: case 26:                       code = 8; break;
    case 24: case 30:                                code = 9; break;
    case 29: case 31:                                code = 0x19; break;
    case 32:                                         code = 0x1D; break;
    case 33:                                         code = sub == 0 ? 0x1F : 0x1E; break;
    case 34:                                         code = sub == 0 ? 0x21 : 0x20; break;
    case 35:                                         code = 0x22; break;
    case 36:                                         code = 0x23; break;
    case 37: case 38: case 39:                       code = 0x24; break;
    case 40:                                         code = 0x25; break;
    case 41:                                         code = 0x1A; break;
    case 42:                                         code = 0x1B; break;
    case 43:                                         code = 0x1C; break;
    case 49:                                         code = sub == 2 ? 1 : 0xD; break;
    case 50:                                         code = sub == 2 ? 1 : 0xC; break;
    case 52:                                         code = sub == 2 ? 0x24 : 0x2A; break;
    case 53:                                         code = sub == 2 ? 0x24 : 0x29; break;
    case 56: case 57:                                code = sub == 2 ? 1 : 0x28; break;
    case 70:                                         code = 0x2F; break;
    case 54: case 55:                                code = flag ? 0xF : 0xE; break;
    case 44: {
        uint8_t m = em_live_u8(p, 0x0D);
        if (m == 0 || m == 1) code = 0x10;
        else if (m == 2) code = sub == 0 ? 0xA : 0x10;
        else code = 5;
        break;
    }
    case 45:                                         code = 0x11; break;
    case 46:                                         code = 0x12; break;
    case 47:                                         code = sub == 0 ? 0x14 : sub == 1 ? 0x15 : 0x16; break;
    case 48:                                         code = 0x13; break;
    case 59: case 60: case 61: case 62:              code = flag ? 2 : 1; break;
    case 63:                                         code = 0x26; break;
    case 64:                                         code = 0x27; break;
    case 67:                                         code = 0x2C; break;
    case 68:                                         code = 0x2D; break;
    case 69:                                         code = 0x2E; break;
    case 51:                                         return 0;         /* +230 untouched */
    default:                                         code = 0; break;  /* 66 and out of range */
    }
    em_live_set_u32(p, 0x230, (uint32_t)code);
    return 0;
}

/* ======================================================================
 * Binding helpers
 * ====================================================================== */
void em_camleft_scratch_from_follow(EmCamLeftScratch *s, const EmCameraFollowScratch *f)
{
    memcpy(em_camleft_spad(s, 0x700038A0), f->s38A0, 16);
    memcpy(em_camleft_spad(s, 0x700038B0), f->s38B0, 16);
    memcpy(em_camleft_spad(s, 0x700038C0), f->s38C0, 16);
    memcpy(em_camleft_spad(s, 0x70003910), f->s3910, 16);
    memcpy(em_camleft_spad(s, 0x70003A20), f->s3A20, 16);
}

void em_camleft_scratch_to_follow(const EmCamLeftScratch *s, EmCameraFollowScratch *f)
{
    EmCamLeftScratch *m = (EmCamLeftScratch *)(uintptr_t)s;
    memcpy(f->s38A0, em_camleft_spad(m, 0x700038A0), 16);
    memcpy(f->s38B0, em_camleft_spad(m, 0x700038B0), 16);
    memcpy(f->s38C0, em_camleft_spad(m, 0x700038C0), 16);
    memcpy(f->s3910, em_camleft_spad(m, 0x70003910), 16);
    memcpy(f->s3A20, em_camleft_spad(m, 0x70003A20), 16);
}
