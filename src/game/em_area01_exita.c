/* em_area01_exita.c - AREA01 lane EXITA translations (see em_area01_exita.h
 * and docs/AREA01_EXITA.md). Every routine names its original address; the
 * comments say what the original does, never how it is encoded. Stores are
 * made in the original's order, and a field the original loads again after
 * a call or a store is loaded again here. */
#include "em_area01_exita.h"

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#include "em_ee_float.h"

/* ------------------------------------------------------------------------
 * Fail-stop plumbing
 * ---------------------------------------------------------------------- */

typedef struct {
    EmArea01Exita *s;
    jmp_buf out;
} Run;

static void fault(Run *r, int32_t code, uint32_t address)
{
    if (r->s->fault == EM_AREA01_EXITA_FAULT_NONE) {
        r->s->fault = code;
        r->s->fault_address = address;
    }
    longjmp(r->out, 1);
}

void em_area01_exita_clear_fault(EmArea01Exita *s)
{
    if (!s)
        return;
    s->fault = EM_AREA01_EXITA_FAULT_NONE;
    s->fault_function = 0;
    s->fault_address = 0;
}

/* The byte span [address, address + size) inside one region, or a fault. */
static uint8_t *span(Run *r, uint32_t address, uint32_t size)
{
    const EmArea01Exita *s = r->s;
    unsigned i;
    for (i = 0; i < s->region_count; i++) {
        const EmArea01ExitaRegion *g = &s->regions[i];
        if (g->bytes && address >= g->base && size <= g->size && address - g->base <= g->size - size)
            return g->bytes + (address - g->base);
    }
    fault(r, EM_AREA01_EXITA_FAULT_UNMAPPED, address);
    return NULL;
}

static uint32_t rd(Run *r, uint32_t a, unsigned n)
{
    const uint8_t *p = span(r, a, n);
    uint32_t v = 0;
    unsigned i;
    for (i = 0; i < n; i++)
        v |= (uint32_t)p[i] << (8 * i);
    return v;
}

/* Test hook. A build that defines EM_AREA01_EXITA_STORE_TRACE as a function
 * name reports every store (original address, size) to it once the address
 * is known to be mapped; tools/test_area01_exita_reference.py uses it to
 * compare memory with the original at every call leaving the module.
 * Ordinary builds compile it out. wr is the only store path. */
#ifdef EM_AREA01_EXITA_STORE_TRACE
void EM_AREA01_EXITA_STORE_TRACE(uint32_t address, unsigned size);
#define TRACE_STORE(a, n) EM_AREA01_EXITA_STORE_TRACE((a), (n))
#else
#define TRACE_STORE(a, n) ((void)0)
#endif

static void wr(Run *r, uint32_t a, uint32_t v, unsigned n)
{
    uint8_t *p = span(r, a, n);
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

/* A 32-bit value as the EE holds it in a 64-bit register. */
static uint64_t reg(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

/* ------------------------------------------------------------------------
 * Calls leaving the module
 * ---------------------------------------------------------------------- */

/* One call: `na` integer registers a0.. (register images) and `nf` float
 * registers f12.. (raw bits), as the original sets them. */
static EmArea01ExitaCall callx(Run *r, uint32_t sp, uint32_t fn, unsigned na, const uint64_t *a, unsigned nf,
                               const uint32_t *f)
{
    EmArea01ExitaCall c;
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
        fault(r, EM_AREA01_EXITA_FAULT_NULL, fn);
    if (r->s->call(r->s->ctx, &c) < 0)
        fault(r, EM_AREA01_EXITA_FAULT_WORKER, fn);
    return c;
}

/* Integer-argument call; returns the callee's whole v0. */
static uint64_t calli(Run *r, uint32_t sp, uint32_t fn, unsigned na, uint64_t a0, uint64_t a1, uint64_t a2,
                      uint64_t a3, uint64_t t0)
{
    uint64_t a[5];
    a[0] = a0;
    a[1] = a1;
    a[2] = a2;
    a[3] = a3;
    a[4] = t0;
    return callx(r, sp, fn, na, a, 0, NULL).v0;
}

#define CALL0(fn) calli(r, sp, (fn), 0, 0, 0, 0, 0, 0)
#define CALL1(fn, x) calli(r, sp, (fn), 1, reg(x), 0, 0, 0, 0)
#define CALL2(fn, x, y) calli(r, sp, (fn), 2, reg(x), reg(y), 0, 0, 0)
#define CALL3(fn, x, y, z) calli(r, sp, (fn), 3, reg(x), reg(y), reg(z), 0, 0)

/* Float-argument calls: a0..a(na-1) and f12..f(12+nf-1); v0 or f0. */
static EmArea01ExitaCall callf(Run *r, uint32_t sp, uint32_t fn, unsigned na, uint64_t a0, uint64_t a1,
                               uint64_t a2, unsigned nf, uint32_t f12, uint32_t f13, uint32_t f14)
{
    uint64_t a[3];
    uint32_t f[3];
    a[0] = a0;
    a[1] = a1;
    a[2] = a2;
    f[0] = f12;
    f[1] = f13;
    f[2] = f14;
    return callx(r, sp, fn, na, a, nf, f);
}

/* ------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define F_ZERO 0x00000000u
#define F_ONE  0x3F800000u

#define FN_MAT_VEC   0x001026A0u /* 3 pointers */
#define FN_CROSS     0x00102718u /* 3 pointers */
#define FN_1027E0    0x001027E0u /* 2 pointers */
#define FN_VADD      0x001028B8u /* 3 pointers */
#define FN_VSUB      0x001028D0u /* 3 pointers */
#define FN_COPY_QW   0x00102948u /* 2 pointers */
#define FN_COPY_QW4  0x00102958u /* 2 pointers */
#define FN_IDENT     0x001029C0u /* 1 pointer */
#define FN_102B08    0x00102B08u /* 2 pointers, f12 */
#define FN_102BB0    0x00102BB0u /* 2 pointers, f12 */
#define FN_1031E0    0x001031E0u /* 2 pointers */
#define FN_VSCALE    0x00103230u /* 2 pointers, f12 */
#define FN_11E748    0x0011E748u /* f12 -> f0 */
#define FN_RAND      0x00122BB8u /* -> v0 */
#define FN_1281C0    0x001281C0u /* f12 -> v0 */
#define FN_1287F0    0x001287F0u
#define FN_1B13F0    0x001B13F0u
#define FN_1B1470    0x001B1470u /* f12 -> f0 */
#define FN_1B12B0    0x001B12B0u /* f12, f13, f14 -> f0 */
#define FN_1C2770    0x001C2770u
#define FN_1C3D60    0x001C3D60u
#define FN_1C63E0    0x001C63E0u
#define FN_1C69A0    0x001C69A0u

#define SPR 0x70000000u

/* ------------------------------------------------------------------------
 * 00113478() -> v0. Frame 0x40 (ee-gcc).
 *  1. 00112E28(0x1E); a zero v0 (all 64 bits) returns 0.
 *  2. The word 0x00241D48 = 8.
 *  3. 0010E8A8 with a0..t3 = (0x0027AF60, 0x16, 0, 0, 0, 0x0027AB40, 4, 0)
 *     and the ninth argument, the word 0, at the stack pointer of the call.
 *  4. A negative v0 (64-bit sign): 0010B840(word D_00241D0C), then the
 *     word 0x00241D48 = 0, and the result is 0.
 *  5. Otherwise the word 0x00241D48 = 0 first, then 0010B840(word
 *     D_00241D0C), and the result is the word at 0x0027AB40 (read through
 *     the uncached mirror 0x2027AB40, which is RAM 0x0027AB40).
 * ---------------------------------------------------------------------- */
static int32_t f_113478(Run *r, uint32_t sp0)
{
    uint32_t sp = sp0 - 0x40u;
    uint64_t a[8];
    int64_t v;
    if (CALL1(0x00112E28u, 0x1Eu) == 0)
        return 0;
    st32(r, 0x00241D48u, 8);
    st32(r, sp, 0); /* the ninth argument */
    a[0] = reg(0x0027AF60u);
    a[1] = reg(0x16u);
    a[2] = 0;
    a[3] = 0;
    a[4] = 0;
    a[5] = reg(0x0027AB40u);
    a[6] = reg(4u);
    a[7] = 0;
    v = (int64_t)callx(r, sp, 0x0010E8A8u, 8, a, 0, NULL).v0;
    if (v < 0) {
        CALL1(0x0010B840u, w32(r, 0x00241D0Cu));
        st32(r, 0x00241D48u, 0);
        return 0;
    }
    st32(r, 0x00241D48u, 0);
    CALL1(0x0010B840u, w32(r, 0x00241D0Cu));
    return (int32_t)w32(r, (0x0027AB40u | 0x20000000u) - 0x20000000u);
}

/* ------------------------------------------------------------------------
 * 001195A8(a0) -> v0. Frame 0x10 (ee-gcc).
 * Returns -1 unless the register a0 (as an unsigned 64-bit value) is below
 * 0x80 and the word at 0x0027C6C0 + a0 * 12 is 1. Then it walks the 48
 * records of 0x6A bytes from 0x0027CCC0: a record whose halfword +0 is 1
 * and whose halfword +0x22 equals a0 returns -1. When none does, it calls
 * 00121A28(0x0027C6C0 + a0 * 12, 0, 12) and returns 0.
 * ---------------------------------------------------------------------- */
static int32_t f_1195A8(Run *r, uint32_t sp0, uint64_t a0)
{
    uint32_t sp = sp0 - 0x10u;
    uint32_t entry, p;
    if (!(a0 < 0x80u))
        return -1;
    entry = 0x0027C6C0u + (uint32_t)a0 * 12u;
    if ((int32_t)w32(r, entry) != 1)
        return -1;
    p = 0x0027CCC0u;
    do {
        if (u16(r, p) == 1 && (uint64_t)u16(r, p + 0x22) == a0)
            return -1;
        p += 0x6Au;
    } while ((int32_t)p < (int32_t)(0x0027CCC0u + 0x13E0u));
    CALL3(0x00121A28u, entry, 0, 12);
    return 0;
}

/* ------------------------------------------------------------------------
 * 00128390(a0, a1) -> v0. Leaf; a0 is not read.
 * D_0081070A zero: 30 when the register a1 is non-zero, else 15.
 * Otherwise: 50 when a1 is non-zero, else 30.
 * ---------------------------------------------------------------------- */
static int32_t f_128390(Run *r, uint64_t a1)
{
    if (u8(r, 0x0081070Au) == 0)
        return a1 != 0 ? 0x1E : 0x0F;
    return a1 != 0 ? 0x32 : 0x1E;
}

/* ------------------------------------------------------------------------
 * 00128600(a0) -> v0. Frame 0x20.
 * The byte at 0x00242ED0 + (a0 << 4) + (00122BB8() & 15).
 * ---------------------------------------------------------------------- */
static uint32_t f_128600(Run *r, uint32_t sp0, uint32_t a0)
{
    uint32_t sp = sp0 - 0x20u;
    uint32_t k = (uint32_t)CALL0(FN_RAND) & 0xFu;
    return u8(r, k + (0x00242ED0u + (a0 << 4)));
}

/* ------------------------------------------------------------------------
 * 00128640(p) -> v0. Frame 0x20.
 * Returns 0 when the byte 0x70003B8D is non-zero, the halfword D_0028A9A0
 * is non-zero, or the word D_008104E0 is 0x28. Otherwise:
 *  - 001028D0(0x70003600, 0x00810350, p + 0xB0), then d = 0011E748 of
 *    x*x + y*y + z*z over the three words at 0x70003600 (the two first
 *    products summed into the accumulator, the third added to it);
 *  - d above 70.0 (not d <= 70.0): returns 0;
 *  - d < 30.1: k = 00128600(4); +5 = 2 (k 0), 3 (k 1), else 5;
 *  - else d < 60.1: k = 00128600(0); +5 = 2 (k 0), 4 (k 1), else 5;
 *  - else +5 = 2;
 *  then +6 = 0, +7 = 0 and the result is 1.
 * ---------------------------------------------------------------------- */
static int32_t f_128640(Run *r, uint32_t sp0, uint32_t p)
{
    uint32_t sp = sp0 - 0x20u;
    uint32_t x, y, z, acc, d, k;
    if (u8(r, 0x70003B8Du) != 0)
        return 0;
    if (s16(r, 0x0028A9A0u) != 0)
        return 0;
    if (w32(r, 0x008104E0u) == 0x28u)
        return 0;
    CALL3(FN_VSUB, 0x70003600u, 0x00810350u, p + 0xB0u);
    x = w32(r, 0x70003600u);
    y = w32(r, 0x70003604u);
    x = em_ee_mul_bits(x, x);
    y = em_ee_mul_bits(y, y);
    z = w32(r, 0x70003608u);
    acc = em_ee_adda_bits(x, y);
    d = callf(r, sp, FN_11E748, 0, 0, 0, 0, 1, em_ee_madd_bits(acc, z, z), 0, 0).f0;
    if (!em_ee_c_le_bits(d, 0x428C0000u)) /* 70.0 */
        return 0;
    if (em_ee_c_lt_bits(d, 0x41F0CCCDu)) { /* 30.1 */
        k = f_128600(r, sp, 4);
        st8(r, p + 5, k == 0 ? 2 : k == 1 ? 3 : 5);
    } else if (em_ee_c_lt_bits(d, 0x42706666u)) { /* 60.1 */
        k = f_128600(r, sp, 0);
        st8(r, p + 5, k == 0 ? 2 : k == 1 ? 4 : 5);
    } else {
        st8(r, p + 5, 2);
    }
    st8(r, p + 6, 0);
    st8(r, p + 7, 0);
    return 1;
}

/* ------------------------------------------------------------------------
 * 001289C0(a0, a1). Frame 0x30.
 *  a1: +0xEC = 1.0, +0xD8 = +0xE4 = +0xDC = +0x60 = 0, +0x64 = -0.4,
 *      +0x68 = 0, +0x6C = 1.0, +0x70 = +0x74 = 0, +0x78 = +0x7C = 1.0,
 *      +0x80 = 0, +0x84 = 1.0, +0x88 = 0, +0x8C = 1.0;
 *  001029C0(a0 + 0xD0);
 *  a0: +0x30 = 0x00275668, byte +0x0B = 0, halfword +0x34 =
 *      00128390(a0, a1 byte +0xE1 != 0), halfword +0x36 = 0;
 *  a1: bytes +0xFA = +0xFB = 0, halfword +0xF6 = 0;
 *  a0: +0x58 = the word D_0028A4C8, +0x80..+0x8C = 1.0, byte +0x0A = 0.
 * ---------------------------------------------------------------------- */
static void f_1289C0(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x30u;
    st32(r, a1 + 0xECu, F_ONE);
    st32(r, a1 + 0xD8u, 0);
    st32(r, a1 + 0xE4u, 0);
    st32(r, a1 + 0xDCu, 0);
    st32(r, a1 + 0x60u, 0);
    st32(r, a1 + 0x64u, 0xBECCCCCDu);
    st32(r, a1 + 0x68u, 0);
    st32(r, a1 + 0x6Cu, F_ONE);
    st32(r, a1 + 0x70u, 0);
    st32(r, a1 + 0x74u, 0);
    st32(r, a1 + 0x78u, F_ONE);
    st32(r, a1 + 0x7Cu, F_ONE);
    st32(r, a1 + 0x80u, 0);
    st32(r, a1 + 0x84u, F_ONE);
    st32(r, a1 + 0x88u, 0);
    st32(r, a1 + 0x8Cu, F_ONE);
    CALL1(FN_IDENT, a0 + 0xD0u);
    st32(r, a0 + 0x30u, 0x00275668u);
    st8(r, a0 + 0xBu, 0);
    st16(r, a0 + 0x34u, (uint32_t)f_128390(r, u8(r, a1 + 0xE1u) != 0 ? 1u : 0u));
    st16(r, a0 + 0x36u, 0);
    st8(r, a1 + 0xFAu, 0);
    st8(r, a1 + 0xFBu, 0);
    st16(r, a1 + 0xF6u, 0);
    st32(r, a0 + 0x58u, w32(r, 0x0028A4C8u));
    st32(r, a0 + 0x80u, F_ONE);
    st32(r, a0 + 0x84u, F_ONE);
    st32(r, a0 + 0x88u, F_ONE);
    st32(r, a0 + 0x8Cu, F_ONE);
    st8(r, a0 + 0xAu, 0);
}

/* ------------------------------------------------------------------------
 * 00128AB0(a0, a1) -> v0. Frame 0x30.
 * D_00810788 is 0xFF: 001B10B0(a0, 0x10, 0x11) non-zero returns 0, else
 * a1 byte +0xE1 = 1. Otherwise 001B10B0(a0, 0x0F, 0x11) non-zero returns 0,
 * else +0xE1 = 0. Then when a0 byte +0x0D has bit 7: a1 byte +0xE0 = 1 and
 * +0x0D (read again) loses bit 7; else +0xE0 = 0. Then 001289C0(a0, a1),
 * 00102948(a1 + 0x50, a0 + 0xB0), result 1.
 * ---------------------------------------------------------------------- */
static int32_t f_128AB0(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x30u;
    if (u8(r, 0x00810788u) == 0xFFu) {
        if (CALL3(0x001B10B0u, a0, 0x10u, 0x11u) != 0)
            return 0;
        st8(r, a1 + 0xE1u, 1);
    } else {
        if (CALL3(0x001B10B0u, a0, 0x0Fu, 0x11u) != 0)
            return 0;
        st8(r, a1 + 0xE1u, 0);
    }
    if (u8(r, a0 + 0xDu) & 0x80u) {
        st8(r, a1 + 0xE0u, 1);
        st8(r, a0 + 0xDu, u8(r, a0 + 0xDu) & 0x7Fu);
    } else {
        st8(r, a1 + 0xE0u, 0);
    }
    f_1289C0(r, sp, a0, a1);
    CALL2(FN_COPY_QW, a1 + 0x50u, a0 + 0xB0u);
    return 1;
}

/* ------------------------------------------------------------------------
 * 00129780(a, b, sel) -> v0. Frame 0x40. By sel & 0xFF (st starts 0):
 *  0, 1, 5, 6  001C2430(a, b + 0x60, a + 0xD0) non-zero: st = 1; else
 *              a +0xB4 -= 1.0.
 *  2, 7        scratch 0x700038A0 = (-3, 1, 0, 1); 001026A0(0x700038A0,
 *              a + 0xD0, 0x700038A0); its x, y, z += a +0xB0, +0xB4, +0xB8;
 *              0019AD00(a, 0x700038A0, 0x80000007) non-zero: st = 2. Else
 *              the same with (3, 1, 0, 1): st = 2, or a +0xB0 -= 1.0.
 *  3, 4, 8, 9  scratch 0x700038A0 set to (0,-6,0,1), (0,6,0,1), (6,0,0,1),
 *              (-6,0,0,1), (0,0,6,1), (0,0,-6,1) in turn, each followed by
 *              001C2540(a, b + 0x60, 0x700038A0, a + 0xD0); at the first
 *              non-zero one the halfword +0x1A of the record at the word
 *              0x700031D0 gives st = 2 (bits 0x3800), 3 (bit 0x8000) or 1.
 *              None: a +0xB4 -= 1.0.
 *  10, 11      a bytes +4 = 1, +5 = 12, +6 = +7 = 0; b +0x80 = (0,-1,0,1);
 *              scratch 0x70003610 = (0,1,0,1); 001C3DB0(b + 0x80,
 *              0x70003610, b + 0x70, 0x70003620); 001031E0(b + 0x70,
 *              0x70003620); b +0x80 = (0,1,0,1); 001C3BE0(a, b);
 *              00102958(a + 0xD0, 0x70003000); b +0xE4 = 0x500,
 *              +0xD8 = +0xF0 = 0; 00128830(a; 0.0, 5.0, 0.5);
 *              001287F0(a, b, 15; 0.0); a +0xC0 = pi/2; result 1.
 *  12          a bytes +4 = 1, +5 = 13, +6 = +7 = 0; scratch 0x70003610 =
 *              (0,1,0,1); 001C3DB0 and 001031E0 as above; b +0x80 =
 *              (0,1,0,1); 001C3BE0(a, b); 00102958(a + 0xD0, 0x70003000);
 *              00102958(0x70003400, 0x70003000); 001C69A0(a); b +0xE4 =
 *              0x400, +0xF0 = 0; result 1.
 * Then (every other selector too): halfword a +0x28 += 1; when it (read
 * again) exceeds 0x28: +0x28 = 0, +4 = 3, +5 = 0, b byte +0xE0 = 0, result
 * 0. Else st 0 returns 0. Else +4 += 1, +5 = 0; scratch 0x70003610 = the
 * record at the word 0x700031D0: its +0x24, +0x28, +0x2C, then 1.0;
 * 001C3DB0 / 001031E0 as above; b +0x80, +0x84, +0x88 = that record's
 * +0x24, +0x28, +0x2C (the pointer read again for each), +0x8C = 1.0;
 * 001C3BE0(a, b); 00102958(a + 0xD0, 0x70003000); 00103230(0x70003600,
 * b + 0x80; -4.0); scratch 0x7000360C = 0; 001028B8(a + 0xB0, a + 0xB0,
 * 0x70003600); scratch 0x700038B0 = (0,-8,0,1); 001C2540(a, b + 0x60,
 * 0x700038B0, a + 0xD0); b +0xE4 = st << 8; result 1.
 * ---------------------------------------------------------------------- */
static void vec4(Run *r, uint32_t a, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    st32(r, a, x);
    st32(r, a + 4, y);
    st32(r, a + 8, z);
    st32(r, a + 12, w);
}

static uint64_t probe_2540(Run *r, uint32_t sp, uint32_t a, uint32_t b, uint32_t x, uint32_t y, uint32_t z)
{
    vec4(r, 0x700038A0u, x, y, z, F_ONE);
    return calli(r, sp, 0x001C2540u, 4, reg(a), reg(b + 0x60u), reg(0x700038A0u), reg(a + 0xD0u), 0);
}

static int probe_ad00(Run *r, uint32_t sp, uint32_t a, uint32_t x)
{
    vec4(r, 0x700038A0u, x, F_ONE, F_ZERO, F_ONE);
    CALL3(FN_MAT_VEC, 0x700038A0u, a + 0xD0u, 0x700038A0u);
    st32(r, 0x700038A0u, em_ee_add_bits(w32(r, 0x700038A0u), w32(r, a + 0xB0u)));
    st32(r, 0x700038A4u, em_ee_add_bits(w32(r, 0x700038A4u), w32(r, a + 0xB4u)));
    st32(r, 0x700038A8u, em_ee_add_bits(w32(r, 0x700038A8u), w32(r, a + 0xB8u)));
    return CALL3(0x0019AD00u, a, 0x700038A0u, 0x80000007u) != 0;
}

static int32_t f_129780(Run *r, uint32_t sp0, uint32_t a, uint32_t b, uint32_t sel)
{
    uint32_t sp = sp0 - 0x40u;
    uint32_t st = 0, flags, cam;
    int32_t t;
    switch (sel & 0xFFu) {
    case 0:
    case 1:
    case 5:
    case 6:
        if (CALL3(0x001C2430u, a, b + 0x60u, a + 0xD0u) != 0)
            st = 1;
        else
            st32(r, a + 0xB4u, em_ee_sub_bits(w32(r, a + 0xB4u), F_ONE));
        break;
    case 2:
    case 7:
        if (probe_ad00(r, sp, a, 0xC0400000u) || probe_ad00(r, sp, a, 0x40400000u))
            st = 2;
        else
            st32(r, a + 0xB0u, em_ee_sub_bits(w32(r, a + 0xB0u), F_ONE));
        break;
    case 3:
    case 4:
    case 8:
    case 9:
        if (probe_2540(r, sp, a, b, F_ZERO, 0xC0C00000u, F_ZERO) != 0 ||
            probe_2540(r, sp, a, b, F_ZERO, 0x40C00000u, F_ZERO) != 0 ||
            probe_2540(r, sp, a, b, 0x40C00000u, F_ZERO, F_ZERO) != 0 ||
            probe_2540(r, sp, a, b, 0xC0C00000u, F_ZERO, F_ZERO) != 0 ||
            probe_2540(r, sp, a, b, F_ZERO, F_ZERO, 0x40C00000u) != 0 ||
            probe_2540(r, sp, a, b, F_ZERO, F_ZERO, 0xC0C00000u) != 0) {
            flags = (uint32_t)s16(r, w32(r, 0x700031D0u) + 0x1Au);
            if (flags & 0x3800u)
                st = 2;
            else if (flags & 0x8000u)
                st = 3;
            else
                st = 1;
        } else {
            st32(r, a + 0xB4u, em_ee_sub_bits(w32(r, a + 0xB4u), F_ONE));
        }
        break;
    case 10:
    case 11:
        st8(r, a + 4, 1);
        st8(r, a + 5, 0xC);
        st8(r, a + 6, 0);
        st8(r, a + 7, 0);
        vec4(r, b + 0x80u, F_ZERO, 0xBF800000u, F_ZERO, F_ONE);
        vec4(r, 0x70003610u, F_ZERO, F_ONE, F_ZERO, F_ONE);
        calli(r, sp, 0x001C3DB0u, 4, reg(b + 0x80u), reg(0x70003610u), reg(b + 0x70u), reg(0x70003620u), 0);
        CALL2(FN_1031E0, b + 0x70u, 0x70003620u);
        vec4(r, b + 0x80u, F_ZERO, F_ONE, F_ZERO, F_ONE);
        CALL2(0x001C3BE0u, a, b);
        CALL2(FN_COPY_QW4, a + 0xD0u, 0x70003000u);
        st32(r, b + 0xE4u, 0x500);
        st32(r, b + 0xD8u, 0);
        st32(r, b + 0xF0u, 0);
        callf(r, sp, 0x00128830u, 1, reg(a), 0, 0, 3, F_ZERO, 0x40A00000u, 0x3F000000u);
        callf(r, sp, FN_1287F0, 3, reg(a), reg(b), reg(0xFu), 1, F_ZERO, 0, 0);
        st32(r, a + 0xC0u, 0x3FC90FDBu);
        return 1;
    case 12:
        st8(r, a + 4, 1);
        st8(r, a + 5, 0xD);
        st8(r, a + 6, 0);
        st8(r, a + 7, 0);
        vec4(r, 0x70003610u, F_ZERO, F_ONE, F_ZERO, F_ONE);
        calli(r, sp, 0x001C3DB0u, 4, reg(b + 0x80u), reg(0x70003610u), reg(b + 0x70u), reg(0x70003620u), 0);
        CALL2(FN_1031E0, b + 0x70u, 0x70003620u);
        vec4(r, b + 0x80u, F_ZERO, F_ONE, F_ZERO, F_ONE);
        CALL2(0x001C3BE0u, a, b);
        CALL2(FN_COPY_QW4, a + 0xD0u, 0x70003000u);
        CALL2(FN_COPY_QW4, 0x70003400u, 0x70003000u);
        CALL1(FN_1C69A0, a);
        st32(r, b + 0xE4u, 0x400);
        st32(r, b + 0xF0u, 0);
        return 1;
    default:
        break;
    }
    st16(r, a + 0x28u, (uint32_t)(s16(r, a + 0x28u) + 1));
    t = s16(r, a + 0x28u);
    if (t > 0x28) {
        st16(r, a + 0x28u, 0);
        st8(r, a + 4, 3);
        st8(r, a + 5, 0);
        st8(r, b + 0xE0u, 0);
        return 0;
    }
    if (st == 0)
        return 0;
    st8(r, a + 4, u8(r, a + 4) + 1);
    st8(r, a + 5, 0);
    cam = w32(r, 0x700031D0u);
    st32(r, 0x70003610u, w32(r, cam + 0x24u));
    st32(r, 0x70003614u, w32(r, cam + 0x28u));
    st32(r, 0x70003618u, w32(r, cam + 0x2Cu));
    st32(r, 0x7000361Cu, F_ONE);
    calli(r, sp, 0x001C3DB0u, 4, reg(b + 0x80u), reg(0x70003610u), reg(b + 0x70u), reg(0x70003620u), 0);
    CALL2(FN_1031E0, b + 0x70u, 0x70003620u);
    st32(r, b + 0x80u, w32(r, w32(r, 0x700031D0u) + 0x24u));
    st32(r, b + 0x84u, w32(r, w32(r, 0x700031D0u) + 0x28u));
    st32(r, b + 0x88u, w32(r, w32(r, 0x700031D0u) + 0x2Cu));
    st32(r, b + 0x8Cu, F_ONE);
    CALL2(0x001C3BE0u, a, b);
    CALL2(FN_COPY_QW4, a + 0xD0u, 0x70003000u);
    callf(r, sp, FN_VSCALE, 2, reg(0x70003600u), reg(b + 0x80u), 0, 1, 0xC0800000u, 0, 0);
    st32(r, 0x7000360Cu, 0);
    CALL3(FN_VADD, a + 0xB0u, a + 0xB0u, 0x70003600u);
    vec4(r, 0x700038B0u, F_ZERO, 0xC1000000u, F_ZERO, F_ONE);
    calli(r, sp, 0x001C2540u, 4, reg(a), reg(b + 0x60u), reg(0x700038B0u), reg(a + 0xD0u), 0);
    st32(r, b + 0xE4u, st << 8);
    return 1;
}

/* ------------------------------------------------------------------------
 * 0012ADC0(a0, a1, a2; f12) -> v0. Frame 0x40. s = a0 + 0x1F0.
 *  - f12 > 0 (not f12 <= 0): 001B13F0(a2, a1; f12) zero returns 0.
 *    Otherwise 001B13F0(a2, a1; -f12) non-zero returns 0. (The original
 *    passes its own a1 through unchanged as the callee's second argument;
 *    the decomp's NEARMISS C declares that callee with one integer
 *    argument, which the compiled call happens to match.)
 *  - 001029C0(0x700036A0); scratch 0x700036C0..C8 = s +0x70..78,
 *    0x700036B0..B8 = s +0x80..88, 0x700038B0..BC = (s +0x80..88, 1.0),
 *    0x700038C0..CC = (s +0x70..78, 1.0) (raw word copies, in this order);
 *  - 00102718(0x700038A0, 0x700038B0, 0x700038C0); scratch 0x700036A0..A8
 *    = 0x700038A0..A8 (all three read, then all three stored);
 *  - 001027E0(0x700036E0, 0x700036A0); 001028D0(0x700038B0, a1, a2);
 *    001026A0(0x700038A0, 0x700036E0, 0x700038B0); scratch 0x700038C0..C8
 *    = 0;
 *  - s +0xE8 = 001B1240(0x700038C0; -x, -z) of the words 0x700038A0 /
 *    0x700038A8; result 1.
 * ---------------------------------------------------------------------- */
static int32_t f_12ADC0(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f12)
{
    uint32_t sp = sp0 - 0x40u;
    uint32_t s = a0 + 0x1F0u;
    uint32_t x, y, z;
    if (em_ee_c_le_bits(f12, F_ZERO)) {
        if (callf(r, sp, FN_1B13F0, 2, reg(a2), reg(a1), 0, 1, em_ee_neg_bits(f12), 0, 0).v0 != 0)
            return 0;
    } else {
        if (callf(r, sp, FN_1B13F0, 2, reg(a2), reg(a1), 0, 1, f12, 0, 0).v0 == 0)
            return 0;
    }
    CALL1(FN_IDENT, 0x700036A0u);
    st32(r, 0x700036C0u, w32(r, s + 0x70u));
    st32(r, 0x700036C4u, w32(r, s + 0x74u));
    st32(r, 0x700036C8u, w32(r, s + 0x78u));
    st32(r, 0x700036B0u, w32(r, s + 0x80u));
    st32(r, 0x700036B4u, w32(r, s + 0x84u));
    st32(r, 0x700036B8u, w32(r, s + 0x88u));
    st32(r, 0x700038B0u, w32(r, s + 0x80u));
    st32(r, 0x700038B4u, w32(r, s + 0x84u));
    st32(r, 0x700038B8u, w32(r, s + 0x88u));
    st32(r, 0x700038BCu, F_ONE);
    st32(r, 0x700038C0u, w32(r, s + 0x70u));
    st32(r, 0x700038C4u, w32(r, s + 0x74u));
    st32(r, 0x700038C8u, w32(r, s + 0x78u));
    st32(r, 0x700038CCu, F_ONE);
    CALL3(FN_CROSS, 0x700038A0u, 0x700038B0u, 0x700038C0u);
    x = w32(r, 0x700038A0u);
    y = w32(r, 0x700038A4u);
    z = w32(r, 0x700038A8u);
    st32(r, 0x700036A0u, x);
    st32(r, 0x700036A4u, y);
    st32(r, 0x700036A8u, z);
    CALL2(FN_1027E0, 0x700036E0u, 0x700036A0u);
    CALL3(FN_VSUB, 0x700038B0u, a1, a2);
    CALL3(FN_MAT_VEC, 0x700038A0u, 0x700036E0u, 0x700038B0u);
    st32(r, 0x700038C0u, 0);
    st32(r, 0x700038C4u, 0);
    st32(r, 0x700038C8u, 0);
    x = w32(r, 0x700038A0u);
    z = w32(r, 0x700038A8u);
    st32(r, s + 0xE8u,
         callf(r, sp, 0x001B1240u, 1, reg(0x700038C0u), 0, 0, 2, em_ee_neg_bits(x), em_ee_neg_bits(z), 0).f0);
    return 1;
}

/* ------------------------------------------------------------------------
 * 0012AFC0(a0, a1). Frame 0x50.
 * mode = 001C2770(a0, a1, byte +6 < 4 ? 1 : 0) (its whole v0). Then by
 * byte +6 (read again):
 *  0  halfword +0x28 = 0, a1 +0xD8 = 0; a1 halfword +0xD0 = 0x78 when
 *     00122BB8() is odd, else 0xF0. 00128600(0) non-zero: 001287F0(a0,
 *     a1, 1; 8.0), then +6 = 3. Else a1 +0xE8 = 001B1470(2pi *
 *     float(00122BB8() & 0xF0) / 256), +6 += 1, then 001287F0(a0, a1, 2;
 *     8.0).
 *  1, 4 (4 after the dwell step) turn: +0xC4 = 001B12B0(a1 +0xE8, +0xC4,
 *     0.06981317); when it equals a1 +0xE8 (read again): +6 += 1, a1
 *     +0xD8 = 0.2.
 *  2..5 dwell step first: 0012ADC0(a0, a0 + 0xB0, 0x00810350; 120.0)
 *     non-zero: halfword +0x28 += 1 and, when it (read again) is 0x5B or
 *     more, 00128640(a0) and nothing else; zero: +0x28 = 0. Then:
 *  2  mode 8: a1 +0xE8 = 001B1470(pi + +0xC4), a1 halfword +0xD0 = 0x28,
 *     +6 = 1, a1 +0xD8 = 0. Else 0012ADC0(a0, a0 + 0xB0, a1 + 0x50;
 *     -30.0) non-zero: +6 = 4, a1 +0xD8 = 0; zero: a1 halfword +0xD0 -= 1,
 *     and at 0: +6 = 0, a1 +0xD8 = 0.
 *  3, 5  a1 halfword +0xD0 -= 1, and at 0: +6 = 0.
 * Last, mode 0 or 8 (all 64 bits): 001C3D60(a0, a1).
 * ---------------------------------------------------------------------- */
static void turn(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    uint32_t yaw = callf(r, sp, FN_1B12B0, 0, 0, 0, 0, 3, w32(r, a1 + 0xE8u), w32(r, a0 + 0xC4u), 0x3D8EFA35u).f0;
    st32(r, a0 + 0xC4u, yaw);
    if (em_ee_c_eq_bits(yaw, w32(r, a1 + 0xE8u))) {
        st8(r, a0 + 6, u8(r, a0 + 6) + 1);
        st32(r, a1 + 0xD8u, 0x3E4CCCCDu);
    }
}

/* the dwell step of states 2..5: 1 when it ended in 00128640 */
static int dwell(Run *r, uint32_t sp, uint32_t a0)
{
    if (f_12ADC0(r, sp, a0, a0 + 0xB0u, 0x008102B0u + 0xA0u, 0x42F00000u) != 0) {
        st16(r, a0 + 0x28u, (uint32_t)(s16(r, a0 + 0x28u) + 1));
        if (s16(r, a0 + 0x28u) > 0x5A) {
            f_128640(r, sp, a0);
            return 1;
        }
    } else {
        st16(r, a0 + 0x28u, 0);
    }
    return 0;
}

/* a1 halfword +0xD0 -= 1; 1 when the stored halfword is 0 */
static int countdown(Run *r, uint32_t a1)
{
    uint32_t t = (uint32_t)(s16(r, a1 + 0xD0u) - 1);
    st16(r, a1 + 0xD0u, t);
    return (int16_t)t == 0;
}

static void f_12AFC0(Run *r, uint32_t sp0, uint32_t a0, uint32_t a1)
{
    uint32_t sp = sp0 - 0x50u;
    uint64_t mode, v;
    uint32_t x;
    if (u8(r, a0 + 6) < 4)
        mode = CALL3(FN_1C2770, a0, a1, 1);
    else
        mode = CALL3(FN_1C2770, a0, a1, 0);
    switch (u8(r, a0 + 6)) {
    case 0:
        st16(r, a0 + 0x28u, 0);
        st32(r, a1 + 0xD8u, 0);
        v = CALL0(FN_RAND);
        st16(r, a1 + 0xD0u, (v & 1) ? 0x78u : 0xF0u);
        if (f_128600(r, sp, 0) != 0) {
            callf(r, sp, FN_1287F0, 3, reg(a0), reg(a1), reg(1), 1, 0x41000000u, 0, 0);
            st8(r, a0 + 6, 3);
        } else {
            v = CALL0(FN_RAND);
            x = em_ee_cvt_s_w_bits((uint32_t)v & 0xF0u);
            x = em_ee_mul_bits(0x40C90FDBu, x);
            x = em_ee_div_bits(x, 0x43800000u);
            st32(r, a1 + 0xE8u, callf(r, sp, FN_1B1470, 0, 0, 0, 0, 1, x, 0, 0).f0);
            st8(r, a0 + 6, u8(r, a0 + 6) + 1);
            callf(r, sp, FN_1287F0, 3, reg(a0), reg(a1), reg(2), 1, 0x41000000u, 0, 0);
        }
        break;
    case 1:
        turn(r, sp, a0, a1);
        break;
    case 2:
        if (dwell(r, sp, a0))
            break;
        if (mode != 8) {
            if (f_12ADC0(r, sp, a0, a0 + 0xB0u, a1 + 0x50u, 0xC1F00000u) != 0) {
                st8(r, a0 + 6, 4);
                st32(r, a1 + 0xD8u, 0);
            } else if (countdown(r, a1)) {
                st8(r, a0 + 6, 0);
                st32(r, a1 + 0xD8u, 0);
            }
        } else {
            x = em_ee_add_bits(0x40490FDBu, w32(r, a0 + 0xC4u));
            st32(r, a1 + 0xE8u, callf(r, sp, FN_1B1470, 0, 0, 0, 0, 1, x, 0, 0).f0);
            st16(r, a1 + 0xD0u, 0x28);
            st8(r, a0 + 6, 1);
            st32(r, a1 + 0xD8u, 0);
        }
        break;
    case 3:
    case 5:
        if (dwell(r, sp, a0))
            break;
        if (countdown(r, a1))
            st8(r, a0 + 6, 0);
        break;
    case 4:
        if (dwell(r, sp, a0))
            break;
        turn(r, sp, a0, a1);
        break;
    default:
        break;
    }
    if (mode == 0 || mode == 8)
        CALL2(FN_1C3D60, a0, a1);
}

/* ------------------------------------------------------------------------
 * 0012A5D0(p). Frame 0x40. sub = p + 0x1F0. By byte +4:
 *  0  by +5: 0 -> 00128AB0(p, sub) non-zero: +5 += 1, sub halfword +0xF8
 *     = 1, 001C63E0(p, 1), halfword +0x28 = 0. 1 -> 00129780(p, sub,
 *     +0x0D) non-zero: halfword +0x54 = 0, +0 = 1, and +0x0D (read again)
 *     4 or 9 sets +5 = 9.
 *  1  001B2140(p) zero: nothing. The byte 0x70003B8D at 2 or more with sub
 *     halfword +0xF6 zero: nothing. 001029C0(0x70003000). When (word
 *     0x70003B68 + halfword 0x70003B8A) & 0x3F is 0 and 001B0D80(p) is
 *     non-zero: nothing more. 001B17A0(p). When 00128B80(p, sub) is zero:
 *     sub +0xFA = 0 and by +5 (below 14):
 *       0 sub +0xFB = 0; 00128640(p) zero: +5 = 1, +6 = +7 = 0.
 *       1 sub +0xFB = 0; 0012AFC0(p, sub); +0x0A bit 0: 00128640(p); probe.
 *       2 sub +0xFB = 0; 0012B410(p, sub); unless +0 is 2: probe.
 *       3..6 sub +0xFB = 0x80, +0xFA = 1; 0012B970 / 0012BE20 / 0012C490 /
 *         0012CAA0 (p, sub).
 *       7 sub +0xFB = 7; 0012D240(p, sub).
 *       8 sub +0xFB = 0x80; v = 001C2770(p, sub, 2); 0012D580(p, sub, v);
 *         v zero: 001C3D60(p, sub).
 *       9 sub +0xFB = 0; 0012D850(p, sub).
 *       10, 11 sub +0xFB = 0x80; 0012D940(p, sub).
 *       12 sub +0xFB = 0x80; 001C2770(p, sub, 2); 0012DD70(p, sub);
 *         001C3D60(p, sub).
 *       13 sub +0xFB = 0x80; 0012B850(p, sub); probe.
 *     ("probe": when sub +0xD8 equals 0.0, scratch 0x700038A0 = (0, -1.4,
 *     0, 1) and 001C25E0(p, 0x700038A0) zero sets +5 = 8, +6 = +7 = 0.)
 *     Then sub +0xEC = 1.0 when sub +0xD8 equals 0.0, else 1.8; sub
 *     halfword +0xF4 = 001C64F0(p; sub +0xEC read back); +5 (read again)
 *     not 7: 00102958(0x70003400, 0x70003000), 001C69A0(p). Byte +1 set:
 *     sub signed byte +0xFA non-zero: 001288D0(p, sub); then the pointer
 *     +0x4C is called with p. Last +0x0A = +0x0B = 0, halfword +0x54 = 0.
 *  2  the same 64-frame 001B0D80 test; then D_0081078F set: nothing.
 *     00129FC0(p, sub); +0x0B = 0, +0x0A = 0.
 *  3  0012E070(sub). +0x0D of 10, 11 or 12: the word 0x700031F4 -= 1.
 *     Else sub byte +0xE0 set: +0 = 2, +4 = 4, +5 = 0, 00102948(p + 0xB0,
 *     sub + 0x50) and nothing more. Then 001B1190(byte +0x9A),
 *     001AFC10(p).
 *  4  by +5: 0 -> 001289C0(p, sub), sub halfword +0xF8 = 1, 001C63E0(p,
 *     1), halfword +0x28 = 0, +5 += 1, +0x60..+0x6C = 1.0. 1 ->
 *     00129780(p, sub, +0x0D) non-zero: +4 = 4, +5 = 2, then the halfword
 *     +0x28 = the entry ((((00122BB8() >> 16) << 2) >> 15), 32-bit
 *     arithmetic shifts) of the halfword table 0x00275380. 2 -> halfword
 *     +0x28 -= 1, at 0: +5 = 3. 3 -> 001B13F0(0x00810350, p + 0xB0; 80.0)
 *     zero and (001B1630(p +0xB0, +0xB4, +0xB8) & 0xFF) zero: +0 = 1,
 *     +4 = 1, +5 = +6 = +7 = 0.
 * ---------------------------------------------------------------------- */
static void probe_d8(Run *r, uint32_t sp, uint32_t p, uint32_t sub)
{
    if (!em_ee_c_eq_bits(F_ZERO, w32(r, sub + 0xD8u)))
        return;
    vec4(r, 0x700038A0u, F_ZERO, 0xBFB33333u, F_ZERO, F_ONE);
    if (CALL2(0x001C25E0u, p, 0x700038A0u) == 0) {
        st8(r, p + 5, 8);
        st8(r, p + 6, 0);
        st8(r, p + 7, 0);
    }
}

/* the 64-frame test of states 1 and 2: 1 when 001B0D80 ran and was non-zero */
static int poll_1B0D80(Run *r, uint32_t sp, uint32_t p)
{
    int32_t k = s16(r, 0x70003B8Au);
    if (((w32(r, 0x70003B68u) + (uint32_t)k) & 0x3Fu) != 0)
        return 0;
    return CALL1(0x001B0D80u, p) != 0;
}

static void f_12A5D0(Run *r, uint32_t sp0, uint32_t p)
{
    uint32_t sp = sp0 - 0x40u;
    uint32_t sub = p + 0x1F0u;
    uint32_t k, m, off;
    int32_t t;
    uint64_t v;
    switch (u8(r, p + 4)) {
    case 0:
        k = u8(r, p + 5);
        if (k == 0) {
            if (f_128AB0(r, sp, p, sub) != 0) {
                st8(r, p + 5, u8(r, p + 5) + 1);
                st16(r, sub + 0xF8u, 1);
                CALL2(FN_1C63E0, p, 1);
                st16(r, p + 0x28u, 0);
            }
        } else if (k == 1) {
            if (f_129780(r, sp, p, sub, u8(r, p + 0xDu)) != 0) {
                st16(r, p + 0x54u, 0);
                st8(r, p, 1);
                k = u8(r, p + 0xDu);
                if (k == 4 || k == 9)
                    st8(r, p + 5, 9);
            }
        }
        break;
    case 1:
        if (CALL1(0x001B2140u, p) == 0)
            break;
        m = u8(r, 0x70003B8Du);
        if (m != 0 && m >= 2 && s16(r, sub + 0xF6u) == 0)
            break;
        CALL1(FN_IDENT, 0x70003000u);
        if (poll_1B0D80(r, sp, p))
            break;
        CALL1(0x001B17A0u, p);
        if (CALL2(0x00128B80u, p, sub) == 0) {
            st8(r, sub + 0xFAu, 0);
            k = u8(r, p + 5);
            switch (k) {
            case 0:
                st8(r, sub + 0xFBu, 0);
                if (f_128640(r, sp, p) == 0) {
                    st8(r, p + 5, 1);
                    st8(r, p + 6, 0);
                    st8(r, p + 7, 0);
                }
                break;
            case 1:
                st8(r, sub + 0xFBu, 0);
                f_12AFC0(r, sp, p, sub);
                if (u8(r, p + 0xAu) & 1u)
                    f_128640(r, sp, p);
                probe_d8(r, sp, p, sub);
                break;
            case 2:
                st8(r, sub + 0xFBu, 0);
                CALL2(0x0012B410u, p, sub);
                if (u8(r, p) != 2)
                    probe_d8(r, sp, p, sub);
                break;
            case 3:
            case 4:
            case 5:
            case 6: {
                static const uint32_t fns[4] = {0x0012B970u, 0x0012BE20u, 0x0012C490u, 0x0012CAA0u};
                st8(r, sub + 0xFBu, 0x80);
                st8(r, sub + 0xFAu, 1);
                CALL2(fns[k - 3], p, sub);
                break;
            }
            case 7:
                st8(r, sub + 0xFBu, 7);
                CALL2(0x0012D240u, p, sub);
                break;
            case 8:
                st8(r, sub + 0xFBu, 0x80);
                v = CALL3(FN_1C2770, p, sub, 2);
                calli(r, sp, 0x0012D580u, 3, reg(p), reg(sub), v, 0, 0);
                if (v == 0)
                    CALL2(FN_1C3D60, p, sub);
                break;
            case 9:
                st8(r, sub + 0xFBu, 0);
                CALL2(0x0012D850u, p, sub);
                break;
            case 10:
            case 11:
                st8(r, sub + 0xFBu, 0x80);
                CALL2(0x0012D940u, p, sub);
                break;
            case 12:
                st8(r, sub + 0xFBu, 0x80);
                CALL3(FN_1C2770, p, sub, 2);
                CALL2(0x0012DD70u, p, sub);
                CALL2(FN_1C3D60, p, sub);
                break;
            case 13:
                st8(r, sub + 0xFBu, 0x80);
                CALL2(0x0012B850u, p, sub);
                probe_d8(r, sp, p, sub);
                break;
            default:
                break;
            }
        }
        st32(r, sub + 0xECu, em_ee_c_eq_bits(w32(r, sub + 0xD8u), F_ZERO) ? F_ONE : 0x3FE66666u);
        v = callf(r, sp, 0x001C64F0u, 1, reg(p), 0, 0, 1, w32(r, sub + 0xECu), 0, 0).v0;
        st16(r, sub + 0xF4u, (uint32_t)v);
        if (u8(r, p + 5) != 7) {
            CALL2(FN_COPY_QW4, 0x70003400u, 0x70003000u);
            CALL1(FN_1C69A0, p);
        }
        if (u8(r, p + 1) != 0) {
            if (s8(r, sub + 0xFAu) != 0)
                CALL2(0x001288D0u, p, sub);
            CALL1(w32(r, p + 0x4Cu), p);
        }
        st8(r, p + 0xAu, 0);
        st8(r, p + 0xBu, 0);
        st16(r, p + 0x54u, 0);
        break;
    case 2:
        if (poll_1B0D80(r, sp, p))
            break;
        if (u8(r, 0x0081078Fu) != 0)
            break;
        CALL2(0x00129FC0u, p, sub);
        st8(r, p + 0xBu, 0);
        st8(r, p + 0xAu, 0);
        break;
    case 3:
        CALL1(0x0012E070u, sub);
        k = u8(r, p + 0xDu);
        if (k == 0xA || k - 0xBu < 2u) {
            st32(r, 0x700031F4u, w32(r, 0x700031F4u) - 1u);
        } else if (u8(r, sub + 0xE0u) != 0) {
            st8(r, p, 2);
            st8(r, p + 4, 4);
            st8(r, p + 5, 0);
            CALL2(FN_COPY_QW, p + 0xB0u, sub + 0x50u);
            break;
        }
        CALL1(0x001B1190u, u8(r, p + 0x9Au));
        CALL1(0x001AFC10u, p);
        break;
    case 4:
        switch (u8(r, p + 5)) {
        case 0:
            f_1289C0(r, sp, p, sub);
            st16(r, sub + 0xF8u, 1);
            CALL2(FN_1C63E0, p, 1);
            st16(r, p + 0x28u, 0);
            st8(r, p + 5, u8(r, p + 5) + 1);
            st32(r, p + 0x60u, F_ONE);
            st32(r, p + 0x64u, F_ONE);
            st32(r, p + 0x68u, F_ONE);
            st32(r, p + 0x6Cu, F_ONE);
            break;
        case 1:
            if (f_129780(r, sp, p, sub, u8(r, p + 0xDu)) != 0) {
                st8(r, p + 4, 4);
                st8(r, p + 5, 2);
                v = CALL0(FN_RAND);
                t = (int32_t)(uint32_t)v >> 16;
                t = (int32_t)((uint32_t)t << 2);
                t >>= 15;
                off = (uint32_t)t << 1;
                st16(r, p + 0x28u, (uint32_t)s16(r, 0x00275380u + off));
            }
            break;
        case 2:
            t = s16(r, p + 0x28u) - 1;
            st16(r, p + 0x28u, (uint32_t)t);
            if ((int16_t)t == 0)
                st8(r, p + 5, 3);
            break;
        case 3:
            if (callf(r, sp, FN_1B13F0, 2, reg(0x008102B0u + 0xA0u), reg(p + 0xB0u), 0, 1, 0x42A00000u, 0, 0).v0 !=
                0)
                break;
            v = callf(r, sp, 0x001B1630u, 0, 0, 0, 0, 3, w32(r, p + 0xB0u), w32(r, p + 0xB4u), w32(r, p + 0xB8u)).v0;
            if ((v & 0xFFu) == 0) {
                st8(r, p, 1);
                st8(r, p + 4, 1);
                st8(r, p + 5, 0);
                st8(r, p + 6, 0);
                st8(r, p + 7, 0);
            }
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------
 * 0022DCD0(p). Frame 0x1A0; the request block lives at sp - 0x1A0 + 0x140
 * (0x60 bytes, written by 001CFB50, read by 001CFBE0; this routine never
 * writes it). c = p + 0x1F0 (word +0 the entry count, word +4 the seed).
 * Tables (entry index i): M = 0x00822CF0 + 64 i (matrices), COL = 0x008230F0
 * + 16 i, and words at 0x008231F0 (T), 0x00823230 (Q), 0x00823270 (R),
 * 0x008232B0 (SIZ), 0x008232F0 (SPD), 0x00823330 (PH), 0x00823370 (ROT),
 * 0x008233B0 (ID), 0x008233F0 (TEX).
 * State (byte +4):
 *  0  key = (D_00810700 << 8) + D_00810701 selects the 64-byte-record table
 *     (0/1: 0x00269400, 2: 0x002695C0, 0x200: 0x00269680, 0x202:
 *     0x00269840, 0x300/0x301: 0x00269940, 0x600: 0x00269D40, 0x601:
 *     0x00269FC0, 0x703: 0x0026A240, 0x800: 0x0026A340, 0x803: 0x0026A3C0,
 *     0xD00: 0x0026A540, 0x1001: 0x0026A5C0, 0x1300: 0x0026A640); any
 *     other key sets +4 = 3 and ends. c +0 = 0; for each record e until
 *     its word +0 is -1 (i = c +0, read again before every use):
 *     001029C0(M[i]); 00102B08(M[i], M[i]; e +0x2C); 00102BB0(M[i], M[i];
 *     e +0x30); M[i] +0x30..+0x38 = e +0x20..+0x28; COL[i] = the four
 *     bytes of e +4 as floats; ID[i] = e +0; PH[i] = e +0x1C; SIZ[i] =
 *     e +0x10; SPD[i] = e +0x14; ROT[i] = e +0x18; Q[i] = e +8; R[i] =
 *     e +0xC; T[i] = 1.0 + float(00122BB8()) / 2^31; TEX[i] = -1; c +0 +=
 *     1. Then c +4 = 00122BB8(), +4 = 1, and state 1 runs at once.
 *  1  seed = c +4 (a local; never stored back). For i from 0 while i < c
 *     +0 (read each time), with m = M[i]:
 *     - key 0x1300: D_008101E4 is 3: the halfword D_0081024E 8 ends the
 *       routine. Otherwise: D_008105D0 <= 770 skips the entry when m +0x30
 *       is above 770 (not <= 770); else it skips it when m +0x30 < 770.
 *       key 0xD00: D_00810702 other than 4 and 6 ends the routine;
 *     - 001D0400(0x00823430, 0x0026A900; Q[i]); 00102948(0x00823450,
 *       COL[i]); 00102948(0x00823460, COL[i]); the words 0x0082346C = 0,
 *       0x00823438 = SIZ[i], 0x008234B4 = SPD[i], 0x008234B0 = ID[i];
 *     - n = 001281C0(R[i]): (first, count) = (1, 2) for 8 and 10, (3, 6)
 *       for 14 and 17, (2, 5) for 28, (2, 8) for 35, else (0, 1);
 *     - scratch 0x700038A0 = (0, 0, 0.5 * SIZ[i], 1.0); 001026A0(0x700038A0,
 *       m, 0x700038A0); h = 001CCF70(0x700038A0);
 *     - for k = first while k < count: the word 0x00823440 = R[i] *
 *       (float(k + 1) / float(count)); flag = Q[i] < 4.0 ? 1 : 6;
 *       fr = float((seed >> 16) & 0xFFFF) / 65535.0; seed = seed * 37 +
 *       11; 001CFB50(block, 0, m; T[i], fr + 0.0001, 1.0, 0.1, ROT[i]);
 *       001CFBE0(h, flag, 0x00823430, block, 1);
 *     - T[i] = T[i] + PH[i], and when that is above 2.0 (not <= 2.0), T[i]
 *       (read again) -= 1.0;
 *     - p +0xB0..+0xB8 = m +0x30..+0x38, p +0xBC = 1.0; 001FC3C0(p,
 *       &TEX[i], 0x420; 100.0, 4096.0).
 *  2, 3  001AFC10(p).
 * ---------------------------------------------------------------------- */
static uint32_t area_table(uint32_t key)
{
    switch (key) {
    case 0x0:
    case 0x1:
        return 0x00269400u;
    case 0x2:
        return 0x002695C0u;
    case 0x200:
        return 0x00269680u;
    case 0x202:
        return 0x00269840u;
    case 0x300:
    case 0x301:
        return 0x00269940u;
    case 0x600:
        return 0x00269D40u;
    case 0x601:
        return 0x00269FC0u;
    case 0x703:
        return 0x0026A240u;
    case 0x800:
        return 0x0026A340u;
    case 0x803:
        return 0x0026A3C0u;
    case 0xD00:
        return 0x0026A540u;
    case 0x1001:
        return 0x0026A5C0u;
    case 0x1300:
        return 0x0026A640u;
    default:
        return 0;
    }
}

#define T_M    0x00822CF0u
#define T_COL  0x008230F0u
#define T_T    0x008231F0u
#define T_Q    0x00823230u
#define T_R    0x00823270u
#define T_SIZ  0x008232B0u
#define T_SPD  0x008232F0u
#define T_PH   0x00823330u
#define T_ROT  0x00823370u
#define T_ID   0x008233B0u
#define T_TEX  0x008233F0u
#define F_770  0x44408000u

static uint32_t area_key(Run *r) { return (u8(r, 0x00810700u) << 8) + u8(r, 0x00810701u); }

static void fill_22DCD0(Run *r, uint32_t sp, uint32_t p, uint32_t c, uint32_t e)
{
    uint32_t m, w, v;
    st32(r, c, 0);
    while (w32(r, e) != 0xFFFFFFFFu) {
        m = T_M + (w32(r, c) << 6);
        CALL1(FN_IDENT, m);
        m = T_M + (w32(r, c) << 6);
        callf(r, sp, FN_102B08, 2, reg(m), reg(m), 0, 1, w32(r, e + 0x2Cu), 0, 0);
        m = T_M + (w32(r, c) << 6);
        callf(r, sp, FN_102BB0, 2, reg(m), reg(m), 0, 1, w32(r, e + 0x30u), 0, 0);
        st32(r, T_M + (w32(r, c) << 6) + 0x30u, w32(r, e + 0x20u));
        st32(r, T_M + (w32(r, c) << 6) + 0x34u, w32(r, e + 0x24u));
        st32(r, T_M + (w32(r, c) << 6) + 0x38u, w32(r, e + 0x28u));
        v = u8(r, e + 4);
        st32(r, T_COL + (w32(r, c) << 4), em_ee_cvt_s_w_bits(v));
        w = w32(r, e + 4);
        st32(r, T_COL + (w32(r, c) << 4) + 4u, em_ee_cvt_s_w_bits((w >> 8) & 0xFFu));
        w = w32(r, e + 4);
        st32(r, T_COL + (w32(r, c) << 4) + 8u, em_ee_cvt_s_w_bits((w >> 16) & 0xFFu));
        w = w32(r, e + 4);
        st32(r, T_COL + (w32(r, c) << 4) + 12u, em_ee_cvt_s_w_bits((w >> 24) & 0xFFu));
        st32(r, T_ID + (w32(r, c) << 2), w32(r, e));
        st32(r, T_PH + (w32(r, c) << 2), w32(r, e + 0x1Cu));
        st32(r, T_SIZ + (w32(r, c) << 2), w32(r, e + 0x10u));
        st32(r, T_SPD + (w32(r, c) << 2), w32(r, e + 0x14u));
        st32(r, T_ROT + (w32(r, c) << 2), w32(r, e + 0x18u));
        st32(r, T_Q + (w32(r, c) << 2), w32(r, e + 8u));
        st32(r, T_R + (w32(r, c) << 2), w32(r, e + 0xCu));
        v = (uint32_t)CALL0(FN_RAND);
        v = em_ee_div_bits(em_ee_cvt_s_w_bits(v), 0x4F000000u);
        st32(r, T_T + (w32(r, c) << 2), em_ee_add_bits(F_ONE, v));
        e += 0x40u;
        st32(r, T_TEX + (w32(r, c) << 2), 0xFFFFFFFFu);
        st32(r, c, w32(r, c) + 1u);
    }
    st32(r, c + 4, (uint32_t)CALL0(FN_RAND));
    st8(r, p + 4, 1);
}

static void f_22DCD0(Run *r, uint32_t sp0, uint32_t p)
{
    uint32_t sp = sp0 - 0x1A0u;
    uint32_t block = sp + 0x140u;
    uint32_t c = p + 0x1F0u;
    uint32_t e, seed, i, m, off, key, x, flag, fr, cnt, k;
    uint32_t f[5];
    uint64_t a[5], n;
    int32_t h;
    switch (u8(r, p + 4)) {
    case 0:
        e = area_table(area_key(r));
        if (!e) {
            st8(r, p + 4, 3);
            return;
        }
        fill_22DCD0(r, sp, p, c, e);
        /* fall through: state 1 runs in the same call */
    case 1:
        seed = w32(r, c + 4);
        for (i = 0; (int32_t)i < (int32_t)w32(r, c); i++) {
            m = T_M + i * 0x40u;
            off = i * 4u;
            key = area_key(r);
            if (key == 0x1300) {
                if (u8(r, 0x008101E4u) == 3) {
                    if (s16(r, 0x0081024Eu) == 8)
                        return;
                } else if (em_ee_c_le_bits(w32(r, 0x008105D0u), F_770)) {
                    if (!em_ee_c_le_bits(w32(r, m + 0x30u), F_770))
                        continue;
                } else {
                    if (em_ee_c_lt_bits(w32(r, m + 0x30u), F_770))
                        continue;
                }
            } else if (key == 0xD00) {
                k = u8(r, 0x00810702u);
                if (k != 4 && k != 6)
                    return;
            }
            callf(r, sp, 0x001D0400u, 2, reg(0x00823430u), reg(0x0026A900u), 0, 1, w32(r, T_Q + off), 0, 0);
            CALL2(FN_COPY_QW, 0x00823450u, T_COL + i * 16u);
            CALL2(FN_COPY_QW, 0x00823460u, T_COL + i * 16u);
            st32(r, 0x0082346Cu, 0);
            st32(r, 0x00823438u, w32(r, T_SIZ + off));
            st32(r, 0x008234B4u, w32(r, T_SPD + off));
            st32(r, 0x008234B0u, w32(r, T_ID + off));
            n = callf(r, sp, FN_1281C0, 0, 0, 0, 0, 1, w32(r, T_R + off), 0, 0).v0;
            if (n == 0x23) {
                k = 2;
                cnt = 8;
            } else if (n == 0x1C) {
                k = 2;
                cnt = 5;
            } else if (n == 0x11 || n == 0xE) {
                k = 3;
                cnt = 6;
            } else if (n == 0xA || n == 8) {
                k = 1;
                cnt = 2;
            } else {
                k = 0;
                cnt = 1;
            }
            st32(r, 0x700038A0u, 0);
            st32(r, 0x700038A4u, 0);
            st32(r, 0x700038A8u, em_ee_mul_bits(0x3F000000u, w32(r, T_SIZ + off)));
            st32(r, 0x700038ACu, F_ONE);
            CALL3(FN_MAT_VEC, 0x700038A0u, m, 0x700038A0u);
            h = (int32_t)(uint32_t)CALL1(0x001CCF70u, 0x700038A0u);
            while ((int32_t)k < (int32_t)cnt) {
                x = em_ee_div_bits(em_ee_cvt_s_w_bits(k + 1), em_ee_cvt_s_w_bits(cnt));
                st32(r, 0x00823440u, em_ee_mul_bits(w32(r, T_R + off), x));
                flag = em_ee_c_lt_bits(w32(r, T_Q + off), 0x40800000u) ? 1u : 6u;
                fr = em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)((int32_t)seed >> 16) & 0xFFFFu), 0x477FFF00u);
                seed = seed * 37u + 11u;
                f[0] = w32(r, T_T + off);
                f[1] = em_ee_add_bits(fr, 0x38D1B717u);
                f[2] = F_ONE;
                f[3] = 0x3DCCCCCDu;
                f[4] = w32(r, T_ROT + off);
                a[0] = reg(block);
                a[1] = 0;
                a[2] = reg(m);
                callx(r, sp, 0x001CFB50u, 3, a, 5, f);
                a[0] = reg((uint32_t)h);
                a[1] = reg(flag);
                a[2] = reg(0x00823430u);
                a[3] = reg(block);
                a[4] = reg(1);
                callx(r, sp, 0x001CFBE0u, 5, a, 0, NULL);
                k++;
            }
            x = em_ee_add_bits(w32(r, T_T + off), w32(r, T_PH + off));
            st32(r, T_T + off, x);
            if (!em_ee_c_le_bits(x, 0x40000000u))
                st32(r, T_T + off, em_ee_sub_bits(w32(r, T_T + off), F_ONE));
            st32(r, p + 0xB0u, w32(r, m + 0x30u));
            st32(r, p + 0xB4u, w32(r, m + 0x34u));
            st32(r, p + 0xB8u, w32(r, m + 0x38u));
            st32(r, p + 0xBCu, F_ONE);
            callf(r, sp, 0x001FC3C0u, 3, reg(p), reg(T_TEX + off), reg(0x420u), 2, 0x42C80000u, 0x45800000u, 0);
        }
        break;
    case 2:
    case 3:
        CALL1(0x001AFC10u, p);
        break;
    default:
        break;
    }
}

/* ======================================================================== */
/* Public entries                                                            */
/* ======================================================================== */

#define ENTER(s, address)                                                   \
    Run run_;                                                               \
    Run *r = &run_;                                                         \
    if (!(s))                                                               \
        return -1;                                                          \
    if ((s)->fault != EM_AREA01_EXITA_FAULT_NONE)                           \
        return -1;                                                          \
    run_.s = (s);                                                           \
    if (setjmp(run_.out)) {                                                 \
        if ((s)->fault_function == 0)                                       \
            (s)->fault_function = (address);                                \
        return -1;                                                          \
    }

static int out_null(EmArea01Exita *s, uint32_t address)
{
    s->fault = EM_AREA01_EXITA_FAULT_NULL;
    s->fault_function = address;
    return -1;
}

#define RESULT(s, address, out, expr)                                       \
    do {                                                                    \
        int32_t v_;                                                         \
        ENTER(s, address);                                                  \
        if (!(out))                                                         \
            return out_null((s), (address));                                \
        v_ = (int32_t)(expr);                                               \
        *(out) = v_;                                                        \
        return 0;                                                           \
    } while (0)

int em_area01_exita_00113478(EmArea01Exita *s, int32_t *out)
{
    RESULT(s, 0x00113478u, out, f_113478(r, s->sp));
}

int em_area01_exita_001195A8(EmArea01Exita *s, uint32_t a0, int32_t *out)
{
    RESULT(s, 0x001195A8u, out, f_1195A8(r, s->sp, reg(a0)));
}

int em_area01_exita_00128390(EmArea01Exita *s, uint32_t a0, int32_t a1, int32_t *out)
{
    (void)a0;
    RESULT(s, 0x00128390u, out, f_128390(r, reg((uint32_t)a1)));
}

int em_area01_exita_00128600(EmArea01Exita *s, int32_t a0, int32_t *out)
{
    RESULT(s, 0x00128600u, out, f_128600(r, s->sp, (uint32_t)a0));
}

int em_area01_exita_00128640(EmArea01Exita *s, uint32_t a0, int32_t *out)
{
    RESULT(s, 0x00128640u, out, f_128640(r, s->sp, a0));
}

int em_area01_exita_001289C0(EmArea01Exita *s, uint32_t a0, uint32_t a1)
{
    ENTER(s, 0x001289C0u);
    f_1289C0(r, s->sp, a0, a1);
    return 0;
}

int em_area01_exita_00128AB0(EmArea01Exita *s, uint32_t a0, uint32_t a1, int32_t *out)
{
    RESULT(s, 0x00128AB0u, out, f_128AB0(r, s->sp, a0, a1));
}

int em_area01_exita_00129780(EmArea01Exita *s, uint32_t a, uint32_t b, uint32_t sel, int32_t *out)
{
    RESULT(s, 0x00129780u, out, f_129780(r, s->sp, a, b, sel));
}

int em_area01_exita_0012A5D0(EmArea01Exita *s, uint32_t p)
{
    ENTER(s, 0x0012A5D0u);
    f_12A5D0(r, s->sp, p);
    return 0;
}

int em_area01_exita_0012ADC0(EmArea01Exita *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f12, int32_t *out)
{
    RESULT(s, 0x0012ADC0u, out, f_12ADC0(r, s->sp, a0, a1, a2, f12));
}

int em_area01_exita_0012AFC0(EmArea01Exita *s, uint32_t a0, uint32_t a1)
{
    ENTER(s, 0x0012AFC0u);
    f_12AFC0(r, s->sp, a0, a1);
    return 0;
}

int em_area01_exita_0022DCD0(EmArea01Exita *s, uint32_t p)
{
    ENTER(s, 0x0022DCD0u);
    f_22DCD0(r, s->sp, p);
    return 0;
}
