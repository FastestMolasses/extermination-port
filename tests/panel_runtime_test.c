#include "game/em_panel_runtime.h"
#include "game/em_hud.h"
#include "game/em_message_live.h"
#include "game/em_scene_bindings.h"
#include "game/em_player_pose.h"
#include "game/em_status_page_record.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int idle_stop_lane(void *ctx, int lane) { (void)ctx; return lane == 1 || lane == 2; }

typedef struct {
    EmInteractionFrame frame;
    EmInteractionRuntime interaction;
    EmPanelRuntime panel;
    EmPlayerPose pose;
    uint8_t page[EM_SPR_PAGE_SIZE]; /* D_00810130 of the BATTERY page 002149F0 */
    unsigned units, finished;
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

/* The live message service (WP-8) over its exported data: the host posts
 * 0x80000018 through 001B7D60 case 0 and step F runs it. Its glyph draw
 * and the frame service registration are this fixture's boundaries. */
static EmSceneState scene;
EmSceneState *em_scene_state(void) { return &scene; }
void em_frame_set_message_service(const EmFrameMessageService *service) { (void)service; }
int em_hud_tall_glyph_cell(uint32_t index, EmHudGlyphCell *cell)
{
    (void)index; (void)cell;
    return 1;
}
void em_hud_glyph_strip(EmGfx *gfx, const EmMessageGlyphFlush *flush) { (void)gfx; (void)flush; }

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
    EmMessageBlock *block = em_message_live_block();
    block->phase = f->frame.message_phase;
    int result = em_message_live_post(token, (int32_t)delay) == 0;
    f->frame.message_phase = block->phase;
    return result;
}

static int message_done(void *context)
{
    Fixture *f = context;
    return f->failure == 4 ? -1 : f->frame.message_phase == 2;
}

static int battery_open(void *context, EmPanel *owner, uint8_t request)
{
    Fixture *f = context;
    assert(owner == &f->panel.owner && request == 0x82);
    assert(owner->status == 1 && !owner->armed && !owner->charged);
    if (f->failure == 5) return 0;
    ++f->menu_requested;
    memset(f->page, 0, sizeof f->page);
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
    memset(&scene, 0, sizeof scene);
    scene.d810700 = 0x0B;
    assert(em_message_live_install("assets/message/message_data.emmd"));
    /* No voiced line here: the voice lanes stay idle (the lanes' 001FAAC0 on
     * an idle lane has no effect; D_00282155/156 read 0). */
    static const EmMessageLiveStreams idle_lanes = {NULL, NULL, NULL, NULL, idle_stop_lane, NULL};
    em_message_live_set_streams(&idle_lanes);
    assert(em_message_live_reset() == 0);
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
    EmMessageBlock *block = em_message_live_block();
    f->frame.message_phase = block->phase;
    if (em_panel_runtime_tick(&f->panel, battery, 1) < 0) return -1;
    block->phase = f->frame.message_phase;
    /* Step F; a present tick of the timed line 0x80000018 (not its
     * zero-duration terminal 0x80000019) is a visible frame. */
    int32_t frames = block->frames;
    assert(em_message_live_tick() == 0);
    if (block->frames == frames + 1 && block->current == 0x80000018u) ++f->message_visible;
    if (f->entered && !f->frame.selector && f->complete_tick < 0)
        f->complete_tick = f->tick;
    ++f->tick;
    return 0;
}

/* The BATTERY page is the original 002149F0 (em_status_page_record) over
 * this fixture's records; its draws, cues and message line are this
 * fixture's boundaries, and its owner record is the fixture panel at the
 * captured address 0x7AA590 (+3 type 0x24, +0x34 cost, +0xA / +0xB). */
#define PANEL_ADDRESS 0x7AA590u
static int page_ok(void *c) { (void)c; return 0; }
static int page_draw(void *c, uint8_t *page, uint32_t table, int32_t flags)
{ (void)c; (void)page; (void)table; (void)flags; return 0; }
static int page_background(void *c, uint64_t tex0) { (void)c; (void)tex0; return 0; }
static int page_list(void *c, uint8_t *page, uint32_t table, uint64_t glyph, int32_t flags,
                     int32_t *result)
{ (void)c; (void)page; (void)table; (void)glyph; (void)flags; *result = 0; return 0; }
static int page_arrows(void *c, uint8_t *page, uint32_t table)
{ (void)c; (void)page; (void)table; return 0; }
static int page_refill(void *c, uint8_t *page, int32_t n) { (void)c; (void)page; (void)n; return -1; }
static int page_marker(void *c, uint8_t *page) { (void)c; (void)page; return 0; }
static int page_blend(void *c, int32_t slot, int32_t mode) { (void)c; (void)slot; (void)mode; return 0; }
static int page_sound(void *c, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    Fixture *f = c;
    assert(a1 == 0x1000 && a2 == 0x1000 && a3 == 0x1000);
    if (id == 6) ++f->units;
    return 0;
}
static int page_find(void *c, int32_t item, uint32_t *owner)
{ (void)c; assert(item == 0x1B); *owner = PANEL_ADDRESS; return 0; }
static int page_read(void *c, uint32_t owner, uint32_t offset, uint32_t size, int32_t *value)
{
    Fixture *f = c;
    assert(owner == PANEL_ADDRESS);
    if (offset == 3 && size == 1) { *value = 0x24; return 0; }
    if (offset == 0x34 && size == 2) { *value = (int16_t)f->panel.owner.cost; return 0; }
    return -1;
}
static int page_write(void *c, uint32_t owner, uint32_t offset, uint8_t value)
{
    Fixture *f = c;
    assert(owner == PANEL_ADDRESS);
    if (offset == 0xA) f->panel.owner.charged = value;
    else if (offset == 0xB) f->panel.owner.armed = value;
    else return -1;
    return 0;
}

/* One 002149F0 frame with `buttons` (D_00810E74). */
static int page_tick(Fixture *f, unsigned buttons, uint8_t *b0, uint8_t *c5, uint8_t *spad3B8D)
{
    static const uint8_t b1 = 0x82, counts[3] = {1, 0, 0}, d0[4] = {0x90, 0xA5, 0x7A, 0x00};
    uint8_t charge[2] = {(uint8_t)f->charge, (uint8_t)(f->charge >> 8)}, capacity = 12;
    uint8_t pressed[2] = {(uint8_t)buttons, (uint8_t)(buttons >> 8)};
    int32_t mode = 4, phase = 0, line = 0, group = 0;
    const EmSprRecords r = {f->page, sizeof f->page, b0, &b1, c5, d0, counts, charge, &capacity,
                            pressed, &mode, &phase, &line, &group, spad3B8D};
    const EmSprWorkers w = {f, page_background, page_draw, page_list, page_arrows, page_refill,
                            page_list, page_marker, page_ok, page_ok, page_ok, page_ok, page_ok,
                            page_blend, page_sound, page_find, page_read, page_write};
    int rc = em_spr_002149F0(&w, &r, NULL);
    f->charge = charge[0] | charge[1] << 8;
    return rc;
}

static void menu(Fixture *f, int discharge)
{
    assert(f->status_active && f->panel.owner.phase == 6);
    freeze(f, 1);
    uint8_t b0 = 1, c5 = 0, spad3B8D = 0;
    /* State 0 takes the request (B1 & 0x80: the confirmation, No). */
    assert(page_tick(f, 0, &b0, &c5, &spad3B8D) == 0 && f->page[5] == 4 && f->page[6] == 1);
    if (!discharge) {
        assert(page_tick(f, 0x40, &b0, &c5, &spad3B8D) == 0); /* No */
        assert(f->page[5] == 1 && f->charge == 12 && !f->panel.owner.charged && !c5);
        /* The real status/root worker must continue to handle Back. This
         * fixture explicitly supplies its eventual completed-exit boundary. */
        freeze(f, 1);
    } else {
        assert(page_tick(f, 0x8040, &b0, &c5, &spad3B8D) == 0); /* Yes */
        assert(f->page[5] == 6 && b0 == 1);
        for (int i = 1; i <= 61; ++i) {
            unsigned units = f->units;
            assert(page_tick(f, 0, &b0, &c5, &spad3B8D) == 0);
            if (f->units != units) assert(i == 1 || i == 31);
            if (c5 == 0xFF && !f->finished) { assert(i == 61); f->finished = 1; }
        }
        assert(f->units == 2 && f->finished == 1 && f->charge == 8 && f->panel.owner.armed == 5);
        assert(f->panel.owner.charged == 1 && spad3B8D == 3);
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
    em_message_live_shutdown();
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
    em_message_live_shutdown();
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
