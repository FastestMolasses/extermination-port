#include "game/em_elevator_runtime.h"
#include <string.h>

static EmScriptCommandResult frame(void *context, EmScript *script,
                                    const unsigned char *record)
{
    EmElevatorRuntime *runtime = context;
    return em_interaction_runtime_frame(runtime->interaction, runtime, script, record);
}

static int align_player(void *context, const float position[3])
{
    EmElevatorRuntime *r = context;
    return r->hooks.align_player(r->hooks.context, position);
}

static int face_player(void *context, float yaw)
{
    EmElevatorRuntime *r = context;
    return r->hooks.face_player(r->hooks.context, yaw);
}

static int camera_set(void *context, const float eye[3], const float target[3])
{
    EmElevatorRuntime *r = context;
    return r->hooks.camera_set(r->hooks.context, eye, target);
}

static int camera_publish(void *context)
{
    EmElevatorRuntime *r = context;
    return r->hooks.camera_publish(r->hooks.context);
}

static int camera_chase(void *context)
{
    EmElevatorRuntime *r = context;
    return r->hooks.camera_chase(r->hooks.context);
}

static int animation_start(void *context, uint16_t clip, float rate, float blend)
{
    EmElevatorRuntime *r = context;
    return em_interaction_runtime_animation_start(r->interaction, r, clip, rate, blend);
}

static int animation_done(void *context)
{
    EmElevatorRuntime *r = context;
    return em_interaction_runtime_animation_done(r->interaction, r);
}

static int message_start(void *context, uint32_t token, uint32_t delay)
{
    EmElevatorRuntime *r = context;
    return r->hooks.message_start(r->hooks.context, token, delay);
}

static int message_done(void *context)
{
    EmElevatorRuntime *r = context;
    return r->hooks.message_done(r->hooks.context);
}

static void sound(void *context, unsigned cue, float radius)
{
    EmElevatorRuntime *r = context;
    r->hooks.sound(r->hooks.context, cue, radius);
}

static void rebuild_pose(void *context, float height)
{
    EmElevatorRuntime *r = context;
    r->hooks.rebuild_pose(r->hooks.context, height);
}

static void copy_indicator_pose(void *context)
{
    EmElevatorRuntime *r = context;
    r->hooks.copy_indicator_pose(r->hooks.context);
}

static void update_actor(void *context)
{
    EmElevatorRuntime *r = context;
    r->hooks.update_actor(r->hooks.context);
}

static void retransform(void *context)
{
    EmElevatorRuntime *r = context;
    r->hooks.retransform(r->hooks.context);
}

static void start_script(void *context, uint32_t entry)
{
    EmElevatorRuntime *r = context;
    if (!em_interaction_runtime_owns(r->interaction, r) ||
        em_elevator_program_start(&r->program, entry) < 0) r->failed = 1;
}

static int tick_script(void *context)
{
    EmElevatorRuntime *r = context;
    if (r->failed || !em_interaction_runtime_owns(r->interaction, r)) return -1;
    return em_elevator_program_tick(&r->program);
}

static EmElevatorHooks owner_hooks(EmElevatorRuntime *runtime)
{
    return (EmElevatorHooks){runtime, start_script, tick_script, sound,
                             rebuild_pose, copy_indicator_pose, update_actor, retransform};
}

static EmScriptCommandResult move(void *context, EmScript *script)
{
    EmElevatorRuntime *r = context;
    EmElevatorHooks hooks = owner_hooks(r);
    r->motion.phase = (uint8_t)script->phase;
    int result = em_elevator_motion_tick(&r->motion, r->owner.lower,
        &r->owner.height, r->player_ground_y, r->camera_target_y, &hooks);
    script->phase = r->motion.phase;
    return result < 0 ? EM_SCRIPT_UNSUPPORTED :
           result ? EM_SCRIPT_ADVANCE : EM_SCRIPT_WAIT;
}

int em_elevator_runtime_load(EmElevatorRuntime *runtime, const char *path,
    int lower, EmInteractionRuntime *interaction, float *player_ground_y,
    float *camera_target_y, const EmElevatorRuntimeHooks *hooks)
{
    if (!runtime || !path || !interaction || !interaction->frame ||
        !player_ground_y || !camera_target_y || !hooks ||
        !hooks->align_player || !hooks->face_player || !hooks->camera_set ||
        !hooks->camera_publish || !hooks->camera_chase || !hooks->message_start ||
        !hooks->message_done || !hooks->sound || !hooks->rebuild_pose ||
        !hooks->copy_indicator_pose || !hooks->update_actor || !hooks->retransform) return 0;
    memset(runtime, 0, sizeof *runtime);
    runtime->interaction = interaction;
    runtime->player_ground_y = player_ground_y;
    runtime->camera_target_y = camera_target_y;
    runtime->hooks = *hooks;
    em_elevator_init(&runtime->owner, lower);
    EmElevatorProgramHooks program_hooks = {runtime, frame, align_player,
        face_player, camera_set, camera_publish, camera_chase, animation_start,
        animation_done, message_start, message_done, move};
    if (em_elevator_program_load(&runtime->program, path, &runtime->owner,
                                 &program_hooks) < 0) {
        memset(runtime, 0, sizeof *runtime);
        return 0;
    }
    return 1;
}

int em_elevator_runtime_arm(EmElevatorRuntime *runtime)
{
    if (!runtime || runtime->failed || !runtime->program.image.bytes ||
        runtime->owner.phase || runtime->owner.armed ||
        !em_interaction_runtime_claim(runtime->interaction, runtime)) return 0;
    runtime->owner.armed = 4;
    return 1;
}

int em_elevator_runtime_tick(EmElevatorRuntime *runtime, int powered,
                            int ordinary_tasks_enabled)
{
    if (!runtime || runtime->failed || !runtime->program.image.bytes) return -1;
    if (!ordinary_tasks_enabled) return 0;
    EmElevatorHooks hooks = owner_hooks(runtime);
    int result = em_elevator_tick(&runtime->owner, powered, &hooks);
    if (result < 0 || runtime->failed) {
        runtime->failed = 1;
        return -1;
    }
    return 0;
}

int em_elevator_runtime_free(EmElevatorRuntime *runtime)
{
    if (!runtime || em_interaction_runtime_owner(runtime->interaction) == runtime)
        return 0;
    em_elevator_program_free(&runtime->program);
    memset(runtime, 0, sizeof *runtime);
    return 1;
}
