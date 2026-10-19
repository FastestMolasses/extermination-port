/* AREA03 00823930 turret, from the matching decomp C and this project's
 * independently verified AREA19 twin. UNWIRED. AREA03 uses different effect
 * scratch vectors and no secondary collision probe. Its original instructions
 * are the oracle; the shared shape alone is not evidence. */
#include "em_area03_route_internal.h"

#define D_00275B40 0x00275B40u /* gp - 0x7830 */
#define S_3190 0x70003190u
#define S_31A0 0x700031A0u
#define S_31B0 0x700031B0u
#define S_31D0 0x700031D0u
#define S_31D4 0x700031D4u
#define S_31D8 0x700031D8u
#define S_3600 0x70003600u
#define S_3604 0x70003604u
#define S_3608 0x70003608u
#define S_360C 0x7000360Cu
#define S_3610 0x70003610u
#define S_3618 0x70003618u
#define S_3680 0x70003680u
#define S_3684 0x70003684u
#define S_38A0 0x700038A0u
#define S_38AC 0x700038ACu
#define S_38B0 0x700038B0u
#define S_38B4 0x700038B4u
#define S_38B8 0x700038B8u
#define S_38BC 0x700038BCu
#define S_3910 0x70003910u
#define S_391C 0x7000391Cu
#define S_3A20 0x70003A20u
#define S_3B68 0x70003B68u

/* Float constants (bit patterns). */
#define K_LIM 0x3F91361Eu      /* 1.134464f */
#define K_MLIM 0xBF91361Eu     /* -1.134464f */
#define K_PI 0x40490FDBu       /* 3.1415927f */
#define K_MPI 0xC0490FDBu      /* -3.1415927f */
#define K_MHALFPI 0xBFC90FDBu  /* -1.5707964f */
#define K_BASE 0xBF543B67u     /* -0.8290314f */
#define K_SWING 0x3E9C61AAu    /* 0.30543262f */
#define K_STEP 0x3C3EA2F1u     /* 0.011635528f */
#define K_MSTEP 0xBC3EA2F1u    /* -0.011635528f */
#define K_EPS 0x3A83126Fu      /* 0.001f */
#define K_BMIN 0xBF060A92u     /* -0.5235988f */
#define K_128 0x43000000u      /* 128.0f */
#define K_90 0x42B40000u       /* 90.0f */
#define K_60 0x42700000u       /* 60.0f */
#define K_300 0x43960000u      /* 300.0f */
#define K_ONE 0x3F800000u      /* 1.0f */
#define K_QUARTER 0x3E800000u  /* 0.25f */
#define K_FIVE 0x40A00000u     /* 5.0f */
#define K_ZERO 0x00000000u

static inline float fv(uint32_t bits) { return em_ee_float(bits); }

/* Arithmetic right shift of a 32-bit word (the EE shift). */
static inline int32_t asr32(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* 00122BB8 result scaled as the original does: ((r >> 16) * mul) >> 15,
 * in 32-bit arithmetic. */
static int32_t a03r_rand_scaled(A03R *o, uint32_t mul)
{
    int32_t r = 0;
    (void)a03r_c_00122BB8(o, &r);
    return asr32((uint32_t)asr32((uint32_t)r, 16) * mul, 15);
}

static uint32_t a03r_block(A03R *o) { return a03r_u32(o, D_00275B40); }
static uint32_t a03r_rec_a(A03R *o) { return a03r_u32(o, a03r_block(o) + 8); }
static uint32_t a03r_rec_b(A03R *o) { return a03r_u32(o, a03r_block(o) + 0xC); }

static void a03r_neg_word(A03R *o, uint32_t address)
{
    a03r_w32(o, address, em_ee_neg_bits(a03r_u32(o, address)));
}

static uint32_t a03r_sine(A03R *o, uint32_t arg_bits)
{
    float s = 0.0f;
    (void)a03r_c_0011E2A8(o, fv(arg_bits), &s);
    return em_ee_bits(s);
}

/* B+0x78 = -0.8290314 + 0.30543262 * sin(+0x1FC) */
static void a03r_pitch_from_phase(A03R *o, uint32_t self)
{
    uint32_t s = a03r_sine(o, a03r_u32(o, self + 0x1FC));
    uint32_t product = em_ee_mul_bits(K_SWING, s);
    uint32_t b = a03r_rec_b(o);
    a03r_w32(o, b + 0x78, em_ee_add_bits(K_BASE, product));
}

/* +0x1FC += step; above pi it becomes -pi. The step is loaded first. */
static void a03r_phase_advance(A03R *o, uint32_t self, uint32_t step_offset)
{
    uint32_t step = a03r_u32(o, self + step_offset);
    uint32_t phase = em_ee_add_bits(a03r_u32(o, self + 0x1FC), step);
    a03r_w32(o, self + 0x1FC, phase);
    if (!em_ee_c_le_bits(phase, K_PI)) a03r_w32(o, self + 0x1FC, K_MPI);
}

/* Companion colour words at +0xA0..+0xAC of the record at +0x220. */
static void a03r_companion(A03R *o, uint32_t self, uint32_t a0, uint32_t a4, uint32_t a8, uint32_t ac)
{
    a03r_w32(o, a03r_u32(o, self + 0x220) + 0xA0, a0);
    a03r_w32(o, a03r_u32(o, self + 0x220) + 0xA4, a4);
    a03r_w32(o, a03r_u32(o, self + 0x220) + 0xA8, a8);
    a03r_w32(o, a03r_u32(o, self + 0x220) + 0xAC, ac);
}

/* 00102958(companion(+0x11C) + 0x90, self(+0x11C) + 0x90) */
static void a03r_companion_matrix(A03R *o, uint32_t self)
{
    uint32_t companion = a03r_u32(o, self + 0x220);
    uint32_t own = a03r_u32(o, self + 0x11C);
    uint32_t dst = a03r_u32(o, companion + 0x11C);
    (void)a03r_c_00102958(o, dst + 0x90, own + 0x90);
}

static void a03r_draw_with_matrix(A03R *o, uint32_t self)
{
    int32_t r = 0;
    (void)a03r_c_001C6380(o, self);
    (void)a03r_c_001A2370(o, self, a03r_rec_b(o) + 0x90);
    (void)a03r_c_001B17A0(o, self, &r);
    a03r_method(o, self);
}

static void a03r_draw_plain(A03R *o, uint32_t self)
{
    int32_t r = 0;
    (void)a03r_c_001C6380(o, self);
    (void)a03r_c_001B17A0(o, self, &r);
    a03r_method(o, self);
}

/* A+0x74 += +0x210, then clamp to +-1.134464, reversing +0x210 on a clamp. */
static void a03r_sweep_a(A03R *o, uint32_t self)
{
    uint32_t step = a03r_u32(o, self + 0x210);
    uint32_t a = a03r_rec_a(o);
    a03r_w32(o, a + 0x74, em_ee_add_bits(a03r_u32(o, a + 0x74), step));
    a = a03r_rec_a(o);
    if (!em_ee_c_le_bits(a03r_u32(o, a + 0x74), K_LIM)) {
        a03r_w32(o, a + 0x74, K_LIM);
        a03r_neg_word(o, self + 0x210);
    }
    a = a03r_rec_a(o);
    if (em_ee_c_lt_bits(a03r_u32(o, a + 0x74), K_MLIM)) {
        a03r_w32(o, a + 0x74, K_MLIM);
        a03r_neg_word(o, self + 0x210);
    }
}

/* The two-random re-arm used when +0x36 is raised while +0x208 is 0. */
static void a03r_rearm(A03R *o, uint32_t self)
{
    a03r_w32(o, self + 0x208, 0x1E0);
    a03r_w32(o, self + 0x20C, (uint32_t)a03r_rand_scaled(o, 30));
    if (a03r_u32(o, self + 0x20C) & 8) a03r_neg_word(o, self + 0x210);
    a03r_w32(o, self + 0x214, (uint32_t)a03r_rand_scaled(o, 30));
    if (a03r_u32(o, self + 0x214) & 4) a03r_neg_word(o, self + 0x218);
}

/* States 4 and 1 while +0x208 > 0: count it down; below 0x1F fade the
 * companion (state 4 in lane +0xA4, state 1 in lane +0xA0); otherwise sweep
 * A and the phase with random re-seeding. Then draw; +0x36 = 0. */
static void a03r_countdown(A03R *o, uint32_t self, int state1)
{
    int32_t left, count;
    int32_t r = 0;
    a03r_w32(o, self + 0x208, a03r_u32(o, self + 0x208) - 1u);
    left = a03r_s32(o, self + 0x208);
    if (left < 0x1F) {
        uint32_t fade = em_ee_div_bits(em_ee_cvt_s_w_bits(0x80u - (uint32_t)left * 4u), K_128);
        a03r_w32(o, S_3A20, fade);
        if (state1) {
            a03r_w32(o, a03r_u32(o, self + 0x220) + 0xA0, fade);
            a03r_w32(o, a03r_u32(o, self + 0x220) + 0xA4, 0);
        } else {
            a03r_w32(o, a03r_u32(o, self + 0x220) + 0xA0, 0);
            {   /* 0x70003A20 is loaded before the pointer */
                uint32_t v = a03r_u32(o, S_3A20);
                a03r_w32(o, a03r_u32(o, self + 0x220) + 0xA4, v);
            }
        }
        a03r_w32(o, a03r_u32(o, self + 0x220) + 0xA8, 0);
        a03r_w32(o, a03r_u32(o, self + 0x220) + 0xAC, K_QUARTER);
        a03r_companion_matrix(o, self);
    } else {
        count = (int32_t)(a03r_u32(o, self + 0x20C) - 1u);
        a03r_w32(o, self + 0x20C, (uint32_t)count);
        if (count > 0) {
            a03r_sweep_a(o, self);
        } else {
            (void)a03r_c_001FBD50(o, self, 0x428, 0, fv(K_300), &r);
            a03r_w32(o, self + 0x20C, (uint32_t)a03r_rand_scaled(o, 30));
            if (a03r_u32(o, self + 0x20C) & 4) a03r_neg_word(o, self + 0x210);
        }
        count = (int32_t)(a03r_u32(o, self + 0x214) - 1u);
        a03r_w32(o, self + 0x214, (uint32_t)count);
        if (count > 0) {
            a03r_phase_advance(o, self, 0x218);
            a03r_pitch_from_phase(o, self);
        } else {
            a03r_w32(o, self + 0x214, (uint32_t)a03r_rand_scaled(o, 30));
            if (a03r_u32(o, self + 0x214) & 8) a03r_neg_word(o, self + 0x218);
        }
        a03r_companion(o, self, 0, 0, 0, 0);
    }
    a03r_draw_with_matrix(o, self);
    a03r_w16(o, self + 0x36, 0);
}

/* State 0: set-up. */
static void a03r_setup(A03R *o, uint32_t self)
{
    int32_t r = 0;
    uint32_t companion = 0;
    (void)a03r_c_001B0FD0(o, self, &r);
    if (r != 0) return;
    a03r_w8(o, self + 4, 4);
    a03r_w8(o, self + 0, 1);
    a03r_w16(o, self + 0x28, (uint32_t)(a03r_rand_scaled(o, 300) + 0x12C));
    a03r_w32(o, self + 0x200, 1);
    a03r_w32(o, self + 0x204, 0);
    a03r_w32(o, self + 0x208, 0);
    a03r_w32(o, self + 0x210, 0x3DD67750u); /* 0.10471976f */
    a03r_w32(o, self + 0x218, 0x3DD67750u);
    a03r_w32(o, self + 0x20C, 0);
    a03r_w32(o, self + 0x214, 0);
    a03r_w32(o, self + 0x1F4, 0x3C84C3C4u); /* 0.01620663f */
    a03r_w32(o, self + 0x1F8, 0x3D37D3FBu); /* 0.044879895f */
    a03r_w32(o, self + 0x1FC, 0);
    {
        uint32_t s = a03r_sine(o, a03r_u32(o, self + 0x1FC));
        uint32_t product = em_ee_mul_bits(K_BASE, s);
        uint32_t b = a03r_rec_b(o);
        a03r_w32(o, b + 0x78, em_ee_add_bits(K_MLIM, product));
    }
    (void)a03r_c_001C6380(o, self);
    (void)a03r_c_001A2370(o, self, a03r_rec_b(o) + 0x90);
    a03r_w32(o, self + 0x220, 0);
    (void)a03r_c_001AFA90(o, 0xC, &companion);
    if (companion == 0) return;
    a03r_w8(o, companion + 0x9A, 0);
    a03r_w8(o, companion + 3, 0);
    a03r_w16(o, companion + 0x2E, 0);
    a03r_w8(o, companion + 0xD, 0x7A);
    a03r_w16(o, companion + 0xE, 0xFFFF);
    a03r_w16(o, companion + 0x54, 0);
    a03r_w16(o, companion + 0x56, 0);
    a03r_w32(o, companion + 0xA0, 0);
    a03r_w32(o, companion + 0xA4, 0);
    a03r_w32(o, companion + 0xA8, 0);
    a03r_w32(o, companion + 0xAC, K_QUARTER);
    (void)a03r_c_00102948(o, companion + 0xB0, self + 0xB0);
    (void)a03r_c_00102948(o, companion + 0xC0, self + 0xC0);
    a03r_w32(o, companion + 0x10, 0x001C5680u);
    a03r_w32(o, self + 0x220, companion);
    a03r_w32(o, self + 0x224, 0);
}

/* State 4 with +0x208 <= 0: idle sweep. */
static void a03r_state4_idle(A03R *o, uint32_t self)
{
    int32_t r = 0;
    a03r_companion(o, self, 0, K_ONE, 0, K_QUARTER);
    a03r_companion_matrix(o, self);
    a03r_w16(o, self + 0x28, (uint32_t)(a03r_s16(o, self + 0x28) - 1));
    if (a03r_s16(o, self + 0x28) < 0) {
        if (a03r_u32(o, self + 0x200) != 0)
            a03r_w16(o, self + 0x28, (uint32_t)(a03r_rand_scaled(o, 180) + 0x3C));
        else
            a03r_w16(o, self + 0x28, (uint32_t)(a03r_rand_scaled(o, 300) + 0x12C));
        a03r_w32(o, self + 0x200, a03r_u32(o, self + 0x200) == 0);
    }
    if (a03r_u32(o, self + 0x200) != 0) {
        uint32_t a;
        if ((a03r_s16(o, self + 0x28) & 0x2F) == 2)
            (void)a03r_c_001FBD50(o, self, 0x423, 0, fv(K_60), &r);
        {
            uint32_t step = a03r_u32(o, self + 0x1F4);
            a = a03r_rec_a(o);
            a03r_w32(o, a + 0x74, em_ee_add_bits(a03r_u32(o, a + 0x74), step));
        }
        if (!em_ee_c_le_bits(a03r_u32(o, self + 0x1F4), K_ZERO)) {
            a = a03r_rec_a(o);
            if (!em_ee_c_le_bits(a03r_u32(o, a + 0x74), K_LIM)) {
                a03r_w32(o, a + 0x74, K_LIM);
                a03r_neg_word(o, self + 0x1F4);
            }
        } else {
            a = a03r_rec_a(o);
            if (em_ee_c_lt_bits(a03r_u32(o, a + 0x74), K_MLIM)) {
                a03r_w32(o, a + 0x74, K_MLIM);
                a03r_neg_word(o, self + 0x1F4);
            }
        }
        a03r_phase_advance(o, self, 0x1F8);
        a03r_pitch_from_phase(o, self);
    }
    a03r_draw_with_matrix(o, self);
    r = 0;
    (void)a03r_c_00824D30(o, self, a03r_rec_b(o) + 0x90, &r);
    if (a03r_s16(o, self + 0x36) != 0 && a03r_u32(o, self + 0x208) == 0) a03r_rearm(o, self);
    a03r_w16(o, self + 0x36, 0);
    if (a03r_u32(o, self + 0x204) != 0) {
        a03r_w8(o, self + 4, 1);
        a03r_w32(o, self + 0x224, 1);
        a03r_w16(o, self + 0x28, (uint32_t)-0x2B);
        a03r_w16(o, self + 0x2A, 0x12C);
        a03r_w32(o, self + 0x200, 0);
        if ((a03r_u32(o, S_3B68) & 0x3F) == 0)
            (void)a03r_c_001FBD50(o, self, 0x424, 0, fv(K_300), &r);
    }
}

/* Effect after a positive 0x824D30 result at the +0x200 period. */
static void a03r_period_effect(A03R *o, uint32_t self)
{
    int32_t r = 0;
    uint32_t hit, kind, t;
    (void)a03r_c_001FBD50(o, self, 0x425, 0, fv(K_300), &r);
    (void)a03r_c_00825200(o, a03r_rec_b(o) + 0x90);
    (void)a03r_c_00102948(o, S_3600, S_31B0);
    hit = a03r_u32(o, S_31D0);
    kind = a03r_u32(o, S_31D8);
    a03r_w32(o, S_3610, a03r_u32(o, hit + 0x24));
    a03r_w32(o, 0x70003614u, a03r_u32(o, hit + 0x28));
    a03r_w32(o, S_3618, a03r_u32(o, hit + 0x2C));
    a03r_w32(o, 0x7000361Cu, K_ONE);
    a03r_w32(o, S_360C, K_ONE);
    if (kind == 1) {
        t = a03r_u32(o, S_31D4);
        if (a03r_u8(o, t + 2) & 0x1F) {
            (void)a03r_c_001EFD90(o, (int32_t)0x80000007u, S_3600, S_3610, &r);
            a03r_w16(o, a03r_u32(o, S_31D4) + 0x36, 5);
        } else if (!(a03r_u8(o, t + 0) & 2)) {
            (void)a03r_c_001EFD90(o, (int32_t)0x80000006u, S_3600, S_3610, &r);
            a03r_w32(o, a03r_u32(o, S_31D4) + 0x224, K_FIVE);
            t = a03r_u32(o, S_31D4);
            a03r_w8(o, t + 0, a03r_u8(o, t + 0) | 2);
            (void)a03r_c_001028D0(o, S_3910, S_31A0, S_3190);
            a03r_w32(o, S_391C, 0);
            (void)a03r_c_00102760(o, S_3910, S_3910);
            (void)a03r_c_00102948(o, a03r_u32(o, S_31D4) + 0x70, S_3910);
        }
    } else {
        (void)a03r_c_001EFD90(o, (int32_t)0x80000003u, S_3600, S_3610, &r);
    }
}

/* State 1 with +0x208 <= 0: track the record at +0x204. */
static void a03r_state1_track(A03R *o, uint32_t self)
{
    int32_t r = 0, result = 0;
    uint32_t block, a, f2, f3, acc, k;
    if ((a03r_u32(o, S_3B68) & 0x3F) == 0)
        (void)a03r_c_001FBD50(o, self, 0x424, 0, fv(K_300), &r);
    a03r_companion(o, self, K_ONE, 0, 0, K_QUARTER);
    a03r_companion_matrix(o, self);

    /* direction from A (+0xC0) to the tracked record (+0xB0), flattened */
    block = a03r_block(o);
    {
        uint32_t target = a03r_u32(o, self + 0x204);
        uint32_t rec_a = a03r_u32(o, block + 8);
        (void)a03r_c_001028D0(o, S_3600, target + 0xB0, rec_a + 0xC0);
    }
    (void)a03r_c_00102948(o, S_3610, S_3600);
    a03r_w32(o, S_360C, 0);
    a03r_w32(o, S_3604, 0);
    (void)a03r_c_00102760(o, S_3600, S_3600);

    /* side = dir.x * -A.x + dir.z * -A.z; step A+0x74 toward the target */
    block = a03r_block(o);
    {
        uint32_t f3v = a03r_u32(o, S_3600);
        uint32_t rec_a = a03r_u32(o, block + 8);
        uint32_t f4 = a03r_u32(o, rec_a + 0xB0);
        uint32_t f2v = a03r_u32(o, rec_a + 0xB8);
        uint32_t f1 = a03r_u32(o, S_3608);
        f4 = em_ee_neg_bits(f4);
        acc = em_ee_mula_bits(f3v, f4);
        f2v = em_ee_neg_bits(f2v);
        f2 = em_ee_madd_bits(acc, f1, f2v);
    }
    a03r_w32(o, S_3680, f2);
    if (em_ee_c_lt_bits(f2, K_MSTEP)) {
        a = a03r_u32(o, block + 8);
        a03r_w32(o, a + 0x74, em_ee_sub_bits(a03r_u32(o, a + 0x74), K_STEP));
    } else if (!em_ee_c_le_bits(f2, K_STEP)) {
        a = a03r_u32(o, block + 8);
        a03r_w32(o, a + 0x74, em_ee_add_bits(a03r_u32(o, a + 0x74), K_STEP));
    } else {
        float mag = 0.0f;
        (void)a03r_c_0011DF78(o, fv(a03r_u32(o, S_3610)), &mag);
        if (!em_ee_c_le_bits(em_ee_bits(mag), K_EPS)) {
            uint32_t x = a03r_u32(o, S_3610);
            float at = 0.0f, wrapped = 0.0f;
            uint32_t q = em_ee_div_bits(a03r_u32(o, S_3618), x);
            a03r_w32(o, S_3684, q);
            (void)a03r_c_0011DBB8(o, fv(q), &at);
            if (em_ee_c_lt_bits(x, K_ZERO))
                a03r_w32(o, S_3684, em_ee_sub_bits(K_PI, em_ee_bits(at)));
            else
                a03r_w32(o, S_3684, em_ee_neg_bits(em_ee_bits(at)));
            {
                uint32_t yaw = a03r_u32(o, self + 0xC4);
                uint32_t heading = a03r_u32(o, S_3684);
                (void)a03r_c_001B1470(o, fv(em_ee_sub_bits(heading, yaw)), &wrapped);
            }
            a03r_w32(o, a03r_rec_a(o) + 0x74, em_ee_bits(wrapped));
        }
    }
    if (em_ee_c_lt_bits(a03r_u32(o, a03r_rec_a(o) + 0x74), K_MLIM)) {
        a03r_w16(o, self + 0x2A, (uint32_t)(a03r_s16(o, self + 0x2A) - 4));
        a03r_w32(o, a03r_rec_a(o) + 0x74, K_MLIM);
    }
    if (!em_ee_c_le_bits(a03r_u32(o, a03r_rec_a(o) + 0x74), K_LIM)) {
        a03r_w16(o, self + 0x2A, (uint32_t)(a03r_s16(o, self + 0x2A) - 4));
        a03r_w32(o, a03r_rec_a(o) + 0x74, K_LIM);
    }

    /* elevation: atan(dir.y / |dir.xz|) stepped into B+0x78 */
    (void)a03r_c_00102948(o, S_3600, S_3610);
    {
        uint32_t x = a03r_u32(o, S_3600), z = a03r_u32(o, S_3608);
        float root = 0.0f, at = 0.0f;
        acc = em_ee_mula_bits(x, x);
        (void)a03r_c_0011E748(o, fv(em_ee_madd_bits(acc, z, z)), &root);
        a03r_w32(o, S_3680, em_ee_bits(root));
        {
            uint32_t y = a03r_u32(o, S_3604);
            uint32_t len = a03r_u32(o, S_3680);
            (void)a03r_c_0011DBB8(o, fv(em_ee_div_bits(y, len)), &at);
        }
        block = a03r_block(o);
        k = K_STEP;
        a03r_w32(o, S_3680, em_ee_bits(at));
        {
            uint32_t b = a03r_u32(o, block + 0xC);
            uint32_t cur;
            f3 = a03r_u32(o, S_3680);
            cur = a03r_u32(o, b + 0x78);
            if (!em_ee_c_le_bits(f3, em_ee_sub_bits(cur, k))) {
                a03r_w32(o, b + 0x78, em_ee_add_bits(a03r_u32(o, b + 0x78), k));
            } else if (em_ee_c_lt_bits(f3, em_ee_add_bits(k, cur))) {
                a03r_w32(o, b + 0x78, em_ee_sub_bits(a03r_u32(o, b + 0x78), k));
            } else {
                a03r_w32(o, b + 0x78, f3);
            }
        }
    }
    if (em_ee_c_lt_bits(a03r_u32(o, a03r_rec_b(o) + 0x78), K_MLIM)) {
        a03r_w16(o, self + 0x2A, (uint32_t)(a03r_s16(o, self + 0x2A) - 4));
        a03r_w32(o, a03r_rec_b(o) + 0x78, K_MLIM);
    }
    if (!em_ee_c_le_bits(a03r_u32(o, a03r_rec_b(o) + 0x78), K_BMIN)) {
        a03r_w16(o, self + 0x2A, (uint32_t)(a03r_s16(o, self + 0x2A) - 4));
        a03r_w32(o, a03r_rec_b(o) + 0x78, K_BMIN);
    }

    a03r_draw_plain(o, self);
    a03r_w16(o, self + 0x28, (uint32_t)(a03r_s16(o, self + 0x28) + 1));
    (void)a03r_c_00824D30(o, self, a03r_rec_b(o) + 0x90, &result);
    if (result == 2) a03r_w16(o, self + 0x2A, 0x12C);
    else a03r_w16(o, self + 0x2A, (uint32_t)(a03r_s16(o, self + 0x2A) - 1));

    if (a03r_u32(o, self + 0x200) != 0) {
        a03r_w32(o, self + 0x200, a03r_u32(o, self + 0x200) + 1u);
        if (a03r_s32(o, self + 0x200) >= 0xE) {
            if (result != 0) a03r_period_effect(o, self);
            a03r_w32(o, self + 0x200, 1);
        }
    } else if (a03r_s16(o, self + 0x28) > 0) {
        a03r_w32(o, self + 0x200, 1);
        a03r_w16(o, self + 0x28, 0);
    }
    if (a03r_s16(o, self + 0x36) != 0 && a03r_u32(o, self + 0x208) == 0) a03r_rearm(o, self);
    a03r_w16(o, self + 0x36, 0);
    if (a03r_s16(o, self + 0x2A) < 0) {
        uint32_t f1, q;
        float as = 0.0f, wrapped = 0.0f;
        a03r_w8(o, self + 4, 4);
        a03r_w32(o, self + 0x224, 0);
        a03r_w32(o, self + 0x204, 0);
        f1 = em_ee_neg_bits(em_ee_sub_bits(a03r_u32(o, a03r_rec_b(o) + 0x78), K_MLIM));
        q = em_ee_div_bits(f1, K_BASE);
        (void)a03r_c_0011E520(o, fv(q), &as);
        (void)a03r_c_001B1470(o, as, &wrapped);
        a03r_w32(o, self + 0x1FC, em_ee_bits(wrapped));
        a03r_w16(o, self + 0x28, (uint32_t)(a03r_rand_scaled(o, 300) + 0x12C));
        a03r_w32(o, self + 0x200, 1);
    }
}

/* State 2: bring the phase to -pi/2, then count
 * +0x21C down, fading the companion lane chosen by +0x224. */
static void a03r_state2(A03R *o, uint32_t self)
{
    {
        uint32_t phase = a03r_u32(o, self + 0x1FC);
        if (em_ee_c_eq_bits(K_MHALFPI, phase)) {
            if (a03r_s32(o, self + 0x21C) > 0) {
                uint32_t fade;
                a03r_w32(o, self + 0x21C, a03r_u32(o, self + 0x21C) - 1u);
                fade = em_ee_div_bits(em_ee_cvt_s_w_bits(a03r_u32(o, self + 0x21C)), K_90);
                a03r_w32(o, S_3A20, fade);
                if (a03r_u32(o, self + 0x224) != 0)
                    a03r_companion(o, self, a03r_u32(o, S_3A20), 0, 0, K_QUARTER);
                else {
                    uint32_t v;
                    a03r_w32(o, a03r_u32(o, self + 0x220) + 0xA0, 0);
                    v = a03r_u32(o, S_3A20); /* loaded before the pointer */
                    a03r_w32(o, a03r_u32(o, self + 0x220) + 0xA4, v);
                    a03r_w32(o, a03r_u32(o, self + 0x220) + 0xA8, 0);
                    a03r_w32(o, a03r_u32(o, self + 0x220) + 0xAC, K_QUARTER);
                }
            }
        } else {
            uint32_t v;
            uint32_t step;
            if (em_ee_c_lt_bits(phase, K_MHALFPI)) {
                step = a03r_u32(o, self + 0x1F8); /* the step is loaded first */
                v = em_ee_add_bits(a03r_u32(o, self + 0x1FC), step);
                a03r_w32(o, self + 0x1FC, v);
                if (!em_ee_c_le_bits(v, K_MHALFPI)) a03r_w32(o, self + 0x1FC, K_MHALFPI);
            } else {
                step = a03r_u32(o, self + 0x1F8);
                v = em_ee_sub_bits(a03r_u32(o, self + 0x1FC), step);
                a03r_w32(o, self + 0x1FC, v);
                if (em_ee_c_lt_bits(v, K_MHALFPI)) a03r_w32(o, self + 0x1FC, K_MHALFPI);
            }
            a03r_pitch_from_phase(o, self);
            a03r_companion_matrix(o, self);
        }
    }
    a03r_draw_plain(o, self);
}

static int turret_00823930(A03R *o, uint32_t self)
{
    switch (a03r_u8(o, self + 4)) {
    case 0:
        a03r_setup(o, self);
        break;
    case 4:
        if (a03r_s32(o, self + 0x208) > 0) a03r_countdown(o, self, 0);
        else a03r_state4_idle(o, self);
        break;
    case 1:
        if (a03r_s32(o, self + 0x208) > 0) a03r_countdown(o, self, 1);
        else a03r_state1_track(o, self);
        break;
    case 2:
        a03r_state2(o, self);
        break;
    default: /* 3 and any other value */
        (void)a03r_c_001AFC10(o, self);
        break;
    }
    return a03r_failed(o) ? -1 : 0;
}

int em_area03_route_00823930(const EmArea03RouteHooks *h, uint32_t self, EmArea03RouteFault *fault)
{
    A03R o; if (a03r_begin(&o, h, fault)) return -1;
    return turret_00823930(&o, self);
}
