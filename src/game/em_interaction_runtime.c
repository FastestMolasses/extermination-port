#include "game/em_interaction_runtime.h"
#include <string.h>

static int fault(EmInteractionRuntime *runtime)
{
    if (runtime)
        runtime->failed = 1;
    return -1;
}

int em_interaction_runtime_init(EmInteractionRuntime *runtime, EmInteractionFrame *frame,
                                const EmInteractionRuntimeHooks *hooks)
{
    if (!runtime || !frame || !hooks)
        return 0;
    memset(runtime, 0, sizeof *runtime);
    runtime->frame = frame;
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

int em_interaction_runtime_claim_scripted(EmInteractionRuntime *runtime, const void *owner)
{
    if (!runtime || !owner || runtime->failed || !runtime->frame || runtime->owner ||
        !runtime->frame->selector || runtime->frame->player_ready)
        return 0;
    runtime->owner = owner;
    return 1;
}

int em_interaction_runtime_release(EmInteractionRuntime *runtime)
{
    if (!runtime || runtime->failed || !runtime->owner)
        return 0;
    runtime->owner = NULL;
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
    /* 0018B9C0 top 1/2 publishes the existing vectors instead of running the
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

static uint8_t *player_record(EmInteractionRuntime *runtime)
{
    return runtime->hooks.player_record ? runtime->hooks.player_record(runtime->hooks.context) : NULL;
}

int em_interaction_runtime_animation_start(EmInteractionRuntime *runtime, const void *owner,
                                           uint16_t clip, float rate, float blend)
{
    if (!em_interaction_runtime_owns(runtime, owner))
        return 0;
    uint8_t *p = player_record(runtime);
    if (!runtime->frame->player_ready || !p || rate != 1.0f) {
        fault(runtime);
        return 0;
    }
    /* 001B9A00 sub 0: +1F2 = the script record's +0x14 halfword, +1F8 = its
     * +0xC word, +1F4 = 0x3F800000. The stage's 00183090 commits it. */
    const uint32_t one = 0x3F800000u;
    memcpy(p + 0x1F2, &clip, 2);
    memcpy(p + 0x1F8, &blend, 4);
    memcpy(p + 0x1F4, &one, 4);
    return 1;
}

int em_interaction_runtime_animation_done(EmInteractionRuntime *runtime, const void *owner)
{
    if (!em_interaction_runtime_owns(runtime, owner))
        return -1;
    uint8_t *p = player_record(runtime);
    if (!p)
        return fault(runtime);
    uint32_t flags;
    memcpy(&flags, p + 0x200, 4);
    return (flags & 0x1000) != 0;
}
