/* Memory and fail-stop plumbing, shared design with em_level14_port_internal.h. */
#ifndef EM_AREA19_ROUTE_INTERNAL_H
#define EM_AREA19_ROUTE_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area19_route.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea19RouteHooks *h;
    EmArea19RouteFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A19R;

static inline int a19r_latch(A19R *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA19_ROUTE_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int a19r_failed(const A19R *o) { return o->fault->code != EM_AREA19_ROUTE_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *a19r_at(A19R *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!a19r_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) a19r_latch(o, address, EM_AREA19_ROUTE_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t a19r_u8(A19R *o, uint32_t a) { return *a19r_at(o, a, 1); }
static inline uint32_t a19r_u16(A19R *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, a19r_at(o, a, 2), 2);
    return v;
}
static inline int32_t a19r_s16(A19R *o, uint32_t a) { return (int16_t)a19r_u16(o, a); }
static inline uint32_t a19r_u32(A19R *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, a19r_at(o, a, 4), 4);
    return v;
}
static inline int32_t a19r_s32(A19R *o, uint32_t a) { return (int32_t)a19r_u32(o, a); }
static inline int32_t a19r_s8(A19R *o, uint32_t a) { return (int8_t)a19r_u8(o, a); }
static inline uint64_t a19r_u64(A19R *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, a19r_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void a19r_q(A19R *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, a19r_at(o, a, 8), 8);
    memcpy(out + 8, a19r_at(o, a + 8, 8), 8);
}
static inline void a19r_wq(A19R *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(a19r_at(o, a, 8), in, 8);
    memcpy(a19r_at(o, a + 8, 8), in + 8, 8);
}

static inline void a19r_w8(A19R *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(a19r_at(o, a, 1), &b, 1);
}
static inline void a19r_w16(A19R *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(a19r_at(o, a, 2), &h, 2);
}
static inline void a19r_w32(A19R *o, uint32_t a, uint32_t v) { memcpy(a19r_at(o, a, 4), &v, 4); }
static inline void a19r_w64(A19R *o, uint32_t a, uint64_t v) { memcpy(a19r_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t a19r_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float a19r_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t a19r_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define A19R_ADD(a, b) em_ee_add_bits((a), (b))
#define A19R_SUB(a, b) em_ee_sub_bits((a), (b))
#define A19R_MUL(a, b) em_ee_mul_bits((a), (b))
#define A19R_LT(a, b) em_ee_c_lt_bits((a), (b))
#define A19R_LE(a, b) em_ee_c_le_bits((a), (b))
#define A19R_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define A19R_DIV(a, b) em_ee_div_bits((a), (b))
#define A19R_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define A19R_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define A19R_MULA(a, b) em_ee_mula_bits((a), (b))
#define A19R_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))

static inline int a19r_begin(A19R *o, const EmArea19RouteHooks *h, EmArea19RouteFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return a19r_failed(o) ? -1 : 0;
}

static inline int a19r_end(const A19R *o) { return a19r_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int a19r_callback(A19R *o, uint32_t fn, uint32_t a0)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_callback) return a19r_latch(o, fn, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? a19r_latch(o, fn, EM_AREA19_ROUTE_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int a19r_method(A19R *o, uint32_t self)
{
    uint32_t fn = a19r_u32(o, self + 0x4C);
    return a19r_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int a19r_c_001026A0(A19R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001026A0) return a19r_latch(o, 0x001026A0u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0) return a19r_latch(o, 0x001026A0u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_00102760(A19R *o, uint32_t a0, uint32_t a1)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_00102760) return a19r_latch(o, 0x00102760u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00102760(o->h->ctx, a0, a1) < 0) return a19r_latch(o, 0x00102760u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_001028D0(A19R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001028D0) return a19r_latch(o, 0x001028D0u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0) return a19r_latch(o, 0x001028D0u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_00102918(A19R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_00102918) return a19r_latch(o, 0x00102918u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00102918(o->h->ctx, a0, a1, a2) < 0) return a19r_latch(o, 0x00102918u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_00102948(A19R *o, uint32_t a0, uint32_t a1)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_00102948) return a19r_latch(o, 0x00102948u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00102948(o->h->ctx, a0, a1) < 0) return a19r_latch(o, 0x00102948u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_00102958(A19R *o, uint32_t a0, uint32_t a1)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_00102958) return a19r_latch(o, 0x00102958u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00102958(o->h->ctx, a0, a1) < 0) return a19r_latch(o, 0x00102958u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_001029C0(A19R *o, uint32_t a0)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001029C0) return a19r_latch(o, 0x001029C0u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001029C0(o->h->ctx, a0) < 0) return a19r_latch(o, 0x001029C0u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_00102C58(A19R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_00102C58) return a19r_latch(o, 0x00102C58u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00102C58(o->h->ctx, a0, a1, a2) < 0) return a19r_latch(o, 0x00102C58u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_0011DBB8(A19R *o, float a0, float *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_0011DBB8) return a19r_latch(o, 0x0011DBB8u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011DBB8(o->h->ctx, a0, &out) < 0) return a19r_latch(o, 0x0011DBB8u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_0011DF78(A19R *o, float a0, float *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_0011DF78) return a19r_latch(o, 0x0011DF78u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011DF78(o->h->ctx, a0, &out) < 0) return a19r_latch(o, 0x0011DF78u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_0011E2A8(A19R *o, float a0, float *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return a19r_latch(o, 0x0011E2A8u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E2A8(o->h->ctx, a0, &out) < 0) return a19r_latch(o, 0x0011E2A8u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_0011E520(A19R *o, float a0, float *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_0011E520) return a19r_latch(o, 0x0011E520u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E520(o->h->ctx, a0, &out) < 0) return a19r_latch(o, 0x0011E520u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_0011E748(A19R *o, float a0, float *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_0011E748) return a19r_latch(o, 0x0011E748u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E748(o->h->ctx, a0, &out) < 0) return a19r_latch(o, 0x0011E748u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_00122BB8(A19R *o, int32_t *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_00122BB8) return a19r_latch(o, 0x00122BB8u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_00122BB8(o->h->ctx, &out) < 0) return a19r_latch(o, 0x00122BB8u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_0019B6C0(A19R *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_0019B6C0) return a19r_latch(o, 0x0019B6C0u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_0019B6C0(o->h->ctx, a0, a1, a2, &out) < 0) return a19r_latch(o, 0x0019B6C0u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_001A2370(A19R *o, uint32_t a0, uint32_t a1)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001A2370) return a19r_latch(o, 0x001A2370u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001A2370(o->h->ctx, a0, a1) < 0) return a19r_latch(o, 0x001A2370u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_001AFA90(A19R *o, int32_t a0, uint32_t *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001AFA90) return a19r_latch(o, 0x001AFA90u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    uint32_t out = 0;
    if (o->h->w_001AFA90(o->h->ctx, a0, &out) < 0) return a19r_latch(o, 0x001AFA90u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_001AFC10(A19R *o, uint32_t a0)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001AFC10) return a19r_latch(o, 0x001AFC10u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001AFC10(o->h->ctx, a0) < 0) return a19r_latch(o, 0x001AFC10u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_001B0FD0(A19R *o, uint32_t a0, int32_t *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return a19r_latch(o, 0x001B0FD0u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B0FD0(o->h->ctx, a0, &out) < 0) return a19r_latch(o, 0x001B0FD0u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_001B1470(A19R *o, float a0, float *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001B1470) return a19r_latch(o, 0x001B1470u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B1470(o->h->ctx, a0, &out) < 0) return a19r_latch(o, 0x001B1470u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_001B17A0(A19R *o, uint32_t a0, int32_t *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001B17A0) return a19r_latch(o, 0x001B17A0u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B17A0(o->h->ctx, a0, &out) < 0) return a19r_latch(o, 0x001B17A0u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_001C6380(A19R *o, uint32_t a0)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001C6380) return a19r_latch(o, 0x001C6380u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001C6380(o->h->ctx, a0) < 0) return a19r_latch(o, 0x001C6380u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_001CCF70(A19R *o, uint32_t a0, int32_t *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001CCF70) return a19r_latch(o, 0x001CCF70u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001CCF70(o->h->ctx, a0, &out) < 0) return a19r_latch(o, 0x001CCF70u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_001CFA60(A19R *o, uint32_t a0, uint32_t a1, float a2, float a3)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001CFA60) return a19r_latch(o, 0x001CFA60u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001CFA60(o->h->ctx, a0, a1, a2, a3) < 0) return a19r_latch(o, 0x001CFA60u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_001CFBE0(A19R *o, int32_t a0, int32_t a1, uint32_t a2, uint32_t a3, int32_t a4)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001CFBE0) return a19r_latch(o, 0x001CFBE0u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001CFBE0(o->h->ctx, a0, a1, a2, a3, a4) < 0) return a19r_latch(o, 0x001CFBE0u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_001E8B90(A19R *o, uint32_t a0, float a1)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001E8B90) return a19r_latch(o, 0x001E8B90u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001E8B90(o->h->ctx, a0, a1) < 0) return a19r_latch(o, 0x001E8B90u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_001EFD90(A19R *o, int32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001EFD90) return a19r_latch(o, 0x001EFD90u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001EFD90(o->h->ctx, a0, a1, a2, &out) < 0) return a19r_latch(o, 0x001EFD90u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_001FBD50(A19R *o, uint32_t a0, int32_t a1, int32_t a2, float a3, int32_t *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001FBD50) return a19r_latch(o, 0x001FBD50u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001FBD50(o->h->ctx, a0, a1, a2, a3, &out) < 0) return a19r_latch(o, 0x001FBD50u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_001FC3C0(A19R *o, uint32_t a0, uint32_t a1, int32_t a2, float a3, float a4)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001FC3C0) return a19r_latch(o, 0x001FC3C0u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001FC3C0(o->h->ctx, a0, a1, a2, a3, a4) < 0) return a19r_latch(o, 0x001FC3C0u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_001FC520(A19R *o, uint32_t a0)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_001FC520) return a19r_latch(o, 0x001FC520u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_001FC520(o->h->ctx, a0) < 0) return a19r_latch(o, 0x001FC520u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a19r_c_00829370(A19R *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_00829370) return a19r_latch(o, 0x00829370u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_00829370(o->h->ctx, a0, a1, &out) < 0) return a19r_latch(o, 0x00829370u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a19r_c_00829840(A19R *o, uint32_t a0)
{
    if (a19r_failed(o)) return -1;
    if (!o->h->w_00829840) return a19r_latch(o, 0x00829840u, EM_AREA19_ROUTE_FAULT_NULL_WORKER);
    if (o->h->w_00829840(o->h->ctx, a0) < 0) return a19r_latch(o, 0x00829840u, EM_AREA19_ROUTE_FAULT_WORKER_FAILED);
    return 0;
}

/* END GENERATED WRAPPERS */
#endif
