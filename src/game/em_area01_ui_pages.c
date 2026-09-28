/* AREA01 lane UI: the status pages 0020F950 (page 1) and 00214020 (page 3)
 * with their helpers. Hand translation of the original functions (boot ELF
 * SCUS-97112); docs/AREA01_UI.md states what each does, how it was
 * verified and where the decomp's NEARMISS C differs from the instructions.
 * Callees are reached through EmArea01Ui.call by original address. */
#include "game/em_area01_ui_internal.h"

/* 64-bit register images of the constants the originals load. */
#define RGBA_80 UINT64_C(0xFFFFFFFF80808080) /* 0x80808080, sign-extended (lui) */
#define RGBA_4C UINT64_C(0x4C808080)
#define RGBA_40 UINT64_C(0x40808080)

/* 00207E40(1, x, y, w, h, rgba, tex): one sprite through the slot. */
static void blit(EmArea01Ui *s, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t rgba, uint64_t tex)
{
    uint64_t a[7] = {1, ui_sx(x), ui_sx(y), ui_sx(w), ui_sx(h), rgba, tex};
    ui_call(s, UI_00207E40, 7, a, 0, NULL, NULL, NULL);
}

static void blend(EmArea01Ui *s, uint32_t mode) { (void)ui_call2(s, UI_00207D00, 1, mode); }

/* ------------------------------------------------------------------ */
/* 00207D90(slot, b0, b2, b1, b3): the slot record is D_00275670 + slot * 4
 * (its cursor word at +0x10, loaded again before each header store);
 * header byte +3 = 0x10, word +4 = 0, halfword +0 = 3, cursor += 0x40;
 * then the packet: quadword +0x10 = 0, +0x1C = 0x50000002, +0x20 =
 * 0x1000000000008001, +0x28 = 0xE, +0x30 = the four lanes b0 | b1 << 16 |
 * b2 << 32 | b3 << 48 (each lane sign-extended first), +0x38 = 0x40. */
int em_area01_ui_00207D90(EmArea01Ui *s, int32_t slot, int32_t b0, int32_t b2, int32_t b1, int32_t b3)
{
    if (ui_latched(s)) return -1;
    UiFrame fr = ui_enter(s, 0x00207D90u, 0);
    const uint64_t packed = ((uint64_t)(int64_t)b2 << 32) | ((uint64_t)(int64_t)b0 | ((uint64_t)(int64_t)b1 << 16)) |
                            ((uint64_t)(int64_t)b3 << 48);
    const uint32_t entry = ui_lw(s, UI_D_00275670) + ((uint32_t)slot << 2);
    ui_sb(s, ui_lw(s, entry + 0x10u) + 3u, 0x10);
    ui_sw(s, ui_lw(s, entry + 0x10u) + 4u, 0);
    ui_sh(s, ui_lw(s, entry + 0x10u), 3);
    const uint32_t p = ui_lw(s, entry + 0x10u);
    ui_sw(s, entry + 0x10u, p + 0x40u);
    const uint32_t zero[4] = {0, 0, 0, 0};
    ui_stq(s, p + 0x10u, zero);
    ui_sw(s, p + 0x1Cu, 0x50000002u);
    ui_sd(s, p + 0x20u, UINT64_C(0x1000000000008001));
    ui_sd(s, p + 0x28u, 0xE);
    ui_sd(s, p + 0x30u, packed);
    ui_sd(s, p + 0x38u, 0x40);
    return ui_leave(s, fr);
}

/* 00208040(slot, a, b, c, rgba): header as 00207D90 with halfword 5 and
 * cursor += 0x60; packet quadword +0x10 = 0, +0x1C = 0x50000004, +0x20 =
 * 0x5400000000008001, +0x28 = 0x44410, +0x30 = 0x143, +0x38 = rgba's low
 * word (zero-extended); then for v = a, b, c in turn the doubleword at
 * +0x40, +0x48, +0x50 = sx32(float_to_int(v.x) | float_to_int(v.z) << 16)
 * | 0xFFFFFF << 32 (v.z converted first, v.x read after that call), and
 * +0x58 = 0. */
int em_area01_ui_00208040(EmArea01Ui *s, int32_t slot, uint32_t a1, uint32_t a2, uint32_t a3, uint64_t a4)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, 0x00208040u, EM_A01R_FAULT_NULL_WORKER, 0);
    UiFrame fr = ui_enter(s, 0x00208040u, 0x50);
    const uint32_t entry = ui_lw(s, UI_D_00275670) + ((uint32_t)slot << 2);
    ui_sb(s, ui_lw(s, entry + 0x10u) + 3u, 0x10);
    ui_sw(s, ui_lw(s, entry + 0x10u) + 4u, 0);
    ui_sh(s, ui_lw(s, entry + 0x10u), 5);
    const uint32_t q = ui_lw(s, entry + 0x10u);
    ui_sw(s, entry + 0x10u, q + 0x60u);
    const uint32_t zero[4] = {0, 0, 0, 0};
    ui_stq(s, q + 0x10u, zero);
    ui_sw(s, q + 0x1Cu, 0x50000004u);
    ui_sd(s, q + 0x20u, UINT64_C(0x5400000000008001));
    ui_sd(s, q + 0x28u, 0x44410);
    ui_sd(s, q + 0x30u, 0x143);
    ui_sd(s, q + 0x38u, a4 & UINT64_C(0xFFFFFFFF));
    const uint32_t v[3] = {a1, a2, a3};
    for (int i = 0; i < 3; ++i) {
        uint64_t r;
        uint32_t f = ui_lw(s, v[i] + 8u);
        ui_call(s, UI_001281C0, 0, NULL, 1, &f, &r, NULL);
        const uint32_t hi = (uint32_t)r << 16;
        f = ui_lw(s, v[i]);
        ui_call(s, UI_001281C0, 0, NULL, 1, &f, &r, NULL);
        const uint32_t lo = (uint32_t)r | hi;
        ui_sd(s, q + 0x40u + 8u * (uint32_t)i, ui_sx(lo) | (UINT64_C(0xFFFFFF) << 32));
    }
    ui_sd(s, q + 0x58u, 0);
    return ui_leave(s, fr);
}

/* 0020F950's and 00210F30's calls of 00207D90 / 00208040: the translations
 * above, or with EmArea01Ui.leaf_calls the call itself through `call` (the
 * argument registers as the original loads them). */
static void leaf_00207D90(EmArea01Ui *s, int32_t slot, int32_t b0, int32_t b2, int32_t b1, int32_t b3)
{
    if (!s->leaf_calls) {
        (void)em_area01_ui_00207D90(s, slot, b0, b2, b1, b3);
        return;
    }
    uint64_t a[5] = {ui_sx((uint32_t)slot), ui_sx((uint32_t)b0), ui_sx((uint32_t)b2), ui_sx((uint32_t)b1),
                     ui_sx((uint32_t)b3)};
    ui_call(s, UI_00207D90, 5, a, 0, NULL, NULL, NULL);
}

static void leaf_00208040(EmArea01Ui *s, int32_t slot, uint32_t a1, uint32_t a2, uint32_t a3, uint64_t a4)
{
    if (!s->leaf_calls) {
        (void)em_area01_ui_00208040(s, slot, a1, a2, a3, a4);
        return;
    }
    uint64_t a[5] = {ui_sx((uint32_t)slot), ui_sx(a1), ui_sx(a2), ui_sx(a3), a4};
    ui_call(s, UI_00208040, 5, a, 0, NULL, NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* 00210A00(a0): 0020A7A0(0x20043C259D422050); blend 0; five sprites;
 * a sixth when a0 != 0; blend 3; one more sprite. */
int em_area01_ui_00210A00(EmArea01Ui *s, int32_t a0)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, 0x00210A00u, EM_A01R_FAULT_NULL_WORKER, 0);
    UiFrame fr = ui_enter(s, 0x00210A00u, 0x20);
    (void)ui_call1(s, UI_0020A7A0, UINT64_C(0x20043C259D422050));
    blend(s, 0);
    blit(s, 0x7000, 0x7900, 0x100, 0x100, RGBA_4C, UINT64_C(0x20042E8621321D00));
    blit(s, 0x8000, 0x7900, 0x100, 0x100, RGBA_4C, UINT64_C(0x20042E8621321D40));
    blit(s, 0x7000, 0x8100, 0x100, 0x40, RGBA_4C, UINT64_C(0x20042E85A1321F40));
    blit(s, 0x8000, 0x8100, 0x100, 0x40, RGBA_4C, UINT64_C(0x20042E85A1321FC0));
    blit(s, 0x7800, 0x8300, 0x100, 0x80, RGBA_40, UINT64_C(0x20043205E1321F00));
    if (a0 != 0) blit(s, 0x8790, 0x8420, 0x80, 0x40, RGBA_80, UINT64_C(0x20043A859D422040));
    blend(s, 3);
    blit(s, 0x7000, 0x8300, 0x80, 0x80, RGBA_80, UINT64_C(0x20043A25DD422020));
    return ui_leave(s, fr);
}

/* 00210C00(a0): blend 3; a left / right sprite pair chosen by D_00810E70
 * bits 0x2000, then 0x8000; when a0 == 1, an up / down pair chosen by bits
 * 0x1000, then 0x4000 (the halfword read again after the first pair). */
int em_area01_ui_00210C00(EmArea01Ui *s, int32_t a0)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, 0x00210C00u, EM_A01R_FAULT_NULL_WORKER, 0);
    UiFrame fr = ui_enter(s, 0x00210C00u, 0x20);
    const uint64_t hi = UINT64_C(0x20042E85) << 32;
    blend(s, 3);
    uint32_t pad = ui_lhu(s, UI_D_00810E70);
    uint32_t l = 0x55322158u, r = 0x5532215Cu;
    if (pad & 0x2000u) {
        l = 0x55322178u;
    } else if (pad & 0x8000u) {
        r = 0x5532217Cu;
    }
    blit(s, 0x8D50, 0x7D80, 0x20, 0x20, RGBA_80, hi | l);
    blit(s, 0x70B0, 0x7D80, 0x20, 0x20, RGBA_80, hi | r);
    if (a0 == 1) {
        pad = ui_lhu(s, UI_D_00810E70);
        uint32_t u = 0x55322168u, d = 0x5532216Cu;
        if (pad & 0x1000u) {
            u = 0x55322180u;
        } else if (pad & 0x4000u) {
            d = 0x55322184u;
        }
        blit(s, 0x7F00, 0x7960, 0x20, 0x20, RGBA_80, hi | u);
        blit(s, 0x7F00, 0x81A0, 0x20, 0x20, RGBA_80, hi | d);
    }
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 00210030(p, a1): with a1 == 0, a = (p+0xA0 + -85.0014) * p+0x60 and
 * b = p+0xA8 * p+0x60; otherwise t = D_00810154 / 2, b = 0 + D_00810158 * t,
 * a = -60 + D_0081015C * t. Then p+0xB0/B4/B8 = D_0081061C/062C/063C, and
 * three passes add D_00810618/0628/0638 * (*D_00275670)+0x2468 (the
 * context word read again each time), D_00810610/0620/0630 * b and
 * D_00810614/0624/0634 * a, each field read back from p before its add. */
int em_area01_ui_00210030(EmArea01Ui *s, uint32_t p, int32_t a1)
{
    if (ui_latched(s)) return -1;
    UiFrame fr = ui_enter(s, 0x00210030u, 0);
    uint32_t a, b;
    if (a1 == 0) {
        const uint32_t s60 = ui_lw(s, p + 0x60u);
        a = ui_fmul(ui_fadd(ui_lw(s, p + 0xA0u), 0xC2AA00B8u), s60);
        b = ui_fmul(ui_lw(s, p + 0xA8u), s60);
    } else {
        const uint32_t t = ui_fdiv(ui_lw(s, 0x00810154u), UI_F_TWO);
        const uint32_t y = ui_fmul(ui_lw(s, 0x0081015Cu), t);
        const uint32_t x = ui_fmul(ui_lw(s, 0x00810158u), t);
        b = ui_fadd(0, x);
        a = ui_fadd(0xC2700000u, y);
    }
    ui_sw(s, p + 0xB0u, ui_lw(s, 0x0081061Cu));
    ui_sw(s, p + 0xB4u, ui_lw(s, 0x0081062Cu));
    ui_sw(s, p + 0xB8u, ui_lw(s, 0x0081063Cu));
    static const uint32_t row0[3] = {0x00810618u, 0x00810628u, 0x00810638u};
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t ctx = ui_lw(s, UI_D_00275670);
        const uint32_t g = ui_lw(s, row0[i]);
        const uint32_t cur = ui_lw(s, p + 0xB0u + 4u * i);
        ui_sw(s, p + 0xB0u + 4u * i, ui_fadd(cur, ui_fmul(g, ui_lw(s, ctx + 0x2468u))));
    }
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t g = ui_lw(s, 0x00810610u + 0x10u * i);
        const uint32_t cur = ui_lw(s, p + 0xB0u + 4u * i);
        ui_sw(s, p + 0xB0u + 4u * i, ui_fadd(cur, ui_fmul(g, b)));
    }
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t g = ui_lw(s, 0x00810614u + 0x10u * i);
        const uint32_t cur = ui_lw(s, p + 0xB0u + 4u * i);
        ui_sw(s, p + 0xB0u + 4u * i, ui_fadd(cur, ui_fmul(g, a)));
    }
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 002101C0(p): the pool callback of 0020F950's 22 nodes (p[3] = map
 * index 0..10, p[0xD] = 0 or 1). Cases by p[4]:
 *   0  when D_00810CB8[p[3]]: 001CA5E0(p, 001C6120(D_0028A570, the word at
 *      0x2658C0 + p[3] * 8 (+4 when p[0xD] != 0)), 7); p[0xC] =
 *      001C6150(p+0x44 word); above the halfword cap D_00275BCC: p[4] = 3
 *      and return; else p+0x110[n] = 001AF7C0() for n < p[0xC], p[9] =
 *      p[0xC], 001CB5B0(p[0xC]), 001C62C0(p). Then (whatever CB8 says)
 *      p[2] |= 0x40, the +0x80.. colour (0,0,0.5) or (0.5,0.5,0.5), +0x8C =
 *      0, p[4] = 1, +0xC0..C8 = 0, +0x60..68 = k (0.10666667), +0xA0 =
 *      -(0x265600 + p[3] * 8), +0xA8 = (0x265604 + p[3] * 8), +0xA4 = 0,
 *      and on into case 1.
 *   1  the colour by (D_008106CD & 0xF) and D_00810142 against p[3]
 *      (p[0xD] == 1 nodes return unless D_00810142 == p[3]); then with
 *      D_00810143 == 0: +0xC8 = 0, +0x60..68 = k, and when CB8[p[3]]:
 *      00210030(p, 0), 001C6380(p), the call through p+0x4C; otherwise,
 *      for the node of the map D_00810142 (and CB8): p[0xD] == 0 nodes
 *      step the zoom D_00810154 (pad 2 / 8, clamps 2 and 10) and the pans
 *      D_00810158 (0x2000 / 0x8000) and D_0081015C (0x1000 / 0x4000)
 *      against the per-map limits at 0x2657B0 + p[3] * 20; +0x60..68 =
 *      k * zoom, +0xC8 = 0, 00210030(p, D_00810143), 001C6380(p), the
 *      call through p+0x4C. Last, p[0xD] == 0 nodes of the current map
 *      (D_008106CD & 0xF, D_00810143 != 0, area byte not 0x12 / 0x15)
 *      recompute the view centre D_00810170 / D_00810178 from the camera
 *      block 0x810350 / 0x810358, the floor row D_00265890[p[3]] +
 *      D_00810144 * 12 and the zoom and pans (001029C0, 00102BB0,
 *      001026A0 on 0x700036A0 / 0x700038A0).
 *   2, 3 and above: 001AFF90(p). */
int em_area01_ui_002101C0(EmArea01Ui *s, uint32_t p)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, 0x002101C0u, EM_A01R_FAULT_NULL_WORKER, 0);
    UiFrame fr = ui_enter(s, 0x002101C0u, 0x50);
    const uint32_t cam = 0x008102B0u;
    const uint32_t st = ui_lbu(s, p + 4u);
    if (st == 3 || st == 2 || st > 3) {
        (void)ui_call1(s, UI_001AFF90, p);
        return ui_leave(s, fr);
    }
    if (st == 0) {
        const uint32_t i = ui_lbu(s, p + 3u);
        if (ui_lbu(s, UI_D_00810CB8 + i) != 0) {
            const uint32_t id = ui_lw(s, (ui_lbu(s, p + 0xDu) == 0 ? 0x002658C0u : 0x002658C4u) + (i << 3));
            const uint32_t model = ui_call2(s, UI_001C6120, ui_sx(ui_lw(s, 0x0028A570u)), ui_sx(id));
            (void)ui_call3(s, UI_001CA5E0, p, ui_sx(model), 7);
            const uint32_t n = ui_call1(s, UI_001C6150, ui_sx(ui_lw(s, p + 0x44u)));
            ui_sb(s, p + 0xCu, n);
            if (ui_lh(s, UI_D_00275BCC) < (int32_t)ui_lbu(s, p + 0xCu)) {
                ui_sb(s, p + 4u, 3);
                return ui_leave(s, fr);
            }
            uint32_t k = 0, c;
            while ((int32_t)k < (int32_t)(c = ui_lbu(s, p + 0xCu)) && !ui_latched(s)) {
                ui_sw(s, p + 0x110u + 4u * k, ui_call0(s, UI_001AF7C0));
                ++k;
            }
            ui_sb(s, p + 9u, c);
            (void)ui_call1(s, UI_001CB5B0, ui_lbu(s, p + 0xCu));
            (void)ui_call1(s, UI_001C62C0, p);
        }
        ui_sb(s, p + 2u, ui_lbu(s, p + 2u) | 0x40u);
        if (ui_lbu(s, p + 0xDu) == 0) {
            ui_sw(s, p + 0x80u, 0);
            ui_sw(s, p + 0x84u, 0);
            ui_sw(s, p + 0x88u, UI_F_HALF);
        } else {
            ui_sw(s, p + 0x80u, UI_F_HALF);
            ui_sw(s, p + 0x84u, UI_F_HALF);
            ui_sw(s, p + 0x88u, UI_F_HALF);
        }
        ui_sw(s, p + 0x8Cu, 0);
        ui_sb(s, p + 4u, 1);
        ui_sw(s, p + 0xC0u, 0);
        ui_sw(s, p + 0xC4u, 0);
        ui_sw(s, p + 0xC8u, 0);
        ui_sw(s, p + 0x60u, UI_F_K);
        ui_sw(s, p + 0x64u, UI_F_K);
        ui_sw(s, p + 0x68u, UI_F_K);
        ui_sw(s, p + 0xA0u, ui_fneg(ui_lw(s, 0x00265600u + (ui_lbu(s, p + 3u) << 3))));
        ui_sw(s, p + 0xA8u, ui_lw(s, 0x00265604u + (ui_lbu(s, p + 3u) << 3)));
        ui_sw(s, p + 0xA4u, 0);
    }
    /* case 1 */
    if (ui_lbu(s, p + 0xDu) == 0) {
        const uint32_t i = ui_lbu(s, p + 3u);
        const uint32_t sel = ui_lbu(s, 0x00810142u);
        if ((ui_lbu(s, UI_D_008106CD) & 0xFu) == i) {
            const uint32_t c = sel == i ? 0x3F0CCCCDu : 0x3E19999Au;
            ui_sw(s, p + 0x80u, 0);
            ui_sw(s, p + 0x84u, c);
            ui_sw(s, p + 0x88u, c);
        } else {
            ui_sw(s, p + 0x80u, 0);
            ui_sw(s, p + 0x84u, 0);
            ui_sw(s, p + 0x88u, sel == i ? UI_F_TWO : UI_F_HALF);
        }
    } else {
        if (ui_lbu(s, 0x00810142u) != ui_lbu(s, p + 3u)) return ui_leave(s, fr);
        ui_sw(s, p + 0x80u, UI_F_TWO);
        ui_sw(s, p + 0x84u, UI_F_TWO);
        ui_sw(s, p + 0x88u, UI_F_TWO);
    }
    if (ui_lbu(s, 0x00810143u) == 0) {
        ui_sw(s, p + 0xC8u, 0);
        ui_sw(s, p + 0x60u, UI_F_K);
        ui_sw(s, p + 0x64u, UI_F_K);
        ui_sw(s, p + 0x68u, UI_F_K);
        if (ui_lbu(s, UI_D_00810CB8 + ui_lbu(s, p + 3u)) != 0) {
            (void)em_area01_ui_00210030(s, p, 0);
            (void)ui_call1(s, UI_001C6380, p);
            (void)ui_call1(s, ui_lw(s, p + 0x4Cu), p);
        }
    } else {
        const uint32_t i = ui_lbu(s, p + 3u);
        if (ui_lbu(s, UI_D_00810CB8 + i) != 0 && ui_lbu(s, 0x00810142u) == i) {
            if (ui_lbu(s, p + 0xDu) == 0) {
                const uint32_t pad = ui_lhu(s, UI_D_00810E70);
                if (pad & 2u) {
                    const uint32_t f = ui_fsub(ui_lw(s, 0x00810154u), 0x3DCCCCCDu);
                    ui_sw(s, 0x00810154u, f);
                    if (em_ee_c_lt_bits(f, UI_F_TWO)) ui_sw(s, 0x00810154u, UI_F_TWO);
                } else if (pad & 8u) {
                    const uint32_t f = ui_fadd(ui_lw(s, 0x00810154u), 0x3DCCCCCDu);
                    ui_sw(s, 0x00810154u, f);
                    if (!em_ee_c_le_bits(f, 0x41200000u)) ui_sw(s, 0x00810154u, 0x41200000u);
                }
                if (pad & 0x2000u) {
                    ui_sw(s, 0x00810158u, ui_fadd(ui_lw(s, 0x00810158u), UI_F_TWO));
                    const uint32_t v = ui_lw(s, 0x00810158u);
                    const uint32_t l = ui_lw(s, 0x002657B4u + ui_lbu(s, p + 3u) * 20u);
                    if (em_ee_c_lt_bits(l, v)) ui_sw(s, 0x00810158u, l);
                } else if (pad & 0x8000u) {
                    ui_sw(s, 0x00810158u, ui_fsub(ui_lw(s, 0x00810158u), UI_F_TWO));
                    const uint32_t v = ui_lw(s, 0x00810158u);
                    const uint32_t l = ui_lw(s, 0x002657B0u + ui_lbu(s, p + 3u) * 20u);
                    if (!em_ee_c_le_bits(l, v)) ui_sw(s, 0x00810158u, l);
                }
                if (pad & 0x1000u) {
                    ui_sw(s, 0x0081015Cu, ui_fsub(ui_lw(s, 0x0081015Cu), UI_F_TWO));
                    const uint32_t v = ui_lw(s, 0x0081015Cu);
                    const uint32_t l = ui_lw(s, 0x002657B8u + ui_lbu(s, p + 3u) * 20u);
                    if (!em_ee_c_le_bits(l, v)) ui_sw(s, 0x0081015Cu, l);
                } else if (pad & 0x4000u) {
                    ui_sw(s, 0x0081015Cu, ui_fadd(ui_lw(s, 0x0081015Cu), UI_F_TWO));
                    const uint32_t v = ui_lw(s, 0x0081015Cu);
                    const uint32_t l = ui_lw(s, 0x002657BCu + ui_lbu(s, p + 3u) * 20u);
                    if (em_ee_c_lt_bits(l, v)) ui_sw(s, 0x0081015Cu, l);
                }
            }
            ui_sw(s, p + 0x60u, ui_fmul(UI_F_K, ui_lw(s, 0x00810154u)));
            ui_sw(s, p + 0x64u, ui_fmul(UI_F_K, ui_lw(s, 0x00810154u)));
            ui_sw(s, p + 0x68u, ui_fmul(UI_F_K, ui_lw(s, 0x00810154u)));
            ui_sw(s, p + 0xC8u, 0);
            (void)em_area01_ui_00210030(s, p, (int32_t)ui_lbu(s, 0x00810143u));
            (void)ui_call1(s, UI_001C6380, p);
            (void)ui_call1(s, ui_lw(s, p + 0x4Cu), p);
        }
    }
    /* the view centre of the current map */
    if (ui_lbu(s, p + 0xDu) == 1) return ui_leave(s, fr);
    const uint32_t area = ui_lbu(s, UI_D_00810700);
    if (area == 0x12 || area == 0x15) return ui_leave(s, fr);
    const uint32_t i = ui_lbu(s, p + 3u);
    if ((ui_lbu(s, UI_D_008106CD) & 0xFu) != i || ui_lbu(s, 0x00810143u) == 0) return ui_leave(s, fr);
    const uint32_t row = ui_lw(s, 0x00265890u + (i << 2));
    const uint32_t camx = ui_lw(s, cam + 0xA0u);
    ui_sw(s, 0x00810170u, ui_fsub(camx, ui_lw(s, row + ui_lbu(s, 0x00810144u) * 12u)));
    const uint32_t camz = ui_lw(s, cam + 0xA8u);
    ui_sw(s, 0x00810178u, ui_fsub(camz, ui_lw(s, row + ui_lbu(s, 0x00810144u) * 12u + 4u)));
    const uint32_t vx = ui_lw(s, 0x00810170u), vy = ui_lw(s, 0x00810174u), vz = ui_lw(s, 0x00810178u);
    ui_sw(s, 0x700038A0u, vx);
    ui_sw(s, 0x700038A4u, vy);
    ui_sw(s, 0x700038A8u, vz);
    ui_sw(s, 0x700038ACu, UI_F_ONE);
    (void)ui_call1(s, UI_001029C0, 0x700036A0u);
    {
        const uint32_t f12 = ui_fneg(ui_lw(s, row + ui_lbu(s, 0x00810144u) * 12u + 8u));
        uint64_t a[2] = {0x700036A0u, 0x700036A0u};
        ui_call(s, UI_00102BB0, 2, a, 1, &f12, NULL, NULL);
    }
    (void)ui_call3(s, UI_001026A0, 0x700038A0u, 0x700036A0u, 0x700038A0u);
    const uint32_t nx = ui_lw(s, 0x700038A0u), nz = ui_lw(s, 0x700038A8u);
    ui_sw(s, 0x00810170u, nx);
    ui_sw(s, 0x00810178u, nz);
    const uint32_t zoom = ui_lw(s, 0x00810154u);
    const uint32_t half = ui_fdiv(zoom, UI_F_TWO);
    const uint32_t pit = ui_lw(s, 0x00810158u);
    const uint32_t yaw = ui_lw(s, 0x0081015Cu);
    const uint32_t x0 = ui_lw(s, 0x00810170u);
    const uint32_t kz = ui_fmul(UI_F_K, zoom);
    const uint32_t qy = ui_fdiv(yaw, kz);
    const uint32_t qp = ui_fdiv(pit, kz);
    ui_sw(s, 0x00810170u, ui_fadd(x0, ui_fmul(half, ui_fneg(qy))));
    ui_sw(s, 0x00810178u, ui_fadd(ui_lw(s, 0x00810178u), ui_fmul(half, qp)));
    {
        const uint32_t z1 = ui_lw(s, 0x00810154u);
        const uint32_t x1 = ui_lw(s, 0x00810170u);
        ui_sw(s, 0x00810170u, ui_fmul(x1, ui_fmul(UI_F_K, z1)));
    }
    {
        const uint32_t z2 = ui_lw(s, 0x00810154u);
        const uint32_t y2 = ui_lw(s, 0x00810178u);
        ui_sw(s, 0x00810178u, ui_fmul(y2, ui_fmul(0x3DAEC33Eu, z2)));
    }
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 00210F30(t): nothing when the area byte is 0x12 or 0x15. Otherwise
 * (x, z) = t+0x40 / t+0x48; 001029C0 on 0x700036A0 and 0x700036E0; the
 * latter's diagonal 0.8, 1.0, 0.5, +0x3C = 0; 0x70003A20 = (D_00810374 -
 * the floor row's angle) - pi/2, wrapped by 001B1470; 001026D0 and
 * 00102BB0 compose 0x700036A0; 0x700038A0 = (256 + z, 0, 164 - x, 1),
 * 001031E0(0x700036D0, 0x700038A0); the three corners 0x26A990 / 9A0 /
 * 9B0 copied to 0x700038A0 / B0 / C0 and turned by 001026A0; each corner
 * scaled to 16 * (1792 + x) and 16 * (1936 + z / 2); blend 3 and
 * 00208040(1, the three corners, 0x802040A0). */
int em_area01_ui_00210F30(EmArea01Ui *s, uint32_t t)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, 0x00210F30u, EM_A01R_FAULT_NULL_WORKER, 0);
    UiFrame fr = ui_enter(s, 0x00210F30u, 0x20);
    const uint32_t area = ui_lbu(s, UI_D_00810700);
    if (area == 0x12 || area == 0x15) return ui_leave(s, fr);
    const uint32_t fx = ui_lw(s, t + 0x40u), fz = ui_lw(s, t + 0x48u);
    (void)ui_call1(s, UI_001029C0, 0x700036A0u);
    (void)ui_call1(s, UI_001029C0, 0x700036E0u);
    ui_sw(s, 0x700036E0u, 0x3F4CCCCDu);
    ui_sw(s, 0x700036F4u, UI_F_ONE);
    ui_sw(s, 0x70003708u, UI_F_HALF);
    ui_sw(s, 0x7000371Cu, 0);
    const uint32_t row = ui_lw(s, 0x00265890u + (ui_lbu(s, 0x00810142u) << 2)) + ui_lbu(s, 0x00810144u) * 12u;
    uint32_t f12 = ui_fsub(ui_fsub(ui_lw(s, 0x00810374u), ui_lw(s, row + 8u)), 0x3FC90FDBu);
    ui_sw(s, 0x70003A20u, f12);
    uint32_t f0;
    ui_call(s, UI_001B1470, 0, NULL, 1, &f12, NULL, &f0);
    ui_sw(s, 0x70003A20u, f0);
    (void)ui_call3(s, UI_001026D0, 0x700036A0u, 0x700036E0u, 0x700036A0u);
    {
        const uint32_t f = ui_lw(s, 0x70003A20u);
        uint64_t a[2] = {0x700036A0u, 0x700036A0u};
        ui_call(s, UI_00102BB0, 2, a, 1, &f, NULL, NULL);
    }
    ui_sw(s, 0x700038A0u, ui_fadd(0x43800000u, fz));
    ui_sw(s, 0x700038A4u, 0);
    ui_sw(s, 0x700038A8u, ui_fsub(0x43240000u, fx));
    ui_sw(s, 0x700038ACu, UI_F_ONE);
    (void)ui_call2(s, UI_001031E0, 0x700036D0u, 0x700038A0u);
    ui_00102948(s, 0x700038A0u, 0x0026A990u);
    ui_00102948(s, 0x700038B0u, 0x0026A9A0u);
    ui_00102948(s, 0x700038C0u, 0x0026A9B0u);
    (void)ui_call3(s, UI_001026A0, 0x700038A0u, 0x700036A0u, 0x700038A0u);
    (void)ui_call3(s, UI_001026A0, 0x700038B0u, 0x700036A0u, 0x700038B0u);
    (void)ui_call3(s, UI_001026A0, 0x700038C0u, 0x700036A0u, 0x700038C0u);
    const uint32_t k16 = 0x41800000u, k1792 = 0x44E00000u, k1936 = 0x44F20000u;
    const uint32_t ax = ui_lw(s, 0x700038A0u), bx = ui_lw(s, 0x700038B0u), cx = ui_lw(s, 0x700038C0u);
    ui_sw(s, 0x700038A0u, ui_fmul(k16, ui_fadd(k1792, ax)));
    const uint32_t az = ui_lw(s, 0x700038A8u);
    ui_sw(s, 0x700038A8u, ui_fmul(k16, ui_fadd(k1936, ui_fdiv(az, UI_F_TWO))));
    ui_sw(s, 0x700038B0u, ui_fmul(k16, ui_fadd(k1792, bx)));
    const uint32_t bz = ui_lw(s, 0x700038B8u);
    ui_sw(s, 0x700038C0u, ui_fmul(k16, ui_fadd(k1792, cx)));
    const uint32_t cz = ui_lw(s, 0x700038C8u);
    ui_sw(s, 0x700038B8u, ui_fmul(k16, ui_fadd(k1936, ui_fdiv(bz, UI_F_TWO))));
    ui_sw(s, 0x700038C8u, ui_fmul(k16, ui_fadd(k1936, ui_fdiv(cz, UI_F_TWO))));
    blend(s, 3);
    leaf_00208040(s, 1, 0x700038A0u, 0x700038B0u, 0x700038C0u, UINT64_C(0xFFFFFFFF802040A0));
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 00211400(t, a1): nothing unless the scratchpad word 0x70003B64 & 3;
 * blend 0; then with a1 == 0 up to five 00211240(k) calls, k = 0..4, each
 * gated by its own pair of bytes; with a1 != 0 the one marker for t[0x12]
 * (5, 6, 0, 3, 4 map to the same five gates): 0x700038A0..A8 = the float
 * triple of that kind (0x265940 + 12 * slot), 002117D0(t, 0x700038A0,
 * t[0x12], 2 / 2 / 0 / 0 / 3), 00211310(0x700038A0). */
int em_area01_ui_00211400(EmArea01Ui *s, uint32_t t, int32_t a1)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, 0x00211400u, EM_A01R_FAULT_NULL_WORKER, 0);
    UiFrame fr = ui_enter(s, 0x00211400u, 0x30);
    if ((ui_lw(s, UI_SPAD_3B64) & 3u) == 0) return ui_leave(s, fr);
    blend(s, 0);
    /* the five gates */
#define GATE0 (ui_lbu(s, 0x00810C8Du) != 0 && ui_lbu(s, 0x0081076Du) != 0xFFu)
#define GATE1 (ui_lbu(s, 0x00810C8Eu) != 0 && ui_lbu(s, 0x00810770u) != 0xFFu)
#define GATE2 (ui_lbu(s, 0x00810784u) == 1)
#define GATE3 (ui_lbu(s, 0x0081077Fu) == 0xFFu && ui_lbu(s, 0x00810789u) != 0xFFu)
#define GATE4 (ui_lbu(s, 0x00810782u) == 0xFFu && ui_lbu(s, 0x0081078Cu) == 0)
    if (a1 == 0) {
        if (GATE0) (void)ui_call1(s, UI_00211240, 0);
        if (GATE1) (void)ui_call1(s, UI_00211240, 1);
        if (GATE2) (void)ui_call1(s, UI_00211240, 2);
        if (GATE3) (void)ui_call1(s, UI_00211240, 3);
        if (GATE4) (void)ui_call1(s, UI_00211240, 4);
        return ui_leave(s, fr);
    }
    const uint32_t kind = ui_lbu(s, t + 0x12u);
    uint32_t slot, a3;
    int open;
    switch (kind) {
    case 5: open = GATE0; slot = 0; a3 = 2; break;
    case 6: open = GATE1; slot = 1; a3 = 2; break;
    case 0: open = GATE2; slot = 2; a3 = 0; break;
    case 3: open = GATE3; slot = 3; a3 = 0; break;
    case 4: open = GATE4; slot = 4; a3 = 3; break;
    default: return ui_leave(s, fr);
    }
#undef GATE0
#undef GATE1
#undef GATE2
#undef GATE3
#undef GATE4
    if (!open) return ui_leave(s, fr);
    const uint32_t src = 0x00265940u + 12u * slot;
    const uint32_t x = ui_lw(s, src), y = ui_lw(s, src + 4u), z = ui_lw(s, src + 8u);
    ui_sw(s, 0x700038A0u, x);
    ui_sw(s, 0x700038A4u, y);
    ui_sw(s, 0x700038A8u, z);
    (void)ui_call4(s, UI_002117D0, t, 0x700038A0u, ui_lbu(s, t + 0x12u), a3);
    (void)ui_call1(s, UI_00211310, 0x700038A0u);
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 0020F950(t): page 1, by t[3]:
 *   0  22 pool nodes from 001AFF10 (callback 002101C0, p[3] = 0..10, p[0xD]
 *      = 0 for the first eleven, 1 for the rest); t[3] += 1; D_002821B0 =
 *      4, D_002821B4 = 0, D_00282240 = 6; if no map is owned (the eleven
 *      bytes at 0x810CB8, read as D_00810700 + 0x5B8) t[0x12] = 0xFF, else
 *      t[0x12] = D_008106CD & 0xF and, when that map's D_00810CB8 byte is
 *      0, the cursor steps down (wrapping 0 -> 10) until an owned map, at
 *      most eleven steps; t[0x14] = (D_008106CD & 0x30) >> 4; +0x2C = +0x28
 *      = 0; with a pending request D_008106B0: clear it, t[0x12] =
 *      D_008106B1, t[0x13] = 1, +0x24 = the float at 0x2657C0 + t[0x12] *
 *      20, t[3] = 2; else t[0x13] = 0, +0x24 = k.
 *   1  the list view: 00210A00(0), 00210C00(0), blend 3, 001B0000(),
 *      00211400(t, 0), one sprite; Triangle edge (D_00810E74 & 0x20):
 *      0020CD60(), t[0x10] = 0x63, D_002821B4 = 0; else unless t[0x12] ==
 *      0xFF: with t[0x13] == 0, pad repeat 0x2000 / 0x8000 steps the cursor
 *      up / down to the next owned map (0020CDA0 on a move, at most eleven
 *      steps); Cross edge (0x40): on a map with D_00810CB8 set, 0020CD40(),
 *      t[0x13] = 1, +0x24 from 0x2657C0, the marker offset +0x28 / +0x2C
 *      (zero in areas 0x12 / 0x15 or off the current map, else from the
 *      camera block through 001029C0 / 00102BB0 / 001026A0 on 0x70003400 /
 *      0x70003600) and t[3] = 2; with it clear, 0020CD80(); then
 *      D_002821B4 = 1, D_002821B8 = t[0x12].
 *   2  the map view: 00210A00(1), blend 3, 00207D90(1, 0x12, 9, 0x1EF,
 *      0x96), 001B0000(), 00210F30(t) on the current map, 00211400(t, 1),
 *      00207D90(1, 0, 0, 0x200, 0xE0), one sprite, 00210C00(1); Triangle:
 *      0020CD60(), t[0x13] = 0, +0x2C = +0x28 = 0, t[3] = 1; D_002821B4 = 1,
 *      D_002821B8 = t[0x12].
 *   other: nothing. */
static int owned(EmArea01Ui *s, uint32_t i) { return ui_lbu(s, UI_D_00810700 + i + 0x5B8u) != 0; }

int em_area01_ui_0020F950(EmArea01Ui *s, uint32_t t)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, 0x0020F950u, EM_A01R_FAULT_NULL_WORKER, 0);
    UiFrame fr = ui_enter(s, 0x0020F950u, 0x30);
    const uint32_t mode = ui_lbu(s, t + 3u);
    const uint64_t sprite_tex = UINT64_C(0x200429859D322000);
    if (mode == 2) {
        (void)em_area01_ui_00210A00(s, 1);
        blend(s, 3);
        leaf_00207D90(s, 1, 0x12, 9, 0x1EF, 0x96);
        (void)ui_call0(s, UI_001B0000);
        if ((ui_lbu(s, UI_D_008106CD) & 0xFu) == ui_lbu(s, t + 0x12u)) (void)em_area01_ui_00210F30(s, t);
        (void)em_area01_ui_00211400(s, t, 1);
        leaf_00207D90(s, 1, 0, 0, 0x200, 0xE0);
        blit(s, 0x7080, 0x7900, 0x80, 0x40, RGBA_80, sprite_tex);
        (void)em_area01_ui_00210C00(s, 1);
        if (ui_lhu(s, UI_D_00810E74) & 0x20u) {
            (void)ui_call0(s, UI_0020CD60);
            ui_sb(s, t + 0x13u, 0);
            ui_sw(s, t + 0x2Cu, 0);
            ui_sw(s, t + 0x28u, 0);
            ui_sb(s, t + 3u, 1);
        }
        ui_sw(s, UI_D_002821B4, 1);
        ui_sw(s, UI_D_002821B8, ui_lbu(s, t + 0x12u));
        return ui_leave(s, fr);
    }
    if (mode == 1) {
        (void)em_area01_ui_00210A00(s, 0);
        (void)em_area01_ui_00210C00(s, 0);
        blend(s, 3);
        (void)ui_call0(s, UI_001B0000);
        (void)em_area01_ui_00211400(s, t, 0);
        blit(s, 0x7080, 0x7900, 0x80, 0x40, RGBA_80, sprite_tex);
        if (ui_lhu(s, UI_D_00810E74) & 0x20u) {
            (void)ui_call0(s, UI_0020CD60);
            ui_sb(s, t + 0x10u, 0x63);
            ui_sw(s, UI_D_002821B4, 0);
            return ui_leave(s, fr);
        }
        if (ui_lbu(s, t + 0x12u) == 0xFFu) return ui_leave(s, fr);
        if (ui_lbu(s, t + 0x13u) == 0) {
            const uint32_t rep = ui_lhu(s, UI_D_00810E78);
            if (rep & 0x2000u) {
                for (int n = 0; n < 11 && !ui_latched(s);) {
                    ui_sb(s, t + 0x12u, ui_lbu(s, t + 0x12u) + 1u);
                    if ((int32_t)ui_lbu(s, t + 0x12u) >= 11) ui_sb(s, t + 0x12u, 0);
                    if (owned(s, ui_lbu(s, t + 0x12u))) {
                        (void)ui_call0(s, UI_0020CDA0);
                        break;
                    }
                    ++n;
                }
            } else if (rep & 0x8000u) {
                for (int n = 0; n < 11 && !ui_latched(s);) {
                    const uint32_t c = ui_lbu(s, t + 0x12u);
                    ui_sb(s, t + 0x12u, c != 0 ? c - 1u : 0xAu);
                    if (owned(s, ui_lbu(s, t + 0x12u))) {
                        (void)ui_call0(s, UI_0020CDA0);
                        break;
                    }
                    ++n;
                }
            }
        }
        if (ui_lhu(s, UI_D_00810E74) & 0x40u) {
            if (ui_lbu(s, UI_D_00810CB8 + ui_lbu(s, t + 0x12u)) != 0) {
                (void)ui_call0(s, UI_0020CD40);
                ui_sb(s, t + 0x13u, 1);
                const uint32_t idx = ui_lbu(s, t + 0x12u);
                ui_sw(s, t + 0x24u, ui_lw(s, 0x002657C0u + idx * 20u));
                const uint32_t area = ui_lbu(s, UI_D_00810700);
                if (area == 0x12 || area == 0x15) {
                    ui_sw(s, t + 0x28u, 0);
                    ui_sw(s, t + 0x2Cu, 0);
                } else if ((ui_lbu(s, UI_D_008106CD) & 0xFu) == ui_lbu(s, t + 0x12u)) {
                    const uint32_t cur = ui_lbu(s, t + 0x12u);
                    const uint32_t base = ui_lw(s, 0x00265890u + (cur << 2));
                    const uint32_t row = base + ui_lbu(s, t + 0x14u) * 12u;
                    const uint32_t cx = ui_lw(s, 0x00810350u), rx = ui_lw(s, row);
                    const uint32_t cz = ui_lw(s, 0x00810358u), rz = ui_lw(s, row + 4u);
                    ui_sw(s, 0x70003600u, ui_fsub(cx, rx));
                    ui_sw(s, 0x70003604u, 0);
                    ui_sw(s, 0x70003608u, ui_fsub(cz, rz));
                    ui_sw(s, 0x7000360Cu, UI_F_ONE);
                    (void)ui_call1(s, UI_001029C0, 0x70003400u);
                    {
                        const uint32_t f = ui_fneg(ui_lw(s, base + ui_lbu(s, t + 0x14u) * 12u + 8u));
                        uint64_t a[2] = {0x70003400u, 0x70003400u};
                        ui_call(s, UI_00102BB0, 2, a, 1, &f, NULL, NULL);
                    }
                    (void)ui_call3(s, UI_001026A0, 0x70003600u, 0x70003400u, 0x70003600u);
                    const uint32_t z = ui_lw(s, 0x70003608u), x = ui_lw(s, 0x70003600u);
                    const uint32_t mz = ui_fmul(UI_F_K, ui_fneg(z));
                    const uint32_t mx = ui_fmul(UI_F_K, x);
                    ui_sw(s, t + 0x2Cu, ui_fmul(UI_F_TWO, mx));
                    ui_sw(s, t + 0x28u, ui_fmul(UI_F_TWO, mz));
                } else {
                    ui_sw(s, t + 0x28u, 0);
                    ui_sw(s, t + 0x2Cu, 0);
                }
                ui_sb(s, t + 3u, 2);
            } else {
                (void)ui_call0(s, UI_0020CD80);
            }
        }
        ui_sw(s, UI_D_002821B4, 1);
        ui_sw(s, UI_D_002821B8, ui_lbu(s, t + 0x12u));
        return ui_leave(s, fr);
    }
    if (mode != 0) return ui_leave(s, fr);
    for (uint32_t pass = 0; pass < 2; ++pass)
        for (uint32_t i = 0; i < 11 && !ui_latched(s); ++i) {
            const uint32_t p = ui_call0(s, UI_001AFF10);
            ui_sw(s, p + 0x10u, 0x002101C0u);
            ui_sb(s, p + 3u, i);
            ui_sb(s, p + 0xDu, pass);
        }
    ui_sb(s, t + 3u, ui_lbu(s, t + 3u) + 1u);
    ui_sw(s, UI_D_002821B0, 4);
    ui_sw(s, UI_D_002821B4, 0);
    ui_sw(s, UI_D_00282240, 6);
    uint32_t total = 0;
    for (uint32_t i = 0; i < 11; ++i) total += ui_lbu(s, UI_D_00810700 + 0x5B8u + i);
    if (total == 0) {
        ui_sb(s, t + 0x12u, 0xFF);
    } else {
        ui_sb(s, t + 0x12u, ui_lbu(s, UI_D_008106CD) & 0xFu);
        if (ui_lbu(s, UI_D_00810CB8 + ui_lbu(s, t + 0x12u)) == 0) {
            for (int n = 0; n < 11 && !ui_latched(s); ++n) {
                const uint32_t c = ui_lbu(s, t + 0x12u);
                ui_sb(s, t + 0x12u, c != 0 ? c - 1u : 0xAu);
                if (owned(s, ui_lbu(s, t + 0x12u))) break;
            }
        }
    }
    ui_sb(s, t + 0x14u, (ui_lbu(s, UI_D_008106CD) & 0x30u) >> 4);
    ui_sw(s, t + 0x2Cu, 0);
    ui_sw(s, t + 0x28u, 0);
    if (ui_lbu(s, UI_D_008106B0) != 0) {
        ui_sb(s, UI_D_008106B0, 0);
        ui_sb(s, t + 0x12u, ui_lbu(s, UI_D_008106B1));
        ui_sb(s, t + 0x13u, 1);
        ui_sw(s, t + 0x24u, ui_lw(s, 0x002657C0u + ui_lbu(s, t + 0x12u) * 20u));
        ui_sb(s, t + 3u, 2);
    } else {
        ui_sb(s, t + 0x13u, 0);
        ui_sw(s, t + 0x24u, UI_F_K);
    }
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 00213F30(p, lo, hi): p[0x18] = 0; the category p[0x12] 0..4 picks the id
 * range [0,0x20) [0x20,0x32) [0x32,0x48) [0x48,0x5D) [0x5D,0x6D) (other
 * values keep the caller's lo / hi); each id appends itself when its byte
 * D_00810700[id + 0x5C3] is set, else 0x6D, at p + 0x50 + p[0x18]++;
 * p[0x19] = 0. `lo_hi_known` 0 (00214020's calls, whose lo / hi registers
 * the original does not set) faults when p[0x12] is not 0..4. */
static void list_ids(EmArea01Ui *s, uint32_t p, int32_t lo, int32_t hi, int lo_hi_known)
{
    UiFrame fr = ui_enter(s, 0x00213F30u, 0);
    ui_sb(s, p + 0x18u, 0);
    switch (ui_lbu(s, p + 0x12u)) {
    case 0: lo = 0; hi = 0x20; break;
    case 1: lo = 0x20; hi = 0x32; break;
    case 2: lo = 0x32; hi = 0x48; break;
    case 3: lo = 0x48; hi = 0x5D; break;
    case 4: lo = 0x5D; hi = 0x6D; break;
    default:
        if (!lo_hi_known) ui_unmeasured(s, p + 0x12u);
        break;
    }
    for (int32_t i = lo; i < hi && !ui_latched(s); ++i) {
        const uint32_t v = ui_lbu(s, UI_D_00810700 + (uint32_t)i + 0x5C3u) != 0 ? (uint32_t)i : 0x6Du;
        const uint32_t n = ui_lbu(s, p + 0x18u);
        ui_sb(s, p + 0x18u, n + 1u);
        ui_sb(s, p + n + 0x50u, v);
    }
    ui_sb(s, p + 0x19u, 0);
    (void)ui_leave(s, fr);
}

int em_area01_ui_00213F30(EmArea01Ui *s, uint32_t p, int32_t lo, int32_t hi)
{
    if (ui_latched(s)) return -1;
    list_ids(s, p, lo, hi, 1);
    return ui_latched(s) ? -1 : 0;
}

/* 002131B0(p, a1): blend 0; four frame sprites (one set for a1 != 0,
 * another for a1 == 0); blend 3; two sprites; then a1 != 0 draws one more,
 * a1 == 0 calls 001FCF30(p[0x12], 0x64, 0x2F). */
int em_area01_ui_002131B0(EmArea01Ui *s, uint32_t p, int32_t a1)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, 0x002131B0u, EM_A01R_FAULT_NULL_WORKER, 0);
    UiFrame fr = ui_enter(s, 0x002131B0u, 0x30);
    blend(s, 0);
    if (a1 != 0) {
        blit(s, 0x7000, 0x7BA0, 0x100, 0x80, RGBA_40, UINT64_C(0x20044A05E1321D00));
        blit(s, 0x7000, 0x7FA0, 0x100, 0x80, RGBA_40, UINT64_C(0x20044C05E1321E00));
        blit(s, 0x8000, 0x7BA0, 0x100, 0x80, RGBA_40, UINT64_C(0x20044A85E1321D40));
        blit(s, 0x8000, 0x7FA0, 0x100, 0x80, RGBA_40, UINT64_C(0x20044C85E1321E40));
    } else {
        blit(s, 0x7000, 0x7BA0, 0x100, 0x80, RGBA_40, UINT64_C(0x20044E85E1321F00));
        blit(s, 0x7000, 0x7FA0, 0x100, 0x80, RGBA_40, UINT64_C(0x20044385E1322000));
        blit(s, 0x8000, 0x7BA0, 0x100, 0x80, RGBA_40, UINT64_C(0x20044305E1321F40));
        blit(s, 0x8000, 0x7FA0, 0x100, 0x80, RGBA_40, UINT64_C(0x20044505E1322040));
    }
    blend(s, 3);
    blit(s, 0x7000, 0x8300, 0x80, 0x80, RGBA_80, UINT64_C(0x20045005DD4221E0));
    blit(s, 0x7100, 0x7900, 0x100, 0x40, RGBA_80, UINT64_C(0x20044785A1322180));
    if (a1 != 0)
        blit(s, 0x8400, 0x7A80, 0x80, 0x40, RGBA_80, UINT64_C(0x20044E059D3221C0));
    else
        (void)ui_call3(s, UI_001FCF30, ui_lbu(s, p + 0x12u), 0x64, 0x2F);
    return ui_leave(s, fr);
}

/* 002134C0(p, a1): blend 3; a1 == 0: a left / right pair by D_00810E70
 * bits 0x2000 then 0x8000 and an up / down pair by 0x1000 then 0x4000
 * (the halfword read again for the second pair); a1 != 0: the page
 * numbers p+0x1C + 1 and D_002659C0[p[0x1B]] (001C5FB0(n, 2, 1), copied
 * by 00123168 into the frame's word at sp + 0x3C, drawn by 001CBA50(1,
 * 0x870 / 0x898, 0x7AC, 0x10, 0x10, that word, D_00275870)) and, when the
 * page count is 2 or more, a next arrow (p+0x1C < count - 1) and a
 * previous arrow (p+0x1C > 0) whose texture flips with the scratchpad
 * counter bit 0x70003B64 & 4. */
int em_area01_ui_002134C0(EmArea01Ui *s, uint32_t p, int32_t a1)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, 0x002134C0u, EM_A01R_FAULT_NULL_WORKER, 0);
    UiFrame fr = ui_enter(s, 0x002134C0u, 0x40);
    blend(s, 3);
    if (a1 == 0) {
        uint32_t pad = ui_lhu(s, UI_D_00810E70);
        uint64_t l = UINT64_C(0x2004498559322220), r = UINT64_C(0x2004490559322210);
        if (pad & 0x2000u) {
            l = UINT64_C(0x20044B8559322240);
        } else if (pad & 0x8000u) {
            r = UINT64_C(0x20044B0559322230);
        }
        blit(s, 0x8A00, 0x7BC0, 0x40, 0x20, RGBA_80, l);
        blit(s, 0x7200, 0x7BC0, 0x40, 0x20, RGBA_80, r);
        pad = ui_lhu(s, UI_D_00810E70);
        uint64_t u = UINT64_C(0x2004502555422278), d = UINT64_C(0x200450255542227C);
        if (pad & 0x1000u) {
            u = UINT64_C(0x200450855542227A);
        } else if (pad & 0x4000u) {
            d = UINT64_C(0x200450855542227E);
        }
        blit(s, 0x7F80, 0x7A80, 0x20, 0x20, RGBA_80, u);
        blit(s, 0x7F80, 0x83C0, 0x20, 0x20, RGBA_80, d);
        return ui_leave(s, fr);
    }
    const uint32_t buf = s->sp + 0x3Cu;
    for (int k = 0; k < 2; ++k) {
        const int32_t n = k == 0 ? ui_lh(s, p + 0x1Cu) + 1 : (int32_t)ui_lw(s, 0x002659C0u + (ui_lbu(s, p + 0x1Bu) << 2));
        const uint32_t str = ui_call3(s, UI_001C5FB0, ui_sx((uint32_t)n), 2, 1);
        (void)ui_call2(s, UI_00123168, buf, ui_sx(str));
        uint64_t a[7] = {1, k == 0 ? 0x870u : 0x898u, 0x7AC, 0x10, 0x10, buf, UI_D_00275870};
        ui_call(s, UI_001CBA50, 7, a, 0, NULL, NULL, NULL);
    }
    const int32_t count = (int32_t)ui_lw(s, 0x002659C0u + (ui_lbu(s, p + 0x1Bu) << 2));
    if (count < 2) return ui_leave(s, fr);
    if (ui_lh(s, p + 0x1Cu) < count - 1) {
        const int blink = (ui_lw(s, UI_SPAD_3B64) & 4u) != 0;
        blit(s, 0x8D00, 0x7F40, 0x20, 0x20, RGBA_80,
             blink ? UINT64_C(0x2004502555422268) : UINT64_C(0x200450855542226A));
    }
    if (ui_lh(s, p + 0x1Cu) > 0) {
        const int blink = (ui_lw(s, UI_SPAD_3B64) & 4u) != 0;
        blit(s, 0x7100, 0x7F40, 0x20, 0x20, RGBA_80,
             blink ? UINT64_C(0x200450255542226C) : UINT64_C(0x200450855542226E));
    }
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 00214020(t): page 3, by t[3]:
 *   0  t[0x17] = t[0x19] = t[0x1A] = 0, halfword +0x1E = 0, D_002821B0 =
 *      4, D_002821B4 = 0, D_00282240 = 0x64; with a pending request
 *      D_008106B0: t[0x1B] = D_008106B1, t[0x12] = its category (< 0x20,
 *      < 0x32, < 0x48, < 0x5D, else: 0..4), 00213F30(t); the first list
 *      slot holding that id sets t[0x19] = slot - 4 (plus the list length
 *      when that is negative as a signed byte), t[0x17] = 4 and D_00282240
 *      = 0x64 again; D_00282244 = 0, D_002821B8 = t[0x1B], +0x1C = 0, t[3]
 *      = 4, request cleared. Without one: t[3] += 1, t[0x12] = 0,
 *      00213F30(t), and on into 1.
 *   1  Triangle (D_00810E74 & 0x20): 0020CD60(), t[0x10] = 0x63,
 *      D_002821B4 = 0. Else 0020A7A0(0x200450A59D422200); repeat 0x2000 /
 *      0x8000 steps the category up / down (wrapping 0..4) with
 *      001FB9F0(0xF, 0x1000, 0x1000, 0x1000) and 00213F30(t);
 *      002131B0(t, 0), 002134C0(t, 0); 00213A00(t, 0) != 0: +0x1C = 0,
 *      t[3] += 1, 00213C50(t, t[0x1A]); else on Cross (0x40): t[0x1B] =
 *      t[0x50 + 0020BEF0(t)]; an id (not 0x6D): t[3] = 4, 0020CD40(),
 *      D_00282244 = 0, D_002821B8 = the id, +0x1C = 0; the blank 0x6D:
 *      0020CD80(), t[3] = 3, t[4] = 8.
 *   2  0020A7A0, 002131B0(t, 0), 002134C0(t, 0); 00213CC0(t) != 0: t[3] -= 1.
 *   3  0020A7A0, 002131B0(t, 0), 002134C0(t, 0), 00213A00(t, 0x400);
 *      t[4] -= 1 and at 0: t[3] = 1, request cleared.
 *   4  the page: 0020A7A0, 002131B0(t, 1), 002134C0(t, 1), D_002821B4 = 1;
 *      with D_002659C0[t[0x1B]] >= 2 pages, repeat 0x2000 turns forward
 *      (0020CDA0; at the last page +0x1C is set to count - 1 without a
 *      cue), 0x8000 back (not below 0, cue after the store); D_00282244 =
 *      +0x1C; Triangle or Cross (0x60): D_002821B4 = 0, 0020CD40(), t[3] = 1. */
int em_area01_ui_00214020(EmArea01Ui *s, uint32_t t)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, 0x00214020u, EM_A01R_FAULT_NULL_WORKER, 0);
    UiFrame fr = ui_enter(s, 0x00214020u, 0x20);
    const uint64_t bg = UINT64_C(0x200450A59D422200);
    const uint32_t st = ui_lbu(s, t + 3u);
    if (st == 4) {
        (void)ui_call1(s, UI_0020A7A0, bg);
        (void)em_area01_ui_002131B0(s, t, 1);
        (void)em_area01_ui_002134C0(s, t, 1);
        ui_sw(s, UI_D_002821B4, 1);
        const uint32_t tbl = 0x002659C0u + (ui_lbu(s, t + 0x1Bu) << 2);
        const int32_t count = (int32_t)ui_lw(s, tbl);
        if (count >= 2) {
            const uint32_t rep = ui_lhu(s, UI_D_00810E78);
            if (rep & 0x2000u) {
                if (!(ui_lh(s, t + 0x1Cu) < count - 1)) {
                    ui_sh(s, t + 0x1Cu, (uint32_t)(ui_lh(s, tbl) - 1));
                } else {
                    (void)ui_call0(s, UI_0020CDA0);
                    ui_sh(s, t + 0x1Cu, (uint32_t)(ui_lh(s, t + 0x1Cu) + 1));
                }
            } else if (rep & 0x8000u) {
                const int32_t cur = ui_lh(s, t + 0x1Cu);
                if (cur != 0) {
                    ui_sh(s, t + 0x1Cu, (uint32_t)(cur - 1));
                    (void)ui_call0(s, UI_0020CDA0);
                }
            }
        }
        const uint32_t edge = ui_lhu(s, UI_D_00810E74);
        ui_sw(s, UI_D_00282244, (uint32_t)ui_lh(s, t + 0x1Cu));
        if (edge & 0x60u) {
            ui_sw(s, UI_D_002821B4, 0);
            (void)ui_call0(s, UI_0020CD40);
            ui_sb(s, t + 3u, 1);
        }
        return ui_leave(s, fr);
    }
    if (st == 3) {
        (void)ui_call1(s, UI_0020A7A0, bg);
        (void)em_area01_ui_002131B0(s, t, 0);
        (void)em_area01_ui_002134C0(s, t, 0);
        (void)ui_call2(s, UI_00213A00, t, 0x400);
        const uint32_t c = (ui_lbu(s, t + 4u) - 1u) & 0xFFu;
        ui_sb(s, t + 4u, c);
        if (c == 0) {
            ui_sb(s, UI_D_008106B0, 0);
            ui_sb(s, t + 3u, 1);
        }
        return ui_leave(s, fr);
    }
    if (st == 2) {
        (void)ui_call1(s, UI_0020A7A0, bg);
        (void)em_area01_ui_002131B0(s, t, 0);
        (void)em_area01_ui_002134C0(s, t, 0);
        if (ui_call1(s, UI_00213CC0, t) != 0) ui_sb(s, t + 3u, ui_lbu(s, t + 3u) - 1u);
        return ui_leave(s, fr);
    }
    if (st != 1 && st != 0) return ui_leave(s, fr);
    if (st == 0) {
        ui_sb(s, t + 0x17u, 0);
        ui_sb(s, t + 0x19u, 0);
        ui_sb(s, t + 0x1Au, 0);
        ui_sh(s, t + 0x1Eu, 0);
        ui_sw(s, UI_D_002821B0, 4);
        ui_sw(s, UI_D_002821B4, 0);
        const uint32_t req = ui_lbu(s, UI_D_008106B0);
        ui_sw(s, UI_D_00282240, 0x64);
        if (req != 0) {
            ui_sb(s, t + 0x1Bu, ui_lbu(s, UI_D_008106B1));
            const uint32_t id = ui_lbu(s, t + 0x1Bu);
            ui_sb(s, t + 0x12u, id < 0x20 ? 0 : id < 0x32 ? 1 : id < 0x48 ? 2 : id < 0x5D ? 3 : 4);
            list_ids(s, t, 2, 3, 0);
            const int32_t n = (int32_t)ui_lbu(s, t + 0x18u);
            for (int32_t i = 0; i < n && !ui_latched(s); ++i) {
                if (ui_lbu(s, t + (uint32_t)i + 0x50u) != ui_lbu(s, t + 0x1Bu)) continue;
                ui_sb(s, t + 0x19u, (uint32_t)(i - 4));
                const uint32_t v = ui_lbu(s, t + 0x19u);
                if ((int8_t)v < 0) ui_sb(s, t + 0x19u, ui_lbu(s, t + 0x18u) + v);
                ui_sb(s, t + 0x17u, 4);
                ui_sw(s, UI_D_00282240, 0x64);
                break;
            }
            ui_sw(s, UI_D_00282244, 0);
            ui_sw(s, UI_D_002821B8, ui_lbu(s, t + 0x1Bu));
            ui_sh(s, t + 0x1Cu, 0);
            ui_sb(s, t + 3u, 4);
            ui_sb(s, UI_D_008106B0, 0);
            return ui_leave(s, fr);
        }
        ui_sb(s, t + 3u, ui_lbu(s, t + 3u) + 1u);
        ui_sb(s, t + 0x12u, 0);
        list_ids(s, t, 2, 3, 0);
    }
    /* state 1 */
    if (ui_lhu(s, UI_D_00810E74) & 0x20u) {
        (void)ui_call0(s, UI_0020CD60);
        ui_sb(s, t + 0x10u, 0x63);
        ui_sw(s, UI_D_002821B4, 0);
        return ui_leave(s, fr);
    }
    (void)ui_call1(s, UI_0020A7A0, bg);
    const uint32_t rep = ui_lhu(s, UI_D_00810E78);
    if (rep & 0x2000u) {
        ui_sb(s, t + 0x12u, ui_lbu(s, t + 0x12u) + 1u);
        if (!((int32_t)ui_lbu(s, t + 0x12u) < 5)) ui_sb(s, t + 0x12u, 0);
        (void)ui_call4(s, UI_001FB9F0, 0xF, 0x1000, 0x1000, 0x1000);
        list_ids(s, t, 0, 0, 0);
    } else if (rep & 0x8000u) {
        ui_sb(s, t + 0x12u, ui_lbu(s, t + 0x12u) - 1u);
        if (ui_lb(s, t + 0x12u) < 0) ui_sb(s, t + 0x12u, 4);
        (void)ui_call4(s, UI_001FB9F0, 0xF, 0x1000, 0x1000, 0x1000);
        list_ids(s, t, 0, 0, 0);
    }
    (void)em_area01_ui_002131B0(s, t, 0);
    (void)em_area01_ui_002134C0(s, t, 0);
    if (ui_call2(s, UI_00213A00, t, 0) != 0) {
        ui_sh(s, t + 0x1Cu, 0);
        ui_sb(s, t + 3u, ui_lbu(s, t + 3u) + 1u);
        (void)ui_call2(s, UI_00213C50, t, ui_lbu(s, t + 0x1Au));
        return ui_leave(s, fr);
    }
    if (ui_lhu(s, UI_D_00810E74) & 0x40u) {
        const uint32_t k = ui_call1(s, UI_0020BEF0, t);
        ui_sb(s, t + 0x1Bu, ui_lbu(s, k + t + 0x50u));
        if (ui_lbu(s, t + 0x1Bu) != 0x6Du) {
            ui_sb(s, t + 3u, 4);
            (void)ui_call0(s, UI_0020CD40);
            ui_sw(s, UI_D_00282244, 0);
            ui_sw(s, UI_D_002821B8, ui_lbu(s, t + 0x1Bu));
            ui_sh(s, t + 0x1Cu, 0);
        } else {
            (void)ui_call0(s, UI_0020CD80);
            ui_sb(s, t + 3u, 3);
            ui_sb(s, t + 4u, 8);
        }
    }
    return ui_leave(s, fr);
}
