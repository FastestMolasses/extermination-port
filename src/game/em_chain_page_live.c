/* em_chain_page_live.c - the chain page drawn live at the frame close
 * (em_chain_page_live.h, docs/CHAIN_PAGE.md section 7). */
#include "game/em_chain_page_live.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_effects_live.h"
#include "game/em_frame.h"
#include "game/em_render_context_live.h"
#include "game/em_shadow_decal_original.h"
#include "game/em_shadow_live.h"

#define CLD_MASK (~(UINT64_C(7) << 61))
#define READS_MAX 40000u

static struct {
    EmChainPage page;
    EmGfxGsPrim prims[EM_CHAIN_PAGE_LIVE_PRIMS];
    EmChainPageQ q[EM_CHAIN_PAGE_LIVE_PRIMS];
    uint32_t skip[1];
    EmGfx *textures_for;
    uint32_t fault;
    EmChainPageLiveLog log;
    struct { uint32_t address, size; } reads[READS_MAX];
    uint32_t nreads;
    uint32_t overlay_reads;
} S;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(uint32_t address, const char *what)
{
    if (!S.fault) {
        S.fault = address;
        fprintf(stderr, "chain page: %s (at %08X)\n", what, (unsigned)address);
    }
    return -1;
}

uint32_t em_chain_page_live_fault(void) { return S.fault; }

/* Original memory the page reads: the render context's storage (the arena,
 * the chain table, the context, the GS blocks, the .data it exports), then
 * the effect-table export's ELF blocks (the program packets, 001CFBE0's
 * source blocks) and the overlay source blocks 001D04B0 was handed (the
 * AREA11 flame's D_00828340). Nothing else is mapped. */
const uint8_t *em_chain_page_live_read(uint32_t address, uint32_t size)
{
    const uint8_t *p = em_rcl_bytes(address, size);
    return p ? p : em_effects_live_window(address, size);
}

static const uint8_t *reader(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    const uint8_t *p = em_chain_page_live_read(address, size);
    if (p && address >= 0x00800000u && !em_rcl_bytes(address, size)) S.overlay_reads++;
    if (p && S.nreads < READS_MAX) {
        S.reads[S.nreads].address = address;
        S.reads[S.nreads].size = size;
        S.nreads++;
    }
    return p;
}

int em_chain_page_live_textures(EmGfx *gfx)
{
    if (!gfx) return -1;
    if (S.textures_for == gfx) return 0;
    FILE *f = fopen(EM_CHAIN_PAGE_LIVE_TEXTURES, "rb");
    if (!f) {
        fprintf(stderr, "chain page: %s is missing (run tools/export_page_textures.py)\n",
                EM_CHAIN_PAGE_LIVE_TEXTURES);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = size > 0x10 ? malloc((size_t)size) : NULL;
    int ok = file && fread(file, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    ok = ok && memcmp(file, "EMOT", 4) == 0 && rd32(file + 4) == 1;
    const uint32_t count = ok ? rd32(file + 8) : 0;
    ok = ok && count > 0 && 0x10u + 24u * (uint64_t)count <= (uint64_t)size;
    for (uint32_t i = 0; ok && i < count; ++i) {
        const uint8_t *e = file + 0x10 + 24 * i;
        const uint64_t tex0 = (uint64_t)rd32(e) | (uint64_t)rd32(e + 4) << 32;
        const uint32_t w = rd32(e + 8), h = rd32(e + 12), at = rd32(e + 16);
        ok = w && h && w <= 1024u && h <= 1024u && at <= (uint64_t)size &&
             (uint64_t)w * h * 4u <= (uint64_t)size - at &&
             em_gfx_gs_texture(gfx, tex0, file + at, w, h) == 0;
    }
    free(file);
    if (!ok) {
        fprintf(stderr, "chain page: %s is not a page-texture export\n", EM_CHAIN_PAGE_LIVE_TEXTURES);
        return -1;
    }
    S.textures_for = gfx;
    return 0;
}

/* FNV-1a over the primitives as the consumer handed them (before the Q
 * substitution), the layout tools/test_level_smoke.py rebuilds. */
static uint32_t fnv(uint32_t h, uint32_t w)
{
    for (unsigned k = 0; k < 4; ++k) {
        h ^= (w >> (8 * k)) & 0xFFu;
        h *= 16777619u;
    }
    return h;
}

static uint32_t digest(const EmGfxGsPrim *p, const EmChainPageQ *q, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i) {
        const uint64_t st[6] = { p[i].tex0, p[i].clamp, p[i].tex1, p[i].alpha, p[i].test, p[i].colclamp };
        h = fnv(h, p[i].prim);
        h = fnv(h, p[i].set);
        for (unsigned k = 0; k < 6; ++k) {
            h = fnv(h, (uint32_t)st[k]);
            h = fnv(h, (uint32_t)(st[k] >> 32));
        }
        h = fnv(h, p[i].count);
        for (uint32_t k = 0; k < p[i].count; ++k) {
            const EmGfxGsVertex *v = &p[i].v[k];
            h = fnv(h, v->x);
            h = fnv(h, v->y);
            h = fnv(h, v->z);
            h = fnv(h, v->f);
            h = fnv(h, v->has_f);
            h = fnv(h, (uint32_t)v->rgba[0] | (uint32_t)v->rgba[1] << 8 | (uint32_t)v->rgba[2] << 16 |
                       (uint32_t)v->rgba[3] << 24);
            h = fnv(h, v->q);
            h = fnv(h, v->s);
            h = fnv(h, v->t);
            h = fnv(h, v->u);
            h = fnv(h, v->v);
            h = fnv(h, q[i].q_known[k]);
        }
    }
    return h;
}

int em_chain_page_live_draw(EmGfx *gfx)
{
    if (S.fault) return -1;
    uint32_t start, four;
    if (em_rcl_page(&start, &four) < 0) return 0;              /* no kick since the last draw */
    if (!gfx) return fail(0x001CB800u, "no frame to draw the page in");
    if (em_chain_page_live_textures(gfx) < 0) return fail(0x001CB800u, "no page textures");
    EmChainPage *p = &S.page;
    memset(p, 0, offsetof(EmChainPage, regs));
    p->read = reader;
    p->prims = S.prims;
    p->prim_q = S.q;
    p->prim_capacity = EM_CHAIN_PAGE_LIVE_PRIMS;
    S.skip[0] = four;
    p->skip_calls = S.skip;
    p->skip_count = four ? 1u : 0u;
    S.nreads = 0;
    S.overlay_reads = 0;
    if (em_chain_page_run(p, start) < 0) {
        fprintf(stderr, "chain page: %s fault at %08X (%08X)\n", em_chain_page_fault_name(p->fault),
                (unsigned)p->fault_address, (unsigned)p->fault_detail);
        return fail(0x001CB800u, "the page cannot be walked as the original's");
    }
    if (four && p->counts.skipped != 1u) return fail(0x001DDE10u, "001DDE10's slot-0xFFF CALL is not in the page");
    uint32_t decal = 0;
    for (uint32_t i = 0; i < p->prim_count; ++i)
        if ((p->prims[i].prim & 7u) == 5u && ((p->prims[i].tex0 ^ EM_SHADOW_DECAL_TEX0) & CLD_MASK) == 0)
            decal++;
    S.log.frame = em_frame_counter();
    S.log.pages++;
    S.log.start = start;
    S.log.four_sprite = four;
    S.log.counts = p->counts;
    S.log.decal_triangles = decal;
    S.log.weather = em_rcl_page_weather();
    S.log.overlay_reads = S.overlay_reads;
    S.log.digest = digest(p->prims, S.q, p->prim_count);
    S.log.total_prims += p->prim_count;
    S.log.total_stale_q += p->counts.stale_q;
    S.log.total_skipped += p->counts.skipped;
    if (em_shadow_live_bound() && em_shadow_live_page_drew(decal) < 0)
        return fail(0x001CE300u, "the decal the page drew is not the 0015BF90 route's");
    /* The frame's Q (em_chain_page_live.h): drawn as 1.0. */
    for (uint32_t i = 0; i < p->prim_count; ++i)
        for (uint32_t k = 0; k < p->prims[i].count; ++k)
            if (!S.q[i].q_known[k]) p->prims[i].v[k].q = 0x3F800000u;
    if (em_gfx_gs_prims(gfx, p->prims, p->prim_count) < 0)
        return fail(0x001CB800u, "a page primitive cannot be drawn exactly");
    return 0;
}

void em_chain_page_live_log(EmChainPageLiveLog *out)
{
    if (out) *out = S.log;
}

const EmGfxGsPrim *em_chain_page_live_prims(uint32_t *count)
{
    if (count) *count = S.page.prim_count;
    return S.log.pages ? S.prims : NULL;
}

uint32_t em_chain_page_live_reads(const uint32_t **pairs)
{
    if (pairs) *pairs = &S.reads[0].address;
    return S.nreads;
}

const uint8_t *em_chain_page_live_q(void)
{
    return &S.q[0].q_known[0];
}
