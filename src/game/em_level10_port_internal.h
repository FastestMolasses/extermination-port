/* Shared plumbing of em_level10_port*.c: the memory view, fault latch, EE
 * float helpers, one typed wrapper per hook (written from one table, the
 * HOOKS list of tools/test_level10_port_reference.py, by a lane script that
 * is not committed; the test's header_checks compares this block and the
 * header's hook struct with HOOKS on every run), and the internal entry
 * points the translations call directly (the design of
 * em_level9_port_internal.h). Not a public interface.
 *
 * Floats inside the module are carried as their bit patterns (uint32_t);
 * the wrappers convert to and from the hook table's float parameters with
 * memcpy, so no value is ever converted or rounded on the way. */
#ifndef EM_LEVEL10_PORT_INTERNAL_H
#define EM_LEVEL10_PORT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level10_port.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel10PortHooks *h;
    EmLevel10PortFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L10;

static inline int l10_latch(L10 *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL10_PORT_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l10_failed(const L10 *o) { return o->fault->code != EM_LEVEL10_PORT_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l10_at(L10 *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l10_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l10_latch(o, address, EM_LEVEL10_PORT_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l10_u8(L10 *o, uint32_t a) { return *l10_at(o, a, 1); }
static inline uint32_t l10_u16(L10 *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l10_at(o, a, 2), 2);
    return v;
}
static inline int32_t l10_s16(L10 *o, uint32_t a) { return (int16_t)l10_u16(o, a); }
static inline uint32_t l10_u32(L10 *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l10_at(o, a, 4), 4);
    return v;
}
static inline int32_t l10_s32(L10 *o, uint32_t a) { return (int32_t)l10_u32(o, a); }

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void l10_q(L10 *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, l10_at(o, a, 8), 8);
    memcpy(out + 8, l10_at(o, a + 8, 8), 8);
}
static inline void l10_wq(L10 *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(l10_at(o, a, 8), in, 8);
    memcpy(l10_at(o, a + 8, 8), in + 8, 8);
}

static inline void l10_w8(L10 *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l10_at(o, a, 1), &b, 1);
}
static inline void l10_w16(L10 *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l10_at(o, a, 2), &h, 2);
}
static inline void l10_w32(L10 *o, uint32_t a, uint32_t v) { memcpy(l10_at(o, a, 4), &v, 4); }
static inline void l10_w64(L10 *o, uint32_t a, uint64_t v) { memcpy(l10_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l10_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l10_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l10_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L10_ADD(a, b) em_ee_add_bits((a), (b))
#define L10_SUB(a, b) em_ee_sub_bits((a), (b))
#define L10_MUL(a, b) em_ee_mul_bits((a), (b))
#define L10_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L10_LE(a, b) em_ee_c_le_bits((a), (b))

static inline int l10_begin(L10 *o, const EmLevel10PortHooks *h, EmLevel10PortFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return l10_failed(o) ? -1 : 0;
}

static inline int l10_end(const L10 *o) { return l10_failed(o) ? -1 : 0; }

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int l10_method(L10 *o, uint32_t self)
{
    uint32_t fn = l10_u32(o, self + 0x4C);
    if (l10_failed(o)) return -1;
    if (!o->h->w_callback) return l10_latch(o, fn, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, self) < 0 ? l10_latch(o, fn, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Float arguments and results are bit patterns. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l10_c_001026A0(L10 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001026A0) return l10_latch(o, 0x001026A0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0 ? l10_latch(o, 0x001026A0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_00102738(L10 *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_00102738) return l10_latch(o, 0x00102738u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_00102738(o->h->ctx, a0, a1, &r) < 0) return l10_latch(o, 0x00102738u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED);
    *result = l10_bits(r);
    return 0;
}

static inline int l10_c_001028B8(L10 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001028B8) return l10_latch(o, 0x001028B8u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0 ? l10_latch(o, 0x001028B8u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001028D0(L10 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001028D0) return l10_latch(o, 0x001028D0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0 ? l10_latch(o, 0x001028D0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_00102948(L10 *o, uint32_t a0, uint32_t a1)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_00102948) return l10_latch(o, 0x00102948u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, a0, a1) < 0 ? l10_latch(o, 0x00102948u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001031E0(L10 *o, uint32_t a0, uint32_t a1)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001031E0) return l10_latch(o, 0x001031E0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001031E0(o->h->ctx, a0, a1) < 0 ? l10_latch(o, 0x001031E0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_00122BB8(L10 *o, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_00122BB8) return l10_latch(o, 0x00122BB8u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? l10_latch(o, 0x00122BB8u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_00131F20(L10 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_00131F20) return l10_latch(o, 0x00131F20u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_00131F20(o->h->ctx, a0, a1, a2) < 0 ? l10_latch(o, 0x00131F20u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_00132490(L10 *o, uint32_t a0, uint32_t a1)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_00132490) return l10_latch(o, 0x00132490u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_00132490(o->h->ctx, a0, a1) < 0 ? l10_latch(o, 0x00132490u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001662D0(L10 *o, uint32_t a0)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001662D0) return l10_latch(o, 0x001662D0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001662D0(o->h->ctx, a0) < 0 ? l10_latch(o, 0x001662D0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001831F0(L10 *o, int32_t n0)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001831F0) return l10_latch(o, 0x001831F0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001831F0(o->h->ctx, n0) < 0 ? l10_latch(o, 0x001831F0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_0019A570(L10 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_0019A570) return l10_latch(o, 0x0019A570u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019A570(o->h->ctx, a0, a1, n2, n3, result) < 0 ? l10_latch(o, 0x0019A570u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_0019AA80(L10 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_0019AA80) return l10_latch(o, 0x0019AA80u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019AA80(o->h->ctx, a0, a1, n2, result) < 0 ? l10_latch(o, 0x0019AA80u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_0019C6F0(L10 *o, int32_t n0, int32_t n1, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_0019C6F0) return l10_latch(o, 0x0019C6F0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_0019C6F0(o->h->ctx, n0, n1, result) < 0 ? l10_latch(o, 0x0019C6F0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001A7B80(L10 *o, uint32_t a0, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001A7B80) return l10_latch(o, 0x001A7B80u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001A7B80(o->h->ctx, a0, result) < 0 ? l10_latch(o, 0x001A7B80u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001AEE10(L10 *o, int32_t n0, int32_t n1)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001AEE10) return l10_latch(o, 0x001AEE10u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AEE10(o->h->ctx, n0, n1) < 0 ? l10_latch(o, 0x001AEE10u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001AFC10(L10 *o, uint32_t a0)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001AFC10) return l10_latch(o, 0x001AFC10u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, a0) < 0 ? l10_latch(o, 0x001AFC10u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001B0F60(L10 *o, uint32_t a0, int32_t n1, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B0F60) return l10_latch(o, 0x001B0F60u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0F60(o->h->ctx, a0, n1, result) < 0 ? l10_latch(o, 0x001B0F60u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001B0FD0(L10 *o, uint32_t a0, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return l10_latch(o, 0x001B0FD0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0FD0(o->h->ctx, a0, result) < 0 ? l10_latch(o, 0x001B0FD0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001B10B0(L10 *o, uint32_t a0, int32_t n1, int32_t n2, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B10B0) return l10_latch(o, 0x001B10B0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B10B0(o->h->ctx, a0, n1, n2, result) < 0 ? l10_latch(o, 0x001B10B0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001B1240(L10 *o, uint32_t a0, uint32_t f1, uint32_t f2, uint32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B1240) return l10_latch(o, 0x001B1240u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_001B1240(o->h->ctx, a0, l10_float(f1), l10_float(f2), &r) < 0) return l10_latch(o, 0x001B1240u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED);
    *result = l10_bits(r);
    return 0;
}

static inline int l10_c_001B12B0(L10 *o, uint32_t f0, uint32_t f1, uint32_t f2, uint32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B12B0) return l10_latch(o, 0x001B12B0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    float r = 0.0f;
    if (o->h->w_001B12B0(o->h->ctx, l10_float(f0), l10_float(f1), l10_float(f2), &r) < 0) return l10_latch(o, 0x001B12B0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED);
    *result = l10_bits(r);
    return 0;
}

static inline int l10_c_001B1560(L10 *o, uint32_t a0, uint32_t a1, uint32_t f2, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B1560) return l10_latch(o, 0x001B1560u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1560(o->h->ctx, a0, a1, l10_float(f2), result) < 0 ? l10_latch(o, 0x001B1560u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001B17A0(L10 *o, uint32_t a0, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B17A0) return l10_latch(o, 0x001B17A0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B17A0(o->h->ctx, a0, result) < 0 ? l10_latch(o, 0x001B17A0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001B1E20(L10 *o, int32_t n0, int32_t n1)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B1E20) return l10_latch(o, 0x001B1E20u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1E20(o->h->ctx, n0, n1) < 0 ? l10_latch(o, 0x001B1E20u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001B1EA0(L10 *o, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return l10_latch(o, 0x001B1EA0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1EA0(o->h->ctx, n0, a1, a2, n3, result) < 0 ? l10_latch(o, 0x001B1EA0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001B2B10(L10 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B2B10) return l10_latch(o, 0x001B2B10u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B2B10(o->h->ctx, a0, a1, a2) < 0 ? l10_latch(o, 0x001B2B10u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001B3250(L10 *o, uint32_t a0, uint32_t a1, uint32_t f2, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B3250) return l10_latch(o, 0x001B3250u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B3250(o->h->ctx, a0, a1, l10_float(f2), result) < 0 ? l10_latch(o, 0x001B3250u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001B55E0(L10 *o, uint32_t a0, int32_t n1)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B55E0) return l10_latch(o, 0x001B55E0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B55E0(o->h->ctx, a0, n1) < 0 ? l10_latch(o, 0x001B55E0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001B6660(L10 *o, uint32_t a0, uint32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001B6660) return l10_latch(o, 0x001B6660u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B6660(o->h->ctx, a0, result) < 0 ? l10_latch(o, 0x001B6660u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001BA1A0(L10 *o, uint32_t a0, uint32_t a1)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return l10_latch(o, 0x001BA1A0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0 ? l10_latch(o, 0x001BA1A0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001BA1C0(L10 *o, uint32_t a0, int32_t n1, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return l10_latch(o, 0x001BA1C0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1C0(o->h->ctx, a0, n1, result) < 0 ? l10_latch(o, 0x001BA1C0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001BA1F0(L10 *o, uint32_t a0, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return l10_latch(o, 0x001BA1F0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, a0, result) < 0 ? l10_latch(o, 0x001BA1F0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001BBDA0(L10 *o, uint32_t a0)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001BBDA0) return l10_latch(o, 0x001BBDA0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BBDA0(o->h->ctx, a0) < 0 ? l10_latch(o, 0x001BBDA0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001BBE40(L10 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001BBE40) return l10_latch(o, 0x001BBE40u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BBE40(o->h->ctx, a0, a1, n2, result) < 0 ? l10_latch(o, 0x001BBE40u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001BC0E0(L10 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001BC0E0) return l10_latch(o, 0x001BC0E0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BC0E0(o->h->ctx, a0, a1, result) < 0 ? l10_latch(o, 0x001BC0E0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001BC240(L10 *o, uint32_t a0, uint32_t a1)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001BC240) return l10_latch(o, 0x001BC240u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BC240(o->h->ctx, a0, a1) < 0 ? l10_latch(o, 0x001BC240u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001BC290(L10 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001BC290) return l10_latch(o, 0x001BC290u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BC290(o->h->ctx, a0, a1, result) < 0 ? l10_latch(o, 0x001BC290u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001BC300(L10 *o, uint32_t a0)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001BC300) return l10_latch(o, 0x001BC300u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BC300(o->h->ctx, a0) < 0 ? l10_latch(o, 0x001BC300u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001C4760(L10 *o, int32_t n0, int32_t n1, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001C4760) return l10_latch(o, 0x001C4760u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C4760(o->h->ctx, n0, n1, result) < 0 ? l10_latch(o, 0x001C4760u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001C5570(L10 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, uint32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001C5570) return l10_latch(o, 0x001C5570u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C5570(o->h->ctx, a0, a1, n2, n3, result) < 0 ? l10_latch(o, 0x001C5570u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001C6120(L10 *o, uint32_t a0, int32_t n1, uint32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001C6120) return l10_latch(o, 0x001C6120u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6120(o->h->ctx, a0, n1, result) < 0 ? l10_latch(o, 0x001C6120u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001C62C0(L10 *o, uint32_t a0)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001C62C0) return l10_latch(o, 0x001C62C0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C62C0(o->h->ctx, a0) < 0 ? l10_latch(o, 0x001C62C0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001C6380(L10 *o, uint32_t a0)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001C6380) return l10_latch(o, 0x001C6380u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6380(o->h->ctx, a0) < 0 ? l10_latch(o, 0x001C6380u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001C63E0(L10 *o, uint32_t a0, int32_t n1)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001C63E0) return l10_latch(o, 0x001C63E0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C63E0(o->h->ctx, a0, n1) < 0 ? l10_latch(o, 0x001C63E0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001C64F0(L10 *o, uint32_t a0, uint32_t f1, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001C64F0) return l10_latch(o, 0x001C64F0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C64F0(o->h->ctx, a0, l10_float(f1), result) < 0 ? l10_latch(o, 0x001C64F0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001C67E0(L10 *o, uint32_t a0, int32_t n1, uint32_t f2, uint32_t f3)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001C67E0) return l10_latch(o, 0x001C67E0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C67E0(o->h->ctx, a0, n1, l10_float(f2), l10_float(f3)) < 0 ? l10_latch(o, 0x001C67E0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001C68C0(L10 *o, uint32_t a0)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001C68C0) return l10_latch(o, 0x001C68C0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C68C0(o->h->ctx, a0) < 0 ? l10_latch(o, 0x001C68C0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001CA6E0(L10 *o, uint32_t a0, uint32_t a1)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001CA6E0) return l10_latch(o, 0x001CA6E0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA6E0(o->h->ctx, a0, a1) < 0 ? l10_latch(o, 0x001CA6E0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001CA6F0(L10 *o, uint32_t a0, int32_t n1)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001CA6F0) return l10_latch(o, 0x001CA6F0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA6F0(o->h->ctx, a0, n1) < 0 ? l10_latch(o, 0x001CA6F0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001CD520(L10 *o, int32_t n0, int32_t n1, uint32_t a2, uint64_t q3, uint32_t a4, uint32_t f5, uint32_t f6, uint32_t f7, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001CD520) return l10_latch(o, 0x001CD520u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CD520(o->h->ctx, n0, n1, a2, q3, a4, l10_float(f5), l10_float(f6), l10_float(f7), result) < 0 ? l10_latch(o, 0x001CD520u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001E2BA0(L10 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f3)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001E2BA0) return l10_latch(o, 0x001E2BA0u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001E2BA0(o->h->ctx, a0, a1, a2, l10_float(f3)) < 0 ? l10_latch(o, 0x001E2BA0u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001E8B40(L10 *o, int32_t n0)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001E8B40) return l10_latch(o, 0x001E8B40u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001E8B40(o->h->ctx, n0) < 0 ? l10_latch(o, 0x001E8B40u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001EFD90(L10 *o, int32_t n0, uint32_t a1, uint32_t a2)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001EFD90) return l10_latch(o, 0x001EFD90u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFD90(o->h->ctx, n0, a1, a2) < 0 ? l10_latch(o, 0x001EFD90u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001EFE00(L10 *o, int32_t n0, uint32_t a1, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001EFE00) return l10_latch(o, 0x001EFE00u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFE00(o->h->ctx, n0, a1, result) < 0 ? l10_latch(o, 0x001EFE00u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001F5940(L10 *o, int32_t n0, uint32_t a1, int32_t n2)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001F5940) return l10_latch(o, 0x001F5940u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F5940(o->h->ctx, n0, a1, n2) < 0 ? l10_latch(o, 0x001F5940u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001FAE70(L10 *o, int32_t n0)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001FAE70) return l10_latch(o, 0x001FAE70u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FAE70(o->h->ctx, n0) < 0 ? l10_latch(o, 0x001FAE70u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001FBD50(L10 *o, uint32_t a0, int32_t n1, int32_t n2, uint32_t f3, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001FBD50) return l10_latch(o, 0x001FBD50u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FBD50(o->h->ctx, a0, n1, n2, l10_float(f3), result) < 0 ? l10_latch(o, 0x001FBD50u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_001FC580(L10 *o, uint32_t a0, int32_t n1)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_001FC580) return l10_latch(o, 0x001FC580u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FC580(o->h->ctx, a0, n1) < 0 ? l10_latch(o, 0x001FC580u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_0021BE40(L10 *o, uint32_t a0, int32_t *result)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_0021BE40) return l10_latch(o, 0x0021BE40u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_0021BE40(o->h->ctx, a0, result) < 0 ? l10_latch(o, 0x0021BE40u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_00825420(L10 *o, uint32_t a0)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_00825420) return l10_latch(o, 0x00825420u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_00825420(o->h->ctx, a0) < 0 ? l10_latch(o, 0x00825420u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int l10_c_00825930(L10 *o, uint32_t a0)
{
    if (l10_failed(o)) return -1;
    if (!o->h->w_00825930) return l10_latch(o, 0x00825930u, EM_LEVEL10_PORT_FAULT_NULL_WORKER);
    return o->h->w_00825930(o->h->ctx, a0) < 0 ? l10_latch(o, 0x00825930u, EM_LEVEL10_PORT_FAULT_WORKER_FAILED) : 0;
}
/* END GENERATED WRAPPERS */

/* Data addresses (runtime). */
#define D_00275B40 0x00275B40u /* pointer: the running actor's bone block */
#define D_0028A59C 0x0028A59Cu
#define D_008102B0 0x008102B0u /* player block */
#define D_00810350 0x00810350u /* the player's position x, y, z */
#define D_00810354 0x00810354u
#define D_00810358 0x00810358u
#define D_00810360 0x00810360u
#define S_700031B0 0x700031B0u
#define S_700031D0 0x700031D0u
#define S_700031D4 0x700031D4u
#define S_700031D8 0x700031D8u
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
#define S_70003A20 0x70003A20u
#define S_70003B8D 0x70003B8Du /* the pause / cinematic byte */

/* Float constants (bit patterns, as the originals materialize them). */
#define F_ZERO    0x00000000u
#define F_ONE     0x3F800000u
#define F_QUARTER 0x3E800000u
#define F_HALF    0x3F000000u
#define F_2       0x40000000u
#define F_3       0x40400000u
#define F_5       0x40A00000u
#define F_10      0x41200000u
#define F_15      0x41700000u
#define F_60      0x42700000u
#define F_100     0x42C80000u
#define F_300     0x43960000u
#define F_PI      0x40490FDBu

/* Translations called by other translations (direct calls, no hook). */
void l10_00825240(L10 *o, uint32_t self, uint32_t sp);
void l10_00825AB0(L10 *o, uint32_t self);

#endif /* EM_LEVEL10_PORT_INTERNAL_H */
