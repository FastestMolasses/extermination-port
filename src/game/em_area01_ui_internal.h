/* AREA01 lane UI: the memory, callee and frame helpers shared by
 * em_area01_ui_pages.c and em_area01_ui_effect.c (docs/AREA01_UI.md).
 *
 * Sticky fail-stop: once core.fault is latched, loads read 0, stores are
 * dropped and no callee is called, so a routine can run on to its end
 * without touching anything; every entry then returns -1. A loop whose
 * bound comes from memory tests the latch itself (0022B7A0's scan).
 *
 * Widths follow the instructions: lbu / lb / lh / lhu / lw, sb / sh / sw /
 * sd, and the quadword forms that ignore the low four address bits. */
#ifndef EM_AREA01_UI_INTERNAL_H
#define EM_AREA01_UI_INTERNAL_H

#include "game/em_area01_ui.h"

static inline int ui_latched(const EmArea01Ui *s) { return em_a01r_latched(&s->core); }

static inline uint8_t *ui_mem(EmArea01Ui *s, uint32_t a, uint32_t n)
{
    return ui_latched(s) ? NULL : em_a01r_mem(&s->core, a, n);
}

static inline uint32_t ui_lbu(EmArea01Ui *s, uint32_t a)
{
    uint8_t *p = ui_mem(s, a, 1);
    return p ? p[0] : 0u;
}

static inline int32_t ui_lb(EmArea01Ui *s, uint32_t a) { return (int32_t)(int8_t)ui_lbu(s, a); }

static inline uint32_t ui_lhu(EmArea01Ui *s, uint32_t a)
{
    uint8_t *p = ui_mem(s, a, 2);
    return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 : 0u;
}

static inline int32_t ui_lh(EmArea01Ui *s, uint32_t a) { return (int32_t)(int16_t)ui_lhu(s, a); }

static inline uint32_t ui_lw(EmArea01Ui *s, uint32_t a)
{
    uint8_t *p = ui_mem(s, a, 4);
    return p ? em_a01r_get32(p) : 0u;
}

static inline void ui_sb(EmArea01Ui *s, uint32_t a, uint32_t v)
{
    uint8_t *p = ui_mem(s, a, 1);
    if (p) p[0] = (uint8_t)v;
}

static inline void ui_sh(EmArea01Ui *s, uint32_t a, uint32_t v)
{
    uint8_t *p = ui_mem(s, a, 2);
    if (p) {
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8);
    }
}

static inline void ui_sw(EmArea01Ui *s, uint32_t a, uint32_t v)
{
    uint8_t *p = ui_mem(s, a, 4);
    if (p) em_a01r_put32(p, v);
}

static inline void ui_sd(EmArea01Ui *s, uint32_t a, uint64_t v)
{
    uint8_t *p = ui_mem(s, a, 8);
    if (p) {
        em_a01r_put32(p, (uint32_t)v);
        em_a01r_put32(p + 4, (uint32_t)(v >> 32));
    }
}

/* A quadword load / store: the low four address bits are ignored. */
static inline void ui_ldq(EmArea01Ui *s, uint32_t a, uint32_t q[4])
{
    uint8_t *p = ui_mem(s, a & ~15u, 16);
    for (int i = 0; i < 4; ++i) q[i] = p ? em_a01r_get32(p + 4 * i) : 0u;
}

static inline void ui_stq(EmArea01Ui *s, uint32_t a, const uint32_t q[4])
{
    uint8_t *p = ui_mem(s, a & ~15u, 16);
    if (p)
        for (int i = 0; i < 4; ++i) em_a01r_put32(p + 4 * i, q[i]);
}

/* 00102948(dst, src): one quadword. */
static inline void ui_00102948(EmArea01Ui *s, uint32_t dst, uint32_t src)
{
    uint32_t q[4];
    ui_ldq(s, src, q);
    ui_stq(s, dst, q);
}

/* 00102958(dst, src): four quadwords, all loaded before the first store. */
static inline void ui_00102958(EmArea01Ui *s, uint32_t dst, uint32_t src)
{
    uint32_t q[4][4];
    for (uint32_t i = 0; i < 4; ++i) ui_ldq(s, src + 16u * i, q[i]);
    for (uint32_t i = 0; i < 4; ++i) ui_stq(s, dst + 16u * i, q[i]);
}

/* A 32-bit integer as the EE holds it in a 64-bit register. */
static inline uint64_t ui_sx(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

/* One callee call. v0 / f0 may be NULL. */
static inline void ui_call(EmArea01Ui *s, uint32_t target, unsigned na, const uint64_t *a, unsigned nf,
                           const uint32_t *f, uint64_t *v0, uint32_t *f0)
{
    uint64_t rv = 0;
    uint32_t rf = 0;
    if (!ui_latched(s)) {
        if (!s->call) {
            em_a01r_fault(&s->core, target, EM_A01R_FAULT_NULL_WORKER, 0);
        } else {
            const uint32_t fn = s->core.function;
            const int rc = s->call(s->ctx, target, s->sp, a, na, f, nf, &rv, &rf);
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

/* Integer-only shorthands (arguments already as register images). */
static inline uint32_t ui_call0(EmArea01Ui *s, uint32_t target)
{
    uint64_t v0;
    ui_call(s, target, 0, NULL, 0, NULL, &v0, NULL);
    return (uint32_t)v0;
}

static inline uint32_t ui_call1(EmArea01Ui *s, uint32_t target, uint64_t a0)
{
    uint64_t v0, a[1] = {a0};
    ui_call(s, target, 1, a, 0, NULL, &v0, NULL);
    return (uint32_t)v0;
}

static inline uint32_t ui_call2(EmArea01Ui *s, uint32_t target, uint64_t a0, uint64_t a1)
{
    uint64_t v0, a[2] = {a0, a1};
    ui_call(s, target, 2, a, 0, NULL, &v0, NULL);
    return (uint32_t)v0;
}

static inline uint32_t ui_call3(EmArea01Ui *s, uint32_t target, uint64_t a0, uint64_t a1, uint64_t a2)
{
    uint64_t v0, a[3] = {a0, a1, a2};
    ui_call(s, target, 3, a, 0, NULL, &v0, NULL);
    return (uint32_t)v0;
}

static inline uint32_t ui_call4(EmArea01Ui *s, uint32_t target, uint64_t a0, uint64_t a1, uint64_t a2,
                                uint64_t a3)
{
    uint64_t v0, a[4] = {a0, a1, a2, a3};
    ui_call(s, target, 4, a, 0, NULL, &v0, NULL);
    return (uint32_t)v0;
}

/* Routine bookkeeping: the running original address (fault attribution)
 * and the frame the original allocates below sp. */
typedef struct {
    uint32_t function;
    uint32_t sp;
} UiFrame;

static inline UiFrame ui_enter(EmArea01Ui *s, uint32_t address, uint32_t frame)
{
    UiFrame f = {s->core.function, s->sp};
    s->core.function = address;
    s->sp -= frame;
    return f;
}

static inline int ui_leave(EmArea01Ui *s, UiFrame f)
{
    s->core.function = f.function;
    s->sp = f.sp;
    return ui_latched(s) ? -1 : 0;
}

/* Code 6: the original would use a register value its caller never set. */
static inline void ui_unmeasured(EmArea01Ui *s, uint32_t detail)
{
    if (!ui_latched(s)) em_a01r_fault(&s->core, s->core.function, EM_A01R_FAULT_UNMEASURED, detail);
}

/* The callees, by original address (names are addresses; roles in
 * docs/AREA01_UI.md where the code shows them). */
enum {
    UI_001026A0 = 0x001026A0u, UI_001026D0 = 0x001026D0u, UI_00102760 = 0x00102760u,
    UI_001028B8 = 0x001028B8u, UI_001029C0 = 0x001029C0u, UI_00102BB0 = 0x00102BB0u,
    UI_001031E0 = 0x001031E0u, UI_00103230 = 0x00103230u, UI_00122BB8 = 0x00122BB8u,
    UI_00123168 = 0x00123168u, UI_001281C0 = 0x001281C0u, UI_001AF780 = 0x001AF780u,
    UI_001AF7C0 = 0x001AF7C0u, UI_001AFC10 = 0x001AFC10u, UI_001AFF10 = 0x001AFF10u,
    UI_001AFF90 = 0x001AFF90u, UI_001B0000 = 0x001B0000u, UI_001B1470 = 0x001B1470u,
    UI_001C5FB0 = 0x001C5FB0u, UI_001C6120 = 0x001C6120u, UI_001C6150 = 0x001C6150u,
    UI_001C62C0 = 0x001C62C0u, UI_001C6380 = 0x001C6380u, UI_001CA5E0 = 0x001CA5E0u,
    UI_001CA6E0 = 0x001CA6E0u, UI_001CB5B0 = 0x001CB5B0u, UI_001CBA50 = 0x001CBA50u,
    UI_001CCF70 = 0x001CCF70u, UI_001CD070 = 0x001CD070u, UI_001CD2B0 = 0x001CD2B0u,
    UI_001CFB50 = 0x001CFB50u, UI_001CFBE0 = 0x001CFBE0u, UI_001F0190 = 0x001F0190u,
    UI_001F0290 = 0x001F0290u, UI_001FB9F0 = 0x001FB9F0u, UI_001FCF30 = 0x001FCF30u,
    UI_00207D00 = 0x00207D00u, UI_00207D90 = 0x00207D90u, UI_00207E40 = 0x00207E40u,
    UI_00208040 = 0x00208040u, UI_0020A7A0 = 0x0020A7A0u,
    UI_0020BEF0 = 0x0020BEF0u, UI_0020CD40 = 0x0020CD40u, UI_0020CD60 = 0x0020CD60u,
    UI_0020CD80 = 0x0020CD80u, UI_0020CDA0 = 0x0020CDA0u, UI_00211240 = 0x00211240u,
    UI_00211310 = 0x00211310u, UI_002117D0 = 0x002117D0u, UI_00213A00 = 0x00213A00u,
    UI_00213C50 = 0x00213C50u, UI_00213CC0 = 0x00213CC0u, UI_0021B9A0 = 0x0021B9A0u
};

/* Original data addresses the lane reads or writes by name. */
#define UI_D_00275670 0x00275670u /* the render context pointer */
#define UI_D_00275B40 0x00275B40u /* the current bone-slot array (0022BB70) */
#define UI_D_00275BCC 0x00275BCCu /* halfword: the bone-slot cap */
#define UI_D_00275870 0x00275870u /* 002134C0's text style word */
#define UI_D_002821B0 0x002821B0u
#define UI_D_002821B4 0x002821B4u
#define UI_D_002821B8 0x002821B8u
#define UI_D_00282240 0x00282240u
#define UI_D_00282244 0x00282244u
#define UI_D_00810700 0x00810700u /* byte: the area; +0x5B8.. / +0x5C3.. owned tables */
#define UI_D_008106B0 0x008106B0u /* the pending status request and its type */
#define UI_D_008106B1 0x008106B1u
#define UI_D_008106CD 0x008106CDu
#define UI_D_00810CB8 0x00810CB8u
#define UI_D_00810E70 0x00810E70u /* pad halfwords */
#define UI_D_00810E74 0x00810E74u
#define UI_D_00810E78 0x00810E78u
#define UI_SPAD_3B64 0x70003B64u  /* the main-loop counter */

#define UI_F_ONE 0x3F800000u
#define UI_F_TWO 0x40000000u
#define UI_F_HALF 0x3F000000u
#define UI_F_K 0x3DDA740Eu /* 0.10666667 */

static inline uint32_t ui_fadd(uint32_t a, uint32_t b) { return em_ee_add_bits(a, b); }
static inline uint32_t ui_fsub(uint32_t a, uint32_t b) { return em_ee_sub_bits(a, b); }
static inline uint32_t ui_fmul(uint32_t a, uint32_t b) { return em_ee_mul_bits(a, b); }
static inline uint32_t ui_fdiv(uint32_t a, uint32_t b) { return em_ee_div_bits(a, b); }
static inline uint32_t ui_fneg(uint32_t a) { return em_ee_neg_bits(a); }
static inline uint32_t ui_fcvt(int32_t w) { return em_ee_cvt_s_w_bits((uint32_t)w); }

#endif /* EM_AREA01_UI_INTERNAL_H */
