/* Native contract test for em_status_scene_original (ASan/UBSan):
 * fail-stop faults and their latch, the documented results, the static
 * actor pool, and the shape of a module-0x21 load with immediate I/O. Original behaviour is
 * proven by tools/test_status_scene_reference.py; this file pins the
 * port-side contract only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_status_scene_original.h"

static int g_failures;
#define CHECK(cond)                                                                             \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);            \
            g_failures++;                                                                       \
        }                                                                                       \
    } while (0)

typedef struct {
    int calls, fail_at, sprites, polls;
    int32_t rand_value;
    EmStatusSceneActor actors[8];
    int next_actor;
} Ctx;

static int step(Ctx *c) { return ++c->calls == c->fail_at ? -1 : 0; }
static int w_rand(void *x, int32_t *v) { *v = ((Ctx *)x)->rand_value; return step(x); }
static int w_sprite(void *x, int32_t a0, int32_t a1, uint32_t p, uint64_t t, uint32_t rgb, float f12,
                    float f13, float f14)
{
    (void)a0; (void)a1; (void)p; (void)t; (void)rgb; (void)f12; (void)f13; (void)f14;
    ((Ctx *)x)->sprites++;
    return step(x);
}
static int w_slot_push(void *x, uint32_t s) { (void)s; return step(x); }
static int w_call(void *x, EmStatusSceneActor *a, uint32_t fn) { (void)a; (void)fn; return step(x); }
static int w_setup(void *x, EmStatusSceneActor *a, int32_t s, uint8_t n) { (void)a; (void)s; (void)n; return step(x); }
static int w_ii(void *x, int32_t a, int32_t b) { (void)a; (void)b; return step(x); }
static int w_a3(void *x, uint32_t a, uint32_t b, uint32_t c) { (void)a; (void)b; (void)c; return step(x); }
static int w_alloc(void *x, EmStatusSceneActor **a)
{
    Ctx *c = x;
    *a = c->next_actor < 8 ? &c->actors[c->next_actor++] : NULL;
    return step(x);
}
static int w_lookup(void *x, uint32_t b, int32_t code, uint32_t *m) { *m = b + (uint32_t)code; return step(x); }
static int w_bind(void *x, EmStatusSceneActor *a, uint32_t m) { a->w44 = m; return step(x); }
static int w_bones(void *x, uint32_t w, uint32_t *v) { (void)w; *v = 121; return step(x); }
static int w_bones1(void *x, uint32_t w, uint32_t *v) { (void)w; *v = 1; return step(x); }
static int w_slot(void *x, uint32_t *v) { *v = 7; return step(x); }
static int w_u32(void *x, uint32_t n) { (void)n; return step(x); }
static int w_actor_i(void *x, EmStatusSceneActor *a, int32_t v) { (void)a; (void)v; return step(x); }
static int w_actor_clip(void *x, EmStatusSceneActor *a, int32_t v, float p, float q)
{
    (void)a; (void)v; (void)p; (void)q;
    return step(x);
}
static int w_actor_f(void *x, EmStatusSceneActor *a, float t) { (void)a; (void)t; return step(x); }
static int w_actor(void *x, EmStatusSceneActor *a) { (void)a; return step(x); }
static int w_read(void *x, uint32_t f, uint32_t b, int32_t o, int32_t s, uint8_t *h)
{
    (void)f; (void)o; (void)s;
    if ((b == EM_STATUS_SCENE_D_00289BC0) != (h != NULL))
        return -1;
    return step(x);
}
static int w_poll(void *x, int32_t *v) { ((Ctx *)x)->polls++; *v = 1; return step(x); }
static int w_addr(void *x, uint32_t a) { (void)a; return step(x); }

static EmStatusSceneWorkers all_workers(Ctx *c)
{
    EmStatusSceneWorkers w = {0};
    w.ctx = c;
    w.w_00122BB8 = w_rand;
    w.w_001CD520 = w_sprite;
    w.w_001AF800_slot = w_slot_push;
    w.w_001CB590 = w_setup;
    w.w_call = w_call;
    w.w_001C62C0 = w_actor;
    w.w_001C6380 = w_actor;
    w.w_001C69A0 = w_actor;
    w.w_001D2040 = w_ii;
    w.w_001029C0 = w_addr;
    w.w_00102B08 = w_a3;
    w.w_00102BB0 = w_a3;
    w.w_00102A60 = w_a3;
    w.w_001026D0 = w_a3;
    w.w_001026A0 = w_a3;
    w.w_001AFF10 = w_alloc;
    w.w_001C6120 = w_lookup;
    w.w_001CA6E0 = w_bind;
    w.w_001C6150 = w_bones1;
    w.w_001AF7C0 = w_slot;
    w.w_001CB5B0 = w_u32;
    w.w_001C63E0 = w_actor_i;
    w.w_001CA5F0 = w_actor_i;
    w.w_001C67E0 = w_actor_clip;
    w.w_001C64F0 = w_actor_f;
    w.w_0020EC80 = w_actor;
    w.w_001AFF90 = w_actor;
    w.w_00200780 = w_read;
    w.w_00200730 = w_poll;
    w.w_00200830 = w_addr;
    return w;
}

static void test_glyph(void)
{
    CHECK(em_status_scene_glyph_0020E3A0(-2) == '0');
    CHECK(em_status_scene_glyph_0020E3A0(5) == '1');
    CHECK(em_status_scene_glyph_0020E3A0(16) == '@');
    CHECK(em_status_scene_glyph_0020E3A0(21) == 'm');
    CHECK(em_status_scene_glyph_0020E3A0(13) == '/' && em_status_scene_glyph_0020E3A0(22) == '/');
    CHECK(em_status_scene_bank_001FEF70(2, 9) == 0x35 && em_status_scene_bank_001FEF70(0, 3) == 0x33 &&
          em_status_scene_bank_001FEF70(0, 5) == -1);
}

static void test_pool(void)
{
    Ctx c = {0};
    EmStatusSceneWorkers w = all_workers(&c);
    EmStatusScenePool *pool = calloc(1, sizeof *pool);
    for (int i = 0; i < EM_STATUS_SCENE_POOL_RECORDS; ++i) {
        EmStatusSceneActor *a = em_status_scene_alloc_001AFF10(pool);
        CHECK(a == &pool->record[i] && a->b00 == 2 && a->h94 == -1 && a->f60[3] == 1.0f &&
              a->w14 == 0x0028B020u + (uint32_t)i * 0x2F0u);
    }
    CHECK(em_status_scene_alloc_001AFF10(pool) == NULL); /* full: no fault */
    EmStatusSceneFault f = {0};
    pool->record[2].b09 = 3;
    CHECK(em_status_scene_free_001AFF90(pool, &pool->record[2], &w, &f) == 0 && f.code == 0 &&
          pool->record[2].b00 == 0 && pool->record[2].b09 == 0 && pool->record[2].w14 == 0 &&
          c.calls == 3);
    CHECK(em_status_scene_alloc_001AFF10(pool) == &pool->record[2]);
    /* A +0x14 outside the pool faults; the latch holds. */
    pool->record[5].w14 = 0x0028B021u;
    CHECK(em_status_scene_free_001AFF90(pool, &pool->record[5], &w, &f) == -1 && f.code == 4);
    int before = c.calls;
    CHECK(em_status_scene_walk_001B0000(pool, &w, &f) == -1 && c.calls == before);
    EmStatusSceneFault g = {0};
    w.w_call = NULL;
    CHECK(em_status_scene_walk_001B0000(pool, &w, &g) == -1 && g.code == 1);
    em_status_scene_clear_001AFE60(pool);
    CHECK(pool->record[0].b00 == 0 && pool->record[23].w14 == 0);
    EmStatusSceneFault h = {0};
    CHECK(em_status_scene_walk_001B0000(pool, &w, &h) == 0 && h.code == 0); /* nothing in use */
    free(pool);
}

static void test_publish_and_glow(void)
{
    Ctx c = {0};
    EmStatusSceneWorkers w = all_workers(&c);
    EmStatusSceneScratch spr;
    memset(&spr, 0, sizeof spr);
    EmStatusSceneActor a;
    memset(&a, 0, sizeof a);
    a.fB0[0] = 7.4f;
    a.w4C = 0x1CB580;
    EmStatusSceneFault f = {0};
    CHECK(em_status_scene_publish_0020EC80(&a, 0, &spr, &w, &f) == 0 && f.code == 0 && c.sprites == 0 &&
          spr.s3440[0] == -1.0f && spr.s3440[5] == -1.0f && spr.s3400[12] == 7.4f &&
          spr.s36A0[12] == 7.4f);
    CHECK(em_status_scene_publish_0020EC80(&a, 1, &spr, &w, &f) == 0 && c.sprites == 1 &&
          spr.s38B0[0] == 0x20 && spr.s38A0[3] == 0x3F800000u);
    /* A negative rand() faults. */
    c.rand_value = -1;
    CHECK(em_status_scene_publish_0020EC80(&a, 1, &spr, &w, &f) == -1 && f.code == 3 &&
          f.address == 0x00122BB8u);
    EmStatusSceneFault g = {0};
    w.w_001CD520 = NULL;
    c.rand_value = 5;
    const uint32_t colour[4] = {1, 2, 3, 4};
    CHECK(em_status_scene_glow_001F4BF0(0, colour, &w, &g) == -1 && g.code == 1 &&
          g.address == 0x001CD520u);
}

static void test_letter_behaviour(void)
{
    Ctx c = {0};
    EmStatusSceneWorkers w = all_workers(&c);
    EmStatusSceneLetterGlobals g;
    memset(&g, 0, sizeof g);
    g.d275BCC = 0;
    g.view[0] = g.view[10] = 1.0f; /* the hub's view: identity, Y row negated */
    g.view[5] = -1.0f;
    EmStatusSceneActor a;
    memset(&a, 0, sizeof a);
    a.b0C = 1;
    EmStatusSceneFault f = {0};
    CHECK(em_status_scene_letter_0020E460(&a, &g, &w, &f) == 1 && a.b04 == 3); /* over the cap */
    CHECK(em_status_scene_letter_0020E460(&a, &g, &w, &f) == 0 && f.code == 0); /* freed */
    memset(&a, 0, sizeof a);
    a.b0C = 1;
    g.d275BCC = 0x3F0;
    CHECK(em_status_scene_letter_0020E460(&a, &g, &w, &f) == 1 && a.b04 == 1 && a.b09 == 1 &&
          a.f80[0] == 1.5f && a.fB0[0] == -18.4f && a.fB0[1] == -1.3f && a.fB0[2] == 40.0f);
    /* The 'm' marker reads D_00275B40's word 0: NULL faults. */
    a.b0D = 'm';
    CHECK(em_status_scene_letter_0020E460(&a, &g, &w, &f) == -1 && f.code == 1 &&
          f.address == 0x00275B40u);
}

static void test_models(void)
{
    Ctx c = {0};
    EmStatusSceneWorkers w = all_workers(&c);
    EmStatusSceneFault f = {0};
    const uint8_t ca[4] = {0xFF, 5, 0, 7};
    CHECK(em_status_scene_letters_0020E250(ca, 0x1000, &w, &f) == 0 && f.code == 0);
    const char want[] = "/@0128";
    CHECK(c.next_actor == 6);
    for (int i = 0; i < 6; ++i)
        CHECK(c.actors[i].b0D == (uint8_t)want[i] && c.actors[i].w10 == 0x0020E460u &&
              c.actors[i].w44 == 0x1000u + (uint8_t)want[i] && c.actors[i].b0C == 1);
    /* Pool full: nothing written, no fault. */
    c.next_actor = 8;
    CHECK(em_status_scene_letter_0020E1E0('m', 0, &w, &f) == 0 && f.code == 0);

    /* Menu player: more bones than the record holds faults. */
    EmStatusSceneActor a;
    memset(&a, 0, sizeof a);
    EmStatusScenePlayerGlobals g;
    memset(&g, 0, sizeof g);
    g.health = 100.0f;
    w.w_001C6150 = w_bones;
    CHECK(em_status_scene_player_0020E6F0(&a, &g, &w, &f) == -1 && f.code == 4);
    EmStatusSceneFault f2 = {0};
    memset(&a, 0, sizeof a);
    w.w_001C6150 = w_bones1;
    CHECK(em_status_scene_player_0020E6F0(&a, &g, &w, &f2) == 1 && a.b04 == 1 && a.b0B == 0 &&
          a.fC0[1] == 3.14159274f && a.w110[0] == 7);
    CHECK(em_status_scene_player_0020E6F0(&a, &g, &w, &f2) == 1 && a.f38 > 1.0f);
    a.b04 = 3;
    CHECK(em_status_scene_player_0020E6F0(&a, &g, &w, &f2) == 0 && f2.code == 0);
    CHECK(em_status_scene_player_0020E6F0(&a, NULL, &w, &f2) == 0); /* free reads no global */
    a.b04 = 1;
    CHECK(em_status_scene_player_0020E6F0(&a, NULL, &w, &f2) == -1 && f2.code == 1);
}

static void test_loader(void)
{
    Ctx c = {0};
    EmStatusSceneWorkers w = all_workers(&c);
    EmStatusSceneLoader *ld = calloc(1, sizeof *ld);
    uint8_t slot = 0, user[24];
    memset(user, 0xAA, sizeof user);
    ld->d275BD8 = 1;
    ld->d28A748 = 0x19A3F40u;
    ld->header[0] = 0x21;
    ld->header[0xE] = 1;                 /* one chunk */
    ld->header[0x24] = 0x08;             /* chunk size */
    em_status_scene_loader_request_001FF080(&slot, user, 0, 0x21);
    CHECK(slot == 1 && user[0] == 0 && user[6] == 0x21 && user[15] == 0 && user[16] == 0xAA);
    slot = 2;
    EmStatusSceneFault f = {0};
    int dispatches = 0;
    while (slot && dispatches < 50) {
        CHECK(em_status_scene_loader_001FF0D0(&slot, user, ld, &w, &f) == 0);
        dispatches++;
    }
    CHECK(dispatches == 10 && ld->d275BD8 == 0 && user[0] == 0x63 && user[7] == 1 &&
          ld->d275C74 == 0x19A3F40u && c.polls == 3);
    /* The gate skips everything. */
    em_status_scene_loader_request_001FF080(&slot, user, 0, 0x21);
    ld->d282157 = 1;
    CHECK(em_status_scene_loader_001FF0D0(&slot, user, ld, &w, &f) == 0 && user[1] == 0);
    ld->d282157 = 0;
    /* A header address outside D_00289BC0 faults. */
    user[1] = 7;
    ld->d275C70 = 0x00100000u;
    CHECK(em_status_scene_loader_001FF0D0(&slot, user, ld, &w, &f) == -1 && f.code == 4);
    /* State 1 without its streamer worker faults. */
    EmStatusSceneFault g = {0};
    user[0] = 1;
    CHECK(em_status_scene_loader_001FF0D0(&slot, user, ld, &w, &g) == -1 && g.code == 1 &&
          g.address == 0x001FFCD0u);
    free(ld);
}

int main(void)
{
    test_glyph();
    test_pool();
    test_models();
    test_letter_behaviour();
    test_publish_and_glow();
    test_loader();
    if (g_failures) {
        fprintf(stderr, "status_scene_original_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("status_scene_original_test: PASS\n");
    return 0;
}
