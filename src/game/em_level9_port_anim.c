/* Pose, animation and actor-state functions of the ninth level (see
 * em_level9_port.h, docs/LEVEL9_PORT.md): 001BA7F0 (a category-6 probe of
 * 001BA580 when D_00810700 == 0xD), 001BDCA0 / 001BDD70 (the AREA13 lift
 * door's wobble open / close), and three AREA19 behaviours that run from
 * its first frames: 001C06E0 ("bone_root_pulse" in the decomp: a label, not
 * evidence), 001C1030 and 001C4BA0.
 *
 * Ground truth: the original instructions; byte-identical decomp C for
 * 001BA7F0, 001BDCA0 and 001BDD70; for 001C06E0 / 001C1030 (NEARMISS) and
 * 001C4BA0 (word assembly) the instructions alone. Where the NEARMISS text
 * and the instructions differ the instructions win:
 *   - 001C1030 state 2 sub-state 0 calls 001C67E0(self, 0, 0, 0) (the text
 *     passes 0 as the actor and 2 as the clip);
 *   - 001C06E0 re-reads the halfword +0x36 after its hit-class logic on
 *     every path before subtracting its low 12 bits.
 */
#include "em_level9_port_internal.h"

#define D_0028A50C 0x0028A50Cu
#define D_00275650 0x00275650u
#define D_00275658 0x00275658u

/* ------------------------------------------------------------------------
 * 001BA7F0 (self): 00102948(0x700038A0, (+0x114) +0xC0); 00102948(
 * 0x700038B0, 0x700038A0); 0x700038B4 -= 20; when 0019A570(0x700038A0,
 * 0x700038B0, 4, 0) is nonzero: 00102948(0x700038A0, 0x700031B0), the hit
 * record (0x700031D0) +0x24..+0x2C into 0x700038B0..B8, and
 * 001F9100((+0x114) +0xC0, 0x700038A0, 0x700038B0, 5.0).
 * ---------------------------------------------------------------------- */
int em_level9_port_001BA7F0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    if (l9_c_00102948(&o, S_700038A0, l9_u32(&o, self + 0x114) + 0xC0)) return -1;
    if (l9_c_00102948(&o, S_700038B0, S_700038A0)) return -1;
    l9_w32(&o, S_700038B4, L9_SUB(l9_u32(&o, S_700038B4), F_20));
    if (l9_c_0019A570(&o, S_700038A0, S_700038B0, 4, 0, &r)) return -1;
    if (r != 0) {
        if (l9_c_00102948(&o, S_700038A0, S_700031B0)) return -1;
        uint32_t hit = l9_u32(&o, S_700031D0);
        l9_w32(&o, S_700038B0, l9_u32(&o, hit + 0x24));
        l9_w32(&o, S_700038B4, l9_u32(&o, hit + 0x28));
        l9_w32(&o, S_700038B8, l9_u32(&o, hit + 0x2C));
        l9_c_001F9100(&o, l9_u32(&o, self + 0x114) + 0xC0, S_700038A0, S_700038B0, F_5);
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 001BDCA0 (blk): blk +0x10 += 0.2; for the pose block's records 1..16
 * ((D_00275B40) + 4 k): +0x80 = blk +0x10, and when blk +0x10 is above
 * 2.5 k (not <=) the halfwords +0x88 / +0x8A / +0x8C = 0 (the record
 * re-read for each). Returns 1 when blk +0x10 is above 25.
 * 001BDD70 (blk): blk +0x10 -= 0.2 (below 0: the word 0); +0x80 the same,
 * and the halfwords = 0x1000 while blk +0x10 <= 2.5 k. Returns 1 when blk
 * +0x10 == 0.
 * ---------------------------------------------------------------------- */
static uint32_t pose_record(L9 *o, uint32_t k) { return l9_u32(o, l9_u32(o, D_00275B40) + 4u * k); }

static int32_t wobble(L9 *o, uint32_t blk, int opening)
{
    uint32_t step = 0x40200000u; /* 2.5 */
    uint32_t limit = step;
    for (uint32_t k = 1; k < 0x11 && !l9_failed(o); k++) {
        uint32_t base = l9_u32(o, D_00275B40);
        uint32_t v = l9_u32(o, blk + 0x10);
        uint32_t rec = l9_u32(o, base + 4u * k);
        l9_w32(o, rec + 0x80, v);
        int within = L9_LE(l9_u32(o, blk + 0x10), limit);
        if (opening ? !within : within) {
            uint32_t v = opening ? 0u : 0x1000u;
            l9_w16(o, pose_record(o, k) + 0x88, v);
            l9_w16(o, pose_record(o, k) + 0x8A, v);
            l9_w16(o, pose_record(o, k) + 0x8C, v);
        }
        limit = L9_ADD(limit, step);
    }
    if (opening) return !L9_LE(l9_u32(o, blk + 0x10), 0x41C80000u /* 25 */) ? 1 : 0;
    return L9_EQ(F_ZERO, l9_u32(o, blk + 0x10)) ? 1 : 0;
}

int em_level9_port_001BDCA0(const EmLevel9PortHooks *h, uint32_t blk, int32_t *result, EmLevel9PortFault *fault)
{
    L9 o;
    if (!result || l9_begin(&o, h, fault)) return -1;
    l9_w32(&o, blk + 0x10, L9_ADD(l9_u32(&o, blk + 0x10), 0x3E4CCCCDu /* 0.2 */));
    int32_t v = wobble(&o, blk, 1);
    if (l9_failed(&o)) return -1;
    *result = v;
    return 0;
}

int em_level9_port_001BDD70(const EmLevel9PortHooks *h, uint32_t blk, int32_t *result, EmLevel9PortFault *fault)
{
    L9 o;
    if (!result || l9_begin(&o, h, fault)) return -1;
    uint32_t v0 = L9_SUB(l9_u32(&o, blk + 0x10), 0x3E4CCCCDu /* 0.2 */);
    l9_w32(&o, blk + 0x10, v0);
    if (L9_LT(v0, F_ZERO)) l9_w32(&o, blk + 0x10, 0);
    int32_t v = wobble(&o, blk, 0);
    if (l9_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001C06E0 (e; NEARMISS, from the instructions): an AREA19 creature on a
 * root bone. tail = e +0x1F0; player = D_008102B0. By +4:
 *   0: 001B1020(e, 3, -1, 0); +0x58 = D_0028A50C, +0x52 = 1, +0x5D = 0x81,
 *      +0 = 1, +0x34 = 0x50, +0x30 = 0x275650, tail +0x18 = 0x275658, +0x2A
 *      = 0x1000, tail +0x1C = 0.
 *   1: only while 001B2140(e) is nonzero. By +5 (0 falls into 1):
 *      0: +0x28 = 0, +5 += 1, tail +4 = 001B1470(-yaw).
 *      1: 001BE5F0(player, e, tail) nonzero: +0x28 += 1; above 0x3C: +5 +=
 *         1 and tail +4 = 001B1470((aim + (-pi/18 + pi/9 r)) - yaw) with aim
 *         = 001B1240(e +0xB0, player x, z) and r = 00122BB8() 2^-31; else
 *         every 64th frame the same with (-pi/12 + pi/6 r). Then (+0x118)
 *         +0x74 = 001B12B0(tail +4, (+0x118) +0x74, 2 degrees). Zero: every
 *         128th frame tail +4 = 001B1470((-pi/2 + pi r) - yaw); (+0x118)
 *         +0x74 eased at 0.4 degrees; +0x28 = 0.
 *      2: the same 2-degree ease; once (+0x118) +0x74 == tail +4: the probe
 *         from (+0x118) +0xC0 along (0, 0, 20) turned by (+0x118) +0x90
 *         (0019A570(.., 6, 0)): blocked +0x28 = 0, +5 = 1; clear +5 += 1,
 *         +6 = 0.
 *      3: by +6 (a jump table at 0x26E280, 6 entries; 0 falls into 1):
 *         0 001FBD50(e, 0x43C, 0, 300), +0x2A = 0x1000, +6 += 1; 1 +0x2A +=
 *         0x20, above 0x2000: 0x2000, +6 += 1, +0x28 = 0x14; 2 +0x28 -= 1,
 *         at 0 +6 += 1; 3 +0x2A -= 0x100, below 0x1200 +6 += 1; 4 the strike
 *         (0x700038A0 = (0, 1.75, 7.91, 1) through (+0x11C) +0x90,
 *         0x700038B0 = (0, 0, 1, 0) through (+0x118) +0x90, 001EFFD0(
 *         0x8000005A, .., .., 0x14, 2), 001FBD50(e, 0x43B, 0, 300), +6 += 1,
 *         +0x2A -= 0x100); 5 +0x2A -= 0x2A, below 0x1000: 0x1000 and
 *         001BE5F0 nonzero: +5 = 2 with the 10-degree re-aim, else +5 = 0.
 *      Then 001C6380(e); tail +0x1C counts down; the hit +0x36 (see the
 *      code); the scale +0x2A to (+0x124) +0x8C / +0x8A / +0x88;
 *      001B17A0(e); the +0x4C method; 0x700038A0 = (0, 0.323, 0.742, 1)
 *      through (+0x128) +0x90; 0x700038B0 = (0x20, 0x70, 0x80, 0x80);
 *      001F4A00(0x700038A0, 0x700038B0).
 *   2: by +5: 0 (+0x11C) +0x70 += 1 degree, above 50 degrees: 50 degrees
 *      and +5 += 1; 1 001EFE00(0x8000001E, e) nonzero: +5 = 2, else +4 =
 *      3. Then +0x2A -= 0x40 (not below 0x1000), the scale to (+0x124),
 *      001C6380(e), the +0x4C method.
 *   3: 001B1190(+0x9A), 001AFC10(e).
 * ---------------------------------------------------------------------- */
#define K_TURN 0x3D0EFA35u     /* 2 degrees */
#define K_TURN_SLOW 0x3BE4C389u /* 0.4 degrees */

static uint32_t aim_with_spread(L9 *o, uint32_t e, uint32_t spread, uint32_t base)
{
    uint32_t aim = 0, out = 0;
    int32_t r = 0;
    uint32_t px = l9_u32(o, D_008102B0 + 0xA0), pz = l9_u32(o, D_008102B0 + 0xA8);
    if (l9_c_001B1240(o, e + 0xB0, px, pz, &aim)) return 0;
    if (l9_c_00122BB8(o, &r)) return 0;
    uint32_t f = L9_MUL(F_2PM31, L9_CVT(r));
    f = L9_MUL(spread, f);
    uint32_t yaw = l9_u32(o, e + 0xC4);
    f = L9_ADD(base, f);
    f = L9_ADD(aim, f);
    if (l9_c_001B1470(o, L9_SUB(f, yaw), &out)) return 0;
    return out;
}

static void ease_yaw(L9 *o, uint32_t e, uint32_t tail, uint32_t rate)
{
    uint32_t out = 0;
    uint32_t sub = l9_u32(o, e + 0x118);
    uint32_t cur = l9_u32(o, sub + 0x74);
    if (l9_c_001B12B0(o, l9_u32(o, tail + 4), cur, rate, &out)) return;
    l9_w32(o, l9_u32(o, e + 0x118) + 0x74, out);
}

static void scale_out(L9 *o, uint32_t e)
{
    uint32_t t = l9_u16(o, e + 0x2A);
    l9_w16(o, l9_u32(o, e + 0x124) + 0x8C, t);
    l9_w16(o, l9_u32(o, e + 0x124) + 0x8A, t);
    l9_w16(o, l9_u32(o, e + 0x124) + 0x88, t);
}

static void sound(L9 *o, uint32_t e, int32_t id)
{
    int32_t r = 0;
    l9_c_001FBD50(o, e, id, 0, F_300, &r);
}

static void attack(L9 *o, uint32_t e, uint32_t tail)
{
    int32_t r = 0;
    switch (l9_u8(o, e + 6)) {
    case 0:
        sound(o, e, 0x43C);
        l9_w16(o, e + 0x2A, 0x1000);
        l9_w8(o, e + 6, l9_u8(o, e + 6) + 1u);
        /* fall through */
    case 1:
        l9_w16(o, e + 0x2A, (uint32_t)(l9_s16(o, e + 0x2A) + 0x20));
        if (!(l9_s16(o, e + 0x2A) < 0x2001)) {
            l9_w16(o, e + 0x2A, 0x2000);
            l9_w8(o, e + 6, l9_u8(o, e + 6) + 1u);
            l9_w16(o, e + 0x28, 0x14);
        }
        break;
    case 2: {
        int32_t t = (int16_t)(l9_s16(o, e + 0x28) - 1);
        l9_w16(o, e + 0x28, (uint32_t)t);
        if (t == 0) l9_w8(o, e + 6, l9_u8(o, e + 6) + 1u);
        break;
    }
    case 3:
        l9_w16(o, e + 0x2A, (uint32_t)(l9_s16(o, e + 0x2A) - 0x100));
        if (l9_s16(o, e + 0x2A) < 0x1200) l9_w8(o, e + 6, l9_u8(o, e + 6) + 1u);
        break;
    case 4:
        l9_w32(o, S_700038A0, F_ZERO);
        l9_w32(o, S_700038A4, 0x3FE00000u /* 1.75 */);
        l9_w32(o, S_700038A8, 0x40FD1EB8u /* 7.91 */);
        l9_w32(o, S_700038AC, F_ONE);
        if (l9_c_001026A0(o, S_700038A0, l9_u32(o, e + 0x11C) + 0x90, S_700038A0)) return;
        l9_w32(o, S_700038B0, F_ZERO);
        l9_w32(o, S_700038B4, F_ZERO);
        l9_w32(o, S_700038B8, F_ONE);
        l9_w32(o, S_700038BC, F_ZERO);
        if (l9_c_001026A0(o, S_700038B0, l9_u32(o, e + 0x118) + 0x90, S_700038B0)) return;
        if (l9_c_001EFFD0(o, (int32_t)0x8000005Au, S_700038A0, S_700038B0, 0x14, F_2)) return;
        sound(o, e, 0x43B);
        l9_w8(o, e + 6, l9_u8(o, e + 6) + 1u);
        l9_w16(o, e + 0x2A, (uint32_t)(l9_s16(o, e + 0x2A) - 0x100));
        break;
    case 5:
        l9_w16(o, e + 0x2A, (uint32_t)(l9_s16(o, e + 0x2A) - 0x2A));
        if (l9_s16(o, e + 0x2A) < 0x1000) {
            l9_w16(o, e + 0x2A, 0x1000);
            if (l9_c_001BE5F0(o, D_008102B0, e, tail, &r)) return;
            if (r != 0) {
                l9_w8(o, e + 5, 2);
                uint32_t v = aim_with_spread(o, e, 0x3EB2B8C3u /* pi/9 */, 0xBE32B8C3u /* -pi/18 */);
                l9_w32(o, tail + 4, v);
            } else {
                l9_w8(o, e + 5, 0);
            }
        }
        break;
    default:
        break;
    }
}

static void alive(L9 *o, uint32_t e, uint32_t tail)
{
    int32_t r = 0;
    if (l9_c_001B2140(o, e, &r) || r == 0) return;
    switch (l9_u8(o, e + 5)) {
    case 0: {
        uint32_t out = 0;
        l9_w16(o, e + 0x28, 0);
        l9_w8(o, e + 5, l9_u8(o, e + 5) + 1u);
        if (l9_c_001B1470(o, L9_NEG(l9_u32(o, e + 0xC4)), &out)) return;
        l9_w32(o, tail + 4, out);
    }
        /* fall through */
    case 1:
        if (l9_c_001BE5F0(o, D_008102B0, e, tail, &r)) return;
        if (r != 0) {
            l9_w16(o, e + 0x28, (uint32_t)(l9_s16(o, e + 0x28) + 1));
            if (!(l9_s16(o, e + 0x28) < 0x3D)) {
                l9_w8(o, e + 5, l9_u8(o, e + 5) + 1u);
                uint32_t v = aim_with_spread(o, e, 0x3EB2B8C3u /* pi/9 */, 0xBE32B8C3u /* -pi/18 */);
                if (l9_failed(o)) return;
                l9_w32(o, tail + 4, v);
            } else if ((l9_u32(o, S_70003B68) & 0x3Fu) == 0) {
                uint32_t v = aim_with_spread(o, e, 0x3F060A92u /* pi/6 */, 0xBE860A92u /* -pi/12 */);
                if (l9_failed(o)) return;
                l9_w32(o, tail + 4, v);
            }
            ease_yaw(o, e, tail, K_TURN);
        } else {
            if ((l9_u32(o, S_70003B68) & 0x7Fu) == 0) {
                uint32_t out = 0;
                if (l9_c_00122BB8(o, &r)) return;
                uint32_t f = L9_MUL(F_2PM31, L9_CVT(r));
                f = L9_MUL(F_PI, f);
                uint32_t yaw = l9_u32(o, e + 0xC4);
                f = L9_ADD(0xBFC90FDBu /* -pi/2 */, f);
                if (l9_c_001B1470(o, L9_SUB(f, yaw), &out)) return;
                l9_w32(o, tail + 4, out);
            }
            ease_yaw(o, e, tail, K_TURN_SLOW);
            l9_w16(o, e + 0x28, 0);
        }
        break;
    case 2: {
        ease_yaw(o, e, tail, K_TURN);
        uint32_t sub = l9_u32(o, e + 0x118);
        uint32_t want = l9_u32(o, tail + 4);
        if (L9_EQ(want, l9_u32(o, sub + 0x74))) {
            if (l9_c_00102948(o, S_700038A0, sub + 0xC0)) return;
            l9_w32(o, S_700038B0, 0);
            l9_w32(o, S_700038B4, 0);
            l9_w32(o, S_700038B8, F_20);
            l9_w32(o, S_700038BC, F_ONE);
            if (l9_c_001026A0(o, S_700038B0, l9_u32(o, e + 0x118) + 0x90, S_700038B0)) return;
            if (l9_c_0019A570(o, S_700038A0, S_700038B0, 6, 0, &r)) return;
            if (r != 0) {
                l9_w16(o, e + 0x28, 0);
                l9_w8(o, e + 5, 1);
            } else {
                l9_w8(o, e + 5, l9_u8(o, e + 5) + 1u);
                l9_w8(o, e + 6, 0);
            }
        }
        break;
    }
    case 3:
        attack(o, e, tail);
        break;
    default:
        break;
    }
    if (l9_failed(o)) return;
    if (l9_c_001C6380(o, e)) return;
    {
        uint32_t c = l9_u32(o, tail + 0x1C);
        if (c != 0) l9_w32(o, tail + 0x1C, c - 1u);
    }
    int32_t f = l9_s16(o, e + 0x36);
    if (f != 0) {
        if (!(f & 0x8000) && (f & 0x5000)) {
            if (l9_u32(o, tail + 0x1C) == 0) {
                l9_w32(o, tail + 0x1C, 0x3C);
                if (l9_s16(o, e + 0x36) & 0x4000) {
                    if (l9_c_001EFE00(o, (int32_t)0x80000027u, e, &r)) return;
                }
            } else {
                l9_w16(o, e + 0x36, 0);
            }
        }
        int32_t hit = l9_s16(o, e + 0x36);
        int32_t pool = l9_s16(o, e + 0x34);
        l9_w16(o, e + 0x34, (uint32_t)(pool - (hit & 0xFFF)));
        if (!(l9_s16(o, e + 0x34) > 0)) {
            l9_w8(o, e + 4, 2);
            l9_w8(o, e + 5, 0);
            l9_w8(o, e + 0, 2);
            sound(o, e, 0x15D);
            sound(o, e, 0x440);
        } else {
            l9_w8(o, e + 0, 1);
            l9_w16(o, e + 0x36, 0);
        }
    }
    if (l9_failed(o)) return;
    scale_out(o, e);
    if (l9_c_001B17A0(o, e, &r)) return;
    if (l9_method(o, e)) return;
    l9_w32(o, S_700038A0, F_ZERO);
    l9_w32(o, S_700038A4, 0x3EA56042u /* 0.323 */);
    l9_w32(o, S_700038A8, 0x3F3DF3B6u /* 0.742 */);
    l9_w32(o, S_700038AC, F_ONE);
    if (l9_c_001026A0(o, S_700038A0, l9_u32(o, e + 0x128) + 0x90, S_700038A0)) return;
    l9_w32(o, S_700038B0, 0x20);
    l9_w32(o, S_700038B4, 0x70);
    l9_w32(o, S_700038B8, 0x80);
    l9_w32(o, S_700038BC, 0x80);
    l9_c_001F4A00(o, S_700038A0, S_700038B0);
}

static void dying(L9 *o, uint32_t e)
{
    int32_t r = 0;
    switch (l9_u8(o, e + 5)) {
    case 0: {
        uint32_t sub = l9_u32(o, e + 0x11C);
        l9_w32(o, sub + 0x70, L9_ADD(l9_u32(o, sub + 0x70), 0x3C8EFA35u /* 1 degree */));
        sub = l9_u32(o, e + 0x11C);
        if (!L9_LE(l9_u32(o, sub + 0x70), 0x3F5F66F3u /* 50 degrees */)) {
            l9_w32(o, sub + 0x70, 0x3F5F66F3u);
            l9_w8(o, e + 5, l9_u8(o, e + 5) + 1u);
        }
        break;
    }
    case 1:
        if (l9_c_001EFE00(o, (int32_t)0x8000001Eu, e, &r)) return;
        if (r != 0) l9_w8(o, e + 5, 2);
        else l9_w8(o, e + 4, 3);
        break;
    default:
        break;
    }
    l9_w16(o, e + 0x2A, (uint32_t)(l9_s16(o, e + 0x2A) - 0x40));
    if (l9_s16(o, e + 0x2A) < 0x1000) l9_w16(o, e + 0x2A, 0x1000);
    scale_out(o, e);
    if (l9_c_001C6380(o, e)) return;
    l9_method(o, e);
}

int em_level9_port_001C06E0(const EmLevel9PortHooks *h, uint32_t e, EmLevel9PortFault *fault)
{
    L9 o;
    if (l9_begin(&o, h, fault)) return -1;
    uint32_t tail = e + 0x1F0;
    switch (l9_u8(&o, e + 4)) {
    case 0:
        if (l9_c_001B1020(&o, e, 3, -1, 0)) break;
        l9_w32(&o, e + 0x58, l9_u32(&o, D_0028A50C));
        l9_w16(&o, e + 0x52, 1);
        l9_w8(&o, e + 0x5D, 0x81);
        l9_w8(&o, e + 0, 1);
        l9_w16(&o, e + 0x34, 0x50);
        l9_w32(&o, e + 0x30, D_00275650);
        l9_w32(&o, tail + 0x18, D_00275658);
        l9_w16(&o, e + 0x2A, 0x1000);
        l9_w32(&o, tail + 0x1C, 0);
        break;
    case 1:
        alive(&o, e, tail);
        break;
    case 2:
        dying(&o, e);
        break;
    case 3:
        if (l9_c_001B1190(&o, (int32_t)l9_u8(&o, e + 0x9A))) break;
        l9_c_001AFC10(&o, e);
        break;
    default:
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 001C1030 (self; NEARMISS, from the instructions): an AREA19 actor that
 * is knocked back when hit. By +4:
 *   0: 001B10B0(self, 0x26, 0x27) nonzero: stop. 001C63E0(self, 1);
 *      001C67E0(self, 1, 0, (float)001C6160(self)); +0x34 = 1, +0 = 1, +4 =
 *      1; 00102948(+0xA0, +0xB0); 001D8BF0(self, 1); the facing matrix
 *      0x700036A0 (001029C0, 00102BB0 by +0xC4) and 00102948(0x700036D0,
 *      +0xA0); 0x700038A0 = (40, 0, 0, 1) through it; when 0019A570(+0xB0,
 *      0x700038A0, 4, 0) is nonzero: 0x700038A0 = 31B0 - +0xB0, 0x700038A8
 *      = 31B8 - +0xB8, 0x70003A20 = 0011E748(x x + z z), +0x60 = that / 20.
 *      Then 00102948(0x700038A0, +0xB0), 0x700038A4 -= 40, and when
 *      0019A570(+0xB0, 0x700038A0, 4, 0) is nonzero: 0x70003A20 = 31B4 -
 *      +0xB4, then 0011E748(that squared), +0x64 = that / 20.
 *   1: by +5: 0 the hit +0x36 (bit 12: +0x34 -= low 12 bits, at 0 or
 *      below +0 = 2, +4 = 2, +5 = 0; else +0 = 2, +0x36 = 0, +5 = 1; no bit
 *      12: +0x36 = 0, +0 = 2, +5 = 1); 1 00102948(+0xB0, +0xA0), +0x28 =
 *      0x14, +0x38 = 0, +5 += 1, and on into 2; 2 +0x38 -= pi/10, the
 *      offset (0, 0, 0.5 sin(+0x38), 1) turned by +0xC4 from +0xA0 into
 *      +0xB0, +0x28 -= 1, at 0 +0 = 1, +5 = 0, 00102948(+0xB0, +0xA0).
 *      Then 001C64F0(self, 1.0), 001C68C0(self), 001B17A0(self), the +0x4C
 *      method.
 *   2: by +5: 0 001C67E0(self, 0, 0, 0), +5 += 1, the block's halfword
 *      +0x1F0 = 0, 001FB9F0(0x442, 0x1000, 0x1000, 0x1000); 1 with bit 12
 *      of +0x1F0: +4 = 3. Then 001C64F0(self, 1.0), 001C68C0(self), the
 *      +0x4C method.
 *   3: 001B1190(+0x9A), 001AFC10(self).
 * ---------------------------------------------------------------------- */
#define F_40 0x42200000u

int em_level9_port_001C1030(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    uint32_t f = 0;
    if (l9_begin(&o, h, fault)) return -1;
    uint32_t blk = self + 0x1F0;
    switch (l9_u8(&o, self + 4)) {
    case 0: {
        if (l9_c_001B10B0(&o, self, 0x26, 0x27, &r) || r != 0) break;
        if (l9_c_001C63E0(&o, self, 1)) break;
        if (l9_c_001C6160(&o, self, &r)) break;
        if (l9_c_001C67E0(&o, self, 1, F_ZERO, L9_CVT(r))) break;
        l9_w16(&o, self + 0x34, 1);
        l9_w8(&o, self + 0, 1);
        l9_w8(&o, self + 4, 1);
        if (l9_c_00102948(&o, self + 0xA0, self + 0xB0)) break;
        if (l9_c_001D8BF0(&o, self, 1)) break;
        if (l9_c_001029C0(&o, S_700036A0)) break;
        if (l9_c_00102BB0(&o, S_700036A0, S_700036A0, l9_u32(&o, self + 0xC4))) break;
        if (l9_c_00102948(&o, S_700036D0, self + 0xA0)) break;
        l9_w32(&o, S_700038A0, F_40);
        l9_w32(&o, S_700038A4, 0);
        l9_w32(&o, S_700038A8, 0);
        l9_w32(&o, S_700038AC, F_ONE);
        if (l9_c_001026A0(&o, S_700038A0, S_700036A0, S_700038A0)) break;
        if (l9_c_0019A570(&o, self + 0xB0, S_700038A0, 4, 0, &r)) break;
        if (r != 0) {
            uint32_t hx = l9_u32(&o, S_700031B0);
            uint32_t sx = l9_u32(&o, self + 0xB0);
            uint32_t hz = l9_u32(&o, S_700031B8);
            l9_w32(&o, S_700038A0, L9_SUB(hx, sx));
            uint32_t sz = l9_u32(&o, self + 0xB8);
            l9_w32(&o, S_700038A8, L9_SUB(hz, sz));
            uint32_t x = l9_u32(&o, S_700038A0), z = l9_u32(&o, S_700038A8);
            if (l9_c_0011E748(&o, L9_MADD(L9_MULA(x, x), z, z), &f)) break;
            l9_w32(&o, S_70003A20, f);
            l9_w32(&o, self + 0x60, L9_DIV(l9_u32(&o, S_70003A20), F_20));
        }
        if (l9_c_00102948(&o, S_700038A0, self + 0xB0)) break;
        l9_w32(&o, S_700038A4, L9_SUB(l9_u32(&o, S_700038A4), F_40));
        if (l9_c_0019A570(&o, self + 0xB0, S_700038A0, 4, 0, &r)) break;
        if (r != 0) {
            uint32_t dy = L9_SUB(l9_u32(&o, S_700031B4), l9_u32(&o, self + 0xB4));
            l9_w32(&o, S_70003A20, dy);
            if (l9_c_0011E748(&o, L9_MUL(dy, dy), &f)) break;
            l9_w32(&o, S_70003A20, f);
            l9_w32(&o, self + 0x64, L9_DIV(l9_u32(&o, S_70003A20), F_20));
        }
        break;
    }
    case 1:
        switch (l9_u8(&o, self + 5)) {
        case 0: {
            int32_t v36 = l9_s16(&o, self + 0x36);
            if (v36 == 0) break;
            if (v36 & 0x1000) {
                l9_w16(&o, self + 0x34, (uint32_t)(l9_s16(&o, self + 0x34) - (v36 & 0xFFF)));
                if (!(l9_s16(&o, self + 0x34) > 0)) {
                    l9_w8(&o, self + 0, 2);
                    l9_w8(&o, self + 4, 2);
                    l9_w8(&o, self + 5, 0);
                } else {
                    l9_w8(&o, self + 0, 2);
                    l9_w16(&o, self + 0x36, 0);
                    l9_w8(&o, self + 5, 1);
                }
            } else {
                l9_w16(&o, self + 0x36, 0);
                l9_w8(&o, self + 0, 2);
                l9_w8(&o, self + 5, 1);
            }
            break;
        }
        case 1:
            if (l9_c_00102948(&o, self + 0xB0, self + 0xA0)) break;
            l9_w16(&o, self + 0x28, 0x14);
            l9_w32(&o, self + 0x38, 0);
            l9_w8(&o, self + 5, l9_u8(&o, self + 5) + 1u);
            /* fall through */
        case 2: {
            if (l9_failed(&o)) break;
            uint32_t a = L9_SUB(l9_u32(&o, self + 0x38), 0x3EA0D97Cu /* pi/10 */);
            l9_w32(&o, self + 0x38, a);
            l9_w32(&o, S_70003600, 0);
            l9_w32(&o, S_70003604, 0);
            if (l9_c_0011E2A8(&o, l9_u32(&o, self + 0x38), &f)) break;
            l9_w32(&o, S_70003608, L9_MUL(F_HALF, f));
            l9_w32(&o, S_7000360C, F_ONE);
            if (l9_c_001029C0(&o, S_70003400)) break;
            if (l9_c_00102BB0(&o, S_70003400, S_70003400, l9_u32(&o, self + 0xC4))) break;
            if (l9_c_00102948(&o, S_70003430, self + 0xA0)) break;
            if (l9_c_001026A0(&o, self + 0xB0, S_70003400, S_70003600)) break;
            int32_t t = (int16_t)(l9_s16(&o, self + 0x28) - 1);
            l9_w16(&o, self + 0x28, (uint32_t)t);
            if (t == 0) {
                l9_w8(&o, self + 0, 1);
                l9_w8(&o, self + 5, 0);
                l9_c_00102948(&o, self + 0xB0, self + 0xA0);
            }
            break;
        }
        default:
            break;
        }
        if (l9_failed(&o)) break;
        if (l9_c_001C64F0(&o, self, F_ONE, &r)) break;
        if (l9_c_001C68C0(&o, self)) break;
        if (l9_c_001B17A0(&o, self, &r)) break;
        l9_method(&o, self);
        break;
    case 2:
        switch (l9_u8(&o, self + 5)) {
        case 0:
            if (l9_c_001C67E0(&o, self, 0, F_ZERO, F_ZERO)) break;
            l9_w8(&o, self + 5, l9_u8(&o, self + 5) + 1u);
            l9_w16(&o, blk, 0);
            l9_c_001FB9F0(&o, 0x442, 0x1000, 0x1000, 0x1000);
            break;
        case 1:
            if (l9_s16(&o, blk) & 0x1000) l9_w8(&o, self + 4, 3);
            break;
        default:
            break;
        }
        if (l9_failed(&o)) break;
        if (l9_c_001C64F0(&o, self, F_ONE, &r)) break;
        if (l9_c_001C68C0(&o, self)) break;
        l9_method(&o, self);
        break;
    case 3:
        if (l9_c_001B1190(&o, (int32_t)l9_u8(&o, self + 0x9A))) break;
        l9_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l9_end(&o);
}

/* ------------------------------------------------------------------------
 * 001C4BA0 (self; word assembly, from the instructions): an AREA19 part
 * that follows its parent (+0x18). By +4 (+4 and +0x18 read first):
 *   0: 001B0FD0(self) zero: 001CA5E0(self, +0x44, 1), 001C6380(self).
 *   1: +3 == 4: 00102958((+0x110) +0x90, ((parent +0x18) +0x110) +0x90),
 *      001031E0((+0x110) +0xC0, self +0xB0); else (+0x110) +0x7C = (parent
 *      +0x110) +0x7C and 001C6380(self). Then 001B17A0(self) and the +0x4C
 *      method.
 *   2, 3: 001AFC10(self). Others: nothing.
 * ---------------------------------------------------------------------- */
int em_level9_port_001C4BA0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault)
{
    L9 o;
    int32_t r = 0;
    if (l9_begin(&o, h, fault)) return -1;
    uint32_t state = l9_u8(&o, self + 4);
    uint32_t parent = l9_u32(&o, self + 0x18);
    switch (state) {
    case 0:
        if (l9_c_001B0FD0(&o, self, &r) || r != 0) break;
        if (l9_c_001CA5E0(&o, self, l9_u32(&o, self + 0x44), 1)) break;
        l9_c_001C6380(&o, self);
        break;
    case 1:
        if (l9_u8(&o, self + 3) == 4u) {
            uint32_t m = l9_u32(&o, parent + 0x18);
            uint32_t mine = l9_u32(&o, self + 0x110);
            uint32_t theirs = l9_u32(&o, m + 0x110);
            if (l9_c_00102958(&o, mine + 0x90, theirs + 0x90)) break;
            if (l9_c_001031E0(&o, l9_u32(&o, self + 0x110) + 0xC0, self + 0xB0)) break;
        } else {
            uint32_t theirs = l9_u32(&o, parent + 0x110);
            uint32_t mine = l9_u32(&o, self + 0x110);
            l9_w32(&o, mine + 0x7C, l9_u32(&o, theirs + 0x7C));
            if (l9_c_001C6380(&o, self)) break;
        }
        if (l9_c_001B17A0(&o, self, &r)) break;
        l9_method(&o, self);
        break;
    case 2:
    case 3:
        l9_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l9_end(&o);
}
