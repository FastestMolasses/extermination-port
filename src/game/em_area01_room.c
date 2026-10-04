/* em_area01_room.c - AREA01 lane A01ROOM translations (see em_area01_room.h
 * and docs/AREA01_ROOM.md). Every routine names its original address; the
 * comments say what the original does, never how it is encoded. */
#include "em_area01_room.h"

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#include "em_ee_float.h"

/* ------------------------------------------------------------------------
 * Fail-stop plumbing (the lane SIDE / SYS model)
 * ---------------------------------------------------------------------- */

typedef struct {
    EmArea01Room *s;
    jmp_buf out;
} Run;

static void fault(Run *r, int32_t code, uint32_t address)
{
    if (r->s->fault == EM_AREA01_ROOM_FAULT_NONE) {
        r->s->fault = code;
        r->s->fault_address = address;
    }
    longjmp(r->out, 1);
}

void em_area01_room_clear_fault(EmArea01Room *s)
{
    if (!s)
        return;
    s->fault = EM_AREA01_ROOM_FAULT_NONE;
    s->fault_function = 0;
    s->fault_address = 0;
}

/* The byte span [address, address + size) inside one region, or a fault. */
static uint8_t *span(Run *r, uint32_t address, uint32_t size, int write)
{
    const EmArea01Room *s = r->s;
    unsigned i;
    if (s->view) {
        uint8_t *p = size && (uint64_t)address + size <= UINT64_C(0x100000000)
                         ? s->view(s->ctx, address, size, write) : NULL;
        if (p) return p;
        fault(r, EM_AREA01_ROOM_FAULT_UNMAPPED, address);
    }
    for (i = 0; s->regions && i < s->region_count; i++) {
        const EmArea01RoomRegion *g = &s->regions[i];
        if (g->bytes && address >= g->base && size <= g->size && address - g->base <= g->size - size)
            return g->bytes + (address - g->base);
    }
    fault(r, EM_AREA01_ROOM_FAULT_UNMAPPED, address);
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

/* Test hook. A build that defines EM_AREA01_ROOM_STORE_TRACE as a function
 * name reports every store (original address, size) to it once the address
 * is known to be mapped; tools/test_area01_room_reference.py uses it to
 * compare memory with the original at every call leaving the module.
 * Ordinary builds compile it out. wr is the only store. */
#ifdef EM_AREA01_ROOM_STORE_TRACE
void EM_AREA01_ROOM_STORE_TRACE(uint32_t address, unsigned size);
#define TRACE_STORE(a, n) EM_AREA01_ROOM_STORE_TRACE((a), (n))
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
static int32_t s16(Run *r, uint32_t a) { return (int32_t)(int16_t)rd(r, a, 2); }
static uint32_t w32(Run *r, uint32_t a) { return rd(r, a, 4); }
static void st8(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 1); }
static void st32(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 4); }

/* 32-bit value as the EE holds it in a 64-bit register (an address formed
 * from a register plus an offset is the sign-extended 32-bit sum). */
static uint64_t reg(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

/* ------------------------------------------------------------------------
 * Calls leaving the module
 * ---------------------------------------------------------------------- */

/* One call: `na` integer argument registers from a[], `nf` float argument
 * registers from f[]; returns the record with v0 / f0 filled in. */
static EmArea01RoomCall call(Run *r, uint32_t sp, uint32_t fn, unsigned na, const uint64_t *a, unsigned nf,
                             const uint32_t *f)
{
    EmArea01RoomCall c;
    unsigned i;
    memset(&c, 0, sizeof c);
    c.fn = fn;
    c.sp = sp;
    c.na = na;
    c.nf = nf;
    for (i = 0; i < na && i < 8; i++)
        c.a[i] = a[i];
    for (i = 0; i < nf && i < 4; i++)
        c.f[i] = f[i];
    if (!r->s->call)
        fault(r, EM_AREA01_ROOM_FAULT_NULL, fn);
    if (r->s->call(r->s->ctx, &c) < 0)
        fault(r, EM_AREA01_ROOM_FAULT_WORKER, fn);
    return c;
}

/* Integer-argument call with up to three arguments; the whole v0 image. */
static uint64_t calli(Run *r, uint32_t sp, uint32_t fn, unsigned na, uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint64_t a[3];
    a[0] = reg(a0);
    a[1] = reg(a1);
    a[2] = reg(a2);
    return call(r, sp, fn, na, a, 0, NULL).v0;
}

#define F_ONE       0x3F800000u /* 1.0 */
#define F_THREE     0x40400000u /* 3.0 */
#define F_FIVE      0x40A00000u /* 5.0 */
#define F_FIFTH     0x3E4CCCCDu /* 0.2 as the original loads it */
#define F_MINUS9    0xC1100000u /* -9.0 */
#define F_MINUS13   0xC1500000u /* -13.0 */

#define FN_COPY_QW      0x00102948u /* one quadword copy (dst, src) */
#define FN_FLOAT_TO_INT 0x001281C0u /* float_to_int(f12) -> v0 */

#define D_00275B40 0x00275B40u /* the current bone pointer array */
#define D_002754D8 0x002754D8u
#define D_008106B8 0x008106B8u
#define D_008105D0 0x008105D0u
#define D_008105E0 0x008105E0u
#define D_00810040 0x00810040u
#define SPR_3400   0x70003400u
#define SPR_3600   0x70003600u

/* The +0x7C word of bone `slot` of the array *D_00275B40 (both words read
 * again at every use, as the original does). */
static uint32_t panel(Run *r, unsigned slot)
{
    return w32(r, w32(r, D_00275B40) + 4u * slot) + 0x7Cu;
}

/* ------------------------------------------------------------------------
 * 00188610(a0): the halfword at D_002754D8 + 2 * (byte a0+0x235 & 1),
 * sign-extended.
 * ---------------------------------------------------------------------- */
static int32_t f_188610(Run *r, uint32_t a0)
{
    return s16(r, D_002754D8 + 2u * (u8(r, a0 + 0x235u) & 1u));
}

/* ------------------------------------------------------------------------
 * 00198D90(a0, a1), 0x30-byte frame. In order:
 *   00102948(a0+0x10, a1+0xA0);
 *   the float a0+0x14 = its value + 3.0 (stored before the next call);
 *   001029C0(0x70003400);
 *   00102C58(0x70003400, 0x70003400, a1+0xC0);
 *   the scratchpad words 0x70003600, 604, 608, 60C = 0, 0, 5.0, 0 (in that
 *   order, all before the next call);
 *   001026A0(a0+0x20, 0x70003400, 0x70003600);
 *   001028B8(a0+0x20, a0+0x20, a0+0x10);
 * then the byte a0+1 (read after that call):
 *   0: a0+1 = 1, a0+2 = 0, 00102948(D_008105E0, a0+0x20),
 *      00102948(D_008105D0, a0+0x10);
 *   1: when the word a1+0x230 is 0x12: a0+6 = 0x0B, then a0+1 = 0. Then
 *      0018C4B0(D_008105E0; f12 = the float a0+0x24, f13 = 0.2),
 *      0018C6A0(a0+0x20, D_008105E0; f12 = 0.2),
 *      00102948(D_008105D0, a0+0x10);
 *   any other value: nothing more.
 * No result is defined.
 * ---------------------------------------------------------------------- */
static void f_198D90(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    uint32_t st;
    uint64_t a[2];
    uint32_t f[2];
    sp -= 0x30;
    calli(r, sp, FN_COPY_QW, 2, a0 + 0x10u, a1 + 0xA0u, 0);
    st32(r, a0 + 0x14u, em_ee_add_bits(w32(r, a0 + 0x14u), F_THREE));
    calli(r, sp, 0x001029C0u, 1, SPR_3400, 0, 0);
    calli(r, sp, 0x00102C58u, 3, SPR_3400, SPR_3400, a1 + 0xC0u);
    st32(r, SPR_3600 + 0x0u, 0);
    st32(r, SPR_3600 + 0x4u, 0);
    st32(r, SPR_3600 + 0x8u, F_FIVE);
    st32(r, SPR_3600 + 0xCu, 0);
    calli(r, sp, 0x001026A0u, 3, a0 + 0x20u, SPR_3400, SPR_3600);
    calli(r, sp, 0x001028B8u, 3, a0 + 0x20u, a0 + 0x20u, a0 + 0x10u);
    st = u8(r, a0 + 1u);
    if (st == 0) {
        st8(r, a0 + 1u, 1);
        st8(r, a0 + 2u, 0);
        calli(r, sp, FN_COPY_QW, 2, D_008105E0, a0 + 0x20u, 0);
        calli(r, sp, FN_COPY_QW, 2, D_008105D0, a0 + 0x10u, 0);
        return;
    }
    if (st != 1)
        return;
    if (w32(r, a1 + 0x230u) == 0x12u) {
        st8(r, a0 + 6u, 0x0B);
        st8(r, a0 + 1u, 0);
    }
    a[0] = reg(D_008105E0);
    f[0] = w32(r, a0 + 0x24u);
    f[1] = F_FIFTH;
    call(r, sp, 0x0018C4B0u, 1, a, 2, f);
    a[0] = reg(a0 + 0x20u);
    a[1] = reg(D_008105E0);
    f[0] = F_FIFTH;
    call(r, sp, 0x0018C6A0u, 2, a, 1, f);
    calli(r, sp, FN_COPY_QW, 2, D_008105D0, a0 + 0x10u, 0);
}

/* ------------------------------------------------------------------------
 * 001BB400(a0): by the byte a0+3 (the kind):
 *   8 or 0x16: panel 0's float = its value - 0.2; the result is 1 when
 *     panel 0's float (read again) is below -9.0 (EE compare), else 0;
 *   any other kind: panel 1's float = its value - 0.2, then panel 2's float
 *     = its value + 0.2; the kind is read again: for 0x3E or 0x3D the result
 *     is 1 when panel 1's float is below -13.0, otherwise when it is below
 *     -9.0; else 0.
 * A panel is the +0x7C float of the bone record (*D_00275B40)[slot], the
 * array pointer and the bone pointer read again at every access.
 * ---------------------------------------------------------------------- */
static int32_t f_1BB400(Run *r, uint32_t a0)
{
    uint32_t kind = u8(r, a0 + 3u), p, limit;
    if (kind == 8 || kind == 0x16) {
        p = panel(r, 0);
        st32(r, p, em_ee_sub_bits(w32(r, p), F_FIFTH));
        return em_ee_c_lt_bits(w32(r, panel(r, 0)), F_MINUS9) ? 1 : 0;
    }
    p = panel(r, 1);
    st32(r, p, em_ee_sub_bits(w32(r, p), F_FIFTH));
    p = panel(r, 2);
    st32(r, p, em_ee_add_bits(w32(r, p), F_FIFTH));
    kind = u8(r, a0 + 3u);
    limit = (kind == 0x3E || kind == 0x3D) ? F_MINUS13 : F_MINUS9;
    return em_ee_c_lt_bits(w32(r, panel(r, 1)), limit) ? 1 : 0;
}

/* ------------------------------------------------------------------------
 * 001BB7C0(a0), 0x10-byte frame: 001BA1F0(a0); the result is 1 when its
 * whole v0 register is non-zero, else 0.
 * ---------------------------------------------------------------------- */
static int32_t f_1BB7C0(Run *r, uint32_t sp, uint32_t a0)
{
    return calli(r, sp - 0x10u, 0x001BA1F0u, 1, a0, 0, 0) != 0 ? 1 : 0;
}

/* ------------------------------------------------------------------------
 * 001BB7F0(a0): when the byte D_008106B8 is non-zero the result is 0 and
 * nothing is written. Otherwise, by the byte a0+3: for 8 or 0x16 panel 0's
 * word = 0; for any other kind panel 1's word = 0, then panel 2's word = 0.
 * Then the byte a0+0x0B = 0 and the result is 1.
 * ---------------------------------------------------------------------- */
static int32_t f_1BB7F0(Run *r, uint32_t a0)
{
    uint32_t kind;
    if (u8(r, D_008106B8) != 0)
        return 0;
    kind = u8(r, a0 + 3u);
    if (kind == 8 || kind == 0x16) {
        st32(r, panel(r, 0), 0);
    } else {
        st32(r, panel(r, 1), 0);
        st32(r, panel(r, 2), 0);
    }
    st8(r, a0 + 0x0Bu, 0);
    return 1;
}

/* ------------------------------------------------------------------------
 * 001D0D60(a0; f12), 0x60-byte frame. Fields of a0: +0 the row table
 * (7 floats = 28 bytes a row), +4 the length L, +8 the time t, byte +0x0C
 * the loop flag, +0x40..+0x58 seven outputs. EE arithmetic throughout.
 *   t = t + f12 (stored); result code c = 0.
 *   Flag set: while !(t < L): t = t - L (stored; c = 0x1000), t and L read
 *     again each round. n = float_to_int(1.0 + t); next = n when
 *     (float)n < L, else 0.
 *   Flag clear: when !(t < L): t = L - 1.0 (stored; c = 0x1000). Then
 *     n = float_to_int(1.0 + t) (t read again); L is read again; next = n
 *     when (float)n < L, else next = float_to_int(L - 1.0).
 *   Then t is read again: frac = t - (float)float_to_int(t), w = 1.0 - frac,
 *   i = float_to_int(t); the table word +0 is read between those two calls.
 *   For k = 0..6: output k = w * row[i][k] + frac * row[next][k] (the
 *   product w * row[i][k] into ACC, then ACC + frac * row[next][k]); both
 *   row words are loaded before output k is stored.
 *   The result is c.
 * ---------------------------------------------------------------------- */
static int32_t call_f2i(Run *r, uint32_t sp, uint32_t x)
{
    return (int32_t)(uint32_t)call(r, sp, FN_FLOAT_TO_INT, 0, NULL, 1, &x).v0;
}

static int32_t f_1D0D60(Run *r, uint32_t sp, uint32_t a0, uint32_t f12)
{
    int32_t code = 0, next, n, i;
    uint32_t t, len, frac, w, table, lo, hi, acc;
    unsigned k;
    sp -= 0x60;
    st32(r, a0 + 8u, em_ee_add_bits(w32(r, a0 + 8u), f12));
    if (u8(r, a0 + 0x0Cu) != 0) {
        t = w32(r, a0 + 8u);
        for (;;) {
            len = w32(r, a0 + 4u);
            if (em_ee_c_lt_bits(t, len))
                break;
            code = 0x1000;
            st32(r, a0 + 8u, em_ee_sub_bits(w32(r, a0 + 8u), len));
            t = w32(r, a0 + 8u);
        }
        n = call_f2i(r, sp, em_ee_add_bits(F_ONE, t));
        next = em_ee_c_lt_bits(em_ee_cvt_s_w_bits((uint32_t)n), len) ? n : 0;
    } else {
        t = w32(r, a0 + 8u);
        len = w32(r, a0 + 4u);
        if (!em_ee_c_lt_bits(t, len)) {
            code = 0x1000;
            st32(r, a0 + 8u, em_ee_sub_bits(len, F_ONE));
        }
        n = call_f2i(r, sp, em_ee_add_bits(F_ONE, w32(r, a0 + 8u)));
        len = w32(r, a0 + 4u);
        next = em_ee_c_lt_bits(em_ee_cvt_s_w_bits((uint32_t)n), len) ? n
                                                                       : call_f2i(r, sp, em_ee_sub_bits(len, F_ONE));
    }
    t = w32(r, a0 + 8u);
    n = call_f2i(r, sp, t);
    table = w32(r, a0);
    frac = em_ee_sub_bits(t, em_ee_cvt_s_w_bits((uint32_t)n));
    w = em_ee_sub_bits(F_ONE, frac);
    i = call_f2i(r, sp, t);
    lo = table + (uint32_t)i * 28u;
    hi = table + (uint32_t)next * 28u;
    for (k = 0; k < 7; k++) {
        acc = em_ee_mula_bits(w, w32(r, lo + 4u * k));
        st32(r, a0 + 0x40u + 4u * k, em_ee_madd_bits(acc, frac, w32(r, hi + 4u * k)));
    }
    return code;
}

/* ------------------------------------------------------------------------
 * 00225A00: 00121A28(D_00810040, 0, 0xD4), entered by a jump (no frame of
 * its own: the callee gets the entry stack pointer). The callee's v0 is
 * left as the routine's; no result is defined.
 * ---------------------------------------------------------------------- */
static void f_225A00(Run *r, uint32_t sp)
{
    calli(r, sp, 0x00121A28u, 3, D_00810040, 0, 0xD4);
}

/* ======================================================================== */
/* Public entries                                                            */
/* ======================================================================== */

#define ENTER(s, address)                                                   \
    Run run_;                                                               \
    Run *r = &run_;                                                         \
    if (!(s))                                                               \
        return -1;                                                          \
    if ((s)->fault != EM_AREA01_ROOM_FAULT_NONE)                            \
        return -1;                                                          \
    run_.s = (s);                                                           \
    if (setjmp(run_.out)) {                                                 \
        if ((s)->fault_function == 0)                                       \
            (s)->fault_function = (address);                                \
        return -1;                                                          \
    }

static int out_null(EmArea01Room *s, uint32_t address)
{
    s->fault = EM_AREA01_ROOM_FAULT_NULL;
    s->fault_function = address;
    s->fault_address = 0;
    return -1;
}

int em_area01_room_00188610(EmArea01Room *s, uint32_t a0, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x00188610u);
    if (!out)
        return out_null(s, 0x00188610u);
    v = f_188610(r, a0);
    *out = v;
    return 0;
}

int em_area01_room_00198D90(EmArea01Room *s, uint32_t a0, uint32_t a1)
{
    ENTER(s, 0x00198D90u);
    f_198D90(r, s->sp, a0, a1);
    return 0;
}

int em_area01_room_001BB400(EmArea01Room *s, uint32_t a0, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x001BB400u);
    if (!out)
        return out_null(s, 0x001BB400u);
    v = f_1BB400(r, a0);
    *out = v;
    return 0;
}

int em_area01_room_001BB7C0(EmArea01Room *s, uint32_t a0, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x001BB7C0u);
    if (!out)
        return out_null(s, 0x001BB7C0u);
    v = f_1BB7C0(r, s->sp, a0);
    *out = v;
    return 0;
}

int em_area01_room_001BB7F0(EmArea01Room *s, uint32_t a0, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x001BB7F0u);
    if (!out)
        return out_null(s, 0x001BB7F0u);
    v = f_1BB7F0(r, a0);
    *out = v;
    return 0;
}

int em_area01_room_001D0D60(EmArea01Room *s, uint32_t a0, uint32_t f12, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x001D0D60u);
    if (!out)
        return out_null(s, 0x001D0D60u);
    v = f_1D0D60(r, s->sp, a0, f12);
    *out = v;
    return 0;
}

int em_area01_room_00225A00(EmArea01Room *s)
{
    ENTER(s, 0x00225A00u);
    f_225A00(r, s->sp);
    return 0;
}
