/* Memory and fail-stop plumbing, shared design with em_level14_port_internal.h. */
#ifndef EM_LEVEL16_CREATURE_INTERNAL_H
#define EM_LEVEL16_CREATURE_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level16_creature.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel16CreatureHooks *h;
    EmLevel16CreatureFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L16C;

static inline int l16c_latch(L16C *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL16_CREATURE_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l16c_failed(const L16C *o) { return o->fault->code != EM_LEVEL16_CREATURE_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l16c_at(L16C *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l16c_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l16c_latch(o, address, EM_LEVEL16_CREATURE_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l16c_u8(L16C *o, uint32_t a) { return *l16c_at(o, a, 1); }
static inline uint32_t l16c_u16(L16C *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l16c_at(o, a, 2), 2);
    return v;
}
static inline int32_t l16c_s16(L16C *o, uint32_t a) { return (int16_t)l16c_u16(o, a); }
static inline uint32_t l16c_u32(L16C *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l16c_at(o, a, 4), 4);
    return v;
}
static inline int32_t l16c_s32(L16C *o, uint32_t a) { return (int32_t)l16c_u32(o, a); }
static inline int32_t l16c_s8(L16C *o, uint32_t a) { return (int8_t)l16c_u8(o, a); }
static inline void l16c_w8(L16C *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l16c_at(o, a, 1), &b, 1);
}
static inline void l16c_w16(L16C *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l16c_at(o, a, 2), &h, 2);
}
static inline void l16c_w32(L16C *o, uint32_t a, uint32_t v) { memcpy(l16c_at(o, a, 4), &v, 4); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l16c_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l16c_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l16c_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L16C_ADD(a, b) em_ee_add_bits((a), (b))
#define L16C_SUB(a, b) em_ee_sub_bits((a), (b))
#define L16C_MUL(a, b) em_ee_mul_bits((a), (b))
#define L16C_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L16C_LE(a, b) em_ee_c_le_bits((a), (b))
#define L16C_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L16C_DIV(a, b) em_ee_div_bits((a), (b))
#define L16C_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))

static inline int l16c_begin(L16C *o, const EmLevel16CreatureHooks *h, EmLevel16CreatureFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return l16c_failed(o) ? -1 : 0;
}

static inline int l16c_end(const L16C *o) { return l16c_failed(o) ? -1 : 0; }

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l16c_c_001026A0(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001026A0) return l16c_latch(o, 0x001026A0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0) return l16c_latch(o, 0x001026A0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001028B8(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001028B8) return l16c_latch(o, 0x001028B8u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0) return l16c_latch(o, 0x001028B8u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_00102760(L16C *o, uint32_t a0, uint32_t a1)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_00102760) return l16c_latch(o, 0x00102760u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_00102760(o->h->ctx, a0, a1) < 0) return l16c_latch(o, 0x00102760u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_00102948(L16C *o, uint32_t a0, uint32_t a1)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_00102948) return l16c_latch(o, 0x00102948u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_00102948(o->h->ctx, a0, a1) < 0) return l16c_latch(o, 0x00102948u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_00102958(L16C *o, uint32_t a0, uint32_t a1)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_00102958) return l16c_latch(o, 0x00102958u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_00102958(o->h->ctx, a0, a1) < 0) return l16c_latch(o, 0x00102958u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001029C0(L16C *o, uint32_t a0)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001029C0) return l16c_latch(o, 0x001029C0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001029C0(o->h->ctx, a0) < 0) return l16c_latch(o, 0x001029C0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_00102B08(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_00102B08) return l16c_latch(o, 0x00102B08u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_00102B08(o->h->ctx, a0, a1, l16c_float(a2)) < 0) return l16c_latch(o, 0x00102B08u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_00102BB0(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_00102BB0) return l16c_latch(o, 0x00102BB0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_00102BB0(o->h->ctx, a0, a1, l16c_float(a2)) < 0) return l16c_latch(o, 0x00102BB0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_0011DF78(L16C *o, uint32_t a0, uint32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_0011DF78) return l16c_latch(o, 0x0011DF78u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011DF78(o->h->ctx, l16c_float(a0), &out) < 0) return l16c_latch(o, 0x0011DF78u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    *result = l16c_bits(out);
    return 0;
}

static inline int l16c_c_0011E620(L16C *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_0011E620) return l16c_latch(o, 0x0011E620u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E620(o->h->ctx, l16c_float(a0), l16c_float(a1), &out) < 0) return l16c_latch(o, 0x0011E620u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    *result = l16c_bits(out);
    return 0;
}

static inline int l16c_c_00122BB8(L16C *o, int32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_00122BB8) return l16c_latch(o, 0x00122BB8u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_00122BB8(o->h->ctx, result) < 0) return l16c_latch(o, 0x00122BB8u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001418F0(L16C *o, uint32_t a0, uint32_t a1)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001418F0) return l16c_latch(o, 0x001418F0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001418F0(o->h->ctx, a0, a1) < 0) return l16c_latch(o, 0x001418F0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_0019A6F0(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t a4, int32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_0019A6F0) return l16c_latch(o, 0x0019A6F0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_0019A6F0(o->h->ctx, a0, a1, a2, a3, a4, result) < 0) return l16c_latch(o, 0x0019A6F0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001A7B80(L16C *o, uint32_t a0, int32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001A7B80) return l16c_latch(o, 0x001A7B80u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001A7B80(o->h->ctx, a0, result) < 0) return l16c_latch(o, 0x001A7B80u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001B1190(L16C *o, int32_t a0)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B1190) return l16c_latch(o, 0x001B1190u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001B1190(o->h->ctx, a0) < 0) return l16c_latch(o, 0x001B1190u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001B1240(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B1240) return l16c_latch(o, 0x001B1240u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B1240(o->h->ctx, a0, l16c_float(a1), l16c_float(a2), &out) < 0) return l16c_latch(o, 0x001B1240u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    *result = l16c_bits(out);
    return 0;
}

static inline int l16c_c_001B12B0(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B12B0) return l16c_latch(o, 0x001B12B0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B12B0(o->h->ctx, l16c_float(a0), l16c_float(a1), l16c_float(a2), &out) < 0) return l16c_latch(o, 0x001B12B0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    *result = l16c_bits(out);
    return 0;
}

static inline int l16c_c_001B13F0(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B13F0) return l16c_latch(o, 0x001B13F0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001B13F0(o->h->ctx, a0, a1, l16c_float(a2), result) < 0) return l16c_latch(o, 0x001B13F0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001B1470(L16C *o, uint32_t a0, uint32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B1470) return l16c_latch(o, 0x001B1470u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B1470(o->h->ctx, l16c_float(a0), &out) < 0) return l16c_latch(o, 0x001B1470u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    *result = l16c_bits(out);
    return 0;
}

static inline int l16c_c_001B1560(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B1560) return l16c_latch(o, 0x001B1560u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001B1560(o->h->ctx, a0, a1, l16c_float(a2), result) < 0) return l16c_latch(o, 0x001B1560u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001B15D0(L16C *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B15D0) return l16c_latch(o, 0x001B15D0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B15D0(o->h->ctx, a0, a1, &out) < 0) return l16c_latch(o, 0x001B15D0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    *result = l16c_bits(out);
    return 0;
}

static inline int l16c_c_001B2B10(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B2B10) return l16c_latch(o, 0x001B2B10u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001B2B10(o->h->ctx, a0, a1, a2) < 0) return l16c_latch(o, 0x001B2B10u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001B2BF0(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, int32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B2BF0) return l16c_latch(o, 0x001B2BF0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001B2BF0(o->h->ctx, a0, a1, a2, l16c_float(a3), result) < 0) return l16c_latch(o, 0x001B2BF0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001B3250(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B3250) return l16c_latch(o, 0x001B3250u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001B3250(o->h->ctx, a0, a1, l16c_float(a2), result) < 0) return l16c_latch(o, 0x001B3250u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001B3390(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, int32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B3390) return l16c_latch(o, 0x001B3390u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001B3390(o->h->ctx, a0, a1, a2, l16c_float(a3), result) < 0) return l16c_latch(o, 0x001B3390u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001B3670(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B3670) return l16c_latch(o, 0x001B3670u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001B3670(o->h->ctx, a0, a1, a2, result) < 0) return l16c_latch(o, 0x001B3670u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001B37D0(L16C *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B37D0) return l16c_latch(o, 0x001B37D0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B37D0(o->h->ctx, a0, l16c_float(a1), l16c_float(a2), &out) < 0) return l16c_latch(o, 0x001B37D0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    *result = l16c_bits(out);
    return 0;
}

static inline int l16c_c_001B55E0(L16C *o, uint32_t a0, int32_t a1)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001B55E0) return l16c_latch(o, 0x001B55E0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001B55E0(o->h->ctx, a0, a1) < 0) return l16c_latch(o, 0x001B55E0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001C63E0(L16C *o, uint32_t a0, int32_t a1)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001C63E0) return l16c_latch(o, 0x001C63E0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001C63E0(o->h->ctx, a0, a1) < 0) return l16c_latch(o, 0x001C63E0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001C67E0(L16C *o, uint32_t a0, int32_t a1, uint32_t a2, uint32_t a3)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001C67E0) return l16c_latch(o, 0x001C67E0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001C67E0(o->h->ctx, a0, a1, l16c_float(a2), l16c_float(a3)) < 0) return l16c_latch(o, 0x001C67E0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001E8B90(L16C *o, uint32_t a0, uint32_t a1)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001E8B90) return l16c_latch(o, 0x001E8B90u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001E8B90(o->h->ctx, a0, l16c_float(a1)) < 0) return l16c_latch(o, 0x001E8B90u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001EFD90(L16C *o, int32_t a0, uint32_t a1, uint32_t a2)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001EFD90) return l16c_latch(o, 0x001EFD90u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001EFD90(o->h->ctx, a0, a1, a2) < 0) return l16c_latch(o, 0x001EFD90u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001EFE00(L16C *o, int32_t a0, uint32_t a1, int32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001EFE00) return l16c_latch(o, 0x001EFE00u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001EFE00(o->h->ctx, a0, a1, result) < 0) return l16c_latch(o, 0x001EFE00u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001EFFD0(L16C *o, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, uint32_t a4)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001EFFD0) return l16c_latch(o, 0x001EFFD0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001EFFD0(o->h->ctx, a0, a1, a2, a3, l16c_float(a4)) < 0) return l16c_latch(o, 0x001EFFD0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_001FBD50(L16C *o, uint32_t a0, int32_t a1, int32_t a2, uint32_t a3)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_001FBD50) return l16c_latch(o, 0x001FBD50u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_001FBD50(o->h->ctx, a0, a1, a2, l16c_float(a3)) < 0) return l16c_latch(o, 0x001FBD50u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_0021BE40(L16C *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_0021BE40) return l16c_latch(o, 0x0021BE40u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_0021BE40(o->h->ctx, a0, a1, result) < 0) return l16c_latch(o, 0x0021BE40u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_0021BED0(L16C *o, uint32_t a0, int32_t *result)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_0021BED0) return l16c_latch(o, 0x0021BED0u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_0021BED0(o->h->ctx, a0, result) < 0) return l16c_latch(o, 0x0021BED0u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_0021BF90(L16C *o, uint32_t a0, uint32_t a1)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_0021BF90) return l16c_latch(o, 0x0021BF90u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_0021BF90(o->h->ctx, a0, a1) < 0) return l16c_latch(o, 0x0021BF90u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l16c_c_0021C040(L16C *o, uint32_t a0, uint32_t a1)
{
    if (l16c_failed(o)) return -1;
    if (!o->h->w_0021C040) return l16c_latch(o, 0x0021C040u, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER);
    if (o->h->w_0021C040(o->h->ctx, a0, a1) < 0) return l16c_latch(o, 0x0021C040u, EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED);
    return 0;
}
/* END GENERATED WRAPPERS */

#endif
