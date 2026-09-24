#include "game/em_elevator_program.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    EmElevator owner;
    EmElevatorMotion motion;
    EmElevatorProgram program;
    EmElevatorHooks owner_hooks;
    int frame, started, released, completed, animations, animation_polls;
    int camera_sets, camera_publishes, chases, messages, message_polls;
    int cue_frame, move_sound_frame, move_done_frame, poses;
    float player[3], camera_target[3], yaw;
} Fixture;

static void start(void *context, uint32_t entry)
{
    Fixture *f = context;
    assert(em_elevator_program_start(&f->program, entry) == 0);
    ++f->started;
}
static int tick(void *context)
{
    Fixture *f = context;
    int result = em_elevator_program_tick(&f->program);
    assert(result >= 0);
    if (result) f->completed = f->frame;
    return result;
}
static void sound(void *context, unsigned cue, float radius)
{
    Fixture *f = context;
    assert(radius == 300);
    if (cue == 0x19a) f->cue_frame = f->frame;
    else {
        assert(cue == (f->owner.lower ? 0x452U : 0x453U));
        f->move_sound_frame = f->frame;
    }
}
static void pose(void *context, float height)
{
    Fixture *f = context;
    assert(height == f->owner.height); ++f->poses;
}
static void noop(void *context) { (void)context; }
static int failed_tick(void *context) { (void)context; return -1; }
static EmScriptCommandResult frame(void *context, EmScript *script,
                                   const unsigned char *record)
{
    Fixture *f = context; (void)script;
    /* Real frame handshake remains a host boundary, deliberately explicit
     * in this test. No claim about its duration follows from this fixture. */
    if (em_script_u32(record, 8) == 4) f->released = f->frame;
    else assert(em_script_u32(record, 8) == 2);
    return EM_SCRIPT_ADVANCE;
}
static int align(void *context, const float position[3])
{
    Fixture *f = context;
    memcpy(f->player, position, sizeof f->player);
    assert(position[0] == 222 && position[2] == 250);
    return 1;
}
static int face(void *context, float yaw)
{
    Fixture *f = context; f->yaw = yaw;
    assert(yaw == -1.3037610054016113f); return 1;
}
static int camera_set(void *context, const float eye[3], const float target[3])
{
    Fixture *f = context;
    assert(eye[0] == 278 && eye[1] == 264 && eye[2] == 226);
    assert(target[1] == f->owner.script_heights[1 + (f->camera_sets != 0)]);
    memcpy(f->camera_target, target, sizeof f->camera_target);
    ++f->camera_sets; return 1;
}
static int camera_publish(void *context)
{ Fixture *f = context; ++f->camera_publishes; return 1; }
static int chase(void *context)
{ Fixture *f = context; ++f->chases; return 1; }
static int animation(void *context, uint16_t clip, float rate, float blend)
{
    Fixture *f = context;
    assert(clip == 0x47 && rate == 1 && blend == 1);
    ++f->animations; return 1;
}
static int animation_done(void *context)
{ Fixture *f = context; return ++f->animation_polls == 3; }
static int message(void *context, uint32_t token, uint32_t delay)
{
    Fixture *f = context;
    assert(token == 0x8000001a && !delay);
    ++f->messages; return 1;
}
static int message_done(void *context)
{ Fixture *f = context; return ++f->message_polls == 3; }
static EmScriptCommandResult move(void *context, EmScript *script)
{
    Fixture *f = context;
    f->motion.phase = (uint8_t)script->phase;
    int result = em_elevator_motion_tick(&f->motion, f->owner.lower,
        &f->owner.height, f->player+1, f->camera_target+1, &f->owner_hooks);
    script->phase = f->motion.phase;
    assert(result >= 0);
    if (result) f->move_done_frame = f->frame;
    return result ? EM_SCRIPT_ADVANCE : EM_SCRIPT_WAIT;
}

static void run(const char *path, int powered, int lower)
{
    Fixture f = {0};
    f.cue_frame = f.move_sound_frame = f.move_done_frame = -1;
    em_elevator_init(&f.owner, lower);
    f.owner.armed = 4;
    f.owner_hooks = (EmElevatorHooks){&f,start,tick,sound,pose,noop,noop,noop};
    EmElevatorProgramHooks hooks = {&f,frame,align,face,camera_set,camera_publish,
        chase,animation,animation_done,message,message_done,move};
    assert(em_elevator_program_load(&f.program, path, &f.owner, &hooks) == 0);
    for (f.frame = 0; f.frame < 200 && !f.completed; ++f.frame) {
        assert(em_elevator_tick(&f.owner, powered, &f.owner_hooks) == 0);
        if (!f.completed) assert(f.owner.lower == lower);
        if (f.frame == 0) assert(f.started == 1 && !f.animations && !f.messages);
    }
    assert(f.completed == f.released && !f.owner.phase && !f.owner.armed);
    if (powered) {
        assert(f.owner.lower != lower && f.cue_frame == 120);
        assert(f.animations == 1 && f.animation_polls == 3);
        assert(f.camera_sets == 2 && f.camera_publishes == 4 && !f.chases);
        assert(f.motion.ticks == 150 && f.move_done_frame-f.move_sound_frame == 150);
        assert(f.completed-f.move_done_frame == 3 && !f.messages);
        assert(f.owner.height == (lower ? 230 : 190));
        assert(f.poses == 151); /*150 motion updates plus owner final snap*/
    } else {
        assert(f.owner.lower == lower && f.cue_frame == -1);
        assert(!f.animations && !f.camera_sets && f.chases == 1);
        assert(f.messages == 1 && f.message_polls == 3 && f.owner.sound_timer == 300);
    }
    em_elevator_program_free(&f.program);
    hooks.align_player = NULL;
    assert(em_elevator_program_load(&f.program, path, &f.owner, &hooks) == 0);
    assert(em_elevator_program_start(&f.program, EM_ELEVATOR_REFUSAL_SCRIPT) == 0);
    assert(em_elevator_program_tick(&f.program) == 0);
    assert(em_elevator_program_tick(&f.program) == -1 && f.program.failed);
    assert(em_elevator_program_tick(&f.program) == -1);
    em_elevator_program_free(&f.program);
    f.owner.phase = 1; f.owner.armed = 4;
    int previous_lower = f.owner.lower;
    f.owner_hooks.tick_script = failed_tick;
    assert(em_elevator_tick(&f.owner, 1, &f.owner_hooks) == -1);
    assert(f.owner.phase == 1 && f.owner.armed == 4 && f.owner.lower == previous_lower);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    run(argv[1], 0, 0); run(argv[1], 1, 0); run(argv[1], 1, 1);
    puts("Original elevator program assets: refusal, both ride directions, camera/animation waits, delayed owner toggle and missing-binding faults PASS");
}
