/* em_vu1_page_programs.h — the VU1 programs the chain page D_007635C0
 * CALLs, translated from their VU1 microcode into CPU-exact C
 * (docs/CHAIN_PAGE.md section 3): the lane, sprite and snow programs below,
 * the level background's grid program (packet 0x0023C990, the channel-3
 * list's; docs/BACKGROUND.md) at the end,
 * and the streak program (table 0x230800) and the kind-2 program (table
 * 0x232540) at the end of this header.
 *
 *   lane program    DMA packet D_00233290 (001F0720's CALL): one MPG of 138
 *                   instructions (ELF 0x002332B8) to micro 0, 14 constant
 *                   rows to dmem 0..13, BASE 0x320, OFFSET 0x70.
 *   sprite program  DMA packet 0x00231770 (001CFBE0's table of kinds 1 and
 *                   5): MPGs of 256 (ELF 0x00231798) and 79 (ELF 0x00231FA0)
 *                   instructions to micro 0 and 0x100, a 128-word lookup
 *                   (V1-32, all four lanes) to dmem 0..127 and 17 rows to
 *                   dmem 0x6E..0x7E, BASE 0, OFFSET 0.
 *   snow program    DMA packet D_00233800 (001CFFE0's table of variant 3,
 *                   the weather 001E67C0's tiles): MPGs of 256 (ELF
 *                   0x00233828) and 81 (ELF 0x00234030) instructions to
 *                   micro 0 and 0x100, the same lookup and 17 constant rows
 *                   (both equal to the sprite program's), BASE 0, OFFSET 0.
 *                   Its instructions are the sprite program's except five
 *                   in the emission (the near weight: micro 0x118, 0x11D,
 *                   0x120, 0x127, 0x13E, with the FTOI0 and its store moved
 *                   to 0x142 / 0x146) and the branch offsets that follow
 *                   from its two extra instructions (0x0F7, 0x147, 0x14B):
 *                   one translation with a `snow` switch
 *                   (em_vu1_snow_program_mscal).
 *
 * Micro addresses below are instruction indices of the uploaded program.
 * All three programs are entered by MSCAL 0 only (every captured page).
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

#include <math.h>
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
    uint32_t p;           /* the EFU's P (the streak program)          */
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
static inline void emvup_streak_particle(EmVu1PCtx *c);

static inline int emvup_sprite_batch(EmVu1PCtx *c, int streak)
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
            if (streak) emvup_streak_particle(c);                      /* 0x03F */
            else emvup_sprite_particle(c);
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
static inline int emvup_particle_program(EmVu1PRegs *r, EmVu1PQword *dmem, EmVu1PKick kick, void *ctx,
                                         int snow)
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
        const int done = emvup_sprite_batch(c, 0);
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
            if (snow) memcpy(r->vf[8], r->vf[15], sizeof r->vf[8]);    /* 0x118 lower: VF08 = K . p */
            r->vi[1] = (uint16_t)(r->vi[15] | r->vi[14]);              /* 0x119 */
            for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_madd(c, r->acc[k], r->vf[21][k], p[1]);
            r->vf[6][0] = (uint32_t)(int32_t)(int16_t)r->vi[1];        /* 0x11A: MFIR */
            for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_madd(c, r->acc[k], r->vf[22][k], p[2]);
            r->vi[6] = (uint16_t)(r->vi[6] - 1u);                      /* 0x11B */
            for (unsigned k = 0; k < 4; ++k) r->vf[1][k] = emvup_madd(c, r->acc[k], r->vf[23][k], r->vf[0][3]);
            r->acc[3] = emvup_mul(c, r->vf[0][3], r->vf[28][2]);       /* 0x11C */
            r->vf[15][3] = emvup_madd(c, r->acc[3], r->vf[28][3], r->vf[15][3]);   /* 0x11D */
            if (snow) r->i = 0x3CA3D70Au;                              /* 0x11D lower: I = 0.02 */
            r->q = q_w;
            for (unsigned k = 0; k < 3; ++k) r->vf[15][k] = emvup_mul(c, r->vf[15][k], r->q);   /* 0x11E */
            const uint32_t q_e = emvup_div(c, r->vf[0][3], r->vf[2][3]);
            emvup_sq(c, 6, r->vi[6]);                                  /* 0x11F */
            emvup_clip(c, r->vf[1]);
            if (snow) r->vf[8][3] = emvup_mul(c, r->vf[8][3], r->i);   /* 0x120: the screen w x 0.02 */
            r->vf[15][3] = em_vu_min_bits(r->vf[15][3], r->vf[28][0]); /* 0x121 */
            r->vf[15][2] = emvup_add(c, r->vf[15][2], r->vf[9][2]);   /* 0x122 */
            r->i = 0x3B800000u;                                        /* 1/256 */
            r->vi[1] = (r->cf & 0x3Fu) != 0u;                          /* 0x123 */
            for (unsigned k = 0; k < 4; ++k) r->vf[14][k] = emvup_mul(c, r->vf[14][k], r->i);
            for (unsigned k = 2; k < 4; ++k) r->vf[2][k] = emvup_sub(c, r->vf[2][k], r->vf[2][k]);   /* 0x124 */
            r->q = q_e;
            for (unsigned k = 0; k < 2; ++k) r->vf[2][k] = emvup_mul(c, r->vf[2][k], r->q);   /* 0x125 */
            r->vf[15][3] = em_vu_max_bits(r->vf[15][3], r->vf[0][0]);  /* 0x126 */
            if (snow)                                                  /* 0x127 (delay slot) */
                r->vf[8][3] = em_vu_min_bits(r->vf[8][3], r->vf[0][3]);   /* the near weight <= 1 */
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
                if (snow)                                              /* 0x13E: x the near weight */
                    for (unsigned k = 0; k < 4; ++k) r->vf[5][k] = emvup_mul(c, r->vf[5][k], r->vf[8][3]);
                /* 0x13E (sprite) / 0x142 (snow): FTOI0; the stores do not
                 * read VF01 / VF10 / VF13 / VF14, so the order is kept. */
                for (unsigned k = 0; k < 4; ++k) r->vf[5][k] = em_vu_ftoi0_bits(r->vf[5][k]);
                emvup_lq(c, 1, r->vi[13]);
                for (unsigned k = 0; k < 4; ++k) r->vf[3][k] = em_vu_ftoi4_bits(r->vf[3][k]);   /* 0x13F */
                emvup_lq(c, 10, 100);
                for (unsigned k = 0; k < 4; ++k) r->vf[4][k] = em_vu_ftoi4_bits(r->vf[4][k]);   /* 0x140 */
                emvup_lq(c, 13, r->vi[13] + 3u);
                emvup_lq(c, 14, r->vi[13] + 2u);
                emvup_sq(c, 5, r->vi[12] + 1u);                        /* 0x142 (snow 0x146) */
                emvup_sq(c, 3, r->vi[12] + 5u);                        /* 0x143 */
                emvup_sq(c, 4, r->vi[12] + 3u);                        /* 0x144 */
                const int more = (int16_t)r->vi[11] > 0;               /* 0x145 (snow 0x147) */
                r->vi[12] = (uint16_t)(r->vi[12] + 6u);                /* its delay slot */
                if (c->bad) return EM_VU1P_FAULT_OPERAND;
                if (more) continue;
            }
            if (kick(ctx, dmem, r->vi[6] & 1023u))                     /* 0x134 / 0x147 (snow 0x149) */
                return EM_VU1P_FAULT_KICK;
            break;                                                     /* -> 0x0F1 */
        }
    }
    return c->bad ? EM_VU1P_FAULT_OPERAND : EM_VU1P_OK;
}

static inline int em_vu1_sprite_program_mscal(EmVu1PRegs *r, EmVu1PQword *dmem, EmVu1PKick kick,
                                              void *ctx)
{
    return emvup_particle_program(r, dmem, kick, ctx, 0);
}

/* MSCAL 0 of the snow program (D_00233800): the sprite program's dmem
 * layout and flow; in the emission a visible particle's colour (after the
 * fog weight) is multiplied by the near weight min(w * 0.02, 1), w the
 * particle's K-projected w (VF08, micro 0x118..0x13E), before its FTOI0.
 * VF08 (all four lanes, w scaled) and I = 0.02 between micro 0x11D and 0x122
 * are the only other register effects. */
static inline int em_vu1_snow_program_mscal(EmVu1PRegs *r, EmVu1PQword *dmem, EmVu1PKick kick,
                                            void *ctx)
{
    return emvup_particle_program(r, dmem, kick, ctx, 1);
}

/* ========================================================== streak ===== */

/* The EFU of the streak program (CHAIN_PAGE.md section 3). No capture holds
 * an EFU result, so this is a model, shared with the background's grid
 * program (em_vu1_grid_program_mscal below, docs/BACKGROUND.md): ERLENG = 1 / sqrt(x*x + y*y + z*z) and ERCPR = 1 / x,
 * truncated to binary32 with denormals flushed; ERCPR's quotient is VDIV's.
 * A zero operand (ERCPR) or a zero length (ERLENG) is not established: the
 * MSCAL faults (EM_VU1P_FAULT_OPERAND), as the reference model raises. */
static inline uint32_t emvup_ercpr(EmVu1PCtx *c, uint32_t x)
{
    x = em_eei_daz(emvup_chk(c, x));
    if (em_eei_is_zero(x)) { c->bad = 1u; return 0; }
    if (c->bad) return 0;
    return em_eei_quotient(EM_EE_ONE, x, 0);
}
static inline double emvup_efu_lane(uint32_t w)
{
    float f;
    w = em_eei_daz(w);
    memcpy(&f, &w, sizeof f);
    return (double)f;
}
static inline uint32_t emvup_erleng(EmVu1PCtx *c, const uint32_t v[4])
{
    const double x = emvup_efu_lane(emvup_chk(c, v[0]));
    const double y = emvup_efu_lane(emvup_chk(c, v[1]));
    const double z = emvup_efu_lane(emvup_chk(c, v[2]));
    if (c->bad) return 0;
    /* One product or sum per statement: no fused multiply-add. */
    const double xx = x * x, yy = y * y, zz = z * z;
    double s = xx + yy;
    s = s + zz;
    if (s == 0.0) { c->bad = 1u; return 0; }
    const double root = sqrt(s);
    const double inv = 1.0 / root;
    float f = (float)inv;
    uint32_t w;
    memcpy(&w, &f, sizeof w);
    if ((double)f > inv) w -= 1u;                                       /* truncate */
    return (w & 0x7F800000u) == 0u ? 0u : w;                             /* flush */
}

/* One particle's record at VI10 (micro 0x0C3..0x0F3): the sprite program's
 * (+0 the position, +2 the colour, +3 the size) and +1, the trailing point:
 * the same position for the age row 87 w (instead of z). Two DIVs: the
 * blend 0x0CF (read at 0x0D6 / 0x0D7) and the fade 0x0DB (read at 0x0E4). */
static inline void emvup_streak_particle(EmVu1PCtx *c)
{
    EmVu1PRegs *r = c->r;
    uint32_t p[4];
    emvup_lq(c, 2, 96);                                                /* 0x0C3 */
    emvup_lq(c, 4, 87);
    emvup_lq(c, 9, 86);
    emvup_lq(c, 10, 82);
    emvup_lq(c, 11, 83);
    r->vf[5][0] = emvup_add(c, r->vf[2][0], r->vf[4][2]);             /* 0x0C8 */
    emvup_lq(c, 1, 97);
    r->vf[6][0] = emvup_add(c, r->vf[2][0], r->vf[4][3]);             /* 0x0C9 */
    emvup_lq(c, 3, 88);
    r->vf[14][0] = emvup_sub(c, r->vf[2][0], r->vf[9][0]);            /* 0x0CA */
    emvup_lq(c, 12, 84);
    r->vf[15][1] = emvup_sub(c, r->vf[9][1], r->vf[9][0]);            /* 0x0CB */
    emvup_lq(c, 13, 85);
    for (unsigned k = 0; k < 3; ++k) r->vf[7][k] = emvup_mul(c, r->vf[1][k], r->vf[5][0]);    /* 0x0CC */
    emvup_lq(c, 18, 89);
    for (unsigned k = 0; k < 3; ++k) r->vf[8][k] = emvup_mul(c, r->vf[1][k], r->vf[6][0]);    /* 0x0CD */
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = emvup_sub(c, r->vf[11][k], r->vf[10][k]); /* 0x0CE */
    const uint32_t q_blend = emvup_div(c, r->vf[14][0], r->vf[15][1]); /* 0x0CF */
    for (unsigned k = 0; k < 4; ++k) r->vf[17][k] = emvup_sub(c, r->vf[13][k], r->vf[12][k]);
    r->vf[5][0] = emvup_mul(c, r->vf[5][0], r->vf[5][0]);             /* 0x0D0 */
    r->vf[6][0] = emvup_mul(c, r->vf[6][0], r->vf[6][0]);             /* 0x0D1 */
    for (unsigned k = 0; k < 3; ++k) r->vf[7][k] = emvup_add(c, r->vf[7][k], r->vf[24][k]);   /* 0x0D2 */
    for (unsigned k = 0; k < 3; ++k) r->vf[8][k] = emvup_add(c, r->vf[8][k], r->vf[24][k]);   /* 0x0D3 */
    r->vf[5][0] = emvup_mul(c, r->vf[5][0], r->vf[3][1]);             /* 0x0D4 */
    r->vf[6][0] = emvup_mul(c, r->vf[6][0], r->vf[3][1]);             /* 0x0D5 */
    r->q = q_blend;
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = emvup_mul(c, r->vf[16][k], r->q);   /* 0x0D6 */
    for (unsigned k = 0; k < 4; ++k) r->vf[17][k] = emvup_mul(c, r->vf[17][k], r->q);   /* 0x0D7 */
    memcpy(p, r->vf[7], sizeof p);
    for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_mul(c, r->vf[28][k], p[0]);      /* 0x0D8 */
    for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_madd(c, r->acc[k], r->vf[29][k], p[1]);
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = emvup_add(c, r->vf[16][k], r->vf[10][k]);  /* 0x0DA */
    for (unsigned k = 0; k < 4; ++k) r->vf[17][k] = emvup_add(c, r->vf[17][k], r->vf[12][k]);  /* 0x0DB */
    const uint32_t q_fade = emvup_div(c, r->vf[2][0], r->vf[18][2]);
    for (unsigned k = 0; k < 4; ++k) r->acc[k] = emvup_madd(c, r->acc[k], r->vf[30][k], p[2]);   /* 0x0DC */
    for (unsigned k = 0; k < 4; ++k) r->vf[7][k] = emvup_madd(c, r->acc[k], r->vf[31][k], r->vf[0][3]);
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = em_vu_max_bits(r->vf[16][k], r->vf[0][0]);  /* 0x0DE */
    for (unsigned k = 0; k < 4; ++k) r->vf[17][k] = em_vu_max_bits(r->vf[17][k], r->vf[0][0]);  /* 0x0DF */
    memcpy(p, r->vf[8], sizeof p);
    emvup_xform(c, 8, 28, p);                                          /* 0x0E0..0x0E3 */
    r->q = q_fade;
    r->vf[18][3] = emvup_mul(c, r->vf[0][3], r->q);                   /* 0x0E4 */
    r->i = 0x437F0000u;                                                /* 255 */
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = em_vu_min_bits(r->vf[16][k], r->i);   /* 0x0E5 */
    for (unsigned k = 0; k < 4; ++k) r->vf[17][k] = em_vu_min_bits(r->vf[17][k], r->i);   /* 0x0E6 */
    r->vf[7][1] = emvup_add(c, r->vf[7][1], r->vf[5][0]);             /* 0x0E7: gravity */
    r->vf[8][1] = emvup_add(c, r->vf[8][1], r->vf[6][0]);             /* 0x0E8 */
    r->vf[18][3] = em_vu_min_bits(r->vf[18][3], r->vf[0][3]);         /* 0x0E9 */
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = emvup_mul(c, r->vf[16][k], r->vf[18][3]);  /* 0x0ED */
    emvup_sq(c, 17, r->vi[10] + 3u);                                   /* 0x0EE */
    emvup_sq(c, 7, r->vi[10]);
    emvup_sq(c, 8, r->vi[10] + 1u);
    emvup_sq(c, 16, r->vi[10] + 2u);
}

/* One particle's quad (micro 0x119..0x175): the two points through K (P =
 * ERCPR of the head's w, Q = 1 / the tail's w), their 2D difference turned
 * a quarter, scaled by the size and normalised (ERLENG), carried through P
 * at each end's depth, and four vertices TEX0, then (STQ, RGBAQ, XYZF2) x 4,
 * at VI12. The first two take ADC from VF28.y always; the last two when the
 * tail's clip test failed. */
static inline void emvup_streak_quad(EmVu1PCtx *c)
{
    EmVu1PRegs *r = c->r;
    uint32_t p[4];
    for (unsigned k = 0; k < 4; ++k) r->vf[13][k] = emvup_mul(c, r->vf[13][k], r->i);   /* 0x119 */
    memcpy(p, r->vf[14], sizeof p);
    emvup_xform(c, 14, 24, p);                                         /* 0x11A..0x11D: head */
    emvup_sq(c, 2, r->vi[12]);                                         /* 0x11C: TEX0 */
    memcpy(p, r->vf[1], sizeof p);
    emvup_xform(c, 15, 24, p);                                         /* 0x11E..0x121: tail */
    const uint32_t p_head = emvup_ercpr(c, r->vf[14][3]);             /* 0x121 ERCPR */
    emvup_xform(c, 1, 20, p);                                          /* 0x122..0x125: clip */
    const uint32_t q_tail = emvup_div(c, r->vf[0][3], r->vf[15][3]);  /* 0x125 */
    r->vf[10][2] = emvup_add(c, r->vf[0][2], r->vf[14][3]);           /* 0x126 */
    r->vf[11][2] = emvup_add(c, r->vf[0][2], r->vf[15][3]);           /* 0x127 */
    r->acc[3] = emvup_mul(c, r->vf[0][3], r->vf[28][2]);              /* 0x128 */
    emvup_clip(c, r->vf[1]);                                           /* 0x129, then its LQ */
    for (unsigned k = 0; k < 3; ++k) r->vf[1][k] = c->m[127].w[k];
    r->vf[14][3] = emvup_madd(c, r->acc[3], r->vf[28][3], r->vf[14][3]);   /* 0x12A */
    for (unsigned k = 0; k < 3; ++k) r->vf[2][k] = c->m[128].w[k];
    for (unsigned k = 0; k < 3; ++k) r->vf[3][k] = c->m[125].w[k];    /* 0x12B */
    r->q = q_tail;
    for (unsigned k = 0; k < 3; ++k) r->vf[15][k] = emvup_mul(c, r->vf[15][k], r->q);   /* 0x12C */
    for (unsigned k = 0; k < 3; ++k) r->vf[4][k] = c->m[126].w[k];
    for (unsigned k = 0; k < 3; ++k) r->vf[1][k] = emvup_mul(c, r->vf[1][k], r->q);     /* 0x12D */
    r->p = p_head;                                                     /* MFP (12 cycles on) */
    r->vf[5][0] = r->p;
    for (unsigned k = 0; k < 3; ++k) r->vf[2][k] = emvup_mul(c, r->vf[2][k], r->q);     /* 0x12E */
    r->vi[10] = 0;
    r->vf[14][3] = em_vu_min_bits(r->vf[14][3], r->vf[28][0]);        /* 0x12F */
    r->vi[1] = (r->cf & 0x3Fu) != 0u;                                  /* FCAND */
    r->vi[10] |= r->vi[1];                                             /* 0x130 */
    for (unsigned k = 0; k < 3; ++k) r->vf[14][k] = emvup_mul(c, r->vf[14][k], r->vf[5][0]);   /* 0x131 */
    for (unsigned k = 0; k < 3; ++k) c->m[(r->vi[12] + 7u) & 1023u].w[k] = r->vf[1][k];
    for (unsigned k = 0; k < 3; ++k) r->vf[3][k] = emvup_mul(c, r->vf[3][k], r->vf[5][0]);     /* 0x132 */
    for (unsigned k = 0; k < 3; ++k) c->m[(r->vi[12] + 10u) & 1023u].w[k] = r->vf[2][k];
    for (unsigned k = 0; k < 3; ++k) r->vf[4][k] = emvup_mul(c, r->vf[4][k], r->vf[5][0]);     /* 0x133 */
    r->vf[14][3] = em_vu_max_bits(r->vf[14][3], r->vf[0][0]);         /* 0x134 */
    for (unsigned k = 0; k < 2; ++k) r->vf[1][k] = emvup_sub(c, r->vf[15][k], r->vf[14][k]);   /* 0x135 */
    for (unsigned k = 0; k < 3; ++k) c->m[(r->vi[12] + 1u) & 1023u].w[k] = r->vf[3][k];    /* 0x136 */
    for (unsigned k = 0; k < 3; ++k) c->m[(r->vi[12] + 4u) & 1023u].w[k] = r->vf[4][k];    /* 0x137 */
    for (unsigned k = 0; k < 4; ++k) r->vf[13][k] = emvup_mul(c, r->vf[13][k], r->vf[14][3]);  /* 0x138 */
    emvup_lq(c, 2, r->vi[13] + 3u);
    const uint32_t p_len = emvup_erleng(c, r->vf[1]);                  /* 0x139 ERLENG, then */
    r->vf[1][0] = emvup_sub(c, r->vf[0][0], r->vf[1][0]);             /* its pair's upper */
    for (unsigned k = 0; k < 4; ++k) r->vf[13][k] = em_vu_ftoi0_bits(r->vf[13][k]);   /* 0x13C */
    for (unsigned k = 0; k < 2; ++k) r->vf[1][k] = emvup_mul(c, r->vf[1][k], r->vf[2][0]);     /* 0x13D */
    emvup_sq(c, 13, r->vi[12] + 2u);                                   /* 0x140 */
    r->vf[12][0] = emvup_add(c, r->vf[0][0], r->vf[1][1]);            /* 0x141 */
    emvup_sq(c, 13, r->vi[12] + 5u);
    r->vf[12][1] = emvup_add(c, r->vf[0][1], r->vf[1][0]);            /* 0x142 */
    const int unclipped = r->vi[10] == 0;
    r->vf[14][3] = emvup_add(c, r->vf[14][3], r->vf[28][1]);          /* 0x143 (delay slot) */
    emvup_sq(c, 13, r->vi[12] + 8u);
    if (!unclipped)
        r->vf[15][3] = emvup_add(c, r->vf[0][3], r->vf[28][1]);       /* 0x144 */
    emvup_sq(c, 13, r->vi[12] + 11u);                                  /* 0x145 */
    r->p = p_len;                                                      /* 0x146 WAITP, 0x147 MFP */
    r->vf[1][0] = r->p;
    emvup_lq(c, 31, 124);                                              /* 0x148 */
    r->vi[10] = emvup_ilw(c, 101);                                     /* 0x149 */
    for (unsigned k = 0; k < 2; ++k) r->vf[12][k] = emvup_mul(c, r->vf[12][k], r->vf[1][0]);   /* 0x14B */
    r->vf[31][0] = (uint32_t)(int32_t)(int16_t)r->vi[15];             /* 0x14C: MFIR */
    r->vi[10] = (uint16_t)(r->vi[10] - 1u);                            /* 0x14D */
    p[0] = r->vf[12][0]; p[1] = r->vf[12][1]; p[2] = r->vf[11][2];
    emvup_xform(c, 1, 16, p);                                          /* 0x14F..0x152: the tail's */
    emvup_sq(c, 31, r->vi[10]);                                        /* 0x150: the GIF tag */
    p[2] = r->vf[10][2];
    emvup_xform(c, 5, 16, p);                                          /* 0x153..0x156: the head's */
    const uint32_t p_tail = emvup_ercpr(c, r->vf[1][3]);              /* 0x156 ERCPR */
    for (unsigned k = 2; k < 4; ++k) r->vf[1][k] = emvup_sub(c, r->vf[1][k], r->vf[1][k]);    /* 0x159 */
    r->i = 0x3F000000u;                                                /* 0.5 */
    const uint32_t q_head = emvup_div(c, r->vf[0][3], r->vf[5][3]);   /* 0x15A (reads VF05 first) */
    for (unsigned k = 2; k < 4; ++k) r->vf[5][k] = emvup_sub(c, r->vf[5][k], r->vf[5][k]);
    for (unsigned k = 0; k < 2; ++k) r->vf[1][k] = emvup_mul(c, r->vf[1][k], r->i);       /* 0x15B */
    for (unsigned k = 0; k < 2; ++k) r->vf[5][k] = emvup_mul(c, r->vf[5][k], r->i);       /* 0x15C */
    r->vf[14][2] = emvup_add(c, r->vf[14][2], r->vf[9][2]);           /* 0x15D */
    r->vf[15][2] = emvup_add(c, r->vf[15][2], r->vf[9][2]);           /* 0x15E */
    r->q = q_head;
    for (unsigned k = 0; k < 2; ++k) r->vf[5][k] = emvup_mul(c, r->vf[5][k], r->q);       /* 0x161 */
    r->p = p_tail;                                                     /* 0x162 MFP (12 cycles on) */
    r->vf[4][0] = r->p;
    for (unsigned k = 0; k < 4; ++k) r->vf[6][k] = emvup_add(c, r->vf[15][k], r->vf[5][k]);    /* 0x165 */
    for (unsigned k = 0; k < 2; ++k) r->vf[1][k] = emvup_mul(c, r->vf[1][k], r->vf[4][0]);     /* 0x166 */
    for (unsigned k = 0; k < 4; ++k) r->vf[7][k] = emvup_sub(c, r->vf[15][k], r->vf[5][k]);    /* 0x167 */
    for (unsigned k = 0; k < 4; ++k) r->vf[6][k] = em_vu_ftoi4_bits(r->vf[6][k]);             /* 0x169 */
    r->i = 0x3B800000u;                                                /* 1/256 */
    for (unsigned k = 0; k < 4; ++k) r->vf[8][k] = emvup_add(c, r->vf[14][k], r->vf[1][k]);    /* 0x16A */
    r->vi[13] = (uint16_t)(r->vi[13] + 4u);
    for (unsigned k = 0; k < 4; ++k) r->vf[3][k] = emvup_sub(c, r->vf[14][k], r->vf[1][k]);    /* 0x16B */
    r->vi[11] = (uint16_t)(r->vi[11] - 1u);
    for (unsigned k = 0; k < 4; ++k) r->vf[7][k] = em_vu_ftoi4_bits(r->vf[7][k]);             /* 0x16C */
    emvup_lq(c, 13, r->vi[13] + 2u);
    emvup_sq(c, 6, r->vi[12] + 9u);                                    /* 0x16D */
    for (unsigned k = 0; k < 4; ++k) r->vf[8][k] = em_vu_ftoi4_bits(r->vf[8][k]);             /* 0x16E */
    emvup_lq(c, 2, 100);
    for (unsigned k = 0; k < 4; ++k) r->vf[3][k] = em_vu_ftoi4_bits(r->vf[3][k]);             /* 0x16F */
    emvup_lq(c, 1, r->vi[13] + 1u);
    emvup_sq(c, 7, r->vi[12] + 12u);                                   /* 0x170 */
    emvup_lq(c, 14, r->vi[13]);                                        /* 0x171 */
    emvup_sq(c, 8, r->vi[12] + 3u);                                    /* 0x172 */
    emvup_sq(c, 3, r->vi[12] + 6u);                                    /* 0x173 */
}

/* Micro 0x000..0x018 of the streak program and the kind-2 program (the
 * two share every instruction up to 0x0F8): the step rows, the entry's
 * branch to 0x0F5, its BAL to 0x014 (the random unit and the batch rows). */
static inline int emvup_streak_start(EmVu1PCtx *c)
{
    EmVu1PRegs *r = c->r;
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
    /* 0x0F5 -> 0x014: the random unit and the batch rows. */
    r->vi[9] = 0xF7;                                                   /* the BAL link */
    emvup_lq(c, 1, 89);
    emvup_lq(c, 2, 88);
    r->vi[1] = 0x100;
    emvup_isw(c, r->vi[1], 101);
    r->r = r->vf[1][3] & 0x7FFFFFu;                                    /* 0x018: RINIT */
    emvup_sq(c, 2, 98);
    return EM_VU1P_OK;
}

/* MSCAL 0 of the streak program (table 0x230800: 001CFBE0's kinds 0 and 4;
 * the AREA11 user is the impact effect 0x80000060's handler 001EACF0). Its
 * micro 0x000..0x0C2 are the sprite program's instructions (the branch at
 * 0x012 lands on 0x0F5, its BAL links are 0x0F7 / 0x0F9); the particle
 * record (0x0C3..0x0F3) adds the trailing point, and the emission
 * (0x0F9..0x17B) draws each particle of a batch as a quad of 13 rows at
 * row 101's base (a TEX0 row from row 100, the uploaded rows 125..128 as
 * the corners' ST, the colour times the fog weight, the four XYZF2s) after
 * the tag row 124 whose NLOOP is the particle count, every particle drawn
 * (none is rejected), and kicks it. The dmem layout is the sprite program's
 * plus rows 125..128 (the packet's own upload). */
static inline int em_vu1_streak_program_mscal(EmVu1PRegs *r, EmVu1PQword *dmem, EmVu1PKick kick,
                                              void *ctx)
{
    if (!r || !dmem || !kick) return EM_VU1P_FAULT_ARGS;
    EmVu1PCtx cc = { r, dmem, 0 }, *c = &cc;
    const int rc0 = emvup_streak_start(c);
    if (rc0) return rc0;
    uint32_t batches = 0;
    for (;;) {
        /* 0x0F7 -> 0x01D */
        r->vi[9] = 0xF9;
        const int done = emvup_sprite_batch(c, 1);
        if (done < 0 || c->bad) return EM_VU1P_FAULT_OPERAND;
        if (++batches > 0x10000u) return EM_VU1P_FAULT_RUNAWAY;
        r->vi[1] = emvup_ilw(c, 99);                                   /* 0x0F9 */
        r->vi[2] = 0x7FFF;
        emvup_lq(c, 16, 110);                                          /* 0x0FB */
        emvup_lq(c, 17, 111);                                          /* 0x0FC */
        emvup_lq(c, 16, 110);                                          /* 0x0FE (delay slot) */
        if (r->vi[1] == r->vi[2]) break;                               /* 0x0FD -> 0x17A, the end */
        emvup_lq(c, 17, 111);                                          /* 0x100 (delay slot) */
        if (r->vi[1] == 0) continue;                                   /* 0x0FF -> 0x0F7 */
        for (unsigned k = 0; k < 4; ++k) emvup_lq(c, 18u + k, 112u + k);   /* 0x101..0x104 */
        r->vf[18][0] = emvup_sub(c, r->vf[18][0], r->vf[18][0]);      /* 0x105, then its LQ */
        r->vf[18][1] = emvup_sub(c, r->vf[18][1], r->vf[18][1]);
        emvup_lq(c, 22, 116);
        for (unsigned k = 0; k < 6; ++k) emvup_lq(c, 23u + k, 117u + k);   /* 0x106..0x10B */
        emvup_lq(c, 9, 123);                                           /* 0x10C */
        r->vi[11] = emvup_ilw(c, 99);                                  /* 0x10D */
        r->vi[12] = emvup_ilw(c, 101);
        r->vi[13] = 0x88;
        r->vi[14] = 0x7FFF;
        r->vi[14] = (uint16_t)(r->vi[14] + 1u);
        r->vi[15] = r->vi[11];
        r->vi[15] |= r->vi[14];                                        /* 0x113 */
        emvup_lq(c, 13, r->vi[13] + 2u);                               /* 0x114..0x117 */
        emvup_lq(c, 14, r->vi[13]);
        emvup_lq(c, 1, r->vi[13] + 1u);
        emvup_lq(c, 2, 100);
        r->i = 0x3B800000u;                                            /* 0x118: 1/256 */
        do {
            emvup_streak_quad(c);                                      /* 0x119..0x173 */
            if (c->bad) return EM_VU1P_FAULT_OPERAND;
            r->vi[12] = (uint16_t)(r->vi[12] + 13u);                   /* 0x175 (delay slot) */
        } while ((int16_t)r->vi[11] > 0);                              /* 0x174 */
        if (kick(ctx, dmem, r->vi[10] & 1023u))                        /* 0x176 */
            return EM_VU1P_FAULT_KICK;
    }                                                                  /* 0x178 -> 0x0F7 */
    return c->bad ? EM_VU1P_FAULT_OPERAND : EM_VU1P_OK;
}

/* ========================================================== kind 2 ===== */

/* One particle's line (micro 0x112..0x137): the head (+0) and the tail
 * (+1) through rows 118..121 (VF24..VF27; P = ERCPR of the head's w, Q =
 * 1 / the tail's w), the tail through the clip rows 114..117, the colour
 * (+2) times 1/256 and the tail's fog weight, clamped to [0, row 122 x];
 * four rows at VI12: VF00, the head's XYZ (w = 1 + row 122 y), the colour,
 * the tail's XYZ (w = its fog weight, or 1 + row 122 y when the tail's clip
 * test failed); the tag row 124 (x = NLOOP | EOP) at row 101's base - 1. */
static inline void emvup_kind2_line(EmVu1PCtx *c)
{
    EmVu1PRegs *r = c->r;
    uint32_t p[4];
    for (unsigned k = 0; k < 4; ++k) r->vf[17][k] = emvup_mul(c, r->vf[17][k], r->i);   /* 0x112 */
    memcpy(p, r->vf[18], sizeof p);
    emvup_xform(c, 18, 24, p);                                         /* 0x113..0x116: head */
    memcpy(p, r->vf[19], sizeof p);
    emvup_xform(c, 2, 24, p);                                          /* 0x117..0x11A: tail */
    const uint32_t p_head = emvup_ercpr(c, r->vf[18][3]);             /* 0x11A ERCPR */
    emvup_xform(c, 19, 20, p);                                         /* 0x11B..0x11E: clip */
    const uint32_t q_tail = emvup_div(c, r->vf[0][3], r->vf[2][3]);   /* 0x11E (reads VF02 first) */
    r->acc[3] = emvup_mul(c, r->vf[0][3], r->vf[28][2]);              /* 0x11F */
    emvup_lq(c, 5, 124);
    r->vf[2][3] = emvup_madd(c, r->acc[3], r->vf[28][3], r->vf[2][3]);   /* 0x120: fog weight */
    r->vi[5] = emvup_ilw(c, 101);
    emvup_clip(c, r->vf[19]);                                          /* 0x122 */
    r->vf[5][0] = (uint32_t)(int32_t)(int16_t)r->vi[15];              /* 0x123: MFIR */
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = emvup_mul(c, r->vf[17][k], r->vf[2][3]);  /* 0x124 */
    r->vi[5] = (uint16_t)(r->vi[5] - 1u);
    r->q = q_tail;
    for (unsigned k = 0; k < 3; ++k) r->vf[2][k] = emvup_mul(c, r->vf[2][k], r->q);     /* 0x125 */
    r->vi[10] = 0;
    r->p = p_head;                                                     /* 0x126 MFP (12 cycles on) */
    r->vf[18][3] = r->p;
    r->vi[1] = (r->cf & 0x3Fu) != 0u;                                  /* 0x127 FCAND */
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = em_vu_min_bits(r->vf[16][k], r->vf[28][0]);  /* 0x128 */
    r->vi[10] |= r->vi[1];
    emvup_sq(c, 5, r->vi[5]);                                          /* 0x129: the GIF tag */
    for (unsigned k = 0; k < 3; ++k) r->vf[3][k] = emvup_mul(c, r->vf[18][k], r->vf[18][3]);   /* 0x12A */
    const int clipped = r->vi[10] != 0;                                /* 0x12B */
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = em_vu_max_bits(r->vf[16][k], r->vf[0][0]);  /* 0x12C (delay slot) */
    if (clipped) r->vf[2][3] = emvup_add(c, r->vf[0][3], r->vf[28][1]);   /* 0x12D */
    r->vf[3][3] = emvup_add(c, r->vf[0][3], r->vf[28][1]);            /* 0x12E */
    r->vi[11] = (uint16_t)(r->vi[11] - 1u);
    r->vi[13] = (uint16_t)(r->vi[13] + 4u);                            /* 0x12F */
    for (unsigned k = 0; k < 4; ++k) r->vf[16][k] = em_vu_ftoi0_bits(r->vf[16][k]);   /* 0x130 */
    emvup_lq(c, 17, r->vi[13] + 2u);
    for (unsigned k = 0; k < 4; ++k) r->vf[2][k] = em_vu_ftoi4_bits(r->vf[2][k]);     /* 0x131 */
    emvup_lq(c, 18, r->vi[13]);
    for (unsigned k = 0; k < 4; ++k) r->vf[3][k] = em_vu_ftoi4_bits(r->vf[3][k]);     /* 0x132 */
    emvup_lq(c, 19, r->vi[13] + 1u);
    emvup_sq(c, 0, r->vi[12]);                                         /* 0x133..0x136 */
    emvup_sq(c, 16, r->vi[12] + 2u);
    emvup_sq(c, 2, r->vi[12] + 3u);
    emvup_sq(c, 3, r->vi[12] + 1u);
    r->vi[12] = (uint16_t)(r->vi[12] + 4u);                            /* 0x137 */
}

/* MSCAL 0 of the kind-2 program (table 0x232540: 001CFBE0's kind 2; the
 * AREA11 user is the cable's hit effect node 0021AAC0). Its micro
 * 0x000..0x0F8 are the streak program's instructions (the particle record
 * with the trailing point included); the emission (0x0F9..0x13D) draws each
 * particle of a batch as a line of four rows at row 101's base after the
 * tag row 124 whose NLOOP is the particle count, every particle drawn
 * (none is rejected), and kicks it. */
static inline int em_vu1_kind2_program_mscal(EmVu1PRegs *r, EmVu1PQword *dmem, EmVu1PKick kick,
                                             void *ctx)
{
    if (!r || !dmem || !kick) return EM_VU1P_FAULT_ARGS;
    EmVu1PCtx cc = { r, dmem, 0 }, *c = &cc;
    const int rc0 = emvup_streak_start(c);
    if (rc0) return rc0;
    uint32_t batches = 0;
    for (;;) {
        /* 0x0F7 -> 0x01D */
        r->vi[9] = 0xF9;
        const int done = emvup_sprite_batch(c, 1);
        if (done < 0 || c->bad) return EM_VU1P_FAULT_OPERAND;
        if (++batches > 0x10000u) return EM_VU1P_FAULT_RUNAWAY;
        r->vi[1] = emvup_ilw(c, 99);                                   /* 0x0F9 */
        r->vi[2] = 0x7FFF;
        emvup_lq(c, 20, 114);                                          /* 0x0FB */
        emvup_lq(c, 21, 115);                                          /* 0x0FC */
        emvup_lq(c, 22, 116);                                          /* 0x0FE (delay slot) */
        if (r->vi[1] == r->vi[2]) break;                               /* 0x0FD -> 0x13E, the end */
        emvup_lq(c, 23, 117);                                          /* 0x100 (delay slot) */
        if (r->vi[1] == 0) continue;                                   /* 0x0FF -> 0x0F7 */
        for (unsigned k = 0; k < 5; ++k) emvup_lq(c, 24u + k, 118u + k);   /* 0x101..0x105 */
        emvup_lq(c, 9, 123);                                           /* 0x106 */
        r->vi[11] = emvup_ilw(c, 99);                                  /* 0x107 */
        r->vi[12] = emvup_ilw(c, 101);
        r->vi[13] = 0x88;
        r->vi[14] = 0x7FFF;
        r->vi[14] = (uint16_t)(r->vi[14] + 1u);
        r->vi[15] = r->vi[11];
        r->vi[15] |= r->vi[14];                                        /* 0x10D */
        emvup_lq(c, 17, r->vi[13] + 2u);                               /* 0x10E..0x110 */
        emvup_lq(c, 18, r->vi[13]);
        emvup_lq(c, 19, r->vi[13] + 1u);
        r->i = 0x3B800000u;                                            /* 0x111: 1/256 */
        do {
            emvup_kind2_line(c);                                       /* 0x112..0x137 */
            if (c->bad) return EM_VU1P_FAULT_OPERAND;
        } while ((int16_t)r->vi[11] > 0);                              /* 0x138 */
        if (kick(ctx, dmem, r->vi[5] & 1023u))                         /* 0x13A */
            return EM_VU1P_FAULT_KICK;
    }                                                                  /* 0x13C -> 0x0F7 */
    return c->bad ? EM_VU1P_FAULT_OPERAND : EM_VU1P_OK;
}

/* ============================================================ grid ====== */

/* The level background's grid program (docs/BACKGROUND.md): the DMA packet
 * 0x0023C990 that 001E1E60 CALLs (FLUSHA, STCYCL 4,4, STMASK, STMOD 0 and
 * one MPG of 79 instructions, ELF 0x0023C9B8, to micro 0), entered by
 * 001D71A0's MSCAL 0. It reads dmem 0x200..0x203 (the matrix D_00253570,
 * VF28..VF31), 0x204 (VF10: the origin x, y and the zoom in z), 0x205
 * (VF11: the x and y steps), 0x206 (VF27: the ST offset, the XY bias in z)
 * and 0x207 (VF26: the ST scale), and the GIF tag template 001E1E60 uploads
 * to dmem 0, 0x81 and 0x102, which it never writes. For each of 32 rows
 * (VI13) and 32 columns (VI12): d = VF28 x + VF29 y + VF30 zoom on all four
 * lanes (an accumulator chain), P = the EFU's reciprocal length of d (the
 * model above), u = d P on all four lanes, ST.xy = (VF0 + VF27) + u VF26
 * (the sum through the accumulator), the vertex VF8.xy = (x, y) + bias with
 * VF8.zw = VF0 + VF0 = (0, 2.0) from before the loop; ST.xy and VF8 are stored as the second row of the
 * current buffer (VI7: qwords +2, +3 of the column) and the first row of
 * the next one (VI8: +0, +1). x is reloaded from 0x204.x at every row
 * and stepped by VF11.x after each column, y by VF11.y after each row.
 * After every row but the first the current buffer is kicked; VI7 and VI8
 * each step by 0x81 and wrap from 0x183 to 0 (three buffers). The
 * pipeline stalls change no value read (the reference test's VU1 machine,
 * tools/test_background_reference.py run_kernel, executes the original). */
static inline int em_vu1_grid_program_mscal(EmVu1PRegs *r, EmVu1PQword *dmem, EmVu1PKick kick, void *ctx)
{
    if (!r || !dmem || !kick) return EM_VU1P_FAULT_ARGS;
    EmVu1PCtx cc = { r, dmem, 0 }, *c = &cc;
    for (unsigned k = 0; k < 4u; ++k) emvup_lq(c, 28u + k, 512u + k);   /* 0x000..0x003 */
    emvup_lq(c, 27, 518);                                               /* 0x004 */
    emvup_lq(c, 26, 519);                                               /* 0x005 */
    r->vi[7] = 0;                                                       /* 0x006 */
    r->vi[8] = 129;                                                     /* 0x007 */
    emvup_lq(c, 10, 516);                                               /* 0x008 */
    emvup_lq(c, 11, 517);                                               /* 0x009 */
    r->vi[13] = 32;                                                     /* 0x00A */
    for (unsigned k = 0; k < 4u; ++k) r->vf[8][k] = emvup_add(c, r->vf[0][k], r->vf[0][k]);   /* 0x00B */
    do {
        r->vf[10][0] = dmem[516u].w[0];                                 /* 0x00C */
        r->vi[12] = 32;                                                 /* 0x00D */
        r->vi[4] = (uint16_t)(r->vi[7] + 1u);                           /* 0x00E */
        r->vi[5] = (uint16_t)(r->vi[8] + 1u);                           /* 0x00F */
        do {
            uint32_t d[4], u[4];
            for (unsigned k = 0; k < 4u; ++k) {
                r->acc[k] = emvup_mul(c, r->vf[28][k], r->vf[10][0]);              /* 0x010 */
                r->acc[k] = emvup_madd(c, r->acc[k], r->vf[29][k], r->vf[10][1]);  /* 0x011 */
                d[k] = emvup_madd(c, r->acc[k], r->vf[30][k], r->vf[10][2]);       /* 0x012 */
            }
            memcpy(r->vf[2], d, sizeof d);
            r->p = emvup_erleng(c, r->vf[2]);                           /* 0x016 */
            r->vf[1][0] = r->p;                                         /* 0x018 */
            for (unsigned k = 0; k < 4u; ++k) u[k] = emvup_mul(c, r->vf[2][k], r->vf[1][0]);   /* 0x01C */
            memcpy(r->vf[3], u, sizeof u);
            for (unsigned k = 0; k < 2u; ++k) {
                r->acc[k] = emvup_add(c, r->vf[0][k], r->vf[27][k]);              /* 0x020 */
                r->vf[4][k] = emvup_madd(c, r->acc[k], r->vf[3][k], r->vf[26][k]); /* 0x021 */
            }
            for (unsigned k = 0; k < 2u; ++k)
                r->vf[8][k] = emvup_add(c, r->vf[10][k], r->vf[27][2]);  /* 0x022 */
            for (unsigned k = 0; k < 2u; ++k) {                         /* 0x026 / 0x027: x and y only */
                dmem[(r->vi[4] + 2u) & 1023u].w[k] = r->vf[4][k];
                dmem[(r->vi[5] + 0u) & 1023u].w[k] = r->vf[4][k];
            }
            emvup_sq(c, 8, r->vi[4] + 3u);                              /* 0x028 */
            emvup_sq(c, 8, r->vi[5] + 1u);                              /* 0x029 */
            r->vi[4] = (uint16_t)(r->vi[4] + 4u);                       /* 0x02D */
            r->vi[5] = (uint16_t)(r->vi[5] + 4u);                       /* 0x02E */
            r->vf[10][0] = emvup_add(c, r->vf[10][0], r->vf[11][0]);    /* 0x02F */
            r->vi[12] = (uint16_t)(r->vi[12] - 1u);                     /* 0x030 */
            if (c->bad) return EM_VU1P_FAULT_OPERAND;
        } while (r->vi[12] != 0);                                       /* 0x032 */
        r->vi[1] = 32;                                                  /* 0x034 */
        if (r->vi[13] != r->vi[1] && kick(ctx, dmem, r->vi[7] & 1023u)) /* 0x036, 0x038 */
            return EM_VU1P_FAULT_KICK;
        r->vi[7] = (uint16_t)(r->vi[7] + 129u);                         /* 0x03A */
        r->vi[1] = (uint16_t)(r->vi[7] - 387u);                         /* 0x03B */
        if ((int16_t)r->vi[1] >= 0) r->vi[7] = 0;                       /* 0x03D, 0x03F */
        r->vi[8] = (uint16_t)(r->vi[8] + 129u);                         /* 0x040 */
        r->vi[1] = (uint16_t)(r->vi[8] - 387u);                         /* 0x041 */
        if ((int16_t)r->vi[1] >= 0) r->vi[8] = 0;                       /* 0x043, 0x045 */
        r->vf[10][1] = emvup_add(c, r->vf[10][1], r->vf[11][1]);        /* 0x046 */
        r->vi[13] = (uint16_t)(r->vi[13] - 1u);                         /* 0x047 */
    } while (r->vi[13] != 0);                                           /* 0x049 */
    return c->bad ? EM_VU1P_FAULT_OPERAND : EM_VU1P_OK;                 /* 0x04B */
}

/* =========================================================== floor ====== */

/* AREA01's floor-field program and its ripple-surface variant
 * (docs/LEVEL2_RENDER.md "Floor-field and ripple programs").
 *
 * The floor-field program: the DMA packet D_002345E0 that 001E9E60 hands 001CB760
 * (FLUSHE, STCYCL 4,4, STMASK, STMOD 0, BASE 0x20, OFFSET 0x190 and one
 * MPG of 153 instructions, ELF 0x00234610, to micro 0), then six batches
 * of 24 qwords UNPACKed to TOPS, the first entered by MSCAL 0 and the
 * others by MSCNT. MSCNT resumes after the end bit's delay slot (micro
 * 0x02F), whose unconditional branch returns to micro 0: every batch runs
 * the same code. Inputs: dmem 0..3 (the camera matrix 0x70003AC0, VF28..
 * VF31) and 0x3F8..0x3FF (001E9E60's 9-qword packet: VF21 = 0x3F8, the
 * colour row 0x3F9, VF20 = 0x3FA, VF16 = 0x3FB, the GIF tag 0x3FC, VF19 =
 * 0x3FD, VF17 = 0x3FE, VF18 = 0x3FF); the batch's three rows of 8 points
 * at TOP..TOP+23. It emits a 16-vertex strip from rows 0 and 1 (TOP + j
 * and TOP + 8 + j alternately), three qwords (ST, RGBAQ, XYZF2) per vertex
 * from TOP+25, and kicks the GIF tag copied to TOP+24.
 *
 * Pipeline: every value read is its newest producer's (the reference test,
 * tools/test_level2_floor_vu_reference.py, runs the original microcode on
 * the shared VU1 machine): the Q of micro 0x050 follows a WAITQ, 0x074 and
 * 0x08F read the DIV of 0x06D (issued 7 cycles before 0x074), FCAND at
 * 0x06C sees the CLIP of 0x068 (4 cycles) with the strip's two previous
 * judgements (mask 0x3FFFF; FCSET 0 at micro 0x005 clears the history
 * at every batch). ERLENG is the shared EFU model above. VF02.w is never
 * written: ST's w lane stores the register's previous value.
 *
 * The ripple program (`wide`): the packet D_00234B00 that 001E7D20 (the
 * AREA01 ripple surface) hands 001CB760: the same VIF set-up and one MPG
 * of 145 instructions (ELF 0x00234B30), then 30 batches of 96 qwords
 * (three rows of 32 points; MSCAL 0, then MSCNT). Its instructions are the
 * floor program's except: rows of 32 (VI12 = TOP + 31, the row step 0x20,
 * the second neighbour at +32), output from TOP + 0x61 with the GIF tag at
 * TOP + 0x60 (a 64-vertex strip), the texture pair's x and y adding VF20.x
 * and VF20.y (not VF0's), and no depth scale (the floor program's micro
 * 0x04C..0x053 are absent, so its later micro addresses are 8 lower). */
static inline void emvup_floor_vertex(EmVu1PCtx *c, int wide)
{
    EmVu1PRegs *r = c->r;
    uint32_t (*vf)[4] = r->vf;
    unsigned k;
    for (k = 0; k < 3u; ++k) vf[10][k] = emvup_sub(c, vf[3][k], vf[16][k]);     /* 0x031 */
    r->p = emvup_erleng(c, vf[10]);                                     /* 0x035, WAITP */
    vf[10][3] = r->p;                                                   /* 0x037 MFP */
    for (k = 0; k < 3u; ++k) vf[10][k] = emvup_mul(c, vf[10][k], vf[10][3]);    /* 0x03B */
    vf[13][3] = emvup_sub(c, vf[0][3], vf[10][1]);                      /* 0x03F */
    r->i = 0x42800000u;                                                 /* its I: 64 */
    vf[13][3] = emvup_mul(c, vf[13][3], vf[21][0]);                     /* 0x040 */
    vf[13][3] = emvup_add(c, vf[13][3], vf[21][1]);                     /* 0x041 */
    vf[13][3] = emvup_mul(c, vf[13][3], vf[3][3]);                      /* 0x042 */
    for (k = 0; k < 3u; ++k) vf[13][k] = c->m[1017u].w[k];              /* 0x043 */
    for (k = 0; k < 3u; ++k) vf[13][k] = emvup_mul(c, vf[13][k], vf[10][1]);    /* 0x044 */
    for (k = 0; k < 3u; ++k) vf[13][k] = emvup_mul(c, vf[13][k], vf[21][2]);    /* 0x045 */
    for (k = 0; k < 4u; ++k) vf[13][k] = em_vu_max_bits(vf[13][k], vf[0][0]);   /* 0x046 */
    r->i = 0x437F0000u;                                                 /* its I: 255 */
    for (k = 0; k < 4u; ++k) vf[13][k] = em_vu_min_bits(vf[13][k], r->i);       /* 0x047 */
    const unsigned off = wide ? 20u : 0u;                               /* VF20 or VF0 */
    vf[11][0] = emvup_add(c, vf[10][0], vf[off][0]);                    /* 0x048 */
    vf[11][1] = vf[10][2];                                              /* 0x049 MR32 */
    vf[11][1] = emvup_add(c, vf[11][1], vf[off][1]);                    /* 0x04A */
    for (k = 0; k < 2u; ++k) vf[11][k] = emvup_mul(c, vf[11][k], vf[20][2]);    /* 0x04B */
    if (!wide) {
        vf[1][0] = emvup_add(c, vf[0][0], vf[21][3]);                   /* 0x04C */
        vf[1][0] = emvup_sub(c, vf[1][0], vf[3][1]);                    /* 0x04D */
        r->q = emvup_div(c, vf[1][0], vf[10][1]);                       /* 0x04E, WAITQ */
        vf[1][0] = emvup_add(c, vf[0][0], r->q);                        /* 0x050 */
        vf[1][0] = emvup_add(c, vf[1][0], vf[16][1]);                   /* 0x051 */
        vf[1][0] = emvup_sub(c, vf[1][0], vf[3][1]);                    /* 0x052 */
        for (k = 0; k < 2u; ++k) vf[11][k] = emvup_mul(c, vf[11][k], vf[1][0]); /* 0x053 */
    }
    vf[11][2] = emvup_add(c, vf[0][2], vf[0][3]);                       /* 0x054 */
    vf[4][1] = c->m[(r->vi[13] + 1u) & 1023u].w[1];                     /* 0x055 */
    vf[12][1] = emvup_sub(c, vf[3][1], vf[4][1]);                       /* 0x056 */
    vf[12][0] = vf[12][1];                                              /* 0x057 MR32 */
    vf[4][1] = c->m[(r->vi[13] + (wide ? 32u : 8u)) & 1023u].w[1];      /* 0x058 */
    vf[12][1] = emvup_sub(c, vf[3][1], vf[4][1]);                       /* 0x059 */
    for (k = 0; k < 2u; ++k) vf[12][k] = emvup_mul(c, vf[12][k], vf[20][3]);    /* 0x05A */
    for (k = 0; k < 2u; ++k) vf[11][k] = emvup_add(c, vf[11][k], vf[12][k]);    /* 0x05B */
    uint32_t p[4];
    memcpy(p, vf[3], sizeof p);
    emvup_xform(c, 4, 28, p);                                           /* 0x05C..0x05F */
    for (k = 0; k < 4u; ++k) r->acc[k] = emvup_mul(c, vf[17][k], vf[4][k]);     /* 0x063 */
    for (k = 0; k < 4u; ++k) vf[5][k] = emvup_madd(c, r->acc[k], vf[18][k], vf[4][3]);   /* 0x064 */
    emvup_clip(c, vf[5]);                                               /* 0x068 */
    r->vi[1] = (r->cf & 0x03FFFFu) != 0u;                               /* 0x06C FCAND */
    r->q = emvup_div(c, vf[0][3], vf[4][3]);                            /* 0x06D */
    for (k = 0; k < 3u; ++k) vf[6][k] = emvup_mul(c, vf[4][k], r->q);   /* 0x074 */
    r->acc[3] = emvup_mul(c, vf[0][3], vf[19][2]);                      /* 0x078 */
    vf[8][3] = emvup_madd(c, r->acc[3], vf[19][3], vf[4][3]);           /* 0x079 */
    vf[8][3] = em_vu_min_bits(vf[8][3], vf[19][0]);                     /* 0x07D */
    vf[6][3] = em_vu_max_bits(vf[8][3], vf[0][0]);                      /* 0x081 */
    if (r->vi[1] != 0)                                                  /* 0x085 */
        vf[6][3] = emvup_add(c, vf[6][3], vf[19][1]);                   /* 0x087 */
    for (k = 0; k < 4u; ++k) vf[7][k] = em_vu_ftoi4_bits(vf[6][k]);     /* 0x08B */
    for (k = 0; k < 3u; ++k) vf[2][k] = emvup_mul(c, vf[11][k], r->q);  /* 0x08F */
    for (k = 0; k < 4u; ++k) vf[5][k] = em_vu_ftoi0_bits(vf[13][k]);    /* 0x090 */
    emvup_sq(c, 2, r->vi[11]);                                          /* 0x094..0x096 */
    emvup_sq(c, 5, r->vi[11] + 1u);
    emvup_sq(c, 7, r->vi[11] + 2u);
}

/* One batch (MSCAL 0, or MSCNT after a batch of this program): `top` is
 * the TOP register (XTOP), the batch's buffer. */
static inline int emvup_floor_batch(EmVu1PRegs *r, EmVu1PQword *dmem, EmVu1PKick kick, void *ctx,
                                    uint32_t top, int wide)
{
    if (!r || !dmem || !kick) return EM_VU1P_FAULT_ARGS;
    EmVu1PCtx cc = { r, dmem, 0 }, *c = &cc;
    const uint32_t row = wide ? 32u : 8u;                               /* points per row */
    r->vi[13] = (uint16_t)(top & 1023u);                                /* 0x000 XTOP */
    r->vi[14] = 0x7FFF;                                                 /* 0x001 */
    r->vi[14] = (uint16_t)(r->vi[14] + 1u);                             /* 0x002 */
    r->vi[12] = (uint16_t)(r->vi[13] + row - 1u);                       /* 0x003 */
    r->vi[11] = (uint16_t)(r->vi[13] + 3u * row + 1u);                  /* 0x004 */
    r->cf = 0;                                                          /* 0x005 FCSET */
    emvup_lq(c, 16, 1019);                                              /* 0x006..0x00F */
    emvup_lq(c, 17, 1022);
    emvup_lq(c, 18, 1023);
    emvup_lq(c, 19, 1021);
    emvup_lq(c, 20, 1018);
    emvup_lq(c, 21, 1016);
    for (unsigned k = 0; k < 4u; ++k) emvup_lq(c, 28u + k, k);
    for (;;) {
        emvup_lq(c, 3, r->vi[13]);                                      /* 0x010 */
        r->vi[1] = 0x31;                                                /* 0x012 */
        r->vi[15] = 0x16;                                               /* 0x014 JALR */
        emvup_floor_vertex(c, wide);
        r->vi[11] = (uint16_t)(r->vi[11] + 3u);                         /* 0x016 */
        r->vi[13] = (uint16_t)(r->vi[13] + row);                        /* 0x017 */
        emvup_lq(c, 3, r->vi[13]);                                      /* 0x018 */
        r->vi[1] = 0x31;                                                /* 0x01A */
        r->vi[15] = 0x1E;                                               /* 0x01C JALR */
        emvup_floor_vertex(c, wide);
        r->vi[13] = (uint16_t)(r->vi[13] - row);                        /* 0x01E */
        r->vi[11] = (uint16_t)(r->vi[11] + 3u);                         /* 0x01F */
        if (c->bad) return EM_VU1P_FAULT_OPERAND;
        const int more = r->vi[13] != r->vi[12];                        /* 0x020 IBNE */
        r->vi[13] = (uint16_t)(r->vi[13] + 1u);                         /* 0x021 (delay slot) */
        if (!more) break;
    }
    r->vi[11] = (uint16_t)(top & 1023u);                                /* 0x022 XTOP */
    r->vi[11] = (uint16_t)(r->vi[11] + 3u * row);                       /* 0x024 */
    emvup_lq(c, 1, 1020);                                               /* 0x025 */
    emvup_sq(c, 1, r->vi[11]);                                          /* 0x029 */
    if (kick(ctx, dmem, r->vi[11] & 1023u)) return EM_VU1P_FAULT_KICK;  /* 0x02D [E] */
    return EM_VU1P_OK;
}

static inline int em_vu1_floor_program_mscal(EmVu1PRegs *r, EmVu1PQword *dmem, EmVu1PKick kick, void *ctx,
                                             uint32_t top)
{
    return emvup_floor_batch(r, dmem, kick, ctx, top, 0);
}

static inline int em_vu1_ripple_program_mscal(EmVu1PRegs *r, EmVu1PQword *dmem, EmVu1PKick kick, void *ctx,
                                              uint32_t top)
{
    return emvup_floor_batch(r, dmem, kick, ctx, top, 1);
}

#endif
