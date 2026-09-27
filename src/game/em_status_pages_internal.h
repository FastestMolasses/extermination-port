/* Status pages lane: helpers shared by the em_status_pages_*.c files
 * (docs/STATUS_PAGES.md). Built on the AREA01 UI lane's memory, callee and
 * frame helpers (em_area01_ui_internal.h), reused unchanged. */
#ifndef EM_STATUS_PAGES_INTERNAL_H
#define EM_STATUS_PAGES_INTERNAL_H

#include "game/em_area01_ui_internal.h"
#include "game/em_status_pages.h"

/* Callees by original address (names are addresses). */
enum {
    SP_001281C0 = 0x001281C0u, /* float_to_int */
    SP_001C5FB0 = 0x001C5FB0u,
    SP_00123168 = 0x00123168u,
    SP_001CBA50 = 0x001CBA50u,
    SP_001FB9F0 = 0x001FB9F0u,
    SP_001FCF10 = 0x001FCF10u,
    SP_001FCF60 = 0x001FCF60u,
    SP_001FE070 = 0x001FE070u,
    SP_001FF080 = 0x001FF080u,
    SP_00207D00 = 0x00207D00u,
    SP_00207D90 = 0x00207D90u,
    SP_00207E40 = 0x00207E40u,
    SP_0020A7A0 = 0x0020A7A0u,
    SP_0020CD40 = 0x0020CD40u,
    SP_0020CD60 = 0x0020CD60u,
    SP_0020CD80 = 0x0020CD80u,
    SP_0020CDA0 = 0x0020CDA0u,
    SP_0020AE40 = 0x0020AE40u,
    SP_0020B0D0 = 0x0020B0D0u,
    SP_0020B210 = 0x0020B210u,
    SP_0020BEF0 = 0x0020BEF0u,
    SP_0020CCB0 = 0x0020CCB0u,
    SP_00185420 = 0x00185420u,
    SP_00182B30 = 0x00182B30u,
    SP_001C47E0 = 0x001C47E0u,
    SP_0015C700 = 0x0015C700u,
    SP_0015C750 = 0x0015C750u,
    SP_0020E020 = 0x0020E020u,
    SP_0020D930 = 0x0020D930u,
    SP_0020AC70 = 0x0020AC70u,
    SP_001026A0 = 0x001026A0u,
    SP_001029C0 = 0x001029C0u,
    SP_00102BB0 = 0x00102BB0u
};

#define SP_D_0028A49C 0x0028A49Cu /* the help-bank pointer */
#define SP_D_00265890 0x00265890u /* per-map floor rows */
#define SP_D_00265920 0x00265920u /* list-mode marker positions (halfword pairs) */
#define SP_D_002862C0 0x002862C0u /* the number string buffer */
#define SP_D_00265510 0x00265510u /* text style records */
#define SP_D_00265518 0x00265518u
#define SP_D_00275BD8 0x00275BD8u /* the module-load busy byte */
#define SP_D_008106C5 0x008106C5u

#define SP_RGBA_80 UINT64_C(0xFFFFFFFF80808080) /* 0x80808080 as lui/ori leave it */

/* A 64-bit texture word as the originals build it: the high word shifted
 * up 32 bits, OR the low word zero-extended (never sign-extended). */
#define SP_TEX(hi, lo) (((uint64_t)(uint32_t)(hi) << 32) | (uint64_t)(uint32_t)(lo))

/* 00207E40(1, x, y, w, h, rgba, tex): one sprite through slot 1; every
 * register image as given. */
static inline void sp_blit(EmArea01Ui *s, uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t rgba,
                           uint64_t tex)
{
    uint64_t a[7] = {1, x, y, w, h, rgba, tex};
    ui_call(s, SP_00207E40, 7, a, 0, NULL, NULL, NULL);
}

/* 00207D00(1, mode). */
static inline void sp_blend(EmArea01Ui *s, uint32_t mode) { (void)ui_call2(s, SP_00207D00, 1, mode); }

/* float_to_int(f): the v0 register image it returns. */
static inline uint64_t sp_f2i(EmArea01Ui *s, uint32_t f)
{
    uint64_t r;
    ui_call(s, SP_001281C0, 0, NULL, 1, &f, &r, NULL);
    return r;
}

/* 16.0f * (float)n through the EE: the operand order of the originals'
 * mul.s (16.0 first). */
static inline uint32_t sp_x16(int32_t n) { return ui_fmul(0x41800000u, ui_fcvt(n)); }

static inline int sp_ceq(uint32_t a, uint32_t b) { return em_ee_c_eq_bits(a, b); }
static inline int sp_clt(uint32_t a, uint32_t b) { return em_ee_c_lt_bits(a, b); }

/* The message request words. */
#define SP_B0 UI_D_002821B0
#define SP_B4 UI_D_002821B4
#define SP_B8 UI_D_002821B8
#define SP_40 UI_D_00282240

static inline int sp_begin(EmArea01Ui *s, uint32_t address)
{
    if (ui_latched(s)) return -1;
    if (!s->call) return em_a01r_fault(&s->core, address, EM_A01R_FAULT_NULL_WORKER, 0);
    return 0;
}

#endif /* EM_STATUS_PAGES_INTERNAL_H */
