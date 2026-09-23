/* em_player_misc_workers.c - see em_player_misc_workers.h and
 * docs/PLAYER_MISC_WORKERS.md.
 *
 * Read from the original instructions: the byte-matched decomp C of
 * 001FBD50, 001FBF50, 001B15D0, 00182250, 0017E510, 0017E7C0, 0017DF70,
 * 0017E0D0, 0017E150, 0017E1D0, 0017FF80, 00182AF0, 0021E650 and 00122BB8,
 * checked against their .s for the float operand order; the .s of the
 * NEARMISS 0017E250, 00177B80 and 0015C1F0; the instruction words of the
 * asm-body 0017DFB0 and 001EFE00. Comment addresses are the original
 * instructions a line translates. Float arithmetic is em_ee_float.h on raw
 * binary32 bits. */
#include "game/em_player_misc_workers.h"
#include "game/em_ee_float.h"
#include "game/em_player_stage_workers.h"
#include "game/em_random.h"

#include <stddef.h>
#include <string.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

/* Binary32 constants as the original materializes them (lui/ori). */
#define K_0        UINT32_C(0x00000000)
#define K_0_1      UINT32_C(0x3DCCCCCD)
#define K_M0_1     UINT32_C(0xBDCCCCCD)
#define K_1_18     UINT32_C(0x3D638E39)   /* 0.055555556 */
#define K_AIM      UINT32_C(0x3EE1307A)   /* 0.43982297 */
#define K_1        UINT32_C(0x3F800000)
#define K_1_5      UINT32_C(0x3FC00000)
#define K_HALF_PI  UINT32_C(0x3FC90FDB)
#define K_2        UINT32_C(0x40000000)
#define K_3        UINT32_C(0x40400000)
#define K_M3       UINT32_C(0xC0400000)
#define K_M2       UINT32_C(0xC0000000)
#define K_3_5      UINT32_C(0x40600000)
#define K_3_62     UINT32_C(0x4067AE14)
#define K_4_01     UINT32_C(0x408051EC)
#define K_4_4      UINT32_C(0x408CCCCD)
#define K_M4_4     UINT32_C(0xC08CCCCD)
#define K_4_5      UINT32_C(0x40900000)
#define K_M4_5     UINT32_C(0xC0900000)
#define K_3PI_2    UINT32_C(0x4096CBE4)   /* 4.712389 */
#define K_5        UINT32_C(0x40A00000)
#define K_M5       UINT32_C(0xC0A00000)
#define K_9        UINT32_C(0x41100000)
#define K_M9       UINT32_C(0xC1100000)
#define K_9_99     UINT32_C(0x411FD70A)
#define K_10       UINT32_C(0x41200000)
#define K_12       UINT32_C(0x41400000)
#define K_14       UINT32_C(0x41600000)
#define K_18       UINT32_C(0x41900000)
#define K_19_5     UINT32_C(0x419C0000)
#define K_20       UINT32_C(0x41A00000)
#define K_20_5     UINT32_C(0x41A40000)
#define K_M20_5    UINT32_C(0xC1A40000)
#define K_24_51    UINT32_C(0x41C4147B)
#define K_45       UINT32_C(0x42340000)
#define K_60       UINT32_C(0x42700000)
#define K_85       UINT32_C(0x42AA0000)
#define K_105      UINT32_C(0x42D20000)
#define K_120      UINT32_C(0x42F00000)
#define K_123_5    UINT32_C(0x42F70000)
#define K_130      UINT32_C(0x43020000)
#define K_156_4    UINT32_C(0x431C6666)
#define K_160      UINT32_C(0x43200000)
#define K_170      UINT32_C(0x432A0000)
#define K_173      UINT32_C(0x432D0000)
#define K_257_5    UINT32_C(0x4380C000)
#define K_300      UINT32_C(0x43960000)
#define K_4096     UINT32_C(0x45800000)
#define K_M0_5     UINT32_C(0xBF000000)

/* ---- raw access -------------------------------------------------------- */

static uint32_t fbits(float f) { return em_ee_bits(f); }
static float bfloat(uint32_t b) { return em_ee_float(b); }
static uint32_t word(const float *p, unsigned i) { uint32_t v; memcpy(&v, p + i, 4); return v; }
static void set_word(float *p, unsigned i, uint32_t v) { memcpy(p + i, &v, 4); }
static void set_quad(float *p, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    set_word(p, 0, x); set_word(p, 1, y); set_word(p, 2, z); set_word(p, 3, w);
}
static uint8_t u8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void set8(EmPlayerLiveActor *a, unsigned at, uint8_t v) { em_live_set_u8(a, at, v); }
static uint32_t w32(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void set32(EmPlayerLiveActor *a, unsigned at, uint32_t v) { em_live_set_u32(a, at, v); }
/* A pointer to the record's own bytes at `at`, handed to a worker exactly
 * where the original passes the record address plus `at`. */
static float *field(EmPlayerLiveActor *a, unsigned at) { return (float *)(void *)(a->bytes + at); }

/* ---- readiness (checked before the first write) ------------------------ */

#define N_IDENTITY    (UINT32_C(1) << 0)
#define N_EULER       (UINT32_C(1) << 1)
#define N_TRANSLATE   (UINT32_C(1) << 2)
#define N_TRANSFORM   (UINT32_C(1) << 3)
#define N_VADD        (UINT32_C(1) << 4)
#define N_VSUB        (UINT32_C(1) << 5)
#define N_NORMALIZE   (UINT32_C(1) << 6)
#define N_DOT         (UINT32_C(1) << 7)
#define N_SINE        (UINT32_C(1) << 8)
#define N_ATAN2       (UINT32_C(1) << 9)
#define N_SQRT        (UINT32_C(1) << 10)
#define N_WRAP        (UINT32_C(1) << 11)
#define N_SIDE        (UINT32_C(1) << 12)
#define N_SUBMIT      (UINT32_C(1) << 13)
#define N_SOUND_BASE  (UINT32_C(1) << 14)
#define N_REQUEST     (UINT32_C(1) << 15)
#define N_CLIPS       (UINT32_C(1) << 16)
#define N_AHEAD       (UINT32_C(1) << 17)
#define N_MOVE        (UINT32_C(1) << 18)
#define N_SWEEP       (UINT32_C(1) << 19)
#define N_COLUMN      (UINT32_C(1) << 20)
#define N_GROUND      (UINT32_C(1) << 21)
#define N_HIT         (UINT32_C(1) << 22)
#define N_EDGE        (UINT32_C(1) << 23)
#define N_GRABS       (UINT32_C(1) << 24)
#define N_LEDGE_TOP   (UINT32_C(1) << 25)
#define N_BLOCKED     (UINT32_C(1) << 26)
#define N_CUE         (UINT32_C(1) << 27)
#define N_LAND_SOUND  (UINT32_C(1) << 28)
#define N_0021D490    (UINT32_C(1) << 29)
#define N_MODEL       (UINT32_C(1) << 30)
#define N_SPAWN       (UINT32_C(1) << 31)

/* 001FBF50 and 001B15D0; 001FBD50 adds the submit. */
#define N_GAIN (N_IDENTITY | N_EULER | N_TRANSFORM | N_VSUB | N_NORMALIZE | N_DOT | N_SINE | \
                N_SQRT | N_SIDE)
#define N_SOUND (N_GAIN | N_SUBMIT)

static int bound(const EmPlayerMiscHost *h, uint32_t need, int scene, int scratch)
{
    if (!h || !h->workers) return 0;
    if (scene && !h->scene) return 0;
    if (scratch && !h->scratch) return 0;
    const EmPlayerMiscWorkers *w = h->workers;
    const struct { uint32_t bit; int ok; } table[] = {
        { N_IDENTITY, w->identity != NULL }, { N_EULER, w->euler != NULL },
        { N_TRANSLATE, w->translate != NULL }, { N_TRANSFORM, w->transform != NULL },
        { N_VADD, w->vadd != NULL }, { N_VSUB, w->vsub != NULL },
        { N_NORMALIZE, w->normalize != NULL }, { N_DOT, w->dot != NULL },
        { N_SINE, w->sine != NULL }, { N_ATAN2, w->atan2 != NULL }, { N_SQRT, w->sqrt != NULL },
        { N_WRAP, w->wrap != NULL }, { N_SIDE, w->side != NULL }, { N_SUBMIT, w->submit != NULL },
        { N_SOUND_BASE, w->sound_base != NULL }, { N_REQUEST, w->request != NULL },
        { N_CLIPS, w->clip_2F1_0 != NULL && w->clip_2F1_1 != NULL },
        { N_AHEAD, w->ahead != NULL }, { N_MOVE, w->move != NULL }, { N_SWEEP, w->sweep != NULL },
        { N_COLUMN, w->column != NULL }, { N_GROUND, w->ground != NULL },
        { N_HIT, w->hit_node_word != NULL && w->hit_node_byte != NULL &&
                 w->hit_point_word != NULL },
        { N_EDGE, w->edge != NULL },
        { N_GRABS, w->grab != NULL && w->grab_33 != NULL && w->reach != NULL },
        { N_LEDGE_TOP, w->ledge_top != NULL }, { N_BLOCKED, w->blocked != NULL },
        { N_CUE, w->cue != NULL }, { N_LAND_SOUND, w->land_sound != NULL },
        { N_0021D490, w->w0021D490 != NULL },
        { N_MODEL, w->bind_model != NULL && w->bone_count != NULL && w->w00200890 != NULL },
        { N_SPAWN, w->spawn != NULL },
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; ++i)
        if ((need & table[i].bit) && !table[i].ok) return 0;
    return 1;
}

/* ---- 001B15D0 ------------------------------------------------------------ */

/* 001B15D0(a, b): 001028D0(0x70003600, a, b) (001B15E4), then
 * f12 = x*x + y*y + z*z as mul, mul, adda, madd (001B15FC..001B1614), and
 * 0011E748(f12) (001B1610). */
static int distance_bits(EmPlayerMiscHost *h, const float a[4], const float b[4], uint32_t *out)
{
    const EmPlayerMiscWorkers *w = h->workers;
    float *s = h->scratch->s3600;
    FAULT(w->vsub(w->context, s, a, b));
    uint32_t x = word(s, 0), y = word(s, 1), z = word(s, 2);
    uint32_t x2 = em_ee_mul_bits(x, x);
    uint32_t y2 = em_ee_mul_bits(y, y);
    uint32_t acc = em_ee_adda_bits(x2, y2);
    uint32_t sum = em_ee_madd_bits(acc, z, z);
    float r = 0.0f;
    FAULT(w->sqrt(w->context, bfloat(sum), &r));
    *out = fbits(r);
    return 0;
}

int em_player_misc_001B15D0(EmPlayerMiscHost *h, const float a[4], const float b[4], float *distance)
{
    if (!distance || !bound(h, N_VSUB | N_SQRT, 0, 1)) return -1;
    uint32_t d = 0;
    FAULT(distance_bits(h, a, b, &d));
    *distance = bfloat(d);
    return 0;
}

/* ---- 001FBF50 / 001FBD50 -------------------------------------------------- */

static int gain(EmPlayerMiscHost *h, const float obj[4], int32_t *a, int32_t *b, int32_t flat,
                uint32_t radius, uint32_t scale, int32_t *result)
{
    const EmPlayerMiscWorkers *w = h->workers;
    const EmPlayerMiscScene *sc = h->scene;
    EmPlayerMiscScratch *s = h->scratch;
    void *c = w->context;

    *b = 0;                                                    /* 001FBF74 */
    *a = 0;                                                    /* 001FBF88 */
    memcpy(s->s3600, sc->d810360, 16);                         /* 001FBFA0: 00102948 */
    memcpy(s->s3610, obj, 16);                                 /* 001FBFB0: 00102948(3610, obj + B0) */
    if ((int8_t)(uint8_t)flat != 0) {                          /* 001FBFB8: the low byte, signed */
        set_word(s->s3610, 1, 0);                              /* 001FBFCC */
        set_word(s->s3600, 1, 0);                              /* 001FBFD4 */
    }
    uint32_t dist = 0;
    FAULT(distance_bits(h, s->s3600, s->s3610, &dist));        /* 001FBFE4: 001B15D0(3600, 3610) */
    if (!em_ee_c_lt_bits(dist, radius)) {                      /* 001FBFF0 */
        *result = 0;
        return 0;
    }
    uint32_t f0 = em_ee_sub_bits(radius, dist);                /* 001FBFFC */
    uint32_t f1 = em_ee_div_bits(f0, radius);                  /* 001FC014 */
    float sine = 0.0f;
    FAULT(w->sine(c, bfloat(em_ee_mul_bits(K_HALF_PI, f1)), &sine)); /* 001FC020 / 001FC024 */
    uint32_t scaled = em_ee_mul_bits(scale, fbits(sine));      /* 001FC028 */
    FAULT(w->identity(c, s->s3400));                           /* 001FC030 */
    set_quad(s->s3600, K_0, fbits(sc->d81027C), K_0, K_1);     /* 001FC03C..001FC07C */
    FAULT(w->euler(c, s->s3400, s->s3400, s->s3600));          /* 001FC078 */
    set_quad(s->s3600, K_0, K_0, K_1, K_1);                    /* 001FC084..001FC0A0 */
    FAULT(w->transform(c, s->s3600, s->s3400, s->s3600));      /* 001FC0B8 */
    memcpy(s->s3610, obj, 16);                                 /* 001FC0C0..001FC108: +B0..+BC */
    FAULT(w->vsub(c, s->s3610, s->s3610, sc->d8105D0));        /* 001FC104 */
    set_word(s->s3610, 1, 0);                                  /* 001FC124 */
    FAULT(w->normalize(c, s->s3610, s->s3610));                /* 001FC120 */
    uint32_t weight = K_1;                                     /* 001FC15C */
    if (em_ee_c_le_bits(dist, K_18))                           /* 001FC134 */
        weight = em_ee_mul_bits(K_1_18, dist);                 /* 001FC154 */
    if (sc->d28215B == 1) {                                    /* 001FC164 */
        int32_t v = em_player_float_to_int(scaled);            /* 001FC174 */
        *b = v;                                                /* 001FC17C */
        *a = v;                                                /* 001FC180 */
        *result = 1;
        return 0;
    }
    float t = 0.0f;
    FAULT(w->dot(c, s->s3600, s->s3610, &t));                  /* 001FC198 */
    uint32_t tb = fbits(t);
    uint32_t p = em_ee_mul_bits(tb, tb);                       /* 001FC1A0 */
    p = em_ee_mul_bits(p, p);                                  /* 001FC1A4 */
    uint32_t k = em_ee_mul_bits(tb, p);                        /* 001FC1A8 */
    k = em_ee_mul_bits(k, weight);                             /* 001FC1AC */
    uint32_t rest = em_ee_sub_bits(K_1, weight);               /* 001FC1D4 / 001FC1EC */
    if (em_ee_c_lt_bits(k, K_0))                               /* 001FC1B8 */
        k = em_ee_sub_bits(k, rest);                           /* 001FC1DC */
    else
        k = em_ee_add_bits(k, rest);                           /* 001FC1F0 */
    int32_t near_side = 0;
    FAULT(w->side(c, obj, sc->d8105D0, sc->d81027C, &near_side)); /* 001FC204: 001B1380 */
    if (near_side != 0) {
        *a = em_player_float_to_int(scaled);                   /* 001FC214 / 001FC224 */
        *b = em_player_float_to_int(em_ee_mul_bits(scaled, k)); /* 001FC21C / 001FC22C */
    } else {
        *b = em_player_float_to_int(scaled);                   /* 001FC234 / 001FC244 */
        *a = em_player_float_to_int(em_ee_mul_bits(scaled, k)); /* 001FC23C / 001FC248 */
    }
    *result = 1;                                               /* 001FC24C */
    return 0;
}

int em_player_misc_001FBF50(EmPlayerMiscHost *h, const float obj[4], int32_t *a, int32_t *b,
                            int32_t flat, float radius, float scale, int32_t *result)
{
    if (!obj || !a || !b || !result || !bound(h, N_GAIN, 1, 1)) return -1;
    return gain(h, obj, a, b, flat, fbits(radius), fbits(scale), result);
}

static int sound(EmPlayerMiscHost *h, const float obj[4], int32_t id, int32_t flat, uint32_t radius,
                 int32_t *result)
{
    int32_t a = 0, b = 0, in_range = 0;
    /* 001FBD50: 001FBF50(obj, &a, &b, flat, radius, 4096.0). */
    FAULT(gain(h, obj, &a, &b, flat, radius, K_4096, &in_range));
    if (in_range == 0) {
        *result = -1;
        return 0;
    }
    const EmPlayerMiscWorkers *w = h->workers;
    return w->submit(w->context, id, 0x1000, a, b, result);    /* 001FB9F0(id, 0x1000, a, b) */
}

int em_player_misc_001FBD50(EmPlayerMiscHost *h, const float obj[4], int32_t id, int32_t flat,
                            float radius, int32_t *result)
{
    if (!obj || !result || !bound(h, N_SOUND, 1, 1)) return -1;
    return sound(h, obj, id, flat, fbits(radius), result);
}

/* The sound the player routines play: 001FBD50(p, id, 0, 300.0). */
static int sound_300(EmPlayerMiscHost *h, EmPlayerLiveActor *actor, int32_t id)
{
    int32_t ignored = 0;
    return sound(h, field(actor, 0xB0), id, 0, K_300, &ignored);
}

/* ---- 00182250 ------------------------------------------------------------ */

int em_player_misc_00182250(EmPlayerMiscHost *h, EmPlayerLiveActor *a)
{
    if (!a || !bound(h, N_IDENTITY | N_EULER | N_TRANSLATE | N_TRANSFORM | N_MOVE | N_HIT |
                        N_ATAN2 | N_WRAP, 0, 1))
        return -1;
    const EmPlayerMiscWorkers *w = h->workers;
    EmPlayerMiscScratch *s = h->scratch;
    void *c = w->context;
    FAULT(w->identity(c, s->s36A0));                              /* 00182268 */
    FAULT(w->euler(c, s->s36A0, s->s36A0, field(a, 0xC0)));      /* 00182280 */
    FAULT(w->translate(c, s->s36A0, s->s36A0, field(a, 0xB0)));  /* 00182298 */
    set_quad(s->s38A0, K_0, K_20, K_5, K_1);                      /* 001822A4..001822C8 */
    FAULT(w->transform(c, s->s38B0, s->s36A0, s->s38A0));         /* 001822E0 */
    int32_t hit = 0;
    FAULT(w->move(c, a, s->s38B0, 6, &hit));                      /* 001822F4 */
    if (hit == 0) return 0;                                       /* 001822FC */
    uint32_t nz = 0, nx = 0;
    FAULT(w->hit_node_word(c, 0x2C, &nz));                        /* 0018230C */
    FAULT(w->hit_node_word(c, 0x24, &nx));                        /* 00182310 */
    float angle = 0.0f;
    FAULT(w->atan2(c, bfloat(em_ee_neg_bits(nz)), bfloat(nx), &angle)); /* 00182314 / 00182318 */
    s->s3A20 = angle;                                             /* 00182320 */
    float yaw = 0.0f;
    FAULT(w->wrap(c, bfloat(em_ee_add_bits(K_3PI_2, fbits(s->s3A20))), &yaw)); /* 00182338 / 0018233C */
    float error = 0.0f;
    FAULT(w->wrap(c, bfloat(em_ee_sub_bits(fbits(yaw), w32(a, 0xC4))), &error)); /* 00182348 / 0018234C */
    s->s3A24 = error;                                             /* 00182354 */
    uint32_t magnitude = fbits(error) & UINT32_C(0x7FFFFFFF);     /* 00182358: 0011DF78 */
    if (!em_ee_c_le_bits(magnitude, K_AIM)) return 0;             /* 00182370 */
    uint32_t n = 0, p = 0;
    FAULT(w->hit_node_word(c, 0x24, &n));                         /* 00182390 */
    FAULT(w->hit_point_word(c, 0, &p));                           /* 00182398: 0x700031B0 */
    set32(a, 0xB0, em_ee_add_bits(p, em_ee_mul_bits(K_1_5, n)));  /* 0018239C..001823A8 */
    FAULT(w->hit_node_word(c, 0x2C, &n));                         /* 001823B0 */
    FAULT(w->hit_point_word(c, 8, &p));                           /* 001823B8: 0x700031B8 */
    set32(a, 0xB8, em_ee_add_bits(p, em_ee_mul_bits(K_1_5, n)));  /* 001823BC..001823C4 */
    set32(a, 0xC4, fbits(yaw));                                   /* 001823C8 */
    return 0;
}

/* ---- 0017E250 / 0017E510 ------------------------------------------------- */

/* 0x700036D0 is row 3 of the 0x700036A0 matrix. */
#define S36D0(s) ((s)->s36A0 + 12)

/* One 0017E250 pass: x for both points, y 4.01, z 0 and 5.0 (w 1.0), both
 * through the actor matrix, then 0019AFE0(p, 38C0, 38D0, 7). */
static int ahead_pass(EmPlayerMiscHost *h, EmPlayerLiveActor *a, uint32_t x, int32_t *acc)
{
    const EmPlayerMiscWorkers *w = h->workers;
    EmPlayerMiscScratch *s = h->scratch;
    void *c = w->context;
    set_quad(s->s38A0, x, K_4_01, K_0, K_1);
    set_quad(s->s38B0, x, K_4_01, K_5, K_1);
    FAULT(w->transform(c, s->s38C0, s->s36A0, s->s38A0));
    FAULT(w->transform(c, s->s38D0, s->s36A0, s->s38B0));
    int32_t r = 0;
    FAULT(w->sweep(c, a, s->s38C0, s->s38D0, 7, &r));
    *acc |= r;
    return 0;
}

int em_player_misc_0017E250(EmPlayerMiscHost *h, EmPlayerLiveActor *a, const float v[4],
                            int32_t *result)
{
    if (!a || !v || !result ||
        !bound(h, N_IDENTITY | N_EULER | N_COLUMN | N_TRANSFORM | N_SWEEP, 0, 1))
        return -1;
    const EmPlayerMiscWorkers *w = h->workers;
    EmPlayerMiscScratch *s = h->scratch;
    void *c = w->context;
    int32_t acc = 0;                                               /* 0017E278 */
    FAULT(w->identity(c, s->s36A0));                               /* 0017E274 */
    FAULT(w->euler(c, s->s36A0, s->s36A0, field(a, 0xC0)));       /* 0017E28C */
    for (unsigned i = 0; i < 3; ++i) set_word(S36D0(s), i, word(v, i)); /* 0017E29C: 001031E0 */
    set_word(S36D0(s), 1, em_ee_add_bits(word(S36D0(s), 1), K_20_5));  /* 0017E2C0 / 0017E2D8 */
    int32_t column = 0;
    FAULT(w->column(c, a, S36D0(s), 1, bfloat(K_14), &column));    /* 0017E2D4 */
    if (column != 0) {                                             /* 0017E2DC */
        *result = 1;
        return 0;
    }
    FAULT(ahead_pass(h, a, K_0, &acc));                            /* 0017E2EC..0017E390 */
    FAULT(ahead_pass(h, a, K_M3, &acc));                           /* 0017E394..0017E43C */
    FAULT(ahead_pass(h, a, K_3, &acc));                            /* 0017E440..0017E4E8 */
    *result = acc;
    return 0;
}

int em_player_misc_0017E510(EmPlayerMiscHost *h, EmPlayerLiveActor *a, int32_t *result)
{
    if (!a || !result || !bound(h, N_IDENTITY | N_EULER | N_TRANSFORM | N_COLUMN, 0, 1)) return -1;
    const EmPlayerMiscWorkers *w = h->workers;
    EmPlayerMiscScratch *s = h->scratch;
    void *c = w->context;
    int32_t acc = 0;
    FAULT(w->identity(c, s->s36A0));
    FAULT(w->euler(c, s->s36A0, s->s36A0, field(a, 0xC0)));
    for (unsigned i = 0; i < 3; ++i) set_word(S36D0(s), i, w32(a, 0xB0 + 4 * i)); /* 001031E0 */
    static const uint32_t xs[3] = { K_0, K_M4_5, K_4_5 };
    for (unsigned i = 0; i < 3; ++i) {
        set_quad(s->s38A0, xs[i], K_24_51, K_3_62, K_1);
        FAULT(w->transform(c, s->s38B0, s->s36A0, s->s38A0));
        int32_t r = 0;
        FAULT(w->column(c, a, s->s38B0, 1, bfloat(K_9_99), &r));
        acc |= r;
    }
    *result = acc != 0 ? 1 : 0;
    return 0;
}

/* ---- 0017E7C0 ------------------------------------------------------------ */

typedef struct SideCall {
    EmPlayerMiscHost *h;
    EmPlayerLiveActor *a;
    int32_t side;
} SideCall;

static int edge(SideCall *k, uint32_t x, uint32_t y, int32_t *r)
{
    const EmPlayerMiscWorkers *w = k->h->workers;
    return w->edge(w->context, k->a, k->side, bfloat(x), bfloat(y), r);
}

/* The segment pair (x, y, z0, 1) and (x, y, z1, 1), x negated for side 0,
 * both through the actor's +D0 matrix into 38C0 / 38D0, then 0019AFE0. */
static int side_sweep(SideCall *k, uint32_t x_left, uint32_t x_right, uint32_t y, uint32_t z0,
                      uint32_t z1, uint32_t mask, int32_t *r)
{
    const EmPlayerMiscWorkers *w = k->h->workers;
    EmPlayerMiscScratch *s = k->h->scratch;
    void *c = w->context;
    uint32_t x = k->side == 0 ? x_left : x_right;
    set_quad(s->s38A0, x, y, z0, K_1);
    set_quad(s->s38B0, x, y, z1, K_1);
    FAULT(w->transform(c, s->s38C0, field(k->a, 0xD0), s->s38A0));
    FAULT(w->transform(c, s->s38D0, field(k->a, 0xD0), s->s38B0));
    return w->sweep(c, k->a, s->s38C0, s->s38D0, mask, r);
}

static int surface(SideCall *k, uint8_t *v)
{
    const EmPlayerMiscWorkers *w = k->h->workers;
    return w->hit_node_byte(w->context, 0x1A, v);
}

static int call_r(int (*fn)(void *, EmPlayerLiveActor *, int32_t *), SideCall *k, int32_t *r)
{
    return fn(k->h->workers->context, k->a, r);
}

/* 0017E7C0 with +D == 1. */
static int side_climb(SideCall *k, int32_t *result)
{
    const EmPlayerMiscWorkers *w = k->h->workers;
    EmPlayerLiveActor *a = k->a;
    int32_t r = 0;
    *result = 0;
    FAULT(edge(k, K_20, K_M5, &r)); if (r) return 0;
    FAULT(edge(k, K_10, K_M5, &r)); if (r) return 0;
    FAULT(edge(k, K_0, K_M5, &r)); if (r) return 0;
    int found = 0;
    FAULT(side_sweep(k, K_M4_5, K_4_5, K_20, K_0, K_2, 6, &r));
    if (r != 0) {
        uint8_t v = 0;
        FAULT(surface(k, &v));
        if (v == 0x3D) {
            set8(a, 0x1F1, 0);
            *result = 1;
            return 0;
        }
    } else {
        found = 1;
    }
    FAULT(side_sweep(k, K_M9, K_9, K_19_5, K_0, K_10, 7, &r));
    if ((r & 6) == 0) {
        *result = 0xA;
        return 0;
    }
    uint8_t v = 0;
    FAULT(surface(k, &v));
    if (v == 0x32 || v == 0x3B) {
        FAULT(call_r(w->grab, k, &r));
        if (r == 0) return 0;
        set8(a, 0x1F1, 1);
        set8(a, 0xD, v == 0x32 ? 0 : 1);
        *result = 2;
        return 0;
    }
    if (found == 0) return 0;
    const EmPlayerMiscScene *sc = k->h->scene;
    if (sc->d810700 == 8 && sc->d810701 == 3) {
        uint32_t f = w32(a, 0xB0);
        if (!em_ee_c_le_bits(f, K_120) && em_ee_c_lt_bits(f, K_130)) {
            f = w32(a, 0xB8);
            if (!em_ee_c_le_bits(f, K_160) && em_ee_c_lt_bits(f, K_170)) {
                set32(a, 0x218, w32(a, 0xC4));
                set32(a, 0x2E0, K_123_5);
                set32(a, 0x2E4, K_257_5);
                set32(a, 0x2E8, K_156_4);
                set8(a, 0x1F1, 5);
                *result = 2;
                return 0;
            }
        }
    }
    *result = 0xA;
    return 0;
}

int em_player_misc_0017E7C0(EmPlayerMiscHost *h, EmPlayerLiveActor *a, int32_t side, int32_t *result)
{
    if (!a || !result ||
        !bound(h, N_EDGE | N_TRANSFORM | N_SWEEP | N_HIT | N_GRABS | N_LEDGE_TOP | N_BLOCKED |
                  N_VADD, 1, 1))
        return -1;
    const EmPlayerMiscWorkers *w = h->workers;
    EmPlayerMiscScratch *s = h->scratch;
    void *c = w->context;
    SideCall k = { h, a, side };
    if (u8(a, 0xD) == 1) return side_climb(&k, result);

    int32_t r = 0;
    *result = 0;
    if (h->scene->d810700 != 0x11) {
        FAULT(call_r(w->reach, &k, &r));                           /* 001784E0(p) */
        if (r != 0) {
            set8(a, 0x1F1, 2);
            *result = 2;
            return 0;
        }
    }
    FAULT(side_sweep(&k, K_M9, K_9, K_20, K_M2, K_3_5, 7, &r));
    if (r & 6) {
        uint8_t v = 0;
        FAULT(surface(&k, &v));
        if (v == 0x32 || v == 0x3B) {
            FAULT(call_r(w->grab, &k, &r));                        /* 001782A0(p) */
            if (r != 0) {
                set8(a, 0x1F1, 1);
                set8(a, 0xD, v == 0x32 ? 0 : 1);
                *result = 2;
                return 0;
            }
        } else if (v == 0x33) {
            FAULT(call_r(w->grab_33, &k, &r));                     /* 00178440(p) */
            if (r != 0) {
                set8(a, 0x1F1, 6);
                *result = 2;
                return 0;
            }
        }
    }
    FAULT(w->blocked(c, a, side, &r));                             /* 0017F130(p, side) */
    if (r != 0) return 0;
    FAULT(edge(&k, K_20, K_M5, &r)); if (r) return 0;
    static const uint32_t chain[7][2] = {
        { K_12, K_M5 }, { K_4_01, K_M5 }, { K_M0_5, K_M5 },
        { K_20, K_0 }, { K_12, K_0 }, { K_4_01, K_0 }, { K_M0_5, K_0 },
    };
    for (unsigned i = 0; i < 7; ++i) {
        FAULT(edge(&k, chain[i][0], chain[i][1], &r));
        if (r) return 0;
    }
    FAULT(side_sweep(&k, K_M4_4, K_4_4, K_20, K_M2, K_4_5, 7, &r));
    if (r & 6) {
        FAULT(w->ledge_top(c, a, 1, &r));                          /* 00178910(p, 1) */
        if (r == 0) return 0;
        set8(a, 0x1F1, 0);
        *result = 1;
        return 0;
    }
    set_quad(s->s3910, side == 0 ? K_0_1 : K_M0_1, K_0, K_0, K_0);
    FAULT(w->transform(c, s->s3900, field(a, 0xD0), s->s3910));
    FAULT(w->vadd(c, s->s38E0, s->s38C0, s->s3900));
    FAULT(w->vadd(c, s->s38F0, s->s38D0, s->s3900));
    FAULT(w->sweep(c, a, s->s38E0, s->s38F0, 7, &r));
    *result = r != 0 ? 1 : 0xA;
    return 0;
}

/* ---- the side clip requests ----------------------------------------------- */

static int request(EmPlayerMiscHost *h, EmPlayerLiveActor *a, int32_t clip, float blend)
{
    const EmPlayerMiscWorkers *w = h->workers;
    return w->request(w->context, a, clip, 0, blend);
}

/* 0017DF70: side 0 -> 0x7E, else 0x7F; blend ($f12) passes through. */
int em_player_misc_0017DF70(EmPlayerMiscHost *h, EmPlayerLiveActor *a, int32_t side, float blend)
{
    if (!a || !bound(h, N_REQUEST, 0, 0)) return -1;
    return request(h, a, side == 0 ? 0x7E : 0x7F, blend);          /* 0017DF80 / 0017DF94 */
}

/* 0017DFB0: +D == 1 or 0017F1C0(p) == 0 sets +315 = 1 and requests
 * 0xCC / 0xCD; otherwise +315 = 0, 0x84 / 0x85 and sound 0x105 / 0x106.
 * The +315 store is in the request's delay slot, before the call. */
int em_player_misc_0017DFB0(EmPlayerMiscHost *h, EmPlayerLiveActor *a, int32_t side, float blend)
{
    if (!a || !bound(h, N_REQUEST | N_AHEAD | N_SOUND, 1, 1)) return -1;
    const EmPlayerMiscWorkers *w = h->workers;
    int right = side != 0;                                         /* 0017DFC4 */
    if (u8(a, 0xD) != 1) {                                         /* 0017DFD4 / 0017E050 */
        int32_t ahead = 0;
        FAULT(w->ahead(w->context, a, &ahead));                    /* 0017DFDC / 0017E058 */
        if (ahead != 0) {
            set8(a, 0x315, 0);                                     /* 0017E000 / 0017E07C */
            FAULT(request(h, a, right ? 0x85 : 0x84, blend));
            return sound_300(h, a, right ? 0x106 : 0x105);         /* 0017E014 / 0017E090 */
        }
    }
    set8(a, 0x315, 1);                                             /* 0017E030 / 0017E0AC */
    return request(h, a, right ? 0xCD : 0xCC, blend);              /* 0017E038 / 0017E0B4 */
}

static int side_315(EmPlayerMiscHost *h, EmPlayerLiveActor *a, int32_t side, float blend,
                    int32_t c0, int32_t c1, int32_t d0, int32_t d1)
{
    if (!a || !bound(h, N_REQUEST, 0, 0)) return -1;
    int plain = u8(a, 0x315) == 0;
    if (side == 0) return request(h, a, plain ? c0 : d0, blend);
    return request(h, a, plain ? c1 : d1, blend);
}

int em_player_misc_0017E0D0(EmPlayerMiscHost *h, EmPlayerLiveActor *a, int32_t side, float blend)
{
    return side_315(h, a, side, blend, 0x86, 0x87, 0xCE, 0xCF);
}

int em_player_misc_0017E150(EmPlayerMiscHost *h, EmPlayerLiveActor *a, int32_t side, float blend)
{
    return side_315(h, a, side, blend, 0x88, 0x89, 0xD2, 0xD3);
}

int em_player_misc_0017E1D0(EmPlayerMiscHost *h, EmPlayerLiveActor *a, int32_t side, float blend)
{
    return side_315(h, a, side, blend, 0x81, 0x82, 0xD0, 0xD1);
}

/* 0017FF80: the clip is 00188570(p) for +2F1 == 0, else 00188590(p); the
 * full v0 is passed on as the clip. */
int em_player_misc_0017FF80(EmPlayerMiscHost *h, EmPlayerLiveActor *a, float blend)
{
    if (!a || !bound(h, N_CLIPS | N_REQUEST, 0, 0)) return -1;
    const EmPlayerMiscWorkers *w = h->workers;
    int32_t clip = 0;
    if (u8(a, 0x2F1) == 0)                                         /* 0017FF90 / 0017FF98 */
        FAULT(w->clip_2F1_0(w->context, a, &clip));                /* 0017FFA0 */
    else
        FAULT(w->clip_2F1_1(w->context, a, &clip));                /* 0017FFC4 */
    return request(h, a, clip, blend);                             /* 0017FFB4 / 0017FFD8 */
}

/* 00182AF0: 001FBD50(p, 00179B90(p) + 0x100, 0, 300.0). */
int em_player_misc_00182AF0(EmPlayerMiscHost *h, EmPlayerLiveActor *a)
{
    if (!a || !bound(h, N_SOUND_BASE | N_SOUND, 1, 1)) return -1;
    const EmPlayerMiscWorkers *w = h->workers;
    int32_t base = 0;
    FAULT(w->sound_base(w->context, a, &base));                    /* 00182AFC */
    return sound_300(h, a, (int32_t)((uint32_t)base + 0x100u));    /* 00182B04..00182B14 */
}

/* ---- 00177B80 ------------------------------------------------------------ */

int em_player_misc_00177B80(EmPlayerMiscHost *h, EmPlayerLiveActor *a,
                            const EmPlayerRecoveryLedge *ledge, float y, int32_t *result)
{
    if (!a || !ledge || !result || !bound(h, N_TRANSFORM | N_VADD | N_GROUND, 0, 1)) return -1;
    const EmPlayerMiscWorkers *w = h->workers;
    EmPlayerMiscScratch *s = h->scratch;
    void *c = w->context;
    /* 00177B90..00177C04: A0 = [3050] + 1.5 * [3060], A8 = [3058] + 1.5 * [3068]. */
    uint32_t ax = em_ee_add_bits(fbits(ledge->point[0]), em_ee_mul_bits(K_1_5, fbits(ledge->normal[0])));
    uint32_t az = em_ee_add_bits(fbits(ledge->point[2]), em_ee_mul_bits(K_1_5, fbits(ledge->normal[2])));
    set_quad(s->s38B0, K_M4_5, K_M20_5, K_0, K_0);                /* 00177BD0..00177BF0 */
    set_word(s->s38A0, 0, ax);                                     /* 00177BF8 */
    set_word(s->s38A0, 2, az);                                     /* 00177C14 */
    set_word(s->s38A0, 1, fbits(y));                               /* 00177C1C */
    set_word(s->s38A0, 3, K_1);                                    /* 00177C28 */
    float tmp[4];
    FAULT(w->transform(c, tmp, ledge->matrix, s->s38B0));          /* 00177C24 */
    FAULT(w->vadd(c, tmp, tmp, s->s38A0));                         /* 00177C38 */
    int32_t r = 0;
    FAULT(w->ground(c, a, tmp, 6, &r));                            /* 00177C4C */
    if (r != 0) {
        *result = 1;                                               /* 00177C60 */
        return 0;
    }
    set_quad(s->s38B0, K_4_5, K_M20_5, K_0, K_0);                 /* 00177C6C..00177CA0 */
    FAULT(w->transform(c, tmp, ledge->matrix, s->s38B0));          /* 00177C9C */
    FAULT(w->vadd(c, tmp, tmp, s->s38A0));                         /* 00177CB0 */
    FAULT(w->ground(c, a, tmp, 6, &r));                            /* 00177CC4 */
    *result = r != 0 ? 1 : 0;                                      /* 00177CD0 (movz) */
    return 0;
}

/* ---- 0021E650 ------------------------------------------------------------ */

int em_player_misc_0021E650(EmPlayerMiscHost *h, EmPlayerLiveActor *a)
{
    if (!a || !bound(h, N_CUE | N_LAND_SOUND | N_0021D490 | N_SOUND, 1, 1)) return -1;
    const EmPlayerMiscWorkers *w = h->workers;
    void *c = w->context;
    static const uint32_t limit[5] = { K_173, K_105, K_85, K_60, K_45 };
    uint8_t state = u8(a, 7);
    if (state > 4) return 0;                                       /* no case: nothing */
    if (!em_ee_c_le_bits(w32(a, 0x3C), limit[state])) return 0;
    set8(a, 7, (uint8_t)(state + 1));
    if (state == 4) {
        FAULT(w->cue(c, 0, 0xD0, 0xA, 1));
        return w->w0021D490(c, a);
    }
    FAULT(w->cue(c, 0, 0xC0, 5, 1));
    if (state == 0) return w->land_sound(c, a, 1);                 /* 00182870(p, 1) */
    if (state == 3) return sound_300(h, a, 0x156);
    return 0;
}

/* ---- 0015C1F0 ------------------------------------------------------------ */

int em_player_misc_0015C1F0(EmPlayerMiscHost *h, EmPlayerLiveActor *a)
{
    if (!a || !bound(h, N_MODEL, 1, 0)) return -1;
    const EmPlayerMiscWorkers *w = h->workers;
    const EmPlayerMiscScene *sc = h->scene;
    void *c = w->context;
    uint8_t mode = u8(a, 0x234), kind;                             /* 0015C1FC */
    if (mode == 0 || mode == 1) {
        uint8_t sel = sc->d810C60;                                 /* 0015C20C / 0015C26C */
        if (sel == 0) kind = mode == 0 ? 0x3B : 0x40;
        else if (sel == 2) kind = 0x3F;
        else if (sel == 1) kind = 0x3E;
        else kind = mode == 0 ? 0x3B : 0x40;
    } else {
        kind = 0x3D;
    }
    /* 0015C2C0: the table index is the +2FF byte just stored; an index
     * outside the native table view faults before any write. */
    if (!sc->d28A490 || kind >= sc->d28A490_count) return -1;
    set8(a, 0x2FF, kind);
    FAULT(w->bind_model(c, a, sc->d28A490[u8(a, 0x2FF)]));         /* 0015C2D8 */
    uint8_t count = 0;
    FAULT(w->bone_count(c, w32(a, 0x44), &count));                 /* 0015C2E0 / 0015C2E4 */
    set8(a, 0xC, count);                                           /* 0015C2E8 */
    a->bytes[0x96] = 0x28;                                         /* 0015C2F4: halfword 0x28 */
    a->bytes[0x97] = 0;
    return w->w00200890(c);                                        /* 0015C2F0 */
}

/* ---- 001EFE00 ------------------------------------------------------------ */

int em_player_misc_001EFE00(EmPlayerMiscHost *h, uint32_t id, EmPlayerLiveActor *a, uint32_t *node)
{
    if (!a || !node || !bound(h, N_SPAWN, 0, 0)) return -1;
    const EmPlayerMiscWorkers *w = h->workers;
    float pos[4];
    memcpy(pos, a->bytes + 0xB0, 16);                              /* 001EFE20: 00102948(sp+30, p+B0) */
    if (id == UINT32_C(0x80000027))                                /* 001EFE2C */
        set_word(pos, 1, em_ee_add_bits(word(pos, 1), K_10));      /* 001EFE3C..001EFE4C */
    EmPlayerMiscEffectView view = { NULL, NULL, NULL };
    uint32_t spawned = 0;
    FAULT(w->spawn(w->context, id, pos, bfloat(K_1), &spawned, &view)); /* 001EFE60 */
    *node = spawned;
    if (spawned == 0) return 0;                                    /* 001EFE6C */
    if (!view.w24 || !view.pos || !view.rot) return -1;
    *view.w24 = w32(a, 0x14);                                      /* 001EFE70 / 001EFE80 */
    memcpy(view.pos, a->bytes + 0xB0, 16);                         /* 001EFE7C: 00102948 */
    memcpy(view.rot, a->bytes + 0xC0, 16);                         /* 001EFE88: 00102948 */
    return 0;
}

/* ---- 00122BB8 ------------------------------------------------------------ */

/* 00122BB8: the state word at (*D_0024295C) + 0x58 becomes state *
 * 0x41C64E6D + 0x3039; the new state & 0x7FFFFFFF is returned. The one
 * state is em_random.c's. */
int em_player_misc_random(void *context, uint32_t *value)
{
    (void)context;
    if (!value) return -1;
    *value = em_random_next();
    return 0;
}

int em_player_misc_random_i32(void *context, int32_t *value)
{
    (void)context;
    if (!value) return -1;
    *value = (int32_t)em_random_next();
    return 0;
}

/* ---- adapters -------------------------------------------------------------- */

int em_player_misc_w_sound(void *context, EmPlayerLiveActor *actor, int id, int flags, float range)
{
    int32_t ignored = 0;
    return em_player_misc_001FBD50(context, actor ? field(actor, 0xB0) : NULL, id, flags, range,
                                   &ignored);
}

int em_player_misc_w_sound_300(void *context, EmPlayerLiveActor *actor, unsigned id)
{
    return em_player_misc_w_sound(context, actor, (int)id, 0, bfloat(K_300));
}

int em_player_misc_w_sound_300_i(void *context, EmPlayerLiveActor *actor, int id)
{
    return em_player_misc_w_sound(context, actor, id, 0, bfloat(K_300));
}

int em_player_misc_w_sound_300_handle(void *context, EmPlayerLiveActor *actor, int id,
                                      int *handle)
{
    int32_t v0 = 0;
    if (!handle) return -1;
    FAULT(em_player_misc_001FBD50(context, actor ? field(actor, 0xB0) : NULL, id, 0, bfloat(K_300),
                                  &v0));
    *handle = v0;
    return 0;
}

int em_player_misc_w_001FBF50(void *context, const float pos[4], float f12, float f13, int32_t *a,
                              int32_t *b, int32_t *result)
{
    return em_player_misc_001FBF50(context, pos, a, b, 0, f12, f13, result);
}

int em_player_misc_w_aim_track(void *context, EmPlayerLiveActor *actor)
{
    return em_player_misc_00182250(context, actor);
}

int em_player_misc_w_ledge_ahead_self(void *context, EmPlayerLiveActor *actor, int *result)
{
    int32_t r = 0;
    FAULT(em_player_misc_0017E250(context, actor, actor ? field(actor, 0xB0) : NULL, &r));
    *result = r;
    return 0;
}

int em_player_misc_w_ledge_ahead(void *context, EmPlayerLiveActor *actor, const float v[4], int *result)
{
    int32_t r = 0;
    FAULT(em_player_misc_0017E250(context, actor, v, &r));
    *result = r;
    return 0;
}

int em_player_misc_w_ledge_above(void *context, EmPlayerLiveActor *actor, int *result)
{
    int32_t r = 0;
    FAULT(em_player_misc_0017E510(context, actor, &r));
    *result = r;
    return 0;
}

int em_player_misc_w_ledge_side(void *context, EmPlayerLiveActor *actor, int side, int *result)
{
    int32_t r = 0;
    FAULT(em_player_misc_0017E7C0(context, actor, side, &r));
    *result = r;
    return 0;
}

int em_player_misc_w_clip_DF70(void *context, EmPlayerLiveActor *actor, int side, float blend)
{
    return em_player_misc_0017DF70(context, actor, side, blend);
}

int em_player_misc_w_clip_DFB0(void *context, EmPlayerLiveActor *actor, int side, float blend)
{
    return em_player_misc_0017DFB0(context, actor, side, blend);
}

int em_player_misc_w_clip_E0D0(void *context, EmPlayerLiveActor *actor, int side, float blend)
{
    return em_player_misc_0017E0D0(context, actor, side, blend);
}

int em_player_misc_w_clip_E150(void *context, EmPlayerLiveActor *actor, int side, float blend)
{
    return em_player_misc_0017E150(context, actor, side, blend);
}

int em_player_misc_w_clip_E1D0(void *context, EmPlayerLiveActor *actor, int side, float blend)
{
    return em_player_misc_0017E1D0(context, actor, side, blend);
}

int em_player_misc_w_clip_FF80(void *context, EmPlayerLiveActor *actor, float blend)
{
    return em_player_misc_0017FF80(context, actor, blend);
}

int em_player_misc_w_sound_100(void *context, EmPlayerLiveActor *actor)
{
    return em_player_misc_00182AF0(context, actor);
}

int em_player_misc_w_depth(void *context, EmPlayerLiveActor *actor,
                           const EmPlayerRecoveryLedge *ledge, float y, int *result)
{
    int32_t r = 0;
    FAULT(em_player_misc_00177B80(context, actor, ledge, y, &r));
    *result = r;
    return 0;
}

int em_player_misc_w_0021E650(void *context, EmPlayerLiveActor *actor)
{
    return em_player_misc_0021E650(context, actor);
}

int em_player_misc_w_0015C1F0(void *context, EmPlayerLiveActor *actor)
{
    return em_player_misc_0015C1F0(context, actor);
}

int em_player_misc_w_001EFE00(void *context, uint32_t id, EmPlayerLiveActor *actor)
{
    uint32_t node = 0;
    return em_player_misc_001EFE00(context, id, actor, &node);
}

int em_player_misc_w_attach(void *context, EmPlayerLiveActor *actor, uint32_t id, uint32_t *handle)
{
    if (!handle) return -1;
    return em_player_misc_001EFE00(context, id, actor, handle);
}
