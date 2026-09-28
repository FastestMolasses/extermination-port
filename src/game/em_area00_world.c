/* em_area00_world.c - AREA00 lane A00WORLD translations (see
 * em_area00_world.h and docs/AREA00_WORLD.md). Every routine names its
 * original address; the comments say what the original does, never how it
 * is encoded. */
#include "em_area00_world.h"

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#include "em_ee_float.h"

/* ------------------------------------------------------------------------
 * Fail-stop plumbing
 * ---------------------------------------------------------------------- */

typedef struct {
    EmArea00World *s;
    uint32_t entry; /* the entry this run serves (fault_function) */
    jmp_buf out;
} Run;

/* Latch the first fault only: the code, entry and address together. A
 * worker that re-enters the module with the same context and faults there
 * has latched first; the outer entry then keeps that record. */
static void fault(Run *r, int32_t code, uint32_t address)
{
    if (r->s->fault == EM_AREA00_WORLD_FAULT_NONE) {
        r->s->fault = code;
        r->s->fault_function = r->entry;
        r->s->fault_address = address;
    }
    longjmp(r->out, 1);
}

void em_area00_world_clear_fault(EmArea00World *s)
{
    if (!s)
        return;
    s->fault = EM_AREA00_WORLD_FAULT_NONE;
    s->fault_function = 0;
    s->fault_address = 0;
}

/* The byte span [address, address + size) inside one region, or a fault. */
static uint8_t *span(Run *r, uint32_t address, uint32_t size)
{
    const EmArea00World *s = r->s;
    unsigned i;
    for (i = 0; i < s->region_count; i++) {
        const EmArea00WorldRegion *g = &s->regions[i];
        if (g->bytes && address >= g->base && size <= g->size && address - g->base <= g->size - size)
            return g->bytes + (address - g->base);
    }
    fault(r, EM_AREA00_WORLD_FAULT_UNMAPPED, address);
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

/* Test hook. A build that defines EM_AREA00_WORLD_STORE_TRACE as a function
 * name reports every store (original address, size) to it once the address
 * is known to be mapped; tools/test_area00_world_reference.py uses it to
 * compare memory with the original at every call leaving the module.
 * Ordinary builds compile it out. wr is the only store path. */
#ifdef EM_AREA00_WORLD_STORE_TRACE
void EM_AREA00_WORLD_STORE_TRACE(uint32_t address, unsigned size);
#define TRACE_STORE(a, n) EM_AREA00_WORLD_STORE_TRACE((a), (n))
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

/* 32-bit value as the EE holds it in a 64-bit register. */
static uint64_t reg(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

/* ------------------------------------------------------------------------
 * Calls leaving the module
 * ---------------------------------------------------------------------- */

/* One call: `na` integer registers (a0..t3 images in a[]) and `nf` float
 * registers (f12.. bits in f[]). Returns the call record (v0 / f0). */
static EmArea00WorldCall callv(Run *r, uint32_t sp, uint32_t fn, unsigned na, const uint64_t *a, unsigned nf,
                               const uint32_t *f)
{
    EmArea00WorldCall c;
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
        fault(r, EM_AREA00_WORLD_FAULT_NULL, fn);
    if (r->s->call(r->s->ctx, &c) < 0)
        fault(r, EM_AREA00_WORLD_FAULT_WORKER, fn);
    return c;
}

/* Integer calls with 32-bit arguments; the result is the full v0. */
static uint64_t call0(Run *r, uint32_t sp, uint32_t fn)
{
    return callv(r, sp, fn, 0, NULL, 0, NULL).v0;
}
static uint64_t call1(Run *r, uint32_t sp, uint32_t fn, uint32_t a)
{
    uint64_t x[1];
    x[0] = reg(a);
    return callv(r, sp, fn, 1, x, 0, NULL).v0;
}
static uint64_t call2(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b)
{
    uint64_t x[2];
    x[0] = reg(a);
    x[1] = reg(b);
    return callv(r, sp, fn, 2, x, 0, NULL).v0;
}
static uint64_t call3(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b, uint32_t c)
{
    uint64_t x[3];
    x[0] = reg(a);
    x[1] = reg(b);
    x[2] = reg(c);
    return callv(r, sp, fn, 3, x, 0, NULL).v0;
}
static uint64_t call4(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint64_t x[4];
    x[0] = reg(a);
    x[1] = reg(b);
    x[2] = reg(c);
    x[3] = reg(d);
    return callv(r, sp, fn, 4, x, 0, NULL).v0;
}
/* (a0, a1; f12) */
static void call2f(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b, uint32_t f12)
{
    uint64_t x[2];
    x[0] = reg(a);
    x[1] = reg(b);
    (void)callv(r, sp, fn, 2, x, 1, &f12);
}
/* (a0, a1, a2; f12) */
static void call3f(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b, uint32_t c, uint32_t f12)
{
    uint64_t x[3];
    x[0] = reg(a);
    x[1] = reg(b);
    x[2] = reg(c);
    (void)callv(r, sp, fn, 3, x, 1, &f12);
}
/* float(f12) -> f0 */
static uint32_t fcall1(Run *r, uint32_t sp, uint32_t fn, uint32_t x)
{
    return callv(r, sp, fn, 0, NULL, 1, &x).f0;
}

/* ------------------------------------------------------------------------
 * Constants, callees and globals
 * ---------------------------------------------------------------------- */

#define F_ZERO   0x00000000u
#define F_ONE    0x3F800000u
#define F_TWO    0x40000000u
#define F_HALF   0x3F000000u
#define F_PI     0x40490FDBu
#define F_47     0x423C0000u /* 47.0 */
#define F_255    0x437F0000u /* 255.0 */
#define F_300    0x43960000u /* 300.0 */
#define F_CENTI  0x3C23D70Au /* 0.01 */

#define FN_1000C0 0x001000C0u /* (a0, a1): 64-bit float compare, 1 when a0 < a1 */
#define FN_102760 0x00102760u /* (out, in) normalise */
#define FN_102948 0x00102948u /* (dst, src) one quadword */
#define FN_102958 0x00102958u /* (dst, src) four quadwords */
#define FN_1026A0 0x001026A0u /* (out, matrix, vector) */
#define FN_1028B8 0x001028B8u /* (out, a, b) a + b */
#define FN_1028D0 0x001028D0u /* (out, a, b) a - b */
#define FN_1029C0 0x001029C0u /* (matrix) identity */
#define FN_1031E0 0x001031E0u /* (dst, src) */
#define FN_103230 0x00103230u /* (out, in; f12) scale */
#define FN_11DE90 0x0011DE90u /* float(f12) */
#define FN_11DF78 0x0011DF78u /* float(f12) */
#define FN_11E2A8 0x0011E2A8u /* float(f12) */
#define FN_RAND   0x00122BB8u /* the LCG draw */
#define FN_128350 0x00128350u /* float(f12) -> 64-bit v0 */
#define FN_19AD00 0x0019AD00u
#define FN_19CB60 0x0019CB60u
#define FN_19FE50 0x0019FE50u
#define FN_1A7280 0x001A7280u
#define FN_1B1470 0x001B1470u /* float(f12) */
#define FN_1B61C0 0x001B61C0u
#define FN_1C9610 0x001C9610u
#define FN_1D7FA0 0x001D7FA0u
#define FN_1EFD20 0x001EFD20u
#define FN_1EFD90 0x001EFD90u
#define FN_1F4F40 0x001F4F40u
#define FN_1FBD50 0x001FBD50u
#define FN_1FC580 0x001FC580u

#define G_AREA   0x00810700u /* area byte; 0x810701 the sub byte */
#define G_SUB    0x00810701u
#define G_810350 0x00810350u /* a vector (001B41F0 subtracts the victim from it) */
#define G_810354 0x00810354u
#define G_810374 0x00810374u
#define G_8102B0 0x008102B0u
#define G_24D7C0 0x0024D7C0u /* per-area pointers to per-sub record-list pointers */
#define G_24D6B0 0x0024D6B0u /* four 16-byte vectors */

#define SPR(x) (0x70000000u + (x))

/* ------------------------------------------------------------------------
 * 00189EC0(a0): by the type byte a0+3:
 *   0x51 -> 2;
 *   0x30, 0x4F, 0x46, 0x1F, 0x1E, 0x50, 0x1C, 0x06 -> 1;
 *   0x0E -> t = 0011DF78(001B1470(D_00810374 - float a0+0xC4)), stored at
 *           0x70003A20; then 001000C0(00128350(t), the 64-bit pattern of
 *           pi/2 widened from single precision, 0x3FF921FB60000000): a
 *           nonzero result gives 0, zero gives 3. 00128350's whole 64-bit
 *           v0 is 001000C0's first argument; 001000C0's whole v0 is tested;
 *   anything else -> 0.
 * The frame is 0x10 bytes.
 * ---------------------------------------------------------------------- */
static int32_t f_189EC0(Run *r, uint32_t sp, uint32_t a0)
{
    uint32_t k, yaw, t;
    uint64_t x[2];
    uint64_t d;
    sp -= 0x10;
    k = u8(r, a0 + 3);
    switch (k) {
    case 0x51:
        return 2;
    case 0x30:
    case 0x4F:
    case 0x46:
    case 0x1F:
    case 0x1E:
    case 0x50:
    case 0x1C:
    case 0x06:
        return 1;
    case 0x0E:
        yaw = w32(r, a0 + 0xC4);
        t = em_ee_sub_bits(w32(r, G_810374), yaw);
        t = fcall1(r, sp, FN_1B1470, t);
        t = fcall1(r, sp, FN_11DF78, t);
        st32(r, SPR(0x3A20), t);
        d = callv(r, sp, FN_128350, 0, NULL, 1, &t).v0;
        x[0] = d;
        x[1] = UINT64_C(0x3FF921FB60000000);
        return callv(r, sp, FN_1000C0, 2, x, 0, NULL).v0 != 0 ? 0 : 3;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------------
 * 001B41F0(victim, hit, dir, flags, p5, p6): the hit application. Returns
 * 1 when applied, 0 when deflected. By the victim's type byte +3 (a jump
 * table of 20 entries; 20 and above take the default):
 *   1: when byte +0x0D is 3 and either byte +5 is 9 or D_00810354 < 47.0:
 *      deflect. Otherwise effect 0x80000024 (the common tail).
 *   6, 7: effect 0x80000024.  2: 0x80000025.  10, 11: 0x80000035.
 *   3: 0x80000034.  4, 5, 8, 12, 14, 15 and 20..255: 0x80000007.
 *   9: effect 0x80000025, but deflect when flags & 0x40.
 *   13: 0x70003610 = D_00810350 - (victim+0xB0) (001028D0), normalised in
 *      place (00102760), its w = 1.0; 001031E0(0x70003600, hit);
 *      001EFD90(0x80000026, 0x70003600, 0x70003610); 1.
 *   19: byte +0 = 3; 001031E0(victim+0x70, hit); halfword +0x36 =
 *      (p5 | p6 * 5 | 0x8000) when flags is nonzero, else (p6 | p5); 1.
 *   0, 16, 17, 18: the spray (below) with its own tail.
 * Deflect: 00102948(victim+0x70, dir); 0.
 * The spray (both tails): 0x70003600 = 0, +4 = 001B1470(pi + D_00810374),
 * +8 = 0, +C = 1.0; three LCG draws d: 0x70003680 / 84 / 88 =
 * float((d >> 17 / 13 / 19) & 0xFF) / 255 (each stored before the next draw;
 * the first two are read back before the third is stored); 0x70003610.. =
 * (hit[i] - 0.5) + that jitter, w = 1.0; 00102948(victim+0x70, dir); byte
 * +0 = 3. Then
 *   types 0/16/17/18: flags nonzero: +0x36 = p5 | p6 * 5 | 0x8000,
 *      001EFD90(0x80000076, 0x70003610, 0x70003600), 001FBD50(victim, 0x15D,
 *      0; 300.0). Else 001EFD90(0x80000007, ...), +0x36 = p6 | p5, and by
 *      bit 13 of an LCG draw 001FC580(victim, 0x15A), else 0x15B.
 *   the common tail: flags nonzero: +0x36 = p6 | p5 | 0x8000, 001EFD90(
 *      0x80000076, ...), 001FBD50(victim, 0x15D, 0; 300.0). Else
 *      001EFD90(effect, ...), +0x36 = p6 | p5, then 001FBD50(victim, 0x1B1,
 *      0; 300.0) when p5 & 0x1000, else by bit 13 of an LCG draw
 *      001FC580(victim, 0x15A / 0x15B).
 * Every path with a spray returns 1. The frame is 0x80 bytes.
 * ---------------------------------------------------------------------- */
static uint32_t jitter(Run *r, uint32_t sp, unsigned shift)
{
    uint32_t v = (uint32_t)call0(r, sp, FN_RAND);
    uint32_t b = (uint32_t)((int32_t)v >> shift) & 0xFFu;
    return em_ee_div_bits(em_ee_cvt_s_w_bits(b), F_255);
}

static void spray(Run *r, uint32_t sp, uint32_t victim, uint32_t hit, uint32_t dir)
{
    uint32_t t, c, fa, fb, fc, h;
    st32(r, SPR(0x3600), 0);
    t = em_ee_add_bits(F_PI, w32(r, G_810374));
    st32(r, SPR(0x3604), fcall1(r, sp, FN_1B1470, t));
    st32(r, SPR(0x3608), 0);
    st32(r, SPR(0x360C), F_ONE);
    st32(r, SPR(0x3680), jitter(r, sp, 17));
    st32(r, SPR(0x3684), jitter(r, sp, 13));
    c = (uint32_t)((int32_t)(uint32_t)call0(r, sp, FN_RAND) >> 19) & 0xFFu;
    fa = w32(r, SPR(0x3680));
    fc = em_ee_div_bits(em_ee_cvt_s_w_bits(c), F_255);
    fb = w32(r, SPR(0x3684));
    st32(r, SPR(0x3688), fc);
    h = w32(r, hit);
    fc = w32(r, SPR(0x3688));
    st32(r, SPR(0x3610), em_ee_add_bits(em_ee_sub_bits(h, F_HALF), fa));
    h = w32(r, hit + 4);
    st32(r, SPR(0x3614), em_ee_add_bits(em_ee_sub_bits(h, F_HALF), fb));
    h = w32(r, hit + 8);
    st32(r, SPR(0x3618), em_ee_add_bits(em_ee_sub_bits(h, F_HALF), fc));
    st32(r, SPR(0x361C), F_ONE);
    call2(r, sp, FN_102948, victim + 0x70, dir);
    st8(r, victim, 3);
}

/* The ricochet sound: bit 13 of an LCG draw picks 0x15A, else 0x15B. */
static void ricochet(Run *r, uint32_t sp, uint32_t victim)
{
    uint32_t v = (uint32_t)call0(r, sp, FN_RAND);
    call2(r, sp, FN_1FC580, victim, ((uint32_t)((int32_t)v >> 13) & 1u) ? 0x15Au : 0x15Bu);
}

static int32_t f_1B41F0(Run *r, uint32_t sp, uint32_t victim, uint32_t hit, uint32_t dir, uint32_t flags,
                        uint32_t p5, uint32_t p6)
{
    uint32_t k, fx;
    sp -= 0x80;
    k = u8(r, victim + 3);
    switch (k < 0x14u ? k : 0xFFu) {
    case 1:
        if (u8(r, victim + 0x0D) == 3u &&
            (u8(r, victim + 5) == 9u || em_ee_c_lt_bits(w32(r, G_810354), F_47))) {
            call2(r, sp, FN_102948, victim + 0x70, dir);
            return 0;
        }
        fx = 0x80000024u;
        break;
    case 6:
    case 7:
        fx = 0x80000024u;
        break;
    case 2:
        fx = 0x80000025u;
        break;
    case 10:
    case 11:
        fx = 0x80000035u;
        break;
    case 3:
        fx = 0x80000034u;
        break;
    case 13:
        call3(r, sp, FN_1028D0, SPR(0x3610), G_810350, victim + 0xB0);
        call2(r, sp, FN_102760, SPR(0x3610), SPR(0x3610));
        st32(r, SPR(0x361C), F_ONE);
        call2(r, sp, FN_1031E0, SPR(0x3600), hit);
        call3(r, sp, FN_1EFD90, 0x80000026u, SPR(0x3600), SPR(0x3610));
        return 1;
    case 9:
        fx = 0x80000025u;
        if (flags & 0x40u) {
            call2(r, sp, FN_102948, victim + 0x70, dir);
            return 0;
        }
        break;
    case 19:
        st8(r, victim, 3);
        call2(r, sp, FN_1031E0, victim + 0x70, hit);
        st16(r, victim + 0x36, flags ? (p5 | (p6 * 5u) | 0x8000u) : (p6 | p5));
        return 1;
    case 0:
    case 16:
    case 17:
    case 18:
        spray(r, sp, victim, hit, dir);
        if (flags) {
            st16(r, victim + 0x36, p5 | (p6 * 5u) | 0x8000u);
            call3(r, sp, FN_1EFD90, 0x80000076u, SPR(0x3610), SPR(0x3600));
            call3f(r, sp, FN_1FBD50, victim, 0x15D, 0, F_300);
        } else {
            call3(r, sp, FN_1EFD90, 0x80000007u, SPR(0x3610), SPR(0x3600));
            st16(r, victim + 0x36, p6 | p5);
            ricochet(r, sp, victim);
        }
        return 1;
    default:
        fx = 0x80000007u;
        break;
    }
    spray(r, sp, victim, hit, dir);
    if (flags) {
        st16(r, victim + 0x36, p6 | p5 | 0x8000u);
        call3(r, sp, FN_1EFD90, 0x80000076u, SPR(0x3610), SPR(0x3600));
        call3f(r, sp, FN_1FBD50, victim, 0x15D, 0, F_300);
    } else {
        call3(r, sp, FN_1EFD90, fx, SPR(0x3610), SPR(0x3600));
        st16(r, victim + 0x36, p6 | p5);
        if (p5 & 0x1000u)
            call3f(r, sp, FN_1FBD50, victim, 0x1B1, 0, F_300);
        else
            ricochet(r, sp, victim);
    }
    return 1;
}

/* ------------------------------------------------------------------------
 * 00189FE0(a0, a1, a2): only while the record e = word 0x700031D4 is
 * nonzero and its byte +0 is 1. The frame is 0x40 bytes with a local vector
 * v at sp+0x30: v = a1 - a2 (001028D0), v.w = 1.0, v normalised in place
 * (00102760). By e's kind (byte +2 with its top three bits cleared):
 *   2: when v.y < 0: v.y = +0 and v normalised again. Then 001B41F0(e,
 *      0x700031B0, v, word (word 0x700031D0)+0x1C, 0x1000, halfword a0+0x36
 *      sign-extended); return.
 *   4: s = 00189EC0(e). 0: return. 2: 0x700038D0 = (0, 001B1470(pi +
 *      D_00810374), 0, 1.0) and 001EFD90(0x80000007, 0x700031B0,
 *      0x700038D0). 3: 001EFD20(0x80000019, 0x700031B0). Then (s != 0)
 *      e+0x36 = a0+0x36, then e+0x36 |= 0x1000 (re-read), and
 *      00102948(e+0x70, v).
 *   other kinds: nothing more.
 * ---------------------------------------------------------------------- */
static void f_189FE0(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t e, v, kind, t;
    int32_t st;
    sp -= 0x40;
    v = sp + 0x30;
    e = w32(r, SPR(0x31D4));
    if (!e || u8(r, e) != 1u)
        return;
    call3(r, sp, FN_1028D0, v, a1, a2);
    st32(r, v + 0xC, F_ONE);
    call2(r, sp, FN_102760, v, v);
    kind = u8(r, e + 2) & 0x1Fu;
    if (kind == 2u) {
        uint32_t base, h;
        if (em_ee_c_lt_bits(w32(r, v + 4), F_ZERO)) {
            st32(r, v + 4, 0);
            call2(r, sp, FN_102760, v, v);
        }
        base = w32(r, SPR(0x31D0));
        h = (uint32_t)s16(r, a0 + 0x36);
        (void)f_1B41F0(r, sp, e, SPR(0x31B0), v, w32(r, base + 0x1C), 0x1000u, h);
        return;
    }
    if (kind != 4u)
        return;
    st = f_189EC0(r, sp, e);
    if (st == 0)
        return;
    if (st == 2) {
        st32(r, SPR(0x38D0), 0);
        t = em_ee_add_bits(F_PI, w32(r, G_810374));
        st32(r, SPR(0x38D4), fcall1(r, sp, FN_1B1470, t));
        st32(r, SPR(0x38D8), 0);
        st32(r, SPR(0x38DC), F_ONE);
        call3(r, sp, FN_1EFD90, 0x80000007u, SPR(0x31B0), SPR(0x38D0));
    } else if (st == 3) {
        call2(r, sp, FN_1EFD20, 0x80000019u, SPR(0x31B0));
    }
    st16(r, e + 0x36, (uint32_t)s16(r, a0 + 0x36));
    st16(r, e + 0x36, (uint32_t)s16(r, e + 0x36) | 0x1000u);
    call2(r, sp, FN_102948, e + 0x70, v);
}

/* ------------------------------------------------------------------------
 * 0018A180(p): byte p+0x0A = 1, byte p+0 = 2; 001B61C0(0, 0xD0, 0x0A, 1);
 * d = an LCG draw (its whole register); b = d & 1, and -1 when d is
 * negative and odd; 001FBD50(0x8102B0, 0x180 + b, 0; 300.0). Frame 0x10.
 * ---------------------------------------------------------------------- */
static void f_18A180(Run *r, uint32_t sp, uint32_t p)
{
    uint64_t d;
    int64_t b;
    sp -= 0x10;
    st8(r, p + 0x0A, 1);
    st8(r, p, 2);
    call4(r, sp, FN_1B61C0, 0, 0xD0, 0x0A, 1);
    d = call0(r, sp, FN_RAND);
    b = (int64_t)(d & 1u);
    if ((int64_t)d < 0 && b != 0)
        b -= 2;
    call3f(r, sp, FN_1FBD50, G_8102B0, (uint32_t)(0x180 + b), 0, F_300);
}

/* ------------------------------------------------------------------------
 * 0019AA80(a0, a1, a2): for i = 0..2, 0x70003190[i] = a0[i] then
 * 0x700031A0[i] = a1[i] (the words as they are, read and stored in that
 * order); 0x700031AC = 1.0, 0x7000319C = 1.0, 0x700031D4 = 0; r =
 * 001A7280(a2 & 0xFFFF). Result 1 when r (the whole register) is nonzero,
 * else 0 and 0x700031D0 = 0; 0x700031D8 = the result. Frame 0x20.
 * ---------------------------------------------------------------------- */
static int32_t f_19AA80(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t a2)
{
    unsigned i;
    int32_t ret;
    sp -= 0x20;
    for (i = 0; i < 3; i++) {
        st32(r, SPR(0x3190) + 4 * i, w32(r, a0 + 4 * i));
        st32(r, SPR(0x31A0) + 4 * i, w32(r, a1 + 4 * i));
    }
    st32(r, SPR(0x31AC), F_ONE);
    st32(r, SPR(0x319C), F_ONE);
    st32(r, SPR(0x31D4), 0);
    ret = call1(r, sp, FN_1A7280, a2 & 0xFFFFu) != 0 ? 1 : 0;
    if (!ret)
        st32(r, SPR(0x31D0), 0);
    st32(r, SPR(0x31D8), (uint32_t)ret);
    return ret;
}

/* ------------------------------------------------------------------------
 * 0019B2C0(a0, a1, flags): frame 0x70 with locals m (sp+0x50) and t
 * (sp+0x60). 0x70003190 = a0.x, 0x70003194 = a1.y, 0x70003198 = a0.z; for
 * i = 0..2: 0x700031A0[i] = t[i] = a1[i]. 0x700031AC = 0x7000319C =
 * 0x700031D4 = 0. m = 0x700031A0 - 0x70003190 (001028D0), normalised
 * (00102760), scaled by 0.01 (00103230); 0x700031A0 += m (001028B8);
 * halfword 0x7000324E = -1. mode = 0; with flags & 2: 0x70003254 = 0 and
 * mode = 2 when 0019FE50() gives 0 (whole register); with flags & 4: mode
 * = 4 when 0019CB60() gives 0. 0x700031A0 -= m (001028D0). With a mode:
 * for i = 0..2, 0x700031A0[i] = t[i] and 0x700031C0[i] = 0x700031B0[i] -
 * 0x700031A0[i] (both re-read); then with flags bit 31 a0.x += 0x700031C0
 * and a0.z += 0x700031C8. Without one: 0x700031D0 = 0. 0x700031D8 = mode;
 * result mode.
 * ---------------------------------------------------------------------- */
static int32_t f_19B2C0(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t flags)
{
    uint32_t m, t;
    unsigned i;
    int32_t mode = 0;
    sp -= 0x70;
    m = sp + 0x50;
    t = sp + 0x60;
    st32(r, SPR(0x3190), w32(r, a0));
    st32(r, SPR(0x3194), w32(r, a1 + 4));
    st32(r, SPR(0x3198), w32(r, a0 + 8));
    for (i = 0; i < 3; i++) {
        uint32_t x = w32(r, a1 + 4 * i);
        st32(r, SPR(0x31A0) + 4 * i, x);
        st32(r, t + 4 * i, x);
    }
    st32(r, SPR(0x31AC), 0);
    st32(r, SPR(0x319C), 0);
    st32(r, SPR(0x31D4), 0);
    call3(r, sp, FN_1028D0, m, SPR(0x31A0), SPR(0x3190));
    call2(r, sp, FN_102760, m, m);
    call2f(r, sp, FN_103230, m, m, F_CENTI);
    call3(r, sp, FN_1028B8, SPR(0x31A0), SPR(0x31A0), m);
    st16(r, SPR(0x324E), 0xFFFFu);
    if (flags & 2u) {
        st32(r, SPR(0x3254), 0);
        if (call0(r, sp, FN_19FE50) == 0)
            mode = 2;
    }
    if (flags & 4u) {
        if (call0(r, sp, FN_19CB60) == 0)
            mode = 4;
    }
    call3(r, sp, FN_1028D0, SPR(0x31A0), SPR(0x31A0), m);
    if (mode != 0) {
        for (i = 0; i < 3; i++) {
            uint32_t a, b;
            st32(r, SPR(0x31A0) + 4 * i, w32(r, t + 4 * i));
            a = w32(r, SPR(0x31B0) + 4 * i);
            b = w32(r, SPR(0x31A0) + 4 * i);
            st32(r, SPR(0x31C0) + 4 * i, em_ee_sub_bits(a, b));
        }
        if (flags & 0x80000000u) {
            uint32_t c0 = w32(r, SPR(0x31C0));
            uint32_t z, c8;
            st32(r, a0, em_ee_add_bits(w32(r, a0), c0));
            z = w32(r, a0 + 8);
            c8 = w32(r, SPR(0x31C8));
            st32(r, a0 + 8, em_ee_add_bits(z, c8));
        }
    } else {
        st32(r, SPR(0x31D0), 0);
    }
    st32(r, SPR(0x31D8), (uint32_t)mode);
    return mode;
}

/* ------------------------------------------------------------------------
 * 0019C6F0(key, a1): a leaf. The list pointer pp = word (D_0024D7C0[area
 * byte D_00810700]) + 4 * sub byte D_00810701; base = word 0x70003250; cur
 * = word *pp. Records are 0x28 bytes. Each step: the halfword at cur ==
 * 0xFF ends the scan (0); the record at (word *pp, re-read) + 0x28 * n
 * must have halfword +0 == 0x0B, else the scan ends (0); when its signed
 * halfword +4 equals key, w = the word at base + 4 * (halfword +6 >> 8) + 4:
 * with key negative (bit 31) bit 29 of w must be set, otherwise clear; then
 * the word gets bit 30 cleared when a1 is nonzero, else set, and the result
 * is 1. Any other record: cur and the offset advance by 0x28.
 * ---------------------------------------------------------------------- */
static int32_t f_19C6F0(Run *r, uint32_t key, uint32_t a1)
{
    uint32_t pp, base, cur, off = 0, rec, idx, at = 0, w;
    uint32_t area = u8(r, G_AREA);
    uint32_t sub = u8(r, G_SUB);
    pp = w32(r, G_24D7C0 + 4 * area) + 4 * sub;
    base = w32(r, SPR(0x3250));
    cur = w32(r, pp);
    for (;;) {
        if (s16(r, cur) == 0xFF)
            return 0;
        rec = w32(r, pp) + off;
        if (s16(r, rec) != 0x0B)
            return 0;
        if ((int32_t)key == s16(r, rec + 4)) {
            idx = u16(r, rec + 6) >> 8;
            at = base + 4 * idx + 4;
            w = w32(r, at) & 0x20000000u;
            if ((key & 0x80000000u) ? w != 0 : w == 0)
                break;
        }
        cur += 0x28;
        off += 0x28;
    }
    w = w32(r, at);
    st32(r, at, a1 ? (w & 0xBFFFFFFFu) : (w | 0x40000000u));
    return 1;
}

/* ------------------------------------------------------------------------
 * 001B0CD0(a0, a1): frame 0x50 with a local vector v at sp+0x40.
 * p = 001F4F40(0); nothing more when its whole register is 0. Else
 * 00102948(p+0xB0, a0+0xB0); 00102958(p+0xD0, (word a0+0x110) + 0x90);
 * 001026A0(v, p+0xD0, 0x24D6B0 + 16 * (a1 & 3)); p+0x100.. = v (four words).
 * ---------------------------------------------------------------------- */
static void f_1B0CD0(Run *r, uint32_t sp, uint32_t a0, uint32_t a1)
{
    uint32_t p, v;
    uint64_t got;
    unsigned i;
    sp -= 0x50;
    v = sp + 0x40;
    got = call1(r, sp, FN_1F4F40, 0);
    if (got == 0)
        return;
    p = (uint32_t)got;
    call2(r, sp, FN_102948, p + 0xB0, a0 + 0xB0);
    call2(r, sp, FN_102958, p + 0xD0, w32(r, a0 + 0x110) + 0x90);
    call3(r, sp, FN_1026A0, v, p + 0xD0, G_24D6B0 + ((a1 & 3u) << 4));
    for (i = 0; i < 4; i++)
        st32(r, p + 0x100 + 4 * i, w32(r, v + 4 * i));
}

/* ------------------------------------------------------------------------
 * 001C24D0(a0, a1, a2): 0x700038C0 = 001026A0(matrix a2, vector a1);
 * 0x700038C0 += a0+0xB0 (001028B8); result 0019AD00(a0, 0x700038C0,
 * 0x80000006)'s whole v0, left untouched. Frame 0x20.
 * ---------------------------------------------------------------------- */
static uint64_t f_1C24D0(Run *r, uint32_t sp, uint32_t a0, uint32_t a1, uint32_t a2)
{
    sp -= 0x20;
    call3(r, sp, FN_1026A0, SPR(0x38C0), a2, a1);
    call3(r, sp, FN_1028B8, SPR(0x38C0), SPR(0x38C0), a0 + 0xB0);
    return call3(r, sp, FN_19AD00, a0, SPR(0x38C0), 0x80000006u);
}

/* ------------------------------------------------------------------------
 * 001C5050(a0; k): frame 0x40 with a local vector v at sp+0x30.
 * 00102948(v, a0+0x80); v.w = k; result 001D7FA0(a0+0xB0, v, 2; 1.0, 0)'s
 * whole v0, left untouched.
 * ---------------------------------------------------------------------- */
static uint64_t f_1C5050(Run *r, uint32_t sp, uint32_t a0, uint32_t k)
{
    uint64_t x[3];
    uint32_t f[2];
    uint32_t v;
    sp -= 0x40;
    v = sp + 0x30;
    call2(r, sp, FN_102948, v, a0 + 0x80);
    st32(r, v + 0xC, k);
    x[0] = reg(a0 + 0xB0);
    x[1] = reg(v);
    x[2] = reg(2);
    f[0] = F_ONE;
    f[1] = F_ZERO;
    return callv(r, sp, FN_1D7FA0, 3, x, 2, f).v0;
}

/* ------------------------------------------------------------------------
 * 001C6200(a0): for i while i < byte a0+0x0C (re-read before each step):
 * the i-th pointer b = word a0+0x110+4i, re-read before every store:
 * halfword b+0x64 = -1; halfwords +0x88, +0x8A, +0x8C = 0x1000; words
 * +0x7C, +0x80, +0x84, +0x70, +0x74, +0x78 = 0 (in that order); then
 * 001029C0(b) (re-read). Frame 0x40.
 * ---------------------------------------------------------------------- */
static void f_1C6200(Run *r, uint32_t sp, uint32_t a0)
{
    static const uint16_t words[6] = {0x7C, 0x80, 0x84, 0x70, 0x74, 0x78};
    uint32_t i, at;
    unsigned k;
    sp -= 0x40;
    for (i = 0; (int32_t)i < (int32_t)u8(r, a0 + 0x0C); i++) {
        at = a0 + 4 * i + 0x110;
        st16(r, w32(r, at) + 0x64, 0xFFFFu);
        st16(r, w32(r, at) + 0x88, 0x1000u);
        st16(r, w32(r, at) + 0x8A, 0x1000u);
        st16(r, w32(r, at) + 0x8C, 0x1000u);
        for (k = 0; k < 6; k++)
            st32(r, w32(r, at) + words[k], 0);
        call1(r, sp, FN_1029C0, w32(r, at));
    }
}

/* ------------------------------------------------------------------------
 * 001C63D0(a0): continues into 001C9610(a0+0x110, byte a0+0x0C, a0+0xD0)
 * with its own stack pointer (no frame); the result is 001C9610's.
 * ---------------------------------------------------------------------- */
static uint64_t f_1C63D0(Run *r, uint32_t sp, uint32_t a0)
{
    uint32_t n = u8(r, a0 + 0x0C);
    return call3(r, sp, FN_1C9610, a0 + 0x110, n, a0 + 0xD0);
}

/* ------------------------------------------------------------------------
 * 001CA3B0(out; x, y, z): half angles hx = x / 2, hy = y / 2, hz = z / 2
 * (each divided just before its sine); sines by 0011E2A8 in the order x,
 * y, z, then cosines by 0011DE90 in the same order. With sx.. / cx..:
 *   out.x = cx * (sz * cy) - sx * (cz * sy)
 *   out.y = cx * (cz * sy) + sx * (sz * cy)
 *   out.z = sx * (cz * cy) - cx * (sz * sy)
 *   out.w = cx * (cz * cy) + sx * (sz * sy)
 * each an EE accumulator pair (the first product into the accumulator, the
 * second added or subtracted by MADD / MSUB). Frame 0x40.
 * ---------------------------------------------------------------------- */
static void f_1CA3B0(Run *r, uint32_t sp, uint32_t out, uint32_t x, uint32_t y, uint32_t z)
{
    uint32_t hx, hy, hz, sx, sy, sz, cx, cy, cz, p1, p2, p3, p4, acc;
    sp -= 0x40;
    hx = em_ee_div_bits(x, F_TWO);
    sx = fcall1(r, sp, FN_11E2A8, hx);
    hy = em_ee_div_bits(y, F_TWO);
    sy = fcall1(r, sp, FN_11E2A8, hy);
    hz = em_ee_div_bits(z, F_TWO);
    sz = fcall1(r, sp, FN_11E2A8, hz);
    cx = fcall1(r, sp, FN_11DE90, hx);
    cy = fcall1(r, sp, FN_11DE90, hy);
    cz = fcall1(r, sp, FN_11DE90, hz);
    p1 = em_ee_mul_bits(sz, cy);
    p4 = em_ee_mul_bits(cz, cy);
    p2 = em_ee_mul_bits(cz, sy);
    acc = em_ee_mula_bits(cx, p1);
    st32(r, out, em_ee_msub_bits(acc, sx, p2));
    p3 = em_ee_mul_bits(sz, sy);
    acc = em_ee_mula_bits(cx, p2);
    st32(r, out + 4, em_ee_madd_bits(acc, sx, p1));
    acc = em_ee_mula_bits(sx, p4);
    st32(r, out + 8, em_ee_msub_bits(acc, cx, p3));
    acc = em_ee_mula_bits(cx, p4);
    st32(r, out + 0xC, em_ee_madd_bits(acc, sx, p3));
}

/* ------------------------------------------------------------------------
 * 001CA4D0(out, a, b): a leaf with a 0x10-byte frame. With A = a, B = b
 * (x, y, z, w = +0, +4, +8, +C; every component read afresh for each
 * result), the four results go to the frame, then the 16 bytes are copied
 * to out rounded down to a multiple of 16 (the quadword store ignores the
 * low four address bits):
 *   x = ((A.x * B.w + A.y * B.z) - A.z * B.y) + A.w * B.x
 *   y = (A.z * B.x + (-A.x * B.z + A.y * B.w)) + A.w * B.y
 *   z = (A.z * B.w + (A.x * B.y - A.y * B.x)) + A.w * B.z
 *   w = ((-A.x * B.x - A.y * B.y) - A.z * B.z) + A.w * B.w
 * with EE arithmetic: products that enter the accumulator or a MADD / MSUB
 * as the original forms them (x: accumulator A.x*B.w, MADD A.y*B.z, then
 * the accumulator is that minus the separately multiplied A.z*B.y, then
 * MADD A.w*B.x; y: accumulator -A.x*B.z, MADD A.y*B.w, accumulator A.z*B.x
 * plus that, MADD A.w*B.y; z: accumulator A.x*B.y, MSUB A.y*B.x,
 * accumulator A.z*B.w plus that, MADD A.w*B.z; w: (-A.x)*B.x - A.y*B.y, the
 * accumulator that minus A.z*B.z, MADD A.w*B.w).
 * ---------------------------------------------------------------------- */
static void f_1CA4D0(Run *r, uint32_t sp, uint32_t out, uint32_t a, uint32_t b)
{
    uint32_t ax, ay, az, aw, bx, by, bz, bw, acc, p, q, t;
    uint32_t loc = sp - 0x10;
    unsigned i;
    /* x */
    ax = w32(r, a);
    bw = w32(r, b + 0xC);
    az = w32(r, a + 8);
    by = w32(r, b + 4);
    ay = w32(r, a + 4);
    bz = w32(r, b + 8);
    acc = em_ee_mula_bits(ax, bw);
    aw = w32(r, a + 0xC);
    bx = w32(r, b);
    p = em_ee_mul_bits(az, by);
    t = em_ee_madd_bits(acc, ay, bz);
    acc = em_ee_suba_bits(t, p);
    st32(r, loc, em_ee_madd_bits(acc, aw, bx));
    /* y */
    ax = w32(r, a);
    bz = w32(r, b + 8);
    ay = w32(r, a + 4);
    bw = w32(r, b + 0xC);
    az = w32(r, a + 8);
    bx = w32(r, b);
    acc = em_ee_mula_bits(em_ee_neg_bits(ax), bz);
    aw = w32(r, a + 0xC);
    t = em_ee_madd_bits(acc, ay, bw);
    by = w32(r, b + 4);
    p = em_ee_mul_bits(az, bx);
    acc = em_ee_adda_bits(p, t);
    st32(r, loc + 4, em_ee_madd_bits(acc, aw, by));
    /* z */
    ax = w32(r, a);
    by = w32(r, b + 4);
    az = w32(r, a + 8);
    bw = w32(r, b + 0xC);
    ay = w32(r, a + 4);
    bx = w32(r, b);
    aw = w32(r, a + 0xC);
    acc = em_ee_mula_bits(ax, by);
    bz = w32(r, b + 8);
    p = em_ee_mul_bits(az, bw);
    t = em_ee_msub_bits(acc, ay, bx);
    acc = em_ee_adda_bits(p, t);
    st32(r, loc + 8, em_ee_madd_bits(acc, aw, bz));
    /* w */
    az = w32(r, a + 8);
    bz = w32(r, b + 8);
    ay = w32(r, a + 4);
    by = w32(r, b + 4);
    ax = w32(r, a);
    bx = w32(r, b);
    aw = w32(r, a + 0xC);
    p = em_ee_mul_bits(az, bz);
    q = em_ee_mul_bits(ay, by);
    t = em_ee_mul_bits(em_ee_neg_bits(ax), bx);
    bw = w32(r, b + 0xC);
    t = em_ee_sub_bits(t, q);
    acc = em_ee_suba_bits(t, p);
    st32(r, loc + 0xC, em_ee_madd_bits(acc, aw, bw));
    /* the quadword: all 16 bytes read, then written */
    {
        uint32_t v[4];
        uint32_t dst = out & ~0xFu;
        for (i = 0; i < 4; i++)
            v[i] = w32(r, loc + 4 * i);
        for (i = 0; i < 4; i++)
            st32(r, dst + 4 * i, v[i]);
    }
}

/* ------------------------------------------------------------------------
 * 0021BD60(p): a leaf returning 0 or 1.
 *   byte +0x236 nonzero: 1.
 *   bit 1 of byte +0 set and byte +0x1F0 != 0x3B: 1.
 *   byte +4 == 1, by byte +5: 0, 1, 0x21, 0x22 -> 0; 0x1D -> 0 when byte
 *     +0x1F1 is 1, else 1; 0x1E -> 0 when byte +0x1F1 is 1, else 1; other
 *     -> 1.
 *   byte +4 == 2: 0 when byte +5 is 0x0B, else 1.
 *   other: 1.
 * ---------------------------------------------------------------------- */
static int32_t f_21BD60(Run *r, uint32_t p)
{
    uint32_t s, sub;
    if (u8(r, p + 0x236))
        return 1;
    if ((u8(r, p) & 2u) && u8(r, p + 0x1F0) != 0x3Bu)
        return 1;
    s = u8(r, p + 4);
    if (s == 1u) {
        sub = u8(r, p + 5);
        if (sub == 0u || sub == 1u || sub - 0x21u < 2u)
            return 0;
        if (sub == 0x1Du && u8(r, p + 0x1F1) == 1u)
            return 0;
        if (sub != 0x1Eu)
            return 1;
        return u8(r, p + 0x1F1) != 1u ? 1 : 0;
    }
    if (s == 2u)
        return u8(r, p + 5) == 0x0Bu ? 0 : 1;
    return 1;
}

/* ------------------------------------------------------------------------
 * Entry points
 * ---------------------------------------------------------------------- */

#define ENTER(s, fn)                                                                                     \
    Run run;                                                                                             \
    if (!(s))                                                                                            \
        return -1;                                                                                       \
    if ((s)->fault != EM_AREA00_WORLD_FAULT_NONE)                                                        \
        return -1;                                                                                       \
    run.s = (s);                                                                                         \
    run.entry = (fn);                                                                                    \
    if (setjmp(run.out))                                                                                 \
        return -1;                                                                                       \
    if (!(s)->regions)                                                                                   \
        fault(&run, EM_AREA00_WORLD_FAULT_NULL, 0);

int em_area00_world_00189EC0(EmArea00World *s, uint32_t a0, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x00189EC0u);
    v = f_189EC0(&run, s->sp, a0);
    if (out)
        *out = v;
    return 0;
}

int em_area00_world_00189FE0(EmArea00World *s, uint32_t a0, uint32_t a1, uint32_t a2)
{
    ENTER(s, 0x00189FE0u);
    f_189FE0(&run, s->sp, a0, a1, a2);
    return 0;
}

int em_area00_world_0018A180(EmArea00World *s, uint32_t a0)
{
    ENTER(s, 0x0018A180u);
    f_18A180(&run, s->sp, a0);
    return 0;
}

int em_area00_world_0019AA80(EmArea00World *s, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x0019AA80u);
    v = f_19AA80(&run, s->sp, a0, a1, a2);
    if (out)
        *out = v;
    return 0;
}

int em_area00_world_0019B2C0(EmArea00World *s, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x0019B2C0u);
    v = f_19B2C0(&run, s->sp, a0, a1, a2);
    if (out)
        *out = v;
    return 0;
}

int em_area00_world_0019C6F0(EmArea00World *s, uint32_t a0, uint32_t a1, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x0019C6F0u);
    v = f_19C6F0(&run, a0, a1);
    if (out)
        *out = v;
    return 0;
}

int em_area00_world_001B0CD0(EmArea00World *s, uint32_t a0, uint32_t a1)
{
    ENTER(s, 0x001B0CD0u);
    f_1B0CD0(&run, s->sp, a0, a1);
    return 0;
}

int em_area00_world_001B41F0(EmArea00World *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0,
                             uint32_t t1, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x001B41F0u);
    v = f_1B41F0(&run, s->sp, a0, a1, a2, a3, t0, t1);
    if (out)
        *out = v;
    return 0;
}

int em_area00_world_001C24D0(EmArea00World *s, uint32_t a0, uint32_t a1, uint32_t a2, uint64_t *out)
{
    uint64_t v;
    ENTER(s, 0x001C24D0u);
    v = f_1C24D0(&run, s->sp, a0, a1, a2);
    if (out)
        *out = v;
    return 0;
}

int em_area00_world_001C5050(EmArea00World *s, uint32_t a0, uint32_t f12, uint64_t *out)
{
    uint64_t v;
    ENTER(s, 0x001C5050u);
    v = f_1C5050(&run, s->sp, a0, f12);
    if (out)
        *out = v;
    return 0;
}

int em_area00_world_001C6200(EmArea00World *s, uint32_t a0)
{
    ENTER(s, 0x001C6200u);
    f_1C6200(&run, s->sp, a0);
    return 0;
}

int em_area00_world_001C63D0(EmArea00World *s, uint32_t a0, uint64_t *out)
{
    uint64_t v;
    ENTER(s, 0x001C63D0u);
    v = f_1C63D0(&run, s->sp, a0);
    if (out)
        *out = v;
    return 0;
}

int em_area00_world_001CA3B0(EmArea00World *s, uint32_t a0, uint32_t f12, uint32_t f13, uint32_t f14)
{
    ENTER(s, 0x001CA3B0u);
    f_1CA3B0(&run, s->sp, a0, f12, f13, f14);
    return 0;
}

int em_area00_world_001CA4D0(EmArea00World *s, uint32_t a0, uint32_t a1, uint32_t a2)
{
    ENTER(s, 0x001CA4D0u);
    f_1CA4D0(&run, s->sp, a0, a1, a2);
    return 0;
}

int em_area00_world_0021BD60(EmArea00World *s, uint32_t a0, int32_t *out)
{
    int32_t v;
    ENTER(s, 0x0021BD60u);
    v = f_21BD60(&run, a0);
    if (out)
        *out = v;
    return 0;
}
