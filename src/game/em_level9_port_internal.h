/* Shared plumbing of em_level9_port*.c: the memory view, fault latch, EE
 * float helpers, one typed wrapper per hook (written from one table, the
 * HOOKS list of tools/test_level9_port_reference.py, by a lane script that
 * is not committed; the test's header_checks compares this block and the
 * header's hook struct with HOOKS on every run), and the internal
 * entry points the translations call directly (the design of
 * em_level8_port_internal.h). Not a public interface.
 *
 * Floats inside the module are carried as their bit patterns (uint32_t);
 * the wrappers convert to and from the hook table's float parameters with
 * memcpy, so no value is ever converted or rounded on the way. */
#ifndef EM_LEVEL9_PORT_INTERNAL_H
#define EM_LEVEL9_PORT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level9_port.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel9PortHooks *h;
    EmLevel9PortFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L9;

static inline int l9_latch(L9 *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL9_PORT_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l9_failed(const L9 *o) { return o->fault->code != EM_LEVEL9_PORT_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l9_at(L9 *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l9_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l9_latch(o, address, EM_LEVEL9_PORT_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l9_u8(L9 *o, uint32_t a) { return *l9_at(o, a, 1); }
static inline int32_t l9_s8(L9 *o, uint32_t a) { return (int8_t)*l9_at(o, a, 1); }
static inline uint32_t l9_u16(L9 *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l9_at(o, a, 2), 2);
    return v;
}
static inline int32_t l9_s16(L9 *o, uint32_t a) { return (int16_t)l9_u16(o, a); }
static inline uint32_t l9_u32(L9 *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l9_at(o, a, 4), 4);
    return v;
}
static inline int32_t l9_s32(L9 *o, uint32_t a) { return (int32_t)l9_u32(o, a); }
static inline uint64_t l9_u64(L9 *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, l9_at(o, a, 8), 8);
    return v;
}
/* A quadword load / store (lq / sq) at an address the caller has aligned
 * to 16, as its two doublewords, low then high (the access granularity the
 * test's EE core models and compares). */
static inline void l9_q(L9 *o, uint32_t a, uint8_t out[16])
{
    memcpy(out, l9_at(o, a, 8), 8);
    memcpy(out + 8, l9_at(o, a + 8, 8), 8);
}
static inline void l9_wq(L9 *o, uint32_t a, const uint8_t in[16])
{
    memcpy(l9_at(o, a, 8), in, 8);
    memcpy(l9_at(o, a + 8, 8), in + 8, 8);
}

static inline void l9_w8(L9 *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l9_at(o, a, 1), &b, 1);
}
static inline void l9_w16(L9 *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l9_at(o, a, 2), &h, 2);
}
static inline void l9_w32(L9 *o, uint32_t a, uint32_t v) { memcpy(l9_at(o, a, 4), &v, 4); }
static inline void l9_w64(L9 *o, uint32_t a, uint64_t v) { memcpy(l9_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l9_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l9_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l9_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* The EE's div: quotient and remainder of a signed division (LO / HI).
 * The originals here only divide by non-zero constants; INT32_MIN / -1 is
 * not reached. */
static inline int32_t l9_div(int32_t a, int32_t b) { return b ? (int32_t)((int64_t)a / b) : 0; }
static inline int32_t l9_rem(int32_t a, int32_t b) { return b ? (int32_t)((int64_t)a % b) : 0; }

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L9_ADD(a, b) em_ee_add_bits((a), (b))
#define L9_SUB(a, b) em_ee_sub_bits((a), (b))
#define L9_MUL(a, b) em_ee_mul_bits((a), (b))
#define L9_DIV(a, b) em_ee_div_bits((a), (b))
#define L9_MULA(a, b) em_ee_mula_bits((a), (b))
#define L9_ADDA(a, b) em_ee_adda_bits((a), (b))
#define L9_MADD(acc, a, b) em_ee_madd_bits((acc), (a), (b))
#define L9_MSUB(acc, a, b) em_ee_msub_bits((acc), (a), (b))
#define L9_CVT(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define L9_CVTW(f) em_ee_cvt_w_s_bits((f))
#define L9_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L9_LE(a, b) em_ee_c_le_bits((a), (b))
#define L9_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L9_NEG(a) em_ee_neg_bits((a))
#define L9_ABS(a) ((a) & 0x7FFFFFFFu)

static inline void l9_open(L9 *o, const EmLevel9PortHooks *h, EmLevel9PortFault *fault)
{
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
}

static inline int l9_begin(L9 *o, const EmLevel9PortHooks *h, EmLevel9PortFault *fault)
{
    if (!h || !fault) return -1;
    l9_open(o, h, fault);
    return l9_failed(o) ? -1 : 0;
}

static inline int l9_end(const L9 *o) { return l9_failed(o) ? -1 : 0; }

/* An indirect call through a function word the caller has read (the
 * actor's +0x4C method, a step table entry): w_callback(ctx, function, a0). */
static inline int l9_callback(L9 *o, uint32_t fn, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_callback) return l9_latch(o, fn, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? l9_latch(o, fn, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method (its word read here, then the call). */
static inline int l9_method(L9 *o, uint32_t self)
{
    return l9_callback(o, l9_u32(o, self + 0x4C), self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Float arguments and results are bit patterns. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l9_c_001026A0(L9 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001026A0) return l9_latch(o, 0x001026A0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0 ? l9_latch(o, 0x001026A0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00102738(L9 *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00102738) return l9_latch(o, 0x00102738u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_00102738(o->h->ctx, a0, a1, &r) < 0) return l9_latch(o, 0x00102738u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED);
    *result = l9_bits(r);
    return 0;
}

static inline int l9_c_00102760(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00102760) return l9_latch(o, 0x00102760u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102760(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x00102760u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00102798(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00102798) return l9_latch(o, 0x00102798u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102798(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x00102798u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001028B8(L9 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001028B8) return l9_latch(o, 0x001028B8u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0 ? l9_latch(o, 0x001028B8u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001028D0(L9 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001028D0) return l9_latch(o, 0x001028D0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0 ? l9_latch(o, 0x001028D0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00102900(L9 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00102900) return l9_latch(o, 0x00102900u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102900(o->h->ctx, a0, a1, l9_float(f2)) < 0 ? l9_latch(o, 0x00102900u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00102918(L9 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00102918) return l9_latch(o, 0x00102918u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102918(o->h->ctx, a0, a1, a2) < 0 ? l9_latch(o, 0x00102918u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00102948(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00102948) return l9_latch(o, 0x00102948u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x00102948u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00102958(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00102958) return l9_latch(o, 0x00102958u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102958(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x00102958u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001029C0(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001029C0) return l9_latch(o, 0x001029C0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001029C0(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001029C0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00102A60(L9 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00102A60) return l9_latch(o, 0x00102A60u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102A60(o->h->ctx, a0, a1, l9_float(f2)) < 0 ? l9_latch(o, 0x00102A60u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00102B08(L9 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00102B08) return l9_latch(o, 0x00102B08u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102B08(o->h->ctx, a0, a1, l9_float(f2)) < 0 ? l9_latch(o, 0x00102B08u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00102BB0(L9 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00102BB0) return l9_latch(o, 0x00102BB0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102BB0(o->h->ctx, a0, a1, l9_float(f2)) < 0 ? l9_latch(o, 0x00102BB0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001031E0(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001031E0) return l9_latch(o, 0x001031E0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001031E0(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x001031E0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00103230(L9 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00103230) return l9_latch(o, 0x00103230u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00103230(o->h->ctx, a0, a1, l9_float(f2)) < 0 ? l9_latch(o, 0x00103230u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0011DBB8(L9 *o, uint32_t f0, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0011DBB8) return l9_latch(o, 0x0011DBB8u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_0011DBB8(o->h->ctx, l9_float(f0), &r) < 0) return l9_latch(o, 0x0011DBB8u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED);
    *result = l9_bits(r);
    return 0;
}

static inline int l9_c_0011DE90(L9 *o, uint32_t f0, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0011DE90) return l9_latch(o, 0x0011DE90u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_0011DE90(o->h->ctx, l9_float(f0), &r) < 0) return l9_latch(o, 0x0011DE90u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED);
    *result = l9_bits(r);
    return 0;
}

static inline int l9_c_0011DF78(L9 *o, uint32_t f0, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0011DF78) return l9_latch(o, 0x0011DF78u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_0011DF78(o->h->ctx, l9_float(f0), &r) < 0) return l9_latch(o, 0x0011DF78u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED);
    *result = l9_bits(r);
    return 0;
}

static inline int l9_c_0011E2A8(L9 *o, uint32_t f0, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return l9_latch(o, 0x0011E2A8u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_0011E2A8(o->h->ctx, l9_float(f0), &r) < 0) return l9_latch(o, 0x0011E2A8u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED);
    *result = l9_bits(r);
    return 0;
}

static inline int l9_c_0011E520(L9 *o, uint32_t f0, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0011E520) return l9_latch(o, 0x0011E520u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_0011E520(o->h->ctx, l9_float(f0), &r) < 0) return l9_latch(o, 0x0011E520u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED);
    *result = l9_bits(r);
    return 0;
}

static inline int l9_c_0011E748(L9 *o, uint32_t f0, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0011E748) return l9_latch(o, 0x0011E748u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_0011E748(o->h->ctx, l9_float(f0), &r) < 0) return l9_latch(o, 0x0011E748u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED);
    *result = l9_bits(r);
    return 0;
}

static inline int l9_c_00122BB8(L9 *o, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00122BB8) return l9_latch(o, 0x00122BB8u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? l9_latch(o, 0x00122BB8u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001281C0(L9 *o, uint32_t f0, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001281C0) return l9_latch(o, 0x001281C0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001281C0(o->h->ctx, l9_float(f0), result) < 0 ? l9_latch(o, 0x001281C0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00128250(L9 *o, uint32_t f0, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00128250) return l9_latch(o, 0x00128250u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00128250(o->h->ctx, l9_float(f0), result) < 0 ? l9_latch(o, 0x00128250u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00131ED0(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00131ED0) return l9_latch(o, 0x00131ED0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00131ED0(o->h->ctx, a0) < 0 ? l9_latch(o, 0x00131ED0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00138900(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00138900) return l9_latch(o, 0x00138900u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00138900(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x00138900u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00138C20(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00138C20) return l9_latch(o, 0x00138C20u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00138C20(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x00138C20u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00139240(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00139240) return l9_latch(o, 0x00139240u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00139240(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x00139240u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001399F0(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001399F0) return l9_latch(o, 0x001399F0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001399F0(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x001399F0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00139E00(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00139E00) return l9_latch(o, 0x00139E00u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00139E00(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x00139E00u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0013A3B0(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0013A3B0) return l9_latch(o, 0x0013A3B0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0013A3B0(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x0013A3B0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0013B350(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0013B350) return l9_latch(o, 0x0013B350u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0013B350(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x0013B350u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0013B9A0(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0013B9A0) return l9_latch(o, 0x0013B9A0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0013B9A0(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x0013B9A0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0013BE60(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0013BE60) return l9_latch(o, 0x0013BE60u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0013BE60(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x0013BE60u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0013BF20(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0013BF20) return l9_latch(o, 0x0013BF20u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0013BF20(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x0013BF20u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001545B0(L9 *o, uint32_t a0, uint32_t f1, uint32_t f2, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001545B0) return l9_latch(o, 0x001545B0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001545B0(o->h->ctx, a0, l9_float(f1), l9_float(f2), result) < 0 ? l9_latch(o, 0x001545B0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00154F00(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00154F00) return l9_latch(o, 0x00154F00u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00154F00(o->h->ctx, a0) < 0 ? l9_latch(o, 0x00154F00u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0015AC00(L9 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0015AC00) return l9_latch(o, 0x0015AC00u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0015AC00(o->h->ctx, a0, a1, result) < 0 ? l9_latch(o, 0x0015AC00u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0015AE20(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0015AE20) return l9_latch(o, 0x0015AE20u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0015AE20(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x0015AE20u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001662D0(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001662D0) return l9_latch(o, 0x001662D0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001662D0(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001662D0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00174A50(L9 *o, uint32_t a0, uint32_t f1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00174A50) return l9_latch(o, 0x00174A50u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00174A50(o->h->ctx, a0, l9_float(f1)) < 0 ? l9_latch(o, 0x00174A50u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0018C0C0(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0018C0C0) return l9_latch(o, 0x0018C0C0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018C0C0(o->h->ctx, a0) < 0 ? l9_latch(o, 0x0018C0C0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0018C4B0(L9 *o, uint32_t a0, uint32_t f1, uint32_t f2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0018C4B0) return l9_latch(o, 0x0018C4B0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018C4B0(o->h->ctx, a0, l9_float(f1), l9_float(f2)) < 0 ? l9_latch(o, 0x0018C4B0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0018C6A0(L9 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0018C6A0) return l9_latch(o, 0x0018C6A0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018C6A0(o->h->ctx, a0, a1, l9_float(f2)) < 0 ? l9_latch(o, 0x0018C6A0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0018D7B0(L9 *o, uint32_t a0, int32_t n1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0018D7B0) return l9_latch(o, 0x0018D7B0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018D7B0(o->h->ctx, a0, n1) < 0 ? l9_latch(o, 0x0018D7B0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001916C0(L9 *o, uint32_t a0, uint32_t a1, int32_t n2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001916C0) return l9_latch(o, 0x001916C0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001916C0(o->h->ctx, a0, a1, n2) < 0 ? l9_latch(o, 0x001916C0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00192010(L9 *o, uint32_t a0, uint32_t f1, uint32_t f2, uint32_t f3)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00192010) return l9_latch(o, 0x00192010u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00192010(o->h->ctx, a0, l9_float(f1), l9_float(f2), l9_float(f3)) < 0 ? l9_latch(o, 0x00192010u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0019A570(L9 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0019A570) return l9_latch(o, 0x0019A570u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019A570(o->h->ctx, a0, a1, n2, n3, result) < 0 ? l9_latch(o, 0x0019A570u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0019AA80(L9 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0019AA80) return l9_latch(o, 0x0019AA80u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019AA80(o->h->ctx, a0, a1, n2, result) < 0 ? l9_latch(o, 0x0019AA80u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0019B6C0(L9 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0019B6C0) return l9_latch(o, 0x0019B6C0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019B6C0(o->h->ctx, a0, a1, a2, result) < 0 ? l9_latch(o, 0x0019B6C0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0019C6F0(L9 *o, int32_t n0, int32_t n1, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0019C6F0) return l9_latch(o, 0x0019C6F0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019C6F0(o->h->ctx, n0, n1, result) < 0 ? l9_latch(o, 0x0019C6F0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001A2370(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001A2370) return l9_latch(o, 0x001A2370u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001A2370(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x001A2370u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001AEE10(L9 *o, int32_t n0, int32_t n1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001AEE10) return l9_latch(o, 0x001AEE10u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AEE10(o->h->ctx, n0, n1) < 0 ? l9_latch(o, 0x001AEE10u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001AF780(L9 *o, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001AF780) return l9_latch(o, 0x001AF780u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AF780(o->h->ctx, result) < 0 ? l9_latch(o, 0x001AF780u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001AF800(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001AF800) return l9_latch(o, 0x001AF800u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AF800(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001AF800u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001AFA90(L9 *o, int32_t n0, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001AFA90) return l9_latch(o, 0x001AFA90u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFA90(o->h->ctx, n0, result) < 0 ? l9_latch(o, 0x001AFA90u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001AFC10(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001AFC10) return l9_latch(o, 0x001AFC10u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001AFC10u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B0C60(L9 *o, int32_t n0, int32_t n1, int32_t n2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B0C60) return l9_latch(o, 0x001B0C60u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0C60(o->h->ctx, n0, n1, n2) < 0 ? l9_latch(o, 0x001B0C60u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B0D80(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B0D80) return l9_latch(o, 0x001B0D80u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0D80(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001B0D80u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B0F60(L9 *o, uint32_t a0, int32_t n1, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B0F60) return l9_latch(o, 0x001B0F60u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0F60(o->h->ctx, a0, n1, result) < 0 ? l9_latch(o, 0x001B0F60u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B0FD0(L9 *o, uint32_t a0, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return l9_latch(o, 0x001B0FD0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0FD0(o->h->ctx, a0, result) < 0 ? l9_latch(o, 0x001B0FD0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B1020(L9 *o, uint32_t a0, int32_t n1, int32_t n2, int32_t n3)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B1020) return l9_latch(o, 0x001B1020u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1020(o->h->ctx, a0, n1, n2, n3) < 0 ? l9_latch(o, 0x001B1020u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B10B0(L9 *o, uint32_t a0, int32_t n1, int32_t n2, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B10B0) return l9_latch(o, 0x001B10B0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B10B0(o->h->ctx, a0, n1, n2, result) < 0 ? l9_latch(o, 0x001B10B0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B1190(L9 *o, int32_t n0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B1190) return l9_latch(o, 0x001B1190u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1190(o->h->ctx, n0) < 0 ? l9_latch(o, 0x001B1190u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B11E0(L9 *o, int32_t n0, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B11E0) return l9_latch(o, 0x001B11E0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B11E0(o->h->ctx, n0, result) < 0 ? l9_latch(o, 0x001B11E0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B1240(L9 *o, uint32_t a0, uint32_t f1, uint32_t f2, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B1240) return l9_latch(o, 0x001B1240u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_001B1240(o->h->ctx, a0, l9_float(f1), l9_float(f2), &r) < 0) return l9_latch(o, 0x001B1240u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED);
    *result = l9_bits(r);
    return 0;
}

static inline int l9_c_001B12B0(L9 *o, uint32_t f0, uint32_t f1, uint32_t f2, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B12B0) return l9_latch(o, 0x001B12B0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_001B12B0(o->h->ctx, l9_float(f0), l9_float(f1), l9_float(f2), &r) < 0) return l9_latch(o, 0x001B12B0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED);
    *result = l9_bits(r);
    return 0;
}

static inline int l9_c_001B1470(L9 *o, uint32_t f0, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B1470) return l9_latch(o, 0x001B1470u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_001B1470(o->h->ctx, l9_float(f0), &r) < 0) return l9_latch(o, 0x001B1470u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED);
    *result = l9_bits(r);
    return 0;
}

static inline int l9_c_001B1630(L9 *o, uint32_t f0, uint32_t f1, uint32_t f2, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B1630) return l9_latch(o, 0x001B1630u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1630(o->h->ctx, l9_float(f0), l9_float(f1), l9_float(f2), result) < 0 ? l9_latch(o, 0x001B1630u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B17A0(L9 *o, uint32_t a0, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B17A0) return l9_latch(o, 0x001B17A0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B17A0(o->h->ctx, a0, result) < 0 ? l9_latch(o, 0x001B17A0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B1B70(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B1B70) return l9_latch(o, 0x001B1B70u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1B70(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001B1B70u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B1EA0(L9 *o, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return l9_latch(o, 0x001B1EA0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1EA0(o->h->ctx, n0, a1, a2, n3, result) < 0 ? l9_latch(o, 0x001B1EA0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B2140(L9 *o, uint32_t a0, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B2140) return l9_latch(o, 0x001B2140u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2140(o->h->ctx, a0, result) < 0 ? l9_latch(o, 0x001B2140u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B4810(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B4810) return l9_latch(o, 0x001B4810u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B4810(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001B4810u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001B5360(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001B5360) return l9_latch(o, 0x001B5360u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B5360(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001B5360u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001BA1A0(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return l9_latch(o, 0x001BA1A0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x001BA1A0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001BA1C0(L9 *o, uint32_t a0, int32_t n1, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return l9_latch(o, 0x001BA1C0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1C0(o->h->ctx, a0, n1, result) < 0 ? l9_latch(o, 0x001BA1C0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001BA1F0(L9 *o, uint32_t a0, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return l9_latch(o, 0x001BA1F0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, a0, result) < 0 ? l9_latch(o, 0x001BA1F0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001BA540(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001BA540) return l9_latch(o, 0x001BA540u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA540(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001BA540u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001BA580(L9 *o, uint32_t a0, int32_t n1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001BA580) return l9_latch(o, 0x001BA580u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA580(o->h->ctx, a0, n1) < 0 ? l9_latch(o, 0x001BA580u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001BA8E0(L9 *o, uint32_t a0, int32_t n1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001BA8E0) return l9_latch(o, 0x001BA8E0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA8E0(o->h->ctx, a0, n1) < 0 ? l9_latch(o, 0x001BA8E0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001BE5F0(L9 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001BE5F0) return l9_latch(o, 0x001BE5F0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BE5F0(o->h->ctx, a0, a1, a2, result) < 0 ? l9_latch(o, 0x001BE5F0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001C47A0(L9 *o, int32_t n0, int32_t n1, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001C47A0) return l9_latch(o, 0x001C47A0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C47A0(o->h->ctx, n0, n1, result) < 0 ? l9_latch(o, 0x001C47A0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001C6120(L9 *o, uint32_t a0, int32_t n1, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001C6120) return l9_latch(o, 0x001C6120u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6120(o->h->ctx, a0, n1, result) < 0 ? l9_latch(o, 0x001C6120u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001C6150(L9 *o, uint32_t a0, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001C6150) return l9_latch(o, 0x001C6150u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6150(o->h->ctx, a0, result) < 0 ? l9_latch(o, 0x001C6150u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001C6160(L9 *o, uint32_t a0, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001C6160) return l9_latch(o, 0x001C6160u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6160(o->h->ctx, a0, result) < 0 ? l9_latch(o, 0x001C6160u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001C62C0(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001C62C0) return l9_latch(o, 0x001C62C0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C62C0(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001C62C0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001C6380(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001C6380) return l9_latch(o, 0x001C6380u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6380(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001C6380u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001C63E0(L9 *o, uint32_t a0, int32_t n1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001C63E0) return l9_latch(o, 0x001C63E0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C63E0(o->h->ctx, a0, n1) < 0 ? l9_latch(o, 0x001C63E0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001C64F0(L9 *o, uint32_t a0, uint32_t f1, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001C64F0) return l9_latch(o, 0x001C64F0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C64F0(o->h->ctx, a0, l9_float(f1), result) < 0 ? l9_latch(o, 0x001C64F0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001C67E0(L9 *o, uint32_t a0, int32_t n1, uint32_t f2, uint32_t f3)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001C67E0) return l9_latch(o, 0x001C67E0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C67E0(o->h->ctx, a0, n1, l9_float(f2), l9_float(f3)) < 0 ? l9_latch(o, 0x001C67E0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001C68C0(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001C68C0) return l9_latch(o, 0x001C68C0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C68C0(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001C68C0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001C6910(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001C6910) return l9_latch(o, 0x001C6910u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6910(o->h->ctx, a0) < 0 ? l9_latch(o, 0x001C6910u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001C7900(L9 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001C7900) return l9_latch(o, 0x001C7900u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C7900(o->h->ctx, a0, a1, n2, n3) < 0 ? l9_latch(o, 0x001C7900u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CA5E0(L9 *o, uint32_t a0, uint32_t a1, int32_t n2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CA5E0) return l9_latch(o, 0x001CA5E0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA5E0(o->h->ctx, a0, a1, n2) < 0 ? l9_latch(o, 0x001CA5E0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CA6E0(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CA6E0) return l9_latch(o, 0x001CA6E0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA6E0(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x001CA6E0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CA6F0(L9 *o, uint32_t a0, int32_t n1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CA6F0) return l9_latch(o, 0x001CA6F0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA6F0(o->h->ctx, a0, n1) < 0 ? l9_latch(o, 0x001CA6F0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CA7B0(L9 *o, uint32_t a0, uint32_t f1, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CA7B0) return l9_latch(o, 0x001CA7B0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA7B0(o->h->ctx, a0, l9_float(f1), result) < 0 ? l9_latch(o, 0x001CA7B0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CA940(L9 *o, int32_t n0, int32_t n1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CA940) return l9_latch(o, 0x001CA940u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA940(o->h->ctx, n0, n1) < 0 ? l9_latch(o, 0x001CA940u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CB5B0(L9 *o, int32_t n0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CB5B0) return l9_latch(o, 0x001CB5B0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CB5B0(o->h->ctx, n0) < 0 ? l9_latch(o, 0x001CB5B0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CB5F0(L9 *o, uint32_t a0, uint32_t a1, int32_t n2, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CB5F0) return l9_latch(o, 0x001CB5F0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CB5F0(o->h->ctx, a0, a1, n2, result) < 0 ? l9_latch(o, 0x001CB5F0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CB760(L9 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CB760) return l9_latch(o, 0x001CB760u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CB760(o->h->ctx, a0, a1, a2, n3) < 0 ? l9_latch(o, 0x001CB760u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CD070(L9 *o, uint32_t a0, int32_t n1, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CD070) return l9_latch(o, 0x001CD070u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CD070(o->h->ctx, a0, n1, result) < 0 ? l9_latch(o, 0x001CD070u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CD2B0(L9 *o, uint32_t f0, uint32_t f1, uint32_t f2, uint32_t f3, uint32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CD2B0) return l9_latch(o, 0x001CD2B0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_001CD2B0(o->h->ctx, l9_float(f0), l9_float(f1), l9_float(f2), l9_float(f3), &r) < 0) return l9_latch(o, 0x001CD2B0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED);
    *result = l9_bits(r);
    return 0;
}

static inline int l9_c_001CD520(L9 *o, int32_t n0, int32_t n1, uint32_t a2, uint64_t q3, uint32_t a4, uint32_t f5, uint32_t f6, uint32_t f7, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CD520) return l9_latch(o, 0x001CD520u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CD520(o->h->ctx, n0, n1, a2, q3, a4, l9_float(f5), l9_float(f6), l9_float(f7), result) < 0 ? l9_latch(o, 0x001CD520u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CFAE0(L9 *o, uint32_t a0, int32_t n1, uint32_t a2, uint32_t f3, uint32_t f4, uint32_t f5, uint32_t f6)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CFAE0) return l9_latch(o, 0x001CFAE0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFAE0(o->h->ctx, a0, n1, a2, l9_float(f3), l9_float(f4), l9_float(f5), l9_float(f6)) < 0 ? l9_latch(o, 0x001CFAE0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CFBE0(L9 *o, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CFBE0) return l9_latch(o, 0x001CFBE0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFBE0(o->h->ctx, n0, n1, a2, a3, n4) < 0 ? l9_latch(o, 0x001CFBE0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001CFFE0(L9 *o, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001CFFE0) return l9_latch(o, 0x001CFFE0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFFE0(o->h->ctx, n0, n1, a2, a3, n4) < 0 ? l9_latch(o, 0x001CFFE0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001D1F20(L9 *o, int32_t n0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001D1F20) return l9_latch(o, 0x001D1F20u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D1F20(o->h->ctx, n0) < 0 ? l9_latch(o, 0x001D1F20u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001D1FF0(L9 *o, int32_t n0, int32_t n1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001D1FF0) return l9_latch(o, 0x001D1FF0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D1FF0(o->h->ctx, n0, n1) < 0 ? l9_latch(o, 0x001D1FF0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001D2040(L9 *o, int32_t n0, int32_t n1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001D2040) return l9_latch(o, 0x001D2040u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D2040(o->h->ctx, n0, n1) < 0 ? l9_latch(o, 0x001D2040u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001D6B10(L9 *o, int32_t n0, int32_t n1, int32_t n2, int32_t n3)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001D6B10) return l9_latch(o, 0x001D6B10u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D6B10(o->h->ctx, n0, n1, n2, n3) < 0 ? l9_latch(o, 0x001D6B10u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001D6BA0(L9 *o, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, int32_t n5)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001D6BA0) return l9_latch(o, 0x001D6BA0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D6BA0(o->h->ctx, n0, n1, n2, n3, n4, n5) < 0 ? l9_latch(o, 0x001D6BA0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001D6C90(L9 *o, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, int32_t n5, int32_t n6, int32_t n7)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001D6C90) return l9_latch(o, 0x001D6C90u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D6C90(o->h->ctx, n0, n1, n2, n3, n4, n5, n6, n7) < 0 ? l9_latch(o, 0x001D6C90u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001D8BF0(L9 *o, uint32_t a0, int32_t n1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001D8BF0) return l9_latch(o, 0x001D8BF0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D8BF0(o->h->ctx, a0, n1) < 0 ? l9_latch(o, 0x001D8BF0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001E2BA0(L9 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f3)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001E2BA0) return l9_latch(o, 0x001E2BA0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001E2BA0(o->h->ctx, a0, a1, a2, l9_float(f3)) < 0 ? l9_latch(o, 0x001E2BA0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001E6F60(L9 *o, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, int32_t n5, int32_t n6)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001E6F60) return l9_latch(o, 0x001E6F60u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001E6F60(o->h->ctx, n0, n1, n2, n3, n4, n5, n6) < 0 ? l9_latch(o, 0x001E6F60u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001EFD20(L9 *o, int32_t n0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001EFD20) return l9_latch(o, 0x001EFD20u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFD20(o->h->ctx, n0, a1) < 0 ? l9_latch(o, 0x001EFD20u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001EFD90(L9 *o, int32_t n0, uint32_t a1, uint32_t a2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001EFD90) return l9_latch(o, 0x001EFD90u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFD90(o->h->ctx, n0, a1, a2) < 0 ? l9_latch(o, 0x001EFD90u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001EFE00(L9 *o, int32_t n0, uint32_t a1, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001EFE00) return l9_latch(o, 0x001EFE00u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFE00(o->h->ctx, n0, a1, result) < 0 ? l9_latch(o, 0x001EFE00u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001EFFD0(L9 *o, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, uint32_t f4)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001EFFD0) return l9_latch(o, 0x001EFFD0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFFD0(o->h->ctx, n0, a1, a2, n3, l9_float(f4)) < 0 ? l9_latch(o, 0x001EFFD0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001F02C0(L9 *o, uint32_t a0, int32_t n1, uint32_t f2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001F02C0) return l9_latch(o, 0x001F02C0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F02C0(o->h->ctx, a0, n1, l9_float(f2)) < 0 ? l9_latch(o, 0x001F02C0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001F4A00(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001F4A00) return l9_latch(o, 0x001F4A00u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F4A00(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x001F4A00u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001F4BF0(L9 *o, uint32_t a0, uint32_t a1)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001F4BF0) return l9_latch(o, 0x001F4BF0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F4BF0(o->h->ctx, a0, a1) < 0 ? l9_latch(o, 0x001F4BF0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001F4E20(L9 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001F4E20) return l9_latch(o, 0x001F4E20u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F4E20(o->h->ctx, a0, a1, l9_float(f2)) < 0 ? l9_latch(o, 0x001F4E20u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001F5940(L9 *o, int32_t n0, uint32_t a1, int32_t n2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001F5940) return l9_latch(o, 0x001F5940u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F5940(o->h->ctx, n0, a1, n2) < 0 ? l9_latch(o, 0x001F5940u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001F9100(L9 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f3)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001F9100) return l9_latch(o, 0x001F9100u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F9100(o->h->ctx, a0, a1, a2, l9_float(f3)) < 0 ? l9_latch(o, 0x001F9100u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001FB9F0(L9 *o, int32_t n0, int32_t n1, int32_t n2, int32_t n3)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001FB9F0) return l9_latch(o, 0x001FB9F0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FB9F0(o->h->ctx, n0, n1, n2, n3) < 0 ? l9_latch(o, 0x001FB9F0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_001FBD50(L9 *o, uint32_t a0, int32_t n1, int32_t n2, uint32_t f3, int32_t *result)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_001FBD50) return l9_latch(o, 0x001FBD50u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FBD50(o->h->ctx, a0, n1, n2, l9_float(f3), result) < 0 ? l9_latch(o, 0x001FBD50u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_0021B9A0(L9 *o, int32_t n0, uint32_t f1, uint32_t f2)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_0021B9A0) return l9_latch(o, 0x0021B9A0u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_0021B9A0(o->h->ctx, n0, l9_float(f1), l9_float(f2)) < 0 ? l9_latch(o, 0x0021B9A0u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00823830(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00823830) return l9_latch(o, 0x00823830u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00823830(o->h->ctx, a0) < 0 ? l9_latch(o, 0x00823830u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00824160(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00824160) return l9_latch(o, 0x00824160u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00824160(o->h->ctx, a0) < 0 ? l9_latch(o, 0x00824160u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00824390(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00824390) return l9_latch(o, 0x00824390u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00824390(o->h->ctx, a0) < 0 ? l9_latch(o, 0x00824390u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00824520(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00824520) return l9_latch(o, 0x00824520u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00824520(o->h->ctx, a0) < 0 ? l9_latch(o, 0x00824520u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00824960(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00824960) return l9_latch(o, 0x00824960u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00824960(o->h->ctx, a0) < 0 ? l9_latch(o, 0x00824960u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l9_c_00826610(L9 *o, uint32_t a0)
{
    if (l9_failed(o)) return -1;
    if (!o->h->w_00826610) return l9_latch(o, 0x00826610u, EM_LEVEL9_PORT_FAULT_NULL_WORKER);
    return o->h->w_00826610(o->h->ctx, a0) < 0 ? l9_latch(o, 0x00826610u, EM_LEVEL9_PORT_FAULT_WORKER_FAILED) : 0;
}
/* END GENERATED WRAPPERS */

/* Data addresses. */
#define D_00275B40 0x00275B40u /* pointer: the pose / bone block */
#define D_008102B0 0x008102B0u /* player block */
#define D_00810350 0x00810350u /* the player's position */
#define D_00810360 0x00810360u
#define D_00810700 0x00810700u
#define D_00810701 0x00810701u
#define D_00810702 0x00810702u
#define D_008106B8 0x008106B8u
#define D_00810774 0x00810774u
#define D_008107F4 0x008107F4u
#define D_00810839 0x00810839u
#define D_00810C8B 0x00810C8Bu
#define S_70003190 0x70003190u
#define S_700031A0 0x700031A0u
#define S_700031B0 0x700031B0u
#define S_700031B4 0x700031B4u
#define S_700031B8 0x700031B8u
#define S_700031D0 0x700031D0u
#define S_700031D4 0x700031D4u
#define S_700031D8 0x700031D8u
#define S_70003400 0x70003400u
#define S_70003430 0x70003430u
#define S_70003600 0x70003600u
#define S_70003604 0x70003604u
#define S_70003608 0x70003608u
#define S_7000360C 0x7000360Cu
#define S_70003610 0x70003610u
#define S_70003618 0x70003618u
#define S_70003680 0x70003680u
#define S_70003684 0x70003684u
#define S_700036A0 0x700036A0u
#define S_700036D0 0x700036D0u
#define S_700036DC 0x700036DCu
#define S_700036E0 0x700036E0u
#define S_70003710 0x70003710u
#define S_700038A0 0x700038A0u
#define S_700038A4 0x700038A4u
#define S_700038A8 0x700038A8u
#define S_700038AC 0x700038ACu
#define S_700038B0 0x700038B0u
#define S_700038B4 0x700038B4u
#define S_700038B8 0x700038B8u
#define S_700038BC 0x700038BCu
#define S_700038C0 0x700038C0u
#define S_700038C4 0x700038C4u
#define S_700038C8 0x700038C8u
#define S_700038CC 0x700038CCu
#define S_700038D0 0x700038D0u
#define S_70003910 0x70003910u
#define S_7000391C 0x7000391Cu
#define S_70003A20 0x70003A20u
#define S_70003B68 0x70003B68u /* the frame counter word */
#define S_70003B8A 0x70003B8Au
#define S_70003B8D 0x70003B8Du /* the pause / cinematic byte */

/* Float constants (bit patterns, as the originals materialize them). */
#define F_ZERO    0x00000000u
#define F_ONE     0x3F800000u
#define F_HALF    0x3F000000u
#define F_2       0x40000000u
#define F_3       0x40400000u
#define F_4       0x40800000u
#define F_5       0x40A00000u
#define F_8       0x41000000u
#define F_10      0x41200000u
#define F_16      0x41800000u
#define F_20      0x41A00000u
#define F_60      0x42700000u
#define F_90      0x42B40000u
#define F_100     0x42C80000u
#define F_128     0x43000000u
#define F_300     0x43960000u
#define F_PI      0x40490FDBu
#define F_MPI     0xC0490FDBu
#define F_2P31    0x4F000000u /* 2^31 */
#define F_2PM31   0x30000000u /* 2^-31 */

/* Translations called by other translations (direct calls, no hook). */
/* AREA13 overlay (em_level9_port_area13.c, em_level9_port_turret.c) */
void l9_00823940(L9 *o, uint32_t self);
void l9_00823C10(L9 *o, uint32_t self);
void l9_00823D50(L9 *o, uint32_t self);
void l9_00824180(L9 *o, uint32_t self);
int32_t l9_00826140(L9 *o, uint32_t self, uint32_t matrix, uint32_t sp);
/* the AREA19 load (em_level9_port_exit.c) */
void l9_00138540(L9 *o, uint32_t p, uint32_t q);
void l9_001386E0(L9 *o, uint32_t self, uint32_t ent);
int32_t l9_00154460(L9 *o, uint32_t self);
void l9_00154740(L9 *o, uint32_t self, uint32_t scr);
void l9_001549C0(L9 *o, uint32_t self, uint32_t scr);
void l9_00196970(L9 *o, uint32_t p, uint32_t q);

#endif /* EM_LEVEL9_PORT_INTERNAL_H */
