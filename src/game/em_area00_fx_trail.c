/* AREA00 lane A00FX, the trail effect 0x8000000D: 001F1550, 001F15F0,
 * 001F18C0. See em_area00_fx.h and docs/AREA00_FX.md. */
#include "game/em_area00_fx_internal.h"

/* ---- 001F1550 ------------------------------------------------------------ */

int em_area00_fx_001F1550(S *s, u64 node, u64 count, u64 *v0)
{
    FxFrame fr = fx_enter(s, 0x001F1550u, 0x40);
    const u32 n = (u32)node;
    u64 result;
    fx_sb(s, n + 0xCu, (u32)count);
    const u32 want = fx_lbu(s, n + 0xCu);
    if (fx_lh(s, FX_D_00275BCC) < (int32_t)want) {
        fx_sb(s, n + 4u, 3);
        result = 0;
    } else {
        u32 p = n;
        for (int32_t i = 0; i < (int32_t)fx_lbu(s, n + 0xCu) && !fx_latched(s); ++i, p += 4u)
            fx_sw(s, p + 0x110u, (u32)fx_call0(s, FX_001AF780));
        fx_call0(s, FX_001CB5B0);
        fx_sb(s, n + 9u, fx_lbu(s, n + 0xCu));
        result = 1;
    }
    if (v0) *v0 = result;
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F15F0 ------------------------------------------------------------ */

#define F_ADC 0x8000u

int em_area00_fx_001F15F0(S *s, u64 node, u64 slot, u64 row)
{
    FxFrame fr = fx_enter(s, 0x001F15F0u, 0xD0);
    const u32 sp = s->sp, w = (u32)node + 0x1F0u;
    const u64 page = fx_call1(s, FX_001CCF70, fx_sx(fx_lw(s, (u32)node + 0x1F0u) + 0x30u));
    if (page != 0xFFFFFFu) {
        u32 ca0[4], m[4][4];
        fx_ldq(s, fx_lw(s, FX_D_00275670) + 0xA0u, ca0);
        fx_ldm(s, 0x70003AC0u, m);
        u32 b = (u32)fx_call3(s, FX_001CB5F0, FX_D_007635C0, page, 0x3E);
        fx_stq0(s, b);
        fx_sw(s, b + 0xCu, 0x5000003Du);
        b += 0x10u;
        fx_sd(s, b, UINT64_C(0x402640000000800F));
        fx_sd(s, b + 8u, 0x4141);
        b += 0x10u;
        u64 n = slot, r = row;
        for (unsigned iter = 0;; ++iter) {
            if (fx_latched(s)) break;
            if (iter != 0 && n == slot && r == row) break;
            const u32 hist = fx_lw(s, fx_lw(s, FX_D_00275B40) + ((u32)n << 2));
            const u32 scale = fx_fdiv(fx_lw(s, ((u32)r << 2) + hist + 0xA0u), FX_F_10);
            fx_call2f(s, FX_00102900, fx_sx(0x70003600u), fx_sx(w + 0x30u), scale);
            fx_call2f(s, FX_00102900, fx_sx(0x70003610u), fx_sx(w + 0x40u), scale);
            fx_call2(s, FX_00102990, fx_sx(b), fx_sx(0x70003600u));
            fx_call2(s, FX_00102990, fx_sx(b + 0x20u), fx_sx(0x70003610u));
            for (u32 e = 0; e < 2; ++e) {
                u32 v[4], q;
                fx_ldq(s, hist + (((u32)r + e) << 4), v);
                fx_transform(s, m, v, v);
                q = fx_vrcp(s, v[3]);
                fx_stq(s, sp + 0xB0u + 0x10u * e, v);
                fx_vu(s, EM_VU_MULQ, 14, EM_VU_NO_BC, v, NULL, q, NULL, v);
                fx_fog(s, ca0, v);
                fx_ftoi4(v);
                fx_stq(s, b + 0x10u + 0x20u * e, v);
            }
            const u32 age = ((u32)r << 2) + hist;
            const u32 f1 = fx_lw(s, age + 0xA0u);
            b += 0x40u;
            if (fx_fle(f1, 0)) {
                fx_sw(s, b + 0x1Cu, fx_lw(s, b + 0x1Cu) | F_ADC);
                fx_sw(s, b + 0x2Cu, fx_lw(s, b + 0x2Cu) | F_ADC);
            } else {
                fx_sw(s, age + 0xA0u, fx_fsub(f1, FX_F_ONE));
            }
            r = fx_sx((u32)r - 2u);
            if ((int64_t)r < 0) {
                n = fx_sx((u32)n - 1u);
                r = 8;
                if ((int64_t)n < 0) n = 2;
            }
        }
        fx_call3(s, FX_001CB900, FX_D_007635C0, page, 2);
    }
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F18C0 ------------------------------------------------------------ */

int em_area00_fx_001F18C0(S *s, u64 node)
{
    FxFrame fr = fx_enter(s, 0x001F18C0u, 0x90);
    const u32 nd = (u32)node, w = nd + 0x1F0u;
    const u32 st = fx_lbu(s, nd + 4u);
    if (st == 3 || st == 2) {
        fx_call1(s, FX_001AFC10, node);
    } else if (st == 0 || st == 1) {
        int run = 1;
        if (st == 0) {
            u64 ok = 0;
            em_area00_fx_001F1550(s, node, 3, &ok);
            s->core.function = 0x001F18C0u;
            if (ok == 0) {
                run = 0;
            } else {
                for (u32 i = 0; i < 3; ++i) {
                    const u32 hist = fx_lw(s, fx_lw(s, FX_D_00275B40) + 4u * i);
                    for (u32 j = 0; j < 10; ++j) {
                        fx_sw(s, hist + 4u * j + 0xA0u, 0);
                        fx_call3(s, FX_001026A0, fx_sx(hist + 16u * j), fx_sx(fx_lw(s, w)), fx_sx(w + 0x10u));
                        fx_call3(s, FX_001026A0, fx_sx(hist + 16u * (j + 1u)), fx_sx(fx_lw(s, w)), fx_sx(w + 0x20u));
                    }
                }
                fx_sw(s, w + 4u, 0);
                fx_sw(s, w + 8u, 0);
                fx_sb(s, nd + 4u, 1);
            }
        }
        if (run) {
            const u32 slot = fx_lw(s, w + 8u);
            const u32 tbl = fx_lw(s, FX_D_00275B40);
            const u32 row = fx_lw(s, w + 4u);
            const u32 hist = fx_lw(s, tbl + (slot << 2));
            fx_sw(s, (row << 2) + hist + 0xA0u, FX_F_10);
            {
                const u32 r1 = fx_lw(s, w + 4u);
                fx_call3(s, FX_001026A0, fx_sx(hist + (r1 << 4)), fx_sx(fx_lw(s, w)), fx_sx(w + 0x10u));
            }
            {
                const u32 r2 = fx_lw(s, w + 4u);
                fx_call3(s, FX_001026A0, fx_sx(hist + ((r2 + 1u) << 4)), fx_sx(fx_lw(s, w)), fx_sx(w + 0x20u));
            }
            {
                const u32 a1 = fx_lw(s, w + 8u), a2 = fx_lw(s, w + 4u);
                em_area00_fx_001F15F0(s, node, fx_sx(a1), fx_sx(a2));
                s->core.function = 0x001F18C0u;
            }
            fx_sw(s, w + 4u, fx_lw(s, w + 4u) + 2u);
            if (!((int32_t)fx_lw(s, w + 4u) < 10)) {
                fx_sw(s, w + 4u, 0);
                fx_sw(s, w + 8u, fx_lw(s, w + 8u) + 1u);
                if (!((int32_t)fx_lw(s, w + 8u) < 3)) fx_sw(s, w + 8u, 0);
            }
        }
    }
    fx_leave(s, fr);
    return fx_result(s);
}
