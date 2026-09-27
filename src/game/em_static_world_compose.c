/* 001C1D00's call tree composed from the verified translations over one set
 * of views. See em_static_world_compose.h and docs/STATIC_WORLD.md. */
#include "game/em_static_world_compose.h"

#include <stddef.h>
#include <string.h>

#include "game/em_effect_original.h"
#include "game/em_frame_render_heads.h"
#include "game/em_load_veil_particles.h"
#include "game/em_player_stage_workers.h"
#include "game/em_render_context.h"
#include "game/em_render_verify_rest.h"
#include "game/em_sdk_math_original.h"
#include "game/em_stream_lanes_original.h"

typedef uint32_t u32;
typedef uint64_t u64;

#define MAX_VIEWS 32u

typedef struct {
    EmSwc *c;
    u32 ctx;                  /* the value of D_00275670 */
    uint8_t *ctx_bytes;       /* host bytes of [ctx, ctx + 0x2540) */
    EmStaticWorld sw;
    EmRenderContext rc;
    EmFrh frh;
    EmLoadVeilParticles lvp;
    EmRvrRenderContext rvr_rc;
    EmRvrFault rvr_fault;
    EmRenderContextView rc_views[MAX_VIEWS];
    EmFrhView frh_views[MAX_VIEWS];
} Env;

/* ------------------------------------------------------------------ */
/* Faults and views.                                                  */
/* ------------------------------------------------------------------ */

static int latched(const EmSwc *c) { return c->fault.code != 0; }

static int note(EmSwc *c, int32_t module, u32 address, int32_t code, u32 detail)
{
    if (!latched(c)) {
        c->fault.module = module;
        c->fault.address = address;
        c->fault.code = code;
        c->fault.detail = detail;
    }
    return -1;
}

/* After a translation failed: latch its fault (the innermost one wins,
 * because the innermost wrapper returns first). */
static int failed(Env *e, int32_t module)
{
    switch (module) {
    case EM_SWC_MODULE_STATIC_WORLD:
        return note(e->c, module, e->sw.fault.address, e->sw.fault.code, e->sw.fault.detail);
    case EM_SWC_MODULE_RENDER_CONTEXT:
        return note(e->c, module, e->rc.fault.address, e->rc.fault.code, 0);
    case EM_SWC_MODULE_FRAME_HEADS:
        return note(e->c, module, e->frh.fault.address, e->frh.fault.code, e->frh.fault.data);
    case EM_SWC_MODULE_VERIFY_REST:
        return note(e->c, module, e->rvr_fault.address, e->rvr_fault.code, 0);
    case EM_SWC_MODULE_VEIL:
        return note(e->c, module, e->lvp.fault.address, e->lvp.fault.code, 0);
    default:
        return note(e->c, EM_SWC_MODULE_COMPOSE, 0, 1, 0);
    }
}

static uint8_t *host(const EmSwc *c, u32 a, u32 n)
{
    for (u32 i = 0; c->views && i < c->view_count; ++i) {
        const EmStaticWorldView *v = &c->views[i];
        if (v->bytes && a >= v->address && (u64)a + n <= (u64)v->address + v->size)
            return v->bytes + (a - v->address);
    }
    return NULL;
}

static const EmStaticWorldView *view_of(const EmSwc *c, u32 a)
{
    for (u32 i = 0; c->views && i < c->view_count; ++i) {
        const EmStaticWorldView *v = &c->views[i];
        if (v->bytes && a >= v->address && (u64)a < (u64)v->address + v->size) return v;
    }
    return NULL;
}

static u32 rd32(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

static void tr(Env *e, u32 address, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5)
{
    if (e->c->trace) {
        const u64 args[6] = {a0, a1, a2, a3, a4, a5};
        e->c->trace(e->c->trace_ctx, address, args);
    }
}
static u64 sx(u32 v) { return (u64)(int64_t)(int32_t)v; }

/* ------------------------------------------------------------------ */
/* Workers of em_static_world.                                        */
/* ------------------------------------------------------------------ */

#define ENV(p) Env *e = (Env *)(p)

static int v_1F80(void *p, int32_t chan, int32_t a1, int32_t a2)
{
    ENV(p);
    return em_load_veil_particles_001D1F80(&e->lvp, chan, a1, a2) < 0 ? failed(e, EM_SWC_MODULE_VEIL) : 0;
}
static int v_1FF0(void *p, int32_t chan, int32_t a1)
{
    ENV(p);
    return em_load_veil_particles_001D1FF0(&e->lvp, chan, a1) < 0 ? failed(e, EM_SWC_MODULE_VEIL) : 0;
}
static int v_2040(void *p, int32_t chan, int32_t a1)
{
    ENV(p);
    return em_load_veil_particles_001D2040(&e->lvp, chan, a1) < 0 ? failed(e, EM_SWC_MODULE_VEIL) : 0;
}
static int v_7080(void *p, int32_t chan, uint32_t rgba, uint32_t f12)
{
    ENV(p);
    return em_load_veil_particles_001D7080(&e->lvp, chan, rgba, f12) < 0 ? failed(e, EM_SWC_MODULE_VEIL) : 0;
}
static int v_6BA0(void *p, int32_t chan, int32_t a1, int32_t a2, int32_t a3, int32_t t0, int32_t t1)
{
    ENV(p);
    uint32_t r;
    return em_load_veil_particles_001D6BA0(&e->lvp, chan, a1, a2, a3, t0, t1, &r) < 0
               ? failed(e, EM_SWC_MODULE_VEIL) : 0;
}
static int s_8250(void *p, uint32_t f12, uint32_t *result)
{
    (void)p;
    *result = em_stream_lanes_00128250(f12);
    return 0;
}
static int s_81C0(void *p, uint32_t f12, int32_t *result)
{
    (void)p;
    *result = em_player_float_to_int(f12);
    return 0;
}
static int s_DF78(void *p, uint32_t x, uint32_t *result)
{
    (void)p;
    float f, g;
    memcpy(&f, &x, 4);
    g = em_sdk_math_original_0011DF78(f);
    memcpy(result, &g, 4);
    return 0;
}
static int s_26A0(void *p, uint32_t out[4], const uint32_t m[16], const uint32_t v[4])
{
    (void)p;
    float mf[16], vf[4], of[4];
    memcpy(mf, m, sizeof mf);
    memcpy(vf, v, sizeof vf);
    em_effect_original_001026A0(of, mf, vf);
    memcpy(out, of, sizeof of);
    return 0;
}
static int r_2910(void *p, int32_t flag, uint32_t *result)
{
    ENV(p);
    return em_render_context_001D2910(&e->rc, flag, result) < 0 ? failed(e, EM_SWC_MODULE_RENDER_CONTEXT) : 0;
}
static int r_2E00(void *p, int32_t a0, uint32_t *result)
{
    ENV(p);
    return em_render_context_001D2E00(&e->rc, a0, result) < 0 ? failed(e, EM_SWC_MODULE_RENDER_CONTEXT) : 0;
}
/* host callees (a NULL one is a fault when reached) */
static int h_22BB8(void *p, int32_t *result)
{
    ENV(p);
    const EmSwcHostWorkers *h = &e->c->host;
    return h->w_00122BB8 && h->w_00122BB8(h->ctx, result) >= 0 ? 0 : note(e->c, EM_SWC_MODULE_COMPOSE, 0x00122BB8u, 1, 0);
}
static int h_73A0(void *p, int32_t handle, int32_t *result)
{
    ENV(p);
    const EmSwcHostWorkers *h = &e->c->host;
    return h->w_001D73A0 && h->w_001D73A0(h->ctx, handle, result) >= 0 ? 0 : note(e->c, EM_SWC_MODULE_COMPOSE, 0x001D73A0u, 1, 0);
}
static int h_72D0(void *p, const uint32_t snd[4], int32_t *result)
{
    ENV(p);
    const EmSwcHostWorkers *h = &e->c->host;
    return h->w_001D72D0 && h->w_001D72D0(h->ctx, snd, result) >= 0 ? 0 : note(e->c, EM_SWC_MODULE_COMPOSE, 0x001D72D0u, 1, 0);
}
static int h_75E0(void *p, int32_t chan, const uint32_t snd[4], uint64_t a2, uint32_t a3, uint32_t f12, uint32_t f13)
{
    ENV(p);
    const EmSwcHostWorkers *h = &e->c->host;
    return h->w_001D75E0 && h->w_001D75E0(h->ctx, chan, snd, a2, a3, f12, f13) >= 0
               ? 0 : note(e->c, EM_SWC_MODULE_COMPOSE, 0x001D75E0u, 1, 0);
}
static int h_EFD0(void *p, const uint32_t snd[4], const uint32_t vol[4])
{
    ENV(p);
    const EmSwcHostWorkers *h = &e->c->host;
    return h->w_001CEFD0 && h->w_001CEFD0(h->ctx, snd, vol) >= 0 ? 0 : note(e->c, EM_SWC_MODULE_COMPOSE, 0x001CEFD0u, 1, 0);
}
static int h_1760(void *p, int32_t chan)
{
    ENV(p);
    const EmSwcHostWorkers *h = &e->c->host;
    return h->w_001E1760 && h->w_001E1760(h->ctx, chan) >= 0 ? 0 : note(e->c, EM_SWC_MODULE_COMPOSE, 0x001E1760u, 1, 0);
}
static int h_17E0(void *p, int32_t chan)
{
    ENV(p);
    const EmSwcHostWorkers *h = &e->c->host;
    return h->w_001E17E0 && h->w_001E17E0(h->ctx, chan) >= 0 ? 0 : note(e->c, EM_SWC_MODULE_COMPOSE, 0x001E17E0u, 1, 0);
}

/* ------------------------------------------------------------------ */
/* Workers of em_render_context (001D5370 / 001D52E0 / 001E0CC0).     */
/* ------------------------------------------------------------------ */

static int sw_rc(Env *e, int rc) { return rc < 0 ? failed(e, EM_SWC_MODULE_STATIC_WORLD) : 0; }

static int c_4DA0(void *p) { ENV(p); return sw_rc(e, em_static_world_001D4DA0(&e->sw)); }
static int c_4FB0(void *p, uint32_t o) { ENV(p); return sw_rc(e, em_static_world_001D4FB0(&e->sw, o)); }
static int c_4B20(void *p, uint32_t o) { ENV(p); return sw_rc(e, em_static_world_001D4B20(&e->sw, o)); }
static int c_26D0(void *p, uint32_t d, uint32_t a, uint32_t b)
{
    ENV(p);
    return sw_rc(e, em_static_world_001026D0(&e->sw, d, a, b));
}
static int c_6120(void *p, uint32_t bank, int32_t id, uint32_t *result)
{
    ENV(p);
    return sw_rc(e, em_static_world_001C6120(&e->sw, bank, id, result));
}
static int c_1F80(void *p, int32_t chan, int32_t a1, int32_t a2)
{
    ENV(p);
    tr(e, 0x001D1F80u, sx((u32)chan), sx((u32)a1), sx((u32)a2), 0, 0, 0);
    return v_1F80(p, chan, a1, a2);
}
static int c_5BD0(void *p)
{
    ENV(p);
    tr(e, 0x001D5BD0u, 0, 0, 0, 0, 0, 0);
    const EmSwcHostWorkers *h = &e->c->host;
    return h->w_001D5BD0 && h->w_001D5BD0(h->ctx) >= 0 ? 0 : note(e->c, EM_SWC_MODULE_COMPOSE, 0x001D5BD0u, 1, 0);
}
/* 001D2D20(m, focal, width, height, near, far): em_frh_001D2D20 builds the
 * matrix natively; it is then stored at m (the original's stores cover the
 * same 64 bytes). */
static int c_2D20(void *p, uint32_t m, uint32_t f12, uint32_t f13, uint32_t f14, uint32_t f15, uint32_t f16)
{
    ENV(p);
    tr(e, 0x001D2D20u, sx(m), f12, f13, f14, f15, f16);
    uint32_t out[16];
    if (em_frh_001D2D20(&e->frh, out, f12, f13, f14, f15, f16) < 0) return failed(e, EM_SWC_MODULE_FRAME_HEADS);
    uint8_t *dst = host(e->c, m, 0x40);
    if (!dst || (m & 15u)) return note(e->c, EM_SWC_MODULE_COMPOSE, 0x001D2D20u, 4, m);
    for (u32 i = 0; i < 16; ++i) {
        dst[4 * i] = (uint8_t)out[i];
        dst[4 * i + 1] = (uint8_t)(out[i] >> 8);
        dst[4 * i + 2] = (uint8_t)(out[i] >> 16);
        dst[4 * i + 3] = (uint8_t)(out[i] >> 24);
    }
    return 0;
}
static int c_unreached(void *p) { ENV(p); return note(e->c, EM_SWC_MODULE_COMPOSE, 0, 1, 0); }
static int c_unreached_u(void *p, uint32_t a) { (void)a; return c_unreached(p); }

/* ------------------------------------------------------------------ */
/* Workers of em_render_verify_rest (001E0CF0) and em_frame_render_heads */
/* (001C1D00).                                                        */
/* ------------------------------------------------------------------ */

static int b_0CC0(void *p)
{
    ENV(p);
    tr(e, 0x001E0CC0u, 0, 0, 0, 0, 0, 0);
    return em_render_context_001E0CC0(&e->rc) < 0 ? failed(e, EM_SWC_MODULE_RENDER_CONTEXT) : 0;
}
static int b_2910(void *p, int32_t flag, int32_t *result)
{
    ENV(p);
    tr(e, 0x001D2910u, sx((u32)flag), 0, 0, 0, 0, 0);
    uint32_t r;
    if (em_render_context_001D2910(&e->rc, flag, &r) < 0) return failed(e, EM_SWC_MODULE_RENDER_CONTEXT);
    *result = (int32_t)r;
    return 0;
}
static int b_1E60(void *p, uint32_t off, int32_t chan, uint32_t *result)
{
    ENV(p);
    return sw_rc(e, em_static_world_001E1E60(&e->sw, e->ctx + off, chan, result));
}
static int b_1AD0(void *p, uint32_t off, int32_t chan, uint32_t *result)
{
    ENV(p);
    return sw_rc(e, em_static_world_001E1AD0(&e->sw, e->ctx + off, chan, result));
}
static const EmRvrBackgroundWorkers *bg_workers(Env *e, EmRvrBackgroundWorkers *w)
{
    memset(w, 0, sizeof *w);
    w->ctx = e;
    w->w_001E0CC0 = b_0CC0;
    w->w_001D2910 = b_2910;
    w->w_001E1E60 = b_1E60;
    w->w_001E1AD0 = b_1AD0;
    return w;
}
static int f_0CF0(void *p)
{
    ENV(p);
    tr(e, 0x001E0CF0u, 0, 0, 0, 0, 0, 0);
    EmRvrBackgroundWorkers w;
    if (em_rvr_001E0CF0(&e->rvr_rc, bg_workers(e, &w), &e->rvr_fault) < 0) {
        if (latched(e->c)) return -1;
        return failed(e, EM_SWC_MODULE_VERIFY_REST);
    }
    return 0;
}
static int f_5370(void *p)
{
    ENV(p);
    tr(e, 0x001D5370u, 0, 0, 0, 0, 0, 0);
    if (em_render_context_001D5370(&e->rc) < 0) {
        if (latched(e->c)) return -1;
        return failed(e, EM_SWC_MODULE_RENDER_CONTEXT);
    }
    return 0;
}
static int f_2260(void *p, uint64_t tag)
{
    ENV(p);
    tr(e, 0x001E2260u, tag, 0, 0, 0, 0, 0);
    return em_rvr_001E2260(&e->rvr_rc, tag, &e->rvr_fault) < 0 ? failed(e, EM_SWC_MODULE_VERIFY_REST) : 0;
}

/* ------------------------------------------------------------------ */
/* The environment.                                                   */
/* ------------------------------------------------------------------ */

static int setup(EmSwc *c, Env *e)
{
    memset(e, 0, sizeof *e);
    e->c = c;
    if (!c->views || c->view_count == 0 || c->view_count > MAX_VIEWS)
        return note(c, EM_SWC_MODULE_COMPOSE, 0, 4, 0);
    const uint8_t *w = host(c, EM_SW_D_00275670, 8);
    if (!w) return note(c, EM_SWC_MODULE_COMPOSE, 0, 4, EM_SW_D_00275670);
    e->ctx = rd32(w);
    e->ctx_bytes = host(c, e->ctx, 0x2540);
    if (!e->ctx_bytes) return note(c, EM_SWC_MODULE_COMPOSE, 0, 4, e->ctx);
    for (u32 i = 0; i < c->view_count; ++i) {
        e->rc_views[i] = (EmRenderContextView){c->views[i].address, c->views[i].size, c->views[i].bytes};
        e->frh_views[i] = (EmFrhView){c->views[i].address, c->views[i].size, c->views[i].bytes,
                                      c->read_only && c->read_only[i] ? 0 : 1};
    }
    /* em_static_world */
    e->sw.views = c->views;
    e->sw.view_count = c->view_count;
    e->sw.read_only = c->read_only;
    e->sw.trace = c->trace;
    e->sw.trace_ctx = c->trace_ctx;
    EmStaticWorldWorkers *s = &e->sw.workers;
    s->ctx = e;
    s->w_001D1F80 = v_1F80; s->w_001D1FF0 = v_1FF0; s->w_001D2040 = v_2040; s->w_001D7080 = v_7080;
    s->w_00128250 = s_8250; s->w_001D2910 = r_2910;
    s->w_00122BB8 = h_22BB8; s->w_001D73A0 = h_73A0; s->w_001D72D0 = h_72D0; s->w_001D75E0 = h_75E0;
    s->w_001CEFD0 = h_EFD0;
    s->w_001D2E00 = r_2E00; s->w_001E1760 = h_1760; s->w_001D6BA0 = v_6BA0; s->w_001281C0 = s_81C0;
    s->w_001E17E0 = h_17E0; s->w_001026A0 = s_26A0; s->w_0011DF78 = s_DF78;
    /* em_render_context */
    e->rc.world.ctx = e->ctx;
    e->rc.world.views = e->rc_views;
    e->rc.world.view_count = c->view_count;
    EmRenderContextWorkers *r = &e->rc.workers;
    r->ctx = e;
    r->w_001D4DA0 = c_4DA0; r->w_001D4FB0 = c_4FB0; r->w_001D4B20 = c_4B20;
    r->w_001026D0 = c_26D0; r->w_001C6120 = c_6120; r->w_001D1F80 = c_1F80;
    r->w_001D5BD0 = c_5BD0; r->w_001D2D20 = c_2D20;
    /* callees of the other render-context routines: not reached from here */
    r->w_001DE920 = c_unreached; r->w_001DDB70 = c_unreached; r->w_001DFF70 = c_unreached;
    r->w_001DF110 = c_unreached_u;
    /* em_frame_render_heads (001C1D00 and 001D2D20) */
    e->frh.views = e->frh_views;
    e->frh.view_count = c->view_count;
    EmFrhWorkers *f = &e->frh.workers;
    f->ctx = e;
    f->w_001D2910 = b_2910; f->w_001E2260 = f_2260; f->w_001E0CF0 = f_0CF0; f->w_001D5370 = f_5370;
    /* em_render_verify_rest */
    e->rvr_rc.bytes = e->ctx_bytes;
    e->rvr_rc.size = 0x2540u;
    /* em_load_veil_particles over the same storage */
    EmLoadVeilParticlesWorld *lw = &e->lvp.world;
    uint8_t *cursor = host(c, e->ctx + 0x10u, 0x10);
    uint8_t *slot = host(c, e->ctx + 0x9Cu, 4);
    uint8_t *block = host(c, EM_SW_D_00275674, 4);
    if (!cursor || !slot || !block || ((uintptr_t)cursor & 3u) || ((uintptr_t)slot & 3u) || ((uintptr_t)block & 3u))
        return note(c, EM_SWC_MODULE_COMPOSE, 0, 4, e->ctx + 0x10u);
    lw->cursor = (uint32_t *)(void *)cursor;
    lw->cursor_count = 4;
    lw->ctx_9C = (const uint32_t *)(const void *)slot;
    lw->d00275674 = (const uint32_t *)(const void *)block;
    const EmStaticWorldView *arena = view_of(c, rd32(cursor));
    if (!arena) return note(c, EM_SWC_MODULE_COMPOSE, 0, 4, rd32(cursor));
    lw->packet = arena->bytes;
    lw->packet_address = arena->address;
    lw->packet_size = arena->size;
    return 0;
}

int em_swc_001C1D00(EmSwc *c, uint32_t state_address)
{
    if (!c || latched(c)) return -1;
    Env e;
    if (setup(c, &e) < 0) return -1;
    tr(&e, 0x001C1D00u, sx(state_address), 0, 0, 0, 0, 0);
    if (em_frh_001C1D00(&e.frh, state_address) < 0)
        return latched(c) ? -1 : failed(&e, EM_SWC_MODULE_FRAME_HEADS);
    return 0;
}

int em_swc_001E0CF0(EmSwc *c)
{
    if (!c || latched(c)) return -1;
    Env e;
    if (setup(c, &e) < 0) return -1;
    return f_0CF0(&e);
}

int em_swc_001D5370(EmSwc *c)
{
    if (!c || latched(c)) return -1;
    Env e;
    if (setup(c, &e) < 0) return -1;
    return f_5370(&e);
}

int em_swc_001D52E0(EmSwc *c)
{
    if (!c || latched(c)) return -1;
    Env e;
    if (setup(c, &e) < 0) return -1;
    tr(&e, 0x001D52E0u, 0, 0, 0, 0, 0, 0);
    if (em_render_context_001D52E0(&e.rc) < 0)
        return latched(c) ? -1 : failed(&e, EM_SWC_MODULE_RENDER_CONTEXT);
    return 0;
}

int em_swc_001E1E60(EmSwc *c, uint32_t a0, int32_t chan, uint32_t *result)
{
    if (!c || latched(c)) return -1;
    Env e;
    if (setup(c, &e) < 0) return -1;
    return sw_rc(&e, em_static_world_001E1E60(&e.sw, a0, chan, result));
}

int em_swc_001E1AD0(EmSwc *c, uint32_t a0, int32_t chan, uint32_t *result)
{
    if (!c || latched(c)) return -1;
    Env e;
    if (setup(c, &e) < 0) return -1;
    return sw_rc(&e, em_static_world_001E1AD0(&e.sw, a0, chan, result));
}

int em_swc_with_static_world(EmSwc *c, int (*body)(EmStaticWorld *sw, void *arg), void *arg)
{
    if (!c || latched(c) || !body) return -1;
    Env e;
    if (setup(c, &e) < 0) return -1;
    return sw_rc(&e, body(&e.sw, arg));
}
