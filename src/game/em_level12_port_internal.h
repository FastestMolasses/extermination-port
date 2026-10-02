/* Shared plumbing of em_level12_port*.c: the memory view, fault latch, EE
 * float helpers, one typed wrapper per hook (written from one table, the
 * HOOKS list of tools/test_level12_port_reference.py, by a lane script that
 * is not committed; the test's header_checks compares this block and the
 * header's hook struct with HOOKS on every run), and the internal entry
 * points the translations call directly (the design of
 * em_level11_port_internal.h). Not a public interface.
 *
 * Floats inside the module are carried as their bit patterns (uint32_t);
 * the wrappers convert to and from the hook table's float parameters with
 * memcpy, so no value is ever converted or rounded on the way. */
#ifndef EM_LEVEL12_PORT_INTERNAL_H
#define EM_LEVEL12_PORT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level12_port.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel12PortHooks *h;
    EmLevel12PortFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L12;

static inline int l12_latch(L12 *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL12_PORT_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l12_failed(const L12 *o) { return o->fault->code != EM_LEVEL12_PORT_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l12_at(L12 *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l12_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l12_latch(o, address, EM_LEVEL12_PORT_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l12_u8(L12 *o, uint32_t a) { return *l12_at(o, a, 1); }
static inline uint32_t l12_u16(L12 *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l12_at(o, a, 2), 2);
    return v;
}
static inline int32_t l12_s16(L12 *o, uint32_t a) { return (int16_t)l12_u16(o, a); }
static inline uint32_t l12_u32(L12 *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l12_at(o, a, 4), 4);
    return v;
}
static inline int32_t l12_s32(L12 *o, uint32_t a) { return (int32_t)l12_u32(o, a); }
static inline int32_t l12_s8(L12 *o, uint32_t a) { return (int8_t)l12_u8(o, a); }
static inline uint64_t l12_u64(L12 *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, l12_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void l12_q(L12 *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, l12_at(o, a, 8), 8);
    memcpy(out + 8, l12_at(o, a + 8, 8), 8);
}
static inline void l12_wq(L12 *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(l12_at(o, a, 8), in, 8);
    memcpy(l12_at(o, a + 8, 8), in + 8, 8);
}

static inline void l12_w8(L12 *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l12_at(o, a, 1), &b, 1);
}
static inline void l12_w16(L12 *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l12_at(o, a, 2), &h, 2);
}
static inline void l12_w32(L12 *o, uint32_t a, uint32_t v) { memcpy(l12_at(o, a, 4), &v, 4); }
static inline void l12_w64(L12 *o, uint32_t a, uint64_t v) { memcpy(l12_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l12_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l12_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l12_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L12_ADD(a, b) em_ee_add_bits((a), (b))
#define L12_SUB(a, b) em_ee_sub_bits((a), (b))
#define L12_MUL(a, b) em_ee_mul_bits((a), (b))
#define L12_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L12_LE(a, b) em_ee_c_le_bits((a), (b))
#define L12_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L12_DIV(a, b) em_ee_div_bits((a), (b))
#define L12_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define L12_NEG(a) em_ee_neg_bits(a)

static inline int l12_begin(L12 *o, const EmLevel12PortHooks *h, EmLevel12PortFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return l12_failed(o) ? -1 : 0;
}

static inline int l12_end(const L12 *o) { return l12_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int l12_callback(L12 *o, uint32_t fn, uint32_t a0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_callback) return l12_latch(o, fn, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? l12_latch(o, fn, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int l12_method(L12 *o, uint32_t self)
{
    uint32_t fn = l12_u32(o, self + 0x4C);
    return l12_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Float arguments and results are bit patterns. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l12_c_001026A0(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001026A0) return l12_latch(o, 0x001026A0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0 ? l12_latch(o, 0x001026A0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00102738(L12 *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00102738) return l12_latch(o, 0x00102738u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_00102738(o->h->ctx, a0, a1, &out) < 0) return l12_latch(o, 0x00102738u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_00102760(L12 *o, uint32_t a0, uint32_t a1)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00102760) return l12_latch(o, 0x00102760u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102760(o->h->ctx, a0, a1) < 0 ? l12_latch(o, 0x00102760u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001028B8(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001028B8) return l12_latch(o, 0x001028B8u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0 ? l12_latch(o, 0x001028B8u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001028D0(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001028D0) return l12_latch(o, 0x001028D0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0 ? l12_latch(o, 0x001028D0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00102918(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00102918) return l12_latch(o, 0x00102918u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102918(o->h->ctx, a0, a1, a2) < 0 ? l12_latch(o, 0x00102918u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00102948(L12 *o, uint32_t a0, uint32_t a1)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00102948) return l12_latch(o, 0x00102948u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, a0, a1) < 0 ? l12_latch(o, 0x00102948u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00102958(L12 *o, uint32_t a0, uint32_t a1)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00102958) return l12_latch(o, 0x00102958u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102958(o->h->ctx, a0, a1) < 0 ? l12_latch(o, 0x00102958u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001029C0(L12 *o, uint32_t a0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001029C0) return l12_latch(o, 0x001029C0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001029C0(o->h->ctx, a0) < 0 ? l12_latch(o, 0x001029C0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00102B08(L12 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00102B08) return l12_latch(o, 0x00102B08u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102B08(o->h->ctx, a0, a1, l12_float(f2)) < 0 ? l12_latch(o, 0x00102B08u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00102BB0(L12 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00102BB0) return l12_latch(o, 0x00102BB0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102BB0(o->h->ctx, a0, a1, l12_float(f2)) < 0 ? l12_latch(o, 0x00102BB0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00102C58(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00102C58) return l12_latch(o, 0x00102C58u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102C58(o->h->ctx, a0, a1, a2) < 0 ? l12_latch(o, 0x00102C58u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00103230(L12 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00103230) return l12_latch(o, 0x00103230u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00103230(o->h->ctx, a0, a1, l12_float(f2)) < 0 ? l12_latch(o, 0x00103230u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0011CB90(L12 *o, uint32_t f0, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0011CB90) return l12_latch(o, 0x0011CB90u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011CB90(o->h->ctx, l12_float(f0), &out) < 0) return l12_latch(o, 0x0011CB90u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_0011DB90(L12 *o, uint32_t a0, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0011DB90) return l12_latch(o, 0x0011DB90u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011DB90(o->h->ctx, a0, result) < 0 ? l12_latch(o, 0x0011DB90u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0011DE90(L12 *o, uint32_t f0, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0011DE90) return l12_latch(o, 0x0011DE90u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011DE90(o->h->ctx, l12_float(f0), &out) < 0) return l12_latch(o, 0x0011DE90u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_0011DF78(L12 *o, uint32_t f0, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0011DF78) return l12_latch(o, 0x0011DF78u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011DF78(o->h->ctx, l12_float(f0), &out) < 0) return l12_latch(o, 0x0011DF78u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_0011E080(L12 *o, uint32_t f0, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0011E080) return l12_latch(o, 0x0011E080u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011E080(o->h->ctx, l12_float(f0), result) < 0 ? l12_latch(o, 0x0011E080u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0011E0A8(L12 *o, uint32_t a0, uint32_t f1, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0011E0A8) return l12_latch(o, 0x0011E0A8u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011E0A8(o->h->ctx, a0, l12_float(f1), &out) < 0) return l12_latch(o, 0x0011E0A8u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_0011E2A8(L12 *o, uint32_t f0, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return l12_latch(o, 0x0011E2A8u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011E2A8(o->h->ctx, l12_float(f0), &out) < 0) return l12_latch(o, 0x0011E2A8u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_0011E620(L12 *o, uint32_t f0, uint32_t f1, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0011E620) return l12_latch(o, 0x0011E620u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011E620(o->h->ctx, l12_float(f0), l12_float(f1), &out) < 0) return l12_latch(o, 0x0011E620u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_0011E748(L12 *o, uint32_t f0, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0011E748) return l12_latch(o, 0x0011E748u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011E748(o->h->ctx, l12_float(f0), &out) < 0) return l12_latch(o, 0x0011E748u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_0011FD78(L12 *o, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0011FD78) return l12_latch(o, 0x0011FD78u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011FD78(o->h->ctx, result) < 0 ? l12_latch(o, 0x0011FD78u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00122BB8(L12 *o, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00122BB8) return l12_latch(o, 0x00122BB8u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? l12_latch(o, 0x00122BB8u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00127758(L12 *o, uint64_t q0, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00127758) return l12_latch(o, 0x00127758u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_00127758(o->h->ctx, q0, &out) < 0) return l12_latch(o, 0x00127758u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_001281C0(L12 *o, uint32_t f0, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001281C0) return l12_latch(o, 0x001281C0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001281C0(o->h->ctx, l12_float(f0), result) < 0 ? l12_latch(o, 0x001281C0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00128250(L12 *o, uint32_t f0, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00128250) return l12_latch(o, 0x00128250u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00128250(o->h->ctx, l12_float(f0), result) < 0 ? l12_latch(o, 0x00128250u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00128350(L12 *o, uint32_t f0, uint64_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00128350) return l12_latch(o, 0x00128350u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00128350(o->h->ctx, l12_float(f0), result) < 0 ? l12_latch(o, 0x00128350u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001749A0(L12 *o, uint32_t a0, int32_t n1, int32_t n2, uint32_t f3)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001749A0) return l12_latch(o, 0x001749A0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001749A0(o->h->ctx, a0, n1, n2, l12_float(f3)) < 0 ? l12_latch(o, 0x001749A0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00174A50(L12 *o, uint32_t a0, uint32_t f1)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00174A50) return l12_latch(o, 0x00174A50u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00174A50(o->h->ctx, a0, l12_float(f1)) < 0 ? l12_latch(o, 0x00174A50u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00174AB0(L12 *o, uint32_t a0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00174AB0) return l12_latch(o, 0x00174AB0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00174AB0(o->h->ctx, a0) < 0 ? l12_latch(o, 0x00174AB0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001751A0(L12 *o, uint32_t a0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001751A0) return l12_latch(o, 0x001751A0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001751A0(o->h->ctx, a0) < 0 ? l12_latch(o, 0x001751A0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00175900(L12 *o, uint32_t a0, int32_t n1)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00175900) return l12_latch(o, 0x00175900u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00175900(o->h->ctx, a0, n1) < 0 ? l12_latch(o, 0x00175900u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001764E0(L12 *o, uint32_t a0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001764E0) return l12_latch(o, 0x001764E0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001764E0(o->h->ctx, a0) < 0 ? l12_latch(o, 0x001764E0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001796C0(L12 *o, uint32_t a0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001796C0) return l12_latch(o, 0x001796C0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001796C0(o->h->ctx, a0) < 0 ? l12_latch(o, 0x001796C0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_00188610(L12 *o, uint32_t a0, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_00188610) return l12_latch(o, 0x00188610u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_00188610(o->h->ctx, a0, result) < 0 ? l12_latch(o, 0x00188610u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0019A310(L12 *o, uint32_t a0, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0019A310) return l12_latch(o, 0x0019A310u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019A310(o->h->ctx, a0, result) < 0 ? l12_latch(o, 0x0019A310u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0019A6F0(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3, int32_t n4, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0019A6F0) return l12_latch(o, 0x0019A6F0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019A6F0(o->h->ctx, a0, a1, a2, n3, n4, result) < 0 ? l12_latch(o, 0x0019A6F0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0019AB20(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0019AB20) return l12_latch(o, 0x0019AB20u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019AB20(o->h->ctx, a0, a1, a2, n3, result) < 0 ? l12_latch(o, 0x0019AB20u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0019AD00(L12 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0019AD00) return l12_latch(o, 0x0019AD00u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019AD00(o->h->ctx, a0, a1, n2, result) < 0 ? l12_latch(o, 0x0019AD00u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0019AFE0(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0019AFE0) return l12_latch(o, 0x0019AFE0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019AFE0(o->h->ctx, a0, a1, a2, n3, result) < 0 ? l12_latch(o, 0x0019AFE0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0019B2C0(L12 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0019B2C0) return l12_latch(o, 0x0019B2C0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019B2C0(o->h->ctx, a0, a1, n2, result) < 0 ? l12_latch(o, 0x0019B2C0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0019B6C0(L12 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0019B6C0) return l12_latch(o, 0x0019B6C0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019B6C0(o->h->ctx, a0, a1, result) < 0 ? l12_latch(o, 0x0019B6C0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0019BC40(L12 *o, uint32_t a0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0019BC40) return l12_latch(o, 0x0019BC40u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019BC40(o->h->ctx, a0) < 0 ? l12_latch(o, 0x0019BC40u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0019F680(L12 *o, uint32_t a0, uint32_t a1, int32_t n2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0019F680) return l12_latch(o, 0x0019F680u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019F680(o->h->ctx, a0, a1, n2) < 0 ? l12_latch(o, 0x0019F680u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001AFC10(L12 *o, uint32_t a0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001AFC10) return l12_latch(o, 0x001AFC10u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, a0) < 0 ? l12_latch(o, 0x001AFC10u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001B1240(L12 *o, uint32_t a0, uint32_t f1, uint32_t f2, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001B1240) return l12_latch(o, 0x001B1240u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_001B1240(o->h->ctx, a0, l12_float(f1), l12_float(f2), &out) < 0) return l12_latch(o, 0x001B1240u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_001B12B0(L12 *o, uint32_t f0, uint32_t f1, uint32_t f2, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001B12B0) return l12_latch(o, 0x001B12B0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_001B12B0(o->h->ctx, l12_float(f0), l12_float(f1), l12_float(f2), &out) < 0) return l12_latch(o, 0x001B12B0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_001B1380(L12 *o, uint32_t a0, uint32_t a1, uint32_t f2, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001B1380) return l12_latch(o, 0x001B1380u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1380(o->h->ctx, a0, a1, l12_float(f2), result) < 0 ? l12_latch(o, 0x001B1380u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001B13F0(L12 *o, uint32_t a0, uint32_t a1, uint32_t f2, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001B13F0) return l12_latch(o, 0x001B13F0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B13F0(o->h->ctx, a0, a1, l12_float(f2), result) < 0 ? l12_latch(o, 0x001B13F0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001B1470(L12 *o, uint32_t f0, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001B1470) return l12_latch(o, 0x001B1470u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_001B1470(o->h->ctx, l12_float(f0), &out) < 0) return l12_latch(o, 0x001B1470u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_001B15D0(L12 *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001B15D0) return l12_latch(o, 0x001B15D0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_001B15D0(o->h->ctx, a0, a1, &out) < 0) return l12_latch(o, 0x001B15D0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED);
    *result = l12_bits(out);
    return 0;
}

static inline int l12_c_001B17A0(L12 *o, uint32_t a0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001B17A0) return l12_latch(o, 0x001B17A0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B17A0(o->h->ctx, a0) < 0 ? l12_latch(o, 0x001B17A0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001B1EA0(L12 *o, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return l12_latch(o, 0x001B1EA0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1EA0(o->h->ctx, n0, a1, a2, n3, result) < 0 ? l12_latch(o, 0x001B1EA0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001B2B10(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001B2B10) return l12_latch(o, 0x001B2B10u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2B10(o->h->ctx, a0, a1, a2) < 0 ? l12_latch(o, 0x001B2B10u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001B6F00(L12 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001B6F00) return l12_latch(o, 0x001B6F00u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B6F00(o->h->ctx, a0, a1, l12_float(f2)) < 0 ? l12_latch(o, 0x001B6F00u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001BA1A0(L12 *o, uint32_t a0, uint32_t a1)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return l12_latch(o, 0x001BA1A0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0 ? l12_latch(o, 0x001BA1A0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001BA1F0(L12 *o, uint32_t a0, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return l12_latch(o, 0x001BA1F0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, a0, result) < 0 ? l12_latch(o, 0x001BA1F0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001C6160(L12 *o, uint32_t a0, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001C6160) return l12_latch(o, 0x001C6160u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6160(o->h->ctx, a0, result) < 0 ? l12_latch(o, 0x001C6160u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001C64F0(L12 *o, uint32_t a0, uint32_t f1)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001C64F0) return l12_latch(o, 0x001C64F0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C64F0(o->h->ctx, a0, l12_float(f1)) < 0 ? l12_latch(o, 0x001C64F0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001C67E0(L12 *o, uint32_t a0, int32_t n1, uint32_t f2, uint32_t f3)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001C67E0) return l12_latch(o, 0x001C67E0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C67E0(o->h->ctx, a0, n1, l12_float(f2), l12_float(f3)) < 0 ? l12_latch(o, 0x001C67E0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001C68C0(L12 *o, uint32_t a0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001C68C0) return l12_latch(o, 0x001C68C0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C68C0(o->h->ctx, a0) < 0 ? l12_latch(o, 0x001C68C0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001C6DA0(L12 *o, uint32_t a0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001C6DA0) return l12_latch(o, 0x001C6DA0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6DA0(o->h->ctx, a0) < 0 ? l12_latch(o, 0x001C6DA0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001C7420(L12 *o, uint32_t a0, int32_t n1, int32_t n2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001C7420) return l12_latch(o, 0x001C7420u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C7420(o->h->ctx, a0, n1, n2) < 0 ? l12_latch(o, 0x001C7420u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001C94B0(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001C94B0) return l12_latch(o, 0x001C94B0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C94B0(o->h->ctx, a0, a1, a2, a3) < 0 ? l12_latch(o, 0x001C94B0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001C9940(L12 *o, uint32_t a0, int32_t n1, uint32_t a2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001C9940) return l12_latch(o, 0x001C9940u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C9940(o->h->ctx, a0, n1, a2) < 0 ? l12_latch(o, 0x001C9940u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001CA5E0(L12 *o, uint32_t a0, uint32_t a1, int32_t n2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001CA5E0) return l12_latch(o, 0x001CA5E0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA5E0(o->h->ctx, a0, a1, n2) < 0 ? l12_latch(o, 0x001CA5E0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001CB760(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001CB760) return l12_latch(o, 0x001CB760u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CB760(o->h->ctx, a0, a1, a2, n3) < 0 ? l12_latch(o, 0x001CB760u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001CCF70(L12 *o, uint32_t a0, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001CCF70) return l12_latch(o, 0x001CCF70u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CCF70(o->h->ctx, a0, result) < 0 ? l12_latch(o, 0x001CCF70u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001CFA60(L12 *o, uint32_t a0, uint32_t a1, uint32_t f2, uint32_t f3)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001CFA60) return l12_latch(o, 0x001CFA60u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFA60(o->h->ctx, a0, a1, l12_float(f2), l12_float(f3)) < 0 ? l12_latch(o, 0x001CFA60u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001CFBE0(L12 *o, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001CFBE0) return l12_latch(o, 0x001CFBE0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFBE0(o->h->ctx, n0, n1, a2, a3, n4) < 0 ? l12_latch(o, 0x001CFBE0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001D1F20(L12 *o, int32_t n0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001D1F20) return l12_latch(o, 0x001D1F20u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D1F20(o->h->ctx, n0) < 0 ? l12_latch(o, 0x001D1F20u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001D1F80(L12 *o, int32_t n0, int32_t n1, int32_t n2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001D1F80) return l12_latch(o, 0x001D1F80u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D1F80(o->h->ctx, n0, n1, n2) < 0 ? l12_latch(o, 0x001D1F80u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001D1FF0(L12 *o, int32_t n0, int32_t n1)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001D1FF0) return l12_latch(o, 0x001D1FF0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D1FF0(o->h->ctx, n0, n1) < 0 ? l12_latch(o, 0x001D1FF0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001D2090(L12 *o, int32_t n0, uint32_t a1)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001D2090) return l12_latch(o, 0x001D2090u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D2090(o->h->ctx, n0, a1) < 0 ? l12_latch(o, 0x001D2090u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001D2910(L12 *o, int32_t n0, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001D2910) return l12_latch(o, 0x001D2910u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D2910(o->h->ctx, n0, result) < 0 ? l12_latch(o, 0x001D2910u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001D6B10(L12 *o, int32_t n0, int32_t n1, int32_t n2, int32_t n3)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001D6B10) return l12_latch(o, 0x001D6B10u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D6B10(o->h->ctx, n0, n1, n2, n3) < 0 ? l12_latch(o, 0x001D6B10u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001D6BA0(L12 *o, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, int32_t n5)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001D6BA0) return l12_latch(o, 0x001D6BA0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D6BA0(o->h->ctx, n0, n1, n2, n3, n4, n5) < 0 ? l12_latch(o, 0x001D6BA0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001D8C20(L12 *o, int32_t n0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001D8C20) return l12_latch(o, 0x001D8C20u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D8C20(o->h->ctx, n0) < 0 ? l12_latch(o, 0x001D8C20u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001EFE00(L12 *o, int32_t n0, uint32_t a1)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001EFE00) return l12_latch(o, 0x001EFE00u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFE00(o->h->ctx, n0, a1) < 0 ? l12_latch(o, 0x001EFE00u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001EFEB0(L12 *o, int32_t n0, uint32_t a1)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001EFEB0) return l12_latch(o, 0x001EFEB0u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFEB0(o->h->ctx, n0, a1) < 0 ? l12_latch(o, 0x001EFEB0u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001F4E20(L12 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001F4E20) return l12_latch(o, 0x001F4E20u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F4E20(o->h->ctx, a0, a1, l12_float(f2)) < 0 ? l12_latch(o, 0x001F4E20u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001FAE70(L12 *o, int32_t n0)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001FAE70) return l12_latch(o, 0x001FAE70u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FAE70(o->h->ctx, n0) < 0 ? l12_latch(o, 0x001FAE70u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_001FBD50(L12 *o, uint32_t a0, int32_t n1, int32_t n2, uint32_t f3)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_001FBD50) return l12_latch(o, 0x001FBD50u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FBD50(o->h->ctx, a0, n1, n2, l12_float(f3)) < 0 ? l12_latch(o, 0x001FBD50u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l12_c_0021BE40(L12 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l12_failed(o)) return -1;
    if (!o->h->w_0021BE40) return l12_latch(o, 0x0021BE40u, EM_LEVEL12_PORT_FAULT_NULL_WORKER);
    return o->h->w_0021BE40(o->h->ctx, a0, a1, result) < 0 ? l12_latch(o, 0x0021BE40u, EM_LEVEL12_PORT_FAULT_WORKER_FAILED) : 0;
}
/* END GENERATED WRAPPERS */

/* Float constants (bit patterns, as the originals materialize them). */
#define F_ONE 0x3F800000u

/* Translations called by other translations (direct calls, no hook). */
uint32_t l12_0011BCF8(L12 *o, uint32_t x);
uint32_t l12_0011E420(L12 *o, uint32_t x, uint32_t sp);
uint32_t l12_001B1270(L12 *o, uint32_t obj, uint32_t px, uint32_t py);
int32_t l12_001B2F70(L12 *o, uint32_t pos, uint32_t out);
int32_t l12_001B3390(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f12, uint32_t sp);
int32_t l12_001B39F0(L12 *o, uint32_t self, uint32_t seg, uint32_t out);
int32_t l12_00146740(L12 *o, uint32_t e, uint32_t d, uint32_t sp);
void l12_001437E0(L12 *o, uint32_t e, uint32_t d);
int32_t l12_001821E0(L12 *o, uint32_t a);
void l12_00179560(L12 *o, uint32_t p);
void l12_0017F9E0(L12 *o, uint32_t p);
void l12_0016EF50(L12 *o, uint32_t p);
void l12_001831F0(L12 *o, int32_t n);
void l12_001A96F0(L12 *o, uint32_t a, uint32_t b);
void l12_001C9570(L12 *o, uint32_t m, uint32_t t, uint32_t r, uint32_t s);
void l12_001C6910(L12 *o, uint32_t obj);
void l12_001D3F60(L12 *o, int32_t sel, uint32_t arg);
void l12_001CAFA0(L12 *o, uint32_t a0, uint32_t a1);
void l12_001E8B40(L12 *o, int32_t n);
void l12_00138900(L12 *o, uint32_t self, uint32_t ent);
void l12_00138C20(L12 *o, uint32_t self, uint32_t ent, uint32_t sp);
void l12_0013BA20(L12 *o, uint32_t self, uint32_t ent);
void l12_0013BBB0(L12 *o, uint32_t self, uint32_t ent, uint32_t sp);
void l12_0013BE60(L12 *o, uint32_t self, uint32_t ent);
void l12_0013BF20(L12 *o, uint32_t self, uint32_t ent);
int32_t l12_0013C1F0(L12 *o, uint32_t self, uint32_t ent);
int32_t l12_0013C4C0(L12 *o, uint32_t self, uint32_t ent);
void l12_0013C8C0(L12 *o, uint32_t self, uint32_t ent, uint32_t sp);
int32_t l12_0013CD50(L12 *o, uint32_t self, uint32_t seg);
int32_t l12_0013D220(L12 *o, uint32_t self);
void l12_00824BE0(L12 *o, uint32_t self, uint32_t sp);
void l12_00825420(L12 *o, uint32_t self, uint32_t sp);
void l12_00825930(L12 *o, uint32_t self);

#endif /* EM_LEVEL12_PORT_INTERNAL_H */
