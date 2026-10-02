/* Shared plumbing of em_level13_port*.c: the memory view, fault latch, EE
 * float helpers, one typed wrapper per hook (written from one table, the
 * HOOKS list of tools/test_level13_port_reference.py, by a lane script that
 * is not committed; the test's header_checks compares this block and the
 * header's hook struct with HOOKS on every run), and the internal entry
 * points the translations call directly (the design of
 * em_level12_port_internal.h). Not a public interface.
 *
 * Floats inside the module are carried as their bit patterns (uint32_t);
 * the wrappers convert to and from the hook table's float parameters with
 * memcpy, so no value is ever converted or rounded on the way. */
#ifndef EM_LEVEL13_PORT_INTERNAL_H
#define EM_LEVEL13_PORT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level13_port.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel13PortHooks *h;
    EmLevel13PortFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L13;

static inline int l13_latch(L13 *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL13_PORT_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l13_failed(const L13 *o) { return o->fault->code != EM_LEVEL13_PORT_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l13_at(L13 *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l13_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l13_latch(o, address, EM_LEVEL13_PORT_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l13_u8(L13 *o, uint32_t a) { return *l13_at(o, a, 1); }
static inline uint32_t l13_u16(L13 *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l13_at(o, a, 2), 2);
    return v;
}
static inline int32_t l13_s16(L13 *o, uint32_t a) { return (int16_t)l13_u16(o, a); }
static inline uint32_t l13_u32(L13 *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l13_at(o, a, 4), 4);
    return v;
}
static inline int32_t l13_s32(L13 *o, uint32_t a) { return (int32_t)l13_u32(o, a); }
static inline int32_t l13_s8(L13 *o, uint32_t a) { return (int8_t)l13_u8(o, a); }
static inline uint64_t l13_u64(L13 *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, l13_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void l13_q(L13 *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, l13_at(o, a, 8), 8);
    memcpy(out + 8, l13_at(o, a + 8, 8), 8);
}
static inline void l13_wq(L13 *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(l13_at(o, a, 8), in, 8);
    memcpy(l13_at(o, a + 8, 8), in + 8, 8);
}

static inline void l13_w8(L13 *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l13_at(o, a, 1), &b, 1);
}
static inline void l13_w16(L13 *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l13_at(o, a, 2), &h, 2);
}
static inline void l13_w32(L13 *o, uint32_t a, uint32_t v) { memcpy(l13_at(o, a, 4), &v, 4); }
static inline void l13_w64(L13 *o, uint32_t a, uint64_t v) { memcpy(l13_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l13_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l13_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l13_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L13_ADD(a, b) em_ee_add_bits((a), (b))
#define L13_SUB(a, b) em_ee_sub_bits((a), (b))
#define L13_MUL(a, b) em_ee_mul_bits((a), (b))
#define L13_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L13_LE(a, b) em_ee_c_le_bits((a), (b))
#define L13_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L13_DIV(a, b) em_ee_div_bits((a), (b))
#define L13_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define L13_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define L13_MULA(a, b) em_ee_mula_bits((a), (b))
#define L13_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))

static inline int l13_begin(L13 *o, const EmLevel13PortHooks *h, EmLevel13PortFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return l13_failed(o) ? -1 : 0;
}

static inline int l13_end(const L13 *o) { return l13_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int l13_callback(L13 *o, uint32_t fn, uint32_t a0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_callback) return l13_latch(o, fn, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? l13_latch(o, fn, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int l13_method(L13 *o, uint32_t self)
{
    uint32_t fn = l13_u32(o, self + 0x4C);
    return l13_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Float arguments and results are bit patterns. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l13_c_001026A0(L13 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001026A0) return l13_latch(o, 0x001026A0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0 ? l13_latch(o, 0x001026A0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_00102760(L13 *o, uint32_t a0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_00102760) return l13_latch(o, 0x00102760u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102760(o->h->ctx, a0, a1) < 0 ? l13_latch(o, 0x00102760u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001028B8(L13 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001028B8) return l13_latch(o, 0x001028B8u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0 ? l13_latch(o, 0x001028B8u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_00102918(L13 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_00102918) return l13_latch(o, 0x00102918u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102918(o->h->ctx, a0, a1, a2) < 0 ? l13_latch(o, 0x00102918u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_00102948(L13 *o, uint32_t a0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_00102948) return l13_latch(o, 0x00102948u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, a0, a1) < 0 ? l13_latch(o, 0x00102948u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_00102958(L13 *o, uint32_t a0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_00102958) return l13_latch(o, 0x00102958u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102958(o->h->ctx, a0, a1) < 0 ? l13_latch(o, 0x00102958u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001029C0(L13 *o, uint32_t a0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001029C0) return l13_latch(o, 0x001029C0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001029C0(o->h->ctx, a0) < 0 ? l13_latch(o, 0x001029C0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_00102B08(L13 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_00102B08) return l13_latch(o, 0x00102B08u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102B08(o->h->ctx, a0, a1, l13_float(f2)) < 0 ? l13_latch(o, 0x00102B08u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_00102BB0(L13 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_00102BB0) return l13_latch(o, 0x00102BB0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102BB0(o->h->ctx, a0, a1, l13_float(f2)) < 0 ? l13_latch(o, 0x00102BB0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_00102C58(L13 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_00102C58) return l13_latch(o, 0x00102C58u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102C58(o->h->ctx, a0, a1, a2) < 0 ? l13_latch(o, 0x00102C58u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_00103230(L13 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_00103230) return l13_latch(o, 0x00103230u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_00103230(o->h->ctx, a0, a1, l13_float(f2)) < 0 ? l13_latch(o, 0x00103230u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0011DE90(L13 *o, uint32_t f0, uint32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0011DE90) return l13_latch(o, 0x0011DE90u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011DE90(o->h->ctx, l13_float(f0), &out) < 0) return l13_latch(o, 0x0011DE90u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED);
    *result = l13_bits(out);
    return 0;
}

static inline int l13_c_0011DF78(L13 *o, uint32_t f0, uint32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0011DF78) return l13_latch(o, 0x0011DF78u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011DF78(o->h->ctx, l13_float(f0), &out) < 0) return l13_latch(o, 0x0011DF78u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED);
    *result = l13_bits(out);
    return 0;
}

static inline int l13_c_0011E2A8(L13 *o, uint32_t f0, uint32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return l13_latch(o, 0x0011E2A8u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_0011E2A8(o->h->ctx, l13_float(f0), &out) < 0) return l13_latch(o, 0x0011E2A8u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED);
    *result = l13_bits(out);
    return 0;
}

static inline int l13_c_00122BB8(L13 *o, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_00122BB8) return l13_latch(o, 0x00122BB8u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? l13_latch(o, 0x00122BB8u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001281C0(L13 *o, uint32_t f0, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001281C0) return l13_latch(o, 0x001281C0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001281C0(o->h->ctx, l13_float(f0), result) < 0 ? l13_latch(o, 0x001281C0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0013BA20(L13 *o, uint32_t a0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0013BA20) return l13_latch(o, 0x0013BA20u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_0013BA20(o->h->ctx, a0, a1) < 0 ? l13_latch(o, 0x0013BA20u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0013BBB0(L13 *o, uint32_t a0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0013BBB0) return l13_latch(o, 0x0013BBB0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_0013BBB0(o->h->ctx, a0, a1) < 0 ? l13_latch(o, 0x0013BBB0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0013C8C0(L13 *o, uint32_t a0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0013C8C0) return l13_latch(o, 0x0013C8C0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_0013C8C0(o->h->ctx, a0, a1) < 0 ? l13_latch(o, 0x0013C8C0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0018D7B0(L13 *o, uint32_t a0, int32_t n1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0018D7B0) return l13_latch(o, 0x0018D7B0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018D7B0(o->h->ctx, a0, n1) < 0 ? l13_latch(o, 0x0018D7B0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001916C0(L13 *o, uint32_t a0, uint32_t a1, int32_t n2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001916C0) return l13_latch(o, 0x001916C0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001916C0(o->h->ctx, a0, a1, n2) < 0 ? l13_latch(o, 0x001916C0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0019A570(L13 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0019A570) return l13_latch(o, 0x0019A570u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019A570(o->h->ctx, a0, a1, n2, n3, result) < 0 ? l13_latch(o, 0x0019A570u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0019AD00(L13 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0019AD00) return l13_latch(o, 0x0019AD00u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019AD00(o->h->ctx, a0, a1, n2, result) < 0 ? l13_latch(o, 0x0019AD00u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0019C6F0(L13 *o, int32_t n0, int32_t n1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0019C6F0) return l13_latch(o, 0x0019C6F0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019C6F0(o->h->ctx, n0, n1) < 0 ? l13_latch(o, 0x0019C6F0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001A2370(L13 *o, uint32_t a0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001A2370) return l13_latch(o, 0x001A2370u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001A2370(o->h->ctx, a0, a1) < 0 ? l13_latch(o, 0x001A2370u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001A7B80(L13 *o, uint32_t a0, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001A7B80) return l13_latch(o, 0x001A7B80u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001A7B80(o->h->ctx, a0, result) < 0 ? l13_latch(o, 0x001A7B80u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001AFA90(L13 *o, int32_t n0, uint32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001AFA90) return l13_latch(o, 0x001AFA90u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFA90(o->h->ctx, n0, result) < 0 ? l13_latch(o, 0x001AFA90u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001AFC10(L13 *o, uint32_t a0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001AFC10) return l13_latch(o, 0x001AFC10u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, a0) < 0 ? l13_latch(o, 0x001AFC10u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001B0FD0(L13 *o, uint32_t a0, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return l13_latch(o, 0x001B0FD0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0FD0(o->h->ctx, a0, result) < 0 ? l13_latch(o, 0x001B0FD0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001B10B0(L13 *o, uint32_t a0, int32_t n1, int32_t n2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B10B0) return l13_latch(o, 0x001B10B0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B10B0(o->h->ctx, a0, n1, n2) < 0 ? l13_latch(o, 0x001B10B0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001B1240(L13 *o, uint32_t a0, uint32_t f1, uint32_t f2, uint32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B1240) return l13_latch(o, 0x001B1240u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_001B1240(o->h->ctx, a0, l13_float(f1), l13_float(f2), &out) < 0) return l13_latch(o, 0x001B1240u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED);
    *result = l13_bits(out);
    return 0;
}

static inline int l13_c_001B1270(L13 *o, uint32_t a0, uint32_t f1, uint32_t f2, uint32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B1270) return l13_latch(o, 0x001B1270u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_001B1270(o->h->ctx, a0, l13_float(f1), l13_float(f2), &out) < 0) return l13_latch(o, 0x001B1270u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED);
    *result = l13_bits(out);
    return 0;
}

static inline int l13_c_001B12B0(L13 *o, uint32_t f0, uint32_t f1, uint32_t f2, uint32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B12B0) return l13_latch(o, 0x001B12B0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_001B12B0(o->h->ctx, l13_float(f0), l13_float(f1), l13_float(f2), &out) < 0) return l13_latch(o, 0x001B12B0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED);
    *result = l13_bits(out);
    return 0;
}

static inline int l13_c_001B1470(L13 *o, uint32_t f0, uint32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B1470) return l13_latch(o, 0x001B1470u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_001B1470(o->h->ctx, l13_float(f0), &out) < 0) return l13_latch(o, 0x001B1470u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED);
    *result = l13_bits(out);
    return 0;
}

static inline int l13_c_001B1560(L13 *o, uint32_t a0, uint32_t a1, uint32_t f2, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B1560) return l13_latch(o, 0x001B1560u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1560(o->h->ctx, a0, a1, l13_float(f2), result) < 0 ? l13_latch(o, 0x001B1560u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001B15D0(L13 *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B15D0) return l13_latch(o, 0x001B15D0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    float out = 0.0f;
    if (o->h->w_001B15D0(o->h->ctx, a0, a1, &out) < 0) return l13_latch(o, 0x001B15D0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED);
    *result = l13_bits(out);
    return 0;
}

static inline int l13_c_001B17A0(L13 *o, uint32_t a0, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B17A0) return l13_latch(o, 0x001B17A0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B17A0(o->h->ctx, a0, result) < 0 ? l13_latch(o, 0x001B17A0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001B1B70(L13 *o, uint32_t a0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B1B70) return l13_latch(o, 0x001B1B70u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1B70(o->h->ctx, a0) < 0 ? l13_latch(o, 0x001B1B70u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001B1E20(L13 *o, int32_t n0, int32_t n1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B1E20) return l13_latch(o, 0x001B1E20u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1E20(o->h->ctx, n0, n1) < 0 ? l13_latch(o, 0x001B1E20u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001B1EA0(L13 *o, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return l13_latch(o, 0x001B1EA0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1EA0(o->h->ctx, n0, a1, a2, n3, result) < 0 ? l13_latch(o, 0x001B1EA0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001B2B10(L13 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B2B10) return l13_latch(o, 0x001B2B10u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2B10(o->h->ctx, a0, a1, a2) < 0 ? l13_latch(o, 0x001B2B10u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001B2D00(L13 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B2D00) return l13_latch(o, 0x001B2D00u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2D00(o->h->ctx, a0, a1, result) < 0 ? l13_latch(o, 0x001B2D00u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001B2F70(L13 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B2F70) return l13_latch(o, 0x001B2F70u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2F70(o->h->ctx, a0, a1, result) < 0 ? l13_latch(o, 0x001B2F70u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001B6660(L13 *o, uint32_t a0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B6660) return l13_latch(o, 0x001B6660u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B6660(o->h->ctx, a0) < 0 ? l13_latch(o, 0x001B6660u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001B6F00(L13 *o, uint32_t a0, uint32_t a1, uint32_t f2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001B6F00) return l13_latch(o, 0x001B6F00u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B6F00(o->h->ctx, a0, a1, l13_float(f2)) < 0 ? l13_latch(o, 0x001B6F00u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001BA1A0(L13 *o, uint32_t a0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return l13_latch(o, 0x001BA1A0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0 ? l13_latch(o, 0x001BA1A0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001BA1C0(L13 *o, uint32_t a0, int32_t n1, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return l13_latch(o, 0x001BA1C0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1C0(o->h->ctx, a0, n1, result) < 0 ? l13_latch(o, 0x001BA1C0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001BA1F0(L13 *o, uint32_t a0, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return l13_latch(o, 0x001BA1F0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, a0, result) < 0 ? l13_latch(o, 0x001BA1F0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001C4760(L13 *o, int32_t n0, int32_t n1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001C4760) return l13_latch(o, 0x001C4760u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C4760(o->h->ctx, n0, n1) < 0 ? l13_latch(o, 0x001C4760u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001C47A0(L13 *o, int32_t n0, int32_t n1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001C47A0) return l13_latch(o, 0x001C47A0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C47A0(o->h->ctx, n0, n1) < 0 ? l13_latch(o, 0x001C47A0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001C47E0(L13 *o, int32_t n0, int32_t n1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001C47E0) return l13_latch(o, 0x001C47E0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C47E0(o->h->ctx, n0, n1) < 0 ? l13_latch(o, 0x001C47E0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001C6380(L13 *o, uint32_t a0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001C6380) return l13_latch(o, 0x001C6380u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6380(o->h->ctx, a0) < 0 ? l13_latch(o, 0x001C6380u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001C63E0(L13 *o, uint32_t a0, int32_t n1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001C63E0) return l13_latch(o, 0x001C63E0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C63E0(o->h->ctx, a0, n1) < 0 ? l13_latch(o, 0x001C63E0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001C64F0(L13 *o, uint32_t a0, uint32_t f1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001C64F0) return l13_latch(o, 0x001C64F0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C64F0(o->h->ctx, a0, l13_float(f1)) < 0 ? l13_latch(o, 0x001C64F0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001C67E0(L13 *o, uint32_t a0, int32_t n1, uint32_t f2, uint32_t f3)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001C67E0) return l13_latch(o, 0x001C67E0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C67E0(o->h->ctx, a0, n1, l13_float(f2), l13_float(f3)) < 0 ? l13_latch(o, 0x001C67E0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001C68C0(L13 *o, uint32_t a0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001C68C0) return l13_latch(o, 0x001C68C0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C68C0(o->h->ctx, a0) < 0 ? l13_latch(o, 0x001C68C0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001C7420(L13 *o, uint32_t a0, int32_t n1, int32_t n2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001C7420) return l13_latch(o, 0x001C7420u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C7420(o->h->ctx, a0, n1, n2) < 0 ? l13_latch(o, 0x001C7420u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001CA770(L13 *o, uint32_t a0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001CA770) return l13_latch(o, 0x001CA770u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA770(o->h->ctx, a0) < 0 ? l13_latch(o, 0x001CA770u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001CAAC0(L13 *o, uint32_t a0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001CAAC0) return l13_latch(o, 0x001CAAC0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CAAC0(o->h->ctx, a0, a1) < 0 ? l13_latch(o, 0x001CAAC0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001CCF70(L13 *o, uint32_t a0, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001CCF70) return l13_latch(o, 0x001CCF70u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CCF70(o->h->ctx, a0, result) < 0 ? l13_latch(o, 0x001CCF70u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001CFA60(L13 *o, uint32_t a0, uint32_t a1, uint32_t f2, uint32_t f3)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001CFA60) return l13_latch(o, 0x001CFA60u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFA60(o->h->ctx, a0, a1, l13_float(f2), l13_float(f3)) < 0 ? l13_latch(o, 0x001CFA60u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001CFB50(L13 *o, uint32_t a0, int32_t n1, uint32_t a2, uint32_t f3, uint32_t f4, uint32_t f5, uint32_t f6, uint32_t f7)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001CFB50) return l13_latch(o, 0x001CFB50u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFB50(o->h->ctx, a0, n1, a2, l13_float(f3), l13_float(f4), l13_float(f5), l13_float(f6), l13_float(f7)) < 0 ? l13_latch(o, 0x001CFB50u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001CFBE0(L13 *o, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001CFBE0) return l13_latch(o, 0x001CFBE0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CFBE0(o->h->ctx, n0, n1, a2, a3, n4) < 0 ? l13_latch(o, 0x001CFBE0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001D1F80(L13 *o, int32_t n0, int32_t n1, int32_t n2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001D1F80) return l13_latch(o, 0x001D1F80u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D1F80(o->h->ctx, n0, n1, n2) < 0 ? l13_latch(o, 0x001D1F80u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001D1FF0(L13 *o, int32_t n0, int32_t n1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001D1FF0) return l13_latch(o, 0x001D1FF0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D1FF0(o->h->ctx, n0, n1) < 0 ? l13_latch(o, 0x001D1FF0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001D2090(L13 *o, int32_t n0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001D2090) return l13_latch(o, 0x001D2090u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D2090(o->h->ctx, n0, a1) < 0 ? l13_latch(o, 0x001D2090u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001D2910(L13 *o, int32_t n0, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001D2910) return l13_latch(o, 0x001D2910u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D2910(o->h->ctx, n0, result) < 0 ? l13_latch(o, 0x001D2910u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001D6F60(L13 *o, int32_t n0, uint64_t q1, int32_t n2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001D6F60) return l13_latch(o, 0x001D6F60u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D6F60(o->h->ctx, n0, q1, n2) < 0 ? l13_latch(o, 0x001D6F60u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001D8C20(L13 *o, int32_t n0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001D8C20) return l13_latch(o, 0x001D8C20u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D8C20(o->h->ctx, n0) < 0 ? l13_latch(o, 0x001D8C20u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001EFD90(L13 *o, int32_t n0, uint32_t a1, uint32_t a2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001EFD90) return l13_latch(o, 0x001EFD90u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFD90(o->h->ctx, n0, a1, a2) < 0 ? l13_latch(o, 0x001EFD90u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001EFEB0(L13 *o, int32_t n0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001EFEB0) return l13_latch(o, 0x001EFEB0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFEB0(o->h->ctx, n0, a1) < 0 ? l13_latch(o, 0x001EFEB0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001F02C0(L13 *o, uint32_t a0, int32_t n1, uint32_t f2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001F02C0) return l13_latch(o, 0x001F02C0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F02C0(o->h->ctx, a0, n1, l13_float(f2)) < 0 ? l13_latch(o, 0x001F02C0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001F0460(L13 *o, int32_t n0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001F0460) return l13_latch(o, 0x001F0460u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F0460(o->h->ctx, n0, a1) < 0 ? l13_latch(o, 0x001F0460u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001F1110(L13 *o, uint32_t a0, int32_t n1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001F1110) return l13_latch(o, 0x001F1110u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F1110(o->h->ctx, a0, n1) < 0 ? l13_latch(o, 0x001F1110u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001F1180(L13 *o, uint32_t a0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001F1180) return l13_latch(o, 0x001F1180u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F1180(o->h->ctx, a0) < 0 ? l13_latch(o, 0x001F1180u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001F4BF0(L13 *o, uint32_t a0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001F4BF0) return l13_latch(o, 0x001F4BF0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F4BF0(o->h->ctx, a0, a1) < 0 ? l13_latch(o, 0x001F4BF0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001F5940(L13 *o, int32_t n0, uint32_t a1, int32_t n2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001F5940) return l13_latch(o, 0x001F5940u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F5940(o->h->ctx, n0, a1, n2) < 0 ? l13_latch(o, 0x001F5940u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001F6640(L13 *o, uint32_t a0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001F6640) return l13_latch(o, 0x001F6640u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F6640(o->h->ctx, a0) < 0 ? l13_latch(o, 0x001F6640u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001F6BA0(L13 *o)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001F6BA0) return l13_latch(o, 0x001F6BA0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F6BA0(o->h->ctx) < 0 ? l13_latch(o, 0x001F6BA0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001FB0B0(L13 *o, int32_t n0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001FB0B0) return l13_latch(o, 0x001FB0B0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FB0B0(o->h->ctx, n0) < 0 ? l13_latch(o, 0x001FB0B0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001FBD50(L13 *o, uint32_t a0, int32_t n1, int32_t n2, uint32_t f3)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001FBD50) return l13_latch(o, 0x001FBD50u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FBD50(o->h->ctx, a0, n1, n2, l13_float(f3)) < 0 ? l13_latch(o, 0x001FBD50u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001FC3C0(L13 *o, uint32_t a0, uint32_t a1, int32_t n2, uint32_t f3, uint32_t f4)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001FC3C0) return l13_latch(o, 0x001FC3C0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FC3C0(o->h->ctx, a0, a1, n2, l13_float(f3), l13_float(f4)) < 0 ? l13_latch(o, 0x001FC3C0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_001FC520(L13 *o, uint32_t a0)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_001FC520) return l13_latch(o, 0x001FC520u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FC520(o->h->ctx, a0) < 0 ? l13_latch(o, 0x001FC520u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0021B9A0(L13 *o, int32_t n0, uint32_t f1, uint32_t f2)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0021B9A0) return l13_latch(o, 0x0021B9A0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_0021B9A0(o->h->ctx, n0, l13_float(f1), l13_float(f2)) < 0 ? l13_latch(o, 0x0021B9A0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0021BE40(L13 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0021BE40) return l13_latch(o, 0x0021BE40u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_0021BE40(o->h->ctx, a0, a1, result) < 0 ? l13_latch(o, 0x0021BE40u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0021BED0(L13 *o, uint32_t a0, int32_t *result)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0021BED0) return l13_latch(o, 0x0021BED0u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_0021BED0(o->h->ctx, a0, result) < 0 ? l13_latch(o, 0x0021BED0u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0021BF90(L13 *o, uint32_t a0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0021BF90) return l13_latch(o, 0x0021BF90u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_0021BF90(o->h->ctx, a0, a1) < 0 ? l13_latch(o, 0x0021BF90u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l13_c_0021C040(L13 *o, uint32_t a0, uint32_t a1)
{
    if (l13_failed(o)) return -1;
    if (!o->h->w_0021C040) return l13_latch(o, 0x0021C040u, EM_LEVEL13_PORT_FAULT_NULL_WORKER);
    return o->h->w_0021C040(o->h->ctx, a0, a1) < 0 ? l13_latch(o, 0x0021C040u, EM_LEVEL13_PORT_FAULT_WORKER_FAILED) : 0;
}
/* END GENERATED WRAPPERS */

/* Translations called by other translations (direct calls, no hook). */
int32_t l13_001B2B80(L13 *o, uint32_t a0, uint32_t a1, uint32_t a2);
int32_t l13_001B3250(L13 *o, uint32_t self, uint32_t point, uint32_t lim, uint32_t sp);
int32_t l13_001B34F0(L13 *o, uint32_t a0, uint32_t a1, uint32_t a2);
void l13_001D42E0(L13 *o, int32_t ch, uint32_t model);
void l13_001D4430(L13 *o, uint32_t model, uint32_t a1);
void l13_001CB140(L13 *o, uint32_t a0, uint32_t a1);
void l13_001F6B90(L13 *o);

#endif /* EM_LEVEL13_PORT_INTERNAL_H */
