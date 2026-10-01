/* AREA01's gap node after the keypad (the a06b_01 AREA01 load at entry 7,
 * and the a01v jump over the gap): 001BF6B0 (the node g[36]'s behaviour
 * after 001C02E0's bit-5 branch) with 001BEAC0 (its companion spawn),
 * 001BE6C0 (the companion's behaviour), 001BF5B0 (the animation-flag
 * notifier), 001BFF90 (the clip setter), 001284E0 (the bug spawn) and
 * 0012B850 (a 0012A5D0 bug's grab state). See em_level8_port.h and
 * docs/LEVEL8_PORT.md; the rules of em_level8_port_lift.c apply.
 */
#include "em_level8_port_internal.h"

/* ------------------------------------------------------------------------
 * 001BEAC0 (C byte-identical): n = 001AFA90(2); when not null: its +0xB0 /
 * +0xC0 vectors = self's (00102948), its +0xA0 = the vector at pos
 * (00102948), +3 = 0x13, +0x0D = a3 (byte), +0x34 = a2 (halfword), +0x10 =
 * 001BE6C0 (its behaviour), +0x20 = self. Returns n.
 * ---------------------------------------------------------------------- */
uint32_t l8_001BEAC0(L8 *o, uint32_t self, uint32_t pos, int32_t a2, int32_t a3)
{
    uint32_t n = 0;
    if (l8_c_001AFA90(o, 2, &n)) return 0;
    if (!n) return 0;
    if (l8_c_00102948(o, n + 0xB0, self + 0xB0)) return n;
    if (l8_c_00102948(o, n + 0xC0, self + 0xC0)) return n;
    if (l8_c_00102948(o, n + 0xA0, pos)) return n;
    l8_w8(o, n + 3, 0x13);
    l8_w8(o, n + 0x0D, (uint32_t)a3);
    l8_w16(o, n + 0x34, (uint32_t)a2);
    l8_w32(o, n + 0x10, 0x001BE6C0u);
    l8_w32(o, n + 0x20, self);
    return n;
}

int em_level8_port_001BEAC0(const EmLevel8PortHooks *h, uint32_t self, uint32_t pos, int32_t a2, int32_t a3,
                            int32_t *result, EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    uint32_t r = l8_001BEAC0(&o, self, pos, a2, a3);
    if (l8_failed(&o)) return -1;
    *result = (int32_t)r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001BFF90 (C byte-identical): when the halfword p[1] differs from (short)
 * clip: p[1] = clip, then 001C67E0(obj, clip, 4.0, 0.0) (the whole clip
 * register goes on as a1).
 * ---------------------------------------------------------------------- */
static void l8_001BFF90(L8 *o, uint32_t obj, uint32_t p, int32_t clip)
{
    if (l8_s16(o, p + 2) == (int16_t)clip) return;
    l8_w16(o, p + 2, (uint32_t)clip);
    l8_c_001C67E0(o, obj, clip, fl(0x40800000u), fl(F_ZERO));
}

int em_level8_port_001BFF90(const EmLevel8PortHooks *h, uint32_t obj, uint32_t p, int32_t clip,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_001BFF90(&o, obj, p, clip);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 001BF5B0 (C byte-identical): when p[0] (signed halfword) has bit 0x1000
 * and p[1] differs from (short)mode: mode 0 -> 001D0D40(a0, 0x24FD50,
 * 0x5B, 1) then p[1] = 0; otherwise 001D0D40(a0, 0x250750, 0x41, 1) then
 * p[1] = 1.
 * ---------------------------------------------------------------------- */
void l8_001BF5B0(L8 *o, uint32_t a0, uint32_t p, int32_t mode)
{
    if (!(l8_s16(o, p) & 0x1000)) return;
    int32_t v = (int16_t)mode;
    if (l8_s16(o, p + 2) == v) return;
    if (v == 0) {
        if (l8_c_001D0D40(o, a0, 0x0024FD50u, 0x5B, 1)) return;
        l8_w16(o, p + 2, 0);
    } else {
        if (l8_c_001D0D40(o, a0, 0x00250750u, 0x41, 1)) return;
        l8_w16(o, p + 2, 1);
    }
}

int em_level8_port_001BF5B0(const EmLevel8PortHooks *h, uint32_t a0, uint32_t p, int32_t mode,
                            EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_001BF5B0(&o, a0, p, mode);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 001284E0 (C byte-identical): the bug spawn. With the live count
 * 0x700031F4 at 10 or less (signed): n = 001AFA90(2); when not null: +3 =
 * 0, +0x0D = kind + 0x0B, +0x9A = 0, +0xB0.. = pos (three words), +0xC0.. =
 * dir when kind is 1, else zeros; +0x9D / +0x9E = owner's; +0x60..+0x6C =
 * 1.0; +0x10 = 0012A5D0; the count + 1; returns 1. Otherwise 0.
 * ---------------------------------------------------------------------- */
int32_t l8_001284E0(L8 *o, uint32_t owner, uint32_t pos, int32_t kind, uint32_t dir)
{
    if (!((int32_t)l8_u32(o, 0x700031F4u) < 0x0B)) return 0;
    uint32_t n = 0;
    if (l8_c_001AFA90(o, 2, &n)) return 0;
    if (!n) return 0;
    l8_w8(o, n + 3, 0);
    l8_w8(o, n + 0x0D, (uint32_t)kind + 0x0Bu);
    l8_w8(o, n + 0x9A, 0);
    for (uint32_t k = 0; k < 12; k += 4) l8_w32(o, n + 0xB0 + k, l8_u32(o, pos + k));
    if (kind == 1) {
        for (uint32_t k = 0; k < 12; k += 4) l8_w32(o, n + 0xC0 + k, l8_u32(o, dir + k));
    } else {
        for (uint32_t k = 0; k < 12; k += 4) l8_w32(o, n + 0xC0 + k, 0);
    }
    l8_w8(o, n + 0x9D, l8_u8(o, owner + 0x9D));
    l8_w8(o, n + 0x9E, l8_u8(o, owner + 0x9E));
    for (uint32_t k = 0; k < 16; k += 4) l8_w32(o, n + 0x60 + k, F_ONE);
    l8_w32(o, n + 0x10, 0x0012A5D0u);
    l8_w32(o, 0x700031F4u, l8_u32(o, 0x700031F4u) + 1u);
    return 1;
}

int em_level8_port_001284E0(const EmLevel8PortHooks *h, uint32_t owner, uint32_t pos, int32_t kind, uint32_t dir,
                            int32_t *result, EmLevel8PortFault *fault)
{
    L8 o;
    if (!result || l8_begin(&o, h, fault)) return -1;
    int32_t r = l8_001284E0(&o, owner, pos, kind, dir);
    if (l8_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0012B850 (word asm; from the instructions): a bug's grab state. held =
 * 001C2770(self, sub, 0). By self +6:
 *   0  sub +0xD0 (halfword) = 120; +6 + 1; sub +0xD8 = 0.3; sub +0xE8 =
 *      self +0xC4; 001287F0(self, sub, 6, 8.0); then on as 1.
 *   1  sub +0xD0 counts down; at 0: +6 + 1 and sub +0xD8 = 0.
 *   2  00128640(self) zero: sub +0x50 = self +0xB0 (00102948), +5 = 1,
 *      +6 = 0, +7 = 0.
 * Then, when held is 0: 001C3D60(self, sub).
 * ---------------------------------------------------------------------- */
static void l8_0012B850(L8 *o, uint32_t self, uint32_t sub)
{
    int32_t held = 0;
    if (l8_c_001C2770(o, self, sub, 0, &held)) return;
    uint32_t step = l8_u8(o, self + 6);
    if (step == 2) {
        int32_t r = 0;
        if (l8_c_00128640(o, self, &r)) return;
        if (r == 0) {
            if (l8_c_00102948(o, sub + 0x50, self + 0xB0)) return;
            l8_w8(o, self + 5, 1);
            l8_w8(o, self + 6, 0);
            l8_w8(o, self + 7, 0);
        }
    } else if (step == 1 || step == 0) {
        if (step == 0) {
            l8_w16(o, sub + 0xD0, 0x78);
            l8_w8(o, self + 6, l8_u8(o, self + 6) + 1u);
            l8_w32(o, sub + 0xD8, F_0_3);
            l8_w32(o, sub + 0xE8, l8_u32(o, self + 0xC4));
            if (l8_c_001287F0(o, self, sub, 6, fl(F_8))) return;
        }
        uint32_t t = (uint32_t)l8_s16(o, sub + 0xD0) - 1u;
        l8_w16(o, sub + 0xD0, t);
        if ((int16_t)t == 0) {
            l8_w8(o, self + 6, l8_u8(o, self + 6) + 1u);
            l8_w32(o, sub + 0xD8, 0);
        }
    }
    if (held == 0) l8_c_001C3D60(o, self, sub);
}

int em_level8_port_0012B850(const EmLevel8PortHooks *h, uint32_t self, uint32_t sub, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_0012B850(&o, self, sub);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 001BE6C0 (NEARMISS; from the instructions): the companion of AREA01's gap
 * node (spawned by 001BEAC0). parent = self +0x20; entry loads it, then
 * self +4.
 *   3  001AFC10(self).
 *   0  parent +4 == 3: self +4 = 3. Otherwise 001B1020(self, 4, -1, 0),
 *      +0x58 = *(0x28A524), +0x5D = 0x81, +0 = 1, +0x30 = 0x275618, +0x20C
 *      (blk +0x1C) = 0.
 *   1  parent +4 >= 2: self +4 = 2. parent +1 == 0: nothing more. The
 *      countdown +0x20C; with a hit halfword +0x36 != 0: when parent +0 is
 *      1 and (the hit has no 0x5000 bits, or both countdowns, self's and
 *      the parent's, are 0: then self's = 60 and the hit is read again) the
 *      parent takes the hit (parent +0x36 = hit, parent +0 = 3) with the
 *      spark 0x80000076 along the eye-to-self direction (001028D0 /
 *      00102760 into 0x70003610, w = 1, 001EFD90) and the sound 0x15D; then
 *      self +0x36 = 0, +0 = 1. Then +0x0D == 1: the pose follows the
 *      parent's matrix (001026A0 of self +0xA0 into +0xB0, a half-scale
 *      copy of it, 00102958 / 001026D0 into self +0xD0, +0x100 = +0xB0,
 *      001C63D0); else 001C6380. Then 001B17A0, the +0x4C method, and the
 *      light 001F4A00 at (0, 2.5, 0, 1) through self +0xD0 with colour
 *      (0x20, 0x70, 0x80, 0x80).
 *   2  +0x64 -= 0.05 (stored); at 0 or below: +0x64 = 0 and +4 = 3; then
 *      001C6380 and the +0x4C method.
 * ---------------------------------------------------------------------- */
static void l8_gap_hit(L8 *o, uint32_t self)
{
    if (l8_c_001028D0(o, 0x70003610u, 0x00810350u, self + 0xB0)) return;
    if (l8_c_00102760(o, 0x70003610u, 0x70003610u)) return;
    l8_w32(o, 0x7000361Cu, F_ONE);
    if (l8_c_001EFD90(o, (int32_t)0x80000076u, self + 0x70, 0x70003610u)) return;
    int32_t ignored = 0;
    l8_c_001FBD50(o, self, 0x15D, 0, fl(F_300), &ignored);
}

static void l8_001BE6C0(L8 *o, uint32_t self)
{
    uint32_t parent = l8_u32(o, self + 0x20);
    uint32_t state = l8_u8(o, self + 4);
    uint32_t blk = self + 0x1F0;
    if (state == 3) {
        l8_c_001AFC10(o, self);
        return;
    }
    if (state == 2) {
        uint32_t a = L8_SUB(l8_u32(o, self + 0x64), 0x3D4CCCCDu);
        int gone = L8_LE(a, F_ZERO);
        l8_w32(o, self + 0x64, a);
        if (gone) {
            l8_w32(o, self + 0x64, 0);
            l8_w8(o, self + 4, 3);
        }
        if (l8_c_001C6380(o, self)) return;
        l8_callback(o, l8_u32(o, self + 0x4C), self);
        return;
    }
    if (state == 0) {
        if (l8_u8(o, parent + 4) == 3) {
            l8_w8(o, self + 4, 3);
            return;
        }
        if (l8_c_001B1020(o, self, 4, -1, 0)) return;
        l8_w32(o, self + 0x58, l8_u32(o, 0x0028A524u));
        l8_w8(o, self + 0x5D, 0x81);
        l8_w8(o, self, 1);
        l8_w32(o, self + 0x30, 0x00275618u);
        l8_w32(o, blk + 0x1C, 0);
        return;
    }
    if (state != 1) return;
    if (!((int32_t)l8_u8(o, parent + 4) < 2)) l8_w8(o, self + 4, 2);
    if (l8_u8(o, parent + 1) == 0) return;
    uint32_t t = l8_u32(o, blk + 0x1C);
    if (t) l8_w32(o, blk + 0x1C, t - 1u);
    int32_t hit = l8_s16(o, self + 0x36);
    if (hit != 0) {
        if (l8_u8(o, parent) == 1) {
            if (((int16_t)hit & 0x5000) == 0) {
                l8_w16(o, parent + 0x36, (uint32_t)hit);
                l8_w8(o, parent, 3);
                l8_gap_hit(o, self);
            } else if (l8_u32(o, blk + 0x1C) == 0 && l8_u32(o, parent + 0x1F0 + 0x1C) == 0) {
                l8_w32(o, blk + 0x1C, 0x3C);
                l8_w16(o, parent + 0x36, (uint32_t)l8_s16(o, self + 0x36));
                l8_w8(o, parent, 3);
                l8_gap_hit(o, self);
            }
        }
        l8_w16(o, self + 0x36, 0);
        l8_w8(o, self, 1);
    }
    if (l8_u8(o, self + 0x0D) == 1) {
        if (l8_c_001026A0(o, self + 0xB0, parent + 0xD0, self + 0xA0)) return;
        if (l8_c_001029C0(o, 0x700036A0u)) return;
        if (l8_c_00102B08(o, 0x700036A0u, 0x700036A0u, fl(0x3F000000u))) return;
        if (l8_c_00102958(o, 0x700036E0u, parent + 0xD0)) return;
        l8_w32(o, 0x70003710u, 0);
        l8_w32(o, 0x70003714u, 0);
        l8_w32(o, 0x70003718u, 0);
        l8_w32(o, 0x7000371Cu, 0);
        if (l8_c_001026D0(o, self + 0xD0, 0x700036E0u, 0x700036A0u)) return;
        if (l8_c_00102948(o, self + 0x100, self + 0xB0)) return;
        if (l8_c_001C63D0(o, self)) return;
    } else if (l8_c_001C6380(o, self)) {
        return;
    }
    if (l8_c_001B17A0(o, self)) return;
    if (l8_callback(o, l8_u32(o, self + 0x4C), self)) return;
    l8_w32(o, S_700038A0, 0);
    l8_w32(o, S_700038A0 + 4u, 0x40200000u);
    l8_w32(o, S_700038A0 + 8u, 0);
    l8_w32(o, S_700038A0 + 12u, F_ONE);
    if (l8_c_001026A0(o, S_700038A0, self + 0xD0, S_700038A0)) return;
    l8_w32(o, 0x700038B0u, 0x20);
    l8_w32(o, 0x700038B4u, 0x70);
    l8_w32(o, 0x700038B8u, 0x80);
    l8_w32(o, 0x700038BCu, 0x80);
    l8_c_001F4A00(o, S_700038A0, 0x700038B0u);
}

int em_level8_port_001BE6C0(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_001BE6C0(&o, self);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 001BF6B0 (C byte-identical): the gap node g[36] (see the decomp's
 * header; its sub-state table 0x26E260 is the switch below). sub = self
 * +0x1F0. The sound gate "quiet" is ((*0x70003B64 + (*(short
 * *)0x70003B8A << 6)) & 0xFF) == 0 (the halfword loaded first), which
 * plays 001FBD50(self, 0x444, 0, 300).
 * ---------------------------------------------------------------------- */
static void l8_sound_444(L8 *o, uint32_t self)
{
    int32_t shift = l8_s16(o, 0x70003B8Au);
    uint32_t frame = l8_u32(o, 0x70003B64u);
    if (((frame + ((uint32_t)shift << 6)) & 0xFFu) == 0) {
        int32_t ignored = 0;
        l8_c_001FBD50(o, self, 0x444, 0, fl(F_300), &ignored);
    }
}

static void l8_bf6b0_init(L8 *o, uint32_t self, uint32_t sub)
{
    l8_w32(o, S_700038A0, 0);
    l8_w32(o, S_700038A0 + 4u, F_2);
    l8_w32(o, S_700038A0 + 8u, F_5);
    l8_w32(o, S_700038A0 + 12u, F_ONE);
    l8_w32(o, self + 0x20, l8_001BEAC0(o, self, S_700038A0, 0x64, 1));
    if (l8_u32(o, self + 0x20) == 0) {
        l8_w8(o, self + 4, 3);
        return;
    }
    uint32_t mate = 0;
    if (l8_c_001AFA90(o, 2, &mate)) return;
    if (!mate) {
        l8_w8(o, self + 4, 3);
        return;
    }
    if (l8_c_00102948(o, mate + 0xB0, self + 0xB0)) return;
    if (l8_c_00102948(o, mate + 0xC0, self + 0xC0)) return;
    l8_w8(o, mate + 3, 0x12);
    l8_w8(o, mate + 0x0D, 1);
    l8_w32(o, mate + 0x10, 0x001BFFD0u);
    l8_w32(o, mate + 0x20, self);
    l8_w32(o, self + 0x24, mate);
    if (l8_c_001D0C80(o, self, l8_u32(o, 0x0028A518u))) return;
    if (l8_c_001D0D40(o, self, 0x0024FD50u, 0x5B, 1)) return;
    l8_w16(o, sub + 2, 0);
    l8_w16(o, sub, 0);
    if (l8_c_001C62C0(o, self)) return;
    l8_w32(o, self + 0x58, l8_u32(o, 0x0028A51Cu));
    l8_w16(o, self + 0x52, 1);
    l8_w8(o, self, 1);
    l8_w8(o, self + 4, 1);
    l8_w16(o, self + 0x34, 0x50);
    l8_w32(o, self + 0x30, 0x00275638u);
    l8_w32(o, sub + 0x18, 0x00275640u);
    if (l8_c_001C6380(o, self)) return;
    if (l8_c_00102948(o, S_700038A0, self + 0xB0)) return;
    l8_w32(o, 0x700038B0u, 0);
    l8_w32(o, 0x700038ACu, F_ONE);
    l8_w32(o, 0x700038B4u, 0x41F00000u);
    l8_w32(o, 0x700038B8u, 0);
    l8_w32(o, 0x700038BCu, F_ONE);
    if (l8_c_001026A0(o, 0x700038B0u, self + 0xD0, 0x700038B0u)) return;
    l8_w32(o, 0x700038BCu, F_ONE);
    if (l8_c_001028D0(o, 0x700038E0u, 0x700038B0u, S_700038A0)) return;
    l8_w32(o, sub + 0x0C, l8_u32(o, 0x700038E0u));
    l8_w32(o, sub + 0x10, l8_u32(o, 0x700038E4u));
    l8_w32(o, sub + 0x14, l8_u32(o, 0x700038E8u));
    if (l8_c_00102760(o, 0x700038E0u, 0x700038E0u)) return;
    l8_w32(o, 0x700038B0u, 0);
    l8_w32(o, 0x700038B4u, 0xC1F00000u);
    l8_w32(o, 0x700038B8u, 0);
    l8_w32(o, 0x700038BCu, F_ONE);
    if (l8_c_001028B8(o, 0x700038B0u, S_700038A0, 0x700038B0u)) return;
    l8_w32(o, 0x700038BCu, F_ONE);
    if (l8_c_001028D0(o, 0x700038F0u, 0x700038B0u, S_700038A0)) return;
    if (l8_c_00102760(o, 0x700038F0u, 0x700038F0u)) return;
    float dot = 0.0f;
    if (l8_c_00102738(o, 0x700038E0u, 0x700038F0u, &dot)) return;
    l8_w32(o, sub + 8, l8_bits(dot));
    l8_w32(o, sub + 0x1C, 0);
}

static void l8_bf6b0_step(L8 *o, uint32_t self, uint32_t sub)
{
    int32_t ignored = 0;
    switch (l8_u8(o, self + 5)) {
    case 0:
        l8_w16(o, self + 0x28, 0);
        l8_w8(o, self + 5, l8_u8(o, self + 5) + 1u);
        /* fall through */
    case 1: {
        int32_t tracking = 0;
        if (l8_c_001BF630(o, D_008102B0, self, sub, &tracking)) return;
        if (!tracking) {
            l8_w16(o, self + 0x28, 0);
            break;
        }
        l8_sound_444(o, self);
        l8_w16(o, self + 0x28, (uint32_t)l8_s16(o, self + 0x28) + 1u);
        if (!(l8_s16(o, self + 0x28) < 0x3D)) l8_w8(o, self + 5, l8_u8(o, self + 5) + 1u);
        break;
    }
    case 2:
        l8_sound_444(o, self);
        if (l8_s16(o, sub) & 0x1000) {
            l8_w8(o, self + 5, l8_u8(o, self + 5) + 1u);
            if (!((int32_t)l8_u32(o, 0x700031F4u) < 0x0B)) {
                l8_w8(o, self + 5, 0);
            } else {
                l8_001BF5B0(o, self, sub, 1);
                l8_w16(o, self + 0x28, 0);
            }
        }
        break;
    case 3:
        l8_w16(o, self + 0x28, (uint32_t)l8_s16(o, self + 0x28) + 1u);
        if (l8_s16(o, self + 0x28) < 0x15) break;
        l8_w32(o, 0x700038B0u, 0);
        l8_w32(o, 0x700038B4u, 0x41200000u);
        l8_w32(o, 0x700038B8u, 0);
        l8_w32(o, 0x700038BCu, F_ONE);
        if (l8_c_001026A0(o, 0x700038B0u, self + 0xD0, 0x700038B0u)) return;
        l8_w32(o, 0x700038BCu, F_ONE);
        if (!L8_LT(l8_u32(o, sub + 8), 0x3F4CCCCDu)) {
            if (l8_001284E0(o, self, 0x700038B0u, 0, S_700038A0))
                l8_c_001FBD50(o, self, 0x445, 0, fl(F_300), &ignored);
        } else {
            l8_w32(o, S_700038A0, 0);
            uint32_t x = l8_u32(o, sub + 0x14);
            uint32_t y = l8_u32(o, sub + 0x0C);
            float angle = 0.0f;
            if (l8_c_0011E620(o, fl(y), fl(x), &angle)) return;
            float yaw = 0.0f;
            if (l8_angle(o, angle, &yaw)) return;
            l8_w32(o, S_700038A0 + 4u, l8_bits(yaw));
            l8_w32(o, S_700038A0 + 8u, 0);
            l8_w32(o, S_700038A0 + 12u, F_ONE);
            if (l8_001284E0(o, self, 0x700038B0u, 1, S_700038A0))
                l8_c_001FBD50(o, self, 0x445, 0, fl(F_300), &ignored);
        }
        l8_w8(o, self + 5, l8_u8(o, self + 5) + 1u);
        break;
    case 4:
        l8_sound_444(o, self);
        if (l8_s16(o, sub) & 0x1000) {
            l8_001BF5B0(o, self, sub, 0);
            int32_t r = 0;
            if (l8_c_00122BB8(o, &r)) return;
            /* 360 (r >> 16) >> 15 as the shifts and subtractions of the
             * original (32-bit wrapping, arithmetic right shifts). */
            uint32_t x = (uint32_t)(r >> 16);
            x = (x << 4) - x;
            x = (x << 2) - x;
            int32_t y = l8_sra((uint32_t)(x << 3), 15);
            l8_w16(o, self + 0x28, (uint32_t)y + 0x12Cu);
            l8_w8(o, self + 5, l8_u8(o, self + 5) + 1u);
        }
        break;
    case 5: {
        l8_sound_444(o, self);
        uint32_t t = (uint32_t)l8_s16(o, self + 0x28) - 1u;
        l8_w16(o, self + 0x28, t);
        if ((int16_t)t == 0) l8_w8(o, self + 5, 0);
        break;
    }
    default:
        break;
    }
}

static void l8_001BF6B0(L8 *o, uint32_t self)
{
    uint32_t state = l8_u8(o, self + 4);
    uint32_t sub = self + 0x1F0;
    if (state == 3) {
        uint32_t model = l8_u32(o, self + 0x90);
        if (l8_c_001AF890(o, model)) return;
        if (l8_c_001B1190(o, (int32_t)l8_u8(o, self + 0x9A))) return;
        l8_c_001AFC10(o, self);
        return;
    }
    if (state == 2) {
        int32_t ended = 0;
        if (l8_c_001C1570(o, self, &ended)) return;
        if (ended) l8_w8(o, self + 4, 3);
        uint32_t a = L8_SUB(l8_u32(o, self + 0x64), F_0_01);
        int below = L8_LT(a, F_ZERO);
        l8_w32(o, self + 0x64, a);
        if (below) l8_w32(o, self + 0x64, 0);
        uint32_t b = L8_SUB(l8_u32(o, self + 0x38), 0x3ECCCCCDu);
        below = L8_LT(b, F_ZERO);
        l8_w32(o, self + 0x38, b);
        if (below) l8_w32(o, self + 0x38, 0);
        l8_001BF5B0(o, self, sub, 0);
        int32_t ignored = 0;
        if (l8_c_001D0D60(o, l8_u32(o, self + 0x90), fl(F_ONE), &ignored)) return;
        if (l8_c_001C6380(o, self)) return;
        if (!L8_EQ(F_ZERO, l8_u32(o, self + 0x64))) l8_callback(o, l8_u32(o, self + 0x4C), self);
        return;
    }
    if (state == 0) {
        l8_bf6b0_init(o, self, sub);
        return;
    }
    if (state != 1) return;
    if (l8_c_001B17A0(o, self)) return;
    l8_w8(o, self + 1, 0);
    int32_t here = 0;
    if (l8_c_001B2140(o, self, &here)) return;
    if (!here) return;
    l8_w8(o, self + 1, 1);
    l8_bf6b0_step(o, self, sub);
    uint32_t t = l8_u32(o, sub + 0x1C);
    if (t) l8_w32(o, sub + 0x1C, t - 1u);
    int32_t hit = l8_s16(o, self + 0x36);
    if (hit != 0) {
        if ((int16_t)hit & 0x5000) {
            if (l8_u32(o, sub + 0x1C) != 0) {
                l8_w16(o, self + 0x36, 0);
            } else {
                l8_w32(o, sub + 0x1C, 0x3C);
                if (l8_s16(o, self + 0x36) & 0x4000) {
                    if (l8_c_001EFE00(o, (int32_t)0x80000027u, self)) return;
                }
            }
        }
        int32_t damage = l8_s16(o, self + 0x36);
        int32_t hp = l8_s16(o, self + 0x34);
        l8_w16(o, self + 0x34, (uint32_t)(hp - (damage & 0xFFF)));
        if (!(l8_s16(o, self + 0x34) > 0)) {
            l8_w8(o, self, 2);
            l8_w16(o, self + 0x34, 0);
            l8_w8(o, self + 4, 2);
            l8_w8(o, self + 5, 0);
            int32_t ignored = 0;
            if (l8_c_001FBD50(o, self, 0x15D, 0, fl(F_300), &ignored)) return;
            if (l8_c_001FBD50(o, self, 0x448, 0, fl(F_300), &ignored)) return;
            if (l8_c_001C1500(o, self, 1, fl(0x40800000u), fl(F_6), fl(0x40800000u))) return;
        } else {
            l8_w8(o, self, 1);
            l8_w16(o, self + 0x36, 0);
        }
    }
    if (l8_c_001C6380(o, self)) return;
    int32_t frame = 0;
    if (l8_c_001D0D60(o, l8_u32(o, self + 0x90), fl(F_ONE), &frame)) return;
    l8_w16(o, sub, (uint32_t)frame);
    l8_callback(o, l8_u32(o, self + 0x4C), self);
}

int em_level8_port_001BF6B0(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    l8_001BF6B0(&o, self);
    return l8_end(&o);
}
