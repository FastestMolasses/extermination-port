/* em_vu1_object_clip.h — the guard-band clip pass of the object kernel,
 * translated from its VU1 microcode:
 *
 *   0x002354A0  the kernel packet 001D3AD0 CALLs for an owner unit whose
 *               001CA7B0 flags have bit 0 set (001CA940 -> 001D3C30 ->
 *               001D3BA0). It uploads a 910-instruction program (four MPG
 *               blocks, micro 0x000..0x38D) and sets BASE 0x1B0, OFFSET 0x84.
 *               The unit then sends the model's blocks a second time, so the
 *               program runs once per 32-vertex batch (MSCAL 0 for the first,
 *               MSCNT for the rest; MSCNT resumes at 0x036, which branches to
 *               0x000, so every batch runs the same code from the start).
 *
 * Addresses in the comments are MICRO addresses (instruction index, 8 bytes
 * each) of that program, or VU1 data-memory qword addresses ("dmem N").
 * docs/VU1_OBJECT_CLIP.md has the evidence. The reference test
 * (tools/test_vu1_object_clip_reference.py) executes the ORIGINAL VU1
 * instructions on every batch of the 14 captured clip units and on a
 * synthetic sweep, and requires every XGKICK of this translation to equal
 * the interpreter's: the same dmem address, in the same order, the same
 * packet bytes.
 *
 * The data memory the program reads (what the unit uploads):
 *   dmem 0 .. 8n-1     per node: rows 0..3 the position matrix (node x VP),
 *                      rows 4..6 the lighting matrix (C x A); a vertex picks
 *                      its node by the low bits of its data word (below)
 *   dmem 1013..1016    the colour matrix B (001D89D0 via 001C7420)
 *   dmem 1017          the triangle-list GIF tag (skin record 1: PRIM 0x03B,
 *                      NREG 9, ST RGBAQ XYZF2 three times)
 *   dmem 1018, 1019    a one-register GIF tag (TEX0_1, NLOOP 1, EOP) and its
 *                      data qword
 *   dmem 1021          the fog row (255, 2048, A, B)
 *   dmem 1022, 1023    the guard rows
 *   dmem TOP..TOP+127  the batch: 32 vertices of 4 qwords (qword 0 a TEX0
 *                      value, qword 1 (s, t, 1, 0), qword 2 the normal,
 *                      qword 3 (x, y, z, data word)). TOP is 0x1B0 or 0x234
 *                      (the VIF double buffer).
 * It writes dmem 994 (saved loop registers), dmem 1019, and the working area
 * dmem 696 .. 984 (packet tag at 696, vertex slots from 697).
 *
 * What the program does:
 *
 * 1. Loop (0x000..0x036) over the 32 vertices at TOP: M = the four qwords at
 *    the data word's address (the data word is lane w of qword 3; its low 16
 *    bits as a dmem address, wrapping at 1024 qwords); c = p x M with w = 1;
 *    g = dmem 1022 * c + dmem 1023 * c.w (per lane); CLIP of g.xyz against
 *    |g.w| into the 24-bit history. Vertex i goes on to the clipping code
 *    (0x04B) when its data word has bit 15 clear, the history of vertices
 *    i-2..i is non-zero (the history's low 18 bits), and the three are not
 *    all outside one guard plane (six clip-flag OR tests, 0x038..0x049). Unlike
 *    the shadow clip kernels there is no i >= 2 test and no back-face test.
 *    After the loop: GIF kick of dmem 1018.
 * 2. Clip entry (0x04B..0x07A): the loop registers vi11, vi14, vi10 go to
 *    dmem 994 x, y, z; dmem 1019 = vertex i's qword 0; GIF kick of dmem 1018
 *    (TEX0_1 = that value). Vertices i-2, i-1, i each go through the vertex
 *    routine (0x1E6) into slots 697, 700, 703 (3 qwords each: ST, RGBAQ, XYZ):
 *      M = dmem[word..word+3], L = dmem[word+4..word+6];
 *      XYZ slot = p x M (clip space, not divided);
 *      ST slot = qword 1 as uploaded;
 *      l = max(n x L (three rows), 0) per lane;
 *      RGBAQ slot = min(l x B (with w = 1), 8388863) per lane.
 * 3. Plane w = 0.1 (0x077..0x0AF), in clip space on the slots: by the signs
 *    of w - 0.1 of the three: all behind aborts the entry (0x1DE: nothing
 *    more kicked); two behind (0x220) move both onto the plane along the
 *    edges to the vertex in front; one behind (0x25B) splits the triangle:
 *    the point on the edge to the second vertex after it starts a new
 *    triangle with the point on the edge to the first and that second
 *    vertex. Each point is v + (u - v) * q, q = (0.1 - v.w) / (u.w - v.w),
 *    for all three slots (XYZ, ST, RGBAQ), v the vertex in front.
 * 4. Projection (0x0B0..0x0BD): for every slot, Q = 1 / w; XYZ.xyz *= Q and
 *    ST.xyz *= Q (w lanes kept: the XYZ slot keeps its clip w).
 * 5. Planes x = 4088, x = 4, y = 4088, y = 4 in screen space (0x0BE..0x1AD),
 *    each over the triangles present when the plane starts: one vertex out
 *    splits into two triangles (the new one appended), two out move both,
 *    three out collapse the triangle onto (2048, 2048, 0, 0). There is no
 *    triangle cap: the w plane leaves at most 2 triangles and each plane at
 *    most doubles them, so at most 32 (slots 697..984) reach the output.
 * 6. Output (0x1AF..0x1E4): per slot, z = min(z, 8388607); F = max(min(A +
 *    B * w, 255), 0) in lane w; XYZ slot = the four lanes to fixed point with
 *    4 fraction bits (X, Y, Z<<4, F<<4 as PACKED XYZF2 reads them); RGBAQ
 *    slot = the four lanes to integers (the GS takes their low bytes); dmem
 *    696 = dmem 1017 with its first word replaced by NLOOP n | EOP; GIF kick
 *    of dmem 696. dmem 994 is reloaded into vi11, vi14, vi10 and the loop
 *    goes on (0x024 reloads the flag mask and the guard rows).
 *
 * So one batch kicks, per clip entry, [1018: TEX0_1 = vertex i's qword 0]
 * and (unless all three are behind w = 0.1) [696: n triangles, PRIM 0x03B,
 * ST RGBAQ XYZF2 per vertex], and after the loop [1018] once more (with
 * whatever dmem 1019 holds). em_vu1_object_clip_triangles decodes a batch's
 * kicks into GS triangles.
 *
 * Arithmetic: the helpers of em_vu1_shadow_clip.h (one VU model for every
 * translated VU1 program): register reads map denormals to signed zero and
 * inf/NaN encodings to +-FLT_MAX; FMAC results are truncated binary32 with
 * denormals flushed and overflow clamped; the product inside a MADD is
 * truncated but not flushed before the sum; DIV by zero gives +-FLT_MAX with
 * the XOR of the signs; MIN/MAX return the mapped operand. Every flag the
 * program reads (MAC sign, CLIP) is read at least four cycles after its
 * producer with no other producer in between, and every Q use follows a
 * WAITQ, so program order is the data order. VU1 rounding was not measured
 * (docs/EE_FLOAT_MODEL.md measured VU0); it is assumed to follow the same
 * rules, as the interpreter does.
 *
 * NOT established, therefore fail-stop: an FTOI whose result lies outside
 * int32 (the interpreter wraps it; the hardware result is unknown) and a
 * kicked packet whose GIF tags do not reach EOP within 64 tags. Both return
 * -1 with `fault` set; the kicks before the fault are kept.
 *
 * Header-only (static inline). Pure C: no GPU, no I/O. */
#ifndef EM_VU1_OBJECT_CLIP_H
#define EM_VU1_OBJECT_CLIP_H

#include "em_vu1_shadow_clip.h"

#define EM_VU1_OBJECT_CLIP_BASE 0x1B0u          /* the kernel packet's BASE */
#define EM_VU1_OBJECT_CLIP_OFFSET 0x84u         /* ... and OFFSET */
#define EM_VU1_OBJECT_CLIP_SAVE 994u            /* vi11 / vi14 / vi10 across an entry */
#define EM_VU1_OBJECT_CLIP_PACKET 696u          /* triangle packet tag; slots from 697 */
#define EM_VU1_OBJECT_CLIP_TEX0 1018u           /* TEX0_1 packet (tag 1018, data 1019) */
#define EM_VU1_OBJECT_CLIP_MAX_TRIANGLES 32u
/* 32 entries x (the TEX0 kick + a packet) + the final kick */
#define EM_VU1_OBJECT_CLIP_MAX_KICKS 65u
/* 32 x (2 + 1 + 9 x 32) + 2 qwords */
#define EM_VU1_OBJECT_CLIP_MAX_QWORDS (32u * (3u + 9u * EM_VU1_OBJECT_CLIP_MAX_TRIANGLES) + 2u)

enum {
    EM_VU1_OBJECT_CLIP_OK = 0,
    EM_VU1_OBJECT_CLIP_FAULT_ARGS = 1,    /* NULL, no storage */
    EM_VU1_OBJECT_CLIP_FAULT_FTOI = 2,    /* FTOI outside int32: not established */
    EM_VU1_OBJECT_CLIP_FAULT_GIF = 3,     /* kicked packet without EOP in 64 tags */
    EM_VU1_OBJECT_CLIP_FAULT_SPACE = 4    /* the caller's qword storage is full */
};

typedef struct {
    uint32_t addr;     /* dmem qword address of the XGKICK (1018 or 696) */
    uint32_t vertex;   /* the clip entry's vertex i; 32 for the kick after the loop */
    uint32_t first;    /* first qword in EmVu1ObjectClipResult.qw */
    uint32_t count;    /* qwords of the packet up to its EOP tag */
} EmVu1ObjectClipKick;

typedef struct {
    uint32_t fault;                       /* EM_VU1_OBJECT_CLIP_FAULT_* */
    uint32_t entries;                     /* clip entries (0x04B reached), in loop order: */
    uint8_t entry[32];                    /*   their vertex i */
    uint32_t kicks;
    EmVu1ObjectClipKick kick[EM_VU1_OBJECT_CLIP_MAX_KICKS];
    uint32_t qwords;                      /* qwords used in `qw` */
    uint32_t capacity;                    /* set by the caller: qwords available in `qw`
                                             (EM_VU1_OBJECT_CLIP_MAX_QWORDS always suffices) */
    EmVu1Qword *qw;                       /* set by the caller: the kicked packets, snapshotted */
} EmVu1ObjectClipResult;

/* ------------------------------------------------------------ helpers -- */

typedef struct {
    EmVu1Clip u;                  /* machine state (the shadow header's layout; u.out unused) */
    EmVu1ObjectClipResult *out;
    uint32_t vertex;              /* vertex i of the entry in progress */
} EmVu1ObjectClip;

/* vfT = dmem[a] in the masked lanes */
static inline void emoc_lq(EmVu1Clip *u, unsigned t, uint32_t a, unsigned mask)
{
    if (!t) return;
    for (unsigned c = 0; c < 4; ++c)
        if (emvu_lane(mask, c)) u->vf[t][c] = u->m[a & 1023u].w[c];
}

/* dmem[a] = vfS in the masked lanes */
static inline void emoc_sq(EmVu1Clip *u, unsigned s, uint32_t a, unsigned mask)
{
    for (unsigned c = 0; c < 4; ++c)
        if (emvu_lane(mask, c)) u->m[a & 1023u].w[c] = u->vf[s][c];
}

/* The XGKICK of the GIF packet at `a`, snapshotted up to its EOP tag (PACKED
 * NLOOP x NREG, REGLIST (NLOOP x NREG + 1) / 2, IMAGE NLOOP qwords). */
static inline int emoc_kick(EmVu1ObjectClip *c, uint32_t a)
{
    EmVu1ObjectClipResult *o = c->out;
    uint32_t q = a;
    int eop = 0;
    for (unsigned t = 0; t < 64 && !eop; ++t) {
        const EmVu1Qword *tag = &c->u.m[q & 1023u];
        const uint32_t nloop = tag->w[0] & 0x7FFFu;
        const uint32_t flg = (tag->w[1] >> 26) & 3u;
        uint32_t nreg = tag->w[1] >> 28;
        if (!nreg) nreg = 16u;
        eop = (tag->w[0] >> 15) & 1u;
        q += 1u + (flg == 0u ? nloop * nreg : flg == 1u ? (nloop * nreg + 1u) / 2u : nloop);
    }
    if (!eop) { o->fault = EM_VU1_OBJECT_CLIP_FAULT_GIF; return -1; }
    const uint32_t count = q - a;
    if (o->kicks >= EM_VU1_OBJECT_CLIP_MAX_KICKS || o->qwords + count > o->capacity) {
        o->fault = EM_VU1_OBJECT_CLIP_FAULT_SPACE;
        return -1;
    }
    EmVu1ObjectClipKick *k = &o->kick[o->kicks++];
    k->addr = a & 1023u;
    k->vertex = c->vertex;
    k->first = o->qwords;
    k->count = count;
    for (uint32_t j = 0; j < count; ++j) o->qw[o->qwords++] = c->u.m[(a + j) & 1023u];
    return 0;
}

/* ---------------------------------------------------- vertex routine -- */

/* 0x1E6..0x20F: vf1 = qword 3, vf2 = qword 2 (normal), vf3 = qword 1 (ST),
 * vi10 = the data word; the three slots at vi12 (ST, RGBAQ, XYZ). */
static inline void emoc_vertex(EmVu1Clip *u)
{
    const uint32_t word = u->vi[10];
    for (unsigned r = 0; r < 3; ++r) emvu_lq(u, 24 + r, word + 4u + r);  /* L */
    for (unsigned r = 0; r < 4; ++r) emvu_lq(u, 28 + r, word + r);       /* M */
    emvu_xform(u, 1, 28, 1);                              /* 0x1ED..0x1F0 */
    emvu_sq(u, 1, u->vi[12] + 2u);                        /* 0x1F4 */
    emvu_sq(u, 3, u->vi[12] + 0u);                        /* 0x1F5 */
    /* 0x1F6..0x1F8: vf11 = n.x * L0 + n.y * L1 + n.z * L2 */
    {
        float acc[4], r[4];
        for (unsigned c = 0; c < 4; ++c)
            acc[c] = emvu_fmac((double)emvu_rd(u->vf[24][c]) * (double)emvu_rd(u->vf[2][0]));
        for (unsigned c = 0; c < 4; ++c) {
            const float prod = emvu_trunc((double)emvu_rd(u->vf[25][c]) *
                                          (double)emvu_rd(u->vf[2][1]));
            acc[c] = emvu_fmac((double)acc[c] + (double)prod);
        }
        for (unsigned c = 0; c < 4; ++c) {
            const float prod = emvu_trunc((double)emvu_rd(u->vf[26][c]) *
                                          (double)emvu_rd(u->vf[2][2]));
            r[c] = emvu_fmac((double)acc[c] + (double)prod);
        }
        memcpy(u->acc, acc, sizeof acc);
        for (unsigned c = 0; c < 4; ++c) emvu_set(u, 11, c, r[c]);
    }
    emvu_max_v(u, 12, 11, emvu_rd(u->vf[0][0]), EMVU_XYZW);   /* 0x1FC: max(l, 0) */
    emvu_xform(u, 13, 20, 12);                                /* 0x200..0x203: x B */
    emvu_mini_v(u, 14, 13, emvu_rd(u->vf[9][0]), EMVU_XYZW);  /* 0x207: min(., 8388863) */
    emvu_sq(u, 14, u->vi[12] + 1u);                           /* 0x20B */
}

/* ---------------------------------------------------- plane w = 0.1 -- */

/* Load the three slots at s1, s2, s3 into vf10..12 (XYZ), vf13..15 (ST),
 * vf16..18 (RGBAQ): 0x220 / 0x25B. */
static inline void emoc_load3(EmVu1Clip *u, uint32_t s1, uint32_t s2, uint32_t s3)
{
    const uint32_t s[3] = { s1, s2, s3 };
    for (unsigned k = 0; k < 3; ++k) {
        emvu_lq(u, 10 + k, s[k] + 2u);
        emvu_lq(u, 13 + k, s[k] + 0u);
        emvu_lq(u, 16 + k, s[k] + 1u);
    }
}

static inline void emoc_store3(EmVu1Clip *u, uint32_t s1, uint32_t s2, uint32_t s3)
{
    emvu_sq(u, 10, s1 + 2u); emvu_sq(u, 11, s2 + 2u); emvu_sq(u, 12, s3 + 2u);
    emvu_sq(u, 13, s1 + 0u); emvu_sq(u, 14, s2 + 0u); emvu_sq(u, 15, s3 + 0u);
    emvu_sq(u, 16, s1 + 1u); emvu_sq(u, 17, s2 + 1u); emvu_sq(u, 18, s3 + 1u);
}

/* Two behind (0x220..0x24A): s1 in front; s2 and s3 move onto w = 0.1. */
static inline void emoc_w_two(EmVu1Clip *u, uint32_t s1, uint32_t s2, uint32_t s3)
{
    emoc_load3(u, s1, s2, s3);
    emvu_edge(u, 10, 12, 3, EMVU_W);                     /* 0x229..0x231 */
    emvu_add(u, 12, 1, 12, EMVU_XYZW);
    emvu_add(u, 15, 3, 15, EMVU_XYZW);
    emvu_add(u, 18, 4, 18, EMVU_XYZW);
    emvu_edge(u, 10, 11, 3, EMVU_W);                     /* 0x235..0x23D */
    emvu_add(u, 11, 1, 11, EMVU_XYZW);
    emvu_add(u, 14, 3, 14, EMVU_XYZW);
    emvu_add(u, 17, 4, 17, EMVU_XYZW);
    emoc_store3(u, s1, s2, s3);                          /* 0x241..0x249 */
}

/* One behind (0x25B..0x290): s1 behind. The point on edge s1-s3 goes to a
 * new triangle at vi9 with the point on edge s1-s2 and s3. */
static inline void emoc_w_one(EmVu1Clip *u, uint32_t s1, uint32_t s2, uint32_t s3)
{
    emoc_load3(u, s1, s2, s3);
    emvu_edge(u, 10, 12, 3, EMVU_W);                     /* 0x264..0x26C */
    emvu_add(u, 1, 1, 12, EMVU_XYZW);
    emvu_add(u, 3, 3, 15, EMVU_XYZW);
    emvu_add(u, 2, 4, 18, EMVU_XYZW);
    emvu_sq(u, 1, u->vi[9] + 2u);                        /* 0x270..0x272 */
    emvu_sq(u, 2, u->vi[9] + 1u);
    emvu_sq(u, 3, u->vi[9] + 0u);
    emvu_edge(u, 10, 11, 3, EMVU_W);                     /* 0x273..0x27B */
    emvu_add(u, 10, 1, 11, EMVU_XYZW);
    emvu_add(u, 13, 3, 14, EMVU_XYZW);
    emvu_add(u, 16, 4, 17, EMVU_XYZW);
    emoc_store3(u, s1, s2, s3);                          /* 0x27F..0x287 */
    emvu_sq(u, 10, u->vi[9] + 5u); emvu_sq(u, 13, u->vi[9] + 3u); emvu_sq(u, 16, u->vi[9] + 4u);
    emvu_sq(u, 12, u->vi[9] + 8u); emvu_sq(u, 15, u->vi[9] + 6u); emvu_sq(u, 18, u->vi[9] + 7u);
    u->vi[9] = (u->vi[9] + 9u) & 0xFFFFu;
    u->vi[8] = (u->vi[8] + 1u) & 0xFFFFu;
}

/* --------------------------------------------------- screen planes -- */

/* One screen plane over the triangles present at its start (x = 4088 at
 * 0x0BE, x = 4 at 0x0FB, y = 4088 at 0x137, y = 4 at 0x173). `lane` 0 = x,
 * 1 = y; `high`: outside when limit - v < 0 (else v - limit < 0). The
 * two-out and one-out cases (0x2A1 / 0x2D9 x, 0x319 / 0x351 y) are the
 * shadow kernels' (emvu_s_two / emvu_s_one, same register use and store
 * order); this program has no triangle cap, so the one-out result is not
 * tested. Three out (0x382): the triangle collapses onto (2048, 2048, 0, 0). */
static inline void emoc_plane(EmVu1Clip *u, unsigned lane, float limit, int high)
{
    const unsigned mask = 8u >> lane;
    uint32_t vi7 = EM_VU1_OBJECT_CLIP_PACKET + 1u;
    uint32_t n = u->vi[8];
    emvu_sub(u, 23, 23, 23, EMVU_XYZW);
    u->i = limit;
    emvu_addi(u, 23, 23, EMVU_XYZW);
    do {
        n = (n - 1u) & 0xFFFFu;
        emvu_lq(u, 1, vi7 + 2u);
        emvu_lq(u, 2, vi7 + 5u);
        emvu_lq(u, 3, vi7 + 8u);
        /* the sign of each difference (the MAC sign of the lane, read 4
         * cycles after it): bit 2 = slot C (vi7+6), bit 1 = B, bit 0 = A */
        uint32_t oc = 0;
        static const unsigned regs[3] = { 3, 2, 1 };
        for (unsigned k = 0; k < 3; ++k) {
            const unsigned r = regs[k];
            if (high) emvu_sub(u, r, 23, r, mask);       /* limit - v */
            else emvu_sub(u, r, r, 23, mask);            /* v - limit */
            oc = oc * 2u + emvu_sign(u, r, lane);
        }
        const uint32_t A = vi7, B = vi7 + 3u, C = vi7 + 6u;
        switch (oc) {
        case 7u:                                         /* 0x382 */
            emvu_sub(u, 1, 1, 1, EMVU_XYZW);
            u->i = 2048.0f;
            emvu_addi(u, 1, 1, EMVU_X | EMVU_Y);
            emvu_sq(u, 1, A + 2u); emvu_sq(u, 1, B + 2u); emvu_sq(u, 1, C + 2u);
            break;
        case 3u: emvu_s_two(u, C, A, B, lane); break;    /* A, B out */
        case 5u: emvu_s_two(u, B, C, A, lane); break;    /* A, C out */
        case 6u: emvu_s_two(u, A, B, C, lane); break;    /* B, C out */
        case 1u: (void)emvu_s_one(u, A, B, C, lane); break;  /* A out */
        case 2u: (void)emvu_s_one(u, B, C, A, lane); break;  /* B out */
        case 4u: (void)emvu_s_one(u, C, A, B, lane); break;  /* C out */
        default: break;
        }
        vi7 += 9u;
    } while (n != 0u);
}

/* ---------------------------------------------------- the clip entry -- */

/* 0x1DE..0x027: vi11, vi14, vi10 from dmem 994; the flag mask and the guard
 * rows again. */
static inline void emoc_resume(EmVu1Clip *u)
{
    u->vi[11] = u->m[EM_VU1_OBJECT_CLIP_SAVE].w[0] & 0xFFFFu;
    u->vi[14] = u->m[EM_VU1_OBJECT_CLIP_SAVE].w[1] & 0xFFFFu;
    u->vi[10] = u->m[EM_VU1_OBJECT_CLIP_SAVE].w[2] & 0xFFFFu;
    u->vi[12] = 0x8000u;
    emvu_lq(u, 17, 1022u);
    emvu_lq(u, 18, 1023u);
}

/* The clip entry for the vertex at vi14 (0x04B..0x1E4). Returns -1 on a
 * fault, else 0 (kicked or aborted). */
static inline int emoc_entry(EmVu1ObjectClip *c)
{
    EmVu1Clip *u = &c->u;
    EmVu1ObjectClipResult *o = c->out;
    emvu_isw(u, 11, EM_VU1_OBJECT_CLIP_SAVE, 0);         /* 0x04B..0x04D */
    emvu_isw(u, 14, EM_VU1_OBJECT_CLIP_SAVE, 1);
    emvu_isw(u, 10, EM_VU1_OBJECT_CLIP_SAVE, 2);
    c->vertex = ((u->vi[14] - u->top) & 0xFFFFu) / 4u;
    o->entry[o->entries++] = (uint8_t)c->vertex;
    emvu_lq(u, 1, u->vi[14]);                            /* 0x04F: qword 0 */
    emvu_sq(u, 1, EM_VU1_OBJECT_CLIP_TEX0 + 1u);
    u->vi[1] = EM_VU1_OBJECT_CLIP_TEX0;
    if (emoc_kick(c, EM_VU1_OBJECT_CLIP_TEX0)) return -1; /* 0x058 */
    u->vi[4] = EM_VU1_OBJECT_CLIP_PACKET + 1u;
    for (unsigned r = 0; r < 4; ++r) emvu_lq(u, 20 + r, 1013u + r);  /* B */
    u->i = 8388863.0f;                                   /* 0x05F */
    emvu_addi(u, 9, 0, EMVU_X);                          /* 0x060 */
    /* vertices i-2, i-1, i (0x061..0x072): qwords 3, 2, 1 */
    static const int32_t base[3] = { -8, -4, 0 };
    for (unsigned k = 0; k < 3; ++k) {
        const uint32_t v = u->vi[14] + (uint32_t)base[k];
        emvu_ilw_w(u, 10, v + 3u);
        emvu_lq(u, 1, v + 3u);
        emvu_lq(u, 2, v + 2u);
        emvu_lq(u, 3, v + 1u);
        u->vi[12] = EM_VU1_OBJECT_CLIP_PACKET + 1u + 3u * k;
        emoc_vertex(u);
    }
    u->vi[8] = 1u;                                       /* 0x073..0x076 */
    u->vi[9] = EM_VU1_OBJECT_CLIP_PACKET + 10u;
    u->vi[5] = 0x10u;
    u->vi[7] = EM_VU1_OBJECT_CLIP_PACKET + 1u;
    emvu_sub(u, 23, 23, 23, EMVU_XYZW);                  /* 0x077..0x079 */
    u->i = emvu_f(0x3DCCCCCDu);                          /* 0.1 */
    emvu_addi(u, 23, 23, EMVU_XYZW);
    /* plane w = 0.1 (0x07E..0x0AE): bit 2 = slot C, 1 = B, 0 = A */
    const uint32_t A = u->vi[7], B = A + 3u, C = A + 6u;
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
    case 7u: emoc_resume(u); return 0;                   /* 0x1DE: all behind */
    case 3u: emoc_w_two(u, C, A, B); break;              /* 0x211: A, B behind */
    case 5u: emoc_w_two(u, B, C, A); break;              /* 0x216: A, C behind */
    case 6u: emoc_w_two(u, A, B, C); break;              /* 0x21B: B, C behind */
    case 1u: emoc_w_one(u, A, B, C); break;              /* 0x24C: A behind */
    case 2u: emoc_w_one(u, B, C, A); break;              /* 0x251: B behind */
    case 4u: emoc_w_one(u, C, A, B); break;              /* 0x256: C behind */
    default: break;
    }
    /* projection (0x0B0..0x0BD) */
    {
        uint32_t n = (3u * u->vi[8]) & 0xFFFFu;
        uint32_t s = EM_VU1_OBJECT_CLIP_PACKET + 1u;
        do {
            n = (n - 1u) & 0xFFFFu;
            emvu_lq(u, 1, s + 2u);
            emoc_lq(u, 2, s + 0u, EMVU_XYZ);
            emvu_div(u, u->vf[0][3], u->vf[1][3]);
            emvu_mulq(u, 1, 1, EMVU_XYZ);
            emvu_mulq(u, 2, 2, EMVU_XYZ);
            emoc_sq(u, 1, s + 2u, EMVU_XYZ);
            emoc_sq(u, 2, s + 0u, EMVU_XYZ);
            s += 3u;
        } while (n != 0u);
    }
    /* the four screen planes */
    u->vi[5] = 0x80u;
    emoc_plane(u, 0, 4088.0f, 1);
    emoc_plane(u, 0, 4.0f, 0);
    u->vi[5] = 0x40u;
    emoc_plane(u, 1, 4088.0f, 1);
    emoc_plane(u, 1, 4.0f, 0);
    /* output (0x1AF..0x1D9) */
    emvu_lq(u, 19, 1021u);
    {
        uint32_t n = (3u * u->vi[8]) & 0xFFFFu;
        uint32_t s = EM_VU1_OBJECT_CLIP_PACKET + 1u;
        do {
            n = (n - 1u) & 0xFFFFu;
            emvu_lq(u, 1, s + 2u);
            emvu_lq(u, 2, s + 1u);
            u->i = 8388607.0f;                           /* 0x1B7 */
            emvu_mini_v(u, 1, 1, u->i, EMVU_Z);          /* z = min(z, 8388607) */
            u->acc[3] = emvu_fmac((double)emvu_rd(u->vf[0][3]) * (double)emvu_rd(u->vf[19][2]));
            {
                const float prod = emvu_trunc((double)emvu_rd(u->vf[19][3]) *
                                              (double)emvu_rd(u->vf[1][3]));
                emvu_set(u, 1, 3, emvu_fmac((double)u->acc[3] + (double)prod));  /* A + B w */
            }
            emvu_mini_v(u, 1, 1, emvu_rd(u->vf[19][0]), EMVU_W);   /* min(F, 255) */
            emvu_max_v(u, 1, 1, emvu_rd(u->vf[0][0]), EMVU_W);     /* max(F, 0) */
            if (emvu_ftoi(u, 3, 1, 16.0) || emvu_ftoi(u, 4, 2, 1.0)) {
                o->fault = EM_VU1_OBJECT_CLIP_FAULT_FTOI;
                return -1;
            }
            emvu_sq(u, 3, s + 2u);
            emvu_sq(u, 4, s + 1u);
            s += 3u;
        } while (n != 0u);
    }
    emvu_lq(u, 1, 1017u);                                /* 0x1D0..0x1D5 */
    emvu_sq(u, 1, EM_VU1_OBJECT_CLIP_PACKET);
    u->vi[14] = EM_VU1_OBJECT_CLIP_PACKET;
    u->vi[1] = (u->vi[8] + 0x8000u) & 0xFFFFu;
    emvu_isw(u, 1, EM_VU1_OBJECT_CLIP_PACKET, 0);
    if (emoc_kick(c, EM_VU1_OBJECT_CLIP_PACKET)) return -1;  /* 0x1D9 */
    emoc_resume(u);
    return 0;
}

/* Run the program on the batch at `top` over `dmem` (the VU1 data memory at
 * its MSCAL/MSCNT; read and written as the original does, so a caller keeps
 * one image across the batches of a unit: dmem 1019 carries the last entry's
 * TEX0 into the next batch's final kick). `out->qw` / `out->capacity` must
 * be set. `out` receives every XGKICK in order. Returns 0, or -1 with
 * out->fault. */
static inline int em_vu1_object_clip_run(EmVu1Qword *dmem, uint32_t top,
                                         EmVu1ObjectClipResult *out)
{
    if (!out) return -1;
    out->fault = EM_VU1_OBJECT_CLIP_OK;
    out->entries = out->kicks = out->qwords = 0;
    if (!dmem || !out->qw || !out->capacity) {
        out->fault = EM_VU1_OBJECT_CLIP_FAULT_ARGS;
        return -1;
    }
    EmVu1ObjectClip c;
    memset(&c, 0, sizeof c);
    EmVu1Clip *u = &c.u;
    u->m = dmem;
    u->top = top & 0xFFFFu;
    c.out = out;
    emvu_fix0(u);
    /* 0x000..0x006 */
    u->vi[14] = u->top;
    u->vi[12] = 0x8000u;
    u->cf = 0u;
    u->vi[11] = (u->vi[14] + 0x80u) & 0xFFFFu;
    emvu_lq(u, 17, 1022u);
    emvu_lq(u, 18, 1023u);
    do {
        /* 0x007..0x019 */
        emvu_ilw_w(u, 10, u->vi[14] + 3u);
        emoc_lq(u, 3, u->vi[14] + 3u, EMVU_XYZ);
        for (unsigned r = 0; r < 4; ++r) emvu_lq(u, 28 + r, u->vi[10] + r);
        emvu_xform(u, 4, 28, 3);
        {
            float acc[4], r[4];
            for (unsigned k = 0; k < 4; ++k)             /* ACC = vf17 * vf4, per lane */
                acc[k] = emvu_fmac((double)emvu_rd(u->vf[17][k]) * (double)emvu_rd(u->vf[4][k]));
            for (unsigned k = 0; k < 4; ++k) {           /* vf5 = ACC + vf18 * vf4.w */
                const float prod = emvu_trunc((double)emvu_rd(u->vf[18][k]) *
                                              (double)emvu_rd(u->vf[4][3]));
                r[k] = emvu_fmac((double)acc[k] + (double)prod);
            }
            memcpy(u->acc, acc, sizeof acc);
            for (unsigned k = 0; k < 4; ++k) emvu_set(u, 5, k, r[k]);
        }
        emvu_clipw(u, 5);
        /* 0x01C..0x049: data flag, clip history, the six plane tests */
        u->vi[2] = u->vi[12] & u->vi[10];
        if (!u->vi[2] && (u->cf & 0x3FFFFu) && !emvu_rejected(u->cf)) {
            if (emoc_entry(&c)) return -1;
        }
        u->vi[14] = (u->vi[14] + 4u) & 0xFFFFu;          /* 0x028 */
    } while (u->vi[14] != u->vi[11]);                    /* 0x02C */
    c.vertex = 32u;
    u->vi[1] = EM_VU1_OBJECT_CLIP_TEX0;
    return emoc_kick(&c, EM_VU1_OBJECT_CLIP_TEX0) ? -1 : 0;   /* 0x032 */
}

/* ------------------------------------------------------- unit image -- */

/* The data memory of one clip unit before its first batch, from the unit's
 * own pieces: `nodes` = the 001C7420 payload (32 words per node: position
 * matrix, lighting matrix), `color` = the colour matrix B (16 words, dmem
 * 1013..1016), `record` = skin record 1's 28 words (dmem 1017..1023; when
 * the unit carries the fog-off REF 2, dmem 1021 replaced by it). Every other
 * qword is zero (the program writes its working area before it reads it).
 * Returns 0, or -1 (NULL, or nodes reaching the batch buffers). */
static inline int em_vu1_object_clip_image(EmVu1Qword *dmem, const uint32_t *nodes,
                                           uint32_t node_count, const uint32_t *color,
                                           const uint32_t *record)
{
    if (!dmem || !color || !record || (node_count && !nodes) ||
        8u * node_count > EM_VU1_OBJECT_CLIP_BASE)
        return -1;
    memset(dmem, 0, EM_VU1_DMEM_QWORDS * sizeof *dmem);
    if (node_count) memcpy(dmem, nodes, 8u * node_count * sizeof *dmem);
    memcpy(&dmem[1013], color, 4u * sizeof *dmem);
    memcpy(&dmem[1017], record, 7u * sizeof *dmem);
    return 0;
}

/* Model block `index` (130 qwords at `block`: STCYCL 4,4 + UNPACK V4-32 of
 * 128 qwords at TOPS, the 32 vertices, MSCAL 0 / MSCNT) into its batch
 * buffer, as the VIF double buffer places it (BASE 0x1B0, OFFSET 0x84: even
 * blocks at 0x1B0, odd at 0x234). Returns the TOP to run, or -1 when the
 * block's VIF codes are not those. */
static inline int32_t em_vu1_object_clip_batch(EmVu1Qword *dmem, uint32_t index,
                                               const uint8_t *block)
{
    if (!dmem || !block) return -1;
    uint32_t head[4], tail[4];
    memcpy(head, block, 16);
    memcpy(tail, block + 16u * 129u, 16);
    const uint32_t mscal = index ? 0x17000000u : 0x14000000u;
    if (head[0] || head[1] || head[2] != 0x01000404u || head[3] != 0x6C808000u ||
        tail[0] != mscal || tail[1] || tail[2] || tail[3])
        return -1;
    const uint32_t top = EM_VU1_OBJECT_CLIP_BASE + ((index & 1u) ? EM_VU1_OBJECT_CLIP_OFFSET : 0u);
    memcpy(&dmem[top], block + 16u, 128u * sizeof *dmem);
    return (int32_t)top;
}

/* ------------------------------------------------ the GS primitives -- */

typedef struct {
    float s, t, q;        /* ST (Q = the ST qword's z lane, PACKED ST) */
    uint8_t rgba[4];      /* RGBAQ: the low byte of each lane */
    uint16_t x, y;        /* XYZF2 X, Y (GS 12.4) */
    uint32_t z;           /* 24 bits */
    uint8_t f;            /* fog */
} EmVu1ObjectClipVertex;

typedef struct {
    uint64_t tex0;        /* TEX0_1 in force (the entry's kick of dmem 1018) */
    uint32_t prim;        /* the PRIM value of the packet's tag */
    uint32_t vertex;      /* the clip entry's vertex i */
    EmVu1ObjectClipVertex v[3];
} EmVu1ObjectClipTriangle;

/* The GS triangles of one batch's kicks, in drawing order. Only what this
 * program kicks is accepted: PACKED tags whose registers are TEX0_1, ST,
 * RGBAQ and XYZF2 (ADC clear), a PRE tag with a triangle-list PRIM, three
 * vertices per triangle, a TEX0 before the first triangle. Returns the
 * triangle count, or -1 for anything else or more than `cap`. */
static inline int em_vu1_object_clip_triangles(const EmVu1ObjectClipResult *r,
                                               EmVu1ObjectClipTriangle *tri, uint32_t cap)
{
    if (!r || !r->qw || (cap && !tri)) return -1;
    uint64_t tex0 = 0;
    int have_tex0 = 0;
    uint32_t n = 0;
    for (uint32_t k = 0; k < r->kicks; ++k) {
        const EmVu1ObjectClipKick *kick = &r->kick[k];
        uint32_t q = kick->first;
        const uint32_t end = kick->first + kick->count;
        int eop = 0;
        while (q < end && !eop) {
            const EmVu1Qword *tag = &r->qw[q++];
            const uint32_t nloop = tag->w[0] & 0x7FFFu;
            const uint32_t pre = (tag->w[1] >> 14) & 1u;
            const uint32_t prim = (tag->w[1] >> 15) & 0x7FFu;
            const uint32_t flg = (tag->w[1] >> 26) & 3u;
            const uint32_t nreg = (tag->w[1] >> 28) ? (tag->w[1] >> 28) : 16u;
            const uint64_t regs = (uint64_t)tag->w[2] | ((uint64_t)tag->w[3] << 32);
            eop = (tag->w[0] >> 15) & 1u;
            if (flg != 0u || q + nloop * nreg > end) return -1;
            if (pre && (prim & 7u) != 3u) return -1;     /* triangle list only */
            uint32_t count = 0;                          /* vertices of the current triangle */
            EmVu1ObjectClipVertex v;
            memset(&v, 0, sizeof v);
            float qv = 0.0f;
            int have_st = 0;
            for (uint32_t l = 0; l < nloop; ++l) {
                for (uint32_t g = 0; g < nreg; ++g) {
                    const EmVu1Qword *d = &r->qw[q++];
                    switch ((regs >> (4u * g)) & 15u) {
                    case 0x6u:                           /* TEX0_1 */
                        tex0 = (uint64_t)d->w[0] | ((uint64_t)d->w[1] << 32);
                        have_tex0 = 1;
                        break;
                    case 0x2u:                           /* ST */
                        v.s = emvu_f(d->w[0]);
                        v.t = emvu_f(d->w[1]);
                        qv = emvu_f(d->w[2]);
                        have_st = 1;
                        break;
                    case 0x1u:                           /* RGBAQ */
                        if (!have_st) return -1;
                        for (unsigned c = 0; c < 4; ++c) v.rgba[c] = (uint8_t)d->w[c];
                        v.q = qv;
                        break;
                    case 0x4u:                           /* XYZF2 */
                        if (!pre || !have_tex0 || ((d->w[3] >> 15) & 1u)) return -1;
                        v.x = (uint16_t)d->w[0];
                        v.y = (uint16_t)d->w[1];
                        v.z = (d->w[2] >> 4) & 0xFFFFFFu;
                        v.f = (uint8_t)(d->w[3] >> 4);
                        if (count == 0u) {
                            if (n >= cap) return -1;
                            tri[n].tex0 = tex0;
                            tri[n].prim = prim;
                            tri[n].vertex = kick->vertex;
                        }
                        tri[n].v[count++] = v;
                        if (count == 3u) { count = 0u; ++n; }
                        break;
                    default:
                        return -1;
                    }
                }
            }
            if (count) return -1;
        }
        if (!eop || q != end) return -1;
    }
    return (int)n;
}

#endif /* EM_VU1_OBJECT_CLIP_H */
