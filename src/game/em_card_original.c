/* em_card_original.c - see em_card_original.h (docs/SAVE_GAME.md section 6).
 *
 * Each routine names the original it translates. 002280F0, 00228320,
 * 002293B0 and 002267A0 are NEARMISS in the decomp and 00227C60 an asm
 * body: those follow the instructions; the others follow the byte-matched
 * C. Callee arguments are the register images the original holds at the
 * call: an addiu / lui-ori constant or an address sum as the sign-extended
 * 32-bit result, a word loaded from memory as loaded (sign-extended), a
 * register passed on as the caller's image. */
#include "game/em_card_original.h"

#include "game/em_area01_ui_internal.h"
#include "game/em_options_original.h"
#include "game/em_save_original.h"

/* Callees by original address. */
enum {
    CD_00114D18 = 0x00114D18u, /* libmc Format(port, slot) */
    CD_00121A28 = 0x00121A28u, /* memset(p, c, n) */
    CD_00122D10 = 0x00122D10u, /* sprintf(dst, format, ...) */
    CD_001CC1E0 = 0x001CC1E0u, /* a text run at (x, y) */
    CD_001FC770 = 0x001FC770u, /* a message line at (x, y) */
    CD_0020CD40 = 0x0020CD40u, /* the cues: confirm, */
    CD_0020CD60 = 0x0020CD60u, /*   back, */
    CD_0020CDA0 = 0x0020CDA0u  /*   move */
};

/* Original data. */
#define D_0028A4A0 0x0028A4A0u /* the icon container pointer */
#define D_002671A0 0x002671A0u /* 001FC770's second glyph table */
#define D_002671C0 0x002671C0u /* the place names (pointers) */
#define D_00273998 0x00273998u /* the slot number's format */
#define D_002739A0 0x002739A0u /* the play time's format */
#define D_002738E0 0x002738E0u /* the game's directory name */
#define D_00275898 0x00275898u /* the third text run's style */
#define D_00275C58 0x00275C58u /* the card helpers' error flag */
#define D_00275C5C 0x00275C5Cu /* the card helpers' command phase */
#define D_00275C64 0x00275C64u /* the last Sync result 001FE9A0 kept */
#define D_00275C94 0x00275C94u /* the slot rows' text buffer */
#define D_00275C95 0x00275C95u
#define D_00289B40 0x00289B40u /* each area's first place name (halfwords, 4 apart) */
#define D_00810CB2 0x00810CB2u /* a progress halfword the save mode moves by 4 */
#define D_00821500 0x00821500u /* GetDir's table (.bss) */

#define MINUS1 0xFFFFFFFFu
#define LINE UINT64_C(0x70808080) /* the lines' style word, lui / ori */

static int begin(EmArea01Ui *s, uint32_t address)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, address, EM_A01R_FAULT_NULL_WORKER, 0);
    return 0;
}

static void put(uint32_t *result, uint32_t v)
{
    if (result) *result = v;
}

static void cue(EmArea01Ui *s, uint32_t address) { (void)ui_call0(s, address); }

static void line(EmArea01Ui *s, uint64_t x, uint64_t y, uint64_t n)
{
    (void)em_options_001FCBD0(s, x, y, 7, n, LINE);
}

/* The record's card port and slot words as loaded. */
static uint64_t port_of(EmArea01Ui *s, uint32_t m) { return ui_sx(ui_lw(s, m + 0x48u)); }
static uint64_t slot_of(EmArea01Ui *s, uint32_t m) { return ui_sx(ui_lw(s, m + 0x4Cu)); }

/* 001FE9A0(1, m + 0x50, m + 0x54): 0 while the command is busy. */
static uint32_t sync_poll(EmArea01Ui *s, uint32_t m)
{
    uint32_t v = 0;
    (void)em_options_001FE9A0(s, 1, ui_sx(m + 0x50u), ui_sx(m + 0x54u), &v);
    return v;
}

/* 001FECB0(port, slot, m + 0x58, a3, t0). */
static uint32_t info(EmArea01Ui *s, uint64_t port, uint64_t slot, uint32_t m, uint64_t a3, uint64_t t0)
{
    uint32_t v = 0;
    (void)em_options_001FECB0(s, port, slot, ui_sx(m + 0x58u), a3, t0, &v);
    return v;
}

static uint32_t pad(EmArea01Ui *s) { return ui_lhu(s, UI_D_00810E74); }

/* The byte m[0x6C + m[0xA]]: the chosen slot's presence. */
static uint32_t chosen_slot(EmArea01Ui *s, uint32_t m) { return ui_lbu(s, ui_lbu(s, m + 0xAu) + m + 0x6Cu); }

static void clear(EmArea01Ui *s, uint32_t m, uint32_t lo, uint32_t hi)
{
    (void)em_options_00225CF0(s, m, lo, hi);
}

static void cb2_add(EmArea01Ui *s, int32_t d)
{
    ui_sh(s, D_00810CB2, (uint32_t)(ui_lh(s, D_00810CB2) + d));
}

/* The common tail of 00229170 / 00227980 / 00226B90 / 00226D50 / 00227DE0:
 * the GetInfo poll's verdict (type 2 kept: `ok`; -1 pending: `ok`; else
 * `bad`). */
static uint32_t verdict(EmArea01Ui *s, uint32_t m, uint32_t v, uint32_t ok, uint32_t bad)
{
    if (v == 0) return ui_lw(s, m + 0x58u) != 2 ? bad : ok;
    if (v == MINUS1) return ok;
    return bad;
}

/* ======================================================================
 * The screen
 * ====================================================================== */

/* 00227300(m) (byte-matched C). */
int em_card_00227300(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00227300u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00227300u, 0x20);
    uint32_t r = 0, v = 0;
    switch (ui_lw(s, m + 0x48u)) {
    case 0: line(s, 0x18, 0x20, 3); break;
    case 1: line(s, 0x18, 0x20, 4); break;
    default: break;
    }
    switch (ui_lbu(s, m + 1u)) {
    case 0:
        line(s, 0x3C, 0x9B, 9);
        (void)em_card_002280F0(s, m, &v);
        switch (v) {
        case 1: ui_sb(s, m + 1u, 1); clear(s, m, 2, 7); break;
        case 2: ui_sb(s, m + 1u, 4); ui_sb(s, m + 2u, 0); break;
        case 3: ui_sb(s, m + 1u, 4); ui_sb(s, m + 2u, 1); break;
        case 4: ui_sb(s, m + 1u, 4); ui_sb(s, m + 2u, 2); break;
        default: break;
        }
        break;
    case 1:
        line(s, 0x3C, 0x9B, 9);
        (void)em_save_002283B0(s, m, &v);
        switch (v) {
        case 1: ui_sb(s, m + 1u, 2); clear(s, m, 2, 7); break;
        case 2: ui_sb(s, m + 1u, 4); ui_sb(s, m + 2u, 2); break;
        case 3: ui_sb(s, m + 1u, 4); ui_sb(s, m + 2u, 3); break;
        default: break;
        }
        break;
    case 2:
        (void)em_card_002293B0(s, m);
        switch (ui_lbu(s, m + 2u)) {
        case 0:
            (void)em_card_00229170(s, m, &v);
            switch (v) {
            case 1:
                ui_sb(s, m + 2u, 1);
                clear(s, m, 3, 7);
                ui_sw(s, m + 0x64u, 0);
                break;
            case 2: r = 1; break;
            case 3: r = 2; break;
            case 4:
                ui_sb(s, m + 0x15u, 1);
                ui_sb(s, m + 1u, 1);
                (void)em_options_00225A20(s);
                clear(s, m, 2, 7);
                (void)em_options_001FE8D0(s);
                break;
            default: break;
            }
            break;
        case 1:
            (void)em_card_00227980(s, m, &v);
            switch (v) {
            case 1: ui_sb(s, m + 2u, 0); clear(s, m, 3, 7); break;
            case 2: r = 2; break;
            case 3:
                ui_sb(s, m + 2u, 2);
                if (ui_lbu(s, m + 0x14u) == 1) cb2_add(s, -4);
                clear(s, m, 3, 7);
                break;
            case 4:
                ui_sb(s, m + 0x15u, 1);
                ui_sb(s, m + 1u, 1);
                (void)em_options_00225A20(s);
                (void)em_options_001FE8D0(s);
                clear(s, m, 2, 7);
                break;
            default: break;
            }
            break;
        case 2: {
            const int unusable = ui_lbu(s, m + 0x14u) == 1 && chosen_slot(s, m) == 2;
            if (unusable) {
                line(s, 0x3C, 0x9B, 0x22);
                (void)em_save_00229680(s, m, &v);
            } else if (ui_lbu(s, m + 0x14u) == 1) {
                line(s, 0x3C, 0x9B, 0x10);
                (void)em_save_00228A90(s, m, &v);
            } else {
                line(s, 0x3C, 0x9B, 0x16);
                (void)em_save_00228B60(s, m, &v);
            }
            if (ui_lbu(s, m + 0x14u) == 1 && chosen_slot(s, m) == 2) {
                if (v == 1) {
                    ui_sb(s, m + 2u, 3);
                    cb2_add(s, 4);
                    clear(s, m, 3, 7);
                } else if (v == 2) {
                    cb2_add(s, 4);
                    ui_sb(s, m + 2u, 4);
                    clear(s, m, 3, 7);
                }
            } else if (v == 1) {
                ui_sb(s, m + 2u, 3);
                clear(s, m, 3, 7);
            } else if (v == 2) {
                if (ui_lbu(s, m + 0x14u) == 1) cb2_add(s, 4);
                ui_sb(s, m + 2u, 4);
                clear(s, m, 3, 7);
            }
            break;
        }
        case 3: (void)em_card_00227010(s, m, &r); break;
        case 4: (void)em_card_00227160(s, m, &r); break;
        default: break;
        }
        break;
    case 4:
        switch (ui_lbu(s, m + 2u)) {
        case 0: (void)em_card_00226E90(s, m, &r); break;
        case 1: (void)em_card_00226B90(s, m, &r); break;
        case 2: (void)em_card_002267A0(s, m, &r); break;
        case 3: (void)em_card_00226D50(s, m, &r); break;
        default: break;
        }
        break;
    default:
        break;
    }
    put(result, r);
    return ui_leave(s, fr);
}

/* 002280F0(m), the card check (the instructions; NEARMISS C), gated by
 * the Sync poll, on m[2]:
 *   0: GetInfo(port, slot, m + 0x58, 0, m + 0x60): 0 -> type other than 2:
 *      4; the format word 1: m[2] = 1; else the retry count m+0x20 below
 *      6 counts up, at 6 it is cleared, 001FE8D0 and 2. -1 -> on; else 4.
 *   1: GetDir(port, slot, D_002738E0, 0, t0 = 1, D_00821500): negative ->
 *      on; nonzero (the directory there) -> 1; none: 1 in load mode
 *      (m[0x14] == 2), else m[2] = 2.
 *   2: GetInfo(port, slot, m + 0x58, m + 0x5C, m + 0x60): as state 0, but
 *      the format word 1 returns 00228320(m) ? 1 : 3.
 * The rest: 4 with the error flag D_00275C58 set, else 0. */
int em_card_002280F0(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x002280F0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x002280F0u, 0x20);
    uint32_t r = 0;
    int tail = 1;
    if (sync_poll(s, m) == 0) {
        tail = 0;
    } else {
        const uint32_t st = ui_lbu(s, m + 2u);
        if (st == 0 || st == 2) {
            const uint32_t v = info(s, port_of(s, m), slot_of(s, m), m, st == 2 ? ui_sx(m + 0x5Cu) : 0,
                                    ui_sx(m + 0x60u));
            if (v == 0) {
                if (ui_lw(s, m + 0x58u) != 2) {
                    r = 4;
                    tail = 0;
                } else if (ui_lw(s, m + 0x60u) == 1) {
                    if (st == 0) {
                        ui_sb(s, m + 2u, 1);
                    } else {
                        uint32_t enough = 0;
                        (void)em_card_00228320(s, m, &enough);
                        r = enough == 0 ? 3u : 1u;
                        tail = 0;
                    }
                } else {
                    const int32_t n = (int32_t)ui_lw(s, m + 0x20u);
                    if (n < 6) {
                        ui_sw(s, m + 0x20u, (uint32_t)(n + 1));
                    } else {
                        ui_sw(s, m + 0x20u, 0);
                        (void)em_options_001FE8D0(s);
                        r = 2;
                        tail = 0;
                    }
                }
            } else if (v != MINUS1) {
                r = 4;
                tail = 0;
            }
        } else if (st == 1) {
            uint32_t v = 0;
            (void)em_save_001FEC20(s, port_of(s, m), slot_of(s, m), ui_sx(D_002738E0), 0, 1, ui_sx(D_00821500),
                                   &v);
            if ((int32_t)v >= 0) {
                if (v != 0 || ui_lbu(s, m + 0x14u) == 2) {
                    r = 1;
                    tail = 0;
                } else {
                    ui_sb(s, m + 2u, 2);
                }
            }
        }
    }
    if (tail) r = ui_lw(s, D_00275C58) != 0 ? 4u : 0u;
    put(result, r);
    return ui_leave(s, fr);
}

/* 00228320(m) (the instructions; NEARMISS C): 1 when the free count
 * m+0x5C covers 18 plus the icon's size in 1 KiB units (rounded up,
 * (*(*D_0028A4A0 + 0x18) + 0x3FF) >> 10, arithmetic); else 0. Its stack
 * copy of D_00266B18 is overwritten before it is read. */
int em_card_00228320(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00228320u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00228320u, 0x10);
    const int32_t icon = (int32_t)ui_lw(s, ui_lw(s, D_0028A4A0) + 0x18u);
    const int32_t kib = (int32_t)((uint32_t)icon + 0x3FFu) >> 10;
    const uint32_t need = 0x12u + (uint32_t)kib;
    const int32_t left = (int32_t)(ui_lw(s, m + 0x5Cu) - need);
    put(result, left >= 0 ? 1u : 0u);
    return ui_leave(s, fr);
}

/* 002293B0(m), the slot rows (the instructions; NEARMISS C). For row i
 * (0..4, y = 0x3C + 12 i): the number i + 1 through sprintf(D_00275C94,
 * D_00273998, i + 1) drawn by 001CC1E0(1, 0x724, y + 0x791, 0xA, 0x14,
 * D_00275C94, 0); then by the presence byte m[0x6C + i]: 0 the line
 * 001FCBD0(0x42, y, 7, 0); 2 the line 1; 1 the place name D_002671C0[r[1]
 * + (short)D_00289B40[4 r[0]]] (r = m + 0x74 + 12 i) by 001FC770(0x42, y,
 * name, r[6] ? D_002671A0 : 0), the play time r+8 (frames) as hours,
 * minutes, seconds through sprintf(D_00275C94, D_002739A0, ...) and
 * 001CC1E0(1, 0x88C, y + 0x790, ...), and with r[6] set the glyph 0x9E by
 * 001CC1E0(1, 0x8D2, y + 0x790, ..., D_00275898). */
int em_card_002293B0(EmArea01Ui *s, uint32_t m)
{
    if (begin(s, 0x002293B0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x002293B0u, 0x60);
    for (uint32_t i = 0; i < 5u; ++i) {
        const uint32_t y = 0x3Cu + 12u * i;
        const uint32_t q = m + 12u * i;
        {
            uint64_t a[3] = {ui_sx(D_00275C94), ui_sx(D_00273998), i + 1u};
            ui_call(s, CD_00122D10, 3, a, 0, NULL, NULL, NULL);
            uint64_t b[7] = {1, 0x724, y + 1u + 0x790u, 0xA, 0x14, ui_sx(D_00275C94), 0};
            ui_call(s, CD_001CC1E0, 7, b, 0, NULL, NULL, NULL);
        }
        const uint32_t kind = ui_lbu(s, m + i + 0x6Cu);
        if (kind == 0) {
            (void)em_options_001FCBD0(s, 0x42, y, 7, 0, LINE);
        } else if (kind == 2) {
            (void)em_options_001FCBD0(s, 0x42, y, 7, 1, LINE);
        } else if (kind == 1) {
            const uint32_t first = (uint32_t)ui_lh(s, D_00289B40 + 4u * ui_lbu(s, q + 0x74u));
            const uint32_t name = ui_lw(s, D_002671C0 + 4u * (ui_lbu(s, q + 0x75u) + first));
            const uint64_t glyphs = ui_lbu(s, q + 0x7Au) != 0 ? ui_sx(D_002671A0) : 0;
            uint64_t a[4] = {0x42, y, ui_sx(name), glyphs};
            ui_call(s, CD_001FC770, 4, a, 0, NULL, NULL, NULL);
            const int32_t t = (int32_t)ui_lw(s, q + 0x7Cu);
            const int32_t hh = t / 216000;
            const int32_t r1 = (int32_t)((uint32_t)t - (uint32_t)hh * 216000u);
            const int32_t mm = r1 / 3600;
            const int32_t r2 = (int32_t)((uint32_t)r1 - (uint32_t)mm * 3600u);
            uint64_t p[5] = {ui_sx(D_00275C94), ui_sx(D_002739A0), ui_sx((uint32_t)hh), ui_sx((uint32_t)mm),
                             ui_sx((uint32_t)(r2 / 60))};
            ui_call(s, CD_00122D10, 5, p, 0, NULL, NULL, NULL);
            uint64_t b[7] = {1, 0x88C, y + 0x790u, 0xA, 0x14, ui_sx(D_00275C94), 0};
            ui_call(s, CD_001CC1E0, 7, b, 0, NULL, NULL, NULL);
            if (ui_lbu(s, q + 0x7Au) != 0) {
                ui_sb(s, D_00275C94, 0x9E);
                ui_sb(s, D_00275C95, 0);
                uint64_t c[7] = {1, 0x8D2, y + 0x790u, 0xA, 0x14, ui_sx(D_00275C94), ui_sx(D_00275898)};
                ui_call(s, CD_001CC1E0, 7, c, 0, NULL, NULL, NULL);
            }
        }
    }
    return ui_leave(s, fr);
}

/* 00229170(m), the slot cursor (byte-matched C): the prompt (save mode
 * line 0x11; else 0x17 with saves found (m+0x68), else 6); the Sync poll
 * (busy: 0); Circle (0x20): 0020CD60, 001FE8D0, 2; Triangle (0x10): the
 * same, 3; Cross (0x40): 0020CD40, 001FE8D0 and 1, except in load mode
 * (m[0x14] == 2) on a slot not saved (presence other than 1), which goes
 * on; Up (repeat 0x1000) / Down (repeat 0x4000) move m[0xA] within 0..4
 * (cue); then the GetInfo poll's verdict: 4 with the error flag, 0 or 4. */
int em_card_00229170(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00229170u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00229170u, 0x20);
    uint32_t r = 0;
    if (ui_lbu(s, m + 0x14u) == 1)
        line(s, 0x3C, 0x9B, 0x11);
    else if (ui_lw(s, m + 0x68u) != 0)
        line(s, 0x3C, 0x9B, 0x17);
    else
        line(s, 0x3C, 0x9B, 6);
    if (sync_poll(s, m) == 0) goto out;
    {
        const uint32_t p = pad(s);
        if (p & 0x20u) {
            cue(s, CD_0020CD60);
            (void)em_options_001FE8D0(s);
            r = 2;
            goto out;
        }
        if (p & 0x10u) {
            cue(s, CD_0020CD60);
            (void)em_options_001FE8D0(s);
            r = 3;
            goto out;
        }
        if (p & 0x40u) {
            cue(s, CD_0020CD40);
            (void)em_options_001FE8D0(s);
            if (ui_lbu(s, m + 0x14u) != 2 || chosen_slot(s, m) == 1) {
                r = 1;
                goto out;
            }
        }
    }
    if ((ui_lhu(s, UI_D_00810E78) & 0x1000u) && ui_lbu(s, m + 0xAu) > 0) {
        cue(s, CD_0020CDA0);
        ui_sb(s, m + 0xAu, ui_lbu(s, m + 0xAu) - 1u);
    }
    if (ui_lhu(s, UI_D_00810E78) & 0x4000u) {
        if (ui_lbu(s, m + 0xAu) < 4) {
            ui_sb(s, m + 0xAu, ui_lbu(s, m + 0xAu) + 1u);
            cue(s, CD_0020CDA0);
        }
    }
    {
        const uint32_t v = info(s, port_of(s, m), slot_of(s, m), m, 0, 0);
        r = ui_lw(s, D_00275C58) != 0 ? 4u : verdict(s, m, v, 0, 4);
    }
out:
    put(result, r);
    return ui_leave(s, fr);
}

/* 00227980(m), the confirmation (byte-matched C): the question (save mode
 * by the slot's presence 0 / 1 / 2: lines 0x14 / 0x15 / 0x27; load mode
 * 0x1A), the Yes / No line 001FCBD0(0x8C, 0xC3, 7, 2) and the marker by
 * m[9] (0: 00225700, 1: 002256E0); the Sync poll (busy: 0); m[3] 0: m[3] =
 * 1, m[9] = 0; 1: Cross: 0020CD40, 001FE8D0, m[9] = 0 and 3 on m[9] == 1
 * (Yes), else 1; Circle: 0020CD60, m[9] = 0, 001FE8D0, 1; Triangle: the
 * same, 2; Left (0x2000) on m[9] != 0 / Right (0x8000) on m[9] == 0 move
 * it (cue). Then the GetInfo poll's verdict (4 with the error flag). */
int em_card_00227980(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00227980u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00227980u, 0x20);
    uint32_t r = 0;
    if (ui_lbu(s, m + 0x14u) == 1) {
        switch (chosen_slot(s, m)) {
        case 0: line(s, 0x3C, 0x9B, 0x14); break;
        case 1: line(s, 0x3C, 0x9B, 0x15); break;
        case 2: line(s, 0x3C, 0x9B, 0x27); break;
        default: break;
        }
    } else {
        line(s, 0x3C, 0x9B, 0x1A);
    }
    line(s, 0x8C, 0xC3, 2);
    switch (ui_lbu(s, m + 9u)) {
    case 0: (void)em_options_00225700(s); break;
    case 1: (void)em_options_002256E0(s); break;
    default: break;
    }
    if (sync_poll(s, m) == 0) goto out;
    switch (ui_lbu(s, m + 3u)) {
    case 0:
        ui_sb(s, m + 3u, 1);
        ui_sb(s, m + 9u, 0);
        break;
    case 1: {
        const uint32_t p = pad(s);
        if (p & 0x40u) {
            cue(s, CD_0020CD40);
            (void)em_options_001FE8D0(s);
            if (ui_lbu(s, m + 9u) == 1) {
                ui_sb(s, m + 9u, 0);
                r = 3;
            } else {
                ui_sb(s, m + 9u, 0);
                r = 1;
            }
            goto out;
        }
        if (p & 0x20u) {
            cue(s, CD_0020CD60);
            ui_sb(s, m + 9u, 0);
            (void)em_options_001FE8D0(s);
            r = 1;
            goto out;
        }
        if (p & 0x10u) {
            cue(s, CD_0020CD60);
            ui_sb(s, m + 9u, 0);
            (void)em_options_001FE8D0(s);
            r = 2;
            goto out;
        }
        if ((p & 0x2000u) && ui_lbu(s, m + 9u) != 0) {
            cue(s, CD_0020CDA0);
            ui_sb(s, m + 9u, 0);
        }
        if ((pad(s) & 0x8000u) && ui_lbu(s, m + 9u) == 0) {
            cue(s, CD_0020CDA0);
            ui_sb(s, m + 9u, 1);
        }
        break;
    }
    default:
        break;
    }
    {
        const uint32_t v = info(s, port_of(s, m), slot_of(s, m), m, 0, 0);
        r = ui_lw(s, D_00275C58) != 0 ? 4u : verdict(s, m, v, 0, 4);
    }
out:
    put(result, r);
    return ui_leave(s, fr);
}

/* 00227010(m), the done message (byte-matched C): its line (save mode
 * over an unusable slot 0x24, save mode 0x12, load mode 0x18); m[3] 0:
 * m[3] = 1, the count m+0x10 = 45; 1: the count down, at 0: over an
 * unusable slot in save mode m[1] = 0 and 00225CF0(m, 2, 7) (the card
 * check again), else 3. Else 0. */
int em_card_00227010(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00227010u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00227010u, 0x20);
    uint32_t r = 0;
    const uint32_t ph = ui_lbu(s, m + 0x14u);
    if (ph == 1 && chosen_slot(s, m) == 2)
        line(s, 0x3C, 0x9B, 0x24);
    else if (ph == 1)
        line(s, 0x3C, 0x9B, 0x12);
    else
        line(s, 0x3C, 0x9B, 0x18);
    switch (ui_lbu(s, m + 3u)) {
    case 0:
        ui_sb(s, m + 3u, 1);
        ui_sw(s, m + 0x10u, 0x2D);
        break;
    case 1:
        ui_sw(s, m + 0x10u, ui_lw(s, m + 0x10u) - 1u);
        if (ui_lw(s, m + 0x10u) == 0) {
            if (ui_lbu(s, m + 0x14u) == 1 && chosen_slot(s, m) == 2) {
                ui_sb(s, m + 1u, 0);
                clear(s, m, 2, 7);
            } else {
                r = 3;
            }
        }
        break;
    default:
        break;
    }
    put(result, r);
    return ui_leave(s, fr);
}

/* 00227160(m), the failure message (byte-matched C): its line (0x25 /
 * 0x13 / 0x19 as 00227010's); Cross: 0020CD40, 1; Circle: 0020CD60, 1;
 * Triangle: 0020CD60, 2; else 0. */
int em_card_00227160(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00227160u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00227160u, 0x10);
    uint32_t r = 0;
    const uint32_t ph = ui_lbu(s, m + 0x14u);
    if (ph == 1 && chosen_slot(s, m) == 2)
        line(s, 0x3C, 0x9B, 0x25);
    else if (ph == 1)
        line(s, 0x3C, 0x9B, 0x13);
    else
        line(s, 0x3C, 0x9B, 0x19);
    const uint32_t p = pad(s);
    if (p & 0x40u) {
        cue(s, CD_0020CD40);
        r = 1;
    } else if (p & 0x20u) {
        cue(s, CD_0020CD60);
        r = 1;
    } else if (p & 0x10u) {
        cue(s, CD_0020CD60);
        r = 2;
    }
    put(result, r);
    return ui_leave(s, fr);
}

/* 00226E90(m), the screen for the card check's result 2 (byte-matched C):
 * save mode 00227C60(m): 0 / 1 / 2 returned, 3 (formatted): m[1] = m[2] =
 * 2, 00225CF0(m, 3, 7), D_00810CB2 -= 4, the presence bytes m+0x6C..73
 * cleared (00121A28), m[0xA] = 0, and 0. Load mode: the slot rows
 * 002293B0(m) and the cursor 00229170(m): 2 -> 1, 3 -> 2, 4 -> m[0x15] =
 * m[1] = 1, 00225A20, 001FE8D0, 00225CF0(m, 2, 7); else 0. */
int em_card_00226E90(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00226E90u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00226E90u, 0x20);
    uint32_t r = 0, v = 0;
    if (ui_lbu(s, m + 0x14u) == 1) {
        (void)em_card_00227C60(s, m, &v);
        if (v == 1 || v == 2) {
            r = v;
        } else if (v == 3) {
            ui_sb(s, m + 1u, 2);
            ui_sb(s, m + 2u, 2);
            clear(s, m, 3, 7);
            cb2_add(s, -4);
            uint64_t a[3] = {ui_sx(m + 0x6Cu), 0, 8};
            ui_call(s, CD_00121A28, 3, a, 0, NULL, NULL, NULL);
            ui_sb(s, m + 0xAu, 0);
        }
    } else {
        (void)em_card_002293B0(s, m);
        (void)em_card_00229170(s, m, &v);
        if (v == 2) {
            r = 1;
        } else if (v == 3) {
            r = 2;
        } else if (v == 4) {
            ui_sb(s, m + 0x15u, 1);
            ui_sb(s, m + 1u, 1);
            (void)em_options_00225A20(s);
            (void)em_options_001FE8D0(s);
            clear(s, m, 2, 7);
        }
    }
    put(result, r);
    return ui_leave(s, fr);
}

/* 00227C60(m) (the instructions; asm body) on m[3]: 0: m[3] = 1, 0. 1:
 * 00227DE0(m): 3 -> m[3] = 2, 0; 2 -> 2; 1 -> 1; 0 -> 0; other -> 1. 2:
 * the line 0x1B and 00228070(m): 1 -> m[3] = 3, 00225CF0(m, 4, 7), 3; 0 ->
 * 0; other -> m[3] = 4, 00225CF0(m, 4, 7), 0. 3: 0. 4: 00227270()'s
 * result. Any other: 1. */
int em_card_00227C60(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00227C60u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00227C60u, 0x20);
    uint32_t r = 0, v = 0;
    switch (ui_lbu(s, m + 3u)) {
    case 0:
        ui_sb(s, m + 3u, 1);
        break;
    case 1:
        (void)em_card_00227DE0(s, m, &v);
        if (v == 3)
            ui_sb(s, m + 3u, 2);
        else if (v != 0)
            r = v == 2 ? 2u : 1u;
        break;
    case 2:
        line(s, 0x3C, 0x9B, 0x1B);
        (void)em_card_00228070(s, m, &v);
        if (v == 1) {
            ui_sb(s, m + 3u, 3);
            clear(s, m, 4, 7);
            r = 3;
        } else if (v != 0) {
            ui_sb(s, m + 3u, 4);
            clear(s, m, 4, 7);
        }
        break;
    case 3:
        break;
    case 4:
        (void)em_card_00227270(s, &r);
        break;
    default:
        r = 1;
        break;
    }
    put(result, r);
    return ui_leave(s, fr);
}

/* 00227DE0(m), the format prompt (byte-matched C) on m[4]: 1 the line 0xD;
 * 2 the line 0x1F, the Yes / No line and its marker by m[9]. The Sync
 * poll (busy: 0); Circle: 0020CD60, m[9] = 0, 001FE8D0, 1; Triangle: the
 * same, 2; m[4] 0: m[4] = 1, m[9] = 0; 1: Cross: 0020CD40, m[4] = 2; 2:
 * Cross: 0020CD40, 001FE8D0, 3 on m[9] != 0 (Yes) else 1, m[9] = 0; Left on
 * m[9] != 0 / Right on m[9] == 0 move it (cue). Then GetInfo's verdict:
 * 0, or 1 (no error flag test). */
int em_card_00227DE0(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00227DE0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00227DE0u, 0x20);
    uint32_t r = 0;
    switch (ui_lbu(s, m + 4u)) {
    case 1:
        line(s, 0x3C, 0x9B, 0xD);
        break;
    case 2:
        line(s, 0x3C, 0x9B, 0x1F);
        line(s, 0x8C, 0xC3, 2);
        switch (ui_lbu(s, m + 9u)) {
        case 0: (void)em_options_00225700(s); break;
        case 1: (void)em_options_002256E0(s); break;
        default: break;
        }
        break;
    default:
        break;
    }
    if (sync_poll(s, m) == 0) goto out;
    if (pad(s) & 0x20u) {
        cue(s, CD_0020CD60);
        ui_sb(s, m + 9u, 0);
        (void)em_options_001FE8D0(s);
        r = 1;
        goto out;
    }
    if (pad(s) & 0x10u) {
        cue(s, CD_0020CD60);
        ui_sb(s, m + 9u, 0);
        (void)em_options_001FE8D0(s);
        r = 2;
        goto out;
    }
    switch (ui_lbu(s, m + 4u)) {
    case 0:
        ui_sb(s, m + 4u, 1);
        ui_sb(s, m + 9u, 0);
        break;
    case 1:
        if (pad(s) & 0x40u) {
            cue(s, CD_0020CD40);
            ui_sb(s, m + 4u, 2);
        }
        break;
    case 2:
        if (pad(s) & 0x40u) {
            cue(s, CD_0020CD40);
            (void)em_options_001FE8D0(s);
            if (ui_lbu(s, m + 9u) != 0) {
                ui_sb(s, m + 9u, 0);
                r = 3;
            } else {
                ui_sb(s, m + 9u, 0);
                r = 1;
            }
            goto out;
        }
        if ((pad(s) & 0x2000u) && ui_lbu(s, m + 9u) != 0) {
            ui_sb(s, m + 9u, 0);
            cue(s, CD_0020CDA0);
        }
        if ((pad(s) & 0x8000u) && ui_lbu(s, m + 9u) == 0) {
            ui_sb(s, m + 9u, 1);
            cue(s, CD_0020CDA0);
        }
        break;
    default:
        break;
    }
    {
        const uint32_t v = info(s, port_of(s, m), slot_of(s, m), m, 0, 0);
        r = verdict(s, m, v, 0, 1);
    }
out:
    put(result, r);
    return ui_leave(s, fr);
}

/* 00228070(m) (byte-matched C): the Sync poll (busy: 0); 001FED40(port,
 * slot): 0 -> 1; then 2 with the error flag, else that. */
int em_card_00228070(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00228070u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00228070u, 0x30);
    uint32_t r = 0;
    if (sync_poll(s, m) != 0) {
        uint32_t v = 0;
        (void)em_card_001FED40(s, port_of(s, m), slot_of(s, m), &v);
        r = v == 0 ? 1u : 0u;
        if (ui_lw(s, D_00275C58) != 0) r = 2;
    }
    put(result, r);
    return ui_leave(s, fr);
}

/* 001FED40(port, slot) (byte-matched C): phase 0: the phase 0x10 (stored
 * before the call), 00114D18(port, slot) (a nonzero v0 sets the error
 * flag), -1; phase 0x10: the kept result D_00275C64 zero: the phase 0, 0;
 * nonzero: the error flag, -1; any other phase: the error flag, -1. */
int em_card_001FED40(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint32_t *result)
{
    if (begin(s, 0x001FED40u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x001FED40u, 0x20);
    uint32_t r = MINUS1;
    const uint32_t phase = ui_lw(s, D_00275C5C);
    if (phase == 0x10u) {
        if (ui_lw(s, D_00275C64) != 0) {
            ui_sw(s, D_00275C58, 1);
        } else {
            ui_sw(s, D_00275C5C, 0);
            r = 0;
        }
    } else if (phase == 0) {
        ui_sw(s, D_00275C5C, 0x10);
        uint64_t v0 = 0, a[2] = {a0, a1};
        ui_call(s, CD_00114D18, 2, a, 0, NULL, &v0, NULL);
        if (v0 != 0) ui_sw(s, D_00275C58, 1);
    } else {
        ui_sw(s, D_00275C58, 1);
    }
    put(result, r);
    return ui_leave(s, fr);
}

/* 00227270() (byte-matched C): the line 0x1E; Cross: 0020CD40, 1; Circle:
 * 0020CD60, 1; Triangle: 0020CD60, 2; else 0. */
int em_card_00227270(EmArea01Ui *s, uint32_t *result)
{
    if (begin(s, 0x00227270u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00227270u, 0x10);
    uint32_t r = 0;
    line(s, 0x3C, 0x9B, 0x1E);
    const uint32_t p = pad(s);
    if (p & 0x40u) {
        cue(s, CD_0020CD40);
        r = 1;
    } else if (p & 0x20u) {
        cue(s, CD_0020CD60);
        r = 1;
    } else if (p & 0x10u) {
        cue(s, CD_0020CD60);
        r = 2;
    }
    put(result, r);
    return ui_leave(s, fr);
}

/* The common end of 00226B90 and 00226D50: Circle 1, Triangle 2 (each
 * 0020CD60, 001FE8D0), then (after Cross for 00226D50) the Sync poll
 * (busy: 0) and the GetInfo poll's verdict: 1 with the error flag. */
static uint32_t card_still_there(EmArea01Ui *s, uint32_t m)
{
    if (sync_poll(s, m) == 0) return 0;
    const uint32_t v = info(s, port_of(s, m), slot_of(s, m), m, 0, 0);
    if (ui_lw(s, D_00275C58) != 0) return 1;
    return verdict(s, m, v, 0, 1);
}

/* 00226B90(m), the screen for the card check's result 3 (byte-matched C)
 * on m[3]: 0: m[3] = 1; 1: the line 5, Cross: 0020CD40, m[3] = 2; 2: the
 * line 7, Cross: 0020CD40, 001FE8D0, 1. Then Circle: 0020CD60, 001FE8D0,
 * 1; Triangle: the same, 2; the Sync poll and GetInfo's verdict. */
int em_card_00226B90(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00226B90u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00226B90u, 0x20);
    uint32_t r = 0;
    switch (ui_lbu(s, m + 3u)) {
    case 0:
        ui_sb(s, m + 3u, 1);
        break;
    case 1:
        line(s, 0x3C, 0x9B, 5);
        if (pad(s) & 0x40u) {
            cue(s, CD_0020CD40);
            ui_sb(s, m + 3u, 2);
        }
        break;
    case 2:
        line(s, 0x3C, 0x9B, 7);
        if (pad(s) & 0x40u) {
            cue(s, CD_0020CD40);
            (void)em_options_001FE8D0(s);
            r = 1;
            goto out;
        }
        break;
    default:
        break;
    }
    if (pad(s) & 0x20u) {
        cue(s, CD_0020CD60);
        (void)em_options_001FE8D0(s);
        r = 1;
    } else if (pad(s) & 0x10u) {
        cue(s, CD_0020CD60);
        (void)em_options_001FE8D0(s);
        r = 2;
    } else {
        r = card_still_there(s, m);
    }
out:
    put(result, r);
    return ui_leave(s, fr);
}

/* 002267A0(m), the screen for a failed card check or load enumeration
 * (the instructions; NEARMISS C): the line 0xF on port 1 (m+0x48 != 0),
 * else 0xE; Cross: 0020CD40, 001FE8D0, 1; Circle or Triangle: 0020CD60,
 * 001FE8D0, 2; the frame count m+0x10 += 1; the error flag: 00226010(m),
 * 0; the Sync poll (busy: 0). Then m[7] picks the port GetInfo(port, 0,
 * m + 0x58, 0, 0) asks: 0 the chosen port, 1 the other (poll_port; the
 * port's flag is +0x28 for port 0, +0x2C for port 1). Returns 1 once a
 * port answers 0 / -2 / -3 (the SDK's 0, -1, -2) with type 2, else 0. */
/* One poll. The first of a turn (m[7] == 0, `first`): -1 asks again; 0,
 * -2 or -3 with type 2: 001FE8D0, the flag 1, 1; with another type m[7] =
 * 1; any other result: the flag 0 and m[7] = 1. The second (m[7] == 1):
 * -1 asks again; 0: the flag = (type == 2), m[7] = 0; -2 or -3 with type
 * 2: 001FE8D0, the flag 1, 1, with another type m[7] = 0; any other: the
 * flag 0, m[7] = 0. */
static uint32_t poll_port(EmArea01Ui *s, uint32_t m, uint32_t port, int first)
{
    const uint32_t flag = port ? m + 0x2Cu : m + 0x28u;
    const uint32_t v = info(s, port, 0, m, 0, 0);
    if (v == MINUS1) return 0;
    if (first) {
        if (v == 0 || v == 0xFFFFFFFDu || v == 0xFFFFFFFEu) {
            if (ui_lw(s, m + 0x58u) == 2) {
                (void)em_options_001FE8D0(s);
                ui_sw(s, flag, 1);
                return 1;
            }
        } else {
            ui_sw(s, flag, 0);
        }
        ui_sb(s, m + 7u, 1);
        return 0;
    }
    if (v == 0) {
        ui_sw(s, flag, ui_lw(s, m + 0x58u) == 2 ? 1u : 0u);
    } else if (v == 0xFFFFFFFDu || v == 0xFFFFFFFEu) {
        if (ui_lw(s, m + 0x58u) == 2) {
            (void)em_options_001FE8D0(s);
            ui_sw(s, flag, 1);
            return 1;
        }
    } else {
        ui_sw(s, flag, 0);
    }
    ui_sb(s, m + 7u, 0);
    return 0;
}

int em_card_002267A0(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x002267A0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x002267A0u, 0x20);
    uint32_t r = 0;
    line(s, 0x3C, 0x9B, ui_lw(s, m + 0x48u) != 0 ? 0xF : 0xE);
    const uint32_t p = pad(s);
    if (p & 0x40u) {
        cue(s, CD_0020CD40);
        (void)em_options_001FE8D0(s);
        r = 1;
        goto out;
    }
    if (p & 0x30u) {
        cue(s, CD_0020CD60);
        (void)em_options_001FE8D0(s);
        r = 2;
        goto out;
    }
    ui_sw(s, m + 0x10u, ui_lw(s, m + 0x10u) + 1u);
    if (ui_lw(s, D_00275C58) != 0) {
        (void)em_options_00226010(s, m, NULL);
        goto out;
    }
    if (sync_poll(s, m) == 0) goto out;
    {
        const uint32_t chosen = ui_lw(s, m + 0x48u) != 0 ? 1u : 0u;
        const uint32_t turn = ui_lbu(s, m + 7u);
        if (turn == 0)
            r = poll_port(s, m, chosen, 1);
        else if (turn == 1)
            r = poll_port(s, m, chosen ^ 1u, 0);
    }
out:
    put(result, r);
    return ui_leave(s, fr);
}

/* 00226D50(m), the screen for the load enumeration's result 3
 * (byte-matched C): the line 0x28; Cross: 0020CD40, 001FE8D0, 1; Circle:
 * 0020CD60, 001FE8D0, 1; Triangle: the same, 2; then the Sync poll and
 * GetInfo's verdict. */
int em_card_00226D50(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00226D50u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00226D50u, 0x20);
    uint32_t r = 0;
    line(s, 0x3C, 0x9B, 0x28);
    const uint32_t p = pad(s);
    if (p & 0x40u) {
        cue(s, CD_0020CD40);
        (void)em_options_001FE8D0(s);
        r = 1;
    } else if (p & 0x20u) {
        cue(s, CD_0020CD60);
        (void)em_options_001FE8D0(s);
        r = 1;
    } else if (p & 0x10u) {
        cue(s, CD_0020CD60);
        (void)em_options_001FE8D0(s);
        r = 2;
    } else {
        r = card_still_there(s, m);
    }
    put(result, r);
    return ui_leave(s, fr);
}
