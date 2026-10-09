/* em_options_original.c - see em_options_original.h (docs/OPTIONS.md).
 *
 * Each routine names the original it translates and, where the decomp's C
 * is a NEARMISS or an asm body, follows the instructions; the points where
 * they differ from that C are said where they occur. Callee arguments are
 * the register images the original holds at the call: an addiu / lui-ori
 * constant sign-extended, an ori constant as it is, a pointer or a value
 * loaded from memory as loaded. */
#include "game/em_options_original.h"

#include "game/em_area01_ui_internal.h"
#include "game/em_startup_load_gaps.h"

/* Callees by original address. */
enum {
    OP_00114848 = 0x00114848u, OP_00114988 = 0x00114988u, OP_00121A28 = 0x00121A28u,
    OP_00122EF0 = 0x00122EF0u, OP_00123168 = 0x00123168u, OP_00123280 = 0x00123280u,
    OP_001232E0 = 0x001232E0u, OP_00123418 = 0x00123418u, OP_001281C0 = 0x001281C0u,
    OP_001AEDE0 = 0x001AEDE0u, OP_001AEE10 = 0x001AEE10u, OP_001B61C0 = 0x001B61C0u,
    OP_001C5FB0 = 0x001C5FB0u, OP_001CBA50 = 0x001CBA50u, OP_001CC170 = 0x001CC170u,
    OP_001CC1E0 = 0x001CC1E0u, OP_001D2830 = 0x001D2830u, OP_001FABB0 = 0x001FABB0u,
    OP_001FBC50 = 0x001FBC50u, OP_001FC770 = 0x001FC770u, OP_001FE480 = 0x001FE480u,
    OP_001FF080 = 0x001FF080u, OP_00200970 = 0x00200970u, OP_00207D00 = 0x00207D00u,
    OP_00207E40 = 0x00207E40u, OP_00207F80 = 0x00207F80u, OP_0020A7A0 = 0x0020A7A0u,
    OP_0020CD40 = 0x0020CD40u, OP_0020CD60 = 0x0020CD60u, OP_0020CDA0 = 0x0020CDA0u,
    OP_002267A0 = 0x002267A0u, OP_00227300 = 0x00227300u
};

/* Original data. */
#define D_00264CB0 0x00264CB0u /* 001FC770's glyph-run config for these lines */
#define D_00264D30 0x00264D30u /* 001FCBD0's 128-byte line template */
#define D_00264E30 0x00264E30u
#define D_00264E34 0x00264E34u
#define D_00264E38 0x00264E38u
#define D_00264F98 0x00264F98u /* 00202D10: three pointers to seven texture words */
#define D_00265510 0x00265510u /* 001CBA50's text style */
#define D_0026C659 0x0026C659u /* the ctype table (+1 past its start) */
#define D_0026EC50 0x0026EC50u /* 001FCBD0's token character set */
#define D_002672C0 0x002672C0u /* the row colours: +8 dim, +0x10 lit texture words */
#define D_002672E0 0x002672E0u /* the action of each of the nine rows */
#define D_00273320 0x00273320u /* 00201F70's x label */
#define D_00273328 0x00273328u /* 00201F70's y label */
#define D_00275828 0x00275828u /* the text style word 001FCBD0 / 001FCE30 set */
#define D_0027582C 0x0027582Cu
#define D_00275840 0x00275840u
#define D_00275844 0x00275844u
#define D_00275BD8 0x00275BD8u /* the screen-module busy byte */
#define D_00275C58 0x00275C58u /* the card helpers' error flag */
#define D_00275C5C 0x00275C5Cu /* 001FECB0 / 001FE920's phase */
#define D_00275C60 0x00275C60u
#define D_00275C64 0x00275C64u /* the last Sync result 001FE9A0 kept */
#define D_00275C68 0x00275C68u
#define D_00275C6C 0x00275C6Cu
#define D_00282157 0x00282157u /* the disc read phase (lb) */
#define D_002862C0 0x002862C0u /* the number string buffer */
#define D_0028A498 0x0028A498u /* the help container pointer */
#define D_0028A9A0 0x0028A9A0u /* the fade state (lh) */
#define D_00810040 EM_OPTIONS_MC_RECORD
#define D_00810118 EM_OPTIONS_SETTINGS
#define D_00810708 0x00810708u
#define D_00810709 0x00810709u
#define D_0081070C 0x0081070Cu
#define D_00810754 0x00810754u
#define D_00810756 0x00810756u
#define D_00810E50 0x00810E50u
#define D_00810E6A 0x00810E6Au
#define SPAD_3B6C 0x70003B6Cu /* the running task's record */
#define SPAD_3B74 0x70003B74u /* the button masks, eight halfwords */
#define SPAD_3B90 0x70003B90u
#define SPAD_3B93 0x70003B93u
#define SPAD_3B94 0x70003B94u /* the screen offset x */
#define SPAD_3B96 0x70003B96u /* the screen offset y */

#define RGBA_80 UINT64_C(0xFFFFFFFF80808080) /* 0x80808080 from lui / ori */
#define RGBA_80CE6000 UINT64_C(0xFFFFFFFF80CE6000)
#define F16 0x41800000u                          /* 16.0f */

static uint64_t tex(uint32_t hi, uint32_t lo) { return (uint64_t)hi << 32 | lo; }

static uint32_t slot(EmArea01Ui *s) { return ui_lw(s, SPAD_3B6C); }

static int begin(EmArea01Ui *s, uint32_t address)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, address, EM_A01R_FAULT_NULL_WORKER, 0);
    return 0;
}

static uint64_t ld(EmArea01Ui *s, uint32_t a) { return (uint64_t)ui_lw(s, a + 4u) << 32 | ui_lw(s, a); }

static void blit(EmArea01Ui *s, uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t rgba, uint64_t t)
{
    uint64_t a[7] = {1, x, y, w, h, rgba, t};
    ui_call(s, OP_00207E40, 7, a, 0, NULL, NULL, NULL);
}

static void blend(EmArea01Ui *s, uint32_t mode) { (void)ui_call2(s, OP_00207D00, 1, mode); }

/* float_to_int(16.0f * (float)n): v0 as the callee returns it. */
static uint64_t x16(EmArea01Ui *s, int32_t n)
{
    const uint32_t f = ui_fmul(F16, ui_fcvt(n));
    uint64_t v0;
    ui_call(s, OP_001281C0, 0, NULL, 1, &f, &v0, NULL);
    return v0;
}

static uint64_t f2i(EmArea01Ui *s, uint32_t f)
{
    uint64_t v0;
    ui_call(s, OP_001281C0, 0, NULL, 1, &f, &v0, NULL);
    return v0;
}

static void cue(EmArea01Ui *s, uint32_t address) { (void)ui_call0(s, address); }

static void background(EmArea01Ui *s, uint64_t t) { (void)ui_call1(s, OP_0020A7A0, t); }

/* ======================================================================
 * The text: 001FCE30 and 001FCBD0
 * ====================================================================== */

/* The line address both look up: entry = tbl + *tbl + row[group * 4],
 * row = tbl + 0x10, then 001FE480(entry, line). The style word is split
 * first: D_00275828 = its low 24 bits, D_0027582C = its high byte. */
static uint64_t text_line(EmArea01Ui *s, uint64_t group, uint64_t line, uint64_t style)
{
    const uint32_t tbl = ui_lw(s, D_0028A498);
    const uint32_t row = ui_lw(s, tbl + 0x10u + ((uint32_t)group << 4));
    const uint32_t entry = tbl + ui_lw(s, tbl) + row;
    ui_sw(s, D_00275828, (uint32_t)(style & 0xFFFFFFu));
    ui_sb(s, D_0027582C, (uint32_t)style >> 24);
    uint64_t v0, a[2] = {ui_sx(entry), line};
    ui_call(s, OP_001FE480, 2, a, 0, NULL, &v0, NULL);
    return v0;
}

static void text_run(EmArea01Ui *s, uint64_t x, uint64_t y, uint64_t text)
{
    uint64_t a[4] = {x, y, text, D_00264CB0};
    ui_call(s, OP_001FC770, 4, a, 0, NULL, NULL, NULL);
}

static void text_glyphs(EmArea01Ui *s, uint64_t x, uint64_t y, uint64_t text)
{
    uint64_t a[7] = {1, x, y, 0xA, 0x14, text, D_00275828};
    ui_call(s, OP_001CC1E0, 7, a, 0, NULL, NULL, NULL);
}

/* 001FCE30: group 8 is centred at 0x800 - width / 2 (001CC170, arithmetic
 * shift) through 001CC1E0; any other group goes to 001FC770(x, y, text,
 * &D_00264CB0). */
int em_options_001FCE30(EmArea01Ui *s, uint64_t x, uint64_t y, uint64_t group, uint64_t line,
                        uint64_t style)
{
    if (begin(s, 0x001FCE30u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x001FCE30u, 0x50);
    const uint64_t text = text_line(s, group, line, style);
    if ((uint32_t)group == 8) {
        const int32_t width = (int32_t)ui_call1(s, OP_001CC170, text);
        text_glyphs(s, ui_sx((uint32_t)(0x800 - (width >> 1))), ui_sx((uint32_t)y + 0x790u), text);
    } else {
        text_run(s, x, y, text);
    }
    return ui_leave(s, fr);
}

/* 001FCBD0. Group 8: the 128-byte template D_00264D30 is copied to sp+0x60;
 * pos = 00123280(text, D_0026EC50) (strcspn); when pos is not the length
 * (001232E0) and the byte after the token character is '7', '8' or '9',
 * the two bytes after it make one byte: (that byte - '0') << 4 | the next
 * one's value (ctype & 4: digit - '0'; & 1: upper hex - 0x37; else lower
 * hex - 0x57), both sign-extended bytes. If the next one is not a hex
 * digit (ctype & 0x44 zero) the line is drawn as it is; else the copy
 * gets text[0..pos) (00123418), the byte, then text from pos + 4
 * (00122EF0), and is drawn. A first byte other than '7'..'9' draws
 * nothing. No token: 001FC770. Any other group: 001FC770. The drawn line
 * goes to 001CC1E0(1, x + 0x700, y + 0x790, 0xA, 0x14, text, &D_00275828). */
int em_options_001FCBD0(EmArea01Ui *s, uint64_t x, uint64_t y, uint64_t group, uint64_t line,
                        uint64_t style)
{
    if (begin(s, 0x001FCBD0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x001FCBD0u, 0xE0);
    const uint64_t text = text_line(s, group, line, style);
    const uint32_t buf = s->sp + 0x60u;
    const uint64_t gx = ui_sx((uint32_t)x + 0x700u), gy = ui_sx((uint32_t)y + 0x790u);
    if ((uint32_t)group != 8) {
        text_run(s, x, y, text);
        return ui_leave(s, fr);
    }
    for (uint32_t i = 0; i < 0x80u; i += 0x10u) ui_00102948(s, buf + i, D_00264D30 + i);
    const uint32_t pos = ui_call2(s, OP_00123280, text, D_0026EC50);
    const uint32_t len = ui_call1(s, OP_001232E0, text);
    if (pos == len) {
        text_run(s, x, y, text);
        return ui_leave(s, fr);
    }
    const uint32_t at = pos + (uint32_t)text;
    const uint32_t c1 = ui_lbu(s, at + 1u);
    if (c1 != 0x37u && c1 - 0x38u >= 2u) return ui_leave(s, fr);
    const int32_t hi = ui_lb(s, at + 1u) - 0x30;
    const int32_t ct = ui_lb(s, D_0026C659 + ui_lbu(s, at + 2u));
    if (!(ct & 0x44)) {
        text_glyphs(s, gx, gy, text);
        return ui_leave(s, fr);
    }
    const int32_t lo = ui_lb(s, at + 2u);
    const int32_t nibble = (ct & 4) ? lo - 0x30 : (ct & 1) ? lo - 0x37 : lo - 0x57;
    const uint32_t byte = (uint32_t)(int32_t)(int8_t)(uint8_t)((uint32_t)(hi << 4) | (uint32_t)nibble);
    (void)ui_call3(s, OP_00123418, ui_sx(buf), text, ui_sx(pos));
    ui_sb(s, pos + s->sp + 0x60u, byte);
    (void)ui_call2(s, OP_00122EF0, ui_sx(buf), ui_sx((uint32_t)text + pos + 4u));
    text_glyphs(s, gx, gy, ui_sx(buf));
    return ui_leave(s, fr);
}

/* ======================================================================
 * 001AF6F0, 001AF1C0, 001AF150, 001AF470
 * ====================================================================== */

/* 001AF470(type) by its one translation (em_slg_001AF470). */
int em_options_001AF470(EmArea01Ui *s, uint32_t type)
{
    if (begin(s, 0x001AF470u) < 0) return -1;
    uint16_t map[8];
    for (uint32_t i = 0; i < 8; ++i) map[i] = (uint16_t)ui_lhu(s, SPAD_3B74 + 2u * i);
    const uint32_t c = type & 0xFFu;
    em_slg_001AF470(map, (int32_t)c);
    if (c <= 2)
        for (uint32_t i = 0; i < 8; ++i) ui_sh(s, SPAD_3B74 + 2u * i, map[i]);
    return ui_latched(s) ? -1 : 0;
}

/* 001AF6F0: a tail jump to 00121A28(D_00810040, 0, 0xD4). */
int em_options_001AF6F0(EmArea01Ui *s)
{
    if (begin(s, 0x001AF6F0u) < 0) return -1;
    (void)ui_call3(s, OP_00121A28, D_00810040, 0, 0xD4);
    return ui_latched(s) ? -1 : 0;
}

/* 001AF1C0: D_00810708 = +0, 709 = +1, 70C = +4, 754 = +8, 756 = +0xA of
 * the settings, then a tail jump to 001AF470(D_00810708). */
int em_options_001AF1C0(EmArea01Ui *s)
{
    if (begin(s, 0x001AF1C0u) < 0) return -1;
    const uint32_t type = ui_lbu(s, D_00810118), vibration = ui_lbu(s, D_00810118 + 1u);
    const uint32_t sound = ui_lbu(s, D_00810118 + 4u);
    const uint32_t ox = ui_lhu(s, D_00810118 + 8u), oy = ui_lhu(s, D_00810118 + 0xAu);
    ui_sb(s, D_00810708, type);
    ui_sb(s, D_00810709, vibration);
    ui_sb(s, D_0081070C, sound);
    ui_sh(s, D_00810754, ox);
    ui_sh(s, D_00810756, oy);
    return em_options_001AF470(s, ui_lbu(s, D_00810708));
}

/* 001AF150: the reverse copy, with the offset also into 0x70003B94 / 96,
 * then a tail jump to 001AF470(type). */
int em_options_001AF150(EmArea01Ui *s)
{
    if (begin(s, 0x001AF150u) < 0) return -1;
    const uint32_t type = ui_lbu(s, D_00810708), vibration = ui_lbu(s, D_00810709);
    const uint32_t sound = ui_lbu(s, D_0081070C);
    const uint32_t ox = ui_lhu(s, D_00810754), oy = ui_lhu(s, D_00810756);
    ui_sb(s, D_00810118, type);
    ui_sb(s, D_00810118 + 1u, vibration);
    ui_sb(s, D_00810118 + 4u, sound);
    ui_sh(s, D_00810118 + 8u, ox);
    ui_sh(s, SPAD_3B94, ox);
    ui_sh(s, D_00810118 + 0xAu, oy);
    ui_sh(s, SPAD_3B96, oy);
    return em_options_001AF470(s, type);
}

/* ======================================================================
 * The list: 0022AEA0
 * ====================================================================== */

#define ROW(x, y, line, rgba) \
    (void)em_options_001FCBD0(s, (x), (y), ui_sx(group), (line), (rgba))

/* 0022AEA0(w, row, open): group = D_00282240; blend 3; the title
 * 001FCE30(0xD0, 0x25, group, 0); then each row lit (0x80808080) when it is
 * `row`, else dim (0x40404040), with its value at x 0x155:
 *   7 exit (line 8);
 *   0 vibration (line 1; dim when D_00810E6A is not 7), its value line
 *     0x28 (on) or 0x29 lit only while open;
 *   1 sound (0xC), its value 2 (stereo, +4 = 0) or 3, lit only while open;
 *   2 screen position (0x17); 3 brightness (0x22);
 *   4 button config (0x25), its value 4, 5 or 6 by +0 (always dim);
 *   9 load (0xB);
 *   5 default (0x26), with open its prompt 0x21 (+3 = 0) or 0x23;
 *   8 quit (0xE), with open its prompt 0x21 (the task's +0x13 = 0) or 0x23;
 * then the legend sprite 00207E40(1, 0x7000, 0x8300, 0x80, 0x80,
 * 0x80808080, 0x20045505DD421D40). */
int em_options_0022AEA0(EmArea01Ui *s, uint32_t w, int32_t row, int32_t open)
{
    if (begin(s, 0x0022AEA0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x0022AEA0u, 0x50);
    const uint32_t group = ui_lw(s, UI_D_00282240);
    const uint64_t lit = RGBA_80, dim = 0x40404040u;
    blend(s, 3);
    (void)em_options_001FCE30(s, 0xD0, 0x25, ui_sx(group), 0, lit);
    ROW(0x6F, 0x47, 8, row == 7 ? lit : dim);
    uint64_t c;
    if (row == 0) {
        if (ui_lhu(s, D_00810E6A) != 7) {
            ROW(0x6F, 0x53, 1, dim);
            c = dim;
        } else {
            ROW(0x6F, 0x53, 1, lit);
            c = open ? lit : dim;
        }
    } else {
        ROW(0x6F, 0x53, 1, dim);
        c = dim;
    }
    ROW(0x155, 0x53, ui_lbu(s, w + 1u) == 1 ? 0x28 : 0x29, c);
    if (row == 1) {
        ROW(0x6F, 0x5F, 0xC, lit);
        c = open ? lit : dim;
    } else {
        ROW(0x6F, 0x5F, 0xC, dim);
        c = dim;
    }
    ROW(0x155, 0x5F, ui_lbu(s, w + 4u) == 0 ? 2 : 3, c);
    ROW(0x6F, 0x6B, 0x17, row == 2 ? lit : dim);
    ROW(0x6F, 0x77, 0x22, row == 3 ? lit : dim);
    ROW(0x6F, 0x83, 0x25, row == 4 ? lit : dim);
    const uint32_t type = ui_lbu(s, w);
    ROW(0x155, 0x83, type == 0 ? 4 : type == 1 ? 5 : 6, dim);
    ROW(0x6F, 0x8F, 0xB, row == 9 ? lit : dim);
    if (row == 5) {
        ROW(0x6F, 0x9B, 0x26, lit);
        if (open) ROW(0x155, 0x9B, ui_lbu(s, w + 3u) == 0 ? 0x21 : 0x23, lit);
    } else {
        ROW(0x6F, 0x9B, 0x26, dim);
    }
    if (row == 8) {
        ROW(0x6F, 0xA7, 0xE, lit);
        if (open) ROW(0x155, 0xA7, ui_lbu(s, slot(s) + 0x13u) == 0 ? 0x21 : 0x23, lit);
    } else {
        ROW(0x6F, 0xA7, 0xE, dim);
    }
    blit(s, 0x7000, 0x8300, 0x80, 0x80, lit, tex(0x20045505u, 0xDD421D40u));
    return ui_leave(s, fr);
}

#undef ROW

/* ======================================================================
 * The row screens
 * ====================================================================== */

/* The row marker the toggles, the default and the quit prompt draw:
 * 00207E40(1, column, float_to_int(16 * (((x + actions[cursor] * 24) >> 1)
 * + 0x790)), 0x20, 0x20, 0x80808080, colours + lit ? 0x10 : 8). */
static void marker(EmArea01Ui *s, uint32_t column, int32_t x, uint32_t colours, uint32_t actions, int lit)
{
    const uint32_t cursor = ui_lhu(s, slot(s) + 0x1Cu);
    const int32_t action = (int32_t)ui_lw(s, actions + 4u * cursor);
    const int32_t n = ((int32_t)((uint32_t)x + (uint32_t)action * 24u) >> 1) + 0x790;
    const uint64_t y = x16(s, n);
    blit(s, column, y, 0x20, 0x20, RGBA_80, ld(s, colours + (lit ? 0x10u : 8u)));
}

/* The blink: +0x1E -= 1, and at 0 the sub-state steps back. */
static void blink(EmArea01Ui *s)
{
    const uint32_t g = slot(s);
    const uint32_t t = (ui_lhu(s, g + 0x1Eu) - 1u) & 0xFFFFu;
    ui_sh(s, g + 0x1Eu, t);
    if (t == 0) {
        const uint32_t g2 = slot(s);
        ui_sb(s, g2 + 0xDu, ui_lbu(s, g2 + 0xDu) - 1u);
    }
}

/* The blink's start: 0020CDA0, the sub-state up, +0x1E = 10. */
static void blink_start(EmArea01Ui *s)
{
    cue(s, OP_0020CDA0);
    const uint32_t g = slot(s);
    ui_sb(s, g + 0xDu, ui_lbu(s, g + 0xDu) + 1u);
    ui_sh(s, slot(s) + 0x1Eu, 0xA);
}

/* 00201720(w, x, colours, actions): the row's action (0 vibration, w+1;
 * else sound, w+4). Sub-state 0 keeps the value in the task's +0x13 and
 * runs on into 1. 1: Right (pressed 0x2000) starts the blink and flips the
 * value (switching vibration on rumbles 001B61C0(1, 0xFF, 4, 1)), then the
 * lit marker; else the dim marker. 2: the blink, the lit marker. The
 * vibration row column is 0x88D0, the sound row's 0x8A40. Then: on the
 * vibration row unless (D_00810E6A is 7 and D_00810E50 is 4), w+1 = +0x13
 * when D_00810E50 is not 4, else 0, and 1; Cross: 0020CD40, 1; Circle or
 * Triangle (0x30): 0020CD60, the value back from +0x13, 2 for Triangle
 * else 1; else 0. */
int em_options_00201720(EmArea01Ui *s, uint32_t w, int32_t x, uint32_t colours, uint32_t actions,
                        uint32_t *result)
{
    if (begin(s, 0x00201720u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00201720u, 0x50);
    uint32_t r = 0;
    const uint32_t g0 = slot(s);
#define ACTION() ((int32_t)ui_lw(s, actions + 4u * ui_lhu(s, slot(s) + 0x1Cu)))
#define COLUMN() (ACTION() == 0 ? 0x88D0u : 0x8A40u)
    switch (ui_lbu(s, g0 + 0xDu)) {
    case 0: {
        ui_sb(s, g0 + 0xDu, ui_lbu(s, g0 + 0xDu) + 1u);
        const uint32_t g2 = slot(s);
        const uint32_t idx = ui_lhu(s, g2 + 0x1Cu);
        ui_sb(s, g2 + 0x13u, (int32_t)ui_lw(s, actions + 4u * idx) == 0 ? ui_lbu(s, w + 1u) : ui_lbu(s, w + 4u));
    }
    /* fall through */
    case 1:
        if (ui_lhu(s, UI_D_00810E74) & 0x2000u) {
            blink_start(s);
            if (ACTION() == 0) {
                ui_sb(s, w + 1u, 1u - ui_lbu(s, w + 1u));
                if (ui_lbu(s, w + 1u) == 1) {
                    uint64_t a[4] = {1, 0xFF, 4, 1};
                    ui_call(s, OP_001B61C0, 4, a, 0, NULL, NULL, NULL);
                }
            } else {
                ui_sb(s, w + 4u, 1u - ui_lbu(s, w + 4u));
            }
            marker(s, COLUMN(), x, colours, actions, 1);
        } else {
            marker(s, COLUMN(), x, colours, actions, 0);
        }
        break;
    case 2:
        blink(s);
        marker(s, COLUMN(), x, colours, actions, 1);
        break;
    default:
        break;
    }
    const uint32_t g = slot(s);
    if ((int32_t)ui_lw(s, actions + 4u * ui_lhu(s, g + 0x1Cu)) == 0 &&
        (ui_lhu(s, D_00810E6A) != 7 || ui_lbu(s, D_00810E50) != 4)) {
        ui_sb(s, w + 1u, ui_lbu(s, D_00810E50) != 4 ? ui_lbu(s, g + 0x13u) : 0u);
        r = 1;
    } else if (ui_lhu(s, UI_D_00810E74) & 0x40u) {
        cue(s, OP_0020CD40);
        r = 1;
    } else if (ui_lhu(s, UI_D_00810E74) & 0x30u) {
        cue(s, OP_0020CD60);
        const uint32_t g2 = slot(s);
        if ((int32_t)ui_lw(s, actions + 4u * ui_lhu(s, g2 + 0x1Cu)) == 0)
            ui_sb(s, w + 1u, ui_lbu(s, g2 + 0x13u));
        else
            ui_sb(s, w + 4u, ui_lbu(s, g2 + 0x13u));
        r = (ui_lhu(s, UI_D_00810E74) & 0x10u) ? 2 : 1;
    }
#undef COLUMN
#undef ACTION
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* 00201C50(w, x, colours, actions), the default prompt (column 0x88D0):
 * sub-state 0 clears w+3 and runs on into 1. 1: Right starts the blink,
 * draws the lit marker, then flips w+3; else the dim marker. 2: the blink,
 * the lit marker. Cross: 0020CD40; with w+3 = 1 the defaults (w+1 = 1 when
 * D_00810E6A is 7 else 0, w+4 = 0, w+0 = 0, 001AF470(0), w+8 = w+0xA = 0,
 * 0x70003B94 = 0x70003B96 = 0); w+3 = 0; 1. Circle or Triangle: 0020CD60,
 * w+3 = 0, 2 for Triangle else 1. Else 0. */
int em_options_00201C50(EmArea01Ui *s, uint32_t w, int32_t x, uint32_t colours, uint32_t actions,
                        uint32_t *result)
{
    if (begin(s, 0x00201C50u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00201C50u, 0x50);
    uint32_t r = 0;
    const uint32_t g0 = slot(s);
    switch (ui_lbu(s, g0 + 0xDu)) {
    case 0:
        ui_sb(s, g0 + 0xDu, ui_lbu(s, g0 + 0xDu) + 1u);
        ui_sb(s, w + 3u, 0);
    /* fall through */
    case 1:
        if (ui_lhu(s, UI_D_00810E74) & 0x2000u) {
            blink_start(s);
            marker(s, 0x88D0u, x, colours, actions, 1);
            ui_sb(s, w + 3u, 1u - ui_lbu(s, w + 3u));
        } else {
            marker(s, 0x88D0u, x, colours, actions, 0);
        }
        break;
    case 2:
        blink(s);
        marker(s, 0x88D0u, x, colours, actions, 1);
        break;
    default:
        break;
    }
    if (ui_lhu(s, UI_D_00810E74) & 0x40u) {
        cue(s, OP_0020CD40);
        if (ui_lbu(s, w + 3u) == 1) {
            ui_sb(s, w + 1u, ui_lhu(s, D_00810E6A) == 7 ? 1u : 0u);
            ui_sb(s, w + 4u, 0);
            ui_sb(s, w, 0);
            (void)em_options_001AF470(s, ui_lbu(s, w));
            ui_sh(s, w + 8u, 0);
            ui_sh(s, w + 0xAu, 0);
            ui_sh(s, SPAD_3B94, 0);
            ui_sh(s, SPAD_3B96, 0);
        }
        ui_sb(s, w + 3u, 0);
        r = 1;
    } else if (ui_lhu(s, UI_D_00810E74) & 0x30u) {
        cue(s, OP_0020CD60);
        ui_sb(s, w + 3u, 0);
        r = (ui_lhu(s, UI_D_00810E74) & 0x10u) ? 2 : 1;
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* The two sprites of a held / idle arrow pair, as 00201F70 draws them. */
static void arrow(EmArea01Ui *s, int32_t bx, int32_t by, uint32_t hi, uint32_t lo)
{
    const int32_t ox = ui_lh(s, SPAD_3B94), oy = ui_lh(s, SPAD_3B96);
    const uint64_t px = x16(s, bx - ox);
    const uint64_t py = x16(s, ((by - oy) >> 1) + 0x790);
    blit(s, px, py, 0x20, 0x20, RGBA_80, tex(hi, lo));
}

/* A number readout: 00123168(D_002862C0, label), 00122EF0(D_002862C0,
 * 001C5FB0(value, 2, 1)), 001CBA50(1, 0x7C8, y, 0x10, 0x10, D_002862C0,
 * D_00265510). */
static void readout(EmArea01Ui *s, uint32_t label, uint32_t spad, int32_t y)
{
    (void)ui_call2(s, OP_00123168, D_002862C0, label);
    const uint32_t number = ui_call3(s, OP_001C5FB0, ui_sx((uint32_t)ui_lh(s, spad)), 2, 1);
    (void)ui_call2(s, OP_00122EF0, D_002862C0, ui_sx(number));
    uint64_t a[7] = {1, 0x7C8, (uint64_t)y, 0x10, 0x10, D_002862C0, D_00265510};
    ui_call(s, OP_001CBA50, 7, a, 0, NULL, NULL, NULL);
}

/* 00201F70(w), screen position: 0020A7A0; sub-state 0 keeps the offset in
 * w+8 / w+0xA. The repeat word moves it one step, each clamped to +-0x14
 * (the step cue only when not clamped): Up (0x1000) y + 1, else Down
 * (0x4000) y - 1; Right (0x2000) x - 1, else Left (0x8000) x + 1 (em_input.h's
 * masks; the offset reaches the picture through main-loop steps R and U,
 * em_display_env_live). Then the
 * title (001FCE30(0x98, 0x2A, group, 0x13)), blend 0, the four corner
 * pieces, the left / right arrow pair (lit by the held Left / Right), the
 * up / down pair (lit by the held Up / Down), the two readouts and the
 * legend. Cross: 0020CD40, the offset into w+8 / w+0xA, 1. Circle or
 * Triangle: 0020CD60, the offset back from w+8 / w+0xA, 2 for Triangle
 * else 1. Else 0. */
int em_options_00201F70(EmArea01Ui *s, uint32_t w, uint32_t *result)
{
    if (begin(s, 0x00201F70u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00201F70u, 0x30);
    uint32_t r = 0;
    background(s, tex(0x20043525u, 0x9D422180u));
    const uint32_t g = slot(s);
    if (ui_lbu(s, g + 0xDu) == 0) {
        ui_sh(s, w + 8u, ui_lhu(s, SPAD_3B94));
        ui_sh(s, w + 0xAu, ui_lhu(s, SPAD_3B96));
        const uint32_t g2 = slot(s);
        ui_sb(s, g2 + 0xDu, ui_lbu(s, g2 + 0xDu) + 1u);
    }
    const uint32_t repeat = ui_lhu(s, UI_D_00810E78);
    if (repeat & 0x1000u) {
        const int32_t y = (int16_t)(ui_lhu(s, SPAD_3B96) + 1u);
        ui_sh(s, SPAD_3B96, (uint32_t)y);
        if (ui_lh(s, SPAD_3B96) > 0x14) ui_sh(s, SPAD_3B96, 0x14);
        else cue(s, OP_0020CDA0);
    } else if (repeat & 0x4000u) {
        const int32_t y = (int16_t)(ui_lhu(s, SPAD_3B96) - 1u);
        ui_sh(s, SPAD_3B96, (uint32_t)y);
        if (ui_lh(s, SPAD_3B96) < -0x14) ui_sh(s, SPAD_3B96, (uint32_t)-0x14);
        else cue(s, OP_0020CDA0);
    }
    const uint32_t repeat2 = ui_lhu(s, UI_D_00810E78);
    if (repeat2 & 0x2000u) {
        ui_sh(s, SPAD_3B94, ui_lhu(s, SPAD_3B94) - 1u);
        if (ui_lh(s, SPAD_3B94) < -0x14) ui_sh(s, SPAD_3B94, (uint32_t)-0x14);
        else cue(s, OP_0020CDA0);
    } else if (repeat2 & 0x8000u) {
        ui_sh(s, SPAD_3B94, ui_lhu(s, SPAD_3B94) + 1u);
        if (ui_lh(s, SPAD_3B94) > 0x14) ui_sh(s, SPAD_3B94, 0x14);
        else cue(s, OP_0020CDA0);
    }
    (void)em_options_001FCE30(s, 0x98, 0x2A, ui_sx(ui_lw(s, UI_D_00282240)), 0x13, RGBA_80);
    blend(s, 0);
    blit(s, 0x7000, 0x7900, 0x100, 0x100, 0x66808080u, tex(0x200438E6u, 0x21421D40u));
    blit(s, 0x7000, 0x8000, 0x100, 0x100, 0x66808080u, tex(0x20043A66u, 0x21421F00u));
    blit(s, 0x8000, 0x7900, 0x100, 0x100, 0x66808080u, tex(0x20043A46u, 0x21421E40u));
    blit(s, 0x8000, 0x8000, 0x100, 0x100, 0x66808080u, tex(0x20043AC6u, 0x21421F40u));
    const uint32_t held = ui_lhu(s, UI_D_00810E70);
    if (held & 0x2000u) {
        arrow(s, 0x8CC, 0xD0, 0x200438C5u, 0x554221C4u);
        arrow(s, 0x714, 0xD0, 0x20043865u, 0x554221B0u);
    } else if (held & 0x8000u) {
        arrow(s, 0x8CC, 0xD0, 0x20043865u, 0x554221A4u);
        arrow(s, 0x714, 0xD0, 0x200438C5u, 0x554221D0u);
    } else {
        arrow(s, 0x8CC, 0xD0, 0x20043865u, 0x554221A4u);
        arrow(s, 0x714, 0xD0, 0x20043865u, 0x554221B0u);
    }
    const uint32_t held2 = ui_lhu(s, UI_D_00810E70);
    if (held2 & 0x1000u) {
        arrow(s, 0x7F0, 0x15, 0x200438C5u, 0x554221D4u);
        arrow(s, 0x7F0, 0x18B, 0x20043865u, 0x554221C0u);
    } else if (held2 & 0x4000u) {
        arrow(s, 0x7F0, 0x15, 0x20043865u, 0x554221B4u);
        arrow(s, 0x7F0, 0x18B, 0x200438C5u, 0x554221E0u);
    } else {
        arrow(s, 0x7F0, 0x15, 0x20043865u, 0x554221B4u);
        arrow(s, 0x7F0, 0x18B, 0x20043865u, 0x554221C0u);
    }
    readout(s, D_00273320, SPAD_3B94, 0x7E0);
    readout(s, D_00273328, SPAD_3B96, 0x7EA);
    blit(s, 0x7BD0, 0x8010, 0x80, 0x80, RGBA_80, tex(0x20043845u, 0xDD422100u));
    const uint32_t pressed = ui_lhu(s, UI_D_00810E74);
    if (pressed & 0x40u) {
        cue(s, OP_0020CD40);
        ui_sh(s, w + 8u, ui_lhu(s, SPAD_3B94));
        ui_sh(s, w + 0xAu, ui_lhu(s, SPAD_3B96));
        r = 1;
    } else if (pressed & 0x30u) {
        cue(s, OP_0020CD60);
        r = (ui_lhu(s, UI_D_00810E74) & 0x10u) ? 2 : 1;
        ui_sh(s, SPAD_3B94, ui_lhu(s, w + 8u));
        ui_sh(s, SPAD_3B96, ui_lhu(s, w + 0xAu));
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* 00202BA0, the brightness picture: 0020A7A0, the title (001FCE30(0x70,
 * 0x20, group, 0x14)), the advice line (001FCBD0(0x89, 0xAF, group,
 * 0x15)), blend 3, the two halves of the bars and the legend. Cross,
 * Circle or Triangle (0x70): 0020CD40, then 2 for Triangle else 1. */
int em_options_00202BA0(EmArea01Ui *s, uint32_t *result)
{
    if (begin(s, 0x00202BA0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00202BA0u, 0x10);
    uint32_t r = 0;
    background(s, tex(0x20043525u, 0x9D422180u));
    (void)em_options_001FCE30(s, 0x70, 0x20, ui_sx(ui_lw(s, UI_D_00282240)), 0x14, RGBA_80);
    (void)em_options_001FCBD0(s, 0x89, 0xAF, ui_sx(ui_lw(s, UI_D_00282240)), 0x15, RGBA_80);
    blend(s, 3);
    blit(s, 0x7000, 0x7B90, 0x100, 0x100, RGBA_80, tex(0x20043AE6u, 0x21422000u));
    blit(s, 0x8000, 0x7B90, 0x100, 0x100, RGBA_80, tex(0x20043C46u, 0x21422040u));
    blit(s, 0x7000, 0x8300, 0x80, 0x80, RGBA_80, tex(0x20043845u, 0xDD422100u));
    if (ui_lhu(s, UI_D_00810E74) & 0x70u) {
        cue(s, OP_0020CD40);
        r = (ui_lhu(s, UI_D_00810E74) & 0x10u) ? 2 : 1;
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* 00202D10(w), button config (w+0 the type 0..2): sub-state 0 keeps the
 * type in +0x13 and runs on into 1; 1: Right (pressed 0x2000) raises it
 * (clamped at 2, the cue only when not clamped), else Left (0x8000) lowers
 * it when nonzero (with the cue); the pad picture's seven icons are
 * D_00264F98[type]. Another sub-state leaves that register as the caller
 * held it (code 6). Then the page: 0020A7A0, the title (001FCE30(0xB0,
 * 0x12, group, 0x16)), blend 0, the pad picture, the three type buttons
 * (the type's lit), the seven action labels 0x1B..0x20 and 0x24, the
 * seven icons and the legend. Cross: 001AF470(type), 0020CD40, 1. Circle
 * or Triangle: 0020CD60, the type back from +0x13, 2 for Triangle else 1.
 * Else 0. */
int em_options_00202D10(EmArea01Ui *s, uint32_t w, uint32_t *result)
{
    if (begin(s, 0x00202D10u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00202D10u, 0x30);
    uint32_t r = 0, table = 0;
    const uint32_t g = slot(s);
    const uint32_t sub = ui_lbu(s, g + 0xDu);
    if (sub == 0) {
        ui_sb(s, g + 0xDu, ui_lbu(s, g + 0xDu) + 1u);
        ui_sb(s, slot(s) + 0x13u, ui_lbu(s, w));
    }
    if (sub <= 1) {
        const uint32_t pressed = ui_lhu(s, UI_D_00810E74);
        if (pressed & 0x2000u) {
            ui_sb(s, w, ui_lbu(s, w) + 1u);
            if (ui_lbu(s, w) >= 3) ui_sb(s, w, 2);
            else cue(s, OP_0020CDA0);
        } else if (pressed & 0x8000u) {
            if (ui_lbu(s, w) != 0) {
                ui_sb(s, w, ui_lbu(s, w) - 1u);
                cue(s, OP_0020CDA0);
            }
        }
        table = ui_lw(s, D_00264F98 + 4u * ui_lbu(s, w));
    } else {
        ui_unmeasured(s, sub);
    }
    background(s, tex(0x20043525u, 0x9D422180u));
    const uint64_t group = ui_sx(ui_lw(s, UI_D_00282240));
    (void)em_options_001FCE30(s, 0xB0, 0x12, group, 0x16, RGBA_80);
    blend(s, 0);
    blit(s, 0x7B40, 0x7F80, 0x100, 0x100, RGBA_80, tex(0x20043206u, 0x21321D00u));
    blit(s, 0x74B0, 0x7BB0, 0x80, 0x40, RGBA_80,
         ui_lbu(s, w) == 0 ? tex(0x20043EE5u, 0x9D422120u) : tex(0x20043385u, 0x9D422150u));
    blit(s, 0x7CB0, 0x7BB0, 0x80, 0x40, RGBA_80,
         ui_lbu(s, w) == 1 ? tex(0x20043305u, 0x9D422130u) : tex(0x200433A5u, 0x9D422160u));
    blit(s, 0x84B0, 0x7BB0, 0x80, 0x40, RGBA_80,
         ui_lbu(s, w) == 2 ? tex(0x20043325u, 0x9D422140u) : tex(0x20043505u, 0x9D422170u));
    static const uint32_t labels[7][2] = {{0x3E, 0x1B}, {0x4A, 0x1C}, {0x56, 0x1D}, {0x62, 0x1E},
                                          {0x6E, 0x1F}, {0x7A, 0x20}, {0x86, 0x24}};
    for (unsigned i = 0; i < 7; ++i)
        (void)em_options_001FCBD0(s, 0x32, labels[i][0], group, labels[i][1], RGBA_80);
    for (uint32_t i = 0; i < 7; ++i)
        blit(s, 0x8AE0, 0x7CE0 + 0xC0u * i, 0x20, 0x20, RGBA_80, ld(s, table + 8u * i));
    blit(s, 0x7000, 0x8300, 0x80, 0x80, RGBA_80, tex(0x20043845u, 0xDD422100u));
    const uint32_t pressed = ui_lhu(s, UI_D_00810E74);
    if (pressed & 0x40u) {
        (void)em_options_001AF470(s, ui_lbu(s, w));
        cue(s, OP_0020CD40);
        r = 1;
    } else if (pressed & 0x30u) {
        cue(s, OP_0020CD60);
        ui_sb(s, w, ui_lbu(s, slot(s) + 0x13u));
        r = (ui_lhu(s, UI_D_00810E74) & 0x10u) ? 2 : 1;
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* 0022B420(w, x, colours, actions), the quit prompt (its choice in the
 * task's +0x13; column 0x88D0): sub-state 0 clears +0x13 and runs on into
 * 1; 1: Right starts the blink, draws the lit marker, then flips +0x13;
 * else the dim marker; 2: the blink, the lit marker. Cross: 0020CD40, 3
 * for Yes (+0x13 = 1) else 1. Circle or Triangle: 0020CD60, 1 for Circle,
 * 2 for Triangle (the instructions; the NEARMISS C swaps them). Else 0. */
int em_options_0022B420(EmArea01Ui *s, uint32_t w, int32_t x, uint32_t colours, uint32_t actions,
                        uint32_t *result)
{
    (void)w;
    if (begin(s, 0x0022B420u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x0022B420u, 0x40);
    uint32_t r = 0;
    const uint32_t g0 = slot(s);
    switch (ui_lbu(s, g0 + 0xDu)) {
    case 0:
        ui_sb(s, g0 + 0xDu, ui_lbu(s, g0 + 0xDu) + 1u);
        ui_sb(s, slot(s) + 0x13u, 0);
    /* fall through */
    case 1:
        if (ui_lhu(s, UI_D_00810E74) & 0x2000u) {
            blink_start(s);
            marker(s, 0x88D0u, x, colours, actions, 1);
            const uint32_t g = slot(s);
            ui_sb(s, g + 0x13u, 1u - ui_lbu(s, g + 0x13u));
        } else {
            marker(s, 0x88D0u, x, colours, actions, 0);
        }
        break;
    case 2: {
        /* the blink on the record the state test read (a3 holds it) */
        const uint32_t t = (ui_lhu(s, g0 + 0x1Eu) - 1u) & 0xFFFFu;
        ui_sh(s, g0 + 0x1Eu, t);
        if (t == 0) {
            const uint32_t g = slot(s);
            ui_sb(s, g + 0xDu, ui_lbu(s, g + 0xDu) - 1u);
        }
        marker(s, 0x88D0u, x, colours, actions, 1);
        break;
    }
    default:
        break;
    }
    const uint32_t pressed = ui_lhu(s, UI_D_00810E74);
    if (pressed & 0x40u) {
        cue(s, OP_0020CD40);
        r = ui_lbu(s, slot(s) + 0x13u) == 1 ? 3 : 1;
    } else if (pressed & 0x30u) {
        cue(s, OP_0020CD60);
        r = (ui_lhu(s, UI_D_00810E74) & 0x10u) ? 2 : 1;
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* ======================================================================
 * 0022A590 and 0022A650
 * ====================================================================== */

/* 0022A590, state 10's module load on the task's +0xD: 0 stops the sounds
 * (001FBC50) and 001FABB0, then +0xD += 1; 1 while D_00282157 is 0:
 * +0xD += 1, D_00275BD8 = 1, 001FF080(0, 0x2B) (its a2, the state byte's
 * address, is not read by 001FF080); 2 returns 1 once D_00275BD8 is 0.
 * Else 0. */
int em_options_0022A590(EmArea01Ui *s, uint32_t *result)
{
    if (begin(s, 0x0022A590u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x0022A590u, 0x10);
    uint32_t r = 0;
    const uint32_t g = slot(s);
    const uint32_t st = ui_lbu(s, g + 0xDu);
    if (st == 0) {
        cue(s, OP_001FBC50);
        cue(s, OP_001FABB0);
        const uint32_t g2 = slot(s);
        ui_sb(s, g2 + 0xDu, ui_lbu(s, g2 + 0xDu) + 1u);
    } else if (st == 1) {
        if (ui_lb(s, D_00282157) == 0) {
            ui_sb(s, g + 0xDu, ui_lbu(s, g + 0xDu) + 1u);
            ui_sb(s, D_00275BD8, 1);
            (void)ui_call2(s, OP_001FF080, 0, 0x2B);
        }
    } else if (st == 2) {
        if (ui_lbu(s, D_00275BD8) == 0) r = 1;
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* The cursor's wraps: forward to 0 at 9, backward to 8 when its low byte
 * (lb) is negative. */
static void cursor_step(EmArea01Ui *s, int forward)
{
    const uint32_t g = slot(s);
    if (forward) {
        ui_sh(s, g + 0x1Cu, ui_lhu(s, g + 0x1Cu) + 1u);
        if (ui_lhu(s, g + 0x1Cu) >= 9) ui_sh(s, g + 0x1Cu, 0);
    } else {
        ui_sh(s, g + 0x1Cu, ui_lhu(s, g + 0x1Cu) - 1u);
        if (ui_lb(s, g + 0x1Cu) < 0) ui_sh(s, g + 0x1Cu, 8);
    }
}

static int32_t row_action(EmArea01Ui *s)
{
    return (int32_t)ui_lw(s, D_002672E0 + 4u * ui_lhu(s, slot(s) + 0x1Cu));
}

static void set_state(EmArea01Ui *s, uint32_t state)
{
    const uint32_t g = slot(s);
    ui_sb(s, g + 0xCu, state);
    ui_sb(s, g + 0xDu, 0);
}

/* 0022A650: 001D2830(3, 1), then the task's state +0xC:
 *   0: D_002821B4 = 0, D_002821B0 = 4, D_00282240 = 8, state 1, the cursor
 *      and +0x12 cleared; on into 1.
 *   1: vibration off (w+1 = 0) when D_00810E6A is not 7; 0020A7A0; the
 *      repeat word moves the cursor (Down 0x4000 forward, Up 0x1000 back,
 *      each with the cue 0020CDA0 and, when D_00810E6A is not 7, a second
 *      step over the vibration row; with neither, an idle cursor on that
 *      row steps forward without the wrap); the list (0022AEA0(w, action,
 *      0)); Cross (0020CD40) acts on the row: exit sets the result 1;
 *      vibration (only with D_00810E6A 7) and sound go to state 5; screen
 *      position, brightness and button config to state 10 with +0x13 = 7,
 *      8 or 9; load clears D_00810040 (001AF6F0) and goes to state 3;
 *      default to 6; quit to 4 with +0x13 = 0. The result 1, Circle,
 *      Triangle or SELECT: 0020CD60 and state 12.
 *   2: 00200970(1), state 1.   11: 00200970(1), state 12.   12: returns 1.
 *   3: 00225AC0(0): 1 -> state 2 and 001AEE10(8, 0); 2 returns 2.
 *   4: 0020A7A0, the list open, 0022B420: 3 returns 3, 1 -> state 1, any
 *      other nonzero -> 12.
 *   5 / 6: 0020A7A0, the list open, 00201720 / 00201C50: 1 -> state 1,
 *      2 -> 12.
 *   7 / 8 / 9: 00201F70 / 00202BA0 / 00202D10: 1 -> state 2, 2 -> 11.
 *   10: 0022A590: when it returns nonzero, state +0x13 (+0xD = 0).
 * Every state change except 10's and 0's clears +0xD as written above. */
int em_options_0022A650(EmArea01Ui *s, uint32_t *result)
{
    if (begin(s, 0x0022A650u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x0022A650u, 0x30);
    const uint32_t w = D_00810118;
    uint32_t hit = 0, r = 0;
    (void)ui_call2(s, OP_001D2830, 3, 1);
    const uint64_t bg = tex(0x20045EE5u, 0x9D421E40u);
    switch (ui_lbu(s, slot(s) + 0xCu)) {
    case 0: {
        ui_sw(s, UI_D_002821B4, 0);
        ui_sw(s, UI_D_002821B0, 4);
        ui_sw(s, UI_D_00282240, 8);
        const uint32_t g = slot(s);
        ui_sb(s, g + 0xCu, ui_lbu(s, g + 0xCu) + 1u);
        ui_sh(s, slot(s) + 0x1Cu, 0);
        ui_sb(s, slot(s) + 0x12u, 0);
    }
    /* fall through */
    case 1: {
        if (ui_lhu(s, D_00810E6A) != 7) ui_sb(s, w + 1u, 0);
        background(s, bg);
        const uint32_t repeat = ui_lhu(s, UI_D_00810E78);
        if (repeat & 0x4000u) {
            cue(s, OP_0020CDA0);
            cursor_step(s, 1);
            if (ui_lhu(s, D_00810E6A) != 7 && row_action(s) == 0) cursor_step(s, 1);
        } else if (repeat & 0x1000u) {
            cue(s, OP_0020CDA0);
            cursor_step(s, 0);
            if (ui_lhu(s, D_00810E6A) != 7 && row_action(s) == 0) cursor_step(s, 0);
        } else if (ui_lhu(s, D_00810E6A) != 7 && row_action(s) == 0) {
            const uint32_t g = slot(s);
            ui_sh(s, g + 0x1Cu, ui_lhu(s, g + 0x1Cu) + 1u);
        }
        (void)em_options_0022AEA0(s, w, row_action(s), 0);
        if (ui_lhu(s, UI_D_00810E74) & 0x40u) {
            cue(s, OP_0020CD40);
            switch (row_action(s)) {
            case 7:
                hit = 1;
                break;
            case 0:
                if (ui_lhu(s, D_00810E6A) != 7) break;
                set_state(s, 5);
                break;
            case 1:
                set_state(s, 5);
                break;
            case 2:
                ui_sb(s, slot(s) + 0x13u, 7);
                set_state(s, 0xA);
                break;
            case 3:
                ui_sb(s, slot(s) + 0x13u, 8);
                set_state(s, 0xA);
                break;
            case 4:
                ui_sb(s, slot(s) + 0x13u, 9);
                set_state(s, 0xA);
                break;
            case 9:
                (void)em_options_001AF6F0(s);
                set_state(s, 3);
                break;
            case 5:
                set_state(s, 6);
                break;
            case 8:
                set_state(s, 4);
                ui_sb(s, slot(s) + 0x13u, 0);
                break;
            default:
                break;
            }
        }
        const uint32_t pressed = ui_lhu(s, UI_D_00810E74);
        if (hit == 1 || (pressed & 0x20u) || (ui_lhu(s, UI_D_00810E74) & 0x10u) ||
            (ui_lhu(s, UI_D_00810E74) & 0x100u)) {
            cue(s, OP_0020CD60);
            ui_sb(s, slot(s) + 0xCu, 0xC);
        }
        r = hit;
        break;
    }
    case 2:
        (void)ui_call1(s, OP_00200970, 1);
        ui_sb(s, slot(s) + 0xCu, 1);
        break;
    case 3: {
        uint32_t v = 0;
        (void)em_options_00225AC0(s, 0, &v);
        if (v == 1) {
            set_state(s, 2);
            (void)ui_call2(s, OP_001AEE10, 8, 0);
        } else if (v == 2) {
            r = 2;
        }
        break;
    }
    case 4: {
        background(s, bg);
        (void)em_options_0022AEA0(s, w, row_action(s), 1);
        uint32_t v = 0;
        (void)em_options_0022B420(s, w, 0x86, D_002672C0, D_002672E0, &v);
        if (v != 0) {
            if (v == 3) r = 3;
            else ui_sb(s, slot(s) + 0xCu, v == 1 ? 1 : 0xC);
        }
        break;
    }
    case 5:
    case 6: {
        const uint32_t st = ui_lbu(s, slot(s) + 0xCu);
        background(s, bg);
        (void)em_options_0022AEA0(s, w, row_action(s), 1);
        uint32_t v = 0;
        if (st == 5) (void)em_options_00201720(s, w, 0x9E, D_002672C0, D_002672E0, &v);
        else (void)em_options_00201C50(s, w, 0xB6, D_002672C0, D_002672E0, &v);
        if (v == 1) ui_sb(s, slot(s) + 0xCu, 1);
        else if (v == 2) ui_sb(s, slot(s) + 0xCu, 0xC);
        break;
    }
    case 7:
    case 8:
    case 9: {
        const uint32_t st = ui_lbu(s, slot(s) + 0xCu);
        uint32_t v = 0;
        if (st == 7) (void)em_options_00201F70(s, w, &v);
        else if (st == 8) (void)em_options_00202BA0(s, &v);
        else (void)em_options_00202D10(s, w, &v);
        if (v == 1) ui_sb(s, slot(s) + 0xCu, 2);
        else if (v == 2) ui_sb(s, slot(s) + 0xCu, 0xB);
        break;
    }
    case 10: {
        uint32_t v = 0;
        (void)em_options_0022A590(s, &v);
        if (v != 0) {
            const uint32_t g = slot(s);
            ui_sb(s, g + 0xCu, ui_lbu(s, g + 0x13u));
            ui_sb(s, slot(s) + 0xDu, 0);
        }
        break;
    }
    case 11:
        (void)ui_call1(s, OP_00200970, 1);
        ui_sb(s, slot(s) + 0xCu, 0xC);
        break;
    case 12:
        r = 1;
        break;
    default:
        break;
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* ======================================================================
 * The memory-card screen
 * ====================================================================== */

/* 00225AC0(mode) over its record m = D_00810040:
 *   m[0] 0: m[1] 0: mode 0: m[0x14] = 2, 001FBC50, 001FABB0, m[1] += 1;
 *              mode 1: m[0x14] = 1, m[1] = 2;
 *           1: once D_00282157 is 0, m[1] += 1, and on into 2;
 *           2: m[1] += 1, D_00275BD8 = 1, 001FF080(0, 0x2A);
 *           3: once D_00275BD8 is 0: m[0] += 1, m[1] = 0, 001AEE10(8, 0).
 *   m[0] 1: 0020A7A0, the frame 00225720(m), m[0x16] = 00225D20(m); a
 *           nonzero result: 001AEDE0(8, 0), m[0] = 2.
 *   m[0] 2: 0020A7A0, the frame; once D_0028A9A0 is 2: 00200970(1) when
 *           0x70003B90 is 2, then m[0] = 3.
 *   m[0] 3: returns m[0x16].   Anything else returns 0. */
int em_options_00225AC0(EmArea01Ui *s, uint32_t mode, uint32_t *result)
{
    if (begin(s, 0x00225AC0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00225AC0u, 0x20);
    const uint32_t m = D_00810040;
    const uint64_t bg = tex(0x20044C05u, 0x9D4221E0u);
    uint32_t r = 0;
    switch (ui_lbu(s, m)) {
    case 0:
        switch (ui_lbu(s, m + 1u)) {
        case 0:
            if (mode == 0) {
                ui_sb(s, m + 0x14u, 2);
                cue(s, OP_001FBC50);
                cue(s, OP_001FABB0);
                ui_sb(s, m + 1u, ui_lbu(s, m + 1u) + 1u);
            } else if (mode == 1) {
                ui_sb(s, m + 0x14u, 1);
                ui_sb(s, m + 1u, 2);
            }
            break;
        case 1:
            if (ui_lb(s, D_00282157) != 0) break;
            ui_sb(s, m + 1u, ui_lbu(s, m + 1u) + 1u);
        /* fall through */
        case 2:
            ui_sb(s, m + 1u, ui_lbu(s, m + 1u) + 1u);
            ui_sb(s, D_00275BD8, 1);
            (void)ui_call2(s, OP_001FF080, 0, 0x2A);
            break;
        case 3:
            if (ui_lbu(s, D_00275BD8) == 0) {
                ui_sb(s, m, ui_lbu(s, m) + 1u);
                ui_sb(s, m + 1u, 0);
                (void)ui_call2(s, OP_001AEE10, 8, 0);
            }
            break;
        default:
            break;
        }
        break;
    case 1: {
        background(s, bg);
        (void)em_options_00225720(s, m);
        uint32_t v = 0;
        (void)em_options_00225D20(s, m, &v);
        ui_sb(s, m + 0x16u, v);
        if ((v & 0xFFu) != 0) {
            (void)ui_call2(s, OP_001AEDE0, 8, 0);
            ui_sb(s, m, 2);
        }
        break;
    }
    case 2:
        background(s, bg);
        (void)em_options_00225720(s, m);
        if (ui_lh(s, D_0028A9A0) == 2) {
            if (ui_lbu(s, SPAD_3B90) == 2) (void)ui_call1(s, OP_00200970, 1);
            ui_sb(s, m, 3);
        }
        break;
    case 3:
        r = ui_lbu(s, m + 0x16u);
        break;
    default:
        break;
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* 00225720(m), the card screen's frame (the asm body; its readable C):
 * blend 0, the four background quarters, blend 3, the corner badge, the
 * mode's title (load m[0x14] = 2, else save) and, when the slot screen's
 * state m[0x15] is 2, the highlight bar at the row m[0xA]. */
int em_options_00225720(EmArea01Ui *s, uint32_t m)
{
    if (begin(s, 0x00225720u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00225720u, 0x20);
    const uint64_t quarter = 0x40808080u, bright = 0x70808080u;
    blend(s, 0);
    blit(s, 0x7000, 0x7F00, 0x100, 0x100, quarter, tex(0x20043F06u, 0x21321F00u));
    blit(s, 0x8000, 0x7F00, 0x100, 0x100, quarter, tex(0x20043F86u, 0x21321F40u));
    blit(s, 0x7000, 0x7A90, 0x100, 0x100, quarter, tex(0x20043E86u, 0x21321D40u));
    blit(s, 0x8000, 0x7A90, 0x100, 0x100, quarter, tex(0x20043E06u, 0x21321D00u));
    blend(s, 3);
    blit(s, 0x8800, 0x7900, 0x80, 0x80, bright, tex(0x20044A05u, 0xDD422180u));
    if (ui_lbu(s, m + 0x14u) == 2)
        blit(s, 0x7080, 0x7900, 0x100, 0x40, bright, tex(0x20044085u, 0x9D3221C0u));
    else
        blit(s, 0x7080, 0x7900, 0x100, 0x40, bright, tex(0x20044005u, 0x9D3221A0u));
    if (ui_lbu(s, m + 0x15u) == 2) {
        const int32_t n = ((int32_t)(ui_lbu(s, m + 0xAu) * 24u + 0x72u) >> 1) + 0x790;
        const uint64_t y = x16(s, n);
        blit(s, 0x7000, y, 0x100, 0x40, quarter, tex(0x20044405u, 0xA1322100u));
        const uint64_t y2 = x16(s, n);
        blit(s, 0x8000, y2, 0x100, 0x40, quarter, tex(0x20044485u, 0xA1322140u));
    }
    return ui_leave(s, fr);
}

/* 002256E0 / 00225700: tail jumps to 00207F80(1, x0, 0x8550, x1, 0x85B0,
 * 0x80CE6000), the Yes / No marker of the save prompt. */
static int prompt_marker(EmArea01Ui *s, uint32_t address, uint32_t x0, uint32_t x1)
{
    if (begin(s, address) < 0) return -1;
    uint64_t a[6] = {1, x0, 0x8550, x1, 0x85B0, RGBA_80CE6000};
    ui_call(s, OP_00207F80, 6, a, 0, NULL, NULL, NULL);
    return ui_latched(s) ? -1 : 0;
}

int em_options_002256E0(EmArea01Ui *s) { return prompt_marker(s, 0x002256E0u, 0x77E0, 0x78A0); }
int em_options_00225700(EmArea01Ui *s) { return prompt_marker(s, 0x00225700u, 0x7CD0, 0x7D90); }

/* 00225A20: D_0081004A = 0, the words D_00810060..84 and D_008100A8 = 0,
 * then 00121A28(D_008100AC, 0, 8) and 00121A28(D_008100B4, 0, 0x60). */
int em_options_00225A20(EmArea01Ui *s)
{
    if (begin(s, 0x00225A20u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00225A20u, 0x10);
    ui_sb(s, 0x0081004Au, 0);
    for (uint32_t a = 0x00810060u; a <= 0x00810084u; a += 4u) ui_sw(s, a, 0);
    ui_sw(s, 0x008100A8u, 0);
    (void)ui_call3(s, OP_00121A28, 0x008100ACu, 0, 8);
    (void)ui_call3(s, OP_00121A28, 0x008100B4u, 0, 0x60);
    return ui_leave(s, fr);
}

/* 00225CF0(m, lo, hi): m[lo..hi] = 0, unsigned bounds. */
int em_options_00225CF0(EmArea01Ui *s, uint32_t m, uint32_t lo, uint32_t hi)
{
    if (begin(s, 0x00225CF0u) < 0) return -1;
    for (uint32_t i = lo; i <= hi && !ui_latched(s); ++i) ui_sb(s, m + i, 0);
    return ui_latched(s) ? -1 : 0;
}

/* 001FE8D0: D_00264E30 / 34 / 38 and D_00275C58..6C = 0, D_00275840 =
 * D_00275844 = -1. */
int em_options_001FE8D0(EmArea01Ui *s)
{
    if (begin(s, 0x001FE8D0u) < 0) return -1;
    ui_sw(s, D_00264E30, 0);
    ui_sw(s, D_00264E34, 0);
    ui_sw(s, D_00275C58, 0);
    ui_sw(s, D_00275C5C, 0);
    ui_sw(s, D_00264E38, 0);
    ui_sw(s, D_00275C60, 0);
    ui_sw(s, D_00275C64, 0);
    ui_sw(s, D_00275C68, 0);
    ui_sw(s, D_00275C6C, 0);
    ui_sw(s, D_00275840, 0xFFFFFFFFu);
    ui_sw(s, D_00275844, 0xFFFFFFFFu);
    return ui_latched(s) ? -1 : 0;
}

/* 001FE9A0(mode, cmd, res): 00114848(mode, cmd, res) (the SDK's Sync); on
 * 1 (done) D_00275C64 = *res. Returns 00114848's v0. */
int em_options_001FE9A0(EmArea01Ui *s, uint64_t mode, uint64_t cmd, uint64_t res, uint32_t *result)
{
    if (begin(s, 0x001FE9A0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x001FE9A0u, 0x20);
    const uint32_t v = ui_call3(s, OP_00114848, mode, cmd, res);
    if (v == 1) ui_sw(s, D_00275C64, ui_lw(s, (uint32_t)res));
    if (result) *result = v;
    return ui_leave(s, fr);
}

/* 001FECB0(a0..t0), the GetInfo poll on the phase D_00275C5C:
 *   0: phase 1, 00114988(a0..t0) (GetInfo); a nonzero result sets the
 *      error flag D_00275C58 = 1; -1 either way;
 *   1: D_00275C64 nonzero: the error flag, D_00275C64 - 1; zero: phase 0,
 *      and 0;
 *   other: the error flag, -1. */
int em_options_001FECB0(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t t0,
                        uint32_t *result)
{
    if (begin(s, 0x001FECB0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x001FECB0u, 0x20);
    uint32_t r = 0xFFFFFFFFu;
    const uint32_t phase = ui_lw(s, D_00275C5C);
    if (phase == 1) {
        const uint32_t last = ui_lw(s, D_00275C64);
        if (last != 0) {
            r = last - 1u;
            ui_sw(s, D_00275C58, 1);
        } else {
            ui_sw(s, D_00275C5C, 0);
            r = 0;
        }
    } else if (phase == 0) {
        ui_sw(s, D_00275C5C, 1);
        uint64_t v0, a[5] = {a0, a1, a2, a3, t0};
        ui_call(s, OP_00114988, 5, a, 0, NULL, &v0, NULL);
        if ((uint32_t)v0 != 0) ui_sw(s, D_00275C58, 1);
    } else {
        ui_sw(s, D_00275C58, 1);
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* 001FE920(a0, a1): a phase at or above 0 becomes -1; then -1: phase -2
 * and 00114988(a0, a1, 0, 0, 0), -1; -2: the error flag and the phase 0,
 * 0; else -1. */
int em_options_001FE920(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint32_t *result)
{
    if (begin(s, 0x001FE920u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x001FE920u, 0x20);
    uint32_t r = 0xFFFFFFFFu;
    if ((int32_t)ui_lw(s, D_00275C5C) >= 0) ui_sw(s, D_00275C5C, 0xFFFFFFFFu);
    const int32_t phase = (int32_t)ui_lw(s, D_00275C5C);
    if (phase == -2) {
        ui_sw(s, D_00275C58, 0);
        ui_sw(s, D_00275C5C, 0);
        r = 0;
    } else if (phase == -1) {
        ui_sw(s, D_00275C5C, 0xFFFFFFFEu);
        uint64_t a[5] = {a0, a1, 0, 0, 0};
        ui_call(s, OP_00114988, 5, a, 0, NULL, NULL, NULL);
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* 00226010(m): 001FE9A0(1, m + 0x50, m + 0x54); a zero result returns 0;
 * else 001FE920(m[0x48], m[0x4C]) and 1 when it returns 0, else 0. */
int em_options_00226010(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00226010u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00226010u, 0x20);
    uint32_t v = 0, r = 0;
    (void)em_options_001FE9A0(s, 1, m + 0x50u, m + 0x54u, &v);
    if (v != 0) {
        uint32_t w = 0;
        (void)em_options_001FE920(s, ui_sx(ui_lw(s, m + 0x48u)), ui_sx(ui_lw(s, m + 0x4Cu)), &w);
        r = w == 0 ? 1 : 0;
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* 00226070's GetInfo poll of one slot: 001FECB0(port, 0, m + 0x58, 0, 0). */
static uint32_t poll_slot(EmArea01Ui *s, uint32_t m, uint32_t port)
{
    uint32_t v = 0;
    (void)em_options_001FECB0(s, port, 0, ui_sx(m + 0x58u), 0, 0, &v);
    return v;
}

/* 00226070(m), the card screen's slot step on m[1]:
 *   0 (the poll): the prompt line 001FCBD0(0x3C, 0x9B, 7, 9, 0x70808080);
 *     with the error flag D_00275C58: 00226010(m), 0. Sync (001FE9A0(1,
 *     m + 0x50, m + 0x54)) busy (0): 0. Then on m[2] (0 slot 1, 1 slot 2)
 *     the GetInfo poll 001FECB0(port, 0, m + 0x58, 0, 0): 0 -> the slot's
 *     flag m+0x28 / m+0x2C = (type == 2) and m[2] = 1 (slot 1) or m[1] =
 *     2, m[2] = 0 (slot 2); -1 -> 0; -2 / -3 -> 001FE8D0; other -> the
 *     flag 0 and the same step. Returns 0.
 *   1: 002267A0(m) (past the recordings: a callee): 1 -> m[1] = 2; 2 -> 2;
 *     anything else 0 (the NEARMISS C returns the value itself).
 *   2 (the slot choice): the lines (no card: 0xB; else 0xA, then slot 1's
 *     line 3 with its bar when m[0x48] is 0, slot 2's line 4 with its bar
 *     when m[0x48] is 1); with both cards Up / Down (pressed 0x1000 /
 *     0x4000) move m[0x48] between 0 and 1 (cue); one card selects it;
 *     the button is kept in m[0x17] (Cross 0x40, else Circle 0x20, else
 *     Triangle 0x10); the error flag: 00226010(m), 0; Sync busy: 0;
 *     Circle or Triangle: 0020CD60, 001FE8D0, m[0x17] = 0, 2; Cross:
 *     0020CD40, m[0x17] = 0 and, with a card, 001FE8D0 and 1; then the
 *     re-poll of the slot m[0x24] & 1 (GetInfo) as in state 0 into its
 *     flag, and m[0x24] += 1 unless the poll returned -1. Returns 0.
 *   3, 4 and any other: 0. */
int em_options_00226070(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00226070u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00226070u, 0x50);
    const uint64_t line_rgba = 0x70808080u;
    uint32_t r = 0;
    const uint32_t st = ui_lbu(s, m + 1u);
    uint32_t sync = 0;
    switch (st) {
    case 0: {
        (void)em_options_001FCBD0(s, 0x3C, 0x9B, 7, 9, line_rgba);
        if (ui_lw(s, D_00275C58) != 0) {
            (void)em_options_00226010(s, m, NULL);
            break;
        }
        (void)em_options_001FE9A0(s, 1, m + 0x50u, m + 0x54u, &sync);
        if (sync == 0) break;
        const uint32_t sub = ui_lbu(s, m + 2u);
        if (sub > 1) break;
        const uint32_t flag = sub == 0 ? m + 0x28u : m + 0x2Cu;
        const uint32_t v = poll_slot(s, m, sub);
        if (v == 0) {
            if (sub == 0) {
                ui_sb(s, m + 2u, 1);
                ui_sw(s, flag, ui_lw(s, m + 0x58u) == 2 ? 1u : 0u);
            } else {
                ui_sb(s, m + 1u, 2);
                ui_sb(s, m + 2u, 0);
                ui_sw(s, flag, ui_lw(s, m + 0x58u) == 2 ? 1u : 0u);
            }
        } else if (v == 0xFFFFFFFFu) {
        } else if (v == 0xFFFFFFFEu || v == 0xFFFFFFFDu) {
            (void)em_options_001FE8D0(s);
        } else if (sub == 0) {
            ui_sb(s, m + 2u, 1);
            ui_sw(s, flag, 0);
        } else {
            ui_sb(s, m + 1u, 2);
            ui_sw(s, flag, 0);
        }
        break;
    }
    case 1: {
        const uint32_t v = ui_call1(s, OP_002267A0, m);
        if (v == 1) ui_sb(s, m + 1u, 2);
        else if (v == 2) r = 2;
        break;
    }
    case 2: {
        if (ui_lw(s, m + 0x28u) == 0 && ui_lw(s, m + 0x2Cu) == 0) {
            (void)em_options_001FCBD0(s, 0x3C, 0x9B, 7, 0xB, line_rgba);
        } else {
            (void)em_options_001FCBD0(s, 0x3C, 0x9B, 7, 0xA, line_rgba);
            if (ui_lw(s, m + 0x28u) != 0) {
                (void)em_options_001FCBD0(s, 0x64, 0xAF, 7, 3, line_rgba);
                if (ui_lw(s, m + 0x48u) == 0) {
                    const uint64_t c0 = f2i(s, 0x46EA0000u), c1 = f2i(s, 0x47041000u);
                    const uint64_t c2 = f2i(s, 0x46EB8000u), c3 = f2i(s, 0x47047000u);
                    uint64_t a[6] = {1, c0, c1, c2, c3, RGBA_80CE6000};
                    ui_call(s, OP_00207F80, 6, a, 0, NULL, NULL, NULL);
                }
            }
            if (ui_lw(s, m + 0x2Cu) != 0) {
                (void)em_options_001FCBD0(s, 0x64, 0xB9, 7, 4, line_rgba);
                if (ui_lw(s, m + 0x48u) == 1) {
                    const uint64_t c0 = f2i(s, 0x46EA0000u), c1 = f2i(s, 0x4704B000u);
                    const uint64_t c2 = f2i(s, 0x46EB8000u), c3 = f2i(s, 0x47051000u);
                    uint64_t a[6] = {1, c0, c1, c2, c3, RGBA_80CE6000};
                    ui_call(s, OP_00207F80, 6, a, 0, NULL, NULL, NULL);
                }
            }
        }
        const uint32_t one = ui_lw(s, m + 0x28u), two = ui_lw(s, m + 0x2Cu);
        if (one != 0 && two != 0) {
            if ((ui_lhu(s, UI_D_00810E74) & 0x1000u) && (int32_t)ui_lw(s, m + 0x48u) > 0) {
                cue(s, OP_0020CDA0);
                ui_sw(s, m + 0x48u, ui_lw(s, m + 0x48u) - 1u);
            }
            if ((ui_lhu(s, UI_D_00810E74) & 0x4000u) && (int32_t)ui_lw(s, m + 0x48u) <= 0) {
                cue(s, OP_0020CDA0);
                ui_sw(s, m + 0x48u, ui_lw(s, m + 0x48u) + 1u);
            }
        } else if (one != 0) {
            ui_sw(s, m + 0x48u, 0);
        } else if (two != 0) {
            ui_sw(s, m + 0x48u, 1);
        }
        const uint32_t pressed = ui_lhu(s, UI_D_00810E74);
        if (pressed & 0x40u) ui_sb(s, m + 0x17u, 0x40);
        else if (pressed & 0x20u) ui_sb(s, m + 0x17u, 0x20);
        else if (pressed & 0x10u) ui_sb(s, m + 0x17u, 0x10);
        if (ui_lw(s, D_00275C58) != 0) {
            (void)em_options_00226010(s, m, NULL);
            break;
        }
        (void)em_options_001FE9A0(s, 1, m + 0x50u, m + 0x54u, &sync);
        if (sync == 0) break;
        const uint32_t button = ui_lbu(s, m + 0x17u);
        if (button & 0x20u || button & 0x10u) {
            cue(s, OP_0020CD60);
            (void)em_options_001FE8D0(s);
            ui_sb(s, m + 0x17u, 0);
            r = 2;
            break;
        }
        if (button & 0x40u) {
            cue(s, OP_0020CD40);
            ui_sb(s, m + 0x17u, 0);
            if (ui_lw(s, m + 0x28u) != 0 || ui_lw(s, m + 0x2Cu) != 0) {
                (void)em_options_001FE8D0(s);
                r = 1;
                break;
            }
        }
        const uint32_t second = ui_lw(s, m + 0x24u) & 1u;
        const uint32_t flag = second ? m + 0x2Cu : m + 0x28u;
        const uint32_t v = poll_slot(s, m, second);
        if (v != 0xFFFFFFFFu) {
            if (v == 0) ui_sw(s, flag, ui_lw(s, m + 0x58u) == 2 ? 1u : 0u);
            else if (v == 0xFFFFFFFDu || v == 0xFFFFFFFEu) (void)em_options_001FE8D0(s);
            else ui_sw(s, flag, 0);
            ui_sw(s, m + 0x24u, ui_lw(s, m + 0x24u) + 1u);
        }
        break;
    }
    default:
        break;
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}

/* 00225D20(m), the slot screen on m[0x15]:
 *   0: with the save prompt m[0x19] (AREA21's end-of-game save): its two
 *      lines (001FCBD0(0x3C, 0x9B, 7, 0x21) and (0x8C, 0xC3, 7, 2)), the
 *      Yes / No marker by m[9] (002256E0 / 00225700); Cross on Yes:
 *      0020CD40, 2; Cross on No: m[0x19] = 0, 0020CD40, 0; Circle or
 *      Triangle: 0020CD60, m[0x19] = 0; Right on Yes / Left on No move m[9]
 *      (cue). Without it: m[0x15] = 1, 00225A20, 00225CF0(m, 1, 7).
 *   1: 00226070(m): 1 -> m[0x15] += 1, 00225CF0(m, 1, 7), m+0x20 = 0;
 *      2 -> with 0x70003B93 set m[0x19] = 1 and m[0x15] -= 1, else
 *      m[0x15] = 3.
 *   2: 00227300(m) (past the recordings: a callee): 1 -> m[0x15] -= 1,
 *      00225CF0(m, 1, 7), 00225A20; 2 -> as state 1's 2, with m[0x15] = 0;
 *      3 returns 2.
 *   3: m[0x15] = 0, 1.   Else 0. */
int em_options_00225D20(EmArea01Ui *s, uint32_t m, uint32_t *result)
{
    if (begin(s, 0x00225D20u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00225D20u, 0x20);
    const uint64_t line_rgba = 0x70808080u;
    uint32_t r = 0;
    const uint32_t st = ui_lbu(s, m + 0x15u);
    switch (st) {
    case 0:
        if (ui_lbu(s, m + 0x19u) != 0) {
            (void)em_options_001FCBD0(s, 0x3C, 0x9B, 7, 0x21, line_rgba);
            (void)em_options_001FCBD0(s, 0x8C, 0xC3, 7, 2, line_rgba);
            const uint32_t choice = ui_lbu(s, m + 9u);
            if (choice == 0) {
                (void)em_options_002256E0(s);
                if (ui_lhu(s, UI_D_00810E74) & 0x40u) {
                    cue(s, OP_0020CD40);
                    r = 2;
                    break;
                }
            } else if (choice == 1) {
                (void)em_options_00225700(s);
                if (ui_lhu(s, UI_D_00810E74) & 0x40u) {
                    ui_sb(s, m + 0x19u, 0);
                    cue(s, OP_0020CD40);
                    break;
                }
            }
            const uint32_t pressed = ui_lhu(s, UI_D_00810E74);
            if (pressed & 0x20u) {
                cue(s, OP_0020CD60);
                ui_sb(s, m + 0x19u, 0);
            } else if (pressed & 0x10u) {
                cue(s, OP_0020CD60);
                ui_sb(s, m + 0x19u, 0);
            } else {
                if ((pressed & 0x2000u) && ui_lbu(s, m + 9u) == 0) {
                    cue(s, OP_0020CDA0);
                    ui_sb(s, m + 9u, 1);
                }
                if ((ui_lhu(s, UI_D_00810E74) & 0x8000u) && ui_lbu(s, m + 9u) != 0) {
                    cue(s, OP_0020CDA0);
                    ui_sb(s, m + 9u, 0);
                }
            }
        } else {
            ui_sb(s, m + 0x15u, st + 1u);
            (void)em_options_00225A20(s);
            (void)em_options_00225CF0(s, m, 1, 7);
        }
        break;
    case 1: {
        uint32_t v = 0;
        (void)em_options_00226070(s, m, &v);
        if (v == 1) {
            ui_sb(s, m + 0x15u, ui_lbu(s, m + 0x15u) + 1u);
            (void)em_options_00225CF0(s, m, 1, 7);
            ui_sw(s, m + 0x20u, 0);
        } else if (v == 2) {
            if (ui_lbu(s, SPAD_3B93) != 0) {
                ui_sb(s, m + 0x19u, 1);
                ui_sb(s, m + 0x15u, ui_lbu(s, m + 0x15u) - 1u);
            } else {
                ui_sb(s, m + 0x15u, 3);
            }
        }
        break;
    }
    case 2: {
        const uint32_t v = ui_call1(s, OP_00227300, m);
        if (v == 1) {
            ui_sb(s, m + 0x15u, ui_lbu(s, m + 0x15u) - 1u);
            (void)em_options_00225CF0(s, m, 1, 7);
            (void)em_options_00225A20(s);
        } else if (v == 2) {
            if (ui_lbu(s, SPAD_3B93) != 0) {
                ui_sb(s, m + 0x19u, 1);
                ui_sb(s, m + 0x15u, 0);
            } else {
                ui_sb(s, m + 0x15u, 3);
            }
        } else if (v == 3) {
            r = 2;
        }
        break;
    }
    case 3:
        ui_sb(s, m + 0x15u, 0);
        r = 1;
        break;
    default:
        break;
    }
    if (result) *result = r;
    return ui_leave(s, fr);
}
