/* Behavioural checks of the translated 001551B0 / 00156620 owners. The
 * instruction-level comparison lives in tools/test_crate_original_reference.py
 * and tools/test_drum_original_reference.py; this test pins the AREA11
 * shapes and the fault contract without the ELF. */
#include "game/em_crate_original.h"
#include "game/em_drum_original.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char log[64][24];
    unsigned count;
    int probe_result, probe_actor, segment_result, fail;
    int queue[8], queued, next;
    uint32_t random_value;
    uint8_t visible;
} Fixture;

static void note(Fixture *f, const char *text)
{
    assert(f->count < 64);
    snprintf(f->log[f->count++], sizeof f->log[0], "%s", text);
}

static int seen(const Fixture *f, const char *text)
{
    for (unsigned i = 0; i < f->count; ++i) if (!strcmp(f->log[i], text)) return 1;
    return 0;
}

#define F ((Fixture *)context)
static int allocate_model(void *context) { note(F, "allocate"); return 0; }
static int bone_init(void *context) { note(F, "bone_init"); return 0; }
static int publish(void *context) { note(F, "publish"); return 0; }
static int place(void *context, float world[16])
{
    note(F, "place");
    em_crate_sdk_identity(world);
    return 0;
}
static int probe(void *context, float position[4], const float from[3], float dy,
                 uint32_t mode, EmCrateProbe *out)
{
    (void)position; (void)from; (void)dy; (void)mode;
    note(F, "probe");
    out->result = F->next < F->queued ? F->queue[F->next++] : F->probe_result;
    out->actor = out->result == 2 ? F->probe_actor : 0;
    return 0;
}
static int random_value(void *context, uint32_t *value) { note(F, "random"); *value = F->random_value; return 0; }
static int sound(void *context, uint16_t id)
{
    char text[24]; snprintf(text, sizeof text, "sound %X", id); note(F, text);
    return F->fail;
}
static int sound3d(void *context, uint16_t id, int32_t mode, float radius)
{
    (void)mode; (void)radius;
    char text[24]; snprintf(text, sizeof text, "sound3d %X", id); note(F, text); return 0;
}
static int crate_effect(void *context, uint32_t id, const float p[4], const float r[4])
{
    (void)p; (void)r;
    char text[24]; snprintf(text, sizeof text, "effect %X", id); note(F, text); return 0;
}
static int taken(void *context, uint8_t puid) { (void)puid; note(F, "taken"); return 0; }
static int spawn(void *context, const EmCrateChild *child) { (void)child; note(F, "spawn"); return 1; }
static int rebind(void *context, uint16_t model)
{
    char text[24]; snprintf(text, sizeof text, "rebind %X", model); note(F, text); return 0;
}
static int bone_matrix(void *context, const float world[16]) { (void)world; note(F, "bone_matrix"); return 0; }
static int draw(void *context) { note(F, "draw"); return 0; }
static int set_taken(void *context, uint8_t puid) { (void)puid; note(F, "set_taken"); return 0; }
static int release(void *context) { note(F, "free"); return 0; }
static int segment(void *context, const float a[3], const float b[3], int32_t mask, int32_t excl)
{
    (void)a; (void)b; (void)mask; (void)excl; note(F, "segment"); return F->segment_result;
}
static int effect_matrix(void *context, int32_t preset, const float m[16]) { (void)preset; (void)m; note(F, "effect_matrix"); return 0; }
static int contact(void *context) { note(F, "contact"); return 0; }
static int visibility(void *context, uint8_t *visible) { note(F, "visibility"); *visible = F->visible; return 0; }
static int drum_effect(void *context, uint32_t id, const float p[4])
{
    (void)p; char text[24]; snprintf(text, sizeof text, "effect %X", id); note(F, text); return 0;
}
static int sweep(void *context, const float p[3], uint32_t mode) { (void)p; (void)mode; note(F, "sweep"); return 0; }
static int hull(void *context, const float m[16]) { (void)m; note(F, "hull"); return 0; }
#undef F

static EmCrateOriginalHooks crate_hooks(Fixture *f)
{
    return (EmCrateOriginalHooks){f, allocate_model, bone_init, publish, place, probe,
        random_value, sound, sound3d, crate_effect, taken, spawn, rebind, bone_matrix,
        draw, set_taken, release};
}

static EmDrumOriginalHooks drum_hooks(Fixture *f)
{
    return (EmDrumOriginalHooks){f, allocate_model, bone_init, place, segment, effect_matrix, draw,
        contact, visibility, drum_effect, sound, random_value, sweep, probe, sound3d, hull,
        release};
}

static EmCrateOriginal area11_crate(float y)
{
    EmCrateOriginal c = {0};
    c.status = 1; c.model = 6; c.state = 4; c.health = 1; c.link = -1; c.placement = 0x700;
    c.position[0] = 214.7f; c.position[1] = y; c.position[2] = 292.8f; c.position[3] = 1.0f;
    em_crate_sdk_identity(c.world);
    memcpy(c.world + 12, c.position, sizeof c.position);
    memcpy(c.rest, c.world, sizeof c.rest);
    return c;
}

static void crate_cases(void)
{
    Fixture f = {0};
    EmCrateOriginalHooks hooks = crate_hooks(&f);
    EmCrateOriginal shot = area11_crate(189.8f), above = area11_crate(203.8f);
    above.raised = 1;
    EmCrateListEntry list[2] = {{6, 0, &shot.alarm}, {6, 1, &above.alarm}};
    EmCrateInput in = {.area = 11, .actors = list, .actor_count = 2};

    /* A missing worker faults before any write. */
    EmCrateOriginalHooks partial = hooks;
    partial.rebind = NULL;
    EmCrateOriginal before = shot;
    shot.damage = 1;
    before.damage = 1;
    assert(em_crate_original_tick(&shot, &in, &partial) == EM_CRATE_FAULT);
    assert(!memcmp(&shot, &before, sizeof shot) && f.count == 0);

    /* 00155504: damage breaks it and wakes only the raised box. */
    assert(em_crate_original_tick(&shot, &in, &hooks) == EM_CRATE_ALIVE);
    assert(shot.state == 2 && shot.status == 2 && !shot.alarm && above.alarm == 1);
    assert(!strcmp(f.log[0], "bone_matrix") && !strcmp(f.log[1], "draw") &&
           !strcmp(f.log[2], "publish") && f.count == 3);

    /* 00155FC0: sound 0x19D, two effects, quarter turn, husk 0x22. */
    f.count = 0; f.random_value = 0;
    assert(em_crate_original_tick(&shot, &in, &hooks) == EM_CRATE_ALIVE);
    assert(seen(&f, "sound 19D") && seen(&f, "effect 8000000A") && seen(&f, "effect 80000015") &&
           seen(&f, "rebind 22") && seen(&f, "bone_init") && seen(&f, "random"));
    assert(shot.state == 2 && shot.break_phase == 1 && shot.timer == 6);
    /* The husk of a ground box never frees: +0x52 is 0. */
    for (int i = 0; i < 100; ++i) {
        f.count = 0;
        assert(em_crate_original_tick(&shot, &in, &hooks) == EM_CRATE_ALIVE);
        assert(f.count == 2 && !strcmp(f.log[1], "draw"));
    }
    assert(shot.state == 2 && shot.timer == 6);

    /* 00155584: the woken box re-probes; four supported corners hold it
     * and it returns to rest when +0x2A reaches 0. */
    f.count = 0; f.probe_result = 2; f.probe_actor = 0x1234;
    assert(em_crate_original_tick(&above, &in, &hooks) == EM_CRATE_ALIVE);
    assert(above.state == 1 && above.timer == 6 && !above.alarm);
    for (int i = 0; i < 6; ++i) {
        f.count = 0;
        assert(em_crate_original_tick(&above, &in, &hooks) == EM_CRATE_ALIVE);
        assert(seen(&f, "publish") && seen(&f, "probe"));
    }
    assert(above.state == 4 && above.timer == 0 && above.fall_phase == 0);

    /* One supporting corner: tips about X for 28 steps, then falls
     * ballistically and breaks on world ground (no damage -> no husk). */
    EmCrateOriginal tipping = above;
    tipping.alarm = 1;
    f.queue[0] = 2; f.queued = 5; f.next = 0; f.probe_result = 0;   /* c0 + 3 free + centre */
    assert(em_crate_original_tick(&tipping, &in, &hooks) == EM_CRATE_ALIVE && tipping.state == 1);
    f.next = 0; f.count = 0;
    assert(em_crate_original_tick(&tipping, &in, &hooks) == EM_CRATE_ALIVE);
    assert(tipping.fall_phase == 1 && tipping.tilt == 29 && tipping.spin[2] < 0.0f);
    assert(tipping.timer == (int16_t)(1019 - 1));   /* model 6: 60 * y / 12, then -1 */
    int ticks = 0;
    while (tipping.tilt >= 2) {
        f.count = 0;
        assert(em_crate_original_tick(&tipping, &in, &hooks) == EM_CRATE_ALIVE);
        ++ticks;
    }
    assert(ticks == 28 && tipping.state == 1);
    f.queued = 0; f.probe_result = 4; f.count = 0;
    assert(em_crate_original_tick(&tipping, &in, &hooks) == EM_CRATE_ALIVE && tipping.state == 2);
    f.count = 0;
    assert(em_crate_original_tick(&tipping, &in, &hooks) == EM_CRATE_ALIVE);
    assert(tipping.state == 3 && !seen(&f, "rebind 22") && seen(&f, "sound 19D"));
    f.count = 0;
    assert(em_crate_original_tick(&tipping, &in, &hooks) == EM_CRATE_FREED && seen(&f, "free"));

    /* Nothing supports it: a random euler kick and no tilt frames. */
    above.alarm = 1; f.probe_result = 0; f.random_value = 0x40000000; f.count = 0;
    assert(em_crate_original_tick(&above, &in, &hooks) == EM_CRATE_ALIVE);
    f.count = 0;
    assert(em_crate_original_tick(&above, &in, &hooks) == EM_CRATE_ALIVE);
    assert(above.fall_phase == 1 && above.tilt == 0 && above.velocity[1] < 0.0f);

    /* A worker fault propagates. */
    f.count = 0;
    EmCrateOriginal again = area11_crate(189.8f);
    again.state = 2; f.fail = -1;
    assert(em_crate_original_tick(&again, &in, &hooks) == EM_CRATE_FAULT);
    f.fail = 0;

    /* The rattle ints overlay step[2]/step[3]. */
    em_crate_original_set_rattle(&again, -1, 5);
    assert(em_crate_original_rattle_wait(&again) == -1 && em_crate_original_rattle_row(&again) == 5);
}

static void drum_cases(void)
{
    Fixture f = {0};
    EmDrumOriginalHooks hooks = drum_hooks(&f);
    EmDrumOriginal d = {0};
    d.status = 1; d.model = 0x18; d.state = 1; d.health = 1;
    d.position[0] = 300.0f; d.position[1] = 249.7f; d.position[2] = 327.9f; d.position[3] = 1.0f;
    EmDrumInput in = {.area = 11, .player = {300.0f, 249.7f, 400.0f, 1.0f}};

    EmDrumOriginalHooks partial = hooks;
    partial.hull = NULL;
    assert(em_drum_original_tick(&d, &in, &partial) == EM_DRUM_FAULT && f.count == 0);

    /* Outside 50 units the owner defers to 001B17A0 (visibility publish). */
    f.visible = 0;
    assert(em_drum_original_tick(&d, &in, &hooks) == EM_DRUM_ALIVE);
    assert(seen(&f, "visibility") && !seen(&f, "contact") && d.visible == 0);
    /* Inside it forces +0x01 = 1 and pushes the class-4 list directly. */
    f.count = 0; in.player[2] = 350.0f;
    assert(em_drum_original_tick(&d, &in, &hooks) == EM_DRUM_ALIVE);
    assert(seen(&f, "contact") && !seen(&f, "visibility") && d.visible == 1);

    /* Hit: flat preset-4 effect under it, then FX + 0x1A1, freed two ticks later. */
    f.count = 0; f.segment_result = 1; d.damage = 1;
    assert(em_drum_original_tick(&d, &in, &hooks) == EM_DRUM_ALIVE);
    assert(d.state == 2 && d.status == 2 && seen(&f, "effect_matrix"));
    f.count = 0;
    assert(em_drum_original_tick(&d, &in, &hooks) == EM_DRUM_ALIVE);
    assert(seen(&f, "effect 80000013") && seen(&f, "effect 8000001C") && seen(&f, "sound 1A1"));
    assert(d.phase == 1 && d.timer == 2);
    assert(em_drum_original_tick(&d, &in, &hooks) == EM_DRUM_ALIVE && d.state == 2);
    assert(em_drum_original_tick(&d, &in, &hooks) == EM_DRUM_ALIVE && d.state == 3);
    f.count = 0;
    assert(em_drum_original_tick(&d, &in, &hooks) == EM_DRUM_FREED && seen(&f, "free"));
    assert(!seen(&f, "sweep") && !seen(&f, "probe"));
}

int main(void)
{
    crate_cases();
    drum_cases();
    puts("crate/drum original owners: PASS");
    return 0;
}
