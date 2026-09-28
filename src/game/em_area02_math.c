/* em_area02_math.c - level-4 lane L4MATH translations (see em_area02_math.h
 * and docs/AREA02_MATH.md). Every routine names its original address; the
 * comments say what the original does, never how it is encoded. */
#include "em_area02_math.h"

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#include "em_ee_float.h"

/* ------------------------------------------------------------------------
 * Fail-stop plumbing
 * ---------------------------------------------------------------------- */

typedef struct {
    EmArea02Math *s;
    uint32_t entry; /* the entry this run serves (fault_function) */
    jmp_buf out;
} Run;

/* Latch the first fault only: the code, entry and address together. A
 * worker that re-enters the module with the same context and faults there
 * has latched first; the outer entry then keeps that record. */
static void fault(Run *r, int32_t code, uint32_t address)
{
    if (r->s->fault == EM_AREA02_MATH_FAULT_NONE) {
        r->s->fault = code;
        r->s->fault_function = r->entry;
        r->s->fault_address = address;
    }
    longjmp(r->out, 1);
}

void em_area02_math_clear_fault(EmArea02Math *s)
{
    if (!s)
        return;
    s->fault = EM_AREA02_MATH_FAULT_NONE;
    s->fault_function = 0;
    s->fault_address = 0;
}

/* The byte span [address, address + size) inside one region, or a fault. */
static uint8_t *span(Run *r, uint32_t address, uint32_t size)
{
    const EmArea02Math *s = r->s;
    unsigned i;
    for (i = 0; i < s->region_count; i++) {
        const EmArea02MathRegion *g = &s->regions[i];
        if (g->bytes && address >= g->base && size <= g->size && address - g->base <= g->size - size)
            return g->bytes + (address - g->base);
    }
    fault(r, EM_AREA02_MATH_FAULT_UNMAPPED, address);
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

/* Test hook. A build that defines EM_AREA02_MATH_STORE_TRACE as a function
 * name reports every store (original address, size) to it once the address
 * is known to be mapped; tools/test_area02_math_reference.py uses it to
 * compare memory with the original at every call leaving the module.
 * Ordinary builds compile it out. wr is the only store path. */
#ifdef EM_AREA02_MATH_STORE_TRACE
void EM_AREA02_MATH_STORE_TRACE(uint32_t address, unsigned size);
#define TRACE_STORE(a, n) EM_AREA02_MATH_STORE_TRACE((a), (n))
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
static uint32_t u16(Run *r, uint32_t a) { return rd(r, a, 2); }
static int32_t s16(Run *r, uint32_t a) { return (int32_t)(int16_t)rd(r, a, 2); }
static uint32_t w32(Run *r, uint32_t a) { return rd(r, a, 4); }
static void st8(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 1); }
static void st16(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 2); }
static void st32(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 4); }

/* One quadword: the EE quadword load and store ignore address bits 0..3.
 * All 16 bytes are read before any is written. */
static void copy_qw(Run *r, uint32_t dst, uint32_t src)
{
    uint32_t w[4];
    unsigned i;
    src &= ~UINT32_C(0xF);
    dst &= ~UINT32_C(0xF);
    for (i = 0; i < 4; i++)
        w[i] = w32(r, src + 4 * i);
    (void)span(r, dst, 16);
    TRACE_STORE(dst, 16);
    for (i = 0; i < 4; i++) {
        uint8_t *p = span(r, dst + 4 * i, 4);
        unsigned k;
        for (k = 0; k < 4; k++)
            p[k] = (uint8_t)(w[i] >> (8 * k));
    }
}

/* 32-bit value as the EE holds it in a 64-bit register. */
static uint64_t reg(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

/* 1 << (n & 31), the EE's variable shift of a 32-bit word. */
static uint32_t bit(uint32_t n) { return UINT32_C(1) << (n & 31u); }

/* ------------------------------------------------------------------------
 * Calls leaving the module
 * ---------------------------------------------------------------------- */

/* One call: `na` integer registers (a0.. images in a[]) and `nf` float
 * registers (f12.. bits in f[]). Returns the full v0; *f0 (optional)
 * receives f0. */
static uint64_t callv(Run *r, uint32_t sp, uint32_t fn, unsigned na, const uint64_t *a, unsigned nf,
                      const uint32_t *f, uint32_t *f0)
{
    EmArea02MathCall c;
    unsigned i;
    memset(&c, 0, sizeof c);
    c.fn = fn;
    c.sp = sp;
    c.na = na;
    c.nf = nf;
    for (i = 0; i < na; i++)
        c.a[i] = a[i];
    for (i = 0; i < nf; i++)
        c.f[i] = f[i];
    if (!r->s->call)
        fault(r, EM_AREA02_MATH_FAULT_NULL, fn);
    if (r->s->call(r->s->ctx, &c) < 0)
        fault(r, EM_AREA02_MATH_FAULT_WORKER, fn);
    if (f0)
        *f0 = c.f0;
    return c.v0;
}

static uint64_t call0(Run *r, uint32_t sp, uint32_t fn)
{
    return callv(r, sp, fn, 0, NULL, 0, NULL, NULL);
}
static uint64_t call1(Run *r, uint32_t sp, uint32_t fn, uint32_t a)
{
    uint64_t x[1];
    x[0] = reg(a);
    return callv(r, sp, fn, 1, x, 0, NULL, NULL);
}
static uint64_t call2(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b)
{
    uint64_t x[2];
    x[0] = reg(a);
    x[1] = reg(b);
    return callv(r, sp, fn, 2, x, 0, NULL, NULL);
}
static uint64_t call3(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b, uint32_t c)
{
    uint64_t x[3];
    x[0] = reg(a);
    x[1] = reg(b);
    x[2] = reg(c);
    return callv(r, sp, fn, 3, x, 0, NULL, NULL);
}
static uint64_t call4(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint64_t x[4];
    x[0] = reg(a);
    x[1] = reg(b);
    x[2] = reg(c);
    x[3] = reg(d);
    return callv(r, sp, fn, 4, x, 0, NULL, NULL);
}

/* The node's own method (word +0x4C), called with a0 = the node. */
static void method(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t m = w32(r, node + 0x4C);
    (void)call1(r, sp, m, node);
}

/* ------------------------------------------------------------------------
 * Constants, callees and globals
 * ---------------------------------------------------------------------- */

#define F_ONE   0x3F800000u
#define F_HALF  0x3F000000u
#define F_HALFPI 0x3FC90FDBu /* pi/2 */

#define FN_1026A0 0x001026A0u /* (out, matrix, vector) */
#define FN_102948 0x00102948u /* (dst, src) one quadword */
#define FN_157860 0x00157860u
#define FN_157CE0 0x00157CE0u
#define FN_158590 0x00158590u
#define FN_1AEBA0 0x001AEBA0u
#define FN_1AF780 0x001AF780u
#define FN_1AFA90 0x001AFA90u
#define FN_1AFC10 0x001AFC10u
#define FN_1B0FD0 0x001B0FD0u
#define FN_1B1020 0x001B1020u
#define FN_1B1240 0x001B1240u
#define FN_1B1630 0x001B1630u
#define FN_1B17A0 0x001B17A0u
#define FN_1B1B70 0x001B1B70u
#define FN_1B1D20 0x001B1D20u
#define FN_1B6F00 0x001B6F00u
#define FN_1BA1A0 0x001BA1A0u
#define FN_1BA1C0 0x001BA1C0u
#define FN_1BA1F0 0x001BA1F0u
#define FN_1BBD20 0x001BBD20u
#define FN_1BC150 0x001BC150u
#define FN_1BC740 0x001BC740u
#define FN_1BD180 0x001BD180u
#define FN_1BD270 0x001BD270u
#define FN_1BD370 0x001BD370u
#define FN_1BD460 0x001BD460u
#define FN_1C5570 0x001C5570u
#define FN_1C6380 0x001C6380u
#define FN_1C63E0 0x001C63E0u
#define FN_1C64F0 0x001C64F0u
#define FN_1C68C0 0x001C68C0u
#define FN_1C7420 0x001C7420u
#define FN_1C7EB0 0x001C7EB0u
#define FN_1CA5E0 0x001CA5E0u
#define FN_1CA7B0 0x001CA7B0u
#define FN_1CAAC0 0x001CAAC0u
#define FN_1CB5B0 0x001CB5B0u
#define FN_1D1F80 0x001D1F80u
#define FN_1D2830 0x001D2830u
#define FN_1D2910 0x001D2910u
#define FN_1D38F0 0x001D38F0u
#define FN_1D3AC0 0x001D3AC0u
#define FN_1D3CE0 0x001D3CE0u
#define FN_1D8C20 0x001D8C20u
#define FN_1F1110 0x001F1110u
#define FN_1F1180 0x001F1180u
#define FN_1F4E40 0x001F4E40u
#define FN_1FB9F0 0x001FB9F0u

#define G_AREA   0x00810700u /* area byte */
#define G_ROOM   0x00810702u /* the room byte beside it */
#define G_MASK   0x00810841u /* per-area room-mask bytes */
#define G_POSE   0x00275B40u /* word: pointer to the pose record pointers */
#define SPR(x)   (0x70000000u + (x))

/* ------------------------------------------------------------------------
 * 001575E0(a0): scratch vector 0x700038A0 = (10, 0, -5, 1.0); 001B6F00 with
 * a0 = the node, a1 = 0x700038A0 and f12 = pi/2. Then, from D_00810350 /
 * 54 / 58 (all three read after that call): D_008105E0 = x, D_008105E4 =
 * 11.0 + y, D_008105E8 = z, D_008105EC = 1.0, with 0x700038A0 = (20, 14,
 * 15, 1.0) stored in between; 001026A0(D_008105D0, node + 0xD0,
 * 0x700038A0). Returns 1. The frame is 0x20 bytes.
 * ---------------------------------------------------------------------- */
static int32_t f_1575E0(Run *r, uint32_t sp, uint32_t a0)
{
    uint32_t x, y, z;
    uint64_t v[2];
    uint32_t f12 = F_HALFPI;
    sp -= 0x20;
    st32(r, SPR(0x38A0), 0x41200000u);
    st32(r, SPR(0x38A4), 0);
    st32(r, SPR(0x38A8), 0xC0A00000u);
    st32(r, SPR(0x38AC), F_ONE);
    v[0] = reg(a0);
    v[1] = reg(SPR(0x38A0));
    (void)callv(r, sp, FN_1B6F00, 2, v, 1, &f12, NULL);
    x = w32(r, 0x00810350u);
    y = w32(r, 0x00810354u);
    z = w32(r, 0x00810358u);
    y = em_ee_add_bits(0x41300000u, y);
    st32(r, 0x008105E0u, x);
    st32(r, SPR(0x38A0), 0x41A00000u);
    st32(r, SPR(0x38A4), 0x41600000u);
    st32(r, 0x008105E4u, y);
    st32(r, 0x008105E8u, z);
    st32(r, SPR(0x38A8), 0x41700000u);
    st32(r, 0x008105ECu, F_ONE);
    st32(r, SPR(0x38AC), F_ONE);
    (void)call3(r, sp, FN_1026A0, 0x008105D0u, a0 + 0xD0, SPR(0x38A0));
    return 1;
}

/* ------------------------------------------------------------------------
 * 00157B30(node, a1): 0 unless bit 2 of byte +0x0B. Then D_00810374 =
 * 001B1240(D_00810350; f12 = +0xB0, f13 = +0xB8), and:
 *   D_00810C84 == 0: word D_00247E74 = 0x8000000E, 001BA1A0(a1,
 *       D_00247E20), 001BA1F0(node), byte +0 = 2, result 2;
 *   D_0081085C == 0.0 and not D_00810858 < 100.0: with bit 0 of +0x0B,
 *       001FB9F0(0x3EC, 0x1000, 0x1000, 0x1000) and 001BA1A0(a1,
 *       D_00247FA0); without, D_00247E74 = 0x80000020 and 001BA1A0(a1,
 *       D_00247E20); then 001BA1F0(node), byte +0 = 2, result 2;
 *   otherwise with bit 0 of +0x0B: byte +0x0A = 1, byte +0 = 2, result 3;
 *   otherwise D_00247F34 = 0x8000000E, 001BA1A0(a1, D_00247EE0),
 *       001BA1F0(node), result 1.
 * The frame is 0x30 bytes.
 * ---------------------------------------------------------------------- */
static int32_t f_157B30(Run *r, uint32_t sp, uint32_t node, uint32_t a1)
{
    uint64_t v[1];
    uint32_t f[2], yaw;
    sp -= 0x30;
    if (!(u8(r, node + 0x0B) & 4))
        return 0;
    f[0] = w32(r, node + 0xB0);
    f[1] = w32(r, node + 0xB8);
    v[0] = reg(0x00810350u);
    (void)callv(r, sp, FN_1B1240, 1, v, 2, f, &yaw);
    st32(r, 0x00810374u, yaw);
    if (u8(r, 0x00810C84u) == 0) {
        st32(r, 0x00247E74u, 0x8000000Eu);
        (void)call2(r, sp, FN_1BA1A0, a1, 0x00247E20u);
        (void)call1(r, sp, FN_1BA1F0, node);
        st8(r, node + 0, 2);
        return 2;
    }
    if (em_ee_c_eq_bits(0, w32(r, 0x0081085Cu)) && !em_ee_c_lt_bits(w32(r, 0x00810858u), 0x42C80000u)) {
        if (u8(r, node + 0x0B) & 1) {
            (void)call4(r, sp, FN_1FB9F0, 0x3EC, 0x1000, 0x1000, 0x1000);
            (void)call2(r, sp, FN_1BA1A0, a1, 0x00247FA0u);
            (void)call1(r, sp, FN_1BA1F0, node);
        } else {
            st32(r, 0x00247E74u, 0x80000020u);
            (void)call2(r, sp, FN_1BA1A0, a1, 0x00247E20u);
            (void)call1(r, sp, FN_1BA1F0, node);
        }
        st8(r, node + 0, 2);
        return 2;
    }
    if (u8(r, node + 0x0B) & 1) {
        st8(r, node + 0x0A, 1);
        st8(r, node + 0, 2);
        return 3;
    }
    st32(r, 0x00247F34u, 0x8000000Eu);
    (void)call2(r, sp, FN_1BA1A0, a1, 0x00247EE0u);
    (void)call1(r, sp, FN_1BA1F0, node);
    return 1;
}

/* ------------------------------------------------------------------------
 * 00157F30(node): D_008106B0 = 4 (byte), D_008106D0 = word +0x14, bytes
 * +0x0A = 0, +0x0B = 0, +0 = 1. Returns 1. No frame.
 * ---------------------------------------------------------------------- */
static int32_t f_157F30(Run *r, uint32_t node)
{
    uint32_t v;
    st8(r, 0x008106B0u, 4);
    v = w32(r, node + 0x14);
    st32(r, 0x008106D0u, v);
    st8(r, node + 0x0A, 0);
    st8(r, node + 0x0B, 0);
    st8(r, node + 0, 1);
    return 1;
}

/* ------------------------------------------------------------------------
 * 00158050(a0): p = word (word a0+0x1C)+0x1C; 0x700038A0 = (p+0xB0, p+0xB4,
 * p+0xB8, 1.0); 00102948(D_008105E0, 0x700038A0). Returns 1. The frame is
 * 0x10 bytes.
 * ---------------------------------------------------------------------- */
static int32_t f_158050(Run *r, uint32_t sp, uint32_t a0)
{
    uint32_t p;
    sp -= 0x10;
    p = w32(r, w32(r, a0 + 0x1C) + 0x1C);
    st32(r, SPR(0x38A0), w32(r, p + 0xB0));
    st32(r, SPR(0x38A4), w32(r, p + 0xB4));
    st32(r, SPR(0x38A8), w32(r, p + 0xB8));
    st32(r, SPR(0x38AC), F_ONE);
    (void)call2(r, sp, FN_102948, 0x008105E0u, SPR(0x38A0));
    return 1;
}

/* ------------------------------------------------------------------------
 * 001582E0(node), by the state byte +4:
 *   0: with m = 1 << (halfword +0x2E): when the room-mask byte
 *      D_00810841[D_00810700] has any bit of m & 0xFF, +4 = 3. Otherwise
 *      001B0FD0(node), 001C6380(node), byte +0 = 1, 001F1110(node, 2);
 *   1: when the halfword +0x36 is nonzero: bytes +0 = 2, +4 = 2, the
 *      room-mask byte |= 1 << (byte +0x2E), D_00810842 |= 2, and
 *      001FB9F0(0x3F1, 0x1000, 0x1000, 0x1000). Always then 001F1180,
 *      001B17A0 and the node's method;
 *   2, 3: 001AFC10(node); other: nothing.
 * The frame is 0x20 bytes.
 * ---------------------------------------------------------------------- */
static void f_1582E0(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t st = u8(r, node + 4);
    sp -= 0x20;
    switch (st) {
    case 0: {
        uint32_t area = u8(r, G_AREA);
        uint32_t m = bit(u16(r, node + 0x2E));
        uint32_t b = u8(r, G_MASK + area);
        if (b & m & 0xFF) {
            st8(r, node + 4, 3);
            return;
        }
        (void)call1(r, sp, FN_1B0FD0, node);
        (void)call1(r, sp, FN_1C6380, node);
        st8(r, node + 0, 1);
        (void)call2(r, sp, FN_1F1110, node, 2);
        return;
    }
    case 1:
        if (s16(r, node + 0x36) != 0) {
            uint32_t area, k, at;
            st8(r, node + 0, 2);
            st8(r, node + 4, 2);
            area = u8(r, G_AREA);
            k = u8(r, node + 0x2E);
            at = G_MASK + area;
            st8(r, at, u8(r, at) | bit(k));
            st8(r, 0x00810842u, u8(r, 0x00810842u) | 2);
            (void)call4(r, sp, FN_1FB9F0, 0x3F1, 0x1000, 0x1000, 0x1000);
        }
        (void)call1(r, sp, FN_1F1180, node);
        (void)call1(r, sp, FN_1B17A0, node);
        method(r, sp, node);
        return;
    case 2:
    case 3:
        (void)call1(r, sp, FN_1AFC10, node);
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------------
 * 00158EC0(node), by the state byte +4 (s = node + 0x1F0):
 *   0: nothing unless 001B0FD0(node) is 0. Then 001C6380(node), word +0x30
 *      = 0x275480, byte +0x0A = 0, word +0x20 = 0; when the room-mask byte
 *      has bit (halfword +0x2E): +0 = 2, +5 = 3. Otherwise halfword +0x34 =
 *      4 / 0x10 / 0x18 for byte +3 = 0x14 / 0x22 / other, +0 = 1,
 *      0x700038A0 = (1.0, 0, 0, 1.0), word +0x20 = 001C5570(node,
 *      0x700038A0, 0x75, 1);
 *   1: sub-state +5 (0..6, jump table; 7 and up do nothing), then 001B17A0
 *      and the node's method:
 *      0: r = 00157860(node, s, 1): 0 nothing, 2 -> +5 = 4, 3 -> +5 += 1,
 *         other -> +5 = 5;
 *      1: +0x0A == 0: +5 = 4, 001BA1A0(s, D_00247DA0); else D_00247934 =
 *         0x15C, +5 += 1, 001BA1A0(s, D_002478A0), 001BA1F0(node);
 *      2: when 001BA1F0(node): +0x0B = 0, +0 = 2, +5 += 1, and a nonzero
 *         word +0x20 gets its byte +4 = 3 and is cleared;
 *      3: nothing;
 *      4: when 001BA1F0(node): +0 = 1, +0x0B = 0, +5 = 0;
 *      5: when 001BA1F0(node): +5 = 6;
 *      6: +0x0A == 0: as 1; else D_00247934 = 0x15C, +5 = 2,
 *         001BA1A0(s, D_002478E0), 001BA1F0(node);
 *   2: +4 += 1; 3: 001AFC10(node); other: nothing.
 * The frame is 0x20 bytes.
 * ---------------------------------------------------------------------- */
static void f_158EC0(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t st = u8(r, node + 4);
    uint32_t s = node + 0x1F0;
    sp -= 0x20;
    switch (st) {
    case 0: {
        uint32_t area, h, b, t;
        if (call1(r, sp, FN_1B0FD0, node) != 0)
            return;
        (void)call1(r, sp, FN_1C6380, node);
        st32(r, node + 0x30, 0x00275480u);
        st8(r, node + 0x0A, 0);
        st32(r, node + 0x20, 0);
        area = u8(r, G_AREA);
        h = u16(r, node + 0x2E);
        b = u8(r, G_MASK + area);
        if (b & bit(h)) {
            st8(r, node + 0, 2);
            st8(r, node + 5, 3);
            return;
        }
        t = u8(r, node + 3);
        st16(r, node + 0x34, t == 0x14 ? 4 : t == 0x22 ? 0x10 : 0x18);
        st8(r, node + 0, 1);
        st32(r, SPR(0x38A0), F_ONE);
        st32(r, SPR(0x38A4), 0);
        st32(r, SPR(0x38A8), 0);
        st32(r, SPR(0x38AC), F_ONE);
        st32(r, node + 0x20, (uint32_t)call4(r, sp, FN_1C5570, node, SPR(0x38A0), 0x75, 1));
        return;
    }
    case 1:
        switch (u8(r, node + 5)) {
        case 0: {
            uint64_t v = call3(r, sp, FN_157860, node, s, 1);
            if (v == 0)
                break;
            if (v == 2)
                st8(r, node + 5, 4);
            else if (v == 3)
                st8(r, node + 5, u8(r, node + 5) + 1);
            else
                st8(r, node + 5, 5);
            break;
        }
        case 1:
            if (u8(r, node + 0x0A) == 0) {
                st8(r, node + 5, 4);
                (void)call2(r, sp, FN_1BA1A0, s, 0x00247DA0u);
            } else {
                st32(r, 0x00247934u, 0x15C);
                st8(r, node + 5, u8(r, node + 5) + 1);
                (void)call2(r, sp, FN_1BA1A0, s, 0x002478A0u);
                (void)call1(r, sp, FN_1BA1F0, node);
            }
            break;
        case 2:
            if (call1(r, sp, FN_1BA1F0, node) != 0) {
                uint32_t h;
                st8(r, node + 0x0B, 0);
                st8(r, node + 0, 2);
                st8(r, node + 5, u8(r, node + 5) + 1);
                h = w32(r, node + 0x20);
                if (h != 0) {
                    st8(r, h + 4, 3);
                    st32(r, node + 0x20, 0);
                }
            }
            break;
        case 3:
            break;
        case 4:
            if (call1(r, sp, FN_1BA1F0, node) != 0) {
                st8(r, node + 0, 1);
                st8(r, node + 0x0B, 0);
                st8(r, node + 5, 0);
            }
            break;
        case 5:
            if (call1(r, sp, FN_1BA1F0, node) != 0)
                st8(r, node + 5, 6);
            break;
        case 6:
            if (u8(r, node + 0x0A) == 0) {
                st8(r, node + 5, 4);
                (void)call2(r, sp, FN_1BA1A0, s, 0x00247DA0u);
            } else {
                st32(r, 0x00247934u, 0x15C);
                st8(r, node + 5, 2);
                (void)call2(r, sp, FN_1BA1A0, s, 0x002478E0u);
                (void)call1(r, sp, FN_1BA1F0, node);
            }
            break;
        default:
            break;
        }
        (void)call1(r, sp, FN_1B17A0, node);
        method(r, sp, node);
        return;
    case 2:
        st8(r, node + 4, st + 1);
        return;
    case 3:
        (void)call1(r, sp, FN_1AFC10, node);
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------------
 * 00159620(node), by the state byte +4 (s = node + 0x1F0):
 *   0: nothing unless 001B0FD0(node) is 0. Then 001C6380(node), +0x0A = 0,
 *      word +0x30 = 0x275458, +8 = 5, +0 = 1, and three 001C5570(node,
 *      0x700038A0, id, 1) with 0x700038A0 = (0, 1.0, 0, 0.2) id 0x6E,
 *      (0, 0.4, 0, 0.2) id 0x6F, (0, 1.0, 0, 0.2) id 0x70;
 *   1: sub-state +5 (0..6; 7 and up nothing), then 001B1B70(node) and the
 *      node's method:
 *      0: r = 00157B30(node, s): 0 nothing, 2 -> +5 = 4, 3 -> +5 += 1,
 *         other -> +5 = 5 and 001FB9F0(0x3EA, 0x1000, 0x1000, 0x1000);
 *      1: +0x0A == 0: +5 = 4, 001BA1A0(s, D_00247DA0); else +5 = the
 *         sub-state read + 1, 001BA1A0(s, D_00247FA0), 001BA1F0(node);
 *      2, 4: when 001BA1F0(node): +0x0B = 0, +0 = 1, +5 = 0 (2) / +0 = 1,
 *         +0x0B = 0, +5 = 0 (4);
 *      3: nothing; 5: when 001BA1F0(node): +5 = 6;
 *      6: +0x0A == 0: as 1; else +5 = 2, 001FB9F0(0x3EC, 0x1000, 0x1000,
 *         0x1000), 001BA1A0(s, D_00247FE0), 001BA1F0(node);
 *   2: +4 += 1; 3: 001AFC10(node); other: nothing.
 * The frame is 0x30 bytes.
 * ---------------------------------------------------------------------- */
static void f_159620(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t st = u8(r, node + 4);
    uint32_t s = node + 0x1F0;
    sp -= 0x30;
    switch (st) {
    case 0:
        if (call1(r, sp, FN_1B0FD0, node) != 0)
            return;
        (void)call1(r, sp, FN_1C6380, node);
        st8(r, node + 0x0A, 0);
        st32(r, node + 0x30, 0x00275458u);
        st8(r, node + 8, 5);
        st8(r, node + 0, 1);
        st32(r, SPR(0x38A0), 0);
        st32(r, SPR(0x38A4), F_ONE);
        st32(r, SPR(0x38A8), 0);
        st32(r, SPR(0x38AC), 0x3E4CCCCDu);
        (void)call4(r, sp, FN_1C5570, node, SPR(0x38A0), 0x6E, 1);
        st32(r, SPR(0x38A0), 0);
        st32(r, SPR(0x38A4), 0x3ECCCCCDu);
        st32(r, SPR(0x38A8), 0);
        st32(r, SPR(0x38AC), 0x3E4CCCCDu);
        (void)call4(r, sp, FN_1C5570, node, SPR(0x38A0), 0x6F, 1);
        st32(r, SPR(0x38A0), 0);
        st32(r, SPR(0x38A4), F_ONE);
        st32(r, SPR(0x38A8), 0);
        st32(r, SPR(0x38AC), 0x3E4CCCCDu);
        (void)call4(r, sp, FN_1C5570, node, SPR(0x38A0), 0x70, 1);
        return;
    case 1: {
        uint32_t sub = u8(r, node + 5);
        switch (sub) {
        case 0: {
            int32_t v = f_157B30(r, sp, node, s);
            if (v == 0)
                break;
            if (v == 2) {
                st8(r, node + 5, 4);
            } else if (v == 3) {
                st8(r, node + 5, u8(r, node + 5) + 1);
            } else {
                st8(r, node + 5, 5);
                (void)call4(r, sp, FN_1FB9F0, 0x3EA, 0x1000, 0x1000, 0x1000);
            }
            break;
        }
        case 1:
            if (u8(r, node + 0x0A) == 0) {
                st8(r, node + 5, 4);
                (void)call2(r, sp, FN_1BA1A0, s, 0x00247DA0u);
            } else {
                st8(r, node + 5, sub + 1);
                (void)call2(r, sp, FN_1BA1A0, s, 0x00247FA0u);
                (void)call1(r, sp, FN_1BA1F0, node);
            }
            break;
        case 2:
            if (call1(r, sp, FN_1BA1F0, node) != 0) {
                st8(r, node + 0x0B, 0);
                st8(r, node + 0, 1);
                st8(r, node + 5, 0);
            }
            break;
        case 3:
            break;
        case 4:
            if (call1(r, sp, FN_1BA1F0, node) != 0) {
                st8(r, node + 0, 1);
                st8(r, node + 0x0B, 0);
                st8(r, node + 5, 0);
            }
            break;
        case 5:
            if (call1(r, sp, FN_1BA1F0, node) != 0)
                st8(r, node + 5, 6);
            break;
        case 6:
            if (u8(r, node + 0x0A) == 0) {
                st8(r, node + 5, 4);
                (void)call2(r, sp, FN_1BA1A0, s, 0x00247DA0u);
            } else {
                st8(r, node + 5, 2);
                (void)call4(r, sp, FN_1FB9F0, 0x3EC, 0x1000, 0x1000, 0x1000);
                (void)call2(r, sp, FN_1BA1A0, s, 0x00247FE0u);
                (void)call1(r, sp, FN_1BA1F0, node);
            }
            break;
        default:
            break;
        }
        (void)call1(r, sp, FN_1B1B70, node);
        method(r, sp, node);
        return;
    }
    case 2:
        st8(r, node + 4, st + 1);
        return;
    case 3:
        (void)call1(r, sp, FN_1AFC10, node);
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------------
 * 00159970(node), by the state byte +4 (s = node + 0x1F0):
 *   0: nothing unless 001B0FD0(node) is 0. Then 001C6380(node), +0x0A = 0,
 *      word +0x30 = 0x275458, +0 = 1, 0x700038A0 = (0, 0.3, 0, 0.2),
 *      001C5570(node, 0x700038A0, 0x71 when (byte +2 & 0x1F) == 6 else
 *      0x7B, 1);
 *   1: sub-state +5, then 001B17A0 and the node's method:
 *      0: r = 00157CE0(node, s): 0 nothing, 2 -> +5 = 2, 3 -> +5 += 1,
 *         other -> +5 = 3 and 001FB9F0(0x3E9, 0x1000, 0x1000, 0x1000);
 *      1: +5 = 2, 001BA1A0(s, D_00247020);
 *      2: when 001BA1F0(node): +0x0B = 0, +0 = 1, +5 = 0;
 *      3: when 001BA1F0(node): +5 = 1; other: nothing;
 *   2: +4 += 1; 3: 001AFC10(node); other: nothing.
 * At 001B0FD0 and 00157CE0 the original also leaves a1 = s, a2 = 2 and
 * a3 = 3; 001B0FD0 reads a0 only and 00157CE0 a0 and a1 (the reference
 * test measures it). 001BA1F0 and 001AFC10 receive a0 = the node. The
 * frame is 0x20 bytes.
 * ---------------------------------------------------------------------- */
static void f_159970(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t st = u8(r, node + 4);
    uint32_t s = node + 0x1F0;
    sp -= 0x20;
    switch (st) {
    case 0:
        if (call1(r, sp, FN_1B0FD0, node) != 0)
            return;
        (void)call1(r, sp, FN_1C6380, node);
        st8(r, node + 0x0A, 0);
        st32(r, node + 0x30, 0x00275458u);
        st8(r, node + 0, 1);
        st32(r, SPR(0x38A0), 0);
        st32(r, SPR(0x38A4), 0x3E99999Au);
        st32(r, SPR(0x38A8), 0);
        st32(r, SPR(0x38AC), 0x3E4CCCCDu);
        (void)call4(r, sp, FN_1C5570, node, SPR(0x38A0), (u8(r, node + 2) & 0x1F) == 6 ? 0x71 : 0x7B, 1);
        return;
    case 1: {
        uint32_t sub = u8(r, node + 5);
        switch (sub) {
        case 0: {
            uint64_t v = call2(r, sp, FN_157CE0, node, s);
            if (v == 0)
                break;
            if (v == 2) {
                st8(r, node + 5, 2);
            } else if (v == 3) {
                st8(r, node + 5, u8(r, node + 5) + 1);
            } else {
                st8(r, node + 5, 3);
                (void)call4(r, sp, FN_1FB9F0, 0x3E9, 0x1000, 0x1000, 0x1000);
            }
            break;
        }
        case 1:
            st8(r, node + 5, sub + 1);
            (void)call2(r, sp, FN_1BA1A0, s, 0x00247020u);
            break;
        case 2:
            if (call1(r, sp, FN_1BA1F0, node) != 0) {
                st8(r, node + 0x0B, 0);
                st8(r, node + 0, 1);
                st8(r, node + 5, 0);
            }
            break;
        case 3:
            if (call1(r, sp, FN_1BA1F0, node) != 0)
                st8(r, node + 5, 1);
            break;
        default:
            break;
        }
        (void)call1(r, sp, FN_1B17A0, node);
        method(r, sp, node);
        return;
    }
    case 2:
        st8(r, node + 4, st + 1);
        return;
    case 3:
        (void)call1(r, sp, FN_1AFC10, node);
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------------
 * 00159E70(node), by the state byte +4:
 *   0: nothing unless 001B0FD0(node) is 0. Then 001C6380(node) and
 *      halfword +0x28 = 0x29 in area 4, else 0x2A;
 *   1: sub-state +5 == 0: when the item-count byte D_00810C64[signed
 *      halfword +0x28] is nonzero: +5 = 1, word +0x78 of the record at
 *      word (word D_00275B40)+4 = pi/2, the room-mask byte |= 1 << (byte
 *      +0x2E), 001C6380(node); then (either way) 001B17A0(node). Every
 *      sub-state then calls the node's method;
 *   2, 3: 001AFC10(node); other: nothing.
 * The frame is 0x20 bytes.
 * ---------------------------------------------------------------------- */
static void f_159E70(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t st = u8(r, node + 4);
    sp -= 0x20;
    switch (st) {
    case 0:
        if (call1(r, sp, FN_1B0FD0, node) != 0)
            return;
        (void)call1(r, sp, FN_1C6380, node);
        st16(r, node + 0x28, u8(r, G_AREA) == 4 ? 0x29 : 0x2A);
        return;
    case 1: {
        uint32_t sub = u8(r, node + 5);
        if (sub == 0) {
            if (u8(r, 0x00810C64u + (uint32_t)s16(r, node + 0x28)) != 0) {
                uint32_t rec, area, k, at;
                st8(r, node + 5, sub + 1);
                rec = w32(r, w32(r, G_POSE) + 4);
                st32(r, rec + 0x78, F_HALFPI);
                area = u8(r, G_AREA);
                k = u8(r, node + 0x2E);
                at = G_MASK + area;
                st8(r, at, u8(r, at) | bit(k));
                (void)call1(r, sp, FN_1C6380, node);
            }
            (void)call1(r, sp, FN_1B17A0, node);
        }
        method(r, sp, node);
        return;
    }
    case 2:
    case 3:
        (void)call1(r, sp, FN_1AFC10, node);
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------------
 * 00183C40(p, out): the aim point into out[0..2]. Unless (byte +2 & 0x1F)
 * == 2, out = p+0xB0..B8. Otherwise by byte +3 (jump table of 19): 0, 3,
 * 4, 7, 8 -> the +0xC0..C8 triple of the record at word +0x118; 1, 2 ->
 * +0x11C; 5 -> +0x130; 6 -> +0x124; 10 -> +0x120; 11 -> +0x148 (the record
 * pointer re-read for each component); 16 -> (p+0xB0, 6.0 + p+0xB4,
 * p+0xB8); 18 -> out = (0, 6.0, 0, 1.0), then 001026A0(out, (word +0x110)
 * + 0x90, out); 9, 12..15, 17 and 19 up -> p+0xB0..B8. Each component is
 * stored before the next is read. The frame is 0x10 bytes.
 * ---------------------------------------------------------------------- */
static void f_183C40(Run *r, uint32_t sp, uint32_t p, uint32_t out)
{
    uint32_t slot = 0;
    sp -= 0x10;
    if ((u8(r, p + 2) & 0x1F) == 2) {
        switch (u8(r, p + 3)) {
        case 0: case 3: case 4: case 7: case 8:
            slot = 0x118;
            break;
        case 1: case 2:
            slot = 0x11C;
            break;
        case 5:
            slot = 0x130;
            break;
        case 6:
            slot = 0x124;
            break;
        case 10:
            slot = 0x120;
            break;
        case 11:
            slot = 0x148;
            break;
        case 16:
            st32(r, out + 0, w32(r, p + 0xB0));
            st32(r, out + 4, em_ee_add_bits(0x40C00000u, w32(r, p + 0xB4)));
            st32(r, out + 8, w32(r, p + 0xB8));
            return;
        case 18:
            st32(r, out + 0, 0);
            st32(r, out + 4, 0x40C00000u);
            st32(r, out + 8, 0);
            st32(r, out + 0xC, F_ONE);
            (void)call3(r, sp, FN_1026A0, out, w32(r, p + 0x110) + 0x90, out);
            return;
        default:
            break;
        }
    }
    if (slot) {
        st32(r, out + 0, w32(r, w32(r, p + slot) + 0xC0));
        st32(r, out + 4, w32(r, w32(r, p + slot) + 0xC4));
        st32(r, out + 8, w32(r, w32(r, p + slot) + 0xC8));
        return;
    }
    st32(r, out + 0, w32(r, p + 0xB0));
    st32(r, out + 4, w32(r, p + 0xB4));
    st32(r, out + 8, w32(r, p + 0xB8));
}

/* ------------------------------------------------------------------------
 * 001B18F0(node, a1, a2): four probes, v = a1, -a1, a2, -a2 in that order
 * (each component negated with NEG.S, read and stored one at a time):
 * 0x70003600 = (v, 1.0), 001026A0(0x70003600, node + 0xD0, 0x70003600),
 * r = 001B1630(f12..f14 = 0x70003600..08), byte +1 = r; the first r whose
 * low byte is nonzero ends with 001B1B70(node) and result 1. Four misses
 * give 0. The frame is 0x40 bytes.
 * ---------------------------------------------------------------------- */
static int32_t f_1B18F0(Run *r, uint32_t sp, uint32_t node, uint32_t a1, uint32_t a2)
{
    unsigned k, i;
    sp -= 0x40;
    for (k = 0; k < 4; k++) {
        uint32_t src = k < 2 ? a1 : a2;
        uint32_t f[3];
        uint64_t v;
        for (i = 0; i < 3; i++) {
            uint32_t x = w32(r, src + 4 * i);
            st32(r, SPR(0x3600) + 4 * i, (k & 1) ? em_ee_neg_bits(x) : x);
        }
        st32(r, SPR(0x360C), F_ONE);
        (void)call3(r, sp, FN_1026A0, SPR(0x3600), node + 0xD0, SPR(0x3600));
        for (i = 0; i < 3; i++)
            f[i] = w32(r, SPR(0x3600) + 4 * i);
        v = callv(r, sp, FN_1B1630, 0, NULL, 3, f, NULL);
        st8(r, node + 1, (uint32_t)v);
        if (v & 0xFF) {
            (void)call1(r, sp, FN_1B1B70, node);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * The door pair behaviours 001BC960 and 001BDFC0 share their state-1 sub
 * machine; `wide` is 00158590's third argument (0 for 001BC960, 1 for
 * 001BDFC0) and `story` picks 001BDFC0's test in sub-state 2.
 * e = word +0x18 read at entry (after the state byte), s = node + 0x1F0.
 * In both, "open(x)" is: +0 = 1, 00158590(node, 1, wide), and when
 * 001BC740(node, s) is nonzero, x; "shut" is 00158590(node, 0, wide), +0 =
 * 2 (in that order).
 * ---------------------------------------------------------------------- */

/* The partner test of sub-state 0: byte +0x0B of word e+0x18 == 0 for
 * byte +3 == 0, else byte +0x0B of word (word e+0x18)+0x18 == 3. */
static int partner_ready(Run *r, uint32_t node, uint32_t e)
{
    if (u8(r, node + 3) == 0)
        return u8(r, w32(r, e + 0x18) + 0x0B) == 0;
    return u8(r, w32(r, w32(r, e + 0x18) + 0x18) + 0x0B) == 3;
}

static void door_sub(Run *r, uint32_t sp, uint32_t node, uint32_t e, uint32_t wide, int story)
{
    uint32_t s = node + 0x1F0;
    switch (u8(r, node + 5)) {
    case 0: {
        int gate;
        if (u16(r, node + 0x2E) == 0xFF)
            gate = 1;
        else
            gate = (u8(r, G_MASK + u8(r, G_AREA)) & bit(u16(r, node + 0x2E))) != 0;
        if (gate) {
            if (partner_ready(r, node, e)) {
                st8(r, node + 0, 1);
                (void)call3(r, sp, FN_158590, node, 1, wide);
                if (call2(r, sp, FN_1BC740, node, s) != 0)
                    st8(r, node + 5, u8(r, node + 5) + 1);
            } else {
                (void)call3(r, sp, FN_158590, node, 0, wide);
                st8(r, node + 0, 2);
            }
        } else {
            st8(r, node + 0, 2);
            (void)call3(r, sp, FN_158590, node, 0, wide);
        }
        break;
    }
    case 1:
        (void)call3(r, sp, FN_158590, node, 1, wide);
        if (call1(r, sp, FN_1BA1F0, node) != 0) {
            (void)u16(r, node + 0x2E);
            st8(r, node + 5, u8(r, node + 3) == 0 ? 2 : 6);
        }
        break;
    case 2: {
        uint32_t flag;
        if (!story)
            (void)call3(r, sp, FN_158590, node, 0, wide);
        if (story) {
            uint32_t area = u8(r, G_AREA);
            flag = u8(r, 0x008107D8u + (area == 4 || area == 7 ? 0x5E : 0x5F));
        } else {
            flag = 0;
        }
        if (u16(r, node + 0x2E) == 0xFF) {
            if (!story)
                flag = u8(r, 0x0081083Eu);
            if (flag != 0) {
                st8(r, node + 5, 4);
            } else {
                st16(r, node + 0x28, 300);
                st8(r, node + 5, 3);
            }
        } else {
            if (!story)
                flag = u8(r, 0x0081083Eu);
            if (flag == 0) {
                st8(r, node + 5, 4);
            } else {
                st16(r, node + 0x28, 300);
                st8(r, node + 5, 3);
            }
        }
        if (story)
            (void)call3(r, sp, FN_158590, node, 0, wide);
        break;
    }
    case 3: {
        int32_t t = (int16_t)(s16(r, node + 0x28) - 1);
        st16(r, node + 0x28, (uint32_t)t);
        if (t == 0)
            st8(r, node + 5, u8(r, node + 5) + 1);
        (void)call3(r, sp, FN_158590, node, 0, wide);
        break;
    }
    case 4: {
        uint32_t q = w32(r, e + 0x18);
        if (u8(r, q + 0x0B) == 0) {
            st8(r, q + 0x0B, 2);
            st8(r, node + 5, u8(r, node + 5) + 1);
        }
        (void)call3(r, sp, FN_158590, node, 0, wide);
        break;
    }
    case 5:
        (void)call3(r, sp, FN_158590, node, 0, wide);
        break;
    case 6: {
        uint32_t q = w32(r, w32(r, e + 0x18) + 0x18);
        if (u8(r, q + 0x0B) == 3)
            st8(r, q + 0x0B, 4);
        st8(r, node + 5, 7);
        (void)call3(r, sp, FN_158590, node, 0, wide);
        break;
    }
    case 7:
        (void)call3(r, sp, FN_158590, node, 0, wide);
        break;
    default:
        break;
    }
    (void)call1(r, sp, FN_1B17A0, node);
    method(r, sp, node);
}

/* ------------------------------------------------------------------------
 * 001BC960(node), by the state byte +4:
 *   0: 001B0FD0(node), word +0x30 = 0x275610, 001C6380(node). Then with
 *      halfword +0x2E == 0xFF: byte +3 == 1 in (area 0x0D, room 0) or
 *      (area 4, room 7) starts the script (001BA1A0(s, D_0024E1A0), +4 =
 *      4), otherwise +0 = 1. With another +0x2E, only in area 4: room 7 as
 *      above (byte +3 == 1 script, else +0 = 1); other rooms: byte +3 == 0
 *      with D_0081076A == 0 -> +4 = 2; byte +3 == 1 with D_0081076A !=
 *      0xFF -> +4 = 2, +5 = 1;
 *   1: the shared sub-machine (door_sub, 00158590's third argument 0;
 *      sub-state 2 tests D_0081083E: != 0 for +0x2E == 0xFF, == 0
 *      otherwise, gives 4, else +0x28 = 300 and 3), 001B17A0, the method;
 *   2: by +5: 0: when the room mask has bit (+0x2E): 00158590(node, 0, 0),
 *      and when the halfword at 0x70003B84 is 0x208: byte +0x0B of the
 *      node at (word (word +0x18)+0x18) = 2, +4 = 1, +5 = 5.
 *      1: when the mask bit is set: D_0081076A == 0xFF -> +5 += 1; else
 *      when D_008107EA == 0x10: with no D_008106C0 record, open(+4 = 1,
 *      +5 = 1); with one whose byte +4 is 2 or more, +5 += 1; else its
 *      +0xB0 < 554.0 opens, and otherwise shut.
 *      2: open(+4 = 1, +5 = 1). Then 001B17A0 and the method;
 *   3: 001AFC10(node);
 *   4: 00158590(node, 0, 0); when 001BA1F0(node): +4 = 1, +0 = 1; then
 *      001B17A0 and the method.
 * The frame is 0x30 bytes.
 * ---------------------------------------------------------------------- */
static void open_door(Run *r, uint32_t sp, uint32_t node, uint32_t wide)
{
    st8(r, node + 0, 1);
    (void)call3(r, sp, FN_158590, node, 1, wide);
    if (call2(r, sp, FN_1BC740, node, node + 0x1F0) != 0) {
        st8(r, node + 4, 1);
        st8(r, node + 5, 1);
    }
}

static void f_1BC960(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t st = u8(r, node + 4);
    uint32_t e = w32(r, node + 0x18);
    uint32_t s = node + 0x1F0;
    sp -= 0x30;
    switch (st) {
    case 0: {
        uint32_t area;
        (void)call1(r, sp, FN_1B0FD0, node);
        st32(r, node + 0x30, 0x00275610u);
        (void)call1(r, sp, FN_1C6380, node);
        if (u16(r, node + 0x2E) == 0xFF) {
            if (u8(r, node + 3) == 1) {
                area = u8(r, G_AREA);
                if ((area == 0x0D && u8(r, G_ROOM) == 0) || (area == 4 && u8(r, G_ROOM) == 7)) {
                    (void)call2(r, sp, FN_1BA1A0, s, 0x0024E1A0u);
                    st8(r, node + 4, 4);
                } else {
                    st8(r, node + 0, 1);
                }
            } else {
                st8(r, node + 0, 1);
            }
        } else if (u8(r, G_AREA) == 4) {
            if (u8(r, G_ROOM) == 7) {
                if (u8(r, node + 3) == 1) {
                    (void)call2(r, sp, FN_1BA1A0, s, 0x0024E1A0u);
                    st8(r, node + 4, 4);
                } else {
                    st8(r, node + 0, 1);
                }
            } else {
                if (u8(r, node + 3) == 0 && u8(r, 0x0081076Au) == 0) {
                    st8(r, node + 4, 2);
                    break;
                }
                if (u8(r, node + 3) == 1 && u8(r, 0x0081076Au) != 0xFF) {
                    st8(r, node + 4, 2);
                    st8(r, node + 5, 1);
                }
            }
        }
        break;
    }
    case 1:
        door_sub(r, sp, node, e, 0, 0);
        break;
    case 2:
        switch (u8(r, node + 5)) {
        case 0:
            if (u8(r, G_MASK + u8(r, G_AREA)) & bit(u16(r, node + 0x2E))) {
                (void)call3(r, sp, FN_158590, node, 0, 0);
                if (u16(r, SPR(0x3B84)) == 0x208) {
                    uint32_t q = w32(r, w32(r, node + 0x18) + 0x18);
                    st8(r, q + 0x0B, 2);
                    st8(r, node + 4, 1);
                    st8(r, node + 5, 5);
                }
            }
            break;
        case 1:
            if (u8(r, G_MASK + u8(r, G_AREA)) & bit(u16(r, node + 0x2E))) {
                if (u8(r, 0x0081076Au) == 0xFF) {
                    st8(r, node + 5, u8(r, node + 5) + 1);
                } else if (u8(r, 0x008107EAu) == 0x10) {
                    uint32_t rec = w32(r, 0x008106C0u);
                    if (rec == 0) {
                        open_door(r, sp, node, 0);
                    } else if (u8(r, rec + 4) >= 2) {
                        st8(r, node + 5, u8(r, node + 5) + 1);
                    } else if (em_ee_c_lt_bits(w32(r, w32(r, 0x008106C0u) + 0xB0), 0x440A8000u)) {
                        open_door(r, sp, node, 0);
                    } else {
                        st8(r, node + 0, 2);
                        (void)call3(r, sp, FN_158590, node, 0, 0);
                    }
                }
            }
            break;
        case 2:
            open_door(r, sp, node, 0);
            break;
        default:
            break;
        }
        (void)call1(r, sp, FN_1B17A0, node);
        method(r, sp, node);
        break;
    case 3:
        (void)call1(r, sp, FN_1AFC10, node);
        break;
    case 4:
        (void)call3(r, sp, FN_158590, node, 0, 0);
        if (call1(r, sp, FN_1BA1F0, node) != 0) {
            st8(r, node + 4, 1);
            st8(r, node + 0, 1);
        }
        (void)call1(r, sp, FN_1B17A0, node);
        method(r, sp, node);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------
 * 001BDFC0(node), by the state byte +4 (e and s as 001BC960):
 *   0: 001B0FD0(node), word +0x30 = 0x275610, 001C6380(node). With
 *      halfword +0x2E == 0xFF: byte +3 == 1 in (area 7, room 0) or (area 8,
 *      room 0) starts the script (001BA1A0(s, D_0024E1A0), +4 = 4),
 *      otherwise +0 = 1. With another +0x2E: in area 4 room 0x0A, and in
 *      any other area room 1: byte +3 == 1 starts the script, else +0 = 1;
 *      other rooms nothing;
 *   1: the shared sub-machine with 00158590's third argument 1; sub-state
 *      2 tests the story byte D_008107D8[0x5E] (areas 4 and 7) or
 *      [0x5F]: != 0 for +0x2E == 0xFF, == 0 otherwise, gives 4, else
 *      +0x28 = 300 and 3, and calls 00158590(node, 0, 1) after the test;
 *      then 001B17A0 and the method;
 *   2, 3: 001AFC10(node);
 *   4: when 001BA1F0(node): +4 = 1, +0 = 1; then 00158590(node, 0, 1),
 *      001B17A0 and the method.
 * The frame is 0x30 bytes.
 * ---------------------------------------------------------------------- */
static void f_1BDFC0(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t st = u8(r, node + 4);
    uint32_t e = w32(r, node + 0x18);
    uint32_t s = node + 0x1F0;
    sp -= 0x30;
    switch (st) {
    case 0: {
        (void)call1(r, sp, FN_1B0FD0, node);
        st32(r, node + 0x30, 0x00275610u);
        (void)call1(r, sp, FN_1C6380, node);
        if (u16(r, node + 0x2E) == 0xFF) {
            if (u8(r, node + 3) == 1) {
                uint32_t area = u8(r, G_AREA);
                int go = 0;
                if (area == 7 && u8(r, G_ROOM) == 0)
                    go = 1;
                else if (u8(r, G_AREA) == 8 && u8(r, G_ROOM) == 0)
                    go = 1;
                if (go) {
                    (void)call2(r, sp, FN_1BA1A0, s, 0x0024E1A0u);
                    st8(r, node + 4, 4);
                } else {
                    st8(r, node + 0, 1);
                }
            } else {
                st8(r, node + 0, 1);
            }
        } else {
            int room_ok;
            if (u8(r, G_AREA) == 4)
                room_ok = u8(r, G_ROOM) == 0x0A;
            else
                room_ok = u8(r, G_ROOM) == 1;
            if (room_ok) {
                if (u8(r, node + 3) == 1) {
                    (void)call2(r, sp, FN_1BA1A0, s, 0x0024E1A0u);
                    st8(r, node + 4, 4);
                } else {
                    st8(r, node + 0, 1);
                }
            }
        }
        break;
    }
    case 1:
        door_sub(r, sp, node, e, 1, 1);
        break;
    case 2:
    case 3:
        (void)call1(r, sp, FN_1AFC10, node);
        break;
    case 4:
        if (call1(r, sp, FN_1BA1F0, node) != 0) {
            st8(r, node + 4, 1);
            st8(r, node + 0, 1);
        }
        (void)call3(r, sp, FN_158590, node, 0, 1);
        (void)call1(r, sp, FN_1B17A0, node);
        method(r, sp, node);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------
 * 001BD560(node), by the state byte +4 (s = node + 0x1F0):
 *   0: 001B0FD0(node), +0 = 1, halfword +0x34 = byte +0x2E, halfword +0x2E
 *      = 0, words s+0x10 and s+0x14 = 0; the words +0x80..+0x88 = 1.5 when
 *      bit 0x40 of halfword +0x56, else 2.0 for bit 0x80, else 1.0;
 *   1: with byte +3 == 0x0B the keyed machine (sub-state +5, 0..7), else
 *      the plain one (0..6); 8 / 7 and up do nothing. Then 001C6380(node),
 *      the node's method, and when byte +0x0B (read after the method) is
 *      not 3 and the float at (word +0x114)+0x80 is below 18.0: +1 = 1 and
 *      001B1D20(node).
 *      keyed: 0: word 0x70003258 = 1; when +0x0B == 2: +5 += 1,
 *         001BBD20(node, 0), 0x70003258 = 0.
 *         1: when 001BD180(s): +5 += 1, +0x0B = 3.
 *         2: 0x70003258 = 0; when +0x0B == 4: +5 += 1, 001BBD20(node, 1),
 *         0x70003258 = 1.
 *         3: when 001BD270(s): +0x0B = 0, s+0x10 = s+0x14 = 0; D_0081076C
 *         == 0: D_0081076C = 1, +5 = 7; else +5 += 1, 001BA1A0(s,
 *         D_0024E6E0).
 *         4: when 001BA1F0(node): 001BC150(node), D_0081083E = 1 -
 *         D_0081083E, +5 += 1.
 *         5: +5 = the sub-state read + 1, 001AEBA0(4, 1). 6: nothing.
 *         7: when D_0081076C == 0xFF: 001BC150(node), D_0081083E = 1 -
 *         D_0081083E, +5 = 5.
 *      plain: 0: when +0x0B == 2: +5 += 1, 001BBD20(node, 0).
 *         1: when 001BD370(s): +5 += 1, +0x0B = 3.
 *         2: when +0x0B == 4: +5 = the sub-state read + 1,
 *         001BBD20(node, 1).
 *         3: when 001BD460(s): +0x0B = 0, s+0x10 = s+0x14 = 0, +5 += 1,
 *         001BA1A0(s, D_0024E6E0).
 *         4: when 001BA1F0(node): 001BC150(node); D_00810836 (areas 4 and
 *         7) or D_00810837 = 1 - itself; +5 += 1.
 *         5: +5 = the sub-state read + 1, 001AEBA0(4, 1). 6: nothing.
 *   2, 3: 001AFC10(node); other: nothing.
 * "+5 += 1" re-reads the byte. The original keeps a1 = 1 through the whole
 * of state 1, and in keyed sub-state 3 / plain sub-state 3 it passes the
 * a0 and a1 that 001BD270 / 001BD460 leave (both leaves write neither, so
 * they are s and 1): the D_0081076C store is that a1 (1) and 001BA1A0's a0
 * is s. The frame is 0x30 bytes.
 * ---------------------------------------------------------------------- */
static void f_1BD560(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t st = u8(r, node + 4);
    uint32_t s = node + 0x1F0;
    sp -= 0x30;
    switch (st) {
    case 0: {
        uint32_t k, h, v;
        (void)call1(r, sp, FN_1B0FD0, node);
        st8(r, node + 0, 1);
        k = u8(r, node + 0x2E);
        st16(r, node + 0x34, k);
        st16(r, node + 0x2E, 0);
        st32(r, s + 0x10, 0);
        st32(r, s + 0x14, 0);
        h = (uint32_t)s16(r, node + 0x56);
        v = (h & 0x40) ? 0x3FC00000u : (h & 0x80) ? 0x40000000u : F_ONE;
        st32(r, node + 0x80, v);
        st32(r, node + 0x84, v);
        st32(r, node + 0x88, v);
        return;
    }
    case 1: {
        uint32_t sub;
        if (u8(r, node + 3) == 0x0B) {
            sub = u8(r, node + 5);
            switch (sub) {
            case 0:
                st32(r, SPR(0x3258), 1);
                if (u8(r, node + 0x0B) == 2) {
                    st8(r, node + 5, u8(r, node + 5) + 1);
                    (void)call2(r, sp, FN_1BBD20, node, 0);
                    st32(r, SPR(0x3258), 0);
                }
                break;
            case 1:
                if (call1(r, sp, FN_1BD180, s) != 0) {
                    st8(r, node + 5, u8(r, node + 5) + 1);
                    st8(r, node + 0x0B, 3);
                }
                break;
            case 2:
                st32(r, SPR(0x3258), 0);
                if (u8(r, node + 0x0B) == 4) {
                    st8(r, node + 5, u8(r, node + 5) + 1);
                    (void)call2(r, sp, FN_1BBD20, node, 1);
                    st32(r, SPR(0x3258), 1);
                }
                break;
            case 3:
                if (call1(r, sp, FN_1BD270, s) != 0) {
                    st8(r, node + 0x0B, 0);
                    st32(r, s + 0x10, 0);
                    st32(r, s + 0x14, 0);
                    if (u8(r, 0x0081076Cu) == 0) {
                        st8(r, 0x0081076Cu, 1);
                        st8(r, node + 5, 7);
                    } else {
                        st8(r, node + 5, u8(r, node + 5) + 1);
                        (void)call2(r, sp, FN_1BA1A0, s, 0x0024E6E0u);
                    }
                }
                break;
            case 4:
                if (call1(r, sp, FN_1BA1F0, node) != 0) {
                    (void)call1(r, sp, FN_1BC150, node);
                    st8(r, 0x0081083Eu, 1 - u8(r, 0x0081083Eu));
                    st8(r, node + 5, u8(r, node + 5) + 1);
                }
                break;
            case 5:
                st8(r, node + 5, sub + 1);
                (void)call2(r, sp, FN_1AEBA0, 4, 1);
                break;
            case 7:
                if (u8(r, 0x0081076Cu) == 0xFF) {
                    (void)call1(r, sp, FN_1BC150, node);
                    st8(r, 0x0081083Eu, 1 - u8(r, 0x0081083Eu));
                    st8(r, node + 5, 5);
                }
                break;
            default:
                break;
            }
        } else {
            sub = u8(r, node + 5);
            switch (sub) {
            case 0:
                if (u8(r, node + 0x0B) == 2) {
                    st8(r, node + 5, u8(r, node + 5) + 1);
                    (void)call2(r, sp, FN_1BBD20, node, 0);
                }
                break;
            case 1:
                if (call1(r, sp, FN_1BD370, s) != 0) {
                    st8(r, node + 5, u8(r, node + 5) + 1);
                    st8(r, node + 0x0B, 3);
                }
                break;
            case 2:
                if (u8(r, node + 0x0B) == 4) {
                    st8(r, node + 5, sub + 1);
                    (void)call2(r, sp, FN_1BBD20, node, 1);
                }
                break;
            case 3:
                if (call1(r, sp, FN_1BD460, s) != 0) {
                    st8(r, node + 0x0B, 0);
                    st32(r, s + 0x10, 0);
                    st32(r, s + 0x14, 0);
                    st8(r, node + 5, u8(r, node + 5) + 1);
                    (void)call2(r, sp, FN_1BA1A0, s, 0x0024E6E0u);
                }
                break;
            case 4:
                if (call1(r, sp, FN_1BA1F0, node) != 0) {
                    uint32_t area, at;
                    (void)call1(r, sp, FN_1BC150, node);
                    area = u8(r, G_AREA);
                    at = (area == 4 || area == 7) ? 0x00810836u : 0x00810837u;
                    st8(r, at, 1 - u8(r, at));
                    st8(r, node + 5, u8(r, node + 5) + 1);
                }
                break;
            case 5:
                st8(r, node + 5, sub + 1);
                (void)call2(r, sp, FN_1AEBA0, 4, 1);
                break;
            default:
                break;
            }
        }
        (void)call1(r, sp, FN_1C6380, node);
        method(r, sp, node);
        if (u8(r, node + 0x0B) != 3 && em_ee_c_lt_bits(w32(r, w32(r, node + 0x114) + 0x80), 0x41900000u)) {
            st8(r, node + 1, 1);
            (void)call1(r, sp, FN_1B1D20, node);
        }
        return;
    }
    case 2:
    case 3:
        (void)call1(r, sp, FN_1AFC10, node);
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------------
 * 001C48C0(node), by the state byte +4: 0: when 001B0FD0(node) is 0,
 * 001C6380(node); 1: 001B1B70(node) and the node's method; 2, 3:
 * 001AFC10(node); other: nothing. The frame is 0x20 bytes.
 * ---------------------------------------------------------------------- */
static void f_1C48C0(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t st = u8(r, node + 4);
    sp -= 0x20;
    switch (st) {
    case 0:
        if (call1(r, sp, FN_1B0FD0, node) == 0)
            (void)call1(r, sp, FN_1C6380, node);
        return;
    case 1:
        (void)call1(r, sp, FN_1B1B70, node);
        method(r, sp, node);
        return;
    case 2:
    case 3:
        (void)call1(r, sp, FN_1AFC10, node);
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------------
 * 001C4960(node), by the state byte +4: 0: when 001B1020(node, byte +0x0D,
 * -1, 0) is 0, 001C6380(node); 1: 001B17A0(node) and the node's method; 3
 * and every other value (2 and 4..255 alike): 001AFC10(node). The frame is
 * 0x20 bytes.
 * ---------------------------------------------------------------------- */
static void f_1C4960(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t st = u8(r, node + 4);
    sp -= 0x20;
    switch (st) {
    case 0:
        if (call4(r, sp, FN_1B1020, node, u8(r, node + 0x0D), 0xFFFFFFFFu, 0) == 0)
            (void)call1(r, sp, FN_1C6380, node);
        return;
    case 1:
        (void)call1(r, sp, FN_1B17A0, node);
        method(r, sp, node);
        return;
    default:
        (void)call1(r, sp, FN_1AFC10, node);
        return;
    }
}

/* ------------------------------------------------------------------------
 * 001C4AF0(node), by the state byte +4: 0: when 001B0FD0(node) is 0,
 * 001CA5E0(node, word +0x44, 1) and 001C6380(node); 1: 001B17A0(node) and
 * the node's method; 2, 3: 001AFC10(node); other: nothing. The frame is
 * 0x20 bytes.
 * ---------------------------------------------------------------------- */
static void f_1C4AF0(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t st = u8(r, node + 4);
    sp -= 0x20;
    switch (st) {
    case 0:
        if (call1(r, sp, FN_1B0FD0, node) == 0) {
            (void)call3(r, sp, FN_1CA5E0, node, w32(r, node + 0x44), 1);
            (void)call1(r, sp, FN_1C6380, node);
        }
        return;
    case 1:
        (void)call1(r, sp, FN_1B17A0, node);
        method(r, sp, node);
        return;
    case 2:
    case 3:
        (void)call1(r, sp, FN_1AFC10, node);
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------------
 * 001C4CB0(node), by the state byte +4:
 *   0: +4 = 1;
 *   1: by byte +3:
 *      2: 001BA1C0(node, 9) nonzero -> +4 = 3; else when D_00810761 == 1:
 *         0x700038B0 = (0x80, 0x10, 0, 0x80) words, then for i = 0..5:
 *         0x700038A0 = (the float triple D_00250F40[3i..3i+2], 1.0),
 *         001F4E40(0x700038A0, 0x700038B0, 10 + 2i; f12 = 30.0);
 *      3: 001BA1C0(node, 12) nonzero -> +4 = 3; else the same with
 *         D_00250F90 for i = 0..1 and ids 10 + 4i;
 *      4: when halfword +0x2E of the node at word +0x18 is nonzero:
 *         0x700038A0 = (-17.457, 18.783, -11.908, 1.0) (bits 0xC18BA7F0,
 *         0x41964396, 0xC13E872B), 001026A0(0x700038A0, that node + 0xD0,
 *         0x700038A0), the 0x700038B0 block, 001F4E40(..., 15; 30.0);
 *      other: nothing;
 *   2, 3: 001AFC10(node); other: nothing.
 * The frame is 0x40 bytes.
 * ---------------------------------------------------------------------- */
static void emit_points(Run *r, uint32_t sp, uint32_t table, unsigned n, uint32_t step)
{
    uint32_t id = 10;
    unsigned i;
    uint32_t f12 = 0x41F00000u;
    st32(r, SPR(0x38B0), 0x80);
    st32(r, SPR(0x38B4), 0x10);
    st32(r, SPR(0x38B8), 0);
    st32(r, SPR(0x38BC), 0x80);
    for (i = 0; i < n; i++) {
        uint64_t v[3];
        st32(r, SPR(0x38A0), w32(r, table + 0));
        st32(r, SPR(0x38A4), w32(r, table + 4));
        st32(r, SPR(0x38A8), w32(r, table + 8));
        st32(r, SPR(0x38AC), F_ONE);
        v[0] = reg(SPR(0x38A0));
        v[1] = reg(SPR(0x38B0));
        v[2] = reg(id);
        (void)callv(r, sp, FN_1F4E40, 3, v, 1, &f12, NULL);
        table += 12;
        id += step;
    }
}

static void f_1C4CB0(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t st = u8(r, node + 4);
    sp -= 0x40;
    switch (st) {
    case 0:
        st8(r, node + 4, 1);
        return;
    case 1:
        switch (u8(r, node + 3)) {
        case 2:
            if (call2(r, sp, FN_1BA1C0, node, 9) != 0) {
                st8(r, node + 4, 3);
                return;
            }
            if (u8(r, 0x00810761u) == 1)
                emit_points(r, sp, 0x00250F40u, 6, 2);
            return;
        case 3:
            if (call2(r, sp, FN_1BA1C0, node, 12) != 0) {
                st8(r, node + 4, 3);
                return;
            }
            emit_points(r, sp, 0x00250F90u, 2, 4);
            return;
        case 4: {
            uint32_t q = w32(r, node + 0x18);
            uint64_t v[3];
            uint32_t f12 = 0x41F00000u;
            if (u16(r, q + 0x2E) == 0)
                return;
            st32(r, SPR(0x38A0), 0xC18BA7F0u);
            st32(r, SPR(0x38A4), 0x41964396u);
            st32(r, SPR(0x38A8), 0xC13E872Bu);
            st32(r, SPR(0x38AC), F_ONE);
            (void)call3(r, sp, FN_1026A0, SPR(0x38A0), q + 0xD0, SPR(0x38A0));
            st32(r, SPR(0x38B0), 0x80);
            st32(r, SPR(0x38B4), 0x10);
            st32(r, SPR(0x38B8), 0);
            st32(r, SPR(0x38BC), 0x80);
            v[0] = reg(SPR(0x38A0));
            v[1] = reg(SPR(0x38B0));
            v[2] = reg(15);
            (void)callv(r, sp, FN_1F4E40, 3, v, 1, &f12, NULL);
            return;
        }
        default:
            return;
        }
    case 2:
    case 3:
        (void)call1(r, sp, FN_1AFC10, node);
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------------
 * 001C7EB0(node), by the state byte +4; rec = word +0x24 (read at entry,
 * before the state byte), t = node + 0x1F0, cb = word t+0xFC (node +0x2EC):
 *   0: byte +0x0C = 1; when the signed halfword D_00275BCC is below byte
 *      +0x0C (re-read): +4 = 3. Otherwise for i = 0 while i < byte +0x0C
 *      (re-read each time): word +0x110+4i = 001AF780(); then +9 = the last
 *      count read, 001CB5B0(byte +0x0C, re-read), cb(node) when cb is
 *      nonzero, 001C63E0(node, signed halfword +0x94), word t+0xF4 = 1,
 *      +4 = 1;
 *   1: with rec nonzero and bit (halfword +0x2E) of rec's halfword +0x2E:
 *      +4 = 3. Otherwise word t+0xF4 -= 1 (stored, re-read): when negative,
 *      n = 001C64F0(node; f12 = word t+0xF8), else n = 0. The words
 *      +0xB0..+0xCC = (0, 0, 0, 1.0, 0, 0, 0, 1.0); 001C68C0(node); with
 *      P = word (word D_00275B40) re-read for each: +0xB0 = P+0xC0, +0xB4 =
 *      P+0xC4, +0xB8 = P+0xC8, +0xBC = 1.0; the quadwords P+0x90..P+0xC0 to
 *      +0xD0..+0x100 (P read once more), then word +0x10C = 1.0. When n &
 *      0x3000: +4 = 3 and cb(node) when nonzero. Otherwise, with P re-read,
 *      unless P+0x18, P+0x1C and P+0x20 are all below 0.9 (tested in that
 *      order, stopping at the first that is not), cb(node) when nonzero;
 *   2, 3: 001AFC10(node); other: nothing.
 * The frame is 0x50 bytes.
 * ---------------------------------------------------------------------- */
static uint32_t pose(Run *r)
{
    return w32(r, w32(r, G_POSE));
}

static void callback(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t cb = w32(r, node + 0x1F0 + 0xFC);
    if (cb != 0)
        (void)call1(r, sp, cb, node);
}

static void f_1C7EB0(Run *r, uint32_t sp, uint32_t node)
{
    uint32_t rec = w32(r, node + 0x24);
    uint32_t t = node + 0x1F0;
    uint32_t st = u8(r, node + 4);
    sp -= 0x50;
    switch (st) {
    case 0: {
        int32_t i = 0;
        uint32_t c;
        st8(r, node + 0x0C, 1);
        if (s16(r, 0x00275BCCu) < (int32_t)u8(r, node + 0x0C)) {
            st8(r, node + 4, 3);
            return;
        }
        c = u8(r, node + 0x0C);
        while (i < (int32_t)c) {
            uint32_t v = (uint32_t)call0(r, sp, FN_1AF780);
            st32(r, node + 0x110 + 4 * (uint32_t)i, v);
            i++;
            c = u8(r, node + 0x0C);
        }
        st8(r, node + 9, c);
        (void)call1(r, sp, FN_1CB5B0, u8(r, node + 0x0C));
        callback(r, sp, node);
        (void)call2(r, sp, FN_1C63E0, node, (uint32_t)s16(r, node + 0x94));
        st32(r, t + 0xF4, 1);
        st8(r, node + 4, 1);
        return;
    }
    case 1: {
        uint64_t n = 0;
        uint32_t p, a;
        if (rec != 0) {
            uint32_t h = u16(r, node + 0x2E);
            if (u16(r, rec + 0x2E) & bit(h)) {
                st8(r, node + 4, 3);
                return;
            }
        }
        st32(r, t + 0xF4, w32(r, t + 0xF4) - 1);
        if ((int32_t)w32(r, t + 0xF4) < 0) {
            uint64_t v[1];
            uint32_t f12 = w32(r, t + 0xF8);
            v[0] = reg(node);
            n = callv(r, sp, FN_1C64F0, 1, v, 1, &f12, NULL);
        }
        st32(r, node + 0xB0, 0);
        st32(r, node + 0xB4, 0);
        st32(r, node + 0xB8, 0);
        st32(r, node + 0xBC, F_ONE);
        st32(r, node + 0xC0, 0);
        st32(r, node + 0xC4, 0);
        st32(r, node + 0xC8, 0);
        st32(r, node + 0xCC, F_ONE);
        (void)call1(r, sp, FN_1C68C0, node);
        st32(r, node + 0xB0, w32(r, pose(r) + 0xC0));
        st32(r, node + 0xB4, w32(r, pose(r) + 0xC4));
        st32(r, node + 0xB8, w32(r, pose(r) + 0xC8));
        st32(r, node + 0xBC, F_ONE);
        p = pose(r);
        copy_qw(r, node + 0xD0, p + 0x90);
        copy_qw(r, node + 0xE0, p + 0xA0);
        copy_qw(r, node + 0xF0, p + 0xB0);
        copy_qw(r, node + 0x100, p + 0xC0);
        st32(r, node + 0x10C, F_ONE);
        if (n & 0x3000) {
            st8(r, node + 4, 3);
            callback(r, sp, node);
            return;
        }
        a = pose(r);
        if (em_ee_c_lt_bits(w32(r, a + 0x18), 0x3F666666u) && em_ee_c_lt_bits(w32(r, a + 0x1C), 0x3F666666u) &&
            em_ee_c_lt_bits(w32(r, a + 0x20), 0x3F666666u))
            return;
        callback(r, sp, node);
        return;
    }
    case 2:
    case 3:
        (void)call1(r, sp, FN_1AFC10, node);
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------------
 * 001C8140(a0, a1, a2): n = 001AFA90(0x0C); when n is nonzero (the whole
 * register): byte n+3 = 0x0C, byte n+0x0D = 0x63, halfword n+0x94 = a1,
 * word n+0x40 = a0, word n+0x10 = 0x1C7EB0, word n+0x24 = 0, word n+0x2E8 =
 * 0.5, word n+0x2EC = a2. The frame is 0x40 bytes.
 * ---------------------------------------------------------------------- */
static void f_1C8140(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint64_t v;
    uint32_t n;
    sp -= 0x40;
    v = call1(r, sp, FN_1AFA90, 0x0C);
    if (v == 0)
        return;
    n = (uint32_t)v;
    st8(r, n + 3, 0x0C);
    st8(r, n + 0x0D, 0x63);
    st16(r, n + 0x94, a1);
    st32(r, n + 0x40, a0);
    st32(r, n + 0x10, FN_1C7EB0);
    st32(r, n + 0x24, 0);
    st32(r, n + 0x2E8, F_HALF);
    st32(r, n + 0x2EC, a2);
}

/* ------------------------------------------------------------------------
 * 001CAE40(node, a1): f12 = the float at (word +0x44)+0x20 when that word is
 * nonzero, else 20.0; r = 001CA7B0(node + 0xB0; f12) (the whole register).
 * Nothing more when r < 0. Otherwise keep = word (word D_00275670)+0x1C;
 * 001D8C20(1); 001C7420(node, 0x3F5, 3); 001D3CE0(a1) when r is odd, else
 * 001D3AC0(a1); then with d = word D_00275670 (read once) and the record
 * pointer word d+0x1C re-read before each access: byte +3 = 0x60, word +4 =
 * 0, halfword +0 = 0, and d+0x1C advanced by 0x10; 001D8C20(0);
 * 001CAAC0((word D_00275B44) + 0xB0, keep). The frame is 0x50 bytes.
 * ---------------------------------------------------------------------- */
static void f_1CAE40(Run *r, uint32_t sp, uint32_t node, uint32_t a1)
{
    uint32_t q, f12, keep, d;
    uint64_t v[1], res;
    sp -= 0x50;
    q = w32(r, node + 0x44);
    f12 = q != 0 ? w32(r, q + 0x20) : 0x41A00000u;
    v[0] = reg(node + 0xB0);
    res = callv(r, sp, FN_1CA7B0, 1, v, 1, &f12, NULL);
    if ((int64_t)res < 0)
        return;
    keep = w32(r, w32(r, 0x00275670u) + 0x1C);
    (void)call1(r, sp, FN_1D8C20, 1);
    (void)call3(r, sp, FN_1C7420, node, 0x3F5, 3);
    if (res != 0 && (res & 1))
        (void)call1(r, sp, FN_1D3CE0, a1);
    else
        (void)call1(r, sp, FN_1D3AC0, a1);
    d = w32(r, 0x00275670u);
    st8(r, w32(r, d + 0x1C) + 3, 0x60);
    st32(r, w32(r, d + 0x1C) + 4, 0);
    st16(r, w32(r, d + 0x1C) + 0, 0);
    st32(r, d + 0x1C, w32(r, d + 0x1C) + 0x10);
    (void)call1(r, sp, FN_1D8C20, 0);
    (void)call2(r, sp, FN_1CAAC0, w32(r, 0x00275B44u) + 0xB0, keep);
}

/* ------------------------------------------------------------------------
 * 001CB4F0(a0, a1): v = 001D2910(0) (the whole register); 001D2830(0, 0);
 * 001D8C20(1); 001C7420(a0, 0x3F5, 0); 001D1F80(0, 1, 0); 001D38F0(a1);
 * 001D8C20(0); 001D2830(0, v). The frame is 0x40 bytes.
 * ---------------------------------------------------------------------- */
static void f_1CB4F0(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    uint64_t v, x[2];
    sp -= 0x40;
    v = call1(r, sp, FN_1D2910, 0);
    (void)call2(r, sp, FN_1D2830, 0, 0);
    (void)call1(r, sp, FN_1D8C20, 1);
    (void)call3(r, sp, FN_1C7420, a0, 0x3F5, 0);
    (void)call3(r, sp, FN_1D1F80, 0, 1, 0);
    (void)call1(r, sp, FN_1D38F0, a1);
    (void)call1(r, sp, FN_1D8C20, 0);
    x[0] = 0;
    x[1] = v;
    (void)callv(r, sp, FN_1D2830, 2, x, 0, NULL, NULL);
}

/* ------------------------------------------------------------------------
 * Entries
 * ---------------------------------------------------------------------- */

#define ENTER(fn)                                                                                                    \
    Run run;                                                                                                         \
    if (!s)                                                                                                          \
        return -1;                                                                                                   \
    if (s->fault != EM_AREA02_MATH_FAULT_NONE)                                                                       \
        return -1;                                                                                                   \
    run.s = s;                                                                                                       \
    run.entry = (fn);                                                                                                \
    if (setjmp(run.out))                                                                                             \
        return -1;                                                                                                   \
    if (!s->regions)                                                                                                 \
        fault(&run, EM_AREA02_MATH_FAULT_NULL, 0)

static void put(int32_t *out, int32_t v)
{
    if (out)
        *out = v;
}

int em_area02_math_001575E0(EmArea02Math *s, uint32_t a0, int32_t *out)
{
    int32_t v;
    ENTER(0x001575E0u);
    v = f_1575E0(&run, s->sp, a0);
    put(out, v);
    return 0;
}

int em_area02_math_00157B30(EmArea02Math *s, uint32_t a0, uint32_t a1, int32_t *out)
{
    int32_t v;
    ENTER(0x00157B30u);
    v = f_157B30(&run, s->sp, a0, a1);
    put(out, v);
    return 0;
}

int em_area02_math_00157F30(EmArea02Math *s, uint32_t a0, int32_t *out)
{
    int32_t v;
    ENTER(0x00157F30u);
    v = f_157F30(&run, a0);
    put(out, v);
    return 0;
}

int em_area02_math_00158050(EmArea02Math *s, uint32_t a0, int32_t *out)
{
    int32_t v;
    ENTER(0x00158050u);
    v = f_158050(&run, s->sp, a0);
    put(out, v);
    return 0;
}

#define VOID1(addr, fname)                                                                                           \
    int em_area02_math_##addr(EmArea02Math *s, uint32_t a0)                                                          \
    {                                                                                                                \
        ENTER(0x##addr##u);                                                                                          \
        fname(&run, s->sp, a0);                                                                                      \
        return 0;                                                                                                    \
    }

VOID1(001582E0, f_1582E0)
VOID1(00158EC0, f_158EC0)
VOID1(00159620, f_159620)
VOID1(00159970, f_159970)
VOID1(00159E70, f_159E70)
VOID1(001BC960, f_1BC960)
VOID1(001BD560, f_1BD560)
VOID1(001BDFC0, f_1BDFC0)
VOID1(001C48C0, f_1C48C0)
VOID1(001C4960, f_1C4960)
VOID1(001C4AF0, f_1C4AF0)
VOID1(001C4CB0, f_1C4CB0)
VOID1(001C7EB0, f_1C7EB0)

int em_area02_math_00183C40(EmArea02Math *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x00183C40u);
    f_183C40(&run, s->sp, a0, a1);
    return 0;
}

int em_area02_math_001B18F0(EmArea02Math *s, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *out)
{
    int32_t v;
    ENTER(0x001B18F0u);
    v = f_1B18F0(&run, s->sp, a0, a1, a2);
    put(out, v);
    return 0;
}

int em_area02_math_001C8140(EmArea02Math *s, uint32_t a0, uint32_t a1, uint32_t a2)
{
    ENTER(0x001C8140u);
    f_1C8140(&run, s->sp, a0, a1, a2);
    return 0;
}

int em_area02_math_001CAE40(EmArea02Math *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001CAE40u);
    f_1CAE40(&run, s->sp, a0, a1);
    return 0;
}

/* 001CAF60(node): continues into 001CAE40(node, word +0x44) with no frame
 * of its own. */
int em_area02_math_001CAF60(EmArea02Math *s, uint32_t a0)
{
    ENTER(0x001CAF60u);
    f_1CAE40(&run, s->sp, a0, w32(&run, a0 + 0x44));
    return 0;
}

int em_area02_math_001CB4F0(EmArea02Math *s, uint32_t a0, uint32_t a1)
{
    ENTER(0x001CB4F0u);
    f_1CB4F0(&run, s->sp, a0, a1);
    return 0;
}

/* 001CB580(node): continues into 001CB4F0(node, word +0x44) with no frame
 * of its own. */
int em_area02_math_001CB580(EmArea02Math *s, uint32_t a0)
{
    ENTER(0x001CB580u);
    f_1CB4F0(&run, s->sp, a0, w32(&run, a0 + 0x44));
    return 0;
}
