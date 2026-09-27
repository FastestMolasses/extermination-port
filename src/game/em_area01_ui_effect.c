/* AREA01 lane UI: the effect driver 0022BBC0 and its helpers 0022B7A0,
 * 0022B700 and 0022BB70. Hand translation of the original functions (boot
 * ELF SCUS-97112); docs/AREA01_UI.md. The census label "ui_credits" is not
 * evidence: the only capture that runs 0022BBC0 is the a01_s3 fire contact,
 * where its node (seq +0x24 = the player block 0x8102B0) drives the
 * player's bones. Callees go through EmArea01Ui.call by original address. */
#include "game/em_area01_ui_internal.h"

/* ------------------------------------------------------------------ */
/* 0022BB70(i): the word at *D_00275B40 + (i / 13) * 4, plus (i % 13) * 16
 * (C division: truncating). */
static uint32_t ring_slot(EmArea01Ui *s, int32_t i)
{
    UiFrame fr = ui_enter(s, 0x0022BB70u, 0);
    const uint32_t row = ui_lw(s, ui_lw(s, UI_D_00275B40) + (uint32_t)(i / 13) * 4u);
    (void)ui_leave(s, fr);
    return row + ((uint32_t)(i % 13) << 4);
}

int em_area01_ui_0022BB70(EmArea01Ui *s, int32_t a0, uint32_t *v0)
{
    if (ui_latched(s)) return -1;
    const uint32_t r = ring_slot(s, a0);
    if (v0) *v0 = r;
    return ui_latched(s) ? -1 : 0;
}

/* 0022B700(p, n): p[0xC] = n; above the halfword cap D_00275BCC: p[4] = 3,
 * result 0; else p+0x110[k] = 001AF780(p[0xC]) for k < p[0xC] (p[0xC] read
 * again each time), 001CB5B0(p[0xC]), p[9] = p[0xC], p[4] = 1, result 1. */
static uint32_t bone_slots(EmArea01Ui *s, uint32_t p, uint32_t n)
{
    UiFrame fr = ui_enter(s, 0x0022B700u, 0x40);
    ui_sb(s, p + 0xCu, n);
    if (ui_lh(s, UI_D_00275BCC) < (int32_t)ui_lbu(s, p + 0xCu)) {
        ui_sb(s, p + 4u, 3);
        (void)ui_leave(s, fr);
        return 0;
    }
    for (uint32_t k = 0; (int32_t)k < (int32_t)ui_lbu(s, p + 0xCu) && !ui_latched(s); ++k)
        ui_sw(s, p + 0x110u + 4u * k, ui_call1(s, UI_001AF780, ui_lbu(s, p + 0xCu)));
    (void)ui_call1(s, UI_001CB5B0, ui_lbu(s, p + 0xCu));
    ui_sb(s, p + 9u, ui_lbu(s, p + 0xCu));
    ui_sb(s, p + 4u, 1);
    (void)ui_leave(s, fr);
    return 1;
}

int em_area01_ui_0022B700(EmArea01Ui *s, uint32_t p, uint32_t n, uint32_t *v0)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, 0x0022B700u, EM_A01R_FAULT_NULL_WORKER, 0);
    const uint32_t r = bone_slots(s, p, n & 0xFFu);
    if (v0) *v0 = r;
    return ui_latched(s) ? -1 : 0;
}

/* 0022B7A0(e): picks the timeline table for e (q = the word e+0x24, the
 * table stored at e+0x1F0+0x90):
 *   e[0xD] 2..8   fixed tables 0x2677B0, 0x2677D0, 0x267870, 0x2678A0,
 *                 0x2678D0, 0x2678F0, 0x267910
 *   e[0xD] 0      by q[3]: 12 and 1 -> 0x267310, 4 -> 0x267470, 5 ->
 *                 0x2673B0, 6 -> 0x267540, 7 -> 0x267660, 16 -> 0x267760
 *   e[0xD] 1      by q[3]: 0 -> 0x267E00, 5 -> 0x267F30, 7 -> 0x267F90,
 *                 12 and 1 -> 0x268020 when the byte D_008101E4 == 3, else
 *                 0x267E30 / 0x267EB0 by bit 0 of q[0xD] (set / clear)
 *   e[0xD] 9      q[2] & ~0xE0 == 0 -> 0x267940; else by q[3]: 0 0x267980,
 *                 1 0x2679C0, 2 0x267C00, 3 0x267940, 4 0x267AA0, 5
 *                 0x267A30, 6 0x267AF0, 7 0x267B90, 9 0x267D60, 10
 *                 0x267C80, 11 0x267CF0, 16 0x267DA0, 17 0x267D90, 18
 *                 0x267DD0
 * Any other case returns 0 (nothing stored). Otherwise the 8-byte records
 * are counted up to and including the first whose halfword +2 is 8; that
 * count goes to e+0x1F0+0x98 and the result is 1. */
static uint32_t pick_table(EmArea01Ui *s, uint32_t e)
{
    UiFrame fr = ui_enter(s, 0x0022B7A0u, 0);
    const uint32_t q = ui_lw(s, e + 0x24u);
    const uint32_t base = e + 0x1F0u;
    uint32_t t = 0;
    const uint32_t kind = ui_lbu(s, e + 0xDu);
    static const uint32_t fixed[7] = {0x002677B0u, 0x002677D0u, 0x00267870u, 0x002678A0u,
                                      0x002678D0u, 0x002678F0u, 0x00267910u};
    if (kind >= 2 && kind <= 8) {
        t = fixed[kind - 2];
    } else if (kind == 0) {
        switch (ui_lbu(s, q + 3u)) {
        case 12: case 1: t = 0x00267310u; break;
        case 4: t = 0x00267470u; break;
        case 5: t = 0x002673B0u; break;
        case 6: t = 0x00267540u; break;
        case 7: t = 0x00267660u; break;
        case 16: t = 0x00267760u; break;
        default: break;
        }
    } else if (kind == 1) {
        switch (ui_lbu(s, q + 3u)) {
        case 0: t = 0x00267E00u; break;
        case 5: t = 0x00267F30u; break;
        case 7: t = 0x00267F90u; break;
        case 12: case 1:
            if (ui_lbu(s, 0x008101E4u) == 3)
                t = 0x00268020u;
            else
                t = (ui_lbu(s, q + 0xDu) & 1u) ? 0x00267E30u : 0x00267EB0u;
            break;
        default: break;
        }
    } else if (kind == 9) {
        if ((ui_lbu(s, q + 2u) & ~0xE0u) == 0) {
            t = 0x00267940u;
        } else {
            switch (ui_lbu(s, q + 3u)) {
            case 0: t = 0x00267980u; break;
            case 1: t = 0x002679C0u; break;
            case 4: t = 0x00267AA0u; break;
            case 5: t = 0x00267A30u; break;
            case 6: t = 0x00267AF0u; break;
            case 7: t = 0x00267B90u; break;
            case 3: t = 0x00267940u; break;
            case 2: t = 0x00267C00u; break;
            case 10: t = 0x00267C80u; break;
            case 11: t = 0x00267CF0u; break;
            case 9: t = 0x00267D60u; break;
            case 16: t = 0x00267DA0u; break;
            case 17: t = 0x00267D90u; break;
            case 18: t = 0x00267DD0u; break;
            default: break;
            }
        }
    }
    if (t == 0) {
        (void)ui_leave(s, fr);
        return 0;
    }
    ui_sw(s, base + 0x90u, t);
    uint32_t m = ui_lw(s, base + 0x90u);
    int32_t i = 0;
    while (!ui_latched(s)) {
        if (ui_lh(s, m + 2u) == 8) {
            ui_sw(s, base + 0x98u, (uint32_t)(i + 1));
            break;
        }
        m += 8u;
        ++i;
    }
    (void)ui_leave(s, fr);
    return 1;
}

int em_area01_ui_0022B7A0(EmArea01Ui *s, uint32_t e, uint32_t *v0)
{
    if (ui_latched(s)) return -1;
    const uint32_t r = pick_table(s, e);
    if (v0) *v0 = r;
    return ui_latched(s) ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* 0022BBC0(seq): a pool callback. scn = the word seq+0x24, w = seq+0x1F0.
 * By seq[4]:
 *   2, 3   001AFC10(seq, seq[4]).
 *   0      0022B7A0(seq) == 0: seq[4] = 3; else w[i] = w[i + 0x38] = 0 for
 *          i < scn[0xC]; 0022B700(seq, 5) == 0 ends the call; the halfword
 *          +0xE of the 65 ring slots (0022BB70) = -1; w+0x94 = 00122BB8();
 *          w+0x9C, +0xA0, +0xA4, +0xB0, +0xAC = 0; seq+0x80 = the quadword
 *          scn+0x80; then on into 1.
 *   1      scn[4] == 3: seq[4] = 3. Else:
 *          the timeline: every 8-byte record of w+0x90 (w+0x98 of them)
 *          whose halfword +0 equals the clock w+0xAC runs its op (halfword
 *          +2; +4 an index, +6 a value): 0 (seq[0xD] 0) the scn+0x60..68
 *          ramp to (1, 0.1, 1) over +6 frames; 1 the scroll count w+0xA4 =
 *          +6, step w+0xA8 = 0x1000 / +6 + 4; 2 w[+4] = 1, w[+4 + 0x38] =
 *          the low byte of +6; 3 (seq[0xD] 0) bone +4's halfwords 0x88..8C
 *          = 0; 4 (seq[0xD] 0) scn[4] = 3; 5 the scn+0x80..8C ramp to (0.2,
 *          0.8, 0.2, 1) for seq[0xD] 0, (0.1, 0.1, 0.1, 1) for 1 and 3; 6
 *          (seq[0xD] 9, 3, 1) the ramp back to seq+0x80..8C; 7 (seq[0xD]
 *          1) 001CA6E0(scn, D_0028A490[+6]); 8 seq[4] = 3.
 *          the ramps: w+0x9C frames of w+0x70..7C onto scn+0x80..8C,
 *          w+0xA0 frames of w+0x80..88 onto scn+0x60..68, w+0xA4 frames
 *          of halfword w+0xA8 off each bone's +0x8A (not below 0).
 *          the emitters: every bone i with w[i] != 0 sets its offsets
 *          (0x700038B0..CC, 0x70003A20 and the period, by seq[0xD]); when
 *          w[i] % period == 1 the bone's position scaled (00103230),
 *          offset (001028B8) and turned (001026A0 by bone +0x90) lands in
 *          ring slot w+0xB0 (0022BB70; +0 xyz, +0xC = w[i + 0x38], +0xE =
 *          0) and w+0xB0 advances mod 0x41; w[i] += 1; then by seq[0xD]:
 *          0 / 6 / 7 / 8 fade bone +0x88 by 0x66 (at below 0 its colour
 *          halfwords and w[i] are cleared), 1 / 2..5 / 9 wrap w[i] at
 *          0x51 / 0x79 / 0x3D; seq[0xD] 9 also draws the bone's burst
 *          (kind w[i + 0x38]: scale, size and table pair; 00102958,
 *          00102760, 00103230, 001CCF70, 001F0190, 001CFB50 and 001CFBE0,
 *          001F0290; kind 5 three layered rounds with the four floats at
 *          0x268910 / 918 / 9A0 / 9A8 set to 3n).
 *          the ring: 0021B9A0(2, 1, 20), 0021B9A0(3, 1, 20); for each of
 *          the 65 slots with its age +0xE not negative after * 1e-4: a
 *          3-step LCG from w+0x94 gives three jitters (0x700038D0..D8),
 *          the scale by +0xC (1, 1.5, 2), 001029C0 / 00103230, and when
 *          001CD070(slot position, 0x30) is on screen the seq[0xD] draw
 *          (001CD2B0 fade for 0; 001CFB50 + 001CFBE0 tables) and age +=
 *          0xC8 (0xFA for 6..8, 0x3A98 for 9); an age of 0x3A99 or more
 *          becomes -1. 0021B9A0(1, 0, 0); the clock w+0xAC += 1.
 *   other  nothing.
 * The period and the burst live in registers the original only sets in
 * the seq[0xD] switches; where it would use them unset this faults (code
 * 6; docs/AREA01_UI.md section 2). */
static void call_cfb50(EmArea01Ui *s, uint32_t buf, uint32_t f12, uint32_t f13, uint32_t f14, uint32_t f15,
                       uint32_t f16)
{
    const uint64_t a[3] = {ui_sx(buf), 0, 0x700036A0u};
    const uint32_t f[5] = {f12, f13, f14, f15, f16};
    ui_call(s, UI_001CFB50, 3, a, 5, f, NULL, NULL);
}

static void call_cfbe0(EmArea01Ui *s, uint32_t handle, uint32_t table, uint32_t buf, uint32_t t0)
{
    const uint64_t a[5] = {ui_sx(handle), 1, table, ui_sx(buf), t0};
    ui_call(s, UI_001CFBE0, 5, a, 0, NULL, NULL, NULL);
}

static void call_scale(EmArea01Ui *s, uint32_t v, uint32_t f)
{
    const uint64_t a[2] = {v, v};
    ui_call(s, UI_00103230, 2, a, 1, &f, NULL, NULL);
}

#define F_TENTH 0x3DCCCCCDu
#define F_1EM6 0x358637BDu
#define F_1EM4 0x38D1B717u

/* (c - scn+off) / (float)(the record's halfword +6, read again) */
static uint32_t ramp(EmArea01Ui *s, uint32_t w, uint32_t off, uint32_t c, uint32_t field)
{
    const uint32_t v = ui_lw(s, field);
    const uint32_t d = ui_fcvt(ui_lh(s, ui_lw(s, w + 0x90u) + off + 6u));
    return ui_fdiv(ui_fsub(c, v), d);
}

int em_area01_ui_0022BBC0(EmArea01Ui *s, uint32_t seq)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, 0x0022BBC0u, EM_A01R_FAULT_NULL_WORKER, 0);
    UiFrame fr = ui_enter(s, 0x0022BBC0u, 0x100);
    const uint32_t buf = s->sp + 0xA0u;
    const uint32_t st = ui_lbu(s, seq + 4u);
    const uint32_t scn = ui_lw(s, seq + 0x24u);
    const uint32_t w = seq + 0x1F0u;
    if (st == 3 || st == 2) {
        (void)ui_call2(s, UI_001AFC10, seq, st);
        return ui_leave(s, fr);
    }
    if (st > 3) return ui_leave(s, fr);
    if (st == 0) {
        if (pick_table(s, seq) == 0) {
            ui_sb(s, seq + 4u, 3);
            return ui_leave(s, fr);
        }
        for (uint32_t i = 0; (int32_t)i < (int32_t)ui_lbu(s, scn + 0xCu) && !ui_latched(s); ++i) {
            ui_sb(s, w + i, 0);
            ui_sb(s, w + i + 0x38u, 0);
        }
        if (bone_slots(s, seq, 5) == 0) return ui_leave(s, fr);
        for (int32_t i = 0; i < 0x41; ++i) ui_sh(s, ring_slot(s, i) + 0xEu, 0xFFFFu);
        ui_sw(s, w + 0x94u, ui_call0(s, UI_00122BB8));
        ui_sw(s, w + 0x9Cu, 0);
        ui_sw(s, w + 0xA0u, 0);
        ui_sw(s, w + 0xA4u, 0);
        ui_sw(s, w + 0xB0u, 0);
        ui_sw(s, w + 0xACu, 0);
        ui_00102948(s, seq + 0x80u, scn + 0x80u);
    }
    /* state 1 */
    if (ui_lbu(s, scn + 4u) == 3) {
        ui_sb(s, seq + 4u, 3);
        return ui_leave(s, fr);
    }
    uint32_t off = 0;
    for (int32_t n = 0; n < (int32_t)ui_lw(s, w + 0x98u) && !ui_latched(s); ++n, off += 8u) {
        const uint32_t ev = ui_lw(s, w + 0x90u) + off;
        if ((int32_t)ui_lw(s, w + 0xACu) != ui_lh(s, ev)) continue;
        const uint32_t op = (uint32_t)ui_lh(s, ev + 2u);
        switch (op) {
        case 0:
            if (ui_lbu(s, seq + 0xDu) != 0) break;
            ui_sw(s, w + 0xA0u, (uint32_t)ui_lh(s, ev + 6u));
            ui_sw(s, w + 0x80u, ramp(s, w, off, UI_F_ONE, scn + 0x60u));
            ui_sw(s, w + 0x84u, ramp(s, w, off, F_TENTH, scn + 0x64u));
            ui_sw(s, w + 0x88u, ramp(s, w, off, UI_F_ONE, scn + 0x68u));
            break;
        case 1: {
            ui_sw(s, w + 0xA4u, (uint32_t)ui_lh(s, ev + 6u));
            const int32_t d = ui_lh(s, ui_lw(s, w + 0x90u) + off + 6u);
            if (d == 0) {
                ui_unmeasured(s, ev + 6u);
                break;
            }
            ui_sw(s, w + 0xA8u, (uint32_t)(0x1000 / d));
            ui_sw(s, w + 0xA8u, ui_lw(s, w + 0xA8u) + 4u);
            break;
        }
        case 2: {
            ui_sb(s, w + (uint32_t)ui_lh(s, ev + 4u), 1);
            const uint32_t e2 = ui_lw(s, w + 0x90u) + off;
            const uint32_t v = (uint32_t)ui_lb(s, e2 + 6u);
            ui_sb(s, w + (uint32_t)ui_lh(s, e2 + 4u) + 0x38u, v);
            break;
        }
        case 3:
            if (ui_lbu(s, seq + 0xDu) != 0) break;
            for (uint32_t k = 0; k < 3; ++k) {
                const uint32_t e2 = ui_lw(s, w + 0x90u) + off;
                const uint32_t bone = ui_lw(s, scn + ((uint32_t)ui_lh(s, e2 + 4u) << 2) + 0x110u);
                ui_sh(s, bone + 0x88u + 2u * k, 0);
            }
            break;
        case 4:
            if (ui_lbu(s, seq + 0xDu) != 0) break;
            ui_sb(s, scn + 4u, 3);
            break;
        case 5: {
            const uint32_t kind = ui_lbu(s, seq + 0xDu);
            uint32_t c[4];
            if (kind == 0) {
                c[0] = 0x3E4CCCCDu; c[1] = 0x3F4CCCCDu; c[2] = 0x3E4CCCCDu; c[3] = UI_F_ONE;
            } else if (kind == 1 || kind == 3) {
                c[0] = F_TENTH; c[1] = F_TENTH; c[2] = F_TENTH; c[3] = UI_F_ONE;
            } else {
                break;
            }
            ui_sw(s, w + 0x9Cu, (uint32_t)ui_lh(s, ev + 6u));
            for (uint32_t k = 0; k < 4; ++k)
                ui_sw(s, w + 0x70u + 4u * k, ramp(s, w, off, c[k], scn + 0x80u + 4u * k));
            break;
        }
        case 6: {
            const uint32_t kind = ui_lbu(s, seq + 0xDu);
            if (kind != 9 && kind != 3 && kind != 1) break;
            ui_sw(s, w + 0x9Cu, (uint32_t)ui_lh(s, ev + 6u));
            for (uint32_t k = 0; k < 4; ++k) {
                const uint32_t to = ui_lw(s, seq + 0x80u + 4u * k);
                ui_sw(s, w + 0x70u + 4u * k, ramp(s, w, off, to, scn + 0x80u + 4u * k));
            }
            break;
        }
        case 7:
            if (ui_lbu(s, seq + 0xDu) != 1) break;
            (void)ui_call2(s, UI_001CA6E0, scn,
                           ui_sx(ui_lw(s, 0x0028A490u + ((uint32_t)ui_lh(s, ev + 6u) << 2))));
            break;
        case 8:
            ui_sb(s, seq + 4u, 3);
            break;
        default:
            break;
        }
    }
    /* the ramps */
    {
        const uint32_t n = ui_lw(s, w + 0x9Cu);
        if (n != 0) {
            ui_sw(s, w + 0x9Cu, n - 1u);
            for (uint32_t k = 0; k < 4; ++k) {
                const uint32_t d = ui_lw(s, w + 0x70u + 4u * k);
                ui_sw(s, scn + 0x80u + 4u * k, ui_fadd(ui_lw(s, scn + 0x80u + 4u * k), d));
            }
        }
    }
    {
        const uint32_t n = ui_lw(s, w + 0xA0u);
        if (n != 0) {
            ui_sw(s, w + 0xA0u, n - 1u);
            for (uint32_t k = 0; k < 3; ++k) {
                const uint32_t d = ui_lw(s, w + 0x80u + 4u * k);
                ui_sw(s, scn + 0x60u + 4u * k, ui_fadd(ui_lw(s, scn + 0x60u + 4u * k), d));
            }
        }
    }
    {
        const uint32_t n = ui_lw(s, w + 0xA4u);
        if (n != 0) {
            ui_sw(s, w + 0xA4u, n - 1u);
            for (uint32_t i = 0; (int32_t)i < (int32_t)ui_lbu(s, scn + 0xCu) && !ui_latched(s); ++i) {
                const uint32_t bone = ui_lw(s, scn + 0x110u + 4u * i);
                const int32_t step = ui_lh(s, w + 0xA8u);
                ui_sh(s, bone + 0x8Au, (uint32_t)(ui_lh(s, bone + 0x8Au) - step));
                const uint32_t b2 = ui_lw(s, scn + 0x110u + 4u * i);
                if (ui_lh(s, b2 + 0x8Au) < 0) ui_sh(s, b2 + 0x8Au, 0);
            }
        }
    }
    /* the emitters */
    int32_t period = 0, burst = 0;
    int period_set = 0, burst_set = 0;
    for (uint32_t i = 0; (int32_t)i < (int32_t)ui_lbu(s, scn + 0xCu) && !ui_latched(s); ++i) {
        const uint32_t p = scn + 4u * i;
        if (ui_lb(s, w + i) == 0) continue;
        const uint32_t kind = ui_lbu(s, seq + 0xDu);
        /* offsets: 0x700038B0..BC (added to the slot), 0x700038C0..CC
         * (001028B8's addend), 0x70003A20 (00103230's scale) */
        uint32_t b4 = 0, c4 = 0, a20 = 0;
        int set = 1;
        switch (kind) {
        case 0: b4 = 0xC0000000u; a20 = 0x3F733333u; period = 0x28; break;
        case 1: b4 = 0xC0000000u; c4 = 0xC0800000u; period = 0xC; break;
        case 6: case 7: case 8: b4 = 0xC0000000u; a20 = 0x3F733333u; period = 4; break;
        case 4: b4 = 0x40400000u; period = 0xC; break;
        case 5: b4 = 0xC0A00000u; period = 0xC; break;
        case 2: c4 = 0xBF99999Au; period = 0x14; break;
        case 3: c4 = 0xC0000000u; a20 = 0x3F733333u; period = 0xC; break;
        case 9: b4 = 0xC0000000u; period = 0x14; break;
        default: set = 0; break;
        }
        if (set) {
            period_set = 1;
            ui_sw(s, 0x700038B0u, 0);
            ui_sw(s, 0x700038B4u, b4);
            ui_sw(s, 0x700038B8u, 0);
            ui_sw(s, 0x700038BCu, 0);
            ui_sw(s, 0x700038C0u, 0);
            ui_sw(s, 0x700038C4u, c4);
            ui_sw(s, 0x700038C8u, 0);
            ui_sw(s, 0x700038CCu, 0);
            ui_sw(s, 0x70003A20u, a20);
        }
        if (!period_set) {
            ui_unmeasured(s, seq + 0xDu);
            break;
        }
        if (ui_lb(s, w + i) % period == 1) {
            const uint32_t b = ui_lw(s, p + 0x110u);
            const uint32_t f12 = ui_lw(s, 0x70003A20u);
            ui_sw(s, 0x700038A0u, ui_lw(s, b));
            ui_sw(s, 0x700038A4u, ui_lw(s, ui_lw(s, p + 0x110u) + 4u));
            ui_sw(s, 0x700038A8u, ui_lw(s, ui_lw(s, p + 0x110u) + 8u));
            ui_sw(s, 0x700038ACu, UI_F_ONE);
            call_scale(s, 0x700038A0u, f12);
            (void)ui_call3(s, UI_001028B8, 0x700038A0u, 0x700038A0u, 0x700038C0u);
            (void)ui_call3(s, UI_001026A0, 0x700038A0u, ui_lw(s, p + 0x110u) + 0x90u, 0x700038A0u);
            const uint32_t slot = ring_slot(s, (int32_t)ui_lw(s, w + 0xB0u));
            for (uint32_t k = 0; k < 3; ++k)
                ui_sw(s, slot + 4u * k, ui_fadd(ui_lw(s, 0x700038A0u + 4u * k), ui_lw(s, 0x700038B0u + 4u * k)));
            ui_sh(s, slot + 0xCu, (uint32_t)ui_lb(s, w + i + 0x38u));
            ui_sh(s, slot + 0xEu, 0);
            ui_sw(s, w + 0xB0u, ui_lw(s, w + 0xB0u) + 1u);
            if (!((int32_t)ui_lw(s, w + 0xB0u) < 0x41)) ui_sw(s, w + 0xB0u, 0);
        }
        ui_sb(s, w + i, (uint32_t)(ui_lb(s, w + i) + 1));
        switch (kind) {
        case 0: case 6: case 7: case 8: {
            const uint32_t b = ui_lw(s, p + 0x110u);
            ui_sh(s, b + 0x88u, (uint32_t)(ui_lh(s, b + 0x88u) - 0x66));
            const uint32_t b2 = ui_lw(s, p + 0x110u);
            if (ui_lh(s, b2 + 0x88u) >= 0) break;
            ui_sh(s, b2 + 0x88u, 0);
            ui_sh(s, ui_lw(s, p + 0x110u) + 0x8Au, 0);
            ui_sh(s, ui_lw(s, p + 0x110u) + 0x8Cu, 0);
            ui_sb(s, w + i, 0);
            break;
        }
        case 1:
            if (ui_lb(s, w + i) >= 0x51) ui_sb(s, w + i, 0);
            break;
        case 2: case 3: case 4: case 5:
            if (ui_lb(s, w + i) >= 0x79) ui_sb(s, w + i, 0);
            break;
        case 9: {
            if (ui_lb(s, w + i) >= 0x3D) {
                ui_sb(s, w + i, 0);
                break;
            }
            const uint32_t fp = w + i + 0x38u;
            uint32_t a24 = 0, s20 = 0;
            int bset = 1;
            switch (ui_lb(s, fp)) {
            case 0: s20 = UI_F_ONE; a24 = 0x40A00000u; burst = 0; break;
            case 1: s20 = UI_F_HALF; a24 = 0x40A00000u; burst = 2; break;
            case 2: s20 = UI_F_ONE; a24 = 0x41700000u; burst = 2; break;
            case 3: s20 = UI_F_ONE; a24 = 0x41A00000u; burst = 4; break;
            case 4: s20 = UI_F_ONE; a24 = 0x41A00000u; burst = 6; break;
            case 5: s20 = UI_F_ONE; a24 = 0x40A00000u; burst = 0; break;
            default: bset = 0; break;
            }
            if (bset) {
                burst_set = 1;
                ui_sw(s, 0x70003A20u, s20);
                ui_sw(s, 0x70003A24u, a24);
            }
            ui_sw(s, 0x70003A28u, ui_fmul(UI_F_TWO, ui_fdiv(ui_fcvt(ui_lb(s, w + i)), 0x42700000u)));
            ui_00102958(s, 0x700036A0u, ui_lw(s, p + 0x110u) + 0x90u);
            (void)ui_call2(s, UI_00102760, 0x700036A0u, 0x700036A0u);
            (void)ui_call2(s, UI_00102760, 0x700036B0u, 0x700036B0u);
            (void)ui_call2(s, UI_00102760, 0x700036C0u, 0x700036C0u);
            call_scale(s, 0x700036A0u, ui_lw(s, 0x70003A20u));
            call_scale(s, 0x700036B0u, ui_lw(s, 0x70003A20u));
            call_scale(s, 0x700036C0u, ui_lw(s, 0x70003A20u));
            const uint32_t handle = ui_call1(s, UI_001CCF70, 0x700036D0u);
            {
                const uint32_t f[2] = {0xC3110000u, 0x43E10000u};
                ui_call(s, UI_001F0190, 0, NULL, 2, f, NULL, NULL);
            }
            if (ui_lb(s, fp) == 5) {
                for (int32_t k = 1; k < 4; ++k) {
                    const uint32_t f12 = ui_lw(s, 0x70003A28u);
                    const uint32_t fk = ui_fcvt(k);
                    const uint32_t f16 = ui_lw(s, 0x70003A24u);
                    const uint32_t t = ui_fmul(0x40400000u, fk);
                    ui_sw(s, 0x00268910u, t);
                    ui_sw(s, 0x00268918u, t);
                    ui_sw(s, 0x002689A0u, t);
                    ui_sw(s, 0x002689A8u, t);
                    call_cfb50(s, buf, f12, ui_fmul(0x3DFCD680u, fk), UI_F_ONE, F_1EM6, f16);
                    call_cfbe0(s, handle, 0x00268990u, buf, 1);
                    call_cfbe0(s, handle, 0x00268900u, buf, 1);
                }
            } else {
                call_cfb50(s, buf, ui_lw(s, 0x70003A28u), 0x3DFCD680u, UI_F_ONE, F_1EM6, ui_lw(s, 0x70003A24u));
                if (!burst_set) {
                    ui_unmeasured(s, fp);
                    break;
                }
                call_cfbe0(s, handle, 0x00268480u + (uint32_t)(burst + 1) * 0x90u, buf, 0);
                call_cfbe0(s, handle, 0x00268480u + (uint32_t)burst * 0x90u, buf, 0);
            }
            (void)ui_call0(s, UI_001F0290);
            break;
        }
        default:
            break;
        }
    }
    if (ui_latched(s)) return ui_leave(s, fr);
    /* the ring */
    {
        const uint32_t f[2] = {UI_F_ONE, 0x41A00000u};
        uint64_t a2[1] = {2}, a3[1] = {3};
        ui_call(s, UI_0021B9A0, 1, a2, 2, f, NULL, NULL);
        ui_call(s, UI_0021B9A0, 1, a3, 2, f, NULL, NULL);
    }
    uint32_t seed = ui_lw(s, w + 0x94u);
    for (int32_t i = 0; i < 0x41 && !ui_latched(s); ++i) {
        const uint32_t act = ring_slot(s, i);
        const int32_t age = ui_lh(s, act + 0xEu);
        const uint32_t seed_b = seed * 37u + 11u;
        const uint32_t seed_c = seed_b * 37u + 11u;
        const uint32_t k65535 = 0x477FFF00u;
        ui_sw(s, 0x70003A24u, ui_fmul(F_1EM4, ui_fcvt(age)));
        const uint32_t a24 = ui_lw(s, 0x70003A24u);
        const uint32_t j0 = ui_fdiv(ui_fcvt((int32_t)((seed >> 16) & 0xFFFFu)), k65535);
        const uint32_t j1 = ui_fdiv(ui_fcvt((int32_t)((seed_b >> 16) & 0xFFFFu)), k65535);
        const uint32_t j2 = ui_fdiv(ui_fcvt((int32_t)((seed_c >> 16) & 0xFFFFu)), k65535);
        ui_sw(s, 0x700038D0u, ui_fadd(j0, F_1EM4));
        ui_sw(s, 0x700038D4u, ui_fadd(j1, F_1EM4));
        ui_sw(s, 0x700038D8u, ui_fadd(j2, F_1EM4));
        seed = seed_c * 37u + 11u;
        if (em_ee_c_lt_bits(a24, 0)) continue;
        switch (ui_lh(s, act + 0xCu)) {
        case 4: case 3: case 0: ui_sw(s, 0x70003A28u, UI_F_ONE); break;
        case 2: ui_sw(s, 0x70003A28u, UI_F_TWO); break;
        case 1: ui_sw(s, 0x70003A28u, 0x3FC00000u); break;
        default: break;
        }
        (void)ui_call1(s, UI_001029C0, 0x700036A0u);
        call_scale(s, 0x700036A0u, ui_lw(s, 0x70003A28u));
        call_scale(s, 0x700036B0u, ui_lw(s, 0x70003A28u));
        call_scale(s, 0x700036C0u, ui_lw(s, 0x70003A28u));
        ui_sw(s, 0x700036D0u, ui_lw(s, act));
        ui_sw(s, 0x700036D4u, ui_lw(s, act + 4u));
        ui_sw(s, 0x700036D8u, ui_lw(s, act + 8u));
        const uint32_t h = ui_call2(s, UI_001CD070, 0x700036D0u, 0x30);
        if (h != 0xFFFFFFu) {
            const uint32_t kind = ui_lbu(s, seq + 0xDu);
            int32_t add = 0xC8;
            switch (kind) {
            case 0: {
                uint32_t f0;
                const uint32_t f[4] = {0x41700000u, 0x41A00000u, 0x43A00000u, 0x43A00000u};
                ui_call(s, UI_001CD2B0, 0, NULL, 4, f, NULL, &f0);
                ui_sw(s, 0x70003A2Cu, f0);
                const uint32_t alpha = ui_lw(s, 0x70003A2Cu);
                if (!em_ee_c_eq_bits(0, alpha)) {
                    call_cfb50(s, buf, ui_lw(s, 0x70003A24u), ui_lw(s, 0x700038D0u), alpha, F_1EM6, 0x41000000u);
                    call_cfbe0(s, h, 0x00268090u, buf, 0);
                    call_cfb50(s, buf, ui_lw(s, 0x70003A24u), ui_lw(s, 0x700038D4u), ui_lw(s, 0x70003A2Cu), F_1EM6,
                               0x41000000u);
                    call_cfbe0(s, h, 0x00268120u, buf, 0);
                    call_cfb50(s, buf, ui_lw(s, 0x70003A24u), ui_lw(s, 0x700038D8u), ui_lw(s, 0x70003A2Cu), F_1EM6,
                               0x41000000u);
                    call_cfbe0(s, h, 0x002681B0u, buf, 0);
                }
                break;
            }
            case 1:
                call_cfb50(s, buf, ui_lw(s, 0x70003A24u), ui_lw(s, 0x700038D0u), UI_F_ONE, F_1EM6, 0x40A00000u);
                call_cfbe0(s, h, 0x00268A20u, buf, 0);
                break;
            case 2: case 3:
                call_cfb50(s, buf, ui_lw(s, 0x70003A24u), ui_lw(s, 0x700038D0u), UI_F_ONE, F_1EM6, 0);
                call_cfbe0(s, h, 0x00268AB0u, buf, 0);
                break;
            case 4:
                call_cfb50(s, buf, ui_lw(s, 0x70003A24u), ui_lw(s, 0x700038D0u), UI_F_ONE, F_TENTH, 0);
                call_cfbe0(s, h, 0x00268360u, buf, 0);
                break;
            case 5:
                call_cfb50(s, buf, ui_lw(s, 0x70003A24u), ui_lw(s, 0x700038D0u), UI_F_ONE, F_TENTH, 0);
                call_cfbe0(s, h, 0x002683F0u, buf, 0);
                break;
            case 6: case 7: case 8:
                call_cfb50(s, buf, ui_lw(s, 0x70003A24u), ui_lw(s, 0x700038D0u), UI_F_ONE, F_1EM6, 0x41000000u);
                call_cfbe0(s, h, 0x00268240u, buf, 0);
                call_cfbe0(s, h, 0x002682D0u, buf, 0);
                add = 0xFA;
                break;
            default:
                add = 0x3A98;
                break;
            }
            ui_sh(s, act + 0xEu, (uint32_t)(ui_lh(s, act + 0xEu) + add));
        }
        if (ui_lh(s, act + 0xEu) >= 0x3A99) ui_sh(s, act + 0xEu, 0xFFFFu);
    }
    {
        const uint32_t f[2] = {0, 0};
        uint64_t a1[1] = {1};
        ui_call(s, UI_0021B9A0, 1, a1, 2, f, NULL, NULL);
    }
    ui_sw(s, w + 0xACu, ui_lw(s, w + 0xACu) + 1u);
    return ui_leave(s, fr);
}
