/* AREA00 lane A00FX, spawn wrappers and the subtype-6 handler: 001AA840,
 * 001EF510, 001EFF10, 001EFFD0, 001F0060, 001F00A0, 001F02C0. See
 * em_area00_fx.h and docs/AREA00_FX.md. */
#include "game/em_area00_fx_internal.h"

/* ---- 001AA840 ------------------------------------------------------------ */

int em_area00_fx_001AA840(S *s)
{
    FxFrame fr = fx_enter(s, 0x001AA840u, 0x10);
    int32_t n = fx_lh(s, FX_D_00275B84);
    if (n != 0) {
        u32 p = fx_lw(s, FX_D_00275B7C);
        do {
            const u32 e = fx_lw(s, p);
            n = (int32_t)((u32)n - 1u);
            p += 4u;
            if (e != 0 && (fx_lbu(s, e + 2u) & 0x1Fu) == 4u && fx_lbu(s, e + 3u) == 0x29u && fx_lbu(s, e) == 1u) {
                EmArea00FxRegs r = {0};
                u64 v0;
                fx_arg(&r, R_A1, fx_sx(e));
                fx_call(s, FX_001AA7A0, &r, &v0, NULL);
                if (v0 != 0) break;
            }
        } while (n != 0 && !fx_latched(s));
    }
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001EF510 ------------------------------------------------------------ */

#define F_1EM6 0x358637BDu /* 1e-6 */
#define F_6 0x40C00000u

/* One round: the LCG word +4 of the work block gives f13 = ((w >> 16) &
 * 0xFFFF) / 65535 + 1e-4 and becomes w * 37 + 11; then 001CFB50 and
 * 001CFBE0 with the table. */
static void ef510_round(S *s, u64 a0, u64 a1, u32 table)
{
    const u32 work = fx_lw(s, FX_D_00275C34);
    const u32 w = fx_lw(s, work + 4u);
    u32 f13 = fx_fcvt((w >> 16) & 0xFFFFu);
    f13 = fx_fdiv(f13, FX_F_65535);
    fx_sw(s, work + 4u, w * 37u + 11u);
    const u32 f12 = fx_lw(s, fx_lw(s, FX_D_00275C34) + 0x54u);
    f13 = fx_fadd(f13, FX_F_1EM4);
    EmArea00FxRegs r = {0};
    fx_arg(&r, R_A0, FX_D_0081F8F0);
    fx_arg(&r, R_A1, 0);
    fx_arg(&r, R_A2, a0);
    fx_farg(&r, 12, f12);
    fx_farg(&r, 13, f13);
    fx_farg(&r, 14, FX_F_ONE);
    fx_farg(&r, 15, F_1EM6);
    fx_farg(&r, 16, F_6);
    fx_call(s, FX_001CFB50, &r, NULL, NULL);
    EmArea00FxRegs t = {0};
    fx_arg(&t, R_A0, a1);
    fx_arg(&t, R_A1, 1);
    fx_arg(&t, R_A2, table);
    fx_arg(&t, R_A3, FX_D_0081F8F0);
    fx_arg(&t, R_T0, 0);
    fx_call(s, FX_001CFBE0, &t, NULL, NULL);
}

int em_area00_fx_001EF510(S *s, u64 a0, u64 a1)
{
    FxFrame fr = fx_enter(s, 0x001EF510u, 0x30);
    if (fx_feq(0, fx_lw(s, fx_lw(s, FX_D_00275C34) + 0x54u))) {
        const u32 node = fx_lw(s, FX_D_00275C30);
        fx_call3(s, FX_001EFD90, fx_sx(0x80000036u), fx_sx(node + 0xB0u), fx_sx(node + 0xC0u));
    }
    ef510_round(s, a0, a1, 0x00257990u);
    fx_call2(s, FX_001EEEB0, a0, a1);
    ef510_round(s, a0, a1, 0x00257AB0u);
    ef510_round(s, a0, a1, 0x00257A20u);
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- the 001EF9D0 wrappers -------------------------------------------------- */

int em_area00_fx_001EFF10(S *s, u64 id, u64 a1, u64 a2, u64 a3, u64 t0, u64 t1, u32 f12, u64 *v0)
{
    FxFrame fr = fx_enter(s, 0x001EFF10u, 0x80);
    const u64 node = fx_call2f(s, FX_001EF9D0, id, fx_sx((u32)a1 + 0x30u), FX_F_ONE);
    if (node != 0) {
        const u32 w = (u32)node + 0x1F0u;
        fx_sw(s, (u32)node + 0x1F0u, (u32)a1);
        fx_sw(s, (u32)node + 0x1FCu, f12);
        fx_00102948(s, w + 0x10u, (u32)a2);
        fx_00102948(s, w + 0x20u, (u32)a3);
        fx_00102948(s, w + 0x30u, (u32)t0);
        fx_00102948(s, w + 0x40u, (u32)t1);
    }
    if (v0) *v0 = node;
    fx_leave(s, fr);
    return fx_result(s);
}

int em_area00_fx_001EFFD0(S *s, u64 id, u64 a1, u64 a2, u64 a3, u32 f12, u64 *v0)
{
    FxFrame fr = fx_enter(s, 0x001EFFD0u, 0x60);
    const u64 node = fx_call2f(s, FX_001EF9D0, id, a1, FX_F_ONE);
    if (node != 0) {
        fx_00102948(s, (u32)node + 0xB0u, (u32)a1);
        fx_00102948(s, (u32)node + 0xC0u, (u32)a2);
        fx_sw(s, (u32)node + 0x20u, f12);
        fx_sh(s, (u32)node + 0x94u, (u32)a3);
    }
    if (v0) *v0 = node;
    fx_leave(s, fr);
    return fx_result(s);
}

int em_area00_fx_001F0060(S *s, u64 id, u64 owner)
{
    FxFrame fr = fx_enter(s, 0x001F0060u, 0x20);
    const u64 node = fx_call2f(s, FX_001EF9D0, id, 0, FX_F_ONE);
    if (node != 0) fx_sw(s, (u32)node + 0x24u, (u32)owner);
    fx_leave(s, fr);
    return fx_result(s);
}

int em_area00_fx_001F00A0(S *s, u64 id, u64 a1, u64 a2, u64 a3, u64 *v0)
{
    FxFrame fr = fx_enter(s, 0x001F00A0u, 0x50);
    const u32 f12 = fx_lw(s, (u32)a2 + 0xCu);
    const u64 node = fx_call2f(s, FX_001EF9D0, id, a1, f12);
    if (node != 0) {
        fx_00102948(s, (u32)node + 0xB0u, (u32)a1);
        fx_00102948(s, (u32)node + 0xC0u, (u32)a2);
        fx_sw(s, (u32)node + 0xBCu, FX_F_ONE);
        fx_sw(s, (u32)node + 0x38u, (u32)a3);
    }
    if (v0) *v0 = node;
    fx_leave(s, fr);
    return fx_result(s);
}

/* ---- 001F02C0 ------------------------------------------------------------ */

int em_area00_fx_001F02C0(S *s, u64 p, u64 id, u32 f12)
{
    FxFrame fr = fx_enter(s, 0x001F02C0u, 0x320);
    const u32 sp = s->sp;
    fx_00102948(s, sp + 0xE0u, (u32)p);
    EmArea00FxRegs r = {0};
    fx_arg(&r, R_A0, fx_sx(sp + 0x30u));
    fx_arg(&r, R_A1, id);
    fx_arg(&r, R_A2, 0);
    fx_farg(&r, 12, f12);
    fx_call(s, FX_001FBD50, &r, NULL, NULL);
    fx_leave(s, fr);
    return fx_result(s);
}
