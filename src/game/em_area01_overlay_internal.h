/* Shared plumbing of em_area01_overlay*.c: the memory view, fault latch and
 * hook-call helpers. Not a public interface. */
#ifndef EM_AREA01_OVERLAY_INTERNAL_H
#define EM_AREA01_OVERLAY_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area01_overlay.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea01OvlHooks *h;
    EmArea01OvlFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A01Ovl;

static inline int a01_latch(A01Ovl *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA01_OVL_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int a01_failed(const A01Ovl *o) { return o->fault->code != EM_AREA01_OVL_FAULT_NONE; }

static inline uint8_t *a01_at(A01Ovl *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!a01_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) a01_latch(o, address, EM_AREA01_OVL_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t a01_u8(A01Ovl *o, uint32_t a) { return *a01_at(o, a, 1); }
static inline int32_t a01_s8(A01Ovl *o, uint32_t a) { return (int8_t)*a01_at(o, a, 1); }
static inline uint32_t a01_u16(A01Ovl *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, a01_at(o, a, 2), 2);
    return v;
}
static inline int32_t a01_s16(A01Ovl *o, uint32_t a) { return (int16_t)a01_u16(o, a); }
static inline uint32_t a01_u32(A01Ovl *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, a01_at(o, a, 4), 4);
    return v;
}
static inline int32_t a01_s32(A01Ovl *o, uint32_t a) { return (int32_t)a01_u32(o, a); }
static inline float a01_f32(A01Ovl *o, uint32_t a) { return em_ee_float(a01_u32(o, a)); }

static inline void a01_w8(A01Ovl *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(a01_at(o, a, 1), &b, 1);
}
static inline void a01_w16(A01Ovl *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(a01_at(o, a, 2), &h, 2);
}
static inline void a01_w32(A01Ovl *o, uint32_t a, uint32_t v) { memcpy(a01_at(o, a, 4), &v, 4); }
static inline void a01_wf(A01Ovl *o, uint32_t a, float v) { a01_w32(o, a, em_ee_bits(v)); }

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the call.
 * Each returns 0 on success, -1 when skipped or failed. */
static inline int a01_c_00102760(A01Ovl *o, uint32_t dst, uint32_t src)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_00102760) return a01_latch(o, 0x00102760u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_00102760(o->h->ctx, dst, src) < 0 ? a01_latch(o, 0x00102760u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001028D0(A01Ovl *o, uint32_t dst, uint32_t a, uint32_t b)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001028D0) return a01_latch(o, 0x001028D0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001028D0(o->h->ctx, dst, a, b) < 0 ? a01_latch(o, 0x001028D0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_00102948(A01Ovl *o, uint32_t dst, uint32_t src)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_00102948) return a01_latch(o, 0x00102948u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, dst, src) < 0 ? a01_latch(o, 0x00102948u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_00102958(A01Ovl *o, uint32_t dst, uint32_t src)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_00102958) return a01_latch(o, 0x00102958u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_00102958(o->h->ctx, dst, src) < 0 ? a01_latch(o, 0x00102958u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_0011DBB8(A01Ovl *o, float x, float *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_0011DBB8) return a01_latch(o, 0x0011DBB8u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_0011DBB8(o->h->ctx, x, result) < 0 ? a01_latch(o, 0x0011DBB8u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_0011DF78(A01Ovl *o, float x, float *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_0011DF78) return a01_latch(o, 0x0011DF78u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_0011DF78(o->h->ctx, x, result) < 0 ? a01_latch(o, 0x0011DF78u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_0011E2A8(A01Ovl *o, float x, float *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return a01_latch(o, 0x0011E2A8u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_0011E2A8(o->h->ctx, x, result) < 0 ? a01_latch(o, 0x0011E2A8u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_0011E520(A01Ovl *o, float x, float *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_0011E520) return a01_latch(o, 0x0011E520u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_0011E520(o->h->ctx, x, result) < 0 ? a01_latch(o, 0x0011E520u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_0011E748(A01Ovl *o, float x, float *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_0011E748) return a01_latch(o, 0x0011E748u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_0011E748(o->h->ctx, x, result) < 0 ? a01_latch(o, 0x0011E748u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_00122BB8(A01Ovl *o, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_00122BB8) return a01_latch(o, 0x00122BB8u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? a01_latch(o, 0x00122BB8u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_00182BF0(A01Ovl *o, uint32_t actor, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_00182BF0) return a01_latch(o, 0x00182BF0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_00182BF0(o->h->ctx, actor, result) < 0 ? a01_latch(o, 0x00182BF0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_0019B6C0(A01Ovl *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_0019B6C0) return a01_latch(o, 0x0019B6C0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_0019B6C0(o->h->ctx, a0, a1, a2, result) < 0 ? a01_latch(o, 0x0019B6C0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001A2370(A01Ovl *o, uint32_t actor, uint32_t matrix)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001A2370) return a01_latch(o, 0x001A2370u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001A2370(o->h->ctx, actor, matrix) < 0 ? a01_latch(o, 0x001A2370u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001AFA90(A01Ovl *o, int32_t cls, uint32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001AFA90) return a01_latch(o, 0x001AFA90u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001AFA90(o->h->ctx, cls, result) < 0 ? a01_latch(o, 0x001AFA90u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001AFC10(A01Ovl *o, uint32_t actor)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001AFC10) return a01_latch(o, 0x001AFC10u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, actor) < 0 ? a01_latch(o, 0x001AFC10u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001B0FD0(A01Ovl *o, uint32_t actor, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return a01_latch(o, 0x001B0FD0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B0FD0(o->h->ctx, actor, result) < 0 ? a01_latch(o, 0x001B0FD0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001B10B0(A01Ovl *o, uint32_t actor, int32_t a1, int32_t a2, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001B10B0) return a01_latch(o, 0x001B10B0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B10B0(o->h->ctx, actor, a1, a2, result) < 0 ? a01_latch(o, 0x001B10B0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001B1190(A01Ovl *o, int32_t a0)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001B1190) return a01_latch(o, 0x001B1190u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B1190(o->h->ctx, a0) < 0 ? a01_latch(o, 0x001B1190u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001B11E0(A01Ovl *o, int32_t a0, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001B11E0) return a01_latch(o, 0x001B11E0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B11E0(o->h->ctx, a0, result) < 0 ? a01_latch(o, 0x001B11E0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001B1240(A01Ovl *o, uint32_t origin, float x, float z, float *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001B1240) return a01_latch(o, 0x001B1240u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B1240(o->h->ctx, origin, x, z, result) < 0 ? a01_latch(o, 0x001B1240u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001B12B0(A01Ovl *o, float goal, float current, float rate, float *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001B12B0) return a01_latch(o, 0x001B12B0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B12B0(o->h->ctx, goal, current, rate, result) < 0 ? a01_latch(o, 0x001B12B0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001B1380(A01Ovl *o, uint32_t a0, uint32_t a1, float f12, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001B1380) return a01_latch(o, 0x001B1380u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B1380(o->h->ctx, a0, a1, f12, result) < 0 ? a01_latch(o, 0x001B1380u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001B1470(A01Ovl *o, float angle, float *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001B1470) return a01_latch(o, 0x001B1470u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B1470(o->h->ctx, angle, result) < 0 ? a01_latch(o, 0x001B1470u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001B17A0(A01Ovl *o, uint32_t actor, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001B17A0) return a01_latch(o, 0x001B17A0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B17A0(o->h->ctx, actor, result) < 0 ? a01_latch(o, 0x001B17A0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001B1B70(A01Ovl *o, uint32_t actor)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001B1B70) return a01_latch(o, 0x001B1B70u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B1B70(o->h->ctx, actor) < 0 ? a01_latch(o, 0x001B1B70u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001B1EA0(A01Ovl *o, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return a01_latch(o, 0x001B1EA0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B1EA0(o->h->ctx, a0, a1, a2, a3, result) < 0 ? a01_latch(o, 0x001B1EA0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001BA1A0(A01Ovl *o, uint32_t block, uint32_t script)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return a01_latch(o, 0x001BA1A0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, block, script) < 0 ? a01_latch(o, 0x001BA1A0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001BA1C0(A01Ovl *o, uint32_t actor, int32_t index, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return a01_latch(o, 0x001BA1C0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BA1C0(o->h->ctx, actor, index, result) < 0 ? a01_latch(o, 0x001BA1C0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001BA1F0(A01Ovl *o, uint32_t actor, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return a01_latch(o, 0x001BA1F0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, actor, result) < 0 ? a01_latch(o, 0x001BA1F0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001BA540(A01Ovl *o, uint32_t actor)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001BA540) return a01_latch(o, 0x001BA540u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BA540(o->h->ctx, actor) < 0 ? a01_latch(o, 0x001BA540u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001BA580(A01Ovl *o, uint32_t actor, int32_t a1)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001BA580) return a01_latch(o, 0x001BA580u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BA580(o->h->ctx, actor, a1) < 0 ? a01_latch(o, 0x001BA580u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001BA8E0(A01Ovl *o, uint32_t actor, int32_t type)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001BA8E0) return a01_latch(o, 0x001BA8E0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BA8E0(o->h->ctx, actor, type) < 0 ? a01_latch(o, 0x001BA8E0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001BBDA0(A01Ovl *o, uint32_t actor)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001BBDA0) return a01_latch(o, 0x001BBDA0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BBDA0(o->h->ctx, actor) < 0 ? a01_latch(o, 0x001BBDA0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001BBE40(A01Ovl *o, uint32_t actor, uint32_t block, int32_t mode, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001BBE40) return a01_latch(o, 0x001BBE40u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BBE40(o->h->ctx, actor, block, mode, result) < 0 ? a01_latch(o, 0x001BBE40u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001BC0E0(A01Ovl *o, uint32_t actor, uint32_t block, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001BC0E0) return a01_latch(o, 0x001BC0E0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BC0E0(o->h->ctx, actor, block, result) < 0 ? a01_latch(o, 0x001BC0E0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001BC240(A01Ovl *o, uint32_t actor, uint32_t block)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001BC240) return a01_latch(o, 0x001BC240u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BC240(o->h->ctx, actor, block) < 0 ? a01_latch(o, 0x001BC240u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001BC290(A01Ovl *o, uint32_t actor, uint32_t block, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001BC290) return a01_latch(o, 0x001BC290u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BC290(o->h->ctx, actor, block, result) < 0 ? a01_latch(o, 0x001BC290u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001BC300(A01Ovl *o, uint32_t actor)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001BC300) return a01_latch(o, 0x001BC300u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BC300(o->h->ctx, actor) < 0 ? a01_latch(o, 0x001BC300u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001C4760(A01Ovl *o, int32_t a0, int32_t a1, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001C4760) return a01_latch(o, 0x001C4760u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C4760(o->h->ctx, a0, a1, result) < 0 ? a01_latch(o, 0x001C4760u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001C4820(A01Ovl *o, uint32_t actor)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001C4820) return a01_latch(o, 0x001C4820u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C4820(o->h->ctx, actor) < 0 ? a01_latch(o, 0x001C4820u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001C5C90(A01Ovl *o, uint32_t actor)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001C5C90) return a01_latch(o, 0x001C5C90u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C5C90(o->h->ctx, actor) < 0 ? a01_latch(o, 0x001C5C90u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001C6380(A01Ovl *o, uint32_t actor)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001C6380) return a01_latch(o, 0x001C6380u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C6380(o->h->ctx, actor) < 0 ? a01_latch(o, 0x001C6380u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001C63E0(A01Ovl *o, uint32_t actor, int32_t a1)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001C63E0) return a01_latch(o, 0x001C63E0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C63E0(o->h->ctx, actor, a1) < 0 ? a01_latch(o, 0x001C63E0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001C64F0(A01Ovl *o, uint32_t actor, float dt, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001C64F0) return a01_latch(o, 0x001C64F0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C64F0(o->h->ctx, actor, dt, result) < 0 ? a01_latch(o, 0x001C64F0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001C67E0(A01Ovl *o, uint32_t actor, int32_t clip, float blend, float frame)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001C67E0) return a01_latch(o, 0x001C67E0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C67E0(o->h->ctx, actor, clip, blend, frame) < 0 ? a01_latch(o, 0x001C67E0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001C68C0(A01Ovl *o, uint32_t actor)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001C68C0) return a01_latch(o, 0x001C68C0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C68C0(o->h->ctx, actor) < 0 ? a01_latch(o, 0x001C68C0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001EFD90(A01Ovl *o, int32_t id, uint32_t position, uint32_t rotation, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001EFD90) return a01_latch(o, 0x001EFD90u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001EFD90(o->h->ctx, id, position, rotation, result) < 0 ? a01_latch(o, 0x001EFD90u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001EFE00(A01Ovl *o, int32_t a0, uint32_t a1, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001EFE00) return a01_latch(o, 0x001EFE00u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001EFE00(o->h->ctx, a0, a1, result) < 0 ? a01_latch(o, 0x001EFE00u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001FBD50(A01Ovl *o, uint32_t actor, int32_t cue, int32_t a2, float f12, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001FBD50) return a01_latch(o, 0x001FBD50u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001FBD50(o->h->ctx, actor, cue, a2, f12, result) < 0 ? a01_latch(o, 0x001FBD50u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001FC3C0(A01Ovl *o, uint32_t actor, uint32_t handle, int32_t cue, float f12, float f13)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001FC3C0) return a01_latch(o, 0x001FC3C0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001FC3C0(o->h->ctx, actor, handle, cue, f12, f13) < 0 ? a01_latch(o, 0x001FC3C0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_001FC520(A01Ovl *o, uint32_t handle)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_001FC520) return a01_latch(o, 0x001FC520u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_001FC520(o->h->ctx, handle) < 0 ? a01_latch(o, 0x001FC520u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_008282F0(A01Ovl *o, uint32_t actor, uint32_t matrix, int32_t *result)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_008282F0) return a01_latch(o, 0x008282F0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_008282F0(o->h->ctx, actor, matrix, result) < 0 ? a01_latch(o, 0x008282F0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a01_c_008287C0(A01Ovl *o, uint32_t matrix)
{
    if (a01_failed(o)) return -1;
    if (!o->h->w_008287C0) return a01_latch(o, 0x008287C0u, EM_AREA01_OVL_FAULT_NULL_WORKER);
    return o->h->w_008287C0(o->h->ctx, matrix) < 0 ? a01_latch(o, 0x008287C0u, EM_AREA01_OVL_FAULT_WORKER_FAILED) : 0;
}

/* The actor's own callback: the function at self +0x4C, called with self. */
static inline void a01_callback(A01Ovl *o, uint32_t self)
{
    uint32_t fn = a01_u32(o, self + 0x4C);
    if (a01_failed(o)) return;
    if (!o->h->w_callback) {
        a01_latch(o, fn, EM_AREA01_OVL_FAULT_NULL_WORKER);
        return;
    }
    if (o->h->w_callback(o->h->ctx, fn, self) < 0) a01_latch(o, fn, EM_AREA01_OVL_FAULT_WORKER_FAILED);
}

static inline void a01_open(A01Ovl *o, const EmArea01OvlHooks *h, EmArea01OvlFault *fault)
{
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
}

/* 0x826D40 lives in its own file. */
int a01_ovl_826d40(A01Ovl *o, uint32_t self);

#endif /* EM_AREA01_OVERLAY_INTERNAL_H */
