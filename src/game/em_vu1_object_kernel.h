/* em_vu1_object_kernel.h — the VU1 object kernel that every world-owner,
 * player and actor draw unit CALLs (DMA CALL 0x0023C750), translated from
 * its VU1 microcode into CPU-exact C.
 *
 * The kernel packet at 0x0023C750 is one DMA CNT whose VIF codes are, in
 * order, FLUSHA, STCYCL 4,4, STMASK 0, STMOD 0, BASE 0x1B0, OFFSET 0x10E
 * and one MPG of 62 instructions (ELF 0x0023C780) to micro address 0; a RET
 * follows (the reference test asserts this exact sequence). The
 * unit's model blocks follow the CALL: each block UNPACKs 128 qwords (32
 * vertices x 4 qwords) to TOPS and ends in MSCAL 0 (the first block) or
 * MSCNT (the others). docs/OWNER_DRAW.md section 3 names every upload and
 * docs/VU1_OBJECT_KERNEL.md the evidence for this translation.
 *
 * Micro addresses below are instruction indices of that program (8 bytes
 * each, micro 0x000 = ELF 0x0023C780).
 *
 * Data memory the kernel reads (qword addresses):
 *   TOP + 4i + 0..3   vertex i (i = 0..31): TEX0 qword, (s, t, 1, 0),
 *                     normal (x, y, z, 0), position (x, y, z, data word)
 *   word .. word + 3  the vertex's position matrix M (node x VP, rows),
 *   word + 4 .. + 6   its lighting matrix L (C x A rows), where word = the
 *                     low 16 bits of the data word (bit 15 = "no kick"),
 *                     addresses modulo 1024
 *   1013 .. 1016      the colour matrix B (three light colours + ambient,
 *                     carrying the 8388608 bias)          [MSCAL only]
 *   1020              the GIF tag template                 [every batch]
 *   1021              the fog row (255, 2048, A, B)        [MSCAL only]
 *   1022 / 1023       the guard-band scale / offset rows   [MSCAL only]
 *
 * Per vertex (every value is a VU lane operation, see "Arithmetic"):
 *   c     = p x M: ((M0 * p.x + M1 * p.y) + M2 * p.z) + M3 * 1   (0x01B..0x01E)
 *   light = (L0 * n.x + L1 * n.y) + L2 * n.z                    (0x01F..0x021)
 *   Q     = 1 / c.w                                             (0x022)
 *   g     = G0 * c + G1 * c.w (G0 = dmem 1022, G1 = dmem 1023)  (0x022..0x023)
 *   CLIP  of g.xyz against |g.w| into a 24-bit history           (0x027)
 *   fog   = max(min(1 * A + B * c.w, 255), 0), the row's x lane
 *           being the cap                          (0x024, 0x025, 0x029, 0x02D)
 *   light = max(light, 0)                                        (0x026)
 *   colour= ((B0 * light.x + B1 * light.y) + B2 * light.z) + B3 * 1
 *                                                               (0x02B..0x02F)
 *   ADC   = data word bit 15, or any CLIP bit of vertices i-2..i
 *           (history AND 0x03FFFF)                               (0x02B, 0x02C)
 *           The window rests on the reference interpreter's CLIP flag
 *           latency (4 cycles, exactly the distance from 0x027 to 0x02B):
 *           an interpreter ASSUMPTION like the float rules below, see
 *           docs/VU1_OBJECT_KERNEL.md section 4.
 *   fog  += the row's y lane (2048) when ADC                     (0x031; the
 *           branch at 0x02F skips it otherwise), so bit 15 of the
 *           kicked F word is the GS ADC bit
 * Output qwords of vertex i at TOP + 0x85 + 4i:
 *   TEX0  = the vertex's TEX0 qword, every bit                   (0x02D)
 *   ST    = (s * Q, t * Q, 1 * Q, w carried)                     (0x030)
 *   RGBAQ = min(colour, 8388863.0) in all four lanes             (0x034)
 *   XYZF2 = ftoi4 of (c.x * Q, c.y * Q, c.z * Q, fog)            (0x02A, 0x035)
 * TOP + 0x84 receives the template tag (dmem 1020) and the kernel kicks the
 * packet at TOP + 0x84 (micro 0x03A): the template (PRE, PRIM 0x03C, NREG 4,
 * REGS TEX0 ST RGBAQ XYZF2, NLOOP 32, EOP in every capture) and 128 qwords.
 *
 * The loop is software-pipelined: iteration i first stores vertex i-1's
 * four qwords (at TOP + 4i + 0x81..0x84), then reads vertex i's ST input
 * and the next vertex's data word, rows, position, normal and TEX0. So
 * iteration 0 stores the three registers left by whatever ran before (the
 * carried TEX0, ST and RGBAQ) at TOP + 0x81..0x83, and the ST output's w
 * lane is never written by this program: it is the carried register's.
 * EmVu1ObjState carries all three; they never reach the kicked packet
 * except that w lane, which the GS ignores (PACKED ST uses S, T and Q).
 * The last iteration's look-ahead reads (TOP + 0x80..0x83 and seven rows at
 * a stale word) are dead; this translation performs them and discards them.
 *
 * Entry points: MSCAL (micro 0x000) loads dmem 1013..1016 and 1021..1023
 * into registers, prepares the colour cap and clears the clip history;
 * MSCNT resumes at micro 0x03C, which re-enters the batch setup at 0x00D
 * with those registers unchanged (a later upload to 1013..1016 or
 * 1021..1023 does NOT reach an MSCNT batch). Every batch re-reads 1020, and
 * every batch starts with an empty clip history (the delay slot after the
 * end, micro 0x03B, clears it). The kernel never culls.
 *
 * Arithmetic. VU1 was never measured (docs/EE_FLOAT_MODEL.md); the INI gives
 * VU1 the VU0 settings, so the VU0 lane rules of em_ee_float.h are ASSUMED:
 * DAZ operands, exact results truncated toward zero, FTZ, finite overflow to
 * +-FLT_MAX (em_eei_vu_add_raw / em_eei_vu_mul_raw, the multiply-add being
 * a truncated product added to ACC), VDIV (3,3) for Q, the raw
 * sign-magnitude MAX/MINI order (-0 below +0) and the saturating VFTOI4.
 * Every captured batch agrees (docs/VU1_OBJECT_KERNEL.md). The VU0 operand
 * clamps depend on the instruction form and were measured for VU0 macro
 * forms only, so an exponent-255 word (Inf/NaN encoding) that reaches a
 * multiply or add on a live lane is NOT established: the batch faults
 * (EM_VU1_OBJ_FAULT_OPERAND). No capture has one.
 *
 * Header-only (static inline), pure C, no host float arithmetic: every
 * backend and the reference test (tools/test_vu1_object_kernel_reference.py)
 * share one translation without a build-list change. */
#ifndef EM_VU1_OBJECT_KERNEL_H
#define EM_VU1_OBJECT_KERNEL_H

#include <stdint.h>
#include <string.h>

#include "game/em_ee_float.h"

#define EM_VU1_OBJ_DMEM_QWORDS 1024u
#define EM_VU1_OBJ_VERTICES 32u
#define EM_VU1_OBJ_BASE 0x1B0u        /* the kernel packet's VIF BASE   */
#define EM_VU1_OBJ_OFFSET 0x10Eu      /* the kernel packet's VIF OFFSET */
#define EM_VU1_OBJ_KICK 0x84u         /* XGKICK address - TOP           */
#define EM_VU1_OBJ_PACKET_QWORDS 129u /* template + 32 x 4              */
#define EM_VU1_OBJ_COLOUR_CAP 0x4B0000FFu  /* 8388863.0 = 2^23 + 255    */

enum {
    EM_VU1_OBJ_OK = 0,
    EM_VU1_OBJ_FAULT_ARGS = 1,     /* NULL argument                        */
    EM_VU1_OBJ_FAULT_OPERAND = 2,  /* an exponent-255 word reached a live
                                      multiply/add: not established      */
    EM_VU1_OBJ_FAULT_STATE = 3     /* MSCNT without an earlier MSCAL      */
};

/* ADC reasons of one kicked vertex. */
#define EM_VU1_OBJ_ADC_DATA 1u     /* data word bit 15                     */
#define EM_VU1_OBJ_ADC_CLIP 2u     /* a CLIP bit of vertex i-2, i-1 or i   */

typedef struct { uint32_t w[4]; } EmVu1ObjQword;

/* The registers that live across batches. */
typedef struct {
    uint32_t loaded;          /* an MSCAL has run                        */
    uint32_t colour[4][4];    /* dmem 1013..1016 as loaded at MSCAL      */
    uint32_t fog[4];          /* dmem 1021 (255, 2048, A, B)             */
    uint32_t guard_scale[4];  /* dmem 1022                               */
    uint32_t guard_offset[4]; /* dmem 1023                               */
    uint32_t cap;             /* 0 + 8388863.0, prepared at MSCAL        */
    /* Carried: vertex 31's outputs of the previous batch, or what the
     * previous program left in those registers before an MSCAL. */
    EmVu1ObjQword tex0, st, rgbaq;
} EmVu1ObjState;

typedef struct {
    uint32_t fault;           /* EM_VU1_OBJ_FAULT_*                      */
    uint32_t fault_vertex;    /* the vertex being computed at the fault  */
    uint32_t top;             /* TOP of the batch                         */
    uint32_t kick;            /* XGKICK dmem address (TOP + 0x84) & 1023 */
    uint32_t why[EM_VU1_OBJ_VERTICES];   /* EM_VU1_OBJ_ADC_* per vertex  */
    uint32_t clip[EM_VU1_OBJ_VERTICES];  /* the vertex's CLIP judgement:
                                            +x -x +y -y +z -z = bits 0..5 */
    uint32_t ftoi_saturated;  /* VFTOI4 lanes outside int32 (saturated by
                                 the VU0 rule, assumed for VU1)          */
    uint32_t saturated;       /* bit i: vertex i has such a lane         */
} EmVu1ObjBatch;

/* ------------------------------------------------------- lane arithmetic */

static inline int emvuo_live(uint32_t w) { return (w & 0x7F800000u) != 0x7F800000u; }

/* Operands of a multiply or add: an exponent-255 word faults (*bad). */
static inline uint32_t emvuo_mul(uint32_t a, uint32_t b, uint32_t *bad)
{
    if (!emvuo_live(a) || !emvuo_live(b)) *bad = 1u;
    return em_eei_vu_mul_raw(a, b);
}

static inline uint32_t emvuo_add(uint32_t a, uint32_t b, uint32_t *bad)
{
    if (!emvuo_live(a) || !emvuo_live(b)) *bad = 1u;
    return em_eei_vu_add_raw(a, b);
}

/* ACC + fs * ft, the product truncated first. */
static inline uint32_t emvuo_madd(uint32_t acc, uint32_t fs, uint32_t ft, uint32_t *bad)
{
    return emvuo_add(acc, emvuo_mul(fs, ft, bad), bad);
}

/* The CLIP judgement of (x, y, z) against |w| on DAZ-read values. */
static inline int64_t emvuo_key(uint32_t b)
{
    b = em_eei_daz(b);
    return (b & EM_EE_SIGN) ? -(int64_t)(b & 0x7FFFFFFFu) : (int64_t)b;
}

static inline uint32_t emvuo_clip(const uint32_t g[4])
{
    const int64_t w = emvuo_key(g[3] & 0x7FFFFFFFu), nw = -w;
    uint32_t f = 0;
    for (unsigned c = 0; c < 3; ++c) {
        const int64_t v = emvuo_key(g[c]);
        if (v > w) f |= 1u << (2 * c);
        if (v < nw) f |= 2u << (2 * c);
    }
    return f;
}

static inline uint32_t emvuo_ftoi4(uint32_t v, uint32_t *saturated)
{
    const uint32_t e = (v >> 23) & 0xFFu;
    if (e != 0u && e + 4u >= 158u) ++*saturated;
    return em_vu_ftoi4_bits(v);
}

/* ------------------------------------------------------------ one vertex */

/* The inputs one vertex's arithmetic reads, as the program loaded them. */
typedef struct {
    uint32_t word;            /* data word, low 16 bits                  */
    uint32_t pos[3];          /* qword 3 x, y, z                         */
    uint32_t normal[4];       /* qword 2                                  */
    EmVu1ObjQword tex0;       /* qword 0                                  */
    uint32_t m[4][4];         /* rows word + 0..3                          */
    uint32_t l[3][4];         /* rows word + 4..6                          */
    uint32_t st_in[4];        /* qword 1 (read in the vertex's own iteration) */
} EmVu1ObjInput;

/* One vertex (micro 0x01B..0x035): out[0..3] = TEX0, ST, RGBAQ, XYZF2.
 * `hist` is the clip history (in/out), `st_w` the carried ST w lane.
 * Returns 0, or 1 when an exponent-255 word reached a live lane. */
static inline uint32_t em_vu1_object_vertex(const EmVu1ObjState *s, const EmVu1ObjInput *in,
                                            uint32_t *hist, uint32_t st_w,
                                            EmVu1ObjQword out[4], uint32_t *why,
                                            uint32_t *clip, uint32_t *saturated)
{
    uint32_t bad = 0u, c[4], light[3], g[4], col[4], scr[3], q = 0u;
    /* 0x01B..0x01E: c = p x M */
    for (unsigned k = 0; k < 4; ++k) {
        uint32_t acc = emvuo_mul(in->m[0][k], in->pos[0], &bad);
        acc = emvuo_madd(acc, in->m[1][k], in->pos[1], &bad);
        acc = emvuo_madd(acc, in->m[2][k], in->pos[2], &bad);
        c[k] = emvuo_madd(acc, in->m[3][k], EM_EE_ONE, &bad);
    }
    /* 0x01F..0x021: the lighting matrix times the normal (lane w unused) */
    for (unsigned k = 0; k < 3; ++k) {
        uint32_t acc = emvuo_mul(in->l[0][k], in->normal[0], &bad);
        acc = emvuo_madd(acc, in->l[1][k], in->normal[1], &bad);
        light[k] = emvuo_madd(acc, in->l[2][k], in->normal[2], &bad);
    }
    /* 0x022: Q = 1 / c.w (VDIV, fs = 1.0) */
    if (!emvuo_live(c[3])) bad = 1u;
    (void)em_vu_div_bits(EM_EE_ONE, c[3], 3, 3, &q);
    /* 0x022..0x023: the guard-band vector, 0x027: CLIP */
    for (unsigned k = 0; k < 4; ++k)
        g[k] = emvuo_madd(emvuo_mul(s->guard_scale[k], c[k], &bad),
                          s->guard_offset[k], c[3], &bad);
    const uint32_t cf = emvuo_clip(g);
    *hist = ((*hist << 6) | cf) & 0xFFFFFFu;
    /* 0x024, 0x025, 0x029, 0x02D: fog */
    uint32_t fog = emvuo_madd(emvuo_mul(EM_EE_ONE, s->fog[2], &bad), s->fog[3], c[3], &bad);
    fog = em_vu_min_bits(fog, s->fog[0]);
    /* 0x026: lighting clamped at 0 */
    for (unsigned k = 0; k < 3; ++k) light[k] = em_vu_max_bits(light[k], 0u);
    /* 0x02A: screen = c.xyz * Q */
    for (unsigned k = 0; k < 3; ++k) scr[k] = emvuo_mul(c[k], q, &bad);
    /* 0x02B..0x02F: colour = light x B + ambient row */
    for (unsigned k = 0; k < 4; ++k) {
        uint32_t acc = emvuo_mul(s->colour[0][k], light[0], &bad);
        acc = emvuo_madd(acc, s->colour[1][k], light[1], &bad);
        acc = emvuo_madd(acc, s->colour[2][k], light[2], &bad);
        col[k] = emvuo_madd(acc, s->colour[3][k], EM_EE_ONE, &bad);
    }
    /* 0x02B, 0x02C: ADC */
    uint32_t w = (in->word & 0x8000u) ? EM_VU1_OBJ_ADC_DATA : 0u;
    if (*hist & 0x03FFFFu) w |= EM_VU1_OBJ_ADC_CLIP;
    fog = em_vu_max_bits(fog, 0u);
    /* 0x02D: TEX0 passes through */
    out[0] = in->tex0;
    /* 0x030: ST = input xyz * Q, w carried */
    for (unsigned k = 0; k < 3; ++k) out[1].w[k] = emvuo_mul(in->st_in[k], q, &bad);
    out[1].w[3] = st_w;
    /* 0x031: ADC adds the fog row's y lane */
    if (w) fog = emvuo_add(fog, s->fog[1], &bad);
    /* 0x034: colour cap, 0x035: fixed point */
    for (unsigned k = 0; k < 4; ++k) out[2].w[k] = em_vu_min_bits(col[k], s->cap);
    for (unsigned k = 0; k < 3; ++k) out[3].w[k] = emvuo_ftoi4(scr[k], saturated);
    out[3].w[3] = emvuo_ftoi4(fog, saturated);
    *why = w;
    *clip = cf;
    return bad;
}

/* ---------------------------------------------------------------- batch */

static inline EmVu1ObjQword emvuo_ld(const EmVu1ObjQword *m, uint32_t a) { return m[a & 1023u]; }
static inline void emvuo_st(EmVu1ObjQword *m, uint32_t a, EmVu1ObjQword v) { m[a & 1023u] = v; }

/* The look-ahead loads of the vertex at `v` (its four qwords start there):
 * data word, rows, position, normal, TEX0. */
static inline void emvuo_fetch(const EmVu1ObjQword *m, uint32_t v, EmVu1ObjInput *in)
{
    const EmVu1ObjQword p = emvuo_ld(m, v + 3u);
    in->word = p.w[3] & 0xFFFFu;
    memcpy(in->pos, p.w, sizeof in->pos);
    memcpy(in->normal, emvuo_ld(m, v + 2u).w, sizeof in->normal);
    in->tex0 = emvuo_ld(m, v);
    for (unsigned r = 0; r < 4; ++r) memcpy(in->m[r], emvuo_ld(m, in->word + r).w, 16);
    for (unsigned r = 0; r < 3; ++r) memcpy(in->l[r], emvuo_ld(m, in->word + 4u + r).w, 16);
}

/* One batch from the setup at micro 0x00D over `dmem` (1024 qwords, read and
 * written exactly as the kernel does). On a fault the batch stops where the
 * fault arose and dmem may be partly written: fail-stop, the caller drops
 * the unit. */
static inline int emvuo_batch(EmVu1ObjState *s, EmVu1ObjQword *dmem, uint32_t top,
                              EmVu1ObjBatch *out)
{
    memset(out, 0, sizeof *out);
    top &= 1023u;
    out->top = top;
    out->kick = (top + EM_VU1_OBJ_KICK) & 1023u;
    EmVu1ObjInput cur, next;
    emvuo_fetch(dmem, top, &cur);                     /* 0x00D..0x01A */
    EmVu1ObjQword prev[4] = { s->tex0, s->st, s->rgbaq, emvuo_ld(dmem, 1020u) };
    uint32_t hist = 0u;
    for (uint32_t i = 0; i < EM_VU1_OBJ_VERTICES; ++i) {
        const uint32_t v = top + 4u * i;
        for (unsigned k = 0; k < 4; ++k)             /* 0x01B..0x01E */
            emvuo_st(dmem, v + 0x81u + k, prev[k]);
        memcpy(cur.st_in, emvuo_ld(dmem, v + 1u).w, sizeof cur.st_in);   /* 0x029 */
        emvuo_fetch(dmem, v + 4u, &next);             /* look-ahead */
        EmVu1ObjQword o[4];
        const uint32_t sat = out->ftoi_saturated;
        if (em_vu1_object_vertex(s, &cur, &hist, prev[1].w[3], o, &out->why[i],
                                 &out->clip[i], &out->ftoi_saturated)) {
            out->fault = EM_VU1_OBJ_FAULT_OPERAND;
            out->fault_vertex = i;
            return -1;
        }
        if (out->ftoi_saturated != sat) out->saturated |= 1u << i;
        memcpy(prev, o, sizeof prev);
        cur = next;
    }
    /* 0x036..0x039: vertex 31's qwords (RGBAQ, XYZF2, TEX0, ST order) */
    const uint32_t v = top + 0x80u;
    emvuo_st(dmem, v + 0x83u, prev[2]);
    emvuo_st(dmem, v + 0x84u, prev[3]);
    emvuo_st(dmem, v + 0x81u, prev[0]);
    emvuo_st(dmem, v + 0x82u, prev[1]);
    s->tex0 = prev[0];
    s->st = prev[1];
    s->rgbaq = prev[2];
    return 0;                                         /* 0x03A: XGKICK */
}

/* MSCAL 0: micro 0x000..0x00C, then the batch. The carried TEX0/ST/RGBAQ in
 * `s` must hold what the VU1 registers held (anything, for a backend that
 * only draws the packet: they never reach a drawn GS field). */
static inline int em_vu1_object_kernel_mscal(EmVu1ObjState *s, EmVu1ObjQword *dmem,
                                             uint32_t top, EmVu1ObjBatch *out)
{
    if (!s || !dmem || !out) {
        if (out) { memset(out, 0, sizeof *out); out->fault = EM_VU1_OBJ_FAULT_ARGS; }
        return -1;
    }
    memcpy(s->guard_scale, dmem[1022].w, 16);
    memcpy(s->guard_offset, dmem[1023].w, 16);
    memcpy(s->fog, dmem[1021].w, 16);
    for (unsigned r = 0; r < 4; ++r) memcpy(s->colour[r], dmem[1013u + r].w, 16);
    s->cap = em_eei_vu_add_raw(0u, EM_VU1_OBJ_COLOUR_CAP);
    s->loaded = 1u;
    return emvuo_batch(s, dmem, top, out);
}

/* MSCNT: micro 0x03C..0x03D then the batch, with the MSCAL registers. */
static inline int em_vu1_object_kernel_mscnt(EmVu1ObjState *s, EmVu1ObjQword *dmem,
                                             uint32_t top, EmVu1ObjBatch *out)
{
    if (!s || !dmem || !out) {
        if (out) { memset(out, 0, sizeof *out); out->fault = EM_VU1_OBJ_FAULT_ARGS; }
        return -1;
    }
    if (!s->loaded) {
        memset(out, 0, sizeof *out);
        out->fault = EM_VU1_OBJ_FAULT_STATE;
        return -1;
    }
    return emvuo_batch(s, dmem, top, out);
}

/* TOP of block `block` (0 = the MSCAL block) of one unit: the kernel
 * packet's OFFSET code resets the VIF double buffer to BASE at every CALL,
 * and every MSCAL/MSCNT flips it, so the blocks alternate BASE and
 * BASE + OFFSET (every captured batch; the reference test checks it). The
 * block's 128 vertex qwords are the model block's qwords 1..128 (qword 0
 * holds its STCYCL/UNPACK codes, qword 129 its MSCAL/MSCNT). */
static inline uint32_t em_vu1_object_kernel_top(uint32_t block)
{
    return EM_VU1_OBJ_BASE + ((block & 1u) ? EM_VU1_OBJ_OFFSET : 0u);
}

/* ------------------------------------------------------- GS-side decode */

/* One kicked vertex as the GS takes it. The kernel always stores the same
 * four qwords; the template tag (dmem 1020) decides which GS register each
 * one writes. The captures carry three templates:
 *   PRIM 0x03C, REGS TEX0_1 ST RGBAQ XYZF2  opaque textured (owners, player,
 *                                           Roger, equipment, items)
 *   PRIM 0x07C, same REGS                   the same with ABE 1 (pickup
 *                                           light, 001C5760)
 *   PRIM 0x004, REGS NOP NOP NOP XYZ2       the drop-shadow silhouette
 *                                           (docs/SHADOW_ORIGINAL.md)
 * Every template is PACKED, NREG 4, PRE 1, EOP, NLOOP 32. */
typedef struct {
    uint64_t tex0;            /* TEX0_1: the qword's low 64 bits         */
    uint32_t s, t, q;         /* ST: S, T and Q as binary32 bit patterns */
    uint8_t r, g, b, a;       /* RGBAQ: each lane's low byte             */
    uint16_t x, y;            /* XYZF2/XYZ2: 12.4 window coordinates     */
    uint32_t z;               /* XYZF2: 24 bits; XYZ2: 32 bits           */
    uint8_t f;                /* XYZF2 fog (0 for XYZ2)                  */
    uint8_t adc;              /* 1: the vertex is queued but draws nothing */
} EmVu1ObjGsVertex;

#define EM_VU1_OBJ_GS_TEX0 1u     /* slot 0 writes TEX0_1  (else NOP)      */
#define EM_VU1_OBJ_GS_ST 2u       /* slot 1 writes ST      (else NOP)      */
#define EM_VU1_OBJ_GS_RGBAQ 4u    /* slot 2 writes RGBAQ   (else NOP)      */
#define EM_VU1_OBJ_GS_XYZF2 8u    /* slot 3 writes XYZF2   (else XYZ2)     */

/* Decodes the packet an XGKICK at `kick` sends. Accepts a PACKED tag with
 * EOP, NREG 4, NLOOP <= 32 whose registers are, slot by slot, TEX0_1 or
 * NOP, ST or NOP, RGBAQ or NOP, XYZF2 or XYZ2; returns the vertex count and
 * fills *prim (the tag's PRIM when PRE is set, else ~0u) and *regs
 * (EM_VU1_OBJ_GS_* of the slots written). Any other tag: -1 (the renderer
 * must not guess). */
static inline int em_vu1_object_kernel_decode(const EmVu1ObjQword *dmem, uint32_t kick,
                                              EmVu1ObjGsVertex out[EM_VU1_OBJ_VERTICES],
                                              uint32_t *prim, uint32_t *regs)
{
    if (!dmem || !out) return -1;
    const EmVu1ObjQword t = dmem[kick & 1023u];
    const uint64_t lo = (uint64_t)t.w[0] | (uint64_t)t.w[1] << 32;
    const uint32_t r = t.w[2] & 0xFFFFu;
    const uint32_t nloop = (uint32_t)(lo & 0x7FFFu);
    const uint32_t r0 = r & 15u, r1 = r >> 4 & 15u, r2 = r >> 8 & 15u, r3 = r >> 12 & 15u;
    if (!((lo >> 15) & 1u) || ((lo >> 58) & 3u) != 0u || ((lo >> 60) & 15u) != 4u ||
        nloop > EM_VU1_OBJ_VERTICES || (r0 != 0x6u && r0 != 0xFu) || (r1 != 0x2u && r1 != 0xFu) ||
        (r2 != 0x1u && r2 != 0xFu) || (r3 != 0x4u && r3 != 0x5u))
        return -1;
    const uint32_t have = (r0 == 0x6u ? EM_VU1_OBJ_GS_TEX0 : 0u) | (r1 == 0x2u ? EM_VU1_OBJ_GS_ST : 0u) |
                          (r2 == 0x1u ? EM_VU1_OBJ_GS_RGBAQ : 0u) | (r3 == 0x4u ? EM_VU1_OBJ_GS_XYZF2 : 0u);
    if (prim) *prim = ((lo >> 46) & 1u) ? (uint32_t)((lo >> 47) & 0x7FFu) : ~0u;
    if (regs) *regs = have;
    for (uint32_t i = 0; i < nloop; ++i) {
        const uint32_t a = kick + 1u + 4u * i;
        const EmVu1ObjQword q0 = dmem[a & 1023u], q1 = dmem[(a + 1u) & 1023u];
        const EmVu1ObjQword q2 = dmem[(a + 2u) & 1023u], q3 = dmem[(a + 3u) & 1023u];
        EmVu1ObjGsVertex *v = &out[i];
        memset(v, 0, sizeof *v);
        if (have & EM_VU1_OBJ_GS_TEX0) v->tex0 = (uint64_t)q0.w[0] | (uint64_t)q0.w[1] << 32;
        if (have & EM_VU1_OBJ_GS_ST) { v->s = q1.w[0]; v->t = q1.w[1]; v->q = q1.w[2]; }
        if (have & EM_VU1_OBJ_GS_RGBAQ) {
            v->r = (uint8_t)q2.w[0]; v->g = (uint8_t)q2.w[1];
            v->b = (uint8_t)q2.w[2]; v->a = (uint8_t)q2.w[3];
        }
        v->x = (uint16_t)q3.w[0]; v->y = (uint16_t)q3.w[1];
        if (have & EM_VU1_OBJ_GS_XYZF2) {
            v->z = (q3.w[2] >> 4) & 0xFFFFFFu;
            v->f = (uint8_t)(q3.w[3] >> 4);
        } else {
            v->z = q3.w[2];
        }
        v->adc = (uint8_t)((q3.w[3] >> 15) & 1u);
    }
    return (int)nloop;
}

/* The triangles one decoded packet draws when its PRIM is a triangle strip
 * (PRIM & 7 == 4, every captured template): vertex i >= 2 without ADC
 * draws (i-2, i-1, i), with the TEX0 of vertex i (the register in force at
 * the drawing kick; in every capture the three TEX0 of a drawn triangle
 * differ at most in CLD, bit 61, so the texture is the strip's). PRE
 * writes PRIM, which empties the GS vertex queue, so no triangle spans two
 * packets. The kernel never culls: both windings draw. Returns the count
 * written to `last` (the index i of each triangle), or ~0u for any other
 * PRIM. */
static inline uint32_t em_vu1_object_kernel_triangles(const EmVu1ObjGsVertex *v, int count,
                                                      uint32_t prim,
                                                      uint8_t last[EM_VU1_OBJ_VERTICES])
{
    if ((prim & 7u) != 4u) return ~0u;
    uint32_t n = 0;
    for (int i = 2; i < count; ++i)
        if (!v[i].adc) last[n++] = (uint8_t)i;
    return n;
}

#endif /* EM_VU1_OBJECT_KERNEL_H */
