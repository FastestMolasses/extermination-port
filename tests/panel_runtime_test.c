#include "game/em_panel_runtime.h"
#include "game/em_panel_message.h"
#include "game/em_player_pose.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    EmInteractionFrame frame;
    EmInteractionRuntime interaction;
    EmPanelRuntime panel;
    EmPanelMessage message;
    EmPlayerPose pose;
    EmPanelBatteryMenu menu;
    float palette[22 * 16];
    int tick, acquired, released, palettes, aligned, cameras, entered, left;
    int message_started, message_visible, menu_requested, status_active;
    int charge, power, indicator_stops, sound_tick, power_tick, animation_tick;
    int complete_tick, failure;
} Fixture;

static int publish(void *context, const float *palette)
{
    Fixture *f = context;
    for (unsigned i = 0; i < 22 * 16; ++i) assert(isfinite(palette[i]));
    assert(palette[21 * 16] == 1 && palette[21 * 16 + 15] == 1);
    ++f->palettes;
    return 1;
}

static int acquire(void *context)
{
    Fixture *f = context;
    ++f->acquired;
    return em_player_pose_acquire(&f->pose) &&
        em_player_pose_palette(&f->pose, f->palette, 22) && publish(f, f->palette);
}

static int idle(void *context, float *palette)
{
    Fixture *f = context;
    return em_player_pose_idle_tick(&f->pose, palette, 22);
}

static int release(void *context)
{
    Fixture *f = context;
    ++f->released;
    return em_player_pose_release(&f->pose);
}

static int pose_worker(void *context, const EmInteractionAnimation *animation,
                        int result, float *palette)
{
    Fixture *f = context;
    if (f->animation_tick < 0) f->animation_tick = f->tick;
    return em_player_pose_script_tick(&f->pose, animation, result, palette, 22);
}

/* Scene geometry, camera, audio-device and status dispatch are explicit
 * host boundaries. Raw pose, frame handshake, owner, scripts, timed message
 * and battery discharge below are actual implementations and actual assets. */
static int event(void *context, EmInteractionFrameEvent event)
{
    Fixture *f = context;
    if (event == EM_INTERACTION_BARS_ENTER) ++f->entered;
    if (event == EM_INTERACTION_BARS_LEAVE) ++f->left;
    return 1;
}

static int camera(void *context)
{
    Fixture *f = context;
    ++f->cameras;
    return f->failure != 2;
}

static int align_player(void *context)
{
    Fixture *f = context;
    ++f->aligned;
    return f->failure != 1;
}

static int message_start(void *context, uint32_t token, uint32_t delay)
{
    Fixture *f = context;
    if (f->failure == 3) return 0;
    ++f->message_started;
    int result = em_panel_message_start(&f->message, token, delay);
    f->frame.message_phase = (int)f->message.phase;
    return result;
}

static int message_done(void *context)
{
    Fixture *f = context;
    return f->failure == 4 ? -1 : em_panel_message_done(&f->message);
}

static int battery_open(void *context, EmPanel *owner, uint8_t request)
{
    Fixture *f = context;
    assert(owner == &f->panel.owner && request == 0x82);
    assert(owner->status == 1 && !owner->armed && !owner->charged);
    if (f->failure == 5) return 0;
    ++f->menu_requested;
    em_panel_battery_begin(&f->menu, f->charge);
    f->status_active = 1;
    return 1;
}

static int sound(void *context, uint32_t cue)
{
    Fixture *f = context;
    if (cue == 0x3EF) f->sound_tick = f->tick;
    else assert(cue == 0x3EE && f->power_tick == f->tick);
    return f->failure != 6;
}

static int power(void *context, uint8_t mask)
{
    Fixture *f = context;
    assert(mask == 0x80);
    if (f->failure == 7) return 0;
    f->power |= mask;
    f->power_tick = f->tick;
    return 1;
}

static int stop_indicator(void *context)
{
    Fixture *f = context;
    ++f->indicator_stops;
    return f->failure != 8;
}

static void setup(Fixture *f, EmModel *model, const EmPoseBank *bank, unsigned clip)
{
    memset(f, 0, sizeof *f);
    f->charge = 12;
    f->sound_tick = f->power_tick = f->animation_tick = f->complete_tick = -1;
    assert(em_player_pose_init(&f->pose, bank, clip, clip ? 64 : 0));
    assert(em_panel_message_load(&f->message, "assets/scene_snow/panel/terminal.emod"));
    EmInteractionRuntimeHooks shared = {f, acquire, idle, release, publish, event, camera};
    assert(em_interaction_runtime_init(&f->interaction, &f->frame, model, f->palette, &shared));
    assert(em_interaction_runtime_set_pose_worker(&f->interaction, pose_worker));
    EmPanelRuntimeHooks hooks = {f, align_player, message_start, message_done,
        battery_open, sound, power, stop_indicator};
    assert(em_panel_runtime_load(&f->panel, "assets/scene_snow/panel/scripts.emsc",
        0, &f->interaction, &hooks));
}

static void freeze(Fixture *f, int battery)
{
    Fixture before = *f;
    for (unsigned i = 0; i < 150; ++i) {
        assert(em_interaction_runtime_player_tick(&f->interaction, 0) == 0);
        assert(em_panel_runtime_tick(&f->panel, battery, 0) == 0);
    }
    assert(!memcmp(f, &before, sizeof *f));
}

static int step(Fixture *f, int battery)
{
    assert(!f->status_active);
    /*0015BA50 advances the old source before the first0015B130 takeover. */
    if (!f->pose.acquired) assert(em_player_pose_advance(&f->pose, 1, 0));
    if (em_interaction_runtime_player_tick(&f->interaction, 1) < 0) return -1;
    f->frame.message_phase = (int)f->message.phase;
    if (em_panel_runtime_tick(&f->panel, battery, 1) < 0) return -1;
    f->message.phase = (unsigned)f->frame.message_phase;
    em_panel_message_tick(&f->message, 0, 0);
    const EmOpeningLine *line = em_opening_dialogue_line(&f->message.dialogue);
    if (line && !line->terminal) ++f->message_visible;
    if (f->entered && !f->frame.selector && f->complete_tick < 0)
        f->complete_tick = f->tick;
    ++f->tick;
    return 0;
}

static void menu(Fixture *f, int discharge)
{
    assert(f->status_active && f->panel.owner.phase == 6);
    freeze(f, 1);
    if (!discharge) {
        unsigned result = em_panel_battery_step(&f->panel.owner, &f->menu, 0x40, &f->charge);
        assert(result == EM_PANEL_MENU_CANCEL && f->menu.phase == EM_PANEL_MENU_BROWSE);
        assert(f->charge == 12 && !f->panel.owner.charged);
        /* The real status/root worker must continue to handle Back. This
         * fixture explicitly supplies its eventual completed-exit boundary. */
        freeze(f, 1);
    } else {
        assert(em_panel_battery_step(&f->panel.owner, &f->menu, 0x8040, &f->charge) ==
               (EM_PANEL_MENU_CURSOR | EM_PANEL_MENU_ACCEPT));
        unsigned units = 0, finished = 0;
        for (int i = 1; i <= 61; ++i) {
            unsigned result = em_panel_battery_step(&f->panel.owner, &f->menu, 0, &f->charge);
            if (result & EM_PANEL_MENU_UNIT_SOUND) { assert(i == 1 || i == 31); ++units; }
            if (result & EM_PANEL_MENU_FINISHED) { assert(i == 61); ++finished; }
        }
        assert(units == 2 && finished == 1 && f->charge == 8 && f->panel.owner.armed == 5);
    }
    assert(em_interaction_runtime_owns(&f->interaction, &f->panel));
    f->status_active = 0;
}

static void run(EmModel *model, const EmPoseBank *bank, int battery, int discharge,
                 unsigned source)
{
    Fixture f;
    setup(&f, model, bank, source);
    int competitor = 0;
    assert(em_interaction_runtime_claim(&f.interaction, &competitor));
    assert(!em_panel_runtime_arm(&f.panel) && !f.panel.owner.armed);
    f.interaction.owner = NULL;
    f.frame.selector = 0;
    assert(em_panel_runtime_arm(&f.panel));
    assert(!em_panel_runtime_arm(&f.panel) && !em_panel_runtime_free(&f.panel));
    int menu_tick = -1;
    while (f.tick < 500 && !f.released) {
        if (f.tick == 40) freeze(&f, battery);
        assert(step(&f, battery) == 0);
        if (f.tick == 1) {
            assert(f.acquired == 1 && f.aligned == 1);
            if (source) assert(f.pose.transition.active && f.pose.transition.remaining == 8);
            else assert(!f.pose.transition.active && f.pose.playback.remaining == 79);
        }
        if (f.status_active) { menu_tick = f.tick; menu(&f, discharge); }
    }
    if (f.released != 1 || f.complete_tick != f.tick - 2 || f.message_visible != 149)
        fprintf(stderr, "Panel sequence mismatch: released%d complete%d tick%d visible%d phase%u pc%X\n",
            f.released, f.complete_tick, f.tick, f.message_visible, f.panel.owner.phase,
            f.panel.program.script.pc);
    assert(f.released == 1 && f.complete_tick == f.tick - 2 && f.message_visible == 149);
    assert(f.entered == 1 && f.left == 1 && f.aligned == 1 && f.message_started == 1);
    assert(!em_interaction_runtime_owner(&f.interaction) && !f.pose.acquired);
    if (battery && discharge) {
        assert(f.panel.owner.phase == 3 && f.panel.owner.status == 2 && !f.panel.owner.child_alive);
        assert(f.power == 0x80 && f.indicator_stops == 1 && f.cameras == 2);
        assert(f.animation_tick == menu_tick + 2 && f.sound_tick == menu_tick + 14);
        assert(f.power_tick == menu_tick + 127 && f.complete_tick == menu_tick + 128);
        assert(f.pose.playback.clip->id == 0 && f.pose.playback.remaining == 80);
    } else {
        assert(f.panel.owner.phase == 0 && f.panel.owner.status == 1 && f.panel.owner.child_alive);
        assert(!f.power && !f.indicator_stops && f.animation_tick < 0 && f.cameras == 1);
    }
    printf("Panel battery%d discharge%d source%u: menu%d clip%d sound%d power%d exit%d release%d PASS\n",
        battery, discharge, source, menu_tick, f.animation_tick, f.sound_tick,
        f.power_tick, f.complete_tick, f.tick - 1);
    assert(em_panel_runtime_free(&f.panel));
    em_panel_message_free(&f.message);
}

static void failure(EmModel *model, const EmPoseBank *bank, int which)
{
    Fixture f;
    setup(&f, model, bank, 0);
    f.failure = which;
    assert(em_panel_runtime_arm(&f.panel));
    int result = 0;
    while (f.tick < 500 && result == 0) {
        result = step(&f, 1);
        if (f.status_active) menu(&f, 1);
    }
    assert(result == -1 && f.panel.failed && !f.released);
    assert(em_interaction_runtime_owner(&f.interaction) == &f.panel);
    assert(!em_panel_runtime_free(&f.panel));
    assert(em_panel_runtime_tick(&f.panel, 1, 1) == -1);
    /* Explicit whole-world destruction after a fault is not player release. */
    memset(&f.interaction, 0, sizeof f.interaction);
    assert(em_panel_runtime_free(&f.panel));
    em_panel_message_free(&f.message);
}

int main(void)
{
    EmModel model = {0};
    EmPoseBank bank = {0};
    assert(em_model_load(&model, "assets/player.emdl") == 0 && model.bone_count == 22);
    assert(em_pose_bank_load(&bank, "assets/player_channels.empc"));
    for (unsigned source = 0; source <= 1; ++source) {
        run(&model, &bank, 0, 0, source);
        run(&model, &bank, 1, 0, source);
        run(&model, &bank, 1, 1, source);
    }
    for (int which = 1; which <= 8; ++which) failure(&model, &bank, which);
    em_pose_bank_free(&bank);
    em_model_free(&model);
    puts("Panel shared runtime: raw source poses, actual script/message assets, discharge, freeze and faults PASS");
    return 0;
}
