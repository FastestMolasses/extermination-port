/* Memory and fail-stop plumbing, shared design with em_level14_port_internal.h. */
#ifndef EM_AREA07_PROBE_INTERNAL_H
#define EM_AREA07_PROBE_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area07_probe.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea07ProbeHooks *h;
    EmArea07ProbeFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A07A;

static inline int a07a_latch(A07A *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA07_PROBE_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int a07a_failed(const A07A *o) { return o->fault->code != EM_AREA07_PROBE_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *a07a_at(A07A *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!a07a_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) a07a_latch(o, address, EM_AREA07_PROBE_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t a07a_u8(A07A *o, uint32_t a) { return *a07a_at(o, a, 1); }
static inline uint32_t a07a_u16(A07A *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, a07a_at(o, a, 2), 2);
    return v;
}
static inline int32_t a07a_s16(A07A *o, uint32_t a) { return (int16_t)a07a_u16(o, a); }
static inline uint32_t a07a_u32(A07A *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, a07a_at(o, a, 4), 4);
    return v;
}
static inline int32_t a07a_s32(A07A *o, uint32_t a) { return (int32_t)a07a_u32(o, a); }
static inline int32_t a07a_s8(A07A *o, uint32_t a) { return (int8_t)a07a_u8(o, a); }
static inline uint64_t a07a_u64(A07A *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, a07a_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void a07a_q(A07A *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, a07a_at(o, a, 8), 8);
    memcpy(out + 8, a07a_at(o, a + 8, 8), 8);
}
static inline void a07a_wq(A07A *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(a07a_at(o, a, 8), in, 8);
    memcpy(a07a_at(o, a + 8, 8), in + 8, 8);
}

static inline void a07a_w8(A07A *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(a07a_at(o, a, 1), &b, 1);
}
static inline void a07a_w16(A07A *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(a07a_at(o, a, 2), &h, 2);
}
static inline void a07a_w32(A07A *o, uint32_t a, uint32_t v) { memcpy(a07a_at(o, a, 4), &v, 4); }
static inline void a07a_w64(A07A *o, uint32_t a, uint64_t v) { memcpy(a07a_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t a07a_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float a07a_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t a07a_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define A07A_ADD(a, b) em_ee_add_bits((a), (b))
#define A07A_SUB(a, b) em_ee_sub_bits((a), (b))
#define A07A_MUL(a, b) em_ee_mul_bits((a), (b))
#define A07A_LT(a, b) em_ee_c_lt_bits((a), (b))
#define A07A_LE(a, b) em_ee_c_le_bits((a), (b))
#define A07A_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define A07A_DIV(a, b) em_ee_div_bits((a), (b))
#define A07A_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define A07A_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define A07A_MULA(a, b) em_ee_mula_bits((a), (b))
#define A07A_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))

static inline int a07a_begin(A07A *o, const EmArea07ProbeHooks *h, EmArea07ProbeFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return a07a_failed(o) ? -1 : 0;
}

static inline int a07a_end(const A07A *o) { return a07a_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int a07a_callback(A07A *o, uint32_t fn, uint32_t a0)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_callback) return a07a_latch(o, fn, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? a07a_latch(o, fn, EM_AREA07_PROBE_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int a07a_method(A07A *o, uint32_t self)
{
    uint32_t fn = a07a_u32(o, self + 0x4C);
    return a07a_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int a07a_c_001026A0(A07A *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001026A0) return a07a_latch(o, 0x001026A0u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    if (o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0) return a07a_latch(o, 0x001026A0u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07a_c_00102738(A07A *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_00102738) return a07a_latch(o, 0x00102738u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_00102738(o->h->ctx, a0, a1, &out) < 0) return a07a_latch(o, 0x00102738u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    *result = a07a_bits(out);
    return 0;
}

static inline int a07a_c_001028B8(A07A *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001028B8) return a07a_latch(o, 0x001028B8u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    if (o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0) return a07a_latch(o, 0x001028B8u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07a_c_001028D0(A07A *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001028D0) return a07a_latch(o, 0x001028D0u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    if (o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0) return a07a_latch(o, 0x001028D0u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07a_c_00102948(A07A *o, uint32_t a0, uint32_t a1)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_00102948) return a07a_latch(o, 0x00102948u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    if (o->h->w_00102948(o->h->ctx, a0, a1) < 0) return a07a_latch(o, 0x00102948u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07a_c_001031E0(A07A *o, uint32_t a0, uint32_t a1)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001031E0) return a07a_latch(o, 0x001031E0u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    if (o->h->w_001031E0(o->h->ctx, a0, a1) < 0) return a07a_latch(o, 0x001031E0u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07a_c_00122BB8(A07A *o, int32_t *result)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_00122BB8) return a07a_latch(o, 0x00122BB8u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_00122BB8(o->h->ctx, &out) < 0) return a07a_latch(o, 0x00122BB8u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07a_c_0019A570(A07A *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t *result)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_0019A570) return a07a_latch(o, 0x0019A570u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_0019A570(o->h->ctx, a0, a1, a2, a3, &out) < 0) return a07a_latch(o, 0x0019A570u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07a_c_0019AA80(A07A *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_0019AA80) return a07a_latch(o, 0x0019AA80u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_0019AA80(o->h->ctx, a0, a1, a2, &out) < 0) return a07a_latch(o, 0x0019AA80u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07a_c_001AFC10(A07A *o, uint32_t a0)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001AFC10) return a07a_latch(o, 0x001AFC10u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    if (o->h->w_001AFC10(o->h->ctx, a0) < 0) return a07a_latch(o, 0x001AFC10u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07a_c_001B0FD0(A07A *o, uint32_t a0, int32_t *result)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return a07a_latch(o, 0x001B0FD0u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B0FD0(o->h->ctx, a0, &out) < 0) return a07a_latch(o, 0x001B0FD0u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07a_c_001B17A0(A07A *o, uint32_t a0, int32_t *result)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001B17A0) return a07a_latch(o, 0x001B17A0u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B17A0(o->h->ctx, a0, &out) < 0) return a07a_latch(o, 0x001B17A0u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07a_c_001B6F00(A07A *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001B6F00) return a07a_latch(o, 0x001B6F00u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    if (o->h->w_001B6F00(o->h->ctx, a0, a1, a07a_float(a2)) < 0) return a07a_latch(o, 0x001B6F00u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07a_c_001BA1A0(A07A *o, uint32_t a0, uint32_t a1)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return a07a_latch(o, 0x001BA1A0u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    if (o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0) return a07a_latch(o, 0x001BA1A0u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07a_c_001BA1F0(A07A *o, uint32_t a0, int32_t *result)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return a07a_latch(o, 0x001BA1F0u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BA1F0(o->h->ctx, a0, &out) < 0) return a07a_latch(o, 0x001BA1F0u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07a_c_001C6380(A07A *o, uint32_t a0)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001C6380) return a07a_latch(o, 0x001C6380u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    if (o->h->w_001C6380(o->h->ctx, a0) < 0) return a07a_latch(o, 0x001C6380u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07a_c_001CD520(A07A *o, int32_t a0, int32_t a1, uint32_t a2, uint64_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, int32_t *result)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001CD520) return a07a_latch(o, 0x001CD520u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001CD520(o->h->ctx, a0, a1, a2, a3, a4, a07a_float(a5), a07a_float(a6), a07a_float(a7), &out) < 0) return a07a_latch(o, 0x001CD520u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07a_c_001E2BA0(A07A *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001E2BA0) return a07a_latch(o, 0x001E2BA0u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    if (o->h->w_001E2BA0(o->h->ctx, a0, a1, a2, a07a_float(a3)) < 0) return a07a_latch(o, 0x001E2BA0u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07a_c_001F4BF0(A07A *o, uint32_t a0, uint32_t a1)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001F4BF0) return a07a_latch(o, 0x001F4BF0u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    if (o->h->w_001F4BF0(o->h->ctx, a0, a1) < 0) return a07a_latch(o, 0x001F4BF0u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07a_c_001F4E20(A07A *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001F4E20) return a07a_latch(o, 0x001F4E20u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    if (o->h->w_001F4E20(o->h->ctx, a0, a1, a07a_float(a2)) < 0) return a07a_latch(o, 0x001F4E20u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07a_c_001FBD50(A07A *o, uint32_t a0, int32_t a1, int32_t a2, uint32_t a3, int32_t *result)
{
    if (a07a_failed(o)) return -1;
    if (!o->h->w_001FBD50) return a07a_latch(o, 0x001FBD50u, EM_AREA07_PROBE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001FBD50(o->h->ctx, a0, a1, a2, a07a_float(a3), &out) < 0) return a07a_latch(o, 0x001FBD50u, EM_AREA07_PROBE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

/* END GENERATED WRAPPERS */
#endif
