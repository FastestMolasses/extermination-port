/* Real door/player resources and shared ownership through the room request.
 * Camera, audio, GPU and the subsequent room loader are explicit boundaries. */
#include "game/em_door_original_runtime.h"
#include "game/em_door_program.h"
#include "game/em_interaction_runtime.h"
#include "game/em_player_pose.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    EmDoorOriginalRuntime door;
    EmDoorProgram program;
    EmInteractionScene scene;
    EmInteractionRuntime shared;
    EmInteractionFrame frame;
    EmModel player_model;
    EmPoseBank player_bank;
    EmPlayerPose pose;
    EmDoorDestination destination;
    float player_position[3], player_yaw, palette[22*16];
    unsigned sounds, cameras, fades, publications, draws, acquisitions;
    uint32_t cue;
} Fixture;

static int acquire(void *context)
{
    Fixture *f = context;
    ++f->acquisitions;
    return em_player_pose_acquire(&f->pose);
}

static int idle(void *context, float *palette)
{
    return em_player_pose_idle_tick(&((Fixture *)context)->pose, palette, 22);
}

static int release(void *context)
{
    return em_player_pose_release(&((Fixture *)context)->pose);
}

static int pose_worker(void *context, const EmInteractionAnimation *animation,
                       int result, float *palette)
{
    return em_player_pose_script_tick(&((Fixture *)context)->pose, animation, result, palette, 22);
}

static int publish_player(void *context, const float *palette)
{
    Fixture *f = context;
    assert(f->pose.valid && palette[0] == 1 && palette[21*16] == 1);
    return 1;
}

static int frame_event(void *context, EmInteractionFrameEvent event)
{
    (void)context;
    assert(event == EM_INTERACTION_SCOPE_ZOOM_ZERO);
    return 1;
}

static int camera(void *context)
{
    ++((Fixture *)context)->cameras;
    return 1;
}

static EmScriptCommandResult frame(void *context, EmScript *script, const unsigned char *record)
{
    Fixture *f = context;
    return em_interaction_runtime_frame(&f->shared, &f->door, script, record);
}

static int chase(void *context)
{
    Fixture *f = context;
    return em_interaction_runtime_camera_retarget(&f->shared, &f->door);
}

static int player_animation(void *context, uint16_t clip, float rate, float blend)
{
    Fixture *f = context;
    return em_interaction_runtime_animation_start(&f->shared, &f->door, clip, rate, blend);
}

static int object_animation(void *context, uint16_t clip, float blend, float start)
{
    Fixture *f = context;
    assert(f->sounds == 1 && blend == 0 && start == 0);
    return em_door_original_runtime_animation(&f->door, clip);
}

static int sound(void *context, uint32_t cue, float radius)
{
    Fixture *f = context;
    assert(radius == 300);
    ++f->sounds;
    f->cue = cue;
    return 1;
}

static int patch(void *context, const EmDoorTransitPlan *plan)
{
    return em_door_program_patch(&((Fixture *)context)->program, plan);
}

static int face(void *context, float yaw)
{
    ((Fixture *)context)->player_yaw = yaw;
    return 1;
}

static int align(void *context, const float point[4])
{
    Fixture *f = context;
    assert(point[3] == 1 && point[1] == f->player_position[1]);
    memcpy(f->player_position, point, sizeof f->player_position);
    return 1;
}

static int start(void *context, uint32_t entry)
{
    return em_door_program_start(&((Fixture *)context)->program, entry);
}

static int pump(void *context)
{
    return em_door_program_tick(&((Fixture *)context)->program);
}

static int kickoff(void *context, int locked)
{
    Fixture *f = context;
    EmDoorTransitHooks hooks = {f, patch, face, align, start, pump};
    return em_door_transit_kickoff(&f->door.owner, f->door.angles[1], f->player_position,
        f->door.sounds, locked, &f->scene.math, &hooks);
}

static int fade(void *context, int whole_area, int ticks)
{
    Fixture *f = context;
    assert(!whole_area && ticks == 4 && f->destination.kind == 0);
    ++f->fades;
    return 1;
}

static int transition(void *context)
{
    Fixture *f = context;
    return em_door_transit_commit(&f->destination, f->door.owner.door_id,
        (uint16_t)f->door.owner.side, f->door.destination, fade, f);
}

static int publish(void *context, const float point[3])
{
    Fixture *f = context;
    assert(point[0] == f->door.owner.origin[0] && point[1] > f->door.owner.origin[1]);
    ++f->publications;
    return 1;
}

static int draw(void *context)
{
    ++((Fixture *)context)->draws;
    return 1;
}

static void run(unsigned side)
{
    Fixture f = {0};
    assert(em_interaction_scene_load(&f.scene, "assets/scene_snow/interaction.emis"));
    assert(em_model_load(&f.player_model, "assets/player.emdl") == 0);
    assert(em_pose_bank_load(&f.player_bank, "assets/player_channels.empc"));
    assert(em_player_pose_init(&f.pose, &f.player_bank, 0, 5));
    EmInteractionRuntimeHooks shared_hooks = {&f, acquire, idle, release, publish_player,
                                             frame_event, camera};
    assert(em_interaction_runtime_init(&f.shared, &f.frame, &f.player_model, f.palette, &shared_hooks));
    assert(em_interaction_runtime_set_pose_worker(&f.shared, pose_worker));
    EmDoorOriginalRuntimeHooks door_hooks = {&f, kickoff, pump, start, transition, publish, draw};
    assert(em_door_original_runtime_load(&f.door, "assets/scene_snow",
        em_interaction_scene_role(&f.scene, EM_INTERACTION_DOOR), &door_hooks));
    EmDoorProgramHooks program_hooks = {&f, frame, chase, player_animation, object_animation, sound};
    assert(em_door_program_load(&f.program, "assets/scene_snow/door_original/program.emsc",
                                &f.door.owner, &program_hooks));
    memcpy(f.player_position, f.door.owner.origin, sizeof f.player_position);
    f.player_position[2] += side ? -5 : 5;
    assert(em_interaction_runtime_claim(&f.shared, &f.door));
    assert(em_door_original_runtime_arm(&f.door));
    unsigned ticks = 0;
    while (!f.destination.kind && ticks < 160) {
        assert(em_interaction_runtime_player_tick(&f.shared, 1) == 1);
        assert(em_door_original_runtime_tick(&f.door, 0) == 1);
        ++ticks;
    }
    assert(f.destination.kind == 2 && f.destination.entry == (side ? 1 : 2));
    assert(f.acquisitions == 1 && f.cameras == 1 && f.fades == 1 && f.sounds == 1);
    assert(f.cue == (side ? 0x402 : 0x401));
    assert(f.door.owner.side == (int)side && f.door.owner.phase == 5);
    assert(f.pose.playback.clip->id == (side ? 0x43 : 0x45));
    assert(f.door.playback.clip->id == (side ? 0 : 2));
    assert(f.frame.camera_top == 2 && f.frame.selector == 2);
    assert(f.publications == ticks && f.draws == ticks && !f.shared.failed && !f.program.failed);
    printf("Door side%u real-resource program reached original room entry%u after%u callbacks\n",
           side, f.destination.entry, ticks);
    /* Stop at the issued request: this fixture does not implement room loading
     * or assert that ownership released. Destroy the isolated world afterward. */
    memset(&f.shared, 0, sizeof f.shared);
    em_door_program_free(&f.program);
    em_door_original_runtime_free(&f.door);
    em_model_free(&f.player_model);
    em_pose_bank_free(&f.player_bank);
}

int main(void)
{
    run(0);
    run(1);
    puts("Original door program: PASS real resources, raw player/object channels and shared ownership");
    return 0;
}
