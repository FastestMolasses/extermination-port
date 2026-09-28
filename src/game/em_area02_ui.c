/* Level-4 lane L4UI: the status hub's 2D layer 00209DF0, the marker glow
 * builder 00208750 and the 001D66A0 forwarder 00208AB0. Hand translation
 * of the original functions (boot ELF SCUS-97112); docs/AREA02_UI.md
 * states what each does, how it was verified and where the decomp's
 * NEARMISS C of 00208750 differs from the instructions. Callees are reached
 * through EmArea01Ui.call by original address (em_area02_ui.h). */
#include "game/em_area02_ui.h"

#include "game/em_area01_ui_internal.h"

/* Callees by original address. */
enum {
    A2_00122EF0 = 0x00122EF0u, /* string append (dst, src) */
    A2_00123168 = 0x00123168u, /* string copy (dst, src) */
    A2_001281C0 = 0x001281C0u, /* float_to_int(f12) */
    A2_001C5FB0 = 0x001C5FB0u, /* number formatter (value, width, flags) -> string */
    A2_001CBA50 = 0x001CBA50u, /* text draw */
    A2_001CC1E0 = 0x001CC1E0u, /* label draw */
    A2_001D66A0 = 0x001D66A0u,
    A2_00207D00 = 0x00207D00u, /* blend mode (slot, mode) */
    A2_00207E40 = 0x00207E40u, /* sprite */
    A2_00207F80 = 0x00207F80u, /* rectangle */
    A2_002082B0 = 0x002082B0u, /* arc (slot, descriptor) */
    A2_00208AD0 = 0x00208AD0u, /* health readout */
    A2_00209280 = 0x00209280u, /* battery readout */
    A2_00209860 = 0x00209860u, /* ammunition readout */
    A2_0020AC70 = 0x0020AC70u  /* analog trail */
};

/* Original data addresses. */
#define A2_D_00265160 0x00265160u /* nine (dx, dy) word pairs: 00208750's copy offsets */
#define A2_D_002651B0 0x002651B0u /* arc descriptors 002082B0 reads (0x60 bytes each) */
#define A2_D_00265210 0x00265210u
#define A2_D_00265270 0x00265270u
#define A2_D_002652D0 0x002652D0u
#define A2_D_00265330 0x00265330u
#define A2_D_00265510 0x00265510u /* text style records */
#define A2_D_00265520 0x00265520u
#define A2_D_00265530 0x00265530u
#define A2_D_00265538 0x00265538u
#define A2_D_00265540 0x00265540u /* marker colour rows: normal */
#define A2_D_00265570 0x00265570u /* marker colour rows: selected */
#define A2_D_00267290 0x00267290u /* string pointers */
#define A2_D_00267294 0x00267294u
#define A2_D_002672A4 0x002672A4u
#define A2_D_002672A8 0x002672A8u
#define A2_D_002672AC 0x002672ACu
#define A2_D_002672B0 0x002672B0u
#define A2_D_00273570 0x00273570u
#define A2_D_002862C0 0x002862C0u /* the formatter's scratch string */
#define A2_D_0081085C 0x0081085Cu /* infection, float */
#define A2_D_00810CA4 0x00810CA4u /* ammunition selectors 00209860 reads */
#define A2_D_00810CA6 0x00810CA6u
#define A2_SPAD_38A0 0x700038A0u

#define RGBA_80 UINT64_C(0xFFFFFFFF80808080) /* 0x80808080 as lui/ori leave it */

static void blend(EmArea01Ui *s, uint32_t mode) { (void)ui_call2(s, A2_00207D00, 1, mode); }

static void sprite(EmArea01Ui *s, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t tex)
{
    uint64_t a[7] = {1, ui_sx(x), ui_sx(y), ui_sx(w), ui_sx(h), RGBA_80, tex};
    ui_call(s, A2_00207E40, 7, a, 0, NULL, NULL, NULL);
}

static void text(EmArea01Ui *s, uint32_t target, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t str,
                 uint64_t style)
{
    uint64_t a[7] = {1, ui_sx(x), ui_sx(y), ui_sx(w), ui_sx(h), str, style};
    ui_call(s, target, 7, a, 0, NULL, NULL, NULL);
}

static void rect(EmArea01Ui *s, uint64_t x0, uint64_t y0, uint64_t x1, uint64_t y1, uint64_t rgba)
{
    uint64_t a[6] = {1, x0, y0, x1, y1, rgba};
    ui_call(s, A2_00207F80, 6, a, 0, NULL, NULL, NULL);
}

/* float_to_int(f): v0 as the EE register image. */
static uint64_t f2i(EmArea01Ui *s, uint32_t f)
{
    uint64_t r;
    ui_call(s, A2_001281C0, 0, NULL, 1, &f, &r, NULL);
    return r;
}

/* ------------------------------------------------------------------ */
/* 00208750(n, xy, rgb). Frame 0x150. The 0x48-byte block D_00265160 is
 * copied once to sp + 0x100 (four quadwords loaded before the first store,
 * then the doubleword at +0x40). For each of the nine word pairs (dx, dy)
 * of that copy, in order: the packet slot 1 record (render context
 * D_00275670, cursor word at +0x14, loaded again before each header
 * store) gets byte +3 = 0x10, word +4 = 0, halfword +0 = 2n + 2, the
 * cursor advances by (2n + 3) * 16, and the packet is quadword +0x10 = 0,
 * word +0x1C = (2n + 1) | 0x50000000, doubleword +0x20 = 0x20254000 << 32
 * | 0x8000 | n (n sign-extended), +0x28 = 0x41, then n vertices of 32
 * bytes. t starts at 0 for every copy and steps by 1 / (n - 1) (EE div,
 * computed for every copy, n <= 1 included). With u = 1 - t the weights
 * are u*u, 2*(u*t) and t*t; each vertex is
 *   words 0..3 = float_to_int(weighted sum of the three integer colour
 *                rows rgb + 0, + 0x10, + 0x20, lane k), each halved
 *                (arithmetic shift, read back from the packet) when dx or
 *                dy is nonzero;
 *   words 4, 5 = float_to_int(dx (or dy) + weighted sum of the three
 *                float rows xy + 0, + 0x10, + 0x20, lane 0 (or 1));
 *   word 6 = 0xFFFFFF, word 7 = 0.
 * Each weighted sum is the EE's ADDA of the first two products then MADD
 * with the third. The decomp's NEARMISS C differs (docs section 1). */
int em_area02_ui_00208750(EmArea01Ui *s, int32_t n, uint32_t xy, uint32_t rgb)
{
    if (ui_latched(s)) return -1;
    if (n > 0 && !s->call) return em_a01r_fault(&s->core, A2_001281C0, EM_A01R_FAULT_NULL_WORKER, 0);
    UiFrame fr = ui_enter(s, 0x00208750u, 0x150);
    const uint32_t copy = s->sp + 0x100u;
    uint32_t q[4][4];
    for (uint32_t i = 0; i < 4; ++i) ui_ldq(s, A2_D_00265160 + 16u * i, q[i]);
    for (uint32_t i = 0; i < 4; ++i) ui_stq(s, copy + 16u * i, q[i]);
    const uint32_t tail_lo = ui_lw(s, A2_D_00265160 + 0x40u), tail_hi = ui_lw(s, A2_D_00265160 + 0x44u);
    ui_sd(s, copy + 0x40u, (uint64_t)tail_lo | (uint64_t)tail_hi << 32);

    const uint32_t un = (uint32_t)n;
    const uint32_t count = (2u * un + 2u) & 0xFFFFu;
    const uint32_t size = (2u * un + 3u) << 4;
    const uint32_t flags = (2u * un + 1u) | 0x50000000u;
    const uint64_t tag = (UINT64_C(0x20254000) << 32) | 0x8000u | ui_sx(un);
    const uint32_t zero[4] = {0, 0, 0, 0};

    for (uint32_t j = 0; j < 9 && !ui_latched(s); ++j) {
        const uint32_t ctx = ui_lw(s, UI_D_00275670);
        const int32_t dx = (int32_t)ui_lw(s, copy + 8u * j);
        const int32_t dy = (int32_t)ui_lw(s, copy + 8u * j + 4u);
        ui_sb(s, ui_lw(s, ctx + 0x14u) + 3u, 0x10);
        const uint32_t step = ui_fdiv(UI_F_ONE, ui_fcvt((int32_t)(un - 1u)));
        uint32_t t = 0;
        ui_sw(s, ui_lw(s, ctx + 0x14u) + 4u, 0);
        ui_sh(s, ui_lw(s, ctx + 0x14u), count);
        const uint32_t p = ui_lw(s, ctx + 0x14u);
        ui_sw(s, ctx + 0x14u, p + size);
        ui_stq(s, p + 0x10u, zero);
        ui_sw(s, p + 0x1Cu, flags);
        ui_sd(s, p + 0x20u, tag);
        ui_sd(s, p + 0x28u, 0x41);
        uint32_t d = p + 0x30u;
        for (int32_t i = 0; i < n && !ui_latched(s); ++i) {
            const uint32_t u = ui_fsub(UI_F_ONE, t);
            const uint32_t w0 = ui_fmul(u, u);
            const uint32_t w1 = ui_fmul(UI_F_TWO, ui_fmul(u, t));
            const uint32_t w2 = ui_fmul(t, t);
            for (uint32_t k = 0; k < 4; ++k) {
                const uint32_t c0 = ui_fcvt((int32_t)ui_lw(s, rgb + 4u * k));
                const uint32_t c1 = ui_fcvt((int32_t)ui_lw(s, rgb + 0x10u + 4u * k));
                const uint32_t c2 = ui_fcvt((int32_t)ui_lw(s, rgb + 0x20u + 4u * k));
                const uint32_t acc = em_ee_adda_bits(ui_fmul(c0, w0), ui_fmul(c1, w1));
                ui_sw(s, d + 4u * k, (uint32_t)f2i(s, em_ee_madd_bits(acc, c2, w2)));
            }
            if (dx != 0 || dy != 0)
                for (uint32_t k = 0; k < 4; ++k) ui_sw(s, d + 4u * k, (uint32_t)((int32_t)ui_lw(s, d + 4u * k) >> 1));
            for (uint32_t k = 0; k < 2; ++k) {
                const uint32_t x0 = ui_lw(s, xy + 4u * k);
                const uint32_t x1 = ui_lw(s, xy + 0x10u + 4u * k);
                const uint32_t off = ui_fcvt(k ? dy : dx);
                const uint32_t x2 = ui_lw(s, xy + 0x20u + 4u * k);
                const uint32_t acc = em_ee_adda_bits(ui_fmul(x0, w0), ui_fmul(x1, w1));
                const uint32_t sum = em_ee_madd_bits(acc, x2, w2);
                ui_sw(s, d + 0x10u + 4u * k, (uint32_t)f2i(s, ui_fadd(off, sum)));
            }
            ui_sw(s, d + 0x18u, 0xFFFFFFu);
            t = ui_fadd(t, step);
            ui_sw(s, d + 0x1Cu, 0);
            d += 0x20u;
        }
    }
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 00208AB0(a0, a1, a2) with f12: 001D66A0(1, a0, a1, a2) with the same
 * f12 (a tail jump; the register images pass unchanged, v0 is its result). */
int em_area02_ui_00208AB0(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint32_t f12, uint32_t *v0)
{
    if (ui_latched(s)) return -1;
    UiFrame fr = ui_enter(s, 0x00208AB0u, 0);
    uint64_t a[4] = {1, a0, a1, a2}, r = 0;
    ui_call(s, A2_001D66A0, 4, a, 1, &f12, &r, NULL);
    if (v0) *v0 = (uint32_t)r;
    return ui_leave(s, fr);
}

/* ------------------------------------------------------------------ */
/* 00209DF0(ui), frame 0xE0 (docs section 1 lists the calls in order). */

/* The three (x, y) float rows of one marker, at sp + 0xA0 (lanes 2 and 3
 * of each row are 0). */
static const uint32_t MARKER_ROWS[4][6] = {
    {0x470B0000u, 0x4704A000u, 0x470B0000u, 0x4706C000u, 0x470B0000u, 0x470D0000u}, /* x 35584; y 33952, 34496, 36096 */
    {0x470DC000u, 0x47030000u, 0x47120000u, 0x47030000u, 0x47260000u, 0x47030000u}, /* y 33536; x 36288, 37376, 42496 */
    {0x470B0000u, 0x47016000u, 0x470B0000u, 0x46FE8000u, 0x470B0000u, 0x46F20000u}, /* x 35584; y 33120, 32576, 30976 */
    {0x47084000u, 0x47030000u, 0x47040000u, 0x47030000u, 0x46E00000u, 0x47030000u}, /* y 33536; x 34880, 33792, 28672 */
};

/* The wheel extents (w, h) of iterations 0..3. */
static const uint32_t WHEEL[4][2] = {{0x1B0, 0x178}, {0x1DC, 0x140}, {0x1B0, 0x108}, {0x184, 0x140}};

#define F_16 0x41800000u
#define F_90 0x42B40000u

int em_area02_ui_00209DF0(EmArea01Ui *s, uint32_t ui)
{
    if (ui_latched(s)) return -1;
    UiFrame fr = ui_enter(s, 0x00209DF0u, 0xE0);
    const uint32_t sp = s->sp, rows = sp + 0xA0u;
    const uint64_t ui64 = ui_sx(ui);

    blend(s, 0);
    /* The four marker glows: marker k uses the selected colours when the
     * hover byte ui + 0x11 is k + 1 (read again for each). */
    for (uint32_t k = 0; k < 4; ++k) {
        for (uint32_t r = 0; r < 3; ++r) {
            ui_sw(s, rows + 16u * r, MARKER_ROWS[k][2 * r]);
            ui_sw(s, rows + 16u * r + 4u, MARKER_ROWS[k][2 * r + 1]);
            ui_sw(s, rows + 16u * r + 8u, 0);
            ui_sw(s, rows + 16u * r + 12u, 0);
        }
        const uint32_t table = ui_lbu(s, ui + 0x11u) == k + 1u ? A2_D_00265570 : A2_D_00265540;
        (void)em_area02_ui_00208750(s, 0x10, rows, table);
    }

    /* The analog trail at (432, 272). */
    ui_sw(s, A2_SPAD_38A0, 0x43D80000u);
    ui_sw(s, A2_SPAD_38A0 + 4u, 0x43880000u);
    (void)ui_call3(s, A2_0020AC70, ui64, A2_SPAD_38A0, 0);
    blend(s, 0);

    /* The two spinner arcs and the wheel trio: centre (35584, 33536),
     * angles -44 / 44 stepped by 90 after each pair. */
    ui_sw(s, A2_D_002651B0, 0x470B0000u);
    ui_sw(s, A2_D_002651B0 + 4u, 0x47030000u);
    ui_sw(s, A2_D_00265210, 0x470B0000u);
    ui_sw(s, A2_D_00265210 + 4u, 0x47030000u);
    ui_sw(s, A2_D_002651B0 + 8u, 0xC2300000u);
    ui_sw(s, A2_D_002651B0 + 12u, 0x42300000u);
    ui_sw(s, A2_D_00265210 + 8u, 0xC2300000u);
    ui_sw(s, A2_D_00265210 + 12u, 0x42300000u);
    for (uint32_t i = 0; i < 4 && !ui_latched(s); ++i) {
        (void)ui_call2(s, A2_002082B0, 1, A2_D_002651B0);
        (void)ui_call2(s, A2_002082B0, 1, A2_D_00265210);
        static const uint32_t angles[4] = {A2_D_002651B0 + 8u, A2_D_002651B0 + 12u, A2_D_00265210 + 8u,
                                           A2_D_00265210 + 12u};
        for (uint32_t a = 0; a < 4; ++a) ui_sw(s, angles[a], ui_fadd(ui_lw(s, angles[a]), F_90));
        const uint32_t x = ui_fmul(F_16, ui_fcvt((int32_t)(WHEEL[i][0] + 0x700u)));
        const uint32_t y = ui_fmul(F_16, ui_fcvt((int32_t)((WHEEL[i][1] >> 1) + 0x790u)));
        static const uint32_t trio[3] = {A2_D_00265270, A2_D_002652D0, A2_D_00265330};
        for (uint32_t t = 0; t < 3; ++t) {
            ui_sw(s, trio[t], x);
            ui_sw(s, trio[t] + 4u, y);
        }
        const uint32_t hover = ui_lbu(s, ui + 0x11u);
        const int selected = hover != 0 && i == hover - 1u;
        /* colour words of the third trio record D_00265330 (0x265330..0x26538F):
         * +0x24/+0x28, +0x34/+0x38, +0x44/+0x48 and +0x54/+0x58 */
        const uint32_t c_a = selected ? 0x43700000u : 0x43000000u; /* 240 : 128 */
        const uint32_t c_b = selected ? 0u : 0x437F0000u;          /* 0 : 255 */
        const uint32_t c_c = selected ? 0x43480000u : 0x42800000u; /* 200 : 64 */
        const uint32_t c_d = selected ? 0u : 0x42800000u;          /* 0 : 64 */
        ui_sw(s, 0x00265354u, c_a);
        ui_sw(s, 0x00265358u, c_b);
        ui_sw(s, 0x00265364u, c_c);
        ui_sw(s, 0x00265368u, c_d);
        ui_sw(s, 0x00265374u, c_a);
        ui_sw(s, 0x00265378u, c_b);
        ui_sw(s, 0x00265384u, c_c);
        ui_sw(s, 0x00265388u, c_d);
        for (uint32_t t = 0; t < 3; ++t) (void)ui_call2(s, A2_002082B0, 1, trio[t]);
    }

    /* Health, battery and ammunition readouts. */
    (void)ui_call3(s, A2_00208AD0, ui64, 0xD0, 0xC4);
    {
        uint64_t a[5] = {ui64, 0x10, 0x76, UINT64_C(0x2004512515422288), 0};
        ui_call(s, A2_00209280, 5, a, 0, NULL, NULL, NULL);
    }
    /* 00209860 draws its secondary row with its caller's saved s0 as the
     * texture word when D_00810CA4 != 2 and D_00810CA6 > 4 (code 6). */
    if (!ui_latched(s)) {
        const uint32_t primary = ui_lbu(s, A2_D_00810CA4), secondary = ui_lbu(s, A2_D_00810CA6);
        if (primary != 2 && secondary > 4) ui_unmeasured(s, A2_00209860);
    }
    (void)ui_call3(s, A2_00209860, ui64, 0x10, 0xBE);

    /* The infection figure: 100 draws the full label, anything else the
     * number (width 3) with the percent string appended. */
    const uint64_t n = f2i(s, ui_lw(s, A2_D_0081085C));
    if (n == 0x64) {
        text(s, A2_001CC1E0, 0x822, 0x812, 0xA, 0x14, ui_sx(ui_lw(s, A2_D_00267290)), A2_D_00265520);
    } else {
        text(s, A2_001CC1E0, 0x828, 0x812, 0xA, 0x14, ui_sx(ui_lw(s, A2_D_00267294)), 0);
        uint64_t fmt[3] = {n, 3, 1}, str = 0;
        ui_call(s, A2_001C5FB0, 3, fmt, 0, NULL, &str, NULL);
        (void)ui_call2(s, A2_00123168, A2_D_002862C0, str);
        (void)ui_call2(s, A2_00122EF0, A2_D_002862C0, A2_D_00273570);
        text(s, A2_001CBA50, 0x828, 0x820, 0x10, 0x10, A2_D_002862C0, A2_D_00265510);
    }

    blend(s, 3);
    sprite(s, 0x70B0, 0x8280, 0x20, 0x20, UINT64_C(0x20045EC555422186));
    sprite(s, 0x8E40, 0x8280, 0x20, 0x20, UINT64_C(0x20045EC5554221F0));
    sprite(s, 0x8A10, 0x7950, 0x20, 0x20, UINT64_C(0x20045EC555422192));
    sprite(s, 0x8A10, 0x85B0, 0x20, 0x20, UINT64_C(0x20045EC5554221F4));
    blend(s, 0);
    sprite(s, 0x7000, 0x8300, 0x80, 0x80, UINT64_C(0x20045505DD421D40));
    sprite(s, 0x7100, 0x7900, 0x80, 0x40, UINT64_C(0x200453A59D421E50));
    text(s, A2_001CBA50, 0x710, 0x7AC, 0xC, 0x10, ui_sx(ui_lw(s, A2_D_002672A4)), A2_D_00265538);
    text(s, A2_001CBA50, 0x718, 0x7B5, 0xA, 0xA, ui_sx(ui_lw(s, A2_D_002672A8)), A2_D_00265530);
    text(s, A2_001CBA50, 0x718, 0x7BB, 0xA, 0xA, ui_sx(ui_lw(s, A2_D_002672AC)), A2_D_00265530);
    text(s, A2_001CBA50, 0x718, 0x7C1, 0xA, 0xA, ui_sx(ui_lw(s, A2_D_002672B0)), A2_D_00265530);

    /* Three marker dots at x 28960..29024 (GS 1/16 units), rows 0x4E / 0x54
     * + 12 i half-lines; the four conversions run in the order a, b, c, d. */
    for (uint32_t i = 0; i < 3 && !ui_latched(s); ++i) {
        const int32_t x = 0x4E + 12 * (int32_t)i, y = 0x54 + 12 * (int32_t)i;
        const uint64_t a = f2i(s, 0x46E24000u);
        const uint64_t b = f2i(s, ui_fmul(F_16, ui_fcvt((x >> 1) + 0x790)));
        const uint64_t c = f2i(s, 0x46E2C000u);
        const uint64_t d = f2i(s, ui_fmul(F_16, ui_fcvt((y >> 1) + 0x790)));
        rect(s, a, b, c, d, ui_sx(0x80CE6000u));
    }
    blend(s, 0);
    rect(s, 0x7800, 0x8380, 0x8800, 0x8680, 0x40404040);
    return ui_leave(s, fr);
}
