/* em_vu1_page_programs.h — the two VU1 programs the chain page D_007635C0
 * CALLs, translated from their VU1 microcode into CPU-exact C
 * (docs/CHAIN_PAGE.md section 3).
 *
 *   lane program    DMA packet D_00233290 (001F0720's CALL): one MPG of 138
 *                   instructions (ELF 0x002332B8) to micro 0, 14 constant
 *                   rows to dmem 0..13, BASE 0x320, OFFSET 0x70.
 *   sprite program  DMA packet 0x00231770 (001CFBE0's table of kinds 1 and
 *                   5): MPGs of 256 (ELF 0x00231798) and 79 (ELF 0x00231FA0)
 *                   instructions to micro 0 and 0x100, a 128-word lookup
 *                   (V1-32, all four lanes) to dmem 0..127 and 17 rows to
 *                   dmem 0x6E..0x7E, BASE 0, OFFSET 0.
 *
 * Micro addresses below are instruction indices of the uploaded program.
 * Both programs are entered by MSCAL 0 only (every captured page).
 *
 * Execution model (the reference test's VU1 machine, tools/chain_page_model.py
 * VuOracle, whose MPG bytes are the ELF's):
 * - Every instruction pair runs in program order, the lower op reading its
 *   registers before the upper op of the same pair writes; the machine
 *   stalls on a VF operand still in the FMAC / load pipeline, so a stall
 *   never changes a value read.
 * - Q, the MAC flags and the clip flags are not interlocked: a read sees
 *   the value of the producer that settled 7 (Q) or 4 (flags) cycles
 *   earlier. On every path of both programs the reader sees exactly one
 *   producer, the same on every run (the test logs the producer of each
 *   read over the captured pages and the synthetic cases):
 *     lane   : the MULQs at micro 0x067 / 0x068 read the DIV of 0x060; the
 *              clip test at 0x069 sees the CLIP of 0x064 (and the two
 *              earlier vertices' judgements of the same slot).
 *     sprite : 0x00F reads the DIV of 0x008; 0x0D5 / 0x0D6 that of 0x0CE;
 *              0x0DF that of 0x0D6; 0x11E that of 0x117; 0x125 that of
 *              0x11E; the MAC test at 0x06A sees the op of 0x066, at 0x071
 *              the op of 0x06D (0x09E / 0x0A5 likewise 0x09A / 0x0A1); the
 *              clip test at 0x123 sees the CLIP of 0x11F.
 *   A DIV in the same pair as a Q read (0x0D6, 0x11E) starts after that
 *   read, so the translation performs the read first.
 * - Arithmetic: the VU0 lane rules of em_ee_float.h, ASSUMED for VU1 as in
 *   em_vu1_object_kernel.h: DAZ operands, truncated results, FTZ, finite
 *   overflow to +-MAX; the multiply-add is a truncated product added to
 *   ACC; VDIV truncated, a zero divisor giving +-MAX by the sign XOR;
 *   VFTOI saturating, VITOF, raw VMAX / VMINI order, VABS the sign clear.
 *   An exponent-255 word reaching a multiply, add or divide on a live lane
 *   is not established: the MSCAL faults (EM_VU1P_FAULT_OPERAND). No
 *   captured page has one.
 * - The MAC flags the sprite program tests are the sign bits of the tested
 *   lanes (Sx 0x80 and Sw 0x10 of the one written lane).
 * - The random unit (sprite program): RINIT loads the 23-bit R from the
 *   mantissa of fs.fsf, RXOR XORs a mantissa into it, RNEXT steps it
 *   (R << 1 | ((R >> 22) ^ (R >> 4)) & 1, 23 bits) and writes 1.R to the
 *   destination lanes (docs/SNOW_PARTICLES.md).
 *
 * The register file lives across MSCALs (EmVu1PRegs): each program reads
 * only registers it wrote in the same MSCAL, except the sprite program's
 * VF11.w, which it stores (ST's unused w lane of every sprite) without
 * writing it. XGKICK hands the kicked dmem address to the caller, which
 * reads the GIF packet at once (the programs double-buffer their output).
 *
 * Header-only (static inline), pure C, no host float arithmetic. */
#ifndef EM_VU1_PAGE_PROGRAMS_H
#define EM_VU1_PAGE_PROGRAMS_H

#include <stdint.h>
#include <string.h>

#include "game/em_ee_float.h"

#define EM_VU1P_DMEM_QWORDS 1024u

enum {
    EM_VU1P_OK = 0,
    EM_VU1P_FAULT_ARGS = 1,     /* NULL argument                          */
    EM_VU1P_FAULT_OPERAND = 2,  /* an exponent-255 word reached a live lane */
    EM_VU1P_FAULT_KICK = 3,     /* the XGKICK consumer refused the packet  */
    EM_VU1P_FAULT_RUNAWAY = 4   /* more batches than the count allows     */
};

typedef struct { uint32_t w[4]; } EmVu1PQword;

typedef struct {
    uint32_t vf[32][4];
    uint16_t vi[16];
    uint32_t acc[4];
    uint32_t q, i, r;
    uint32_t cf;          /* 24-bit clip history, newest judgement low */
} EmVu1PRegs;

/* XGKICK of dmem qword `at`: 0 to continue, nonzero to fault. */
typedef int (*EmVu1PKick)(void *ctx, const EmVu1PQword *dmem, uint32_t at);

/* Reset to the power-on register state: VF0 = (0, 0, 0, 1), the rest 0. */
static inline void em_vu1p_regs_reset(EmVu1PRegs *r)
{
    memset(r, 0, sizeof *r);
    r->vf[0][3] = EM_EE_ONE;
}

/* ------------------------------------------------------- lane arithmetic */

typedef struct {
    EmVu1PRegs *r;
    EmVu1PQword *m;
    uint32_t bad;
} EmVu1PCtx;

static inline uint32_t emvup_chk(EmVu1PCtx *c, uint32_t w)
{
    if ((w & 0x7F800000u) == 0x7F800000u) c->bad = 1u;
    return w;
}
static inline uint32_t emvup_add(EmVu1PCtx *c, uint32_t a, uint32_t b)
{
    return em_eei_vu_add_raw(emvup_chk(c, a), emvup_chk(c, b));
}
static inline uint32_t emvup_sub(EmVu1PCtx *c, uint32_t a, uint32_t b)
{
    return em_eei_vu_sub_raw(emvup_chk(c, a), emvup_chk(c, b));
}
static inline uint32_t emvup_mul(EmVu1PCtx *c, uint32_t a, uint32_t b)
{
    return em_eei_vu_mul_raw(emvup_chk(c, a), emvup_chk(c, b));
}
static inline uint32_t emvup_madd(EmVu1PCtx *c, uint32_t acc, uint32_t a, uint32_t b)
{
    return emvup_add(c, acc, emvup_mul(c, a, b));
}
static inline uint32_t emvup_msub(EmVu1PCtx *c, uint32_t acc, uint32_t a, uint32_t b)
{
    return emvup_sub(c, acc, emvup_mul(c, a, b));
}
/* VDIV on live operands (the forms differ only for a NaN divisor). */
static inline uint32_t emvup_div(EmVu1PCtx *c, uint32_t a, uint32_t b)
{
    a = em_eei_daz(emvup_chk(c, a));
    b = em_eei_daz(emvup_chk(c, b));
    if (em_eei_is_zero(b)) return ((a ^ b) & EM_EE_SIGN) | EM_EE_MAX;
    if (c->bad) return 0;
    return em_eei_quotient(a, b, 0);
}

/* dmem access, addresses modulo 1024 qwords. */
static inline void emvup_lq(EmVu1PCtx *c, unsigned reg, uint32_t at)
{
    memcpy(c->r->vf[reg], c->m[at & 1023u].w, 16);
}
static inline void emvup_sq(EmVu1PCtx *c, unsigned reg, uint32_t at)
{
    memcpy(c->m[at & 1023u].w, c->r->vf[reg], 16);
}
/* ILW / ISW on one field (x, except the sprite program's flags read of
 * row 88's z at micro 0x053). */
static inline uint16_t emvup_ilw_field(EmVu1PCtx *c, uint32_t at, unsigned field)
{
    return (uint16_t)(c->m[at & 1023u].w[field] & 0xFFFFu);
}
static inline uint16_t emvup_ilw(EmVu1PCtx *c, uint32_t at)
{
    return emvup_ilw_field(c, at, 0);
}
static inline void emvup_isw(EmVu1PCtx *c, uint16_t v, uint32_t at)
{
    c->m[at & 1023u].w[0] = v;
}

/* CLIP of (x, y, z) against |w| on DAZ-read values, as
 * em_vu1_object_kernel.h's (bits +x -x +y -y +z -z). */
static inline int64_t emvup_key(uint32_t b)
{
    b = em_eei_daz(b);
    return (b & EM_EE_SIGN) ? -(int64_t)(b & 0x7FFFFFFFu) : (int64_t)b;
}
static inline void emvup_clip(EmVu1PCtx *c, const uint32_t v[4])
{
    const int64_t w = emvup_key(v[3] & 0x7FFFFFFFu), nw = -w;
    uint32_t f = 0;
    for (unsigned k = 0; k < 3; ++k) {
        const int64_t x = emvup_key(v[k]);
        if (x > w) f |= 1u << (2 * k);
        if (x < nw) f |= 2u << (2 * k);
    }
    c->r->cf = ((c->r->cf << 6) | f) & 0xFFFFFFu;
}

#define EMVUP_VF(c, n) ((c)->r->vf[n])
#define EMVUP_VI(c, n) ((c)->r->vi[n])

/* ACC = A * p.x; ACC += B * p.y; ACC += C * p.z; dst = ACC + D * VF0.w, on
 * all four lanes (the four-row transform both programs use). */
static inline void emvup_xform(EmVu1PCtx *c, unsigned dst, unsigned rows, const uint32_t p[4])
{
    uint32_t in[3] = { p[0], p[1], p[2] };
    EmVu1PRegs *r = c->r;
    for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_mul(c, r->vf[rows][k], in[0]);
    for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_madd(c, r->acc[k], r->vf[rows + 1][k], in[1]);
    for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_madd(c, r->acc[k], r->vf[rows + 2][k], in[2]);
    for (unsigned k = 0; k < 4; ++k) r->vf[dst][k] = emvup_madd(c, r->acc[k], r->vf[rows + 3][k], r->vf[0][3]);
}

/* ============================================================ lane ===== */

/* One vertex (micro 0x04E..0x088): corner vi08, ST row vi05, output at
 * vi09 (ST, RGBAQ, XYZF2). The slot's matrix is VF16..VF19, its colour
 * VF15; VF20..VF23 the clip projection, VF24..VF27 K, VF28 the fog row. */
static inline void emvup_lane_vertex(EmVu1PCtx *c)
{
    EmVu1PRegs *r = c->r;
    emvup_lq(c, 1, r->vi[8]);                                          /* 0x04E */
    emvup_lq(c, 2, r->vi[5]);                                          /* 0x04F */
    uint32_t p[4];
    memcpy(p, r->vf[1], sizeof p);
    emvup_xform(c, 1, 16, p);                                          /* 0x052..0x055 */
    memcpy(p, r->vf[1], sizeof p);
    emvup_xform(c, 3, 24, p);                                          /* 0x059..0x05C */
    emvup_xform(c, 1, 20, p);                                          /* 0x05D..0x060 */
    const uint32_t qnew = emvup_div(c, r->vf[0][3], r->vf[3][3]);      /* 0x060 */
    emvup_clip(c, r->vf[1]);                                           /* 0x064 */
    r->acc[3] = emvup_mul(c, r->vf[0][3], r->vf[28][2]);              /* 0x066 */
    r->q = qnew;
    for (unsigned k = 0; k < 3; ++k) r->vf[3][k] = emvup_mul(c, r->vf[3][k], r->q);   /* 0x067 */
    r->i = 0x3B800000u;                                                /* 1/256 */
    for (unsigned k = 0; k < 3; ++k) r->vf[2][k] = emvup_mul(c, r->vf[2][k], r->q);   /* 0x068 */
    const uint16_t hit = (r->cf & 0x03FFFFu) != 0u;                    /* 0x069 */
    for (unsigned k = 0; k < 4; ++k) r->vf[1][k] = emvup_mul(c, r->vf[15][k], r->i);
    r->vi[1] = hit;
    r->vf[3][3] = emvup_madd(c, r->acc[3], r->vf[28][3], r->vf[3][3]); /* 0x06A */
    r->vi[10] |= r->vi[1];
    r->i = 0x44800000u;                                                /* 1024 */
    r->vf[3][3] = em_vu_min_bits(r->vf[3][3], r->vf[28][0]);          /* 0x06E */
    r->vf[3][2] = emvup_add(c, r->vf[3][2], r->i);                    /* 0x06F */
    r->vf[3][3] = em_vu_max_bits(r->vf[3][3], r->vf[0][0]);           /* 0x072 */
    r->vf[1][3] = emvup_mul(c, r->vf[1][3], r->vf[3][3]);             /* 0x076 */
    r->i = 0x437E0000u;                                                /* 254 */
    r->vf[3][3] = emvup_add(c, r->vf[0][3], r->i);                    /* 0x07A */
    for (unsigned k = 0; k < 4; ++k) r->vf[1][k] = em_vu_ftoi0_bits(r->vf[1][k]);     /* 0x07B */
    if (r->vi[10] != 0)                                                /* 0x07C */
        r->vf[3][3] = emvup_add(c, r->vf[3][3], r->vf[28][1]);         /* 0x07E */
    for (unsigned k = 0; k < 4; ++k) r->vf[3][k] = em_vu_ftoi4_bits(r->vf[3][k]);     /* 0x082 */
    emvup_sq(c, 2, r->vi[9]);                                          /* 0x084 */
    emvup_sq(c, 1, r->vi[9] + 1u);
    emvup_sq(c, 3, r->vi[9] + 2u);
}

/* MSCAL 0 of the lane program. dmem rows 0..8: the clip projection (4),
 * K (4) and the fog row; 9: the GIF tag; 0xA..0xD: the four ST rows;
 * 0xE..0x11: the four corners; 0x20 + 6 k: the 32 slots (a 4-row matrix,
 * the colour, and a TEX0 row whose z word is the countdown). */
static inline int em_vu1_lane_program_mscal(EmVu1PRegs *r, EmVu1PQword *dmem, EmVu1PKick kick,
                                            void *ctx)
{
    if (!r || !dmem || !kick) return EM_VU1P_FAULT_ARGS;
    EmVu1PCtx c = { r, dmem, 0 };
    for (unsigned k = 0; k < 9; ++k) emvup_lq(&c, 20u + k, k);         /* 0x000..0x008 */
    r->vi[11] = 0x20;
    r->vi[13] = 0x20;
    r->vi[14] = 0x7FFF;
    r->vi[14] = (uint16_t)(r->vi[14] + 1u);
    r->vi[6] = 0x320;
    r->vi[7] = 0x390;
    do {
        emvup_lq(&c, 1, r->vi[13] + 5u);                               /* 0x00F */
        r->vi[1] = (uint16_t)(r->vf[1][2] & 0xFFFFu);                  /* 0x013 */
        r->vi[12] = r->vi[6];                                          /* 0x018 (delay slot) */
        if ((int16_t)r->vi[1] > 0) {                                   /* 0x017 */
            emvup_sq(&c, 1, r->vi[12]);                                /* 0x019 */
            emvup_lq(&c, 15, r->vi[13] + 4u);
            for (unsigned k = 0; k < 4; ++k) emvup_lq(&c, 16u + k, r->vi[13] + k);
            r->cf = 0;                                                 /* 0x022 */
            r->vi[10] = 0;
            for (unsigned k = 0; k < 4; ++k) {                         /* 0x025..0x038 */
                r->vi[5] = (uint16_t)(0x0A + k);
                r->vi[8] = (uint16_t)(0x0E + k);
                r->vi[9] = (uint16_t)(r->vi[12] + 1u + 3u * k);
                r->vi[15] = (uint16_t)(0x2A + 5u * k);                 /* the BAL link */
                emvup_lane_vertex(&c);
                if (c.bad) return EM_VU1P_FAULT_OPERAND;
            }
            emvup_lq(&c, 1, 9);                                        /* 0x039 */
            r->vi[12] = (uint16_t)(r->vi[12] - 1u);
            emvup_sq(&c, 1, r->vi[12]);                                /* 0x03D */
            if (kick(ctx, dmem, r->vi[12] & 1023u))                    /* 0x041 */
                return EM_VU1P_FAULT_KICK;
        }
        r->vi[1] = r->vi[6];                                           /* 0x043..0x045 */
        r->vi[6] = r->vi[7];
        r->vi[7] = r->vi[1];
        r->vi[11] = (uint16_t)(r->vi[11] - 1u);
        r->vi[13] = (uint16_t)(r->vi[13] + 6u);
    } while ((int16_t)r->vi[11] > 0);                                  /* 0x048 */
    return EM_VU1P_OK;
}

/* ========================================================== sprite ===== */

/* RNEXT to the lanes of `mask` (x = 8, y = 4) of VF`reg`. */
static inline void emvup_rnext(EmVu1PCtx *c, unsigned reg, unsigned lane)
{
    EmVu1PRegs *r = c->r;
    r->r = ((r->r << 1) ^ (((r->r >> 22) ^ (r->r >> 4)) & 1u)) & 0x7FFFFFu;
    r->vf[reg][lane] = 0x3F800000u | r->r;
}

/* One source particle's draw (micro 0x053..0x08D, or 0x08F..0x0C1 when the
 * descriptor's flags word has bit 0x10): two random lookup angles, the
 * age tests (VI11 = the MAC sign of elapsed | of life - elapsed), the
 * velocity (row 97) and offset (VF24) and the age (row 96 x). */
static inline void emvup_sprite_draw(EmVu1PCtx *c)
{
    EmVu1PRegs *r = c->r;
    r->vi[3] = emvup_ilw_field(c, 88, 2);                              /* 0x053: flags */
    r->vi[4] = 0x10;
    emvup_lq(c, 25, 102);
    emvup_rnext(c, 1, 0);                                              /* 0x056 */
    r->vi[4] &= r->vi[3];
    emvup_rnext(c, 1, 1);                                              /* 0x058 */
    const int keep_z = (int16_t)r->vi[4] > 0;                          /* 0x059 -> 0x08F */
    r->vf[25][0] = emvup_add(c, r->vf[25][0], r->vf[25][1]);          /* fraction += step */
    r->vi[4] = 4;
    r->vi[5] = 1;
    r->vi[6] = 2;
    r->vi[7] = 8;
    r->i = 0x427C0000u;                                                /* 63 */
    emvup_lq(c, 3, 89);                                                /* the lower op first */
    r->acc[0] = emvup_sub(c, r->vf[0][0], r->i);                       /* 0x060 */
    emvup_lq(c, 5, 86);
    r->vf[2][0] = emvup_madd(c, r->acc[0], r->vf[1][0], r->i);        /* 0x061 */
    r->vi[11] = 0;
    r->vf[1][0] = emvup_mul(c, r->vf[1][0], r->vf[1][1]);             /* 0x062 */
    emvup_lq(c, 23, 80);                                               /* 0x063 */
    emvup_lq(c, 24, 81);
    r->acc[0] = emvup_add(c, r->vf[0][0], r->vf[3][0]);               /* 0x064: ACC = phase */
    r->vi[2] = 0x80;
    for (unsigned k = 0; k < 4; ++k) r->vf[2][k] = em_vu_ftoi0_bits(r->vf[2][k]);     /* 0x065 */
    r->r ^= r->vf[1][0] & 0x7FFFFFu;                                   /* 0x066 */
    r->vf[3][0] = emvup_msub(c, r->acc[0], r->vf[25][0], r->vf[5][2]); /* elapsed */
    const uint16_t mac_elapsed = (uint16_t)((r->vf[3][0] >> 31) ? 0x80u : 0u);
    emvup_rnext(c, 1, 0);                                              /* 0x067 */
    emvup_rnext(c, 1, 1);                                              /* 0x068 */
    r->vi[1] = (uint16_t)(r->vf[2][0] & 0xFFFFu);                      /* 0x069 */
    r->acc[0] = emvup_sub(c, r->vf[0][0], r->i);
    r->vi[2] &= mac_elapsed;                                           /* 0x06A */
    for (unsigned k = 0; k < 4; ++k) r->vf[4][k] = em_vu_ftoi0_bits(r->vf[3][k]);
    r->vi[11] |= r->vi[2];                                             /* 0x06B */
    r->vf[2][0] = emvup_madd(c, r->acc[0], r->vf[1][0], r->i);
    emvup_sq(c, 25, 102);                                              /* 0x06C */
    r->vf[1][0] = emvup_mul(c, r->vf[1][0], r->vf[1][1]);
    emvup_lq(c, 12, r->vi[1]);                                         /* 0x06D */
    r->vf[5][3] = emvup_sub(c, r->vf[5][3], r->vf[3][0]);             /* life - elapsed */
    const uint16_t mac_life = (uint16_t)((r->vf[5][3] >> 31) ? 0x10u : 0u);
    emvup_lq(c, 13, r->vi[1] + 16u);                                   /* 0x06E */
    for (unsigned k = 0; k < 4; ++k) r->vf[4][k] = em_vu_itof0_bits(r->vf[4][k]);
    r->vi[2] = 0x10;                                                   /* 0x06F */
    for (unsigned k = 0; k < 4; ++k) r->vf[2][k] = em_vu_ftoi0_bits(r->vf[2][k]);
    r->r ^= r->vf[1][0] & 0x7FFFFFu;                                   /* 0x070 */
    r->vi[2] &= mac_life;                                              /* 0x071 */
    if (!keep_z) r->vf[23][2] = emvup_mul(c, r->vf[23][2], r->vf[12][0]);
    r->vi[11] |= r->vi[2];                                             /* 0x072 */
    r->vf[3][0] = emvup_sub(c, r->vf[3][0], r->vf[4][0]);             /* age */
    r->vi[1] = (uint16_t)(r->vf[2][0] & 0xFFFFu);                      /* 0x073 */
    r->vf[23][0] = emvup_mul(c, r->vf[23][0], r->vf[13][0]);
    r->vf[24][2] = emvup_mul(c, r->vf[24][2], r->vf[12][0]);          /* 0x074 */
    r->vf[24][0] = emvup_mul(c, r->vf[24][0], r->vf[13][0]);          /* 0x075 */
    emvup_sq(c, 3, 96);                                                /* 0x076 */
    emvup_lq(c, 14, r->vi[1]);
    emvup_lq(c, 15, r->vi[1] + 16u);
    r->vi[4] &= r->vi[3];                                              /* 0x079..0x07C */
    r->vi[5] &= r->vi[3];
    r->vf[23][0] = emvup_mul(c, r->vf[23][0], r->vf[14][0]);
    if (!keep_z) r->vf[23][2] = emvup_mul(c, r->vf[23][2], r->vf[14][0]);
    r->vi[6] &= r->vi[3];
    r->vf[23][1] = emvup_mul(c, r->vf[23][1], r->vf[15][0]);
    r->vi[7] &= r->vi[3];
    r->vf[24][1] = emvup_mul(c, r->vf[24][1], r->vf[15][0]);          /* 0x07D */
    if (!((int16_t)r->vi[7] > 0))                                      /* 0x07E */
        for (unsigned k = 0; k < 3; ++k) r->vf[23][k] = emvup_mul(c, r->vf[23][k], r->vf[25][0]);
    if (!((int16_t)r->vi[5] <= 0))                                     /* 0x081 */
        r->vf[23][1] = em_vu_abs_bits(r->vf[23][1]);
    if (!((int16_t)r->vi[6] <= 0))                                     /* 0x084 */
        r->vf[23][2] = em_vu_abs_bits(r->vf[23][2]);
    if (!((int16_t)r->vi[4] > 0))                                      /* 0x087 */
        for (unsigned k = 0; k < 4; ++k) r->vf[24][k] = emvup_sub(c, r->vf[24][k], r->vf[24][k]);
    emvup_sq(c, 23, 97);                                               /* 0x08B */
}

/* One particle's record at VI10 (micro 0x0C3..0x0ED): +0 the position,
 * +2 the colour, +3 the size (+1 is not written). */
static inline void emvup_sprite_particle(EmVu1PCtx *c)
{
    EmVu1PRegs *r = c->r;
    emvup_lq(c, 2, 96);
    emvup_lq(c, 4, 87);
    emvup_lq(c, 9, 86);
    emvup_lq(c, 10, 82);
    emvup_lq(c, 11, 83);
    emvup_lq(c, 1, 97);                                                /* 0x0C8 */
    r->vf[5][0] = emvup_add(c, r->vf[2][0], r->vf[4][2]);
    emvup_lq(c, 3, 88);                                                /* 0x0C9 */
    r->vf[6][0] = emvup_add(c, r->vf[2][0], r->vf[4][3]);
    emvup_lq(c, 12, 84);                                               /* 0x0CA */
    r->vf[14][0] = emvup_sub(c, r->vf[2][0], r->vf[9][0]);
    emvup_lq(c, 13, 85);                                               /* 0x0CB */
    r->vf[15][1] = emvup_sub(c, r->vf[9][1], r->vf[9][0]);
    emvup_lq(c, 18, 89);                                               /* 0x0CC */
    for (unsigned k = 0; k < 3; ++k) r->vf[7][k] = emvup_mul(c, r->vf[1][k], r->vf[5][0]);
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = emvup_sub(c, r->vf[11][k], r->vf[10][k]);   /* 0x0CD */
    const uint32_t q_blend = emvup_div(c, r->vf[14][0], r->vf[15][1]); /* 0x0CE */
    for (unsigned k = 0; k < 4; ++k) r->vf[17][k] = emvup_sub(c, r->vf[13][k], r->vf[12][k]);
    r->vf[5][0] = emvup_mul(c, r->vf[5][0], r->vf[5][0]);             /* 0x0CF */
    r->vf[6][0] = emvup_mul(c, r->vf[6][0], r->vf[6][0]);             /* 0x0D0 */
    for (unsigned k = 0; k < 3; ++k) r->vf[7][k] = emvup_add(c, r->vf[7][k], r->vf[24][k]);    /* 0x0D1 */
    r->vf[5][0] = emvup_mul(c, r->vf[5][0], r->vf[3][1]);             /* 0x0D3 */
    r->vf[6][0] = emvup_mul(c, r->vf[6][0], r->vf[3][1]);             /* 0x0D4 */
    r->q = q_blend;
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = emvup_mul(c, r->vf[16][k], r->q);   /* 0x0D5 */
    for (unsigned k = 0; k < 4; ++k) r->vf[17][k] = emvup_mul(c, r->vf[17][k], r->q);   /* 0x0D6 */
    const uint32_t q_fade = emvup_div(c, r->vf[2][0], r->vf[18][2]);
    uint32_t p[4];
    memcpy(p, r->vf[7], sizeof p);
    for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_mul(c, r->vf[28][k], p[0]);      /* 0x0D7 */
    for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_madd(c, r->acc[k], r->vf[29][k], p[1]);
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = emvup_add(c, r->vf[16][k], r->vf[10][k]);  /* 0x0D9 */
    for (unsigned k = 0; k < 4; ++k) r->vf[17][k] = emvup_add(c, r->vf[17][k], r->vf[12][k]);  /* 0x0DA */
    for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_madd(c, r->acc[k], r->vf[30][k], p[2]);
    for (unsigned k = 0; k < 4; ++k) r->vf[7][k] = emvup_madd(c, r->acc[k], r->vf[31][k], r->vf[0][3]);
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = em_vu_max_bits(r->vf[16][k], r->vf[0][0]);  /* 0x0DD */
    for (unsigned k = 0; k < 4; ++k) r->vf[17][k] = em_vu_max_bits(r->vf[17][k], r->vf[0][0]);  /* 0x0DE */
    r->q = q_fade;
    r->vf[18][3] = emvup_mul(c, r->vf[0][3], r->q);                   /* 0x0DF */
    r->vf[7][1] = emvup_add(c, r->vf[7][1], r->vf[5][0]);             /* 0x0E0: gravity */
    r->i = 0x437F0000u;                                                /* 255 */
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = em_vu_min_bits(r->vf[16][k], r->i);   /* 0x0E1 */
    for (unsigned k = 0; k < 4; ++k) r->vf[17][k] = em_vu_min_bits(r->vf[17][k], r->i);   /* 0x0E2 */
    r->vf[18][3] = em_vu_min_bits(r->vf[18][3], r->vf[0][3]);         /* 0x0E3 */
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = emvup_mul(c, r->vf[16][k], r->vf[18][3]);  /* 0x0E7 */
    emvup_sq(c, 17, r->vi[10] + 3u);                                   /* 0x0E8 */
    emvup_sq(c, 7, r->vi[10]);
    emvup_sq(c, 16, r->vi[10] + 2u);
}

/* One batch of up to 28 source particles (micro 0x01D..0x051): returns 1
 * when the count is exhausted (row 99 x = 0x7FFF), else 0. */
static inline int emvup_sprite_batch(EmVu1PCtx *c)
{
    EmVu1PRegs *r = c->r;
    r->vi[12] = emvup_ilw(c, 98);                                      /* 0x01D: remaining */
    emvup_lq(c, 2, 87);
    for (unsigned k = 0; k < 4; ++k) emvup_lq(c, 28u + k, 90u + k);   /* the tile matrix */
    emvup_sq(c, 2, 100);                                               /* 0x023 */
    r->vi[10] = 0x88;
    r->vi[8] = 0;
    if (r->vi[12] == 0) {                                              /* 0x026 -> 0x030 */
        r->vi[1] = 0x7FFF;
        emvup_isw(c, r->vi[1], 99);
        return 1;
    }
    r->vi[1] = (uint16_t)(r->vi[12] - 0x1C);                           /* 0x028 */
    if ((int16_t)r->vi[1] > 0) {                                       /* 0x02A -> 0x035 */
        r->vi[12] = (uint16_t)(r->vi[12] - 0x1C);
        emvup_isw(c, r->vi[12], 98);
        r->vi[12] = 0x1C;
    } else {
        emvup_isw(c, 0, 98);                                           /* 0x02C */
    }
    do {                                                               /* 0x03B */
        r->vi[15] = 0x3D;                                              /* the BAL link */
        emvup_sprite_draw(c);
        if (c->bad) return -1;
        if (r->vi[11] == 0) {                                          /* 0x03D */
            r->vi[15] = 0x41;
            emvup_sprite_particle(c);                                  /* 0x03F */
            if (c->bad) return -1;
            r->vi[10] = (uint16_t)(r->vi[10] + 4u);
            r->vi[8] = (uint16_t)(r->vi[8] + 1u);
        }
        r->vi[12] = (uint16_t)(r->vi[12] - 1u);                        /* 0x043 */
        emvup_isw(c, r->vi[8], 99);
        r->vi[1] = emvup_ilw(c, 101);                                  /* 0x046 (delay slot) */
    } while ((int16_t)r->vi[12] > 0);                                  /* 0x045 */
    r->vi[2] = 0x100;
    r->vi[3] = 0x280;
    emvup_isw(c, r->vi[1] == r->vi[2] ? r->vi[3] : r->vi[2], 101);
    return 0;
}

/* MSCAL 0 of the sprite program. dmem rows 0..79: the lookup (read at an
 * angle index and index + 16); 80..88: the descriptor (88: count, gravity,
 * flags, kind as words); 89: phase, colour scale, fade interval, seed;
 * 90..93: the tile matrix; 110..123: P, K, the clip projection, the fog row
 * and the depth bias; 124: the GIF tag. The program keeps its batch state
 * in rows 96..102 and writes the sprites of each batch to 0x100 or 0x280
 * (six rows per visible particle after a tag row), then kicks the tag. */
static inline int em_vu1_sprite_program_mscal(EmVu1PRegs *r, EmVu1PQword *dmem, EmVu1PKick kick,
                                              void *ctx)
{
    if (!r || !dmem || !kick) return EM_VU1P_FAULT_ARGS;
    EmVu1PCtx cc = { r, dmem, 0 }, *c = &cc;
    emvup_lq(c, 4, 88);                                                /* 0x000..0x003 */
    emvup_lq(c, 1, 89);
    emvup_lq(c, 2, 82);
    emvup_lq(c, 3, 83);
    for (unsigned k = 0; k < 4; ++k) r->vf[4][k] = em_vu_itof0_bits(r->vf[4][k]);     /* 0x004 */
    for (unsigned k = 0; k < 4; ++k) r->vf[2][k] = emvup_mul(c, r->vf[2][k], r->vf[1][1]);  /* 0x006 */
    for (unsigned k = 0; k < 4; ++k) r->vf[3][k] = emvup_mul(c, r->vf[3][k], r->vf[1][1]);  /* 0x007 */
    const uint32_t q_step = emvup_div(c, r->vf[0][3], r->vf[4][0]);   /* 0x008 */
    emvup_sq(c, 2, 82);                                                /* 0x00A */
    emvup_sq(c, 3, 83);                                                /* 0x00B */
    r->vf[4][0] = emvup_add(c, r->vf[0][0], r->vf[0][0]);             /* 0x00E */
    r->q = q_step;
    r->vf[4][1] = emvup_add(c, r->vf[0][1], r->q);                    /* 0x00F */
    emvup_sq(c, 4, 102);                                               /* 0x013 (delay slot) */
    if (c->bad) return EM_VU1P_FAULT_OPERAND;
    /* 0x0EF -> 0x014: the random unit and the batch rows. */
    r->vi[9] = 0xF1;                                                   /* the BAL link */
    emvup_lq(c, 1, 89);
    emvup_lq(c, 2, 88);
    r->vi[1] = 0x100;
    emvup_isw(c, r->vi[1], 101);
    r->r = r->vf[1][3] & 0x7FFFFFu;                                    /* 0x018: RINIT */
    emvup_sq(c, 2, 98);
    uint32_t batches = 0;
    for (;;) {
        /* 0x0F1 -> 0x01D */
        r->vi[9] = 0xF3;
        const int done = emvup_sprite_batch(c);
        if (done < 0 || c->bad) return EM_VU1P_FAULT_OPERAND;
        if (++batches > 0x10000u) return EM_VU1P_FAULT_RUNAWAY;
        r->vi[1] = emvup_ilw(c, 99);                                   /* 0x0F3 */
        r->vi[2] = 0x7FFF;
        r->i = EM_EE_ONE;
        for (unsigned k = 0; k < 3; ++k) r->vf[11][k] = emvup_add(c, r->vf[0][k], r->i);   /* 0x0F6 */
        emvup_lq(c, 16, 110);
        emvup_lq(c, 17, 111);                                          /* 0x0F8 (delay slot) */
        if (r->vi[1] == r->vi[2]) break;                               /* 0x0F7 -> 0x14B */
        emvup_lq(c, 18, 112);                                          /* 0x0FA (delay slot) */
        if (r->vi[1] == 0) continue;                                   /* 0x0F9 -> 0x0F1 */
        emvup_lq(c, 19, 113);
        emvup_lq(c, 20, 114);
        emvup_lq(c, 21, 115);
        emvup_lq(c, 22, 116);                                          /* 0x0FE: the lower op first */
        r->vf[18][0] = emvup_sub(c, r->vf[18][0], r->vf[18][0]);
        r->vf[18][1] = emvup_sub(c, r->vf[18][1], r->vf[18][1]);
        for (unsigned k = 0; k < 6; ++k) emvup_lq(c, 23u + k, 117u + k);   /* 0x0FF..0x104 */
        emvup_lq(c, 9, 123);
        r->vi[11] = emvup_ilw(c, 99);                                  /* 0x106 */
        r->vi[12] = emvup_ilw(c, 101);
        r->vi[13] = 0x88;
        r->vi[14] = 0x7FFF;
        r->vi[14] = (uint16_t)(r->vi[14] + 1u);
        r->vi[15] = r->vi[11];
        emvup_lq(c, 1, r->vi[13]);                                     /* 0x10C..0x10F */
        emvup_lq(c, 10, 100);
        emvup_lq(c, 13, r->vi[13] + 3u);
        emvup_lq(c, 14, r->vi[13] + 2u);
        for (;;) {                                                     /* 0x110 */
            uint32_t p[4];
            emvup_sq(c, 0, r->vi[12] + 2u);
            emvup_sq(c, 11, r->vi[12] + 4u);
            emvup_sq(c, 10, r->vi[12]);
            memcpy(p, r->vf[1], sizeof p);
            emvup_xform(c, 15, 24, p);                                 /* 0x110..0x113: K */
            emvup_lq(c, 6, 124);
            r->vi[6] = emvup_ilw(c, 101);                              /* 0x114 */
            for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_mul(c, r->vf[16][k], r->vf[13][0]);
            for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_madd(c, r->acc[k], r->vf[17][k], r->vf[13][1]);
            for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_madd(c, r->acc[k], r->vf[18][k], r->vf[15][3]);
            for (unsigned k = 0; k < 4; ++k) r->vf[2][k] = emvup_madd(c, r->acc[k], r->vf[19][k], r->vf[0][3]);
            const uint32_t q_w = emvup_div(c, r->vf[0][3], r->vf[15][3]);   /* 0x117 */
            memcpy(p, r->vf[1], sizeof p);
            for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_mul(c, r->vf[20][k], p[0]);   /* 0x118 */
            r->vi[1] = (uint16_t)(r->vi[15] | r->vi[14]);              /* 0x119 */
            for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_madd(c, r->acc[k], r->vf[21][k], p[1]);
            r->vf[6][0] = (uint32_t)(int32_t)(int16_t)r->vi[1];        /* 0x11A: MFIR */
            for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_madd(c, r->acc[k], r->vf[22][k], p[2]);
            r->vi[6] = (uint16_t)(r->vi[6] - 1u);                      /* 0x11B */
            for (unsigned k = 0; k < 4; ++k) r->vf[1][k] = emvup_madd(c, r->acc[k], r->vf[23][k], r->vf[0][3]);
            r->acc[3] = emvup_mul(c, r->vf[0][3], r->vf[28][2]);       /* 0x11C */
            r->vf[15][3] = emvup_madd(c, r->acc[3], r->vf[28][3], r->vf[15][3]);   /* 0x11D */
            r->q = q_w;
            for (unsigned k = 0; k < 3; ++k) r->vf[15][k] = emvup_mul(c, r->vf[15][k], r->q);   /* 0x11E */
            const uint32_t q_e = emvup_div(c, r->vf[0][3], r->vf[2][3]);
            emvup_sq(c, 6, r->vi[6]);                                  /* 0x11F */
            emvup_clip(c, r->vf[1]);
            r->vf[15][3] = em_vu_min_bits(r->vf[15][3], r->vf[28][0]); /* 0x121 */
            r->vf[15][2] = emvup_add(c, r->vf[15][2], r->vf[9][2]);   /* 0x122 */
            r->i = 0x3B800000u;                                        /* 1/256 */
            r->vi[1] = (r->cf & 0x3Fu) != 0u;                          /* 0x123 */
            for (unsigned k = 0; k < 4; ++k) r->vf[14][k] = emvup_mul(c, r->vf[14][k], r->i);
            for (unsigned k = 2; k < 4; ++k) r->vf[2][k] = emvup_sub(c, r->vf[2][k], r->vf[2][k]);   /* 0x124 */
            r->q = q_e;
            for (unsigned k = 0; k < 2; ++k) r->vf[2][k] = emvup_mul(c, r->vf[2][k], r->q);   /* 0x125 */
            r->vf[15][3] = em_vu_max_bits(r->vf[15][3], r->vf[0][0]);  /* 0x126 */
            if (c->bad) return EM_VU1P_FAULT_OPERAND;
            if (r->vi[1] != 0) {                                       /* 0x126 -> 0x138 not taken */
                r->vi[15] = (uint16_t)(r->vi[15] - 1u);                /* 0x128 */
                r->vi[1] = (uint16_t)(r->vi[15] | r->vi[14]);
                r->vf[6][0] = (uint32_t)(int32_t)(int16_t)r->vi[1];    /* 0x12A */
                r->vi[11] = (uint16_t)(r->vi[11] - 1u);
                r->vi[13] = (uint16_t)(r->vi[13] + 4u);
                emvup_sq(c, 6, r->vi[6]);                              /* 0x12E */
                emvup_lq(c, 1, r->vi[13]);
                emvup_lq(c, 10, 100);
                emvup_lq(c, 13, r->vi[13] + 3u);
                emvup_lq(c, 14, r->vi[13] + 2u);                       /* 0x133 (delay slot) */
                if ((int16_t)r->vi[11] > 0) continue;                  /* 0x132 */
            } else {
                for (unsigned k = 0; k < 4; ++k)                       /* 0x13A */
                    r->vf[5][k] = emvup_mul(c, r->vf[14][k], r->vf[15][3]);
                for (unsigned k = 0; k < 4; ++k)
                    r->vf[3][k] = emvup_sub(c, r->vf[15][k], r->vf[2][k]);
                for (unsigned k = 0; k < 4; ++k)
                    r->vf[4][k] = emvup_add(c, r->vf[15][k], r->vf[2][k]);
                r->vi[11] = (uint16_t)(r->vi[11] - 1u);
                r->vi[13] = (uint16_t)(r->vi[13] + 4u);
                for (unsigned k = 0; k < 4; ++k) r->vf[5][k] = em_vu_ftoi0_bits(r->vf[5][k]);   /* 0x13E */
                emvup_lq(c, 1, r->vi[13]);
                for (unsigned k = 0; k < 4; ++k) r->vf[3][k] = em_vu_ftoi4_bits(r->vf[3][k]);   /* 0x13F */
                emvup_lq(c, 10, 100);
                for (unsigned k = 0; k < 4; ++k) r->vf[4][k] = em_vu_ftoi4_bits(r->vf[4][k]);   /* 0x140 */
                emvup_lq(c, 13, r->vi[13] + 3u);
                emvup_lq(c, 14, r->vi[13] + 2u);
                emvup_sq(c, 5, r->vi[12] + 1u);                        /* 0x142..0x144 */
                emvup_sq(c, 3, r->vi[12] + 5u);
                emvup_sq(c, 4, r->vi[12] + 3u);
                const int more = (int16_t)r->vi[11] > 0;               /* 0x145 */
                r->vi[12] = (uint16_t)(r->vi[12] + 6u);                /* 0x146 (delay slot) */
                if (c->bad) return EM_VU1P_FAULT_OPERAND;
                if (more) continue;
            }
            if (kick(ctx, dmem, r->vi[6] & 1023u))                     /* 0x134 / 0x147 */
                return EM_VU1P_FAULT_KICK;
            break;                                                     /* -> 0x0F1 */
        }
    }
    return c->bad ? EM_VU1P_FAULT_OPERAND : EM_VU1P_OK;
}

#endif
