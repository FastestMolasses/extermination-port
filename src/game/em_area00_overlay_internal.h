/* Shared plumbing of em_area00_overlay.c: the memory view, fault latch and
 * hook-call helpers (the design of em_area01_overlay_internal.h). Not a
 * public interface. */
#ifndef EM_AREA00_OVERLAY_INTERNAL_H
#define EM_AREA00_OVERLAY_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area00_overlay.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea00OvlHooks *h;
    EmArea00OvlFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A00Ovl;

static inline int a00_latch(A00Ovl *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA00_OVL_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int a00_failed(const A00Ovl *o) { return o->fault->code != EM_AREA00_OVL_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *a00_at(A00Ovl *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!a00_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) a00_latch(o, address, EM_AREA00_OVL_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t a00_u8(A00Ovl *o, uint32_t a) { return *a00_at(o, a, 1); }
static inline uint32_t a00_u16(A00Ovl *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, a00_at(o, a, 2), 2);
    return v;
}
static inline uint32_t a00_u32(A00Ovl *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, a00_at(o, a, 4), 4);
    return v;
}
static inline int32_t a00_s32(A00Ovl *o, uint32_t a) { return (int32_t)a00_u32(o, a); }

static inline void a00_w8(A00Ovl *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(a00_at(o, a, 1), &b, 1);
}
static inline void a00_w16(A00Ovl *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(a00_at(o, a, 2), &h, 2);
}
static inline void a00_w32(A00Ovl *o, uint32_t a, uint32_t v) { memcpy(a00_at(o, a, 4), &v, 4); }

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the call.
 * Each returns 0 on success, -1 when skipped or failed. */
static inline int a00_c_001026A0(A00Ovl *o, uint32_t dst, uint32_t matrix, uint32_t vector)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001026A0) return a00_latch(o, 0x001026A0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001026A0(o->h->ctx, dst, matrix, vector) < 0 ? a00_latch(o, 0x001026A0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001028B8(A00Ovl *o, uint32_t dst, uint32_t a, uint32_t b)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001028B8) return a00_latch(o, 0x001028B8u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001028B8(o->h->ctx, dst, a, b) < 0 ? a00_latch(o, 0x001028B8u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001028D0(A00Ovl *o, uint32_t dst, uint32_t a, uint32_t b)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001028D0) return a00_latch(o, 0x001028D0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001028D0(o->h->ctx, dst, a, b) < 0 ? a00_latch(o, 0x001028D0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_00102948(A00Ovl *o, uint32_t dst, uint32_t src)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_00102948) return a00_latch(o, 0x00102948u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, dst, src) < 0 ? a00_latch(o, 0x00102948u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_00102958(A00Ovl *o, uint32_t dst, uint32_t src)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_00102958) return a00_latch(o, 0x00102958u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_00102958(o->h->ctx, dst, src) < 0 ? a00_latch(o, 0x00102958u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001029C0(A00Ovl *o, uint32_t matrix)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001029C0) return a00_latch(o, 0x001029C0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001029C0(o->h->ctx, matrix) < 0 ? a00_latch(o, 0x001029C0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_00122BB8(A00Ovl *o, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_00122BB8) return a00_latch(o, 0x00122BB8u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? a00_latch(o, 0x00122BB8u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001281C0(A00Ovl *o, float x, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001281C0) return a00_latch(o, 0x001281C0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001281C0(o->h->ctx, x, result) < 0 ? a00_latch(o, 0x001281C0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_00129780(A00Ovl *o, uint32_t actor, uint32_t block, int32_t sel, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_00129780) return a00_latch(o, 0x00129780u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_00129780(o->h->ctx, actor, block, sel, result) < 0 ? a00_latch(o, 0x00129780u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_00182F90(A00Ovl *o, uint32_t actor, uint32_t point)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_00182F90) return a00_latch(o, 0x00182F90u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_00182F90(o->h->ctx, actor, point) < 0 ? a00_latch(o, 0x00182F90u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_0019C6F0(A00Ovl *o, int32_t a0, int32_t a1, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_0019C6F0) return a00_latch(o, 0x0019C6F0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_0019C6F0(o->h->ctx, a0, a1, result) < 0 ? a00_latch(o, 0x0019C6F0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001A2370(A00Ovl *o, uint32_t actor, uint32_t matrix)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001A2370) return a00_latch(o, 0x001A2370u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001A2370(o->h->ctx, actor, matrix) < 0 ? a00_latch(o, 0x001A2370u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001AF800(A00Ovl *o, uint32_t actor)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001AF800) return a00_latch(o, 0x001AF800u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001AF800(o->h->ctx, actor) < 0 ? a00_latch(o, 0x001AF800u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001AFC10(A00Ovl *o, uint32_t actor)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001AFC10) return a00_latch(o, 0x001AFC10u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, actor) < 0 ? a00_latch(o, 0x001AFC10u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001B0C60(A00Ovl *o, int32_t a0, int32_t a1, int32_t a2)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001B0C60) return a00_latch(o, 0x001B0C60u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B0C60(o->h->ctx, a0, a1, a2) < 0 ? a00_latch(o, 0x001B0C60u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001B0F60(A00Ovl *o, uint32_t actor, int32_t a1, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001B0F60) return a00_latch(o, 0x001B0F60u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B0F60(o->h->ctx, actor, a1, result) < 0 ? a00_latch(o, 0x001B0F60u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001B0FD0(A00Ovl *o, uint32_t actor, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return a00_latch(o, 0x001B0FD0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B0FD0(o->h->ctx, actor, result) < 0 ? a00_latch(o, 0x001B0FD0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001B10B0(A00Ovl *o, uint32_t actor, int32_t a1, int32_t a2, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001B10B0) return a00_latch(o, 0x001B10B0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B10B0(o->h->ctx, actor, a1, a2, result) < 0 ? a00_latch(o, 0x001B10B0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001B17A0(A00Ovl *o, uint32_t actor, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001B17A0) return a00_latch(o, 0x001B17A0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B17A0(o->h->ctx, actor, result) < 0 ? a00_latch(o, 0x001B17A0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001B1B70(A00Ovl *o, uint32_t actor)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001B1B70) return a00_latch(o, 0x001B1B70u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B1B70(o->h->ctx, actor) < 0 ? a00_latch(o, 0x001B1B70u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001B1E20(A00Ovl *o, int32_t a0, int32_t a1)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001B1E20) return a00_latch(o, 0x001B1E20u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B1E20(o->h->ctx, a0, a1) < 0 ? a00_latch(o, 0x001B1E20u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001B6660(A00Ovl *o, uint32_t record, uint32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001B6660) return a00_latch(o, 0x001B6660u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B6660(o->h->ctx, record, result) < 0 ? a00_latch(o, 0x001B6660u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001BA1A0(A00Ovl *o, uint32_t block, uint32_t script)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return a00_latch(o, 0x001BA1A0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, block, script) < 0 ? a00_latch(o, 0x001BA1A0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001BA1C0(A00Ovl *o, uint32_t actor, int32_t index, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return a00_latch(o, 0x001BA1C0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BA1C0(o->h->ctx, actor, index, result) < 0 ? a00_latch(o, 0x001BA1C0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001BA1F0(A00Ovl *o, uint32_t actor, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return a00_latch(o, 0x001BA1F0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, actor, result) < 0 ? a00_latch(o, 0x001BA1F0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001BBDA0(A00Ovl *o, uint32_t actor)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001BBDA0) return a00_latch(o, 0x001BBDA0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BBDA0(o->h->ctx, actor) < 0 ? a00_latch(o, 0x001BBDA0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001BBE40(A00Ovl *o, uint32_t actor, uint32_t block, int32_t mode, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001BBE40) return a00_latch(o, 0x001BBE40u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BBE40(o->h->ctx, actor, block, mode, result) < 0 ? a00_latch(o, 0x001BBE40u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001BC0E0(A00Ovl *o, uint32_t actor, uint32_t block, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001BC0E0) return a00_latch(o, 0x001BC0E0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BC0E0(o->h->ctx, actor, block, result) < 0 ? a00_latch(o, 0x001BC0E0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001BC240(A00Ovl *o, uint32_t actor, uint32_t block)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001BC240) return a00_latch(o, 0x001BC240u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BC240(o->h->ctx, actor, block) < 0 ? a00_latch(o, 0x001BC240u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001BC290(A00Ovl *o, uint32_t actor, uint32_t block, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001BC290) return a00_latch(o, 0x001BC290u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BC290(o->h->ctx, actor, block, result) < 0 ? a00_latch(o, 0x001BC290u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001BC300(A00Ovl *o, uint32_t actor)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001BC300) return a00_latch(o, 0x001BC300u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BC300(o->h->ctx, actor) < 0 ? a00_latch(o, 0x001BC300u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001C2770(A00Ovl *o, uint32_t actor, uint32_t block, int32_t a2, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001C2770) return a00_latch(o, 0x001C2770u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C2770(o->h->ctx, actor, block, a2, result) < 0 ? a00_latch(o, 0x001C2770u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001C3D60(A00Ovl *o, uint32_t actor, uint32_t block)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001C3D60) return a00_latch(o, 0x001C3D60u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C3D60(o->h->ctx, actor, block) < 0 ? a00_latch(o, 0x001C3D60u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001C4760(A00Ovl *o, int32_t a0, int32_t a1, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001C4760) return a00_latch(o, 0x001C4760u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C4760(o->h->ctx, a0, a1, result) < 0 ? a00_latch(o, 0x001C4760u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001C5570(A00Ovl *o, uint32_t actor, uint32_t vector, int32_t a2, int32_t a3)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001C5570) return a00_latch(o, 0x001C5570u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C5570(o->h->ctx, actor, vector, a2, a3) < 0 ? a00_latch(o, 0x001C5570u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001C6380(A00Ovl *o, uint32_t actor)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001C6380) return a00_latch(o, 0x001C6380u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C6380(o->h->ctx, actor) < 0 ? a00_latch(o, 0x001C6380u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001C63E0(A00Ovl *o, uint32_t actor, int32_t a1)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001C63E0) return a00_latch(o, 0x001C63E0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C63E0(o->h->ctx, actor, a1) < 0 ? a00_latch(o, 0x001C63E0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001C64F0(A00Ovl *o, uint32_t actor, float dt, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001C64F0) return a00_latch(o, 0x001C64F0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C64F0(o->h->ctx, actor, dt, result) < 0 ? a00_latch(o, 0x001C64F0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001C67E0(A00Ovl *o, uint32_t actor, int32_t clip, float blend, float frame)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001C67E0) return a00_latch(o, 0x001C67E0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C67E0(o->h->ctx, actor, clip, blend, frame) < 0 ? a00_latch(o, 0x001C67E0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001C68C0(A00Ovl *o, uint32_t actor)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001C68C0) return a00_latch(o, 0x001C68C0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C68C0(o->h->ctx, actor) < 0 ? a00_latch(o, 0x001C68C0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001C69A0(A00Ovl *o, uint32_t actor)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001C69A0) return a00_latch(o, 0x001C69A0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C69A0(o->h->ctx, actor) < 0 ? a00_latch(o, 0x001C69A0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001CB5B0(A00Ovl *o, int32_t a0)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001CB5B0) return a00_latch(o, 0x001CB5B0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001CB5B0(o->h->ctx, a0) < 0 ? a00_latch(o, 0x001CB5B0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001CCF70(A00Ovl *o, uint32_t point, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001CCF70) return a00_latch(o, 0x001CCF70u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001CCF70(o->h->ctx, point, result) < 0 ? a00_latch(o, 0x001CCF70u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001CD520(A00Ovl *o, int32_t bucket, int32_t mode, uint32_t point, uint64_t giftag, uint32_t rgba, float w, float h, float zbias, int32_t *result)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001CD520) return a00_latch(o, 0x001CD520u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001CD520(o->h->ctx, bucket, mode, point, giftag, rgba, w, h, zbias, result) < 0 ? a00_latch(o, 0x001CD520u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001CFA60(A00Ovl *o, uint32_t packet, uint32_t matrix, float f12, float f13)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001CFA60) return a00_latch(o, 0x001CFA60u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001CFA60(o->h->ctx, packet, matrix, f12, f13) < 0 ? a00_latch(o, 0x001CFA60u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001CFBE0(A00Ovl *o, int32_t a0, int32_t a1, uint32_t a2, uint32_t a3, int32_t t0)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001CFBE0) return a00_latch(o, 0x001CFBE0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001CFBE0(o->h->ctx, a0, a1, a2, a3, t0) < 0 ? a00_latch(o, 0x001CFBE0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001EFD20(A00Ovl *o, int32_t a0, uint32_t point)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001EFD20) return a00_latch(o, 0x001EFD20u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001EFD20(o->h->ctx, a0, point) < 0 ? a00_latch(o, 0x001EFD20u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001F4E20(A00Ovl *o, uint32_t point, uint32_t colour, float f12)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001F4E20) return a00_latch(o, 0x001F4E20u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001F4E20(o->h->ctx, point, colour, f12) < 0 ? a00_latch(o, 0x001F4E20u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001F5940(A00Ovl *o, int32_t a0, uint32_t point, int32_t a2)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001F5940) return a00_latch(o, 0x001F5940u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001F5940(o->h->ctx, a0, point, a2) < 0 ? a00_latch(o, 0x001F5940u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001FB9F0(A00Ovl *o, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001FB9F0) return a00_latch(o, 0x001FB9F0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001FB9F0(o->h->ctx, id, a1, a2, a3) < 0 ? a00_latch(o, 0x001FB9F0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a00_c_001FC3C0(A00Ovl *o, uint32_t actor, uint32_t handle, int32_t cue, float f12, float f13)
{
    if (a00_failed(o)) return -1;
    if (!o->h->w_001FC3C0) return a00_latch(o, 0x001FC3C0u, EM_AREA00_OVL_FAULT_NULL_WORKER);
    return o->h->w_001FC3C0(o->h->ctx, actor, handle, cue, f12, f13) < 0 ? a00_latch(o, 0x001FC3C0u, EM_AREA00_OVL_FAULT_WORKER_FAILED) : 0;
}


/* The actor's own callback: the function at self +0x4C, called with self. */
static inline void a00_callback(A00Ovl *o, uint32_t self)
{
    uint32_t fn = a00_u32(o, self + 0x4C);
    if (a00_failed(o)) return;
    if (!o->h->w_callback) {
        a00_latch(o, fn, EM_AREA00_OVL_FAULT_NULL_WORKER);
        return;
    }
    if (o->h->w_callback(o->h->ctx, fn, self) < 0) a00_latch(o, fn, EM_AREA00_OVL_FAULT_WORKER_FAILED);
}

static inline void a00_open(A00Ovl *o, const EmArea00OvlHooks *h, EmArea00OvlFault *fault)
{
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
}

#endif /* EM_AREA00_OVERLAY_INTERNAL_H */
