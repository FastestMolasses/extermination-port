/* em_camera_leftovers_internal.h - shared arithmetic and access helpers of
 * em_camera_leftovers*.c (not a public interface).
 *
 * The SDK vector leaves are translated here as their VU0 macro
 * instructions (the same forms em_camera_follow_original.c translates; each
 * is an asm-word file in the decomp and was read from its instructions):
 *   001028D0(out, a, b)  out = a - b, all four lanes (VSUB.xyzw)
 *   001028B8(out, a, b)  out = a + b, all four lanes (VADD.xyzw)
 *   00102760(out, v)     xyz of v times 1/sqrt(v.v), w = 0
 *   00103230(out, v, s)  out.xyz = v.xyz * s, out.w = v.w (VMULx.xyz)
 *   00102900(out, v, s)  out = v * s, all four lanes (VMULx.xyzw)
 *   00102738(a, b)       a.x*b.x + a.y*b.y + a.z*b.z (VMUL.xyz, VADDy.x, VADDz.x)
 *   00102948(out, v)     a 16-byte copy
 *   001031E0(out, v)     a 12-byte copy
 * A VU form em_ee_float.h has not measured is a fault (-1). */
#ifndef EM_CAMERA_LEFTOVERS_INTERNAL_H
#define EM_CAMERA_LEFTOVERS_INTERNAL_H

#include "game/em_camera_leftovers.h"
#include "game/em_ee_float.h"
#include "game/em_sdk_math_original.h"

#include <string.h>

/* Float constants as the instructions build them (lui / ori). */
#define CL_ZERO     UINT32_C(0x00000000)
#define CL_ONE      UINT32_C(0x3F800000)
#define CL_M1       UINT32_C(0xBF800000)
#define CL_0_1      UINT32_C(0x3DCCCCCD)
#define CL_0_15     UINT32_C(0x3E19999A)
#define CL_0_17     UINT32_C(0x3E2E147B)
#define CL_0_2      UINT32_C(0x3E4CCCCD)
#define CL_0_3      UINT32_C(0x3E99999A)
#define CL_M0_3     UINT32_C(0xBE99999A)
#define CL_0_4      UINT32_C(0x3ECCCCCD)
#define CL_0_5      UINT32_C(0x3F000000)
#define CL_0_707    UINT32_C(0x3F34FDF4)
#define CL_0_8      UINT32_C(0x3F4CCCCD)
#define CL_0_9      UINT32_C(0x3F666666)
#define CL_M0_08    UINT32_C(0xBDA3D70A)
#define CL_M0_998   UINT32_C(0xBF7F7CEE)
#define CL_1_5      UINT32_C(0x3FC00000)
#define CL_M1_5     UINT32_C(0xBFC00000)
#define CL_2        UINT32_C(0x40000000)
#define CL_3        UINT32_C(0x40400000)
#define CL_M3       UINT32_C(0xC0400000)
#define CL_4        UINT32_C(0x40800000)
#define CL_5_5      UINT32_C(0x40B00000)
#define CL_6        UINT32_C(0x40C00000)
#define CL_7        UINT32_C(0x40E00000)
#define CL_M7       UINT32_C(0xC0E00000)
#define CL_8        UINT32_C(0x41000000)
#define CL_8_6      UINT32_C(0x4109999A)
#define CL_M10      UINT32_C(0xC1200000)
#define CL_11       UINT32_C(0x41300000)
#define CL_13       UINT32_C(0x41500000)
#define CL_15       UINT32_C(0x41700000)
#define CL_17       UINT32_C(0x41880000)
#define CL_17_5     UINT32_C(0x418C0000)
#define CL_20       UINT32_C(0x41A00000)
#define CL_M20      UINT32_C(0xC1A00000)
#define CL_M46_8    UINT32_C(0xC23B3333)
#define CL_M67_5    UINT32_C(0xC2870000)
#define CL_70       UINT32_C(0x428C0000)
#define CL_M83      UINT32_C(0xC2A60000)
#define CL_120      UINT32_C(0x42F00000)
#define CL_169_5    UINT32_C(0x43298000)
#define CL_180      UINT32_C(0x43340000)
#define CL_200      UINT32_C(0x43480000)
#define CL_230_6    UINT32_C(0x4366999A)
#define CL_260      UINT32_C(0x43820000)
#define CL_285      UINT32_C(0x438E8000)
#define CL_1000     UINT32_C(0x447A0000)
#define CL_M200     UINT32_C(0xC3480000)
#define CL_M1590    UINT32_C(0xC4C6C000)
#define CL_PI       UINT32_C(0x40490FDB)
#define CL_HALF_PI  UINT32_C(0x3FC90FDB)
#define CL_3DEG     UINT32_C(0x3D567750)   /* 0.05235988 */

/* ---- EE COP1 on raw words -------------------------------------------------- */
static inline uint32_t cl_add(uint32_t a, uint32_t b) { return em_ee_add_bits(a, b); }
static inline uint32_t cl_sub(uint32_t a, uint32_t b) { return em_ee_sub_bits(a, b); }
static inline uint32_t cl_mul(uint32_t a, uint32_t b) { return em_ee_mul_bits(a, b); }
static inline uint32_t cl_div(uint32_t a, uint32_t b) { return em_ee_div_bits(a, b); }
static inline uint32_t cl_neg(uint32_t a) { return em_ee_neg_bits(a); }
/* madd.s fd, fs, ft: ACC + fs * ft. */
static inline uint32_t cl_madd(uint32_t acc, uint32_t fs, uint32_t ft) { return em_ee_madd_bits(acc, fs, ft); }
static inline int cl_eq(uint32_t a, uint32_t b) { return em_ee_c_eq_bits(a, b); }
static inline int cl_lt(uint32_t a, uint32_t b) { return em_ee_c_lt_bits(a, b); }
static inline int cl_le(uint32_t a, uint32_t b) { return em_ee_c_le_bits(a, b); }
/* 0011DF78 (fabsf): the em_sdk_math_original translation. */
static inline uint32_t cl_fabs(uint32_t x)
{
    return em_ee_bits(em_sdk_math_original_0011DF78(em_ee_float(x)));
}

/* ---- Record access by original offset ------------------------------------ */
static inline uint32_t cl_cw(const EmCameraFollowRecord *c, unsigned at) { return em_camera_follow_word(c, at); }
static inline void cl_cset(EmCameraFollowRecord *c, unsigned at, uint32_t v) { em_camera_follow_set_word(c, at, v); }
static inline uint8_t cl_cb(const EmCameraFollowRecord *c, unsigned at) { return c->bytes[at]; }
static inline void cl_cbset(EmCameraFollowRecord *c, unsigned at, unsigned v) { c->bytes[at] = (uint8_t)v; }
static inline uint16_t cl_ch(const EmCameraFollowRecord *c, unsigned at)
{
    uint16_t v; memcpy(&v, c->bytes + at, 2); return v;
}
static inline void cl_chset(EmCameraFollowRecord *c, unsigned at, unsigned v)
{
    uint16_t h = (uint16_t)v; memcpy(c->bytes + at, &h, 2);
}
static inline uint32_t *cl_cvec(EmCameraFollowRecord *c, unsigned at)
{
    return (uint32_t *)(void *)(c->bytes + at);
}
static inline uint32_t cl_pw(const EmPlayerLiveActor *p, unsigned at) { return em_live_u32(p, at); }

/* ---- The SDK vector leaves -------------------------------------------------
 * Vectors are four raw words; every leaf reads its inputs completely before
 * it writes, so out may alias an input (as it does in the original calls). */
#define CL_VU(expr) do { if ((expr) != EM_EE_FLOAT_OK) return -1; } while (0)

static inline void cl_load4(uint32_t out[4], const void *in) { memcpy(out, in, 16); }
static inline void cl_store4(void *out, const uint32_t in[4]) { memcpy(out, in, 16); }

/* 001028D0 */
static inline int cl_v_sub(void *out, const void *a, const void *b)
{
    uint32_t x[4], y[4], r[4] = { 0, 0, 0, 0 };
    cl_load4(x, a); cl_load4(y, b);
    CL_VU(em_vu_vec_bits(EM_VU_SUB, 15, EM_VU_NO_BC, x, y, 0, NULL, r));
    cl_store4(out, r);
    return 0;
}
/* 001028B8 */
static inline int cl_v_add(void *out, const void *a, const void *b)
{
    uint32_t x[4], y[4], r[4] = { 0, 0, 0, 0 };
    cl_load4(x, a); cl_load4(y, b);
    CL_VU(em_vu_vec_bits(EM_VU_ADD, 15, EM_VU_NO_BC, x, y, 0, NULL, r));
    cl_store4(out, r);
    return 0;
}
/* 00102760: t.xyz = v * v; t.x += t.y; t.x += t.z; Q = sqrt(t.x);
 * t.x = 0 + Q; Q = 1.0 / t.x; r = 0; r.xyz = v * Q. */
static inline int cl_v_normalize(void *out, const void *in)
{
    static const uint32_t vf0[4] = { 0, 0, 0, CL_ONE };
    uint32_t v[4], t[4], r[4] = { 0, 0, 0, 0 }, q;
    cl_load4(v, in);
    memcpy(t, v, sizeof t);
    CL_VU(em_vu_vec_bits(EM_VU_MUL, 14, EM_VU_NO_BC, v, v, 0, NULL, t));
    CL_VU(em_vu_vec_bits(EM_VU_ADDBC, 8, 1, t, t, 0, NULL, t));
    CL_VU(em_vu_vec_bits(EM_VU_ADDBC, 8, 2, t, t, 0, NULL, t));
    q = em_vu_sqrt_bits(t[0]);
    CL_VU(em_vu_vec_bits(EM_VU_ADDQ, 8, EM_VU_NO_BC, vf0, NULL, q, NULL, t));
    CL_VU(em_vu_div_bits(vf0[3], t[0], 3, 0, &q));
    CL_VU(em_vu_vec_bits(EM_VU_SUB, 15, EM_VU_NO_BC, vf0, vf0, 0, NULL, r));
    CL_VU(em_vu_vec_bits(EM_VU_MULQ, 14, EM_VU_NO_BC, v, NULL, q, NULL, r));
    cl_store4(out, r);
    return 0;
}
/* 00103230: out.xyz = v.xyz * s; out.w = v.w. */
static inline int cl_v_scale(void *out, const void *in, uint32_t s)
{
    uint32_t r[4], b[4] = { s, 0, 0, 0 };
    cl_load4(r, in);
    CL_VU(em_vu_vec_bits(EM_VU_MULBC, 14, 0, r, b, 0, NULL, r));
    cl_store4(out, r);
    return 0;
}
/* 00102900: out = v * s, all four lanes. */
static inline int cl_v_scale4(void *out, const void *in, uint32_t s)
{
    uint32_t v[4], r[4] = { 0, 0, 0, 0 }, b[4] = { s, 0, 0, 0 };
    cl_load4(v, in);
    CL_VU(em_vu_vec_bits(EM_VU_MULBC, 15, 0, v, b, 0, NULL, r));
    cl_store4(out, r);
    return 0;
}
/* 00102738: the xyz dot product (t.w = b.w is not read back). */
static inline int cl_v_dot(const void *a, const void *b, uint32_t *out)
{
    uint32_t x[4], t[4];
    cl_load4(x, a); cl_load4(t, b);
    CL_VU(em_vu_vec_bits(EM_VU_MUL, 14, EM_VU_NO_BC, x, t, 0, NULL, t));
    CL_VU(em_vu_vec_bits(EM_VU_ADDBC, 8, 1, t, t, 0, NULL, t));
    CL_VU(em_vu_vec_bits(EM_VU_ADDBC, 8, 2, t, t, 0, NULL, t));
    *out = t[0];
    return 0;
}
/* 00102948 */
static inline void cl_v_copy(void *out, const void *in) { memmove(out, in, 16); }
/* 001031E0 */
static inline void cl_v_copy3(void *out, const void *in) { memmove(out, in, 12); }

/* ---- Fault bookkeeping ------------------------------------------------------ */
static inline int cl_fault(EmCamLeftWorld *w, uint32_t address)
{
    if (w && w->fault == 0) w->fault = address;
    return -1;
}
/* Run a worker call; a missing worker or a negative result stops the
 * routine (fault = the callee's address). */
#define CL_CALL(w, address, fn, ...) \
    do { if (!(fn) || (fn)(__VA_ARGS__) < 0) return cl_fault((w), (address)); } while (0)
/* A worker the path needs; missing is a fault before any write. */
#define CL_NEED(w, address, ptr) do { if (!(ptr)) return cl_fault((w), (address)); } while (0)

/* The world's base pointers every entry point needs. */
static inline int cl_world_ok(const EmCamLeftWorld *w)
{
    return w && w->cam && w->globals && w->globals->follow && w->scratch && w->hit && w->workers;
}

#endif /* EM_CAMERA_LEFTOVERS_INTERNAL_H */
