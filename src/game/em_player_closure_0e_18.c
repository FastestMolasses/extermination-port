/* em_player_closure_0e_18.c - FLOOR-closure player states +5 = 0xE, 0x13,
 * 0x14, 0x18 and their private callees (see em_player_closure_0e_18.h).
 *
 * Read from the original instructions (build/asm of the decomp); the decomp
 * C of every routine here agrees with them on everything checked (the
 * differences the instructions settle are listed in
 * docs/PLAYER_CLOSURE_0E_18.md). Every comment address is the original
 * instruction the line translates. EE COP1 arithmetic goes through
 * em_ee_float.h on raw binary32 bits; loads and stores of floats the original
 * only moves (lwc1/swc1, mov.s, lq/sq) copy the bits. A store the original
 * makes in a call's delay slot is made before the worker call. */
#include "game/em_player_closure_0e_18.h"
#include "game/em_ee_float.h"
#include "game/em_player_hang.h"
#include "game/em_player_ladder_climb.h"
#include "game/em_player_ladder_entry.h"
#include "game/em_player_major2.h"
#include "game/em_sdk_math_original.h"

#include <stddef.h>
#include <string.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

/* Binary32 constants as the original materializes them (lui/ori). */
#define K_0       UINT32_C(0x00000000)
#define K_0_6     UINT32_C(0x3F19999A)
#define K_1       UINT32_C(0x3F800000)
#define K_2       UINT32_C(0x40000000)
#define K_4       UINT32_C(0x40800000)
#define K_4_5     UINT32_C(0x40900000)
#define K_5       UINT32_C(0x40A00000)
#define K_6       UINT32_C(0x40C00000)
#define K_7_5     UINT32_C(0x40F00000)
#define K_8       UINT32_C(0x41000000)
#define K_9       UINT32_C(0x41100000)
#define K_10      UINT32_C(0x41200000)
#define K_10_5    UINT32_C(0x41280000)
#define K_11_5    UINT32_C(0x41380000)
#define K_14      UINT32_C(0x41600000)
#define K_16      UINT32_C(0x41800000)
#define K_18      UINT32_C(0x41900000)
#define K_19_5    UINT32_C(0x419C0000)
#define K_20_5    UINT32_C(0x41A40000)
#define K_22      UINT32_C(0x41B00000)
#define K_25      UINT32_C(0x41C80000)
#define K_42      UINT32_C(0x42280000)
#define K_53      UINT32_C(0x42540000)
#define K_64      UINT32_C(0x42800000)
#define K_78      UINT32_C(0x429C0000)
#define K_186     UINT32_C(0x433A0000)
#define K_300     UINT32_C(0x43960000)
#define K_PI      UINT32_C(0x40490FDB)
#define K_HALF_PI UINT32_C(0x3FC90FDB)
#define K_RATE    UINT32_C(0x3E0EFA35)   /* 0.13962634 (0016B9DC) */
#define K_M0_2    UINT32_C(0xBE4CCCCD)
#define K_M3      UINT32_C(0xC0400000)
#define K_M4_5    UINT32_C(0xC0900000)
#define K_M6      UINT32_C(0xC0C00000)
#define K_M9      UINT32_C(0xC1100000)
#define K_M19_5   UINT32_C(0xC19C0000)
#define K_M1463_5 UINT32_C(0xC4B6F000)

/* Original tables (read-only data of the pinned ELF; the oracle checks every
 * word against the user's ELF and every entry is read by some case). */
/* D_00248610 / D_00248620, indexed by the gait byte +23F (00168A94 ..). */
static const uint32_t kD00248610[4] = { 0x00000000u, 0x3CF5C28Fu, 0x3D75C28Fu, 0x3DF5C28Fu };
static const uint32_t kD00248620[4] = { 0x00000000u, 0x3F4CCCCDu, 0x3F800000u, 0x3FA00000u };
/* D_002754B0 (.sdata), the two height offsets of 00180850's loop. */
static const uint32_t kD002754B0[2] = { 0x40000000u, 0xC0000000u };
/* D_002488AC (0016D420). */
static const uint32_t kD002488AC = 0x41880000u;
/* D_00275498 (.sdata words), indexed by the side byte +2F1 (0016DA68). */
static const int32_t kD00275498[2] = { 2, 3 };
/* D_00248970: 001790B0's seven probe points (x, y, z, w). */
static const uint32_t kD00248970[7][4] = {
    { 0x00000000u, 0x3F800000u, 0x40600000u, 0x3F800000u },
    { 0x40333333u, 0x3F800000u, 0x00000000u, 0x3F800000u },
    { 0xC0333333u, 0x3F800000u, 0x00000000u, 0x3F800000u },
    { 0x40333333u, 0x3F800000u, 0x40333333u, 0x3F800000u },
    { 0xC0333333u, 0x3F800000u, 0x40333333u, 0x3F800000u },
    { 0x40333333u, 0x3F800000u, 0xC0333333u, 0x3F800000u },
    { 0xC0333333u, 0x3F800000u, 0xC0333333u, 0x3F800000u },
};

static uint8_t u8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void set8(EmPlayerLiveActor *a, unsigned at, uint8_t v) { em_live_set_u8(a, at, v); }
static uint32_t w32(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void set32(EmPlayerLiveActor *a, unsigned at, uint32_t v) { em_live_set_u32(a, at, v); }
static int16_t h16(const EmPlayerLiveActor *a, unsigned at) { return (int16_t)em_live_u16(a, at); }
static void set16(EmPlayerLiveActor *a, unsigned at, uint16_t v) { em_live_set_u16(a, at, v); }
static float fl(uint32_t bits) { return em_ee_float(bits); }

static void words(const EmPlayerLiveActor *a, unsigned at, uint32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) out[i] = w32(a, at + 4 * i);
}

static void put_words(EmPlayerLiveActor *a, unsigned at, const uint32_t *in, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) set32(a, at + 4 * i, in[i]);
}

int em_player_closure_workers_bound(const EmPlayerClosureWorkers *w)
{
    if (!w || !w->scratch) return 0;
    const void *const need[] = {
        (const void *)w->scene, (const void *)w->node, (const void *)w->hit_surface,
        (const void *)w->set_275B08, (const void *)w->set_810702, (const void *)w->request,
        (const void *)w->sound, (const void *)w->steer, (const void *)w->ledge_move,
        (const void *)w->clip_FF80, (const void *)w->clip_FC80, (const void *)w->clip_DFB0,
        (const void *)w->clip_E0D0, (const void *)w->clip_E150, (const void *)w->clip_E1D0,
        (const void *)w->surface_sound, (const void *)w->sound_109, (const void *)w->land_sound,
        (const void *)w->floor, (const void *)w->place, (const void *)w->pose_reset,
        (const void *)w->skeleton, (const void *)w->translate, (const void *)w->arbiter,
        (const void *)w->use_test, (const void *)w->clip_row, (const void *)w->clip_row_B,
        (const void *)w->ledge_3D, (const void *)w->ledge_3B, (const void *)w->sweep,
        (const void *)w->ground, (const void *)w->point_test, (const void *)w->random,
        (const void *)w->clip_frames, (const void *)w->to_int, (const void *)w->fade,
        (const void *)w->fade_end, (const void *)w->camera, (const void *)w->alloc,
        (const void *)w->trs, (const void *)w->transform, (const void *)w->vadd,
        (const void *)w->sine, (const void *)w->cosine, (const void *)w->wrap,
        (const void *)w->approach,
    };
    for (size_t i = 0; i < sizeof need / sizeof *need; ++i)
        if (!need[i]) return 0;
    return 1;
}

/* ---- worker call shapes ------------------------------------------------- */

typedef struct Call {
    const EmPlayerClosureWorkers *w;
    EmPlayerLiveActor *a;
} Call;

static int request(const Call *c, int clip, int force, uint32_t blend)
{
    return c->w->request(c->w->context, c->a, clip, force, fl(blend));
}

/* 001FBD50(p, id, 0, 300.0): every sound call here passes flags 0 and
 * range 300.0. */
static int sound(const Call *c, int id)
{
    return c->w->sound(c->w->context, c->a, id, 0, fl(K_300));
}

static int scene(const Call *c, EmPlayerClosureScene *s)
{
    memset(s, 0, sizeof *s);
    return c->w->scene(c->w->context, s);
}

static int node(const Call *c, int n, unsigned offset, uint32_t *bits)
{
    return c->w->node(c->w->context, n, offset, bits);
}

static int use_pressed(const Call *c, int *pressed)
{
    EmPlayerClosureScene s;
    FAULT(scene(c, &s));
    *pressed = (s.pad & s.use_mask) != 0;             /* D_00810E74 & spad 0x70003B76 */
    return 0;
}

/* 00188550(p) then 001749A0(p, clip, 0, 16.0) (the row clip). */
static int row_request(const Call *c)
{
    int clip = 0;
    FAULT(c->w->clip_row(c->w->context, c->a, &clip));
    return request(c, clip, 0, K_16);
}

static int gait(const EmPlayerLiveActor *a, unsigned *index)
{
    uint8_t g = u8(a, 0x23F);
    if (g > 3) return -1;                              /* outside D_00248610/20 */
    *index = g;
    return 0;
}

static int wrap(const Call *c, uint32_t x, uint32_t *out)
{
    return c->w->wrap(c->w->context, x, out);
}

static int approach(const Call *c, uint32_t target, uint32_t current, uint32_t rate, uint32_t *out)
{
    return c->w->approach(c->w->context, target, current, rate, out);
}

static int sine(const Call *c, uint32_t x, uint32_t *out)
{
    float r = 0.0f;
    FAULT(c->w->sine(c->w->context, fl(x), &r));
    *out = em_ee_bits(r);
    return 0;
}

static int cosine(const Call *c, uint32_t x, uint32_t *out)
{
    float r = 0.0f;
    FAULT(c->w->cosine(c->w->context, fl(x), &r));
    *out = em_ee_bits(r);
    return 0;
}

static int transform(const Call *c, const uint32_t v[4], uint32_t out[4])
{
    uint32_t m[16], in[4], res[4] = { 0, 0, 0, 0 };
    words(c->a, 0xD0, m, 16);
    memcpy(in, v, sizeof in);
    FAULT(c->w->transform(c->w->context, m, in, res));
    memcpy(out, res, sizeof res);
    return 0;
}

/* build_trs_matrix(p + D0, p + B0, p + C0, p + 60). */
static int trs(const Call *c)
{
    uint32_t out[16], pos[3], rot[3], scale[3];
    words(c->a, 0xB0, pos, 3);
    words(c->a, 0xC0, rot, 3);
    words(c->a, 0x60, scale, 3);
    words(c->a, 0xD0, out, 16);
    FAULT(c->w->trs(c->w->context, out, pos, rot, scale));
    put_words(c->a, 0xD0, out, 16);
    return 0;
}

/* 00102948(p + B0, node1 + C0): the quadword copy of the node's position. */
static int copy_node_position(const Call *c)
{
    uint32_t v[4];
    for (unsigned i = 0; i < 4; ++i) FAULT(node(c, 1, 0xC0 + 4 * i, &v[i]));
    put_words(c->a, 0xB0, v, 4);
    return 0;
}

/* 00102948(dst, src) within the record: a quadword load and store. */
static void copy_qw(EmPlayerLiveActor *a, unsigned dst, unsigned src)
{
    uint32_t v[4];
    words(a, src, v, 4);
    put_words(a, dst, v, 4);
}

/* 001031E0(dst, src) within the record: three words. */
static void copy_xyz(EmPlayerLiveActor *a, unsigned dst, unsigned src)
{
    uint32_t v[3];
    words(a, src, v, 3);
    put_words(a, dst, v, 3);
}

/* ---- 00180000 .. 00180280: the side clip requests -------------------------- */
/* Each tests the side byte +2F1 and passes the caller's $f12 unchanged. */

static int side_clip(const Call *c, int clip0, int clip1, int force, uint32_t blend)
{
    return request(c, u8(c->a, 0x2F1) == 0 ? clip0 : clip1, force, blend);
}

static int sel_clip(const Call *c, int sel, int c00, int c01, int c10, int c11, uint32_t blend)
{
    /* sel != 0 picks the second pair, then the +2F1 byte picks within it; force 0 */
    if (sel == 0) return side_clip(c, c00, c01, 0, blend);
    return side_clip(c, c10, c11, 0, blend);
}

static int x00180000(const Call *c, uint32_t b) { return side_clip(c, 0x99, 0x9B, 1, b); } /* 00180008.. */
static int x00180040(const Call *c, uint32_t b) { return side_clip(c, 0x9C, 0x9A, 1, b); } /* 00180048.. */
static int x00180080(const Call *c, uint32_t b) { return side_clip(c, 0x9D, 0x9F, 1, b); } /* 00180088.. */
static int x001800C0(const Call *c, uint32_t b) { return side_clip(c, 0x9E, 0xA0, 1, b); } /* 001800C8.. */
static int x00180100(const Call *c, int s, uint32_t b) { return sel_clip(c, s, 0xA7, 0xAD, 0xAA, 0xB0, b); }
static int x00180180(const Call *c, int s, uint32_t b) { return sel_clip(c, s, 0xA8, 0xAE, 0xAB, 0xB1, b); }
static int x00180200(const Call *c, int s, uint32_t b) { return sel_clip(c, s, 0xA9, 0xAF, 0xAC, 0xB2, b); }
static int x00180280(const Call *c, int s, uint32_t b) { return sel_clip(c, s, 0xA1, 0xA3, 0xA2, 0xA4, b); }

/* ---- 00180420, 00180300, 001806E0, 00180790, 00180850 ------------------- */

/* 00180420 and 00180300 are translated once each, by the ladder lanes
 * (docs/PLAYER_CLOSURE_0E_18.md "One owner"): 00180420 in
 * em_player_ladder_climb.c, 00180300 in em_player_ladder_entry.c. The
 * bridges below run those translations over this lane's workers and
 * scratch: the same original callees (001026A0, 001028B8, 0019AFE0 and the
 * hit record's surface byte) in the same order, over the same scratchpad
 * words (0x700038A0..0x700038DF, 0x70003600..0x7000361F). The owner works on
 * its own view of those words; the bridge copies them into the view before
 * the call and back into this lane's scratch before every worker call and
 * after the routine, so each worker sees the scratchpad as the original
 * leaves it at that call. */

typedef struct ReachBridge {
    const Call *c;
    EmPlayerLadderClimbScene *scene;     /* 00180420's view of 0x700038A0.. */
} ReachBridge;

static void reach_sync(const ReachBridge *b)
{
    memcpy(b->c->w->scratch->s38A0, b->scene->spad38A0, sizeof b->scene->spad38A0);
}

static int reach_transform(void *context, float out[4], const float matrix[16], const float in[4])
{
    const ReachBridge *b = context;
    uint32_t m[16], v[4], res[4] = { 0, 0, 0, 0 };
    reach_sync(b);
    memcpy(m, matrix, sizeof m);                       /* the words, bit for bit */
    memcpy(v, in, sizeof v);
    FAULT(b->c->w->transform(b->c->w->context, m, v, res));
    memcpy(out, res, sizeof res);
    return 0;
}

/* 00180420: spad 0x700038A0 = (0, 0, -3.0, 1.0), then (tail jump)
 * 001026A0(p + 290, p + D0, 0x700038A0). */
static int x00180420(const Call *c)
{
    EmPlayerLadderClimbWorkers workers;
    EmPlayerLadderClimbScene scene;
    memset(&workers, 0, sizeof workers);
    memset(&scene, 0, sizeof scene);
    memcpy(scene.spad38A0, c->w->scratch->s38A0, sizeof scene.spad38A0);
    ReachBridge bridge = { c, &scene };
    workers.context = &bridge;
    workers.transform = reach_transform;
    EmPlayerLadderClimb ladder = { &workers, &scene };
    int r = em_player_ladder_climb_00180420(&ladder, c->a);
    reach_sync(&bridge);
    return r;
}

typedef struct ProbeBridge {
    const Call *c;
    EmPlayerLadderScratch *scratch;      /* 00180300's view of 0x70003600.. */
} ProbeBridge;

static void probe_sync(const ProbeBridge *b)
{
    memcpy(&b->c->w->scratch->s3600[0], b->scratch->s3600, sizeof b->scratch->s3600);
    memcpy(&b->c->w->scratch->s3600[4], b->scratch->s3610, sizeof b->scratch->s3610);
}

static int probe_apply(void *context, uint32_t out[4], const uint32_t matrix[16],
                       const uint32_t v[4])
{
    const ProbeBridge *b = context;
    uint32_t m[16], in[4], res[4] = { 0, 0, 0, 0 };
    probe_sync(b);
    memcpy(m, matrix, sizeof m);
    memcpy(in, v, sizeof in);
    FAULT(b->c->w->transform(b->c->w->context, m, in, res));
    memcpy(out, res, sizeof res);
    return 0;
}

static int probe_vadd(void *context, uint32_t out[4], const uint32_t a[4], const uint32_t b4[4])
{
    const ProbeBridge *b = context;
    uint32_t x[4], y[4], res[4] = { 0, 0, 0, 0 };
    probe_sync(b);
    memcpy(x, a, sizeof x);                            /* out may be a */
    memcpy(y, b4, sizeof y);
    FAULT(b->c->w->vadd(b->c->w->context, x, y, res));
    memcpy(out, res, sizeof res);
    return 0;
}

/* 0019AFE0 over this lane's sweep. On a hit, the record 0x700031D0 names is
 * the one hit_surface reads: the owner reads its surface byte (+1A) next,
 * with nothing in between, so it is taken here into the owner's record. */
static int probe_sweep(void *context, EmPlayerLiveActor *a, const uint32_t from[4],
                       const uint32_t to[4], int mask, int *result)
{
    const ProbeBridge *b = context;
    const Call *c = b->c;
    probe_sync(b);
    FAULT(c->w->sweep(c->w->context, a, from, to, (unsigned)mask, result));
    b->scratch->record = EM_PLAYER_LADDER_RECORD_NONE;
    if (*result != 0) {
        uint8_t surface = 0;
        FAULT(c->w->hit_surface(c->w->context, &surface));
        b->scratch->record = EM_PLAYER_LADDER_RECORD_OTHER;
        b->scratch->record_bytes[0x1A] = surface;
    }
    return 0;
}

/* 00180300(p, v, kind): the owner's em_player_ladder_probe_00180300. */
static int x00180300(const Call *c, const uint32_t v_in[4], int kind, int *result)
{
    EmPlayerLadderScratch scratch;
    EmPlayerLadderWorkers workers;
    memset(&scratch, 0, sizeof scratch);
    memset(&workers, 0, sizeof workers);
    uint32_t v[4];
    memcpy(v, v_in, sizeof v);                         /* v may point into the scratch */
    memcpy(scratch.s3600, &c->w->scratch->s3600[0], sizeof scratch.s3600);
    memcpy(scratch.s3610, &c->w->scratch->s3600[4], sizeof scratch.s3610);
    ProbeBridge bridge = { c, &scratch };
    workers.context = &bridge;
    workers.scratch = &scratch;
    workers.apply = probe_apply;
    workers.vadd = probe_vadd;
    workers.sweep_0019AFE0 = probe_sweep;
    int r = em_player_ladder_probe_00180300(&workers, c->a, v, kind, result);
    probe_sync(&bridge);
    return r;
}

/* 001806E0: 00180420; spad x = +290, z = +298, y = 18.0 + +B4; 00180300
 * with kind +D. 2 returns 2; else y = 6.0 + node1 +C4 and 00180300 again. */
static int x001806E0(const Call *c, int *result)
{
    uint32_t *s = c->w->scratch->s38A0;
    int r = 0;
    FAULT(x00180420(c));                               /* 001806EC */
    s[0] = w32(c->a, 0x290);                           /* 0018070C */
    s[2] = w32(c->a, 0x298);                           /* 00180718 */
    s[1] = em_ee_add_bits(K_18, w32(c->a, 0xB4));      /* 00180724 / 00180728 */
    FAULT(x00180300(c, s, u8(c->a, 0xD), &r));         /* 00180730 */
    if (r == 2) { *result = 2; return 0; }             /* 0018073C */
    uint32_t y = 0;
    FAULT(node(c, 1, 0xC4, &y));                       /* 00180768 */
    s[1] = em_ee_add_bits(K_6, y);                     /* 0018076C / 00180770 */
    FAULT(x00180300(c, s, u8(c->a, 0xD), &r));         /* 00180778 */
    *result = r;
    return 0;
}

/* 00180790: 00180420; spad = +290 (three words, 001031E0); y -= 4.5;
 * 0019AB20(p, spad, p + 280, 6) nonzero returns 2; else y = node1 +C4 - 6.0
 * and 00180300 != 0 returns 1, else 0. */
static int x00180790(const Call *c, int *result)
{
    uint32_t *s = c->w->scratch->s38A0;
    int r = 0;
    FAULT(x00180420(c));                               /* 0018079C */
    for (unsigned i = 0; i < 3; ++i) s[i] = w32(c->a, 0x290 + 4 * i); /* 001807AC */
    s[1] = em_ee_sub_bits(s[1], K_4_5);                /* 001807CC / 001807E4 */
    uint32_t point[4];
    memcpy(point, s, sizeof point);
    FAULT(c->w->ground(c->w->context, c->a, point, 6, &r)); /* 001807E0 */
    if (r != 0) { *result = 2; return 0; }             /* 001807E8 */
    uint32_t y = 0;
    FAULT(node(c, 1, 0xC4, &y));                       /* 00180814 */
    s[1] = em_ee_sub_bits(y, K_6);                     /* 00180818 / 0018081C */
    FAULT(x00180300(c, s, u8(c->a, 0xD), &r));         /* 00180824 */
    *result = r != 0 ? 1 : 0;                          /* 00180830 movz */
    return 0;
}

/* 00180850(p, flag): 00180420; spad x = +290, z = +298; 0x700038B0 =
 * (flag ? 4.5 : -4.5, 0, 0, 0) x M; then for the two D_002754B0 offsets:
 * y = node1 +C4 + offset, 0x700038C0 = 0x700038B0 + 0x700038A0, 00180300
 * with kind +D; a 0 sets that bit. Both bits set return 0, else 1. */
static int x00180850(const Call *c, int flag, int *result)
{
    uint32_t *s = c->w->scratch->s38A0;
    unsigned mask = 0;
    FAULT(x00180420(c));                               /* 00180870 */
    s[0] = w32(c->a, 0x290);                           /* 00180880 */
    s[2] = w32(c->a, 0x298);                           /* 00180890 (delay slot) */
    s[4] = flag != 0 ? K_4_5 : K_M4_5;                 /* 0018089C / 001808C4 */
    s[5] = K_0;
    s[6] = K_0;
    s[7] = K_0;                                        /* 001808B8 / 001808DC */
    uint32_t out[4];
    FAULT(transform(c, &s[4], out));                   /* 001808F0 */
    memcpy(&s[4], out, sizeof out);
    for (unsigned i = 0; i < 2; ++i) {                 /* 00180900 .. 00180968 */
        uint32_t y = 0, sum[4];
        FAULT(node(c, 1, 0xC4, &y));                   /* 00180928 */
        s[1] = em_ee_add_bits(y, kD002754B0[i]);       /* 0018092C / 00180934 */
        FAULT(c->w->vadd(c->w->context, &s[4], &s[0], sum)); /* 00180930 */
        memcpy(&s[8], sum, sizeof sum);
        int r = 0;
        FAULT(x00180300(c, &s[8], u8(c->a, 0xD), &r)); /* 00180944 */
        if (r == 0) mask |= 1u << i;                   /* 0018094C .. 0018095C */
    }
    *result = mask == 3 ? 0 : 1;                       /* 00180970 .. 00180984 */
    return 0;
}

/* ---- 00182AB0, 00174AB0 --------------------------------------------------- */

/* 00182AB0: 001FBD50(p, 00179B90() + 0x11B, 0, 300.0). */
static int x00182AB0(const Call *c)
{
    int r = 0;
    FAULT(c->w->random(c->w->context, &r));            /* 00182ABC */
    return sound(c, r + 0x11B);                        /* 00182AC4 / 00182AD4 */
}

static int reach_request(void *context, EmPlayerLiveActor *a, int clip, int force, float blend)
{
    const Call *c = context;
    return c->w->request(c->w->context, a, clip, force, blend);
}

/* 00174AB0: (tail jump) 001749A0(p, 0, 1, 0.0); translated once, in
 * em_player_ladder_climb.c (the owner), run here over this lane's request. */
static int x00174AB0(const Call *c)
{
    EmPlayerLadderClimbWorkers workers;
    memset(&workers, 0, sizeof workers);
    workers.context = (void *)c;
    workers.request = reach_request;
    EmPlayerLadderClimb ladder = { &workers, NULL };
    return em_player_ladder_climb_00174AB0(&ladder, c->a);
}

/* ---- 00178620, 001790B0, 00179150 --------------------------------------- */

/* 00178620(p, side): the two points (-+9, 19.5, -6, 1) and (-+9, 19.5, 4, 1)
 * (-9 for side 0) through the +D0 matrix, 0019AFE0(p, near, far, 7). With
 * (result & 6) and hit surface 0x3D, 00178390 != 0 returns 3; with surface
 * 0x3B, 001782A0 != 0 sets +D = 1 and returns 1. Every other path returns 0. */
static int x00178620(const Call *c, int side, int *result)
{
    uint32_t *s = c->w->scratch->s38A0;
    uint32_t x = side != 0 ? K_9 : K_M9;               /* 00178634: side != 0 */
    s[0] = x;                                          /* 00178644 / 0017869C */
    s[4] = x;                                          /* 0017864C / 001786A4 */
    s[1] = K_19_5;
    s[5] = K_19_5;
    s[2] = K_M6;
    s[3] = K_1;
    s[6] = K_4;
    s[7] = K_1;                                        /* 00178690 / 001786E4 */
    uint32_t out[4];
    FAULT(transform(c, &s[0], out));                   /* 001786F8: into 0x700038C0 */
    memcpy(&s[8], out, sizeof out);
    FAULT(transform(c, &s[4], out));                   /* 00178710: into 0x700038D0 */
    memcpy(&s[12], out, sizeof out);
    int hit = 0;
    FAULT(c->w->sweep(c->w->context, c->a, &s[8], &s[12], 7, &hit)); /* 0017872C */
    *result = 0;
    if ((hit & 6) == 0) return 0;                      /* 00178734 / 00178738 */
    uint8_t surface = 0;
    FAULT(c->w->hit_surface(c->w->context, &surface)); /* 00178744 / 0017874C */
    int r = 0;
    if (surface == 0x3D) {                             /* 00178750 */
        FAULT(c->w->ledge_3D(c->w->context, c->a, &r)); /* 00178758 */
        *result = r != 0 ? 3 : 0;                      /* 00178760 / 0017876C */
    } else if (surface == 0x3B) {                      /* 00178774 */
        FAULT(c->w->ledge_3B(c->w->context, c->a, &r)); /* 0017877C */
        if (r != 0) {
            set8(c->a, 0xD, 1);                        /* 0017878C / 00178790 */
            *result = 1;
        }
    }
    return 0;
}

/* 001790B0: +314 = 0; for each D_00248970 point: 001026A0(0x700038A0, p + D0,
 * point) and 0019AD00(p, 0x700038A0, 0x80000007); a nonzero result sets
 * +314 bit i. Returns +314. */
static int x001790B0(const Call *c, int *result)
{
    uint32_t *s = c->w->scratch->s38A0;
    set8(c->a, 0x314, 0);                              /* 001790C8 */
    for (unsigned i = 0; i < 7; ++i) {                 /* 001790D0 .. 00179124 */
        uint32_t out[4];
        FAULT(transform(c, kD00248970[i], out));       /* 001790DC */
        memcpy(s, out, sizeof out);
        int r = 0;
        uint32_t point[4];
        memcpy(point, s, sizeof point);
        FAULT(c->w->point_test(c->w->context, c->a, point, 0x80000007u, &r)); /* 001790F0 */
        if (r != 0) set8(c->a, 0x314, (uint8_t)(u8(c->a, 0x314) | (1u << i))); /* 00179100 .. 00179110 */
    }
    *result = u8(c->a, 0x314);                         /* 00179124 */
    return 0;
}

/* 00179150: +B0 += sin(+C4) * (+38 * cos(+9C)); +B8 += cos(+C4) *
 * (+38 * cos(+9C)) (+9C re-read); then 001790B0. */
static int x00179150(const Call *c)
{
    uint32_t t = 0, s = 0, f20;
    FAULT(cosine(c, w32(c->a, 0x9C), &t));             /* 00179164 */
    f20 = em_ee_mul_bits(w32(c->a, 0x38), t);          /* 00179178 (delay slot) */
    FAULT(sine(c, w32(c->a, 0xC4), &s));               /* 00179174 */
    set32(c->a, 0xB0, em_ee_add_bits(w32(c->a, 0xB0), em_ee_mul_bits(s, f20))); /* 0017917C .. 00179188 */
    FAULT(cosine(c, w32(c->a, 0x9C), &t));             /* 0017918C */
    f20 = em_ee_mul_bits(w32(c->a, 0x38), t);          /* 001791A0 (delay slot) */
    FAULT(cosine(c, w32(c->a, 0xC4), &s));             /* 0017919C */
    set32(c->a, 0xB8, em_ee_add_bits(w32(c->a, 0xB8), em_ee_mul_bits(s, f20))); /* 001791A4 .. 001791B8 */
    int r = 0;
    return x001790B0(c, &r);                           /* 001791B4 */
}

/* ---- 0016BAE0 ------------------------------------------------------------ */

/* 0016BAE0(p, arg): node = 001AFA90(8); when allocated: +3 = 5, +D = arg,
 * +B0..+B8 = the record's, +BC = 1.0, +C0..+C8 = 0, +CC = 1.0, +60..+6C =
 * 1.0 and +10 = 00188340. */
static int x0016BAE0(const Call *c, int arg)
{
    uint8_t *n = NULL;
    FAULT(c->w->alloc(c->w->context, 8, &n));          /* 0016BAF8 */
    if (!n) return 0;                                  /* 0016BB00 */
    const uint32_t one = K_1;
    n[3] = 5;                                          /* 0016BB0C */
    n[0xD] = (uint8_t)arg;                             /* 0016BB10 */
    for (unsigned i = 0; i < 3; ++i) {                 /* 0016BB14 .. 0016BB34 */
        uint32_t v = w32(c->a, 0xB0 + 4 * i);
        memcpy(n + 0xB0 + 4 * i, &v, 4);
    }
    memcpy(n + 0xBC, &one, 4);                         /* 0016BB38 */
    memset(n + 0xC0, 0, 12);                           /* 0016BB3C .. 0016BB44 */
    memcpy(n + 0xCC, &one, 4);                         /* 0016BB48 */
    for (unsigned i = 0; i < 4; ++i) memcpy(n + 0x60 + 4 * i, &one, 4); /* 0016BB4C .. 0016BB58 */
    const uint32_t update = EM_PLAYER_CLOSURE_SPAWN_UPDATE;
    memcpy(n + 0x10, &update, 4);                      /* 0016BB5C */
    return 0;
}

/* ---- 00168050: state 0xE ------------------------------------------------ */

/* The root-motion tail of sub-state 2 in 0xA / 0x14 (001683C0 / 00168590):
 * +2E4 = node0 +4 - +21C; +21C = node0 +4; +B4 += +2E4; then, when +24C is
 * `mode`, +204 = 2.0 / 1.5 / 1.0 for gait 3 / 2 / other. */
static int e_root_motion(const Call *c, int32_t mode)
{
    EmPlayerLiveActor *a = c->a;
    uint32_t y = 0;
    FAULT(node(c, 0, 4, &y));                          /* 001683CC */
    set32(a, 0x2E4, em_ee_sub_bits(y, w32(a, 0x21C))); /* 001683D0 / 001683D4 */
    FAULT(node(c, 0, 4, &y));                          /* 001683E0 */
    set32(a, 0x21C, y);                                /* 001683E4 */
    set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), w32(a, 0x2E4))); /* 001683F0 / 001683F4 */
    if ((int32_t)w32(a, 0x24C) != mode) return 0;      /* 001683FC / 001685D0 */
    uint8_t g = u8(a, 0x23F);                          /* 00168404 */
    if (g == 3) set32(a, 0x204, K_2);                  /* 0016841C */
    else if (g == 2) set32(a, 0x204, UINT32_C(0x3FC00000)); /* 00168430 */
    else set32(a, 0x204, K_1);                         /* 00168440 */
    return 0;
}

/* The three-step sub-state machine of 0xA / 0x14 (+7): 0 requests the side
 * clip at 4.0, 1 waits for the blend, 2 steers and, at the clip end, flips
 * +2F1 and re-tests the reach. */
static int e_climb(const Call *c, int is14)
{
    EmPlayerLiveActor *a = c->a;
    int r = 0;
    /* 00168278 / 00168448: 00181180(p, 1) for 0xA, (p, 0) for 0x14. */
    r = em_player_major2_00181180(a, is14 ? 0 : 1);
    if (r < 0) return -1;
    if (r != 0) return 0;
    uint8_t sub = u8(a, 7);                            /* 00168288 / 00168458 */
    if (sub == 0) {
        set8(a, 7, (uint8_t)(sub + 1));                /* 001682B8 / 00168488 */
        return is14 ? x00180040(c, K_4) : x00180000(c, K_4); /* 001682C4 / 00168494 */
    }
    if (sub == 1) {
        if (!(w32(a, 0x200) & 0x8000u)) set8(a, 7, (uint8_t)(sub + 1)); /* 001682D4.. / 001684A4.. */
        return 0;
    }
    if (sub != 2) return 0;
    FAULT(c->w->steer(c->w->context, a));              /* 001682F4 / 001684C4 */
    if (!(w32(a, 0x200) & 0x1000u))                    /* 001682FC / 001684CC */
        return e_root_motion(c, is14 ? 1 : 0);
    FAULT(x00182AB0(c));                               /* 0016830C / 001684DC */
    set8(a, 0x2F1, (uint8_t)(1 - u8(a, 0x2F1)));       /* 00168314 .. 00168320 */
    int32_t mode = (int32_t)w32(a, 0x24C);             /* 00168324 / 001684F4 */
    if (mode == (is14 ? 1 : 0)) {
        if (!is14) {
            FAULT(x001806E0(c, &r));                   /* 00168330 */
            if (r == 0) {
                FAULT(x00180000(c, K_1));              /* 00168348 */
                set32(a, 0x21C, 0);                    /* 00168354 (b delay slot) */
            } else if (r == 2) {
                set8(a, 6, 0x1E);                      /* 00168368 */
                set8(a, 7, 0);                         /* 00168370 */
                set8(a, 0x1F0, 0x18);                  /* 00168378 */
            } else {
                set8(a, 6, 0);                         /* 0016838C (delay slot) */
                FAULT(c->w->clip_FF80(c->w->context, a, fl(K_8))); /* 00168388 */
            }
        } else {
            FAULT(x00180790(c, &r));                   /* 00168500 */
            if (r == 0) {
                FAULT(x00180040(c, K_1));              /* 00168518 */
                set32(a, 0x21C, 0);                    /* 00168524 */
            } else if (r == 1) {
                set8(a, 6, 0);                         /* 00168544 */
                FAULT(c->w->clip_FF80(c->w->context, a, fl(K_8))); /* 00168540 */
            } else {
                set8(a, 6, 0x28);                      /* 00168554 */
                set8(a, 7, 0);                         /* 0016855C */
                set8(a, 0x1F0, 0x18);                  /* 00168564 */
            }
        }
    } else {
        set8(a, 6, 0);                                 /* 001683A8 / 00168578 */
        FAULT(c->w->clip_FF80(c->w->context, a, fl(K_8))); /* 001683A4 / 00168574 */
    }
    copy_xyz(a, 0x290, 0xB0);                          /* 001683B0 / 00168580: 001031E0 */
    return 0;
}

/* The ledge-shuffle sub-states of 0x32 / 0x3C (+7 0..2). */
static int e_shuffle(const Call *c, int right)
{
    EmPlayerLiveActor *a = c->a;
    int r = em_player_major2_00181180(a, 0);           /* 001689D8 / 00168B50 */
    if (r < 0) return -1;
    if (r != 0) return 0;
    uint8_t sub = u8(a, 7);
    if (sub == 0) {
        set8(a, 7, 1);                                 /* 00168A18 / 00168B90 */
        return right ? x001800C0(c, K_8) : x00180080(c, K_8); /* 00168A24 / 00168B9C */
    }
    if (sub == 1) {
        if (!(w32(a, 0x200) & 0x8000u)) set8(a, 7, 2); /* 00168A34.. / 00168BAC.. */
        return 0;
    }
    if (sub != 2) return 0;
    FAULT(c->w->steer(c->w->context, a));              /* 00168A54 / 00168BCC */
    if ((int32_t)w32(a, 0x24C) != (right ? 3 : 2)) {   /* 00168A64 / 00168BDC */
        set8(a, 6, 0);                                 /* 00168B18 / 00168C8C */
        FAULT(c->w->clip_FF80(c->w->context, a, fl(K_8))); /* 00168B14 / 00168C88 */
        return x00182AB0(c);                           /* 00168B1C / 00168C90 */
    }
    FAULT(x00180850(c, right, &r));                    /* 00168A70 / 00168BE8 */
    if (r == 0) {
        unsigned g = 0;
        FAULT(gait(a, &g));                            /* 00168A80 */
        uint32_t step = kD00248610[g];
        set32(a, 0x38, right ? step : em_ee_neg_bits(step)); /* 00168AA0 / 00168AA4 */
        FAULT(gait(a, &g));                            /* 00168AA8 (re-read) */
        set32(a, 0x204, kD00248620[g]);                /* 00168AB8 */
        uint32_t cs = 0, sn = 0;
        FAULT(cosine(c, w32(a, 0xC4), &cs));           /* 00168ABC */
        set32(a, 0xB0, em_ee_add_bits(w32(a, 0xB0), em_ee_mul_bits(w32(a, 0x38), cs))); /* 00168ACC / 00168AD0 */
        FAULT(sine(c, w32(a, 0xC4), &sn));             /* 00168AD8 */
        set32(a, 0xB8, em_ee_sub_bits(w32(a, 0xB8), em_ee_mul_bits(w32(a, 0x38), sn))); /* 00168AE8 / 00168AEC */
    } else {
        set8(a, 6, right ? 0x50 : 0x46);               /* 00168AFC / 00168C70 */
        set8(a, 7, 0);                                 /* 00168B04 */
    }
    if (w32(a, 0x200) & 0x1000u)                       /* 00168B2C / 00168CA0 */
        return x00182AB0(c);                           /* 00168B3C / 00168CB0 */
    return 0;
}

/* The corner sub-states of 0x46 / 0x50 (+7 0..4); side 0 or 1. */
static int e_corner(const Call *c, int side)
{
    EmPlayerLiveActor *a = c->a;
    int r = em_player_major2_00181180(a, 0);           /* 00168CC4 / 00168E7C */
    if (r < 0) return -1;
    if (r != 0) return 0;
    uint8_t sub = u8(a, 7);
    switch (sub) {
    case 0:
        set8(a, 7, 1);                                 /* 00168D1C / 00168ED4 */
        return x00180100(c, side, K_4);                /* 00168D2C / 00168EE0 */
    case 1:
        if (w32(a, 0x200) & 0x8000u) return 0;         /* 00168D3C / 00168EF0 */
        set8(a, 7, 2);                                 /* 00168D50 / 00168F04 */
        return sound(c, side ? 0x121 : 0x120);         /* 00168D64 / 00168F18 */
    case 2:
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 00168D74 / 00168F28 */
        set8(a, 7, 3);                                 /* 00168D88 / 00168F3C */
        return x00180180(c, side, K_1);                /* 00168D98 / 00168F4C */
    case 3: {
        FAULT(c->w->steer(c->w->context, a));          /* 00168DAC / 00168F60 */
        int pressed = 0;
        FAULT(use_pressed(c, &pressed));               /* 00168DB4 .. 00168DC4 */
        if (pressed) {
            FAULT(c->w->ledge_move(c->w->context, a, side, &r)); /* 00168DD4 / 00168F88 */
            if (r == 0) return 0;
            set8(a, 6, 0x5A);                          /* 00168DE8 */
            set8(a, 7, 0);                             /* 00168DF0 */
            set8(a, 0x1F0, 0x1F);                      /* 00168DF4 */
            return x00180280(c, side, K_1);            /* 00168E04 / 00168FB8 */
        }
        if ((int32_t)w32(a, 0x24C) == (side ? 3 : 2)) return 0; /* 00168E1C / 00168FD0 */
        set8(a, 7, (uint8_t)(u8(a, 7) + 1));           /* 00168E24 / 00168E40 (delay slot) */
        return x00180200(c, side, K_1);                /* 00168E3C / 00168FF0 */
    }
    case 4:
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 00168E4C / 00169000 */
        set8(a, 6, 0);                                 /* 00168E6C (delay slot) */
        return c->w->clip_FF80(c->w->context, a, fl(K_16)); /* 00168E68 / 0016901C */
    default:
        return 0;
    }
}

/* The corner move of 0x5A (00168050) and 0x2B (0016D130): at +3C <= 6.0,
 * +6 + 1, sound 0x187, then the eight-frame step toward the saved target.
 * Returns 1 when it started (the caller then requests its clip). */
static int corner_start(const Call *c, uint8_t st, int *started)
{
    EmPlayerLiveActor *a = c->a;
    *started = 0;
    if (!em_ee_c_le_bits(w32(a, 0x3C), K_6)) return 0; /* 0016903C / 0016DB34 */
    set8(a, 6, (uint8_t)(st + 1));                     /* 00169050 / 0016DB48 */
    FAULT(sound(c, 0x187));                            /* 00169060 / 0016DB58 */
    set32(a, 0x2F4, w32(a, 0x2E0));                    /* 00169078 */
    set32(a, 0x2F8, w32(a, 0x2E8));                    /* 00169080 */
    set32(a, 0x258, w32(a, 0x2E4));                    /* 00169088 */
    set16(a, 0x28, 8);                                 /* 0016908C */
    set32(a, 0x2E0, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x2F4), w32(a, 0xB0)), K_8)); /* 00169098 .. 001690A0 */
    set32(a, 0x2E8, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x2F8), w32(a, 0xB8)), K_8)); /* 001690AC .. 001690B4 */
    set32(a, 0x2E4, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x258), w32(a, 0xB4)), K_8)); /* 001690C0 .. 001690C8 */
    uint32_t t = 0;
    FAULT(wrap(c, em_ee_sub_bits(w32(a, 0x218), w32(a, 0xC4)), &t)); /* 001690D4 / 001690D8 */
    c->w->scratch->s3A20 = t;                          /* 001690F0 (delay slot) */
    if (!em_ee_c_lt_bits(t, K_0))                      /* 001690E4 */
        set32(a, 0x26C, em_ee_div_bits(t, K_8));       /* 00169100 / 00169110 */
    else
        set32(a, 0x26C, em_ee_div_bits(em_ee_neg_bits(t), K_8)); /* 00169114 .. 00169128 */
    *started = 1;
    return 0;
}

/* 0x5B / 0x2C: step +B0/+B8/+B4 and turn +C4 while +28 != 0; at 0 place
 * the actor on the target. Returns 1 in *arrived on the arriving frame. */
static int corner_step(const Call *c, uint8_t st, int *arrived)
{
    EmPlayerLiveActor *a = c->a;
    *arrived = 0;
    if (h16(a, 0x28) == 0) {                           /* 0016914C / 0016DC74 */
        set8(a, 6, (uint8_t)(st + 1));                 /* 0016915C */
        set32(a, 0xB0, w32(a, 0x2F4));                 /* 00169174 */
        set32(a, 0xB8, w32(a, 0x2F8));                 /* 0016917C */
        set32(a, 0xB4, w32(a, 0x258));                 /* 00169184 */
        set32(a, 0xC4, w32(a, 0x218));                 /* 00169190 */
        *arrived = 1;
        return 0;
    }
    set32(a, 0xB0, em_ee_add_bits(w32(a, 0xB0), w32(a, 0x2E0))); /* 001691A4 / 001691A8 */
    set32(a, 0xB8, em_ee_add_bits(w32(a, 0xB8), w32(a, 0x2E8))); /* 001691B4 / 001691B8 */
    set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), w32(a, 0x2E4))); /* 001691C4 / 001691C8 */
    uint32_t yaw = 0;
    FAULT(approach(c, w32(a, 0x218), w32(a, 0xC4), w32(a, 0x26C), &yaw)); /* 001691D4 */
    set32(a, 0xC4, yaw);                               /* 001691DC */
    set16(a, 0x28, (uint16_t)(h16(a, 0x28) - 1));      /* 001691E0 .. 001691EC */
    return 0;
}

/* The drop-down placement shared by 0x21 / 0x2A / 0x2B: 001C68C0, then
 * +B0.. = node1 +C0.. (00102948), +B4 = +B4 - depth, +B4 += -0.2, then
 * 00175900(p, 1). */
static int e_place_on_node(const Call *c, uint32_t depth)
{
    EmPlayerLiveActor *a = c->a;
    int r = 0;
    FAULT(c->w->skeleton(c->w->context, a));           /* 001687B8 / 001688C4 / 00168958 */
    FAULT(copy_node_position(c));                      /* 001687CC / 001688D8 / 0016896C */
    uint32_t y = em_ee_sub_bits(w32(a, 0xB4), depth);  /* 001687E8 */
    set32(a, 0xB4, y);                                 /* 001687FC */
    set32(a, 0xB4, em_ee_add_bits(y, K_M0_2));         /* 00168800 / 00168808 (delay slot) */
    return c->w->floor(c->w->context, a, 1, &r);       /* 00168804 */
}

static int state0E(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    uint8_t st = u8(a, 6);                             /* 0016805C */
    int r = 0;
    switch (st) {
    case 0:
        set8(a, 6, (uint8_t)(st + 1));                 /* 00168158 (delay slot) */
        copy_xyz(a, 0x290, 0xB0);                      /* 00168154: 001031E0 */
        /* fall through */
    case 1: {
        r = em_player_major2_00181180(a, 0);           /* 00168160 */
        if (r < 0) return -1;
        if (r != 0) return 0;
        FAULT(c->w->steer(c->w->context, a));          /* 00168170 */
        int32_t mode = (int32_t)w32(a, 0x24C);         /* 00168178 */
        if (mode == 0) {
            FAULT(x001806E0(c, &r));                   /* 00168184 */
            if (r == 0) set8(a, 6, 0xA);               /* 0016819C */
            else if (r == 2) set8(a, 6, 0x1E);         /* 001681B4 */
        } else if (mode == 1) {
            FAULT(x00180790(c, &r));                   /* 001681C4 */
            if (r == 0) set8(a, 6, 0x14);              /* 001681DC */
            else if (r == 2) set8(a, 6, 0x28);         /* 001681F4 */
        } else if (mode == 2) {
            FAULT(x00180850(c, 0, &r));                /* 00168208 */
            set8(a, 6, r == 0 ? 0x32 : 0x46);          /* 00168220 / 0016822C */
        } else if (mode == 3) {
            FAULT(x00180850(c, 1, &r));                /* 0016823C */
            set8(a, 6, r == 0 ? 0x3C : 0x50);          /* 00168254 / 0016825C */
        }
        set8(a, 7, 0);                                 /* 00168260 */
        set32(a, 0x21C, 0);                            /* 00168264 */
        set32(a, 0x38, 0);                             /* 00168268 */
        set32(a, 0x2E4, 0);                            /* 00168270 (delay slot) */
        return 0;
    }
    case 0xA:
        return e_climb(c, 0);
    case 0x14:
        return e_climb(c, 1);
    case 0x1E:
    case 0x28:
        set8(a, 6, (uint8_t)(st + 1));                 /* 0016861C / 00168854 */
        return request(c, st == 0x1E ? 0xA5 : 0xA6, 0, K_4); /* 0016862C / 00168864 */
    case 0x1F:
    case 0x29:
        if (!(w32(a, 0x200) & 0x8000u)) set8(a, 6, (uint8_t)(st + 1)); /* 0016863C.. / 00168874.. */
        return 0;
    case 0x20: {
        static const uint32_t kLimit[5] = { K_78, K_64, K_42, K_25, K_14 };
        uint8_t sub = u8(a, 7);                        /* 00168658 */
        if (sub > 4) return 0;
        if (!em_ee_c_le_bits(w32(a, 0x3C), kLimit[sub])) return 0; /* 001686AC et al. */
        if (sub == 4) set8(a, 6, (uint8_t)(st + 1));   /* 0016879C (delay slot) */
        else set8(a, 7, (uint8_t)(sub + 1));           /* 001686C4 et al. (delay slot) */
        if (sub < 2) return x00182AB0(c);              /* 001686C0 / 001686F4 */
        return c->w->surface_sound(c->w->context, a, 2); /* 00168728 / 00168760 / 00168798 */
    }
    case 0x21:
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 001687A8 */
        FAULT(e_place_on_node(c, K_10_5));
        FAULT(c->w->surface_sound(c->w->context, a, 2)); /* 00168810 */
        FAULT(c->w->place(c->w->context, a));          /* 00168820 */
        FAULT(x00174AB0(c));                           /* 00168828 */
        FAULT(c->w->pose_reset(c->w->context, a, fl(K_18))); /* 00168838 */
        set8(a, 5, 0);                                 /* 00168840 */
        set8(a, 6, 0);                                 /* 00168844 */
        set8(a, 0x1F0, 0);                             /* 0016884C (delay slot) */
        return 0;
    case 0x2A:
        if (!em_ee_c_le_bits(w32(a, 0x3C), K_22)) return 0; /* 001688A0 */
        set8(a, 6, (uint8_t)(st + 1));                 /* 001688C0 (delay slot) */
        copy_qw(a, 0x290, 0xB0);                       /* 001688BC: 00102948 */
        FAULT(e_place_on_node(c, K_11_5));
        FAULT(c->w->surface_sound(c->w->context, a, 2)); /* 0016891C */
        FAULT(c->w->place(c->w->context, a));          /* 0016892C */
        copy_qw(a, 0xB0, 0x290);                       /* 00168938: 00102948 */
        return 0;
    case 0x2B:
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 00168948 */
        FAULT(e_place_on_node(c, K_11_5));
        FAULT(x00174AB0(c));                           /* 001689AC */
        FAULT(c->w->pose_reset(c->w->context, a, fl(K_18))); /* 001689BC */
        set8(a, 5, 0);                                 /* 001689C4 */
        set8(a, 6, 0);                                 /* 001689C8 */
        set8(a, 0x1F0, 0);                             /* 001689D0 (delay slot) */
        return 0;
    case 0x32:
        return e_shuffle(c, 0);
    case 0x3C:
        return e_shuffle(c, 1);
    case 0x46:
        return e_corner(c, 0);
    case 0x50:
        return e_corner(c, 1);
    case 0x5A: {
        int started = 0;
        FAULT(corner_start(c, st, &started));
        if (!started) return 0;
        return request(c, 0x7A, 0, K_8);               /* 0016913C */
    }
    case 0x5B: {
        int arrived = 0;
        FAULT(corner_step(c, st, &arrived));
        if (arrived) return sound(c, 0xFE);            /* 0016918C */
        return 0;
    }
    case 0x5C:
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 001691F0 */
        set8(a, 5, 9);                                 /* 00169204 */
        set8(a, 6, 0);                                 /* 0016920C */
        set8(a, 0x1F0, 0x10);                          /* 00169210 */
        set8(a, 0xD, 0);                               /* 00169218 (delay slot) */
        return row_request(c);                         /* 00169214 / 0016922C */
    default:
        return 0;
    }
}

/* ---- 0016B790: state 0x13 ------------------------------------------------ */

static int state13(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    uint8_t st = u8(a, 6);                             /* 0016B79C */
    int r = 0;
    switch (st) {
    case 0:
        set8(a, 6, 1);                                 /* 0016B7D8 */
        set8(a, 7, 0);                                 /* 0016B7F0 (delay slot) */
        FAULT(request(c, 0x182, 0, K_8));              /* 0016B7EC */
        return c->w->set_275B08(c->w->context, 0);     /* 0016B7F8 (b delay slot) */
    case 1:
        if (w32(a, 0x200) & 0x8000u) return 0;         /* 0016B7FC */
        set8(a, 6, 2);                                 /* 0016B810 */
        return x0016BAE0(c, 0);                        /* 0016B814 */
    case 2:
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 0016B824 */
        set8(a, 6, 3);                                 /* 0016B838 */
        set8(a, 0x1F0, 0x27);                          /* 0016B840 */
        return request(c, 0x180, 0, K_1);              /* 0016B850 */
    case 3:
        FAULT(c->w->use_test(c->w->context, a, &r));   /* 0016B860 */
        if (r != 0) return 0;                          /* 0016B868 */
        set8(a, 4, 1);                                 /* 0016B874 */
        set8(a, 5, 0x14);                              /* 0016B87C */
        set8(a, 6, 0);                                 /* 0016B884 */
        set8(a, 0x1F0, 0x26);                          /* 0016B888 */
        return 0;
    default:
        return 0;
    }
}

/* ---- 0016B8A0: state 0x14 ------------------------------------------------ */

static int state14(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    uint8_t st = u8(a, 6);                             /* 0016B8AC */
    EmPlayerClosureScene s;
    switch (st) {
    case 0:
        set8(a, 6, 1);                                 /* 0016B900 */
        FAULT(request(c, 0x180, 0, K_1));              /* 0016B910 */
        set32(a, 0xC0, 0);                             /* 0016B91C (b delay slot) */
        return 0;
    case 1:
        FAULT(scene(c, &s));                           /* 0016B920: D_00275B14 */
        if (s.d275B14 == 0x1E) {
            set8(a, 6, 0xA);                           /* 0016B934 (delay slot) */
        } else if (s.d275B14 == 0x34) {
            set8(a, 6, 2);                             /* 0016B948 */
            uint32_t d = 0, target = 0;
            FAULT(wrap(c, em_ee_sub_bits(s.d281B64, w32(a, 0xC4)), &d)); /* 0016B958 / 0016B95C */
            c->w->scratch->s3A20 = d;                  /* 0016B964 */
            uint32_t mag = em_ee_bits(em_sdk_math_original_0011DF78(fl(d))); /* 0016B968 */
            if (em_ee_c_le_bits(mag, K_HALF_PI)) {     /* 0016B980 */
                FAULT(scene(c, &s));                   /* 0016B994 */
                set32(a, 0x218, s.d281B64);            /* 0016B99C (delay slot) */
            } else {
                FAULT(scene(c, &s));                   /* 0016B9A4 */
                FAULT(wrap(c, em_ee_add_bits(K_PI, s.d281B64), &target)); /* 0016B9B4 / 0016B9B8 */
                set32(a, 0x218, target);               /* 0016B9BC */
            }
            FAULT(scene(c, &s));                       /* 0016B9C0 / 0016B9C8 */
            set32(a, 0x2E0, s.d275B10);                /* 0016B9C4 */
            set32(a, 0x2E8, s.d275B0C);                /* 0016B9CC */
        }
        set8(a, 7, 0);                                 /* 0016B9D4 (delay slot) */
        return 0;
    case 2: {
        uint32_t d = 0;
        FAULT(approach(c, w32(a, 0x218), w32(a, 0xC4), K_RATE, &d)); /* 0016B9E8 */
        set32(a, 0xC4, d);                             /* 0016B9F0 */
        if (em_ee_c_eq_bits(d, w32(a, 0x218)))         /* 0016B9F8 */
            set8(a, 6, 0xA);                           /* 0016BA10 (delay slot) */
        return 0;
    }
    case 0xA:
        set8(a, 6, 0xB);                               /* 0016BA18 */
        return request(c, 0x183, 0, K_8);              /* 0016BA28 */
    case 0xB:
        if (w32(a, 0x200) & 0x8000u) return 0;         /* 0016BA38 */
        set8(a, 6, 0xC);                               /* 0016BA4C */
        return c->w->set_275B08(c->w->context, 1);     /* 0016BA58 (delay slot) */
    case 0xC: {
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 0016BA5C */
        if (u8(a, 0x23B) == 0x1E) {                    /* 0016BA74 */
            set8(a, 5, 0x12);                          /* 0016BA80 */
            set8(a, 6, 0);                             /* 0016BA88 */
            set8(a, 0x1F0, 0x22);                      /* 0016BA90 */
        } else {
            set8(a, 5, 0x10);                          /* 0016BA98 */
            set8(a, 6, 0);                             /* 0016BAA0 */
            set8(a, 0x1F0, 0x21);                      /* 0016BAA4 */
        }
        set8(a, 0x2F1, 0);                             /* 0016BAB0 (delay slot) */
        int clip = 0;
        FAULT(c->w->clip_row_B(c->w->context, a, &clip)); /* 0016BAAC */
        return request(c, clip, 0, K_16);              /* 0016BAC4 */
    }
    default:
        return 0;
    }
}

/* ---- 0016D130: state 0x18 ------------------------------------------------ */

/* +28 = float_to_int(r) with r first stored at 0x70003A20. */
static int d_frames(const Call *c, uint32_t r)
{
    int32_t n = 0;
    c->w->scratch->s3A20 = r;                          /* 0016D460 / 0016D918 (delay slot) */
    FAULT(c->w->to_int(c->w->context, r, &n));         /* 0016D45C / 0016D914 */
    set16(c->a, 0x28, (uint16_t)n);                    /* 0016D464 / 0016D920 */
    return 0;
}

static int d_state0(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    set32(a, 0x38, 0);                                 /* 0016D240 */
    uint8_t d = u8(a, 0xD);                            /* 0016D244 */
    if (d == 0) {
        set8(a, 6, 3);                                 /* 0016D25C */
        FAULT(request(c, 0x152, 0, K_8));              /* 0016D260 */
        set16(a, 0x28, 0x50);                          /* 0016D26C */
        FAULT(sound(c, 0x122));                        /* 0016D280 */
    } else if (d == 1) {
        set8(a, 6, (uint8_t)(u8(a, 6) + 1));           /* 0016D298 / 0016D2B4 (delay slot) */
        FAULT(request(c, 0x70, 0, K_8));               /* 0016D2B0 */
    } else if (d == 2) {
        if (em_player_hang_0017F240(a, 0) != 0) return 0; /* 0016D2C8 / 0016D2D0 */
        set8(a, 0x1F1, 0);                             /* 0016D2D8 */
        EmPlayerClosureScene s;
        FAULT(scene(c, &s));                           /* 0016D2E0: D_00810700 */
        if (s.area == 0) {
            set8(a, 6, 0x14);                          /* 0016D2F4 */
        } else {
            FAULT(c->w->steer(c->w->context, a));      /* 0016D2FC */
            int32_t k = (int32_t)w32(a, 0x24C);        /* 0016D304 */
            if (k == 0) {
                set8(a, 6, 0xA);                       /* 0016D314 */
                set8(a, 0x1F1, 1);                     /* 0016D320 */
            } else if (k == 1) {
                int pressed = 0;
                FAULT(use_pressed(c, &pressed));       /* 0016D330 .. 0016D340 */
                if (pressed) set8(a, 6, 0x14);         /* 0016D354 */
            } else if (k == 2) {
                set8(a, 0x2F1, 0);                     /* 0016D368 */
                set8(a, 6, 0x28);                      /* 0016D370 */
            } else if (k == 3) {
                set8(a, 0x2F1, 1);                     /* 0016D384 */
                set8(a, 6, 0x28);                      /* 0016D38C */
            }
        }
    } else if (d == 3) {
        set8(a, 6, 0x1E);                              /* 0016D3A4 */
        FAULT(request(c, 0x70, 0, K_8));               /* 0016D3A8 */
    }
    set8(a, 7, 0);                                     /* 0016D3B0 / 0016D28C */
    return 0;
}

static int state18(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    uint8_t st = u8(a, 6);                             /* 0016D13C */
    EmPlayerClosureScene s;
    int16_t t;
    switch (st) {
    case 0:
        return d_state0(c);
    case 1: {
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 0016D3BC */
        set8(a, 6, 2);                                 /* 0016D3D0 */
        FAULT(c->w->land_sound(c->w->context, a, 0));  /* 0016D3D4 */
        int32_t frames = 0;
        FAULT(c->w->clip_frames(c->w->context, w32(a, 0x40), 0x79, &frames)); /* 0016D3DC / 0016D3E0 */
        c->w->scratch->s3A20 = em_ee_cvt_s_w_bits(em_ee_int_word(frames)); /* 0016D3F0 / 0016D404 */
        uint32_t f = c->w->scratch->s3A20;             /* 0016D40C */
        FAULT(c->w->arbiter(c->w->context, a, 0x79, fl(K_2), fl(em_ee_sub_bits(f, K_53)))); /* 0016D418 / 0016D41C */
        set32(a, 0x258, em_ee_sub_bits(w32(a, 0x254),
                                       em_ee_add_bits(w32(a, 0xB4), kD002488AC))); /* 0016D438 .. 0016D440 */
        set32(a, 0x2E4, K_0_6);                        /* 0016D444 */
        FAULT(d_frames(c, em_ee_div_bits(w32(a, 0x258), w32(a, 0x2E4)))); /* 0016D450 */
        set32(a, 0x254, em_ee_sub_bits(w32(a, 0x254), kD002488AC)); /* 0016D484 / 0016D490 (delay slot) */
        return sound(c, 0x12C);                        /* 0016D48C */
    }
    case 2:
        t = h16(a, 0x28);                              /* 0016D49C */
        set16(a, 0x28, (uint16_t)(t - 1));             /* 0016D4A8 (delay slot) */
        if (t == 0) {
            set8(a, 6, (uint8_t)(u8(a, 6) + 1));       /* 0016D4AC .. 0016D4B8 */
            set32(a, 0xB4, w32(a, 0x254));             /* 0016D4C0 */
            set16(a, 0x28, 0x18);                      /* 0016D4C8 (delay slot) */
        } else {
            set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), w32(a, 0x2E4))); /* 0016D4D4 / 0016D4DC */
        }
        return 0;
    case 3: {
        t = h16(a, 0x28);                              /* 0016D4E0 */
        set16(a, 0x28, (uint16_t)(t - 1));             /* 0016D4EC (delay slot) */
        if (t != 0) return 0;
        set8(a, 6, (uint8_t)(u8(a, 6) + 1));           /* 0016D4F0 .. 0016D4FC (delay slot) */
        int r = 0;
        FAULT(c->w->random(c->w->context, &r));        /* 0016D4F8 */
        FAULT(sound(c, r + 0x13F));                    /* 0016D500 / 0016D510 */
        return c->w->fade(c->w->context, 4, 0);        /* 0016D51C */
    }
    case 4:
        FAULT(scene(c, &s));                           /* 0016D530: D_0028A9A0 */
        if (s.fade != 2) { set32(a, 0x204, 0); return 0; } /* 0016D610 */
        set8(a, 5, 0x19);                              /* 0016D544 */
        set8(a, 6, 0);                                 /* 0016D548 */
        set8(a, 0x1F0, 0x2D);                          /* 0016D54C */
        if (u8(a, 0xD) == 1) {                         /* 0016D550 / 0016D558 */
            uint32_t v = 0, sn = 0, cs = 0;
            FAULT(node(c, 1, 0xC4, &v));               /* 0016D568 */
            set32(a, 0xB4, v);                         /* 0016D56C */
            FAULT(sine(c, w32(a, 0xC4), &sn));         /* 0016D570 */
            set32(a, 0xB0, em_ee_add_bits(w32(a, 0xB0), em_ee_mul_bits(K_7_5, sn))); /* 0016D584 .. 0016D58C */
            FAULT(cosine(c, w32(a, 0xC4), &cs));       /* 0016D590 */
            set32(a, 0xB8, em_ee_add_bits(w32(a, 0xB8), em_ee_mul_bits(K_7_5, cs))); /* 0016D5A4 .. 0016D5C0 */
            FAULT(trs(c));                             /* 0016D5BC */
            FAULT(x00179150(c));                       /* 0016D5C4 */
        } else {
            uint32_t v = 0;
            FAULT(node(c, 1, 0xC0, &v));               /* 0016D5DC */
            set32(a, 0xB0, v);                         /* 0016D5E0 */
            FAULT(node(c, 1, 0xC8, &v));               /* 0016D5EC */
            set32(a, 0xB8, v);                         /* 0016D5F0 */
        }
        set8(a, 1, 0);                                 /* 0016D600 (delay slot) */
        return c->w->fade_end(c->w->context, 4, 0);    /* 0016D5FC */
    case 0xA:
        set8(a, 6, 0xB);                               /* 0016D618 */
        return request(c, 0x7C, 0, K_5);               /* 0016D628 */
    case 0xB:
        if (w32(a, 0x200) & 0x8000u) return 0;         /* 0016D638 */
        set8(a, 6, 0xC);                               /* 0016D64C */
        return sound(c, 0x12C);                        /* 0016D65C */
    case 0xC:
        if (!em_ee_c_le_bits(w32(a, 0x3C), K_18)) return 0; /* 0016D67C */
        set8(a, 6, 0xD);                               /* 0016D694 */
        return c->w->fade(c->w->context, 4, 0);        /* 0016D698 */
    case 0xD:
        FAULT(scene(c, &s));                           /* 0016D6A8: D_0028A9A0 */
        if (s.fade != 2) { set32(a, 0x204, 0); return 0; } /* 0016D7C0 */
        FAULT(scene(c, &s));                           /* 0016D6BC: D_00810700 */
        if (s.area == 0) {
            set8(a, 5, 0x1A);                          /* 0016D6D0 */
            set8(a, 6, 0);                             /* 0016D6D8 */
            set8(a, 0x1F0, 0x2E);                      /* 0016D6DC */
            set8(a, 0xD, 0);                           /* 0016D6E0 */
            set32(a, 0xB0, K_186);                     /* 0016D6E8 */
            set32(a, 0xB4, K_M19_5);                   /* 0016D6F0 */
            set32(a, 0xB8, K_M1463_5);                 /* 0016D6FC */
            set32(a, 0xC4, K_PI);                      /* 0016D708 */
            FAULT(c->w->set_810702(c->w->context, 0xA)); /* 0016D71C (delay slot) */
            FAULT(c->w->camera(c->w->context, 1));     /* 0016D718 */
        } else {
            set8(a, 5, 0x19);                          /* 0016D72C */
            set8(a, 6, 0);                             /* 0016D730 */
            set8(a, 0x1F0, 0x2D);                      /* 0016D734 */
            set8(a, 1, 0);                             /* 0016D738 */
            set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), K_20_5)); /* 0016D750 / 0016D760 (delay slot) */
            FAULT(trs(c));                             /* 0016D75C */
            uint32_t *sp = c->w->scratch->s38A0;
            sp[0] = K_0;                               /* 0016D768 */
            sp[1] = K_1;                               /* 0016D774 */
            sp[2] = K_5;                               /* 0016D780 */
            sp[3] = K_1;                               /* 0016D79C (delay slot) */
            uint32_t out[4];
            FAULT(transform(c, sp, out));              /* 0016D798: out = p + B0 */
            put_words(a, 0xB0, out, 4);
            FAULT(x00179150(c));                       /* 0016D7A0 */
        }
        return c->w->fade_end(c->w->context, 4, 0);    /* 0016D7AC */
    case 0x14:
        set8(a, 6, 0x15);                              /* 0016D7C8 */
        FAULT(request(c, 0x80, 0, K_4));               /* 0016D7D8 */
        set32(a, 0x21C, 0);                            /* 0016D7E0 */
        set32(a, 0x38, 0);                             /* 0016D7E4 */
        set32(a, 0x2E4, 0);                            /* 0016D7E8 */
        set32(a, 0x2F4, w32(a, 0xB4));                 /* 0016D7EC / 0016D7F4 (delay slot) */
        return 0;
    case 0x15: {
        uint32_t fl200 = w32(a, 0x200);                /* 0016D7F8 */
        if (fl200 & 0x1000u) {
            set8(a, 5, 7);                             /* 0016D80C */
            set8(a, 6, 0);                             /* 0016D814 */
            set8(a, 0x1F0, 0xD);                       /* 0016D81C (delay slot) */
            return 0;
        }
        if (fl200 & 0x8000u) return 0;                 /* 0016D824 */
        uint32_t v = 0;
        FAULT(node(c, 0, 8, &v));                      /* 0016D83C */
        set32(a, 0x38, em_ee_sub_bits(v, w32(a, 0x21C))); /* 0016D840 / 0016D844 */
        FAULT(node(c, 0, 8, &v));                      /* 0016D850 */
        set32(a, 0x21C, v);                            /* 0016D858 (delay slot) */
        FAULT(c->w->translate(c->w->context, a, 1));   /* 0016D854 */
        FAULT(node(c, 0, 4, &v));                      /* 0016D868 */
        set32(a, 0x2EC, em_ee_sub_bits(v, w32(a, 0x2E4))); /* 0016D86C / 0016D870 */
        FAULT(node(c, 0, 4, &v));                      /* 0016D87C */
        set32(a, 0x2E4, v);                            /* 0016D880 */
        set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), w32(a, 0x2EC))); /* 0016D88C / 0016D894 */
        return 0;
    }
    case 0x1E:
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 0016D898 */
        set8(a, 6, 0x1F);                              /* 0016D8AC */
        FAULT(request(c, 0x71, 0, K_1));               /* 0016D8BC */
        set8(a, 0x25F, 1);                             /* 0016D8C8 */
        FAULT(c->w->land_sound(c->w->context, a, 0));  /* 0016D8D0 */
        set32(a, 0x254, em_ee_sub_bits(w32(a, 0x254), K_20_5)); /* 0016D8EC / 0016D8F0 */
        set32(a, 0x2E4, K_0_6);                        /* 0016D8F4 */
        return d_frames(c, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x254), w32(a, 0xB4)),
                                          w32(a, 0x2E4))); /* 0016D904 / 0016D908 */
    case 0x1F:
        t = h16(a, 0x28);                              /* 0016D924 */
        set16(a, 0x28, (uint16_t)(t - 1));             /* 0016D930 (delay slot) */
        if (t == 0) {
            set8(a, 6, (uint8_t)(u8(a, 6) + 1));       /* 0016D934 .. 0016D94C */
            set32(a, 0xB4, w32(a, 0x254));             /* 0016D958 (delay slot) */
            return request(c, 0x7A, 0, K_1);           /* 0016D954 */
        }
        set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), w32(a, 0x2E4))); /* 0016D96C / 0016D974 */
        return 0;
    case 0x20:
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 0016D978 */
        set8(a, 6, 0);                                 /* 0016D988 */
        set8(a, 0x1F0, 0x2C);                          /* 0016D98C */
        set8(a, 0x1F1, 0);                             /* 0016D994 */
        set8(a, 0xD, 2);                               /* 0016D99C (delay slot) */
        return row_request(c);                         /* 0016D998 / 0016D9B0 */
    case 0x28:
        if (em_player_hang_0017F240(a, 0) != 0) return 0; /* 0016D9C4 */
        set8(a, 6, (uint8_t)(u8(a, 6) + 1));           /* 0016D9D4 .. 0016D9E4 */
        return c->w->clip_DFB0(c->w->context, a, u8(a, 0x2F1), fl(K_8)); /* 0016D9E8 / 0016D9EC */
    case 0x29:
        if (em_player_hang_0017F240(a, 0) != 0) return 0; /* 0016DA00 */
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 0016DA10 */
        set8(a, 6, (uint8_t)(u8(a, 6) + 1));           /* 0016DA20 .. 0016DA30 */
        return c->w->clip_E0D0(c->w->context, a, u8(a, 0x2F1), fl(K_1)); /* 0016DA34 / 0016DA38 */
    case 0x2A: {
        if (em_player_hang_0017F240(a, 0) != 0) return 0; /* 0016DA4C */
        FAULT(c->w->steer(c->w->context, a));          /* 0016DA5C */
        uint8_t side = u8(a, 0x2F1);                   /* 0016DA64 */
        if (side > 1) return -1;                       /* outside D_00275498 */
        int32_t m = (int32_t)w32(a, 0x24C);            /* 0016DA6C */
        if (m != kD00275498[side]) {                   /* 0016DA7C */
            set8(a, 6, 0x31);                          /* 0016DA88 */
            return c->w->clip_E150(c->w->context, a, u8(a, 0x2F1), fl(K_1)); /* 0016DA8C / 0016DA98 */
        }
        int pressed = 0;
        FAULT(use_pressed(c, &pressed));               /* 0016DAA8 .. 0016DAB8 */
        if (!pressed) return 0;
        int r = 0;
        FAULT(x00178620(c, side, &r));                 /* 0016DAC4 / 0016DAC8 */
        set8(a, 0x1F1, (uint8_t)r);                    /* 0016DAD0 */
        uint8_t tv = u8(a, 0x1F1);                     /* 0016DAD4 */
        if (tv != 1 && tv != 3) return 0;              /* 0016DADC / 0016DAE8 */
        set8(a, 6, (uint8_t)(u8(a, 6) + 1));           /* 0016DAF0 .. 0016DB04 */
        set8(a, 0xD, 4);                               /* 0016DB08 */
        FAULT(c->w->clip_E1D0(c->w->context, a, u8(a, 0x2F1), fl(K_1))); /* 0016DB0C / 0016DB10 */
        set8(a, 0x1F0, 0x12);                          /* 0016DB20 (b delay slot) */
        return 0;
    }
    case 0x2B: {
        int started = 0;
        FAULT(corner_start(c, st, &started));
        if (!started) return 0;
        return request(c, u8(a, 0x1F1) == 1 ? 0xE5 : 0x7A, 0, K_8); /* 0016DC24 .. 0016DC64 */
    }
    case 0x2C: {
        int arrived = 0;
        FAULT(corner_step(c, st, &arrived));
        if (!arrived) return 0;
        if (u8(a, 0x1F1) == 1) return c->w->sound_109(c->w->context, a); /* 0016DCAC / 0016DCB8 */
        return sound(c, 0xFF);                         /* 0016DCD4 */
    }
    case 0x2D: {
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 0016DD38 */
        uint8_t d = u8(a, 0x1F1);                      /* 0016DD48 */
        if (d == 1) {
            FAULT(c->w->sound_109(c->w->context, a));  /* 0016DD58 */
            set8(a, 5, 0xC);                           /* 0016DD64 */
            set8(a, 6, 0);                             /* 0016DD6C */
            set8(a, 0x1F0, 0x17);                      /* 0016DD70 */
            set8(a, 0xD, 1);                           /* 0016DD78 */
            set8(a, 0x2F1, 0);                         /* 0016DD8C (delay slot) */
            return c->w->clip_FC80(c->w->context, a, fl(K_16)); /* 0016DD88 */
        }
        if (d == 3) {
            set8(a, 5, 9);                             /* 0016DDA8 */
            set8(a, 6, 0);                             /* 0016DDB0 */
            set8(a, 0x1F0, 0x10);                      /* 0016DDB4 */
            set8(a, 0xD, 1);                           /* 0016DDBC (delay slot) */
            return row_request(c);                     /* 0016DDB8 / 0016DDD0 */
        }
        return 0;
    }
    case 0x31:
        if (em_player_hang_0017F240(a, 0) != 0) return 0; /* 0016DDE0 */
        if (!(w32(a, 0x200) & 0x1000u)) return 0;      /* 0016DDF0 */
        set8(a, 6, 0);                                 /* 0016DE08 (delay slot) */
        return row_request(c);                         /* 0016DE04 / 0016DE1C */
    default:
        return 0;
    }
}

/* ---- entry points --------------------------------------------------------- */

static int enter(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, Call *c)
{
    if (!a || !em_player_closure_workers_bound(w)) return -1;
    c->w = w;
    c->a = a;
    return 0;
}

int em_player_closure_state0E(void *workers, EmPlayerLiveActor *actor)
{
    Call c;
    FAULT(enter(workers, actor, &c));
    return state0E(&c) < 0 ? -1 : 0;
}

int em_player_closure_state13(void *workers, EmPlayerLiveActor *actor)
{
    Call c;
    FAULT(enter(workers, actor, &c));
    return state13(&c) < 0 ? -1 : 0;
}

int em_player_closure_state14(void *workers, EmPlayerLiveActor *actor)
{
    Call c;
    FAULT(enter(workers, actor, &c));
    return state14(&c) < 0 ? -1 : 0;
}

int em_player_closure_state18(void *workers, EmPlayerLiveActor *actor)
{
    Call c;
    FAULT(enter(workers, actor, &c));
    return state18(&c) < 0 ? -1 : 0;
}

#define ENTRY(name, body) \
    { Call c; FAULT(enter(w, a, &c)); return (body) < 0 ? -1 : 0; }

int em_player_closure_00180000(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, uint32_t b)
ENTRY(00180000, x00180000(&c, b))
int em_player_closure_00180040(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, uint32_t b)
ENTRY(00180040, x00180040(&c, b))
int em_player_closure_00180080(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, uint32_t b)
ENTRY(00180080, x00180080(&c, b))
int em_player_closure_001800C0(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, uint32_t b)
ENTRY(001800C0, x001800C0(&c, b))
int em_player_closure_00180100(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int s, uint32_t b)
ENTRY(00180100, x00180100(&c, s, b))
int em_player_closure_00180180(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int s, uint32_t b)
ENTRY(00180180, x00180180(&c, s, b))
int em_player_closure_00180200(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int s, uint32_t b)
ENTRY(00180200, x00180200(&c, s, b))
int em_player_closure_00180280(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int s, uint32_t b)
ENTRY(00180280, x00180280(&c, s, b))
int em_player_closure_00180420(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a)
ENTRY(00180420, x00180420(&c))
int em_player_closure_00180300(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a,
                               const uint32_t v[4], int kind, int *result)
ENTRY(00180300, x00180300(&c, v, kind, result))
int em_player_closure_001806E0(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int *result)
ENTRY(001806E0, x001806E0(&c, result))
int em_player_closure_00180790(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int *result)
ENTRY(00180790, x00180790(&c, result))
int em_player_closure_00180850(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int flag,
                               int *result)
ENTRY(00180850, x00180850(&c, flag, result))
int em_player_closure_00182AB0(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a)
ENTRY(00182AB0, x00182AB0(&c))
int em_player_closure_00174AB0(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a)
ENTRY(00174AB0, x00174AB0(&c))
int em_player_closure_00178620(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int side,
                               int *result)
ENTRY(00178620, x00178620(&c, side, result))
int em_player_closure_00179150(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a)
ENTRY(00179150, x00179150(&c))
int em_player_closure_001790B0(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int *result)
ENTRY(001790B0, x001790B0(&c, result))
int em_player_closure_0016BAE0(const EmPlayerClosureWorkers *w, EmPlayerLiveActor *a, int arg)
ENTRY(0016BAE0, x0016BAE0(&c, arg))
