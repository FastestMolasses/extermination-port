/* Unit test for em_head_sprite_original (001E2560 and helpers).
 * Behaviour equality with the original instructions is established by
 * tools/test_head_sprite_reference.py; this test pins the native contract:
 * fail-stop faults, the latched fault, no tick after the free, the view
 * guards and the undefined 001CFBE0 paths. Tables here are synthetic (the
 * contract does not depend on the ELF's values). */
#include <stdio.h>
#include <string.h>

#include "game/em_head_sprite_original.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); failures++; } } while (0)

typedef struct {
    int fail;          /* worker index that returns -1 (0: none) */
    int calls[16];
    int32_t rng;
    EmHeadSpriteOriginal *alloc;
    uint8_t packet[16][0x100];
    int packets;
    uint8_t m40[64];
} Log;

static int hit(Log *l, int i) { l->calls[i]++; return l->fail == i ? -1 : 0; }
static int w_rng(void *c, int32_t *v) { Log *l = c; *v = l->rng; return hit(l, 1); }
static int w_alloc(void *c, uint32_t h, uint32_t a1, float wt, EmHeadSpriteOriginal **r)
{ Log *l = c; (void)h; (void)a1; (void)wt; *r = l->alloc; return hit(l, 2); }
static int w_free(void *c, uint32_t s) { (void)s; return hit(c, 3); }
static int w_apply(void *c, float o[4], uint32_t m, const float v[4]) { (void)o; (void)m; (void)v; return hit(c, 4); }
static int w_ident(void *c, float m[16]) { (void)m; return hit(c, 5); }
static int w_euler(void *c, float m[16], const float v[3]) { (void)m; (void)v; return hit(c, 6); }
static int w_trans(void *c, float o[16], const float i[16], const float v[4]) { (void)o; (void)i; (void)v; return hit(c, 7); }
static int w_proj(void *c, const float p[4], int32_t *h) { (void)p; *h = 0x100; return hit(c, 8); }
static int w_m40(void *c, int32_t a0, uint32_t *a, const uint8_t **b)
{ Log *l = c; (void)a0; *a = 0x813F00; *b = l->m40; return hit(l, 9); }
static int w_open(void *c, uint32_t a0, int32_t id, int32_t n, uint8_t **out)
{
    Log *l = c; (void)a0; (void)id;
    if (n > 16 || l->packets >= 16) return -1;
    *out = l->packet[l->packets++];
    return hit(l, 10);
}
static int w_ref(void *c, uint32_t a0, int32_t id, int32_t n, uint32_t a) { (void)a0; (void)id; (void)n; (void)a; return hit(c, 11); }
static int w_tbl(void *c, uint32_t a0, int32_t id, uint32_t t, uint32_t e) { (void)a0; (void)id; (void)t; (void)e; return hit(c, 12); }
static int w_mode(void *c, uint32_t a0, int32_t id, int32_t m) { (void)a0; (void)id; (void)m; return hit(c, 13); }

static EmHeadSpriteOriginalWorkers workers(Log *l)
{
    EmHeadSpriteOriginalWorkers w = {l, w_rng, w_alloc, w_free, w_apply, w_ident, w_euler, w_trans,
                                     w_proj, w_m40, w_open, w_ref, w_tbl, w_mode};
    return w;
}

static EmHeadSpriteOriginalTables tables;
static uint8_t scratch[64], ctx_a0[16];
static uint32_t slots[8] = {0x7D0000, 0x7D0100, 0x7D0200, 0x7D0300, 0x7D0400, 0x7D0500, 0x7D0600, 0x7D0700};
static float rot[3];

static EmHeadSpriteOriginalWorld world(void)
{
    EmHeadSpriteOriginalWorld w = {0, 0, 0x480000, scratch, scratch, ctx_a0, &tables};
    return w;
}

static EmHeadSpriteOriginalOwner owner(void)
{
    EmHeadSpriteOriginalOwner o = {0x8102B0, 1, 0, 0, 10.0f, slots, 8, rot};
    return o;
}

static EmHeadSpriteOriginal ramping(void)
{
    EmHeadSpriteOriginal e;
    memset(&e, 0, sizeof e);
    e.self = 0x7AC8D0;
    e.lifecycle = 1;
    e.sub = 1;
    e.owner = 0x8102B0;
    e.bone = 7;
    return e;
}

static void test_cycle(void)
{
    Log l = {0};
    EmHeadSpriteOriginalWorkers w = workers(&l);
    EmHeadSpriteOriginalFault fault = {0};
    EmHeadSpriteOriginalWorld wd = world();
    EmHeadSpriteOriginalOwner ow = owner();
    EmHeadSpriteOriginal e;
    memset(&e, 0, sizeof e);
    l.alloc = &e;
    EmHeadSpriteOriginal *out = NULL;
    /* Spawn: gate, allocation, key and owner. */
    CHECK(em_head_sprite_original_spawn_001F0120(0x8102B0, 0x3B, &w, &out, &fault) == 0 && out == &e);
    CHECK(e.key == 0x3B && e.owner == 0x8102B0 && l.calls[2] == 1);
    CHECK(em_head_sprite_original_spawn_001F0120(0x8102B0, 1, &w, &out, &fault) == 0 && out == NULL);
    CHECK(l.calls[2] == 1); /* key 1 fails 001E2290: no allocation */
    /* Lifecycle 0: rng 99 -> wait 99 % 40 + 60 = 79; entry word -> bone. */
    tables.d2535F0[0][0] = 7;
    l.rng = 99;
    CHECK(em_head_sprite_original_tick(&e, &ow, &wd, &w, &fault) == 1);
    CHECK(e.lifecycle == 1 && e.timer == 79 && e.bone == 7 && e.local[3] == 1.0f);
    /* The wait: 79 -> -1 takes 80 ticks, then sub 1 with ramp 0. */
    int ticks = 0;
    while (e.sub == 0 && ticks < 200) {
        CHECK(em_head_sprite_original_tick(&e, &ow, &wd, &w, &fault) == 1);
        ticks++;
    }
    CHECK(ticks == 80 && e.timer == -1 && e.ramp == 0.0f);
    /* The ramp: every tick emits (3 opened packets + ref + tbl + mode). */
    int ramp_ticks = 0;
    while (e.sub == 1 && ramp_ticks < 200) {
        l.packets = 0;
        CHECK(em_head_sprite_original_tick(&e, &ow, &wd, &w, &fault) == 1);
        CHECK(l.packets == 3);
        ramp_ticks++;
    }
    CHECK(ramp_ticks == 76 && e.ramp == 1.5f && fault.code == 0);
    /* Owner +0x220 <= 0 ends it; the next tick frees. */
    ow.f220 = 0.0f;
    CHECK(em_head_sprite_original_tick(&e, &ow, &wd, &w, &fault) == 1 && e.lifecycle == 3);
    CHECK(em_head_sprite_original_tick(&e, &ow, &wd, &w, &fault) == 0 && l.calls[3] == 1 && e.freed);
    CHECK(em_head_sprite_original_tick(&e, &ow, &wd, &w, &fault) == -1);
    CHECK(fault.code == EM_HEAD_SPRITE_FAULT_BAD_INDEX && fault.address == 0x1E2560u);
    /* A latched fault makes every later call return -1. */
    e.freed = 0;
    CHECK(em_head_sprite_original_tick(&e, &ow, &wd, &w, &fault) == -1);
}

static void test_faults(void)
{
    /* Each worker failing on the ramp path faults with its address. */
    static const struct { int index; uint32_t address; } cases[] = {
        {4, 0x001026A0u}, {5, 0x001029C0u}, {6, 0x00102C58u}, {7, 0x00102918u}, {8, 0x001CCF70u},
        {9, 0x001CD370u}, {10, 0x001CB5F0u}, {11, 0x001CB6B0u}, {12, 0x001CB760u}, {13, 0x001CB900u}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        Log l = {0};
        l.fail = cases[i].index;
        EmHeadSpriteOriginalWorkers w = workers(&l);
        EmHeadSpriteOriginalFault fault = {0};
        EmHeadSpriteOriginalWorld wd = world();
        EmHeadSpriteOriginalOwner ow = owner();
        EmHeadSpriteOriginal e = ramping();
        CHECK(em_head_sprite_original_tick(&e, &ow, &wd, &w, &fault) == -1);
        CHECK(fault.code == EM_HEAD_SPRITE_FAULT_WORKER_FAILED && fault.address == cases[i].address);
    }
    /* NULL workers, views and guards. */
    Log l = {0};
    EmHeadSpriteOriginalWorkers w = workers(&l);
    EmHeadSpriteOriginalWorld wd = world();
    EmHeadSpriteOriginalOwner ow = owner();
    EmHeadSpriteOriginalFault fault = {0};
    EmHeadSpriteOriginal e = ramping();
    w.w_001CCF70 = NULL;
    CHECK(em_head_sprite_original_tick(&e, &ow, &wd, &w, &fault) == -1);
    CHECK(fault.code == EM_HEAD_SPRITE_FAULT_NULL_WORKER && fault.address == 0x001CCF70u);
    w = workers(&l);
    memset(&fault, 0, sizeof fault);
    e = ramping();
    e.bone = 8; /* outside the 8-slot view */
    CHECK(em_head_sprite_original_tick(&e, &ow, &wd, &w, &fault) == -1);
    CHECK(fault.code == EM_HEAD_SPRITE_FAULT_BAD_INDEX && fault.address == 0x8102B0u + 0x110u);
    memset(&fault, 0, sizeof fault);
    e = ramping();
    ow.address = 0x7A8830; /* view of another owner */
    CHECK(em_head_sprite_original_tick(&e, &ow, &wd, &w, &fault) == -1);
    CHECK(fault.code == EM_HEAD_SPRITE_FAULT_BAD_RESULT);
    ow = owner();
    memset(&fault, 0, sizeof fault);
    e = ramping();
    CHECK(em_head_sprite_original_tick(&e, &ow, NULL, &w, &fault) == -1);
    CHECK(fault.code == EM_HEAD_SPRITE_FAULT_NULL_WORKER);
    /* Lifecycle 0: no entry -> state 3 without reading the world. */
    memset(&fault, 0, sizeof fault);
    memset(&e, 0, sizeof e);
    e.key = 0x3C;
    CHECK(em_head_sprite_original_tick(&e, &ow, NULL, &w, &fault) == 1 && e.lifecycle == 3);
    /* Lifecycle 0 with an entry but no world: faults at the tables. */
    memset(&e, 0, sizeof e);
    e.key = 0x3B;
    CHECK(em_head_sprite_original_tick(&e, &ow, NULL, &w, &fault) == -1 && fault.address == 0x002535F0u);
}

static void test_undefined_and_guard(void)
{
    Log l = {0};
    EmHeadSpriteOriginalWorkers w = workers(&l);
    EmHeadSpriteOriginalWorld wd = world();
    EmHeadSpriteOriginalXf xf;
    memset(&xf, 0, sizeof xf);
    xf.m40_bytes = l.m40;
    uint8_t st_bytes[0x90] = {0};
    EmHeadSpriteOriginalSource st = {0x940000, st_bytes};
    static const int32_t modes[] = {0, 5, -1};
    for (unsigned i = 0; i < 3; i++) {
        EmHeadSpriteOriginalFault fault = {0};
        memcpy(st_bytes + 0x8C, &modes[i], 4);
        CHECK(em_head_sprite_original_001CFBE0(0, 1, &st, &xf, 0, &wd, &w, &fault) == -1);
        CHECK(fault.code == EM_HEAD_SPRITE_FAULT_UNDEFINED && l.calls[10] == 0);
    }
    int32_t one = 1;
    memcpy(st_bytes + 0x8C, &one, 4);
    EmHeadSpriteOriginalFault fault = {0};
    CHECK(em_head_sprite_original_001CFBE0(0, 7, &st, &xf, 0, &wd, &w, &fault) == -1);
    CHECK(fault.code == EM_HEAD_SPRITE_FAULT_UNDEFINED);
    /* Guard: 0x7FFF free bytes skip; 0x8000 emit. */
    memset(&fault, 0, sizeof fault);
    wd.cursor = 0x4F35C0u - 0x7FFFu;
    CHECK(em_head_sprite_original_001CFBE0(0, 1, &st, &xf, 0, &wd, &w, &fault) == 0 && l.calls[10] == 0);
    wd.cursor = 0x4F35C0u - 0x8000u;
    CHECK(em_head_sprite_original_001CFBE0(0, 1, &st, &xf, 1, &wd, &w, &fault) == 1 && l.calls[10] == 4);
    CHECK(fault.code == 0);
}

int main(void)
{
    int32_t mode = 1;
    memcpy(tables.d253670 + 0x8C, &mode, 4);
    test_cycle();
    test_faults();
    test_undefined_and_guard();
    if (failures) {
        fprintf(stderr, "head_sprite_original_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("head_sprite_original_test: PASS\n");
    return 0;
}
