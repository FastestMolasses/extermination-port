/* The C side of tools/test_dof_pass_reference.py: 001DDE10's depth-of-field
 * pass (docs/CHAIN_PAGE.md section 6.2) against the original's GS memory
 * captured on both sides of the pass (decomp build/dof_capture, PCSX2 fork,
 * software renderer). Three paths, each over the port's own code:
 *
 *   dof_replay  the captured GIF bytes of the pass through the CPU GS model
 *               (em_gs_raster, strict), from the captured GS memory and
 *               registers before the pass (bands > 1: that many row-band
 *               EmGs in lockstep, as em_gs_world's workers draw);
 *   dof_build   the port's em_render_context_001DDE10 over a captured EE
 *               image, with the workers the live binding gives it
 *               (em_render_context_live.c: em_player_equipment's 0015D2F0,
 *               em_status_ui_leftovers' 0022EBE0, em_effect_original's
 *               001026A0, em_sdk_math_original's 0011DF78,
 *               em_player_stage_workers' 001281C0, em_load_veil_particles'
 *               packet builders); its 001CB760 is recorded, not run (the
 *               captured chain table already holds the slot's block);
 *   dof_live    the Original profile's path: em_chain_page's pass mode over
 *               an EE image (the CALL followed from a slot block of its
 *               own), then em_gs_world (the field declared, the primitives
 *               with their environments, the kick's environment packet at
 *               each mark, `workers` row bands) from the captured GS memory.
 *
 * Nothing here is disc-derived: the captures are read at run time from the
 * user's own build folder. */
#include "gs/em_gs_raster.h"
#include "gs/em_gs_world.h"
#include "game/em_chain_page.h"
#include "game/em_ee_float.h"
#include "game/em_effect_original.h"
#include "game/em_load_veil_particles.h"
#include "game/em_player_equipment.h"
#include "game/em_player_stage_workers.h"
#include "game/em_render_context.h"
#include "game/em_sdk_math_original.h"
#include "game/em_status_ui_leftovers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_BYTES 0x2000000u
#define SPAD_BASE 0x70000000u
#define SPAD_BYTES 0x4000u

/* ------------------------------------------------------------------ A */

/* Word addresses of a w x h PSMCT32 (z 0) or PSMZ32 / Z24 (z 1) buffer, for
 * the test's decoding of local memory (the model's own tables). */
void dof_map32(uint32_t bp, uint32_t bw, uint32_t w, uint32_t h, int z, uint32_t *out)
{
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) out[y * w + x] = em_gs_addr32(bp, bw, x, y, z);
}

int dof_replay(uint8_t *mem, const uint32_t *reg, const uint64_t *val, uint32_t n, const uint8_t *gif,
               uint64_t gif_bytes, uint32_t bands, uint64_t out[6], char *reason, uint32_t reason_cap)
{
    if (!mem || (n && (!reg || !val)) || !gif || !out || bands == 0u || bands > 16u) return -1;
    EmGs *g = calloc(bands, sizeof *g);
    if (!g) return -1;
    for (uint32_t k = 0; k < bands; ++k) {
        em_gs_init(&g[k], mem);
        g[k].strict = 1;
        if (bands > 1u) {
            g[k].band_count = bands;
            g[k].band_index = k;
            g[k].band_shift = 2;
            g[k].tee = k + 1u < bands ? &g[k + 1u] : NULL;
        }
    }
    for (uint32_t i = 0; i < n; ++i) em_gs_write(&g[0], reg[i], val[i]);
    out[0] = em_gs_gif(&g[0], gif, (size_t)gif_bytes);
    em_gs_flush(&g[0]);
    out[1] = g[0].refusals;
    out[2] = g[0].refused_prims;
    out[3] = g[0].span_faults;
    out[4] = g[0].drawn_prims;
    out[5] = 0;
    for (uint32_t k = 0; k < bands; ++k) out[5] += g[k].drawn_pixels;
    if (reason && reason_cap) snprintf(reason, reason_cap, "%s", g[0].reason);
    for (uint32_t k = 0; k < bands; ++k) {
        g[k].tee = NULL;
        em_gs_release(&g[k]);
    }
    free(g);
    return 0;
}

/* ------------------------------------------------------------------ B */

typedef struct {
    uint8_t *ram, *spad;
    EmLoadVeilParticles lvp;
    uint32_t calls, table, address;
    int32_t id;
} Build;

static uint8_t *at(Build *b, uint32_t address, uint32_t size)
{
    if (address >= SPAD_BASE && address - SPAD_BASE <= SPAD_BYTES && size <= SPAD_BYTES - (address - SPAD_BASE))
        return b->spad + (address - SPAD_BASE);
    if (address < RAM_BYTES && size <= RAM_BYTES - address) return b->ram + address;
    return NULL;
}

static int w_0015D2F0(void *ctx, int32_t *result)
{
    Build *b = ctx;
    const uint8_t *player = at(b, 0x008102B0u, 0x320u);
    return player ? em_player_equipment_0015D2F0(player, result) : -1;
}

static int w_0022EBE0(void *ctx, int32_t *result)
{
    Build *b = ctx;
    const uint8_t *d = at(b, 0x008101E4u, 1), *s = at(b, 0x70003B8Du, 1);
    if (!d || !s) return -1;
    *result = em_sul_0022EBE0(*d, *s);
    return 0;
}

static int w_001026A0(void *ctx, uint32_t out[4], uint32_t matrix, const uint32_t v[4])
{
    Build *b = ctx;
    const uint8_t *m = at(b, matrix, 0x40u);
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
    float f, r;
    memcpy(&f, &x, 4);
    r = em_sdk_math_original_0011DF78(f);
    memcpy(result, &r, 4);
    return 0;
}

static int w_001281C0(void *ctx, uint32_t x, int32_t *result)
{
    (void)ctx;
    *result = em_player_float_to_int(x);
    return 0;
}

static int w_001D6930(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3, uint32_t t0, uint32_t *result)
{
    Build *b = ctx;
    const uint8_t *src = at(b, t0, 16);
    return src ? em_load_veil_particles_001D6930(&b->lvp, a0, a1, a2, a3, src, result) : -1;
}

static int w_001D1F20(void *ctx, int32_t chan)
{
    Build *b = ctx;
    return em_load_veil_particles_001D1F20(&b->lvp, chan);
}

static int w_001D6BA0(void *ctx, int32_t chan, int32_t a1, int32_t a2, int32_t a3, int32_t t0, int32_t t1)
{
    Build *b = ctx;
    uint32_t result;
    return em_load_veil_particles_001D6BA0(&b->lvp, chan, a1, a2, a3, t0, t1, &result);
}

static int w_001D1FF0(void *ctx, int32_t chan, int32_t a1)
{
    Build *b = ctx;
    return em_load_veil_particles_001D1FF0(&b->lvp, chan, a1);
}

/* 001CB760: recorded (the captured chain table holds the slot's block). */
static int w_001CB760(void *ctx, uint32_t table, int32_t id, uint32_t address)
{
    Build *b = ctx;
    b->calls++;
    b->table = table;
    b->id = id;
    b->address = address;
    return 0;
}

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* out: rc, fault address, fault code, 001CB760 calls, its table, its id,
 * its address. */
int dof_build(uint8_t *ram, uint8_t *spad, uint32_t out[7])
{
    if (!ram || !spad || !out) return -1;
    static Build b;
    memset(&b, 0, sizeof b);
    b.ram = ram;
    b.spad = spad;
    const uint32_t ctx = rd32(ram + 0x275670u);
    if (ctx + 0x2600u > RAM_BYTES) return -1;
    EmLoadVeilParticlesWorld *v = &b.lvp.world;
    v->cursor = (uint32_t *)(void *)(ram + ctx + 0x10u);
    v->cursor_count = 4;
    v->ctx_9C = (const uint32_t *)(const void *)(ram + ctx + 0x9Cu);
    v->d00275674 = (const uint32_t *)(const void *)(ram + 0x275674u);
    v->d0027568C = (const uint32_t *)(const void *)(ram + 0x27568Cu);
    v->d0026E880 = ram + 0x26E880u;
    v->d00241010 = ram + 0x241010u;
    v->packet = ram;
    v->packet_address = 0;
    v->packet_size = RAM_BYTES;
    b.lvp.workers.ctx = &b;
    b.lvp.workers.w_0011DF78 = w_0011DF78;
    b.lvp.workers.w_001281C0 = w_001281C0;

    EmRenderContext s;
    memset(&s, 0, sizeof s);
    EmRenderContextView views[2] = {{0u, RAM_BYTES, ram}, {SPAD_BASE, SPAD_BYTES, spad}};
    s.world.ctx = ctx;
    s.world.views = views;
    s.world.view_count = 2;
    EmRenderContextWorkers *w = &s.workers;
    w->ctx = &b;
    w->w_0015D2F0 = w_0015D2F0;
    w->w_0022EBE0 = w_0022EBE0;
    w->w_001026A0 = w_001026A0;
    w->w_0011DF78 = w_0011DF78;
    w->w_001281C0 = w_001281C0;
    w->w_001D6930 = w_001D6930;
    w->w_001D1F20 = w_001D1F20;
    w->w_001D6BA0 = w_001D6BA0;
    w->w_001D1FF0 = w_001D1FF0;
    w->w_001CB760 = w_001CB760;
    const int rc = em_render_context_001DDE10(&s);
    out[0] = (uint32_t)rc;
    out[1] = s.fault.address;
    out[2] = (uint32_t)s.fault.code;
    out[3] = b.calls;
    out[4] = b.table;
    out[5] = (uint32_t)b.id;
    out[6] = b.address;
    return rc;
}

/* The EE's float operations (em_ee_float.h), for the test's inversion of
 * the eased values: op 0 add, 1 sub, 2 mul, 3 div; raw binary32 words. */
uint32_t dof_ee(uint32_t op, uint32_t a, uint32_t b)
{
    switch (op) {
    case 0: return em_ee_add_bits(a, b);
    case 1: return em_ee_sub_bits(a, b);
    case 2: return em_ee_mul_bits(a, b);
    default: return em_ee_div_bits(a, b);
    }
}

/* ------------------------------------------------------------------ C */

#define SLOT_BLOCK 0x7F000000u   /* the walk's own slot block (no EE address) */

typedef struct {
    const uint8_t *ram;
    uint8_t block[32];
} Reader;

static const uint8_t *reader(void *ctx, uint32_t address, uint32_t size)
{
    const Reader *r = ctx;
    if (address >= SLOT_BLOCK && address - SLOT_BLOCK <= 32u && size <= 32u - (address - SLOT_BLOCK))
        return r->block + (address - SLOT_BLOCK);
    if (address < RAM_BYTES && size <= RAM_BYTES - address) return r->ram + address;
    return NULL;
}

#define LIVE_PRIMS 64u

/* `mem` / `resident`: the GS memory before the pass and which of its 256-byte
 * blocks count as uploads (em_gs_world_memory_restore: the others as drawn
 * by earlier frames). out: [0] walk rc, [1] walk fault, [2] its address,
 * [3] primitives, [4] passes, [5] pass primitives, [6] pass DIRECT packets,
 * [7] marks, [8..12] the marks, [13] the kick's environment bytes,
 * [14] workers, [15] the world's result (0 / -1). */
int dof_live(const uint8_t *ram, uint32_t target, uint32_t bank, uint32_t slot, const uint8_t *mem,
             const uint8_t *resident, const uint32_t *reg, const uint64_t *val, uint32_t n, uint32_t workers,
             uint8_t *mem_out, uint32_t out[16], char *why, uint32_t why_cap)
{
    if (!ram || !mem || !resident || (n && (!reg || !val)) || !mem_out || !out || workers == 0u || workers > 16u)
        return -1;
    memset(out, 0, 16u * sizeof *out);
    if (why && why_cap) why[0] = 0;
    static Reader rd;
    static EmChainPage page;
    static EmGfxGsPrim prims[LIVE_PRIMS];
    static EmGfxGsEnv envs[LIVE_PRIMS];
    rd.ram = ram;
    /* a CALL of the pass and a NEXT to the block's end: the page's slot
     * block shape (001CB760), walked alone */
    const uint64_t call = 0x50000000u | (uint64_t)target << 32, next = 0x20000000u | (uint64_t)(SLOT_BLOCK + 0x20u) << 32;
    memset(rd.block, 0, sizeof rd.block);
    memcpy(rd.block, &call, 8);
    memcpy(rd.block + 16, &next, 8);
    memset(&page, 0, offsetof(EmChainPage, regs));
    memset(prims, 0, sizeof prims);
    memset(envs, 0, sizeof envs);
    page.read = reader;
    page.read_ctx = &rd;
    page.prims = prims;
    page.prim_env = envs;
    page.prim_capacity = LIVE_PRIMS;
    page.pass_call = target;
    page.draw_envs = bank;
    page.draw_env_slot = slot;
    const int rc = em_chain_page_run(&page, SLOT_BLOCK);
    out[0] = (uint32_t)rc;
    out[1] = page.fault;
    out[2] = page.fault_address;
    out[3] = page.prim_count;
    out[4] = page.counts.passes;
    out[5] = page.counts.pass_prims;
    out[6] = page.counts.pass_direct;
    out[7] = page.again_count;
    for (uint32_t k = 0; k < page.again_count && k < 5u; ++k) out[8 + k] = page.again[k];
    if (rc < 0 || page.counts.passes != 1u || page.pass_first != 0u || page.prim_count != page.counts.pass_prims)
        return -1;
    /* the kick's draw environment: bank A's block of the slot (001D1F20's
     * REF, 0x19 qwords: its VIF codes and DIRECT data) */
    const uint8_t *block = reader(&rd, bank + EM_CHAIN_PAGE_DRAW_ENV * slot, EM_CHAIN_PAGE_DRAW_ENV_QWC * 16u);
    uint8_t env[EM_CHAIN_PAGE_DRAW_ENV_QWC * 16u];
    size_t env_bytes = 0;
    if (!block || em_gs_vif_direct(block, EM_CHAIN_PAGE_DRAW_ENV_QWC * 16u, 0, env, sizeof env, &env_bytes) < 0)
        return -1;
    out[13] = (uint32_t)env_bytes;
    uint64_t field_frame, field_scissor;
    if (block[0x28] != 0x4Cu || block[0x58] != 0x40u) return -1;   /* FRAME_1 / SCISSOR_1 where em_rcl_draw_env reads them */
    memcpy(&field_frame, block + 0x20, 8);
    memcpy(&field_scissor, block + 0x50, 8);
    char count[8];
    snprintf(count, sizeof count, "%u", (unsigned)workers);
    setenv("EM_GS_THREADS", count, 1);
    EmGsWorld *w = em_gs_world_create();
    if (!w) return -1;
    out[14] = em_gs_world_workers(w);
    int r = em_gs_world_memory_restore(w, mem, resident);
    if (r == 0) {
        em_gs_world_begin(w);
        for (uint32_t i = 0; i < n; ++i) em_gs_world_write(w, reg[i], val[i]);
        uint32_t again[EM_CHAIN_PAGE_AGAIN_MAX];
        for (uint32_t k = 0; k < page.again_count; ++k) again[k] = page.again[k] - page.pass_first;
        r = em_gs_world_page_pass(w, prims + page.pass_first, envs + page.pass_first, page.counts.pass_prims, again,
                                  page.again_count, field_frame, field_scissor);
        if (r == 0) r = em_gs_world_kick(w, env, env_bytes, NULL, 0);
        if (r == 0) r = em_gs_world_wait(w);
    }
    const uint8_t *after = r == 0 ? em_gs_world_memory(w) : NULL;
    if (after) memcpy(mem_out, after, EM_GS_MEM_BYTES);
    if (why && why_cap && em_gs_world_fault(w)) snprintf(why, why_cap, "%s", em_gs_world_fault(w));
    em_gs_world_destroy(w);
    out[15] = (uint32_t)(after ? 0 : -1);
    return after ? 0 : -1;
}
