/* Memory and fail-stop plumbing, shared design with em_level14_port_internal.h. */
#ifndef EM_LEVEL16_BOOT_INTERNAL_H
#define EM_LEVEL16_BOOT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level16_boot.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel16BootHooks *h;
    EmLevel16BootFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L16B;

static inline int l16b_latch(L16B *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL16_BOOT_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l16b_failed(const L16B *o) { return o->fault->code != EM_LEVEL16_BOOT_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l16b_at(L16B *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l16b_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l16b_latch(o, address, EM_LEVEL16_BOOT_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l16b_u8(L16B *o, uint32_t a) { return *l16b_at(o, a, 1); }
static inline uint32_t l16b_u16(L16B *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l16b_at(o, a, 2), 2);
    return v;
}
static inline int32_t l16b_s16(L16B *o, uint32_t a) { return (int16_t)l16b_u16(o, a); }
static inline uint32_t l16b_u32(L16B *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l16b_at(o, a, 4), 4);
    return v;
}
static inline int32_t l16b_s32(L16B *o, uint32_t a) { return (int32_t)l16b_u32(o, a); }
static inline int32_t l16b_s8(L16B *o, uint32_t a) { return (int8_t)l16b_u8(o, a); }
static inline void l16b_w8(L16B *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l16b_at(o, a, 1), &b, 1);
}
static inline void l16b_w16(L16B *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l16b_at(o, a, 2), &h, 2);
}
static inline void l16b_w32(L16B *o, uint32_t a, uint32_t v) { memcpy(l16b_at(o, a, 4), &v, 4); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l16b_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l16b_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l16b_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L16B_ADD(a, b) em_ee_add_bits((a), (b))
#define L16B_SUB(a, b) em_ee_sub_bits((a), (b))
#define L16B_MUL(a, b) em_ee_mul_bits((a), (b))
#define L16B_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L16B_LE(a, b) em_ee_c_le_bits((a), (b))
#define L16B_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L16B_DIV(a, b) em_ee_div_bits((a), (b))
#define L16B_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))

static inline int l16b_begin(L16B *o, const EmLevel16BootHooks *h, EmLevel16BootFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return l16b_failed(o) ? -1 : 0;
}

static inline int l16b_end(const L16B *o) { return l16b_failed(o) ? -1 : 0; }

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Each returns 0 on
 * success, -1 when skipped or failed. */
static inline int l16b_method(L16B *o, uint32_t self)
{
    if (l16b_failed(o)) return -1;
    const uint32_t fn = l16b_u32(o, self + 0x4C);
    if (l16b_failed(o)) return -1;
    if (!o->h->w_callback) return l16b_latch(o, fn, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_callback(o->h->ctx, fn, self) < 0) return l16b_latch(o, fn, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

/* BEGIN GENERATED WRAPPERS */
static inline int l16b_c_001026A0(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001026A0) return l16b_latch(o, 0x001026A0u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0) return l16b_latch(o, 0x001026A0u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001026D0(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001026D0) return l16b_latch(o, 0x001026D0u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001026D0(o->h->ctx, a0, a1, a2) < 0) return l16b_latch(o, 0x001026D0u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_00102738(L16B *o, uint32_t a0, uint32_t a1, uint32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00102738) return l16b_latch(o, 0x00102738u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    float r = 0;
    if (o->h->w_00102738(o->h->ctx, a0, a1, &r) < 0) return l16b_latch(o, 0x00102738u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    *out = l16b_bits(r);
    return 0;
}

static inline int l16b_c_00102760(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00102760) return l16b_latch(o, 0x00102760u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_00102760(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x00102760u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001028B8(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001028B8) return l16b_latch(o, 0x001028B8u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0) return l16b_latch(o, 0x001028B8u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001028D0(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001028D0) return l16b_latch(o, 0x001028D0u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0) return l16b_latch(o, 0x001028D0u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_00102948(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00102948) return l16b_latch(o, 0x00102948u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_00102948(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x00102948u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_00102958(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00102958) return l16b_latch(o, 0x00102958u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_00102958(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x00102958u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001029C0(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001029C0) return l16b_latch(o, 0x001029C0u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001029C0(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001029C0u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_00102C58(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00102C58) return l16b_latch(o, 0x00102C58u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_00102C58(o->h->ctx, a0, a1, a2) < 0) return l16b_latch(o, 0x00102C58u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_00103230(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_00103230) return l16b_latch(o, 0x00103230u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_00103230(o->h->ctx, a0, a1, l16b_float(a2)) < 0) return l16b_latch(o, 0x00103230u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_0011E420(L16B *o, uint32_t a0, uint32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0011E420) return l16b_latch(o, 0x0011E420u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    float r = 0;
    if (o->h->w_0011E420(o->h->ctx, l16b_float(a0), &r) < 0) return l16b_latch(o, 0x0011E420u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    *out = l16b_bits(r);
    return 0;
}

static inline int l16b_c_0017C370(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0017C370) return l16b_latch(o, 0x0017C370u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_0017C370(o->h->ctx, a0) < 0) return l16b_latch(o, 0x0017C370u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_0019A570(L16B *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0019A570) return l16b_latch(o, 0x0019A570u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_0019A570(o->h->ctx, a0, a1, a2, a3, out) < 0) return l16b_latch(o, 0x0019A570u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_0019AA80(L16B *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0019AA80) return l16b_latch(o, 0x0019AA80u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_0019AA80(o->h->ctx, a0, a1, a2, out) < 0) return l16b_latch(o, 0x0019AA80u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_0019DB50(L16B *o, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0019DB50) return l16b_latch(o, 0x0019DB50u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_0019DB50(o->h->ctx, out) < 0) return l16b_latch(o, 0x0019DB50u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001A1B80(L16B *o, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001A1B80) return l16b_latch(o, 0x001A1B80u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001A1B80(o->h->ctx, out) < 0) return l16b_latch(o, 0x001A1B80u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001AFA90(L16B *o, int32_t a0, uint32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001AFA90) return l16b_latch(o, 0x001AFA90u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001AFA90(o->h->ctx, a0, out) < 0) return l16b_latch(o, 0x001AFA90u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001AFC10(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001AFC10) return l16b_latch(o, 0x001AFC10u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001AFC10(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001AFC10u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B0FD0(L16B *o, uint32_t a0, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B0FD0) return l16b_latch(o, 0x001B0FD0u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001B0FD0(o->h->ctx, a0, out) < 0) return l16b_latch(o, 0x001B0FD0u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B1020(L16B *o, uint32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B1020) return l16b_latch(o, 0x001B1020u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001B1020(o->h->ctx, a0, a1, a2, a3, out) < 0) return l16b_latch(o, 0x001B1020u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B1190(L16B *o, int32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B1190) return l16b_latch(o, 0x001B1190u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001B1190(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001B1190u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B1380(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B1380) return l16b_latch(o, 0x001B1380u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001B1380(o->h->ctx, a0, a1, l16b_float(a2), out) < 0) return l16b_latch(o, 0x001B1380u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B15D0(L16B *o, uint32_t a0, uint32_t a1, uint32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B15D0) return l16b_latch(o, 0x001B15D0u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    float r = 0;
    if (o->h->w_001B15D0(o->h->ctx, a0, a1, &r) < 0) return l16b_latch(o, 0x001B15D0u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    *out = l16b_bits(r);
    return 0;
}

static inline int l16b_c_001B17A0(L16B *o, uint32_t a0, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B17A0) return l16b_latch(o, 0x001B17A0u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001B17A0(o->h->ctx, a0, out) < 0) return l16b_latch(o, 0x001B17A0u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B2B10(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B2B10) return l16b_latch(o, 0x001B2B10u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001B2B10(o->h->ctx, a0, a1, a2) < 0) return l16b_latch(o, 0x001B2B10u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001B39F0(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001B39F0) return l16b_latch(o, 0x001B39F0u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001B39F0(o->h->ctx, a0, a1, a2, out) < 0) return l16b_latch(o, 0x001B39F0u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001BA1A0(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001BA1A0) return l16b_latch(o, 0x001BA1A0u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001BA1A0(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x001BA1A0u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001BA1F0(L16B *o, uint32_t a0, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001BA1F0) return l16b_latch(o, 0x001BA1F0u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001BA1F0(o->h->ctx, a0, out) < 0) return l16b_latch(o, 0x001BA1F0u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001C4760(L16B *o, int32_t a0, int32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001C4760) return l16b_latch(o, 0x001C4760u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001C4760(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x001C4760u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001C47A0(L16B *o, int32_t a0, int32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001C47A0) return l16b_latch(o, 0x001C47A0u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001C47A0(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x001C47A0u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001C6380(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001C6380) return l16b_latch(o, 0x001C6380u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001C6380(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001C6380u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001CAF60(L16B *o, uint32_t a0, uint32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001CAF60) return l16b_latch(o, 0x001CAF60u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001CAF60(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x001CAF60u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001E2BA0(L16B *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001E2BA0) return l16b_latch(o, 0x001E2BA0u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001E2BA0(o->h->ctx, a0, a1, a2, l16b_float(a3)) < 0) return l16b_latch(o, 0x001E2BA0u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001EFD90(L16B *o, int32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001EFD90) return l16b_latch(o, 0x001EFD90u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001EFD90(o->h->ctx, a0, a1, a2) < 0) return l16b_latch(o, 0x001EFD90u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001F1110(L16B *o, uint32_t a0, int32_t a1)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001F1110) return l16b_latch(o, 0x001F1110u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001F1110(o->h->ctx, a0, a1) < 0) return l16b_latch(o, 0x001F1110u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001F1180(L16B *o, uint32_t a0)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001F1180) return l16b_latch(o, 0x001F1180u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001F1180(o->h->ctx, a0) < 0) return l16b_latch(o, 0x001F1180u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_001F4F40(L16B *o, int32_t a0, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_001F4F40) return l16b_latch(o, 0x001F4F40u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_001F4F40(o->h->ctx, a0, out) < 0) return l16b_latch(o, 0x001F4F40u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_0021BB00(L16B *o, uint32_t a0, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0021BB00) return l16b_latch(o, 0x0021BB00u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_0021BB00(o->h->ctx, a0, out) < 0) return l16b_latch(o, 0x0021BB00u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_0021BC40(L16B *o, uint32_t a0, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0021BC40) return l16b_latch(o, 0x0021BC40u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_0021BC40(o->h->ctx, a0, out) < 0) return l16b_latch(o, 0x0021BC40u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16b_c_0021BE40(L16B *o, uint32_t a0, uint32_t a1, int32_t *out)
{
    if (l16b_failed(o)) return -1;
    if (!o->h->w_0021BE40) return l16b_latch(o, 0x0021BE40u, EM_LEVEL16_BOOT_FAULT_NULL_WORKER);
    if (o->h->w_0021BE40(o->h->ctx, a0, a1, out) < 0) return l16b_latch(o, 0x0021BE40u, EM_LEVEL16_BOOT_FAULT_WORKER_FAILED);
    return 0;
}
/* END GENERATED WRAPPERS */

#endif
