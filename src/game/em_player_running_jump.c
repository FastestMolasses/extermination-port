/* em_player_running_jump.c - the running jump and the Use chain's last two
 * tests (see em_player_running_jump.h).
 *
 * Read from the original instructions (build/asm of the decomp), not from
 * the readable decompilation alone: 0015EC50, 0015FDF0 and 001634A0 are
 * NEARMISS C. Where the NEARMISS C differs from the instructions:
 *   - 001634A0 cases 2 and 3 return early when 002243F0 returned 0 AND
 *     0017C860 then returned nonzero (the grab took the actor); the C reads
 *     the opposite polarity (return when 0017C860 returned 0);
 *   - 001634A0 case 0 calls 001C61D0 with two arguments (+40, 0x69);
 *   - 001634A0 case 3 passes p + 0x2E4 to 00179880 (the drop accumulator
 *     runs on +2E4, not +2EC);
 *   - the distances of 0015EC50 and 0015FDF0 are sqrt(madd(mula(dx, dx),
 *     dz, dz)) on the EE accumulator, with dx re-read from 0x70003A20.
 * Every address in a comment is the original instruction translated there.
 * tools/test_player_running_jump_reference.py executes the original
 * instructions and compares every record byte, scratch word, return value
 * and worker call. */
#include "game/em_player_running_jump.h"
#include "game/em_ee_float.h"
#include "game/em_owner_services_original.h"
#include "game/em_player_fall.h"

#include <stddef.h>
#include <string.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

typedef uint32_t F;

/* Float constants as the instructions build them (lui/ori). */
#define K_ZERO       UINT32_C(0x00000000)
#define K_ONE        UINT32_C(0x3F800000)
#define K_2          UINT32_C(0x40000000)
#define K_3          UINT32_C(0x40400000)
#define K_4          UINT32_C(0x40800000)
#define K_8          UINT32_C(0x41000000)
#define K_10         UINT32_C(0x41200000)
#define K_18         UINT32_C(0x41900000)
#define K_25         UINT32_C(0x41C80000)
#define K_30         UINT32_C(0x41F00000)
#define K_1000       UINT32_C(0x447A0000)
#define K_300        UINT32_C(0x43960000)
#define K_4_5        UINT32_C(0x40900000)
#define K_5          UINT32_C(0x40A00000)
#define K_M5         UINT32_C(0xC0A00000)
#define K_M1         UINT32_C(0xBF800000)
#define K_M2         UINT32_C(0xC0000000)
#define K_0_1        UINT32_C(0x3DCCCCCD)
#define K_M0_1       UINT32_C(0xBDCCCCCD)
#define K_13_5       UINT32_C(0x41580000)
#define K_4_01       UINT32_C(0x408051EC)
#define K_M4_01      UINT32_C(0xC08051EC)
#define K_HALF_PI    UINT32_C(0x3FC90FDB)
#define K_M_HALF_PI  UINT32_C(0xBFC90FDB)
#define K_AIM        UINT32_C(0x3F32B8C3)   /* 0.69813174 (40 degrees) */
#define K_0_9        UINT32_C(0x3F666666)
#define K_4_1        UINT32_C(0x40833333)
#define K_6_3        UINT32_C(0x40C9999A)
#define K_M0_03      UINT32_C(0xBCF5C28F)
#define K_M0_04      UINT32_C(0xBD23D70A)
#define K_M4         UINT32_C(0xC0800000)
#define K_M0_2       UINT32_C(0xBE4CCCCD)

/* D_002489E0: the forward probe point (x, y, z, w) in actor space. */
static const F kForward[4] = { UINT32_C(0x00000000), UINT32_C(0x3F800000), UINT32_C(0x40C00000),
                               UINT32_C(0x3F800000) };
/* D_002485D0[tier]: the jump's gravity step (+2EC). */
static const F kGravity[4] = { UINT32_C(0xBD23D70A), UINT32_C(0xBD23D70A), UINT32_C(0xBCF5C28F),
                               UINT32_C(0xBCED9168) };
/* D_002485B0[tier]: (launch speed, launch angle) pairs. */
static const F kLaunch[4][2] = {
    { UINT32_C(0x3F800000), UINT32_C(0x3F7A35DE) }, { UINT32_C(0x3F800000), UINT32_C(0x3F7A35DE) },
    { UINT32_C(0x3FC00000), UINT32_C(0x3F567750) }, { UINT32_C(0x3FE66666), UINT32_C(0x3F29C91F) },
};

/* 0015EC50's exclusion boxes (0015ED08..0015F738): y (+B4), x (+B0), z (+B8)
 * ranges, each bound inclusive (the instructions branch out on y < lo or
 * !(y <= hi), and so on). */
typedef struct Box { F y0, y1, x0, x1, z0, z1; } Box;
static const Box kArea04 = {   /* D_00810700 == 4 */
    UINT32_C(0x42700000), UINT32_C(0x428C0000), UINT32_C(0x43CD0000), UINT32_C(0x43E88000),
    UINT32_C(0x436B0000), UINT32_C(0x438E8000) };
static const Box kArea0D[3] = {  /* 0xD, D_00810701 == 0 */
    { UINT32_C(0x434D0000), UINT32_C(0x435C0000), UINT32_C(0x44278000), UINT32_C(0x44390000),
      UINT32_C(0x4497E000), UINT32_C(0x44A14000) },
    { UINT32_C(0x43160000), UINT32_C(0x43520000), UINT32_C(0x44340000), UINT32_C(0x44480000),
      UINT32_C(0x44480000), UINT32_C(0x44520000) },
    { UINT32_C(0x43160000), UINT32_C(0x43570000), UINT32_C(0x441EC000), UINT32_C(0x44340000),
      UINT32_C(0x449EC000), UINT32_C(0x44A5A000) },
};
/* 0xF, D_00810701 == 1: two x/z boxes under one shared y range, then a box. */
static const F kArea0FY[2] = { UINT32_C(0x438E8000), UINT32_C(0x43AC8000) };
static const F kArea0FXZ[2][4] = {
    { UINT32_C(0x4450C000), UINT32_C(0x44638000), UINT32_C(0x444D0000), UINT32_C(0x44548000) },
    { UINT32_C(0x445D4000), UINT32_C(0x44638000), UINT32_C(0x444F8000), UINT32_C(0x44624000) },
};
static const Box kArea0F = {
    UINT32_C(0x439B0000), UINT32_C(0x43A78000), UINT32_C(0x44584000), UINT32_C(0x445E8000),
    UINT32_C(0x44688000), UINT32_C(0x44750000) };
static const Box kArea10[2] = {  /* 0x10, D_00810701 == 1 */
    { UINT32_C(0x42BE0000), UINT32_C(0x434D0000), UINT32_C(0x432A0000), UINT32_C(0x43660000),
      UINT32_C(0x42B40000), UINT32_C(0x43820000) },
    { UINT32_C(0x42C60000), UINT32_C(0x42D40000), UINT32_C(0x43640000), UINT32_C(0x43810000),
      UINT32_C(0x43350000), UINT32_C(0x436A0000) },
};
static const Box kArea13[3] = {  /* 0x13, D_00810701 == 0 */
    { UINT32_C(0x43820000), UINT32_C(0x43870000), UINT32_C(0x447A0000), UINT32_C(0x44816000),
      UINT32_C(0x44778000), UINT32_C(0x448D4000) },
    { UINT32_C(0x43520000), UINT32_C(0x43660000), UINT32_C(0x44820000), UINT32_C(0x44898000),
      UINT32_C(0x44728000), UINT32_C(0x44834000) },
    { UINT32_C(0x43390000), UINT32_C(0x43520000), UINT32_C(0x44764000), UINT32_C(0x447F0000),
      UINT32_C(0x44660000), UINT32_C(0x446D8000) },
};
static const Box kArea16 = {   /* 0x16 */
    UINT32_C(0x43660000), UINT32_C(0x437A0000), UINT32_C(0x42BE0000), UINT32_C(0x43070000),
    UINT32_C(0x436B0000), UINT32_C(0x438E8000) };

/* ---- record and float helpers ------------------------------------------ */

static F ld(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void st(EmPlayerLiveActor *a, unsigned at, F v) { em_live_set_u32(a, at, v); }
static unsigned u8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void put8(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u8(a, at, (uint8_t)v); }

static F add(F a, F b) { return em_ee_add_bits(a, b); }
static F sub(F a, F b) { return em_ee_sub_bits(a, b); }
static F mul(F a, F b) { return em_ee_mul_bits(a, b); }
static F dvd(F a, F b) { return em_ee_div_bits(a, b); }
static F neg(F a) { return em_ee_neg_bits(a); }
static int lt(F a, F b) { return em_ee_c_lt_bits(a, b); }
static int le(F a, F b) { return em_ee_c_le_bits(a, b); }
static F bits_of(float f) { return em_ee_bits(f); }
static float float_of(F b) { return em_ee_float(b); }

/* 0011DF78: fabs (the sign bit of the raw word cleared). */
static F fabs_0011DF78(F x) { return x & UINT32_C(0x7FFFFFFF); }

/* sqrt(madd(mula(dx, dx), dz, dz)) through the SDK sqrt worker. */
static F distance(const EmPlayerRunningJumpWorkers *w, F dx, F dz)
{
    F acc = em_ee_mula_bits(dx, dx);
    return bits_of(w->sqrt(w->context, float_of(em_ee_madd_bits(acc, dz, dz))));
}

static void to_float(const F in[], float out[], unsigned n)
{
    for (unsigned i = 0; i < n; ++i) out[i] = float_of(in[i]);
}
static void to_bits(const float in[], F out[], unsigned n)
{
    for (unsigned i = 0; i < n; ++i) out[i] = bits_of(in[i]);
}

/* 001026A0(out, M, v): VMULAx.xyzw, VMADDAy.xyzw, VMADDAz.xyzw, VMADDw.xyzw
 * over the rows M[0..3] (out may alias v). */
static int apply_001026A0(const F m[16], const F v[4], F out[4])
{
    F acc[4] = { 0, 0, 0, 0 }, r[4] = { 0, 0, 0, 0 };
    if (em_vu_vec_bits(EM_VU_MULABC, 15, 0, m, v, 0, NULL, acc) != EM_EE_FLOAT_OK) return -1;
    if (em_vu_vec_bits(EM_VU_MADDABC, 15, 1, m + 4, v, 0, acc, acc) != EM_EE_FLOAT_OK) return -1;
    if (em_vu_vec_bits(EM_VU_MADDABC, 15, 2, m + 8, v, 0, acc, acc) != EM_EE_FLOAT_OK) return -1;
    if (em_vu_vec_bits(EM_VU_MADDBC, 15, 3, m + 12, v, 0, acc, r) != EM_EE_FLOAT_OK) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}

/* The record's +D0 world matrix. */
static void record_matrix(const EmPlayerLiveActor *a, F m[16])
{
    for (unsigned i = 0; i < 16; ++i) m[i] = ld(a, 0xD0 + 4 * i);
}

/* 001029C0(M), 00102BB0(M, M, angle), 00102918(M, M, v) into 0x700036A0
 * (v's w lane is not read). */
static int yaw_matrix(EmPlayerRunningJumpScratch *s, F angle, const F v[3])
{
    float m[16], t[3];
    to_float(v, t, 3);
    if (em_owner_services_identity_001029C0(m) != EM_EE_FLOAT_OK) return -1;
    if (em_owner_services_rotate_y_00102BB0(m, m, angle) != EM_EE_FLOAT_OK) return -1;
    if (em_owner_services_translate_00102918(m, m, t) != EM_EE_FLOAT_OK) return -1;
    to_bits(m, s->s36A0, 16);
    return 0;
}

/* 001B1470 (em_player_recovery_wrap): -1 where the original never returns. */
static int wrap(F angle, F *out) { return em_player_recovery_wrap(angle, out); }

static int w_move(const EmPlayerRunningJumpWorkers *w, EmPlayerLiveActor *a, const F target[4],
                  unsigned mask, EmPlayerProbeHit *hit, int *kind)
{
    float t[4];
    to_float(target, t, 4);
    memset(hit, 0, sizeof *hit);
    int r = w->move(w->context, a, t, mask, hit);
    if (r < 0) return -1;
    *kind = r;
    return 0;
}

static int w_sweep(const EmPlayerRunningJumpWorkers *w, EmPlayerLiveActor *a, const F from[4],
                   const F to[4], unsigned mask, EmPlayerProbeHit *hit, int *kind)
{
    float f[4], t[4];
    to_float(from, f, 4);
    to_float(to, t, 4);
    memset(hit, 0, sizeof *hit);
    int r = w->sweep(w->context, a, f, t, mask, hit);
    if (r < 0) return -1;
    *kind = r;
    return 0;
}

static int w_column(const EmPlayerRunningJumpWorkers *w, EmPlayerLiveActor *a, int *result)
{
    F at[4];
    float t[4];
    for (unsigned i = 0; i < 4; ++i) at[i] = ld(a, 0xB0 + 4 * i);
    to_float(at, t, 4);
    return w->column(w->context, a, t, 0, float_of(K_18), result);
}

int em_player_running_jump_workers_bound(const EmPlayerRunningJumpWorkers *w)
{
    return w && w->scratch && w->sine && w->cosine && w->atan2 && w->sqrt && w->move && w->sweep &&
           w->table && w->ledge && w->column && w->link_type && w->target && w->target_xz &&
           w->target_radius && w->target_sight && w->heading && w->request && w->arbiter &&
           w->clip_frames && w->sound && w->translate && w->strafe && w->quadrant && w->react &&
           w->grab && w->dust && w->land && w->surface5d && w->teleport && w->probes && w->floor &&
           w->fall_check && w->root_clock;
}

/* ---- 0015EC50 ----------------------------------------------------------- */

static int in_range(F v, F lo, F hi) { return !lt(v, lo) && le(v, hi); }

static int in_box(const EmPlayerLiveActor *a, const Box *b)
{
    return in_range(ld(a, 0xB4), b->y0, b->y1) && in_range(ld(a, 0xB0), b->x0, b->x1) &&
           in_range(ld(a, 0xB8), b->z0, b->z1);
}

/* 0015ED08..0015F738: 1 when the record's +B0 lies in one of the area's
 * hand-placed boxes (0015EC50 then returns 0). */
static int excluded(const EmPlayerLiveActor *a, const EmPlayerRunningJumpScene *scene)
{
    unsigned area = scene->area, sub = scene->subarea;
    if (area == 4) return in_box(a, &kArea04);                          /* 0015ED14 */
    if (area == 0xD) {                                                  /* 0015EDE4 */
        if (sub != 0) return 0;                                         /* 0015EDF4 */
        for (unsigned i = 0; i < 3; ++i)
            if (in_box(a, &kArea0D[i])) return 1;
        return 0;
    }
    if (area == 0xF) {                                                  /* 0015F038 */
        if (sub != 1) return 0;                                         /* 0015F04C */
        if (in_range(ld(a, 0xB4), kArea0FY[0], kArea0FY[1])) {          /* 0015F068 */
            for (unsigned i = 0; i < 2; ++i) {
                const F *b = kArea0FXZ[i];
                if (in_range(ld(a, 0xB0), b[0], b[1]) && in_range(ld(a, 0xB8), b[2], b[3]))
                    return 1;
            }
        }
        return in_box(a, &kArea0F);                                     /* 0015F1B4 */
    }
    if (area == 0x10) {                                                 /* 0015F27C */
        if (sub != 1) return 0;                                         /* 0015F290 */
        return in_box(a, &kArea10[0]) || in_box(a, &kArea10[1]);
    }
    if (area == 0x13) {                                                 /* 0015F410 */
        if (sub != 0) return 0;                                         /* 0015F420 */
        for (unsigned i = 0; i < 3; ++i)
            if (in_box(a, &kArea13[i])) return 1;
        return 0;
    }
    if (area == 0x16) return in_box(a, &kArea16);                       /* 0015F678 */
    return 0;
}

/* The forward probe's distance (0015F7FC..0015F854 and 0015F890..0015F8E8):
 * 00177510, then 0x70003A20 = +B0 - point.x, 0x70003A28 = +B8 - point.z. */
static int ledge_distance(const EmPlayerRunningJumpWorkers *w, EmPlayerLiveActor *a,
                          const EmPlayerProbeHit *hit, EmPlayerRecoveryLedge *frame, F *dist)
{
    EmPlayerRunningJumpScratch *s = w->scratch;
    memset(frame, 0, sizeof *frame);
    FAULT(w->ledge(w->context, hit, frame));
    s->s3A20 = sub(ld(a, 0xB0), bits_of(frame->point[0]));
    F dz = sub(ld(a, 0xB8), bits_of(frame->point[2]));
    s->s3A28 = dz;
    *dist = distance(w, s->s3A20, dz);
    return 0;
}

static void enter_jump(EmPlayerLiveActor *a)
{
    put8(a, 5, 6);
    put8(a, 6, 0);
    put8(a, 0x1F0, 0xC);
}

static int probe(EmPlayerLiveActor *a, const EmPlayerRunningJumpScene *scene,
                 const EmPlayerRunningJumpWorkers *w, int *result)
{
    EmPlayerRunningJumpScratch *s = w->scratch;
    F *v = s->s38A0;                     /* v: A0, v + 4: B0, v + 8: C0, v + 12: D0 */
    *result = 0;
    if (u8(a, 0x314) == 1) return 0;                                   /* 0015EC70 */
    if (a->link_prev) {                                                /* 0015EC8C */
        uint8_t type = 0;
        FAULT(w->link_type(w->context, a->link_prev, &type));          /* 0015EC94 */
        if (type == 0x28) return 0;                                    /* 0015EC9C */
        if (type == 2) {                                               /* 0015ECB0 */
            F yaw = ld(a, 0xC4);
            if (!lt(yaw, K_HALF_PI)) return 0;                         /* 0015ECCC */
            if (le(yaw, K_M_HALF_PI)) return 0;                        /* 0015ECEC */
        }
    }
    if (excluded(a, scene)) return 0;

    /* 0015F740: the reach by the size tier +25C. */
    unsigned tier = u8(a, 0x25C);
    F reach = tier < 2 ? K_2 : tier == 2 ? K_3 : K_4;
    memcpy(v, kForward, sizeof kForward);                              /* 0015F790 00102948 */
    v[2] = mul(v[2], reach);                                           /* 0015F7B0 */
    F m[16];
    record_matrix(a, m);
    FAULT(apply_001026A0(m, v, v + 4));                                /* 0015F7BC */
    v[5] = add(v[5], K_ONE);                                           /* 0015F7DC */
    EmPlayerProbeHit hit;
    EmPlayerRecoveryLedge frame;
    int kind = 0, ahead = 0;
    F dist = 0;
    FAULT(w_move(w, a, v + 4, 7, &hit, &kind));                       /* 0015F7EC */
    if (kind != 0) {
        FAULT(ledge_distance(w, a, &hit, &frame, &dist));              /* 0015F7FC */
        ahead = 1;
    } else {
        v[5] = add(v[5], K_10);                                        /* 0015F870 */
        FAULT(w_move(w, a, v + 4, 7, &hit, &kind));                   /* 0015F880 */
        if (kind != 0) {
            FAULT(ledge_distance(w, a, &hit, &frame, &dist));          /* 0015F890 */
            ahead = 1;
        }
    }
    F pos[4];                            /* the stack vector at sp+0x60 (w never read) */
    if (ahead) {                                                       /* 0015F8F8 */
        pos[0] = bits_of(frame.point[0]);
        pos[2] = bits_of(frame.point[2]);
    } else {                                                           /* 0015F914 */
        pos[0] = v[4];
        pos[2] = v[6];
    }
    pos[1] = ld(a, 0xB4);                                              /* 0015F93C */
    pos[3] = 0;
    FAULT(yaw_matrix(s, ld(a, 0xC4), pos));                            /* 0015F940..0015F970 */

    /* The side sweeps: (-0.1, 10, 0) to (-5, 10, 0), then (0.1, 10, 0) to
     * (5, 10, 0), both in the 0x700036A0 frame (0015F978..0015FADC). */
    unsigned side = 0;
    for (unsigned k = 0; k < 2; ++k) {
        v[0] = k ? K_0_1 : K_M0_1;
        v[1] = K_10;
        v[2] = 0;
        v[3] = K_ONE;
        v[4] = k ? K_5 : K_M5;
        v[5] = K_10;
        v[6] = 0;
        v[7] = K_ONE;
        FAULT(apply_001026A0(s->s36A0, v, v + 8));                     /* 0015F9E0 / 0015FA94 */
        FAULT(apply_001026A0(s->s36A0, v + 4, v + 12));                /* 0015F9FC / 0015FAB0 */
        int blocked = 0;
        FAULT(w_sweep(w, a, v + 8, v + 12, 6, &hit, &blocked));        /* 0015FA18 / 0015FACC */
        if (blocked) side |= k ? 2u : 1u;
    }
    if (!ahead) {                                                      /* 0015FAE0 */
        if (side == 3) return 0;
    } else if (side != 0) {
        return 0;
    }

    /* The drop sweep from one below the feet (0015FB0C..0015FBB8). */
    pos[1] = sub(ld(a, 0xB4), K_ONE);                                  /* 0015FB1C */
    if (u8(a, 0x319) & 1) {                                            /* 0015FB24 */
        for (unsigned i = 0; i < 4; ++i) v[12 + i] = ld(a, 0xB0 + 4 * i);  /* 00102948 */
        v[13] = sub(v[13], K_ONE);                                     /* 0015FB58 */
    } else {
        v[0] = 0;                                                      /* 0015FB6C */
        v[1] = K_M1;
        v[2] = K_M2;
        v[3] = K_ONE;
        record_matrix(a, m);
        FAULT(apply_001026A0(m, v, v + 12));                           /* 0015FBA0 */
    }
    int down = 0;
    FAULT(w_sweep(w, a, pos, v + 12, 6, &hit, &down));                 /* 0015FBB8 */
    if (down == 0) return 0;
    if ((hit.node & 0xFF00u) != 0x2000u) return 0;                     /* 0015FBD4 */
    F px = bits_of(hit.point[0]), pz = bits_of(hit.point[2]);
    if (ahead) {                                                       /* 0015FBE4 */
        s->s3A20 = sub(ld(a, 0xB0), px);
        F dz = sub(ld(a, 0xB8), pz);
        s->s3A28 = dz;
        s->s3A2C = distance(w, s->s3A20, dz);                          /* 0015FC2C */
        if (le(dist, add(K_13_5, s->s3A2C))) return 0;                 /* 0015FC54 */
    }
    v[4] = add(px, mul(K_4_5, bits_of(hit.normal[0])));                /* 0015FC9C */
    v[6] = add(pz, mul(K_4_5, bits_of(hit.normal[2])));                /* 0015FCB4 */
    v[5] = ld(a, 0xB4);                                                /* 0015FCC8 */
    EmPlayerClimbTable table;
    float at[4];
    to_float(v + 4, at, 4);
    memset(&table, 0, sizeof table);
    FAULT(w->table(w->context, at, &table));                           /* 0015FCC4 */
    if (table.count < 0 || table.count > EM_PLAYER_CLIMB_TABLE_MAX) return -1;
    if (table.count == 0) {                                            /* 0015FCD4 */
        enter_jump(a);                                                 /* 0015FDB0 */
        *result = 1;
        return 0;
    }
    for (int i = table.count - 1; i >= 0; --i) {                       /* 0015FD14 */
        if (!(table.flags[i] & 1u)) continue;
        F y = ld(a, 0xB4);
        F h = bits_of(table.height[i]);
        if (!le(h, add(K_4_01, y))) continue;                          /* 0015FD30 */
        F drop = sub(h, y);                                            /* 0015FD58 */
        st(a, 0x254, drop);                                            /* 0015FD74 */
        if (!lt(drop, K_M4_01)) return 0;                              /* 0015FD68 */
        enter_jump(a);                                                 /* 0015FD7C */
        *result = 1;
        return 0;
    }
    return 0;
}

int em_player_running_jump_probe(EmPlayerLiveActor *actor, const EmPlayerRunningJumpScene *scene,
                                 const EmPlayerRunningJumpWorkers *workers, int *result)
{
    if (!actor || !scene || !result || !em_player_running_jump_workers_bound(workers)) return -1;
    return probe(actor, scene, workers, result);
}

/* ---- 001AA4E0 ----------------------------------------------------------- */

static int target_scan(EmPlayerLiveActor *a, const EmPlayerRunningJumpScene *scene,
                       const EmPlayerRunningJumpWorkers *w, const void **out)
{
    F best = K_1000;                                                   /* 001AA500 */
    const void *found = NULL;
    *out = NULL;
    if (scene->spad3B8D != 0) return 0;                                /* 001AA514 */
    int n = scene->target_count;                                       /* 001AA524 */
    if (n < 0) return -1;
    for (int i = 0; n != 0; ++i) {                                     /* 001AA530 */
        EmPlayerRunningJumpTarget t;
        memset(&t, 0, sizeof t);
        FAULT(w->target(w->context, i, &t));
        n -= 1;
        if ((t.flags & 0x1Fu) != 2u) continue;                         /* 001AA544 */
        if (t.field34 == 0) continue;                                  /* 001AA550 */
        switch (t.type) {                                              /* 001AA560..001AA5B4 */
        case 8: case 0xC: case 7: case 6: case 5: case 4: case 2: case 1:
            break;
        default:
            continue;
        }
        float radius = 0.0f;
        FAULT(w->target_radius(w->context, t.object, &radius));        /* 001AA5C4 */
        int seen = 0;
        FAULT(w->target_sight(w->context, a, t.object, radius, &seen)); /* 001AA5D4 */
        if (seen == 0) continue;
        F d = w->scratch->s3A20;                                       /* 001AA5E8 */
        if (lt(d, best)) {                                             /* 001AA5EC */
            best = d;
            found = t.object;
        }
    }
    *out = found;
    return 0;
}

int em_player_running_jump_target(EmPlayerLiveActor *actor, const EmPlayerRunningJumpScene *scene,
                                  const EmPlayerRunningJumpWorkers *workers, const void **target)
{
    if (!actor || !scene || !target || !em_player_running_jump_workers_bound(workers)) return -1;
    return target_scan(actor, scene, workers, target);
}

/* ---- 0015FDF0 ----------------------------------------------------------- */

/* One 25-unit probe along `angle` from the record's +B0 (0015FF80..00160010
 * and 001600AC..0016013C). */
static int aim_probe(const EmPlayerRunningJumpWorkers *w, EmPlayerLiveActor *a, F angle,
                     EmPlayerProbeHit *hit, int *kind)
{
    EmPlayerRunningJumpScratch *s = w->scratch;
    F at[3] = { ld(a, 0xB0), ld(a, 0xB4), ld(a, 0xB8) };
    FAULT(yaw_matrix(s, angle, at));
    s->s38A0[0] = 0;
    s->s38A0[1] = 0;
    s->s38A0[2] = K_25;
    s->s38A0[3] = K_ONE;
    FAULT(apply_001026A0(s->s36A0, s->s38A0, s->s38A0 + 4));
    return w_move(w, a, s->s38A0 + 4, 7, hit, kind);
}

/* The hit distance of an aim probe (00160028..0016006C, 00160154..00160198). */
static F aim_distance(const EmPlayerRunningJumpWorkers *w, EmPlayerLiveActor *a,
                      const EmPlayerProbeHit *hit)
{
    EmPlayerRunningJumpScratch *s = w->scratch;
    s->s3A20 = sub(bits_of(hit->point[0]), ld(a, 0xB0));
    F dz = sub(bits_of(hit->point[2]), ld(a, 0xB8));
    s->s3A28 = dz;
    return distance(w, s->s3A20, dz);
}

static int aim(EmPlayerLiveActor *a, const EmPlayerRunningJumpScene *scene,
               const EmPlayerRunningJumpWorkers *w, int *result)
{
    EmPlayerRunningJumpScratch *s = w->scratch;
    const void *target = NULL;
    *result = 0;
    FAULT(target_scan(a, scene, w, &target));                          /* 0015FE0C */
    if (!target) return 0;                                             /* 0015FE14 */
    *result = 1;
    float tx = 0.0f, tz = 0.0f;
    FAULT(w->target_xz(w->context, target, &tx, &tz));
    s->s38A0[0] = sub(bits_of(tx), ld(a, 0xB0));                        /* 0015FE28 */
    F dz = sub(bits_of(tz), ld(a, 0xB8));                               /* 0015FE3C */
    s->s38A0[2] = dz;
    F t = bits_of(w->atan2(w->context, float_of(neg(dz)), float_of(s->s38A0[0])));  /* 0015FE4C */
    F ang = 0;
    FAULT(wrap(add(K_HALF_PI, t), &ang));                              /* 0015FE60 */
    int turned = 0;
    FAULT(w->heading(w->context, a, 2, &turned));                      /* 0015FE70 */
    if (turned != 0 && u8(a, 0x23F) >= 2) {                            /* 0015FE78 */
        FAULT(wrap(sub(ang, ld(a, 0x218)), &s->s3A20));                /* 0015FE94 */
        if (!lt(fabs_0011DF78(s->s3A20), K_AIM)) {                     /* 0015FEBC */
            st(a, 0xC4, ld(a, 0x218));                                 /* 0015FED8 */
            return 0;
        }
    }
    FAULT(wrap(sub(ang, ld(a, 0xC4)), &s->s3A20));                     /* 0015FEE0 */
    if (!lt(fabs_0011DF78(s->s3A20), K_AIM)) return 0;                 /* 0015FF08 */
    F d = 0, first = 0;
    FAULT(wrap(sub(ang, ld(a, 0xC4)), &d));                            /* 0015FF24 */
    int left = lt(d, K_ZERO);                                          /* 0015FF34 */
    if (left) FAULT(wrap(add(K_AIM, ang), &first));                    /* 0015FF70 */
    else FAULT(wrap(sub(ang, K_AIM), &first));                         /* 0015FF50 */
    EmPlayerProbeHit hit;
    int kind = 0;
    FAULT(aim_probe(w, a, first, &hit, &kind));
    if (kind == 0) {                                                   /* 00160014 */
        st(a, 0xC4, first);
        return 0;
    }
    F clear = aim_distance(w, a, &hit);
    F second = 0;
    if (!left) FAULT(wrap(add(K_AIM, ang), &second));                  /* 00160084 */
    else FAULT(wrap(sub(ang, K_AIM), &second));                        /* 001600A0 */
    FAULT(aim_probe(w, a, second, &hit, &kind));
    if (kind == 0) {                                                   /* 00160140 */
        st(a, 0xC4, second);
        return 0;
    }
    F other = aim_distance(w, a, &hit);
    if (!lt(other, clear)) {                                           /* 0016019C */
        st(a, 0xC4, second);                                           /* 001601B0 */
        return 0;
    }
    F back = 0;
    if (!left) FAULT(wrap(sub(ang, K_AIM), &back));                    /* 001601C8 */
    else FAULT(wrap(add(K_AIM, ang), &back));                          /* 001601E4 */
    st(a, 0xC4, back);
    return 0;
}

int em_player_running_jump_aim(EmPlayerLiveActor *actor, const EmPlayerRunningJumpScene *scene,
                               const EmPlayerRunningJumpWorkers *workers, int *result)
{
    if (!actor || !scene || !result || !em_player_running_jump_workers_bound(workers)) return -1;
    return aim(actor, scene, workers, result);
}

/* ---- 001634A0 ----------------------------------------------------------- */

/* 00179880(p, p + 0x2E4). */
static void drop_2E4(EmPlayerLiveActor *a)
{
    F v = add(ld(a, 0x2E4), K_M0_04);                                  /* 00179894 */
    st(a, 0x2E4, v);                                                   /* 001798AC */
    if (lt(v, K_M4)) st(a, 0x2E4, K_M4);                               /* 001798A0 */
    st(a, 0xB4, add(ld(a, 0xB4), ld(a, 0x2E4)));                       /* 001798C0 */
    put8(a, 0x25F, 2);                                                 /* 001798CC */
}

/* The airborne head of cases 2 and 3: 002243F0; with no reaction, 0017C860
 * on +2E4 may take the actor (then the callback returns). *go = 0 then. */
static int airborne_head(EmPlayerLiveActor *a, const EmPlayerRunningJumpWorkers *w, int *reacted,
                         int *go)
{
    *go = 0;
    FAULT(w->react(w->context, a, reacted));                           /* 00163678 / 00163844 */
    if (*reacted == 0) {
        int grabbed = 0;
        FAULT(w->grab(w->context, a, ld(a, 0x2E4), &grabbed));         /* 00163690 / 0016385C */
        if (grabbed != 0) return 0;
    }
    FAULT(w->quadrant(w->context, a));                                 /* 001636A4 / 00163870 */
    *go = 1;
    return 0;
}

/* +2E4 > 0: 001760C0(p, p+B0, 0, 18.0) stops the rise under a roof; else
 * +25F = 2 (0016374C..00163788, 001638D0..0016390C). */
static int ceiling(EmPlayerLiveActor *a, const EmPlayerRunningJumpWorkers *w)
{
    if (!le(ld(a, 0x2E4), K_ZERO)) {
        int covered = 0;
        FAULT(w_column(w, a, &covered));
        if (covered != 0) st(a, 0x2E4, K_ZERO);
    } else {
        put8(a, 0x25F, 2);
    }
    return 0;
}

static int jump_case0(EmPlayerLiveActor *a, const EmPlayerRunningJumpWorkers *w)
{
    put8(a, 6, 1);                                                     /* 001634F8 */
    put8(a, 7, 0);
    if (u8(a, 0x25C) < 2) {                                            /* 00163504 */
        FAULT(w->request(w->context, a, 0x6A, 0, float_of(K_8)));      /* 0016351C */
    } else {
        int32_t frames = 0;
        FAULT(w->clip_frames(w->context, ld(a, 0x40), 0x69, &frames)); /* 00163530 */
        F f = em_ee_cvt_s_w_bits(em_ee_int_word(frames));              /* 00163540 */
        w->scratch->s3A20 = f;                                         /* 00163554 */
        FAULT(w->arbiter(w->context, a, 0x69, float_of(K_4), float_of(sub(f, K_4))));  /* 00163558 */
    }
    st(a, 0x2F4, ld(a, 0xB4));                                         /* 00163568 */
    return 0;
}

static int jump_case1(EmPlayerLiveActor *a, const EmPlayerRunningJumpWorkers *w)
{
    if (!(ld(a, 0x200) & 0x1000u)) return 0;                           /* 00163574 */
    put8(a, 6, 2);                                                     /* 00163580 */
    if (u8(a, 0x25C) < 2) {                                            /* 00163588 */
        st(a, 0x38, K_4_1);                                            /* 001635A0 */
        FAULT(w->translate(w->context, a, 1));                         /* 0016359C */
        FAULT(w->request(w->context, a, 0x6C, 0, float_of(K_ZERO)));   /* 001635B0 */
    } else {
        st(a, 0x38, K_6_3);                                            /* 001635CC */
        FAULT(w->translate(w->context, a, 1));                         /* 001635C8 */
        FAULT(w->request(w->context, a, 0x6B, 0, float_of(K_ZERO)));   /* 001635DC */
        st(a, 0x2EC, K_M0_03);                                         /* 001635EC */
    }
    unsigned tier = u8(a, 0x25C);                                      /* 001635F0 */
    if (tier > 3) return -1;          /* D_002485D0 / D_002485B0 end at tier 3 */
    st(a, 0x2EC, kGravity[tier]);                                      /* 00163610 */
    tier = u8(a, 0x25C);                                               /* 00163614 */
    const F *row = kLaunch[tier];
    F c = bits_of(w->cosine(w->context, float_of(row[1])));            /* 00163620 */
    st(a, 0x38, mul(row[0], c));                                       /* 00163630 */
    F s = bits_of(w->sine(w->context, float_of(row[1])));              /* 00163634 */
    st(a, 0x2E4, mul(row[0], s));                                      /* 00163654 */
    st(a, 0x270, dvd(ld(a, 0x38), K_3));                               /* 00163660 */
    em_live_set_u16(a, 0x2E, 0);                                       /* 00163664 */
    put8(a, 0x25F, 1);                                                 /* 0016366C */
    return w->dust(w->context, a);                                     /* 00163668 */
}

static int jump_case2(EmPlayerLiveActor *a, const EmPlayerRunningJumpWorkers *w)
{
    int reacted = 0, go = 0;
    FAULT(airborne_head(a, w, &reacted, &go));
    if (!go) return 0;
    if (ld(a, 0x24C) == 1u) {                                          /* 001636B4 */
        if (u8(a, 0x23F) >= 2) {                                       /* 001636C4 */
            F v = mul(ld(a, 0x38), K_0_9);                             /* 001636E0 */
            st(a, 0x38, v);
            F floor = ld(a, 0x270);
            if (lt(v, floor)) st(a, 0x38, floor);                      /* 001636EC */
        }
    } else {
        FAULT(w->strafe(w->context, a));                               /* 00163708 */
    }
    FAULT(w->translate(w->context, a, 1));                             /* 00163714 */
    F v = add(ld(a, 0x2E4), mul(K_2, ld(a, 0x2EC)));                   /* 00163730 / 00163734 */
    st(a, 0x2E4, v);
    st(a, 0xB4, add(ld(a, 0xB4), v));                                  /* 00163740 */
    FAULT(ceiling(a, w));
    if (le(ld(a, 0xB4), ld(a, 0x2F4)) || u8(a, 0x314) == 1) {          /* 00163794 / 001637AC */
        put8(a, 6, u8(a, 6) + 1);                                      /* 001637C4 */
        st(a, 0x2E0, dvd(ld(a, 0x38), K_30));                          /* 001637CC */
        st(a, 0x2EC, ld(a, 0x2E4));                                    /* 001637D8 */
    }
    if (le(ld(a, 0x2E4), K_ZERO)) {                                    /* 001637E8 */
        int contact = 0;
        FAULT(w->floor(w->context, a, 1, &contact));                   /* 001637FC */
        if (u8(a, 0xA) != 0) {                                         /* 00163808 */
            st(a, 0x38, K_ZERO);                                       /* 00163814 */
            if (reacted == 0) FAULT(w->land(w->context, a));           /* 00163818 */
        }
    }
    if (u8(a, 0x23A) == 0x5D) return w->surface5d(w->context, a, 0);   /* 00163834 */
    return 0;
}

static int jump_case3(EmPlayerLiveActor *a, const EmPlayerRunningJumpWorkers *w)
{
    int reacted = 0, go = 0;
    FAULT(airborne_head(a, w, &reacted, &go));
    if (!go) return 0;
    F speed = ld(a, 0x38), decay = ld(a, 0x2E0);
    if (!le(speed, decay)) {                                           /* 00163880 */
        st(a, 0x38, sub(speed, decay));                                /* 0016389C */
        FAULT(w->strafe(w->context, a));                               /* 00163898 */
    } else {
        st(a, 0x38, K_ZERO);                                           /* 001638A8 */
    }
    FAULT(w->translate(w->context, a, 1));                             /* 001638B0 */
    drop_2E4(a);                                                       /* 001638BC */
    FAULT(ceiling(a, w));
    int contact = 0;
    FAULT(w->floor(w->context, a, 1, &contact));                       /* 00163914 */
    if (u8(a, 0xA) != 0) {                                             /* 00163920 */
        st(a, 0x38, K_ZERO);                                           /* 0016392C */
        if (reacted == 0) FAULT(w->land(w->context, a));               /* 00163930 */
    } else if (le(ld(a, 0x38), K_ZERO) && reacted == 0) {              /* 0016394C / 0016395C */
        put8(a, 5, 7);                                                 /* 00163968 */
        put8(a, 6, 0);
        put8(a, 0x1F0, 0xD);
        FAULT(w->request(w->context, a, 0x72, 0, float_of(K_8)));      /* 00163988 */
        st(a, 0x2EC, ld(a, 0x2E4));                                    /* 00163994 */
    }
    if (u8(a, 0x23A) == 0x5D) return w->surface5d(w->context, a, 0);   /* 001639AC */
    return 0;
}

int em_player_running_jump_tick(EmPlayerLiveActor *a, const EmPlayerRunningJumpWorkers *w)
{
    if (!a || !em_player_running_jump_workers_bound(w)) return -1;
    switch (u8(a, 6)) {                                                /* 001634B0 */
    case 0: return jump_case0(a, w);
    case 1: return jump_case1(a, w);
    case 2: return jump_case2(a, w);
    case 3: return jump_case3(a, w);
    case 0x63: return w->teleport(w->context, a, 0x78, 0);             /* 001639C0 */
    default: return 0;
    }
}

/* ---- 001747F0 ----------------------------------------------------------- */

int em_player_running_jump_state24_tick(EmPlayerLiveActor *a, const EmPlayerRunningJumpWorkers *w)
{
    if (!a || !em_player_running_jump_workers_bound(w)) return -1;
    unsigned sub_state = u8(a, 6);                                     /* 001747FC */
    if (sub_state == 0) {
        put8(a, 6, 1);                                                 /* 0017482C */
        put8(a, 7, 0);
        int clip = u8(a, 0x236) == 0 ? 0x3E : 0x5D;                    /* 00174838 */
        FAULT(w->request(w->context, a, clip, 0, float_of(K_ZERO)));   /* 00174848 / 00174860 */
        st(a, 0x38, K_ZERO);                                           /* 0017486C */
        put8(a, 0x25C, 1);                                             /* 00174870 */
        st(a, 0x21C, K_ZERO);                                          /* 00174878 */
        st(a, 0x2E4, K_ZERO);                                          /* 00174890 */
        FAULT(w->sound(w->context, a, 0x186));                         /* 0017488C */
    } else if (sub_state == 1 || sub_state == 2) {
        if (sub_state == 1 && le(ld(a, 0x3C), K_10)) {                 /* 001748AC */
            put8(a, 6, 2);                                             /* 001748C0 */
            put8(a, 0, 1);                                             /* 001748C4 */
        }
        if (ld(a, 0x200) & 0x1000u) {                                  /* 001748CC */
            put8(a, 5, 0);                                             /* 001748D8 */
            put8(a, 6, 0);
            put8(a, 0x1F0, 0);
        } else {
            F clock = 0;
            FAULT(w->root_clock(w->context, &clock));                  /* 001748FC */
            st(a, 0x38, sub(clock, ld(a, 0x21C)));                     /* 00174900 */
            FAULT(w->root_clock(w->context, &clock));                  /* 00174910 */
            st(a, 0x21C, clock);                                       /* 00174918 */
            FAULT(w->translate(w->context, a, 0));                     /* 00174914 */
        }
    }
    FAULT(w->probes(w->context, a, EM_PLAYER_LAND_S1_CALLER));         /* 00174920 */
    st(a, 0xB4, add(ld(a, 0xB4), K_M0_2));                             /* 0017493C */
    int contact = 0;
    FAULT(w->floor(w->context, a, 1, &contact));                       /* 00174944 */
    FAULT(w->fall_check(w->context, a));                               /* 0017494C */
    if (u8(a, 4) == 1 && u8(a, 5) != 0x24 && u8(a, 0) != 1)            /* 00174954..00174978 */
        put8(a, 0, 1);                                                 /* 00174980 */
    return 0;
}

/* ---- Binding adapters ------------------------------------------------------ */

static int live_ready(const EmPlayerRunningJumpLive *l, const EmPlayerLiveActor *a)
{
    return l && a && em_player_running_jump_workers_bound(&l->workers);
}

static void load_shared(const EmPlayerRunningJumpLive *l)
{
    if (l->shared3A20) l->workers.scratch->s3A20 = *l->shared3A20;
}

static int store_shared(const EmPlayerRunningJumpLive *l, int status)
{
    if (l->shared3A20) *l->shared3A20 = l->workers.scratch->s3A20;
    return status;
}

static int live_scene(const EmPlayerRunningJumpLive *l, EmPlayerRunningJumpScene *scene)
{
    if (!l->scene) return -1;
    memset(scene, 0, sizeof *scene);
    return l->scene(l->scene_context, scene) < 0 ? -1 : 0;
}

int em_player_running_jump_state6(void *context, EmPlayerLiveActor *actor)
{
    const EmPlayerRunningJumpLive *l = context;
    if (!live_ready(l, actor)) return -1;
    load_shared(l);
    return store_shared(l, em_player_running_jump_tick(actor, &l->workers));
}

int em_player_running_jump_state24(void *context, EmPlayerLiveActor *actor)
{
    const EmPlayerRunningJumpLive *l = context;
    if (!live_ready(l, actor)) return -1;
    load_shared(l);
    return store_shared(l, em_player_running_jump_state24_tick(actor, &l->workers));
}

int em_player_running_jump_use_probe(void *context, EmPlayerLiveActor *actor, int *result)
{
    const EmPlayerRunningJumpLive *l = context;
    EmPlayerRunningJumpScene scene;
    if (!live_ready(l, actor) || !result || live_scene(l, &scene) < 0) return -1;
    load_shared(l);
    return store_shared(l, probe(actor, &scene, &l->workers, result));
}

int em_player_running_jump_use_aim(void *context, EmPlayerLiveActor *actor, int *result)
{
    const EmPlayerRunningJumpLive *l = context;
    EmPlayerRunningJumpScene scene;
    if (!live_ready(l, actor) || !result || live_scene(l, &scene) < 0) return -1;
    load_shared(l);
    return store_shared(l, aim(actor, &scene, &l->workers, result));
}
