/* Render context lane (L30). See em_render_context.h and
 * docs/RENDER_CONTEXT.md. Every store and branch below cites the original
 * address it comes from; the code follows the original's order of loads,
 * stores and calls. */
#include "game/em_render_context.h"

#include <stddef.h>
#include <string.h>

#include "game/em_ee_float.h"

typedef uint32_t u32;
typedef uint64_t u64;
typedef EmRenderContext S;

/* binary32 constants the originals load (lui/ori immediates). */
#define F_ONE 0x3F800000u     /* 1.0 */
#define F_HALF 0x3F000000u    /* 0.5 */
#define F_1_5 0x3FC00000u     /* 1.5 */
#define F_TWO 0x40000000u     /* 2.0 */
#define F_3_5 0x40600000u     /* 3.5 */
#define F_FOUR 0x40800000u    /* 4.0 */
#define F_FIVE 0x40A00000u    /* 5.0 */
#define F_5_5 0x40B00000u     /* 5.5 */
#define F_SIX 0x40C00000u     /* 6.0 */
#define F_SEVEN 0x40E00000u   /* 7.0 */
#define F_EIGHT 0x41000000u   /* 8.0 */
#define F_16 0x41800000u      /* 16.0 */
#define F_30 0x41F00000u      /* 30.0 */
#define F_62 0x42780000u      /* 62.0 */
#define F_100 0x42C80000u     /* 100.0 */
#define F_256 0x43800000u     /* 256.0 */
#define F_448 0x43E00000u     /* 448.0 */
#define F_1024 0x44800000u    /* 1024.0 */
#define F_1500 0x44BB8000u    /* 1500.0 */
#define F_4096 0x45800000u    /* 4096.0 */
#define F_8500 0x4604D000u    /* 8500.0 */
#define F_30500 0x46EE4800u   /* 30500.0 */
#define F_40500 0x471E3400u   /* 40500.0 */
#define F_50 0x42480000u      /* 50.0 */
#define F_TENTH 0x3DCCCCCDu   /* 0.1 */
#define F_0_05 0x3D4CCCCDu    /* 0.05 */
#define F_0_15 0x3E19999Au    /* 0.15 */
#define F_FAR 0x4B7F0000u     /* 16711680.0 */
#define F_24BIT 0x4B7FFFFFu   /* 16777215.0 */

/* ------------------------------------------------------------------ */
/* Fault latch and original-address memory.                           */
/* ------------------------------------------------------------------ */

static int latched(const S *s) { return s->fault.code != EM_RC_FAULT_NONE; }

static int fault(S *s, u32 address, int32_t code)
{
    if (!latched(s)) {
        s->fault.address = address;
        s->fault.code = code;
    }
    return -1;
}

/* Host bytes for original addresses [a, a + n), or NULL with a fault. */
static uint8_t *mem(S *s, u32 a, u32 n)
{
    for (u32 i = 0; s->world.views && i < s->world.view_count; ++i) {
        const EmRenderContextView *v = &s->world.views[i];
        if (v->bytes && a >= v->address && (u64)a + n <= (u64)v->address + v->size)
            return v->bytes + (a - v->address);
    }
    fault(s, a, EM_RC_FAULT_BAD_INDEX);
    return NULL;
}

static int span(S *s, u32 a, u32 n) { return mem(s, a, n) ? 0 : -1; }

static u32 get32(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void put32(uint8_t *p, u32 v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static int ld8(S *s, u32 a, u32 *v)
{
    uint8_t *p = mem(s, a, 1);
    if (!p) return -1;
    *v = p[0];
    return 0;
}
static int ld16(S *s, u32 a, u32 *v)
{
    uint8_t *p = mem(s, a, 2);
    if (!p) return -1;
    *v = (u32)p[0] | (u32)p[1] << 8;
    return 0;
}
static int ld32(S *s, u32 a, u32 *v)
{
    uint8_t *p = mem(s, a, 4);
    if (!p) return -1;
    *v = get32(p);
    return 0;
}
static int st8(S *s, u32 a, u32 v)
{
    uint8_t *p = mem(s, a, 1);
    if (!p) return -1;
    p[0] = (uint8_t)v;
    return 0;
}
static int st16(S *s, u32 a, u32 v)
{
    uint8_t *p = mem(s, a, 2);
    if (!p) return -1;
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    return 0;
}
static int st32(S *s, u32 a, u32 v)
{
    uint8_t *p = mem(s, a, 4);
    if (!p) return -1;
    put32(p, v);
    return 0;
}
static int st64(S *s, u32 a, u64 v)
{
    uint8_t *p = mem(s, a, 8);
    if (!p) return -1;
    put32(p, (u32)v);
    put32(p + 4, (u32)(v >> 32));
    return 0;
}
/* Quadword loads and stores (the original 128-bit memory accesses): the low
 * four address bits are ignored. */
static int ldq(S *s, u32 a, u32 out[4])
{
    uint8_t *p = mem(s, a & ~15u, 16);
    if (!p) return -1;
    for (int i = 0; i < 4; ++i) out[i] = get32(p + 4 * i);
    return 0;
}
static int stq(S *s, u32 a, const u32 in[4])
{
    uint8_t *p = mem(s, a & ~15u, 16);
    if (!p) return -1;
    for (int i = 0; i < 4; ++i) put32(p + 4 * i, in[i]);
    return 0;
}

#define TRY(expr) do { if ((expr) < 0) return -1; } while (0)
/* A reached worker must be bound. */
#define NEEDW(field, address) \
    do { if (!s->workers.field) return fault(s, (address), EM_RC_FAULT_NULL_WORKER); } while (0)
/* A worker call; a negative result faults. */
#define CALLW(address, expr) \
    do { if ((expr) < 0) return fault(s, (address), EM_RC_FAULT_WORKER_FAILED); } while (0)

/* A sign-extended 32-bit register value as the 64-bit register image. */
static u64 sx(int32_t x) { return (u64)(int64_t)x; }
/* The original's sra on a 32-bit register. */
static int32_t sra32(int32_t x, unsigned n) { return x < 0 ? ~(~x >> n) : x >> n; }

/* ------------------------------------------------------------------ */
/* Requirement checks: the fixed addresses and the workers a routine   */
/* (with its nested translations) can reach, checked at its entry.     */
/* ------------------------------------------------------------------ */

static int req_flags(S *s) /* 001D2910 -> 001D2710 / 001E0C60 */
{
    TRY(span(s, s->world.ctx + EM_RC_CTX_FLAGS_LO, 4));
    return span(s, s->world.ctx + EM_RC_CTX_FLAGS_HI, 4);
}

static int req_001D6B10(S *s)
{
    NEEDW(w_001D6930, 0x001D6930u);
    NEEDW(w_001D1F20, 0x001D1F20u);
    return 0;
}

static int req_001DDE10(S *s)
{
    const u32 c = s->world.ctx;
    NEEDW(w_0015D2F0, 0x0015D2F0u);
    NEEDW(w_0022EBE0, 0x0022EBE0u);
    NEEDW(w_001026A0, 0x001026A0u);
    NEEDW(w_0011DF78, 0x0011DF78u);
    NEEDW(w_001281C0, 0x001281C0u);
    NEEDW(w_001D6BA0, 0x001D6BA0u);
    NEEDW(w_001D1FF0, 0x001D1FF0u);
    NEEDW(w_001CB760, 0x001CB760u);
    TRY(req_001D6B10(s));
    TRY(req_flags(s));
    TRY(span(s, c + EM_RC_CTX_CHAN3, 4));
    TRY(span(s, c + 0x1F4u, 4));
    TRY(span(s, (c + EM_RC_CTX_DEPTH) & ~15u, 0x18));
    TRY(span(s, c + EM_RC_CTX_BARS, 0x24));
    TRY(span(s, EM_RC_D_0027568C, 4));
    TRY(span(s, EM_RC_D_00275690, 8));
    TRY(span(s, EM_RC_D_008104E0, 4));
    TRY(span(s, EM_RC_D_00810700, 2));
    return span(s, EM_RC_D_00810360 & ~15u, 16);
}

static int req_001DDAA0(S *s)
{
    NEEDW(w_001B0070, 0x001B0070u);
    NEEDW(w_001DE920, 0x001DE920u);
    TRY(span(s, EM_RC_D_00810700, 2));
    return req_001DDE10(s);
}

static int req_001DEEE0(S *s, u32 p)
{
    NEEDW(w_001DF110, 0x001DF110u);
    TRY(span(s, p, 0x20));
    return req_flags(s);
}

static int req_001D2E00(S *s, int32_t a0) /* also 001D2DE0 */
{
    return span(s, (u32)(a0 << 2) + s->world.ctx + EM_RC_CTX_SLOTS, 4);
}

/* ------------------------------------------------------------------ */
/* Helpers: the flag words, the +0x2520 words, 001D21B0.               */
/* ------------------------------------------------------------------ */

int em_render_context_001D2710(S *s, int32_t a0, uint32_t *result)
{
    if (!s || latched(s)) return -1;
    u32 flags;
    TRY(ld32(s, s->world.ctx + EM_RC_CTX_FLAGS_LO, &flags)); /* 001D271C */
    u32 v = flags & (1u << ((u32)a0 & 31u));                 /* sllv 001D2718 */
    if (result) *result = v;
    return 0;
}

int em_render_context_001E0C60(S *s, int32_t a0, uint32_t *result)
{
    if (!s || latched(s)) return -1;
    u32 flags;
    TRY(ld32(s, s->world.ctx + EM_RC_CTX_FLAGS_HI, &flags)); /* 001E0C70 */
    u32 v = flags & (1u << ((u32)(a0 - 0x20) & 31u));        /* 001E0C60/001E0C6C */
    if (result) *result = v;
    return 0;
}

int em_render_context_001D2910(S *s, int32_t a0, uint32_t *result)
{
    if (!s || latched(s)) return -1;
    u32 v = 0; /* the delay-slot zero at 001D2938 */
    if (a0 < 0x20)                                            /* 001D2914 */
        TRY(em_render_context_001D2710(s, a0, &v));
    else if (a0 < 0x40)                                       /* 001D2930 */
        TRY(em_render_context_001E0C60(s, a0, &v));
    if (result) *result = v;
    return 0;
}

int em_render_context_001E0C80(S *s, int32_t a0, int32_t a1, uint32_t *result)
{
    if (!s || latched(s)) return -1;
    const u32 at = s->world.ctx + EM_RC_CTX_FLAGS_HI;
    u32 old;
    TRY(ld32(s, at, &old));                                   /* 001E0C90 */
    const u32 bit = 1u << ((u32)(a0 - 0x20) & 31u);           /* 001E0C80/001E0C8C */
    const u32 was = old & bit;    /* 001E0C9C: computed in the delay slot, both ways */
    const u32 now = a1 ? (old | bit) : (old & ~bit);          /* 001E0C98 */
    TRY(st32(s, at, now));                                    /* 001E0CB0 */
    if (result) *result = was != 0;                           /* 001E0CB8 */
    return 0;
}

/* 001D2730(a0, a1) (NEARMISS C; the .s was followed): flags 0..0x1F at
 * context +0x0C. bit = 1 << (a0 & 31) (SLLV), was = old & bit. For a0 == 0
 * only, the 32-byte blocks move through block_copy (00121870, a forward
 * copy; the blocks never overlap): a1 != 0 and the bit clear copies +0xC0
 * to +0xA0; a1 == 0 and the bit set copies +0xA0 to +0xC0, then +0x100 to
 * +0xA0 (D_00275670 re-read). The cases 1..4 and 0x20 skip the copies like
 * every other a0. Then the flag word is stored with the bit set (a1 != 0)
 * or cleared, and the result is was != 0. a1 is tested as a whole register
 * (PADDUB copy, BEQZ). */
static int block_copy32(S *s, u32 dst, u32 src)
{
    uint8_t *d = mem(s, dst, 0x20);
    if (!d) return -1;
    const uint8_t *from = mem(s, src, 0x20);
    if (!from) return -1;
    uint8_t q[0x20];
    memcpy(q, from, sizeof q);
    memcpy(d, q, sizeof q);
    return 0;
}

int em_render_context_001D2730(S *s, int32_t a0, int32_t a1, uint32_t *result)
{
    if (!s || latched(s)) return -1;
    const u32 ctx = s->world.ctx;
    u32 old;
    TRY(ld32(s, ctx + EM_RC_CTX_FLAGS_LO, &old));             /* 001D275C */
    const u32 bit = 1u << ((u32)a0 & 31u);                     /* 001D2758 */
    const u32 was = old & bit;                                 /* 001D2764 */
    if (a0 == 0) {                                             /* 001D2794 */
        TRY(span(s, ctx + 0xA0u, 0x20));
        TRY(span(s, ctx + 0xC0u, 0x20));
        TRY(span(s, ctx + 0x100u, 0x20));
        if (a1 != 0) {                                         /* 001D27A4 */
            if (was == 0) TRY(block_copy32(s, ctx + 0xA0u, ctx + 0xC0u));   /* 001D27B8 */
        } else if (was != 0) {                                 /* 001D27C8 */
            TRY(block_copy32(s, ctx + 0xC0u, ctx + 0xA0u));    /* 001D27D4 */
            TRY(block_copy32(s, ctx + 0xA0u, ctx + 0x100u));   /* 001D27E8 */
        }
    }
    const u32 now = a1 ? (old | bit) : (old & ~bit);           /* 001D27F0..001D2804 */
    TRY(st32(s, ctx + EM_RC_CTX_FLAGS_LO, now));               /* 001D2810 */
    if (result) *result = was != 0;                            /* 001D280C */
    return 0;
}

/* 001DEDB0(a0) (asm words): a0 == 9 -> context +0x2490, any other value ->
 * context +0x2470 (D_00275670 read in either arm). */
static u32 ramp_record(S *s, int32_t a0)
{
    return s->world.ctx + (a0 == 9 ? EM_RC_CTX_RAMP_B : EM_RC_CTX_RAMP_A);
}

/* 001DEDE0 (a jump to 001DEDF0, asm words): the two ramp records' set-up.
 * For flag 2, then flag 9: record = 001DEDB0(flag); bytes +0, +3, +2, +1 =
 * 0 (in that order) and word +8 = flag. Then 001DEE80(2, &D_0026E850),
 * 001DEEC0(2, 0x60), 001DEE80(9, &D_0026E850), 001DEEC0(9, 0x60): 001DEE80
 * copies the three words at its a1 to the record's +0x10..+0x18, 001DEEC0
 * stores its a1 at +4. */
int em_render_context_001DEDE0(S *s)
{
    if (!s || latched(s)) return -1;
    TRY(span(s, s->world.ctx + EM_RC_CTX_RAMP_A, 0x40));
    TRY(span(s, EM_RC_D_0026E850, 12));
    static const int32_t flags[2] = {2, 9};
    for (int i = 0; i < 2; ++i) {
        const u32 r = ramp_record(s, flags[i]);
        TRY(st8(s, r + 0, 0));
        TRY(st8(s, r + 3, 0));
        TRY(st8(s, r + 2, 0));
        TRY(st8(s, r + 1, 0));
        TRY(st32(s, r + 8, (u32)flags[i]));
    }
    for (int i = 0; i < 2; ++i) {
        u32 r = ramp_record(s, flags[i]), w;
        for (u32 k = 0; k < 3; ++k) {                          /* 001DEE80 */
            TRY(ld32(s, EM_RC_D_0026E850 + 4 * k, &w));
            TRY(st32(s, r + 0x10 + 4 * k, w));
        }
        r = ramp_record(s, flags[i]);
        TRY(st32(s, r + 4, 0x60));                              /* 001DEEC0 */
    }
    return 0;
}

int em_render_context_001D2E00(S *s, int32_t a0, uint32_t *result)
{
    if (!s || latched(s)) return -1;
    u32 v;
    TRY(ld32(s, (u32)(a0 << 2) + s->world.ctx + EM_RC_CTX_SLOTS, &v)); /* 001D2E10 */
    if (result) *result = v;
    return 0;
}

int em_render_context_001D2DE0(S *s, int32_t a0, uint32_t a1)
{
    if (!s || latched(s)) return -1;
    return st32(s, (u32)(a0 << 2) + s->world.ctx + EM_RC_CTX_SLOTS, a1); /* 001D2DF0 */
}

int em_render_context_001D21B0(S *s, uint32_t a0)
{
    if (!s || latched(s)) return -1;
    const u32 c = s->world.ctx;
    u32 buf;
    TRY(ld32(s, c + 8u, &buf));                               /* 001D21B8 */
    TRY(span(s, buf, 8));
    TRY(st8(s, buf + 3u, 0x50));                              /* 001D21BC */
    TRY(st32(s, buf + 4u, a0));                               /* 001D21C0 */
    TRY(st16(s, buf, 0));                                     /* 001D21C4 */
    TRY(ld32(s, c + 8u, &buf));                               /* 001D21CC */
    return st32(s, c + 8u, buf + 0x10u);                      /* 001D21D8 */
}

/* ------------------------------------------------------------------ */
/* Packet builders: 001D6B10, 001D6C90.                               */
/* ------------------------------------------------------------------ */

int em_render_context_001D6B10(S *s, int32_t a0, int32_t a1, int32_t a2, int32_t a3, uint32_t *result)
{
    if (!s || latched(s)) return -1;
    TRY(req_001D6B10(s));
    u32 r = 0;
    /* 001D6B28: a0..a3 unchanged, t0 = D_0026E510 */
    CALLW(0x001D6930u, s->workers.w_001D6930(s->workers.ctx, a0, a1, a2, a3, EM_RC_D_0026E510, &r));
    CALLW(0x001D1F20u, s->workers.w_001D1F20(s->workers.ctx, a0)); /* 001D6B34 */
    if (result) *result = r;                                  /* 001D6B3C */
    return 0;
}

int em_render_context_001D6C90(S *s, const int32_t args[15], uint32_t *result)
{
    if (!s || latched(s) || !args) return -1;
    /* The two register words (001D6C90..001D6CEC): each argument is the
     * sign-extended 32-bit register value. */
    const u64 w40lo = sx(args[2]) | sx(args[3]) << 1 | sx(args[4]) << 4 | sx(args[5]) << 12 |
                      sx(args[6]) << 14 | sx(args[7]) << 15;
    const u32 at = (u32)(args[0] << 2) + s->world.ctx + 0x10u; /* 001D6D04 */
    u32 p;
    TRY(ld32(s, at, &p));                                     /* 001D6D08 */
    TRY(span(s, p, EM_RC_001D6C90_BYTES));
    TRY(st8(s, p + 3u, 0x10));                                /* 001D6D20 */
    TRY(ld32(s, at, &p));
    TRY(st32(s, p + 4u, 0));                                  /* 001D6D34 */
    TRY(ld32(s, at, &p));
    TRY(st16(s, p, 5));                                       /* 001D6D40 */
    TRY(ld32(s, at, &p));                                     /* 001D6D44 */
    TRY(st32(s, at, p + EM_RC_001D6C90_BYTES));               /* 001D6D4C */
    TRY(span(s, p, EM_RC_001D6C90_BYTES));
    static const u32 zero[4] = {0, 0, 0, 0};
    TRY(stq(s, p + 0x10u, zero));                             /* 001D6D50 */
    TRY(st32(s, p + 0x1Cu, 0x50000004u));                     /* 001D6D54 */
    TRY(st64(s, p + 0x20u, (u64)0x10000000u << 32 | 0x8003u)); /* 001D6D58 */
    TRY(st64(s, p + 0x28u, 0xEu));                            /* 001D6D5C */
    TRY(st64(s, p + 0x30u, sx(args[1])));                     /* 001D6D60 */
    TRY(st64(s, p + 0x38u, 0x3Bu));                           /* 001D6D64 */
    /* 001D6D68..001D6D84: the stack words at sp+0 and sp+8 */
    TRY(st64(s, p + 0x40u, sx(args[9]) << 17 | (sx(args[8]) << 16 | w40lo)));
    TRY(st64(s, p + 0x48u, 0x47u));                           /* 001D6D88 */
    /* 001D6D8C..001D6DC0: sp+0x10 | sp+0x18 << 2 | sp+0x20 << 4 | sp+0x28 << 6
     * | sp+0x30 << 32 (the last one's upper word shifted out) */
    const u64 w50 = (u64)(u32)args[14] << 32 |
                    (sx(args[13]) << 6 | (sx(args[12]) << 4 | (sx(args[10]) | sx(args[11]) << 2)));
    TRY(st64(s, p + 0x50u, w50));
    TRY(st64(s, p + 0x58u, 0x42u));                           /* 001D6DC8 */
    if (result) *result = p + 0x10u;                          /* 001D6D70 */
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001DD7B0 / 001DD940: the GS block D_0081C050 and context +0x24F0.   */
/* ------------------------------------------------------------------ */

int em_render_context_001DD7B0(S *s)
{
    if (!s || latched(s)) return -1;
    const u32 c = s->world.ctx;
    NEEDW(w_001006D8, 0x001006D8u);
    TRY(span(s, EM_RC_D_0081C050, 0x30));
    TRY(span(s, EM_RC_D_0027568C, 4));
    TRY(span(s, c + EM_RC_CTX_BARS, 0x24));
    u32 v;
    TRY(st32(s, 0x0081C050u, 0));                             /* 001DD7C0 */
    TRY(st32(s, 0x0081C054u, 0));                             /* 001DD7C8 */
    TRY(st32(s, 0x0081C058u, 0x11000000u));                   /* 001DD7D4 */
    TRY(st32(s, 0x0081C05Cu, 0x50000009u));                   /* 001DD7E8 */
    TRY(ld16(s, 0x0081C060u, &v));                            /* 001DD7F0 */
    TRY(st16(s, 0x0081C060u, (v & 0x8000u) | 8u));            /* 001DD80C: low 15 bits = 8 */
    TRY(ld8(s, 0x0081C061u, &v));                             /* 001DD814 */
    TRY(st8(s, 0x0081C061u, (v & 0x7Fu) | 0x80u));            /* 001DD834: bit 7 set */
    TRY(ld8(s, 0x0081C065u, &v));                             /* 001DD844 */
    TRY(st8(s, 0x0081C065u, v & 0xBFu));                      /* 001DD860: bit 6 = 0 */
    TRY(ld8(s, 0x0081C067u, &v));                             /* 001DD868 */
    TRY(st8(s, 0x0081C067u, v & 0xF3u));                      /* 001DD888: bits 2..3 = 0 */
    TRY(ld8(s, 0x0081C067u, &v));                             /* 001DD890 */
    TRY(st8(s, 0x0081C067u, (v & 0x0Fu) | 0x10u));            /* 001DD8B0: bits 4..7 = 1 */
    TRY(ld8(s, 0x0081C068u, &v));                             /* 001DD8B8 */
    TRY(st8(s, 0x0081C068u, (v & 0xF0u) | 0x0Eu));            /* 001DD8D4: bits 0..3 = 0xE */
    /* 001DD8D0: 001006D8(D_0081C070, 0, 0x100, 0x100, t0 = 0, t1 = 2) */
    CALLW(0x001006D8u, s->workers.w_001006D8(s->workers.ctx, EM_RC_D_0081C070, 0, 0x100, 0x100, 0, 2));
    u32 tex;
    TRY(ld32(s, EM_RC_D_0027568C, &tex));                     /* 001DD8DC */
    TRY(ld16(s, EM_RC_D_0081C070, &v));                       /* 001DD8E0 */
    /* 001DD8EC..001DD900: low 9 bits = (D_0027568C >> 13) & 0x1FF */
    TRY(st16(s, EM_RC_D_0081C070, (v & 0xFE00u) | ((u32)sra32((int32_t)tex, 13) & 0x1FFu)));
    const u32 p = c + EM_RC_CTX_BARS;                         /* 001DD7F4 */
    TRY(st32(s, p + 0xCu, 0));                                /* 001DD904 */
    TRY(st32(s, p + 0x8u, 0));
    TRY(st32(s, p + 0x4u, 0));
    TRY(st32(s, p + 0x0u, 0));
    TRY(st32(s, p + 0x1Cu, 0));                               /* 001DD914 */
    TRY(st32(s, p + 0x18u, 0));
    TRY(st32(s, p + 0x14u, 0));
    TRY(st32(s, p + 0x10u, 0));
    return st32(s, p + 0x20u, 1);                             /* 001DD924 */
}

int em_render_context_001DD940(S *s) { return em_render_context_001DD7B0(s); } /* j 001DD7B0 */

/* ------------------------------------------------------------------ */
/* 001DD950: the +0x2450 quadword and its two floats.                 */
/* ------------------------------------------------------------------ */

int em_render_context_001DD950(S *s, uint32_t a0, uint32_t f12, uint32_t f13)
{
    if (!s || latched(s)) return -1;
    const u32 c = s->world.ctx;
    TRY(span(s, a0 & ~15u, 16));
    TRY(span(s, (c + EM_RC_CTX_DEPTH) & ~15u, 16));
    TRY(span(s, c + 0x2460u, 8));
    u32 q[4];
    TRY(ldq(s, a0, q));                                       /* 001DD95C */
    const u32 scale = em_ee_div_bits(F_24BIT, f12);           /* 001DD960 */
    TRY(stq(s, c + EM_RC_CTX_DEPTH, q));                      /* 001DD968 */
    TRY(st32(s, c + 0x2460u, scale));                         /* 001DD970 */
    return st32(s, c + 0x2464u, f13);                         /* 001DD97C */
}

/* ------------------------------------------------------------------ */
/* 001DEEE0: the ramp record.                                          */
/* ------------------------------------------------------------------ */

int em_render_context_001DEEE0(S *s, uint32_t p)
{
    if (!s || latched(s)) return -1;
    TRY(req_001DEEE0(s, p));
    u32 a, state, test, n, limit;
    TRY(ld32(s, p + 8u, &a));
    TRY(ld8(s, p, &state));
    switch (state) {
    case 0:
        TRY(st32(s, p + 0x1Cu, 0));
        TRY(em_render_context_001D2910(s, (int32_t)a, &test));
        return st8(s, p, test == 0 ? 3u : 1u);
    case 1:
        TRY(em_render_context_001D2910(s, (int32_t)a, &test));
        if (test == 0) TRY(st8(s, p, 2));
        TRY(ld32(s, p + 0x1Cu, &n));
        n += 8u;
        TRY(st32(s, p + 0x1Cu, n));
        TRY(ld32(s, p + 4u, &limit));
        if ((int32_t)n >= (int32_t)limit) TRY(st32(s, p + 0x1Cu, limit));
        CALLW(0x001DF110u, s->workers.w_001DF110(s->workers.ctx, p + 0x10u));
        return 0;
    case 2:
        TRY(ld32(s, p + 0x1Cu, &n));
        n -= 8u;
        TRY(st32(s, p + 0x1Cu, n));
        if ((int32_t)n <= 0) {
            TRY(st32(s, p + 0x1Cu, 0));
            TRY(st8(s, p, 3));
        }
        TRY(em_render_context_001D2910(s, (int32_t)a, &test));
        if (test != 0) TRY(st8(s, p, 1));
        CALLW(0x001DF110u, s->workers.w_001DF110(s->workers.ctx, p + 0x10u));
        return 0;
    default:
        TRY(em_render_context_001D2910(s, (int32_t)a, &test));
        if (test != 0) TRY(st8(s, p, 1));
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* 001DDE10: the four-sprite pass.                                     */
/* ------------------------------------------------------------------ */

/* t + (gain * (q2 - t)) / 8 in the original's operand order: sub, mul
 * (gain first), div by 8, add (t first). gain == 0 means the forms with no
 * multiply (the unit gain). */
static u32 ease_row(u32 t, u32 q2, u32 gain)
{
    u32 d = em_ee_sub_bits(q2, t);
    if (gain) d = em_ee_mul_bits(gain, d);
    return em_ee_add_bits(t, em_ee_div_bits(d, F_EIGHT));
}

int em_render_context_001DDE10(S *s)
{
    if (!s || latched(s)) return -1;
    TRY(req_001DDE10(s));
    const u32 c = s->world.ctx;
    const u32 rp = c + EM_RC_CTX_BARS;                        /* 001DDE58 */
    u32 k0, k1, e0, v690, v694, t, f1F4;
    TRY(ld8(s, 0x00810700u, &k0));
    TRY(ld8(s, 0x00810701u, &k1));
    const u32 key = (k0 << 8) + k1;                           /* 001DDE5C */
    int32_t mode, v;
    CALLW(0x0015D2F0u, s->workers.w_0015D2F0(s->workers.ctx, &mode)); /* 001DDE60 */
    TRY(ld32(s, EM_RC_D_008104E0, &e0));                      /* 001DDE6C */
    if (e0 == 0x29u || e0 == 0xCu || e0 == 0xDu) mode = 2;    /* 001DDE78..001DDE9C */
    CALLW(0x0022EBE0u, s->workers.w_0022EBE0(s->workers.ctx, &v)); /* 001DDEA0 */

    u32 q[4];
    if (v != 0) {                                             /* 001DDEAC */
        TRY(ldq(s, c + EM_RC_CTX_DEPTH, q));                  /* 00102948 copy */
        u32 in[4];
        memcpy(in, q, sizeof in);
        CALLW(0x001026A0u, s->workers.w_001026A0(s->workers.ctx, q, EM_RC_D_70003AC0, in));
        CALLW(0x0011DF78u, s->workers.w_0011DF78(s->workers.ctx, q[2], &q[2])); /* 001DDEDC */
        CALLW(0x0011DF78u, s->workers.w_0011DF78(s->workers.ctx, q[3], &q[3])); /* 001DDEEC */
        q[3] = em_ee_add_bits(q[3], F_100);                   /* 001DDF04 */
        q[2] = em_ee_div_bits(em_ee_mul_bits(F_16, q[2]), q[3]); /* 001DDF24/001DDF28 */
        q[2] = em_ee_div_bits(q[2], F_FOUR);                  /* 001DDF34 */
    } else {
        u32 src[4];
        TRY(ldq(s, EM_RC_D_00810360, src));                   /* 00102948 copy */
        TRY(stq(s, c + EM_RC_CTX_DEPTH, src));
        TRY(ld32(s, EM_RC_D_00275690, &v690));
        TRY(st32(s, c + 0x2460u, em_ee_div_bits(F_24BIT, v690))); /* 001DDF74/001DDF7C */
        TRY(st32(s, c + 0x2464u, F_ONE));                     /* 001DDF84 */
        TRY(ldq(s, c + EM_RC_CTX_DEPTH, q));                  /* 00102948 copy */
        u32 in[4];
        memcpy(in, q, sizeof in);
        CALLW(0x001026A0u, s->workers.w_001026A0(s->workers.ctx, q, EM_RC_D_70003AC0, in));
        TRY(ld32(s, EM_RC_D_00275694, &v694));
        q[3] = em_ee_add_bits(q[3], v694);                    /* 001DDFC4 */
        q[2] = em_ee_div_bits(em_ee_mul_bits(F_16, q[2]), q[3]); /* 001DDFD8/001DDFDC */
        q[2] = em_ee_div_bits(q[2], F_FOUR);                  /* 001DDFE8 */
    }

    /* 001DDFF0..001DE0F8: the ease targets by the 0015D2F0 code. */
    u32 far, bias;
    if (mode == 0x82 || mode == 3 || mode == 2) {
        far = F_40500;
        bias = F_1500;
    } else if (mode == 1) {
        far = F_30500;
        bias = F_1500;
    } else {
        far = F_8500;
        bias = F_50;
    }
    TRY(ld32(s, EM_RC_D_00275690, &v690));
    TRY(st32(s, EM_RC_D_00275690, em_ee_add_bits(v690, em_ee_mul_bits(F_0_05, em_ee_sub_bits(far, v690)))));
    TRY(ld32(s, EM_RC_D_00275694, &v694));
    TRY(st32(s, EM_RC_D_00275694, em_ee_mul_bits(F_0_05, em_ee_sub_bits(bias, v694))));

    u32 base;
    TRY(ld32(s, c + EM_RC_CTX_CHAN3, &base));                 /* 001DE10C, sp+0xF0 */
    u32 value[4], width[4];                                   /* the two stack rows */
    int32_t r1 = 0, r2 = 0;
    for (int i = 0; i < 4; ++i) {                             /* 001DE11C */
        u32 flag7;
        TRY(em_render_context_001D2910(s, 7, &flag7));
        if (flag7 != 0) {
            TRY(ld32(s, c + 0x1F4u, &f1F4));
            const u32 f20 = em_ee_mul_bits(F_TWO, f1F4);      /* 001DE144 */
            static const u32 base_gain[4] = {0, F_FOUR, F_1_5, F_ONE};
            static const u32 slope[4] = {0, F_3_5, F_5_5, F_FIVE};
            TRY(ld32(s, c + 0x2460u, &t));
            u32 x;
            if (i == 0) {                                     /* 001DE170: 8 * d / 8 */
                x = ease_row(t, q[2], F_EIGHT);
            } else {                                          /* 001DE1A8 / 001DE1F4 / 001DE240 */
                const u32 gain = em_ee_add_bits(base_gain[i], em_ee_mul_bits(slope[i], f20));
                x = ease_row(t, q[2], gain);
            }
            CALLW(0x001281C0u, s->workers.w_001281C0(s->workers.ctx, x, &r1));
            /* 001DE294: 62 + 30 * f20 */
            CALLW(0x001281C0u, s->workers.w_001281C0(s->workers.ctx,
                                                    em_ee_add_bits(F_62, em_ee_mul_bits(F_30, f20)), &r2));
        } else if (v != 0) {                                  /* 001DE2B8 */
            static const u32 gain[4] = {F_FOUR, F_TWO, 0, F_HALF};
            TRY(ld32(s, c + 0x2460u, &t));
            CALLW(0x001281C0u, s->workers.w_001281C0(s->workers.ctx, ease_row(t, q[2], gain[i]), &r1));
            r2 = 0x3E;
        } else if (key == 0xB00u) {                           /* 001DE3EC */
            static const u32 gain[4] = {F_EIGHT, F_SEVEN, F_SIX, F_FIVE};
            static const int32_t w[4] = {0x18, 0x28, 0x38, 0x48};
            TRY(ld32(s, c + 0x2460u, &t));
            CALLW(0x001281C0u, s->workers.w_001281C0(s->workers.ctx, ease_row(t, q[2], gain[i]), &r1));
            r2 = w[i];
        } else {                                              /* 001DE530 */
            static const u32 gain[4] = {F_EIGHT, F_FOUR, F_1_5, 0};
            TRY(ld32(s, c + 0x2460u, &t));
            CALLW(0x001281C0u, s->workers.w_001281C0(s->workers.ctx, ease_row(t, q[2], gain[i]), &r1));
            r2 = 0x3E;
        }
        value[i] = em_ee_cvt_s_w_bits((u32)r1);               /* 001DE65C */
        width[i] = em_ee_cvt_s_w_bits((u32)r2);               /* 001DE66C */
    }

    u32 mode20;
    TRY(ld32(s, rp + 0x20u, &mode20));
    if (v == 0 && mode20 == 0) {                              /* 001DE680/001DE68C */
        for (int j = 0; j < 4; ++j) {                         /* 001DE6A8: 0.15 ease */
            u32 old;
            TRY(ld32(s, rp + 4u * (u32)j, &old));
            TRY(st32(s, rp + 4u * (u32)j,
                     em_ee_add_bits(old, em_ee_mul_bits(F_0_15, em_ee_sub_bits(value[j], old)))));
            TRY(ld32(s, rp + 0x10u + 4u * (u32)j, &old));
            TRY(st32(s, rp + 0x10u + 4u * (u32)j,
                     em_ee_add_bits(old, em_ee_mul_bits(F_0_15, em_ee_sub_bits(width[j], old)))));
        }
    } else {
        for (int j = 0; j < 4; ++j) {                         /* 001DE6FC: snap */
            TRY(st32(s, rp + 4u * (u32)j, value[j]));
            TRY(st32(s, rp + 0x10u + 4u * (u32)j, width[j]));
        }
    }
    TRY(st32(s, rp + 0x20u, (u32)v));                         /* 001DE6F4 / 001DE728 */

    for (int k = 0; k < 4; ++k) {                             /* 001DE730 */
        u32 x, tex, e;
        int32_t cc, rr;
        TRY(ld32(s, rp + 4u * (u32)k, &x));
        CALLW(0x001281C0u, s->workers.w_001281C0(s->workers.ctx, x, &cc));
        TRY(ld32(s, rp + 0x10u + 4u * (u32)k, &x));
        CALLW(0x001281C0u, s->workers.w_001281C0(s->workers.ctx, x, &rr));
        TRY(ld32(s, EM_RC_D_0027568C, &tex));
        TRY(em_render_context_001D6B10(s, 3, (int32_t)tex, 8, 8, NULL)); /* 001DE754 */
        TRY(ld32(s, EM_RC_D_0027568C, &tex));
        CALLW(0x001D6BA0u, s->workers.w_001D6BA0(s->workers.ctx, 3, (int32_t)tex, 8, 8, 0, 0)); /* 001DE770 */
        CALLW(0x001D1FF0u, s->workers.w_001D1FF0(s->workers.ctx, 3, 3)); /* 001DE77C */
        static const int32_t alpha[15] = {3, 0, 1, 0, 0, 1, 0, 0, 1, 2, 0, 1, 0, 1, 0};
        TRY(em_render_context_001D6C90(s, alpha, NULL));      /* 001DE7C0 */
        /* 001DE7C8..001DE894: one 0x80-byte GIF packet at the channel 3 cursor */
        TRY(ld32(s, c + EM_RC_CTX_CHAN3, &e));
        TRY(span(s, e, EM_RC_001DDE10_SPRITE_BYTES));
        TRY(st8(s, e + 3u, 0x10));                            /* 001DE7F4 */
        TRY(ld32(s, c + EM_RC_CTX_CHAN3, &e));
        TRY(st32(s, e + 4u, 0));                              /* 001DE808 */
        TRY(ld32(s, c + EM_RC_CTX_CHAN3, &e));
        TRY(st16(s, e, 7));                                   /* 001DE81C */
        TRY(ld32(s, c + EM_RC_CTX_CHAN3, &e));                /* 001DE820 */
        TRY(st32(s, c + EM_RC_CTX_CHAN3, e + EM_RC_001DDE10_SPRITE_BYTES)); /* 001DE834 */
        TRY(span(s, e, EM_RC_001DDE10_SPRITE_BYTES));
        static const u32 zero[4] = {0, 0, 0, 0};
        TRY(stq(s, e + 0x10u, zero));                         /* 001DE838 */
        TRY(st32(s, e + 0x1Cu, 0x50000006u));                 /* 001DE83C */
        TRY(st64(s, e + 0x20u, (u64)0x50AB4000u << 32 | 0x8001u)); /* 001DE840 */
        TRY(st64(s, e + 0x28u, 0x43431u));                    /* 001DE844 */
        TRY(st32(s, e + 0x38u, 0x80));                        /* 001DE848 */
        TRY(st32(s, e + 0x34u, 0x80));
        TRY(st32(s, e + 0x30u, 0x80));
        TRY(st32(s, e + 0x3Cu, (u32)rr));                     /* 001DE854 */
        TRY(st32(s, e + 0x40u, 8));
        TRY(st32(s, e + 0x44u, 8));
        TRY(st32(s, e + 0x50u, 0x7000));                      /* 001DE860 */
        TRY(st32(s, e + 0x54u, 0x7900));
        TRY(st32(s, e + 0x58u, (u32)cc));                     /* 001DE868 */
        TRY(st32(s, e + 0x5Cu, 0x80));
        TRY(st32(s, e + 0x60u, 0x1008));                      /* 001DE870 */
        TRY(st32(s, e + 0x64u, 0x1008));
        TRY(st32(s, e + 0x70u, 0x9000));                      /* 001DE87C */
        TRY(st32(s, e + 0x74u, 0x8700));
        TRY(st32(s, e + 0x78u, (u32)cc));                     /* 001DE884 */
        TRY(st32(s, e + 0x7Cu, 0x80));                        /* 001DE894 */
    }
    CALLW(0x001D1F20u, s->workers.w_001D1F20(s->workers.ctx, 3)); /* 001DE898 */
    /* 001DE8B8..001DE8E0: the end tag */
    u32 e;
    TRY(ld32(s, c + EM_RC_CTX_CHAN3, &e));
    TRY(span(s, e, 8));
    TRY(st8(s, e + 3u, 0x60));
    TRY(ld32(s, c + EM_RC_CTX_CHAN3, &e));
    TRY(st32(s, e + 4u, 0));
    TRY(ld32(s, c + EM_RC_CTX_CHAN3, &e));
    TRY(st16(s, e, 0));
    TRY(ld32(s, c + EM_RC_CTX_CHAN3, &e));
    TRY(st32(s, c + EM_RC_CTX_CHAN3, e + EM_RC_001DDE10_END_BYTES));
    /* 001DE8DC: 001CB760(D_007635C0, 0xFFF000, sp+0xF0) */
    CALLW(0x001CB760u, s->workers.w_001CB760(s->workers.ctx, EM_RC_D_007635C0, 0xFFF000, base));
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001DDAA0 / 001DDA00: the dispatch and the per-frame tick.           */
/* ------------------------------------------------------------------ */

int em_render_context_001DDAA0(S *s)
{
    if (!s || latched(s)) return -1;
    TRY(req_001DDAA0(s));
    u32 k0, k1;
    TRY(ld8(s, 0x00810700u, &k0));
    TRY(ld8(s, 0x00810701u, &k1));
    const u32 key = (k0 << 8) + k1;                           /* 001DDAA0.. */
    if (key == 0xB00u || key == 0xC00u || key == 0xD00u || key == 0xE00u) {
        u32 flags;
        CALLW(0x001B0070u, s->workers.w_001B0070(s->workers.ctx, &flags));
        if (flags & 0x60u) {
            CALLW(0x001DE920u, s->workers.w_001DE920(s->workers.ctx));
            return 0;
        }
    }
    return em_render_context_001DDE10(s);
}

int em_render_context_001DDA00(S *s)
{
    if (!s || latched(s)) return -1;
    const u32 c = s->world.ctx;
    TRY(req_001DEEE0(s, c + EM_RC_CTX_RAMP_A));
    TRY(req_001DEEE0(s, c + EM_RC_CTX_RAMP_B));
    NEEDW(w_0015D2F0, 0x0015D2F0u);
    NEEDW(w_001DDB70, 0x001DDB70u);
    NEEDW(w_001DFF70, 0x001DFF70u);
    TRY(span(s, EM_RC_D_008106C6, 1));
    TRY(req_001DDAA0(s));
    u32 test, b6;
    int32_t mode;
    TRY(em_render_context_001DEEE0(s, c + EM_RC_CTX_RAMP_A));
    TRY(em_render_context_001DEEE0(s, c + EM_RC_CTX_RAMP_B));
    TRY(em_render_context_001D2910(s, 1, &test));
    if (test != 0) TRY(em_render_context_001DDAA0(s));
    CALLW(0x0015D2F0u, s->workers.w_0015D2F0(s->workers.ctx, &mode));
    if (mode == 2) {
        TRY(ld8(s, EM_RC_D_008106C6, &b6));
        if (b6 == 2) {
            TRY(em_render_context_001D2910(s, 6, &test));
            if (test != 0) CALLW(0x001DDB70u, s->workers.w_001DDB70(s->workers.ctx));
        }
    }
    TRY(em_render_context_001D2910(s, 7, &test));
    if (test != 0) CALLW(0x001DFF70u, s->workers.w_001DFF70(s->workers.ctx));
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001E0C30 / 001E1010 / 001E0CC0 / 001E0D70 / 001E0DF0.              */
/* ------------------------------------------------------------------ */

int em_render_context_001E1010(S *s)
{
    if (!s || latched(s)) return -1;
    TRY(span(s, EM_RC_D_0081E0F0, 16u * 0x180u));
    u32 row = EM_RC_D_0081E0F0;
    for (int32_t i = 0; i < 16; ++i, row += 0x180u) {         /* 001E102C */
        const u32 fo = em_ee_div_bits(em_ee_mul_bits(F_256, em_ee_cvt_s_w_bits((u32)i)), F_16);
        u32 e = row;
        for (int32_t j = 0; j < 16; ++j, e += 0x18u) {        /* 001E104C */
            const u32 fi = em_ee_div_bits(em_ee_mul_bits(F_256, em_ee_cvt_s_w_bits((u32)j)), F_16);
            TRY(st32(s, e + 0x0u, fi));                       /* 001E1068 */
            TRY(st32(s, e + 0x4u, fo));                       /* 001E106C */
            TRY(st32(s, e + 0x8u, 0));
            TRY(st32(s, e + 0xCu, 0));
        }
    }
    return 0;
}

int em_render_context_001E0C30(S *s)
{
    if (!s || latched(s)) return -1;
    const u32 c = s->world.ctx;
    TRY(span(s, c + 0x170u, 8));
    TRY(span(s, EM_RC_D_0081E0F0, 16u * 0x180u));
    TRY(st8(s, c + 0x173u, 0));                               /* 001E0C34 */
    TRY(st8(s, c + 0x172u, 0));
    TRY(st8(s, c + 0x171u, 0));
    TRY(st8(s, c + 0x170u, 0));
    TRY(st32(s, c + EM_RC_CTX_FLAGS_HI, 0));                  /* 001E0C58 */
    return em_render_context_001E1010(s);                     /* j 001E1010 */
}

int em_render_context_001E0CC0(S *s)
{
    if (!s || latched(s)) return -1;
    const u32 c = s->world.ctx;
    TRY(req_001D2E00(s, 0));
    TRY(span(s, c + 0x1D8u, 4));
    TRY(span(s, c + 0x1E8u, 4));
    TRY(em_render_context_001D2DE0(s, 0, 0));
    TRY(st32(s, c + 0x1D8u, 0));
    return st32(s, c + 0x1E8u, 0);
}

int em_render_context_001E0D70(S *s)
{
    if (!s || latched(s)) return -1;
    NEEDW(w_001B0070, 0x001B0070u);
    NEEDW(w_001CB760, 0x001CB760u);
    TRY(req_001D2E00(s, 0));
    TRY(req_flags(s));
    u32 pending, test, flags;
    TRY(em_render_context_001D2E00(s, 0, &pending));          /* 001E0D7C */
    TRY(em_render_context_001D2910(s, 0x20, &test));          /* 001E0D88 */
    if (test == 0) return 0;
    CALLW(0x001B0070u, s->workers.w_001B0070(s->workers.ctx, &flags)); /* 001E0D98 */
    if (flags & 0x0E000000u) return 0;                        /* 001E0DA8 */
    if (pending == 0) return 0;                               /* 001E0DB0 */
    CALLW(0x001CB760u, s->workers.w_001CB760(s->workers.ctx, EM_RC_D_007635C0, 0xFFC000, pending));
    return em_render_context_001D2DE0(s, 0, 0);               /* 001E0DD4 */
}

int em_render_context_001E0DF0(S *s)
{
    if (!s || latched(s)) return -1;
    const u32 c = s->world.ctx;
    TRY(req_flags(s));
    TRY(req_001D2E00(s, 0));
    TRY(span(s, c + 0x1D8u, 4));
    TRY(span(s, c + 0x1E8u, 4));
    TRY(span(s, c + 8u, 4));
    u32 test, h, node;
    TRY(em_render_context_001D2910(s, 0x20, &test));
    if (test == 0) return 0;
    TRY(ld32(s, c + 0x1D8u, &h));
    if (h != 0) TRY(em_render_context_001D21B0(s, h));
    TRY(ld32(s, c + 0x1E8u, &h));
    if (h != 0) TRY(em_render_context_001D21B0(s, h));
    TRY(em_render_context_001D2E00(s, 0, &node));
    if (node != 0) {
        TRY(em_render_context_001D2910(s, 5, &test));
        if (test == 0) TRY(em_render_context_001D21B0(s, node));
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001D52E0: the grid header.                                          */
/* ------------------------------------------------------------------ */

int em_render_context_001D52E0(S *s)
{
    if (!s || latched(s)) return -1;
    const u32 c = s->world.ctx;
    NEEDW(w_001C6120, 0x001C6120u);
    TRY(span(s, EM_RC_D_0028A5A0, 4));
    TRY(span(s, c + EM_RC_CTX_GRID, 0x28));
    u32 bank, e, v;
    TRY(ld32(s, EM_RC_D_0028A5A0, &bank));                    /* 001D52EC */
    CALLW(0x001C6120u, s->workers.w_001C6120(s->workers.ctx, bank, 0, &e)); /* 001D52F0 */
    TRY(span(s, e, 0x20));
    TRY(ld32(s, e + 0x0u, &v)); TRY(st32(s, c + 0x144u, v));  /* 001D52F8/001D5304 */
    TRY(ld32(s, e + 0x4u, &v)); TRY(st32(s, c + 0x148u, v));  /* 001D5308/001D5310 */
    TRY(ld32(s, e + 0x8u, &v)); TRY(st32(s, c + 0x150u, v));  /* 001D5314/001D531C */
    TRY(ld32(s, e + 0xCu, &v)); TRY(st32(s, c + 0x154u, v));
    TRY(ld32(s, e + 0x10u, &v)); TRY(st32(s, c + 0x158u, v));
    TRY(ld32(s, e + 0x14u, &v)); TRY(st32(s, c + 0x15Cu, v));
    TRY(ld32(s, e + 0x18u, &v)); TRY(st32(s, c + 0x160u, v));
    TRY(ld32(s, e + 0x1Cu, &v)); TRY(st32(s, c + 0x164u, v)); /* 001D5350/001D5358 */
    return st32(s, c + EM_RC_CTX_GRID, e + 0x20u);            /* 001D5360 */
}

/* ------------------------------------------------------------------ */
/* 001D5370: the grid pass.                                           */
/* ------------------------------------------------------------------ */

int em_render_context_clipw(const uint32_t v[4], uint32_t *flags)
{
    for (int i = 0; i < 4; ++i)
        if (((v[i] >> 23) & 0xFFu) == 0xFFu) return -1;
    const u32 w = em_eei_daz(v[3]) & 0x7FFFFFFFu;
    u32 f = 0;
    for (int k = 0; k < 3; ++k) {
        const u32 x = em_eei_daz(v[k]);
        if ((x & 0x7FFFFFFFu) > w) f |= (x >> 31) ? 2u << (2 * k) : 1u << (2 * k);
    }
    *flags = f;
    return 0;
}

static const u32 VF0[4] = {0, 0, 0, F_ONE};

/* One corner: ACC = m0 * px.y; ACC += m1 * py.z; ACC += m2 * pz.w;
 * vf3 = ACC + m3 * vf0.w (the four-instruction chain at 001D5598 and its
 * seven siblings), then its clip flags. */
static int corner(S *s, const u32 m[4][4], const u32 *px, const u32 *py, const u32 *pz, u32 *flags)
{
    u32 acc[4] = {0, 0, 0, 0}, out[4] = {0, 0, 0, 0};
    if (em_vu_vec_bits(EM_VU_MULABC, 15, 1, m[0], px, 0, NULL, acc) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MADDABC, 15, 2, m[1], py, 0, acc, acc) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MADDABC, 15, 3, m[2], pz, 0, acc, acc) != EM_EE_FLOAT_OK ||
        em_vu_vec_bits(EM_VU_MADDBC, 15, 3, m[3], VF0, 0, acc, out) != EM_EE_FLOAT_OK)
        return fault(s, 0x001D5370u, EM_RC_FAULT_UNMEASURED);
    if (em_render_context_clipw(out, flags) < 0) return fault(s, 0x001D5370u, EM_RC_FAULT_UNMEASURED);
    return 0;
}

/* Two runs of four corners, each read back as one 24-bit CLIP word (the
 * register shifts six bits per test, so a read after four tests holds the
 * first corner in bits 18..23 and the last in bits 0..5). Run A keeps the
 * z source at vf1 (lo), run B at vf2 (hi). */
static int clip8(S *s, const u32 m[4][4], const u32 v1[4], const u32 v2[4], u32 *lo, u32 *hi)
{
    u32 f[8];
    TRY(corner(s, m, v1, v1, v1, &f[0]));
    TRY(corner(s, m, v2, v1, v1, &f[1]));
    TRY(corner(s, m, v1, v2, v1, &f[2]));
    TRY(corner(s, m, v2, v2, v1, &f[3]));
    TRY(corner(s, m, v1, v1, v2, &f[4]));
    TRY(corner(s, m, v2, v1, v2, &f[5]));
    TRY(corner(s, m, v1, v2, v2, &f[6]));
    TRY(corner(s, m, v2, v2, v2, &f[7]));
    *lo = (f[0] << 18 | f[1] << 12 | f[2] << 6 | f[3]) & 0xFFFFFFu;
    *hi = (f[4] << 18 | f[5] << 12 | f[6] << 6 | f[7]) & 0xFFFFFFu;
    return 0;
}

static int load_matrix(S *s, u32 a, u32 m[4][4])
{
    for (u32 r = 0; r < 4; ++r) TRY(ldq(s, a + 0x10u * r, m[r]));
    return 0;
}

int em_render_context_001D5370(S *s)
{
    if (!s || latched(s)) return -1;
    const u32 c = s->world.ctx;
    NEEDW(w_001D4DA0, 0x001D4DA0u);
    NEEDW(w_001D2D20, 0x001D2D20u);
    NEEDW(w_001026D0, 0x001026D0u);
    NEEDW(w_001C6120, 0x001C6120u);
    NEEDW(w_001D4FB0, 0x001D4FB0u);
    NEEDW(w_001D1F80, 0x001D1F80u);
    NEEDW(w_001D4B20, 0x001D4B20u);
    NEEDW(w_001D5BD0, 0x001D5BD0u);
    TRY(span(s, c + EM_RC_CTX_GRID, 0xC));
    TRY(span(s, c + 0x2468u, 4));
    TRY(span(s, EM_RC_D_00810700, 3));
    TRY(span(s, EM_RC_D_0028A5A0, 4));
    TRY(span(s, EM_RC_D_70003400, 0x80));

    u32 grid, stride, k0, k1, room, zoom;
    TRY(ld32(s, c + EM_RC_CTX_GRID, &grid));                  /* 001D53B0 */
    TRY(ld32(s, c + 0x148u, &stride));                        /* 001D53B4 */
    TRY(ld8(s, 0x00810700u, &k0));
    TRY(ld8(s, 0x00810701u, &k1));
    const u32 key = (k0 << 8) + k1;                           /* 001D53CC */
    /* x = the grid row multiplied by the stride, z = the cell in the row */
    int32_t x0 = 0, x1 = 0x20, z0 = 0, z1 = 0x20;             /* the four loop bounds */
    if (key == 0x1300u) {                                     /* 001D53D0 */
        TRY(ld8(s, 0x00810702u, &room));
        if (room == 8 || room == 7 || room == 9 || room == 5) { /* 001D5424..001D5448 */
            x1 = 0x1E; z0 = 0xC; z1 = 0x20; x0 = 0;
        } else if (room == 4) {                               /* 001D5454 */
            z1 = 0x19; x0 = 9;
        }
    } else if (key == 0xD00u) {                               /* 001D53DC */
        TRY(ld8(s, 0x00810702u, &room));
        if (room < 4) {                                       /* 001D53F4 */
            x0 = 0x14; x1 = 0x17; z0 = 0xF; z1 = 0x17;
        }
    }
    CALLW(0x001D4DA0u, s->workers.w_001D4DA0(s->workers.ctx)); /* 001D5480 */
    TRY(ld32(s, c + 0x2468u, &zoom));                         /* 001D54AC */
    CALLW(0x001D2D20u, s->workers.w_001D2D20(s->workers.ctx, EM_RC_D_70003440, zoom, F_4096, F_4096,
                                             F_TENTH, F_FAR));
    CALLW(0x001026D0u, s->workers.w_001026D0(s->workers.ctx, EM_RC_D_70003440, EM_RC_D_70003440,
                                             EM_RC_D_00810610));
    TRY(ld32(s, c + 0x2468u, &zoom));                         /* 001D54F8 */
    CALLW(0x001D2D20u, s->workers.w_001D2D20(s->workers.ctx, EM_RC_D_70003400, zoom, F_1024, F_448,
                                             F_TENTH, F_FAR));
    CALLW(0x001026D0u, s->workers.w_001026D0(s->workers.ctx, EM_RC_D_70003400, EM_RC_D_70003400,
                                             EM_RC_D_00810610));
    u32 m[4][4];
    TRY(load_matrix(s, EM_RC_D_70003400, m));                 /* 001D5534 */

    for (int32_t iz = z0; iz < z1; ++iz) {                    /* 001D5940 */
        for (int32_t ix = x0; ix < x1; ++ix) {                /* 001D5930 */
            for (int32_t slot = 0; slot < 4; ++slot) {        /* 001D5920 */
                /* 001D555C..001D5570: grid + (((ix * stride + iz) * 4 + slot) * 4) */
                const u32 cell = ((((u32)ix * stride) + (u32)iz) << 2) + (u32)slot;
                u32 id, bank, obj;
                TRY(ld32(s, grid + (cell << 2), &id));
                if ((int32_t)id <= 0) continue;               /* 001D5574 */
                TRY(ld32(s, EM_RC_D_0028A5A0, &bank));        /* 001D5584 */
                CALLW(0x001C6120u, s->workers.w_001C6120(s->workers.ctx, bank, (int32_t)id, &obj));
                u32 v1[4], v2[4], lo, hi;
                TRY(ldq(s, obj + 0x10u, v1));                 /* 001D5590 */
                TRY(ldq(s, obj + 0x20u, v2));                 /* 001D5594 */
                TRY(clip8(s, (const u32(*)[4])m, v1, v2, &lo, &hi));
                /* 001D56EC..001D571C: all eight corners outside one plane */
                u32 both = lo & hi;
                both = (both & (both >> 6)) & ((both >> 12) & (both >> 18));
                both &= 0x3Fu;
                if (((both ^ (both << 1)) & 0x2Au) != 0) continue;
                if (((lo | hi) & 0xFFFFFFu) == 0) {           /* 001D5724 */
                    CALLW(0x001D4FB0u, s->workers.w_001D4FB0(s->workers.ctx, obj));
                    continue;                                 /* no matrix reload */
                }
                u32 g[4][4];
                TRY(load_matrix(s, EM_RC_D_70003440, g));     /* 001D5744 */
                TRY(clip8(s, (const u32(*)[4])g, v1, v2, &lo, &hi));
                if (((lo | hi) & 0xFFFFFFu) == 0) {           /* 001D58B0 */
                    CALLW(0x001D4FB0u, s->workers.w_001D4FB0(s->workers.ctx, obj));
                } else {
                    CALLW(0x001D4FB0u, s->workers.w_001D4FB0(s->workers.ctx, obj));
                    CALLW(0x001D1F80u, s->workers.w_001D1F80(s->workers.ctx, 0, 1, 0));
                    CALLW(0x001D4B20u, s->workers.w_001D4B20(s->workers.ctx, obj));
                    CALLW(0x001D4DA0u, s->workers.w_001D4DA0(s->workers.ctx));
                }
                TRY(load_matrix(s, EM_RC_D_70003400, m));     /* 001D58F8 */
            }
        }
    }
    switch (key) {                                            /* 001D594C..001D5A28 */
    case 0x1500u: case 0x1100u: case 0x1001u: case 0x1000u: case 0x0F00u:
    case 0x0803u: case 0x0806u: case 0x0801u: case 0x0805u: case 0x0800u:
    case 0x0703u: case 0x0700u: case 0x0601u: case 0x0600u: case 0x0401u:
    case 0x0400u: case 0x0301u: case 0x0101u: case 0x0100u:
        CALLW(0x001D5BD0u, s->workers.w_001D5BD0(s->workers.ctx)); /* 001D5A38 */
        break;
    default:
        break;
    }
    return 0;
}
