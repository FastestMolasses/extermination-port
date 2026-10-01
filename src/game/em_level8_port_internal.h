/* Shared plumbing of em_level8_port*.c: the memory view, fault latch, EE
 * float helpers, one typed wrapper per hook (generated from one table), and
 * the internal entry points the translations call directly (the design of
 * em_area06_port_internal.h). Not a public interface. */
#ifndef EM_LEVEL8_PORT_INTERNAL_H
#define EM_LEVEL8_PORT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level8_port.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel8PortHooks *h;
    EmLevel8PortFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L8;

static inline int l8_latch(L8 *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL8_PORT_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l8_failed(const L8 *o) { return o->fault->code != EM_LEVEL8_PORT_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l8_at(L8 *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l8_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l8_latch(o, address, EM_LEVEL8_PORT_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l8_u8(L8 *o, uint32_t a) { return *l8_at(o, a, 1); }
static inline int32_t l8_s8(L8 *o, uint32_t a) { return (int8_t)*l8_at(o, a, 1); }
static inline uint32_t l8_u16(L8 *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l8_at(o, a, 2), 2);
    return v;
}
static inline int32_t l8_s16(L8 *o, uint32_t a) { return (int16_t)l8_u16(o, a); }
static inline uint32_t l8_u32(L8 *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l8_at(o, a, 4), 4);
    return v;
}
static inline uint64_t l8_u64(L8 *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, l8_at(o, a, 8), 8);
    return v;
}
/* A quadword load / store (lq / sq) at an address the caller has aligned
 * to 16, as its two doublewords, low then high (the access granularity the
 * test's EE core models and compares). */
static inline void l8_q(L8 *o, uint32_t a, uint8_t out[16])
{
    memcpy(out, l8_at(o, a, 8), 8);
    memcpy(out + 8, l8_at(o, a + 8, 8), 8);
}
static inline void l8_wq(L8 *o, uint32_t a, const uint8_t in[16])
{
    memcpy(l8_at(o, a, 8), in, 8);
    memcpy(l8_at(o, a + 8, 8), in + 8, 8);
}

static inline void l8_w8(L8 *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l8_at(o, a, 1), &b, 1);
}
static inline void l8_w16(L8 *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l8_at(o, a, 2), &h, 2);
}
static inline void l8_w32(L8 *o, uint32_t a, uint32_t v) { memcpy(l8_at(o, a, 4), &v, 4); }

/* A float argument or result as its bits, and back (no conversion). */
static inline uint32_t l8_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l8_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L8_ADD(a, b) em_ee_add_bits((a), (b))
#define L8_SUB(a, b) em_ee_sub_bits((a), (b))
#define L8_MUL(a, b) em_ee_mul_bits((a), (b))
#define L8_DIV(a, b) em_ee_div_bits((a), (b))
#define L8_MADD(acc, a, b) em_ee_madd_bits((acc), (a), (b))
#define L8_MSUB(acc, a, b) em_ee_msub_bits((acc), (a), (b))
#define L8_CVT(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define L8_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L8_LE(a, b) em_ee_c_le_bits((a), (b))
#define L8_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L8_NEG(a) em_ee_neg_bits((a))

/* x*x + y*y + z*z as the originals compute it: mul, mul, adda, madd. */
static inline uint32_t l8_sumsq3(uint32_t x, uint32_t y, uint32_t z)
{
    return L8_MADD(L8_ADD(L8_MUL(x, x), L8_MUL(y, y)), z, z);
}

static inline void l8_open(L8 *o, const EmLevel8PortHooks *h, EmLevel8PortFault *fault)
{
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
}

static inline int l8_begin(L8 *o, const EmLevel8PortHooks *h, EmLevel8PortFault *fault)
{
    if (!h || !fault) return -1;
    l8_open(o, h, fault);
    return l8_failed(o) ? -1 : 0;
}

static inline int l8_end(const L8 *o) { return l8_failed(o) ? -1 : 0; }

/* The actor's +0x4C method: the function word is read by the caller. */
static inline int l8_callback(L8 *o, uint32_t fn, uint32_t actor)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_callback) return l8_latch(o, fn, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, actor) < 0 ? l8_latch(o, fn, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Each returns 0 on success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l8_c_001000E0(L8 *o, uint64_t q0, uint64_t q1, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001000E0) return l8_latch(o, 0x001000E0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001000E0(o->h->ctx, q0, q1, result) < 0 ? l8_latch(o, 0x001000E0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001026A0(L8 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001026A0) return l8_latch(o, 0x001026A0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0 ? l8_latch(o, 0x001026A0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001026D0(L8 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001026D0) return l8_latch(o, 0x001026D0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001026D0(o->h->ctx, a0, a1, a2) < 0 ? l8_latch(o, 0x001026D0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00102718(L8 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00102718) return l8_latch(o, 0x00102718u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102718(o->h->ctx, a0, a1, a2) < 0 ? l8_latch(o, 0x00102718u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00102738(L8 *o, uint32_t a0, uint32_t a1, float *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00102738) return l8_latch(o, 0x00102738u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102738(o->h->ctx, a0, a1, result) < 0 ? l8_latch(o, 0x00102738u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00102760(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00102760) return l8_latch(o, 0x00102760u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102760(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x00102760u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001028B8(L8 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001028B8) return l8_latch(o, 0x001028B8u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0 ? l8_latch(o, 0x001028B8u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001028D0(L8 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001028D0) return l8_latch(o, 0x001028D0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0 ? l8_latch(o, 0x001028D0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00102948(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00102948) return l8_latch(o, 0x00102948u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x00102948u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00102958(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00102958) return l8_latch(o, 0x00102958u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102958(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x00102958u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001029C0(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001029C0) return l8_latch(o, 0x001029C0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001029C0(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001029C0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00102B08(L8 *o, uint32_t a0, uint32_t a1, float f2)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00102B08) return l8_latch(o, 0x00102B08u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102B08(o->h->ctx, a0, a1, f2) < 0 ? l8_latch(o, 0x00102B08u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00102BB0(L8 *o, uint32_t a0, uint32_t a1, float f2)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00102BB0) return l8_latch(o, 0x00102BB0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102BB0(o->h->ctx, a0, a1, f2) < 0 ? l8_latch(o, 0x00102BB0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00102C58(L8 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00102C58) return l8_latch(o, 0x00102C58u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102C58(o->h->ctx, a0, a1, a2) < 0 ? l8_latch(o, 0x00102C58u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0011DE90(L8 *o, float f0, float *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0011DE90) return l8_latch(o, 0x0011DE90u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011DE90(o->h->ctx, f0, result) < 0 ? l8_latch(o, 0x0011DE90u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0011DF78(L8 *o, float f0, float *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0011DF78) return l8_latch(o, 0x0011DF78u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011DF78(o->h->ctx, f0, result) < 0 ? l8_latch(o, 0x0011DF78u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0011E2A8(L8 *o, float f0, float *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return l8_latch(o, 0x0011E2A8u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011E2A8(o->h->ctx, f0, result) < 0 ? l8_latch(o, 0x0011E2A8u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0011E620(L8 *o, float f0, float f1, float *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0011E620) return l8_latch(o, 0x0011E620u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011E620(o->h->ctx, f0, f1, result) < 0 ? l8_latch(o, 0x0011E620u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0011E748(L8 *o, float f0, float *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0011E748) return l8_latch(o, 0x0011E748u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011E748(o->h->ctx, f0, result) < 0 ? l8_latch(o, 0x0011E748u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00122BB8(L8 *o, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00122BB8) return l8_latch(o, 0x00122BB8u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? l8_latch(o, 0x00122BB8u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001281C0(L8 *o, float f0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001281C0) return l8_latch(o, 0x001281C0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001281C0(o->h->ctx, f0, result) < 0 ? l8_latch(o, 0x001281C0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00128250(L8 *o, float f0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00128250) return l8_latch(o, 0x00128250u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00128250(o->h->ctx, f0, result) < 0 ? l8_latch(o, 0x00128250u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00128350(L8 *o, float f0, uint64_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00128350) return l8_latch(o, 0x00128350u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00128350(o->h->ctx, f0, result) < 0 ? l8_latch(o, 0x00128350u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00128640(L8 *o, uint32_t a0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00128640) return l8_latch(o, 0x00128640u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00128640(o->h->ctx, a0, result) < 0 ? l8_latch(o, 0x00128640u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001287F0(L8 *o, uint32_t a0, uint32_t a1, int32_t n2, float f3)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001287F0) return l8_latch(o, 0x001287F0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001287F0(o->h->ctx, a0, a1, n2, f3) < 0 ? l8_latch(o, 0x001287F0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0012F6C0(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0012F6C0) return l8_latch(o, 0x0012F6C0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0012F6C0(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x0012F6C0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0012F980(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0012F980) return l8_latch(o, 0x0012F980u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0012F980(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x0012F980u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0012FA50(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0012FA50) return l8_latch(o, 0x0012FA50u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0012FA50(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x0012FA50u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001305B0(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001305B0) return l8_latch(o, 0x001305B0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001305B0(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x001305B0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00130AB0(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00130AB0) return l8_latch(o, 0x00130AB0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00130AB0(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x00130AB0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00131210(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00131210) return l8_latch(o, 0x00131210u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00131210(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x00131210u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00131510(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00131510) return l8_latch(o, 0x00131510u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00131510(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x00131510u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00131650(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00131650) return l8_latch(o, 0x00131650u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00131650(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x00131650u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00131E80(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00131E80) return l8_latch(o, 0x00131E80u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00131E80(o->h->ctx, a0) < 0 ? l8_latch(o, 0x00131E80u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00131F20(L8 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00131F20) return l8_latch(o, 0x00131F20u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00131F20(o->h->ctx, a0, a1, a2) < 0 ? l8_latch(o, 0x00131F20u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001339E0(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001339E0) return l8_latch(o, 0x001339E0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001339E0(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x001339E0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00158590(L8 *o, uint32_t a0, int32_t n1, int32_t n2)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00158590) return l8_latch(o, 0x00158590u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00158590(o->h->ctx, a0, n1, n2) < 0 ? l8_latch(o, 0x00158590u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_00182F90(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_00182F90) return l8_latch(o, 0x00182F90u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_00182F90(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x00182F90u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0018D7B0(L8 *o, uint32_t a0, int32_t n1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0018D7B0) return l8_latch(o, 0x0018D7B0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018D7B0(o->h->ctx, a0, n1) < 0 ? l8_latch(o, 0x0018D7B0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0019A310(L8 *o, uint32_t a0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0019A310) return l8_latch(o, 0x0019A310u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019A310(o->h->ctx, a0, result) < 0 ? l8_latch(o, 0x0019A310u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0019A570(L8 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0019A570) return l8_latch(o, 0x0019A570u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019A570(o->h->ctx, a0, a1, n2, n3, result) < 0 ? l8_latch(o, 0x0019A570u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0019AB20(L8 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0019AB20) return l8_latch(o, 0x0019AB20u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019AB20(o->h->ctx, a0, a1, a2, n3, result) < 0 ? l8_latch(o, 0x0019AB20u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0019AD00(L8 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0019AD00) return l8_latch(o, 0x0019AD00u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019AD00(o->h->ctx, a0, a1, n2, result) < 0 ? l8_latch(o, 0x0019AD00u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0019B6C0(L8 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0019B6C0) return l8_latch(o, 0x0019B6C0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019B6C0(o->h->ctx, a0, a1, result) < 0 ? l8_latch(o, 0x0019B6C0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0019BC40(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0019BC40) return l8_latch(o, 0x0019BC40u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019BC40(o->h->ctx, a0) < 0 ? l8_latch(o, 0x0019BC40u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001AEBA0(L8 *o, int32_t n0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001AEBA0) return l8_latch(o, 0x001AEBA0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AEBA0(o->h->ctx, n0) < 0 ? l8_latch(o, 0x001AEBA0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001AF890(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001AF890) return l8_latch(o, 0x001AF890u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AF890(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001AF890u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001AFA90(L8 *o, int32_t n0, uint32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001AFA90) return l8_latch(o, 0x001AFA90u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFA90(o->h->ctx, n0, result) < 0 ? l8_latch(o, 0x001AFA90u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001AFC10(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001AFC10) return l8_latch(o, 0x001AFC10u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001AFC10u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B0D80(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B0D80) return l8_latch(o, 0x001B0D80u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0D80(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001B0D80u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B0FD0(L8 *o, uint32_t a0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return l8_latch(o, 0x001B0FD0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0FD0(o->h->ctx, a0, result) < 0 ? l8_latch(o, 0x001B0FD0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B1020(L8 *o, uint32_t a0, int32_t n1, int32_t n2, int32_t n3)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B1020) return l8_latch(o, 0x001B1020u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1020(o->h->ctx, a0, n1, n2, n3) < 0 ? l8_latch(o, 0x001B1020u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B10B0(L8 *o, uint32_t a0, int32_t n1, int32_t n2, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B10B0) return l8_latch(o, 0x001B10B0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B10B0(o->h->ctx, a0, n1, n2, result) < 0 ? l8_latch(o, 0x001B10B0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B1190(L8 *o, int32_t n0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B1190) return l8_latch(o, 0x001B1190u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1190(o->h->ctx, n0) < 0 ? l8_latch(o, 0x001B1190u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B1240(L8 *o, uint32_t a0, float f1, float f2, float *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B1240) return l8_latch(o, 0x001B1240u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1240(o->h->ctx, a0, f1, f2, result) < 0 ? l8_latch(o, 0x001B1240u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B12B0(L8 *o, float f0, float f1, float f2, float *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B12B0) return l8_latch(o, 0x001B12B0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B12B0(o->h->ctx, f0, f1, f2, result) < 0 ? l8_latch(o, 0x001B12B0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B13F0(L8 *o, uint32_t a0, uint32_t a1, float f2, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B13F0) return l8_latch(o, 0x001B13F0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B13F0(o->h->ctx, a0, a1, f2, result) < 0 ? l8_latch(o, 0x001B13F0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B1470(L8 *o, float f0, float *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B1470) return l8_latch(o, 0x001B1470u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1470(o->h->ctx, f0, result) < 0 ? l8_latch(o, 0x001B1470u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B15D0(L8 *o, uint32_t a0, uint32_t a1, float *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B15D0) return l8_latch(o, 0x001B15D0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B15D0(o->h->ctx, a0, a1, result) < 0 ? l8_latch(o, 0x001B15D0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B17A0(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B17A0) return l8_latch(o, 0x001B17A0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B17A0(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001B17A0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B1D20(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B1D20) return l8_latch(o, 0x001B1D20u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1D20(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001B1D20u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B1EA0(L8 *o, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return l8_latch(o, 0x001B1EA0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1EA0(o->h->ctx, n0, a1, a2, n3, result) < 0 ? l8_latch(o, 0x001B1EA0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B2140(L8 *o, uint32_t a0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B2140) return l8_latch(o, 0x001B2140u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2140(o->h->ctx, a0, result) < 0 ? l8_latch(o, 0x001B2140u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B2E50(L8 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B2E50) return l8_latch(o, 0x001B2E50u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2E50(o->h->ctx, a0, a1, result) < 0 ? l8_latch(o, 0x001B2E50u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B2F70(L8 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B2F70) return l8_latch(o, 0x001B2F70u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2F70(o->h->ctx, a0, a1, result) < 0 ? l8_latch(o, 0x001B2F70u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B3250(L8 *o, uint32_t a0, uint32_t a1, float f2, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B3250) return l8_latch(o, 0x001B3250u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B3250(o->h->ctx, a0, a1, f2, result) < 0 ? l8_latch(o, 0x001B3250u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B3390(L8 *o, uint32_t a0, uint32_t a1, uint32_t a2, float f3, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B3390) return l8_latch(o, 0x001B3390u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B3390(o->h->ctx, a0, a1, a2, f3, result) < 0 ? l8_latch(o, 0x001B3390u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B37D0(L8 *o, uint32_t a0, float f1, float f2, float *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B37D0) return l8_latch(o, 0x001B37D0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B37D0(o->h->ctx, a0, f1, f2, result) < 0 ? l8_latch(o, 0x001B37D0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B3F10(L8 *o, uint32_t a0, float f1, float f2, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B3F10) return l8_latch(o, 0x001B3F10u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B3F10(o->h->ctx, a0, f1, f2, result) < 0 ? l8_latch(o, 0x001B3F10u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B4CF0(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B4CF0) return l8_latch(o, 0x001B4CF0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B4CF0(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001B4CF0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001B5360(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001B5360) return l8_latch(o, 0x001B5360u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B5360(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001B5360u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001BA1A0(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return l8_latch(o, 0x001BA1A0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x001BA1A0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001BA1F0(L8 *o, uint32_t a0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return l8_latch(o, 0x001BA1F0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, a0, result) < 0 ? l8_latch(o, 0x001BA1F0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001BA580(L8 *o, uint32_t a0, int32_t n1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001BA580) return l8_latch(o, 0x001BA580u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA580(o->h->ctx, a0, n1) < 0 ? l8_latch(o, 0x001BA580u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001BC150(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001BC150) return l8_latch(o, 0x001BC150u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BC150(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001BC150u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001BDCA0(L8 *o, uint32_t a0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001BDCA0) return l8_latch(o, 0x001BDCA0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BDCA0(o->h->ctx, a0, result) < 0 ? l8_latch(o, 0x001BDCA0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001BDD70(L8 *o, uint32_t a0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001BDD70) return l8_latch(o, 0x001BDD70u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BDD70(o->h->ctx, a0, result) < 0 ? l8_latch(o, 0x001BDD70u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001BF630(L8 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001BF630) return l8_latch(o, 0x001BF630u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BF630(o->h->ctx, a0, a1, a2, result) < 0 ? l8_latch(o, 0x001BF630u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001C1500(L8 *o, uint32_t a0, int32_t n1, float f2, float f3, float f4)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001C1500) return l8_latch(o, 0x001C1500u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C1500(o->h->ctx, a0, n1, f2, f3, f4) < 0 ? l8_latch(o, 0x001C1500u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001C1570(L8 *o, uint32_t a0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001C1570) return l8_latch(o, 0x001C1570u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C1570(o->h->ctx, a0, result) < 0 ? l8_latch(o, 0x001C1570u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001C2770(L8 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001C2770) return l8_latch(o, 0x001C2770u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C2770(o->h->ctx, a0, a1, n2, result) < 0 ? l8_latch(o, 0x001C2770u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001C3D60(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001C3D60) return l8_latch(o, 0x001C3D60u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C3D60(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x001C3D60u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001C6160(L8 *o, uint32_t a0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001C6160) return l8_latch(o, 0x001C6160u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6160(o->h->ctx, a0, result) < 0 ? l8_latch(o, 0x001C6160u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001C62C0(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001C62C0) return l8_latch(o, 0x001C62C0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C62C0(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001C62C0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001C6380(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001C6380) return l8_latch(o, 0x001C6380u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6380(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001C6380u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001C63D0(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001C63D0) return l8_latch(o, 0x001C63D0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C63D0(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001C63D0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001C63E0(L8 *o, uint32_t a0, int32_t n1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001C63E0) return l8_latch(o, 0x001C63E0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C63E0(o->h->ctx, a0, n1) < 0 ? l8_latch(o, 0x001C63E0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001C64F0(L8 *o, uint32_t a0, float f1, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001C64F0) return l8_latch(o, 0x001C64F0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C64F0(o->h->ctx, a0, f1, result) < 0 ? l8_latch(o, 0x001C64F0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001C67E0(L8 *o, uint32_t a0, int32_t n1, float f2, float f3)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001C67E0) return l8_latch(o, 0x001C67E0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C67E0(o->h->ctx, a0, n1, f2, f3) < 0 ? l8_latch(o, 0x001C67E0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001C68C0(L8 *o, uint32_t a0)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001C68C0) return l8_latch(o, 0x001C68C0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C68C0(o->h->ctx, a0) < 0 ? l8_latch(o, 0x001C68C0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001CFB50(L8 *o, uint32_t a0, int32_t n1, int32_t n2, float f3, float f4, float f5, float f6, float f7)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001CFB50) return l8_latch(o, 0x001CFB50u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFB50(o->h->ctx, a0, n1, n2, f3, f4, f5, f6, f7) < 0 ? l8_latch(o, 0x001CFB50u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001CFBE0(L8 *o, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001CFBE0) return l8_latch(o, 0x001CFBE0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFBE0(o->h->ctx, n0, n1, a2, a3, n4) < 0 ? l8_latch(o, 0x001CFBE0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001D04B0(L8 *o, uint32_t a0, int32_t n1, uint32_t a2, float f3, float f4)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001D04B0) return l8_latch(o, 0x001D04B0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D04B0(o->h->ctx, a0, n1, a2, f3, f4) < 0 ? l8_latch(o, 0x001D04B0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001D0C80(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001D0C80) return l8_latch(o, 0x001D0C80u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D0C80(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x001D0C80u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001D0D40(L8 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001D0D40) return l8_latch(o, 0x001D0D40u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D0D40(o->h->ctx, a0, a1, n2, n3) < 0 ? l8_latch(o, 0x001D0D40u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001D0D60(L8 *o, uint32_t a0, float f1, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001D0D60) return l8_latch(o, 0x001D0D60u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D0D60(o->h->ctx, a0, f1, result) < 0 ? l8_latch(o, 0x001D0D60u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001EFD20(L8 *o, int32_t n0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001EFD20) return l8_latch(o, 0x001EFD20u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFD20(o->h->ctx, n0, a1) < 0 ? l8_latch(o, 0x001EFD20u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001EFD90(L8 *o, int32_t n0, uint32_t a1, uint32_t a2)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001EFD90) return l8_latch(o, 0x001EFD90u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFD90(o->h->ctx, n0, a1, a2) < 0 ? l8_latch(o, 0x001EFD90u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001EFE00(L8 *o, int32_t n0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001EFE00) return l8_latch(o, 0x001EFE00u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFE00(o->h->ctx, n0, a1) < 0 ? l8_latch(o, 0x001EFE00u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001EFEB0(L8 *o, int32_t n0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001EFEB0) return l8_latch(o, 0x001EFEB0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFEB0(o->h->ctx, n0, a1) < 0 ? l8_latch(o, 0x001EFEB0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001EFF10(L8 *o, int32_t n0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, float f6, uint32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001EFF10) return l8_latch(o, 0x001EFF10u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFF10(o->h->ctx, n0, a1, a2, a3, a4, a5, f6, result) < 0 ? l8_latch(o, 0x001EFF10u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001EFFD0(L8 *o, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, float f4)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001EFFD0) return l8_latch(o, 0x001EFFD0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFFD0(o->h->ctx, n0, a1, a2, n3, f4) < 0 ? l8_latch(o, 0x001EFFD0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001F4A00(L8 *o, uint32_t a0, uint32_t a1)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001F4A00) return l8_latch(o, 0x001F4A00u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F4A00(o->h->ctx, a0, a1) < 0 ? l8_latch(o, 0x001F4A00u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001F4E20(L8 *o, uint32_t a0, uint32_t a1, float f2)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001F4E20) return l8_latch(o, 0x001F4E20u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F4E20(o->h->ctx, a0, a1, f2) < 0 ? l8_latch(o, 0x001F4E20u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001F8D30(L8 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, float f4, float f5, float f6, uint32_t a7)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001F8D30) return l8_latch(o, 0x001F8D30u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F8D30(o->h->ctx, a0, a1, a2, a3, f4, f5, f6, a7) < 0 ? l8_latch(o, 0x001F8D30u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_001FBD50(L8 *o, uint32_t a0, int32_t n1, int32_t n2, float f3, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_001FBD50) return l8_latch(o, 0x001FBD50u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FBD50(o->h->ctx, a0, n1, n2, f3, result) < 0 ? l8_latch(o, 0x001FBD50u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0021BB00(L8 *o, uint32_t a0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0021BB00) return l8_latch(o, 0x0021BB00u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0021BB00(o->h->ctx, a0, result) < 0 ? l8_latch(o, 0x0021BB00u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l8_c_0021BED0(L8 *o, uint32_t a0, int32_t *result)
{
    if (l8_failed(o)) return -1;
    if (!o->h->w_0021BED0) return l8_latch(o, 0x0021BED0u, EM_LEVEL8_PORT_FAULT_NULL_WORKER);
    return o->h->w_0021BED0(o->h->ctx, a0, result) < 0 ? l8_latch(o, 0x0021BED0u, EM_LEVEL8_PORT_FAULT_WORKER_FAILED) : 0;
}

/* END GENERATED WRAPPERS */

static inline float fl(uint32_t bits) { return l8_float(bits); }

/* 001B1470: the angle into (-pi, pi]. */
static inline int l8_angle(L8 *o, float x, float *result) { return l8_c_001B1470(o, x, result); }

/* An arithmetic right shift of a 32-bit register value (sra). */
static inline int32_t l8_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* Data addresses. */
#define D_00275B40 0x00275B40u /* pointer: the pose / bone block */
#define D_00275610 0x00275610u
#define D_0024DB80 0x0024DB80u /* sound id table (halfwords, 2 per model) */
#define D_0024E3A0 0x0024E3A0u /* script */
#define D_0024E560 0x0024E560u /* script */
#define D_0024E7E0 0x0024E7E0u /* script */
#define D_008101E0 0x008101E0u /* camera block */
#define D_008102B0 0x008102B0u /* player block */
#define D_008105E0 0x008105E0u
#define D_008106B8 0x008106B8u
#define D_00810774 0x00810774u
#define S_70003400 0x70003400u
#define S_70003600 0x70003600u
#define S_700038A0 0x700038A0u
#define S_70003B50 0x70003B50u
#define S_70003B68 0x70003B68u /* the frame counter word */
#define S_700031D0 0x700031D0u /* pointer: the probe's hit polygon */

/* Float constants (bit patterns, as the original materializes them). */
#define F_ZERO    0x00000000u
#define F_ONE     0x3F800000u
#define F_0_01    0x3C23D70Au
#define F_0_2     0x3E4CCCCDu
#define F_0_3     0x3E99999Au
#define F_2       0x40000000u
#define F_5       0x40A00000u
#define F_6       0x40C00000u
#define F_8       0x41000000u
#define F_13_8    0x415CCCCDu
#define F_14      0x41600000u
#define F_16      0x41800000u
#define F_20      0x41A00000u
#define F_M20     0xC1A00000u
#define F_300     0x43960000u
#define F_2P31    0x4F000000u /* 2^31 */
#define F_PI      0x40490FDBu
#define F_1_1693707 0x3F95ADF0u

/* Translations called by other translations (direct calls, no hook). */
int32_t l8_001BBD20(L8 *o, uint32_t self, int32_t index);
int32_t l8_001BC860(L8 *o, uint32_t self, uint32_t blk);
uint32_t l8_001BEAC0(L8 *o, uint32_t self, uint32_t pos, int32_t a2, int32_t a3);
void l8_001BF5B0(L8 *o, uint32_t a0, uint32_t p, int32_t mode);
int32_t l8_001284E0(L8 *o, uint32_t owner, uint32_t pos, int32_t kind, uint32_t dir);
/* creature (em_level8_port_creature.c) */
void l8_0012E3A0(L8 *o, uint32_t self, uint32_t sp);
void l8_0012E560(L8 *o, uint32_t self, uint32_t ent);
void l8_0012E840(L8 *o, uint32_t self, uint32_t ent, uint32_t sp);
void l8_0012EB60(L8 *o, uint32_t self, uint32_t ent, uint32_t sp);
void l8_0012F100(L8 *o, uint32_t self, uint32_t ent, uint32_t sp);
void l8_0012FC10(L8 *o, uint32_t self, uint32_t ent, uint32_t sp);
void l8_00131ED0(L8 *o, uint32_t self);
void l8_00131F90(L8 *o, uint32_t self, uint32_t ent, uint32_t sp);
void l8_00132490(L8 *o, uint32_t self, uint32_t ent, uint32_t sp);
void l8_001328D0(L8 *o, uint32_t self, uint32_t ent, uint32_t sp);
int32_t l8_00132FB0(L8 *o, uint32_t self, uint32_t ent, uint32_t sp);
int32_t l8_001333F0(L8 *o, uint32_t self, uint32_t ent);
int32_t l8_00133640(L8 *o, uint32_t self, uint32_t ent);
int32_t l8_00133A20(L8 *o, uint32_t self, uint32_t ent);
void l8_00133DB0(L8 *o, uint32_t self, uint32_t ent);
uint32_t l8_0011E0A8(L8 *o, uint32_t ptr, uint32_t x);
int32_t l8_0021BE40(L8 *o, uint32_t self);
void l8_001B4810(L8 *o, uint32_t self);
/* probes (em_level8_port_probe.c) */
int32_t l8_001A7B80(L8 *o, uint32_t self, uint32_t sp);
int32_t l8_001A7BA0(L8 *o, uint32_t a, uint32_t b, uint32_t m1, uint32_t m2, uint32_t sp);
int32_t l8_001B1560(L8 *o, uint32_t self, uint32_t pos, uint32_t limit);
void l8_001B2B10(L8 *o, uint32_t self, uint32_t dst, uint32_t src);
int32_t l8_001B2BF0(L8 *o, uint32_t self, uint32_t pos, uint32_t out, uint32_t limit, uint32_t sp);
int32_t l8_001B30E0(L8 *o, uint32_t pos, uint32_t out);
int32_t l8_001B32F0(L8 *o, uint32_t self, uint32_t pos, uint32_t limit, uint32_t sp);
int32_t l8_001B3440(L8 *o, uint32_t self, uint32_t a, uint32_t b, uint32_t limit, uint32_t sp);
uint32_t l8_001B3580(L8 *o, uint32_t self, uint32_t pos);
void l8_001B55E0(L8 *o, uint32_t self, int32_t kind);

#endif /* EM_LEVEL8_PORT_INTERNAL_H */
