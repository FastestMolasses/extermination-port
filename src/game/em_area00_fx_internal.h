/* AREA00 lane A00FX: the memory, callee, frame and VU0 helpers the lane's
 * translations share (docs/AREA00_FX.md).
 *
 * Sticky fail-stop: once core.fault is latched, loads read 0, stores are
 * dropped and no callee is called, so a routine runs on to its end without
 * touching anything; every entry then returns -1. Every loop of the lane is
 * bounded by a constant, a counter or a byte that then reads 0.
 *
 * Widths follow the instructions: lbu / lb / lh / lhu / lw, sb / sh / sw /
 * sd, and the quadword forms that ignore the low four address bits. */
#ifndef EM_AREA00_FX_INTERNAL_H
#define EM_AREA00_FX_INTERNAL_H

#include "game/em_area00_fx.h"

typedef uint32_t u32;
typedef uint64_t u64;
typedef EmArea00Fx S;

static inline int fx_latched(const S *s) { return em_a01r_latched(&s->core); }

static inline uint8_t *fx_mem(S *s, u32 a, u32 n, int write) { return fx_latched(s) ? NULL : em_a01r_mem(&s->core, a, n, write); }

static inline u32 fx_lbu(S *s, u32 a)
{
    uint8_t *p = fx_mem(s, a, 1, 0);
    return p ? p[0] : 0u;
}

static inline u32 fx_lhu(S *s, u32 a)
{
    uint8_t *p = fx_mem(s, a, 2, 0);
    return p ? (u32)p[0] | (u32)p[1] << 8 : 0u;
}

static inline int32_t fx_lh(S *s, u32 a) { return (int32_t)(int16_t)fx_lhu(s, a); }

static inline u32 fx_lw(S *s, u32 a)
{
    uint8_t *p = fx_mem(s, a, 4, 0);
    return p ? em_a01r_get32(p) : 0u;
}

static inline void fx_sb(S *s, u32 a, u32 v)
{
    uint8_t *p = fx_mem(s, a, 1, 1);
    if (p) p[0] = (uint8_t)v;
}

static inline void fx_sh(S *s, u32 a, u32 v)
{
    uint8_t *p = fx_mem(s, a, 2, 1);
    if (p) {
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8);
    }
}

static inline void fx_sw(S *s, u32 a, u32 v)
{
    uint8_t *p = fx_mem(s, a, 4, 1);
    if (p) em_a01r_put32(p, v);
}

static inline void fx_sd(S *s, u32 a, u64 v)
{
    uint8_t *p = fx_mem(s, a, 8, 1);
    if (p) {
        em_a01r_put32(p, (u32)v);
        em_a01r_put32(p + 4, (u32)(v >> 32));
    }
}

/* A quadword load / store: the low four address bits are ignored. */
static inline void fx_ldq(S *s, u32 a, u32 q[4])
{
    uint8_t *p = fx_mem(s, a & ~15u, 16, 0);
    for (int i = 0; i < 4; ++i) q[i] = p ? em_a01r_get32(p + 4 * i) : 0u;
}

static inline void fx_stq(S *s, u32 a, const u32 q[4])
{
    uint8_t *p = fx_mem(s, a & ~15u, 16, 1);
    if (p)
        for (int i = 0; i < 4; ++i) em_a01r_put32(p + 4 * i, q[i]);
}

static inline void fx_stq0(S *s, u32 a)
{
    static const u32 z[4] = {0, 0, 0, 0};
    fx_stq(s, a, z);
}

/* 00102948(dst, src): one quadword. */
static inline void fx_00102948(S *s, u32 dst, u32 src)
{
    u32 q[4];
    fx_ldq(s, src, q);
    fx_stq(s, dst, q);
}

/* 00102958(dst, src): four quadwords, all loaded before the first store. */
static inline void fx_00102958(S *s, u32 dst, u32 src)
{
    u32 q[4][4];
    for (u32 i = 0; i < 4; ++i) fx_ldq(s, src + 16u * i, q[i]);
    for (u32 i = 0; i < 4; ++i) fx_stq(s, dst + 16u * i, q[i]);
}

/* A 32-bit value as the EE holds it in a 64-bit register (lw, addiu). */
static inline u64 fx_sx(u32 v) { return (u64)(int64_t)(int32_t)v; }

/* ---- callees ------------------------------------------------------------ */

enum { R_A0 = 4, R_A1 = 5, R_A2 = 6, R_A3 = 7, R_T0 = 8, R_T1 = 9, R_S4 = 20 };

static inline void fx_arg(EmArea00FxRegs *r, int reg, u64 v)
{
    r->imask |= 1u << reg;
    r->r[reg] = v;
}

static inline void fx_farg(EmArea00FxRegs *r, int reg, u32 bits)
{
    r->fmask |= 1u << reg;
    r->f[reg] = bits;
}

/* One callee call with the registers in `in`. v0 / f0 may be NULL. */
static inline void fx_call(S *s, u32 target, const EmArea00FxRegs *in, u64 *v0, u32 *f0)
{
    u64 rv = 0;
    u32 rf = 0;
    if (!fx_latched(s)) {
        if (!s->call) {
            em_a01r_fault(&s->core, target, EM_A01R_FAULT_NULL_WORKER, 0);
        } else {
            const u32 fn = s->core.function;
            const int rc = s->call(s->ctx, target, s->sp, in, &rv, &rf);
            s->core.function = fn;
            if (rc < 0) {
                em_a01r_fault(&s->core, target, EM_A01R_FAULT_WORKER_FAILED, 0);
                rv = 0;
                rf = 0;
            }
        }
    }
    if (v0) *v0 = rv;
    if (f0) *f0 = rf;
}

/* Integer-only shorthands: the registers a0.. in order. */
static inline u64 fx_call0(S *s, u32 target)
{
    EmArea00FxRegs r = {0};
    u64 v0;
    fx_call(s, target, &r, &v0, NULL);
    return v0;
}

static inline u64 fx_call1(S *s, u32 target, u64 a0)
{
    EmArea00FxRegs r = {0};
    u64 v0;
    fx_arg(&r, R_A0, a0);
    fx_call(s, target, &r, &v0, NULL);
    return v0;
}

static inline u64 fx_call2(S *s, u32 target, u64 a0, u64 a1)
{
    EmArea00FxRegs r = {0};
    u64 v0;
    fx_arg(&r, R_A0, a0);
    fx_arg(&r, R_A1, a1);
    fx_call(s, target, &r, &v0, NULL);
    return v0;
}

static inline u64 fx_call3(S *s, u32 target, u64 a0, u64 a1, u64 a2)
{
    EmArea00FxRegs r = {0};
    u64 v0;
    fx_arg(&r, R_A0, a0);
    fx_arg(&r, R_A1, a1);
    fx_arg(&r, R_A2, a2);
    fx_call(s, target, &r, &v0, NULL);
    return v0;
}

static inline u64 fx_call4(S *s, u32 target, u64 a0, u64 a1, u64 a2, u64 a3)
{
    EmArea00FxRegs r = {0};
    u64 v0;
    fx_arg(&r, R_A0, a0);
    fx_arg(&r, R_A1, a1);
    fx_arg(&r, R_A2, a2);
    fx_arg(&r, R_A3, a3);
    fx_call(s, target, &r, &v0, NULL);
    return v0;
}

/* (a0, a1) and f12: 00102900 / 00102BB0 / 001CA7B0-style leaves. */
static inline u64 fx_call2f(S *s, u32 target, u64 a0, u64 a1, u32 f12)
{
    EmArea00FxRegs r = {0};
    u64 v0;
    fx_arg(&r, R_A0, a0);
    fx_arg(&r, R_A1, a1);
    fx_farg(&r, 12, f12);
    fx_call(s, target, &r, &v0, NULL);
    return v0;
}

/* f12 only; returns f0 (0011DE90 / 0011E2A8). */
static inline u32 fx_callf(S *s, u32 target, u32 f12)
{
    EmArea00FxRegs r = {0};
    u32 f0;
    fx_farg(&r, 12, f12);
    fx_call(s, target, &r, NULL, &f0);
    return f0;
}

/* ---- frames -------------------------------------------------------------- */

typedef struct {
    u32 function;
    u32 sp;
} FxFrame;

static inline FxFrame fx_enter(S *s, u32 address, u32 frame)
{
    FxFrame f = {s->core.function, s->sp};
    s->core.function = address;
    s->sp -= frame;
    return f;
}

static inline void fx_leave(S *s, FxFrame f)
{
    s->core.function = f.function;
    s->sp = f.sp;
}

static inline int fx_result(const S *s) { return fx_latched(s) ? -1 : 0; }

/* Code 6: an operation outside the measured model. */
static inline void fx_unmeasured(S *s, u32 detail)
{
    if (!fx_latched(s)) em_a01r_fault(&s->core, s->core.function, EM_A01R_FAULT_UNMEASURED, detail);
}

/* ---- COP1 ------------------------------------------------------------------ */

static inline u32 fx_fadd(u32 a, u32 b) { return em_ee_add_bits(a, b); }
static inline u32 fx_fsub(u32 a, u32 b) { return em_ee_sub_bits(a, b); }
static inline u32 fx_fmul(u32 a, u32 b) { return em_ee_mul_bits(a, b); }
static inline u32 fx_fdiv(u32 a, u32 b) { return em_ee_div_bits(a, b); }
static inline u32 fx_fcvt(u32 w) { return em_ee_cvt_s_w_bits(w); }
static inline int fx_flt(u32 a, u32 b) { return em_ee_c_lt_bits(a, b); }
static inline int fx_fle(u32 a, u32 b) { return em_ee_c_le_bits(a, b); }
static inline int fx_feq(u32 a, u32 b) { return em_ee_c_eq_bits(a, b); }

#define FX_F_ONE 0x3F800000u
#define FX_F_HALF 0x3F000000u
#define FX_F_2POW31 0x4F000000u /* 2147483648.0 */
#define FX_F_PI 0x40490FDBu
#define FX_F_2PI 0x40C90FDBu
#define FX_F_HALFPI 0x3FC90FDBu
#define FX_F_180 0x43340000u
#define FX_F_360 0x43B40000u
#define FX_F_65535 0x477FFF00u
#define FX_F_1EM4 0x38D1B717u /* 0.0001 */
#define FX_F_10 0x41200000u

/* ---- VU0 -------------------------------------------------------------------- */

static const u32 FX_VF0[4] = {0, 0, 0, FX_F_ONE};

/* One VU macro instruction; a form outside the measured table faults 6. */
static inline void fx_vu(S *s, em_vu_op op, unsigned dest, int bc, const u32 fs[4], const u32 ft[4], u32 q,
                         const u32 acc[4], u32 dst[4])
{
    if (fx_latched(s)) return;
    if (em_vu_vec_bits(op, dest, bc, fs, ft, q, acc, dst) != EM_EE_FLOAT_OK) fx_unmeasured(s, 0);
}

/* vdiv Q = vf0.w / w (the reciprocal form (3,3)). */
static inline u32 fx_vrcp(S *s, u32 w)
{
    u32 q = 0;
    if (!fx_latched(s) && em_vu_div_bits(FX_F_ONE, w, 3, 3, &q) != EM_EE_FLOAT_OK) fx_unmeasured(s, 0);
    return q;
}

/* out = m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3] * vf0.w, all four lanes
 * (the multiply-accumulate chain ending in the vf0.w form); v.w is not read. */
static inline void fx_transform(S *s, const u32 m[4][4], const u32 v[4], u32 out[4])
{
    u32 acc[4] = {0, 0, 0, 0};
    fx_vu(s, EM_VU_MULABC, 15, 0, m[0], v, 0, NULL, acc);
    fx_vu(s, EM_VU_MADDABC, 15, 1, m[1], v, 0, acc, acc);
    fx_vu(s, EM_VU_MADDABC, 15, 2, m[2], v, 0, acc, acc);
    fx_vu(s, EM_VU_MADDBC, 15, 3, m[3], FX_VF0, 0, acc, out);
}

/* The clip judgment of v.x, v.y, v.z against |v.w| (flag bits +x, -x, +y,
 * -y, +z, -z), the rule of em_area01_render_mem.h: DAZ, then magnitude
 * compares; a lane with exponent 255 faults 6 (unmeasured). */
static inline u32 fx_clipw(S *s, const u32 v[4])
{
    if (fx_latched(s)) return 0;
    for (int i = 0; i < 4; ++i)
        if (((v[i] >> 23) & 0xFFu) == 0xFFu) {
            fx_unmeasured(s, 0);
            return 0;
        }
    const u32 w = em_eei_daz(v[3]) & 0x7FFFFFFFu;
    u32 f = 0;
    for (int k = 0; k < 3; ++k) {
        const u32 x = em_eei_daz(v[k]);
        if ((x & 0x7FFFFFFFu) > w) f |= (x >> 31) ? 2u << (2 * k) : 1u << (2 * k);
    }
    return f;
}

static inline void fx_ldm(S *s, u32 a, u32 m[4][4])
{
    for (u32 i = 0; i < 4; ++i) fx_ldq(s, a + 16u * i, m[i]);
}

/* The fog lane shared by the projections: w' = 1.0 * ca0.z + ca0.w * w (two
 * rounded steps), then the smaller of that and ca0.x, then the larger of
 * that and +0 (raw VU order). */
static inline u32 fx_fog(S *s, const u32 ca0[4], u32 v[4])
{
    u32 acc[4] = {0, 0, 0, 0};
    fx_vu(s, EM_VU_MULABC, 1, 2, FX_VF0, ca0, 0, NULL, acc);
    fx_vu(s, EM_VU_MADDBC, 1, 3, ca0, v, 0, acc, v);
    v[3] = em_vu_min_bits(v[3], ca0[0]);
    v[3] = em_vu_max_bits(v[3], 0);
    return v[3];
}

static inline void fx_ftoi4(u32 v[4])
{
    for (int i = 0; i < 4; ++i) v[i] = em_vu_ftoi4_bits(v[i]);
}

/* Original data and scratchpad addresses the lane names. */
#define FX_D_00275670 0x00275670u /* the render context pointer */
#define FX_D_00275B40 0x00275B40u /* the current bone-slot array */
#define FX_D_00275B7C 0x00275B7Cu /* 001AA840's list */
#define FX_D_00275B84 0x00275B84u /* halfword: its count */
#define FX_D_00275BCC 0x00275BCCu /* halfword: free bone slots */
#define FX_D_00275C30 0x00275C30u /* the 001EA240 node */
#define FX_D_00275C34 0x00275C34u /* its work block */
#define FX_D_00275C44 0x00275C44u /* 001F3620's sound gap */
#define FX_D_00275C48 0x00275C48u /* 001F3620's splash count */
#define FX_D_0028A490 0x0028A490u /* 001F3E30's table of tables */
#define FX_D_0028A56C 0x0028A56Cu
#define FX_D_007635C0 0x007635C0u /* the chain table */
#define FX_D_0081F8F0 0x0081F8F0u

/* The callees, by original address. */
enum {
    FX_0011DE90 = 0x0011DE90u, FX_0011E2A8 = 0x0011E2A8u, FX_00122BB8 = 0x00122BB8u,
    FX_001026A0 = 0x001026A0u, FX_001026D0 = 0x001026D0u, FX_001028B8 = 0x001028B8u,
    FX_001028D0 = 0x001028D0u, FX_00102900 = 0x00102900u, FX_00102918 = 0x00102918u,
    FX_00102990 = 0x00102990u, FX_001029C0 = 0x001029C0u, FX_00102BB0 = 0x00102BB0u,
    FX_00102C58 = 0x00102C58u, FX_001031E0 = 0x001031E0u, FX_001281C0 = 0x001281C0u,
    FX_0019A570 = 0x0019A570u, FX_001AA7A0 = 0x001AA7A0u, FX_001AF780 = 0x001AF780u,
    FX_001AFA90 = 0x001AFA90u, FX_001AFC10 = 0x001AFC10u, FX_001B17A0 = 0x001B17A0u,
    FX_001C22A0 = 0x001C22A0u, FX_001C6120 = 0x001C6120u, FX_001C6150 = 0x001C6150u,
    FX_001C6200 = 0x001C6200u, FX_001C62C0 = 0x001C62C0u, FX_001C6380 = 0x001C6380u,
    FX_001C63D0 = 0x001C63D0u, FX_001C7900 = 0x001C7900u, FX_001C9E40 = 0x001C9E40u,
    FX_001CA1C0 = 0x001CA1C0u, FX_001CA3B0 = 0x001CA3B0u, FX_001CA4D0 = 0x001CA4D0u,
    FX_001CA5E0 = 0x001CA5E0u, FX_001CA7B0 = 0x001CA7B0u, FX_001CA940 = 0x001CA940u,
    FX_001CAAC0 = 0x001CAAC0u, FX_001CB5B0 = 0x001CB5B0u, FX_001CB5F0 = 0x001CB5F0u,
    FX_001CB6B0 = 0x001CB6B0u, FX_001CB760 = 0x001CB760u, FX_001CB900 = 0x001CB900u,
    FX_001CCF70 = 0x001CCF70u, FX_001CD370 = 0x001CD370u, FX_001CD390 = 0x001CD390u,
    FX_001CFA60 = 0x001CFA60u, FX_001CFB50 = 0x001CFB50u, FX_001CFBE0 = 0x001CFBE0u,
    FX_001D3990 = 0x001D3990u, FX_001D80E0 = 0x001D80E0u, FX_001D8C20 = 0x001D8C20u,
    FX_001EEEB0 = 0x001EEEB0u, FX_001EF9D0 = 0x001EF9D0u, FX_001EFD20 = 0x001EFD20u,
    FX_001EFD90 = 0x001EFD90u, FX_001F4BF0 = 0x001F4BF0u, FX_001F4D40 = 0x001F4D40u,
    FX_001FBD50 = 0x001FBD50u
};

#endif /* EM_AREA00_FX_INTERNAL_H */
