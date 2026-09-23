/* em_vu1_shadow_clip.h — the two guard-band clip kernels of the player drop
 * shadow chain (001DA6A0), translated from their VU1 microcode:
 *
 *   00239C90  box clip kernel     (001DA310 runs it after every 00237180
 *             pass of the chunk27 box models 0x14 / 0x15; 1183 instructions,
 *             MPG blocks at micro 0x000..0x49F)
 *   0023E8A0  receiver clip kernel (001D5C80's re-pass of a class-2 level
 *             object after 0023C200; 1235 instructions, micro 0x000..0x4D3)
 *
 * Addresses in the comments are MICRO addresses (instruction index, 8 bytes
 * each) of the program the kernel packet uploads; docs/SHADOW_ORIGINAL.md
 * has the evidence. The reference test (tools/test_shadow_original_
 * reference.py, section G) executes the original VU1 instructions on every
 * clip batch of the captured chains and of the 15 AREA11 route beats, and
 * on synthetic batches that reach every branch, and requires every XGKICK
 * of this translation to equal the interpreter's: the same dmem address,
 * in the same order, the same packet bytes.
 *
 * What the kernels do (both share one structure; `R` = receiver only,
 * `B` = box only):
 *
 * 1. Loop (0x000..0x035 R / 0x000..0x03A B) over the 32 vertices of the
 *    batch at TOP: c = p x M (M = vf28..31, dmem 0..3 at the start), the
 *    guard rows dmem 1022/1023 (x' = x / 2040 - (256/255) w), CLIP into a
 *    24-bit history. Vertex i goes on to the clipping code when its data
 *    word (qword 3 lane w, low 16 bits) has none of the flag bits
 *    (R 0xA000, B 0x8000), the history of vertices i-2..i is non-zero
 *    (an AND test of the history's low 18 bits), the three are not all
 *    outside one guard plane (six clip-flag OR tests) and i >= 2. After the
 *    loop: GIF kick of dmem 1019.
 * 2. Clip entry (0x06A.. R / 0x068.. B): vi11/vi14/vi10 saved to dmem 250,
 *    GIF kick of dmem 1019, packet header at vi4 = 1185 - TOP: [dmem 1018,
 *    vertex i qword 0, dmem 1017]. Each of vertices i-2, i-1, i goes
 *    through the vertex routine with M = the four qwords at its data
 *    word's address (R: the ST matrix is the next four):
 *      R (0x037): c = p x M; Q = 1/c.w; t = p x T; RGBAQ = (0,0,0,
 *                 max(min(t.w, 8388863), 8388608)); XYZ slot = (c.xyz*Q,
 *                 c.w); ST slot = (t.xyz*Q, t.w).
 *      B (0x03B): c = p x M; Q = 1/c.w; RGBAQ slot = qword 2 x 128; XYZ
 *                 slot = (c.xyz*Q, c.w); ST slot = (qword 1 .x .y, 1, .w),
 *                 .xyz * Q.
 *    Vertex slots are 3 qwords (ST, RGBAQ, XYZ) from vi4+3; one triangle.
 * 3. Plane w = 0.1 (0x0C3.. R / 0x0B7.. B): by the signs of w - 0.1 of the
 *    three vertices: all behind aborts the entry (nothing kicked); one or
 *    two behind rebuild the triangle from the ORIGINAL vertices in clip
 *    space (camera = dmem 0..3) and project the clipped points (the
 *    one-behind case adds a second triangle). Then a back-face test: the
 *    sign of the screen cross product of the first triangle times the
 *    float value of vertex i's data word; negative aborts.
 * 4. Planes x = 4088, x = 4, y = 4088, y = 4 in screen space (0x10A..0x214
 *    R / 0x10E..0x218 B), each over the triangles present when the plane
 *    starts: one vertex out splits into two triangles (the new one is
 *    appended; more than 9 triangles aborts the entry), two out moves both,
 *    three out collapses the triangle onto (2048, 2048, 0, 0).
 * 5. Output (0x215.. R / 0x219.. B): per vertex XYZ -> fog F = max(min(A +
 *    B * w, 255), 0) (template row 1021 = (255, 2048, A, B)), (x, y, z, F)
 *    to fixed point with 4 fraction bits (B clamps z to 8388607 first); the
 *    RGBAQ slot to plain integers; the dmem 1017 copy gets NLOOP 3n | EOP;
 *    GIF kick of the packet at vi4. vi11/vi14/vi10 are
 *    restored and the loop goes on with the M the entry left in vf28..31
 *    (vertex i's matrix, or dmem 0..3 after a w-plane case).
 *
 * The packet: dmem 1018 is a one-register tag (R: NOP; B: TEX0_1, as
 * em_shadow_gs_clip_template holds and the reference test checks),
 * the data qword is vertex i's qword 0 as uploaded, and dmem 1017 is the
 * triangle-list tag (R: PRIM 0x07B, REGS ST RGBAQ XYZF2; B: PRIM 0x043,
 * REGS NOP NOP XYZF2). The template qwords are the ELF's D_00251750 (R) and
 * D_00251550 (B) +0x10..+0x40, copied by skin_arena_init (the test reads
 * them from the ELF and from every captured batch).
 *
 * Arithmetic follows the test's VU1 interpreter exactly: every register
 * read maps denormals to signed zero and the inf/NaN encodings to +-FLT_MAX;
 * every FMAC result is the binary32 nearest value truncated toward zero,
 * denormals flushed, overflow clamped to +-FLT_MAX; the product inside a
 * MADD/MSUB is truncated but not flushed before the sum; DIV by zero gives
 * +-FLT_MAX with the XOR of the signs; MIN/MAX return the (mapped) operand;
 * MOVE copies the raw word. Every flag the code reads (MAC sign, status S,
 * CLIP) is read four cycles after the instruction that produced it with no
 * other producer in between, and every Q use follows its DIV by at least
 * seven cycles, so program order is the data order.
 *
 * NOT established, therefore fail-stop: an FTOI whose result lies outside
 * int32 (the interpreter wraps it, the header em_shadow_gs.h saturates it;
 * the hardware result is unknown) and a kicked packet whose GIF tags do not
 * reach EOP within 64 tags. Both return -1 with `fault` set.
 *
 * Header-only (static inline) so that every backend and the tests share
 * one translation without a build-list change. Pure C: no GPU, no I/O. */
#ifndef EM_VU1_SHADOW_CLIP_H
#define EM_VU1_SHADOW_CLIP_H

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define EM_VU1_DMEM_QWORDS 1024u
/* 30 clip entries (i = 2..31) x (1019 kick + packet) + the final kick */
#define EM_VU1_CLIP_MAX_KICKS 61u
/* a packet: 3 header qwords + 9 triangles x 3 vertices x 3 qwords */
#define EM_VU1_CLIP_MAX_PACKET 84u
#define EM_VU1_CLIP_MAX_QWORDS (30u * (1u + EM_VU1_CLIP_MAX_PACKET) + 1u)

enum {
    EM_VU1_CLIP_RECEIVER = 0,   /* 0023E8A0 */
    EM_VU1_CLIP_BOX = 1         /* 00239C90 */
};

enum {
    EM_VU1_CLIP_OK = 0,
    EM_VU1_CLIP_FAULT_ARGS = 1,   /* NULL / unknown kernel */
    EM_VU1_CLIP_FAULT_FTOI = 2,   /* FTOI outside int32: not established */
    EM_VU1_CLIP_FAULT_GIF = 3,    /* kicked packet without EOP in 64 tags */
    EM_VU1_CLIP_FAULT_SPACE = 4   /* result buffer full (cannot happen for
                                     a 32-vertex batch; kept fail-stop) */
};

typedef struct { uint32_t w[4]; } EmVu1Qword;

typedef struct {
    uint32_t addr;     /* dmem qword address of the XGKICK */
    uint32_t vertex;   /* 32 - vi11 when kicked: the clip entry's vertex
                          i, 32 for the kick after the loop */
    uint32_t first;    /* first qword in EmVu1ClipResult.qw */
    uint32_t count;    /* qwords of the packet up to its EOP tag */
} EmVu1ClipKick;

typedef struct {
    uint32_t fault;                  /* EM_VU1_CLIP_FAULT_* */
    uint32_t entries;                /* clip entries reached (0x070 R /
                                        0x06E B), in loop order: */
    uint8_t entry[32];               /*   their vertex i */
    uint32_t kicks, qwords;
    EmVu1ClipKick kick[EM_VU1_CLIP_MAX_KICKS];
    EmVu1Qword qw[EM_VU1_CLIP_MAX_QWORDS];
} EmVu1ClipResult;

/* ------------------------------------------------------ VU arithmetic -- */

static inline float emvu_f(uint32_t w)
{
    float f;
    memcpy(&f, &w, sizeof f);
    return f;
}

static inline uint32_t emvu_w(float f)
{
    uint32_t w;
    memcpy(&w, &f, sizeof w);
    return w;
}

/* A register word as an operand. */
static inline float emvu_rd(uint32_t w)
{
    if ((w & 0x7F800000u) == 0u) return emvu_f(w & 0x80000000u);
    if ((w & 0x7F800000u) == 0x7F800000u)
        return emvu_f((w & 0x80000000u) | 0x7F7FFFFFu);
    return emvu_f(w);
}

/* Nearest binary32, then one step toward zero when that rounded away. A
 * value beyond binary32 clamps to +-FLT_MAX (the interpreter cannot
 * represent it; no batch reaches it). */
static inline float emvu_trunc(double x)
{
    float r = (float)x;
    if (isinf(r)) return r < 0.0f ? -FLT_MAX : FLT_MAX;
    if (fabs((double)r) > fabs(x)) r = emvu_f(emvu_w(r) - 1u);
    return r;
}

/* An FMAC result: truncated, denormals flushed to signed zero. */
static inline float emvu_fmac(double x)
{
    const float r = emvu_trunc(x);
    const uint32_t w = emvu_w(r);
    if ((w & 0x7F800000u) == 0u && (w & 0x7FFFFFu)) return emvu_f(w & 0x80000000u);
    return r;
}

/* ----------------------------------------------------------- the VU -- */

typedef struct {
    EmVu1Qword *m;          /* data memory (1024 qwords), read and written */
    uint32_t vf[32][4];
    float acc[4];
    float q, i;
    uint32_t vi[16];        /* 16-bit values */
    uint32_t cf;            /* clip flag history, 24 bits */
    uint32_t top;
    EmVu1ClipResult *out;
} EmVu1Clip;

#define EMVU_X 8u
#define EMVU_Y 4u
#define EMVU_Z 2u
#define EMVU_W 1u
#define EMVU_XYZ 14u
#define EMVU_XYZW 15u

static inline int emvu_lane(unsigned mask, unsigned c) { return (mask >> (3u - c)) & 1u; }

static inline void emvu_fix0(EmVu1Clip *u)
{
    u->vf[0][0] = u->vf[0][1] = u->vf[0][2] = 0u;
    u->vf[0][3] = 0x3F800000u;
    u->vi[0] = 0u;
}

/* vfT = dmem[a] (all lanes) / dmem[a] = vfS (all lanes); addresses wrap at 1024 qwords */
static inline void emvu_lq(EmVu1Clip *u, unsigned t, uint32_t a)
{
    if (t) memcpy(u->vf[t], u->m[a & 1023u].w, 16);
}

static inline void emvu_sq(EmVu1Clip *u, unsigned s, uint32_t a)
{
    memcpy(u->m[a & 1023u].w, u->vf[s], 16);
}

/* integer load: viT = the low 16 bits of lane w of dmem[a] */
static inline void emvu_ilw_w(EmVu1Clip *u, unsigned t, uint32_t a)
{
    if (t) u->vi[t] = u->m[a & 1023u].w[3] & 0xFFFFu;
}

/* integer store: dmem[a].lane = viT (low 16 bits) */
static inline void emvu_isw(EmVu1Clip *u, unsigned t, uint32_t a, unsigned lane)
{
    u->m[a & 1023u].w[lane] = u->vi[t] & 0xFFFFu;
}

static inline void emvu_set(EmVu1Clip *u, unsigned d, unsigned c, float v)
{
    if (d) u->vf[d][c] = emvu_w(v);
}

/* vfD = vfS - vfT / vfS + vfT in the masked lanes */
static inline void emvu_sub(EmVu1Clip *u, unsigned d, unsigned s, unsigned t, unsigned mask)
{
    float r[4];
    for (unsigned c = 0; c < 4; ++c)
        r[c] = emvu_fmac((double)emvu_rd(u->vf[s][c]) - (double)emvu_rd(u->vf[t][c]));
    for (unsigned c = 0; c < 4; ++c) if (emvu_lane(mask, c)) emvu_set(u, d, c, r[c]);
}

static inline void emvu_add(EmVu1Clip *u, unsigned d, unsigned s, unsigned t, unsigned mask)
{
    float r[4];
    for (unsigned c = 0; c < 4; ++c)
        r[c] = emvu_fmac((double)emvu_rd(u->vf[s][c]) + (double)emvu_rd(u->vf[t][c]));
    for (unsigned c = 0; c < 4; ++c) if (emvu_lane(mask, c)) emvu_set(u, d, c, r[c]);
}

/* vfD = vfS + vfT.bc in the masked lanes (broadcast lane bc) */
static inline void emvu_addbc(EmVu1Clip *u, unsigned d, unsigned s, unsigned t,
                              unsigned bc, unsigned mask)
{
    const double b = emvu_rd(u->vf[t][bc]);
    float r[4];
    for (unsigned c = 0; c < 4; ++c) r[c] = emvu_fmac((double)emvu_rd(u->vf[s][c]) + b);
    for (unsigned c = 0; c < 4; ++c) if (emvu_lane(mask, c)) emvu_set(u, d, c, r[c]);
}

/* vfD = vfS * Q / vfS * I / vfS + I in the masked lanes */
static inline void emvu_mulq(EmVu1Clip *u, unsigned d, unsigned s, unsigned mask)
{
    float r[4];
    for (unsigned c = 0; c < 4; ++c) r[c] = emvu_fmac((double)emvu_rd(u->vf[s][c]) * (double)u->q);
    for (unsigned c = 0; c < 4; ++c) if (emvu_lane(mask, c)) emvu_set(u, d, c, r[c]);
}

static inline void emvu_muli(EmVu1Clip *u, unsigned d, unsigned s, unsigned mask)
{
    float r[4];
    for (unsigned c = 0; c < 4; ++c) r[c] = emvu_fmac((double)emvu_rd(u->vf[s][c]) * (double)u->i);
    for (unsigned c = 0; c < 4; ++c) if (emvu_lane(mask, c)) emvu_set(u, d, c, r[c]);
}

static inline void emvu_addi(EmVu1Clip *u, unsigned d, unsigned s, unsigned mask)
{
    float r[4];
    for (unsigned c = 0; c < 4; ++c) r[c] = emvu_fmac((double)emvu_rd(u->vf[s][c]) + (double)u->i);
    for (unsigned c = 0; c < 4; ++c) if (emvu_lane(mask, c)) emvu_set(u, d, c, r[c]);
}

/* minibc / maxbc / minii: the operand kept unless the other is smaller /
 * larger (no flags, no rounding) */
static inline void emvu_mini_v(EmVu1Clip *u, unsigned d, unsigned s, float b, unsigned mask)
{
    for (unsigned c = 0; c < 4; ++c) {
        if (!emvu_lane(mask, c)) continue;
        const float a = emvu_rd(u->vf[s][c]);
        emvu_set(u, d, c, b < a ? b : a);
    }
}

static inline void emvu_max_v(EmVu1Clip *u, unsigned d, unsigned s, float b, unsigned mask)
{
    for (unsigned c = 0; c < 4; ++c) {
        if (!emvu_lane(mask, c)) continue;
        const float a = emvu_rd(u->vf[s][c]);
        emvu_set(u, d, c, b > a ? b : a);
    }
}

/* vfT = vfS in the masked lanes: raw words */
static inline void emvu_move(EmVu1Clip *u, unsigned t, unsigned s, unsigned mask)
{
    uint32_t src[4];
    memcpy(src, u->vf[s], 16);
    for (unsigned c = 0; c < 4; ++c) if (emvu_lane(mask, c) && t) u->vf[t][c] = src[c];
}

/* The accumulate chain over rows vfM..vfM+2 plus row vfM+3 times w = 1:
 * vfD = p x [vfM..vfM+3] (row-vector, w = 1). */
static inline void emvu_xform(EmVu1Clip *u, unsigned d, unsigned mreg, unsigned p)
{
    for (unsigned c = 0; c < 4; ++c)
        u->acc[c] = emvu_fmac((double)emvu_rd(u->vf[mreg][c]) * (double)emvu_rd(u->vf[p][0]));
    for (unsigned k = 1; k < 3; ++k)
        for (unsigned c = 0; c < 4; ++c) {
            const float prod = emvu_trunc((double)emvu_rd(u->vf[mreg + k][c]) *
                                          (double)emvu_rd(u->vf[p][k]));
            u->acc[c] = emvu_fmac((double)u->acc[c] + (double)prod);
        }
    float r[4];
    for (unsigned c = 0; c < 4; ++c) {
        const float prod = emvu_trunc((double)emvu_rd(u->vf[mreg + 3][c]) *
                                      (double)emvu_rd(u->vf[0][3]));
        r[c] = emvu_fmac((double)u->acc[c] + (double)prod);
    }
    for (unsigned c = 0; c < 4; ++c) emvu_set(u, d, c, r[c]);
}

/* div Q, vfS.fs, vfT.ft */
static inline void emvu_div(EmVu1Clip *u, uint32_t num, uint32_t den)
{
    const float a = emvu_rd(num), b = emvu_rd(den);
    if (b == 0.0f) {
        const int neg = (signbit(a) != 0) != (signbit(b) != 0);
        u->q = neg ? -FLT_MAX : FLT_MAX;
        return;
    }
    u->q = emvu_fmac((double)a / (double)b);
}

/* clipw.xyz vfS, vfS.w */
static inline void emvu_clipw(EmVu1Clip *u, unsigned s)
{
    const float w = fabsf(emvu_rd(u->vf[s][3]));
    uint32_t f = 0;
    for (unsigned c = 0; c < 3; ++c) {
        const float x = emvu_rd(u->vf[s][c]);
        if (x > w) f |= 1u << (2u * c);
        if (x < -w) f |= 2u << (2u * c);
    }
    u->cf = ((u->cf << 6) | f) & 0xFFFFFFu;
}

/* The MAC sign of a lane just written by sub/mulbc (fmand with one sign
 * bit set): the sign bit of the result word. */
static inline uint32_t emvu_sign(const EmVu1Clip *u, unsigned r, unsigned c)
{
    return u->vf[r][c] >> 31;
}

/* vfT.xyzw = vfS.xyzw converted to int32 with 0 or 4 fraction bits. Returns -1 outside int32. */
static inline int emvu_ftoi(EmVu1Clip *u, unsigned t, unsigned s, double scale)
{
    uint32_t r[4];
    for (unsigned c = 0; c < 4; ++c) {
        const double x = trunc((double)emvu_rd(u->vf[s][c]) * scale);
        if (!(x >= -2147483648.0 && x < 2147483648.0)) return -1;
        r[c] = (uint32_t)(int32_t)x;
    }
    if (t) memcpy(u->vf[t], r, 16);
    return 0;
}

/* xgkick: the GIF packet at `a` up to its EOP tag (PACKED NLOOP x NREG,
 * REGLIST (NLOOP x NREG + 1) / 2, IMAGE NLOOP qwords), snapshotted. */
static inline int emvu_xgkick(EmVu1Clip *u, uint32_t a)
{
    EmVu1ClipResult *o = u->out;
    uint32_t q = a;
    int eop = 0;
    for (unsigned t = 0; t < 64 && !eop; ++t) {
        const EmVu1Qword *tag = &u->m[q & 1023u];
        const uint32_t nloop = tag->w[0] & 0x7FFFu;
        const uint32_t flg = (tag->w[1] >> 26) & 3u;
        uint32_t nreg = tag->w[1] >> 28;
        if (!nreg) nreg = 16u;
        eop = (tag->w[0] >> 15) & 1u;
        q += 1u + (flg == 0u ? nloop * nreg : flg == 1u ? (nloop * nreg + 1u) / 2u : nloop);
    }
    if (!eop) { o->fault = EM_VU1_CLIP_FAULT_GIF; return -1; }
    const uint32_t count = q - a;
    if (o->kicks >= EM_VU1_CLIP_MAX_KICKS || o->qwords + count > EM_VU1_CLIP_MAX_QWORDS) {
        o->fault = EM_VU1_CLIP_FAULT_SPACE;
        return -1;
    }
    EmVu1ClipKick *k = &o->kick[o->kicks++];
    k->addr = a & 1023u;
    k->vertex = 32u - u->vi[11];
    k->first = o->qwords;
    k->count = count;
    for (uint32_t j = 0; j < count; ++j) o->qw[o->qwords++] = u->m[(a + j) & 1023u];
    return 0;
}

/* ------------------------------------------------ the vertex routines -- */

/* R 0x037..0x053: vf1 = qword 3 of the vertex, vf28..31 its camera,
 * vf24..27 its ST matrix; the three slots at vi12. */
static inline void emvu_recv_vertex(EmVu1Clip *u)
{
    emvu_xform(u, 2, 28, 1);                              /* 0x037..0x03A */
    emvu_div(u, u->vf[0][3], u->vf[2][3]);                /* 0x03E */
    emvu_xform(u, 3, 24, 1);                              /* 0x03F..0x042 */
    emvu_mini_v(u, 4, 3, emvu_rd(u->vf[9][0]), EMVU_W);   /* 0x046 minix.w */
    emvu_move(u, 4, 0, EMVU_XYZ);                         /* 0x047 */
    emvu_max_v(u, 4, 4, emvu_rd(u->vf[9][1]), EMVU_W);    /* 0x04A maxy.w */
    emvu_mulq(u, 2, 2, EMVU_XYZ);                         /* 0x04C */
    emvu_mulq(u, 3, 3, EMVU_XYZ);                         /* 0x04D */
    emvu_sq(u, 4, u->vi[12] + 1u);                        /* 0x04F */
    emvu_sq(u, 2, u->vi[12] + 2u);
    emvu_sq(u, 3, u->vi[12] + 0u);
}

/* B 0x03B..0x051: vf1 = qword 3, vf4 = qword 2, vf3 = qword 1. */
static inline void emvu_box_vertex(EmVu1Clip *u)
{
    emvu_xform(u, 1, 28, 1);                              /* 0x03B..0x03E */
    emvu_sub(u, 3, 3, 3, EMVU_Z);                         /* 0x03F */
    u->i = 128.0f;                                        /* 0x040 I */
    emvu_muli(u, 4, 4, EMVU_XYZW);                        /* 0x041 */
    emvu_div(u, u->vf[0][3], u->vf[1][3]);                /* 0x042 */
    emvu_addbc(u, 3, 3, 0, 3, EMVU_Z);                    /* 0x043 z += 1 */
    emvu_mulq(u, 1, 1, EMVU_XYZ);                         /* 0x04A */
    emvu_mulq(u, 3, 3, EMVU_XYZ);                         /* 0x04B */
    emvu_sq(u, 4, u->vi[12] + 1u);                        /* 0x04D */
    emvu_sq(u, 1, u->vi[12] + 2u);
    emvu_sq(u, 3, u->vi[12] + 0u);
}

/* The camera of an original vertex (the w-plane cases): an integer load of
 * its data word (unused), vf28..31 = dmem 0..3, vfD = vfD x camera. */
static inline void emvu_camera0(EmVu1Clip *u, unsigned d)
{
    for (unsigned k = 0; k < 4; ++k) emvu_lq(u, 28 + k, k);
    emvu_xform(u, d, 28, d);
}

/* ---------------------------------------------------- plane w = 0.1 -- */

/* The attributes of the three original vertices in vf10..12 (qword 3),
 * vf13..15 (qword 1), vf16..18 (qword 2), before their camera transform:
 * R 0x383..0x39D / 0x42A..0x444, B 0x389..0x38C / 0x40D..0x410. */
static inline void emvu_w_attributes(EmVu1Clip *u, int box)
{
    if (box) {
        u->i = 128.0f;
        emvu_muli(u, 16, 16, EMVU_XYZW);
        emvu_muli(u, 17, 17, EMVU_XYZW);
        emvu_muli(u, 18, 18, EMVU_XYZW);
        return;
    }
    emvu_xform(u, 13, 24, 10);
    emvu_xform(u, 14, 24, 11);
    emvu_xform(u, 15, 24, 12);
    for (unsigned k = 0; k < 3; ++k) {
        emvu_mini_v(u, 16 + k, 13 + k, emvu_rd(u->vf[9][0]), EMVU_W);
        emvu_move(u, 16 + k, 0, EMVU_XYZ);
        emvu_max_v(u, 16 + k, 16 + k, emvu_rd(u->vf[9][1]), EMVU_W);
    }
}

/* vf1 = vfA - vfB, vf3 = vfA+3 - vfB+3, vf4 = vfA+6 - vfB+6; Q =
 * (vf23.lane - vfB.lane) / vf1.lane; vf1/3/4 *= Q. */
static inline void emvu_edge(EmVu1Clip *u, unsigned a, unsigned b, unsigned lane,
                             unsigned mask)
{
    emvu_sub(u, 1, a, b, EMVU_XYZW);
    emvu_sub(u, 3, a + 3, b + 3, EMVU_XYZW);
    emvu_sub(u, 4, a + 6, b + 6, EMVU_XYZW);
    emvu_sub(u, 2, 23, b, mask);
    emvu_div(u, u->vf[2][lane], u->vf[1][lane]);
    emvu_mulq(u, 1, 1, EMVU_XYZW);
    emvu_mulq(u, 3, 3, EMVU_XYZW);
    emvu_mulq(u, 4, 4, EMVU_XYZW);
}

/* Project a clip-space point vfP and its ST vfS: Q = 1/w, P.xyz *= Q,
 * S.z = 1, S.xyz *= Q. `zero_first`: S.z = S.z - S.z, then S.z += 1 (R
 * and the B one-behind case); else S.z = 0 + 1 from the constant register
 * (B two-behind case). */
static inline void emvu_project(EmVu1Clip *u, unsigned p, unsigned s, int zero_first)
{
    emvu_div(u, u->vf[0][3], u->vf[p][3]);
    emvu_mulq(u, p, p, EMVU_XYZ);
    if (zero_first) {
        emvu_sub(u, s, s, s, EMVU_Z);
        emvu_addbc(u, s, s, 0, 3, EMVU_Z);
    } else {
        emvu_addbc(u, s, 0, 0, 3, EMVU_Z);
    }
    emvu_mulq(u, s, s, EMVU_XYZ);
}

/* Load the three original vertices (bases b10, b11, b12 = vertex i-2/i-1/i
 * in some order): R 0x37A / 0x421, B 0x380 / 0x404. */
static inline void emvu_w_load(EmVu1Clip *u, uint32_t b10, uint32_t b11, uint32_t b12, int box)
{
    const uint32_t b[3] = { b10, b11, b12 };
    for (unsigned k = 0; k < 3; ++k) {
        emvu_lq(u, 10 + k, b[k] + 3u);
        emvu_lq(u, 13 + k, b[k] + 1u);
        emvu_lq(u, 16 + k, b[k] + 2u);
    }
    emvu_w_attributes(u, box);
    for (unsigned k = 0; k < 3; ++k) {
        emvu_ilw_w(u, 10 + k, b[k] + 3u);    /* vi10..12 = the data words */
        emvu_camera0(u, 10 + k);
    }
}

/* Two vertices behind (R L37A, B L380): vf10 is the vertex in front;
 * slots s1, s2, s3 receive vf10, vf11, vf12. */
static inline void emvu_w_two(EmVu1Clip *u, uint32_t s1, uint32_t s2, uint32_t s3,
                              uint32_t b10, uint32_t b11, uint32_t b12, int box)
{
    emvu_w_load(u, b10, b11, b12, box);
    emvu_edge(u, 10, 12, 3, EMVU_W);                     /* 0x3C2 / 0x3B1 */
    emvu_add(u, 12, 1, 12, EMVU_XYZW);
    emvu_add(u, 15, 3, 15, EMVU_XYZW);
    emvu_add(u, 18, 4, 18, EMVU_XYZW);
    emvu_edge(u, 10, 11, 3, EMVU_W);                     /* 0x3CE / 0x3BD */
    emvu_add(u, 11, 1, 11, EMVU_XYZW);
    emvu_add(u, 14, 3, 14, EMVU_XYZW);
    emvu_add(u, 17, 4, 17, EMVU_XYZW);
    emvu_project(u, 10, 13, !box);                       /* 0x3DA / 0x3C9 */
    emvu_project(u, 11, 14, !box);
    emvu_project(u, 12, 15, !box);
    emvu_sq(u, 10, s1 + 2u); emvu_sq(u, 11, s2 + 2u); emvu_sq(u, 12, s3 + 2u);
    emvu_sq(u, 13, s1 + 0u); emvu_sq(u, 14, s2 + 0u); emvu_sq(u, 15, s3 + 0u);
    emvu_sq(u, 16, s1 + 1u); emvu_sq(u, 17, s2 + 1u); emvu_sq(u, 18, s3 + 1u);
}

/* One vertex behind (R L421, B L404): vf10 is the vertex behind. The
 * point on edge 10-12 goes to a new triangle at vi9 with the point on
 * edge 10-11 and vertex 12. */
static inline void emvu_w_one(EmVu1Clip *u, uint32_t s1, uint32_t s2, uint32_t s3,
                              uint32_t b10, uint32_t b11, uint32_t b12, int box)
{
    emvu_w_load(u, b10, b11, b12, box);
    emvu_edge(u, 10, 12, 3, EMVU_W);                     /* 0x469 / 0x435 */
    emvu_add(u, 1, 1, 12, EMVU_XYZW);
    emvu_add(u, 3, 3, 15, EMVU_XYZW);
    emvu_add(u, 2, 4, 18, EMVU_XYZW);
    emvu_project(u, 1, 3, 1);                            /* 0x475 / 0x441 */
    emvu_sq(u, 1, u->vi[9] + 2u);
    emvu_sq(u, 2, u->vi[9] + 1u);
    emvu_sq(u, 3, u->vi[9] + 0u);
    emvu_edge(u, 10, 11, 3, EMVU_W);                     /* 0x484 / 0x450 */
    emvu_add(u, 10, 1, 11, EMVU_XYZW);
    emvu_add(u, 13, 3, 14, EMVU_XYZW);
    emvu_add(u, 16, 4, 17, EMVU_XYZW);
    emvu_project(u, 10, 13, 1);                          /* 0x490 / 0x45C */
    emvu_project(u, 11, 14, 1);
    emvu_project(u, 12, 15, 1);
    emvu_sq(u, 10, s1 + 2u); emvu_sq(u, 11, s2 + 2u); emvu_sq(u, 12, s3 + 2u);
    emvu_sq(u, 13, s1 + 0u); emvu_sq(u, 14, s2 + 0u); emvu_sq(u, 15, s3 + 0u);
    emvu_sq(u, 16, s1 + 1u); emvu_sq(u, 17, s2 + 1u); emvu_sq(u, 18, s3 + 1u);
    emvu_sq(u, 10, u->vi[9] + 5u); emvu_sq(u, 13, u->vi[9] + 3u); emvu_sq(u, 16, u->vi[9] + 4u);
    emvu_sq(u, 12, u->vi[9] + 8u); emvu_sq(u, 15, u->vi[9] + 6u); emvu_sq(u, 18, u->vi[9] + 7u);
    u->vi[9] = (u->vi[9] + 9u) & 0xFFFFu;
    u->vi[8] = (u->vi[8] + 1u) & 0xFFFFu;
}

/* --------------------------------------------------- screen planes -- */

/* The slots of triangle vi7 in vf10..12 (XYZ), vf13..15 (ST), vf16..18
 * (RGBAQ): R 0x275 / 0x2AD / 0x2F3 / 0x32B. */
static inline void emvu_s_load(EmVu1Clip *u, uint32_t a10, uint32_t a11, uint32_t a12)
{
    const uint32_t a[3] = { a10, a11, a12 };
    for (unsigned k = 0; k < 3; ++k) emvu_lq(u, 10 + k, a[k] + 2u);
    for (unsigned k = 0; k < 3; ++k) emvu_lq(u, 13 + k, a[k] + 0u);
    for (unsigned k = 0; k < 3; ++k) emvu_lq(u, 16 + k, a[k] + 1u);
}

/* Two out (L275 / L2F3): vf10 in, vf11 and vf12 move onto the plane. */
static inline void emvu_s_two(EmVu1Clip *u, uint32_t a10, uint32_t a11, uint32_t a12,
                              unsigned lane)
{
    const unsigned mask = 8u >> lane;
    emvu_s_load(u, a10, a11, a12);
    emvu_edge(u, 10, 11, lane, mask);
    emvu_add(u, 11, 1, 11, EMVU_XYZW);
    emvu_add(u, 14, 3, 14, EMVU_XYZW);
    emvu_add(u, 17, 4, 17, EMVU_XYZW);
    emvu_edge(u, 10, 12, lane, mask);
    emvu_add(u, 12, 1, 12, EMVU_XYZW);
    emvu_add(u, 15, 3, 15, EMVU_XYZW);
    emvu_add(u, 18, 4, 18, EMVU_XYZW);
    emvu_sq(u, 11, a11 + 2u); emvu_sq(u, 12, a12 + 2u);
    emvu_sq(u, 14, a11 + 0u); emvu_sq(u, 15, a12 + 0u);
    emvu_sq(u, 17, a11 + 1u); emvu_sq(u, 18, a12 + 1u);
}

/* One out (L2AD / L32B): vf10 out. Returns 1 when the triangle count
 * passes 9 (the entry aborts, L25D). The edge-10-12 point (vf1 =
 * vf12 - vf10: the operands are the other way round here) is the new
 * triangle's first vertex. */
static inline int emvu_s_one(EmVu1Clip *u, uint32_t a10, uint32_t a11, uint32_t a12,
                             unsigned lane)
{
    const unsigned mask = 8u >> lane;
    emvu_s_load(u, a10, a11, a12);
    emvu_edge(u, 12, 10, lane, mask);                    /* vf12 - vf10 */
    emvu_add(u, 1, 1, 10, EMVU_XYZW);
    emvu_add(u, 3, 3, 13, EMVU_XYZW);
    emvu_add(u, 4, 4, 16, EMVU_XYZW);
    emvu_sq(u, 1, u->vi[9] + 2u);
    emvu_sq(u, 4, u->vi[9] + 1u);
    emvu_sq(u, 3, u->vi[9] + 0u);
    emvu_edge(u, 11, 10, lane, mask);                    /* vf11 - vf10 */
    emvu_add(u, 10, 1, 10, EMVU_XYZW);
    emvu_add(u, 13, 3, 13, EMVU_XYZW);
    emvu_add(u, 16, 4, 16, EMVU_XYZW);
    emvu_sq(u, 10, a10 + 2u); emvu_sq(u, 13, a10 + 0u); emvu_sq(u, 16, a10 + 1u);
    emvu_sq(u, 10, u->vi[9] + 5u); emvu_sq(u, 13, u->vi[9] + 3u); emvu_sq(u, 16, u->vi[9] + 4u);
    emvu_sq(u, 12, u->vi[9] + 8u); emvu_sq(u, 15, u->vi[9] + 6u); emvu_sq(u, 18, u->vi[9] + 7u);
    u->vi[9] = (u->vi[9] + 9u) & 0xFFFFu;
    u->vi[8] = (u->vi[8] + 1u) & 0xFFFFu;
    return (int16_t)(uint16_t)(u->vi[8] - 9u) > 0;
}

/* One screen plane over the triangles present at its start (R 0x10A..,
 * 0x158.., 0x197.., 0x1D6..; B 0x10E.., 0x15C.., 0x19B.., 0x1DA..).
 * `lane` 0 = x, 1 = y; `limit` 4088 or 4; `high`: outside when limit - v
 * < 0 (else v - limit < 0). Returns 1 when the entry aborts. */
static inline int emvu_s_plane(EmVu1Clip *u, unsigned lane, float limit, int high)
{
    const unsigned mask = 8u >> lane;
    uint32_t vi7 = u->vi[4] + 3u;
    int32_t n = (int32_t)u->vi[8];
    emvu_sub(u, 23, 23, 23, EMVU_XYZW);
    u->i = limit;
    emvu_addi(u, 23, 23, EMVU_XYZW);
    do {
        --n;
        emvu_lq(u, 1, vi7 + 2u);
        emvu_lq(u, 2, vi7 + 5u);
        emvu_lq(u, 3, vi7 + 8u);
        /* vi3 = sign(vf3) * 4 + sign(vf2) * 2 + sign(vf1) (fmand of the
         * MAC sign of each sub, 4 cycles after it): bit 2 = slot C
         * (vi7+6, loaded from vi7+8), bit 1 = slot B, bit 0 = slot A. */
        uint32_t oc = 0;
        const unsigned regs[3] = { 3, 2, 1 };
        for (unsigned k = 0; k < 3; ++k) {
            const unsigned r = regs[k];
            if (high) emvu_sub(u, r, 23, r, mask);   /* limit - v */
            else emvu_sub(u, r, r, 23, mask);        /* v - limit */
            oc = oc * 2u + emvu_sign(u, r, lane);
        }
        const uint32_t A = vi7, B = vi7 + 3u, C = vi7 + 6u;
        if (oc == 7u) {                                  /* L4C7 / L493 */
            emvu_sub(u, 1, 1, 1, EMVU_XYZW);
            u->i = 2048.0f;
            emvu_addi(u, 1, 1, EMVU_X | EMVU_Y);
            emvu_sq(u, 1, A + 2u); emvu_sq(u, 1, B + 2u); emvu_sq(u, 1, C + 2u);
        } else if (oc == 3u) {                           /* A, B out */
            emvu_s_two(u, C, A, B, lane);
        } else if (oc == 5u) {                           /* A, C out */
            emvu_s_two(u, B, C, A, lane);
        } else if (oc == 6u) {                           /* B, C out */
            emvu_s_two(u, A, B, C, lane);
        } else if (oc == 1u) {                           /* A out */
            if (emvu_s_one(u, A, B, C, lane)) return 1;
        } else if (oc == 2u) {                           /* B out */
            if (emvu_s_one(u, B, C, A, lane)) return 1;
        } else if (oc == 4u) {                           /* C out */
            if (emvu_s_one(u, C, A, B, lane)) return 1;
        }
        vi7 += 9u;
    } while (n != 0);
    return 0;
}

/* vfT.w = dmem[a].w (B 0x0FB loads only lane w) */
static inline void emvu_lq_w(EmVu1Clip *u, unsigned t, uint32_t a)
{
    if (t) u->vf[t][3] = u->m[a & 1023u].w[3];
}

/* The clip-flag OR test: every CLIP bit outside `imm` is set. */
static inline int emvu_fcor(uint32_t cf, uint32_t imm)
{
    return ((cf | imm) & 0xFFFFFFu) == 0xFFFFFFu;
}

/* The six plane tests (R 0x054..0x065, B 0x055..0x066): 1 when vertices
 * i-2..i are all outside one guard plane. */
static inline int emvu_rejected(uint32_t cf)
{
    return emvu_fcor(cf, 0xFFEFBEu) || emvu_fcor(cf, 0xFFDF7Du) ||
           emvu_fcor(cf, 0xFFBEFBu) || emvu_fcor(cf, 0xFF7DF7u) ||
           emvu_fcor(cf, 0xFEFBEFu) || emvu_fcor(cf, 0xFDF7DFu);
}

/* L25D / L263: vf15 = dmem 1021; vi11, vi14, vi10 from dmem 250. */
static inline void emvu_restore(EmVu1Clip *u)
{
    emvu_lq(u, 15, 1021u);
    u->vi[11] = u->m[250].w[0] & 0xFFFFu;
    u->vi[14] = u->m[250].w[1] & 0xFFFFu;
    u->vi[10] = u->m[250].w[2] & 0xFFFFu;
}

/* The clip entry for vertex i (vi14 = its base, vi11 = 32 - i). Returns
 * -1 on a fault, else 0 (kicked or aborted). */
static inline int emvu_entry(EmVu1Clip *u, int box)
{
    EmVu1ClipResult *o = u->out;
    /* R 0x06A / B 0x068: save, then xtop */
    emvu_isw(u, 11, 250u, 0);
    emvu_isw(u, 14, 250u, 1);
    emvu_isw(u, 10, 250u, 2);
    o->entry[o->entries++] = (uint8_t)(32u - u->vi[11]);
    u->vi[1] = u->top;                                   /* R 0x070 / B 0x06E */
    u->vi[4] = (1185u - u->vi[1]) & 0xFFFFu;
    u->vi[1] = 1019u;
    if (emvu_xgkick(u, 1019u)) return -1;                /* R 0x07A / B 0x078 */
    emvu_lq(u, 1, 1018u); emvu_sq(u, 1, u->vi[4] + 0u);
    emvu_lq(u, 1, u->vi[14]); emvu_sq(u, 1, u->vi[4] + 1u);
    emvu_lq(u, 1, 1017u); emvu_sq(u, 1, u->vi[4] + 2u);
    /* the three vertices (R 0x082..0x0B8, B 0x080..0x0AF) */
    static const int32_t word[3] = { -5, -1, 3 };
    for (unsigned k = 0; k < 3; ++k) {
        const uint32_t base = u->vi[14] + (uint32_t)(word[k] - 3);
        emvu_ilw_w(u, 10, base + 3u);
        for (unsigned r = 0; r < 4; ++r) emvu_lq(u, 28 + r, u->vi[10] + r);
        if (!box && k == 0) {
            for (unsigned r = 0; r < 4; ++r) emvu_lq(u, 24 + r, u->vi[10] + 4u + r);
            u->i = 8388863.0f;                           /* 0x08E I */
            emvu_addi(u, 9, 0, EMVU_X);                  /* 0x08F */
            u->i = 8388608.0f;                           /* 0x08F I */
            emvu_addi(u, 9, 0, EMVU_Y);                  /* 0x090 */
        }
        emvu_lq(u, 1, base + 3u);
        emvu_lq(u, 4, base + 2u);
        emvu_lq(u, 3, base + 1u);
        u->vi[12] = (u->vi[4] + 3u + 3u * k) & 0xFFFFu;
        if (box) emvu_box_vertex(u);
        else emvu_recv_vertex(u);
    }
    u->vi[8] = 1u;
    u->vi[9] = (u->vi[4] + 12u) & 0xFFFFu;
    u->vi[7] = (u->vi[4] + 3u) & 0xFFFFu;
    emvu_sub(u, 23, 23, 23, EMVU_XYZW);                  /* R 0x0C0 / B 0x0B7 */
    u->i = emvu_f(0x3DCCCCCDu);                          /* 0.1 */
    emvu_addi(u, 23, 23, EMVU_XYZW);
    /* plane w = 0.1: vi3 = s(vf3.w) * 4 + s(vf2.w) * 2 + s(vf1.w) */
    const uint32_t A = u->vi[7], B = A + 3u, C = A + 6u, v = u->vi[14];
    emvu_lq(u, 3, A + 8u);
    emvu_lq(u, 2, A + 5u);
    emvu_lq(u, 1, A + 2u);
    uint32_t wc = 0;
    static const unsigned wr[3] = { 3, 2, 1 };
    for (unsigned k = 0; k < 3; ++k) {
        emvu_sub(u, wr[k], wr[k], 23, EMVU_W);
        wc = wc * 2u + emvu_sign(u, wr[k], 3);
    }
    switch (wc) {
    case 7u: emvu_restore(u); return 0;                  /* L25D / L263 */
    case 3u: emvu_w_two(u, C, A, B, v, v - 8u, v - 4u, box); break;
    case 5u: emvu_w_two(u, B, C, A, v - 4u, v, v - 8u, box); break;
    case 6u: emvu_w_two(u, A, B, C, v - 8u, v - 4u, v, box); break;
    case 1u: emvu_w_one(u, A, B, C, v - 8u, v - 4u, v, box); break;
    case 2u: emvu_w_one(u, B, C, A, v - 4u, v, v - 8u, box); break;
    case 4u: emvu_w_one(u, C, A, B, v, v - 8u, v - 4u, box); break;
    default: break;
    }
    /* back face (R 0x0F3..0x108, B 0x0F7..0x10C) */
    u->vi[1] = u->m[250].w[1] & 0xFFFFu;
    emvu_lq(u, 2, u->vi[4] + 5u);
    emvu_lq(u, 6, u->vi[4] + 8u);
    emvu_lq(u, 1, u->vi[4] + 11u);
    if (box) emvu_lq_w(u, 5, u->vi[1] + 3u);
    else emvu_lq(u, 5, u->vi[1] + 3u);
    emvu_sub(u, 9, 6, 2, EMVU_X | EMVU_Y);
    emvu_sub(u, 4, 1, 6, EMVU_X | EMVU_Y);
    u->acc[0] = emvu_fmac((double)emvu_rd(u->vf[4][0]) * (double)emvu_rd(u->vf[9][1]));
    {
        const float prod = emvu_trunc((double)emvu_rd(u->vf[9][0]) *
                                      (double)emvu_rd(u->vf[4][1]));
        emvu_set(u, 9, 0, emvu_fmac((double)u->acc[0] - (double)prod));
    }
    emvu_set(u, 9, 0, emvu_fmac((double)emvu_rd(u->vf[9][0]) * (double)emvu_rd(u->vf[5][3])));
    if (emvu_sign(u, 9, 0)) { emvu_restore(u); return 0; }   /* fsand 0x2 */
    /* the four screen planes */
    if (emvu_s_plane(u, 0, 4088.0f, 1) || emvu_s_plane(u, 0, 4.0f, 0) ||
        emvu_s_plane(u, 1, 4088.0f, 1) || emvu_s_plane(u, 1, 4.0f, 0)) {
        emvu_restore(u);
        return 0;
    }
    /* output (R 0x215..0x24F, B 0x219..0x255) */
    emvu_lq(u, 15, 1021u);
    uint32_t n = 3u * u->vi[8];
    uint32_t vi7 = u->vi[4] + 3u;
    do {
        --n;
        emvu_lq(u, 1, vi7 + 2u);
        if (box) {
            u->i = 8388607.0f;                           /* B 0x221 I */
            emvu_mini_v(u, 1, 1, u->i, EMVU_Z);          /* minii.z */
        }
        u->acc[3] = emvu_fmac((double)emvu_rd(u->vf[0][3]) * (double)emvu_rd(u->vf[15][2]));
        {
            const float prod = emvu_trunc((double)emvu_rd(u->vf[15][3]) *
                                          (double)emvu_rd(u->vf[1][3]));
            emvu_set(u, 1, 3, emvu_fmac((double)u->acc[3] + (double)prod));
        }
        emvu_mini_v(u, 1, 1, emvu_rd(u->vf[15][0]), EMVU_W);
        emvu_max_v(u, 1, 1, emvu_rd(u->vf[0][0]), EMVU_W);
        if (emvu_ftoi(u, 1, 1, 16.0)) { o->fault = EM_VU1_CLIP_FAULT_FTOI; return -1; }
        emvu_sq(u, 1, vi7 + 2u);
        emvu_lq(u, 4, vi7 + 1u);
        if (emvu_ftoi(u, 4, 4, 1.0)) { o->fault = EM_VU1_CLIP_FAULT_FTOI; return -1; }
        emvu_sq(u, 4, vi7 + 1u);
        vi7 += 3u;
    } while (n != 0);
    u->vi[14] = (u->vi[4] + 2u) & 0xFFFFu;
    u->vi[1] = (3u * u->vi[8] + 0x8000u) & 0xFFFFu;
    emvu_isw(u, 1, u->vi[14], 0);
    u->vi[14] = u->vi[4];
    if (emvu_xgkick(u, u->vi[14])) return -1;            /* R 0x24F / B 0x255 */
    emvu_restore(u);
    return 0;
}

/* Run the kernel on the batch at `top` over `dmem` (the VU1 data memory at
 * its MSCAL/MSCNT: dmem 0..7 matrices, 1017..1023 template rows, 250 and
 * the working area at 1185 - top are written as the original writes them).
 * `out` receives every XGKICK in order. Returns 0, or -1 with out->fault. */
static inline int em_vu1_shadow_clip_run(int kernel, EmVu1Qword *dmem, uint32_t top,
                                         EmVu1ClipResult *out)
{
    if (!out) return -1;
    out->fault = EM_VU1_CLIP_OK;
    out->entries = out->kicks = out->qwords = 0;
    if (!dmem || (kernel != EM_VU1_CLIP_RECEIVER && kernel != EM_VU1_CLIP_BOX)) {
        out->fault = EM_VU1_CLIP_FAULT_ARGS;
        return -1;
    }
    const int box = kernel == EM_VU1_CLIP_BOX;
    const uint32_t flags = box ? 0x8000u : 0xA000u;
    EmVu1Clip u;
    memset(&u, 0, sizeof u);
    u.m = dmem;
    u.out = out;
    u.top = top & 0xFFFFu;
    emvu_fix0(&u);
    /* 0x000..0x00E (B ..0x012) */
    u.vi[14] = u.top;
    u.vi[11] = 32u;
    emvu_lq(&u, 15, 1021u);
    u.cf = 0u;                                           /* clip flags = 0 */
    u.vi[15] = flags;
    emvu_lq(&u, 7, 1022u);
    emvu_lq(&u, 8, 1023u);
    emvu_lq(&u, 19, 1021u);
    emvu_lq(&u, 5, u.vi[14] + 3u);
    if (box)
        for (unsigned r = 0; r < 4; ++r) emvu_lq(&u, 20 + r, 4u + r);
    for (unsigned r = 0; r < 4; ++r) emvu_lq(&u, 28 + r, r);
    for (;;) {
        /* R L00F / B L013 */
        emvu_xform(&u, 2, 28, 5);
        {
            float acc[4], r[4];
            for (unsigned c = 0; c < 4; ++c)           /* ACC = vf7 * vf2, per lane */
                acc[c] = emvu_fmac((double)emvu_rd(u.vf[7][c]) * (double)emvu_rd(u.vf[2][c]));
            for (unsigned c = 0; c < 4; ++c) {         /* vf3 = ACC + vf8 * vf2.w */
                const float prod = emvu_trunc((double)emvu_rd(u.vf[8][c]) *
                                              (double)emvu_rd(u.vf[2][3]));
                r[c] = emvu_fmac((double)acc[c] + (double)prod);
            }
            memcpy(u.acc, acc, sizeof acc);
            for (unsigned c = 0; c < 4; ++c) emvu_set(&u, 3, c, r[c]);
        }
        emvu_ilw_w(&u, 10, u.vi[14] + 3u);
        emvu_clipw(&u, 3);
        if (!(u.vi[15] & u.vi[10]) && (u.cf & 0x3FFFFu) && !emvu_rejected(u.cf) &&
            !((int16_t)(uint16_t)(u.vi[11] - 30u) > 0)) {
            if (emvu_entry(&u, box)) return -1;
            u.vi[15] = flags;                            /* R L026 / B L02A */
        }
        /* R L028 / B L02C */
        u.vi[11] = (u.vi[11] - 1u) & 0xFFFFu;
        emvu_lq(&u, 5, u.vi[14] + 7u);
        u.vi[14] = (u.vi[14] + 4u) & 0xFFFFu;            /* delay slot */
        if (u.vi[11] == 0u) break;
    }
    u.vi[1] = 1019u;
    return emvu_xgkick(&u, 1019u) ? -1 : 0;              /* R 0x031 / B 0x035 */
}

#endif /* EM_VU1_SHADOW_CLIP_H */
