/* AREA01 render lane, module vif: 001D5BD0, 001D5A70, 001D4FC0, 001D5170.
 * See em_area01_render_vif.h and docs/AREA01_RENDER.md. Each routine follows
 * the original's order of loads, stores and calls, including its re-reads of
 * D_00275670 and of the channel-3 cursor (context +0x1C) around every store.
 * docs/AREA01_RENDER.md section 3 lists how the reference test observes
 * them: the cursor aliased onto its own word (context +0x18, +0x1C, +0x19,
 * relocated contexts) and stores landing on D_00275670 before each of its
 * re-reads. The two re-reads no completing execution can observe (the inline
 * tag's before its qwc store and before its cursor store) are argued there. */
#include "game/em_area01_render_vif.h"

typedef EmArea01RenderVif S;
typedef uint32_t u32;

#define CTX_CURSOR3 EM_A01R_CTX_CURSOR3 /* channel-3 packet cursor */
#define CTX_SLOT 0x9Cu    /* index of the 0x80-byte slot at D_00816A40 */

static int need(S *s, u32 fn, int mask)
{
    s->core.function = fn;
    if (em_a01r_latched(&s->core)) return -1;
    const struct { int bit; const void *w; u32 address; } list[] = {
        {1, (const void *)s->workers.w_00121870, 0x00121870u},
        {2, (const void *)s->workers.w_001D2090, 0x001D2090u},
        {4, (const void *)s->workers.w_001D4750, 0x001D4750u},
        {8, (const void *)s->workers.w_001D1F80, 0x001D1F80u},
        {16, (const void *)s->workers.w_001CAAC0, 0x001CAAC0u},
    };
    for (unsigned i = 0; i < sizeof list / sizeof list[0]; ++i)
        if ((mask & list[i].bit) && !list[i].w)
            return em_a01r_fault(&s->core, list[i].address, EM_A01R_FAULT_NULL_WORKER, 0);
    return 0;
}

#define CALLW(address, expr) \
    do { if ((expr) < 0) return em_a01r_fault(&s->core, (address), EM_A01R_FAULT_WORKER_FAILED, 0); } while (0)
#define TRY EM_A01R_TRY

/* The common tail of 001D4FC0 and 001D5170, from the 0x30 slot tag on.
 * first is the first tag's address. */
static int chain_tail(S *s, u32 rec, u32 first)
{
    EmArea01RenderCore *c = &s->core;
    u32 ctx, index, word;

    TRY(em_a01r_ld32(c, EM_A01R_D_00275670, &ctx));
    TRY(em_a01r_ld32(c, ctx + CTX_SLOT, &index));
    const u32 slot = EM_A01R_D_00816A40 + (index << 7);
    TRY(em_a01r_tag3(c, ctx, 0x30u, slot, 8u));
    CALLW(0x001D1F80u, s->workers.w_001D1F80(s->workers.ctx, 3, 2, 1, slot));

    TRY(em_a01r_ld32(c, rec, &word));
    int32_t n = (int32_t)word;
    u32 run = rec + 0x40u;
    while (n > 0) {
        const int32_t chunk = n < 0x1F8 ? n : 0x1F8;
        n -= 0x1F8;
        TRY(em_a01r_ld32(c, EM_A01R_D_00275670, &ctx));
        const u32 units = (u32)chunk * 65u;
        TRY(em_a01r_tag3(c, ctx, 0x30u, run, units * 2u));
        run += units << 5;
    }

    TRY(em_a01r_ld32(c, EM_A01R_D_00275670, &ctx));
    TRY(em_a01r_tag3(c, ctx, 0x60u, 0u, 0u));
    u32 v[4];
    TRY(em_a01r_ld32(c, rec + 0x34u, &v[0]));
    TRY(em_a01r_ld32(c, rec + 0x38u, &v[1]));
    TRY(em_a01r_ld32(c, rec + 0x3Cu, &v[2]));
    v[3] = EM_EE_ONE;
    CALLW(0x001CAAC0u, s->workers.w_001CAAC0(s->workers.ctx, v, first, 0x60, ctx));
    return 0;
}

static int body_001D4FC0(S *s, u32 rec)
{
    EmArea01RenderCore *c = &s->core;
    const u32 fn = 0x001D4FC0u;
    u32 ctx, first, cur;
    c->function = fn;
    TRY(em_a01r_ld32(c, EM_A01R_D_00275670, &ctx));
    TRY(em_a01r_ld32(c, ctx + CTX_CURSOR3, &first));
    TRY(em_a01r_st8(c, first + 3u, 0x10u));
    TRY(em_a01r_ld32(c, ctx + CTX_CURSOR3, &cur));
    TRY(em_a01r_st32(c, cur + 4u, 0u));
    TRY(em_a01r_ld32(c, ctx + CTX_CURSOR3, &cur));
    TRY(em_a01r_st16(c, cur, 0xEu));
    TRY(em_a01r_ld32(c, ctx + CTX_CURSOR3, &cur));
    TRY(em_a01r_st32(c, ctx + CTX_CURSOR3, cur + 0xF0u));
    CALLW(0x00121870u, s->workers.w_00121870(s->workers.ctx, cur + 0x10u, EM_A01R_D_002513D0, 0xE0, ctx));
    CALLW(0x001D2090u, s->workers.w_001D2090(s->workers.ctx, 3, EM_A01R_D_00237450));
    c->function = fn;
    return chain_tail(s, rec, first);
}

static int body_001D5170(S *s, u32 rec)
{
    EmArea01RenderCore *c = &s->core;
    const u32 fn = 0x001D5170u;
    u32 ctx, first;
    c->function = fn;
    TRY(em_a01r_ld32(c, EM_A01R_D_00275670, &ctx));
    TRY(em_a01r_ld32(c, ctx + CTX_CURSOR3, &first));
    CALLW(0x001D4750u, s->workers.w_001D4750(s->workers.ctx, 3));
    CALLW(0x001D2090u, s->workers.w_001D2090(s->workers.ctx, 3, EM_A01R_D_00237720));
    return chain_tail(s, rec, first);
}

/* One of the three points: the quadword at p with w = 1.0, through m. */
static int point_flags(S *s, const u32 m[4][4], u32 p, u32 *flags)
{
    u32 v[4], out[4];
    TRY(em_a01r_ldq(&s->core, p, v));
    v[3] = EM_EE_ONE;
    TRY(em_a01r_transform(&s->core, m, v, out));
    TRY(em_a01r_clipw(&s->core, out, flags));
    *flags &= 0x3Fu;
    return 0;
}

static int body_001D5A70(S *s, u32 a1, u32 *result)
{
    u32 m[4][4], address, c1, c2, c3;
    s->core.function = 0x001D5A70u;
    TRY(em_a01r_001CD370(&s->core, 0, &address));
    TRY(em_a01r_ldm(&s->core, address, m));
    TRY(point_flags(s, m, a1 + 0x40u, &c1));
    TRY(point_flags(s, m, a1 + 0x80u, &c2));
    TRY(point_flags(s, m, a1 + 0xC0u, &c3));
    if (c3 & (c1 & c2)) *result = 0xFFu;
    else *result = (c3 | (c1 | c2)) ? 1u : 0u;
    return 0;
}

int em_area01_render_001D4FC0(S *s, u32 rec)
{
    TRY(need(s, 0x001D4FC0u, 1 | 2 | 8 | 16));
    return body_001D4FC0(s, rec);
}

int em_area01_render_001D5170(S *s, u32 rec)
{
    TRY(need(s, 0x001D5170u, 2 | 4 | 8 | 16));
    return body_001D5170(s, rec);
}

int em_area01_render_001D5A70(S *s, u32 a0, u32 a1, u32 *result)
{
    u32 r = 0;
    (void)a0;
    TRY(need(s, 0x001D5A70u, 0));
    TRY(body_001D5A70(s, a1, &r));
    if (result) *result = r;
    return 0;
}

int em_area01_render_001D5BD0(S *s)
{
    EmArea01RenderCore *c = &s->core;
    u32 base, word, r;
    TRY(need(s, 0x001D5BD0u, 1 | 2 | 4 | 8 | 16));
    c->function = 0x001D5BD0u;
    TRY(em_a01r_ld32(c, EM_A01R_D_0028A5A4, &base));
    TRY(em_a01r_ld32(c, base, &word));
    const int32_t count = (int32_t)word;
    u32 p = base + 0x10u;
    for (int32_t i = 0; i < count; ++i) {
        const u32 q = p + 0x40u;
        TRY(body_001D5A70(s, q, &r));
        if (r == 0) {
            TRY(body_001D4FC0(s, p));
        } else if (r != 0xFFu) {
            TRY(body_001D4FC0(s, p));
            TRY(body_001D5170(s, p));
        }
        c->function = 0x001D5BD0u;
        p = q + 0x820u;
    }
    return 0;
}
