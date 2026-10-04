/* em_area01_side.c - AREA01 lane SIDE translations (see em_area01_side.h and
 * docs/AREA01_SIDE.md). Every routine names its original address; the
 * comments say what the original does, never how it is encoded. */
#include "em_area01_side.h"

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#include "em_ee_float.h"

/* ------------------------------------------------------------------------
 * Fail-stop plumbing
 * ---------------------------------------------------------------------- */

typedef struct {
    EmArea01Side *s;
    jmp_buf out;
} Run;

static void fault(Run *r, int32_t code, uint32_t address)
{
    if (r->s->fault == EM_AREA01_SIDE_FAULT_NONE) {
        r->s->fault = code;
        r->s->fault_address = address;
    }
    longjmp(r->out, 1);
}

void em_area01_side_clear_fault(EmArea01Side *s)
{
    if (!s)
        return;
    s->fault = EM_AREA01_SIDE_FAULT_NONE;
    s->fault_function = 0;
    s->fault_address = 0;
}

/* The byte span [address, address + size) inside one region, or a fault. */
static uint8_t *span(Run *r, uint32_t address, uint32_t size, int write)
{
    const EmArea01Side *s = r->s;
    if(!size || (uint64_t)address+size>UINT64_C(0x100000000))
        fault(r,EM_AREA01_SIDE_FAULT_UNMAPPED,address);
    if(s->view) {
        uint8_t *p=s->view(s->ctx,address,size,write);
        if(p)return p;
        fault(r,EM_AREA01_SIDE_FAULT_UNMAPPED,address);
    }
    unsigned i;
    for (i = 0; i < s->region_count; i++) {
        const EmArea01SideRegion *g = &s->regions[i];
        if (g->bytes && address >= g->base && size <= g->size && address - g->base <= g->size - size)
            return g->bytes + (address - g->base);
    }
    fault(r, EM_AREA01_SIDE_FAULT_UNMAPPED, address);
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

/* Test hook. A build that defines EM_AREA01_SIDE_STORE_TRACE as a function
 * name reports every store (original address, size) to it once the address
 * is known to be mapped; tools/test_area01_side_reference.py uses it to
 * compare memory with the original at every call leaving the module.
 * Ordinary builds compile it out. wr is the only store. */
#ifdef EM_AREA01_SIDE_STORE_TRACE
void EM_AREA01_SIDE_STORE_TRACE(uint32_t address, unsigned size);
#define TRACE_STORE(a, n) EM_AREA01_SIDE_STORE_TRACE((a), (n))
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
static void st16(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 2); }
static void st32(Run *r, uint32_t a, uint32_t v) { wr(r, a, v, 4); }

/* 32-bit value as the EE holds it in a 64-bit register. */
static uint64_t reg(int32_t v) { return (uint64_t)(int64_t)v; }

/* ------------------------------------------------------------------------
 * Calls leaving the module
 * ---------------------------------------------------------------------- */

/* One call: `na` integer argument registers from a[], `nf` float argument
 * registers from f[]; returns the record with v0 / f0 filled in. */
static EmArea01SideCall call(Run *r, uint32_t sp, uint32_t fn, unsigned na, const uint64_t *a, unsigned nf,
                             const uint32_t *f)
{
    EmArea01SideCall c;
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
        fault(r, EM_AREA01_SIDE_FAULT_NULL, fn);
    if (r->s->call(r->s->ctx, &c) < 0)
        fault(r, EM_AREA01_SIDE_FAULT_WORKER, fn);
    return c;
}

/* Integer-argument call; returns the whole v0 register image. */
static uint64_t call2(Run *r, uint32_t sp, uint32_t fn, unsigned na, uint64_t a0, uint64_t a1)
{
    uint64_t a[2];
    a[0] = a0;
    a[1] = a1;
    return call(r, sp, fn, na, a, 0, NULL).v0;
}

static uint64_t call3(Run *r, uint32_t sp, uint32_t fn, uint64_t a0, uint64_t a1, uint64_t a2)
{
    uint64_t a[3];
    a[0] = a0;
    a[1] = a1;
    a[2] = a2;
    return call(r, sp, fn, 3, a, 0, NULL).v0;
}

/* 0021B9A0(channel; f12 = x, f13 = y): the request both 001F0190 and
 * 001F0290 make. */
static void channel(Run *r, uint32_t sp, int32_t which, uint32_t x, uint32_t y)
{
    uint64_t a[1];
    uint32_t f[2];
    a[0] = reg(which);
    f[0] = x;
    f[1] = y;
    call(r, sp, 0x0021B9A0u, 1, a, 2, f);
}

#define F_ZERO 0x00000000u
#define F_ONE  0x3F800000u
#define F_TEN  0x41200000u

#define FN_COPY_QW 0x00102948u /* one quadword copy (run as original code by the test) */

/* ------------------------------------------------------------------------
 * 001AF7C0: when the signed halfword D_00275BCC is positive, it is
 * decremented, the word pointer D_00275BD0 advances by 4, and the result is
 * the word at the old pointer, read after both stores. Otherwise the result
 * is 0 and nothing is written.
 * ---------------------------------------------------------------------- */
static int32_t f_1AF7C0(Run *r)
{
    int32_t count = s16(r, 0x00275BCCu);
    uint32_t p;
    if (count <= 0)
        return 0;
    p = w32(r, 0x00275BD0u);
    st16(r, 0x00275BCCu, (uint32_t)(count - 1));
    st32(r, 0x00275BD0u, p + 4);
    return (int32_t)w32(r, p);
}

/* ------------------------------------------------------------------------
 * 001C4720(a0, a1): the byte at D_00810CB8 + a0 (32-bit sum) becomes its
 * value plus a1 (low 8 bits), then D_008106B0 = 2 and D_008106B1 = the low
 * byte of a0, in that order. The result is 0.
 * ---------------------------------------------------------------------- */
static int32_t f_1C4720(Run *r, int32_t a0, int32_t a1)
{
    uint32_t at = 0x00810CB8u + (uint32_t)a0;
    st8(r, at, u8(r, at) + (uint32_t)a1);
    st8(r, 0x008106B0u, 2);
    st8(r, 0x008106B1u, (uint32_t)a0);
    return 0;
}

/* ------------------------------------------------------------------------
 * 001CB480(a0): in order 001D2910(0) (its whole v0 kept), 001D8C20(2),
 * 001D2830(0, 0), 001C7420(a0, 0x3F5, 1), 001D3BA0(1, the word at a0+0x44,
 * read after 001C7420 returns), and 001D2830(0, the kept 001D2910 v0).
 * No result is defined (v0 is what the last callee left).
 * ---------------------------------------------------------------------- */
static void f_1CB480(Run *r, uint32_t sp, uint32_t a0)
{
    uint64_t kept;
    sp -= 0x30;
    kept = call2(r, sp, 0x001D2910u, 1, 0, 0);
    call2(r, sp, 0x001D8C20u, 1, 2, 0);
    call2(r, sp, 0x001D2830u, 2, 0, 0);
    call3(r, sp, 0x001C7420u, reg((int32_t)a0), 0x3F5, 1);
    call2(r, sp, 0x001D3BA0u, 2, 1, reg((int32_t)w32(r, a0 + 0x44)));
    call2(r, sp, 0x001D2830u, 2, 0, kept);
}

/* ------------------------------------------------------------------------
 * 001EFE00(a0, a1): a point local p (at sp+0x30 of its 0x40-byte frame) =
 * the quadword at a1+0xB0 (00102948). For a0 == 0x80000027 the float
 * p.y (+4) becomes p.y + 10.0. Then r = 001EF9D0(a0, &p; f12 = 1.0). When
 * the whole r register is non-zero: the word a1+0x14 is read and stored at
 * r+0x24, then the quadwords a1+0xB0 -> r+0xB0 and a1+0xC0 -> r+0xC0
 * (00102948 each). The result is r.
 * ---------------------------------------------------------------------- */
static uint64_t f_1EFE00(Run *r, uint32_t sp, int32_t a0, uint32_t a1)
{
    uint32_t p, rp, word;
    uint64_t a[2], got;
    uint32_t f[1];
    sp -= 0x40;
    p = sp + 0x30;
    call2(r, sp, FN_COPY_QW, 2, reg((int32_t)p), reg((int32_t)(a1 + 0xB0)));
    if (a0 == (int32_t)0x80000027u)
        st32(r, p + 4, em_ee_add_bits(w32(r, p + 4), F_TEN));
    a[0] = reg(a0);
    a[1] = reg((int32_t)p);
    f[0] = F_ONE;
    got = call(r, sp, 0x001EF9D0u, 2, a, 1, f).v0;
    if (got == 0)
        return got;
    rp = (uint32_t)got;
    word = w32(r, a1 + 0x14);
    st32(r, rp + 0x24, word);
    call2(r, sp, FN_COPY_QW, 2, reg((int32_t)(rp + 0xB0)), reg((int32_t)(a1 + 0xB0)));
    call2(r, sp, FN_COPY_QW, 2, reg((int32_t)(rp + 0xC0)), reg((int32_t)(a1 + 0xC0)));
    return got;
}

/* ------------------------------------------------------------------------
 * 001E3D20(a0, a1): nothing when bit 1 (value 2) of the byte a1+0 is set.
 * Otherwise 0021BB00(D_008102B0); when its whole v0 is non-zero, nothing
 * more. Otherwise 001EFE00(0x80000027, a1), then the byte a1+0x0F = 0x0C
 * and the word a0+0x1F0 = 0x3C.
 * ---------------------------------------------------------------------- */
static void f_1E3D20(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    sp -= 0x30;
    if (u8(r, a1) & 2)
        return;
    if (call2(r, sp, 0x0021BB00u, 1, reg((int32_t)0x008102B0u), 0) != 0)
        return;
    f_1EFE00(r, sp, (int32_t)0x80000027u, a1);
    st8(r, a1 + 0x0F, 0x0C);
    st32(r, a0 + 0x1F0, 0x3C);
}

/* ------------------------------------------------------------------------
 * 001F0190(f12, f13): key = D_00810700 * 256 + D_00810701 (bytes), and
 * D_00275C3C = 0.
 *  - key 0x1600 or 0x800: 0021B9A0(2; 1.0, 10.0), 0021B9A0(3; 1.0, 10.0),
 *    then D_00275C3C (read again) + 1.
 *  - any other key: unless the float at (*D_00275670)+0xB8 <= f12 (EE
 *    compare): 0021B9A0(2; 0.0, f12) and D_00275C3C + 1. Then D_00275670 is
 *    read again and, unless its +0xBC <= f13: 0021B9A0(3; 0.0, f13) and
 *    D_00275C3C + 1.
 * ---------------------------------------------------------------------- */
static void f_1F0190(Run *r, uint32_t sp, uint32_t f12, uint32_t f13)
{
    uint32_t key;
    sp -= 0x20;
    key = (u8(r, 0x00810700u) << 8) + u8(r, 0x00810701u);
    st32(r, 0x00275C3Cu, 0);
    if (key == 0x1600 || key == 0x800) {
        channel(r, sp, 2, F_ONE, F_TEN);
        channel(r, sp, 3, F_ONE, F_TEN);
        st32(r, 0x00275C3Cu, w32(r, 0x00275C3Cu) + 1);
        return;
    }
    if (!em_ee_c_le_bits(w32(r, w32(r, 0x00275670u) + 0xB8), f12)) {
        channel(r, sp, 2, F_ZERO, f12);
        st32(r, 0x00275C3Cu, w32(r, 0x00275C3Cu) + 1);
    }
    if (!em_ee_c_le_bits(w32(r, w32(r, 0x00275670u) + 0xBC), f13)) {
        channel(r, sp, 3, F_ZERO, f13);
        st32(r, 0x00275C3Cu, w32(r, 0x00275C3Cu) + 1);
    }
}

/* ------------------------------------------------------------------------
 * 001F0290: 0021B9A0(1; 0.0, 0.0) when the word D_00275C3C is non-zero.
 * ---------------------------------------------------------------------- */
static void f_1F0290(Run *r, uint32_t sp)
{
    sp -= 0x10;
    if (w32(r, 0x00275C3Cu) != 0)
        channel(r, sp, 1, F_ZERO, F_ZERO);
}

/* ======================================================================== */
/* Public entries                                                            */
/* ======================================================================== */

#define ENTER(s, address)                                                   \
    Run run_;                                                               \
    Run *r = &run_;                                                         \
    if (!(s))                                                               \
        return -1;                                                          \
    if ((s)->fault != EM_AREA01_SIDE_FAULT_NONE)                            \
        return -1;                                                          \
    run_.s = (s);                                                           \
    if (setjmp(run_.out)) {                                                 \
        if ((s)->fault_function == 0)                                       \
            (s)->fault_function = (address);                                \
        return -1;                                                          \
    }

static int out_null(EmArea01Side *s, uint32_t address)
{
    s->fault = EM_AREA01_SIDE_FAULT_NULL;
    s->fault_function = address;
    s->fault_address = 0;
    return -1;
}

int em_area01_side_001AF7C0(EmArea01Side *s, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x001AF7C0u);
    if (!out)
        return out_null(s, 0x001AF7C0u);
    v = f_1AF7C0(r);
    *out = v;
    return 0;
}

int em_area01_side_001C4720(EmArea01Side *s, int32_t a0, int32_t a1, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x001C4720u);
    if (!out)
        return out_null(s, 0x001C4720u);
    v = f_1C4720(r, a0, a1);
    *out = v;
    return 0;
}

int em_area01_side_001CB480(EmArea01Side *s, uint32_t a0)
{
    ENTER(s, 0x001CB480u);
    f_1CB480(r, s->sp, a0);
    return 0;
}

int em_area01_side_001E3D20(EmArea01Side *s, uint32_t a0, uint32_t a1)
{
    ENTER(s, 0x001E3D20u);
    f_1E3D20(r, s->sp, a0, a1);
    return 0;
}

int em_area01_side_001EFE00(EmArea01Side *s, int32_t a0, uint32_t a1, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x001EFE00u);
    if (!out)
        return out_null(s, 0x001EFE00u);
    v = (int32_t)(uint32_t)f_1EFE00(r, s->sp, a0, a1);
    *out = v;
    return 0;
}

int em_area01_side_001F0190(EmArea01Side *s, uint32_t f12, uint32_t f13)
{
    ENTER(s, 0x001F0190u);
    f_1F0190(r, s->sp, f12, f13);
    return 0;
}

int em_area01_side_001F0290(EmArea01Side *s)
{
    ENTER(s, 0x001F0290u);
    f_1F0290(r, s->sp);
    return 0;
}
