/* camera_follow_original_test.c - sanitizer fixture for
 * em_camera_follow_original.c (docs/CAMERA_FOLLOW_ORIGINAL.md).
 *
 * The original-instruction evidence is tools/test_camera_follow_original_reference.py.
 * This fixture only runs the translation under ASan/UBSan: every player
 * state through 001921D0 (both freelook values), every 0018D7B0 style, the
 * leaves, the refusals (each worker, record and global pointer missing:
 * -1 before any write or call) and the fail-stop cut (a worker failing at
 * call k: -1 after exactly k + 1 calls). The workers are plain stubs; no
 * value here claims anything about the original callees. */
#include "game/em_camera_follow_original.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static int calls, fail_at = -1;
static int step(void) { return calls++ == fail_at ? -1 : 0; }

static int s_approach(void *c, uint32_t t, uint32_t cur, uint32_t r, uint32_t *o) { (void)c; (void)cur; (void)r; *o = t; return step(); }
static int s_wrap(void *c, uint32_t x, uint32_t *o) { (void)c; *o = x; return step(); }
static int s_heading(void *c, const uint32_t obj[3], uint32_t x, uint32_t z, uint32_t *o) { (void)c; (void)obj; (void)z; *o = x; return step(); }
static int s_sine(void *c, uint32_t x, uint32_t *o) { (void)c; (void)x; *o = 0x3F000000u; return step(); }
static int s_cosine(void *c, uint32_t x, uint32_t *o) { (void)c; (void)x; *o = 0x3F5DB3D7u; return step(); }
static int s_tether(void *c, EmCameraFollowRecord *cam, EmPlayerLiveActor *p) { (void)c; (void)cam; (void)p; return step(); }
static int s_solve(void *c, EmCameraFollowRecord *cam, EmPlayerLiveActor *p, int style, int mask, int *r) { (void)c; (void)cam; (void)p; (void)mask; *r = style * 3; return step(); }
static int s_bounds(void *c, EmCameraFollowRecord *cam, EmPlayerLiveActor *p, int mask) { (void)c; (void)cam; (void)p; (void)mask; return step(); }
static int s_segment(void *c, const uint32_t a[4], const uint32_t b[4], int mask, EmCameraFollowHit *h) { (void)c; (void)a; (void)b; h->result = mask & 1; h->record_1A = 0x2000; h->point_y = 0x42C80000u; return step(); }
static int s_ground(void *c, const uint32_t a[4], const uint32_t b[4], int *r) { (void)c; (void)a; (void)b; *r = 1; return step(); }
static int s_identity(void *c, uint32_t m[16]) { (void)c; for (int i = 0; i < 16; i++) m[i] = (i % 5) ? 0 : 0x3F800000u; return step(); }
static int s_euler(void *c, uint32_t out[16], const uint32_t in[16], const uint32_t a[4]) { (void)c; (void)a; memmove(out, in, 64); return step(); }

static const EmCameraFollowWorkers WORKERS = {
    NULL, s_approach, s_wrap, s_heading, s_sine, s_cosine, s_tether, s_solve, s_solve,
    s_bounds, s_segment, s_ground, s_identity, s_euler,
};

typedef struct {
    EmCameraFollowRecord cam;
    EmPlayerLiveActor player;
    uint32_t eye[4], target[4], d690, d698, d69C;
    uint8_t area, d701, d702;
    EmCameraFollowScratch scratch;
    EmCameraFollowGlobals globals;
    EmCameraFollowWorkers workers;
    EmCameraFollowWorld world;
} Fixture;

static void put(uint8_t *b, unsigned at, uint32_t v) { memcpy(b + at, &v, 4); }

static void setup(Fixture *f, int32_t state, unsigned seed)
{
    memset(f, 0, sizeof *f);
    srand(seed);
    for (unsigned i = 0; i < sizeof f->cam.bytes; i++) f->cam.bytes[i] = (uint8_t)rand();
    for (unsigned i = 0; i < sizeof f->player.bytes; i++) f->player.bytes[i] = (uint8_t)rand();
    static const unsigned floats[] = { 0x0C, 0x10, 0x14, 0x18, 0x20, 0x24, 0x28, 0x44, 0x50, 0x54,
                                       0x5C, 0x64, 0x8C, 0x90, 0x94, 0x98 };
    for (unsigned i = 0; i < sizeof floats / sizeof floats[0]; i++)
        put(f->cam.bytes, floats[i], 0x41200000u + (uint32_t)(rand() & 0xFFFFF));
    put(f->player.bytes, 0x230, (uint32_t)state);
    f->d690 = 0x41F00000u; f->d698 = 0x41BA6666u; f->d69C = 0x4109999Au;
    f->area = 0xB; f->d701 = 1; f->d702 = (uint8_t)(seed % 8);
    f->globals = (EmCameraFollowGlobals){ f->eye, f->target, &f->d690, &f->d698, &f->d69C,
                                          &f->area, &f->d701, &f->d702 };
    f->workers = WORKERS;
    f->world = (EmCameraFollowWorld){ &f->cam, &f->player, &f->globals, &f->scratch, &f->workers };
}

/* Unbind one of the 13 workers, 5 world pointers or 8 global pointers. */
static void drop(Fixture *f, int k)
{
    EmCameraFollowWorkers *w = &f->workers;
    EmCameraFollowGlobals *g = &f->globals;
    switch (k) {
    case 0: w->approach = NULL; break;
    case 1: w->wrap = NULL; break;
    case 2: w->heading = NULL; break;
    case 3: w->sine = NULL; break;
    case 4: w->cosine = NULL; break;
    case 5: w->tether = NULL; break;
    case 6: w->solve = NULL; break;
    case 7: w->solve_aim = NULL; break;
    case 8: w->bounds = NULL; break;
    case 9: w->segment = NULL; break;
    case 10: w->ground = NULL; break;
    case 11: w->identity = NULL; break;
    case 12: w->euler = NULL; break;
    case 13: f->world.cam = NULL; break;
    case 14: f->world.player = NULL; break;
    case 15: f->world.globals = NULL; break;
    case 16: f->world.scratch = NULL; break;
    case 17: f->world.workers = NULL; break;
    case 18: g->eye = NULL; break;
    case 19: g->target = NULL; break;
    case 20: g->d690 = NULL; break;
    case 21: g->d698 = NULL; break;
    case 22: g->d69C = NULL; break;
    case 23: g->area = NULL; break;
    case 24: g->d701 = NULL; break;
    default: g->d702 = NULL; break;
    }
}

static const int32_t STATES[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xF, 0x13, 0x14, 0x15, 0x18,
                                  0x19, 0x26, 0x27, 0x2C, 0x2D, 0x2F, 0x30, -1 };
#define NSTATES (int)(sizeof STATES / sizeof STATES[0])

int main(void)
{
    Fixture f;
    int runs = 0, refusals = 0, cuts = 0;

    /* Every state, both freelook values, and every solve style. */
    for (int s = 0; s < NSTATES; s++) {
        for (int freelook = 0; freelook < 2; freelook++) {
            setup(&f, STATES[s], (unsigned)(s * 2 + freelook));
            calls = 0; fail_at = -1;
            CHECK(em_camera_follow_001921D0(&f.world, &f.player, freelook) == 0, "state %X", STATES[s]);
            runs++;
        }
    }
    for (int style = 0; style < 8; style++) {
        int r = -99;
        setup(&f, 1, 100u + (unsigned)style);
        calls = 0; fail_at = -1;
        CHECK(em_camera_follow_0018D7B0(&f.world, style, &r) == 0, "style %d", style);
        CHECK(f.cam.bytes[7] == (uint8_t)r, "cam+7 is the solver result (style %d)", style);
        runs++;
    }

    /* The leaves refuse NULL arguments. */
    CHECK(em_camera_follow_0018C6A0(NULL, f.eye, 0, NULL) == -1, "0018C6A0 NULL");
    CHECK(em_camera_follow_0018C4B0(NULL, 0, 0, NULL) == -1, "0018C4B0 NULL");
    CHECK(em_camera_follow_00192010(NULL, 0, 0, 0) == -1, "00192010 NULL");
    CHECK(em_camera_follow_00191D40(&f.cam, NULL, 0, 0) == -1, "00191D40 NULL");
    CHECK(em_camera_follow_00191390(NULL, &f.player) == -1, "00191390 NULL");

    /* Refusals: each worker, then each record and global pointer missing. */
    for (int k = 0; k < 13 + 5 + 8; k++) {
        setup(&f, 1, 7);
        drop(&f, k);
        Fixture before = f;
        calls = 0; fail_at = -1;
        CHECK(em_camera_follow_001921D0(&f.world, &f.player, 0) == -1, "refusal %d (001921D0)", k);
        CHECK(em_camera_follow_0018D7B0(&f.world, 0, NULL) == -1, "refusal %d (0018D7B0)", k);
        CHECK(em_camera_follow_0018D330(&f.world, &f.player, 0, 6) == -1, "refusal %d (0018D330)", k);
        CHECK(calls == 0 && memcmp(&before.cam, &f.cam, sizeof f.cam) == 0 &&
              memcmp(&before.scratch, &f.scratch, sizeof f.scratch) == 0 &&
              memcmp(before.eye, f.eye, sizeof f.eye) == 0, "refusal %d wrote or called", k);
        refusals++;
    }

    /* Fail-stop: a worker failing at call k stops the routine there. */
    for (int s = 0; s < NSTATES; s++) {
        setup(&f, STATES[s], 50u + (unsigned)s);
        calls = 0; fail_at = -1;
        CHECK(em_camera_follow_001921D0(&f.world, &f.player, 0) == 0, "state %X", STATES[s]);
        int total = calls;
        for (int k = 0; k < total; k++) {
            setup(&f, STATES[s], 50u + (unsigned)s);
            calls = 0; fail_at = k;
            CHECK(em_camera_follow_001921D0(&f.world, &f.player, 0) == -1 && calls == k + 1,
                  "state %X: fault at call %d not stopped (calls %d)", STATES[s], k, calls);
            cuts++;
        }
    }

    if (failures) {
        fprintf(stderr, "camera follow fixture: %d FAILURES\n", failures);
        return 1;
    }
    printf("camera follow fixture (ASan/UBSan): PASS %d runs, %d refusals, %d fail-stop cuts\n",
           runs, refusals, cuts);
    return 0;
}
