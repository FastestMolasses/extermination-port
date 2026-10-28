/* gs_world_test - the Original profile's GS frame (src/gs/em_gs_world.h):
 * its workers draw exactly what one worker draws.
 *
 * Designed primitives only (nothing disc-derived): a GS memory image with
 * one uploaded texture, then two recorded frames and a list frame, each
 * run with 1, 2, 3 and 8 workers (EM_GS_THREADS) and the host default
 * (EM_GS_THREADS unset). The frames exercise what
 * the world frame does: textured, fogged, Z-tested triangles over resident
 * texels; a 128x128 target drawn and then sampled (a barrier: every band of
 * the target is drawn before any band reads it), drawn into again (a
 * barrier: every band has read it first) and sampled again; the draw
 * environment REFed again; a state block; sprites; the field of the next
 * frame read as a texture by a list frame; and the chain page's
 * depth-of-field pass (001DDE10, CHAIN_PAGE.md section 6.2): the field
 * declared, then four times the field copied into a 256x256 buffer over the
 * target's memory (a barrier: every band of the field is drawn first) and
 * the copy blended back over the field under its Z test (a barrier: every
 * band of the copy is drawn first; the next copy waits for every band's
 * blend), the draw environment again after each copy and at the end. Every
 * worker count must leave the same local memory and the same fields, and
 * the one-worker run is the model run directly (em_gs_raster, one EmGs, the
 * same writes); the pass changes the field (against the same frames
 * without it). The same frames again with 1,100 extra texture keys in the
 * first (more than the recorder's texture table keeps, so every later
 * texture is checked again at each kick) draw the same with 1 and 8 workers,
 * and the EM_GS_TEX_COUNTERS lines show the table full. The decoded texture
 * cache off (EM_GS_TEXCACHE=0) and the specialized span loops off
 * (EM_GS_SPEC=0) draw the same, and so does the loops' check
 * (EM_GS_SPEC_CHECK=1: each primitive a loop takes drawn by the generic path
 * too and compared), which the loops' counters show took primitives of each
 * loop (EM_GS_SPEC_COUNTERS). With more than one worker the frames are
 * parsed once and drawn in bands from a queue (docs/PERFORMANCE.md M4):
 * the frames again kicked as the backend kicks them, the next one parsed
 * early while the one before is drawn (em_gs_world_preparse; its T8 CLUT
 * loaded from the field the one before drew, so the early parse waits for
 * that frame), with 3 and 8 workers, the early parse forced to hand over
 * after 37 ops (EM_GS_EARLY_OPS; also with one worker drawing the list,
 * EM_GS_LIST=1), the early parse off and 1 and 8 bands a worker
 * (EM_GS_BANDS), draw what one worker draws; and the list's texture-cache
 * stamps (em_gs_list_draw, em_gs_list_pass) with two drawing models in a
 * fixed band order leave the memory one model leaves; with the early parse
 * forced past each frame's end the second frame's CLUT load waits for the
 * first frame (counted). Small frames over every block uploaded check the
 * recorder's barriers and the parse's CLUT loads against one worker drawing
 * directly, with one worker drawing the list (its bands one after another,
 * so a missing barrier shows as wrong pixels every time), 3 and 8 workers:
 * a barrier for a texture read, then a read of the drawing target drawn
 * since (texture_read marks the target drawn again), and a read of the
 * declared field before the first FRAME_1 write (the field is the drawing
 * target: em_gs_world_field_declare), each in three segments, and a read of
 * uploaded memory before its first draw (a barrier at that draw); and CLUT
 * loads from pages the frame itself drew (own_drain: the spans so far drawn
 * first), also parsed early, whole or handing over. Also the refusals:
 * a transfer register, an IMAGE packet and a texture outside the uploads
 * fault, and so do a field declared outside a recording, a second other
 * field, and a kick whose head draws another field than the declared one.
 * And GIF frames (gif_frames): a whole list's GIF data, a transfer in it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_gfx.h"
#include "gs/em_gs_raster.h"
#include "gs/em_gs_world.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "gs_world_test: FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static const char *const image_one = "build/gs_world_test/memory.emgm";
static const char *const image_all = "build/gs_world_test/memory_all.emgm";
static const char *image_path = "build/gs_world_test/memory.emgm";

/* An EMGM with the 64x64 PSMCT32 texture at block 0x2000 (16 blocks a row
 * of pages: 64 blocks), and the same memory as one run of every block (all
 * of local memory uploaded: the buffers the frames draw too, whose reads the
 * workers must order all the same). */
static void write_image(void)
{
    uint8_t *mem = calloc(1, EM_GS_MEM_BYTES);
    EmGs g;
    em_gs_init(&g, mem);
    for (uint32_t y = 0; y < 64; ++y)
        for (uint32_t x = 0; x < 64; ++x)
            em_gs_write_pixel(&g, 0x2000, 1, EM_GS_PSMCT32, x, y,
                              (x * 4u) | (y * 4u) << 8 | ((x ^ y) * 4u) << 16 | 0x80u << 24);
    for (int all = 0; all < 2; ++all) {
        FILE *f = fopen(all ? image_all : image_one, "wb");
        CHECK(f);
        const uint32_t head[4] = { 0x4D474D45u, 1, 1, 0 };   /* "EMGM", v1, one run */
        fwrite(head, 4, 4, f);
        const uint32_t run[2] = { all ? 0u : 0x2000u, all ? EM_GS_MEM_BYTES / 256u : 64u };
        fwrite(run, 4, 2, f);
        fwrite(mem + run[0] * 256u, 1, run[1] * 256u, f);
        fclose(f);
    }
    free(mem);
}

/* A+D GIF packet of `n` (register, value) pairs. */
static size_t ad_packet(uint8_t *out, const uint64_t (*pairs)[2], unsigned n)
{
    const uint64_t tag[2] = { (uint64_t)n | 0x8000u | (UINT64_C(1) << 60), 0xE };
    memcpy(out, tag, 16);
    for (unsigned i = 0; i < n; ++i) {
        memcpy(out + 16 + 16 * i, &pairs[i][1], 8);
        memcpy(out + 24 + 16 * i, &pairs[i][0], 8);
    }
    return 16u + 16u * n;
}

static EmGfxGsVertex vtx(int x, int y, uint32_t z, uint8_t f, uint8_t r, uint8_t gg, uint8_t b, uint8_t a, float s,
                         float t, float q)
{
    EmGfxGsVertex v;
    memset(&v, 0, sizeof v);
    v.x = (uint16_t)((1792 + x) * 16);
    v.y = (uint16_t)((1936 + y) * 16);
    v.z = z;
    v.f = f;
    v.has_f = 1;
    v.rgba[0] = r; v.rgba[1] = gg; v.rgba[2] = b; v.rgba[3] = a;
    memcpy(&v.q, &q, 4);
    memcpy(&v.s, &s, 4);
    memcpy(&v.t, &t, 4);
    return v;
}

#define TEX_RESIDENT (0x2000ull | 1ull << 14 | 6ull << 26 | 6ull << 30 | 1ull << 34)   /* CT32 64x64, TCC */
#define TEX_TARGET (0x2580ull | 2ull << 14 | 7ull << 26 | 7ull << 30 | 1ull << 34)    /* the 128x128 target */
#define FRAME_FIELD(fbp) ((uint64_t)(fbp) | 8ull << 16)
#define FRAME_TARGET 0x2012Cull

/* The head: the draw environment (FRAME_1 fbp, ZBUF, XYOFFSET, SCISSOR,
 * PRMODECONT, COLCLAMP, DTHE, TEST) and a Z clear. */
static void head(uint8_t *env, size_t *env_n, uint8_t *clr, size_t *clr_n, uint32_t fbp, int half)
{
    const uint64_t e[][2] = {
        { EM_GS_FRAME_1, FRAME_FIELD(fbp) }, { EM_GS_ZBUF_1, 0x01000070 },
        { EM_GS_XYOFFSET_1, (uint64_t)(1792 * 16) | (uint64_t)(1936 * 16 + (half ? 8 : 0)) << 32 },
        { EM_GS_SCISSOR_1, 511ull << 16 | 223ull << 48 }, { EM_GS_PRMODECONT, 1 }, { EM_GS_COLCLAMP, 1 },
        { EM_GS_DTHE, 0 }, { EM_GS_TEST_1, 0x50000 }, { EM_GS_FOGCOL, 0x303030 },
    };
    *env_n = ad_packet(env, e, sizeof e / sizeof e[0]);
    const uint64_t c[][2] = {
        { EM_GS_TEST_1, 0x32001 }, { EM_GS_PRIM, 6 }, { EM_GS_RGBAQ, 0x3F80000000000000ull },
        { EM_GS_XYZ2, (1792 * 16) | (uint64_t)(1936 * 16) << 16 },
        { EM_GS_XYZ2, (2304 * 16) | (uint64_t)(2160 * 16) << 16 }, { EM_GS_TEST_1, 0x50000 },
    };
    *clr_n = ad_packet(clr, c, sizeof c / sizeof c[0]);
}

static EmGfxGsPrim tri(uint32_t prim, uint64_t tex0, EmGfxGsVertex a, EmGfxGsVertex b, EmGfxGsVertex c)
{
    EmGfxGsPrim p;
    memset(&p, 0, sizeof p);
    p.prim = prim;
    p.set = EM_GFX_GS_TEX0 | EM_GFX_GS_TEX1 | EM_GFX_GS_CLAMP | EM_GFX_GS_TEST | EM_GFX_GS_ALPHA | EM_GFX_GS_COLCLAMP;
    p.tex0 = tex0;
    p.tex1 = 0x60;
    p.clamp = 0;
    p.test = 0x5000D;
    p.alpha = 0x44;
    p.colclamp = 1;
    p.count = 3;
    p.v[0] = a; p.v[1] = b; p.v[2] = c;
    return p;
}

/* The depth-of-field pass as em_chain_page's pass mode hands it to the
 * model (em_gfx_gs_page_pass -> em_gs_world_page_pass): the declared field,
 * then per pass the copy (PRIM 0x116: the field, DECAL, REGION_CLAMP to its
 * 512 x 224, into the 256 x 256 buffer at FBP 0x12C with the field's Z
 * masked), the draw environment again, the blend (PRIM 0x156: the copy,
 * MODULATE, alpha NEVER with FB_ONLY, Z GEQUAL at the pass's depth, (Cs -
 * Cd) As + Cd); the draw environment again at the end (marks 1, 3, 5, 7,
 * 8). */
static void record_pass(EmGsWorld *w, uint32_t fbp)
{
    EmGfxGsPrim prims[8];
    EmGfxGsEnv envs[8];
    static const uint32_t again[5] = { 1, 3, 5, 7, 8 };
    for (unsigned k = 0; k < 4u; ++k) {
        EmGfxGsPrim c, b;
        EmGfxGsEnv ce, be;
        memset(&c, 0, sizeof c);
        memset(&ce, 0, sizeof ce);
        c.prim = 0x116;
        c.set = EM_GFX_GS_TEX0 | EM_GFX_GS_CLAMP | EM_GFX_GS_TEST | EM_GFX_GS_COLCLAMP;
        c.tex0 = (uint64_t)fbp * 32u | 8ull << 14 | 9ull << 26 | 8ull << 30 | 1ull << 35;
        c.clamp = 2 | 2 << 2 | 511ull << 14 | 223ull << 34;
        c.test = 0x3000D;
        c.colclamp = 1;
        c.count = 2;
        for (unsigned v = 0; v < 2u; ++v) {
            c.v[v].x = c.v[v].y = (uint16_t)(v ? 0x8800 : 0x7800);
            c.v[v].f = 0xF;
            c.v[v].has_f = 1;
            c.v[v].rgba[0] = c.v[v].rgba[1] = c.v[v].rgba[2] = c.v[v].rgba[3] = 0x80;
            c.v[v].q = 0x3F800000u;
            c.v[v].u = (uint16_t)(v ? 0x1FF8 : 8);
            c.v[v].v = (uint16_t)(v ? 0xDF8 : 8);
        }
        ce.set = EM_GFX_GS_ENV_FRAME | EM_GFX_GS_ENV_ZBUF | EM_GFX_GS_ENV_XYOFFSET | EM_GFX_GS_ENV_SCISSOR |
                 EM_GFX_GS_ENV_PRMODECONT | EM_GFX_GS_ENV_DTHE | EM_GFX_GS_ENV_TEXA;
        ce.frame = 0x4012C;
        ce.zbuf = 0x101000070ull;
        ce.xyoffset = 0x780000007800ull;
        ce.scissor = 0x00FF000000FF0000ull;
        ce.prmodecont = 1;
        ce.texa = 0x20;
        prims[2 * k] = c;
        envs[2 * k] = ce;
        memset(&b, 0, sizeof b);
        memset(&be, 0, sizeof be);
        b.prim = 0x156;
        b.set = EM_GFX_GS_TEX0 | EM_GFX_GS_CLAMP | EM_GFX_GS_TEST | EM_GFX_GS_ALPHA;
        b.tex0 = 0x220012580ull;
        b.clamp = 2 | 2 << 2 | 255ull << 14 | 255ull << 34;
        b.test = 0x51001;
        b.alpha = 0x44;
        b.count = 2;
        for (unsigned v = 0; v < 2u; ++v) {
            b.v[v].x = (uint16_t)(v ? 0x9000 : 0x7000);
            b.v[v].y = (uint16_t)(v ? 0x8700 : 0x7900);
            b.v[v].z = 2000u + 2500u * k;      /* the scene's Z runs 1000..9000 */
            b.v[v].f = 8;
            b.v[v].has_f = 1;
            b.v[v].rgba[0] = b.v[v].rgba[1] = b.v[v].rgba[2] = 0x80;
            b.v[v].rgba[3] = (uint8_t)(0x18 + 0x10 * k);
            b.v[v].q = 0x3F800000u;
            b.v[v].u = b.v[v].v = (uint16_t)(v ? 0x1008 : 8);
        }
        be.set = EM_GFX_GS_ENV_TEXA;
        prims[2 * k + 1] = b;
        envs[2 * k + 1] = be;
    }
    CHECK(em_gs_world_page_pass(w, prims, envs, 8, again, 5, FRAME_FIELD(fbp), 0x00DF000001FF0000ull) == 0);
}

static int g_with_pass = 1;
static int g_many_keys = 0;
static int g_clut_prev = 0;   /* the second frame's T8 triangles take their CLUT from the first's field */
#define TEX_T8_PREV (0x2000ull | 2ull << 14 | 0x13ull << 20 | 6ull << 26 | 6ull << 30 | 1ull << 34 | \
                     0x100ull << 37 | 1ull << 61)   /* T8 64x64 over the resident texels, CLUT at block 0x100 */
#define MANY_KEYS 1100u    /* more texture keys than em_gs_world.c keeps (CHECKED_MAX 1024) */

/* One world frame: the scene, the target pass twice (the first frame
 * only: in the second, nothing reads the copy's memory before the pass, so
 * only the field's declaration orders the first copy after the scene), the
 * depth-of-field pass, the field. */
static void record_world(EmGsWorld *w, uint32_t seed, uint32_t fbp, int target_pass)
{
    em_gs_world_begin(w);
    /* textured, fogged, Z-tested triangles over the resident texture */
    EmGfxGsPrim p[64];
    unsigned n = 0;
    for (unsigned i = 0; i < 24; ++i) {
        const int x = (int)((seed * 37u + i * 53u) % 460u), y = (int)((seed * 11u + i * 29u) % 200u);
        p[n++] = tri(0x03C, TEX_RESIDENT, vtx(x, y, 1000 + i, 200, 128, 100, 90, 128, 0.0f, 0.0f, 1.0f),
                     vtx(x + 60, y + 7, 9000 + i * 3, 120, 90, 128, 70, 128, 1.0f, 0.1f, 1.0f),
                     vtx(x + 11, y + 23, 5000, 40, 60, 70, 128, 128, 0.3f, 1.0f, 1.0f));
    }
    em_gs_world_prims(w, p, NULL, n);
    /* (g_clut_prev, the second frame) T8 triangles whose CLUT (CLD 1) is
     * loaded from block 0x100, in the field the first frame drew: a parse
     * of this frame that runs while the first is drawn (em_gs_world_preparse)
     * must wait for it before the load reads memory */
    for (unsigned i = 0; g_clut_prev && seed == 12u && i < 6; ++i) {
        const int x = (int)((i * 83u) % 420u), y = (int)((i * 31u) % 170u);
        EmGfxGsPrim t8 = tri(0x03C, TEX_T8_PREV, vtx(x, y, 3000 + i, 200, 128, 128, 128, 128, 0.0f, 0.0f, 1.0f),
                             vtx(x + 70, y + 9, 3000 + i, 200, 128, 128, 128, 128, 1.0f, 0.2f, 1.0f),
                             vtx(x + 13, y + 40, 3000 + i, 200, 128, 128, 128, 128, 0.2f, 1.0f, 1.0f));
        em_gs_world_prims(w, &t8, NULL, 1);
    }
    /* (g_many_keys, the first frame) the resident texture under MANY_KEYS
     * distinct REGION_CLAMP words, kicked by triangles of no area: the
     * texture lookup fills its table (CHECKED_MAX), and every texture after
     * them (the target, the copy, the field) is checked again at every kick
     * (docs/PERFORMANCE.md Q7); nothing is drawn */
    for (unsigned i = 0; g_many_keys && seed == 11u && i < MANY_KEYS; ++i) {
        const EmGfxGsVertex v = vtx(5, 5, 1000, 200, 128, 100, 90, 128, 0.0f, 0.0f, 1.0f);
        EmGfxGsPrim k = tri(0x03C, TEX_RESIDENT, v, v, v);
        k.clamp = 2u | (uint64_t)(i & 31u) << 4 | (uint64_t)(32u + (i >> 5)) << 14;
        em_gs_world_prims(w, &k, NULL, 1);
    }
    /* the 128x128 target: drawn, sampled by blended triangles (a barrier) */
    for (int pass = 0; pass < (target_pass ? 2 : 0); ++pass) {
        em_gs_world_write(w, EM_GS_FRAME_1, FRAME_TARGET);
        em_gs_world_drawn_buffer(w, FRAME_TARGET, 128);
        em_gs_world_write(w, EM_GS_ZBUF_1, 0x102000010ull);
        em_gs_world_write(w, EM_GS_XYOFFSET_1, 0x7C0000007C00ull);
        em_gs_world_write(w, EM_GS_SCISSOR_1, 0x7F0000007F0000ull);
        em_gs_world_write(w, EM_GS_TEST_1, 0x30000);
        em_gs_world_write(w, EM_GS_PRIM, 6);
        em_gs_world_write(w, EM_GS_RGBAQ, 0x3F80000000808080ull);
        em_gs_world_write(w, EM_GS_XYZ2, 0x7C007C00ull);
        em_gs_world_write(w, EM_GS_XYZ2, 0x84008400ull);
        em_gs_world_write(w, EM_GS_RGBAQ, 0xFFFFFF80ull);
        for (unsigned i = 0; i < 6; ++i) {
            EmGfxGsPrim t;
            memset(&t, 0, sizeof t);
            t.prim = 0x004;
            t.count = 3;
            /* target window coordinates: XYOFFSET (1984, 1984) */
            const int x = (int)((seed + i * 17u + (unsigned)pass * 9u) % 90u);
            const int y = (int)((seed * 3u + i * 13u) % 80u);
            const int xs[3] = { x, x + 30, x + 5 }, ys[3] = { y, y + 4, y + 40 };
            for (unsigned k = 0; k < 3; ++k) {
                memset(&t.v[k], 0, sizeof t.v[k]);
                t.v[k].x = (uint16_t)((1984 + xs[k]) * 16 + 5);
                t.v[k].y = (uint16_t)((1984 + ys[k]) * 16 + 3);
                t.v[k].rgba[0] = 0x80; t.v[k].rgba[1] = t.v[k].rgba[2] = t.v[k].rgba[3] = 0xFF;
            }
            em_gs_world_prims(w, &t, NULL, 1);
        }
        em_gs_world_env_again(w);
        em_gs_world_write(w, EM_GS_TEX0_1, TEX_TARGET);
        em_gs_world_write(w, EM_GS_CLAMP_1, 5);
        em_gs_world_write(w, EM_GS_TEX1_1, 0x60);
        em_gs_world_write(w, EM_GS_ALPHA_1, 0x44);
        em_gs_world_write(w, EM_GS_TEST_1, 0x5000D);
        EmGfxGsPrim r[8];
        for (unsigned i = 0; i < 8; ++i) {
            const int x = (int)((seed * 7u + i * 61u + (unsigned)pass * 30u) % 400u), y = (int)((i * 27u) % 180u);
            r[i] = tri(0x07C, 0, vtx(x, y, 100, 128, 0, 0, 0, 60, 0.0f, 0.0f, 1.0f),
                       vtx(x + 90, y + 10, 100, 128, 0, 0, 0, 60, 1.0f, 0.0f, 1.0f),
                       vtx(x + 20, y + 44, 100, 128, 0, 0, 0, 60, 0.2f, 1.0f, 1.0f));
            r[i].set = 0;
        }
        em_gs_world_prims(w, r, NULL, 8);
    }
    if (g_with_pass) record_pass(w, fbp);
}

typedef struct {
    uint8_t field[3][512 * 224 * 4];
    uint8_t buf[5][512 * 224 * 4];    /* fields A and B, the Z buffer (read as CT32), the target, the copy */
} Result;

static unsigned g_host_workers;          /* the host default's count (run(0)) */

/* A list frame: sprites reading the field just drawn (FBP 0x38) into FBP 0. */
static void list_frame(EmGsWorld *w, Result *out)
{
    EmGfxGsPrim sp[4];
    EmGfxGsEnv ev[4];
    for (unsigned i = 0; i < 4; ++i) {
        memset(&sp[i], 0, sizeof sp[i]);
        memset(&ev[i], 0, sizeof ev[i]);
        sp[i].prim = 0x156;      /* sprite, TME, ABE, FST */
        sp[i].set = EM_GFX_GS_TEX0 | EM_GFX_GS_TEX1 | EM_GFX_GS_CLAMP | EM_GFX_GS_TEST | EM_GFX_GS_ALPHA |
                    EM_GFX_GS_COLCLAMP;
        sp[i].tex0 = 0x700ull | 8ull << 14 | 9ull << 26 | 8ull << 30 | 1ull << 35;   /* FBP 0x38, DECAL */
        sp[i].tex1 = 0;
        sp[i].clamp = 2 | 2 << 2 | 511ull << 14 | 223ull << 34;   /* REGION_CLAMP to the field's rows */
        sp[i].test = 0x30000;
        sp[i].alpha = 0x80000000A8ull;
        sp[i].colclamp = 1;
        sp[i].count = 2;
        sp[i].v[0] = vtx((int)i * 100, (int)i * 40, 0, 0, 128, 128, 128, 128, 0, 0, 1);
        sp[i].v[1] = vtx((int)i * 100 + 150, (int)i * 40 + 90, 0, 0, 128, 128, 128, 128, 0, 0, 1);
        sp[i].v[0].u = (uint16_t)(i * 40 * 16); sp[i].v[0].v = 0;
        sp[i].v[1].u = (uint16_t)((i * 40 + 150) * 16); sp[i].v[1].v = 90 * 16;
        sp[i].v[0].has_f = sp[i].v[1].has_f = 0;
        ev[i].set = EM_GFX_GS_ENV_FRAME | EM_GFX_GS_ENV_XYOFFSET | EM_GFX_GS_ENV_SCISSOR | EM_GFX_GS_ENV_PRMODECONT |
                    EM_GFX_GS_ENV_ZBUF;
        ev[i].frame = FRAME_FIELD(0);
        ev[i].zbuf = 0x101000070ull;
        ev[i].xyoffset = (uint64_t)(1792 * 16) | (uint64_t)(1936 * 16) << 32;
        ev[i].scissor = 511ull << 16 | 223ull << 48;
        ev[i].prmodecont = 1;
    }
    CHECK(em_gs_world_list_frame(w, sp, ev, 4, FRAME_FIELD(0), 223ull << 48) == 0);
    CHECK(em_gs_world_wait(w) == 0);
    const uint8_t *fld = em_gs_world_field(w, NULL, NULL, NULL);
    CHECK(fld);
    memcpy(out->field[2], fld, sizeof out->field[2]);
}

/* workers 0: EM_GS_THREADS unset, the host default (em_gs_world.c
 * worker_count: the core-type count and the workers' QoS) */
static void run(unsigned workers, Result *out)
{
    char n[8];
    snprintf(n, sizeof n, "%u", workers);
    if (workers)
        setenv("EM_GS_THREADS", n, 1);
    else
        unsetenv("EM_GS_THREADS");
    EmGsWorld *w = em_gs_world_create();
    CHECK(w);
    if (workers)
        CHECK(em_gs_world_workers(w) == workers);
    else {
        g_host_workers = em_gs_world_workers(w);
        CHECK(g_host_workers >= 1 && g_host_workers <= 8);
    }
    CHECK(em_gs_world_memory_load(w, image_path) == 0);
    uint8_t env[512], clr[512];
    size_t env_n, clr_n;
    for (unsigned f = 0; f < 2; ++f) {
        record_world(w, 11u + f, f ? 0x38 : 0, f == 0);
        head(env, &env_n, clr, &clr_n, f ? 0x38 : 0, (int)f);
        CHECK(em_gs_world_kick(w, env, env_n, clr, clr_n) == 0);
        CHECK(em_gs_world_wait(w) == 0);
        uint32_t fw, fh;
        uint64_t frame;
        const uint8_t *fld = em_gs_world_field(w, &fw, &fh, &frame);
        CHECK(fld && fw == 512 && fh == 224 && (frame & 0x1FF) == (f ? 0x38u : 0u));
        memcpy(out->field[f], fld, sizeof out->field[f]);
    }
    list_frame(w, out);
    EmGsWorldStats st;
    em_gs_world_stats(w, &st);
    CHECK(st.runs == 3 && st.pixels > 0);
    /* the buffers the frames drew, read back */
    CHECK(em_gs_world_read(w, 0, 8, 512, 224, out->buf[0]) == 0);
    CHECK(em_gs_world_read(w, 0x38, 8, 512, 224, out->buf[1]) == 0);
    CHECK(em_gs_world_read(w, 0x70, 8, 512, 224, out->buf[2]) == 0);
    CHECK(em_gs_world_read(w, 0x12C, 2, 128, 128, out->buf[3]) == 0);
    CHECK(em_gs_world_read(w, 0x12C, 4, 256, 256, out->buf[4]) == 0);
    em_gs_world_destroy(w);
}

/* The frames of run() (with every block uploaded and g_clut_prev), kicked
 * as the backend kicks them: the next frame recorded and parsed early
 * (em_gs_world_preparse) while the one before is drawn, then that one
 * waited for and its field read, then the kick. */
static void run_piped(unsigned workers, Result *out)
{
    char n[8];
    snprintf(n, sizeof n, "%u", workers);
    setenv("EM_GS_THREADS", n, 1);
    EmGsWorld *w = em_gs_world_create();
    CHECK(w && em_gs_world_workers(w) == workers);
    CHECK(em_gs_world_memory_load(w, image_all) == 0);
    uint8_t env[512], clr[512];
    size_t env_n, clr_n;
    for (unsigned f = 0; f < 2; ++f) {
        record_world(w, 11u + f, f ? 0x38 : 0, f == 0);
        head(env, &env_n, clr, &clr_n, f ? 0x38 : 0, (int)f);
        CHECK(em_gs_world_preparse(w, env, env_n, clr, clr_n) == 0);
        if (f) {
            CHECK(em_gs_world_wait(w) == 0);
            const uint8_t *fld = em_gs_world_field(w, NULL, NULL, NULL);
            CHECK(fld);
            memcpy(out->field[0], fld, sizeof out->field[0]);
        }
        CHECK(em_gs_world_kick(w, env, env_n, clr, clr_n) == 0);
    }
    CHECK(em_gs_world_wait(w) == 0);
    const uint8_t *fld = em_gs_world_field(w, NULL, NULL, NULL);
    CHECK(fld);
    memcpy(out->field[1], fld, sizeof out->field[1]);
    list_frame(w, out);
    CHECK(em_gs_world_read(w, 0, 8, 512, 224, out->buf[0]) == 0);
    CHECK(em_gs_world_read(w, 0x38, 8, 512, 224, out->buf[1]) == 0);
    CHECK(em_gs_world_read(w, 0x70, 8, 512, 224, out->buf[2]) == 0);
    CHECK(em_gs_world_read(w, 0x12C, 2, 128, 128, out->buf[3]) == 0);
    CHECK(em_gs_world_read(w, 0x12C, 4, 256, 256, out->buf[4]) == 0);
    em_gs_world_destroy(w);
}

/* EM_GS_LIST_COUNTERS' last two columns (prior_waits, early_ops), summed
 * over the frames; the frames with early ops. */
static unsigned early_frames(const char *path, unsigned long long *ops, unsigned long long *waits)
{
    FILE *f = fopen(path, "r");
    CHECK(f);
    char line[512];
    unsigned frames = 0;
    *ops = *waits = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        char *last = strrchr(line, ' ');
        CHECK(last);
        const unsigned long long v = strtoull(last + 1, NULL, 10);
        *last = 0;
        const char *prev = strrchr(line, ' ');
        CHECK(prev);
        *waits += strtoull(prev + 1, NULL, 10);
        *ops += v;
        frames += v > 0;
    }
    fclose(f);
    return frames;
}

/* An upload chain as em_gs_world_upload_chain takes it (one END DMA tag, a
 * DIRECT VIF code, then GIF data: the A+D transfer registers and the IMAGE
 * data of a host-to-local PSMCT32 transfer): the 64x64 resident texture at
 * block 0x2000 with new texels. */
static size_t upload_chain(uint8_t *buf, uint32_t seed)
{
    const uint64_t regs[4][2] = { { EM_GS_BITBLTBUF, 0x2000ull << 32 | 1ull << 48 }, { EM_GS_TRXPOS, 0 },
                                  { EM_GS_TRXREG, 64 | 64ull << 32 }, { EM_GS_TRXDIR, 0 } };
    uint8_t *gif = buf + 32;               /* after the DMA tag and the VIF codes */
    size_t n = ad_packet(gif, regs, 4);
    gif[7] &= 0x7F;                        /* (EOP 0: the IMAGE tag follows) */
    const uint64_t img[2] = { 1024u | 0x8000u | (UINT64_C(2) << 58), 0 };   /* IMAGE, 1024 qwords, EOP */
    memcpy(gif + n, img, 16);
    n += 16;
    for (uint32_t i = 0; i < 64u * 64u; ++i) {
        const uint32_t x = i & 63u, y = i >> 6;
        const uint32_t c = (255u - x * 4u) | ((y * 4u) ^ seed) << 8 | (x * y + seed) << 16 | 0x80u << 24;
        memcpy(gif + n + 4u * i, &c, 4);
    }
    n += 64u * 64u * 4u;
    const uint32_t qw = (uint32_t)(n / 16u);
    const uint32_t vif[4] = { 0, 0, 0, 0x50000000u | qw };                  /* NOP x3, DIRECT */
    memcpy(buf + 16, vif, 16);
    const uint64_t tag[2] = { (uint64_t)(qw + 1u) | 7ull << 28, 0 };        /* END, the VIF data's qwords */
    memcpy(buf, tag, 16);
    return 32u + n;
}

/* The decoded texture cache across the frames' sync (em_gs_world.c
 * workers_memory_written): a world frame samples the resident texture (its
 * copies made), an upload rewrites it between frames, and the next frame
 * samples it again. `cache_off`: EM_GS_TEXCACHE=0 (every texel from memory). */
static void run_upload(unsigned workers, int cache_off, Result *out)
{
    char n[8];
    snprintf(n, sizeof n, "%u", workers);
    setenv("EM_GS_THREADS", n, 1);
    if (cache_off) setenv("EM_GS_TEXCACHE", "0", 1);
    EmGsWorld *w = em_gs_world_create();
    unsetenv("EM_GS_TEXCACHE");
    CHECK(w && em_gs_world_workers(w) == workers);
    CHECK(em_gs_world_memory_load(w, image_path) == 0);
    static uint8_t chain[32 + 16 * 6 + 64 * 64 * 4];
    uint8_t env[512], clr[512];
    size_t env_n, clr_n;
    for (unsigned f = 0; f < 3; ++f) {
        if (f) {
            const size_t bytes = upload_chain(chain, 0x35u * f);
            CHECK(bytes <= sizeof chain && em_gs_world_upload_chain(w, chain, bytes) == 0);
        }
        record_world(w, 11u + f, (f & 1) ? 0x38 : 0, f == 0);
        head(env, &env_n, clr, &clr_n, (f & 1) ? 0x38 : 0, (int)(f & 1));
        CHECK(em_gs_world_kick(w, env, env_n, clr, clr_n) == 0);
        CHECK(em_gs_world_wait(w) == 0);
        const uint8_t *fld = em_gs_world_field(w, NULL, NULL, NULL);
        CHECK(fld);
        memcpy(out->field[f], fld, sizeof out->field[f]);
    }
    CHECK(em_gs_world_read(w, 0, 8, 512, 224, out->buf[0]) == 0);
    CHECK(em_gs_world_read(w, 0x38, 8, 512, 224, out->buf[1]) == 0);
    CHECK(em_gs_world_read(w, 0x70, 8, 512, 224, out->buf[2]) == 0);
    CHECK(em_gs_world_read(w, 0x12C, 2, 128, 128, out->buf[3]) == 0);
    CHECK(em_gs_world_read(w, 0x12C, 4, 256, 256, out->buf[4]) == 0);
    em_gs_world_destroy(w);
}

/* The refusals: a recorded transfer, an IMAGE packet, a texture outside
 * the uploads, a textured primitive whose TEX0 the recorder does not know
 * (written before the environment again), and a texture outside the uploads
 * whose TEX0 only a state block wrote each latch the fault; the resident
 * texture through a state block's TEX0 does not. */
static void refusals(void)
{
    setenv("EM_GS_THREADS", "2", 1);
    for (int k = 0; k < 6; ++k) {
        EmGsWorld *w = em_gs_world_create();
        CHECK(w && em_gs_world_memory_load(w, image_path) == 0);
        em_gs_world_begin(w);
        if (k == 0) {
            em_gs_world_write(w, EM_GS_TRXDIR, 0);
        } else if (k == 1) {
            uint8_t img[32] = { 0 };
            const uint64_t tag = 1u | 0x8000u | (UINT64_C(2) << 58);   /* IMAGE, NLOOP 1 */
            memcpy(img, &tag, 8);
            em_gs_world_gif(w, img, sizeof img);
        } else if (k >= 3) {
            /* k 3: TEX0 written, then the environment again; k 4 / 5: TEX0
             * (outside the uploads / resident) only in a state block */
            if (k == 3) {
                em_gs_world_write(w, EM_GS_TEX0_1, TEX_RESIDENT);
                em_gs_world_env_again(w);
            } else {
                uint8_t blk[64];
                const uint64_t pairs[2][2] = { { EM_GS_TEX0_1, k == 4 ? 0x3000ull | 1ull << 14 | 6ull << 26 | 6ull << 30
                                                                      : TEX_RESIDENT },
                                               { EM_GS_CLAMP_1, 0 } };
                em_gs_world_gif(w, blk, ad_packet(blk, pairs, 2));
            }
            EmGfxGsPrim p = tri(0x03C, 0, vtx(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1), vtx(9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1),
                                vtx(0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 1));
            p.set &= ~(unsigned)(EM_GFX_GS_TEX0 | EM_GFX_GS_CLAMP);
            em_gs_world_prims(w, &p, NULL, 1);
            CHECK((em_gs_world_fault(w) != NULL) == (k != 5));
            em_gs_world_destroy(w);
            continue;
        } else {
            EmGfxGsPrim p = tri(0x03C, 0x3000ull | 1ull << 14 | 6ull << 26 | 6ull << 30, vtx(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1),
                                vtx(9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1), vtx(0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 1));
            em_gs_world_prims(w, &p, NULL, 1);
        }
        CHECK(em_gs_world_fault(w) != NULL);
        em_gs_world_destroy(w);
    }
}

/* The field declaration's refusals: outside a recording, a second other
 * field in one frame, and a kick whose head draws another field (the
 * declared one is checked against the head); the same declaration with the
 * head's field kicks. */
/* The parsed list's texture-cache stamps (em_gs_raster.h EmGsList), with
 * the bands drawn in a fixed order by two drawing EmGs (W0 the rows < 64,
 * W1 the rest), as two em_gs_world workers may take them: the 128 x 128
 * target drawn (span 0); field rows 0..63 sampling its rows 64..127 (span
 * 1: W0 decodes them); the target's rows 72..127 drawn again (span 2: only
 * W1 draws; W0's walk skips the span, whose pages it must still stamp, or,
 * when W0 does not walk it at all, em_gs_list_pass); the same rows sampled
 * again (span 3: W0 must decode them again). Both orders leave the memory
 * one EmGs leaves. */
static void stamp_writes(EmGs *g)
{
    const uint64_t tex = 0x2580ull | 2ull << 14 | 7ull << 26 | 7ull << 30 | 1ull << 34 | 1ull << 35;
    const uint64_t w[][2] = {
        { EM_GS_PRMODECONT, 1 }, { EM_GS_TEST_1, 0x30000 }, { EM_GS_ZBUF_1, 0x101000070ull },
        { EM_GS_XYOFFSET_1, 0 }, { EM_GS_COLCLAMP, 1 },
        /* span 0: the target, flat */
        { EM_GS_FRAME_1, 0x2012C }, { EM_GS_SCISSOR_1, 127ull << 16 | 127ull << 48 }, { EM_GS_PRIM, 6 },
        { EM_GS_RGBAQ, 0x80204060ull }, { EM_GS_XYZ2, 0 }, { EM_GS_XYZ2, (128u * 16) | (uint64_t)(128u * 16) << 16 },
        /* span 1: field rows 0..63 from the target's rows 64..127 (UV, a texel a pixel) */
        { EM_GS_FRAME_1, 8ull << 16 }, { EM_GS_SCISSOR_1, 511ull << 16 | 223ull << 48 },
        { EM_GS_TEX0_1, tex }, { EM_GS_TEX1_1, 0 }, { EM_GS_CLAMP_1, 0 }, { EM_GS_PRIM, 6 | 1 << 4 | 1 << 8 },
        { EM_GS_RGBAQ, 0x80808080ull }, { EM_GS_UV, 8 | (uint64_t)(64 * 16 + 8) << 16 }, { EM_GS_XYZ2, 0 },
        { EM_GS_UV, (128 * 16 + 8) | (uint64_t)(128 * 16 + 8) << 16 },
        { EM_GS_XYZ2, (128u * 16) | (uint64_t)(64u * 16) << 16 },
        /* span 2: the target's rows 72..127 (past W0's rows with the list's two-row margin), another colour */
        { EM_GS_FRAME_1, 0x2012C }, { EM_GS_SCISSOR_1, 127ull << 16 | 127ull << 48 }, { EM_GS_PRIM, 6 },
        { EM_GS_RGBAQ, 0x80F01020ull }, { EM_GS_XYZ2, (uint64_t)(72u * 16) << 16 },
        { EM_GS_XYZ2, (128u * 16) | (uint64_t)(128u * 16) << 16 },
        /* span 3: the same sample again, into field columns 200.. */
        { EM_GS_FRAME_1, 8ull << 16 }, { EM_GS_SCISSOR_1, 511ull << 16 | 223ull << 48 },
        { EM_GS_PRIM, 6 | 1 << 4 | 1 << 8 }, { EM_GS_RGBAQ, 0x80808080ull },
        { EM_GS_UV, 8 | (uint64_t)(64 * 16 + 8) << 16 }, { EM_GS_XYZ2, 200u * 16 },
        { EM_GS_UV, (128 * 16 + 8) | (uint64_t)(128 * 16 + 8) << 16 },
        { EM_GS_XYZ2, (328u * 16) | (uint64_t)(64u * 16) << 16 },
    };
    for (unsigned i = 0; i < sizeof w / sizeof w[0]; ++i) em_gs_write(g, (unsigned)w[i][0], w[i][1]);
    em_gs_flush(g);
}

static void list_stamps(void)
{
    uint8_t *ref = calloc(1, EM_GS_MEM_BYTES);
    CHECK(ref);
    EmGs one;
    em_gs_init(&one, ref);
    one.strict = 1;
    stamp_writes(&one);
    CHECK(!one.refusals && !one.span_faults);
    em_gs_release(&one);
    for (int skip_walk = 0; skip_walk < 2; ++skip_walk) {
        uint8_t *mem = calloc(1, EM_GS_MEM_BYTES);
        CHECK(mem);
        EmGs parser, b[2];
        em_gs_init(&parser, mem);
        parser.strict = 1;
        parser.tc_off = 1;
        EmGsList *l = em_gs_list_create();
        CHECK(l);
        parser.list = l;
        stamp_writes(&parser);
        parser.list = NULL;
        CHECK(!parser.refusals && em_gs_list_spans(l) == 4);
        for (int k = 0; k < 2; ++k) {
            em_gs_init(&b[k], mem);
            b[k].strict = 1;
            b[k].band_rows = 1;
            b[k].band_y0 = k ? 64 : INT32_MIN;
            b[k].band_y1 = k ? INT32_MAX : 63;
        }
        for (uint32_t s = 0; s < 4; ++s) {
            if (s == 2) {
                (void)em_gs_list_draw(&b[1], l, 2, 3, NULL, NULL);
                if (skip_walk) em_gs_list_pass(&b[0], l, 2, 3);
                else (void)em_gs_list_draw(&b[0], l, 2, 3, NULL, NULL);
                continue;
            }
            (void)em_gs_list_draw(&b[0], l, s, s + 1, NULL, NULL);
            (void)em_gs_list_draw(&b[1], l, s, s + 1, NULL, NULL);
        }
        CHECK(memcmp(mem, ref, EM_GS_MEM_BYTES) == 0);
        /* (the second sample did read the new rows: a field pixel of span 3) */
        CHECK(em_gs_read_pixel(&b[0], 0, 8, EM_GS_PSMCT32, 210, 10) == 0x80F01020u);
        for (int k = 0; k < 2; ++k) em_gs_release(&b[k]);
        em_gs_release(&parser);
        em_gs_list_destroy(l);
        free(mem);
    }
    free(ref);
}

static void field_refusals(void)
{
    setenv("EM_GS_THREADS", "2", 1);
    uint8_t env[512], clr[512];
    size_t env_n, clr_n;
    for (int k = 0; k < 4; ++k) {
        EmGsWorld *w = em_gs_world_create();
        CHECK(w && em_gs_world_memory_load(w, image_path) == 0);
        if (k == 0) {
            em_gs_world_field_declare(w, FRAME_FIELD(0x38), 224);
            CHECK(em_gs_world_fault(w) != NULL);
        } else {
            em_gs_world_begin(w);
            em_gs_world_field_declare(w, FRAME_FIELD(0x38), 224);
            if (k == 1) em_gs_world_field_declare(w, FRAME_FIELD(0), 224);
            CHECK((em_gs_world_fault(w) != NULL) == (k == 1));
            if (k > 1) {
                head(env, &env_n, clr, &clr_n, k == 2 ? 0 : 0x38, 0);
                CHECK((em_gs_world_kick(w, env, env_n, clr, clr_n) == 0) == (k == 3));
                CHECK((em_gs_world_wait(w) == 0) == (k == 3));
            }
        }
        em_gs_world_destroy(w);
    }
}

/* em_gs_head_probe (the presenter's frames that draw nothing, GS_EXACT.md
 * section 11): the field a head names (FRAME_1, SCISSOR_1's rows, the half
 * line's XYOFFSET_1), and whether its clear writes the field's colour and
 * leaves it black: the Z-only clear (head(): ATST NEVER, AFAIL ZB_ONLY)
 * writes no colour, a black sprite clear writes black, a grey one writes
 * colour that is not black; a transfer register is refused. */
static void head_probe(void)
{
    uint8_t env[512], clr[512], *scratch = malloc(EM_GS_MEM_BYTES);
    size_t env_n, clr_n;
    CHECK(scratch);
    EmGsHeadProbe hp;
    head(env, &env_n, clr, &clr_n, 0x38, 1);
    CHECK(em_gs_head_probe(scratch, env, env_n, clr, clr_n, 0, &hp) == 0);
    CHECK(hp.frame == FRAME_FIELD(0x38) && hp.height == 224 && !hp.writes && !hp.black);
    CHECK(hp.xyoffset == ((uint64_t)(1792 * 16) | (uint64_t)(1936 * 16 + 8) << 32));
    CHECK(em_gs_head_probe(scratch, env, env_n, clr, clr_n, 1, &hp) == 0);
    CHECK(hp.frame == FRAME_FIELD(0x38) && !hp.writes && !hp.black);
    for (int grey = 0; grey < 2; ++grey) {
        const uint64_t c[][2] = {
            { EM_GS_TEST_1, 0x30000 }, { EM_GS_PRIM, 6 },
            { EM_GS_RGBAQ, (grey ? 0x40404040ull : 0x80000000ull) | 0x3F80000000000000ull },
            { EM_GS_XYZ2, (1792 * 16) | (uint64_t)(1936 * 16) << 16 },
            { EM_GS_XYZ2, (2304 * 16) | (uint64_t)(2160 * 16) << 16 }, { EM_GS_TEST_1, 0x50000 },
        };
        const size_t n = ad_packet(clr, c, sizeof c / sizeof c[0]);
        CHECK(em_gs_head_probe(scratch, env, env_n, clr, n, 1, &hp) == 0);
        CHECK(hp.writes && hp.black == !grey);
    }
    const uint64_t t[][2] = { { EM_GS_TRXDIR, 0 } };
    const size_t n = ad_packet(clr, t, 1);
    CHECK(em_gs_head_probe(scratch, env, env_n, clr, n, 1, &hp) < 0);
    free(scratch);
}

/* ---- the recorder's barriers and the parse's CLUT loads, frame by frame
 * (small world frames over every block uploaded, image_all) ---- */

#define FIELD_OFS ((uint64_t)(1792 * 16) | (uint64_t)(1936 * 16) << 32)
#define TARGET_OFS 0x7C0000007C00ull           /* the 128 x 128 target's: (1984, 1984) */
#define FRAME_Y (0x38ull | 8ull << 16)         /* a second 512 x 224 buffer (field B's) */

/* A CT32 buffer as a DECAL texture with TCC, its rows minv..maxv (REGION_CLAMP). */
static uint64_t buf_tex(uint32_t fbp, uint32_t fbw, uint32_t tw, uint32_t th)
{
    return (uint64_t)fbp * 32u | (uint64_t)fbw << 14 | (uint64_t)tw << 26 | (uint64_t)th << 30 | 1ull << 34 |
           1ull << 35;
}
static uint64_t region(uint32_t maxu, uint32_t minv, uint32_t maxv)
{
    return 2u | 2u << 2 | (uint64_t)maxu << 14 | (uint64_t)minv << 24 | (uint64_t)maxv << 34;
}

/* A sprite over window pixels [x0, x1) x [y0, y1) under the offset (ox, oy):
 * flat, or (tex0 != 0) UV-textured one texel a pixel from texel (u0, v0),
 * nearest; TEST_1 `test` (ZTE, ALWAYS; the caller alternates AREF so the
 * write ends the span before it). */
static EmGfxGsPrim sprite(int ox, int oy, int x0, int y0, int x1, int y1, uint32_t rgba, uint64_t tex0,
                          uint64_t clamp, int u0, int v0, uint64_t test)
{
    EmGfxGsPrim p;
    memset(&p, 0, sizeof p);
    p.prim = 6u | (tex0 ? 1u << 4 | 1u << 8 : 0u);
    p.set = EM_GFX_GS_TEST | EM_GFX_GS_COLCLAMP | (tex0 ? EM_GFX_GS_TEX0 | EM_GFX_GS_TEX1 | EM_GFX_GS_CLAMP : 0u);
    p.tex0 = tex0;
    p.tex1 = 0;
    p.clamp = clamp;
    p.test = test;
    p.colclamp = 1;
    p.count = 2;
    const int xs[2] = { x0, x1 }, ys[2] = { y0, y1 };
    for (unsigned k = 0; k < 2; ++k) {
        p.v[k].x = (uint16_t)((ox + xs[k]) * 16);
        p.v[k].y = (uint16_t)((oy + ys[k]) * 16);
        memcpy(p.v[k].rgba, &rgba, 4);
        p.v[k].q = 0x3F800000u;
        p.v[k].u = (uint16_t)((u0 + xs[k] - x0) * 16 + 8);
        p.v[k].v = (uint16_t)((v0 + ys[k] - y0) * 16 + 8);
    }
    return p;
}

/* The environment of a draw into a CT32 buffer (FRAME_1 with SCISSOR_1: the
 * recorder marks it drawn; Z masked). */
static EmGfxGsEnv buf_env(uint64_t frame, uint32_t width, uint32_t height, uint64_t xyoffset)
{
    EmGfxGsEnv e;
    memset(&e, 0, sizeof e);
    e.set = EM_GFX_GS_ENV_FRAME | EM_GFX_GS_ENV_ZBUF | EM_GFX_GS_ENV_XYOFFSET | EM_GFX_GS_ENV_SCISSOR |
            EM_GFX_GS_ENV_PRMODECONT;
    e.frame = frame;
    e.zbuf = 0x101000070ull;
    e.xyoffset = xyoffset;
    e.scissor = (uint64_t)(width - 1u) << 16 | (uint64_t)(height - 1u) << 48;
    e.prmodecont = 1;
    return e;
}

#define C_TARGET 0x80406020u
#define C_LATE 0x80F01020u

/* A barrier for a texture read, then a later read of the drawing target in
 * the same stretch (em_gs_world.c texture_read: the target's blocks are
 * drawn ones again after the barrier). The 128 x 128 target drawn; buffer Y
 * (FBP 0x38) drawn from it (a barrier: every band of the target first); Y's
 * rows 150..223 drawn flat; the field's rows 0..40 drawn from Y's rows
 * 150..190, which needs a barrier again: Y was drawn since the last one.
 * Without it the field's rows read Y before Y's rows 150.. are drawn
 * whenever a band of the field's rows comes before the band of Y's (one
 * worker drawing the list: always). */
static void body_remark(EmGsWorld *w, unsigned f)
{
    (void)f;
    EmGfxGsPrim p;
    EmGfxGsEnv e = buf_env(FRAME_TARGET, 128, 128, TARGET_OFS);
    p = sprite(1984, 1984, 0, 0, 128, 128, C_TARGET, 0, 0, 0, 0, 0x30000);
    em_gs_world_prims(w, &p, &e, 1);
    e = buf_env(FRAME_Y, 512, 224, FIELD_OFS);
    p = sprite(1792, 1936, 0, 0, 128, 64, 0, TEX_TARGET | 1ull << 35, region(127, 0, 63), 0, 0, 0x30010);
    em_gs_world_prims(w, &p, &e, 1);
    p = sprite(1792, 1936, 0, 150, 512, 224, C_LATE, 0, 0, 0, 0, 0x30000);
    em_gs_world_prims(w, &p, NULL, 1);
    e = buf_env(FRAME_FIELD(0), 512, 224, FIELD_OFS);
    p = sprite(1792, 1936, 0, 0, 512, 41, 0, buf_tex(0x38, 8, 9, 8), region(511, 150, 223), 0, 150, 0x30010);
    em_gs_world_prims(w, &p, &e, 1);
}

/* A barrier for a read of the declared field before the frame's first
 * FRAME_1 write (em_gs_world_field_declare: the drawing target is the
 * head's, the field). The field declared; through the head's FRAME_1 the
 * field's rows 100..120 drawn from its rows 0..63 (a barrier: the field is
 * a drawn buffer), its rows 150..223 drawn flat, and its rows 0..40 drawn
 * from its rows 150..190, which needs a barrier again: the field was drawn
 * since (and its rows 0..63 read). */
static void body_declared(EmGsWorld *w, unsigned f)
{
    (void)f;
    em_gs_world_field_declare(w, FRAME_FIELD(0), 224);
    EmGfxGsPrim p = sprite(1792, 1936, 0, 100, 512, 121, 0, buf_tex(0, 8, 9, 8), region(511, 0, 63), 0, 0, 0x30010);
    em_gs_world_prims(w, &p, NULL, 1);
    p = sprite(1792, 1936, 0, 150, 512, 224, C_LATE, 0, 0, 0, 0, 0x30000);
    em_gs_world_prims(w, &p, NULL, 1);
    p = sprite(1792, 1936, 0, 0, 512, 41, 0, buf_tex(0, 8, 9, 8), region(511, 150, 223), 0, 150, 0x30010);
    em_gs_world_prims(w, &p, NULL, 1);
}

/* A read of uploaded memory no draw has touched, then its first draw in
 * the same stretch (em_gs_world.c em_gs_world_drawn_buffer: a barrier
 * once a texture was read since the last one). The field's rows 150..213
 * drawn from the uploaded 128 x 128 buffer U (FBP 0x180) never drawn
 * before, then U's rows 0..63 drawn flat: every band must read U first.
 * Without the barrier the band of U's rows, above the field's, draws U
 * before the field's band reads it whenever it comes first (one worker
 * drawing the list: always). */
#define FRAME_U (0x180ull | 2ull << 16)
static void body_upload(EmGsWorld *w, unsigned f)
{
    (void)f;
    EmGfxGsEnv e = buf_env(FRAME_FIELD(0), 512, 224, FIELD_OFS);
    EmGfxGsPrim p = sprite(1792, 1936, 0, 150, 128, 214, 0, buf_tex(0x180, 2, 7, 7), region(127, 0, 63), 0, 0, 0x30010);
    em_gs_world_prims(w, &p, &e, 1);
    e = buf_env(FRAME_U, 128, 128, TARGET_OFS);
    p = sprite(1984, 1984, 0, 0, 128, 64, C_LATE, 0, 0, 0, 0, 0x30000);
    em_gs_world_prims(w, &p, &e, 1);
}

/* CLUT loads from memory the frame itself draws (em_gs_world.c own_drain):
 * the 128 x 128 target drawn and read into the field (a barrier), the
 * field's rows 32..47, columns 0..15 (block 0x100: the CLUT of
 * TEX_T8_PREV) drawn in four colours, T8 triangles over the resident texels
 * through that CLUT (CLD 1) into the field's rows 100.., the CLUT's pixels
 * drawn again in four other colours, and the CLUT loaded again (CSA
 * changes TEX0; a T8 CT32 load reads the same entries) for more T8
 * triangles. Each load reads what one worker drawing directly reads: the
 * frame's spans before it drawn. A blended sprite before each load would
 * show a span drawn twice. */
static uint32_t own_colour(unsigned f, unsigned pass, unsigned k)
{
    return 0x80000000u | (0x30u + 0x40u * k) | (0x20u + 0x50u * pass + 0x10u * f) << 8 | (0xC0u - 0x28u * k) << 16;
}
static void body_own_clut(EmGsWorld *w, unsigned f)
{
    for (unsigned pass = 0; pass < 2; ++pass) {
        /* the 128 x 128 target drawn, and read into the field's rows
         * 192..223 (a barrier: the load below has two segments to draw) */
        EmGfxGsEnv e = buf_env(FRAME_TARGET, 128, 128, TARGET_OFS);
        EmGfxGsPrim t0 = sprite(1984, 1984, 0, 0, 128, 128, own_colour(f, pass, 0) ^ 0x00FFFF00u, 0, 0, 0, 0, 0x30000);
        em_gs_world_prims(w, &t0, &e, 1);
        e = buf_env(FRAME_FIELD(0), 512, 224, FIELD_OFS);
        t0 = sprite(1792, 1936, 0, 192, 128, 224, 0, TEX_TARGET | 1ull << 35, region(127, 0, 127), 0, 32 * (int)pass,
                    0x30010);
        em_gs_world_prims(w, &t0, &e, 1);
        for (unsigned k = 0; k < 4; ++k) {
            EmGfxGsPrim p = sprite(1792, 1936, 0, 32 + 4 * (int)k, 16, 36 + 4 * (int)k, own_colour(f, pass, k), 0, 0,
                                   0, 0, k & 1 ? 0x30010 : 0x30000);
            em_gs_world_prims(w, &p, k ? NULL : &e, 1);
        }
        /* a blended sprite over rows 60..89 (drawn twice, it would show) */
        EmGfxGsPrim b = sprite(1792, 1936, 0, 60, 512, 90, 0x40806040u + 0x2000u * pass + 0x10u * f, 0, 0, 0, 0,
                               0x30010);
        b.prim |= 1u << 6;
        b.set |= EM_GFX_GS_ALPHA;
        b.alpha = 0x44;
        em_gs_world_prims(w, &b, NULL, 1);
        for (unsigned i = 0; i < 4; ++i) {
            const int x = (int)((i * 97u + f * 31u + pass * 200u) % 400u), y = 100 + (int)((i * 23u) % 60u);
            EmGfxGsPrim t = tri(0x01C, TEX_T8_PREV | (uint64_t)pass << 56,
                                vtx(x, y, 3000, 200, 128, 128, 128, 128, 0.0f, 0.0f, 1.0f),
                                vtx(x + 90, y + 9, 3000, 200, 128, 128, 128, 128, 1.0f, 0.2f, 1.0f),
                                vtx(x + 13, y + 60, 3000, 200, 128, 128, 128, 128, 0.2f, 1.0f, 1.0f));
            t.test = 0x30000;
            t.tex1 = 0;                /* (nearest, no fog: the palette's colours as drawn) */
            em_gs_world_prims(w, &t, NULL, 1);
        }
    }
}

/* EM_GS_LIST_COUNTERS per frame: segments, the parse's CLUT loads from its
 * own spans, its waits for the frame before, the early parse's ops. */
typedef struct { unsigned frames; unsigned long long segs[4], own[4], prior[4], early[4]; } ListCounts;
static void list_counts(const char *path, ListCounts *c)
{
    memset(c, 0, sizeof *c);
    FILE *fp = fopen(path, "r");
    CHECK(fp);
    char line[1024];
    while (fgets(line, sizeof line, fp)) {
        if (line[0] == '#') continue;
        unsigned long long v[32];
        unsigned n = 0;
        for (char *t = strtok(line, " \n"); t && n < 32; t = strtok(NULL, " \n")) v[n++] = strtoull(t, NULL, 10);
        CHECK(n == 22 && c->frames < 4);
        c->segs[c->frames] = v[3];
        c->own[c->frames] = v[19];
        c->prior[c->frames] = v[20];
        c->early[c->frames] = v[21];
        c->frames++;
    }
    fclose(fp);
}

enum { SM_DIRECT, SM_LIST, SM_THREADS };

/* `frames` world frames of `body` (each its head: the field FBP 0, the half
 * line on odd frames), with `workers` workers: SM_DIRECT one worker drawing
 * directly, SM_LIST one worker drawing the parsed list (EM_GS_LIST=1: its
 * bands one after another, top first), SM_THREADS the parsed list on
 * `workers` threads; `piped`: each next frame recorded and parsed early
 * while the one before is drawn (em_gs_world_preparse). The local memory
 * after the last frame into `mem`; the counters into `lc` (SM_DIRECT: none);
 * each frame's pixels drawn (EmGsWorldStats) into `pixels`. */
static void run_small(void (*body)(EmGsWorld *, unsigned), unsigned frames, int mode, unsigned workers, int piped,
                      uint8_t *mem, ListCounts *lc, unsigned long long *pixels)
{
    char n[8];
    snprintf(n, sizeof n, "%u", mode == SM_THREADS ? workers : 1u);
    setenv("EM_GS_THREADS", n, 1);
    if (mode == SM_LIST) setenv("EM_GS_LIST", "1", 1);
    const char *const lcp = "build/gs_world_test/small_counters.txt";
    if (mode != SM_DIRECT) setenv("EM_GS_LIST_COUNTERS", lcp, 1);
    EmGsWorld *w = em_gs_world_create();
    unsetenv("EM_GS_LIST");
    unsetenv("EM_GS_LIST_COUNTERS");
    CHECK(w && em_gs_world_workers(w) == (mode == SM_THREADS ? workers : 1u));
    CHECK(em_gs_world_memory_load(w, image_all) == 0);
    uint8_t env[512], clr[512];
    size_t env_n, clr_n;
    for (unsigned f = 0; f < frames; ++f) {
        em_gs_world_begin(w);
        body(w, f);
        CHECK(!em_gs_world_fault(w));
        head(env, &env_n, clr, &clr_n, 0, (int)(f & 1u));
        if (piped) CHECK(em_gs_world_preparse(w, env, env_n, clr, clr_n) == 0);
        CHECK(em_gs_world_kick(w, env, env_n, clr, clr_n) == 0);
        if (!piped) CHECK(em_gs_world_wait(w) == 0);
        /* the frame waited for last: this one, or (piped) the one before */
        EmGsWorldStats st;
        em_gs_world_stats(w, &st);
        if (!piped) pixels[f] = st.pixels;
        else if (f) pixels[f - 1] = st.pixels;
    }
    CHECK(em_gs_world_wait(w) == 0);
    EmGsWorldStats st;
    em_gs_world_stats(w, &st);
    pixels[frames - 1] = st.pixels;
    const uint8_t *m = em_gs_world_memory(w);
    CHECK(m);
    memcpy(mem, m, EM_GS_MEM_BYTES);
    em_gs_world_destroy(w);
    if (lc) {
        if (mode == SM_DIRECT) memset(lc, 0, sizeof *lc);
        else list_counts(lcp, lc);
    }
}

/* A CT32 pixel of a buffer in a local memory image. */
static uint32_t mem_pixel(const uint8_t *mem, uint32_t fbp, uint32_t fbw, uint32_t x, uint32_t y)
{
    uint32_t v;
    memcpy(&v, mem + (size_t)em_gs_addr32(fbp * 32u, fbw, x, y, 0) * 4u, 4);
    return v;
}

/* The barrier cases (body_remark, body_declared, body_upload): one worker
 * drawing the list, 3 and (full) 8 workers draw what one worker draws
 * directly, in three segments (the head's; the first read's; the second
 * read's), or two for body_upload (the head's and the read's; U's draw). */
static unsigned g_barrier_px[3];
static void barrier_cases(int full)
{
    uint8_t *ref = malloc(EM_GS_MEM_BYTES), *got = malloc(EM_GS_MEM_BYTES);
    CHECK(ref && got);
    for (int c = 0; c < 3; ++c) {
        void (*body)(EmGsWorld *, unsigned) = c == 2 ? body_upload : c ? body_declared : body_remark;
        unsigned long long px_ref[1], px[1];
        run_small(body, 1, SM_DIRECT, 1, 0, ref, NULL, px_ref);
        unsigned late = 0;
        if (c < 2) {
            /* the second read took the late rows: the field's rows 0..40
             * hold C_LATE, read from rows 150..190 (of Y, or of the field) */
            for (uint32_t y = 0; y <= 40; ++y)
                for (uint32_t x = 0; x < 512; ++x) late += mem_pixel(ref, 0, 8, x, y) == C_LATE;
            CHECK(late == 512u * 41u);
        } else {
            /* the field's rows 150..213 took U as uploaded (zero), and U's
             * rows 0..63 hold C_LATE */
            for (uint32_t y = 0; y < 64; ++y)
                for (uint32_t x = 0; x < 128; ++x) {
                    late += mem_pixel(ref, 0x180, 2, x, y) == C_LATE;
                    CHECK(mem_pixel(ref, 0, 8, x, 150 + y) == 0);
                }
            CHECK(late == 128u * 64u);
        }
        g_barrier_px[c] = late;
        static const int modes[3] = { SM_LIST, SM_THREADS, SM_THREADS };
        static const unsigned wk[3] = { 1, 3, 8 };
        for (unsigned i = 0; i < (full ? 3u : 2u); ++i) {
            ListCounts lc;
            run_small(body, 1, modes[i], wk[i], 0, got, &lc, px);
            CHECK(memcmp(got, ref, EM_GS_MEM_BYTES) == 0 && px[0] == px_ref[0]);
            CHECK(lc.frames == 1 && lc.segs[0] == (c == 2 ? 2u : 3u) && lc.own[0] == 0);
        }
    }
    free(ref);
    free(got);
}

/* The CLUT loads from the frame's own spans (body_own_clut, two frames, two
 * loads each): one worker drawing the list (its parse draws the spans so far
 * itself), 3 workers (the parsing worker waits for the others), the frames
 * parsed early whole (EM_GS_EARLY_OPS past the frame: the game's thread
 * waits for the frame before, then draws the spans so far itself) with 3
 * workers and one worker drawing the list, the early parse handing over
 * after 37 ops (8 workers), and (full) 8 workers, 8 bands a worker and the
 * early parse off: each the memory and the per-frame pixel counts one
 * worker drawing directly leaves, the loads counted. */
static unsigned g_own_px, g_own_waits;
static void own_clut_cases(int full)
{
    uint8_t *ref = malloc(EM_GS_MEM_BYTES), *got = malloc(EM_GS_MEM_BYTES);
    CHECK(ref && got);
    unsigned long long px_ref[2], pxs[2];
    run_small(body_own_clut, 2, SM_DIRECT, 1, 0, ref, NULL, px_ref);
    /* the T8 triangles took the second frame's second palette: its colours
     * are in the field's rows 100.. */
    unsigned px = 0;
    for (uint32_t y = 100; y < 224; ++y)
        for (uint32_t x = 0; x < 512; ++x) {
            const uint32_t v = mem_pixel(ref, 0, 8, x, y);
            for (unsigned k = 0; k < 4; ++k) px += (v & 0xFFFFFFu) == (own_colour(1, 1, k) & 0xFFFFFFu);
        }
    CHECK(px > 1000);
    g_own_px = px;
    static const struct { int mode; unsigned workers; int piped; const char *ops, *bands, *early; } m[7] = {
        { SM_LIST, 1, 0, NULL, NULL, NULL },      { SM_THREADS, 3, 0, NULL, NULL, NULL },
        { SM_THREADS, 3, 1, "100000", NULL, NULL }, { SM_LIST, 1, 1, "100000", NULL, NULL },
        { SM_THREADS, 8, 1, "37", NULL, NULL },   { SM_THREADS, 8, 1, NULL, "8", NULL },
        { SM_THREADS, 8, 1, NULL, NULL, "0" },
    };
    for (unsigned i = 0; i < (full ? 7u : 5u); ++i) {
        if (m[i].ops) setenv("EM_GS_EARLY_OPS", m[i].ops, 1);
        if (m[i].bands) setenv("EM_GS_BANDS", m[i].bands, 1);
        if (m[i].early) setenv("EM_GS_EARLY_PARSE", m[i].early, 1);
        ListCounts lc;
        run_small(body_own_clut, 2, m[i].mode, m[i].workers, m[i].piped, got, &lc, pxs);
        unsetenv("EM_GS_EARLY_OPS");
        unsetenv("EM_GS_BANDS");
        unsetenv("EM_GS_EARLY_PARSE");
        CHECK(memcmp(got, ref, EM_GS_MEM_BYTES) == 0);
        /* (each frame's pixels are its own, also those an early parse drew) */
        CHECK(pxs[0] == px_ref[0] && pxs[1] == px_ref[1]);
        CHECK(lc.frames == 2 && lc.own[0] == 2 && lc.own[1] == 2);
        g_own_waits += (unsigned)(lc.own[0] + lc.own[1]);
        /* parsed early whole: the second frame's first load waited for the
         * first frame before its thread drew the spans so far */
        if (m[i].ops && m[i].ops[0] == '1')
            CHECK(lc.early[0] > 0 && lc.early[1] > 0 && (m[i].mode != SM_THREADS || lc.prior[1] >= 1));
    }
    free(ref);
    free(got);
}

/* GIF frames (em_gs_world_gif_frame, the status pages' frames): a world
 * frame, then a GIF frame whose stream is a head (the field 0x38, a half
 * line), a clear, a host-to-local transfer of a 16 x 16 PSMCT32 texture to
 * block 0x2800 (no upload wrote it) and bilinear UV sprites from it and
 * from the resident texture, then another world frame. The GIF frame's
 * field is the stream run on one EmGs over the memory before it, with its
 * XYOFFSET; 1, 2, 3 and 8 workers leave the same fields and memory; its
 * transfer makes block 0x2800 resident (a later GIF frame reads it); a
 * sprite reading unwritten memory, and a GIF frame inside a recording,
 * fault. */
static size_t gif_stream(uint8_t *out, uint32_t tbp, int transfer)
{
    size_t n = 0;
    const uint64_t envw[][2] = {
        { EM_GS_FRAME_1, FRAME_FIELD(0x38) }, { EM_GS_ZBUF_1, 0x101000070ull },
        { EM_GS_XYOFFSET_1, (uint64_t)(1792 * 16) | (uint64_t)(1936 * 16 + 8) << 32 },
        { EM_GS_SCISSOR_1, 511ull << 16 | 223ull << 48 }, { EM_GS_PRMODECONT, 1 }, { EM_GS_COLCLAMP, 1 },
        { EM_GS_DTHE, 0 }, { EM_GS_TEST_1, 0x30000 }, { EM_GS_PRIM, 0x6 },
        { EM_GS_RGBAQ, 0x80102030 }, { EM_GS_XYZ2, (uint64_t)(1792 * 16) | (uint64_t)(1936 * 16) << 16 },
        { EM_GS_XYZ2, (uint64_t)((1792 + 512) * 16) | (uint64_t)((1936 + 224) * 16) << 16 },
    };
    n += ad_packet(out + n, envw, sizeof envw / sizeof envw[0]);
    if (transfer) {
        const uint64_t trx[][2] = {
            { EM_GS_BITBLTBUF, (uint64_t)tbp << 32 | 1ull << 48 }, { EM_GS_TRXPOS, 0 },
            { EM_GS_TRXREG, 16 | 16ull << 32 }, { EM_GS_TRXDIR, 0 },
        };
        n += ad_packet(out + n, trx, 4);
        const uint64_t img[2] = { 64u | 0x8000u | (2ull << 58), 0 };   /* IMAGE, 64 quadwords */
        memcpy(out + n, img, 16);
        n += 16;
        for (uint32_t i = 0; i < 256; ++i) {
            const uint32_t px = (i * 37u) | (i * 11u) << 8 | (255u - i) << 16 | 0x80u << 24;
            memcpy(out + n + 4u * i, &px, 4);
        }
        n += 1024;
    }
    for (unsigned k = 0; k < 6; ++k) {
        const uint64_t tex0 = k & 1 ? TEX_RESIDENT : ((uint64_t)tbp | 1ull << 14 | 4ull << 26 | 4ull << 30 | 1ull << 34);
        const int x0 = 13 + 70 * (int)k, y1 = 200 - 9 * (int)k;
        const uint64_t spr[][2] = {
            { EM_GS_TEX0_1, tex0 }, { EM_GS_TEX1_1, 0x60 }, { EM_GS_CLAMP_1, 0 }, { EM_GS_PRIM, 0x116 },
            { EM_GS_RGBAQ, 0x80808080 }, { EM_GS_UV, 3u | 5u << 16 },
            { EM_GS_XYZ2, (uint64_t)((1792 + x0) * 16 + 5) | (uint64_t)((1936 + y1) * 16 + 3) << 16 },
            { EM_GS_UV, (uint64_t)(16u * 15u + 7u) | (uint64_t)(16u * 13u + 1u) << 16 },
            { EM_GS_XYZ2, (uint64_t)((1792 + x0 + 61) * 16 + 9) | (uint64_t)((1936 + 3 + (int)k) * 16 + 11) << 16 },
        };
        n += ad_packet(out + n, spr, sizeof spr / sizeof spr[0]);
    }
    return n;
}

static unsigned g_gif_px;
static void gif_run(unsigned workers, uint8_t *fields /* 3 fields */, uint8_t *mem_out)
{
    char nb[8];
    snprintf(nb, sizeof nb, "%u", workers);
    setenv("EM_GS_THREADS", nb, 1);
    EmGsWorld *w = em_gs_world_create();
    CHECK(w && em_gs_world_workers(w) == workers);
    CHECK(em_gs_world_memory_load(w, image_one) == 0);
    uint8_t env[512], clr[512];
    size_t env_n, clr_n;
    static uint8_t gif[8192];
    record_world(w, 21u, 0, 0);
    head(env, &env_n, clr, &clr_n, 0, 0);
    CHECK(em_gs_world_kick(w, env, env_n, clr, clr_n) == 0);
    CHECK(em_gs_world_wait(w) == 0);
    memcpy(fields, em_gs_world_field(w, NULL, NULL, NULL), 512 * 224 * 4);
    /* the memory the GIF frame starts from, and the stream on one EmGs */
    uint8_t *ref = malloc(EM_GS_MEM_BYTES);
    CHECK(ref);
    memcpy(ref, em_gs_world_memory(w), EM_GS_MEM_BYTES);
    const size_t n = gif_stream(gif, 0x2800, 1);
    CHECK(!em_gs_world_resident(w, 0x2800));
    CHECK(em_gs_world_gif_frame(w, gif, n, FRAME_FIELD(0x38), 223ull << 48) == 0);
    CHECK(em_gs_world_wait(w) == 0);
    CHECK(em_gs_world_resident(w, 0x2800));
    uint64_t xy = 0;
    CHECK(em_gs_world_handed_xyoffset(w, &xy) == 1 && xy == ((uint64_t)(1792 * 16) | (uint64_t)(1936 * 16 + 8) << 32));
    uint32_t fw, fh;
    uint64_t frame;
    const uint8_t *fld = em_gs_world_field(w, &fw, &fh, &frame);
    CHECK(fld && fw == 512 && fh == 224 && (frame & 0x1FF) == 0x38 && em_gs_world_field_xyoffset_known(w));
    memcpy(fields + 512 * 224 * 4, fld, 512 * 224 * 4);
    EmGs g;
    em_gs_init(&g, ref);
    g.strict = 1;
    CHECK(em_gs_gif(&g, gif, n) == n);
    em_gs_flush(&g);
    CHECK(g.refusals == 0 && g.refused_prims == 0 && g.span_faults == 0);
    g_gif_px = 0;
    for (uint32_t y = 0; y < 224; ++y)
        for (uint32_t x = 0; x < 512; ++x) {
            uint32_t a;
            memcpy(&a, ref + 4u * em_gs_addr32(0x38 * 32, 8, x, y, 0), 4);
            uint32_t b;
            memcpy(&b, fld + 4u * (y * 512u + x), 4);
            CHECK(a == b);
            g_gif_px += (a & 0xFFFFFFu) != 0x102030u;   /* not the clear */
        }
    em_gs_release(&g);
    free(ref);
    /* a later GIF frame reads the transferred block without a transfer */
    const size_t n2 = gif_stream(gif, 0x2800, 0);
    CHECK(em_gs_world_gif_frame(w, gif, n2, FRAME_FIELD(0x38), 223ull << 48) == 0);
    /* then a world frame again */
    record_world(w, 22u, 0, 0);
    head(env, &env_n, clr, &clr_n, 0, 0);
    CHECK(em_gs_world_kick(w, env, env_n, clr, clr_n) == 0);
    CHECK(em_gs_world_wait(w) == 0);
    memcpy(fields + 2 * 512 * 224 * 4, em_gs_world_field(w, NULL, NULL, NULL), 512 * 224 * 4);
    memcpy(mem_out, em_gs_world_memory(w), EM_GS_MEM_BYTES);
    em_gs_world_destroy(w);
}

static void gif_frames(void)
{
    static uint8_t f[4][3 * 512 * 224 * 4];
    uint8_t *m[4];
    const unsigned counts[4] = { 1, 2, 3, 8 };
    for (unsigned i = 0; i < 4; ++i) {
        m[i] = malloc(EM_GS_MEM_BYTES);
        CHECK(m[i]);
        gif_run(counts[i], f[i], m[i]);
    }
    for (unsigned i = 1; i < 4; ++i) {
        CHECK(memcmp(f[i], f[0], sizeof f[0]) == 0);
        CHECK(memcmp(m[i], m[0], EM_GS_MEM_BYTES) == 0);
    }
    for (unsigned i = 0; i < 4; ++i) free(m[i]);
    CHECK(g_gif_px > 1000);
    /* the refusals: a sprite reading memory no upload wrote; a GIF frame in
     * a recording */
    setenv("EM_GS_THREADS", "1", 1);
    static uint8_t gif[8192];
    EmGsWorld *w = em_gs_world_create();
    CHECK(w && em_gs_world_memory_load(w, image_one) == 0);
    size_t n = gif_stream(gif, 0x3000, 0);
    CHECK(em_gs_world_gif_frame(w, gif, n, FRAME_FIELD(0x38), 223ull << 48) < 0);
    CHECK(strstr(em_gs_world_fault(w), "no upload wrote"));
    em_gs_world_destroy(w);
    w = em_gs_world_create();
    CHECK(w && em_gs_world_memory_load(w, image_one) == 0);
    em_gs_world_begin(w);
    n = gif_stream(gif, 0x2800, 1);
    CHECK(em_gs_world_gif_frame(w, gif, n, FRAME_FIELD(0x38), 223ull << 48) < 0);
    CHECK(strstr(em_gs_world_fault(w), "inside a recorded world frame"));
    em_gs_world_destroy(w);
}

int main(void)
{
    (void)system("mkdir -p build/gs_world_test");
    write_image();
    static Result r[6];
    const unsigned counts[4] = { 1, 2, 3, 8 };
    for (unsigned i = 0; i < 4; ++i) run(counts[i], &r[i]);
    g_with_pass = 0;                       /* the same frames without the pass */
    run(1, &r[4]);
    g_with_pass = 1;
    run(0, &r[5]);                         /* the host default */
    for (unsigned i = 1; i < 6; ++i) {
        if (i == 4) continue;
        for (unsigned f = 0; f < 3; ++f) CHECK(memcmp(r[i].field[f], r[0].field[f], sizeof r[0].field[f]) == 0);
        for (unsigned b = 0; b < 5; ++b) CHECK(memcmp(r[i].buf[b], r[0].buf[b], sizeof r[0].buf[b]) == 0);
    }
    /* the fields are not trivial: the scene, the target pass and the list
     * frame all wrote pixels of the field */
    unsigned lit = 0, target = 0, listed = 0;
    for (unsigned i = 0; i < 512 * 224; ++i) {
        lit += r[0].field[0][4 * i] != 0;
        listed += memcmp(r[0].field[2] + 4 * i, r[0].field[0] + 4 * i, 3) != 0;
    }
    /* (the target's memory is the copy's at the end of a frame with the
     * pass, as in the game: the target is checked without it) */
    for (unsigned i = 0; i < 128 * 128; ++i) target += r[4].buf[3][4 * i + 1] == 0xFF;
    CHECK(lit > 1000 && target > 500 && listed > 1000);
    /* the pass: the copy holds the field, and the blend changed the fields
     * where its Z test passed (and only RGB and alpha: the Z buffer is the
     * same as without the pass) */
    unsigned copied = 0, blended[2] = { 0, 0 };
    for (unsigned i = 0; i < 256 * 256; ++i) copied += r[0].buf[4][4 * i] != 0;
    for (unsigned f = 0; f < 2; ++f)
        for (unsigned i = 0; i < 512 * 224; ++i)
            blended[f] += memcmp(r[0].field[f] + 4 * i, r[4].field[f] + 4 * i, 3) != 0;
    CHECK(copied > 10000 && blended[0] > 1000 && blended[1] > 1000);
    CHECK(memcmp(r[0].buf[2], r[4].buf[2], sizeof r[0].buf[2]) == 0);
    /* every block uploaded (image_all): the reads of the buffers the frames
     * draw are ordered all the same (em_gs_world.c mark_drawn and
     * texture_entry list drawn blocks whatever their residency), so 1 and 8
     * workers draw what the frames over the texture alone drew */
    static Result ra[2];
    image_path = image_all;
    run(1, &ra[0]);
    run(8, &ra[1]);
    image_path = image_one;
    for (unsigned i = 0; i < 2; ++i) {
        for (unsigned f = 0; f < 3; ++f) CHECK(memcmp(ra[i].field[f], r[0].field[f], sizeof r[0].field[f]) == 0);
        for (unsigned b = 0; b < 5; ++b) CHECK(memcmp(ra[i].buf[b], r[0].buf[b], sizeof r[0].buf[b]) == 0);
    }
    /* past the texture table's end (MANY_KEYS): the same fields and memory
     * with 1 and 8 workers, and the counters (EM_GS_TEX_COUNTERS) show the
     * table full and the later textures checked again */
    static Result rk[2];
    unsigned long long cliff[2] = { 0, 0 };
    g_many_keys = 1;
    for (unsigned i = 0; i < 2; ++i) {
        setenv("EM_GS_TEX_COUNTERS", "build/gs_world_test/tex_counters.txt", 1);
        run(i ? 8 : 1, &rk[i]);
        unsetenv("EM_GS_TEX_COUNTERS");
        FILE *tc = fopen("build/gs_world_test/tex_counters.txt", "r");
        CHECK(tc);
        char line[256];
        unsigned frames = 0, entries_max = 0;
        while (fgets(line, sizeof line, tc)) {
            unsigned long long v[10];
            unsigned entries, pool;
            if (line[0] == '#') continue;
            CHECK(sscanf(line, "%llu %llu %llu %llu %llu %llu %llu %llu %u %u", &v[0], &v[1], &v[2], &v[3], &v[4],
                         &v[5], &v[6], &v[7], &entries, &pool) == 10);
            CHECK(v[1] == v[3] + v[4]);        /* searches = hits + misses */
            cliff[i] += v[5];
            if (entries > entries_max) entries_max = entries;
            frames++;
        }
        fclose(tc);
        CHECK(frames == 3 && entries_max == 1024u && cliff[i] > 0);
        for (unsigned f = 0; f < 3; ++f) CHECK(memcmp(rk[i].field[f], r[0].field[f], sizeof r[0].field[f]) == 0);
        for (unsigned b = 0; b < 5; ++b) CHECK(memcmp(rk[i].buf[b], r[0].buf[b], sizeof r[0].buf[b]) == 0);
    }
    g_many_keys = 0;
    CHECK(cliff[0] == cliff[1]);
    /* the decoded texture cache (docs/PERFORMANCE.md M3): with it off
     * (EM_GS_TEXCACHE=0, every texel read from memory) 1 and 8 workers draw
     * the same fields and memory as with it; and an upload over the
     * resident texture between frames, after its copies were made, is seen
     * by the next frame alike with it on (1, 3 and 8 workers) and off */
    static Result rc[2], ru[4];
    const int full = getenv("EM_TEST_FULL") && getenv("EM_TEST_FULL")[0] == '1';
    setenv("EM_GS_TEXCACHE", "0", 1);
    run(1, &rc[0]);
    if (full) run(8, &rc[1]);
    unsetenv("EM_GS_TEXCACHE");
    for (unsigned i = 0; i < (full ? 2u : 1u); ++i) {
        for (unsigned f = 0; f < 3; ++f) CHECK(memcmp(rc[i].field[f], r[0].field[f], sizeof r[0].field[f]) == 0);
        for (unsigned b = 0; b < 5; ++b) CHECK(memcmp(rc[i].buf[b], r[0].buf[b], sizeof r[0].buf[b]) == 0);
    }
    run_upload(1, 1, &ru[0]);
    run_upload(1, 0, &ru[1]);
    if (full) run_upload(3, 0, &ru[2]);
    run_upload(8, 0, &ru[3]);
    for (unsigned i = 1; i < 4; ++i) {
        if (i == 2 && !full) continue;
        for (unsigned f = 0; f < 3; ++f) CHECK(memcmp(ru[i].field[f], ru[0].field[f], sizeof ru[0].field[f]) == 0);
        for (unsigned b = 0; b < 5; ++b) CHECK(memcmp(ru[i].buf[b], ru[0].buf[b], sizeof ru[0].buf[b]) == 0);
    }
    /* the first frame is the frame without uploads; the uploads changed the
     * texels the next frames drew */
    CHECK(memcmp(ru[0].field[0], r[0].field[0], sizeof r[0].field[0]) == 0);
    unsigned uploaded = 0;
    for (unsigned i = 0; i < 512 * 224; ++i) uploaded += memcmp(ru[0].field[1] + 4 * i, r[0].field[1] + 4 * i, 3) != 0;
    CHECK(uploaded > 1000);
    /* the specialized span loops (docs/PERFORMANCE.md M2): with them off
     * (EM_GS_SPEC=0, every primitive through the generic path) 1 and 8
     * workers draw the same fields and memory as with them; with
     * EM_GS_SPEC_CHECK=1 (each primitive a loop takes drawn by the generic
     * path and by the loop, a difference aborts) 1 and 3 workers draw them
     * too; and the loops drew (EM_GS_SPEC_COUNTERS: textured sprites,
     * textured and untextured triangles) */
    static Result rs[4];
    setenv("EM_GS_SPEC", "0", 1);
    run(1, &rs[0]);
    if (full) run(8, &rs[1]);
    unsetenv("EM_GS_SPEC");
    setenv("EM_GS_SPEC_CHECK", "1", 1);
    setenv("EM_GS_SPEC_COUNTERS", "build/gs_world_test/spec_counters.txt", 1);
    run(1, &rs[2]);
    unsetenv("EM_GS_SPEC_COUNTERS");
    if (full) run(3, &rs[3]);
    unsetenv("EM_GS_SPEC_CHECK");
    for (unsigned i = 0; i < 4; ++i) {
        if (i % 2 && !full) continue;
        for (unsigned f = 0; f < 3; ++f) CHECK(memcmp(rs[i].field[f], r[0].field[f], sizeof r[0].field[f]) == 0);
        for (unsigned b = 0; b < 5; ++b) CHECK(memcmp(rs[i].buf[b], r[0].buf[b], sizeof r[0].buf[b]) == 0);
    }
    unsigned long long spc[8] = {0};
    {
        FILE *sc = fopen("build/gs_world_test/spec_counters.txt", "r");
        CHECK(sc);
        char line[256];
        while (fgets(line, sizeof line, sc)) {
            unsigned long long v[9];
            if (line[0] == '#') continue;
            CHECK(sscanf(line, "%llu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4],
                         &v[5], &v[6], &v[7], &v[8]) == 9);
            for (unsigned k = 0; k < 8; ++k) spc[k] += v[k + 1];
        }
        fclose(sc);
    }
    /* every primitive a loop took was checked, and each loop drew pixels */
    CHECK(spc[6] == spc[0] + spc[1] + spc[2]);
    CHECK(spc[3] > 0 && spc[4] > 0 && spc[5] > 0);
    /* parse once (docs/PERFORMANCE.md M4): frames parsed early while the one
     * before is drawn (em_gs_world_preparse; the second frame's T8 CLUT is
     * loaded from the field the first drew, so that parse waits for the
     * first frame before the load) draw what one worker draws the frames
     * one at a time: 3 and 8 workers as the backend kicks them; the early
     * parse forced to stop after 37 ops (EM_GS_EARLY_OPS, the workers go on
     * from there) with 3 and 8 workers and with one worker drawing the
     * parsed list (EM_GS_LIST=1); the early parse off; 1 and 8 bands a
     * worker (EM_GS_BANDS); and the early parse forced past each frame's
     * end (EM_GS_EARLY_OPS 100000, 3 workers): every frame parsed whole on
     * the game's thread, the second while the first is drawn, so its CLUT
     * load from the first frame's field waits for that frame whatever the
     * timing (the waits of the default early parse depend on it) */
    static Result rp_ref, rp_plain, rp[9];
    image_path = image_all;
    run(1, &rp_plain);
    g_clut_prev = 1;
    run(1, &rp_ref);
    unsigned clut_px = 0;
    for (unsigned i = 0; i < 512 * 224; ++i) clut_px += memcmp(rp_ref.field[1] + 4 * i, rp_plain.field[1] + 4 * i, 3) != 0;
    CHECK(clut_px > 1000);
    unsigned long long eops[9] = {0}, pwaits[9] = {0};
    unsigned eframes[9] = {0};
    const char *const lc = "build/gs_world_test/list_counters.txt";
    for (unsigned i = 0; i < 9; ++i) {
        static const unsigned wk[9] = { 3, 8, 3, 8, 1, 8, 3, 8, 3 };
        if (i == 2 || i == 3 || i == 4) setenv("EM_GS_EARLY_OPS", "37", 1);
        if (i == 8) setenv("EM_GS_EARLY_OPS", "100000", 1);
        if (i == 4) setenv("EM_GS_LIST", "1", 1);
        if (i == 5) setenv("EM_GS_EARLY_PARSE", "0", 1);
        if (i == 6) setenv("EM_GS_BANDS", "1", 1);
        if (i == 7) setenv("EM_GS_BANDS", "8", 1);
        setenv("EM_GS_LIST_COUNTERS", lc, 1);
        run_piped(wk[i], &rp[i]);
        unsetenv("EM_GS_LIST_COUNTERS");
        unsetenv("EM_GS_EARLY_OPS");
        unsetenv("EM_GS_LIST");
        unsetenv("EM_GS_EARLY_PARSE");
        unsetenv("EM_GS_BANDS");
        eframes[i] = early_frames(lc, &eops[i], &pwaits[i]);
        for (unsigned f = 0; f < 3; ++f) CHECK(memcmp(rp[i].field[f], rp_ref.field[f], sizeof rp_ref.field[f]) == 0);
        for (unsigned b = 0; b < 5; ++b) CHECK(memcmp(rp[i].buf[b], rp_ref.buf[b], sizeof rp_ref.buf[b]) == 0);
    }
    g_clut_prev = 0;
    image_path = image_one;
    /* the forced early parses parsed 37 ops of every frame; with it off, none */
    CHECK(eframes[2] == 3 && eops[2] == 3 * 37 && eframes[3] == 3 && eops[3] == 3 * 37);
    CHECK(eframes[4] == 3 && eops[4] == 3 * 37 && eframes[5] == 0);
    /* parsed whole early: the second frame's CLUT load waited for the first */
    CHECK(eframes[8] == 3 && pwaits[8] >= 1);
    /* the recorder's barriers and the parse's CLUT loads from its own spans */
    barrier_cases(full);
    own_clut_cases(full);
    list_stamps();
    refusals();
    field_refusals();
    head_probe();
    gif_frames();
    printf("gs_world_test: PASS (GIF frames: a transfer and its texture in the stream, the field of one EmGs "
           "(%u pixels drawn), the same fields and memory with 1, 2, 3 and 8 workers, 2 refusals; "
           "1, 2, 3 and 8 workers and the host default (%u): the same fields and memory "
           "over two world frames "
           "with the target pass, the depth-of-field pass (%u and %u field pixels blended, the Z buffer "
           "untouched) and a list frame, and the same with every block uploaded (1 and 8 workers); 5 refusals, "
           "1 state-block TEX0 accepted; 3 field-declaration refusals; the head probe: the Z-only clear writes no colour, a black clear black, a grey one not black, a transfer refused; past the texture table's end (%u keys, %llu "
           "lookups checked again): the same with 1 and 8 workers; the decoded texture cache off: the same with %s "
           "workers; uploads over the resident texture between frames (%u field pixels changed): the same with the "
           "cache on (%s workers) and off; the specialized span loops off: the same with %s workers; each of their "
           "%llu primitives (%llu / %llu / %llu pixels: textured sprites, textured and untextured triangles) drawn "
           "by the generic path too, the same (%s workers); parse once: frames parsed early while the one before "
           "is drawn, with a CLUT loaded from that one's field (%u pixels), the same with 3 and 8 workers (early "
           "ops %llu and %llu, waits for the frame before %llu and %llu), forced to hand over after 37 ops (3, 8 workers and one worker drawing the list), "
           "the early parse off (8), 1 and 8 bands a worker (3, 8), and parsed whole early (3; waits for the frame "
           "before %llu); the list's texture-cache stamps with two drawing models in a fixed band order: the same as "
           "one model; a barrier for a read, then a read of the drawing target, and a read of the declared field "
           "before the first FRAME_1 write (%u and %u pixels read after the second barrier): three segments; a read "
           "of uploaded memory, then its first draw (%u pixels drawn after the read): two segments; each the "
           "same with one worker drawing the list and %s workers; CLUT loads from the frame's own spans (%u pixels "
           "of the last palette, %u loads counted): the same with one worker drawing the list, %s, parsed early "
           "whole (3 workers, one drawing the list) and handing over after 37 ops (8))\n", g_gif_px, g_host_workers,
           blended[0], blended[1], MANY_KEYS, cliff[0], full ? "1 and 8" : "1", uploaded, full ? "1, 3 and 8" : "1 and 8",
           full ? "1 and 8" : "1", spc[6], spc[3], spc[4], spc[5], full ? "1 and 3" : "1", clut_px, eops[0], eops[1],
           pwaits[0], pwaits[1], pwaits[8], g_barrier_px[0], g_barrier_px[1], g_barrier_px[2], full ? "3 and 8" : "3", g_own_px,
           g_own_waits, full ? "3 and 8 workers, 8 bands a worker and the early parse off" : "3 workers");
    return 0;
}
