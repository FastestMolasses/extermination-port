/* em_player_recovery.c - player helpers shared by the fall, slide, climb and
 * hang states (see em_player_recovery.h, docs/PLAYER_RECOVERY.md).
 *
 * Read from the original instructions of each routine (the split listing
 * under ../Extermination/build/asm; the decomp C of 00178B90, 002243F0 and
 * the NEARMISS 0017C860 / 0017D080 / 001751A0 / 00162A40 was checked against
 * it, and 00224B80 / 00178EC0 are byte-matched C). Every float operation is
 * the instruction's, in its operand order, through em_ee_float.h. The comment
 * on each block names the original address of the branch or store it
 * translates. tools/test_player_recovery_reference.py executes the original
 * instructions and compares every record byte, scratch word and worker call. */
#include "game/em_player_recovery.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

typedef uint32_t F;   /* a binary32 bit pattern */

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

/* Constants as the instructions load them (upper-half immediate, low half OR'd in). */
#define K_ZERO      UINT32_C(0x00000000)
#define K_ONE       UINT32_C(0x3F800000)
#define K_HALF      UINT32_C(0x3F000000)
#define K_TWO       UINT32_C(0x40000000)
#define K_THREE     UINT32_C(0x40400000)
#define K_FOUR      UINT32_C(0x40800000)
#define K_MFOUR     UINT32_C(0xC0800000)
#define K_4_5       UINT32_C(0x40900000)
#define K_1_5       UINT32_C(0x3FC00000)
#define K_2_8       UINT32_C(0x40333333)
#define K_4_01      UINT32_C(0x408051EC)
#define K_5_5       UINT32_C(0x40B00000)
#define K_8         UINT32_C(0x41000000)
#define K_M8        UINT32_C(0xC1000000)
#define K_9_5       UINT32_C(0x41180000)
#define K_11        UINT32_C(0x41300000)
#define K_16        UINT32_C(0x41800000)
#define K_18        UINT32_C(0x41900000)
#define K_20_5      UINT32_C(0x41A40000)
#define K_32        UINT32_C(0x42000000)
#define K_100       UINT32_C(0x42C80000)
#define K_115       UINT32_C(0x42E60000)
#define K_256       UINT32_C(0x43800000)
#define K_270       UINT32_C(0x43870000)
#define K_300       UINT32_C(0x43960000)
#define K_340       UINT32_C(0x43AA0000)
#define K_M0_2      UINT32_C(0xBE4CCCCD)
#define K_PI        UINT32_C(0x40490FDB)
#define K_TWO_PI    UINT32_C(0x40C90FDB)
#define K_HALF_PI   UINT32_C(0x3FC90FDB)
#define K_QUARTER_PI UINT32_C(0x3F490FDB)
#define K_3PI_4     UINT32_C(0x4016CBE4)
#define K_PI_9      UINT32_C(0x3EB2B8C3)   /* 0.34906584 */
#define K_PI_5      UINT32_C(0x3F20D97C)   /* 0.62831855 */
#define K_0_6PI     UINT32_C(0x3FF1463A)   /* the double 0x3FFE28C740000000 */

/* ---- EE arithmetic in instruction form ---------------------------------- */
static F add(F a, F b) { return em_ee_add_bits(a, b); }
static F sub(F a, F b) { return em_ee_sub_bits(a, b); }
static F mul(F a, F b) { return em_ee_mul_bits(a, b); }
static F dvd(F a, F b) { return em_ee_div_bits(a, b); }
static F neg(F a) { return em_ee_neg_bits(a); }
static int lt(F a, F b) { return em_ee_c_lt_bits(a, b); }
static int le(F a, F b) { return em_ee_c_le_bits(a, b); }
static int eq(F a, F b) { return em_ee_c_eq_bits(a, b); }
/* ACC = x*x, then d = ACC + z*z through the EE float accumulator */
static F sum_squares(F x, F z) { return em_ee_madd_bits(em_ee_mula_bits(x, x), z, z); }

static F bits_of(float v) { return em_ee_bits(v); }
static float float_of(F b) { return em_ee_float(b); }

/* ---- the record ---------------------------------------------------------- */
static F ld(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void st(EmPlayerLiveActor *a, unsigned at, F v) { em_live_set_u32(a, at, v); }
static unsigned u8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void st8(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u8(a, at, (uint8_t)v); }
static void st16(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u16(a, at, (uint16_t)v); }

static void vec_to_float(const F in[4], float out[4])
{
    for (int i = 0; i < 4; ++i) out[i] = float_of(in[i]);
}

/* ---- inline leaves --------------------------------------------------------- */

/* 0011DF78: fabs (clear the sign bit of the raw word). */
static F fabs_0011DF78(F x) { return x & UINT32_C(0x7FFFFFFF); }

/* 001B1470: while (a > pi) a -= 2pi; while (a <= -pi) a += 2pi.
 * If a step leaves `a` unchanged the original can never leave the loop (it
 * would hang); that input is a fault here instead of a hang. */
static int wrap_001B1470(F a, F *out)
{
    while (lt(K_PI, a)) {
        F next = sub(a, K_TWO_PI);
        if (next == a) return -1;
        a = next;
    }
    while (le(a, neg(K_PI))) {
        F next = add(a, K_TWO_PI);
        if (next == a) return -1;
        a = next;
    }
    *out = a;
    return 0;
}

/* float_to_int (001281C0): the soft-float unpack 001278C0, then NaN and zero
 * give 0, infinity saturates by sign, a negative exponent (denormals
 * included) gives 0, an exponent >= 31 saturates by sign, else the fraction
 * (implicit bit at 30) is shifted right by 30 - exponent and negated for a
 * set sign: truncation toward zero. */
static int32_t float_to_int(F b)
{
    unsigned e = (b >> 23) & 0xFFu;
    uint32_t frac = b & UINT32_C(0x7FFFFF);
    int negative = (b >> 31) != 0;
    if (e == 0xFFu) {
        if (frac) return 0;
        return negative ? INT32_MIN : INT32_MAX;
    }
    if (e == 0) return 0;
    int exponent = (int)e - 127;
    if (exponent < 0) return 0;
    if (exponent >= 31) return negative ? INT32_MIN : INT32_MAX;
    uint32_t magnitude = ((frac | UINT32_C(0x800000)) << 7) >> (30 - exponent);
    return negative ? (int32_t)(0u - magnitude) : (int32_t)magnitude;
}

/* 001026A0(out, M, v): out = M[0]*v.x + M[1]*v.y + M[2]*v.z + M[3]*v.w over
 * all four lanes, summed in the VU0 accumulator (first product into ACC, the
 * next two added into ACC, the last added straight into out). */
static int mat_apply(const F m[16], const F v[4], F out[4])
{
    F acc[4] = { 0, 0, 0, 0 };
    if (em_vu_vec_bits(EM_VU_MULABC, 15, 0, m, v, 0, NULL, acc) != EM_EE_FLOAT_OK) return -1;
    if (em_vu_vec_bits(EM_VU_MADDABC, 15, 1, m + 4, v, 0, acc, acc) != EM_EE_FLOAT_OK) return -1;
    if (em_vu_vec_bits(EM_VU_MADDABC, 15, 2, m + 8, v, 0, acc, acc) != EM_EE_FLOAT_OK) return -1;
    if (em_vu_vec_bits(EM_VU_MADDBC, 15, 3, m + 12, v, 0, acc, out) != EM_EE_FLOAT_OK) return -1;
    return 0;
}

/* 001028B8(out, a, b): out.xyzw = a + b (one VU0 add). */
static int vec_add(const F a[4], const F b[4], F out[4])
{
    return em_vu_vec_bits(EM_VU_ADD, 15, EM_VU_NO_BC, a, b, 0, NULL, out) == EM_EE_FLOAT_OK ? 0 : -1;
}

/* The record's +D0 world matrix. */
static void record_matrix(const EmPlayerLiveActor *a, F m[16])
{
    for (int i = 0; i < 16; ++i) m[i] = ld(a, 0xD0 + 4u * (unsigned)i);
}

static void ledge_matrix(const EmPlayerRecoveryLedge *l, F m[16])
{
    for (int i = 0; i < 16; ++i) m[i] = bits_of(l->matrix[i]);
}

/* 0017D040(owner): (owner+2 & 0x1F) == 4 and owner+3 == 2. */
static int owner_0017D040(const EmPlayerProbeHit *hit)
{
    return (hit->entity_flags & 0x1Fu) == 4u && hit->entity_type == 2u;
}

/* 00128350(x) (the soft-float float -> double) then 001000C0(d, 0x3FFE28C7
 * 40000000), which returns 001274B0(d, c) < 0. The fp-bit compare returns 1
 * for a NaN operand (not less); otherwise the exact values compare, and the
 * constant is the float 0x3FF1463A exactly. */
static int less_than_0_6pi(F x)
{
    if (((x >> 23) & 0xFFu) == 0xFFu && (x & UINT32_C(0x7FFFFF))) return 0;
    if (x & UINT32_C(0x80000000)) return 1;              /* -0 and negatives */
    return x < K_0_6PI;                                   /* non-negative order */
}

int32_t em_player_recovery_float_to_int(uint32_t x) { return float_to_int(x); }
int em_player_recovery_wrap(uint32_t angle, uint32_t *out) { return out ? wrap_001B1470(angle, out) : -1; }
int em_player_recovery_below_0_6pi(uint32_t x) { return less_than_0_6pi(x); }

/* ---- worker calls ---------------------------------------------------------- */

static F w_sin(const EmPlayerRecoveryWorkers *w, F x) { return bits_of(w->sine(w->context, float_of(x))); }
static F w_cos(const EmPlayerRecoveryWorkers *w, F x) { return bits_of(w->cosine(w->context, float_of(x))); }
static F w_sqrt(const EmPlayerRecoveryWorkers *w, F x) { return bits_of(w->sqrt(w->context, float_of(x))); }

static int w_request(const EmPlayerRecoveryWorkers *w, EmPlayerLiveActor *a, int clip, F blend)
{
    return w->request(w->context, a, clip, 0, float_of(blend));
}

static int w_move(const EmPlayerRecoveryWorkers *w, EmPlayerLiveActor *a, const F target[4],
                  EmPlayerProbeHit *hit, int *kind)
{
    float t[4];
    vec_to_float(target, t);
    memset(hit, 0, sizeof *hit);
    int r = w->move(w->context, a, t, 7, hit);
    if (r < 0) return -1;
    *kind = r;
    return 0;
}

static int w_table(const EmPlayerRecoveryWorkers *w, const F at[4], EmPlayerClimbTable *t)
{
    float v[4];
    vec_to_float(at, v);
    memset(t, 0, sizeof *t);
    FAULT(w->table(w->context, v, t));
    if (t->count < 0 || t->count > EM_PLAYER_CLIMB_TABLE_MAX) return -1;
    return 0;
}

/* ---- 00178B90(p, probe) ----------------------------------------------------- */

static int need_translate(const EmPlayerRecoveryWorkers *w)
{
    return w && w->sine && w->cosine && w->probes;
}

static int translate(EmPlayerLiveActor *a, int probe, const EmPlayerRecoveryWorkers *w)
{
    F scale = K_ONE;                                                   /* 00178BBC $f20 */
    /* 00178BC8/00178BD8: +25F == 0 and +23B == 0x35 */
    if (u8(a, 0x25F) == 0 && u8(a, 0x23B) == 0x35) {
        F turn = fabs_0011DF78(sub(ld(a, 0x310), ld(a, 0xC4)));        /* 00178BE8 */
        if (lt(turn, K_HALF_PI)) {                                     /* 00178C04 */
            F c = w_cos(w, ld(a, 0x9C));                               /* 00178C14 */
            F q = dvd(sub(K_ONE, c), K_HALF_PI);                       /* 00178C28/00178C3C */
            scale = mul(turn, q);                                      /* 00178C4C */
            F c2 = w_cos(w, ld(a, 0x9C));                              /* 00178C48 */
            scale = add(c2, scale);                                    /* 00178C50 */
        }
    }
    F speed = fabs_0011DF78(ld(a, 0x38));                              /* 00178C54 */
    if (lt(speed, K_4_5)) {                                            /* 00178C68 */
        if (u8(a, 0x25F) == 0) {                                       /* 00178C7C */
            F s = w_sin(w, ld(a, 0xC4));
            st(a, 0xB0, add(ld(a, 0xB0), mul(mul(ld(a, 0x38), scale), s)));   /* 00178CA0 */
            F c = w_cos(w, ld(a, 0xC4));
            st(a, 0xB8, add(ld(a, 0xB8), mul(mul(ld(a, 0x38), scale), c)));   /* 00178CC4 */
        } else {
            F s = w_sin(w, ld(a, 0xC4));
            st(a, 0xB0, add(ld(a, 0xB0), mul(ld(a, 0x38), s)));                /* 00178CE0 */
            F c = w_cos(w, ld(a, 0xC4));
            st(a, 0xB8, add(ld(a, 0xB8), mul(ld(a, 0x38), c)));                /* 00178CFC */
        }
        if (probe) FAULT(w->probes(w->context, a));                    /* 00178D00/00178D08 */
        return 0;
    }
    /* 00178D18: the remainder is walked in steps of +-4.0. */
    F rest = ld(a, 0x38);
    F step = lt(rest, K_ZERO) ? K_MFOUR : K_FOUR;                      /* 00178D24 */
    int32_t count = float_to_int(dvd(rest, step));                     /* 00178D54/00178D60 */
    if (0 < count) {                                                   /* 00178D6C */
        F scaled = mul(step, scale);                                   /* 00178D78 */
        for (int32_t i = 0; i < count; ++i) {
            if (u8(a, 0x25F) == 0) {                                   /* 00178D80 */
                F s = w_sin(w, ld(a, 0xC4));
                st(a, 0xB0, add(ld(a, 0xB0), mul(scaled, s)));        /* 00178D9C */
                F c = w_cos(w, ld(a, 0xC4));
                st(a, 0xB8, add(ld(a, 0xB8), mul(scaled, c)));        /* 00178DB8 */
            } else {
                F s = w_sin(w, ld(a, 0xC4));
                st(a, 0xB0, add(ld(a, 0xB0), mul(step, s)));          /* 00178DD0 */
                F c = w_cos(w, ld(a, 0xC4));
                st(a, 0xB8, add(ld(a, 0xB8), mul(step, c)));          /* 00178DE8 */
            }
            rest = sub(rest, step);                                    /* 00178DF4 */
            if (probe) FAULT(w->probes(w->context, a));                /* 00178DF8 */
        }
    }
    if (u8(a, 0x25F) == 0) {                                           /* 00178E14 */
        F s = w_sin(w, ld(a, 0xC4));
        F part = mul(rest, scale);                                     /* 00178E24 */
        st(a, 0xB0, add(ld(a, 0xB0), mul(part, s)));                  /* 00178E34 */
        F c = w_cos(w, ld(a, 0xC4));
        st(a, 0xB8, add(ld(a, 0xB8), mul(part, c)));                  /* 00178E50 */
    } else {
        F s = w_sin(w, ld(a, 0xC4));
        st(a, 0xB0, add(ld(a, 0xB0), mul(rest, s)));                  /* 00178E68 */
        F c = w_cos(w, ld(a, 0xC4));
        st(a, 0xB8, add(ld(a, 0xB8), mul(rest, c)));                  /* 00178E80 */
    }
    if (probe) FAULT(w->probes(w->context, a));                        /* 00178E8C */
    return 0;
}

int em_player_recovery_translate(EmPlayerLiveActor *a, int probe, const EmPlayerRecoveryWorkers *w)
{
    if (!a || !need_translate(w)) return -1;
    return translate(a, probe, w);
}

/* ---- 00178EC0(p) ------------------------------------------------------------- */

/* D_00248730, indexed by the gait byte +23F (0.0, 0.1, 0.25, 0.5). */
static const F kStrafe[4] = { UINT32_C(0x00000000), UINT32_C(0x3DCCCCCD), UINT32_C(0x3E800000),
                              UINT32_C(0x3F000000) };

int em_player_recovery_strafe(EmPlayerLiveActor *a)
{
    if (!a) return -1;
    int32_t mode = (int32_t)ld(a, 0x24C);                              /* 00178ED0 */
    if (mode != 2 && mode != 3) return 0;
    unsigned gait = u8(a, 0x23F);
    if (gait > 3) return -1;
    F x;
    if (mode == 2) x = mul(ld(a, 0x38), neg(kStrafe[gait]));           /* 00178F0C/00178F10 */
    else x = mul(ld(a, 0x38), kStrafe[gait]);                          /* 00178FA0 */
    const F local[4] = { x, 0, 0, 0 };                                 /* 0x700038A0..AC */
    F m[16], out[4];
    record_matrix(a, m);
    FAULT(mat_apply(m, local, out));                                   /* 00178F34 */
    st(a, 0xB0, add(ld(a, 0xB0), out[0]));                             /* 00178F50 */
    st(a, 0xB8, add(ld(a, 0xB8), out[2]));                             /* 00178F64 */
    return 0;
}

/* ---- 001751A0(p) ------------------------------------------------------------- */

int em_player_recovery_stick_quadrant(EmPlayerLiveActor *a, const EmPlayerRecoveryScene *s,
                                      EmPlayerRecoveryScratch *x, const EmPlayerRecoveryWorkers *w)
{
    if (!a || !s || !x || !w || !w->cosine || !w->atan2) return -1;
    if (s->spad3B8D) return 0;                                         /* 001751B8 */
    st8(a, 0x23F, s->pad_gait);                                        /* 001751C8 */
    if (u8(a, 0x23F) == 0) {                                           /* 001751D0 */
        st(a, 0x24C, UINT32_C(0xFFFFFFFF));                            /* 001751E0 */
        return 0;
    }
    F y = dvd(em_ee_cvt_s_w_bits(s->pad_y), K_256);                    /* 001751FC/00175228 */
    F angle_y = mul(K_PI, y);                                          /* 00175240 */
    F xx = dvd(em_ee_cvt_s_w_bits(s->pad_x), K_256);                   /* 0017524C/00175278 */
    st(a, 0x244, w_cos(w, mul(K_PI, xx)));                             /* 0017528C/00175290 */
    F c = w_cos(w, angle_y);                                           /* 00175294 */
    st(a, 0x248, c);                                                   /* 0017529C */
    F r = bits_of(w->atan2(w->context, float_of(neg(c)), float_of(ld(a, 0x244))));   /* 001752A4 */
    F heading;
    FAULT(wrap_001B1470(add(add(K_PI, r), bits_of(s->camera_yaw)), &heading));       /* 001752C4 */
    F relative;
    FAULT(wrap_001B1470(sub(heading, ld(a, 0xC4)), &relative));        /* 001752D0 */
    x->spad3A20 = float_of(relative);                                  /* 001752DC */
    F m = fabs_0011DF78(relative);                                     /* 001752E0 */
    x->spad3A24 = float_of(m);                                         /* 00175304 */
    if (le(m, K_QUARTER_PI)) {                                         /* 001752F8 */
        st(a, 0x24C, 0);                                               /* 0017530C */
    } else if (!lt(m, K_3PI_4)) {                                      /* 00175320 */
        st(a, 0x24C, 1);                                               /* 00175338 */
    } else {
        st(a, 0x24C, lt(bits_of(x->spad3A20), K_ZERO) ? 2u : 3u);      /* 0017534C */
    }
    return 0;
}

/* ---- 002243F0(p) ------------------------------------------------------------- */

int em_player_recovery_react_002243F0(EmPlayerLiveActor *a, EmPlayerRecoveryScratch *x,
                                      const EmPlayerRecoveryWorkers *w, int *result)
{
    if (!a || !x || !w || !result || !w->sound || !w->react_0021C350 || !w->react_0021C270 ||
        !w->shake || !w->request || !w->clip_frames || !w->arbiter)
        return -1;
    unsigned sub_state = u8(a, 7);                                     /* 002243FC */
    *result = 1;
    if (sub_state == 1) {                                              /* 00224404 */
        if (!(ld(a, 0x200) & 0x1000u)) return 0;                       /* 00224514 */
        st8(a, 7, 0);                                                  /* 00224520 */
        st16(a, 0x20E, 0x3C);                                          /* 00224524 */
        int clip = u8(a, 0x25C) < 2 ? 0x6C : 0x6B;                     /* 00224530 */
        int frames = 0;
        FAULT(w->clip_frames(w->context, a, clip, &frames));           /* 0022453C/00224594 */
        F length = em_ee_cvt_s_w_bits(em_ee_int_word(frames));         /* 0022454C */
        x->spad3A20 = float_of(length);                                /* 00224560 */
        F frame = dvd(bits_of(x->spad3A20), K_TWO);                    /* 00224574 */
        FAULT(w->arbiter(w->context, a, clip, float_of(K_FOUR), float_of(frame)));      /* 00224580 */
        return 0;
    }
    if (sub_state != 0) return 0;                                      /* 00224414 */
    F hit = ld(a, 0x224);                                              /* 0022441C */
    if (eq(hit, K_ZERO) && eq(ld(a, 0x22C), K_ZERO)) {                 /* 00224430/00224444 */
        *result = 0;
        return 0;
    }
    if (!eq(hit, K_ZERO)) {                                            /* 0022445C */
        FAULT(w->sound(w->context, a, 0x152));                         /* 00224474 */
        FAULT(w->react_0021C350(w->context, a));                       /* 0022447C */
    }
    if (!eq(ld(a, 0x22C), K_ZERO)) {                                   /* 00224498 */
        FAULT(w->sound(w->context, a, 0x153));                         /* 002244B0 */
        FAULT(w->react_0021C270(w->context, a));                       /* 002244B8 */
    }
    st8(a, 7, u8(a, 7) + 1);                                           /* 002244DC */
    FAULT(w->shake(w->context, 0, 0xC0, 5, 1));                        /* 002244D8 */
    FAULT(w_request(w, a, 0x6F, K_ONE));                               /* 002244F0 */
    return 0;
}

/* ---- 00224B80(p) ------------------------------------------------------------- */

int em_player_recovery_react_00224B80(EmPlayerLiveActor *a, const EmPlayerRecoveryScene *s,
                                      const EmPlayerRecoveryWorkers *w, int *result)
{
    if (!a || !s || !s->d8106F1 || !w || !result || !w->shake || !w->sound || !w->react_0021C350 ||
        !w->react_0021C270 || !w->react_0021C120 || !w->react_0021C190 || !w->react_0021D490 ||
        !w->random || !w->request)
        return -1;
    unsigned sub_state = u8(a, 7);                                     /* 00224B8C */
    *result = 1;
    switch (sub_state) {
    case 0:
        if (!eq(ld(a, 0x224), K_ZERO)) {                               /* 00224C24 */
            FAULT(w->shake(w->context, 0, 0xC0, 5, 1));                /* 00224C3C */
            FAULT(w->sound(w->context, a, 0x152));                     /* 00224C54 */
            FAULT(w->react_0021C350(w->context, a));                   /* 00224C5C */
            if (le(ld(a, 0x220), K_ZERO)) {                            /* 00224C70 */
                if (u8(a, 0xF) == 0x63 || u8(a, 0x234) == 1)           /* 00224C88/00224C98 */
                    st8(a, 7, 0x1E);                                   /* 00224CA8 */
                else
                    st8(a, 7, 0xA);                                    /* 00224CB4 */
            } else {
                st8(a, 7, u8(a, 7) + 1);                               /* 00224CC4 */
                st16(a, 0x20E, 0x3C);                                  /* 00224CCC */
            }
            return 0;
        }
        if (!eq(ld(a, 0x22C), K_ZERO)) {                               /* 00224CD4 */
            FAULT(w->shake(w->context, 0, 0xC0, 5, 1));                /* 00224CEC */
            FAULT(w->sound(w->context, a, 0x153));                     /* 00224D04 */
            FAULT(w->react_0021C270(w->context, a));                   /* 00224D0C */
            if (!lt(ld(a, 0x228), K_100) && *s->d8106F1 != 0)           /* 00224D24/00224D3C */
                st8(a, 7, 0x14);                                       /* 00224D4C */
            else
                st8(a, 7, u8(a, 7) + 1);                               /* 00224D58 */
            st16(a, 0x20E, 0x3C);                                      /* 00224D64 */
            return 0;
        }
        *result = 0;                                                   /* 00224CE0 */
        return 0;
    case 1:
        st8(a, 7, sub_state + 1);                                      /* 00224D78 */
        if (u8(a, 0xF) & 2u) {                                         /* 00224D84 */
            FAULT(w_request(w, a, 0x67, K_FOUR));                      /* 00224D98 */
            st8(a, 0xF, 0);                                            /* 00224DA4 */
        } else {
            uint32_t value = 0;
            FAULT(w->random(w->context, &value));                      /* 00224DA8 */
            FAULT(w_request(w, a, (value & 1u) ? 0x67 : 0x66, K_FOUR)); /* 00224DCC/00224DEC */
        }
        return 0;
    case 2:
    case 0x18:
        if (ld(a, 0x200) & 0x1000u) {                                  /* 00224E04/00224F78 */
            st8(a, 7, 0);                                              /* 00224E20/00224F94 */
            FAULT(w_request(w, a, 0x5F, K_8));                         /* 00224E1C/00224F90 */
            st16(a, 0x20E, 0x3C);                                      /* 00224E2C/00224FA0 */
        }
        return 0;
    case 0xA:
        FAULT(w->shake(w->context, 0, 0xC0, 5, 1));                    /* 00224E3C */
        FAULT(w->sound(w->context, a, 0x146));                         /* 00224E54 */
        FAULT(w->sound(w->context, a, 0x151));                         /* 00224E6C */
        st8(a, 7, u8(a, 7) + 1);                                       /* 00224E8C */
        FAULT(w_request(w, a, 0x68, K_8));                             /* 00224E90 */
        st8(a, 0x1F0, 0x40);                                           /* 00224EA0 */
        return 0;
    case 0xB:
        if (ld(a, 0x200) & 0x1000u) {                                  /* 00224EAC */
            st8(a, 6, 0x1E);                                           /* 00224EB4 */
            st8(a, 7, 0);                                              /* 00224EBC */
            FAULT(w->react_0021D490(w->context, a));                   /* 00224EB8 */
            FAULT(w->shake(w->context, 1, 0xEE, 0x3C, 1));             /* 00224ECC */
            *result = 2;                                               /* 00224ED8 */
        }
        return 0;
    case 0x14:
        st8(a, 7, sub_state + 1);                                      /* 00224EE0 */
        FAULT(w_request(w, a, 0x67, K_FOUR));                          /* 00224EF0 */
        return 0;
    case 0x15:
        if (!(ld(a, 0x200) & 0x8000u)) st8(a, 7, sub_state + 1);       /* 00224F08/00224F18 */
        return 0;
    case 0x16:
        if (le(ld(a, 0x3C), K_32)) {                                   /* 00224F2C */
            st8(a, 7, sub_state + 1);                                  /* 00224F44 */
            FAULT(w->react_0021C120(w->context, a));                   /* 00224F40 */
        }
        return 0;
    case 0x17: {
        int ready = 0;
        FAULT(w->react_0021C190(w->context, a, &ready));               /* 00224F50 */
        if (ready != 0) st8(a, 7, u8(a, 7) + 1);                       /* 00224F58/00224F6C */
        return 0;
    }
    case 0x1E:
        st8(a, 4, 2);                                                  /* 00224FAC */
        st8(a, 5, 3);                                                  /* 00224FB0 */
        st8(a, 6, 0);                                                  /* 00224FB8 */
        st8(a, 0x1F0, 0x3F);                                           /* 00224FC0 */
        *result = 2;
        return 0;
    default:
        return 0;
    }
}

/* ---- The ledge scans ------------------------------------------------------- */

static int need_ledge_common(const EmPlayerRecoveryWorkers *w)
{
    return w && w->move && w->ledge && w->sqrt && w->table && w->attribute && w->sides &&
           w->hands;
}

/* The hit-node class test both scans apply: attribute byte (node & 0xFF) and
 * class (node & 0xFF00, of the sign-extended halfword). */
static unsigned node_attribute(const EmPlayerProbeHit *hit) { return hit->node & 0xFFu; }
static unsigned node_class(const EmPlayerProbeHit *hit) { return hit->node & 0xFF00u; }

static int w_ledge(const EmPlayerRecoveryWorkers *w, const EmPlayerProbeHit *hit, EmPlayerRecoveryLedge *l)
{
    memset(l, 0, sizeof *l);
    return w->ledge(w->context, hit, l);
}

static int w_attribute(const EmPlayerRecoveryWorkers *w, const EmPlayerClimbTable *t, int i, int *attr)
{
    int value = 0;
    FAULT(w->attribute(w->context, t, i, &value));
    *attr = (int)(int16_t)value;                                       /* sign-extend the low halfword */
    return 0;
}

/* The success writes of 0017C860 (0017CC14.. / 0017CF6C..). */
static void grab_commit(EmPlayerLiveActor *a, const EmPlayerRecoveryLedge *l, F height, F lower,
                        unsigned variant)
{
    F px = bits_of(l->point[0]), pz = bits_of(l->point[2]);
    F nx = bits_of(l->normal[0]), nz = bits_of(l->normal[2]);
    st(a, 0x2E0, add(px, mul(K_1_5, nx)));                             /* 0017CC58 / 0017CFAC */
    st(a, 0x2E8, add(pz, mul(K_1_5, nz)));                             /* 0017CC78 / 0017CFC8 */
    st(a, 0x2E4, lower);                                               /* 0017CC84 / 0017CFD0 */
    st(a, 0x254, height);                                              /* 0017CC8C / 0017CFD8 */
    st(a, 0x218, bits_of(l->heading));                                 /* 0017CC94 / 0017CFE0 */
    st8(a, 0x25F, 1);                                                  /* 0017CC98 / 0017CFE4 */
    st8(a, 5, 4);                                                      /* 0017CC9C / 0017CFE8 */
    st8(a, 6, 0);                                                      /* 0017CCA0 / 0017CFEC */
    st8(a, 0x1F0, 9);                                                  /* 0017CCA4 / 0017CFF0 */
    st8(a, 0x1F1, variant);                                            /* 0017CCAC / 0017CFF8 */
}

/* 0017C860 after a hit ahead at 18.0 (0017C938..0017CCC8). */
static int grab_high(EmPlayerLiveActor *a, F reach, const EmPlayerProbeHit *hit,
                     EmPlayerRecoveryScratch *x, const EmPlayerRecoveryWorkers *w, int *result)
{
    if (node_attribute(hit) == 0x46) return 0;                         /* 0017C94C */
    if (node_class(hit) != 0x2000) return 0;                           /* 0017C95C */
    EmPlayerRecoveryLedge l;
    FAULT(w_ledge(w, hit, &l));                                        /* 0017C964 */
    F dx = sub(bits_of(l.point[0]), ld(a, 0xB0));                      /* 0017C980 */
    x->spad3A20 = float_of(dx);                                        /* 0017C988 */
    F dz = sub(bits_of(l.point[2]), ld(a, 0xB8));                      /* 0017C998 */
    x->spad3A24 = float_of(dz);                                        /* 0017C9A0 */
    F far = w_sqrt(w, sum_squares(bits_of(x->spad3A20), dz));          /* 0017C9AC */
    const F probe[4] = { 0, K_4_01, K_5_5, K_ONE };                    /* 0017C9B8..0017C9E0 */
    F m[16], target[4];
    record_matrix(a, m);
    FAULT(mat_apply(m, probe, target));                                /* 0017C9F8 */
    EmPlayerProbeHit low;
    int kind = 0;
    FAULT(w_move(w, a, target, &low, &kind));                          /* 0017CA0C */
    if (kind != 0) {                                                   /* 0017CA14 */
        F ex = sub(bits_of(low.point[0]), ld(a, 0xB0));                /* 0017CA30 */
        x->spad3A20 = float_of(ex);
        F ez = sub(bits_of(low.point[2]), ld(a, 0xB8));                /* 0017CA48 */
        x->spad3A24 = float_of(ez);
        F near = w_sqrt(w, sum_squares(bits_of(x->spad3A20), ez));     /* 0017CA5C */
        if (lt(add(K_HALF, near), far)) return 0;                      /* 0017CA70/0017CA74 */
    }
    const F at[4] = { sub(bits_of(l.point[0]), bits_of(l.normal[0])), bits_of(l.point[1]),
                      sub(bits_of(l.point[2]), bits_of(l.normal[2])), K_ONE };   /* 0017CAB0..0017CAE8 */
    EmPlayerClimbTable t;
    FAULT(w_table(w, at, &t));                                         /* 0017CAE4 */
    if (t.count == 0) return 0;                                        /* 0017CAF4 */
    for (int i = t.count - 1; i >= 0; --i) {                           /* 0017CAFC..0017CCBC */
        if (!(t.flags[i] & 1u)) continue;                              /* 0017CB3C */
        int attr = 0;
        FAULT(w_attribute(w, &t, i, &attr));                           /* 0017CB48 */
        if ((attr & 0xFF) == 0x46) continue;                           /* 0017CB60 */
        F height = bits_of(t.height[i]);
        if (!lt(bits_of(t.aux[i]), K_PI_5)) continue;                  /* 0017CB7C */
        int blocked = 0;
        FAULT(w->lip(w->context, a, &l, 0, float_of(height), &blocked));   /* 0017CB94 */
        if (blocked) continue;                                         /* 0017CB9C */
        F top = add(K_20_5, ld(a, 0xB4));                              /* 0017CBB4 */
        x->spad3A20 = float_of(top);                                   /* 0017CBB8 */
        if (lt(sub(top, reach), height)) continue;                     /* 0017CBC0/0017CBC4 */
        if (lt(height, top)) continue;                                 /* 0017CBD4 */
        FAULT(w->sides(w->context, a, &l, float_of(height), &blocked));   /* 0017CBE4 */
        if (blocked) continue;
        FAULT(w->hands(w->context, a, &l, float_of(sub(height, K_ONE)), &blocked));   /* 0017CC04 */
        if (blocked) continue;
        grab_commit(a, &l, height, sub(height, K_20_5), 1);            /* 0017CC80 */
        *result = 1;
        return 0;
    }
    return 0;
}

/* 0017C860 with no hit at 18.0 (0017CCCC..0017D00C). */
static int grab_low(EmPlayerLiveActor *a, EmPlayerRecoveryScratch *x,
                    const EmPlayerRecoveryWorkers *w, int *result)
{
    const F probe[4] = { 0, K_4_01, K_5_5, K_ONE };                    /* 0017CCD0..0017CCF8 */
    F m[16], target[4];
    record_matrix(a, m);
    FAULT(mat_apply(m, probe, target));                                /* 0017CD0C */
    EmPlayerProbeHit hit;
    int kind = 0;
    FAULT(w_move(w, a, target, &hit, &kind));                          /* 0017CD20 */
    if (kind == 0) return 0;                                           /* 0017CD28 */
    if (node_attribute(&hit) == 0x46) return 0;                        /* 0017CD44 */
    if (node_class(&hit) != 0x2000) return 0;                          /* 0017CD54 */
    EmPlayerRecoveryLedge l;
    FAULT(w_ledge(w, &hit, &l));                                       /* 0017CD5C */
    const F at[4] = { sub(bits_of(l.point[0]), bits_of(l.normal[0])), bits_of(l.point[1]),
                      sub(bits_of(l.point[2]), bits_of(l.normal[2])), K_ONE };   /* 0017CD88..0017CDC0 */
    EmPlayerClimbTable t;
    FAULT(w_table(w, at, &t));                                         /* 0017CDBC */
    if (t.count == 0) return 0;                                        /* 0017CDCC */
    F lm[16];
    ledge_matrix(&l, lm);
    for (int i = t.count - 1; i >= 0; --i) {                           /* 0017CDD4..0017D008 */
        if (!(t.flags[i] & 1u)) continue;                              /* 0017CE14 */
        int attr = 0;
        FAULT(w_attribute(w, &t, i, &attr));                           /* 0017CE20 */
        if ((attr & 0xFF) == 0x46) continue;                           /* 0017CE38 */
        F height = bits_of(t.height[i]);
        if (!lt(bits_of(t.aux[i]), K_PI_5)) continue;                  /* 0017CE54 */
        int blocked = 0;
        FAULT(w->lip(w->context, a, &l, 1, float_of(height), &blocked));   /* 0017CE6C */
        if (blocked) continue;                                         /* 0017CE74 */
        F gap = fabs_0011DF78(sub(height, ld(a, 0xB4)));               /* 0017CE84/0017CE88 */
        x->spad3A20 = float_of(gap);                                   /* 0017CEA4 */
        if (!le(gap, K_HALF)) continue;                                /* 0017CE98 */
        const F from[4] = { ld(a, 0xB0), height, ld(a, 0xB8), K_ONE }; /* 0017CEC0..0017CEF8 */
        const F local[4] = { 0, K_4_01, K_9_5, 0 };                    /* 0017CF00..0017CF1C */
        F turned[4], to[4];
        FAULT(mat_apply(lm, local, turned));                           /* 0017CF18 (0x700038C0) */
        FAULT(vec_add(from, turned, to));                              /* 0017CF34 (0x700038B0) */
        to[3] = K_ONE;                                                 /* 0017CF44 */
        float ff[4], tf[4];
        vec_to_float(from, ff);
        vec_to_float(to, tf);
        EmPlayerProbeHit swept;
        memset(&swept, 0, sizeof swept);
        int r = w->sweep(w->context, a, ff, tf, 7, &swept);            /* 0017CF5C */
        if (r < 0) return -1;
        if (r != 0) continue;                                          /* 0017CF64 */
        grab_commit(a, &l, height, height, 0);                         /* 0017CFD0 */
        *result = 1;
        return 0;
    }
    return 0;
}

int em_player_recovery_ledge_grab(EmPlayerLiveActor *a, float reach_value,
                                  const EmPlayerRecoveryScene *s, EmPlayerRecoveryScratch *x,
                                  const EmPlayerRecoveryWorkers *w, int *result)
{
    if (!a || !s || !s->d8106F1 || !x || !result || !need_ledge_common(w) || !w->lip || !w->sweep)
        return -1;
    F reach = bits_of(reach_value);                                    /* 0017C88C $f20 */
    *result = 0;
    if (le(ld(a, 0x220), K_ZERO)) return 0;                            /* 0017C890 */
    if (!lt(ld(a, 0x228), K_100) && *s->d8106F1 != 0) return 0;         /* 0017C8B0/0017C8C8 */
    const F probe[4] = { 0, K_18, K_5_5, K_ONE };                      /* 0017C8D8..0017C900 */
    F m[16], target[4];
    record_matrix(a, m);
    FAULT(mat_apply(m, probe, target));                                /* 0017C914 */
    EmPlayerProbeHit hit;
    int kind = 0;
    FAULT(w_move(w, a, target, &hit, &kind));                          /* 0017C928 */
    if (kind != 0) return grab_high(a, reach, &hit, x, w, result);     /* 0017C930 */
    return grab_low(a, x, w, result);
}

/* ---- 0017D080(p) ------------------------------------------------------------- */

int em_player_recovery_ledge_catch(EmPlayerLiveActor *a, const EmPlayerRecoveryScene *s,
                                   EmPlayerRecoveryScratch *x, const EmPlayerRecoveryWorkers *w,
                                   int *result)
{
    if (!a || !s || !s->d8106F1 || !x || !result || !need_ledge_common(w) || !w->depth ||
        !w->segment)
        return -1;
    int owner_ok = 0;                                                  /* 0017D0BC $s1 */
    *result = 0;
    if (le(ld(a, 0x220), K_ZERO)) return 0;                            /* 0017D0B0 */
    if (!lt(ld(a, 0x228), K_100) && *s->d8106F1 != 0) return 0;         /* 0017D0D0/0017D0E8 */
    const F behind[4] = { 0, 0, K_M8, K_ONE };                         /* 0017D0F8..0017D11C */
    F m[16], target[4];
    record_matrix(a, m);
    FAULT(mat_apply(m, behind, target));                               /* 0017D130 */
    EmPlayerProbeHit hit;
    int kind = 0;
    FAULT(w_move(w, a, target, &hit, &kind));                          /* 0017D144 */
    if (!(kind & 6)) return 0;                                         /* 0017D14C */
    if (node_class(&hit) != 0x2000) return 0;                          /* 0017D16C */
    if (hit.owner != NULL && owner_0017D040(&hit)) owner_ok = 1;       /* 0017D184/0017D18C */
    if (node_attribute(&hit) == 0x46 && !owner_ok) return 0;           /* 0017D1B0/0017D1B8 */
    EmPlayerRecoveryLedge l;
    FAULT(w_ledge(w, &hit, &l));                                       /* 0017D1C8 */
    F turn;
    FAULT(wrap_001B1470(sub(bits_of(l.heading), ld(a, 0xC4)), &turn)); /* 0017D1DC */
    F facing = fabs_0011DF78(turn);                                    /* 0017D1E4 */
    x->spad3A20 = float_of(facing);                                    /* 0017D1F0 */
    if (less_than_0_6pi(facing)) return 0;                             /* 0017D1F4..0017D218 */
    const F back[4] = { 0, 0, K_M8, 0 };                               /* 0017D228..0017D264 */
    F lm[16], turned[4], position[4], probe[4];
    ledge_matrix(&l, lm);
    FAULT(mat_apply(lm, back, turned));                                /* 0017D260 */
    for (int i = 0; i < 4; ++i) position[i] = ld(a, 0xB0 + 4u * (unsigned)i);
    FAULT(vec_add(turned, position, probe));                           /* 0017D278 */
    EmPlayerProbeHit second;
    FAULT(w_move(w, a, probe, &second, &kind));                        /* 0017D28C */
    if (kind & 6) {                                                    /* 0017D298 */
        x->spad3A20 = l.heading;                                       /* 0017D2B0 */
        FAULT(w_ledge(w, &second, &l));                                /* 0017D2AC */
        F change;
        FAULT(wrap_001B1470(sub(bits_of(x->spad3A20), bits_of(l.heading)), &change));   /* 0017D2C4 */
        x->spad3A24 = float_of(change);                                /* 0017D2D0 */
        if (!le(fabs_0011DF78(change), K_PI_9)) return 0;              /* 0017D2D4/0017D2EC */
    }
    /* 0017D304: the first column table half a unit behind the face. */
    F px = bits_of(l.point[0]), py = bits_of(l.point[1]), pz = bits_of(l.point[2]);
    F nx = bits_of(l.normal[0]), nz = bits_of(l.normal[2]);
    const F first[4] = { add(px, mul(K_HALF, nx)), py, add(pz, mul(K_HALF, nz)), K_ONE };
    EmPlayerClimbTable t;
    FAULT(w_table(w, first, &t));                                      /* 0017D36C */
    if (t.count != 0) {                                                /* 0017D37C */
        int below = 0;                                                 /* $s0 */
        for (int i = 0; i < t.count; ++i) {                            /* 0017D39C..0017D3CC */
            if (!lt(bits_of(t.height[i]), sub(ld(a, 0xB4), K_20_5))) break;   /* 0017D3A4/0017D3A8 */
            below = 1;
        }
        if (!below) return 0;                                          /* 0017D3D8 */
    }
    /* 0017D3E8: area 0x11 within 115 of (340, 270) reaches 3.0 behind. */
    int deep = 0;
    if (s->area == 0x11) {                                             /* 0017D3F4 */
        F dx = sub(ld(a, 0xB0), K_340);                                /* 0017D418 */
        x->spad3A20 = float_of(dx);
        F dz = sub(ld(a, 0xB8), K_270);                                /* 0017D42C */
        x->spad3A28 = float_of(dz);
        F d = w_sqrt(w, sum_squares(bits_of(x->spad3A20), dz));        /* 0017D438..0017D444 */
        x->spad3A2C = float_of(d);                                     /* 0017D460 */
        if (le(d, K_115)) deep = 1;                                    /* 0017D454/0017D464 */
    }
    const F scale = deep ? K_THREE : K_HALF;                           /* 0017D478 / 0017D4E0 */
    const F second_at[4] = { sub(px, mul(scale, nx)), py, sub(pz, mul(scale, nz)), K_ONE };
    FAULT(w_table(w, second_at, &t));                                  /* 0017D540 */
    if (t.count == 0) return 0;                                        /* 0017D550 */
    for (int i = t.count - 1; i >= 0; --i) {                           /* 0017D558..0017D7D0 */
        if (!(t.flags[i] & 1u)) continue;                              /* 0017D598 */
        if (!owner_ok) {                                               /* 0017D5A0 */
            int attr = 0;
            FAULT(w_attribute(w, &t, i, &attr));                       /* 0017D5AC */
            if ((attr & 0xFF) == 0x46) continue;                       /* 0017D5C4 */
        }
        if (!lt(bits_of(t.aux[i]), K_PI_5)) continue;                  /* 0017D5E4 */
        F height = bits_of(t.height[i]);
        F gap = fabs_0011DF78(sub(height, ld(a, 0xB4)));               /* 0017D5FC/0017D600 */
        if (!le(gap, K_2_8)) continue;                                 /* 0017D614 */
        int blocked = 0;
        FAULT(w->sides(w->context, a, &l, float_of(height), &blocked));   /* 0017D628 */
        if (blocked) continue;                                         /* 0017D630 */
        FAULT(w->hands(w->context, a, &l, l.point[1], &blocked));      /* 0017D640 */
        if (blocked) return 0;                                         /* 0017D648 */
        FAULT(w->depth(w->context, a, &l, float_of(height), &blocked));   /* 0017D66C */
        if (blocked) return 0;                                         /* 0017D674 */
        /* 0017D684: the drop column 1.5 in front of the face. */
        const float from[4] = { float_of(add(px, mul(K_1_5, nx))), float_of(height),
                                float_of(add(pz, mul(K_1_5, nz))), 1.0f };   /* 0017D6C0..0017D6E8 */
        float to[4];
        memcpy(to, from, sizeof to);                                   /* 00102948 at 0017D6E4 */
        to[1] = float_of(sub(height, K_20_5));                         /* 0017D708/0017D718 */
        FAULT(w->segment(w->context, from, to, 6, 0, &blocked));       /* 0017D71C */
        if (blocked) return 0;                                         /* 0017D724 */
        st(a, 0xB0, px);                                               /* 0017D74C */
        st(a, 0xB8, pz);                                               /* 0017D758 */
        F yaw;
        FAULT(wrap_001B1470(sub(bits_of(l.heading), K_PI), &yaw));     /* 0017D764 */
        st(a, 0xC4, yaw);                                              /* 0017D76C */
        st(a, 0x290, add(px, mul(K_1_5, nx)));                         /* 0017D798 */
        st(a, 0x298, add(pz, mul(K_1_5, nz)));                         /* 0017D7B0 */
        st(a, 0x294, height);                                          /* 0017D7BC */
        *result = 1;                                                   /* 0017D780 */
        return 0;
    }
    return 0;
}

/* ---- 00162A40(p): the +5 = 4 state ---------------------------------------- */

static int need_hang(const EmPlayerRecoveryWorkers *w)
{
    return need_translate(w) && w->request && w->hang_clear && w->sound && w->hang_row &&
           w->skeleton && w->column && w->heading && w->reentry && w->handoff && w->floor &&
           w->fall;
}

int em_player_recovery_hang_entry(EmPlayerLiveActor *a, const EmPlayerRecoveryWorkers *w)
{
    if (!a || !need_hang(w)) return -1;
    int r = 0;
    switch (u8(a, 6)) {                                                /* 00162A4C */
    case 0:
        st(a, 0xB0, ld(a, 0x2E0));                                     /* 00162AAC */
        st(a, 0xB4, ld(a, 0x2E4));                                     /* 00162AB4 */
        st(a, 0xB8, ld(a, 0x2E8));                                     /* 00162ABC */
        st(a, 0xC4, ld(a, 0x218));                                     /* 00162AC4 */
        if (u8(a, 0x1F1) == 1) {                                       /* 00162ACC */
            st8(a, 6, u8(a, 6) + 1);                                   /* 00162AEC */
            st16(a, 0x2E, 0);                                          /* 00162AF4 */
            FAULT(w_request(w, a, 0x7A, K_FOUR));                      /* 00162AF0 */
        } else {
            st8(a, 6, 0xA);                                            /* 00162B00 */
            st8(a, 0x25F, 0);                                          /* 00162B18 */
            FAULT(w_request(w, a, 0x7D, K_8));                         /* 00162B14 */
            FAULT(w->probes(w->context, a));                           /* 00162B1C */
        }
        return 0;
    case 1:
        FAULT(w->hang_clear(w->context, a, &r));                       /* 00162B2C */
        if (r != 0) {
            st8(a, 6, 3);                                              /* 00162B44 */
        } else if (!(ld(a, 0x200) & 0x8000u)) {                        /* 00162B50 */
            st8(a, 6, u8(a, 6) + 1);                                   /* 00162B70 */
            FAULT(w->sound(w->context, a, 0xFF));                      /* 00162B74 */
        }
        return 0;
    case 2:
        FAULT(w->hang_clear(w->context, a, &r));                       /* 00162B84 */
        if (r != 0) {
            st8(a, 6, 3);                                              /* 00162B9C */
        } else if (ld(a, 0x200) & 0x1000u) {                           /* 00162BA8 */
            st8(a, 5, 9);                                              /* 00162BB4 */
            st8(a, 6, 0);                                              /* 00162BBC */
            st8(a, 0x1F0, 0x10);                                       /* 00162BC0 */
            st8(a, 0xD, 0);                                            /* 00162BCC */
            int clip = 0;
            FAULT(w->hang_row(w->context, a, &clip));                  /* 00162BC8 */
            FAULT(w_request(w, a, clip, K_16));                        /* 00162BE0 */
        }
        return 0;
    case 3:
        st(a, 0x2F4, ld(a, 0xB4));                                     /* 00162C00 */
        st8(a, 5, 7);                                                  /* 00162C04 */
        st8(a, 6, 0);                                                  /* 00162C08 */
        st8(a, 0x1F0, 0xD);                                            /* 00162C0C */
        st(a, 0x2EC, K_M0_2);                                          /* 00162C18 */
        return 0;
    case 0xA: {
        if (!(ld(a, 0x200) & 0x1000u)) return 0;                       /* 00162C24 */
        float node[4];
        FAULT(w->skeleton(w->context, a, node));                       /* 00162C2C */
        for (unsigned i = 0; i < 4; ++i)                               /* 00102948 at 00162C40 */
            st(a, 0xB0 + 4u * i, bits_of(node[i]));
        st(a, 0xB4, sub(ld(a, 0xB4), K_11));                           /* 00162C6C */
        FAULT(w_request(w, a, 0x8C, K_ZERO));                          /* 00162C68 */
        float at[4];
        for (unsigned i = 0; i < 4; ++i) at[i] = float_of(ld(a, 0xB0 + 4u * i));
        FAULT(w->column(w->context, a, at, float_of(K_18), &r));       /* 00162C80 */
        if (r != 0) {                                                  /* 00162C88 */
            st8(a, 0x236, 1);                                          /* 00162C94 */
            st8(a, 0x235, u8(a, 0x235) | 2u);                          /* 00162CA0 */
        }
        st8(a, 6, u8(a, 6) + 1);                                       /* 00162CB0 */
        return 0;
    }
    case 0xB:
        FAULT(w->heading(w->context, a, 0));                           /* 00162CB8 */
        if (u8(a, 0x23F) >= 2) {                                       /* 00162CC4 */
            st8(a, 6, u8(a, 6) + 1);                                   /* 00162CE4 */
            FAULT(w->reentry(w->context, a, 1));                       /* 00162CE0 */
        } else {
            st8(a, 0x25C, 0);                                          /* 00162CF8 */
            FAULT(w->handoff(w->context, a));                          /* 00162CF4 */
            FAULT(w->probes(w->context, a));                           /* 00162CFC */
        }
        st(a, 0xB4, add(ld(a, 0xB4), K_M0_2));                         /* 00162D18/00162D24 */
        FAULT(w->floor(w->context, a, 1, &r));                         /* 00162D20 */
        if (r == 0) {                                                  /* 00162D28 */
            st(a, 0x2F4, ld(a, 0xB4));                                 /* 00162D3C */
            st8(a, 5, 7);                                              /* 00162D40 */
            st8(a, 6, 0);                                              /* 00162D44 */
            st8(a, 0x1F0, 0xD);                                        /* 00162D4C */
        }
        return 0;
    case 0xC:
        FAULT(translate(a, 1, w));                                     /* 00162D50 */
        if (!(ld(a, 0x200) & 0x8000u))                                 /* 00162D60 */
            FAULT(w->handoff(w->context, a));                          /* 00162D68 */
        st(a, 0xB4, add(ld(a, 0xB4), K_M0_2));                         /* 00162D84/00162D90 */
        FAULT(w->floor(w->context, a, 1, &r));                         /* 00162D8C */
        FAULT(w->fall(w->context, a));                                 /* 00162D94 */
        return 0;
    default:
        return 0;
    }
}

/* ---- Binding adapters ------------------------------------------------------ */

static int live_scene(EmPlayerRecoveryLive *b, EmPlayerRecoveryScene *scene)
{
    memset(scene, 0, sizeof *scene);
    if (!b->scene) return -1;
    return b->scene(b->scene_context, scene) < 0 ? -1 : 0;
}

static void shared_in(EmPlayerRecoveryLive *b)
{
    if (b->shared3A20) memcpy(&b->scratch.spad3A20, b->shared3A20, sizeof *b->shared3A20);
}

static void shared_out(EmPlayerRecoveryLive *b)
{
    if (b->shared3A20) memcpy(b->shared3A20, &b->scratch.spad3A20, sizeof *b->shared3A20);
}

int em_player_recovery_translate_worker(void *context, EmPlayerLiveActor *actor, int arg)
{
    EmPlayerRecoveryLive *b = context;
    if (!b) return -1;
    return em_player_recovery_translate(actor, arg, &b->workers);
}

int em_player_recovery_strafe_worker(void *context, EmPlayerLiveActor *actor)
{
    return context ? em_player_recovery_strafe(actor) : -1;
}

int em_player_recovery_stick_quadrant_worker(void *context, EmPlayerLiveActor *actor)
{
    EmPlayerRecoveryLive *b = context;
    EmPlayerRecoveryScene scene;
    if (!b || live_scene(b, &scene) < 0) return -1;
    shared_in(b);
    int r = em_player_recovery_stick_quadrant(actor, &scene, &b->scratch, &b->workers);
    shared_out(b);
    return r;
}

int em_player_recovery_react_002243F0_worker(void *context, EmPlayerLiveActor *actor, int *result)
{
    EmPlayerRecoveryLive *b = context;
    if (!b) return -1;
    shared_in(b);
    int r = em_player_recovery_react_002243F0(actor, &b->scratch, &b->workers, result);
    shared_out(b);
    return r;
}

int em_player_recovery_react_00224B80_worker(void *context, EmPlayerLiveActor *actor, int *result)
{
    EmPlayerRecoveryLive *b = context;
    EmPlayerRecoveryScene scene;
    if (!b || live_scene(b, &scene) < 0) return -1;
    return em_player_recovery_react_00224B80(actor, &scene, &b->workers, result);
}

int em_player_recovery_ledge_catch_worker(void *context, EmPlayerLiveActor *actor, int *result)
{
    EmPlayerRecoveryLive *b = context;
    EmPlayerRecoveryScene scene;
    if (!b || live_scene(b, &scene) < 0) return -1;
    shared_in(b);
    int r = em_player_recovery_ledge_catch(actor, &scene, &b->scratch, &b->workers, result);
    shared_out(b);
    return r;
}

int em_player_recovery_ledge_grab_worker(void *context, EmPlayerLiveActor *actor, uint32_t reach,
                                         int *result)
{
    EmPlayerRecoveryLive *b = context;
    EmPlayerRecoveryScene scene;
    if (!b || live_scene(b, &scene) < 0) return -1;
    shared_in(b);
    int r = em_player_recovery_ledge_grab(actor, float_of(reach), &scene, &b->scratch, &b->workers,
                                          result);
    shared_out(b);
    return r;
}

int em_player_recovery_state4(void *context, EmPlayerLiveActor *actor)
{
    EmPlayerRecoveryLive *b = context;
    if (!b || !actor) return -1;
    return em_player_recovery_hang_entry(actor, &b->workers);
}

int em_player_recovery_slide_translate(void *context, EmPlayerSlideActor *actor, int arg)
{
    EmPlayerRecoveryLive *b = context;
    if (!b || !b->live || !actor || !need_translate(&b->workers)) return -1;
    em_player_slide_actor_to_live(actor, b->live);
    int r = translate(b->live, arg, &b->workers);
    em_player_slide_actor_from_live(b->live, actor);
    return r;
}

int em_player_recovery_slide_damage(void *context, EmPlayerSlideActor *actor, int *result)
{
    EmPlayerRecoveryLive *b = context;
    EmPlayerRecoveryScene scene;
    if (!b || !b->live || !actor || !result || live_scene(b, &scene) < 0) return -1;
    em_player_slide_actor_to_live(actor, b->live);
    int r = em_player_recovery_react_00224B80(b->live, &scene, &b->workers, result);
    em_player_slide_actor_from_live(b->live, actor);
    return r;
}

int em_player_recovery_climb_translate(void *context, EmPlayerClimbActor *actor, int arg)
{
    EmPlayerRecoveryLive *b = context;
    if (!b || !b->live || !actor || !need_translate(&b->workers)) return -1;
    uint8_t link_kind = actor->link_kind;          /* derived from +308, not a record byte */
    em_player_climb_actor_to_live(actor, b->live);
    int r = translate(b->live, arg, &b->workers);
    em_player_climb_actor_from_live(b->live, actor);
    actor->link_kind = link_kind;
    return r;
}
