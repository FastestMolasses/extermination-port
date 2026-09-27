/* em_frame_kick.c - the main loop's steps V (001D2300) and W (001D2580); see
 * em_frame_kick.h and docs/RENDER_CONTEXT.md section 9.
 *
 * Read from the original instructions: the split listings of 001D2300 and
 * 001D21E0 (their decomp C is NEARMISS), the byte-matched 001D2110,
 * 001D2130, 001D2160, 001D2180 and 001D2580, and the SDK asm-word units
 * 001015A8 / 00101810. The original address of each step is cited beside
 * it. Every load reloads what the original reloads (the context pointer and
 * the cursor words), so the result does not depend on the cursors pointing
 * outside the context. */
#include "game/em_frame_kick.h"

#include <stddef.h>

typedef uint32_t u32;

/* ---- fault latch and memory ---------------------------------------------- */

typedef struct {
    const EmFrameKickWorkers *w;
    EmFrameKickFault *fault;
    u32 fn;
} K;

static int fail(K *k, u32 data)
{
    if (k->fault && !k->fault->address) {
        k->fault->address = k->fn;
        k->fault->data = data;
    }
    return -1;
}

#define TRY(expr) do { if ((expr) < 0) return -1; } while (0)

static int load(K *k, u32 address, u32 size, uint64_t *value)
{
    uint8_t *p = (size > 1 && (address & (size - 1)) != 0) ? NULL : k->w->mem(k->w->ctx, address, size, 0);
    if (!p) return fail(k, address);
    uint64_t v = 0;
    for (u32 i = size; i-- > 0;) v = v << 8 | p[i];
    *value = v;
    return 0;
}

static int store(K *k, u32 address, u32 size, uint64_t value)
{
    uint8_t *p = (size > 1 && (address & (size - 1)) != 0) ? NULL : k->w->mem(k->w->ctx, address, size, 1);
    if (!p) return fail(k, address);
    for (u32 i = 0; i < size; ++i) p[i] = (uint8_t)(value >> (8 * i));
    return 0;
}

static int lw(K *k, u32 address, u32 *v)
{
    uint64_t x;
    TRY(load(k, address, 4, &x));
    *v = (u32)x;
    return 0;
}

/* lh: the halfword sign-extended. */
static int lh(K *k, u32 address, int32_t *v)
{
    uint64_t x;
    TRY(load(k, address, 2, &x));
    *v = (int16_t)(uint16_t)x;
    return 0;
}

static int sw(K *k, u32 address, u32 v) { return store(k, address, 4, v); }

/* ---- workers --------------------------------------------------------------- */

static int flag(K *k, int32_t a0, int32_t *ret)
{
    if (!k->w->w_001D2910) return fail(k, 0x001D2910u);
    return k->w->w_001D2910(k->w->ctx, a0, ret) < 0 ? fail(k, 0x001D2910u) : 0;
}

/* ---- the list-cursor helpers ----------------------------------------------- */

/* A tag at the list cursor (context +0x08): byte +3 = id, word +4 = address,
 * halfword +0 = count, then the cursor += 0x10 (001D2130: id 0x20, count 0;
 * 001D2180: id 0x30, count a1; 001D21E0's head: id 0x20, count 0). */
static int list_tag(K *k, u32 id, u32 address, u32 count)
{
    u32 ctx, buf;
    TRY(lw(k, EM_FRAME_KICK_D_00275670, &ctx));
    TRY(lw(k, ctx + 0x08u, &buf));
    TRY(store(k, buf + 3, 1, id));
    TRY(sw(k, buf + 4, address));
    TRY(store(k, buf, 2, count & 0xFFFFu));
    TRY(lw(k, EM_FRAME_KICK_D_00275670, &ctx));
    TRY(lw(k, ctx + 0x08u, &buf));
    return sw(k, ctx + 0x08u, buf + 0x10u);
}

/* 001D2130(a0): NEXT to a0. */
static int f_001D2130(K *k, u32 a0) { return list_tag(k, 0x20u, a0, 0); }
/* 001D2180(a0, a1): REF of a1 quadwords at a0 (a1 stored as a halfword). */
static int f_001D2180(K *k, u32 a0, u32 a1) { return list_tag(k, 0x30u, a0, a1); }

/* 001D2160(a0): the tag at a0 (a list's end) = NEXT back to the list
 * cursor (byte +3 = 0x20, word +4 = context +0x08, halfword +0 = 0). */
static int f_001D2160(K *k, u32 a0)
{
    u32 ctx, cursor;
    TRY(lw(k, EM_FRAME_KICK_D_00275670, &ctx));
    TRY(lw(k, ctx + 0x08u, &cursor));
    TRY(store(k, a0 + 3, 1, 0x20u));
    TRY(sw(k, a0 + 4, cursor));
    return store(k, a0, 2, 0);
}

/* 001D2110(): list cursor = D_0028F700 + (context +0x9C << 14). */
static int f_001D2110(K *k)
{
    u32 ctx, slot;
    TRY(lw(k, EM_FRAME_KICK_D_00275670, &ctx));
    TRY(lw(k, ctx + 0x9Cu, &slot));
    return sw(k, ctx + 0x08u, EM_FRAME_KICK_D_0028F700 + (slot << 14));
}

/* 001015A8 / 00101810(env, cx, cy, half): the draw environment's XYOFFSET
 * (+0x20) from its SCISSOR (+0x30). The two SDK routines are the same
 * instructions at two addresses. cx / cy / half arrive as the registers the
 * caller set (cx, cy sign-extended halfwords; half the sign-extended
 * 1 - field), and only their low 16 bits are used: cx and cy are
 * sign-extended from their low halfwords, half is tested on its low halfword. */
static int half_offset(K *k, u32 env, int32_t cx, int32_t cy, int32_t half)
{
    uint64_t scissor;
    TRY(load(k, env + 0x30u, 8, &scissor));
    const int64_t x0 = (int16_t)(uint16_t)cx, y0 = (int16_t)(uint16_t)cy;
    const int64_t w = (int64_t)((scissor >> 16) & 0x7FFu), h = (int64_t)((scissor >> 48) & 0x7FFu);
    const uint64_t y = (uint64_t)(y0 - (int64_t)((uint64_t)(h + 1) >> 1));
    const uint64_t x = (uint64_t)(x0 - (int64_t)((uint64_t)(w + 1) >> 1)) << 4;
    uint64_t hi;
    if (((uint32_t)half & 0xFFFFu) != 0)
        hi = ((y << 4) + 8u) << 32;
    else
        hi = y << 36;
    return store(k, env + 0x20u, 8, x | hi);
}

/* 001D21E0(): the closing tag, then the hardware kick. */
static int f_001D21E0(K *k)
{
    const u32 fn = k->fn;
    k->fn = 0x001D21E0u;
    u32 gs, ctx, slot;
    /* dmac_channel_base(1) is the hardware channel pointer (boundary). */
    TRY(lw(k, EM_FRAME_KICK_D_00275674, &gs));
    TRY(list_tag(k, 0x20u, gs + 0x10u, 0));                            /* 001D2208..001D2230 */
    /* dma_wait_and_submit(0, 0), the D1_CHCR / VIF1 edits, 0011B9E0(1, 1,
     * 0) and 0010BAA0(0): hardware (the w_kick boundary). */
    TRY(lw(k, EM_FRAME_KICK_D_00275670, &ctx));
    TRY(lw(k, ctx + 0x9Cu, &slot));                                    /* 001D22D4 */
    if (!k->w->w_kick) return fail(k, 0x00101F08u);
    if (k->w->w_kick(k->w->ctx, EM_FRAME_KICK_D_0028F700 + (slot << 14)) < 0)
        return fail(k, 0x00101F08u);
    k->fn = fn;
    return 0;
}

/* The gate before 001D2300's 001E0DF0 (0x1D2438..0x1D2468): *calls = 1 when
 * D_008106C4 == 0, 001D2910(4) == 0 and 001D2910(0x20) != 0. `c4` is the
 * byte already loaded. */
static int gate(K *k, uint8_t c4, int *calls)
{
    *calls = 0;
    if (c4 != 0) return 0;
    int32_t r;
    TRY(flag(k, 4, &r));
    if (r != 0) return 0;
    TRY(flag(k, 0x20, &r));
    *calls = r != 0;
    return 0;
}

static int c4_byte(K *k, uint8_t *c4)
{
    uint64_t v;
    TRY(load(k, EM_FRAME_KICK_D_008106C4, 1, &v));
    *c4 = (uint8_t)v;
    return 0;
}

/* ---- 001D2300 ----------------------------------------------------------------- */

static int ready(const EmFrameKickWorkers *w)
{
    return w && w->mem && w->w_001D1F80 && w->w_001D2910 && w->w_001D2830 && w->w_001E0DF0 && w->w_kick;
}

int em_frame_kick_001D2300(const EmFrameKickWorkers *w, EmFrameKickFault *fault)
{
    K k = {w, fault, 0x001D2300u};
    if (!w || (fault && fault->address)) return -1;
    if (!ready(w)) return fail(&k, 0);
    u32 t0, gs, slot, cur;
    TRY(lw(&k, EM_FRAME_KICK_D_00275670, &t0));                          /* 001D2310 */
    TRY(lw(&k, EM_FRAME_KICK_D_00275674, &gs));
    TRY(lw(&k, t0 + 0x9Cu, &slot));                                      /* 001D2320 */
    /* The channel-1 cursor (+0x14): REF of one quadword at the GS block. */
    TRY(lw(&k, t0 + 0x14u, &cur));
    TRY(store(&k, cur + 3, 1, 0x30u));                                   /* 001D2330 */
    TRY(lw(&k, t0 + 0x14u, &cur));
    TRY(sw(&k, cur + 4, gs));
    TRY(lw(&k, t0 + 0x14u, &cur));
    TRY(store(&k, cur, 2, 1));
    TRY(lw(&k, t0 + 0x14u, &cur));
    TRY(sw(&k, t0 + 0x14u, cur + 0x10u));                                /* 001D2350 */
    if (w->w_001D1F80(w->ctx, 1, 0, 7) < 0) return fail(&k, 0x001D1F80u);  /* 001D234C */

    /* The slot's draw environments: 001015A8 at +0x40, 00101810 at +0xC0. */
    const u32 off = slot * 0x190u;
    for (int pass = 0; pass < 2; ++pass) {
        int32_t field, cx, cy;
        TRY(lw(&k, EM_FRAME_KICK_D_00275674, &gs));
        TRY(lh(&k, EM_FRAME_KICK_D_00810E88, &field));                 /* 001D236C / 001D23A4 */
        TRY(lh(&k, EM_FRAME_KICK_SPR_3B70, &cx));
        TRY(lh(&k, EM_FRAME_KICK_SPR_3B70 + 2, &cy));
        const int32_t half = (int16_t)(uint16_t)(uint32_t)(1 - field); /* the low halfword, sign-extended */
        const u32 fn = k.fn;
        k.fn = pass == 0 ? 0x001015A8u : 0x00101810u;
        TRY(half_offset(&k, gs + off + (pass == 0 ? 0x40u : 0xC0u), cx, cy, half));
        k.fn = fn;
    }
    TRY(f_001D2110(&k));                                                 /* 001D23D8 */
    TRY(lw(&k, EM_FRAME_KICK_D_00275674, &gs));
    TRY(f_001D2180(&k, gs + off + 0x20u, 0x19u));                        /* 001D23EC */

    int32_t flag3;
    TRY(flag(&k, 3, &flag3));                                            /* 001D23F4 */
    TRY(lw(&k, EM_FRAME_KICK_D_00275674, &gs));
    if (flag3 != 0) {
        TRY(f_001D2180(&k, gs + 0x420u, 8));                             /* 001D240C */
        if (w->w_001D2830(w->ctx, 3, 0) < 0) return fail(&k, 0x001D2830u);  /* 001D2418 */
    } else {
        TRY(f_001D2180(&k, gs + 0x3A0u, 8));                             /* 001D2430 */
    }

    const u32 channel0 = EM_FRAME_KICK_D_0028F700 + slot * 0x60800u + 0x8000u;
    uint8_t c4;
    u32 ctx, a0;
    TRY(c4_byte(&k, &c4));                                               /* 001D2438 */
    if (c4 == 0) {
        int calls;
        TRY(gate(&k, c4, &calls));
        if (calls && w->w_001E0DF0(w->ctx) < 0) return fail(&k, 0x001E0DF0u);  /* 001D2468 */
        TRY(f_001D2130(&k, channel0));                                   /* 001D2494 */
        TRY(lw(&k, EM_FRAME_KICK_D_00275670, &ctx));
        TRY(lw(&k, ctx + 0x10u, &a0));
        TRY(f_001D2160(&k, a0));                                         /* 001D24A0 */
    }
    TRY(c4_byte(&k, &c4));                                               /* 001D24A8 */
    if (c4 == 0) {
        TRY(lw(&k, EM_FRAME_KICK_D_00275670, &ctx));
        TRY(lw(&k, ctx + 0x00u, &a0));
        TRY(f_001D2130(&k, a0));                                         /* 001D24BC */
        TRY(lw(&k, EM_FRAME_KICK_D_00275670, &ctx));
        TRY(lw(&k, ctx + 0x04u, &a0));
        TRY(f_001D2160(&k, a0));                                         /* 001D24C8 */
    }
    TRY(f_001D2130(&k, EM_FRAME_KICK_D_0028F700 + slot * 0x95760u + 0xC9000u));  /* 001D24F0 */
    TRY(lw(&k, EM_FRAME_KICK_D_00275670, &ctx));
    TRY(lw(&k, ctx + 0x14u, &a0));
    TRY(f_001D2160(&k, a0));                                             /* 001D24FC */
    TRY(c4_byte(&k, &c4));                                               /* 001D2504 */
    if (c4 != 0) {
        TRY(f_001D2130(&k, channel0));                                   /* 001D2538 */
        TRY(lw(&k, EM_FRAME_KICK_D_00275670, &ctx));
        TRY(lw(&k, ctx + 0x10u, &a0));
        TRY(f_001D2160(&k, a0));
        TRY(lw(&k, EM_FRAME_KICK_D_00275670, &ctx));
        TRY(lw(&k, ctx + 0x00u, &a0));
        TRY(f_001D2130(&k, a0));
        TRY(lw(&k, EM_FRAME_KICK_D_00275670, &ctx));
        TRY(lw(&k, ctx + 0x04u, &a0));
        TRY(f_001D2160(&k, a0));                                         /* 001D255C */
    }
    return f_001D21E0(&k);                                               /* 001D2564 */
}

int em_frame_kick_calls_001E0DF0(const EmFrameKickWorkers *w, int *calls, EmFrameKickFault *fault)
{
    K k = {w, fault, 0x001D2300u};
    if (!w || !calls || (fault && fault->address)) return -1;
    if (!w->mem || !w->w_001D2910) return fail(&k, 0);
    uint8_t c4;
    TRY(c4_byte(&k, &c4));
    return gate(&k, c4, calls);
}

/* ---- 001D2580 ----------------------------------------------------------------- */

int em_frame_kick_001D2580(const EmFrameKickWorkers *w, int32_t a0, EmFrameKickFault *fault)
{
    K k = {w, fault, 0x001D2580u};
    if (!w || (fault && fault->address)) return -1;
    if (!w->mem) return fail(&k, 0);
    u32 ctx;
    TRY(lw(&k, EM_FRAME_KICK_D_00275670, &ctx));
    return sw(&k, ctx + 0x98u, (u32)a0);
}
