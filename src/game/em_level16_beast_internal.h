/* Memory and fail-stop plumbing, the design of em_level15_motion_internal.h. */
#ifndef EM_LEVEL16_BEAST_INTERNAL_H
#define EM_LEVEL16_BEAST_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level16_beast.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel16BeastHooks *h;
    EmLevel16BeastFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L16B;

static inline int l16b_latch(L16B *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL16_BEAST_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l16b_failed(const L16B *o) { return o->fault->code != EM_LEVEL16_BEAST_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l16b_at(L16B *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l16b_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l16b_latch(o, address, EM_LEVEL16_BEAST_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l16b_u8(L16B *o, uint32_t a) { return *l16b_at(o, a, 1); }
static inline uint32_t l16b_u16(L16B *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l16b_at(o, a, 2), 2);
    return v;
}
static inline int32_t l16b_s16(L16B *o, uint32_t a) { return (int16_t)l16b_u16(o, a); }
static inline uint32_t l16b_u32(L16B *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l16b_at(o, a, 4), 4);
    return v;
}
static inline int32_t l16b_s32(L16B *o, uint32_t a) { return (int32_t)l16b_u32(o, a); }
static inline int32_t l16b_s8(L16B *o, uint32_t a) { return (int8_t)l16b_u8(o, a); }
static inline uint64_t l16b_u64(L16B *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, l16b_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void l16b_q(L16B *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, l16b_at(o, a, 8), 8);
    memcpy(out + 8, l16b_at(o, a + 8, 8), 8);
}
static inline void l16b_wq(L16B *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(l16b_at(o, a, 8), in, 8);
    memcpy(l16b_at(o, a + 8, 8), in + 8, 8);
}

static inline void l16b_w8(L16B *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l16b_at(o, a, 1), &b, 1);
}
static inline void l16b_w16(L16B *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l16b_at(o, a, 2), &h, 2);
}
static inline void l16b_w32(L16B *o, uint32_t a, uint32_t v) { memcpy(l16b_at(o, a, 4), &v, 4); }
static inline void l16b_w64(L16B *o, uint32_t a, uint64_t v) { memcpy(l16b_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l16b_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l16b_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l16b_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L16B_ADD(a, b) em_ee_add_bits((a), (b))
#define L16B_SUB(a, b) em_ee_sub_bits((a), (b))
#define L16B_MUL(a, b) em_ee_mul_bits((a), (b))
#define L16B_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L16B_LE(a, b) em_ee_c_le_bits((a), (b))
#define L16B_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L16B_DIV(a, b) em_ee_div_bits((a), (b))
#define L16B_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define L16B_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define L16B_MULA(a, b) em_ee_mula_bits((a), (b))
#define L16B_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))

static inline int l16b_begin(L16B *o, const EmLevel16BeastHooks *h, EmLevel16BeastFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return l16b_failed(o) ? -1 : 0;
}

static inline int l16b_end(const L16B *o) { return l16b_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int l16b_callback(L16B *o, uint32_t fn, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_callback) return l16b_latch(o, fn, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? l16b_latch(o, fn, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int l16b_method(L16B *o, uint32_t self)
{
    uint32_t fn = l16b_u32(o, self + 0x4C);
    return l16b_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l16b_c_001000E0(L16B *o, uint64_t a0, uint64_t a1, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001000E0) return l16b_latch(o, 0x001000E0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001000E0(o->h->ctx, a0, a1, &out) < 0) return l16b_latch(o, 0x001000E0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_00102760(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00102760) return l16b_latch(o, 0x00102760u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_00102760(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x00102760u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_00102948(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00102948) return l16b_latch(o, 0x00102948u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_00102948(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x00102948u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_00102958(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00102958) return l16b_latch(o, 0x00102958u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_00102958(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x00102958u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001026A0(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001026A0) return l16b_latch(o, 0x001026A0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0) return l16b_latch(o, 0x001026A0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001028B8(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001028B8) return l16b_latch(o, 0x001028B8u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0) return l16b_latch(o, 0x001028B8u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001029C0(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001029C0) return l16b_latch(o, 0x001029C0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001029C0(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001029C0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_00102BB0(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00102BB0) return l16b_latch(o, 0x00102BB0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_00102BB0(o->h->ctx, a0, a1, l16b_float(a2)) < 0) return l16b_latch(o, 0x00102BB0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001031E0(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001031E0) return l16b_latch(o, 0x001031E0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001031E0(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x001031E0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_0011DE90(L16B *o, uint32_t a0, uint32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0011DE90) return l16b_latch(o, 0x0011DE90u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011DE90(o->h->ctx, l16b_float(a0), &out) < 0) return l16b_latch(o, 0x0011DE90u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = l16b_bits(out);
    return 0;
}

static inline int l16b_c_0011DF78(L16B *o, uint32_t a0, uint32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0011DF78) return l16b_latch(o, 0x0011DF78u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011DF78(o->h->ctx, l16b_float(a0), &out) < 0) return l16b_latch(o, 0x0011DF78u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = l16b_bits(out);
    return 0;
}

static inline int l16b_c_0011E2A8(L16B *o, uint32_t a0, uint32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return l16b_latch(o, 0x0011E2A8u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E2A8(o->h->ctx, l16b_float(a0), &out) < 0) return l16b_latch(o, 0x0011E2A8u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = l16b_bits(out);
    return 0;
}

static inline int l16b_c_0011E620(L16B *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0011E620) return l16b_latch(o, 0x0011E620u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E620(o->h->ctx, l16b_float(a0), l16b_float(a1), &out) < 0) return l16b_latch(o, 0x0011E620u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = l16b_bits(out);
    return 0;
}

static inline int l16b_c_0011E748(L16B *o, uint32_t a0, uint32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0011E748) return l16b_latch(o, 0x0011E748u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E748(o->h->ctx, l16b_float(a0), &out) < 0) return l16b_latch(o, 0x0011E748u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = l16b_bits(out);
    return 0;
}

static inline int l16b_c_00122BB8(L16B *o, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00122BB8) return l16b_latch(o, 0x00122BB8u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_00122BB8(o->h->ctx, &out) < 0) return l16b_latch(o, 0x00122BB8u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001281C0(L16B *o, uint32_t a0, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001281C0) return l16b_latch(o, 0x001281C0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001281C0(o->h->ctx, l16b_float(a0), &out) < 0) return l16b_latch(o, 0x001281C0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_00128350(L16B *o, uint32_t a0, uint64_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00128350) return l16b_latch(o, 0x00128350u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    uint64_t out = 0;
    if (o->h->w_00128350(o->h->ctx, l16b_float(a0), &out) < 0) return l16b_latch(o, 0x00128350u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_00131ED0(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00131ED0) return l16b_latch(o, 0x00131ED0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_00131ED0(o->h->ctx, a0) < 0) return l16b_latch(o, 0x00131ED0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_00182F90(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00182F90) return l16b_latch(o, 0x00182F90u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_00182F90(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x00182F90u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_0019A440(L16B *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0019A440) return l16b_latch(o, 0x0019A440u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_0019A440(o->h->ctx, a0, a1, a2, &out) < 0) return l16b_latch(o, 0x0019A440u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_0019A570(L16B *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0019A570) return l16b_latch(o, 0x0019A570u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_0019A570(o->h->ctx, a0, a1, a2, a3, &out) < 0) return l16b_latch(o, 0x0019A570u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_0019AB20(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0019AB20) return l16b_latch(o, 0x0019AB20u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_0019AB20(o->h->ctx, a0, a1, a2, a3, &out) < 0) return l16b_latch(o, 0x0019AB20u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_0019AD00(L16B *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0019AD00) return l16b_latch(o, 0x0019AD00u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_0019AD00(o->h->ctx, a0, a1, a2, &out) < 0) return l16b_latch(o, 0x0019AD00u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_0019B6C0(L16B *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0019B6C0) return l16b_latch(o, 0x0019B6C0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_0019B6C0(o->h->ctx, a0, a1, &out) < 0) return l16b_latch(o, 0x0019B6C0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001A7B80(L16B *o, uint32_t a0, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001A7B80) return l16b_latch(o, 0x001A7B80u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001A7B80(o->h->ctx, a0, &out) < 0) return l16b_latch(o, 0x001A7B80u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001AFC10(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001AFC10) return l16b_latch(o, 0x001AFC10u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001AFC10(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001AFC10u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B0D80(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B0D80) return l16b_latch(o, 0x001B0D80u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001B0D80(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001B0D80u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B10B0(L16B *o, uint32_t a0, int32_t a1, int32_t a2, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B10B0) return l16b_latch(o, 0x001B10B0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B10B0(o->h->ctx, a0, a1, a2, &out) < 0) return l16b_latch(o, 0x001B10B0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B1190(L16B *o, int32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B1190) return l16b_latch(o, 0x001B1190u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001B1190(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001B1190u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B1240(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B1240) return l16b_latch(o, 0x001B1240u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B1240(o->h->ctx, a0, l16b_float(a1), l16b_float(a2), &out) < 0) return l16b_latch(o, 0x001B1240u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = l16b_bits(out);
    return 0;
}

static inline int l16b_c_001B12B0(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B12B0) return l16b_latch(o, 0x001B12B0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B12B0(o->h->ctx, l16b_float(a0), l16b_float(a1), l16b_float(a2), &out) < 0) return l16b_latch(o, 0x001B12B0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = l16b_bits(out);
    return 0;
}

static inline int l16b_c_001B13F0(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B13F0) return l16b_latch(o, 0x001B13F0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B13F0(o->h->ctx, a0, a1, l16b_float(a2), &out) < 0) return l16b_latch(o, 0x001B13F0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B1470(L16B *o, uint32_t a0, uint32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B1470) return l16b_latch(o, 0x001B1470u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B1470(o->h->ctx, l16b_float(a0), &out) < 0) return l16b_latch(o, 0x001B1470u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = l16b_bits(out);
    return 0;
}

static inline int l16b_c_001B1560(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B1560) return l16b_latch(o, 0x001B1560u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B1560(o->h->ctx, a0, a1, l16b_float(a2), &out) < 0) return l16b_latch(o, 0x001B1560u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B15D0(L16B *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B15D0) return l16b_latch(o, 0x001B15D0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B15D0(o->h->ctx, a0, a1, &out) < 0) return l16b_latch(o, 0x001B15D0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = l16b_bits(out);
    return 0;
}

static inline int l16b_c_001B17A0(L16B *o, uint32_t a0, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B17A0) return l16b_latch(o, 0x001B17A0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B17A0(o->h->ctx, a0, &out) < 0) return l16b_latch(o, 0x001B17A0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B1EA0(L16B *o, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return l16b_latch(o, 0x001B1EA0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B1EA0(o->h->ctx, a0, a1, a2, a3, &out) < 0) return l16b_latch(o, 0x001B1EA0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B2140(L16B *o, uint32_t a0, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B2140) return l16b_latch(o, 0x001B2140u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B2140(o->h->ctx, a0, &out) < 0) return l16b_latch(o, 0x001B2140u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B2B10(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B2B10) return l16b_latch(o, 0x001B2B10u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001B2B10(o->h->ctx, a0, a1, a2) < 0) return l16b_latch(o, 0x001B2B10u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B2BF0(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B2BF0) return l16b_latch(o, 0x001B2BF0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B2BF0(o->h->ctx, a0, a1, a2, l16b_float(a3), &out) < 0) return l16b_latch(o, 0x001B2BF0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B2F70(L16B *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B2F70) return l16b_latch(o, 0x001B2F70u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B2F70(o->h->ctx, a0, a1, &out) < 0) return l16b_latch(o, 0x001B2F70u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B3250(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B3250) return l16b_latch(o, 0x001B3250u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B3250(o->h->ctx, a0, a1, l16b_float(a2), &out) < 0) return l16b_latch(o, 0x001B3250u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B32F0(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B32F0) return l16b_latch(o, 0x001B32F0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B32F0(o->h->ctx, a0, a1, l16b_float(a2), &out) < 0) return l16b_latch(o, 0x001B32F0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B3390(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B3390) return l16b_latch(o, 0x001B3390u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B3390(o->h->ctx, a0, a1, a2, l16b_float(a3), &out) < 0) return l16b_latch(o, 0x001B3390u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B3440(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B3440) return l16b_latch(o, 0x001B3440u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B3440(o->h->ctx, a0, a1, a2, l16b_float(a3), &out) < 0) return l16b_latch(o, 0x001B3440u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B3580(L16B *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B3580) return l16b_latch(o, 0x001B3580u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B3580(o->h->ctx, a0, a1, &out) < 0) return l16b_latch(o, 0x001B3580u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = l16b_bits(out);
    return 0;
}

static inline int l16b_c_001B37D0(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B37D0) return l16b_latch(o, 0x001B37D0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B37D0(o->h->ctx, a0, l16b_float(a1), l16b_float(a2), &out) < 0) return l16b_latch(o, 0x001B37D0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = l16b_bits(out);
    return 0;
}

static inline int l16b_c_001B3C30(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B3C30) return l16b_latch(o, 0x001B3C30u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001B3C30(o->h->ctx, a0, a1, a2, l16b_float(a3), l16b_float(a4)) < 0) return l16b_latch(o, 0x001B3C30u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B3F10(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B3F10) return l16b_latch(o, 0x001B3F10u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B3F10(o->h->ctx, a0, l16b_float(a1), l16b_float(a2), &out) < 0) return l16b_latch(o, 0x001B3F10u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001B4810(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B4810) return l16b_latch(o, 0x001B4810u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001B4810(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001B4810u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B4CF0(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B4CF0) return l16b_latch(o, 0x001B4CF0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001B4CF0(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001B4CF0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B5360(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B5360) return l16b_latch(o, 0x001B5360u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001B5360(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001B5360u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B55E0(L16B *o, uint32_t a0, int32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B55E0) return l16b_latch(o, 0x001B55E0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001B55E0(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x001B55E0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001C63E0(L16B *o, uint32_t a0, int32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001C63E0) return l16b_latch(o, 0x001C63E0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001C63E0(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x001C63E0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001C64F0(L16B *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001C64F0) return l16b_latch(o, 0x001C64F0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001C64F0(o->h->ctx, a0, l16b_float(a1), &out) < 0) return l16b_latch(o, 0x001C64F0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001C67E0(L16B *o, uint32_t a0, int32_t a1, uint32_t a2, uint32_t a3)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001C67E0) return l16b_latch(o, 0x001C67E0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001C67E0(o->h->ctx, a0, a1, l16b_float(a2), l16b_float(a3)) < 0) return l16b_latch(o, 0x001C67E0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001C68C0(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001C68C0) return l16b_latch(o, 0x001C68C0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001C68C0(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001C68C0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001C81C0(L16B *o, uint32_t a0, int32_t a1, int32_t a2, uint32_t a3)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001C81C0) return l16b_latch(o, 0x001C81C0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001C81C0(o->h->ctx, a0, a1, a2, a3) < 0) return l16b_latch(o, 0x001C81C0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001C82A0(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001C82A0) return l16b_latch(o, 0x001C82A0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001C82A0(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x001C82A0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001C9D50(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001C9D50) return l16b_latch(o, 0x001C9D50u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001C9D50(o->h->ctx, a0, a1, a2, l16b_float(a3)) < 0) return l16b_latch(o, 0x001C9D50u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001E8B90(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001E8B90) return l16b_latch(o, 0x001E8B90u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001E8B90(o->h->ctx, a0, l16b_float(a1)) < 0) return l16b_latch(o, 0x001E8B90u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001EFD90(L16B *o, int32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001EFD90) return l16b_latch(o, 0x001EFD90u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001EFD90(o->h->ctx, a0, a1, a2) < 0) return l16b_latch(o, 0x001EFD90u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001EFE00(L16B *o, int32_t a0, uint32_t a1, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001EFE00) return l16b_latch(o, 0x001EFE00u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001EFE00(o->h->ctx, a0, a1, &out) < 0) return l16b_latch(o, 0x001EFE00u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_001EFEB0(L16B *o, int32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001EFEB0) return l16b_latch(o, 0x001EFEB0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001EFEB0(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x001EFEB0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001FBD50(L16B *o, uint32_t a0, int32_t a1, int32_t a2, uint32_t a3)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001FBD50) return l16b_latch(o, 0x001FBD50u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_001FBD50(o->h->ctx, a0, a1, a2, l16b_float(a3)) < 0) return l16b_latch(o, 0x001FBD50u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_0021BE40(L16B *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0021BE40) return l16b_latch(o, 0x0021BE40u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_0021BE40(o->h->ctx, a0, a1, &out) < 0) return l16b_latch(o, 0x0021BE40u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_0021BED0(L16B *o, uint32_t a0, int32_t *result)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0021BED0) return l16b_latch(o, 0x0021BED0u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_0021BED0(o->h->ctx, a0, &out) < 0) return l16b_latch(o, 0x0021BED0u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l16b_c_0021BF90(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0021BF90) return l16b_latch(o, 0x0021BF90u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_0021BF90(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x0021BF90u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_0021C040(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0021C040) return l16b_latch(o, 0x0021C040u, EM_LEVEL16_BEAST_FAULT_NULL_WORKER);
    if (o->h->w_0021C040(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x0021C040u, EM_LEVEL16_BEAST_FAULT_WORKER_FAILED);
    return 0;
}
/* END GENERATED WRAPPERS */

#endif
