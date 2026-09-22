#include "game/em_interaction_runtime.h"
#include <string.h>

static int fault(EmInteractionRuntime *runtime)
{
    if (runtime)
        runtime->failed = 1;
    return -1;
}

int em_interaction_runtime_init(EmInteractionRuntime *runtime, EmInteractionFrame *frame,
                                const EmModel *model, float *local_palette,
                                const EmInteractionRuntimeHooks *hooks)
{
    if (!runtime || !frame || !model || !model->bone_count || !local_palette || !hooks)
        return 0;
    memset(runtime, 0, sizeof *runtime);
    runtime->frame = frame;
    runtime->model = model;
    runtime->local_palette = local_palette;
    runtime->hooks = *hooks;
    return 1;
}

int em_interaction_runtime_claim(EmInteractionRuntime *runtime, const void *owner)
{
    if (!runtime || !owner || runtime->failed || !runtime->frame || runtime->owner ||
        runtime->frame->selector || runtime->frame->ready || runtime->frame->player_ready)
        return 0;
    runtime->owner = owner;
    runtime->frame->selector = 3;
    return 1;
}

int em_interaction_runtime_set_pose_worker(EmInteractionRuntime *runtime,
                                           EmInteractionPoseWorker worker)
{
    if (!runtime || runtime->failed || runtime->owner)
        return 0;
    runtime->pose_worker = worker;
    return 1;
}

int em_interaction_runtime_owns(const EmInteractionRuntime *runtime, const void *owner)
{
    return runtime && owner && !runtime->failed && runtime->owner == owner;
}

const void *em_interaction_runtime_owner(const EmInteractionRuntime *runtime)
{
    return runtime ? runtime->owner : NULL;
}

int em_interaction_runtime_camera_owned(const EmInteractionRuntime *runtime)
{
    /*0018B9C0 top1/2 publishes the existing vectors instead of running the
     * ordinary solver. Retain that ownership even if a host worker faults. */
    return runtime && runtime->owner && runtime->frame &&
           (runtime->frame->camera_top == 1 || runtime->frame->camera_top == 2);
}

EmScriptCommandResult em_interaction_runtime_frame(EmInteractionRuntime *runtime, const void *owner,
                                                   EmScript *script, const unsigned char *record)
{
    if (!em_interaction_runtime_owns(runtime, owner) || !script || !record ||
        (em_script_u32(record, 0) & 0xFFF) != 7)
        return EM_SCRIPT_UNSUPPORTED;
    EmScriptCommandResult result = em_interaction_frame_command(
        runtime->frame, script, em_script_u32(record, 8), em_script_u32(record, 0x14) != 0,
        runtime->hooks.frame_event, runtime->hooks.context);
    if (result == EM_SCRIPT_UNSUPPORTED)
        fault(runtime);
    return result;
}

int em_interaction_runtime_camera_retarget(EmInteractionRuntime *runtime, const void *owner)
{
    if (!em_interaction_runtime_owns(runtime, owner))
        return 0;
    if (!runtime->frame->player_ready || !runtime->hooks.camera_retarget ||
        runtime->hooks.camera_retarget(runtime->hooks.context) != 1) {
        fault(runtime);
        return 0;
    }
    return 1;
}

int em_interaction_runtime_animation_start(EmInteractionRuntime *runtime, const void *owner,
                                           uint16_t clip, float rate, float blend)
{
    if (!em_interaction_runtime_owns(runtime, owner))
        return 0;
    if (!runtime->frame->player_ready ||
        !em_interaction_animation_request(&runtime->animation, runtime->model, clip, rate, blend)) {
        fault(runtime);
        return 0;
    }
    return 1;
}

int em_interaction_runtime_animation_done(const EmInteractionRuntime *runtime, const void *owner)
{
    return em_interaction_runtime_owns(runtime, owner)
               ? em_interaction_animation_done(&runtime->animation)
               : -1;
}

int em_interaction_runtime_player_tick(EmInteractionRuntime *runtime, int ordinary_tasks_enabled)
{
    if (!runtime || runtime->failed)
        return -1;
    if (!ordinary_tasks_enabled || !runtime->owner)
        return 0;
    EmInteractionFrame *frame = runtime->frame;
    EmInteractionRuntimeHooks *hooks = &runtime->hooks;
    if (!frame->player_ready) {
        if (!frame->selector || !hooks->acquire_player)
            return fault(runtime);
        int acquired = hooks->acquire_player(hooks->context);
        if (acquired < 0 || acquired > 1)
            return fault(runtime);
        if (!acquired)
            return 0;
        frame->player_ready = 1;
        /*0015B130 acquires after the ordinary advance on this callback.
         * Advancing the newly acquired default clip here would add a tick. */
        return 1;
    }
    /* External skeleton ready2 requires a different player worker. Neither
     * panel nor elevator installs one; don't pass it to the ordinary sampler. */
    if (frame->player_ready != 1)
        return fault(runtime);
    int palette_result;
    if (runtime->animation.active)
        palette_result = em_interaction_animation_tick(&runtime->animation, runtime->model,
                                                       runtime->local_palette);
    else if (hooks->idle_player_tick)
        palette_result = hooks->idle_player_tick(hooks->context, runtime->local_palette);
    else
        return fault(runtime);
    if (palette_result < 0 || palette_result > 1)
        return fault(runtime);
    if (runtime->animation.active && runtime->pose_worker &&
        runtime->pose_worker(hooks->context, &runtime->animation, palette_result,
                             runtime->local_palette) != 1)
        return fault(runtime);
    if (palette_result && (!hooks->publish_palette ||
                           hooks->publish_palette(hooks->context, runtime->local_palette) != 1))
        return fault(runtime);
    if (!frame->selector) {
        if (!hooks->release_player || hooks->release_player(hooks->context) != 1)
            return fault(runtime);
        frame->player_ready = 0;
        em_interaction_animation_clear(&runtime->animation);
        runtime->owner = NULL;
    }
    return 1;
}
