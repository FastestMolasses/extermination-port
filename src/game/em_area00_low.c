/* em_area00_low.c - AREA00 lane A00LOW translations (see em_area00_low.h
 * and docs/AREA00_LOW.md). Every routine names its original address; the
 * comments say what the original does, never how it is encoded. Stores are
 * made in the original's order, and a field the original loads again after
 * a call or a store is loaded again here. */
#include "em_area00_low.h"

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#include "em_ee_float.h"

/* ------------------------------------------------------------------------
 * Fail-stop plumbing
 * ---------------------------------------------------------------------- */

typedef struct {
    EmArea00Low *s;
    jmp_buf out;
} Run;

static void fault(Run *r, int32_t code, uint32_t address)
{
    if (r->s->fault == EM_AREA00_LOW_FAULT_NONE) {
        r->s->fault = code;
        r->s->fault_address = address;
    }
    longjmp(r->out, 1);
}

void em_area00_low_clear_fault(EmArea00Low *s)
{
    if (!s)
        return;
    s->fault = EM_AREA00_LOW_FAULT_NONE;
    s->fault_function = 0;
    s->fault_address = 0;
}

/* The byte span [address, address + size) inside one region, else the
 * optional view (for a store when `write`), or a fault. */
static uint8_t *span(Run *r, uint32_t address, uint32_t size, int write)
{
    const EmArea00Low *s = r->s;
    unsigned i;
    for (i = 0; i < s->region_count; i++) {
        const EmArea00LowRegion *g = &s->regions[i];
        if (g->bytes && address >= g->base && size <= g->size && address - g->base <= g->size - size)
            return g->bytes + (address - g->base);
    }
    if (s->view) {
        uint8_t *p = s->view(s->view_ctx, address, size, write);
        if (p)
            return p;
    }
    fault(r, EM_AREA00_LOW_FAULT_UNMAPPED, address);
    return NULL;
}

static uint32_t rd(Run *r, uint32_t a, unsigned n)
{
    const uint8_t *p = span(r, a, n, 0);
    uint32_t v = 0;
    unsigned i;
    for (i = 0; i < n; i++)
        v |= (uint32_t)p[i] << (8 * i);
    return v;
}

/* Test hook. A build that defines EM_AREA00_LOW_STORE_TRACE as a function
 * name reports every store (original address, size) to it once the address
 * is known to be mapped; tools/test_area00_low_reference.py uses it to
 * compare memory with the original at every call leaving the module.
 * Ordinary builds compile it out. wr is the only store path. */
#ifdef EM_AREA00_LOW_STORE_TRACE
void EM_AREA00_LOW_STORE_TRACE(uint32_t address, unsigned size);
#define TRACE_STORE(a, n) EM_AREA00_LOW_STORE_TRACE((a), (n))
#else
#define TRACE_STORE(a, n) ((void)0)
#endif

static void wr(Run *r, uint32_t a, uint32_t v, unsigned n)
{
    uint8_t *p = span(r, a, n, 1);
    unsigned i;
    TRACE_STORE(a, n);
    for (i = 0; i < n; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t u8(Run *r, uint32_t a) { return rd(r, a, 1); }
static int32_t s8(Run *r, uint32_t a) { return (int32_t)(int8_t)rd(r, a, 1); }
static uint32_t u16(Run *r, uint32_t a) { return rd(r, a, 2); }
static int32_t s16(Run *r, uint32_t a) { return (int32_t)(int16_t)rd(r, a, 2); }
static uint32_t w32(Run *r, uint32_t a) { return rd(r, a, 4); }
static void st8(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 1); }
static void st16(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 2); }
static void st32(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 4); }

/* D_0081083C (the player's grab-slot bits, a canonical progress byte of
 * the scene state) is reached only through the binder's view `grab_bits`
 * (SCENE_COORDINATOR_DESIGN.md 3.2), never through the regions. A NULL
 * view that a routine reaches is a NULL fault with address 0. */
static uint32_t grab_get(Run *r)
{
    if (!r->s->grab_bits)
        fault(r, EM_AREA00_LOW_FAULT_NULL, 0);
    return *r->s->grab_bits;
}

static void grab_set(Run *r, uint32_t v)
{
    if (!r->s->grab_bits)
        fault(r, EM_AREA00_LOW_FAULT_NULL, 0);
    *r->s->grab_bits = (uint8_t)v;
}

/* A 32-bit value as the EE holds it in a 64-bit register. */
static uint64_t reg(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

/* ------------------------------------------------------------------------
 * Calls leaving the module
 * ---------------------------------------------------------------------- */

/* One call: `na` integer registers a0.. (register images) and `nf` float
 * registers f12.. (raw bits), as the original sets them. */
static EmArea00LowCall callx(Run *r, uint32_t sp, uint32_t fn, unsigned na, const uint64_t *a, unsigned nf,
                             const uint32_t *f)
{
    EmArea00LowCall c;
    unsigned i;
    memset(&c, 0, sizeof c);
    c.fn = fn;
    c.sp = sp;
    c.na = na;
    c.nf = nf;
    for (i = 0; i < na && i < 8; i++)
        c.a[i] = a[i];
    for (i = 0; i < nf && i < 8; i++)
        c.f[i] = f[i];
    if (!r->s->call)
        fault(r, EM_AREA00_LOW_FAULT_NULL, fn);
    if (r->s->call(r->s->ctx, &c) < 0)
        fault(r, EM_AREA00_LOW_FAULT_WORKER, fn);
    return c;
}

/* a0..a3 register images and f12..f14 bits. */
static EmArea00LowCall callg(Run *r, uint32_t sp, uint32_t fn, unsigned na, uint64_t a0, uint64_t a1, uint64_t a2,
                             uint64_t a3, unsigned nf, uint32_t f12, uint32_t f13, uint32_t f14)
{
    uint64_t a[4];
    uint32_t f[3];
    a[0] = a0;
    a[1] = a1;
    a[2] = a2;
    a[3] = a3;
    f[0] = f12;
    f[1] = f13;
    f[2] = f14;
    return callx(r, sp, fn, na, a, nf, f);
}

#define CALL0(fn) callg(r, sp, (fn), 0, 0, 0, 0, 0, 0, 0, 0, 0).v0
#define CALL1(fn, x) callg(r, sp, (fn), 1, reg(x), 0, 0, 0, 0, 0, 0, 0).v0
#define CALL2(fn, x, y) callg(r, sp, (fn), 2, reg(x), reg(y), 0, 0, 0, 0, 0, 0).v0
#define CALL3(fn, x, y, z) callg(r, sp, (fn), 3, reg(x), reg(y), reg(z), 0, 0, 0, 0, 0).v0
#define CALL4(fn, x, y, z, w) callg(r, sp, (fn), 4, reg(x), reg(y), reg(z), reg(w), 0, 0, 0, 0).v0
/* Float-only calls: f12.. -> f0. */
#define FCALL1(fn, x) callg(r, sp, (fn), 0, 0, 0, 0, 0, 1, (x), 0, 0).f0
#define FCALL2(fn, x, y) callg(r, sp, (fn), 0, 0, 0, 0, 0, 2, (x), (y), 0).f0
#define FCALL3(fn, x, y, z) callg(r, sp, (fn), 0, 0, 0, 0, 0, 3, (x), (y), (z)).f0

/* ------------------------------------------------------------------------
 * Constants and EE float shorthands (raw bits)
 * ---------------------------------------------------------------------- */

#define F_ZERO   0x00000000u
#define F_ONE    0x3F800000u
#define F_TWO    0x40000000u
#define F_PI     0x40490FDBu /* 3.1415927 */
#define F_HALFPI 0x3FC90FDBu /* 1.5707964 */
#define F_300    0x43960000u
#define F_TURN   0x3D8EFA35u /* 0.06981317: 4 degrees */

#define FN_MAT_VEC   0x001026A0u /* 3 pointers */
#define FN_1026D0    0x001026D0u /* 3 pointers */
#define FN_102760    0x00102760u /* 2 pointers */
#define FN_VADD      0x001028B8u /* 3 pointers */
#define FN_COPY_QW   0x00102948u /* 2 pointers */
#define FN_COPY_QW4  0x00102958u /* 2 pointers */
#define FN_IDENT     0x001029C0u /* 1 pointer */
#define FN_102A60    0x00102A60u /* 2 pointers, f12 */
#define FN_102B08    0x00102B08u /* 2 pointers, f12 */
#define FN_102BB0    0x00102BB0u /* 2 pointers, f12 */
#define FN_102C58    0x00102C58u /* 3 pointers */
#define FN_1031E0    0x001031E0u /* 2 pointers */
#define FN_VSCALE    0x00103230u /* 2 pointers, f12 */
#define FN_11DE90    0x0011DE90u /* f12 -> f0 */
#define FN_11DF78    0x0011DF78u /* f12 -> f0 */
#define FN_11E2A8    0x0011E2A8u /* f12 -> f0 */
#define FN_RAND      0x00122BB8u /* -> v0 */
#define FN_128390    0x00128390u
#define FN_128600    0x00128600u
#define FN_128640    0x00128640u
#define FN_F2I       0x001281C0u /* f12 -> v0 */
#define FN_1287F0    0x001287F0u /* a0, a1, a2, f12 */
#define FN_12ADC0    0x0012ADC0u
#define FN_18C4B0    0x0018C4B0u /* a0, f12, f13 */
#define FN_18C6A0    0x0018C6A0u /* a0, a1, f12 */
#define FN_1B1240    0x001B1240u /* a0, f12, f13 -> f0 */
#define FN_1B12B0    0x001B12B0u /* f12, f13, f14 -> f0 */
#define FN_1B13F0    0x001B13F0u /* a0, a1, f12 */
#define FN_1B1470    0x001B1470u /* f12 -> f0 */
#define FN_1B17A0    0x001B17A0u
#define FN_1C24D0    0x001C24D0u
#define FN_1C2540    0x001C2540u
#define FN_1C25E0    0x001C25E0u
#define FN_1C2770    0x001C2770u
#define FN_1C3D60    0x001C3D60u
#define FN_1C3DB0    0x001C3DB0u
#define FN_1C64F0    0x001C64F0u /* a0, f12 -> v0 */
#define FN_1C69A0    0x001C69A0u
#define FN_1EFD90    0x001EFD90u
#define FN_1EFE00    0x001EFE00u
#define FN_1EFFD0    0x001EFFD0u /* a0..a3, f12 */
#define FN_1FBD50    0x001FBD50u /* a0, a1, a2, f12 */
#define FN_1FC580    0x001FC580u

#define PLAYER 0x008102B0u /* D_008102B0 */
#define SPR    0x70000000u

static uint32_t fadd(uint32_t a, uint32_t b) { return em_ee_add_bits(a, b); }
static uint32_t fsub(uint32_t a, uint32_t b) { return em_ee_sub_bits(a, b); }
static uint32_t fmul(uint32_t a, uint32_t b) { return em_ee_mul_bits(a, b); }
static uint32_t fdiv(uint32_t a, uint32_t b) { return em_ee_div_bits(a, b); }
static int flt(uint32_t a, uint32_t b) { return em_ee_c_lt_bits(a, b); }
static int fle(uint32_t a, uint32_t b) { return em_ee_c_le_bits(a, b); }
static int feq(uint32_t a, uint32_t b) { return em_ee_c_eq_bits(a, b); }

static void vec4(Run *r, uint32_t a, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    st32(r, a, x);
    st32(r, a + 4, y);
    st32(r, a + 8, z);
    st32(r, a + 12, w);
}

/* 001287F0(a, b, k; f) */
static void anim(Run *r, uint32_t sp, uint32_t a, uint32_t b, uint32_t k, uint32_t f)
{
    callg(r, sp, FN_1287F0, 3, reg(a), reg(b), reg(k), 0, 1, f, 0, 0);
}

/* 001B12B0(goal, cur, rate) -> f0 */
static uint32_t turn(Run *r, uint32_t sp, uint32_t goal, uint32_t cur, uint32_t rate)
{
    return FCALL3(FN_1B12B0, goal, cur, rate);
}

/* 001FBD50(a0, id, 0; 300.0) */
static void sound_1FBD50(Run *r, uint32_t sp, uint32_t a0, uint32_t id)
{
    callg(r, sp, FN_1FBD50, 3, reg(a0), reg(id), 0, 0, 1, F_300, 0, 0);
}

/* The matrix at `m`: identity, then turned by 00102B08 / 00102BB0 /
 * 00102A60 with the three angles. */
static void rot_matrix(Run *r, uint32_t sp, uint32_t m, uint32_t ax, uint32_t ay, uint32_t az)
{
    CALL1(FN_IDENT, m);
    callg(r, sp, FN_102B08, 2, reg(m), reg(m), 0, 0, 1, ax, 0, 0);
    callg(r, sp, FN_102BB0, 2, reg(m), reg(m), 0, 0, 1, ay, 0, 0);
    callg(r, sp, FN_102A60, 2, reg(m), reg(m), 0, 0, 1, az, 0, 0);
}

/* The word at 0x008102B0 + 0x110 + (word D_00242DD0[k & 7] << 2), + 0x90:
 * the matrix the 0012CAA0 / 0012D240 attachment follows. */
static uint32_t attach_matrix(Run *r, uint32_t k)
{
    uint32_t sel = w32(r, 0x00242DD0u + ((k & 7u) << 2));
    return w32(r, (sel << 2) + PLAYER + 0x110u) + 0x90u;
}

/* ------------------------------------------------------------------------
 * 001000C0(a0..a3) -> v0. Frame 0x10.
 * Calls 001274B0 with a0..a3 as it received them and returns 1 when the
 * callee's whole 64-bit v0 is negative, else 0.
 * ---------------------------------------------------------------------- */
static int32_t f_1000C0(Run *r, uint32_t sp0, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
    uint32_t sp = sp0 - 0x10u;
    return (int64_t)callg(r, sp, 0x001274B0u, 4, a0, a1, a2, a3, 0, 0, 0, 0).v0 < 0;
}

/* ------------------------------------------------------------------------
 * 00102870(a0, a1; f12). Leaf, VU0 macro mode.
 * Loads the quadword at a1 (low four address bits ignored), Q = 1.0 /
 * f12 (the VU0 divide of vf0.w by the f12 bits), multiplies x, y and z by
 * Q (w keeps a1's w) and stores the quadword at a0 (low bits ignored).
 * ---------------------------------------------------------------------- */
static void f_102870(Run *r, uint32_t a0, uint32_t a1, uint32_t f12)
{
    uint32_t v[4], q = 0;
    unsigned k;
    for (k = 0; k < 4; k++)
        v[k] = w32(r, (a1 & ~0xFu) + 4u * k);
    em_vu_div_bits(F_ONE, f12, 3, 0, &q);
    em_vu_vec_bits(EM_VU_MULQ, 14, EM_VU_NO_BC, v, NULL, q, NULL, v);
    for (k = 0; k < 4; k++)
        st32(r, (a0 & ~0xFu) + 4u * k, v[k]);
}

/* ------------------------------------------------------------------------
 * 00102990(a0, a1). Leaf, VU0 macro mode.
 * The quadword at a1 converted lane by lane to integers (VFTOI0) and
 * stored at a0 (low four address bits ignored on both).
 * ---------------------------------------------------------------------- */
static void f_102990(Run *r, uint32_t a0, uint32_t a1)
{
    uint32_t v[4];
    unsigned k;
    for (k = 0; k < 4; k++)
        v[k] = em_vu_ftoi0_bits(w32(r, (a1 & ~0xFu) + 4u * k));
    for (k = 0; k < 4; k++)
        st32(r, (a0 & ~0xFu) + 4u * k, v[k]);
}

/* ------------------------------------------------------------------------
 * 001181B0(ev). Frame 0x90 (ee-gcc). V is the table of 48 records of 0x6A
 * bytes at 0x0027CCC0; G = 0x00281AC0; T = 0x0027F740.
 * Halfword ev +0x34 == 1:
 *   base = word ev +8 (read once). For each record v whose halfword +0 is
 *   1, +0x1A is 2, +0x22 equals halfword ev +0x24, and with d = base +
 *   word G +0x14 (read per record): halfword v +0x3E == byte d[4], v +2 ==
 *   byte d[5], and the zero-extended halfword v +6 equals the word ev +0x18
 *   (all 64 bits, so a word with bit 31 set never matches):
 *     v +0x46 = 1; t = halfword T +0x3A; v +0x50 = byte d[3];
 *     v +0x52 = halfword v +0x2C; t2 = halfword T +0x3A;
 *     v +0x54 = (byte d[2] * 4 * t) / 60; v +0x56 = (byte d[2] (read
 *     again) * 4 * t2) / 60 (32-bit products, signed quotients).
 *   Then ev +8 = base + 6.
 * Otherwise:
 *   d = word ev +8 + word G +0x14; byte (word G +0xC)[3] = byte d[2].
 *   For each record i whose halfword +0 is 1, +0x1A is 1, +4 equals byte
 *   ev +0 & 15, +0x22 equals halfword ev +0x24, +8 is not 1, and the
 *   zero-extended +6 equals the word ev +0x18:
 *     v +0x34 = byte (word G +0xC)[3]; k = 001179E0(i, ev);
 *     001157F0(1, i, k >> 16 (arithmetic, of k's low word), k & 0xFFFF).
 *   Then ev +8 = word ev +8 (read again) + 3.
 * ---------------------------------------------------------------------- */
static void f_1181B0(Run *r, uint32_t sp0, uint32_t ev)
{
    uint32_t sp = sp0 - 0x90u;
    uint32_t v, d, t, t2, i;
    if (u16(r, ev + 0x34u) == 1u) {
        uint32_t base = w32(r, ev + 8u);
        for (v = 0x0027CCC0u; (int32_t)v < (int32_t)(0x0027CCC0u + 0x13E0u); v += 0x6Au) {
            if (u16(r, v) != 1u)
                continue;
            if (u16(r, v + 0x1Au) != 2u)
                continue;
            if (u16(r, v + 0x22u) != u16(r, ev + 0x24u))
                continue;
            d = base + w32(r, 0x00281AC0u + 0x14u);
            if (u16(r, v + 0x3Eu) != u8(r, d + 4u))
                continue;
            if (u16(r, v + 2u) != u8(r, d + 5u))
                continue;
            if ((uint64_t)u16(r, v + 6u) != reg(w32(r, ev + 0x18u)))
                continue;
            st16(r, v + 0x46u, 1);
            t = u16(r, 0x0027F740u + 0x3Au);
            st16(r, v + 0x50u, u8(r, d + 3u));
            st16(r, v + 0x52u, u16(r, v + 0x2Cu));
            t2 = u16(r, 0x0027F740u + 0x3Au);
            st16(r, v + 0x54u, (uint32_t)((int32_t)((u8(r, d + 2u) << 2) * t) / 60));
            st16(r, v + 0x56u, (uint32_t)((int32_t)((u8(r, d + 2u) << 2) * t2) / 60));
        }
        st32(r, ev + 8u, base + 6u);
        return;
    }
    d = w32(r, ev + 8u) + w32(r, 0x00281AC0u + 0x14u);
    st8(r, w32(r, 0x00281AC0u + 0xCu) + 3u, u8(r, d + 2u));
    for (i = 0; i < 48u; i++) {
        uint32_t k;
        v = 0x0027CCC0u + i * 0x6Au;
        if (u16(r, v) != 1u)
            continue;
        if (u16(r, v + 0x1Au) != 1u)
            continue;
        if (u16(r, v + 4u) != (u8(r, ev) & 0xFu))
            continue;
        if (u16(r, v + 0x22u) != u16(r, ev + 0x24u))
            continue;
        if (u16(r, v + 8u) == 1u)
            continue;
        if ((uint64_t)u16(r, v + 6u) != reg(w32(r, ev + 0x18u)))
            continue;
        st16(r, v + 0x34u, u8(r, w32(r, 0x00281AC0u + 0xCu) + 3u));
        k = (uint32_t)CALL2(0x001179E0u, i, ev);
        CALL4(0x001157F0u, 1u, i, (uint32_t)((int32_t)k >> 16), k & 0xFFFFu);
    }
    st32(r, ev + 8u, w32(r, ev + 8u) + 3u);
}

/* ------------------------------------------------------------------------
 * 00119080(a, b, c, d) -> v0. Leaf (ee-gcc). With the low bytes of the
 * four registers: (a + ((b - a) * d) / c) & 0xFF, a 32-bit product and a
 * signed 32-bit quotient. c == 0 takes the divide-by-zero trap: fault.
 * ---------------------------------------------------------------------- */
static int32_t f_119080(Run *r, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    int32_t n;
    a &= 0xFFu;
    c &= 0xFFu;
    n = (int32_t)(((b & 0xFFu) - a) * (d & 0xFFu));
    if (c == 0)
        fault(r, EM_AREA00_LOW_FAULT_TRAP, 0x00119080u);
    return (int32_t)((a + (uint32_t)(n / (int32_t)c)) & 0xFFu);
}

/* ------------------------------------------------------------------------
 * 00128830(a0; f12, f13, f14). Frame 0x20.
 * Scratch 0x700038A0 = (f12, f13, f14, 1.0); 001026A0(0x700038A0, a0 +
 * 0xD0, 0x700038A0); then a0 +0xB0, +0xB4, +0xB8 each += the scratch x, y,
 * z (field first).
 * ---------------------------------------------------------------------- */
static void f_128830(Run *r, uint32_t sp0, uint32_t a0, uint32_t x, uint32_t y, uint32_t z)
{
    uint32_t sp = sp0 - 0x20u;
    vec4(r, 0x700038A0u, x, y, z, F_ONE);
    CALL3(FN_MAT_VEC, 0x700038A0u, a0 + 0xD0u, 0x700038A0u);
    st32(r, a0 + 0xB0u, fadd(w32(r, a0 + 0xB0u), w32(r, 0x700038A0u)));
    st32(r, a0 + 0xB4u, fadd(w32(r, a0 + 0xB4u), w32(r, 0x700038A4u)));
    st32(r, a0 + 0xB8u, fadd(w32(r, a0 + 0xB8u), w32(r, 0x700038A8u)));
}

/* ------------------------------------------------------------------------
 * 001288D0(a0, a1). Frame 0x20. a0 is not read.
 * Scratch 0x700038A0 = (0, -1.5, 0, 1.0); 001026A0(0x700038A0, word
 * (word D_00275B40) +0x14, + 0x90, 0x700038A0). Scratch 0x700038B0 =
 * (0x20, 0x70, 0x80, 0x80) when byte a1 +0xE1 is 0, else (0x80, 0x50,
 * 0x30, 0x80) (integers); then 001F4A00(0x700038A0, 0x700038B0).
 * ---------------------------------------------------------------------- */
static void f_1288D0(Run *r, uint32_t sp0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x20u;
    vec4(r, 0x700038A0u, F_ZERO, 0xBFC00000u, F_ZERO, F_ONE);
    CALL3(FN_MAT_VEC, 0x700038A0u, w32(r, w32(r, 0x00275B40u) + 0x14u) + 0x90u, 0x700038A0u);
    if (u8(r, a1 + 0xE1u) == 0)
        vec4(r, 0x700038B0u, 0x20u, 0x70u, 0x80u, 0x80u);
    else
        vec4(r, 0x700038B0u, 0x80u, 0x50u, 0x30u, 0x80u);
    CALL2(0x001F4A00u, 0x700038A0u, 0x700038B0u);
}

/* ------------------------------------------------------------------------
 * 00129F00(a0, a1). Frame 0x20.
 * a1 +0xF0 = a1 +0xF0 + 0.06, then 4.0 when that sum is not <= 4.0;
 * a0 +0xB4 -= a1 +0xF0 (read again); scratch 0x700038A0 = (0, -5.0, 0,
 * 1.0); 0019AB20(a0, a0 + 0xB0, 0x700038A0, 0x80000007) non-zero (all 64
 * bits): a1 +0xF0 = 0.06.
 * ---------------------------------------------------------------------- */
static void f_129F00(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x20u;
    uint32_t t = fadd(w32(r, a1 + 0xF0u), 0x3D75C28Fu);
    st32(r, a1 + 0xF0u, t);
    if (!fle(t, 0x40800000u))
        st32(r, a1 + 0xF0u, 0x40800000u);
    st32(r, a0 + 0xB4u, fsub(w32(r, a0 + 0xB4u), w32(r, a1 + 0xF0u)));
    vec4(r, 0x700038A0u, F_ZERO, 0xC0A00000u, F_ZERO, F_ONE);
    if (CALL4(0x0019AB20u, a0, a0 + 0xB0u, 0x700038A0u, 0x80000007u) != 0)
        st32(r, a1 + 0xF0u, 0x3D75C28Fu);
}

/* ------------------------------------------------------------------------
 * 0012E260(a0). Frame 0x20.
 * a0 +0xC4 = 001B12B0(001B1240(a0 + 0xB0; D_00810350, D_00810358),
 * a0 +0xC4, pi/72).
 * ---------------------------------------------------------------------- */
static void f_12E260(Run *r, uint32_t sp0, uint32_t a0)
{
    uint32_t sp = sp0 - 0x20u;
    uint32_t goal = callg(r, sp, FN_1B1240, 1, reg(a0 + 0xB0u), 0, 0, 0, 2, w32(r, 0x00810350u),
                          w32(r, 0x00810358u), 0).f0;
    st32(r, a0 + 0xC4u, turn(r, sp, goal, w32(r, a0 + 0xC4u), 0x3D32B8C3u));
}

/* ------------------------------------------------------------------------
 * 0012E2C0(a0, a1) -> v0. Frame 0x30. By byte a0 +7:
 *  0  a1 +0xE8 = 001B1240(a0 + 0xB0; D_00810350, D_00810358); +7 += 1;
 *     halfword a1 +0xD2 = 0x78; result 0.
 *  1  a0 +0xC4 = 001B12B0(a1 +0xE8, a0 +0xC4, 4 degrees); halfword
 *     a1 +0xD2 -= 1: result 1 when it reached 0, else 1 when a0 +0xC4 ==
 *     a1 +0xE8 (both read again), else 0.
 *  other: 0.
 * ---------------------------------------------------------------------- */
static int32_t f_12E2C0(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x30u;
    int16_t t;
    switch (u8(r, a0 + 7u)) {
    case 0:
        st32(r, a1 + 0xE8u, callg(r, sp, FN_1B1240, 1, reg(a0 + 0xB0u), 0, 0, 0, 2, w32(r, 0x00810350u),
                                  w32(r, 0x00810358u), 0).f0);
        st8(r, a0 + 7u, u8(r, a0 + 7u) + 1u);
        st16(r, a1 + 0xD2u, 0x78u);
        break;
    case 1:
        st32(r, a0 + 0xC4u, turn(r, sp, w32(r, a1 + 0xE8u), w32(r, a0 + 0xC4u), F_TURN));
        t = (int16_t)(s16(r, a1 + 0xD2u) - 1);
        st16(r, a1 + 0xD2u, (uint16_t)t);
        if (t == 0)
            return 1;
        if (feq(w32(r, a0 + 0xC4u), w32(r, a1 + 0xE8u)))
            return 1;
        break;
    default:
        break;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * 0012E070(a0). Leaf (C linked from asm; the instructions decide).
 * When halfword a0 +0xF6 has bit 7: D_0081083C &= ~(1 << (+0xF6 & 7)).
 * Either way halfword +0xF6 = 0 (the store sits in the return's delay slot,
 * so it runs on both paths; the decomp C stores only on the first).
 * ---------------------------------------------------------------------- */
static void f_12E070(Run *r, uint32_t a0)
{
    uint32_t k = (uint32_t)s16(r, a0 + 0xF6u);
    if (k & 0x80u)
        grab_set(r, grab_get(r) & ~(1u << (k & 7u)));
    st16(r, a0 + 0xF6u, 0);
}

/* ------------------------------------------------------------------------
 * 0012E0B0(a0, a1) -> v0. Frame 0x40.
 * 0 when the byte 0x70003B8D is non-zero, 0021BD60(0x008102B0) is
 * non-zero (all 64 bits), D_008106BC is non-zero or byte a0 +0x0B is 0.
 * Otherwise d = 0011DF78(001B1470(player +0xC4 - a0 +0xC4)); not d <=
 * pi/2: bits 0..3 of D_0081083C, else bits 4..7; the first clear bit i
 * (the byte read per bit) is set, halfword a1 +0xF6 = i + 0x80, byte
 * a0 +0 = 2, player byte +0 |= 2, result 1. None clear: 0.
 * ---------------------------------------------------------------------- */
static int32_t f_12E0B0(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x40u;
    uint32_t d, i, lo;
    if (u8(r, 0x70003B8Du) != 0)
        return 0;
    if (CALL1(0x0021BD60u, PLAYER) != 0)
        return 0;
    if (u8(r, 0x008106BCu) != 0)
        return 0;
    if (u8(r, a0 + 0xBu) == 0)
        return 0;
    d = FCALL1(FN_1B1470, fsub(w32(r, PLAYER + 0xC4u), w32(r, a0 + 0xC4u)));
    d = FCALL1(FN_11DF78, d);
    lo = fle(d, F_HALFPI) ? 4u : 0u;
    for (i = lo; i < lo + 4u; i++) {
        uint32_t bit = 1u << i;
        if (!(grab_get(r) & bit)) {
            grab_set(r, grab_get(r) | bit);
            st16(r, a1 + 0xF6u, i + 0x80u);
            st8(r, a0, 2);
            st8(r, PLAYER, u8(r, PLAYER) | 2u);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B5360(p). Frame 0x20.
 * 00102948(0x700038A0, p + 0xB0); 00102948(0x700038B0, 0x700038A0);
 * scratch y (0x700038A4) += 10.0; 0x700038B4 = 0x700038A4 (read again) -
 * 200.0 when byte p +3 is 4, else - 30.0. 0019A570(0x700038A0,
 * 0x700038B0, 6, 0) zero (all 64 bits): done. Otherwise 00102948(
 * 0x700038A0, 0x700031B0); q = word 0x700031D0; 0x700038B0, B4, B8 = q
 * +0x24, +0x28, +0x2C (each load after the previous store); then by byte
 * p +3 (read again): 0 -> 001F9100 2.0, 3 -> 001F9100 3.5, 4 -> 001F9180
 * 2.0, 5 -> 001F9100 5.0, 6 -> 001F9100 6.5, 7 -> 001F9100 6.0, 8 ->
 * 001F9180 8.0, any other -> 001F9100 4.2, each (p + 0xB0, 0x700038A0,
 * 0x700038B0; weight).
 * ---------------------------------------------------------------------- */
static void f_1B5360(Run *r, uint32_t sp0, uint32_t p)
{
    uint32_t sp = sp0 - 0x20u;
    uint32_t q, fn, wt;
    CALL2(FN_COPY_QW, 0x700038A0u, p + 0xB0u);
    CALL2(FN_COPY_QW, 0x700038B0u, 0x700038A0u);
    st32(r, 0x700038A4u, fadd(w32(r, 0x700038A4u), 0x41200000u));
    if (u8(r, p + 3u) == 4u)
        st32(r, 0x700038B4u, fsub(w32(r, 0x700038A4u), 0x43480000u));
    else
        st32(r, 0x700038B4u, fsub(w32(r, 0x700038A4u), 0x41F00000u));
    if (CALL4(0x0019A570u, 0x700038A0u, 0x700038B0u, 6u, 0u) == 0)
        return;
    CALL2(FN_COPY_QW, 0x700038A0u, 0x700031B0u);
    q = w32(r, 0x700031D0u);
    st32(r, 0x700038B0u, w32(r, q + 0x24u));
    st32(r, 0x700038B4u, w32(r, q + 0x28u));
    st32(r, 0x700038B8u, w32(r, q + 0x2Cu));
    fn = 0x001F9100u;
    switch (u8(r, p + 3u)) {
    case 0: wt = F_TWO; break;
    case 3: wt = 0x40600000u; break;
    case 4: fn = 0x001F9180u; wt = F_TWO; break;
    case 5: wt = 0x40A00000u; break;
    case 6: wt = 0x40D00000u; break;
    case 7: wt = 0x40C00000u; break;
    case 8: fn = 0x001F9180u; wt = 0x41000000u; break;
    default: wt = 0x40866666u; break;
    }
    callg(r, sp, fn, 3, reg(p + 0xB0u), reg(0x700038A0u), reg(0x700038B0u), 0, 1, wt, 0, 0);
}

/* ------------------------------------------------------------------------
 * 001FC580(a0, a1). Frame 0x40; two stack words at sp + 0x38 / + 0x3C.
 * The table T by a1 (the whole register): 0x19D, 0x19E -> 0x00281F30;
 * 0x19F..0x1A1 -> 0x00281F40; 0x1AA, 0x1B5 -> 0x00281F50; 0x15A, 0x15B ->
 * 0x00281F60; any other: nothing. Then 001FBF50(a0, sp+0x38, sp+0x3C, 0;
 * 300.0, 4096.0); a zero v0 (all 64 bits): done. Otherwise, with x, y the
 * two stack words: T +0 zero: T = (4, a1, x, y). Else when T +8 + T +0xC
 * < x + y (32-bit sums, signed compare): T +4 = a1, T +8 = x, T +0xC = y.
 * ---------------------------------------------------------------------- */
static void f_1FC580(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x40u;
    uint32_t t;
    switch (a1) {
    case 0x15Bu: case 0x15Au: t = 0x00281F60u; break;
    case 0x1B5u: case 0x1AAu: t = 0x00281F50u; break;
    case 0x1A1u: case 0x1A0u: case 0x19Fu: t = 0x00281F40u; break;
    case 0x19Eu: case 0x19Du: t = 0x00281F30u; break;
    default: return;
    }
    if (callg(r, sp, 0x001FBF50u, 4, reg(a0), reg(sp + 0x38u), reg(sp + 0x3Cu), 0, 2, F_300, 0x45800000u, 0).v0 == 0)
        return;
    if (w32(r, t) != 0) {
        uint32_t x = w32(r, sp + 0x38u), y = w32(r, sp + 0x3Cu);
        uint32_t have = w32(r, t + 8u) + w32(r, t + 0xCu);
        if (!((int32_t)have < (int32_t)(x + y)))
            return;
        st32(r, t + 4u, a1);
        st32(r, t + 8u, w32(r, sp + 0x38u));
        st32(r, t + 0xCu, w32(r, sp + 0x3Cu));
    } else {
        st32(r, t, 4);
        st32(r, t + 4u, a1);
        st32(r, t + 8u, w32(r, sp + 0x38u));
        st32(r, t + 0xCu, w32(r, sp + 0x3Cu));
    }
}

/* ------------------------------------------------------------------------
 * 0012DE90(a0). Frame 0x20. B = word D_00275B40; M1 = word B +0xC, M2 =
 * word B +0x14 (every use loads them again, as the original does).
 * Scratch 0x700038A0.. = M1 +0xC0, +0xC4, +0xC8; 0x700038B0.. = M2 +0xC0,
 * +0xC4, +0xC8; 001029C0(0x700036A0). a0 +0xD4 = a0 +0xD4 + 0.02, then
 * 2.0 when the sum is not <= 2.0. The scratch matrix diagonal 0x700036A0,
 * B4, C8 = a0 +0xD4 (read each time). M1 +0xC0..C8 = 0, M2 +0xC0..C8 = 0;
 * 001026D0(M1 + 0x90, 0x700036A0, M1 + 0x90); the same for M2; then
 * M1 +0xC0.. and M2 +0xC0.. get the saved scratch words back.
 * ---------------------------------------------------------------------- */
static uint32_t m_of(Run *r, uint32_t off) { return w32(r, w32(r, 0x00275B40u) + off); }

static void f_12DE90(Run *r, uint32_t sp0, uint32_t a0)
{
    uint32_t sp = sp0 - 0x20u;
    uint32_t b = w32(r, 0x00275B40u), t;
    unsigned k;
    for (k = 0; k < 3; k++)
        st32(r, 0x700038A0u + 4u * k, w32(r, w32(r, b + 0xCu) + 0xC0u + 4u * k));
    for (k = 0; k < 3; k++)
        st32(r, 0x700038B0u + 4u * k, w32(r, w32(r, b + 0x14u) + 0xC0u + 4u * k));
    CALL1(FN_IDENT, 0x700036A0u);
    t = fadd(w32(r, a0 + 0xD4u), 0x3CA3D70Au);
    st32(r, a0 + 0xD4u, t);
    if (!fle(t, F_TWO))
        st32(r, a0 + 0xD4u, F_TWO);
    st32(r, 0x700036A0u, w32(r, a0 + 0xD4u));
    st32(r, 0x700036B4u, w32(r, a0 + 0xD4u));
    st32(r, 0x700036C8u, w32(r, a0 + 0xD4u));
    b = w32(r, 0x00275B40u);
    st32(r, w32(r, b + 0xCu) + 0xC0u, 0);
    st32(r, m_of(r, 0xCu) + 0xC4u, 0);
    st32(r, m_of(r, 0xCu) + 0xC8u, 0);
    st32(r, m_of(r, 0x14u) + 0xC0u, 0);
    st32(r, m_of(r, 0x14u) + 0xC4u, 0);
    st32(r, m_of(r, 0x14u) + 0xC8u, 0);
    t = m_of(r, 0xCu) + 0x90u;
    CALL3(FN_1026D0, t, 0x700036A0u, t);
    t = m_of(r, 0x14u) + 0x90u;
    CALL3(FN_1026D0, t, 0x700036A0u, t);
    for (k = 0; k < 3; k++) {
        uint32_t v = w32(r, 0x700038A0u + 4u * k);
        st32(r, m_of(r, 0xCu) + 0xC0u + 4u * k, v);
    }
    for (k = 0; k < 3; k++) {
        uint32_t v = w32(r, 0x700038B0u + 4u * k);
        st32(r, m_of(r, 0x14u) + 0xC0u + 4u * k, v);
    }
}

/* ------------------------------------------------------------------------
 * 0012D580(a0, a1, a2). Frame 0x30. By byte a0 +7:
 *  0  a2 (the whole register) non-zero: nothing. Else a1 +0xD8 = 0; a1
 *     +0xE4 == 0x300: 00128830(a0; 0, 0, -3.5) and 001287F0(a0, a1, 7;
 *     0), else 001287F0(a0, a1, 8; 0); +7 += 1.
 *  1  halfword a1 +0xF8 == 8 or halfword a1 +0xF4 & 0x1000: scratch
 *     0x70003610 = (0, 1.0, 0, 1.0); 001C3DB0(a1 + 0x80, 0x70003610, a1 +
 *     0x70, 0x70003620); 001031E0(a1 + 0x70, 0x70003620); a1 +0x80 = (0,
 *     1.0, 0, 1.0); a1 +0xE4 = 0x500, +0xF0 = 0; halfword a1 +0xF8 == 7:
 *     00128830(a0; 0, 5.0, 0.5), 001287F0(a0, a1, 15; 0), a0 +0xC0 = pi/2;
 *     then +7 += 1.
 *  2  001B5360(a0); a0 +0xC0 = 001B12B0(pi/2, a0 +0xC0, 4 degrees); a1
 *     +0xE4 == 0x100: 001287F0(a0, a1, 9; 0), a0 +0xC0 = 0, +7 += 1, a1
 *     +0xD8 = 0, halfword a1 +0xF4 = 0.
 *  3  halfword a1 +0xF4 & 0x1000: 00128830(a0; 0, 0, 1.0), 001287F0(a0,
 *     a1, 0; 0), 001287F0(a0, a1, 2; 6.0); byte +4 == 2: +5 += 1, +6 = 0;
 *     else +5 == 8: +5 = 1, +6 = 0; else +6 += 1; then +7 = 0.
 * ---------------------------------------------------------------------- */
static void f_12D580(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1, uint64_t a2)
{
    uint32_t sp = sp0 - 0x30u;
    switch (u8(r, a0 + 7u)) {
    case 0:
        if (a2 != 0)
            return;
        st32(r, a1 + 0xD8u, 0);
        if (w32(r, a1 + 0xE4u) == 0x300u) {
            f_128830(r, sp, a0, F_ZERO, F_ZERO, 0xC0600000u);
            anim(r, sp, a0, a1, 7, F_ZERO);
        } else {
            anim(r, sp, a0, a1, 8, F_ZERO);
        }
        st8(r, a0 + 7u, u8(r, a0 + 7u) + 1u);
        break;
    case 1:
        if (s16(r, a1 + 0xF8u) == 8 || (s16(r, a1 + 0xF4u) & 0x1000)) {
            vec4(r, 0x70003610u, F_ZERO, F_ONE, F_ZERO, F_ONE);
            CALL4(FN_1C3DB0, a1 + 0x80u, 0x70003610u, a1 + 0x70u, 0x70003620u);
            CALL2(FN_1031E0, a1 + 0x70u, 0x70003620u);
            vec4(r, a1 + 0x80u, F_ZERO, F_ONE, F_ZERO, F_ONE);
            st32(r, a1 + 0xE4u, 0x500u);
            st32(r, a1 + 0xF0u, 0);
            if (s16(r, a1 + 0xF8u) == 7) {
                f_128830(r, sp, a0, F_ZERO, 0x40A00000u, 0x3F000000u);
                anim(r, sp, a0, a1, 0xF, F_ZERO);
                st32(r, a0 + 0xC0u, F_HALFPI);
            }
            st8(r, a0 + 7u, u8(r, a0 + 7u) + 1u);
        }
        break;
    case 2:
        f_1B5360(r, sp, a0);
        st32(r, a0 + 0xC0u, turn(r, sp, F_HALFPI, w32(r, a0 + 0xC0u), F_TURN));
        if (w32(r, a1 + 0xE4u) == 0x100u) {
            anim(r, sp, a0, a1, 9, F_ZERO);
            st32(r, a0 + 0xC0u, 0);
            st8(r, a0 + 7u, u8(r, a0 + 7u) + 1u);
            st32(r, a1 + 0xD8u, 0);
            st16(r, a1 + 0xF4u, 0);
        }
        break;
    case 3:
        if (s16(r, a1 + 0xF4u) & 0x1000) {
            f_128830(r, sp, a0, F_ZERO, F_ZERO, F_ONE);
            anim(r, sp, a0, a1, 0, F_ZERO);
            anim(r, sp, a0, a1, 2, 0x40C00000u);
            if (u8(r, a0 + 4u) == 2u) {
                st8(r, a0 + 5u, u8(r, a0 + 5u) + 1u);
                st8(r, a0 + 6u, 0);
            } else if (u8(r, a0 + 5u) == 8u) {
                st8(r, a0 + 5u, 1);
                st8(r, a0 + 6u, 0);
            } else {
                st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
            }
            st8(r, a0 + 7u, 0);
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------
 * 00129FC0(a0, a1). Frame 0x40. mode = 001C2770(a0, a1, 0) (kept whole).
 * The damage step D (states 1 and 5): halfword +0x28 non-zero: -= 1. t =
 * halfword +0x36; t non-zero and halfword +0x34 non-zero: when t & 0x4000
 * and +0x28 (read again) is 0: 001EFE00(0x80000027, a0), +0x28 = 0x3C;
 * +0x34 -= byte +0x36; +0x34 <= 0: +0x34 = 0, a1 byte +0xFA = 0,
 * 001EFD90(0x8000000C, a0 + 0xB0, a0 + 0xC0), byte +0 = 3; else byte +0 =
 * 1, +0x36 = 0.
 * By byte a0 +5:
 *  0  a0 +0x38 = a1 +0xD8, a1 +0xD8 = 0; +0x36 & 0x4000: 001EFE00(
 *     0x80000027, a0), +0x28 = 0x3C, else +0x28 = 0; +0x34 -= byte +0x36;
 *     halfword a1 +0xF4 = 0; 001FBD50(a0, 0x1B1, 0; 300). Then when +0x36
 *     & 0x2000, a1 byte +0xFB bit 7, or the word a1 +0xE4 is 0, 0x400 or
 *     0x500: 001287F0(a0, a1, 0x1B; 0), 00103230(a0 + 0x70, a0 + 0x70;
 *     0.5), a0 +0xC0 = 0, a1 +0xF0 = 0.8, a1 +0xE4 = 0x600, +5 = 5; else
 *     001287F0(a0, a1, 0x1D; 0), +5 += 1. Either way +0x34 <= 0: +0x34 =
 *     0, a1 +0xFA = 0, 001EFD90(0x8000000C, a0 + 0xB0, a0 + 0xC0); else
 *     byte +0 = 1. Then +0x36 = 0.
 *  1  D; then halfword a1 +0xF4 & 0x1000: a1 +0xFA = 0; +0x34 <= 0: byte
 *     +0 = 2, +0x34 = 0, scratch 0x700038A0 = (0, -1.4, 0, 1.0), +5 = 2
 *     when 001C25E0(a0, 0x700038A0) is 0 (all 64 bits), else 3 when a1
 *     +0xE4 >> 8 (arithmetic) is 1, else 2; +0x34 > 0: +4 = 1, +5 = a1
 *     +0xFB & 0x7F, +6 = +7 = 0, a1 +0xD8 = a0 +0x38, byte +0 = 1.
 *  2  0012D580(a0, a1, mode).
 *  3  001287F0(a0, a1, 0x20; 4.0), 001FBD50(a0, 0x1B7, 0; 300), byte +0 =
 *     2, +5 += 1, a1 +0xF0 = 0.06.
 *  4  a0 +0x3C < 10.0: 001FC580(a0, 0x1B5); 001EFE00(0x8000000F, a0)
 *     non-zero: +5 = 7, else +4 = 3. Then 00129F00(a0, a1).
 *  5  001B5360(a0); D; a0 +0xB0 += a0 +0x70, a0 +0xB8 += a0 +0x78; a1
 *     +0xE4 != 0x600: +5 += 1.
 *  6  001287F0(a0, a1, 0x1D; 4.0), +5 = 1.
 *  7  00129F00(a0, a1).
 *  8 and above: nothing.
 * Tail: mode zero: 001C3D60(a0, a1). Halfword a1 +0xF4 = 001C64F0(a0; 1.0);
 * 00102958(0x70003400, 0x70003000); 001C69A0(a0); 001B17A0(a0) non-zero:
 * signed byte a1 +0xFA non-zero: 001288D0(a0, a1); then the word a0 +0x4C
 * called with a0.
 * ---------------------------------------------------------------------- */
static void kill_event(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    st16(r, a0 + 0x34u, 0);
    st8(r, a1 + 0xFAu, 0);
    CALL3(FN_1EFD90, 0x8000000Cu, a0 + 0xB0u, a0 + 0xC0u);
}

static void damage_step(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    int32_t t;
    if (s16(r, a0 + 0x28u) != 0)
        st16(r, a0 + 0x28u, (uint32_t)(s16(r, a0 + 0x28u) - 1));
    t = s16(r, a0 + 0x36u);
    if (t != 0 && s16(r, a0 + 0x34u) != 0) {
        if ((t & 0x4000) && s16(r, a0 + 0x28u) == 0) {
            CALL2(FN_1EFE00, 0x80000027u, a0);
            st16(r, a0 + 0x28u, 0x3C);
        }
        st16(r, a0 + 0x34u, (uint32_t)(s16(r, a0 + 0x34u) - (int32_t)u8(r, a0 + 0x36u)));
        if (s16(r, a0 + 0x34u) <= 0) {
            kill_event(r, sp, a0, a1);
            st8(r, a0, 3);
        } else {
            st8(r, a0, 1);
            st16(r, a0 + 0x36u, 0);
        }
    }
}

static void f_129FC0(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x40u;
    uint64_t mode = CALL3(FN_1C2770, a0, a1, 0);
    uint32_t n;
    switch (u8(r, a0 + 5u)) {
    case 0:
        st32(r, a0 + 0x38u, w32(r, a1 + 0xD8u));
        st32(r, a1 + 0xD8u, 0);
        if (s16(r, a0 + 0x36u) & 0x4000) {
            CALL2(FN_1EFE00, 0x80000027u, a0);
            st16(r, a0 + 0x28u, 0x3C);
        } else {
            st16(r, a0 + 0x28u, 0);
        }
        st16(r, a0 + 0x34u, (uint32_t)(s16(r, a0 + 0x34u) - (int32_t)u8(r, a0 + 0x36u)));
        st16(r, a1 + 0xF4u, 0);
        sound_1FBD50(r, sp, a0, 0x1B1u);
        if ((s16(r, a0 + 0x36u) & 0x2000) || (u8(r, a1 + 0xFBu) & 0x80u) ||
            (n = w32(r, a1 + 0xE4u)) == 0 || n == 0x400u || n == 0x500u) {
            anim(r, sp, a0, a1, 0x1B, F_ZERO);
            callg(r, sp, FN_VSCALE, 2, reg(a0 + 0x70u), reg(a0 + 0x70u), 0, 0, 1, 0x3F000000u, 0, 0);
            st32(r, a0 + 0xC0u, 0);
            st32(r, a1 + 0xF0u, 0x3F4CCCCDu);
            st32(r, a1 + 0xE4u, 0x600u);
            st8(r, a0 + 5u, 5);
        } else {
            anim(r, sp, a0, a1, 0x1D, F_ZERO);
            st8(r, a0 + 5u, u8(r, a0 + 5u) + 1u);
        }
        if (s16(r, a0 + 0x34u) <= 0)
            kill_event(r, sp, a0, a1);
        else
            st8(r, a0, 1);
        st16(r, a0 + 0x36u, 0);
        break;
    case 1:
        damage_step(r, sp, a0, a1);
        if (s16(r, a1 + 0xF4u) & 0x1000) {
            st8(r, a1 + 0xFAu, 0);
            if (s16(r, a0 + 0x34u) <= 0) {
                st8(r, a0, 2);
                st16(r, a0 + 0x34u, 0);
                vec4(r, 0x700038A0u, F_ZERO, 0xBFB33333u, F_ZERO, F_ONE);
                if (CALL2(FN_1C25E0, a0, 0x700038A0u) == 0)
                    st8(r, a0 + 5u, 2);
                else if (((int32_t)w32(r, a1 + 0xE4u) >> 8) == 1)
                    st8(r, a0 + 5u, 3);
                else
                    st8(r, a0 + 5u, 2);
            } else {
                st8(r, a0 + 4u, 1);
                st8(r, a0 + 5u, u8(r, a1 + 0xFBu) & 0x7Fu);
                st8(r, a0 + 6u, 0);
                st8(r, a0 + 7u, 0);
                st32(r, a1 + 0xD8u, w32(r, a0 + 0x38u));
                st8(r, a0, 1);
            }
        }
        break;
    case 2:
        f_12D580(r, sp, a0, a1, mode);
        break;
    case 3:
        anim(r, sp, a0, a1, 0x20, 0x40800000u);
        sound_1FBD50(r, sp, a0, 0x1B7u);
        st8(r, a0, 2);
        st8(r, a0 + 5u, u8(r, a0 + 5u) + 1u);
        st32(r, a1 + 0xF0u, 0x3D75C28Fu);
        break;
    case 4:
        if (flt(w32(r, a0 + 0x3Cu), 0x41200000u)) {
            f_1FC580(r, sp, a0, 0x1B5u);
            if (CALL2(FN_1EFE00, 0x8000000Fu, a0) != 0)
                st8(r, a0 + 5u, 7);
            else
                st8(r, a0 + 4u, 3);
        }
        f_129F00(r, sp, a0, a1);
        break;
    case 5:
        f_1B5360(r, sp, a0);
        damage_step(r, sp, a0, a1);
        st32(r, a0 + 0xB0u, fadd(w32(r, a0 + 0xB0u), w32(r, a0 + 0x70u)));
        st32(r, a0 + 0xB8u, fadd(w32(r, a0 + 0xB8u), w32(r, a0 + 0x78u)));
        if (w32(r, a1 + 0xE4u) != 0x600u)
            st8(r, a0 + 5u, u8(r, a0 + 5u) + 1u);
        break;
    case 6:
        anim(r, sp, a0, a1, 0x1D, 0x40800000u);
        st8(r, a0 + 5u, 1);
        break;
    case 7:
        f_129F00(r, sp, a0, a1);
        break;
    default:
        break;
    }
    if (mode == 0)
        CALL2(FN_1C3D60, a0, a1);
    st16(r, a1 + 0xF4u, (uint32_t)callg(r, sp, FN_1C64F0, 1, reg(a0), 0, 0, 0, 1, F_ONE, 0, 0).v0);
    CALL2(FN_COPY_QW4, 0x70003400u, 0x70003000u);
    CALL1(FN_1C69A0, a0);
    if (CALL1(FN_1B17A0, a0) != 0) {
        if (s8(r, a1 + 0xFAu) != 0)
            f_1288D0(r, sp, a1);
        CALL1(w32(r, a0 + 0x4Cu), a0);
    }
}

/* ------------------------------------------------------------------------
 * 0012B410(a0, a1). Frame 0x50 (NEARMISS C; the instructions decide).
 * busy = 001C2770(a0, a1, 0) (kept whole). By byte a0 +6:
 *  0  a1 +0xD8 = 0 (always). busy zero: halfword a1 +0xD0 = 0x3C when
 *     00122BB8() & 1, else 0x78; scratch 0x700038A0 = (0, -1.4, -5.0,
 *     1.0); when 001C25E0(a0, 0x700038A0) is zero or 00128600(3) is
 *     non-zero (called only after a non-zero 001C25E0): +6 += 1, a1 +0xD8
 *     = 0.3, 0012ADC0(a0, a0 + 0xB0, 0x00810350; 220.0) zero: a1 +0xE8 =
 *     a0 +0xC4; then 001287F0(a0, a1, 6; 8.0). Otherwise +6 = 2, a1 +0xD8
 *     = 0, 001287F0(a0, a1, 3; 8.0).
 *  1  a0 +0xC4 = 001B12B0(a1 +0xE8, a0 +0xC4, 4 degrees). The word
 *     player +0x230 == 8: when the word 0x70003B68 & 0x1F is 0, a1 +0xE8 =
 *     001B1470(2pi * float(00122BB8() & 0xF0) / 256). Otherwise when
 *     0x70003B68 & 0xF is 0: 0012ADC0(a0, a0 + 0xB0, 0x00810350; 220.0).
 *     Then the countdown C: halfword a1 +0xD0 -= 1; at 0: +6 = 3, a1 +0xD8
 *     = 0.
 *  2  byte a1 +0xE1 zero and halfword a0 +0x54 non-zero: +6 = 4, +7 = 0;
 *     else C.
 *  3  00128640(a0) zero: +5 = 1, +6 = 0, +7 = 0.
 *  4  by byte +7: 0: a1 +0xD8 = 0, byte +0 = 2, 001287F0(a0, a1, 0x21;
 *     8.0), 001EFE00(0x8000002B, a0), +7 += 1. 1: halfword a1 +0xF4 &
 *     0x1000: a1 +0xE1 = 1, +7 += 1. 2: halfword +0x34 = 00128390(a0, 1),
 *     +0x36 = 0, byte +0 = the a1 register after that call (00128390 does
 *     not change it: 1), +6 = 3, +7 = 0.
 * Tail: busy zero: 001C3D60(a0, a1).
 * ---------------------------------------------------------------------- */
static void countdown_d0(Run *r, uint32_t a0, uint32_t a1)
{
    int16_t t = (int16_t)(s16(r, a1 + 0xD0u) - 1);
    st16(r, a1 + 0xD0u, (uint16_t)t);
    if (t == 0) {
        st8(r, a0 + 6u, 3);
        st32(r, a1 + 0xD8u, 0);
    }
}

/* 0012ADC0(a0, a0 + 0xB0, 0x00810350; 220.0) -> v0 */
static uint64_t aim_12ADC0(Run *r, uint32_t sp, uint32_t a0)
{
    return callg(r, sp, FN_12ADC0, 3, reg(a0), reg(a0 + 0xB0u), reg(PLAYER + 0xA0u), 0, 1, 0x435C0000u, 0, 0).v0;
}

static void f_12B410(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x50u;
    uint64_t busy = CALL3(FN_1C2770, a0, a1, 0);
    uint32_t k;
    switch (u8(r, a0 + 6u)) {
    case 0:
        st32(r, a1 + 0xD8u, 0);
        if (busy != 0)
            break;
        st16(r, a1 + 0xD0u, (CALL0(FN_RAND) & 1u) ? 0x3Cu : 0x78u);
        vec4(r, 0x700038A0u, F_ZERO, 0xBFB33333u, 0xC0A00000u, F_ONE);
        if (CALL2(FN_1C25E0, a0, 0x700038A0u) == 0 || CALL1(FN_128600, 3u) != 0) {
            st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
            st32(r, a1 + 0xD8u, 0x3E99999Au);
            if (aim_12ADC0(r, sp, a0) == 0)
                st32(r, a1 + 0xE8u, w32(r, a0 + 0xC4u));
            anim(r, sp, a0, a1, 6, 0x41000000u);
        } else {
            st8(r, a0 + 6u, 2);
            st32(r, a1 + 0xD8u, 0);
            anim(r, sp, a0, a1, 3, 0x41000000u);
        }
        break;
    case 1:
        st32(r, a0 + 0xC4u, turn(r, sp, w32(r, a1 + 0xE8u), w32(r, a0 + 0xC4u), F_TURN));
        if (w32(r, PLAYER + 0x230u) == 8u) {
            if ((w32(r, 0x70003B68u) & 0x1Fu) == 0) {
                uint32_t a = em_ee_cvt_s_w_bits((uint32_t)CALL0(FN_RAND) & 0xF0u);
                a = fdiv(fmul(0x40C90FDBu, a), 0x43800000u);
                st32(r, a1 + 0xE8u, FCALL1(FN_1B1470, a));
            }
        } else if ((w32(r, 0x70003B68u) & 0xFu) == 0) {
            aim_12ADC0(r, sp, a0);
        }
        countdown_d0(r, a0, a1);
        break;
    case 2:
        if (u8(r, a1 + 0xE1u) == 0 && s16(r, a0 + 0x54u) != 0) {
            st8(r, a0 + 6u, 4);
            st8(r, a0 + 7u, 0);
        } else {
            countdown_d0(r, a0, a1);
        }
        break;
    case 3:
        if (CALL1(FN_128640, a0) == 0) {
            st8(r, a0 + 5u, 1);
            st8(r, a0 + 6u, 0);
            st8(r, a0 + 7u, 0);
        }
        break;
    case 4:
        k = u8(r, a0 + 7u);
        if (k == 0) {
            st32(r, a1 + 0xD8u, 0);
            st8(r, a0, 2);
            anim(r, sp, a0, a1, 0x21, 0x41000000u);
            CALL2(FN_1EFE00, 0x8000002Bu, a0);
            st8(r, a0 + 7u, u8(r, a0 + 7u) + 1u);
        } else if (k == 1) {
            if (s16(r, a1 + 0xF4u) & 0x1000) {
                st8(r, a1 + 0xE1u, 1);
                st8(r, a0 + 7u, u8(r, a0 + 7u) + 1u);
            }
        } else if (k == 2) {
            st16(r, a0 + 0x34u, (uint32_t)CALL2(FN_128390, a0, 1u));
            st16(r, a0 + 0x36u, 0);
            st8(r, a0, 1);
            st8(r, a0 + 6u, 3);
            st8(r, a0 + 7u, 0);
        }
        break;
    default:
        break;
    }
    if (busy == 0)
        CALL2(FN_1C3D60, a0, a1);
}

/* ------------------------------------------------------------------------
 * The shared opening of 0012B970 / 0012BE20 / 0012C490 (state 0): a1 +0xD8
 * = 0, +7 = 0; busy non-zero: done. Scratch 0x700038A0 = (0, -1.4, -5.0,
 * 1.0); 001C25E0(a0, 0x700038A0) zero: +5 = 2, +6 = +7 = 0, result 0.
 * Otherwise a0 +0xB0, +0xB4, +0xB8 -= a1 +0x80, +0x84, +0x88 / 2.0 (each);
 * scratch 0x700038B0 = (0, -4.0, 0, 1.0); 001C2540(a0, a1 + 0x60,
 * 0x700038B0, a0 + 0xD0); result 1. `up` then stages (0, 1.0, -5.0, 1.0)
 * at 0x700038B0 and calls 001C24D0(a0, 0x700038B0, a0 + 0xD0).
 * ---------------------------------------------------------------------- */
static int settle_open(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint64_t busy)
{
    st32(r, a1 + 0xD8u, 0);
    st8(r, a0 + 7u, 0);
    if (busy != 0)
        return 0;
    vec4(r, 0x700038A0u, F_ZERO, 0xBFB33333u, 0xC0A00000u, F_ONE);
    if (CALL2(FN_1C25E0, a0, 0x700038A0u) == 0) {
        st8(r, a0 + 5u, 2);
        st8(r, a0 + 6u, 0);
        st8(r, a0 + 7u, 0);
        return 0;
    }
    st32(r, a0 + 0xB0u, fsub(w32(r, a0 + 0xB0u), fdiv(w32(r, a1 + 0x80u), F_TWO)));
    st32(r, a0 + 0xB4u, fsub(w32(r, a0 + 0xB4u), fdiv(w32(r, a1 + 0x84u), F_TWO)));
    st32(r, a0 + 0xB8u, fsub(w32(r, a0 + 0xB8u), fdiv(w32(r, a1 + 0x88u), F_TWO)));
    vec4(r, 0x700038B0u, F_ZERO, 0xC0800000u, F_ZERO, F_ONE);
    CALL4(FN_1C2540, a0, a1 + 0x60u, 0x700038B0u, a0 + 0xD0u);
    return 1;
}

static void settle_up(Run *r, uint32_t sp, uint32_t a0)
{
    vec4(r, 0x700038B0u, F_ZERO, F_ONE, 0xC0A00000u, F_ONE);
    CALL3(FN_1C24D0, a0, 0x700038B0u, a0 + 0xD0u);
}

/* +5 = 1, +6 = +7 = 0 when 00128640(a0) is zero (all 64 bits). */
static void back_to_one(Run *r, uint32_t sp, uint32_t a0)
{
    if (CALL1(FN_128640, a0) == 0) {
        st8(r, a0 + 5u, 1);
        st8(r, a0 + 6u, 0);
        st8(r, a0 + 7u, 0);
    }
}

/* Scratch 0x700038A0 = (x, y, 0, 1.0) turned by the matrix at (word
 * (word D_00275B40) +0x14) + 0x90; 0x700038B0 = (0, by, bz, 1.0); `norm`:
 * 00102760(0x700038B0, 0x700038B0); then turned by a0 + 0xD0;
 * 001EFFD0(0x80000008, 0x700038A0, 0x700038B0, 0xA or 0xB by a1 +0xE1;
 * f). */
static void spray(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t x, uint32_t y, uint32_t by,
                  uint32_t bz, int norm, uint32_t f)
{
    vec4(r, 0x700038A0u, x, y, F_ZERO, F_ONE);
    CALL3(FN_MAT_VEC, 0x700038A0u, w32(r, w32(r, 0x00275B40u) + 0x14u) + 0x90u, 0x700038A0u);
    vec4(r, 0x700038B0u, F_ZERO, by, bz, F_ONE);
    if (norm)
        CALL2(FN_102760, 0x700038B0u, 0x700038B0u);
    CALL3(FN_MAT_VEC, 0x700038B0u, a0 + 0xD0u, 0x700038B0u);
    callg(r, sp, FN_1EFFD0, 4, reg(0x80000008u), reg(0x700038A0u), reg(0x700038B0u),
          reg(u8(r, a1 + 0xE1u) == 0 ? 0xAu : 0xBu), 1, f, 0, 0);
}

/* ------------------------------------------------------------------------
 * 0012B970(a0, a1). Frame 0x40. busy = 001C2770(a0, a1, 0). By byte +6:
 *  0  settle_open; on 1: a1 +0xE4 == 0x100: +6 = 2 and settle_up; else +6
 *     = 1 when 00128600(1) is non-zero, else 2.
 *  1  0012D580(a0, a1, busy).
 *  2  00128830(a0; 0, 0, -3.5), 001287F0(a0, a1, 10; 0), +6 += 1, +7 = 0.
 *  3  halfword a1 +0xF4 & 0x1000: 00128830(a0; 0, 3.5, 0), 001287F0(a0,
 *     a1, 11; 0), +6 += 1, +7 = 0; else 0012E2C0(a0, a1).
 *  4  a0 +0x3C == 70.0: spray((0.838, 0.42), (0, 0.7071, 0.7071)
 *     normalised; 0.6). Then halfword a1 +0xF4 & 0x1000: 00128830(a0; 0,
 *     -3.5, 3.5), 001287F0(a0, a1, 12; 0), +6 += 1.
 *  5  halfword a1 +0xF4 & 0x1000 and 00128640(a0) zero: settle_up, +5 =
 *     1, +6 = +7 = 0.
 * Tail: busy zero: 001C3D60(a0, a1).
 * ---------------------------------------------------------------------- */
static void f_12B970(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x40u;
    uint64_t busy = CALL3(FN_1C2770, a0, a1, 0);
    switch (u8(r, a0 + 6u)) {
    case 0:
        if (settle_open(r, sp, a0, a1, busy)) {
            if (w32(r, a1 + 0xE4u) == 0x100u) {
                st8(r, a0 + 6u, 2);
                settle_up(r, sp, a0);
            } else if (CALL1(FN_128600, 1u) != 0) {
                st8(r, a0 + 6u, 1);
            } else {
                st8(r, a0 + 6u, 2);
            }
        }
        break;
    case 1:
        f_12D580(r, sp, a0, a1, busy);
        break;
    case 2:
        f_128830(r, sp, a0, F_ZERO, F_ZERO, 0xC0600000u);
        anim(r, sp, a0, a1, 0xA, F_ZERO);
        st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
        st8(r, a0 + 7u, 0);
        break;
    case 3:
        if (s16(r, a1 + 0xF4u) & 0x1000) {
            f_128830(r, sp, a0, F_ZERO, 0x40600000u, F_ZERO);
            anim(r, sp, a0, a1, 0xB, F_ZERO);
            st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
            st8(r, a0 + 7u, 0);
        } else {
            f_12E2C0(r, sp, a0, a1);
        }
        break;
    case 4:
        if (feq(w32(r, a0 + 0x3Cu), 0x428C0000u))
            spray(r, sp, a0, a1, 0x3F56872Bu, 0x3ED70A3Du, 0x3F34FDF4u, 0x3F34FDF4u, 1, 0x3F19999Au);
        if (s16(r, a1 + 0xF4u) & 0x1000) {
            f_128830(r, sp, a0, F_ZERO, 0xC0600000u, 0x40600000u);
            anim(r, sp, a0, a1, 0xC, F_ZERO);
            st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
        }
        break;
    case 5:
        if ((s16(r, a1 + 0xF4u) & 0x1000) && CALL1(FN_128640, a0) == 0) {
            settle_up(r, sp, a0);
            st8(r, a0 + 5u, 1);
            st8(r, a0 + 6u, 0);
            st8(r, a0 + 7u, 0);
        }
        break;
    default:
        break;
    }
    if (busy == 0)
        CALL2(FN_1C3D60, a0, a1);
}

/* ------------------------------------------------------------------------
 * The rising step of 0012BE20 / 0012C490 (state 5, a1 +0xE4 & 0xF zero):
 * a0 +0xC0 = pi * (75.0 * -(a1 +0xF0)) / 180.0; a1 +0xF0 = a1 +0xF0 (read
 * again) - 0.04; below 0: a0 +0xC0 = 0, +6 += 1, then `below`. Then a0
 * +0xB4 += a1 +0xF0 (read again). Returns 1 when it went below 0.
 * ---------------------------------------------------------------------- */
static int rise_step(Run *r, uint32_t a0, uint32_t a1, int kind)
{
    uint32_t t = fmul(0x42960000u, em_ee_neg_bits(w32(r, a1 + 0xF0u)));
    int below;
    st32(r, a0 + 0xC0u, fdiv(fmul(F_PI, t), 0x43340000u));
    t = fsub(w32(r, a1 + 0xF0u), 0x3D23D70Au);
    st32(r, a1 + 0xF0u, t);
    below = flt(t, F_ZERO);
    if (below) {
        st32(r, a0 + 0xC0u, 0);
        st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
        if (kind == 0)
            st32(r, a1 + 0xE4u, 0x400u);
        else
            st16(r, a1 + 0xD0u, 0);
    }
    st32(r, a0 + 0xB4u, fadd(w32(r, a0 + 0xB4u), w32(r, a1 + 0xF0u)));
    return below;
}

/* 0012BE20 / 0012C490 state 4: halfword a1 +0xF4 & 0x5000: +6 = st + 1,
 * 00128830(a0; 0, 1.5, 3.0), 001287F0(a0, a1, k; 0), halfword a1 +0xD0 =
 * 0x78, a1 +0xE4 = 0, a1 +0xF0 = 0.6, a0 +0xC0 = -pi/4, a1 +0xD8 = 0.6,
 * 001FC580(a0, 0x1AA). */
static void jump_off(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t st, uint32_t k)
{
    if (!(s16(r, a1 + 0xF4u) & 0x5000))
        return;
    st8(r, a0 + 6u, st + 1u);
    f_128830(r, sp, a0, F_ZERO, 0x3FC00000u, 0x40400000u);
    anim(r, sp, a0, a1, k, F_ZERO);
    st16(r, a1 + 0xD0u, 0x78u);
    st32(r, a1 + 0xE4u, 0);
    st32(r, a1 + 0xF0u, 0x3F19999Au);
    st32(r, a0 + 0xC0u, 0xBF490FDBu);
    st32(r, a1 + 0xD8u, 0x3F19999Au);
    f_1FC580(r, sp, a0, 0x1AAu);
}

/* The landing: halfword a1 +0xF4 & 0x1000: 00128830(a0; 0, 0, 1.0),
 * 001287F0(a0, a1, 0; 0), 001287F0(a0, a1, 1; 6.0), a0 +0xC4 = 001B1470(pi
 * + a0 +0xC4), back_to_one. */
static void land(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    if (!(s16(r, a1 + 0xF4u) & 0x1000))
        return;
    f_128830(r, sp, a0, F_ZERO, F_ZERO, F_ONE);
    anim(r, sp, a0, a1, 0, F_ZERO);
    anim(r, sp, a0, a1, 1, 0x40C00000u);
    st32(r, a0 + 0xC4u, FCALL1(FN_1B1470, fadd(F_PI, w32(r, a0 + 0xC4u))));
    back_to_one(r, sp, a0);
}

/* ------------------------------------------------------------------------
 * 0012BE20(a0, a1). Frame 0x40. busy = 001C2770(a0, a1, 2). st = byte +6:
 *  0  settle_open; on 1: a1 +0xE4 == 0x100: settle_up, +6 = 2; else +6 =
 *     1.
 *  1  0012D580(a0, a1, busy).
 *  2  00128830(a0; 0, 0, -2.5), 001287F0(a0, a1, 13; 0), +6 += 1, +7 = 0,
 *     a1 +0xD8 = 0.
 *  3  halfword a1 +0xF4 & 0x1000: +6 = st + 1, +7 = 0, 00128830(a0; 0,
 *     2.5, 0), 001287F0(a0, a1, 14; 0); else 0012E2C0(a0, a1).
 *  4  jump_off(k 15).
 *  5  001B5360(a0). a1 +0xE4 & 0xF: back_to_one. Else rise_step (below:
 *     a1 +0xE4 = 0x400); then halfword a1 +0xF8 != 0x10 and c = a0 +0xC0
 *     is not < -10 degrees but < 0 and 001B13F0(0x00810350, a0 + 0xB0;
 *     50.0) non-zero: 001287F0(a0, a1, 16; 0) and spray((0.838, 0.42),
 *     (0, 0, 1.0) not normalised; 0.7).
 *  6  001B5360(a0). a1 +0xE4 == 0x100: 001287F0(a0, a1, 17; 0), a0 +0xC0
 *     = 0, +6 += 1, a1 +0xD8 = 0, halfword a1 +0xF4 = 0. Else a1 +0xE4 &
 *     0xF: back_to_one.
 *  7  land.
 * Tail: busy zero: 001C3D60(a0, a1).
 * ---------------------------------------------------------------------- */
static void f_12BE20(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x40u;
    uint64_t busy = CALL3(FN_1C2770, a0, a1, 2);
    uint32_t st = u8(r, a0 + 6u), c;
    switch (st) {
    case 0:
        if (settle_open(r, sp, a0, a1, busy)) {
            if (w32(r, a1 + 0xE4u) == 0x100u) {
                settle_up(r, sp, a0);
                st8(r, a0 + 6u, 2);
            } else {
                st8(r, a0 + 6u, 1);
            }
        }
        break;
    case 1:
        f_12D580(r, sp, a0, a1, busy);
        break;
    case 2:
        f_128830(r, sp, a0, F_ZERO, F_ZERO, 0xC0200000u);
        anim(r, sp, a0, a1, 0xD, F_ZERO);
        st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
        st8(r, a0 + 7u, 0);
        st32(r, a1 + 0xD8u, 0);
        break;
    case 3:
        if (s16(r, a1 + 0xF4u) & 0x1000) {
            st8(r, a0 + 6u, st + 1u);
            st8(r, a0 + 7u, 0);
            f_128830(r, sp, a0, F_ZERO, 0x40200000u, F_ZERO);
            anim(r, sp, a0, a1, 0xE, F_ZERO);
        } else {
            f_12E2C0(r, sp, a0, a1);
        }
        break;
    case 4:
        jump_off(r, sp, a0, a1, st, 0xF);
        break;
    case 5:
        f_1B5360(r, sp, a0);
        if (w32(r, a1 + 0xE4u) & 0xFu) {
            back_to_one(r, sp, a0);
            break;
        }
        rise_step(r, a0, a1, 0);
        if (s16(r, a1 + 0xF8u) == 0x10)
            break;
        c = w32(r, a0 + 0xC0u);
        if (flt(c, 0xBE32B8C3u) || !flt(c, F_ZERO))
            break;
        if (callg(r, sp, FN_1B13F0, 2, reg(0x00810350u), reg(a0 + 0xB0u), 0, 0, 1, 0x42480000u, 0, 0).v0 == 0)
            break;
        anim(r, sp, a0, a1, 0x10, F_ZERO);
        spray(r, sp, a0, a1, 0x3F56872Bu, 0x3ED70A3Du, F_ZERO, F_ONE, 0, 0x3F333333u);
        break;
    case 6:
        f_1B5360(r, sp, a0);
        if (w32(r, a1 + 0xE4u) == 0x100u) {
            anim(r, sp, a0, a1, 0x11, F_ZERO);
            st32(r, a0 + 0xC0u, 0);
            st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
            st32(r, a1 + 0xD8u, 0);
            st16(r, a1 + 0xF4u, 0);
        } else if (w32(r, a1 + 0xE4u) & 0xFu) {
            back_to_one(r, sp, a0);
        }
        break;
    case 7:
        land(r, sp, a0, a1);
        break;
    default:
        break;
    }
    if (busy == 0)
        CALL2(FN_1C3D60, a0, a1);
}

/* ------------------------------------------------------------------------
 * 0012C490(a0, a1). Frame 0x40. busy = 001C2770(a0, a1, 2). st = byte +6:
 *  0  as 0012BE20 state 0.
 *  1  0012D580(a0, a1, busy).
 *  2  00128830(a0; 0, 0, -2.5), 001287F0(a0, a1, 19; 0), +6 += 1, +7 = 0,
 *     a1 +0xD8 = 0, 001FBD50(a0, 0x1AE, 0; 300).
 *  3  as 0012BE20 state 3 with animation 20.
 *  4  jump_off(k 21).
 *  5  001B5360(a0), 0012E260(a0). a1 +0xE4 & 0xF: back_to_one. Else
 *     rise_step (below: halfword a1 +0xD0 = 0); then 0012E0B0(a0, a1)
 *     non-zero: a0 +0xC0 = 0, +5 = 6, +6 = 0.
 *  6  001B5360(a0), 0012E260(a0); a0 +0xB4 += 0.1 * 0011E2A8(001B1470(
 *     float(halfword a1 +0xD0))); halfword a1 +0xD0 += 0x18. a1 +0xE4 &
 *     0xF: back_to_one. Else 0012E0B0(a0, a1) non-zero: a0 +0xC0 = 0, +5 =
 *     6, +6 = 0. Else halfword a1 +0xD0 > 0x2D0: a1 +0xE4 = 0x400, +6 += 1.
 *  7  001B5360(a0); e = a1 +0xE4: 0x100: 001287F0(a0, a1, 17; 0), a0
 *     +0xC0 = 0, +6 += 1, a1 +0xD8 = 0, halfword a1 +0xF4 = 0. Else e &
 *     0xF: back_to_one.
 *  8  land.
 * Tail: busy zero: 001C3D60(a0, a1).
 * ---------------------------------------------------------------------- */
static void f_12C490(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x40u;
    uint64_t busy = CALL3(FN_1C2770, a0, a1, 2);
    uint32_t st = u8(r, a0 + 6u), e, t;
    switch (st) {
    case 0:
        if (settle_open(r, sp, a0, a1, busy)) {
            if (w32(r, a1 + 0xE4u) == 0x100u) {
                settle_up(r, sp, a0);
                st8(r, a0 + 6u, 2);
            } else {
                st8(r, a0 + 6u, 1);
            }
        }
        break;
    case 1:
        f_12D580(r, sp, a0, a1, busy);
        break;
    case 2:
        f_128830(r, sp, a0, F_ZERO, F_ZERO, 0xC0200000u);
        anim(r, sp, a0, a1, 0x13, F_ZERO);
        st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
        st8(r, a0 + 7u, 0);
        st32(r, a1 + 0xD8u, 0);
        sound_1FBD50(r, sp, a0, 0x1AEu);
        break;
    case 3:
        if (s16(r, a1 + 0xF4u) & 0x1000) {
            st8(r, a0 + 6u, st + 1u);
            st8(r, a0 + 7u, 0);
            f_128830(r, sp, a0, F_ZERO, 0x40200000u, F_ZERO);
            anim(r, sp, a0, a1, 0x14, F_ZERO);
        } else {
            f_12E2C0(r, sp, a0, a1);
        }
        break;
    case 4:
        jump_off(r, sp, a0, a1, st, 0x15);
        break;
    case 5:
        f_1B5360(r, sp, a0);
        f_12E260(r, sp, a0);
        if (w32(r, a1 + 0xE4u) & 0xFu) {
            back_to_one(r, sp, a0);
            break;
        }
        rise_step(r, a0, a1, 1);
        if (f_12E0B0(r, sp, a0, a1) != 0) {
            st32(r, a0 + 0xC0u, 0);
            st8(r, a0 + 5u, 6);
            st8(r, a0 + 6u, 0);
        }
        break;
    case 6:
        f_1B5360(r, sp, a0);
        f_12E260(r, sp, a0);
        t = FCALL1(FN_1B1470, em_ee_cvt_s_w_bits((uint32_t)s16(r, a1 + 0xD0u)));
        t = FCALL1(FN_11E2A8, t);
        st32(r, a0 + 0xB4u, fadd(w32(r, a0 + 0xB4u), fmul(0x3DCCCCCDu, t)));
        st16(r, a1 + 0xD0u, (uint32_t)(s16(r, a1 + 0xD0u) + 0x18));
        if (w32(r, a1 + 0xE4u) & 0xFu) {
            back_to_one(r, sp, a0);
        } else if (f_12E0B0(r, sp, a0, a1) != 0) {
            st32(r, a0 + 0xC0u, 0);
            st8(r, a0 + 5u, 6);
            st8(r, a0 + 6u, 0);
        } else if (s16(r, a1 + 0xD0u) > 0x2D0) {
            st32(r, a1 + 0xE4u, 0x400u);
            st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
        }
        break;
    case 7:
        f_1B5360(r, sp, a0);
        e = w32(r, a1 + 0xE4u);
        if (e == 0x100u) {
            anim(r, sp, a0, a1, 0x11, F_ZERO);
            st32(r, a0 + 0xC0u, 0);
            st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
            st32(r, a1 + 0xD8u, 0);
            st16(r, a1 + 0xF4u, 0);
        } else if (e & 0xFu) {
            back_to_one(r, sp, a0);
        }
        break;
    case 8:
        land(r, sp, a0, a1);
        break;
    default:
        break;
    }
    if (busy == 0)
        CALL2(FN_1C3D60, a0, a1);
}

/* ------------------------------------------------------------------------
 * The attachment matrix of 0012CAA0 / 0012D240 at `m`: rot_matrix(m;
 * a0 +0xC0, +0xC4, +0xC8); 001031E0(m + 0x30, a1 + 0x30); 001026D0(m,
 * attach_matrix(halfword a1 +0xF6), m).
 * ---------------------------------------------------------------------- */
static void attach_pose(Run *r, uint32_t sp, uint32_t m, uint32_t a0, uint32_t a1, uint32_t k)
{
    rot_matrix(r, sp, m, w32(r, a0 + 0xC0u), w32(r, a0 + 0xC4u), w32(r, a0 + 0xC8u));
    CALL2(FN_1031E0, m + 0x30u, a1 + 0x30u);
    {
        uint32_t mm = attach_matrix(r, k);
        CALL3(FN_1026D0, m, mm, m);
    }
}

/* The six floats of row (k & 7) of the table 0x00242DF0 (0x18 bytes per
 * row) into a1 +0x30, +0x34, +0x38 (and +0x40, +0x44, +0x48 when `all`). */
static void attach_row(Run *r, uint32_t a1, uint32_t k, int all)
{
    uint32_t row = 0x00242DF0u + k * 0x18u;
    st32(r, a1 + 0x30u, w32(r, row));
    st32(r, a1 + 0x34u, w32(r, row + 4u));
    st32(r, a1 + 0x38u, w32(r, row + 8u));
    if (all) {
        st32(r, a1 + 0x40u, w32(r, row + 0xCu));
        st32(r, a1 + 0x44u, w32(r, row + 0x10u));
        st32(r, a1 + 0x48u, w32(r, row + 0x14u));
    }
}

/* ------------------------------------------------------------------------
 * 0012CAA0(a0, a1). Frame 0x60. k = halfword a1 +0xF6 & 7 (read once). P
 * is the record 0x008102B0. By byte +6:
 *  0  +6 += 1; attach_row(k, all); scratch matrix 0x700036A0 = identity
 *     turned by a1 +0x40, +0x44, +0x48; 001031E0(0x700036D0, a1 + 0x30);
 *     001026D0(0x700036A0, attach_matrix(k), 0x700036A0); a1 +0x10..+0x18
 *     = 0x700036D0.. - a0 +0xB0..; a1 +0x1C = 1.0; a1 +0xE8 = P +0xC4;
 *     a1 +0xD4 = 1.0; then state 1 in the same call.
 *  1  a0 +0xC4 = 001B12B0(P +0xC4 (k >= 4) or pi + P +0xC4, a0 +0xC4, 4
 *     degrees); a0 +0xC0 = 001B12B0(-pi/2, a0 +0xC0, float(4) * 0.0174532925);
 *     a1 +0xD4 -= 0.02; 00103230(a1 + 0x10, a1 + 0x10; a1 +0xD4); g =
 *     001B1470(P +0xC4 - a1 +0xE8); 001029C0(0x700036A0); 00102BB0(
 *     0x700036A0, 0x700036A0; 001B1470(pi + g)); 001026A0(0x700038A0,
 *     0x700036A0, a1 + 0x10); the state-0 matrix again (identity, a1 +0x40
 *     .. turns, 001031E0, 001026D0); a0 +0xB0 = 0x700036D0 + 0x700038A0,
 *     +0xB4 = 0x700036D4 - 0x700038A4, +0xB8 = 0x700036D8 + 0x700038A8;
 *     rot_matrix(0x70003000; a0 +0xC0, +0xC4, +0xC8); 00102958(a0 + 0xD0,
 *     0x70003000); 001031E0(0x70003030, a0 + 0xB0). a1 +0xD4 <= 0.2: +6 +=
 *     1, +7 = 0, attach_row(k, xyz only), a0 +0xC0..C8 = a1 +0x40..48,
 *     001287F0(a0, a1, 0x16; 0), halfword a1 +0xD0 = 0xF0.
 *  2  attach_pose(0x70003000); 001031E0(a0 + 0xB0, 0x70003030). Byte +7
 *     zero and halfword a1 +0xF4 & 0x4000: P +0x224 = 10.0 / 15.0 (a1 +0xE1
 *     zero; D_0081070A zero / not) or 18.0 / 22.0; scratch 0x700038A0 =
 *     (0, 1.0, 3.0, 1.0) turned by 0x70003000; f = 001B1470(pi + P +0xC4)
 *     when k >= 4, else P +0xC4; 0x700038B0 = (0, f, 0, 1.0); P +0x70 =
 *     0011E2A8(f), +0x74 = 0, +0x78 = 0011DE90(f), +0x7C = 1.0;
 *     001EFD90(0x80000006, 0x700038A0, 0x700038B0); +7 = 1; 001FBD50(a0,
 *     0x1B2, 0; 300). Then halfword a1 +0xD0 -= 1; at 0: +5 = 7, +6 = +7 =
 *     0.
 * Tail (every state, also 3 and above): D_008106BC or the byte 0x70003B8D
 * non-zero: +5 = 0xA, +6 = +7 = 0.
 * ---------------------------------------------------------------------- */
static void f_12CAA0(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x60u;
    uint32_t k = (uint32_t)s16(r, a1 + 0xF6u) & 7u, g, f;
    uint32_t st = u8(r, a0 + 6u);
    if (st == 0) {
        st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
        attach_row(r, a1, k, 1);
        rot_matrix(r, sp, 0x700036A0u, w32(r, a1 + 0x40u), w32(r, a1 + 0x44u), w32(r, a1 + 0x48u));
        CALL2(FN_1031E0, 0x700036D0u, a1 + 0x30u);
        g = attach_matrix(r, k);
        CALL3(FN_1026D0, 0x700036A0u, g, 0x700036A0u);
        st32(r, a1 + 0x10u, fsub(w32(r, 0x700036D0u), w32(r, a0 + 0xB0u)));
        st32(r, a1 + 0x14u, fsub(w32(r, 0x700036D4u), w32(r, a0 + 0xB4u)));
        st32(r, a1 + 0x18u, fsub(w32(r, 0x700036D8u), w32(r, a0 + 0xB8u)));
        st32(r, a1 + 0x1Cu, F_ONE);
        st32(r, a1 + 0xE8u, w32(r, PLAYER + 0xC4u));
        st32(r, a1 + 0xD4u, F_ONE);
        st = 1;
    }
    if (st == 1) {
        if (k >= 4)
            g = w32(r, PLAYER + 0xC4u);
        else
            g = fadd(F_PI, w32(r, PLAYER + 0xC4u));
        st32(r, a0 + 0xC4u, turn(r, sp, g, w32(r, a0 + 0xC4u), F_TURN));
        st32(r, a0 + 0xC0u,
             turn(r, sp, 0xBFC90FDBu, w32(r, a0 + 0xC0u), fmul(em_ee_cvt_s_w_bits(4u), 0x3C8EFA35u)));
        st32(r, a1 + 0xD4u, fsub(w32(r, a1 + 0xD4u), 0x3CA3D70Au));
        callg(r, sp, FN_VSCALE, 2, reg(a1 + 0x10u), reg(a1 + 0x10u), 0, 0, 1, w32(r, a1 + 0xD4u), 0, 0);
        g = FCALL1(FN_1B1470, fsub(w32(r, PLAYER + 0xC4u), w32(r, a1 + 0xE8u)));
        CALL1(FN_IDENT, 0x700036A0u);
        f = FCALL1(FN_1B1470, fadd(F_PI, g));
        callg(r, sp, FN_102BB0, 2, reg(0x700036A0u), reg(0x700036A0u), 0, 0, 1, f, 0, 0);
        CALL3(FN_MAT_VEC, 0x700038A0u, 0x700036A0u, a1 + 0x10u);
        rot_matrix(r, sp, 0x700036A0u, w32(r, a1 + 0x40u), w32(r, a1 + 0x44u), w32(r, a1 + 0x48u));
        CALL2(FN_1031E0, 0x700036D0u, a1 + 0x30u);
        g = attach_matrix(r, k);
        CALL3(FN_1026D0, 0x700036A0u, g, 0x700036A0u);
        st32(r, a0 + 0xB0u, fadd(w32(r, 0x700036D0u), w32(r, 0x700038A0u)));
        st32(r, a0 + 0xB4u, fsub(w32(r, 0x700036D4u), w32(r, 0x700038A4u)));
        st32(r, a0 + 0xB8u, fadd(w32(r, 0x700036D8u), w32(r, 0x700038A8u)));
        rot_matrix(r, sp, 0x70003000u, w32(r, a0 + 0xC0u), w32(r, a0 + 0xC4u), w32(r, a0 + 0xC8u));
        CALL2(FN_COPY_QW4, a0 + 0xD0u, 0x70003000u);
        CALL2(FN_1031E0, 0x70003030u, a0 + 0xB0u);
        if (fle(w32(r, a1 + 0xD4u), 0x3E4CCCCDu)) {
            st8(r, a0 + 6u, u8(r, a0 + 6u) + 1u);
            st8(r, a0 + 7u, 0);
            attach_row(r, a1, k, 0);
            st32(r, a0 + 0xC0u, w32(r, a1 + 0x40u));
            st32(r, a0 + 0xC4u, w32(r, a1 + 0x44u));
            st32(r, a0 + 0xC8u, w32(r, a1 + 0x48u));
            anim(r, sp, a0, a1, 0x16, F_ZERO);
            st16(r, a1 + 0xD0u, 0xF0u);
        }
    } else if (st == 2) {
        int16_t t;
        attach_pose(r, sp, 0x70003000u, a0, a1, k);
        CALL2(FN_1031E0, a0 + 0xB0u, 0x70003030u);
        if (u8(r, a0 + 7u) == 0 && (s16(r, a1 + 0xF4u) & 0x4000)) {
            if (u8(r, a1 + 0xE1u) == 0)
                st32(r, PLAYER + 0x224u, u8(r, 0x0081070Au) == 0 ? 0x41200000u : 0x41700000u);
            else
                st32(r, PLAYER + 0x224u, u8(r, 0x0081070Au) == 0 ? 0x41900000u : 0x41B00000u);
            vec4(r, 0x700038A0u, F_ZERO, F_ONE, 0x40400000u, F_ONE);
            CALL3(FN_MAT_VEC, 0x700038A0u, 0x70003000u, 0x700038A0u);
            if (k >= 4)
                f = FCALL1(FN_1B1470, fadd(F_PI, w32(r, PLAYER + 0xC4u)));
            else
                f = w32(r, PLAYER + 0xC4u);
            vec4(r, 0x700038B0u, F_ZERO, f, F_ZERO, F_ONE);
            st32(r, PLAYER + 0x70u, FCALL1(FN_11E2A8, f));
            st32(r, PLAYER + 0x74u, 0);
            st32(r, PLAYER + 0x78u, FCALL1(FN_11DE90, f));
            st32(r, PLAYER + 0x7Cu, F_ONE);
            CALL3(FN_1EFD90, 0x80000006u, 0x700038A0u, 0x700038B0u);
            st8(r, a0 + 7u, 1);
            sound_1FBD50(r, sp, a0, 0x1B2u);
        }
        t = (int16_t)(s16(r, a1 + 0xD0u) - 1);
        st16(r, a1 + 0xD0u, (uint16_t)t);
        if (t == 0) {
            st8(r, a0 + 5u, 7);
            st8(r, a0 + 6u, 0);
            st8(r, a0 + 7u, 0);
        }
    }
    if (u8(r, 0x008106BCu) == 0 && u8(r, 0x70003B8Du) == 0)
        return;
    st8(r, a0 + 5u, 0xA);
    st8(r, a0 + 6u, 0);
    st8(r, a0 + 7u, 0);
}

/* ------------------------------------------------------------------------
 * 0012D240(a0, a1). Frame 0x50 (NEARMISS C; the instructions decide). P
 * is the record 0x008102B0.
 * D_008106BC or the byte 0x70003B8D non-zero: +5 = 0xA, +6 = +7 = 0, done.
 * By byte +6: other than 0 / 1: done. 0: +6 += 1, 001287F0(a0, a1, 0x19;
 * 0), halfword a1 +0xD0 = 0xF0, a1 +0xD4 = 1.0, then as 1.
 * 1: attach_pose(0x70003000) with k = halfword a1 +0xF6 (read here);
 * 001031E0(a0 + 0xB0, 0x70003030); 00102958(0x70003400, 0x70003000);
 * 001C69A0(a0); 0012DE90(a1). a1 +0xD4 < 2.0: done. Otherwise
 * 001FBD50(a0, 0x1B0, 0; 300); P byte +0 |= 2; P byte +0xF = 1; P +0x22C =
 * 40.0 (D_0081070A zero), else 50.0 (a1 +0xE1 zero) or 55.0; scratch
 * 0x700038A0 = (0, 1.0, 1.0, 1.0) turned by 0x70003000; f = 001B1470(pi +
 * P +0xC4) when halfword a1 +0xF6 & 7 >= 4, else P +0xC4; 0x700038B0 =
 * (0, f, 0, 1.0); P +0x70 = 0011E2A8(f), +0x74 = 0, +0x78 = 0011DE90(f),
 * +0x7C = 1.0; 001EFD90(0x80000009, 0x700038A0, 0x700038B0); +4 = 3.
 * ---------------------------------------------------------------------- */
static void f_12D240(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x50u;
    uint32_t st, f;
    if (u8(r, 0x008106BCu) != 0 || u8(r, 0x70003B8Du) != 0) {
        st8(r, a0 + 5u, 0xA);
        st8(r, a0 + 6u, 0);
        st8(r, a0 + 7u, 0);
        return;
    }
    st = u8(r, a0 + 6u);
    if (st != 1) {
        if (st != 0)
            return;
        st8(r, a0 + 6u, st + 1u);
        anim(r, sp, a0, a1, 0x19, F_ZERO);
        st16(r, a1 + 0xD0u, 0xF0u);
        st32(r, a1 + 0xD4u, F_ONE);
    }
    rot_matrix(r, sp, 0x70003000u, w32(r, a0 + 0xC0u), w32(r, a0 + 0xC4u), w32(r, a0 + 0xC8u));
    CALL2(FN_1031E0, 0x70003030u, a1 + 0x30u);
    f = attach_matrix(r, (uint32_t)s16(r, a1 + 0xF6u));
    CALL3(FN_1026D0, 0x70003000u, f, 0x70003000u);
    CALL2(FN_1031E0, a0 + 0xB0u, 0x70003030u);
    CALL2(FN_COPY_QW4, 0x70003400u, 0x70003000u);
    CALL1(FN_1C69A0, a0);
    f_12DE90(r, sp, a1);
    if (flt(w32(r, a1 + 0xD4u), F_TWO))
        return;
    sound_1FBD50(r, sp, a0, 0x1B0u);
    st8(r, PLAYER, u8(r, PLAYER) | 2u);
    st8(r, PLAYER + 0xFu, 1);
    if (u8(r, a1 + 0xE1u) == 0)
        st32(r, PLAYER + 0x22Cu, u8(r, 0x0081070Au) != 0 ? 0x42480000u : 0x42200000u);
    else
        st32(r, PLAYER + 0x22Cu, u8(r, 0x0081070Au) != 0 ? 0x425C0000u : 0x42200000u);
    vec4(r, 0x700038A0u, F_ZERO, F_ONE, F_ONE, F_ONE);
    CALL3(FN_MAT_VEC, 0x700038A0u, 0x70003000u, 0x700038A0u);
    if (((uint32_t)s16(r, a1 + 0xF6u) & 7u) >= 4u) {
        f = FCALL1(FN_1B1470, fadd(F_PI, w32(r, PLAYER + 0xC4u)));
        vec4(r, 0x700038B0u, F_ZERO, f, F_ZERO, F_ONE);
    } else {
        f = w32(r, PLAYER + 0xC4u);
        vec4(r, 0x700038B0u, F_ZERO, f, F_ZERO, F_ONE);
    }
    st32(r, PLAYER + 0x70u, FCALL1(FN_11E2A8, f));
    st32(r, PLAYER + 0x74u, 0);
    st32(r, PLAYER + 0x78u, FCALL1(FN_11DE90, f));
    st32(r, PLAYER + 0x7Cu, F_ONE);
    CALL3(FN_1EFD90, 0x80000009u, 0x700038A0u, 0x700038B0u);
    st8(r, a0 + 4u, 3);
}

/* ------------------------------------------------------------------------
 * 0012D850(a0, a1). Frame 0x40 (word assembly). busy = 001C2770(a0, a1,
 * 2) (kept whole). By byte +6:
 *  0  +6 = 1 (stored before the draw); halfword a1 +0xD0 = (00122BB8() &
 *     0x30) + 0x3C; a1 +0xD8 = 0.6; 001287F0(a0, a1, 6; 0).
 *  1  halfword a1 +0xD0 -= 1; at 0: 00102948(a1 + 0x50, a0 + 0xB0), a1
 *     +0xEC = 1.0, a1 +0xD8 = 0, +5 = 0.
 *  other: nothing.
 * Tail: busy zero: 001C3D60(a0, a1).
 * ---------------------------------------------------------------------- */
static void f_12D850(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x40u;
    uint64_t busy = CALL3(FN_1C2770, a0, a1, 2);
    uint32_t st = u8(r, a0 + 6u);
    if (st == 1) {
        int16_t t = (int16_t)(s16(r, a1 + 0xD0u) - 1);
        st16(r, a1 + 0xD0u, (uint16_t)t);
        if (t == 0) {
            CALL2(FN_COPY_QW, a1 + 0x50u, a0 + 0xB0u);
            st32(r, a1 + 0xECu, F_ONE);
            st32(r, a1 + 0xD8u, 0);
            st8(r, a0 + 5u, 0);
        }
    } else if (st == 0) {
        st8(r, a0 + 6u, st + 1u);
        st16(r, a1 + 0xD0u, ((uint32_t)CALL1(FN_RAND, st) & 0x30u) + 0x3Cu);
        st32(r, a1 + 0xD8u, 0x3F19999Au);
        anim(r, sp, a0, a1, 6, F_ZERO);
    }
    if (busy == 0)
        CALL2(FN_1C3D60, a0, a1);
}

/* ------------------------------------------------------------------------
 * 00198CE0(e, a1). Frame 0x20. By byte e +1:
 *  0  e +1 = 1, e +2 = 0, then as 1.
 *  1  the word a1 +0x230 == 0x11: e +6 = 0xA, e +1 = 0. Then
 *     0018C4B0(e + 0x20; a1 +0xB4 + e +0x8C, 0.2) and
 *     0018C4B0(0x008105E0; e +0x24, 0.1).
 *  other: nothing.
 * ---------------------------------------------------------------------- */
static void f_198CE0(Run *r, uint32_t sp0, uint32_t e, uint32_t a1)
{
    uint32_t sp = sp0 - 0x20u;
    uint32_t st = u8(r, e + 1u);
    if (st > 1u)
        return;
    if (st == 0) {
        st8(r, e + 1u, st + 1u);
        st8(r, e + 2u, 0);
    }
    if (w32(r, a1 + 0x230u) == 0x11u) {
        st8(r, e + 6u, 0xA);
        st8(r, e + 1u, 0);
    }
    callg(r, sp, FN_18C4B0, 1, reg(e + 0x20u), 0, 0, 0, 2, fadd(w32(r, a1 + 0xB4u), w32(r, e + 0x8Cu)),
          0x3E4CCCCDu, 0);
    callg(r, sp, FN_18C4B0, 1, reg(0x008105E0u), 0, 0, 0, 2, w32(r, e + 0x24u), 0x3DCCCCCDu, 0);
}

/* ------------------------------------------------------------------------
 * 00198F10(e, a1). Frame 0x30 (NEARMISS C; the instructions decide). By
 * byte e +1:
 *  0  001029C0(0x70003400); 00102C58(0x70003400, 0x70003400, a1 + 0xC0).
 *     Byte a1 +0x0D == 2: 00102948(e + 0x20, a1 + 0xB0), scratch
 *     0x70003600 = (0, 0, -35.0, 0). == 1: 00102948(e + 0x20, a1 + 0xA0),
 *     e +0x24 -= 14.0, scratch = (0, 0, 35.0, 0). Else 00102948(e + 0x20,
 *     a1 + 0xB0), scratch = (0, 0, 35.0, 0). Then 001026A0(e + 0x10,
 *     0x70003400, 0x70003600); 001028B8(e + 0x10, e + 0x20, e + 0x10); e
 *     +0x14 += 6.0; e +1 += 1; e +2 = 0; 00102948(0x008105E0, e + 0x20);
 *     00102948(0x008105D0, e + 0x10); then as 1.
 *  1  the word a1 +0x230 != 0x12: e +6 = 0, e +1 = 0, then the tail. Else
 *     D_00810700 == 8 and D_00810701 == 3 and dx*dx + dz*dz < 64.0 (dx =
 *     a1 +0xA0 - 123.5, dz = a1 +0xA8 - 156.4; the first product into the
 *     accumulator, the second added to it): e +0x10 = 148.6, +0x18 =
 *     129.2, +0x14 = 282.4, 00191530(e, a1), 00102948(0x008105D0, e +
 *     0x10), done (no tail). Otherwise 0018C6A0(a1 + 0xB0, e + 0x20; 0.2),
 *     0018C4B0(e + 0x20; a1 +0xB4 + e +0x8C, 0.2), 0018C6A0(e + 0x20,
 *     0x008105E0; 0.2), 0018C4B0(0x008105E0; e +0x24, 0.2), then the tail.
 *  other: nothing.
 * Tail: e +0x44 = 001B1240(0x008105D0; D_008105E0, D_008105E8).
 * ---------------------------------------------------------------------- */
static void f_198F10(Run *r, uint32_t sp0, uint32_t e, uint32_t a1)
{
    uint32_t sp = sp0 - 0x30u;
    uint32_t st = u8(r, e + 1u), k;
    if (st > 1u)
        return;
    if (st == 0) {
        CALL1(FN_IDENT, 0x70003400u);
        CALL3(FN_102C58, 0x70003400u, 0x70003400u, a1 + 0xC0u);
        k = u8(r, a1 + 0xDu);
        if (k == 2u) {
            CALL2(FN_COPY_QW, e + 0x20u, a1 + 0xB0u);
            vec4(r, 0x70003600u, F_ZERO, F_ZERO, 0xC20C0000u, F_ZERO);
        } else if (k == 1u) {
            CALL2(FN_COPY_QW, e + 0x20u, a1 + 0xA0u);
            st32(r, e + 0x24u, fsub(w32(r, e + 0x24u), 0x41600000u));
            vec4(r, 0x70003600u, F_ZERO, F_ZERO, 0x420C0000u, F_ZERO);
        } else {
            CALL2(FN_COPY_QW, e + 0x20u, a1 + 0xB0u);
            vec4(r, 0x70003600u, F_ZERO, F_ZERO, 0x420C0000u, F_ZERO);
        }
        CALL3(FN_MAT_VEC, e + 0x10u, 0x70003400u, 0x70003600u);
        CALL3(FN_VADD, e + 0x10u, e + 0x20u, e + 0x10u);
        st32(r, e + 0x14u, fadd(w32(r, e + 0x14u), 0x40C00000u));
        st8(r, e + 1u, u8(r, e + 1u) + 1u);
        st8(r, e + 2u, 0);
        CALL2(FN_COPY_QW, 0x008105E0u, e + 0x20u);
        CALL2(FN_COPY_QW, 0x008105D0u, e + 0x10u);
    }
    if (w32(r, a1 + 0x230u) != 0x12u) {
        st8(r, e + 6u, 0);
        st8(r, e + 1u, 0);
    } else {
        if (u8(r, 0x00810700u) == 8u && u8(r, 0x00810701u) == 3u) {
            uint32_t dx = fsub(w32(r, a1 + 0xA0u), 0x42F70000u);
            uint32_t dz = fsub(w32(r, a1 + 0xA8u), 0x431C6666u);
            uint32_t acc = em_ee_mula_bits(dx, dx);
            if (flt(em_ee_madd_bits(acc, dz, dz), 0x42800000u)) {
                st32(r, e + 0x10u, 0x4314999Au);
                st32(r, e + 0x18u, 0x43013333u);
                st32(r, e + 0x14u, 0x438D3333u);
                CALL2(0x00191530u, e, a1);
                CALL2(FN_COPY_QW, 0x008105D0u, e + 0x10u);
                return;
            }
        }
        callg(r, sp, FN_18C6A0, 2, reg(a1 + 0xB0u), reg(e + 0x20u), 0, 0, 1, 0x3E4CCCCDu, 0, 0);
        callg(r, sp, FN_18C4B0, 1, reg(e + 0x20u), 0, 0, 0, 2, fadd(w32(r, a1 + 0xB4u), w32(r, e + 0x8Cu)),
              0x3E4CCCCDu, 0);
        callg(r, sp, FN_18C6A0, 2, reg(e + 0x20u), reg(0x008105E0u), 0, 0, 1, 0x3E4CCCCDu, 0, 0);
        callg(r, sp, FN_18C4B0, 1, reg(0x008105E0u), 0, 0, 0, 2, w32(r, e + 0x24u), 0x3E4CCCCDu, 0);
    }
    st32(r, e + 0x44u, callg(r, sp, FN_1B1240, 1, reg(0x008105D0u), 0, 0, 0, 2, w32(r, 0x008105E0u),
                             w32(r, 0x008105E8u), 0).f0);
}

/* ------------------------------------------------------------------------
 * 001B7670(a0, a1, a2) -> v0. Leaf; reads only a2. Mode m = word a2 +8:
 * 1: the byte 0x70003B91 = 1 when it is 0. 0: 0x70003B91 = 0 when it is 1.
 * Any other mode: nothing. The result is 1.
 * ---------------------------------------------------------------------- */
static int32_t f_1B7670(Run *r, uint32_t a2)
{
    uint32_t m = w32(r, a2 + 8u);
    if (m == 1u) {
        if (u8(r, 0x70003B91u) == 0)
            st8(r, 0x70003B91u, 1);
    } else if (m == 0) {
        if (u8(r, 0x70003B91u) == 1u)
            st8(r, 0x70003B91u, 0);
    }
    return 1;
}

/* ------------------------------------------------------------------------
 * 001B7700(a0, a1, a2) -> v0. Leaf; a0 is not read. Mode m = word a2 +8;
 * s = byte a1 +4.
 * m == 1: s 0: +4 = 1. s 1: D_008106CE = 1, D_00275BD8 = 1, D_008106CF =
 *   byte a2 +0x14, +4 (read again) += 1. s 2: result 1 when D_00275BD8 is
 *   0. Result 0 otherwise.
 * m == 2 or 0: s 0: +4 = 1. s 1: D_00275BD8 = 1; D_008106CE = 2 when the
 *   word a2 +8 (read again) is 2, else 1; D_008106CF = byte a2 +0x14 +
 *   0x80; +4 (read again) += 1. s 2: result 1 when D_00275BD8 is 0.
 *   Result 0 otherwise.
 * Other modes: 0.
 * ---------------------------------------------------------------------- */
static int32_t f_1B7700(Run *r, uint32_t a1, uint32_t a2)
{
    uint32_t m = w32(r, a2 + 8u), s;
    if (m == 1u) {
        s = u8(r, a1 + 4u);
        if (s == 2u)
            return u8(r, 0x00275BD8u) == 0 ? 1 : 0;
        if (s == 1u) {
            st8(r, 0x008106CEu, 1);
            st8(r, 0x00275BD8u, 1);
            st8(r, 0x008106CFu, u8(r, a2 + 0x14u));
            st8(r, a1 + 4u, u8(r, a1 + 4u) + 1u);
        } else if (s == 0) {
            st8(r, a1 + 4u, s + 1u);
        }
        return 0;
    }
    if (m != 2u && m != 0)
        return 0;
    s = u8(r, a1 + 4u);
    if (s == 2u)
        return u8(r, 0x00275BD8u) == 0 ? 1 : 0;
    if (s == 1u) {
        st8(r, 0x00275BD8u, 1);
        st8(r, 0x008106CEu, w32(r, a2 + 8u) == 2u ? 2u : 1u);
        st8(r, 0x008106CFu, u8(r, a2 + 0x14u) + 0x80u);
        st8(r, a1 + 4u, u8(r, a1 + 4u) + 1u);
    } else if (s == 0) {
        st8(r, a1 + 4u, s + 1u);
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B8AB0(a0, a1, a2) -> v0. Frame 0x30 (NEARMISS C; the instructions
 * decide). a0 is not read. Mode m = word a2 +8 (0, 1 or 2; others return
 * 0); s = byte a1 +4 (0 or 1; others return 0).
 * s 0: 00102948(a2 + 0x20, 0x008105D0), 00102948(a2 + 0x30, 0x008105E0),
 *   +4 (read again) += 1, a2 +0x10 = 0; then as s 1.
 * s 1: c = a2 +0x10; c not < a2 +0xC: 00102948(0x008105D0, a2 + 0x20),
 *   00102948(0x008105E0, a2 + 0x30), result 1. Else a2 +0x10 = c + 1.0;
 *   when 001281C0(c + 1.0) is odd, scratch 0x70003600 = (x, y, 0, 1.0)
 *   with, by m (R = 00122BB8(), drawn in order, x stored before y's draw):
 *     0: x = float((R & 15) - 8) / 10, y the same with a second draw;
 *     1: x = 0 (stored first); y = float((R & 7) - 4) / 10 when the word
 *        a2 +0x14 is 0, else float((R & 15) - 8) / 10;
 *     2: with n = the word a2 +0x14 read before the draw and n' read after
 *        it: x = float(((R >> 16) * n >> 15) - (n' >> 1)) / 10 (32-bit,
 *        arithmetic shifts), y the same with a second draw;
 *   then 001026A0(0x70003610, 0x00810650, 0x70003600), 001028B8(
 *   0x008105D0, a2 + 0x20, 0x70003610), 001028B8(0x008105E0, a2 + 0x30,
 *   0x70003610). Result 0.
 * ---------------------------------------------------------------------- */
/* float(n) / 10.0 */
static uint32_t tenth(int32_t n) { return fdiv(em_ee_cvt_s_w_bits((uint32_t)n), 0x41200000u); }

static uint32_t shake_draw2(Run *r, uint32_t sp, uint32_t a2)
{
    uint32_t n0 = w32(r, a2 + 0x14u);
    uint32_t v = (uint32_t)CALL0(FN_RAND);
    uint32_t n1 = w32(r, a2 + 0x14u);
    uint32_t p = (uint32_t)((int32_t)v >> 16) * n0;
    return tenth((int32_t)((uint32_t)((int32_t)p >> 15) - (uint32_t)((int32_t)n1 >> 1)));
}

static int32_t f_1B8AB0(Run *r, uint32_t sp0, uint32_t a1, uint32_t a2)
{
    uint32_t sp = sp0 - 0x30u;
    uint32_t m = w32(r, a2 + 8u), s, c;
    if (m > 2u)
        return 0;
    s = u8(r, a1 + 4u);
    if (s > 1u)
        return 0;
    if (s == 0) {
        CALL2(FN_COPY_QW, a2 + 0x20u, 0x008105D0u);
        CALL2(FN_COPY_QW, a2 + 0x30u, 0x008105E0u);
        st8(r, a1 + 4u, u8(r, a1 + 4u) + 1u);
        st32(r, a2 + 0x10u, 0);
    }
    c = w32(r, a2 + 0x10u);
    if (!flt(c, w32(r, a2 + 0xCu))) {
        CALL2(FN_COPY_QW, 0x008105D0u, a2 + 0x20u);
        CALL2(FN_COPY_QW, 0x008105E0u, a2 + 0x30u);
        return 1;
    }
    c = fadd(c, F_ONE);
    st32(r, a2 + 0x10u, c);
    if (!(callg(r, sp, FN_F2I, 0, 0, 0, 0, 0, 1, c, 0, 0).v0 & 1u))
        return 0;
    if (m == 0) {
        st32(r, 0x70003600u, tenth((int32_t)((uint32_t)CALL0(FN_RAND) & 0xFu) - 8));
        st32(r, 0x70003604u, tenth((int32_t)((uint32_t)CALL0(FN_RAND) & 0xFu) - 8));
    } else if (m == 1) {
        uint32_t n = w32(r, a2 + 0x14u);
        st32(r, 0x70003600u, 0);
        if (n == 0)
            st32(r, 0x70003604u, tenth((int32_t)((uint32_t)CALL0(FN_RAND) & 7u) - 4));
        else
            st32(r, 0x70003604u, tenth((int32_t)((uint32_t)CALL0(FN_RAND) & 0xFu) - 8));
    } else {
        st32(r, 0x70003600u, shake_draw2(r, sp, a2));
        st32(r, 0x70003604u, shake_draw2(r, sp, a2));
    }
    st32(r, 0x70003608u, 0);
    st32(r, 0x7000360Cu, F_ONE);
    CALL3(FN_MAT_VEC, 0x70003610u, 0x00810650u, 0x70003600u);
    CALL3(FN_VADD, 0x008105D0u, a2 + 0x20u, 0x70003610u);
    CALL3(FN_VADD, 0x008105E0u, a2 + 0x30u, 0x70003610u);
    return 0;
}

/* ------------------------------------------------------------------------
 * 001E7310(p). Frame 0x20. By byte p +4:
 *  0  +4 = 1, p +0x60 = 0.5, p +0x64 = 64.0; 001D2830(9, 1);
 *     001DEE80(9, 0x0026E970); then as 1.
 *  1  001DEEC0(9, 001281C0(p +0x64)); 001DF5A0(0.2 * p +0x60); p +0x64 =
 *     p +0x64 * 0.98; x = p +0x60: p +0x60 = x + (0.05 * -x - 0.015); not
 *     above 0 (the new value <= 0): +4 = 3.
 *  2, 3  001D2830(9, 0); 001AFC10(p).
 *  other: nothing.
 * ---------------------------------------------------------------------- */
static void f_1E7310(Run *r, uint32_t sp0, uint32_t p)
{
    uint32_t sp = sp0 - 0x20u;
    uint32_t st = u8(r, p + 4u), x, v;
    if (st == 2u || st == 3u) {
        CALL2(0x001D2830u, 9u, 0u);
        CALL1(0x001AFC10u, p);
        return;
    }
    if (st > 1u)
        return;
    if (st == 0) {
        st8(r, p + 4u, 1);
        st32(r, p + 0x60u, 0x3F000000u);
        st32(r, p + 0x64u, 0x42800000u);
        CALL2(0x001D2830u, 9u, 1u);
        CALL2(0x001DEE80u, 9u, 0x0026E970u);
    }
    v = (uint32_t)callg(r, sp, FN_F2I, 0, 0, 0, 0, 0, 1, w32(r, p + 0x64u), 0, 0).v0;
    CALL2(0x001DEEC0u, 9u, v);
    FCALL1(0x001DF5A0u, fmul(0x3E4CCCCDu, w32(r, p + 0x60u)));
    st32(r, p + 0x64u, fmul(w32(r, p + 0x64u), 0x3F7AE148u));
    x = w32(r, p + 0x60u);
    x = fadd(x, fsub(fmul(0x3D4CCCCDu, em_ee_neg_bits(x)), 0x3C75C28Fu));
    st32(r, p + 0x60u, x);
    if (fle(x, F_ZERO))
        st8(r, p + 4u, 3);
}

/* ------------------------------------------------------------------------
 * 001FF030(a0). Leaf with a tail jump. When a0 & 0x80: byte (0x00810730 +
 * D_00810700) = the low byte of a0. D_00810701 = a0 & 0x7F; D_00275BD8 =
 * 1; then 001FF080(0, 0x1D) as a tail call (same stack pointer).
 * ---------------------------------------------------------------------- */
static void f_1FF030(Run *r, uint32_t sp, uint32_t a0)
{
    if (a0 & 0x80u)
        st8(r, 0x00810730u + u8(r, 0x00810700u), a0 & 0xFFu);
    st8(r, 0x00810701u, a0 & 0x7Fu);
    st8(r, 0x00275BD8u, 1);
    CALL2(0x001FF080u, 0u, 0x1Du);
}

/* ------------------------------------------------------------------------
 * Entry points
 * ---------------------------------------------------------------------- */

#define ENTER(fn)                                                                                            \
    Run run;                                                                                                 \
    Run *r = &run;                                                                                           \
    if (!s)                                                                                                  \
        return -1;                                                                                           \
    if (s->fault != EM_AREA00_LOW_FAULT_NONE)                                                                \
        return -1;                                                                                           \
    r->s = s;                                                                                                \
    if (setjmp(r->out)) {                                                                                    \
        s->fault_function = (fn);                                                                            \
        return -1;                                                                                           \
    }

#define NEED_OUT(fn)                                                                                         \
    if (!out) {                                                                                              \
        s->fault = EM_AREA00_LOW_FAULT_NULL;                                                                 \
        s->fault_function = (fn);                                                                            \
        s->fault_address = 0;                                                                                \
        return -1;                                                                                           \
    }

int em_area00_low_001000C0(EmArea00Low *s, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, int32_t *out)
{
    ENTER(0x001000C0u)
    NEED_OUT(0x001000C0u)
    *out = f_1000C0(r, s->sp, a0, a1, a2, a3);
    return 0;
}

int em_area00_low_00102870(EmArea00Low *s, uint32_t a0, uint32_t a1, uint32_t f12)
{
    ENTER(0x00102870u)
    f_102870(r, a0, a1, f12);
    return 0;
}

int em_area00_low_00102990(EmArea00Low *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x00102990u)
    f_102990(r, a0, a1);
    return 0;
}

int em_area00_low_001181B0(EmArea00Low *s, uint32_t ev)
{
    ENTER(0x001181B0u)
    f_1181B0(r, s->sp, ev);
    return 0;
}

int em_area00_low_00119080(EmArea00Low *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, int32_t *out)
{
    ENTER(0x00119080u)
    NEED_OUT(0x00119080u)
    *out = f_119080(r, a0, a1, a2, a3);
    return 0;
}

int em_area00_low_00128830(EmArea00Low *s, uint32_t a0, uint32_t f12, uint32_t f13, uint32_t f14)
{
    ENTER(0x00128830u)
    f_128830(r, s->sp, a0, f12, f13, f14);
    return 0;
}

int em_area00_low_001288D0(EmArea00Low *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001288D0u)
    (void)a0;
    f_1288D0(r, s->sp, a1);
    return 0;
}

#define ENTRY2(name, addr, fn)                                                                               \
    int name(EmArea00Low *s, uint32_t a0, uint32_t a1)                                                       \
    {                                                                                                        \
        ENTER(addr)                                                                                          \
        fn(r, s->sp, a0, a1);                                                                                \
        return 0;                                                                                            \
    }

ENTRY2(em_area00_low_00129F00, 0x00129F00u, f_129F00)
ENTRY2(em_area00_low_00129FC0, 0x00129FC0u, f_129FC0)
ENTRY2(em_area00_low_0012B410, 0x0012B410u, f_12B410)
ENTRY2(em_area00_low_0012B970, 0x0012B970u, f_12B970)
ENTRY2(em_area00_low_0012BE20, 0x0012BE20u, f_12BE20)
ENTRY2(em_area00_low_0012C490, 0x0012C490u, f_12C490)
ENTRY2(em_area00_low_0012CAA0, 0x0012CAA0u, f_12CAA0)
ENTRY2(em_area00_low_0012D240, 0x0012D240u, f_12D240)
ENTRY2(em_area00_low_0012D850, 0x0012D850u, f_12D850)
ENTRY2(em_area00_low_00198CE0, 0x00198CE0u, f_198CE0)
ENTRY2(em_area00_low_00198F10, 0x00198F10u, f_198F10)

int em_area00_low_0012D580(EmArea00Low *s, uint32_t a0, uint32_t a1, uint64_t a2)
{
    ENTER(0x0012D580u)
    f_12D580(r, s->sp, a0, a1, a2);
    return 0;
}

int em_area00_low_0012DE90(EmArea00Low *s, uint32_t a0)
{
    ENTER(0x0012DE90u)
    f_12DE90(r, s->sp, a0);
    return 0;
}

int em_area00_low_0012E070(EmArea00Low *s, uint32_t a0)
{
    ENTER(0x0012E070u)
    f_12E070(r, a0);
    return 0;
}

int em_area00_low_0012E0B0(EmArea00Low *s, uint32_t a0, uint32_t a1, int32_t *out)
{
    ENTER(0x0012E0B0u)
    NEED_OUT(0x0012E0B0u)
    *out = f_12E0B0(r, s->sp, a0, a1);
    return 0;
}

int em_area00_low_0012E260(EmArea00Low *s, uint32_t a0)
{
    ENTER(0x0012E260u)
    f_12E260(r, s->sp, a0);
    return 0;
}

int em_area00_low_0012E2C0(EmArea00Low *s, uint32_t a0, uint32_t a1, int32_t *out)
{
    ENTER(0x0012E2C0u)
    NEED_OUT(0x0012E2C0u)
    *out = f_12E2C0(r, s->sp, a0, a1);
    return 0;
}

int em_area00_low_001B5360(EmArea00Low *s, uint32_t a0)
{
    ENTER(0x001B5360u)
    f_1B5360(r, s->sp, a0);
    return 0;
}

int em_area00_low_001B7670(EmArea00Low *s, uint32_t a2, int32_t *out)
{
    ENTER(0x001B7670u)
    NEED_OUT(0x001B7670u)
    *out = f_1B7670(r, a2);
    return 0;
}

int em_area00_low_001B7700(EmArea00Low *s, uint32_t a1, uint32_t a2, int32_t *out)
{
    ENTER(0x001B7700u)
    NEED_OUT(0x001B7700u)
    *out = f_1B7700(r, a1, a2);
    return 0;
}

int em_area00_low_001B8AB0(EmArea00Low *s, uint32_t a1, uint32_t a2, int32_t *out)
{
    ENTER(0x001B8AB0u)
    NEED_OUT(0x001B8AB0u)
    *out = f_1B8AB0(r, s->sp, a1, a2);
    return 0;
}

int em_area00_low_001E7310(EmArea00Low *s, uint32_t a0)
{
    ENTER(0x001E7310u)
    f_1E7310(r, s->sp, a0);
    return 0;
}

int em_area00_low_001FC580(EmArea00Low *s, uint32_t a0, int32_t a1)
{
    ENTER(0x001FC580u)
    f_1FC580(r, s->sp, a0, (uint32_t)a1);
    return 0;
}

int em_area00_low_001FF030(EmArea00Low *s, uint32_t a0)
{
    ENTER(0x001FF030u)
    f_1FF030(r, s->sp, a0);
    return 0;
}
