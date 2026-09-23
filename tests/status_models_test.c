/* em_status_models over the status-hub capture (ASan/UBSan).
 *
 * The capture ../Extermination/build/startup-reference/status-hub holds
 * the hub a few frames after it opened: pool records 0..6 of D_0028B020
 * (the menu player and six equipment letter models) and their bone slots
 * (+0x90: the node world matrices 001C69A0 / 001C6380 wrote). This test
 * runs the port's hub model workers from a cleared pool exactly as
 * 0020CDC0 does (0020DFA0 configure, sub-state 0: 001AFEB0, 001AFE60,
 * 001AFF10 + 0020E6F0, 0020E250; each sub-state-1 frame: 001B0000) over
 * the captured globals, until the menu player's breathe/yaw pair equals
 * the captured one, then compares every modelled record byte and every
 * node world matrix with the capture, and checks the queued 001CB580
 * draws. It also pins the fail-stop contract (missing glyph model, the
 * D_008104E4 == 1 glow sprite 001CD520).
 *
 * Usage: status_models_test <assets/status_models> <status-hub eeMemory.bin>
 * Exit 0 with "status models: PASS", or 77 when the inputs are missing. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_gfx.h"
#include "game/em_status_models.h"

static int failures;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        if (!(c)) {                                                                      \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);        \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

/* ---- em_gfx stubs: record what the render hands the backend ---- */
struct EmGfxMesh { int id; };
static struct EmGfxMesh meshes[16];
static int mesh_count, draws, rigs, fog_offs;
static float last_amb[4];
static uint32_t last_bones;
EmGfxMesh *em_gfx_mesh_create(EmGfx *g, const float *v, uint32_t vc, const uint32_t *i, uint32_t ic,
                              const EmGfxTexDesc *t, uint32_t tc, const uint8_t *tx, uint32_t f)
{
    (void)g; (void)v; (void)vc; (void)i; (void)ic; (void)t; (void)tc; (void)tx; (void)f;
    return mesh_count < 16 ? &meshes[mesh_count++] : NULL;
}
void em_gfx_mesh_destroy(EmGfx *g, EmGfxMesh *m) { (void)g; (void)m; }
void em_gfx_draw_skinned(EmGfx *g, EmGfxMesh *m, const float *vp, const float *p, uint32_t n)
{
    (void)g; (void)m; (void)vp; (void)p;
    last_bones = n;
    draws++;
}
void em_gfx_char_rig(EmGfx *g, const EmGfxCharRig *rig)
{
    (void)g;
    if (rig) {
        memcpy(last_amb, rig->amb, sizeof last_amb);
        rigs++;
    }
}
void em_gfx_fog_off(EmGfx *g) { (void)g; fog_offs++; }

static uint8_t *ram;
static uint32_t word(uint32_t a) { return (uint32_t)ram[a] | (uint32_t)ram[a + 1] << 8 |
                                          (uint32_t)ram[a + 2] << 16 | (uint32_t)ram[a + 3] << 24; }
static float fword(uint32_t a) { uint32_t w = word(a); float f; memcpy(&f, &w, 4); return f; }
static uint32_t bits(float f) { uint32_t w; memcpy(&w, &f, 4); return w; }

#define POOL 0x0028B020u
#define STRIDE 0x2F0u

static EmStatusModelsInputs captured_inputs(void)
{
    EmStatusModelsInputs in = {.health = fword(0x00810858u), .infection = fword(0x0081085Cu),
                               .d8104E4 = ram[0x008104E4u], .d810C60 = ram[0x00810C60u]};
    memcpy(in.ca, ram + 0x00810CA4u, 4);
    return in;
}

/* The hub's sub-state 0 as 0020CDC0 runs it after 0020DFA0. */
static int open_hub(EmStatusModels *m, const EmStatusModelsInputs *in)
{
    return em_status_models_clear(m) == 1 && em_status_models_configure(m) == 1 &&
           em_status_models_release(m) == 1 && em_status_models_clear(m) == 1 &&
           em_status_models_event(m, EM_STATUS_HUB_INSTALL_DRAW, 0x0020E6F0u, in) == 1 &&
           em_status_models_event(m, EM_STATUS_HUB_BUILD_MODELS, 0, in) == 1;
}

/* Every modelled field of a record against the captured record. */
static int record_matches(const EmStatusSceneActor *a, uint32_t r)
{
    int ok = a->b00 == ram[r] && a->b04 == ram[r + 4] && a->b05 == ram[r + 5] &&
             a->b09 == ram[r + 9] && a->b0B == ram[r + 0xB] && a->b0C == ram[r + 0xC] &&
             a->b0D == ram[r + 0xD] && a->w10 == word(r + 0x10) && a->w14 == word(r + 0x14) &&
             bits(a->f38) == word(r + 0x38) && a->w4C == word(r + 0x4C) &&
             (uint16_t)a->h94 == (uint16_t)(ram[r + 0x94] | ram[r + 0x95] << 8);
    for (int k = 0; k < 4; ++k)
        ok = ok && bits(a->f60[k]) == word(r + 0x60 + 4u * k) &&
             bits(a->f80[k]) == word(r + 0x80 + 4u * k);
    for (int k = 0; k < 3; ++k)
        ok = ok && bits(a->fB0[k]) == word(r + 0xB0 + 4u * k) &&
             bits(a->fC0[k]) == word(r + 0xC0 + 4u * k);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <assets/status_models> <status-hub eeMemory.bin>\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[2], "rb");
    ram = malloc(0x2000000);
    if (!f || !ram || fread(ram, 1, 0x2000000, f) != 0x2000000) {
        fprintf(stderr, "status models: SKIP (no capture %s)\n", argv[2]);
        return 77;
    }
    fclose(f);
    EmStatusModels *m = em_status_models_load(argv[1]);
    if (!m) {
        fprintf(stderr, "status models: SKIP (run tools/export_status_models.py)\n");
        return 77;
    }
    const EmStatusModelsInputs in = captured_inputs();
    CHECK(in.d8104E4 == 0 && in.d810C60 == 0);

    /* The walk before 0020DFA0 set D_00810610 faults (fail-stop). */
    EmStatusModels *early = em_status_models_load(argv[1]);
    CHECK(early && em_status_models_event(early, EM_STATUS_HUB_ACTORS_TICK, 0, &in) == -1);
    CHECK(em_status_models_event(early, EM_STATUS_HUB_BUILD_MODELS, 0, &in) == -1); /* latched */
    em_status_models_free(early, NULL);

    CHECK(open_hub(m, &in));
    const EmStatusScenePool *pool = em_status_models_pool(m);
    uint32_t r0 = POOL;
    unsigned walks = 0;
    int reached = 0;
    while (walks < 400 && !reached) {
        CHECK(em_status_models_event(m, EM_STATUS_HUB_ACTORS_TICK, 0, &in) == 1);
        ++walks;
        const EmStatusSceneActor *a = &pool->record[0];
        reached = a->b04 == 1 && a->b05 == ram[r0 + 5] && bits(a->f38) == word(r0 + 0x38) &&
                  bits(a->fC0[1]) == word(r0 + 0xC4);
        if (!reached)
            CHECK(em_status_models_queued(m) == 7 || walks == 1);
    }
    CHECK(reached);
    fprintf(stderr, "status models: captured breathe/yaw at walk %u\n", walks);
    CHECK(walks == 10); /* 0020E6F0 state 0 + nine runs (docs/STATUS_SCENE.md section 6) */

    /* Records 0..6 and the empty record 7. */
    for (uint32_t i = 0; i < 8; ++i)
        CHECK(record_matches(&pool->record[i], POOL + STRIDE * i));

    /* Node world matrices (+0x90 of each bone slot): exact bits. */
    unsigned nodes = 0, exact = 0;
    float worst = 0;
    for (uint32_t i = 0; i < 7; ++i) {
        uint32_t r = POOL + STRIDE * i;
        for (unsigned b = 0; b < ram[r + 0xC]; ++b) {
            uint32_t slot = word(r + 0x110 + 4 * b);
            float native[16];
            CHECK(em_status_models_node_world(m, i, b, native));
            int same = 1;
            for (int k = 0; k < 16; ++k) {
                float d = fabsf(native[k] - fword(slot + 0x90 + 4u * k));
                if (d > worst)
                    worst = d;
                same = same && bits(native[k]) == word(slot + 0x90 + 4u * k);
            }
            exact += same;
            ++nodes;
        }
    }
    fprintf(stderr, "status models: %u of %u node world matrices bit-exact (max |d| %g)\n",
            exact, nodes, (double)worst);
    CHECK(nodes == 27 && exact == nodes);

    /* The walk queued one 001CB580 draw per record (lighting mode 1:
     * ambient 128 + actor +0x80..; the letters' +0x80 = 1.5). */
    CHECK(em_status_models_queued(m) == 7);
    struct EmGfx *gfx = (struct EmGfx *)(uintptr_t)1;
    CHECK(em_status_models_render(m, gfx, 480.0f) == 1);
    CHECK(draws == 7 && rigs == 7 && fog_offs == 1 && mesh_count == 7);
    CHECK(last_bones == 2 && last_amb[0] == 129.0f && last_amb[3] == 0.0f);
    CHECK(em_status_models_queued(m) == 0);

    /* 0020E0C0 / the next open: 001AFEB0 then 001AFE60 empty the pool. */
    CHECK(em_status_models_release(m) == 1 && em_status_models_clear(m) == 1);
    for (int i = 0; i < EM_STATUS_SCENE_POOL_RECORDS; ++i)
        CHECK(pool->record[i].b00 == 0);
    /* A second visit reproduces the capture again (no state leaks). */
    CHECK(open_hub(m, &in));
    for (unsigned k = 0; k < 10; ++k)
        CHECK(em_status_models_event(m, EM_STATUS_HUB_ACTORS_TICK, 0, &in) == 1);
    for (uint32_t i = 0; i < 7; ++i)
        CHECK(record_matches(&pool->record[i], POOL + STRIDE * i));
    em_status_models_free(m, NULL);

    /* A glyph the export does not hold faults (CA6 = 4 adds 0x15 -> 'm'). */
    m = em_status_models_load(argv[1]);
    EmStatusModelsInputs other = in;
    other.ca[2] = 4;
    CHECK(m && em_status_models_configure(m) == 1 &&
          em_status_models_event(m, EM_STATUS_HUB_BUILD_MODELS, 0, &other) == -1);
    CHECK(em_status_models_fault(m)->address == 0x001C6120u);
    em_status_models_free(m, NULL);

    /* D_008104E4 == 1: 0020EC80 reaches 001F4BF0 (rand, then the glow
     * sprite 001CD520, which is not translated): fault. The model word of
     * variant 1 (D_0028A590) is not exported either, so the fault comes
     * at the first walk's 0020E6F0. */
    m = em_status_models_load(argv[1]);
    other = in;
    other.d8104E4 = 1;
    CHECK(m && open_hub(m, &other));
    CHECK(em_status_models_event(m, EM_STATUS_HUB_ACTORS_TICK, 0, &other) == -1);
    em_status_models_free(m, NULL);

    free(ram);
    if (failures) {
        fprintf(stderr, "status models: FAIL (%d)\n", failures);
        return 1;
    }
    printf("status models: PASS (walk %u = capture; 27 node world matrices bit-exact)\n", walks);
    return 0;
}
