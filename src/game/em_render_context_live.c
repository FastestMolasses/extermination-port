/* em_render_context_live.c - the one canonical render context (see
 * em_render_context_live.h, docs/RENDER_CONTEXT.md section 8).
 *
 * This file adds no behaviour of its own: it owns the storage the original
 * routines address and wires each lane translation's workers to the other
 * translations over that one storage. Every worker below names the original
 * callee it stands for. */
#include "game/em_render_context_live.h"

#include "game/em_actor_light_001D89D0.h"
#include "game/em_effect_original.h"
#include "game/em_frame_render_heads.h"
#include "game/em_load_veil_particles.h"
#include "game/em_packet_chain_original.h"
#include "game/em_player_equipment.h"
#include "game/em_player_stage_workers.h"
#include "game/em_render_context.h"
#include "game/em_render_verify_rest.h"
#include "game/em_sdk_math_original.h"
#include "game/em_status_ui_leftovers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;

/* ---- storage (original address ranges this module owns) ---------------- */

#define ARENA_BASE  0x0028F700u   /* D_0028F700 */
#define ARENA_END   0x0076B5C0u   /* the chain table D_007635C0 ends here */
#define CTX_BASE    EM_RCL_CONTEXT
#define CTX_END     0x00817240u   /* context, GS blocks, skin records */
#define D_241010    0x00241010u
#define D_250F30    0x00250F30u
#define D_250F30_SIZE 0x2240u
#define D_26E510    0x0026E510u
#define D_26E850    0x0026E850u
#define D_275670    0x00275670u
#define SPAD_3A40   0x70003A40u
#define SPAD_3B60   0x70003B60u
#define D_251C50    0x00251C50u
#define D_810E80    0x00810E80u
#define D_8106B0    0x008106B0u
#define D_810700    0x00810700u
#define D_8101E4    0x008101E4u
#define D_8102B0    0x008102B0u
#define D_810610    0x00810610u
#define D_8105E0    0x008105E0u
#define SPAD_3B8D   0x70003B8Du
#define SKIN_816440 0x00816440u
#define GS_BLOCKS   0x00814220u   /* the value of D_00275674 (checked at load) */

static uint32_t s_arena_words[(ARENA_END - ARENA_BASE) / 4];
static uint32_t s_ctx_words[(CTX_END - CTX_BASE) / 4];
static uint32_t s_d250F30_words[D_250F30_SIZE / 4];
static uint32_t s_d275670_words[0x30 / 4];
static uint32_t s_spad3A40_words[0x100 / 4];
static uint32_t s_spad3B60_words[1];
static uint8_t s_d241010[8];
static uint8_t s_d26E510[16];
static uint8_t s_d26E850[16];

#define ARENA ((uint8_t *)s_arena_words)
#define CTXB ((uint8_t *)s_ctx_words)

/* External views (EmRclExternal) the binder hands over. */
enum { X_810E80, X_810610, X_8105E0, X_8106B0, X_810700, X_8101E4, X_3B8D, X_8102B0, X_COUNT };
static const struct { u32 address, size; } k_external[X_COUNT] = {
    {D_810E80, 2}, {D_810610, 0x40}, {D_8105E0, 0x10}, {D_8106B0, 0x48},
    {D_810700, 3}, {D_8101E4, 1}, {SPAD_3B8D, 1}, {D_8102B0, 0x320},
};

static struct {
    int loaded, bound, head_ran;
    u32 head_zoom;   /* +0x2468 as the last 001D1C50's 001D2960 read it */
    u32 fault;
    uint8_t *ext[X_COUNT];
    EmRclWorkers host;
    /* the lane modules, all over the storage above */
    EmFrhView frh_views[20];
    EmRenderContextView rc_views[20];
    EmPacketChainRegion pc_regions[20];
    unsigned view_count;
    EmFrh frh;
    EmRenderContext rc;
    EmPacketChain pc;
    EmLoadVeilParticles veil;
    EmActorLight light;
} R;

/* ---- faults -------------------------------------------------------------- */

static void report(u32 address, const char *what)
{
    fprintf(stderr, "render context: %s at %08X (frh %08X/%d/%08X, rc %08X/%d, pc %08X/%d/%08X, "
            "veil %08X/%d, light %08X)\n", what, (unsigned)address,
            (unsigned)R.frh.fault.address, (int)R.frh.fault.code, (unsigned)R.frh.fault.data,
            (unsigned)R.rc.fault.address, (int)R.rc.fault.code,
            (unsigned)R.pc.fault_function, (int)R.pc.fault, (unsigned)R.pc.fault_address,
            (unsigned)R.veil.fault.address, (int)R.veil.fault.code,
            (unsigned)R.light.fault.address);
}

static int fail(u32 address, const char *what)
{
    if (!R.fault) {
        R.fault = address ? address : 0x00275670u;
        report(R.fault, what);
    }
    return -1;
}

uint32_t em_rcl_fault(void) { return R.fault; }
int em_rcl_loaded(void) { return R.loaded; }
int em_rcl_bound(void) { return R.bound; }

/* The entry's result: a latched sub-module fault becomes this module's. */
static int done(int rc, u32 entry)
{
    if (rc < 0) {
        u32 at = R.frh.fault.code ? R.frh.fault.address : R.rc.fault.code ? R.rc.fault.address
               : R.pc.fault ? R.pc.fault_function : entry;
        return fail(at, "fault");
    }
    return 0;
}

/* ---- little helpers ------------------------------------------------------ */

static u32 rd32(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

static uint8_t *own(u32 address, u32 size)
{
    for (unsigned i = 0; i < R.view_count; ++i) {
        const EmFrhView *v = &R.frh_views[i];
        if (v->bytes && address >= v->address && size <= v->size && address - v->address <= v->size - size)
            return v->bytes + (address - v->address);
    }
    return NULL;
}

static float f32(u32 bits) { float f; memcpy(&f, &bits, 4); return f; }

/* ---- views --------------------------------------------------------------- */

static void add_view(u32 address, u32 size, uint8_t *bytes, int writable)
{
    unsigned i = R.view_count++;
    R.frh_views[i] = (EmFrhView){address, size, bytes, writable};
    R.rc_views[i] = (EmRenderContextView){address, size, bytes};
    R.pc_regions[i] = (EmPacketChainRegion){address, size, bytes};
}

static void build_views(void)
{
    R.view_count = 0;
    add_view(ARENA_BASE, ARENA_END - ARENA_BASE, ARENA, 1);
    add_view(CTX_BASE, CTX_END - CTX_BASE, CTXB, 1);
    add_view(D_250F30, D_250F30_SIZE, (uint8_t *)s_d250F30_words, 1);
    add_view(D_275670, 0x30, (uint8_t *)s_d275670_words, 1);
    add_view(SPAD_3A40, 0x100, (uint8_t *)s_spad3A40_words, 1);
    add_view(SPAD_3B60, 4, (uint8_t *)s_spad3B60_words, 1);
    add_view(D_241010, 8, s_d241010, 0);
    add_view(D_26E510, 16, s_d26E510, 0);
    add_view(D_26E850, 16, s_d26E850, 0);
    for (unsigned x = 0; x < X_COUNT; ++x)
        if (R.ext[x]) add_view(k_external[x].address, k_external[x].size, R.ext[x], 0);
    R.frh.views = R.frh_views;
    R.frh.view_count = R.view_count;
    R.rc.world.views = R.rc_views;
    R.rc.world.view_count = R.view_count;
    R.pc.regions = R.pc_regions;
    R.pc.region_count = R.view_count;
}

/* ---- workers: other translations over the same storage ------------------- */

static int unbound(void *ctx) { (void)ctx; return -1; }   /* reached = a fault */
static int unbound_u(void *ctx, uint32_t a) { (void)ctx; (void)a; return -1; }

/* 001B0070(): the request word D_008106C8 (em_player_stage_workers' read). */
static int w_001B0070(void *ctx, uint32_t *flags)
{
    (void)ctx;
    if (!R.ext[X_8106B0]) return -1;
    *flags = rd32(R.ext[X_8106B0] + 0x18);
    return 0;
}

/* 0015D2F0(): em_player_equipment_0015D2F0 over the player record. */
static int w_0015D2F0(void *ctx, int32_t *result)
{
    (void)ctx;
    if (!R.ext[X_8102B0]) return -1;
    return em_player_equipment_0015D2F0(R.ext[X_8102B0], result);
}

static int w_0022EBE0(void *ctx, int32_t *result)
{
    (void)ctx;
    if (!R.ext[X_8101E4] || !R.ext[X_3B8D]) return -1;
    *result = em_sul_0022EBE0(*R.ext[X_8101E4], *R.ext[X_3B8D]);
    return 0;
}

/* 001026A0(out, m, v) with m an original address (001DDE10: D_70003AC0). */
static int w_001026A0(void *ctx, uint32_t out[4], uint32_t matrix, const uint32_t v[4])
{
    (void)ctx;
    const uint8_t *m = own(matrix, 0x40);
    if (!m) return -1;
    float mf[16], vf[4], of[4];
    memcpy(mf, m, sizeof mf);
    memcpy(vf, v, sizeof vf);
    em_effect_original_001026A0(of, mf, vf);
    memcpy(out, of, sizeof of);
    return 0;
}

static int w_0011DF78(void *ctx, uint32_t x, uint32_t *result)
{
    (void)ctx;
    float r = em_sdk_math_original_0011DF78(f32(x));
    memcpy(result, &r, 4);
    return 0;
}

static int w_001281C0(void *ctx, uint32_t x, int32_t *result)
{
    (void)ctx;
    *result = em_player_float_to_int(x);
    return 0;
}

static int w_sqrt(void *ctx, uint32_t x, uint32_t *result)
{
    (void)ctx;
    return R.host.w_0011E748 ? R.host.w_0011E748(R.host.ctx, x, result) : -1;
}

static int w_tan(void *ctx, uint32_t x, uint32_t *result)
{
    (void)ctx;
    return R.host.w_0011E398 ? R.host.w_0011E398(R.host.ctx, x, result) : -1;
}

static int w_001D7C30(void *ctx)
{
    (void)ctx;
    return R.host.w_001D7C30 ? R.host.w_001D7C30(R.host.ctx) : -1;
}

/* em_render_context entries as frh workers. */
static int f_001D2730(void *ctx, int32_t a0, int32_t a1, int32_t *ret)
{
    (void)ctx;
    uint32_t r = 0;
    if (em_render_context_001D2730(&R.rc, a0, a1, &r) < 0) return -1;
    *ret = (int32_t)r;
    return 0;
}

static int f_001E0C80(void *ctx, int32_t a0, int32_t a1, int32_t *ret)
{
    (void)ctx;
    uint32_t r = 0;
    if (em_render_context_001E0C80(&R.rc, a0, a1, &r) < 0) return -1;
    *ret = (int32_t)r;
    return 0;
}

static int f_001D2910(void *ctx, int32_t a0, int32_t *ret)
{
    (void)ctx;
    uint32_t r = 0;
    if (em_render_context_001D2910(&R.rc, a0, &r) < 0) return -1;
    *ret = (int32_t)r;
    return 0;
}

static int f_001E0D70(void *ctx) { (void)ctx; return em_render_context_001E0D70(&R.rc); }
static int f_001DDA00(void *ctx) { (void)ctx; return em_render_context_001DDA00(&R.rc); }
static int f_001D2DE0(void *ctx, int32_t a0, int32_t a1)
{
    (void)ctx;
    return em_render_context_001D2DE0(&R.rc, a0, (uint32_t)a1);
}
static int f_001E0CC0(void *ctx) { (void)ctx; return em_render_context_001E0CC0(&R.rc); }

/* em_packet_chain_original entries. */
static int f_0021B970(void *ctx, uint32_t f12, uint32_t f13) { (void)ctx; return em_packet_chain_0021B970(&R.pc, f12, f13); }
static int f_0021BA80(void *ctx, int32_t a0, int32_t a1, int32_t a2)
{
    (void)ctx;
    return em_packet_chain_0021BA80(&R.pc, a0, a1, a2);
}
static int f_001CB800(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)ctx;
    return em_packet_chain_001CB800(&R.pc, a0, (int32_t)a1, a2, a3);
}
static int f_001CB8A0(void *ctx, uint32_t a0, int32_t a1, uint32_t a2, uint32_t a3)
{
    (void)ctx;
    (void)a0;   /* 001CB8A0 does not read a0 */
    return em_packet_chain_001CB8A0(&R.pc, a1, a2, a3);
}

/* em_load_veil_particles entries (the REF tags and the frame-copy packets). */
static int v_001D1F20(void *ctx, int32_t chan) { (void)ctx; return em_load_veil_particles_001D1F20(&R.veil, chan); }
static int v_001D2040(void *ctx, int32_t chan, int32_t a1)
{
    (void)ctx;
    return em_load_veil_particles_001D2040(&R.veil, chan, a1);
}
static int v_001D1FF0(void *ctx, int32_t chan, int32_t a1)
{
    (void)ctx;
    return em_load_veil_particles_001D1FF0(&R.veil, chan, a1);
}
static int v_001D1F80(void *ctx, int32_t chan, int32_t a1, int32_t a2)
{
    (void)ctx;
    return em_load_veil_particles_001D1F80(&R.veil, chan, a1, a2);
}
/* 001D6930(a0..a3, t0): t0 is the original address of the source quadword
 * (001D6B10 passes D_0026E510). */
static int v_001D6930(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3, uint32_t t0,
                      uint32_t *result)
{
    (void)ctx;
    const uint8_t *src = own(t0, 16);
    if (!src) return -1;
    return em_load_veil_particles_001D6930(&R.veil, a0, a1, a2, a3, src, result);
}
static int v_001D6BA0(void *ctx, int32_t chan, int32_t a1, int32_t a2, int32_t a3, int32_t t0, int32_t t1)
{
    (void)ctx;
    uint32_t result;
    return em_load_veil_particles_001D6BA0(&R.veil, chan, a1, a2, a3, t0, t1, &result);
}
static int v_001006D8(void *ctx, uint32_t env, int32_t psm, int32_t w, int32_t h, int32_t t0, int32_t t1)
{
    (void)ctx;
    int32_t result;
    return em_load_veil_particles_001006D8(&R.veil, env, psm, w, h, t0, t1, &result);
}

/* 001D5370's and 001DD7B0's callees (not bound live: those entries are not
 * called) and the four 001DDA00 effect callees the first level never
 * reaches: reaching any of them is a fault. */
static int u_001D2D20(void *ctx, uint32_t m, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)ctx; (void)m; (void)a; (void)b; (void)c; (void)d; (void)e;
    return -1;
}
static int u_001026D0(void *ctx, uint32_t d, uint32_t a, uint32_t b) { (void)ctx; (void)d; (void)a; (void)b; return -1; }
static int u_001C6120(void *ctx, uint32_t bank, int32_t id, uint32_t *r)
{
    (void)ctx; (void)bank; (void)id; (void)r;
    return -1;
}
static int u_frh_001C6120(void *ctx, uint32_t bank, uint32_t id, uint32_t *r)
{
    (void)ctx; (void)bank; (void)id; (void)r;
    return -1;
}
static int u_001E2260(void *ctx, uint64_t tag) { (void)ctx; (void)tag; return -1; }
static int u_001D8130(void *ctx, int32_t a0, uint32_t a1) { (void)ctx; (void)a0; (void)a1; return -1; }
static int u_001D8340(void *ctx, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, uint32_t t0)
{
    (void)ctx; (void)a0; (void)a1; (void)a2; (void)a3; (void)t0;
    return -1;
}
static int u_001D8690(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t a3)
{
    (void)ctx; (void)a0; (void)a1; (void)a2; (void)a3;
    return -1;
}

/* 001D8FD0's workers: 001D7B30 (em_actor_light over the context flags, the
 * area bytes and the exported room table) and 001B0070. */
static int a_001D7B30(void *ctx, uint32_t *record)
{
    (void)ctx;
    uint32_t entry = 0;
    if (em_actor_light_001D7B30(&R.light, &entry) < 0) return -1;
    *record = D_251C50 + entry * 0x78u;
    return 0;
}

/* 001C1DC0's workers. */
static int g_001D2830(void *ctx, int32_t flag, int32_t value)
{
    (void)ctx;
    int32_t ignored;
    return em_frh_001D2830(&R.frh, flag, value, &ignored);
}
static EmRvrFault s_rvr_fault;
static int g_001E2260(void *ctx, uint64_t tag)
{
    (void)ctx;
    EmRvrRenderContext rc = {CTXB, 0x2540u};
    return em_rvr_001E2260(&rc, tag, &s_rvr_fault);
}
static int g_001E2270(void *ctx, uint32_t address)
{
    (void)ctx;
    const uint8_t *src = own(address, 16);
    if (!src) return -1;
    EmRvrRenderContext rc = {CTXB, 0x2540u};
    return em_rvr_001E2270(&rc, src, &s_rvr_fault);
}
static int g_001E2280(void *ctx, uint64_t tag)
{
    (void)ctx;
    EmRvrRenderContext rc = {CTXB, 0x2540u};
    return em_rvr_001E2280(&rc, tag, &s_rvr_fault);
}
static int g_001D52E0(void *ctx)
{
    (void)ctx;
    return R.host.w_001D52E0 ? R.host.w_001D52E0(R.host.ctx) : -1;
}
static int g_001D8FD0(void *ctx)
{
    (void)ctx;
    const EmPacketChainAreaFogWorkers w = {NULL, a_001D7B30, w_001B0070};
    return em_packet_chain_001D8FD0(&R.pc, &w);
}
static int g_001C1EA0(void *ctx, uint32_t block)
{
    (void)ctx;
    return R.host.w_001C1EA0 ? R.host.w_001C1EA0(R.host.ctx, block) : -1;
}

static void wire(void)
{
    /* Both worker tables carry the packet chain as their context, so the
     * packet chain's own adapters bind directly (0021B9A0's heads order,
     * 001CB760's three arguments); every other worker here ignores it. */
    EmFrhWorkers *f = &R.frh.workers;
    memset(f, 0, sizeof *f);
    f->ctx = &R.pc;
    f->w_001D2730 = f_001D2730;
    f->w_001E0C80 = f_001E0C80;
    f->w_001B0070 = w_001B0070;
    f->w_0015D2F0 = w_0015D2F0;
    f->w_0021B970 = f_0021B970;
    f->w_0021B9A0 = em_packet_chain_w_0021B9A0_heads;
    f->w_0021BA80 = f_0021BA80;
    f->w_001D7C30 = w_001D7C30;
    f->w_001D2910 = f_001D2910;
    f->w_001E0D70 = f_001E0D70;
    f->w_001DDA00 = f_001DDA00;
    f->w_001CB800 = f_001CB800;
    f->w_001E2260 = u_001E2260;       /* 001C1D00 (not bound: section 8) */
    f->w_001E0CF0 = unbound;
    f->w_001D5370 = unbound;
    f->w_0011E748 = w_sqrt;
    f->w_0011E398 = w_tan;
    f->w_skin_arena_init = unbound;   /* 001D19E0 (not bound: section 8) */
    f->w_001D9720 = unbound;
    f->w_001DD940 = unbound;
    f->w_001E0C30 = unbound;
    f->w_001D9060 = unbound;
    f->w_001D71F0 = unbound;
    f->w_001D7BB0 = unbound;
    f->w_001D2DE0 = f_001D2DE0;
    f->w_001E0CC0 = f_001E0CC0;
    f->w_001E0380 = unbound;
    f->w_001D8130 = u_001D8130;       /* 001D88B0 (not bound) */
    f->w_001D8340 = u_001D8340;
    f->w_001D8690 = u_001D8690;
    f->w_001C6120 = u_frh_001C6120;   /* 001D9070 (not bound) */
    f->w_001D1F20 = v_001D1F20;
    f->w_001D2040 = v_001D2040;
    f->w_001D1FF0 = v_001D1FF0;
    f->w_001CB8A0 = f_001CB8A0;

    EmRenderContextWorkers *w = &R.rc.workers;
    memset(w, 0, sizeof *w);
    w->ctx = &R.pc;
    w->w_0015D2F0 = w_0015D2F0;
    w->w_0022EBE0 = w_0022EBE0;
    w->w_001B0070 = w_001B0070;
    w->w_001026A0 = w_001026A0;
    w->w_0011DF78 = w_0011DF78;
    w->w_001281C0 = w_001281C0;
    w->w_001D6930 = v_001D6930;
    w->w_001D1F20 = v_001D1F20;
    w->w_001D6BA0 = v_001D6BA0;
    w->w_001D1FF0 = v_001D1FF0;
    w->w_001D1F80 = v_001D1F80;
    w->w_001006D8 = v_001006D8;
    w->w_001CB760 = em_packet_chain_w_001CB760;
    w->w_001D2D20 = u_001D2D20;
    w->w_001026D0 = u_001026D0;
    w->w_001C6120 = u_001C6120;
    w->w_001D4DA0 = unbound;
    w->w_001D4FB0 = unbound_u;
    w->w_001D4B20 = unbound_u;
    w->w_001D5BD0 = unbound;
    w->w_001DE920 = unbound;
    w->w_001DDB70 = unbound;
    w->w_001DFF70 = unbound;
    w->w_001DF110 = unbound_u;
    R.rc.world.ctx = CTX_BASE;

    EmLoadVeilParticlesWorld *v = &R.veil.world;
    memset(&R.veil, 0, sizeof R.veil);
    v->cursor = s_ctx_words + 0x10 / 4;
    v->cursor_count = 4;
    v->ctx_9C = s_ctx_words + 0x9C / 4;
    v->d00275674 = s_d275670_words + 1;
    v->d0027568C = s_d275670_words + 7;
    v->d00241010 = s_d241010;
    v->packet = ARENA;
    v->packet_address = ARENA_BASE;
    v->packet_size = ARENA_END - ARENA_BASE;
    R.veil.workers.w_0011DF78 = w_0011DF78;
    R.veil.workers.w_001281C0 = w_001281C0;

    memset(&R.light, 0, sizeof R.light);
    R.light.world.ctx_000C = s_ctx_words + 0x0C / 4;
    R.light.world.d00810700 = R.ext[X_810700];
    R.light.world.d00251C50 = s_d250F30_words + (D_251C50 - D_250F30) / 4;

    R.pc.d275670 = CTX_BASE;
    R.pc.d275674 = GS_BLOCKS;
}

/* ---- the export ---------------------------------------------------------- */

static const struct { u32 address, size; uint8_t *bytes; } k_blocks[] = {
    {D_241010, 8, s_d241010},
    {D_250F30, D_250F30_SIZE, (uint8_t *)s_d250F30_words},
    {D_26E510, 16, s_d26E510},
    {D_26E850, 16, s_d26E850},
    {D_275670, 0x30, (uint8_t *)s_d275670_words},
};
#define BLOCK_COUNT (sizeof k_blocks / sizeof k_blocks[0])

static int load_export(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "render context: %s is missing (run tools/export_render_context.py)\n", path);
        return -1;
    }
    uint8_t head[0x10 + 0x10 * BLOCK_COUNT];
    int ok = fread(head, 1, sizeof head, f) == sizeof head && memcmp(head, "EMRC", 4) == 0 &&
             rd32(head + 4) == 1 && rd32(head + 8) == BLOCK_COUNT;
    for (unsigned i = 0; ok && i < BLOCK_COUNT; ++i) {
        const uint8_t *e = head + 0x10 + 0x10 * i;
        ok = rd32(e) == k_blocks[i].address && rd32(e + 4) == k_blocks[i].size &&
             fseek(f, (long)rd32(e + 8), SEEK_SET) == 0 &&
             fread(k_blocks[i].bytes, 1, k_blocks[i].size, f) == k_blocks[i].size;
    }
    fclose(f);
    /* The routines here take D_00275670 / D_00275674 as the fixed values the
     * captures hold (EmRenderContextWorld.ctx, EmPacketChain.d275670). */
    if (!ok || s_d275670_words[0] != CTX_BASE || s_d275670_words[1] != GS_BLOCKS) {
        fprintf(stderr, "render context: %s is not a render-context export\n", path);
        return -1;
    }
    return 0;
}

int em_rcl_init(const char *export_path, uint8_t *d810E80)
{
    if (R.loaded) return R.fault ? -1 : 0;
    if (!d810E80 || load_export(export_path ? export_path : EM_RCL_EXPORT_PATH) < 0) return -1;
    R.ext[X_810E80] = d810E80;
    wire();
    build_views();
    R.loaded = 1;
    /* Two stores of the boot builder sub_EXTERMINATION (NEARMISS, not
     * translated) that a bound routine reads before anything else writes
     * them: the zoom 001D25F0(480.0) and the ramp records 001DEDE0 (their
     * flag numbers 2 / 9 steer 001DDA00's 001DEEE0). Its other context
     * stores (the flag registrations, the 0021B970 / 0021BA80 fog pair and
     * the +0xA0 copies) are rewritten by the area load (001C1DC0, 001D8FD0)
     * before a bound routine reads them, except the +0x100 copy that only
     * 001D2730(0, 0) reads (never on the route); its arena pattern and GS
     * blocks reach only DMA packet bytes (the renderer boundary).
     * docs/RENDER_CONTEXT.md section 8. */
    if (done(em_frh_001D25F0(&R.frh, UINT32_C(0x43F00000)), 0x001D25F0u) < 0) return -1;
    return done(em_render_context_001DEDE0(&R.rc), 0x001DEDE0u);
}

int em_rcl_bind(const EmRclExternal *views, unsigned count, const EmRclWorkers *workers)
{
    if (!R.loaded || R.fault || !workers) return -1;
    for (unsigned x = 1; x < X_COUNT; ++x) {
        R.ext[x] = NULL;
        for (unsigned i = 0; i < count; ++i)
            if (views[i].address == k_external[x].address && views[i].size >= k_external[x].size)
                R.ext[x] = views[i].bytes;
        if (!R.ext[x]) {
            fprintf(stderr, "render context: no view of %08X\n", (unsigned)k_external[x].address);
            return -1;
        }
    }
    R.host = *workers;
    R.light.world.d00810700 = R.ext[X_810700];
    build_views();
    R.bound = 1;
    R.head_ran = 0;
    return 0;
}

/* ---- the bound originals -------------------------------------------------- */

#define READY(bind) do { if (!R.loaded || R.fault || ((bind) && !R.bound)) return -1; } while (0)

int em_rcl_001D1AE0(int32_t index)
{
    READY(0);
    return done(em_frh_001D1AE0(&R.frh, index), 0x001D1AE0u);
}

int em_rcl_001D1C50(void)
{
    READY(1);
    const u32 zoom = s_ctx_words[0x2468 / 4];   /* 001D1C50 stores no zoom */
    if (done(em_frh_001D1C50(&R.frh), 0x001D1C50u) < 0) return -1;
    R.head_zoom = zoom;
    R.head_ran = 1;
    return 0;
}

int em_rcl_001D1EA0(int32_t a0)
{
    READY(1);
    return done(em_frh_001D1EA0(&R.frh, a0), 0x001D1EA0u);
}

int em_rcl_001C1DC0(void)
{
    READY(1);
    const EmRvrAreaWorkers w = {NULL, g_001D2830, g_001E2260, g_001E2270, g_001E2280,
                                g_001D52E0, g_001D8FD0, g_001C1EA0};
    if (em_rvr_001C1DC0(R.ext[X_810700], &w, &s_rvr_fault) < 0)
        return fail(s_rvr_fault.address ? s_rvr_fault.address : 0x001C1DC0u, "001C1DC0 fault");
    return done(0, 0x001C1DC0u);
}

int em_rcl_001D25F0(uint32_t zoom)
{
    READY(0);
    return done(em_frh_001D25F0(&R.frh, zoom), 0x001D25F0u);
}

int em_rcl_001D2610(uint32_t x)
{
    READY(1);
    return done(em_frh_001D2610(&R.frh, x), 0x001D2610u);
}

int em_rcl_001D2830(int32_t a0, int32_t a1)
{
    READY(0);
    int32_t ignored;
    return done(em_frh_001D2830(&R.frh, a0, a1, &ignored), 0x001D2830u);
}

int em_rcl_001E0CC0(void)
{
    READY(0);
    return done(em_render_context_001E0CC0(&R.rc), 0x001E0CC0u);
}

int em_rcl_0021B9A0(int32_t mode, uint32_t scale, uint32_t bias)
{
    READY(0);
    return done(em_packet_chain_0021B9A0(&R.pc, mode, scale, bias), 0x0021B9A0u);
}

int em_rcl_001DD950(uint32_t a0, uint32_t f12, uint32_t f13)
{
    READY(1);
    return done(em_render_context_001DD950(&R.rc, a0, f12, f13), 0x001DD950u);
}

/* ---- readers --------------------------------------------------------------- */

float em_rcl_zoom(void)
{
    return f32(s_ctx_words[0x2468 / 4]);
}

const uint8_t *em_rcl_bytes(uint32_t address, uint32_t size)
{
    return R.loaded ? own(address, size) : NULL;
}

int em_rcl_frame_fog(float coef[2], float rgb[3])
{
    if (!R.bound || !R.head_ran || R.fault) return -1;
    const u32 index = s_ctx_words[0x9C / 4];
    /* 001D30A0: skin record slot +0x50 = context +0xA0 (the first record);
     * 001D1C50: GS block +0x360 + 0x30 index = context +0xB0 (FOGCOL). */
    const uint8_t *fog = own(SKIN_816440 + (index << 7) + 0x50u, 16);
    const uint8_t *col = own(GS_BLOCKS + index * 0x30u + 0x360u, 8);
    if (!fog || !col) return -1;
    coef[0] = f32(rd32(fog + 8));
    coef[1] = f32(rd32(fog + 12));
    for (unsigned i = 0; i < 3; ++i) rgb[i] = (float)col[i];
    return 0;
}

int em_rcl_frame_view(uint32_t view[16], float *zoom)
{
    if (!R.bound || !R.head_ran || R.fault) return -1;
    memcpy(view, CTXB + 0x2380, 0x40);
    *zoom = f32(R.head_zoom);
    return 0;
}

int em_rcl_frame_matrices(uint32_t p[16], uint32_t clip[16], uint32_t k[16])
{
    if (!R.bound || !R.head_ran || R.fault) return -1;
    memcpy(p, CTXB + 0x2340, 0x40);
    memcpy(clip, CTXB + 0x2240, 0x40);
    memcpy(k, CTXB + 0x23C0, 0x40);
    return 0;
}

int em_rcl_poke(uint32_t address, const uint8_t *bytes, uint32_t size)
{
    uint8_t *p = R.loaded ? own(address, size) : NULL;
    if (!p || !bytes) return -1;
    for (unsigned x = 0; x < X_COUNT; ++x)
        if (R.ext[x] && p >= R.ext[x] && p < R.ext[x] + k_external[x].size) return -1;
    memcpy(p, bytes, size);
    return 0;
}
