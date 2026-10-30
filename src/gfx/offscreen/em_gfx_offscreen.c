/* CPU GS backend for display-free platform proofs. The packet adapters
 * below retain the project's Metal backend's CPU algorithms. No GPU calls.
 * Overlays and legacy meshes have no output in this null presenter; presented
 * image tests must use a real presenter. GS fields and failures remain real. */
#include "em_gfx.h"
#include "em_platform.h"
#include "game/em_object_unit.h"
#include "gfx/metal/em_shadow_gs.h"
#include "gfx/metal/em_background_gs.h"
#include "gfx/metal/em_fog_gs.h"
#include "gs/em_gs_world.h"
#include "gs/em_gs_display.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct EmGfx {
    EmGsWorld *gsw;
    EmGfxGsRead gswRead;
    void *gswReadCtx;
    int gswOn, gswFrame, gswShow, gswPresentation, shadowRecvOpen;
    /* The GS world draws its frames (em_gfx_gs_world_raster): 0 is the
     * headless raster skip (docs/PERFORMANCE.md P18), where no picture may
     * be read back or captured. */
    int gswRaster;
    int inFrame;   /* between em_gfx_begin_frame and em_gfx_end_frame */
    char gswWhy[256];
    EmGfxGsPrim *gswPrims;
    uint32_t gswPrimCap;
    EmObjectUnitResult objResult;
    float fog[8], shadowUV[16], shadowCam[16];
    double presentDisplayNs, presentFieldNs;
    uint64_t presentFrames;
    uint8_t *bgFile;
    EmBackgroundGsAsset bgAsset;
    EmGsDisplayRegs display;
};
struct EmGfxMesh { uint32_t vertices; };
static int gsw_check(EmGfx *g);
static void gsw_complete(EmGfx *g) {
    if (g && g->gsw && em_gs_world_wait(g->gsw) < 0) (void)gsw_check(g);
}
static EmVu1ClipResult *shadow_clip_result(void) { return malloc(sizeof(EmVu1ClipResult)); }
#define GSW_CLASS0_SET   0x00815360u   /* 001D1F80(0, 1, 0): set 1, class 0 */
#define GSW_SHADOW_2_9   0x00815E10u   /* 001D1F80(a0, 2, 9): 001DA290 / 001DA310 */
#define GSW_SILHOUETTE   0x00814DC0u   /* 001D1F80(., 0, 0): 001D9EE0's set */
#define GSW_SHADOW_2_6   0x00815C60u   /* 001D1F80(., 2, 6): 001D4CD0 */
#define GSW_CLAMP_BLOCK  0x008146C0u   /* 001D1FF0(., 0): CLAMP 5; (., 1): CLAMP 0 */
#define GSW_ALPHA_ROWS   0x002531D0u   /* D_002531D0: 001DA290's four strip rows */

static int gsw_fail(EmGfx *g, const char *what)
{
    if (!g->gswWhy[0]) {
        snprintf(g->gswWhy, sizeof g->gswWhy, "%s", what);
        fprintf(stderr, "gfx: GS frame: %s\n", what);
    }
    return -1;
}

/* The model's latched fault, if any, as the backend's. */
static int gsw_check(EmGfx *g)
{
    const char *why = em_gs_world_fault(g->gsw);
    return why ? gsw_fail(g, why) : 0;
}

/* A state block the list REFs: `qwords` at `address` (its FLUSH / DIRECT
 * codes and the GIF data), recorded at this point of the frame. */
static int gsw_block(EmGfx *g, uint32_t address, uint32_t qwords)
{
    const uint8_t *p = g->gswRead ? g->gswRead(g->gswReadCtx, address, qwords * 16u) : NULL;
    if (!p) return gsw_fail(g, "a state block the game's memory does not map");
    uint8_t gif[0x200];
    size_t used = 0;
    if (qwords * 16u > sizeof gif || em_gs_vif_direct(p, qwords * 16u, 0, gif, sizeof gif, &used) < 0 || !used)
        return gsw_fail(g, "a state block that is not VIF DIRECT GIF data");
    em_gs_world_gif(g->gsw, gif, used);
    return gsw_check(g);
}

static EmGfxGsPrim *gsw_prims(EmGfx *g, uint32_t count)
{
    if (count > g->gswPrimCap) {
        EmGfxGsPrim *n = realloc(g->gswPrims, sizeof *n * count);
        if (!n) return NULL;
        g->gswPrims = n;
        g->gswPrimCap = count;
    }
    return g->gswPrims;
}

static uint32_t gsw_fbits(float f)
{
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
}

/* One kicked XYZF2 vertex of the shadow kernels (PACKED words: X, Y, Z << 4,
 * F << 4) with the vertex's RGBAQ / ST. */
static void gsw_vertex_xyzf(EmGfxGsVertex *o, const int32_t w[4], const uint8_t rgba[4], uint32_t q,
                            uint32_t s, uint32_t t)
{
    memset(o, 0, sizeof *o);
    o->x = (uint16_t)((uint32_t)w[0] & 0xFFFFu);
    o->y = (uint16_t)((uint32_t)w[1] & 0xFFFFu);
    o->z = ((uint32_t)w[2] >> 4) & 0xFFFFFFu;
    o->f = (uint8_t)(((uint32_t)w[3] >> 4) & 0xFFu);
    o->has_f = 1;
    memcpy(o->rgba, rgba, 4);
    o->q = q;
    o->s = s;
    o->t = t;
}

int em_gfx_gs_world_enable(EmGfx *g, int on, EmGfxGsRead read, void *read_ctx)
{
    if (!g) return -1;
    if (!on) {
        g->gswOn = false;
        return 0;
    }
    if (!read) return -1;
    if (!g->gsw && !(g->gsw = em_gs_world_create_raster(g->gswRaster ? 1 : 0))) return -1;
    if (!g->gswRaster) {
        static int noted;
        if (!noted++)
            fprintf(stderr, "gfx: headless raster skip: the GS frames are recorded and parsed, not drawn "
                            "(EM_HEADLESS_RASTER=1 draws them)\n");
    }
    g->gswRead = read;
    g->gswReadCtx = read_ctx;
    g->gswOn = true;
    return 0;
}

int em_gfx_gs_world_enabled(EmGfx *g) { return g && g->gswOn; }

int em_gfx_gs_world_raster(EmGfx *g, int raster)
{
    if (!g) return -1;
    if (g->gsw) return (raster != 0) == (g->gswRaster != 0) ? 0 : -1;
    g->gswRaster = raster != 0;
    return 0;
}

int em_gfx_gs_memory_load(EmGfx *g, const char *path)
{
    if (!g || !g->gsw) return -1;
    if (em_gs_world_memory_load(g->gsw, path) < 0) return gsw_check(g);
    return 0;
}

int em_gfx_gs_upload(EmGfx *g, const uint8_t *chain, size_t bytes)
{
    if (!g || !g->gswOn) return 0;
    gsw_complete(g);   /* the frame before is shown before memory changes */
    if (em_gs_world_upload_chain(g->gsw, chain, bytes) < 0) return gsw_check(g);
    return 0;
}

int em_gfx_gs_world_frame(EmGfx *g)
{
    if (!g || !g->gswOn) return 0;
    if (g->gswWhy[0]) return -1;
    if (!em_gs_world_memory_loaded(g->gsw)) return gsw_fail(g, "a world frame before the GS memory image");
    em_gs_world_begin(g->gsw);
    g->gswFrame = true;
    return 1;
}

void em_gfx_gs_world_drop(EmGfx *g)
{
    if (g) g->gswFrame = false;   /* em_gs_world_begin drops the body */
}

int em_gfx_gs_world_recording(EmGfx *g) { return g && g->gswFrame; }

/* em_gfx_gs_page_pass: 001DDE10's depth-of-field pass into the recorded
 * world frame (em_gs_world_page_pass: the field declared, then the
 * primitives with their environment, the kick's environment again at each
 * mark). */
int em_gfx_gs_page_pass(EmGfx *g, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count,
                        const uint32_t *again, uint32_t again_count, uint64_t field_frame, uint64_t field_scissor)
{
    if (!g) return -1;
    if (!g->gswFrame) {
        fprintf(stderr, "gfx: the depth-of-field pass outside a recorded GS world frame (only the GS model "
                        "draws it)\n");
        return -1;
    }
    (void)em_gs_world_page_pass(g->gsw, prims, envs, count, again, again_count, field_frame, field_scissor);
    return gsw_check(g);
}

int em_gfx_gs_world_kick(EmGfx *g, const void *env, size_t env_bytes, const void *clear, size_t clear_bytes)
{
    if (!g || !g->gswFrame) return 0;
    g->gswFrame = false;
    /* while the frame before is drawn, this thread parses this one (it
     * waits for that frame's field next) */
    (void)em_gs_world_preparse(g->gsw, env, env_bytes, clear, clear_bytes);
    /* the frame before ends first: its field drawn, uploaded, committed */
    gsw_complete(g);
    if (g->gswWhy[0]) return -1;
    if (em_gs_world_kick(g->gsw, env, env_bytes, clear, clear_bytes) < 0) return gsw_check(g);
    g->gswShow = true;
    return 0;
}

const char *em_gfx_gs_world_fault(EmGfx *g) { return g && g->gswWhy[0] ? g->gswWhy : NULL; }

int em_gfx_gs_field_read(EmGfx *g, uint8_t *rgba, uint64_t *frame)
{
    uint32_t w, h;
    if (!g || !rgba || !g->gsw) return -1;
    if (!g->gswRaster) return gsw_fail(g, "a picture read under the headless raster skip");
    gsw_complete(g);
    const uint8_t *f = em_gs_world_field(g->gsw, &w, &h, frame);
    if (!f || w != EM_GS_WORLD_FIELD_W || h != EM_GS_WORLD_FIELD_H) return -1;
    memcpy(rgba, f, (size_t)w * h * 4u);
    return 0;
}

int em_gfx_present_cost(EmGfx *g, EmGfxPresentCost *out)
{
    if (!g || !out) return -1;
    out->display_wait_ns = g->presentDisplayNs;
    out->field_wait_ns = g->presentFieldNs;
    out->frames = g->presentFrames;
    return 0;
}

int em_gfx_gs_world_cost(EmGfx *g, EmGfxGsCost *out)
{
    if (!g || !g->gsw || !out) return -1;
    EmGsWorldStats st;
    em_gs_world_stats(g->gsw, &st);
    out->wall_ns = st.ns;
    out->cpu_max_ns = st.cpu_max_ns;
    out->cpu_sum_ns = st.cpu_sum_ns;
    out->pixels = st.pixels;
    out->runs = st.runs;
    out->workers = em_gs_world_workers(g->gsw);
    return 0;
}

void em_gfx_field_presentation(EmGfx *g, EmGfxFieldPresentation mode)
{
    if (g) g->gswPresentation = (int)mode;
}

/* em_gfx_background_prims_env: the channel-3 list's triangles with its
 * environment (ZBUF_1 ZMSK 1) and its own state writes. */
static int gsw_background(EmGfx *g, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count)
{
    em_gs_world_prims(g->gsw, prims, envs, count);
    return gsw_check(g);
}

/* em_gfx_gs_opaque: the static world's run, in the class-0 set its REF names. */
static int gsw_opaque(EmGfx *g, const EmGfxGsPrim *prims, uint32_t count)
{
    if (gsw_block(g, GSW_CLASS0_SET, 9) < 0) return -1;
    em_gs_world_prims(g->gsw, prims, NULL, count);
    return gsw_check(g);
}

/* em_gfx_object_unit: the unit's programs (the same CPU translations), then
 * the triangles they kick in the class-0 set its REF names: pass 0 with the
 * template's PRIM, the clip pass with its own (prim - 1). */
static int gsw_object(EmGfx *g, const EmGfxObjectUnit *unit)
{
    if (unit->gs_class != 0u) return gsw_fail(g, "a class-2 object unit outside the chain page");
    if (em_object_unit_run(unit, &g->objResult)) return gsw_fail(g, "the VU1 programs refused an object unit");
    const EmObjectUnitResult *r = &g->objResult;
    EmGfxGsPrim *p = gsw_prims(g, r->count);
    if (r->count && !p) return gsw_fail(g, "out of memory");
    for (uint32_t i = 0; i < r->count; i++) {
        const EmObjectUnitTriangle *t = &r->tri[i];
        memset(&p[i], 0, sizeof p[i]);
        p[i].prim = t->pass ? r->prim - 1u : r->prim;
        p[i].set = EM_GFX_GS_TEX0;
        p[i].tex0 = t->tex0;
        p[i].count = 3;
        for (unsigned c = 0; c < 3u; c++) {
            const EmObjectUnitVertex *s = &t->v[c];
            EmGfxGsVertex *d = &p[i].v[c];
            d->x = s->x; d->y = s->y; d->z = s->z; d->f = s->f; d->has_f = 1;
            memcpy(d->rgba, s->rgba, 4);
            d->q = s->q; d->s = s->s; d->t = s->t;
        }
    }
    if (gsw_block(g, GSW_CLASS0_SET, 9) < 0) return -1;
    em_gs_world_prims(g->gsw, p, NULL, r->count);
    return gsw_check(g);
}

/* em_gfx_gs_prims: the chain page's primitives, with the state they carry. */
static int gsw_page(EmGfx *g, const EmGfxGsPrim *prims, uint32_t count)
{
    em_gs_world_prims(g->gsw, prims, NULL, count);
    return gsw_check(g);
}

/* The frame's FOGCOL (context +0xB0, which 001D1C50 copies to the GS block):
 * integers 0..255. */
static int gsw_fogcol(EmGfx *g, const float rgb[3])
{
    uint64_t col = 0;
    for (unsigned k = 0; k < 3u; k++) {
        if (!(rgb[k] >= 0.0f && rgb[k] <= 255.0f) || rgb[k] != floorf(rgb[k]))
            return gsw_fail(g, "a FOGCOL that is not 8-bit integers");
        col |= (uint64_t)(uint32_t)rgb[k] << (8u * k);
    }
    em_gs_world_write(g->gsw, EM_GS_FOGCOL, col);
    return gsw_check(g);
}

/* 001DA290: state (2,9), then 001DA1E0's DIRECT strip: the GIF tag (PRE,
 * PRIM 0x044, NLOOP 1, REGS RGBAQ + XYZF2 x 4), RGBAQ (0, 0, 0, a1 = 0) and
 * the four rows of D_002531D0. */
static int gsw_alpha_clear(EmGfx *g)
{
    if (gsw_block(g, GSW_SHADOW_2_9, 9) < 0) return -1;
    const uint8_t *rows = g->gswRead(g->gswReadCtx, GSW_ALPHA_ROWS, 64);
    if (!rows) return gsw_fail(g, "D_002531D0 is not mapped (re-export effect_tables.emet)");
    uint8_t gif[96];
    const uint64_t tag_lo = UINT64_C(0x8001) | (UINT64_C(0x50224000) << 32), tag_hi = 0x44441u;
    memcpy(gif, &tag_lo, 8);
    memcpy(gif + 8, &tag_hi, 8);
    memset(gif + 16, 0, 16);                   /* RGBAQ: R, G, B, A = 0 */
    memcpy(gif + 32, rows, 64);
    em_gs_world_gif(g->gsw, gif, sizeof gif);
    return gsw_check(g);
}

/* 001DA310: one box through 00237180 (its kicked triangles) and 00239C90 (its
 * clipped triangles), flat with the call's RGBAQ, in state (2,9). */
static int gsw_box(EmGfx *g, const EmGfxShadowStrips *model, const float clip[16], uint32_t rgbaq)
{
    float k1021[4] = { 255.0f, 2048.0f, 0.0f, 0.0f }, k1022[4], k1023[4];
    em_shadow_gs_level_rows(k1022, k1023);
    const uint32_t n = model->vertex_count, clip_cap = 30u * 27u;
    const uint8_t rgba[4] = { (uint8_t)rgbaq, (uint8_t)(rgbaq >> 8), (uint8_t)(rgbaq >> 16), (uint8_t)(rgbaq >> 24) };
    EmShadowGsVertex *out = malloc(sizeof *out * n);
    EmVu1Qword *dmem = malloc(sizeof *dmem * EM_VU1_DMEM_QWORDS);
    EmVu1ClipResult *res = shadow_clip_result();
    EmShadowGsClipVertex *gv = malloc(sizeof *gv * clip_cap);
    float (*cp)[4] = malloc(sizeof *cp * clip_cap);
    EmGfxGsPrim *p = gsw_prims(g, n + clip_cap * (n / EM_GFX_SHADOW_BATCH));
    int bad = !out || !dmem || !res || !gv || !cp || !p ? 3 : 0;
    const float (*qw3)[4] = (const float (*)[4])model->qw3;
    uint32_t count = 0;
    for (uint32_t b = 0; b < n && !bad; b += EM_GFX_SHADOW_BATCH) {
        if (em_shadow_gs_level_batch(clip, k1021, k1022, k1023, qw3 + b, EM_GFX_SHADOW_BATCH, out + b)) bad = 1;
        for (uint32_t i = b + 2; i < b + EM_GFX_SHADOW_BATCH && !bad; ++i) {
            if (out[i].why) continue;
            EmGfxGsPrim *t = &p[count++];
            memset(t, 0, sizeof *t);
            t->prim = 0x044u;
            t->count = 3;
            for (unsigned k = 0; k < 3; ++k)
                gsw_vertex_xyzf(&t->v[k], out[i - 2 + k].w, rgba, 0x3F800000u, 0, 0);
        }
    }
    for (uint32_t b = 0; b < n && !bad; b += EM_GFX_SHADOW_BATCH) {
        if (em_shadow_gs_clip_dmem(EM_VU1_CLIP_BOX, clip, NULL, k1021, qw3 + b, EM_SHADOW_GS_CLIP_TOP, dmem) ||
            em_vu1_shadow_clip_run(EM_VU1_CLIP_BOX, dmem, EM_SHADOW_GS_CLIP_TOP, res)) {
            bad = 2;
            break;
        }
        const int v = em_shadow_gs_clip_vertices(EM_VU1_CLIP_BOX, res, gv, clip_cap);
        if (v < 0 || v % 3) { bad = 2; break; }
        for (int k = 0; k < v; k += 3) {
            EmGfxGsPrim *t = &p[count++];
            memset(t, 0, sizeof *t);
            t->prim = 0x043u;
            t->count = 3;
            for (unsigned c = 0; c < 3; ++c) {
                const EmShadowGsClipVertex *s = &gv[k + c];
                const int32_t w[4] = { (int32_t)(s->x * 16.0f), (int32_t)(s->y * 16.0f), (int32_t)(s->z << 4),
                                       (int32_t)(s->f << 4) };
                gsw_vertex_xyzf(&t->v[c], w, rgba, 0x3F800000u, 0, 0);
            }
        }
    }
    free(out); free(dmem); free(res); free(gv); free(cp);
    if (bad) return gsw_fail(g, bad == 3 ? "out of memory" : bad == 1 ? "box strip starts without ADC"
                                                                      : "box clip kernel 00239C90 fault");
    em_gs_world_prims(g->gsw, p, NULL, count);
    return gsw_check(g);
}

/* 001D9EE0: the target packet D_00817E20 (its draw environment and the
 * clear sprite: the values em_shadow_gs.h pins), the set D_00814DC0, the
 * A+D RGBAQ 0xFFFFFF80 and the proxy mesh's triangles through the object
 * kernel's position path (flat, PRIM 0x004; a triangle with an ADC vertex is
 * not kicked). The receivers sample the target from GS memory. */
static int gsw_silhouette(EmGfx *g, const float *verts, uint32_t vert_count, const uint32_t *indices,
                          uint32_t index_count, const float *nodes, uint32_t node_count, const float vp[16])
{
    em_gs_world_write(g->gsw, EM_GS_FRAME_1, EM_SHADOW_GS_FRAME_TARGET);
    /* the target is drawn from here (its readers so far finish first) */
    em_gs_world_drawn_buffer(g->gsw, EM_SHADOW_GS_FRAME_TARGET, EM_SHADOW_GS_TARGET_SIZE);
    em_gs_world_write(g->gsw, EM_GS_ZBUF_1, EM_SHADOW_GS_ZBUF_TARGET);
    em_gs_world_write(g->gsw, EM_GS_XYOFFSET_1, EM_SHADOW_GS_XYOFFSET_TARGET);
    em_gs_world_write(g->gsw, EM_GS_SCISSOR_1, EM_SHADOW_GS_SCISSOR_TARGET);
    em_gs_world_write(g->gsw, EM_GS_PRMODECONT, EM_SHADOW_GS_PRMODECONT_TARGET);
    em_gs_world_write(g->gsw, EM_GS_COLCLAMP, EM_SHADOW_GS_COLCLAMP_TARGET);
    em_gs_world_write(g->gsw, EM_GS_DTHE, EM_SHADOW_GS_DTHE_TARGET);
    em_gs_world_write(g->gsw, EM_GS_TEST_1, EM_SHADOW_GS_TARGET_CLEAR_TEST);
    em_gs_world_write(g->gsw, EM_GS_PRIM, EM_SHADOW_GS_TARGET_CLEAR_PRIM);
    em_gs_world_write(g->gsw, EM_GS_RGBAQ, EM_SHADOW_GS_TARGET_CLEAR_RGBAQ);
    em_gs_world_write(g->gsw, EM_GS_XYZ2, EM_SHADOW_GS_TARGET_CLEAR_XYZ0);
    em_gs_world_write(g->gsw, EM_GS_XYZ2, EM_SHADOW_GS_TARGET_CLEAR_XYZ1);
    em_gs_world_write(g->gsw, EM_GS_TEST_1, EM_SHADOW_GS_TARGET_CLEAR_TEST);
    if (gsw_check(g) < 0 || gsw_block(g, GSW_SILHOUETTE, 9) < 0) return -1;
    float k1021[4] = { 255.0f, 2048.0f, 0.0f, 0.0f }, k1022[4], k1023[4];
    em_shadow_gs_object_rows(k1022, k1023);
    float *bones = malloc(sizeof(float) * 16 * node_count);
    EmShadowGsVertex *sv = malloc(sizeof *sv * vert_count);
    EmGfxGsPrim *p = gsw_prims(g, index_count / 3u);
    if (!bones || !sv || !p) {
        free(bones); free(sv);
        return gsw_fail(g, "out of memory");
    }
    for (uint32_t b = 0; b < node_count; ++b) em_shadow_gs_bone(nodes + 16 * b, vp, bones + 16 * b);
    int bad = 0;
    for (uint32_t i = 0; i < vert_count && !bad; ++i) {
        const float *rec = verts + (size_t)i * 10;
        uint32_t bone;
        memcpy(&bone, rec + 8, 4);
        bone &= EM_GFX_VERT_BONE_MASK;
        if (bone >= node_count) { bad = 1; break; }
        float q3[1][4] = { { rec[0], rec[1], rec[2], 0.0f } };
        const float *bp = bones + 16 * bone;
        em_shadow_gs_object_batch(&bp, k1021, k1022, k1023, (const float (*)[4])q3, 1, &sv[i]);
    }
    const uint8_t rgba[4] = { 0x80, 0xFF, 0xFF, 0xFF };          /* A+D RGBAQ 0xFFFFFF80, Q 0 */
    uint32_t count = 0;
    for (uint32_t t = 0; t + 2 < index_count && !bad; t += 3) {
        const uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
        if (a >= vert_count || b >= vert_count || c >= vert_count) { bad = 1; break; }
        if ((sv[a].why | sv[b].why | sv[c].why) & EM_SHADOW_GS_ADC_CLIP) continue;
        EmGfxGsPrim *o = &p[count++];
        memset(o, 0, sizeof *o);
        o->prim = 0x004u;
        o->count = 3;
        const uint32_t idx[3] = { a, b, c };
        for (unsigned k = 0; k < 3; ++k) {
            EmGfxGsVertex *d = &o->v[k];
            memset(d, 0, sizeof *d);
            d->x = (uint16_t)((uint32_t)sv[idx[k]].w[0] & 0xFFFFu);
            d->y = (uint16_t)((uint32_t)sv[idx[k]].w[1] & 0xFFFFu);
            d->z = (uint32_t)sv[idx[k]].w[2];                     /* XYZ2: the whole Z word */
            memcpy(d->rgba, rgba, 4);
        }
    }
    free(bones); free(sv);
    if (bad) return gsw_fail(g, "silhouette vertex/node out of range");
    em_gs_world_prims(g->gsw, p, NULL, count);
    return gsw_check(g);
}

/* 001D4CD0: the frame's draw environment again, set (2,6), CLAMP block 0
 * (CLAMP 5) and the target's TEX0. */
static int gsw_receiver_begin(EmGfx *g, const float uv[16], const float camera[16])
{
    memcpy(g->shadowUV, uv, 64);
    memcpy(g->shadowCam, camera, 64);
    g->shadowRecvOpen = true;
    em_gs_world_env_again(g->gsw);
    if (gsw_check(g) < 0 || gsw_block(g, GSW_SHADOW_2_6, 9) < 0 || gsw_block(g, GSW_CLAMP_BLOCK, 4) < 0)
        return -1;
    em_gs_world_write(g->gsw, EM_GS_TEX0_1, EM_SHADOW_GS_TEX0_RECEIVER);
    return gsw_check(g);
}

/* 001D4FB0: one receiver through 0023C200 (PRIM 0x07C; ST, RGBAQ (0, 0, 0, a)
 * with the ST's Q, XYZF2) and, for class 2, 0023E8A0's clipped triangles
 * (PRIM 0x07B) after the object's own. */
static int gsw_receiver(EmGfx *g, const EmGfxShadowStrips *object, uint32_t cls)
{
    float k1021[4] = { 255.0f, 2048.0f, g->fog[4], g->fog[5] }, k1022[4], k1023[4];
    em_shadow_gs_level_rows(k1022, k1023);
    const uint32_t n = object->vertex_count, clip_cap = 30u * 27u;
    EmShadowGsReceiverVertex *out = malloc(sizeof *out * n);
    EmGfxGsPrim *p = gsw_prims(g, n + (cls == 2u ? clip_cap * (n / EM_GFX_SHADOW_BATCH) : 0u));
    if (!out || !p) {
        free(out);
        return gsw_fail(g, "out of memory");
    }
    const float (*qw3)[4] = (const float (*)[4])object->qw3;
    uint32_t count = 0;
    for (uint32_t b = 0; b < n; b += EM_GFX_SHADOW_BATCH) {
        em_shadow_gs_receiver_batch(g->shadowCam, g->shadowUV, k1021, k1022, k1023, qw3 + b,
                                    EM_GFX_SHADOW_BATCH, out + b);
        for (uint32_t i = b + 2; i < b + EM_GFX_SHADOW_BATCH; ++i) {
            if (out[i].xyzf.why) continue;
            EmGfxGsPrim *t = &p[count++];
            memset(t, 0, sizeof *t);
            t->prim = 0x07Cu;
            t->count = 3;
            for (unsigned k = 0; k < 3; ++k) {
                const EmShadowGsReceiverVertex *v = &out[i - 2 + k];
                const uint8_t rgba[4] = { 0, 0, 0, (uint8_t)v->a };
                gsw_vertex_xyzf(&t->v[k], v->xyzf.w, rgba, gsw_fbits(v->q), gsw_fbits(v->s), gsw_fbits(v->t));
            }
        }
    }
    free(out);
    int clip = 0;
    if (cls == 2u) {
        EmVu1Qword *dmem = malloc(sizeof *dmem * EM_VU1_DMEM_QWORDS);
        EmVu1ClipResult *res = shadow_clip_result();
        EmShadowGsClipVertex *gv = malloc(sizeof *gv * clip_cap);
        if (!dmem || !res || !gv) clip = 2;
        for (uint32_t b = 0; b < n && !clip; b += EM_GFX_SHADOW_BATCH) {
            if (em_shadow_gs_clip_dmem(EM_VU1_CLIP_RECEIVER, g->shadowCam, g->shadowUV, k1021, qw3 + b,
                                       EM_SHADOW_GS_CLIP_TOP, dmem) ||
                em_vu1_shadow_clip_run(EM_VU1_CLIP_RECEIVER, dmem, EM_SHADOW_GS_CLIP_TOP, res)) {
                clip = 1;
                break;
            }
            const int v = em_shadow_gs_clip_vertices(EM_VU1_CLIP_RECEIVER, res, gv, clip_cap);
            if (v < 0 || v % 3) { clip = 1; break; }
            for (int k = 0; k < v; k += 3) {
                EmGfxGsPrim *t = &p[count++];
                memset(t, 0, sizeof *t);
                t->prim = 0x07Bu;
                t->count = 3;
                for (unsigned c = 0; c < 3; ++c) {
                    const EmShadowGsClipVertex *s = &gv[k + c];
                    const int32_t w[4] = { (int32_t)(s->x * 16.0f), (int32_t)(s->y * 16.0f), (int32_t)(s->z << 4),
                                           (int32_t)(s->f << 4) };
                    const uint8_t rgba[4] = { 0, 0, 0, (uint8_t)s->a };
                    gsw_vertex_xyzf(&t->v[c], w, rgba, gsw_fbits(s->q), gsw_fbits(s->s), gsw_fbits(s->t));
                }
            }
        }
        free(dmem); free(res); free(gv);
    }
    if (clip) return gsw_fail(g, clip == 2 ? "out of memory" : "receiver clip kernel 0023E8A0 fault");
    em_gs_world_prims(g->gsw, p, NULL, count);
    return gsw_check(g);
}

/* 001D1FF0(0, 1): CLAMP block 1 (CLAMP 0). */
static int gsw_receiver_end(EmGfx *g)
{
    g->shadowRecvOpen = false;
    return gsw_block(g, GSW_CLAMP_BLOCK + 0x40u, 4);
}

/* A list frame (the load veil) through the model. */
static int gsw_list_frame(EmGfx *g, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count,
                          uint64_t display_frame, uint64_t display_scissor)
{
    gsw_complete(g);
    if (g->gswWhy[0]) return -1;
    if (em_gs_world_list_frame(g->gsw, prims, envs, count, display_frame, display_scissor) < 0) return gsw_check(g);
    g->gswShow = true;
    return 0;
}

/* em_gfx_gs_gif_frame: a list frame given as its GIF data (the status
 * pages), through the model (em_gs_world_gif_frame), with the Metal
 * backend's checks. */
int em_gfx_gs_gif_frame(EmGfx *g, const void *gif, size_t bytes, uint64_t display_frame,
                        uint64_t display_scissor)
{
    if (!g || !g->gswOn) return -1;
    if (!g->inFrame) return gsw_fail(g, "a GIF frame outside a frame");
    if (g->gswFrame) return gsw_fail(g, "a GIF frame in a recorded world frame");
    gsw_complete(g);
    if (g->gswWhy[0]) return -1;
    if (em_gs_world_gif_frame(g->gsw, gif, bytes, display_frame, display_scissor) < 0) return gsw_check(g);
    g->gswShow = true;
    return 0;
}


EmGfx *em_gfx_create(EmWindow *win) {
    if (!win || !em_headless()) return NULL;
    /* A null presenter cannot supply GPU readback. Refuse the request explicitly. */
    if (getenv("EM_PRESENT_LOG") || getenv("EM_CAPTURE")) {
        fprintf(stderr, "offscreen: presented-image readback requires a GPU backend\n");
        return NULL;
    }
    EmGfx *g = calloc(1, sizeof(EmGfx));
    if (g) g->gswRaster = 1;   /* (em_gfx_gs_world_raster) */
    return g;
}
void em_gfx_destroy(EmGfx *g) {
    if (!g) return;
    em_gs_world_destroy(g->gsw);
    em_object_unit_result_free(&g->objResult);
    free(g->gswPrims); free(g->bgFile); free(g);
}
void em_gfx_begin_frame(EmGfx *g, float r, float green, float b, float a) {
    (void)r; (void)green; (void)b; (void)a;
    g->gswShow = 0;
    g->inFrame = 1;
}
void em_gfx_end_frame(EmGfx *g) {
    if (g->gswFrame) gsw_fail(g, "a world frame was recorded but never kicked");
    g->inFrame = 0;
    g->presentFrames++;
}
void em_gfx_request_capture(EmGfx *g, const char *path) {
    (void)path;
    if (g->gswOn && !g->gswRaster) {
        gsw_fail(g, "a capture requested under the headless raster skip (EM_HEADLESS_RASTER=1 draws)");
        return;
    }
    gsw_fail(g, "presented-image capture requires a GPU backend");
}
int em_gfx_gs_display_store(EmGfx *g, uint32_t address, uint64_t value) {
    return g ? em_gs_display_store(&g->display, address, value) : -1;
}
void em_gfx_gs_display_release(EmGfx *g) { if (g) memset(&g->display, 0, sizeof g->display); }
int em_gfx_gs_surface_read(EmGfx *g, uint32_t fbp, uint32_t fbw, uint32_t height, uint8_t *rgba) {
    if (g && g->gswOn && !g->gswRaster) return gsw_fail(g, "a picture read under the headless raster skip");
    return g && g->gsw ? em_gs_world_read(g->gsw, fbp, fbw, 64u * fbw, height, rgba) : -1;
}
int em_gfx_gs_frame(EmGfx *g, const EmGfxGsPrim *p, const EmGfxGsEnv *e, uint32_t n,
                    uint64_t frame, uint64_t scissor) {
    return g && g->gsw ? gsw_list_frame(g, p, e, n, frame, scissor) : -1;
}
int em_gfx_gs_head(EmGfx *g, const void *env, size_t en, const void *clear, size_t cn, uint64_t dispfb) {
    (void)env; (void)en; (void)clear; (void)cn; (void)dispfb;
    /* No presented image is retained by a null presenter. */
    gsw_complete(g);
    return 0;
}
int em_gfx_background_prims_env(EmGfx *g, const EmGfxGsPrim *p, const EmGfxGsEnv *e, uint32_t n) {
    return g && g->gswFrame ? gsw_background(g,p,e,n) : -1;
}
int em_gfx_background_prims(EmGfx *g, const EmGfxGsPrim *p, uint32_t n) {
    return em_gfx_background_prims_env(g,p,NULL,n);
}
int em_gfx_gs_opaque(EmGfx *g, const EmGfxGsPrim *p, uint32_t n) {
    return g && g->gswFrame ? gsw_opaque(g,p,n) : -1;
}
int em_gfx_gs_prims(EmGfx *g, const EmGfxGsPrim *p, uint32_t n) {
    if (g && !n) return 0;
    return g && g->gswFrame ? gsw_page(g,p,n) : -1;
}
int em_gfx_object_unit(EmGfx *g, const EmGfxObjectUnit *u) {
    return g && u && g->gswFrame ? gsw_object(g,u) : -1;
}
int em_gfx_shadow_alpha_clear(EmGfx *g) { return g && g->gswFrame ? gsw_alpha_clear(g) : -1; }
int em_gfx_shadow_box(EmGfx *g, const EmGfxShadowStrips *m, const float world[16],
                     const float clip[16], uint32_t rgba, const float vp[16]) {
    (void)world; (void)vp;
    return g && m && clip && g->gswFrame ? gsw_box(g,m,clip,rgba) : -1;
}
int em_gfx_shadow_silhouette(EmGfx *g, const float *v, uint32_t nv, const uint32_t *ix, uint32_t ni,
                            const float *nodes, uint32_t nn, const float vp[16]) {
    return g && v && ix && nodes && vp && g->gswFrame ? gsw_silhouette(g,v,nv,ix,ni,nodes,nn,vp) : -1;
}
int em_gfx_shadow_receiver_begin(EmGfx *g, const float uv[16], const float camera[16], const float vp[16]) {
    (void)vp;
    return g && uv && camera && g->gswFrame && g->fog[3] > 0 ? gsw_receiver_begin(g,uv,camera) : -1;
}
int em_gfx_shadow_receiver(EmGfx *g, const EmGfxShadowStrips *o, uint32_t cls) {
    return g && o && g->gswFrame && g->shadowRecvOpen ? gsw_receiver(g,o,cls) : -1;
}
int em_gfx_shadow_receiver_end(EmGfx *g) {
    return g && g->gswFrame && g->shadowRecvOpen ? gsw_receiver_end(g) : -1;
}
int em_gfx_shadow_target_read(EmGfx *g, uint8_t *rgba) {
    return em_gfx_gs_surface_read(g, (uint32_t)EM_SHADOW_GS_FRAME_TARGET & 511u,
        (uint32_t)(EM_SHADOW_GS_FRAME_TARGET >> 16) & 63u, EM_SHADOW_GS_TARGET_SIZE, rgba);
}
void em_gfx_fog_coefficients(EmGfx *g, const float coef[2], const float rgb[3]) {
    if (!g || !coef || !rgb) return;
    if (g->gswFrame) (void)gsw_fogcol(g, rgb);
    g->fog[3]=1; g->fog[4]=coef[0]; g->fog[5]=coef[1];
}
void em_gfx_fog_off(EmGfx *g) { if (g) memset(g->fog,0,sizeof g->fog); }
void em_gfx_background_unload(EmGfx *g) {
    if (g) { free(g->bgFile); g->bgFile=NULL; memset(&g->bgAsset,0,sizeof g->bgAsset); }
}
int em_gfx_background_ready(EmGfx *g) { return g && g->bgFile; }
int em_gfx_background_state(EmGfx *g, uint64_t *tex0, uint32_t *rgbaq) {
    if (!g || !g->bgFile || !tex0 || !rgbaq) return 0;
    *tex0=g->bgAsset.tex0; *rgbaq=g->bgAsset.rgbaq; return 1;
}
int em_gfx_background_load(EmGfx *g, const char *path) {
    if (!g || !path) return -1;
    em_gfx_background_unload(g);
    FILE *f=fopen(path,"rb"); if (!f) return -1;
    long n=-1;
    if (!fseek(f,0,SEEK_END)) n=ftell(f);
    if (n <= 0 || n > EM_BACKGROUND_GS_HEADER+1024L*1024L*4L || fseek(f,0,SEEK_SET)) {
        fclose(f); return -1;
    }
    uint8_t *b=malloc((size_t)n);
    int ok=b && fread(b,1,(size_t)n,f)==(size_t)n;
    fclose(f);
    EmBackgroundGsAsset a;
    if (!ok || em_background_gs_parse(b,(size_t)n,&a) || em_background_gs_unsupported(&a)) {
        free(b); return -1;
    }
    g->bgFile=b; g->bgAsset=a; return 0;
}
EmGfxMesh *em_gfx_mesh_create(EmGfx *g, const float *v, uint32_t nv, const uint32_t *ix,
                            uint32_t ni, const EmGfxTexDesc *tex, uint32_t nt, const uint8_t *px,
                            uint32_t flags) {
    (void)g; (void)ix; (void)ni; (void)tex; (void)nt; (void)px; (void)flags;
    if (!v || !nv) return NULL;
    EmGfxMesh *m=malloc(sizeof *m); if (m) m->vertices=nv; return m;
}
void em_gfx_mesh_destroy(EmGfx *g, EmGfxMesh *m) { (void)g; free(m); }
int em_gfx_mesh_update_positions(EmGfx *g, EmGfxMesh *m, const float *p, uint32_t n) {
    (void)g; return m && p && n==m->vertices;
}
/* Textures expanded for GPU presentation are not sampled by this backend.
 * The GS path samples the original uploads in em_gs_world's local memory. */
int em_gfx_object_texture(EmGfx *g, uint64_t tex0, const uint8_t *rgba, uint32_t w, uint32_t h) {
    (void)tex0; return g && rgba && w && h ? 0 : -1;
}
int em_gfx_gs_texture(EmGfx *g, uint64_t tex0, const uint8_t *rgba, uint32_t w, uint32_t h) {
    return em_gfx_object_texture(g,tex0,rgba,w,h);
}
int em_gfx_world_textures_reset(EmGfx *g) { return g ? 0 : -1; }
int em_gfx_overlay_texture_set(EmGfx *g, int slot, const uint8_t *rgba, uint32_t w, uint32_t h) {
    (void)slot; return g && rgba && w && h;
}
int em_gfx_overlay_triangle(EmGfx *g, const float xy[3][2], const float rgba[3][4],
                            float u, float v, EmGfxOverlayBlend blend) {
    (void)u; (void)v; (void)blend; return g && xy && rgba;
}

void em_gfx_draw_test_triangle(EmGfx *gfx) { (void)gfx; }

void em_gfx_draw_skinned(EmGfx *gfx, EmGfxMesh *mesh, const float *viewproj,
                         const float *palette, uint32_t bone_count) { (void)gfx; (void)mesh; (void)viewproj; (void)palette; (void)bone_count; }

void em_gfx_draw_skinned_tinted(EmGfx *gfx, EmGfxMesh *mesh,
                                const float *viewproj, const float *palette,
                                uint32_t bone_count, const float rgba[4]) { (void)gfx; (void)mesh; (void)viewproj; (void)palette; (void)bone_count; (void)rgba; }

void em_gfx_overlay_canvas(EmGfx *gfx, float w, float h) { (void)gfx; (void)w; (void)h; }

void em_gfx_overlay_rect(EmGfx *gfx, float x, float y, float w, float h,
                         const float rgba[4]) { (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)rgba; }

void em_gfx_overlay_rect_sub(EmGfx *gfx, float x, float y, float w, float h,
                             const float rgb[3]) { (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)rgb; }

void em_gfx_overlay_rect_sub_before_text(EmGfx *gfx, float x, float y,
                                        float w, float h, const float rgb[3]) { (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)rgb; }

void em_gfx_overlay_rect_add(EmGfx *gfx, float x, float y, float w, float h,
                             const float rgb[3]) { (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)rgb; }

void em_gfx_overlay_arc4(EmGfx *gfx, float cx, float cy,
                         float r_in, float r_out, float a0, float a1,
                         const float rgba_is[4], const float rgba_os[4],
                         const float rgba_ie[4], const float rgba_oe[4]) { (void)gfx; (void)cx; (void)cy; (void)r_in; (void)r_out; (void)a0; (void)a1; (void)rgba_is; (void)rgba_os; (void)rgba_ie; (void)rgba_oe; }

void em_gfx_overlay_arc(EmGfx *gfx, float cx, float cy,
                        float r_in, float r_out, float a0, float a1,
                        const float rgba[4]) { (void)gfx; (void)cx; (void)cy; (void)r_in; (void)r_out; (void)a0; (void)a1; (void)rgba; }

void em_gfx_overlay_glyph(EmGfx *gfx, float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1,
                          const float rgba[4]) { (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)u0; (void)v0; (void)u1; (void)v1; (void)rgba; }

void em_gfx_overlay_glyph_nearest(EmGfx *gfx, int on) { (void)gfx; (void)on; }

void em_gfx_overlay_glyph_gs(EmGfx *gfx, const EmGfxGsGlyph *q, const float rgba[4]) { (void)gfx; (void)q; (void)rgba; }

void em_gfx_overlay_glyph_skew(EmGfx *gfx, float x, float y, float w, float h,
                              float top_skew,
                              float u0, float v0, float u1, float v1,
                              const float rgba[4]) { (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)top_skew; (void)u0; (void)v0; (void)u1; (void)v1; (void)rgba; }

void em_gfx_overlay_sprite(EmGfx *gfx, float x, float y, float w, float h,
                           float u0, float v0, float u1, float v1,
                           const float rgba[4]) { (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)u0; (void)v0; (void)u1; (void)v1; (void)rgba; }

void em_gfx_overlay_sprite_blend(EmGfx *gfx,float x,float y,float w,float h,
    float u0,float v0,float u1,float v1,const float rgba[4],EmGfxOverlayBlend blend) { (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)u0; (void)v0; (void)u1; (void)v1; (void)rgba; (void)blend; }

void em_gfx_overlay_backdrop_fill(EmGfx *gfx, const float rgba[4]) { (void)gfx; (void)rgba; }

void em_gfx_overlay_backdrop(EmGfx *gfx, float x, float y, float w, float h,
                             float u0, float v0, float u1, float v1,
                             const float rgba[4]) { (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)u0; (void)v0; (void)u1; (void)v1; (void)rgba; }

void em_gfx_overlay_backdrop_flush(EmGfx *gfx) { (void)gfx; }

void em_gfx_overlay_decor_flush(EmGfx *gfx) { (void)gfx; }

void em_gfx_draw_scissor(EmGfx *gfx, const float rect[4]) { (void)gfx; (void)rect; }

void em_gfx_spot_light(EmGfx *gfx, const float pos[3], const float dir[3],
                       const float rgb[3], float range,
                       float cos_inner, float cos_outer) { (void)gfx; (void)pos; (void)dir; (void)rgb; (void)range; (void)cos_inner; (void)cos_outer; }

void em_gfx_char_rig(EmGfx *gfx, const EmGfxCharRig *rig) { (void)gfx; (void)rig; }

void em_gfx_char_face_rig(EmGfx *gfx, const EmGfxCharRig *rig) { (void)gfx; (void)rig; }

void em_gfx_fog(EmGfx *gfx, float near_z, float far_z, const float rgb[3]) { (void)gfx; (void)near_z; (void)far_z; (void)rgb; }

void em_gfx_present_tag(EmGfx *gfx, uint64_t tag) { (void)gfx; (void)tag; }
