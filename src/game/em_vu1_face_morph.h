/* em_vu1_face_morph.h — the VU1 face morph program that draws Roger's and
 * Dennis's faces (DMA CALL 0x0023C480), translated from its VU1 microcode
 * into CPU-exact C.
 *
 * The packet at 0x0023C480 is one DMA CNT whose VIF codes are, in order,
 * FLUSHA, STCYCL 4,4, STMASK 0, STMOD 0, BASE 0x20, OFFSET 0x1E5 and one
 * MPG of 80 instructions (ELF 0x0023C4B0) to micro address 0; a RET
 * follows (the reference test asserts this exact sequence). 001D3E40
 * appends the CALL, reached as 001CAA00 -> 001CB3C0 -> 001D3F50 -> 001D3E40
 * (docs/VU1_FACE_MORPH.md names every upload of a face unit). The face
 * resource's blocks follow: each block UNPACKs 352 qwords (32 vertices x 11
 * qwords, as 256 + 96) to TOPS and ends in MSCAL 0 (the first block) or
 * MSCNT (the others).
 *
 * Micro addresses below are instruction indices of that program (8 bytes
 * each, micro 0x000 = ELF 0x0023C4B0).
 *
 * Data memory the program reads (qword addresses):
 *   TOP + 11i + 0     vertex i (i = 0..31): the TEX0 qword
 *   TOP + 11i + 1     (s, t, 1, 0)
 *   TOP + 11i + 2     the normal (x, y, z, unused)
 *   TOP + 11i + 3     the base position (x, y, z) and the data word (w)
 *   TOP + 11i + 4..10 the seven position deltas (x, y, z; w unused)
 *   word .. word + 3  the vertex's position matrix M (rows), and
 *   word + 4 .. + 6   its lighting matrix L (rows), where word = the low
 *                     16 bits of the data word (bit 15 = "no kick"),
 *                     addresses modulo 1024
 *   1011              the morph weights w0..w3 (x, y, z, w lanes)
 *   1012              the morph weights w4..w6 (x, y, z; w unused)
 *   1013 .. 1016      the colour matrix B (three light colours + ambient)
 *   1020              the GIF tag template
 *   1021              the fog row (255, 2048, A, B)
 *   1022 / 1023       the guard-band scale / offset rows
 * Every batch (MSCAL or MSCNT) reads all of 1011..1016 and 1020..1023 anew.
 *
 * Per vertex (every value is a VU lane operation, see "Arithmetic"):
 *   p     = (((((((D0 * w0 + D1 * w1) + D2 * w2) + D3 * w3) + D4 * w4)
 *             + D5 * w5) + D6 * w6) + base * 1), lanes x, y, z only
 *                                                             (0x019..0x020)
 *   c     = ((M0 * p.x + M1 * p.y) + M2 * p.z) + M3 * 1       (0x021..0x024)
 *   light = (L0 * n.x + L1 * n.y) + L2 * n.z                  (0x025..0x027)
 *           (the program also forms lane w; it never reaches an output)
 *   Q     = 1 / c.w                                            (0x028)
 *   g     = G0 * c + G1 * c.w (G0 = dmem 1022, G1 = dmem 1023) (0x028..0x029)
 *   fog   = 1 * A + B * c.w                                    (0x02A, 0x02B)
 *   light = max(light, 0)                                      (0x02C)
 *   CLIP  of g.xyz against |g.w| into a 24-bit history         (0x02D)
 *   scr   = c.xyz * Q                                          (0x02F)
 *   fog   = min(fog, the row's x lane)                         (0x030)
 *   ADC   = data word bit 15, or any CLIP bit of vertices i-2..i
 *           (history AND 0x03FFFF)                             (0x030..0x032)
 *           The window rests on the reference interpreter's CLIP flag
 *           latency (4 cycles, exactly the distance from 0x02D to 0x031),
 *           an interpreter ASSUMPTION like the float rules below.
 *   colour= ((B0 * light.x + B1 * light.y) + B2 * light.z) + B3 * 1
 *                                                             (0x031..0x035)
 *   fog   = max(fog, 0)                                        (0x034)
 *   fog  += the row's y lane when ADC                          (0x038; the
 *           branch at 0x036 skips it otherwise)
 *   ST    = (s * Q, t * Q, 1 * Q, w carried)                   (0x039)
 *   RGBAQ = min(colour, 8388863.0) in all four lanes           (0x03B)
 *   XYZF2 = ftoi4 of (scr.x, scr.y, scr.z, fog)                (0x03C)
 * The multiply at 0x02F reads Q exactly 7 cycles after the division at
 * 0x028 issues (the reference interpreter's Q latency; the program's own
 * spacing, a no-op at 0x02E, suggests it was scheduled for it): an
 * interpreter ASSUMPTION too. With one cycle more it would read the
 * previous vertex's Q. docs/VU1_FACE_MORPH.md section 4.
 * The normal is not morphed; only the position is.
 *
 * Output: vertex i's TEX0, ST, RGBAQ and XYZF2 qwords at TOP + 0x164 + 4i;
 * TOP + 0x163 receives the template (dmem 1020, read after the last
 * vertex's stores) and the program kicks the packet at TOP + 0x163
 * (micro 0x04C): the template and 128 qwords.
 *
 * The loop (micro 0x019..0x03C) is software-pipelined. Iteration i
 *   1. reads vertex i's deltas 4..6 and its seven matrix rows;
 *   2. stores vertex i-1's four qwords at TOP + 0x160 + 4i .. + 0x163 + 4i
 *      (RGBAQ, XYZF2, ST, TEX0 in that order);
 *   3. reads vertex i+1's base, deltas 0..3, normal and data word, and
 *      vertex i's ST input and TEX0.
 * So iteration 0 stores the four registers left by whatever ran before
 * (the carried TEX0, ST, RGBAQ and XYZF2) at TOP + 0x160..0x163 (the last
 * is then overwritten by the template), and the ST output's w lane is never
 * written by this program: it is the carried register's. EmVu1FaceState
 * carries all four; they reach the kicked packet only in that w lane, which
 * the GS ignores (PACKED ST uses S, T and Q). The last iteration's
 * look-ahead reads (TOP + 0x160..0x167) are dead; this translation performs
 * them and discards them.
 *
 * Entry points: MSCAL 0 runs micro 0x000 (the constants, the colour cap,
 * clip flags cleared) into the loop. MSCNT resumes at micro 0x04E, which
 * branches back to 0x000: an MSCNT batch is the same program from the
 * start, with only the carried registers differing. The program never
 * culls.
 *
 * Arithmetic. VU1 was never measured (docs/EE_FLOAT_MODEL.md); like
 * em_vu1_object_kernel.h this header ASSUMES the VU0 lane rules of
 * em_ee_float.h: DAZ operands, exact results truncated toward zero, FTZ,
 * finite overflow to +-FLT_MAX, a multiply-add being a truncated product
 * added to ACC, VDIV (3,3) for Q, the raw sign-magnitude MAX/MINI order
 * (-0 below +0) and the saturating VFTOI4. An exponent-255 word that
 * reaches a multiply or add on a live lane faults the batch
 * (EM_VU1_FACE_FAULT_OPERAND); the same word in a dead lane does not. The
 * lane helpers are the object kernel's (emvuo_*), so every translated VU1
 * program shares one float model.
 *
 * The kicked packet has the object kernel's layout (template + TEX0, ST,
 * RGBAQ, XYZF2 per vertex): em_vu1_object_kernel_decode and
 * em_vu1_object_kernel_triangles decode it (every captured face template is
 * PRE, PRIM 0x03C, PACKED, NREG 4, NLOOP 32, EOP; the reference test checks
 * the decode on every compared packet).
 *
 * Header-only (static inline), pure C, no host float arithmetic. */
#ifndef EM_VU1_FACE_MORPH_H
#define EM_VU1_FACE_MORPH_H

#include <stdint.h>
#include <string.h>

#include "game/em_ee_float.h"
#include "game/em_vu1_object_kernel.h"

#define EM_VU1_FACE_VERTICES 32u
#define EM_VU1_FACE_VERTEX_QWORDS 11u  /* TEX0, ST, normal, base, 7 deltas */
#define EM_VU1_FACE_DELTAS 7u
#define EM_VU1_FACE_BASE 0x20u         /* the packet's VIF BASE           */
#define EM_VU1_FACE_OFFSET 0x1E5u      /* the packet's VIF OFFSET         */
#define EM_VU1_FACE_STORE 0x160u       /* iteration 0's store slot - TOP  */
#define EM_VU1_FACE_KICK 0x163u        /* XGKICK address - TOP            */
#define EM_VU1_FACE_WEIGHTS 1011u      /* dmem 1011..1012                 */

enum {
    EM_VU1_FACE_OK = 0,
    EM_VU1_FACE_FAULT_ARGS = 1,     /* NULL argument                        */
    EM_VU1_FACE_FAULT_OPERAND = 2,  /* an exponent-255 word reached a live
                                       multiply/add: not established      */
    EM_VU1_FACE_FAULT_STATE = 3     /* MSCNT without an earlier batch of
                                       this program (nothing to resume)   */
};

/* The registers that live across batches. */
typedef struct {
    uint32_t loaded;          /* a batch of this program has run         */
    /* Carried: vertex 31's outputs of the previous batch, or what the
     * previous program left in those registers before an MSCAL. */
    EmVu1ObjQword tex0, st, rgbaq, xyzf;
} EmVu1FaceState;

/* Per batch: the object kernel's record (fault, fault_vertex, top, kick,
 * why[] = EM_VU1_OBJ_ADC_* per vertex, clip[] = the CLIP judgement,
 * ftoi_saturated, saturated). */
typedef EmVu1ObjBatch EmVu1FaceBatch;

/* The inputs one vertex's arithmetic reads, as the program loaded them. */
typedef struct {
    uint32_t word;            /* data word, low 16 bits                  */
    uint32_t base[3];         /* qword 3 x, y, z                         */
    uint32_t delta[EM_VU1_FACE_DELTAS][3];   /* qwords 4..10 x, y, z     */
    uint32_t normal[3];       /* qword 2 x, y, z                         */
    uint32_t m[4][4];         /* rows word + 0..3                        */
    uint32_t l[3][3];         /* rows word + 4..6, lanes x, y, z         */
    uint32_t st_in[3];        /* qword 1 x, y, z                         */
    EmVu1ObjQword tex0;       /* qword 0                                 */
} EmVu1FaceInput;

/* The per-batch constants (micro 0x007..0x011). */
typedef struct {
    uint32_t weight[EM_VU1_FACE_DELTAS];  /* 1011.xyzw, 1012.xyz         */
    uint32_t colour[4][4];    /* 1013..1016                              */
    uint32_t fog[4];          /* 1021                                     */
    uint32_t guard_scale[4];  /* 1022                                     */
    uint32_t guard_offset[4]; /* 1023                                     */
    uint32_t cap;             /* 0 + 8388863.0                            */
} EmVu1FaceConst;

/* ------------------------------------------------------------ one vertex */

/* The morphed position (micro 0x019..0x020), lane by lane:
 * p = (((((((D0 * w0 + D1 * w1) + D2 * w2) + D3 * w3) + D4 * w4) + D5 * w5)
 *      + D6 * w6) + base * 1), each product truncated before its add.
 * `weight` = dmem 1011.xyzw then 1012.xyz. Returns 0, or 1 when an
 * exponent-255 word reached one of these multiplies/adds. */
static inline uint32_t em_vu1_face_morph_position(const uint32_t weight[EM_VU1_FACE_DELTAS],
                                                  const uint32_t base[3],
                                                  const uint32_t delta[EM_VU1_FACE_DELTAS][3],
                                                  uint32_t p[3])
{
    uint32_t bad = 0u;
    for (unsigned x = 0; x < 3; ++x) {
        uint32_t acc = emvuo_mul(delta[0][x], weight[0], &bad);
        for (unsigned j = 1; j < EM_VU1_FACE_DELTAS; ++j)
            acc = emvuo_madd(acc, delta[j][x], weight[j], &bad);
        p[x] = emvuo_madd(acc, base[x], EM_EE_ONE, &bad);
    }
    return bad;
}

/* One vertex (micro 0x019..0x03C): out[0..3] = TEX0, ST, RGBAQ, XYZF2.
 * `hist` is the clip history (in/out), `st_w` the carried ST w lane.
 * Returns 0, or 1 when an exponent-255 word reached a live lane. */
static inline uint32_t em_vu1_face_morph_vertex(const EmVu1FaceConst *k, const EmVu1FaceInput *in,
                                                uint32_t *hist, uint32_t st_w,
                                                EmVu1ObjQword out[4], uint32_t *why,
                                                uint32_t *clip, uint32_t *saturated)
{
    uint32_t p[3], c[4], light[3], g[4], col[4], scr[3], q = 0u;
    /* 0x019..0x020: p = the weighted deltas, then the base */
    uint32_t bad = em_vu1_face_morph_position(k->weight, in->base, in->delta, p);
    /* 0x021..0x024: c = p x M */
    for (unsigned x = 0; x < 4; ++x) {
        uint32_t acc = emvuo_mul(in->m[0][x], p[0], &bad);
        acc = emvuo_madd(acc, in->m[1][x], p[1], &bad);
        acc = emvuo_madd(acc, in->m[2][x], p[2], &bad);
        c[x] = emvuo_madd(acc, in->m[3][x], EM_EE_ONE, &bad);
    }
    /* 0x025..0x027: the lighting matrix times the normal (lane w dead) */
    for (unsigned x = 0; x < 3; ++x) {
        uint32_t acc = emvuo_mul(in->l[0][x], in->normal[0], &bad);
        acc = emvuo_madd(acc, in->l[1][x], in->normal[1], &bad);
        light[x] = emvuo_madd(acc, in->l[2][x], in->normal[2], &bad);
    }
    /* 0x028: Q = 1 / c.w (VDIV, fs = 1.0); 0x028..0x029: the guard vector */
    if (!emvuo_live(c[3])) bad = 1u;
    (void)em_vu_div_bits(EM_EE_ONE, c[3], 3, 3, &q);
    for (unsigned x = 0; x < 4; ++x)
        g[x] = emvuo_madd(emvuo_mul(k->guard_scale[x], c[x], &bad), k->guard_offset[x], c[3], &bad);
    /* 0x02A, 0x02B: fog = 1 * A + B * c.w */
    uint32_t fog = emvuo_madd(emvuo_mul(EM_EE_ONE, k->fog[2], &bad), k->fog[3], c[3], &bad);
    /* 0x02C: lighting clamped at 0 */
    for (unsigned x = 0; x < 3; ++x) light[x] = em_vu_max_bits(light[x], 0u);
    /* 0x02D: CLIP */
    const uint32_t cf = emvuo_clip(g);
    *hist = ((*hist << 6) | cf) & 0xFFFFFFu;
    /* 0x02F: screen = c.xyz * Q */
    for (unsigned x = 0; x < 3; ++x) scr[x] = emvuo_mul(c[x], q, &bad);
    /* 0x030: fog capped by the row's x lane */
    fog = em_vu_min_bits(fog, k->fog[0]);
    /* 0x030..0x032: ADC */
    uint32_t w = (in->word & 0x8000u) ? EM_VU1_OBJ_ADC_DATA : 0u;
    if (*hist & 0x03FFFFu) w |= EM_VU1_OBJ_ADC_CLIP;
    /* 0x031..0x035: colour = light x B + ambient row */
    for (unsigned x = 0; x < 4; ++x) {
        uint32_t acc = emvuo_mul(k->colour[0][x], light[0], &bad);
        acc = emvuo_madd(acc, k->colour[1][x], light[1], &bad);
        acc = emvuo_madd(acc, k->colour[2][x], light[2], &bad);
        col[x] = emvuo_madd(acc, k->colour[3][x], EM_EE_ONE, &bad);
    }
    /* 0x034: fog clamped at 0; 0x038: ADC adds the row's y lane */
    fog = em_vu_max_bits(fog, 0u);
    if (w) fog = emvuo_add(fog, k->fog[1], &bad);
    /* 0x039: TEX0 passes through; ST = input xyz * Q, w carried */
    out[0] = in->tex0;
    for (unsigned x = 0; x < 3; ++x) out[1].w[x] = emvuo_mul(in->st_in[x], q, &bad);
    out[1].w[3] = st_w;
    /* 0x03B: colour cap; 0x03C: fixed point */
    for (unsigned x = 0; x < 4; ++x) out[2].w[x] = em_vu_min_bits(col[x], k->cap);
    for (unsigned x = 0; x < 3; ++x) out[3].w[x] = emvuo_ftoi4(scr[x], saturated);
    out[3].w[3] = emvuo_ftoi4(fog, saturated);
    *why = w;
    *clip = cf;
    return bad;
}

/* ---------------------------------------------------------------- batch */

/* The look-ahead loads of the vertex whose qwords start at `v`: data word,
 * base, normal, deltas 0..3 (the prologue for vertex 0, iteration i-1 for
 * vertex i). */
static inline void emvuf_fetch_ahead(const EmVu1ObjQword *m, uint32_t v, EmVu1FaceInput *in)
{
    const EmVu1ObjQword b = emvuo_ld(m, v + 3u);
    in->word = b.w[3] & 0xFFFFu;
    memcpy(in->base, b.w, sizeof in->base);
    memcpy(in->normal, emvuo_ld(m, v + 2u).w, sizeof in->normal);
    for (unsigned j = 0; j < 4; ++j) memcpy(in->delta[j], emvuo_ld(m, v + 4u + j).w, 12);
}

/* One batch from micro 0x000 over `dmem` (1024 qwords, read and written
 * exactly as the program does). On a fault the batch stops where the fault
 * arose and dmem may be partly written: fail-stop, the caller drops the
 * unit. */
static inline int emvuf_batch(EmVu1FaceState *s, EmVu1ObjQword *dmem, uint32_t top,
                              EmVu1FaceBatch *out)
{
    memset(out, 0, sizeof *out);
    top &= 1023u;
    out->top = top;
    out->kick = (top + EM_VU1_FACE_KICK) & 1023u;
    /* 0x007..0x011: the constants and the colour cap; 0x003 clears the
     * clip flags */
    EmVu1FaceConst k;
    memcpy(k.weight, dmem[EM_VU1_FACE_WEIGHTS].w, 16);
    memcpy(&k.weight[4], dmem[EM_VU1_FACE_WEIGHTS + 1u].w, 12);
    memcpy(k.guard_scale, dmem[1022].w, 16);
    memcpy(k.guard_offset, dmem[1023].w, 16);
    memcpy(k.fog, dmem[1021].w, 16);
    for (unsigned r = 0; r < 4; ++r) memcpy(k.colour[r], dmem[1013u + r].w, 16);
    k.cap = em_eei_vu_add_raw(0u, EM_VU1_OBJ_COLOUR_CAP);
    s->loaded = 1u;
    EmVu1FaceInput cur, next;
    emvuf_fetch_ahead(dmem, top, &cur);               /* 0x012..0x018 */
    EmVu1ObjQword prev[4] = { s->tex0, s->st, s->rgbaq, s->xyzf };
    uint32_t hist = 0u;
    for (uint32_t i = 0; i < EM_VU1_FACE_VERTICES; ++i) {
        const uint32_t v = top + EM_VU1_FACE_VERTEX_QWORDS * i;
        const uint32_t o = top + EM_VU1_FACE_STORE + 4u * i;
        for (unsigned j = 4; j < EM_VU1_FACE_DELTAS; ++j)       /* 0x019..0x01B */
            memcpy(cur.delta[j], emvuo_ld(dmem, v + 4u + j).w, 12);
        for (unsigned r = 0; r < 4; ++r)                        /* 0x01D..0x020 */
            memcpy(cur.m[r], emvuo_ld(dmem, cur.word + r).w, 16);
        for (unsigned r = 0; r < 3; ++r)                        /* 0x021..0x023 */
            memcpy(cur.l[r], emvuo_ld(dmem, cur.word + 4u + r).w, 12);
        emvuo_st(dmem, o + 2u, prev[2]);                        /* 0x024..0x027 */
        emvuo_st(dmem, o + 3u, prev[3]);
        emvuo_st(dmem, o + 1u, prev[1]);
        emvuo_st(dmem, o + 0u, prev[0]);
        emvuf_fetch_ahead(dmem, v + EM_VU1_FACE_VERTEX_QWORDS, &next);   /* look-ahead */
        memcpy(cur.st_in, emvuo_ld(dmem, v + 1u).w, sizeof cur.st_in);  /* 0x033 */
        cur.tex0 = emvuo_ld(dmem, v);                                   /* 0x039 */
        EmVu1ObjQword res[4];
        const uint32_t sat = out->ftoi_saturated;
        if (em_vu1_face_morph_vertex(&k, &cur, &hist, prev[1].w[3], res, &out->why[i],
                                     &out->clip[i], &out->ftoi_saturated)) {
            out->fault = EM_VU1_FACE_FAULT_OPERAND;
            out->fault_vertex = i;
            return -1;
        }
        if (out->ftoi_saturated != sat) out->saturated |= 1u << i;
        memcpy(prev, res, sizeof prev);
        cur = next;
    }
    /* 0x03D..0x040: vertex 31's qwords (RGBAQ, XYZF2, ST, TEX0 order) */
    const uint32_t o = top + EM_VU1_FACE_STORE + 4u * EM_VU1_FACE_VERTICES;
    emvuo_st(dmem, o + 2u, prev[2]);
    emvuo_st(dmem, o + 3u, prev[3]);
    emvuo_st(dmem, o + 1u, prev[1]);
    emvuo_st(dmem, o + 0u, prev[0]);
    s->tex0 = prev[0];
    s->st = prev[1];
    s->rgbaq = prev[2];
    s->xyzf = prev[3];
    /* 0x044, 0x048: the template to TOP + 0x163; 0x04C: XGKICK */
    emvuo_st(dmem, out->kick, emvuo_ld(dmem, 1020u));
    return 0;
}

/* MSCAL 0. The carried TEX0/ST/RGBAQ/XYZF2 in `s` must hold what the VU1
 * registers held (anything, for a backend that only draws the packet: they
 * never reach a drawn GS field). */
static inline int em_vu1_face_morph_mscal(EmVu1FaceState *s, EmVu1ObjQword *dmem, uint32_t top,
                                          EmVu1FaceBatch *out)
{
    if (!s || !dmem || !out) {
        if (out) { memset(out, 0, sizeof *out); out->fault = EM_VU1_FACE_FAULT_ARGS; }
        return -1;
    }
    return emvuf_batch(s, dmem, top, out);
}

/* MSCNT: micro 0x04E branches to 0x000, so the batch is the MSCAL batch
 * with the carried registers of the previous one. */
static inline int em_vu1_face_morph_mscnt(EmVu1FaceState *s, EmVu1ObjQword *dmem, uint32_t top,
                                          EmVu1FaceBatch *out)
{
    if (!s || !dmem || !out) {
        if (out) { memset(out, 0, sizeof *out); out->fault = EM_VU1_FACE_FAULT_ARGS; }
        return -1;
    }
    if (!s->loaded) {
        memset(out, 0, sizeof *out);
        out->fault = EM_VU1_FACE_FAULT_STATE;
        return -1;
    }
    return emvuf_batch(s, dmem, top, out);
}

/* TOP of block `block` (0 = the MSCAL block) of one face unit: the
 * packet's OFFSET code resets the VIF double buffer to BASE at every CALL
 * and every MSCAL/MSCNT flips it, so the blocks alternate BASE and
 * BASE + OFFSET (every captured batch; the reference test checks it). */
static inline uint32_t em_vu1_face_morph_top(uint32_t block)
{
    return EM_VU1_FACE_BASE + ((block & 1u) ? EM_VU1_FACE_OFFSET : 0u);
}

#endif /* EM_VU1_FACE_MORPH_H */
