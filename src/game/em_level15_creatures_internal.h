/* Memory and fail-stop plumbing, shared design with em_level14_port_internal.h. */
#ifndef EM_LEVEL15_CREATURES_INTERNAL_H
#define EM_LEVEL15_CREATURES_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level15_creatures.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel15CreaturesHooks *h;
    EmLevel15CreaturesFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L15C;

static inline int l15c_latch(L15C *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL15_CREATURES_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l15c_failed(const L15C *o) { return o->fault->code != EM_LEVEL15_CREATURES_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l15c_at(L15C *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l15c_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l15c_latch(o, address, EM_LEVEL15_CREATURES_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l15c_u8(L15C *o, uint32_t a) { return *l15c_at(o, a, 1); }
static inline uint32_t l15c_u16(L15C *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l15c_at(o, a, 2), 2);
    return v;
}
static inline int32_t l15c_s16(L15C *o, uint32_t a) { return (int16_t)l15c_u16(o, a); }
static inline uint32_t l15c_u32(L15C *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l15c_at(o, a, 4), 4);
    return v;
}
static inline int32_t l15c_s32(L15C *o, uint32_t a) { return (int32_t)l15c_u32(o, a); }
static inline int32_t l15c_s8(L15C *o, uint32_t a) { return (int8_t)l15c_u8(o, a); }
static inline uint64_t l15c_u64(L15C *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, l15c_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void l15c_q(L15C *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, l15c_at(o, a, 8), 8);
    memcpy(out + 8, l15c_at(o, a + 8, 8), 8);
}
static inline void l15c_wq(L15C *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(l15c_at(o, a, 8), in, 8);
    memcpy(l15c_at(o, a + 8, 8), in + 8, 8);
}

static inline void l15c_w8(L15C *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l15c_at(o, a, 1), &b, 1);
}
static inline void l15c_w16(L15C *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l15c_at(o, a, 2), &h, 2);
}
static inline void l15c_w32(L15C *o, uint32_t a, uint32_t v) { memcpy(l15c_at(o, a, 4), &v, 4); }
static inline void l15c_w64(L15C *o, uint32_t a, uint64_t v) { memcpy(l15c_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l15c_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l15c_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l15c_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L15C_ADD(a, b) em_ee_add_bits((a), (b))
#define L15C_SUB(a, b) em_ee_sub_bits((a), (b))
#define L15C_MUL(a, b) em_ee_mul_bits((a), (b))
#define L15C_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L15C_LE(a, b) em_ee_c_le_bits((a), (b))
#define L15C_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L15C_DIV(a, b) em_ee_div_bits((a), (b))
#define L15C_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define L15C_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define L15C_MULA(a, b) em_ee_mula_bits((a), (b))
#define L15C_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))

static inline int l15c_begin(L15C *o, const EmLevel15CreaturesHooks *h, EmLevel15CreaturesFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return l15c_failed(o) ? -1 : 0;
}

static inline int l15c_end(const L15C *o) { return l15c_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int l15c_callback(L15C *o, uint32_t fn, uint32_t a0)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_callback) return l15c_latch(o, fn, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? l15c_latch(o, fn, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int l15c_method(L15C *o, uint32_t self)
{
    uint32_t fn = l15c_u32(o, self + 0x4C);
    return l15c_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l15c_c_001028B8(L15C *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001028B8) return l15c_latch(o, 0x001028B8u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    if (o->h->w_001028B8(o->h->ctx, a0, a1, a2) < 0) return l15c_latch(o, 0x001028B8u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15c_c_0011DE90(L15C *o, uint32_t a0, uint32_t *result)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_0011DE90) return l15c_latch(o, 0x0011DE90u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011DE90(o->h->ctx, l15c_float(a0), &out) < 0) return l15c_latch(o, 0x0011DE90u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    *result = l15c_bits(out);
    return 0;
}

static inline int l15c_c_0011E2A8(L15C *o, uint32_t a0, uint32_t *result)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_0011E2A8) return l15c_latch(o, 0x0011E2A8u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_0011E2A8(o->h->ctx, l15c_float(a0), &out) < 0) return l15c_latch(o, 0x0011E2A8u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    *result = l15c_bits(out);
    return 0;
}

static inline int l15c_c_00122BB8(L15C *o, int32_t *result)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_00122BB8) return l15c_latch(o, 0x00122BB8u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_00122BB8(o->h->ctx, &out) < 0) return l15c_latch(o, 0x00122BB8u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15c_c_00131ED0(L15C *o, uint32_t a0)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_00131ED0) return l15c_latch(o, 0x00131ED0u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    if (o->h->w_00131ED0(o->h->ctx, a0) < 0) return l15c_latch(o, 0x00131ED0u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15c_c_00131F20(L15C *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_00131F20) return l15c_latch(o, 0x00131F20u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    if (o->h->w_00131F20(o->h->ctx, a0, a1, a2) < 0) return l15c_latch(o, 0x00131F20u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15c_c_00132490(L15C *o, uint32_t a0, uint32_t a1)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_00132490) return l15c_latch(o, 0x00132490u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    if (o->h->w_00132490(o->h->ctx, a0, a1) < 0) return l15c_latch(o, 0x00132490u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15c_c_0019AB20(L15C *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, int32_t *result)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_0019AB20) return l15c_latch(o, 0x0019AB20u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_0019AB20(o->h->ctx, a0, a1, a2, a3, &out) < 0) return l15c_latch(o, 0x0019AB20u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15c_c_001B10B0(L15C *o, uint32_t a0, int32_t a1, int32_t a2, int32_t *result)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001B10B0) return l15c_latch(o, 0x001B10B0u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B10B0(o->h->ctx, a0, a1, a2, &out) < 0) return l15c_latch(o, 0x001B10B0u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15c_c_001B13F0(L15C *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001B13F0) return l15c_latch(o, 0x001B13F0u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B13F0(o->h->ctx, l15c_float(a0), a1, a2, &out) < 0) return l15c_latch(o, 0x001B13F0u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15c_c_001B1560(L15C *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001B1560) return l15c_latch(o, 0x001B1560u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B1560(o->h->ctx, a0, a1, l15c_float(a2), &out) < 0) return l15c_latch(o, 0x001B1560u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15c_c_001B17A0(L15C *o, uint32_t a0, int32_t *result)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001B17A0) return l15c_latch(o, 0x001B17A0u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B17A0(o->h->ctx, a0, &out) < 0) return l15c_latch(o, 0x001B17A0u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15c_c_001B3250(L15C *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001B3250) return l15c_latch(o, 0x001B3250u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B3250(o->h->ctx, a0, a1, l15c_float(a2), &out) < 0) return l15c_latch(o, 0x001B3250u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15c_c_001B4CF0(L15C *o, uint32_t a0)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001B4CF0) return l15c_latch(o, 0x001B4CF0u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    if (o->h->w_001B4CF0(o->h->ctx, a0) < 0) return l15c_latch(o, 0x001B4CF0u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15c_c_001C63E0(L15C *o, uint32_t a0, int32_t a1)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001C63E0) return l15c_latch(o, 0x001C63E0u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    if (o->h->w_001C63E0(o->h->ctx, a0, a1) < 0) return l15c_latch(o, 0x001C63E0u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15c_c_001C64F0(L15C *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001C64F0) return l15c_latch(o, 0x001C64F0u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001C64F0(o->h->ctx, a0, l15c_float(a1), &out) < 0) return l15c_latch(o, 0x001C64F0u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15c_c_001C67E0(L15C *o, uint32_t a0, int32_t a1, uint32_t a2, uint32_t a3)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001C67E0) return l15c_latch(o, 0x001C67E0u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    if (o->h->w_001C67E0(o->h->ctx, a0, a1, l15c_float(a2), l15c_float(a3)) < 0) return l15c_latch(o, 0x001C67E0u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15c_c_001C68C0(L15C *o, uint32_t a0)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001C68C0) return l15c_latch(o, 0x001C68C0u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    if (o->h->w_001C68C0(o->h->ctx, a0) < 0) return l15c_latch(o, 0x001C68C0u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15c_c_001EFD90(L15C *o, int32_t a0, uint32_t a1, uint32_t a2)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001EFD90) return l15c_latch(o, 0x001EFD90u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    if (o->h->w_001EFD90(o->h->ctx, a0, a1, a2) < 0) return l15c_latch(o, 0x001EFD90u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15c_c_001EFE00(L15C *o, int32_t a0, uint32_t a1, int32_t *result)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001EFE00) return l15c_latch(o, 0x001EFE00u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001EFE00(o->h->ctx, a0, a1, &out) < 0) return l15c_latch(o, 0x001EFE00u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15c_c_001FBD50(L15C *o, uint32_t a0, int32_t a1, int32_t a2, uint32_t a3)
{
    if (l15c_failed(o)) return -1;
    if (!o->h->w_001FBD50) return l15c_latch(o, 0x001FBD50u, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER);
    if (o->h->w_001FBD50(o->h->ctx, a0, a1, a2, l15c_float(a3)) < 0) return l15c_latch(o, 0x001FBD50u, EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED);
    return 0;
}

/* END GENERATED WRAPPERS */
#endif
