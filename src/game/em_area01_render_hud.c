/* AREA01 render lane, module hud: 001E8B90, 001E9E60 and the effect
 * handlers 001EAF00, 001EAF80, 001EB020, 001EB7F0, 001EC270. See
 * em_area01_render_hud.h and docs/AREA01_RENDER.md. */
#include "game/em_area01_render_hud.h"

typedef EmArea01RenderHud S;
typedef uint32_t u32;
typedef uint64_t u64;

#define F_NEG7 0xC0E00000u   /* -7.0 */
#define F_NEG5 0xC0A00000u   /* -5.0 */
#define F_NEG3 0xC0400000u   /* -3.0 */
#define F_11 0x41300000u     /* 11.0 */
#define F_32 0x42000000u     /* 32.0 */
#define F_60 0x42700000u     /* 60.0 */
#define F_5 0x40A00000u      /* 5.0 */
#define F_3 0x40400000u      /* 3.0 */
#define F_9 0x41100000u      /* 9.0 */
#define F_1EM6 0x358637BDu   /* 1e-6 */
#define F_1EM4 0x38D1B717u   /* 1e-4 */
#define F_65535 0x477FFF00u  /* 65535.0 */

#define D_0026E9B0 0x0026E9B0u
#define D_008105D0 0x008105D0u
#define D_002345E0 0x002345E0u
#define GIF_TOP 0x01000404u

enum { W_281C0 = 1, W_CB5F0 = 2, W_CB950 = 4, W_CB6B0 = 8, W_CB760 = 16, W_CFB50 = 32, W_CFBE0 = 64 };

static int need(S *s, u32 fn, int mask)
{
    s->core.function = fn;
    if (em_a01r_latched(&s->core)) return -1;
    const struct { int bit; const void *w; u32 address; } list[] = {
        {W_281C0, (const void *)s->workers.w_001281C0, 0x001281C0u},
        {W_CB5F0, (const void *)s->workers.w_001CB5F0, 0x001CB5F0u},
        {W_CB950, (const void *)s->workers.w_001CB950, 0x001CB950u},
        {W_CB6B0, (const void *)s->workers.w_001CB6B0, 0x001CB6B0u},
        {W_CB760, (const void *)s->workers.w_001CB760, 0x001CB760u},
        {W_CFB50, (const void *)s->workers.w_001CFB50, 0x001CFB50u},
        {W_CFBE0, (const void *)s->workers.w_001CFBE0, 0x001CFBE0u},
    };
    for (unsigned i = 0; i < sizeof list / sizeof list[0]; ++i)
        if ((mask & list[i].bit) && !list[i].w)
            return em_a01r_fault(&s->core, list[i].address, EM_A01R_FAULT_NULL_WORKER, 0);
    return 0;
}

#define CALLW(address, expr) \
    do { if ((expr) < 0) return em_a01r_fault(&s->core, (address), EM_A01R_FAULT_WORKER_FAILED, 0); } while (0)
#define TRY EM_A01R_TRY

static int32_t clamp31(int32_t i) { return i < 0 ? 0 : (i < 0x20 ? i : 0x1F); }

/* ---- 001E8B90 ---------------------------------------------------------- */

int em_area01_render_001E8B90(S *s, u32 p, u32 f12)
{
    EmArea01RenderCore *c = &s->core;
    u32 area;
    TRY(need(s, 0x001E8B90u, W_281C0));
    TRY(em_a01r_ld8(c, EM_A01R_D_00810700, &area));
    if (area == 0x15u || area == 0x10u) return 0;
    const u32 w_centre = em_ee_mul_bits(F_NEG7, f12);
    u32 px, py, pz;
    TRY(em_a01r_ld32(c, p, &px));
    TRY(em_a01r_ld32(c, p + 4u, &py));
    TRY(em_a01r_ld32(c, p + 8u, &pz));
    const u32 w_edge = em_ee_mul_bits(F_NEG5, f12);
    const u32 w_corner = em_ee_mul_bits(F_NEG3, f12);

    u32 offset = 0;
    for (int n = 0; n < 4; ++n, offset += 0xA060u) {
        u32 grid, active, x0, w, z0, d, y0;
        TRY(em_a01r_ld32(c, EM_A01R_D_00275C20, &grid));
        const u32 rec = grid + offset;
        TRY(em_a01r_ld32(c, rec + 0x54u, &active));
        if (active == 0) continue;
        TRY(em_a01r_ld32(c, rec, &x0));
        if (em_ee_c_le_bits(px, x0)) continue;
        TRY(em_a01r_ld32(c, rec + 0x30u, &w));
        if (!em_ee_c_le_bits(px, em_ee_add_bits(x0, w))) continue;
        TRY(em_a01r_ld32(c, rec + 8u, &z0));
        if (em_ee_c_le_bits(pz, z0)) continue;
        TRY(em_a01r_ld32(c, rec + 0x34u, &d));
        if (!em_ee_c_le_bits(pz, em_ee_add_bits(z0, d))) continue;
        TRY(em_a01r_ld32(c, rec + 4u, &y0));
        if (!em_ee_c_lt_bits(py, em_ee_add_bits(F_11, y0))) continue;

        const u32 dx = em_ee_mul_bits(F_32, em_ee_sub_bits(px, x0));
        const u32 dz = em_ee_mul_bits(F_32, em_ee_sub_bits(pz, z0));
        pz = em_ee_div_bits(dz, d);
        px = em_ee_div_bits(dx, w);
        int32_t row = 0, col = 0;
        CALLW(0x001281C0u, s->workers.w_001281C0(s->workers.ctx, pz, &row));
        CALLW(0x001281C0u, s->workers.w_001281C0(s->workers.ctx, px, &col));
        c->function = 0x001E8B90u;
        row = clamp31(row);
        col = clamp31(col);
        for (int dc = -1; dc < 2; ++dc) {
            const u32 column = rec + ((u32)clamp31(col + dc) << 2);
            for (int dr = -1; dr < 2; ++dr) {
                const u32 cell = column + ((u32)clamp31(row + dr) << 7) + 0x9060u;
                const u32 add = (dr == 0 && dc == 0) ? w_centre : (dr != 0 && dc != 0) ? w_corner : w_edge;
                u32 value;
                TRY(em_a01r_ld32(c, cell, &value));
                TRY(em_a01r_st32(c, cell, em_ee_add_bits(value, add)));
            }
        }
    }
    return 0;
}

/* ---- 001E9E60 ---------------------------------------------------------- */

static int header(EmArea01RenderCore *c, u32 blk, u32 tag)
{
    TRY(em_a01r_st32(c, blk, 0u));
    TRY(em_a01r_st32(c, blk + 4u, 0u));
    TRY(em_a01r_st32(c, blk + 8u, GIF_TOP));
    return em_a01r_st32(c, blk + 0xCu, tag);
}

static int packet(S *s, int32_t qwc, u32 *blk)
{
    *blk = 0;
    CALLW(0x001CB5F0u, s->workers.w_001CB5F0(s->workers.ctx, EM_A01R_D_007635C0, 0, qwc, blk));
    s->core.function = 0x001E9E60u;
    return 0;
}

static int copy_word(EmArea01RenderCore *c, u32 dst, u32 src)
{
    u32 v;
    TRY(em_a01r_ld32(c, src, &v));
    return em_a01r_st32(c, dst, v);
}

int em_area01_render_001E9E60(S *s, u32 a0, int32_t a1)
{
    EmArea01RenderCore *c = &s->core;
    u32 table, blk;
    TRY(need(s, 0x001E9E60u, W_CB5F0 | W_CB950 | W_CB6B0 | W_CB760));
    TRY(em_a01r_ld32(c, EM_A01R_D_00275C1C, &table));
    const u32 orig = table + (u32)((int64_t)a1 * 0xA060);
    for (u32 seg = 0; seg < 6; ++seg) {
        TRY(packet(s, 0x1A, &blk));
        TRY(header(c, blk, 0x6C188000u));
        const u32 s0 = orig + (seg << 9), s1 = orig + ((seg + 1u) << 9), s2 = orig + ((seg + 2u) << 9);
        for (u32 row = 0; row < 8; ++row) {
            TRY(em_a01r_00102948(c, blk + 0x10u + 16u * row, s0 + 0x60u + 16u * row));
            TRY(em_a01r_00102948(c, blk + 0x90u + 16u * row, s1 + 0x60u + 16u * row));
            TRY(em_a01r_00102948(c, blk + 0x110u + 16u * row, s2 + 0x60u + 16u * row));
        }
        TRY(em_a01r_st32(c, blk + 0x190u, seg == 5 ? 0x14000000u : 0x17000000u));
        TRY(em_a01r_st32(c, blk + 0x194u, 0u));
        TRY(em_a01r_st32(c, blk + 0x198u, 0u));
        TRY(em_a01r_st32(c, blk + 0x19Cu, 0u));
    }

    TRY(packet(s, 5, &blk));
    TRY(header(c, blk, 0x6C040000u));
    TRY(em_a01r_00102958(c, blk + 0x10u, 0x70003AC0u));

    TRY(packet(s, 9, &blk));
    TRY(header(c, blk, 0x6C0803F8u));
    u32 f, t, b0, b1, b2, o0, o1, o2, ow, aw, acc, blend[4];
    TRY(copy_word(c, blk + 0x10u, orig + 0x48u));
    TRY(copy_word(c, blk + 0x14u, orig + 0x4Cu));
    TRY(copy_word(c, blk + 0x18u, orig + 0x50u));
    TRY(em_a01r_ld32(c, a0 + 0xB4u, &f));
    TRY(em_a01r_st32(c, blk + 0x1Cu, em_ee_add_bits(F_60, f)));
    /* blend[i] = orig[i] * (1 - t) + D_0026E9B0[i] * t (MULA then MADD) */
    TRY(em_a01r_ld32(c, a0 + 0x80u, &t));
    TRY(em_a01r_ld32(c, orig + 0x10u, &o0));
    TRY(em_a01r_ld32(c, D_0026E9B0, &b0));
    const u32 rest = em_ee_sub_bits(EM_EE_ONE, t);
    TRY(em_a01r_ld32(c, D_0026E9B0 + 4u, &b1));
    acc = em_ee_mula_bits(o0, rest);
    blend[0] = em_ee_madd_bits(acc, b0, t);
    TRY(em_a01r_ld32(c, orig + 0x14u, &o1));
    TRY(em_a01r_ld32(c, D_0026E9B0 + 8u, &b2));
    acc = em_ee_mula_bits(o1, rest);
    blend[1] = em_ee_madd_bits(acc, b1, t);
    TRY(em_a01r_ld32(c, orig + 0x18u, &o2));
    acc = em_ee_mula_bits(o2, rest);
    blend[2] = em_ee_madd_bits(acc, b2, t);
    TRY(em_a01r_ld32(c, a0 + 0x8Cu, &aw));
    TRY(em_a01r_ld32(c, orig + 0x1Cu, &ow));
    blend[3] = em_ee_mul_bits(ow, aw);
    TRY(em_a01r_stq(c, blk + 0x20u, blend));
    TRY(copy_word(c, blk + 0x30u, orig + 0x3Cu));
    TRY(copy_word(c, blk + 0x34u, orig + 0x38u));
    TRY(copy_word(c, blk + 0x38u, orig + 0x40u));
    TRY(copy_word(c, blk + 0x3Cu, orig + 0x44u));
    TRY(em_a01r_00102948(c, blk + 0x40u, D_008105D0));
    TRY(em_a01r_st64(c, blk + 0x50u, ((u64)0x303E4000u << 32) | 0x8010u));
    TRY(em_a01r_st64(c, blk + 0x58u, 0x412u));
    u32 ctx, byte, top;
    TRY(em_a01r_ld32(c, EM_A01R_D_00275670, &ctx));
    TRY(em_a01r_00102948(c, blk + 0x60u, ctx + 0xA0u));
    TRY(em_a01r_ld32(c, EM_A01R_D_00275670, &ctx));
    TRY(em_a01r_00102948(c, blk + 0x70u, ctx + 0x2220u));
    TRY(em_a01r_ld32(c, EM_A01R_D_00275670, &ctx));
    TRY(em_a01r_00102948(c, blk + 0x80u, ctx + 0x2230u));
    TRY(em_a01r_ld8(c, orig + 0x5Cu, &byte));
    const u64 word = byte != 1u ? ((u64)0x20048CC1u << 32) | 0x55422242u : ((u64)0x20048E41u << 32) | 0x55422256u;
    CALLW(0x001CB950u, s->workers.w_001CB950(s->workers.ctx, EM_A01R_D_007635C0, 0, word));
    c->function = 0x001E9E60u;
    TRY(em_a01r_ld32(c, EM_A01R_D_00275674, &top));
    CALLW(0x001CB6B0u, s->workers.w_001CB6B0(s->workers.ctx, EM_A01R_D_007635C0, 0, 8, top + 0x720u));
    CALLW(0x001CB760u, s->workers.w_001CB760(s->workers.ctx, EM_A01R_D_007635C0, 0, D_002345E0));
    return 0;
}

/* ---- the effect handlers ------------------------------------------------ */

/* 001CFB50(D_0081F8F0, 0, a0, f12, f13, 1.0, 1e-6, f16). */
static int transform(S *s, u32 a0, u32 f12, u32 f13, u32 f16)
{
    CALLW(0x001CFB50u, s->workers.w_001CFB50(s->workers.ctx, EM_A01R_D_0081F8F0, 0, a0, f12, f13, EM_EE_ONE,
                                             F_1EM6, f16));
    return 0;
}

static int emit(S *s, u32 a1, int32_t kind, u32 table)
{
    CALLW(0x001CFBE0u, s->workers.w_001CFBE0(s->workers.ctx, a1, kind, table, EM_A01R_D_0081F8F0, 0));
    return 0;
}

/* f12 = work +0x54 and f13 = work +0x5C (001EAF00 / 001EAF80). */
static int fixed_round(S *s, u32 a0, u32 f16)
{
    EmArea01RenderCore *c = &s->core;
    u32 work, f12, f13;
    TRY(em_a01r_ld32(c, EM_A01R_D_00275C34, &work));
    TRY(em_a01r_ld32(c, work + 0x54u, &f12));
    TRY(em_a01r_ld32(c, work + 0x5Cu, &f13));
    return transform(s, a0, f12, f13, f16);
}

/* One random round of 001EB020 / 001EC270: f13 from the work block's +4
 * word, which advances by x * 37 + 11. */
static int random_round(S *s, u32 a0, u32 f16)
{
    EmArea01RenderCore *c = &s->core;
    u32 work, r, f12;
    TRY(em_a01r_ld32(c, EM_A01R_D_00275C34, &work));
    TRY(em_a01r_ld32(c, work + 4u, &r));
    u32 f13 = em_ee_div_bits(em_ee_cvt_s_w_bits((r >> 16) & 0xFFFFu), F_65535);
    TRY(em_a01r_st32(c, work + 4u, r * 37u + 11u));
    TRY(em_a01r_ld32(c, EM_A01R_D_00275C34, &work));
    TRY(em_a01r_ld32(c, work + 0x54u, &f12));
    f13 = em_ee_add_bits(f13, F_1EM4);
    return transform(s, a0, f12, f13, f16);
}

int em_area01_render_001EAF00(S *s, u32 a0, u32 a1)
{
    TRY(need(s, 0x001EAF00u, W_CFB50 | W_CFBE0));
    TRY(fixed_round(s, a0, F_5));
    return emit(s, a1, 1, 0x002557D0u);
}

int em_area01_render_001EAF80(S *s, u32 a0, u32 a1)
{
    TRY(need(s, 0x001EAF80u, W_CFB50 | W_CFBE0));
    TRY(fixed_round(s, a0, F_9));
    TRY(emit(s, a1, 1, 0x00255860u));
    return emit(s, a1, 1, 0x002558F0u);
}

int em_area01_render_001EB020(S *s, u32 a0, u32 a1)
{
    TRY(need(s, 0x001EB020u, W_CFB50 | W_CFBE0));
    TRY(random_round(s, a0, F_9));
    TRY(emit(s, a1, 1, 0x00255980u));
    TRY(random_round(s, a0, F_9));
    TRY(emit(s, a1, 1, 0x00255A10u));
    TRY(random_round(s, a0, F_9));
    return emit(s, a1, 1, 0x00255AA0u);
}

int em_area01_render_001EC270(S *s, u32 a0, u32 a1)
{
    TRY(need(s, 0x001EC270u, W_CFB50 | W_CFBE0));
    TRY(random_round(s, a0, F_5));
    TRY(emit(s, a1, 1, 0x00256790u));
    TRY(random_round(s, a0, F_5));
    return emit(s, a1, 0, 0x00256820u);
}

/* Water bullet impact: two independently advanced random fractions and
 * the original pair of sprite sources. Original func_001EB7F0.c. */
int em_area01_render_001EB7F0(S *s, u32 a0, u32 a1)
{
    TRY(need(s, 0x001EB7F0u, W_CFB50 | W_CFBE0));
    TRY(random_round(s, a0, F_3));
    TRY(emit(s, a1, 1, 0x00255E90u));
    TRY(random_round(s, a0, F_3));
    return emit(s, a1, 1, 0x00255F20u);
}
