/* camera_leftovers_test.c - ASan/UBSan fixture for em_camera_leftovers*.c.
 *
 * Stub workers only: nothing here is a claim about the original callees
 * (tools/test_camera_leftovers_reference.py is the evidence). The fixture
 * runs every entry point over random records and scripted worker results,
 * then checks the fail-stop contract:
 *   - a worker failing at call k: the entry returns -1, makes no call
 *     after k and names the failing callee in world.fault;
 *   - a missing world pointer: -1 with no call and no write.
 * Every run must be clean under the sanitizers. */
#include "game/em_camera_leftovers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint32_t rnd_float(float lo, float hi)
{
    return fbits(lo + (hi - lo) * (float)(rnd() & 0xFFFF) / 65535.0f);
}

/* ---- the stub workers ------------------------------------------------------ */
static int calls, fail_at;
static uint32_t last_callee;
static EmCamLeftHit *hit_seen;

static int step(uint32_t callee)
{
    last_callee = callee;
    return calls++ == fail_at ? -1 : 0;
}
static int s_f1(uint32_t callee, uint32_t *out) { if (step(callee)) return -1; *out = rnd_float(-3.2f, 3.2f); return 0; }
static int s_wrap(void *c, uint32_t x, uint32_t *o) { (void)c; (void)x; return s_f1(0x1B1470, o); }
static int s_approach(void *c, uint32_t t, uint32_t cur, uint32_t r, uint32_t *o)
{ (void)c; (void)t; (void)cur; (void)r; return s_f1(0x1B12B0, o); }
static int s_heading(void *c, const uint32_t ob[3], uint32_t x, uint32_t z, uint32_t *o)
{ (void)c; (void)ob; (void)x; (void)z; return s_f1(0x1B1240, o); }
static int s_sine(void *c, uint32_t x, uint32_t *o) { (void)c; (void)x; return s_f1(0x11E2A8, o); }
static int s_cosine(void *c, uint32_t x, uint32_t *o) { (void)c; (void)x; return s_f1(0x11DE90, o); }
static int s_atan2(void *c, uint32_t y, uint32_t x, uint32_t *o) { (void)c; (void)y; (void)x; return s_f1(0x11E620, o); }
static int s_sqrt(void *c, uint32_t x, uint32_t *o) { (void)c; (void)x; return s_f1(0x11E748, o); }
static int s_segment(void *c, const uint32_t a[4], const uint32_t b[4], int m, EmCamLeftHit *h, int *r)
{
    (void)c; (void)a; (void)b; (void)m;
    hit_seen = h;
    if (step(0x19A910)) return -1;
    *r = (int)(rnd() % 3);
    for (int i = 0; i < 4; i++) h->point[i] = rnd_float(-300.0f, 300.0f);
    h->record_1A = (uint16_t)rnd();
    for (int i = 0; i < 3; i++) h->normal[i] = rnd_float(-1.0f, 1.0f);
    return 0;
}
static int s_inside(void *c, int mode, const uint32_t p[3], uint32_t poly, int n, int *r)
{ (void)c; (void)mode; (void)p; (void)poly; (void)n; if (step(0x1B1EA0)) return -1; *r = (int)(rnd() & 1); return 0; }
static int s_solve(void *c, EmCameraFollowRecord *cam, int style, int *r)
{ (void)c; (void)cam; (void)style; if (step(0x18D7B0)) return -1; *r = (int)(rnd() & 0xFF); return 0; }
static int s_commit(void *c, EmCameraFollowRecord *cam, int m) { (void)c; (void)cam; (void)m; return step(0x18C0D0); }
static int s_eef0(void *c, EmCameraFollowRecord *cam, int a) { (void)c; (void)cam; (void)a; return step(0x22EEF0); }
static int s_0c60(void *c, int a, int b, int d) { (void)c; (void)a; (void)b; (void)d; return step(0x1B0C60); }
#define HANDLER(NAME, ADDR) static int s_##NAME(void *c, EmCameraFollowRecord *cam, EmPlayerLiveActor *e) \
    { (void)c; (void)cam; (void)e; return step(ADDR); }
HANDLER(95130, 0x195130) HANDLER(97D20, 0x197D20) HANDLER(98650, 0x198650) HANDLER(98AF0, 0x198AF0)
HANDLER(936E0, 0x1936E0) HANDLER(8CA90, 0x18CA90) HANDLER(98CE0, 0x198CE0) HANDLER(98D90, 0x198D90)
HANDLER(98F10, 0x198F10) HANDLER(963A0, 0x1963A0) HANDLER(96CE0, 0x196CE0) HANDLER(97390, 0x197390)
static int s_3eb0(void *c, EmCameraFollowRecord *cam, EmPlayerLiveActor *e, int a)
{ (void)c; (void)cam; (void)e; (void)a; return step(0x193EB0); }
static int s_d980(void *c, uint32_t *eye, uint32_t *t) { (void)c; (void)eye; (void)t; return step(0x1DD980); }
static int s_2830(void *c, int a, int b) { (void)c; (void)a; (void)b; return step(0x1D2830); }
static int s_0300(void *c) { (void)c; return step(0x1B0300); }

static const EmCamLeftWorkers WORKERS = {
    NULL, s_wrap, s_approach, s_heading, s_sine, s_cosine, s_atan2, s_sqrt, s_segment, s_inside,
    s_solve, s_commit, s_eef0, s_0c60, s_95130, s_97D20, s_98650, s_98AF0, s_936E0, s_8CA90,
    s_98CE0, s_98D90, s_98F10, s_963A0, s_96CE0, s_97390, s_3eb0, s_d980, s_2830, s_0300,
};

/* ---- the world ----------------------------------------------------------- */
typedef struct {
    EmCameraFollowRecord cam;
    EmPlayerLiveActor player;
    uint32_t eye[4], target[4], d5F0[4], d690, d698, d69C;
    uint8_t area, d701, d702, d6B8, d6EF, s3B8D, s31F0;
    uint16_t dE74, s3B80;
    int16_t d28A9A0;
    uint32_t table[0x40];
    EmCamLeftScratch scratch;
    EmCamLeftHit hit;
    EmCameraFollowGlobals follow;
    EmCamLeftGlobals globals;
    EmCamLeftWorld world;
} Fixture;

static void randomize(Fixture *f)
{
    memset(f, 0, sizeof *f);
    for (unsigned i = 0; i < sizeof f->cam.bytes; i++) f->cam.bytes[i] = (uint8_t)rnd();
    for (unsigned i = 0; i < sizeof f->player.bytes; i++) f->player.bytes[i] = (uint8_t)rnd();
    static const unsigned floats[] = { 0x0C, 0x10, 0x14, 0x18, 0x20, 0x24, 0x28, 0x44, 0x48, 0x4C,
                                       0x50, 0x54, 0x5C, 0x64, 0x8C, 0x90, 0x98 };
    for (unsigned i = 0; i < sizeof floats / sizeof floats[0]; i++)
        em_camera_follow_set_word(&f->cam, floats[i], rnd_float(-300.0f, 300.0f));
    for (unsigned at = 0xA0; at < 0xC8; at += 4) em_live_set_u32(&f->player, at, rnd_float(-300.0f, 300.0f));
    f->cam.bytes[0] = (uint8_t)(rnd() % 3);
    f->cam.bytes[4] = (uint8_t)(rnd() % 5);
    f->cam.bytes[5] = (uint8_t)(rnd() % 3);
    f->cam.bytes[6] = (uint8_t)(rnd() % 18);
    em_live_set_u32(&f->player, 0x230, rnd() % 0x32);
    for (int i = 0; i < 4; i++) { f->eye[i] = rnd_float(-300, 300); f->target[i] = rnd_float(-300, 300); }
    f->d690 = rnd_float(-10, 60); f->d698 = rnd_float(0, 30); f->d69C = rnd_float(-10, 60);
    static const uint8_t areas[] = { 0xB, 0, 0x12, 0xE, 0x15 };
    f->area = areas[rnd() % 5]; f->d702 = (uint8_t)(rnd() & 1); f->d6B8 = (uint8_t)(rnd() & 1);
    f->d6EF = (uint8_t)(rnd() % 3); f->dE74 = (uint16_t)rnd(); f->s3B80 = (uint16_t)rnd();
    f->d28A9A0 = (int16_t)(rnd() & 1); f->s3B8D = (uint8_t)(rnd() & 1);
    for (int i = 0; i < 0x40; i++) f->table[i] = rnd_float(-300, 300);
    for (int i = 0; i < EM_CAMLEFT_SCRATCH_WORDS; i++) f->scratch.w[i] = rnd_float(-300, 300);
    f->follow = (EmCameraFollowGlobals){ f->eye, f->target, &f->d690, &f->d698, &f->d69C,
                                         &f->area, &f->d701, &f->d702 };
    f->globals = (EmCamLeftGlobals){ &f->follow, f->d5F0, &f->d6EF, &f->d6B8, &f->dE74, &f->d28A9A0,
                                     &f->s3B80, &f->s3B8D, &f->s31F0, f->table, 0x40 };
    f->world = (EmCamLeftWorld){ &f->cam, &f->player, &f->globals, &f->scratch, &f->hit, &WORKERS, 0 };
}

enum { ENTRIES = 19 };
static int run(Fixture *f, int entry, uint32_t arg)
{
    EmCamLeftWorld *w = &f->world;
    EmPlayerLiveActor *e = &f->player;
    int r = 0;
    uint32_t v[2] = { f->cam.bytes[0x10], f->cam.bytes[0x14] };
    switch (entry) {
    case 0: return em_camleft_0018B9C0(w);
    case 1: return em_camleft_0018BC20(w, e);
    case 2: return em_camleft_00190F20(w, e);
    case 3: return em_camleft_0018C0C0(w);
    case 4: return em_camleft_001914A0(w, e);
    case 5: return em_camleft_00191580(w, e);
    case 6: return em_camleft_0018C5A0(w, v, rnd_float(-300, 300), rnd_float(0, 5), &r);
    case 7: return em_camleft_001916C0(w, e, (int)(arg % 3));
    case 8: return em_camleft_00191000(w, e, &r);
    case 9: return em_camleft_0022FCA0(w);
    case 10: return em_camleft_00230000(w, e);
    case 11: return em_camleft_00194D10(w, e, (int)(arg % 3), &r);
    case 12: return em_camleft_0018DD20(w, e, (int)(arg % 8), 6 + (int)(arg & 1), &r);
    case 13: return em_camleft_0018CE60(w, e->bytes + 0xB0, (int)(arg % 7));
    case 14: return em_camleft_0018D910(w, e, 6 + (int)(arg & 1));
    case 15: return em_camleft_0015CBA0(e);
    case 16: return em_camleft_0018F870(w, e, (int)(arg % 8), 6 + (int)(arg & 1), &r);
    case 17: return em_camleft_00193D90(w, e);
    default: return 0;
    }
}

int main(void)
{
    static Fixture f, before;
    int runs = 0, cuts = 0, refusals = 0;
    for (int round = 0; round < 400; round++) {
        for (int entry = 0; entry < ENTRIES - 1; entry++) {
            uint64_t seed = rng_state;
            uint32_t arg = rnd();
            randomize(&f);
            calls = 0; fail_at = -1;
            int status = run(&f, entry, arg);
            if (status != 0) {
                fprintf(stderr, "entry %d: status %d fault %06X\n", entry, status, f.world.fault);
                return 1;
            }
            runs++;
            int made = calls;
            for (int k = 0; k < made; k++) {                   /* a fault at every call */
                rng_state = seed; arg = rnd(); randomize(&f);
                calls = 0; fail_at = k;
                status = run(&f, entry, arg);
                if (status != -1 || calls != k + 1 || f.world.fault != last_callee) {
                    fprintf(stderr, "entry %d: cut %d gave %d after %d calls (fault %06X, callee %06X)\n",
                            entry, k, status, calls, f.world.fault, last_callee);
                    return 1;
                }
                cuts++;
            }
            if (entry == 15) continue;                         /* 0015CBA0 takes no world */
            for (int missing = 0; missing < 6; missing++) {    /* a missing world pointer */
                rng_state = seed; arg = rnd(); randomize(&f);
                if (missing == 0) f.world.cam = NULL;
                if (missing == 1) f.world.globals = NULL;
                if (missing == 2) f.world.scratch = NULL;
                if (missing == 3) f.world.hit = NULL;
                if (missing == 4) f.world.workers = NULL;
                if (missing == 5) f.follow.target = NULL;
                memcpy(&before, &f, sizeof f);
                calls = 0; fail_at = -1;
                status = run(&f, entry, arg);
                if (missing < 5 && (status != -1 || calls != 0 || memcmp(&before, &f, offsetof(Fixture, world)) != 0)) {
                    fprintf(stderr, "entry %d: missing %d not refused cleanly\n", entry, missing);
                    return 1;
                }
                if (status == -1) {
                    if (calls != 0 || memcmp(&before, &f, offsetof(Fixture, world)) != 0) {
                        fprintf(stderr, "entry %d: refusal %d after a call or write\n", entry, missing);
                        return 1;
                    }
                    refusals++;
                }
            }
        }
    }
    (void)hit_seen;
    printf("camera leftovers sanitizer fixture: %d runs, %d fail-stop cuts, %d refusals, clean\n",
           runs, cuts, refusals);
    return 0;
}
