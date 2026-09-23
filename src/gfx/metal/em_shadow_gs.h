/* em_shadow_gs.h — the GS side of the player drop shadow (001DA6A0 chain),
 * shared by the Metal backend (em_gfx_metal.m) and
 * tools/test_shadow_original_reference.py. docs/SHADOW_ORIGINAL.md has the
 * evidence; src/game/em_shadow_original.{h,c} is the EE side that computes
 * every matrix, colour and the receiver object list.
 *
 * The chain 001DA6A0 builds draws, in this order, into the frame:
 *   1. 001DA290: a two-triangle strip over the whole 512x224 field
 *      (1792..2304 x 1936..2160, Z 0xFFFFFFFF, RGBAQ 0) in state block
 *      (2,9) D_00815E10: TEST 0x51001 (alpha test NEVER, AFAIL FB_ONLY,
 *      ZTST GEQUAL), ALPHA 0x80000000A9 (Cv = Cd * 0x80 >> 7 = Cd), ZBUF
 *      ZMSK 1. Colour is kept, destination alpha becomes 0.
 *   2. 001DA310 x2: the chunk27 library box models 0x14 then 0x15 through
 *      the level kernel 00237180 (template D_008166C0: PRIM 0x044 = strip,
 *      flat, untextured, ABE; per vertex only XYZF2) and its clip kernel
 *      00239C90, in the same (2,9) state with the RGBAQ 001DA310 computes.
 *      Only destination alpha changes: 128 where a front face of model
 *      0x14 lies in front of the frame's depth, then 1 where a face of
 *      model 0x15 does. 00237180 culls (ADC) every vertex whose triangle
 *      has a negative screen-space winding (em_shadow_gs_level_batch).
 *   3. 001D9EE0: the 128x128 PSMCT32 target at FBP 0x12C (D_00817E20:
 *      FRAME 0x2012C, ZBUF ZMSK 1, XYOFFSET 1984, SCISSOR 0..127, TEST
 *      ZTST ALWAYS) is cleared by a sprite to RGBAQ (128,128,128,0); the
 *      proxy mesh is drawn by the object kernel 0023C750 (template
 *      D_008168C0: PRIM 0x004 = strip, flat, untextured, no blend; per
 *      vertex only XYZ2) in state block D_00814DC0 (TEST 0x3000D: alpha
 *      GREATER 0, ZTST ALWAYS; ZBUF ZMSK 1) with the A+D RGBAQ 0xFFFFFF80.
 *      The kernel does not cull. Every drawn texel is (128,255,255,255).
 *   4. 001D5C80: the receiver objects through kernel 0023C200 (template
 *      D_008169C0: PRIM 0x07C = strip, Gouraud, textured, fogged, ABE;
 *      per vertex ST, RGBAQ, XYZF2) and, for objects outside the guard
 *      band, again through 0023E8A0; state block (2,6) D_00815C60 (TEST
 *      0x5C00D: alpha GREATER 0, DATE with DATM 1, ZTST GEQUAL; ALPHA
 *      0x44: Cv = (Cs - Cd) * As >> 7 + Cd; TEX1 0x60 bilinear; ZBUF ZMSK
 *      1), CLAMP 5 (D_008146C0) and TEX0 0x5DC00A580 (the target, TCC 1,
 *      MODULATE). 0023C200 does not cull.
 * The register values below are the ones the captured chains carry; the
 * reference test replays each executed chain as the DMAC/VIF1/GIF would
 * and checks every one of them, and the kernels' per-vertex outputs
 * against the functions here.
 *
 * Arithmetic: binary32 with the VU's truncation after every product and
 * sum (em_fog_gs_vu_trunc), in the instruction order of the kernels. The
 * VU DIV result is modelled as the truncated quotient (the same model the
 * test's VU interpreter uses). The status S flag (00237180's cull) is the
 * sign bit of the truncated product. */
#ifndef EM_SHADOW_GS_H
#define EM_SHADOW_GS_H

#include "gfx/metal/em_fog_gs.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* ---------------------------------------------------------------- state -- */

/* GS register numbers (A+D addresses). */
#define EM_SHADOW_GS_PRIM 0x00u
#define EM_SHADOW_GS_RGBAQ 0x01u
#define EM_SHADOW_GS_TEX0_1 0x06u
#define EM_SHADOW_GS_CLAMP_1 0x08u
#define EM_SHADOW_GS_TEX1_1 0x14u
#define EM_SHADOW_GS_XYOFFSET_1 0x18u
#define EM_SHADOW_GS_SCISSOR_1 0x40u
#define EM_SHADOW_GS_ALPHA_1 0x42u
#define EM_SHADOW_GS_TEST_1 0x47u
#define EM_SHADOW_GS_FRAME_1 0x4Cu
#define EM_SHADOW_GS_ZBUF_1 0x4Eu

/* The draws of the chain, in chain order. */
enum {
    EM_SHADOW_GS_PASS_ALPHA_CLEAR = 0, /* 001DA290 */
    EM_SHADOW_GS_PASS_BOX = 1,         /* 001DA310, kernel 00237180 */
    EM_SHADOW_GS_PASS_TARGET_CLEAR = 2,/* D_00817E20 sprite */
    EM_SHADOW_GS_PASS_SILHOUETTE = 3,  /* 001D9EE0, kernel 0023C750 */
    EM_SHADOW_GS_PASS_RECEIVER = 4,    /* 001D5C80, kernel 0023C200 */
    EM_SHADOW_GS_PASS_COUNT = 5
};

/* The GS context-1 state in force at a pass's draw. `prim` is the PRIM of
 * the GIF tag the GS draws with (the kernel template's, or the DIRECT
 * packet's); `regs` its REGS nibbles (NREG 4 or 5, low nibble first) and
 * `nloop` its NLOOP. `tex0`/`tex1`/`clamp` are meaningful only for the
 * textured receiver pass (the others have TME 0); `rgbaq` only where it is
 * constant for the pass (it is per call for the box). */
typedef struct {
    uint64_t prim, regs, nloop;
    uint64_t frame, zbuf, xyoffset, scissor, test, alpha;
    uint64_t tex0, tex1, clamp, rgbaq;
} EmShadowGsState;

/* The frame's draw environment (D_008143D0, REFed at the head of every
 * world list and again after the silhouette): FRAME_1 FBW 8 PSMCT32 FBMSK
 * 0 with FBP 0 or 0x38 (the two frame buffers alternate), XYOFFSET_1
 * (1792, 1936) plus the field's half line (OFY + 8/16 on alternate
 * fields), SCISSOR_1 0..511 x 0..223. The main-frame passes draw into
 * whatever that environment names; the backend draws them into its frame
 * target, which is progressive (no field offset). */
#define EM_SHADOW_GS_FRAME_MAIN 0x80000ull
#define EM_SHADOW_GS_FRAME_FBP_MASK 0x1FFull
#define EM_SHADOW_GS_XYOFFSET_MAIN 0x790000007000ull
#define EM_SHADOW_GS_XYOFFSET_FIELD_MASK (0xFull << 32)
#define EM_SHADOW_GS_SCISSOR_MAIN 0xDF000001FF0000ull
/* The 128x128 target (D_00817E20). */
#define EM_SHADOW_GS_FRAME_TARGET 0x2012Cull          /* FBP 0x12C, FBW 2 */
#define EM_SHADOW_GS_ZBUF_TARGET 0x102000010ull       /* ZMSK 1 */
#define EM_SHADOW_GS_XYOFFSET_TARGET 0x7C0000007C00ull /* 1984, 1984 */
#define EM_SHADOW_GS_SCISSOR_TARGET 0x7F0000007F0000ull /* 0..127 */
#define EM_SHADOW_GS_TARGET_ORIGIN 1984.0f
#define EM_SHADOW_GS_TARGET_SIZE 128u
/* Receiver TEX0: TBP0 0x2580 (= FBP 0x12C x 32), TBW 2, PSMCT32, TW = TH
 * = 7, TCC 1, TFX MODULATE. */
#define EM_SHADOW_GS_TEX0_RECEIVER 0x5DC00A580ull
/* Z of the 001DA290 strip (PACKED XYZ2 Z word, all ones). */
#define EM_SHADOW_GS_CLEAR_Z 0xFFFFFFFFu

/* The values above per pass. Returns 0, or -1 for an unknown pass. */
static inline int em_shadow_gs_pass_state(int pass, EmShadowGsState *s)
{
    memset(s, 0, sizeof *s);
    s->frame = EM_SHADOW_GS_FRAME_MAIN;
    s->xyoffset = EM_SHADOW_GS_XYOFFSET_MAIN;
    s->scissor = EM_SHADOW_GS_SCISSOR_MAIN;
    switch (pass) {
    case EM_SHADOW_GS_PASS_ALPHA_CLEAR:           /* DIRECT in 001DA290 */
        s->prim = 0x044; s->regs = 0x44441; s->nloop = 1;
        s->zbuf = 0x101000070ull; s->test = 0x51001; s->alpha = 0x80000000A9ull;
        s->rgbaq = 0;
        return 0;
    case EM_SHADOW_GS_PASS_BOX:                   /* D_008166C0 template */
        s->prim = 0x044; s->regs = 0x4FFF; s->nloop = 32;
        s->zbuf = 0x101000070ull; s->test = 0x51001; s->alpha = 0x80000000A9ull;
        return 0;
    case EM_SHADOW_GS_PASS_TARGET_CLEAR:          /* D_00817E20 sprite */
        s->prim = 0x006; s->regs = 0; s->nloop = 0;
        s->frame = EM_SHADOW_GS_FRAME_TARGET; s->zbuf = EM_SHADOW_GS_ZBUF_TARGET;
        s->xyoffset = EM_SHADOW_GS_XYOFFSET_TARGET;
        s->scissor = EM_SHADOW_GS_SCISSOR_TARGET;
        s->test = 0x30000; s->rgbaq = 0x3F80000000808080ull;
        return 0;
    case EM_SHADOW_GS_PASS_SILHOUETTE:            /* D_008168C0 template */
        s->prim = 0x004; s->regs = 0x5FFF; s->nloop = 32;
        s->frame = EM_SHADOW_GS_FRAME_TARGET; s->zbuf = 0x101000070ull;
        s->xyoffset = EM_SHADOW_GS_XYOFFSET_TARGET;
        s->scissor = EM_SHADOW_GS_SCISSOR_TARGET;
        s->test = 0x3000D; s->alpha = 0x80000000A8ull; s->rgbaq = 0xFFFFFF80u;
        return 0;
    case EM_SHADOW_GS_PASS_RECEIVER:              /* D_008169C0 template */
        s->prim = 0x07C; s->regs = 0x412F; s->nloop = 32;
        s->zbuf = 0x101000070ull; s->test = 0x5C00D; s->alpha = 0x44;
        s->tex0 = EM_SHADOW_GS_TEX0_RECEIVER; s->tex1 = 0x60; s->clamp = 5;
        return 0;
    }
    return -1;
}

/* NULL when the Metal backend implements `s` for `pass` exactly (the
 * values of em_shadow_gs_pass_state), else the first field it does not.
 * The backend's pipelines are built for exactly these values:
 *   ALPHA_CLEAR / BOX: alpha-only colour write (ALPHA A9 FIX 0x80 keeps
 *     Cd; AFAIL FB_ONLY writes As), depth test (GEQUAL) without write;
 *   TARGET_CLEAR: a clear of the whole 128x128 target;
 *   SILHOUETTE: flat colour, no blend, no depth, no cull;
 *   RECEIVER: the GS pixel pipeline in the fragment shader (bilinear
 *     MODULATE of the target, fog, alpha test, DATE, ALPHA 0x44 blend,
 *     COLCLAMP) with depth test (GEQUAL) without write. */
static inline const char *em_shadow_gs_unsupported(int pass,
                                                   const EmShadowGsState *s)
{
    EmShadowGsState want;
    if (em_shadow_gs_pass_state(pass, &want)) return "unknown pass";
    if (s->prim != want.prim) return "PRIM";
    if (s->regs != want.regs || s->nloop != want.nloop) return "GIF REGS/NLOOP";
    if (want.frame == EM_SHADOW_GS_FRAME_MAIN) {
        const uint64_t fbp = s->frame & EM_SHADOW_GS_FRAME_FBP_MASK;
        if ((s->frame & ~EM_SHADOW_GS_FRAME_FBP_MASK) != want.frame ||
            (fbp != 0 && fbp != 0x38)) return "FRAME_1";
        if ((s->xyoffset & ~EM_SHADOW_GS_XYOFFSET_FIELD_MASK) != want.xyoffset ||
            ((s->xyoffset >> 32 & 0xFu) != 0 && (s->xyoffset >> 32 & 0xFu) != 8))
            return "XYOFFSET_1";
    } else if (s->frame != want.frame || s->xyoffset != want.xyoffset) {
        return "FRAME_1/XYOFFSET_1";
    }
    if (s->zbuf != want.zbuf) return "ZBUF_1";
    if (s->scissor != want.scissor) return "SCISSOR_1";
    if (s->test != want.test) return "TEST_1";
    if (want.prim & 0x40u && s->alpha != want.alpha) return "ALPHA_1";
    if (want.prim & 0x10u && (s->tex0 != want.tex0 || s->tex1 != want.tex1 ||
                              s->clamp != want.clamp))
        return "TEX0_1/TEX1_1/CLAMP_1";
    if (pass != EM_SHADOW_GS_PASS_BOX && pass != EM_SHADOW_GS_PASS_RECEIVER &&
        s->rgbaq != want.rgbaq)
        return "RGBAQ";
    return NULL;
}

/* The kernels' guard rows, VU1 dmem 1022/1023, as the channel templates
 * upload them (checked against the captured D_008166C0, D_008168C0 and
 * D_008169C0 by the reference test). x' = x / 2040 - (256/255) w and the
 * same for y put the guard band at GS 8..4088; the z rows differ per
 * template. dmem 1021 is (255, 2048, A, B) with the area's fog
 * coefficients (em_fog_gs_coefficients). */
static inline float em_shadow_gs_bits(uint32_t b)
{
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

/* D_008166C0 (level/box, kernel 00237180) and D_008169C0 (receivers,
 * kernel 0023C200). */
static inline void em_shadow_gs_level_rows(float k1022[4], float k1023[4])
{
    k1022[0] = k1022[1] = em_shadow_gs_bits(0x3A008081u);
    k1022[2] = em_shadow_gs_bits(0x34000000u);
    k1022[3] = 1.0f;
    k1023[0] = k1023[1] = em_shadow_gs_bits(0xBF808081u);
    k1023[2] = em_shadow_gs_bits(0xBF7FFFFEu);
    k1023[3] = 0.0f;
}

/* D_008168C0 (the silhouette, object kernel 0023C750). */
static inline void em_shadow_gs_object_rows(float k1022[4], float k1023[4])
{
    k1022[0] = k1022[1] = em_shadow_gs_bits(0x3A008081u);
    k1022[2] = em_shadow_gs_bits(0x34008080u);
    k1022[3] = 0.0f;
    k1023[0] = k1023[1] = em_shadow_gs_bits(0xBF808081u);
    k1023[2] = 0.0f;
    k1023[3] = 1.0f;
}

/* ------------------------------------------------------------ VU helpers -- */

static inline float em_shadow_gs_mul(float a, float b)
{
    return em_fog_gs_vu_trunc((double)a * b);
}

static inline float em_shadow_gs_add(float a, float b)
{
    return em_fog_gs_vu_trunc((double)a + b);
}

static inline float em_shadow_gs_sub(float a, float b)
{
    return em_fog_gs_vu_trunc((double)a - b);
}

/* mulAx / maddAy / maddAz / maddbcw with vf00.w: out = p x m, m's memory
 * rows = the four uploaded qwords (row-vector convention). */
static inline void em_shadow_gs_xform(const float m[16], const float p[3],
                                      float out[4])
{
    for (unsigned lane = 0; lane < 4; ++lane) {
        float acc = em_shadow_gs_mul(m[lane], p[0]);
        acc = em_shadow_gs_add(acc, em_shadow_gs_mul(m[4 + lane], p[1]));
        acc = em_shadow_gs_add(acc, em_shadow_gs_mul(m[8 + lane], p[2]));
        out[lane] = em_shadow_gs_add(acc, em_shadow_gs_mul(m[12 + lane], 1.0f));
    }
}

/* The VU CLIP judgement of (x, y, z) against |w|: +x -x +y -y +z -z. */
static inline uint32_t em_shadow_gs_clip(const float v[4])
{
    const float w = fabsf(v[3]);
    return (uint32_t)(v[0] > w) | (uint32_t)(v[0] < -w) << 1 |
           (uint32_t)(v[1] > w) << 2 | (uint32_t)(v[1] < -w) << 3 |
           (uint32_t)(v[2] > w) << 4 | (uint32_t)(v[2] < -w) << 5;
}

/* mulA.xyzw ACC = g0 * c; maddbcw.xyzw out = ACC + g1 * c.w (the guard
 * scale/offset rows dmem 1022/1023, or 1023/1022 as each kernel loads). */
static inline void em_shadow_gs_guard(const float g0[4], const float g1[4],
                                      const float c[4], float out[4])
{
    for (unsigned lane = 0; lane < 4; ++lane)
        out[lane] = em_shadow_gs_add(em_shadow_gs_mul(g0[lane], c[lane]),
                                     em_shadow_gs_mul(g1[lane], c[3]));
}

/* ftoi4: truncation toward zero of value * 16. What the VU returns
 * outside the int32 range is NOT established (this model saturates, the
 * reference test's interpreter wraps); it only matters for vertices
 * outside the guard band, whose words are never drawn (ADC; the test
 * counts such words as gs_overflow_words_clipped). */
static inline int32_t em_shadow_gs_ftoi4(float v)
{
    const double x = (double)v * 16.0;
    if (x >= 2147483647.0) return 2147483647;
    if (x <= -2147483648.0) return (int32_t)-2147483647 - 1;
    return (int32_t)x;
}

/* One kicked vertex: the four XYZF2/XYZ2 words the kernel stores (ftoi4 of
 * the screen x, y, z and of F, F + 2048 when ADC is set), plus the ADC
 * reasons. Fields: X = w[0] & 0xFFFF, Y = w[1] & 0xFFFF (12.4); XYZF2 Z =
 * (w[2] >> 4) & 0xFFFFFF, F = (w[3] >> 4) & 0xFF, ADC = w[3] bit 15. */
typedef struct {
    int32_t w[4];
    uint32_t adc;       /* ADC bit: no drawing kick for this vertex */
    uint32_t why;       /* EM_SHADOW_GS_ADC_* reasons */
} EmShadowGsVertex;

#define EM_SHADOW_GS_ADC_DATA 1u   /* the vertex word carries the flag */
#define EM_SHADOW_GS_ADC_CLIP 2u   /* a vertex of the triangle is outside
                                      the guard band (CLIP flag of
                                      vertices i-2..i, fcand 0x03FFFF) */
#define EM_SHADOW_GS_ADC_CULL 4u   /* 00237180: negative winding */
#define EM_SHADOW_GS_ADC_STALE 8u  /* 00237180: a strip's first two
                                      vertices without the data flag; the
                                      cull would read the previous batch's
                                      registers, which is not modelled */
#define EM_SHADOW_GS_ADC_REJECT 16u /* with CLIP: vertices i-2..i are all
                                      outside the same guard plane, so the
                                      clip kernel skips the triangle too
                                      (its six fcor tests) */

/* The clip kernels 00239C90 (box pass, after 00237180) and 0023E8A0
 * (receiver re-pass of a guard-band object, after 0023C200) receive the
 * same batch and matrix as the kernel before them and repeat its
 * per-vertex transform, guard rows and CLIP. Their loop (micro 0x000..0x035
 * of each program, vertex index i = 32 - vi11) goes on to the clipping
 * code only when vertex i has no data ADC (0x8000 / 0xA000), the CLIP
 * history of vertices i-2..i is non-zero (fcand 0x03FFFF), not all three
 * vertices are outside one guard plane (fcor 0xFFEFBE, 0xFFDF7D, 0xFFBEFB,
 * 0xFF7DF7, 0xFEFBEF, 0xFDF7DF) and i >= 2 (isubiu vi1, vi11, 0x1E;
 * ibgtz). Exactly the triangles that kernel left undrawn for CLIP (the
 * same history test), minus the rejected ones, so the two passes never
 * draw one triangle twice. The clipping itself (planes w = 0.1 and screen
 * X, Y = 4.0 / 4088.0 by the constants it loads; triangle-list kicks, PRIM
 * 0x07B, in the receiver captures) is NOT translated:
 * em_shadow_gs_needs_clip names the triangles a caller must fault on. */
static inline uint32_t em_shadow_gs_reject(uint32_t hist)
{
    static const uint32_t plane[6] = { 0x001041u, 0x002082u, 0x004104u,
                                       0x008208u, 0x010410u, 0x020820u };
    for (unsigned k = 0; k < 6; ++k)
        if ((hist & plane[k]) == plane[k]) return 1u;
    return 0u;
}

/* The strip triangle ending at vertex i (0..31 inside its batch) goes to
 * the clip kernel. */
static inline uint32_t em_shadow_gs_needs_clip(uint32_t why, uint32_t i)
{
    return i >= 2u && !(why & EM_SHADOW_GS_ADC_DATA) &&
           (why & EM_SHADOW_GS_ADC_CLIP) && !(why & EM_SHADOW_GS_ADC_REJECT);
}

/* The kernels' fog term: ACC = 1 * A; F = ACC + B * c.w; min 255; max 0
 * (mulAz.w, maddbcw.w, minibcx.w, maxbcx.w) with the template row
 * k1021 = (255, 2048, A, B); ADC then adds 2048 (addbcy.w) before ftoi4. */
static inline int32_t em_shadow_gs_fog_word(const float k1021[4], float cw,
                                            uint32_t adc)
{
    float fog = em_shadow_gs_add(em_shadow_gs_mul(1.0f, k1021[2]),
                                 em_shadow_gs_mul(k1021[3], cw));
    fog = fminf(fog, k1021[0]);
    fog = fmaxf(fog, 0.0f);
    if (adc) fog = em_shadow_gs_add(fog, k1021[1]);
    return em_shadow_gs_ftoi4(fog);
}

/* The screen words: ftoi4 of c.xyz * Q (mulq.xyz). */
static inline void em_shadow_gs_screen(const float c[4], float q, float s[3],
                                       int32_t w[3])
{
    for (unsigned lane = 0; lane < 3; ++lane) {
        s[lane] = em_shadow_gs_mul(c[lane], q);
        w[lane] = em_shadow_gs_ftoi4(s[lane]);
    }
}

/* ------------------------------------------------------ kernel 00237180 -- */

/* The level kernel (MPG 0x002371B0) over one 32-vertex batch, for the box
 * models. `m` = dmem 0..3 (the (W x V) x P matrix 001DA310 uploads),
 * `k1021`/`k1022`/`k1023` the template qwords (fog row (255, 2048, A, B)
 * and the guard rows), `qw3[i]` the vertex's position qword: x, y, z and
 * the data word (its float value is the strip's winding sign, bit 15 the
 * ADC flag). Per vertex i:
 *   clip  c = p x m; Q = 1/c.w; screen s = c.xyz * Q -> XYZF2 (ftoi4);
 *   ADC   = word bit 15
 *         | any CLIP flag of vertices i-2..i (fcand 0x03FFFF; the
 *           00239C90 pass takes those, em_shadow_gs_needs_clip)
 *         | S of (e_i.x * e_{i-1}.y - e_{i-1}.x * e_i.y) * w_i, where e_i =
 *           s_i.xy - s_{i-1}.xy and w_i the data word as a float (msubbcy,
 *           mulbcw, fsand 0x2 at 0x237378).
 * Returns the number of vertices with EM_SHADOW_GS_ADC_STALE (0 for every
 * captured box). */
static inline uint32_t em_shadow_gs_level_batch(const float m[16],
    const float k1021[4], const float k1022[4], const float k1023[4],
    const float (*qw3)[4], uint32_t n, EmShadowGsVertex *out)
{
    float prev[2][2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f } };
    uint32_t clip_hist = 0, stale = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const float p[3] = { qw3[i][0], qw3[i][1], qw3[i][2] };
        uint32_t word;
        memcpy(&word, &qw3[i][3], sizeof word);
        float c[4], g[4], s[3];
        em_shadow_gs_xform(m, p, c);
        em_shadow_gs_guard(k1022, k1023, c, g);
        clip_hist = ((clip_hist << 6) | em_shadow_gs_clip(g)) & 0xFFFFFFu;
        const float q = em_fog_gs_vu_trunc(1.0 / (double)c[3]);
        EmShadowGsVertex *v = &out[i];
        em_shadow_gs_screen(c, q, s, v->w);
        v->why = (word & 0x8000u) ? EM_SHADOW_GS_ADC_DATA : 0u;
        if (clip_hist & 0x3FFFFu) v->why |= EM_SHADOW_GS_ADC_CLIP;
        if ((clip_hist & 0x3FFFFu) && em_shadow_gs_reject(clip_hist))
            v->why |= EM_SHADOW_GS_ADC_REJECT;
        if (i >= 2) {
            const float e1[2] = { em_shadow_gs_sub(s[0], prev[1][0]),
                                  em_shadow_gs_sub(s[1], prev[1][1]) };
            const float e0[2] = { em_shadow_gs_sub(prev[1][0], prev[0][0]),
                                  em_shadow_gs_sub(prev[1][1], prev[0][1]) };
            float cross = em_shadow_gs_sub(em_shadow_gs_mul(e1[0], e0[1]),
                                           em_shadow_gs_mul(e0[0], e1[1]));
            cross = em_shadow_gs_mul(cross, qw3[i][3]);
            uint32_t cb;
            memcpy(&cb, &cross, sizeof cb);
            if (cb >> 31) v->why |= EM_SHADOW_GS_ADC_CULL;
        } else if (!(v->why & EM_SHADOW_GS_ADC_DATA)) {
            v->why |= EM_SHADOW_GS_ADC_STALE;
            ++stale;
        }
        v->adc = v->why ? 1u : 0u;
        v->w[3] = em_shadow_gs_fog_word(k1021, c[3], v->adc);
        prev[0][0] = prev[1][0]; prev[0][1] = prev[1][1];
        prev[1][0] = s[0]; prev[1][1] = s[1];
    }
    return stale;
}

/* ------------------------------------------------------ kernel 0023C750 -- */

/* 001C7420's bone upload: node (+0x90, row-vector) x vp, per memory row
 * b: ACC = vp.row0 * b.x; ACC += vp.row1 * b.y; ACC += vp.row2 * b.z;
 * row = ACC + vp.row3 * b.w (checked against the captured palette by the
 * reference test). */
static inline void em_shadow_gs_bone(const float node[16], const float vp[16],
                                     float out[16])
{
    for (unsigned row = 0; row < 4; ++row) {
        const float *b = node + 4 * row;
        for (unsigned lane = 0; lane < 4; ++lane) {
            float acc = em_shadow_gs_mul(vp[lane], b[0]);
            acc = em_shadow_gs_add(acc, em_shadow_gs_mul(vp[4 + lane], b[1]));
            acc = em_shadow_gs_add(acc, em_shadow_gs_mul(vp[8 + lane], b[2]));
            out[4 * row + lane] = em_shadow_gs_add(acc,
                em_shadow_gs_mul(vp[12 + lane], b[3]));
        }
    }
}

/* The object kernel (MPG 0x0023C780) position path over one 32-vertex
 * batch of the silhouette: vertex i uses the bone matrix bone[i] (the
 * uploaded rows at the dmem address its data word names), c = p x bone,
 * XYZ2 words = ftoi4(c.xyz * (1/c.w)), and ADC = data word bit 15 or any
 * guard-band CLIP flag of vertices i-2..i (k1022 * c + k1023 * c.w). The
 * kernel never culls. */
static inline void em_shadow_gs_object_batch(const float *const *bone,
    const float k1021[4], const float k1022[4], const float k1023[4],
    const float (*qw3)[4], uint32_t n, EmShadowGsVertex *out)
{
    uint32_t clip_hist = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const float p[3] = { qw3[i][0], qw3[i][1], qw3[i][2] };
        uint32_t word;
        memcpy(&word, &qw3[i][3], sizeof word);
        float c[4], g[4], s[3];
        em_shadow_gs_xform(bone[i], p, c);
        em_shadow_gs_guard(k1022, k1023, c, g);
        clip_hist = ((clip_hist << 6) | em_shadow_gs_clip(g)) & 0xFFFFFFu;
        const float q = em_fog_gs_vu_trunc(1.0 / (double)c[3]);
        EmShadowGsVertex *v = &out[i];
        em_shadow_gs_screen(c, q, s, v->w);
        v->why = (word & 0x8000u) ? EM_SHADOW_GS_ADC_DATA : 0u;
        if (clip_hist & 0x3FFFFu) v->why |= EM_SHADOW_GS_ADC_CLIP;
        v->adc = v->why ? 1u : 0u;
        v->w[3] = em_shadow_gs_fog_word(k1021, c[3], v->adc);
    }
}

/* Target pixel -> Metal NDC of the 128x128 target. The GS samples window
 * pixel i of the target at X = 1984 + i (XYOFFSET), the Metal pixel centre
 * of column i is i + 0.5: ndc = ((X - 1984) + 0.5) / 64 - 1, Y down. With
 * X on the 1/16 grid every step is exact in binary32, so the target's
 * Metal pixel centres are the GS sample points. */
static inline void em_shadow_gs_target_ndc(float x_gs, float y_gs, float ndc[2])
{
    ndc[0] = ((x_gs - EM_SHADOW_GS_TARGET_ORIGIN) + 0.5f) / 64.0f - 1.0f;
    ndc[1] = 1.0f - ((y_gs - EM_SHADOW_GS_TARGET_ORIGIN) + 0.5f) / 64.0f;
}

/* ------------------------------------------------------ kernel 0023C200 -- */

/* The receiver kernel (MPG 0x0023C230) over one 32-vertex batch. `camera`
 * = dmem 0..3 (D_70003AC0), `uv` = dmem 8..11 (ctx+0x24B0), `k1021` the
 * template fog row (255, 2048, A, B), `k1022`/`k1023` the guard rows:
 *   c = p x camera; t = p x uv; Q = 1/c.w;
 *   ST    = (t.x * Q, t.y * Q, t.z * Q)          (t.z is 1 for every uv)
 *   RGBAQ = (0, 0, 0, low byte of max(min(t.w, 8388863), 8388608))
 *   XYZF2 = ftoi4(c.xyz * Q), F = max(min(A + B * c.w, 255), 0)
 *   ADC   = data word bits 15 or 13 (vi12 = 0xA000) or any CLIP flag of
 *           vertices i-2..i (k1022 * c + k1023 * c.w); only a guard-band
 *           object (class 2) gets the 0023E8A0 pass for those triangles
 *           (em_shadow_gs_needs_clip), classes 0 and 1 never draw them.
 * The kernel never culls. */
typedef struct {
    float s, t, q;
    uint32_t a;          /* RGBAQ A */
    EmShadowGsVertex xyzf;
} EmShadowGsReceiverVertex;

static inline void em_shadow_gs_receiver_batch(const float camera[16],
    const float uv[16], const float k1021[4], const float k1022[4],
    const float k1023[4], const float (*qw3)[4], uint32_t n,
    EmShadowGsReceiverVertex *out)
{
    uint32_t hist = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const float p[3] = { qw3[i][0], qw3[i][1], qw3[i][2] };
        uint32_t word;
        memcpy(&word, &qw3[i][3], sizeof word);
        float c[4], t[4], g[4], s[3];
        em_shadow_gs_xform(camera, p, c);
        em_shadow_gs_xform(uv, p, t);
        em_shadow_gs_guard(k1022, k1023, c, g);
        hist = ((hist << 6) | em_shadow_gs_clip(g)) & 0xFFFFFFu;
        const float q = em_fog_gs_vu_trunc(1.0 / (double)c[3]);
        EmShadowGsReceiverVertex *v = &out[i];
        v->s = em_shadow_gs_mul(t[0], q);
        v->t = em_shadow_gs_mul(t[1], q);
        v->q = em_shadow_gs_mul(t[2], q);
        float a = fminf(t[3], 8388863.0f);
        a = fmaxf(a, 8388608.0f);
        uint32_t ab;
        memcpy(&ab, &a, sizeof ab);
        v->a = ab & 0xFFu;
        em_shadow_gs_screen(c, q, s, v->xyzf.w);
        v->xyzf.why = (word & 0xA000u) ? EM_SHADOW_GS_ADC_DATA : 0u;
        if (hist & 0x3FFFFu) v->xyzf.why |= EM_SHADOW_GS_ADC_CLIP;
        if ((hist & 0x3FFFFu) && em_shadow_gs_reject(hist))
            v->xyzf.why |= EM_SHADOW_GS_ADC_REJECT;
        v->xyzf.adc = v->xyzf.why ? 1u : 0u;
        v->xyzf.w[3] = em_shadow_gs_fog_word(k1021, c[3], v->xyzf.adc);
    }
}

/* ----------------------------------------------------------- pixel side -- */

/* GS bilinear (TEX1 MMAG/MMIN LINEAR, CLAMP_1 5) of the target's alpha at
 * texel coordinate (u, v) = (S/Q, T/Q) * 128: the GS works on the 1/16
 * texel grid, offsets by half a texel and weights the four texels with
 * 4-bit fractions, (sum of w * At) >> 8. `alpha(x, y)` = At of texel
 * (x, y) after the CLAMP (0..127). The Metal fragment shader performs the
 * same steps; this is its C statement for the unit test. */
static inline uint32_t em_shadow_gs_bilinear_alpha(float u, float v,
    const uint8_t *target_alpha)
{
    const int32_t uu = (int32_t)floorf(u * 16.0f) - 8;
    const int32_t vv = (int32_t)floorf(v * 16.0f) - 8;
    const int32_t x0 = uu >> 4, y0 = vv >> 4, fu = uu & 15, fv = vv & 15;
    int32_t xs[2] = { x0, x0 + 1 }, ys[2] = { y0, y0 + 1 };
    for (unsigned k = 0; k < 2; ++k) {
        xs[k] = xs[k] < 0 ? 0 : xs[k] > 127 ? 127 : xs[k];
        ys[k] = ys[k] < 0 ? 0 : ys[k] > 127 ? 127 : ys[k];
    }
    const uint32_t a00 = target_alpha[ys[0] * 128 + xs[0]];
    const uint32_t a10 = target_alpha[ys[0] * 128 + xs[1]];
    const uint32_t a01 = target_alpha[ys[1] * 128 + xs[0]];
    const uint32_t a11 = target_alpha[ys[1] * 128 + xs[1]];
    return (a00 * (uint32_t)((16 - fu) * (16 - fv)) + a10 * (uint32_t)(fu * (16 - fv)) +
            a01 * (uint32_t)((16 - fu) * fv) + a11 * (uint32_t)(fu * fv)) >> 8;
}

/* The receiver pixel after texturing: MODULATE with the vertex colour
 * (0, 0, 0, a) and TCC 1 gives Cf = 0, Af = At * a >> 7; fog gives Cs =
 * (F * 0 + (255 - F) * FOGCOL) >> 8; TEST: alpha GREATER 0 (AFAIL KEEP),
 * then DATE/DATM 1 (destination alpha bit 7 set); ALPHA 0x44 with
 * COLCLAMP: C = clamp(((Cs - Cd) * As >> 7) + Cd, 0, 255); the written
 * alpha is As (FBA 0). Returns 0 when the pixel is not written. The
 * depth test (GEQUAL, no write) is the pipeline's. */
static inline int em_shadow_gs_receiver_pixel(uint32_t at, uint32_t a,
    uint32_t f, const uint32_t fogcol[3], const uint8_t dst[4], uint8_t out[4])
{
    uint32_t as = (at * a) >> 7;
    if (as > 255u) as = 255u;
    if (as == 0u) return 0;
    if (!(dst[3] & 0x80u)) return 0;
    for (unsigned k = 0; k < 3; ++k) {
        const int32_t cs = (int32_t)(((255u - f) * fogcol[k]) >> 8);
        const int32_t d = (cs - (int32_t)dst[k]) * (int32_t)as;
        int32_t c = (int32_t)floor((double)d / 128.0) + (int32_t)dst[k];
        out[k] = (uint8_t)(c < 0 ? 0 : c > 255 ? 255 : c);
    }
    out[3] = (uint8_t)as;
    return 1;
}

#endif /* EM_SHADOW_GS_H */
