/* em_area01_exitb.c - AREA01 lane EXITB translations (see em_area01_exitb.h
 * and docs/AREA01_EXITB.md). Every routine names its original address; the
 * comments say what the original does, never how it is encoded. */
#include "em_area01_exitb.h"

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#include "em_ee_float.h"

/* ------------------------------------------------------------------------
 * Fail-stop plumbing
 * ---------------------------------------------------------------------- */

typedef struct {
    EmArea01Exitb *s;
    jmp_buf out;
} Run;

static void fault(Run *r, int32_t code, uint32_t address)
{
    if (r->s->fault == EM_AREA01_EXITB_FAULT_NONE) {
        r->s->fault = code;
        r->s->fault_address = address;
    }
    longjmp(r->out, 1);
}

void em_area01_exitb_clear_fault(EmArea01Exitb *s)
{
    if (!s)
        return;
    s->fault = EM_AREA01_EXITB_FAULT_NONE;
    s->fault_function = 0;
    s->fault_address = 0;
}

/* The byte span [address, address + size) inside one region, or a fault. */
static uint8_t *span(Run *r, uint32_t address, uint32_t size, int write)
{
    const EmArea01Exitb *s = r->s;
    unsigned i;
    if (s->view) {
        uint8_t *p = size && (uint64_t)address + size <= UINT64_C(0x100000000)
                         ? s->view(s->ctx, address, size, write) : NULL;
        if (p) return p;
        fault(r, EM_AREA01_EXITB_FAULT_UNMAPPED, address);
    }
    for (i = 0; s->regions && i < s->region_count; i++) {
        const EmArea01ExitbRegion *g = &s->regions[i];
        if (g->bytes && address >= g->base && size <= g->size && address - g->base <= g->size - size)
            return g->bytes + (address - g->base);
    }
    fault(r, EM_AREA01_EXITB_FAULT_UNMAPPED, address);
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

/* Test hook. A build that defines EM_AREA01_EXITB_STORE_TRACE as a function
 * name reports every store (original address, size) to it once the address
 * is known to be mapped; tools/test_area01_exitb_reference.py uses it to
 * compare memory with the original at every call leaving the module.
 * Ordinary builds compile it out. wr is the only store path. */
#ifdef EM_AREA01_EXITB_STORE_TRACE
void EM_AREA01_EXITB_STORE_TRACE(uint32_t address, unsigned size);
#define TRACE_STORE(a, n) EM_AREA01_EXITB_STORE_TRACE((a), (n))
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
static uint32_t u16(Run *r, uint32_t a) { return rd(r, a, 2); }
static int32_t s16(Run *r, uint32_t a) { return (int32_t)(int16_t)rd(r, a, 2); }
static uint32_t w32(Run *r, uint32_t a) { return rd(r, a, 4); }
static void st8(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 1); }
static void st16(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 2); }
static void st32(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 4); }

/* 32-bit value as the EE holds it in a 64-bit register. */
static uint64_t reg(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

/* ------------------------------------------------------------------------
 * Calls leaving the module
 * ---------------------------------------------------------------------- */

/* One call: `na` integer registers (a0..a3 images) and `nf` float
 * registers (f12, f13 bits). Returns the call record (v0 / f0). */
static EmArea01ExitbCall callx(Run *r, uint32_t sp, uint32_t fn, unsigned na, uint64_t a0, uint64_t a1,
                               uint64_t a2, uint64_t a3, unsigned nf, uint32_t f12, uint32_t f13)
{
    EmArea01ExitbCall c;
    memset(&c, 0, sizeof c);
    c.fn = fn;
    c.sp = sp;
    c.na = na;
    c.nf = nf;
    c.a[0] = a0;
    c.a[1] = a1;
    c.a[2] = a2;
    c.a[3] = a3;
    c.f[0] = f12;
    c.f[1] = f13;
    if (!r->s->call)
        fault(r, EM_AREA01_EXITB_FAULT_NULL, fn);
    if (r->s->call(r->s->ctx, &c) < 0)
        fault(r, EM_AREA01_EXITB_FAULT_WORKER, fn);
    return c;
}

/* Integer calls; the result is the callee's full v0 register. */
static uint64_t call1(Run *r, uint32_t sp, uint32_t fn, uint32_t a)
{
    return callx(r, sp, fn, 1, reg(a), 0, 0, 0, 0, 0, 0).v0;
}
static uint64_t call2(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b)
{
    return callx(r, sp, fn, 2, reg(a), reg(b), 0, 0, 0, 0, 0).v0;
}
static uint64_t call3(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b, uint32_t c)
{
    return callx(r, sp, fn, 3, reg(a), reg(b), reg(c), 0, 0, 0, 0).v0;
}
static uint64_t call4(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    return callx(r, sp, fn, 4, reg(a), reg(b), reg(c), reg(d), 0, 0, 0).v0;
}
/* (a0, a1; f12) */
static void call2f(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b, uint32_t f12)
{
    (void)callx(r, sp, fn, 2, reg(a), reg(b), 0, 0, 1, f12, 0);
}
/* float(f12) -> f0 */
static uint32_t fcall1(Run *r, uint32_t sp, uint32_t fn, uint32_t x)
{
    return callx(r, sp, fn, 0, 0, 0, 0, 0, 1, x, 0).f0;
}
/* float(f12, f13) -> f0 */
static uint32_t fcall2(Run *r, uint32_t sp, uint32_t fn, uint32_t x, uint32_t y)
{
    return callx(r, sp, fn, 0, 0, 0, 0, 0, 2, x, y).f0;
}

/* ------------------------------------------------------------------------
 * Constants and callees
 * ---------------------------------------------------------------------- */

#define F_ZERO     0x00000000u
#define F_ONE      0x3F800000u
#define F_TWO      0x40000000u
#define F_HALF     0x3F000000u
#define F_PI       0x40490FDBu
#define F_NEG_PI   0xC0490FDBu
#define F_2POW31   0x4F000000u /* 2147483648.0 */

#define FN_COPY_QW      0x00102948u /* one quadword copy (dst, src) */
#define FN_COPY_QW4     0x00102958u /* four quadword copy (dst, src) */
#define FN_MAT_VEC      0x001026A0u /* (out, matrix, vector) */
#define FN_MAT_MUL      0x001026D0u /* (out, a, b) */
#define FN_CROSS        0x00102718u /* (out, a, b) */
#define FN_NORM         0x00102760u /* (out, in) */
#define FN_TRANSPOSE    0x00102798u /* (out, in) */
#define FN_VADD         0x001028B8u /* (out, a, b) */
#define FN_VSUB         0x001028D0u /* (out, a, b) */
#define FN_VSCALE_B     0x00102900u /* (out, in; f12) */
#define FN_IDENT        0x001029C0u /* (matrix) */
#define FN_ROT_A        0x00102A60u /* (out, in; f12) */
#define FN_ROT_B        0x00102BB0u /* (out, in; f12) */
#define FN_VSCALE       0x00103230u /* (out, in; f12) */
#define FN_11DE90       0x0011DE90u /* float(f12) */
#define FN_11E2A8       0x0011E2A8u /* float(f12) */
#define FN_11E398       0x0011E398u /* float(f12) */
#define FN_11E620       0x0011E620u /* float(f12, f13) */
#define FN_11E748       0x0011E748u /* float(f12) */
#define FN_BLOCK_COPY   0x00121870u /* (dst, src, bytes) */
#define FN_RAND         0x00122BB8u /* the LCG draw */
#define FN_FLOAT_TO_INT 0x001281C0u /* int(f12) */

#define FN_15AC00 0x0015AC00u
#define FN_15AE20 0x0015AE20u
#define FN_19AB20 0x0019AB20u
#define FN_19B4C0 0x0019B4C0u
#define FN_1AFC10 0x001AFC10u
#define FN_1B0FD0 0x001B0FD0u
#define FN_1B1190 0x001B1190u
#define FN_1B17A0 0x001B17A0u
#define FN_1B6F00 0x001B6F00u
#define FN_1BA1A0 0x001BA1A0u
#define FN_1BA1F0 0x001BA1F0u
#define FN_1C5570 0x001C5570u
#define FN_1C6120 0x001C6120u
#define FN_1C6380 0x001C6380u
#define FN_1CB5F0 0x001CB5F0u
#define FN_1CB6B0 0x001CB6B0u
#define FN_1CB760 0x001CB760u
#define FN_1CB950 0x001CB950u
#define FN_1F1110 0x001F1110u
#define FN_1F1180 0x001F1180u
#define FN_1F4A10 0x001F4A10u
#define FN_1FB9F0 0x001FB9F0u

/* Globals (original addresses). */
#define G_275B40  0x00275B40u /* pointer to two record pointers */
#define G_275BF8  0x00275BF8u
#define G_275C18  0x00275C18u /* base of the 0xA060-byte records (001E8E80 / 001E9280) */
#define G_275C1C  0x00275C1Cu /* base of the 0xA060-byte records (001E9580) */
#define G_275670  0x00275670u
#define G_275674  0x00275674u
#define G_AREA    0x00810700u /* area byte; 0x810701 the sub byte */
#define G_BITS    0x00810841u /* one byte per area */
#define G_810C87  0x00810C87u
#define G_810C88  0x00810C88u
#define G_81076A  0x0081076Au

#define SPR(x) (0x70000000u + (x))

/* The per-record tail shared by the owners: 001B17A0(p), then the handler
 * the record holds at +0x4C (loaded after that call) with p. */
static void tail_handler(Run *r, uint32_t sp, uint32_t p)
{
    call1(r, sp, FN_1B17A0, p);
    call1(r, sp, w32(r, p + 0x4C), p);
}

/* The bit (1 << b) of D_00810841[area] for the halfword or byte b: the
 * original shifts 1 by the low five bits of b. */
static uint32_t area_bit_set(Run *r, uint32_t b)
{
    uint32_t idx = u8(r, G_AREA);
    uint32_t byte = u8(r, G_BITS + idx);
    return byte & (1u << (b & 31u));
}

static void area_bit_or(Run *r, uint32_t b)
{
    uint32_t idx = u8(r, G_AREA);
    uint32_t at = G_BITS + idx;
    st8(r, at, u8(r, at) | (1u << (b & 31u)));
}

/* ------------------------------------------------------------------------
 * 001576E0(a0, a1): only when bit 2 of byte a0+0x0B is set (otherwise 0).
 * The scratch vector 0x700038A0 = (0.3, 0, 5.0, 1.0); 001B6F00(a0,
 * 0x700038A0; f12 = pi); byte a0+0 = 2. Then by byte a0+3: for 0x12 and
 * 0x2F the flag D_00810C87 decides, otherwise D_00810C88. Flag set:
 * D_00246CB4 = 0x154, 001BA1A0(a1, 0x246C20), 001BA1F0(a0), result 1.
 * Flag clear: D_00246FB4 = 0x80000014 (0x12 / 0x2F) or 0x80000016 (other),
 * 001BA1A0(a1, 0x246F20), 001BA1F0(a0), result 2.
 * ---------------------------------------------------------------------- */
static int32_t f_1576E0(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    uint32_t type;
    int flag;
    sp -= 0x30;
    if (!(u8(r, a0 + 0x0B) & 4u))
        return 0;
    st32(r, SPR(0x38A0), 0x3E99999Au);
    st32(r, SPR(0x38A4), 0);
    st32(r, SPR(0x38A8), 0x40A00000u);
    st32(r, SPR(0x38AC), F_ONE);
    (void)callx(r, sp, FN_1B6F00, 2, reg(a0), reg(SPR(0x38A0)), 0, 0, 1, F_PI, 0);
    st8(r, a0, 2);
    type = u8(r, a0 + 3);
    if (type == 0x12u || type == 0x2Fu) {
        flag = u8(r, G_810C87) != 0;
        if (!flag)
            st32(r, 0x00246FB4u, 0x80000014u);
    } else {
        flag = u8(r, G_810C88) != 0;
        if (!flag)
            st32(r, 0x00246FB4u, 0x80000016u);
    }
    if (flag) {
        st32(r, 0x00246CB4u, 0x154u);
        call2(r, sp, FN_1BA1A0, a1, 0x00246C20u);
        call1(r, sp, FN_1BA1F0, a0);
        return 1;
    }
    call2(r, sp, FN_1BA1A0, a1, 0x00246F20u);
    call1(r, sp, FN_1BA1F0, a0);
    return 2;
}

/* ------------------------------------------------------------------------
 * 00156F30(p): by state byte +4.
 *   0: when 001B0FD0(p) gives 0: byte +0 = 1, 001C6380(p), then the four
 *      quadwords at +0x90 of the two records D_00275B40 points at ([0], [1],
 *      each read afresh) are copied to p+0x1F0 and p+0x230; state 4.
 *   4: when halfword +0x36 is set, the swing restarts (below, with the
 *      +0x2E4 angle cleared); then 001B17A0(p) and the +0x4C handler.
 *   1: angle +0x2E4 += step +0x2E0; if it is not <= pi it becomes -pi,
 *      else if it is < -pi it becomes pi. 0x70003A20 = +0x2E8 *
 *      0011E2A8(angle); 0x700036A0 = identity, rotated by 00102A60 with
 *      0x70003A20; 001C6380(p); both records' +0x90 matrices are multiplied
 *      by it (001026D0, in place). Then 0x700038A0 = (4 * 0011E398(0x70003A20),
 *      0, 0, 0) is put through record [1]'s matrix into 0x700038B0, and its x
 *      and z are subtracted from record [1]'s +0xC0 and +0xC8. +0x2E8 -=
 *      0.001; below 0 the state becomes 4. When +0x36 is set the swing
 *      restarts (without clearing +0x2E4). Then 001B17A0(p) and the handler.
 *   3: 001AFC10(p).
 *   2 and 5..255: nothing.
 * The restart: state 1, +0x2E8 = 0.2, (+0x2E4 = 0 in state 4), +0x36 = 0;
 * 0x700038A0 = (p+0xB0) - D_00810350 (001028D0 subtracts its third argument
 * from its second); 0x700036A0 = identity rotated
 * by 00102BB0 with -(+0xC4); 0x700038A0 through that matrix (001026A0); the
 * step +0x2E0 = 0.08 when its x is below 0, else -0.08.
 * ---------------------------------------------------------------------- */
static void swing_restart(Run *r, uint32_t sp, uint32_t p, int clear_angle)
{
    st8(r, p + 4, 1);
    st32(r, p + 0x2E8, 0x3E4CCCCDu);
    if (clear_angle)
        st32(r, p + 0x2E4, 0);
    st16(r, p + 0x36, 0);
    call3(r, sp, FN_VSUB, SPR(0x38A0), p + 0xB0, 0x00810350u);
    call1(r, sp, FN_IDENT, SPR(0x36A0));
    call2f(r, sp, FN_ROT_B, SPR(0x36A0), SPR(0x36A0), em_ee_neg_bits(w32(r, p + 0xC4)));
    call3(r, sp, FN_MAT_VEC, SPR(0x38A0), SPR(0x36A0), SPR(0x38A0));
    if (em_ee_c_lt_bits(w32(r, SPR(0x38A0)), F_ZERO))
        st32(r, p + 0x2E0, 0x3DA3D70Au);
    else
        st32(r, p + 0x2E0, 0xBDA3D70Au);
}

static void f_156F30(Run *r, uint32_t sp, uint32_t p)
{
    uint32_t st, t, a, x, q;
    sp -= 0x40;
    st = u8(r, p + 4);
    if (st == 3) {
        call1(r, sp, FN_1AFC10, p);
        return;
    }
    if (st == 1) {
        x = em_ee_add_bits(w32(r, p + 0x2E4), w32(r, p + 0x2E0));
        st32(r, p + 0x2E4, x);
        if (!em_ee_c_le_bits(x, F_PI))
            st32(r, p + 0x2E4, F_NEG_PI);
        else if (em_ee_c_lt_bits(x, F_NEG_PI))
            st32(r, p + 0x2E4, F_PI);
        x = fcall1(r, sp, FN_11E2A8, w32(r, p + 0x2E4));
        st32(r, SPR(0x3A20), em_ee_mul_bits(w32(r, p + 0x2E8), x));
        call1(r, sp, FN_IDENT, SPR(0x36A0));
        call2f(r, sp, FN_ROT_A, SPR(0x36A0), SPR(0x36A0), w32(r, SPR(0x3A20)));
        call1(r, sp, FN_1C6380, p);
        a = w32(r, w32(r, G_275B40)) + 0x90;
        call3(r, sp, FN_MAT_MUL, a, a, SPR(0x36A0));
        a = w32(r, w32(r, G_275B40) + 4) + 0x90;
        call3(r, sp, FN_MAT_MUL, a, a, SPR(0x36A0));
        st32(r, SPR(0x38AC), 0);
        st32(r, SPR(0x38A8), 0);
        st32(r, SPR(0x38A4), 0);
        x = fcall1(r, sp, FN_11E398, w32(r, SPR(0x3A20)));
        t = w32(r, G_275B40);
        st32(r, SPR(0x38A0), em_ee_mul_bits(0x40800000u, x));
        call3(r, sp, FN_MAT_VEC, SPR(0x38B0), w32(r, t + 4) + 0x90, SPR(0x38A0));
        t = w32(r, G_275B40);
        x = w32(r, SPR(0x38B0));
        q = w32(r, t + 4);
        st32(r, q + 0xC0, em_ee_sub_bits(w32(r, q + 0xC0), x));
        t = w32(r, G_275B40);
        x = w32(r, SPR(0x38B8));
        q = w32(r, t + 4);
        st32(r, q + 0xC8, em_ee_sub_bits(w32(r, q + 0xC8), x));
        x = em_ee_sub_bits(w32(r, p + 0x2E8), 0x3A83126Fu);
        st32(r, p + 0x2E8, x);
        if (em_ee_c_lt_bits(x, F_ZERO))
            st8(r, p + 4, 4);
        if (s16(r, p + 0x36) != 0)
            swing_restart(r, sp, p, 0);
        tail_handler(r, sp, p);
        return;
    }
    if (st == 4) {
        if (s16(r, p + 0x36) != 0)
            swing_restart(r, sp, p, 1);
        tail_handler(r, sp, p);
        return;
    }
    if (st != 0)
        return;
    if (call1(r, sp, FN_1B0FD0, p) != 0)
        return;
    st8(r, p, 1);
    call1(r, sp, FN_1C6380, p);
    call2(r, sp, FN_COPY_QW4, p + 0x1F0, w32(r, w32(r, G_275B40)) + 0x90);
    call2(r, sp, FN_COPY_QW4, p + 0x230, w32(r, w32(r, G_275B40) + 4) + 0x90);
    st8(r, p + 4, 4);
}

/* ------------------------------------------------------------------------
 * 001581A0(p): by state byte +4.
 *   0: when the bit (halfword +0x2E) of D_00810841[area] is set: state 3.
 *      Otherwise 001B0FD0(p), 001C6380(p), byte +0 = 1, 001F1110(p, 2).
 *   1: when halfword +0x36 is set: bytes +0 and +4 = 2, the bit (byte
 *      +0x2E) is set in D_00810841[area], 001FB9F0(0x3F1, 0x1000, 0x1000,
 *      0x1000). Then 001F1180(p), 001B17A0(p) and the +0x4C handler.
 *   2, 3: 001AFC10(p).  4..255: nothing.
 * ---------------------------------------------------------------------- */
static void f_1581A0(Run *r, uint32_t sp, uint32_t p)
{
    uint32_t st;
    sp -= 0x20;
    st = u8(r, p + 4);
    if (st == 3 || st == 2) {
        call1(r, sp, FN_1AFC10, p);
        return;
    }
    if (st == 1) {
        if (s16(r, p + 0x36) != 0) {
            st8(r, p, 2);
            st8(r, p + 4, 2);
            area_bit_or(r, u8(r, p + 0x2E));
            call4(r, sp, FN_1FB9F0, 0x3F1u, 0x1000u, 0x1000u, 0x1000u);
        }
        call1(r, sp, FN_1F1180, p);
        tail_handler(r, sp, p);
        return;
    }
    if (st != 0)
        return;
    if (area_bit_set(r, u16(r, p + 0x2E)) & 0xFFu) {
        st8(r, p + 4, 3);
        return;
    }
    call1(r, sp, FN_1B0FD0, p);
    call1(r, sp, FN_1C6380, p);
    st8(r, p, 1);
    call2(r, sp, FN_1F1110, p, 2);
}

/* ------------------------------------------------------------------------
 * 00158810(p): by state byte +4 (e = p+0x1F0 is handed to 001576E0).
 *   0: 001B0FD0(p), 001C6380(p), +0x20 = 0, +0x30 = &D_00275470. When the
 *      bit (halfword +0x2E) of D_00810841[area] is set: byte +0 = 2, sub +5
 *      = 4, done. Otherwise, for type +3 == 0x2F with D_00810C87 set and
 *      D_0081076A clear, state 2; byte +0 = 1; 0x700038A0 = (1, 0, 0, 1);
 *      +0x20 = 001C5570(p, 0x700038A0, 0x79 for type 0x13 else 0x78, 1).
 *   1: by sub +5:
 *      0: type 0x2F with D_00810C87 set and D_0081076A clear: state 2.
 *         Otherwise 001576E0(p, e): 1 -> sub + 1 (read again), 2 or any
 *         other nonzero -> sub 3.
 *      1: when 001BA1F0(p) is nonzero: +0x0B = 0, byte +0 = 2, sub + 1.
 *      2: when the pointer +0x20 is set, its byte +4 = 3 and +0x20 = 0;
 *         sub 4.
 *      3: when 001BA1F0(p) is nonzero: +0x0B = 0, byte +0 = 1, sub 0.
 *      4..255: nothing.
 *      Then 001B17A0(p) and the +0x4C handler.
 *   2: by sub +5:
 *      0: when 001576E0(p, e) is nonzero: byte +0 = 2, D_0081076A = 1,
 *         sub + 1 (read again).
 *      1: when the halfword 0x70003B84 is 0: sub 2.
 *      2: when 0x70003B84 is 0x64: 001FB9F0(0x3F3, 0x1000, 0x1000, 0x1000).
 *         Then (0x70003B84 read again) when it is 0x78 or more, or the byte
 *         0x70003B91 is 2 or more: sub 2, state 1, and the bit (byte +0x2E)
 *         set in D_00810841[area].
 *      3..255: nothing.
 *      Then 001B17A0(p) and the +0x4C handler.
 *   3: 001AFC10(p).  4..255: nothing.
 * ---------------------------------------------------------------------- */
static int gate_2f(Run *r, uint32_t p)
{
    return u8(r, p + 3) == 0x2Fu && u8(r, G_810C87) != 0 && u8(r, G_81076A) == 0;
}

static void f_158810(Run *r, uint32_t sp, uint32_t p)
{
    uint32_t st, sub, e = p + 0x1F0u, q;
    int32_t v;
    sp -= 0x20;
    st = u8(r, p + 4);
    if (st == 3) {
        call1(r, sp, FN_1AFC10, p);
        return;
    }
    if (st == 2) {
        sub = u8(r, p + 5);
        if (sub == 2) {
            if (u16(r, SPR(0x3B84)) == 0x64u)
                call4(r, sp, FN_1FB9F0, 0x3F3u, 0x1000u, 0x1000u, 0x1000u);
            if (u16(r, SPR(0x3B84)) >= 0x78u || u8(r, SPR(0x3B91)) >= 2u) {
                st8(r, p + 5, 2);
                st8(r, p + 4, 1);
                area_bit_or(r, u8(r, p + 0x2E));
            }
        } else if (sub == 1) {
            if (u16(r, SPR(0x3B84)) == 0)
                st8(r, p + 5, sub + 1);
        } else if (sub == 0) {
            if (f_1576E0(r, sp, p, e) != 0) {
                st8(r, p, 2);
                st8(r, G_81076A, 1);
                st8(r, p + 5, u8(r, p + 5) + 1);
            }
        }
        tail_handler(r, sp, p);
        return;
    }
    if (st == 1) {
        sub = u8(r, p + 5);
        if (sub == 3) {
            if (call1(r, sp, FN_1BA1F0, p) != 0) {
                st8(r, p + 0x0B, 0);
                st8(r, p, 1);
                st8(r, p + 5, 0);
            }
        } else if (sub == 2) {
            q = w32(r, p + 0x20);
            if (q != 0) {
                st8(r, q + 4, 3);
                st32(r, p + 0x20, 0);
            }
            st8(r, p + 5, 4);
        } else if (sub == 1) {
            if (call1(r, sp, FN_1BA1F0, p) != 0) {
                st8(r, p + 0x0B, 0);
                st8(r, p, 2);
                st8(r, p + 5, u8(r, p + 5) + 1);
            }
        } else if (sub == 0) {
            if (gate_2f(r, p)) {
                st8(r, p + 4, 2);
            } else {
                v = f_1576E0(r, sp, p, e);
                if (v == 1)
                    st8(r, p + 5, u8(r, p + 5) + 1);
                else if (v != 0)
                    st8(r, p + 5, 3);
            }
        }
        tail_handler(r, sp, p);
        return;
    }
    if (st != 0)
        return;
    call1(r, sp, FN_1B0FD0, p);
    call1(r, sp, FN_1C6380, p);
    st32(r, p + 0x20, 0);
    st32(r, p + 0x30, 0x00275470u);
    if (area_bit_set(r, u16(r, p + 0x2E))) {
        st8(r, p, 2);
        st8(r, p + 5, 4);
        return;
    }
    if (gate_2f(r, p))
        st8(r, p + 4, 2);
    st8(r, p, 1);
    st32(r, SPR(0x38A0), F_ONE);
    st32(r, SPR(0x38A4), 0);
    st32(r, SPR(0x38A8), 0);
    st32(r, SPR(0x38AC), F_ONE);
    q = u8(r, p + 3) == 0x13u ? 0x79u : 0x78u;
    st32(r, p + 0x20, (uint32_t)call4(r, sp, FN_1C5570, p, SPR(0x38A0), q, 1));
}

/* ------------------------------------------------------------------------
 * 00158BD0(p): by state byte +4.
 *   0: 001B0FD0(p).
 *   1: 001C6380(p); when 001B17A0(p) is nonzero, the +0x4C handler. Then
 *      0x700038B0 = (0, 0x80, 0, 0x80) when the bit (halfword +0x2E) of
 *      D_00810841[area] is set, else (0x80, 0, 0, 0x80); 0x700038C0 =
 *      (1, 1, 1, 1); 001F4A10(p+0xD0, 0x700038B0, 0x700038C0).
 *   2, 3: 001AFC10(p).  4..255: nothing.
 * ---------------------------------------------------------------------- */
static void f_158BD0(Run *r, uint32_t sp, uint32_t p)
{
    uint32_t st;
    sp -= 0x20;
    st = u8(r, p + 4);
    if (st == 3 || st == 2) {
        call1(r, sp, FN_1AFC10, p);
        return;
    }
    if (st == 0) {
        call1(r, sp, FN_1B0FD0, p);
        return;
    }
    if (st != 1)
        return;
    call1(r, sp, FN_1C6380, p);
    if (call1(r, sp, FN_1B17A0, p) != 0)
        call1(r, sp, w32(r, p + 0x4C), p);
    if (area_bit_set(r, u16(r, p + 0x2E))) {
        st32(r, SPR(0x38B0), 0);
        st32(r, SPR(0x38B4), 0x80);
        st32(r, SPR(0x38B8), 0);
        st32(r, SPR(0x38BC), 0x80);
    } else {
        st32(r, SPR(0x38B0), 0x80);
        st32(r, SPR(0x38B4), 0);
        st32(r, SPR(0x38B8), 0);
        st32(r, SPR(0x38BC), 0x80);
    }
    st32(r, SPR(0x38C0), F_ONE);
    st32(r, SPR(0x38C4), F_ONE);
    st32(r, SPR(0x38C8), F_ONE);
    st32(r, SPR(0x38CC), F_ONE);
    call3(r, sp, FN_1F4A10, p + 0xD0, SPR(0x38B0), SPR(0x38C0));
}

/* ------------------------------------------------------------------------
 * 001E8E80(a0, a1, a2): the record R = D_00275C18 + a1 * 0xA060 (32-bit).
 * Constants: +0x40 0.7, +0x44 0.445, +0x48 255, +0x4C 0, +0x50 -1.3; +0/4/8
 * = a0+0xB0/B4/B8; +0x30 = a2[0], +0x34 = a2[2]; +0x3C and +0x38 = 0.325,
 * +0x10/14 = 83, +0x18 = 95, +0x1C = 0, +0x20 0.991, +0x24 0.022, +0x2C 0.4,
 * +0x28 0.05, then +0x38 = 0.85 and +0x3C = 0.82 (the stores in that order,
 * given as bit patterns below). Then for j = 0..7 (rows 0x200 apart) and
 * i = 0..7 (cells 0x10 apart) the cell C:
 *   C+0x60 = (R[0] - 0.5 * R[0x30]) + R[0x30] * i / 6;
 *   C+0x68 = (R[8] - 0.5 * R[0x34]) + R[0x34] * j / 6;  C+0x64 = R[4];
 *   C+0x60 += 0.1 * (rand / 2^31) - 0.05;  C+0x68 += the same (next draw);
 *   t = (3 - sqrt((i-3)^2 + (j-3)^2)) / 4 (the sum as an EE multiply-add);
 *   unless t <= 0.1: C+0x64 += 0.15, then += 0.05 * rand / 2^31;
 *   t = 1 unless t <= 0.35, where t *= 2; t < 0 becomes 0;
 *   C+0x6C = 128 * t; C+0x4060 = i / 8; C+0x4064 = j / 8; and the words
 *   R + j*0x80 + i*4 + 0x8060 / + 0x9060 = 0.
 * Finally a0 bytes +0x0C = 0, +9 = 0, +4 = 1.
 * ---------------------------------------------------------------------- */
static uint32_t rand_word(Run *r, uint32_t sp)
{
    return (uint32_t)callx(r, sp, FN_RAND, 0, 0, 0, 0, 0, 0, 0, 0).v0;
}

/* The two lattice set-ups share the per-cell dome height rule:
 * t = (3 - sqrt(dd)) / 4 with dd already formed; unless t <= 0.1 the
 * height C+0x64 += 0.15 and then += 0.05 * rand / 2^31 (C+0x64 read again);
 * t = 1 unless t <= limit, where t *= 2; t < 0 becomes +0. */
static uint32_t dome(Run *r, uint32_t sp, uint32_t c, uint32_t root, uint32_t limit)
{
    uint32_t t = em_ee_div_bits(em_ee_sub_bits(0x40400000u, root), 0x40800000u);
    if (!em_ee_c_le_bits(t, 0x3DCCCCCDu)) {
        uint32_t h, d;
        st32(r, c + 0x64, em_ee_add_bits(w32(r, c + 0x64), 0x3E19999Au));
        d = em_ee_mul_bits(0x3D4CCCCDu, em_ee_cvt_s_w_bits(rand_word(r, sp)));
        h = w32(r, c + 0x64);
        st32(r, c + 0x64, em_ee_add_bits(h, em_ee_div_bits(d, F_2POW31)));
    }
    if (!em_ee_c_le_bits(t, limit))
        t = F_ONE;
    else
        t = em_ee_mul_bits(t, F_TWO);
    if (em_ee_c_lt_bits(t, F_ZERO))
        t = F_ZERO;
    return t;
}

/* C + off = (R[b] - 0.5 * R[s]) + R[s] * n / 6, in the original's order. */
static uint32_t lattice_axis(uint32_t base, uint32_t spread, int32_t n)
{
    uint32_t a = em_ee_mul_bits(spread, em_ee_cvt_s_w_bits((uint32_t)n));
    uint32_t h = em_ee_sub_bits(base, em_ee_mul_bits(F_HALF, spread));
    return em_ee_add_bits(h, em_ee_div_bits(a, 0x40C00000u));
}

static void f_1E8E80(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t R, c, x, d, dd;
    int32_t i, j;
    sp -= 0xA0;
    R = w32(r, G_275C18) + (uint32_t)((uint64_t)a1 * 0xA060u);
    st32(r, R + 0x40, 0x3F333333u);
    st32(r, R + 0x44, 0x3EE3D70Au);
    st32(r, R + 0x48, 0x437F0000u);
    st32(r, R + 0x4C, 0);
    st32(r, R + 0x50, 0xBFA66666u);
    st32(r, R + 0x00, w32(r, a0 + 0xB0));
    st32(r, R + 0x04, w32(r, a0 + 0xB4));
    st32(r, R + 0x08, w32(r, a0 + 0xB8));
    st32(r, R + 0x30, w32(r, a2 + 0));
    st32(r, R + 0x34, w32(r, a2 + 8));
    st32(r, R + 0x3C, 0x3EA66666u);
    st32(r, R + 0x38, 0x3EA66666u);
    st32(r, R + 0x10, 0x42A60000u);
    st32(r, R + 0x14, 0x42A60000u);
    st32(r, R + 0x18, 0x42BE0000u);
    st32(r, R + 0x1C, 0);
    st32(r, R + 0x20, 0x3F7DB22Du);
    st32(r, R + 0x24, 0x3CB43958u);
    st32(r, R + 0x2C, 0x3ECCCCCDu);
    st32(r, R + 0x28, 0x3D4CCCCDu);
    st32(r, R + 0x38, 0x3F59999Au);
    st32(r, R + 0x3C, 0x3F51EB85u);
    for (j = 0; j < 8; j++) {
        for (i = 0; i < 8; i++) {
            c = R + (uint32_t)j * 0x200u + (uint32_t)i * 0x10u;
            st32(r, c + 0x60, lattice_axis(w32(r, R + 0x00), w32(r, R + 0x30), i));
            st32(r, c + 0x68, lattice_axis(w32(r, R + 0x08), w32(r, R + 0x34), j));
            st32(r, c + 0x64, w32(r, R + 0x04));
            d = em_ee_div_bits(em_ee_cvt_s_w_bits(rand_word(r, sp)), F_2POW31);
            x = w32(r, c + 0x60);
            st32(r, c + 0x60, em_ee_add_bits(x, em_ee_sub_bits(em_ee_mul_bits(0x3DCCCCCDu, d), 0x3D4CCCCDu)));
            d = em_ee_div_bits(em_ee_cvt_s_w_bits(rand_word(r, sp)), F_2POW31);
            x = w32(r, c + 0x68);
            st32(r, c + 0x68, em_ee_add_bits(x, em_ee_sub_bits(em_ee_mul_bits(0x3DCCCCCDu, d), 0x3D4CCCCDu)));
            dd = em_ee_mula_bits(em_ee_cvt_s_w_bits((uint32_t)(i - 3)), em_ee_cvt_s_w_bits((uint32_t)(i - 3)));
            dd = em_ee_madd_bits(dd, em_ee_cvt_s_w_bits((uint32_t)(j - 3)), em_ee_cvt_s_w_bits((uint32_t)(j - 3)));
            x = dome(r, sp, c, fcall1(r, sp, FN_11E748, dd), 0x3EB33333u);
            st32(r, c + 0x6C, em_ee_mul_bits(0x43000000u, x));
            st32(r, c + 0x4060, em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)i), 0x41000000u));
            st32(r, c + 0x4064, em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)j), 0x41000000u));
            st32(r, R + (uint32_t)j * 0x80u + (uint32_t)i * 4u + 0x8060u, 0);
            st32(r, R + (uint32_t)j * 0x80u + (uint32_t)i * 4u + 0x9060u, 0);
        }
    }
    st8(r, a0 + 0x0C, 0);
    st8(r, a0 + 9, 0);
    st8(r, a0 + 4, 1);
}

/* ------------------------------------------------------------------------
 * 001E9580(a0, a1, a2): the record R = D_00275C1C + a1 * 0xA060 (32-bit).
 * Defaults +0x40 0.005, +0x44 0.445, +0x48 50, +0x4C -9, +0x50 -1.7;
 * +0/4/8 = a0+0xB0/B4/B8; +0x30 = a2[0], +0x34 = a2[2]. Then by the key
 * (D_00810700 << 8) + D_00810701 one row of the table below sets byte +0x5C
 * and the words +0x48, +0x4C, +0x50, +0x40, +0x10, +0x14, +0x18, +0x1C,
 * +0x3C, +0x38 (unlisted keys take the default row). Then +0x20 0.991,
 * +0x24 0.022, +0x2C 0.4, +0x28 0.05. Then the 8 x 8 lattice as in
 * 001E8E80 without the x / z jitter: C+0x60 / +0x68 / +0x64 as there, the
 * root of (i-3)^2 + (j-3)^2 (an EE multiply-add) before the height rule
 * with limit 0.15, C+0x6C = t (not scaled), C+0x4060 = i / 8, C+0x4064 =
 * j / 8, and the two zero words.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t key;
    uint8_t b5c;
    uint32_t w48, w4c, w10, w14, w18, w3c38;
} Area9580;

/* +0x50 is -1.5, +0x40 is 0.014 and +0x1C is 0 in every row. */
static const Area9580 k_9580[] = {
    { 0x0001, 1, 0x414CCCCDu, 0x42400000u, 0x43000000u, 0x42CC0000u, 0x42F40000u, 0 },
    { 0x1001, 0, 0x414CCCCDu, 0x42040000u, 0x43000000u, 0x42CC0000u, 0x42F40000u, 0 },
    { 0x0000, 0, 0x41B66666u, 0x42440000u, 0x43000000u, 0x43000000u, 0x43000000u, 1 },
    { 0x0200, 0, 0x41B66666u, 0x42440000u, 0x43000000u, 0x43000000u, 0x43000000u, 1 },
    { 0x0002, 0, 0x414CCCCDu, 0x42400000u, 0x43000000u, 0x42CC0000u, 0x42F40000u, 1 },
    { 0x0101, 0, 0x414CCCCDu, 0x42400000u, 0x43000000u, 0x42CC0000u, 0x42F40000u, 1 },
    { 0x0601, 0, 0x414CCCCDu, 0x42400000u, 0x43000000u, 0x42CC0000u, 0x42F40000u, 1 },
    { 0x0401, 0, 0x414CCCCDu, 0x42400000u, 0x43000000u, 0x42B00000u, 0x42B00000u, 1 },
    { 0x0202, 0, 0x414CCCCDu, 0x428C0000u, 0x43000000u, 0x42CC0000u, 0x42F40000u, 1 },
    { 0x0600, 1, 0x414CCCCDu, 0x428C0000u, 0x43000000u, 0x42CC0000u, 0x42F40000u, 1 },
    { 0x0100, 1, 0x414CCCCDu, 0x428C0000u, 0x43000000u, 0x42DC0000u, 0x43000000u, 1 },
    { 0x0D00, 1, 0x414CCCCDu, 0x428C0000u, 0x43000000u, 0x42DC0000u, 0x43000000u, 1 },
    { 0x0700, 1, 0x414CCCCDu, 0x42400000u, 0x42FA0000u, 0x42C00000u, 0x43000000u, 1 },
    { 0x0702, 1, 0x414CCCCDu, 0x42400000u, 0x42FE0000u, 0x42D00000u, 0x43000000u, 1 },
    { 0x0703, 1, 0x414CCCCDu, 0x42400000u, 0x42FE0000u, 0x42D00000u, 0x43000000u, 1 },
    { 0x0803, 0, 0x414CCCCDu, 0x42400000u, 0x42C20000u, 0x42BE0000u, 0x43000000u, 1 },
    { 0x1300, 1, 0x414CCCCDu, 0x429C0000u, 0x42BE0000u, 0x42A00000u, 0x43000000u, 1 },
};
/* The default row (also keys 0x300, 0x400, 0x1000, 0x1301, 0x1400). */
static const Area9580 k_9580_default = { 0, 1, 0x414CCCCDu, 0x42400000u, 0x43000000u, 0x42DC0000u, 0x43000000u, 1 };

static void f_1E9580(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t R, c, key, root, dd, x;
    const Area9580 *row = &k_9580_default;
    unsigned k;
    int32_t i, j;
    sp -= 0x90;
    R = w32(r, G_275C1C) + (uint32_t)((uint64_t)a1 * 0xA060u);
    st32(r, R + 0x40, 0x3BA3D70Au);
    st32(r, R + 0x44, 0x3EE3D70Au);
    st32(r, R + 0x48, 0x42480000u);
    st32(r, R + 0x4C, 0xC1100000u);
    st32(r, R + 0x50, 0xBFD9999Au);
    st32(r, R + 0x00, w32(r, a0 + 0xB0));
    st32(r, R + 0x04, w32(r, a0 + 0xB4));
    st32(r, R + 0x08, w32(r, a0 + 0xB8));
    st32(r, R + 0x30, w32(r, a2 + 0));
    st32(r, R + 0x34, w32(r, a2 + 8));
    key = (u8(r, G_AREA) << 8) + u8(r, G_AREA + 1);
    for (k = 0; k < sizeof k_9580 / sizeof k_9580[0]; k++) {
        if (k_9580[k].key == key) {
            row = &k_9580[k];
            break;
        }
    }
    st8(r, R + 0x5C, row->b5c);
    st32(r, R + 0x48, row->w48);
    st32(r, R + 0x4C, row->w4c);
    st32(r, R + 0x50, 0xBFC00000u);
    st32(r, R + 0x40, 0x3C656042u);
    st32(r, R + 0x10, row->w10);
    st32(r, R + 0x14, row->w14);
    st32(r, R + 0x18, row->w18);
    st32(r, R + 0x1C, 0);
    /* +0x3C / +0x38: 0.21 / 0.25 in rows 0x1 and 0x1001, 0.265 elsewhere */
    st32(r, R + 0x3C, row->w3c38 ? 0x3E87AE14u : 0x3E570A3Du);
    st32(r, R + 0x38, row->w3c38 ? 0x3E87AE14u : 0x3E800000u);
    st32(r, R + 0x20, 0x3F7DB22Du);
    st32(r, R + 0x24, 0x3CB43958u);
    st32(r, R + 0x2C, 0x3ECCCCCDu);
    st32(r, R + 0x28, 0x3D4CCCCDu);
    for (j = 0; j < 8; j++) {
        for (i = 0; i < 8; i++) {
            c = R + (uint32_t)j * 0x200u + (uint32_t)i * 0x10u;
            st32(r, c + 0x60, lattice_axis(w32(r, R + 0x00), w32(r, R + 0x30), i));
            dd = em_ee_mula_bits(em_ee_cvt_s_w_bits((uint32_t)(i - 3)), em_ee_cvt_s_w_bits((uint32_t)(i - 3)));
            x = em_ee_sub_bits(w32(r, R + 0x08), em_ee_mul_bits(F_HALF, w32(r, R + 0x34)));
            dd = em_ee_madd_bits(dd, em_ee_cvt_s_w_bits((uint32_t)(j - 3)), em_ee_cvt_s_w_bits((uint32_t)(j - 3)));
            st32(r, c + 0x68,
                 em_ee_add_bits(x, em_ee_div_bits(em_ee_mul_bits(w32(r, R + 0x34), em_ee_cvt_s_w_bits((uint32_t)j)),
                                                  0x40C00000u)));
            st32(r, c + 0x64, w32(r, R + 0x04));
            root = fcall1(r, sp, FN_11E748, dd);
            x = dome(r, sp, c, root, 0x3E19999Au);
            st32(r, c + 0x6C, x);
            st32(r, c + 0x4060, em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)i), 0x41000000u));
            st32(r, c + 0x4064, em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)j), 0x41000000u));
            st32(r, R + (uint32_t)j * 0x80u + (uint32_t)i * 4u + 0x8060u, 0);
            st32(r, R + (uint32_t)j * 0x80u + (uint32_t)i * 4u + 0x9060u, 0);
        }
    }
}

/* ------------------------------------------------------------------------
 * 001E9280(a0): packet build from the record R = D_00275C18 + (halfword
 * a0+0x0E) * 0xA060 (computed once). For each segment s = 0..5 a block B =
 * 001CB5F0(0x7635C0, 0, 0x1A) gets the words (0, 0, 0x01000404, 0x6C188000)
 * and, for row n = 0..7, the quadwords R + s*0x200 + n*0x10 + 0x60,
 * R + (s+1)*0x200 + ... and R + (s+2)*0x200 + ... copied (00102948, in that
 * order per row) to B + 0x10 + n*0x10, B + 0x90 + n*0x10, B + 0x110 +
 * n*0x10; then B+0x190 = 0x14000000 for s = 5 else 0x17000000, and
 * B+0x194/198/19C = 0. Then B = 001CB5F0(.., 5): header words (0, 0,
 * 0x01000404, 0x6C040000), four quadwords from 0x70003AC0 (00102958).
 * Then B = 001CB5F0(.., 9): header (0, 0, 0x01000404, 0x6C0803F8), B+0x10/
 * 14/18 = R+0x48/4C/50 (each read after the previous store), B+0x1C = 0,
 * B+0x20 = the quadword R+0x10, B+0x30/34/38/3C = R+0x3C/38/40/44, B+0x40 =
 * the quadword D_008105D0, B+0x50 = 0x303E400000008010, B+0x58 = 0x412,
 * B+0x60/70/80 = the quadwords D_00275670 + 0xA0 / 0x2220 / 0x2230 (the
 * pointer read before each copy). Last 001CB950(0x7635C0, 0,
 * 0x20048BA199422040), 001CB6B0(0x7635C0, 0, 8, D_00275674 + 0x720) and
 * 001CB760(0x7635C0, 0, 0x234FE0).
 * ---------------------------------------------------------------------- */
#define CTX_7635C0 0x007635C0u

static void st64(Run *r, uint32_t a, uint64_t v)
{
    uint8_t *p = span(r, a, 8, 1);
    unsigned i;
    TRACE_STORE(a, 8);
    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t block(Run *r, uint32_t sp, uint32_t qwords, uint32_t tag)
{
    uint32_t b = (uint32_t)call3(r, sp, FN_1CB5F0, CTX_7635C0, 0, qwords);
    st32(r, b + 0x0, 0);
    st32(r, b + 0x4, 0);
    st32(r, b + 0x8, 0x01000404u);
    st32(r, b + 0xC, tag);
    return b;
}

static void f_1E9280(Run *r, uint32_t sp, uint32_t a0)
{
    uint32_t R, b, s, n;
    sp -= 0xA0;
    R = w32(r, G_275C18) + (uint32_t)((uint64_t)u16(r, a0 + 0x0E) * 0xA060u);
    for (s = 0; s < 6; s++) {
        b = block(r, sp, 0x1A, 0x6C188000u);
        for (n = 0; n < 8; n++) {
            call2(r, sp, FN_COPY_QW, b + 0x10 + n * 0x10, R + s * 0x200 + n * 0x10 + 0x60);
            call2(r, sp, FN_COPY_QW, b + 0x90 + n * 0x10, R + (s + 1) * 0x200 + n * 0x10 + 0x60);
            call2(r, sp, FN_COPY_QW, b + 0x110 + n * 0x10, R + (s + 2) * 0x200 + n * 0x10 + 0x60);
        }
        st32(r, b + 0x190, s == 5 ? 0x14000000u : 0x17000000u);
        st32(r, b + 0x194, 0);
        st32(r, b + 0x198, 0);
        st32(r, b + 0x19C, 0);
    }
    b = block(r, sp, 5, 0x6C040000u);
    call2(r, sp, FN_COPY_QW4, b + 0x10, SPR(0x3AC0));
    b = block(r, sp, 9, 0x6C0803F8u);
    st32(r, b + 0x10, w32(r, R + 0x48));
    st32(r, b + 0x14, w32(r, R + 0x4C));
    st32(r, b + 0x18, w32(r, R + 0x50));
    st32(r, b + 0x1C, 0);
    call2(r, sp, FN_COPY_QW, b + 0x20, R + 0x10);
    st32(r, b + 0x30, w32(r, R + 0x3C));
    st32(r, b + 0x34, w32(r, R + 0x38));
    st32(r, b + 0x38, w32(r, R + 0x40));
    st32(r, b + 0x3C, w32(r, R + 0x44));
    call2(r, sp, FN_COPY_QW, b + 0x40, 0x008105D0u);
    st64(r, b + 0x50, UINT64_C(0x303E400000008010));
    st64(r, b + 0x58, UINT64_C(0x412));
    call2(r, sp, FN_COPY_QW, b + 0x60, w32(r, G_275670) + 0xA0);
    call2(r, sp, FN_COPY_QW, b + 0x70, w32(r, G_275670) + 0x2220);
    call2(r, sp, FN_COPY_QW, b + 0x80, w32(r, G_275670) + 0x2230);
    (void)callx(r, sp, FN_1CB950, 3, reg(CTX_7635C0), 0, UINT64_C(0x20048BA199422040), 0, 0, 0, 0);
    call4(r, sp, FN_1CB6B0, CTX_7635C0, 0, 8, w32(r, G_275674) + 0x720);
    call3(r, sp, FN_1CB760, CTX_7635C0, 0, 0x00234FE0u);
}

/* ------------------------------------------------------------------------
 * 0015AB00(p): by state byte +4.
 *   0: row = 0x248290 + (signed halfword +0x54) * 20; +0x30 = row, +0x34 =
 *      0x15AAF0, bytes +0x0C, +9 = 0, +0 = 1, +4 = 1; 0x700038A0 = 2 *
 *      row[0], 0x700038A8 = 2 * row[2]; 001E8E80(p, halfword +0x0E,
 *      0x700038A0).
 *   1: 001E9280(p), 001B17A0(p).
 *   2, 3: 001AFC10(p).  4..255: nothing.
 * ---------------------------------------------------------------------- */
static void f_15AB00(Run *r, uint32_t sp, uint32_t p)
{
    uint32_t st, row;
    sp -= 0x20;
    st = u8(r, p + 4);
    if (st == 3 || st == 2) {
        call1(r, sp, FN_1AFC10, p);
        return;
    }
    if (st == 1) {
        f_1E9280(r, sp, p);
        call1(r, sp, FN_1B17A0, p);
        return;
    }
    if (st != 0)
        return;
    row = 0x00248290u + (uint32_t)s16(r, p + 0x54) * 20u;
    st32(r, p + 0x30, row);
    st32(r, p + 0x34, 0x0015AAF0u);
    st8(r, p + 0x0C, 0);
    st8(r, p + 9, 0);
    st8(r, p, 1);
    st8(r, p + 4, 1);
    st32(r, SPR(0x38A0), em_ee_mul_bits(F_TWO, w32(r, row)));
    st32(r, SPR(0x38A8), em_ee_mul_bits(F_TWO, w32(r, row + 8)));
    f_1E8E80(r, sp, p, u16(r, p + 0x0E), SPR(0x38A0));
}

/* ------------------------------------------------------------------------
 * 0015B030(p): q = the word +0x20 (read once, on entry); by state byte +4.
 *   0: when 0015AC00(p) is 0 and byte q+4 is 2 or more: state 3.
 *   1: when byte q+4 is below 2: p+0xB0 = q+0xB0 + p+0xA0 (001028B8),
 *      +0xBC = 1.0, 001C6380(p), 0015AE20(p, p+0x1F0). Otherwise state 3.
 *   2, 3 and 4..255: 001B1190(byte +0x9A), then 001AFC10(p).
 * ---------------------------------------------------------------------- */
static void f_15B030(Run *r, uint32_t sp, uint32_t p)
{
    uint32_t st, q;
    sp -= 0x40;
    st = u8(r, p + 4);
    q = w32(r, p + 0x20);
    if (st == 0) {
        if (call1(r, sp, FN_15AC00, p) != 0)
            return;
        if (u8(r, q + 4) >= 2u)
            st8(r, p + 4, 3);
        return;
    }
    if (st == 1) {
        if (u8(r, q + 4) < 2u) {
            call3(r, sp, FN_VADD, p + 0xB0, q + 0xB0, p + 0xA0);
            st32(r, p + 0xBC, F_ONE);
            call1(r, sp, FN_1C6380, p);
            call2(r, sp, FN_15AE20, p, p + 0x1F0);
        } else {
            st8(r, p + 4, 3);
        }
        return;
    }
    call1(r, sp, FN_1B1190, u8(r, p + 0x9A));
    call1(r, sp, FN_1AFC10, p);
}

/* ------------------------------------------------------------------------
 * 001BB520(a0): 001B0FD0(a0); +0x30 = 0x2755E8; halfword +0x34 = byte
 * +0x2E; halfword +0x2E = 0. v0 stays 001B0FD0's (all 64 bits).
 * ---------------------------------------------------------------------- */
static uint64_t f_1BB520(Run *r, uint32_t sp, uint32_t a0)
{
    uint64_t v;
    sp -= 0x20;
    v = call1(r, sp, FN_1B0FD0, a0);
    st32(r, a0 + 0x30, 0x002755E8u);
    st16(r, a0 + 0x34, u8(r, a0 + 0x2E));
    st16(r, a0 + 0x2E, 0);
    return v;
}

/* ------------------------------------------------------------------------
 * 001C2430(a0, a1, a2): 0x700038C0 = matrix a2 * vector a1 (001026A0),
 * plus a0+0xB0 (001028B8); 0x700038D0 = (0, -10, 0, 0); 0019AB20(a0,
 * 0x700038C0, 0x700038D0, 0x80000007). v0 stays 0019AB20's (all 64 bits).
 * ---------------------------------------------------------------------- */
static uint64_t f_1C2430(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t a2)
{
    sp -= 0x20;
    call3(r, sp, FN_MAT_VEC, SPR(0x38C0), a2, a1);
    call3(r, sp, FN_VADD, SPR(0x38C0), SPR(0x38C0), a0 + 0xB0);
    st32(r, SPR(0x38D0), 0);
    st32(r, SPR(0x38D4), 0xC1200000u);
    st32(r, SPR(0x38D8), 0);
    st32(r, SPR(0x38DC), 0);
    return call4(r, sp, FN_19AB20, a0, SPR(0x38C0), SPR(0x38D0), 0x80000007u);
}

/* ------------------------------------------------------------------------
 * 001C2540(a0, a1, a2, a3): 0x700038C0 = matrix a3 * a1 (001026A0) plus
 * a0+0xB0 (001028B8); 0x700038D0 = matrix a3 * a2; result 0019B4C0(a0,
 * 0x700038C0, 0x700038D0, 0x80000006).
 * ---------------------------------------------------------------------- */
static uint64_t f_1C2540(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    sp -= 0x40;
    call3(r, sp, FN_MAT_VEC, SPR(0x38C0), a3, a1);
    call3(r, sp, FN_VADD, SPR(0x38C0), SPR(0x38C0), a0 + 0xB0);
    call3(r, sp, FN_MAT_VEC, SPR(0x38D0), a3, a2);
    return call4(r, sp, FN_19B4C0, a0, SPR(0x38C0), SPR(0x38D0), 0x80000006u);
}

/* ------------------------------------------------------------------------
 * 001C3DB0(a0, a1, a2, a3): n = a0 x a1 into 0x70003630 (00102718). When
 * its x, y and z all compare equal to 0, a3[0..2] = a2[0..2] (each read
 * before its store) and done. Otherwise a basis: 0x700038A0 = norm(n);
 * 0x70003600 = 0x700038A0 x a0; 0x70003630 = norm(0x70003600); 0x70003400
 * = identity with rows 0, 1, 2 (x, y, z) = 0x700038A0, a0, 0x70003630;
 * 0x70003440 = its transpose (00102798). The angle: dot = a0.x*a1.x +
 * a0.y*a1.y + a0.z*a1.z (ADDA of the first two products, then MADD of the
 * third; MADD adds the unsaturated product, so it differs from MUL then ADD
 * only when a z operand has exponent 255),
 * 0x700038B0 = a0 x a1, ang = 0011E620(0011E748(|0x700038B0|^2 as the same
 * multiply-add), dot). 0x70003480 = identity with (1, 0, 0) in column 0 and
 * rows 1, 2 = (0, cos, sin), (0, -sin, cos) of ang (cos = 0011DE90 into
 * +0x14 / +0x28, sin = 0011E2A8 into +0x18, its negation into +0x24);
 * 0x700034C0 = 0x70003400 * 0x70003480, 0x70003480 = 0x700034C0 *
 * 0x70003440 (001026D0); a3 = 0x70003480 * a2 (001026A0), then normalised in
 * place (00102760).
 * ---------------------------------------------------------------------- */
static void f_1C3DB0(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t x0, x1, x2, y0, y1, y2, dot, len, ang, v;
    sp -= 0x60;
    call3(r, sp, FN_CROSS, SPR(0x3630), a0, a1);
    if (em_ee_c_eq_bits(F_ZERO, w32(r, SPR(0x3630))) && em_ee_c_eq_bits(F_ZERO, w32(r, SPR(0x3634))) &&
        em_ee_c_eq_bits(F_ZERO, w32(r, SPR(0x3638)))) {
        st32(r, a3 + 0, w32(r, a2 + 0));
        st32(r, a3 + 4, w32(r, a2 + 4));
        st32(r, a3 + 8, w32(r, a2 + 8));
        return;
    }
    call2(r, sp, FN_NORM, SPR(0x38A0), SPR(0x3630));
    call3(r, sp, FN_CROSS, SPR(0x3600), SPR(0x38A0), a0);
    call2(r, sp, FN_NORM, SPR(0x3630), SPR(0x3600));
    call1(r, sp, FN_IDENT, SPR(0x3400));
    x0 = w32(r, SPR(0x38A0));
    x1 = w32(r, SPR(0x38A4));
    x2 = w32(r, SPR(0x38A8));
    y0 = w32(r, SPR(0x3630));
    y1 = w32(r, SPR(0x3634));
    y2 = w32(r, SPR(0x3638));
    st32(r, SPR(0x3400), x0);
    st32(r, SPR(0x3404), x1);
    st32(r, SPR(0x3408), x2);
    st32(r, SPR(0x3410), w32(r, a0 + 0));
    st32(r, SPR(0x3414), w32(r, a0 + 4));
    st32(r, SPR(0x3418), w32(r, a0 + 8));
    st32(r, SPR(0x3420), y0);
    st32(r, SPR(0x3424), y1);
    st32(r, SPR(0x3428), y2);
    call2(r, sp, FN_TRANSPOSE, SPR(0x3440), SPR(0x3400));
    {
        uint32_t py = em_ee_mul_bits(w32(r, a0 + 4), w32(r, a1 + 4));
        uint32_t px = em_ee_mul_bits(w32(r, a0 + 0), w32(r, a1 + 0));
        uint32_t az = w32(r, a0 + 8), bz = w32(r, a1 + 8);
        dot = em_ee_madd_bits(em_ee_adda_bits(px, py), az, bz);
    }
    call3(r, sp, FN_CROSS, SPR(0x38B0), a0, a1);
    {
        uint32_t sx = w32(r, SPR(0x38B0)), sy = w32(r, SPR(0x38B4)), sz;
        uint32_t qx = em_ee_mul_bits(sx, sx), qy = em_ee_mul_bits(sy, sy);
        sz = w32(r, SPR(0x38B8));
        len = em_ee_madd_bits(em_ee_adda_bits(qx, qy), sz, sz);
    }
    v = fcall1(r, sp, FN_11E748, len);
    ang = fcall2(r, sp, FN_11E620, v, dot);
    call1(r, sp, FN_IDENT, SPR(0x3480));
    st32(r, SPR(0x3480), F_ONE);
    st32(r, SPR(0x3490), 0);
    st32(r, SPR(0x34A0), 0);
    st32(r, SPR(0x3484), 0);
    st32(r, SPR(0x3494), fcall1(r, sp, FN_11DE90, ang));
    v = fcall1(r, sp, FN_11E2A8, ang);
    st32(r, SPR(0x34A4), em_ee_neg_bits(v));
    st32(r, SPR(0x3488), 0);
    st32(r, SPR(0x3498), fcall1(r, sp, FN_11E2A8, ang));
    st32(r, SPR(0x34A8), fcall1(r, sp, FN_11DE90, ang));
    call3(r, sp, FN_MAT_MUL, SPR(0x34C0), SPR(0x3400), SPR(0x3480));
    call3(r, sp, FN_MAT_MUL, SPR(0x3480), SPR(0x34C0), SPR(0x3440));
    call3(r, sp, FN_MAT_VEC, a3, SPR(0x3480), a2);
    call2(r, sp, FN_NORM, a3, a3);
}

/* ------------------------------------------------------------------------
 * 001C6160(a0): D_00275BF8 = 001C6120(word a0+0x40, signed halfword
 * a0+0x2C); the result is the halfword at D_00275BF8 + 2 (the word read
 * back after the store).
 * ---------------------------------------------------------------------- */
static uint32_t f_1C6160(Run *r, uint32_t sp, uint32_t a0)
{
    int32_t h;
    sp -= 0x10;
    h = s16(r, a0 + 0x2C);
    st32(r, G_275BF8, (uint32_t)call2(r, sp, FN_1C6120, w32(r, a0 + 0x40), (uint32_t)h));
    return u16(r, w32(r, G_275BF8) + 2);
}

/* ------------------------------------------------------------------------
 * 001D0400(a0, a1; f12 = k): 0x90 bytes a1 -> a0 (00121870); the vectors
 * a0+0x00 and a0+0x10 scaled by k with 00103230, a0+0x40 and a0+0x50 with
 * 00102900 (each in place); the floats +0x78, +0x7C, +0x84 multiplied by k;
 * the word +0x80 = 001281C0(float(word +0x80) * k).
 * ---------------------------------------------------------------------- */
static void f_1D0400(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t k)
{
    uint32_t v;
    sp -= 0x30;
    call3(r, sp, FN_BLOCK_COPY, a0, a1, 0x90);
    call2f(r, sp, FN_VSCALE, a0, a0, k);
    call2f(r, sp, FN_VSCALE, a0 + 0x10, a0 + 0x10, k);
    call2f(r, sp, FN_VSCALE_B, a0 + 0x40, a0 + 0x40, k);
    call2f(r, sp, FN_VSCALE_B, a0 + 0x50, a0 + 0x50, k);
    st32(r, a0 + 0x78, em_ee_mul_bits(w32(r, a0 + 0x78), k));
    st32(r, a0 + 0x7C, em_ee_mul_bits(w32(r, a0 + 0x7C), k));
    st32(r, a0 + 0x84, em_ee_mul_bits(w32(r, a0 + 0x84), k));
    v = em_ee_mul_bits(em_ee_cvt_s_w_bits(w32(r, a0 + 0x80)), k);
    st32(r, a0 + 0x80, (uint32_t)callx(r, sp, FN_FLOAT_TO_INT, 0, 0, 0, 0, 0, 1, v, 0).v0);
}

/* ------------------------------------------------------------------------
 * Entry points
 * ---------------------------------------------------------------------- */

#define ENTER(s, fn)                                                                                     \
    Run run;                                                                                             \
    if (!(s))                                                                                            \
        return -1;                                                                                       \
    if ((s)->fault != EM_AREA01_EXITB_FAULT_NONE)                                                        \
        return -1;                                                                                       \
    run.s = (s);                                                                                         \
    if (setjmp(run.out)) {                                                                               \
        (s)->fault_function = (fn);                                                                      \
        return -1;                                                                                       \
    }                                                                                                    \
    if (!(s)->regions && !(s)->view) {                                                                                 \
        (s)->fault_function = (fn);                                                                      \
        fault(&run, EM_AREA01_EXITB_FAULT_NULL, 0);                                                      \
    }

int em_area01_exitb_00156F30(EmArea01Exitb *s, uint32_t p)
{
    ENTER(s, 0x00156F30u);
    f_156F30(&run, s->sp, p);
    return 0;
}

int em_area01_exitb_001576E0(EmArea01Exitb *s, uint32_t a0, uint32_t a1, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x001576E0u);
    v = f_1576E0(&run, s->sp, a0, a1);
    if (out)
        *out = v;
    return 0;
}

int em_area01_exitb_001581A0(EmArea01Exitb *s, uint32_t p)
{
    ENTER(s, 0x001581A0u);
    f_1581A0(&run, s->sp, p);
    return 0;
}

int em_area01_exitb_00158810(EmArea01Exitb *s, uint32_t p)
{
    ENTER(s, 0x00158810u);
    f_158810(&run, s->sp, p);
    return 0;
}

int em_area01_exitb_00158BD0(EmArea01Exitb *s, uint32_t p)
{
    ENTER(s, 0x00158BD0u);
    f_158BD0(&run, s->sp, p);
    return 0;
}

int em_area01_exitb_0015AB00(EmArea01Exitb *s, uint32_t p)
{
    ENTER(s, 0x0015AB00u);
    f_15AB00(&run, s->sp, p);
    return 0;
}

int em_area01_exitb_0015B030(EmArea01Exitb *s, uint32_t p)
{
    ENTER(s, 0x0015B030u);
    f_15B030(&run, s->sp, p);
    return 0;
}

int em_area01_exitb_001BB520(EmArea01Exitb *s, uint32_t a0, uint64_t *v0)
{
    uint64_t v;
    ENTER(s, 0x001BB520u);
    v = f_1BB520(&run, s->sp, a0);
    if (v0)
        *v0 = v;
    return 0;
}

int em_area01_exitb_001C2430(EmArea01Exitb *s, uint32_t a0, uint32_t a1, uint32_t a2, uint64_t *v0)
{
    uint64_t v;
    ENTER(s, 0x001C2430u);
    v = f_1C2430(&run, s->sp, a0, a1, a2);
    if (v0)
        *v0 = v;
    return 0;
}

int em_area01_exitb_001C2540(EmArea01Exitb *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                             uint64_t *out)
{
    uint64_t v;
    ENTER(s, 0x001C2540u);
    v = f_1C2540(&run, s->sp, a0, a1, a2, a3);
    if (out)
        *out = v;
    return 0;
}

int em_area01_exitb_001C3DB0(EmArea01Exitb *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    ENTER(s, 0x001C3DB0u);
    f_1C3DB0(&run, s->sp, a0, a1, a2, a3);
    return 0;
}

int em_area01_exitb_001C6160(EmArea01Exitb *s, uint32_t a0, uint32_t *out)
{
    uint32_t v;
    ENTER(s, 0x001C6160u);
    v = f_1C6160(&run, s->sp, a0);
    if (out)
        *out = v;
    return 0;
}

int em_area01_exitb_001D0400(EmArea01Exitb *s, uint32_t a0, uint32_t a1, uint32_t f12)
{
    ENTER(s, 0x001D0400u);
    f_1D0400(&run, s->sp, a0, a1, f12);
    return 0;
}

int em_area01_exitb_001E8E80(EmArea01Exitb *s, uint32_t a0, uint32_t a1, uint32_t a2)
{
    ENTER(s, 0x001E8E80u);
    f_1E8E80(&run, s->sp, a0, a1, a2);
    return 0;
}

int em_area01_exitb_001E9280(EmArea01Exitb *s, uint32_t a0)
{
    ENTER(s, 0x001E9280u);
    f_1E9280(&run, s->sp, a0);
    return 0;
}

int em_area01_exitb_001E9580(EmArea01Exitb *s, uint32_t a0, uint32_t a1, uint32_t a2)
{
    ENTER(s, 0x001E9580u);
    f_1E9580(&run, s->sp, a0, a1, a2);
    return 0;
}
