/* Shared plumbing of em_area06_port*.c: the memory view, fault latch, EE
 * float helpers, one typed wrapper per hook, and the internal entry points
 * the translations call directly (the design of em_area22_port_internal.h /
 * em_area04_port_internal.h). Not a public interface. */
#ifndef EM_AREA06_PORT_INTERNAL_H
#define EM_AREA06_PORT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area06_port.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea06PortHooks *h;
    EmArea06PortFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A6;

static inline int a6_latch(A6 *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA06_PORT_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int a6_failed(const A6 *o) { return o->fault->code != EM_AREA06_PORT_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *a6_at(A6 *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!a6_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) a6_latch(o, address, EM_AREA06_PORT_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t a6_u8(A6 *o, uint32_t a) { return *a6_at(o, a, 1); }
static inline int32_t a6_s8(A6 *o, uint32_t a) { return (int8_t)*a6_at(o, a, 1); }
static inline uint32_t a6_u16(A6 *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, a6_at(o, a, 2), 2);
    return v;
}
static inline int32_t a6_s16(A6 *o, uint32_t a) { return (int16_t)a6_u16(o, a); }
static inline uint32_t a6_u32(A6 *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, a6_at(o, a, 4), 4);
    return v;
}
static inline uint64_t a6_u64(A6 *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, a6_at(o, a, 8), 8);
    return v;
}
/* A quadword load / store (lq / sq) at an address the caller has aligned
 * to 16, as its two doublewords, low then high (the access granularity the
 * test's EE core models and compares). */
static inline void a6_q(A6 *o, uint32_t a, uint8_t out[16])
{
    memcpy(out, a6_at(o, a, 8), 8);
    memcpy(out + 8, a6_at(o, a + 8, 8), 8);
}
static inline void a6_wq(A6 *o, uint32_t a, const uint8_t in[16])
{
    memcpy(a6_at(o, a, 8), in, 8);
    memcpy(a6_at(o, a + 8, 8), in + 8, 8);
}

static inline void a6_w8(A6 *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(a6_at(o, a, 1), &b, 1);
}
static inline void a6_w16(A6 *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(a6_at(o, a, 2), &h, 2);
}
static inline void a6_w32(A6 *o, uint32_t a, uint32_t v) { memcpy(a6_at(o, a, 4), &v, 4); }

/* A float argument or result as its bits, and back (no conversion). */
static inline uint32_t a6_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float a6_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define A6_ADD(a, b) em_ee_add_bits((a), (b))
#define A6_SUB(a, b) em_ee_sub_bits((a), (b))
#define A6_MUL(a, b) em_ee_mul_bits((a), (b))
#define A6_DIV(a, b) em_ee_div_bits((a), (b))
#define A6_MADD(acc, a, b) em_ee_madd_bits((acc), (a), (b))
#define A6_MSUB(acc, a, b) em_ee_msub_bits((acc), (a), (b))
#define A6_CVT(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define A6_LT(a, b) em_ee_c_lt_bits((a), (b))
#define A6_LE(a, b) em_ee_c_le_bits((a), (b))
#define A6_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define A6_NEG(a) em_ee_neg_bits((a))

/* x*x + y*y + z*z as the originals compute it: mul, mul, adda, madd. */
static inline uint32_t a6_sumsq3(uint32_t x, uint32_t y, uint32_t z)
{
    return A6_MADD(A6_ADD(A6_MUL(x, x), A6_MUL(y, y)), z, z);
}

static inline void a6_open(A6 *o, const EmArea06PortHooks *h, EmArea06PortFault *fault)
{
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
}

static inline int a6_begin(A6 *o, const EmArea06PortHooks *h, EmArea06PortFault *fault)
{
    if (!h || !fault) return -1;
    a6_open(o, h, fault);
    return a6_failed(o) ? -1 : 0;
}

static inline int a6_end(const A6 *o) { return a6_failed(o) ? -1 : 0; }

/* The actor's +0x4C method: the function word is read by the caller. */
static inline int a6_callback(A6 *o, uint32_t fn, uint32_t actor)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_callback) return a6_latch(o, fn, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, actor) < 0 ? a6_latch(o, fn, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Each returns 0 on success, -1 when skipped or failed. */
static inline int a6_c_0011DE90(A6 *o, float f0, float *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_0011DE90) return a6_latch(o, 0x0011DE90u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011DE90(o->h->ctx, f0, result) < 0 ? a6_latch(o, 0x0011DE90u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_0011DF78(A6 *o, float f0, float *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_0011DF78) return a6_latch(o, 0x0011DF78u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011DF78(o->h->ctx, f0, result) < 0 ? a6_latch(o, 0x0011DF78u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_0011E2A8(A6 *o, float f0, float *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return a6_latch(o, 0x0011E2A8u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011E2A8(o->h->ctx, f0, result) < 0 ? a6_latch(o, 0x0011E2A8u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_0011E748(A6 *o, float f0, float *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_0011E748) return a6_latch(o, 0x0011E748u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011E748(o->h->ctx, f0, result) < 0 ? a6_latch(o, 0x0011E748u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001026A0(A6 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001026A0) return a6_latch(o, 0x001026A0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0 ? a6_latch(o, 0x001026A0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00102738(A6 *o, uint32_t a0, uint32_t a1, float *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00102738) return a6_latch(o, 0x00102738u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102738(o->h->ctx, a0, a1, result) < 0 ? a6_latch(o, 0x00102738u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00102760(A6 *o, uint32_t a0, uint32_t a1)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00102760) return a6_latch(o, 0x00102760u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102760(o->h->ctx, a0, a1) < 0 ? a6_latch(o, 0x00102760u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001028B8(A6 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001028B8) return a6_latch(o, 0x001028B8u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0 ? a6_latch(o, 0x001028B8u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001028D0(A6 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001028D0) return a6_latch(o, 0x001028D0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0 ? a6_latch(o, 0x001028D0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00102900(A6 *o, uint32_t a0, uint32_t a1, float f2)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00102900) return a6_latch(o, 0x00102900u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102900(o->h->ctx, a0, a1, f2) < 0 ? a6_latch(o, 0x00102900u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00102918(A6 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00102918) return a6_latch(o, 0x00102918u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102918(o->h->ctx, a0, a1, a2) < 0 ? a6_latch(o, 0x00102918u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00102948(A6 *o, uint32_t a0, uint32_t a1)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00102948) return a6_latch(o, 0x00102948u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, a0, a1) < 0 ? a6_latch(o, 0x00102948u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00102958(A6 *o, uint32_t a0, uint32_t a1)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00102958) return a6_latch(o, 0x00102958u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102958(o->h->ctx, a0, a1) < 0 ? a6_latch(o, 0x00102958u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001029C0(A6 *o, uint32_t a0)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001029C0) return a6_latch(o, 0x001029C0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001029C0(o->h->ctx, a0) < 0 ? a6_latch(o, 0x001029C0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00102BB0(A6 *o, uint32_t a0, uint32_t a1, float f2)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00102BB0) return a6_latch(o, 0x00102BB0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102BB0(o->h->ctx, a0, a1, f2) < 0 ? a6_latch(o, 0x00102BB0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00122BB8(A6 *o, int32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00122BB8) return a6_latch(o, 0x00122BB8u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? a6_latch(o, 0x00122BB8u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001281C0(A6 *o, float f0, int32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001281C0) return a6_latch(o, 0x001281C0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001281C0(o->h->ctx, f0, result) < 0 ? a6_latch(o, 0x001281C0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001749A0(A6 *o, uint32_t a0, int32_t n1, int32_t n2, float f3)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001749A0) return a6_latch(o, 0x001749A0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001749A0(o->h->ctx, a0, n1, n2, f3) < 0 ? a6_latch(o, 0x001749A0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00182F90(A6 *o, uint32_t a0, uint32_t a1)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00182F90) return a6_latch(o, 0x00182F90u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00182F90(o->h->ctx, a0, a1) < 0 ? a6_latch(o, 0x00182F90u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_0018C4B0(A6 *o, uint32_t a0, float f1, float f2)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_0018C4B0) return a6_latch(o, 0x0018C4B0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018C4B0(o->h->ctx, a0, f1, f2) < 0 ? a6_latch(o, 0x0018C4B0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_0018C6A0(A6 *o, uint32_t a0, uint32_t a1, float f2)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_0018C6A0) return a6_latch(o, 0x0018C6A0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018C6A0(o->h->ctx, a0, a1, f2) < 0 ? a6_latch(o, 0x0018C6A0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_0018D7B0(A6 *o, uint32_t a0, int32_t n1)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_0018D7B0) return a6_latch(o, 0x0018D7B0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018D7B0(o->h->ctx, a0, n1) < 0 ? a6_latch(o, 0x0018D7B0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00191120(A6 *o, int32_t n0, float f1, float f2, float f3, float f4, float *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00191120) return a6_latch(o, 0x00191120u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00191120(o->h->ctx, n0, f1, f2, f3, f4, result) < 0 ? a6_latch(o, 0x00191120u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00194240(A6 *o, uint32_t a0)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00194240) return a6_latch(o, 0x00194240u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00194240(o->h->ctx, a0) < 0 ? a6_latch(o, 0x00194240u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_0019A570(A6 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_0019A570) return a6_latch(o, 0x0019A570u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019A570(o->h->ctx, a0, a1, n2, n3, result) < 0 ? a6_latch(o, 0x0019A570u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_0019AA80(A6 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_0019AA80) return a6_latch(o, 0x0019AA80u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019AA80(o->h->ctx, a0, a1, n2, result) < 0 ? a6_latch(o, 0x0019AA80u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_0019AFE0(A6 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_0019AFE0) return a6_latch(o, 0x0019AFE0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019AFE0(o->h->ctx, a0, a1, a2, n3, result) < 0 ? a6_latch(o, 0x0019AFE0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_0019C6F0(A6 *o, int32_t n0, int32_t n1)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_0019C6F0) return a6_latch(o, 0x0019C6F0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019C6F0(o->h->ctx, n0, n1) < 0 ? a6_latch(o, 0x0019C6F0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001A2370(A6 *o, uint32_t a0, uint32_t a1)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001A2370) return a6_latch(o, 0x001A2370u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001A2370(o->h->ctx, a0, a1) < 0 ? a6_latch(o, 0x001A2370u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001AED80(A6 *o, int32_t n0)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001AED80) return a6_latch(o, 0x001AED80u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AED80(o->h->ctx, n0) < 0 ? a6_latch(o, 0x001AED80u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001AEDB0(A6 *o, int32_t n0, int32_t n1, uint32_t a2)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001AEDB0) return a6_latch(o, 0x001AEDB0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AEDB0(o->h->ctx, n0, n1, a2) < 0 ? a6_latch(o, 0x001AEDB0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001AF800(A6 *o, uint32_t a0)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001AF800) return a6_latch(o, 0x001AF800u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AF800(o->h->ctx, a0) < 0 ? a6_latch(o, 0x001AF800u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001AFC10(A6 *o, uint32_t a0)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001AFC10) return a6_latch(o, 0x001AFC10u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, a0) < 0 ? a6_latch(o, 0x001AFC10u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001AFF10(A6 *o, uint32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001AFF10) return a6_latch(o, 0x001AFF10u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFF10(o->h->ctx, result) < 0 ? a6_latch(o, 0x001AFF10u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001AFF90(A6 *o, uint32_t a0)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001AFF90) return a6_latch(o, 0x001AFF90u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFF90(o->h->ctx, a0) < 0 ? a6_latch(o, 0x001AFF90u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001B0000(A6 *o)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001B0000) return a6_latch(o, 0x001B0000u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0000(o->h->ctx) < 0 ? a6_latch(o, 0x001B0000u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001B0FD0(A6 *o, uint32_t a0, int32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return a6_latch(o, 0x001B0FD0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0FD0(o->h->ctx, a0, result) < 0 ? a6_latch(o, 0x001B0FD0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001B1190(A6 *o, int32_t n0)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001B1190) return a6_latch(o, 0x001B1190u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1190(o->h->ctx, n0) < 0 ? a6_latch(o, 0x001B1190u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001B11E0(A6 *o, int32_t n0, int32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001B11E0) return a6_latch(o, 0x001B11E0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B11E0(o->h->ctx, n0, result) < 0 ? a6_latch(o, 0x001B11E0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001B1240(A6 *o, uint32_t a0, float f1, float f2, float *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001B1240) return a6_latch(o, 0x001B1240u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1240(o->h->ctx, a0, f1, f2, result) < 0 ? a6_latch(o, 0x001B1240u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001B12B0(A6 *o, float f0, float f1, float f2, float *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001B12B0) return a6_latch(o, 0x001B12B0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B12B0(o->h->ctx, f0, f1, f2, result) < 0 ? a6_latch(o, 0x001B12B0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001B1470(A6 *o, float f0, float *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001B1470) return a6_latch(o, 0x001B1470u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1470(o->h->ctx, f0, result) < 0 ? a6_latch(o, 0x001B1470u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001B1630(A6 *o, float f0, float f1, float f2, int32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001B1630) return a6_latch(o, 0x001B1630u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1630(o->h->ctx, f0, f1, f2, result) < 0 ? a6_latch(o, 0x001B1630u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001B1B70(A6 *o, uint32_t a0)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001B1B70) return a6_latch(o, 0x001B1B70u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1B70(o->h->ctx, a0) < 0 ? a6_latch(o, 0x001B1B70u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001B1EA0(A6 *o, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return a6_latch(o, 0x001B1EA0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1EA0(o->h->ctx, n0, a1, a2, n3, result) < 0 ? a6_latch(o, 0x001B1EA0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001BA1A0(A6 *o, uint32_t a0, uint32_t a1)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return a6_latch(o, 0x001BA1A0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0 ? a6_latch(o, 0x001BA1A0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001BA1C0(A6 *o, uint32_t a0, int32_t n1, int32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return a6_latch(o, 0x001BA1C0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1C0(o->h->ctx, a0, n1, result) < 0 ? a6_latch(o, 0x001BA1C0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001BA1F0(A6 *o, uint32_t a0)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return a6_latch(o, 0x001BA1F0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, a0) < 0 ? a6_latch(o, 0x001BA1F0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001C5570(A6 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, uint32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001C5570) return a6_latch(o, 0x001C5570u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C5570(o->h->ctx, a0, a1, n2, n3, result) < 0 ? a6_latch(o, 0x001C5570u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001C6380(A6 *o, uint32_t a0)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001C6380) return a6_latch(o, 0x001C6380u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6380(o->h->ctx, a0) < 0 ? a6_latch(o, 0x001C6380u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001CB5B0(A6 *o, int32_t n0)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001CB5B0) return a6_latch(o, 0x001CB5B0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CB5B0(o->h->ctx, n0) < 0 ? a6_latch(o, 0x001CB5B0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001CB5F0(A6 *o, uint32_t a0, int32_t n1, int32_t n2, uint32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001CB5F0) return a6_latch(o, 0x001CB5F0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CB5F0(o->h->ctx, a0, n1, n2, result) < 0 ? a6_latch(o, 0x001CB5F0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001CB900(A6 *o, uint32_t a0, int32_t n1, int32_t n2)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001CB900) return a6_latch(o, 0x001CB900u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CB900(o->h->ctx, a0, n1, n2) < 0 ? a6_latch(o, 0x001CB900u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001CC1E0(A6 *o, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, uint32_t a5, int32_t n6)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001CC1E0) return a6_latch(o, 0x001CC1E0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CC1E0(o->h->ctx, n0, n1, n2, n3, n4, a5, n6) < 0 ? a6_latch(o, 0x001CC1E0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001CCF70(A6 *o, uint32_t a0, int32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001CCF70) return a6_latch(o, 0x001CCF70u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CCF70(o->h->ctx, a0, result) < 0 ? a6_latch(o, 0x001CCF70u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001CD370(A6 *o, int32_t n0, uint32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001CD370) return a6_latch(o, 0x001CD370u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CD370(o->h->ctx, n0, result) < 0 ? a6_latch(o, 0x001CD370u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001CD390(A6 *o, uint32_t a0, uint32_t a1)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001CD390) return a6_latch(o, 0x001CD390u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CD390(o->h->ctx, a0, a1) < 0 ? a6_latch(o, 0x001CD390u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001CFA60(A6 *o, uint32_t a0, uint32_t a1, float f2, float f3)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001CFA60) return a6_latch(o, 0x001CFA60u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFA60(o->h->ctx, a0, a1, f2, f3) < 0 ? a6_latch(o, 0x001CFA60u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001CFB50(A6 *o, uint32_t a0, int32_t n1, int32_t n2, float f3, float f4, float f5, float f6, float f7)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001CFB50) return a6_latch(o, 0x001CFB50u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFB50(o->h->ctx, a0, n1, n2, f3, f4, f5, f6, f7) < 0 ? a6_latch(o, 0x001CFB50u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001CFBE0(A6 *o, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001CFBE0) return a6_latch(o, 0x001CFBE0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFBE0(o->h->ctx, n0, n1, a2, a3, n4) < 0 ? a6_latch(o, 0x001CFBE0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001D2DE0(A6 *o, int32_t n0, int32_t n1)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001D2DE0) return a6_latch(o, 0x001D2DE0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D2DE0(o->h->ctx, n0, n1) < 0 ? a6_latch(o, 0x001D2DE0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001EFD20(A6 *o, int32_t n0, uint32_t a1)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001EFD20) return a6_latch(o, 0x001EFD20u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFD20(o->h->ctx, n0, a1) < 0 ? a6_latch(o, 0x001EFD20u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001EFEB0(A6 *o, int32_t n0, uint32_t a1, uint32_t *result)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001EFEB0) return a6_latch(o, 0x001EFEB0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFEB0(o->h->ctx, n0, a1, result) < 0 ? a6_latch(o, 0x001EFEB0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001F02C0(A6 *o, uint32_t a0, int32_t n1, float f2)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001F02C0) return a6_latch(o, 0x001F02C0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F02C0(o->h->ctx, a0, n1, f2) < 0 ? a6_latch(o, 0x001F02C0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001FABB0(A6 *o)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001FABB0) return a6_latch(o, 0x001FABB0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FABB0(o->h->ctx) < 0 ? a6_latch(o, 0x001FABB0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001FB0B0(A6 *o, int32_t n0)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001FB0B0) return a6_latch(o, 0x001FB0B0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FB0B0(o->h->ctx, n0) < 0 ? a6_latch(o, 0x001FB0B0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001FB9F0(A6 *o, int32_t n0, int32_t n1, int32_t n2, int32_t n3)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001FB9F0) return a6_latch(o, 0x001FB9F0u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FB9F0(o->h->ctx, n0, n1, n2, n3) < 0 ? a6_latch(o, 0x001FB9F0u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_001FBD50(A6 *o, uint32_t a0, int32_t n1, int32_t n2, float f3)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_001FBD50) return a6_latch(o, 0x001FBD50u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FBD50(o->h->ctx, a0, n1, n2, f3) < 0 ? a6_latch(o, 0x001FBD50u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00207D00(A6 *o, int32_t n0, int32_t n1)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00207D00) return a6_latch(o, 0x00207D00u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00207D00(o->h->ctx, n0, n1) < 0 ? a6_latch(o, 0x00207D00u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_00207E40(A6 *o, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, uint32_t a5, uint64_t q6)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_00207E40) return a6_latch(o, 0x00207E40u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_00207E40(o->h->ctx, n0, n1, n2, n3, n4, a5, q6) < 0 ? a6_latch(o, 0x00207E40u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a6_c_0020CD60(A6 *o)
{
    if (a6_failed(o)) return -1;
    if (!o->h->w_0020CD60) return a6_latch(o, 0x0020CD60u, EM_AREA06_PORT_FAULT_NULL_WORKER);
    return o->h->w_0020CD60(o->h->ctx) < 0 ? a6_latch(o, 0x0020CD60u, EM_AREA06_PORT_FAULT_WORKER_FAILED) : 0;
}


/* Original globals, scratchpad words and float constants the
 * translations name. */
#define D_00264FE0 0x00264FE0u /* keypad cell positions (x, y halfwords) */
#define D_00265010 0x00265010u /* keypad cell digit / texture row bytes */
#define D_00266AE0 0x00266AE0u
#define D_00266B00 0x00266B00u
#define D_00275868 0x00275868u
#define D_00275B40 0x00275B40u
#define D_00275C34 0x00275C34u
#define D_002754CC 0x002754CCu
#define D_00281B50 0x00281B50u
#define D_00810360 0x00810360u /* player block + 0xB0 */
#define D_008102C8 0x008102C8u
#define D_00810690 0x00810690u
#define D_008105D0 0x008105D0u /* camera eye (vec4) */
#define D_008105E0 0x008105E0u
#define D_008105E8 0x008105E8u
#define D_00810700 0x00810700u
#define D_00810701 0x00810701u
#define D_0081F8F0 0x0081F8F0u
#define D_0024A530 0x0024A530u /* camera preset rows, 20 bytes each */

/* Scratchpad. */
#define S_700031B0 0x700031B0u
#define S_700031C0 0x700031C0u
#define S_700031C8 0x700031C8u
#define S_700031F0 0x700031F0u
#define S_700031FC 0x700031FCu
#define S_70003204 0x70003204u
#define S_700036A0 0x700036A0u
#define S_700036C0 0x700036C0u
#define S_700036D0 0x700036D0u
#define S_700036E0 0x700036E0u
#define S_70003710 0x70003710u
#define S_700038A0 0x700038A0u
#define S_700038B0 0x700038B0u
#define S_70003A20 0x70003A20u
#define S_70003A24 0x70003A24u
#define S_70003A28 0x70003A28u
#define S_70003B88 0x70003B88u

/* Float constants (bit patterns, as the original materializes them). */
#define F_ZERO    0x00000000u
#define F_ONE     0x3F800000u
#define F_QUARTER 0x3E800000u
#define F_0_1     0x3DCCCCCDu
#define F_0_2     0x3E4CCCCDu
#define F_0_8     0x3F4CCCCDu
#define F_1_1     0x3F8CCCCDu
#define F_2_25    0x40100000u
#define F_3       0x40400000u
#define F_4_5     0x40900000u
#define F_6       0x40C00000u
#define F_8       0x41000000u
#define F_16      0x41800000u
#define F_100     0x42C80000u
#define F_128     0x43000000u
#define F_300     0x43960000u
#define F_HALF    0x3F000000u
#define F_1EM6    0x358637BDu /* 1e-6 */
#define F_M0_015  0xBC75C28Fu
#define F_PI_8    0x3EC90FDBu /* pi / 8 */
#define F_PI_2    0x3FC90FDBu
#define F_PI_6    0x3F060A92u /* pi / 6 */
#define F_PI_600  0x3BAB92A7u /* pi / 600 */
#define F_PI_90   0x3D0EFA35u /* pi / 90 */
#define F_M46_8   0xC23B3333u
#define F_M30     0xC1F00000u
#define F_M20     0xC1A00000u
#define F_M365    0xC3B68000u
#define F_749_5   0x443B6000u
#define F_1010    0x447C8000u

static inline float fl(uint32_t bits) { return a6_float(bits); }

/* Translations called by other translations (direct calls, no hook). */
void a6_001885B0(A6 *o, uint32_t actor, int32_t *result);
void a6_0019F680(A6 *o, uint32_t out, uint32_t rec, int32_t index, int32_t *result);
void a6_001EA210(A6 *o, uint32_t value_bits);
void a6_002072A0(A6 *o);
void a6_002079F0(A6 *o, uint32_t page, uint32_t tex);
void a6_00207BB0(A6 *o, uint32_t page, uint32_t tex, uint32_t sp);
void a6_00207CA0(A6 *o, uint32_t tex);
void a6_00207CD0(A6 *o, uint32_t tex);
int32_t a6_00123020(A6 *o, uint32_t a, uint32_t b);
void a6_00219F50(A6 *o, uint32_t self);
int32_t a6_0021A180(A6 *o, uint32_t self, uint32_t sp);
void a6_0021A440(A6 *o, uint32_t self, uint32_t src);

#endif /* EM_AREA06_PORT_INTERNAL_H */
