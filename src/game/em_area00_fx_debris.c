/* AREA00 lane A00FX, the debris effects: 001F2BA0, 001F2E90, 001F2F90,
 * 001F3340, 001F3620, 001F3E30. See em_area00_fx.h and docs/AREA00_FX.md. */
#include "game/em_area00_fx_internal.h"

#define D_0025A350 0x0025A350u /* the debris rows (0x60 bytes per subtype) */
#define D_0025A398 0x0025A398u /* the per-kind gravity (stride 0x60) */
#define F_2 0x40000000u
#define F_3P5 0x40600000u
#define F_7 0x40E00000u

static u32 debris_row(u32 sub) { return D_0025A350 + ((sub << 1) + sub) * 32u; }

/* ---- 001F2BA0 ------------------------------------------------------------ */

int em_area00_fx_001F2BA0(S *s, u64 node)
{
    FxFrame fr = fx_enter(s, 0x001F2BA0u, 0x60);
    const u32 nd = (u32)node;
    const u32 mode = fx_lbu(s, nd + 4u);
    if (mode == 3 || mode == 2) {
        fx_call1(s, FX_001AFC10, node);
    } else if (mode == 0) {
        const u32 sub = fx_lbu(s, nd + 0xDu);
        u32 add = 0;
        if (sub == 4) add = F_2;
        else if (sub == 6) add = F_3P5;
        else if (sub == 5) add = F_7;
        else if (sub == 1) add = F_3P5;
        else if (sub == 0) add = F_7;
        if (add) fx_sw(s, nd + 0xB4u, fx_fadd(fx_lw(s, nd + 0xB4u), add));
        fx_call1(s, FX_001029C0, fx_sx(nd + 0xD0u));
        fx_call3(s, FX_00102C58, fx_sx(nd + 0xD0u), fx_sx(nd + 0xD0u), fx_sx(nd + 0xC0u));
        fx_call3(s, FX_00102918, fx_sx(nd + 0xD0u), fx_sx(nd + 0xD0u), fx_sx(nd + 0xB0u));
        em_area00_fx_001F2E90(s, node, fx_sx(debris_row(fx_lbu(s, nd + 0xDu))), NULL);
    } else if (mode == 1) {
        int32_t miss = (int32_t)fx_lbu(s, nd + 0xCu), i = 0;
        u32 k = 0;
        while (i < (int32_t)fx_lbu(s, nd + 0xCu) && !fx_latched(s)) {
            const u32 ent = fx_lw(s, fx_lw(s, FX_D_00275B40) + k);
            if (fx_lh(s, ent + 0x80u) != 0) {
                miss = (int32_t)((u32)miss - 1u);
            } else {
                em_area00_fx_001F3620(s, fx_sx(ent), fx_lbu(s, nd + 0xDu));
                if (fx_lh(s, ent + 0x80u) == 0) {
                    const u32 row = debris_row(fx_lbu(s, nd + 0xDu));
                    int32_t idx = i & 1;
                    if (i < 0 && idx != 0) idx -= 2;
                    const u32 a2 = fx_lw(s, row + 0x50u);
                    const u32 a3 = fx_lw(s, row + (u32)idx * 4u + 0x54u);
                    const u32 t0 = fx_lw(s, row + 0x5Cu);
                    em_area00_fx_001F3E30(s, fx_sx(0x700036A0u), fx_sx(ent + 0x40u), fx_sx(a2), fx_sx(a3), fx_sx(t0));
                }
            }
            k += 4u;
            i += 1;
        }
        if (miss == 0) fx_sb(s, nd + 4u, 3);
        int32_t j = 0, count = 0;
        k = 0;
        while (j < (int32_t)fx_lbu(s, nd + 0xCu) && !fx_latched(s)) {
            const u32 ent = fx_lw(s, fx_lw(s, FX_D_00275B40) + k);
            if (fx_lw(s, ent + 0x78u) == 0) {
                const u32 sub = fx_lbu(s, nd + 0xDu);
                em_area00_fx_001F3340(s, fx_sx(ent), fx_sx(debris_row(sub)), sub);
                if (!(++count < 4)) break;
            }
            k += 4u;
            j += 1;
        }
    }
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F2E90 ------------------------------------------------------------ */

int em_area00_fx_001F2E90(S *s, u64 node, u64 row, u64 *v0)
{
    FxFrame fr = fx_enter(s, 0x001F2E90u, 0x50);
    const u32 nd = (u32)node;
    u64 result;
    fx_sb(s, nd + 0xCu, fx_lbu(s, (u32)row + 0x4Cu));
    const u32 want = fx_lbu(s, nd + 0xCu);
    if (fx_lh(s, FX_D_00275BCC) < (int32_t)want) {
        fx_sb(s, nd + 4u, 3);
        result = 0;
    } else {
        u32 p = nd;
        for (int32_t i = 0; i < (int32_t)fx_lbu(s, nd + 0xCu) && !fx_latched(s); ++i, p += 4u)
            fx_sw(s, p + 0x110u, (u32)fx_call0(s, FX_001AF780));
        fx_call0(s, FX_001CB5B0);
        fx_sb(s, nd + 9u, fx_lbu(s, nd + 0xCu));
        fx_call1(s, FX_001C6200, node);
        u32 k = 0;
        for (int32_t i = 0; i < (int32_t)fx_lbu(s, nd + 0xCu) && !fx_latched(s); ++i, k += 4u) {
            const u32 tbl = fx_lw(s, FX_D_00275B40);
            const u32 kind = fx_lbu(s, nd + 0xDu);
            const u32 ent = fx_lw(s, tbl + k);
            em_area00_fx_001F2F90(s, fx_sx(nd + 0xD0u), fx_sx(ent), row, kind);
        }
        fx_sb(s, nd + 4u, 1);
        result = 1;
    }
    if (v0) *v0 = result;
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F2F90 ------------------------------------------------------------ */

#define F_0P04 0x3D23D70Au

/* cvt(rand) / 2^31 */
static u32 fx_rand_unit(S *s) { return fx_fdiv(fx_fcvt((u32)fx_call0(s, FX_00122BB8)), FX_F_2POW31); }

int em_area00_fx_001F2F90(S *s, u64 frame, u64 piece, u64 row, u64 kind)
{
    FxFrame fr = fx_enter(s, 0x001F2F90u, 0x60);
    const u32 f = (u32)frame, p = (u32)piece, t = (u32)row;
    const u32 ang = fx_fsub(fx_fmul(FX_F_2PI, fx_rand_unit(s)), FX_F_PI);
    const u32 spin = fx_fmul(FX_F_HALF, fx_fmul(FX_F_PI, fx_rand_unit(s)));
    if (kind == 9 || kind == 8 || kind == 7 || kind == 3) {
        fx_00102948(s, p, f + 0x30u);
        fx_sw(s, p + 0xCu, FX_F_ONE);
        fx_call3(s, FX_001026A0, fx_sx(p + 0x10u), frame, fx_sx(t + 0x10u));
        fx_call3(s, FX_001028D0, fx_sx(p + 0x10u), fx_sx(p + 0x10u), fx_sx(f + 0x30u));
        {
            const u32 r = fx_callf(s, FX_0011E2A8, ang);
            fx_sw(s, p + 0x10u, fx_fadd(fx_lw(s, p + 0x10u), fx_fmul(F_0P04, r)));
        }
        {
            const u32 r = fx_callf(s, FX_0011DE90, ang);
            fx_sw(s, p + 0x18u, fx_fadd(fx_lw(s, p + 0x18u), fx_fmul(F_0P04, r)));
        }
        fx_call1(s, FX_001029C0, fx_sx(0x70003400u));
        fx_call2f(s, FX_00102BB0, fx_sx(0x70003400u), fx_sx(0x70003400u), FX_F_HALFPI);
        fx_call3(s, FX_001026D0, fx_sx(0x70003400u), frame, fx_sx(0x70003400u));
        fx_call2(s, FX_001C9E40, fx_sx(p + 0x20u), fx_sx(0x70003400u));
    } else {
        u32 r, a, b;
        r = fx_callf(s, FX_0011DE90, ang);
        a = fx_lw(s, t);
        b = fx_lw(s, f + 0x30u);
        fx_sw(s, p, fx_fadd(b, fx_fmul(a, r)));
        r = fx_callf(s, FX_0011DE90, spin);
        a = fx_lw(s, t + 4u);
        b = fx_lw(s, f + 0x34u);
        fx_sw(s, p + 4u, fx_fadd(b, fx_fmul(a, r)));
        r = fx_callf(s, FX_0011E2A8, ang);
        a = fx_lw(s, t + 8u);
        b = fx_lw(s, f + 0x38u);
        fx_sw(s, p + 8u, fx_fadd(b, fx_fmul(a, r)));
        fx_sw(s, p + 0xCu, FX_F_ONE);
        for (u32 i = 0; i < 3; ++i) {
            const u32 q = fx_rand_unit(s);
            const u32 scale = fx_lw(s, t + 0x10u + 4u * i), bias = fx_lw(s, t + 0x20u + 4u * i);
            fx_sw(s, p + 0x10u + 4u * i, fx_fsub(fx_fmul(scale, q), bias));
        }
        fx_sw(s, p + 0x1Cu, FX_F_ONE);
        fx_call3(s, FX_001026A0, fx_sx(p + 0x10u), frame, fx_sx(p + 0x10u));
        fx_call3(s, FX_001028D0, fx_sx(p + 0x10u), fx_sx(p + 0x10u), fx_sx(f + 0x30u));
        const u32 deg = fx_fdiv(fx_fmul(FX_F_PI, fx_fmul(FX_F_360, fx_rand_unit(s))), FX_F_180);
        EmArea00FxRegs rr = {0};
        fx_arg(&rr, R_A0, fx_sx(p + 0x20u));
        fx_farg(&rr, 12, deg);
        fx_farg(&rr, 13, deg);
        fx_farg(&rr, 14, deg);
        fx_call(s, FX_001CA3B0, &rr, NULL, NULL);
    }
    fx_00102948(s, p + 0x40u, t + 0x30u);
    {
        const u32 q = fx_rand_unit(s);
        const u32 spread = fx_lw(s, t + 0x44u), base = fx_lw(s, t + 0x40u);
        const u32 size = fx_fadd(base, fx_fmul(spread, q));
        fx_sw(s, p + 0x30u, size);
        fx_sw(s, p + 0x34u, size);
        fx_sw(s, p + 0x38u, size);
    }
    fx_sw(s, p + 0x3Cu, FX_F_ONE);
    fx_sw(s, p + 0x78u, 0);
    fx_sw(s, p + 0x70u, 0);
    fx_sh(s, p + 0x80u, 0);
    fx_sw(s, p + 0x7Cu, 0);
    fx_sw(s, p + 0x74u, 0x2710u);
    fx_sh(s, p + 0x82u, (u32)kind);
    fx_sw(s, p + 0x84u, FX_F_ONE);
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F3340 ------------------------------------------------------------ */

int em_area00_fx_001F3340(S *s, u64 piece, u64 row, u64 kind)
{
    FxFrame fr = fx_enter(s, 0x001F3340u, 0x60);
    const u32 p = (u32)piece;
    int32_t step = 0;
    if (kind == 0x16 || kind == 0x12 || kind == 0x11) {
        fx_sw(s, p + 0x74u, 0xB4u);
        fx_sw(s, p + 0x78u, 1);
    } else {
        if (kind == 9 || kind == 8 || kind == 7 || kind == 3) step = 4;
        else if (kind == 0x17 || kind == 0xA || kind == 2) step = 6;
        else if (kind == 4 || kind == 6 || kind == 5 || kind == 1 || kind == 0) step = 0xC;
        if (step == 0) {
            fx_sw(s, p + 0x74u, 1);
            fx_sw(s, p + 0x78u, 1);
        } else {
            const int32_t n = 0x78 / step;
            fx_sw(s, p + 0x74u, 0);
            fx_00102948(s, 0x700038A0u, p);
            fx_00102948(s, 0x700038B0u, p + 0x10u);
            int32_t i = 0;
            if (0 < n) {
                do {
                    const u32 b4 = fx_lw(s, 0x700038B4u);
                    const u32 t = fx_fcvt((u32)step);
                    u32 f3 = fx_lw(s, (u32)row + 0x48u);
                    const u32 f4 = fx_lw(s, 0x700038A0u);
                    f3 = fx_fmul(f3, t);
                    const u32 f2 = fx_lw(s, 0x700038A4u);
                    const u32 f1 = fx_fadd(b4, f3);
                    const u32 f0 = fx_lw(s, 0x700038A8u);
                    fx_sw(s, 0x700038B4u, f1);
                    const u32 f5 = fx_fmul(fx_lw(s, 0x700038B0u), t);
                    const u32 g3 = fx_fmul(fx_lw(s, 0x700038B4u), t);
                    const u32 g1 = fx_fmul(fx_lw(s, 0x700038B8u), t);
                    fx_sw(s, 0x700038C0u, fx_fadd(f4, f5));
                    fx_sw(s, 0x700038C4u, fx_fadd(f2, g3));
                    fx_sw(s, 0x700038C8u, fx_fadd(f0, g1));
                    EmArea00FxRegs r = {0};
                    u64 hit;
                    fx_arg(&r, R_A0, fx_sx(0x700038A0u));
                    fx_arg(&r, R_A1, fx_sx(0x700038C0u));
                    fx_arg(&r, R_A2, 4);
                    fx_arg(&r, R_A3, 0);
                    fx_arg(&r, R_S4, piece);
                    fx_call(s, FX_0019A570, &r, &hit, NULL);
                    if (hit != 0) {
                        fx_00102948(s, p + 0x60u, 0x700031B0u);
                        fx_sw(s, p + 0x50u, fx_lw(s, fx_lw(s, 0x700031D0u) + 0x24u));
                        fx_sw(s, p + 0x54u, fx_lw(s, fx_lw(s, 0x700031D0u) + 0x28u));
                        fx_sw(s, p + 0x58u, fx_lw(s, fx_lw(s, 0x700031D0u) + 0x2Cu));
                        break;
                    }
                    fx_call2(s, FX_001031E0, fx_sx(0x700038A0u), fx_sx(0x700038C0u));
                    const u32 was = fx_lw(s, p + 0x74u);
                    i += 1;
                    fx_sw(s, p + 0x74u, was + (u32)step);
                } while (i < n && !fx_latched(s));
            }
            if (i == n) {
                fx_sw(s, p + 0x7Cu, 1);
                fx_sw(s, p + 0x74u, (u32)(step * n));
            } else {
                fx_sw(s, p + 0x7Cu, 0);
                fx_sw(s, p + 0x74u, fx_lw(s, p + 0x74u) + (u32)step);
                if (!((int32_t)fx_lw(s, p + 0x74u) > 0)) fx_sw(s, p + 0x74u, 1);
            }
            fx_sw(s, p + 0x78u, 1);
        }
    }
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F3620 ------------------------------------------------------------ */

#define F_4 0x40800000u
#define F_8 0x41000000u
#define F_16 0x41800000u
#define F_0P05 0x3D4CCCCDu
#define F_0P6 0x3F19999Au
#define F_0P02 0x3CA3D70Au
#define F_0P1 0x3DCCCCCDu
#define F_0P4 0x3ECCCCCDu
#define F_0P01 0x3C23D70Au
#define F_M0P5 0xBF000000u
#define F_200 0x43480000u
#define F_800 0x44480000u

/* (pi * (deg * rand / 2^31)) / 180, stored at 0x70003A20, as the angle of
 * a 001CA3B0 rotation (all three axes) composed into the piece's +0x20
 * quaternion by 001CA4D0. */
static void f3620_wobble(S *s, u32 p, u32 deg)
{
    const u32 a = fx_fdiv(fx_fmul(FX_F_PI, fx_fmul(deg, fx_rand_unit(s))), FX_F_180);
    fx_sw(s, 0x70003A20u, a);
    EmArea00FxRegs r = {0};
    fx_arg(&r, R_A0, fx_sx(0x700038A0u));
    fx_farg(&r, 12, a);
    fx_farg(&r, 13, a);
    fx_farg(&r, 14, a);
    fx_call(s, FX_001CA3B0, &r, NULL, NULL);
    fx_call3(s, FX_001CA4D0, fx_sx(p + 0x20u), fx_sx(p + 0x20u), fx_sx(0x700038A0u));
}

/* 00102900(m, m, +off * +0x84) on one matrix row (the two words loaded in
 * that order). */
static void f3620_scale_row(S *s, u32 p, u32 row, u32 off)
{
    const u32 k = fx_lw(s, p + off);
    const u32 fade = fx_lw(s, p + 0x84u);
    fx_call2f(s, FX_00102900, fx_sx(row), fx_sx(row), fx_fmul(k, fade));
}

static void f3620_scale3(S *s, u32 p)
{
    f3620_scale_row(s, p, 0x700036A0u, 0x30u);
    f3620_scale_row(s, p, 0x700036B0u, 0x34u);
}

int em_area00_fx_001F3620(S *s, u64 piece, u64 kind)
{
    FxFrame fr = fx_enter(s, 0x001F3620u, 0x40);
    const u32 p = (u32)piece;
    int ret = 0;
    const u32 state = fx_lw(s, p + 0x70u);
    if (state == 3) {
        f3620_wobble(s, p, F_16);
        if ((int32_t)fx_lw(s, p + 0x74u) < 0x14) fx_sw(s, p + 0x84u, fx_fsub(fx_lw(s, p + 0x84u), F_0P05));
        fx_sw(s, p + 0x74u, fx_lw(s, p + 0x74u) - 1u);
        if (!((int32_t)fx_lw(s, p + 0x74u) > 0)) fx_sh(s, p + 0x80u, 1);
        ret = 1;
    } else if (state == 2) {
        fx_call2(s, FX_001CD390, fx_sx(0x700036A0u), fx_sx(p + 0x50u));
        fx_call2(s, FX_001031E0, fx_sx(0x700036D0u), piece);
        f3620_scale3(s, p);
        fx_call2f(s, FX_00102900, fx_sx(0x700036C0u), fx_sx(0x700036C0u), fx_lw(s, p + 0x38u));
        const u32 f1 = fx_fsub(fx_lw(s, p + 0x84u), F_0P01);
        fx_sw(s, p + 0x84u, f1);
        if (fx_flt(f1, 0)) fx_sh(s, p + 0x80u, 1);
    } else if (state == 1) {
        if (fx_lw(s, p + 0x7Cu) != 0) {
            fx_sh(s, p + 0x80u, 1);
            ret = 1;
        } else if (kind == 9 || kind == 8 || kind == 7 || kind == 3) {
            fx_sw(s, p + 0x10u, F_0P1);
            fx_sw(s, p + 0x18u, 0);
            fx_sw(s, p + 0x1Cu, FX_F_ONE);
            fx_00102948(s, p, p + 0x60u);
            fx_call1(s, FX_001029C0, fx_sx(0x700036A0u));
            const u32 a = fx_fdiv(fx_fmul(FX_F_PI, fx_fmul(FX_F_360, fx_rand_unit(s))), FX_F_180);
            fx_call2f(s, FX_00102BB0, fx_sx(0x700036A0u), fx_sx(0x700036A0u), a);
            fx_call3(s, FX_001026A0, fx_sx(p + 0x10u), fx_sx(0x700036A0u), fx_sx(p + 0x10u));
            u32 f1 = em_ee_neg_bits(fx_fmul(F_0P4, fx_lw(s, p + 0x14u)));
            fx_sw(s, p + 0x14u, f1);
            if (!fx_fle(f1, FX_F_ONE)) f1 = FX_F_ONE;
            fx_sw(s, p + 0x14u, f1);
            fx_sw(s, p + 0x74u, 0x2D);
            fx_sw(s, p + 0x70u, 3);
            ret = 1;
            if ((int32_t)fx_lw(s, FX_D_00275C44) < 0) {
                const int32_t r = (int32_t)(u32)fx_call0(s, FX_00122BB8);
                const u32 mute = fx_lbu(s, 0x70003B8Du);
                fx_sw(s, FX_D_00275C44, (u32)(r % 20) + 0x1Eu);
                if (mute == 0) {
                    u32 id = 0;
                    if (kind == 9) id = 0x5DE;
                    else if (kind == 8) id = 0x5E0;
                    else if (kind == 7) id = 0x5E0;
                    else if (kind == 3) id = 0x16A;
                    if (id) em_area00_fx_001F02C0(s, piece, id, F_200);
                }
            }
        } else if (kind == 0x17 || kind == 0xA || kind == 2) {
            fx_00102948(s, p, p + 0x60u);
            fx_sw(s, p + 0x40u, F_0P6);
            fx_sw(s, p + 0x44u, F_0P6);
            fx_sw(s, p + 0x48u, F_0P6);
            fx_sw(s, p + 0x4Cu, F_0P6);
            const u32 q = fx_rand_unit(s);
            const u32 spin = fx_fmul(fx_lw(s, p + 0x30u), fx_fadd(F_2, q));
            fx_sw(s, p + 0x30u, spin);
            fx_sw(s, p + 0x34u, fx_fmul(F_2, spin));
            fx_sw(s, p + 0x38u, F_0P02);
            fx_sw(s, p + 0x70u, 2);
            em_area00_fx_001F3620(s, piece, kind);
        } else {
            fx_sh(s, p + 0x80u, 1);
        }
    } else if (state == 0) {
        if (kind == 7) {
            EmArea00FxRegs r = {0};
            fx_arg(&r, R_A0, fx_sx(0x700038A0u));
            fx_farg(&r, 12, 0);
            fx_farg(&r, 13, F_M0P5);
            fx_farg(&r, 14, 0);
            fx_call(s, FX_001CA3B0, &r, NULL, NULL);
            fx_call3(s, FX_001CA4D0, fx_sx(p + 0x20u), fx_sx(p + 0x20u), fx_sx(0x700038A0u));
        } else if (kind == 0x16 || kind == 0x12 || kind == 0x11) {
            f3620_wobble(s, p, F_4);
            if (fx_flt(fx_lw(s, p + 4u), FX_F_10)) {
                fx_sw(s, 0x700038D0u, fx_lw(s, p));
                fx_sw(s, 0x700038D4u, 0);
                fx_sw(s, 0x700038D8u, fx_lw(s, p + 8u));
                fx_sw(s, 0x700038DCu, FX_F_ONE);
                fx_call2(s, FX_001EFD20, fx_sx(0x8000005Fu), fx_sx(0x700038D0u));
                fx_sh(s, p + 0x80u, 1);
                fx_sw(s, FX_D_00275C48, fx_lw(s, FX_D_00275C48) + 1u);
                if (fx_lw(s, FX_D_00275C48) & 1u) em_area00_fx_001F02C0(s, fx_sx(0x700038D0u), 0xDB, F_800);
            }
        } else if (kind == 4 || kind == 6 || kind == 5 || kind == 1 || kind == 0) {
            f3620_wobble(s, p, F_8);
            if ((int32_t)fx_lw(s, p + 0x74u) < 0x14) fx_sw(s, p + 0x84u, fx_fsub(fx_lw(s, p + 0x84u), F_0P05));
        }
        fx_sw(s, p + 0x74u, fx_lw(s, p + 0x74u) - 1u);
        if (!((int32_t)fx_lw(s, p + 0x74u) > 0)) fx_sw(s, p + 0x70u, 1);
        ret = 1;
    }
    if (ret) {
        fx_call3(s, FX_001CA1C0, fx_sx(0x700036A0u), fx_sx(p + 0x20u), piece);
        f3620_scale3(s, p);
        f3620_scale_row(s, p, 0x700036C0u, 0x38u);
        fx_call3(s, FX_001028B8, piece, piece, fx_sx(p + 0x10u));
        const u32 vy = fx_lw(s, p + 0x14u);
        const u32 g = fx_lw(s, D_0025A398 + (((u32)kind << 1) + (u32)kind) * 32u);
        fx_sw(s, p + 0x14u, fx_fadd(vy, g));
    }
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F3E30 ------------------------------------------------------------ */

int em_area00_fx_001F3E30(S *s, u64 a0, u64 a1, u64 a2, u64 a3, u64 mode)
{
    FxFrame fr = fx_enter(s, 0x001F3E30u, 0x70);
    u64 h;
    {
        EmArea00FxRegs r = {0};
        fx_arg(&r, R_A0, fx_sx((u32)a0 + 0x30u));
        fx_farg(&r, 12, FX_F_10);
        fx_call(s, FX_001CA7B0, &r, &h, NULL);
    }
    if ((int64_t)h >= 0) {
        if (mode == 2) {
            const u64 depth = fx_call1(s, FX_001CCF70, fx_sx((u32)a0 + 0x30u));
            const u32 cur = fx_lw(s, fx_lw(s, FX_D_00275670) + 0x1Cu);
            fx_call3(s, FX_001CB760, FX_D_007635C0, depth, fx_sx(cur));
            fx_call1(s, FX_001D8C20, 1);
            fx_call4(s, FX_001C7900, a0, a1, 0x3F5, 3);
            const u32 table = fx_lw(s, 0x0028A490u + ((u32)a2 << 2));
            const u64 e = fx_call2(s, FX_001C6120, fx_sx(table), a3);
            fx_call1(s, FX_001D3990, e);
            const u32 ctx = fx_lw(s, FX_D_00275670);
            fx_sb(s, fx_lw(s, ctx + 0x1Cu) + 3u, 0x60);
            fx_sw(s, fx_lw(s, ctx + 0x1Cu) + 4u, 0);
            fx_sh(s, fx_lw(s, ctx + 0x1Cu), 0);
            fx_sw(s, ctx + 0x1Cu, fx_lw(s, ctx + 0x1Cu) + 0x10u);
            fx_call1(s, FX_001D8C20, 0);
        } else if (mode == 0) {
            fx_call4(s, FX_001C7900, a0, a1, 0x3F5, 0);
            const u32 table = fx_lw(s, 0x0028A490u + ((u32)a2 << 2));
            const u64 e = fx_call2(s, FX_001C6120, fx_sx(table), a3);
            fx_call2(s, FX_001CA940, h, e);
        }
    }
    fx_leave(s, fr);
    return fx_result(s);
}
