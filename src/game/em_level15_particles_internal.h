/* Memory and fail-stop plumbing, shared design with em_level14_port_internal.h. */
#ifndef EM_LEVEL15_PARTICLES_INTERNAL_H
#define EM_LEVEL15_PARTICLES_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level15_particles.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel15ParticlesHooks *h;
    EmLevel15ParticlesFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L15S;

static inline int l15s_latch(L15S *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL15_PARTICLES_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l15s_failed(const L15S *o) { return o->fault->code != EM_LEVEL15_PARTICLES_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l15s_at(L15S *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l15s_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l15s_latch(o, address, EM_LEVEL15_PARTICLES_FAULT_BAD_ADDRESS);
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

static inline int l15s_begin(L15S *o, const EmLevel15ParticlesHooks *h, EmLevel15ParticlesFault *fault)
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
static inline int l15s_c_00102918(L15S *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_00102918) return l15s_latch(o, 0x00102918u, EM_LEVEL15_PARTICLES_FAULT_NULL_WORKER);
    if (o->h->w_00102918(o->h->ctx, a0, a1, a2) < 0)
        return l15s_latch(o, 0x00102918u, EM_LEVEL15_PARTICLES_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001029C0(L15S *o, uint32_t a0)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001029C0) return l15s_latch(o, 0x001029C0u, EM_LEVEL15_PARTICLES_FAULT_NULL_WORKER);
    if (o->h->w_001029C0(o->h->ctx, a0) < 0)
        return l15s_latch(o, 0x001029C0u, EM_LEVEL15_PARTICLES_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001CCF70(L15S *o, uint32_t a0, int32_t *result)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001CCF70) return l15s_latch(o, 0x001CCF70u, EM_LEVEL15_PARTICLES_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001CCF70(o->h->ctx, a0, &out) < 0)
        return l15s_latch(o, 0x001CCF70u, EM_LEVEL15_PARTICLES_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15s_c_001CFAE0(L15S *o, uint32_t a0, int32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001CFAE0) return l15s_latch(o, 0x001CFAE0u, EM_LEVEL15_PARTICLES_FAULT_NULL_WORKER);
    if (o->h->w_001CFAE0(o->h->ctx, a0, a1, a2, l15s_float(a3), l15s_float(a4), l15s_float(a5), l15s_float(a6)) < 0)
        return l15s_latch(o, 0x001CFAE0u, EM_LEVEL15_PARTICLES_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15s_c_001CFBE0(L15S *o, int32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, int32_t a4)
{
    if (l15s_failed(o)) return -1;
    if (!o->h->w_001CFBE0) return l15s_latch(o, 0x001CFBE0u, EM_LEVEL15_PARTICLES_FAULT_NULL_WORKER);
    if (o->h->w_001CFBE0(o->h->ctx, a0, a1, a2, a3, a4) < 0)
        return l15s_latch(o, 0x001CFBE0u, EM_LEVEL15_PARTICLES_FAULT_WORKER_FAILED);
    return 0;
}

/* END GENERATED WRAPPERS */
#endif
