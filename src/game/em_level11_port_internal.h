/* Shared plumbing of em_level11_port*.c: the memory view, fault latch, EE
 * float helpers, one typed wrapper per hook (written from one table, the
 * HOOKS list of tools/test_level11_port_reference.py, by a lane script that
 * is not committed; the test's header_checks compares this block and the
 * header's hook struct with HOOKS on every run), and the internal entry
 * points the translations call directly (the design of
 * em_level9_port_internal.h). Not a public interface.
 *
 * Floats inside the module are carried as their bit patterns (uint32_t);
 * the wrappers convert to and from the hook table's float parameters with
 * memcpy, so no value is ever converted or rounded on the way. */
#ifndef EM_LEVEL11_PORT_INTERNAL_H
#define EM_LEVEL11_PORT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level11_port.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel11PortHooks *h;
    EmLevel11PortFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L11;

static inline int l11_latch(L11 *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL11_PORT_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l11_failed(const L11 *o) { return o->fault->code != EM_LEVEL11_PORT_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l11_at(L11 *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l11_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l11_latch(o, address, EM_LEVEL11_PORT_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l11_u8(L11 *o, uint32_t a) { return *l11_at(o, a, 1); }
static inline uint32_t l11_u16(L11 *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l11_at(o, a, 2), 2);
    return v;
}
static inline int32_t l11_s16(L11 *o, uint32_t a) { return (int16_t)l11_u16(o, a); }
static inline uint32_t l11_u32(L11 *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l11_at(o, a, 4), 4);
    return v;
}
static inline int32_t l11_s32(L11 *o, uint32_t a) { return (int32_t)l11_u32(o, a); }

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void l11_q(L11 *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, l11_at(o, a, 8), 8);
    memcpy(out + 8, l11_at(o, a + 8, 8), 8);
}
static inline void l11_wq(L11 *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(l11_at(o, a, 8), in, 8);
    memcpy(l11_at(o, a + 8, 8), in + 8, 8);
}

static inline void l11_w8(L11 *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l11_at(o, a, 1), &b, 1);
}
static inline void l11_w16(L11 *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l11_at(o, a, 2), &h, 2);
}
static inline void l11_w32(L11 *o, uint32_t a, uint32_t v) { memcpy(l11_at(o, a, 4), &v, 4); }
static inline void l11_w64(L11 *o, uint32_t a, uint64_t v) { memcpy(l11_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l11_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l11_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l11_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L11_ADD(a, b) em_ee_add_bits((a), (b))
#define L11_SUB(a, b) em_ee_sub_bits((a), (b))
#define L11_MUL(a, b) em_ee_mul_bits((a), (b))
#define L11_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L11_LE(a, b) em_ee_c_le_bits((a), (b))
#define L11_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L11_DIV(a, b) em_ee_div_bits((a), (b))
#define L11_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))

static inline int l11_begin(L11 *o, const EmLevel11PortHooks *h, EmLevel11PortFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return l11_failed(o) ? -1 : 0;
}

static inline int l11_end(const L11 *o) { return l11_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int l11_callback(L11 *o, uint32_t fn, uint32_t a0)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_callback) return l11_latch(o, fn, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? l11_latch(o, fn, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int l11_method(L11 *o, uint32_t self)
{
    uint32_t fn = l11_u32(o, self + 0x4C);
    return l11_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Float arguments and results are bit patterns. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l11_c_001000E0(L11 *o, uint64_t q0, uint64_t q1, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001000E0) return l11_latch(o, 0x001000E0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001000E0(o->h->ctx, q0, q1, result) < 0 ? l11_latch(o, 0x001000E0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001026A0(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001026A0) return l11_latch(o, 0x001026A0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0 ? l11_latch(o, 0x001026A0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00102850(L11 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00102850) return l11_latch(o, 0x00102850u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102850(o->h->ctx, a0, a1, l11_float(f2)) < 0 ? l11_latch(o, 0x00102850u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001028B8(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001028B8) return l11_latch(o, 0x001028B8u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0 ? l11_latch(o, 0x001028B8u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001028D0(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001028D0) return l11_latch(o, 0x001028D0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0 ? l11_latch(o, 0x001028D0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00102900(L11 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00102900) return l11_latch(o, 0x00102900u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102900(o->h->ctx, a0, a1, l11_float(f2)) < 0 ? l11_latch(o, 0x00102900u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00102918(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00102918) return l11_latch(o, 0x00102918u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102918(o->h->ctx, a0, a1, a2) < 0 ? l11_latch(o, 0x00102918u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00102948(L11 *o, uint32_t a0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00102948) return l11_latch(o, 0x00102948u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, a0, a1) < 0 ? l11_latch(o, 0x00102948u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00102958(L11 *o, uint32_t a0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00102958) return l11_latch(o, 0x00102958u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102958(o->h->ctx, a0, a1) < 0 ? l11_latch(o, 0x00102958u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001029C0(L11 *o, uint32_t a0)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001029C0) return l11_latch(o, 0x001029C0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001029C0(o->h->ctx, a0) < 0 ? l11_latch(o, 0x001029C0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00102BB0(L11 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00102BB0) return l11_latch(o, 0x00102BB0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102BB0(o->h->ctx, a0, a1, l11_float(f2)) < 0 ? l11_latch(o, 0x00102BB0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00102C58(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00102C58) return l11_latch(o, 0x00102C58u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102C58(o->h->ctx, a0, a1, a2) < 0 ? l11_latch(o, 0x00102C58u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001157F0(L11 *o, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001157F0) return l11_latch(o, 0x001157F0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001157F0(o->h->ctx, n0, n1, n2, n3, result) < 0 ? l11_latch(o, 0x001157F0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001179E0(L11 *o, int32_t n0, uint32_t a1, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001179E0) return l11_latch(o, 0x001179E0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001179E0(o->h->ctx, n0, a1, result) < 0 ? l11_latch(o, 0x001179E0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00117BA0(L11 *o, int32_t n0, int32_t n1, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00117BA0) return l11_latch(o, 0x00117BA0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00117BA0(o->h->ctx, n0, n1, result) < 0 ? l11_latch(o, 0x00117BA0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_0011DE90(L11 *o, uint32_t f0, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_0011DE90) return l11_latch(o, 0x0011DE90u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_0011DE90(o->h->ctx, l11_float(f0), &r) < 0) return l11_latch(o, 0x0011DE90u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED);
    *result = l11_bits(r);
    return 0;
}

static inline int l11_c_0011DF78(L11 *o, uint32_t f0, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_0011DF78) return l11_latch(o, 0x0011DF78u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_0011DF78(o->h->ctx, l11_float(f0), &r) < 0) return l11_latch(o, 0x0011DF78u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED);
    *result = l11_bits(r);
    return 0;
}

static inline int l11_c_0011E2A8(L11 *o, uint32_t f0, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return l11_latch(o, 0x0011E2A8u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_0011E2A8(o->h->ctx, l11_float(f0), &r) < 0) return l11_latch(o, 0x0011E2A8u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED);
    *result = l11_bits(r);
    return 0;
}

static inline int l11_c_0011E620(L11 *o, uint32_t f0, uint32_t f1, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_0011E620) return l11_latch(o, 0x0011E620u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_0011E620(o->h->ctx, l11_float(f0), l11_float(f1), &r) < 0) return l11_latch(o, 0x0011E620u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED);
    *result = l11_bits(r);
    return 0;
}

static inline int l11_c_00122BB8(L11 *o, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00122BB8) return l11_latch(o, 0x00122BB8u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? l11_latch(o, 0x00122BB8u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001274B0(L11 *o, uint64_t q0, uint64_t q1, uint64_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001274B0) return l11_latch(o, 0x001274B0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001274B0(o->h->ctx, q0, q1, result) < 0 ? l11_latch(o, 0x001274B0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00128250(L11 *o, uint32_t f0, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00128250) return l11_latch(o, 0x00128250u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00128250(o->h->ctx, l11_float(f0), result) < 0 ? l11_latch(o, 0x00128250u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00128350(L11 *o, uint32_t f0, uint64_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00128350) return l11_latch(o, 0x00128350u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00128350(o->h->ctx, l11_float(f0), result) < 0 ? l11_latch(o, 0x00128350u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00131ED0(L11 *o, uint32_t a0)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00131ED0) return l11_latch(o, 0x00131ED0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00131ED0(o->h->ctx, a0) < 0 ? l11_latch(o, 0x00131ED0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001434C0(L11 *o, uint32_t a0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001434C0) return l11_latch(o, 0x001434C0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001434C0(o->h->ctx, a0, a1) < 0 ? l11_latch(o, 0x001434C0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00143610(L11 *o, uint32_t a0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00143610) return l11_latch(o, 0x00143610u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00143610(o->h->ctx, a0, a1) < 0 ? l11_latch(o, 0x00143610u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001437E0(L11 *o, uint32_t a0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001437E0) return l11_latch(o, 0x001437E0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001437E0(o->h->ctx, a0, a1) < 0 ? l11_latch(o, 0x001437E0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00143AF0(L11 *o, uint32_t a0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00143AF0) return l11_latch(o, 0x00143AF0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00143AF0(o->h->ctx, a0, a1) < 0 ? l11_latch(o, 0x00143AF0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00144040(L11 *o, uint32_t a0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00144040) return l11_latch(o, 0x00144040u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00144040(o->h->ctx, a0, a1) < 0 ? l11_latch(o, 0x00144040u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00144C20(L11 *o, uint32_t a0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00144C20) return l11_latch(o, 0x00144C20u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00144C20(o->h->ctx, a0, a1) < 0 ? l11_latch(o, 0x00144C20u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001450B0(L11 *o, uint32_t a0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001450B0) return l11_latch(o, 0x001450B0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001450B0(o->h->ctx, a0, a1) < 0 ? l11_latch(o, 0x001450B0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00145850(L11 *o, uint32_t a0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00145850) return l11_latch(o, 0x00145850u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00145850(o->h->ctx, a0, a1) < 0 ? l11_latch(o, 0x00145850u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00146740(L11 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00146740) return l11_latch(o, 0x00146740u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00146740(o->h->ctx, a0, a1, result) < 0 ? l11_latch(o, 0x00146740u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00146AF0(L11 *o, uint32_t a0, uint32_t a1, uint32_t f2, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00146AF0) return l11_latch(o, 0x00146AF0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00146AF0(o->h->ctx, a0, a1, l11_float(f2), result) < 0 ? l11_latch(o, 0x00146AF0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_0019A570(L11 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_0019A570) return l11_latch(o, 0x0019A570u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019A570(o->h->ctx, a0, a1, n2, n3, result) < 0 ? l11_latch(o, 0x0019A570u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_0019AB20(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_0019AB20) return l11_latch(o, 0x0019AB20u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019AB20(o->h->ctx, a0, a1, a2, n3, result) < 0 ? l11_latch(o, 0x0019AB20u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_0019AD00(L11 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_0019AD00) return l11_latch(o, 0x0019AD00u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019AD00(o->h->ctx, a0, a1, n2, result) < 0 ? l11_latch(o, 0x0019AD00u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_0019D330(L11 *o, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_0019D330) return l11_latch(o, 0x0019D330u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019D330(o->h->ctx, result) < 0 ? l11_latch(o, 0x0019D330u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001A0B10(L11 *o, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001A0B10) return l11_latch(o, 0x001A0B10u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001A0B10(o->h->ctx, result) < 0 ? l11_latch(o, 0x001A0B10u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001A6440(L11 *o, int32_t n0, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001A6440) return l11_latch(o, 0x001A6440u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001A6440(o->h->ctx, n0, result) < 0 ? l11_latch(o, 0x001A6440u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001A7280(L11 *o, int32_t n0, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001A7280) return l11_latch(o, 0x001A7280u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001A7280(o->h->ctx, n0, result) < 0 ? l11_latch(o, 0x001A7280u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001AFA90(L11 *o, int32_t n0, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001AFA90) return l11_latch(o, 0x001AFA90u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFA90(o->h->ctx, n0, result) < 0 ? l11_latch(o, 0x001AFA90u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001AFC10(L11 *o, uint32_t a0)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001AFC10) return l11_latch(o, 0x001AFC10u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, a0) < 0 ? l11_latch(o, 0x001AFC10u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B0D80(L11 *o, uint32_t a0)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B0D80) return l11_latch(o, 0x001B0D80u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0D80(o->h->ctx, a0) < 0 ? l11_latch(o, 0x001B0D80u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B10B0(L11 *o, uint32_t a0, int32_t n1, int32_t n2, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B10B0) return l11_latch(o, 0x001B10B0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B10B0(o->h->ctx, a0, n1, n2, result) < 0 ? l11_latch(o, 0x001B10B0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B1240(L11 *o, uint32_t a0, uint32_t f1, uint32_t f2, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B1240) return l11_latch(o, 0x001B1240u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_001B1240(o->h->ctx, a0, l11_float(f1), l11_float(f2), &r) < 0) return l11_latch(o, 0x001B1240u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED);
    *result = l11_bits(r);
    return 0;
}

static inline int l11_c_001B12B0(L11 *o, uint32_t f0, uint32_t f1, uint32_t f2, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B12B0) return l11_latch(o, 0x001B12B0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_001B12B0(o->h->ctx, l11_float(f0), l11_float(f1), l11_float(f2), &r) < 0) return l11_latch(o, 0x001B12B0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED);
    *result = l11_bits(r);
    return 0;
}

static inline int l11_c_001B13F0(L11 *o, uint32_t a0, uint32_t a1, uint32_t f2, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B13F0) return l11_latch(o, 0x001B13F0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B13F0(o->h->ctx, a0, a1, l11_float(f2), result) < 0 ? l11_latch(o, 0x001B13F0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B1470(L11 *o, uint32_t f0, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B1470) return l11_latch(o, 0x001B1470u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_001B1470(o->h->ctx, l11_float(f0), &r) < 0) return l11_latch(o, 0x001B1470u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED);
    *result = l11_bits(r);
    return 0;
}

static inline int l11_c_001B1560(L11 *o, uint32_t a0, uint32_t a1, uint32_t f2, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B1560) return l11_latch(o, 0x001B1560u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1560(o->h->ctx, a0, a1, l11_float(f2), result) < 0 ? l11_latch(o, 0x001B1560u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B15D0(L11 *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B15D0) return l11_latch(o, 0x001B15D0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_001B15D0(o->h->ctx, a0, a1, &r) < 0) return l11_latch(o, 0x001B15D0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED);
    *result = l11_bits(r);
    return 0;
}

static inline int l11_c_001B17A0(L11 *o, uint32_t a0, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B17A0) return l11_latch(o, 0x001B17A0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B17A0(o->h->ctx, a0, result) < 0 ? l11_latch(o, 0x001B17A0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B1B70(L11 *o, uint32_t a0)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B1B70) return l11_latch(o, 0x001B1B70u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1B70(o->h->ctx, a0) < 0 ? l11_latch(o, 0x001B1B70u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B1E20(L11 *o, int32_t n0, int32_t n1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B1E20) return l11_latch(o, 0x001B1E20u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1E20(o->h->ctx, n0, n1) < 0 ? l11_latch(o, 0x001B1E20u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B1EA0(L11 *o, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return l11_latch(o, 0x001B1EA0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1EA0(o->h->ctx, n0, a1, a2, n3, result) < 0 ? l11_latch(o, 0x001B1EA0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B2140(L11 *o, uint32_t a0, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B2140) return l11_latch(o, 0x001B2140u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2140(o->h->ctx, a0, result) < 0 ? l11_latch(o, 0x001B2140u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B2B10(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B2B10) return l11_latch(o, 0x001B2B10u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2B10(o->h->ctx, a0, a1, a2) < 0 ? l11_latch(o, 0x001B2B10u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B2BF0(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f3, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B2BF0) return l11_latch(o, 0x001B2BF0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2BF0(o->h->ctx, a0, a1, a2, l11_float(f3), result) < 0 ? l11_latch(o, 0x001B2BF0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B2F70(L11 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B2F70) return l11_latch(o, 0x001B2F70u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2F70(o->h->ctx, a0, a1, result) < 0 ? l11_latch(o, 0x001B2F70u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B32F0(L11 *o, uint32_t a0, uint32_t a1, uint32_t f2, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B32F0) return l11_latch(o, 0x001B32F0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B32F0(o->h->ctx, a0, a1, l11_float(f2), result) < 0 ? l11_latch(o, 0x001B32F0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B3390(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f3, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B3390) return l11_latch(o, 0x001B3390u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B3390(o->h->ctx, a0, a1, a2, l11_float(f3), result) < 0 ? l11_latch(o, 0x001B3390u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B3440(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f3, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B3440) return l11_latch(o, 0x001B3440u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B3440(o->h->ctx, a0, a1, a2, l11_float(f3), result) < 0 ? l11_latch(o, 0x001B3440u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B3580(L11 *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B3580) return l11_latch(o, 0x001B3580u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_001B3580(o->h->ctx, a0, a1, &r) < 0) return l11_latch(o, 0x001B3580u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED);
    *result = l11_bits(r);
    return 0;
}

static inline int l11_c_001B37D0(L11 *o, uint32_t a0, uint32_t f1, uint32_t f2, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B37D0) return l11_latch(o, 0x001B37D0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_001B37D0(o->h->ctx, a0, l11_float(f1), l11_float(f2), &r) < 0) return l11_latch(o, 0x001B37D0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED);
    *result = l11_bits(r);
    return 0;
}

static inline int l11_c_001B39F0(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B39F0) return l11_latch(o, 0x001B39F0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B39F0(o->h->ctx, a0, a1, a2, result) < 0 ? l11_latch(o, 0x001B39F0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B4810(L11 *o, uint32_t a0)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B4810) return l11_latch(o, 0x001B4810u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B4810(o->h->ctx, a0) < 0 ? l11_latch(o, 0x001B4810u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B4CF0(L11 *o, uint32_t a0)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B4CF0) return l11_latch(o, 0x001B4CF0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B4CF0(o->h->ctx, a0) < 0 ? l11_latch(o, 0x001B4CF0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001B5360(L11 *o, uint32_t a0)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001B5360) return l11_latch(o, 0x001B5360u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B5360(o->h->ctx, a0) < 0 ? l11_latch(o, 0x001B5360u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001BA1A0(L11 *o, uint32_t a0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return l11_latch(o, 0x001BA1A0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0 ? l11_latch(o, 0x001BA1A0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001BA1F0(L11 *o, uint32_t a0, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return l11_latch(o, 0x001BA1F0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, a0, result) < 0 ? l11_latch(o, 0x001BA1F0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001C6120(L11 *o, uint32_t a0, int32_t n1, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001C6120) return l11_latch(o, 0x001C6120u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6120(o->h->ctx, a0, n1, result) < 0 ? l11_latch(o, 0x001C6120u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001C6380(L11 *o, uint32_t a0)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001C6380) return l11_latch(o, 0x001C6380u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6380(o->h->ctx, a0) < 0 ? l11_latch(o, 0x001C6380u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001C63E0(L11 *o, uint32_t a0, int32_t n1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001C63E0) return l11_latch(o, 0x001C63E0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C63E0(o->h->ctx, a0, n1) < 0 ? l11_latch(o, 0x001C63E0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001C64F0(L11 *o, uint32_t a0, uint32_t f1, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001C64F0) return l11_latch(o, 0x001C64F0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C64F0(o->h->ctx, a0, l11_float(f1), result) < 0 ? l11_latch(o, 0x001C64F0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001C67E0(L11 *o, uint32_t a0, int32_t n1, uint32_t f2, uint32_t f3)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001C67E0) return l11_latch(o, 0x001C67E0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C67E0(o->h->ctx, a0, n1, l11_float(f2), l11_float(f3)) < 0 ? l11_latch(o, 0x001C67E0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001C68C0(L11 *o, uint32_t a0)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001C68C0) return l11_latch(o, 0x001C68C0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C68C0(o->h->ctx, a0) < 0 ? l11_latch(o, 0x001C68C0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001C7900(L11 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001C7900) return l11_latch(o, 0x001C7900u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C7900(o->h->ctx, a0, a1, n2, n3) < 0 ? l11_latch(o, 0x001C7900u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CA1C0(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CA1C0) return l11_latch(o, 0x001CA1C0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA1C0(o->h->ctx, a0, a1, a2) < 0 ? l11_latch(o, 0x001CA1C0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CA3B0(L11 *o, uint32_t a0, uint32_t f1, uint32_t f2, uint32_t f3)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CA3B0) return l11_latch(o, 0x001CA3B0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA3B0(o->h->ctx, a0, l11_float(f1), l11_float(f2), l11_float(f3)) < 0 ? l11_latch(o, 0x001CA3B0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CA4D0(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CA4D0) return l11_latch(o, 0x001CA4D0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA4D0(o->h->ctx, a0, a1, a2) < 0 ? l11_latch(o, 0x001CA4D0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CA7B0(L11 *o, uint32_t a0, uint32_t f1, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CA7B0) return l11_latch(o, 0x001CA7B0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA7B0(o->h->ctx, a0, l11_float(f1), result) < 0 ? l11_latch(o, 0x001CA7B0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CA940(L11 *o, int32_t n0, int32_t n1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CA940) return l11_latch(o, 0x001CA940u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA940(o->h->ctx, n0, n1) < 0 ? l11_latch(o, 0x001CA940u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CB5F0(L11 *o, uint32_t a0, int32_t n1, int32_t n2, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CB5F0) return l11_latch(o, 0x001CB5F0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CB5F0(o->h->ctx, a0, n1, n2, result) < 0 ? l11_latch(o, 0x001CB5F0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CB900(L11 *o, uint32_t a0, int32_t n1, int32_t n2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CB900) return l11_latch(o, 0x001CB900u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CB900(o->h->ctx, a0, n1, n2) < 0 ? l11_latch(o, 0x001CB900u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CB950(L11 *o, uint32_t a0, int32_t n1, uint64_t q2, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CB950) return l11_latch(o, 0x001CB950u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CB950(o->h->ctx, a0, n1, q2, result) < 0 ? l11_latch(o, 0x001CB950u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CCF70(L11 *o, uint32_t a0, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CCF70) return l11_latch(o, 0x001CCF70u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CCF70(o->h->ctx, a0, result) < 0 ? l11_latch(o, 0x001CCF70u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CD370(L11 *o, int32_t n0, uint32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CD370) return l11_latch(o, 0x001CD370u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CD370(o->h->ctx, n0, result) < 0 ? l11_latch(o, 0x001CD370u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CF470(L11 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CF470) return l11_latch(o, 0x001CF470u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CF470(o->h->ctx, a0, a1, result) < 0 ? l11_latch(o, 0x001CF470u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CFA60(L11 *o, uint32_t a0, uint32_t a1, uint32_t f2, uint32_t f3)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CFA60) return l11_latch(o, 0x001CFA60u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFA60(o->h->ctx, a0, a1, l11_float(f2), l11_float(f3)) < 0 ? l11_latch(o, 0x001CFA60u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CFB50(L11 *o, uint32_t a0, int32_t n1, uint32_t a2, uint32_t f3, uint32_t f4, uint32_t f5, uint32_t f6, uint32_t f7)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CFB50) return l11_latch(o, 0x001CFB50u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFB50(o->h->ctx, a0, n1, a2, l11_float(f3), l11_float(f4), l11_float(f5), l11_float(f6), l11_float(f7)) < 0 ? l11_latch(o, 0x001CFB50u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001CFBE0(L11 *o, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001CFBE0) return l11_latch(o, 0x001CFBE0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFBE0(o->h->ctx, n0, n1, a2, a3, n4) < 0 ? l11_latch(o, 0x001CFBE0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001D04B0(L11 *o, uint32_t a0, int32_t n1, uint32_t a2, uint32_t f3, uint32_t f4)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001D04B0) return l11_latch(o, 0x001D04B0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D04B0(o->h->ctx, a0, n1, a2, l11_float(f3), l11_float(f4)) < 0 ? l11_latch(o, 0x001D04B0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001EFD20(L11 *o, int32_t n0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001EFD20) return l11_latch(o, 0x001EFD20u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFD20(o->h->ctx, n0, a1) < 0 ? l11_latch(o, 0x001EFD20u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001EFE00(L11 *o, int32_t n0, uint32_t a1, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001EFE00) return l11_latch(o, 0x001EFE00u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFE00(o->h->ctx, n0, a1, result) < 0 ? l11_latch(o, 0x001EFE00u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001F02C0(L11 *o, uint32_t a0, int32_t n1, uint32_t f2)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001F02C0) return l11_latch(o, 0x001F02C0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F02C0(o->h->ctx, a0, n1, l11_float(f2)) < 0 ? l11_latch(o, 0x001F02C0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001F3E30(L11 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t n4)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001F3E30) return l11_latch(o, 0x001F3E30u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F3E30(o->h->ctx, a0, a1, n2, n3, n4) < 0 ? l11_latch(o, 0x001F3E30u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001F8D30(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t f4, uint32_t f5, uint32_t f6, uint32_t a7)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001F8D30) return l11_latch(o, 0x001F8D30u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F8D30(o->h->ctx, a0, a1, a2, a3, l11_float(f4), l11_float(f5), l11_float(f6), a7) < 0 ? l11_latch(o, 0x001F8D30u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001FA790(L11 *o, int32_t n0, int32_t n1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001FA790) return l11_latch(o, 0x001FA790u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FA790(o->h->ctx, n0, n1) < 0 ? l11_latch(o, 0x001FA790u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001FABB0(L11 *o)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001FABB0) return l11_latch(o, 0x001FABB0u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FABB0(o->h->ctx) < 0 ? l11_latch(o, 0x001FABB0u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001FAE70(L11 *o, int32_t n0)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001FAE70) return l11_latch(o, 0x001FAE70u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FAE70(o->h->ctx, n0) < 0 ? l11_latch(o, 0x001FAE70u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_001FBD50(L11 *o, uint32_t a0, int32_t n1, int32_t n2, uint32_t f3, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_001FBD50) return l11_latch(o, 0x001FBD50u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FBD50(o->h->ctx, a0, n1, n2, l11_float(f3), result) < 0 ? l11_latch(o, 0x001FBD50u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_0021C040(L11 *o, uint32_t a0, uint32_t a1)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_0021C040) return l11_latch(o, 0x0021C040u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_0021C040(o->h->ctx, a0, a1) < 0 ? l11_latch(o, 0x0021C040u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l11_c_00824060(L11 *o, int32_t *result)
{
    if (l11_failed(o)) return -1;
    if (!o->h->w_00824060) return l11_latch(o, 0x00824060u, EM_LEVEL11_PORT_FAULT_NULL_WORKER);
    return o->h->w_00824060(o->h->ctx, result) < 0 ? l11_latch(o, 0x00824060u, EM_LEVEL11_PORT_FAULT_WORKER_FAILED) : 0;
}
/* END GENERATED WRAPPERS */



/* Float constants (bit patterns, as the originals materialize them). */
#define F_ZERO 0x00000000u
#define F_ONE  0x3F800000u

/* Translations called by other translations (direct calls, no hook). */
int32_t l11_008240E0(L11 *o);
int32_t l11_008249F0(L11 *o, uint32_t self);
void l11_001F9140(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t f12, uint32_t sp);
int32_t l11_0019A6F0(L11 *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t t0, uint32_t sp);
int32_t l11_001B2E50(L11 *o, uint32_t a0, uint32_t out);
int32_t l11_001B3F10(L11 *o, uint32_t self, uint32_t bearing, uint32_t height, uint32_t sp);
void l11_001F4190(L11 *o, uint32_t self, uint32_t timing, uint32_t cfg);
void l11_00141F00(L11 *o, uint32_t e, uint32_t d);
void l11_00142070(L11 *o, uint32_t e, uint32_t d, uint32_t sp);
void l11_00142330(L11 *o, uint32_t e, uint32_t d, uint32_t sp);
void l11_001424C0(L11 *o, uint32_t e, uint32_t d, uint32_t sp);
void l11_001429D0(L11 *o, uint32_t e, uint32_t d, uint32_t sp);
void l11_00145880(L11 *o, uint32_t e, uint32_t d);
void l11_001459A0(L11 *o, uint32_t e, uint32_t d);
int32_t l11_00146110(L11 *o, uint32_t e, uint32_t d, uint32_t sp);
int32_t l11_001464B0(L11 *o, uint32_t e, uint32_t d);
void l11_001469B0(L11 *o, uint32_t e, uint32_t d, uint32_t sp);
int32_t l11_00146CE0(L11 *o, uint32_t e, uint32_t d);
void l11_001471E0(L11 *o, uint32_t e, uint32_t d);
void l11_001CDDC0(L11 *o, int32_t layer, int32_t mode, uint32_t corners, uint64_t tex0, uint32_t rgba, uint32_t sp);
void l11_001CE660(L11 *o, int32_t layer, int32_t tag, uint32_t mtx, uint32_t band, uint64_t tex0, uint32_t rgba,
                  uint32_t sp);

#endif /* EM_LEVEL11_PORT_INTERNAL_H */
