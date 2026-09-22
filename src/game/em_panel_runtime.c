#include "game/em_panel_runtime.h"
#include <string.h>

static int accepted(EmPanelRuntime *runtime, int result)
{
    if (result != 1) runtime->failed = 1;
    return result == 1;
}

static EmScriptCommandResult frame(void *context, EmScript *script,
                                    const unsigned char *record)
{
    EmPanelRuntime *r = context;
    return em_interaction_runtime_frame(r->interaction, r, script, record);
}

static int camera(void *context)
{
    EmPanelRuntime *r = context;
    return em_interaction_runtime_camera_retarget(r->interaction, r);
}

static int message_start(void *context, uint32_t token, uint32_t delay)
{
    EmPanelRuntime *r = context;
    return accepted(r, r->hooks.message_start(r->hooks.context, token, delay));
}

static int message_done(void *context)
{
    EmPanelRuntime *r = context;
    int result = r->hooks.message_done(r->hooks.context);
    if (result < 0 || result > 1) {
        r->failed = 1;
        return -1;
    }
    return result;
}

static int animation(void *context, uint16_t clip, float rate, float blend)
{
    EmPanelRuntime *r = context;
    return em_interaction_runtime_animation_start(r->interaction, r, clip, rate, blend);
}

static int battery(void *context, EmPanel *owner, uint8_t request)
{
    EmPanelRuntime *r = context;
    return accepted(r, r->hooks.battery_open(r->hooks.context, owner, request));
}

static int sound(void *context, uint32_t cue)
{
    EmPanelRuntime *r = context;
    return accepted(r, r->hooks.sound(r->hooks.context, cue));
}

static int power(void *context, uint8_t mask)
{
    EmPanelRuntime *r = context;
    return accepted(r, r->hooks.power(r->hooks.context, mask));
}

static void align_player(void *context)
{
    EmPanelRuntime *r = context;
    if (!r->failed) accepted(r, r->hooks.align_player(r->hooks.context));
}

static void start_script(void *context, EmPanelScript entry, uint32_t token)
{
    EmPanelRuntime *r = context;
    if (r->failed || !em_interaction_runtime_owns(r->interaction, r) ||
        em_panel_program_start(&r->program, entry, token) < 0) r->failed = 1;
}

static int tick_script(void *context)
{
    EmPanelRuntime *r = context;
    if (r->failed || !em_interaction_runtime_owns(r->interaction, r)) return -1;
    int result = em_panel_program_tick(&r->program);
    if (result < 0) r->failed = 1;
    return result;
}

static void stop_indicator(void *context)
{
    EmPanelRuntime *r = context;
    if (!r->failed) accepted(r, r->hooks.stop_indicator(r->hooks.context));
}

int em_panel_runtime_load(EmPanelRuntime *runtime, const char *path,
    int completed, EmInteractionRuntime *interaction, const EmPanelRuntimeHooks *hooks)
{
    if (!runtime || !path || !interaction || !interaction->frame || !hooks ||
        !hooks->align_player || !hooks->message_start || !hooks->message_done ||
        !hooks->battery_open || !hooks->sound || !hooks->power ||
        !hooks->stop_indicator) return 0;
    memset(runtime, 0, sizeof *runtime);
    runtime->interaction = interaction;
    runtime->hooks = *hooks;
    em_panel_init(&runtime->owner, completed);
    EmPanelProgramHooks program_hooks = {runtime, frame, camera, message_start,
        message_done, animation, battery, sound, power};
    if (em_panel_program_load(&runtime->program, path, &runtime->owner,
                              &program_hooks) < 0) {
        memset(runtime, 0, sizeof *runtime);
        return 0;
    }
    return 1;
}

int em_panel_runtime_arm(EmPanelRuntime *runtime)
{
    if (!runtime || runtime->failed || !runtime->program.image.bytes ||
        runtime->owner.status != 1 || runtime->owner.phase || runtime->owner.armed ||
        !em_interaction_runtime_claim(runtime->interaction, runtime)) return 0;
    runtime->owner.armed = 4;
    return 1;
}

int em_panel_runtime_tick(EmPanelRuntime *runtime, int has_small_battery,
                         int ordinary_tasks_enabled)
{
    if (!runtime || runtime->failed || !runtime->program.image.bytes) return -1;
    if (!ordinary_tasks_enabled) return 0;
    EmPanelHooks hooks = {runtime, align_player, start_script, tick_script, stop_indicator};
    int result = em_panel_tick(&runtime->owner, has_small_battery, &hooks);
    if (result < 0 || runtime->failed) {
        runtime->failed = 1;
        return -1;
    }
    return 0;
}

int em_panel_runtime_free(EmPanelRuntime *runtime)
{
    if (!runtime || em_interaction_runtime_owner(runtime->interaction) == runtime) return 0;
    em_panel_program_free(&runtime->program);
    memset(runtime, 0, sizeof *runtime);
    return 1;
}
