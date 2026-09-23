/* Unit test for em_fan_original (overlay behaviour 0x827630).
 * Behaviour equality with the original instructions is established by
 * tools/test_fan_original_reference.py; this test pins the native contract:
 * fail-stop faults, the worker protocol and the cycle's timing. */
#include <stdio.h>
#include <string.h>

#include "game/em_fan_original.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); failures++; } } while (0)

typedef struct {
    int init_result, fail;       /* fail: which worker returns -1 (0 none) */
    int init, sound, matrix, publish, exit, draw, free_;
    int exit_args[3];
    float sound_range;
    int sound_cue;
} Log;

static int w_init(void *c, const EmFanOriginal *f) { Log *l = c; (void)f; l->init++; return l->fail == 1 ? -1 : l->init_result; }
static int w_sound(void *c, int32_t cue, int32_t a2, float r)
{
    Log *l = c; (void)a2; l->sound++; l->sound_cue = cue; l->sound_range = r;
    return l->fail == 2 ? -1 : 0;
}
static int w_matrix(void *c, const EmFanOriginal *f) { Log *l = c; (void)f; l->matrix++; return l->fail == 3 ? -1 : 0; }
static int w_publish(void *c) { Log *l = c; l->publish++; return l->fail == 4 ? -1 : 1; }
static int w_exit(void *c, int32_t a, int32_t b, int32_t d)
{
    Log *l = c; l->exit++; l->exit_args[0] = a; l->exit_args[1] = b; l->exit_args[2] = d;
    return l->fail == 5 ? -1 : 0;
}
static int w_draw(void *c) { Log *l = c; l->draw++; return l->fail == 6 ? -1 : 0; }
static int w_free(void *c) { Log *l = c; l->free_++; return l->fail == 7 ? -1 : 0; }

static EmFanOriginalWorkers workers(Log *l)
{
    EmFanOriginalWorkers w = { l, w_init, w_sound, w_matrix, w_publish, w_exit, w_draw, w_free };
    return w;
}

static void test_init_and_free(void)
{
    Log l = {0};
    EmFanOriginalWorkers w = workers(&l);
    EmFanOriginalFault fault = {0};
    EmFanOriginal fan;
    EmFanOriginalGlobals g = {0};
    em_fan_original_spawn(&fan, 1, 0.0f);
    CHECK(em_fan_original_tick(&fan, NULL, &g, &w, &fault) == 1);
    CHECK(fan.lifecycle == 1 && l.init == 1 && fan.rot_z < 0.0f && fan.spin == 0.0f);

    em_fan_original_spawn(&fan, 0, 0.0f);
    l.init_result = 1; /* over the bone cap: 001B0EA0 stores 3 */
    CHECK(em_fan_original_tick(&fan, NULL, &g, &w, &fault) == 1);
    CHECK(fan.lifecycle == 3 && fan.rot_z > 0.0f);
    CHECK(em_fan_original_tick(&fan, NULL, &g, &w, &fault) == 0);
    CHECK(fan.freed == 1 && l.free_ == 1);
    /* A tick after the free faults instead of running a freed record. */
    CHECK(em_fan_original_tick(&fan, NULL, &g, &w, &fault) == -1);
    CHECK(fault.code == EM_FAN_FAULT_BAD_INDEX && fault.address == EM_FAN_ORIGINAL_CALLBACK);
    CHECK(l.free_ == 1);

    /* 001B0FD0 results other than 0/1 fault. */
    EmFanOriginalFault f2 = {0};
    em_fan_original_spawn(&fan, 0, 0.0f);
    l.init_result = 2;
    CHECK(em_fan_original_tick(&fan, NULL, &g, &w, &f2) == -1);
    CHECK(f2.code == EM_FAN_FAULT_BAD_RESULT && f2.address == 0x001B0FD0u);

    /* lifecycle >= 4: no call at all. */
    Log quiet = {0};
    EmFanOriginalWorkers qw = workers(&quiet);
    EmFanOriginalFault f3 = {0};
    em_fan_original_spawn(&fan, 0, 0.0f);
    fan.lifecycle = 4;
    CHECK(em_fan_original_tick(&fan, NULL, NULL, &qw, &f3) == 1);
    CHECK(quiet.init + quiet.matrix + quiet.draw + quiet.free_ == 0 && f3.code == 0);
}

/* One full cycle from spawn: the phase lengths follow the original counts
 * (phase 1 runs 60 ticks, phase 3 runs 30 ticks), the sound fires once for
 * flags2 0 and never for flags2 1, and the cycle returns to phase 0. */
static void test_cycle(uint16_t flags2)
{
    Log l = {0};
    EmFanOriginalWorkers w = workers(&l);
    EmFanOriginalFault fault = {0};
    EmFanOriginal fan;
    EmFanOriginalGlobals g = {0, 0, 0xFF, 0};
    EmFanOriginalPlayer p;
    memset(&p, 0, sizeof p);
    em_fan_original_spawn(&fan, flags2, 0.0f);
    CHECK(em_fan_original_tick(&fan, &p, &g, &w, &fault) == 1); /* init */
    int ticks[5] = {0}, t = 0, wrapped = 0;
    for (t = 0; t < 2000 && !wrapped; t++) {
        uint8_t before = fan.phase;
        CHECK(em_fan_original_tick(&fan, &p, &g, &w, &fault) == 1);
        ticks[before]++;
        if (before == 4 && fan.phase == 0)
            wrapped = 1;
        CHECK(fan.rot_z > -3.1415927f && fan.rot_z <= 3.1415927f);
    }
    CHECK(wrapped && fault.code == 0);
    CHECK(ticks[0] == 1 && ticks[1] == 60 && ticks[3] == 30);
    CHECK(ticks[2] > 0 && ticks[4] > 0);
    CHECK(l.sound == (flags2 == 0));
    if (flags2 == 0)
        CHECK(l.sound_cue == 0x451 && l.sound_range == 300.0f);
    CHECK(l.matrix == t && l.draw == t && l.exit == 0);
    CHECK(fan.spin == 0.0f);
}

static void test_box(void)
{
    Log l = {0};
    EmFanOriginalWorkers w = workers(&l);
    EmFanOriginal fan;
    EmFanOriginalPlayer p;
    memset(&p, 0, sizeof p);
    p.pos[0] = 330.0f; p.pos[1] = 300.0f; p.pos[2] = 150.0f; p.b00 = 1;

    /* Slow spin, D_00810758 == 0xFF: the area-change request (1,1,4). */
    EmFanOriginalGlobals g = {0, 0, 0xFF, 0x01};
    EmFanOriginalFault fault = {0};
    em_fan_original_spawn(&fan, 1, 0.0f);
    fan.lifecycle = 1; fan.phase = 5;
    CHECK(em_fan_original_tick(&fan, &p, &g, &w, &fault) == 1);
    CHECK(l.exit == 1 && l.exit_args[0] == 1 && l.exit_args[1] == 1 && l.exit_args[2] == 4);
    CHECK(l.publish == 0 && g.d8107D8 == 0x01);

    /* Otherwise the Roger bit D_008107D8 |= 0x80, no request. */
    g.d810758 = 0;
    CHECK(em_fan_original_tick(&fan, &p, &g, &w, &fault) == 1);
    CHECK(l.exit == 1 && g.d8107D8 == 0x81);

    /* A pending request (B8 != 0) skips the box. */
    g.d8106B8 = 1; g.d8107D8 = 0;
    CHECK(em_fan_original_tick(&fan, &p, &g, &w, &fault) == 1);
    CHECK(g.d8107D8 == 0);

    /* Fast spin, 156 <= Z < 166.5: the hit writes. */
    g.d8106B8 = 0;
    fan.spin = 0.2f; p.pos[2] = 160.0f;
    CHECK(em_fan_original_tick(&fan, &p, &g, &w, &fault) == 1);
    CHECK(l.publish == 1 && p.b00 == 3 && p.b0F == 6 && p.f224 == 5.0f);
    CHECK(p.f70[0] == 0.0f && p.f70[1] == 0.0f && p.f70[2] == 1.0f && p.f70[3] == 1.0f);
    /* The fast arm needs player +0x00 == 1: now 3, nothing more happens. */
    p.b0F = 0;
    CHECK(em_fan_original_tick(&fan, &p, &g, &w, &fault) == 1);
    CHECK(p.b0F == 0 && fault.code == 0);
}

static void expect_fault(int which, uint16_t flags2, uint8_t phase, float spin, float z,
                         uint32_t address, int32_t code, int null_worker)
{
    Log l = {0};
    EmFanOriginalWorkers w = workers(&l);
    if (null_worker) {
        switch (which) {
        case 2: w.w_001FBD50 = NULL; break;
        case 3: w.w_001C6380 = NULL; break;
        case 4: w.w_001B17A0 = NULL; break;
        case 5: w.w_001B0C60 = NULL; break;
        case 6: w.w_draw_4C = NULL; break;
        default: break;
        }
    } else {
        l.fail = which;
    }
    EmFanOriginal fan;
    EmFanOriginalPlayer p;
    memset(&p, 0, sizeof p);
    p.b00 = 1; p.pos[0] = 330.0f; p.pos[1] = 300.0f; p.pos[2] = z;
    EmFanOriginalGlobals g = {0, 0, 0xFF, 0};
    EmFanOriginalFault fault = {0};
    em_fan_original_spawn(&fan, flags2, 0.0f);
    fan.lifecycle = 1; fan.phase = phase; fan.timer = 1; fan.spin = spin;
    CHECK(em_fan_original_tick(&fan, &p, &g, &w, &fault) == -1);
    CHECK(fault.address == address && fault.code == code);
    /* Latched: the next tick does nothing. */
    int calls = l.init + l.sound + l.matrix + l.publish + l.exit + l.draw + l.free_;
    CHECK(em_fan_original_tick(&fan, &p, &g, &w, &fault) == -1);
    CHECK(calls == l.init + l.sound + l.matrix + l.publish + l.exit + l.draw + l.free_);
}

static void test_faults(void)
{
    for (int null_worker = 0; null_worker < 2; null_worker++) {
        int32_t code = null_worker ? EM_FAN_FAULT_NULL_WORKER : EM_FAN_FAULT_WORKER_FAILED;
        expect_fault(2, 0, 1, 0.0f, 200.0f, 0x001FBD50u, code, null_worker);
        expect_fault(3, 0, 5, 0.0f, 200.0f, 0x001C6380u, code, null_worker);
        expect_fault(4, 1, 5, 0.2f, 200.0f, 0x001B17A0u, code, null_worker);
        expect_fault(5, 1, 5, 0.0f, 150.0f, 0x001B0C60u, code, null_worker);
        expect_fault(6, 0, 5, 0.0f, 200.0f, EM_FAN_ORIGINAL_CALLBACK, code, null_worker);
    }
    /* Missing data views. */
    Log l = {0};
    EmFanOriginalWorkers w = workers(&l);
    EmFanOriginal fan;
    EmFanOriginalFault fault = {0};
    em_fan_original_spawn(&fan, 1, 0.0f);
    fan.lifecycle = 1;
    CHECK(em_fan_original_tick(&fan, NULL, NULL, &w, &fault) == -1);
    CHECK(fault.address == EM_FAN_ORIGINAL_D_00810788 && fault.code == EM_FAN_FAULT_NULL_WORKER);
    EmFanOriginalGlobals g = {0};
    EmFanOriginalFault f2 = {0};
    fan.phase = 5;
    CHECK(em_fan_original_tick(&fan, NULL, &g, &w, &f2) == -1);
    CHECK(f2.address == EM_FAN_ORIGINAL_D_008102B0 && f2.code == EM_FAN_FAULT_NULL_WORKER);
    /* flags2 0 never reads the player. */
    EmFanOriginalFault f3 = {0};
    fan.flags2 = 0;
    CHECK(em_fan_original_tick(&fan, NULL, &g, &w, &f3) == 1 && f3.code == 0);
    /* Free with no worker. */
    EmFanOriginalWorkers none = {0};
    EmFanOriginalFault f4 = {0};
    fan.lifecycle = 2;
    CHECK(em_fan_original_tick(&fan, NULL, NULL, &none, &f4) == -1);
    CHECK(f4.address == 0x001AFC10u && f4.code == EM_FAN_FAULT_NULL_WORKER && !fan.freed);
    CHECK(em_fan_original_tick(&fan, NULL, NULL, NULL, &(EmFanOriginalFault){0}) == -1);
    CHECK(em_fan_original_tick(&fan, NULL, NULL, &w, NULL) == -1);
}

int main(void)
{
    test_init_and_free();
    test_cycle(0);
    test_cycle(1);
    test_box();
    test_faults();
    if (failures) {
        fprintf(stderr, "fan_original_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("fan_original_test: PASS\n");
    return 0;
}
