/* Status pages lane: the ITEM root's child pages EQUIPMENT 00214570,
 * EVENT 00215870 and HEALING 002160B0 (0020EE50 states 4, 6, 7; BATTERY
 * 002149F0 is em_status_page_record's em_spr_002149F0), HEALING's list
 * builder 00215FE0 and the shared list helpers 0020BBE0 / 0020BC50. Hand
 * translation of the original functions (boot ELF SCUS-97112);
 * docs/STATUS_PAGES.md. */
#include "game/em_status_pages_internal.h"

/* 64-bit texture / background words as the originals build them. */
#define EQ_BG SP_TEX(0x20042585u, 0x9D422100u)
#define EQ_LIST SP_TEX(0x20042605u, 0xA1321F80u)
#define EV_BG SP_TEX(0x200453A5u, 0x9D422250u)
#define EV_LIST SP_TEX(0x20044D05u, 0xA1321F80u)
#define HL_BG SP_TEX(0x200434C5u, 0x9D422150u)
#define HL_LIST SP_TEX(0x20042D05u, 0xA1321F80u)

/* The page draws every list page shares: 0020A7A0(bg), 0020AE40(t, frame,
 * flags) and (for `list`) 0020B210(t, rows, tex, lflags) whose result is
 * returned. */
static void frame(EmArea01Ui *s, uint32_t t, uint64_t bg, uint32_t tbl, uint32_t flags)
{
    (void)ui_call1(s, SP_0020A7A0, bg);
    (void)ui_call3(s, SP_0020AE40, t, tbl, flags);
}

static uint32_t list(EmArea01Ui *s, uint32_t t, uint32_t rows, uint64_t tex, uint32_t flags)
{
    return ui_call4(s, SP_0020B210, t, rows, tex, flags);
}

static void arrows(EmArea01Ui *s, uint32_t t, uint32_t tbl) { (void)ui_call2(s, SP_0020B0D0, t, tbl); }

/* Append kind k at t+0x50 + t[0x18]++ (the counter read, stored + 1, then
 * the entry stored). */
static void append(EmArea01Ui *s, uint32_t t, uint32_t k)
{
    const uint32_t n = ui_lbu(s, t + 0x18u);
    ui_sb(s, t + 0x18u, n + 1u);
    ui_sb(s, n + t + 0x50u, k);
}

/* The back-out of a list page to the ITEM root: cue 0020CD60, D_002821B4 =
 * 2, t[1] = 3 and t[2..5] = 0 (0020CDC0 reloads module 0x1F). */
static void back_to_root(EmArea01Ui *s, uint32_t t)
{
    (void)ui_call0(s, SP_0020CD60);
    ui_sw(s, SP_B4, 2);
    ui_sb(s, t + 1u, 3);
    ui_sb(s, t + 2u, 0);
    ui_sb(s, t + 3u, 0);
    ui_sb(s, t + 4u, 0);
    ui_sb(s, t + 5u, 0);
}

/* The index i of a list entry as page (i >> 2) * 4 = t[0x19] and row
 * i % 4 = t[0x17] (i >= 0 here). */
static void locate(EmArea01Ui *s, uint32_t t, int32_t i)
{
    ui_sb(s, t + 0x19u, (uint32_t)((i >> 2) << 2));
    ui_sb(s, t + 0x17u, (uint32_t)(i & 3));
}

/* ------------------------------------------------------------------ */
/* 0020BBE0(t, n): the 5-row window t+0x90.. from the ring t+0x50..
 * (length t[0x18]) at t[0x19], one entry earlier when n == 1. */
int em_status_pages_0020BBE0(EmArea01Ui *s, uint32_t t, int32_t n)
{
    if (ui_latched(s)) return -1;
    UiFrame fr = ui_enter(s, 0x0020BBE0u, 0);
    int32_t idx = (int32_t)ui_lbu(s, t + 0x19u);
    if (n == 1) idx = idx != 0 ? idx - 1 : (int32_t)ui_lbu(s, t + 0x18u) - 1;
    for (uint32_t i = 0; i < 5; ++i) {
        ui_sb(s, t + i + 0x90u, ui_lbu(s, t + (uint32_t)idx + 0x50u));
        ++idx;
        if (!(idx < (int32_t)ui_lbu(s, t + 0x18u))) idx = 0;
    }
    return ui_leave(s, fr);
}

/* 0020BC50(t, rows, tex, flags): one frame of the list scroll. Blend 0;
 * 00207D90(1, 0x7F, 0x34, 0x17F, 0x92); the direction d = +4 (row offset
 * -0x30) when t[0x1A] == 1, else -4 (offset 0); the 5 window rows k: two
 * sprites at x 0x77F0 / 0x7FF0, y = float_to_int(16 * (((t+0x1C + offset
 * + 0x6A + 0x30 k) >> 1) + 0x790)) (the halfword re-read for each), 0x80
 * x 0x40, colour 0x40404040, textures the doublewords rows + 24 * id and
 * + 8 (id = t[0x90 + k], read after the conversion); 00207D90(1, 0, 0,
 * 0x200, 0xE0); t+0x1C += d; d > 0: at t+0x1C >= 0x30 t[0x19] steps back
 * (0 wraps to t[0x18] - 1) and the result is 1; d < 0: below -0x2F t[0x19]
 * steps forward (t[0x18] - 1 or more wraps to 0), result 1; last the
 * cursor bar at y = float_to_int(16 * (((t[0x17] * 48 + 0x6A) >> 1) +
 * 0x790)), 0x100 x 0x40, colour 0x20808080, texture `tex`. `flags` is not
 * used. */
int em_status_pages_0020BC50(EmArea01Ui *s, uint32_t t, uint32_t rows, uint64_t tex, int32_t flags, uint32_t *v0)
{
    (void)flags;
    if (sp_begin(s, 0x0020BC50u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x0020BC50u, 0xB0);
    uint32_t done = 0;
    sp_blend(s, 0);
    {
        uint64_t a[5] = {1, 0x7F, 0x34, 0x17F, 0x92};
        ui_call(s, SP_00207D90, 5, a, 0, NULL, NULL, NULL);
    }
    const int32_t d = ui_lbu(s, t + 0x1Au) == 1 ? 4 : -4;
    const int32_t off = d == 4 ? -0x30 : 0;
    for (uint32_t k = 0; k < 5; ++k) {
        const int32_t base = off + 0x6A + 0x30 * (int32_t)k;
        for (uint32_t half = 0; half < 2; ++half) {
            const uint64_t y = sp_f2i(s, sp_x16(((ui_lh(s, t + 0x1Cu) + base) >> 1) + 0x790));
            const uint32_t id = ui_lbu(s, t + k + 0x90u);
            const uint32_t e = rows + id * 24u + 8u * half;
            const uint64_t tx = (uint64_t)ui_lw(s, e) | (uint64_t)ui_lw(s, e + 4u) << 32;
            sp_blit(s, half ? 0x7FF0u : 0x77F0u, y, 0x80, 0x40, UINT64_C(0x40404040), tx);
        }
    }
    {
        uint64_t a[5] = {1, 0, 0, 0x200, 0xE0};
        ui_call(s, SP_00207D90, 5, a, 0, NULL, NULL, NULL);
    }
    ui_sh(s, t + 0x1Cu, (uint32_t)(ui_lh(s, t + 0x1Cu) + d));
    if (d < 0) {
        if (ui_lh(s, t + 0x1Cu) < -0x2F) {
            const int32_t n = (int32_t)ui_lbu(s, t + 0x18u);
            const int32_t c = (int32_t)ui_lbu(s, t + 0x19u);
            ui_sb(s, t + 0x19u, c < n - 1 ? (uint32_t)(c + 1) : 0u);
            done = 1;
        }
    } else if (!(ui_lh(s, t + 0x1Cu) < 0x30)) {
        const uint32_t c = ui_lbu(s, t + 0x19u);
        ui_sb(s, t + 0x19u, c != 0 ? c - 1u : ui_lbu(s, t + 0x18u) - 1u);
        done = 1;
    }
    const uint64_t y = sp_f2i(s, sp_x16((((int32_t)ui_lbu(s, t + 0x17u) * 48 + 0x6A) >> 1) + 0x790));
    sp_blit(s, 0x77F0, y, 0x100, 0x40, UINT64_C(0x20808080), tex);
    if (v0) *v0 = done;
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 00214570(t): EQUIPMENT (entry base t+0x1E = 0x17). State t[5]:
 *   0: cursor bytes cleared, D_002821B0 = 4, D_002821B4 = 0, D_00282240 =
 *      3; the list: 1 if D_00810C7C, else 0 if D_00810C7B; 2 if D_00810C7D;
 *      3 if D_00810C7E. A pending request D_008106B0 (a take) selects the
 *      entry B1 - 0x17 (t[0x1B]; cursor t[0x17] = its index, D_00282240 =
 *      4, and for item 0x19 a D_00810C60 of 1 becomes 2), clears the
 *      request and shows the notice (t[5] = 3, t[6] = 0xF0). Else t[5] =
 *      1, t[6] = 0 and on into 1.
 *   1: Triangle (0x20) backs out to the ITEM root. Else a debounce t[6]:
 *      at 0, Cross (0x40) buzzes (0020CD80) and sets 15; else t[6] -= 1.
 *      Frame (0020AE40 flags 1), list (0020B210 flags 1): a wrap event
 *      zeroes t+0x1C, t[5] = 2, 0020BBE0(t, t[0x1A]). Arrows.
 *   2: frame, 0020BC50 (flags 1): done returns to 1. Arrows.
 *   3: frame, list flags 0x401, arrows; t[6] -= 1; at 0 or on 0x5060
 *      (with the back cue on 0x5060): request cleared, t[5] = 1, t[6] = 0,
 *      D_00282240 = 3.
 * (The NEARMISS C passes 0020B0D0 (0, t); the instructions pass (t,
 * D_00265B80) as the other pages do.) */
int em_status_pages_00214570(EmArea01Ui *s, uint32_t t)
{
    if (sp_begin(s, 0x00214570u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00214570u, 0x20);
    const uint32_t tbl = 0x00265B80u, rows = 0x00265BF0u;
    const uint32_t st = ui_lbu(s, t + 5u);
    if (st == 3) {
        frame(s, t, EQ_BG, tbl, 1);
        (void)list(s, t, rows, EQ_LIST, 0x401);
        arrows(s, t, tbl);
        const uint32_t c = (ui_lbu(s, t + 6u) - 1u) & 0xFFu;
        ui_sb(s, t + 6u, c);
        if (c == 0 || (ui_lhu(s, UI_D_00810E74) & 0x5060u)) {
            if (ui_lhu(s, UI_D_00810E74) & 0x5060u) (void)ui_call0(s, SP_0020CD60);
            ui_sb(s, UI_D_008106B0, 0);
            ui_sb(s, t + 5u, 1);
            ui_sb(s, t + 6u, 0);
            ui_sw(s, SP_40, 3);
        }
        return ui_leave(s, fr);
    }
    if (st == 2) {
        frame(s, t, EQ_BG, tbl, 1);
        uint32_t r = 0;
        (void)em_status_pages_0020BC50(s, t, rows, EQ_LIST, 1, &r);
        if (r != 0) ui_sb(s, t + 5u, ui_lbu(s, t + 5u) - 1u);
        arrows(s, t, tbl);
        return ui_leave(s, fr);
    }
    if (st == 0) {
        ui_sb(s, t + 0x17u, 0);
        ui_sb(s, t + 0x19u, 0);
        ui_sb(s, t + 0x18u, 0);
        ui_sb(s, t + 0x1Au, 0);
        ui_sw(s, SP_B0, 4);
        ui_sw(s, SP_B4, 0);
        ui_sw(s, SP_40, 3);
        ui_sh(s, t + 0x1Eu, 0x17);
        if (ui_lbu(s, 0x00810C7Cu) != 0)
            append(s, t, 1);
        else if (ui_lbu(s, 0x00810C7Bu) != 0)
            append(s, t, 0);
        if (ui_lbu(s, 0x00810C7Du) != 0) append(s, t, 2);
        if (ui_lbu(s, 0x00810C7Eu) != 0) append(s, t, 3);
        if (ui_lbu(s, UI_D_008106B0) != 0) {
            ui_sb(s, t + 0x1Bu, ui_lbu(s, UI_D_008106B1) - 0x17u);
            const int32_t n = (int32_t)ui_lbu(s, t + 0x18u);
            for (int32_t i = 0; i < n; ++i) {
                if (ui_lbu(s, t + (uint32_t)i + 0x50u) == ui_lbu(s, t + 0x1Bu)) {
                    ui_sb(s, t + 0x17u, (uint32_t)i);
                    const uint32_t b1 = ui_lbu(s, UI_D_008106B1);
                    ui_sw(s, SP_40, 4);
                    if (b1 == 0x19 && ui_lbu(s, 0x00810C60u) == 1) ui_sb(s, 0x00810C60u, 2);
                    break;
                }
            }
            ui_sb(s, UI_D_008106B0, 0);
            ui_sb(s, t + 5u, 3);
            ui_sb(s, t + 6u, 0xF0);
            return ui_leave(s, fr);
        }
        ui_sb(s, t + 5u, ui_lbu(s, t + 5u) + 1u);
        ui_sb(s, t + 6u, 0);
    } else if (st != 1) {
        return ui_leave(s, fr);
    }
    const uint32_t pad = ui_lhu(s, UI_D_00810E74);
    if (pad & 0x20u) {
        back_to_root(s, t);
        return ui_leave(s, fr);
    }
    const uint32_t c = ui_lbu(s, t + 6u);
    if (c != 0) {
        ui_sb(s, t + 6u, c - 1u);
    } else if (pad & 0x40u) {
        (void)ui_call0(s, SP_0020CD80);
        ui_sb(s, t + 6u, 0xF);
    }
    frame(s, t, EQ_BG, tbl, 1);
    if (list(s, t, rows, EQ_LIST, 1) != 0) {
        ui_sh(s, t + 0x1Cu, 0);
        ui_sb(s, t + 5u, ui_lbu(s, t + 5u) + 1u);
        (void)em_status_pages_0020BBE0(s, t, (int32_t)ui_lbu(s, t + 0x1Au));
    }
    arrows(s, t, tbl);
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 00215870(t): EVENT (entry base 0x23). State t[5]:
 *   0: cursor bytes cleared, the message words as EQUIPMENT; the list: 1
 *      if D_00810C88, else 0 if D_00810C87; then k - 0x23 for each event
 *      item k = 0x25..0x3B whose owned byte 0x810C89 + (k - 0x25) is set.
 *      A pending request selects B1 - 0x23 (t[0x1B]; page / row of its
 *      index, D_00282240 = 4), clears the request, notice (t[5] = 3, t[6]
 *      = 0xF0). Else t[5] = 1 and on into 1.
 *   1: Triangle backs out to the ITEM root. Frame (flags 4), list (flags
 *      4): a wrap event -> state 2 as EQUIPMENT; else Cross on a non-empty
 *      list: t+0x30 = 00185420(entry + 0x23) (the device), and when it is
 *      nonzero and 00182B30(D_008102B0) returns 0: accept cue, t[5] = 4,
 *      t[6] = 1; else the no-target cue, D_002821B4 = 0, t[5] = 5, t[6] =
 *      0xF0. Arrows.
 *   2: frame, 0020BC50 (flags 4): done returns to 1; arrows.
 *   5: D_002821B4 = 1, D_00282240 = 5, D_002821B8 = 0x19; frame, list
 *      flags 0x604, arrows; on 0x60 (back cue) or t[6] counting to 0:
 *      D_002821B4 = 0, request cleared, D_00282240 = 3, t[5] = 1.
 *   3: frame, list flags 0x404, arrows; t[6] -= 1; at 0 or on 0x5060 (back
 *      cue on 0x5060): request cleared, t[5] = 1, D_00282240 = 3.
 *   4: the use prompt: frame, list 0x404, arrows, D_002821B4 = 1,
 *      D_00282240 = 5, D_002821B8 = 0x18, 001FCF10, blend 3; left (0x8000)
 *      / right (0x2000) move the Yes / No cursor t[6] (cursor cue);
 *      0020CCB0(t); Cross: on No (t[6] != 0) D_002821B4 = 0, D_00282240 =
 *      3, t[5] = 1, back cue; on Yes the accept cue, the device's +0xB = 5,
 *      the scratchpad mode byte 0x70003B8D = 3 and D_008106C5 = 0xFF (the
 *      status screen closes); Triangle as No. */
int em_status_pages_00215870(EmArea01Ui *s, uint32_t t)
{
    if (sp_begin(s, 0x00215870u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00215870u, 0x20);
    const uint32_t tbl = 0x00265D20u, rows = 0x00265D90u;
    const uint32_t st = ui_lbu(s, t + 5u);
    switch (st) {
    case 0: {
        ui_sb(s, t + 0x17u, 0);
        ui_sb(s, t + 0x19u, 0);
        ui_sb(s, t + 0x18u, 0);
        ui_sb(s, t + 0x1Au, 0);
        ui_sw(s, SP_B0, 4);
        ui_sw(s, SP_B4, 0);
        ui_sw(s, SP_40, 3);
        ui_sh(s, t + 0x1Eu, 0x23);
        if (ui_lbu(s, 0x00810C88u) != 0)
            append(s, t, 1);
        else if (ui_lbu(s, 0x00810C87u) != 0)
            append(s, t, 0);
        for (uint32_t k = 0x25; k < 0x3C; ++k)
            if (ui_lbu(s, 0x00810725u + (k - 0x25u) + 0x564u) != 0) append(s, t, k - 0x23u);
        if (ui_lbu(s, UI_D_008106B0) != 0) {
            ui_sb(s, t + 0x1Bu, ui_lbu(s, UI_D_008106B1) - 0x23u);
            const int32_t n = (int32_t)ui_lbu(s, t + 0x18u);
            for (int32_t i = 0; i < n; ++i) {
                if (ui_lbu(s, t + (uint32_t)i + 0x50u) == ui_lbu(s, t + 0x1Bu)) {
                    locate(s, t, i);
                    ui_sw(s, SP_40, 4);
                    break;
                }
            }
            ui_sb(s, UI_D_008106B0, 0);
            ui_sb(s, t + 5u, 3);
            ui_sb(s, t + 6u, 0xF0);
            break;
        }
        ui_sb(s, t + 5u, ui_lbu(s, t + 5u) + 1u);
    }
    /* fall through */
    case 1:
        if (ui_lhu(s, UI_D_00810E74) & 0x20u) {
            back_to_root(s, t);
            break;
        }
        frame(s, t, EV_BG, tbl, 4);
        if (list(s, t, rows, EV_LIST, 4) != 0) {
            ui_sh(s, t + 0x1Cu, 0);
            ui_sb(s, t + 5u, ui_lbu(s, t + 5u) + 1u);
            (void)em_status_pages_0020BBE0(s, t, (int32_t)ui_lbu(s, t + 0x1Au));
        } else if (ui_lbu(s, t + 0x18u) != 0 && (ui_lhu(s, UI_D_00810E74) & 0x40u)) {
            const uint32_t i = ui_call1(s, SP_0020BEF0, t);
            const uint32_t id = ui_lbu(s, i + t + 0x50u);
            ui_sw(s, t + 0x30u, ui_call1(s, SP_00185420, id + 0x23u));
            if (ui_lw(s, t + 0x30u) != 0 && ui_call1(s, SP_00182B30, 0x008102B0u) == 0) {
                (void)ui_call0(s, SP_0020CD40);
                ui_sb(s, t + 5u, 4);
                ui_sb(s, t + 6u, 1);
            } else {
                (void)ui_call0(s, SP_0020CD80);
                ui_sw(s, SP_B4, 0);
                ui_sb(s, t + 5u, 5);
                ui_sb(s, t + 6u, 0xF0);
            }
        }
        arrows(s, t, tbl);
        break;
    case 2: {
        frame(s, t, EV_BG, tbl, 4);
        uint32_t r = 0;
        (void)em_status_pages_0020BC50(s, t, rows, EV_LIST, 4, &r);
        if (r != 0) ui_sb(s, t + 5u, ui_lbu(s, t + 5u) - 1u);
        arrows(s, t, tbl);
        break;
    }
    case 5: {
        ui_sw(s, SP_B4, 1);
        ui_sw(s, SP_40, 5);
        ui_sw(s, SP_B8, 0x19);
        frame(s, t, EV_BG, tbl, 4);
        (void)list(s, t, rows, EV_LIST, 0x604);
        arrows(s, t, tbl);
        if (!(ui_lhu(s, UI_D_00810E74) & 0x60u)) {
            const uint32_t c = (ui_lbu(s, t + 6u) - 1u) & 0xFFu;
            ui_sb(s, t + 6u, c);
            if (c != 0) break;
        }
        if (ui_lhu(s, UI_D_00810E74) & 0x60u) (void)ui_call0(s, SP_0020CD60);
        ui_sw(s, SP_B4, 0);
        ui_sb(s, UI_D_008106B0, 0);
        ui_sw(s, SP_40, 3);
        ui_sb(s, t + 5u, 1);
        break;
    }
    case 3: {
        frame(s, t, EV_BG, tbl, 4);
        (void)list(s, t, rows, EV_LIST, 0x404);
        arrows(s, t, tbl);
        const uint32_t c = (ui_lbu(s, t + 6u) - 1u) & 0xFFu;
        ui_sb(s, t + 6u, c);
        if (c != 0 && !(ui_lhu(s, UI_D_00810E74) & 0x5060u)) break;
        if (ui_lhu(s, UI_D_00810E74) & 0x5060u) (void)ui_call0(s, SP_0020CD60);
        ui_sb(s, UI_D_008106B0, 0);
        ui_sb(s, t + 5u, 1);
        ui_sw(s, SP_40, 3);
        break;
    }
    case 4: {
        frame(s, t, EV_BG, tbl, 4);
        (void)list(s, t, rows, EV_LIST, 0x404);
        arrows(s, t, tbl);
        ui_sw(s, SP_B4, 1);
        ui_sw(s, SP_40, 5);
        ui_sw(s, SP_B8, 0x18);
        (void)ui_call0(s, SP_001FCF10);
        sp_blend(s, 3);
        const uint32_t ev = ui_lhu(s, UI_D_00810E74);
        if (ev & 0x8000u) {
            const uint32_t c = ui_lbu(s, t + 6u);
            if (c != 0) {
                ui_sb(s, t + 6u, c - 1u);
                (void)ui_call0(s, SP_0020CDA0);
            }
        } else if (ev & 0x2000u) {
            const uint32_t c = ui_lbu(s, t + 6u);
            if (c == 0) {
                ui_sb(s, t + 6u, c + 1u);
                (void)ui_call0(s, SP_0020CDA0);
            }
        }
        (void)ui_call1(s, SP_0020CCB0, t);
        const uint32_t e2 = ui_lhu(s, UI_D_00810E74);
        if (e2 & 0x40u) {
            if (ui_lbu(s, t + 6u) != 0) {
                ui_sw(s, SP_B4, 0);
                ui_sw(s, SP_40, 3);
                ui_sb(s, t + 5u, 1);
                (void)ui_call0(s, SP_0020CD60);
                break;
            }
            (void)ui_call0(s, SP_0020CD40);
            const uint32_t dev = ui_lw(s, t + 0x30u);
            ui_sb(s, dev + 0xBu, 5);
            ui_sb(s, 0x70003B8Du, 3);
            ui_sb(s, SP_D_008106C5, 0xFF);
        } else if (e2 & 0x20u) {
            ui_sw(s, SP_B4, 0);
            ui_sw(s, SP_40, 3);
            ui_sb(s, t + 5u, 1);
            (void)ui_call0(s, SP_0020CD60);
        }
        break;
    }
    default:
        break;
    }
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 00215FE0(t): HEALING's list: t[0x18] = 0, then kind k = 0..4 appended
 * for each set byte D_00810C82 + k. */
int em_status_pages_00215FE0(EmArea01Ui *s, uint32_t t)
{
    if (ui_latched(s)) return -1;
    UiFrame fr = ui_enter(s, 0x00215FE0u, 0);
    ui_sb(s, t + 0x18u, 0);
    for (uint32_t k = 0; k < 5; ++k)
        if (ui_lbu(s, 0x00810C82u + k) != 0) append(s, t, k);
    return ui_leave(s, fr);
}

/* A refused use: the no-target cue, D_002821B4 = 0, D_002821B8 = line,
 * t[5] = 6, t[6] = 0xF0. */
static void refuse(EmArea01Ui *s, uint32_t t, uint32_t line)
{
    (void)ui_call0(s, SP_0020CD80);
    ui_sw(s, SP_B4, 0);
    ui_sw(s, SP_B8, line);
    ui_sb(s, t + 5u, 6);
    ui_sb(s, t + 6u, 0xF0);
}

/* An accepted use: t[5] = 4, t[6] = 1, the accept cue. */
static void prompt(EmArea01Ui *s, uint32_t t)
{
    ui_sb(s, t + 5u, 4);
    ui_sb(s, t + 6u, 1);
    (void)ui_call0(s, SP_0020CD40);
}

#define F_30 0x41F00000u
#define F_60 0x42700000u
#define F_100 0x42C80000u
#define D_HEALTH 0x00810858u    /* displayed health (float) */
#define D_INFECTION 0x0081085Cu /* infection (float) */
#define D_MODE 0x00810707u      /* byte: 1 caps health at 60 */

/* 002160B0(t, s0): HEALING (entry base 0x1E; kinds 0..4 = the medicine
 * items 0x1E..0x22). `s0` is the caller's s0 register (0020EE50 holds t
 * there): state 4 reads the list byte t + 0x50 + s0, a register the
 * original never sets on that path (the case-0 scan counter's register).
 * State t[5]:
 *   0: cursor bytes cleared, D_002821B0 = 4, D_002821B4 = 0, D_00282240 =
 *      3, t+0x1E = 0x1E; the list (00215FE0's, inline). A request 4 (the
 *      panel's) clears it, t+0x30 = D_008106D0 and (unless infection is 0
 *      and health 100 or more: D_002821B4 = 0, D_002821B8 = 0x1A, t[5] =
 *      6, t[6] = 0xF0) finds kind 2 (page / row) and prompts (t[5] = 4,
 *      t[6] = 1). Another request (a take) clears it, t[0x1B] = B1 - 0x1E,
 *      finds that kind (page / row, D_00282240 = 4) and shows the notice
 *      (t[5] = 3, t[6] = 0xF0). Without one: t[5] += 1 and on into 1.
 *   1: Triangle backs out to the ITEM root (no arrows). Else frame (flags
 *      8), list (flags 8): a wrap event zeroes t+0x1C, t[5] += 1,
 *      0020BBE0(t, t[0x1A]); else Cross on a non-empty list: t[6] = 1, the
 *      kind under the cursor (0020BEF0): kind 2 needs a device
 *      (t+0x30 = 00185420(0x20), else refused with line 0x19) and not
 *      (infection 0 and health >= 100) (refused 0x1A); kinds 3 / 4 prompt
 *      when infection is not 0 and D_00810707 is 0, else (D_00810707 == 1:
 *      health >= 60 refused, line 1; other: health >= 100 refused, line
 *      1); other kinds: D_00810707 == 1: health >= 60 refused (0x1B); else
 *      health >= 100 refused (0x1B); otherwise prompt. Arrows.
 *   2: frame, 0020BC50 (flags 8): done steps back to 1; arrows.
 *   6: D_002821B4 = 1, D_00282240 = 5; frame, list 0x608, arrows; t[6]
 *      counts down unless 0x60; at 0 or on 0x60 (back cue): D_002821B4 =
 *      0, request cleared, D_00282240 = 3, t[5] = 1.
 *   3: frame, list 0x408, arrows; t[6] -= 1; at 0 or on 0x5060 (back cue
 *      on 0x5060): request cleared, t[5] = 1, D_00282240 = 3.
 *   4: frame, list 0x408, arrows, D_002821B4 = 1, D_00282240 = 5, the
 *      prompt line D_002821B8 = 0x12 when the byte t + 0x50 + s0 is 2, else
 *      0x18; 001FCF10; blend 3; the Yes / No cursor t[6] (0x8000 / 0x2000,
 *      cursor cue); 0020CCB0(t). Cross on No: D_002821B4 = 0, D_00282240 =
 *      3, t[5] = 1, back cue. Cross on Yes: accept cue, k = the kind under
 *      the cursor, D_008106B0 = 1, then k 0: target t+0x34 = 30 + health
 *      capped at 60 (D_00810707 == 1) or 100, t[5] = 5, t+0x3C = 3, t[6] =
 *      0; 1: target 100 capped the same way, state 5 as 0; 2: D_008106B0 =
 *      2, target 100, t+0x38 = 0, t[0x10] = 8, the device (t+0x30) +0xA =
 *      1 and +0xB = 5, 0x70003B8D = 3, 0015C750(D_008102B0, 3, 1, device);
 *      3: D_00810707 == 1: target 30 + health capped at 60, t+0x38 =
 *      infection; else target 30 + health capped at 100, t+0x38 =
 *      infection - 30 floored at 0; t[0x10] = 8; 4: D_00810707 == 1:
 *      target 60, t+0x38 = infection; else 100 and 0; t[0x10] = 8. Then
 *      (every kind) 001C47E0(k + t+0x1E, 1), 00215FE0(t) and, with fewer
 *      than 4 entries left, the cursor clamped to the last. Triangle as No.
 *   5: the health count-up: frame, list 0x608, arrows; t+0x3C -= 1; every
 *      tenth main-loop frame (0x70003B64 % 10 == 0) sound 0xE; health
 *      steps +1.0 each time t+0x3C reaches 0 until it equals the target
 *      (then t[6] = 1); at t[6] == 1 or on 0x870: health = D_008104D0 =
 *      the target, 0015C700(D_008102B0), accept cue on 0x870, request
 *      cleared, t[5] = 1, t[6] = 0, D_002821B4 = 0, D_00282240 = 3; else
 *      t+0x3C back to 3 at 0. */
int em_status_pages_002160B0(EmArea01Ui *s, uint32_t t, uint32_t s0)
{
    if (sp_begin(s, 0x002160B0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x002160B0u, 0x30);
    const uint32_t tbl = 0x00265FF0u, rows = 0x00266060u;
    const uint32_t st = ui_lbu(s, t + 5u);
    switch (st) {
    case 0: {
        ui_sb(s, t + 0x17u, 0);
        ui_sb(s, t + 0x19u, 0);
        ui_sb(s, t + 0x18u, 0);
        ui_sb(s, t + 0x1Au, 0);
        ui_sw(s, SP_B0, 4);
        ui_sw(s, SP_B4, 0);
        ui_sw(s, SP_40, 3);
        ui_sh(s, t + 0x1Eu, 0x1E);
        for (uint32_t k = 0; k < 5; ++k)
            if (ui_lbu(s, 0x00810C82u + k) != 0) append(s, t, k);
        const uint32_t req = ui_lbu(s, UI_D_008106B0);
        if (req != 0) {
            if (req == 4) {
                ui_sb(s, UI_D_008106B0, 0);
                ui_sw(s, t + 0x30u, ui_lw(s, 0x008106D0u));
                if (sp_ceq(0, ui_lw(s, D_INFECTION)) && !sp_clt(ui_lw(s, D_HEALTH), F_100)) {
                    ui_sw(s, SP_B4, 0);
                    ui_sw(s, SP_B8, 0x1A);
                    ui_sb(s, t + 5u, 6);
                    ui_sb(s, t + 6u, 0xF0);
                    break;
                }
                const int32_t n = (int32_t)ui_lbu(s, t + 0x18u);
                for (int32_t i = 0; i < n; ++i) {
                    if (ui_lbu(s, t + (uint32_t)i + 0x50u) == 2) {
                        locate(s, t, i);
                        break;
                    }
                }
                ui_sb(s, t + 5u, 4);
                ui_sb(s, t + 6u, 1);
                break;
            }
            ui_sb(s, UI_D_008106B0, 0);
            ui_sb(s, t + 0x1Bu, ui_lbu(s, UI_D_008106B1) - 0x1Eu);
            const int32_t n = (int32_t)ui_lbu(s, t + 0x18u);
            for (int32_t i = 0; i < n; ++i) {
                if (ui_lbu(s, t + (uint32_t)i + 0x50u) == ui_lbu(s, t + 0x1Bu)) {
                    locate(s, t, i);
                    ui_sw(s, SP_40, 4);
                    break;
                }
            }
            ui_sb(s, t + 5u, 3);
            ui_sb(s, t + 6u, 0xF0);
            break;
        }
        ui_sb(s, t + 5u, ui_lbu(s, t + 5u) + 1u);
    }
    /* fall through */
    case 1: {
        if (ui_lhu(s, UI_D_00810E74) & 0x20u) {
            back_to_root(s, t);
            break;
        }
        frame(s, t, HL_BG, tbl, 8);
        if (list(s, t, rows, HL_LIST, 8) != 0) {
            ui_sh(s, t + 0x1Cu, 0);
            ui_sb(s, t + 5u, ui_lbu(s, t + 5u) + 1u);
            (void)em_status_pages_0020BBE0(s, t, (int32_t)ui_lbu(s, t + 0x1Au));
        } else if (ui_lbu(s, t + 0x18u) != 0 && (ui_lhu(s, UI_D_00810E74) & 0x40u)) {
            ui_sb(s, t + 6u, 1);
            const uint32_t i = ui_call1(s, SP_0020BEF0, t);
            const uint32_t kind = ui_lbu(s, i + t + 0x50u);
            if (kind == 2) {
                ui_sw(s, t + 0x30u, ui_call1(s, SP_00185420, kind + 0x1Eu));
                if (ui_lw(s, t + 0x30u) == 0)
                    refuse(s, t, 0x19);
                else if (sp_ceq(0, ui_lw(s, D_INFECTION)) && !sp_clt(ui_lw(s, D_HEALTH), F_100))
                    refuse(s, t, 0x1A);
                else
                    prompt(s, t);
            } else if (kind - 3u < 2u) {
                if (!sp_ceq(0, ui_lw(s, D_INFECTION)) && ui_lbu(s, D_MODE) == 0)
                    prompt(s, t);
                else if (ui_lbu(s, D_MODE) == 1)
                    sp_clt(ui_lw(s, D_HEALTH), F_60) ? prompt(s, t) : refuse(s, t, 1);
                else
                    sp_clt(ui_lw(s, D_HEALTH), F_100) ? prompt(s, t) : refuse(s, t, 1);
            } else if (ui_lbu(s, D_MODE) == 1) {
                sp_clt(ui_lw(s, D_HEALTH), F_60) ? prompt(s, t) : refuse(s, t, 0x1B);
            } else {
                sp_clt(ui_lw(s, D_HEALTH), F_100) ? prompt(s, t) : refuse(s, t, 0x1B);
            }
        }
        arrows(s, t, tbl);
        break;
    }
    case 2: {
        frame(s, t, HL_BG, tbl, 8);
        uint32_t r = 0;
        (void)em_status_pages_0020BC50(s, t, rows, HL_LIST, 8, &r);
        if (r != 0) ui_sb(s, t + 5u, ui_lbu(s, t + 5u) - 1u);
        arrows(s, t, tbl);
        break;
    }
    case 6: {
        ui_sw(s, SP_B4, 1);
        ui_sw(s, SP_40, 5);
        frame(s, t, HL_BG, tbl, 8);
        (void)list(s, t, rows, HL_LIST, 0x608);
        arrows(s, t, tbl);
        if (!(ui_lhu(s, UI_D_00810E74) & 0x60u)) {
            const uint32_t c = (ui_lbu(s, t + 6u) - 1u) & 0xFFu;
            ui_sb(s, t + 6u, c);
            if (c != 0) break;
        }
        if (ui_lhu(s, UI_D_00810E74) & 0x60u) (void)ui_call0(s, SP_0020CD60);
        ui_sw(s, SP_B4, 0);
        ui_sb(s, UI_D_008106B0, 0);
        ui_sw(s, SP_40, 3);
        ui_sb(s, t + 5u, 1);
        break;
    }
    case 3: {
        frame(s, t, HL_BG, tbl, 8);
        (void)list(s, t, rows, HL_LIST, 0x408);
        arrows(s, t, tbl);
        const uint32_t c = (ui_lbu(s, t + 6u) - 1u) & 0xFFu;
        ui_sb(s, t + 6u, c);
        if (c != 0 && !(ui_lhu(s, UI_D_00810E74) & 0x5060u)) break;
        if (ui_lhu(s, UI_D_00810E74) & 0x5060u) (void)ui_call0(s, SP_0020CD60);
        ui_sb(s, UI_D_008106B0, 0);
        ui_sb(s, t + 5u, 1);
        ui_sw(s, SP_40, 3);
        break;
    }
    case 4: {
        frame(s, t, HL_BG, tbl, 8);
        (void)list(s, t, rows, HL_LIST, 0x408);
        arrows(s, t, tbl);
        ui_sw(s, SP_B4, 1);
        const uint32_t probe = s0 + t;
        ui_sw(s, SP_40, 5);
        ui_sw(s, SP_B8, ui_lbu(s, probe + 0x50u) == 2 ? 0x12 : 0x18);
        (void)ui_call0(s, SP_001FCF10);
        sp_blend(s, 3);
        const uint32_t ev = ui_lhu(s, UI_D_00810E74);
        if (ev & 0x8000u) {
            const uint32_t c = ui_lbu(s, t + 6u);
            if (c != 0) {
                ui_sb(s, t + 6u, c - 1u);
                (void)ui_call0(s, SP_0020CDA0);
            }
        } else if (ev & 0x2000u) {
            const uint32_t c = ui_lbu(s, t + 6u);
            if (c == 0) {
                ui_sb(s, t + 6u, c + 1u);
                (void)ui_call0(s, SP_0020CDA0);
            }
        }
        (void)ui_call1(s, SP_0020CCB0, t);
        const uint32_t e2 = ui_lhu(s, UI_D_00810E74);
        if (e2 & 0x40u) {
            if (ui_lbu(s, t + 6u) != 0) {
                ui_sw(s, SP_B4, 0);
                ui_sw(s, SP_40, 3);
                ui_sb(s, t + 5u, 1);
                (void)ui_call0(s, SP_0020CD60);
                break;
            }
            (void)ui_call0(s, SP_0020CD40);
            const uint32_t i = ui_call1(s, SP_0020BEF0, t);
            const uint32_t kind = ui_lbu(s, i + t + 0x50u);
            ui_sb(s, UI_D_008106B0, 1);
            switch (kind) {
            case 0:
            case 1: {
                if (kind == 0) {
                    ui_sw(s, t + 0x34u, ui_fadd(F_30, ui_lw(s, D_HEALTH)));
                    if (ui_lbu(s, D_MODE) == 1) {
                        if (!sp_clt(ui_lw(s, t + 0x34u), F_60)) ui_sw(s, t + 0x34u, F_60);
                    } else if (!sp_clt(ui_lw(s, t + 0x34u), F_100)) {
                        ui_sw(s, t + 0x34u, F_100);
                    }
                } else {
                    ui_sw(s, t + 0x34u, F_100);
                    if (ui_lbu(s, D_MODE) == 1) {
                        if (!sp_clt(ui_lw(s, t + 0x34u), F_60)) ui_sw(s, t + 0x34u, F_60);
                    } else if (!sp_clt(ui_lw(s, t + 0x34u), F_100)) {
                        ui_sw(s, t + 0x34u, F_100);
                    }
                }
                ui_sb(s, t + 5u, 5);
                ui_sh(s, t + 0x3Cu, 3);
                ui_sb(s, t + 6u, 0);
                break;
            }
            case 2: {
                ui_sb(s, UI_D_008106B0, 2);
                ui_sw(s, t + 0x34u, F_100);
                ui_sw(s, t + 0x38u, 0);
                ui_sb(s, t + 0x10u, 8);
                const uint32_t dev = ui_lw(s, t + 0x30u);
                ui_sb(s, dev + 0xAu, 1);
                ui_sb(s, dev + 0xBu, 5);
                ui_sb(s, 0x70003B8Du, 3);
                (void)ui_call4(s, SP_0015C750, 0x008102B0u, 3, 1, ui_sx(dev));
                break;
            }
            case 3: {
                const uint32_t lvl = ui_fadd(F_30, ui_lw(s, D_HEALTH));
                if (ui_lbu(s, D_MODE) == 1) {
                    ui_sw(s, t + 0x34u, lvl);
                    if (!sp_clt(lvl, F_60)) ui_sw(s, t + 0x34u, F_60);
                    ui_sw(s, t + 0x38u, ui_lw(s, D_INFECTION));
                } else {
                    ui_sw(s, t + 0x34u, lvl);
                    if (!sp_clt(lvl, F_100)) ui_sw(s, t + 0x34u, F_100);
                    const uint32_t left = ui_fsub(ui_lw(s, D_INFECTION), F_30);
                    ui_sw(s, t + 0x38u, left);
                    if (sp_clt(left, 0)) ui_sw(s, t + 0x38u, 0);
                }
                ui_sb(s, t + 0x10u, 8);
                break;
            }
            case 4:
                if (ui_lbu(s, D_MODE) == 1) {
                    ui_sw(s, t + 0x34u, F_60);
                    ui_sw(s, t + 0x38u, ui_lw(s, D_INFECTION));
                } else {
                    ui_sw(s, t + 0x34u, F_100);
                    ui_sw(s, t + 0x38u, 0);
                }
                ui_sb(s, t + 0x10u, 8);
                break;
            default:
                break;
            }
            (void)ui_call2(s, SP_001C47E0, ui_sx(kind + (uint32_t)ui_lh(s, t + 0x1Eu)), 1);
            (void)em_status_pages_00215FE0(s, t);
            const int32_t n = (int32_t)ui_lbu(s, t + 0x18u);
            if (n < 4 && !((int32_t)ui_lbu(s, t + 0x17u) < n - 1)) ui_sb(s, t + 0x17u, (uint32_t)(n - 1));
        } else if (e2 & 0x20u) {
            (void)ui_call0(s, SP_0020CD60);
            ui_sw(s, SP_B4, 0);
            ui_sw(s, SP_40, 3);
            ui_sb(s, t + 5u, 1);
        }
        break;
    }
    case 5: {
        frame(s, t, HL_BG, tbl, 8);
        (void)list(s, t, rows, HL_LIST, 0x608);
        arrows(s, t, tbl);
        ui_sh(s, t + 0x3Cu, (uint32_t)(ui_lh(s, t + 0x3Cu) - 1));
        if ((int32_t)ui_lw(s, UI_SPAD_3B64) % 10 == 0) (void)ui_call4(s, SP_001FB9F0, 0xE, 0x1000, 0x1000, 0x1000);
        const uint32_t health = ui_lw(s, D_HEALTH);
        if (sp_ceq(ui_lw(s, t + 0x34u), health)) {
            ui_sb(s, t + 6u, 1);
        } else if (ui_lh(s, t + 0x3Cu) == 0) {
            ui_sw(s, D_HEALTH, ui_fadd(health, UI_F_ONE));
        }
        if (ui_lbu(s, t + 6u) == 1 || (ui_lhu(s, UI_D_00810E74) & 0x870u)) {
            const uint32_t lvl = ui_lw(s, t + 0x34u);
            ui_sw(s, D_HEALTH, lvl);
            ui_sw(s, 0x008104D0u, lvl);
            (void)ui_call1(s, SP_0015C700, 0x008102B0u);
            if (ui_lhu(s, UI_D_00810E74) & 0x870u) (void)ui_call0(s, SP_0020CD40);
            ui_sb(s, UI_D_008106B0, 0);
            ui_sb(s, t + 5u, 1);
            ui_sb(s, t + 6u, 0);
            ui_sw(s, SP_B4, 0);
            ui_sw(s, SP_40, 3);
        } else if (ui_lh(s, t + 0x3Cu) == 0) {
            ui_sh(s, t + 0x3Cu, 3);
        }
        break;
    }
    default:
        break;
    }
    return ui_leave(s, fr);
}
