/* AREA00 lane A00FX, the exit-phase effect and 001C50B0's helpers: 001F4F40,
 * 001F4F90, 001F5040, 001F5490, 001F5F60. See em_area00_fx.h and
 * docs/AREA00_FX.md. */
#include "game/em_area00_fx_internal.h"

/* ---- 001F4F40 ------------------------------------------------------------ */

int em_area00_fx_001F4F40(S *s, u64 sub, u64 *v0)
{
    FxFrame fr = fx_enter(s, 0x001F4F40u, 0x20);
    u64 node = fx_call1(s, FX_001AFA90, 0xC);
    if (node != 0) {
        fx_sb(s, (u32)node + 0xDu, (u32)sub);
        fx_sw(s, (u32)node + 0x10u, 0x001F5040u);
    } else {
        node = 0;
    }
    if (v0) *v0 = node;
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F4F90 ------------------------------------------------------------ */

#define D_0026EA80 0x0026EA80u /* the line colours (quadwords) */
#define D_0026EAC0 0x0026EAC0u /* the eight colour-index pairs (words) */

int em_area00_fx_001F4F90(S *s, u64 node, u32 f12)
{
    FxFrame fr = fx_enter(s, 0x001F4F90u, 0x60);
    const u32 sp = s->sp, nd = (u32)node;
    fx_call2f(s, FX_00102900, fx_sx(sp + 0x50u), fx_sx(nd + 0xD0u), f12);
    fx_00102948(s, sp + 0x30u, nd + 0x100u);
    u32 pair = D_0026EAC0;
    for (u32 i = 0; i < 8 && !fx_latched(s); ++i, pair += 4u) {
        fx_call3(s, FX_001028B8, fx_sx(sp + 0x40u), fx_sx(sp + 0x30u), fx_sx(sp + 0x50u));
        const u32 c0 = fx_lw(s, pair), c1 = fx_lw(s, pair + 4u);
        em_area00_fx_001CD940(s, 2, fx_sx(sp + 0x30u), fx_sx(D_0026EA80 + (c0 << 4)), fx_sx(sp + 0x40u),
                              fx_sx(D_0026EA80 + (c1 << 4)));
        fx_00102948(s, sp + 0x30u, sp + 0x40u);
    }
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F5040 ------------------------------------------------------------ */

#define D_0025AD70 0x0025AD70u /* the quadword 001D80E0 receives */
#define F_0P15 0x3E19999Au
#define F_0P05 0x3D4CCCCCu
#define F_2POWM31 0x30000000u
#define F_M96 0xC2C00000u
#define F_M128 0xC3000000u
#define F_0P35 0x3EB33333u
#define F_0P8 0x3F4CCCCDu
#define F_2P4 0x4019999Au

/* 001CA5E0(node, 001C6120(*D_0028A56C, index), 2) */
static void f5040_clip(S *s, u64 node, u32 index)
{
    const u64 e = fx_call2(s, FX_001C6120, fx_sx(fx_lw(s, FX_D_0028A56C)), index);
    fx_call3(s, FX_001CA5E0, node, e, 2);
}

int em_area00_fx_001F5040(S *s, u64 node)
{
    FxFrame fr = fx_enter(s, 0x001F5040u, 0x60);
    const u32 sp = s->sp, nd = (u32)node, w = nd + 0x1F0u;
    fx_00102948(s, sp + 0x50u, D_0025AD70);
    const u32 st = fx_lbu(s, nd + 4u);
    if (st == 3 || st == 2) {
        fx_call1(s, FX_001AFC10, node);
        goto out;
    }
    if (st != 0 && st != 1) goto out;
    if (st == 0) {
        const u32 sub = fx_lbu(s, nd + 0xDu);
        f5040_clip(s, node, sub == 2 ? 0xF : sub == 4 ? 0xE : sub == 1 ? 0xB : 0xD);
        const u64 n = fx_call1(s, FX_001C6150, fx_sx(fx_lw(s, nd + 0x44u)));
        fx_sb(s, nd + 0xCu, (u32)n);
        const int32_t room = fx_lh(s, FX_D_00275BCC);
        if (room < (int32_t)fx_lbu(s, nd + 0xCu)) {
            fx_sb(s, nd + 4u, 3);
            goto out;
        }
        u32 p = nd, have;
        for (int32_t i = 0; (have = fx_lbu(s, nd + 0xCu)), i < (int32_t)have && !fx_latched(s); ++i, p += 4u)
            fx_sw(s, p + 0x110u, (u32)fx_call0(s, FX_001AF780));
        fx_sb(s, nd + 9u, have);
        (void)fx_lbu(s, nd + 0xCu);
        fx_call0(s, FX_001CB5B0);
        fx_call1(s, FX_001C62C0, node);
        const u32 r = fx_fcvt((u32)fx_call0(s, FX_00122BB8));
        const u32 j = fx_fadd(F_0P15, fx_fmul(F_0P05, fx_fmul(F_2POWM31, r)));
        fx_sw(s, nd + 0x68u, j);
        fx_sw(s, nd + 0x64u, j);
        fx_sw(s, nd + 0x60u, j);
        fx_sw(s, w + 0x48u, F_0P15);
        fx_sw(s, w + 0x44u, F_0P15);
        fx_sw(s, w + 0x40u, F_0P15);
        const u32 kind = fx_lbu(s, nd + 0xDu);
        const u32 base = (kind == 4 || kind == 3) ? F_M96 : 0;
        fx_sw(s, nd + 0x88u, base);
        fx_sw(s, nd + 0x84u, base);
        fx_sw(s, nd + 0x80u, base);
        fx_sw(s, nd + 0x8Cu, 0);
        fx_call2(s, FX_001D80E0, fx_sx(nd + 0x100u), fx_sx(sp + 0x50u));
        fx_sh(s, nd + 0x28u, 0);
        fx_sb(s, nd + 4u, 1);
    }
    {
        const int32_t frame = fx_lh(s, nd + 0x28u);
        fx_sh(s, nd + 0x28u, (u32)frame + 1u);
        if (frame == 0xF) {
            fx_sb(s, nd + 4u, 2);
        } else if (frame == 2 || frame == 1 || frame == 0) {
            const u32 sub = fx_lbu(s, nd + 0xDu);
            if (sub == 0) f5040_clip(s, node, 8);
            if (sub == 0 || sub == 1) em_area00_fx_001F4F90(s, node, F_2P4);
        } else if (frame == 3) {
            const u32 sub = fx_lbu(s, nd + 0xDu);
            if (!(sub == 2 || sub == 4 || sub == 1 || sub == 3)) f5040_clip(s, node, 7);
        } else {
            const u32 cur = fx_lw(s, nd + 0x80u);
            const u32 eased = fx_fadd(cur, fx_fmul(F_0P35, fx_fsub(F_M128, cur)));
            fx_sw(s, nd + 0x80u, eased);
            fx_sw(s, nd + 0x88u, eased);
            fx_sw(s, nd + 0x84u, eased);
        }
        for (u32 k = 0; k < 3; ++k) {
            const u32 step = fx_lw(s, w + 0x40u + 4u * k);
            const u32 v = fx_lw(s, nd + 0x60u + 4u * k);
            fx_sw(s, nd + 0x60u + 4u * k, fx_fadd(v, step));
        }
        for (u32 k = 0; k < 3; ++k) fx_sw(s, w + 0x40u + 4u * k, fx_fmul(fx_lw(s, w + 0x40u + 4u * k), F_0P8));
        fx_call1(s, FX_001C63D0, node);
        fx_call1(s, FX_001B17A0, node);
        fx_call1(s, fx_lw(s, nd + 0x4Cu), node);
    }
out:
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F5490 ------------------------------------------------------------ */

int em_area00_fx_001F5490(S *s, u64 p, u64 *v0)
{
    FxFrame fr = fx_enter(s, 0x001F5490u, 0x20);
    u64 result = 3;
    if (fx_call1(s, FX_001C22A0, p) == 0) {
        fx_call1(s, FX_001C6380, p);
        result = 1;
    }
    if (v0) *v0 = result;
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F5F60 ------------------------------------------------------------ */

/* One tag on the channel-3 cursor (context +0x1C; ctx is the D_00275670
 * value the caller loaded just before), each store after its own reload of
 * the cursor word; returns the cursor loaded last, which becomes cursor +
 * size. */
static u32 f5f60_tag(S *s, u32 ctx, u32 id, u32 qwc, u32 size)
{
    fx_sb(s, fx_lw(s, ctx + 0x1Cu) + 3u, id);
    fx_sw(s, fx_lw(s, ctx + 0x1Cu) + 4u, 0);
    fx_sh(s, fx_lw(s, ctx + 0x1Cu), qwc);
    const u32 cur = fx_lw(s, ctx + 0x1Cu);
    fx_sw(s, ctx + 0x1Cu, cur + size);
    return cur;
}

int em_area00_fx_001F5F60(S *s, u64 pos, u64 rot, u64 a2, u64 entry)
{
    FxFrame fr = fx_enter(s, 0x001F5F60u, 0xA0);
    const u32 sp = s->sp;
    const u32 start = fx_lw(s, fx_lw(s, FX_D_00275670) + 0x1Cu);
    fx_00102948(s, sp + 0x90u, (u32)pos);
    fx_sw(s, sp + 0x9Cu, FX_F_ONE);
    fx_call1(s, FX_001029C0, fx_sx(sp + 0x50u));
    fx_call3(s, FX_00102C58, fx_sx(sp + 0x50u), fx_sx(sp + 0x50u), rot);
    fx_call3(s, FX_00102918, fx_sx(sp + 0x50u), fx_sx(sp + 0x50u), fx_sx(sp + 0x90u));
    const u32 blk = f5f60_tag(s, fx_lw(s, FX_D_00275670), 0x10, 5, 0x60) + 0x10u;
    for (u32 a = 0x70003400u; a < 0x70003470u; a += 4u) fx_sw(s, a, 0);
    fx_call3(s, FX_001028B8, fx_sx(0x70003470u), a2, fx_sx(0x0026EB50u));
    fx_sw(s, blk, 0x11000000u);
    fx_sw(s, blk + 4u, 0x01000101u);
    fx_sw(s, blk + 8u, 0);
    fx_sw(s, blk + 0xCu, 0x6C0403F5u);
    for (u32 i = 0; i < 4; ++i) fx_00102948(s, blk + 0x10u + 0x10u * i, 0x70003440u + 0x10u * i);
    const u32 t = f5f60_tag(s, fx_lw(s, FX_D_00275670), 0x10, 9, 0xA0);
    fx_stq0(s, t + 0x10u);
    fx_sw(s, t + 0x14u, 0x01000101u);
    fx_sw(s, t + 0x18u, 0);
    fx_sw(s, t + 0x1Cu, 0x6C080000u);
    fx_call3(s, FX_001026D0, fx_sx(t + 0x20u), fx_sx(0x70003AC0u), fx_sx(sp + 0x50u));
    fx_call3(s, FX_001026D0, fx_sx(t + 0x60u), fx_sx(0x70003400u), fx_sx(sp + 0x50u));
    fx_call1(s, FX_001D3990, entry);
    const u32 ctx = fx_lw(s, FX_D_00275670);
    (void)f5f60_tag(s, ctx, 0x60, 0, 0x10);
    fx_call3(s, FX_001CAAC0, fx_sx(sp + 0x90u), fx_sx(start), fx_sx(ctx));
    fx_leave(s, fr);
    return fx_result(s);
}
