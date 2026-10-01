/* AREA13's deferred-group owner 0x824BB0 (four in the group 0x829D00, the
 * outdoor watchers; the AREA13 twin of AREA01 0x826D40) and its probe
 * 0x826140 (the twin of AREA01 0x8282F0). See em_level9_port.h and
 * docs/LEVEL9_PORT.md. The decomp's C of both is byte-identical (lane
 * A13C); the translation follows the instructions' order of loads and
 * stores. Its shape follows the AREA01 translations (em_area01_overlay_
 * 826d40.c, em_area01_revisit.c) with the AREA13 differences, read from
 * the AREA13 instructions:
 *   - state 0 always goes on to state 4 (AREA01 tests 001BA1C0(self, 6)
 *     and may wait in state 0x64; AREA13 has no state 0x64: it frees);
 *   - states 4 and 1 do nothing while D_00810702 (the arrival entry) is
 *     below 8 (the player has not been outside);
 *   - state 2 runs without the 001BA1C0(self, 6) test;
 *   - when the period effect's 0019B6C0 misses, the byte +0x1A of the
 *     probe's hit record (read before the call) selects 0x8000002C when it
 *     is 5, else 0x80000003;
 *   - 0x826140's tint vector is the overlay's 0x82C9D0.
 *
 * Record fields (offsets; roles only as the code uses them): +0x04
 * state, +0x28 / +0x2A halfword counters, +0x36 halfword flag, +0x1F4 /
 * +0x1F8 steps, +0x1FC phase, +0x200 word flag / counter, +0x204 the
 * tracked record, +0x208 / +0x20C / +0x214 countdowns, +0x210 / +0x218
 * steps, +0x21C countdown, +0x220 the companion (class 0xC, behaviour
 * 001C5680), +0x224 the colour lane. D_00275B40 points at a block whose
 * +0x08 / +0x0C are the records A (+0x74 yaw) and B (+0x78 pitch, +0x90
 * matrix).
 */
#include "em_level9_port_internal.h"

#define K_LIM 0x3F91361Eu      /* 1.134464f */
#define K_MLIM 0xBF91361Eu     /* -1.134464f */
#define K_MHALFPI 0xBFC90FDBu  /* -1.5707964f */
#define K_BASE 0xBF543B67u     /* -0.8290314f */
#define K_SWING 0x3E9C61AAu    /* 0.30543262f */
#define K_STEP 0x3C3EA2F1u     /* 0.011635528f */
#define K_MSTEP 0xBC3EA2F1u    /* -0.011635528f */
#define K_EPS 0x3A83126Fu      /* 0.001f */
#define K_BMIN 0xBF060A92u     /* -0.5235988f */
#define K_QUARTER 0x3E800000u  /* 0.25f */
#define K_0_8 0x3F4CCCCDu      /* 0.8f */
#define K_10000 0x461C4000u    /* 10000.0f */
#define K_M2 0xC0000000u       /* -2.0f */

/* 00122BB8 scaled as the original does: ((r >> 16) * mul) >> 15 (32-bit,
 * arithmetic shifts). */
static int32_t rand_scaled(L9 *o, uint32_t mul)
{
    int32_t r = 0;
    l9_c_00122BB8(o, &r);
    return l9_sra((uint32_t)l9_sra((uint32_t)r, 16) * mul, 15);
}

static uint32_t block(L9 *o) { return l9_u32(o, D_00275B40); }
static uint32_t rec_a(L9 *o) { return l9_u32(o, block(o) + 8); }
static uint32_t rec_b(L9 *o) { return l9_u32(o, block(o) + 0xC); }

static void neg_word(L9 *o, uint32_t address) { l9_w32(o, address, L9_NEG(l9_u32(o, address))); }

static uint32_t sine(L9 *o, uint32_t arg)
{
    uint32_t s = 0;
    l9_c_0011E2A8(o, arg, &s);
    return s;
}

static void sound(L9 *o, uint32_t self, int32_t id, uint32_t range)
{
    int32_t r = 0;
    l9_c_001FBD50(o, self, id, 0, range, &r);
}

/* B+0x78 = -0.8290314 + 0.30543262 * sin(+0x1FC) */
static void pitch_from_phase(L9 *o, uint32_t self)
{
    uint32_t s = sine(o, l9_u32(o, self + 0x1FC));
    uint32_t product = L9_MUL(K_SWING, s);
    uint32_t b = rec_b(o);
    l9_w32(o, b + 0x78, L9_ADD(K_BASE, product));
}

/* +0x1FC += step; above pi it becomes -pi. The step is loaded first. */
static void phase_advance(L9 *o, uint32_t self, uint32_t step_offset)
{
    uint32_t step = l9_u32(o, self + step_offset);
    uint32_t phase = L9_ADD(l9_u32(o, self + 0x1FC), step);
    l9_w32(o, self + 0x1FC, phase);
    if (!L9_LE(phase, F_PI)) l9_w32(o, self + 0x1FC, F_MPI);
}

static void companion(L9 *o, uint32_t self, uint32_t a0, uint32_t a4, uint32_t a8, uint32_t ac)
{
    l9_w32(o, l9_u32(o, self + 0x220) + 0xA0, a0);
    l9_w32(o, l9_u32(o, self + 0x220) + 0xA4, a4);
    l9_w32(o, l9_u32(o, self + 0x220) + 0xA8, a8);
    l9_w32(o, l9_u32(o, self + 0x220) + 0xAC, ac);
}

/* 00102958(companion(+0x11C) + 0x90, self(+0x11C) + 0x90) */
static void companion_matrix(L9 *o, uint32_t self)
{
    uint32_t comp = l9_u32(o, self + 0x220);
    uint32_t own = l9_u32(o, self + 0x11C);
    uint32_t dst = l9_u32(o, comp + 0x11C);
    l9_c_00102958(o, dst + 0x90, own + 0x90);
}

static void draw_with_matrix(L9 *o, uint32_t self)
{
    int32_t r = 0;
    if (l9_c_001C6380(o, self)) return;
    if (l9_c_001A2370(o, self, rec_b(o) + 0x90)) return;
    if (l9_c_001B17A0(o, self, &r)) return;
    l9_method(o, self);
}

static void draw_plain(L9 *o, uint32_t self)
{
    int32_t r = 0;
    if (l9_c_001C6380(o, self)) return;
    if (l9_c_001B17A0(o, self, &r)) return;
    l9_method(o, self);
}

static void sweep_a(L9 *o, uint32_t self)
{
    uint32_t step = l9_u32(o, self + 0x210);
    uint32_t a = rec_a(o);
    l9_w32(o, a + 0x74, L9_ADD(l9_u32(o, a + 0x74), step));
    a = rec_a(o);
    if (!L9_LE(l9_u32(o, a + 0x74), K_LIM)) {
        l9_w32(o, a + 0x74, K_LIM);
        neg_word(o, self + 0x210);
    }
    a = rec_a(o);
    if (L9_LT(l9_u32(o, a + 0x74), K_MLIM)) {
        l9_w32(o, a + 0x74, K_MLIM);
        neg_word(o, self + 0x210);
    }
}

static void rearm(L9 *o, uint32_t self)
{
    l9_w32(o, self + 0x208, 0x1E0);
    l9_w32(o, self + 0x20C, (uint32_t)rand_scaled(o, 30));
    if (l9_u32(o, self + 0x20C) & 8u) neg_word(o, self + 0x210);
    l9_w32(o, self + 0x214, (uint32_t)rand_scaled(o, 30));
    if (l9_u32(o, self + 0x214) & 4u) neg_word(o, self + 0x218);
}

/* States 4 and 1 while +0x208 > 0. */
static void countdown(L9 *o, uint32_t self, int state1)
{
    int32_t left, count;
    l9_w32(o, self + 0x208, l9_u32(o, self + 0x208) - 1u);
    left = l9_s32(o, self + 0x208);
    if (left < 0x1F) {
        uint32_t fade = L9_DIV(L9_CVT(0x80u - (uint32_t)left * 4u), F_128);
        l9_w32(o, S_70003A20, fade);
        if (state1) {
            l9_w32(o, l9_u32(o, self + 0x220) + 0xA0, fade);
            l9_w32(o, l9_u32(o, self + 0x220) + 0xA4, 0);
        } else {
            l9_w32(o, l9_u32(o, self + 0x220) + 0xA0, 0);
            uint32_t v = l9_u32(o, S_70003A20);
            l9_w32(o, l9_u32(o, self + 0x220) + 0xA4, v);
        }
        l9_w32(o, l9_u32(o, self + 0x220) + 0xA8, 0);
        l9_w32(o, l9_u32(o, self + 0x220) + 0xAC, K_QUARTER);
        companion_matrix(o, self);
    } else {
        count = (int32_t)(l9_u32(o, self + 0x20C) - 1u);
        l9_w32(o, self + 0x20C, (uint32_t)count);
        if (count > 0) {
            sweep_a(o, self);
        } else {
            sound(o, self, 0x428, F_300);
            l9_w32(o, self + 0x20C, (uint32_t)rand_scaled(o, 30));
            if (l9_u32(o, self + 0x20C) & 4u) neg_word(o, self + 0x210);
        }
        count = (int32_t)(l9_u32(o, self + 0x214) - 1u);
        l9_w32(o, self + 0x214, (uint32_t)count);
        if (count > 0) {
            phase_advance(o, self, 0x218);
            pitch_from_phase(o, self);
        } else {
            l9_w32(o, self + 0x214, (uint32_t)rand_scaled(o, 30));
            if (l9_u32(o, self + 0x214) & 8u) neg_word(o, self + 0x218);
        }
        companion(o, self, 0, 0, 0, 0);
    }
    draw_with_matrix(o, self);
    l9_w16(o, self + 0x36, 0);
}

static void setup(L9 *o, uint32_t self)
{
    int32_t r = 0;
    uint32_t comp = 0;
    if (l9_c_001B0FD0(o, self, &r) || r != 0) return;
    l9_w8(o, self + 4, 4);
    l9_w8(o, self + 0, 1);
    l9_w16(o, self + 0x28, (uint32_t)(rand_scaled(o, 300) + 0x12C));
    l9_w32(o, self + 0x200, 1);
    l9_w32(o, self + 0x204, 0);
    l9_w32(o, self + 0x208, 0);
    l9_w32(o, self + 0x210, 0x3DD67750u); /* 0.10471976f */
    l9_w32(o, self + 0x218, 0x3DD67750u);
    l9_w32(o, self + 0x20C, 0);
    l9_w32(o, self + 0x214, 0);
    l9_w32(o, self + 0x1F4, 0x3C84C3C4u); /* 0.01620663f */
    l9_w32(o, self + 0x1F8, 0x3D37D3FBu); /* 0.044879895f */
    l9_w32(o, self + 0x1FC, 0);
    {
        uint32_t s = sine(o, l9_u32(o, self + 0x1FC));
        uint32_t product = L9_MUL(K_BASE, s);
        uint32_t b = rec_b(o);
        l9_w32(o, b + 0x78, L9_ADD(K_MLIM, product));
    }
    if (l9_c_001C6380(o, self)) return;
    if (l9_c_001A2370(o, self, rec_b(o) + 0x90)) return;
    l9_w32(o, self + 0x220, 0);
    if (l9_c_001AFA90(o, 0xC, &comp) || comp == 0) return;
    l9_w8(o, comp + 0x9A, 0);
    l9_w8(o, comp + 3, 0);
    l9_w16(o, comp + 0x2E, 0);
    l9_w8(o, comp + 0xD, 0x7A);
    l9_w16(o, comp + 0xE, 0xFFFF);
    l9_w16(o, comp + 0x54, 0);
    l9_w16(o, comp + 0x56, 0);
    l9_w32(o, comp + 0xA0, 0);
    l9_w32(o, comp + 0xA4, 0);
    l9_w32(o, comp + 0xA8, 0);
    l9_w32(o, comp + 0xAC, K_QUARTER);
    if (l9_c_00102948(o, comp + 0xB0, self + 0xB0)) return;
    if (l9_c_00102948(o, comp + 0xC0, self + 0xC0)) return;
    l9_w32(o, comp + 0x10, 0x001C5680u);
    l9_w32(o, self + 0x220, comp);
    l9_w32(o, self + 0x224, 0);
}

static void state4_idle(L9 *o, uint32_t self, uint32_t sp)
{
    companion(o, self, 0, F_ONE, 0, K_QUARTER);
    companion_matrix(o, self);
    l9_w16(o, self + 0x28, (uint32_t)(l9_s16(o, self + 0x28) - 1));
    if (l9_s16(o, self + 0x28) < 0) {
        if (l9_u32(o, self + 0x200) != 0)
            l9_w16(o, self + 0x28, (uint32_t)(rand_scaled(o, 180) + 0x3C));
        else
            l9_w16(o, self + 0x28, (uint32_t)(rand_scaled(o, 300) + 0x12C));
        l9_w32(o, self + 0x200, l9_u32(o, self + 0x200) == 0);
    }
    if (l9_u32(o, self + 0x200) != 0) {
        uint32_t a;
        if ((l9_s16(o, self + 0x28) & 0x2F) == 2) sound(o, self, 0x423, F_60);
        {
            uint32_t step = l9_u32(o, self + 0x1F4);
            a = rec_a(o);
            l9_w32(o, a + 0x74, L9_ADD(l9_u32(o, a + 0x74), step));
        }
        if (!L9_LE(l9_u32(o, self + 0x1F4), F_ZERO)) {
            a = rec_a(o);
            if (!L9_LE(l9_u32(o, a + 0x74), K_LIM)) {
                l9_w32(o, a + 0x74, K_LIM);
                neg_word(o, self + 0x1F4);
            }
        } else {
            a = rec_a(o);
            if (L9_LT(l9_u32(o, a + 0x74), K_MLIM)) {
                l9_w32(o, a + 0x74, K_MLIM);
                neg_word(o, self + 0x1F4);
            }
        }
        phase_advance(o, self, 0x1F8);
        pitch_from_phase(o, self);
    }
    draw_with_matrix(o, self);
    (void)l9_00826140(o, self, rec_b(o) + 0x90, sp);
    if (l9_s16(o, self + 0x36) != 0 && l9_u32(o, self + 0x208) == 0) rearm(o, self);
    l9_w16(o, self + 0x36, 0);
    if (l9_u32(o, self + 0x204) != 0) {
        l9_w8(o, self + 4, 1);
        l9_w32(o, self + 0x224, 1);
        l9_w16(o, self + 0x28, (uint32_t)-0x2B);
        l9_w16(o, self + 0x2A, 0x12C);
        l9_w32(o, self + 0x200, 0);
        if ((l9_u32(o, S_70003B68) & 0x3Fu) == 0) sound(o, self, 0x424, F_300);
    }
}

static void period_effect(L9 *o, uint32_t self)
{
    int32_t r = 0;
    uint32_t hit, kind, t;
    sound(o, self, 0x425, F_300);
    if (l9_c_00826610(o, rec_b(o) + 0x90)) return;
    if (l9_c_00102948(o, S_700038A0, S_700031B0)) return;
    hit = l9_u32(o, S_700031D0);
    kind = l9_u32(o, S_700031D8);
    l9_w32(o, S_700038B0, l9_u32(o, hit + 0x24));
    l9_w32(o, S_700038B4, l9_u32(o, hit + 0x28));
    l9_w32(o, S_700038B8, l9_u32(o, hit + 0x2C));
    l9_w32(o, S_700038BC, F_ONE);
    l9_w32(o, S_700038AC, F_ONE);
    if (kind == 1) {
        t = l9_u32(o, S_700031D4);
        if (l9_u8(o, t + 2) & 0x1Fu) {
            if (l9_c_001EFD90(o, (int32_t)0x80000007u, S_700038A0, S_700038B0)) return;
            l9_w16(o, l9_u32(o, S_700031D4) + 0x36, 5);
        } else if (!(l9_u8(o, t + 0) & 2u)) {
            if (l9_c_001EFD90(o, (int32_t)0x80000006u, S_700038A0, S_700038B0)) return;
            l9_w32(o, l9_u32(o, S_700031D4) + 0x224, F_5);
            t = l9_u32(o, S_700031D4);
            l9_w8(o, t + 0, l9_u8(o, t + 0) | 2u);
            if (l9_c_001028D0(o, S_70003910, S_700031A0, S_70003190)) return;
            l9_w32(o, S_7000391C, 0);
            if (l9_c_00102760(o, S_70003910, S_70003910)) return;
            l9_c_00102948(o, l9_u32(o, S_700031D4) + 0x70, S_70003910);
        }
        return;
    }
    {
        int32_t earlier = (int32_t)l9_u8(o, hit + 0x1A);
        if (l9_c_0019B6C0(o, S_70003190, S_700031A0, hit, &r)) return;
        if (r == 0) {
            l9_c_001EFD90(o, (int32_t)(earlier == 5 ? 0x8000002Cu : 0x80000003u), S_700038A0, S_700038B0);
            return;
        }
    }
    {
        uint32_t id;
        if (l9_c_00102948(o, S_700038A0, S_700031B0)) return;
        hit = l9_u32(o, S_700031D0);
        l9_w32(o, S_700038B0, l9_u32(o, hit + 0x24));
        l9_w32(o, S_700038B4, l9_u32(o, hit + 0x28));
        l9_w32(o, S_700038B8, l9_u32(o, hit + 0x2C));
        l9_w32(o, S_700038BC, F_ONE);
        l9_w32(o, S_700038AC, F_ONE);
        switch (l9_u8(o, hit + 0x1A)) {
        case 0x5C: id = 0x80000067u; break;
        case 0x5B: id = 0x80000026u; break;
        case 0x5A: id = 0x8000002Cu; break;
        default: id = 0x80000003u; break;
        }
        l9_c_001EFD90(o, (int32_t)id, S_700038A0, S_700038B0);
    }
}

static void state1_track(L9 *o, uint32_t self, uint32_t sp)
{
    int32_t result = 0;
    uint32_t blk, a, f2, f3, acc, k;
    if ((l9_u32(o, S_70003B68) & 0x3Fu) == 0) sound(o, self, 0x424, F_300);
    companion(o, self, F_ONE, 0, 0, K_QUARTER);
    companion_matrix(o, self);

    blk = block(o);
    {
        uint32_t target = l9_u32(o, self + 0x204);
        uint32_t ra = l9_u32(o, blk + 8);
        if (l9_c_001028D0(o, S_70003600, target + 0xB0, ra + 0xC0)) return;
    }
    if (l9_c_00102948(o, S_70003610, S_70003600)) return;
    l9_w32(o, S_7000360C, 0);
    l9_w32(o, S_70003604, 0);
    if (l9_c_00102760(o, S_70003600, S_70003600)) return;

    blk = block(o);
    {
        uint32_t f3v = l9_u32(o, S_70003600);
        uint32_t ra = l9_u32(o, blk + 8);
        uint32_t f4 = l9_u32(o, ra + 0xB0);
        uint32_t f2v = l9_u32(o, ra + 0xB8);
        uint32_t f1 = l9_u32(o, S_70003608);
        f4 = L9_NEG(f4);
        acc = L9_MULA(f3v, f4);
        f2v = L9_NEG(f2v);
        f2 = L9_MADD(acc, f1, f2v);
    }
    l9_w32(o, S_70003680, f2);
    if (L9_LT(f2, K_MSTEP)) {
        a = l9_u32(o, blk + 8);
        l9_w32(o, a + 0x74, L9_SUB(l9_u32(o, a + 0x74), K_STEP));
    } else if (!L9_LE(f2, K_STEP)) {
        a = l9_u32(o, blk + 8);
        l9_w32(o, a + 0x74, L9_ADD(l9_u32(o, a + 0x74), K_STEP));
    } else {
        uint32_t mag = 0;
        if (l9_c_0011DF78(o, l9_u32(o, S_70003610), &mag)) return;
        if (!L9_LE(mag, K_EPS)) {
            uint32_t x = l9_u32(o, S_70003610);
            uint32_t at = 0, wrapped = 0;
            uint32_t q = L9_DIV(l9_u32(o, S_70003618), x);
            l9_w32(o, S_70003684, q);
            if (l9_c_0011DBB8(o, q, &at)) return;
            if (L9_LT(x, F_ZERO))
                l9_w32(o, S_70003684, L9_SUB(F_PI, at));
            else
                l9_w32(o, S_70003684, L9_NEG(at));
            {
                uint32_t yaw = l9_u32(o, self + 0xC4);
                uint32_t heading = l9_u32(o, S_70003684);
                if (l9_c_001B1470(o, L9_SUB(heading, yaw), &wrapped)) return;
            }
            l9_w32(o, rec_a(o) + 0x74, wrapped);
        }
    }
    if (L9_LT(l9_u32(o, rec_a(o) + 0x74), K_MLIM)) {
        l9_w16(o, self + 0x2A, (uint32_t)(l9_s16(o, self + 0x2A) - 4));
        l9_w32(o, rec_a(o) + 0x74, K_MLIM);
    }
    if (!L9_LE(l9_u32(o, rec_a(o) + 0x74), K_LIM)) {
        l9_w16(o, self + 0x2A, (uint32_t)(l9_s16(o, self + 0x2A) - 4));
        l9_w32(o, rec_a(o) + 0x74, K_LIM);
    }

    if (l9_c_00102948(o, S_70003600, S_70003610)) return;
    {
        uint32_t x = l9_u32(o, S_70003600), z = l9_u32(o, S_70003608);
        uint32_t root = 0, at = 0;
        acc = L9_MULA(x, x);
        if (l9_c_0011E748(o, L9_MADD(acc, z, z), &root)) return;
        l9_w32(o, S_70003680, root);
        {
            uint32_t y = l9_u32(o, S_70003604);
            uint32_t len = l9_u32(o, S_70003680);
            if (l9_c_0011DBB8(o, L9_DIV(y, len), &at)) return;
        }
        blk = block(o);
        k = K_STEP;
        l9_w32(o, S_70003680, at);
        {
            uint32_t b = l9_u32(o, blk + 0xC);
            uint32_t cur;
            f3 = l9_u32(o, S_70003680);
            cur = l9_u32(o, b + 0x78);
            if (!L9_LE(f3, L9_SUB(cur, k))) {
                l9_w32(o, b + 0x78, L9_ADD(l9_u32(o, b + 0x78), k));
            } else if (L9_LT(f3, L9_ADD(k, cur))) {
                l9_w32(o, b + 0x78, L9_SUB(l9_u32(o, b + 0x78), k));
            } else {
                l9_w32(o, b + 0x78, f3);
            }
        }
    }
    if (L9_LT(l9_u32(o, rec_b(o) + 0x78), K_MLIM)) {
        l9_w16(o, self + 0x2A, (uint32_t)(l9_s16(o, self + 0x2A) - 4));
        l9_w32(o, rec_b(o) + 0x78, K_MLIM);
    }
    if (!L9_LE(l9_u32(o, rec_b(o) + 0x78), K_BMIN)) {
        l9_w16(o, self + 0x2A, (uint32_t)(l9_s16(o, self + 0x2A) - 4));
        l9_w32(o, rec_b(o) + 0x78, K_BMIN);
    }

    draw_plain(o, self);
    l9_w16(o, self + 0x28, (uint32_t)(l9_s16(o, self + 0x28) + 1));
    result = l9_00826140(o, self, rec_b(o) + 0x90, sp);
    if (result == 2) l9_w16(o, self + 0x2A, 0x12C);
    else l9_w16(o, self + 0x2A, (uint32_t)(l9_s16(o, self + 0x2A) - 1));

    if (l9_u32(o, self + 0x200) != 0) {
        l9_w32(o, self + 0x200, l9_u32(o, self + 0x200) + 1u);
        if (l9_s32(o, self + 0x200) >= 0xE) {
            if (result != 0) period_effect(o, self);
            l9_w32(o, self + 0x200, 1);
        }
    } else if (l9_s16(o, self + 0x28) > 0) {
        l9_w32(o, self + 0x200, 1);
        l9_w16(o, self + 0x28, 0);
    }
    if (l9_s16(o, self + 0x36) != 0 && l9_u32(o, self + 0x208) == 0) rearm(o, self);
    l9_w16(o, self + 0x36, 0);
    if (l9_s16(o, self + 0x2A) < 0) {
        uint32_t f1, q, as = 0, wrapped = 0;
        l9_w8(o, self + 4, 4);
        l9_w32(o, self + 0x224, 0);
        l9_w32(o, self + 0x204, 0);
        f1 = L9_NEG(L9_SUB(l9_u32(o, rec_b(o) + 0x78), K_MLIM));
        q = L9_DIV(f1, K_BASE);
        if (l9_c_0011E520(o, q, &as)) return;
        if (l9_c_001B1470(o, as, &wrapped)) return;
        l9_w32(o, self + 0x1FC, wrapped);
        l9_w16(o, self + 0x28, (uint32_t)(rand_scaled(o, 300) + 0x12C));
        l9_w32(o, self + 0x200, 1);
    }
}

/* State 2: bring the phase to -pi/2, then count +0x21C down, fading the
 * companion lane chosen by +0x224. */
static void state2(L9 *o, uint32_t self)
{
    uint32_t phase = l9_u32(o, self + 0x1FC);
    if (L9_EQ(K_MHALFPI, phase)) {
        if (l9_s32(o, self + 0x21C) > 0) {
            uint32_t fade;
            l9_w32(o, self + 0x21C, l9_u32(o, self + 0x21C) - 1u);
            fade = L9_DIV(L9_CVT(l9_u32(o, self + 0x21C)), F_90);
            l9_w32(o, S_70003A20, fade);
            if (l9_u32(o, self + 0x224) != 0) {
                companion(o, self, l9_u32(o, S_70003A20), 0, 0, K_QUARTER);
            } else {
                uint32_t v;
                l9_w32(o, l9_u32(o, self + 0x220) + 0xA0, 0);
                v = l9_u32(o, S_70003A20);
                l9_w32(o, l9_u32(o, self + 0x220) + 0xA4, v);
                l9_w32(o, l9_u32(o, self + 0x220) + 0xA8, 0);
                l9_w32(o, l9_u32(o, self + 0x220) + 0xAC, K_QUARTER);
            }
        }
    } else {
        uint32_t v, step;
        if (L9_LT(phase, K_MHALFPI)) {
            step = l9_u32(o, self + 0x1F8);
            v = L9_ADD(l9_u32(o, self + 0x1FC), step);
            l9_w32(o, self + 0x1FC, v);
            if (!L9_LE(v, K_MHALFPI)) l9_w32(o, self + 0x1FC, K_MHALFPI);
        } else {
            step = l9_u32(o, self + 0x1F8);
            v = L9_SUB(l9_u32(o, self + 0x1FC), step);
            l9_w32(o, self + 0x1FC, v);
            if (L9_LT(v, K_MHALFPI)) l9_w32(o, self + 0x1FC, K_MHALFPI);
        }
        pitch_from_phase(o, self);
        companion_matrix(o, self);
    }
    draw_plain(o, self);
}

/* ------------------------------------------------------------------------
 * 0x824BB0 (self; sp: the original's stack pointer at entry, passed on to
 * 0x826140, whose frame holds two vectors).
 * ---------------------------------------------------------------------- */
int em_level9_port_00824BB0(const EmLevel9PortHooks *h, uint32_t self, uint32_t sp, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    switch (l9_u8(&o, self + 4)) {
    case 0:
        setup(&o, self);
        break;
    case 4:
        if (l9_u8(&o, D_00810702) < 8u) break;
        if (l9_s32(&o, self + 0x208) > 0) countdown(&o, self, 0);
        else state4_idle(&o, self, sp - 0x60);
        break;
    case 1:
        if (l9_u8(&o, D_00810702) < 8u) break;
        if (l9_s32(&o, self + 0x208) > 0) countdown(&o, self, 1);
        else state1_track(&o, self, sp - 0x60);
        break;
    case 2:
        state2(&o, self);
        break;
    default:
        l9_c_001AFC10(&o, self);
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x826140 (self, matrix; sp: the stack pointer at its entry). Stack
 * locals: dir = sp - 0x20, tint = sp - 0x10.
 *  - 0x700038A0 = (60, 0, 0, 0) (stored A0, then dir = (3, -2, 0, 1), then
 *    A8, A4, AC); 001026A0(0x700038A0, matrix, 0x700038A0); 001026A0(dir,
 *    matrix, dir); 001028B8(0x700038A0, 0x700038A0, dir); 38AC = 1.
 *  - hit = 2 when 0019AA80(dir, 0x700038A0, 0x20) is nonzero (then
 *    00102948(0x700038A0, 0x700031B0)), else 0.
 *  - The words 0x700031D8, D4, D0 are read; when 0019A570(dir,
 *    0x700038A0, 7, 0x20) is nonzero: 001028D0(0x700038A0, 0x700031B0,
 *    D_00810360), 38AC = 0, d = 00102738(0x700038A0, 0x700038A0),
 *    0x70003A20 = d; hit = 1 when d > 10000, else 1 when 31D8 == 1 and
 *    byte +3 of the record at 31D4 is 0x10..0x13, else 2. When it is zero
 *    the three words are stored back (D8, D4, D0).
 *  - hit 2: self +0x204 = 31D4 when it is 0 and 31D8 == 1. Then +4 == 4:
 *    the glint (001031E0, the random colour, 001CD520(0, 2, 0x700038A0,
 *    tag, rgba, 3, 3, 2), 001E2BA0(dir, 0x700038A0, 0x700038C0, 100));
 *    otherwise when +0x200 >= 13 the tinted line (tint = 0x82C9D0).
 *  - hit 1: 001031E0(0x700038B0, 0x700031B0), the line to it.
 *  Returns 0 unless hit is 2; then 2 when self +0x204 == 31D4, else 1.
 * ---------------------------------------------------------------------- */
#define GIFTAG_826140 UINT64_C(0x20045BA5154222DC)

int32_t l9_00826140(L9 *o, uint32_t self, uint32_t matrix, uint32_t sp)
{
    uint32_t dir = sp - 0x20, tint = sp - 0x10;
    int32_t r = 0;
    int hit;

    l9_w32(o, S_700038A0, F_60);
    l9_w32(o, dir + 0x0, F_3);
    l9_w32(o, dir + 0x4, K_M2);
    l9_w32(o, dir + 0x8, F_ZERO);
    l9_w32(o, dir + 0xC, F_ONE);
    l9_w32(o, S_700038A8, F_ZERO);
    l9_w32(o, S_700038A4, F_ZERO);
    l9_w32(o, S_700038AC, F_ZERO);
    if (l9_c_001026A0(o, S_700038A0, matrix, S_700038A0)) return 0;
    if (l9_c_001026A0(o, dir, matrix, dir)) return 0;
    if (l9_c_001028B8(o, S_700038A0, S_700038A0, dir)) return 0;
    l9_w32(o, S_700038AC, F_ONE);
    if (l9_c_0019AA80(o, dir, S_700038A0, 0x20, &r)) return 0;
    if (r != 0) {
        if (l9_c_00102948(o, S_700038A0, S_700031B0)) return 0;
        hit = 2;
    } else {
        hit = 0;
    }
    uint32_t save_d8 = l9_u32(o, S_700031D8);
    uint32_t save_d4 = l9_u32(o, S_700031D4);
    uint32_t save_d0 = l9_u32(o, S_700031D0);
    if (l9_c_0019A570(o, dir, S_700038A0, 7, 0x20, &r)) return 0;
    if (r != 0) {
        uint32_t d = 0;
        if (l9_c_001028D0(o, S_700038A0, S_700031B0, D_00810360)) return 0;
        l9_w32(o, S_700038AC, F_ZERO);
        if (l9_c_00102738(o, S_700038A0, S_700038A0, &d)) return 0;
        l9_w32(o, S_70003A20, d);
        if (!L9_LE(d, K_10000)) {
            hit = 1;
        } else if (l9_u32(o, S_700031D8) != 1) {
            hit = 2;
        } else {
            uint32_t kind = l9_u8(o, l9_u32(o, S_700031D4) + 3);
            hit = (kind >= 0x10 && kind < 0x14) ? 1 : 2;
        }
    } else {
        l9_w32(o, S_700031D8, save_d8);
        l9_w32(o, S_700031D4, save_d4);
        l9_w32(o, S_700031D0, save_d0);
    }

    if (hit == 2) {
        if (l9_u32(o, self + 0x204) == 0 && l9_u32(o, S_700031D8) == 1)
            l9_w32(o, self + 0x204, l9_u32(o, S_700031D4));
        if (l9_u8(o, self + 4) == 4) {
            if (l9_c_001031E0(o, S_700038A0, S_700031B0)) return 0;
            l9_w32(o, S_700038AC, F_ONE);
            r = 0;
            if (l9_c_00122BB8(o, &r)) return 0;
            int32_t x = l9_sra((uint32_t)r, 16);
            x = l9_sra(((uint32_t)x << 16) - (uint32_t)x, 15);
            l9_w32(o, S_700038B0, (uint32_t)((l9_sra((uint32_t)x, 15) & 0x1F) + 0x40));
            l9_w32(o, S_700038B4, 0);
            l9_w32(o, S_700038B8, 0);
            l9_w32(o, S_700038BC, 0x80);
            uint32_t bc = l9_u32(o, S_700038BC);
            uint32_t b8 = l9_u32(o, S_700038B8);
            uint32_t b4 = l9_u32(o, S_700038B4);
            uint32_t b0 = l9_u32(o, S_700038B0);
            uint32_t rgba = b0 | (b4 << 8 | (bc << 24 | b8 << 16));
            r = 0;
            if (l9_c_001CD520(o, 0, 2, S_700038A0, GIFTAG_826140, rgba, F_3, F_3, F_2, &r)) return 0;
            l9_w32(o, S_700038C0, K_0_8);
            l9_w32(o, S_700038C8, F_ZERO);
            l9_w32(o, S_700038C4, F_ZERO);
            if (l9_c_001E2BA0(o, dir, S_700038A0, S_700038C0, F_100)) return 0;
        } else if (l9_s32(o, self + 0x200) >= 0xD) {
            uint8_t q[16];
            l9_q(o, 0x0082C9D0u, q);
            l9_wq(o, tint, q);
            if (l9_c_001031E0(o, S_700038A0, S_700031B0)) return 0;
            uint32_t table = l9_u32(o, D_00275B40);
            l9_w32(o, S_700038AC, F_ONE);
            uint32_t record = l9_u32(o, table + 0xC);
            if (l9_c_001026A0(o, S_700038C0, record + 0x90, tint)) return 0;
            l9_w32(o, S_700038CC, F_ONE);
            l9_w32(o, S_700038D0 + 8, K_0_8);
            l9_w32(o, S_700038D0 + 4, K_0_8);
            l9_w32(o, S_700038D0, K_0_8);
            if (l9_c_001E2BA0(o, S_700038A0, S_700038C0, S_700038D0, F_100)) return 0;
        }
    } else if (hit == 1) {
        if (l9_c_001031E0(o, S_700038B0, S_700031B0)) return 0;
        l9_w32(o, S_700038BC, F_ONE);
        l9_w32(o, S_700038C0, K_0_8);
        l9_w32(o, S_700038C8, F_ZERO);
        l9_w32(o, S_700038C4, F_ZERO);
        if (l9_c_001E2BA0(o, dir, S_700038B0, S_700038C0, F_100)) return 0;
    }
    if (hit != 2) return 0;
    uint32_t mine = l9_u32(o, self + 0x204);
    uint32_t other = l9_u32(o, S_700031D4);
    return mine == other ? 2 : 1;
}

int em_level9_port_00826140(const EmLevel9PortHooks *h, uint32_t self, uint32_t matrix, uint32_t sp,
                            int32_t *result, EmLevel9PortFault *fault)
{
    L9 o;
    if (!result || l9_begin(&o, h, fault)) return -1;
    int32_t r = l9_00826140(&o, self, matrix, sp);
    if (l9_failed(&o)) return -1;
    *result = r;
    return 0;
}
