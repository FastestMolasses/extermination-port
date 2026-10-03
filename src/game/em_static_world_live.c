/* em_static_world_live.c - the static world drawn live from its original
 * packets (em_static_world_live.h, docs/STATIC_WORLD.md section 7). */
#include "game/em_static_world_live.h"

#include <stdio.h>
#include <string.h>

#include "game/em_frame.h"
#include "game/em_owner_draw_live.h"
#include "game/em_render_context_live.h"
#include "game/em_static_world_draw.h"

#define RUN_MAX 0x10000u   /* the captured runs are 0xB00..0x3150 bytes */

static struct {
    EmStaticWorldDraw draw;
    uint32_t fault;
    EmStaticWorldLiveLog log;
    uint8_t run[RUN_MAX];
    uint32_t run_size;
} S;

static int fail(uint32_t address, const char *what)
{
    if (!S.fault) {
        S.fault = address ? address : 0x001D5370u;
        fprintf(stderr, "static world: %s (at %08X)\n", what, (unsigned)address);
    }
    return -1;
}

uint32_t em_static_world_live_fault(void) { return S.fault; }

/* Original memory the run reads: the render context's storage (the arena,
 * the context, the GS blocks and skin records) and the bank. */
static const uint8_t *reader(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    return em_rcl_bytes(address, size);
}

static uint32_t fnv(uint32_t h, uint32_t w)
{
    for (unsigned k = 0; k < 4; ++k) {
        h ^= (w >> (8 * k)) & 0xFFu;
        h *= 16777619u;
    }
    return h;
}

/* FNV-1a over the triangles (tools/test_level_smoke.py rebuilds it). */
static uint32_t prim_digest(const EmGfxGsPrim *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i) {
        h = fnv(h, p[i].prim);
        h = fnv(h, (uint32_t)p[i].tex0);
        h = fnv(h, (uint32_t)(p[i].tex0 >> 32));
        for (uint32_t k = 0; k < 3; ++k) {
            const EmGfxGsVertex *v = &p[i].v[k];
            h = fnv(h, v->x);
            h = fnv(h, v->y);
            h = fnv(h, v->z);
            h = fnv(h, v->f);
            h = fnv(h, (uint32_t)v->rgba[0] | (uint32_t)v->rgba[1] << 8 | (uint32_t)v->rgba[2] << 16 |
                       (uint32_t)v->rgba[3] << 24);
            h = fnv(h, v->s);
            h = fnv(h, v->t);
            h = fnv(h, v->q);
        }
    }
    return h;
}

/* Step V CALLs the channel-3 list before channel 0 in this frame: the
 * condition em_rcl_001D2300_calls_001E0DF0 reads by the translation's own
 * code, the movie frame's flag 4 (em_frame_movie_active, as background_gate
 * reads it) and 001E0DF0's +0x1D8 != 0. */
static int background_called(void)
{
    int calls = 0;
    if (em_rcl_001D2300_calls_001E0DF0(&calls) < 0) return -1;
    const uint8_t *word = em_rcl_bytes(EM_RCL_CONTEXT + 0x1D8, 4);
    return calls && !em_frame_movie_active() && word && (word[0] | word[1] | word[2] | word[3]);
}

int em_static_world_live_draw(EmGfx *gfx)
{
    if (S.fault) return -1;
    uint32_t start, end;
    if (em_rcl_static_run(&start, &end) < 0) return 0;   /* no 001C1D00 this frame */
    const int ch3 = background_called();
    if (ch3 < 0) return fail(0x001D2300u, "the step V gate faulted");
    if (end < start || end - start > RUN_MAX) return fail(start, "the run is larger than RUN_MAX");
    const uint8_t *bytes = em_rcl_bytes(start, end - start);
    if (!bytes && end > start) return fail(start, "the run is outside the render context's storage");
    S.run_size = end - start;
    if (S.run_size) memcpy(S.run, bytes, S.run_size);
    EmStaticWorldDraw *d = &S.draw;
    d->read = reader;
    d->read_ctx = NULL;
    d->kick = NULL;
    /* the cycle the list leaves before channel 0 (em_static_world_live.h):
     * 4,4 after the channel-3 CALL, else the previous frame's last STCYCL
     * (1,1; the walk's writes depend only on CL == WL) */
    d->cl = d->wl = ch3 ? 4u : EM_STATIC_WORLD_FRAME_CYCLE;
    if (em_static_world_draw_run(d, start, end) < 0) {
        char what[96];
        snprintf(what, sizeof what, "walk fault: %s (detail %08X)", em_static_world_draw_fault_name(d->fault),
                 (unsigned)d->fault_detail);
        return fail(d->fault_address ? d->fault_address : start, what);
    }
    /* the bank's textures are in the object texture registry */
    if (d->prim_count && em_owner_draw_live_textures(gfx) < 0)
        return fail(start, "the object textures are not registered (tools/export_object_textures.py)");
    if (d->prim_count && em_gfx_gs_opaque(gfx, d->prims, d->prim_count) < 0)
        return fail(start, "the renderer refused a triangle (em_gfx_gs_opaque)");
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i + 4 <= S.run_size; i += 4)
        h = fnv(h, (uint32_t)S.run[i] | (uint32_t)S.run[i + 1] << 8 | (uint32_t)S.run[i + 2] << 16 |
                   (uint32_t)S.run[i + 3] << 24);
    EmStaticWorldLiveLog *l = &S.log;
    l->frame = em_frame_counter();
    l->runs++;
    l->start = start;
    l->end = end;
    l->digest = h;
    l->batches[0] = d->counts.batches[0];
    l->batches[1] = d->counts.batches[1];
    l->triangles[0] = d->counts.triangles[0];
    l->triangles[1] = d->counts.triangles[1];
    l->culled = d->counts.culled;
    l->prim_digest = prim_digest(d->prims, d->prim_count);
    l->total_triangles += d->prim_count;
    return 0;
}

void em_static_world_live_log(EmStaticWorldLiveLog *out)
{
    if (out) *out = S.log;
}

const uint8_t *em_static_world_live_run(uint32_t *size)
{
    if (size) *size = S.run_size;
    return S.log.runs ? S.run : NULL;
}

/* ---- the channel-3 list's GS writes (background_gate) --------------------- */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* VIF words [p, p + size): skips UNPACK data, reads DIRECT packets' A+D
 * writes of TEX0_1 (0x06) and RGBAQ (0x01). */
static int list_vif(const uint8_t *p, uint32_t size, uint64_t *tex0, uint32_t *rgbaq, int *have)
{
    uint32_t i = 0;
    while (i + 4u <= size) {
        const uint32_t v = rd32(p + i), cmd = (v >> 24) & 0x7Fu, num = (v >> 16) & 0xFFu, imm = v & 0xFFFFu;
        if ((cmd & 0x60u) == 0x60u) {
            if ((cmd & 0x1Fu) != 0x0Cu) return -1;         /* V4-32 only */
            i += 4u + 16u * (num ? num : 256u);
            continue;
        }
        if (cmd == 0x00u || cmd == 0x01u || cmd == 0x10u || cmd == 0x11u || cmd == 0x13u || cmd == 0x14u) {
            i += 4u;
            continue;
        }
        if (cmd != 0x50u) return -1;
        const uint32_t data = (i + 4u + 15u) & ~15u, n = imm;
        if (!n || data + 16u * n > size) return -1;
        const uint8_t *q = p + data;
        const uint64_t lo = (uint64_t)rd32(q) | (uint64_t)rd32(q + 4) << 32;
        const uint32_t nloop = (uint32_t)(lo & 0x7FFFu), flg = (uint32_t)(lo >> 58) & 3u;
        const uint32_t nreg = (uint32_t)(lo >> 60) & 15u, regs = rd32(q + 8);
        if (flg != 0u || nreg != 1u || (regs & 15u) != 0xEu || 1u + nloop > n) return -1;
        for (uint32_t k = 0; k < nloop; ++k) {
            const uint8_t *w = q + 16u * (1u + k);
            const uint32_t reg = w[8];
            if (reg == 0x06u) { *tex0 = (uint64_t)rd32(w) | (uint64_t)rd32(w + 4) << 32; *have |= 1; }
            if (reg == 0x01u) { *rgbaq = rd32(w); *have |= 2; }
        }
        i = data + 16u * n;
    }
    return i == size ? 0 : -1;
}

int em_static_world_live_background_state(uint64_t *tex0, uint32_t *rgbaq)
{
    const uint8_t *head = em_rcl_bytes(EM_RCL_CONTEXT + 0x1D8, 4);
    if (!head || !tex0 || !rgbaq) return -1;
    uint32_t a = rd32(head);
    if (!a) return 0;
    int have = 0;
    for (unsigned tags = 0; tags < 64u; ++tags) {
        const uint8_t *tag = em_rcl_bytes(a, 16);
        if (!tag) break;
        const uint32_t w0 = rd32(tag), addr = rd32(tag + 4), id = (w0 >> 28) & 7u, qwc = w0 & 0xFFFFu;
        if (id == 6u)                                           /* RET */
            return have == 3 ? 1 : (fprintf(stderr, "background: the channel-3 list holds no TEX0 / RGBAQ\n"), -1);
        if (id == 1u || id == 3u) {                             /* CNT, REF */
            const uint32_t at = id == 1u ? a + 16u : addr;
            const uint8_t *data = qwc ? em_rcl_bytes(at, 16u * qwc) : tag;
            if (!data || (qwc && list_vif(data, 16u * qwc, tex0, rgbaq, &have) < 0)) break;
            a += id == 1u ? 16u * (1u + qwc) : 16u;
            continue;
        }
        if (id == 5u && qwc == 0u) {                            /* CALL of the kernel packet */
            a += 16u;
            continue;
        }
        break;
    }
    fprintf(stderr, "background: the channel-3 list at %08X has a form the reader does not take\n",
            (unsigned)rd32(head));
    return -1;
}
