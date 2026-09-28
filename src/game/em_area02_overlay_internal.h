/* Shared plumbing of em_area02_overlay.c: the memory view, fault latch and
 * hook-call helpers (the design of em_area00_overlay_internal.h, with
 * 64-bit accesses for the quadword copies). Not a public interface. */
#ifndef EM_AREA02_OVERLAY_INTERNAL_H
#define EM_AREA02_OVERLAY_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area02_overlay.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea02OvlHooks *h;
    EmArea02OvlFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A02Ovl;

static inline int a02_latch(A02Ovl *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA02_OVL_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int a02_failed(const A02Ovl *o) { return o->fault->code != EM_AREA02_OVL_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *a02_at(A02Ovl *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!a02_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) a02_latch(o, address, EM_AREA02_OVL_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t a02_u8(A02Ovl *o, uint32_t a) { return *a02_at(o, a, 1); }
static inline uint32_t a02_u16(A02Ovl *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, a02_at(o, a, 2), 2);
    return v;
}
static inline uint32_t a02_u32(A02Ovl *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, a02_at(o, a, 4), 4);
    return v;
}
static inline uint64_t a02_u64(A02Ovl *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, a02_at(o, a, 8), 8);
    return v;
}
static inline int32_t a02_s32(A02Ovl *o, uint32_t a) { return (int32_t)a02_u32(o, a); }

static inline void a02_w8(A02Ovl *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(a02_at(o, a, 1), &b, 1);
}
static inline void a02_w16(A02Ovl *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(a02_at(o, a, 2), &h, 2);
}
static inline void a02_w32(A02Ovl *o, uint32_t a, uint32_t v) { memcpy(a02_at(o, a, 4), &v, 4); }
static inline void a02_w64(A02Ovl *o, uint32_t a, uint64_t v) { memcpy(a02_at(o, a, 8), &v, 8); }

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the call.
 * Each returns 0 on success, -1 when skipped or failed. */
static inline int a02_c_0011DE90(A02Ovl *o, float x, float *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_0011DE90) return a02_latch(o, 0x0011DE90u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_0011DE90(o->h->ctx, x, result) < 0 ? a02_latch(o, 0x0011DE90u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_0011E2A8(A02Ovl *o, float x, float *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return a02_latch(o, 0x0011E2A8u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_0011E2A8(o->h->ctx, x, result) < 0 ? a02_latch(o, 0x0011E2A8u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001026A0(A02Ovl *o, uint32_t dst, uint32_t matrix, uint32_t vector)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001026A0) return a02_latch(o, 0x001026A0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001026A0(o->h->ctx, dst, matrix, vector) < 0 ? a02_latch(o, 0x001026A0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_00121A28(A02Ovl *o, uint32_t dst, int32_t value, int32_t count)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_00121A28) return a02_latch(o, 0x00121A28u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_00121A28(o->h->ctx, dst, value, count) < 0 ? a02_latch(o, 0x00121A28u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_00122BB8(A02Ovl *o, int32_t *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_00122BB8) return a02_latch(o, 0x00122BB8u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? a02_latch(o, 0x00122BB8u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_0019C6F0(A02Ovl *o, int32_t a0, int32_t a1)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_0019C6F0) return a02_latch(o, 0x0019C6F0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_0019C6F0(o->h->ctx, a0, a1) < 0 ? a02_latch(o, 0x0019C6F0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001A2370(A02Ovl *o, uint32_t actor, uint32_t matrix)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001A2370) return a02_latch(o, 0x001A2370u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001A2370(o->h->ctx, actor, matrix) < 0 ? a02_latch(o, 0x001A2370u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001AA700(A02Ovl *o, uint32_t actor)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001AA700) return a02_latch(o, 0x001AA700u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001AA700(o->h->ctx, actor) < 0 ? a02_latch(o, 0x001AA700u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001AF780(A02Ovl *o, uint32_t *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001AF780) return a02_latch(o, 0x001AF780u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001AF780(o->h->ctx, result) < 0 ? a02_latch(o, 0x001AF780u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001AFC10(A02Ovl *o, uint32_t actor)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001AFC10) return a02_latch(o, 0x001AFC10u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, actor) < 0 ? a02_latch(o, 0x001AFC10u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001B0FD0(A02Ovl *o, uint32_t actor, int32_t *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return a02_latch(o, 0x001B0FD0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B0FD0(o->h->ctx, actor, result) < 0 ? a02_latch(o, 0x001B0FD0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001B12B0(A02Ovl *o, float goal, float current, float rate, float *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001B12B0) return a02_latch(o, 0x001B12B0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B12B0(o->h->ctx, goal, current, rate, result) < 0 ? a02_latch(o, 0x001B12B0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001B1470(A02Ovl *o, float angle, float *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001B1470) return a02_latch(o, 0x001B1470u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B1470(o->h->ctx, angle, result) < 0 ? a02_latch(o, 0x001B1470u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001B17A0(A02Ovl *o, uint32_t actor)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001B17A0) return a02_latch(o, 0x001B17A0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B17A0(o->h->ctx, actor) < 0 ? a02_latch(o, 0x001B17A0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001B18F0(A02Ovl *o, uint32_t actor, uint32_t a, uint32_t b, int32_t *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001B18F0) return a02_latch(o, 0x001B18F0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B18F0(o->h->ctx, actor, a, b, result) < 0 ? a02_latch(o, 0x001B18F0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001B1B70(A02Ovl *o, uint32_t actor)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001B1B70) return a02_latch(o, 0x001B1B70u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B1B70(o->h->ctx, actor) < 0 ? a02_latch(o, 0x001B1B70u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001B1EA0(A02Ovl *o, int32_t a0, uint32_t point, uint32_t quad, int32_t count, int32_t *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return a02_latch(o, 0x001B1EA0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B1EA0(o->h->ctx, a0, point, quad, count, result) < 0 ? a02_latch(o, 0x001B1EA0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001B6660(A02Ovl *o, uint32_t record)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001B6660) return a02_latch(o, 0x001B6660u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B6660(o->h->ctx, record) < 0 ? a02_latch(o, 0x001B6660u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001B6F00(A02Ovl *o, uint32_t actor, uint32_t point, float yaw)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001B6F00) return a02_latch(o, 0x001B6F00u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001B6F00(o->h->ctx, actor, point, yaw) < 0 ? a02_latch(o, 0x001B6F00u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001BA1A0(A02Ovl *o, uint32_t block, uint32_t script)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return a02_latch(o, 0x001BA1A0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, block, script) < 0 ? a02_latch(o, 0x001BA1A0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001BA1C0(A02Ovl *o, uint32_t actor, int32_t index, int32_t *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return a02_latch(o, 0x001BA1C0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BA1C0(o->h->ctx, actor, index, result) < 0 ? a02_latch(o, 0x001BA1C0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001BA1F0(A02Ovl *o, uint32_t actor, int32_t *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return a02_latch(o, 0x001BA1F0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, actor, result) < 0 ? a02_latch(o, 0x001BA1F0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001C6120(A02Ovl *o, uint32_t table, int32_t id, uint32_t *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001C6120) return a02_latch(o, 0x001C6120u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C6120(o->h->ctx, table, id, result) < 0 ? a02_latch(o, 0x001C6120u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001C6150(A02Ovl *o, uint32_t model, int32_t *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001C6150) return a02_latch(o, 0x001C6150u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C6150(o->h->ctx, model, result) < 0 ? a02_latch(o, 0x001C6150u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001C6380(A02Ovl *o, uint32_t actor)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001C6380) return a02_latch(o, 0x001C6380u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C6380(o->h->ctx, actor) < 0 ? a02_latch(o, 0x001C6380u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001C63E0(A02Ovl *o, uint32_t actor, int32_t a1)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001C63E0) return a02_latch(o, 0x001C63E0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C63E0(o->h->ctx, actor, a1) < 0 ? a02_latch(o, 0x001C63E0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001C64F0(A02Ovl *o, uint32_t actor, float dt)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001C64F0) return a02_latch(o, 0x001C64F0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C64F0(o->h->ctx, actor, dt) < 0 ? a02_latch(o, 0x001C64F0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001C68C0(A02Ovl *o, uint32_t actor)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001C68C0) return a02_latch(o, 0x001C68C0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001C68C0(o->h->ctx, actor) < 0 ? a02_latch(o, 0x001C68C0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001CA6E0(A02Ovl *o, uint32_t actor, uint32_t model)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001CA6E0) return a02_latch(o, 0x001CA6E0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001CA6E0(o->h->ctx, actor, model) < 0 ? a02_latch(o, 0x001CA6E0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001CB5B0(A02Ovl *o, int32_t a0)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001CB5B0) return a02_latch(o, 0x001CB5B0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001CB5B0(o->h->ctx, a0) < 0 ? a02_latch(o, 0x001CB5B0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001CD520(A02Ovl *o, int32_t bucket, int32_t mode, uint32_t point, uint64_t giftag, float w, float h, float zbias, uint32_t rgba)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001CD520) return a02_latch(o, 0x001CD520u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001CD520(o->h->ctx, bucket, mode, point, giftag, w, h, zbias, rgba) < 0 ? a02_latch(o, 0x001CD520u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001EFD20(A02Ovl *o, int32_t id, uint32_t point, uint32_t *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001EFD20) return a02_latch(o, 0x001EFD20u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001EFD20(o->h->ctx, id, point, result) < 0 ? a02_latch(o, 0x001EFD20u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001EFD90(A02Ovl *o, int32_t id, uint32_t a, uint32_t b, int32_t *result)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001EFD90) return a02_latch(o, 0x001EFD90u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001EFD90(o->h->ctx, id, a, b, result) < 0 ? a02_latch(o, 0x001EFD90u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001F02C0(A02Ovl *o, uint32_t point, int32_t id, float volume)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001F02C0) return a02_latch(o, 0x001F02C0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001F02C0(o->h->ctx, point, id, volume) < 0 ? a02_latch(o, 0x001F02C0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001F4BF0(A02Ovl *o, uint32_t a, uint32_t b)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001F4BF0) return a02_latch(o, 0x001F4BF0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001F4BF0(o->h->ctx, a, b) < 0 ? a02_latch(o, 0x001F4BF0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001F6B30(A02Ovl *o)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001F6B30) return a02_latch(o, 0x001F6B30u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001F6B30(o->h->ctx) < 0 ? a02_latch(o, 0x001F6B30u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001FA790(A02Ovl *o, int32_t a0, int32_t a1)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001FA790) return a02_latch(o, 0x001FA790u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001FA790(o->h->ctx, a0, a1) < 0 ? a02_latch(o, 0x001FA790u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001FABB0(A02Ovl *o)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001FABB0) return a02_latch(o, 0x001FABB0u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001FABB0(o->h->ctx) < 0 ? a02_latch(o, 0x001FABB0u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

static inline int a02_c_001FAE70(A02Ovl *o, int32_t a0)
{
    if (a02_failed(o)) return -1;
    if (!o->h->w_001FAE70) return a02_latch(o, 0x001FAE70u, EM_AREA02_OVL_FAULT_NULL_WORKER);
    return o->h->w_001FAE70(o->h->ctx, a0) < 0 ? a02_latch(o, 0x001FAE70u, EM_AREA02_OVL_FAULT_WORKER_FAILED) : 0;
}

/* The actor's own callback: the function at self +0x4C, called with self. */
static inline void a02_callback(A02Ovl *o, uint32_t self)
{
    uint32_t fn = a02_u32(o, self + 0x4C);
    if (a02_failed(o)) return;
    if (!o->h->w_callback) {
        a02_latch(o, fn, EM_AREA02_OVL_FAULT_NULL_WORKER);
        return;
    }
    if (o->h->w_callback(o->h->ctx, fn, self) < 0) a02_latch(o, fn, EM_AREA02_OVL_FAULT_WORKER_FAILED);
}

static inline void a02_open(A02Ovl *o, const EmArea02OvlHooks *h, EmArea02OvlFault *fault)
{
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
}

#endif /* EM_AREA02_OVERLAY_INTERNAL_H */
