/* em_frame_render_heads.c - the frame setup and projection heads (see
 * em_frame_render_heads.h, docs/FRAME_RENDER_HEADS.md).
 *
 * Read from the original instructions: the decomp C where it is byte-matched
 * (001D1EA0, 001D1EF0, 001D19D0, 001D19E0, 001D2960, 001D2D20, 001D25F0,
 * 001D2610, 001C1D00, 001D8060, 001D80B0, 001D88B0), the split listing for
 * the NEARMISS units (001D1C50, 001D30A0, 001D8C30, 001D9070), the asm-word
 * unit 001D2830 and the inline-asm units 001D2590, copy_qw4, 00102948,
 * 001026D0 and 001029C0. The original address a branch, load or store comes
 * from is cited beside it. Float arithmetic and compares go through
 * em_ee_float.h on bit patterns, as the COP1 and VU0 instructions execute. */
#include "game/em_frame_render_heads.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

/* Float constants (bit patterns the original materialises). */
#define F_ZERO   UINT32_C(0x00000000)
#define F_ONE    UINT32_C(0x3F800000)
#define F_TWO    UINT32_C(0x40000000)
#define F_HALF   UINT32_C(0x3F000000)
#define F_0_8    UINT32_C(0x3F4CCCCD)
#define F_0_1    UINT32_C(0x3DCCCCCD)
#define F_0_2    UINT32_C(0x3E4CCCCD)
#define F_0_3    UINT32_C(0x3E99999A)
#define F_M2     UINT32_C(0xC0000000)
#define F_5      UINT32_C(0x40A00000)
#define F_20     UINT32_C(0x41A00000)
#define F_45     UINT32_C(0x42340000)
#define F_50     UINT32_C(0x42480000)
#define F_64     UINT32_C(0x42800000)
#define F_128    UINT32_C(0x43000000)
#define F_150    UINT32_C(0x43160000)
#define F_190    UINT32_C(0x433E0000)
#define F_200    UINT32_C(0x43480000)
#define F_210    UINT32_C(0x43520000)
#define F_224    UINT32_C(0x43600000)
#define F_450    UINT32_C(0x43E10000)
#define F_560    UINT32_C(0x440C0000)
#define F_1280   UINT32_C(0x44A00000)
#define F_2048   UINT32_C(0x45000000)
#define F_3584   UINT32_C(0x45600000)
#define F_M1023  UINT32_C(0xC47FC000)
#define F_1046529 UINT32_C(0x497F8010)
#define F_FAR    UINT32_C(0x4B7F0000) /* 16711680 */
#define F_BIAS   UINT32_C(0x4B000000) /* 8388608 */
#define F_BIAS64 UINT32_C(0x4B000040) /* 8388672 */
#define F_DEG    UINT32_C(0x3C8EFA35) /* 0.017453292 */

/* ---- fault latch ------------------------------------------------------------ */

static int fail(EmFrh *h, int32_t code, uint32_t data)
{
    if (h->fault.code == EM_FRH_FAULT_NONE) {
        h->fault.address = h->fn;
        h->fault.code = code;
        h->fault.data = data;
    }
    return -1;
}

/* Entry: refuse while a fault is latched, then name the running routine. */
static int enter(EmFrh *h, uint32_t fn)
{
    if (!h || h->fault.code != EM_FRH_FAULT_NONE) return -1;
    h->fn = fn;
    return 0;
}

#define ENTER(fn) do { if (enter(h, (fn)) < 0) return -1; } while (0)
#define TRY(expr) do { if ((expr) < 0) return -1; } while (0)
/* A nested translated routine: its own entry names itself; afterwards the
 * caller is the running routine again. */
#define SUB(expr) do { uint32_t fn_ = h->fn; if ((expr) < 0) return -1; h->fn = fn_; } while (0)
/* A worker call: NULL or a negative result latches a fault. */
#define WORK(worker, callee, ...) do {                                                   \
        if (!h->workers.worker) return fail(h, EM_FRH_FAULT_NULL_WORKER, (callee));     \
        if (h->workers.worker(h->workers.ctx, __VA_ARGS__) < 0)                         \
            return fail(h, EM_FRH_FAULT_WORKER_FAILED, (callee));                       \
    } while (0)
#define WORK0(worker, callee) do {                                                       \
        if (!h->workers.worker) return fail(h, EM_FRH_FAULT_NULL_WORKER, (callee));     \
        if (h->workers.worker(h->workers.ctx) < 0)                                      \
            return fail(h, EM_FRH_FAULT_WORKER_FAILED, (callee));                       \
    } while (0)
#define NEED(worker, callee) do {                                                        \
        if (!h->workers.worker) return fail(h, EM_FRH_FAULT_NULL_WORKER, (callee));     \
    } while (0)
#define VU(expr) do { if ((expr) != EM_EE_FLOAT_OK) return fail(h, EM_FRH_FAULT_UNMEASURED_FORM, 0); } while (0)

/* ---- memory ------------------------------------------------------------------- */

static uint8_t *map(const EmFrh *h, uint32_t address, uint32_t size, int write)
{
    for (uint32_t i = 0; i < h->view_count; ++i) {
        const EmFrhView *v = &h->views[i];
        if (!v->bytes || address < v->address) continue;
        uint32_t at = address - v->address;
        if (size > v->size || at > v->size - size) continue;
        if (write && !v->writable) return NULL;
        return v->bytes + at;
    }
    return NULL;
}

/* Probe (no access): 0, or a fault. */
static int need(EmFrh *h, uint32_t address, uint32_t size, int write)
{
    return map(h, address, size, write) ? 0 : fail(h, EM_FRH_FAULT_BAD_ADDRESS, address);
}

static int load(EmFrh *h, uint32_t address, uint32_t size, uint64_t *value)
{
    if (size > 1 && (address & (size - 1)) != 0) return fail(h, EM_FRH_FAULT_BAD_ADDRESS, address);
    const uint8_t *p = map(h, address, size, 0);
    if (!p) return fail(h, EM_FRH_FAULT_BAD_ADDRESS, address);
    uint64_t v = 0;
    for (uint32_t i = size; i-- > 0;) v = v << 8 | p[i];
    *value = v;
    return 0;
}

static int store(EmFrh *h, uint32_t address, uint32_t size, uint64_t value)
{
    if (size > 1 && (address & (size - 1)) != 0) return fail(h, EM_FRH_FAULT_BAD_ADDRESS, address);
    uint8_t *p = map(h, address, size, 1);
    if (!p) return fail(h, EM_FRH_FAULT_BAD_ADDRESS, address);
    for (uint32_t i = 0; i < size; ++i) p[i] = (uint8_t)(value >> (8 * i));
    return 0;
}

static int lw(EmFrh *h, uint32_t address, uint32_t *v)
{
    uint64_t x;
    TRY(load(h, address, 4, &x));
    *v = (uint32_t)x;
    return 0;
}

static int lbu(EmFrh *h, uint32_t address, uint8_t *v)
{
    uint64_t x;
    TRY(load(h, address, 1, &x));
    *v = (uint8_t)x;
    return 0;
}

static int sw(EmFrh *h, uint32_t address, uint32_t v) { return store(h, address, 4, v); }

/* lq / lqc2: 16 bytes from address & ~15, as four words. */
static int lq(EmFrh *h, uint32_t address, uint32_t out[4])
{
    address &= ~UINT32_C(15);
    const uint8_t *p = map(h, address, 16, 0);
    if (!p) return fail(h, EM_FRH_FAULT_BAD_ADDRESS, address);
    for (unsigned k = 0; k < 4; ++k)
        out[k] = (uint32_t)p[4 * k] | (uint32_t)p[4 * k + 1] << 8 | (uint32_t)p[4 * k + 2] << 16 |
                 (uint32_t)p[4 * k + 3] << 24;
    return 0;
}

/* sq / sqc2: 16 bytes to address & ~15. */
static int sq(EmFrh *h, uint32_t address, const uint32_t in[4])
{
    address &= ~UINT32_C(15);
    uint8_t *p = map(h, address, 16, 1);
    if (!p) return fail(h, EM_FRH_FAULT_BAD_ADDRESS, address);
    for (unsigned k = 0; k < 4; ++k)
        for (unsigned i = 0; i < 4; ++i) p[4 * k + i] = (uint8_t)(in[k] >> (8 * i));
    return 0;
}

/* The render context address (the D_00275670 word). */
static int context(EmFrh *h, uint32_t *ctx) { return lw(h, EM_FRH_D_00275670, ctx); }

/* ---- SDK leaves ------------------------------------------------------------- */

/* copy_qw4 (00102958)(dst, src): four quadword loads from src, then four
 * quadword stores to dst. */
static int copy_qw4(EmFrh *h, uint32_t dst, uint32_t src)
{
    uint32_t q[4][4];
    for (unsigned r = 0; r < 4; ++r) TRY(lq(h, src + 16 * r, q[r]));
    for (unsigned r = 0; r < 4; ++r) TRY(sq(h, dst + 16 * r, q[r]));
    return 0;
}

/* 00102948(dst, src): one quadword. */
static int copy_qw(EmFrh *h, uint32_t dst, uint32_t src)
{
    uint32_t q[4];
    TRY(lq(h, src, q));
    return sq(h, dst, q);
}

/* 001026D0's row: ACC = a0 * v.x; ACC += a1 * v.y; ACC += a2 * v.z;
 * out = ACC + a3 * v.w (the MULAbc x, MADDAbc y and z, MADDbc w forms, all
 * four lanes). */
static int vu_row(EmFrh *h, uint32_t out[4], const uint32_t a[16], const uint32_t v[4])
{
    uint32_t acc[4] = {0, 0, 0, 0};
    VU(em_vu_vec_bits(EM_VU_MULABC, 15, 0, a + 0, v, 0, NULL, acc));
    VU(em_vu_vec_bits(EM_VU_MADDABC, 15, 1, a + 4, v, 0, acc, acc));
    VU(em_vu_vec_bits(EM_VU_MADDABC, 15, 2, a + 8, v, 0, acc, acc));
    VU(em_vu_vec_bits(EM_VU_MADDBC, 15, 3, a + 12, v, 0, acc, out));
    return 0;
}

/* 001026D0(dst, a, b) with `a` already in registers: each row of b (read at
 * b + 16 r just before its row of dst is stored) goes through a. */
static int vu_product(EmFrh *h, uint32_t dst, const uint32_t a[16], uint32_t b)
{
    for (unsigned r = 0; r < 4; ++r) {
        uint32_t v[4], out[4] = {0, 0, 0, 0};
        TRY(lq(h, b + 16 * r, v));
        TRY(vu_row(h, out, a, v));
        TRY(sq(h, dst + 16 * r, out));
    }
    return 0;
}

/* 001026D0(dst, a, b) with a in memory: its four rows are loaded first. */
static int vu_product_mem(EmFrh *h, uint32_t dst, uint32_t a, uint32_t b)
{
    uint32_t m[16];
    for (unsigned r = 0; r < 4; ++r) TRY(lq(h, a + 16 * r, m + 4 * r));
    return vu_product(h, dst, m, b);
}

/* 001029C0(m): row 0 = vf0 - vf0 (all lanes) with w = that w + vf0.w, and
 * rows 1..3 each the previous row rotated by one lane (VMR32); stored rows
 * 3, 2, 1, 0. The result is the identity. */
static int vu_identity(EmFrh *h, uint32_t m[16])
{
    static const uint32_t vf0[4] = {0, 0, 0, F_ONE};
    uint32_t r0[4] = {0, 0, 0, 0};
    VU(em_vu_vec_bits(EM_VU_SUB, 15, EM_VU_NO_BC, vf0, vf0, 0, NULL, r0));
    VU(em_vu_vec_bits(EM_VU_ADD, 1, EM_VU_NO_BC, r0, vf0, 0, NULL, r0));
    uint32_t r[4][4];
    memcpy(r[0], r0, sizeof r0);
    for (unsigned i = 1; i < 4; ++i)
        for (unsigned k = 0; k < 4; ++k) r[i][k] = r[i - 1][(k + 1) & 3];
    /* m rows: +0x30 = r0, +0x20 = r1, +0x10 = r2, +0x00 = r3. */
    for (unsigned i = 0; i < 4; ++i) memcpy(m + 4 * (3 - i), r[i], sizeof r[i]);
    return 0;
}

/* ---- 001D2830 ------------------------------------------------------------- */

/* 001D2830(a0, a1): a0 < 0x20 (signed) -> v0 = 001D2730(a0, a1);
 * a0 < 0x40 -> v0 = 001E0C80(a0, a1); otherwise v0 = 0. */
int em_frh_001D2830(EmFrh *h, int32_t a0, int32_t a1, int32_t *ret)
{
    ENTER(0x001D2830u);
    int32_t v = 0;
    if (a0 < 0x20) {                                           /* 001D2834 */
        WORK(w_001D2730, 0x001D2730u, a0, a1, &v);             /* 001D2840 */
    } else if (a0 < 0x40) {                                    /* 001D2850 */
        WORK(w_001E0C80, 0x001E0C80u, a0, a1, &v);             /* 001D285C */
    }
    if (ret) *ret = v;
    return 0;
}

/* ---- 001D2D20 ------------------------------------------------------------- */

/* 001D2D20(m, focal, width, height, near, far): identity, then
 *   m[0]  = focal / (0.5 * width)       m[5]  = focal / (0.5 * height)
 *   m[10] = (far + near) / (far - near) m[14] = (-2 * (far * near)) / (far - near)
 *   m[11] = 1, m[15] = 0. */
int em_frh_001D2D20(EmFrh *h, uint32_t m[16], uint32_t focal, uint32_t width, uint32_t height,
                    uint32_t znear, uint32_t zfar)
{
    ENTER(0x001D2D20u);
    if (!m) return fail(h, EM_FRH_FAULT_BAD_ADDRESS, 0);
    TRY(vu_identity(h, m));                                             /* 001D2D54 */
    uint32_t hw = em_ee_mul_bits(F_HALF, width);
    uint32_t hh = em_ee_mul_bits(F_HALF, height);
    m[0] = em_ee_div_bits(focal, hw);
    m[5] = em_ee_div_bits(focal, hh);
    uint32_t product = em_ee_mul_bits(zfar, znear);
    uint32_t sum = em_ee_add_bits(zfar, znear);
    uint32_t scaled = em_ee_mul_bits(F_M2, product);
    uint32_t depth = em_ee_sub_bits(zfar, znear);
    m[11] = F_ONE;
    m[15] = F_ZERO;
    m[10] = em_ee_div_bits(sum, depth);
    m[14] = em_ee_div_bits(scaled, depth);
    return 0;
}

/* ---- 001D2960 ------------------------------------------------------------- */

static int ctx_sw(EmFrh *h, uint32_t ctx, uint32_t off, uint32_t v) { return sw(h, ctx + off, v); }

/* 001D2960(view): the projection of this frame. */
int em_frh_001D2960(EmFrh *h, uint32_t view)
{
    ENTER(0x001D2960u);
    NEED(w_0011E748, 0x0011E748u);
    uint32_t ctx;
    TRY(context(h, &ctx));
    TRY(need(h, ctx + EM_FRH_CTX_GUARD, EM_FRH_CTX_PLANES + 0x40 - EM_FRH_CTX_GUARD, 1));
    TRY(need(h, ctx + EM_FRH_CTX_ZOOM, 4, 0));
    TRY(need(h, view & ~UINT32_C(15), 0x40, 0));

    uint32_t s;
    TRY(lw(h, ctx + EM_FRH_CTX_ZOOM, &s));
    /* P (001D29B0..001D2A2C): x = 0.8 s, y = 0.5 s, z/w rows constant. */
    TRY(ctx_sw(h, ctx, 0x2340, em_ee_mul_bits(F_0_8, s)));
    TRY(ctx_sw(h, ctx, 0x2350, F_ZERO));
    TRY(ctx_sw(h, ctx, 0x2360, F_2048));
    TRY(ctx_sw(h, ctx, 0x2370, F_ZERO));
    TRY(ctx_sw(h, ctx, 0x2344, F_ZERO));
    TRY(ctx_sw(h, ctx, 0x2354, em_ee_mul_bits(F_HALF, s)));
    TRY(ctx_sw(h, ctx, 0x2364, F_2048));
    TRY(ctx_sw(h, ctx, 0x2374, F_ZERO));
    TRY(ctx_sw(h, ctx, 0x2348, F_ZERO));
    TRY(ctx_sw(h, ctx, 0x2358, F_ZERO));
    TRY(ctx_sw(h, ctx, 0x2368, UINT32_C(0x3F664CB3)));
    TRY(ctx_sw(h, ctx, 0x2378, UINT32_C(0x49CCCCCC)));
    TRY(ctx_sw(h, ctx, 0x234C, F_ZERO));
    TRY(ctx_sw(h, ctx, 0x235C, F_ZERO));
    TRY(ctx_sw(h, ctx, 0x236C, F_ONE));
    TRY(ctx_sw(h, ctx, 0x237C, F_ZERO));

    TRY(copy_qw4(h, ctx + EM_FRH_CTX_V, view));                                   /* V */
    TRY(vu_product_mem(h, ctx + EM_FRH_CTX_K, ctx + EM_FRH_CTX_P, ctx + EM_FRH_CTX_V)); /* K */

    /* The four alternate projections (f13 width, f14 height, f15 near,
     * f16 far), each multiplied with V into +0x2240 + 0x40 i. */
    static const uint32_t alt[4][3] = {
        {F_1280, F_560, F_0_1}, {F_1280, F_560, F_20}, {F_3584, F_3584, F_0_1}, {F_2048, F_2048, F_0_1},
    };
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t m[16];
        SUB(em_frh_001D2D20(h, m, s, alt[i][0], alt[i][1], alt[i][2], F_FAR));
        TRY(vu_product(h, ctx + EM_FRH_CTX_ALT + 0x40 * i, m, ctx + EM_FRH_CTX_V));
    }

    /* Guard band (001D2B6C..001D2BCC). */
    TRY(ctx_sw(h, ctx, 0x2220, UINT32_C(0x3A008081)));
    TRY(ctx_sw(h, ctx, 0x2224, UINT32_C(0x3A008081)));
    TRY(ctx_sw(h, ctx, 0x2228, UINT32_C(0x34000000)));
    TRY(ctx_sw(h, ctx, 0x222C, F_ONE));
    TRY(ctx_sw(h, ctx, 0x2230, UINT32_C(0xBF808081)));
    TRY(ctx_sw(h, ctx, 0x2234, UINT32_C(0xBF808081)));
    TRY(ctx_sw(h, ctx, 0x2238, UINT32_C(0xBF7FFFFE)));
    uint32_t norm = em_ee_add_bits(F_1046529, em_ee_mul_bits(s, s));
    TRY(ctx_sw(h, ctx, 0x223C, F_ZERO));

    /* The four planes: each takes its own sqrtf(norm) call; inv = 1 / root.
     * Plane i: (+-s * inv in lane 0 (i < 2) or lane 1, 0, -(-1023 * inv), 0). */
    uint32_t neg_s = 0;
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t root;
        WORK(w_0011E748, 0x0011E748u, norm, &root);
        if (i == 1) neg_s = em_ee_neg_bits(s);                 /* 001D2C28 */
        uint32_t inv = em_ee_div_bits(F_ONE, root);
        uint32_t lane = em_ee_mul_bits(i & 1 ? neg_s : s, inv);
        uint32_t z = em_ee_neg_bits(em_ee_mul_bits(F_M1023, inv));
        uint32_t base = EM_FRH_CTX_PLANES + 0x10 * i;
        if (i < 2) {
            TRY(ctx_sw(h, ctx, base + 0, lane));
            TRY(ctx_sw(h, ctx, base + 4, F_ZERO));
        } else {
            TRY(ctx_sw(h, ctx, base + 0, F_ZERO));
            TRY(ctx_sw(h, ctx, base + 4, lane));
        }
        TRY(ctx_sw(h, ctx, base + 8, z));
        TRY(ctx_sw(h, ctx, base + 12, F_ZERO));
    }
    return 0;
}

/* ---- 001D30A0 ------------------------------------------------------------- */

/* The skin records (D_00816440 + 0x100 k) that take the three rows, in
 * 001D30A0's order; 0x816540 follows after the four constant words and
 * 0x816840 takes its own constants. */
static const uint32_t k_records_30A0[] = {
    0x00816440u, 0x00816640u, 0x00816740u, 0x00816B40u, 0x00816C40u, 0x00816D40u,
    0x00816E40u, 0x00816F40u, 0x00817040u, 0x00817140u, 0x00816940u, 0x00816A40u,
};

/* One record: 00102948 copies of context +0x2220 to +0x60, +0x2230 to +0x70
 * and +0xA0 to +0x50 of the slot (base + (context +0x9C << 7)); the slot
 * index is re-read before each copy, as the original does. */
static int slot_of(EmFrh *h, uint32_t ctx, uint32_t base, uint32_t *slot)
{
    uint32_t index;
    TRY(lw(h, ctx + EM_FRH_CTX_SLOT, &index));
    *slot = base + (index << 7);
    return 0;
}

static int guard_rows(EmFrh *h, uint32_t ctx, uint32_t base)
{
    uint32_t slot;
    TRY(slot_of(h, ctx, base, &slot));
    TRY(copy_qw(h, slot + 0x60, ctx + 0x2220));
    TRY(slot_of(h, ctx, base, &slot));
    TRY(copy_qw(h, slot + 0x70, ctx + 0x2230));
    TRY(slot_of(h, ctx, base, &slot));
    return copy_qw(h, slot + 0x50, ctx + EM_FRH_CTX_FOG);
}

static int slot_word(EmFrh *h, uint32_t ctx, uint32_t address, uint32_t v)
{
    uint32_t slot;
    TRY(slot_of(h, ctx, address, &slot));
    return sw(h, slot, v);
}

int em_frh_001D30A0(EmFrh *h)
{
    ENTER(0x001D30A0u);
    uint32_t ctx;
    TRY(context(h, &ctx));
    TRY(need(h, EM_FRH_D_00816440, 0xE00, 1));
    TRY(need(h, ctx + EM_FRH_CTX_GUARD, 0x20, 0));
    TRY(need(h, ctx + EM_FRH_CTX_FOG, 0x10, 0));
    TRY(need(h, EM_FRH_SPR_3AC0, 0x40, 0));
    TRY(need(h, EM_FRH_D_002513E0, 0x40, 1));

    for (size_t i = 0; i < sizeof k_records_30A0 / sizeof k_records_30A0[0]; ++i)
        TRY(guard_rows(h, ctx, k_records_30A0[i]));
    /* 001D35EC..001D3628: record 0x816B40 +0x58 = 255, +0x5C = 0; record 0x816C40
     * likewise. */
    TRY(slot_word(h, ctx, 0x00816B98u, UINT32_C(0x437F0000)));
    TRY(slot_word(h, ctx, 0x00816B9Cu, 0));
    TRY(slot_word(h, ctx, 0x00816C98u, UINT32_C(0x437F0000)));
    TRY(slot_word(h, ctx, 0x00816C9Cu, 0));
    TRY(guard_rows(h, ctx, 0x00816540u));
    /* 001D36C0..001D3770: record 0x816840 takes its own guard row values at
     * +0x60..+0x7C, then +0x50 from context +0xA0. */
    static const struct { uint32_t address, value; } k_816840[] = {
        {0x008168A0u, 0x3A008081u}, {0x008168A4u, 0x3A008081u}, {0x008168A8u, 0x34008080u},
        {0x008168ACu, 0},           {0x008168B0u, 0xBF808081u}, {0x008168B4u, 0xBF808081u},
        {0x008168B8u, 0},           {0x008168BCu, 0x3F800000u},
    };
    for (size_t i = 0; i < sizeof k_816840 / sizeof k_816840[0]; ++i)
        TRY(slot_word(h, ctx, k_816840[i].address, k_816840[i].value));
    uint32_t slot;
    TRY(slot_of(h, ctx, 0x00816840u, &slot));
    TRY(copy_qw(h, slot + 0x50, ctx + EM_FRH_CTX_FOG));
    /* 001D3798..001D37B4: the four quadwords of spad 0x3AC0 to D_002513E0. */
    return copy_qw4(h, EM_FRH_D_002513E0, EM_FRH_SPR_3AC0);
}

/* ---- 001D1AE0 ------------------------------------------------------------- */

/* 001D1AE0(a0) (NEARMISS C; followed from the .s). D_00275670 is
 * re-read before every store; the cursors are the arena D_0028F700 plus
 * a0 * 0x60800 + 8 + 0x7FF8 (+0x10), a0 * 0x95760 (low word of the MULT)
 * + 0xC9000 (+0x14), a0 * 0x70000 + 0x1F3EC0 (+0x18) and (a0 << 20) +
 * 0x2D3EC0 (+0x1C), all in 32-bit arithmetic. 001CBA40, called after the
 * +0x54 store, is an empty routine. */
int em_frh_001D1AE0(EmFrh *h, int32_t index)
{
    ENTER(0x001D1AE0u);
    NEED(w_001D1F20, 0x001D1F20u);
    NEED(w_001D2040, 0x001D2040u);
    NEED(w_001D1FF0, 0x001D1FF0u);
    NEED(w_001CB8A0, 0x001CB8A0u);
    NEED(w_001D2DE0, 0x001D2DE0u);
    TRY(need(h, EM_FRH_D_00275670, 4, 0));
    uint32_t ctx;
    TRY(context(h, &ctx));
    TRY(need(h, ctx + EM_FRH_CTX_CURSOR, 0x10, 1));
    TRY(need(h, ctx + 0x50u, 0x10, 1));
    TRY(need(h, ctx + EM_FRH_CTX_SLOT, 4, 1));

    const uint32_t a0 = (uint32_t)index;
    TRY(sw(h, ctx + EM_FRH_CTX_SLOT, a0));                             /* 001D1B08 */
    const uint32_t c10 = EM_FRH_D_0028F700 + (((((a0 << 1) + a0) << 6) + a0) << 11) + 8u + 0x7FF8u;
    TRY(context(h, &ctx));
    TRY(sw(h, ctx + EM_FRH_CTX_CURSOR, c10));                          /* 001D1B20 */
    const uint32_t c14 = EM_FRH_D_0028F700 + a0 * UINT32_C(0x95760) + UINT32_C(0xC9000);
    TRY(context(h, &ctx));
    TRY(sw(h, ctx + EM_FRH_CTX_CURSOR + 4u, c14));                     /* 001D1B4C */
    const uint32_t c18 = EM_FRH_D_0028F700 + (((a0 << 3) - a0) << 16) + UINT32_C(0x1F3EC0);
    TRY(context(h, &ctx));
    TRY(sw(h, ctx + EM_FRH_CTX_CURSOR + 8u, c18));                     /* 001D1B88 */
    const uint32_t c1c = EM_FRH_D_0028F700 + (a0 << 20) + UINT32_C(0x2D3EC0);
    TRY(context(h, &ctx));
    TRY(sw(h, ctx + EM_FRH_CTX_CURSOR + 12u, c1c));                    /* 001D1B98 */
    TRY(context(h, &ctx));
    TRY(sw(h, ctx + 0x50u, 0));                                        /* 001D1BA0 */
    TRY(context(h, &ctx));
    TRY(sw(h, ctx + 0x5Cu, 0));                                        /* 001D1BA8 */
    TRY(context(h, &ctx));
    TRY(sw(h, ctx + 0x58u, 0));                                        /* 001D1BB0 */
    TRY(context(h, &ctx));
    TRY(sw(h, ctx + 0x54u, 0));                                        /* 001D1BBC */
    /* 001D1BB8: 001CBA40 is empty. */
    WORK(w_001D1F20, 0x001D1F20u, 1);                                  /* 001D1BC0 */
    WORK(w_001D2040, 0x001D2040u, 1, 0);                               /* 001D1BCC */
    WORK(w_001D1FF0, 0x001D1FF0u, 1, 1);                               /* 001D1BD8 */
    TRY(context(h, &ctx));                                             /* 001D1BE0 */
    WORK(w_001CB8A0, 0x001CB8A0u, EM_FRH_D_007635C0, 0, ctx, ctx + 4u); /* 001D1BF0 */
    WORK(w_001D2DE0, 0x001D2DE0u, 0, 0);                               /* 001D1BFC */
    return 0;
}

/* ---- 001D1C50 ------------------------------------------------------------- */

static int fog_flag(EmFrh *h, int *flag)
{
    /* 0015D2F0() == 2 && D_008106C6 == 2 (001D1CB0 / 001D1D88). */
    int32_t variant;
    uint8_t c6;
    *flag = 0;
    WORK(w_0015D2F0, 0x0015D2F0u, &variant);
    if (variant != 2) return 0;
    TRY(lbu(h, EM_FRH_D_008106C6, &c6));
    if (c6 == 2) *flag = 1;
    return 0;
}

int em_frh_001D1C50(EmFrh *h)
{
    ENTER(0x001D1C50u);
    /* Every worker the call tree reaches, and the fixed addresses. */
    NEED(w_001D2730, 0x001D2730u);
    NEED(w_001B0070, 0x001B0070u);
    NEED(w_0015D2F0, 0x0015D2F0u);
    NEED(w_0021B970, 0x0021B970u);
    NEED(w_0021B9A0, 0x0021B9A0u);
    NEED(w_0021BA80, 0x0021BA80u);
    NEED(w_001D7C30, 0x001D7C30u);
    NEED(w_0011E748, 0x0011E748u);
    TRY(need(h, EM_FRH_D_00275670, 8, 0));
    TRY(need(h, EM_FRH_D_008106C4, 4, 0));
    TRY(need(h, EM_FRH_D_00810700, 1, 0));
    TRY(need(h, EM_FRH_SPR_3B8D, 1, 0));
    TRY(need(h, EM_FRH_D_00810610, 0x40, 0));
    TRY(need(h, EM_FRH_SPR_3A40, 0x100, 1));
    TRY(need(h, EM_FRH_D_00816440, 0xE00, 1));
    TRY(need(h, EM_FRH_D_002513E0, 0x40, 1));
    {
        uint32_t ctx;
        TRY(context(h, &ctx));
        TRY(need(h, ctx + EM_FRH_CTX_CURSOR, 4, 1));
        TRY(need(h, ctx + EM_FRH_CTX_SLOT, 0x18, 0));
        TRY(need(h, ctx + EM_FRH_CTX_GUARD, EM_FRH_CTX_ZOOM + 4 - EM_FRH_CTX_GUARD, 1));
    }

    int32_t ignored;
    SUB(em_frh_001D2830(h, 4, 0, &ignored));                          /* 001D1C60 */
    uint8_t c4;
    TRY(lbu(h, EM_FRH_D_008106C4, &c4));
    if (c4 != 0) {                                                     /* 001D1C70 */
        SUB(em_frh_001D2830(h, 6, 0, &ignored));                      /* 001D1C7C */
    } else {
        uint32_t flags;
        WORK(w_001B0070, 0x001B0070u, &flags);                         /* 001D1C8C */
        if (flags & 0x80) {                                            /* 001D1C98 */
            uint8_t c7;
            int flag = 1;
            TRY(lbu(h, EM_FRH_D_008106C7, &c7));
            if (c7 == 0) TRY(fog_flag(h, &flag));                      /* 001D1CA8 */
            if (flag == 0) {
                WORK(w_0021B970, 0x0021B970u, F_ZERO, F_50);           /* 001D1CEC */
            } else {
                uint8_t area;
                TRY(lbu(h, EM_FRH_D_00810700, &area));
                if (area == 8) WORK(w_0021B970, 0x0021B970u, F_50, F_150);   /* 001D1D20 */
                else WORK(w_0021B970, 0x0021B970u, F_ZERO, F_210);           /* 001D1D3C */
            }
            WORK(w_0021BA80, 0x0021BA80u, 8, 8, 0x15);                 /* 001D1D4C */
            SUB(em_frh_001D2830(h, 6, 1, &ignored));                   /* 001D1D58 */
        } else {
            uint8_t selector;
            TRY(lbu(h, EM_FRH_SPR_3B8D, &selector));                  /* 001D1D6C */
            if (selector == 0 || selector == 4) {
                int flag;
                TRY(fog_flag(h, &flag));
                if (flag == 0) WORK(w_0021B9A0, 0x0021B9A0u, F_ZERO, F_ZERO, 0);   /* 001D1DC0 */
            }
            SUB(em_frh_001D2830(h, 6, 1, &ignored));                   /* 001D1DCC */
        }
    }

    /* 001D1DD4..001D1E4C: slot +0x360 = context +0xB0 (doubleword); then
     * the REF tag at the cursor: byte 3 = 0x30, word 1 = slot + 0x340,
     * halfword 0 = 3 (the tag's bytes 2 and 8..15 are left as they are),
     * and the cursor advances by 0x10. Pointers are re-read as the
     * original re-reads them. */
    uint32_t ctx, slots, index;
    uint64_t b0;
    TRY(context(h, &ctx));
    TRY(lw(h, EM_FRH_D_00275674, &slots));
    TRY(load(h, ctx + EM_FRH_CTX_B0, 8, &b0));
    TRY(lw(h, ctx + EM_FRH_CTX_SLOT, &index));
    TRY(store(h, slots + index * 0x30u + 0x360u, 8, b0));
    TRY(context(h, &ctx));
    TRY(lw(h, EM_FRH_D_00275674, &slots));
    TRY(lw(h, ctx + EM_FRH_CTX_SLOT, &index));
    uint32_t cursor, tag_address = slots + index * 0x30u + 0x340u;
    TRY(lw(h, ctx + EM_FRH_CTX_CURSOR, &cursor));
    TRY(store(h, cursor + 3, 1, 0x30));
    TRY(lw(h, ctx + EM_FRH_CTX_CURSOR, &cursor));
    TRY(sw(h, cursor + 4, tag_address));
    TRY(lw(h, ctx + EM_FRH_CTX_CURSOR, &cursor));
    TRY(store(h, cursor, 2, 3));
    TRY(lw(h, ctx + EM_FRH_CTX_CURSOR, &cursor));
    TRY(sw(h, ctx + EM_FRH_CTX_CURSOR, cursor + 0x10u));

    SUB(em_frh_001D2960(h, EM_FRH_D_00810610));                        /* 001D1E48 */
    TRY(context(h, &ctx));
    TRY(copy_qw4(h, EM_FRH_SPR_3A40, ctx + EM_FRH_CTX_P));             /* 001D1E5C */
    TRY(context(h, &ctx));
    TRY(copy_qw4(h, EM_FRH_SPR_3AC0, ctx + EM_FRH_CTX_K));             /* 001D1E70 */
    WORK0(w_001D7C30, 0x001D7C30u);                                    /* 001D1E78 */
    SUB(em_frh_001D30A0(h));                                           /* 001D1E80 */
    return 0;
}

/* ---- 001D1EA0 / 001D1EF0 ---------------------------------------------------- */

int em_frh_001D1EA0(EmFrh *h, int32_t a0)
{
    ENTER(0x001D1EA0u);
    NEED(w_001CB800, 0x001CB800u);
    if (a0 != 0) {
        NEED(w_001D2910, 0x001D2910u);
        NEED(w_001E0D70, 0x001E0D70u);
        NEED(w_001DDA00, 0x001DDA00u);
    }
    TRY(need(h, EM_FRH_D_00275670, 4, 0));
    if (a0 != 0) {                                                     /* 001D1EA4 */
        int32_t off;
        WORK(w_001D2910, 0x001D2910u, 4, &off);                        /* 001D1EAC */
        if (off == 0) {                                                /* 001D1EB4 */
            WORK0(w_001E0D70, 0x001E0D70u);
            WORK0(w_001DDA00, 0x001DDA00u);
        }
    }
    uint32_t ctx;
    TRY(context(h, &ctx));                                             /* 001D1ECC */
    WORK(w_001CB800, 0x001CB800u, EM_FRH_D_007635C0, 0, ctx, ctx + 4u);
    return 0;
}

int em_frh_001D1EF0(EmFrh *h)
{
    ENTER(0x001D1EF0u);
    NEED(w_001CB800, 0x001CB800u);
    SUB(em_frh_001D1C50(h));
    int32_t ignored;
    SUB(em_frh_001D2830(h, 3, 1, &ignored));
    SUB(em_frh_001D1EA0(h, 0));
    return 0;
}

/* ---- 001D19E0 / 001D19D0 / 001D9070 ------------------------------------------ */

int em_frh_001D19E0(EmFrh *h)
{
    ENTER(0x001D19E0u);
    NEED(w_skin_arena_init, 0x001D2E20u);
    NEED(w_001D9720, 0x001D9720u);
    NEED(w_001DD940, 0x001DD940u);
    NEED(w_001E0C30, 0x001E0C30u);
    NEED(w_001D9060, 0x001D9060u);
    NEED(w_001D71F0, 0x001D71F0u);
    NEED(w_001D7BB0, 0x001D7BB0u);
    NEED(w_001D2730, 0x001D2730u);
    NEED(w_001E0C80, 0x001E0C80u);
    NEED(w_001D2DE0, 0x001D2DE0u);
    NEED(w_001E0CC0, 0x001E0CC0u);
    NEED(w_001E0380, 0x001E0380u);
    TRY(need(h, EM_FRH_D_00275670, 4, 0));
    TRY(need(h, EM_FRH_D_00810700, 2, 0));

    int32_t ignored;
    WORK0(w_skin_arena_init, 0x001D2E20u);
    WORK0(w_001D9720, 0x001D9720u);
    WORK0(w_001DD940, 0x001DD940u);
    WORK0(w_001E0C30, 0x001E0C30u);
    WORK0(w_001D9060, 0x001D9060u);
    WORK0(w_001D71F0, 0x001D71F0u);
    WORK0(w_001D7BB0, 0x001D7BB0u);
    SUB(em_frh_001D2830(h, 2, 0, &ignored));
    SUB(em_frh_001D2830(h, 9, 0, &ignored));
    SUB(em_frh_001D2830(h, 0x24, 0, &ignored));
    SUB(em_frh_001D2830(h, 5, 0, &ignored));
    WORK(w_001D2DE0, 0x001D2DE0u, 1, 0x320);
    WORK(w_001D2DE0, 0x001D2DE0u, 2, 0);
    SUB(em_frh_001D2830(h, 7, 0, &ignored));
    SUB(em_frh_001D2830(h, 8, 0, &ignored));
    WORK0(w_001E0CC0, 0x001E0CC0u);
    WORK(w_001D2DE0, 0x001D2DE0u, 0, 0);
    uint32_t ctx;
    TRY(context(h, &ctx));
    TRY(sw(h, ctx + EM_FRH_CTX_1D8, 0));
    TRY(context(h, &ctx));
    TRY(sw(h, ctx + EM_FRH_CTX_1E8, 0));
    uint8_t area, sub;
    TRY(lbu(h, EM_FRH_D_00810700, &area));
    TRY(lbu(h, EM_FRH_D_00810701, &sub));
    if (((uint32_t)area << 8) + sub == 0x1100) WORK0(w_001E0380, 0x001E0380u);
    return 0;
}

/* 001D9070: the 0x16 model's per-vertex weights. For each group i < the
 * count word at the model, and each of its 32 entries j, with
 * e = model + 0x50 + 0x820 i + 0x40 j:
 *   t = e[+0x38] / 190;
 *   v = t <= 0.3 ? (t < 0 ? 1 : clamp(1 - t, 0, 1)) : 0;
 *   e[+0x2C] = e[+0x28] = e[+0x24] = e[+0x20] = 2 v. */
int em_frh_001D9070(EmFrh *h)
{
    ENTER(0x001D9070u);
    NEED(w_001C6120, 0x001C6120u);
    uint32_t bank, model;
    TRY(lw(h, EM_FRH_D_0028A56C, &bank));
    WORK(w_001C6120, 0x001C6120u, bank, 0x16, &model);                  /* 001D9080 */
    uint32_t group = model + 0x40u;
    for (int32_t i = 0;; ++i) {
        uint32_t count;
        TRY(lw(h, model, &count));                                      /* 001D9178..001D9180 */
        if (!(i < (int32_t)count)) break;
        uint32_t e = group + 0x10u;
        for (unsigned j = 0; j < 0x20; ++j, e += 0x40u) {
            uint32_t x, v;
            TRY(lw(h, e + 0x38u, &x));
            uint32_t t = em_ee_div_bits(x, F_190);
            if (!em_ee_c_le_bits(t, F_0_3)) {                           /* 001D90DC */
                v = F_ZERO;
            } else if (em_ee_c_lt_bits(t, F_ZERO)) {                    /* 001D90F8 */
                v = F_ONE;
            } else {
                v = em_ee_sub_bits(F_ONE, t);
                if (em_ee_c_lt_bits(v, F_ZERO)) v = F_ZERO;             /* 001D9118 */
                if (!em_ee_c_le_bits(v, F_ONE)) v = F_ONE;              /* 001D9130 */
            }
            v = em_ee_mul_bits(F_TWO, v);
            TRY(need(h, e + 0x20u, 0x10, 1));
            TRY(sw(h, e + 0x2Cu, v));
            TRY(sw(h, e + 0x28u, v));
            TRY(sw(h, e + 0x24u, v));
            TRY(sw(h, e + 0x20u, v));
        }
        group += 0x820u;
    }
    return 0;
}

int em_frh_001D19D0(EmFrh *h)
{
    ENTER(0x001D19D0u);
    SUB(em_frh_001D9070(h));
    return 0;
}

/* ---- zoom ------------------------------------------------------------------- */

/* 001D25F0(f12): spad 0x70003B60 = f12, then context +0x2468 = f12. */
int em_frh_001D25F0(EmFrh *h, uint32_t zoom)
{
    ENTER(0x001D25F0u);
    uint32_t ctx;
    TRY(context(h, &ctx));
    TRY(need(h, EM_FRH_SPR_3B60, 4, 1));
    TRY(need(h, ctx + EM_FRH_CTX_ZOOM, 4, 1));
    TRY(sw(h, EM_FRH_SPR_3B60, zoom));
    return sw(h, ctx + EM_FRH_CTX_ZOOM, zoom);
}

/* 001D2590(f12 = a, f13 = b): 001D25F0(a / tanf(b / 2)). */
int em_frh_001D2590(EmFrh *h, uint32_t size, uint32_t angle)
{
    ENTER(0x001D2590u);
    NEED(w_0011E398, 0x0011E398u);
    TRY(need(h, EM_FRH_D_00275670, 4, 0));
    uint32_t tangent;
    WORK(w_0011E398, 0x0011E398u, em_ee_div_bits(angle, F_TWO), &tangent);   /* 001D25A8, 001D25B4 */
    SUB(em_frh_001D25F0(h, em_ee_div_bits(size, tangent)));                    /* 001D25C4, 001D25D0 */
    return 0;
}

/* 001D2610(f12 = x):
 *   001D2590(224, 0.017453292 * (5 + 45 * (1 - x)));
 *   a = context +0xF8;
 *   b = context +0xFC when 001B0070() & 0x80 or D_00810700 == 0x11, else
 *       fc < 450 ? fc + x * (450 - fc) : fc + 200 * x;
 *   0021B970(a, b). */
int em_frh_001D2610(EmFrh *h, uint32_t x)
{
    ENTER(0x001D2610u);
    NEED(w_0011E398, 0x0011E398u);
    NEED(w_001B0070, 0x001B0070u);
    NEED(w_0021B970, 0x0021B970u);
    TRY(need(h, EM_FRH_D_00810700, 1, 0));
    uint32_t ctx;
    TRY(context(h, &ctx));
    TRY(need(h, ctx + EM_FRH_CTX_F8, 8, 0));

    uint32_t angle = em_ee_mul_bits(F_DEG, em_ee_add_bits(F_5, em_ee_mul_bits(F_45, em_ee_sub_bits(F_ONE, x))));
    SUB(em_frh_001D2590(h, F_224, angle));                             /* 001D2664 */
    uint32_t a, b, flags;
    uint8_t area = 0;
    TRY(context(h, &ctx));
    TRY(lw(h, ctx + EM_FRH_CTX_F8, &a));                               /* 001D2674 (call delay slot) */
    WORK(w_001B0070, 0x001B0070u, &flags);                             /* 001D2670 */
    TRY(context(h, &ctx));
    if (!(flags & 0x80)) TRY(lbu(h, EM_FRH_D_00810700, &area));        /* 001D267C, 001D2694 */
    if ((flags & 0x80) || area == 0x11) {
        TRY(lw(h, ctx + EM_FRH_CTX_FC, &b));
    } else {
        uint32_t fc;
        TRY(lw(h, ctx + EM_FRH_CTX_FC, &fc));
        if (em_ee_c_lt_bits(fc, F_450))                                /* 001D26C0 */
            b = em_ee_add_bits(fc, em_ee_mul_bits(x, em_ee_sub_bits(F_450, fc)));
        else
            b = em_ee_add_bits(fc, em_ee_mul_bits(F_200, x));
    }
    WORK(w_0021B970, 0x0021B970u, a, b);
    return 0;
}

/* ---- 001C1D00 ------------------------------------------------------------- */

/* 001C1D00(state): state 0 -> 1 and run; 1 -> run; else nothing. Run: in
 * area/sub 0x15/0x00 pick the 001E2260 tag by 001D2910(0x24), then
 * 001E0CF0() and 001D5370(). */
int em_frh_001C1D00(EmFrh *h, uint32_t state_address)
{
    ENTER(0x001C1D00u);
    NEED(w_001D2910, 0x001D2910u);
    NEED(w_001E2260, 0x001E2260u);
    NEED(w_001E0CF0, 0x001E0CF0u);
    NEED(w_001D5370, 0x001D5370u);
    TRY(need(h, EM_FRH_D_00810700, 2, 0));
    uint8_t st;
    TRY(lbu(h, state_address, &st));                                   /* 001C1D08 */
    if (st != 1) {
        if (st != 0) return 0;                                         /* 001C1D18 */
        TRY(store(h, state_address, 1, 1));                            /* 001C1D28 */
    }
    uint8_t area, sub;
    TRY(lbu(h, EM_FRH_D_00810700, &area));
    TRY(lbu(h, EM_FRH_D_00810701, &sub));
    if (((uint32_t)area << 8) + sub == 0x1500) {                       /* 001C1D48 */
        int32_t on;
        WORK(w_001D2910, 0x001D2910u, 0x24, &on);
        if (on != 0) WORK(w_001E2260, 0x001E2260u, UINT64_C(0x20076C0121323740));
        else WORK(w_001E2260, 0x001E2260u, UINT64_C(0x20076A8121323700));
    }
    WORK0(w_001E0CF0, 0x001E0CF0u);
    WORK0(w_001D5370, 0x001D5370u);
    return 0;
}

/* ---- lights ----------------------------------------------------------------- */

/* 001D8060(id): id == -1 -> 0; else the first of the 32 slots at context
 * +0x220 (stride 0x80) whose +0xC word equals id, or 0. */
int em_frh_001D8060(EmFrh *h, int32_t id, uint32_t *slot)
{
    ENTER(0x001D8060u);
    uint32_t ctx;
    TRY(context(h, &ctx));
    uint32_t p = ctx + EM_FRH_CTX_LIGHTS, found = 0;
    if (id != -1) {                                                    /* 001D8068 */
        for (unsigned i = 0; i < 0x20; ++i, p += 0x80u) {
            uint32_t word;
            TRY(lw(h, p + 0xCu, &word));
            if ((int32_t)word == id) { found = p; break; }            /* 001D8080 */
        }
    }
    if (slot) *slot = found;
    return 0;
}

/* 001D80B0(id): a found slot gets +0x2C = 0 and then +0xC = -1. */
int em_frh_001D80B0(EmFrh *h, int32_t id)
{
    ENTER(0x001D80B0u);
    uint32_t p;
    SUB(em_frh_001D8060(h, id, &p));
    if (p != 0) {
        TRY(need(h, p + 0xCu, 0x24, 1));
        TRY(sw(h, p + 0x2Cu, 0));
        TRY(sw(h, p + 0xCu, UINT32_C(0xFFFFFFFF)));
    }
    return 0;
}

/* ---- 001D8C30 ------------------------------------------------------------- */

static int zero_words(EmFrh *h, uint32_t base, const unsigned *index, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) TRY(sw(h, base + 4u * index[i], 0));
    return 0;
}

/* The clearing prologue of cases 0/1/default and 2: m column by column,
 * then out[0..11] each quadword high word first. */
static int clear_m_out(EmFrh *h, uint32_t m, uint32_t out)
{
    static const unsigned mi[16] = {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15};
    static const unsigned oi[12] = {3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8};
    TRY(zero_words(h, m, mi, 16));
    return zero_words(h, out, oi, 12);
}

/* 001D8C30(mode, m, out, in). t = max(in.w - 1, 0) is formed first from the
 * in.w read at entry. Cases (jump table for mode < 7 unsigned; else the
 * first case):
 *   0, 1, >= 7: clear; out[12..14] = bias + (128 + in.xyz); out[15] = bias + 64 t
 *   2:          clear; out[12..14] = bias + in.xyz;         out[15] = bias + 64 t
 *   3:          out[0..2] = bias + 128 in.xyz; out[3] = bias + 64 t
 *   4:          m = context V; out[0..2] = bias + 128 in.xyz; out[3] = 8388672
 *   5:          m = context V; out[0..2] as 4; out[3] = 0.2 * (128 in.w)
 *   6:          m = context V; out[0..2] as 4; out[3] = bias + 128 in.w
 * Each in lane is read just before the out word it feeds is stored. */
int em_frh_001D8C30(EmFrh *h, int32_t mode, uint32_t m, uint32_t out, uint32_t in)
{
    ENTER(0x001D8C30u);
    uint32_t w, x;
    TRY(lw(h, in + 0xCu, &w));                                         /* 001D8C44 */
    uint32_t t = em_ee_sub_bits(w, F_ONE);
    if (em_ee_c_le_bits(t, F_ZERO)) t = F_ZERO;                        /* 001D8C58 */
    uint32_t sel = (uint32_t)mode < 7u ? (uint32_t)mode : 0u;          /* 001D8C78 */
    switch (sel) {
    case 0:
    case 1:
    case 2: {
        TRY(need(h, m, 0x40, 1));
        TRY(need(h, out, 0x40, 1));
        TRY(clear_m_out(h, m, out));
        uint32_t t64 = em_ee_mul_bits(F_64, t);
        for (unsigned k = 0; k < 3; ++k) {
            TRY(lw(h, in + 4u * k, &x));
            uint32_t v = sel == 2 ? em_ee_add_bits(F_BIAS, x)
                                  : em_ee_add_bits(F_BIAS, em_ee_add_bits(F_128, x));
            TRY(sw(h, out + 0x30u + 4u * k, v));
        }
        return sw(h, out + 0x3Cu, em_ee_add_bits(F_BIAS, t64));
    }
    case 3: {
        TRY(need(h, out, 0x10, 1));
        uint32_t t64 = em_ee_mul_bits(F_64, t);
        for (unsigned k = 0; k < 3; ++k) {
            TRY(lw(h, in + 4u * k, &x));
            TRY(sw(h, out + 4u * k, em_ee_add_bits(F_BIAS, em_ee_mul_bits(F_128, x))));
        }
        return sw(h, out + 0xCu, em_ee_add_bits(F_BIAS, t64));
    }
    default: {                                                         /* 4, 5, 6 */
        uint32_t ctx;
        TRY(context(h, &ctx));
        TRY(need(h, ctx + EM_FRH_CTX_V, 0x40, 0));
        TRY(need(h, m & ~UINT32_C(15), 0x40, 1));
        TRY(need(h, out, 0x10, 1));
        TRY(copy_qw4(h, m, ctx + EM_FRH_CTX_V));
        for (unsigned k = 0; k < 3; ++k) {
            TRY(lw(h, in + 4u * k, &x));
            TRY(sw(h, out + 4u * k, em_ee_add_bits(F_BIAS, em_ee_mul_bits(F_128, x))));
        }
        if (sel == 4) return sw(h, out + 0xCu, F_BIAS64);
        TRY(lw(h, in + 0xCu, &x));
        if (sel == 5) return sw(h, out + 0xCu, em_ee_mul_bits(F_0_2, em_ee_mul_bits(F_128, x)));
        return sw(h, out + 0xCu, em_ee_add_bits(F_BIAS, em_ee_mul_bits(F_128, x)));
    }
    }
}

/* ---- 001D88B0 ------------------------------------------------------------- */

/* 001D88B0(a0, a1, a2, a3): the context +0x246C mode 1, 3, 4, 5 or 6 ->
 * 001D8C30(mode, a1, a2, a3); otherwise D_00275688 = &D_00817BC0,
 * 001D8130(0x20, a0), 001D8340(0, a1, a2, 0x20, a0), 001D8690(a1, a2, a3, 0x20). */
int em_frh_001D88B0(EmFrh *h, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    ENTER(0x001D88B0u);
    uint32_t ctx, mode;
    TRY(context(h, &ctx));
    TRY(lw(h, ctx + EM_FRH_CTX_LIGHT_MODE, &mode));
    if (mode == 1 || mode == 3 || mode == 4 || mode == 5 || mode == 6) {
        SUB(em_frh_001D8C30(h, (int32_t)mode, a1, a2, a3));
        return 0;
    }
    NEED(w_001D8130, 0x001D8130u);
    NEED(w_001D8340, 0x001D8340u);
    NEED(w_001D8690, 0x001D8690u);
    TRY(sw(h, EM_FRH_D_00275688, EM_FRH_D_00817BC0));
    WORK(w_001D8130, 0x001D8130u, 0x20, a0);
    WORK(w_001D8340, 0x001D8340u, 0, a1, a2, 0x20, a0);
    WORK(w_001D8690, 0x001D8690u, a1, a2, a3, 0x20);
    return 0;
}
