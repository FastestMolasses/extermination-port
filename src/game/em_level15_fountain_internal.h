/* Memory and fail-stop plumbing, shared design with em_level14_port_internal.h. */
#ifndef EM_LEVEL15_FOUNTAIN_INTERNAL_H
#define EM_LEVEL15_FOUNTAIN_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_level15_fountain.h"
#include "em_ee_float.h"

typedef struct {
    const EmLevel15FountainHooks *h;
    EmLevel15FountainFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} L15F;

static inline int l15f_latch(L15F *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_LEVEL15_FOUNTAIN_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int l15f_failed(const L15F *o) { return o->fault->code != EM_LEVEL15_FOUNTAIN_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *l15f_at(L15F *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!l15f_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) l15f_latch(o, address, EM_LEVEL15_FOUNTAIN_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t l15f_u8(L15F *o, uint32_t a) { return *l15f_at(o, a, 1); }
static inline uint32_t l15f_u16(L15F *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, l15f_at(o, a, 2), 2);
    return v;
}
static inline int32_t l15f_s16(L15F *o, uint32_t a) { return (int16_t)l15f_u16(o, a); }
static inline uint32_t l15f_u32(L15F *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, l15f_at(o, a, 4), 4);
    return v;
}
static inline int32_t l15f_s32(L15F *o, uint32_t a) { return (int32_t)l15f_u32(o, a); }
static inline int32_t l15f_s8(L15F *o, uint32_t a) { return (int8_t)l15f_u8(o, a); }
static inline uint64_t l15f_u64(L15F *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, l15f_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void l15f_q(L15F *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, l15f_at(o, a, 8), 8);
    memcpy(out + 8, l15f_at(o, a + 8, 8), 8);
}
static inline void l15f_wq(L15F *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(l15f_at(o, a, 8), in, 8);
    memcpy(l15f_at(o, a + 8, 8), in + 8, 8);
}

static inline void l15f_w8(L15F *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(l15f_at(o, a, 1), &b, 1);
}
static inline void l15f_w16(L15F *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(l15f_at(o, a, 2), &h, 2);
}
static inline void l15f_w32(L15F *o, uint32_t a, uint32_t v) { memcpy(l15f_at(o, a, 4), &v, 4); }
static inline void l15f_w64(L15F *o, uint32_t a, uint64_t v) { memcpy(l15f_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t l15f_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float l15f_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t l15f_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define L15F_ADD(a, b) em_ee_add_bits((a), (b))
#define L15F_SUB(a, b) em_ee_sub_bits((a), (b))
#define L15F_MUL(a, b) em_ee_mul_bits((a), (b))
#define L15F_LT(a, b) em_ee_c_lt_bits((a), (b))
#define L15F_LE(a, b) em_ee_c_le_bits((a), (b))
#define L15F_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define L15F_DIV(a, b) em_ee_div_bits((a), (b))
#define L15F_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define L15F_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define L15F_MULA(a, b) em_ee_mula_bits((a), (b))
#define L15F_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))

static inline int l15f_begin(L15F *o, const EmLevel15FountainHooks *h, EmLevel15FountainFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return l15f_failed(o) ? -1 : 0;
}

static inline int l15f_end(const L15F *o) { return l15f_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int l15f_callback(L15F *o, uint32_t fn, uint32_t a0)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_callback) return l15f_latch(o, fn, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? l15f_latch(o, fn, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int l15f_method(L15F *o, uint32_t self)
{
    uint32_t fn = l15f_u32(o, self + 0x4C);
    return l15f_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int l15f_c_00187EC0(L15F *o, int32_t a0, int32_t a1)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_00187EC0) return l15f_latch(o, 0x00187EC0u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_00187EC0(o->h->ctx, a0, a1) < 0) return l15f_latch(o, 0x00187EC0u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001AF890(L15F *o, uint32_t a0)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001AF890) return l15f_latch(o, 0x001AF890u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001AF890(o->h->ctx, a0) < 0) return l15f_latch(o, 0x001AF890u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001AFC10(L15F *o, uint32_t a0)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001AFC10) return l15f_latch(o, 0x001AFC10u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001AFC10(o->h->ctx, a0) < 0) return l15f_latch(o, 0x001AFC10u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001B1020(L15F *o, uint32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t *result)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001B1020) return l15f_latch(o, 0x001B1020u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001B1020(o->h->ctx, a0, a1, a2, a3, &out) < 0) return l15f_latch(o, 0x001B1020u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15f_c_001B1190(L15F *o, int32_t a0)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001B1190) return l15f_latch(o, 0x001B1190u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001B1190(o->h->ctx, a0) < 0) return l15f_latch(o, 0x001B1190u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001B17A0(L15F *o, uint32_t a0)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001B17A0) return l15f_latch(o, 0x001B17A0u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001B17A0(o->h->ctx, a0) < 0) return l15f_latch(o, 0x001B17A0u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001BE5F0(L15F *o, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001BE5F0) return l15f_latch(o, 0x001BE5F0u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BE5F0(o->h->ctx, a0, a1, a2, &out) < 0) return l15f_latch(o, 0x001BE5F0u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15f_c_001BEAC0(L15F *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, uint32_t *result)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001BEAC0) return l15f_latch(o, 0x001BEAC0u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    uint32_t out = 0;
    if (o->h->w_001BEAC0(o->h->ctx, a0, a1, a2, a3, &out) < 0) return l15f_latch(o, 0x001BEAC0u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15f_c_001BEB70(L15F *o, uint32_t a0)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001BEB70) return l15f_latch(o, 0x001BEB70u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001BEB70(o->h->ctx, a0) < 0) return l15f_latch(o, 0x001BEB70u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001BEC40(L15F *o, uint32_t a0, uint32_t a1, int32_t a2)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001BEC40) return l15f_latch(o, 0x001BEC40u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001BEC40(o->h->ctx, a0, a1, a2) < 0) return l15f_latch(o, 0x001BEC40u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001C1500(L15F *o, uint32_t a0, int32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001C1500) return l15f_latch(o, 0x001C1500u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001C1500(o->h->ctx, a0, a1, l15f_float(a2), l15f_float(a3), l15f_float(a4)) < 0) return l15f_latch(o, 0x001C1500u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001C1570(L15F *o, uint32_t a0, int32_t *result)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001C1570) return l15f_latch(o, 0x001C1570u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001C1570(o->h->ctx, a0, &out) < 0) return l15f_latch(o, 0x001C1570u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15f_c_001C62C0(L15F *o, uint32_t a0)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001C62C0) return l15f_latch(o, 0x001C62C0u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001C62C0(o->h->ctx, a0) < 0) return l15f_latch(o, 0x001C62C0u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001C6380(L15F *o, uint32_t a0)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001C6380) return l15f_latch(o, 0x001C6380u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001C6380(o->h->ctx, a0) < 0) return l15f_latch(o, 0x001C6380u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001D0C80(L15F *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001D0C80) return l15f_latch(o, 0x001D0C80u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001D0C80(o->h->ctx, a0, a1, &out) < 0) return l15f_latch(o, 0x001D0C80u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15f_c_001D0D40(L15F *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001D0D40) return l15f_latch(o, 0x001D0D40u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001D0D40(o->h->ctx, a0, a1, a2, a3) < 0) return l15f_latch(o, 0x001D0D40u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001D0D60(L15F *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001D0D60) return l15f_latch(o, 0x001D0D60u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001D0D60(o->h->ctx, a0, l15f_float(a1), &out) < 0) return l15f_latch(o, 0x001D0D60u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int l15f_c_001EFD90(L15F *o, int32_t a0, uint32_t a1, uint32_t a2)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001EFD90) return l15f_latch(o, 0x001EFD90u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001EFD90(o->h->ctx, a0, a1, a2) < 0) return l15f_latch(o, 0x001EFD90u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001EFE00(L15F *o, int32_t a0, uint32_t a1)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001EFE00) return l15f_latch(o, 0x001EFE00u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001EFE00(o->h->ctx, a0, a1) < 0) return l15f_latch(o, 0x001EFE00u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int l15f_c_001FBD50(L15F *o, uint32_t a0, int32_t a1, int32_t a2, uint32_t a3)
{
    if (l15f_failed(o)) return -1;
    if (!o->h->w_001FBD50) return l15f_latch(o, 0x001FBD50u, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER);
    if (o->h->w_001FBD50(o->h->ctx, a0, a1, a2, l15f_float(a3)) < 0) return l15f_latch(o, 0x001FBD50u, EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED);
    return 0;
}

/* END GENERATED WRAPPERS */
#endif
