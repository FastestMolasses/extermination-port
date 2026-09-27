/* Status pages lane: the SPR4 page 00211970 (0020CDC0 page 2, module
 * 0x2C; the 0x10 take's notice and the magazine refill) and its draw
 * helpers 002121A0 / 002125B0 / 00212B60 / 00212F30 / 0020BF20. The five
 * part pages it dispatches to are in em_status_pages_parts.c. Hand
 * translation of the original functions (boot ELF SCUS-97112);
 * docs/STATUS_PAGES.md. */
#include "game/em_status_pages_internal.h"

#define RGBA_40 UINT64_C(0x40808080)
#define RGBA_HI UINT64_C(0xFFFFFFFF805FFF6E) /* 0x805FFF6E as lui/ori leave it */
#define RGBA_20 UINT64_C(0x20808080)
#define RGBA_44 UINT64_C(0x40404040)

/* The number readout 001C5FB0(n, 4, 1) -> 00123168(D_002862C0, it) ->
 * 001CBA50(1, x, y, 0x10, 0x10, D_002862C0, style). */
static void number(EmArea01Ui *s, uint64_t n, uint64_t x, uint64_t y, uint32_t style)
{
    const uint32_t str = ui_call3(s, SP_001C5FB0, n, 4, 1);
    (void)ui_call2(s, SP_00123168, SP_D_002862C0, ui_sx(str));
    uint64_t a[7] = {1, x, y, 0x10, 0x10, SP_D_002862C0, style};
    ui_call(s, SP_001CBA50, 7, a, 0, NULL, NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* 002121A0(a0): the SPR4 frame. 0020A7A0(0x200479459D422300); blend 0; six
 * frame sprites; blend 3; one more; the blinking cartridge sprite (drawn
 * unless a0 is odd and the main-loop counter's bit 0x10 is clear); unless
 * a0 == 3 the reserve D_00810CB4 as a number at (0x8AC, 0x79F); a sprite;
 * the capacity D_00810C63 * 30 at (0x8AC, 0x7B6) and D_00810C63 itself at
 * (0x8AC, 0x7CC) (D_00810C63 read once); a last sprite. */
int em_status_pages_002121A0(EmArea01Ui *s, int32_t a0)
{
    if (sp_begin(s, 0x002121A0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x002121A0u, 0x20);
    (void)ui_call1(s, SP_0020A7A0, SP_TEX(0x20047945u, 0x9D422300u));
    sp_blend(s, 0);
    sp_blit(s, 0x7950, 0x7A90, 0x80, 0x80, RGBA_40, SP_TEX(0x20046785u, 0xDD322160u));
    sp_blit(s, 0x7800, 0x8300, 0x100, 0x80, RGBA_40, SP_TEX(0x200479C5u, 0xE1422100u));
    sp_blit(s, 0x74F0, 0x7A40, 0x40, 0x80, SP_RGBA_80, SP_TEX(0x20047B45u, 0xD9422320u));
    sp_blit(s, 0x7E20, 0x7A20, 0x80, 0x80, SP_RGBA_80, SP_TEX(0x20047B65u, 0xDD4222C0u));
    sp_blit(s, 0x7000, 0x7E80, 0x100, 0x80, RGBA_20, SP_TEX(0x200479E5u, 0xE1422180u));
    sp_blit(s, 0x8000, 0x7E80, 0x100, 0x80, RGBA_20, SP_TEX(0x200479E5u, 0xE1422200u));
    sp_blend(s, 3);
    sp_blit(s, 0x7000, 0x8300, 0x80, 0x80, SP_RGBA_80, SP_TEX(0x20047965u, 0xDD422240u));
    if (!(a0 & 1) || (ui_lw(s, UI_SPAD_3B64) & 0x10u))
        sp_blit(s, 0x87E0, 0x7960, 0x30, 0x30, SP_RGBA_80, SP_TEX(0x20047BE5u, 0x554223C8u));
    if (a0 != 3) number(s, ui_sx((uint32_t)ui_lh(s, 0x00810CB4u)), 0x8AC, 0x79F, SP_D_00265510);
    sp_blit(s, 0x8800, 0x7AE0, 0x80, 0x80, SP_RGBA_80, SP_TEX(0x20047D45u, 0xDD4222E0u));
    const uint32_t packs = ui_lbu(s, 0x00810C63u);
    number(s, packs * 30u, 0x8AC, 0x7B6, SP_D_00265510);
    number(s, packs, 0x8AC, 0x7CC, SP_D_00265510);
    sp_blit(s, 0x7100, 0x7900, 0x80, 0x40, SP_RGBA_80, SP_TEX(0x200480A5u, 0x9D422340u));
    return ui_leave(s, fr);
}

/* 002125B0(t, a1): the part selector. a1 != 0 clears the hover t[0x11];
 * a1 == 0 runs 0020D930(t, 2) (the stick hover, mode 2). The trail base
 * (212.0, 170.0) goes to 0x700038A0 / A4 and 0020AC70(t, 0x700038A0, a1)
 * steps the trail. Blend 0; the six part labels, each drawn highlighted
 * (colour 0x805FFF6E, plus a marker sprite) when it is the hover t[0x11]
 * (1..6), else in 0x80808080; blend 3; one last sprite. */
int em_status_pages_002125B0(EmArea01Ui *s, uint32_t t, int32_t a1)
{
    if (sp_begin(s, 0x002125B0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x002125B0u, 0x30);
    if (a1 != 0)
        ui_sb(s, t + 0x11u, 0);
    else
        (void)ui_call2(s, SP_0020D930, t, 2);
    ui_sw(s, 0x700038A0u, 0x43540000u);
    ui_sw(s, 0x700038A4u, 0x432A0000u);
    (void)ui_call3(s, SP_0020AC70, t, 0x700038A0u, ui_sx((uint32_t)a1));
    sp_blend(s, 0);
    const uint64_t mark = SP_TEX(0x20046785u, 0x55322338u);
    if (ui_lbu(s, t + 0x11u) == 1) {
        sp_blit(s, 0x7000, 0x7C10, 0x80, 0x20, RGBA_HI, SP_TEX(0x20047F65u, 0x5D4223A0u));
        sp_blit(s, 0x7800, 0x7C10, 0x80, 0x20, RGBA_HI, SP_TEX(0x20047FC5u, 0x5D4223A4u));
        sp_blit(s, 0x7940, 0x7C10, 0x20, 0x20, RGBA_HI, mark);
    } else {
        sp_blit(s, 0x7000, 0x7C10, 0x80, 0x20, SP_RGBA_80, SP_TEX(0x20047F65u, 0x5D4223A0u));
        sp_blit(s, 0x7800, 0x7C10, 0x80, 0x20, SP_RGBA_80, SP_TEX(0x20047FC5u, 0x5D4223A4u));
    }
    static const struct {
        uint16_t x, y, mx, my;
        uint32_t hi, lo;
    } part[5] = {
        {0x74C0, 0x7AD0, 0x7AD0, 0x7AC0, 0x20047F45u, 0x5D422394u},
        {0x7E20, 0x7A80, 0x7DC0, 0x7AC0, 0x20047D65u, 0x5D422380u},
        {0x7F20, 0x7C10, 0x7F30, 0x7C10, 0x20047DC5u, 0x5D422384u},
        {0x7E20, 0x7DB0, 0x7DC0, 0x7D60, 0x20047DE5u, 0x5D422390u},
        {0x74C0, 0x7D50, 0x7AC0, 0x7D60, 0x20047F45u, 0x5D422394u},
    };
    for (uint32_t k = 0; k < 5; ++k) {
        if (ui_lbu(s, t + 0x11u) == k + 2u) {
            sp_blit(s, part[k].x, part[k].y, 0x80, 0x20, RGBA_HI, SP_TEX(part[k].hi, part[k].lo));
            sp_blit(s, part[k].mx, part[k].my, 0x20, 0x20, RGBA_HI, mark);
        } else {
            sp_blit(s, part[k].x, part[k].y, 0x80, 0x20, SP_RGBA_80, SP_TEX(part[k].hi, part[k].lo));
        }
    }
    sp_blend(s, 3);
    sp_blit(s, 0x70B0, 0x7C00, 0x20, 0x20, SP_RGBA_80, SP_TEX(0x20047BC5u, 0x554223BCu));
    return ui_leave(s, fr);
}

/* 00212F30(id, ctx): one of ten part-marker sprites by id & 0xFF (0..9;
 * others draw nothing), colour `ctx` passed on as its register image. */
int em_status_pages_00212F30(EmArea01Ui *s, int32_t id, uint64_t ctx)
{
    if (sp_begin(s, 0x00212F30u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00212F30u, 0x10);
    static const struct {
        uint16_t x, y, w, h;
        uint32_t hi, lo;
    } m[10] = {
        {0x7C80, 0x8060, 0x40, 0x40, 0x20046F05u, 0x99322310u}, {0x7380, 0x7EA0, 0x100, 0x80, 0x20046F85u, 0xE1321E00u},
        {0x7380, 0x7EA0, 0x100, 0x80, 0x20047605u, 0xE1321E40u}, {0x7380, 0x7EA0, 0x100, 0x80, 0x20047685u, 0xE1321F00u},
        {0x7380, 0x7EA0, 0x100, 0x80, 0x20047805u, 0xE1321F40u}, {0x7C80, 0x7FA0, 0x40, 0x20, 0x20046D85u, 0x59322350u},
        {0x7C80, 0x7EE0, 0x80, 0x40, 0x20047885u, 0x9D322280u}, {0x8080, 0x7F20, 0x40, 0x20, 0x20047A05u, 0x59322360u},
        {0x8080, 0x7EE0, 0x40, 0x20, 0x20047A85u, 0x59322370u}, {0x8080, 0x7EE0, 0x40, 0x20, 0x20047C05u, 0x59322328u},
    };
    const uint32_t k = (uint32_t)id & 0xFFu;
    if (k < 10) sp_blit(s, m[k].x, m[k].y, m[k].w, m[k].h, ctx, SP_TEX(m[k].hi, m[k].lo));
    return ui_leave(s, fr);
}

/* 00212B60(t): the SPR4 weapon picture by the equipment byte D_00810CA4
 * (and the parts D_00810CA5 / CA6 / CA7 through 00212F30): colours c0 /
 * c1 by the hover t[0x11] (2, 5, 6: 0x40808080 / 0x80808080; 3, 4: both
 * 0x80808080; others both 0x40808080); blend 0; the body sprite in c0;
 * CA4 == 2: two more sprites in c1; CA4 == 0: the barrel (c0 and CA6's
 * marker in c1 when the hover is 6, else the reverse); other CA4: a
 * sprite in c0, then CA4 == 1: as CA4 == 0 with another sprite; else the
 * three markers CA5 / CA6 / CA7, each in c1 when the hover is 2 / 6 / 5. */
int em_status_pages_00212B60(EmArea01Ui *s, uint32_t t)
{
    if (sp_begin(s, 0x00212B60u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00212B60u, 0x40);
    uint64_t c0, c1;
    switch (ui_lbu(s, t + 0x11u)) {
    case 2:
    case 5:
    case 6:
        c1 = SP_RGBA_80;
        c0 = RGBA_40;
        break;
    case 3:
    case 4:
        c1 = c0 = SP_RGBA_80;
        break;
    default:
        c1 = c0 = RGBA_40;
        break;
    }
    sp_blend(s, 0);
    sp_blit(s, 0x7C80, 0x7EA0, 0x100, 0x80, c0, SP_TEX(0x20046B05u, 0xE1321D00u));
    const uint32_t eq = ui_lbu(s, 0x00810CA4u);
    if (eq == 2) {
        sp_blit(s, 0x7480, 0x7EA0, 0x80, 0x80, c1, SP_TEX(0x20047E85u, 0xDD322140u));
        sp_blit(s, 0x7C80, 0x7EA0, 0x100, 0x80, c1, SP_TEX(0x20047705u, 0xE1322040u));
        return ui_leave(s, fr);
    }
    if (eq == 0) {
        const int h6 = ui_lbu(s, t + 0x11u) == 6;
        sp_blit(s, 0x7580, 0x7EA0, 0x100, 0x80, h6 ? c0 : c1, SP_TEX(0x20047C85u, 0xE1322000u));
        (void)em_status_pages_00212F30(s, (int32_t)ui_lbu(s, 0x00810CA6u), h6 ? c1 : c0);
        return ui_leave(s, fr);
    }
    sp_blit(s, 0x7580, 0x7EA0, 0x80, 0x40, c0, SP_TEX(0x20046D05u, 0x9D322260u));
    if (ui_lbu(s, 0x00810CA4u) == 1) {
        const int h6 = ui_lbu(s, t + 0x11u) == 6;
        sp_blit(s, 0x7C80, 0x7EA0, 0x80, 0x40, h6 ? c0 : c1, SP_TEX(0x20047E05u, 0x9D3222A0u));
        (void)em_status_pages_00212F30(s, (int32_t)ui_lbu(s, 0x00810CA6u), h6 ? c1 : c0);
        return ui_leave(s, fr);
    }
    (void)em_status_pages_00212F30(s, (int32_t)ui_lbu(s, 0x00810CA5u), ui_lbu(s, t + 0x11u) == 2 ? c1 : c0);
    (void)em_status_pages_00212F30(s, (int32_t)ui_lbu(s, 0x00810CA6u), ui_lbu(s, t + 0x11u) == 6 ? c1 : c0);
    (void)em_status_pages_00212F30(s, (int32_t)ui_lbu(s, 0x00810CA7u), ui_lbu(s, t + 0x11u) == 5 ? c1 : c0);
    return ui_leave(s, fr);
}

/* 0020BF20(tbl, mode, sel): the four-row counter panel (primary
 * ammunition D_00810CA8, secondary D_00810CAA, fuel D_00810CAE + 100 *
 * D_00810CAC, D_00810CB0). mode bit 0 clear: y0 = 0x128, every row
 * 0x80808080 with style D_00265510; set: y0 = 0x96, rows 0x40404040 with
 * style D_00265518, and sel 1..4 brightens that row (0x80808080, style
 * D_00265510). x0 = 0x17E, row pitch 0x22, icons 0x30 square. Blend 0;
 * each nonzero row r: its icon at (float_to_int(16 * (x0 + 0x700)),
 * float_to_int(16 * (((y0 + 0x22 r) >> 1) + 0x790))) (x converted
 * first), texture the doubleword tbl + {0x10 / 0x18, 0 / 8, 0x30 / 0x38,
 * 0x20 / 0x28}[r] (the second when D_00810CA6 == 1 / D_00810CA6 is 2 or 3
 * / D_00810CA6 == 4 / D_00810CA4 == 2); with mode 2 and sel naming the row
 * (0x11..0x12 / 0x13..0x14 / 0x15 / 0x16) the icon blinks with the
 * main-loop counter's bit 0x10; then the count through 001C5FB0(n, 4, 1)
 * and 00123168 at (float_to_int(16 * (x0 + 0x2E + 0x700)) >> 4,
 * float_to_int(16 * (((y0 + 0x22 r + 0x10) >> 1) + 0x790)) >> 4). */
int em_status_pages_0020BF20(EmArea01Ui *s, uint32_t tbl, int32_t mode, int32_t sel)
{
    if (sp_begin(s, 0x0020BF20u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x0020BF20u, 0x1F0);
    const int32_t x0 = 0x17E, dy = 0x22, h = 0x30, xoff = 0x2E, yoff = 0x10;
    int32_t y0;
    uint64_t c[4];
    uint32_t st[4];
    if (!(mode & 1)) {
        y0 = 0x128;
        for (int r = 0; r < 4; ++r) {
            c[r] = SP_RGBA_80;
            st[r] = SP_D_00265510;
        }
    } else {
        y0 = 0x96;
        for (int r = 0; r < 4; ++r) {
            c[r] = RGBA_44;
            st[r] = SP_D_00265518;
        }
        if (sel >= 1 && sel <= 4) {
            c[sel - 1] = SP_RGBA_80;
            st[sel - 1] = SP_D_00265510;
        }
    }
    sp_blend(s, 0);
    for (int r = 0; r < 4; ++r) {
        int32_t n;
        if (r == 0)
            n = ui_lh(s, 0x00810CA8u);
        else if (r == 1)
            n = ui_lh(s, 0x00810CAAu);
        else if (r == 2)
            n = ui_lh(s, 0x00810CAEu) + ui_lh(s, 0x00810CACu) * 100;
        else
            n = ui_lh(s, 0x00810CB0u);
        if (n == 0) continue;
        int blink;
        if (r == 0)
            blink = mode == 2 && (uint32_t)(sel - 0x11) < 2u;
        else if (r == 1)
            blink = mode == 2 && (uint32_t)(sel - 0x13) < 2u;
        else
            blink = mode == 2 && sel == (r == 2 ? 0x15 : 0x16);
        if (!blink || (ui_lw(s, UI_SPAD_3B64) & 0x10u)) {
            int alt;
            if (r == 0) {
                alt = ui_lbu(s, 0x00810CA6u) == 1;
            } else if (r == 1) {
                const uint32_t k = ui_lbu(s, 0x00810CA6u);
                alt = k == 2 || k == 3;
            } else if (r == 2) {
                alt = ui_lbu(s, 0x00810CA6u) == 4;
            } else {
                alt = ui_lbu(s, 0x00810CA4u) == 2;
            }
            static const uint32_t off[4][2] = {{0x10, 0x18}, {0x0, 0x8}, {0x30, 0x38}, {0x20, 0x28}};
            const uint64_t x = sp_f2i(s, sp_x16(x0 + 0x700));
            const uint64_t y = sp_f2i(s, sp_x16(((y0 + dy * r) >> 1) + 0x790));
            const uint32_t e = tbl + off[r][alt];
            const uint64_t tex = (uint64_t)ui_lw(s, e) | (uint64_t)ui_lw(s, e + 4u) << 32;
            sp_blit(s, x, y, (uint32_t)h, (uint32_t)h, c[r], tex);
        }
        const uint32_t str = ui_call3(s, SP_001C5FB0, ui_sx((uint32_t)n), 4, 1);
        (void)ui_call2(s, SP_00123168, SP_D_002862C0, ui_sx(str));
        const uint64_t tx = sp_f2i(s, sp_x16(x0 + xoff + 0x700));
        const uint64_t ty = sp_f2i(s, sp_x16(((yoff + (y0 + dy * r)) >> 1) + 0x790));
        uint64_t a[7] = {1, ui_sx((uint32_t)((int32_t)(uint32_t)tx >> 4)), ui_sx((uint32_t)((int32_t)(uint32_t)ty >> 4)),
                         0x10, 0x10, SP_D_002862C0, st[r]};
        ui_call(s, SP_001CBA50, 7, a, 0, NULL, NULL, NULL);
    }
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* The SPR4 page's common draw: 002121A0(frame), 002125B0(t, sel),
 * 00212B60(t), 0020BF20(D_00265980, mode, arg); with mode 2 the panel's
 * sel is D_008106B1, read after 00212B60. */
static void spr4_draw(EmArea01Ui *s, uint32_t t, int32_t frame, int32_t sel, int32_t mode)
{
    (void)em_status_pages_002121A0(s, frame);
    (void)em_status_pages_002125B0(s, t, sel);
    (void)em_status_pages_00212B60(s, t);
    const int32_t arg = mode == 2 ? (int32_t)ui_lbu(s, UI_D_008106B1) : 0;
    (void)em_status_pages_0020BF20(s, 0x00265980u, mode, arg);
}

/* 00211970(t): the SPR4 page, by its state t[4]:
 *   0: D_002821B0 = 4, D_002821B4 = 0, D_00282240 = 2, t[5] = 0, 0020E020.
 *      Request 5 (the magazine refill): t+0x1C = D_00810C63 * 30 (the
 *      capacity), t+0x1E = D_00810CB4 (the reserve); already full: t+0x3C
 *      = 0xF0, t[4] = 12, D_002821B4 = 1, D_00282240 = 5, D_002821B8 = 5;
 *      else t[4] = 10, t[6] = 1. Another request (a take, B1 < 0x17):
 *      it is cleared, D_00282240 = 4, D_002821B8 = B1, t[6] = 0xF0, t[4] =
 *      9. None: t[4] = 1.
 *   1: the part selector: draw (frame 0, selector 0, panel mode 0); the
 *      help line by the hover t[0x11] (1..6: lines 5, 3, 1, 4, 0, 2; none:
 *      D_002821B4 = 0); Triangle: back cue, t[0x10] = 0x63 (to the hub);
 *      Cross on a hover: accept cue, D_002821B4 = 0, t[0x15] = the hover,
 *      t[4] = 2.
 *   2: by t[0x15]: 1 -> t[0x10] = 0x63; 2..6 -> D_00275BD8 = 1 and the
 *      part page's state t[6] (5, 7, 8, 6, 4) and module t[0x16] (0x2E,
 *      0x30, 0x31, 0x2F, 0x2D), t[4] = 3.
 *   3: at t[5] == 0 001FF080(0, t[0x16]) and t[5] = 1; then once
 *      D_00275BD8 clears, t[4] = t[6], t[5] = 0.
 *   4..8: the part pages 00218D90, 00217090, 00218640, 002177B0, 00217FA0.
 *   9: the take notice: D_002821B4 = 1; frame 1 for B1 == 0x10, else 0;
 *      selector 1, panel mode 2 with sel = B1; t[6] counts down unless
 *      0x60; at 0 or on 0x60 (accept cue on 0x60): D_002821B4 = 0,
 *      D_00282240 = 2, t[4] = 1, t[5] = 0.
 *   10: the refill prompt: D_002821B4 = 1, D_00282240 = 5, frame 1,
 *      selector 1, panel mode 0, D_002821B8 = 0x15, 001FCF10, blend 3; the
 *      Yes / No cursor t[6] (0x8000 / 0x2000, cursor cue); 0020CCB0(t);
 *      Cross: D_002821B4 = 0, then on No D_008106C5 = 0xFF (close) and the
 *      back cue, on Yes the accept cue and t[4] = 11; 0x30: D_002821B4 =
 *      0, back cue, D_008106C5 = 0xFF.
 *   11: the refill: frame 3, selector 1, panel mode 0; t+0x1E += 3; sound
 *      0x182 every tenth main-loop frame; at 0x70 (accept cue) or t+0x1E
 *      reaching t+0x1C: t+0x1E = t+0x1C, D_00810C62 = 30, D_00810CB4 =
 *      D_00810C63 * 30, t[4] = 12, t+0x3C = 0x78; then t+0x1E as a number
 *      at (0x8AC, 0x79F).
 *   12: the done notice: frame 1, selector 1, panel mode 2 with sel = B1;
 *      t+0x3C counts down unless 0x70; at 0 or on 0x70 (accept cue on
 *      0x70): D_002821B4 = 0, D_008106C5 = 0xFF. */
int em_status_pages_00211970(EmArea01Ui *s, uint32_t t)
{
    if (sp_begin(s, 0x00211970u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00211970u, 0x20);
    switch (ui_lbu(s, t + 4u)) {
    case 0: {
        ui_sw(s, SP_B0, 4);
        ui_sw(s, SP_B4, 0);
        ui_sw(s, SP_40, 2);
        ui_sb(s, t + 5u, 0);
        (void)ui_call0(s, SP_0020E020);
        const uint32_t req = ui_lbu(s, UI_D_008106B0);
        if (req == 5) {
            ui_sh(s, t + 0x1Cu, ui_lbu(s, 0x00810C63u) * 30u);
            ui_sh(s, t + 0x1Eu, (uint32_t)ui_lh(s, 0x00810CB4u));
            if (ui_lh(s, t + 0x1Cu) == ui_lh(s, t + 0x1Eu)) {
                ui_sh(s, t + 0x3Cu, 0xF0);
                ui_sb(s, t + 4u, 12);
                ui_sw(s, SP_B4, 1);
                ui_sw(s, SP_40, 5);
                ui_sw(s, SP_B8, 5);
            } else {
                ui_sb(s, t + 4u, 10);
                ui_sb(s, t + 6u, 1);
            }
        } else if (req != 0) {
            ui_sb(s, UI_D_008106B0, 0);
            const uint32_t b1 = ui_lbu(s, UI_D_008106B1);
            ui_sw(s, SP_40, 4);
            ui_sw(s, SP_B8, b1);
            ui_sb(s, t + 6u, 0xF0);
            ui_sb(s, t + 4u, 9);
        } else {
            ui_sb(s, t + 4u, ui_lbu(s, t + 4u) + 1u);
        }
        break;
    }
    case 1: {
        spr4_draw(s, t, 0, 0, 0);
        static const uint8_t line[7] = {0, 5, 3, 1, 4, 0, 2};
        const uint32_t hv = ui_lbu(s, t + 0x11u);
        if (hv >= 1 && hv <= 6) {
            ui_sw(s, SP_B4, 1);
            ui_sw(s, SP_B8, line[hv]);
        } else {
            ui_sw(s, SP_B4, 0);
        }
        const uint32_t ev = ui_lhu(s, UI_D_00810E74);
        if (ev & 0x20u) {
            (void)ui_call0(s, SP_0020CD60);
            ui_sb(s, t + 0x10u, 0x63);
        } else if (ui_lbu(s, t + 0x11u) != 0 && (ev & 0x40u)) {
            (void)ui_call0(s, SP_0020CD40);
            ui_sw(s, SP_B4, 0);
            ui_sb(s, t + 0x15u, ui_lbu(s, t + 0x11u));
            ui_sb(s, t + 4u, ui_lbu(s, t + 4u) + 1u);
        }
        break;
    }
    case 2: {
        static const uint8_t next[7] = {0, 0, 5, 7, 8, 6, 4};
        static const uint8_t module[7] = {0, 0, 0x2E, 0x30, 0x31, 0x2F, 0x2D};
        const uint32_t k = ui_lbu(s, t + 0x15u);
        if (k == 1) {
            ui_sb(s, t + 0x10u, 0x63);
        } else if (k >= 2 && k <= 6) {
            ui_sb(s, SP_D_00275BD8, 1);
            ui_sb(s, t + 6u, next[k]);
            ui_sb(s, t + 4u, ui_lbu(s, t + 4u) + 1u);
            ui_sb(s, t + 0x16u, module[k]);
        }
        break;
    }
    case 3:
        if (ui_lbu(s, t + 5u) == 0) {
            (void)ui_call2(s, SP_001FF080, 0, ui_lbu(s, t + 0x16u));
            ui_sb(s, t + 5u, ui_lbu(s, t + 5u) + 1u);
        } else if (ui_lbu(s, SP_D_00275BD8) == 0) {
            ui_sb(s, t + 4u, ui_lbu(s, t + 6u));
            ui_sb(s, t + 5u, 0);
        }
        break;
    case 4:
        (void)em_status_pages_00218D90(s, t);
        break;
    case 5:
        (void)em_status_pages_00217090(s, t);
        break;
    case 6:
        (void)em_status_pages_00218640(s, t);
        break;
    case 7:
        (void)em_status_pages_002177B0(s, t);
        break;
    case 8:
        (void)em_status_pages_00217FA0(s, t);
        break;
    case 9: {
        const uint32_t b1 = ui_lbu(s, UI_D_008106B1);
        ui_sw(s, SP_B4, 1);
        spr4_draw(s, t, b1 == 0x10 ? 1 : 0, 1, 2);
        if (!(ui_lhu(s, UI_D_00810E74) & 0x60u)) {
            const uint32_t c = (ui_lbu(s, t + 6u) - 1u) & 0xFFu;
            ui_sb(s, t + 6u, c);
            if (c != 0) break;
        }
        if (ui_lhu(s, UI_D_00810E74) & 0x60u) (void)ui_call0(s, SP_0020CD40);
        ui_sw(s, SP_B4, 0);
        ui_sw(s, SP_40, 2);
        ui_sb(s, t + 4u, 1);
        ui_sb(s, t + 5u, 0);
        break;
    }
    case 10: {
        ui_sw(s, SP_B4, 1);
        ui_sw(s, SP_40, 5);
        spr4_draw(s, t, 1, 1, 0);
        ui_sw(s, SP_B8, 0x15);
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
            ui_sw(s, SP_B4, 0);
            if (ui_lbu(s, t + 6u) != 0) {
                ui_sb(s, SP_D_008106C5, 0xFF);
                (void)ui_call0(s, SP_0020CD60);
            } else {
                (void)ui_call0(s, SP_0020CD40);
                ui_sb(s, t + 4u, 0xB);
            }
        } else if (e2 & 0x30u) {
            ui_sw(s, SP_B4, 0);
            (void)ui_call0(s, SP_0020CD60);
            ui_sb(s, SP_D_008106C5, 0xFF);
        }
        break;
    }
    case 11: {
        spr4_draw(s, t, 3, 1, 0);
        ui_sh(s, t + 0x1Eu, (uint32_t)(ui_lh(s, t + 0x1Eu) + 3));
        if ((int32_t)ui_lw(s, UI_SPAD_3B64) % 10 == 0)
            (void)ui_call4(s, SP_001FB9F0, 0x182, 0x1000, 0x1000, 0x1000);
        const uint32_t e = ui_lhu(s, UI_D_00810E74) & 0x70u;
        if (e != 0 || !(ui_lh(s, t + 0x1Eu) < ui_lh(s, t + 0x1Cu))) {
            if (e != 0) (void)ui_call0(s, SP_0020CD40);
            ui_sh(s, t + 0x1Eu, (uint32_t)ui_lh(s, t + 0x1Cu));
            ui_sb(s, 0x00810C62u, 0x1E);
            ui_sh(s, 0x00810CB4u, ui_lbu(s, 0x00810C63u) * 30u);
            ui_sb(s, t + 4u, 12);
            ui_sh(s, t + 0x3Cu, 0x78);
        }
        number(s, ui_sx((uint32_t)ui_lh(s, t + 0x1Eu)), 0x8AC, 0x79F, SP_D_00265510);
        break;
    }
    case 12: {
        spr4_draw(s, t, 1, 1, 2);
        if (!(ui_lhu(s, UI_D_00810E74) & 0x70u)) {
            const int32_t c = (int16_t)(ui_lh(s, t + 0x3Cu) - 1);
            ui_sh(s, t + 0x3Cu, (uint32_t)c);
            if (c != 0) break;
        }
        if (ui_lhu(s, UI_D_00810E74) & 0x70u) (void)ui_call0(s, SP_0020CD40);
        ui_sw(s, SP_B4, 0);
        ui_sb(s, SP_D_008106C5, 0xFF);
        break;
    }
    default:
        break;
    }
    return ui_leave(s, fr);
}
