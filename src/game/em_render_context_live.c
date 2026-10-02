/* em_render_context_live.c - the one canonical render context (see
 * em_render_context_live.h, docs/RENDER_CONTEXT.md section 8).
 *
 * This file adds no behaviour of its own: it owns the storage the original
 * routines address and wires each lane translation's workers to the other
 * translations over that one storage. Every worker below names the original
 * callee it stands for. */
#include "game/em_render_context_live.h"
#include "game/em_gs_blocks_original.h"
#include "game/em_chain_page.h"

#include "game/em_actor_light_001D89D0.h"
#include "game/em_census_standins.h"
#include "game/em_effect_original.h"
#include "game/em_frame_kick.h"
#include "game/em_frame_render_heads.h"
#include "game/em_load_veil_particles.h"
#include "game/em_skin_arena_init.h"
#include "game/em_packet_chain_original.h"
#include "game/em_player_equipment.h"
#include "game/em_player_stage_workers.h"
#include "game/em_point_light.h"
#include "game/em_render_context.h"
#include "game/em_render_verify_rest.h"
#include "game/em_sdk_math_original.h"
#include "game/em_static_world_compose.h"
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
#define D_250F30_SIZE 0x2250u   /* through D_00253170 (001D8340's fold seed) */
#define D_26E510    0x0026E510u
#define D_26E850    0x0026E850u
#define D_26E880    0x0026E880u   /* 001DFA40's frame-copy colour (the load veil) */
#define D_275670    0x00275670u
#define SPAD_3A40   0x70003A40u
#define SPAD_3B60   0x70003B60u
#define SPAD_3B70   0x70003B70u   /* halfwords 3B70 / 3B72 (001AB370) */
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
/* The static world (docs/STATIC_WORLD.md section 5): 001C1D00's tree. */
#define D_253560    0x00253560u   /* 001E1E60's upload block (export block 1) */
#define D_253560_SIZE 0x90u
#define D_817240    0x00817240u   /* 001D4750's constant block (.bss) */
#define SPAD_3400   0x70003400u   /* 001D5370's two clip matrices */
#define D_8101D0    0x008101D0u   /* 001C1D00's state block (001AF690 zeroes 0x10) */
#define D_28A5A0    0x0028A5A0u   /* the bank address word (the loader's slot 0x44) */
#define VIEWS_MAX   32u

static uint32_t s_arena_words[(ARENA_END - ARENA_BASE) / 4];
static uint32_t s_ctx_words[(CTX_END - CTX_BASE) / 4];
static uint32_t s_d250F30_words[D_250F30_SIZE / 4];
static uint32_t s_d275670_words[0x30 / 4];
static uint32_t s_spad3A40_words[0x100 / 4];
static uint32_t s_spad3B60_words[1];
static uint32_t s_spad3B70_words[1];
static uint8_t s_d241010[8];
static uint8_t s_d26E510[16];
static uint8_t s_d26E850[16];
static uint8_t s_d26E880[16];
static uint32_t s_d253560_words[D_253560_SIZE / 4];
static uint32_t s_d817240_words[0x80 / 4];
static uint32_t s_spad3400_words[0x80 / 4];
static uint32_t s_d8101D0_words[0x10 / 4];
/* The static-object bank *D_0028A5A0 (static_world.emsw block 0), read only. */
static uint8_t *s_bank;
static uint32_t s_bank_address, s_bank_size;
/* 001DFA40's 16 x 16 table (its stack frame at sp + 0xD0): lanes 0..2 of
 * each entry are written every call, lane 3 never (docs/LOAD_VEIL_PARTICLES.md
 * section 5). */
static uint8_t s_veil_table[EM_LOAD_VEIL_PARTICLES_TABLE_BYTES];

#define ARENA ((uint8_t *)s_arena_words)
#define CTXB ((uint8_t *)s_ctx_words)

/* External views (EmRclExternal) the binder hands over (X_810610..X_8102B0,
 * at each area load), and the frame loop's views from the start
 * (D_00810E80 at em_rcl_init; D_00810E88 and D_008106C4 through
 * em_rcl_frame_views: main-loop step V reads them in every iteration, before
 * the first area load too). While the binder's request block is bound,
 * own() resolves D_008106C4 through it first (the same bytes). */
enum { X_810E80, X_810610, X_8105E0, X_8106B0, X_810700, X_8101E4, X_3B8D, X_8102B0, X_28A5A0, X_BIND_END,
       X_810E88 = X_BIND_END, X_8106C4, X_COUNT };
static const struct { u32 address, size; } k_external[X_COUNT] = {
    {D_810E80, 2}, {D_810610, 0x40}, {D_8105E0, 0x10}, {D_8106B0, 0x48},
    {D_810700, 3}, {D_8101E4, 1}, {SPAD_3B8D, 1}, {D_8102B0, 0x320}, {D_28A5A0, 4},
    {0x00810E88u, 2}, {0x008106C4u, 1},
};

static struct {
    int loaded, bound, head_ran;
    u32 head_zoom;   /* +0x2468 as the last 001D1C50's 001D2960 read it */
    u32 fault;
    uint8_t *ext[X_COUNT];
    EmRclWorkers host;
    /* the lane modules, all over the storage above */
    EmFrhView frh_views[VIEWS_MAX];
    EmRenderContextView rc_views[VIEWS_MAX];
    EmPacketChainRegion pc_regions[VIEWS_MAX];
    /* the same views for 001C1D00's tree (em_static_world_compose), with
     * the read-only marking em_frh's views carry */
    EmStaticWorldView swc_views[VIEWS_MAX];
    uint8_t swc_read_only[VIEWS_MAX];
    unsigned view_count;
    EmFrh frh;
    EmRenderContext rc;
    EmPacketChain pc;
    EmLoadVeilParticles veil;
    EmActorLight light;
    /* the page 001CB800 spliced at the last kick (em_rcl_page), and the
     * weather's list 001E0D70 CALLed into it (em_rcl_page_weather) */
    u32 page_start, page_four_sprite, frame_four_sprite;
    u32 page_weather, frame_weather;
    int page_ready;
    /* main-loop step V (001D2300): its kick's list, the kicks so far */
    u32 kick_chain, kicks;
    /* the load veil's 0021B1B0 in this frame: its channel-0 run and the
     * buffer index (em_rcl_veil_span), cleared by the frame head */
    u32 veil_start, veil_end, veil_slot;
    int veil_ready;
    /* 001C1D00 in this frame: its channel-0 static run (em_rcl_static_run),
     * cleared by the frame head; the static world's bank loaded */
    u32 static_start, static_end, static_runs;
    int static_ready, static_loaded;
    EmSwcFault swc_fault;
    EmFrameKickFault kick_fault;
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
    if (R.view_count >= VIEWS_MAX) {
        fail(address, "more views than VIEWS_MAX");
        return;
    }
    unsigned i = R.view_count++;
    R.frh_views[i] = (EmFrhView){address, size, bytes, writable};
    R.rc_views[i] = (EmRenderContextView){address, size, bytes};
    R.pc_regions[i] = (EmPacketChainRegion){address, size, bytes};
    R.swc_views[i] = (EmStaticWorldView){address, size, bytes};
    R.swc_read_only[i] = writable ? 0 : 1;
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
    add_view(SPAD_3B70, 4, (uint8_t *)s_spad3B70_words, 1);
    add_view(D_241010, 8, s_d241010, 0);
    add_view(D_26E510, 16, s_d26E510, 0);
    add_view(D_26E850, 16, s_d26E850, 0);
    add_view(D_26E880, 16, s_d26E880, 0);
    /* 001C1D00's tree (docs/STATIC_WORLD.md 5.2): 001E1E60's upload block,
     * 001D4750's constants, 001D5370's scratchpad matrices, the state block
     * and, once loaded, the bank (read only). */
    add_view(D_253560, D_253560_SIZE, (uint8_t *)s_d253560_words, 1);
    add_view(D_817240, 0x80, (uint8_t *)s_d817240_words, 1);
    add_view(SPAD_3400, 0x80, (uint8_t *)s_spad3400_words, 1);
    add_view(D_8101D0, 0x10, (uint8_t *)s_d8101D0_words, 1);
    if (s_bank) add_view(s_bank_address, s_bank_size, s_bank, 0);
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

int em_rcl_skin_arena_init(void);
static int w_skin_arena_init(void *ctx)
{
    (void)ctx;
    return em_rcl_skin_arena_init();
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
/* 001D1EA0's kick: the splice, and this frame's page start for the page's
 * consumer (em_rcl_page; 001CB800's base, D_00810E80 read as it does). */
static int f_001CB800(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)ctx;
    const int r = em_packet_chain_001CB800(&R.pc, a0, (int32_t)a1, a2, a3);
    if (r == 0 && R.ext[X_810E80]) {
        const int16_t index = (int16_t)(R.ext[X_810E80][0] | R.ext[X_810E80][1] << 8);
        R.page_start = em_chain_page_start(index, (int32_t)a1);
        R.page_four_sprite = R.frame_four_sprite;
        R.page_weather = R.frame_weather;
        R.page_ready = 1;
    }
    R.frame_four_sprite = 0;
    R.frame_weather = 0;
    return r;
}
/* 001DDE10's and 001E0D70's 001CB760: the slot-0xFFF CALL target of this
 * frame (001DDE10's four-sprite packet) is kept for the page's consumer,
 * which walks over it (docs/CHAIN_PAGE.md section 6); 001E0D70's (id
 * 0xFFC000, the weather's channel-3 list) is noted for the level smoke. */
static int f_001CB760(void *ctx, uint32_t table, int32_t id, uint32_t address)
{
    const int r = em_packet_chain_w_001CB760(ctx, table, id, address);
    if (r == 0 && id == 0xFFF000) R.frame_four_sprite = address & 0x0FFFFFFFu;
    if (r == 0 && id == 0xFFC000) R.frame_weather = address & 0x0FFFFFFFu;
    return r;
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
/* 001C1E70's 001D52E0: the grid header from the bank into +0x140..+0x167
 * (em_swc_001D52E0 over this module's views; the bank must be loaded). */
static int swc_setup(EmSwc *c)
{
    memset(c, 0, sizeof *c);
    c->views = R.swc_views;
    c->view_count = R.view_count;
    c->read_only = R.swc_read_only;
    /* c->host: every host worker NULL. 001D5BD0 (keys other than 0x0B00),
     * the flag-0x23 sound branch and 001E1AD0's 001E1760 / 001E17E0 (flag
     * 0x22) are not reached on the first level; reaching one faults. */
    return s_bank ? 0 : -1;
}
static int swc_done(EmSwc *c, int rc, u32 entry)
{
    if (rc >= 0) return 0;
    R.swc_fault = c->fault;
    fprintf(stderr, "render context: static world fault in module %d at %08X (code %d, detail %08X)\n",
            (int)c->fault.module, (unsigned)c->fault.address, (int)c->fault.code, (unsigned)c->fault.detail);
    return fail(c->fault.address ? c->fault.address : entry, "static world fault");
}
static int g_001D52E0(void *ctx)
{
    (void)ctx;
    EmSwc c;
    if (swc_setup(&c) < 0) return fail(0x001D52E0u, "the static-object bank is not loaded");
    return swc_done(&c, em_swc_001D52E0(&c), 0x001D52E0u);
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
    f->w_skin_arena_init = w_skin_arena_init;   /* 001D19E0 itself is not bound (section 8) */
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
    w->w_001CB760 = f_001CB760;
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
    v->d0026E880 = s_d26E880;
    v->table = s_veil_table;
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
    {D_26E880, 16, s_d26E880},
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
     * 001D2730(0, 0) reads (never on the route); its arena pattern reaches
     * only DMA packet bytes. Its GS register blocks at D_00275674 (banks
     * A..G and the header, from 00101898's templates) are the GS state the
     * frame's REFs send: the chain page's blend presets (docs/CHAIN_PAGE.md
     * section 4), the draw environments, clears and per-pass presets the
     * load veil's list sends (docs/LOAD_VEIL_PARTICLES.md section 3); they
     * run here (em_gs_blocks_001D0F20). docs/RENDER_CONTEXT.md section 8. */
    /* 001AB370 (byte-matched; the start-up's GS set-up, main loop 0x1AAE40)
     * ends with the stores 0x70003B70 = 0x70003B72 = 0x800 (halfwords), the
     * screen centre step V's 001015A8 / 00101810 read; nothing else writes
     * them (0x800 in every route capture). Its other stores (the SDK's
     * double-buffer block D_00810EA0, the clear colours D_00811020 /
     * D_00811190, 0x70003B94 / 96) are the renderer's. */
    s_spad3B70_words[0] = UINT32_C(0x08000800);
    if (em_gs_blocks_001D0F20(own(GS_BLOCKS, EM_GS_BLOCKS_SIZE), s_d241010) < 0)
        return fail(0x001D0F20u, "the GS blocks are not in the context storage");
    if (done(em_frh_001D25F0(&R.frh, UINT32_C(0x43F00000)), 0x001D25F0u) < 0) return -1;
    return done(em_render_context_001DEDE0(&R.rc), 0x001DEDE0u);
}

int em_rcl_bind(const EmRclExternal *views, unsigned count, const EmRclWorkers *workers)
{
    if (!R.loaded || R.fault || !workers) return -1;
    for (unsigned x = 1; x < X_BIND_END; ++x) {
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
    /* 001AF690 zeroes D_008101D0..DF at 0x1AE040 state 0; the binder binds
     * at that step (w_001AFCA0), so 001C1D00 starts from state 0 at every
     * area build. */
    memset(s_d8101D0_words, 0, sizeof s_d8101D0_words);
    build_views();
    R.bound = 1;
    R.head_ran = 0;
    R.static_ready = 0;
    return R.fault ? -1 : 0;
}

/* ---- the bound originals -------------------------------------------------- */

#define READY(bind) do { if (!R.loaded || R.fault || ((bind) && !R.bound)) return -1; } while (0)

int em_rcl_page(uint32_t *start, uint32_t *four_sprite)
{
    if (!R.loaded || R.fault || !R.page_ready || !start || !four_sprite) return -1;
    *start = R.page_start;
    *four_sprite = R.page_four_sprite;
    R.page_ready = 0;
    return 0;
}

uint32_t em_rcl_page_weather(void)
{
    return R.loaded ? R.page_weather : 0;
}

int em_rcl_001D1AE0(int32_t index)
{
    READY(0);
    R.veil_ready = 0;   /* the channel cursors start over */
    R.static_ready = 0;
    return done(em_frh_001D1AE0(&R.frh, index), 0x001D1AE0u);
}

int em_rcl_0021B1B0(const EmLoadVeilParticlesBlock *veil)
{
    READY(0);
    const u32 start = s_ctx_words[0x10 / 4];
    if (em_load_veil_particles_0021B1B0(&R.veil, veil) < 0)
        return fail(R.veil.fault.address ? R.veil.fault.address : 0x0021B1B0u, "0021B1B0 fault");
    if (!R.veil_ready) R.veil_start = start;
    R.veil_end = s_ctx_words[0x10 / 4];
    R.veil_slot = s_ctx_words[0x9C / 4];
    R.veil_ready = 1;
    return 0;
}

int em_rcl_veil_span(uint32_t *start, uint32_t *end, uint32_t *slot)
{
    if (!R.loaded || R.fault || !R.veil_ready || !start || !end || !slot) return -1;
    *start = R.veil_start;
    *end = R.veil_end;
    *slot = R.veil_slot;
    R.veil_ready = 0;
    return 0;
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

/* ---- the static world (docs/STATIC_WORLD.md) ------------------------------ */

int em_rcl_static_world_load(const char *path)
{
    if (!R.loaded || R.fault) return -1;
    if (s_bank) return 0;
    FILE *f = fopen(path ? path : EM_RCL_STATIC_WORLD_PATH, "rb");
    if (!f) {
        fprintf(stderr, "render context: %s is missing (run tools/export_static_world.py)\n",
                path ? path : EM_RCL_STATIC_WORLD_PATH);
        return -1;
    }
    uint8_t head[0x30];
    int ok = fread(head, 1, sizeof head, f) == sizeof head && memcmp(head, "EMSW", 4) == 0 &&
             rd32(head + 4) == 1 && rd32(head + 8) == 2;
    const u32 bank_address = ok ? rd32(head + 0x10) : 0, bank_size = ok ? rd32(head + 0x14) : 0;
    ok = ok && bank_address == rd32(head + 12) && bank_size > 0 && bank_size <= 0x1000000u &&
         rd32(head + 0x20) == D_253560 && rd32(head + 0x24) == D_253560_SIZE;
    uint8_t *bank = ok ? malloc(bank_size) : NULL;
    ok = bank && fseek(f, (long)rd32(head + 0x18), SEEK_SET) == 0 && fread(bank, 1, bank_size, f) == bank_size &&
         fseek(f, (long)rd32(head + 0x28), SEEK_SET) == 0 &&
         fread(s_d253560_words, 1, D_253560_SIZE, f) == D_253560_SIZE;
    fclose(f);
    if (!ok) {
        free(bank);
        fprintf(stderr, "render context: %s is not a static-world export\n", path ? path : EM_RCL_STATIC_WORLD_PATH);
        return -1;
    }
    s_bank = bank;
    s_bank_address = bank_address;
    s_bank_size = bank_size;
    build_views();
    return R.fault ? -1 : 0;
}

int em_rcl_static_world_loaded(void) { return s_bank != NULL; }

static EmRclStaticSample s_sample;
static int s_sample_valid;

int em_rcl_001C1D00(uint32_t state_address)
{
    READY(1);
    EmSwc c;
    if (swc_setup(&c) < 0) return fail(0x001C1D00u, "the static-object bank is not loaded");
    const u32 start = s_ctx_words[0x10 / 4], ch3 = s_ctx_words[0x1C / 4];
    /* the inputs, for the smoke's re-execution of the original */
    EmRclStaticSample *sm = &s_sample;
    const uint8_t *cam = R.ext[X_810610], *area = R.ext[X_810700];
    memcpy(sm->ctx, CTXB, sizeof sm->ctx);
    memcpy(sm->spad, s_spad3A40_words, sizeof sm->spad);
    if (cam) memcpy(sm->cam610, cam, sizeof sm->cam610);
    if (area) memcpy(sm->area, area, sizeof sm->area);
    memcpy(sm->state, s_d8101D0_words, sizeof sm->state);
    memcpy(sm->d253560, s_d253560_words, sizeof sm->d253560);
    memcpy(sm->d817240, s_d817240_words, sizeof sm->d817240);
    memcpy(sm->skin, own(SKIN_816440, sizeof sm->skin), sizeof sm->skin);
    if (swc_done(&c, em_swc_001C1D00(&c, state_address), 0x001C1D00u) < 0) return -1;
    R.static_start = start;
    R.static_end = s_ctx_words[0x10 / 4];
    R.static_ready = 1;
    R.static_runs++;
    sm->runs = R.static_runs;
    sm->ch0_start = start;
    sm->ch0_end = R.static_end;
    sm->ch3_start = ch3;
    sm->ch3_end = s_ctx_words[0x1C / 4];
    s_sample_valid = 1;
    return 0;
}

const EmRclStaticSample *em_rcl_static_sample(void)
{
    return s_sample_valid ? &s_sample : NULL;
}

int em_rcl_static_run(uint32_t *start, uint32_t *end)
{
    if (!R.loaded || R.fault || !R.static_ready || !start || !end) return -1;
    *start = R.static_start;
    *end = R.static_end;
    R.static_ready = 0;
    return 0;
}

uint32_t em_rcl_static_runs(void) { return R.static_runs; }

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

int em_rcl_001D2040(int32_t chan, int32_t a1)
{
    READY(1);
    return done(em_load_veil_particles_001D2040(&R.veil, chan, a1), 0x001D2040u);
}

int em_rcl_001E0CC0(void)
{
    READY(0);
    return done(em_render_context_001E0CC0(&R.rc), 0x001E0CC0u);
}

int em_rcl_001D2DE0(int32_t a0, uint32_t a1)
{
    READY(0);
    return done(em_render_context_001D2DE0(&R.rc, a0, a1), 0x001D2DE0u);
}

int em_rcl_0021B9A0(int32_t mode, uint32_t scale, uint32_t bias)
{
    READY(0);
    return done(em_packet_chain_0021B9A0(&R.pc, mode, scale, bias), 0x0021B9A0u);
}

/* 00121870 block_copy inside the context for 0021BAC0 / 0021BAE0: the
 * record +0xA0..+0xBF and the save slots +0x120 + 32 * slot never overlap
 * for the slots the status page uses (0), so a forward copy is the
 * original's result. */
static int slot_copy(void *unused, uint8_t *block, uint32_t dst, uint32_t src, int32_t count)
{
    (void)unused;
    if (!block || count < 0 || (dst < src ? src - dst : dst - src) < (u32)count) return -1;
    for (int32_t i = 0; i < count; ++i)
        block[dst + (u32)i] = block[src + (u32)i];
    return 0;
}

int em_rcl_0021BAC0(int32_t slot)
{
    READY(0);
    EmSulWorkers w;
    memset(&w, 0, sizeof w);
    w.block_copy = slot_copy;
    return em_sul_0021BAC0(&w, (uint8_t *)s_ctx_words, EM_RCL_CONTEXT_SIZE, slot) < 0
               ? fail(0x0021BAC0u, "fault")
               : 0;
}

int em_rcl_0021BAE0(int32_t slot)
{
    READY(0);
    EmSulWorkers w;
    memset(&w, 0, sizeof w);
    w.block_copy = slot_copy;
    return em_cs_0021BAE0(&w, (uint8_t *)s_ctx_words, EM_RCL_CONTEXT_SIZE, slot) < 0
               ? fail(0x0021BAE0u, "fault")
               : 0;
}

int em_rcl_001DD950(uint32_t a0, uint32_t f12, uint32_t f13)
{
    READY(1);
    return done(em_render_context_001DD950(&R.rc, a0, f12, f13), 0x001DD950u);
}

/* ---- main-loop steps V and W, and 001D1EF0 ------------------------------ */

/* The frame loop's views (em_frame owns D_00810E88; the scene state owns
 * D_008106C4). They stay across area binds. */
int em_rcl_frame_views(uint8_t *d810E88, uint8_t *d8106C4)
{
    if (!R.loaded || !d810E88 || !d8106C4) return -1;
    R.ext[X_810E88] = d810E88;
    R.ext[X_8106C4] = d8106C4;
    build_views();
    return 0;
}

/* The step's memory: this module's storage and views, writes only into
 * its own writable ranges (em_rcl_bytes_mut). */
static uint8_t *k_mem(void *ctx, uint32_t address, uint32_t size, int write)
{
    (void)ctx;
    return write ? em_rcl_bytes_mut(address, size) : own(address, size);
}
static int k_001D1F80(void *ctx, int32_t a0, int32_t a1, int32_t a2)
{
    (void)ctx;
    return em_load_veil_particles_001D1F80(&R.veil, a0, a1, a2);
}
static int k_001D2910(void *ctx, int32_t a0, int32_t *ret) { return f_001D2910(ctx, a0, ret); }
static int k_001D2830(void *ctx, int32_t a0, int32_t a1)
{
    (void)ctx;
    int32_t ignored;
    return em_frh_001D2830(&R.frh, a0, a1, &ignored);
}
static int k_001E0DF0(void *ctx) { (void)ctx; return em_render_context_001E0DF0(&R.rc); }
/* 001D21E0's hardware kick: the port's renderer presents the frame
 * (em_gfx_end_frame, the step after V); the list address is kept for the
 * checks (em_rcl_kick). */
static int k_kick(void *ctx, uint32_t chain)
{
    (void)ctx;
    R.kick_chain = chain;
    R.kicks++;
    return 0;
}
static const EmFrameKickWorkers k_kick_workers = {NULL, k_mem, k_001D1F80, k_001D2910, k_001D2830,
                                                  k_001E0DF0, k_kick};

static int kick_done(int rc, u32 entry)
{
    if (rc < 0 && !R.frh.fault.code && !R.rc.fault.code && !R.veil.fault.code)
        return fail(R.kick_fault.address ? R.kick_fault.address : entry, "fault");
    if (rc < 0 && R.veil.fault.code) return fail(R.veil.fault.address, "001D1F80 fault");
    return done(rc, entry);
}

int em_rcl_001D2300(void)
{
    READY(0);
    if (!R.ext[X_810E88] || !R.ext[X_8106C4]) return fail(0x001D2300u, "no frame views");
    return kick_done(em_frame_kick_001D2300(&k_kick_workers, &R.kick_fault), 0x001D2300u);
}

int em_rcl_001D2580(int32_t a0)
{
    READY(0);
    return kick_done(em_frame_kick_001D2580(&k_kick_workers, a0, &R.kick_fault), 0x001D2580u);
}

int em_rcl_001D2300_calls_001E0DF0(int *calls)
{
    READY(0);
    if (!R.ext[X_810E88] || !R.ext[X_8106C4]) return fail(0x001D2300u, "no frame views");
    return kick_done(em_frame_kick_calls_001E0DF0(&k_kick_workers, calls, &R.kick_fault), 0x001D2300u);
}

int em_rcl_kick(uint32_t *chain, uint32_t *kicks)
{
    if (!R.loaded || R.fault || !chain || !kicks) return -1;
    *chain = R.kick_chain;
    *kicks = R.kicks;
    return 0;
}

int em_rcl_001D1EF0(void)
{
    READY(1);
    const u32 zoom = s_ctx_words[0x2468 / 4];   /* its 001D1C50 stores no zoom */
    if (done(em_frh_001D1EF0(&R.frh), 0x001D1EF0u) < 0) return -1;
    R.head_zoom = zoom;
    R.head_ran = 1;
    return 0;
}

/* 001D80B0(id) (byte-matched): the point-light release over this context's
 * slots, through its 001D8060 lookup (em_frh_001D80B0, the one
 * translation): the slot whose +0xC equals id gets +0x2C = 0, then +0xC =
 * -1. The room point-light lists' 001F66F0 calls it (em_effects_live). */
int em_rcl_001D80B0(int32_t id)
{
    READY(0);
    return done(em_frh_001D80B0(&R.frh, id), 0x001D80B0u);
}

/* The point-light slots (001D7BB0 / 001D7C30 / 001D7FA0): the words
 * +0x210 / +0x214, the active slots +0x220 and the staged slots +0x1220 of
 * this context, as the pool's layout (em_point_light.h). */
_Static_assert(sizeof(EmPointLightPool) == 0x2010u, "EmPointLightPool covers context +0x210..+0x221F");
EmPointLightPool *em_rcl_point_lights(void)
{
    if (!R.loaded) return NULL;
    return (EmPointLightPool *)(void *)(CTXB + 0x210);
}

/* ---- readers --------------------------------------------------------------- */

float em_rcl_zoom(void)
{
    return f32(s_ctx_words[0x2468 / 4]);
}

unsigned em_rcl_views(uint32_t *address, uint32_t *size, uint8_t **bytes, int *writable, unsigned capacity)
{
    if (!R.loaded) return 0;
    for (unsigned i = 0; i < R.view_count && i < capacity; ++i) {
        address[i] = R.frh_views[i].address;
        size[i] = R.frh_views[i].size;
        bytes[i] = R.frh_views[i].bytes;
        writable[i] = R.frh_views[i].writable;
    }
    return R.view_count;
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

EmPacketChain *em_rcl_packet_chain(void)
{
    return R.loaded && !R.fault ? &R.pc : NULL;
}

uint8_t *em_rcl_bytes_mut(uint32_t address, uint32_t size)
{
    uint8_t *p = R.loaded ? own(address, size) : NULL;
    if (!p) return NULL;
    for (unsigned x = 0; x < X_COUNT; ++x)
        if (R.ext[x] && p >= R.ext[x] && p < R.ext[x] + k_external[x].size) return NULL;
    if (p >= s_d241010 && p < s_d241010 + sizeof s_d241010) return NULL;
    if (p >= s_d26E510 && p < s_d26E510 + sizeof s_d26E510) return NULL;
    if (p >= s_d26E850 && p < s_d26E850 + sizeof s_d26E850) return NULL;
    if (s_bank && p >= s_bank && p < s_bank + s_bank_size) return NULL;
    return p;
}

int em_rcl_skin_arena_init(void)
{
    if (!R.loaded || R.fault) return -1;
    uint8_t *records = own(EM_SKIN_ARENA_RECORDS, 0x100u * EM_SKIN_ARENA_COUNT);
    const uint8_t *source = own(EM_SKIN_ARENA_SOURCE, 0x80u * EM_SKIN_ARENA_COUNT);
    if (!records || !source) return fail(0x001D2E20u, "skin arena views");
    em_skin_arena_init_001D2E20(records, source);
    return 0;
}

int em_rcl_001D1F80(int32_t a0, int32_t a1, int32_t a2)
{
    if (!R.bound || R.fault) return -1;
    if (em_load_veil_particles_001D1F80(&R.veil, a0, a1, a2) < 0)
        return fail(0x001D1F80u, "001D1F80 fault");
    return 0;
}

int em_rcl_001D2910(int32_t a0, uint32_t *result)
{
    if (!R.loaded || R.fault || !result) return -1;
    if (em_render_context_001D2910(&R.rc, a0, result) < 0) return fail(0x001D2910u, "001D2910 fault");
    return 0;
}

int em_rcl_poke(uint32_t address, const uint8_t *bytes, uint32_t size)
{
    uint8_t *p = R.loaded ? own(address, size) : NULL;
    if (!p || !bytes) return -1;
    for (unsigned x = 0; x < X_COUNT; ++x)
        if (R.ext[x] && p >= R.ext[x] && p < R.ext[x] + k_external[x].size) return -1;
    if (s_bank && p >= s_bank && p < s_bank + s_bank_size) return -1;
    memcpy(p, bytes, size);
    return 0;
}
