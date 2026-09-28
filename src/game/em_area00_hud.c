/* em_area00_hud.c - AREA00 lane A00HUD translations (see em_area00_hud.h and
 * docs/AREA00_HUD.md). Every routine names its original address; the
 * comments say what the original does, never how it is encoded. */
#include "em_area00_hud.h"

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#include "em_ee_float.h"

/* ------------------------------------------------------------------------
 * Fail-stop plumbing
 * ---------------------------------------------------------------------- */

typedef struct {
    EmArea00Hud *s;
    jmp_buf out;
} Run;

static void fault(Run *r, int32_t code, uint32_t address)
{
    if (r->s->fault == EM_AREA00_HUD_FAULT_NONE) {
        r->s->fault = code;
        r->s->fault_address = address;
    }
    longjmp(r->out, 1);
}

void em_area00_hud_clear_fault(EmArea00Hud *s)
{
    if (!s)
        return;
    s->fault = EM_AREA00_HUD_FAULT_NONE;
    s->fault_function = 0;
    s->fault_address = 0;
}

/* The byte span [address, address + size) inside one region, or a fault. */
static uint8_t *span(Run *r, uint32_t address, uint32_t size)
{
    const EmArea00Hud *s = r->s;
    unsigned i;
    for (i = 0; i < s->region_count; i++) {
        const EmArea00HudRegion *g = &s->regions[i];
        if (g->bytes && address >= g->base && size <= g->size && address - g->base <= g->size - size)
            return g->bytes + (address - g->base);
    }
    fault(r, EM_AREA00_HUD_FAULT_UNMAPPED, address);
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

/* Test hook. A build that defines EM_AREA00_HUD_STORE_TRACE as a function
 * name reports every store (original address, size) to it once the address
 * is known to be mapped; tools/test_area00_hud_reference.py uses it to
 * compare memory with the original at every call leaving the module.
 * Ordinary builds compile it out. wr is the only store path. */
#ifdef EM_AREA00_HUD_STORE_TRACE
void EM_AREA00_HUD_STORE_TRACE(uint32_t address, unsigned size);
#define TRACE_STORE(a, n) EM_AREA00_HUD_STORE_TRACE((a), (n))
#else
#define TRACE_STORE(a, n) ((void)0)
#endif

static void wr(Run *r, uint32_t a, const uint8_t *bytes, unsigned n)
{
    uint8_t *p = span(r, a, n);
    TRACE_STORE(a, n);
    memcpy(p, bytes, n);
}

static void wrv(Run *r, uint32_t a, uint64_t v, unsigned n)
{
    uint8_t b[8];
    unsigned i;
    for (i = 0; i < n; i++)
        b[i] = (uint8_t)(v >> (8 * i));
    wr(r, a, b, n);
}

static uint32_t u8(Run *r, uint32_t a) { return rd(r, a, 1); }
static int32_t s16(Run *r, uint32_t a) { return (int32_t)(int16_t)rd(r, a, 2); }
static uint32_t w32(Run *r, uint32_t a) { return rd(r, a, 4); }
static void st8(Run *r, uint32_t a, uint32_t v) { wrv(r, a, v, 1); }
static void st16(Run *r, uint32_t a, uint32_t v) { wrv(r, a, v, 2); }
static void st32(Run *r, uint32_t a, uint32_t v) { wrv(r, a, v, 4); }
static void st64(Run *r, uint32_t a, uint64_t v) { wrv(r, a, v, 8); }

/* Quadword load / store: the address with its low four bits cleared. */
static void ld128(Run *r, uint32_t a, uint32_t q[4])
{
    uint32_t b = a & ~15u;
    unsigned k;
    const uint8_t *p = span(r, b, 16);
    for (k = 0; k < 4; k++)
        q[k] = (uint32_t)p[4 * k] | (uint32_t)p[4 * k + 1] << 8 | (uint32_t)p[4 * k + 2] << 16 |
               (uint32_t)p[4 * k + 3] << 24;
}

static void st128(Run *r, uint32_t a, const uint32_t q[4])
{
    uint8_t b[16];
    unsigned k;
    for (k = 0; k < 16; k++)
        b[k] = (uint8_t)(q[k / 4] >> (8 * (k % 4)));
    wr(r, a & ~15u, b, 16);
}

/* 32-bit value as the EE holds it in a 64-bit register. */
static uint64_t reg(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

/* ------------------------------------------------------------------------
 * Calls leaving the module
 * ---------------------------------------------------------------------- */

static EmArea00HudCall mk(uint32_t sp, uint32_t fn)
{
    EmArea00HudCall c;
    memset(&c, 0, sizeof c);
    c.fn = fn;
    c.sp = sp;
    return c;
}

static EmArea00HudCall *go(Run *r, EmArea00HudCall *c)
{
    c->vu = &r->s->vu;
    if (!r->s->call)
        fault(r, EM_AREA00_HUD_FAULT_NULL, c->fn);
    if (r->s->call(r->s->ctx, c) < 0)
        fault(r, EM_AREA00_HUD_FAULT_WORKER, c->fn);
    return c;
}

/* Integer arguments as register images; the result is the callee's v0. */
static uint64_t calli(Run *r, uint32_t sp, uint32_t fn, unsigned na, uint64_t a0, uint64_t a1, uint64_t a2,
                      uint64_t a3, uint64_t t0, uint64_t t1)
{
    EmArea00HudCall c = mk(sp, fn);
    c.na = na;
    c.a[0] = a0;
    c.a[1] = a1;
    c.a[2] = a2;
    c.a[3] = a3;
    c.a[4] = t0;
    c.a[5] = t1;
    return go(r, &c)->v0;
}

static uint64_t call1(Run *r, uint32_t sp, uint32_t fn, uint64_t a0)
{
    return calli(r, sp, fn, 1, a0, 0, 0, 0, 0, 0);
}
static uint64_t call2(Run *r, uint32_t sp, uint32_t fn, uint64_t a0, uint64_t a1)
{
    return calli(r, sp, fn, 2, a0, a1, 0, 0, 0, 0);
}
static uint64_t call3(Run *r, uint32_t sp, uint32_t fn, uint64_t a0, uint64_t a1, uint64_t a2)
{
    return calli(r, sp, fn, 3, a0, a1, a2, 0, 0, 0);
}
static uint64_t call4(Run *r, uint32_t sp, uint32_t fn, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
    return calli(r, sp, fn, 4, a0, a1, a2, a3, 0, 0);
}

/* (a0, a1; f12) */
static void call2f(Run *r, uint32_t sp, uint32_t fn, uint64_t a0, uint64_t a1, uint32_t f12)
{
    EmArea00HudCall c = mk(sp, fn);
    c.na = 2;
    c.a[0] = a0;
    c.a[1] = a1;
    c.nf = 1;
    c.f[0] = f12;
    (void)go(r, &c);
}

/* float(f12) -> f0 */
static uint32_t fcall(Run *r, uint32_t sp, uint32_t fn, uint32_t f12)
{
    EmArea00HudCall c = mk(sp, fn);
    c.nf = 1;
    c.f[0] = f12;
    return go(r, &c)->f0;
}

/* int(f12) -> v0 */
static uint64_t icall(Run *r, uint32_t sp, uint32_t fn, uint32_t f12)
{
    EmArea00HudCall c = mk(sp, fn);
    c.nf = 1;
    c.f[0] = f12;
    return go(r, &c)->v0;
}

/* ------------------------------------------------------------------------
 * Constants and callees
 * ---------------------------------------------------------------------- */

#define F_ONE     0x3F800000u
#define F_NEG_ONE 0xBF800000u
#define F_TWO     0x40000000u
#define F_THREE   0x40400000u
#define F_FOUR    0x40800000u
#define F_SIX     0x40C00000u
#define F_EIGHT   0x41000000u
#define F_TEN     0x41200000u
#define F_FIFTEEN 0x41700000u
#define F_32      0x42000000u
#define F_192     0x43400000u
#define F_255     0x437F0000u
#define F_500     0x43FA0000u
#define F_65535   0x477FFF00u
#define F_2POW31  0x4F000000u /* 2**31 */
#define F_2POWM31 0x30000000u /* 2**-31 */
#define F_HALF    0x3F000000u
#define F_TENTH   0x3DCCCCCDu
#define F_2P5     0x40200000u
#define F_PI      0x40490FDBu
#define F_2PI     0x40C90FDBu
#define F_0_05    0x3D4CCCCDu
#define F_0_02    0x3CA3D70Au
#define F_M0_01   0xBC23D70Au
#define F_1EM4    0x38D1B717u /* 0.0001 */
#define F_1EM6    0x358637BDu /* 1e-6 */
#define F_1_512   0x3B000000u /* 2**-9 */
#define F_1_224   0x3B924925u
#define F_546     0x44088889u /* 546.13336 */

#define FN_COPY_QW      0x00102948u /* one quadword (dst, src) */
#define FN_COPY_QW4     0x00102958u /* four quadwords (dst, src) */
#define FN_MAT_MUL      0x001026D0u /* (out, a, b) */
#define FN_NORM         0x00102760u /* (out, in) */
#define FN_VDIV         0x00102870u /* (out, in; f12) */
#define FN_VADD         0x001028B8u /* (out, a, b) */
#define FN_VSUB         0x001028D0u /* (out, a, b): a - b */
#define FN_VSCALE_B     0x00102900u /* (out, in; f12) */
#define FN_102918       0x00102918u /* (out, a, b) */
#define FN_IDENT        0x001029C0u /* (matrix) */
#define FN_ROT_B        0x00102BB0u /* (out, in; f12) */
#define FN_1031E0       0x001031E0u /* (out, in) */
#define FN_11DF78       0x0011DF78u /* float(f12) */
#define FN_11E2A8       0x0011E2A8u /* float(f12) */
#define FN_11E748       0x0011E748u /* float(f12) */
#define FN_RAND         0x00122BB8u /* the LCG draw */
#define FN_FLOAT_TO_INT 0x001281C0u /* int(f12) */

#define FN_19A570 0x0019A570u
#define FN_19AA80 0x0019AA80u
#define FN_1AFC10 0x001AFC10u
#define FN_1CB5F0 0x001CB5F0u
#define FN_1CB6B0 0x001CB6B0u
#define FN_1CB760 0x001CB760u
#define FN_1CB900 0x001CB900u
#define FN_1CD370 0x001CD370u
#define FN_1CD390 0x001CD390u
#define FN_1CD520 0x001CD520u
#define FN_1CFB50 0x001CFB50u
#define FN_1CFBE0 0x001CFBE0u
#define FN_1D04B0 0x001D04B0u
#define FN_1D1F20 0x001D1F20u
#define FN_1D1F80 0x001D1F80u
#define FN_1D1FF0 0x001D1FF0u
#define FN_1D2040 0x001D2040u
#define FN_1D6B60 0x001D6B60u
#define FN_1D6BA0 0x001D6BA0u
#define FN_1D6DD0 0x001D6DD0u
#define FN_1D7000 0x001D7000u
#define FN_1D7080 0x001D7080u
#define FN_1D7510 0x001D7510u
#define FN_1D7A80 0x001D7A80u
#define FN_1EFD20 0x001EFD20u
#define FN_1EFEB0 0x001EFEB0u
#define FN_1F02C0 0x001F02C0u

/* Globals (original addresses). */
#define G_CTX     0x00275670u /* the render context pointer */
#define G_27568C  0x0027568Cu
#define G_WORK    0x00275C34u /* the effect work block pointer (node + 0x1F0) */
#define G_275C30  0x00275C30u
#define G_AREA70A 0x0081070Au
#define G_CHAIN   0x007635C0u /* the z-bucketed packet chain */
#define G_XFORM   0x0081F8F0u /* 001CFB50's transform block */

#define SPR(x) (0x70000000u + (x))

static void vu_ok(Run *r, int status)
{
    if (status != EM_EE_FLOAT_OK)
        fault(r, EM_AREA00_HUD_FAULT_UNMEASURED, 0);
}

static const uint32_t VF0[4] = {0, 0, 0, F_ONE};

/* ------------------------------------------------------------------------
 * 001DEDB0(a0): the render context D_00275670 plus 0x2490 when a0 == 9,
 * else plus 0x2470. 001DEE80(a0, a1): that record, then the three words at
 * a1 copied to its +0x10, +0x14, +0x18 (each loaded, then stored). 001DEEC0
 * (a0, a1): that record's word +4 = a1. Both leave the record in v0.
 * ---------------------------------------------------------------------- */
static uint32_t f_1DEDB0(Run *r, uint32_t a0)
{
    uint32_t ctx = w32(r, G_CTX);
    return ctx + (a0 == 9u ? 0x2490u : 0x2470u);
}

static uint32_t f_1DEE80(Run *r, uint32_t a0, uint32_t a1)
{
    uint32_t rec = f_1DEDB0(r, a0);
    unsigned k;
    for (k = 0; k < 3; k++)
        st32(r, rec + 0x10 + 4 * k, w32(r, a1 + 4 * k));
    return rec;
}

static uint32_t f_1DEEC0(Run *r, uint32_t a0, uint32_t a1)
{
    uint32_t rec = f_1DEDB0(r, a0);
    st32(r, rec + 4, a1);
    return rec;
}

/* ------------------------------------------------------------------------
 * 001DF020(a0, a1), frame 0x80: the two 0x20-byte templates D_002534C0 and
 * D_002534E0 are copied to the frame at +0x40 and +0x60 (each as two
 * quadword loads, then two stores); the result is the word at
 * D_00275670 + a0 * 4 + 0x10. Then, all with a0: 001D1FF0(a0, 2),
 * 001D7510(a0, 0, 0), 001D1F80(a0, 0, 1), 001D7000(a0, 0),
 * 001D6DD0(a0, 0x7000, 0x7900), 001D7A80(a0, 0x40, sp+0x40, sp+0x60; t0 =
 * a1), 001D1F20(a0).
 * ---------------------------------------------------------------------- */
static uint64_t f_1DF020(Run *r, uint32_t sp, uint64_t a0, uint64_t a1)
{
    uint32_t q0[4], q1[4], ctx, slot;
    sp -= 0x80;
    ld128(r, 0x002534C0u, q0);
    ld128(r, 0x002534D0u, q1);
    st128(r, sp + 0x40, q0);
    st128(r, sp + 0x50, q1);
    ld128(r, 0x002534E0u, q0);
    ld128(r, 0x002534F0u, q1);
    st128(r, sp + 0x60, q0);
    st128(r, sp + 0x70, q1);
    ctx = w32(r, G_CTX);
    slot = w32(r, ((uint32_t)a0 << 2) + ctx + 0x10);
    call2(r, sp, FN_1D1FF0, a0, 2);
    call3(r, sp, FN_1D7510, a0, 0, 0);
    call3(r, sp, FN_1D1F80, a0, 0, 1);
    call2(r, sp, FN_1D7000, a0, 0);
    call3(r, sp, FN_1D6DD0, a0, 0x7000, 0x7900);
    calli(r, sp, FN_1D7A80, 5, a0, 0x40, reg(sp + 0x40), reg(sp + 0x60), a1, 0);
    call1(r, sp, FN_1D1F20, a0);
    return reg(slot);
}

/* ------------------------------------------------------------------------
 * The header 001DF110 and 001DF5A0 both write after their call: with
 * t = D_00275670 read once and the cursor c = word t+0x1C read before each
 * access: byte c+3 = 0x60, word c+4 = 0, halfword c+0 = 0, then t+0x1C =
 * c + 0x10; then 001CB760(D_007635C0, 0xFFF000, the call's v0, the first
 * cursor).
 * ---------------------------------------------------------------------- */
static void header_tail(Run *r, uint32_t sp, uint64_t v0)
{
    uint32_t t = w32(r, G_CTX);
    uint32_t first = w32(r, t + 0x1C);
    st8(r, first + 3, 0x60);
    st32(r, w32(r, t + 0x1C) + 4, 0);
    st16(r, w32(r, t + 0x1C), 0);
    st32(r, t + 0x1C, w32(r, t + 0x1C) + 0x10);
    call4(r, sp, FN_1CB760, G_CHAIN, 0xFFF000u, v0, reg(first));
}

/* ------------------------------------------------------------------------
 * 001DF180(a0; f12 = amplitude h), frame 0x10F0. The result is the word at
 * D_00275670 + a0 * 4 + 0x10, read first. Calls: 001D6B60(a0,
 * D_0027568C, 8, 8, 0x26E860), 001D6BA0(a0, D_0027568C re-read, 8, 8, 0,
 * 0), 001D1FF0(a0, 3), 001D2040(a0, 0), 001D7080(a0, 0x80808080; f12 = 1).
 * Grid (16 x 16 quadwords at frame +0xD0, row i at +0xD0 + 0x100 i): u =
 * i / 15, su = 0011DF78(2u - 1), su2 = su * su; for each j: v = j / 15,
 * sv = 0011DF78(2v - 1), k = (sv * sv + su2) * h; two LCG draws r1, r2
 * scaled by 2**-31; x = 2**-9 + (v + k * (-1 + 2 r1)), y = 0x3B924925 +
 * (u + k * (-1 + 2 r2)), z = 1.0; the w word is not written (it keeps what
 * the stack held).
 * Rows i = 0..14: p = D_00275670 + a0 * 4 (re-read per row), c = word
 * p+0x10 re-read before each access: byte c+3 = 0x10, word c+4 = 0,
 * halfword c+0 = 0x42; p+0x10 = c + 0x430; quadword c+0x10 = 0, word c+0x1C
 * = 0x50000041, doubleword c+0x20 = 0x400A400000008010, c+0x28 = 0x4242.
 * The two ST quadwords at frame +0x10D0 / +0x10E0 = (0x7000, ((0xE0 i) / 15
 * + 0x790) << 4, 0xFFFFFF, 0) and the same with i + 1 (the division by 15
 * is the signed one the original computes). Then 16 columns: 00102948 of
 * grid[i][j], the first ST, grid[i+1][j], the second ST to c+0x30 + 0x40 j;
 * each ST's first word = float_to_int(float(word) + 546.13336).
 * Then 001D1F20(a0), 001D1FF0(a0, 1).
 * ---------------------------------------------------------------------- */
static uint32_t div15(uint32_t x)
{
    int64_t prod = (int64_t)(int32_t)0x88888889u * (int64_t)(int32_t)x;
    uint32_t hi = (uint32_t)((uint64_t)prod >> 32);
    uint32_t t = (uint32_t)((int32_t)(hi + x) >> 3);
    return t + (x >> 31);
}

static uint64_t f_1DF180(Run *r, uint32_t sp, uint64_t a0, uint32_t h)
{
    uint32_t ctx, slot, i, j;
    uint32_t off = (uint32_t)a0 << 2;
    sp -= 0x10F0;
    ctx = w32(r, G_CTX);
    {
        uint32_t k8c = w32(r, G_27568C);
        slot = w32(r, off + ctx + 0x10);
        calli(r, sp, FN_1D6B60, 5, a0, reg(k8c), 8, 8, 0x0026E860u, 0);
    }
    calli(r, sp, FN_1D6BA0, 6, a0, reg(w32(r, G_27568C)), 8, 8, 0, 0);
    call2(r, sp, FN_1D1FF0, a0, 3);
    call2(r, sp, FN_1D2040, a0, 0);
    call2f(r, sp, FN_1D7080, a0, reg(0x80808080u), F_ONE);

    for (i = 0; i < 16; i++) {
        uint32_t u = em_ee_div_bits(em_ee_cvt_s_w_bits(i), F_FIFTEEN);
        uint32_t su = fcall(r, sp, FN_11DF78, em_ee_sub_bits(em_ee_mul_bits(F_TWO, u), F_ONE));
        uint32_t su2 = em_ee_mul_bits(su, su);
        for (j = 0; j < 16; j++) {
            uint32_t at = sp + 0xD0 + 0x100 * i + 0x10 * j;
            uint32_t v = em_ee_div_bits(em_ee_cvt_s_w_bits(j), F_FIFTEEN);
            uint32_t sv = fcall(r, sp, FN_11DF78, em_ee_sub_bits(em_ee_mul_bits(F_TWO, v), F_ONE));
            uint32_t k = em_ee_add_bits(em_ee_mul_bits(sv, sv), su2);
            uint32_t r1, r2, a, b;
            k = em_ee_mul_bits(k, h);
            r1 = (uint32_t)calli(r, sp, FN_RAND, 0, 0, 0, 0, 0, 0, 0);
            r1 = em_ee_mul_bits(F_2POWM31, em_ee_cvt_s_w_bits(r1));
            r2 = (uint32_t)calli(r, sp, FN_RAND, 0, 0, 0, 0, 0, 0, 0);
            r2 = em_ee_mul_bits(F_2POWM31, em_ee_cvt_s_w_bits(r2));
            a = em_ee_add_bits(F_NEG_ONE, em_ee_mul_bits(F_TWO, r1));
            a = em_ee_add_bits(v, em_ee_mul_bits(k, a));
            b = em_ee_add_bits(F_NEG_ONE, em_ee_mul_bits(F_TWO, r2));
            b = em_ee_add_bits(u, em_ee_mul_bits(k, b));
            st32(r, at, em_ee_add_bits(F_1_512, a));
            st32(r, at + 4, em_ee_add_bits(F_1_224, b));
            st32(r, at + 8, F_ONE);
        }
    }

    for (i = 0; i < 15; i++) {
        uint32_t p = w32(r, G_CTX) + off;
        uint32_t c, rows = 0xE0u * i, next = 0xE0u * (i + 1), out;
        st8(r, w32(r, p + 0x10) + 3, 0x10);
        st32(r, w32(r, p + 0x10) + 4, 0);
        st16(r, w32(r, p + 0x10), 0x42);
        c = w32(r, p + 0x10);
        st32(r, p + 0x10, c + 0x430);
        {
            static const uint32_t zero[4] = {0, 0, 0, 0};
            st128(r, c + 0x10, zero);
        }
        st32(r, c + 0x1C, 0x50000041u);
        st64(r, c + 0x20, UINT64_C(0x400A400000008010));
        st64(r, c + 0x28, 0x4242u);
        st32(r, sp + 0x10D0, 0x7000);
        st32(r, sp + 0x10E0, 0x7000);
        st32(r, sp + 0x10D4, (div15(rows) + 0x790u) << 4);
        st32(r, sp + 0x10E4, (div15(next) + 0x790u) << 4);
        st32(r, sp + 0x10D8, 0xFFFFFFu);
        st32(r, sp + 0x10E8, 0xFFFFFFu);
        st32(r, sp + 0x10DC, 0);
        st32(r, sp + 0x10EC, 0);
        out = c + 0x30;
        for (j = 0; j < 16; j++) {
            uint32_t v;
            call2(r, sp, FN_COPY_QW, reg(out), reg(sp + 0xD0 + 0x100 * i + 0x10 * j));
            call2(r, sp, FN_COPY_QW, reg(out + 0x10), reg(sp + 0x10D0));
            call2(r, sp, FN_COPY_QW, reg(out + 0x20), reg(sp + 0xD0 + 0x100 * (i + 1) + 0x10 * j));
            call2(r, sp, FN_COPY_QW, reg(out + 0x30), reg(sp + 0x10E0));
            v = em_ee_add_bits(em_ee_cvt_s_w_bits(w32(r, sp + 0x10D0)), F_546);
            st32(r, sp + 0x10D0, (uint32_t)icall(r, sp, FN_FLOAT_TO_INT, v));
            v = em_ee_add_bits(em_ee_cvt_s_w_bits(w32(r, sp + 0x10E0)), F_546);
            st32(r, sp + 0x10E0, (uint32_t)icall(r, sp, FN_FLOAT_TO_INT, v));
            out += 0x40;
        }
    }
    call1(r, sp, FN_1D1F20, a0);
    call2(r, sp, FN_1D1FF0, a0, 1);
    return reg(slot);
}

/* ------------------------------------------------------------------------
 * 001E2800(mode, pa, ca, pb, cb), frame 0x40, VU0.
 * Each point p (pa to 0x70003600, then pb to 0x70003610): vf1 = p; vf2 =
 * vf28 * x + vf29 * y + vf30 * z + vf31 (ACC chain); Q = 1 / vf2.w;
 * vf2.xy *= Q; vf3 = (1.0, 0, ..); vf2.w -= 1.0; Q = 1 / vf2.w; vf2.z *=
 * Q; vf2.w = min(max-free ramp vf23.z + vf23.w * vf2.w, vf23.x), then
 * max(.., 0); the quadword stored is vf2 converted to 28.4 fixed point (all
 * four lanes). (The original also stores the vector after the first
 * transform to its own frame; nothing reads it.)
 * Then the colours: quadword ca to 0x70003620, cb to 0x70003630.
 * For mode != 0, the fade of each point in turn (A: w 0x7000360C with the
 * colour 0x70003620; B: 0x7000361C with 0x70003630): w >>= 4 (arithmetic),
 * stored; re-read: not < 0x100 -> 0xFF; re-read: < 0 -> 0. Mode 1: the
 * colour's alpha word = (alpha * w) >> 8; modes 2 and 3: its three colour
 * words (w read once) = (c * w) >> 8 (32-bit products); then (every mode)
 * w = 0xFF0.
 * Packet: z = word 0x70003608; d = 001CB5F0(D_007635C0, z, 4); quadwords
 * 0x70003620, 0x70003600, 0x70003630, 0x70003610 to d+0x00..+0x30;
 * 001CB6B0(D_007635C0, z re-read, 2, D_00253720); 001CB900(D_007635C0, z
 * re-read, mode).
 * ---------------------------------------------------------------------- */
static void project(Run *r, uint32_t src, uint32_t dst)
{
    EmArea00HudVu *v = &r->s->vu;
    uint32_t out[4];
    unsigned k;
    ld128(r, src, v->vf[1]);
    vu_ok(r, em_vu_vec_bits(EM_VU_MULABC, 15, 0, v->vf[28], v->vf[1], 0, NULL, v->acc));
    vu_ok(r, em_vu_vec_bits(EM_VU_MADDABC, 15, 1, v->vf[29], v->vf[1], 0, v->acc, v->acc));
    vu_ok(r, em_vu_vec_bits(EM_VU_MADDABC, 15, 2, v->vf[30], v->vf[1], 0, v->acc, v->acc));
    vu_ok(r, em_vu_vec_bits(EM_VU_MADDBC, 15, 3, v->vf[31], VF0, 0, v->acc, v->vf[2]));
    vu_ok(r, em_vu_div_bits(VF0[3], v->vf[2][3], 3, 3, &v->q));
    vu_ok(r, em_vu_vec_bits(EM_VU_MULQ, 12, -1, v->vf[2], NULL, v->q, NULL, v->vf[2]));
    v->vf[3][0] = F_ONE;  /* the low 64 bits of the moved register */
    v->vf[3][1] = 0;
    vu_ok(r, em_vu_vec_bits(EM_VU_SUBBC, 1, 0, v->vf[2], v->vf[3], 0, NULL, v->vf[2]));
    vu_ok(r, em_vu_div_bits(VF0[3], v->vf[2][3], 3, 3, &v->q));
    vu_ok(r, em_vu_vec_bits(EM_VU_MULQ, 2, -1, v->vf[2], NULL, v->q, NULL, v->vf[2]));
    vu_ok(r, em_vu_vec_bits(EM_VU_MULABC, 1, 2, VF0, v->vf[23], 0, NULL, v->acc));
    vu_ok(r, em_vu_vec_bits(EM_VU_MADDBC, 1, 3, v->vf[23], v->vf[2], 0, v->acc, v->vf[2]));
    v->vf[2][3] = em_vu_min_bits(v->vf[2][3], v->vf[23][0]);
    v->vf[2][3] = em_vu_max_bits(v->vf[2][3], VF0[0]);
    for (k = 0; k < 4; k++)
        v->vf[2][k] = em_vu_ftoi4_bits(v->vf[2][k]);
    for (k = 0; k < 4; k++)
        out[k] = v->vf[2][k];
    st128(r, dst, out);
}

static void fade(Run *r, uint64_t mode, uint32_t wa, uint32_t col)
{
    int32_t w = (int32_t)w32(r, wa) >> 4;
    st32(r, wa, (uint32_t)w);
    if (!((int32_t)w32(r, wa) < 0x100))
        st32(r, wa, 0xFF);
    if ((int32_t)w32(r, wa) < 0)
        st32(r, wa, 0);
    if (mode == 1) {
        uint32_t a = w32(r, col + 0xC);
        uint32_t f = w32(r, wa);
        st32(r, col + 0xC, (uint32_t)((int32_t)(a * f) >> 8));
    } else if (mode == 3 || mode == 2) {
        uint32_t c0 = w32(r, col);
        uint32_t f = w32(r, wa);
        st32(r, col, (uint32_t)((int32_t)(c0 * f) >> 8));
        c0 = w32(r, col + 4);
        st32(r, col + 4, (uint32_t)((int32_t)(c0 * f) >> 8));
        c0 = w32(r, col + 8);
        st32(r, col + 8, (uint32_t)((int32_t)(c0 * f) >> 8));
    }
    st32(r, wa, 0xFF0);
}

static void f_1E2800(Run *r, uint32_t sp, uint64_t mode, uint32_t pa, uint32_t ca, uint32_t pb, uint32_t cb)
{
    uint32_t q[4], d, z;
    sp -= 0x40;
    project(r, pa, SPR(0x3600));
    project(r, pb, SPR(0x3610));
    ld128(r, ca, q);
    st128(r, SPR(0x3620), q);
    ld128(r, cb, q);
    st128(r, SPR(0x3630), q);
    if (mode != 0)
        fade(r, mode, SPR(0x360C), SPR(0x3620));
    if (mode != 0)
        fade(r, mode, SPR(0x361C), SPR(0x3630));
    z = w32(r, SPR(0x3608));
    d = (uint32_t)call3(r, sp, FN_1CB5F0, G_CHAIN, reg(z), 4);
    ld128(r, SPR(0x3620), q);
    st128(r, d, q);
    ld128(r, SPR(0x3600), q);
    st128(r, d + 0x10, q);
    ld128(r, SPR(0x3630), q);
    st128(r, d + 0x20, q);
    ld128(r, SPR(0x3610), q);
    st128(r, d + 0x30, q);
    call4(r, sp, FN_1CB6B0, G_CHAIN, reg(w32(r, SPR(0x3608))), 2, 0x00253720u);
    call3(r, sp, FN_1CB900, G_CHAIN, reg(w32(r, SPR(0x3608))), mode);
}

/* ------------------------------------------------------------------------
 * 001E2BA0(start, end, colour), frame 0xA0, VU0. Frame locals: +0x50 the
 * previous RGBA (4 words), +0x60 the RGBA, +0x70 the current point, +0x80
 * the step, +0x90 the next point.
 * m = 001CD370(2); vf24..vf27 = its four rows; vf28..vf31 = the four rows
 * at 0x70003AC0; vf23 = the quadword at D_00275670 + 0xA0.
 * step = end - start (001028D0); len = 0011E748(x*x + y*y + z*z of the step,
 * as ADDA of the first two squares and MADD of the third); step /= 32
 * (00102870); current = start (00102948); previous RGBA = 0 (words +0x5C,
 * +0x58, +0x54, +0x50 in that order). phase = float(rand) / 2**31 * 2 pi;
 * dphase = (0.1 * len) / 4. Clip: the current point through vf24..vf27,
 * vclipw.xyz against its w; s = CLIP & 0x3F.
 * 32 steps: shade = 0011DF78(0011E2A8(phase)); RGBA[k] = float_to_int(255 *
 * (colour[k] * shade)) for k = 0..3 (colour re-read each step); next =
 * current + step (001028B8); s = ((s << 6) & 0xFC0) | the next point's
 * clip flags; when s == 0: 001E2800(2, current, previous RGBA, next, RGBA);
 * current = next and previous = RGBA (00102948 each); phase += dphase, and
 * when not below 2 pi it drops by 2 pi.
 * ---------------------------------------------------------------------- */
static uint32_t clip_point(Run *r, uint32_t src)
{
    EmArea00HudVu *v = &r->s->vu;
    uint32_t wmag, flags = 0;
    unsigned k;
    ld128(r, src, v->vf[1]);
    vu_ok(r, em_vu_vec_bits(EM_VU_MULABC, 15, 0, v->vf[24], v->vf[1], 0, NULL, v->acc));
    vu_ok(r, em_vu_vec_bits(EM_VU_MADDABC, 15, 1, v->vf[25], v->vf[1], 0, v->acc, v->acc));
    vu_ok(r, em_vu_vec_bits(EM_VU_MADDABC, 15, 2, v->vf[26], v->vf[1], 0, v->acc, v->acc));
    vu_ok(r, em_vu_vec_bits(EM_VU_MADDBC, 15, 3, v->vf[27], VF0, 0, v->acc, v->vf[2]));
    /* the clip test of x, y, z against |w| on DAZ'd magnitudes; exponent 255 unmeasured */
    for (k = 0; k < 4; k++)
        if (em_eei_exp(v->vf[2][k]) == 0xFFu)
            fault(r, EM_AREA00_HUD_FAULT_UNMEASURED, 0);
    wmag = em_eei_daz(v->vf[2][3]) & 0x7FFFFFFFu;
    for (k = 0; k < 3; k++) {
        uint32_t x = em_eei_daz(v->vf[2][k]);
        if ((x & 0x7FFFFFFFu) > wmag)
            flags |= ((x >> 31) ? 2u : 1u) << (2 * k);
    }
    v->clip = ((v->clip << 6) | flags) & 0xFFFFFFu;
    return v->clip & 0x3Fu;
}

static void f_1E2BA0(Run *r, uint32_t sp, uint64_t a0, uint64_t a1, uint64_t a2)
{
    EmArea00HudVu *v = &r->s->vu;
    uint32_t m, ctx, x, y, z, len, phase, dphase, s, i, k;
    sp -= 0xA0;
    m = (uint32_t)call1(r, sp, FN_1CD370, 2);
    for (k = 0; k < 4; k++)
        ld128(r, m + 0x10 * k, v->vf[24 + k]);
    for (k = 0; k < 4; k++)
        ld128(r, SPR(0x3AC0) + 0x10 * k, v->vf[28 + k]);
    ctx = w32(r, G_CTX);
    ld128(r, ctx + 0xA0, v->vf[23]);
    call3(r, sp, FN_VSUB, reg(sp + 0x80), a1, a0);
    x = w32(r, sp + 0x80);
    y = w32(r, sp + 0x84);
    z = w32(r, sp + 0x88);
    x = em_ee_mul_bits(x, x);
    y = em_ee_mul_bits(y, y);
    len = fcall(r, sp, FN_11E748, em_ee_madd_bits(em_ee_adda_bits(x, y), z, z));
    call2f(r, sp, FN_VDIV, reg(sp + 0x80), reg(sp + 0x80), F_32);
    call2(r, sp, FN_COPY_QW, reg(sp + 0x70), a0);
    st32(r, sp + 0x5C, 0);
    st32(r, sp + 0x58, 0);
    st32(r, sp + 0x54, 0);
    st32(r, sp + 0x50, 0);
    phase = (uint32_t)calli(r, sp, FN_RAND, 0, 0, 0, 0, 0, 0, 0);
    phase = em_ee_div_bits(em_ee_cvt_s_w_bits(phase), F_2POW31);
    phase = em_ee_mul_bits(phase, F_2PI);
    dphase = em_ee_div_bits(em_ee_mul_bits(F_TENTH, len), F_FOUR);
    s = clip_point(r, sp + 0x70);
    for (i = 0; i < 32; i++) {
        uint32_t shade = fcall(r, sp, FN_11DF78, fcall(r, sp, FN_11E2A8, phase));
        for (k = 0; k < 4; k++) {
            uint32_t c = em_ee_mul_bits(w32(r, (uint32_t)a2 + 4 * k), shade);
            st32(r, sp + 0x60 + 4 * k, (uint32_t)icall(r, sp, FN_FLOAT_TO_INT, em_ee_mul_bits(F_255, c)));
        }
        call3(r, sp, FN_VADD, reg(sp + 0x90), reg(sp + 0x70), reg(sp + 0x80));
        s = (s << 6) & 0xFC0u;
        s |= clip_point(r, sp + 0x90);
        if (s == 0)
            f_1E2800(r, sp, 2, sp + 0x70, sp + 0x50, sp + 0x90, sp + 0x60);
        call2(r, sp, FN_COPY_QW, reg(sp + 0x70), reg(sp + 0x90));
        call2(r, sp, FN_COPY_QW, reg(sp + 0x50), reg(sp + 0x60));
        phase = em_ee_add_bits(phase, dphase);
        if (!em_ee_c_lt_bits(phase, F_2PI))
            phase = em_ee_sub_bits(phase, F_2PI);
    }
}

/* ------------------------------------------------------------------------
 * 001E2E80(e), frame 0x60: b = e + 0x1F0, amount = 0, hit = 0; by state
 * byte e+4: 2 and 3: 001AFC10(e); 0: set-up, then 1; 1: run; others:
 * nothing.
 * Set-up: b+0x20 = 0; b+0x24, +0x28, +0x2C = float(rand) / 2**31 (three
 * draws; each stored before the next draw); b+0x30 = 0, +0x34 = 0, +0x38 =
 * 150; b+0x10 = e+0xB0 (00102948); b = e+0xC0 * float e+0x20 (00102900);
 * bytes e+4 = 1, e+5 = 0.
 * Run: e+0xB0 += b (001028B8); float e+0xB4 += b+0x30. Kind byte e+0xD:
 * 3: b+0x30 += -0.01; 4: word b+0x34 += 1, and when not < 26 (re-read)
 * b+0x30 += -0.01. Word b+0x38 -= 1; below 0 (re-read) state 3.
 * 0x700038A0 = b+0x10 - e+0xB0 (001028D0), normalised (00102760);
 * 001CD390(e+0xD0, 0x700038A0). hit |= 0019A570(b+0x10, e+0xB0, 6, 0) (64-bit
 * v0); when hit and the byte +0x1A of the record at word 0x700031D0 is 0x32,
 * hit = 0. When hit == 0: hit |= 0019AA80(b+0x10, e+0xB0, 0x20); when set and
 * the target t = word 0x700031D4 has byte t+0 == 1: amount from the tables
 * below by D_0081070A == 0 or not, the kind (re-read) and the halfword
 * e+0x94 (signed; re-read); a nonzero amount: t+0x22C = amount, byte t+0 |=
 * 2, t+0x70 = e+0xB0 - b+0x10 (001028D0), normalised in place (00102760),
 * 001EFD20(0x8000001B, e+0xB0).
 * When hit != 0: 001031E0(0x700038B0, 0x700031B0); 0x700038BC = 1.0;
 * 00102918(0x700036A0, e+0xD0, 0x700038B0); 001EFEB0(0x8000002A,
 * 0x700036A0); 001F02C0(0x700036D0, 0x1B3; 500.0); state 3.
 * When byte e+5 == 0: e+5 = 1; 001029C0(0x700036A0); 00102BB0(0x700036A0,
 * 0x700036A0; pi); 001026D0(0x700036A0, e+0xD0, 0x700036A0);
 * 00102918(0x700036A0, 0x700036A0, e+0xB0); 001EFEB0(0x80000029, 0x700036A0).
 * 00102918(e+0xD0, e+0xD0, e+0xB0). Kind (re-read) 3: 001D04B0(e+0xD0, 1,
 * D_00253740; b+0x20, b+0x24), (.., 1, D_002537D0; b+0x20, b+0x28), (.., 0,
 * D_00253860; b+0x20, b+0x2C); kind 4: the same with D_002538F0, D_00253980,
 * D_00253A10 (each float re-read). b+0x20 += 0.05 (stored); when not <= 2
 * it is re-read and drops by 1. b+0x10 = e+0xB0 (00102948).
 * ---------------------------------------------------------------------- */
static const uint32_t AMOUNT_A0[9] = {  /* D_0081070A == 0, kind 3, h = 10..18 */
    0x41200000u, 0x41400000u, 0x41A00000u, 0x41C80000u, 0x41700000u,
    0x41C80000u, 0x41A00000u, 0x41A00000u, 0x41C80000u,
};
static const uint32_t AMOUNT_A1[9] = {  /* D_0081070A != 0, kind 3 */
    0x41700000u, 0x41880000u, 0x41A00000u, 0x41F00000u, 0x41A00000u,
    0x41C80000u, 0x41C80000u, 0x41F00000u, 0x41C80000u,
};

static uint32_t hazard_amount(Run *r, uint32_t e)
{
    int area = u8(r, G_AREA70A) != 0;
    uint32_t kind = u8(r, e + 0xD);
    int32_t h;
    if (kind == 4) {
        h = s16(r, e + 0x94);
        if (h == 0x13)
            return 0x41200000u;                     /* 10 */
        if (h == 0x14)
            return area ? 0x41C80000u : 0x41200000u; /* 25 / 10 */
        return 0;
    }
    if (kind == 3) {
        uint32_t idx = (uint32_t)(s16(r, e + 0x94) - 10);
        if (idx < 9)
            return area ? AMOUNT_A1[idx] : AMOUNT_A0[idx];
    }
    return 0;
}

static void f_1E2E80(Run *r, uint32_t sp, uint64_t ereg)
{
    uint32_t e = (uint32_t)ereg, b = e + 0x1F0, amount = 0, st, kind;
    uint64_t hit = 0;
    sp -= 0x60;
    st = u8(r, e + 4);
    if (st == 3 || st == 2) {
        call1(r, sp, FN_1AFC10, ereg);
        return;
    }
    if (st != 1 && st != 0)
        return;
    if (st == 0) {
        unsigned k;
        st32(r, b + 0x20, 0);
        for (k = 0; k < 3; k++) {
            uint32_t v = (uint32_t)calli(r, sp, FN_RAND, 0, 0, 0, 0, 0, 0, 0);
            st32(r, b + 0x24 + 4 * k, em_ee_div_bits(em_ee_cvt_s_w_bits(v), F_2POW31));
        }
        st32(r, b + 0x30, 0);
        st32(r, b + 0x34, 0);
        st32(r, b + 0x38, 150);
        call2(r, sp, FN_COPY_QW, reg(b + 0x10), reg(e + 0xB0));
        call2f(r, sp, FN_VSCALE_B, reg(b), reg(e + 0xC0), w32(r, e + 0x20));
        st8(r, e + 4, 1);
        st8(r, e + 5, 0);
    }
    call3(r, sp, FN_VADD, reg(e + 0xB0), reg(e + 0xB0), reg(b));
    {
        uint32_t d = w32(r, b + 0x30);
        st32(r, e + 0xB4, em_ee_add_bits(w32(r, e + 0xB4), d));
    }
    kind = u8(r, e + 0xD);
    if (kind == 4) {
        st32(r, b + 0x34, w32(r, b + 0x34) + 1);
        if (!((int32_t)w32(r, b + 0x34) < 0x1A))
            st32(r, b + 0x30, em_ee_add_bits(w32(r, b + 0x30), F_M0_01));
    } else if (kind == 3) {
        st32(r, b + 0x30, em_ee_add_bits(w32(r, b + 0x30), F_M0_01));
    }
    st32(r, b + 0x38, w32(r, b + 0x38) - 1);
    if ((int32_t)w32(r, b + 0x38) < 0)
        st8(r, e + 4, 3);
    call3(r, sp, FN_VSUB, reg(SPR(0x38A0)), reg(b + 0x10), reg(e + 0xB0));
    call2(r, sp, FN_NORM, reg(SPR(0x38A0)), reg(SPR(0x38A0)));
    call2(r, sp, FN_1CD390, reg(e + 0xD0), reg(SPR(0x38A0)));
    hit |= call4(r, sp, FN_19A570, reg(b + 0x10), reg(e + 0xB0), 6, 0);
    if (hit != 0 && u8(r, w32(r, SPR(0x31D0)) + 0x1A) == 0x32u)
        hit = 0;
    if (hit == 0) {
        hit |= call3(r, sp, FN_19AA80, reg(b + 0x10), reg(e + 0xB0), 0x20);
        if (hit != 0) {
            uint32_t t = w32(r, SPR(0x31D4));
            if (u8(r, t) == 1) {
                amount = hazard_amount(r, e);
                if (!em_ee_c_eq_bits(0, amount)) {
                    st32(r, t + 0x22C, amount);
                    st8(r, t, u8(r, t) | 2);
                    call3(r, sp, FN_VSUB, reg(t + 0x70), reg(e + 0xB0), reg(b + 0x10));
                    call2(r, sp, FN_NORM, reg(t + 0x70), reg(t + 0x70));
                    call2(r, sp, FN_1EFD20, reg(0x8000001Bu), reg(e + 0xB0));
                }
            }
        }
    }
    if (hit != 0) {
        call2(r, sp, FN_1031E0, reg(SPR(0x38B0)), reg(SPR(0x31B0)));
        st32(r, SPR(0x38BC), F_ONE);
        call3(r, sp, FN_102918, reg(SPR(0x36A0)), reg(e + 0xD0), reg(SPR(0x38B0)));
        call2(r, sp, FN_1EFEB0, reg(0x8000002Au), reg(SPR(0x36A0)));
        call2f(r, sp, FN_1F02C0, reg(SPR(0x36D0)), 0x1B3, F_500);
        st8(r, e + 4, 3);
    }
    if (u8(r, e + 5) == 0) {
        st8(r, e + 5, 1);
        call1(r, sp, FN_IDENT, reg(SPR(0x36A0)));
        call2f(r, sp, FN_ROT_B, reg(SPR(0x36A0)), reg(SPR(0x36A0)), F_PI);
        call3(r, sp, FN_MAT_MUL, reg(SPR(0x36A0)), reg(e + 0xD0), reg(SPR(0x36A0)));
        call3(r, sp, FN_102918, reg(SPR(0x36A0)), reg(SPR(0x36A0)), reg(e + 0xB0));
        call2(r, sp, FN_1EFEB0, reg(0x80000029u), reg(SPR(0x36A0)));
    }
    call3(r, sp, FN_102918, reg(e + 0xD0), reg(e + 0xD0), reg(e + 0xB0));
    kind = u8(r, e + 0xD);
    if (kind == 4 || kind == 3) {
        static const uint32_t tables[2][3] = {
            {0x00253740u, 0x002537D0u, 0x00253860u},  /* kind 3 */
            {0x002538F0u, 0x00253980u, 0x00253A10u},  /* kind 4 */
        };
        unsigned k;
        for (k = 0; k < 3; k++) {
            EmArea00HudCall c = mk(sp, FN_1D04B0);
            c.f[0] = w32(r, b + 0x20);
            c.f[1] = w32(r, b + 0x24 + 4 * k);
            c.nf = 2;
            c.na = 3;
            c.a[0] = reg(e + 0xD0);
            c.a[1] = k < 2 ? 1 : 0;
            c.a[2] = tables[kind == 4][k];
            (void)go(r, &c);
        }
    }
    {
        uint32_t t = em_ee_add_bits(w32(r, b + 0x20), F_0_05);
        st32(r, b + 0x20, t);
        if (!em_ee_c_le_bits(t, F_TWO))
            st32(r, b + 0x20, em_ee_sub_bits(w32(r, b + 0x20), F_ONE));
    }
    call2(r, sp, FN_COPY_QW, reg(b + 0x10), reg(e + 0xB0));
}

/* ------------------------------------------------------------------------
 * The effect-kind handlers (a0 = node + 0xD0, a1 = the depth key; the work
 * block is read through D_00275C34 each time it is needed).
 *
 * draw(a0, f13, f16): 001CFB50(D_0081F8F0, 0, a0; work+0x54, f13, 1.0,
 * 1e-6, f16). emit(table, mode, copy): 001CFBE0(a1, mode, table,
 * D_0081F8F0, copy).
 * The LCG step (from the work block w already read): n = word w+4;
 * f13 = float((n >> 16) & 0xFFFF) / 65535; w+4 = n * 37 + 11; then the
 * work block pointer is re-read for +0x54 and f13 += 0.0001.
 * The ease: v = work+8; work+8 = v + (target - v) / div; re-read (pointer
 * and value); below target -> target; stored.
 * ---------------------------------------------------------------------- */
static void draw(Run *r, uint32_t sp, uint64_t src, uint32_t f12, uint32_t f13, uint32_t f16)
{
    EmArea00HudCall c = mk(sp, FN_1CFB50);
    c.na = 3;
    c.a[0] = G_XFORM;
    c.a[1] = 0;
    c.a[2] = src;
    c.nf = 5;
    c.f[0] = f12;
    c.f[1] = f13;
    c.f[2] = F_ONE;
    c.f[3] = F_1EM6;
    c.f[4] = f16;
    (void)go(r, &c);
}

static void emit(Run *r, uint32_t sp, uint64_t depth, uint32_t mode, uint32_t table, uint32_t copy)
{
    calli(r, sp, FN_1CFBE0, 5, depth, mode, table, G_XFORM, copy, 0);
}

static void lcg_draw(Run *r, uint32_t sp, uint32_t w, uint64_t src, uint32_t f16)
{
    uint32_t n = w32(r, w + 4);
    uint32_t f13 = em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)((int32_t)n >> 16) & 0xFFFFu), F_65535);
    uint32_t f12;
    st32(r, w + 4, n * 37u + 11u);
    f12 = w32(r, w32(r, G_WORK) + 0x54);
    draw(r, sp, src, f12, em_ee_add_bits(f13, F_1EM4), f16);
}

static void ease(Run *r, uint32_t target, uint32_t div)
{
    uint32_t w = w32(r, G_WORK);
    uint32_t v = w32(r, w + 8);
    uint32_t d = em_ee_div_bits(em_ee_sub_bits(target, v), div);
    st32(r, w + 8, em_ee_add_bits(v, d));
    w = w32(r, G_WORK);
    v = w32(r, w + 8);
    if (em_ee_c_lt_bits(v, target))
        v = target;
    st32(r, w + 8, v);
}

/* The plain draw: 001CFB50 with work +0x54 and +0x5C (one pointer read). */
static void plain_draw(Run *r, uint32_t sp, uint64_t src, uint32_t f16)
{
    uint32_t w = w32(r, G_WORK);
    uint32_t f12 = w32(r, w + 0x54);
    draw(r, sp, src, f12, w32(r, w + 0x5C), f16);
}

/* The colour words of 001EAB50 / 001ECB00 (fade weight t already
 * computed): 0x70003600 = float_to_int(255 t), 0x70003604 =
 * float_to_int(192 t) << 8, 0x70003608 = float_to_int(192 t) << 16 (the
 * 192 t product computed once; 0x70003604 is stored before the third
 * call). The callers read them back in their own order and pass the 64-bit
 * colour register the original builds in t0: the three words
 * sign-extended, ORed with each other and with 0xFFFFFFFF80000000. */
static void sprite_colour(Run *r, uint32_t sp, uint32_t t)
{
    uint32_t t192;
    st32(r, SPR(0x3600), (uint32_t)icall(r, sp, FN_FLOAT_TO_INT, em_ee_mul_bits(F_255, t)));
    t192 = em_ee_mul_bits(F_192, t);
    st32(r, SPR(0x3604), (uint32_t)icall(r, sp, FN_FLOAT_TO_INT, t192) << 8);
    st32(r, SPR(0x3608), (uint32_t)icall(r, sp, FN_FLOAT_TO_INT, t192) << 16);
}

static uint64_t colour_reg(uint32_t c600, uint32_t c604, uint32_t c608)
{
    return reg(c600) | ((reg(c608) | UINT64_C(0xFFFFFFFF80000000)) | reg(c604));
}

static void sprite(Run *r, uint32_t sp, uint64_t a0, uint64_t packed, uint64_t colour, uint32_t size,
                   uint32_t f14)
{
    EmArea00HudCall c = mk(sp, FN_1CD520);
    c.na = 5;
    c.a[0] = 0;
    c.a[1] = 2;
    c.a[2] = reg((uint32_t)a0 + 0x30);
    c.a[3] = packed;
    c.a[4] = colour;
    c.nf = 3;
    c.f[0] = size;
    c.f[1] = size;
    c.f[2] = f14;
    (void)go(r, &c);
}

/* 001EAB50(a0, a1), frame 0x50: x = work+0x54 (the address kept); when x <
 * 0.5: t = (0.5 - x) / 0.5; the colour words; size = 1 + (4 * x re-read) /
 * 0.5; 001CD520(0, 2, a0 + 0x30, 0x20045B2599421E98, colour; size, size,
 * 2.0). Then draw(a0; +0x5C, 3.0) and emit(D_00255590, 0, 0). */
static void f_1EAB50(Run *r, uint32_t sp, uint64_t a0, uint64_t a1)
{
    uint32_t xa = w32(r, G_WORK) + 0x54;
    uint32_t x = w32(r, xa);
    sp -= 0x50;
    if (em_ee_c_lt_bits(x, F_HALF)) {
        uint32_t t = em_ee_div_bits(em_ee_sub_bits(F_HALF, x), F_HALF);
        uint32_t x2, c608, c604, c600, size;
        sprite_colour(r, sp, t);
        x2 = w32(r, xa);
        c608 = w32(r, SPR(0x3608));
        c604 = w32(r, SPR(0x3604));
        c600 = w32(r, SPR(0x3600));
        size = em_ee_div_bits(em_ee_mul_bits(F_FOUR, x2), F_HALF);
        size = em_ee_add_bits(F_ONE, size);
        sprite(r, sp, a0, UINT64_C(0x20045B2599421E98), colour_reg(c600, c604, c608), size, F_TWO);
    }
    plain_draw(r, sp, a0, F_THREE);
    emit(r, sp, a1, 0, 0x00255590u, 0);
}

/* 001EB980(a0, a1): draw(a0; +0x5C, 3.0), emit(D_00255FB0, 1, 0),
 * emit(D_00256040, 1, 0). */
static void f_1EB980(Run *r, uint32_t sp, uint64_t a0, uint64_t a1)
{
    sp -= 0x20;
    plain_draw(r, sp, a0, F_THREE);
    emit(r, sp, a1, 1, 0x00255FB0u, 0);
    emit(r, sp, a1, 1, 0x00256040u, 0);
}

/* 001EBBB0(a0, a1): draw(a0; +0x5C, 3.0), emit(D_002561F0, 1, 0). */
static void f_1EBBB0(Run *r, uint32_t sp, uint64_t a0, uint64_t a1)
{
    sp -= 0x20;
    plain_draw(r, sp, a0, F_THREE);
    emit(r, sp, a1, 1, 0x002561F0u, 0);
}

/* 001EBC30(a0, a1): 00102958(0x70003400, a0) (four quadwords); float
 * 0x70003434 += 2.5 (the work pointer read between its load and store);
 * draw(0x70003400; +0x5C, 3.0); then emit(D_00256310, 1, 0) when the word
 * +0x38 of the record D_00275C30 points at is 0, else emit(D_00256280, 1,
 * 0). */
static void f_1EBC30(Run *r, uint32_t sp, uint64_t a0, uint64_t a1)
{
    uint32_t v, w;
    sp -= 0x20;
    call2(r, sp, FN_COPY_QW4, reg(SPR(0x3400)), a0);
    v = w32(r, SPR(0x3434));
    w = w32(r, G_WORK);
    st32(r, SPR(0x3434), em_ee_add_bits(v, F_2P5));
    v = w32(r, w + 0x54);
    draw(r, sp, reg(SPR(0x3400)), v, w32(r, w + 0x5C), F_THREE);
    if (w32(r, w32(r, G_275C30) + 0x38) == 0)
        emit(r, sp, a1, 1, 0x00256310u, 0);
    else
        emit(r, sp, a1, 1, 0x00256280u, 0);
}

/* 001ECB00(a0, a1), frame 0x50: x = work+0x54 (the address kept); when x <
 * 1.0: t = 1 - x; the colour words; size = 6 + 6 * x (re-read);
 * 001CD520(0, 2, a0 + 0x30, 0x20045BA5154222DC, colour; size, size, 6.0).
 * Then three LCG draws with f16 = 6.0: emit(D_00256EE0, 0, 0),
 * emit(D_00256EE0, 2, 0), emit(D_00256F70, 1, 0). Ease to 0.05 by / 10. */
static void f_1ECB00(Run *r, uint32_t sp, uint64_t a0, uint64_t a1)
{
    static const struct { uint32_t table, mode; } E[3] = {
        {0x00256EE0u, 0}, {0x00256EE0u, 2}, {0x00256F70u, 1},
    };
    uint32_t xa = w32(r, G_WORK) + 0x54;
    uint32_t x = w32(r, xa);
    unsigned k;
    sp -= 0x50;
    if (em_ee_c_lt_bits(x, F_ONE)) {
        uint32_t t = em_ee_sub_bits(F_ONE, x);
        uint32_t x2, c608, c604, c600, size;
        sprite_colour(r, sp, t);
        c608 = w32(r, SPR(0x3608));
        x2 = w32(r, xa);
        c604 = w32(r, SPR(0x3604));
        c600 = w32(r, SPR(0x3600));
        size = em_ee_add_bits(F_SIX, em_ee_mul_bits(F_SIX, x2));
        sprite(r, sp, a0, UINT64_C(0x20045BA5154222DC), colour_reg(c600, c604, c608), size, F_SIX);
    }
    for (k = 0; k < 3; k++) {
        lcg_draw(r, sp, w32(r, G_WORK), a0, F_SIX);
        emit(r, sp, a1, E[k].mode, E[k].table, 0);
    }
    ease(r, F_0_05, F_TEN);
}

/* 001ECFB0(a0, a1): three LCG draws with f16 = 3.0, each followed by
 * emit(D_002571B0 + 0x90 k, 1, 0). Ease to 0.02 by / 10. */
static void f_1ECFB0(Run *r, uint32_t sp, uint64_t a0, uint64_t a1)
{
    unsigned k;
    sp -= 0x50;
    for (k = 0; k < 3; k++) {
        lcg_draw(r, sp, w32(r, G_WORK), a0, F_THREE);
        emit(r, sp, a1, 1, 0x002571B0u + 0x90u * k, 0);
    }
    ease(r, F_0_02, F_TEN);
}

/* The three-draw handlers 001ED7A0 / 001EEBA0 / 001EEEB0: two 8-word
 * blocks set to constants in address order (the last word of the second
 * block after the first work-pointer read), then three LCG draws with f16 =
 * 10.0, each followed by emit(table[k], 1, 1); ease to 0.02 by `div`. */
static void three_draws(Run *r, uint32_t sp, uint64_t a0, uint64_t a1, uint32_t blk_a, const uint32_t va[8],
                        uint32_t blk_b, const uint32_t vb[8], const uint32_t tables[3], uint32_t div)
{
    uint32_t w;
    unsigned k;
    sp -= 0x30;
    for (k = 0; k < 8; k++)
        st32(r, blk_a + 4 * k, va[k]);
    for (k = 0; k < 7; k++)
        st32(r, blk_b + 4 * k, vb[k]);
    w = w32(r, G_WORK);
    st32(r, blk_b + 0x1C, vb[7]);
    for (k = 0; k < 3; k++) {
        if (k > 0)
            w = w32(r, G_WORK);
        lcg_draw(r, sp, w, a0, F_TEN);
        emit(r, sp, a1, 1, tables[k], 1);
    }
    ease(r, F_0_02, div);
}

#define F16_ 0x41800000u
#define F12_ 0x41400000u
#define F24_ 0x41C00000u
#define F96_ 0x42C00000u
#define F112 0x42E00000u
#define F128 0x43000000u

/* 001ED7A0: D_00257380.. = (16, 12, 16, 0, 0, 0, 0, 0), D_00257410.. =
 * (128, 0, 128, 96, 96, 0, 96, 0), D_002574A0.. = the same; tables
 * D_00257360, D_002573F0, D_00257480; ease / 10. (Its first two blocks are
 * stored before the third; the third block's last word follows the first
 * work-pointer read.) */
static void f_1ED7A0(Run *r, uint32_t sp, uint64_t a0, uint64_t a1)
{
    static const uint32_t a[8] = {F16_, F12_, F16_, 0, 0, 0, 0, 0};
    static const uint32_t b[8] = {F128, 0, F128, F96_, F96_, 0, F96_, 0};
    static const uint32_t t[3] = {0x00257360u, 0x002573F0u, 0x00257480u};
    unsigned k;
    for (k = 0; k < 8; k++)
        st32(r, 0x00257380u + 4 * k, a[k]);
    three_draws(r, sp, a0, a1, 0x00257410u, b, 0x002574A0u, b, t, F_TEN);
}

/* 001EEBA0: D_00257380.. = (8, 24, 8, 0, 0, 0, 0, 0), D_002574A0.. = (24,
 * 112, 24, 128, 24, 112, 24, 0); tables D_00257360, D_00257870, D_00257480;
 * ease / 10. */
static void f_1EEBA0(Run *r, uint32_t sp, uint64_t a0, uint64_t a1)
{
    static const uint32_t a[8] = {F_EIGHT, F24_, F_EIGHT, 0, 0, 0, 0, 0};
    static const uint32_t b[8] = {F24_, F112, F24_, F128, F24_, F112, F24_, 0};
    static const uint32_t t[3] = {0x00257360u, 0x00257870u, 0x00257480u};
    three_draws(r, sp, a0, a1, 0x00257380u, a, 0x002574A0u, b, t, F_TEN);
}

/* 001EEEB0: the same blocks at D_00257530 / D_00257650; tables D_00257510,
 * D_00257900, D_00257630; ease / 8. */
static void f_1EEEB0(Run *r, uint32_t sp, uint64_t a0, uint64_t a1)
{
    static const uint32_t a[8] = {F_EIGHT, F24_, F_EIGHT, 0, 0, 0, 0, 0};
    static const uint32_t b[8] = {F24_, F112, F24_, F128, F24_, F112, F24_, 0};
    static const uint32_t t[3] = {0x00257510u, 0x00257900u, 0x00257630u};
    three_draws(r, sp, a0, a1, 0x00257530u, a, 0x00257650u, b, t, F_EIGHT);
}

/* ------------------------------------------------------------------------
 * Entries
 * ---------------------------------------------------------------------- */

#define ENTER(s, fn)                                                   \
    Run run;                                                           \
    if (!(s))                                                          \
        return -1;                                                     \
    if ((s)->fault != EM_AREA00_HUD_FAULT_NONE)                        \
        return -1;                                                     \
    run.s = (s);                                                       \
    (s)->fault_function = (fn);                                        \
    if (!(s)->regions) {                                               \
        (s)->fault = EM_AREA00_HUD_FAULT_NULL;                         \
        (s)->fault_address = 0;                                        \
        return -1;                                                     \
    }                                                                  \
    if (setjmp(run.out))                                               \
        return -1

static void put32(uint32_t *out, uint32_t v)
{
    if (out)
        *out = v;
}

int em_area00_hud_001DEDB0(EmArea00Hud *s, uint32_t a0, uint32_t *out)
{
    uint32_t v;
    ENTER(s, 0x001DEDB0u);
    v = f_1DEDB0(&run, a0);
    put32(out, v);
    return 0;
}

int em_area00_hud_001DEE80(EmArea00Hud *s, uint32_t a0, uint32_t a1, uint32_t *out)
{
    uint32_t v;
    ENTER(s, 0x001DEE80u);
    v = f_1DEE80(&run, a0, a1);
    put32(out, v);
    return 0;
}

int em_area00_hud_001DEEC0(EmArea00Hud *s, uint32_t a0, uint32_t a1, uint32_t *out)
{
    uint32_t v;
    ENTER(s, 0x001DEEC0u);
    v = f_1DEEC0(&run, a0, a1);
    put32(out, v);
    return 0;
}

int em_area00_hud_001DF020(EmArea00Hud *s, uint32_t a0, uint32_t a1, uint32_t *out)
{
    uint64_t v;
    ENTER(s, 0x001DF020u);
    v = f_1DF020(&run, s->sp, reg(a0), reg(a1));
    put32(out, (uint32_t)v);
    return 0;
}

int em_area00_hud_001DF110(EmArea00Hud *s, uint32_t a0)
{
    uint32_t sp;
    uint64_t v;
    ENTER(s, 0x001DF110u);
    sp = s->sp - 0x10;
    v = f_1DF020(&run, sp, 3, reg(a0));
    header_tail(&run, sp, v);
    return 0;
}

int em_area00_hud_001DF180(EmArea00Hud *s, uint32_t a0, uint32_t f12, uint32_t *out)
{
    uint64_t v;
    ENTER(s, 0x001DF180u);
    v = f_1DF180(&run, s->sp, reg(a0), f12);
    put32(out, (uint32_t)v);
    return 0;
}

int em_area00_hud_001DF5A0(EmArea00Hud *s, uint32_t f12)
{
    uint32_t sp;
    uint64_t v;
    ENTER(s, 0x001DF5A0u);
    sp = s->sp - 0x10;
    v = f_1DF180(&run, sp, 3, f12);
    header_tail(&run, sp, v);
    return 0;
}

int em_area00_hud_001E2800(EmArea00Hud *s, uint32_t mode, uint32_t pa, uint32_t ca, uint32_t pb, uint32_t cb)
{
    ENTER(s, 0x001E2800u);
    f_1E2800(&run, s->sp, reg(mode), pa, ca, pb, cb);
    return 0;
}

int em_area00_hud_001E2BA0(EmArea00Hud *s, uint32_t a0, uint32_t a1, uint32_t a2)
{
    ENTER(s, 0x001E2BA0u);
    f_1E2BA0(&run, s->sp, reg(a0), reg(a1), reg(a2));
    return 0;
}

int em_area00_hud_001E2E80(EmArea00Hud *s, uint32_t e)
{
    ENTER(s, 0x001E2E80u);
    f_1E2E80(&run, s->sp, reg(e));
    return 0;
}

#define HANDLER_ENTRY(addr, fn)                                        \
    int em_area00_hud_##addr(EmArea00Hud *s, uint32_t a0, uint32_t a1) \
    {                                                                  \
        ENTER(s, 0x##addr##u);                                         \
        fn(&run, s->sp, reg(a0), reg(a1));                             \
        return 0;                                                      \
    }

HANDLER_ENTRY(001EAB50, f_1EAB50)
HANDLER_ENTRY(001EB980, f_1EB980)
HANDLER_ENTRY(001EBBB0, f_1EBBB0)
HANDLER_ENTRY(001EBC30, f_1EBC30)
HANDLER_ENTRY(001ECB00, f_1ECB00)
HANDLER_ENTRY(001ECFB0, f_1ECFB0)
HANDLER_ENTRY(001ED7A0, f_1ED7A0)
HANDLER_ENTRY(001EEBA0, f_1EEBA0)
HANDLER_ENTRY(001EEEB0, f_1EEEB0)
