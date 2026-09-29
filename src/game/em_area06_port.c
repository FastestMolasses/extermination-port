/* AREA01 upper floor / AREA06 boot functions (see em_area06_port.h,
 * docs/AREA06_PORT.md).
 *
 * Each function below is a translation of the original code at the named
 * address. Calls, their arguments and the memory accesses between each two
 * calls follow the original instructions: the test compares memory at every
 * call entry and after the last store, and the memory accesses between
 * calls one for one, in order, by address and size (docs/AREA06_PORT.md
 * section 3). Where the original loads the operands of one expression in a
 * set order, the translation loads them in separate statements in that
 * order, because C leaves the order of evaluation of operands open. Float
 * arithmetic is the EE model (em_ee_float.h) on bit patterns, with the
 * original's operand order for each add/sub/mul/div/madd/compare; a float
 * the original only moves is carried as its bits.
 */
#include "em_area06_port_internal.h"

/* ------------------------------------------------------------------------
 * 001885B0 (C byte-identical): the signed halfword D_002754CC[actor +0x235
 * & 1].
 * ---------------------------------------------------------------------- */
void a6_001885B0(A6 *o, uint32_t actor, int32_t *result)
{
    uint32_t b = a6_u8(o, actor + 0x235);
    *result = a6_s16(o, D_002754CC + ((b & 1u) << 1));
}

int em_area06_port_001885B0(const EmArea06PortHooks *h, uint32_t actor, int32_t *result, EmArea06PortFault *fault)
{
    A6 o;
    int32_t r = 0;
    if (!result || a6_begin(&o, h, fault)) return -1;
    a6_001885B0(&o, actor, &r);
    if (a6_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0019F680 (word asm; from the instructions). A leaf: n = rec +0x18 (byte),
 * the halfword list at *0x70003204 + rec +0x1C. index < n (signed): the
 * three words of the 12-byte row *0x700031FC + 12 list[index] are copied to
 * out +0, +4, +8 (list[index] and *0x700031FC read again for each), and
 * the result is rec +0x18 read again; otherwise 0.
 * ---------------------------------------------------------------------- */
void a6_0019F680(A6 *o, uint32_t out, uint32_t rec, int32_t index, int32_t *result)
{
    uint32_t n = a6_u8(o, rec + 0x18);
    uint32_t off = a6_u32(o, rec + 0x1C);
    uint32_t base = a6_u32(o, S_70003204);
    if (!(index < (int32_t)n)) {
        *result = 0;
        return;
    }
    uint32_t at = base + off + ((uint32_t)index << 1);
    for (uint32_t k = 0; k < 3; k++) {
        int32_t row = a6_s16(o, at);
        uint32_t table = a6_u32(o, S_700031FC);
        uint32_t v = a6_u32(o, table + (uint32_t)row * 12u + 4u * k);
        a6_w32(o, out + 4u * k, v);
    }
    *result = (int32_t)a6_u8(o, rec + 0x18);
}

int em_area06_port_0019F680(const EmArea06PortHooks *h, uint32_t out, uint32_t rec, int32_t index, int32_t *result,
                            EmArea06PortFault *fault)
{
    A6 o;
    int32_t r = 0;
    if (!result || a6_begin(&o, h, fault)) return -1;
    a6_0019F680(&o, out, rec, index, &r);
    if (a6_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001EA210 (C byte-identical): 001D2DE0(2, 001281C0(value)); the float
 * argument goes on to 001281C0 in f12 unchanged.
 * ---------------------------------------------------------------------- */
void a6_001EA210(A6 *o, uint32_t value_bits)
{
    int32_t r = 0;
    if (a6_c_001281C0(o, fl(value_bits), &r)) return;
    a6_c_001D2DE0(o, 2, r);
}

int em_area06_port_001EA210(const EmArea06PortHooks *h, float value, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_001EA210(&o, a6_bits(value));
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 001EBE10 (NEARMISS; the instructions agree with its C) and 001ECA20 (C
 * byte-identical): one or two 001CFB50 / 001CFBE0 pairs on the packet
 * D_0081F8F0, the view floats *D_00275C34 +0x54 / +0x5C read for each.
 * ---------------------------------------------------------------------- */
static void a6_packet(A6 *o, uint32_t a0, uint32_t a1, uint32_t last, int32_t mode, uint32_t table)
{
    uint32_t g = a6_u32(o, D_00275C34);
    uint32_t f12 = a6_u32(o, g + 0x54);
    uint32_t f13 = a6_u32(o, g + 0x5C);
    if (a6_c_001CFB50(o, D_0081F8F0, 0, (int32_t)a0, fl(f12), fl(f13), fl(F_ONE), fl(F_1EM6), fl(last))) return;
    a6_c_001CFBE0(o, (int32_t)a1, mode, table, D_0081F8F0, 0);
}

int em_area06_port_001EBE10(const EmArea06PortHooks *h, uint32_t a0, uint32_t a1, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_packet(&o, a0, a1, F_3, 1, 0x002564C0u);
    return a6_end(&o);
}

int em_area06_port_001ECA20(const EmArea06PortHooks *h, uint32_t a0, uint32_t a1, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_packet(&o, a0, a1, F_ZERO, 2, 0x00256DC0u);
    a6_packet(&o, a0, a1, F_ZERO, 1, 0x00256E50u);
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 002072A0 (C byte-identical): the sound 0x8CD (001FB9F0, three 0x1000).
 * ---------------------------------------------------------------------- */
void a6_002072A0(A6 *o) { a6_c_001FB9F0(o, 0x8CD, 0x1000, 0x1000, 0x1000); }

int em_area06_port_002072A0(const EmArea06PortHooks *h, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_002072A0(&o);
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 002072C0 (C byte-identical): by self +3: 0 allocates a record
 * (001AFF10), sets its +0x10 to 00207350 and +0x20 to self, self +3 + 1,
 * 001AED80(0); 1 00207D00(1, 3), 001B0000(); other values nothing.
 * ---------------------------------------------------------------------- */
int em_area06_port_002072C0(const EmArea06PortHooks *h, uint32_t self, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    uint32_t s = a6_u8(&o, self + 3);
    if (s == 1) {
        if (!a6_c_00207D00(&o, 1, 3)) a6_c_001B0000(&o);
    } else if (s == 0) {
        uint32_t p = 0;
        if (!a6_c_001AFF10(&o, &p)) {
            a6_w32(&o, p + 0x10, 0x00207350u);
            a6_w32(&o, p + 0x20, self);
            uint32_t v = a6_u8(&o, self + 3);
            a6_w8(&o, self + 3, v + 1);
            a6_c_001AED80(&o, 0);
        }
    }
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 00207CA0 / 00207CD0 (inline asm; from the instructions): one 00207E40
 * sprite each, with the 64-bit word tex +0x78 / +0x80.
 * ---------------------------------------------------------------------- */
void a6_00207CA0(A6 *o, uint32_t tex)
{
    uint64_t q = a6_u64(o, tex + 0x78);
    a6_c_00207E40(o, 1, 0x8AE0, 0x7B30, 8, 0x10, 0x80808080u, q);
}

void a6_00207CD0(A6 *o, uint32_t tex)
{
    uint64_t q = a6_u64(o, tex + 0x80);
    a6_c_00207E40(o, 1, 0x8B10, 0x7AF0, 0x20, 0x18, 0x80808080u, q);
}

int em_area06_port_00207CA0(const EmArea06PortHooks *h, uint32_t tex, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_00207CA0(&o, tex);
    return a6_end(&o);
}

int em_area06_port_00207CD0(const EmArea06PortHooks *h, uint32_t tex, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_00207CD0(&o, tex);
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 002079F0 (NEARMISS; the instructions agree with its C): the keypad
 * page's frame: five 00207E40 sprites (tex +0, +8, +0x10, +0x18, +0x90),
 * then the cursor sprite at the cell page +0x28: x = 001281C0(16 (x0 +
 * 0x700)), y = 001281C0(16 ((y0 >> 1) + 0x790)) with (x0, y0) the
 * halfwords D_00264FE0[cell] (y read first), width 0x20 below cell 10
 * else 0x40, the texture word tex +0x20 + 8 D_00265010[cell].
 * ---------------------------------------------------------------------- */
void a6_002079F0(A6 *o, uint32_t page, uint32_t tex)
{
    static const uint32_t q[5][5] = {
        { 0x00, 0x7000, 0x7900, 0x100, 0xE0 }, { 0x08, 0x8000, 0x7900, 0x100, 0xE0 },
        { 0x10, 0x7000, 0x8000, 0x100, 0xE0 }, { 0x18, 0x8000, 0x8000, 0x100, 0xE0 },
        { 0x90, 0x8570, 0x7E60, 0x80, 0x80 },
    };
    for (unsigned k = 0; k < 5; k++) {
        uint64_t w = a6_u64(o, tex + q[k][0]);
        if (a6_c_00207E40(o, 1, (int32_t)q[k][1], (int32_t)q[k][2], (int32_t)q[k][3], (int32_t)q[k][4], 0x80808080u,
                          w))
            return;
    }
    int32_t cell = a6_s16(o, page + 0x28);
    int32_t y0 = a6_s16(o, D_00264FE0 + 2u + (uint32_t)cell * 4u);
    int32_t x0 = a6_s16(o, D_00264FE0 + (uint32_t)cell * 4u);
    int32_t x = 0, y = 0;
    if (a6_c_001281C0(o, fl(A6_MUL(F_16, A6_CVT(x0 + 0x700))), &x)) return;
    if (a6_c_001281C0(o, fl(A6_MUL(F_16, A6_CVT((y0 >> 1) + 0x790))), &y)) return;
    int32_t span = cell < 10 ? 0x20 : 0x40;
    uint32_t row = a6_u8(o, D_00265010 + (uint32_t)cell);
    uint64_t w = a6_u64(o, (row << 3) + tex + 0x20);
    a6_c_00207E40(o, 1, x, y, span, 0x20, 0x80808080u, w);
}

int em_area06_port_002079F0(const EmArea06PortHooks *h, uint32_t page, uint32_t tex, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_002079F0(&o, page, tex);
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 00207BB0 (C byte-identical): the typed characters. A 2-byte frame local
 * at sp - 4 is set from the halfword D_00275868; for i = 0..7: while i <
 * page +0xA (read each time) its first byte = 0x7F and 001CC1E0(1, 0x7C2 +
 * 16i, 0x7E8, 0xA, 0x14, &local, 0), 00207D00(1, 3); then 00207E40(1,
 * 001281C0(16 (0x7C0 + 16i)), 0x7F00, 0x10, 0x10, 0x80808080, tex +0x88).
 * ---------------------------------------------------------------------- */
void a6_00207BB0(A6 *o, uint32_t page, uint32_t tex, uint32_t sp)
{
    uint32_t local = sp - 0x70 + 0x6C;
    a6_w16(o, local, a6_u16(o, D_00275868));
    for (int32_t i = 0; i < 8; i++) {
        uint32_t n = a6_u8(o, page + 0xA);
        if (i < (int32_t)n) {
            a6_w8(o, local, 0x7F);
            if (a6_c_001CC1E0(o, 1, 0x7C2 + 0x10 * i, 0x7E8, 0xA, 0x14, local, 0)) return;
            if (a6_c_00207D00(o, 1, 3)) return;
        }
        int32_t x = 0;
        if (a6_c_001281C0(o, fl(A6_MUL(F_16, A6_CVT(0x7C0 + 0x10 * i))), &x)) return;
        uint64_t w = a6_u64(o, tex + 0x88);
        if (a6_c_00207E40(o, 1, x, 0x7F00, 0x10, 0x10, 0x80808080u, w)) return;
    }
}

int em_area06_port_00207BB0(const EmArea06PortHooks *h, uint32_t page, uint32_t tex, uint32_t sp,
                            EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_00207BB0(&o, page, tex, sp);
    return a6_end(&o);
}

/* ------------------------------------------------------------------------
 * 00123020 (word asm; from the instructions): the EE library string
 * compare. When (a | b) & 7 is 0 it compares 16 bytes at a time (both 16
 * aligned) or 8 at a time, stopping with 0 at a chunk that is equal and
 * holds a zero byte, and falls back to the byte loop at the first chunk
 * that differs; the byte loop returns *a - *b (unsigned bytes) at the
 * first difference or at a's terminator. The loads (and their widths)
 * are the original's, in its order: the 8-byte load of b before either
 * wide loop, each chunk of a read again before its zero test.
 * ---------------------------------------------------------------------- */
static int32_t a6_strcmp_bytes(A6 *o, uint32_t a, uint32_t b)
{
    /* entered with the signed byte *a already loaded (the callers' delay
     * slots); the loop re-reads it unsigned. */
    for (;;) {
        int32_t v0 = a6_s8(o, a);
        uint32_t v1 = a6_u8(o, a);
        if (v0 == 0) return (int32_t)(v1 - a6_u8(o, b));
        int32_t c = a6_s8(o, b);
        if ((int32_t)(int8_t)v1 != c) {
            uint32_t x = a6_u8(o, a);
            return (int32_t)(x - a6_u8(o, b));
        }
        a++;
        b++;
    }
}

static int a6_has_zero8(uint64_t v)
{
    return ((v - 0x0101010101010101ull) & ~v & 0x8080808080808080ull) != 0;
}

static int a6_has_zero16(const uint8_t q[16])
{
    for (unsigned k = 0; k < 16; k++)
        if (q[k] == 0) return 1;
    return 0;
}

/* The 8-byte loop: vb is b's first doubleword (loaded by the caller). */
static int32_t a6_strcmp8(A6 *o, uint32_t a, uint32_t b, uint64_t vb)
{
    if (a6_u64(o, a) != vb) return a6_strcmp_bytes(o, a, b);
    uint64_t v = a6_u64(o, a);
    for (;;) {
        if (a6_has_zero8(v)) return 0;
        a += 8;
        b += 8;
        v = a6_u64(o, a);
        uint64_t w = a6_u64(o, b);
        if (w != v) return a6_strcmp_bytes(o, a, b);
    }
}

static int32_t a6_strcmp16(A6 *o, uint32_t a, uint32_t b)
{
    uint8_t qa[16], qb[16];
    a6_q(o, a, qa);
    a6_q(o, b, qb);
    if (memcmp(qa, qb, 16) != 0) return a6_strcmp_bytes(o, a, b);
    a6_q(o, a, qa);
    for (;;) {
        if (a6_has_zero16(qa)) return 0;
        a += 16;
        b += 16;
        a6_q(o, a, qa);
        a6_q(o, b, qb);
        if (memcmp(qa, qb, 16) != 0) return a6_strcmp_bytes(o, a, b);
    }
}

int32_t a6_00123020(A6 *o, uint32_t a, uint32_t b)
{
    uint32_t t = a | b;
    if (t & 7) return a6_strcmp_bytes(o, a, b);
    uint64_t vb = a6_u64(o, b);
    if (t & 0xF) return a6_strcmp8(o, a, b, vb);
    return a6_strcmp16(o, a, b);
}

int em_area06_port_00123020(const EmArea06PortHooks *h, uint32_t a, uint32_t b, int32_t *result,
                            EmArea06PortFault *fault)
{
    A6 o;
    if (!result || a6_begin(&o, h, fault)) return -1;
    int32_t r = a6_00123020(&o, a, b);
    if (a6_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 00207350 (C byte-identical): the keypad page (the record 002072C0
 * allocates; info = page +0x20, the typed buffer at page +0x60, the texture
 * block D_00275860[page +0xB], the compared string D_00275858[page +0xB]).
 * By page +4:
 *  0  cursor +0x28 = 9, timer +0x2A = 0, count +0xA = 0, slot +0xB = info
 *     +0x10 - 4, the 16 buffer bytes cleared, +4 + 1, +5 = +6 = 0.
 *  1  002079F0(page, tex), then by +5 (the original's jump table):
 *     0  by +6: 0 the dialog words D_002821B0 = 3, D_002821B4 = 1,
 *        D_00282228 = D_002821B8 = D_002821C0 = 0, +6 + 1; 1 +6 + 1 once
 *        D_002821C0 is set; 2 the pad (D_00810E74, read three times):
 *        0x1000 up a row (cursor >= 3: - 3), 0x4000 down (cursor < 9: + 3,
 *        at most 10), 0x2000 right (not from 2, 5, 8, 10), 0x8000 left
 *        (not from 0, 3, 6, 9), each with the move sound 002072A0; then
 *        0x810 cancels (0020CD60, +4 + 1), 0x40 on cell 10 ends the entry
 *        (+5 + 1, +6 = 0, buffer[count] = 0, sound 0x8C8) or types the
 *        cell's digit (D_00265010[cursor] + '0', count < 8, sound 0x8C6),
 *        0x20 deletes (count > 0, sound 0x8C7); then 00207BB0.
 *     1  00123020(buffer, string) == 0: slot 0 sets +5 = 3 and D_00810845
 *        |= 0x20 (the lock bit of AREA04's door [45]), other slots +5 = 6;
 *        +0x2A = 60, D_002821B8 = 4, D_002821C0 = 0. Otherwise
 *        D_002821B8 = 2, D_002821C0 = 0, sound 0x8CB, +5 + 1. Then
 *        00207BB0.
 *     2  D_002821C0 set: +0x2A = 80, +5 = 7.  3  set: +5 = 8, +0x2A = 90.
 *     4  --+0x2A == 0: sound 0x8CA, +5 + 1, +0x2A = 240; 00207CD0 unless
 *        0x70003B64 & 0x20, then 00207CA0.
 *     5  --+0x2A == 0: +4 + 1; 00207CD0, 00207CA0.
 *     6  D_002821C0 set: +4 + 1.  7  --+0x2A == 0: +4 = 0.
 *     8  --+0x2A == 0: +5 = 4, D_002821B8 = 6, D_002821C0 = 0, +0x2A =
 *        300.  Other values: nothing.
 *  2, 3  D_002821B4 = 2, 001AEDB0(0, state, info), D_008106C5 = 0xFF,
 *        001AFF90(page).
 * The frame is 0x30; 00207BB0 runs with sp - 0x30.
 * ---------------------------------------------------------------------- */
#define D_00275858 0x00275858u
#define D_00275860 0x00275860u
#define D_002821B0 0x002821B0u
#define D_002821B4 0x002821B4u
#define D_002821B8 0x002821B8u
#define D_002821C0 0x002821C0u
#define D_00282228 0x00282228u
#define D_008106C5 0x008106C5u
#define D_00810845 0x00810845u
#define D_00810E74 0x00810E74u
#define S_70003B64 0x70003B64u

static uint32_t a6_page_tex(A6 *o, uint32_t page)
{
    uint32_t slot = a6_u8(o, page + 0xB);
    return a6_u32(o, D_00275860 + (slot << 2));
}

static void a6_page_sound(A6 *o, int32_t id) { a6_c_001FB9F0(o, id, 0x1000, 0x1000, 0x1000); }

/* --page +0x2A; nonzero when it reached 0 (the halfword is stored first). */
static int a6_page_tick(A6 *o, uint32_t page)
{
    uint32_t v = (uint32_t)a6_s16(o, page + 0x2A) - 1u;
    a6_w16(o, page + 0x2A, v);
    return (int16_t)v == 0;
}

static void a6_page_pad(A6 *o, uint32_t page, uint32_t sp)
{
    uint32_t buf = page + 0x60;
    uint32_t pad = a6_u16(o, D_00810E74);
    if (pad & 0x1000) {
        int32_t n = a6_s16(o, page + 0x28);
        if (!(n < 3)) {
            a6_w16(o, page + 0x28, (uint32_t)(n - 3));
            a6_002072A0(o);
        }
    } else if (pad & 0x4000) {
        int32_t n = a6_s16(o, page + 0x28);
        if (n < 9) {
            a6_w16(o, page + 0x28, (uint32_t)(n + 3));
            if (!(a6_s16(o, page + 0x28) < 0xB)) a6_w16(o, page + 0x28, 0xA);
            a6_002072A0(o);
        }
    }
    if (a6_failed(o)) return;
    pad = a6_u16(o, D_00810E74);
    if (pad & 0x2000) {
        int32_t n = a6_s16(o, page + 0x28);
        if (n != 2 && n != 5 && n != 8 && n != 0xA) {
            a6_002072A0(o);
            if (a6_failed(o)) return;
            a6_w16(o, page + 0x28, (uint32_t)a6_s16(o, page + 0x28) + 1);
        }
    } else if (pad & 0x8000) {
        int32_t n = a6_s16(o, page + 0x28);
        if (n != 0 && n != 3 && n != 6 && n != 9) {
            a6_002072A0(o);
            if (a6_failed(o)) return;
            a6_w16(o, page + 0x28, (uint32_t)a6_s16(o, page + 0x28) - 1);
        }
    }
    pad = a6_u16(o, D_00810E74);
    if (pad & 0x810) {
        if (a6_c_0020CD60(o)) return;
        a6_w8(o, page + 4, a6_u8(o, page + 4) + 1);
    } else if (pad & 0x40) {
        int32_t n = a6_s16(o, page + 0x28);
        if (n == 0xA) {
            a6_w8(o, page + 5, a6_u8(o, page + 5) + 1);
            a6_w8(o, page + 6, 0);
            a6_w8(o, buf + a6_u8(o, page + 0xA), 0);
            a6_page_sound(o, 0x8C8);
        } else {
            uint32_t count = a6_u8(o, page + 0xA);
            if ((int32_t)count < 8) {
                int32_t c = (int16_t)a6_u8(o, D_00265010 + (uint32_t)n);
                a6_w8(o, buf + (count & 0xFF), (uint32_t)(c + 0x30));
                a6_w8(o, page + 0xA, a6_u8(o, page + 0xA) + 1);
                a6_page_sound(o, 0x8C6);
            }
        }
    } else if (pad & 0x20) {
        uint32_t count = a6_u8(o, page + 0xA);
        if (count != 0) {
            a6_w8(o, page + 0xA, count - 1);
            a6_page_sound(o, 0x8C7);
        }
    }
    if (a6_failed(o)) return;
    a6_00207BB0(o, page, a6_page_tex(o, page), sp);
}

static void a6_00207350(A6 *o, uint32_t page, uint32_t sp)
{
    uint32_t state = a6_u8(o, page + 4);
    uint32_t info = a6_u32(o, page + 0x20);
    uint32_t buf = page + 0x60, inner = sp - 0x30;
    if (state == 3 || state == 2) {
        a6_w32(o, D_002821B4, 2);
        if (a6_c_001AEDB0(o, 0, (int32_t)state, info)) return;
        a6_w8(o, D_008106C5, 0xFF);
        a6_c_001AFF90(o, page);
        return;
    }
    if (state == 0) {
        a6_w16(o, page + 0x28, 9);
        a6_w16(o, page + 0x2A, 0);
        a6_w8(o, page + 0xA, 0);
        a6_w8(o, page + 0xB, a6_u8(o, info + 0x10) - 4);
        for (uint32_t i = 0; i < 0x10; i++) a6_w8(o, buf + i, 0);
        a6_w8(o, page + 4, a6_u8(o, page + 4) + 1);
        a6_w8(o, page + 5, 0);
        a6_w8(o, page + 6, 0);
        return;
    }
    if (state != 1) return;
    a6_002079F0(o, page, a6_page_tex(o, page));
    if (a6_failed(o)) return;
    uint32_t sub = a6_u8(o, page + 5);
    switch (sub) {
    case 0: {
        uint32_t step = a6_u8(o, page + 6);
        if (step == 2) {
            a6_page_pad(o, page, inner);
        } else if (step == 1) {
            if (a6_u32(o, D_002821C0) != 0) a6_w8(o, page + 6, step + 1);
        } else if (step == 0) {
            a6_w32(o, D_002821B0, 3);
            a6_w32(o, D_002821B4, 1);
            a6_w32(o, D_00282228, 0);
            a6_w32(o, D_002821B8, 0);
            a6_w32(o, D_002821C0, 0);
            a6_w8(o, page + 6, a6_u8(o, page + 6) + 1);
        }
        break;
    }
    case 1: {
        uint32_t slot = a6_u8(o, page + 0xB);
        uint32_t str = a6_u32(o, D_00275858 + (slot << 2));
        int32_t diff = a6_00123020(o, buf, str);
        if (a6_failed(o)) return;
        if (diff == 0) {
            if (a6_u8(o, page + 0xB) == 0) {
                a6_w8(o, page + 5, 3);
                a6_w8(o, D_00810845, a6_u8(o, D_00810845) | 0x20);
            } else {
                a6_w8(o, page + 5, 6);
            }
            a6_w16(o, page + 0x2A, 0x3C);
            a6_w32(o, D_002821B8, 4);
            a6_w32(o, D_002821C0, 0);
        } else {
            a6_w32(o, D_002821B8, 2);
            a6_w32(o, D_002821C0, 0);
            a6_page_sound(o, 0x8CB);
            if (a6_failed(o)) return;
            a6_w8(o, page + 5, a6_u8(o, page + 5) + 1);
        }
        a6_00207BB0(o, page, a6_page_tex(o, page), inner);
        break;
    }
    case 2:
        if (a6_u32(o, D_002821C0) != 0) {
            a6_w16(o, page + 0x2A, 0x50);
            a6_w8(o, page + 5, 7);
        }
        break;
    case 3:
        if (a6_u32(o, D_002821C0) != 0) {
            a6_w8(o, page + 5, 8);
            a6_w16(o, page + 0x2A, 0x5A);
        }
        break;
    case 4:
        if (a6_page_tick(o, page)) {
            a6_page_sound(o, 0x8CA);
            if (a6_failed(o)) return;
            a6_w8(o, page + 5, a6_u8(o, page + 5) + 1);
            a6_w16(o, page + 0x2A, 0xF0);
        }
        if (!(a6_u32(o, S_70003B64) & 0x20)) {
            a6_00207CD0(o, a6_page_tex(o, page));
            if (a6_failed(o)) return;
        }
        a6_00207CA0(o, a6_page_tex(o, page));
        break;
    case 5:
        if (a6_page_tick(o, page)) a6_w8(o, page + 4, a6_u8(o, page + 4) + 1);
        a6_00207CD0(o, a6_page_tex(o, page));
        if (a6_failed(o)) return;
        a6_00207CA0(o, a6_page_tex(o, page));
        break;
    case 6:
        if (a6_u32(o, D_002821C0) != 0) a6_w8(o, page + 4, a6_u8(o, page + 4) + 1);
        break;
    case 7:
        if (a6_page_tick(o, page)) a6_w8(o, page + 4, 0);
        break;
    case 8:
        if (a6_page_tick(o, page)) {
            a6_w8(o, page + 5, 4);
            a6_w32(o, D_002821B8, 6);
            a6_w32(o, D_002821C0, 0);
            a6_w16(o, page + 0x2A, 0x12C);
        }
        break;
    default:
        break;
    }
}

int em_area06_port_00207350(const EmArea06PortHooks *h, uint32_t page, uint32_t sp, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_00207350(&o, page, sp);
    return a6_end(&o);
}
