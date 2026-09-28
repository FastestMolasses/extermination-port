/* em_vu_host_lanes.h — the VU lane rules of em_ee_float.h for the finite
 * (exponent below 255) operands a VU1 kernel translation feeds its
 * multiplies, adds and reciprocals, computed on the host FPU.
 *
 * Why this exists: the VU1 kernels run tens of thousands of lane operations
 * a frame (the static world's level kernel alone about a million on the
 * heaviest captured route frame). em_ee_float.h's integer arithmetic is the
 * model; this header computes the SAME results with one host instruction
 * per lane operation, which is what keeps the live frame inside the
 * 59.94 Hz tick (docs/STATIC_WORLD.md section 6).
 *
 * The rules (em_ee_float.h, VU0 lane arithmetic, assumed for VU1 by the
 * kernel translations): DAZ operands; the exact result truncated toward
 * zero; a truncated magnitude below the smallest normal flushes to a signed
 * zero; a finite overflow gives +-MAX; x + (-x) is +0 and only
 * (-0) + (-0) is -0. For finite operands that is exactly IEEE 754 binary32
 * arithmetic with rounding toward zero, input denormals read as zero and
 * tiny results flushed to zero:
 *   - IEEE computes the exact result and rounds it once: toward zero, that
 *     is the truncation. Overflow toward zero gives the largest finite
 *     value, never an infinity.
 *   - a result is flushed when its unrounded magnitude is below 2**-126
 *     (arm64 FPCR.FZ) or its rounded one is (x86 MXCSR.FTZ); rounding
 *     toward zero never raises a magnitude to 2**-126, so both equal the
 *     model's test on the truncated value. The zero keeps the sign.
 *   - the sign of an exact zero sum is +0 except (-0) + (-0), in every
 *     rounding mode but toward minus infinity.
 *   - a zero divisor is the one case IEEE answers differently (an
 *     infinity); emvuh_rcp returns the model's +-MAX itself.
 * So each function below is equal to its em_ee_float.h counterpart for
 * every pair of finite operands, provided the thread's FP environment is
 * the one em_vu_host_enter sets (round toward zero, flush-to-zero,
 * denormals-are-zero). tools/test_vu_host_lanes.py proves the equality
 * against em_ee_float.h (targeted boundary cases plus random operands)
 * and the kernel reference tests prove whole batches.
 *
 * Operands with exponent 255 are NOT handled: the kernels fault (fail-stop,
 * *bad) on them and drop the result.
 *
 * Using it: the host lanes run only inside an EM_VU_HOST_NOINLINE function
 * that the caller brackets with em_vu_host_enter / em_vu_host_leave (the
 * call is the barrier that keeps every host float operation inside the
 * environment, and nothing else runs in it). Users: the level kernel
 * (em_vu1_level_kernel.h, one batch text instantiated with `host` 0 or 1;
 * em_vu1_level_kernel_batch_host) and the object and face kernels in
 * em_object_unit.c (EMVUO_HOST_LANES, em_vu1_object_kernel.h). Where the
 * platform has no such environment (neither arm64 nor x86-64, or a
 * compiler without GNU inline asm), EM_VU_HOST_LANES is 0 and the functions
 * below are em_ee_float.h's integer arithmetic. */
#ifndef EM_VU_HOST_LANES_H
#define EM_VU_HOST_LANES_H

#include <stdint.h>
#include <string.h>

#include "game/em_ee_float.h"

#if (defined(__GNUC__) || defined(__clang__)) && (defined(__aarch64__) || defined(__x86_64__))
#define EM_VU_HOST_LANES 1
#else
#define EM_VU_HOST_LANES 0
#endif

/* The function that holds the host-lane work (never inlined into its
 * caller, so no float operation crosses the environment switch). */
#if defined(__GNUC__) || defined(__clang__)
#define EM_VU_HOST_NOINLINE __attribute__((noinline))
#else
#define EM_VU_HOST_NOINLINE
#endif

typedef struct {
    uint64_t saved;
} EmVuHostEnv;

#if EM_VU_HOST_LANES

#if defined(__aarch64__)
/* FPCR: RMode (bits 22..23) = 3, round toward zero; FZ (bit 24) = 1. */
#define EMVUH_HIDE(x) __asm__("" : "+w"(x))
static inline void em_vu_host_enter(EmVuHostEnv *e)
{
    uint64_t v;
    __asm__ volatile("mrs %0, fpcr" : "=r"(v) : : "memory");
    e->saved = v;
    v |= (UINT64_C(3) << 22) | (UINT64_C(1) << 24);
    __asm__ volatile("msr fpcr, %0" : : "r"(v) : "memory");
}
static inline void em_vu_host_leave(const EmVuHostEnv *e)
{
    __asm__ volatile("msr fpcr, %0" : : "r"(e->saved) : "memory");
}
#else
/* MXCSR: RC (bits 13..14) = 3, round toward zero; FTZ (bit 15); DAZ (bit 6). */
#define EMVUH_HIDE(x) __asm__("" : "+x"(x))
static inline void em_vu_host_enter(EmVuHostEnv *e)
{
    uint32_t v;
    __asm__ volatile("stmxcsr %0" : "=m"(v) : : "memory");
    e->saved = v;
    v |= 0x6000u | 0x8000u | 0x0040u;
    __asm__ volatile("ldmxcsr %0" : : "m"(v) : "memory");
}
static inline void em_vu_host_leave(const EmVuHostEnv *e)
{
    const uint32_t v = (uint32_t)e->saved;
    __asm__ volatile("ldmxcsr %0" : : "m"(v) : "memory");
}
#endif

static inline float emvuh_f(uint32_t b)
{
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

static inline uint32_t emvuh_b(float f)
{
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
}

/* EMVUH_HIDE keeps the compiler from folding an operand it can see (1.0 *
 * x is not x when x is a denormal) and from fusing a product into the next
 * add (a VU multiply-add truncates the product first). */
static inline uint32_t emvuh_mul(uint32_t a, uint32_t b)
{
    float x = emvuh_f(a), y = emvuh_f(b);
    EMVUH_HIDE(x);
    EMVUH_HIDE(y);
    float r = x * y;
    EMVUH_HIDE(r);
    return emvuh_b(r);
}

static inline uint32_t emvuh_add(uint32_t a, uint32_t b)
{
    float x = emvuh_f(a), y = emvuh_f(b);
    EMVUH_HIDE(x);
    EMVUH_HIDE(y);
    float r = x + y;
    EMVUH_HIDE(r);
    return emvuh_b(r);
}

static inline uint32_t emvuh_sub(uint32_t a, uint32_t b)
{
    float x = emvuh_f(a), y = emvuh_f(b);
    EMVUH_HIDE(x);
    EMVUH_HIDE(y);
    float r = x - y;
    EMVUH_HIDE(r);
    return emvuh_b(r);
}

/* VDIV (3, 3) Q = 1 / b: a zero (or denormal) divisor gives +-MAX by b's
 * sign, as em_vu_div_bits does. */
static inline uint32_t emvuh_rcp(uint32_t b)
{
    if (!(b & 0x7F800000u)) return (b & EM_EE_SIGN) | EM_EE_MAX;
    float x = 1.0f, y = emvuh_f(b);
    EMVUH_HIDE(x);
    EMVUH_HIDE(y);
    float r = x / y;
    EMVUH_HIDE(r);
    return emvuh_b(r);
}

#else /* no host environment: the model's integer arithmetic */

static inline void em_vu_host_enter(EmVuHostEnv *e) { e->saved = 0; }
static inline void em_vu_host_leave(const EmVuHostEnv *e) { (void)e; }
static inline uint32_t emvuh_mul(uint32_t a, uint32_t b) { return em_eei_vu_mul_raw(a, b); }
static inline uint32_t emvuh_add(uint32_t a, uint32_t b) { return em_eei_vu_add_raw(a, b); }
static inline uint32_t emvuh_sub(uint32_t a, uint32_t b) { return em_eei_vu_sub_raw(a, b); }
static inline uint32_t emvuh_rcp(uint32_t b)
{
    uint32_t q = 0;
    (void)em_vu_div_bits(EM_EE_ONE, b, 3, 3, &q);
    return q;
}

#endif

#endif /* EM_VU_HOST_LANES_H */
