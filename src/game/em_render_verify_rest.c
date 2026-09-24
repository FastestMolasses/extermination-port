/* em_render_verify_rest.c - see em_render_verify_rest.h and
 * docs/RENDER_VERIFY_REST.md.
 *
 * Read from the original instructions: the byte-matched decomp C for
 * 001C1DC0, 001C1E70, 001C1E80, 001C1E90, 001C22A0, 001C2360, 001E2260,
 * 001E2270, 001E2280, 001E0CF0, 001FCF10, 001000E0, 001D4B50 and 001DA1E0;
 * the split listing for 001DA290 (asm-linked: its decomp C's first
 * argument to 001D1F80 is not what executes),
 * the NEARMISS 001C1F50, the asm-word 001027E0 and the inline-asm
 * 00102850. Every original address a branch, call or store
 * comes from is cited beside it. */
#include "game/em_render_verify_rest.h"
#include "game/em_ee_float.h"
#include "game/em_sdk_soft_float.h"

#include <stddef.h>
#include <string.h>

/* ---- faults ------------------------------------------------------------- */

static int latched(const EmRvrFault *fault)
{
    return fault && fault->code != EM_RVR_FAULT_NONE;
}

static int fail(EmRvrFault *fault, uint32_t address, int32_t code)
{
    if (fault && fault->code == EM_RVR_FAULT_NONE) {
        fault->address = address;
        fault->code = code;
    }
    return -1;
}

/* A worker call: a negative result latches WORKER_FAILED at `address`. */
static int worker(EmRvrFault *fault, uint32_t address, int result)
{
    return result < 0 ? fail(fault, address, EM_RVR_FAULT_WORKER_FAILED) : 0;
}

#define CALL(address, expr) do { if (worker(fault, (address), (expr))) return -1; } while (0)
#define NEED(ptr, address) do { if (!(ptr)) return fail(fault, (address), EM_RVR_FAULT_NULL_WORKER); } while (0)

/* ---- little-endian record access ----------------------------------------- */

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int rc_span(const EmRvrRenderContext *rc, uint32_t at, uint32_t n)
{
    return rc && rc->bytes && at <= rc->size && n <= rc->size - at;
}

/* ======================================================================
 * L31
 * ====================================================================== */

static uint32_t area_key(const uint8_t d810700[2])
{
    return ((uint32_t)d810700[0] << 8) + d810700[1];   /* (D_00810700 << 8) + D_00810701 */
}

/* 001C1F50. Two independent chains over the same key, then the colour,
 * then two single-key tails; the key is re-read from D_00810700/701 before
 * each chain (0x1C1F58, 0x1C2054, 0x1C21F8, 0x1C2244). */
int em_rvr_001C1F50(const uint8_t d810700[2], const EmRvrAreaWorkers *w, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    if (!d810700) return fail(fault, 0x001C1F50u, EM_RVR_FAULT_BAD_INPUT);
    NEED(w, 0x001C1F50u);
    NEED(w->w_001D2830, 0x001D2830u);
    NEED(w->w_001E2260, 0x001E2260u);
    NEED(w->w_001E2270, 0x001E2270u);
    NEED(w->w_001E2280, 0x001E2280u);

    /* Chain 1: the render flags (calls at 0x1C1FE8..0x1C204C). */
    uint32_t key = area_key(d810700);
    switch (key) {
    case 0x1500:
        CALL(0x001D2830u, w->w_001D2830(w->ctx, 0x20, 1));
        CALL(0x001D2830u, w->w_001D2830(w->ctx, 0x21, 1));
        CALL(0x001D2830u, w->w_001D2830(w->ctx, 0x22, 1));
        CALL(0x001D2830u, w->w_001D2830(w->ctx, 0x23, 1));
        break;
    case 0x1200: case 0x1100: case 0x0F01: case 0x0F00:
    case 0x0E00: case 0x0D00: case 0x0C00: case 0x0B00:
        CALL(0x001D2830u, w->w_001D2830(w->ctx, 0x20, 1));
        CALL(0x001D2830u, w->w_001D2830(w->ctx, 0x21, 1));
        CALL(0x001D2830u, w->w_001D2830(w->ctx, 0x22, 0));
        break;
    default:
        CALL(0x001D2830u, w->w_001D2830(w->ctx, 0x20, 0));
        break;
    }

    /* Chain 2: the background TEX0 (001E2260 calls at 0x1C20E8..0x1C21E4).
     * Key 0x0C00 arms the flags above but has no tag here. */
    key = area_key(d810700);
    static const struct { uint16_t key; uint64_t tag; } tex0[] = {
        {0x0B00, UINT64_C(0x20069F0121323200)},
        {0x0D00, UINT64_C(0x2006D50121323240)},
        {0x0E00, UINT64_C(0x2006EA8121323440)},
        {0x0F00, UINT64_C(0x2006590121322F82)},
        {0x0F01, UINT64_C(0x2006B48121323280)},
        {0x1100, UINT64_C(0x20066001213230C0)},
        {0x1200, UINT64_C(0x2006CC8121323300)},
        {0x1500, UINT64_C(0x20076A8121323700)},
    };
    for (size_t i = 0; i < sizeof tex0 / sizeof tex0[0]; ++i)
        if (key == tex0[i].key) {
            CALL(0x001E2260u, w->w_001E2260(w->ctx, tex0[i].tag));
            break;
        }

    /* 0x1C21F0: 001E2270(&D_00250F30), every area. */
    CALL(0x001E2270u, w->w_001E2270(w->ctx, EM_RVR_COLOUR_00250F30));

    /* 0x1C223C: 001E2280 for area 0x1500 only. */
    key = area_key(d810700);
    if (key == 0x1500)
        CALL(0x001E2280u, w->w_001E2280(w->ctx, UINT64_C(0x20042B05DD321D00)));

    /* 0x1C2280: flag 0x25 for areas 0x1200 and 0x0F00. */
    key = area_key(d810700);
    if (key == 0x1200 || key == 0x0F00)
        CALL(0x001D2830u, w->w_001D2830(w->ctx, 0x25, 1));
    return 0;
}

/* 001C1E70: a jump to 001D52E0 (which takes no argument). */
int em_rvr_001C1E70(const EmRvrAreaWorkers *w, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    NEED(w, 0x001C1E70u);
    NEED(w->w_001D52E0, 0x001D52E0u);
    CALL(0x001D52E0u, w->w_001D52E0(w->ctx));
    return 0;
}

/* 001C1E80: a jump to 001D8FD0 (which takes no argument). */
int em_rvr_001C1E80(const EmRvrAreaWorkers *w, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    NEED(w, 0x001C1E80u);
    NEED(w->w_001D8FD0, 0x001D8FD0u);
    CALL(0x001D8FD0u, w->w_001D8FD0(w->ctx));
    return 0;
}

/* 001C1E90: returns; no reads, no writes. */
int em_rvr_001C1E90(EmRvrFault *fault)
{
    return latched(fault) ? -1 : 0;
}

/* 001C1DC0. Every worker the routine can reach is checked before the first
 * call (the eight registrations have no undo). */
int em_rvr_001C1DC0(const uint8_t d810700[2], const EmRvrAreaWorkers *w, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    if (!d810700) return fail(fault, 0x001C1DC0u, EM_RVR_FAULT_BAD_INPUT);
    NEED(w, 0x001C1DC0u);
    NEED(w->w_001D2830, 0x001D2830u);
    NEED(w->w_001D52E0, 0x001D52E0u);
    NEED(w->w_001D8FD0, 0x001D8FD0u);
    NEED(w->w_001C1EA0, 0x001C1EA0u);
    NEED(w->w_001E2260, 0x001E2260u);
    NEED(w->w_001E2270, 0x001E2270u);
    NEED(w->w_001E2280, 0x001E2280u);
    static const int32_t reg[8][2] = {
        {0x00, 1}, {0x01, 1}, {0x02, 0}, {0x24, 0}, {0x20, 0}, {0x21, 0}, {0x22, 0}, {0x25, 0},
    };
    for (int i = 0; i < 8; ++i)
        CALL(0x001D2830u, w->w_001D2830(w->ctx, reg[i][0], reg[i][1]));
    if (em_rvr_001C1E70(w, fault) < 0) return -1;
    if (em_rvr_001C1E80(w, fault) < 0) return -1;
    if (em_rvr_001C1E90(fault) < 0) return -1;
    CALL(0x001C1EA0u, w->w_001C1EA0(w->ctx, EM_RVR_BLOCK_008101D0));
    return em_rvr_001C1F50(d810700, w, fault);
}

/* 001E2260: the doubleword at ctx+0x1D0. */
int em_rvr_001E2260(EmRvrRenderContext *rc, uint64_t tag, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    if (!rc_span(rc, 0x1D0, 8)) return fail(fault, 0x001E2260u, EM_RVR_FAULT_BAD_INPUT);
    put64(rc->bytes + 0x1D0, tag);
    return 0;
}

/* 001E2270: the quadword at ctx+0x1C0 (one 128-bit load and store). */
int em_rvr_001E2270(EmRvrRenderContext *rc, const uint8_t src[16], EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    if (!src || !rc_span(rc, 0x1C0, 16)) return fail(fault, 0x001E2270u, EM_RVR_FAULT_BAD_INPUT);
    uint8_t q[16];
    memcpy(q, src, 16);
    memcpy(rc->bytes + 0x1C0, q, 16);
    return 0;
}

/* 001E2280: the doubleword at ctx+0x1E0. */
int em_rvr_001E2280(EmRvrRenderContext *rc, uint64_t tag, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    if (!rc_span(rc, 0x1E0, 8)) return fail(fault, 0x001E2280u, EM_RVR_FAULT_BAD_INPUT);
    put64(rc->bytes + 0x1E0, tag);
    return 0;
}

/* 001E0CF0. The results of 001E1E60 / 001E1AD0 are stored as words. */
int em_rvr_001E0CF0(EmRvrRenderContext *rc, const EmRvrBackgroundWorkers *w, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    if (!rc_span(rc, 0x1D8, 4) || !rc_span(rc, 0x1E8, 4))
        return fail(fault, 0x001E0CF0u, EM_RVR_FAULT_BAD_INPUT);
    NEED(w, 0x001E0CF0u);
    NEED(w->w_001E0CC0, 0x001E0CC0u);
    NEED(w->w_001D2910, 0x001D2910u);
    NEED(w->w_001E1E60, 0x001E1E60u);
    NEED(w->w_001E1AD0, 0x001E1AD0u);
    CALL(0x001E0CC0u, w->w_001E0CC0(w->ctx));
    int32_t on = 0;
    CALL(0x001D2910u, w->w_001D2910(w->ctx, 0x20, &on));
    if (on == 0) return 0;
    CALL(0x001D2910u, w->w_001D2910(w->ctx, 0x21, &on));
    if (on != 0) {
        uint32_t list = 0;
        CALL(0x001E1E60u, w->w_001E1E60(w->ctx, 0x180, 3, &list));
        put32(rc->bytes + 0x1D8, list);
    }
    CALL(0x001D2910u, w->w_001D2910(w->ctx, 0x22, &on));
    if (on != 0) {
        uint32_t list = 0;
        CALL(0x001E1AD0u, w->w_001E1AD0(w->ctx, 0x1E0, 3, &list));
        put32(rc->bytes + 0x1E8, list);
    }
    return 0;
}

/* 001C22A0 / 001C2360 (identical but for the table word). The comments give
 * instruction offsets from the entry (001C22A0 + off = 001C2360 + off). */
static int model_bind(uint32_t fn, uint8_t *self, uint32_t size, uint32_t self_address,
                      uint32_t table_word, int16_t d275BCC, const EmRvrModelWorkers *w,
                      int32_t *result, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    if (!self || !result || size < 0x110) return fail(fault, fn, EM_RVR_FAULT_BAD_INPUT);
    NEED(w, fn);
    NEED(w->w_001C6120, 0x001C6120u);
    NEED(w->w_001CA5E0, 0x001CA5E0u);
    NEED(w->w_001C6150, 0x001C6150u);
    NEED(w->w_001AF780, 0x001AF780u);
    NEED(w->w_001CB5B0, 0x001CB5B0u);
    NEED(w->w_001C62C0, 0x001C62C0u);

    uint32_t model = 0, count = 0;
    CALL(0x001C6120u, w->w_001C6120(w->ctx, table_word, self[0xD], &model));        /* +0x20 */
    CALL(0x001CA5E0u, w->w_001CA5E0(w->ctx, self, self_address, model, 2));          /* +0x30 */
    CALL(0x001C6150u, w->w_001C6150(w->ctx, get32(self + 0x44), &count));            /* +0x38 */
    self[0xC] = (uint8_t)count;                                                      /* +0x40 */
    if ((int32_t)d275BCC < (int32_t)self[0xC]) {                                     /* +0x4C */
        *result = 1;
        return 0;
    }
    for (uint32_t i = 0; i < self[0xC]; ++i) {                                       /* +0x80 */
        uint32_t bone = 0;
        if (0x110u + 4u * i + 4u > size) return fail(fault, fn + 0x74u, EM_RVR_FAULT_BAD_INPUT);
        CALL(0x001AF780u, w->w_001AF780(w->ctx, &bone));                             /* +0x6C */
        put32(self + 0x110 + 4 * i, bone);                                           /* +0x74 */
    }
    self[9] = self[0xC];                                                             /* +0x90 */
    CALL(0x001CB5B0u, w->w_001CB5B0(w->ctx, self[0xC]));                             /* +0x94 */
    CALL(0x001C62C0u, w->w_001C62C0(w->ctx, self, self_address));                    /* +0x9C */
    *result = 0;
    return 0;
}

int em_rvr_001C22A0(uint8_t *self, uint32_t size, uint32_t self_address, uint32_t table_word,
                    int16_t d275BCC, const EmRvrModelWorkers *w, int32_t *result, EmRvrFault *fault)
{
    return model_bind(0x001C22A0u, self, size, self_address, table_word, d275BCC, w, result, fault);
}

int em_rvr_001C2360(uint8_t *self, uint32_t size, uint32_t self_address, uint32_t table_word,
                    int16_t d275BCC, const EmRvrModelWorkers *w, int32_t *result, EmRvrFault *fault)
{
    return model_bind(0x001C2360u, self, size, self_address, table_word, d275BCC, w, result, fault);
}

/* ======================================================================
 * L37
 * ====================================================================== */

#define VU(address, expr) do { if ((expr) != EM_EE_FLOAT_OK) \
    return fail(fault, (address), EM_RVR_FAULT_UNMEASURED); } while (0)

/* 001027E0. The three rows are loaded as integers and transposed with the
 * MMI word interleaves (0x102800..0x102818; every output row gets w = a
 * zero lane of the cleared translation copy); row 3 is loaded into VU0.
 * Then (0x102828..0x102834): ACC = row0' * t.x, ACC += row1' * t.y,
 * vf4.xyz = ACC + row2' * t.z, vf4.xyz = 0 - vf4.xyz, vf4.w = in[15]. All
 * loads happen before the first store, so out may alias in. */
int em_rvr_001027E0(uint32_t out[16], const uint32_t in[16], EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    if (!out || !in) return fail(fault, 0x001027E0u, EM_RVR_FAULT_BAD_INPUT);
    uint32_t m[16];
    memcpy(m, in, sizeof m);
    uint32_t t[4], vf4[4], vf9[4], r0[4], r1[4], r2[4], acc[4] = {0, 0, 0, 0};
    memcpy(t, m + 12, sizeof t);                                  /* vf5 = row 3 (vmove) */
    memcpy(vf4, m + 12, sizeof vf4);
    VU(0x001027F4u, em_vu_vec_bits(EM_VU_SUB, 14, EM_VU_NO_BC, vf4, vf4, 0, NULL, vf4));
    memcpy(vf9, vf4, sizeof vf9);                                 /* vf9 = (0, 0, 0, w) */
    for (int i = 0; i < 3; ++i) {                                 /* the MMI transpose */
        r0[i] = m[4 * i + 0];
        r1[i] = m[4 * i + 1];
        r2[i] = m[4 * i + 2];
    }
    r0[3] = vf9[0];
    r1[3] = vf9[1];
    r2[3] = vf9[2];
    VU(0x00102828u, em_vu_vec_bits(EM_VU_MULABC, 14, 0, r0, t, 0, NULL, acc));
    VU(0x0010282Cu, em_vu_vec_bits(EM_VU_MADDABC, 14, 1, r1, t, 0, acc, acc));
    VU(0x00102830u, em_vu_vec_bits(EM_VU_MADDBC, 14, 2, r2, t, 0, acc, vf4));
    VU(0x00102834u, em_vu_vec_bits(EM_VU_SUB, 14, EM_VU_NO_BC, vf9, vf4, 0, NULL, vf4));
    memcpy(out + 0, r0, 16);
    memcpy(out + 4, r1, 16);
    memcpy(out + 8, r2, 16);
    memcpy(out + 12, vf4, 16);
    return 0;
}

/* 00102850. vf5.x = s (moved from f12), Q = vf0.w / vf5.x, vf4 = v * Q
 * (xyzw), stored to out. */
int em_rvr_00102850(uint32_t out[4], const uint32_t v[4], uint32_t s, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    if (!out || !v) return fail(fault, 0x00102850u, EM_RVR_FAULT_BAD_INPUT);
    uint32_t q = 0, vf4[4];
    memcpy(vf4, v, sizeof vf4);
    VU(0x0010285Cu, em_vu_div_bits(EM_EE_ONE, s, 3, 0, &q));
    VU(0x00102864u, em_vu_vec_bits(EM_VU_MULQ, 15, EM_VU_NO_BC, vf4, NULL, q, NULL, vf4));
    memcpy(out, vf4, sizeof vf4);
    return 0;
}

/* 001000E0: the compare's v0 <= 0 (slti 1). */
int32_t em_rvr_001000E0(uint64_t a, uint64_t b)
{
    return em_sdk_soft_float_001274B0(a, b) <= 0;
}

/* ======================================================================
 * L20
 * ====================================================================== */

int em_rvr_001FCF10(const EmRvrMessageWorkers *w, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    NEED(w, 0x001FCF10u);
    NEED(w->w_001FCB90, 0x001FCB90u);
    CALL(0x001FCB90u, w->w_001FCB90(w->ctx, 0x10E, 0xCC, 5, 0));
    return 0;
}

/* ======================================================================
 * L29b
 * ====================================================================== */

int em_rvr_001D4B50(const EmRvrClipWorkers *w, uint32_t obj, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    NEED(w, 0x001D4B50u);
    NEED(w->w_001D49D0, 0x001D49D0u);
    NEED(w->w_001D4B10, 0x001D4B10u);
    CALL(0x001D49D0u, w->w_001D49D0(w->ctx, obj));
    CALL(0x001D4B10u, w->w_001D4B10(w->ctx, obj));
    return 0;
}

/* 001DA1E0. The cursor word is ctx + 0x10 + 4 * channel (the routine
 * indexes the render context as a word array). The record is checked to lie
 * inside `mem` before any byte is written. The payload is read one qword
 * at a time after the header stores, as the original interleaves it. */
int em_rvr_001DA1E0(EmRvrRenderContext *rc, EmRvrMemory *mem, int32_t channel,
                    const uint8_t payload[64], uint32_t word, uint32_t *body, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    if (!payload || !body || !mem || !mem->bytes || channel < 0 || channel > 0x3FFFFFFB)
        return fail(fault, 0x001DA1E0u, EM_RVR_FAULT_BAD_INPUT);
    uint32_t slot = 0x10u + 4u * (uint32_t)channel;
    if (!rc_span(rc, slot, 4)) return fail(fault, 0x001DA1E0u, EM_RVR_FAULT_BAD_INPUT);
    uint32_t record = get32(rc->bytes + slot);
    if (record < mem->base || record - mem->base > mem->size || mem->size - (record - mem->base) < 0x80)
        return fail(fault, 0x001DA1E0u, EM_RVR_FAULT_BAD_INPUT);
    uint8_t *p = mem->bytes + (record - mem->base);
    p[3] = 0x10;                                                   /* 0x1DA214 */
    put32(p + 4, 0);                                               /* 0x1DA224 */
    put16(p + 0, 7);                                               /* 0x1DA22C */
    put32(rc->bytes + slot, record + 0x80);                        /* 0x1DA238 */
    memset(p + 0x10, 0, 16);                                       /* 0x1DA23C */
    put32(p + 0x1C, 0x50000006u);                                  /* 0x1DA240 */
    put64(p + 0x20, UINT64_C(0x5022400000008001));                 /* 0x1DA244 */
    put64(p + 0x28, UINT64_C(0x44441));                            /* 0x1DA248 */
    put32(p + 0x30, 0);                                            /* 0x1DA24C */
    put32(p + 0x34, 0);
    put32(p + 0x38, 0);
    put32(p + 0x3C, word);                                         /* 0x1DA258 */
    for (int k = 0; k < 4; ++k) {                                  /* 0x1DA25C..0x1DA280: */
        uint8_t q[16];                                             /* each qword read, then */
        memcpy(q, payload + 16 * k, 16);                           /* stored, in order */
        memcpy(p + 0x40 + 16 * k, q, 16);
    }
    *body = record + 0x10;
    return 0;
}

int em_rvr_001DA290(EmRvrRenderContext *rc, EmRvrMemory *mem, const EmRvrStateWorkers *w,
                    const uint8_t template_2531D0[64], int32_t a0, uint32_t a1, EmRvrFault *fault)
{
    if (latched(fault)) return -1;
    if (!template_2531D0) return fail(fault, 0x001DA290u, EM_RVR_FAULT_BAD_INPUT);
    NEED(w, 0x001DA290u);
    NEED(w->w_001D1F80, 0x001D1F80u);
    uint8_t tmp[64];
    memcpy(tmp, template_2531D0, sizeof tmp);                      /* the stack copy */
    /* 0x1DA2D8: a0 is passed through unchanged (the listing sets only a1 = 2
     * and a2 = 9; the decomp C's literal 0 is not what executes). */
    CALL(0x001D1F80u, w->w_001D1F80(w->ctx, a0, 2, 9));
    uint32_t body = 0;                                             /* 0x1DA2E8: */
    return em_rvr_001DA1E0(rc, mem, a0, tmp, a1, &body, fault);
}
