/* Host-binding test for the WP-15/H11 reversal skid in em_player.c.
 *
 * Links the real em_player.c, em_player_reversal.c, em_player_motor.c and
 * em_player_heading.c against a fake pose source, model and frame input
 * (defined below) and drives player_move() through a run-speed reversal.
 * The skid logic itself is checked against original instructions by
 * tools/test_player_reversal_reference.py; this test checks only that the
 * port's callback feeds and consumes that logic in the original order:
 * detection tick (turn suppressed, clip request blend 4, sound 0x137,
 * translation), state-2 ticks without the 00160220 Use poll, the effect
 * worker fault, the pi turn at the clip end, resume and idle hand-off, and
 * cancellation when another owner resets the scalar mode. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game/em_player.h"
#include "game/em_player_reversal.h"
#include "game/em_scene_bindings.h"

EmGameState g;
const float kLocoTierSpeed[4] = {0.0f, 0.1f, 0.3f, 0.8f};

/* ---- fake frame input -------------------------------------------------- */
static EmFrameInput input;
static EmPadUnpack pad;
const EmFrameInput *em_frame_input(void) { return &input; }
const EmPadUnpack *em_frame_pad_block(void) { return &pad; }

static void stick(uint8_t x, uint8_t y, uint8_t gait)
{
    input.lx = pad.lx = x;
    input.ly = pad.ly = y;
    pad.gait = gait;
}

/* ---- fake original source (player_pose_*) ------------------------------ */
typedef struct { unsigned id, frames; int ends; } FakeClip;
static const FakeClip kBank[] = {
    {0, 80, 0}, {1, 120, 0}, {2, 45, 0}, {3, 40, 0},
    {6, 15, 1}, {7, 15, 1}, {8, 1, 1}, {9, 1, 1},
};
static struct {
    int valid, bank_has_skid;
    unsigned clip, flags, frames, blend_left;
    float remaining;
    int ends;
    unsigned requests, last_blend, last_force, holds, use_polls, idle_enters;
    float last_frame;
} source;

static const FakeClip *bank_clip(unsigned id)
{
    for (unsigned i = 0; i < sizeof kBank / sizeof *kBank; ++i)
        if (kBank[i].id == id && (source.bank_has_skid || id < 6)) return &kBank[i];
    return NULL;
}

void player_pose_request(unsigned clip, float frame, unsigned blend, int force)
{
    const FakeClip *entry = bank_clip(clip);
    if (!source.valid) return;
    if (!entry) { source.valid = 0; return; }
    ++source.requests;
    source.last_blend = blend;
    source.last_force = (unsigned)force;
    source.last_frame = frame;
    if (!force && clip == source.clip) return;
    source.clip = clip;
    source.frames = entry->frames;
    source.ends = entry->ends;
    source.remaining = (float)entry->frames - frame;
    source.blend_left = blend;
    source.flags = blend ? 0x8000u : 0;
}

int player_pose_source(unsigned *clip, float *remaining, unsigned *flags, int *transition)
{
    if (!source.valid) return 0;
    if (clip) *clip = source.clip;
    if (remaining) *remaining = source.blend_left ? (float)source.blend_left : source.remaining;
    if (flags) *flags = source.flags;
    if (transition) *transition = source.blend_left != 0;
    return 1;
}

/* 0015BA50 order: advance by the prior +204 before the state callback. */
static void source_advance(void)
{
    if (!source.valid) return;
    if (source.blend_left) {
        if (--source.blend_left == 0) source.flags &= ~0x8000u;
        return;
    }
    source.remaining -= g.loco_rate;
    if (source.remaining <= 0) {
        if (source.ends) { source.remaining = 0; source.flags |= 0x1000u; }
        else source.remaining += (float)source.frames;
    }
}

void player_pose_unsupported_hold(const char *reason) { (void)reason; ++source.holds; }
void player_pose_legacy_hold(const char *owner) { (void)owner; }
int player_pose_legacy_release(void) { return 1; }
int player_use_poll(void) { ++source.use_polls; return 0; }
int player_pose_entry_return_tick(void) { return 0; }
int player_pose_idle_state_wait(void) { return 0; }
void player_pose_idle_enter(void) { ++source.idle_enters; }
void player_pose_entry_cancel(void) {}
int player_pose_foot_stop_active(void) { return 0; }
int player_pose_foot_stop_begin(void) { return 0; }
int player_pose_foot_stop_tick(void) { return 0; }
void player_pose_invalidate(const char *reason) { (void)reason; source.valid = 0; }

/* ---- fake model / world / audio ---------------------------------------- */
static EmModelClip model_clips[8];
int em_model_clip_index(const EmModel *m, uint32_t id)
{
    for (uint32_t i = 0; i < m->clip_count; ++i)
        if (m->clips[i].id == id) return (int)i;
    return -1;
}
void em_model_palette_at(const EmModel *m, uint32_t clip, double t, float *out)
{
    (void)clip; (void)t;
    for (uint32_t i = 0; i < m->bone_count * 16; ++i) out[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}
void palette_apply_placement(float *pal, uint32_t n, const float pos[3], float yaw)
{
    (void)pal; (void)n; (void)pos; (void)yaw;
}

static unsigned sounds[16], sound_count;
void em_sfx_play(unsigned id) { if (sound_count < 16) sounds[sound_count++] = id; }
int em_sfx_cue_state(unsigned id) { (void)id; return 1; }
void em_sfx_play_at(unsigned id, const float pos[3], float radius) { (void)id; (void)pos; (void)radius; }
uint32_t em_random_next(void) { return 0; }

int em_door_transit_active(float t[3], float *yaw) { (void)t; (void)yaw; return 0; }
int em_door_walkout_active(float *yaw, float *speed) { (void)yaw; (void)speed; return 0; }
int em_door_movement_locked(void) { return 0; }
int em_examine_input_locked(void) { return 0; }
int em_game_player_interact_busy(void) { return 0; }
int em_weapon_is_aiming(void) { return 0; }
int em_weapon_is_melee(void) { return 0; }
int em_weapon_lock_steer(const float p[3], float y, float a, float b, float *c, float *d)
{
    (void)p; (void)y; (void)a; (void)b; (void)c; (void)d; return 0;
}
int em_game_anim_hold(unsigned id, float rate) { (void)id; (void)rate; return 0; }
void em_game_anim_cancel(void) {}
unsigned em_game_anim_active(void) { return 0; }
/* Collision is absent (poly_count 0): these must not be reached. */
int em_collision_segment_query(const EmCollision *c, const float a[3], const float b[3],
                               unsigned mask, int id, EmCollHit *hit)
{
    (void)c; (void)a; (void)b; (void)mask; (void)id; (void)hit; assert(!"collision"); return 0;
}
int em_collision_moving_carry(float pos[3]) { (void)pos; assert(!"carry"); return 0; }
int em_collision_blocker_probe(float pos[3], float radius) { (void)pos; (void)radius; assert(!"blocker"); return 0; }
int em_collision_move_probe(const EmCollision *c, float pos[3], const float target[3],
                            unsigned mask, EmCollHit *hit)
{
    (void)c; (void)pos; (void)target; (void)mask; (void)hit; assert(!"probe"); return 0;
}
int em_door_count(void) { assert(!"door"); return 0; }
int em_door_probe(const float from[3], const float to[3], EmCollHit *hit)
{
    (void)from; (void)to; (void)hit; assert(!"door"); return 0;
}
/* D_00810700 for the probe workers (never reached without collision). */
static EmSceneState scene_state;
EmSceneState *em_scene_state(void) { return &scene_state; }

static uint32_t effects[16];
static unsigned effect_count;
static int effect_worker(void *context, uint32_t id, const float position[3], float yaw)
{
    (void)context; (void)position; (void)yaw;
    if (effect_count < 16) effects[effect_count++] = id;
    return 0;
}

static int failing_effect_worker(void *context, uint32_t id, const float position[3], float yaw)
{
    (void)context; (void)id; (void)position; (void)yaw;
    return -1;
}

/* ---- scenario ---------------------------------------------------------- */
static void reset(int bank_has_skid)
{
    memset(&g, 0, sizeof g);
    memset(&source, 0, sizeof source);
    sound_count = effect_count = 0;
    static const uint32_t ids[8] = {0, 1, 2, 3, 6, 7, 8, 9};
    static const uint32_t lengths[8] = {80, 120, 45, 40, 15, 15, 1, 1};
    for (int i = 0; i < 8; ++i)
        model_clips[i] = (EmModelClip){ids[i], 0, lengths[i], 60.0f};
    g.model.clips = model_clips;
    g.model.clip_count = 8;
    g.model.bone_count = 22;
    g.clip_walk = 1; g.clip_jog = 2; g.clip_run = 3; g.clip_idle = 0;
    g.status.health = 100;
    g.cam.fwd[2] = 1.0f;                       /* forward stick = +Z */
    g.pos[0] = 50; g.pos[2] = -150;
    g.yaw = 0;
    /* Running at tier 3, 0.8 units/tick, run clip playing. */
    g.loco_mode = 1; g.loco_substate = 0; g.loco_tier = 3;
    g.loco_upt = 0.8f; g.loco_rate = 1;
    source.valid = 1;
    source.bank_has_skid = bank_has_skid;
    player_pose_request(3, 0, 0, 1);
    source.requests = 0;
    player_reversal_set_effect_worker(NULL, NULL);
    player_reversal_bind_display(1);
}

static void tick(void)
{
    source_advance();
    player_move();
}

static void detection_tick(void)
{
    stick(128, 255, 3);                          /* full reverse */
    float z = g.pos[2];
    tick();
    assert(g.yaw == 0.0f);                       /* 00174AC0 skipped the turn */
    assert(g.loco_mode == 6);
    assert(g.loco_substate == 3 || g.loco_substate == 4);
    assert(source.clip == (g.loco_substate == 3 ? 6u : 7u));
    assert(source.last_blend == 4 && source.last_force == 0);
    assert(sound_count == 1 && sounds[0] == 0x137);
    assert(fabsf(g.pos[2] - (z + 0.8f)) < 1e-5f);  /* 00178B90 at +38 = 0.8 */
    assert(player_reversal_owns_walk());
    assert(source.use_polls == 1);               /* case 1 polls 00160220 */
    assert(player_reversal_palette() == 1);
}

int main(void)
{
    /* 0. Gate: until display, effect worker and clips 6/7 are all bound the
     *    skid never engages and the ordinary 00174AC0 turn runs instead. */
    for (int missing = 0; missing < 3; ++missing) {
        reset(1);
        if (missing != 1) player_reversal_set_effect_worker(effect_worker, NULL);
        if (missing == 0) player_reversal_bind_display(0);
        if (missing == 2) g.model.clip_count = 4;   /* clips 6/7 absent */
        stick(128, 255, 3);
        unsigned gate_faults = player_reversal_faults();
        tick();
        assert(g.yaw != 0.0f);                   /* ordinary turn ran */
        assert(g.loco_mode != 6 && g.loco_mode != 7);
        assert(sound_count == 0 && !player_reversal_owns_walk());
        assert(player_reversal_faults() == gate_faults);
    }

    /* 1. A failing effect worker: the first state-2 tick faults visibly. */
    reset(1);
    player_reversal_set_effect_worker(failing_effect_worker, NULL);
    detection_tick();
    unsigned faults = player_reversal_faults();
    tick();
    assert(player_reversal_faults() == faults + 1);
    assert(source.holds == 1 && g.loco_mode == 0 && !player_reversal_owns_walk());
    assert(source.use_polls == 1);

    /* 2. Bound effect: skid, clip end, pi turn, resume with the stick held. */
    reset(1);
    player_reversal_set_effect_worker(effect_worker, NULL);
    detection_tick();
    uint8_t variant = g.loco_substate;
    unsigned skid_ticks = 0;
    while (g.loco_mode == 6) {
        float speed = g.loco_upt;
        tick();
        ++skid_ticks;
        assert(source.use_polls == 1);           /* case 2 never polls 00160220 */
        if (g.loco_mode == 6) {
            assert(g.loco_rate == 0.75f);        /* 0017C030 case 6 */
            assert(g.loco_upt == fmaxf(0.0f, speed - 0.05f) || g.loco_upt < speed);
        }
        assert(skid_ticks < 64);
    }
    assert(effect_count >= 1 && effects[0] == 0x80000012u);  /* +23A 0, no depth */
    assert(effect_count == (skid_ticks + 7) / 8);
    /* Exit tick: follow-up clip, zero speed and tier, heading turned by pi. */
    assert(source.clip == (variant == 3 ? 8u : 9u));
    assert(source.last_force == 1 && source.last_blend == 0);
    assert(g.loco_upt == 0 && g.loco_tier == 0);
    assert(fabsf(fabsf(g.yaw) - 3.1415927f) < 1e-5f);
    assert(player_reversal_owns_walk());
    /* Resume tick: gait 3 -> tier 2 at 0.3, rising to 0.3625 this tick. */
    tick();
    assert(!player_reversal_owns_walk());
    assert(g.loco_mode == 1 && g.loco_substate == 1);
    assert(g.loco_tier == 2 && fabsf(g.loco_upt - 0.3625f) < 1e-6f);
    assert(source.clip == 2);
    if (variant == 3) assert(source.last_force == 0 && source.last_frame == 0);
    else assert(source.last_force == 1 && source.last_frame == 22.5f);
    assert(player_reversal_palette() == 0);

    /* 3. Stick released at the end: idle hand-off through the stop phase 3. */
    reset(1);
    player_reversal_set_effect_worker(effect_worker, NULL);
    detection_tick();
    while (g.loco_mode == 6) tick();
    stick(128, 128, 0);
    tick();
    assert(!player_reversal_owns_walk());
    assert(g.loco_mode == 0 && g.loco_stop.phase == 3);
    assert(g.model.clips[g.loco_stop_clip].id == source.clip);
    tick();                                      /* 00161020 case 0 next */
    assert(source.idle_enters == 1 && g.loco_stop.phase == 4);

    /* 4. Another owner resets +1F0 mid-skid: the saved +6 is dropped. */
    reset(1);
    player_reversal_set_effect_worker(effect_worker, NULL);
    detection_tick();
    g.loco_mode = 0;
    player_pose_request(0, 0, 0, 1);
    faults = player_reversal_faults();
    stick(128, 128, 0);
    tick();
    assert(!player_reversal_owns_walk() && player_reversal_faults() == faults);

    /* 5. A source bank without the skid clips faults at the request. */
    reset(0);
    player_reversal_set_effect_worker(effect_worker, NULL);
    stick(128, 255, 3);
    faults = player_reversal_faults();
    tick();
    assert(player_reversal_faults() == faults + 1 && g.loco_mode == 0);

    printf("player reversal host PASS\n");
    return 0;
}
