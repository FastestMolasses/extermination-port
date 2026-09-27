/* Status pages lane: the MAP helpers 00211240 / 00211310 / 002117D0 and
 * the DATABASE helpers 00213A00 / 00213C50 / 00213CC0 / 001FCF30, the
 * callees the committed AREA01 translations of 0020F950 / 00211400 and
 * 00214020 / 002131B0 still reach through `call`. Hand translation of the
 * original functions (boot ELF SCUS-97112); docs/STATUS_PAGES.md. */
#include "game/em_status_pages_internal.h"

/* 16 * (1792 + x) style sums: the operand orders of the originals. */

/* ------------------------------------------------------------------ */
/* 00211240(k): one list-mode item marker. The halfword pair at
 * D_00265920 + 4k gives (x, y); the sprite goes to
 * (float_to_int(16 * (x + 0x700)), float_to_int(16 * ((y >> 1) + 0x790))),
 * 16 x 16, colour 0x80808080. The second halfword is read after the first
 * conversion. */
int em_status_pages_00211240(EmArea01Ui *s, int32_t k)
{
    if (sp_begin(s, 0x00211240u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00211240u, 0x30);
    const uint32_t off = (uint32_t)k << 2;
    const uint64_t x = sp_f2i(s, sp_x16(ui_lh(s, SP_D_00265920 + off) + 0x700));
    const uint64_t y = sp_f2i(s, sp_x16((ui_lh(s, SP_D_00265920 + 2u + off) >> 1) + 0x790));
    sp_blit(s, x, y, 0x10, 0x10, SP_RGBA_80, SP_TEX(0x20042E85u, 0x113221D0u));
    return ui_leave(s, fr);
}

/* 00211310(v): one map-view item marker at the vector v (x at +0, z at
 * +8): screen x = float_to_int(16 * (1792 + ((256 + z) - 8))), then (x
 * read after that call) screen y = float_to_int(16 * (1936 + ((164 - x) -
 * 8) / 2)); 16 x 16, colour 0x80808080, the texture of 00211240. */
int em_status_pages_00211310(EmArea01Ui *s, uint32_t v)
{
    if (sp_begin(s, 0x00211310u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00211310u, 0x20);
    uint32_t f = ui_fadd(0x43800000u, ui_lw(s, v + 8u));
    f = ui_fsub(f, 0x41000000u);
    f = ui_fadd(0x44E00000u, f);
    const uint64_t sx = sp_f2i(s, ui_fmul(0x41800000u, f));
    f = ui_fsub(0x43240000u, ui_lw(s, v));
    f = ui_fsub(f, 0x41000000u);
    f = ui_fdiv(f, 0x40000000u);
    f = ui_fadd(0x44F20000u, f);
    const uint64_t sy = sp_f2i(s, ui_fmul(0x41800000u, f));
    sp_blit(s, sx, sy, 0x10, 0x10, SP_RGBA_80, SP_TEX(0x20042E85u, 0x113221D0u));
    return ui_leave(s, fr);
}

/* 002117D0(a0, v, map, floor): the floor row r = the word D_00265890[map]
 * + floor * 12; v.x -= r[0], v.z -= r[1]; v (w = 1.0) to 0x70003600;
 * 001029C0(0x70003400) (a1 still holds v: the original never loads the
 * row there), 00102BB0(0x70003400, 0x70003400, -r[2]) (r[2] read after
 * the first call), 001026A0(0x70003600, 0x70003400, 0x70003600); v.x / v.z
 * = the turned x / z; then with k = D_00810154 (the zoom, re-read where
 * the original re-reads it): h = k / 2, s = 0.10666667 * k, v.x += h *
 * -(D_0081015C / s), v.z += h * (D_00810158 / s), v.x *= 0.10666667 * k,
 * v.z *= 0.08533333 * k. a0 is not used. */
int em_status_pages_002117D0(EmArea01Ui *s, uint64_t a0, uint32_t v, int32_t map, int32_t floor)
{
    (void)a0;
    if (sp_begin(s, 0x002117D0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x002117D0u, 0x40);
    const uint32_t base = ui_lw(s, SP_D_00265890 + ((uint32_t)map << 2));
    const uint32_t off = (uint32_t)floor * 12u;
    const uint32_t row = base + off;
    ui_sw(s, v, ui_fsub(ui_lw(s, v), ui_lw(s, row)));
    ui_sw(s, v + 8u, ui_fsub(ui_lw(s, v + 8u), ui_lw(s, row + 4u)));
    ui_sw(s, 0x70003600u, ui_lw(s, v));
    ui_sw(s, 0x70003604u, ui_lw(s, v + 4u));
    ui_sw(s, 0x70003608u, ui_lw(s, v + 8u));
    ui_sw(s, 0x7000360Cu, 0x3F800000u);
    (void)ui_call1(s, SP_001029C0, 0x70003400u);
    {
        uint64_t a[2] = {0x70003400u, 0x70003400u};
        uint32_t f = ui_fneg(ui_lw(s, off + base + 8u));
        ui_call(s, SP_00102BB0, 2, a, 1, &f, NULL, NULL);
    }
    (void)ui_call3(s, SP_001026A0, 0x70003600u, 0x70003400u, 0x70003600u);
    ui_sw(s, v, ui_lw(s, 0x70003600u));
    ui_sw(s, v + 8u, ui_lw(s, 0x70003608u));
    const uint32_t k = ui_lw(s, 0x00810154u);
    const uint32_t x0 = ui_lw(s, v);
    const uint32_t h = ui_fdiv(k, 0x40000000u);
    uint32_t zp = ui_lw(s, 0x00810158u);
    uint32_t xp = ui_lw(s, 0x0081015Cu);
    const uint32_t sk = ui_fmul(UI_F_K, k);
    xp = ui_fdiv(xp, sk);
    zp = ui_fdiv(zp, sk);
    xp = ui_fneg(xp);
    ui_sw(s, v, ui_fadd(x0, ui_fmul(h, xp)));
    ui_sw(s, v + 8u, ui_fadd(ui_lw(s, v + 8u), ui_fmul(h, zp)));
    uint32_t m = ui_fmul(UI_F_K, ui_lw(s, 0x00810154u));
    ui_sw(s, v, ui_fmul(ui_lw(s, v), m));
    m = ui_fmul(0x3DAEC33Eu, ui_lw(s, 0x00810154u));
    ui_sw(s, v + 8u, ui_fmul(ui_lw(s, v + 8u), m));
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 00213C50(t, sel): the 9-entry window t+0x90.. from the ring t+0x50..
 * (length t[0x18]) starting at t[0x19], one entry earlier (wrapping to
 * length - 1) when sel == 1. */
int em_status_pages_00213C50(EmArea01Ui *s, uint32_t t, int32_t sel)
{
    if (ui_latched(s)) return -1;
    UiFrame fr = ui_enter(s, 0x00213C50u, 0);
    int32_t idx = (int32_t)ui_lbu(s, t + 0x19u);
    if (sel == 1) idx = idx != 0 ? idx - 1 : (int32_t)ui_lbu(s, t + 0x18u) - 1;
    for (uint32_t i = 0; i < 9; ++i) {
        ui_sb(s, t + i + 0x90u, ui_lbu(s, t + (uint32_t)idx + 0x50u));
        ++idx;
        if (!(idx < (int32_t)ui_lbu(s, t + 0x18u))) idx = 0;
    }
    return ui_leave(s, fr);
}

/* The two list-highlight sprites of 00213A00 / 00213CC0 at the cursor
 * row t[0x17]: y = float_to_int(16 * (((t[0x17] * 24 + 0x88) >> 1) +
 * 0x790)), t[0x17] read again for the second. */
static void cursor_bars(EmArea01Ui *s, uint32_t t)
{
    for (int k = 0; k < 2; ++k) {
        const int32_t row = (int32_t)ui_lbu(s, t + 0x17u) * 24 + 0x88;
        const uint64_t y = sp_f2i(s, sp_x16((row >> 1) + 0x790));
        sp_blit(s, k == 0 ? 0x7000u : 0x8000u, y, 0x100, 0x40, SP_RGBA_80,
                SP_TEX(k == 0 ? 0x20044585u : 0x20044705u, k == 0 ? 0xA1322100u : 0xA1322140u));
    }
}

/* 00213A00(t, a1): t[0x1A] = 0; unless the list is empty (t[0x18] == 0)
 * or a1 & 0x400, the pad repeat D_00810E78 moves the cursor t[0x17]: 0x1000
 * up (at 0: t[0x1A] = 1 and the result 1), else 0x4000 down (at 7 or
 * more: t[0x17] = 7, t[0x1A] = 2, result 1), each with the cursor cue
 * 0020CDA0; the window t+0x90.. is refilled from the ring at t[0x19]
 * (00213C50's loop, inline); the first 8 window entries are drawn as help
 * lines 001FCF60(id, 0x64, 0x47 + 12 i); blend 0 and the two cursor bars.
 * Returns the wrap flag. */
int em_status_pages_00213A00(EmArea01Ui *s, uint32_t t, int32_t a1, uint32_t *v0)
{
    if (sp_begin(s, 0x00213A00u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00213A00u, 0x50);
    uint32_t wrap = 0;
    ui_sb(s, t + 0x1Au, 0);
    if (ui_lbu(s, t + 0x18u) != 0 && !(a1 & 0x400)) {
        const uint32_t rep = ui_lhu(s, UI_D_00810E78);
        if (rep & 0x1000u) {
            const uint32_t c = ui_lbu(s, t + 0x17u);
            if (c != 0) {
                ui_sb(s, t + 0x17u, c - 1u);
            } else {
                wrap = 1;
                ui_sb(s, t + 0x1Au, 1);
            }
            (void)ui_call0(s, SP_0020CDA0);
        } else if (rep & 0x4000u) {
            const uint32_t c = ui_lbu(s, t + 0x17u);
            if ((int32_t)c < 7) {
                ui_sb(s, t + 0x17u, c + 1u);
            } else {
                ui_sb(s, t + 0x17u, 7);
                wrap = 1;
                ui_sb(s, t + 0x1Au, 2);
            }
            (void)ui_call0(s, SP_0020CDA0);
        }
    }
    int32_t idx = (int32_t)ui_lbu(s, t + 0x19u);
    for (uint32_t i = 0; i < 9; ++i) {
        const uint32_t b = ui_lbu(s, t + (uint32_t)idx + 0x50u);
        ++idx;
        ui_sb(s, t + i + 0x90u, b);
        if (!(idx < (int32_t)ui_lbu(s, t + 0x18u))) idx = 0;
    }
    for (uint32_t i = 0; i < 8; ++i)
        (void)ui_call3(s, SP_001FCF60, ui_lbu(s, t + i + 0x90u), 0x64, 0x47u + 12u * i);
    sp_blend(s, 0);
    cursor_bars(s, t);
    if (v0) *v0 = wrap;
    return ui_leave(s, fr);
}

/* 00213CC0(t): the scroll step of the list. 00207D90(1, 0x46, 0x46, 0x1EA,
 * 0xA5); the direction d = +1 (and line offset -12) when t[0x1A] == 1,
 * else d = -1 (offset 0); the 9 window entries as help lines
 * 001FCF60(id, 0x64, t+0x1C + offset + 0x47 + 12 i) (the halfword re-read
 * each line); 00207D90(1, 0, 0, 0x200, 0xE0); t+0x1C += d; d = +1: when
 * t+0x1C >= 12, t[0x19] steps back one (0 wraps to t[0x18] - 1) and the
 * result is 1; d = -1: when t+0x1C < -11, t[0x19] steps forward one (past
 * t[0x18] - 2 wraps to 0) and the result is 1; then blend 0 and the two
 * cursor bars. */
int em_status_pages_00213CC0(EmArea01Ui *s, uint32_t t, uint32_t *v0)
{
    if (sp_begin(s, 0x00213CC0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00213CC0u, 0x70);
    uint32_t done = 0;
    {
        uint64_t a[5] = {1, 0x46, 0x46, 0x1EA, 0xA5};
        ui_call(s, SP_00207D90, 5, a, 0, NULL, NULL, NULL);
    }
    const int32_t d = ui_lbu(s, t + 0x1Au) == 1 ? 1 : -1;
    const int32_t off = d == 1 ? -12 : 0;
    for (uint32_t i = 0; i < 9; ++i) {
        const uint32_t id = ui_lbu(s, t + i + 0x90u);
        const int32_t y = ui_lh(s, t + 0x1Cu) + (off + 0x47 + 12 * (int32_t)i);
        (void)ui_call3(s, SP_001FCF60, id, 0x64, ui_sx((uint32_t)y));
    }
    {
        uint64_t a[5] = {1, 0, 0, 0x200, 0xE0};
        ui_call(s, SP_00207D90, 5, a, 0, NULL, NULL, NULL);
    }
    ui_sh(s, t + 0x1Cu, (uint32_t)(ui_lh(s, t + 0x1Cu) + d));
    if (d >= 0) {
        if (!(ui_lh(s, t + 0x1Cu) < 12)) {
            const uint32_t c = ui_lbu(s, t + 0x19u);
            if (c != 0)
                ui_sb(s, t + 0x19u, c - 1u);
            else
                ui_sb(s, t + 0x19u, ui_lbu(s, t + 0x18u) - 1u);
            done = 1;
        }
    } else if (ui_lh(s, t + 0x1Cu) < -11) {
        const int32_t n = (int32_t)ui_lbu(s, t + 0x18u);
        const int32_t c = (int32_t)ui_lbu(s, t + 0x19u);
        ui_sb(s, t + 0x19u, c < n - 1 ? (uint32_t)(c + 1) : 0u);
        done = 1;
    }
    sp_blend(s, 0);
    cursor_bars(s, t);
    if (v0) *v0 = done;
    return ui_leave(s, fr);
}

/* 001FCF30(a0, a1, a2): tail call 001FE070(bank, a0, a1, a2) with bank = the
 * word at D_0028A49C (p) plus its word +0 plus its word +0x10; the three
 * arguments are passed on as their full register images. */
int em_status_pages_001FCF30(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint32_t *v0)
{
    if (sp_begin(s, 0x001FCF30u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x001FCF30u, 0);
    const uint32_t p = ui_lw(s, SP_D_0028A49C);
    const uint32_t first = ui_lw(s, p);
    const uint32_t bank = ui_lw(s, p + 0x10u) + (p + first);
    uint64_t a[4] = {ui_sx(bank), a0, a1, a2}, r = 0;
    ui_call(s, SP_001FE070, 4, a, 0, NULL, &r, NULL);
    if (v0) *v0 = (uint32_t)r;
    return ui_leave(s, fr);
}
