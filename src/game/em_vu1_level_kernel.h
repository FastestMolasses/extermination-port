/* em_vu1_level_kernel.h — the VU1 level kernel that the static world's
 * channel-0 run CALLs (DMA CALL 0x00237180), translated from its VU1
 * microcode into CPU-exact C. The drop shadow's box pass (001DA310) CALLs
 * the same program; its position and cull path is em_shadow_gs_level_batch
 * (src/gfx/metal/em_shadow_gs.h), which is this translation's adapter.
 *
 * The kernel packet at 0x00237180 is one DMA CNT whose VIF codes are, in
 * order, NOP, NOP, FLUSHA, STCYCL 4,4, STMASK 0, STMOD 0, BASE 0x190,
 * OFFSET 0x109 and one MPG of 79 instructions (ELF 0x002371B0) to micro
 * address 0; a RET follows (tools/test_static_world_draw_reference.py
 * asserts this exact sequence). The static objects' blocks follow the
 * CALL: each block UNPACKs 128 qwords (32 vertices x 4 qwords) to TOPS and
 * ends in MSCAL 0 (an object's first block) or MSCNT (the others).
 * docs/STATIC_WORLD.md section 7 has the evidence.
 *
 * Micro addresses below are instruction indices of that program (8 bytes
 * each, micro 0x000 = ELF 0x002371B0).
 *
 * Data memory the kernel reads (qword addresses):
 *   TOP + 4i + 0..3   vertex i (i = 0..31): TEX0 qword, (s, t, 1, 0), the
 *                     vertex colour (r, g, b, a) / 128, position (x, y, z,
 *                     data word)
 *   0 .. 3            the matrix M (001D4750 uploads the scratchpad view
 *                     projection D_70003AC0 there)
 *   4 .. 6, 1013..1016  loaded into registers the program never reads
 *   1020              the GIF tag template
 *   1021              the fog row (255, 2048, A, B)
 *   1022 / 1023       the guard-band scale / offset rows
 * Every batch, MSCAL or MSCNT, reads all of them: the program ends with a
 * branch back to micro 0x000 (micro 0x04D), so an MSCNT re-runs the whole
 * set-up (a later upload to dmem 0..3 or 1020..1023 DOES reach an MSCNT
 * batch, unlike the object kernel's).
 *
 * Per vertex (every value is a VU lane operation, see "Arithmetic"):
 *   c     = p x M: ((M0 * p.x + M1 * p.y) + M2 * p.z) + M3 * 1
 *                                            (0x01A..0x01D; 0x02B..0x02F)
 *   Q     = 1 / c.w                                   (0x021; 0x033)
 *   g     = G0 * c + G1 * c.w (G0 = dmem 1022, G1 = 1023) (0x023, 0x024;
 *           0x03B, 0x03C), then CLIP of g.xyz against |g.w| into a 24-bit
 *           history, cleared at every batch start (0x003)  (0x028; 0x041)
 *   s     = c.xyz * Q                                 (0x029; 0x03A)
 *   fog   = max(min(1 * A + B * c.w, 255), 0)  (0x021, 0x022; 0x03D, 0x03F;
 *                                              0x033; 0x037)
 *   e_i   = s_i.xy - s_{i-1}.xy                       (0x02C)
 *   cross = e_i.x * e_{i-1}.y - e_{i-1}.x * e_i.y     (0x030, 0x031)
 *   S     = the sign of cross * w_i, w_i the vertex's data word read as a
 *           float (its value is the strip's winding sign): the status S
 *           flag the product sets (0x035), read four cycles later (0x039)
 *   ADC   = data word bit 15 (0x02F)
 *         | a CLIP bit of vertices i-2..i (history AND 0x03FFFF, 0x030)
 *         | S (a back-facing triangle: the kernel culls)          (0x031,
 *           0x039, 0x03A); the branch at 0x03C skips the next step otherwise
 *   fog  += the row's y lane (2048) when ADC (0x03E), so bit 15 of the
 *           kicked F word is the GS ADC bit
 * Output qwords of vertex i at TOP + 0x85 + 4i:
 *   TEX0  = the vertex's TEX0 qword, every bit                  (0x040)
 *   ST    = (s * Q, t * Q, 1 * Q, w carried)                    (0x032)
 *   RGBAQ = the colour + 65536.0 in all four lanes; the GS takes each
 *           lane's low byte, so a colour lane of k / 128 gives k   (0x040)
 *   XYZF2 = ftoi4 of (s.x, s.y, s.z, fog)                       (0x042)
 * TOP + 0x84 receives the template tag (dmem 1020) and the kernel kicks the
 * packet at TOP + 0x84 (micro 0x04B): in the static run the template is
 * PRE, PRIM 0x03C, NREG 4, REGS TEX0 ST RGBAQ XYZF2, NLOOP 32, EOP.
 *
 * The loop is software-pipelined: iteration i first stores vertex i-1's
 * four qwords (at TOP + 4i + 0x81..0x84), computes vertex i+1's c, Q, g and
 * fog while it finishes vertex i. So iteration 0 stores the four registers
 * the previous run left (the carried RGBAQ, ST, TEX0 and XYZF2) at TOP +
 * 0x81..0x84 (0x84 is then overwritten by the template), and vertex i's
 * cull reads the previous screen point s_{i-1}, the previous edge e_{i-1}
 * and the float w_i from registers: for vertices 0 and 1 of a batch those
 * come from the previous run (s_31 and e_31 of the previous batch, and a w
 * that iteration 30 loaded from TOP + 0x83, the stored carried RGBAQ's w
 * lane). EmVu1LvlState carries all of them. The last iteration's look-ahead
 * (vertex 32's c, Q, g, fog and loads) is dead: it reaches no output and
 * no register a later batch reads before writing it, so this translation
 * performs only its loads.
 *
 * Arithmetic. As em_vu1_object_kernel.h (whose lane helpers this header
 * uses): the VU0 lane rules of em_ee_float.h are ASSUMED for VU1 (DAZ
 * operands, exact results truncated toward zero, FTZ, the multiply-add
 * being a truncated product added to ACC, the multiply-subtract a
 * truncated product subtracted from ACC, VDIV (3,3) for Q, the raw
 * sign-magnitude MAX/MINI order and the saturating VFTOI4); an
 * exponent-255 word on a live lane faults (EM_VU1_LVL_FAULT_OPERAND).
 * The status S flag is the sign bit of the truncated product, as the
 * reference interpreter sets it (a -0 result counts as negative).
 *
 * NOT established, therefore fail-stop: a vertex 0 or 1 whose ADC depends
 * on the cull while the carried registers are not the previous level
 * batch's (EM_VU1_LVL_FAULT_STALE): what another program left in vf09,
 * vf15 and vf16 is not modelled. No captured or exported block has one
 * (both vertices carry data bit 15 in every block of the AREA11 bank).
 *
 * Header-only (static inline), pure C, no host float arithmetic: the
 * static-world walker (em_static_world_draw.c), the shadow's box pass and
 * the reference test share one translation. */
#ifndef EM_VU1_LEVEL_KERNEL_H
#define EM_VU1_LEVEL_KERNEL_H

#include <stdint.h>
#include <string.h>

#include "game/em_vu1_object_kernel.h"
#include "game/em_vu_host_lanes.h"

#if defined(__GNUC__) || defined(__clang__)
#define EMVUL_ALWAYS_INLINE static inline __attribute__((always_inline))
#else
#define EMVUL_ALWAYS_INLINE static inline
#endif
#define EMVUL_NOINLINE static inline EM_VU_HOST_NOINLINE

#define EM_VU1_LVL_KERNEL 0x00237180u     /* the static run's CALL target  */
#define EM_VU1_LVL_BASE 0x190u            /* the kernel packet's VIF BASE   */
#define EM_VU1_LVL_OFFSET 0x109u          /* the kernel packet's VIF OFFSET */
#define EM_VU1_LVL_KICK 0x84u             /* XGKICK address - TOP           */
#define EM_VU1_LVL_VERTICES 32u
#define EM_VU1_LVL_RGBA_BIAS 0x47800000u  /* 65536.0, the I literal (0x00D) */

enum {
    EM_VU1_LVL_OK = 0,
    EM_VU1_LVL_FAULT_ARGS = 1,     /* NULL argument                          */
    EM_VU1_LVL_FAULT_OPERAND = 2,  /* an exponent-255 word reached a live
                                      multiply/add: not established        */
    EM_VU1_LVL_FAULT_STALE = 3     /* vertex 0/1's cull needs registers
                                      another program left: not modelled   */
};

/* ADC reasons of one kicked vertex. */
#define EM_VU1_LVL_ADC_DATA 1u    /* data word bit 15                       */
#define EM_VU1_LVL_ADC_CLIP 2u    /* a CLIP bit of vertex i-2, i-1 or i     */
#define EM_VU1_LVL_ADC_CULL 4u    /* the cull's S flag                      */

/* The registers that live across batches (see the header comment). */
typedef struct {
    uint32_t known;           /* e_prev / s_prev are a level batch's      */
    uint32_t known_rgbaq;     /* rgbaq is a level batch's                  */
    uint32_t known_w;         /* w_prev came from a known rgbaq            */
    uint32_t e_prev[2];       /* vf15.xy: e_31 of the previous batch       */
    uint32_t s_prev[2];       /* vf16.xy: s_31 of the previous batch       */
    uint32_t w_prev;          /* vf09.w                                    */
    /* vf14, vf02, vf27, vf07: vertex 31's RGBAQ, ST, TEX0 and XYZF2 of the
     * previous batch (or what the previous program left); iteration 0
     * stores them at TOP + 0x81..0x84. vf02's w lane is also the ST w lane
     * of every output vertex (mulq writes only x, y, z). */
    EmVu1ObjQword rgbaq, st, tex0, xyzf;
} EmVu1LvlState;

typedef struct {
    uint32_t fault;           /* EM_VU1_LVL_FAULT_*                        */
    uint32_t fault_vertex;    /* the vertex being computed at the fault    */
    uint32_t top;             /* TOP of the batch                           */
    uint32_t kick;            /* XGKICK dmem address (TOP + 0x84) & 1023   */
    uint32_t why[EM_VU1_LVL_VERTICES];   /* EM_VU1_LVL_ADC_* per vertex    */
    uint32_t clip[EM_VU1_LVL_VERTICES];  /* +x -x +y -y +z -z = bits 0..5  */
    uint32_t ftoi_saturated;  /* VFTOI4 lanes outside int32 (saturated by
                                 the VU0 rule, assumed for VU1)            */
    uint32_t saturated;       /* bit i: vertex i has such a lane           */
} EmVu1LvlBatch;

/* A fresh state: nothing carried is known (the first batch after another
 * program). */
static inline void em_vu1_level_kernel_reset(EmVu1LvlState *s)
{
    if (s) memset(s, 0, sizeof *s);
}

/* What another program did to the carried registers: none is known any
 * more (the walker calls this after a clip-kernel batch). */
static inline void em_vu1_level_kernel_forget(EmVu1LvlState *s)
{
    if (!s) return;
    s->known = s->known_rgbaq = s->known_w = 0u;
}

/* The lane operations. `host` selects the arithmetic: 0 is em_ee_float.h's
 * integer model, 1 the host FPU in the em_vu_host_enter environment
 * (em_vu_host_lanes.h: equal for every finite operand pair). Every operand
 * is checked exactly as the model's path checks it: an exponent-255 word on
 * a live lane sets *bad, whichever arithmetic runs. */
EMVUL_ALWAYS_INLINE uint32_t emvul_mul(const int host, uint32_t a, uint32_t b, uint32_t *bad)
{
    if (!emvuo_live(a) || !emvuo_live(b)) *bad = 1u;
    return host ? emvuh_mul(a, b) : em_eei_vu_mul_raw(a, b);
}

EMVUL_ALWAYS_INLINE uint32_t emvul_add(const int host, uint32_t a, uint32_t b, uint32_t *bad)
{
    if (!emvuo_live(a) || !emvuo_live(b)) *bad = 1u;
    return host ? emvuh_add(a, b) : em_eei_vu_add_raw(a, b);
}

/* ACC + fs * ft, the product truncated first. */
EMVUL_ALWAYS_INLINE uint32_t emvul_madd(const int host, uint32_t acc, uint32_t fs, uint32_t ft, uint32_t *bad)
{
    return emvul_add(host, acc, emvul_mul(host, fs, ft, bad), bad);
}

/* a - b; the caller checks the operands. */
EMVUL_ALWAYS_INLINE uint32_t emvul_sub(const int host, uint32_t a, uint32_t b)
{
    return host ? emvuh_sub(a, b) : em_eei_vu_sub_raw(a, b);
}

/* VDIV (3, 3): Q = 1 / b; the caller checks b. */
EMVUL_ALWAYS_INLINE uint32_t emvul_rcp(const int host, uint32_t b)
{
    uint32_t q = 0u;
    if (host) return emvuh_rcp(b);
    (void)em_vu_div_bits(EM_EE_ONE, b, 3, 3, &q);
    return q;
}

/* c = p x M: ((M0 * x + M1 * y) + M2 * z) + M3 * 1, lane by lane. */
EMVUL_ALWAYS_INLINE void emvul_xform(const int host, const uint32_t m[4][4], const uint32_t p[3], uint32_t c[4],
                                     uint32_t *bad)
{
    for (unsigned k = 0; k < 4; ++k) {
        uint32_t acc = emvul_mul(host, m[0][k], p[0], bad);
        acc = emvul_madd(host, acc, m[1][k], p[1], bad);
        acc = emvul_madd(host, acc, m[2][k], p[2], bad);
        c[k] = emvul_madd(host, acc, m[3][k], EM_EE_ONE, bad);
    }
}

/* ACC - fs * ft, the product truncated first. */
EMVUL_ALWAYS_INLINE uint32_t emvul_msub(const int host, uint32_t acc, uint32_t fs, uint32_t ft, uint32_t *bad)
{
    const uint32_t p = emvul_mul(host, fs, ft, bad);
    if (!emvuo_live(acc)) *bad = 1u;
    return emvul_sub(host, acc, p);
}

/* The per-vertex values the pipeline computes one iteration ahead. */
typedef struct {
    uint32_t c[4];            /* vf04 */
    uint32_t q;               /* Q    */
    uint32_t g[4];            /* vf12 */
    uint32_t s[3];            /* vf13 */
    uint32_t fog;             /* vf08.w before the clamps */
    uint32_t cf;              /* the CLIP judgement of g */
} EmVu1LvlAhead;

EMVUL_ALWAYS_INLINE void emvul_ahead(const int host, const uint32_t m[4][4], const uint32_t fog_row[4],
                               const uint32_t gs[4], const uint32_t go[4],
                               const uint32_t pos[3], EmVu1LvlAhead *a, uint32_t *bad)
{
    emvul_xform(host, m, pos, a->c, bad);
    if (!emvuo_live(a->c[3])) *bad = 1u;
    a->q = emvul_rcp(host, a->c[3]);
    a->fog = emvul_madd(host, emvul_mul(host, EM_EE_ONE, fog_row[2], bad), fog_row[3], a->c[3], bad);
    for (unsigned k = 0; k < 4; ++k)
        a->g[k] = emvul_madd(host, emvul_mul(host, gs[k], a->c[k], bad), go[k], a->c[3], bad);
    a->cf = emvuo_clip(a->g);
    for (unsigned k = 0; k < 3; ++k) a->s[k] = emvul_mul(host, a->c[k], a->q, bad);
}

/* One batch at TOP over `dmem` (1024 qwords, read and written exactly as
 * the kernel does): micro 0x000..0x04B. MSCAL and MSCNT both enter here
 * (micro 0x04D branches back to 0x000). On a fault the batch stops where
 * the fault arose and dmem may be partly written: fail-stop, the caller
 * drops the run. */
EMVUL_ALWAYS_INLINE int emvul_batch(const int host, EmVu1LvlState *s, EmVu1ObjQword *dmem, uint32_t top,
                                    EmVu1LvlBatch *out, uint32_t vertices, uint32_t kick_offset)
{
    if (!s || !dmem || !out) {
        if (out) { memset(out, 0, sizeof *out); out->fault = EM_VU1_LVL_FAULT_ARGS; }
        return -1;
    }
    memset(out, 0, sizeof *out);
    top &= 1023u;
    out->top = top;
    out->kick = (top + kick_offset) & 1023u;
    uint32_t bad = 0u;
    /* 0x006..0x00C, 0x010..0x013: the rows (dmem 4..6 and 1013..1016 are
     * loaded into registers the program never reads) */
    uint32_t gs[4], go[4], fog_row[4], m[4][4];
    memcpy(gs, emvuo_ld(dmem, 1022u).w, 16);
    memcpy(go, emvuo_ld(dmem, 1023u).w, 16);
    memcpy(fog_row, emvuo_ld(dmem, 1021u).w, 16);
    for (unsigned r = 0; r < 4; ++r) memcpy(m[r], emvuo_ld(dmem, r).w, 16);
    /* 0x00D, 0x00E: vf09.y = 0 + 65536.0 (ADDi) */
    const uint32_t bias = em_eei_vu_add_raw(0u, EM_VU1_LVL_RGBA_BIAS);
    /* 0x017..0x029: vertex 0 */
    uint32_t word = emvuo_ld(dmem, top + 3u).w[3] & 0xFFFFu;            /* vi10 */
    EmVu1ObjQword pos = emvuo_ld(dmem, top + 3u);                        /* vf03 */
    EmVu1ObjQword st_in = emvuo_ld(dmem, top + 1u);                      /* vf01 */
    EmVu1LvlAhead cur;
    emvul_ahead(host, m, fog_row, gs, go, pos.w, &cur, &bad);
    uint32_t hist = cur.cf;                        /* 0x003 cleared it, 0x028 */
    out->clip[0] = cur.cf;
    pos = emvuo_ld(dmem, top + 7u);                                      /* 0x02A */
    /* carried registers */
    EmVu1ObjQword o_rgbaq = s->rgbaq, o_st = s->st, o_tex0 = s->tex0, o_xyzf = s->xyzf;
    uint32_t e_prev[2] = { s->e_prev[0], s->e_prev[1] };
    uint32_t s_prev[2] = { s->s_prev[0], s->s_prev[1] };
    uint32_t w_f = s->w_prev;
    /* vertex 0's cull reads the carried s_31, e_31 and w; vertex 1's the
     * carried s_31 (through e_0) */
    const uint32_t known_es = s->known, known_w = s->known_w;
    const uint32_t known_rgbaq_at_start = s->known_rgbaq;
    uint32_t w_next_known = 0u;
    for (uint32_t i = 0; i < vertices; ++i) {
        const uint32_t v = top + 4u * i;
        /* 0x02B..0x02E: vertex i-1's outputs (RGBAQ, ST, TEX0, XYZF2) */
        emvuo_st(dmem, v + kick_offset - 1u, o_rgbaq);
        emvuo_st(dmem, v + kick_offset - 2u, o_st);
        emvuo_st(dmem, v + kick_offset - 3u, o_tex0);
        emvuo_st(dmem, v + kick_offset, o_xyzf);
        const int last = i + 1u == vertices;
        /* 0x02B..0x02F: vertex i+1's c (dead in the last iteration) */
        EmVu1LvlAhead next;
        memset(&next, 0, sizeof next);
        if (!last) {
            emvul_xform(host, m, pos.w, next.c, &bad);
        }
        /* 0x02C: e_i = s_i - s_{i-1} (x, y) */
        uint32_t e[2];
        for (unsigned k = 0; k < 2; ++k) {
            if (!emvuo_live(cur.s[k]) || !emvuo_live(s_prev[k])) bad = 1u;
            e[k] = emvul_sub(host, cur.s[k], s_prev[k]);
        }
        /* 0x02F: data bit 15; 0x030: the clip history of i-2..i */
        uint32_t why = (word & 0x8000u) ? EM_VU1_LVL_ADC_DATA : 0u;
        if (hist & 0x03FFFFu) why |= EM_VU1_LVL_ADC_CLIP;
        /* 0x030, 0x031: cross = e_i.x * e_{i-1}.y - e_{i-1}.x * e_i.y */
        uint32_t cross = emvul_msub(host, emvul_mul(host, e[0], e_prev[1], &bad), e_prev[0], e[1], &bad);
        /* 0x032: ST = the vertex's (s, t, 1) * Q, w carried */
        EmVu1ObjQword st;
        for (unsigned k = 0; k < 3; ++k) st.w[k] = emvul_mul(host, st_in.w[k], cur.q, &bad);
        st.w[3] = o_st.w[3];
        /* 0x033: fog min 255; Q for vertex i+1 */
        uint32_t fog = em_vu_min_bits(cur.fog, fog_row[0]);
        if (!last) {
            if (!emvuo_live(next.c[3])) bad = 1u;
            next.q = emvul_rcp(host, next.c[3]);
        }
        /* 0x035: the cull product, S = its sign; vf09.w = vertex i+1's word */
        const uint32_t cull = emvul_mul(host, cross, w_f, &bad);
        const int cull_known = i >= 2u || (i == 1u && known_es) || (known_es && known_w);
        w_f = pos.w[3];
        /* 0x036, 0x037: e and s kept for vertex i+1; fog max 0 */
        e_prev[0] = e[0]; e_prev[1] = e[1];
        s_prev[0] = cur.s[0]; s_prev[1] = cur.s[1];
        fog = em_vu_max_bits(fog, 0u);
        /* 0x038: the look-ahead position of vertex i+2 */
        pos = emvuo_ld(dmem, v + 11u);
        if (i + 2u == vertices) w_next_known = known_rgbaq_at_start;
        /* 0x039, 0x03A: S joins the ADC. With carried registers another
         * program left, S is unknown: harmless when the vertex has ADC
         * already, fail-stop otherwise. */
        if (!cull_known && !why) {
            out->fault = EM_VU1_LVL_FAULT_STALE;
            out->fault_vertex = i;
            return -1;
        }
        if (cull_known && (cull >> 31)) why |= EM_VU1_LVL_ADC_CULL;
        /* 0x03A..0x03C, 0x03D, 0x03F: vertex i+1's s, g, fog (dead last) */
        if (!last) {
            next.fog = emvul_madd(host, emvul_mul(host, EM_EE_ONE, fog_row[2], &bad), fog_row[3], next.c[3], &bad);
            for (unsigned k = 0; k < 4; ++k)
                next.g[k] = emvul_madd(host, emvul_mul(host, gs[k], next.c[k], &bad), go[k], next.c[3], &bad);
            for (unsigned k = 0; k < 3; ++k) next.s[k] = emvul_mul(host, next.c[k], next.q, &bad);
        }
        /* 0x03B: the colour; 0x03D: vertex i+1's (s, t, 1); 0x03F: its word */
        const EmVu1ObjQword colour = emvuo_ld(dmem, v + 2u);
        const EmVu1ObjQword st_next = emvuo_ld(dmem, v + 5u);
        const uint32_t word_next = emvuo_ld(dmem, v + 7u).w[3] & 0xFFFFu;
        /* 0x03E: ADC adds the fog row's y lane */
        if (why) fog = emvul_add(host, fog, fog_row[1], &bad);
        /* 0x040: RGBAQ = colour + 65536 (all lanes); the vertex's TEX0 */
        EmVu1ObjQword rgbaq;
        for (unsigned k = 0; k < 4; ++k) rgbaq.w[k] = emvul_add(host, colour.w[k], bias, &bad);
        const EmVu1ObjQword tex0 = emvuo_ld(dmem, v);
        /* 0x041: CLIP of vertex i+1's g (dead last) */
        if (!last) {
            next.cf = emvuo_clip(next.g);
            hist = ((hist << 6) | next.cf) & 0xFFFFFFu;
            out->clip[i + 1u] = next.cf;
        }
        /* 0x042: XYZF2 */
        EmVu1ObjQword xyzf;
        const uint32_t sat = out->ftoi_saturated;
        for (unsigned k = 0; k < 3; ++k) xyzf.w[k] = emvuo_ftoi4(cur.s[k], &out->ftoi_saturated);
        xyzf.w[3] = emvuo_ftoi4(fog, &out->ftoi_saturated);
        if (out->ftoi_saturated != sat) out->saturated |= 1u << i;
        if (bad) {
            out->fault = EM_VU1_LVL_FAULT_OPERAND;
            out->fault_vertex = i;
            return -1;
        }
        out->why[i] = why;
        o_rgbaq = rgbaq; o_st = st; o_tex0 = tex0; o_xyzf = xyzf;
        cur = next;
        st_in = st_next;
        word = word_next;
    }
    /* 0x043..0x046: vertex 31's qwords; 0x047..0x049: the template */
    const uint32_t v = top + 4u * vertices;
    emvuo_st(dmem, v + kick_offset - 1u, o_rgbaq);
    emvuo_st(dmem, v + kick_offset - 2u, o_st);
    emvuo_st(dmem, v + kick_offset - 3u, o_tex0);
    emvuo_st(dmem, v + kick_offset, o_xyzf);
    emvuo_st(dmem, top + kick_offset, emvuo_ld(dmem, 1020u));
    s->rgbaq = o_rgbaq; s->st = o_st; s->tex0 = o_tex0; s->xyzf = o_xyzf;
    s->e_prev[0] = e_prev[0]; s->e_prev[1] = e_prev[1];
    s->s_prev[0] = s_prev[0]; s->s_prev[1] = s_prev[1];
    s->w_prev = w_f;
    s->known = 1u;
    s->known_rgbaq = 1u;
    s->known_w = w_next_known;
    return 0;                                         /* 0x04B: XGKICK */
}

/* One batch with the model's integer arithmetic: the reference form (the
 * tests, the drop shadow's box pass). */
static inline int em_vu1_level_kernel_batch(EmVu1LvlState *s, EmVu1ObjQword *dmem, uint32_t top,
                                            EmVu1LvlBatch *out)
{
    return emvul_batch(0, s, dmem, top, out, EM_VU1_LVL_VERTICES, EM_VU1_LVL_KICK);
}

/* The host-arithmetic instance; only em_vu1_level_kernel_batch_host calls
 * it, inside the host environment (the call keeps every float operation
 * inside it). */
EMVUL_NOINLINE int emvul_batch_host_core(EmVu1LvlState *s, EmVu1ObjQword *dmem, uint32_t top, EmVu1LvlBatch *out)
{
    return emvul_batch(1, s, dmem, top, out, EM_VU1_LVL_VERTICES, EM_VU1_LVL_KICK);
}

/* The same batch on the host FPU (em_vu_host_lanes.h), for the live walk:
 * every finite operation equals the model's, so a batch that does not fault
 * is identical in every written word, ADC reason and counter (by induction
 * over the program: the same values give the same checks and branches). A
 * batch that faults either way is re-run on the model's arithmetic, which
 * decides the fault code and vertex exactly as em_vu1_level_kernel_batch:
 * the state is written only on success; the batch writes only dmem
 * TOP + 0x81 .. TOP + 0x104, never its inputs TOP .. TOP + 0x7F; the re-run
 * rewrites every word of that range it later reads (the look-ahead loads);
 * and a TOP whose range reaches the rows 0..3 or 1020..1023 runs on the
 * model's arithmetic from the start. */
EMVUL_NOINLINE int em_vu1_level_kernel_batch_host(EmVu1LvlState *s, EmVu1ObjQword *dmem, uint32_t top,
                                                 EmVu1LvlBatch *out)
{
    static const uint32_t rows[8] = { 0u, 1u, 2u, 3u, 1020u, 1021u, 1022u, 1023u };
    int host = EM_VU_HOST_LANES;
    for (unsigned k = 0; k < 8u; ++k)
        if (((rows[k] - (top + 0x81u)) & 1023u) <= 0x83u) host = 0;
    if (!host) return em_vu1_level_kernel_batch(s, dmem, top, out);
    EmVuHostEnv env;
    em_vu_host_enter(&env);
    int rc = emvul_batch_host_core(s, dmem, top, out);
    em_vu_host_leave(&env);
    if (rc < 0) rc = em_vu1_level_kernel_batch(s, dmem, top, out);
    return rc;
}

/* AREA01's 00237450 is the same 79-instruction program with three
 * vertices and output/kick offset 0x10. The instruction oracle asserts
 * exactly those eleven encoded-immediate differences; arithmetic and
 * carried-register behavior stay in the one translation above. */
static inline int em_vu1_dynamic_kernel_batch(EmVu1LvlState *s, EmVu1ObjQword *dmem,
                                              uint32_t top, EmVu1LvlBatch *out)
{
    return emvul_batch(0, s, dmem, top, out, 3u, 0x10u);
}

/* TOP of the k-th MSCAL / MSCNT since the kernel packet's OFFSET code (its
 * CALL): the OFFSET code resets the VIF double buffer to BASE, and every
 * MSCAL / MSCNT flips it. */
static inline uint32_t em_vu1_level_kernel_top(uint32_t k)
{
    return EM_VU1_LVL_BASE + ((k & 1u) ? EM_VU1_LVL_OFFSET : 0u);
}

#endif /* EM_VU1_LEVEL_KERNEL_H */
