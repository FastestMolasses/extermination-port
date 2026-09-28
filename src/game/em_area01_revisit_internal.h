/* Shared plumbing of em_area01_revisit.c: the memory view, fault latch and
 * hook-call helpers (the design of em_area01_overlay_internal.h and
 * em_area00_overlay_internal.h). Not a public interface. */
#ifndef EM_AREA01_REVISIT_INTERNAL_H
#define EM_AREA01_REVISIT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area01_revisit.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea01RevisitHooks *h;
    EmArea01RevisitFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A01Rv;

static inline int rv_latch(A01Rv *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA01_RV_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int rv_failed(const A01Rv *o) { return o->fault->code != EM_AREA01_RV_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *rv_at(A01Rv *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!rv_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) rv_latch(o, address, EM_AREA01_RV_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t rv_u8(A01Rv *o, uint32_t a) { return *rv_at(o, a, 1); }
static inline uint32_t rv_u16(A01Rv *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, rv_at(o, a, 2), 2);
    return v;
}
/* A halfword load, sign-extended (the original's lh). */
static inline int32_t rv_s16(A01Rv *o, uint32_t a) { return (int16_t)rv_u16(o, a); }
static inline uint32_t rv_u32(A01Rv *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, rv_at(o, a, 4), 4);
    return v;
}
/* One half of a quadword load or store (the EE moves a quadword as two
 * doublewords, the lower first). */
static inline uint64_t rv_u64(A01Rv *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, rv_at(o, a, 8), 8);
    return v;
}

static inline void rv_w8(A01Rv *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(rv_at(o, a, 1), &b, 1);
}
static inline void rv_w16(A01Rv *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(rv_at(o, a, 2), &h, 2);
}
static inline void rv_w32(A01Rv *o, uint32_t a, uint32_t v) { memcpy(rv_at(o, a, 4), &v, 4); }
static inline void rv_w64(A01Rv *o, uint32_t a, uint64_t v) { memcpy(rv_at(o, a, 8), &v, 8); }

/* The actor's +0x4C callback, read at the call (the original loads it just
 * before the jump). */
static inline int rv_callback(A01Rv *o, uint32_t self)
{
    uint32_t fn = rv_u32(o, self + 0x4C);
    if (rv_failed(o)) return -1;
    if (!o->h->w_callback) return rv_latch(o, fn, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, self) < 0 ? rv_latch(o, fn, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the call.
 * Each returns 0 on success, -1 when skipped or failed. */
static inline int rv_c_001026A0(A01Rv *o, uint32_t dst, uint32_t matrix, uint32_t vector)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001026A0) return rv_latch(o, 0x001026A0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001026A0(o->h->ctx, dst, matrix, vector) < 0 ? rv_latch(o, 0x001026A0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001026D0(A01Rv *o, uint32_t dst, uint32_t a, uint32_t b)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001026D0) return rv_latch(o, 0x001026D0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001026D0(o->h->ctx, dst, a, b) < 0 ? rv_latch(o, 0x001026D0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001028B8(A01Rv *o, uint32_t dst, uint32_t a, uint32_t b)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001028B8) return rv_latch(o, 0x001028B8u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001028B8(o->h->ctx, dst, a, b) < 0 ? rv_latch(o, 0x001028B8u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001028D0(A01Rv *o, uint32_t dst, uint32_t a, uint32_t b)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001028D0) return rv_latch(o, 0x001028D0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001028D0(o->h->ctx, dst, a, b) < 0 ? rv_latch(o, 0x001028D0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_00102738(A01Rv *o, uint32_t a, uint32_t b, float *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_00102738) return rv_latch(o, 0x00102738u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_00102738(o->h->ctx, a, b, result) < 0 ? rv_latch(o, 0x00102738u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_00102760(A01Rv *o, uint32_t dst, uint32_t src)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_00102760) return rv_latch(o, 0x00102760u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_00102760(o->h->ctx, dst, src) < 0 ? rv_latch(o, 0x00102760u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_00102948(A01Rv *o, uint32_t dst, uint32_t src)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_00102948) return rv_latch(o, 0x00102948u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, dst, src) < 0 ? rv_latch(o, 0x00102948u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_00102958(A01Rv *o, uint32_t dst, uint32_t src)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_00102958) return rv_latch(o, 0x00102958u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_00102958(o->h->ctx, dst, src) < 0 ? rv_latch(o, 0x00102958u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001029C0(A01Rv *o, uint32_t matrix)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001029C0) return rv_latch(o, 0x001029C0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001029C0(o->h->ctx, matrix) < 0 ? rv_latch(o, 0x001029C0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_00102A60(A01Rv *o, uint32_t dst, uint32_t src, float angle)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_00102A60) return rv_latch(o, 0x00102A60u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_00102A60(o->h->ctx, dst, src, angle) < 0 ? rv_latch(o, 0x00102A60u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_00102B08(A01Rv *o, uint32_t dst, uint32_t src, float angle)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_00102B08) return rv_latch(o, 0x00102B08u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_00102B08(o->h->ctx, dst, src, angle) < 0 ? rv_latch(o, 0x00102B08u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_00102BB0(A01Rv *o, uint32_t dst, uint32_t src, float angle)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_00102BB0) return rv_latch(o, 0x00102BB0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_00102BB0(o->h->ctx, dst, src, angle) < 0 ? rv_latch(o, 0x00102BB0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001031E0(A01Rv *o, uint32_t dst, uint32_t src)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001031E0) return rv_latch(o, 0x001031E0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001031E0(o->h->ctx, dst, src) < 0 ? rv_latch(o, 0x001031E0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_00103230(A01Rv *o, uint32_t dst, uint32_t src, float scale)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_00103230) return rv_latch(o, 0x00103230u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_00103230(o->h->ctx, dst, src, scale) < 0 ? rv_latch(o, 0x00103230u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_0011E2A8(A01Rv *o, float x, float *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return rv_latch(o, 0x0011E2A8u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_0011E2A8(o->h->ctx, x, result) < 0 ? rv_latch(o, 0x0011E2A8u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_0011E748(A01Rv *o, float x, float *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_0011E748) return rv_latch(o, 0x0011E748u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_0011E748(o->h->ctx, x, result) < 0 ? rv_latch(o, 0x0011E748u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_00122BB8(A01Rv *o, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_00122BB8) return rv_latch(o, 0x00122BB8u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? rv_latch(o, 0x00122BB8u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001287F0(A01Rv *o, uint32_t actor, uint32_t block, int32_t clip, float f12)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001287F0) return rv_latch(o, 0x001287F0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001287F0(o->h->ctx, actor, block, clip, f12) < 0 ? rv_latch(o, 0x001287F0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_00128830(A01Rv *o, uint32_t actor, float f12, float f13, float f14)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_00128830) return rv_latch(o, 0x00128830u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_00128830(o->h->ctx, actor, f12, f13, f14) < 0 ? rv_latch(o, 0x00128830u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_0012D580(A01Rv *o, uint32_t actor, uint32_t block, int32_t a2)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_0012D580) return rv_latch(o, 0x0012D580u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_0012D580(o->h->ctx, actor, block, a2) < 0 ? rv_latch(o, 0x0012D580u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_0012DE90(A01Rv *o, uint32_t block)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_0012DE90) return rv_latch(o, 0x0012DE90u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_0012DE90(o->h->ctx, block) < 0 ? rv_latch(o, 0x0012DE90u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_0019A570(A01Rv *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_0019A570) return rv_latch(o, 0x0019A570u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_0019A570(o->h->ctx, a0, a1, a2, a3, result) < 0 ? rv_latch(o, 0x0019A570u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_0019AA80(A01Rv *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_0019AA80) return rv_latch(o, 0x0019AA80u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_0019AA80(o->h->ctx, a0, a1, a2, result) < 0 ? rv_latch(o, 0x0019AA80u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_0019C6F0(A01Rv *o, int32_t a0, int32_t a1, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_0019C6F0) return rv_latch(o, 0x0019C6F0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_0019C6F0(o->h->ctx, a0, a1, result) < 0 ? rv_latch(o, 0x0019C6F0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001AFA90(A01Rv *o, int32_t cls, uint32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001AFA90) return rv_latch(o, 0x001AFA90u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001AFA90(o->h->ctx, cls, result) < 0 ? rv_latch(o, 0x001AFA90u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001AFC10(A01Rv *o, uint32_t actor)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001AFC10) return rv_latch(o, 0x001AFC10u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, actor) < 0 ? rv_latch(o, 0x001AFC10u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001B10B0(A01Rv *o, uint32_t actor, int32_t a1, int32_t a2, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001B10B0) return rv_latch(o, 0x001B10B0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001B10B0(o->h->ctx, actor, a1, a2, result) < 0 ? rv_latch(o, 0x001B10B0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001B1240(A01Rv *o, uint32_t origin, float x, float z, float *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001B1240) return rv_latch(o, 0x001B1240u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001B1240(o->h->ctx, origin, x, z, result) < 0 ? rv_latch(o, 0x001B1240u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001B12B0(A01Rv *o, float goal, float current, float rate, float *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001B12B0) return rv_latch(o, 0x001B12B0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001B12B0(o->h->ctx, goal, current, rate, result) < 0 ? rv_latch(o, 0x001B12B0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001B1470(A01Rv *o, float angle, float *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001B1470) return rv_latch(o, 0x001B1470u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001B1470(o->h->ctx, angle, result) < 0 ? rv_latch(o, 0x001B1470u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001B17A0(A01Rv *o, uint32_t actor, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001B17A0) return rv_latch(o, 0x001B17A0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001B17A0(o->h->ctx, actor, result) < 0 ? rv_latch(o, 0x001B17A0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001B1EA0(A01Rv *o, int32_t a0, uint32_t point, uint32_t quad, int32_t a3, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return rv_latch(o, 0x001B1EA0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001B1EA0(o->h->ctx, a0, point, quad, a3, result) < 0 ? rv_latch(o, 0x001B1EA0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001B6660(A01Rv *o, uint32_t record, uint32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001B6660) return rv_latch(o, 0x001B6660u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001B6660(o->h->ctx, record, result) < 0 ? rv_latch(o, 0x001B6660u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001BA1A0(A01Rv *o, uint32_t block, uint32_t script)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return rv_latch(o, 0x001BA1A0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, block, script) < 0 ? rv_latch(o, 0x001BA1A0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001BA1F0(A01Rv *o, uint32_t actor, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return rv_latch(o, 0x001BA1F0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, actor, result) < 0 ? rv_latch(o, 0x001BA1F0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001C2770(A01Rv *o, uint32_t actor, uint32_t block, int32_t a2, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001C2770) return rv_latch(o, 0x001C2770u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001C2770(o->h->ctx, actor, block, a2, result) < 0 ? rv_latch(o, 0x001C2770u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001C3D60(A01Rv *o, uint32_t actor, uint32_t block)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001C3D60) return rv_latch(o, 0x001C3D60u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001C3D60(o->h->ctx, actor, block) < 0 ? rv_latch(o, 0x001C3D60u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001C4760(A01Rv *o, int32_t a0, int32_t a1, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001C4760) return rv_latch(o, 0x001C4760u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001C4760(o->h->ctx, a0, a1, result) < 0 ? rv_latch(o, 0x001C4760u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001C47A0(A01Rv *o, int32_t a0, int32_t a1, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001C47A0) return rv_latch(o, 0x001C47A0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001C47A0(o->h->ctx, a0, a1, result) < 0 ? rv_latch(o, 0x001C47A0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001C63E0(A01Rv *o, uint32_t actor, int32_t a1)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001C63E0) return rv_latch(o, 0x001C63E0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001C63E0(o->h->ctx, actor, a1) < 0 ? rv_latch(o, 0x001C63E0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001C64F0(A01Rv *o, uint32_t actor, float dt, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001C64F0) return rv_latch(o, 0x001C64F0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001C64F0(o->h->ctx, actor, dt, result) < 0 ? rv_latch(o, 0x001C64F0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001C68C0(A01Rv *o, uint32_t actor)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001C68C0) return rv_latch(o, 0x001C68C0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001C68C0(o->h->ctx, actor) < 0 ? rv_latch(o, 0x001C68C0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001C69A0(A01Rv *o, uint32_t actor)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001C69A0) return rv_latch(o, 0x001C69A0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001C69A0(o->h->ctx, actor) < 0 ? rv_latch(o, 0x001C69A0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001CA6F0(A01Rv *o, uint32_t actor, int32_t a1)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001CA6F0) return rv_latch(o, 0x001CA6F0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001CA6F0(o->h->ctx, actor, a1) < 0 ? rv_latch(o, 0x001CA6F0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001CD520(A01Rv *o, int32_t bucket, int32_t mode, uint32_t point, uint64_t giftag, uint32_t rgba, float w, float h, float zbias, int32_t *result)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001CD520) return rv_latch(o, 0x001CD520u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001CD520(o->h->ctx, bucket, mode, point, giftag, rgba, w, h, zbias, result) < 0 ? rv_latch(o, 0x001CD520u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001E2BA0(A01Rv *o, uint32_t start, uint32_t end, uint32_t colour, float length)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001E2BA0) return rv_latch(o, 0x001E2BA0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001E2BA0(o->h->ctx, start, end, colour, length) < 0 ? rv_latch(o, 0x001E2BA0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001EFD20(A01Rv *o, int32_t id, uint32_t point)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001EFD20) return rv_latch(o, 0x001EFD20u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001EFD20(o->h->ctx, id, point) < 0 ? rv_latch(o, 0x001EFD20u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001EFD90(A01Rv *o, int32_t id, uint32_t a, uint32_t b)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001EFD90) return rv_latch(o, 0x001EFD90u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001EFD90(o->h->ctx, id, a, b) < 0 ? rv_latch(o, 0x001EFD90u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001F4010(A01Rv *o, int32_t a0, uint32_t matrix)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001F4010) return rv_latch(o, 0x001F4010u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001F4010(o->h->ctx, a0, matrix) < 0 ? rv_latch(o, 0x001F4010u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001F4E20(A01Rv *o, uint32_t point, uint32_t colour, float f12)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001F4E20) return rv_latch(o, 0x001F4E20u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001F4E20(o->h->ctx, point, colour, f12) < 0 ? rv_latch(o, 0x001F4E20u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001FAE70(A01Rv *o, int32_t a0)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001FAE70) return rv_latch(o, 0x001FAE70u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001FAE70(o->h->ctx, a0) < 0 ? rv_latch(o, 0x001FAE70u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_001FB9F0(A01Rv *o, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_001FB9F0) return rv_latch(o, 0x001FB9F0u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_001FB9F0(o->h->ctx, id, a1, a2, a3) < 0 ? rv_latch(o, 0x001FB9F0u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline int rv_c_00826010(A01Rv *o, uint32_t actor)
{
    if (rv_failed(o)) return -1;
    if (!o->h->w_00826010) return rv_latch(o, 0x00826010u, EM_AREA01_RV_FAULT_NULL_WORKER);
    return o->h->w_00826010(o->h->ctx, actor) < 0 ? rv_latch(o, 0x00826010u, EM_AREA01_RV_FAULT_WORKER_FAILED) : 0;
}

static inline void rv_open(A01Rv *o, const EmArea01RevisitHooks *h, EmArea01RevisitFault *fault)
{
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
}

#endif /* EM_AREA01_REVISIT_INTERNAL_H */
