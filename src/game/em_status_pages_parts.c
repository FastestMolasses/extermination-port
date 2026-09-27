/* Status pages lane: the five SPR4 part pages 00211970 dispatches to
 * (states 4..8; modules 0x2D..0x31): 00218D90, 00217090, 00218640,
 * 002177B0 and 00217FA0. The five share one list-page skeleton; what each
 * lists, how Cross is checked and what Yes commits differ and are spelled
 * out per page below. Hand translation of the original functions (boot
 * ELF SCUS-97112), from the instructions (the decomp's NEARMISS C differs
 * in 002177B0's Cross check: docs/STATUS_PAGES.md section 2);
 * docs/STATUS_PAGES.md. */
#include "game/em_status_pages_internal.h"

enum { PANEL_ZERO, PANEL_KIND4, PANEL_KIND2 };

typedef struct {
    uint32_t address;
    uint64_t bg;       /* 0020A7A0 */
    uint32_t frame;    /* 0020AE40 / 0020B0D0 table */
    uint32_t panel;    /* 0020BF20 table */
    uint32_t rows;     /* 0020B210 / 0020BC50 rows */
    uint64_t list_tex; /* 0020B210 / 0020BC50 texture word */
    uint32_t flags;    /* list flags; the notice / prompt use flags | 0x400 */
    int panel_kind;    /* the 0020BF20 selection in states 1, 3, 4 */
} Part;

/* 0020BF20(panel, 1, sel): the selection from the entry under the cursor
 * (0020BEF0) for 00218D90 (4 -> 3, 3 or 2 -> 2, 1 -> 1, else 0) and
 * 002177B0 (below 2 -> 0, else 4); 0 for the others and in state 2. */
static void panel(EmArea01Ui *s, const Part *pg, uint32_t t, int live)
{
    int32_t sel = 0;
    if (live && pg->panel_kind != PANEL_ZERO) {
        const uint32_t i = ui_call1(s, SP_0020BEF0, t);
        const uint32_t k = ui_lbu(s, i + t + 0x50u);
        if (pg->panel_kind == PANEL_KIND4)
            sel = k == 4 ? 3 : k == 3 || k == 2 ? 2 : k == 1 ? 1 : 0;
        else
            sel = (int32_t)k < 2 ? 0 : 4;
    }
    (void)em_status_pages_0020BF20(s, pg->panel, 1, sel);
}

static void top(EmArea01Ui *s, const Part *pg, uint32_t t, int live)
{
    (void)ui_call1(s, SP_0020A7A0, pg->bg);
    (void)ui_call3(s, SP_0020AE40, t, pg->frame, pg->flags);
    panel(s, pg, t, live);
}

static void arrows(EmArea01Ui *s, const Part *pg, uint32_t t) { (void)ui_call2(s, SP_0020B0D0, t, pg->frame); }

static void append(EmArea01Ui *s, uint32_t t, uint32_t k)
{
    const uint32_t n = ui_lbu(s, t + 0x18u);
    ui_sb(s, t + 0x18u, n + 1u);
    ui_sb(s, n + t + 0x50u, k);
}

/* The back-out to the SPR4 page (0020CDC0 reloads module 0x2C and the page
 * restarts at its state 0): D_002821B4 = 2, t[1] = 3, t[2..5] = 0. */
static void to_spr4(EmArea01Ui *s, uint32_t t)
{
    ui_sw(s, SP_B4, 2);
    ui_sb(s, t + 1u, 3);
    ui_sb(s, t + 2u, 0);
    ui_sb(s, t + 3u, 0);
    ui_sb(s, t + 4u, 0);
    ui_sb(s, t + 5u, 0);
}

/* The page-specific parts. */
typedef void (*PartHook)(EmArea01Ui *s, uint32_t t);
typedef int (*PartCross)(EmArea01Ui *s, uint32_t t); /* 1: accept (state 4) */

/* The shared skeleton. State t[5]:
 *   0: t[0x17] = t[0x19] = t[0x18] = t[0x1A] = 0, D_002821B0 = 4,
 *      D_002821B4 = 0, D_00282240 = 3, then `init` (t[7], t+0x1E, t[0x12]
 *      and the list); a pending request (a take): t[0x1B] = B1 - base, the
 *      entry holding it is located (`locate`: page / row, else t[0x17] =
 *      its index) with D_00282240 = 4, the request is cleared and the
 *      notice shows (t[5] = 3, t[6] = 0xF0). Else t[5] = 1 and on into 1.
 *   1: Triangle: back cue and back to the SPR4 page. Else top (0020A7A0,
 *      0020AE40, 0020BF20), the list (a wrap event: t+0x1C = 0, t[5] += 1,
 *      0020BBE0(t, t[0x1A])), else Cross on a non-empty list: `cross` says
 *      accept (cue, t[5] = 4, t[6] = 1) or, with the debounce t[7] at 0,
 *      the no-target cue and t[7] = 0x10. Arrows.
 *   2: top (selection 0), 0020BC50: done steps back to 1; arrows.
 *   3: top, list (flags | 0x400), arrows; t[6] -= 1; at 0 or on 0x5060
 *      (back cue on 0x5060): request cleared, t[5] = 1, D_00282240 = 3.
 *   4: top, list (flags | 0x400), arrows, D_002821B4 = 1, D_00282240 = 5,
 *      the prompt line (`line`), 001FCF10, blend 3, the Yes / No cursor t[6]
 *      (0x8000 / 0x2000, cursor cue), 0020CCB0(t); Cross on No: D_002821B4
 *      = 0, D_00282240 = 3, t[5] = 1, back cue; on Yes sound 0x17B, `commit`,
 *      and back to the SPR4 page; Triangle: back cue, D_002821B4 = 0,
 *      D_00282240 = 3, t[5] = 1.
 * Every path ends with the debounce t[7] stepping down when nonzero. */
static int part_tick(EmArea01Ui *s, const Part *pg, uint32_t t, uint32_t base, PartHook init, int locate,
                     PartCross cross, uint32_t (*line)(EmArea01Ui *, uint32_t), PartHook commit)
{
    if (sp_begin(s, pg->address) < 0) return -1;
    UiFrame fr = ui_enter(s, pg->address, 0x20);
    switch (ui_lbu(s, t + 5u)) {
    case 0:
        ui_sb(s, t + 0x17u, 0);
        ui_sb(s, t + 0x19u, 0);
        ui_sb(s, t + 0x18u, 0);
        ui_sb(s, t + 0x1Au, 0);
        ui_sw(s, SP_B0, 4);
        ui_sw(s, SP_B4, 0);
        ui_sw(s, SP_40, 3);
        init(s, t);
        if (ui_lbu(s, UI_D_008106B0) != 0) {
            ui_sb(s, t + 0x1Bu, ui_lbu(s, UI_D_008106B1) - base);
            const int32_t n = (int32_t)ui_lbu(s, t + 0x18u);
            for (int32_t i = 0; i < n; ++i) {
                if (ui_lbu(s, t + (uint32_t)i + 0x50u) == ui_lbu(s, t + 0x1Bu)) {
                    if (locate) {
                        ui_sb(s, t + 0x19u, (uint32_t)((i >> 2) << 2));
                        ui_sb(s, t + 0x17u, (uint32_t)(i & 3));
                    } else {
                        ui_sb(s, t + 0x17u, (uint32_t)i);
                    }
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
    /* fall through */
    case 1:
        if (ui_lhu(s, UI_D_00810E74) & 0x20u) {
            (void)ui_call0(s, SP_0020CD60);
            to_spr4(s, t);
            break;
        }
        top(s, pg, t, 1);
        if (ui_call4(s, SP_0020B210, t, pg->rows, pg->list_tex, pg->flags) != 0) {
            ui_sh(s, t + 0x1Cu, 0);
            ui_sb(s, t + 5u, ui_lbu(s, t + 5u) + 1u);
            (void)em_status_pages_0020BBE0(s, t, (int32_t)ui_lbu(s, t + 0x1Au));
        } else if (ui_lbu(s, t + 0x18u) != 0 && (ui_lhu(s, UI_D_00810E74) & 0x40u)) {
            if (cross(s, t)) {
                (void)ui_call0(s, SP_0020CD40);
                ui_sb(s, t + 5u, 4);
                ui_sb(s, t + 6u, 1);
            } else if (ui_lbu(s, t + 7u) == 0) {
                (void)ui_call0(s, SP_0020CD80);
                ui_sb(s, t + 7u, 0x10);
            }
        }
        arrows(s, pg, t);
        break;
    case 2: {
        top(s, pg, t, 0);
        uint32_t r = 0;
        (void)em_status_pages_0020BC50(s, t, pg->rows, pg->list_tex, (int32_t)pg->flags, &r);
        if (r != 0) ui_sb(s, t + 5u, ui_lbu(s, t + 5u) - 1u);
        arrows(s, pg, t);
        break;
    }
    case 3: {
        top(s, pg, t, 1);
        (void)ui_call4(s, SP_0020B210, t, pg->rows, pg->list_tex, pg->flags | 0x400u);
        arrows(s, pg, t);
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
        top(s, pg, t, 1);
        (void)ui_call4(s, SP_0020B210, t, pg->rows, pg->list_tex, pg->flags | 0x400u);
        arrows(s, pg, t);
        ui_sw(s, SP_B4, 1);
        ui_sw(s, SP_40, 5);
        ui_sw(s, SP_B8, line(s, t));
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
            (void)ui_call4(s, SP_001FB9F0, 0x17B, 0x1000, 0x1000, 0x1000);
            commit(s, t);
            to_spr4(s, t);
        } else if (e2 & 0x20u) {
            (void)ui_call0(s, SP_0020CD60);
            ui_sw(s, SP_B4, 0);
            ui_sw(s, SP_40, 3);
            ui_sb(s, t + 5u, 1);
        }
        break;
    }
    default:
        break;
    }
    const uint32_t d = ui_lbu(s, t + 7u);
    if (d != 0) ui_sb(s, t + 7u, d - 1u);
    return ui_leave(s, fr);
}

static uint32_t line4(EmArea01Ui *s, uint32_t t)
{
    (void)s;
    (void)t;
    return 4;
}

/* The entry under the cursor as the originals index it without 0020BEF0:
 * t + t[0x19] + t[0x17] + 0x50 (t[0x17] read first). */
static uint32_t raw_entry(EmArea01Ui *s, uint32_t t)
{
    const uint32_t row = ui_lbu(s, t + 0x17u);
    const uint32_t page = ui_lbu(s, t + 0x19u);
    return ui_lbu(s, page + (row + t) + 0x50u);
}

/* ------------------------------------------------------------------ */
/* 00218D90: LOWER U.R.S. (module 0x2D), base 0; the list holds k = 0..4
 * for each set byte 0x810C64 + k; t[0x12] = D_00810CA6. Cross accepts an
 * entry (0020BEF0) other than D_00810CA6; Yes sets D_00810CA6 = entry +
 * t+0x1E, t[0x12] = it, and with D_00810CA4 == 2: D_00810CA7 = 7,
 * D_00810CA5 = 5, D_00810CA4 = 0xFF. */
static const Part LOWER = {0x00218D90u, SP_TEX(0x20043885u, 0x9D422130u), 0x002664F0u, 0x00266560u, 0x002665A0u,
                           SP_TEX(0x20042C05u, 0xA1321F80u), 0x100, PANEL_KIND4};

static void lower_init(EmArea01Ui *s, uint32_t t)
{
    ui_sb(s, t + 7u, 0);
    ui_sh(s, t + 0x1Eu, 0);
    ui_sb(s, t + 0x12u, ui_lbu(s, 0x00810CA6u));
    for (uint32_t k = 0; k < 5; ++k)
        if (ui_lbu(s, 0x00810700u + k + 0x564u) != 0) append(s, t, k);
}

static int lower_cross(EmArea01Ui *s, uint32_t t)
{
    const uint32_t i = ui_call1(s, SP_0020BEF0, t);
    const uint32_t cur = ui_lbu(s, 0x00810CA6u);
    return cur != ui_lbu(s, i + t + 0x50u);
}

static void lower_commit(EmArea01Ui *s, uint32_t t)
{
    const uint32_t i = ui_call1(s, SP_0020BEF0, t);
    const uint32_t base = (uint32_t)ui_lh(s, t + 0x1Eu);
    ui_sb(s, 0x00810CA6u, ui_lbu(s, i + t + 0x50u) + base);
    ui_sb(s, t + 0x12u, ui_lbu(s, 0x00810CA6u));
    const uint32_t eq = ui_lbu(s, 0x00810CA4u);
    if (eq != 0xFF && eq == 2) {
        ui_sb(s, 0x00810CA7u, 7);
        ui_sb(s, 0x00810CA5u, 5);
        ui_sb(s, 0x00810CA4u, 0xFF);
    }
}

int em_status_pages_00218D90(EmArea01Ui *s, uint32_t t)
{
    return part_tick(s, &LOWER, t, 0, lower_init, 1, lower_cross, line4, lower_commit);
}

/* ------------------------------------------------------------------ */
/* 00217090: UPPER U.R.S. (module 0x2E), base 5; the list: 0 if
 * D_00810C69, 1 if D_00810C6A; t[0x12] = D_00810CA5. Cross accepts an
 * entry (t[0x17], not 0020BEF0) whose id entry + 5 is not D_00810CA5;
 * Yes sets D_00810CA5 = entry + base (the entry at t[0x19] + t[0x17]),
 * t[0x12] = it, and unless D_00810CA4 is 0xFF: CA4 == 2 sets D_00810CA6 =
 * 0 and D_00810CA7 = 7; else D_00810CA7 = 7 and D_00810CA6 = 0 when it is
 * 0xFF; then D_00810CA4 = 0xFF. */
static const Part UPPER = {0x00217090u, SP_TEX(0x200419E5u, 0x9D4220A0u), 0x002660E0u, 0x00266150u, 0x00266190u,
                           SP_TEX(0x20041A05u, 0xA1321F80u), 0x10, PANEL_ZERO};

static void upper_init(EmArea01Ui *s, uint32_t t)
{
    ui_sb(s, t + 7u, 0);
    ui_sh(s, t + 0x1Eu, 5);
    ui_sb(s, t + 0x12u, ui_lbu(s, 0x00810CA5u));
    if (ui_lbu(s, 0x00810C69u) != 0) append(s, t, 0);
    if (ui_lbu(s, 0x00810C6Au) != 0) append(s, t, 1);
}

static int upper_cross(EmArea01Ui *s, uint32_t t)
{
    const uint32_t row = ui_lbu(s, t + 0x17u);
    const uint32_t cur = ui_lbu(s, 0x00810CA5u);
    return cur != ui_lbu(s, row + t + 0x50u) + 5u;
}

static void upper_commit(EmArea01Ui *s, uint32_t t)
{
    const uint32_t row = ui_lbu(s, t + 0x17u);
    const uint32_t page = ui_lbu(s, t + 0x19u);
    const uint32_t base = (uint32_t)ui_lh(s, t + 0x1Eu);
    ui_sb(s, 0x00810CA5u, ui_lbu(s, page + (row + t) + 0x50u) + base);
    ui_sb(s, t + 0x12u, ui_lbu(s, 0x00810CA5u));
    const uint32_t eq = ui_lbu(s, 0x00810CA4u);
    if (eq == 0xFF) return;
    if (eq == 2) {
        ui_sb(s, 0x00810CA6u, 0);
        ui_sb(s, 0x00810CA7u, 7);
    } else {
        ui_sb(s, 0x00810CA7u, 7);
        if (ui_lbu(s, 0x00810CA6u) == 0xFF) ui_sb(s, 0x00810CA6u, 0);
    }
    ui_sb(s, 0x00810CA4u, 0xFF);
}

int em_status_pages_00217090(EmArea01Ui *s, uint32_t t)
{
    return part_tick(s, &UPPER, t, 5, upper_init, 0, upper_cross, line4, upper_commit);
}

/* ------------------------------------------------------------------ */
/* 00218640: SCOPE MOUNT (module 0x2F), base 7; the list: 0 / 1 / 2 for
 * D_00810C6B / C6C / C6D; t[0x12] = D_00810CA7. Cross: entry (t[0x17]) + 7
 * not D_00810CA7; Yes: D_00810CA7 = entry (t[0x19] + t[0x17]) + base,
 * t[0x12] = it, and unless D_00810CA4 is 0xFF: CA4 == 2 sets D_00810CA6 =
 * 0 and D_00810CA5 = 5; else D_00810CA5 = 5 and D_00810CA6 = 0 when it is
 * 0xFF; then D_00810CA4 = 0xFF. */
static const Part SCOPE = {0x00218640u, SP_TEX(0x200424C5u, 0x9D4220D0u), 0x002663F0u, 0x00266460u, 0x002664A0u,
                           SP_TEX(0x20041D05u, 0xA1321F80u), 0x80, PANEL_ZERO};

static void scope_init(EmArea01Ui *s, uint32_t t)
{
    ui_sb(s, t + 7u, 0);
    ui_sh(s, t + 0x1Eu, 7);
    ui_sb(s, t + 0x12u, ui_lbu(s, 0x00810CA7u));
    if (ui_lbu(s, 0x00810C6Bu) != 0) append(s, t, 0);
    if (ui_lbu(s, 0x00810C6Cu) != 0) append(s, t, 1);
    if (ui_lbu(s, 0x00810C6Du) != 0) append(s, t, 2);
}

static int scope_cross(EmArea01Ui *s, uint32_t t)
{
    const uint32_t row = ui_lbu(s, t + 0x17u);
    const uint32_t cur = ui_lbu(s, 0x00810CA7u);
    return cur != ui_lbu(s, row + t + 0x50u) + 7u;
}

static void scope_commit(EmArea01Ui *s, uint32_t t)
{
    const uint32_t row = ui_lbu(s, t + 0x17u);
    const uint32_t page = ui_lbu(s, t + 0x19u);
    const uint32_t base = (uint32_t)ui_lh(s, t + 0x1Eu);
    ui_sb(s, 0x00810CA7u, ui_lbu(s, page + (row + t) + 0x50u) + base);
    ui_sb(s, t + 0x12u, ui_lbu(s, 0x00810CA7u));
    const uint32_t eq = ui_lbu(s, 0x00810CA4u);
    if (eq == 0xFF) return;
    if (eq == 2) {
        ui_sb(s, 0x00810CA6u, 0);
        ui_sb(s, 0x00810CA5u, 5);
    } else {
        ui_sb(s, 0x00810CA5u, 5);
        if (ui_lbu(s, 0x00810CA6u) == 0xFF) ui_sb(s, 0x00810CA6u, 0);
    }
    ui_sb(s, 0x00810CA4u, 0xFF);
}

int em_status_pages_00218640(EmArea01Ui *s, uint32_t t)
{
    return part_tick(s, &SCOPE, t, 7, scope_init, 0, scope_cross, line4, scope_commit);
}

/* ------------------------------------------------------------------ */
/* 002177B0: MULTIPLE ATTACHMENT (module 0x30), base 0xA; the list holds
 * k - 0xA for each set byte 0x810C6E + (k - 0xA), k = 0xA..0xE; t[0x12] =
 * D_00810CA4 + 0xA (0xFF when CA4 is 0xFF). Cross, from the instructions
 * (the NEARMISS C differs, section 2): with k = the entry under the cursor
 * (0020BEF0) and c = k - 2, when c < 3 (unsigned): D_0081070B = c and the
 * wanted id is 0xC when all of D_00810C70 / C71 / C72 are set, else t[0x12];
 * when not: the wanted id is k + 0xA. It accepts (t[0x13] = the id, stored
 * before the cue) when the id is not t[0x12]. The prompt line is 3 for id
 * 0xC, else 2. Yes: id 0xC sets D_00810CA4 = 2 and CA5 / CA6 / CA7 =
 * 0xFF; another id clears the words D_008106E8 / E4 / E0 and sets
 * D_00810CA4 = id - t[0x1E] (the low byte), CA5 = CA7 = 0xFF and CA6 = 0
 * when it is 0xFF; then t[0x12] = D_00810CA4 + t[0x1E]. */
static const Part MULTI = {0x002177B0u, SP_TEX(0x200427C5u, 0x9D422110u), 0x002661C0u, 0x00266230u, 0x00266270u,
                           SP_TEX(0x20042805u, 0xA1321F80u), 0x20, PANEL_KIND2};

static void multi_init(EmArea01Ui *s, uint32_t t)
{
    ui_sb(s, t + 7u, 0);
    ui_sh(s, t + 0x1Eu, 0xA);
    const uint32_t eq = ui_lbu(s, 0x00810CA4u);
    ui_sb(s, t + 0x12u, eq != 0xFF ? eq + ui_lbu(s, t + 0x1Eu) : 0xFFu);
    for (uint32_t k = 0xA; k < 0xF; ++k)
        if (ui_lbu(s, 0x0081070Au + (k - 0xAu) + 0x564u) != 0) append(s, t, k - 0xAu);
}

static int multi_cross(EmArea01Ui *s, uint32_t t)
{
    const uint32_t i = ui_call1(s, SP_0020BEF0, t);
    const uint32_t k = ui_lbu(s, i + t + 0x50u);
    const uint32_t c = k + 0xAu - 0xCu;
    uint32_t want = k + 0xAu;
    if (c < 3u) {
        ui_sb(s, 0x0081070Bu, c);
        uint32_t full = ui_lbu(s, 0x00810C70u) != 0 ? 1u : 0u;
        if (ui_lbu(s, 0x00810C71u) != 0) full |= 2u;
        if (ui_lbu(s, 0x00810C72u) != 0) full |= 4u;
        want = full == 7u ? 0xCu : ui_lbu(s, t + 0x12u);
    }
    if (ui_lbu(s, t + 0x12u) == want) return 0;
    ui_sb(s, t + 0x13u, want);
    return 1;
}

static uint32_t multi_line(EmArea01Ui *s, uint32_t t) { return ui_lbu(s, t + 0x13u) == 0xC ? 3u : 2u; }

static void multi_commit(EmArea01Ui *s, uint32_t t)
{
    if (ui_lbu(s, t + 0x13u) == 0xC) {
        ui_sb(s, 0x00810CA4u, 2);
        ui_sb(s, 0x00810CA5u, 0xFF);
        ui_sb(s, 0x00810CA6u, 0xFF);
        ui_sb(s, 0x00810CA7u, 0xFF);
    } else {
        ui_sw(s, 0x008106E8u, 0);
        ui_sw(s, 0x008106E4u, 0);
        ui_sw(s, 0x008106E0u, 0);
        const uint32_t id = ui_lbu(s, t + 0x13u);
        ui_sb(s, 0x00810CA4u, id - ui_lbu(s, t + 0x1Eu));
        ui_sb(s, 0x00810CA5u, 0xFF);
        ui_sb(s, 0x00810CA7u, 0xFF);
        if (ui_lbu(s, 0x00810CA6u) == 0xFF) ui_sb(s, 0x00810CA6u, 0);
    }
    const uint32_t eq = ui_lbu(s, 0x00810CA4u);
    ui_sb(s, t + 0x12u, eq + ui_lbu(s, t + 0x1Eu));
}

int em_status_pages_002177B0(EmArea01Ui *s, uint32_t t)
{
    return part_tick(s, &MULTI, t, 0xA, multi_init, 1, multi_cross, multi_line, multi_commit);
}

/* ------------------------------------------------------------------ */
/* 00217FA0: SELECTOR SWITCH (module 0x31), base 0xD; the list is always 0
 * and 1, plus 2 when D_00810C73 is set; t[0x12] = D_00810C61 + 0xD.
 * Cross: entry (t[0x17]) is not D_00810C61; Yes: the entry at t[0x19] +
 * t[0x17]: t[0x12] = entry + 0xD, D_00810C61 = entry. */
static const Part SELECTOR = {0x00217FA0u, SP_TEX(0x200412A5u, 0x9D422040u), 0x002662F0u, 0x00266360u,
                              0x002663A0u, SP_TEX(0x20040E05u, 0xA1321F00u), 0x40, PANEL_ZERO};

static void selector_init(EmArea01Ui *s, uint32_t t)
{
    ui_sb(s, t + 7u, 0);
    ui_sb(s, t + 0x12u, ui_lbu(s, 0x00810C61u) + 0xDu);
    ui_sh(s, t + 0x1Eu, 0xD);
    append(s, t, 0);
    append(s, t, 1);
    if (ui_lbu(s, 0x00810C73u) != 0) append(s, t, 2);
}

static int selector_cross(EmArea01Ui *s, uint32_t t)
{
    const uint32_t row = ui_lbu(s, t + 0x17u);
    const uint32_t cur = ui_lbu(s, 0x00810C61u);
    return cur != ui_lbu(s, row + t + 0x50u);
}

static void selector_commit(EmArea01Ui *s, uint32_t t)
{
    const uint32_t k = raw_entry(s, t);
    ui_sb(s, t + 0x12u, k + 0xDu);
    ui_sb(s, 0x00810C61u, k);
}

int em_status_pages_00217FA0(EmArea01Ui *s, uint32_t t)
{
    return part_tick(s, &SELECTOR, t, 0xD, selector_init, 0, selector_cross, line4, selector_commit);
}
