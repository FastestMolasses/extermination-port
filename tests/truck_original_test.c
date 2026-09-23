/* Native unit test for em_truck_original: fail-stop workers and the
 * complete set-piece timeline. Values that the original-instruction oracle
 * (tools/test_truck_original_reference.py) proves are restated here only
 * as counts and ticks. */
#include "game/em_truck_original.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned binds, placements, poses, hulls, publishes, draws, frees;
    unsigned effects, sounds, rumbles, script_starts, polls;
    int rumble_ticks[8], rumble_ids[8];
    int sound_ticks[4];
    unsigned sound_ids[4];
    int tick, pending, script_done, fail_draw;
    uint32_t script_entry;
    float bounds[6];
} Fixture;

static int bind(void *c, int *pending) { Fixture *f = c; ++f->binds; *pending = f->pending; return 1; }
static int placement(void *c, float m[16])
{
    Fixture *f = c;
    ++f->placements;
    memset(m, 0, 16 * sizeof *m);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    m[12] = 380.8f; m[13] = 164.0f; m[14] = 391.1f;
    return 1;
}
static int pose(void *c, const float m[16]) { (void)m; ++((Fixture *)c)->poses; return 1; }
static int hull(void *c, const float m[16]) { (void)m; ++((Fixture *)c)->hulls; return 1; }
static int bounds(void *c, float out[6]) { memcpy(out, ((Fixture *)c)->bounds, 6 * sizeof *out); return 1; }
static int publish(void *c) { ++((Fixture *)c)->publishes; return 1; }
static int draw(void *c) { Fixture *f = c; ++f->draws; return f->fail_draw ? 0 : 1; }
static int rumble(void *c, int id)
{
    Fixture *f = c;
    assert(f->rumbles < 8);
    f->rumble_ticks[f->rumbles] = f->tick;
    f->rumble_ids[f->rumbles++] = id;
    return 1;
}
static int effect(void *c, uint32_t id, const float p[4])
{
    assert(id == EM_TRUCK_EFFECT_ID && p[3] == 1.0f);
    ++((Fixture *)c)->effects;
    return 1;
}
static int sound(void *c, uint16_t id, float radius)
{
    Fixture *f = c;
    assert(radius == 300.0f && f->sounds < 4);
    f->sound_ticks[f->sounds] = f->tick;
    f->sound_ids[f->sounds++] = id;
    return 1;
}
static int release(void *c) { ++((Fixture *)c)->frees; return 1; }
static int start(void *c, uint32_t entry) { Fixture *f = c; ++f->script_starts; f->script_entry = entry; return 1; }
static int poll(void *c, int *done) { Fixture *f = c; ++f->polls; *done = f->script_done; return 1; }

typedef struct {
    uint8_t story, phase, byte_0a, kind;
    float a0[3], b0[3];
    int32_t carry;
    EmTruckWorld world;
} Canon;

static void canon(Canon *c, int standing)
{
    memset(c, 0, sizeof *c);
    c->byte_0a = 1;
    c->kind = 9;
    c->a0[0] = c->b0[0] = 380.8f; c->a0[1] = c->b0[1] = 175.0f; c->a0[2] = c->b0[2] = 391.1f;
    c->world = (EmTruckWorld){&c->story, &c->phase, &c->byte_0a, standing ? &c->kind : NULL,
                              c->a0, c->b0, &c->carry};
}

static EmTruckHooks hooks(Fixture *f)
{
    return (EmTruckHooks){f, bind, placement, pose, hull, bounds, publish, draw, rumble, effect,
                          sound, release};
}

static void missing_workers(void)
{
    Fixture f = {0};
    Canon c;
    canon(&c, 1);
    EmTruckOriginal truck = {0};
    EmTruckHooks h = hooks(&f);
    assert(em_truck_original_tick(&truck, &c.world, &h) == 1);
    EmTruckHooks broken = h; broken.publish = NULL;
    assert(em_truck_original_tick(&truck, &c.world, &broken) == -1);
    broken = h; broken.draw = NULL;
    assert(em_truck_original_tick(&truck, &c.world, &broken) == -1);
    broken = h; broken.free_owner = NULL;
    assert(em_truck_original_tick(&truck, &c.world, &broken) == -1);
    broken = h; broken.effect = NULL;
    assert(em_truck_original_tick(&truck, &c.world, &broken) == -1);
    broken = h; broken.hull_bounds = NULL;
    assert(em_truck_original_tick(&truck, &c.world, &broken) == -1);
    EmTruckWorld world = c.world; world.carry = NULL;
    assert(em_truck_original_tick(&truck, &world, &h) == -1);
    assert(em_truck_original_tick(NULL, &c.world, &h) == -1);
    f.fail_draw = 1;
    assert(em_truck_original_tick(&truck, &c.world, &h) == -1);

    EmTruckTrigger trigger = {0};
    EmTruckTriggerHooks th = {&f, start, poll, release};
    th.script_tick = NULL;
    assert(em_truck_trigger_tick(&trigger, &c.world, &th) == -1);
}

static void set_piece(void)
{
    Fixture f = {0};
    Canon c;
    canon(&c, 1);
    const float box[6] = {370, 150, 380, 392, 180, 402};
    memcpy(f.bounds, box, sizeof box);
    EmTruckOriginal truck = {0};
    truck.position[0] = 380.8f; truck.position[1] = 164.0f; truck.position[2] = 391.1f;
    EmTruckHooks h = hooks(&f);

    f.pending = 1;
    assert(em_truck_original_tick(&truck, &c.world, &h) == 1 && truck.state == 0 && !f.placements);
    f.pending = 0;
    assert(em_truck_original_tick(&truck, &c.world, &h) == 1 && truck.state == 4);
    assert(f.placements == 1 && f.hulls == 1 && !f.draws);
    assert(!memcmp(truck.matrix, truck.rest_matrix, sizeof truck.matrix) && truck.rest_y == 164.0f);

    int arm = -1, fall = -1, rest = -1;
    for (f.tick = 0; f.tick < 200; ++f.tick) {
        assert(em_truck_original_tick(&truck, &c.world, &h) == 1);
        if (arm < 0 && truck.shake > 0) arm = f.tick;
        if (fall < 0 && truck.state == 1) fall = f.tick;
        if (rest < 0 && truck.state == 2) rest = f.tick;
    }
    assert(arm == 0 && fall == 46 && rest == 166);
    assert(c.story == 0xFF && truck.frame == 120);
    assert(f.effects == 32 && f.sounds == 2 && f.rumbles == 3);
    assert(f.sound_ids[0] == EM_TRUCK_SOUND_FALL_START && f.sound_ticks[0] == 47 + 8);
    assert(f.sound_ids[1] == EM_TRUCK_SOUND_FALL_END && f.sound_ticks[1] == 47 + 110);
    assert(f.rumble_ids[0] == 0 && f.rumble_ticks[0] == 0);
    assert(f.rumble_ids[1] == 2 && f.rumble_ticks[1] == 47 + 14);
    assert(f.rumble_ids[2] == 2 && f.rumble_ticks[2] == 47 + 87);
    /* The carried velocity has no z component: the carry only raises the flag. */
    assert(c.carry == 1 && c.a0[2] == 391.1f);
    assert(truck.velocity[0] == 0.0f && truck.velocity[1] == 0.0f && truck.velocity[2] == 0.0f);
    assert(truck.position[1] < 164.0f - 1.8f - 40.0f);

    /* A reload after the fall seats the fallen pose and rests immediately. */
    EmTruckOriginal again = {0};
    unsigned placements = f.placements;
    assert(em_truck_original_tick(&again, &c.world, &h) == 1 && again.state == 2);
    assert(f.placements == placements && again.matrix[15] == 1.0f && again.matrix[2] == -1.0f);

    truck.state = 3;
    assert(em_truck_original_tick(&truck, &c.world, &h) == 0 && truck.freed && f.frees == 1);
    assert(em_truck_original_tick(&truck, &c.world, &h) == 0 && f.frees == 1);
}

static void trigger(void)
{
    Fixture f = {0};
    Canon c;
    canon(&c, 0);
    EmTruckTriggerHooks th = {&f, start, poll, release};
    EmTruckTrigger t = {0};
    c.a0[0] = 250.8f; c.a0[2] = 209.0f;
    assert(em_truck_trigger_tick(&t, &c.world, &th) == 1 && t.state == 4);
    assert(em_truck_trigger_tick(&t, &c.world, &th) == 1 && t.state == 4 && !f.script_starts);
    c.a0[0] = 325.0f; c.a0[2] = 420.0f; c.phase = 2;
    assert(em_truck_trigger_tick(&t, &c.world, &th) == 1 && t.state == 4 && !f.script_starts);
    c.phase = 1;
    assert(em_truck_trigger_tick(&t, &c.world, &th) == 1 && t.state == 1 && t.armed == 4);
    assert(f.script_starts == 1 && f.script_entry == EM_TRUCK_CAMERA_SCRIPT);
    assert(em_truck_trigger_tick(&t, &c.world, &th) == 1 && t.state == 1 && c.story == 0);
    f.script_done = 1;
    assert(em_truck_trigger_tick(&t, &c.world, &th) == 1 && t.state == 3 && c.story == 1);
    assert(em_truck_trigger_tick(&t, &c.world, &th) == 0 && t.freed && f.frees == 1);

    assert(em_truck_trigger_bands(325.0f, 420.0f) && em_truck_trigger_bands(330.0f, 400.0f));
    assert(!em_truck_trigger_bands(315.0f, 400.0f) && !em_truck_trigger_bands(312.0f, 420.0f));
    assert(!em_truck_trigger_bands(336.0f, 420.0f) && !em_truck_trigger_bands(330.0f, 427.0f));
    assert(!em_truck_trigger_bands(380.8f, 391.1f));
}

int main(void)
{
    missing_workers();
    set_piece();
    trigger();
    puts("truck_original_test: fail-stop, set piece and trigger PASS");
    return 0;
}
