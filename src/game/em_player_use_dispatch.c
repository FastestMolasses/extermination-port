/* em_player_use_dispatch.c - the Use dispatcher 00160220, the accepted-Use
 * reset 001798D0 and the gait re-entry request 0017C440 (see
 * em_player_use_dispatch.h and docs/PLAYER_USE_DISPATCH.md).
 *
 * All three are byte-matched in the decomp; the readable C there is the
 * source, cross-checked against the original instructions for the points
 * the C leaves open (which D_00810700 loads are repeated, the order of the
 * stores around each call, and the operand order of the two +-pi/4 sums).
 * Every address in a comment is the original instruction translated there.
 * tools/test_player_use_dispatch_reference.py executes the original
 * instructions and compares every record byte, the scratch word, the return
 * values and every worker call. */
#include "game/em_player_use_dispatch.h"
#include "game/em_ee_float.h"

#include <stddef.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

typedef uint32_t F;

/* Float constants as the instructions build them (lui/ori). */
#define K_ZERO   UINT32_C(0x00000000)
#define K_PI_4   UINT32_C(0x3F490FDB)   /* 0.7853982 */
#define K_4      UINT32_C(0x40800000)
#define K_18     UINT32_C(0x41900000)
#define K_46     UINT32_C(0x42380000)

/* The trigger boxes of 00160220 (00160330..00160634): the y (+B4), x (+B0)
 * and z (+B8) ranges, each bound inclusive: the instructions leave a box on
 * v < lo (c.lt) or !(v <= hi) (c.le). Inside a box the ledge probes are
 * skipped and the dispatcher goes straight to the running-jump test. */
typedef struct Box { F y0, y1, x0, x1, z0, z1; } Box;
static const Box kArea01 = {   /* D_00810700 == 1 */
    UINT32_C(0xC2200000), UINT32_C(0xC1A00000), UINT32_C(0xC20C0000), UINT32_C(0x420C0000),
    UINT32_C(0xC4834000), UINT32_C(0xC4778000) };
static const Box kArea04 = {   /* D_00810700 == 4 */
    UINT32_C(0x41200000), UINT32_C(0x41A00000), UINT32_C(0x439D8000), UINT32_C(0x43B40000),
    UINT32_C(0x439D8000), UINT32_C(0x43C08000) };
static const Box kArea0D[2] = {  /* D_00810700 == 0xD: tried in this order */
    { UINT32_C(0x43160000), UINT32_C(0x43520000), UINT32_C(0x44340000), UINT32_C(0x44480000),
      UINT32_C(0x44480000), UINT32_C(0x44520000) },
    { UINT32_C(0x43160000), UINT32_C(0x43570000), UINT32_C(0x441EC000), UINT32_C(0x44340000),
      UINT32_C(0x449EC000), UINT32_C(0x44A5A000) },
};

/* ---- record helpers ------------------------------------------------------ */

static F ld(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void st(EmPlayerLiveActor *a, unsigned at, F v) { em_live_set_u32(a, at, v); }
static unsigned u8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void put8(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u8(a, at, (uint8_t)v); }

static int inside(const EmPlayerLiveActor *a, const Box *b)
{
    F y = ld(a, 0xB4), x = ld(a, 0xB0), z = ld(a, 0xB8);
    if (em_ee_c_lt_bits(y, b->y0) || !em_ee_c_le_bits(y, b->y1)) return 0;
    if (em_ee_c_lt_bits(x, b->x0) || !em_ee_c_le_bits(x, b->x1)) return 0;
    if (em_ee_c_lt_bits(z, b->z0) || !em_ee_c_le_bits(z, b->z1)) return 0;
    return 1;
}

/* build_trs_matrix(p + D0, p + B0, p + C0, p + 60). The output starts as the
 * record's +D0 words, so a worker that writes part of it leaves the rest as
 * the record held it (the original writes the record in place). */
static int trs(const EmPlayerUseWorkers *w, EmPlayerLiveActor *a)
{
    uint32_t out[16], position[4], rotation[4], scale[4];
    for (unsigned i = 0; i < 16; i++) out[i] = ld(a, 0xD0 + 4 * i);
    for (unsigned i = 0; i < 4; i++) {
        position[i] = ld(a, 0xB0 + 4 * i);
        rotation[i] = ld(a, 0xC0 + 4 * i);
        scale[i] = ld(a, 0x60 + 4 * i);
    }
    FAULT(w->trs(w->context, out, position, rotation, scale));
    for (unsigned i = 0; i < 16; i++) st(a, 0xD0 + 4 * i, out[i]);
    return 0;
}

/* 0015DF10(p, mode, +C4) after the record's matrix was rebuilt. */
static int ledge(const EmPlayerUseWorkers *w, EmPlayerLiveActor *a, int mode, int *taken)
{
    *taken = 0;
    return w->ledge(w->context, a, mode, ld(a, 0xC4), taken);
}

/* ---- 001798D0 ------------------------------------------------------------ */

static int accepted(const EmPlayerUseWorkers *w, EmPlayerLiveActor *a)
{
    st(a, 0x38, 0);
    st(a, 0x21C, 0);
    put8(a, 0x25C, 0);
    FAULT(w->row_request(w->context, a, em_ee_float(K_ZERO)));
    put8(a, 5, 0);
    put8(a, 6, 0);
    put8(a, 0x1F0, 0);
    return 0;
}

int em_player_use_001798D0(const EmPlayerUseWorkers *w, EmPlayerLiveActor *a)
{
    if (!w || !a || !w->row_request) return -1;
    return accepted(w, a);
}

/* ---- 00160220 ------------------------------------------------------------ */

int em_player_use_workers_bound(const EmPlayerUseWorkers *w)
{
    /* classify (001AAC00) is reached only in area 0x15 and is checked there. */
    return w && w->scene && w->scan && w->row_request && w->surface && w->trs && w->ledge &&
           w->wrap && w->jump && w->aim;
}

static int dispatch(const EmPlayerUseWorkers *w, EmPlayerLiveActor *a, int *result)
{
    const EmPlayerUseScene *s = w->scene;
    int r;
    *result = 0;
    if (!(s->d810E74 & s->spad3B76)) return 0;                             /* 00160240 */
    r = 0;
    FAULT(w->scan(w->context, a, &r));                                     /* 0016024C */
    if (r != 0) {
        FAULT(accepted(w, a));                                             /* 0016025C */
        put8(a, 5, 0x25);
        put8(a, 6, 0);
        *result = 1;
        return 0;
    }
    if (s->area == 0x15) {                                                 /* 0016027C */
        if (!w->classify) return -1;
        r = 0;
        FAULT(w->classify(w->context, a, &r));                             /* 00160294 */
        if (r != 0) {
            if (r == 1) {
                put8(a, 0x1F0, 0x38);
            } else if (r == 2) {
                put8(a, 0x1F0, 0x39);
                em_live_set_u16(a, 0x2E, 0);
            } else if (r == 3) {
                put8(a, 0x1F0, 0x39);
                em_live_set_u16(a, 0x2E, 1);
            }
            put8(a, 5, 0x23);                                              /* 001602F4 */
            put8(a, 6, 0);
            put8(a, 0x1F1, 0);
            put8(a, 0, u8(a, 0) | 2);
            *result = 1;
            return 0;
        }
    }
    r = 0;
    FAULT(w->surface(w->context, a, &r));                                  /* 00160318 */
    if (r != 0) {
        *result = 1;
        return 0;
    }
    int boxed = 0;
    uint8_t area = s->area;                                                /* 00160334 */
    if (area == 1) {
        boxed = inside(a, &kArea01);
    } else if (area == 4) {
        boxed = inside(a, &kArea04);
    } else if (area == 0xD) {
        boxed = inside(a, &kArea0D[0]) || inside(a, &kArea0D[1]);
    }
    if (!boxed && u8(a, 0x236) == 0 && u8(a, 0x23B) != 0x35) {             /* 0016063C */
        F yaw = ld(a, 0xC4);                                               /* 00160658 */
        F wrapped;
        FAULT(trs(w, a));                                                  /* 00160668 */
        FAULT(ledge(w, a, 0, &r));                                         /* 00160678 */
        if (r != 0) { *result = 1; return 0; }
        FAULT(w->wrap(w->context, em_ee_sub_bits(yaw, K_PI_4), &wrapped)); /* 0016069C */
        st(a, 0xC4, wrapped);                                              /* 001606B8 */
        FAULT(trs(w, a));
        FAULT(ledge(w, a, 1, &r));                                         /* 001606C4 */
        if (r != 0) { *result = 1; return 0; }
        FAULT(w->wrap(w->context, em_ee_add_bits(K_PI_4, yaw), &wrapped)); /* 001606E8 */
        st(a, 0xC4, wrapped);                                              /* 00160704 */
        FAULT(trs(w, a));
        FAULT(ledge(w, a, 1, &r));                                         /* 00160710 */
        if (r != 0) { *result = 1; return 0; }
        st(a, 0xC4, yaw);                                                  /* 0016073C */
        FAULT(trs(w, a));
    }
    if (u8(a, 0x236) == 0 && u8(a, 0x23B) != 0x35) {                       /* 00160740 */
        r = 0;
        FAULT(w->jump(w->context, a, &r));                                 /* 0016075C */
        if (r != 0) { *result = 1; return 0; }
    }
    r = 0;
    FAULT(w->aim(w->context, a, &r));                                      /* 00160778 */
    if (r != 0) {
        put8(a, 5, 0x24);                                                  /* 0016078C */
        put8(a, 6, 0);
        put8(a, 0x1F0, 0x3A);
        put8(a, 0, 3);
        *result = 1;
    }
    return 0;
}

int em_player_use_00160220(const EmPlayerUseWorkers *w, EmPlayerLiveActor *a, int *result)
{
    if (!em_player_use_workers_bound(w) || !a || !result) return -1;
    return dispatch(w, a, result);
}

int em_player_use_dispatch_worker(void *workers, EmPlayerLiveActor *actor, int *result)
{
    return em_player_use_00160220(workers, actor, result);
}

int em_player_use_accepted_worker(void *workers, EmPlayerLiveActor *actor)
{
    return em_player_use_001798D0(workers, actor);
}

/* ---- 0017C440 ------------------------------------------------------------ */

int em_player_reentry_workers_bound(const EmPlayerReentryWorkers *w)
{
    return w && w->spad3A20 && w->speed && w->translate && w->select && w->clip_frames &&
           w->arbiter;
}

int em_player_reentry_0017C440(const EmPlayerReentryWorkers *w, EmPlayerLiveActor *a, int arg)
{
    if (!em_player_reentry_workers_bound(w) || !a) return -1;
    put8(a, 0x25C, u8(a, 0x23F) - 1u);                                     /* 0017C464 */
    F speed;
    FAULT(w->speed(w->context, u8(a, 0x25C), &speed));                     /* 0017C474 */
    st(a, 0x38, speed);                                                    /* 0017C47C */
    FAULT(w->translate(w->context, a, arg));                               /* 0017C478 */
    int16_t clip = 0;
    FAULT(w->select(w->context, a, 1, (int)u8(a, 0x235), (int)u8(a, 0x25C),
                    &clip));                                               /* 0017C48C */
    int32_t frames = 0;
    FAULT(w->clip_frames(w->context, ld(a, 0x40), clip, &frames));         /* 0017C4A0 */
    *w->spad3A20 = em_ee_cvt_s_w_bits((uint32_t)frames);                   /* 0017C4B8 */
    F less = u8(a, 0x25C) == 2 ? K_18 : K_46;                              /* 0017C4C0 */
    F frame = em_ee_sub_bits(*w->spad3A20, less);
    FAULT(w->arbiter(w->context, a, clip, em_ee_float(K_4), em_ee_float(frame)));
    put8(a, 0x1F0, 1);                                                     /* 0017C524 */
    return 0;
}

int em_player_reentry_worker(void *workers, EmPlayerLiveActor *actor, int arg)
{
    return em_player_reentry_0017C440(workers, actor, arg);
}
