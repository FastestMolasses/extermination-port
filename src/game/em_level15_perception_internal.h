/* Memory and fail-stop plumbing, shared design with em_level14_port_internal.h. */
#ifndef EM_LEVEL15_PERCEPTION_INTERNAL_H
#define EM_LEVEL15_PERCEPTION_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level15_perception.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel15PerceptionHooks *h;
    EmLevel15PerceptionFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L15P;

static inline int l15p_latch(L15P *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL15_PERCEPTION_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l15p_failed(const L15P *o) { return o->fault->code != EM_LEVEL15_PERCEPTION_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l15p_at(L15P *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l15p_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l15p_latch(o, address, EM_LEVEL15_PERCEPTION_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l15p_u8(L15P *o, uint32_t a) { return *l15p_at(o, a, 1); }
static inline uint32_t l15p_u16(L15P *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l15p_at(o, a, 2), 2);
    return v;
}
static inline int32_t l15p_s16(L15P *o, uint32_t a) { return (int16_t)l15p_u16(o, a); }
static inline uint32_t l15p_u32(L15P *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l15p_at(o, a, 4), 4);
    return v;
}
static inline int32_t l15p_s32(L15P *o, uint32_t a) { return (int32_t)l15p_u32(o, a); }
static inline int32_t l15p_s8(L15P *o, uint32_t a) { return (int8_t)l15p_u8(o, a); }
static inline uint64_t l15p_u64(L15P *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, l15p_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void l15p_q(L15P *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, l15p_at(o, a, 8), 8);
    memcpy(out + 8, l15p_at(o, a + 8, 8), 8);
}
static inline void l15p_wq(L15P *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(l15p_at(o, a, 8), in, 8);
    memcpy(l15p_at(o, a + 8, 8), in + 8, 8);
}

static inline void l15p_w8(L15P *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l15p_at(o, a, 1), &b, 1);
}
static inline void l15p_w16(L15P *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l15p_at(o, a, 2), &h, 2);
}
static inline void l15p_w32(L15P *o, uint32_t a, uint32_t v) { memcpy(l15p_at(o, a, 4), &v, 4); }
static inline void l15p_w64(L15P *o, uint32_t a, uint64_t v) { memcpy(l15p_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l15p_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l15p_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l15p_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L15P_ADD(a, b) em_ee_add_bits((a), (b))
#define L15P_SUB(a, b) em_ee_sub_bits((a), (b))
#define L15P_MUL(a, b) em_ee_mul_bits((a), (b))
#define L15P_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L15P_LE(a, b) em_ee_c_le_bits((a), (b))
#define L15P_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L15P_DIV(a, b) em_ee_div_bits((a), (b))
#define L15P_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define L15P_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define L15P_MULA(a, b) em_ee_mula_bits((a), (b))
#define L15P_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))

static inline int l15p_begin(L15P *o, const EmLevel15PerceptionHooks *h, EmLevel15PerceptionFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return l15p_failed(o) ? -1 : 0;
}

static inline int l15p_end(const L15P *o) { return l15p_failed(o) ? -1 : 0; }

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l15p_c_0011DF78(L15P *o, uint32_t a0, uint32_t *result)
{
    if (l15p_failed(o)) return -1;
    if (!o->h->w_0011DF78) return l15p_latch(o, 0x0011DF78u, EM_LEVEL15_PERCEPTION_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011DF78(o->h->ctx, l15p_float(a0), &out) < 0)
        return l15p_latch(o, 0x0011DF78u, EM_LEVEL15_PERCEPTION_FAULT_WORKER_FAILED);
    *result = l15p_bits(out);
    return 0;
}

static inline int l15p_c_0011E620(L15P *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l15p_failed(o)) return -1;
    if (!o->h->w_0011E620) return l15p_latch(o, 0x0011E620u, EM_LEVEL15_PERCEPTION_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E620(o->h->ctx, l15p_float(a0), l15p_float(a1), &out) < 0)
        return l15p_latch(o, 0x0011E620u, EM_LEVEL15_PERCEPTION_FAULT_WORKER_FAILED);
    *result = l15p_bits(out);
    return 0;
}

static inline int l15p_c_0011E748(L15P *o, uint32_t a0, uint32_t *result)
{
    if (l15p_failed(o)) return -1;
    if (!o->h->w_0011E748) return l15p_latch(o, 0x0011E748u, EM_LEVEL15_PERCEPTION_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E748(o->h->ctx, l15p_float(a0), &out) < 0)
        return l15p_latch(o, 0x0011E748u, EM_LEVEL15_PERCEPTION_FAULT_WORKER_FAILED);
    *result = l15p_bits(out);
    return 0;
}

static inline int l15p_c_001B1470(L15P *o, uint32_t a0, uint32_t *result)
{
    if (l15p_failed(o)) return -1;
    if (!o->h->w_001B1470) return l15p_latch(o, 0x001B1470u, EM_LEVEL15_PERCEPTION_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B1470(o->h->ctx, l15p_float(a0), &out) < 0)
        return l15p_latch(o, 0x001B1470u, EM_LEVEL15_PERCEPTION_FAULT_WORKER_FAILED);
    *result = l15p_bits(out);
    return 0;
}

/* END GENERATED WRAPPERS */
#endif
