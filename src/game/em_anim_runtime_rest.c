/* em_anim_runtime_rest.c - the remaining animation-runtime originals (see
 * em_anim_runtime_rest.h and docs/ANIM_RUNTIME_REST.md).
 *
 * Read from the original instructions (the decomp's build/asm): 001C9E40 is
 * an asm-word file, 001C7900, 001CB2C0 and 001CAAC0 are NEARMISS C whose .s
 * was followed; 001C9D50, 001CACB0 and 001CB5B0 are byte-matched C. Every
 * address in a comment is the original instruction translated there.
 * Comments describe what the original computes; they never reproduce its
 * instruction stream. Float arithmetic, compares and VU0 lanes go through
 * em_ee_float.h on bit patterns, under the forms the originals execute. */
#include "game/em_anim_runtime_rest.h"

#include "game/em_ee_float.h"
#include "game/em_sdk_math_original.h"

#include <stddef.h>
#include <string.h>

typedef uint32_t u32;

#define F_ONE  UINT32_C(0x3F800000)   /* built at 0x001C9D68 / 0x001C9E80 and the like */
#define F_HALF UINT32_C(0x3F000000)   /* built at 0x001C9E90 and the like */

/* Dest masks (x = 8, y = 4, z = 2, w = 1). */
#define DX 8u
#define DXYZ 14u
#define DXYZW 15u
#define NO_BC EM_VU_NO_BC

/* The VU constant register: (0, 0, 0, 1.0). */
static const u32 VF0[4] = {0, 0, 0, EM_EE_ONE};

/* ======================================================================
 * Faults
 * ==================================================================== */

static int latched(const EmAnimRest *r) { return r->fault.code != EM_ANIM_REST_FAULT_NONE; }

static int fault(EmAnimRest *r, u32 address, int32_t code)
{
    if (!latched(r)) {
        r->fault.address = address;
        r->fault.code = code;
    }
    return -1;
}

#define NEED(r, ptr, address) \
    do { if (!(ptr)) return fault((r), (address), EM_ANIM_REST_FAULT_NULL); } while (0)
#define CALL(r, address, expr) \
    do { if ((expr) < 0) return fault((r), (address), EM_ANIM_REST_FAULT_WORKER); } while (0)
#define FLOAT(r, address, expr) \
    do { if ((expr) != EM_EE_FLOAT_OK) return fault((r), (address), EM_ANIM_REST_FAULT_UNMEASURED); } while (0)

/* ======================================================================
 * Raw access
 * ==================================================================== */

static u32 rd32(const uint8_t *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static void put32(uint8_t *p, u32 v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void put_words(uint8_t *p, const u32 *w, int n)
{
    for (int i = 0; i < n; ++i) put32(p + 4 * i, w[i]);
}

/* DMA tag of a display-list packet: byte +3 = 0x10, word +4 = 0, halfword
 * +0 = qwc. Bytes +2 and +8..+0xF are left as they are. */
static void put_tag(uint8_t *tag, u32 qwc)
{
    tag[3] = 0x10;
    put32(tag + 4, 0);
    tag[0] = (uint8_t)qwc;
    tag[1] = (uint8_t)(qwc >> 8);
}

/* The host bytes of the EE range [address, address + len), or NULL. */
static const uint8_t *map(const EmAnimRest *r, u32 address, u32 len)
{
    const EmAnimRestWorld *w = &r->world;
    for (unsigned i = 0; i < w->region_count && i < EM_POSE_REGION_MAX; ++i) {
        const EmPoseRegion *g = &w->region[i];
        if (!g->bytes || len > g->size) continue;
        u32 off = address - g->address;
        if (address < g->address || off > g->size - len) continue;
        return g->bytes + off;
    }
    return NULL;
}

/* The channel at D_00275670 + 0x10 + 4 * chan with at least `room` bytes
 * after its cursor, or NULL (fault latched). */
static EmOwnerServicesChannel *channel(EmAnimRest *r, int32_t chan, size_t room, u32 function)
{
    if (!r->world.channel) { fault(r, 0x00275670u, EM_ANIM_REST_FAULT_NULL); return NULL; }
    if (chan < 0 || (u32)chan >= r->world.channel_count) {
        fault(r, 0x00275670u, EM_ANIM_REST_FAULT_BAD_INDEX);
        return NULL;
    }
    EmOwnerServicesChannel *ch = &r->world.channel[chan];
    if (!ch->cursor) { fault(r, 0x00275670u, EM_ANIM_REST_FAULT_NULL); return NULL; }
    if (!ch->end || ch->end < ch->cursor || (size_t)(ch->end - ch->cursor) < room) {
        fault(r, function, EM_ANIM_REST_FAULT_BAD_INDEX);
        return NULL;
    }
    return ch;
}

/* ======================================================================
 * VU0 macro plumbing
 * ==================================================================== */

/* One VU0 macro instruction; `*st` is sticky (nothing runs after a
 * refusal, and the caller faults). */
static void vu(int *st, em_vu_op op, unsigned dest, int bc, const u32 fs[4], const u32 ft[4], u32 q,
               const u32 acc[4], u32 dst[4])
{
    if (*st != EM_EE_FLOAT_OK) return;
    *st = em_vu_vec_bits(op, dest, bc, fs, ft, q, acc, dst);
}

/* out = M row 0 * v.x + M row 1 * v.y + M row 2 * v.z + M row 3 * v.w, all
 * four lanes, summed left to right in the VU accumulator (forms MULABC /0,
 * MADDABC /1 and /2, and MADDBC /3 writing out). */
static void vu_row(int *st, u32 out[4], const u32 m[16], const u32 v[4])
{
    u32 acc[4] = {0, 0, 0, 0};
    vu(st, EM_VU_MULABC, DXYZW, 0, m, v, 0, NULL, acc);
    vu(st, EM_VU_MADDABC, DXYZW, 1, m + 4, v, 0, acc, acc);
    vu(st, EM_VU_MADDABC, DXYZW, 2, m + 8, v, 0, acc, acc);
    vu(st, EM_VU_MADDBC, DXYZW, 3, m + 12, v, 0, acc, out);
}

/* One row normalised (xyz), w cleared: p = row.xyz * row.xyz (MUL xyz);
 * len2 = (p.x + p.y) + p.z in lane x; Q = VU square root of len2; len =
 * 0 + Q; Q = 1.0 / len (VU divide, form (3,0)); out = (0, 0, 0, 0) (SUB of
 * the constant register with itself), then out.xyz = row.xyz * Q. The w
 * lane of p is never read. */
static int normalise_row(u32 out[4], const u32 row[4])
{
    int st = EM_EE_FLOAT_OK;
    u32 v4[4], v5[4] = {0, 0, 0, 0}, v6[4] = {0, 0, 0, 0}, q = 0;
    memcpy(v4, row, sizeof v4);
    vu(&st, EM_VU_MUL, DXYZ, NO_BC, v4, v4, 0, NULL, v5);
    vu(&st, EM_VU_ADDBC, DX, 1, v5, v5, 0, NULL, v5);
    vu(&st, EM_VU_ADDBC, DX, 2, v5, v5, 0, NULL, v5);
    if (st != EM_EE_FLOAT_OK) return st;
    q = em_vu_sqrt_bits(v5[0]);
    vu(&st, EM_VU_ADDQ, DX, NO_BC, VF0, NULL, q, NULL, v5);
    if (st != EM_EE_FLOAT_OK) return st;
    st = em_vu_div_bits(VF0[3], v5[0], 3, 0, &q);
    vu(&st, EM_VU_SUB, DXYZW, NO_BC, VF0, VF0, 0, NULL, v6);
    vu(&st, EM_VU_MULQ, DXYZ, NO_BC, v4, NULL, q, NULL, v6);
    if (st != EM_EE_FLOAT_OK) return st;
    memcpy(out, v6, sizeof v6);
    return st;
}

static void load16(u32 out[16], const float in[16]) { memcpy(out, in, 16 * sizeof(u32)); }

/* ======================================================================
 * 001C9E40: rotation matrix -> quaternion
 * ==================================================================== */

/* After the square root s = sqrtf(1 + d) of the selected branch: the
 * branch's own component is 0.5 * s (0x001C9E9C and the like) and the other
 * three are (0.5 / s) times a sum or difference of two matrix words
 * (0x001C9EA0 and the like). Each word is read where the original reads it,
 * after the call. */
static u32 scaled(u32 half_over_s, u32 diff) { return em_ee_mul_bits(half_over_s, diff); }

int em_anim_rest_001C9E40(EmAnimRest *r, uint32_t q[4], const uint32_t m[16])
{
    if (!r) return -1;
    if (latched(r)) return -1;
    NEED(r, q, 0x001C9E40u);
    NEED(r, m, 0x001C9E40u);
    NEED(r, r->workers.w_0011E748, 0x0011E748u);

    const u32 m00 = m[0], m11 = m[5], m22 = m[10];             /* 0x001C9E54 / 58 / 60 */
    u32 trace = em_ee_add_bits(m00, m11);                       /* 0x001C9E68 */
    trace = em_ee_add_bits(m22, trace);                         /* 0x001C9E6C */

    if (!em_ee_c_le_bits(trace, 0)) {                           /* 0x001C9E70: trace > +0 */
        u32 s;
        CALL(r, 0x0011E748u, r->workers.w_0011E748(r->workers.sqrt_ctx, em_ee_add_bits(F_ONE, trace), &s));
        u32 k = em_ee_div_bits(F_HALF, s);                      /* 0x001C9EA0 */
        q[3] = em_ee_mul_bits(F_HALF, s);                       /* 0x001C9E9C, stored 0x001C9EA4 */
        q[0] = scaled(k, em_ee_sub_bits(m[9], m[6]));           /* 0x001C9EB0..0x001C9EB8 */
        q[1] = scaled(k, em_ee_sub_bits(m[2], m[8]));           /* 0x001C9EC4..0x001C9ECC */
        q[2] = scaled(k, em_ee_sub_bits(m[4], m[1]));           /* 0x001C9ED8..0x001C9EE4 */
        return 0;
    }

    /* The largest diagonal word picks the branch: 0 unless m11 > m00
     * (0x001C9EE8), then 2 when m22 > that one (0x001C9F00). */
    int i = 0;
    u32 big = m00;
    if (!em_ee_c_le_bits(m11, m00)) { i = 1; big = m11; }
    if (!em_ee_c_le_bits(m22, big)) i = 2;

    u32 d, s, k;
    switch (i) {
    case 0:                                                     /* 0x001C9F40 */
        d = em_ee_sub_bits(em_ee_sub_bits(m00, m11), m22);      /* 0x001C9F30 / 0x001C9F44 */
        CALL(r, 0x0011E748u, r->workers.w_0011E748(r->workers.sqrt_ctx, em_ee_add_bits(F_ONE, d), &s));
        k = em_ee_div_bits(F_HALF, s);
        q[0] = em_ee_mul_bits(F_HALF, s);                       /* 0x001C9F68 */
        q[1] = scaled(k, em_ee_add_bits(m[1], m[4]));           /* 0x001C9F74..0x001C9F7C */
        q[2] = scaled(k, em_ee_add_bits(m[8], m[2]));           /* 0x001C9F88..0x001C9F90 */
        q[3] = scaled(k, em_ee_sub_bits(m[9], m[6]));           /* 0x001C9F9C..0x001C9FA8 */
        return 0;
    case 1:                                                     /* 0x001C9FB0 */
        d = em_ee_sub_bits(em_ee_sub_bits(m11, m00), m22);      /* 0x001C9F28 / 0x001C9FB4 */
        CALL(r, 0x0011E748u, r->workers.w_0011E748(r->workers.sqrt_ctx, em_ee_add_bits(F_ONE, d), &s));
        k = em_ee_div_bits(F_HALF, s);
        q[1] = em_ee_mul_bits(F_HALF, s);                       /* 0x001C9FD8 */
        q[2] = scaled(k, em_ee_add_bits(m[6], m[9]));           /* 0x001C9FE4..0x001C9FEC */
        q[0] = scaled(k, em_ee_add_bits(m[1], m[4]));           /* 0x001C9FF8..0x001CA000 */
        q[3] = scaled(k, em_ee_sub_bits(m[2], m[8]));           /* 0x001CA00C..0x001CA018 */
        return 0;
    default:                                                    /* 0x001CA020 */
        d = em_ee_sub_bits(em_ee_sub_bits(m22, m00), m11);      /* 0x001C9F1C / 0x001CA024 */
        CALL(r, 0x0011E748u, r->workers.w_0011E748(r->workers.sqrt_ctx, em_ee_add_bits(F_ONE, d), &s));
        k = em_ee_div_bits(F_HALF, s);
        q[2] = em_ee_mul_bits(F_HALF, s);                       /* 0x001CA048 */
        q[0] = scaled(k, em_ee_add_bits(m[8], m[2]));           /* 0x001CA054..0x001CA05C */
        q[1] = scaled(k, em_ee_add_bits(m[6], m[9]));           /* 0x001CA068..0x001CA070 */
        q[3] = scaled(k, em_ee_sub_bits(m[4], m[1]));           /* 0x001CA07C..0x001CA084 */
        return 0;
    }
}

/* ======================================================================
 * 001C9D50: two-pose blend
 * ==================================================================== */

int em_anim_rest_001C9D50(EmAnimRest *r, uint32_t out[16], const uint32_t a[16],
                          const uint32_t b[16], uint32_t blend)
{
    if (!r) return -1;
    if (latched(r)) return -1;
    NEED(r, out, 0x001C9D50u);
    NEED(r, a, 0x001C9D50u);
    NEED(r, b, 0x001C9D50u);
    NEED(r, r->world.spad34C0, 0x700034C0u);
    NEED(r, r->world.spad34D0, 0x700034D0u);
    NEED(r, r->world.spad34E0, 0x700034E0u);
    NEED(r, r->world.spad3760, 0x70003760u);
    NEED(r, r->workers.w_0011E748, 0x0011E748u);

    const u32 w = em_ee_sub_bits(F_ONE, blend);                 /* 0x001C9D74: 1 - blend */
    if (em_anim_rest_001C9E40(r, r->world.spad34C0, a) < 0) return -1;   /* 0x001C9D8C */
    if (em_anim_rest_001C9E40(r, r->world.spad34D0, b) < 0) return -1;   /* 0x001C9D9C */
    em_pose_host_001CA0A0(r->world.spad34E0, r->world.spad34C0, r->world.spad34D0, blend); /* 0x001C9DBC */
    /* quat_to_mat3(out, 0x700034E0, 0x700034C0): the translation row it
     * copies is the first quaternion's x, y, z (0x001C9DD4). */
    em_pose_host_001CA1C0(out, r->world.spad34E0, r->world.spad34C0, r->world.spad3760);
    /* Row 3 xyz = a*w + b*blend through the COP1 accumulator, word by word,
     * each word of a and b read after out was written (0x001C9DDC..0x001C9E14). */
    for (int k = 12; k < 15; ++k) {
        u32 acc = em_ee_mula_bits(a[k], w);
        out[k] = em_ee_madd_bits(acc, b[k], blend);
    }
    return 0;
}

/* ======================================================================
 * 001C7900: single-matrix upload
 * ==================================================================== */

int em_anim_rest_001C7900(EmAnimRest *r, const uint32_t m[16], uint32_t token, int32_t vuaddr,
                          int32_t chan, uint8_t **first)
{
    if (!r) return -1;
    if (latched(r)) return -1;
    NEED(r, m, 0x001C7900u);
    NEED(r, r->world.scratch, 0x70003400u);
    NEED(r, r->workers.w_001D88B0, 0x001D88B0u);
    /* The channel and its room are checked before the call (the cursor is
     * read first, 0x001C793C) and again after it, before the first store,
     * in case the worker appended to the same channel. */
    EmOwnerServicesChannel *ch = channel(r, chan, EM_ANIM_REST_7900_BYTES, 0x001C7900u);
    if (!ch) return -1;
    EmOwnerServicesScratch *spr = r->world.scratch;
    uint8_t *start = ch->cursor;                                /* 0x001C793C: the return value */

    /* 001D88B0(m + 0x30, 0x70003400, 0x70003440, token) (0x001C7948). */
    CALL(r, 0x001D88B0u, r->workers.w_001D88B0(r->workers.ctx, m + 12, spr->s3400, spr->s3440, token));

    if (!channel(r, chan, EM_ANIM_REST_7900_BYTES, 0x001C7900u)) return -1;

    /* Packet 0 (0x001C797C..0x001C79E0): tag qwc 5; the cursor moves 0x60;
     * quadword +0x10 = 0, then +0x14 FLUSH, +0x18 STCYCL 1/1, +0x1C UNPACK
     * V4-32 x4 to vuaddr; B's four rows follow the header. */
    uint8_t *p = ch->cursor;
    put_tag(p, 5);
    ch->cursor = p + 0x60;
    memset(p + 0x10, 0, 16);
    put32(p + 0x14, 0x11000000u);
    put32(p + 0x18, 0x01000101u);
    put32(p + 0x1C, 0x6C040000u | (u32)vuaddr);
    u32 rows[16];
    load16(rows, spr->s3440);
    put_words(p + 0x20, rows, 16);

    /* Packet 1 (0x001C79E4..0x001C7BD4): tag qwc 9; the cursor moves 0xA0;
     * quadword +0x10 = 0, +0x18 STCYCL 1/1, +0x1C UNPACK V4-32 x8 to VU
     * address 0. */
    p = ch->cursor;
    put_tag(p, 9);
    ch->cursor = p + 0xA0;
    memset(p + 0x10, 0, 16);
    put32(p + 0x18, 0x01000101u);
    put32(p + 0x1C, 0x6C080000u);

    /* Rows +0x20..+0x5F: m row i x the view-projection (0x70003AC0, read
     * once before the rows), each m row read just before its result row is
     * stored (0x001C7A24..0x001C7A90). */
    int st = EM_EE_FLOAT_OK;
    u32 vp[16];
    load16(vp, spr->s3AC0);
    for (int i = 0; i < 4; ++i) {
        u32 v[4], o[4] = {0, 0, 0, 0};
        memcpy(v, m + 4 * i, sizeof v);
        vu_row(&st, o, vp, v);
        FLOAT(r, 0x001C7A38u, st);
        put_words(p + 0x20 + 0x10 * i, o, 4);
    }

    /* 0x70003480 / 90 / A0 = m rows 0..2 normalised (w = 0), 0x700034B0 =
     * m row 3 raw (0x001C7A9C..0x001C7B54). */
    for (int i = 0; i < 3; ++i) {
        u32 in[4], o[4];
        memcpy(in, m + 4 * i, sizeof in);
        FLOAT(r, 0x001C7AA0u, normalise_row(o, in));
        memcpy(spr->s3480 + 4 * i, o, sizeof o);
    }
    memcpy(spr->s3480 + 12, m + 12, 16);

    /* Rows +0x60..+0x9F: those four rows x A (0x70003400, read once), each
     * source row read from the scratchpad just before its result is stored
     * (0x001C7B68..0x001C7BD4). */
    u32 a[16];
    load16(a, spr->s3400);
    for (int i = 0; i < 4; ++i) {
        u32 v[4], o[4] = {0, 0, 0, 0};
        memcpy(v, spr->s3480 + 4 * i, sizeof v);
        vu_row(&st, o, a, v);
        FLOAT(r, 0x001C7B7Cu, st);
        put_words(p + 0x60 + 0x10 * i, o, 4);
    }

    if (first) *first = start;
    return 0;
}

/* ======================================================================
 * 001CB2C0: attachment colour packet
 * ==================================================================== */

int em_anim_rest_001CB2C0(EmAnimRest *r, uint32_t owner, int32_t vuaddr, int32_t chan)
{
    if (!r) return -1;
    if (latched(r)) return -1;
    const uint8_t *o = map(r, owner + 0x90u, 4);
    if (!o) return fault(r, 0x001CB2D8u, EM_ANIM_REST_FAULT_BAD_INDEX);
    const u32 base = rd32(o);                                   /* 0x001CB2D8: *(owner + 0x90) */
    /* 00102948 copies a quadword: its load ignores the low four address
     * bits, as the original's does. */
    const uint8_t *q40 = map(r, (base + 0x40u) & ~15u, 16);
    const uint8_t *q50 = map(r, (base + 0x50u) & ~15u, 16);
    if (!q40 || !q50) return fault(r, 0x00102948u, EM_ANIM_REST_FAULT_BAD_INDEX);
    EmOwnerServicesChannel *ch = channel(r, chan, EM_ANIM_REST_B2C0_BYTES, 0x001CB2C0u);
    if (!ch) return -1;

    uint8_t *p = ch->cursor;                                    /* 0x001CB2E8 */
    put_tag(p, 3);                                              /* 0x001CB300..0x001CB310 */
    ch->cursor = p + 0x40;                                      /* 0x001CB31C */
    put32(p + 0x10, 0);                                         /* 0x001CB324 */
    put32(p + 0x14, 0);
    put32(p + 0x18, 0x01000404u);                               /* STCYCL 4/4 */
    put32(p + 0x1C, 0x6C020000u | (u32)vuaddr);                 /* UNPACK V4-32 x2 to vuaddr */
    memcpy(p + 0x20, q40, 16);                                  /* 00102948(p + 0x20, base + 0x40) */
    memcpy(p + 0x30, q50, 16);                                  /* 00102948(p + 0x30, base + 0x50) */
    return 0;
}

/* ======================================================================
 * 001CAAC0: depth key of a world point
 * ==================================================================== */

int em_anim_rest_001CAAC0(EmAnimRest *r, uint32_t position, uint32_t payload, int32_t *key)
{
    if (!r) return -1;
    if (latched(r)) return -1;
    NEED(r, r->world.scratch, 0x70003AC0u);
    NEED(r, r->workers.w_001CB760, 0x001CB760u);
    /* 00102948(stack, position): the quadword at position & ~15 (0x001CAAD8). */
    const uint8_t *src = map(r, position & ~15u, 16);
    if (!src) return fault(r, 0x00102948u, EM_ANIM_REST_FAULT_BAD_INDEX);
    u32 p[4];
    for (int k = 0; k < 4; ++k) p[k] = rd32(src + 4 * k);

    /* The projection (0x001CAB14..0x001CAB30): c = VP row 0 * p.x + row 1 *
     * p.y + row 2 * p.z + row 3 * 1.0 (the constant register's w, not p.w);
     * Q = 1.0 / c.w (VU divide, form (3,3)); c.xyz *= Q. */
    int st = EM_EE_FLOAT_OK;
    u32 vp[16], acc[4] = {0, 0, 0, 0}, c[4] = {0, 0, 0, 0}, q = 0;
    load16(vp, r->world.scratch->s3AC0);
    vu(&st, EM_VU_MULABC, DXYZW, 0, vp, p, 0, NULL, acc);
    vu(&st, EM_VU_MADDABC, DXYZW, 1, vp + 4, p, 0, acc, acc);
    vu(&st, EM_VU_MADDABC, DXYZW, 2, vp + 8, p, 0, acc, acc);
    vu(&st, EM_VU_MADDBC, DXYZW, 3, vp + 12, VF0, 0, acc, c);
    FLOAT(r, 0x001CAB14u, st);
    FLOAT(r, 0x001CAB24u, em_vu_div_bits(VF0[3], c[3], 3, 3, &q));
    vu(&st, EM_VU_MULQ, DXYZ, NO_BC, c, NULL, q, NULL, c);
    FLOAT(r, 0x001CAB30u, st);
    /* The w lane then goes through the fog clamp (0x001CAB34..0x001CAB40)
     * and the four lanes are converted to 12.4 fixed point (0x001CAB44) and
     * stored to the stack; only the z word is read back (0x001CAB4C). The
     * other lanes are never read, so only z is converted here. */
    int32_t k = em_ee_word_int(em_vu_ftoi4_bits(c[2]));
    if (k < 0) k = 0xFFB000;                                    /* 0x001CAB50: the last bucket */
    if (k < 0x1000) k = 0x1000;                                 /* 0x001CAB64: the first bucket */

    CALL(r, 0x001CB760u, r->workers.w_001CB760(r->workers.ctx, EM_ANIM_REST_DEPTH_TABLE, k, payload));
    if (key) *key = k;
    return 0;
}

/* ======================================================================
 * 001CACB0: the indicator draw method
 * ==================================================================== */

int em_anim_rest_001CACB0(EmAnimRest *r, uint32_t owner)
{
    if (!r) return -1;
    if (latched(r)) return -1;
    NEED(r, r->workers.w_001CABA0, 0x001CABA0u);
    const uint8_t *o = map(r, owner + 0x44u, 4);
    if (!o) return fault(r, 0x001CACB4u, EM_ANIM_REST_FAULT_BAD_INDEX);
    /* A tail call: 001CABA0(owner, *(owner + 0x44)) (0x001CACB0 / 0x001CACB4). */
    CALL(r, 0x001CABA0u, r->workers.w_001CABA0(r->workers.ctx, owner, rd32(o)));
    return 0;
}

/* ======================================================================
 * 001CB5B0 anim_bone_array_setup
 * ==================================================================== */

int em_anim_rest_001CB5B0(EmAnimRest *r)
{
    if (!r) return -1;
    if (latched(r)) return -1;
    NEED(r, r->world.d275B48, 0x00275B48u);
    NEED(r, r->world.d275B40, 0x00275B40u);
    *r->world.d275B40 = *r->world.d275B48 + 0x110u;             /* wraps as the 32-bit add does */
    return 0;
}

/* ======================================================================
 * Worker adapters
 * ==================================================================== */

int em_anim_rest_sqrt_0011E748(void *sdk_math_context, uint32_t x, uint32_t *result)
{
    EmSdkMathContext *c = sdk_math_context;
    if (!c || !result) return -1;
    float out = 0.0f;
    uint32_t where = 0;
    if (em_sdk_math_original_0011E748(c->tables, &c->world, &c->workers, em_ee_float(x), &out, &where) < 0) {
        if (c->fault == 0) c->fault = where ? where : 0x0011E748u;
        return -1;
    }
    *result = em_ee_bits(out);
    return 0;
}
