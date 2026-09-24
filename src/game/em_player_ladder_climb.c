/* em_player_ladder_climb.c - the player's ladder climb, state +5 = 0xC (see
 * em_player_ladder_climb.h).
 *
 * Read from the original instructions of 001662D0 and 001809B0 (the decomp's
 * C for both is a NEARMISS; docs/PLAYER_LADDER_CLIMB.md lists what the
 * instructions settle) and from the byte-matched decomp C of 0017FC80,
 * 0017FD00..0017FF00, 00180420, 00180460, 00180530, 00180600, 00174AB0,
 * 001885D0 and 001885F0, checked against their instructions. Every comment
 * address is the original instruction the line translates. EE COP1
 * arithmetic goes through em_ee_float.h on raw binary32 bits; loads and
 * stores of floats the original only moves (lwc1/swc1, lw/sw, mov.s) copy
 * the bits. */
#include "game/em_player_ladder_climb.h"
#include "game/em_player_major2.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)
/* A worker's status as this file's: 0, or -1 for any negative value. */
#define STATUS(expr) ((expr) < 0 ? -1 : 0)

/* Binary32 constants as the original materializes them (lui/ori). */
#define K_0       UINT32_C(0x00000000)
#define K_0_5     UINT32_C(0x3F000000)
#define K_1       UINT32_C(0x3F800000)
#define K_1_5     UINT32_C(0x3FC00000)
#define K_2       UINT32_C(0x40000000)
#define K_3       UINT32_C(0x40400000)
#define K_4       UINT32_C(0x40800000)
#define K_4_5     UINT32_C(0x40900000)
#define K_5       UINT32_C(0x40A00000)
#define K_6       UINT32_C(0x40C00000)
#define K_7_2     UINT32_C(0x40E66666)
#define K_8       UINT32_C(0x41000000)
#define K_9       UINT32_C(0x41100000)
#define K_10      UINT32_C(0x41200000)
#define K_10_5    UINT32_C(0x41280000)
#define K_11_5    UINT32_C(0x41380000)
#define K_12      UINT32_C(0x41400000)
#define K_16      UINT32_C(0x41800000)
#define K_16_8    UINT32_C(0x41866666)
#define K_18      UINT32_C(0x41900000)
#define K_19      UINT32_C(0x41980000)
#define K_20      UINT32_C(0x41A00000)
#define K_20_5    UINT32_C(0x41A40000)
#define K_22      UINT32_C(0x41B00000)
#define K_30      UINT32_C(0x41F00000)
#define K_48      UINT32_C(0x42400000)
#define K_64      UINT32_C(0x42800000)
#define K_78      UINT32_C(0x429C0000)
#define K_120     UINT32_C(0x42F00000)
#define K_130     UINT32_C(0x43020000)
#define K_250     UINT32_C(0x437A0000)
#define K_260     UINT32_C(0x43820000)
#define K_300     UINT32_C(0x43960000)
#define K_560     UINT32_C(0x440C0000)
#define K_156_4   UINT32_C(0x431C6666)
#define K_M0_2    UINT32_C(0xBE4CCCCD)
#define K_M0_4    UINT32_C(0xBECCCCCD)
#define K_M1_5    UINT32_C(0xBFC00000)
#define K_M2      UINT32_C(0xC0000000)
#define K_M3      UINT32_C(0xC0400000)
#define K_M5      UINT32_C(0xC0A00000)
#define K_M6      UINT32_C(0xC0C00000)

/* D_002754D0 / D_002754D4 (.sdata halfwords, read by 001885D0 / 001885F0 and
 * directly by sub-state 0x2C); the oracle checks them against the ELF. */
static const int16_t kD002754D0[2] = { 0xE6, 0x100 };
static const int16_t kD002754D4[2] = { 0xE7, 0x101 };

int16_t em_player_ladder_climb_d2754D0(unsigned row) { return kD002754D0[row & 1]; }

/* The scratchpad vectors, as word indexes into EmPlayerLadderClimbScene.spad38A0. */
enum { SP_A0 = 0, SP_B0 = 4, SP_C0 = 8, SP_D0 = 12 };

typedef struct Call {
    const EmPlayerLadderClimbWorkers *w;
    EmPlayerLadderClimbScene *s;
    EmPlayerLiveActor *a;
} Call;

static uint8_t u8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void set8(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u8(a, at, (uint8_t)v); }
static uint32_t w32(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void set32(EmPlayerLiveActor *a, unsigned at, uint32_t v) { em_live_set_u32(a, at, v); }
static int16_t h16(const EmPlayerLiveActor *a, unsigned at) { return (int16_t)em_live_u16(a, at); }
static void set16(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u16(a, at, (uint16_t)v); }
static float fl(uint32_t bits) { return em_ee_float(bits); }

static uint32_t sp(const Call *c, unsigned index)
{
    uint32_t v;
    memcpy(&v, &c->s->spad38A0[index], 4);
    return v;
}
static void set_sp(const Call *c, unsigned index, uint32_t v) { memcpy(&c->s->spad38A0[index], &v, 4); }
static uint32_t sp3A(const Call *c, unsigned index)
{
    uint32_t v;
    memcpy(&v, &c->s->spad3A20[index], 4);
    return v;
}
static void set_sp3A(const Call *c, unsigned index, uint32_t v) { memcpy(&c->s->spad3A20[index], &v, 4); }

/* 001031E0(dst, src): three lwc1/swc1 word copies (x, y, z). */
static void copy3_actor(EmPlayerLiveActor *a, unsigned dst, unsigned src)
{
    for (unsigned i = 0; i < 3; i++) set32(a, dst + 4 * i, w32(a, src + 4 * i));
}
static void copy3_to_spad(const Call *c, unsigned dst, unsigned src)
{
    for (unsigned i = 0; i < 3; i++) set_sp(c, dst + i, w32(c->a, src + 4 * i));
}

static int node_word(const Call *c, int node, unsigned offset, uint32_t *bits)
{
    return c->w->node(c->w->context, node, offset, bits);
}

static int request(const Call *c, int clip, int flags, uint32_t blend)
{
    return STATUS(c->w->request(c->w->context, c->a, clip, flags, fl(blend)));
}

static int sound(const Call *c, int id)
{
    return STATUS(c->w->sound(c->w->context, c->a, id, 0, fl(K_300)));
}

/* 001026A0(spad out, p + 0xD0, spad in). */
static int transform_spad(const Call *c, unsigned out, unsigned in)
{
    float matrix[16];
    memcpy(matrix, c->a->bytes + 0xD0, sizeof matrix);
    return c->w->transform(c->w->context, &c->s->spad38A0[out], matrix, &c->s->spad38A0[in]);
}

static int hit_kind(const Call *c, int *kind)
{
    return c->w->hit_kind(c->w->context, kind);
}

/* ---- the clip helpers ---------------------------------------------------- */

static int bound_request(const EmPlayerLadderClimb *l, const EmPlayerLiveActor *a)
{
    return l && a && l->workers && l->workers->request;
}

/* 0017FC80(p, blend): 001749A0(p, D_002754D0[+235 & 1] or (+2F1 != 0)
 * D_002754D4[+235 & 1], 0, blend). */
static int clip_FC80(const Call *c, uint32_t blend)
{
    unsigned row = u8(c->a, 0x235) & 1u;             /* 001885D0 / 001885F0 */
    int clip = u8(c->a, 0x2F1) == 0 ? kD002754D0[row] : kD002754D4[row];   /* 0017FC90 / 0017FC98 */
    return request(c, clip, 0, blend);               /* 0017FCB4 / 0017FCD8 */
}

/* 0017FD00 / 0017FD40: one clip pair chosen by +2F1 (0 -> first). */
static int clip_pair(const Call *c, int zero, int other, uint32_t blend)
{
    return request(c, u8(c->a, 0x2F1) == 0 ? zero : other, 0, blend);
}

/* 0017FD80 / 0017FE00 / 0017FE80 / 0017FF00: side == 0 picks the first
 * pair, then +2F1 picks within the pair (0 -> first). */
static int clip_side(const Call *c, int side, const int clips[4], uint32_t blend)
{
    const int *pair = side == 0 ? clips : clips + 2;
    return request(c, u8(c->a, 0x2F1) == 0 ? pair[0] : pair[1], 0, blend);
}

static const int kClipsFD80[4] = { 0xF2, 0xF8, 0xF5, 0xFB };
static const int kClipsFE00[4] = { 0xF3, 0xF9, 0xF6, 0xFC };
static const int kClipsFE80[4] = { 0xF4, 0xFA, 0xF7, 0xFD };
static const int kClipsFF00[4] = { 0xEC, 0xEE, 0xED, 0xEF };

static Call helper_call(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a)
{
    Call c;
    c.w = l->workers;
    c.s = l->scene;
    c.a = a;
    return c;
}

int em_player_ladder_climb_0017FC80(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a, float blend)
{
    if (!bound_request(l, a)) return -1;
    Call c = helper_call(l, a);
    return clip_FC80(&c, em_ee_bits(blend)) < 0 ? -1 : 0;
}

int em_player_ladder_climb_0017FD00(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a, float blend)
{
    if (!bound_request(l, a)) return -1;
    Call c = helper_call(l, a);
    return clip_pair(&c, 0xE8, 0xEA, em_ee_bits(blend)) < 0 ? -1 : 0;
}

int em_player_ladder_climb_0017FD40(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a, float blend)
{
    if (!bound_request(l, a)) return -1;
    Call c = helper_call(l, a);
    return clip_pair(&c, 0xE9, 0xEB, em_ee_bits(blend)) < 0 ? -1 : 0;
}

static int side_export(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a, int side,
                       const int clips[4], float blend)
{
    if (!bound_request(l, a)) return -1;
    Call c = helper_call(l, a);
    return clip_side(&c, side, clips, em_ee_bits(blend)) < 0 ? -1 : 0;
}

int em_player_ladder_climb_0017FD80(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a, int side, float blend)
{
    return side_export(l, a, side, kClipsFD80, blend);
}
int em_player_ladder_climb_0017FE00(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a, int side, float blend)
{
    return side_export(l, a, side, kClipsFE00, blend);
}
int em_player_ladder_climb_0017FE80(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a, int side, float blend)
{
    return side_export(l, a, side, kClipsFE80, blend);
}
int em_player_ladder_climb_0017FF00(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a, int side, float blend)
{
    return side_export(l, a, side, kClipsFF00, blend);
}

/* ---- the probes ---------------------------------------------------------- */

/* 00180420(p): spad A0 = (0, 0, -3.0, 1.0) (00180424..00180448), then the
 * tail call 001026A0(p + 0x290, p + 0xD0, spad A0) (00180458). */
static int probe_00180420(const Call *c)
{
    set_sp(c, SP_A0 + 0, K_0);
    set_sp(c, SP_A0 + 1, K_0);
    set_sp(c, SP_A0 + 2, K_M3);
    set_sp(c, SP_A0 + 3, K_1);
    float matrix[16], out[4];
    memcpy(matrix, c->a->bytes + 0xD0, sizeof matrix);
    memcpy(out, c->a->bytes + 0x290, sizeof out);
    FAULT(c->w->transform(c->w->context, out, matrix, &c->s->spad38A0[SP_A0]));
    memcpy(c->a->bytes + 0x290, out, sizeof out);
    return 0;
}

/* 00180460(p): 3, or 00180300's result. */
static int probe_00180460(const Call *c, int *result)
{
    const EmPlayerLadderClimbWorkers *w = c->w;
    int r = 0;
    copy3_to_spad(c, SP_A0, 0xB0);                                  /* 00180478: 001031E0 */
    set_sp(c, SP_A0 + 1, em_ee_add_bits(sp(c, SP_A0 + 1), K_4));    /* 00180484..001804B4 */
    FAULT(w->column(w->context, c->a, &c->s->spad38A0[SP_A0], 1, fl(K_18), &r));   /* 001804B0 */
    if (r != 0) { *result = 3; return 0; }          /* 001804B8 / 001804BC */
    FAULT(probe_00180420(c));                        /* 001804C0 */
    set_sp(c, SP_A0 + 0, w32(c->a, 0x290));          /* 001804C8 / 001804E0 */
    set_sp(c, SP_A0 + 2, w32(c->a, 0x298));          /* 001804E4 / 001804EC */
    set_sp(c, SP_A0 + 1, em_ee_add_bits(K_18, w32(c->a, 0xB4)));   /* 001804F0..001804FC */
    return STATUS(w->probe(w->context, c->a, &c->s->spad38A0[SP_A0], u8(c->a, 0xD), result));  /* 00180504 */
}

/* 00180530(p): 2 when 0019AB20 hits, else 00180300 != 0. */
static int probe_00180530(const Call *c, int *result)
{
    const EmPlayerLadderClimbWorkers *w = c->w;
    int r = 0;
    FAULT(probe_00180420(c));                        /* 0018053C */
    copy3_to_spad(c, SP_A0, 0x290);                  /* 0018054C: 001031E0 */
    set_sp(c, SP_A0 + 1, em_ee_sub_bits(sp(c, SP_A0 + 1), K_3));    /* 00180558..0018057C */
    copy3_to_spad(c, SP_B0, 0x290);                  /* 00180578: 001031E0 */
    set_sp(c, SP_B0 + 1, em_ee_sub_bits(sp(c, SP_B0 + 1), K_4_5));  /* 00180584..001805B0 */
    FAULT(w->wall(w->context, c->a, &c->s->spad38A0[SP_B0], 6, &r));   /* 001805AC: 0019AB20 */
    if (r != 0) { *result = 2; return 0; }
    FAULT(w->probe(w->context, c->a, &c->s->spad38A0[SP_A0], u8(c->a, 0xD), &r));   /* 001805D0 */
    *result = r != 0 ? 1 : 0;
    return 0;
}

/* 00180600(p, side, reach $f12, height $f13, depth $f14). */
static int probe_00180600(const Call *c, int side, uint32_t reach, uint32_t height, uint32_t depth,
                          int *result)
{
    const EmPlayerLadderClimbWorkers *w = c->w;
    set_sp(c, SP_A0 + 0, K_0);                       /* 00180610 */
    set_sp(c, SP_A0 + 1, height);                    /* 00180618 */
    set_sp(c, SP_A0 + 2, depth);                     /* 00180620 */
    set_sp(c, SP_A0 + 3, K_1);                       /* 00180634 (delay slot) */
    /* 00180630: side == 0 negates the reach (neg.s, 00180638). */
    set_sp(c, SP_B0 + 0, side == 0 ? em_ee_neg_bits(reach) : reach);
    set_sp(c, SP_B0 + 1, height);                    /* 00180648 / 0018066C */
    set_sp(c, SP_B0 + 2, depth);                     /* 00180650 / 00180674 */
    set_sp(c, SP_B0 + 3, K_1);                       /* 0018065C / 0018067C */
    FAULT(transform_spad(c, SP_C0, SP_A0));          /* 00180690 */
    FAULT(transform_spad(c, SP_D0, SP_B0));          /* 001806A8 */
    return STATUS(w->sweep(w->context, c->a, &c->s->spad38A0[SP_C0], &c->s->spad38A0[SP_D0], 7, result));  /* 001806C4 */
}

/* The two box writes of 001809B0 that differ only in their constants:
 * A0 = B0 = (+-ydist, y, z_a / z_b, 1.0). */
static void box(const Call *c, int side, uint32_t ydist, uint32_t y, uint32_t za, uint32_t zb)
{
    uint32_t x = side != 0 ? ydist : em_ee_neg_bits(ydist);   /* 00180A88 / 00180E70 */
    set_sp(c, SP_A0 + 0, x);
    set_sp(c, SP_B0 + 0, x);
    set_sp(c, SP_A0 + 1, y);
    set_sp(c, SP_B0 + 1, y);
    set_sp(c, SP_A0 + 2, za);
    set_sp(c, SP_A0 + 3, K_1);
    set_sp(c, SP_B0 + 2, zb);
    set_sp(c, SP_B0 + 3, K_1);
}

/* 001809B0(p, side). */
static int probe_001809B0(const Call *c, int side, int *result)
{
    const EmPlayerLadderClimbWorkers *w = c->w;
    EmPlayerLiveActor *a = c->a;
    int r = 0, kind = 0;
    /* 001809F0: 00180600(p, side, 11.5, 10.0, -1.5); nonzero returns 0. */
    FAULT(probe_00180600(c, side, K_11_5, K_10, K_M1_5, &r));
    if (r != 0) { *result = 0; return 0; }           /* 00180A00 */
    uint32_t ydist;
    if (u8(a, 0xD) == 2)                             /* 00180A08 */
        ydist = em_ee_c_lt_bits(w32(a, 0xB4), K_560) ? K_12 : K_10;   /* 00180A28 */
    else
        ydist = K_9;
    uint32_t xc = u8(a, 5) == 0xC ? K_16 : K_19;    /* 00180A60..00180A84 */
    box(c, side, ydist, xc, K_M6, K_4);              /* 00180A90..00180B2C */
    FAULT(transform_spad(c, SP_C0, SP_A0));          /* 00180B40 */
    FAULT(transform_spad(c, SP_D0, SP_B0));          /* 00180B58 */
    FAULT(w->sweep(w->context, a, &c->s->spad38A0[SP_C0], &c->s->spad38A0[SP_D0], 7, &r));  /* 00180B74 */
    if (r & 6) {                                     /* 00180B7C */
        FAULT(hit_kind(c, &kind));                   /* 00180B88..00180B94 */
        if (kind == 0x3D) {
            FAULT(w->grab_check(w->context, a, &r));     /* 00180BA0: 00178390 */
            if (r != 0) { *result = 3; return 0; }       /* 00180BB0 */
        }
    }
    /* 00180BB8..00180C34: A0 = (2 * +-ydist, 12, -2, 1). */
    if (side != 0)
        set_sp(c, SP_A0 + 0, em_ee_mul_bits(K_2, ydist));                  /* 00180C0C */
    else
        set_sp(c, SP_A0 + 0, em_ee_mul_bits(K_2, em_ee_neg_bits(ydist)));  /* 00180BC8 / 00180BCC */
    set_sp(c, SP_A0 + 1, K_12);
    set_sp(c, SP_A0 + 2, K_M2);
    set_sp(c, SP_A0 + 3, K_1);
    FAULT(transform_spad(c, SP_B0, SP_A0));          /* 00180C48 */
    for (unsigned i = 0; i < 3; i++) set_sp(c, SP_C0 + i, sp(c, SP_B0 + i));   /* 00180C5C: 001031E0 */
    set_sp(c, SP_C0 + 1, em_ee_add_bits(sp(c, SP_C0 + 1), K_10));  /* 00180C64..00180C90 */
    FAULT(w->sweep_box(w->context, &c->s->spad38A0[SP_B0], &c->s->spad38A0[SP_C0], 4, 0, &r));  /* 00180C94 */
    if (r != 0) {
        FAULT(hit_kind(c, &kind));                   /* 00180CA4..00180CB0 */
        if (kind == 0x34) {
            uint32_t sx = w32(a, 0xB0), sz = w32(a, 0xB8);   /* 00180CBC / 00180CC0 */
            FAULT(w->dash(w->context, a, 2));        /* 00180CC8: 00177030 */
            FAULT(w->hit_point(w->context, &c->s->spad38A0[SP_A0]));   /* 00180CD4: 00199DB0 */
            set_sp3A(c, 0, em_ee_sub_bits(sp(c, SP_A0 + 0), w32(a, 0xB0)));    /* 00180CDC..00180CF8 */
            uint32_t dz = em_ee_sub_bits(sp(c, SP_A0 + 2), w32(a, 0xB8));      /* 00180CFC..00180D08 */
            set_sp3A(c, 2, dz);                                                /* 00180D10 */
            uint32_t dx = sp3A(c, 0);                                          /* 00180D04 */
            uint32_t sum = em_ee_madd_bits(em_ee_mula_bits(dx, dx), dz, dz);   /* 00180D14..00180D20 */
            set_sp3A(c, 3, em_ee_bits(w->sqrt(w->context, fl(sum))));         /* 00180D1C / 00180D30 */
            uint32_t q = em_ee_div_bits(sp3A(c, 3), K_4_5);                    /* 00180D38..00180D48 / 00180DC4..00180DD4 */
            int32_t n = w->to_int(w->context, fl(q));                          /* 00180D54 / 00180DE0 */
            /* 00180D2C: side == 1 steps one fewer, any other side one more. */
            n = (int32_t)((uint32_t)n + (side == 1 ? UINT32_C(0xFFFFFFFF) : 1u));   /* 00180D5C / 00180DE8 */
            uint32_t step = em_ee_add_bits(K_0_5, em_ee_cvt_s_w_bits((uint32_t)n)); /* 00180D6C / 00180D7C */
            uint32_t s = em_ee_bits(w->sine(w->context, fl(w32(a, 0xC4))));   /* 00180D60 / 00180D78 */
            step = em_ee_mul_bits(K_4_5, step);                                /* 00180D8C */
            uint32_t along = em_ee_mul_bits(step, s);                          /* 00180D94 */
            set32(a, 0x290, side == 1 ? em_ee_add_bits(sp(c, SP_A0 + 0), along)     /* 00180D98 */
                                      : em_ee_sub_bits(sp(c, SP_A0 + 0), along));   /* 00180E24 */
            uint32_t co = em_ee_bits(w->cosine(w->context, fl(w32(a, 0xC4))));  /* 00180DA0 / 00180E2C */
            uint32_t across = em_ee_mul_bits(step, co);                        /* 00180DA8 / 00180E34 */
            set32(a, 0x298, side == 1 ? em_ee_add_bits(sp(c, SP_A0 + 2), across)    /* 00180DB4 */
                                      : em_ee_sub_bits(sp(c, SP_A0 + 2), across));  /* 00180E40 */
            set32(a, 0xB0, sx);                                                /* 00180E48 */
            set32(a, 0xB8, sz);                                                /* 00180E4C */
            uint32_t hy = 0;
            FAULT(w->hit_y(w->context, &hy));                                  /* 00180E54 */
            set32(a, 0x294, em_ee_sub_bits(hy, K_20_5));                       /* 00180E64 / 00180E6C */
            *result = 7;
            return 0;
        }
    }
    box(c, side, ydist, K_9, K_M5, K_5);             /* 00180E70..00180F1C */
    FAULT(transform_spad(c, SP_C0, SP_A0));          /* 00180F30 */
    FAULT(transform_spad(c, SP_D0, SP_B0));          /* 00180F48 */
    FAULT(w->sweep(w->context, a, &c->s->spad38A0[SP_C0], &c->s->spad38A0[SP_D0], 7, &r));  /* 00180F64 */
    if (!(r & 6)) { *result = 0; return 0; }         /* 00180F6C / 00180F70 */
    FAULT(hit_kind(c, &kind));                       /* 00180F78..00180F84 */
    int out = 0;
    if (kind == 0x32 || kind == 0x3B) {
        FAULT(w->grab(w->context, a, &r));           /* 00180F90 / 00180FB8: 001782A0 */
        if (r != 0) {
            out = 1;
            set8(a, 0xD, kind == 0x32 ? 0 : 1);      /* 00180FA8 / 00180FD0 (delay slots) */
        }
    } else if (kind != 0x3D) {                       /* 00180FD8 */
        FAULT(w->hit_react(w->context, a, &r));      /* 00180FE0: 00178080 */
        if (r != 0) {
            if (c->s->area == 8 && c->s->area_sub == 3) {    /* 00180FF0..00181010 */
                /* 00181018..001810A8: 2 unless 120 < +2E0 < 130 and
                 * 250 < +2E4 < 260 (c.le.s / c.lt.s). */
                uint32_t x = w32(a, 0x2E0);
                out = 2;
                if (!em_ee_c_le_bits(x, K_120) && em_ee_c_lt_bits(x, K_130)) {
                    uint32_t z = w32(a, 0x2E4);
                    if (!em_ee_c_le_bits(z, K_250) && em_ee_c_lt_bits(z, K_260)) {
                        out = 5;                     /* 00181098 */
                        set32(a, 0x2E8, K_156_4);    /* 001810A0 */
                    }
                }
            } else {
                /* 001810B0..001810D8: the stack vector (+2E0, +2E4, +2E8, 1.0). */
                uint32_t v[4] = { w32(a, 0x2E0), w32(a, 0x2E4), w32(a, 0x2E8), K_1 };
                float vf[4];
                memcpy(vf, v, sizeof vf);
                FAULT(w->ledge_ahead(w->context, a, vf, &r));   /* 001810D4: 0017E250 */
                if (r == 0) out = 2;                 /* 001810E4 */
            }
        }
    }
    *result = out;
    return 0;
}

static int bound_probe(const EmPlayerLadderClimb *l, const EmPlayerLiveActor *a)
{
    return l && a && l->workers && l->scene && l->workers->transform;
}

static int clip_zero(const Call *c);

int em_player_ladder_climb_00174AB0(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a)
{
    if (!bound_request(l, a)) return -1;
    Call c = helper_call(l, a);
    return clip_zero(&c) < 0 ? -1 : 0;
}

int em_player_ladder_climb_00180420(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a)
{
    if (!bound_probe(l, a)) return -1;
    Call c = helper_call(l, a);
    return probe_00180420(&c) < 0 ? -1 : 0;
}

int em_player_ladder_climb_00180460(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a, int *result)
{
    if (!bound_probe(l, a) || !result || !l->workers->column || !l->workers->probe) return -1;
    Call c = helper_call(l, a);
    return probe_00180460(&c, result) < 0 ? -1 : 0;
}

int em_player_ladder_climb_00180530(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a, int *result)
{
    if (!bound_probe(l, a) || !result || !l->workers->wall || !l->workers->probe) return -1;
    Call c = helper_call(l, a);
    return probe_00180530(&c, result) < 0 ? -1 : 0;
}

int em_player_ladder_climb_00180600(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a, int side,
                                    float reach, float height, float depth, int *result)
{
    if (!bound_probe(l, a) || !result || !l->workers->sweep) return -1;
    Call c = helper_call(l, a);
    return probe_00180600(&c, side, em_ee_bits(reach), em_ee_bits(height), em_ee_bits(depth),
                          result) < 0 ? -1 : 0;
}

static int bound_809B0(const EmPlayerLadderClimbWorkers *w)
{
    return w->transform && w->sweep && w->hit_kind && w->grab_check && w->sweep_box && w->dash &&
           w->hit_point && w->sqrt && w->to_int && w->sine && w->cosine && w->hit_y && w->grab &&
           w->hit_react && w->ledge_ahead;
}

int em_player_ladder_climb_001809B0(const EmPlayerLadderClimb *l, EmPlayerLiveActor *a, int side,
                                    int *result)
{
    if (!bound_probe(l, a) || !result || !bound_809B0(l->workers)) return -1;
    Call c = helper_call(l, a);
    return probe_001809B0(&c, side, result) < 0 ? -1 : 0;
}

/* ---- 001662D0 ------------------------------------------------------------ */

/* 00181110(p, arg): 1 when the reaction entered +4 = 2, +5 = 4. */
static int reacts(const Call *c, int arg)
{
    return em_player_major2_00181110(c->a, arg);
}

/* The +204 rate of the climb cycle, by +23F (001667A4..001667C4 /
 * 00166984..001669A4): 3 -> 2.0, 2 -> 1.5, anything else 1.0. */
static void cycle_rate(EmPlayerLiveActor *a)
{
    uint8_t gait = u8(a, 0x23F);
    set32(a, 0x204, gait == 3 ? K_2 : gait == 2 ? K_1_5 : K_1);
}

/* The in-cycle root motion of sub-states 4 and 0xC (00166748..0016677C /
 * 00166924..0016695C): +38 = bone0+4 - +21C, +21C = bone0+4, +B4 += +38. */
static int cycle_motion(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    uint32_t node = 0;
    FAULT(node_word(c, 0, 0x4, &node));
    set32(a, 0x38, em_ee_sub_bits(node, w32(a, 0x21C)));
    FAULT(node_word(c, 0, 0x4, &node));
    set32(a, 0x21C, node);
    set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), w32(a, 0x38)));
    return 0;
}

/* Sub-states 0 and 1 (00166480 / 001664A4). Returns 1 when the original
 * returns without the tail. */
static int state_1(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    int r = 0;
    if (reacts(c, 0) != 0) return 1;                 /* 001664A4 / 001664AC */
    FAULT(w->steer_input(w->context, a));            /* 001664B4: 00174FD0 */
    int32_t mode = (int32_t)w32(a, 0x24C);           /* 001664BC */
    if (mode == 0) {
        FAULT(probe_00180460(c, &r));                /* 001664C8 */
        if (r == 0) set8(a, 6, u8(a, 6) + 1);        /* 001664D8..001664E4 */
        else if (r == 2 && u8(a, 0xD) == 0) set8(a, 6, 0x14);   /* 001664EC..00166508 */
    } else if (mode == 1) {
        FAULT(probe_00180530(c, &r));                /* 00166518 */
        set8(a, 6, r == 0 ? 0xA : r == 1 ? 0x28 : 0x1E);        /* 00166528..00166554 */
    } else if (mode == 2 || mode == 3) {
        /* 00166580 / 001665C0: 00180600(p, 0 / 1, 9.0, 10.0, -2.0). */
        FAULT(probe_00180600(c, mode == 2 ? 0 : 1, K_9, K_10, K_M2, &r));
        if (r == 0) set8(a, 6, mode == 2 ? 0x32 : 0x3C);        /* 00166590 / 001665D0 */
    }
    set32(a, 0x21C, 0);                              /* 001665D8 */
    set32(a, 0x38, 0);                               /* 001665E0 (delay slot) */
    return 0;
}

/* Sub-states 4 (up) and 0xC (down). */
static int cycle(const Call *c, int down)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    int r = 0;
    FAULT(w->steer_input(w->context, a));            /* 00166650 / 00166850: 00174FD0 */
    int32_t want = down ? 1 : 0;                     /* the +24C of this direction */
    if (w32(a, 0x200) & 0x1000u) {                   /* 00166658 / 00166858 */
        /* 00166668..00166680 / 00166868..00166880: +B4 = 3.0 + +294 or
         * +294 - 3.0, stored in 00182A70's delay slot. */
        set32(a, 0xB4, down ? em_ee_sub_bits(w32(a, 0x294), K_3) : em_ee_add_bits(K_3, w32(a, 0x294)));
        FAULT(w->sound_109(w->context, a));          /* 0016667C / 0016687C: 00182A70 */
        FAULT(w->sfx(w->context, 0x107, 0x1000, 0x1000, 0x1000));   /* 00166690 / 00166890: 001FB9F0 */
        set8(a, 0x2F1, 1u - u8(a, 0x2F1));           /* 00166698..001666A4 / 00166898..001668A4 */
        if ((int32_t)w32(a, 0x24C) == want) {        /* 001666A8 / 001668A8 */
            if (!down) {
                FAULT(probe_00180460(c, &r));        /* 001666B4 */
                if (r == 0) {
                    FAULT(clip_pair(c, 0xE8, 0xEA, K_1));   /* 001666CC: 0017FD00(p, 1.0) */
                    set32(a, 0x21C, 0);              /* 001666D8 (delay slot) */
                } else if (r == 2 && u8(a, 0xD) == 0) {
                    set8(a, 6, 0x14);                /* 001666FC */
                } else {
                    set8(a, 6, 1);                   /* 00166704 */
                    FAULT(clip_FC80(c, K_16));       /* 00166710 */
                }
            } else {
                FAULT(probe_00180530(c, &r));        /* 001668B4 */
                if (r == 0) {
                    FAULT(clip_pair(c, 0xE9, 0xEB, K_0));   /* 001668C8: 0017FD40(p, 0.0) */
                    set32(a, 0x21C, 0);              /* 001668D4 (delay slot) */
                } else {
                    set8(a, 6, r == 1 ? 0x28 : 0x1E);        /* 001668DC..001668F8 */
                }
            }
        } else {
            set8(a, 6, 1);                           /* 00166728 / 00166904 */
            FAULT(clip_FC80(c, K_16));               /* 0016672C / 00166908: 0017FC80 */
        }
        copy3_actor(a, 0x290, 0xB0);                 /* 00166738 / 00166914: 001031E0 */
    } else {
        FAULT(cycle_motion(c));
        if ((int32_t)w32(a, 0x24C) == want) cycle_rate(a);   /* 00166780 / 00166960 */
    }
    return reacts(c, 1) != 0 ? 1 : 0;                /* 001667CC / 001669AC: 00181110(p, 1) */
}

/* The shared lowering of sub-states 0x15 and 0x20: +B4 = (read - 11.5),
 * then +B4 + lift (00166A24..00166A58 / 00166E98..00166EF4). */
static void place_below(EmPlayerLiveActor *a, uint32_t lift)
{
    uint32_t y = em_ee_sub_bits(w32(a, 0xB4), K_11_5);
    set32(a, 0xB4, y);
    set32(a, 0xB4, em_ee_add_bits(y, lift));
}

/* 00175900(p, 1), then on a nonzero result 00182430(p, 2) and
 * 00187EE0(p, p + 0xB0, p + 0xD0) (00166A54..00166A7C / 00166EF0..00166F18). */
static int settle(const Call *c)
{
    const EmPlayerLadderClimbWorkers *w = c->w;
    int r = 0;
    FAULT(w->floor(w->context, c->a, 1, &r));
    if (r != 0) {
        FAULT(w->footstep(w->context, c->a, 2));
        FAULT(w->ground_effect(w->context, c->a));
    }
    return 0;
}

/* 00174AB0(p): 001749A0(p, 0, 1, 0.0). */
static int clip_zero(const Call *c)
{
    return request(c, 0, 1, K_0);
}

/* The four-step +3C countdown shared by sub-states 0x15, 0x2C:
 * when +28 == index and +3C <= limit[index]. */
static int state_15(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    uint32_t flags = w32(a, 0x200);                  /* 001669F8 */
    if (flags & 0x1000u) {
        FAULT(w->skeleton(w->context, a));           /* 00166A08: 001C68C0(p) */
        /* 00166A1C: 00102948(p + 0xB0, bone1 + 0xC0), a quadword copy. */
        for (unsigned i = 0; i < 4; i++) {
            uint32_t word = 0;
            FAULT(node_word(c, 1, 0xC0 + 4 * i, &word));
            set32(a, 0xB0 + 4 * i, word);
        }
        place_below(a, K_M0_4);                      /* 00166A24..00166A58 */
        FAULT(settle(c));
        FAULT(clip_zero(c));                         /* 00166A84: 00174AB0 */
        FAULT(w->heading(w->context, a, 0));         /* 00166A90: 00174AC0 */
        if (u8(a, 0x23F) >= 2) {                     /* 00166A98 / 00166A9C */
            set8(a, 6, u8(a, 6) + 1);                /* 00166AA8..00166ABC */
            return STATUS(w->reentry(w->context, a, 1));     /* 00166AB8: 0017C440 */
        }
        set8(a, 0x25C, 0);                           /* 00166AD0 (delay slot) */
        return STATUS(w->handoff(w->context, a));            /* 00166ACC: 0017C540 */
    }
    if (flags & 0x8000u) return 0;                   /* 00166AE0 */
    uint8_t sub = u8(a, 7);                          /* 00166AE8 */
    if (sub == 0) {
        int r = 0;
        copy3_to_spad(c, SP_A0, 0xB0);               /* 00166B1C: 001031E0 */
        set_sp(c, SP_A0 + 1, em_ee_add_bits(sp(c, SP_A0 + 1), K_10));   /* 00166B24..00166B50 */
        FAULT(w->probe(w->context, a, &c->s->spad38A0[SP_A0], 0, &r));   /* 00166B4C: 00180300 */
        if (r == 0) {
            float pa[4] = { 0, 0, 0, 0 }, pb[4] = { 0, 0, 0, 0 };
            FAULT(w->hit_probe(w->context, pa, pb, &r));    /* 00166B60: 00199FA0 */
            if (r != 0) {
                uint32_t hy;
                memcpy(&hy, &pb[1], 4);              /* 00166B74 */
                /* 00166B78: 0011DF78 fabs of (b.y - +B4). */
                uint32_t d = em_ee_sub_bits(hy, w32(a, 0xB4)) & UINT32_C(0x7FFFFFFF);
                set_sp3A(c, 0, d);                   /* 00166B8C */
                uint32_t rise = em_ee_sub_bits(d, K_16_8);   /* 00166B98 */
                set_sp3A(c, 0, rise);                /* 00166B9C */
                int frames = 0;
                FAULT(w->clip_frames(w->context, w32(a, 0x40), h16(a, 0x20C), &frames));  /* 00166BA4 */
                /* 00166BAC..00166BC0: +2E4 = spad 3A20 / (float)frames. */
                set32(a, 0x2E4, em_ee_div_bits(sp3A(c, 0), em_ee_cvt_s_w_bits((uint32_t)frames)));
                set8(a, 7, u8(a, 7) + 1);            /* 00166BC4..00166BD0 */
            }
        }
    } else if (sub == 1) {
        if (em_ee_c_le_bits(w32(a, 0x3C), K_1)) set8(a, 7, sub + 1);   /* 00166BD4..00166BF8 */
        set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), w32(a, 0x2E4)));  /* 00166BFC..00166C08 */
    }
    /* 00166C0C: the +28 countdown against +3C (78, 64, 48, 30). */
    static const uint32_t kLimit[4] = { K_78, K_64, K_48, K_30 };
    int16_t count = h16(a, 0x28);
    if (count >= 0 && count <= 3 && em_ee_c_le_bits(w32(a, 0x3C), kLimit[count])) {
        set16(a, 0x28, (unsigned)count + 1);         /* 00166C64..00166D10 */
        return STATUS(w->sound_109(w->context, a));          /* 00166C6C..00166D14: 00182A70 */
    }
    return 0;
}

static int state_20(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    uint32_t node = 0;
    if (w32(a, 0x200) & 0x1000u) {                   /* 00166DA8 */
        FAULT(w->skeleton(w->context, a));           /* 00166DB8: 001C68C0(p) */
        FAULT(node_word(c, 1, 0xC0, &node));         /* 00166DC8..00166DE4 */
        set32(a, 0xB0, node);                        /* 00166DE8 */
        FAULT(node_word(c, 1, 0xC4, &node));         /* 00166DEC..00166DF4 */
        set32(a, 0xB4, em_ee_sub_bits(node, K_11_5));    /* 00166DF8 / 00166DFC */
        FAULT(node_word(c, 1, 0xC8, &node));         /* 00166E00..00166E08 */
        set32(a, 0xB8, node);                        /* 00166E0C */
        set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), K_M0_2));   /* 00166E10..00166E1C */
        int r = 0;
        FAULT(w->floor(w->context, a, 1, &r));       /* 00166E18: 00175900 (result unused) */
        FAULT(clip_zero(c));                         /* 00166E20: 00174AB0 */
        set8(a, 4, 1);                               /* 00166E2C */
        set8(a, 5, 0);                               /* 00166E30 */
        set8(a, 6, 0);                               /* 00166E34 */
        set8(a, 0x1F0, 0);                           /* 00166E38 */
    }
    int16_t count = h16(a, 0x28);                    /* 00166E3C */
    if (count != 0 || !em_ee_c_le_bits(w32(a, 0x3C), K_22)) return 0;   /* 00166E44..00166E74 */
    set16(a, 0x28, (unsigned)count + 1);             /* 00166E8C (delay slot) */
    copy3_actor(a, 0x290, 0xB0);                     /* 00166E88: 001031E0 */
    FAULT(w->skeleton(w->context, a));               /* 00166E90: 001C68C0 */
    FAULT(node_word(c, 1, 0xC0, &node));             /* 00166EA0..00166EBC */
    set32(a, 0xB0, node);                            /* 00166EC0 */
    FAULT(node_word(c, 1, 0xC4, &node));             /* 00166EC4..00166ECC */
    set32(a, 0xB4, em_ee_sub_bits(node, K_11_5));    /* 00166ED0 / 00166ED4 */
    FAULT(node_word(c, 1, 0xC8, &node));             /* 00166ED8..00166EE0 */
    set32(a, 0xB8, node);                            /* 00166EE4 */
    set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), K_M0_4));   /* 00166EE8..00166EF4 */
    FAULT(settle(c));                                /* 00166EF0..00166F18 */
    copy3_actor(a, 0xB0, 0x290);                     /* 00166F20: 001031E0 */
    return 0;
}

static int state_2B(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    /* 00166F9C..00167050: a pending hit on +224 or +22C (c.eq.s against 0)
     * plays its cue and hit reaction and sets +24C = 1; else 00174FD0. */
    if (!em_ee_c_eq_bits(w32(a, 0x224), K_0)) {
        FAULT(w->cue(w->context, 0, 0xC0, 5, 1));    /* 00166FC4: 001B61C0 */
        FAULT(w->sound(w->context, a, 0x152, 0, fl(K_300)));    /* 00166FDC */
        FAULT(w->w0021C350(w->context, a));          /* 00166FE4 */
        set32(a, 0x24C, 1);                          /* 00166FF4 (delay slot) */
    } else if (!em_ee_c_eq_bits(w32(a, 0x22C), K_0)) {
        FAULT(w->cue(w->context, 0, 0xC0, 5, 1));    /* 00167018 */
        FAULT(w->sound(w->context, a, 0x153, 0, fl(K_300)));    /* 00167030 */
        FAULT(w->w0021C270(w->context, a));          /* 00167038 */
        set32(a, 0x24C, 1);                          /* 00167048 (delay slot) */
    } else {
        FAULT(w->steer_input(w->context, a));        /* 0016704C: 00174FD0 */
    }
    int32_t mode = (int32_t)w32(a, 0x24C);           /* 00167054 */
    if (mode == 0) {
        set8(a, 6, u8(a, 6) + 1);                    /* 00167060..00167078 */
        set16(a, 0x28, 0);                           /* 00167084 (delay slot) */
        return request(c, 0xFE, 1, K_1);             /* 00167080 */
    }
    if (mode != 1) return 0;                         /* 00167094 */
    uint32_t node = 0;
    set8(a, 6, u8(a, 6) + 2);                        /* 0016709C..001670B8 */
    FAULT(node_word(c, 1, 0xC0, &node));             /* 001670BC..001670C8 */
    set32(a, 0xB0, node);                            /* 001670CC */
    FAULT(node_word(c, 1, 0xC8, &node));             /* 001670D0..001670D8 */
    set32(a, 0xB8, node);                            /* 001670DC */
    set32(a, 0xB4, em_ee_sub_bits(w32(a, 0xB4), K_7_2));    /* 001670E0..001670EC */
    FAULT(request(c, 0x80, 1, K_0));                 /* 001670E8 */
    set32(a, 0x21C, 0);                              /* 001670F0 */
    set32(a, 0x38, 0);                               /* 001670F4 */
    set32(a, 0x2E4, 0);                              /* 001670F8 */
    set32(a, 0x2F4, w32(a, 0xB4));                   /* 001670FC / 00167104 */
    return 0;
}

static int state_2C(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    if (w32(a, 0x200) & 0x1000u) {                   /* 00167108 */
        uint32_t node = 0;
        set8(a, 6, 1);                               /* 00167120 */
        FAULT(node_word(c, 1, 0xC4, &node));         /* 00167124..00167138 */
        set32(a, 0xB4, em_ee_sub_bits(node, K_10_5));    /* 0016713C / 00167140 */
        set8(a, 0x2F1, 0);                           /* 00167144 */
        FAULT(request(c, kD002754D0[0], 0, K_0));    /* 00167148 / 0016714C */
        FAULT(clip_FC80(c, K_16));                   /* 0016715C: 0017FC80 */
        set8(a, 0x1F0, 0x17);                        /* 00167168 */
    }
    int16_t count = h16(a, 0x28);                    /* 0016716C */
    uint32_t limit;
    if (count == 0) limit = K_20;                    /* 00167198 */
    else if (count == 1) limit = K_2;                /* 001671D0 */
    else return 0;
    if (!em_ee_c_le_bits(w32(a, 0x3C), limit)) return 0;
    FAULT(w->sound_109(w->context, a));              /* 001671B8 / 001671F0: 00182A70 */
    set16(a, 0x28, (unsigned)h16(a, 0x28) + 1);      /* 001671C0..001671CC / 001671F8..00167204 */
    return 0;
}

static int state_2D(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    if (w32(a, 0x200) & 0x1000u) {                   /* 00167208 */
        set8(a, 5, 7);                               /* 0016721C */
        set8(a, 6, 0);                               /* 00167224 */
        set8(a, 0x1F0, 0xD);                         /* 0016722C (delay slot) */
        return 0;
    }
    uint32_t node = 0;
    FAULT(node_word(c, 0, 0x8, &node));              /* 00167230..00167240 */
    set32(a, 0x38, em_ee_sub_bits(node, w32(a, 0x21C)));    /* 00167244 / 00167248 */
    FAULT(node_word(c, 0, 0x8, &node));              /* 0016724C..00167254 */
    set32(a, 0x21C, node);                           /* 0016725C (delay slot) */
    FAULT(w->translate(w->context, a, 1));           /* 00167258: 00178B90 */
    FAULT(node_word(c, 0, 0x4, &node));              /* 00167260..0016726C */
    set32(a, 0x2EC, em_ee_sub_bits(node, w32(a, 0x2E4)));   /* 00167270 / 00167274 */
    FAULT(node_word(c, 0, 0x4, &node));              /* 00167278..00167280 */
    set32(a, 0x2E4, node);                           /* 00167284 */
    set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), w32(a, 0x2EC)));   /* 00167288..00167298 */
    return 0;
}

/* Sub-states 0x33 / 0x3D: at the clip end +6 += 1 and 0017FE00(p, side,
 * 1.0); else the root motion along the +D0 matrix. */
static int side_move(const Call *c, int side)
{
    EmPlayerLiveActor *a = c->a;
    if (w32(a, 0x200) & 0x1000u) {                   /* 00167300 / 0016756C */
        set8(a, 6, u8(a, 6) + 1);                    /* 00167310..0016732C / 0016757C..00167598 */
        return clip_side(c, side, kClipsFE00, K_1);  /* 00167328 / 00167594: 0017FE00 */
    }
    uint32_t node = 0;
    FAULT(node_word(c, 0, 0x0, &node));              /* 00167338..0016735C */
    set32(a, 0x38, em_ee_sub_bits(node, w32(a, 0x21C)));    /* 00167360 / 00167364 */
    FAULT(node_word(c, 0, 0x0, &node));              /* 00167368..00167370 */
    set32(a, 0x21C, node);                           /* 00167374 */
    set_sp(c, SP_A0 + 0, w32(a, 0x38));              /* 00167378 / 0016737C */
    set_sp(c, SP_A0 + 1, 0);                         /* 00167384 */
    set_sp(c, SP_A0 + 2, 0);                         /* 0016738C */
    set_sp(c, SP_A0 + 3, 0);                         /* 00167398 (delay slot) */
    FAULT(transform_spad(c, SP_B0, SP_A0));          /* 00167394: 001026A0 */
    set32(a, 0xB0, em_ee_add_bits(w32(a, 0xB0), sp(c, SP_B0 + 0)));   /* 0016739C..001673B0 */
    set32(a, 0xB8, em_ee_add_bits(w32(a, 0xB8), sp(c, SP_B0 + 2)));   /* 001673B4..001673C4 */
    return 0;
}

/* Sub-states 0x34 / 0x3E. */
static int side_choice(const Call *c, int side)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    if (c->s->pad & c->s->use_mask) {                /* 001673DC..001673F0 / 00167648..0016765C */
        int r = 0;
        FAULT(probe_001809B0(c, side, &r));          /* 001673FC / 00167668 */
        set8(a, 0x1F1, (unsigned)r);                 /* 00167404 / 00167670 */
        uint8_t hop = u8(a, 0x1F1);                  /* 00167408 / 00167674 */
        if (hop == 7) {
            set8(a, 6, 0x50);                        /* 0016741C / 00167688 */
            set8(a, 0x1F0, 0x19);                    /* 00167424 / 00167690 */
            FAULT(clip_side(c, side, kClipsFF00, K_1));   /* 00167434 / 001676A0: 0017FF00 */
            c->s->d8106F2 = side == 0 ? 5 : 4;       /* 00167448 / 001676B4 (delay slot) */
        } else if (hop != 0) {
            set8(a, 6, 0x46);                        /* 00167458 / 001676C4 */
            set8(a, 0x1F0, 0x19);                    /* 00167460 / 001676CC */
            FAULT(clip_side(c, side, kClipsFF00, K_1));   /* 00167470 / 001676DC: 0017FF00 */
        }
        return 0;
    }
    FAULT(w->steer_input(w->context, a));            /* 00167484 / 001676F0: 00174FD0 */
    if ((int32_t)w32(a, 0x24C) == (side == 0 ? 2 : 3)) return 0;   /* 00167494 / 00167700 */
    set8(a, 6, u8(a, 6) + 1);                        /* 0016749C..001674B8 / 00167708..00167724 */
    return clip_side(c, side, kClipsFE80, K_1);      /* 001674B4 / 00167720: 0017FE80 */
}

/* Sub-state 0x46: the 8-step move towards +2E0/+2E8/+2E4. */
static int state_46(const Call *c, uint8_t st)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    if (!em_ee_c_le_bits(w32(a, 0x3C), K_6)) return 0;      /* 00167774..0016778C */
    set8(a, 6, st + 1u);                             /* 00167794 / 00167798 */
    FAULT(sound(c, 0x187));                          /* 001677A8: 001FBD50 */
    set32(a, 0x2F4, w32(a, 0x2E0));                  /* 001677B0 / 001677C0 */
    set32(a, 0x2F8, w32(a, 0x2E8));                  /* 001677C4 / 001677C8 */
    set32(a, 0x258, w32(a, 0x2E4));                  /* 001677CC / 001677D0 */
    set16(a, 0x28, 8);                               /* 001677D4 */
    set32(a, 0x2E0, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x2F4), w32(a, 0xB0)), K_8));   /* 001677D8..001677E8 */
    set32(a, 0x2E8, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x2F8), w32(a, 0xB8)), K_8));   /* 001677EC..001677FC */
    set32(a, 0x2E4, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x258), w32(a, 0xB4)), K_8));   /* 00167800..00167810 */
    uint32_t t = em_ee_bits(w->wrap(w->context, fl(em_ee_sub_bits(w32(a, 0x218), w32(a, 0xC4)))));  /* 0016781C */
    set_sp3A(c, 0, t);                               /* 00167838 (delay slot) */
    if (em_ee_c_lt_bits(t, K_0))                     /* 0016782C */
        set32(a, 0x26C, em_ee_div_bits(em_ee_neg_bits(t), K_8));    /* 0016785C..00167870 */
    else
        set32(a, 0x26C, em_ee_div_bits(t, K_8));     /* 00167848 / 00167858 */
    return request(c, u8(a, 0x1F1) == 1 ? 0xE5 : 0x7A, 0, K_8);    /* 00167874..001678B4 */
}

/* Sub-states 0x47 / 0x51: step +B0/+B8/+B4 and turn +C4 until +28 == 0. */
static int stepping(const Call *c, unsigned dx, unsigned dz, unsigned dy)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    set32(a, 0xB0, em_ee_add_bits(w32(a, 0xB0), w32(a, dx)));      /* 00167934..00167940 / 00167BD0..00167BDC */
    set32(a, 0xB8, em_ee_add_bits(w32(a, 0xB8), w32(a, dz)));      /* 00167944..00167950 / 00167BE0..00167BEC */
    set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), w32(a, dy)));      /* 00167954..00167960 / 00167BF0..00167BFC */
    float heading = w->approach(w->context, fl(w32(a, 0x218)), fl(w32(a, 0xC4)),
                                fl(w32(a, 0x26C)));  /* 0016796C / 00167C08: 001B12B0 */
    set32(a, 0xC4, em_ee_bits(heading));             /* 00167974 / 00167C10 */
    set16(a, 0x28, (unsigned)h16(a, 0x28) - 1u);     /* 00167978..00167984 / 00167C14..00167C20 */
    return 0;
}

static int state_47(const Call *c, uint8_t st)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    if (h16(a, 0x28) != 0) return stepping(c, 0x2E0, 0x2E8, 0x2E4);   /* 001678C4 / 001678C8 */
    set8(a, 6, st + 1u);                             /* 001678D0 / 001678D4 */
    set32(a, 0xB0, w32(a, 0x2F4));                   /* 001678D8 / 001678E0 */
    set32(a, 0xB8, w32(a, 0x2F8));                   /* 001678E4 / 001678E8 */
    set32(a, 0xB4, w32(a, 0x258));                   /* 001678EC / 001678F0 */
    set32(a, 0xC4, w32(a, 0x218));                   /* 001678F4 / 001678F8 */
    if (u8(a, 0x1F1) == 1) return STATUS(w->sound_109(w->context, a));    /* 00167900 / 00167908 */
    return sound(c, 0xFF);                           /* 00167924 */
}

static int state_48(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    if (!(w32(a, 0x200) & 0x1000u)) return 0;        /* 00167988..00167990 */
    uint8_t hop = u8(a, 0x1F1);                      /* 00167998 */
    if (hop == 1) {
        FAULT(w->sound_109(w->context, a));          /* 001679A8: 00182A70 */
        set8(a, 6, 0);                               /* 001679B4 */
        set8(a, 0x1F0, 0x17);                        /* 001679B8 */
        set8(a, 0x2F1, 0);                           /* 001679CC (delay slot) */
        return clip_FC80(c, K_16);                   /* 001679C8: 0017FC80 */
    }
    if (hop == 5) {
        set8(a, 5, 0x18);                            /* 001679E8 */
        set8(a, 6, 0);                               /* 001679F0 */
        set8(a, 0x1F0, 0x2C);                        /* 001679F4 */
        set8(a, 0x1F1, 0);                           /* 001679FC */
        set8(a, 0xD, 2);                             /* 00167A04 (delay slot) */
    } else {
        set8(a, 5, 9);                               /* 00167A18 / 00167A34 */
        set8(a, 6, 0);                               /* 00167A20 / 00167A3C */
        set8(a, 0x1F0, 0x10);                        /* 00167A24 / 00167A40 */
        set8(a, 0xD, hop == 3 ? 1 : 0);              /* 00167A2C / 00167A44 */
    }
    int clip = 0;
    FAULT(w->clip_row(w->context, a, &clip));        /* 00167A48: 00188550 */
    return request(c, clip, 0, K_16);                /* 00167A60: 001749A0 */
}

/* Sub-state 0x50: the 12-step move towards +290/+298/+294. */
static int state_50(const Call *c, uint8_t st)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    if (!(w32(a, 0x200) & 0x1000u)) return 0;        /* 00167A70..00167A78 */
    set8(a, 6, st + 1u);                             /* 00167A80 / 00167A84 */
    FAULT(sound(c, 0x187));                          /* 00167A94: 001FBD50 */
    set_sp3A(c, 0, K_12);                            /* 00167AAC */
    FAULT(request(c, 0xBA, 0, K_12));                /* 00167AB4: 001749A0 */
    int32_t n = w->to_int(w->context, fl(sp3A(c, 0)));   /* 00167AC0 / 00167AC4: float_to_int */
    set16(a, 0x28, (uint32_t)n);                     /* 00167AC8 */
    set32(a, 0x260, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x290), w32(a, 0xB0)), sp3A(c, 0)));  /* 00167ACC..00167AE8 */
    set32(a, 0x264, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x298), w32(a, 0xB8)), sp3A(c, 0)));  /* 00167AEC..00167B04 */
    set32(a, 0x258, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x294), w32(a, 0xB4)), sp3A(c, 0)));  /* 00167B08..00167B1C */
    uint32_t t = em_ee_bits(w->wrap(w->context, fl(em_ee_sub_bits(w32(a, 0x218), w32(a, 0xC4)))));  /* 00167B28 */
    set_sp3A(c, 1, t);                               /* 00167B44 (delay slot) */
    if (em_ee_c_lt_bits(t, K_0))                     /* 00167B38 */
        set32(a, 0x26C, em_ee_div_bits(em_ee_neg_bits(t), sp3A(c, 0)));   /* 00167B64..00167B80 */
    else
        set32(a, 0x26C, em_ee_div_bits(t, sp3A(c, 0)));   /* 00167B48..00167B60 */
    return 0;
}

static int state_51(const Call *c, uint8_t st)
{
    EmPlayerLiveActor *a = c->a;
    if (h16(a, 0x28) != 0) return stepping(c, 0x260, 0x264, 0x258);   /* 00167B84 / 00167B88 */
    set8(a, 6, st + 1u);                             /* 00167B90 / 00167B94 */
    FAULT(sound(c, 0x123));                          /* 00167BA4: 001FBD50 */
    set32(a, 0xB0, w32(a, 0x290));                   /* 00167BAC / 00167BB0 */
    set32(a, 0xB4, w32(a, 0x294));                   /* 00167BB4 / 00167BB8 */
    set32(a, 0xB8, w32(a, 0x298));                   /* 00167BBC / 00167BC0 */
    set32(a, 0xC4, w32(a, 0x218));                   /* 00167BC4 / 00167BCC */
    return 0;
}

static int all_bound(const EmPlayerLadderClimbWorkers *w)
{
    return w->node && w->hit_kind && w->hit_y && w->request && w->sound && w->sfx && w->cue &&
           w->sound_109 && w->steer_input && w->heading && w->skeleton && w->floor && w->footstep &&
           w->ground_effect && w->translate && w->reentry && w->handoff && w->w0021C270 &&
           w->w0021C350 && w->camera && w->clip_row && w->clip_frames && w->probe && w->column &&
           w->wall && w->sweep && w->sweep_box && w->hit_probe && w->hit_point && w->transform &&
           w->ledge_ahead && w->grab_check && w->dash && w->grab && w->hit_react && w->sqrt &&
           w->sine && w->cosine && w->wrap && w->approach && w->to_int;
}

/* The +6 dispatch (001662DC: the compare chain on +6). Returns 1 for the
 * paths that return at once (00167C68, skipping the tail), 0 for the
 * paths that reach the tail at 00167C4C, -1 on a fault. */
static int ladder_dispatch(const Call *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerLadderClimbWorkers *w = c->w;
    uint8_t st = u8(a, 6);
    switch (st) {
    case 0:                                          /* 00166480 */
        set8(a, 6, st + 1u);
        set8(a, 7, 0);                               /* 00166490 (delay slot) */
        FAULT(clip_FC80(c, K_16));                   /* 0016648C: 0017FC80 */
        copy3_actor(a, 0x290, 0xB0);                 /* 00166498: 001031E0 */
        return state_1(c);                           /* falls into 001664A4 */
    case 1: return state_1(c);
    case 2: case 0xA:                                /* 001665E8 / 001667E8 */
        if (reacts(c, 0) != 0) return 1;
        set8(a, 6, u8(a, 6) + 1);                    /* 001665F8..00166610 / 001667F8..00166810 */
        /* 0016660C: 0017FD00(p, 4.0); 0016680C: 0017FD40(p, 4.0). */
        return st == 2 ? clip_pair(c, 0xE8, 0xEA, K_4) : clip_pair(c, 0xE9, 0xEB, K_4);
    case 3: case 0xB:                                /* 00166620 / 00166820 */
        if (reacts(c, 0) != 0) return 1;
        if (!(w32(a, 0x200) & 0x8000u)) set8(a, 6, u8(a, 6) + 1);   /* 00166630..0016664C */
        return 0;
    case 4: return cycle(c, 0);
    case 0xC: return cycle(c, 1);
    case 0x14:                                       /* 001669C8 */
        set8(a, 6, st + 1u);
        set8(a, 0x1F0, 0x18);                        /* 001669D4 */
        set8(a, 7, 0);                               /* 001669D0 */
        set16(a, 0x28, 0);                           /* 001669EC (delay slot) */
        return request(c, 0xF0, 1, K_4);             /* 001669E8 */
    case 0x15: return state_15(c);
    case 0x16:                                       /* 00166D28 */
        FAULT(w->heading(w->context, a, 0));         /* 00166D28: 00174AC0 */
        FAULT(w->translate(w->context, a, 1));       /* 00166D34: 00178B90 */
        if (w32(a, 0x200) & 0x8000u) return 0;       /* 00166D3C..00166D44 */
        return STATUS(w->handoff(w->context, a));            /* 00166D4C: 0017C540 */
    case 0x1E:                                       /* 00166D60 */
        set8(a, 6, st + 1u);
        set8(a, 0x1F0, 0x18);                        /* 00166D68 */
        set16(a, 0x28, 0);                           /* 00166D80 (delay slot) */
        return request(c, 0xF1, 1, K_8);             /* 00166D7C */
    case 0x1F:                                       /* 00166D8C */
        if (!(w32(a, 0x200) & 0x8000u)) set8(a, 6, st + 1u);
        return 0;
    case 0x20: return state_20(c);
    case 0x28:                                       /* 00166F34 */
        set8(a, 6, st + 1u);
        set8(a, 0x1F0, 0x1A);                        /* 00166F3C */
        return request(c, 0xFF, 1, K_4);             /* 00166F4C */
    case 0x29:                                       /* 00166F5C */
        if (w32(a, 0x200) & 0x8000u) return 0;
        set8(a, 6, st + 1u);                         /* 00166F74 (delay slot) */
        return STATUS(w->sound_109(w->context, a));          /* 00166F70: 00182A70 */
    case 0x2A:                                       /* 00166F80 */
        if (w32(a, 0x200) & 0x1000u) set8(a, 6, st + 1u);
        return 0;
    case 0x2B: return state_2B(c);
    case 0x2C: return state_2C(c);
    case 0x2D: return state_2D(c);
    case 0x32: case 0x3C:                            /* 001672A0 / 0016750C */
        if (reacts(c, 0) != 0) return 1;
        set8(a, 6, u8(a, 6) + 1);                    /* 001672B0..001672C4 / 0016751C..00167530 */
        FAULT(w->sfx(w->context, st == 0x32 ? 0x10E : 0x10F, 0x1000, 0x1000, 0x1000));  /* 001672C8 / 00167534 */
        return clip_side(c, st == 0x32 ? 0 : 1, kClipsFD80, K_4);   /* 001672DC / 00167548: 0017FD80 */
    case 0x33: case 0x3D:                            /* 001672F0 / 0016755C */
        if (reacts(c, 0) != 0) return 1;
        return side_move(c, st == 0x33 ? 0 : 1);
    case 0x34: case 0x3E:                            /* 001673CC / 00167638 */
        if (reacts(c, 0) != 0) return 1;
        return side_choice(c, st == 0x34 ? 0 : 1);
    case 0x35: case 0x3F:                            /* 001674C8 / 00167734 */
        if (reacts(c, 0) != 0) return 1;
        if (!(w32(a, 0x200) & 0x1000u)) return 0;
        set8(a, 6, 1);                               /* 001674EC / 00167758 */
        return clip_FC80(c, K_16);                   /* 001674F8 / 00167764: 0017FC80 */
    case 0x46: return state_46(c, st);
    case 0x47: return state_47(c, st);
    case 0x48: return state_48(c);
    case 0x50: return state_50(c, st);
    case 0x51: return state_51(c, st);
    case 0x52:                                       /* 00167C24 */
        if (w32(a, 0x200) & 0x1000u) {
            set8(a, 5, 0x10);                        /* 00167C38 */
            set8(a, 6, 0);                           /* 00167C40 */
            set8(a, 0x1F0, 0x21);                    /* 00167C44 */
            set8(a, 0x2F1, 0);                       /* 00167C48 */
        }
        return 0;
    default:
        return 0;                                    /* 00166474: straight to the tail */
    }
}

int em_player_ladder_climb_state(void *context, EmPlayerLiveActor *actor)
{
    const EmPlayerLadderClimb *ladder = context;
    if (!ladder || !actor || !ladder->workers || !ladder->scene || !all_bound(ladder->workers))
        return -1;
    Call call;
    call.w = ladder->workers;
    call.s = ladder->scene;
    call.a = actor;
    int r = ladder_dispatch(&call);
    if (r < 0) return -1;
    if (r > 0) return 0;                             /* 00167C68: returned before the tail */
    /* 00167C4C..00167C64: D_00810700 == 2 runs 00176DC0(p). */
    if (call.s->area == 2)
        return call.w->camera(call.w->context, actor) < 0 ? -1 : 0;
    return 0;
}
