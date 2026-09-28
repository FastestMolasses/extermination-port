/* Static world packet builders and the background channel (lane STATIC).
 * See em_static_world.h and docs/STATIC_WORLD.md. Each store and call below
 * cites the original address it comes from; the code follows the original
 * order of loads, stores and calls, including the reload of the channel
 * cursor word before every tag field it writes. */
#include "game/em_static_world.h"
#include "game/em_owner_draw_original.h"

#include <stddef.h>
#include <string.h>

#include "game/em_camera_commit_original.h"
#include "game/em_ee_float.h"
#include "game/em_sdk_vu0.h"

typedef uint32_t u32;
typedef uint64_t u64;
typedef EmStaticWorld S;

/* binary32 constants the originals load. */
#define F_ONE 0x3F800000u
#define F_M_ONE 0xBF800000u
#define F_TWO 0x40000000u
#define F_TEN 0x41200000u
#define F_16 0x41800000u
#define F_31 0x41F80000u
#define F_32 0x42000000u
#define F_128 0x43000000u
#define F_224 0x43600000u
#define F_256 0x43800000u
#define F_384 0x43C00000u
#define F_512 0x44000000u
#define F_65535 0x477FFF00u
#define F_M112 0xC2E00000u
#define F_M256 0xC3800000u
#define F_RAND 0x30000000u   /* 2^-31 */
#define F_0_002 0x3B03126Fu

/* ------------------------------------------------------------------ */
/* Fault latch, trace, original-address memory.                       */
/* ------------------------------------------------------------------ */

static int latched(const S *s) { return s->fault.code != EM_SW_FAULT_NONE; }

static int fault(S *s, int32_t code, u32 detail)
{
    if (!latched(s)) {
        s->fault.address = s->fn;
        s->fault.code = code;
        s->fault.detail = detail;
    }
    return -1;
}

/* The test hook (em_static_world.h): integer arguments first, then the
 * float argument registers (f12, f13) as raw bits. */
static void trace6(S *s, u32 address, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5)
{
    if (s->trace) {
        const u64 args[6] = {a0, a1, a2, a3, a4, a5};
        s->trace(s->trace_ctx, address, args);
    }
}
static void trace(S *s, u32 address, u64 a0, u64 a1, u64 a2, u64 a3)
{
    trace6(s, address, a0, a1, a2, a3, 0, 0);
}

/* sign-extended register image of a 32-bit value (for the trace) */
static u64 sx(u32 v) { return (u64)(int64_t)(int32_t)v; }

/* The host bytes of [a, a + n): the first view that covers them. A store
 * into a view marked read-only (EmStaticWorld.read_only) faults. */
static uint8_t *mem_at(S *s, u32 a, u32 n, u32 align, int write)
{
    if (align > 1 && (a & (align - 1u)) != 0) {
        fault(s, EM_SW_FAULT_BAD_ADDRESS, a);
        return NULL;
    }
    for (u32 i = 0; s->views && i < s->view_count; ++i) {
        const EmStaticWorldView *v = &s->views[i];
        if (v->bytes && a >= v->address && (u64)a + n <= (u64)v->address + v->size) {
            if (write && s->read_only && s->read_only[i]) {
                fault(s, EM_SW_FAULT_READ_ONLY, a);
                return NULL;
            }
            return v->bytes + (a - v->address);
        }
    }
    fault(s, EM_SW_FAULT_BAD_ADDRESS, a);
    return NULL;
}
static uint8_t *mem(S *s, u32 a, u32 n, u32 align) { return mem_at(s, a, n, align, 0); }
static uint8_t *memw(S *s, u32 a, u32 n, u32 align) { return mem_at(s, a, n, align, 1); }

static int span(S *s, u32 a, u32 n) { return mem(s, a, n, 1) ? 0 : -1; }

static u32 get32(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void put32(uint8_t *p, u32 v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static int ld32(S *s, u32 a, u32 *v)
{
    uint8_t *p = mem(s, a, 4, 4);
    if (!p) return -1;
    *v = get32(p);
    return 0;
}
static int ld64(S *s, u32 a, u64 *v)
{
    uint8_t *p = mem(s, a, 8, 8);
    if (!p) return -1;
    *v = (u64)get32(p) | (u64)get32(p + 4) << 32;
    return 0;
}
static int st8(S *s, u32 a, u32 v)
{
    uint8_t *p = memw(s, a, 1, 1);
    if (!p) return -1;
    p[0] = (uint8_t)v;
    return 0;
}
static int st16(S *s, u32 a, u32 v)
{
    uint8_t *p = memw(s, a, 2, 2);
    if (!p) return -1;
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    return 0;
}
static int st32(S *s, u32 a, u32 v)
{
    uint8_t *p = memw(s, a, 4, 4);
    if (!p) return -1;
    put32(p, v);
    return 0;
}
static int st64(S *s, u32 a, u64 v)
{
    uint8_t *p = memw(s, a, 8, 8);
    if (!p) return -1;
    put32(p, (u32)v);
    put32(p + 4, (u32)(v >> 32));
    return 0;
}
/* Quadword load / store: the low four address bits are ignored. */
static int ldq(S *s, u32 a, u32 out[4])
{
    uint8_t *p = mem(s, a & ~15u, 16, 1);
    if (!p) return -1;
    for (int i = 0; i < 4; ++i) out[i] = get32(p + 4 * i);
    return 0;
}
static int stq(S *s, u32 a, const u32 v[4])
{
    uint8_t *p = memw(s, a & ~15u, 16, 1);
    if (!p) return -1;
    for (int i = 0; i < 4; ++i) put32(p + 4 * i, v[i]);
    return 0;
}
static const u32 ZERO4[4] = {0, 0, 0, 0};

#define TRY(x) do { if ((x) < 0) return -1; } while (0)
#define W (&s->workers)
#define NEEDW(field, address) do { if (!W->field) return fault(s, EM_SW_FAULT_NULL_WORKER, (address)); } while (0)
#define CALLW(address, expr) do { if ((expr) < 0) return fault(s, EM_SW_FAULT_WORKER_FAILED, (address)); } while (0)

/* Each entry: no state, or a latched fault -> -1; record the function for
 * fault attribution; LEAVE and every early return (ETRY, ENEEDW) restore
 * the caller's. */
#define ENTER(address) \
    if (!s || latched(s)) return -1; \
    const u32 saved_fn_ = s->fn; \
    s->fn = (address)
#define LEAVE(rc) do { int rc_ = (rc); s->fn = saved_fn_; return rc_; } while (0)
/* Early returns between ENTER and LEAVE restore the caller's function too
 * (the fault, if any, is already latched and attributed to this one). */
#define ETRY(x) do { if ((x) < 0) { s->fn = saved_fn_; return -1; } } while (0)
#define ENEEDW(field, address) \
    do { if (!W->field) { fault(s, EM_SW_FAULT_NULL_WORKER, (address)); s->fn = saved_fn_; return -1; } } while (0)

static int context(S *s, u32 *ctx)
{
    return ld32(s, EM_SW_D_00275670, ctx);
}

/* The channel cursor word: context + 0x10 + 4 * chan (32-bit wrap). */
static int cursor_word(S *s, int32_t chan, u32 *word)
{
    u32 ctx;
    TRY(context(s, &ctx));
    *word = ctx + ((u32)chan << 2) + 0x10u;
    return 0;
}

/* One DMA tag at the cursor held in `word`: byte +3, word +4, halfword +0
 * (each after its own reload of the cursor), then the cursor advances by
 * `advance` (reloaded once more). Bytes +2 and +8..+0xF are left as they
 * are. The returned *at is the cursor the halfword store used. */
static int tag(S *s, u32 word, u32 id, u32 address, u32 qwc, u32 advance, u32 *at)
{
    u32 c;
    TRY(ld32(s, word, &c));
    TRY(span(s, c, 0x10));
    TRY(st8(s, c + 3u, id));
    TRY(ld32(s, word, &c));
    TRY(st32(s, c + 4u, address));
    TRY(ld32(s, word, &c));
    TRY(st16(s, c, qwc));
    TRY(ld32(s, word, &c));
    if (at) *at = c;
    return st32(s, word, c + advance);
}

/* ------------------------------------------------------------------ */
/* Leaves: 00102958, 00121870, 00102798, 001026D0, 001C6120.          */
/* ------------------------------------------------------------------ */

/* 00102958 (copy_qw4): four quadwords, all loaded before any store. */
static int copy_qw4(S *s, u32 dst, u32 src)
{
    u32 q[4][4];
    for (u32 r = 0; r < 4; ++r) TRY(ldq(s, src + 0x10u * r, q[r]));
    for (u32 r = 0; r < 4; ++r) TRY(stq(s, dst + 0x10u * r, q[r]));
    return 0;
}

int em_static_world_00102958(S *s, u32 dst, u32 src)
{
    ENTER(0x00102958u);
    trace(s, 0x00102958u, sx(dst), sx(src), 0, 0);
    LEAVE(copy_qw4(s, dst, src));
}

/* block_copy(dst, src, n, a3): a3 is not read. n < 0x20 or either address
 * not 16-aligned: bytes. Otherwise pairs of quadwords while n >= 0x20 (the
 * loop tests after the pair), then doublewords while n >= 8, then bytes. */
static int block_copy(S *s, u32 dst, u32 src, u32 n)
{
    if (n >= 0x20u && ((src | dst) & 15u) == 0) {
        do {
            u32 q[4];
            TRY(ldq(s, src, q)); n -= 0x20u; src += 0x10u;
            TRY(stq(s, dst, q)); dst += 0x10u;
            TRY(ldq(s, src, q)); src += 0x10u;
            TRY(stq(s, dst, q)); dst += 0x10u;
        } while (n >= 0x20u);
        while (n >= 8u) {
            uint8_t *p = mem(s, src, 8, 8);
            if (!p) return -1;
            uint8_t d[8];
            memcpy(d, p, 8);
            n -= 8u; src += 8u;
            uint8_t *q = memw(s, dst, 8, 8);
            if (!q) return -1;
            memcpy(q, d, 8);
            dst += 8u;
        }
    }
    while (n-- != 0u) {
        uint8_t *p = mem(s, src, 1, 1);
        if (!p) return -1;
        uint8_t b = *p;
        src += 1u;
        TRY(st8(s, dst, b));
        dst += 1u;
    }
    return 0;
}

int em_static_world_00121870(S *s, u32 dst, u32 src, u32 n)
{
    ENTER(0x00121870u);
    trace(s, 0x00121870u, sx(dst), sx(src), sx(n), 0);
    LEAVE(block_copy(s, dst, src, n));
}

/* 00102798 over memory: the four rows are loaded, em_camera_commit_00102798
 * (the port's verified translation) transposes them, the four rows are
 * stored (dst may be src). */
static int transpose(S *s, u32 dst, u32 src)
{
    u32 m[16], out[16];
    for (u32 r = 0; r < 4; ++r) TRY(ldq(s, src + 0x10u * r, m + 4 * r));
    em_camera_commit_00102798(out, m);
    for (u32 r = 0; r < 4; ++r) TRY(stq(s, dst + 0x10u * r, out + 4 * r));
    return 0;
}

int em_static_world_00102798(S *s, u32 dst, u32 src)
{
    ENTER(0x00102798u);
    trace(s, 0x00102798u, sx(dst), sx(src), 0, 0);
    LEAVE(transpose(s, dst, src));
}

/* 001026D0 over memory: the four rows of a are loaded first; then for
 * each r the row r of b is loaded and output row r (em_sdk_vu0_001026D0's
 * arithmetic, which uses only that row of b) is stored before the next row
 * of b is loaded, so dst may be a or b. */
static int product(S *s, u32 dst, u32 a, const u32 *a_host, u32 b)
{
    u32 am[16];
    if (a_host) memcpy(am, a_host, sizeof am);
    else for (u32 r = 0; r < 4; ++r) TRY(ldq(s, a + 0x10u * r, am + 4 * r));
    for (u32 r = 0; r < 4; ++r) {
        u32 v[4], bm[16], out[16];
        TRY(ldq(s, b + 0x10u * r, v));
        for (u32 k = 0; k < 4; ++k) memcpy(bm + 4 * k, v, sizeof v);
        if (em_sdk_vu0_001026D0(out, am, bm) != EM_EE_FLOAT_OK)
            return fault(s, EM_SW_FAULT_UNMEASURED, 0x001026D0u);
        TRY(stq(s, dst + 0x10u * r, out));
    }
    return 0;
}

int em_static_world_001026D0(S *s, u32 dst, u32 a, u32 b)
{
    ENTER(0x001026D0u);
    trace(s, 0x001026D0u, sx(dst), sx(a), sx(b), 0);
    LEAVE(product(s, dst, a, NULL, b));
}

int em_static_world_001C6120(S *s, u32 bank, int32_t id, u32 *result)
{
    ENTER(0x001C6120u);
    trace(s, 0x001C6120u, sx(bank), sx((u32)id), 0, 0);
    const u32 index = (u32)id & 0xFFFFu & 0xFFFF7FFFu;
    u32 word;
    if (ld32(s, (index << 2) + bank + 4u, &word) < 0) LEAVE(-1);
    const u32 entry = bank + (u32)(((int32_t)word >> 2) << 2);
    if (result) *result = entry;
    LEAVE(0);
}

/* ------------------------------------------------------------------ */
/* Channel builders: 001D2090, 001D4750, 001D6F60, 001D7000, 001D7100, */
/* 001D71A0.                                                          */
/* ------------------------------------------------------------------ */

/* 001D2090 is em_owner_draw_vif_append_ref_tag, the one translation (the
 * owner draw's and the face attachment's callers use it too): this memory
 * form checks what it touches over the views (the context and block
 * words, the channel's cursor word, the 0x20 bytes of the two tags and the
 * context +0x50 + 4 chan word, all writable) and runs it on those host
 * bytes: REF 1 qword to *D_00275674, +0x50 + 4 chan = target, CALL target;
 * then the cursor advances past both tags. A view or alignment fault is
 * therefore raised before the first tag byte is written. */
static int ref_tag(S *s, int32_t chan, u32 target)
{
    u32 ctx, word, block, c;
    if (chan < 0 || chan > 3) return fault(s, EM_SW_FAULT_BAD_ADDRESS, (u32)chan);
    TRY(context(s, &ctx));
    word = ctx + ((u32)chan << 2) + 0x10u;
    TRY(ld32(s, EM_SW_D_00275674, &block));                   /* 001D2098 */
    TRY(ld32(s, word, &c));
    uint8_t *packet = memw(s, c, 0x20u, 4);                    /* 001D20B4..001D2108 */
    if (!packet) return -1;
    uint8_t *call = memw(s, ctx + 0x50u + ((u32)chan << 2), 4, 4);   /* 001D20DC */
    if (!call || !memw(s, word, 4, 4)) return -1;
    EmOwnerServicesChannel channel[4];
    memset(channel, 0, sizeof channel);
    channel[chan].cursor = packet;
    channel[chan].end = packet + 0x20u;
    EmOwnerDrawWorld w;
    memset(&w, 0, sizeof w);
    w.channel = channel;
    w.channel_count = 4u;
    w.d00275674 = &block;
    w.ctx_50 = (uint32_t *)(void *)(call - 4u * (u32)chan);
    w.ctx_50_count = 4u;
    em_owner_draw_vif_append_ref_tag(&w, chan, target);
    return st32(s, word, c + 0x20u);
}

int em_static_world_001D2090(S *s, int32_t chan, u32 target)
{
    ENTER(0x001D2090u);
    trace(s, 0x001D2090u, sx((u32)chan), sx(target), 0, 0);
    ETRY(span(s, EM_SW_D_00275670, 8));
    LEAVE(ref_tag(s, chan, target));
}

/* 001D4750's constant block (16 words each; +0x10..+0x1F of a row). */
static const u32 K_4750[32] = {
    F_ONE, 0, 0, 0,   0, F_M_ONE, 0, 0,   0, F_ONE, 0, 0,   0, 0, 0, F_ONE,
    F_32, F_32, F_32, 0,   0, 0, 0, 0,   F_32, F_32, F_32, 0,
    0x4B000040u, 0x4B000040u, 0x4B000040u, 0x4B000080u,
};

static int unpack_const(S *s, int32_t chan)
{
    for (u32 i = 0; i < 32; ++i)                               /* 001D4764..001D4874 */
        TRY(st32(s, EM_SW_D_00817240 + 4u * i, K_4750[i]));
    u32 word, c;
    TRY(cursor_word(s, chan, &word));                          /* 001D487C */
    /* 9 qwords: FLUSH, UNPACK V4-32 of 8 qwords to VU address 0. */
    TRY(tag(s, word, 0x10u, 0u, 9u, 0xA0u, &c));               /* 001D48A8..001D48C8 */
    TRY(span(s, c, 0xA0));
    TRY(stq(s, c + 0x10u, ZERO4));                             /* 001D48CC */
    TRY(st32(s, c + 0x18u, 0x11000000u));                      /* 001D48D0 */
    TRY(st32(s, c + 0x1Cu, 0x6C080000u));                      /* 001D48E0 */
    TRY(em_static_world_00102958(s, c + 0x20u, EM_SW_D_70003AC0));
    TRY(em_static_world_00102958(s, c + 0x60u, EM_SW_D_00817240));
    /* 5 qwords: UNPACK V4-32 of 4 qwords to VU address 0x3F5, flag 0x0400. */
    TRY(cursor_word(s, chan, &word));                          /* 001D48F4 */
    TRY(tag(s, word, 0x10u, 0u, 5u, 0x60u, &c));               /* 001D4918..001D493C */
    TRY(span(s, c, 0x60));
    TRY(stq(s, c + 0x10u, ZERO4));                             /* 001D4940 */
    TRY(st32(s, c + 0x1Cu, 0x6C0403F5u));                      /* 001D4948 */
    TRY(em_static_world_00102958(s, c + 0x20u, EM_SW_D_00817240 + 0x40u));
    return 0;
}

int em_static_world_001D4750(S *s, int32_t chan)
{
    ENTER(0x001D4750u);
    trace(s, 0x001D4750u, sx((u32)chan), 0, 0, 0);
    ETRY(span(s, EM_SW_D_00275670, 4));
    ETRY(span(s, EM_SW_D_00817240, 0x80));
    ETRY(span(s, EM_SW_D_70003AC0, 0x40));
    LEAVE(unpack_const(s, chan));
}

/* The skin-record REF at channel 0 (001D4960 / 001D4DA0 tails):
 * D_00816440 + (context +0x9C << 7), qwc 8; the context word is loaded once. */
static int skin_ref(S *s)
{
    u32 ctx, slot, word;
    TRY(context(s, &ctx));
    TRY(ld32(s, ctx + 0x9Cu, &slot));
    word = ctx + 0x10u;
    return tag(s, word, 0x30u, EM_SW_D_00816440 + (slot << 7), 8u, 0x10u, NULL);
}

static int d4960(S *s)
{
    TRY(em_static_world_001D4750(s, 0));                       /* 001D4758 */
    TRY(em_static_world_001D2090(s, 0, EM_SW_D_00239C90));     /* 001D4768 */
    return skin_ref(s);                                        /* 001D4774..001D47AC */
}

int em_static_world_001D4960(S *s, u32 a0)
{
    ENTER(0x001D4960u);
    trace(s, 0x001D4960u, sx(a0), 0, 0, 0);
    ETRY(span(s, EM_SW_D_00275670, 4));
    LEAVE(d4960(s));
}

static int d4da0(S *s)
{
    TRY(em_static_world_001D4750(s, 0));                       /* 001D4DAC */
    TRY(em_static_world_001D2090(s, 0, EM_SW_D_00237180));     /* 001D4DBC */
    trace(s, 0x001D1F80u, 0, 1, 0, 0);
    CALLW(0x001D1F80u, W->w_001D1F80(W->ctx, 0, 1, 0));        /* 001D4DC8 */
    return skin_ref(s);                                        /* 001D4DD0..001D4E10 */
}

int em_static_world_001D4DA0(S *s)
{
    ENTER(0x001D4DA0u);
    trace(s, 0x001D4DA0u, 0, 0, 0, 0);
    ENEEDW(w_001D1F80, 0x001D1F80u);
    ETRY(span(s, EM_SW_D_00275670, 4));
    LEAVE(d4da0(s));
}

/* 001D4F30 / 001D4A90: runs of at most 0x1F8 blocks of 0x820 bytes. */
static int runs(S *s, int32_t chan, u32 obj)
{
    u32 count;
    TRY(ld32(s, obj, &count));                                 /* 001D4F30 */
    int32_t left = (int32_t)count;
    u32 at = obj + 0x40u;
    if (left <= 0) return 0;                                   /* 001D4F34 */
    do {
        const int32_t n = left < 0x1F8 ? left : 0x1F8;         /* 001D4F44..001D4F54 */
        const u32 n65 = (u32)n * 65u;
        u32 word;
        TRY(cursor_word(s, chan, &word));                      /* 001D4F60 */
        left = (int32_t)((u32)left - 0x1F8u);                  /* 001D4F7C */
        TRY(tag(s, word, 0x30u, at, (n65 << 1) & 0xFFFFu, 0x10u, NULL));
        at += n65 << 5;                                        /* 001D4F88 */
    } while (left > 0);                                        /* 001D4FA0 */
    return 0;
}

int em_static_world_001D4F30(S *s, int32_t chan, u32 obj)
{
    ENTER(0x001D4F30u);
    trace(s, 0x001D4F30u, sx((u32)chan), sx(obj), 0, 0);
    ETRY(span(s, EM_SW_D_00275670, 4));
    LEAVE(runs(s, chan, obj));
}

int em_static_world_001D4A90(S *s, int32_t chan, u32 obj)
{
    ENTER(0x001D4A90u);
    trace(s, 0x001D4A90u, sx((u32)chan), sx(obj), 0, 0);
    ETRY(span(s, EM_SW_D_00275670, 4));
    LEAVE(runs(s, chan, obj));
}

int em_static_world_001D4FB0(S *s, u32 obj)
{
    ENTER(0x001D4FB0u);
    trace(s, 0x001D4FB0u, sx(obj), 0, 0, 0);
    LEAVE(em_static_world_001D4F30(s, 0, obj));
}

int em_static_world_001D4B10(S *s, u32 obj)
{
    ENTER(0x001D4B10u);
    trace(s, 0x001D4B10u, sx(obj), 0, 0, 0);
    LEAVE(em_static_world_001D4A90(s, 0, obj));
}

int em_static_world_001D4B20(S *s, u32 obj)
{
    ENTER(0x001D4B20u);
    trace(s, 0x001D4B20u, sx(obj), 0, 0, 0);
    ETRY(em_static_world_001D4960(s, obj));                     /* 001D4B2C */
    LEAVE(em_static_world_001D4B10(s, obj));                   /* 001D4B34 */
}

/* 001D6F60(chan, tex0, a2): CNT 5; DIRECT 4; GIF A+D tag of 3 registers;
 * TEXFLUSH (0x3F) 0, TEX0_1 (6) = tex0, TEXA (0x3B) = a2. */
static int tex0_packet(S *s, int32_t chan, u64 tex0, int32_t a2)
{
    u32 word, c;
    TRY(cursor_word(s, chan, &word));
    TRY(tag(s, word, 0x10u, 0u, 5u, 0x60u, &c));               /* 001D6FB0..001D6FDC */
    TRY(span(s, c, 0x60));
    TRY(stq(s, c + 0x10u, ZERO4));                             /* 001D6FE0 */
    TRY(st32(s, c + 0x1Cu, 0x50000004u));
    TRY(st64(s, c + 0x20u, UINT64_C(0x1000000000008003)));
    TRY(st64(s, c + 0x28u, 0xEu));
    TRY(st64(s, c + 0x30u, 0u));
    TRY(st64(s, c + 0x38u, 0x3Fu));
    TRY(st64(s, c + 0x40u, tex0));
    TRY(st64(s, c + 0x48u, 6u));
    TRY(st64(s, c + 0x50u, (u64)(int64_t)a2));
    return st64(s, c + 0x58u, 0x3Bu);                          /* 001D6FF0 */
}

int em_static_world_001D6F60(S *s, int32_t chan, u64 tex0, int32_t a2)
{
    ENTER(0x001D6F60u);
    trace(s, 0x001D6F60u, sx((u32)chan), tex0, sx((u32)a2), 0);
    ETRY(span(s, EM_SW_D_00275670, 4));
    LEAVE(tex0_packet(s, chan, tex0, a2));
}

/* 001D7000(chan, a1): CNT 3; DIRECT 2; GIF A+D tag of 1 register;
 * register 0x3B (TEXA) = a1. */
static int texa_packet(S *s, int32_t chan, int32_t a1)
{
    u32 word, c;
    TRY(cursor_word(s, chan, &word));
    TRY(tag(s, word, 0x10u, 0u, 3u, 0x40u, &c));               /* 001D7040..001D7064 */
    TRY(span(s, c, 0x40));
    TRY(stq(s, c + 0x10u, ZERO4));                             /* 001D7068 */
    TRY(st32(s, c + 0x1Cu, 0x50000002u));
    TRY(st64(s, c + 0x20u, UINT64_C(0x1000000000008001)));
    TRY(st64(s, c + 0x28u, 0xEu));
    TRY(st64(s, c + 0x30u, (u64)(int64_t)a1));
    return st64(s, c + 0x38u, 0x3Bu);                          /* 001D7078 */
}

int em_static_world_001D7000(S *s, int32_t chan, int32_t a1)
{
    ENTER(0x001D7000u);
    trace(s, 0x001D7000u, sx((u32)chan), sx((u32)a1), 0, 0);
    ETRY(span(s, EM_SW_D_00275670, 4));
    LEAVE(texa_packet(s, chan, a1));
}

/* 001D7100(chan, vu, src, n): CNT (n >> 4) + 1; STCYCL 4,4; UNPACK V4-32
 * of n >> 4 qwords to VU address vu; the data by block_copy. Returns the
 * cursor loaded first. */
static int upload(S *s, int32_t chan, u32 vu, u32 src, int32_t n, u32 *result)
{
    u32 word, first, c;
    const int32_t q = n >> 4;                                  /* 001D7114 (sra) */
    const u32 advance = (u32)(q + 2) << 4;
    const u32 unpack = (vu | ((u32)q << 16)) | 0x6C000000u;
    TRY(cursor_word(s, chan, &word));
    TRY(ld32(s, word, &first));                                /* 001D7124 */
    TRY(tag(s, word, 0x10u, 0u, (u32)(q + 1) & 0xFFFFu, advance, &c));
    TRY(span(s, c, 0x20));
    TRY(st32(s, c + 0x10u, 0u));                               /* 001D7170 */
    TRY(st32(s, c + 0x14u, 0u));
    TRY(st32(s, c + 0x18u, 0x01000404u));
    TRY(st32(s, c + 0x1Cu, unpack));                           /* 001D717C */
    trace(s, 0x00121870u, sx(c + 0x20u), sx(src), sx((u32)n), sx(c));
    {
        const u32 fn = s->fn;
        s->fn = 0x00121870u;
        const int rc = block_copy(s, c + 0x20u, src, (u32)n);  /* 001D7184 */
        s->fn = fn;
        TRY(rc);
    }
    if (result) *result = first;
    return 0;
}

int em_static_world_001D7100(S *s, int32_t chan, u32 vu, u32 src, int32_t n, u32 *result)
{
    ENTER(0x001D7100u);
    trace(s, 0x001D7100u, sx((u32)chan), sx(vu), sx(src), sx((u32)n));
    ETRY(span(s, EM_SW_D_00275670, 4));
    LEAVE(upload(s, chan, vu, src, n, result));
}

/* 001D71A0(chan, a1): CNT 1; NOP, MSCAL a1. */
static int mscal(S *s, int32_t chan, u32 a1)
{
    u32 word, c;
    TRY(cursor_word(s, chan, &word));
    TRY(tag(s, word, 0x10u, 0u, 1u, 0x20u, &c));               /* 001D71C0..001D71E0 */
    TRY(span(s, c, 0x20));
    TRY(stq(s, c + 0x10u, ZERO4));                             /* 001D71E4 */
    return st32(s, c + 0x1Cu, a1 | 0x14000000u);               /* 001D71E8 */
}

int em_static_world_001D71A0(S *s, int32_t chan, u32 a1)
{
    ENTER(0x001D71A0u);
    trace(s, 0x001D71A0u, sx((u32)chan), sx(a1), 0, 0);
    ETRY(span(s, EM_SW_D_00275670, 4));
    LEAVE(mscal(s, chan, a1));
}

/* The RET tag that closes a channel list (001E1E60 / 001E1AD0 tails). */
static int ret_tag(S *s, int32_t chan)
{
    u32 word;
    TRY(cursor_word(s, chan, &word));
    return tag(s, word, 0x60u, 0u, 0u, 0x10u, NULL);
}

/* ------------------------------------------------------------------ */
/* 001E1E60: the background channel list.                             */
/* ------------------------------------------------------------------ */

static int sound_branch(S *s, int32_t chan)
{
    u32 on24;
    trace(s, 0x001D2910u, 0x24, 0, 0, 0);
    CALLW(0x001D2910u, W->w_001D2910(W->ctx, 0x24, &on24));   /* 001E20A8 */
    int32_t r;
    trace(s, 0x00122BB8u, 0, 0, 0, 0);
    CALLW(0x00122BB8u, W->w_00122BB8(W->ctx, &r));             /* 001E20B8 / 001E2118 */
    const u32 base = on24 != 0 ? F_16 : F_32;
    u32 snd[4] = {on24 != 0 ? 0x4B4E4514u : 0xCDC3D411u, 0xC1200000u, 0x4E1D842Du, 0u};
    u32 f = em_ee_cvt_s_w_bits((u32)r);
    f = em_ee_mul_bits(F_RAND, f);
    f = em_ee_mul_bits(base, f);
    f = em_ee_add_bits(base, f);
    const u32 vol[4] = {f, f, f, f};
    u32 handle;
    TRY(ld32(s, EM_SW_D_0027569C, &handle));                   /* 001E217C */
    int32_t old = -1;
    if (handle != 0xFFFFFFFFu) {
        trace(s, 0x001D73A0u, sx(handle), 0, 0, 0);
        CALLW(0x001D73A0u, W->w_001D73A0(W->ctx, (int32_t)handle, &old)); /* 001E218C */
    }
    int32_t h;
    trace(s, 0x001D72D0u, EM_SW_TRACE_STACK, 0, 0, 0);
    CALLW(0x001D72D0u, W->w_001D72D0(W->ctx, snd, &h));        /* 001E2198 */
    TRY(st32(s, EM_SW_D_0027569C, (u32)h));                    /* 001E21A4 */
    trace(s, 0x001D1F80u, sx((u32)chan), 2, 2, 0);
    CALLW(0x001D1F80u, W->w_001D1F80(W->ctx, chan, 2, 2));     /* 001E21AC */
    const u64 gs = UINT64_C(0x2007780621322A00);
    trace6(s, 0x001D75E0u, sx((u32)chan), EM_SW_TRACE_STACK, gs, sx(0x80808080u), F_384, F_384);
    CALLW(0x001D75E0u, W->w_001D75E0(W->ctx, chan, snd, gs, 0x80808080u, F_384, F_384));
    if (old == 0) {
        trace(s, 0x001CEFD0u, EM_SW_TRACE_STACK, EM_SW_TRACE_STACK, 0, 0);
        CALLW(0x001CEFD0u, W->w_001CEFD0(W->ctx, snd, vol));   /* 001E21F8 */
    }
    return 0;
}

static int background(S *s, int32_t chan, u32 *result)
{
    u32 word, start, ctx;
    TRY(cursor_word(s, chan, &word));
    TRY(ld32(s, word, &start));                                /* 001E1E84 */
    trace(s, 0x001D1F80u, sx((u32)chan), 0, 7, 0);
    CALLW(0x001D1F80u, W->w_001D1F80(W->ctx, chan, 0, 7));     /* 001E1E8C */
    trace(s, 0x001D1FF0u, sx((u32)chan), 0, 0, 0);
    CALLW(0x001D1FF0u, W->w_001D1FF0(W->ctx, chan, 0));        /* 001E1E98 */
    u64 tex0;
    TRY(context(s, &ctx));
    TRY(ld64(s, ctx + 0x1D0u, &tex0));                         /* 001E1EA8 */
    TRY(em_static_world_001D6F60(s, chan, tex0, 0x80));        /* 001E1EAC */
    /* RGBAQ: int(128 * a) << 24 | int(128 * b) << 16 | int(128 * g) << 8
     * | int(128 * r); the context word is loaded once, each float after
     * the previous call. */
    u32 c4, f, v, rgba;
    TRY(context(s, &c4));                                      /* 001E1EB4 */
    TRY(ld32(s, c4 + 0x1CCu, &f));
    f = em_ee_mul_bits(F_128, f);
    trace(s, 0x00128250u, f, 0, 0, 0);
    CALLW(0x00128250u, W->w_00128250(W->ctx, f, &v));          /* 001E1EC4 */
    rgba = v << 24;
    TRY(ld32(s, c4 + 0x1C8u, &f));
    f = em_ee_mul_bits(F_128, f);
    trace(s, 0x00128250u, f, 0, 0, 0);
    CALLW(0x00128250u, W->w_00128250(W->ctx, f, &v));
    rgba |= v << 16;
    TRY(ld32(s, c4 + 0x1C4u, &f));
    f = em_ee_mul_bits(F_128, f);
    trace(s, 0x00128250u, f, 0, 0, 0);
    CALLW(0x00128250u, W->w_00128250(W->ctx, f, &v));
    rgba = (v << 8) | rgba;
    TRY(ld32(s, c4 + 0x1C0u, &f));
    f = em_ee_mul_bits(F_128, f);
    trace(s, 0x00128250u, f, 0, 0, 0);
    CALLW(0x00128250u, W->w_00128250(W->ctx, f, &v));
    rgba = v | rgba;
    trace(s, 0x001D7080u, sx((u32)chan), sx(rgba), F_ONE, 0);
    CALLW(0x001D7080u, W->w_001D7080(W->ctx, chan, rgba, F_ONE)); /* 001E1F38 */
    /* The matrix D_00253570: transpose of the view copy, its second row
     * doubled word by word, then times the X/Y swap. */
    TRY(context(s, &ctx));
    TRY(em_static_world_00102798(s, EM_SW_D_00253570, ctx + 0x2380u)); /* 001E1F4C */
    for (u32 i = 0; i < 4; ++i) {                              /* 001E1F54..001E1FFC */
        u32 x;
        TRY(ld32(s, EM_SW_D_00253570 + 0x10u + 4u * i, &x));
        TRY(st32(s, EM_SW_D_00253570 + 0x10u + 4u * i, em_ee_mul_bits(x, F_TWO)));
    }
    static const u32 SWAP[16] = {0, F_ONE, 0, 0,  F_ONE, 0, 0, 0,  0, 0, F_ONE, 0,  0, 0, 0, F_ONE};
    trace(s, 0x001026D0u, sx(EM_SW_D_00253570), EM_SW_TRACE_STACK, sx(EM_SW_D_00253570), 0);
    {
        const u32 fn = s->fn;
        s->fn = 0x001026D0u;
        const int rc = product(s, EM_SW_D_00253570, 0, SWAP, EM_SW_D_00253570); /* 001E1FF8 */
        s->fn = fn;
        TRY(rc);
    }
    u32 zoom;
    TRY(context(s, &ctx));
    TRY(ld32(s, ctx + 0x2468u, &zoom));                        /* 001E2010 */
    TRY(st32(s, EM_SW_D_002535B8, zoom));                      /* 001E2028 */
    TRY(em_static_world_001D7100(s, chan, 0x0u, EM_SW_D_00253560, 0x10, NULL));
    TRY(em_static_world_001D7100(s, chan, 0x81u, EM_SW_D_00253560, 0x10, NULL));
    TRY(em_static_world_001D7100(s, chan, 0x102u, EM_SW_D_00253560, 0x10, NULL));
    TRY(em_static_world_001D7100(s, chan, 0x200u, EM_SW_D_00253570, 0x80, NULL));
    TRY(em_static_world_001D2090(s, chan, EM_SW_D_0023C990));  /* 001E2078 */
    TRY(em_static_world_001D71A0(s, chan, 0u));                /* 001E2084 */
    u32 on23;
    trace(s, 0x001D2910u, 0x23, 0, 0, 0);
    CALLW(0x001D2910u, W->w_001D2910(W->ctx, 0x23, &on23));   /* 001E208C */
    if (on23 != 0) TRY(sound_branch(s, chan));
    trace(s, 0x001D2040u, sx((u32)chan), 1, 0, 0);
    CALLW(0x001D2040u, W->w_001D2040(W->ctx, chan, 1));        /* 001E2204 */
    TRY(ret_tag(s, chan));                                     /* 001E220C..001E2240 */
    if (result) *result = start;
    return 0;
}

int em_static_world_001E1E60(S *s, u32 a0, int32_t chan, u32 *result)
{
    ENTER(0x001E1E60u);
    trace(s, 0x001E1E60u, sx(a0), sx((u32)chan), 0, 0);
    ENEEDW(w_001D1F80, 0x001D1F80u);
    ENEEDW(w_001D1FF0, 0x001D1FF0u);
    ENEEDW(w_00128250, 0x00128250u);
    ENEEDW(w_001D7080, 0x001D7080u);
    ENEEDW(w_001D2910, 0x001D2910u);
    ENEEDW(w_00122BB8, 0x00122BB8u);
    ENEEDW(w_001D73A0, 0x001D73A0u);
    ENEEDW(w_001D72D0, 0x001D72D0u);
    ENEEDW(w_001D75E0, 0x001D75E0u);
    ENEEDW(w_001CEFD0, 0x001CEFD0u);
    ENEEDW(w_001D2040, 0x001D2040u);
    ETRY(span(s, EM_SW_D_00275670, 8));
    ETRY(span(s, EM_SW_D_00253560, 0x90));
    ETRY(span(s, EM_SW_D_0027569C, 4));
    u32 ctx;
    ETRY(context(s, &ctx));
    ETRY(span(s, ctx + 0x1C0u, 0x18));
    ETRY(span(s, ctx + 0x2380u, 0x40));
    ETRY(span(s, ctx + 0x2468u, 4));
    LEAVE(background(s, chan, result));
}

/* ------------------------------------------------------------------ */
/* 001E0E80: one grid point of 001E1AD0.                              */
/* ------------------------------------------------------------------ */

static int grid_point(S *s, u32 out, int32_t x, int32_t y, u32 *result)
{
    u32 ctx, m[16], v[4], r[4];
    TRY(context(s, &ctx));
    trace(s, 0x00102798u, EM_SW_TRACE_STACK, sx(ctx + 0x2380u), 0, 0);
    {                                                          /* 001E0EAC (00102798) */
        u32 v4[16];
        for (u32 row = 0; row < 4; ++row) TRY(ldq(s, ctx + 0x2380u + 0x10u * row, v4 + 4 * row));
        em_camera_commit_00102798(m, v4);
    }
    v[0] = em_ee_cvt_s_w_bits((u32)x);                         /* 001E0EC0 */
    v[1] = em_ee_cvt_s_w_bits((u32)y);
    TRY(context(s, &ctx));
    TRY(ld32(s, ctx + 0x2468u, &v[2]));                        /* 001E0EEC */
    v[3] = 0;                                                  /* 001E0EF8 */
    trace(s, 0x001026A0u, EM_SW_TRACE_STACK, EM_SW_TRACE_STACK, EM_SW_TRACE_STACK, 0);
    CALLW(0x001026A0u, W->w_001026A0(W->ctx, r, m, v));        /* 001E0EF4 */
    const u32 ry = r[1];
    if (!em_ee_c_lt_bits(ry, 0u)) {                            /* 001E0F0C */
        TRY(st32(s, out, 0u));
        TRY(st32(s, out + 4u, 0u));
        TRY(st32(s, out + 8u, F_ONE));
        if (result) *result = 0;
        return 0;
    }
    u32 d, f0, f20 = F_ONE;
    TRY(ld32(s, EM_SW_D_008105D4, &d));                        /* 001E0F30 */
    f0 = em_ee_sub_bits(d, F_TEN);
    if (em_ee_c_le_bits(F_ONE, f0)) f20 = f0;                  /* 001E0F4C */
    const u32 k = em_ee_div_bits(f0, ry);                      /* 001E0F6C */
    const u32 f3 = em_ee_mul_bits(r[2], k);
    const u32 fx = em_ee_mul_bits(r[0], k);
    TRY(st32(s, out, em_ee_mul_bits(F_0_002, fx)));            /* 001E0F94 */
    TRY(st32(s, out + 4u, em_ee_mul_bits(F_0_002, f3)));
    TRY(st32(s, out + 8u, F_ONE));
    u32 a;
    trace(s, 0x0011DF78u, ry, 0, 0, 0);
    CALLW(0x0011DF78u, W->w_0011DF78(W->ctx, ry, &a));         /* 001E0FA8 */
    a = em_ee_mul_bits(F_256, a);
    const u32 q = em_ee_div_bits(a, f20);
    int32_t n;
    trace(s, 0x001281C0u, q, 0, 0, 0);
    CALLW(0x001281C0u, W->w_001281C0(W->ctx, q, &n));          /* 001E0FCC */
    if (result) *result = n < 0 ? 0u : (n < 0x81 ? (u32)n : 0x80u);
    return 0;
}

int em_static_world_001E0E80(S *s, u32 out, int32_t x, int32_t y, u32 *result)
{
    ENTER(0x001E0E80u);
    trace(s, 0x001E0E80u, sx(out), sx((u32)x), sx((u32)y), 0);
    ENEEDW(w_001026A0, 0x001026A0u);
    ENEEDW(w_0011DF78, 0x0011DF78u);
    ENEEDW(w_001281C0, 0x001281C0u);
    ETRY(span(s, EM_SW_D_00275670, 4));
    ETRY(span(s, EM_SW_D_008105D4, 4));
    ETRY(span(s, out, 0xC));
    LEAVE(grid_point(s, out, x, y, result));
}

/* ------------------------------------------------------------------ */
/* 001E1AD0: the 32 x 32 grid variant.                                */
/* ------------------------------------------------------------------ */

static int grid_to_int(S *s, u32 f12, int32_t *out)
{
    trace(s, 0x001281C0u, f12, 0, 0, 0);
    CALLW(0x001281C0u, W->w_001281C0(W->ctx, f12, out));
    return 0;
}

static int grid(S *s, int32_t chan, u32 *result)
{
    u32 word, start;
    TRY(cursor_word(s, chan, &word));
    TRY(ld32(s, word, &start));                                /* 001E1B14 */
    u32 rnd;
    trace(s, 0x001D2E00u, 1, 0, 0, 0);
    CALLW(0x001D2E00u, W->w_001D2E00(W->ctx, 1, &rnd));        /* 001E1B18 */
    u32 f2;
    if ((int32_t)rnd >= 0) {
        f2 = em_ee_cvt_s_w_bits(rnd);                          /* 001E1B2C */
    } else {
        f2 = em_ee_cvt_s_w_bits((rnd >> 1) | (rnd & 1u));      /* 001E1B38..001E1B4C */
        f2 = em_ee_add_bits(f2, f2);
    }
    u32 phase;
    TRY(ld32(s, EM_SW_D_00275C0C, &phase));                    /* 001E1B5C */
    phase = em_ee_sub_bits(phase, em_ee_div_bits(f2, F_65535));
    TRY(st32(s, EM_SW_D_00275C0C, phase));                     /* 001E1B68 */
    TRY(ld32(s, EM_SW_D_00275C0C, &phase));                    /* 001E1B7C */
    while (em_ee_c_lt_bits(phase, 0u)) {                       /* 001E1B94 */
        TRY(ld32(s, EM_SW_D_00275C0C, &phase));
        phase = em_ee_add_bits(phase, F_ONE);
        TRY(st32(s, EM_SW_D_00275C0C, phase));
        TRY(ld32(s, EM_SW_D_00275C0C, &phase));
    }
    trace(s, 0x001E1760u, sx((u32)chan), 0, 0, 0);
    CALLW(0x001E1760u, W->w_001E1760(W->ctx, chan));           /* 001E1BA4 */
    trace6(s, 0x001D6BA0u, sx((u32)chan), 0x258000, 8, 8, 0, 1);
    CALLW(0x001D6BA0u, W->w_001D6BA0(W->ctx, chan, 0x258000, 8, 8, 0, 1)); /* 001E1BC4 */
    trace(s, 0x001D1F80u, sx((u32)chan), 0, 1, 0);
    CALLW(0x001D1F80u, W->w_001D1F80(W->ctx, chan, 0, 1));     /* 001E1BD4 */
    trace(s, 0x001D1FF0u, sx((u32)chan), 1, 0, 0);
    CALLW(0x001D1FF0u, W->w_001D1FF0(W->ctx, chan, 1));        /* 001E1BE0 */
    TRY(em_static_world_001D7000(s, chan, 0x80));              /* 001E1BEC */

    for (int32_t row = 0; row < 0x20; ++row) {                 /* 001E1BF8 */
        u32 c;
        TRY(cursor_word(s, chan, &word));
        const u32 top = em_ee_mul_bits(F_224, em_ee_cvt_s_w_bits((u32)row));
        const u32 bottom = em_ee_mul_bits(F_224, em_ee_cvt_s_w_bits((u32)(row + 1)));
        TRY(tag(s, word, 0x10u, 0u, 0xC2u, EM_SW_001E1AD0_ROW_BYTES, &c)); /* 001E1C30..001E1C78 */
        TRY(span(s, c, EM_SW_001E1AD0_ROW_BYTES));
        TRY(stq(s, c + 0x10u, ZERO4));                         /* 001E1C80 */
        TRY(st32(s, c + 0x1Cu, 0x500000C1u));
        TRY(st64(s, c + 0x20u, UINT64_C(0x602E400000008020)));
        TRY(st64(s, c + 0x28u, 0x512512u));                    /* 001E1C8C */
        u32 cell = c + 0x30u;
        for (int32_t col = 0; col < 0x20; ++col) {             /* 001E1C90 */
            int32_t u, vt, vb;
            u32 f = em_ee_mul_bits(F_512, em_ee_cvt_s_w_bits((u32)col));
            TRY(grid_to_int(s, em_ee_add_bits(F_M256, em_ee_div_bits(f, F_31)), &u));
            TRY(grid_to_int(s, em_ee_add_bits(F_M112, em_ee_div_bits(top, F_31)), &vt));
            TRY(grid_to_int(s, em_ee_add_bits(F_M112, em_ee_div_bits(bottom, F_31)), &vb));
            u32 alpha;
            TRY(em_static_world_001E0E80(s, cell, u, (int32_t)((u32)vt << 1), &alpha));
            TRY(st32(s, cell + 0x1Cu, alpha));                 /* 001E1D28 */
            TRY(em_static_world_001E0E80(s, cell + 0x30u, u, (int32_t)((u32)vb << 1), &alpha));
            TRY(st32(s, cell + 0x4Cu, alpha));                 /* 001E1D38 */
            u32 t, p;
            TRY(ld32(s, cell + 4u, &t));
            TRY(ld32(s, EM_SW_D_00275C0C, &p));
            TRY(st32(s, cell + 4u, em_ee_add_bits(t, p)));     /* 001E1D5C */
            TRY(ld32(s, cell + 0x34u, &t));
            TRY(ld32(s, EM_SW_D_00275C0C, &p));
            TRY(st32(s, cell + 0x34u, em_ee_add_bits(t, p)));  /* 001E1D78 */
            const u32 xs = ((u32)u + 0x800u) << 4;
            TRY(st32(s, cell + 0x10u, 0x80u));
            TRY(st32(s, cell + 0x14u, 0x80u));
            TRY(st32(s, cell + 0x18u, 0x80u));
            TRY(st32(s, cell + 0x20u, xs));
            TRY(st32(s, cell + 0x24u, ((u32)vt + 0x800u) << 4));
            TRY(st32(s, cell + 0x2Cu, 0u));
            TRY(st32(s, cell + 0x40u, 0x80u));
            TRY(st32(s, cell + 0x44u, 0x80u));
            TRY(st32(s, cell + 0x48u, 0x80u));
            TRY(st32(s, cell + 0x50u, xs));
            TRY(st32(s, cell + 0x54u, ((u32)vb + 0x800u) << 4));
            TRY(st32(s, cell + 0x5Cu, 0u));                    /* 001E1DAC */
            cell += 0x60u;
        }
    }
    trace(s, 0x001D1F80u, sx((u32)chan), 3, 8, 0);
    CALLW(0x001D1F80u, W->w_001D1F80(W->ctx, chan, 3, 8));     /* 001E1DC8 */
    trace(s, 0x001E17E0u, sx((u32)chan), 0, 0, 0);
    CALLW(0x001E17E0u, W->w_001E17E0(W->ctx, chan));           /* 001E1DD0 */
    TRY(ret_tag(s, chan));                                     /* 001E1DD8..001E1E0C */
    if (result) *result = start;
    return 0;
}

int em_static_world_001E1AD0(S *s, u32 a0, int32_t chan, u32 *result)
{
    ENTER(0x001E1AD0u);
    trace(s, 0x001E1AD0u, sx(a0), sx((u32)chan), 0, 0);
    ENEEDW(w_001D2E00, 0x001D2E00u);
    ENEEDW(w_001E1760, 0x001E1760u);
    ENEEDW(w_001D6BA0, 0x001D6BA0u);
    ENEEDW(w_001D1F80, 0x001D1F80u);
    ENEEDW(w_001D1FF0, 0x001D1FF0u);
    ENEEDW(w_001281C0, 0x001281C0u);
    ENEEDW(w_001026A0, 0x001026A0u);
    ENEEDW(w_0011DF78, 0x0011DF78u);
    ENEEDW(w_001E17E0, 0x001E17E0u);
    ETRY(span(s, EM_SW_D_00275670, 4));
    ETRY(span(s, EM_SW_D_00275C0C, 4));
    ETRY(span(s, EM_SW_D_008105D4, 4));
    u32 ctx;
    ETRY(context(s, &ctx));
    ETRY(span(s, ctx + 0x2380u, 0x40));
    ETRY(span(s, ctx + 0x2468u, 4));
    LEAVE(grid(s, chan, result));
}
