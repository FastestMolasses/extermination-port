/* Memory and fail-stop plumbing, shared design with em_area15_route_internal.h. */
#ifndef EM_AREA08_ROUTE_INTERNAL_H
#define EM_AREA08_ROUTE_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area08_route.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea08RouteHooks *h;
    EmArea08RouteFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A08R;

static inline int a08r_latch(A08R *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA08_ROUTE_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int a08r_failed(const A08R *o) { return o->fault->code != EM_AREA08_ROUTE_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *a08r_at(A08R *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!a08r_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) a08r_latch(o, address, EM_AREA08_ROUTE_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t a08r_u8(A08R *o, uint32_t a) { return *a08r_at(o, a, 1); }
static inline uint32_t a08r_u16(A08R *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, a08r_at(o, a, 2), 2);
    return v;
}
static inline int32_t a08r_s16(A08R *o, uint32_t a) { return (int16_t)a08r_u16(o, a); }
static inline uint32_t a08r_u32(A08R *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, a08r_at(o, a, 4), 4);
    return v;
}
static inline int32_t a08r_s32(A08R *o, uint32_t a) { return (int32_t)a08r_u32(o, a); }
static inline int32_t a08r_s8(A08R *o, uint32_t a) { return (int8_t)a08r_u8(o, a); }
static inline uint64_t a08r_u64(A08R *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, a08r_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void a08r_q(A08R *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, a08r_at(o, a, 8), 8);
    memcpy(out + 8, a08r_at(o, a + 8, 8), 8);
}
static inline void a08r_wq(A08R *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(a08r_at(o, a, 8), in, 8);
    memcpy(a08r_at(o, a + 8, 8), in + 8, 8);
}

static inline void a08r_w8(A08R *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(a08r_at(o, a, 1), &b, 1);
}
static inline void a08r_w16(A08R *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(a08r_at(o, a, 2), &h, 2);
}
static inline void a08r_w32(A08R *o, uint32_t a, uint32_t v) { memcpy(a08r_at(o, a, 4), &v, 4); }
static inline void a08r_w64(A08R *o, uint32_t a, uint64_t v) { memcpy(a08r_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t a08r_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float a08r_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t a08r_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define A08R_ADD(a, b) em_ee_add_bits((a), (b))
#define A08R_SUB(a, b) em_ee_sub_bits((a), (b))
#define A08R_MUL(a, b) em_ee_mul_bits((a), (b))
#define A08R_LT(a, b) em_ee_c_lt_bits((a), (b))
#define A08R_LE(a, b) em_ee_c_le_bits((a), (b))
#define A08R_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define A08R_DIV(a, b) em_ee_div_bits((a), (b))
#define A08R_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define A08R_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define A08R_MULA(a, b) em_ee_mula_bits((a), (b))
#define A08R_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))
#define A08R_ADDA(a, b) em_ee_adda_bits((a), (b))

static inline int a08r_begin(A08R *o, const EmArea08RouteHooks *h, EmArea08RouteFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return a08r_failed(o) ? -1 : 0;
}

static inline int a08r_end(const A08R *o) { return a08r_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int a08r_callback(A08R *o, uint32_t fn, uint32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_callback) return a08r_latch(o, fn, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? a08r_latch(o, fn, EM_AREA08_ROUTE_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int a08r_method(A08R *o, uint32_t self)
{
    uint32_t fn = a08r_u32(o, self + 0x4C);
    return a08r_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Float arguments and results are bit patterns. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int a08r_c_00102760(A08R *o, uint32_t a0, uint32_t a1)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_00102760) return a08r_latch(o, 0x00102760u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00102760(o->h->ctx, a0, a1) < 0) return a08r_latch(o, 0x00102760u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001028B8(A08R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001028B8) return a08r_latch(o, 0x001028B8u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0) return a08r_latch(o, 0x001028B8u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001028D0(A08R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001028D0) return a08r_latch(o, 0x001028D0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0) return a08r_latch(o, 0x001028D0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001028E8(A08R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001028E8) return a08r_latch(o, 0x001028E8u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001028E8(o->h->ctx, a0, a1, a2) < 0) return a08r_latch(o, 0x001028E8u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_00102918(A08R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_00102918) return a08r_latch(o, 0x00102918u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00102918(o->h->ctx, a0, a1, a2) < 0) return a08r_latch(o, 0x00102918u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001029C0(A08R *o, uint32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001029C0) return a08r_latch(o, 0x001029C0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001029C0(o->h->ctx, a0) < 0) return a08r_latch(o, 0x001029C0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_00102B08(A08R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_00102B08) return a08r_latch(o, 0x00102B08u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00102B08(o->h->ctx, a0, a1, a08r_float(a2)) < 0) return a08r_latch(o, 0x00102B08u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_00102BB0(A08R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_00102BB0) return a08r_latch(o, 0x00102BB0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00102BB0(o->h->ctx, a0, a1, a08r_float(a2)) < 0) return a08r_latch(o, 0x00102BB0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_0011A070(A08R *o, int32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_0011A070) return a08r_latch(o, 0x0011A070u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_0011A070(o->h->ctx, a0) < 0) return a08r_latch(o, 0x0011A070u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_0011DE90(A08R *o, uint32_t a0, uint32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_0011DE90) return a08r_latch(o, 0x0011DE90u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011DE90(o->h->ctx, a08r_float(a0), &out) < 0) return a08r_latch(o, 0x0011DE90u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = a08r_bits(out);
    return 0;
}

static inline int a08r_c_0011DF78(A08R *o, uint32_t a0, uint32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_0011DF78) return a08r_latch(o, 0x0011DF78u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011DF78(o->h->ctx, a08r_float(a0), &out) < 0) return a08r_latch(o, 0x0011DF78u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = a08r_bits(out);
    return 0;
}

static inline int a08r_c_0011E2A8(A08R *o, uint32_t a0, uint32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return a08r_latch(o, 0x0011E2A8u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E2A8(o->h->ctx, a08r_float(a0), &out) < 0) return a08r_latch(o, 0x0011E2A8u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = a08r_bits(out);
    return 0;
}

static inline int a08r_c_0011E748(A08R *o, uint32_t a0, uint32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_0011E748) return a08r_latch(o, 0x0011E748u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E748(o->h->ctx, a08r_float(a0), &out) < 0) return a08r_latch(o, 0x0011E748u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = a08r_bits(out);
    return 0;
}

static inline int a08r_c_00122BB8(A08R *o, int32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_00122BB8) return a08r_latch(o, 0x00122BB8u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_00122BB8(o->h->ctx, &out) < 0) return a08r_latch(o, 0x00122BB8u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_00182F90(A08R *o, uint32_t a0, uint32_t a1)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_00182F90) return a08r_latch(o, 0x00182F90u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00182F90(o->h->ctx, a0, a1) < 0) return a08r_latch(o, 0x00182F90u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001AFC10(A08R *o, uint32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001AFC10) return a08r_latch(o, 0x001AFC10u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001AFC10(o->h->ctx, a0) < 0) return a08r_latch(o, 0x001AFC10u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001B0FD0(A08R *o, uint32_t a0, int32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return a08r_latch(o, 0x001B0FD0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B0FD0(o->h->ctx, a0, &out) < 0) return a08r_latch(o, 0x001B0FD0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_001B10B0(A08R *o, uint32_t a0, int32_t a1, int32_t a2)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001B10B0) return a08r_latch(o, 0x001B10B0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001B10B0(o->h->ctx, a0, a1, a2) < 0) return a08r_latch(o, 0x001B10B0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001B1240(A08R *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001B1240) return a08r_latch(o, 0x001B1240u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B1240(o->h->ctx, a0, a08r_float(a1), a08r_float(a2), &out) < 0) return a08r_latch(o, 0x001B1240u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = a08r_bits(out);
    return 0;
}

static inline int a08r_c_001B1470(A08R *o, uint32_t a0, uint32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001B1470) return a08r_latch(o, 0x001B1470u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B1470(o->h->ctx, a08r_float(a0), &out) < 0) return a08r_latch(o, 0x001B1470u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = a08r_bits(out);
    return 0;
}

static inline int a08r_c_001B17A0(A08R *o, uint32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001B17A0) return a08r_latch(o, 0x001B17A0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001B17A0(o->h->ctx, a0) < 0) return a08r_latch(o, 0x001B17A0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001B1B70(A08R *o, uint32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001B1B70) return a08r_latch(o, 0x001B1B70u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001B1B70(o->h->ctx, a0) < 0) return a08r_latch(o, 0x001B1B70u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001B1DE0(A08R *o, uint32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001B1DE0) return a08r_latch(o, 0x001B1DE0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001B1DE0(o->h->ctx, a0) < 0) return a08r_latch(o, 0x001B1DE0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001B2140(A08R *o, uint32_t a0, int32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001B2140) return a08r_latch(o, 0x001B2140u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B2140(o->h->ctx, a0, &out) < 0) return a08r_latch(o, 0x001B2140u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_001B6660(A08R *o, uint32_t a0, uint32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001B6660) return a08r_latch(o, 0x001B6660u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    uint32_t out = 0;
    if (o->h->w_001B6660(o->h->ctx, a0, &out) < 0) return a08r_latch(o, 0x001B6660u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_001B6F00(A08R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001B6F00) return a08r_latch(o, 0x001B6F00u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001B6F00(o->h->ctx, a0, a1, a08r_float(a2)) < 0) return a08r_latch(o, 0x001B6F00u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001BA1A0(A08R *o, uint32_t a0, uint32_t a1)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return a08r_latch(o, 0x001BA1A0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0) return a08r_latch(o, 0x001BA1A0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001BA1C0(A08R *o, uint32_t a0, int32_t a1, int32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return a08r_latch(o, 0x001BA1C0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BA1C0(o->h->ctx, a0, a1, &out) < 0) return a08r_latch(o, 0x001BA1C0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_001BA1F0(A08R *o, uint32_t a0, int32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return a08r_latch(o, 0x001BA1F0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BA1F0(o->h->ctx, a0, &out) < 0) return a08r_latch(o, 0x001BA1F0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_001BA540(A08R *o, uint32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001BA540) return a08r_latch(o, 0x001BA540u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001BA540(o->h->ctx, a0) < 0) return a08r_latch(o, 0x001BA540u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001BA580(A08R *o, uint32_t a0, int32_t a1)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001BA580) return a08r_latch(o, 0x001BA580u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001BA580(o->h->ctx, a0, a1) < 0) return a08r_latch(o, 0x001BA580u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001BA8E0(A08R *o, uint32_t a0, int32_t a1)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001BA8E0) return a08r_latch(o, 0x001BA8E0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001BA8E0(o->h->ctx, a0, a1) < 0) return a08r_latch(o, 0x001BA8E0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001BB520(A08R *o, uint32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001BB520) return a08r_latch(o, 0x001BB520u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001BB520(o->h->ctx, a0) < 0) return a08r_latch(o, 0x001BB520u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001BB560(A08R *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001BB560) return a08r_latch(o, 0x001BB560u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BB560(o->h->ctx, a0, a1, a2, &out) < 0) return a08r_latch(o, 0x001BB560u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_001BB7C0(A08R *o, uint32_t a0, int32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001BB7C0) return a08r_latch(o, 0x001BB7C0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BB7C0(o->h->ctx, a0, &out) < 0) return a08r_latch(o, 0x001BB7C0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_001BB7F0(A08R *o, uint32_t a0, int32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001BB7F0) return a08r_latch(o, 0x001BB7F0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BB7F0(o->h->ctx, a0, &out) < 0) return a08r_latch(o, 0x001BB7F0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_001BC150(A08R *o, uint32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001BC150) return a08r_latch(o, 0x001BC150u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001BC150(o->h->ctx, a0) < 0) return a08r_latch(o, 0x001BC150u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001C4760(A08R *o, int32_t a0, int32_t a1)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001C4760) return a08r_latch(o, 0x001C4760u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001C4760(o->h->ctx, a0, a1) < 0) return a08r_latch(o, 0x001C4760u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001C6380(A08R *o, uint32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001C6380) return a08r_latch(o, 0x001C6380u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001C6380(o->h->ctx, a0) < 0) return a08r_latch(o, 0x001C6380u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001C63E0(A08R *o, uint32_t a0, int32_t a1)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001C63E0) return a08r_latch(o, 0x001C63E0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001C63E0(o->h->ctx, a0, a1) < 0) return a08r_latch(o, 0x001C63E0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001C64F0(A08R *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001C64F0) return a08r_latch(o, 0x001C64F0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001C64F0(o->h->ctx, a0, a08r_float(a1), &out) < 0) return a08r_latch(o, 0x001C64F0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_001C67E0(A08R *o, uint32_t a0, int32_t a1, uint32_t a2, uint32_t a3)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001C67E0) return a08r_latch(o, 0x001C67E0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001C67E0(o->h->ctx, a0, a1, a08r_float(a2), a08r_float(a3)) < 0) return a08r_latch(o, 0x001C67E0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001C68C0(A08R *o, uint32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001C68C0) return a08r_latch(o, 0x001C68C0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001C68C0(o->h->ctx, a0) < 0) return a08r_latch(o, 0x001C68C0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001CA6F0(A08R *o, uint32_t a0, int32_t a1)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001CA6F0) return a08r_latch(o, 0x001CA6F0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001CA6F0(o->h->ctx, a0, a1) < 0) return a08r_latch(o, 0x001CA6F0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001CCF70(A08R *o, uint32_t a0, int32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001CCF70) return a08r_latch(o, 0x001CCF70u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001CCF70(o->h->ctx, a0, &out) < 0) return a08r_latch(o, 0x001CCF70u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_001CD390(A08R *o, uint32_t a0, uint32_t a1)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001CD390) return a08r_latch(o, 0x001CD390u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001CD390(o->h->ctx, a0, a1) < 0) return a08r_latch(o, 0x001CD390u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001CFAE0(A08R *o, uint32_t a0, int32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001CFAE0) return a08r_latch(o, 0x001CFAE0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001CFAE0(o->h->ctx, a0, a1, a2, a08r_float(a3), a08r_float(a4), a08r_float(a5), a08r_float(a6)) < 0) return a08r_latch(o, 0x001CFAE0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001CFBE0(A08R *o, int32_t a0, int32_t a1, uint32_t a2, uint32_t a3, int32_t a4)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001CFBE0) return a08r_latch(o, 0x001CFBE0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001CFBE0(o->h->ctx, a0, a1, a2, a3, a4) < 0) return a08r_latch(o, 0x001CFBE0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001EFEB0(A08R *o, int32_t a0, uint32_t a1, uint32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001EFEB0) return a08r_latch(o, 0x001EFEB0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    uint32_t out = 0;
    if (o->h->w_001EFEB0(o->h->ctx, a0, a1, &out) < 0) return a08r_latch(o, 0x001EFEB0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_001F4BF0(A08R *o, uint32_t a0, uint32_t a1)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001F4BF0) return a08r_latch(o, 0x001F4BF0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001F4BF0(o->h->ctx, a0, a1) < 0) return a08r_latch(o, 0x001F4BF0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001FABB0(A08R *o)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001FABB0) return a08r_latch(o, 0x001FABB0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001FABB0(o->h->ctx) < 0) return a08r_latch(o, 0x001FABB0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001FA790(A08R *o, int32_t a0, int32_t a1)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001FA790) return a08r_latch(o, 0x001FA790u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001FA790(o->h->ctx, a0, a1) < 0) return a08r_latch(o, 0x001FA790u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001FAE70(A08R *o, int32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001FAE70) return a08r_latch(o, 0x001FAE70u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001FAE70(o->h->ctx, a0) < 0) return a08r_latch(o, 0x001FAE70u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001FB0B0(A08R *o, int32_t a0)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001FB0B0) return a08r_latch(o, 0x001FB0B0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001FB0B0(o->h->ctx, a0) < 0) return a08r_latch(o, 0x001FB0B0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001FB9F0(A08R *o, int32_t a0, int32_t a1, int32_t a2, int32_t a3)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001FB9F0) return a08r_latch(o, 0x001FB9F0u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001FB9F0(o->h->ctx, a0, a1, a2, a3) < 0) return a08r_latch(o, 0x001FB9F0u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a08r_c_001FBD50(A08R *o, uint32_t a0, int32_t a1, int32_t a2, uint32_t a3, int32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001FBD50) return a08r_latch(o, 0x001FBD50u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001FBD50(o->h->ctx, a0, a1, a2, a08r_float(a3), &out) < 0) return a08r_latch(o, 0x001FBD50u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a08r_c_001FBE80(A08R *o, uint32_t a0, int32_t a1, uint32_t a2, uint32_t a3, int32_t *result)
{
    if (a08r_failed(o)) return -1;
    if (!o->h->w_001FBE80) return a08r_latch(o, 0x001FBE80u, EM_AREA08_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001FBE80(o->h->ctx, a0, a1, a08r_float(a2), a08r_float(a3), &out) < 0) return a08r_latch(o, 0x001FBE80u, EM_AREA08_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}
/* END GENERATED WRAPPERS */
#endif
