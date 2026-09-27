/* AREA01 render lane, module gs: 001CD070, 001CD180, 001CD2B0, 001F4A10,
 * 001F4CC0. See em_area01_render_gs.h and docs/AREA01_RENDER.md. */
#include "game/em_area01_render_gs.h"

typedef EmArea01RenderGs S;
typedef uint32_t u32;

#define F_HALF 0x3F000000u    /* 0.5 */
#define F_128 0x43000000u     /* 128.0 */
#define F_2POW31 0x4F000000u  /* 2147483648.0 */
#define F_32 0x42000000u      /* 32.0 */
#define F_KEYNUM 0x4D7FFFFFu  /* 268435440.0: 001F4A10's key numerator */
#define F_1E5 0x47C35000u     /* 100000.0 */
#define F_1E6 0x49742400u     /* 1000000.0 */

enum { W_281C0 = 1, W_22BB8 = 2, W_C7900 = 4, W_C6120 = 8, W_D3990 = 16, W_CB760 = 32, W_1B9A0 = 64,
       W_F4BF0 = 128 };

static int need(S *s, u32 fn, int mask)
{
    s->core.function = fn;
    if (em_a01r_latched(&s->core)) return -1;
    const struct { int bit; const void *w; u32 address; } list[] = {
        {W_281C0, (const void *)s->workers.w_001281C0, 0x001281C0u},
        {W_22BB8, (const void *)s->workers.w_00122BB8, 0x00122BB8u},
        {W_C7900, (const void *)s->workers.w_001C7900, 0x001C7900u},
        {W_C6120, (const void *)s->workers.w_001C6120, 0x001C6120u},
        {W_D3990, (const void *)s->workers.w_001D3990, 0x001D3990u},
        {W_CB760, (const void *)s->workers.w_001CB760, 0x001CB760u},
        {W_1B9A0, (const void *)s->workers.w_0021B9A0, 0x0021B9A0u},
        {W_F4BF0, (const void *)s->workers.w_001F4BF0, 0x001F4BF0u},
    };
    for (unsigned i = 0; i < sizeof list / sizeof list[0]; ++i)
        if ((mask & list[i].bit) && !list[i].w)
            return em_a01r_fault(&s->core, list[i].address, EM_A01R_FAULT_NULL_WORKER, 0);
    return 0;
}

#define CALLW(address, expr) \
    do { if ((expr) < 0) return em_a01r_fault(&s->core, (address), EM_A01R_FAULT_WORKER_FAILED, 0); } while (0)
#define TRY EM_A01R_TRY

/* ---- 001CD070 ---------------------------------------------------------- */

int em_area01_render_001CD070(S *s, u32 p, u32 mask, u32 *v0)
{
    EmArea01RenderCore *c = &s->core;
    u32 address, m[4][4], v[4], out[4], flags;
    TRY(need(s, 0x001CD070u, W_281C0));
    TRY(em_a01r_001CD370(c, 0, &address));
    TRY(em_a01r_ldm(c, address, m));
    TRY(em_a01r_ldq(c, p, v));
    TRY(em_a01r_transform(c, m, v, out));
    TRY(em_a01r_clipw(c, out, &flags));
    if (mask & (flags & 0x3Fu)) {
        if (v0) *v0 = 0xFFFFFFu;
        return 0;
    }
    u32 ctx, ca0[4], k[4][4], clip[4], screen[4], word;
    TRY(em_a01r_ld32(c, EM_A01R_D_00275670, &ctx));
    TRY(em_a01r_ldq(c, ctx + 0xA0u, ca0));
    TRY(em_a01r_ldm(c, EM_A01R_SPAD_3AC0, k));
    TRY(em_a01r_project(c, k, ca0, v, clip, screen));
    TRY(em_a01r_stq(c, EM_A01R_SPAD_3600, screen));
    int32_t wint = 0;
    CALLW(0x001281C0u, s->workers.w_001281C0(s->workers.ctx, clip[3], &wint));
    TRY(em_a01r_st32(c, EM_A01R_D_00275C04, (u32)wint));
    TRY(em_a01r_ld32(c, EM_A01R_SPAD_3600 + 8u, &word));
    if (v0) *v0 = word;
    return 0;
}

/* ---- 001CD180 ---------------------------------------------------------- */

static int body_001CD180(S *s, u32 f12, u32 f13, u32 *v0)
{
    EmArea01RenderCore *c = &s->core;
    u32 wint, v[4], m[4][4], out[4], q;
    c->function = 0x001CD180u;
    const u32 half_w = em_ee_mul_bits(F_HALF, f12);
    TRY(em_a01r_ld32(c, EM_A01R_D_00275C04, &wint));
    const u32 half_h = em_ee_mul_bits(F_HALF, f13);
    TRY(em_a01r_st32(c, EM_A01R_SPAD_3610, half_w));
    TRY(em_a01r_st32(c, EM_A01R_SPAD_3610 + 4u, half_h));
    TRY(em_a01r_st32(c, EM_A01R_SPAD_3610 + 8u, em_ee_cvt_s_w_bits(wint)));
    TRY(em_a01r_ldq(c, EM_A01R_SPAD_3610, v));
    TRY(em_a01r_ldm(c, EM_A01R_SPAD_3A40, m));
    if (em_vu_vec_bits(EM_VU_SUB, 12, EM_VU_NO_BC, m[2], m[2], 0, NULL, m[2]) != EM_EE_FLOAT_OK)
        return em_a01r_fault(c, c->function, EM_A01R_FAULT_UNMEASURED, 0);
    TRY(em_a01r_transform(c, m, v, out));
    if (em_vu_div_bits(EM_EE_ONE, out[3], 3, 3, &q) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MULQ, 12, EM_VU_NO_BC, out, NULL, q, NULL, out) != EM_EE_FLOAT_OK)
        return em_a01r_fault(c, c->function, EM_A01R_FAULT_UNMEASURED, 0);
    out[0] = em_vu_ftoi4_bits(out[0]);
    out[1] = em_vu_ftoi4_bits(out[1]);
    TRY(em_a01r_stq(c, EM_A01R_SPAD_3610, out));

    u32 cx, cy, hw, hh, r;
    TRY(em_a01r_ld32(c, EM_A01R_SPAD_3600, &cx));
    TRY(em_a01r_ld32(c, EM_A01R_SPAD_3610, &hw));
    *v0 = 0;
    if (!((int32_t)(cx - hw) < 0x9001)) return 0;
    TRY(em_a01r_ld32(c, EM_A01R_SPAD_3600 + 4u, &cy));
    TRY(em_a01r_ld32(c, EM_A01R_SPAD_3610 + 4u, &hh));
    if (!((int32_t)(cy - hh) < 0x8701)) return 0;
    if ((int32_t)(cx + hw) < 0x7000) return 0;
    if ((int32_t)(cy + hh) < 0x7900) return 0;
    if ((int32_t)hw < (int32_t)hh) TRY(em_a01r_st32(c, EM_A01R_SPAD_3610, hh));
    TRY(em_a01r_ld32(c, EM_A01R_SPAD_3610, &r));
    const int32_t sr = (int32_t)r;
    *v0 = (u32)((sr < 0 ? ~(~sr >> 3) : sr >> 3) + 1);
    return 0;
}

int em_area01_render_001CD180(S *s, u32 f12, u32 f13, u32 *v0)
{
    u32 r = 0;
    TRY(need(s, 0x001CD180u, 0));
    TRY(body_001CD180(s, f12, f13, &r));
    if (v0) *v0 = r;
    return 0;
}

/* ---- 001CD2B0 ---------------------------------------------------------- */

int em_area01_render_001CD2B0(S *s, u32 f12, u32 f13, u32 f14, u32 f15, u32 *f0)
{
    EmArea01RenderCore *c = &s->core;
    u32 n, out;
    TRY(need(s, 0x001CD2B0u, 0));
    TRY(em_a01r_st32(c, EM_A01R_SPAD_3680, 0u));
    TRY(body_001CD180(s, f12, f13, &n));
    c->function = 0x001CD2B0u;
    TRY(em_a01r_st32(c, EM_A01R_D_00275C00, n));
    TRY(em_a01r_ld32(c, EM_A01R_D_00275C00, &n));
    if (n != 0) {
        const u32 t = em_ee_sub_bits(em_ee_cvt_s_w_bits(n), f14);
        TRY(em_a01r_st32(c, EM_A01R_SPAD_3680, t));
        u32 v = 0;                                  /* t < 0 keeps +0.0 */
        if (!em_ee_c_lt_bits(t, 0)) v = em_ee_div_bits(t, f15);
        if (!em_ee_c_le_bits(v, EM_EE_ONE)) v = EM_EE_ONE;
        TRY(em_a01r_st32(c, EM_A01R_SPAD_3680, em_ee_sub_bits(EM_EE_ONE, v)));
    }
    TRY(em_a01r_ld32(c, EM_A01R_SPAD_3680, &out));
    if (f0) *f0 = out;
    return 0;
}

/* ---- 001F4A10 ---------------------------------------------------------- */

int em_area01_render_001F4A10(S *s, u32 obj, u32 v)
{
    EmArea01RenderCore *c = &s->core;
    u32 w3, ctx, packet, x, y, z;
    TRY(need(s, 0x001F4A10u, W_281C0 | W_22BB8 | W_C7900 | W_C6120 | W_D3990 | W_CB760));
    TRY(em_a01r_ld32(c, v + 0xCu, &w3));
    TRY(em_a01r_ld32(c, EM_A01R_D_00275670, &ctx));
    TRY(em_a01r_ld32(c, ctx + EM_A01R_CTX_CURSOR3, &packet));
    const u32 base = em_ee_div_bits(em_ee_cvt_s_w_bits(w3), F_128);
    int32_t r = 0;
    CALLW(0x00122BB8u, s->workers.w_00122BB8(s->workers.ctx, &r));
    u32 f2 = em_ee_div_bits(em_ee_cvt_s_w_bits((u32)r), F_2POW31);
    f2 = em_ee_mul_bits(f2, base);
    f2 = em_ee_add_bits(base, f2);
    TRY(em_a01r_ld32(c, v, &x));
    f2 = em_ee_div_bits(f2, F_32);
    u32 dir[4];
    dir[0] = em_ee_mul_bits(em_ee_cvt_s_w_bits(x), f2);
    TRY(em_a01r_ld32(c, v + 4u, &y));
    dir[1] = em_ee_mul_bits(em_ee_cvt_s_w_bits(y), f2);
    TRY(em_a01r_ld32(c, v + 8u, &z));
    dir[2] = em_ee_mul_bits(em_ee_cvt_s_w_bits(z), f2);
    dir[3] = 0;
    CALLW(0x001C7900u, s->workers.w_001C7900(s->workers.ctx, obj, dir, 0x3F5, 3));

    u32 table, entry = 0;
    TRY(em_a01r_ld32(c, EM_A01R_D_0028A56C, &table));
    CALLW(0x001C6120u, s->workers.w_001C6120(s->workers.ctx, table, 0xC, &entry));
    CALLW(0x001D3990u, s->workers.w_001D3990(s->workers.ctx, entry));

    TRY(em_a01r_ld32(c, EM_A01R_D_00275670, &ctx));
    TRY(em_a01r_tag3(c, ctx, 0x60u, 0u, 0u));

    u32 subject, p[4], ca0[4], k[4][4], clip[4], screen[4];
    TRY(em_a01r_ld32(c, EM_A01R_D_00275B44, &subject));
    TRY(em_a01r_ldq(c, subject + 0xB0u, p));
    TRY(em_a01r_ld32(c, EM_A01R_D_00275670, &ctx));
    TRY(em_a01r_ldq(c, ctx + 0xA0u, ca0));
    TRY(em_a01r_ldm(c, EM_A01R_SPAD_3AC0, k));
    TRY(em_a01r_project(c, k, ca0, p, clip, screen));
    const u32 f12 = em_ee_div_bits(F_KEYNUM, em_ee_cvt_s_w_bits(screen[3]));
    int32_t key = 0;
    CALLW(0x001281C0u, s->workers.w_001281C0(s->workers.ctx, f12, &key));
    CALLW(0x001CB760u, s->workers.w_001CB760(s->workers.ctx, EM_A01R_D_007635C0, key, packet));
    return 0;
}

/* ---- 001F4CC0 ---------------------------------------------------------- */

int em_area01_render_001F4CC0(S *s, u32 a0, u32 a1)
{
    TRY(need(s, 0x001F4CC0u, W_1B9A0 | W_F4BF0));
    CALLW(0x0021B9A0u, s->workers.w_0021B9A0(s->workers.ctx, 2, 0u, F_1E5));
    CALLW(0x0021B9A0u, s->workers.w_0021B9A0(s->workers.ctx, 3, 0u, F_1E6));
    CALLW(0x001F4BF0u, s->workers.w_001F4BF0(s->workers.ctx, a0, a1));
    CALLW(0x0021B9A0u, s->workers.w_0021B9A0(s->workers.ctx, 1, 0u, 0u));
    return 0;
}
