/* Memory and fail-stop plumbing, shared design with em_level14_port_internal.h. */
#ifndef EM_LEVEL15_SUPPORT_INTERNAL_H
#define EM_LEVEL15_SUPPORT_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level15_support.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel15SupportHooks *h;
    EmLevel15SupportFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L15S;

static inline int l15s_latch(L15S *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL15_SUPPORT_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l15s_failed(const L15S *o) { return o->fault->code != EM_LEVEL15_SUPPORT_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l15s_at(L15S *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l15s_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l15s_latch(o, address, EM_LEVEL15_SUPPORT_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l15s_u8(L15S *o, uint32_t a) { return *l15s_at(o, a, 1); }
static inline uint32_t l15s_u16(L15S *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l15s_at(o, a, 2), 2);
    return v;
}
static inline int32_t l15s_s16(L15S *o, uint32_t a) { return (int16_t)l15s_u16(o, a); }
static inline uint32_t l15s_u32(L15S *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l15s_at(o, a, 4), 4);
    return v;
}
static inline int32_t l15s_s32(L15S *o, uint32_t a) { return (int32_t)l15s_u32(o, a); }
static inline int32_t l15s_s8(L15S *o, uint32_t a) { return (int8_t)l15s_u8(o, a); }
static inline uint64_t l15s_u64(L15S *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, l15s_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void l15s_q(L15S *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, l15s_at(o, a, 8), 8);
    memcpy(out + 8, l15s_at(o, a + 8, 8), 8);
}
static inline void l15s_wq(L15S *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(l15s_at(o, a, 8), in, 8);
    memcpy(l15s_at(o, a + 8, 8), in + 8, 8);
}

static inline void l15s_w8(L15S *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l15s_at(o, a, 1), &b, 1);
}
static inline void l15s_w16(L15S *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l15s_at(o, a, 2), &h, 2);
}
static inline void l15s_w32(L15S *o, uint32_t a, uint32_t v) { memcpy(l15s_at(o, a, 4), &v, 4); }
static inline void l15s_w64(L15S *o, uint32_t a, uint64_t v) { memcpy(l15s_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l15s_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l15s_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l15s_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L15S_ADD(a, b) em_ee_add_bits((a), (b))
#define L15S_SUB(a, b) em_ee_sub_bits((a), (b))
#define L15S_MUL(a, b) em_ee_mul_bits((a), (b))
#define L15S_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L15S_LE(a, b) em_ee_c_le_bits((a), (b))
#define L15S_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L15S_DIV(a, b) em_ee_div_bits((a), (b))
#define L15S_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define L15S_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define L15S_MULA(a, b) em_ee_mula_bits((a), (b))
#define L15S_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))

static inline int l15s_begin(L15S *o, const EmLevel15SupportHooks *h, EmLevel15SupportFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return l15s_failed(o) ? -1 : 0;
}

static inline int l15s_end(const L15S *o) { return l15s_failed(o) ? -1 : 0; }

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l15s_c_001026A0(L15S *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001026A0) return l15s_latch(o, 0x001026A0u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0) return l15s_latch(o, 0x001026A0u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001028B8(L15S *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001028B8) return l15s_latch(o, 0x001028B8u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0) return l15s_latch(o, 0x001028B8u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_00102948(L15S *o, uint32_t a0, uint32_t a1)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_00102948) return l15s_latch(o, 0x00102948u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_00102948(o->h->ctx, a0, a1) < 0) return l15s_latch(o, 0x00102948u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001029C0(L15S *o, uint32_t a0)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001029C0) return l15s_latch(o, 0x001029C0u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001029C0(o->h->ctx, a0) < 0) return l15s_latch(o, 0x001029C0u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_00102BB0(L15S *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_00102BB0) return l15s_latch(o, 0x00102BB0u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_00102BB0(o->h->ctx, a0, a1, l15s_float(a2)) < 0) return l15s_latch(o, 0x00102BB0u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_0011E620(L15S *o, uint32_t a0, uint32_t a1, uint32_t *result)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_0011E620) return l15s_latch(o, 0x0011E620u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E620(o->h->ctx, l15s_float(a0), l15s_float(a1), &out) < 0) return l15s_latch(o, 0x0011E620u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    *result = l15s_bits(out);
    return 0;
}

static inline int l15s_c_001281C0(L15S *o, uint32_t a0, int32_t *result)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001281C0) return l15s_latch(o, 0x001281C0u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001281C0(o->h->ctx, l15s_float(a0), &out) < 0) return l15s_latch(o, 0x001281C0u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15s_c_00184D20(L15S *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_00184D20) return l15s_latch(o, 0x00184D20u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_00184D20(o->h->ctx, a0, a1, a2, &out) < 0) return l15s_latch(o, 0x00184D20u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15s_c_0019B4C0(L15S *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_0019B4C0) return l15s_latch(o, 0x0019B4C0u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_0019B4C0(o->h->ctx, a0, a1, a2, a3) < 0) return l15s_latch(o, 0x0019B4C0u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001AFA90(L15S *o, int32_t a0, uint32_t *result)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001AFA90) return l15s_latch(o, 0x001AFA90u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    uint32_t out = 0;
    if (o->h->w_001AFA90(o->h->ctx, a0, &out) < 0) return l15s_latch(o, 0x001AFA90u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15s_c_001AFC10(L15S *o, uint32_t a0)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001AFC10) return l15s_latch(o, 0x001AFC10u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001AFC10(o->h->ctx, a0) < 0) return l15s_latch(o, 0x001AFC10u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001B1190(L15S *o, int32_t a0)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001B1190) return l15s_latch(o, 0x001B1190u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001B1190(o->h->ctx, a0) < 0) return l15s_latch(o, 0x001B1190u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001B1470(L15S *o, uint32_t a0, uint32_t *result)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001B1470) return l15s_latch(o, 0x001B1470u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B1470(o->h->ctx, l15s_float(a0), &out) < 0) return l15s_latch(o, 0x001B1470u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    *result = l15s_bits(out);
    return 0;
}

static inline int l15s_c_001B3F10(L15S *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001B3F10) return l15s_latch(o, 0x001B3F10u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B3F10(o->h->ctx, a0, l15s_float(a1), l15s_float(a2), &out) < 0) return l15s_latch(o, 0x001B3F10u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15s_c_001C7420(L15S *o, uint32_t a0, int32_t a1, int32_t a2)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001C7420) return l15s_latch(o, 0x001C7420u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001C7420(o->h->ctx, a0, a1, a2) < 0) return l15s_latch(o, 0x001C7420u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001CAAC0(L15S *o, uint32_t a0, uint32_t a1)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001CAAC0) return l15s_latch(o, 0x001CAAC0u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001CAAC0(o->h->ctx, a0, a1) < 0) return l15s_latch(o, 0x001CAAC0u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001D0D40(L15S *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001D0D40) return l15s_latch(o, 0x001D0D40u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001D0D40(o->h->ctx, a0, a1, a2, a3) < 0) return l15s_latch(o, 0x001D0D40u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001D1F20(L15S *o, int32_t a0)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001D1F20) return l15s_latch(o, 0x001D1F20u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001D1F20(o->h->ctx, a0) < 0) return l15s_latch(o, 0x001D1F20u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001D1F80(L15S *o, int32_t a0, int32_t a1, int32_t a2)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001D1F80) return l15s_latch(o, 0x001D1F80u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001D1F80(o->h->ctx, a0, a1, a2) < 0) return l15s_latch(o, 0x001D1F80u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001D1FF0(L15S *o, int32_t a0, int32_t a1)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001D1FF0) return l15s_latch(o, 0x001D1FF0u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001D1FF0(o->h->ctx, a0, a1) < 0) return l15s_latch(o, 0x001D1FF0u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001D2090(L15S *o, int32_t a0, uint32_t a1)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001D2090) return l15s_latch(o, 0x001D2090u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001D2090(o->h->ctx, a0, a1) < 0) return l15s_latch(o, 0x001D2090u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001D2910(L15S *o, int32_t a0, int32_t *result)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001D2910) return l15s_latch(o, 0x001D2910u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001D2910(o->h->ctx, a0, &out) < 0) return l15s_latch(o, 0x001D2910u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15s_c_001D6930(L15S *o, int32_t a0, int32_t a1, int32_t a2, int32_t a3, uint32_t a4)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001D6930) return l15s_latch(o, 0x001D6930u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001D6930(o->h->ctx, a0, a1, a2, a3, a4) < 0) return l15s_latch(o, 0x001D6930u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001D6BA0(L15S *o, int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001D6BA0) return l15s_latch(o, 0x001D6BA0u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001D6BA0(o->h->ctx, a0, a1, a2, a3, a4, a5) < 0) return l15s_latch(o, 0x001D6BA0u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001D8C20(L15S *o, int32_t a0)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001D8C20) return l15s_latch(o, 0x001D8C20u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_001D8C20(o->h->ctx, a0) < 0) return l15s_latch(o, 0x001D8C20u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_00207D00(L15S *o, int32_t a0, int32_t a1)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_00207D00) return l15s_latch(o, 0x00207D00u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_00207D00(o->h->ctx, a0, a1) < 0) return l15s_latch(o, 0x00207D00u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_00207E40(L15S *o, int32_t a0, int32_t a1, int32_t a2, int32_t a3, uint32_t a4, uint32_t a5, uint64_t a6)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_00207E40) return l15s_latch(o, 0x00207E40u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_00207E40(o->h->ctx, a0, a1, a2, a3, a4, a5, a6) < 0) return l15s_latch(o, 0x00207E40u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_0020A7A0(L15S *o, uint64_t a0)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_0020A7A0) return l15s_latch(o, 0x0020A7A0u, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER);
    if (o->h->w_0020A7A0(o->h->ctx, a0) < 0) return l15s_latch(o, 0x0020A7A0u, EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED);
    return 0;
}

/* END GENERATED WRAPPERS */
#endif
