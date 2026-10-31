/* Memory and fail-stop plumbing, shared design with em_level14_port_internal.h. */
#ifndef EM_AREA07_ROUTE_INTERNAL_H
#define EM_AREA07_ROUTE_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area07_route.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea07RouteHooks *h;
    EmArea07RouteFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A07R;

static inline int a07r_latch(A07R *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA07_ROUTE_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int a07r_failed(const A07R *o) { return o->fault->code != EM_AREA07_ROUTE_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *a07r_at(A07R *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!a07r_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) a07r_latch(o, address, EM_AREA07_ROUTE_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t a07r_u8(A07R *o, uint32_t a) { return *a07r_at(o, a, 1); }
static inline uint32_t a07r_u16(A07R *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, a07r_at(o, a, 2), 2);
    return v;
}
static inline int32_t a07r_s16(A07R *o, uint32_t a) { return (int16_t)a07r_u16(o, a); }
static inline uint32_t a07r_u32(A07R *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, a07r_at(o, a, 4), 4);
    return v;
}
static inline int32_t a07r_s32(A07R *o, uint32_t a) { return (int32_t)a07r_u32(o, a); }
static inline int32_t a07r_s8(A07R *o, uint32_t a) { return (int8_t)a07r_u8(o, a); }
static inline uint64_t a07r_u64(A07R *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, a07r_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void a07r_q(A07R *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, a07r_at(o, a, 8), 8);
    memcpy(out + 8, a07r_at(o, a + 8, 8), 8);
}
static inline void a07r_wq(A07R *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(a07r_at(o, a, 8), in, 8);
    memcpy(a07r_at(o, a + 8, 8), in + 8, 8);
}

static inline void a07r_w8(A07R *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(a07r_at(o, a, 1), &b, 1);
}
static inline void a07r_w16(A07R *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(a07r_at(o, a, 2), &h, 2);
}
static inline void a07r_w32(A07R *o, uint32_t a, uint32_t v) { memcpy(a07r_at(o, a, 4), &v, 4); }
static inline void a07r_w64(A07R *o, uint32_t a, uint64_t v) { memcpy(a07r_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t a07r_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float a07r_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t a07r_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define A07R_ADD(a, b) em_ee_add_bits((a), (b))
#define A07R_SUB(a, b) em_ee_sub_bits((a), (b))
#define A07R_MUL(a, b) em_ee_mul_bits((a), (b))
#define A07R_LT(a, b) em_ee_c_lt_bits((a), (b))
#define A07R_LE(a, b) em_ee_c_le_bits((a), (b))
#define A07R_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define A07R_DIV(a, b) em_ee_div_bits((a), (b))
#define A07R_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define A07R_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define A07R_MULA(a, b) em_ee_mula_bits((a), (b))
#define A07R_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))

static inline int a07r_begin(A07R *o, const EmArea07RouteHooks *h, EmArea07RouteFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return a07r_failed(o) ? -1 : 0;
}

static inline int a07r_end(const A07R *o) { return a07r_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int a07r_callback(A07R *o, uint32_t fn, uint32_t a0)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_callback) return a07r_latch(o, fn, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? a07r_latch(o, fn, EM_AREA07_ROUTE_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int a07r_method(A07R *o, uint32_t self)
{
    uint32_t fn = a07r_u32(o, self + 0x4C);
    return a07r_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int a07r_c_001026A0(A07R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001026A0) return a07r_latch(o, 0x001026A0u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0) return a07r_latch(o, 0x001026A0u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07r_c_00102760(A07R *o, uint32_t a0, uint32_t a1)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_00102760) return a07r_latch(o, 0x00102760u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00102760(o->h->ctx, a0, a1) < 0) return a07r_latch(o, 0x00102760u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07r_c_001028D0(A07R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001028D0) return a07r_latch(o, 0x001028D0u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0) return a07r_latch(o, 0x001028D0u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07r_c_00102948(A07R *o, uint32_t a0, uint32_t a1)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_00102948) return a07r_latch(o, 0x00102948u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00102948(o->h->ctx, a0, a1) < 0) return a07r_latch(o, 0x00102948u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07r_c_00102958(A07R *o, uint32_t a0, uint32_t a1)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_00102958) return a07r_latch(o, 0x00102958u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00102958(o->h->ctx, a0, a1) < 0) return a07r_latch(o, 0x00102958u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07r_c_0011DBB8(A07R *o, float a0, float *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_0011DBB8) return a07r_latch(o, 0x0011DBB8u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011DBB8(o->h->ctx, a0, &out) < 0) return a07r_latch(o, 0x0011DBB8u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_0011DF78(A07R *o, float a0, float *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_0011DF78) return a07r_latch(o, 0x0011DF78u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011DF78(o->h->ctx, a0, &out) < 0) return a07r_latch(o, 0x0011DF78u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_0011E2A8(A07R *o, float a0, float *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return a07r_latch(o, 0x0011E2A8u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E2A8(o->h->ctx, a0, &out) < 0) return a07r_latch(o, 0x0011E2A8u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_0011E520(A07R *o, float a0, float *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_0011E520) return a07r_latch(o, 0x0011E520u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E520(o->h->ctx, a0, &out) < 0) return a07r_latch(o, 0x0011E520u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_0011E748(A07R *o, float a0, float *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_0011E748) return a07r_latch(o, 0x0011E748u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E748(o->h->ctx, a0, &out) < 0) return a07r_latch(o, 0x0011E748u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_00122BB8(A07R *o, int32_t *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_00122BB8) return a07r_latch(o, 0x00122BB8u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_00122BB8(o->h->ctx, &out) < 0) return a07r_latch(o, 0x00122BB8u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_001A2370(A07R *o, uint32_t a0, uint32_t a1)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001A2370) return a07r_latch(o, 0x001A2370u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001A2370(o->h->ctx, a0, a1) < 0) return a07r_latch(o, 0x001A2370u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07r_c_001AFA90(A07R *o, int32_t a0, uint32_t *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001AFA90) return a07r_latch(o, 0x001AFA90u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    uint32_t out = 0;
    if (o->h->w_001AFA90(o->h->ctx, a0, &out) < 0) return a07r_latch(o, 0x001AFA90u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_001AFC10(A07R *o, uint32_t a0)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001AFC10) return a07r_latch(o, 0x001AFC10u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001AFC10(o->h->ctx, a0) < 0) return a07r_latch(o, 0x001AFC10u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07r_c_001B0FD0(A07R *o, uint32_t a0, int32_t *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return a07r_latch(o, 0x001B0FD0u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B0FD0(o->h->ctx, a0, &out) < 0) return a07r_latch(o, 0x001B0FD0u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_001B1190(A07R *o, int32_t a0)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001B1190) return a07r_latch(o, 0x001B1190u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001B1190(o->h->ctx, a0) < 0) return a07r_latch(o, 0x001B1190u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07r_c_001B11E0(A07R *o, int32_t a0, int32_t *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001B11E0) return a07r_latch(o, 0x001B11E0u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B11E0(o->h->ctx, a0, &out) < 0) return a07r_latch(o, 0x001B11E0u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_001B1470(A07R *o, float a0, float *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001B1470) return a07r_latch(o, 0x001B1470u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B1470(o->h->ctx, a0, &out) < 0) return a07r_latch(o, 0x001B1470u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_001B17A0(A07R *o, uint32_t a0, int32_t *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001B17A0) return a07r_latch(o, 0x001B17A0u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B17A0(o->h->ctx, a0, &out) < 0) return a07r_latch(o, 0x001B17A0u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_001BA1C0(A07R *o, uint32_t a0, int32_t a1, int32_t *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return a07r_latch(o, 0x001BA1C0u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BA1C0(o->h->ctx, a0, a1, &out) < 0) return a07r_latch(o, 0x001BA1C0u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_001C6380(A07R *o, uint32_t a0)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001C6380) return a07r_latch(o, 0x001C6380u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001C6380(o->h->ctx, a0) < 0) return a07r_latch(o, 0x001C6380u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a07r_c_001EFD90(A07R *o, int32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001EFD90) return a07r_latch(o, 0x001EFD90u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001EFD90(o->h->ctx, a0, a1, a2, &out) < 0) return a07r_latch(o, 0x001EFD90u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_001EFE00(A07R *o, int32_t a0, uint32_t a1, int32_t *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001EFE00) return a07r_latch(o, 0x001EFE00u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001EFE00(o->h->ctx, a0, a1, &out) < 0) return a07r_latch(o, 0x001EFE00u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_001FBD50(A07R *o, uint32_t a0, int32_t a1, int32_t a2, float a3, int32_t *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_001FBD50) return a07r_latch(o, 0x001FBD50u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001FBD50(o->h->ctx, a0, a1, a2, a3, &out) < 0) return a07r_latch(o, 0x001FBD50u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_00825470(A07R *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_00825470) return a07r_latch(o, 0x00825470u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_00825470(o->h->ctx, a0, a1, &out) < 0) return a07r_latch(o, 0x00825470u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a07r_c_00825940(A07R *o, uint32_t a0)
{
    if (a07r_failed(o)) return -1;
    if (!o->h->w_00825940) return a07r_latch(o, 0x00825940u, EM_AREA07_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00825940(o->h->ctx, a0) < 0) return a07r_latch(o, 0x00825940u, EM_AREA07_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

/* END GENERATED WRAPPERS */
#endif
