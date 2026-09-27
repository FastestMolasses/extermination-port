/* em_area01_sys.c - AREA01 lane SYS translations (see em_area01_sys.h and
 * docs/AREA01_SYS.md). Every routine names its original address; the
 * comments say what the original does, never how it is encoded. */
#include "em_area01_sys.h"

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#include "em_ee_float.h"

/* ------------------------------------------------------------------------
 * Fail-stop plumbing
 * ---------------------------------------------------------------------- */

typedef struct {
    EmArea01Sys *s;
    jmp_buf out;
} Run;

static void fault(Run *r, int32_t code, uint32_t address)
{
    if (r->s->fault == EM_AREA01_SYS_FAULT_NONE) {
        r->s->fault = code;
        r->s->fault_address = address;
    }
    longjmp(r->out, 1);
}

void em_area01_sys_clear_fault(EmArea01Sys *s)
{
    if (!s)
        return;
    s->fault = EM_AREA01_SYS_FAULT_NONE;
    s->fault_function = 0;
    s->fault_address = 0;
}

/* The byte span [address, address + size) inside one region, or a fault. */
static uint8_t *span(Run *r, uint32_t address, uint32_t size)
{
    const EmArea01Sys *s = r->s;
    unsigned i;
    for (i = 0; i < s->region_count; i++) {
        const EmArea01SysRegion *g = &s->regions[i];
        if (g->bytes && address >= g->base && size <= g->size && address - g->base <= g->size - size)
            return g->bytes + (address - g->base);
    }
    fault(r, EM_AREA01_SYS_FAULT_UNMAPPED, address);
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

/* Test hook. A build that defines EM_AREA01_SYS_STORE_TRACE as a function
 * name reports every store (original address, size) to it once the address
 * is known to be mapped; tools/test_area01_sys_reference.py uses it to
 * compare memory with the original at every call leaving the module.
 * Ordinary builds compile it out. wr and st64 are the only stores. */
#ifdef EM_AREA01_SYS_STORE_TRACE
void EM_AREA01_SYS_STORE_TRACE(uint32_t address, unsigned size);
#define TRACE_STORE(a, n) EM_AREA01_SYS_STORE_TRACE((a), (n))
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

static void st64(Run *r, uint32_t a, uint64_t v)
{
    uint8_t *p = span(r, a, 8);
    unsigned i;
    TRACE_STORE(a, 8);
    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t ld64(Run *r, uint32_t a)
{
    const uint8_t *p = span(r, a, 8);
    uint64_t v = 0;
    unsigned i;
    for (i = 0; i < 8; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* 32-bit value as the EE holds it in a 64-bit register. */
static uint64_t reg(int32_t v) { return (uint64_t)(int64_t)v; }

/* ------------------------------------------------------------------------
 * Calls leaving the module
 * ---------------------------------------------------------------------- */

typedef struct {
    uint32_t fn, sp;
    unsigned na, nf;
    uint64_t a[8];
    uint32_t f[4];
} Out;

static EmArea01SysCall invoke(Run *r, const Out *o)
{
    EmArea01SysCall c;
    unsigned i;
    memset(&c, 0, sizeof c);
    c.fn = o->fn;
    c.sp = o->sp;
    for (i = 0; i < 8; i++)
        c.a[i] = o->a[i];
    for (i = 0; i < 4; i++)
        c.f[i] = o->f[i];
    c.na = o->na;
    c.nf = o->nf;
    if (!r->s->call)
        fault(r, EM_AREA01_SYS_FAULT_NULL, o->fn);
    if (r->s->call(r->s->ctx, &c) < 0)
        fault(r, EM_AREA01_SYS_FAULT_WORKER, o->fn);
    return c;
}

/* Integer-argument call: up to four a-registers, returns v0 (low word). */
static int32_t callv(Run *r, uint32_t sp, uint32_t fn, unsigned na, uint64_t a0, uint64_t a1, uint64_t a2,
                     uint64_t a3)
{
    Out o;
    EmArea01SysCall c;
    memset(&o, 0, sizeof o);
    o.fn = fn;
    o.sp = sp;
    o.na = na;
    o.a[0] = a0;
    o.a[1] = a1;
    o.a[2] = a2;
    o.a[3] = a3;
    c = invoke(r, &o);
    return (int32_t)(uint32_t)c.v0;
}

#define CALL0(r, sp, fn) callv(r, sp, fn, 0, 0, 0, 0, 0)
#define CALL1(r, sp, fn, a) callv(r, sp, fn, 1, reg((int32_t)(a)), 0, 0, 0)
#define CALL2(r, sp, fn, a, b) callv(r, sp, fn, 2, reg((int32_t)(a)), reg((int32_t)(b)), 0, 0)
#define CALL3(r, sp, fn, a, b, c) callv(r, sp, fn, 3, reg((int32_t)(a)), reg((int32_t)(b)), reg((int32_t)(c)), 0)
#define CALL4(r, sp, fn, a, b, c, d)                                                                     \
    callv(r, sp, fn, 4, reg((int32_t)(a)), reg((int32_t)(b)), reg((int32_t)(c)), reg((int32_t)(d)))

/* General call: integer registers and float registers, returns the record. */
static EmArea01SysCall callf(Run *r, uint32_t sp, uint32_t fn, unsigned na, const uint64_t *a, unsigned nf,
                             const uint32_t *f)
{
    Out o;
    unsigned i;
    memset(&o, 0, sizeof o);
    o.fn = fn;
    o.sp = sp;
    o.na = na;
    o.nf = nf;
    for (i = 0; i < na && i < 8; i++)
        o.a[i] = a[i];
    for (i = 0; i < nf && i < 4; i++)
        o.f[i] = f[i];
    return invoke(r, &o);
}

/* float(f12) -> f0 */
static uint32_t fcall1(Run *r, uint32_t sp, uint32_t fn, uint32_t x)
{
    uint32_t f[1];
    f[0] = x;
    return callf(r, sp, fn, 0, NULL, 1, f).f0;
}

/* float(f12, f13) -> f0 */
static uint32_t fcall2(Run *r, uint32_t sp, uint32_t fn, uint32_t x, uint32_t y)
{
    uint32_t f[2];
    f[0] = x;
    f[1] = y;
    return callf(r, sp, fn, 0, NULL, 2, f).f0;
}

/* ------------------------------------------------------------------------
 * Constants and callees
 * ---------------------------------------------------------------------- */

#define F_ZERO   0x00000000u
#define F_ONE    0x3F800000u
#define F_TWO    0x40000000u
#define F_HALF   0x3F000000u

#define FN_COPY_QW      0x00102948u /* one quadword copy */
#define FN_COPY_QW4     0x00102958u /* four quadword copy */
#define FN_MAT_VEC      0x001026A0u
#define FN_DOT3         0x00102738u
#define FN_NORM         0x00102760u
#define FN_VSUB         0x001028D0u
#define FN_VADD         0x001028B8u
#define FN_MAT_T        0x00102918u
#define FN_IDENT        0x001029C0u
#define FN_VSCALE       0x00103230u
#define FN_11DE90        0x0011DE90u
#define FN_FABS         0x0011DF78u
#define FN_11E2A8        0x0011E2A8u
#define FN_ATAN2        0x0011E620u
#define FN_SQRT         0x0011E748u
#define FN_RAND         0x00122BB8u
#define FN_WRAP         0x001B1470u
#define FN_APPROACH     0x001B12B0u

/* ------------------------------------------------------------------------
 * 001B0D80: when the float at +0xB4 is below -200.0, state byte +0x04 = 3
 * and the result is 1; otherwise 0.
 * ---------------------------------------------------------------------- */
static int32_t f_1B0D80(Run *r, uint32_t a0)
{
    if (em_ee_c_lt_bits(w32(r, a0 + 0xB4), 0xC3480000u)) {
        st8(r, a0 + 4, 3);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * 001287F0(a0, a1, a2; f12 passed through): when the halfword at a1+0xF8
 * already equals (short)a2, nothing. Otherwise the halfword = a2 and
 * 001C67E0(a0, a2, a2; f12 = the caller's f12, f13 = 0.0).
 * ---------------------------------------------------------------------- */
static void f_1287F0(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, int32_t a2, uint32_t f12)
{
    uint64_t a[3];
    uint32_t f[2];
    sp -= 0x10;
    if (s16(r, a1 + 0xF8) == (int32_t)(int16_t)a2)
        return;
    st16(r, a1 + 0xF8, (uint32_t)a2);
    a[0] = reg((int32_t)a0);
    a[1] = reg(a2);
    a[2] = reg(a2);
    f[0] = f12;
    f[1] = F_ZERO;
    callf(r, sp, 0x001C67E0u, 3, a, 2, f);
}

/* ------------------------------------------------------------------------
 * 00128B80(a0, a1): when the halfword at +0x36 is 0 and D_0081080F is 0 the
 * result is 0. Otherwise bytes +0 = 3, +4 = 2, +5..+7 = 0, 0012E070(a1),
 * and when D_0081080F is non-zero the halfword +0x34 is copied to +0x36;
 * the result is 1.
 * ---------------------------------------------------------------------- */
static int32_t f_128B80(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    sp -= 0x20;
    if (s16(r, a0 + 0x36) == 0 && u8(r, 0x0081080Fu) == 0)
        return 0;
    st8(r, a0 + 0, 3);
    st8(r, a0 + 4, 2);
    st8(r, a0 + 5, 0);
    st8(r, a0 + 6, 0);
    st8(r, a0 + 7, 0);
    CALL1(r, sp, 0x0012E070u, a1);
    if (u8(r, 0x0081080Fu) != 0)
        st16(r, a0 + 0x36, (uint32_t)s16(r, a0 + 0x34));
    return 1;
}

/* ------------------------------------------------------------------------
 * 00157CE0(a0, a1): only when bit 2 (value 4) of byte +0x0B is set,
 * otherwise the result is 0. The scratch vector 0x700038A0 = (0, 0, 8.8,
 * 1.0) and 001B6F00(a0, 0x700038A0; f12 = pi). Then:
 *  - byte +3 == 0x38: D_00810C7F == 0 -> event 0x80000012 in D_00246FB4,
 *    001BA1A0(a1, D_00246F20), 001BA1F0(a0), byte +0 = 2, result 2;
 *    else D_00810CB2 < 2 * (short)+0x34 -> event 0x80000024, same calls,
 *    +0 = 2, result 2; else D_00247274 = 0x42 when +0xB4 < 6.0 +
 *    D_00810354 (0x41 otherwise), D_002472B4 = 0x80000012,
 *    001BA1A0(a1, D_002471E0), 001BA1F0(a0), result 1.
 *  - other types: (short)(D_00810C63 * 30) == D_00810CB4 -> event
 *    0x80000022 via D_00246F20, +0 = 2, result 2; else bit 0 of +0x0B set
 *    -> +0x0A = 1, +0 = 2, result 3; else D_00247734 = 0x8000000C,
 *    001BA1A0(a1, D_002476A0), 001BA1F0(a0), result 1.
 * ---------------------------------------------------------------------- */
static int32_t f_157CE0(Run *r, uint32_t sp, uint32_t a0, int32_t a1)
{
    uint64_t a[2];
    uint32_t f[1];
    sp -= 0x30;
    if (!(u8(r, a0 + 0xB) & 4))
        return 0;
    st32(r, 0x700038A0u, 0);
    st32(r, 0x700038A4u, 0);
    st32(r, 0x700038A8u, 0x410CCCCDu);
    st32(r, 0x700038ACu, F_ONE);
    a[0] = reg((int32_t)a0);
    a[1] = reg((int32_t)0x700038A0u);
    f[0] = 0x40490FDBu;
    callf(r, sp, 0x001B6F00u, 2, a, 1, f);
    if (u8(r, a0 + 3) == 0x38) {
        if (u8(r, 0x00810C7Fu) == 0) {
            st32(r, 0x00246FB4u, 0x80000012u);
            CALL2(r, sp, 0x001BA1A0u, a1, 0x00246F20u);
            CALL1(r, sp, 0x001BA1F0u, a0);
            st8(r, a0, 2);
            return 2;
        }
        if (s16(r, 0x00810CB2u) < s16(r, a0 + 0x34) * 2) {
            st32(r, 0x00246FB4u, 0x80000024u);
            CALL2(r, sp, 0x001BA1A0u, a1, 0x00246F20u);
            CALL1(r, sp, 0x001BA1F0u, a0);
            st8(r, a0, 2);
            return 2;
        }
        if (em_ee_c_lt_bits(w32(r, a0 + 0xB4), em_ee_add_bits(0x40C00000u, w32(r, 0x00810354u))))
            st32(r, 0x00247274u, 0x42);
        else
            st32(r, 0x00247274u, 0x41);
        st32(r, 0x002472B4u, 0x80000012u);
        CALL2(r, sp, 0x001BA1A0u, a1, 0x002471E0u);
        CALL1(r, sp, 0x001BA1F0u, a0);
        return 1;
    }
    if ((int32_t)(int16_t)(u8(r, 0x00810C63u) * 30) == s16(r, 0x00810CB4u)) {
        st32(r, 0x00246FB4u, 0x80000022u);
        CALL2(r, sp, 0x001BA1A0u, a1, 0x00246F20u);
        CALL1(r, sp, 0x001BA1F0u, a0);
        st8(r, a0, 2);
        return 2;
    }
    if (u8(r, a0 + 0xB) & 1) {
        st8(r, a0 + 0xA, 1);
        st8(r, a0, 2);
        return 3;
    }
    st32(r, 0x00247734u, 0x8000000Cu);
    CALL2(r, sp, 0x001BA1A0u, a1, 0x002476A0u);
    CALL1(r, sp, 0x001BA1F0u, a0);
    return 1;
}

/* ------------------------------------------------------------------------
 * 00158590(p, a1, mode). Frame 0x50; a quadword local at sp+0x40.
 *  - mode != -2: local = D_008105D0 - p+0xB0 (001028D0); when its dot
 *    product with p+0xF0 (00102738) is negative, return.
 *  - mode -1: 0x700038A0..A8 = p+0xB0..B8 + 0x700038B0..B8, +AC = 1.0.
 *    mode 1: 00102948 copies p+0xB0 to 0x700038A0, +AC = 1.0.
 *    other non-zero: 0x700038B0 = 1.5 * 0011E2A8(p+0xC4), +B8 = 1.5 *
 *    0011DE90(p+0xC4), +B4 = 0, then the mode -1 sums.
 *    mode 0: 0x700038A0 = (0.4, -0.1, 0.25, 1.0) transformed in place by
 *    the matrix at p+0xD0 (001026A0).
 *  - Then 0x700038B0..BC = (0x80,0,0,0x80) when a1 == 0, else
 *    (0,0x80,0,0x80), and 001F4CC0(0x700038A0, 0x700038B0).
 * ---------------------------------------------------------------------- */
static void spad_sum3(Run *r, uint32_t p)
{
    uint32_t b0 = w32(r, 0x700038B0u), b4 = w32(r, 0x700038B4u), b8 = w32(r, 0x700038B8u);
    st32(r, 0x700038A0u, em_ee_add_bits(w32(r, p + 0xB0), b0));
    st32(r, 0x700038A4u, em_ee_add_bits(w32(r, p + 0xB4), b4));
    st32(r, 0x700038A8u, em_ee_add_bits(w32(r, p + 0xB8), b8));
    st32(r, 0x700038ACu, F_ONE);
}

static void f_158590(Run *r, uint32_t sp, uint32_t p, int32_t a1, int32_t mode)
{
    sp -= 0x50;
    if (mode != -2) {
        uint32_t local = sp + 0x40;
        uint64_t a[2];
        EmArea01SysCall c;
        CALL3(r, sp, FN_VSUB, local, 0x008105D0u, p + 0xB0);
        a[0] = reg((int32_t)local);
        a[1] = reg((int32_t)(p + 0xF0));
        c = callf(r, sp, FN_DOT3, 2, a, 0, NULL);
        if (em_ee_c_lt_bits(c.f0, F_ZERO))
            return;
        if (mode == -1) {
            spad_sum3(r, p);
        } else if (mode == 1) {
            CALL2(r, sp, FN_COPY_QW, 0x700038A0u, p + 0xB0);
            st32(r, 0x700038ACu, F_ONE);
        } else if (mode != 0) {
            uint32_t c0 = fcall1(r, sp, FN_11E2A8, w32(r, p + 0xC4));
            st32(r, 0x700038B0u, em_ee_mul_bits(0x3FC00000u, c0));
            c0 = fcall1(r, sp, FN_11DE90, w32(r, p + 0xC4));
            st32(r, 0x700038B8u, em_ee_mul_bits(0x3FC00000u, c0));
            st32(r, 0x700038B4u, 0);
            spad_sum3(r, p);
        } else {
            st32(r, 0x700038A0u, 0x3ECCCCCDu);
            st32(r, 0x700038A4u, 0xBDCCCCCDu);
            st32(r, 0x700038A8u, 0x3E800000u);
            st32(r, 0x700038ACu, F_ONE);
            CALL3(r, sp, FN_MAT_VEC, 0x700038A0u, p + 0xD0, 0x700038A0u);
        }
    }
    if (a1 == 0) {
        st32(r, 0x700038B0u, 0x80);
        st32(r, 0x700038B4u, 0);
        st32(r, 0x700038B8u, 0);
        st32(r, 0x700038BCu, 0x80);
    } else {
        st32(r, 0x700038B0u, 0);
        st32(r, 0x700038B4u, 0x80);
        st32(r, 0x700038B8u, 0);
        st32(r, 0x700038BCu, 0x80);
    }
    CALL2(r, sp, 0x001F4CC0u, 0x700038A0u, 0x700038B0u);
}

/* ------------------------------------------------------------------------
 * 00158D30(p): on the state byte +4.
 *  0: 001B0FD0(p); +0x80..+0x88 = 2.0; 001C6380(p).
 *  1: by the halfword +0x2E: 0 -> 0x700038B0..BC = (0x80,0,0,0x80) when
 *     +0x0B is 0, else (0,0x80,0,0x80); 1 -> (0,0x80,0,0x80); others leave
 *     them. 0x700038C0..CC = 1.0; when 001B17A0(p) is non-zero the pointer
 *     at +0x4C is called with p; 001F4A10(p+0xD0, 0x700038B0, 0x700038C0).
 *  2, 3: 001AFC10(p).
 * ---------------------------------------------------------------------- */
static void b0_words(Run *r, int second)
{
    st32(r, 0x700038B0u, second ? 0 : 0x80);
    st32(r, 0x700038B4u, second ? 0x80 : 0);
    st32(r, 0x700038B8u, 0);
    st32(r, 0x700038BCu, 0x80);
}

static void f_158D30(Run *r, uint32_t sp, uint32_t p)
{
    uint32_t t;
    sp -= 0x20;
    t = u8(r, p + 4);
    if (t == 3 || t == 2) {
        CALL1(r, sp, 0x001AFC10u, p);
        return;
    }
    if (t == 0) {
        CALL1(r, sp, 0x001B0FD0u, p);
        st32(r, p + 0x80, F_TWO);
        st32(r, p + 0x84, F_TWO);
        st32(r, p + 0x88, F_TWO);
        CALL1(r, sp, 0x001C6380u, p);
        return;
    }
    if (t != 1)
        return;
    switch (u16(r, p + 0x2E)) {
    case 0:
        b0_words(r, u8(r, p + 0xB) != 0);
        break;
    case 1:
        b0_words(r, 1);
        break;
    default:
        break;
    }
    st32(r, 0x700038C0u, F_ONE);
    st32(r, 0x700038C4u, F_ONE);
    st32(r, 0x700038C8u, F_ONE);
    st32(r, 0x700038CCu, F_ONE);
    if (CALL1(r, sp, 0x001B17A0u, p) != 0)
        CALL1(r, sp, w32(r, p + 0x4C), p);
    CALL3(r, sp, 0x001F4A10u, p + 0xD0, 0x700038B0u, 0x700038C0u);
}

/* ------------------------------------------------------------------------
 * 001A8840(a0, a1): the box contact of a0's point (+0xA0..A8) with a1's
 * position (+0xB0..B8) against the extents at *(a1+0x30): |dx| <= ext[0],
 * |dz| <= ext[2], |dy| <= 1.5 + ext[1] (0011DF78 is the absolute value; the
 * extents are re-read after each call). On contact, by a1's byte +0x0D:
 * 0 -> a1+0x0A = 1, 00187EC0(6, a1's byte +0x56), and when a1+0x0B is set,
 * D_00810707 != 1 and a0's byte +0 == 1: a0+0x22C = 5.0, a0+0 = 3;
 * 1 -> 00187EC0(7, 0). Any contact clears the halfword 0x70003B86.
 * ---------------------------------------------------------------------- */
static void f_1A8840(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    uint32_t v, t;
    sp -= 0x30;
    v = fcall1(r, sp, FN_FABS, em_ee_sub_bits(w32(r, a0 + 0xA0), w32(r, a1 + 0xB0)));
    if (!em_ee_c_le_bits(v, w32(r, w32(r, a1 + 0x30))))
        return;
    v = fcall1(r, sp, FN_FABS, em_ee_sub_bits(w32(r, a0 + 0xA8), w32(r, a1 + 0xB8)));
    if (!em_ee_c_le_bits(v, w32(r, w32(r, a1 + 0x30) + 8)))
        return;
    v = fcall1(r, sp, FN_FABS, em_ee_sub_bits(w32(r, a0 + 0xA4), w32(r, a1 + 0xB4)));
    if (!em_ee_c_le_bits(v, em_ee_add_bits(0x3FC00000u, w32(r, w32(r, a1 + 0x30) + 4))))
        return;
    t = u8(r, a1 + 0xD);
    if (t == 0) {
        st8(r, a1 + 0xA, 1);
        CALL2(r, sp, 0x00187EC0u, 6, u8(r, a1 + 0x56));
        if (u8(r, a1 + 0xB) != 0 && u8(r, 0x00810707u) != 1 && u8(r, a0) == 1) {
            st32(r, a0 + 0x22C, 0x40A00000u);
            st8(r, a0, 3);
        }
    } else if (t == 1) {
        CALL2(r, sp, 0x00187EC0u, 7, 0);
    }
    st16(r, 0x70003B86u, 0);
}

/* ------------------------------------------------------------------------
 * 001A9E00(a0, a1): dx = a0+0xA0 - a1+0xB0, dz = a0+0xA8 - a1+0xB8,
 * d = 0011E748(dx*dx + dz*dz) (accumulator multiply-add), sum = r0 + r1
 * (the first floats at *(a0+0x30) and *(a1+0x30)). When d <= sum:
 * h = r0[1] / 2; e = |a0+0xA4 + h - a1+0xB4| (negated when negative); when
 * e <= h + r1[1] / 2 and a1's byte +3 is 0: ang = 001B1470(0011E620(dx,
 * dz)); s = sum * 0011DE90(ang); a1+0xB0 = a0+0xA0 - sum * 0011E2A8(ang);
 * a1+0xB8 = a0+0xA8 - s; a1+0x0B = 1 unless bit 2 of a0's byte +0 is set.
 * ---------------------------------------------------------------------- */
static void f_1A9E00(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    uint32_t dx, dz, d, sum, h, e, p0, p1, ang, s, c;
    sp -= 0x40;
    dx = em_ee_sub_bits(w32(r, a0 + 0xA0), w32(r, a1 + 0xB0));
    dz = em_ee_sub_bits(w32(r, a0 + 0xA8), w32(r, a1 + 0xB8));
    d = fcall1(r, sp, FN_SQRT, em_ee_madd_bits(em_ee_mula_bits(dx, dx), dz, dz));
    p0 = w32(r, a0 + 0x30);
    p1 = w32(r, a1 + 0x30);
    sum = em_ee_add_bits(w32(r, p0), w32(r, p1));
    if (!em_ee_c_le_bits(d, sum))
        return;
    h = em_ee_div_bits(w32(r, p0 + 4), F_TWO);
    e = em_ee_sub_bits(em_ee_add_bits(w32(r, a0 + 0xA4), h), w32(r, a1 + 0xB4));
    if (em_ee_c_lt_bits(e, F_ZERO))
        e = em_ee_neg_bits(e);
    if (!em_ee_c_le_bits(e, em_ee_add_bits(h, em_ee_div_bits(w32(r, p1 + 4), F_TWO))))
        return;
    if (u8(r, a1 + 3) != 0)
        return;
    ang = fcall1(r, sp, FN_WRAP, fcall2(r, sp, FN_ATAN2, dx, dz));
    s = em_ee_mul_bits(sum, fcall1(r, sp, FN_11DE90, ang));
    c = em_ee_mul_bits(sum, fcall1(r, sp, FN_11E2A8, ang));
    st32(r, a1 + 0xB0, em_ee_sub_bits(w32(r, a0 + 0xA0), c));
    st32(r, a1 + 0xB8, em_ee_sub_bits(w32(r, a0 + 0xA8), s));
    if (!(u8(r, a0) & 4))
        st8(r, a1 + 0xB, 1);
}

/* ------------------------------------------------------------------------
 * 001AA000(a0, a1, a2, a3): when the words a2+0xE4 and a3+0xE4 differ,
 * nothing. When they are equal to 0x200 the halfword 0x70003B86 is cleared
 * and nothing else. Otherwise: dx = a0+0xB0 - a1+0xB0, dz = a0+0xB8 -
 * a1+0xB8, d = 0011E748(dx*dx + dz*dz) (accumulator form), sum = r0 + r1
 * from *(a0+0x30) / *(a1+0x30); when d <= sum: dy = a0+0xB4 - a1+0xB4 and
 * when 0011E748(dy*dy) <= r0[1] + r1[1]: ang = 001B1470(0011E620(dx, dz)),
 * s = sum * 0011DE90(ang), c = sum * 0011E2A8(ang), a0+0xB0 = a1+0xB0 + c,
 * a0+0xB8 = a1+0xB8 + s, and the halfword 0x70003B86 is cleared.
 * ---------------------------------------------------------------------- */
static void f_1AA000(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t k, dx, dz, d, sum, dy, ang, s, c, p0, p1;
    sp -= 0x40;
    k = w32(r, a2 + 0xE4);
    if (k != w32(r, a3 + 0xE4))
        return;
    if (k == 0x200) {
        st16(r, 0x70003B86u, 0);
        return;
    }
    dx = em_ee_sub_bits(w32(r, a0 + 0xB0), w32(r, a1 + 0xB0));
    dz = em_ee_sub_bits(w32(r, a0 + 0xB8), w32(r, a1 + 0xB8));
    d = fcall1(r, sp, FN_SQRT, em_ee_madd_bits(em_ee_mula_bits(dx, dx), dz, dz));
    p0 = w32(r, a0 + 0x30);
    p1 = w32(r, a1 + 0x30);
    sum = em_ee_add_bits(w32(r, p0), w32(r, p1));
    if (!em_ee_c_le_bits(d, sum))
        return;
    dy = em_ee_sub_bits(w32(r, a0 + 0xB4), w32(r, a1 + 0xB4));
    d = fcall1(r, sp, FN_SQRT, em_ee_mul_bits(dy, dy));
    p0 = w32(r, a0 + 0x30);
    p1 = w32(r, a1 + 0x30);
    if (!em_ee_c_le_bits(d, em_ee_add_bits(w32(r, p0 + 4), w32(r, p1 + 4))))
        return;
    ang = fcall1(r, sp, FN_WRAP, fcall2(r, sp, FN_ATAN2, dx, dz));
    s = em_ee_mul_bits(sum, fcall1(r, sp, FN_11DE90, ang));
    c = em_ee_mul_bits(sum, fcall1(r, sp, FN_11E2A8, ang));
    st32(r, a0 + 0xB0, em_ee_add_bits(w32(r, a1 + 0xB0), c));
    st32(r, a0 + 0xB8, em_ee_add_bits(w32(r, a1 + 0xB8), s));
    st16(r, 0x70003B86u, 0);
}

/* ------------------------------------------------------------------------
 * 001B0300(): the active scene entry p = D_0024D650[D_00810700]
 * [D_00810701] + D_00810702 * 0x30. D_008101E7 = 0; its float +0x18 goes to
 * D_00810244 and D_008101EC; byte +0x10: bit 7 -> D_008101E5 (1/0), the low
 * seven bits -> D_008101E6. When enabled: the three floats of row
 * D_0024A8D0 + (word +0x10 >> 8) * 12 go to D_008101F0..F8, D_008101FC =
 * 1.0, 00102948(D_00810200, D_00810350) (the third register holds the row
 * address), D_00810204 += 15.0, 00102948(D_008105E0, D_00810200),
 * 00102948(D_008105D0, D_008101F0). Always 001DD980(D_008105D0,
 * D_008105E0).
 * ---------------------------------------------------------------------- */
static void f_1B0300(Run *r, uint32_t sp)
{
    const uint32_t base = 0x008101E0u;
    uint32_t p, f, v1;
    sp -= 0x20;
    p = w32(r, w32(r, 0x0024D650u + u8(r, 0x00810700u) * 4) + u8(r, 0x00810701u) * 4);
    p += u8(r, 0x00810702u) * 0x30;
    st8(r, 0x008101E7u, 0);
    f = w32(r, p + 0x18);
    st32(r, 0x00810244u, f);
    st32(r, 0x008101ECu, f);
    v1 = u8(r, p + 0x10);
    st8(r, base + 5, (v1 & 0x80) ? 1 : 0);
    st8(r, base + 6, v1 & 0x7F);
    if (u8(r, base + 5) == 1) {
        uint32_t m = 0x0024A8D0u + (uint32_t)(((int32_t)w32(r, p + 0x10) >> 8) * 0xC);
        st32(r, base + 0x10, w32(r, m));
        st32(r, base + 0x14, w32(r, m + 4));
        st32(r, base + 0x18, w32(r, m + 8));
        st32(r, base + 0x1C, F_ONE);
        CALL3(r, sp, FN_COPY_QW, base + 0x20, 0x00810350u, m);
        st32(r, base + 0x24, em_ee_add_bits(w32(r, base + 0x24), 0x41700000u));
        CALL2(r, sp, FN_COPY_QW, 0x008105E0u, base + 0x20);
        CALL2(r, sp, FN_COPY_QW, 0x008105D0u, base + 0x10);
    }
    CALL2(r, sp, 0x001DD980u, 0x008105D0u, 0x008105E0u);
}

/* ------------------------------------------------------------------------
 * 001B6D70(a0, a1, a2): command record a2 (+0x08 opcode, +0x18 id, +0x20
 * float). 6: 001FAE70(id); 3: 001FBC50(); 4: 001FA790(0, id); 5:
 * 001FABB0(); 1: 001FBD50(a0, id, 0; f12 = +0x20); 0: 001FBD50(a0, id, 0;
 * f12 = 300.0); 2 and every opcode >= 7 (unsigned): 001FB9F0(id, 0x1000,
 * 0x1000, 0x1000). The result is 1. 001FA790 / 001FABB0 / 001FAE70 are
 * translated in em_stream_lanes_original; every callee is a request whose
 * IOP side is outside this module (only the EE-side call is reproduced).
 * ---------------------------------------------------------------------- */
static int32_t f_1B6D70(Run *r, uint32_t sp, uint32_t a0, uint32_t a2)
{
    uint32_t op = w32(r, a2 + 8);
    uint64_t a[3];
    uint32_t f[1];
    sp -= 0x10;
    switch (op) {
    case 6:
        CALL1(r, sp, 0x001FAE70u, w32(r, a2 + 0x18));
        break;
    case 3:
        CALL0(r, sp, 0x001FBC50u);
        break;
    case 4:
        CALL2(r, sp, 0x001FA790u, 0, w32(r, a2 + 0x18));
        break;
    case 5:
        CALL0(r, sp, 0x001FABB0u);
        break;
    case 1:
    case 0:
        a[0] = reg((int32_t)a0);
        a[1] = reg((int32_t)w32(r, a2 + 0x18));
        a[2] = 0;
        f[0] = op == 1 ? w32(r, a2 + 0x20) : 0x43960000u;
        callf(r, sp, 0x001FBD50u, 3, a, 1, f);
        break;
    default:
        CALL4(r, sp, 0x001FB9F0u, w32(r, a2 + 0x18), 0x1000, 0x1000, 0x1000);
        break;
    }
    return 1;
}

/* ------------------------------------------------------------------------
 * 001B76D0(a0, a1, a2): 001B1E20(word a2+0x14, word a2+0x18); result 1.
 * ---------------------------------------------------------------------- */
static int32_t f_1B76D0(Run *r, uint32_t sp, uint32_t a2)
{
    sp -= 0x10;
    CALL2(r, sp, 0x001B1E20u, w32(r, a2 + 0x14), w32(r, a2 + 0x18));
    return 1;
}

/* ------------------------------------------------------------------------
 * 001E7CB0(): 1 unless D_00810700 == 0x13 and D_00810702 is one of 4, 5,
 * 7, 8, 9 (then 0).
 * ---------------------------------------------------------------------- */
static int32_t f_1E7CB0(Run *r)
{
    uint32_t w;
    if (u8(r, 0x00810700u) != 0x13)
        return 1;
    w = u8(r, 0x00810702u);
    return (w == 4 || w == 5 || w == 7 || w == 8 || w == 9) ? 0 : 1;
}

/* ------------------------------------------------------------------------
 * 00159B90(p): on the state byte +4 (e = p + 0x1F0 is passed along as the
 * original's second register).
 *  0: when 001B0FD0(p) is 0: 001C6380(p), +0x0A = 0, +0x30 = 0x00275460,
 *     halfword +0x34 = 2, +0 = 1, 0x700038A0 = (0, 1, 0, 1) and
 *     001C5570(p, 0x700038A0, 0x76, 1).
 *  1: by the sub-state +5:
 *     0 -> r = 00157CE0(p, e); when r != 0: 00102948(D_00810710,
 *          D_00810350), 00102948(D_00810720, D_00810370), 001FB9F0(1000,
 *          0x1000, 0x1000, 0x1000); +5 = 2 for r == 2, +5 + 1 for r == 3,
 *          else 3;
 *     1 -> +5 = 2, 001BA1A0(e, D_002470E0);
 *     2 -> when 001BA1F0(p) != 0: +0x0B = 0, +0 = 1, +5 = 0;
 *     3 -> when 001BA1F0(p) != 0: +5 = 1;
 *     then for x = -1.75 and x = 1.75: 0x700038A0 = (x, 1.7, 2.03, 1.0)
 *     transformed in place by the matrix at p+0xD0 (001026A0) and
 *     00158590(p, 1, -2); then 001B17A0(p) and the pointer at +0x4C is
 *     called with p.
 *  2: +4 = 3.   3: 001AFC10(p).
 * ---------------------------------------------------------------------- */
static void f_159B90(Run *r, uint32_t sp, uint32_t p)
{
    uint32_t st, e = p + 0x1F0;
    sp -= 0x30;
    st = u8(r, p + 4);
    if (st == 3) {
        CALL1(r, sp, 0x001AFC10u, p);
        return;
    }
    if (st == 2) {
        st8(r, p + 4, st + 1);
        return;
    }
    if (st == 0) {
        if (CALL1(r, sp, 0x001B0FD0u, p) != 0)
            return;
        CALL1(r, sp, 0x001C6380u, p);
        st8(r, p + 0xA, 0);
        st32(r, p + 0x30, 0x00275460u);
        st16(r, p + 0x34, 2);
        st8(r, p + 0, 1);
        st32(r, 0x700038A0u, 0);
        st32(r, 0x700038A4u, F_ONE);
        st32(r, 0x700038A8u, 0);
        st32(r, 0x700038ACu, F_ONE);
        CALL4(r, sp, 0x001C5570u, p, 0x700038A0u, 0x76, 1);
        return;
    }
    if (st != 1)
        return;
    switch (u8(r, p + 5)) {
    case 0: {
        int32_t v = f_157CE0(r, sp, p, (int32_t)e);
        if (v != 0) {
            CALL2(r, sp, FN_COPY_QW, 0x00810710u, 0x00810350u);
            CALL2(r, sp, FN_COPY_QW, 0x00810720u, 0x00810370u);
            CALL4(r, sp, 0x001FB9F0u, 0x3E8, 0x1000, 0x1000, 0x1000);
            if (v == 2)
                st8(r, p + 5, 2);
            else if (v == 3)
                st8(r, p + 5, u8(r, p + 5) + 1);
            else
                st8(r, p + 5, 3);
        }
        break;
    }
    case 1:
        st8(r, p + 5, 2);
        CALL2(r, sp, 0x001BA1A0u, e, 0x002470E0u);
        break;
    case 2:
        if (CALL1(r, sp, 0x001BA1F0u, p) != 0) {
            st8(r, p + 0xB, 0);
            st8(r, p + 0, 1);
            st8(r, p + 5, 0);
        }
        break;
    case 3:
        if (CALL1(r, sp, 0x001BA1F0u, p) != 0)
            st8(r, p + 5, 1);
        break;
    default:
        break;
    }
    st32(r, 0x700038A0u, 0xBFE00000u);
    st32(r, 0x700038A4u, 0x3FD9999Au);
    st32(r, 0x700038A8u, 0x4001EB85u);
    st32(r, 0x700038ACu, F_ONE);
    CALL3(r, sp, FN_MAT_VEC, 0x700038A0u, p + 0xD0, 0x700038A0u);
    f_158590(r, sp, p, 1, -2);
    st32(r, 0x700038A0u, 0x3FE00000u);
    st32(r, 0x700038A4u, 0x3FD9999Au);
    st32(r, 0x700038A8u, 0x4001EB85u);
    st32(r, 0x700038ACu, F_ONE);
    CALL3(r, sp, FN_MAT_VEC, 0x700038A0u, p + 0xD0, 0x700038A0u);
    f_158590(r, sp, p, 1, -2);
    CALL1(r, sp, 0x001B17A0u, p);
    CALL1(r, sp, w32(r, p + 0x4C), p);
}

/* ------------------------------------------------------------------------
 * 0015A2C0(p): on the state byte +4.
 *  0: row = D_00248120 + (short)+0x54 * 20 goes to +0x30; +0x0C = +9 = 0,
 *     +0 = +4 = 1, +0x1F0 = 0, +0x1F4 = 00122BB8(); 0x700038A0 = 2 *
 *     row[0], 0x700038A8 = 2 * row[2]; 001E9580(p, halfword +0x0E,
 *     0x700038A0); +0x0A = 0, halfword +0x2E = 0, +0x20 = 0. By the halfword
 *     +0x56: 1 (2) -> +0x56 = byte D_002481B0 (D_002481D0) [(word
 *     0x70003B68 & 3) * 8 + (D_008106EC (D_008106ED) & 7)], that counter
 *     + 1, and when the new +0x56 is 1: 0015A200(p, 14, 0), 0015A200(p, 14,
 *     1). Finally +0x80 = 0 and +0x0B = 0.
 *  1: 001E9E60(p, halfword +0x0E). With +0x56 == 1, by +5:
 *     0 -> +0x80 = +0x20 / 100; when +0x0A: +0x20 += 1 and once it is not
 *          below 100: +5 + 1, +0x20 = 60, +0x80 = 1.0, +0x0B = 1; without
 *          +0x0A: +0x20 = 0;
 *     1 -> when (word 0x70003B64 & 0x7F) == 0: 001FBD50(p, 0x42F, 0;
 *          300.0); +0x80 = +0x20 / 60; 0015A750(p); when +0x0A: +0x20 =
 *          60; otherwise +0x20 -= 1 and once it is <= 0: +0x80 = 0, +5 = 0,
 *          +0x0B = 0, +0x1F0 = 0.
 *     With +0x56 == 2, by +5: 0 -> when +0x0A: +0x20 += 1 and once it is
 *     above 120: 0015A200(p, 13, 0) != 0 adds 1 to halfword +0x2E; +5 = 2
 *     when +0x2E >= 4, else +5 = 1 and +0x20 = D_002481F0[((00122BB8() >>
 *     16) * 3) >> 15]; without +0x0A: +0x20 = 0. 1 -> +0x20 -= 1 and once
 *     it is <= 0: +5 = 0. Then 001B17A0(p) and +0x0A = 0.
 *  2, 3: 001AFC10(p).
 * ---------------------------------------------------------------------- */
static void pick_link(Run *r, uint32_t sp, uint32_t p, uint32_t table, uint32_t counter)
{
    uint32_t row = w32(r, 0x70003B68u) & 3;
    uint32_t v = u8(r, table + row * 8 + (u8(r, counter) & 7));
    st16(r, p + 0x56, v);
    st8(r, counter, u8(r, counter) + 1);
    if (s16(r, p + 0x56) == 1) {
        CALL3(r, sp, 0x0015A200u, p, 0xE, 0);
        CALL3(r, sp, 0x0015A200u, p, 0xE, 1);
    }
}

static void f_15A2C0(Run *r, uint32_t sp, uint32_t p)
{
    uint32_t st, tbl, f;
    int32_t link;
    sp -= 0x30;
    st = u8(r, p + 4);
    if (st == 3 || st == 2) {
        CALL1(r, sp, 0x001AFC10u, p);
        return;
    }
    if (st == 0) {
        tbl = 0x00248120u + (uint32_t)(s16(r, p + 0x54) * 20);
        st32(r, p + 0x30, tbl);
        st8(r, p + 0xC, 0);
        st8(r, p + 9, 0);
        st8(r, p + 0, 1);
        st8(r, p + 4, 1);
        st32(r, p + 0x1F0, 0);
        st32(r, p + 0x1F4, (uint32_t)CALL0(r, sp, FN_RAND));
        st32(r, 0x700038A0u, em_ee_mul_bits(F_TWO, w32(r, tbl)));
        st32(r, 0x700038A8u, em_ee_mul_bits(F_TWO, w32(r, tbl + 8)));
        CALL3(r, sp, 0x001E9580u, p, u16(r, p + 0xE), 0x700038A0u);
        st8(r, p + 0xA, 0);
        st16(r, p + 0x2E, 0);
        st32(r, p + 0x20, 0);
        link = s16(r, p + 0x56);
        if (link == 2)
            pick_link(r, sp, p, 0x002481D0u, 0x008106EDu);
        else if (link == 1)
            pick_link(r, sp, p, 0x002481B0u, 0x008106ECu);
        st32(r, p + 0x80, 0);
        st8(r, p + 0xB, 0);
        return;
    }
    if (st != 1)
        return;
    CALL2(r, sp, 0x001E9E60u, p, u16(r, p + 0xE));
    link = s16(r, p + 0x56);
    if (link == 1) {
        uint32_t sub = u8(r, p + 5);
        if (sub == 0) {
            st32(r, p + 0x80, em_ee_div_bits(w32(r, p + 0x20), 0x42C80000u));
            if (u8(r, p + 0xA) != 0) {
                f = em_ee_add_bits(w32(r, p + 0x20), F_ONE);
                st32(r, p + 0x20, f);
                if (!em_ee_c_lt_bits(f, 0x42C80000u)) {
                    st8(r, p + 5, u8(r, p + 5) + 1);
                    st32(r, p + 0x20, 0x42700000u);
                    st32(r, p + 0x80, F_ONE);
                    st8(r, p + 0xB, 1);
                }
            } else {
                st32(r, p + 0x20, 0);
            }
        } else if (sub == 1) {
            if ((w32(r, 0x70003B64u) & 0x7F) == 0) {
                uint64_t a[3];
                uint32_t fl[1];
                a[0] = reg((int32_t)p);
                a[1] = 0x42F;
                a[2] = 0;
                fl[0] = 0x43960000u;
                callf(r, sp, 0x001FBD50u, 3, a, 1, fl);
            }
            st32(r, p + 0x80, em_ee_div_bits(w32(r, p + 0x20), 0x42700000u));
            CALL1(r, sp, 0x0015A750u, p);
            if (u8(r, p + 0xA) != 0) {
                st32(r, p + 0x20, 0x42700000u);
            } else {
                f = em_ee_sub_bits(w32(r, p + 0x20), F_ONE);
                st32(r, p + 0x20, f);
                if (em_ee_c_le_bits(f, F_ZERO)) {
                    st32(r, p + 0x80, 0);
                    st8(r, p + 5, 0);
                    st8(r, p + 0xB, 0);
                    st32(r, p + 0x1F0, 0);
                }
            }
        }
    } else if (link == 2) {
        uint32_t sub = u8(r, p + 5);
        if (sub == 1) {
            f = em_ee_sub_bits(w32(r, p + 0x20), F_ONE);
            st32(r, p + 0x20, f);
            if (em_ee_c_le_bits(f, F_ZERO))
                st8(r, p + 5, 0);
        } else if (sub == 0) {
            if (u8(r, p + 0xA) == 0) {
                st32(r, p + 0x20, 0);
            } else {
                f = em_ee_add_bits(w32(r, p + 0x20), F_ONE);
                st32(r, p + 0x20, f);
                if (!em_ee_c_le_bits(f, 0x42F00000u)) {
                    if (CALL3(r, sp, 0x0015A200u, p, 0xD, 0) != 0)
                        st16(r, p + 0x2E, u16(r, p + 0x2E) + 1);
                    if ((int32_t)u16(r, p + 0x2E) >= 4) {
                        st8(r, p + 5, 2);
                    } else {
                        int32_t v;
                        st8(r, p + 5, 1);
                        v = CALL0(r, sp, FN_RAND);
                        v = ((v >> 16) * 3) >> 15;
                        st32(r, p + 0x20, w32(r, 0x002481F0u + (uint32_t)(v * 4)));
                    }
                }
            }
        }
    }
    CALL1(r, sp, 0x001B17A0u, p);
    st8(r, p + 0xA, 0);
}

/* ------------------------------------------------------------------------
 * 00128C10(e), b = e + 0x1F0, the record at D_008102B0 (its point +0xA0 is
 * the 001B13F0 reference). On the state byte +4:
 *  0: by +5: 0 -> when 00128AB0(e, b) != 0: +5 + 1, halfword b+0xF8 = 1,
 *     001C63E0(e, 1), halfword +0x28 = 0. 1 -> when 00129780(e, b, +0x0D)
 *     != 0: halfword +0x54 = 0 and +0 = 1 unless +0x0D is 4 or 9.
 *  1: nothing unless 001B2140(e) != 0; nothing when the byte 0x70003B8D is
 *     2 or more. +0x54 = 0, 001029C0(0x70003000); every 64th frame (word
 *     0x70003B68 + halfword 0x70003B8A) 001B0D80(e) != 0 ends the frame.
 *     001B17A0(e). When 00128B80(e, b) is 0: b+0xFA = 0 and by the kind
 *     +0x0D (below 10):
 *       0..3, 5..8, by +5:
 *         0: b+0xFB = 2, 001287F0(e, b, 1; 4.0), b+0xEC = 1.0. With bit 0
 *            of +0x0A: b+0xE8 = 001B1470(2 pi * (00122BB8() & 0xF0) /
 *            256), +5 = 2. Otherwise 001B13F0(0x00810350, e+0xB0; r) with
 *            r = 100 for kinds below 4, else 20 (b+0xE4 >> 8 == 1) or 40:
 *            when non-zero +5 + 1 and b+0xD0 = 0. Then when +1 is set:
 *            0x700038A0 = (0, -1.4, 0, 1) and 001C25E0(e, 0x700038A0) == 0
 *            sets +5 = 8, +6 = +7 = 0.
 *         1: b+0xFB = 2, 001287F0(e, b, v; 4.0) with v = the
 *            halfword D_00242F20[kind * 4 bytes], b+0xEC = 1.0. Bit 0 of
 *            +0x0A: as in 0 (random heading, +5 = 2). Otherwise when
 *            001B13F0(..., 150) is 0: b+0xD0 + 1 and +5 = 0 once it reaches
 *            90; else b+0xD0 = 0 and 001B13F0(..., 10 or 24) != 0 gives
 *            +5 + 1 and a random heading. Then when +5 < 2 and +1 is set:
 *            0x700038A0 = (0, -4, 0, 1) and the 001C25E0 test as above.
 *         2: 001287F0(e, b, v; 4.0), b+0xFB = 2, b+0xFA = 1, +0xC4 =
 *            001B12B0(b+0xE8, +0xC4, 0.34906587); when it equals b+0xE8:
 *            +5 + 1, +6 = 0, b+0xD8 = 0.8, b+0xEC = 2.6, b+0xD0 =
 *            D_00242EB6[00128600(0)].
 *         3: b+0xFB = 3; by +6: 0 -> +0xC4 = 001B12B0(b+0xE8, +0xC4,
 *            0.06981317), 001287F0(e, b, 6; 4.0), b+0xFA = 1, b+0xEC =
 *            2.6, b+0xD8 = 0.8, b+0xD0 - 1 and when it reaches 0:
 *            001287F0(e, b, v; 4.0), b+0xEC = 1.0, b+0xD8 = 0, b+0xE8 =
 *            001B1470(b+0xE8 +- 0.69813174 by bit 4 of 00122BB8()), b+0xD0 =
 *            D_00242EB0[00128600(0)], +6 + 1. 1 -> b+0xD0 - 1 and at 0:
 *            b+0xEC = 2.6, b+0xD8 = 0.8, 001287F0(e, b, 6; 4.0), +6 = 0,
 *            b+0xD0 = D_00242EBC[00128600(0)]. Then when +1 is clear: +0 =
 *            2, +4 = 3, +5 = +6 = 0.
 *         4: b+0xFB = 0, 001287F0(e, b, 6; 0.0), b+0xD0 - 1 and at 0:
 *            00102948(b+0x50, e+0xB0), b+0xEC = 1.0, b+0xD8 = 0, +5 = 0.
 *         8: b+0xFB = 0x80, r = 001C2770(e, b, 2), 0012D580(e, b, r),
 *            001C3D60(e, b) when r == 0.
 *       4, 9: +0x0D = 3 (8), b+0xEC = 1.6, b+0xD0 = (00122BB8() & 0x30) +
 *         60, b+0xD8 = 0.6, +5 = 4, +0 = 1, 00102948(b+0x50, e+0xB0).
 *     Then when +5 != 8 and 001C2770(e, b, 0) == 0: 001C3D60(e, b).
 *     b+0xF4 = 001C64F0(e; b+0xEC); 00102958(0x70003400, 0x70003000);
 *     001C69A0(e); when +1 is set: 001288D0(e, b) when (signed) b+0xFA is
 *     non-zero, and the pointer at +0x4C is called with e. +0x0B = +0x0A =
 *     0.
 *  2: the 64th-frame 001B0D80 test as in 1; 00129FC0(e, b); +0x0B = +0x0A
 *     = 0.
 *  3: when b+0xE0 is set: +0 = 2, +4 = 4, +5 = 0, 00102948(e+0xB0,
 *     b+0x50); otherwise 001B1190(byte +0x9A), 001AFC10(e).
 *  4: by +5: 0 -> +0x60..+0x6C = 1.0, 001289C0(e, b), b+0xF8 = 1,
 *     001C63E0(e, 1), +0x28 = 0, +5 + 1. 1 -> when 00129780(e, b, +0x0D)
 *     != 0: +4 = 4, +5 = 2, +0x28 = D_00275380[((00122BB8() >> 16) * 4)
 *     >> 15]. 2 -> +0x28 - 1 and at 0: +5 = 3. 3 -> when 001B13F0(...,
 *     100) is 0 and the low byte of 001B1630(+0xB0, +0xB4, +0xB8) is 0: +0
 *     = +4 = 1, +5 = +6 = +7 = 0.
 * ---------------------------------------------------------------------- */
#define E_POINT 0x00810350u /* D_008102B0 + 0xA0 */

static int32_t near_test(Run *r, uint32_t sp, uint32_t e, uint32_t radius)
{
    uint64_t a[2];
    uint32_t f[1];
    a[0] = reg((int32_t)E_POINT);
    a[1] = reg((int32_t)(e + 0xB0));
    f[0] = radius;
    return (int32_t)(uint32_t)callf(r, sp, 0x001B13F0u, 2, a, 1, f).v0;
}

static uint32_t random_heading(Run *r, uint32_t sp)
{
    int32_t v = CALL0(r, sp, FN_RAND) & 0xF0;
    uint32_t x = em_ee_mul_bits(0x40C90FDBu, em_ee_cvt_s_w_bits((uint32_t)v));
    return fcall1(r, sp, FN_WRAP, em_ee_div_bits(x, 0x43800000u));
}

static uint32_t approach(Run *r, uint32_t sp, uint32_t goal, uint32_t cur, uint32_t rate)
{
    uint32_t f[3];
    f[0] = goal;
    f[1] = cur;
    f[2] = rate;
    return callf(r, sp, FN_APPROACH, 0, NULL, 3, f).f0;
}

static void ground_test(Run *r, uint32_t sp, uint32_t e, uint32_t down)
{
    st32(r, 0x700038A0u, 0);
    st32(r, 0x700038A4u, down);
    st32(r, 0x700038A8u, 0);
    st32(r, 0x700038ACu, F_ONE);
    if (CALL2(r, sp, 0x001C25E0u, e, 0x700038A0u) == 0) {
        st8(r, e + 5, 8);
        st8(r, e + 6, 0);
        st8(r, e + 7, 0);
    }
}

static int32_t table_2F20(Run *r, uint32_t kind) { return s16(r, 0x00242F20u + kind * 4); }

static int32_t every_64th(Run *r)
{
    int32_t c = s16(r, 0x70003B8Au);
    return ((w32(r, 0x70003B68u) + (uint32_t)c) & 0x3F) == 0;
}

static void kind_step(Run *r, uint32_t sp, uint32_t e, uint32_t b, uint32_t kind)
{
    int32_t c;
    switch (u8(r, e + 5)) {
    case 0:
        st8(r, b + 0xFB, 2);
        f_1287F0(r, sp, e, b, 1, 0x40800000u);
        st32(r, b + 0xEC, F_ONE);
        if (u8(r, e + 0xA) & 1) {
            st32(r, b + 0xE8, random_heading(r, sp));
            st8(r, e + 5, 2);
            break;
        }
        if ((int32_t)u8(r, e + 0xD) < 4) {
            if (near_test(r, sp, e, 0x42C80000u) != 0) {
                st8(r, e + 5, u8(r, e + 5) + 1);
                st16(r, b + 0xD0, 0);
            }
        } else {
            uint32_t radius = ((int32_t)w32(r, b + 0xE4) >> 8) == 1 ? 0x41A00000u : 0x42200000u;
            if (near_test(r, sp, e, radius) != 0) {
                st8(r, e + 5, u8(r, e + 5) + 1);
                st16(r, b + 0xD0, 0);
            }
        }
        if (u8(r, e + 1) != 0)
            ground_test(r, sp, e, 0xBFB33333u);
        break;
    case 1:
        st8(r, b + 0xFB, 2);
        f_1287F0(r, sp, e, b, table_2F20(r, u8(r, e + 0xD)), 0x40800000u);
        st32(r, b + 0xEC, F_ONE);
        if (u8(r, e + 0xA) & 1) {
            st32(r, b + 0xE8, random_heading(r, sp));
            st8(r, e + 5, 2);
            break;
        }
        if (near_test(r, sp, e, 0x43160000u) == 0) {
            st16(r, b + 0xD0, (uint32_t)(s16(r, b + 0xD0) + 1));
            if (s16(r, b + 0xD0) >= 0x5A)
                st8(r, e + 5, 0);
        } else {
            uint32_t radius;
            st16(r, b + 0xD0, 0);
            radius = ((int32_t)w32(r, b + 0xE4) >> 8) == 1 ? 0x41200000u : 0x41C00000u;
            if (near_test(r, sp, e, radius) != 0) {
                st8(r, e + 5, u8(r, e + 5) + 1);
                st32(r, b + 0xE8, random_heading(r, sp));
            }
        }
        if ((int32_t)u8(r, e + 5) < 2 && u8(r, e + 1) != 0)
            ground_test(r, sp, e, 0xC0800000u);
        break;
    case 2: {
        uint32_t x;
        f_1287F0(r, sp, e, b, table_2F20(r, kind & 0xFF), 0x40800000u);
        st8(r, b + 0xFB, 2);
        st8(r, b + 0xFA, 1);
        x = approach(r, sp, w32(r, b + 0xE8), w32(r, e + 0xC4), 0x3EB2B8C3u);
        st32(r, e + 0xC4, x);
        if (em_ee_c_eq_bits(x, w32(r, b + 0xE8))) {
            st8(r, e + 5, u8(r, e + 5) + 1);
            st8(r, e + 6, 0);
            st32(r, b + 0xD8, 0x3F4CCCCDu);
            st32(r, b + 0xEC, 0x40266666u);
            c = CALL1(r, sp, 0x00128600u, 0);
            st16(r, b + 0xD0, (uint32_t)s16(r, 0x00242EB6u + (uint32_t)(c * 2)));
        }
        break;
    }
    case 3:
        st8(r, b + 0xFB, 3);
        switch (u8(r, e + 6)) {
        case 0:
            st32(r, e + 0xC4, approach(r, sp, w32(r, b + 0xE8), w32(r, e + 0xC4), 0x3D8EFA35u));
            f_1287F0(r, sp, e, b, 6, 0x40800000u);
            st8(r, b + 0xFA, 1);
            st32(r, b + 0xEC, 0x40266666u);
            st32(r, b + 0xD8, 0x3F4CCCCDu);
            c = s16(r, b + 0xD0) - 1;
            st16(r, b + 0xD0, (uint32_t)c);
            if ((int16_t)c == 0) {
                f_1287F0(r, sp, e, b, table_2F20(r, u8(r, e + 0xD)), 0x40800000u);
                st32(r, b + 0xEC, F_ONE);
                st32(r, b + 0xD8, 0);
                if (CALL0(r, sp, FN_RAND) & 0x10)
                    st32(r, b + 0xE8, fcall1(r, sp, FN_WRAP, em_ee_add_bits(0x3F32B8C3u, w32(r, b + 0xE8))));
                else
                    st32(r, b + 0xE8, fcall1(r, sp, FN_WRAP, em_ee_add_bits(0xBF32B8C3u, w32(r, b + 0xE8))));
                c = CALL1(r, sp, 0x00128600u, 0);
                st16(r, b + 0xD0, (uint32_t)s16(r, 0x00242EB0u + (uint32_t)(c * 2)));
                st8(r, e + 6, u8(r, e + 6) + 1);
            }
            break;
        case 1:
            c = s16(r, b + 0xD0) - 1;
            st16(r, b + 0xD0, (uint32_t)c);
            if ((int16_t)c == 0) {
                st32(r, b + 0xEC, 0x40266666u);
                st32(r, b + 0xD8, 0x3F4CCCCDu);
                f_1287F0(r, sp, e, b, 6, 0x40800000u);
                st8(r, e + 6, 0);
                c = CALL1(r, sp, 0x00128600u, 0);
                st16(r, b + 0xD0, (uint32_t)s16(r, 0x00242EBCu + (uint32_t)(c * 2)));
            }
            break;
        default:
            break;
        }
        if (u8(r, e + 1) == 0) {
            st8(r, e + 0, 2);
            st8(r, e + 4, 3);
            st8(r, e + 5, 0);
            st8(r, e + 6, 0);
        }
        break;
    case 4:
        st8(r, b + 0xFB, 0);
        f_1287F0(r, sp, e, b, 6, F_ZERO);
        c = s16(r, b + 0xD0) - 1;
        st16(r, b + 0xD0, (uint32_t)c);
        if ((int16_t)c == 0) {
            CALL2(r, sp, FN_COPY_QW, b + 0x50, e + 0xB0);
            st32(r, b + 0xEC, F_ONE);
            st32(r, b + 0xD8, 0);
            st8(r, e + 5, 0);
        }
        break;
    case 8:
        st8(r, b + 0xFB, 0x80);
        c = CALL3(r, sp, 0x001C2770u, e, b, 2);
        CALL3(r, sp, 0x0012D580u, e, b, c);
        if (c == 0)
            CALL2(r, sp, 0x001C3D60u, e, b);
        break;
    default: /* 5, 6, 7 and above 8 */
        break;
    }
}

static void f_128C10(Run *r, uint32_t sp, uint32_t e)
{
    uint32_t b = e + 0x1F0, t;
    int32_t c;
    sp -= 0x40;
    switch (u8(r, e + 4)) {
    case 0:
        t = u8(r, e + 5);
        if (t == 0) {
            if (CALL2(r, sp, 0x00128AB0u, e, b) != 0) {
                st8(r, e + 5, u8(r, e + 5) + 1);
                st16(r, b + 0xF8, 1);
                CALL2(r, sp, 0x001C63E0u, e, 1);
                st16(r, e + 0x28, 0);
            }
        } else if (t == 1) {
            if (CALL3(r, sp, 0x00129780u, e, b, u8(r, e + 0xD)) != 0) {
                st16(r, e + 0x54, 0);
                t = u8(r, e + 0xD);
                if (t != 4 && t != 9)
                    st8(r, e + 0, 1);
            }
        }
        return;
    case 1:
        if (CALL1(r, sp, 0x001B2140u, e) == 0)
            return;
        t = u8(r, 0x70003B8Du);
        if (t != 0 && (int32_t)(t & 0xFF) >= 2)
            return;
        st16(r, e + 0x54, 0);
        CALL1(r, sp, FN_IDENT, 0x70003000u);
        if (every_64th(r) && f_1B0D80(r, e) != 0)
            return;
        CALL1(r, sp, 0x001B17A0u, e);
        if (f_128B80(r, sp, e, b) == 0) {
            st8(r, b + 0xFA, 0);
            t = u8(r, e + 0xD);
            if (t < 10) {
                if (t == 4 || t == 9) {
                    st8(r, e + 0xD, t == 4 ? 3 : 8);
                    st32(r, b + 0xEC, 0x3FCCCCCDu);
                    c = CALL0(r, sp, FN_RAND);
                    st16(r, b + 0xD0, (uint32_t)((c & 0x30) + 0x3C));
                    st32(r, b + 0xD8, 0x3F19999Au);
                    st8(r, e + 5, 4);
                    st8(r, e + 0, 1);
                    CALL2(r, sp, FN_COPY_QW, b + 0x50, e + 0xB0);
                } else if (u8(r, e + 5) < 9) {
                    kind_step(r, sp, e, b, t);
                }
            }
        }
        if (u8(r, e + 5) != 8 && CALL3(r, sp, 0x001C2770u, e, b, 0) == 0)
            CALL2(r, sp, 0x001C3D60u, e, b);
        {
            uint64_t a[1];
            uint32_t f[1];
            a[0] = reg((int32_t)e);
            f[0] = w32(r, b + 0xEC);
            st16(r, b + 0xF4, (uint32_t)callf(r, sp, 0x001C64F0u, 1, a, 1, f).v0);
        }
        CALL2(r, sp, FN_COPY_QW4, 0x70003400u, 0x70003000u);
        CALL1(r, sp, 0x001C69A0u, e);
        if (u8(r, e + 1) != 0) {
            if (s8(r, b + 0xFA) != 0)
                CALL2(r, sp, 0x001288D0u, e, b);
            CALL1(r, sp, w32(r, e + 0x4C), e);
        }
        st8(r, e + 0xB, 0);
        st8(r, e + 0xA, 0);
        return;
    case 2:
        if (every_64th(r) && f_1B0D80(r, e) != 0)
            return;
        CALL2(r, sp, 0x00129FC0u, e, b);
        st8(r, e + 0xB, 0);
        st8(r, e + 0xA, 0);
        return;
    case 3:
        if (u8(r, b + 0xE0) != 0) {
            st8(r, e + 0, 2);
            st8(r, e + 4, 4);
            st8(r, e + 5, 0);
            CALL2(r, sp, FN_COPY_QW, e + 0xB0, b + 0x50);
            return;
        }
        CALL1(r, sp, 0x001B1190u, u8(r, e + 0x9A));
        CALL1(r, sp, 0x001AFC10u, e);
        return;
    case 4:
        switch (u8(r, e + 5)) {
        case 0:
            st32(r, e + 0x60, F_ONE);
            st32(r, e + 0x64, F_ONE);
            st32(r, e + 0x68, F_ONE);
            st32(r, e + 0x6C, F_ONE);
            CALL2(r, sp, 0x001289C0u, e, b);
            st16(r, b + 0xF8, 1);
            CALL2(r, sp, 0x001C63E0u, e, 1);
            st16(r, e + 0x28, 0);
            st8(r, e + 5, u8(r, e + 5) + 1);
            break;
        case 1:
            if (CALL3(r, sp, 0x00129780u, e, b, u8(r, e + 0xD)) != 0) {
                st8(r, e + 4, 4);
                st8(r, e + 5, 2);
                c = CALL0(r, sp, FN_RAND);
                c = ((c >> 16) * 4) >> 15;
                st16(r, e + 0x28, (uint32_t)s16(r, 0x00275380u + (uint32_t)(c * 2)));
            }
            break;
        case 2:
            c = s16(r, e + 0x28) - 1;
            st16(r, e + 0x28, (uint32_t)c);
            if ((int16_t)c == 0)
                st8(r, e + 5, 3);
            break;
        case 3:
            if (near_test(r, sp, e, 0x42C80000u) == 0) {
                uint32_t f[3];
                f[0] = w32(r, e + 0xB0);
                f[1] = w32(r, e + 0xB4);
                f[2] = w32(r, e + 0xB8);
                if ((callf(r, sp, 0x001B1630u, 0, NULL, 3, f).v0 & 0xFF) == 0) {
                    st8(r, e + 0, 1);
                    st8(r, e + 4, 1);
                    st8(r, e + 5, 0);
                    st8(r, e + 6, 0);
                    st8(r, e + 7, 0);
                }
            }
            break;
        default:
            break;
        }
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------------
 * 0019CF50(): the collision-grid walk for the segment 0x70003190 ->
 * 0x700031A0 (scratch block). Per axis the two direction masks s5 / s6 get
 * 1/2, 4/8, 0x10/0x20 by whether the start coordinate is <= the end one.
 * 0019F1A0(start, s6) and 0019F1A0(end, s5) fill the six cell bounds at
 * the halfwords 0x70003240..0x7000324A; they are kept, then 0019F1A0(start,
 * s5) and 0019F1A0(end, s6) run again. For the six bounds i the span
 * (lo, hi) is (column word 0x70003228[i] + kept[i] * 2 as a halfword,
 * bound[i] + 1) for even i and (bound[i], that halfword) for odd i (the
 * halfwords 0x70003B86 / 0x70003B88 carry the pair); the smallest hi - lo
 * below the word 0x7000320C wins. Each entry in the winning list
 * (0x70003210[best] + lo * 2 .. hi) names a 64-byte face in the table at
 * the word 0x70003208; a face whose halfword box (+0x0C..+0x16) overlaps
 * the bounds and whose byte +0x1A (also stored to 0x70003B88) is below
 * 0x5A and outside 0x51..0x53 is tested by 0019ED80(0x70003190, face). A
 * hit copies 0x700031B0..B8 to 0x700031A0..A8 and keeps it, and records the
 * word 0x700031D0. Result: with a hit the word goes back to 0x700031D0,
 * the kept point to 0x700031B0 and the result is 0; otherwise 1.
 * When no span beats the word 0x7000320C the original goes on with values
 * it never set (its caller's registers); the translation faults instead.
 * ---------------------------------------------------------------------- */

static int32_t f_19CF50(Run *r, uint32_t sp)
{
    uint32_t s5, s6, hit = 0, keep[3] = {0, 0, 0};
    int32_t kept[6], best, lo = 0, hi = 0, which = -1, i;
    uint32_t node;
    sp -= 0xB0;
    if (!em_ee_c_le_bits(w32(r, 0x70003190u), w32(r, 0x700031A0u))) {
        s5 = 1;
        s6 = 2;
    } else {
        s5 = 2;
        s6 = 1;
    }
    if (em_ee_c_le_bits(w32(r, 0x70003194u), w32(r, 0x700031A4u))) {
        s5 |= 8;
        s6 |= 4;
    } else {
        s5 |= 4;
        s6 |= 8;
    }
    if (em_ee_c_le_bits(w32(r, 0x70003198u), w32(r, 0x700031A8u))) {
        s5 |= 0x20;
        s6 |= 0x10;
    } else {
        s5 |= 0x10;
        s6 |= 0x20;
    }
    CALL2(r, sp, 0x0019F1A0u, 0x70003190u, s6);
    CALL2(r, sp, 0x0019F1A0u, 0x700031A0u, s5);
    for (i = 0; i < 6; i++)
        kept[i] = s16(r, 0x70003240u + (uint32_t)i * 2);
    CALL2(r, sp, 0x0019F1A0u, 0x70003190u, s5);
    CALL2(r, sp, 0x0019F1A0u, 0x700031A0u, s6);
    best = (int32_t)w32(r, 0x7000320Cu);
    for (i = 0; i < 6; i++) {
        uint32_t column = w32(r, 0x70003228u + (uint32_t)i * 4) + (uint32_t)(kept[i] * 2);
        int32_t a, b;
        if (i & 1) {
            st16(r, 0x70003B86u, (uint32_t)s16(r, 0x70003240u + (uint32_t)i * 2));
            st16(r, 0x70003B88u, (uint32_t)s16(r, column));
        } else {
            st16(r, 0x70003B86u, (uint32_t)s16(r, column));
            st16(r, 0x70003B88u, (uint32_t)s16(r, 0x70003240u + (uint32_t)i * 2));
            st16(r, 0x70003B88u, (uint32_t)(s16(r, 0x70003B88u) + 1));
        }
        b = s16(r, 0x70003B88u);
        a = s16(r, 0x70003B86u);
        if ((int32_t)((uint32_t)b - (uint32_t)a) < best) {
            best = (int32_t)((uint32_t)b - (uint32_t)a);
            lo = a;
            hi = b;
            which = i;
        }
    }
    if (which < 0)
        fault(r, EM_AREA01_SYS_FAULT_UNDEFINED, 0x0019CF50u);
    node = w32(r, 0x70003210u + (uint32_t)which * 4) + (uint32_t)(lo * 2);
    for (; lo < hi; lo++) {
        uint32_t face = w32(r, 0x70003208u) + ((uint32_t)s16(r, node) << 6);
        int32_t kind;
        node += 2;
        if (s16(r, 0x70003240u) < s16(r, face + 0xC))
            continue;
        if (s16(r, face + 0xE) < s16(r, 0x70003242u))
            continue;
        if (s16(r, 0x70003248u) < s16(r, face + 0x14))
            continue;
        if (s16(r, face + 0x16) < s16(r, 0x7000324Au))
            continue;
        if (s16(r, 0x70003244u) < s16(r, face + 0x10))
            continue;
        if (s16(r, face + 0x12) < s16(r, 0x70003246u))
            continue;
        st16(r, 0x70003B88u, u8(r, face + 0x1A));
        kind = s16(r, 0x70003B88u);
        if (kind >= 0x5A)
            continue;
        if (kind >= 0x51 && kind < 0x54)
            continue;
        if (CALL2(r, sp, 0x0019ED80u, 0x70003190u, face) != 0) {
            unsigned k;
            for (k = 0; k < 3; k++) {
                st32(r, 0x700031A0u + k * 4, w32(r, 0x700031B0u + k * 4));
                keep[k] = w32(r, 0x700031B0u + k * 4);
            }
            hit = w32(r, 0x700031D0u);
        }
    }
    if (hit != 0) {
        unsigned k;
        st32(r, 0x700031D0u, hit);
        for (k = 0; k < 3; k++)
            st32(r, 0x700031B0u + k * 4, keep[k]);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------
 * 001A06A0(): the object hit-box walk for the segment 0x70003190 ->
 * 0x700031A0. 0x700031D0 = 0x700030B0; per axis min / max of the two ends.
 * For each of the (short)D_00275B84 records in the list at D_00275B7C: a
 * record whose byte +0 is non-zero, whose byte +2 & 0x1F is 4, that is not
 * the word 0x70003254, whose box index (halfword +0x0E >> 8) is not 0xFF
 * and below the halfword 0x7000324C, with a non-zero box offset in the
 * table at the word 0x70003250 (+4 + index * 4), whose box (six floats:
 * min x y z, max x y z) overlaps the segment bounds and whose byte +0x54 is
 * below 0x50, has its hit shapes walked (count = halfword box+0x18, shapes
 * from box+0x1C): kind 0x1000 -> 001A4030 (size 0x14 + n * 0x18, or 0x24 +
 * n * 0x30 with flag 0x800, n = byte +2), 0x2000 -> 001A50A0 (0x1C), 0x4000
 * -> 001A5C30 (0x18 / 0x2C), 0x8000 -> 001A5C30 (0x14 / 0x24); other kinds
 * neither test nor advance. The first non-zero result ends the walk: then
 * 0x700031B0..B8 is copied to 0x700031A0..A8, 0x700031D4 = the record, the
 * low byte of the halfword 0x700030CA = byte +0x54, the result becomes 0
 * and the bound on the end side of each axis is narrowed to the new end.
 * Result 1 when nothing was hit.
 * ---------------------------------------------------------------------- */
static void minmax(Run *r, uint32_t a, uint32_t b, uint32_t *mn, uint32_t *mx)
{
    uint32_t va = w32(r, a), vb = w32(r, b);
    if (em_ee_c_le_bits(va, vb)) {
        *mn = va;
        *mx = vb;
    } else {
        *mn = vb;
        *mx = va;
    }
}

static int32_t f_1A06A0(Run *r, uint32_t sp)
{
    uint32_t minx, maxx, miny, maxy, minz, maxz, list;
    int32_t result = 1, i;
    sp -= 0xA0;
    st32(r, 0x700031D0u, 0x700030B0u);
    minmax(r, 0x70003190u, 0x700031A0u, &minx, &maxx);
    minmax(r, 0x70003194u, 0x700031A4u, &miny, &maxy);
    minmax(r, 0x70003198u, 0x700031A8u, &minz, &maxz);
    list = w32(r, 0x00275B7Cu);
    for (i = 0; i < s16(r, 0x00275B84u); i++) {
        uint32_t e = w32(r, list), base, off, box, shape;
        int32_t idx, j, v = 0;
        list += 4;
        if (u8(r, e) == 0)
            continue;
        if ((u8(r, e + 2) & 0x1F) != 4)
            continue;
        if (w32(r, 0x70003254u) == e)
            continue;
        idx = (int32_t)((u16(r, e + 0xE) >> 8) & 0xFF);
        if (idx == 0xFF)
            continue;
        base = w32(r, 0x70003250u);
        off = w32(r, base + (uint32_t)idx * 4 + 4);
        if (off == 0)
            continue;
        if (!(idx < s16(r, 0x7000324Cu)) || idx < 0)
            continue;
        box = base + off;
        if (em_ee_c_lt_bits(maxx, w32(r, box + 0x0)) || !em_ee_c_le_bits(minx, w32(r, box + 0xC)))
            continue;
        if (em_ee_c_lt_bits(maxz, w32(r, box + 0x8)) || !em_ee_c_le_bits(minz, w32(r, box + 0x14)))
            continue;
        if (em_ee_c_lt_bits(maxy, w32(r, box + 0x4)) || !em_ee_c_le_bits(miny, w32(r, box + 0x10)))
            continue;
        if ((int32_t)u8(r, e + 0x54) >= 0x50)
            continue;
        shape = box + 0x1C;
        for (j = 0; j < s16(r, box + 0x18); j++) {
            uint32_t kind = (uint32_t)s16(r, shape) & 0xF000;
            if (kind == 0x1000) {
                uint32_t n;
                v = CALL1(r, sp, 0x001A4030u, shape);
                n = u8(r, shape + 2);
                shape += (s16(r, shape) & 0x800) ? 0x24 + n * 0x30 : 0x14 + n * 0x18;
            } else if (kind == 0x2000) {
                v = CALL1(r, sp, 0x001A50A0u, shape);
                shape += 0x1C;
            } else if (kind == 0x4000) {
                v = CALL1(r, sp, 0x001A5C30u, shape);
                shape += (s16(r, shape) & 0x800) ? 0x2C : 0x18;
            } else if (kind == 0x8000) {
                v = CALL1(r, sp, 0x001A5C30u, shape);
                shape += (s16(r, shape) & 0x800) ? 0x24 : 0x14;
            }
            if (v != 0)
                break;
        }
        if (v == 0)
            continue;
        {
            unsigned k;
            uint32_t a, b;
            for (k = 0; k < 3; k++)
                st32(r, 0x700031A0u + k * 4, w32(r, 0x700031B0u + k * 4));
            st32(r, 0x700031D4u, e);
            st16(r, 0x700030CAu, (u16(r, 0x700030CAu) & 0xFF00) | u8(r, e + 0x54));
            result = 0;
            a = w32(r, 0x70003190u);
            b = w32(r, 0x700031A0u);
            if (em_ee_c_le_bits(a, b))
                maxx = b;
            else
                minx = b;
            a = w32(r, 0x70003194u);
            b = w32(r, 0x700031A4u);
            if (em_ee_c_le_bits(a, b))
                maxy = b;
            else
                miny = b;
            a = w32(r, 0x70003198u);
            b = w32(r, 0x700031A8u);
            if (em_ee_c_le_bits(a, b))
                maxz = b;
            else
                minz = b;
        }
    }
    return result;
}

/* ------------------------------------------------------------------------
 * 0019B4C0(a0, a1, a2, flags). Frame 0x70; a quadword local at sp+0x50.
 * For i = 0..2: 0x70003190[i] = a1[i] - a2[i], then a1[i] (read again) goes
 * to 0x700031A0[i] and a kept copy. 0x700031AC = 0x7000319C = 0x700031D4 =
 * 0. local = 0x700031A0 - 0x70003190 (001028D0), normalised (00102760),
 * scaled by 0.01 (00103230); 0x70003190 -= local (001028D0). The halfword
 * 0x7000324E = a0's byte +2 & 0x1F. flags & 2: 0x70003254 = a0+0x14 and
 * mode = 2 when 001A06A0() is 0. flags & 4: mode = 4 when 0019CF50() is 0.
 * 0x70003190 += local (001028B8). With a mode: 0x700031A0[i] = the kept
 * a1[i] and 0x700031C0[i] = 0x700031B0[i] - 0x700031A0[i], and with bit 31
 * of flags a0+0xB0[i] += 0x700031C0[i]; without: 0x700031D0 = 0.
 * 0x700031D8 = mode; the result is mode.
 * ---------------------------------------------------------------------- */
static int32_t f_19B4C0(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t a2, int32_t flags)
{
    uint32_t kept[3], local;
    int32_t mode = 0;
    unsigned i;
    uint64_t a[2];
    uint32_t f[1];
    sp -= 0x70;
    local = sp + 0x50;
    for (i = 0; i < 3; i++) {
        st32(r, 0x70003190u + i * 4, em_ee_sub_bits(w32(r, a1 + i * 4), w32(r, a2 + i * 4)));
        kept[i] = w32(r, a1 + i * 4);
        st32(r, 0x700031A0u + i * 4, kept[i]);
    }
    st32(r, 0x700031ACu, 0);
    st32(r, 0x7000319Cu, 0);
    st32(r, 0x700031D4u, 0);
    CALL3(r, sp, FN_VSUB, local, 0x700031A0u, 0x70003190u);
    CALL2(r, sp, FN_NORM, local, local);
    a[0] = reg((int32_t)local);
    a[1] = reg((int32_t)local);
    f[0] = 0x3C23D70Au;
    callf(r, sp, FN_VSCALE, 2, a, 1, f);
    CALL3(r, sp, FN_VSUB, 0x70003190u, 0x70003190u, local);
    st16(r, 0x7000324Eu, u8(r, a0 + 2) & 0x1F);
    if (flags & 2) {
        st32(r, 0x70003254u, w32(r, a0 + 0x14));
        if (f_1A06A0(r, sp) == 0)
            mode = 2;
    }
    if (flags & 4) {
        if (f_19CF50(r, sp) == 0)
            mode = 4;
    }
    CALL3(r, sp, FN_VADD, 0x70003190u, 0x70003190u, local);
    if (mode != 0) {
        for (i = 0; i < 3; i++) {
            st32(r, 0x700031A0u + i * 4, kept[i]);
            st32(r, 0x700031C0u + i * 4, em_ee_sub_bits(w32(r, 0x700031B0u + i * 4), w32(r, 0x700031A0u + i * 4)));
        }
        if ((uint32_t)flags & 0x80000000u) {
            for (i = 0; i < 3; i++)
                st32(r, a0 + 0xB0 + i * 4, em_ee_add_bits(w32(r, a0 + 0xB0 + i * 4), w32(r, 0x700031C0u + i * 4)));
        }
    } else {
        st32(r, 0x700031D0u, 0);
    }
    st32(r, 0x700031D8u, (uint32_t)mode);
    return mode;
}

/* Arithmetic right shift of a 32-bit word (the original's shift). */
static int32_t sra32(int32_t v, unsigned n)
{
    return v < 0 ? (int32_t)~(~(uint32_t)v >> n) : (int32_t)((uint32_t)v >> n);
}

/* ------------------------------------------------------------------------
 * 001E3D90(p), s3 = p + 0x1F0. Frame 0xE0; a 0x60-byte local at sp+0x80
 * (the 001CFAE0 output handed to 001CFBE0). On the state byte +4:
 *  0: 0x70003A20 = (float)00122BB8() / 2^31 and it seeds s3+0x10 / +0x14 /
 *     +0x18; s3+0 = s3+4 = 0, s3+8 = 00122BB8(), s3+0xC = -1; the matrix
 *     at p+0xD0 is set to identity (001029C0) and translated by p+0xB0
 *     (00102918); +0x30 = D_00253CA0 + byte +0x0D * 8, +0x34 = 0x001E3D20,
 *     +0x0C = +9 = 0, +0 = +4 = 1; then state 1 runs in the same call.
 *  1: returns early when D_008101E4 == 3, the area key (D_00810700 << 8 |
 *     D_00810701) is 0x100 and +0xB8 < -700, or when D_008101E4 != 3, the
 *     key is 0 or 1 and D_00810702 is 5 or 6. The matrix is copied to
 *     0x700036A0, 0x700036E0 and 0x70003720 (00102958). By +0x0D (0, 1, 2)
 *     the translation words 0x700036D4 / 0x70003714 / 0x70003754 grow by
 *     (6, 1.5, 2), (12, 4, 4) or (18, 6, 6), the row set is D_00253CE0 /
 *     D_00253E90 / D_00254040, the pair D_00253CC0 / CC8 / CD0 and the phase
 *     steps are 0.005, 0.0175, 0.0125 for all three. Area keys 0x1301 ->
 *     0021B9A0(2 and 3; 1.0, 150.0); 0x1100, 0xE00, 0x202, 0x200 ->
 *     0021B9A0(2 and 3; 1.0, 50.0). id = 001CD070(0x70003750, 0x30); unless
 *     it is 0xFFFFFF: 0x70003A20 = d = 001CD2B0(pair[0], pair[1], 320,
 *     320); with the seed s = s3+8 and n = s * 37 + 11, n' = n * 37 + 11:
 *     0x70003A24/28/2C = ((s, n, n' >> 16) & 0xFFFF) / 65535 + 0.0001;
 *     when d != 0: 001CFAE0(local, 0, 0x700036A0; s3+0x10, A24, d, 0.4),
 *     001CFBE0(id - 0x2000, 1, rows, local, 0), 001CFAE0(local, 0,
 *     0x700036E0; s3+0x14, A28, A20, 0.2), 001CFBE0(id, 1, rows + 0x90,
 *     local, 0); always 001CFAE0(local, 0, 0x70003720; s3+0x18, A2C, 1.0,
 *     0.2) and 001CFBE0(id, 6 when the word D_00275C00 > 0x100 else 1, rows
 *     + 0x120, local, 0). The five keys then get 0021B9A0(1; 0, 0). s3+0 is
 *     counted down (+0 = 2 while it was non-zero, else +0 = 1).
 *     001FC3C0(p, s3+0xC, 0x411 / 0x412 / 0x413; 100, 4096) by +0x0D.
 *     s3+4 + 1; the three phases grow by their steps and lose 1.0 once
 *     above 2.0; 001B17A0(p).
 *  2, 3: 001AFC10(p).
 * A +0x0D above 2 leaves the row set, the pair and the phase steps unset
 * in the original (its caller's registers); the translation faults where
 * the original first uses one of them.
 * ---------------------------------------------------------------------- */
static void flash(Run *r, uint32_t sp, uint32_t local, uint32_t src, uint32_t f12, uint32_t f13, uint32_t f14,
                  uint32_t f15)
{
    uint64_t a[3];
    uint32_t f[4];
    a[0] = reg((int32_t)local);
    a[1] = 0;
    a[2] = reg((int32_t)src);
    f[0] = f12;
    f[1] = f13;
    f[2] = f14;
    f[3] = f15;
    callf(r, sp, 0x001CFAE0u, 3, a, 4, f);
}

static void emit(Run *r, uint32_t sp, int32_t id, uint32_t kind, uint32_t rows, uint32_t local)
{
    uint64_t a[5];
    a[0] = reg(id);
    a[1] = reg((int32_t)kind);
    a[2] = reg((int32_t)rows);
    a[3] = reg((int32_t)local);
    a[4] = 0;
    callf(r, sp, 0x001CFBE0u, 5, a, 0, NULL);
}

static void shake(Run *r, uint32_t sp, int32_t chan, uint32_t x, uint32_t y)
{
    uint64_t a[1];
    uint32_t f[2];
    a[0] = reg(chan);
    f[0] = x;
    f[1] = y;
    callf(r, sp, 0x0021B9A0u, 1, a, 2, f);
}

static uint32_t unit16(int32_t v)
{
    uint32_t q = em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)(sra32(v, 16) & 0xFFFF)), 0x477FFF00u);
    return em_ee_add_bits(q, 0x38D1B717u);
}

static void f_1E3D90(Run *r, uint32_t sp, uint32_t p)
{
    uint32_t s3 = p + 0x1F0, st, key, local, rows = 0, pair = 0, stepA = 0, stepB = 0, stepC = 0;
    int32_t seed, id, n;
    int have = 0;
    sp -= 0xE0;
    local = sp + 0x80;
    st = u8(r, p + 4);
    if (st == 3 || st == 2) {
        CALL1(r, sp, 0x001AFC10u, p);
        return;
    }
    if (st == 0) {
        uint32_t f = em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)CALL0(r, sp, FN_RAND)), 0x4F000000u);
        st32(r, 0x70003A20u, f);
        st32(r, s3 + 0x10, f);
        st32(r, s3 + 0x14, w32(r, 0x70003A20u));
        st32(r, s3 + 0x18, w32(r, 0x70003A20u));
        st32(r, s3 + 0, 0);
        st32(r, s3 + 4, 0);
        st32(r, s3 + 8, (uint32_t)CALL0(r, sp, FN_RAND));
        st32(r, s3 + 0xC, 0xFFFFFFFFu);
        CALL1(r, sp, FN_IDENT, p + 0xD0);
        CALL3(r, sp, FN_MAT_T, p + 0xD0, p + 0xD0, p + 0xB0);
        st32(r, p + 0x30, 0x00253CA0u + u8(r, p + 0xD) * 8);
        st32(r, p + 0x34, 0x001E3D20u);
        st8(r, p + 0xC, 0);
        st8(r, p + 9, 0);
        st8(r, p + 0, 1);
        st8(r, p + 4, 1);
    } else if (st != 1) {
        return;
    }
    if (u8(r, 0x008101E4u) == 3) {
        if (((u8(r, 0x00810700u) << 8) + u8(r, 0x00810701u)) == 0x100 &&
            em_ee_c_lt_bits(w32(r, p + 0xB8), 0xC42F0000u))
            return;
    } else {
        key = (u8(r, 0x00810700u) << 8) + u8(r, 0x00810701u);
        if (key == 1 || key == 0) {
            uint32_t sub = u8(r, 0x00810702u);
            if (sub == 5 || sub == 6)
                return;
        }
    }
    seed = (int32_t)w32(r, s3 + 8);
    CALL2(r, sp, FN_COPY_QW4, 0x700036A0u, p + 0xD0);
    CALL2(r, sp, FN_COPY_QW4, 0x700036E0u, p + 0xD0);
    CALL2(r, sp, FN_COPY_QW4, 0x70003720u, p + 0xD0);
    switch (u8(r, p + 0xD)) {
    case 0:
        have = 1;
        rows = 0x00253CE0u;
        pair = 0x00253CC0u;
        st32(r, 0x700036D4u, em_ee_add_bits(w32(r, 0x700036D4u), 0x40C00000u));
        st32(r, 0x70003714u, em_ee_add_bits(w32(r, 0x70003714u), 0x3FC00000u));
        st32(r, 0x70003754u, em_ee_add_bits(w32(r, 0x70003754u), 0x40000000u));
        break;
    case 1:
        have = 1;
        rows = 0x00253E90u;
        pair = 0x00253CC8u;
        st32(r, 0x700036D4u, em_ee_add_bits(w32(r, 0x700036D4u), 0x41400000u));
        st32(r, 0x70003714u, em_ee_add_bits(w32(r, 0x70003714u), 0x40800000u));
        st32(r, 0x70003754u, em_ee_add_bits(w32(r, 0x70003754u), 0x40800000u));
        break;
    case 2:
        have = 1;
        rows = 0x00254040u;
        pair = 0x00253CD0u;
        st32(r, 0x700036D4u, em_ee_add_bits(w32(r, 0x700036D4u), 0x41900000u));
        st32(r, 0x70003714u, em_ee_add_bits(w32(r, 0x70003714u), 0x40C00000u));
        st32(r, 0x70003754u, em_ee_add_bits(w32(r, 0x70003754u), 0x40C00000u));
        break;
    default:
        break;
    }
    if (have) {
        stepA = 0x3BA3D70Au;
        stepB = 0x3C8F5C29u;
        stepC = 0x3C4CCCCDu;
    }
    key = (u8(r, 0x00810700u) << 8) + u8(r, 0x00810701u);
    if (key == 0x1301) {
        shake(r, sp, 2, F_ONE, 0x43160000u);
        shake(r, sp, 3, F_ONE, 0x43160000u);
    } else if (key == 0x1100 || key == 0xE00 || key == 0x202 || key == 0x200) {
        shake(r, sp, 2, F_ONE, 0x42480000u);
        shake(r, sp, 3, F_ONE, 0x42480000u);
    }
    id = CALL2(r, sp, 0x001CD070u, 0x70003750u, 0x30);
    if (id != 0xFFFFFF) {
        uint32_t f[4], d;
        int32_t n2;
        if (!have)
            fault(r, EM_AREA01_SYS_FAULT_UNDEFINED, 0x001E3D90u);
        f[0] = w32(r, pair);
        f[1] = w32(r, pair + 4);
        f[2] = 0x43A00000u;
        f[3] = 0x43A00000u;
        st32(r, 0x70003A20u, callf(r, sp, 0x001CD2B0u, 0, NULL, 4, f).f0);
        n2 = (int32_t)((uint32_t)seed * 37u + 11u);
        st32(r, 0x70003A24u, unit16(seed));
        st32(r, 0x70003A28u, unit16(n2));
        st32(r, 0x70003A2Cu, unit16((int32_t)((uint32_t)n2 * 37u + 11u)));
        d = w32(r, 0x70003A20u);
        if (!em_ee_c_eq_bits(F_ZERO, d)) {
            flash(r, sp, local, 0x700036A0u, w32(r, s3 + 0x10), w32(r, 0x70003A24u), d, 0x3ECCCCCDu);
            emit(r, sp, id - 0x2000, 1, rows, local);
            flash(r, sp, local, 0x700036E0u, w32(r, s3 + 0x14), w32(r, 0x70003A28u), w32(r, 0x70003A20u),
                  0x3E4CCCCDu);
            emit(r, sp, id, 1, rows + 0x90, local);
        }
        n = (int32_t)w32(r, 0x00275C00u) < 0x101 ? 1 : 6;
        flash(r, sp, local, 0x70003720u, w32(r, s3 + 0x18), w32(r, 0x70003A2Cu), F_ONE, 0x3E4CCCCDu);
        emit(r, sp, id, (uint32_t)n & 0xFF, rows + 0x120, local);
    }
    key = (u8(r, 0x00810700u) << 8) + u8(r, 0x00810701u);
    if (key == 0x1301 || key == 0x1100 || key == 0xE00 || key == 0x202 || key == 0x200)
        shake(r, sp, 1, F_ZERO, F_ZERO);
    n = (int32_t)w32(r, s3 + 0);
    if (n != 0) {
        st32(r, s3 + 0, (uint32_t)(n - 1));
        st8(r, p + 0, 2);
    } else {
        st8(r, p + 0, 1);
    }
    {
        uint32_t ids[3] = {0x411, 0x412, 0x413}, t = u8(r, p + 0xD);
        if (t < 3) {
            uint64_t a[3];
            uint32_t f[2];
            a[0] = reg((int32_t)p);
            a[1] = reg((int32_t)(s3 + 0xC));
            a[2] = ids[t];
            f[0] = 0x42C80000u;
            f[1] = 0x45800000u;
            callf(r, sp, 0x001FC3C0u, 3, a, 2, f);
        }
    }
    st32(r, s3 + 4, w32(r, s3 + 4) + 1);
    if (!have)
        fault(r, EM_AREA01_SYS_FAULT_UNDEFINED, 0x001E3D90u);
    st32(r, s3 + 0x10, em_ee_add_bits(w32(r, s3 + 0x10), stepA));
    st32(r, s3 + 0x14, em_ee_add_bits(w32(r, s3 + 0x14), stepB));
    st32(r, s3 + 0x18, em_ee_add_bits(w32(r, s3 + 0x18), stepC));
    {
        unsigned k;
        for (k = 0; k < 3; k++) {
            uint32_t v = w32(r, s3 + 0x10 + k * 4);
            if (!em_ee_c_le_bits(v, F_TWO))
                st32(r, s3 + 0x10 + k * 4, em_ee_sub_bits(v, F_ONE));
        }
    }
    CALL1(r, sp, 0x001B17A0u, p);
}

/* ------------------------------------------------------------------------
 * 001E7C60(s; f12 = v): the 32 x 32 grid points s + i*0x200 + j*0x10
 * (+0x64, their height) and the word s+4 are set to v.
 * ---------------------------------------------------------------------- */
static void f_1E7C60(Run *r, uint32_t s, uint32_t v)
{
    uint32_t i, j;
    for (i = 0; i < 0x20; i++) {
        for (j = 0; j < 0x20; j++) {
            st32(r, s + i * 0x200 + j * 0x10 + 0x64, v);
            st32(r, s + 4, v);
        }
    }
}

/* ------------------------------------------------------------------------
 * 001E7D20(p). s = the word D_00275C20 + byte +0x0D * 0xA060 (a record of
 * a 32 x 32 grid: points at s+0x60 + i*0x200 + j*0x10, texture pairs at
 * s+0x4060 (same stride), heights H at s+0x8060 + i*0x80 + j*4, rates V at
 * s+0x9060, same stride). Frame 0x130; a quadword local at sp+0xA0 (the
 * 001E8B90 point). On the state byte +4:
 *  0: s+0x58 = p; s+0/4/8 = p+0xB0/B4/B8, s+0x30 = p+0xC0, s+0x34 = p+0xC8;
 *     s+0x20 = 0.961, s+0x24 = 0.022, s+0x2C = 0.4, s+0x28 = 0.25; +5 = 0,
 *     +0x20 = 0. By D_00810700: 0x13 -> s+0x10..0x1C = 128, s+0x38/3C =
 *     0.325, s+0x2C = 2.5, and for +0x0D 0 (1) when D_008107F6 (F5) is 0xFF:
 *     +5 = 2 and +0xB4 = 132 (160); then 001E7C60(s; +0xB4). 6 -> (36, 34,
 *     62, 40, 0.325, 0.325). 0 -> with D_00810701 == 1 (148, 91, 70, 89,
 *     s+0x3C = 0.21, s+0x38 = 0.25), else (32, 48, 48, 80, 0.265, 0.265).
 *     Others -> (36, 34, 62, 88, 0.325, 0.325). Every point (i, j): x = s+0
 *     + s+0x30 * j / 30, z = s+8 + s+0x34 * i / 30 (area 0x13 with +0x0D ==
 *     1: x = 930 when z <= 921 and x >= 930); the point = (x, s+4, z, 1),
 *     its texture pair = (j / 32, i / 32), H = V = 0. +0x0C = +9 = 0, +4 = 1.
 *  1: s+0x54 = 1. In area 0x13: +0x0D 0 with +5 == 1 -> +0xB4 = 132 and +5
 *     + 1 when +0xB4 <= 132 or D_008107F6 == 0xFF, else +0xB4 -= 0.1; then
 *     001E7C60(s; +0xB4). +0x0D 1: the same with 160, D_008107F5 and 1.0;
 *     then (every +5) a splash 001E8B90(local; f12) at local = (877.5 +
 *     62.200012 * u, 0, 982 + 37.400024 * u', 1), f12 = 0.1 + 1.1 * u'',
 *     with u = 4.656613e-10 * (float)00122BB8() (three draws).
 *     Ripple step, k4 = 4 + s+0x28: for every cell, with clamped
 *     neighbours: V = V * s+0x20 + (acc-form (H[i+1][j] + (H[i-1][j] +
 *     (H[i][j-1] + H[i][j+1]))) - H * k4) * s+0x24 (V is written once
 *     after the damping product); the texture pair = (s+0x3C + 0.085 *
 *     (H[i][j-1] - H[i][j+1]), s+0x38 + 0.085 * (H[i-1][j] - H[i+1][j])).
 *     An impulse k = s+0x2C at a random cell (row, column = ((00122BB8() >>
 *     16) * 32) >> 15, row first): V += k there, k / 2 on the four edge
 *     neighbours, k / 4 on the four corners (clamped, in the original's
 *     order). Then H += V and the point height = s+4 + 0.02 * H.
 *     When 001E7CB0(s) is non-zero, the GS packets: 30 strips of 0x62
 *     quadwords from 001CB5F0(D_007635C0, 0x1000, 0x62) (a four-word header
 *     0, 0, 0x01000404, 0x6C608000; rows s, s+1, s+2 of points copied
 *     quadword by quadword (00102948) to +0x10, +0x210, +0x410; the words
 *     +0x610..0x61C = 0x17000000 (0x14000000 for the last strip), 0, 0, 0);
 *     a 5-quadword block (header 0x6C040000) with the four quadwords at
 *     0x70003AC0 (00102958); a 9-quadword block (header 0x6C0803F8): by
 *     D_00810700 (0x13: 34.8, 24, -1.5, 0; 6: 24, 26, -1.6, 0; else 48, 24,
 *     -1.5, 0), by the area key (1: 127, 102, 81; 0: 47, 44, 44; else 119 x
 *     3) and 0, then 0.44, 0.44, 0.35, 0.245; D_008105D0 at +0x40; the
 *     doublewords 0x303E400000008040 and 0x412; the render context
 *     (word D_00275670) +0xA0, +0x2220, +0x2230 at +0x60/70/80. Then
 *     001CB950(D_007635C0, 0x1000, v): area 0x13: the doubleword
 *     D_002553B0[n] with bit 34 cleared, n = the word D_00275C10 clamped to
 *     0..14 whenever the frame word 0x70003B68 differs from D_00275C14
 *     (which then takes it); other areas 0x20048BA199422040. When
 *     001D2E00(2) is non-zero: 001CB6B0(D_007635C0, 0x1000, 8, D_00275674 +
 *     0x8A0) and 001D2DE0(2, 0), else 001CB6B0(..., D_00275674 + 0x720);
 *     finally 001CB760(D_007635C0, 0x1000, 0x00234B00).
 *  2, 3 and above: nothing.
 * ---------------------------------------------------------------------- */
static uint32_t rnd_unit(Run *r, uint32_t sp)
{
    return em_ee_mul_bits(0x30000000u, em_ee_cvt_s_w_bits((uint32_t)CALL0(r, sp, FN_RAND)));
}

static int32_t rnd_cell(Run *r, uint32_t sp)
{
    return sra32(sra32(CALL0(r, sp, FN_RAND), 16) * 32, 15);
}

static void level_fill(Run *r, uint32_t s, uint32_t p, uint32_t floor_bits, uint32_t flag, uint32_t step)
{
    uint32_t f = w32(r, p + 0xB4);
    if (em_ee_c_le_bits(f, floor_bits) || u8(r, flag) == 0xFF) {
        st32(r, p + 0xB4, floor_bits);
        st8(r, p + 5, u8(r, p + 5) + 1);
    } else {
        st32(r, p + 0xB4, em_ee_sub_bits(f, step));
    }
    f_1E7C60(r, s, w32(r, p + 0xB4));
}

static void add_to(Run *r, uint32_t a, uint32_t k) { st32(r, a, em_ee_add_bits(w32(r, a), k)); }

static uint32_t header(Run *r, uint32_t sp, int32_t count, uint32_t tag)
{
    uint32_t blk = (uint32_t)CALL3(r, sp, 0x001CB5F0u, 0x007635C0u, 0x1000, count);
    st32(r, blk + 0, 0);
    st32(r, blk + 4, 0);
    st32(r, blk + 8, 0x01000404u);
    st32(r, blk + 0xC, tag);
    return blk;
}

static void f_1E7D20(Run *r, uint32_t sp, uint32_t p)
{
    uint32_t s, st, area, i, j;
    sp -= 0x130;
    s = w32(r, 0x00275C20u) + u8(r, p + 0xD) * 0xA060u;
    st = u8(r, p + 4);
    area = u8(r, 0x00810700u);
    if (st == 0) {
        st32(r, s + 0x58, p);
        st32(r, s + 0x0, w32(r, p + 0xB0));
        st32(r, s + 0x4, w32(r, p + 0xB4));
        st32(r, s + 0x8, w32(r, p + 0xB8));
        st32(r, s + 0x30, w32(r, p + 0xC0));
        st32(r, s + 0x34, w32(r, p + 0xC8));
        st32(r, s + 0x20, 0x3F760419u);
        st32(r, s + 0x24, 0x3CB43958u);
        st32(r, s + 0x2C, 0x3ECCCCCDu);
        st32(r, s + 0x28, 0x3E800000u);
        st8(r, p + 5, 0);
        st32(r, p + 0x20, 0);
        area = u8(r, 0x00810700u);
        if (area == 0x13) {
            uint32_t d;
            st32(r, s + 0x1C, 0x43000000u);
            st32(r, s + 0x18, 0x43000000u);
            st32(r, s + 0x14, 0x43000000u);
            st32(r, s + 0x10, 0x43000000u);
            st32(r, s + 0x3C, 0x3EA66666u);
            st32(r, s + 0x38, 0x3EA66666u);
            st32(r, s + 0x2C, 0x40200000u);
            d = u8(r, p + 0xD);
            if (d == 1) {
                if (u8(r, 0x008107F5u) == 0xFF) {
                    st8(r, p + 5, 2);
                    st32(r, p + 0xB4, 0x43200000u);
                }
            } else if (d == 0) {
                if (u8(r, 0x008107F6u) == 0xFF) {
                    st8(r, p + 5, 2);
                    st32(r, p + 0xB4, 0x43040000u);
                }
            }
            f_1E7C60(r, s, w32(r, p + 0xB4));
        } else if (area == 6) {
            st32(r, s + 0x10, 0x42100000u);
            st32(r, s + 0x14, 0x42080000u);
            st32(r, s + 0x18, 0x42780000u);
            st32(r, s + 0x1C, 0x42200000u);
            st32(r, s + 0x3C, 0x3EA66666u);
            st32(r, s + 0x38, 0x3EA66666u);
        } else if (area == 0) {
            if (u8(r, 0x00810701u) == 1) {
                st32(r, s + 0x10, 0x43140000u);
                st32(r, s + 0x14, 0x42B60000u);
                st32(r, s + 0x18, 0x428C0000u);
                st32(r, s + 0x1C, 0x42B20000u);
                st32(r, s + 0x3C, 0x3E570A3Du);
                st32(r, s + 0x38, 0x3E800000u);
            } else {
                st32(r, s + 0x10, 0x42000000u);
                st32(r, s + 0x14, 0x42400000u);
                st32(r, s + 0x18, 0x42400000u);
                st32(r, s + 0x1C, 0x42A00000u);
                st32(r, s + 0x3C, 0x3E87AE14u);
                st32(r, s + 0x38, 0x3E87AE14u);
            }
        } else {
            st32(r, s + 0x10, 0x42100000u);
            st32(r, s + 0x14, 0x42080000u);
            st32(r, s + 0x18, 0x42780000u);
            st32(r, s + 0x1C, 0x42B00000u);
            st32(r, s + 0x3C, 0x3EA66666u);
            st32(r, s + 0x38, 0x3EA66666u);
        }
        for (i = 0; i < 0x20; i++) {
            uint32_t fi = em_ee_cvt_s_w_bits(i), vi = em_ee_div_bits(fi, 0x42000000u);
            for (j = 0; j < 0x20; j++) {
                uint32_t c1 = s + i * 0x200 + j * 0x10, c2 = s + i * 0x80 + j * 4;
                uint32_t fj = em_ee_cvt_s_w_bits(j), x, z;
                x = em_ee_add_bits(w32(r, s + 0x0), em_ee_div_bits(em_ee_mul_bits(w32(r, s + 0x30), fj), 0x41F00000u));
                z = em_ee_add_bits(w32(r, s + 0x8), em_ee_div_bits(em_ee_mul_bits(w32(r, s + 0x34), fi), 0x41F00000u));
                if (u8(r, 0x00810700u) == 0x13 && u8(r, p + 0xD) == 1 && em_ee_c_le_bits(z, 0x44664000u) &&
                    !em_ee_c_lt_bits(x, 0x44688000u))
                    x = 0x44688000u;
                st32(r, c1 + 0x60, x);
                st32(r, c1 + 0x68, z);
                st32(r, c1 + 0x64, w32(r, s + 0x4));
                st32(r, c1 + 0x6C, F_ONE);
                st32(r, c1 + 0x4060, em_ee_div_bits(fj, 0x42000000u));
                st32(r, c1 + 0x4064, vi);
                st32(r, c2 + 0x8060, 0);
                st32(r, c2 + 0x9060, 0);
            }
        }
        st8(r, p + 0xC, 0);
        st8(r, p + 9, 0);
        st8(r, p + 4, 1);
        return;
    }
    if (st != 1)
        return;
    st32(r, s + 0x54, 1);
    if (area == 0x13) {
        uint32_t d = u8(r, p + 0xD);
        if (d == 0) {
            if (u8(r, p + 5) == 1)
                level_fill(r, s, p, 0x43040000u, 0x008107F6u, 0x3DCCCCCDu);
        } else if (d == 1) {
            uint32_t local = sp + 0xA0, x, z;
            uint64_t a[1];
            uint32_t f[1];
            if (u8(r, p + 5) == 1)
                level_fill(r, s, p, 0x43200000u, 0x008107F5u, F_ONE);
            x = em_ee_add_bits(0x445B6000u, em_ee_mul_bits(0x4278CCD0u, rnd_unit(r, sp)));
            st32(r, local + 0, x);
            st32(r, local + 4, 0);
            z = em_ee_add_bits(0x44758000u, em_ee_mul_bits(0x421599A0u, rnd_unit(r, sp)));
            st32(r, local + 8, z);
            st32(r, local + 0xC, F_ONE);
            a[0] = reg((int32_t)local);
            f[0] = em_ee_add_bits(0x3DCCCCCDu, em_ee_mul_bits(0x3F8CCCCDu, rnd_unit(r, sp)));
            callf(r, sp, 0x001E8B90u, 1, a, 1, f);
        }
    }
    {
        uint32_t k4 = em_ee_add_bits(0x40800000u, w32(r, s + 0x28));
        uint32_t damp = w32(r, s + 0x20), coef = w32(r, s + 0x24), sv = w32(r, s + 0x38), su = w32(r, s + 0x3C);
        for (i = 0; i < 0x20; i++) {
            uint32_t im1 = i == 0 ? 0 : i - 1, ip1 = i + 1 >= 0x20 ? 0x1F : i + 1;
            for (j = 0; j < 0x20; j++) {
                uint32_t jm1 = j == 0 ? 0 : j - 1, jp1 = j + 1 >= 0x20 ? 0x1F : j + 1;
                uint32_t h = s + 0x8060, v = s + 0x9060 + i * 0x80 + j * 4;
                uint32_t up = w32(r, h + im1 * 0x80 + j * 4), dn = w32(r, h + ip1 * 0x80 + j * 4);
                uint32_t lf = w32(r, h + i * 0x80 + jm1 * 4), rt = w32(r, h + i * 0x80 + jp1 * 4);
                uint32_t hc = w32(r, h + i * 0x80 + j * 4), vc = w32(r, v);
                uint32_t acc = em_ee_adda_bits(dn, em_ee_add_bits(up, em_ee_add_bits(lf, rt)));
                uint32_t lap = em_ee_msub_bits(acc, hc, k4);
                uint32_t d1 = em_ee_mul_bits(vc, damp);
                uint32_t d2 = em_ee_mul_bits(lap, coef);
                uint32_t uv = s + 0x4060 + i * 0x200 + j * 0x10;
                st32(r, v, d1);
                st32(r, v, em_ee_add_bits(d1, d2));
                st32(r, uv, em_ee_add_bits(su, em_ee_mul_bits(0x3DAE147Bu,
                                                              em_ee_sub_bits(w32(r, h + i * 0x80 + jm1 * 4),
                                                                             w32(r, h + i * 0x80 + jp1 * 4)))));
                st32(r, uv + 4, em_ee_add_bits(sv, em_ee_mul_bits(0x3DAE147Bu,
                                                                  em_ee_sub_bits(w32(r, h + im1 * 0x80 + j * 4),
                                                                                 w32(r, h + ip1 * 0x80 + j * 4)))));
            }
        }
    }
    {
        int32_t row = rnd_cell(r, sp), col = rnd_cell(r, sp);
        int32_t cm = col - 1 < 0 ? 0 : col - 1, cp = col + 1 < 0x20 ? col + 1 : 0x1F;
        int32_t rm = row - 1 < 0 ? 0 : row - 1, rp = row + 1 < 0x20 ? row + 1 : 0x1F;
        uint32_t vc = s + 0x9060 + (uint32_t)row * 0x80, vm = s + 0x9060 + (uint32_t)rm * 0x80;
        uint32_t vp = s + 0x9060 + (uint32_t)rp * 0x80, k = w32(r, s + 0x2C);
        add_to(r, vc + (uint32_t)col * 4, k);
        k = em_ee_mul_bits(k, F_HALF);
        add_to(r, vm + (uint32_t)col * 4, k);
        add_to(r, vp + (uint32_t)col * 4, k);
        add_to(r, vc + (uint32_t)cm * 4, k);
        add_to(r, vc + (uint32_t)cp * 4, k);
        k = em_ee_mul_bits(k, F_HALF);
        add_to(r, vm + (uint32_t)cm * 4, k);
        add_to(r, vp + (uint32_t)cm * 4, k);
        add_to(r, vm + (uint32_t)cp * 4, k);
        add_to(r, vp + (uint32_t)cp * 4, k);
    }
    for (i = 0; i < 0x20; i++) {
        for (j = 0; j < 0x20; j++) {
            uint32_t hc = s + 0x8060 + i * 0x80 + j * 4;
            st32(r, hc, em_ee_add_bits(w32(r, hc), w32(r, hc + 0x1000)));
            st32(r, s + i * 0x200 + j * 0x10 + 0x64,
                 em_ee_add_bits(w32(r, s + 4), em_ee_mul_bits(0x3CA3D70Au, w32(r, hc))));
        }
    }
    if (f_1E7CB0(r) == 0) /* called with s, which it does not read */
        return;
    for (i = 0; i < 0x1E; i++) {
        uint32_t blk = header(r, sp, 0x62, 0x6C608000u);
        for (j = 0; j < 0x20; j++) {
            CALL2(r, sp, FN_COPY_QW, blk + 0x10 + j * 0x10, s + i * 0x200 + j * 0x10 + 0x60);
            CALL2(r, sp, FN_COPY_QW, blk + ((j + 0x20) << 4) + 0x10, s + (i + 1) * 0x200 + j * 0x10 + 0x60);
            CALL2(r, sp, FN_COPY_QW, blk + ((j + 0x40) << 4) + 0x10, s + (i + 2) * 0x200 + j * 0x10 + 0x60);
        }
        st32(r, blk + 0x610, i == 0x1D ? 0x14000000u : 0x17000000u);
        st32(r, blk + 0x614, 0);
        st32(r, blk + 0x618, 0);
        st32(r, blk + 0x61C, 0);
    }
    {
        uint32_t blk = header(r, sp, 5, 0x6C040000u), key, ctx;
        CALL2(r, sp, FN_COPY_QW4, blk + 0x10, 0x70003AC0u);
        blk = header(r, sp, 9, 0x6C0803F8u);
        area = u8(r, 0x00810700u);
        if (area == 0x13) {
            st32(r, blk + 0x10, 0x420B3333u);
            st32(r, blk + 0x14, 0x41C00000u);
            st32(r, blk + 0x18, 0xBFC00000u);
        } else if (area == 6) {
            st32(r, blk + 0x10, 0x41C00000u);
            st32(r, blk + 0x14, 0x41D00000u);
            st32(r, blk + 0x18, 0xBFCCCCCDu);
        } else {
            st32(r, blk + 0x10, 0x42400000u);
            st32(r, blk + 0x14, 0x41C00000u);
            st32(r, blk + 0x18, 0xBFC00000u);
        }
        st32(r, blk + 0x1C, 0);
        key = (u8(r, 0x00810700u) << 8) + u8(r, 0x00810701u);
        if (key == 1) {
            st32(r, blk + 0x20, 0x42FE0000u);
            st32(r, blk + 0x24, 0x42CC0000u);
            st32(r, blk + 0x28, 0x42A20000u);
        } else if (key == 0) {
            st32(r, blk + 0x20, 0x423C0000u);
            st32(r, blk + 0x24, 0x42300000u);
            st32(r, blk + 0x28, 0x42300000u);
        } else {
            st32(r, blk + 0x20, 0x42EE0000u);
            st32(r, blk + 0x24, 0x42EE0000u);
            st32(r, blk + 0x28, 0x42EE0000u);
        }
        st32(r, blk + 0x2C, 0);
        st32(r, blk + 0x30, 0x3EE147AEu);
        st32(r, blk + 0x34, 0x3EE147AEu);
        st32(r, blk + 0x38, 0x3EB33333u);
        st32(r, blk + 0x3C, 0x3E7AE148u);
        CALL2(r, sp, FN_COPY_QW, blk + 0x40, 0x008105D0u);
        st64(r, blk + 0x50, UINT64_C(0x303E400000008040));
        st64(r, blk + 0x58, UINT64_C(0x412));
        ctx = w32(r, 0x00275670u);
        CALL2(r, sp, FN_COPY_QW, blk + 0x60, ctx + 0xA0);
        ctx = w32(r, 0x00275670u);
        CALL2(r, sp, FN_COPY_QW, blk + 0x70, ctx + 0x2220);
        ctx = w32(r, 0x00275670u);
        CALL2(r, sp, FN_COPY_QW, blk + 0x80, ctx + 0x2230);
        if (u8(r, 0x00810700u) == 0x13) {
            int32_t n;
            uint64_t word;
            if (w32(r, 0x00275C14u) != w32(r, 0x70003B68u)) {
                n = (int32_t)w32(r, 0x00275C10u);
                st32(r, 0x00275C14u, w32(r, 0x70003B68u));
                if (n <= 0)
                    n = 0;
                if (n >= 0xE)
                    n = 0xE;
                st32(r, 0x00275C10u, (uint32_t)n);
            }
            n = (int32_t)w32(r, 0x00275C10u);
            if (n < 0 || n > 14) /* the original would read past its 15-entry local copy */
                fault(r, EM_AREA01_SYS_FAULT_UNDEFINED, 0x001E7D20u);
            word = ld64(r, 0x002553B0u + (uint32_t)n * 8) & UINT64_C(0xFFFFFFFBFFFFFFFF);
            callv(r, sp, 0x001CB950u, 3, reg((int32_t)0x007635C0u), 0x1000, word, 0);
        } else {
            callv(r, sp, 0x001CB950u, 3, reg((int32_t)0x007635C0u), 0x1000, UINT64_C(0x20048BA199422040), 0);
        }
        if (CALL1(r, sp, 0x001D2E00u, 2) != 0) {
            CALL4(r, sp, 0x001CB6B0u, 0x007635C0u, 0x1000, 8, w32(r, 0x00275674u) + 0x8A0);
            CALL2(r, sp, 0x001D2DE0u, 2, 0);
        } else {
            CALL4(r, sp, 0x001CB6B0u, 0x007635C0u, 0x1000, 8, w32(r, 0x00275674u) + 0x720);
        }
        CALL3(r, sp, 0x001CB760u, 0x007635C0u, 0x1000, 0x00234B00u);
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
    if ((s)->fault != EM_AREA01_SYS_FAULT_NONE)                             \
        return -1;                                                          \
    run_.s = (s);                                                           \
    if (setjmp(run_.out)) {                                                 \
        if ((s)->fault_function == 0)                                       \
            (s)->fault_function = (address);                                \
        return -1;                                                          \
    }

static int out_null(EmArea01Sys *s, uint32_t address)
{
    s->fault = EM_AREA01_SYS_FAULT_NULL;
    s->fault_function = address;
    return -1;
}

int em_area01_sys_001287F0(EmArea01Sys *s, uint32_t a0, uint32_t a1, int32_t a2, uint32_t f12)
{
    ENTER(s, 0x001287F0u);
    f_1287F0(r, s->sp, a0, a1, a2, f12);
    return 0;
}

int em_area01_sys_00128B80(EmArea01Sys *s, uint32_t a0, uint32_t a1, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x00128B80u);
    if (!out)
        return out_null(s, 0x00128B80u);
    v = f_128B80(r, s->sp, a0, a1);
    *out = v;
    return 0;
}

int em_area01_sys_00157CE0(EmArea01Sys *s, uint32_t a0, int32_t a1, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x00157CE0u);
    if (!out)
        return out_null(s, 0x00157CE0u);
    v = f_157CE0(r, s->sp, a0, a1);
    *out = v;
    return 0;
}

int em_area01_sys_00158590(EmArea01Sys *s, uint32_t p, int32_t a1, int32_t mode)
{
    ENTER(s, 0x00158590u);
    f_158590(r, s->sp, p, a1, mode);
    return 0;
}

int em_area01_sys_00158D30(EmArea01Sys *s, uint32_t p)
{
    ENTER(s, 0x00158D30u);
    f_158D30(r, s->sp, p);
    return 0;
}

int em_area01_sys_001A8840(EmArea01Sys *s, uint32_t a0, uint32_t a1)
{
    ENTER(s, 0x001A8840u);
    f_1A8840(r, s->sp, a0, a1);
    return 0;
}

int em_area01_sys_001A9E00(EmArea01Sys *s, uint32_t a0, uint32_t a1)
{
    ENTER(s, 0x001A9E00u);
    f_1A9E00(r, s->sp, a0, a1);
    return 0;
}

int em_area01_sys_001AA000(EmArea01Sys *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    ENTER(s, 0x001AA000u);
    f_1AA000(r, s->sp, a0, a1, a2, a3);
    return 0;
}

int em_area01_sys_001B0300(EmArea01Sys *s)
{
    ENTER(s, 0x001B0300u);
    f_1B0300(r, s->sp);
    return 0;
}

int em_area01_sys_001B0D80(EmArea01Sys *s, uint32_t a0, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x001B0D80u);
    if (!out)
        return out_null(s, 0x001B0D80u);
    v = f_1B0D80(r, a0);
    *out = v;
    return 0;
}

int em_area01_sys_001B6D70(EmArea01Sys *s, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *out)
{
    int32_t v;
    (void)a1; /* the original never reads a1 */
    ENTER(s, 0x001B6D70u);
    if (!out)
        return out_null(s, 0x001B6D70u);
    v = f_1B6D70(r, s->sp, a0, a2);
    *out = v;
    return 0;
}

int em_area01_sys_001B76D0(EmArea01Sys *s, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *out)
{
    int32_t v;
    (void)a0; /* neither a0 nor a1 is read */
    (void)a1;
    ENTER(s, 0x001B76D0u);
    if (!out)
        return out_null(s, 0x001B76D0u);
    v = f_1B76D0(r, s->sp, a2);
    *out = v;
    return 0;
}

int em_area01_sys_001E7CB0(EmArea01Sys *s, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x001E7CB0u);
    if (!out)
        return out_null(s, 0x001E7CB0u);
    v = f_1E7CB0(r);
    *out = v;
    return 0;
}

int em_area01_sys_00128C10(EmArea01Sys *s, uint32_t e)
{
    ENTER(s, 0x00128C10u);
    f_128C10(r, s->sp, e);
    return 0;
}

int em_area01_sys_00159B90(EmArea01Sys *s, uint32_t p)
{
    ENTER(s, 0x00159B90u);
    f_159B90(r, s->sp, p);
    return 0;
}

int em_area01_sys_0015A2C0(EmArea01Sys *s, uint32_t p)
{
    ENTER(s, 0x0015A2C0u);
    f_15A2C0(r, s->sp, p);
    return 0;
}

int em_area01_sys_0019B4C0(EmArea01Sys *s, uint32_t a0, uint32_t a1, uint32_t a2, int32_t flags, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x0019B4C0u);
    if (!out)
        return out_null(s, 0x0019B4C0u);
    v = f_19B4C0(r, s->sp, a0, a1, a2, flags);
    *out = v;
    return 0;
}

int em_area01_sys_0019CF50(EmArea01Sys *s, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x0019CF50u);
    if (!out)
        return out_null(s, 0x0019CF50u);
    v = f_19CF50(r, s->sp);
    *out = v;
    return 0;
}

int em_area01_sys_001A06A0(EmArea01Sys *s, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x001A06A0u);
    if (!out)
        return out_null(s, 0x001A06A0u);
    v = f_1A06A0(r, s->sp);
    *out = v;
    return 0;
}

int em_area01_sys_001E3D90(EmArea01Sys *s, uint32_t p)
{
    ENTER(s, 0x001E3D90u);
    f_1E3D90(r, s->sp, p);
    return 0;
}

int em_area01_sys_001E7C60(EmArea01Sys *s, uint32_t p, uint32_t value_bits)
{
    ENTER(s, 0x001E7C60u);
    f_1E7C60(r, p, value_bits);
    return 0;
}

int em_area01_sys_001E7D20(EmArea01Sys *s, uint32_t a0)
{
    ENTER(s, 0x001E7D20u);
    f_1E7D20(r, s->sp, a0);
    return 0;
}
