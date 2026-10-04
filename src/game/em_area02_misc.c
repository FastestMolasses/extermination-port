/* em_area02_misc.c - level-4 lane L4MISC translations (see em_area02_misc.h
 * and docs/AREA02_MISC.md). Every routine names its original address; the
 * comments say what the original does, never how it is encoded. Stores are
 * made in the original's order, and a field the original loads again after
 * a call or a store is loaded again here. Loads that feed one expression are
 * sequenced through locals, in the original's order. */
#include "em_area02_misc.h"

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#include "em_ee_float.h"

/* ------------------------------------------------------------------------
 * Fail-stop plumbing
 * ---------------------------------------------------------------------- */

typedef struct {
    EmArea02Misc *s;
    EmArea02MiscView view;
    jmp_buf out;
} Run;

static void fault(Run *r, int32_t code, uint32_t address)
{
    if (r->s->fault == EM_AREA02_MISC_FAULT_NONE) {
        r->s->fault = code;
        r->s->fault_address = address;
    }
    longjmp(r->out, 1);
}

void em_area02_misc_clear_fault(EmArea02Misc *s)
{
    if (!s)
        return;
    s->fault = EM_AREA02_MISC_FAULT_NONE;
    s->fault_function = 0;
    s->fault_address = 0;
}

/* The byte span [address, address + size) inside one region, or a fault. */
static uint8_t *span(Run *r, uint32_t address, uint32_t size,int write)
{
    const EmArea02Misc *s = r->s;
    if(r->view) {
        uint8_t *p=r->view(s->ctx,address,size,write);
        if(p)return p;
        fault(r,EM_AREA02_MISC_FAULT_UNMAPPED,address);
    }
    unsigned i;
    for (i = 0; i < s->region_count; i++) {
        const EmArea02MiscRegion *g = &s->regions[i];
        if (g->bytes && address >= g->base && size <= g->size && address - g->base <= g->size - size)
            return g->bytes + (address - g->base);
    }
    fault(r, EM_AREA02_MISC_FAULT_UNMAPPED, address);
    return NULL;
}

static uint64_t rd(Run *r, uint32_t a, unsigned n)
{
    const uint8_t *p = span(r, a, n,0);
    uint64_t v = 0;
    unsigned i;
    for (i = 0; i < n; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* Test hook. A build that defines EM_AREA02_MISC_STORE_TRACE as a function
 * name reports every store (original address, size) to it once the address
 * is known to be mapped; tools/test_area02_misc_reference.py uses it to
 * compare memory with the original at every call leaving the module.
 * Ordinary builds compile it out. wr is the only store path. */
#ifdef EM_AREA02_MISC_STORE_TRACE
void EM_AREA02_MISC_STORE_TRACE(uint32_t address, unsigned size);
#define TRACE_STORE(a, n) EM_AREA02_MISC_STORE_TRACE((a), (n))
#else
#define TRACE_STORE(a, n) ((void)0)
#endif

static void wr(Run *r, uint32_t a, uint64_t v, unsigned n)
{
    uint8_t *p = span(r, a, n,1);
    unsigned i;
    TRACE_STORE(a, n);
    for (i = 0; i < n; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t u8(Run *r, uint32_t a) { return (uint32_t)rd(r, a, 1); }
static int32_t s16(Run *r, uint32_t a) { return (int32_t)(int16_t)rd(r, a, 2); }
static uint32_t w32(Run *r, uint32_t a) { return (uint32_t)rd(r, a, 4); }
static uint64_t d64(Run *r, uint32_t a) { return rd(r, a, 8); }
static void st8(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 1); }
static void st16(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 2); }
static void st32(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 4); }
static void st64(Run *r, uint32_t a, uint64_t v) { wr(r, a, v, 8); }

/* A 32-bit value as the EE holds it in a 64-bit register. */
static uint64_t reg(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

/* The register the original reads without setting (header): UNDEFINED at
 * the instruction that reads it. */
static void undefined(Run *r, uint32_t pc) { fault(r, EM_AREA02_MISC_FAULT_UNDEFINED, pc); }

/* ------------------------------------------------------------------------
 * Calls leaving the module
 * ---------------------------------------------------------------------- */

/* One call: `na` integer registers a0.. (register images) and `nf` float
 * registers f12.. (raw bits), as the original sets them. */
static EmArea02MiscCall callx(Run *r, uint32_t sp, uint32_t fn, unsigned na, const uint64_t *a, unsigned nf,
                              const uint32_t *f)
{
    EmArea02MiscCall c;
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
        fault(r, EM_AREA02_MISC_FAULT_NULL, fn);
    if (r->s->call(r->s->ctx, &c) < 0)
        fault(r, EM_AREA02_MISC_FAULT_WORKER, fn);
    return c;
}

/* a0..a3 register images and f12. */
static EmArea02MiscCall callg(Run *r, uint32_t sp, uint32_t fn, unsigned na, uint64_t a0, uint64_t a1, uint64_t a2,
                              uint64_t a3, unsigned nf, uint32_t f12)
{
    uint64_t a[4];
    uint32_t f[1];
    a[0] = a0;
    a[1] = a1;
    a[2] = a2;
    a[3] = a3;
    f[0] = f12;
    return callx(r, sp, fn, na, a, nf, f);
}

#define CALL0(fn) callg(r, sp, (fn), 0, 0, 0, 0, 0, 0, 0).v0
#define CALL1(fn, x) callg(r, sp, (fn), 1, reg(x), 0, 0, 0, 0, 0).v0
#define CALL2(fn, x, y) callg(r, sp, (fn), 2, reg(x), reg(y), 0, 0, 0, 0).v0
#define CALL3(fn, x, y, z) callg(r, sp, (fn), 3, reg(x), reg(y), reg(z), 0, 0, 0).v0
#define CALL4(fn, x, y, z, w) callg(r, sp, (fn), 4, reg(x), reg(y), reg(z), reg(w), 0, 0).v0
/* Float-only call: f12 -> f0. */
#define FCALL1(fn, x) callg(r, sp, (fn), 0, 0, 0, 0, 0, 1, (x)).f0

/* ------------------------------------------------------------------------
 * Constants and EE float shorthands (raw bits)
 * ---------------------------------------------------------------------- */

#define F_ZERO   0x00000000u
#define F_ONE    0x3F800000u
#define F_TWO    0x40000000u
#define F_HALF   0x3F000000u
#define F_0_2    0x3E4CCCCDu /* 0.2 */
#define F_0_8    0x3F4CCCCDu /* 0.8 */
#define F_1_8    0x3FE66666u /* 1.8 */
#define F_16     0x41800000u
#define F_256    0x43800000u
#define F_65535  0x477FFF00u
#define F_1EM4   0x38D1B717u /* 1e-4 */
#define F_1EM6   0x358637BDu /* 1e-6 */

#define FN_ABS       0x0011DF78u /* f12 -> f0 (fabsf-shaped leaf) */
#define FN_SQRT      0x0011E748u /* f12 -> f0 */
#define FN_SIN       0x0011E2A8u /* f12 -> f0 */
#define FN_COS       0x0011DE90u /* f12 -> f0 */
#define FN_F2I       0x001281C0u /* f12 -> v0 */
#define FN_F2U       0x00128250u /* f12 -> v0 */
#define FN_RAND      0x00122BB8u /* -> v0 */
#define FN_VSUB      0x001028D0u /* 3 pointers */
#define FN_NORM      0x00102760u /* 2 pointers */
#define FN_PACKET    0x001CFB50u /* a0..a2, f12..f16 */
#define FN_SUBMIT    0x001CFBE0u /* a0..a3, t0 */
#define FN_SPRITE    0x001CD520u /* a0..a3, t0, f12..f14 */

#define SPR_A20 0x70003A20u
#define SPR_A24 0x70003A24u
#define SPR_A28 0x70003A28u
#define SPR_A2C 0x70003A2Cu
#define SPR_A30 0x70003A30u
#define SPR_A34 0x70003A34u
#define SPR_8A0 0x700038A0u
#define SPR_8AC 0x700038ACu
#define SPR_8B0 0x700038B0u

#define D_275C34 0x00275C34u /* pointer: the effect state record */
#define D_275C30 0x00275C30u
#define PKT      0x0081F8F0u /* D_0081F8F0: the shared packet descriptor */

static uint32_t fadd(uint32_t a, uint32_t b) { return em_ee_add_bits(a, b); }
static uint32_t fsub(uint32_t a, uint32_t b) { return em_ee_sub_bits(a, b); }
static uint32_t fmul(uint32_t a, uint32_t b) { return em_ee_mul_bits(a, b); }
static uint32_t fdiv(uint32_t a, uint32_t b) { return em_ee_div_bits(a, b); }
static int flt(uint32_t a, uint32_t b) { return em_ee_c_lt_bits(a, b); }
static int fle(uint32_t a, uint32_t b) { return em_ee_c_le_bits(a, b); }
static int feq(uint32_t a, uint32_t b) { return em_ee_c_eq_bits(a, b); }

/* 001CFB50(a0, a1, a2; f12, f13, 1.0, 1e-6, f16): the packet builder. */
static void packet(Run *r, uint32_t sp, uint32_t a0, uint32_t a2, uint32_t f12, uint32_t f13, uint32_t f16)
{
    const uint64_t a[3] = {reg(a0), 0, reg(a2)};
    const uint32_t f[5] = {f12, f13, F_ONE, F_1EM6, f16};
    callx(r, sp, FN_PACKET, 3, a, 5, f);
}

/* 001CFBE0(a0, a1, a2, a3, t0): the packet submit. a0 is a register image. */
static void submit(Run *r, uint32_t sp, uint64_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0)
{
    const uint64_t a[5] = {a0, reg(a1), reg(a2), reg(a3), reg(t0)};
    callx(r, sp, FN_SUBMIT, 5, a, 0, NULL);
}

/* The draw of the effect LCG shared by 001EC5F0 / 001EDAF0 / 001E4CE0:
 * float((seed >> 16) & 0xFFFF) / 65535.0 + 1e-4. */
static uint32_t lcg_unit(uint32_t seed)
{
    const uint32_t f = em_ee_cvt_s_w_bits((uint32_t)((int32_t)seed >> 16) & 0xFFFFu);
    return fadd(fdiv(f, F_65535), F_1EM4);
}

static uint32_t lcg_next(uint32_t seed) { return seed * 37u + 11u; }

/* ------------------------------------------------------------------------
 * 0011C128 / 0011E520
 * ---------------------------------------------------------------------- */

/* The rational term shared by both forms of 0011C128: p(t) = t * (pS0 +
 * t * (pS1 + ... t * pS5)), q(t) = 1 + t * (qS1 + ... t * qS4), each step
 * one EE product then one EE sum, in the original's operand order. */
static void asin_pq(uint32_t t, uint32_t *p, uint32_t *q)
{
    uint32_t a = fmul(t, 0x3811EF08u), b = fmul(t, 0x3D9DC62Eu);
    a = fadd(a, 0x3A4F7F04u);
    b = fadd(b, 0xBF303361u);
    a = fmul(t, a);
    b = fmul(t, b);
    a = fadd(a, 0xBD241146u);
    b = fadd(b, 0x4001572Du);
    a = fmul(t, a);
    b = fmul(t, b);
    a = fadd(a, 0x3E4E0AA8u);
    b = fadd(b, 0xC019D139u);
    a = fmul(t, a);
    b = fmul(t, b);
    a = fadd(a, 0xBEA6B090u);
    *q = fadd(b, F_ONE);
    a = fmul(t, a);
    a = fadd(a, 0x3E2AAAABu);
    *p = fmul(t, a);
}

/* 0011C128(x) -> f0, frame 0x50. By ix = bits(x) & 0x7FFFFFFF:
 *   ix == 1.0: x * pio2_hi + x * pio2_lo;
 *   ix > 1.0 (as signed words): (x - x) / (x - x);
 *   ix <= 0x3EFFFFFF: for ix <= 0x31FFFFFF, x when 1.0 < x + 1e30 (always
 *     for such x; the other outcome would read the caller's f20: UNDEFINED
 *     at 0x0011C1FC); else x + x * (p / q) with t = x * x;
 *   otherwise w = 0011DF78(x), t = (1 - w) * 0.5, s = 0011CB90(t) and
 *     for ix > 0x3F799999: pio2_hi - ((s + s * (p / q)) * 2 - pio2_lo),
 *     else the split form with df = bits(s) & 0xFFFFF000:
 *     pio4_hi - ((2s * (p / q) - (pio2_lo - 2 (t - df df) / (s + df)))
 *     - (pio4_hi - 2 df)); negated unless x (as a signed word) is > 0. */
static uint32_t f_11C128(Run *r, uint32_t sp_in, uint32_t x)
{
    const uint32_t sp = sp_in - 0x50u;
    const uint32_t ix = x & 0x7FFFFFFFu;
    uint32_t t, p, q, s, v;
    if (ix == 0x3F800000u) {
        const uint32_t hi = fmul(x, 0x3FC90FDAu);
        const uint32_t lo = fmul(x, 0x33A22168u);
        return fadd(hi, lo);
    }
    if ((int32_t)ix > 0x3F800000) {
        const uint32_t z = fsub(x, x);
        return fdiv(z, z);
    }
    if (!((int32_t)ix > 0x3EFFFFFF)) {
        if (!((int32_t)ix > 0x31FFFFFF)) {
            if (flt(F_ONE, fadd(x, 0x7149F2CAu)))
                return x;
            undefined(r, 0x0011C1FCu);
        }
        t = fmul(x, x);
        asin_pq(t, &p, &q);
        return fadd(x, fmul(x, fdiv(p, q)));
    }
    {
        const uint32_t w = FCALL1(FN_ABS, x);
        t = fmul(fsub(F_ONE, w), F_HALF);
    }
    asin_pq(t, &p, &q);
    s = FCALL1(0x0011CB90u, t);
    if ((int32_t)ix > 0x3F799999) {
        const uint32_t k = fdiv(p, q);
        v = fadd(s, fmul(s, k));
        v = fadd(v, v);
        v = fsub(v, 0x33A22168u);
        v = fsub(0x3FC90FDAu, v);
    } else {
        const uint32_t df = s & 0xFFFFF000u;
        uint32_t c = fsub(t, fmul(df, df));
        const uint32_t sdf = fadd(s, df);
        const uint32_t k = fdiv(p, q);
        const uint32_t s2 = fadd(s, s);
        const uint32_t d2 = fadd(df, df);
        uint32_t pr, qq;
        c = fdiv(c, sdf);
        pr = fmul(s2, k);
        qq = fsub(0x3F490FDBu, d2);
        c = fadd(c, c);
        c = fsub(0x33A22168u, c);
        pr = fsub(pr, c);
        v = fsub(0x3F490FDBu, fsub(pr, qq));
    }
    return (int32_t)x > 0 ? v : em_ee_neg_bits(v);
}

/* 0011E520(x) -> f0, frame 0x60, the exception record at sp - 0x60:
 * +0 type, +4 name, +8 / +0x10 the argument as a double, +0x18 the return
 * value, +0x20 the error code. The kernel's result is returned unless the
 * mode word D_0026C5D0 is not -1, 0011E080(x) is 0 and 1.0 < 0011DF78(x);
 * then the record is filled (type 1, name 0x0026C638, error 0, the
 * argument from 00128350(x) stored three times, return value 0), errno
 * (0011FD78's cell) = 0x21 when the mode is 2 or 0011DB90(record) returns
 * 0, errno = the record's error code when that is not 0 (read again after
 * the second 0011FD78), and the result is 00127758(the record's return
 * value), not the kernel's. */
static uint32_t f_11E520(Run *r, uint32_t sp_in, uint32_t x)
{
    const uint32_t sp = sp_in - 0x60u;
    const uint32_t saved = f_11C128(r, sp, x);
    const int32_t mode = (int32_t)w32(r, 0x0026C5D0u);
    uint64_t d;
    int err = 1;
    if (mode == -1)
        return saved;
    if (callg(r, sp, 0x0011E080u, 0, 0, 0, 0, 0, 1, x).v0 != 0)
        return saved;
    if (!flt(F_ONE, FCALL1(FN_ABS, x)))
        return saved;
    st32(r, sp + 0x0u, 1);
    st32(r, sp + 0x4u, 0x0026C638u);
    st32(r, sp + 0x20u, 0);
    d = callg(r, sp, 0x00128350u, 0, 0, 0, 0, 0, 1, x).v0;
    st64(r, sp + 0x8u, d);
    st64(r, sp + 0x18u, 0);
    st64(r, sp + 0x10u, d);
    if (mode != 2 && callg(r, sp, 0x0011DB90u, 1, reg(sp), 0, 0, 0, 0, 0).v0 != 0)
        err = 0;
    if (err) {
        const uint32_t cell = (uint32_t)CALL0(0x0011FD78u);
        st32(r, cell, 0x21u);
    }
    if (w32(r, sp + 0x20u) != 0) {
        const uint32_t cell = (uint32_t)CALL0(0x0011FD78u);
        st32(r, cell, w32(r, sp + 0x20u));
    }
    return callg(r, sp, 0x00127758u, 1, d64(r, sp + 0x18u), 0, 0, 0, 0, 0).f0;
}

/* ------------------------------------------------------------------------
 * 0015C750 / 0015C7C0
 * ---------------------------------------------------------------------- */

/* 001749A0(e, clip, 0; 1.0) */
static void restart(Run *r, uint32_t sp, uint32_t e, int32_t clip)
{
    callg(r, sp, 0x001749A0u, 3, reg(e), reg((uint32_t)clip), 0, 0, 1, F_ONE);
}

/* 0015C7C0(e), frame 0x20. clip = halfword e +0x20C; against the hurt
 * clips D_00248A02, D_00248A06, D_002754C2 .. D_002754DA (each loaded when
 * tested): the first equal one restarts its normal clip (the halfword two
 * below it); for D_00248A02 the normal clip is D_00248A08 instead when
 * 001B0070() & 4. No match: nothing. */
static void f_15C7C0(Run *r, uint32_t sp_in, uint32_t e)
{
    static const uint32_t pairs[8][2] = {{0x00248A06u, 0x00248A04u}, {0x002754C2u, 0x002754C0u},
                                         {0x002754C6u, 0x002754C4u}, {0x002754CAu, 0x002754C8u},
                                         {0x002754CEu, 0x002754CCu}, {0x002754D2u, 0x002754D0u},
                                         {0x002754D6u, 0x002754D4u}, {0x002754DAu, 0x002754D8u}};
    const uint32_t sp = sp_in - 0x20u;
    const int32_t clip = s16(r, e + 0x20Cu);
    unsigned i;
    if (clip == s16(r, 0x00248A02u)) {
        const uint64_t v = CALL0(0x001B0070u);
        restart(r, sp, e, s16(r, (v & 4u) ? 0x00248A08u : 0x00248A00u));
        return;
    }
    for (i = 0; i < 8; i++) {
        if (clip == s16(r, pairs[i][0])) {
            restart(r, sp, e, s16(r, pairs[i][1]));
            return;
        }
    }
}

/* 0015C750(a0), frame 0x20: +0x234 = 0, D_00810707 = 0, 0015C1F0(a0);
 * the link +0x1C: when not 0, link +4 = 3 and +0x1C = 0; then 0015C7C0. */
static void f_15C750(Run *r, uint32_t sp_in, uint32_t a0)
{
    const uint32_t sp = sp_in - 0x20u;
    uint32_t link;
    st8(r, a0 + 0x234u, 0);
    st8(r, 0x00810707u, 0);
    CALL1(0x0015C1F0u, a0);
    link = w32(r, a0 + 0x1Cu);
    if (link) {
        st8(r, link + 4u, 3);
        st32(r, a0 + 0x1Cu, 0);
    }
    f_15C7C0(r, sp, a0);
}

/* ------------------------------------------------------------------------
 * 0021BD10 / 001A8970 / 001A8E80 / 001A9360 / 001AA640 / 001AA700
 * ---------------------------------------------------------------------- */

/* 0021BD10() -> v0, frame 0x10: 1 when byte D_008102B0 == 1 and
 * 0021BB00(0x008102B0) returns 0 (the whole register), else 0. */
static uint32_t f_21BD10(Run *r, uint32_t sp_in)
{
    const uint32_t sp = sp_in - 0x10u;
    if (u8(r, 0x008102B0u) != 1)
        return 0;
    if (CALL1(0x0021BB00u, 0x008102B0u) != 0)
        return 0;
    return 1;
}

/* |a - b| through 0011DF78 */
static uint32_t absdiff(Run *r, uint32_t sp, uint32_t a, uint32_t b) { return FCALL1(FN_ABS, fsub(a, b)); }

/* 001028D0(0x700038A0, a0 +0xA0, a1 +0xB0); word 0x700038AC = 1.0;
 * 00102760(a0 +0x70, 0x700038A0): the unit direction. */
static void face_away(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    CALL3(FN_VSUB, SPR_8A0, a0 + 0xA0u, a1 + 0xB0u);
    st32(r, SPR_8AC, F_ONE);
    CALL2(FN_NORM, a0 + 0x70u, SPR_8A0);
}

/* 001A8970(a0 self, a1 other), frame 0x40. Gates (each fails -> return):
 * |a0 +0xB0 - a1 +0xB0| <= (a1 +0x30)[0]; |a0 +0xB8 - a1 +0xB8| <=
 * (a1 +0x30)[2]; with h = (a0 +0x30)[1] / 2: |a0 +0xA4 + h - a1 +0xB4| <=
 * h + (a1 +0x30)[1]. Then the handler a1 +0x34 (a1, a0, a0 +0xB0). With
 * k = byte a1 +0xD: k == 5: a0 +0xF = 0xA, +0x224 = +0x220, +0 = 3 and the
 * direction; else when byte a0 +0 != 1: only the clear; else when k is 6
 * or 7 the caller's f20 must pass f20 <= 0.8 * f20 (fail: return, no
 * clear); then k == 0xE and 0021BD10() -> +0xF = 2; the table D_0024A7C0
 * (D_0081070A == 0) or D_0024A800 by k (read again): +0x22C = [k] and
 * +0x224 = [byte a1 +0xD again] for k 9, 0xC, 0xD, else +0x224 = [k];
 * +0 = 3 and the direction. The clear: halfword 0x70003B86 = 0. */
static void f_1A8970(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1)
{
    const uint32_t sp = sp_in - 0x40u;
    uint32_t x, y, ext, e1, h, lift, top, reach, f0, k, tab, kk;
    x = w32(r, a0 + 0xB0u);
    y = w32(r, a1 + 0xB0u);
    f0 = absdiff(r, sp, x, y);
    if (!fle(f0, w32(r, w32(r, a1 + 0x30u))))
        return;
    x = w32(r, a0 + 0xB8u);
    y = w32(r, a1 + 0xB8u);
    f0 = absdiff(r, sp, x, y);
    ext = w32(r, a1 + 0x30u);
    if (!fle(f0, w32(r, ext + 8u)))
        return;
    e1 = w32(r, ext + 4u);
    lift = w32(r, a0 + 0xA4u);
    top = w32(r, a1 + 0xB4u);
    h = fdiv(w32(r, w32(r, a0 + 0x30u) + 4u), F_TWO);
    x = fadd(lift, h);
    reach = fadd(h, e1);
    f0 = FCALL1(FN_ABS, fsub(x, top));
    if (!fle(f0, reach))
        return;
    callg(r, sp, w32(r, a1 + 0x34u), 3, reg(a1), reg(a0), reg(a0 + 0xB0u), 0, 0, 0);
    k = u8(r, a1 + 0xDu);
    if (k == 5) {
        st8(r, a0 + 0xFu, 0xA);
        st32(r, a0 + 0x224u, w32(r, a0 + 0x220u));
        st8(r, a0, 3);
        face_away(r, sp, a0, a1);
    } else if (u8(r, a0) == 1) {
        if (k - 6u < 2u) {
            uint32_t f20;
            if (!r->s->entry_f20)
                undefined(r, 0x001A8AC4u);
            f20 = *r->s->entry_f20;
            if (!fle(f20, fmul(F_0_8, f20)))
                return;
        }
        if (k == 0xEu && f_21BD10(r, sp) != 0)
            st8(r, a0 + 0xFu, 2);
        tab = u8(r, 0x0081070Au) ? 0x0024A800u : 0x0024A7C0u;
        kk = u8(r, a1 + 0xDu);
        if (kk == 9 || kk - 0xCu < 2u) {
            st32(r, a0 + 0x22Cu, w32(r, tab + kk * 4u));
            kk = u8(r, a1 + 0xDu);
            st32(r, a0 + 0x224u, w32(r, tab + kk * 4u));
        } else {
            st32(r, a0 + 0x224u, w32(r, tab + kk * 4u));
        }
        st8(r, a0, 3);
        face_away(r, sp, a0, a1);
    }
    st16(r, 0x70003B86u, 0);
}

/* 001A8E80(a0, a1), frame 0x30: with dx = a1 +0xB0 - a0 +0xB0 and dz =
 * a1 +0xB8 - a0 +0xB8, 0011E748(dx dx + dz dz, accumulated) <= 7.0 +
 * (a0 +0x30)[0] and |6.0 + a1 +0xB4 - a0 +0xB4| <= 8.0: halfword a1 +0x36
 * = 0x14, halfword 0x70003B88 = 0. */
static void f_1A8E80(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1)
{
    const uint32_t sp = sp_in - 0x30u;
    const uint32_t bx = w32(r, a1 + 0xB0u), ax = w32(r, a0 + 0xB0u);
    const uint32_t bz = w32(r, a1 + 0xB8u), az = w32(r, a0 + 0xB8u);
    const uint32_t dx = fsub(bx, ax), dz = fsub(bz, az);
    uint32_t f0, reach, by, ay;
    f0 = FCALL1(FN_SQRT, em_ee_madd_bits(em_ee_mula_bits(dx, dx), dz, dz));
    reach = fadd(0x40E00000u, w32(r, w32(r, a0 + 0x30u)));
    if (!fle(f0, reach))
        return;
    by = w32(r, a1 + 0xB4u);
    ay = w32(r, a0 + 0xB4u);
    f0 = FCALL1(FN_ABS, fsub(fadd(0x40C00000u, by), ay));
    if (!fle(f0, 0x41000000u))
        return;
    st16(r, a1 + 0x36u, 0x14);
    st16(r, 0x70003B88u, 0);
}

/* 001A9360(a0, a1), frame 0x30: 00183C40(a1, 0x700038B0); 001028D0(0x700038A0,
 * 0x700038B0, a0 +0xB0); word 0x700038AC = 0; word 0x70003A20 = 00102738(
 * 0x700038A0, 0x700038A0); v = (a0 +0x30)[0] + (a1 +0x30)[0]; 0x70003A24
 * = v, then v * v; when 0x70003A20 (read again) <= v * v: 001A91C0(a0, a1;
 * 0011E748(that value)), 001028D0 again, 0x700038AC = 1.0, 00102760(a1
 * +0x70, 0x700038A0), halfword 0x70003B88 = 0. */
static void f_1A9360(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1)
{
    const uint32_t sp = sp_in - 0x30u;
    uint32_t pa, pb, thr, ea, eb, v, sq, root;
    CALL2(0x00183C40u, a1, SPR_8B0);
    CALL3(FN_VSUB, SPR_8A0, SPR_8B0, a0 + 0xB0u);
    st32(r, SPR_8AC, 0);
    st32(r, SPR_A20, callg(r, sp, 0x00102738u, 2, reg(SPR_8A0), reg(SPR_8A0), 0, 0, 0, 0).f0);
    pa = w32(r, a0 + 0x30u);
    pb = w32(r, a1 + 0x30u);
    thr = w32(r, SPR_A20);
    ea = w32(r, pa);
    eb = w32(r, pb);
    v = fadd(ea, eb);
    sq = fmul(v, v);
    st32(r, SPR_A24, v);
    st32(r, SPR_A24, sq);
    if (!fle(thr, sq))
        return;
    root = FCALL1(FN_SQRT, thr);
    callg(r, sp, 0x001A91C0u, 2, reg(a0), reg(a1), 0, 0, 1, root);
    CALL3(FN_VSUB, SPR_8A0, SPR_8B0, a0 + 0xB0u);
    st32(r, SPR_8AC, F_ONE);
    CALL2(FN_NORM, a1 + 0x70u, SPR_8A0);
    st16(r, 0x70003B88u, 0);
}

/* 001AA640(a0, a1), frame 0x30: |a0 +0xB0 - a1 +0xB0| <= 50.0, |a0 +0xB8 -
 * a1 +0xB8| <= 46.0 and |a0 +0xB4 - a1 +0xB4| <= 40.0 (each through
 * 0011DF78): halfword a1 +0x36 = 1. */
static void f_1AA640(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1)
{
    const uint32_t sp = sp_in - 0x30u;
    static const uint32_t off[3] = {0xB0u, 0xB8u, 0xB4u};
    static const uint32_t lim[3] = {0x42480000u, 0x42380000u, 0x42200000u};
    unsigned i;
    for (i = 0; i < 3; i++) {
        const uint32_t x = w32(r, a0 + off[i]);
        const uint32_t y = w32(r, a1 + off[i]);
        if (!fle(absdiff(r, sp, x, y), lim[i]))
            return;
    }
    st16(r, a1 + 0x36u, 1);
}

/* 001AA700(a0), frame 0x40: for the halfword count D_00275B84 (signed, read
 * once; 0 -> nothing) of words from D_00275B7C: each non-zero entry e with
 * (byte e +2 & 0x1F) == 4, byte e +3 == 6 and byte e +0 == 1 gets
 * 001AA640(a0, e). */
static void f_1AA700(Run *r, uint32_t sp_in, uint32_t a0)
{
    const uint32_t sp = sp_in - 0x40u;
    uint32_t n = (uint32_t)s16(r, 0x00275B84u), p;
    if (n == 0)
        return;
    p = w32(r, 0x00275B7Cu);
    do {
        const uint32_t e = w32(r, p);
        n -= 1u;
        p += 4u;
        if (e && (u8(r, e + 2u) & 0x1Fu) == 4 && u8(r, e + 3u) == 6 && u8(r, e) == 1)
            f_1AA640(r, sp, a0, e);
    } while (n != 0);
}

/* ------------------------------------------------------------------------
 * 001B6AE0
 * ---------------------------------------------------------------------- */

/* 001B6AE0(a0 unused, a1, a2) -> v0, frame 0x30, by byte a1 +4:
 *   2: 1 when byte D_008106F4 == 1, else 0;
 *   1: a2 +0x10 += 1.0 (stored); when not <= 60.0, a1 +4 += 1; 0;
 *   0: 001FD4C0(a2 +0x18); a2 +8 != 0: a1 +4 = 2; else a1 +4 += 1,
 *      00119828(0, 0, 0), 00119828(1, 0, 0), a2 +0x10 = 0; 0;
 *   other: 0. */
static uint32_t f_1B6AE0(Run *r, uint32_t sp_in, uint32_t a1, uint32_t a2)
{
    const uint32_t sp = sp_in - 0x30u;
    const uint32_t st = u8(r, a1 + 4u);
    if (st == 2)
        return u8(r, 0x008106F4u) == 1 ? 1u : 0u;
    if (st == 1) {
        const uint32_t f = fadd(w32(r, a2 + 0x10u), F_ONE);
        st32(r, a2 + 0x10u, f);
        if (!fle(f, 0x42700000u))
            st8(r, a1 + 4u, u8(r, a1 + 4u) + 1u);
        return 0;
    }
    if (st != 0)
        return 0;
    CALL1(0x001FD4C0u, w32(r, a2 + 0x18u));
    if (w32(r, a2 + 8u) != 0) {
        st8(r, a1 + 4u, 2);
        return 0;
    }
    st8(r, a1 + 4u, u8(r, a1 + 4u) + 1u);
    CALL3(0x00119828u, 0, 0, 0);
    CALL3(0x00119828u, 1, 0, 0);
    st32(r, a2 + 0x10u, 0);
    return 0;
}

/* ------------------------------------------------------------------------
 * 001D3A30 / 001D3AC0 / 001D3C40 / 001D3CE0 / 001D66A0
 * ---------------------------------------------------------------------- */

/* The list record of 001D3A30 / 001D3C40 after 001D1F80(a0, 2, 1): with
 * c = D_00275670 and the slot word c +0x10 + 4 a0 (loaded again before each
 * use): slot +3 = 0x30, slot +4 = bank + (c +0x9C << 7), halfword slot +0
 * = 8, the slot word += 0x10; then next(a0, a1, 0x30, c + 4 a0). */
static void list_record(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t bank, uint32_t next)
{
    uint32_t c, t1, slot, p;
    CALL3(0x001D1F80u, a0, 2, 1);
    c = w32(r, 0x00275670u);
    t1 = w32(r, c + 0x9Cu);
    slot = c + (a0 << 2);
    p = w32(r, slot + 0x10u);
    st8(r, p + 3u, 0x30);
    p = w32(r, slot + 0x10u);
    st32(r, p + 4u, bank + (t1 << 7));
    p = w32(r, slot + 0x10u);
    st16(r, p, 8);
    p = w32(r, slot + 0x10u);
    st32(r, slot + 0x10u, p + 0x10u);
    CALL4(next, a0, a1, 0x30u, slot);
}

/* 001D3A30(a0, a1), frame 0x30: the record of bank D_00816D40, then
 * 001D37D0. */
static void f_1D3A30(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1)
{
    list_record(r, sp_in - 0x30u, a0, a1, 0x00816D40u, 0x001D37D0u);
}

/* 001D3C40(a0, a1), frame 0x30: 001D3A30(a0, a1), then the record of bank
 * D_00816E40 and 001D3AD0. */
static void f_1D3C40(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1)
{
    const uint32_t sp = sp_in - 0x30u;
    f_1D3A30(r, sp, a0, a1);
    list_record(r, sp, a0, a1, 0x00816E40u, 0x001D3AD0u);
}

/* 001D66A0(a0 slot, a1 xyzw, a2 colour, a3 colour; f12 angle) -> v0,
 * frame 0x90. With z = a1[2], w = a1[3], sn / cs = 0011E2A8 / 0011DE90 of
 * the angle and of 0.09817477 (sin first), k = 2 sin(step): the packet at
 * the slot word p (D_00275670 +0x10 + 4 a0, loaded again for each of the
 * header stores): p +3 = 0x10, p +4 = 0, halfword p +0 = 0x86; the slot
 * word = p + 0x870; the quadword at p +0x10 zeroed (low four address bits
 * ignored), p +0x1C = 0x50000085, doubleword p +0x20 = 0x4026400000008021,
 * p +0x28 = 0x4141. Then 33 vertex pairs of 0x40 bytes from p +0x30: the
 * a2 and a3 quadwords (word by word) at +0 / +0x20, the centre
 * 001281C0(16 a1[0]) / (16 a1[1]) at +0x10 / +0x14, +0x18 = 0xFFFFFF, +0x1C
 * = 0, the rim 001281C0(16 (0.8 u + a1[0])) / (16 (0.5 v + a1[1])) at +0x30
 * / +0x34 (a1 read again), +0x38 = 0xFFFFFF, +0x3C = 0; the rotation u -=
 * k c, v -= k d, c += k u, d += k v, starting from u = z cs, v = z sn,
 * c = z cs sin(step) + cos(step) (w sn) (accumulated), d = z sn sin(step)
 * - cos(step) (w cs) (accumulated). The result is p + 0x10. */
static uint32_t f_1D66A0(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t ang)
{
    const uint32_t sp = sp_in - 0x90u;
    uint32_t z = w32(r, a1 + 8u), w = w32(r, a1 + 12u);
    uint32_t sn, cs, ss, cst, u, k, v, c, d, acc, t2, p, q, ws, wc, i;
    sn = FCALL1(FN_SIN, ang);
    cs = FCALL1(FN_COS, ang);
    ss = FCALL1(FN_SIN, 0x3DC90FDBu);
    cst = FCALL1(FN_COS, 0x3DC90FDBu);
    t2 = w32(r, 0x00275670u) + (a0 << 2);
    p = w32(r, t2 + 0x10u);
    u = fmul(z, cs);
    k = fmul(F_TWO, ss);
    st8(r, p + 3u, 0x10);
    p = w32(r, t2 + 0x10u);
    ws = fmul(w, sn);
    st32(r, p + 4u, 0);
    p = w32(r, t2 + 0x10u);
    wc = fmul(w, cs);
    st16(r, p, 0x86);
    p = w32(r, t2 + 0x10u);
    acc = em_ee_mula_bits(u, ss);
    v = fmul(z, sn);
    c = em_ee_madd_bits(acc, cst, ws);
    acc = em_ee_mula_bits(v, ss);
    st32(r, t2 + 0x10u, p + 0x870u);
    {
        const uint32_t qa = (p + 0x10u) & ~0xFu;
        st64(r, qa, 0);
        st64(r, qa + 8u, 0);
    }
    st32(r, p + 0x1Cu, 0x50000085u);
    st64(r, p + 0x20u, UINT64_C(0x4026400000008021));
    d = em_ee_msub_bits(acc, cst, wc);
    st64(r, p + 0x28u, 0x4141u);
    q = p + 0x30u;
    for (i = 0; i < 0x21u; i++) {
        unsigned j;
        uint32_t x, y;
        for (j = 0; j < 4; j++)
            st32(r, q + 4u * j, w32(r, a2 + 4u * j));
        for (j = 0; j < 4; j++)
            st32(r, q + 0x20u + 4u * j, w32(r, a3 + 4u * j));
        x = w32(r, a1);
        st32(r, q + 0x10u, (uint32_t)callg(r, sp, FN_F2I, 0, 0, 0, 0, 0, 1, fmul(F_16, x)).v0);
        y = w32(r, a1 + 4u);
        st32(r, q + 0x14u, (uint32_t)callg(r, sp, FN_F2I, 0, 0, 0, 0, 0, 1, fmul(F_16, y)).v0);
        st32(r, q + 0x18u, 0xFFFFFFu);
        st32(r, q + 0x1Cu, 0);
        x = w32(r, a1);
        x = fadd(fmul(F_0_8, u), x);
        st32(r, q + 0x30u, (uint32_t)callg(r, sp, FN_F2I, 0, 0, 0, 0, 0, 1, fmul(F_16, x)).v0);
        y = w32(r, a1 + 4u);
        y = fadd(fmul(F_HALF, v), y);
        x = (uint32_t)callg(r, sp, FN_F2I, 0, 0, 0, 0, 0, 1, fmul(F_16, y)).v0;
        st32(r, q + 0x34u, x);
        st32(r, q + 0x38u, 0xFFFFFFu);
        u = fsub(u, fmul(k, c));
        st32(r, q + 0x3Cu, 0);
        v = fsub(v, fmul(k, d));
        c = fadd(c, fmul(k, u));
        d = fadd(d, fmul(k, v));
        q += 0x40u;
    }
    return p + 0x10u;
}

/* ------------------------------------------------------------------------
 * 001E4A00 / 001E4CE0
 * ---------------------------------------------------------------------- */

/* One VU0 instruction of a resolved form over 4-lane registers. */
static void vu(Run *r, em_vu_op op, int bc, const uint32_t fs[4], const uint32_t ft[4], uint32_t dst[4])
{
    if (em_vu_vec_bits(op, 15u, bc, fs, ft, 0, NULL, dst) != EM_EE_FLOAT_OK)
        fault(r, EM_AREA02_MISC_FAULT_FLOAT, 0x001E4AD0u);
}

/* The colour ramp of 001E4A00 into the quadword 0x700038B0: for t <= 0.2
 * the ramp's first quadword through 00102948; else with u = (t - 0.2) /
 * 0.8, VU0: (ramp[1] - ramp[0]) * u + ramp[0], then max with 0.0 and min
 * with 255.0 per lane. */
static void ramp(Run *r, uint32_t sp, uint32_t table, uint32_t t)
{
    uint32_t v28[4], v29[4], v30[4], v1[4], zero[4] = {0, 0, 0, F_ONE};
    unsigned i;
    if (fle(t, F_0_2)) {
        CALL2(0x00102948u, SPR_8B0, table);
        return;
    }
    v30[0] = fdiv(fsub(t, F_0_2), F_0_8);
    v30[1] = 0x437F0000u;
    v30[2] = v30[3] = 0;
    for (i = 0; i < 4; i++)
        v28[i] = w32(r, (table & ~0xFu) + 4u * i);
    for (i = 0; i < 4; i++)
        v29[i] = w32(r, ((table + 0x10u) & ~0xFu) + 4u * i);
    vu(r, EM_VU_SUB, EM_VU_NO_BC, v29, v28, v1);
    vu(r, EM_VU_MULBC, 0, v1, v30, v1);
    vu(r, EM_VU_ADD, EM_VU_NO_BC, v1, v28, v1);
    for (i = 0; i < 4; i++)
        v1[i] = em_vu_max_bits(v1[i], zero[0]);
    for (i = 0; i < 4; i++)
        v1[i] = em_vu_min_bits(v1[i], v30[1]);
    for (i = 0; i < 4; i++)
        st32(r, SPR_8B0 + 4u * i, v1[i]);
}

/* The four lanes of 0x700038B0 through 00128250 (each read before its
 * call): lane0 | lane1 << 8 | lane2 << 16 | lane3 << 24 as the original
 * builds it in a 64-bit register (each shifted word sign-extended). */
static uint64_t ramp_colour(Run *r, uint32_t sp)
{
    uint64_t c = 0;
    unsigned i;
    for (i = 0; i < 4; i++) {
        const uint32_t lane = w32(r, SPR_8B0 + 4u * i);
        const uint64_t v = callg(r, sp, FN_F2U, 0, 0, 0, 0, 0, 1, lane).v0;
        c = i == 0 ? v : c | reg((uint32_t)v << (8u * i));
    }
    return c;
}

/* 001CD520(0, 2, a2, a3, t0; f12, f13, f14) */
static void sprite(Run *r, uint32_t sp, uint32_t a2, uint64_t a3, uint64_t t0, uint32_t f12, uint32_t f13,
                   uint32_t f14)
{
    const uint64_t a[5] = {0, 2, reg(a2), a3, t0};
    const uint32_t f[3] = {f12, f13, f14};
    callx(r, sp, FN_SPRITE, 5, a, 3, f);
}

/* 001E4A00(a0; f12 age, f13 size), frame 0x70. Only while age < 1.8: t =
 * age / 1.8, w = size + (2 size) t; the ramps D_002541F0 and D_00254210
 * give the colours c1 and c2; frame = 001281C0(10 t); 001CD520(0, 2, a0
 * +0x30, doubleword D_00253C50[frame], c1; w, w, size / 2) and 001CD520(0,
 * 2, a0 +0x30, 0x20045BA5154222DC, c2; 2 w, 2 w, size / 2). */
static void f_1E4A00(Run *r, uint32_t sp_in, uint32_t a0, uint32_t age, uint32_t size)
{
    const uint32_t sp = sp_in - 0x70u;
    uint32_t t, ten, wv, frame, row;
    uint64_t c1, c2, tex;
    if (!flt(age, F_1_8))
        return;
    t = fdiv(age, F_1_8);
    ten = fmul(0x41200000u, t);
    wv = fadd(size, fmul(fmul(F_TWO, size), t));
    ramp(r, sp, 0x002541F0u, t);
    c1 = ramp_colour(r, sp);
    ramp(r, sp, 0x00254210u, t);
    c2 = ramp_colour(r, sp);
    frame = (uint32_t)callg(r, sp, FN_F2I, 0, 0, 0, 0, 0, 1, ten).v0;
    row = 0x00253C50u + (frame << 3);
    tex = d64(r, row);
    sprite(r, sp, a0 + 0x30u, tex, c1, wv, wv, fdiv(size, F_TWO));
    {
        const uint32_t half = fdiv(size, F_TWO), w2 = fmul(F_TWO, wv);
        sprite(r, sp, a0 + 0x30u, UINT64_C(0x20045BA5154222DC), c2, w2, w2, half);
    }
}

/* The per-variant settings of 001E4CE0 state 1 (its second jump table):
 * the mesh set, the scratch words 0x70003A20 / A24 / A28 / A2C and the
 * lifetime limit. `a24_first`: the order of the stores. */
typedef struct {
    uint32_t set, a20, a24, a28, a2c;
    int32_t limit;
    int a24_first;
} Burst;

static const Burst *burst(uint32_t k)
{
    static const Burst b[15] = {
        {0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0},
        {0x002543F0u, 0x41600000u, 0x41600000u, 0x41100000u, 0x40A00000u, 0x1E, 0},
        {0x00254510u, 0x41600000u, 0x41900000u, 0x41100000u, 0x41200000u, 0x1E, 0},
        {0x00254630u, 0x41600000u, 0x42200000u, 0x41100000u, 0x41A00000u, 0x1E, 0},
        {0x00254630u, 0x41600000u, 0x42200000u, 0x41100000u, 0x41A00000u, 0x3E7, 0},
        {0x00254750u, 0x41400000u, 0, 0x41100000u, 0x41A00000u, 0x1E, 1},
        {0x00254AB0u, 0x41600000u, 0x42200000u, 0x41100000u, 0x41A00000u, 0x1E, 0},
        {0x00254900u, 0x41400000u, 0, 0x41100000u, 0x41A00000u, 0x1E, 1},
        {0x00254BD0u, 0x41600000u, 0, 0x41100000u, 0x40A00000u, 0x1E, 1},
        {0x002542D0u, 0x41600000u, 0x40C00000u, 0x41100000u, 0x40400000u, 0x1E, 0},
        {0, 0, 0, 0, 0, 0, 0},
        {0x00254BD0u, 0x41600000u, 0, 0x41100000u, 0x40A00000u, 0x1E, 1},
        {0x00254BD0u, 0x41600000u, 0, 0x41100000u, 0x40A00000u, 0x1E, 1},
        {0x00254D80u, 0x41200000u, 0, 0x3F800000u, 0x41F00000u, 0x1E, 1},
    };
    return k < 15 && b[k].set ? &b[k] : NULL;
}

/* The parameter table of 001E4CE0 state 0 (its first jump table), 0 for
 * the variants that leave +0x30 as it is. */
static uint32_t burst_params(uint32_t k)
{
    static const uint32_t t[15] = {0, 0, 0x00254230u, 0x00254244u, 0x00254258u, 0x002542BCu, 0x00254258u,
                                   0x00254258u, 0x0025426Cu, 0x00254280u, 0x00254294u, 0, 0x00254280u,
                                   0x00254280u, 0x002542A8u};
    return k < 15 ? t[k] : 0;
}

/* 001E4CE0(a), frame 0xE0, the packet descriptor at sp - 0x60, the
 * emitter block b = a +0x1F0 (+0 seed, +4 frame count, +8 age, +0xC age
 * step). By byte a +4:
 *   2, 3: 001AFC10(a).
 *   0: +0x30 = the parameter table of variant a +0xD (first jump table);
 *      variants 9, 12, 13: 001EFD20(0x8000006E, a +0xB0); a +0 = 2 for
 *      variant 4 (read again) else 1; 001029C0(a +0xD0), 00102C58(a +0xD0,
 *      a +0xD0, a +0xC0), 00102918(a +0xD0, a +0xD0, a +0xB0); b +4 = 0;
 *      b +0 = 00122BB8(); b +8 = b +0xC = 0.1; a +0x34 = 0x001E49F0; a +0xC
 *      = a +9 = 0; a +4 = 1; then state 1 at once.
 *   1: the variant's settings (second jump table); seed = b +0; h =
 *      001CD070(a +0x100, 0x30); variants 6 / 7 in area 0x10 sub 1 with
 *      275 < a +0x100 < 335 and 170 < a +0x108 < 230: h += 0x40000. When h
 *      is not 0xFFFFFF: 0x70003A34 = 2 (+0x30)[0]; 0x70003A30 = 001CD2B0(
 *      that, that, 256, 256); pass = 1, or 6 when D_00275C00 >= 0x101;
 *      0x70003A24 != 0: 001E4A00(a +0xD0; b +8, it); else when b +8 > 0.2
 *      the packet (seed draw) submitted with set +0x120, the seed advanced;
 *      0x70003A28 *= b +8 / 1.8, set +0x70 = D_00253C50[001281C0(it)];
 *      the packet (seed draw) submitted with set (t0 1), the seed advanced;
 *      the packet (seed draw) submitted with set +0x90. Then the step eases
 *      toward 0.01 by / 0x70003A20 (floor 0.01, stored twice), the age
 *      advances; age >= 1.8: a +4 = 3; else b +4 += 1 and when it exceeds
 *      the limit a +0 = 2; variants 5 and 14: 001B1B70(a), others
 *      001B17A0(a).
 *   other: nothing.
 * The seed in b +0 is never written back in state 1. Variants 0, 1, 11
 * and 15 and up have no settings: the original then uses its caller's s0
 * (set) and s5 (limit); UNDEFINED at the first read. */
static void f_1E4CE0(Run *r, uint32_t sp_in, uint32_t a)
{
    const uint32_t sp = sp_in - 0xE0u;
    const uint32_t desc = sp + 0x80u, b = a + 0x1F0u;
    const Burst *cfg;
    uint32_t st = u8(r, a + 4u), k, seed, pass, age, f1, scale, f0, step, n, set = 0;
    uint64_t h;
    if (st == 3 || st == 2) {
        CALL1(0x001AFC10u, a);
        return;
    }
    if (st != 1) {
        if (st != 0)
            return;
        k = u8(r, a + 0xDu);
        if (burst_params(k))
            st32(r, a + 0x30u, burst_params(k));
        k = u8(r, a + 0xDu);
        if (k == 0xD || k == 0xC || k == 9) {
            callg(r, sp, 0x001EFD20u, 2, reg(0x8000006Eu), reg(a + 0xB0u), 0, 0, 0, 0);
            k = u8(r, a + 0xDu);
        }
        st8(r, a, k == 4 ? 2 : 1);
        CALL1(0x001029C0u, a + 0xD0u);
        CALL3(0x00102C58u, a + 0xD0u, a + 0xD0u, a + 0xC0u);
        CALL3(0x00102918u, a + 0xD0u, a + 0xD0u, a + 0xB0u);
        st32(r, b + 4u, 0);
        st32(r, b, (uint32_t)CALL0(FN_RAND));
        st32(r, b + 8u, 0x3DCCCCCDu);
        st32(r, b + 0xCu, 0x3DCCCCCDu);
        st32(r, a + 0x34u, 0x001E49F0u);
        st8(r, a + 0xCu, 0);
        st8(r, a + 9u, 0);
        st8(r, a + 4u, 1);
    }
    cfg = burst(u8(r, a + 0xDu));
    if (cfg) {
        if (cfg->a24_first) {
            st32(r, SPR_A24, cfg->a24);
            st32(r, SPR_A20, cfg->a20);
        } else {
            st32(r, SPR_A20, cfg->a20);
            st32(r, SPR_A24, cfg->a24);
        }
        st32(r, SPR_A28, cfg->a28);
        st32(r, SPR_A2C, cfg->a2c);
        set = cfg->set;
    }
    seed = w32(r, b);
    h = CALL2(0x001CD070u, a + 0x100u, 0x30u);
    k = u8(r, a + 0xDu);
    if (k == 7 || k == 6) {
        const uint32_t area = u8(r, 0x00810700u), sub = u8(r, 0x00810701u);
        if ((area << 8) + sub == 0x1001u) {
            const uint32_t x = w32(r, a + 0x100u);
            if (!fle(x, 0x43898000u) && flt(x, 0x43A78000u)) {
                const uint32_t z = w32(r, a + 0x108u);
                if (!fle(z, 0x432A0000u) && flt(z, 0x43660000u))
                    h = reg((uint32_t)h + 0x40000u);
            }
        }
    }
    if (h != 0xFFFFFFu) {
        uint32_t len, frame, row;
        uint64_t tex;
        scale = fmul(F_TWO, w32(r, w32(r, a + 0x30u)));
        st32(r, SPR_A34, scale);
        {
            const uint32_t f[4] = {scale, scale, F_256, F_256};
            f0 = callx(r, sp, 0x001CD2B0u, 0, NULL, 4, f).f0;
        }
        pass = (int32_t)w32(r, 0x00275C00u) < 0x101 ? 1u : 6u;
        st32(r, SPR_A30, f0);
        len = w32(r, SPR_A24);
        if (!feq(F_ZERO, len)) {
            f_1E4A00(r, sp, a + 0xD0u, w32(r, b + 8u), len);
        } else {
            age = w32(r, b + 8u);
            if (!fle(age, F_0_2)) {
                packet(r, sp, desc, a + 0xD0u, age, lcg_unit(seed), w32(r, SPR_A2C));
                seed = lcg_next(seed);
                if (!cfg)
                    undefined(r, 0x001E537Cu);
                submit(r, sp, h, pass, set + 0x120u, desc, 0);
            }
        }
        f1 = fdiv(w32(r, b + 8u), F_1_8);
        f0 = fmul(w32(r, SPR_A28), f1);
        st32(r, SPR_A28, f0);
        frame = (uint32_t)callg(r, sp, FN_F2I, 0, 0, 0, 0, 0, 1, f0).v0;
        row = 0x00253C50u + (frame << 3);
        tex = d64(r, row);
        if (!cfg)
            undefined(r, 0x001E53ECu);
        st64(r, set + 0x70u, tex);
        {
            const uint32_t draw = lcg_unit(seed);
            const uint32_t now = w32(r, b + 8u);
            packet(r, sp, desc, a + 0xD0u, now, draw, w32(r, SPR_A2C));
        }
        seed = lcg_next(seed);
        submit(r, sp, h, pass, set, desc, 1);
        {
            const uint32_t draw = lcg_unit(seed);
            const uint32_t now = w32(r, b + 8u);
            packet(r, sp, desc, a + 0xD0u, now, draw, w32(r, SPR_A2C));
        }
        submit(r, sp, h, pass, set + 0x90u, desc, 0);
    }
    step = w32(r, b + 0xCu);
    f0 = w32(r, SPR_A20);
    step = fadd(step, fdiv(fsub(0x3C23D70Au, step), f0));
    st32(r, b + 0xCu, step);
    if (flt(step, 0x3C23D70Au))
        step = 0x3C23D70Au;
    st32(r, b + 0xCu, step);
    age = fadd(w32(r, b + 8u), step);
    st32(r, b + 8u, age);
    if (!flt(age, F_1_8)) {
        st8(r, a + 4u, 3);
        return;
    }
    st32(r, b + 4u, w32(r, b + 4u) + 1u);
    n = w32(r, b + 4u);
    if (!cfg)
        undefined(r, 0x001E555Cu);
    if (cfg->limit < (int32_t)n)
        st8(r, a, 2);
    k = u8(r, a + 0xDu);
    if (k == 5 || k == 14)
        CALL1(0x001B1B70u, a);
    else
        CALL1(0x001B17A0u, a);
}

/* ------------------------------------------------------------------------
 * 001EBD20 / 001EC5F0 / 001EC9A0 / 001ECEF0 / 001EDAF0
 * ---------------------------------------------------------------------- */

/* 001EBD20(a0, a1), frame 0x20: copy_qw4 (00102958)(0x70003400, a0); with
 * c = D_00275C34 (read before the store): 0x70003434 += 5.0; the packet
 * (0x0081F8F0, 0, 0x70003400; c +0x54, c +0x5C, 1.0, 1e-6, 6.0); submit
 * (a1, 1, D_00275C30 +0x38 ? 0x002563A0 : 0x00256430, 0x0081F8F0, 0). */
static void f_1EBD20(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1)
{
    const uint32_t sp = sp_in - 0x20u;
    uint32_t f1, c, v, f12, f13, flag;
    CALL2(0x00102958u, 0x70003400u, a0);
    f1 = w32(r, 0x70003434u);
    c = w32(r, D_275C34);
    v = fadd(f1, 0x40A00000u);
    st32(r, 0x70003434u, v);
    f12 = w32(r, c + 0x54u);
    f13 = w32(r, c + 0x5Cu);
    packet(r, sp, PKT, 0x70003400u, f12, f13, 0x40C00000u);
    flag = w32(r, w32(r, D_275C30) + 0x38u);
    submit(r, sp, reg(a1), 1, flag ? 0x002563A0u : 0x00256430u, PKT, 0);
}

/* One pass of 001EC5F0 / 001EDAF0: seed = c +4 (c = D_00275C34), the draw,
 * c +4 = seed * 37 + 11; the packet (0x0081F8F0, 0, a0; (D_00275C34 read
 * again) +0x54, draw, 1.0, 1e-6, f16); submit(a1, kind, table, 0x0081F8F0,
 * t0). */
static void seeded_pass(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t f16, uint32_t kind,
                        uint32_t table, uint32_t t0)
{
    const uint32_t c = w32(r, D_275C34);
    const uint32_t seed = w32(r, c + 4u);
    const uint32_t draw = lcg_unit(seed);
    uint32_t f12;
    st32(r, c + 4u, lcg_next(seed));
    f12 = w32(r, w32(r, D_275C34) + 0x54u);
    packet(r, sp, PKT, a0, f12, draw, f16);
    submit(r, sp, reg(a1), kind, table, PKT, t0);
}

/* 001EC5F0(a0, a1), frame 0x30: three seeded passes (5.0; kind 0, t0 0)
 * with the tables 0x00256A60, 0x00256AF0, 0x00256B80. */
static void f_1EC5F0(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1)
{
    const uint32_t sp = sp_in - 0x30u;
    seeded_pass(r, sp, a0, a1, 0x40A00000u, 0, 0x00256A60u, 0);
    seeded_pass(r, sp, a0, a1, 0x40A00000u, 0, 0x00256AF0u, 0);
    seeded_pass(r, sp, a0, a1, 0x40A00000u, 0, 0x00256B80u, 0);
}

/* The unseeded packet of 001EC9A0 / 001ECEF0: c = D_00275C34, the packet
 * (0x0081F8F0, 0, a0; c +0x54, c +0x5C, 1.0, 1e-6, f16). */
static void plain_packet(Run *r, uint32_t sp, uint32_t a0, uint32_t f16)
{
    const uint32_t c = w32(r, D_275C34);
    const uint32_t f12 = w32(r, c + 0x54u);
    const uint32_t f13 = w32(r, c + 0x5Cu);
    packet(r, sp, PKT, a0, f12, f13, f16);
}

/* 001EC9A0(a0, a1), frame 0x20: the packet (30.0); submit(a1, 2,
 * 0x00256D30, 0x0081F8F0, 0). */
static void f_1EC9A0(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1)
{
    const uint32_t sp = sp_in - 0x20u;
    plain_packet(r, sp, a0, 0x41F00000u);
    submit(r, sp, reg(a1), 2, 0x00256D30u, PKT, 0);
}

/* 001ECEF0(a0, a1), frame 0x20: the packet (3.0); submit(a1, 1,
 * D_00275C30 +0x38 ? 0x00257090 : 0x00257120, 0x0081F8F0, 0). */
static void f_1ECEF0(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1)
{
    const uint32_t sp = sp_in - 0x20u;
    uint32_t flag;
    plain_packet(r, sp, a0, 0x40400000u);
    flag = w32(r, w32(r, D_275C30) + 0x38u);
    submit(r, sp, reg(a1), 1, flag ? 0x00257090u : 0x00257120u, PKT, 0);
}

/* 001EDAF0(a0, a1), frame 0x30: the words of the three 0x90-byte records
 * 0x00257510 / 0x002575A0 / 0x00257630 at +0x20..+0x3C (the last store
 * after D_00275C34 is read); three seeded passes (10.0; kind 1, t0 1) with
 * those records as tables; then c +8 += (0.02 - c +8) / 10.0 and (c read
 * again) c +8 = max(c +8, 0.02) by a less-than test. */
static void f_1EDAF0(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1)
{
    static const struct {
        uint32_t address, value;
    } init[23] = {
        {0x00257530u, 0x41800000u}, {0x00257534u, 0x41400000u}, {0x00257538u, 0x41800000u},
        {0x0025753Cu, 0},           {0x00257540u, 0},           {0x00257544u, 0},
        {0x00257548u, 0},           {0x0025754Cu, 0},           {0x002575C0u, 0x43000000u},
        {0x002575C4u, 0},           {0x002575C8u, 0x43000000u}, {0x002575CCu, 0x42C00000u},
        {0x002575D0u, 0x42C00000u}, {0x002575D4u, 0},           {0x002575D8u, 0x42C00000u},
        {0x002575DCu, 0},           {0x00257650u, 0x43000000u}, {0x00257654u, 0},
        {0x00257658u, 0x43000000u}, {0x0025765Cu, 0x42C00000u}, {0x00257660u, 0x42C00000u},
        {0x00257664u, 0},           {0x00257668u, 0x42C00000u},
    };
    const uint32_t sp = sp_in - 0x30u;
    uint32_t c, seed, draw, f12, cur, v;
    unsigned i;
    for (i = 0; i < 23; i++)
        st32(r, init[i].address, init[i].value);
    /* the first pass, with the D_0025766C store between its two loads */
    c = w32(r, D_275C34);
    st32(r, 0x0025766Cu, 0);
    seed = w32(r, c + 4u);
    draw = lcg_unit(seed);
    st32(r, c + 4u, lcg_next(seed));
    f12 = w32(r, w32(r, D_275C34) + 0x54u);
    packet(r, sp, PKT, a0, f12, draw, 0x41200000u);
    submit(r, sp, reg(a1), 1, 0x00257510u, PKT, 1);
    seeded_pass(r, sp, a0, a1, 0x41200000u, 1, 0x002575A0u, 1);
    seeded_pass(r, sp, a0, a1, 0x41200000u, 1, 0x00257630u, 1);
    c = w32(r, D_275C34);
    cur = w32(r, c + 8u);
    st32(r, c + 8u, fadd(cur, fdiv(fsub(0x3CA3D70Au, cur), 0x41200000u)));
    c = w32(r, D_275C34);
    v = w32(r, c + 8u);
    if (flt(v, 0x3CA3D70Au))
        v = 0x3CA3D70Au;
    st32(r, c + 8u, v);
}

/* ------------------------------------------------------------------------
 * 001F4010 / 001F4E40 / 001F6B30 / 001F9660
 * ---------------------------------------------------------------------- */

/* 001F4010(a0 index, a1), frame 0x40: slot = 0x007709C0 + 0x90 D_00275C40,
 * e = 0x0025A350 + 0x60 a0; 001F2F90(a1, slot, e, a0); 001F3340(slot, e,
 * a0); D_00275C40 += 1, then (read again) = 0 unless < 0x80 (signed). */
static void f_1F4010(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1)
{
    const uint32_t sp = sp_in - 0x40u;
    const uint32_t ring = w32(r, 0x00275C40u);
    const uint32_t e = 0x0025A350u + ((a0 * 3u) << 5);
    const uint32_t slot = 0x007709C0u + ((ring * 9u) << 4);
    CALL4(0x001F2F90u, a1, slot, e, a0);
    CALL3(0x001F3340u, slot, e, a0);
    st32(r, 0x00275C40u, w32(r, 0x00275C40u) + 1u);
    if (!((int32_t)w32(r, 0x00275C40u) < 0x80))
        st32(r, 0x00275C40u, 0);
}

/* 001F4E40(a0, a1, a2; f12), frame 0x50 (32-bit products): m =
 * |((word 0x70003B68 * a2) & 0x1FF) - 0x100|; s = (a1 +0xC * m) >> 8;
 * the draw ((00122BB8() >> 23, signed) & 0xFF); s = (s + ((s * draw) >> 8))
 * >> 1; colour = ((a1 +8 * s) >> 7) << 16 | ((a1 +4 * s) >> 7) << 8 |
 * (a1 +0 * s) >> 7 (a1 +4, +0, +8 read in that order after the draw);
 * 001CD520(0, 2, a0, 0x20045B0599421EF0, colour; f12, f12, 1.5). */
static void f_1F4E40(Run *r, uint32_t sp_in, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f12)
{
    const uint32_t sp = sp_in - 0x50u;
    const uint32_t frame = w32(r, 0x70003B68u);
    uint32_t s = w32(r, a1 + 0xCu), m, draw, g, rr, bb, col;
    m = ((frame * a2) & 0x1FFu) - 0x100u;
    if ((int32_t)m < 0)
        m = 0u - m;
    s = (s * m) >> 8;
    draw = (uint32_t)((int32_t)(uint32_t)CALL0(FN_RAND) >> 23) & 0xFFu;
    g = w32(r, a1 + 4u);
    rr = w32(r, a1);
    bb = w32(r, a1 + 8u);
    s = (s + ((s * draw) >> 8)) >> 1;
    col = (((bb * s) >> 7) << 16) | (((g * s) >> 7) << 8) | ((rr * s) >> 7);
    {
        const uint64_t a[5] = {0, 2, reg(a0), UINT64_C(0x20045B0599421EF0), reg(col)};
        const uint32_t f[3] = {f12, f12, 0x3FC00000u};
        callx(r, sp, FN_SPRITE, 5, a, 3, f);
    }
}

/* 001F6B30(), frame 0x10: 001F6640(001F6760()), the whole register. */
static void f_1F6B30(Run *r, uint32_t sp_in)
{
    const uint32_t sp = sp_in - 0x10u;
    const uint64_t v = CALL0(0x001F6760u);
    callg(r, sp, 0x001F6640u, 1, v, 0, 0, 0, 0, 0);
}

/* 001F9660(a0, a1), frame 0x310, the copy at sp - 0x2F0: block_copy
 * (00121870)(copy, a0, 0x2F0); with key = (D_00810700 << 8) + D_00810701:
 * key 0x1500: a1 0xA0 -> 0xB, 0x9D -> 0xA; other keys: a1 0x72 -> 0xC,
 * 0x6E -> 1, 0x9D -> 9, 0x9C -> 2 (a1 compared as the whole register);
 * a match stores the byte at copy +3 and calls 001F91C0(copy); no match
 * returns. */
static void f_1F9660(Run *r, uint32_t sp_in, uint32_t a0, uint64_t a1)
{
    const uint32_t sp = sp_in - 0x310u;
    const uint32_t copy = sp + 0x20u;
    uint32_t area, sub, v;
    CALL3(0x00121870u, copy, a0, 0x2F0u);
    area = u8(r, 0x00810700u);
    sub = u8(r, 0x00810701u);
    if ((area << 8) + sub == 0x1500u) {
        if (a1 == 0xA0u)
            v = 0xB;
        else if (a1 == 0x9Du)
            v = 0xA;
        else
            return;
    } else if (a1 == 0x72u) {
        v = 0xC;
    } else if (a1 == 0x6Eu) {
        v = 1;
    } else if (a1 == 0x9Du) {
        v = 9;
    } else if (a1 == 0x9Cu) {
        v = 2;
    } else {
        return;
    }
    st8(r, copy + 3u, v);
    CALL1(0x001F91C0u, copy);
}

/* ------------------------------------------------------------------------
 * Entry points
 * ---------------------------------------------------------------------- */

#define ENTER(fn)                                                                                            \
    Run run;                                                                                                 \
    Run *r = &run;                                                                                           \
    if (!s)                                                                                                  \
        return -1;                                                                                           \
    if (s->fault != EM_AREA02_MISC_FAULT_NONE)                                                               \
        return -1;                                                                                           \
    r->s = s; r->view = NULL;                                                                                \
    if (setjmp(r->out)) {                                                                                    \
        s->fault_function = (fn);                                                                            \
        return -1;                                                                                           \
    }

#define NEED_OUT(fn)                                                                                         \
    if (!out) {                                                                                              \
        s->fault = EM_AREA02_MISC_FAULT_NULL;                                                                \
        s->fault_function = (fn);                                                                            \
        s->fault_address = 0;                                                                                \
        return -1;                                                                                           \
    }

int em_area02_misc_0011C128(EmArea02Misc *s, uint32_t f12, uint32_t *out)
{
    ENTER(0x0011C128u)
    NEED_OUT(0x0011C128u)
    *out = f_11C128(r, s->sp, f12);
    return 0;
}

int em_area02_misc_0011E520(EmArea02Misc *s, uint32_t f12, uint32_t *out)
{
    ENTER(0x0011E520u)
    NEED_OUT(0x0011E520u)
    *out = f_11E520(r, s->sp, f12);
    return 0;
}

int em_area02_misc_view_0011E520(EmArea02Misc *s,EmArea02MiscView view,uint32_t f12,uint32_t *out)
{
    ENTER(0x0011E520u)
    NEED_OUT(0x0011E520u)
    if(!view) { s->fault=EM_AREA02_MISC_FAULT_NULL;s->fault_function=0x0011E520u;return -1; }
    r->view=view;
    *out=f_11E520(r,s->sp,f12);
    return 0;
}

int em_area02_misc_0015C750(EmArea02Misc *s, uint32_t a0)
{
    ENTER(0x0015C750u)
    f_15C750(r, s->sp, a0);
    return 0;
}

int em_area02_misc_0015C7C0(EmArea02Misc *s, uint32_t a0)
{
    ENTER(0x0015C7C0u)
    f_15C7C0(r, s->sp, a0);
    return 0;
}

int em_area02_misc_001A8970(EmArea02Misc *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001A8970u)
    f_1A8970(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_001A8E80(EmArea02Misc *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001A8E80u)
    f_1A8E80(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_001A9360(EmArea02Misc *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001A9360u)
    f_1A9360(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_001AA640(EmArea02Misc *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001AA640u)
    f_1AA640(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_001AA700(EmArea02Misc *s, uint32_t a0)
{
    ENTER(0x001AA700u)
    f_1AA700(r, s->sp, a0);
    return 0;
}

int em_area02_misc_001B6AE0(EmArea02Misc *s, uint32_t a1, uint32_t a2, int32_t *out)
{
    ENTER(0x001B6AE0u)
    NEED_OUT(0x001B6AE0u)
    *out = (int32_t)f_1B6AE0(r, s->sp, a1, a2);
    return 0;
}

int em_area02_misc_001D3A30(EmArea02Misc *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001D3A30u)
    f_1D3A30(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_001D3AC0(EmArea02Misc *s, uint32_t a0)
{
    ENTER(0x001D3AC0u)
    f_1D3A30(r, s->sp, 3, a0);
    return 0;
}

int em_area02_misc_001D3C40(EmArea02Misc *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001D3C40u)
    f_1D3C40(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_001D3CE0(EmArea02Misc *s, uint32_t a0)
{
    ENTER(0x001D3CE0u)
    f_1D3C40(r, s->sp, 3, a0);
    return 0;
}

int em_area02_misc_001D66A0(EmArea02Misc *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t f12,
                            uint32_t *out)
{
    ENTER(0x001D66A0u)
    NEED_OUT(0x001D66A0u)
    *out = f_1D66A0(r, s->sp, a0, a1, a2, a3, f12);
    return 0;
}

int em_area02_misc_001E49F0(EmArea02Misc *s)
{
    /* 001E49F0: returns at once (the think function 001E4CE0 installs). */
    ENTER(0x001E49F0u)
    (void)r;
    return 0;
}

int em_area02_misc_001E4A00(EmArea02Misc *s, uint32_t a0, uint32_t f12, uint32_t f13)
{
    ENTER(0x001E4A00u)
    f_1E4A00(r, s->sp, a0, f12, f13);
    return 0;
}

int em_area02_misc_001E4CE0(EmArea02Misc *s, uint32_t a0)
{
    ENTER(0x001E4CE0u)
    f_1E4CE0(r, s->sp, a0);
    return 0;
}

int em_area02_misc_001EBD20(EmArea02Misc *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001EBD20u)
    f_1EBD20(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_001EC5F0(EmArea02Misc *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001EC5F0u)
    f_1EC5F0(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_001EC9A0(EmArea02Misc *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001EC9A0u)
    f_1EC9A0(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_001ECEF0(EmArea02Misc *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001ECEF0u)
    f_1ECEF0(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_001EDAF0(EmArea02Misc *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001EDAF0u)
    f_1EDAF0(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_001F4010(EmArea02Misc *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001F4010u)
    f_1F4010(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_001F4E40(EmArea02Misc *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f12)
{
    ENTER(0x001F4E40u)
    f_1F4E40(r, s->sp, a0, a1, a2, f12);
    return 0;
}

int em_area02_misc_001F6B30(EmArea02Misc *s)
{
    ENTER(0x001F6B30u)
    f_1F6B30(r, s->sp);
    return 0;
}

int em_area02_misc_001F9660(EmArea02Misc *s, uint32_t a0, uint64_t a1)
{
    ENTER(0x001F9660u)
    f_1F9660(r, s->sp, a0, a1);
    return 0;
}

int em_area02_misc_0021BD10(EmArea02Misc *s, int32_t *out)
{
    ENTER(0x0021BD10u)
    NEED_OUT(0x0021BD10u)
    *out = (int32_t)f_21BD10(r, s->sp);
    return 0;
}
