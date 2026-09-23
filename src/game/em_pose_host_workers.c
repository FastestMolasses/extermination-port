/* em_pose_host_workers.c - the original clip and skeleton routines over the
 * raw records (see em_pose_host_workers.h, docs/POSE_HOST_WORKERS.md).
 *
 * Read from the original instructions (the decomp's build/asm), not from the
 * readable C alone: 001C87C0, 001C92C0, quat_nlerp, 001C9940 and 00178910
 * are NEARMISS C, 001C8710 is an asm-word file and anim_eval_skeleton is an
 * INCLUDE_ASM placeholder (no C). 00128250 and the SDK matrix routines are
 * not translated here: they bind the existing translations
 * (em_stream_lanes_00128250, em_owner_services_*). Every address in a comment is the original instruction
 * translated there. Float arithmetic, compares and VU0 lanes go through
 * em_ee_float.h on bit patterns, as the COP1 / COP2 instructions execute.
 * Comments describe what the original computes; they never reproduce its
 * instruction stream. */
#include "game/em_pose_host_workers.h"
#include "game/em_ee_float.h"
#include "game/em_owner_services_original.h"
#include "game/em_player_reaction.h"
#include "game/em_stream_lanes_original.h"

#include <string.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

#define F_ZERO   UINT32_C(0x00000000)
#define F_ONE    UINT32_C(0x3F800000)
#define F_TWO    UINT32_C(0x40000000)
#define F_4096TH UINT32_C(0x39800000)   /* 2^-12: the 4.12 row scale */
#define F_4      UINT32_C(0x40800000)
#define F_1_5    UINT32_C(0x3FC00000)
#define F_20_5   UINT32_C(0x41A40000)
#define F_SLOPE  UINT32_C(0x3F20D97C)   /* 0.62831855 */
#define F_3PI_2  UINT32_C(0x4096CBE4)   /* 4.712389 */

/* ---- raw little-endian access ------------------------------------------- */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* The host bytes of [address, address + len), or NULL when no region holds
 * the whole range (or a write reaches a read-only region). */
static uint8_t *map(const EmPoseHost *h, uint32_t address, uint32_t len, int write)
{
    for (unsigned i = 0; i < h->region_count && i < EM_POSE_REGION_MAX; ++i) {
        const EmPoseRegion *r = &h->region[i];
        if (!r->bytes || len > r->size) continue;
        uint32_t off = address - r->address;
        if (address < r->address || off > r->size - len) continue;
        if (write && !r->writable) return NULL;
        return r->bytes + off;
    }
    return NULL;
}
static int load32(const EmPoseHost *h, uint32_t address, uint32_t *out)
{
    const uint8_t *p = map(h, address, 4, 0);
    if (!p) return -1;
    *out = rd32(p);
    return 0;
}
static int load16(const EmPoseHost *h, uint32_t address, uint16_t *out)
{
    const uint8_t *p = map(h, address, 2, 0);
    if (!p) return -1;
    *out = rd16(p);
    return 0;
}

/* ---- float helpers (em_ee_float.h) --------------------------------------- */

#define ADD(a, b) em_ee_add_bits((a), (b))
#define SUB(a, b) em_ee_sub_bits((a), (b))
#define MUL(a, b) em_ee_mul_bits((a), (b))
#define DIV(a, b) em_ee_div_bits((a), (b))
#define CVT(w)    em_ee_cvt_s_w_bits((uint32_t)(w))

/* The unsigned-to-float idiom the compiler emits for (float)(unsigned)x:
 * cvt.s.w when x is non-negative as a word, else (x >> 1 | x & 1) converted
 * and doubled. The key times are halfwords, so only the first path runs. */
static uint32_t ucvt(uint32_t x)
{
    if ((int32_t)x >= 0) return CVT(x);
    uint32_t f = CVT((x >> 1) | (x & 1u));
    return ADD(f, f);
}

/* VU0 lane operations; a form the original never executes is refused. */
static int vu(em_vu_op op, unsigned dest, int bc, const uint32_t fs[4], const uint32_t ft[4],
              const uint32_t acc[4], uint32_t dst[4])
{
    return em_vu_vec_bits(op, dest, bc, fs, ft, 0, acc, dst) == EM_EE_FLOAT_OK ? 0 : -1;
}

/* ---- leaves ----------------------------------------------------------------- */

/* 001C84D0: four 20-bit channels from five halfwords; each channel is
 * staged in the scratchpad word 0x70003600 + 4k, shifted left by 12 there
 * and copied out as the float's bit pattern. */
void em_pose_host_001C84D0(const uint8_t src[10], uint32_t out[4], uint32_t spad3600[4])
{
    uint32_t s0 = rd16(src), s1 = rd16(src + 2), s2 = rd16(src + 4), s3 = rd16(src + 6),
             s4 = rd16(src + 8);
    spad3600[0] = (s0 | s1 << 16) << 12;
    out[0] = spad3600[0];
    spad3600[1] = ((s1 >> 4) | s2 << 12) << 12;
    out[1] = spad3600[1];
    spad3600[2] = ((s2 >> 8) | s3 << 8) << 12;
    out[2] = spad3600[2];
    spad3600[3] = ((s3 >> 12) | s4 << 4) << 12;
    out[3] = spad3600[3];
}

/* 001C85D0 (anim_decode_translation): three 26-bit channels staged in
 * 0x70003600..08, each shifted left by 6, then copied out. */
void em_pose_host_001C85D0(const uint8_t src[10], uint32_t out[3], uint32_t spad3600[4])
{
    uint32_t s0 = rd16(src), s1 = rd16(src + 2), s2 = rd16(src + 4), s3 = rd16(src + 6),
             s4 = rd16(src + 8);
    spad3600[0] = s0 | s1 << 16;
    spad3600[1] = s3 << 22 | ((s1 >> 10) | s2 << 6);
    spad3600[2] = (s3 >> 4) | s4 << 12;
    for (int k = 0; k < 3; ++k) spad3600[k] <<= 6;
    for (int k = 0; k < 3; ++k) out[k] = spad3600[k];
}

/* 001CA0A0 quat_nlerp: t is clamped above at 1.0 (001CA0AC); inv = 1 - t;
 * dot = ((a0*b0 + a1*b1) + a2*b2) + a3*b3 formed through the COP1
 * accumulator; a negative dot gives b*t - a*inv, otherwise a*inv + b*t.
 * No normalization. */
void em_pose_host_001CA0A0(uint32_t out[4], const uint32_t a[4], const uint32_t b[4], uint32_t t)
{
    if (!em_ee_c_le_bits(t, F_ONE)) t = F_ONE;               /* 001CA0AC */
    uint32_t inv = SUB(F_ONE, t);                            /* 001CA0CC */
    uint32_t z = MUL(a[2], b[2]);                            /* 001CA0EC */
    uint32_t acc = em_ee_mula_bits(a[0], b[0]);              /* 001CA0F0 */
    uint32_t xy = em_ee_madd_bits(acc, a[1], b[1]);          /* 001CA0F4 */
    acc = em_ee_adda_bits(z, xy);                            /* 001CA0F8 */
    uint32_t dot = em_ee_madd_bits(acc, a[3], b[3]);         /* 001CA104 */
    uint32_t r[4];
    if (em_ee_c_lt_bits(dot, F_ZERO)) {                      /* 001CA108 */
        for (int k = 0; k < 4; ++k) {                        /* 001CA118..001CA160 */
            acc = em_ee_mula_bits(b[k], t);
            r[k] = em_ee_msub_bits(acc, a[k], inv);
        }
    } else {
        for (int k = 0; k < 4; ++k) {                        /* 001CA164..001CA1A8 */
            acc = em_ee_mula_bits(a[k], inv);
            r[k] = em_ee_madd_bits(acc, b[k], t);
        }
    }
    memcpy(out, r, sizeof r);
}

/* 001CA1C0 quat_to_mat3: the nine products are staged at 0x70003760..88
 * (XX YY ZZ, XY XZ YZ, WX WY WZ), then the rows; the translation row is
 * copied raw and m[15] = 1.0. */
void em_pose_host_001CA1C0(uint32_t m[16], const uint32_t q[4], const uint32_t translation[3],
                           uint32_t spad3760[11])
{
    uint32_t *s = spad3760;
    s[0] = MUL(q[0], q[0]); s[1] = MUL(q[1], q[1]); s[2] = MUL(q[2], q[2]);   /* 001CA1D8.. */
    s[4] = MUL(q[0], q[1]); s[5] = MUL(q[0], q[2]); s[6] = MUL(q[1], q[2]);
    s[8] = MUL(q[3], q[0]); s[9] = MUL(q[3], q[1]); s[10] = MUL(q[3], q[2]);
    uint32_t XX = s[0], YY = s[1], ZZ = s[2], XY = s[4], XZ = s[5], YZ = s[6];
    uint32_t WX = s[8], WY = s[9], WZ = s[10];
    uint32_t r[16];
    r[0] = SUB(F_ONE, MUL(F_TWO, ADD(YY, ZZ)));              /* 001CA288 */
    r[1] = MUL(F_TWO, SUB(XY, WZ));
    r[2] = MUL(F_TWO, ADD(XZ, WY));
    r[3] = F_ZERO;
    r[4] = MUL(F_TWO, ADD(XY, WZ));
    r[5] = SUB(F_ONE, MUL(F_TWO, ADD(XX, ZZ)));
    r[6] = MUL(F_TWO, SUB(YZ, WX));
    r[7] = F_ZERO;
    r[8] = MUL(F_TWO, SUB(XZ, WY));
    r[9] = MUL(F_TWO, ADD(YZ, WX));
    r[10] = SUB(F_ONE, MUL(F_TWO, ADD(XX, YY)));
    r[11] = F_ZERO;
    r[12] = translation[0]; r[13] = translation[1]; r[14] = translation[2];   /* 001CA38C */
    r[15] = F_ONE;
    memcpy(m, r, sizeof r);
}

/* ---- host checks ------------------------------------------------------------ */

/* The host and every shared-storage binding of its globals. */
static int host_ready(const EmPoseHost *h)
{
    const EmPoseGlobals *g = h ? h->globals : NULL;
    if (!g || !g->d8106F3 || !g->spad3400 || !g->spad3440 || !g->spad3600 || !g->spad3760 ||
        !g->spad3A3C || !g->spad38B0 || !g->spad3A20 || !g->column)
        return -1;
    return 0;
}

/* The node pointer i of an array at `bones` (host bytes). */
static uint32_t node_word(const uint8_t *bones, int i) { return rd32(bones + 4 * (uint32_t)i); }

/* Every node record 0..n-1 (at least node 0 when want0) is mapped
 * writable. */
static int nodes_ready(const EmPoseHost *h, const uint8_t *bones, uint32_t bones_size, int n, int want0)
{
    int count = n > 0 ? n : 0;
    if (want0 && count < 1) count = 1;
    if (!bones || (uint64_t)count * 4 > bones_size) return -1;
    for (int i = 0; i < count; ++i)
        if (!map(h, node_word(bones, i), EM_POSE_NODE_BYTES, 1)) return -1;
    return 0;
}
static uint8_t *node_at(const EmPoseHost *h, const uint8_t *bones, int i)
{
    return map(h, node_word(bones, i), EM_POSE_NODE_BYTES, 1);
}

/* ---- 001C6120 / 001C8480 / 001C61D0 --------------------------------------------- */

/* 001C6120(bank, clip): the directory word at bank + 4 + 4 * (clip & 0x7FFF)
 * (001C6124..001C6138), rounded down to a word offset (001C613C), added to
 * the bank. */
int em_pose_host_001C6120(EmPoseHost *h, uint32_t bank, int clip, uint32_t *header)
{
    if (!h || !header) return -1;
    uint32_t index = (uint32_t)clip & 0xFFFFu & UINT32_C(0xFFFF7FFF);
    uint32_t word;
    FAULT(load32(h, bank + index * 4 + 4, &word));
    *header = bank + (word & ~UINT32_C(3));   /* the arithmetic shift pair keeps bits 31..2 */
    return 0;
}

/* 001C8480 anim_clip_resolve: D_00275BF8 = the header; D_00275BF4/BF0/BEC =
 * header + its words +8 / +C / +10 (001C8494..001C84BC). The header words
 * are read before the first store, so an unmapped header writes nothing. */
int em_pose_host_001C8480(EmPoseHost *h, uint32_t bank, int clip)
{
    FAULT(host_ready(h));
    uint32_t header, rot, trans, scale;
    FAULT(em_pose_host_001C6120(h, bank, (int16_t)clip, &header));
    FAULT(load32(h, header + 0x8, &rot));
    FAULT(load32(h, header + 0xC, &trans));
    FAULT(load32(h, header + 0x10, &scale));
    EmPoseGlobals *g = h->globals;
    g->d275BF8 = header;
    g->d275BF4 = header + rot;
    g->d275BF0 = header + trans;
    g->d275BEC = header + scale;
    return 0;
}

/* 001C61D0: D_00275BF8 = 001C6120(bank, clip), then the halfword +2 of it
 * (the clip's frame count, zero-extended). */
int em_pose_host_001C61D0(EmPoseHost *h, uint32_t bank, int clip, int32_t *frames)
{
    FAULT(host_ready(h));
    if (!frames) return -1;
    uint32_t header;
    uint16_t count;
    FAULT(em_pose_host_001C6120(h, bank, (int16_t)clip, &header));
    FAULT(load16(h, header + 2, &count));
    h->globals->d275BF8 = header;                            /* 001C61E4 */
    *frames = count;                                         /* 001C61F0 */
    return 0;
}

/* ---- 001C86A0 and the key walkers -------------------------------------------------- */

/* 001C86A0(dst, a, b, d): 0x70003A3C = 1.0 / d, then dst[k] = that * (a[k] -
 * b[k]) for k = 0, 1, 2, each stored before the next is read. */
static void scaled_difference(EmPoseGlobals *g, uint8_t *dst, const uint8_t *a, const uint8_t *b,
                              uint32_t d)
{
    *g->spad3A3C = DIV(F_ONE, d);                             /* 001C86AC */
    for (int k = 0; k < 3; ++k)
        wr32(dst + 4 * k, MUL(*g->spad3A3C, SUB(rd32(a + 4 * k), rd32(b + 4 * k))));
}

static int decode_rotation(EmPoseHost *h, uint32_t address, uint8_t *out)
{
    const uint8_t *src = map(h, address, 10, 0);
    if (!src) return -1;
    uint32_t w[4];
    em_pose_host_001C84D0(src, w, h->globals->spad3600);
    for (int k = 0; k < 4; ++k) wr32(out + 4 * k, w[k]);
    return 0;
}
static int decode_translation(EmPoseHost *h, uint32_t address, uint8_t *out)
{
    const uint8_t *src = map(h, address, 10, 0);
    if (!src) return -1;
    uint32_t w[3];
    em_pose_host_001C85D0(src, w, h->globals->spad3600);
    for (int k = 0; k < 3; ++k) wr32(out + 4 * k, w[k]);
    return 0;
}

/* The interval walk shared by the three samplers: from the node's stream in
 * `section` (its directory word idx), count steps into the halfword at
 * chan + step (seeded 1) until key(+A) <= time < next key(+16). */
static int walk(EmPoseHost *h, uint8_t *chan, unsigned step, uint32_t section, int idx,
                int32_t time, uint32_t *rec_out, uint16_t *t_prev, uint16_t *t_next)
{
    uint32_t dir;
    FAULT(load32(h, section + (uint32_t)idx * 4, &dir));
    uint32_t rec = section + dir;
    wr16(chan + step, 1);
    for (;;) {
        FAULT(load16(h, rec + 0xA, t_prev));
        if (!((int32_t)*t_prev > time)) {
            FAULT(load16(h, rec + 0x16, t_next));
            if (time < (int32_t)*t_next) break;
        }
        wr16(chan + step, (uint16_t)(rd16(chan + step) + 1));
        rec += 0xC;
    }
    *rec_out = rec;
    return 0;
}

/* 001C8F10 anim_sample_rotation(chan, idx, time): after the walk, decode the
 * straddling keys into +30 / +40; +60 = next - time, +54 = 1 / (next -
 * key), +50 = +54 * (time - key). */
static int sample_rotation(EmPoseHost *h, uint8_t *chan, int idx, int32_t time)
{
    uint32_t rec;
    uint16_t t_prev, t_next, again;
    FAULT(walk(h, chan, 0x66, h->globals->d275BF4, idx, time, &rec, &t_prev, &t_next));
    uint32_t span = ucvt(t_prev);
    FAULT(decode_rotation(h, rec, chan + 0x30));
    FAULT(load16(h, rec + 0x16, &again));
    span = SUB(ucvt(again), span);
    FAULT(decode_rotation(h, rec + 0xC, chan + 0x40));
    wr32(chan + 0x60, SUB(ucvt(t_next), CVT(time)));
    wr32(chan + 0x54, DIV(F_ONE, span));
    wr32(chan + 0x50, MUL(rd32(chan + 0x54), SUB(CVT(time), ucvt(t_prev))));
    return 0;
}

/* 001C90D0 (translation) and 001C92C0 (scale): after the walk, decode the
 * key into value (+0 / +18) and the next key into the velocity slot (+C /
 * +24); velocity = (next - key) / span through 001C86A0; the remaining time
 * (+58 / +5C) = next - time; value += velocity * (time - key). */
static int sample_vector(EmPoseHost *h, uint8_t *chan, unsigned step, uint32_t section,
                         unsigned value, unsigned velocity, unsigned remaining, int idx, int32_t time)
{
    uint32_t rec;
    uint16_t t_prev, t_next, again;
    FAULT(walk(h, chan, step, section, idx, time, &rec, &t_prev, &t_next));
    uint32_t along = SUB(CVT(time), ucvt(t_prev));           /* 001C9358 / 001C90D0 */
    uint32_t span = ucvt(t_prev);
    FAULT(decode_translation(h, rec, chan + value));
    FAULT(load16(h, rec + 0x16, &again));
    span = SUB(ucvt(again), span);
    FAULT(decode_translation(h, rec + 0xC, chan + velocity));
    scaled_difference(h->globals, chan + velocity, chan + velocity, chan + value, span);
    wr32(chan + remaining, SUB(ucvt(t_next), CVT(time)));
    for (int k = 0; k < 3; ++k)
        wr32(chan + value + 4 * k,
             ADD(rd32(chan + value + 4 * k), MUL(rd32(chan + velocity + 4 * k), along)));
    return 0;
}
static int sample_translation(EmPoseHost *h, uint8_t *chan, int idx, int32_t time)
{
    return sample_vector(h, chan, 0x68, h->globals->d275BF0, 0x00, 0x0C, 0x58, idx, time);
}
static int sample_scale(EmPoseHost *h, uint8_t *chan, int idx, int32_t time)
{
    return sample_vector(h, chan, 0x6A, h->globals->d275BEC, 0x18, 0x24, 0x5C, idx, time);
}

/* ---- 001C8710 / 001C87C0 / 001C8D50 --------------------------------------------------- */

/* 001C8710(bones, n, frame): each node samples its rotation, translation and
 * scale directly at float_to_int(frame). */
int em_pose_host_001C8710(EmPoseHost *h, const uint8_t *bones, uint32_t bones_size, int n, float frame)
{
    FAULT(host_ready(h));
    FAULT(nodes_ready(h, bones, bones_size, n, 0));
    for (int i = 0; i < n; ++i) {                            /* 001C8740 */
        uint8_t *node = node_at(h, bones, i);
        FAULT(sample_rotation(h, node, i, em_player_float_to_int(em_ee_bits(frame))));
        FAULT(sample_translation(h, node, i, em_player_float_to_int(em_ee_bits(frame))));
        FAULT(sample_scale(h, node, i, em_player_float_to_int(em_ee_bits(frame))));
    }
    return 0;
}

/* One channel crossing its key in 001C87C0: the record at the stream's
 * current step, the span next - key. */
static int step_record(EmPoseHost *h, uint32_t section, int idx, uint16_t step, uint32_t *rec,
                       uint32_t *span_key)
{
    uint32_t dir;
    uint16_t key;
    FAULT(load32(h, section + (uint32_t)idx * 4, &dir));
    *rec = section + dir + (uint32_t)step * 0xC;
    FAULT(load16(h, *rec + 0xA, &key));
    *span_key = ucvt(key);
    return 0;
}

/* 001C87C0(bones, n, dt). Per node and channel: remaining -= dt; at or below
 * zero the stream steps one record (decode, new span, remaining += span,
 * the step counter + 1) and the in-between value restarts at -old * rate;
 * otherwise the value moves by dt * rate. The scale crossing records the
 * key's 0x8000 flag (the last crossing decides). If that flag is set or
 * D_008106F3 is nonzero, every node's +54 and the translation/scale
 * velocities are cleared. */
int em_pose_host_001C87C0(EmPoseHost *h, const uint8_t *bones, uint32_t bones_size, int n, float dt_in)
{
    FAULT(host_ready(h));
    FAULT(nodes_ready(h, bones, bones_size, n, 0));
    EmPoseGlobals *g = h->globals;
    uint32_t dt = em_ee_bits(dt_in);
    int end = 0;
    for (int i = 0; i < n; ++i) {                            /* 001C8810 */
        uint8_t *node = node_at(h, bones, i);
        uint32_t rec, span;
        uint16_t next;

        wr32(node + 0x60, SUB(rd32(node + 0x60), dt));       /* 001C881C */
        if (em_ee_c_le_bits(rd32(node + 0x60), F_ZERO)) {    /* 001C882C */
            FAULT(step_record(h, g->d275BF4, i, rd16(node + 0x66), &rec, &span));
            FAULT(decode_rotation(h, rec, node + 0x30));
            FAULT(load16(h, rec + 0x16, &next));
            span = SUB(ucvt(next), span);                    /* 001C88D8 */
            FAULT(decode_rotation(h, rec + 0xC, node + 0x40));
            uint32_t inv = DIV(F_ONE, span);                 /* 001C88F4 */
            uint32_t old = rd32(node + 0x60);
            wr32(node + 0x60, ADD(old, span));               /* 001C88FC */
            wr32(node + 0x54, inv);
            wr32(node + 0x50, MUL(em_ee_neg_bits(old), rd32(node + 0x54)));   /* 001C8918 */
            wr16(node + 0x66, (uint16_t)(rd16(node + 0x66) + 1));
        } else {
            wr32(node + 0x50, ADD(rd32(node + 0x50), MUL(dt, rd32(node + 0x54))));   /* 001C8934 */
        }

        wr32(node + 0x58, SUB(rd32(node + 0x58), dt));       /* 001C8954 */
        if (em_ee_c_le_bits(rd32(node + 0x58), F_ZERO)) {    /* 001C8964 */
            FAULT(step_record(h, g->d275BF0, i, rd16(node + 0x68), &rec, &span));
            FAULT(decode_translation(h, rec, node + 0x00));
            FAULT(load16(h, rec + 0x16, &next));
            span = SUB(ucvt(next), span);                    /* 001C8A0C */
            FAULT(decode_translation(h, rec + 0xC, node + 0x0C));
            scaled_difference(g, node + 0x0C, node + 0x0C, node + 0x00, span);   /* 001C8A28 */
            uint32_t old = rd32(node + 0x58);
            wr32(node + 0x58, ADD(old, span));               /* 001C8A38 */
            uint32_t back = em_ee_neg_bits(old);             /* 001C8A44 */
            for (int k = 0; k < 3; ++k)
                wr32(node + 4 * k, ADD(rd32(node + 4 * k), MUL(back, rd32(node + 0x0C + 4 * k))));
            wr16(node + 0x68, (uint16_t)(rd16(node + 0x68) + 1));
        } else {
            for (int k = 0; k < 3; ++k)                      /* 001C8AA0 */
                wr32(node + 4 * k, ADD(rd32(node + 4 * k), MUL(dt, rd32(node + 0x0C + 4 * k))));
        }

        wr32(node + 0x5C, SUB(rd32(node + 0x5C), dt));       /* 001C8AF4 */
        if (em_ee_c_le_bits(rd32(node + 0x5C), F_ZERO)) {    /* 001C8B04 */
            uint16_t flags;
            FAULT(step_record(h, g->d275BEC, i, rd16(node + 0x6A), &rec, &span));
            FAULT(decode_translation(h, rec, node + 0x18));
            FAULT(load16(h, rec + 0x8, &flags));
            end = (flags & 0x8000) ? 1 : 0;                  /* 001C8B78..001C8B8C */
            FAULT(load16(h, rec + 0x16, &next));
            span = SUB(ucvt(next), span);                    /* 001C8BBC */
            FAULT(decode_translation(h, rec + 0xC, node + 0x24));
            scaled_difference(g, node + 0x24, node + 0x24, node + 0x18, span);   /* 001C8BDC */
            uint32_t old = rd32(node + 0x5C);
            wr32(node + 0x5C, ADD(old, span));               /* 001C8BEC */
            uint32_t back = em_ee_neg_bits(old);
            for (int k = 0; k < 3; ++k)
                wr32(node + 0x18 + 4 * k,
                     ADD(rd32(node + 0x18 + 4 * k), MUL(back, rd32(node + 0x24 + 4 * k))));
            wr16(node + 0x6A, (uint16_t)(rd16(node + 0x6A) + 1));
        } else {
            for (int k = 0; k < 3; ++k)                      /* 001C8C54 */
                wr32(node + 0x18 + 4 * k,
                     ADD(rd32(node + 0x18 + 4 * k), MUL(dt, rd32(node + 0x24 + 4 * k))));
        }
    }
    if (end || *g->d8106F3) {                                 /* 001C8CB0 / 001C8CBC */
        for (int i = 0; i < n; ++i) {                        /* 001C8CD4 */
            uint8_t *node = node_at(h, bones, i);
            static const unsigned cleared[] = {0x54, 0x0C, 0x10, 0x14, 0x24, 0x28, 0x2C};
            for (unsigned k = 0; k < sizeof cleared / sizeof cleared[0]; ++k)
                wr32(node + cleared[k], F_ZERO);
        }
    }
    return 0;
}

/* 001C8D50 anim_sample_bones(bones, n, new_t, prev_t): per node, freeze the
 * current rotation (nlerp of +30/+40 by +50 into +30), sample the target at
 * new_t through the scratch channel record D_008111F0 and seed a
 * prev_t-long transition toward it: +40 = the target rotation, +60 = +58 =
 * +5C = prev_t, +50 = 0, +54 = 1 / prev_t, the step counters copied, and the
 * translation / scale velocities = (target - current) / prev_t. */
int em_pose_host_001C8D50(EmPoseHost *h, const uint8_t *bones, uint32_t bones_size, int n,
                          float new_t, float prev_t_in)
{
    FAULT(host_ready(h));
    FAULT(nodes_ready(h, bones, bones_size, n, 0));
    EmPoseGlobals *g = h->globals;
    uint8_t *s = g->d8111F0;
    uint32_t prev_t = em_ee_bits(prev_t_in);
    for (int i = 0; i < n; ++i) {
        uint8_t *node = node_at(h, bones, i);
        uint32_t a[4], b[4];
        for (int k = 0; k < 4; ++k) { a[k] = rd32(node + 0x30 + 4 * k); b[k] = rd32(node + 0x40 + 4 * k); }
        em_pose_host_001CA0A0(g->spad3600, a, b, rd32(node + 0x50));
        for (int k = 0; k < 4; ++k) wr32(node + 0x30 + 4 * k, g->spad3600[k]);
        FAULT(sample_rotation(h, s, i, em_player_float_to_int(em_ee_bits(new_t))));
        for (int k = 0; k < 4; ++k) { a[k] = rd32(s + 0x30 + 4 * k); b[k] = rd32(s + 0x40 + 4 * k); }
        em_pose_host_001CA0A0(g->spad3600, a, b, rd32(s + 0x50));
        for (int k = 0; k < 4; ++k) wr32(node + 0x40 + 4 * k, g->spad3600[k]);
        wr32(node + 0x60, prev_t);
        wr16(node + 0x66, rd16(s + 0x66));
        wr32(node + 0x50, F_ZERO);
        wr32(node + 0x54, DIV(F_ONE, prev_t));
        FAULT(sample_translation(h, s, i, em_player_float_to_int(em_ee_bits(new_t))));
        wr32(node + 0x58, prev_t);
        wr16(node + 0x68, rd16(s + 0x68));
        scaled_difference(g, node + 0x0C, s + 0x00, node + 0x00, prev_t);
        FAULT(sample_scale(h, s, i, em_player_float_to_int(em_ee_bits(new_t))));
        wr32(node + 0x5C, prev_t);
        wr16(node + 0x6A, rd16(s + 0x6A));
        scaled_difference(g, node + 0x24, s + 0x18, node + 0x18, prev_t);
    }
    return 0;
}

/* ---- anim_clip_init / bone_init_default_2 / request / arbiter --------------------------- */

static int record_ready(const uint8_t *record, uint32_t size)
{
    return record && size >= EM_POSE_RECORD_MIN ? 0 : -1;
}
static int bones_of(const uint8_t *record, uint32_t size, const uint8_t **bones, uint32_t *bsize)
{
    *bones = record + 0x110;
    *bsize = size - 0x110;
    return 0;
}

/* The reads and mappings 001C67E0 needs, checked before its first store:
 * the clip header of (clip | 0x8000) with its three section words, the
 * node records 0..n-1 and node 0 (its +8E), and the advance worker. */
static int clip_init_ready(EmPoseHost *h, const uint8_t *record, uint32_t size, int clip)
{
    FAULT(host_ready(h));
    FAULT(record_ready(record, size));
    if (!h->callees.advance) return -1;
    const uint8_t *bones;
    uint32_t bsize, header, word;
    bones_of(record, size, &bones, &bsize);
    FAULT(nodes_ready(h, bones, bsize, record[0xC], 1));
    FAULT(em_pose_host_001C6120(h, rd32(record + 0x40), (int16_t)(clip | 0x8000), &header));
    for (unsigned at = 0x8; at <= 0x10; at += 4) FAULT(load32(h, header + at, &word));
    return 0;
}

/* 001C67E0 anim_clip_init(p, clip, blend, frame): +2C = clip | 0x8000 (the
 * transition mark), resolve it; a zero blend (c.eq) sets +3C = 1.0, node 0's
 * +8E = 00128250(frame), samples with prev_t 1.0 and advances once by 1.0
 * (001C64F0); otherwise +3C = blend, the same +8E, and samples with prev_t
 * = blend. */
static int clip_init(EmPoseHost *h, uint8_t *record, uint32_t size, int clip, uint32_t blend,
                     uint32_t frame)
{
    const uint8_t *bones;
    uint32_t bsize;
    bones_of(record, size, &bones, &bsize);
    wr16(record + 0x2C, (uint16_t)(clip | 0x8000));          /* 001C67FC */
    FAULT(em_pose_host_001C8480(h, rd32(record + 0x40), (int16_t)rd16(record + 0x2C)));
    uint8_t *node0 = node_at(h, bones, 0);
    if (em_ee_c_eq_bits(F_ZERO, blend)) {                    /* 001C681C */
        wr32(record + 0x3C, F_ONE);                          /* 001C6830 */
        wr16(node0 + 0x8E, (uint16_t)em_stream_lanes_00128250(frame));   /* 001C684C */
        FAULT(em_pose_host_001C8D50(h, bones, bsize, record[0xC], em_ee_float(frame), 1.0f));
        return h->callees.advance(h->callees.advance_context, record, 1.0f) < 0 ? -1 : 0;   /* 001C6864 */
    }
    wr32(record + 0x3C, blend);                              /* 001C687C */
    wr16(node0 + 0x8E, (uint16_t)em_stream_lanes_00128250(frame));   /* 001C688C */
    return em_pose_host_001C8D50(h, bones, bsize, record[0xC], em_ee_float(frame), em_ee_float(blend));
}

int em_pose_host_001C67E0(EmPoseHost *h, uint8_t *record, uint32_t size, int clip, float blend,
                          float frame)
{
    FAULT(clip_init_ready(h, record, size, clip));
    return clip_init(h, record, size, clip, em_ee_bits(blend), em_ee_bits(frame));
}

/* 001C63E0 bone_init_default_2(p, clip): +2C = clip, resolve; +3C = the
 * clip's frame count (halfword +2 of the header, unsigned); per node i:
 * +64 = the header's halfword at +20 + 4i (the parent), +88/+8A/+8C =
 * 0x1000, +7C/+80/+84 and +70/+74/+78 = 0; then 001C8710(p+110, n, 0.0). */
int em_pose_host_001C63E0(EmPoseHost *h, uint8_t *record, uint32_t size, int clip)
{
    FAULT(host_ready(h));
    FAULT(record_ready(record, size));
    const uint8_t *bones;
    uint32_t bsize, header, word;
    bones_of(record, size, &bones, &bsize);
    int n = record[0xC];
    FAULT(nodes_ready(h, bones, bsize, n, 0));
    FAULT(em_pose_host_001C6120(h, rd32(record + 0x40), (int16_t)clip, &header));
    for (unsigned at = 0x8; at <= 0x10; at += 4) FAULT(load32(h, header + at, &word));
    for (int i = 0; i < n; ++i) {
        uint16_t parent;
        FAULT(load16(h, header + 0x20 + 4 * (uint32_t)i, &parent));
    }

    wr16(record + 0x2C, (uint16_t)clip);                     /* 001C63EC */
    FAULT(em_pose_host_001C8480(h, rd32(record + 0x40), (int16_t)rd16(record + 0x2C)));
    EmPoseGlobals *g = h->globals;
    uint16_t frames;
    FAULT(load16(h, g->d275BF8 + 2, &frames));
    wr32(record + 0x3C, ucvt(frames));                       /* 001C6438 */
    uint32_t src = g->d275BF8 + 0x20;
    for (int i = 0; i < n; ++i) {                            /* 001C6454 */
        uint8_t *node = node_at(h, bones, i);
        uint16_t parent;
        FAULT(load16(h, src, &parent));
        wr16(node + 0x64, parent);
        wr16(node + 0x88, 0x1000);
        wr16(node + 0x8A, 0x1000);
        wr16(node + 0x8C, 0x1000);
        wr32(node + 0x7C, 0); wr32(node + 0x80, 0); wr32(node + 0x84, 0);
        wr32(node + 0x70, 0); wr32(node + 0x74, 0); wr32(node + 0x78, 0);
        src += 4;
    }
    return em_pose_host_001C8710(h, bones, bsize, n, 0.0f); /* 001C64CC */
}

/* 001749A0(p, clip, flags, blend): with flags 0 an equal (sign-extended
 * halfword) +20C returns 1; otherwise +20C = clip and anim_clip_init(p, +20C,
 * blend, 0.0), returning 0. */
int em_pose_host_001749A0(EmPoseHost *h, uint8_t *record, uint32_t size, int clip, int flags,
                          float blend, int *result)
{
    FAULT(host_ready(h));
    FAULT(record_ready(record, size));
    if (!result) return -1;
    if (flags == 0 && (int16_t)clip == (int16_t)rd16(record + 0x20C)) {   /* 001749A4..001749B8 */
        *result = 1;
        return 0;
    }
    FAULT(clip_init_ready(h, record, size, (int16_t)clip));
    wr16(record + 0x20C, (uint16_t)clip);                    /* 001749C8 */
    FAULT(clip_init(h, record, size, (int16_t)rd16(record + 0x20C), em_ee_bits(blend), F_ZERO));
    *result = 0;
    return 0;
}

/* 001749F0 anim_clip_arbiter(p, clip, blend, frame): anim_clip_init first,
 * then a different (sign-extended halfword) +20C is replaced and returns
 * 1; an equal one returns 0. */
int em_pose_host_001749F0(EmPoseHost *h, uint8_t *record, uint32_t size, int clip, float blend,
                          float frame, int *result)
{
    if (!result) return -1;
    FAULT(clip_init_ready(h, record, size, clip));
    FAULT(clip_init(h, record, size, clip, em_ee_bits(blend), em_ee_bits(frame)));   /* 00174A04 */
    if ((int16_t)clip != (int16_t)rd16(record + 0x20C)) {    /* 00174A18 */
        wr16(record + 0x20C, (uint16_t)clip);
        *result = 1;
    } else {
        *result = 0;
    }
    return 0;
}

/* ---- matrices: 001029C0, 00102C58, build_trs_matrix (bound, not translated) ---- */

typedef uint32_t Row[4];

/* The SDK matrix routines are em_owner_services_original.c's translations
 * (verified there by tools/test_owner_services_reference.py and here by
 * this module's oracle). They move every word with memcpy, so the raw
 * words of these uint32_t matrices pass through them unchanged, and they
 * read each argument where the original reads it: an angle just before its
 * rotation, the scale and position after the rotations, a source row just
 * before its result row is stored. Passing the caller's own pointers keeps
 * the original's behaviour when an argument lies inside the output. */
static int ee_ok(int status) { return status == EM_EE_FLOAT_OK ? 0 : -1; }

/* 001029C0. */
static int identity(uint32_t m[16])
{
    return ee_ok(em_owner_services_identity_001029C0((float *)m));
}

/* 00102C58(out, in, v): 00102A60 (z = v[2]) from in to out, then 00102BB0
 * (y = v[1]) and 00102B08 (x = v[0]) in place. */
static int euler(uint32_t out[16], const uint32_t in[16], const uint32_t v[3])
{
    return ee_ok(em_owner_services_euler_00102C58((float *)out, (const float *)in, (const float *)v));
}

/* 001C94B0 build_trs_matrix(out, position, rotation, scale): out = the
 * identity rotated about X, Y then Z, rows 0..2 xyz times scale x, y, z,
 * then 00102918 adds the position to row 3 xyz. */
int em_pose_host_build_trs_matrix(void *host, uint32_t out[16], const uint32_t position[3],
                                  const uint32_t rotation[3], const uint32_t scale[3])
{
    (void)host;
    if (!out || !position || !rotation || !scale) return -1;
    return ee_ok(em_owner_services_build_trs_matrix((float *)out, (const float *)position,
                                                    (const float *)rotation, (const float *)scale));
}

/* out row j = ACC chain over the left rows by right row j's lanes:
 * left0 * r.x, + left1 * r.y, + left2 * r.z (VU ACC), then + left3 * r.w.
 * Left is read in full before the first store (the original loads it into
 * four registers); each right row is read just before its output row. */
static int combine(uint32_t out[16], const uint32_t left_in[16], const uint32_t *right)
{
    uint32_t left[16], acc[4], row[4];
    memcpy(left, left_in, sizeof left);
    for (int j = 0; j < 4; ++j) {
        memcpy(row, right + 4 * j, sizeof row);
        FAULT(vu(EM_VU_MULABC, 15, 0, left + 0, row, NULL, acc));
        FAULT(vu(EM_VU_MADDABC, 15, 1, left + 4, row, acc, acc));
        FAULT(vu(EM_VU_MADDABC, 15, 2, left + 8, row, acc, acc));
        uint32_t result[4];
        FAULT(vu(EM_VU_MADDBC, 15, 3, left + 12, row, acc, result));
        memcpy(out + 4 * j, result, sizeof result);
    }
    return 0;
}

static void load_matrix(uint32_t m[16], const uint8_t *p)
{
    for (int k = 0; k < 16; ++k) m[k] = rd32(p + 4 * k);
}
static void store_matrix(uint8_t *p, const uint32_t m[16])
{
    for (int k = 0; k < 16; ++k) wr32(p + 4 * k, m[k]);
}

/* The local basis L of one node (0x70003440): the identity rotated by the
 * Euler angles +70 (00102C58), row 3 xyz = +7C/+80/+84 (raw), rows 0..2 xyz
 * times 2^-12 * the halfwords +88/+8A/+8C; then L = R x L with R at
 * 0x70003400. */
static int local_basis(EmPoseHost *h, const uint8_t *node)
{
    EmPoseGlobals *g = h->globals;
    uint32_t angles[3] = {rd32(node + 0x70), rd32(node + 0x74), rd32(node + 0x78)};
    FAULT(identity(g->spad3440));
    FAULT(euler(g->spad3440, g->spad3440, angles));
    g->spad3440[12] = rd32(node + 0x7C);
    g->spad3440[13] = rd32(node + 0x80);
    g->spad3440[14] = rd32(node + 0x84);
    Row *L = (Row *)g->spad3440;
    for (int k = 0; k < 3; ++k) {
        uint32_t s = MUL(F_4096TH, CVT((int32_t)(int16_t)rd16(node + 0x88 + 2 * k)));
        uint32_t sv[4] = {s, 0, 0, 0};
        FAULT(vu(EM_VU_MULBC, 14, 0, L[k], sv, NULL, L[k]));
    }
    return combine(g->spad3440, g->spad3400, g->spad3440);
}

/* R for a node with channels: nlerp(+30, +40, +50) into 0x70003600,
 * quat_to_mat3 with the node's +0 translation into 0x70003400, rows 0..2
 * xyz times the scale words +18/+1C/+20. */
static int channel_rotation(EmPoseHost *h, const uint8_t *node)
{
    EmPoseGlobals *g = h->globals;
    uint32_t a[4], b[4], t[3];
    for (int k = 0; k < 4; ++k) { a[k] = rd32(node + 0x30 + 4 * k); b[k] = rd32(node + 0x40 + 4 * k); }
    em_pose_host_001CA0A0(g->spad3600, a, b, rd32(node + 0x50));
    for (int k = 0; k < 3; ++k) t[k] = rd32(node + 4 * k);
    em_pose_host_001CA1C0(g->spad3400, g->spad3600, t, g->spad3760);
    Row *R = (Row *)g->spad3400;
    for (int k = 0; k < 3; ++k) {
        uint32_t sv[4] = {rd32(node + 0x18 + 4 * k), 0, 0, 0};
        FAULT(vu(EM_VU_MULBC, 14, 0, R[k], sv, NULL, R[k]));
    }
    return 0;
}

/* world (+90) = P x L, where P is the parent's world matrix (the node
 * pointer at parent index of the array) or `root` for parent -1. */
static int world(EmPoseHost *h, const uint8_t *bones, uint32_t bsize, uint8_t *node, const uint8_t *root)
{
    int16_t parent = (int16_t)rd16(node + 0x64);
    uint32_t P[16], out[16];
    if (parent != -1) {
        int64_t at = 4 * (int64_t)parent;
        if (at < -0x110 || at + 4 > (int64_t)bsize) return -1;   /* outside the record */
        const uint8_t *pn = map(h, rd32(bones + at) + 0x90, 0x40, 0);
        if (!pn) return -1;
        load_matrix(P, pn);
    } else {
        load_matrix(P, root);
    }
    FAULT(combine(out, P, h->globals->spad3440));
    store_matrix(node + 0x90, out);
    return 0;
}

/* The parent reads of nodes first..n-1 resolve (a read-only check). */
static int parents_ready(const EmPoseHost *h, const uint8_t *bones, uint32_t bsize, int first, int n)
{
    for (int i = first; i < n; ++i) {
        const uint8_t *node = map(h, node_word(bones, i), EM_POSE_NODE_BYTES, 1);
        if (!node) return -1;
        int16_t parent = (int16_t)rd16(node + 0x64);
        if (parent == -1) continue;
        int64_t at = 4 * (int64_t)parent;
        if (at < -0x110 || at + 4 > (int64_t)bsize) return -1;
        if (!map(h, rd32(bones + at) + 0x90, 0x40, 0)) return -1;
    }
    return 0;
}

/* 001C9940(bones, n, root): for every node, R from its channels, L from its
 * basis, world = parent x L (root for parent -1). */
int em_pose_host_001C9940(EmPoseHost *h, const uint8_t *bones, uint32_t bsize, int n, const uint8_t *root)
{
    FAULT(host_ready(h));
    if (!root) return -1;
    FAULT(nodes_ready(h, bones, bsize, n, 0));
    FAULT(parents_ready(h, bones, bsize, 0, n));
    for (int i = 0; i < n; ++i) {                            /* 001C9970 */
        uint8_t *node = node_at(h, bones, i);
        FAULT(channel_rotation(h, node));
        FAULT(local_basis(h, node));
        FAULT(world(h, bones, bsize, node, root));
    }
    return 0;
}

/* 001C6DA0 anim_eval_skeleton(p): build_trs_matrix(+D0, +B0, +C0, +60);
 * node 0 with R = the identity (no channel rotation) and nodes 1..+C-1
 * with their channels, each world = parent x L (parent -1: the +D0). */
int em_pose_host_001C6DA0(EmPoseHost *h, uint8_t *record, uint32_t size)
{
    FAULT(host_ready(h));
    FAULT(record_ready(record, size));
    const uint8_t *bones;
    uint32_t bsize;
    bones_of(record, size, &bones, &bsize);
    int n = record[0xC];
    FAULT(nodes_ready(h, bones, bsize, n, 1));
    FAULT(parents_ready(h, bones, bsize, 0, n > 1 ? n : 1));

    uint32_t m[16], pos[3], rot[3], sc[3];
    for (int k = 0; k < 3; ++k) {
        pos[k] = rd32(record + 0xB0 + 4 * k);
        rot[k] = rd32(record + 0xC0 + 4 * k);
        sc[k] = rd32(record + 0x60 + 4 * k);
    }
    load_matrix(m, record + 0xD0);
    FAULT(em_pose_host_build_trs_matrix(h, m, pos, rot, sc));   /* 001C6DC4 */
    store_matrix(record + 0xD0, m);

    EmPoseGlobals *g = h->globals;
    uint8_t *node0 = node_at(h, bones, 0);
    FAULT(identity(g->spad3400));                            /* 001C6DD4 */
    FAULT(local_basis(h, node0));
    FAULT(world(h, bones, bsize, node0, record + 0xD0));
    for (int i = 1; i < n; ++i) {                            /* 001C7094 */
        uint8_t *node = node_at(h, bones, i);
        FAULT(channel_rotation(h, node));
        FAULT(local_basis(h, node));
        FAULT(world(h, bones, bsize, node, record + 0xD0));
    }
    return 0;
}

/* 001C68C0(o): build_trs_matrix(+D0, +B0, +C0, +60), then 001C9940(+110,
 * +C, +D0). */
int em_pose_host_001C68C0(EmPoseHost *h, uint8_t *record, uint32_t size)
{
    FAULT(host_ready(h));
    FAULT(record_ready(record, size));
    const uint8_t *bones;
    uint32_t bsize;
    bones_of(record, size, &bones, &bsize);
    FAULT(nodes_ready(h, bones, bsize, record[0xC], 0));
    FAULT(parents_ready(h, bones, bsize, 0, record[0xC]));
    uint32_t m[16], pos[3], rot[3], sc[3];
    for (int k = 0; k < 3; ++k) {
        pos[k] = rd32(record + 0xB0 + 4 * k);
        rot[k] = rd32(record + 0xC0 + 4 * k);
        sc[k] = rd32(record + 0x60 + 4 * k);
    }
    load_matrix(m, record + 0xD0);
    FAULT(em_pose_host_build_trs_matrix(h, m, pos, rot, sc));
    store_matrix(record + 0xD0, m);
    return em_pose_host_001C9940(h, bones, bsize, record[0xC], record + 0xD0);
}

/* 001C6960(o): 001029C0(+D0), then 001C9940(+110, +C, +D0). */
int em_pose_host_001C6960(EmPoseHost *h, uint8_t *record, uint32_t size)
{
    FAULT(host_ready(h));
    FAULT(record_ready(record, size));
    const uint8_t *bones;
    uint32_t bsize;
    bones_of(record, size, &bones, &bsize);
    FAULT(nodes_ready(h, bones, bsize, record[0xC], 0));
    FAULT(parents_ready(h, bones, bsize, 0, record[0xC]));
    uint32_t m[16];
    FAULT(identity(m));
    store_matrix(record + 0xD0, m);
    return em_pose_host_001C9940(h, bones, bsize, record[0xC], record + 0xD0);
}

/* ---- 00178910: the ledge-top column search ------------------------------------------------ */

/* 00178910(p, arg). The column query point 0x700038B0 is the sweep hit
 * backed off along the hit normal (by 4 * normal when +316 is set, else by
 * the normal; x and z), y = 20.5 + +B4, w = 1.0; 0019BC40 fills the column
 * table. The first column with flags bit 0 and aux below 0.62831855 whose
 * |query y - height| (0011DF78, stored at 0x70003A20) is below 1.0 returns
 * 1; with arg nonzero it first stores the ledge point +2E0 / +2E8 = hit +
 * 1.5 * normal, +2E4 = height - 20.5, and the heading +218 = 001B1470(
 * 4.712389 + atan2(-nz, nx)) (the atan2 also to 0x70003A20). No column: 0. */
int em_pose_host_00178910(EmPoseHost *h, uint8_t *record, uint32_t size, int arg, int *result)
{
    FAULT(host_ready(h));
    if (!record || size < 0x318 || !result) return -1;
    const EmPoseHostCallees *c = &h->callees;
    if (!c->column || !c->ledge_hit || !c->atan2) return -1;
    EmPoseGlobals *g = h->globals;
    EmPoseLedgeHit hit;
    FAULT(c->ledge_hit(c->context, &hit));                   /* 00178944 / 00178990 */
    if (record[0x316]) {                                     /* 00178930 */
        g->spad38B0[0] = SUB(hit.x, MUL(F_4, hit.nx));       /* 00178968 */
        g->spad38B0[2] = SUB(hit.z, MUL(F_4, hit.nz));       /* 00178980 */
    } else {
        g->spad38B0[0] = SUB(hit.x, hit.nx);                 /* 001789A8 */
        g->spad38B0[2] = SUB(hit.z, hit.nz);                 /* 001789BC */
    }
    g->spad38B0[1] = ADD(F_20_5, rd32(record + 0xB4));       /* 001789DC */
    g->spad38B0[3] = F_ONE;                                  /* 001789F0 */
    float point[3] = {em_ee_float(g->spad38B0[0]), em_ee_float(g->spad38B0[1]),
                      em_ee_float(g->spad38B0[2])};
    EmPlayerFloorTable *t = g->column;
    FAULT(c->column(c->context, point, t));                  /* 001789EC */
    if (t->count > EM_PLAYER_FLOOR_TABLE_MAX) return -1;
    for (int i = 0; i < t->count; ++i) {                     /* 00178A14 / 00178B50 */
        if (!(t->flags[i] & 1)) continue;
        if (!em_ee_c_lt_bits(em_ee_bits(t->aux[i]), F_SLOPE)) continue;   /* 00178A38 */
        uint32_t d = SUB(g->spad38B0[1], em_ee_bits(t->height[i])) & UINT32_C(0x7FFFFFFF);
        *g->spad3A20 = d;                                    /* 00178A74 */
        if (!em_ee_c_lt_bits(d, F_ONE)) continue;            /* 00178A68 */
        if (arg) {                                           /* 00178A78 */
            FAULT(c->ledge_hit(c->context, &hit));           /* 00178A84 */
            /* The atan2 worker (00178B00) is pure: its arguments come from
             * this hit, so it is called here, before the record stores, and
             * a heading 001B1470 could not reduce is refused with the
             * record unwritten. 001B1470 subtracts (adds) 2pi until the
             * angle is at most pi (above -pi); with the EE's rounding an
             * angle of biased exponent 0x9A or more (and the clamped
             * non-finite encodings) is left unchanged by that step, so the
             * original never returns. Every smaller angle reduces, and is
             * reduced here exactly as there. A real atan2 (|result| <= pi)
             * never gets near. */
            uint32_t angle = em_ee_bits(c->atan2(c->context, em_ee_float(em_ee_neg_bits(hit.nz)),
                                                 em_ee_float(hit.nx)));
            uint32_t heading = ADD(F_3PI_2, angle);          /* 00178B28 */
            if ((heading >> 23 & 0xFF) >= 0x9A) return -1;
            uint8_t *p = record;
            wr32(p + 0x2E0, ADD(hit.x, MUL(F_1_5, hit.nx)));  /* 00178AB8 */
            wr32(p + 0x2E8, ADD(hit.z, MUL(F_1_5, hit.nz)));  /* 00178AE0 */
            wr32(p + 0x2E4, SUB(em_ee_bits(t->height[i]), F_20_5));   /* 00178AEC */
            *g->spad3A20 = angle;                            /* 00178B00 */
            wr32(p + 0x218, em_player_001B1470(heading));    /* 00178B24 */
        }
        *result = 1;
        return 0;
    }
    *result = 0;
    return 0;
}

/* ---- adapters ------------------------------------------------------------------------------- */

#define LIVE(a) (a)->bytes, (uint32_t)EM_PLAYER_ACTOR_SIZE

int em_pose_host_request(void *host, EmPlayerLiveActor *a, int clip, int flags, float blend)
{
    int result;
    if (!a) return -1;
    return em_pose_host_001749A0(host, LIVE(a), clip, flags, blend, &result);
}
int em_pose_host_arbiter(void *host, EmPlayerLiveActor *a, int clip, float blend, float frame)
{
    int result;
    if (!a) return -1;
    return em_pose_host_001749F0(host, LIVE(a), clip, blend, frame, &result);
}
int em_pose_host_clip_frames(void *host, uint32_t bank, int clip, int32_t *frames)
{
    return em_pose_host_001C61D0(host, bank, clip, frames);
}
int em_pose_host_clip_frames_actor(void *host, EmPlayerLiveActor *a, int clip, int *frames)
{
    int32_t count;
    if (!a || !frames) return -1;
    FAULT(em_pose_host_001C61D0(host, em_live_u32(a, 0x40), clip, &count));
    *frames = count;
    return 0;
}
int em_pose_host_eval_skeleton(void *host, EmPlayerLiveActor *a)
{
    return a ? em_pose_host_001C6DA0(host, LIVE(a)) : -1;
}
int em_pose_host_skeleton(void *host, EmPlayerLiveActor *a)
{
    return a ? em_pose_host_001C68C0(host, LIVE(a)) : -1;
}
/* 0017C540 is em_player_reaction_0017C540 (verified by
 * tools/test_player_reaction_reference.py; re-checked by this lane's
 * oracle). It uses nothing of the host. */
int em_pose_host_handoff(void *host, EmPlayerLiveActor *a)
{
    (void)host;
    if (!a) return -1;
    em_player_reaction_0017C540(a);
    return 0;
}
int em_pose_host_ledge_top(void *host, EmPlayerLiveActor *a, int arg, int *result)
{
    return a ? em_pose_host_00178910(host, LIVE(a), arg, result) : -1;
}
int em_pose_host_node_word(void *host, uint32_t node_address, unsigned offset, uint32_t *out)
{
    EmPoseHost *h = host;
    if (!h || !out) return -1;
    return load32(h, node_address + offset, out);
}

int em_pose_host_stage_bone_init(void *host, EmPlayerLiveActor *a, int clip)
{
    return a ? em_pose_host_001C63E0(host, LIVE(a), clip) : -1;
}
int em_pose_host_stage_clip_init(void *host, EmPlayerLiveActor *a, int clip, float blend, float frame)
{
    return a ? em_pose_host_001C67E0(host, LIVE(a), clip, blend, frame) : -1;
}
/* anim_advance_time's resolve: 001C8480 with its side effects, and the
 * header fields the stage worker reads (frames +2, next +4, start +6, the
 * event table at header + [+14]: its count, then {frame, flags} pairs). */
int em_pose_host_stage_clip_resolve(void *host, uint32_t bank, int clip, EmPlayerClipHeader *hd)
{
    EmPoseHost *h = host;
    if (!hd) return -1;
    FAULT(em_pose_host_001C8480(h, bank, clip));
    uint32_t a = h->globals->d275BF8, events;
    uint16_t frames, next, start, count;
    FAULT(load16(h, a + 2, &frames));
    FAULT(load16(h, a + 4, &next));
    FAULT(load16(h, a + 6, &start));
    FAULT(load32(h, a + 0x14, &events));
    hd->frames = frames;
    hd->next = (int16_t)next;
    hd->start = (int16_t)start;
    hd->events = events;
    hd->event_count = 0;
    hd->event_pairs = h->events;
    if (events) {
        FAULT(load16(h, a + events, &count));
        hd->event_count = (int16_t)count;
        int pairs = (int16_t)count;
        if (pairs > EM_POSE_EVENT_MAX) return -1;
        for (int k = 0; k < 2 * pairs; ++k) {
            uint16_t word;
            FAULT(load16(h, a + events + 4 + 2 * (uint32_t)k, &word));
            h->events[k] = (int16_t)word;
        }
    }
    return 0;
}
int em_pose_host_stage_skeleton_frame(void *host, uint32_t skeleton, int16_t *frame)
{
    EmPoseHost *h = host;
    uint16_t word;
    if (!h || !frame) return -1;
    FAULT(load16(h, skeleton + 0x8E, &word));
    *frame = (int16_t)word;
    return 0;
}
int em_pose_host_stage_8710(void *host, EmPlayerLiveActor *a, int nodes, float frame)
{
    if (!a) return -1;
    return em_pose_host_001C8710(host, a->bytes + 0x110, EM_PLAYER_ACTOR_SIZE - 0x110, nodes, frame);
}
int em_pose_host_stage_87C0(void *host, EmPlayerLiveActor *a, int nodes, float step)
{
    if (!a) return -1;
    return em_pose_host_001C87C0(host, a->bytes + 0x110, EM_PLAYER_ACTOR_SIZE - 0x110, nodes, step);
}
int em_pose_host_stage_sample_bones(void *host, EmPlayerLiveActor *a, int nodes, float new_t, float prev_t)
{
    if (!a) return -1;
    return em_pose_host_001C8D50(host, a->bytes + 0x110, EM_PLAYER_ACTOR_SIZE - 0x110, nodes, new_t,
                                 prev_t);
}
int em_pose_host_stage_request(void *host, EmPlayerLiveActor *a, int clip, int flags, float blend)
{
    return em_pose_host_request(host, a, clip, flags, blend);
}

int em_pose_host_player_advance(void *stage_host, uint8_t *record, float step)
{
    uint32_t flags;
    if (!stage_host || !record) return -1;
    return em_player_stage_anim_advance(stage_host, (EmPlayerLiveActor *)(void *)record, step, &flags) < 0
               ? -1 : 0;
}

/* ---- bound-actor view ----------------------------------------------------------------------- */

static int view_ready(const EmPoseActorView *v)
{
    return v && v->host && v->actor ? 0 : -1;
}
int em_pose_view_request(void *view, int clip, int force, float blend)
{
    EmPoseActorView *v = view;
    FAULT(view_ready(v));
    return em_pose_host_request(v->host, v->actor, clip, force, blend);
}
int em_pose_view_arbiter(void *view, int clip, float blend, float frame)
{
    EmPoseActorView *v = view;
    FAULT(view_ready(v));
    return em_pose_host_arbiter(v->host, v->actor, clip, blend, frame);
}
int em_pose_view_clip_frames(void *view, int clip, int *frames)
{
    EmPoseActorView *v = view;
    FAULT(view_ready(v));
    return em_pose_host_clip_frames_actor(v->host, v->actor, clip, frames);
}
/* *(D_00275B40 + 4 * node), then the word at that node + offset. */
int em_pose_view_node_bits(void *view, int node, unsigned offset, uint32_t *bits)
{
    EmPoseActorView *v = view;
    uint32_t address;
    if (!v || !v->host || !v->d275B40 || !bits) return -1;
    FAULT(load32(v->host, *v->d275B40 + 4u * (uint32_t)node, &address));
    return load32(v->host, address + offset, bits);
}
int em_pose_view_node_float(void *view, int node, unsigned offset, float *value)
{
    uint32_t bits;
    if (!value) return -1;
    FAULT(em_pose_view_node_bits(view, node, offset, &bits));
    *value = em_ee_float(bits);
    return 0;
}
int em_pose_view_root_node(void *view, unsigned offset, uint32_t *bits)
{
    return em_pose_view_node_bits(view, 0, offset, bits);
}
/* anim_eval_skeleton(p), then `count` words of node 1 from +C0 (00162A40
 * state 0xA reads four, the reaction lane's 0021D2E0 three). The node reads
 * are checked mapped before the evaluation writes anything. */
static int eval_node1(void *view, EmPlayerLiveActor *actor, float *node1, unsigned count)
{
    EmPoseActorView *v = view;
    uint32_t bits;
    if (!v || !v->host || !v->d275B40 || !actor || !node1) return -1;
    for (unsigned k = 0; k < count; ++k) FAULT(em_pose_view_node_bits(view, 1, 0xC0 + 4 * k, &bits));
    FAULT(em_pose_host_eval_skeleton(v->host, actor));
    for (unsigned k = 0; k < count; ++k) {
        FAULT(em_pose_view_node_bits(view, 1, 0xC0 + 4 * k, &bits));
        node1[k] = em_ee_float(bits);
    }
    return 0;
}
int em_pose_view_eval_node1(void *view, EmPlayerLiveActor *actor, float node1[4])
{
    return eval_node1(view, actor, node1, 4);
}
int em_pose_view_eval_node1_xyz(void *view, EmPlayerLiveActor *actor, float node1[3])
{
    return eval_node1(view, actor, node1, 3);
}
int em_pose_view_eval_hip(void *view, float *hip_y, float *hip_8)
{
    EmPoseActorView *v = view;
    uint32_t y, w8;
    if (view_ready(v) < 0 || !v->d275B40 || !hip_y || !hip_8) return -1;
    FAULT(em_pose_view_node_bits(view, 1, 0xC4, &y));
    FAULT(em_pose_view_node_bits(view, 1, 0x8, &w8));
    FAULT(em_pose_host_eval_skeleton(v->host, v->actor));
    FAULT(em_pose_view_node_bits(view, 1, 0xC4, &y));
    FAULT(em_pose_view_node_bits(view, 1, 0x8, &w8));
    *hip_y = em_ee_float(y);
    *hip_8 = em_ee_float(w8);
    return 0;
}

/* `count` words from *(D_00275B40 + 4 * node) + offset; all are checked
 * mapped before the first is stored. */
static int node_words(void *view, int node, unsigned offset, unsigned count, uint32_t *out)
{
    uint32_t bits;
    if (!out) return -1;
    for (unsigned k = 0; k < count; ++k) FAULT(em_pose_view_node_bits(view, node, offset + 4 * k, &bits));
    for (unsigned k = 0; k < count; ++k) FAULT(em_pose_view_node_bits(view, node, offset + 4 * k, &out[k]));
    return 0;
}
int em_pose_view_hip_xz(void *view, uint32_t *x, uint32_t *z)
{
    uint32_t bx, bz;
    if (!x || !z) return -1;
    FAULT(em_pose_view_node_bits(view, 1, 0xC0, &bx));
    FAULT(em_pose_view_node_bits(view, 1, 0xC8, &bz));
    *x = bx;
    *z = bz;
    return 0;
}
int em_pose_view_node1_words(void *view, uint32_t out[4])
{
    return node_words(view, 1, 0xC0, 4, out);
}
int em_pose_view_root_clock(void *view, uint32_t *value)
{
    return em_pose_view_node_bits(view, 0, 0x8, value);
}
int em_pose_view_bone(void *view, unsigned slot, uint32_t words[16])
{
    return node_words(view, (int)slot, 0x90, 16, words);
}

/* ---- signature variants ------------------------------------------------------------------ */

/* 001749A0 with the blend as the raw $f12 bit pattern (EmPlayerWeaponBWorkers). */
int em_pose_host_request_bits(void *host, EmPlayerLiveActor *actor, int clip, int force, uint32_t blend)
{
    return em_pose_host_request(host, actor, clip, force, em_ee_float(blend));
}
/* 00102C58 with a void context (EmPlayerLadderWorkers.euler); the host is
 * not read. */
int em_pose_host_euler(void *host, uint32_t out[16], const uint32_t in[16], const uint32_t angles[3])
{
    (void)host;
    if (!out || !in || !angles) return -1;
    return euler(out, in, angles);
}
