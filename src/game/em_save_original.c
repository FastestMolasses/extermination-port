/* em_save_original.c - see em_save_original.h (docs/SAVE_GAME.md).
 *
 * Each routine names the original it translates. 002285A0, 002288A0,
 * 001AF220 and 001FE9E0 are NEARMISS in the decomp and follow the
 * instructions; the others follow the byte-matched C. Callee arguments are
 * the register images the original holds at the call: an addiu / lui-ori
 * constant or an address sum as the sign-extended 32-bit result, a word
 * loaded from memory as loaded (sign-extended), a register passed on as
 * the caller's image. */
#include "game/em_save_original.h"

#include "game/em_area01_ui_internal.h"
#include "game/em_options_original.h"

/* Callees by original address. */
enum {
    SV_00114360 = 0x00114360u, /* libmc Open(port, slot, name, mode) */
    SV_00114448 = 0x00114448u, /* libmc Mkdir(port, slot, name) */
    SV_00114480 = 0x00114480u, /* libmc Close(fd) */
    SV_00114708 = 0x00114708u, /* libmc Write(fd, buf, size) */
    SV_00114AC0 = 0x00114AC0u, /* libmc GetDir(port, slot, name, mode, max, table) */
    SV_00121870 = 0x00121870u, /* block_copy(dst, src, n) */
    SV_00121A28 = 0x00121A28u, /* memset(p, c, n) */
    SV_00123168 = 0x00123168u, /* strcpy(dst, src) */
    SV_001AF2C0 = 0x001AF2C0u  /* New Game's progress reset */
};

/* Original data. */
#define D_00266B30 0x00266B30u /* the icon.sys image (0x3C4 bytes, .data) */
#define D_00267060 0x00267060u /* the five slot file names, 0x40 apart */
#define D_002738E0 0x002738E0u /* the game's directory name */
#define D_002738F8 0x002738F8u /* the header file's title string */
#define D_00273910 0x00273910u /* the header file's name */
#define D_00273940 0x00273940u /* icon.sys's name */
#define D_002739B0 0x002739B0u /* the icon file's name */
#define D_00275840 0x00275840u /* the write side's open descriptor */
#define D_00275C58 0x00275C58u /* the card helpers' error flag */
#define D_00275C5C 0x00275C5Cu /* the card helpers' command phase */
#define D_00275C64 0x00275C64u /* the last Sync result 001FE9A0 kept */
#define D_00275C68 0x00275C68u /* the bytes the last Write wrote */
#define D_0028A4A0 0x0028A4A0u /* the icon container pointer */
#define D_00810700 EM_SAVE_BLOCK
#define D_00810701 0x00810701u
#define D_00810702 0x00810702u
#define D_00810703 0x00810703u
#define D_00810704 0x00810704u
#define D_00810705 0x00810705u
#define D_0081070A 0x0081070Au
#define D_00810730 0x00810730u
#define D_00810750 0x00810750u
#define D_00821500 0x00821500u /* GetDir's table (.bss) */
#define D_00821980 0x00821980u /* the header file's 0x48 bytes (.bss) */
#define D_008219A0 0x008219A0u /* its word at +0x20 */
#define D_008219D0 0x008219D0u /* a slot's staging copy (.bss) */
#define D_00822690 0x00822690u /* 00229680's staging copy (.bss) */
#define SPAD_0000 0x70000000u   /* the New Game block 001AF220 leaves */
#define SPAD_0640 0x70000640u   /* the live block 001AF220 keeps */

#define MINUS1 0xFFFFFFFFu
#define F_TWO 0x40000000u /* 2.0f */

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

/* The record's card port and slot words as loaded. */
static uint64_t port_of(EmArea01Ui *s, uint32_t e) { return ui_sx(ui_lw(s, e + 0x48u)); }
static uint64_t slot_of(EmArea01Ui *s, uint32_t e) { return ui_sx(ui_lw(s, e + 0x4Cu)); }

/* The Sync poll 001FE9A0(1, e + 0x50, e + 0x54) (its translation in the
 * options lane): 00114848's v0 (-1 no command, 0 busy, 1 done). */
static uint32_t sync_poll(EmArea01Ui *s, uint32_t e)
{
    uint32_t v = 0;
    (void)em_options_001FE9A0(s, 1, ui_sx(e + 0x50u), ui_sx(e + 0x54u), &v);
    return v;
}

static void block_copy(EmArea01Ui *s, uint32_t dst, uint32_t src, uint32_t n)
{
    uint64_t a[3] = {ui_sx(dst), ui_sx(src), n};
    ui_call(s, SV_00121870, 3, a, 0, NULL, NULL, NULL);
}

/* ======================================================================
 * The libmc commands on the phase D_00275C5C
 * ====================================================================== */

/* One command routine (frame 0x20): at the phase `phase` the result kept
 * in D_00275C64 is taken (accepted: nonnegative for Open / Write / GetDir,
 * zero for Close / Mkdir; the phase back to 0, and the result or 0
 * returned) or refused (the error flag); at phase 0 the phase is set (the
 * store precedes the call) and the SDK call issued, a nonzero v0 setting
 * the error flag; any other phase sets the error flag. -1 unless a result
 * was accepted. */
static int command(EmArea01Ui *s, uint32_t self, uint32_t phase, uint32_t sdk, unsigned na, const uint64_t *a,
                   int nonneg, uint32_t *result)
{
    if (begin(s, self) < 0) return -1;
    UiFrame fr = ui_enter(s, self, 0x20);
    uint32_t r = MINUS1;
    const uint32_t cur = ui_lw(s, D_00275C5C);
    if (cur == phase) {
        const int32_t last = (int32_t)ui_lw(s, D_00275C64);
        if (nonneg ? last >= 0 : last == 0) {
            ui_sw(s, D_00275C5C, 0);
            r = nonneg ? (uint32_t)last : 0u;
        } else {
            ui_sw(s, D_00275C58, 1);
        }
    } else if (cur == 0) {
        ui_sw(s, D_00275C5C, phase);
        uint64_t v0;
        ui_call(s, sdk, na, a, 0, NULL, &v0, NULL);
        if (v0 != 0) ui_sw(s, D_00275C58, 1);
    } else {
        ui_sw(s, D_00275C58, 1);
    }
    put(result, r);
    return ui_leave(s, fr);
}

/* 001FE9E0(port, slot, name, mode): Open, phase 2 (the instructions; the
 * decomp's NEARMISS C has the same arms). */
int em_save_001FE9E0(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint32_t *result)
{
    const uint64_t a[4] = {a0, a1, a2, a3};
    return command(s, 0x001FE9E0u, 2, SV_00114360, 4, a, 1, result);
}

/* 001FEB90(fd, buf, size): Write, phase 6. */
int em_save_001FEB90(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint32_t *result)
{
    const uint64_t a[3] = {a0, a1, a2};
    return command(s, 0x001FEB90u, 6, SV_00114708, 3, a, 1, result);
}

/* 001FEA70(fd): Close, phase 3. */
int em_save_001FEA70(EmArea01Ui *s, uint64_t a0, uint32_t *result)
{
    const uint64_t a[1] = {a0};
    return command(s, 0x001FEA70u, 3, SV_00114480, 1, a, 0, result);
}

/* 001FEDD0(port, slot, name): Mkdir, phase 0xB. */
int em_save_001FEDD0(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint32_t *result)
{
    const uint64_t a[3] = {a0, a1, a2};
    return command(s, 0x001FEDD0u, 0xB, SV_00114448, 3, a, 0, result);
}

/* 001FEC20(port, slot, name, mode, max, table): GetDir, phase 0xD (the
 * result is the number of entries). */
int em_save_001FEC20(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t t0,
                     uint64_t t1, uint32_t *result)
{
    const uint64_t a[6] = {a0, a1, a2, a3, t0, t1};
    return command(s, 0x001FEC20u, 0xD, SV_00114AC0, 6, a, 1, result);
}

/* 001FE6B0(st, port, slot, name, buf, size), one file on the word *st:
 *   0: Open(port, slot, name, 0x203); a nonnegative descriptor goes to
 *      D_00275840 and *st = 1;
 *   1: Write(D_00275840, buf, size); a nonnegative count goes to
 *      D_00275C68 and *st = 2;
 *   2: Close(D_00275840); on 0: *st = -1, D_00275840 = -1, D_00275C68 = 0
 *      and 1 when the count was `size` (the 64-bit images compared), else
 *      -1;
 * otherwise (and after 0 / 1 / a refused Close) the error flag D_00275C58
 * set gives *st = -1, D_00275840 = -1, D_00275C68 = 0 and -1; clear, 0. */
int em_save_001FE6B0(EmArea01Ui *s, uint64_t st, uint64_t port, uint64_t slot, uint64_t name, uint64_t buf,
                     uint64_t size, uint32_t *result)
{
    if (begin(s, 0x001FE6B0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x001FE6B0u, 0x30);
    const uint32_t at = (uint32_t)st;
    const uint32_t phase = ui_lw(s, at);
    uint32_t v, r = 0;
    int done = 0;
    if (phase == 2) {
        (void)em_save_001FEA70(s, ui_sx(ui_lw(s, D_00275840)), &v);
        if (v == 0) {
            ui_sw(s, at, MINUS1);
            const uint32_t count = ui_lw(s, D_00275C68);
            ui_sw(s, D_00275840, MINUS1);
            ui_sw(s, D_00275C68, 0);
            r = ui_sx(count) == size ? 1u : MINUS1;
            done = 1;
        }
    } else if (phase == 1) {
        (void)em_save_001FEB90(s, ui_sx(ui_lw(s, D_00275840)), buf, size, &v);
        if ((int32_t)v >= 0) {
            ui_sw(s, at, 2);
            ui_sw(s, D_00275C68, v);
        }
    } else if (phase == 0) {
        (void)em_save_001FE9E0(s, port, slot, name, 0x203, &v);
        if ((int32_t)v >= 0) {
            ui_sw(s, D_00275840, v);
            ui_sw(s, at, 1);
        }
    }
    if (!done) {
        if (ui_lw(s, D_00275C58) != 0) {
            ui_sw(s, at, MINUS1);
            ui_sw(s, D_00275840, MINUS1);
            ui_sw(s, D_00275C68, 0);
            r = MINUS1;
        } else {
            r = 0;
        }
    }
    put(result, r);
    return ui_leave(s, fr);
}

/* ======================================================================
 * The block
 * ====================================================================== */

/* 002284F0(p) (a leaf): the 8-bit sum of p[0..0x63E]; then p[0xF] = 1 and
 * p[0x63F] = 0x100 - sum (low byte). The sum is taken before p[0xF] is
 * stored, so the block's bytes sum to zero only when p[0xF] was 1. */
int em_save_002284F0(EmArea01Ui *s, uint32_t p)
{
    if (begin(s, 0x002284F0u) < 0) return -1;
    const uint32_t fn = s->core.function;
    s->core.function = 0x002284F0u;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 0x63Fu && !ui_latched(s); ++i) sum = (sum + ui_lbu(s, p + i)) & 0xFFu;
    ui_sb(s, p + 0xFu, 1);
    ui_sb(s, p + 0x63Fu, 0x100u - sum);
    s->core.function = fn;
    return ui_latched(s) ? -1 : 0;
}

/* 001AF220(): block_copy(0x70000640, D_00810700, 0x640) (the live block
 * kept), 001AF2C0() (New Game's progress reset of the live block),
 * block_copy(0x70000000, D_00810700, 0x640) (the New Game block), then
 * D_00810700 = 0xB, D_00810701 = D_00810702 = 0 and D_00810730[D_00810700]
 * = D_00810701 (both bytes loaded back), and block_copy(D_00810700,
 * 0x70000640, 0x640): the live block back, so those four stores do not
 * survive the call (the instructions; the decomp's NEARMISS comment reads
 * the last copy the other way round). */
int em_save_001AF220(EmArea01Ui *s)
{
    if (begin(s, 0x001AF220u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x001AF220u, 0x10);
    block_copy(s, SPAD_0640, D_00810700, EM_SAVE_BLOCK_SIZE);
    (void)ui_call0(s, SV_001AF2C0);
    block_copy(s, SPAD_0000, D_00810700, EM_SAVE_BLOCK_SIZE);
    ui_sb(s, D_00810700, 0xB);
    ui_sb(s, D_00810701, 0);
    ui_sb(s, D_00810702, 0);
    const uint32_t value = ui_lbu(s, D_00810701);
    const uint32_t index = ui_lbu(s, D_00810700);
    ui_sb(s, D_00810730 + index, value);
    block_copy(s, D_00810700, SPAD_0640, EM_SAVE_BLOCK_SIZE);
    return ui_leave(s, fr);
}

/* The slot header e + 12n + 0x74 from the progress block: bytes 0..5 to
 * +0..+5 and the word 0x50 to +8, the slot byte e[0xA] reloaded for every
 * store, as the original does. */
static void header(EmArea01Ui *s, uint32_t e)
{
    static const uint32_t from[6] = {D_00810700, D_00810701, D_00810702, D_00810703, D_00810704, D_00810705};
    for (uint32_t i = 0; i < 6; ++i) {
        const uint32_t n = ui_lbu(s, e + 0xAu);
        ui_sb(s, e + n * 12u + 0x74u + i, ui_lbu(s, from[i]));
    }
    const uint32_t n = ui_lbu(s, e + 0xAu);
    ui_sw(s, e + n * 12u + 0x7Cu, ui_lw(s, D_00810750));
}

static uint64_t slot_name(EmArea01Ui *s, uint32_t index) { (void)s; return ui_sx(D_00267060 + (index << 6)); }

/* ======================================================================
 * The writer
 * ====================================================================== */

/* 00228530(e): n = 001FEC20(port, slot, D_002738E0, 0, 1, D_00821500); n
 * >= 0: e[3] = 1 for n == 0 (no directory), 2 otherwise. */
int em_save_00228530(EmArea01Ui *s, uint32_t e)
{
    if (begin(s, 0x00228530u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00228530u, 0x20);
    uint32_t n;
    (void)em_save_001FEC20(s, port_of(s, e), slot_of(s, e), D_002738E0, 0, 1, D_00821500, &n);
    if ((int32_t)n >= 0) ui_sb(s, e + 3u, n == 0 ? 1u : 2u);
    return ui_leave(s, fr);
}

/* 00229640(e): 001FE6B0(e + 0x64, port, slot, D_002739B0, c + c[0] +
 * c[0x10], c[0x18]) with c = *D_0028A4A0 (a tail call: no frame). */
int em_save_00229640(EmArea01Ui *s, uint32_t e, uint32_t *result)
{
    if (begin(s, 0x00229640u) < 0) return -1;
    const uint32_t c = ui_lw(s, D_0028A4A0);
    const uint64_t port = port_of(s, e), slot = slot_of(s, e);
    const uint32_t first = ui_lw(s, c), size = ui_lw(s, c + 0x18u), off = ui_lw(s, c + 0x10u);
    return em_save_001FE6B0(s, ui_sx(e + 0x64u), port, slot, D_002739B0, ui_sx(c + first + off), ui_sx(size),
                            result);
}

/* 002285A0(e), a new directory, on the step e[4]:
 *   0: 001FEDD0(port, slot, D_002738E0) == 0: step 1, e[0x64] = 0;
 *   1: 00123168(D_00821980, D_002738F8), D_008219A0 = 2.0f, then
 *      001FE6B0(e + 0x64, port, slot, D_00273910, D_00821980, 0x48) == 1:
 *      step 2, e[0x64] = 0;
 *   2: 00229640(e) == 1: step 3, e[0x64] = 0;
 *   3: 001FE6B0(.., D_00273940, D_00266B30, 0x3C4) == 1: step 4, the
 *      iterator e[0x68] = 0, e[0x64] = 0;
 *   4: the slot k = e[0x68] (a word, compared with the byte e[0xA]): the
 *      chosen slot is 002284F0(D_00810700) and that block; another is
 *      001AF220() and block_copy(D_008219D0, 0x70000000, 0x640) of the New
 *      Game block; 001FE6B0(.., D_00267060 + (k << 6), block, 0x640) == 1:
 *      for the chosen slot e[0x6C + n] = 1 and its header; then e[0x64] =
 *      0, k + 1, and past 4: e[3] = 3, e[4] = e[5] = 0, e[0x64] = 0. */
int em_save_002285A0(EmArea01Ui *s, uint32_t e)
{
    if (begin(s, 0x002285A0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x002285A0u, 0x20);
    uint32_t v;
    switch (ui_lbu(s, e + 4u)) {
    case 0:
        (void)em_save_001FEDD0(s, port_of(s, e), slot_of(s, e), D_002738E0, &v);
        if (v == 0) {
            ui_sb(s, e + 4u, 1);
            ui_sw(s, e + 0x64u, 0);
        }
        break;
    case 1:
        (void)ui_call2(s, SV_00123168, D_00821980, D_002738F8);
        ui_sw(s, D_008219A0, F_TWO);
        (void)em_save_001FE6B0(s, ui_sx(e + 0x64u), port_of(s, e), slot_of(s, e), D_00273910, D_00821980, 0x48,
                               &v);
        if (v == 1) {
            ui_sb(s, e + 4u, 2);
            ui_sw(s, e + 0x64u, 0);
        }
        break;
    case 2:
        (void)em_save_00229640(s, e, &v);
        if (v == 1) {
            ui_sb(s, e + 4u, 3);
            ui_sw(s, e + 0x64u, 0);
        }
        break;
    case 3:
        (void)em_save_001FE6B0(s, ui_sx(e + 0x64u), port_of(s, e), slot_of(s, e), D_00273940, D_00266B30, 0x3C4,
                               &v);
        if (v == 1) {
            ui_sb(s, e + 4u, 4);
            ui_sw(s, e + 0x68u, 0);
            ui_sw(s, e + 0x64u, 0);
        }
        break;
    case 4: {
        uint32_t block;
        if (ui_sx(ui_lw(s, e + 0x68u)) == (uint64_t)ui_lbu(s, e + 0xAu)) {
            (void)em_save_002284F0(s, D_00810700);
            block = D_00810700;
        } else {
            (void)em_save_001AF220(s);
            block_copy(s, D_008219D0, SPAD_0000, EM_SAVE_BLOCK_SIZE);
            block = D_008219D0;
        }
        const uint32_t k = ui_lw(s, e + 0x68u);
        (void)em_save_001FE6B0(s, ui_sx(e + 0x64u), port_of(s, e), slot_of(s, e), slot_name(s, k), block,
                               EM_SAVE_BLOCK_SIZE, &v);
        if (v == 1) {
            if (ui_sx(ui_lw(s, e + 0x68u)) == (uint64_t)ui_lbu(s, e + 0xAu)) {
                ui_sb(s, e + ui_lbu(s, e + 0xAu) + 0x6Cu, 1);
                header(s, e);
            }
            ui_sw(s, e + 0x64u, 0);
            ui_sw(s, e + 0x68u, ui_lw(s, e + 0x68u) + 1u);
            if ((int32_t)ui_lw(s, e + 0x68u) >= 5) {
                ui_sb(s, e + 3u, 3);
                ui_sb(s, e + 4u, 0);
                ui_sb(s, e + 5u, 0);
                ui_sw(s, e + 0x64u, 0);
            }
        }
        break;
    }
    default:
        break;
    }
    return ui_leave(s, fr);
}

/* 002288A0(e), the chosen slot over an existing directory, on e[4]:
 *   0: 002284F0(D_00810700), then 001FE6B0(e + 0x64, port, slot,
 *      D_00267060 + (e[0xA] << 6), D_00810700, 0x640) == 1: 00121A28(the
 *      slot's header, 0, 12), e[0x6C + n] = 1, the header, +6 =
 *      D_0081070A (after the word), step 1;
 *   1: e[3] = 3, e[4] = e[5] = 0, e[0x64] = 0. */
int em_save_002288A0(EmArea01Ui *s, uint32_t e)
{
    if (begin(s, 0x002288A0u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x002288A0u, 0x20);
    const uint32_t step = ui_lbu(s, e + 4u);
    if (step == 1) {
        ui_sb(s, e + 3u, 3);
        ui_sb(s, e + 4u, 0);
        ui_sb(s, e + 5u, 0);
        ui_sw(s, e + 0x64u, 0);
    } else if (step == 0) {
        uint32_t v;
        (void)em_save_002284F0(s, D_00810700);
        (void)em_save_001FE6B0(s, ui_sx(e + 0x64u), port_of(s, e), slot_of(s, e),
                               slot_name(s, ui_lbu(s, e + 0xAu)), D_00810700, EM_SAVE_BLOCK_SIZE, &v);
        if (v == 1) {
            uint64_t a[3] = {ui_sx(e + ui_lbu(s, e + 0xAu) * 12u + 0x74u), 0, 12};
            ui_call(s, SV_00121A28, 3, a, 0, NULL, NULL, NULL);
            ui_sb(s, e + ui_lbu(s, e + 0xAu) + 0x6Cu, 1);
            header(s, e);
            const uint32_t n = ui_lbu(s, e + 0xAu);
            ui_sb(s, e + n * 12u + 0x7Au, ui_lbu(s, D_0081070A));
            ui_sb(s, e + 4u, 1);
        }
    }
    return ui_leave(s, fr);
}

/* 00229680(e): the Sync poll's zero v0 returns 0; else 001AF220(),
 * block_copy(D_00822690, 0x70000000, 0x640) and 001FE6B0(e + 0x64, port,
 * slot, D_00267060 + (e[0xA] << 6), D_00822690, 0x640): 1 gives 1, -1
 * gives 2, any other value 2 when the error flag D_00275C58 is set, else
 * 0. */
int em_save_00229680(EmArea01Ui *s, uint32_t e, uint32_t *result)
{
    if (begin(s, 0x00229680u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00229680u, 0x20);
    uint32_t r = 0;
    if (sync_poll(s, e) != 0) {
        uint32_t v;
        (void)em_save_001AF220(s);
        block_copy(s, D_00822690, SPAD_0000, EM_SAVE_BLOCK_SIZE);
        (void)em_save_001FE6B0(s, ui_sx(e + 0x64u), port_of(s, e), slot_of(s, e),
                               slot_name(s, ui_lbu(s, e + 0xAu)), D_00822690, EM_SAVE_BLOCK_SIZE, &v);
        if (v == 1)
            r = 1;
        else if (v == MINUS1)
            r = 2;
        else
            r = ui_lw(s, D_00275C58) != 0 ? 2u : 0u;
    }
    put(result, r);
    return ui_leave(s, fr);
}

/* 00228A90(e): the Sync poll's zero v0 returns 0; then e[3]: 3 returns 1;
 * 2 runs 002288A0, 1 002285A0, 0 00228530; then (and for any other state)
 * 2 when the error flag D_00275C58 is set, else 0. */
int em_save_00228A90(EmArea01Ui *s, uint32_t e, uint32_t *result)
{
    if (begin(s, 0x00228A90u) < 0) return -1;
    UiFrame fr = ui_enter(s, 0x00228A90u, 0x20);
    uint32_t r = 0;
    if (sync_poll(s, e) != 0) {
        const uint32_t state = ui_lbu(s, e + 3u);
        if (state == 3) {
            r = 1;
        } else {
            if (state == 2)
                (void)em_save_002288A0(s, e);
            else if (state == 1)
                (void)em_save_002285A0(s, e);
            else if (state == 0)
                (void)em_save_00228530(s, e);
            r = ui_lw(s, D_00275C58) != 0 ? 2u : 0u;
        }
    }
    put(result, r);
    return ui_leave(s, fr);
}
