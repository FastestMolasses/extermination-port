/* Shared plumbing of em_area04_port.c: the memory view, fault latch and
 * hook-call helpers (the design of em_area02_overlay_internal.h). Not a
 * public interface. */
#ifndef EM_AREA04_PORT_INTERNAL_H
#define EM_AREA04_PORT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area04_port.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea04PortHooks *h;
    EmArea04PortFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A04;

static inline int a04_latch(A04 *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA04_PORT_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int a04_failed(const A04 *o) { return o->fault->code != EM_AREA04_PORT_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *a04_at(A04 *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!a04_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) a04_latch(o, address, EM_AREA04_PORT_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t a04_u8(A04 *o, uint32_t a) { return *a04_at(o, a, 1); }
static inline uint32_t a04_u16(A04 *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, a04_at(o, a, 2), 2);
    return v;
}
static inline uint32_t a04_u32(A04 *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, a04_at(o, a, 4), 4);
    return v;
}
static inline uint64_t a04_u64(A04 *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, a04_at(o, a, 8), 8);
    return v;
}

static inline void a04_w8(A04 *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(a04_at(o, a, 1), &b, 1);
}
static inline void a04_w16(A04 *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(a04_at(o, a, 2), &h, 2);
}
static inline void a04_w32(A04 *o, uint32_t a, uint32_t v) { memcpy(a04_at(o, a, 4), &v, 4); }
static inline void a04_w64(A04 *o, uint32_t a, uint64_t v) { memcpy(a04_at(o, a, 8), &v, 8); }

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the call.
 * Each returns 0 on success, -1 when skipped or failed. */
static inline int a04_c_0011DE90(A04 *o, float f0, float *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_0011DE90) return a04_latch(o, 0x0011DE90u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011DE90(o->h->ctx, f0, result) < 0 ? a04_latch(o, 0x0011DE90u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_0011DF78(A04 *o, float f0, float *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_0011DF78) return a04_latch(o, 0x0011DF78u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011DF78(o->h->ctx, f0, result) < 0 ? a04_latch(o, 0x0011DF78u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_0011E2A8(A04 *o, float f0, float *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return a04_latch(o, 0x0011E2A8u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011E2A8(o->h->ctx, f0, result) < 0 ? a04_latch(o, 0x0011E2A8u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_0011E748(A04 *o, float f0, float *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_0011E748) return a04_latch(o, 0x0011E748u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011E748(o->h->ctx, f0, result) < 0 ? a04_latch(o, 0x0011E748u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001028D0(A04 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001028D0) return a04_latch(o, 0x001028D0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0 ? a04_latch(o, 0x001028D0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_00102900(A04 *o, uint32_t a0, uint32_t a1, float f2)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_00102900) return a04_latch(o, 0x00102900u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102900(o->h->ctx, a0, a1, f2) < 0 ? a04_latch(o, 0x00102900u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_00102948(A04 *o, uint32_t a0, uint32_t a1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_00102948) return a04_latch(o, 0x00102948u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, a0, a1) < 0 ? a04_latch(o, 0x00102948u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_00121870(A04 *o, uint32_t a0, uint32_t a1, int32_t n2)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_00121870) return a04_latch(o, 0x00121870u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_00121870(o->h->ctx, a0, a1, n2) < 0 ? a04_latch(o, 0x00121870u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_00122BB8(A04 *o, int32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_00122BB8) return a04_latch(o, 0x00122BB8u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? a04_latch(o, 0x00122BB8u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_00182F90(A04 *o, uint32_t a0, uint32_t a1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_00182F90) return a04_latch(o, 0x00182F90u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_00182F90(o->h->ctx, a0, a1) < 0 ? a04_latch(o, 0x00182F90u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_00183010(A04 *o, uint32_t a0, uint32_t a1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_00183010) return a04_latch(o, 0x00183010u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_00183010(o->h->ctx, a0, a1) < 0 ? a04_latch(o, 0x00183010u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_00187EC0(A04 *o, int32_t n0, int32_t n1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_00187EC0) return a04_latch(o, 0x00187EC0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_00187EC0(o->h->ctx, n0, n1) < 0 ? a04_latch(o, 0x00187EC0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001A2370(A04 *o, uint32_t a0, uint32_t a1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001A2370) return a04_latch(o, 0x001A2370u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001A2370(o->h->ctx, a0, a1) < 0 ? a04_latch(o, 0x001A2370u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001AF890(A04 *o, uint32_t a0)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001AF890) return a04_latch(o, 0x001AF890u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AF890(o->h->ctx, a0) < 0 ? a04_latch(o, 0x001AF890u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001AFC10(A04 *o, uint32_t a0)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001AFC10) return a04_latch(o, 0x001AFC10u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001AFC10(o->h->ctx, a0) < 0 ? a04_latch(o, 0x001AFC10u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001B0FD0(A04 *o, uint32_t a0, int32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return a04_latch(o, 0x001B0FD0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B0FD0(o->h->ctx, a0, result) < 0 ? a04_latch(o, 0x001B0FD0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001B10B0(A04 *o, uint32_t a0, int32_t n1, int32_t n2, int32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001B10B0) return a04_latch(o, 0x001B10B0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B10B0(o->h->ctx, a0, n1, n2, result) < 0 ? a04_latch(o, 0x001B10B0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001B1240(A04 *o, uint32_t a0, float f1, float f2, float *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001B1240) return a04_latch(o, 0x001B1240u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1240(o->h->ctx, a0, f1, f2, result) < 0 ? a04_latch(o, 0x001B1240u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001B1470(A04 *o, float f0, float *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001B1470) return a04_latch(o, 0x001B1470u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1470(o->h->ctx, f0, result) < 0 ? a04_latch(o, 0x001B1470u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001B17A0(A04 *o, uint32_t a0, int32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001B17A0) return a04_latch(o, 0x001B17A0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B17A0(o->h->ctx, a0, result) < 0 ? a04_latch(o, 0x001B17A0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001B1B70(A04 *o, uint32_t a0)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001B1B70) return a04_latch(o, 0x001B1B70u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1B70(o->h->ctx, a0) < 0 ? a04_latch(o, 0x001B1B70u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001B1DE0(A04 *o, uint32_t a0)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001B1DE0) return a04_latch(o, 0x001B1DE0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1DE0(o->h->ctx, a0) < 0 ? a04_latch(o, 0x001B1DE0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001B1E20(A04 *o, int32_t n0, int32_t n1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001B1E20) return a04_latch(o, 0x001B1E20u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1E20(o->h->ctx, n0, n1) < 0 ? a04_latch(o, 0x001B1E20u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001B1EA0(A04 *o, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001B1EA0) return a04_latch(o, 0x001B1EA0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1EA0(o->h->ctx, n0, a1, a2, n3, result) < 0 ? a04_latch(o, 0x001B1EA0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001B6660(A04 *o, uint32_t a0, uint32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001B6660) return a04_latch(o, 0x001B6660u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B6660(o->h->ctx, a0, result) < 0 ? a04_latch(o, 0x001B6660u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001B6F80(A04 *o, uint32_t a0, float f1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001B6F80) return a04_latch(o, 0x001B6F80u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B6F80(o->h->ctx, a0, f1) < 0 ? a04_latch(o, 0x001B6F80u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001BA1A0(A04 *o, uint32_t a0, uint32_t a1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return a04_latch(o, 0x001BA1A0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0 ? a04_latch(o, 0x001BA1A0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001BA1C0(A04 *o, uint32_t a0, int32_t n1, int32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return a04_latch(o, 0x001BA1C0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1C0(o->h->ctx, a0, n1, result) < 0 ? a04_latch(o, 0x001BA1C0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001BA1F0(A04 *o, uint32_t a0, int32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return a04_latch(o, 0x001BA1F0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA1F0(o->h->ctx, a0, result) < 0 ? a04_latch(o, 0x001BA1F0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001BA580(A04 *o, uint32_t a0, int32_t n1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001BA580) return a04_latch(o, 0x001BA580u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BA580(o->h->ctx, a0, n1) < 0 ? a04_latch(o, 0x001BA580u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001BB520(A04 *o, uint32_t a0, uint32_t a1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001BB520) return a04_latch(o, 0x001BB520u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BB520(o->h->ctx, a0, a1) < 0 ? a04_latch(o, 0x001BB520u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001BB560(A04 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001BB560) return a04_latch(o, 0x001BB560u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BB560(o->h->ctx, a0, a1, n2, result) < 0 ? a04_latch(o, 0x001BB560u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001BB7C0(A04 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001BB7C0) return a04_latch(o, 0x001BB7C0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BB7C0(o->h->ctx, a0, a1, result) < 0 ? a04_latch(o, 0x001BB7C0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001BB7F0(A04 *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001BB7F0) return a04_latch(o, 0x001BB7F0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BB7F0(o->h->ctx, a0, a1, result) < 0 ? a04_latch(o, 0x001BB7F0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001BC150(A04 *o, uint32_t a0, uint32_t a1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001BC150) return a04_latch(o, 0x001BC150u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001BC150(o->h->ctx, a0, a1) < 0 ? a04_latch(o, 0x001BC150u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001C4760(A04 *o, int32_t n0, int32_t n1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001C4760) return a04_latch(o, 0x001C4760u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C4760(o->h->ctx, n0, n1) < 0 ? a04_latch(o, 0x001C4760u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001C47A0(A04 *o, int32_t n0, int32_t n1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001C47A0) return a04_latch(o, 0x001C47A0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C47A0(o->h->ctx, n0, n1) < 0 ? a04_latch(o, 0x001C47A0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001C5570(A04 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, uint32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001C5570) return a04_latch(o, 0x001C5570u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C5570(o->h->ctx, a0, a1, n2, n3, result) < 0 ? a04_latch(o, 0x001C5570u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001C6120(A04 *o, uint32_t a0, int32_t n1, uint32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001C6120) return a04_latch(o, 0x001C6120u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6120(o->h->ctx, a0, n1, result) < 0 ? a04_latch(o, 0x001C6120u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001C62C0(A04 *o, uint32_t a0)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001C62C0) return a04_latch(o, 0x001C62C0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C62C0(o->h->ctx, a0) < 0 ? a04_latch(o, 0x001C62C0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001C6380(A04 *o, uint32_t a0)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001C6380) return a04_latch(o, 0x001C6380u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C6380(o->h->ctx, a0) < 0 ? a04_latch(o, 0x001C6380u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001C63E0(A04 *o, uint32_t a0, int32_t n1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001C63E0) return a04_latch(o, 0x001C63E0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C63E0(o->h->ctx, a0, n1) < 0 ? a04_latch(o, 0x001C63E0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001C64F0(A04 *o, uint32_t a0, float f1, int32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001C64F0) return a04_latch(o, 0x001C64F0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C64F0(o->h->ctx, a0, f1, result) < 0 ? a04_latch(o, 0x001C64F0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001C67E0(A04 *o, uint32_t a0, int32_t n1, float f2, float f3)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001C67E0) return a04_latch(o, 0x001C67E0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C67E0(o->h->ctx, a0, n1, f2, f3) < 0 ? a04_latch(o, 0x001C67E0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001C68C0(A04 *o, uint32_t a0)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001C68C0) return a04_latch(o, 0x001C68C0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C68C0(o->h->ctx, a0) < 0 ? a04_latch(o, 0x001C68C0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001CA5E0(A04 *o, uint32_t a0, uint32_t a1, int32_t n2)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001CA5E0) return a04_latch(o, 0x001CA5E0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA5E0(o->h->ctx, a0, a1, n2) < 0 ? a04_latch(o, 0x001CA5E0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001CA6E0(A04 *o, uint32_t a0, uint32_t a1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001CA6E0) return a04_latch(o, 0x001CA6E0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001CA6E0(o->h->ctx, a0, a1) < 0 ? a04_latch(o, 0x001CA6E0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001D0C80(A04 *o, uint32_t a0, uint32_t a1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001D0C80) return a04_latch(o, 0x001D0C80u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D0C80(o->h->ctx, a0, a1) < 0 ? a04_latch(o, 0x001D0C80u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001D0D40(A04 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001D0D40) return a04_latch(o, 0x001D0D40u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D0D40(o->h->ctx, a0, a1, n2, n3) < 0 ? a04_latch(o, 0x001D0D40u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001D0D60(A04 *o, uint32_t a0, float f1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001D0D60) return a04_latch(o, 0x001D0D60u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001D0D60(o->h->ctx, a0, f1) < 0 ? a04_latch(o, 0x001D0D60u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001EFD20(A04 *o, int32_t n0, uint32_t a1, uint32_t *result)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001EFD20) return a04_latch(o, 0x001EFD20u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001EFD20(o->h->ctx, n0, a1, result) < 0 ? a04_latch(o, 0x001EFD20u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001F4BF0(A04 *o, uint32_t a0, uint32_t a1)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001F4BF0) return a04_latch(o, 0x001F4BF0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001F4BF0(o->h->ctx, a0, a1) < 0 ? a04_latch(o, 0x001F4BF0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001FABB0(A04 *o)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001FABB0) return a04_latch(o, 0x001FABB0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FABB0(o->h->ctx) < 0 ? a04_latch(o, 0x001FABB0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001FAE70(A04 *o, int32_t n0)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001FAE70) return a04_latch(o, 0x001FAE70u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FAE70(o->h->ctx, n0) < 0 ? a04_latch(o, 0x001FAE70u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001FB0B0(A04 *o, int32_t n0)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001FB0B0) return a04_latch(o, 0x001FB0B0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FB0B0(o->h->ctx, n0) < 0 ? a04_latch(o, 0x001FB0B0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001FB9F0(A04 *o, int32_t n0, int32_t n1, int32_t n2, int32_t n3)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001FB9F0) return a04_latch(o, 0x001FB9F0u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FB9F0(o->h->ctx, n0, n1, n2, n3) < 0 ? a04_latch(o, 0x001FB9F0u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a04_c_001FBD50(A04 *o, uint32_t a0, int32_t n1, int32_t n2, float f3)
{
    if (a04_failed(o)) return -1;
    if (!o->h->w_001FBD50) return a04_latch(o, 0x001FBD50u, EM_AREA04_PORT_FAULT_NULL_WORKER);
    return o->h->w_001FBD50(o->h->ctx, a0, n1, n2, f3) < 0 ? a04_latch(o, 0x001FBD50u, EM_AREA04_PORT_FAULT_WORKER_FAILED) : 0;
}

/* The actor's own callback: the function at self +0x4C, called with self. */
static inline void a04_callback(A04 *o, uint32_t self)
{
    uint32_t fn = a04_u32(o, self + 0x4C);
    if (a04_failed(o)) return;
    if (!o->h->w_callback) {
        a04_latch(o, fn, EM_AREA04_PORT_FAULT_NULL_WORKER);
        return;
    }
    if (o->h->w_callback(o->h->ctx, fn, self) < 0) a04_latch(o, fn, EM_AREA04_PORT_FAULT_WORKER_FAILED);
}

static inline void a04_open(A04 *o, const EmArea04PortHooks *h, EmArea04PortFault *fault)
{
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
}

#endif /* EM_AREA04_PORT_INTERNAL_H */
