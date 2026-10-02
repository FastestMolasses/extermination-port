/* Shared plumbing of em_level14_port*.c: the memory view, fault latch, EE
 * float helpers, one typed wrapper per hook (written from one table, the
 * HOOKS list of tools/test_level14_port_reference.py, by a lane script that
 * is not committed; the test's header_checks compares this block and the
 * header's hook struct with HOOKS on every run), and the internal entry
 * points the translations call directly (the design of
 * em_level13_port_internal.h). Not a public interface.
 *
 * Floats inside the module are carried as their bit patterns (uint32_t);
 * the wrappers convert to and from the hook table's float parameters with
 * memcpy, so no value is ever converted or rounded on the way. */
#ifndef EM_LEVEL14_PORT_INTERNAL_H
#define EM_LEVEL14_PORT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level14_port.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel14PortHooks *h;
    EmLevel14PortFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L14;

static inline int l14_latch(L14 *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL14_PORT_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l14_failed(const L14 *o) { return o->fault->code != EM_LEVEL14_PORT_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l14_at(L14 *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l14_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l14_latch(o, address, EM_LEVEL14_PORT_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l14_u8(L14 *o, uint32_t a) { return *l14_at(o, a, 1); }
static inline uint32_t l14_u16(L14 *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l14_at(o, a, 2), 2);
    return v;
}
static inline int32_t l14_s16(L14 *o, uint32_t a) { return (int16_t)l14_u16(o, a); }
static inline uint32_t l14_u32(L14 *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l14_at(o, a, 4), 4);
    return v;
}
static inline int32_t l14_s32(L14 *o, uint32_t a) { return (int32_t)l14_u32(o, a); }
static inline int32_t l14_s8(L14 *o, uint32_t a) { return (int8_t)l14_u8(o, a); }
static inline uint64_t l14_u64(L14 *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, l14_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void l14_q(L14 *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, l14_at(o, a, 8), 8);
    memcpy(out + 8, l14_at(o, a + 8, 8), 8);
}
static inline void l14_wq(L14 *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(l14_at(o, a, 8), in, 8);
    memcpy(l14_at(o, a + 8, 8), in + 8, 8);
}

static inline void l14_w8(L14 *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l14_at(o, a, 1), &b, 1);
}
static inline void l14_w16(L14 *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l14_at(o, a, 2), &h, 2);
}
static inline void l14_w32(L14 *o, uint32_t a, uint32_t v) { memcpy(l14_at(o, a, 4), &v, 4); }
static inline void l14_w64(L14 *o, uint32_t a, uint64_t v) { memcpy(l14_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l14_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l14_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l14_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L14_ADD(a, b) em_ee_add_bits((a), (b))
#define L14_SUB(a, b) em_ee_sub_bits((a), (b))
#define L14_MUL(a, b) em_ee_mul_bits((a), (b))
#define L14_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L14_LE(a, b) em_ee_c_le_bits((a), (b))
#define L14_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L14_DIV(a, b) em_ee_div_bits((a), (b))
#define L14_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define L14_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define L14_MULA(a, b) em_ee_mula_bits((a), (b))
#define L14_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))

static inline int l14_begin(L14 *o, const EmLevel14PortHooks *h, EmLevel14PortFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return l14_failed(o) ? -1 : 0;
}

static inline int l14_end(const L14 *o) { return l14_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int l14_callback(L14 *o, uint32_t fn, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_callback) return l14_latch(o, fn, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? l14_latch(o, fn, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int l14_method(L14 *o, uint32_t self)
{
    uint32_t fn = l14_u32(o, self + 0x4C);
    return l14_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Float arguments and results are bit patterns. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l14_c_001026A0(L14 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001026A0) return l14_latch(o, 0x001026A0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0 ? l14_latch(o, 0x001026A0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00102948(L14 *o, uint32_t a0, uint32_t a1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00102948) return l14_latch(o, 0x00102948u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, a0, a1) < 0 ? l14_latch(o, 0x00102948u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001029C0(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001029C0) return l14_latch(o, 0x001029C0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001029C0(o->h->ctx, a0) < 0 ? l14_latch(o, 0x001029C0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00102C58(L14 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00102C58) return l14_latch(o, 0x00102C58u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102C58(o->h->ctx, a0, a1, a2) < 0 ? l14_latch(o, 0x00102C58u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00103230(L14 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00103230) return l14_latch(o, 0x00103230u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00103230(o->h->ctx, a0, a1, l14_float(f2)) < 0 ? l14_latch(o, 0x00103230u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_0011DF78(L14 *o, uint32_t f0, uint32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_0011DF78) return l14_latch(o, 0x0011DF78u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011DF78(o->h->ctx, l14_float(f0), &out) < 0) return l14_latch(o, 0x0011DF78u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED);
    *result = l14_bits(out);
    return 0;
}

static inline int l14_c_0011E2A8(L14 *o, uint32_t f0, uint32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return l14_latch(o, 0x0011E2A8u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011E2A8(o->h->ctx, l14_float(f0), &out) < 0) return l14_latch(o, 0x0011E2A8u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED);
    *result = l14_bits(out);
    return 0;
}

static inline int l14_c_0011E748(L14 *o, uint32_t f0, uint32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_0011E748) return l14_latch(o, 0x0011E748u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011E748(o->h->ctx, l14_float(f0), &out) < 0) return l14_latch(o, 0x0011E748u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED);
    *result = l14_bits(out);
    return 0;
}

static inline int l14_c_00122BB8(L14 *o, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00122BB8) return l14_latch(o, 0x00122BB8u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? l14_latch(o, 0x00122BB8u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001281C0(L14 *o, uint32_t f0, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001281C0) return l14_latch(o, 0x001281C0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001281C0(o->h->ctx, l14_float(f0), result) < 0 ? l14_latch(o, 0x001281C0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00131940(L14 *o, uint32_t a0, uint32_t a1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00131940) return l14_latch(o, 0x00131940u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00131940(o->h->ctx, a0, a1) < 0 ? l14_latch(o, 0x00131940u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00131ED0(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00131ED0) return l14_latch(o, 0x00131ED0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00131ED0(o->h->ctx, a0) < 0 ? l14_latch(o, 0x00131ED0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00132490(L14 *o, uint32_t a0, uint32_t a1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00132490) return l14_latch(o, 0x00132490u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00132490(o->h->ctx, a0, a1) < 0 ? l14_latch(o, 0x00132490u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001328D0(L14 *o, uint32_t a0, uint32_t a1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001328D0) return l14_latch(o, 0x001328D0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001328D0(o->h->ctx, a0, a1) < 0 ? l14_latch(o, 0x001328D0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00133A20(L14 *o, uint32_t a0, uint32_t a1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00133A20) return l14_latch(o, 0x00133A20u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00133A20(o->h->ctx, a0, a1) < 0 ? l14_latch(o, 0x00133A20u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00153B50(L14 *o, uint32_t a0, uint32_t a1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00153B50) return l14_latch(o, 0x00153B50u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00153B50(o->h->ctx, a0, a1) < 0 ? l14_latch(o, 0x00153B50u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00153EA0(L14 *o, uint32_t a0, uint32_t a1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00153EA0) return l14_latch(o, 0x00153EA0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00153EA0(o->h->ctx, a0, a1) < 0 ? l14_latch(o, 0x00153EA0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001749A0(L14 *o, uint32_t a0, int32_t n1, int32_t n2, uint32_t f3)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001749A0) return l14_latch(o, 0x001749A0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001749A0(o->h->ctx, a0, n1, n2, l14_float(f3)) < 0 ? l14_latch(o, 0x001749A0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001885B0(L14 *o, uint32_t a0, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001885B0) return l14_latch(o, 0x001885B0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001885B0(o->h->ctx, a0, result) < 0 ? l14_latch(o, 0x001885B0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00199DB0(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00199DB0) return l14_latch(o, 0x00199DB0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00199DB0(o->h->ctx, a0) < 0 ? l14_latch(o, 0x00199DB0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00199FA0(L14 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00199FA0) return l14_latch(o, 0x00199FA0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00199FA0(o->h->ctx, a0, a1, result) < 0 ? l14_latch(o, 0x00199FA0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_0019BC40(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_0019BC40) return l14_latch(o, 0x0019BC40u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019BC40(o->h->ctx, a0) < 0 ? l14_latch(o, 0x0019BC40u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_0019C6F0(L14 *o, int32_t n0, int32_t n1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_0019C6F0) return l14_latch(o, 0x0019C6F0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019C6F0(o->h->ctx, n0, n1) < 0 ? l14_latch(o, 0x0019C6F0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001A2370(L14 *o, uint32_t a0, uint32_t a1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001A2370) return l14_latch(o, 0x001A2370u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001A2370(o->h->ctx, a0, a1) < 0 ? l14_latch(o, 0x001A2370u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001AEE10(L14 *o, int32_t n0, int32_t n1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001AEE10) return l14_latch(o, 0x001AEE10u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AEE10(o->h->ctx, n0, n1) < 0 ? l14_latch(o, 0x001AEE10u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001AFC10(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001AFC10) return l14_latch(o, 0x001AFC10u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, a0) < 0 ? l14_latch(o, 0x001AFC10u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001B0CD0(L14 *o, uint32_t a0, int32_t n1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001B0CD0) return l14_latch(o, 0x001B0CD0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0CD0(o->h->ctx, a0, n1) < 0 ? l14_latch(o, 0x001B0CD0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001B0FD0(L14 *o, uint32_t a0, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return l14_latch(o, 0x001B0FD0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0FD0(o->h->ctx, a0, result) < 0 ? l14_latch(o, 0x001B0FD0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001B10B0(L14 *o, uint32_t a0, int32_t n1, int32_t n2, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001B10B0) return l14_latch(o, 0x001B10B0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B10B0(o->h->ctx, a0, n1, n2, result) < 0 ? l14_latch(o, 0x001B10B0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001B1190(L14 *o, int32_t n0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001B1190) return l14_latch(o, 0x001B1190u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1190(o->h->ctx, n0) < 0 ? l14_latch(o, 0x001B1190u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001B1470(L14 *o, uint32_t f0, uint32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001B1470) return l14_latch(o, 0x001B1470u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_001B1470(o->h->ctx, l14_float(f0), &out) < 0) return l14_latch(o, 0x001B1470u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED);
    *result = l14_bits(out);
    return 0;
}

static inline int l14_c_001B1630(L14 *o, uint32_t f0, uint32_t f1, uint32_t f2, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001B1630) return l14_latch(o, 0x001B1630u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1630(o->h->ctx, l14_float(f0), l14_float(f1), l14_float(f2), result) < 0 ? l14_latch(o, 0x001B1630u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001B17A0(L14 *o, uint32_t a0, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001B17A0) return l14_latch(o, 0x001B17A0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B17A0(o->h->ctx, a0, result) < 0 ? l14_latch(o, 0x001B17A0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001B1B70(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001B1B70) return l14_latch(o, 0x001B1B70u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1B70(o->h->ctx, a0) < 0 ? l14_latch(o, 0x001B1B70u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001B1EA0(L14 *o, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return l14_latch(o, 0x001B1EA0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1EA0(o->h->ctx, n0, a1, a2, n3, result) < 0 ? l14_latch(o, 0x001B1EA0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001B5360(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001B5360) return l14_latch(o, 0x001B5360u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B5360(o->h->ctx, a0) < 0 ? l14_latch(o, 0x001B5360u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001B6660(L14 *o, uint32_t a0, uint32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001B6660) return l14_latch(o, 0x001B6660u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B6660(o->h->ctx, a0, result) < 0 ? l14_latch(o, 0x001B6660u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001BA1A0(L14 *o, uint32_t a0, uint32_t a1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return l14_latch(o, 0x001BA1A0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0 ? l14_latch(o, 0x001BA1A0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001BA1C0(L14 *o, uint32_t a0, int32_t n1, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return l14_latch(o, 0x001BA1C0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1C0(o->h->ctx, a0, n1, result) < 0 ? l14_latch(o, 0x001BA1C0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001BA1F0(L14 *o, uint32_t a0, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return l14_latch(o, 0x001BA1F0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, a0, result) < 0 ? l14_latch(o, 0x001BA1F0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001BA540(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001BA540) return l14_latch(o, 0x001BA540u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA540(o->h->ctx, a0) < 0 ? l14_latch(o, 0x001BA540u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001BA580(L14 *o, uint32_t a0, int32_t n1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001BA580) return l14_latch(o, 0x001BA580u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA580(o->h->ctx, a0, n1) < 0 ? l14_latch(o, 0x001BA580u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001BA8E0(L14 *o, uint32_t a0, int32_t n1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001BA8E0) return l14_latch(o, 0x001BA8E0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA8E0(o->h->ctx, a0, n1) < 0 ? l14_latch(o, 0x001BA8E0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001C4760(L14 *o, int32_t n0, int32_t n1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001C4760) return l14_latch(o, 0x001C4760u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C4760(o->h->ctx, n0, n1) < 0 ? l14_latch(o, 0x001C4760u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001C4820(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001C4820) return l14_latch(o, 0x001C4820u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C4820(o->h->ctx, a0) < 0 ? l14_latch(o, 0x001C4820u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001C5C90(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001C5C90) return l14_latch(o, 0x001C5C90u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C5C90(o->h->ctx, a0) < 0 ? l14_latch(o, 0x001C5C90u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001C6160(L14 *o, uint32_t a0, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001C6160) return l14_latch(o, 0x001C6160u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6160(o->h->ctx, a0, result) < 0 ? l14_latch(o, 0x001C6160u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001C6380(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001C6380) return l14_latch(o, 0x001C6380u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6380(o->h->ctx, a0) < 0 ? l14_latch(o, 0x001C6380u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001C63E0(L14 *o, uint32_t a0, int32_t n1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001C63E0) return l14_latch(o, 0x001C63E0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C63E0(o->h->ctx, a0, n1) < 0 ? l14_latch(o, 0x001C63E0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001C64F0(L14 *o, uint32_t a0, uint32_t f1, int32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001C64F0) return l14_latch(o, 0x001C64F0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C64F0(o->h->ctx, a0, l14_float(f1), result) < 0 ? l14_latch(o, 0x001C64F0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001C67E0(L14 *o, uint32_t a0, int32_t n1, uint32_t f2, uint32_t f3)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001C67E0) return l14_latch(o, 0x001C67E0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C67E0(o->h->ctx, a0, n1, l14_float(f2), l14_float(f3)) < 0 ? l14_latch(o, 0x001C67E0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001C68C0(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001C68C0) return l14_latch(o, 0x001C68C0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C68C0(o->h->ctx, a0) < 0 ? l14_latch(o, 0x001C68C0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001CA5F0(L14 *o, uint32_t a0, int32_t n1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001CA5F0) return l14_latch(o, 0x001CA5F0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA5F0(o->h->ctx, a0, n1) < 0 ? l14_latch(o, 0x001CA5F0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001CA6F0(L14 *o, uint32_t a0, int32_t n1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001CA6F0) return l14_latch(o, 0x001CA6F0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA6F0(o->h->ctx, a0, n1) < 0 ? l14_latch(o, 0x001CA6F0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001CFB50(L14 *o, uint32_t a0, int32_t n1, uint32_t a2, uint32_t f3, uint32_t f4, uint32_t f5, uint32_t f6, uint32_t f7)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001CFB50) return l14_latch(o, 0x001CFB50u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFB50(o->h->ctx, a0, n1, a2, l14_float(f3), l14_float(f4), l14_float(f5), l14_float(f6), l14_float(f7)) < 0 ? l14_latch(o, 0x001CFB50u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001CFBE0(L14 *o, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001CFBE0) return l14_latch(o, 0x001CFBE0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFBE0(o->h->ctx, n0, n1, a2, a3, n4) < 0 ? l14_latch(o, 0x001CFBE0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001EFD90(L14 *o, int32_t n0, uint32_t a1, uint32_t a2)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001EFD90) return l14_latch(o, 0x001EFD90u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFD90(o->h->ctx, n0, a1, a2) < 0 ? l14_latch(o, 0x001EFD90u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001EFE00(L14 *o, int32_t n0, uint32_t a1, uint32_t *result)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001EFE00) return l14_latch(o, 0x001EFE00u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFE00(o->h->ctx, n0, a1, result) < 0 ? l14_latch(o, 0x001EFE00u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001F5940(L14 *o, int32_t n0, uint32_t a1, int32_t n2)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001F5940) return l14_latch(o, 0x001F5940u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F5940(o->h->ctx, n0, a1, n2) < 0 ? l14_latch(o, 0x001F5940u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001F66F0(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001F66F0) return l14_latch(o, 0x001F66F0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F66F0(o->h->ctx, a0) < 0 ? l14_latch(o, 0x001F66F0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001FAE70(L14 *o, int32_t n0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001FAE70) return l14_latch(o, 0x001FAE70u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FAE70(o->h->ctx, n0) < 0 ? l14_latch(o, 0x001FAE70u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001FBD50(L14 *o, uint32_t a0, int32_t n1, int32_t n2, uint32_t f3)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001FBD50) return l14_latch(o, 0x001FBD50u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FBD50(o->h->ctx, a0, n1, n2, l14_float(f3)) < 0 ? l14_latch(o, 0x001FBD50u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001FC3C0(L14 *o, uint32_t a0, uint32_t a1, int32_t n2, uint32_t f3, uint32_t f4)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001FC3C0) return l14_latch(o, 0x001FC3C0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FC3C0(o->h->ctx, a0, a1, n2, l14_float(f3), l14_float(f4)) < 0 ? l14_latch(o, 0x001FC3C0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_001FC520(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_001FC520) return l14_latch(o, 0x001FC520u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FC520(o->h->ctx, a0) < 0 ? l14_latch(o, 0x001FC520u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_0021C040(L14 *o, uint32_t a0, uint32_t a1)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_0021C040) return l14_latch(o, 0x0021C040u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_0021C040(o->h->ctx, a0, a1) < 0 ? l14_latch(o, 0x0021C040u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00823B40(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00823B40) return l14_latch(o, 0x00823B40u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00823B40(o->h->ctx, a0) < 0 ? l14_latch(o, 0x00823B40u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00823C80(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00823C80) return l14_latch(o, 0x00823C80u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00823C80(o->h->ctx, a0) < 0 ? l14_latch(o, 0x00823C80u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00824240(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00824240) return l14_latch(o, 0x00824240u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00824240(o->h->ctx, a0) < 0 ? l14_latch(o, 0x00824240u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00824350(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00824350) return l14_latch(o, 0x00824350u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00824350(o->h->ctx, a0) < 0 ? l14_latch(o, 0x00824350u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_008243E0(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_008243E0) return l14_latch(o, 0x008243E0u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_008243E0(o->h->ctx, a0) < 0 ? l14_latch(o, 0x008243E0u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00824B40(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00824B40) return l14_latch(o, 0x00824B40u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00824B40(o->h->ctx, a0) < 0 ? l14_latch(o, 0x00824B40u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l14_c_00824C90(L14 *o, uint32_t a0)
{
    if (l14_failed(o)) return -1;
    if (!o->h->w_00824C90) return l14_latch(o, 0x00824C90u, EM_LEVEL14_PORT_FAULT_NULL_WORKER);
    return o->h->w_00824C90(o->h->ctx, a0) < 0 ? l14_latch(o, 0x00824C90u, EM_LEVEL14_PORT_FAULT_WORKER_FAILED) : 0;
}
/* END GENERATED WRAPPERS */

/* Translations called by other translations (direct calls, no hook). */
void l14_00131740(L14 *o, uint32_t self, uint32_t ent);
void l14_00131B10(L14 *o, uint32_t self, uint32_t ent);
void l14_001339E0(L14 *o, uint32_t a0, uint32_t ent);
void l14_00153A10(L14 *o, uint32_t self, uint32_t ent);
void l14_00153A90(L14 *o, uint32_t self, uint32_t out);

#endif /* EM_LEVEL14_PORT_INTERNAL_H */
