/* Sanitizer fixture for em_area_script over the user's exported AREA11
 * script images (and the AREA11 overlay for the truck/director arena).
 *
 * The original-instruction comparison lives in
 * tools/test_area_script_reference.py. This fixture checks the host's own
 * contracts under ASan/UBSan: every admitted level script runs to its end
 * with bound workers, a skip aborts through the canonical 3B91 byte, a
 * missing worker/world pointer or a failing worker faults and stays
 * faulted, and nothing is reported as completed after a fault.
 *
 * The services here are deliberately minimal and make no timing claim:
 * a fade request settles on the next tick, a message completes on the
 * next tick, the camera track cursor passes its head at once. */
#include "game/em_area_script.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t s3B84;
    uint8_t s3B8D, s3B8F, s3B91, s3B92;
    float s3600[4];
    uint8_t d6EF, d6F3, d6F4, d78F, activity[12];
    uint8_t flags[256], counters[256];
    uint8_t e1, e2, e3, e4, e6;
    float cam0C, cam10[4], cam20[4], cam50, cam54, cam74, cam78;
    int16_t cam6E, camA0;
    uint32_t cam70;
    float w5D0[4], w5E0[4], w5F0[4];
    int32_t m0, m4, mC;
    uint32_t m8;
    uint32_t p040, p200;
    float p0A0[4], p0B0[4], p0C0[4], p1F4, p1F8;
    int16_t p1F2, p20C;
    uint8_t p25C, p2F3, p2FF;
    uint32_t s040;
    float s0B0[4], s0C0[4];
    int16_t fade;
    int8_t busy;
    uint8_t cue, cue_flag;
    int16_t table[9];
    /* service state */
    int calls, failing;         /* failing: address whose worker returns -1 */
    int callback_calls;
} Fixture;

static Fixture fx;

#define W(name) static int name
#define FAILS(address) do { ++fx.calls; if (fx.failing == (int)(address)) return -1; } while (0)
W(r_0028A490)(void *c, uint32_t a, uint32_t *v) { (void)c; *v = a; return 0; }
W(r_track_head)(void *c, uint32_t t, float *v) { (void)c; (void)t; *v = 0.0f; return 0; }
W(r_player_bone_C0)(void *c, float out[4]) { (void)c; out[0] = 330; out[1] = 300; out[2] = 190; out[3] = 1; return 0; }
W(w_001AEB60)(void *c, int16_t a) { (void)c; (void)a; FAILS(0x1AEB60); return 0; }
W(w_001AEBA0)(void *c, int16_t a) { (void)c; (void)a; FAILS(0x1AEBA0); return 0; }
W(w_001AEDE0)(void *c, int16_t a, uint8_t b) { (void)c; (void)a; (void)b; FAILS(0x1AEDE0); fx.fade = 2; return 0; }
W(w_001AEE10)(void *c, int16_t a, uint8_t b) { (void)c; (void)a; (void)b; FAILS(0x1AEE10); fx.fade = 0; return 0; }
W(w_001FD4C0)(void *c, int32_t a) { (void)c; (void)a; FAILS(0x1FD4C0); fx.d6F4 = 1; return 0; }
W(w_00119828)(void *c, int a, int b, int d) { (void)c; (void)a; (void)b; (void)d; FAILS(0x119828); return 0; }
W(w_001D2610)(void *c, float a) { (void)c; (void)a; FAILS(0x1D2610); return 0; }
W(w_001D25F0)(void *c, float a) { (void)c; assert(a == 480.0f); FAILS(0x1D25F0); return 0; }
W(w_001CA770)(void *c, uint32_t a) { (void)c; assert(a == EM_AREA_SCRIPT_D_008102B0); FAILS(0x1CA770); return 0; }
W(w_001FAE70)(void *c, int a) { (void)c; (void)a; FAILS(0x1FAE70); return 0; }
W(w_001CA700)(void *c, uint32_t a, uint32_t b, int16_t d, int32_t *r) { (void)c; (void)a; (void)b; assert(d == 7); FAILS(0x1CA700); *r = 1; return 0; }
W(w_001D06D0)(void *c, uint32_t a, uint8_t b) { (void)c; (void)a; assert(b == 1); FAILS(0x1D06D0); return 0; }
W(w_001DD980)(void *c, const float *e, const float *t) { (void)c; assert(e == fx.w5D0 && t == fx.w5E0); FAILS(0x1DD980); return 0; }
W(w_0011E2A8)(void *c, float a, float *r) { (void)c; FAILS(0x11E2A8); *r = a < 0 ? -1.0f : 1.0f; return 0; }
W(w_001C6120)(void *c, uint32_t t, int32_t i, uint32_t *r) { (void)c; (void)t; (void)i; FAILS(0x1C6120); *r = 0x9F0000; return 0; }
W(w_0022EC30)(void *c, uint32_t a) { (void)c; assert(a == EM_AREA_SCRIPT_D_008101E0); FAILS(0x22EC30); return 0; }
W(w_00182F90)(void *c, uint32_t a, const float *t) { (void)c; (void)a; FAILS(0x182F90); memcpy(fx.p0A0, t, 16); return 0; }
W(w_001B1240)(void *c, const float *o, float x, float z, float *r) { (void)c; (void)o; (void)x; (void)z; FAILS(0x1B1240); *r = 0.5f; return 0; }
W(w_001B12B0)(void *c, float t, float cur, float s, float *r) { (void)c; (void)cur; (void)s; FAILS(0x1B12B0); *r = t; return 0; }
W(c_record)(void *c, uint32_t cb, EmAreaScript *h, unsigned char *rec, int32_t *r)
{
    (void)c; (void)h; (void)rec; FAILS(cb);
    *r = ++fx.callback_calls >= 2;
    return 0;
}
W(w_001B7D60)(void *c, uint8_t *hs, const unsigned char *rec, int32_t *r)
{
    (void)c; FAILS(0x1B7D60);
    uint32_t sub = em_script_u32(rec, 8);
    assert(sub <= 1);
    if (*hs == 0) { *hs = 1; *r = sub == 1 && !em_script_u32(rec, 0x1C); }
    else *r = 1;
    return 0;
}
W(w_001C67E0)(void *c, uint32_t a, int16_t clip, float x, float y) { (void)c; (void)a; (void)clip; (void)x; (void)y; FAILS(0x1C67E0); return 0; }
W(w_001B0250)(void *c) { (void)c; FAILS(0x1B0250); return 0; }
W(w_0021B9A0)(void *c, int m, float s, float b) { (void)c; (void)m; (void)s; (void)b; FAILS(0x21B9A0); return 0; }
W(w_001D2830)(void *c, int a, int b) { (void)c; (void)a; (void)b; FAILS(0x1D2830); return 0; }
W(w_0018CBD0)(void *c, uint32_t cam, uint32_t a, float f) { (void)c; (void)cam; (void)a; (void)f; FAILS(0x18CBD0); return 0; }
W(w_0018D7B0)(void *c, uint32_t cam, int a) { (void)c; (void)cam; (void)a; FAILS(0x18D7B0); return 0; }
W(w_001B0460)(void *c, int a) { (void)c; (void)a; FAILS(0x1B0460); return 0; }
W(w_001FBC50)(void *c) { (void)c; FAILS(0x1FBC50); return 0; }
W(w_001FABB0)(void *c) { (void)c; FAILS(0x1FABB0); fx.busy = 0; return 0; }
W(w_001AED80)(void *c, uint8_t a) { (void)c; (void)a; FAILS(0x1AED80); fx.fade = 0; return 0; }
W(w_001AEDB0)(void *c, uint8_t a) { (void)c; (void)a; FAILS(0x1AEDB0); fx.fade = 2; return 0; }
W(w_001B1380)(void *c, const float *a, const float *b, float y, int32_t *r) { (void)c; (void)a; (void)b; (void)y; FAILS(0x1B1380); *r = 0; return 0; }
W(w_001B1470)(void *c, float a, float *r) { (void)c; FAILS(0x1B1470); *r = a; return 0; }
W(w_00182BF0)(void *c, uint32_t a, int32_t *r) { (void)c; (void)a; FAILS(0x182BF0); *r = 0; return 0; }
W(w_001B0C00)(void *c, int a) { (void)c; assert(a == 8); FAILS(0x1B0C00); fx.fade = 2; return 0; }
W(w_001B6250)(void *c, uint32_t a) { (void)c; assert(a == EM_AREA_SCRIPT_D_00810E40); FAILS(0x1B6250); return 0; }

static EmAreaScriptWorkers all_workers(void)
{
    EmAreaScriptWorkers k = {0};
#define B(name) k.name = name
    B(r_0028A490); B(r_track_head); B(r_player_bone_C0);
    B(w_001AEB60); B(w_001AEBA0); B(w_001AEDE0); B(w_001AEE10);     B(w_001FD4C0); B(w_00119828); B(w_001D2610); B(w_001D25F0); B(w_001CA770);
    B(w_001FAE70); B(w_001CA700); B(w_001D06D0); B(w_001DD980); B(w_0011E2A8);
    B(w_001C6120); B(w_0022EC30); B(w_00182F90); B(w_001B1240); B(w_001B12B0);
    B(c_record); B(w_001B7D60); B(w_001C67E0); B(w_001B0250); B(w_0021B9A0);
    B(w_001D2830); B(w_0018CBD0); B(w_0018D7B0); B(w_001B0460); B(w_001FBC50);
    B(w_001FABB0); B(w_001AED80); B(w_001AEDB0); B(w_001B1380); B(w_001B1470);
    B(w_00182BF0); B(w_001B0C00); B(w_001B6250);
#undef B
    return k;
}

static EmAreaScriptWorld world(void)
{
    EmAreaScriptWorld w = {
        &fx.s3B84, &fx.s3B8D, &fx.s3B8F, &fx.s3B91, &fx.s3B92, fx.s3600,
        &fx.d6EF, &fx.d6F3, &fx.d6F4, fx.activity, fx.flags, fx.counters, &fx.d78F,
        &fx.e1, &fx.e2, &fx.e3, &fx.e4, &fx.e6,
        &fx.cam0C, fx.cam10, fx.cam20, &fx.cam50, &fx.cam54, &fx.cam6E, &fx.cam70,
        &fx.cam74, &fx.cam78, &fx.camA0,
        fx.w5D0, fx.w5E0, fx.w5F0,
        &fx.m0, &fx.m4, &fx.mC, &fx.m8,
        &fx.p040, fx.p0A0, fx.p0B0, fx.p0C0, &fx.p1F2, &fx.p1F4, &fx.p1F8, &fx.p200,
        &fx.p20C, &fx.p25C, &fx.p2F3, &fx.p2FF,
        0x7A8830u, &fx.s040, fx.s0B0, fx.s0C0,
        &fx.fade, &fx.busy, &fx.cue, &fx.cue_flag, fx.table, 9,
    };
    return w;
}

static void reset_fixture(void)
{
    memset(&fx, 0, sizeof fx);
    fx.s3B8F = 1;
    fx.p2FF = 0x3B;
    fx.p200 = 0x1000;          /* animation already at its end */
    for (int i = 0; i < 9; ++i) fx.table[i] = (int16_t)i;
    fx.counters[0x3B] = 1;     /* director beat 0's op06 sub2 wait */
}

/* One environment step before each tick: the message block completes,
 * the stream cue handshake clears. */
static void environment(void)
{
    if (fx.m4 == 1) fx.m4 = 2;
    if (fx.cue_flag) fx.cue_flag = 0;
}

static unsigned char *load_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "area_script_test: cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); *size = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *data = malloc((size_t)*size);
    assert(data && fread(data, 1, (size_t)*size, f) == (size_t)*size);
    fclose(f);
    return data;
}

/* Run one script to its end; returns the final result and tick count. */
static int run(EmScriptImage *image, uint32_t entry, const EmAreaScriptWorkers *k,
               int skip_tick, int *ticks)
{
    EmAreaScriptWorld w = world();
    EmAreaScript h;
    em_area_script_init(&h, image, &w, k);
    assert(em_area_script_start(&h, entry) == 0);
    for (int t = 0; t < 5000; ++t) {
        environment();
        if (skip_tick >= 0 && t >= skip_tick && fx.s3B91 == 1) fx.s3B91 = 2;
        int r = em_area_script_tick(&h);
        if (r != 0) { *ticks = t + 1; return r == -1 ? -(int)h.fault_address - 1 : r; }
    }
    assert(!"script did not finish");
    return 0;
}

static void finished_clean(uint32_t entry, int result)
{
    if ((result != 1 && result != 3) || fx.w5F0[1] != -1.0f) {
        fprintf(stderr, "script %#x ended %d\n", entry, result);
        assert(0);
    }
    assert(fx.s3B8D == 0 && fx.s3B92 == 0 && fx.s3B91 == 0);
    assert(fx.w5F0[0] == 0.0f && fx.w5F0[1] == -1.0f && fx.w5F0[2] == 0.0f && fx.w5F0[3] == 1.0f);
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s roger/programs.emsc elevator.emsc panel/scripts.emsc AREA11.BIN\n",
                argv[0]);
        return 2;
    }
    EmScriptImage roger = {0}, elevator = {0}, panel = {0};
    assert(em_script_image_load(&roger, argv[1]) && roger.base == 0x8283D0);
    assert(em_script_image_load(&elevator, argv[2]) && elevator.base == 0x82A750);
    assert(em_script_image_load(&panel, argv[3]) && panel.base == 0x246F20);
    long size;
    unsigned char *overlay = load_file(argv[4], &size);
    assert(size >= 0x82AB10 - 0x823500);
    EmScriptImage area = {overlay, 0x823500, 0x8292C0, (uint32_t)size};
    const EmAreaScriptWorkers k = all_workers();
    int ticks, checked = 0;

    /* Every admitted level script runs to its end. */
    const struct { EmScriptImage *image; uint32_t entry; int teardown; } level[] = {
        {&area, 0x8292C0, 1}, {&elevator, 0x82A990, 1}, {&elevator, 0x82A750, 1},
        {&area, 0x8294C0, 1}, {&area, 0x829A40, 1}, {&area, 0x829CC0, 1},
        {&area, 0x829E80, 1},
        {&roger, 0x8283D0, 1}, {&roger, 0x828810, 1}, {&roger, 0x828A10, 1},
        {&roger, 0x828990, 0}, {&panel, 0x246F20, 1}, {&panel, 0x2477A0, 0},
        {&panel, 0x247BE0, 1}, {&panel, 0x247DA0, 1},
    };
    for (size_t i = 0; i < sizeof level / sizeof level[0]; ++i) {
        reset_fixture();
        /* Scripts without their own enter run inside a scripted frame
         * another script or owner took (3B8D nonzero). */
        if (level[i].entry == 0x828810 || level[i].entry == 0x828A10 ||
            level[i].entry == 0x247BE0 || level[i].entry == 0x247DA0) fx.s3B8D = 3;
        int r = run(level[i].image, level[i].entry, &k, -1, &ticks);
        if (level[i].teardown) finished_clean(level[i].entry, r);
        else assert(r == 1);
        ++checked;
    }

    /* Skippable scripts abort through the canonical 3B91 byte. */
    const uint32_t skippable[] = {0x8294C0, 0x829A40, 0x829CC0, 0x829E80};
    for (size_t i = 0; i < 4; ++i) {
        reset_fixture();
        int r = run(&area, skippable[i], &k, 3, &ticks);
        assert(r == 3);
        finished_clean(skippable[i], r);
        ++checked;
    }
    reset_fixture();
    assert(run(&roger, 0x8283D0, &k, 4, &ticks) == 3);
    ++checked;

    /* Fail-stop: each reached worker, removed, faults at its address. */
    const struct { size_t offset; uint32_t address; uint32_t entry; EmScriptImage *image; } missing[] = {
        {offsetof(EmAreaScriptWorkers, w_001DD980), 0x1DD980, 0x8292C0, &area},
        {offsetof(EmAreaScriptWorkers, w_00182F90), 0x182F90, 0x8292C0, &area},
        {offsetof(EmAreaScriptWorkers, w_001B7D60), 0x1B7D60, 0x82A990, &elevator},
        {offsetof(EmAreaScriptWorkers, w_0011E2A8), 0x11E2A8, 0x8292C0, &area},
        {offsetof(EmAreaScriptWorkers, w_001CA700), 0x1CA700, 0x8294C0, &area},
        {offsetof(EmAreaScriptWorkers, c_record), 0x1B99F0, 0x82A750, &elevator},
        {offsetof(EmAreaScriptWorkers, w_00182BF0), 0x182BF0, 0x8283D0, &roger},
        {offsetof(EmAreaScriptWorkers, w_001B1380), 0x1B1380, 0x828990, &roger},
    };
    for (size_t i = 0; i < sizeof missing / sizeof missing[0]; ++i) {
        EmAreaScriptWorkers partial = k;
        memset((char *)&partial + missing[i].offset, 0, sizeof(void (*)(void)));
        reset_fixture();
        int r = run(missing[i].image, missing[i].entry, &partial, -1, &ticks);
        assert(r == -(int)missing[i].address - 1);
        ++checked;
    }
    /* A failing worker faults; the fault latches and nothing runs after it. */
    reset_fixture();
    fx.failing = 0x1AEB60;
    {
        EmAreaScriptWorld w = world();
        EmAreaScript h;
        em_area_script_init(&h, &area, &w, &k);
        assert(em_area_script_start(&h, 0x8292C0) == 0);
        assert(em_area_script_tick(&h) == -1 && h.faulted && h.fault_address == 0x1AEB60);
        int calls = fx.calls;
        fx.failing = 0;
        assert(em_area_script_tick(&h) == -1 && fx.calls == calls);
        /* A NULL canonical 3B92 faults before any write. */
        reset_fixture();
        w.spad3B92 = NULL;
        em_area_script_init(&h, &area, &w, &k);
        assert(em_area_script_start(&h, 0x8292C0) == 0);
        assert(em_area_script_tick(&h) == -1 && h.fault_address == 0x70003B92);
        assert(fx.calls == 0 && fx.s3B8D == 0);
        /* Entries outside the image are refused; inactive blocks finish. */
        em_area_script_init(&h, &roger, &w, &k);
        assert(em_area_script_start(&h, 0x8292C0) == -1);
        assert(em_area_script_tick(&h) == 1);
        checked += 3;
    }
    /* The 0011E2A8 binding refuses arguments past its translated range
     * (|x| > 0x4016CBE3) and a NULL result without writing; the worker form
     * is the same function. Its values are checked against the original in
     * tools/test_area_script_reference.py. */
    {
        float out = 7.0f, a = 0.0f, b = 0.0f;
        uint32_t edge = 0x4016CBE3u, past = 0x4016CBE4u;
        float x_edge, x_past;
        memcpy(&x_edge, &edge, 4);
        memcpy(&x_past, &past, 4);
        assert(em_area_script_sin_0011E2A8(x_past, &out) == -1 && out == 7.0f);
        assert(em_area_script_sin_0011E2A8(-x_past, &out) == -1 && out == 7.0f);
        assert(em_area_script_sin_0011E2A8(0.5f, NULL) == -1);
        const float xs[] = {0.0f, -0.0f, 0.5f, -1.5707964f, 1.5707964f, x_edge, -x_edge};
        for (size_t i = 0; i < sizeof xs / sizeof xs[0]; ++i) {
            assert(em_area_script_sin_0011E2A8(xs[i], &a) == 0);
            assert(em_area_script_w_0011E2A8(NULL, xs[i], &b) == 0 && memcmp(&a, &b, 4) == 0);
        }
        checked += 2;
    }

    em_script_image_free(&roger);
    em_script_image_free(&elevator);
    em_script_image_free(&panel);
    free(overlay);
    printf("area script host: %d fixture checks passed\n", checked);
    return 0;
}
