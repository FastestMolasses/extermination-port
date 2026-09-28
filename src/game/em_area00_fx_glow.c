/* AREA00 lane A00FX, the glow effect 0x8000000F: 001F6FB0. See
 * em_area00_fx.h and docs/AREA00_FX.md.
 *
 * The node follows a parent (node +0x24): its phase (work +0) grows by
 * 0.016 a frame, and while it is below 3.5 the node draws 24 random sparks
 * (work +0xC is the LCG) and a sprite through 001CFA60 / 001CFBE0. Past 4.0
 * both the node and the parent end (state 3). */
#include "game/em_area00_fx_internal.h"

#define F_0P016 0x3C83126Fu
#define F_1P5 0x3FC00000u
#define F_1P2 0x3F99999Au
#define F_0P2 0x3E4CCCCDu
#define F_4P8 0x4099999Au
#define F_3P5 0x40600000u
#define F_2 0x40000000u
#define F_4 0x40800000u
#define F_M1 0xBF800000u
#define F_255 0x437F0000u

/* ((w >> 16) & 0xFFFF) / 65535 + 1e-4, with w the LCG word at `lcg`, which
 * becomes w * 37 + 11 (the divide happens after the store). */
static u32 glow_unit(S *s, u32 lcg)
{
    const u32 w = fx_lw(s, lcg);
    const u32 k = fx_fcvt((w >> 16) & 0xFFFFu);
    fx_sw(s, lcg, w * 37u + 11u);
    return k;
}

/* One spark (loop body; returns 0 when the spark is skipped). */
static void glow_spark(S *s, u32 nd, u32 w, u32 sp)
{
    /* f20 = 2 * phase - 2 * unit, folded into [0, 1) by its integer part,
     * or -1.0 when it falls outside [0, 4]. */
    u32 f20;
    {
        const u32 phase = fx_lw(s, w);
        const u32 lcg = fx_lw(s, w + 0xCu);
        const u32 acc = em_ee_mula_bits(F_2, phase);
        u32 k = fx_fcvt((lcg >> 16) & 0xFFFFu);
        k = fx_fdiv(k, FX_F_65535);
        k = fx_fadd(k, FX_F_1EM4);
        f20 = em_ee_msub_bits(acc, F_2, k);
        fx_sw(s, w + 0xCu, lcg * 37u + 11u);
        if (fx_flt(f20, 0) || !fx_fle(f20, F_4)) {
            f20 = F_M1;
        } else {
            EmArea00FxRegs r = {0};
            u64 v0;
            fx_farg(&r, 12, f20);
            fx_call(s, FX_001281C0, &r, &v0, NULL);
            f20 = fx_fsub(f20, fx_fcvt((u32)v0));
        }
    }
    /* the spark's direction */
    u32 ang, spin;
    {
        const u32 q1 = glow_unit(s, w + 0xCu);
        const u32 q1d = fx_fdiv(q1, FX_F_65535);
        const u32 q2 = glow_unit(s, w + 0xCu);
        const u32 q1a = fx_fadd(q1d, FX_F_1EM4);
        const u32 q2d = fx_fdiv(q2, FX_F_65535);
        ang = fx_fsub(fx_fmul(FX_F_2PI, q1a), FX_F_PI);
        const u32 q2a = fx_fadd(q2d, FX_F_1EM4);
        spin = fx_fmul(FX_F_HALF, fx_fmul(FX_F_PI, q2a));
    }
    {
        const u32 c = fx_callf(s, FX_0011DE90, ang);
        fx_sw(s, 0x700038B0u, fx_fmul(fx_lw(s, 0x700038A0u), c));
    }
    {
        const u32 c = fx_callf(s, FX_0011DE90, spin);
        fx_sw(s, 0x700038B4u, fx_fmul(fx_lw(s, 0x700038A4u), c));
    }
    {
        const u32 c = fx_callf(s, FX_0011E2A8, ang);
        fx_sw(s, 0x700038B8u, fx_fmul(fx_lw(s, 0x700038A8u), c));
    }
    fx_sw(s, 0x700038BCu, FX_F_ONE);
    {
        u32 v[4], m[4][4];
        u32 f[4] = {f20, 0, 0, 0}, z[4] = {0, 0, 0, 0};
        fx_ldq(s, 0x700038B0u, v);
        fx_ldm(s, nd + 0xD0u, m);
        fx_vu(s, EM_VU_MULBC, 14, 0, v, f, 0, NULL, v);
        fx_vu(s, EM_VU_MULBC, 8, 0, f, f, 0, NULL, f);
        fx_vu(s, EM_VU_MULBC, 8, 0, z, f, 0, NULL, z);
        fx_transform(s, m, v, v);
        fx_vu(s, EM_VU_ADDBC, 4, 0, v, z, 0, NULL, v);
        fx_stq(s, 0x700038B0u, v);
    }
    if (fx_fle(f20, FX_F_HALF)) {
        fx_00102948(s, 0x700038C0u, 0x700036A0u);
    } else {
        const u32 t = fx_fdiv(fx_fsub(f20, FX_F_HALF), FX_F_HALF);
        u32 a[4], b[4], k[4], v[4];
        fx_sw(s, sp + 0xD0u, t);
        fx_sw(s, sp + 0xD4u, F_255);
        fx_ldq(s, 0x700036A0u, a);
        fx_ldq(s, 0x700036B0u, b);
        fx_ldq(s, sp + 0xD0u, k);
        fx_vu(s, EM_VU_SUB, 15, EM_VU_NO_BC, b, a, 0, NULL, v);
        fx_vu(s, EM_VU_MULBC, 15, 0, v, k, 0, NULL, v);
        fx_vu(s, EM_VU_ADD, 15, EM_VU_NO_BC, v, a, 0, NULL, v);
        for (int i = 0; i < 4; ++i) v[i] = em_vu_max_bits(v[i], 0);
        for (int i = 0; i < 4; ++i) v[i] = em_vu_min_bits(v[i], k[1]);
        fx_stq(s, 0x700038C0u, v);
    }
    if (fx_flt(f20, 0)) return;
    fx_call1(s, FX_001029C0, fx_sx(0x700036E0u));
    fx_call3(s, FX_00102918, fx_sx(0x700036E0u), fx_sx(0x700036E0u), fx_sx(0x700038B0u));
    fx_call2f(s, FX_00102900, fx_sx(0x700036E0u), fx_sx(0x700036E0u), fx_lw(s, 0x700038C0u));
    fx_call2f(s, FX_00102900, fx_sx(0x700036F0u), fx_sx(0x700036F0u), fx_lw(s, 0x700038C4u));
    fx_call2f(s, FX_00102900, fx_sx(0x70003700u), fx_sx(0x70003700u), fx_lw(s, 0x700038C8u));
    u64 h;
    {
        EmArea00FxRegs r = {0};
        fx_arg(&r, R_A0, fx_sx(0x70003710u));
        fx_farg(&r, 12, FX_F_10);
        fx_call(s, FX_001CA7B0, &r, &h, NULL);
    }
    if ((int64_t)h >= 0) {
        fx_call4(s, FX_001C7900, fx_sx(0x700036E0u), fx_sx(nd + 0x80u), 0x3F5, 0);
        const u64 e = fx_call2(s, FX_001C6120, fx_sx(fx_lw(s, FX_D_0028A56C)), 6);
        fx_call2(s, FX_001CA940, h, e);
    }
}

int em_area00_fx_001F6FB0(S *s, u64 node)
{
    FxFrame fr = fx_enter(s, 0x001F6FB0u, 0xE0);
    const u32 sp = s->sp, nd = (u32)node, w = nd + 0x1F0u;
    const u32 parent = fx_lw(s, nd + 0x24u), pw = parent + 0x1F0u;
    const u32 st = fx_lbu(s, nd + 4u);
    if (st == 3 || st == 2) {
        fx_call1(s, FX_001AFC10, node);
        goto out;
    }
    if (st != 0 && st != 1) goto out;
    if (st == 0) {
        fx_00102948(s, nd + 0x60u, parent + 0x60u);
        fx_sw(s, w, 0);
        fx_sw(s, w + 4u, 0);
        fx_sw(s, w + 8u, (u32)fx_call0(s, FX_00122BB8));
        fx_sb(s, nd + 4u, 1);
        fx_sb(s, nd + 5u, 0);
    }
    if (fx_lbu(s, parent + 4u) == 3) {
        fx_sb(s, nd + 4u, 3);
        goto out;
    }
    fx_00102958(s, nd + 0xD0u, parent + 0xD0u);
    fx_00102948(s, nd + 0x100u, parent + 0xB0u);
    fx_sw(s, w + 0xCu, fx_lw(s, w + 8u));
    {
        const u32 sub = fx_lbu(s, nd + 5u);
        if (sub == 2) {
            fx_sw(s, parent + 0x60u, 0);
            fx_sw(s, parent + 0x68u, 0);
            fx_sw(s, parent + 0x64u, 0);
            fx_sb(s, nd + 5u, fx_lbu(s, nd + 5u) + 1u);
        } else if (sub == 1) {
            u32 t, v;
            t = fx_lw(s, w + 4u);
            v = fx_lw(s, nd + 0x60u);
            fx_sw(s, parent + 0x60u, fx_fmul(v, fx_fsub(FX_F_ONE, t)));
            t = fx_lw(s, w + 4u);
            v = fx_lw(s, nd + 0x68u);
            fx_sw(s, parent + 0x68u, fx_fmul(v, fx_fsub(FX_F_ONE, t)));
            t = fx_lw(s, w + 4u);
            v = fx_lw(s, nd + 0x64u);
            fx_sw(s, parent + 0x64u, fx_fmul(v, fx_fsub(FX_F_ONE, t)));
            fx_sw(s, parent + 0x80u, fx_fsub(FX_F_ONE, fx_fmul(FX_F_HALF, fx_lw(s, w + 4u))));
            fx_sw(s, parent + 0x84u, fx_fsub(FX_F_ONE, fx_fmul(FX_F_HALF, fx_lw(s, w + 4u))));
            fx_sw(s, parent + 0x88u, fx_fsub(FX_F_ONE, fx_lw(s, w + 4u)));
            fx_sw(s, nd + 0x80u, fx_lw(s, parent + 0x80u));
            fx_sw(s, nd + 0x84u, fx_lw(s, parent + 0x84u));
            fx_sw(s, nd + 0x88u, fx_lw(s, parent + 0x88u));
            const u32 f0 = fx_fadd(fx_lw(s, w + 4u), F_0P016);
            fx_sw(s, w + 4u, f0);
            if (!fx_fle(f0, FX_F_ONE)) fx_sb(s, nd + 5u, fx_lbu(s, nd + 5u) + 1u);
        } else if (sub == 0) {
            fx_sw(s, w + 4u, fx_fadd(fx_lw(s, w + 4u), F_0P016));
            if ((u32)fx_lh(s, pw + 0xF4u) & 0x1000u) {
                fx_sw(s, w + 4u, 0);
                fx_sb(s, nd + 5u, fx_lbu(s, nd + 5u) + 1u);
            }
        }
    }
    {
        const u32 f0 = fx_fdiv(fx_lw(s, w), F_1P5);
        fx_sw(s, 0x70003A20u, f0);
        const u32 f2 = fx_fle(f0, FX_F_ONE) ? fx_lw(s, 0x70003A20u) : FX_F_ONE;
        fx_sw(s, 0x70003A20u, f2);
        const u32 g = fx_lw(s, 0x70003A20u);
        fx_sw(s, 0x700036ACu, FX_F_ONE);
        fx_sw(s, 0x700036A8u, FX_F_ONE);
        fx_sw(s, 0x700036A4u, FX_F_ONE);
        fx_sw(s, 0x700038A0u, fx_fmul(F_1P2, g));
        fx_sw(s, 0x700038A4u, F_0P2);
        fx_sw(s, 0x700036A0u, FX_F_ONE);
        fx_sw(s, 0x700036BCu, 0);
        fx_sw(s, 0x700036B8u, 0);
        fx_sw(s, 0x700036B4u, 0);
        fx_sw(s, 0x700038A8u, fx_fmul(F_4P8, g));
        fx_sw(s, 0x700038ACu, FX_F_ONE);
        fx_sw(s, 0x700036B0u, 0);
    }
    if (fx_flt(fx_lw(s, w), F_3P5))
        for (int32_t i = 0; i < 0x18 && !fx_latched(s); ++i) glow_spark(s, nd, w, sp);
    if (fx_flt(fx_lw(s, w), F_3P5)) {
        const u64 depth = fx_call1(s, FX_001CCF70, fx_sx(nd + 0x100u));
        if (depth != 0xFFFFFFu) {
            const u32 seed = fx_lw(s, w + 0xCu);
            const u32 x = fx_lw(s, 0x700038A0u);
            const u32 y = fx_lw(s, 0x700038A4u);
            const u32 t = fx_fdiv(fx_fcvt(seed), FX_F_2POW31);
            const u32 z = fx_lw(s, 0x700038A8u);
            fx_sw(s, 0x0025D810u, x);
            fx_sw(s, 0x0025D814u, y);
            fx_sw(s, 0x0025D818u, z);
            EmArea00FxRegs r = {0};
            fx_arg(&r, R_A0, fx_sx(sp + 0x70u));
            fx_arg(&r, R_A1, fx_sx(nd + 0xD0u));
            fx_farg(&r, 12, fx_lw(s, w));
            fx_farg(&r, 13, t);
            fx_call(s, FX_001CFA60, &r, NULL, NULL);
            EmArea00FxRegs b = {0};
            fx_arg(&b, R_A0, depth);
            fx_arg(&b, R_A1, 1);
            fx_arg(&b, R_A2, 0x0025D800u);
            fx_arg(&b, R_A3, fx_sx(sp + 0x70u));
            fx_arg(&b, R_T0, 1);
            fx_call(s, FX_001CFBE0, &b, NULL, NULL);
        }
    }
    {
        const u32 f1 = fx_fadd(fx_lw(s, w), F_0P016);
        fx_sw(s, w, f1);
        if (!fx_fle(f1, F_4)) {
            fx_sb(s, parent + 4u, 3);
            fx_sb(s, nd + 4u, 3);
        }
    }
out:
    fx_leave(s, fr);
    return fx_result(s);
}
