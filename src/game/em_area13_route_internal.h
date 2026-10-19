/* Memory and fail-stop plumbing, shared design with em_level14_port_internal.h. */
#ifndef EM_AREA13_ROUTE_INTERNAL_H
#define EM_AREA13_ROUTE_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area13_route.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea13RouteHooks *h;
    EmArea13RouteFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A13R;

static inline int a13r_latch(A13R *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA13_ROUTE_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int a13r_failed(const A13R *o) { return o->fault->code != EM_AREA13_ROUTE_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *a13r_at(A13R *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!a13r_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) a13r_latch(o, address, EM_AREA13_ROUTE_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t a13r_u8(A13R *o, uint32_t a) { return *a13r_at(o, a, 1); }
static inline uint32_t a13r_u16(A13R *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, a13r_at(o, a, 2), 2);
    return v;
}
static inline int32_t a13r_s16(A13R *o, uint32_t a) { return (int16_t)a13r_u16(o, a); }
static inline uint32_t a13r_u32(A13R *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, a13r_at(o, a, 4), 4);
    return v;
}
static inline int32_t a13r_s32(A13R *o, uint32_t a) { return (int32_t)a13r_u32(o, a); }
static inline int32_t a13r_s8(A13R *o, uint32_t a) { return (int8_t)a13r_u8(o, a); }
static inline uint64_t a13r_u64(A13R *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, a13r_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void a13r_q(A13R *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, a13r_at(o, a, 8), 8);
    memcpy(out + 8, a13r_at(o, a + 8, 8), 8);
}
static inline void a13r_wq(A13R *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(a13r_at(o, a, 8), in, 8);
    memcpy(a13r_at(o, a + 8, 8), in + 8, 8);
}

static inline void a13r_w8(A13R *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(a13r_at(o, a, 1), &b, 1);
}
static inline void a13r_w16(A13R *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(a13r_at(o, a, 2), &h, 2);
}
static inline void a13r_w32(A13R *o, uint32_t a, uint32_t v) { memcpy(a13r_at(o, a, 4), &v, 4); }
static inline void a13r_w64(A13R *o, uint32_t a, uint64_t v) { memcpy(a13r_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t a13r_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float a13r_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t a13r_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define A13R_ADD(a, b) em_ee_add_bits((a), (b))
#define A13R_SUB(a, b) em_ee_sub_bits((a), (b))
#define A13R_MUL(a, b) em_ee_mul_bits((a), (b))
#define A13R_LT(a, b) em_ee_c_lt_bits((a), (b))
#define A13R_LE(a, b) em_ee_c_le_bits((a), (b))
#define A13R_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define A13R_DIV(a, b) em_ee_div_bits((a), (b))
#define A13R_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define A13R_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define A13R_MULA(a, b) em_ee_mula_bits((a), (b))
#define A13R_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))

static inline int a13r_begin(A13R *o, const EmArea13RouteHooks *h, EmArea13RouteFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return a13r_failed(o) ? -1 : 0;
}

static inline int a13r_end(const A13R *o) { return a13r_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int a13r_callback(A13R *o, uint32_t fn, uint32_t a0)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_callback) return a13r_latch(o, fn, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? a13r_latch(o, fn, EM_AREA13_ROUTE_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int a13r_method(A13R *o, uint32_t self)
{
    uint32_t fn = a13r_u32(o, self + 0x4C);
    return a13r_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Float arguments and results are bit patterns. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int a13r_c_001AEDB0(A13R *o, int32_t a0)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001AEDB0) return a13r_latch(o, 0x001AEDB0u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001AEDB0(o->h->ctx, a0) < 0) return a13r_latch(o, 0x001AEDB0u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a13r_c_001AFC10(A13R *o, uint32_t a0)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001AFC10) return a13r_latch(o, 0x001AFC10u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001AFC10(o->h->ctx, a0) < 0) return a13r_latch(o, 0x001AFC10u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a13r_c_001B1020(A13R *o, uint32_t a0, int32_t a1, int32_t a2, int32_t a3)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001B1020) return a13r_latch(o, 0x001B1020u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001B1020(o->h->ctx, a0, a1, a2, a3) < 0) return a13r_latch(o, 0x001B1020u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a13r_c_001B1EA0(A13R *o, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t *result)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return a13r_latch(o, 0x001B1EA0u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B1EA0(o->h->ctx, a0, a1, a2, a3, &out) < 0) return a13r_latch(o, 0x001B1EA0u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a13r_c_001B6660(A13R *o, uint32_t a0)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001B6660) return a13r_latch(o, 0x001B6660u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001B6660(o->h->ctx, a0) < 0) return a13r_latch(o, 0x001B6660u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a13r_c_001BA1A0(A13R *o, uint32_t a0, uint32_t a1)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return a13r_latch(o, 0x001BA1A0u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0) return a13r_latch(o, 0x001BA1A0u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a13r_c_001BA1C0(A13R *o, uint32_t a0, int32_t a1, int32_t *result)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return a13r_latch(o, 0x001BA1C0u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BA1C0(o->h->ctx, a0, a1, &out) < 0) return a13r_latch(o, 0x001BA1C0u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a13r_c_001BA1F0(A13R *o, uint32_t a0, int32_t *result)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return a13r_latch(o, 0x001BA1F0u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BA1F0(o->h->ctx, a0, &out) < 0) return a13r_latch(o, 0x001BA1F0u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a13r_c_001BBDA0(A13R *o, uint32_t a0, uint32_t a1)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001BBDA0) return a13r_latch(o, 0x001BBDA0u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001BBDA0(o->h->ctx, a0, a1) < 0) return a13r_latch(o, 0x001BBDA0u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a13r_c_001BBE40(A13R *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001BBE40) return a13r_latch(o, 0x001BBE40u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BBE40(o->h->ctx, a0, a1, a2, &out) < 0) return a13r_latch(o, 0x001BBE40u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a13r_c_001BC0E0(A13R *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001BC0E0) return a13r_latch(o, 0x001BC0E0u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BC0E0(o->h->ctx, a0, a1, &out) < 0) return a13r_latch(o, 0x001BC0E0u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a13r_c_001BC240(A13R *o, uint32_t a0, uint32_t a1)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001BC240) return a13r_latch(o, 0x001BC240u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001BC240(o->h->ctx, a0, a1) < 0) return a13r_latch(o, 0x001BC240u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a13r_c_001BC290(A13R *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001BC290) return a13r_latch(o, 0x001BC290u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BC290(o->h->ctx, a0, a1, &out) < 0) return a13r_latch(o, 0x001BC290u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a13r_c_001BC300(A13R *o, uint32_t a0)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001BC300) return a13r_latch(o, 0x001BC300u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001BC300(o->h->ctx, a0) < 0) return a13r_latch(o, 0x001BC300u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a13r_c_001C4760(A13R *o, int32_t a0, int32_t a1)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001C4760) return a13r_latch(o, 0x001C4760u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001C4760(o->h->ctx, a0, a1) < 0) return a13r_latch(o, 0x001C4760u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a13r_c_001C6380(A13R *o, uint32_t a0)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001C6380) return a13r_latch(o, 0x001C6380u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001C6380(o->h->ctx, a0) < 0) return a13r_latch(o, 0x001C6380u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a13r_c_001EFD20(A13R *o, int32_t a0, uint32_t a1)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001EFD20) return a13r_latch(o, 0x001EFD20u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001EFD20(o->h->ctx, a0, a1) < 0) return a13r_latch(o, 0x001EFD20u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a13r_c_001FAE70(A13R *o, int32_t a0)
{
    if (a13r_failed(o)) return -1;
    if (!o->h->w_001FAE70) return a13r_latch(o, 0x001FAE70u, EM_AREA13_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001FAE70(o->h->ctx, a0) < 0) return a13r_latch(o, 0x001FAE70u, EM_AREA13_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

/* END GENERATED WRAPPERS */
#endif
