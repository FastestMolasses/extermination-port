/* AREA00 lane A00FX, packets: 001CD940, 001D6DD0, 001D7510, 001D7A80,
 * 001F4A00, 001F4E20. See em_area00_fx.h and docs/AREA00_FX.md. */
#include "game/em_area00_fx_internal.h"

/* ---- 001CD940 ------------------------------------------------------------ */

/* One end of the line: clip = K * (p.xyz, 1); Q = 1 / clip.w; x, y *= Q;
 * w -= 1.0; Q = 1 / w; z *= Q; the fog lane from w; every lane to 12.4.
 * The clip-space vector is spilled to the stack slot, as the original does. */
static void cd940_project(S *s, const u32 k[4][4], const u32 ca0[4], u32 p, u32 spill, u32 out)
{
    static const u32 one_x[4] = {FX_F_ONE, 0, 0, 0};
    u32 v[4], q;
    fx_ldq(s, p, v);
    fx_transform(s, k, v, v);
    q = fx_vrcp(s, v[3]);
    fx_stq(s, spill, v);
    fx_vu(s, EM_VU_MULQ, 12, EM_VU_NO_BC, v, NULL, q, NULL, v);
    fx_vu(s, EM_VU_SUBBC, 1, 0, v, one_x, 0, NULL, v);
    q = fx_vrcp(s, v[3]);
    fx_vu(s, EM_VU_MULQ, 2, EM_VU_NO_BC, v, NULL, q, NULL, v);
    fx_fog(s, ca0, v);
    fx_ftoi4(v);
    fx_stq(s, out, v);
}

/* The low word of the signed product, shifted right by 8 (arithmetic). */
static u32 cd940_scale(u32 a, u32 b)
{
    const u32 lo = (u32)((int64_t)(int32_t)a * (int64_t)(int32_t)b);
    return (u32)((int32_t)lo >> 8);
}

/* The fog word w (0x7000360C or 0x7000361C) and its colour c (0x70003620
 * or 0x70003630), for a nonzero mode. */
static void cd940_fade(S *s, u64 mode, u32 w, u32 c)
{
    fx_sw(s, w, (u32)((int32_t)fx_lw(s, w) >> 4));
    if (!((int32_t)fx_lw(s, w) < 0x100)) fx_sw(s, w, 0xFFu);
    if ((int32_t)fx_lw(s, w) < 0) fx_sw(s, w, 0);
    if (mode == 1) {
        const u32 a = fx_lw(s, c + 0xC);
        fx_sw(s, c + 0xC, cd940_scale(a, fx_lw(s, w)));
    } else if (mode == 3 || mode == 2) {
        const u32 r = fx_lw(s, c), f = fx_lw(s, w);
        fx_sw(s, c, cd940_scale(r, f));
        fx_sw(s, c + 4, cd940_scale(fx_lw(s, c + 4), f));
        fx_sw(s, c + 8, cd940_scale(fx_lw(s, c + 8), f));
    }
    fx_sw(s, w, 0xFF0u);
}

int em_area00_fx_001CD940(S *s, u64 mode, u64 p0, u64 c0, u64 p1, u64 c1)
{
    FxFrame fr = fx_enter(s, 0x001CD940u, 0x80);
    const u32 sp = s->sp;
    u32 k[4][4], v[4];
    const u32 clip = (u32)fx_call1(s, FX_001CD370, 0);
    fx_ldm(s, clip, k);
    fx_ldq(s, (u32)p0, v);
    fx_transform(s, k, v, v);
    if (fx_clipw(s, v) & 0x3Fu) goto out;
    fx_ldq(s, (u32)p1, v);
    fx_transform(s, k, v, v);
    if (fx_clipw(s, v) & 0x3Fu) goto out;
    {
        u32 ca0[4], m[4][4], q[4];
        fx_ldq(s, fx_lw(s, FX_D_00275670) + 0xA0u, ca0);
        fx_ldm(s, 0x70003AC0u, m);
        cd940_project(s, m, ca0, (u32)p0, sp + 0x60u, 0x70003600u);
        cd940_project(s, m, ca0, (u32)p1, sp + 0x70u, 0x70003610u);
        fx_ldq(s, (u32)c0, q);
        fx_stq(s, 0x70003620u, q);
        fx_ldq(s, (u32)c1, q);
        fx_stq(s, 0x70003630u, q);
        if (mode != 0) cd940_fade(s, mode, 0x7000360Cu, 0x70003620u);
        if (mode != 0) cd940_fade(s, mode, 0x7000361Cu, 0x70003630u);
        const u32 blk = (u32)fx_call3(s, FX_001CB5F0, FX_D_007635C0, fx_sx(fx_lw(s, 0x70003608u)), 4);
        fx_00102948(s, blk, 0x70003620u);
        fx_00102948(s, blk + 0x10u, 0x70003600u);
        fx_00102948(s, blk + 0x20u, 0x70003630u);
        fx_00102948(s, blk + 0x30u, 0x70003610u);
        fx_call4(s, FX_001CB6B0, FX_D_007635C0, fx_sx(fx_lw(s, 0x70003608u)), 2, 0x00251240u);
        fx_call3(s, FX_001CB900, FX_D_007635C0, fx_sx(fx_lw(s, 0x70003608u)), mode);
    }
out:
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- the channel tag ---------------------------------------------------- */

/* One DMA tag on the channel cursor at `slot` (context + ch * 4 + 0x10):
 * byte +3 = 0x10, word +4 = 0, halfword +0 = qwc, each after its own reload
 * of the cursor; returns the cursor loaded last, which becomes cursor + size. */
static u32 fx_channel_tag(S *s, u32 slot, u32 qwc, u32 size)
{
    fx_sb(s, fx_lw(s, slot) + 3u, 0x10u);
    fx_sw(s, fx_lw(s, slot) + 4u, 0);
    fx_sh(s, fx_lw(s, slot), qwc);
    const u32 cur = fx_lw(s, slot);
    fx_sw(s, slot, cur + size);
    return cur;
}

/* ---- 001D6DD0 ------------------------------------------------------------ */

int em_area00_fx_001D6DD0(S *s, u64 ch, u64 a1, u64 a2, u64 *v0)
{
    FxFrame fr = fx_enter(s, 0x001D6DD0u, 0);
    const u64 data = fx_sx((u32)a1) | ((u64)(u32)a2 << 32);
    const u32 slot = fx_lw(s, FX_D_00275670) + ((u32)ch << 2) + 0x10u;
    const u32 t = fx_channel_tag(s, slot, 3, 0x40);
    fx_stq0(s, t + 0x10u);
    fx_sw(s, t + 0x1Cu, 0x50000002u);
    fx_sd(s, t + 0x20u, UINT64_C(0x1000000000008001));
    fx_sd(s, t + 0x28u, 0xE);
    fx_sd(s, t + 0x30u, data);
    fx_sd(s, t + 0x38u, 0x18);
    if (v0) *v0 = fx_sx(t + 0x10u);
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001D7510 ------------------------------------------------------------ */

int em_area00_fx_001D7510(S *s, u64 ch, u64 a1, u64 a2)
{
    FxFrame fr = fx_enter(s, 0x001D7510u, 0);
    const u64 x2 = fx_sx((u32)a2) << 34, x1 = fx_sx((u32)a1) << 35;
    const u32 ctx = fx_lw(s, FX_D_00275670);
    const u32 flag = fx_lw(s, ctx + 0x9Cu);
    const u32 slot = ctx + ((u32)ch << 2) + 0x10u;
    const u64 on = fx_sx((u32)((int32_t)(flag != 0 ? 0x70000 : 0) >> 8));
    const u32 t = fx_channel_tag(s, slot, 4, 0x50);
    fx_stq0(s, t + 0x10u);
    fx_sw(s, t + 0x1Cu, 0x50000003u);
    fx_sd(s, t + 0x20u, UINT64_C(0x1000000000008002));
    fx_sd(s, t + 0x28u, 0xE);
    fx_sd(s, t + 0x30u, 0);
    fx_sd(s, t + 0x38u, 0x3F);
    fx_sd(s, t + 0x40u, x1 | on | UINT64_C(0x0000000224020000) | x2);
    fx_sd(s, t + 0x48u, 6);
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001D7A80 ------------------------------------------------------------ */

int em_area00_fx_001D7A80(S *s, u64 ch, u64 a1, u64 a2, u64 a3, u64 t0, u64 *v0)
{
    FxFrame fr = fx_enter(s, 0x001D7A80u, 0);
    const u32 ctx = fx_lw(s, FX_D_00275670);
    const u64 tag = fx_sx((u32)(a1 | 0x116u)) << 47;
    const u32 slot = ctx + ((u32)ch << 2) + 0x10u;
    const u32 t = fx_channel_tag(s, slot, 7, 0x80);
    fx_stq0(s, t + 0x10u);
    fx_sw(s, t + 0x1Cu, 0x50000006u);
    fx_sd(s, t + 0x20u, tag | UINT64_C(0x5000400000008001));
    fx_sd(s, t + 0x28u, 0x43431);
    fx_00102948(s, t + 0x30u, (u32)t0);
    fx_00102948(s, t + 0x40u, (u32)a3);
    fx_00102948(s, t + 0x50u, (u32)a2);
    fx_00102948(s, t + 0x60u, (u32)a3 + 0x10u);
    fx_00102948(s, t + 0x70u, (u32)a2 + 0x10u);
    if (v0) *v0 = fx_sx(t + 0x10u);
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F4A00 / 001F4E20: tail calls -------------------------------------- */

int em_area00_fx_001F4A00(S *s, u64 a0, u64 a1, u64 *v0)
{
    FxFrame fr = fx_enter(s, 0x001F4A00u, 0);
    EmArea00FxRegs r = {0};
    fx_arg(&r, R_A0, a0);
    fx_arg(&r, R_A1, a1);
    fx_call(s, FX_001F4BF0, &r, v0, NULL);
    fx_leave(s, fr);
    return fx_result(s);
}

int em_area00_fx_001F4E20(S *s, u64 a0, u64 a1, u32 f12, u64 *v0)
{
    FxFrame fr = fx_enter(s, 0x001F4E20u, 0);
    EmArea00FxRegs r = {0};
    fx_arg(&r, R_A0, a0);
    fx_arg(&r, R_A1, a1);
    fx_farg(&r, 12, f12);
    fx_farg(&r, 13, 0x40B00000u); /* 5.5 */
    fx_call(s, FX_001F4D40, &r, v0, NULL);
    fx_leave(s, fr);
    return fx_result(s);
}
