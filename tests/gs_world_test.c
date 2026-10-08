/* gs_world_test - the Original profile's GS frame (src/gs/em_gs_world.h):
 * its workers draw exactly what one worker draws.
 *
 * Designed primitives only (nothing disc-derived): a GS memory image with
 * one uploaded texture, then two recorded frames and a list frame, each
 * run with 1, 2, 3 and 8 workers (EM_GS_THREADS). The frames exercise what
 * the world frame does: textured, fogged, Z-tested triangles over resident
 * texels; a 128x128 target drawn and then sampled (a barrier: every band of
 * the target is drawn before any band reads it), drawn into again (a
 * barrier: every band has read it first) and sampled again; the draw
 * environment REFed again; a state block; sprites; the field of the next
 * frame read as a texture by a list frame. Every worker count must leave
 * the same local memory and the same fields, and the one-worker run is the
 * model run directly (em_gs_raster, one EmGs, the same writes). Also the
 * refusals: a transfer register, an IMAGE packet and a texture outside the
 * uploads fault. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_gfx.h"
#include "gs/em_gs_raster.h"
#include "gs/em_gs_world.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "gs_world_test: FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static const char *image_path = "build/gs_world_test/memory.emgm";

/* An EMGM with the 64x64 PSMCT32 texture at block 0x2000 (16 blocks a row
 * of pages: 64 blocks). */
static void write_image(void)
{
    uint8_t *mem = calloc(1, EM_GS_MEM_BYTES);
    EmGs g;
    em_gs_init(&g, mem);
    for (uint32_t y = 0; y < 64; ++y)
        for (uint32_t x = 0; x < 64; ++x)
            em_gs_write_pixel(&g, 0x2000, 1, EM_GS_PSMCT32, x, y,
                              (x * 4u) | (y * 4u) << 8 | ((x ^ y) * 4u) << 16 | 0x80u << 24);
    FILE *f = fopen(image_path, "wb");
    CHECK(f);
    const uint32_t head[4] = { 0x4D474D45u, 1, 1, 0 };   /* "EMGM", v1, one run */
    fwrite(head, 4, 4, f);
    const uint32_t run[2] = { 0x2000, 64 };
    fwrite(run, 4, 2, f);
    fwrite(mem + 0x2000u * 256u, 1, 64u * 256u, f);
    fclose(f);
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

/* One world frame: the scene, the target pass twice, the field. */
static void record_world(EmGsWorld *w, uint32_t seed)
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
    /* the 128x128 target: drawn, sampled by blended triangles (a barrier) */
    for (int pass = 0; pass < 2; ++pass) {
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
}

typedef struct {
    uint8_t field[3][512 * 224 * 4];
    uint8_t buf[4][512 * 224 * 4];    /* fields A and B, the Z buffer (read as CT32), the target */
} Result;

static void run(unsigned workers, Result *out)
{
    char n[8];
    snprintf(n, sizeof n, "%u", workers);
    setenv("EM_GS_THREADS", n, 1);
    EmGsWorld *w = em_gs_world_create();
    CHECK(w);
    CHECK(em_gs_world_workers(w) == workers);
    CHECK(em_gs_world_memory_load(w, image_path) == 0);
    uint8_t env[512], clr[512];
    size_t env_n, clr_n;
    for (unsigned f = 0; f < 2; ++f) {
        record_world(w, 11u + f);
        head(env, &env_n, clr, &clr_n, f ? 0x38 : 0, (int)f);
        CHECK(em_gs_world_kick(w, env, env_n, clr, clr_n) == 0);
        CHECK(em_gs_world_wait(w) == 0);
        uint32_t fw, fh;
        uint64_t frame;
        const uint8_t *fld = em_gs_world_field(w, &fw, &fh, &frame);
        CHECK(fld && fw == 512 && fh == 224 && (frame & 0x1FF) == (f ? 0x38u : 0u));
        memcpy(out->field[f], fld, sizeof out->field[f]);
    }
    /* a list frame: sprites reading the field just drawn (FBP 0x38) into FBP 0 */
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
    EmGsWorldStats st;
    em_gs_world_stats(w, &st);
    CHECK(st.runs == 3 && st.pixels > 0);
    /* the buffers the frames drew, read back */
    CHECK(em_gs_world_read(w, 0, 8, 512, 224, out->buf[0]) == 0);
    CHECK(em_gs_world_read(w, 0x38, 8, 512, 224, out->buf[1]) == 0);
    CHECK(em_gs_world_read(w, 0x70, 8, 512, 224, out->buf[2]) == 0);
    CHECK(em_gs_world_read(w, 0x12C, 2, 128, 128, out->buf[3]) == 0);
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

int main(void)
{
    (void)system("mkdir -p build/gs_world_test");
    write_image();
    static Result r[4];
    const unsigned counts[4] = { 1, 2, 3, 8 };
    for (unsigned i = 0; i < 4; ++i) run(counts[i], &r[i]);
    for (unsigned i = 1; i < 4; ++i) {
        for (unsigned f = 0; f < 3; ++f) CHECK(memcmp(r[i].field[f], r[0].field[f], sizeof r[0].field[f]) == 0);
        for (unsigned b = 0; b < 4; ++b) CHECK(memcmp(r[i].buf[b], r[0].buf[b], sizeof r[0].buf[b]) == 0);
    }
    /* the fields are not trivial: the scene, the target pass and the list
     * frame all wrote pixels of the field */
    unsigned lit = 0, target = 0, listed = 0;
    for (unsigned i = 0; i < 512 * 224; ++i) {
        lit += r[0].field[0][4 * i] != 0;
        listed += memcmp(r[0].field[2] + 4 * i, r[0].field[0] + 4 * i, 3) != 0;
    }
    for (unsigned i = 0; i < 128 * 128; ++i) target += r[0].buf[3][4 * i + 1] == 0xFF;
    CHECK(lit > 1000 && target > 500 && listed > 1000);
    refusals();
    printf("gs_world_test: PASS (1, 2, 3 and 8 workers: the same fields and memory over two world frames "
           "with the target pass and a list frame; 5 refusals, 1 state-block TEX0 accepted)\n");
    return 0;
}
