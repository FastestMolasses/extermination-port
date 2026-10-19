/* Memory and fail-stop plumbing, shared design with em_level14_port_internal.h. */
#ifndef EM_AREA00_RETURN_INTERNAL_H
#define EM_AREA00_RETURN_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include "em_area00_return.h"
#include "em_ee_float.h"

typedef struct {
    const EmArea00ReturnHooks *h;
    EmArea00ReturnFault *fault;
    uint8_t sink[16]; /* reads/writes after a fault land here */
} A00R;

static inline int a00r_latch(A00R *o, uint32_t address, int32_t code)
{
    if (o->fault->code == EM_AREA00_RETURN_FAULT_NONE) {
        o->fault->address = address;
        o->fault->code = code;
    }
    return -1;
}

static inline int a00r_failed(const A00R *o) { return o->fault->code != EM_AREA00_RETURN_FAULT_NONE; }

/* The native bytes of [address, address + size). Each pointer is used
 * before the next request or hook call (the test relies on that to see
 * each access in order). After a fault: a zeroed sink, `bytes` not called. */
static inline uint8_t *a00r_at(A00R *o, uint32_t address, uint32_t size)
{
    uint8_t *p = NULL;
    if (!a00r_failed(o)) {
        p = o->h->bytes ? o->h->bytes(o->h->ctx, address, size) : NULL;
        if (!p) a00r_latch(o, address, EM_AREA00_RETURN_FAULT_BAD_ADDRESS);
    }
    if (!p) {
        memset(o->sink, 0, sizeof o->sink);
        p = o->sink;
    }
    return p;
}

static inline uint32_t a00r_u8(A00R *o, uint32_t a) { return *a00r_at(o, a, 1); }
static inline uint32_t a00r_u16(A00R *o, uint32_t a)
{
    uint16_t v;
    memcpy(&v, a00r_at(o, a, 2), 2);
    return v;
}
static inline int32_t a00r_s16(A00R *o, uint32_t a) { return (int16_t)a00r_u16(o, a); }
static inline uint32_t a00r_u32(A00R *o, uint32_t a)
{
    uint32_t v;
    memcpy(&v, a00r_at(o, a, 4), 4);
    return v;
}
static inline int32_t a00r_s32(A00R *o, uint32_t a) { return (int32_t)a00r_u32(o, a); }
static inline int32_t a00r_s8(A00R *o, uint32_t a) { return (int8_t)a00r_u8(o, a); }
static inline uint64_t a00r_u64(A00R *o, uint32_t a)
{
    uint64_t v;
    memcpy(&v, a00r_at(o, a, 8), 8);
    return v;
}

/* A quadword load / store (lq / sq) of the 16 bytes at a & ~15 (the EE
 * ignores the low four address bits), as its two doublewords, low then
 * high (the access granularity the test's EE core models and compares). */
static inline void a00r_q(A00R *o, uint32_t a, uint8_t out[16])
{
    a &= ~15u;
    memcpy(out, a00r_at(o, a, 8), 8);
    memcpy(out + 8, a00r_at(o, a + 8, 8), 8);
}
static inline void a00r_wq(A00R *o, uint32_t a, const uint8_t in[16])
{
    a &= ~15u;
    memcpy(a00r_at(o, a, 8), in, 8);
    memcpy(a00r_at(o, a + 8, 8), in + 8, 8);
}

static inline void a00r_w8(A00R *o, uint32_t a, uint32_t v)
{
    uint8_t b = (uint8_t)v;
    memcpy(a00r_at(o, a, 1), &b, 1);
}
static inline void a00r_w16(A00R *o, uint32_t a, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(a00r_at(o, a, 2), &h, 2);
}
static inline void a00r_w32(A00R *o, uint32_t a, uint32_t v) { memcpy(a00r_at(o, a, 4), &v, 4); }
static inline void a00r_w64(A00R *o, uint32_t a, uint64_t v) { memcpy(a00r_at(o, a, 8), &v, 8); }

/* A float as its bits, and back (no conversion). */
static inline uint32_t a00r_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
static inline float a00r_float(uint32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

/* The EE's arithmetic right shift of a 32-bit register. */
static inline int32_t a00r_sra(uint32_t v, unsigned n)
{
    return (int32_t)((v >> n) | ((v & 0x80000000u) && n ? ~(0xFFFFFFFFu >> n) : 0u));
}

/* EE arithmetic on bit patterns (em_ee_float.h). */
#define A00R_ADD(a, b) em_ee_add_bits((a), (b))
#define A00R_SUB(a, b) em_ee_sub_bits((a), (b))
#define A00R_MUL(a, b) em_ee_mul_bits((a), (b))
#define A00R_LT(a, b) em_ee_c_lt_bits((a), (b))
#define A00R_LE(a, b) em_ee_c_le_bits((a), (b))
#define A00R_EQ(a, b) em_ee_c_eq_bits((a), (b))
#define A00R_DIV(a, b) em_ee_div_bits((a), (b))
#define A00R_CVT_S_W(w) em_ee_cvt_s_w_bits((uint32_t)(w))
#define A00R_NEG(a) em_ee_neg_bits(a)
/* The multiply-add pair: ACC = a * b (MULA), then ACC + c * d (MADD). */
#define A00R_MULA(a, b) em_ee_mula_bits((a), (b))
#define A00R_MADD(acc, c, d) em_ee_madd_bits((acc), (c), (d))

static inline int a00r_begin(A00R *o, const EmArea00ReturnHooks *h, EmArea00ReturnFault *fault)
{
    if (!h || !fault) return -1;
    o->h = h;
    o->fault = fault;
    memset(o->sink, 0, sizeof o->sink);
    return a00r_failed(o) ? -1 : 0;
}

static inline int a00r_end(const A00R *o) { return a00r_failed(o) ? -1 : 0; }

/* An indirect call through the function word `fn` (already read from
 * memory): w_callback(ctx, fn, a0). */
static inline int a00r_callback(A00R *o, uint32_t fn, uint32_t a0)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_callback) return a00r_latch(o, fn, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    return o->h->w_callback(o->h->ctx, fn, a0) < 0 ? a00r_latch(o, fn, EM_AREA00_RETURN_FAULT_WORKER_FAILED) : 0;
}

/* The actor's +0x4C method: its word read here, then
 * w_callback(ctx, function, self). */
static inline int a00r_method(A00R *o, uint32_t self)
{
    uint32_t fn = a00r_u32(o, self + 0x4C);
    return a00r_callback(o, fn, self);
}

/* One wrapper per hook. Arguments are evaluated by the caller before the
 * wrapper runs, so a fault latched while reading an argument stops the
 * call. Float arguments and results are bit patterns. Each returns 0 on
 * success, -1 when skipped or failed. */
/* BEGIN GENERATED WRAPPERS */
static inline int a00r_c_001026A0(A00R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001026A0) return a00r_latch(o, 0x001026A0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001026A0(o->h->ctx, a0, a1, a2) < 0) return a00r_latch(o, 0x001026A0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_00102718(A00R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_00102718) return a00r_latch(o, 0x00102718u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_00102718(o->h->ctx, a0, a1, a2) < 0) return a00r_latch(o, 0x00102718u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001027E0(A00R *o, uint32_t a0, uint32_t a1)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001027E0) return a00r_latch(o, 0x001027E0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001027E0(o->h->ctx, a0, a1) < 0) return a00r_latch(o, 0x001027E0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001028D0(A00R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001028D0) return a00r_latch(o, 0x001028D0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001028D0(o->h->ctx, a0, a1, a2) < 0) return a00r_latch(o, 0x001028D0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_00102958(A00R *o, uint32_t a0, uint32_t a1)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_00102958) return a00r_latch(o, 0x00102958u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_00102958(o->h->ctx, a0, a1) < 0) return a00r_latch(o, 0x00102958u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001029C0(A00R *o, uint32_t a0)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001029C0) return a00r_latch(o, 0x001029C0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001029C0(o->h->ctx, a0) < 0) return a00r_latch(o, 0x001029C0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001031E0(A00R *o, uint32_t a0, uint32_t a1)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001031E0) return a00r_latch(o, 0x001031E0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001031E0(o->h->ctx, a0, a1) < 0) return a00r_latch(o, 0x001031E0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_00103230(A00R *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_00103230) return a00r_latch(o, 0x00103230u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_00103230(o->h->ctx, a0, a1, a00r_float(a2)) < 0) return a00r_latch(o, 0x00103230u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_00122BB8(A00R *o, int32_t *result)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_00122BB8) return a00r_latch(o, 0x00122BB8u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_00122BB8(o->h->ctx, &out) < 0) return a00r_latch(o, 0x00122BB8u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a00r_c_001287F0(A00R *o, uint32_t a0, uint32_t a1, int32_t a2, uint32_t a3)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001287F0) return a00r_latch(o, 0x001287F0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001287F0(o->h->ctx, a0, a1, a2, a00r_float(a3)) < 0) return a00r_latch(o, 0x001287F0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001288D0(A00R *o, uint32_t a0, uint32_t a1)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001288D0) return a00r_latch(o, 0x001288D0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001288D0(o->h->ctx, a0, a1) < 0) return a00r_latch(o, 0x001288D0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_00128AB0(A00R *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_00128AB0) return a00r_latch(o, 0x00128AB0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_00128AB0(o->h->ctx, a0, a1, &out) < 0) return a00r_latch(o, 0x00128AB0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a00r_c_00129780(A00R *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_00129780) return a00r_latch(o, 0x00129780u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_00129780(o->h->ctx, a0, a1, a2, &out) < 0) return a00r_latch(o, 0x00129780u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a00r_c_0019A570(A00R *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t *result)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_0019A570) return a00r_latch(o, 0x0019A570u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_0019A570(o->h->ctx, a0, a1, a2, a3, &out) < 0) return a00r_latch(o, 0x0019A570u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a00r_c_001AFC10(A00R *o, uint32_t a0)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001AFC10) return a00r_latch(o, 0x001AFC10u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001AFC10(o->h->ctx, a0) < 0) return a00r_latch(o, 0x001AFC10u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001B0CD0(A00R *o, uint32_t a0, int32_t a1)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001B0CD0) return a00r_latch(o, 0x001B0CD0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001B0CD0(o->h->ctx, a0, a1) < 0) return a00r_latch(o, 0x001B0CD0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001B1240(A00R *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t *result)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001B1240) return a00r_latch(o, 0x001B1240u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B1240(o->h->ctx, a0, a00r_float(a1), a00r_float(a2), &out) < 0) return a00r_latch(o, 0x001B1240u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    *result = a00r_bits(out);
    return 0;
}

static inline int a00r_c_001B12B0(A00R *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t *result)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001B12B0) return a00r_latch(o, 0x001B12B0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B12B0(o->h->ctx, a00r_float(a0), a00r_float(a1), a00r_float(a2), &out) < 0) return a00r_latch(o, 0x001B12B0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    *result = a00r_bits(out);
    return 0;
}

static inline int a00r_c_001B1470(A00R *o, uint32_t a0, uint32_t *result)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001B1470) return a00r_latch(o, 0x001B1470u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    float out = 0;
    if (o->h->w_001B1470(o->h->ctx, a00r_float(a0), &out) < 0) return a00r_latch(o, 0x001B1470u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    *result = a00r_bits(out);
    return 0;
}

static inline int a00r_c_001B17A0(A00R *o, uint32_t a0)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001B17A0) return a00r_latch(o, 0x001B17A0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001B17A0(o->h->ctx, a0) < 0) return a00r_latch(o, 0x001B17A0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001BA1C0(A00R *o, uint32_t a0, int32_t a1, int32_t *result)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001BA1C0) return a00r_latch(o, 0x001BA1C0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001BA1C0(o->h->ctx, a0, a1, &out) < 0) return a00r_latch(o, 0x001BA1C0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a00r_c_001C2770(A00R *o, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001C2770) return a00r_latch(o, 0x001C2770u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001C2770(o->h->ctx, a0, a1, a2, &out) < 0) return a00r_latch(o, 0x001C2770u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a00r_c_001C3D60(A00R *o, uint32_t a0, uint32_t a1)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001C3D60) return a00r_latch(o, 0x001C3D60u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001C3D60(o->h->ctx, a0, a1) < 0) return a00r_latch(o, 0x001C3D60u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001C63E0(A00R *o, uint32_t a0, int32_t a1)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001C63E0) return a00r_latch(o, 0x001C63E0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001C63E0(o->h->ctx, a0, a1) < 0) return a00r_latch(o, 0x001C63E0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001C64F0(A00R *o, uint32_t a0, uint32_t a1, int32_t *result)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001C64F0) return a00r_latch(o, 0x001C64F0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    int32_t out = 0;
    if (o->h->w_001C64F0(o->h->ctx, a0, a00r_float(a1), &out) < 0) return a00r_latch(o, 0x001C64F0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    *result = out;
    return 0;
}

static inline int a00r_c_001C6960(A00R *o, uint32_t a0)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001C6960) return a00r_latch(o, 0x001C6960u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001C6960(o->h->ctx, a0) < 0) return a00r_latch(o, 0x001C6960u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001C69A0(A00R *o, uint32_t a0)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001C69A0) return a00r_latch(o, 0x001C69A0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001C69A0(o->h->ctx, a0) < 0) return a00r_latch(o, 0x001C69A0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001CA6F0(A00R *o, uint32_t a0, int32_t a1)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001CA6F0) return a00r_latch(o, 0x001CA6F0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001CA6F0(o->h->ctx, a0, a1) < 0) return a00r_latch(o, 0x001CA6F0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001EFD90(A00R *o, int32_t a0, uint32_t a1, uint32_t a2)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001EFD90) return a00r_latch(o, 0x001EFD90u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001EFD90(o->h->ctx, a0, a1, a2) < 0) return a00r_latch(o, 0x001EFD90u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001FB9F0(A00R *o, int32_t a0, int32_t a1, int32_t a2, int32_t a3)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001FB9F0) return a00r_latch(o, 0x001FB9F0u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001FB9F0(o->h->ctx, a0, a1, a2, a3) < 0) return a00r_latch(o, 0x001FB9F0u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_001FBD50(A00R *o, uint32_t a0, int32_t a1, int32_t a2, uint32_t a3)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_001FBD50) return a00r_latch(o, 0x001FBD50u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_001FBD50(o->h->ctx, a0, a1, a2, a00r_float(a3)) < 0) return a00r_latch(o, 0x001FBD50u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_00824340(A00R *o, uint32_t a0, uint32_t a1)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_00824340) return a00r_latch(o, 0x00824340u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_00824340(o->h->ctx, a0, a1) < 0) return a00r_latch(o, 0x00824340u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_00824770(A00R *o, uint32_t a0, uint32_t a1)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_00824770) return a00r_latch(o, 0x00824770u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_00824770(o->h->ctx, a0, a1) < 0) return a00r_latch(o, 0x00824770u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

static inline int a00r_c_00824D50(A00R *o, uint32_t a0, uint32_t a1)
{
    if (a00r_failed(o)) return -1;
    if (!o->h->w_00824D50) return a00r_latch(o, 0x00824D50u, EM_AREA00_RETURN_FAULT_NULL_WORKER);
    if (o->h->w_00824D50(o->h->ctx, a0, a1) < 0) return a00r_latch(o, 0x00824D50u, EM_AREA00_RETURN_FAULT_WORKER_FAILED);
    return 0;
}

/* END GENERATED WRAPPERS */
#endif
