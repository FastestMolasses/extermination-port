/* AREA01 render lane: the memory, fault and VU0 helpers the lane's
 * translations share (em_area01_render_vif.c, em_area01_render_gs.c,
 * em_area01_render_hud.c, em_area01_render_frame.c). Docs:
 * docs/AREA01_RENDER.md.
 *
 * Memory: every original byte a translated routine reads or writes is reached
 * through EmArea01RenderWorld.views by its ORIGINAL address, exactly as the
 * instructions address it (EE RAM, the scratchpad at 0x70000000). An address
 * no view covers latches EM_A01R_FAULT_BAD_ADDRESS. A quadword access (the
 * original's 128-bit GPR and VU loads and stores) ignores the low four address
 * bits, as the hardware does.
 *
 * Faults are fail-stop: the first fault is latched with the original function
 * address, its code and the data address involved; a latched fault makes every
 * later entry return -1 without reading or writing. What the reference test
 * checks of this (and what it does not) is listed in docs/AREA01_RENDER.md
 * section 1: NULL, failing and latched cases at every entry; the recorded
 * address and function for no views, NULL view bytes, two 2^32 wraps, a view
 * ending at 001E8B90's first access and one starting after 001CD2B0's store
 * of n. Other partial-view worlds are not tested.
 *
 * Arithmetic: COP1 and the VU0 macro products go through em_ee_float.h on
 * binary32 bit patterns. The VU clip test is not in the measured model: the
 * rule here is the one em_render_context.c documents (docs/RENDER_CONTEXT.md
 * section 4): DAZ, then magnitude compares against |w|; a lane with exponent
 * 255 faults EM_A01R_FAULT_UNMEASURED.
 *
 * stdint only (plus em_ee_float.h). */
#ifndef EM_AREA01_RENDER_MEM_H
#define EM_AREA01_RENDER_MEM_H

#include <stdint.h>
#include <string.h>

#include "game/em_ee_float.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_A01R_FAULT_NONE = 0,
    EM_A01R_FAULT_NULL_WORKER = 1,   /* a reachable worker is NULL */
    EM_A01R_FAULT_WORKER_FAILED = 2, /* a worker returned a negative value */
    EM_A01R_FAULT_BAD_ADDRESS = 4,   /* an original address no view covers */
    EM_A01R_FAULT_UNMEASURED = 6     /* a VU form or clip lane outside the model */
};

typedef struct {
    uint32_t address; /* original function (or worker) address */
    int32_t code;     /* EM_A01R_FAULT_* */
    uint32_t detail;  /* the data address for BAD_ADDRESS, else 0 */
} EmArea01RenderFault;

/* Host bytes standing for original addresses [address, address + size). */
typedef struct {
    uint32_t address;
    uint32_t size;
    uint8_t *bytes;
} EmArea01RenderView;

/* Authoritative when non-NULL: no array fallback on refusal. The provider
 * sees the original address unchanged and write=0 (load) or 1 (store). */
typedef uint8_t *(*EmArea01RenderMemory)(void *ctx, uint32_t address, uint32_t size, int write);

typedef struct {
    const EmArea01RenderView *views; /* searched in order */
    uint32_t view_count;
    EmArea01RenderMemory view;
    void *view_ctx;
} EmArea01RenderWorld;

/* The part every module state starts with. */
typedef struct {
    EmArea01RenderWorld world;
    EmArea01RenderFault fault;
    uint32_t function; /* the original routine running (fault attribution) */
} EmArea01RenderCore;

/* ------------------------------------------------------------------ */

static inline int em_a01r_latched(const EmArea01RenderCore *c) { return c->fault.code != EM_A01R_FAULT_NONE; }

static inline int em_a01r_fault(EmArea01RenderCore *c, uint32_t address, int32_t code, uint32_t detail)
{
    if (!em_a01r_latched(c)) {
        c->fault.address = address;
        c->fault.code = code;
        c->fault.detail = detail;
    }
    return -1;
}

static inline uint8_t *em_a01r_mem(EmArea01RenderCore *c, uint32_t a, uint32_t n, int write)
{
    if (c->world.view) {
        uint8_t *p = n && (uint64_t)a + n <= UINT64_C(0x100000000)
                         ? c->world.view(c->world.view_ctx, a, n, write) : NULL;
        if (p) return p;
        em_a01r_fault(c, c->function, EM_A01R_FAULT_BAD_ADDRESS, a);
        return NULL;
    }
    for (uint32_t i = 0; c->world.views && i < c->world.view_count; ++i) {
        const EmArea01RenderView *v = &c->world.views[i];
        if (v->bytes && a >= v->address && (uint64_t)a + n <= (uint64_t)v->address + v->size)
            return v->bytes + (a - v->address);
    }
    em_a01r_fault(c, c->function, EM_A01R_FAULT_BAD_ADDRESS, a);
    return NULL;
}

static inline uint32_t em_a01r_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void em_a01r_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline int em_a01r_ld8(EmArea01RenderCore *c, uint32_t a, uint32_t *v)
{
    uint8_t *p = em_a01r_mem(c, a, 1, 0);
    if (!p) return -1;
    *v = p[0];
    return 0;
}

static inline int em_a01r_ld32(EmArea01RenderCore *c, uint32_t a, uint32_t *v)
{
    uint8_t *p = em_a01r_mem(c, a, 4, 0);
    if (!p) return -1;
    *v = em_a01r_get32(p);
    return 0;
}

static inline int em_a01r_st8(EmArea01RenderCore *c, uint32_t a, uint32_t v)
{
    uint8_t *p = em_a01r_mem(c, a, 1, 1);
    if (!p) return -1;
    p[0] = (uint8_t)v;
    return 0;
}

static inline int em_a01r_st16(EmArea01RenderCore *c, uint32_t a, uint32_t v)
{
    uint8_t *p = em_a01r_mem(c, a, 2, 1);
    if (!p) return -1;
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    return 0;
}

static inline int em_a01r_st32(EmArea01RenderCore *c, uint32_t a, uint32_t v)
{
    uint8_t *p = em_a01r_mem(c, a, 4, 1);
    if (!p) return -1;
    em_a01r_put32(p, v);
    return 0;
}

static inline int em_a01r_st64(EmArea01RenderCore *c, uint32_t a, uint64_t v)
{
    uint8_t *p = em_a01r_mem(c, a, 8, 1);
    if (!p) return -1;
    em_a01r_put32(p, (uint32_t)v);
    em_a01r_put32(p + 4, (uint32_t)(v >> 32));
    return 0;
}

/* Quadword load / store: the low four address bits are ignored. */
static inline int em_a01r_ldq(EmArea01RenderCore *c, uint32_t a, uint32_t out[4])
{
    uint8_t *p = em_a01r_mem(c, a & ~15u, 16, 0);
    if (!p) return -1;
    for (int i = 0; i < 4; ++i) out[i] = em_a01r_get32(p + 4 * i);
    return 0;
}

static inline int em_a01r_stq(EmArea01RenderCore *c, uint32_t a, const uint32_t in[4])
{
    uint8_t *p = em_a01r_mem(c, a & ~15u, 16, 1);
    if (!p) return -1;
    for (int i = 0; i < 4; ++i) em_a01r_put32(p + 4 * i, in[i]);
    return 0;
}

/* 00102948(dst, src): one quadword copy (both addresses quadword-aligned by
 * the access). */
static inline int em_a01r_00102948(EmArea01RenderCore *c, uint32_t dst, uint32_t src)
{
    uint32_t q[4];
    if (em_a01r_ldq(c, src, q) < 0) return -1;
    return em_a01r_stq(c, dst, q);
}

/* 00102958(dst, src): four quadwords, all loaded before the first store. */
static inline int em_a01r_00102958(EmArea01RenderCore *c, uint32_t dst, uint32_t src)
{
    uint32_t q[4][4];
    for (int i = 0; i < 4; ++i)
        if (em_a01r_ldq(c, src + 16u * (uint32_t)i, q[i]) < 0) return -1;
    for (int i = 0; i < 4; ++i)
        if (em_a01r_stq(c, dst + 16u * (uint32_t)i, q[i]) < 0) return -1;
    return 0;
}

/* The render-context address D_00275670 and 001CD370(i) = context + (i << 6)
 * + 0x2240 (the clip matrices). */
#define EM_A01R_D_00275670 0x00275670u
#define EM_A01R_D_007635C0 0x007635C0u /* the chain table (001CB5F0 / 001CB760 a0) */
static inline int em_a01r_001CD370(EmArea01RenderCore *c, int32_t i, uint32_t *out)
{
    uint32_t ctx;
    if (em_a01r_ld32(c, EM_A01R_D_00275670, &ctx) < 0) return -1;
    *out = ctx + ((uint32_t)i << 6) + 0x2240u;
    return 0;
}

/* One DMA tag at the channel-3 cursor (context +0x1C), as every routine of
 * the lane writes it: byte +3 = id, word +4 = addr, halfword +0 = qwc, then
 * cursor += 0x10. The cursor word is loaded again before each of the four
 * stores. That is observable only when a tag store lands on the cursor word
 * itself (cursor at context +0x18 or +0x1C); the reference test's
 * tag-alias cases reach both, +0x1C also with the context above 16 MiB.
 * ctx is the context address the caller last loaded. */
#define EM_A01R_CTX_CURSOR3 0x1Cu
static inline int em_a01r_tag3(EmArea01RenderCore *c, uint32_t ctx, uint32_t id, uint32_t addr, uint32_t qwc)
{
    uint32_t cur;
    if (em_a01r_ld32(c, ctx + EM_A01R_CTX_CURSOR3, &cur) < 0 || em_a01r_st8(c, cur + 3u, id) < 0) return -1;
    if (em_a01r_ld32(c, ctx + EM_A01R_CTX_CURSOR3, &cur) < 0 || em_a01r_st32(c, cur + 4u, addr) < 0) return -1;
    if (em_a01r_ld32(c, ctx + EM_A01R_CTX_CURSOR3, &cur) < 0 || em_a01r_st16(c, cur, qwc) < 0) return -1;
    if (em_a01r_ld32(c, ctx + EM_A01R_CTX_CURSOR3, &cur) < 0) return -1;
    return em_a01r_st32(c, ctx + EM_A01R_CTX_CURSOR3, cur + 0x10u);
}

/* ---- VU0 ---------------------------------------------------------- */

static const uint32_t EM_A01R_VF0[4] = {0, 0, 0, EM_EE_ONE};

/* out = m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3] * 1.0, all four lanes,
 * summed left to right in the VU0 accumulator with em_ee_float.h's rounding
 * at every step. The last term takes 1.0 from vf0, so v.w is never read. */
static inline int em_a01r_transform(EmArea01RenderCore *c, const uint32_t m[4][4], const uint32_t v[4],
                                    uint32_t out[4])
{
    uint32_t acc[4] = {0, 0, 0, 0};
    if (em_vu_vec_bits(EM_VU_MULABC, 15, 0, m[0], v, 0, NULL, acc) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MADDABC, 15, 1, m[1], v, 0, acc, acc) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MADDABC, 15, 2, m[2], v, 0, acc, acc) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MADDBC, 15, 3, m[3], EM_A01R_VF0, 0, acc, out) != EM_EE_FLOAT_OK)
        return em_a01r_fault(c, c->function, EM_A01R_FAULT_UNMEASURED, 0);
    return 0;
}

/* The clip test of v.x, v.y, v.z against |v.w| (flag bits +x, -x, +y, -y,
 * +z, -z): DAZ, then magnitude compares; exponent 255 faults. */
static inline int em_a01r_clipw(EmArea01RenderCore *c, const uint32_t v[4], uint32_t *flags)
{
    for (int i = 0; i < 4; ++i)
        if (((v[i] >> 23) & 0xFFu) == 0xFFu) return em_a01r_fault(c, c->function, EM_A01R_FAULT_UNMEASURED, 0);
    const uint32_t w = em_eei_daz(v[3]) & 0x7FFFFFFFu;
    uint32_t f = 0;
    for (int k = 0; k < 3; ++k) {
        const uint32_t x = em_eei_daz(v[k]);
        if ((x & 0x7FFFFFFFu) > w) f |= (x >> 31) ? 2u << (2 * k) : 1u << (2 * k);
    }
    *flags = f;
    return 0;
}

/* The projection shared by 001CD070, 001CAAC0 and 001F4A10, with the matrix
 * k (0x70003AC0 in every caller) and ca0, the quadword at context + 0xA0:
 *   clip   = k * (p.xyz, 1), as em_a01r_transform computes it
 *   Q      = 1 / clip.w, the VU divide
 *   s.xyz  = clip.xyz * Q
 *   s.w    = 1.0 * ca0.z + ca0.w * clip.w (two rounded steps), then the
 *            smaller of that and ca0.x, then the larger of that and +0
 *            (in this order: the orders differ when ca0.x < 0)
 *   screen = each lane of s as signed 12.4 fixed point */
static inline int em_a01r_project(EmArea01RenderCore *c, const uint32_t k[4][4], const uint32_t ca0[4],
                                  const uint32_t p[4], uint32_t clip[4], uint32_t screen[4])
{
    uint32_t v[4], q, acc[4] = {0, 0, 0, 0};
    if (em_a01r_transform(c, k, p, v) < 0) return -1;
    memcpy(clip, v, sizeof v);
    if (em_vu_div_bits(EM_EE_ONE, v[3], 3, 3, &q) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MULQ, 14, EM_VU_NO_BC, v, NULL, q, NULL, v) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MULABC, 1, 2, EM_A01R_VF0, ca0, 0, NULL, acc) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MADDBC, 1, 3, ca0, v, 0, acc, v) != EM_EE_FLOAT_OK)
        return em_a01r_fault(c, c->function, EM_A01R_FAULT_UNMEASURED, 0);
    v[3] = em_vu_min_bits(v[3], ca0[0]);
    v[3] = em_vu_max_bits(v[3], 0);
    for (int i = 0; i < 4; ++i) screen[i] = em_vu_ftoi4_bits(v[i]);
    return 0;
}

static inline int em_a01r_ldm(EmArea01RenderCore *c, uint32_t a, uint32_t m[4][4])
{
    for (int i = 0; i < 4; ++i)
        if (em_a01r_ldq(c, a + 16u * (uint32_t)i, m[i]) < 0) return -1;
    return 0;
}

#define EM_A01R_TRY(expr) do { if ((expr) < 0) return -1; } while (0)

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_RENDER_MEM_H */
