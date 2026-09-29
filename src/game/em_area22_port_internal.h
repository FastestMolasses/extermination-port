/* Shared plumbing of em_area22_port.c: the memory view, fault latch and
 * hook-call helpers (the design of em_area04_port_internal.h). Not a
 * public interface. */
#ifndef EM_AREA22_PORT_INTERNAL_H
#define EM_AREA22_PORT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area22_port.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea22PortHooks *h;
    EmArea22PortFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A22;

static inline int a22_latch(A22 *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA22_PORT_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int a22_failed(const A22 *o) { return o->fault->code != EM_AREA22_PORT_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *a22_at(A22 *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!a22_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) a22_latch(o, address, EM_AREA22_PORT_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t a22_u8(A22 *o, uint32_t a) { return *a22_at(o, a, 1); }
static inline uint32_t a22_u16(A22 *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, a22_at(o, a, 2), 2);
    return v;
}
static inline uint32_t a22_u32(A22 *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, a22_at(o, a, 4), 4);
    return v;
}

static inline void a22_w8(A22 *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(a22_at(o, a, 1), &b, 1);
}
static inline void a22_w16(A22 *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(a22_at(o, a, 2), &h, 2);
}
static inline void a22_w32(A22 *o, uint32_t a, uint32_t v) { memcpy(a22_at(o, a, 4), &v, 4); }

/* A float argument or result as its bits, and back (no conversion). */
static inline uint32_t a22_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float a22_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the call.
 * Each returns 0 on success, -1 when skipped or failed. */
static inline int a22_c_0011DE90(A22 *o, float f0, float *result)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_0011DE90) return a22_latch(o, 0x0011DE90u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011DE90(o->h->ctx, f0, result) < 0 ? a22_latch(o, 0x0011DE90u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_0011DF78(A22 *o, float f0, float *result)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_0011DF78) return a22_latch(o, 0x0011DF78u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011DF78(o->h->ctx, f0, result) < 0 ? a22_latch(o, 0x0011DF78u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_0011E2A8(A22 *o, float f0, float *result)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return a22_latch(o, 0x0011E2A8u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_0011E2A8(o->h->ctx, f0, result) < 0 ? a22_latch(o, 0x0011E2A8u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_001028B8(A22 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_001028B8) return a22_latch(o, 0x001028B8u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0 ? a22_latch(o, 0x001028B8u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_00102948(A22 *o, uint32_t a0, uint32_t a1)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_00102948) return a22_latch(o, 0x00102948u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_00102948(o->h->ctx, a0, a1) < 0 ? a22_latch(o, 0x00102948u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_00122BB8(A22 *o, int32_t *result)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_00122BB8) return a22_latch(o, 0x00122BB8u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_00122BB8(o->h->ctx, result) < 0 ? a22_latch(o, 0x00122BB8u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_0012E070(A22 *o, uint32_t a0)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_0012E070) return a22_latch(o, 0x0012E070u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_0012E070(o->h->ctx, a0) < 0 ? a22_latch(o, 0x0012E070u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_001274B0(A22 *o, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t *result)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_001274B0) return a22_latch(o, 0x001274B0u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_001274B0(o->h->ctx, a0, a1, a2, a3, result) < 0 ? a22_latch(o, 0x001274B0u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_00128640(A22 *o, uint32_t a0, int32_t *result)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_00128640) return a22_latch(o, 0x00128640u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_00128640(o->h->ctx, a0, result) < 0 ? a22_latch(o, 0x00128640u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_001287F0(A22 *o, uint32_t a0, uint32_t a1, int32_t n2, float f3)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_001287F0) return a22_latch(o, 0x001287F0u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_001287F0(o->h->ctx, a0, a1, n2, f3) < 0 ? a22_latch(o, 0x001287F0u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_00128830(A22 *o, uint32_t a0, float f1, float f2, float f3)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_00128830) return a22_latch(o, 0x00128830u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_00128830(o->h->ctx, a0, f1, f2, f3) < 0 ? a22_latch(o, 0x00128830u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_0018C0C0(A22 *o, uint32_t a0)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_0018C0C0) return a22_latch(o, 0x0018C0C0u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018C0C0(o->h->ctx, a0) < 0 ? a22_latch(o, 0x0018C0C0u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_0018C4B0(A22 *o, uint32_t a0, float f1, float f2)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_0018C4B0) return a22_latch(o, 0x0018C4B0u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018C4B0(o->h->ctx, a0, f1, f2) < 0 ? a22_latch(o, 0x0018C4B0u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_0018C6A0(A22 *o, uint32_t a0, uint32_t a1, float f2)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_0018C6A0) return a22_latch(o, 0x0018C6A0u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018C6A0(o->h->ctx, a0, a1, f2) < 0 ? a22_latch(o, 0x0018C6A0u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_0018D7B0(A22 *o, uint32_t a0, int32_t n1)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_0018D7B0) return a22_latch(o, 0x0018D7B0u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_0018D7B0(o->h->ctx, a0, n1) < 0 ? a22_latch(o, 0x0018D7B0u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_001B1470(A22 *o, float f0, float *result)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_001B1470) return a22_latch(o, 0x001B1470u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B1470(o->h->ctx, f0, result) < 0 ? a22_latch(o, 0x001B1470u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_001B5360(A22 *o, uint32_t a0)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_001B5360) return a22_latch(o, 0x001B5360u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_001B5360(o->h->ctx, a0) < 0 ? a22_latch(o, 0x001B5360u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_001C2770(A22 *o, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_001C2770) return a22_latch(o, 0x001C2770u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C2770(o->h->ctx, a0, a1, n2, result) < 0 ? a22_latch(o, 0x001C2770u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline int a22_c_001C3D60(A22 *o, uint32_t a0, uint32_t a1)
{
    if (a22_failed(o)) return -1;
    if (!o->h->w_001C3D60) return a22_latch(o, 0x001C3D60u, EM_AREA22_PORT_FAULT_NULL_WORKER);
    return o->h->w_001C3D60(o->h->ctx, a0, a1) < 0 ? a22_latch(o, 0x001C3D60u, EM_AREA22_PORT_FAULT_WORKER_FAILED) : 0;
}

static inline void a22_open(A22 *o, const EmArea22PortHooks *h, EmArea22PortFault *fault)
{
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
}

#endif /* EM_AREA22_PORT_INTERNAL_H */
