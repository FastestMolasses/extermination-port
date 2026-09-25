/* Host-binding test for the live player states in em_player.c
 * (em_player.h "Live player states", docs/FIRST_CONTROL.md).
 *
 * Links the real em_player.c and em_player_floor.c (00175900 / 00175CF0 /
 * 001796C0, oracle-tested by tools/test_player_floor_reference.py) with the
 * real actor-collision ground worker (em_actor_collision.c, 0019AB20, oracle-
 * tested by tools/test_actor_collision_reference.py) over a synthetic cell
 * directory, and fakes for everything else. It checks only the binding:
 *   - the gate: with any prerequisite missing nothing of the live layer runs;
 *   - the stage (census L01): 0015BA50 / 0015B130 / 0015BCF0's tail run on
 *     every stage, the port's idle and walk callbacks included (they are
 *     0015B130's state[0] / state[1]); the takeover stand-in consumes a stage
 *     at 0015B130's prelude position; the vitals are a per-stage view of the
 *     port's storage;
 *   - once engaged, the idle and walk tails lower +B4 by 0.2 / 0.4 and run
 *     00175900(p, 1) then 001796C0 through the bound workers in that order;
 *   - 0015BA50's per-stage +A / +214 / +B moves and the +214 store from the
 *     owner the ground probe hit (standing on a published actor cell);
 *   - 00175640 over EmActor records (em_actor_collision_player_link);
 *   - the fall (state 5) and slide (state 0x1C) entries park the port's
 *     locomotion and the next stage runs the bound state callback;
 *   - the 0x5A first-contact sound, and fail-stop on a failing worker;
 *   - a +4 = 2 reaction state (em_player_reaction.c 0021E9C0, oracle-tested by
 *     tools/test_player_reaction_reference.py) bound in 0015B770's table:
 *     entered by the 0021C440 stage worker (a fake that makes its store),
 *     run with its EmPlayerReaction context over the real floor service, and
 *     handed back to the port's idle.
 * No disc data. Build (ASan/UBSan), see the Makefile target
 * test-player-states-host. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game/em_actor_collision.h"
#include "game/em_coll_probe_original.h"
#include "game/em_effect_color.h"
#include "game/em_ee_float.h"
#include "game/em_area11_boxes.h"
#include "game/em_camera_leftovers.h"
#include "game/em_player.h"
#include "game/em_player_climb.h"
#include "game/em_player_reaction.h"
#include "game/em_player_slide.h"
#include "game/em_scene_bindings.h"

EmGameState g;
const float kLocoTierSpeed[4] = {0.0f, 0.1f, 0.3f, 0.8f};

/* ---- fake frame input -------------------------------------------------- */
static EmFrameInput input;
static EmPadUnpack pad;
const EmFrameInput *em_frame_input(void) { return &input; }
const EmPadUnpack *em_frame_pad_block(void) { return &pad; }
static unsigned quit_requests;
void em_frame_request_quit(void) { ++quit_requests; }

/* ---- fake original source (player_pose_*) ------------------------------ */
static struct { unsigned requests, idle_enters; } source;
void player_pose_request(unsigned clip, float frame, unsigned blend, int force)
{
    (void)clip; (void)frame; (void)blend; (void)force;
    ++source.requests;
}
int player_pose_source(unsigned *clip, float *remaining, unsigned *flags, int *transition)
{
    if (clip) *clip = 1;
    if (remaining) *remaining = 60;
    if (flags) *flags = 0;
    if (transition) *transition = 0;
    return 1;
}
void player_pose_unsupported_hold(const char *reason) { (void)reason; }
void player_pose_legacy_hold(const char *owner) { (void)owner; }
/* The stage's two camera-side stores (census L13..L16): 0015BCF0's
 * 0x700031F0 = 0 (em_area11_boxes' carry word) and 0015CBA0 (the camera's
 * action map, em_camera_leftovers with its own oracle). Neither is this
 * fixture's subject. */
int32_t *em_area11_boxes_carry31F0(void) { static int32_t carry; return &carry; }
int em_camleft_0015CBA0(EmPlayerLiveActor *p) { (void)p; return 0; }
/* 0015BCF0's 00187350 on the record runs only when the closure binder bound
 * 00161020 / 001612D0 (census L12); this fixture binds the legacy callbacks. */
int em_player_closure_live_footstep(EmPlayerLiveActor *p) { (void)p; assert(!"footstep"); return -1; }
/* No scripted takeover in this fixture (em_player.c reads it at every stage). */
int player_pose_owned(void) { return 0; }
int player_pose_legacy_release(void) { return 1; }
int player_use_poll(void) { return 0; }
int player_pose_use_accepted_port(void) { return 1; } /* never reached: player_use_poll returns 0 */
int player_pose_entry_return_tick(void) { return 0; }
int player_pose_idle_state_wait(void) { return 0; }
void player_pose_idle_enter(void) { ++source.idle_enters; }
void player_pose_entry_cancel(void) {}
int player_pose_foot_stop_active(void) { return 0; }
int player_pose_foot_stop_begin(void) { return 0; }
int player_pose_foot_stop_tick(void) { return 0; }
/* 0015BCF0's animate step on the record (em_player_pose_host.c): counted. */
static unsigned animate_calls;
int player_pose_animate(void) { ++animate_calls; return 0; }
void player_pose_invalidate(const char *reason) { (void)reason; }

/* ---- fake model / audio / other owners --------------------------------- */
static EmModelClip model_clips[4];
int em_model_clip_index(const EmModel *m, uint32_t id)
{
    for (uint32_t i = 0; i < m->clip_count; ++i)
        if (m->clips[i].id == id) return (int)i;
    return -1;
}
void em_model_palette_at(const EmModel *m, uint32_t clip, double t, float *out)
{
    (void)clip; (void)t;
    for (uint32_t i = 0; i < m->bone_count * 16; ++i) out[i] = 0.0f;
}
void palette_apply_placement(float *pal, uint32_t n, const float pos[3], float yaw)
{
    (void)pal; (void)n; (void)pos; (void)yaw;
}
static unsigned positional[8], positional_count;
void em_sfx_play(unsigned id) { (void)id; }
int em_sfx_cue_state(unsigned id) { (void)id; return 1; }
void em_sfx_play_at(unsigned id, const float pos[3], float radius)
{
    (void)pos; (void)radius;
    if (positional_count < 8) positional[positional_count++] = id;
}
uint32_t em_random_next(void) { return 0; }
int em_door_transit_active(float t[3], float *yaw) { (void)t; (void)yaw; return 0; }
int em_door_walkout_active(float *yaw, float *speed) { (void)yaw; (void)speed; return 0; }
int em_door_movement_locked(void) { return 0; }
int em_door_movement_stage_release(void) { return 0; }
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
int em_door_count(void) { assert(!"door"); return 0; }
int em_door_probe(const float from[3], const float to[3], EmCollHit *hit)
{
    (void)from; (void)to; (void)hit; assert(!"door"); return 0;
}
static EmSceneState scene_state;
EmSceneState *em_scene_state(void) { return &scene_state; }

/* ---- the actor-cell world: one published owner with a top face --------- */
static void put_u32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put_u16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void put_f(uint8_t *p, float v) { memcpy(p, &v, 4); }

/* uid 0: one 0x2000 top face (face 3) at y 10 over x 0..4, z 0..4. */
static size_t build_directory(uint8_t *image)
{
    memset(image, 0, 0x100);
    put_u32(image, 1);
    uint32_t at = 0x10;
    put_u32(image + 4, at);
    const float box[6] = { 0, 10, 0, 4, 10, 4 };
    for (int k = 0; k < 6; ++k) put_f(image + at + 4 * k, box[k]);
    put_u16(image + at + 0x18, 1);
    uint8_t *p = image + at + 0x1C;
    put_u16(p, 0x2000); p[2] = 3;
    put_f(p + 4, 0); put_f(p + 8, 10); put_f(p + 0xC, 0);
    put_f(p + 0x10, 4); put_f(p + 0x14, 0); put_f(p + 0x18, 4);
    return at + 0x1C + 0x1C;
}

static EmActorCellTable table;
static EmActorClassLists lists;
static EmCollision grid;             /* empty grid that carries the node class */
static uint8_t grid_blob[4];
/* Its rank section (0019C830 walks it): one node whose rank tables pick an
 * empty span, so the grid pass reports no hit, as the empty grid did. */
static const float rank_verts[3];
static const int16_t rank_words[12];
static const int16_t rank_tables[12];
static EmCollProbeGrid ranks;
static EmActorCollisionWorld world;
static EmActor box_owner, player_record;
static EmActorCollisionPlayer player_query;

/* ---- fakes for the untranslated workers -------------------------------- */
typedef struct { char name; float y; } Call;
static Call calls[64];
static unsigned call_count;
static void note(char name, float y) { if (call_count < 64) calls[call_count++] = (Call){ name, y }; }

static EmPlayerProbeHit head_hit;    /* the scripted 0019B6C0 answer */
static int head_result;
static int fake_head(void *context, const float top[3], const float bottom[3], EmPlayerProbeHit *hit)
{
    (void)context;
    note('h', bottom[1]);
    assert(fabsf(top[1] - bottom[1] - 18.0f) < 1e-4f);
    *hit = head_hit;
    return head_result < 0 ? head_result : head_hit.kind;
}
/* 001764E0 / 001756E0's original probe workers (EmPlayerStatesBinding.probes):
 * nothing in this synthetic world blocks a lane. */
static int probe_none(void *context, const float a[3], const float b[3], unsigned mask,
                      EmPlayerProbeHit *hit)
{
    (void)context; (void)a; (void)b; (void)mask;
    memset(hit, 0, sizeof *hit);
    return 0;
}
static int probe_column_none(void *context, const float at[3], float height, EmPlayerProbeHit *hit)
{
    (void)context; (void)at; (void)height;
    memset(hit, 0, sizeof *hit);
    return 0;
}
static int probe_shove(void *context, const float target[3]) { (void)context; (void)target; return -1; }
static int probe_target(void *context) { (void)context; return -1; }
static int probe_pose(void *context, float blend) { (void)context; (void)blend; return 0; }
static float probe_sqrt(void *context, float x) { (void)context; return sqrtf(x); }
static float probe_atan(void *context, float x) { (void)context; return atanf(x); }

static int fake_object(void *context, const float at[3], const float probe[3], unsigned mask,
                       EmPlayerProbeHit *hit)
{
    (void)context; (void)probe; (void)mask;
    note('o', at[1]);
    memset(hit, 0, sizeof *hit);
    return 0;
}
static int counted_ground(void *context, const float position[3], const float probe[3],
                          unsigned mask, EmPlayerProbeHit *hit)
{
    note('g', position[1]);
    assert(probe[0] == 0.0f && probe[1] == -13.8f && probe[2] == 0.0f && mask == 6);
    return em_actor_collision_player_ground(context, position, probe, mask, hit);
}
/* The SDK workers' fault latch (EmPlayerStatesBinding.sdk_fault): the
 * original translations record a fault instead of returning one. */
static uint32_t sdk_fault_word;
static int faulting_ground(void *context, const float position[3], const float probe[3],
                           unsigned mask, EmPlayerProbeHit *hit)
{
    sdk_fault_word = 0x0011E398u;   /* as a faulted SDK call records it */
    return counted_ground(context, position, probe, mask, hit);
}
static EmPlayerProbeHit slope_hit;   /* a scripted 0019AB20 answer (slide case) */
static int scripted_ground(void *context, const float position[3], const float probe[3],
                           unsigned mask, EmPlayerProbeHit *hit)
{
    (void)context; (void)probe; (void)mask;
    note('g', position[1]);
    *hit = slope_hit;
    return hit->kind;
}
static const void *link_seen[8];
static unsigned link_count;
static int counted_link(void *context, const void *owner, int *result)
{
    if (link_count < 8) link_seen[link_count++] = owner;
    return em_actor_collision_player_link(context, owner, result);
}
static int column_count_value;
static int fake_column(void *context, const float position[3], EmPlayerFloorTable *out)
{
    (void)context;
    note('c', position[1]);
    memset(out, 0, sizeof *out);
    out->count = column_count_value;
    return 0;
}
static float sdk_atan2(void *c, float y, float x) { (void)c; return atan2f(y, x); }
static float sdk_tan(void *c, float x) { (void)c; return tanf(x); }
static float sdk_atan(void *c, float x) { (void)c; return atanf(x); }
static float sdk_sqrt(void *c, float x) { (void)c; return sqrtf(x); }

/* State callbacks: record the entry bytes 0015BA50 reset, then exit to the
 * scripted (+4, +5) and leave the bytes a state routine writes. */
static unsigned state_runs[EM_PLAYER_STATE_COUNT], state2_runs[EM_PLAYER_STATE2_COUNT];
static unsigned major_runs[EM_PLAYER_MAJOR_COUNT];
static uint8_t state_exit_major = 1, state_exit = 0;
static struct { uint8_t lean, b303, b318, b1; uint16_t b94; float rate, step; } entry;
static int record_entry(EmPlayerLiveActor *actor)
{
    entry.lean = em_live_u8(actor, 0x25D);
    entry.b303 = em_live_u8(actor, 0x303);
    entry.b318 = em_live_u8(actor, 0x318);
    entry.b1 = em_live_u8(actor, 1);
    entry.b94 = em_live_u16(actor, 0x94);
    entry.rate = em_live_f32(actor, 0x204);
    entry.step = em_live_f32(actor, 0x34);
    /* What 0016C6A0 / 00161790 leave: the lean latch, the one-shot rate. */
    em_live_set_u8(actor, 0x25D, 1);
    em_live_set_f32(actor, 0x204, 2.0f);
    em_live_set_u8(actor, 0x303, 1);
    em_live_set_u8(actor, 0x318, 1);
    em_live_set_u8(actor, 1, 0);
    em_live_set_u16(actor, 0x94, 5);
    em_live_set_u8(actor, 4, state_exit_major);
    em_live_set_u8(actor, 5, state_exit);
    em_live_set_u8(actor, 6, 0);
    return 0;
}
static int fake_state(void *context, EmPlayerLiveActor *actor)
{
    (void)context;
    ++state_runs[em_live_u8(actor, 5)];
    return record_entry(actor);
}
static int fake_state2(void *context, EmPlayerLiveActor *actor)
{
    (void)context;
    ++state2_runs[em_live_u8(actor, 5)];
    return record_entry(actor);
}
static int fake_major(void *context, EmPlayerLiveActor *actor)
{
    (void)actor;
    ++major_runs[(uintptr_t)context];
    return 0;
}
static int stops[4], stop_count;
static int fake_stop(void *context, int handle)
{
    (void)context;
    if (stop_count < 4) stops[stop_count++] = handle;
    return 0;
}
/* The stage workers: D_00248C98 as a fixed rate, anim_advance_time and the
 * 0015B130 wrapper's workers as recorders. */
static unsigned stage_calls[16];
enum { SC_ADVANCE, SC_COMMIT, SC_REACTION, SC_DRAIN, SC_HEARTBEAT, SC_CHECK, SC_NOTIFY, SC_ROW,
       SC_FADE };
static float advanced_step;
static int reaction_result;
static uint32_t advance_flags = 0x10;
static int w_rate(void *c, int clip, float *rate) { (void)c; *rate = clip == 1 ? 1.25f : 1.0f; return 0; }
static int w_advance(void *c, EmPlayerLiveActor *a, float step, uint32_t *flags)
{
    (void)c; (void)a; ++stage_calls[SC_ADVANCE]; advanced_step = step; *flags = advance_flags; return 0;
}
static int w_commit(void *c, EmPlayerLiveActor *a, int *r) { (void)c; (void)a; ++stage_calls[SC_COMMIT]; *r = 0; return 0; }
static int w_reaction(void *c, EmPlayerLiveActor *a, int *r)
{
    (void)c; (void)a; ++stage_calls[SC_REACTION]; *r = reaction_result; return 0;
}
static int w_drain(void *c, EmPlayerLiveActor *a) { (void)c; (void)a; ++stage_calls[SC_DRAIN]; return 0; }
static int w_heartbeat(void *c, EmPlayerLiveActor *a) { (void)c; (void)a; ++stage_calls[SC_HEARTBEAT]; return 0; }
static int w_check(void *c, EmPlayerLiveActor *a, int *r) { (void)c; (void)a; ++stage_calls[SC_CHECK]; *r = 1; return 0; }
static int w_notify(void *c, EmPlayerLiveActor *a) { (void)c; (void)a; ++stage_calls[SC_NOTIFY]; return 0; }
static int w_row(void *c, EmPlayerLiveActor *a, float blend) { (void)c; (void)a; (void)blend; ++stage_calls[SC_ROW]; return 0; }
static int fade_args[2];
static int w_fade(void *c, int a0, int a1) { (void)c; ++stage_calls[SC_FADE]; fade_args[0] = a0; fade_args[1] = a1; return 0; }
static EmPlayerStageFade fade = { NULL, w_fade };

/* The 0021E9C0 reaction bound through EmPlayerReaction: recording workers,
 * the real floor service over the live actor, a refresh that fills the root
 * translation. The fake 0021C440 enters +4 2 +5 0x11 (its store at
 * 0021C81C) once and reports a reaction. */
static char reaction_calls[32];
static unsigned reaction_call_count, refreshes;
static void rnote(char c) { if (reaction_call_count < 32) reaction_calls[reaction_call_count++] = c; }
static int r_request(void *c, EmPlayerLiveActor *a, int clip, int force, float blend)
{
    (void)c; (void)a; (void)force; (void)blend; assert(clip == 0x20); rnote('q'); return 0;
}
static int r_arbiter(void *c, EmPlayerLiveActor *a, int clip, float blend, float frame)
{
    (void)c; (void)a; (void)clip; (void)blend; (void)frame; rnote('A'); return 0;
}
static int r_frames(void *c, EmPlayerLiveActor *a, int clip, int *frames)
{
    (void)c; (void)a; (void)clip; *frames = 0; rnote('F'); return 0;
}
static int r_sound(void *c, EmPlayerLiveActor *a, unsigned id)
{
    (void)c; (void)a; assert(id == 0x154); rnote('s'); return 0;
}
static int r_rumble(void *c, int x, int y, int z, int u)
{
    (void)c; assert(x == 0 && y == 0xC0 && z == 5 && u == 1); rnote('r'); return 0;
}
static int r_random(void *c, uint32_t *v) { (void)c; *v = 0; rnote('R'); return 0; }
static int r_effect(void *c, uint32_t id, const float p[4], const float q[4])
{
    (void)c; (void)id; (void)p; (void)q; rnote('e'); return 0;
}
static int r_attach(void *c, EmPlayerLiveActor *a, uint32_t id, uint32_t *h)
{
    (void)c; (void)a; (void)id; *h = 0; rnote('a'); return 0;
}
static int r_floor(void *c, EmPlayerLiveActor *a, int search, int *result)
{
    rnote('f');
    return player_states_floor_service(c, a, search, result);
}
static int r_translate(void *c, EmPlayerLiveActor *a, int arg)
{
    (void)c; (void)a; assert(arg == 1); rnote('t'); return 0;
}
static int r_actor(void *c, EmPlayerLiveActor *a) { (void)c; (void)a; rnote('p'); return 0; }
static int r_heading(void *c, EmPlayerLiveActor *a, int arg) { (void)c; (void)a; (void)arg; rnote('h'); return 0; }
static int r_skeleton(void *c, EmPlayerLiveActor *a, float n[3])
{
    (void)c; (void)a; n[0] = n[1] = n[2] = 0; rnote('k'); return 0;
}
static int r_fade(void *c, int x, int y) { (void)c; (void)x; (void)y; rnote('d'); return 0; }
static int r_plain(void *c) { (void)c; rnote('m'); return 0; }
static float r_atan2(void *c, float y, float x) { (void)c; rnote('T'); return atan2f(y, x); }
static int r_w0021C270(void *c, EmPlayerLiveActor *a) { (void)c; (void)a; rnote('M'); return 0; }
static int r_w0021C350(void *c, EmPlayerLiveActor *a) { (void)c; (void)a; rnote('D'); return 0; }
static uint8_t shared_8106F1;            /* the one D_008106F1 byte */
static EmPlayerReactionScene reaction_scene = { .d8106F1 = &shared_8106F1 };
static int r_refresh(void *c, EmPlayerReactionScene *s)
{
    (void)c; ++refreshes; s->root8 = 1.5f; return 0;
}
static EmPlayerLandScratch reaction_scratch;   /* the shared 0x700038A0 / 0x70003A20 words */
static EmPlayerReaction reaction_binding = {
    { NULL, r_request, r_arbiter, r_frames, r_sound, r_rumble, r_random, r_effect, r_attach,
      r_floor, r_translate, r_actor, r_heading, r_skeleton, r_fade, r_actor, r_plain,
      r_atan2, r_w0021C270, r_w0021C350, &reaction_scratch },
    &reaction_scene, r_refresh, NULL
};
/* A 0021C440 stand-in for this binding test only: once, it makes the store
 * 0021C440 makes for a pending +F 6 with health > 0 (+4 2 +5 0x11 +6 0 +1F0
 * 0x3E at 0021C81C, +F |= 0x80) and reports a start. The translation is lane
 * player-stage-workers' (em_player_stage_workers.c). */
static int reaction_entries;
static int w_reaction_enter(void *c, EmPlayerLiveActor *a, int *r)
{
    (void)c; ++stage_calls[SC_REACTION];
    *r = 0;
    if (reaction_entries-- > 0) {
        em_live_set_u8(a, 4, 2); em_live_set_u8(a, 5, 0x11); em_live_set_u8(a, 6, 0);
        em_live_set_u8(a, 0x1F0, 0x3E);
        em_live_set_u8(a, 0xF, em_live_u8(a, 0xF) | 0x80);
        *r = 1;
    }
    return 0;
}

static EmPlayerStatesBinding full_binding(void)
{
    EmPlayerStatesBinding b;
    memset(&b, 0, sizeof b);
    b.ground = counted_ground; b.ground_context = &player_query;
    b.grid = &grid;
    b.head = fake_head; b.object = fake_object;
    b.probes = (EmPlayerProbeWorkers){ NULL, probe_none, probe_none, probe_column_none, probe_shove,
                                       probe_target, probe_pose, probe_sqrt, probe_atan };
    b.link_test = counted_link;
    b.column = fake_column;
    b.atan2 = sdk_atan2; b.tangent = sdk_tan; b.atan = sdk_atan; b.sqrt = sdk_sqrt;
    EmPlayerStageWorkers *w = &b.stage;
    w->clip_rate = w_rate; w->advance = w_advance; w->commit = w_commit;
    w->reaction = w_reaction; w->drain = w_drain; w->heartbeat = w_heartbeat;
    w->scripted_check = w_check; w->scripted_notify = w_notify; w->row_request = w_row;
    w->stop_sound = fake_stop;
    /* The FLOOR closure and the Use roots (em_player.c kFloorStates /
     * kUseStates). */
    static const uint8_t kStates[] = { 2, 3, 4, 5, 6, 7, 8, 9, 0xB, 0xC, 0xE, 0x10, 0x12, 0x13,
                                       0x14, 0x18, 0x19, 0x1A, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
                                       0x21, 0x22, 0x24 };
    for (unsigned i = 0; i < sizeof kStates; ++i) w->state[kStates[i]] = fake_state;
    static const uint8_t kStates2[] = { 0, 1, 2, 3, 4, 5, 6, 7, 0xA, 0xB, 0xC, 0xF, 0x10, 0x11,
                                        0x12, 0x13, 0x14, 0x16, 0x17, 0x18, 0x19 };
    for (unsigned i = 0; i < sizeof kStates2; ++i) w->state2[kStates2[i]] = fake_state2;
    w->major[4] = fake_major; w->major_context[4] = (void *)(uintptr_t)4;
    w->major[6] = em_player_stage_0015D460; w->major_context[6] = &fade;
    return b;
}

static void world_init(void)
{
    static uint8_t image[0x100];
    size_t size = build_directory(image);
    assert(em_actor_cells_init(&table, image, size) == 0);
    memset(&box_owner, 0, sizeof box_owner);
    box_owner.status = 1; box_owner.cls = 4; box_owner.uid = 0; box_owner.kind = 0x05;
    box_owner.model = 6; box_owner.callback = 0x001551B0u;   /* a crate-like owner */
    box_owner.self = &box_owner;
    em_actor_class_lists_reset(&lists);
    em_actor_class_push4_001B1D20(&lists, &box_owner);
    em_actor_class_lists_swap_001AAD00(&lists);
    memset(&grid, 0, sizeof grid);
    grid.flags = EM_COLL_FLAG_GRID | EM_COLL_FLAG_NODE_CLASS;
    grid.blob = grid_blob;
    ranks = (EmCollProbeGrid){ &grid, 1, 0, 1, rank_verts, rank_words, rank_tables, NULL, NULL };
    world = (EmActorCollisionWorld){ &table, &lists, &grid, NULL, 0, &ranks };
    memset(&player_record, 0, sizeof player_record);
    player_record.cls = 1; player_record.self = &player_record;
    player_query = (EmActorCollisionPlayer){ &world, { &player_record, 1, NULL }, NULL };
}

static void reset(void)
{
    memset(&g, 0, sizeof g);
    memset(&source, 0, sizeof source);
    static const uint32_t ids[4] = {0, 1, 2, 3};
    static const uint32_t lengths[4] = {80, 120, 45, 40};
    for (int i = 0; i < 4; ++i) model_clips[i] = (EmModelClip){ids[i], 0, lengths[i], 60.0f};
    g.model.clips = model_clips; g.model.clip_count = 4; g.model.bone_count = 22;
    g.clip_walk = 1; g.clip_jog = 2; g.clip_run = 3; g.clip_idle = 0;
    g.status.health = 100;
    g.cam.fwd[2] = 1.0f;
    g.pos[0] = 2; g.pos[1] = 10; g.pos[2] = 2;
    input.lx = pad.lx = input.ly = pad.ly = 128; pad.gait = 0;
    call_count = link_count = positional_count = 0;
    stop_count = 0;
    quit_requests = 0;
    memset(state_runs, 0, sizeof state_runs);
    memset(state2_runs, 0, sizeof state2_runs);
    memset(major_runs, 0, sizeof major_runs);
    memset(stage_calls, 0, sizeof stage_calls);
    memset(&scene_state, 0, sizeof scene_state);
    reaction_result = 0;
    advance_flags = 0x10;
    state_exit_major = 1;
    memset(&head_hit, 0, sizeof head_hit);
    head_hit.kind = 4; head_hit.node = 0x4005; head_hit.point[1] = 10;
    head_result = 0;
    column_count_value = 0;
    state_exit = 0;
    player_states_reset();
    /* +20C is the record pose's own field (001749A0 / 001749F0 write it; no
     * mirror since the display step): the fake source plays clip 1. */
    em_live_set_u16(player_states_actor_mut(), 0x20C, 1);
    EmPlayerStatesBinding b = full_binding();
    player_states_bind(&b);
    player_states_bind_display(1);
    player_states_bind_use_chain(1);
}

static void tick(void)
{
    ++g.frame_no;
    /* em_player_frame.c actor_update: the original stage once engaged. */
    if (player_states_stage_live()) (void)player_states_stage();
    else player_move();
}

/* The takeover stand-in (EmPlayerStatesBinding.takeover): consumes the
 * stage while set, and stores 3B8F as the interaction runtime's frame view
 * does. */
static int takeover_consumes, takeover_calls;
static int w_takeover(void *c)
{
    (void)c;
    ++takeover_calls;
    if (takeover_consumes) scene_state.spad3B8F = 2;
    return takeover_consumes;
}

int main(void)
{
    world_init();

    /* 0. The gate. Unbound: every prerequisite is reported and nothing of the
     *    live layer runs (the port's own path, here without collision). */
    reset();
    player_states_bind(NULL);
    player_states_bind_display(0);
    player_states_bind_use_chain(0);
    assert(player_states_missing() == 0x3FFFu);
    assert(!player_states_engaged(EM_PLAYER_MECH_FLOOR));
    tick();
    assert(call_count == 0 && g.pos[1] == 10.0f);  /* the port's idle tail only */
    /* Each FLOOR prerequisite alone keeps it gated off. */
    static const unsigned kFloorBits[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13 };
    for (unsigned k = 0; k < sizeof kFloorBits / sizeof *kFloorBits; ++k) {
        unsigned bit = kFloorBits[k];
        reset();
        EmPlayerStatesBinding b = full_binding();
        switch (1u << bit) {
        case EM_PLAYER_NEED_GROUND: b.ground = NULL; break;
        case EM_PLAYER_NEED_NODE_CLASS: grid.flags = EM_COLL_FLAG_GRID; break;
        case EM_PLAYER_NEED_HEAD: b.head = NULL; break;
        case EM_PLAYER_NEED_OBJECT: b.object = NULL; break;
        case EM_PLAYER_NEED_LINK: b.link_test = NULL; break;
        case EM_PLAYER_NEED_COLUMN: b.column = NULL; break;
        case EM_PLAYER_NEED_SDK: b.tangent = NULL; break;
        case EM_PLAYER_NEED_DISPLAY: player_states_bind_display(0); break;
        case EM_PLAYER_NEED_FLOOR_STATES: b.stage.state2[0x16] = NULL; break;
        case EM_PLAYER_NEED_STOP_SOUND: b.stage.stop_sound = NULL; break;
        case EM_PLAYER_NEED_STAGE: b.stage.clip_rate = NULL; break;
        case EM_PLAYER_NEED_PROBES: b.probes.move = NULL; break;
        }
        player_states_bind(&b);
        assert(player_states_missing() == (1u << bit));
        assert(!player_states_engaged(EM_PLAYER_MECH_FLOOR));
        tick();
        assert(call_count == 0);
        grid.flags = EM_COLL_FLAG_GRID | EM_COLL_FLAG_NODE_CLASS;
    }
    /* The Use mechanism additionally needs the chain and its states. */
    reset();
    player_states_bind_use_chain(0);
    assert(player_states_engaged(EM_PLAYER_MECH_FLOOR) && !player_states_engaged(EM_PLAYER_MECH_USE));
    reset();
    assert(player_states_missing() == 0 && player_states_engaged(EM_PLAYER_MECH_USE));

    /* 0b. The SDK fault latch: the floor service and the fall check clear it
     *     before they run (a stale fault does not fail a clean call) and fail
     *     when a worker recorded one during the call (no value is used). */
    {
        reset();
        EmPlayerStatesBinding b = full_binding();
        b.sdk_fault = &sdk_fault_word;
        player_states_bind(&b);
        EmPlayerLiveActor *live_actor = player_states_actor_mut();
        em_live_set_f32(live_actor, 0xB4, 10.0f);
        int contact = -1;
        sdk_fault_word = 0x1234u;
        assert(player_states_floor_service(NULL, live_actor, 1, &contact) == 0 && sdk_fault_word == 0);
        sdk_fault_word = 0x1234u;
        assert(player_states_fall_check(NULL, live_actor) == 0 && sdk_fault_word == 0);
        b.ground = faulting_ground;
        player_states_bind(&b);
        assert(player_states_floor_service(NULL, live_actor, 1, &contact) == -1);
        sdk_fault_word = 0;
    }

    /* 1. Engaged, idle on the published box top (y 10): the idle tail lowers
     *    +B4 by 0.2, 0019AB20 finds the face 13.8 above -> +B4 back to 10,
     *    the owner goes to +214, contact 1 | 0x80, the surface record gives
     *    +23A; the fall check sees contact and calls nothing. */
    reset();
    animate_calls = 0;
    tick();
    /* The port's idle callback owned the stage: its legacy display stays
     * (player_states_record_display 0); 0015BCF0 evaluated the record once. */
    assert(animate_calls == 1 && !player_states_record_display());
    assert(call_count == 2 && calls[0].name == 'g' && calls[1].name == 'h');
    assert(calls[0].y == em_ee_add(10.0f, -0.2f));
    assert(g.pos[1] == 10.0f && calls[1].y == 10.0f);
    const EmPlayerLiveActor *a = player_states_actor();
    assert(a->link_owner == &box_owner && a->link_prev == NULL);
    /* +B: the service's floor hit sets it; 0015BA50 clears it after the
     * switch. +BC: 0015BCF0 writes 1.0 every stage. The idle stage runs
     * 0015B130 around the port's own callback (its state[0]), as the
     * original runs it on every +4 = 1 stage: the display's advance, then
     * 0021C440, the callback, 0015D100 (+20E = 0) and 0015D000. */
    assert(em_live_u8(a, 0xA) == 0x81 && em_live_u8(a, 0xB) == 0 && em_live_u8(a, 0x23A) == 5);
    assert(em_live_f32(a, 0xBC) == 1.0f && stage_calls[SC_REACTION] == 1);
    assert(stage_calls[SC_ADVANCE] == 1 && stage_calls[SC_DRAIN] == 1 &&
           stage_calls[SC_HEARTBEAT] == 1 && stage_calls[SC_CHECK] == 0);
    assert(em_live_u16(a, 0x238) == 0x4000 && em_live_u8(a, 0x23B) == 0x05);
    assert(em_live_u8(a, 5) == 0 && link_count == 0 && quit_requests == 0);

    assert(em_live_u8(a, 0x319) == 0);                 /* +319 = the previous +A */
    /* Next stage: 0015BA50 moved +214 to +308 before the service stored it
     * again. */
    tick();
    assert(a->link_prev == &box_owner && a->link_owner == &box_owner);
    assert(em_live_u8(a, 0x319) == 0x81);

    /* 2. 00175640 over EmActor records (byte-matched rule). */
    int linked = -1;
    assert(em_actor_collision_player_link(NULL, NULL, &linked) == 0 && linked == 0);
    EmActor probe_actor;
    memset(&probe_actor, 0, sizeof probe_actor);
    static const struct { uint8_t model; uint32_t callback; int want; } kLink[] = {
        { 0x0C, 0, 1 }, { 0x2A, 0, 1 }, { 0x0A, 0, 1 }, { 0x18, 0, 1 }, { 2, 0, 1 },
        { 6, 0x00156F30u, 1 }, { 6, 0x00827880u, 1 }, { 6, 0x00828700u, 1 },
        { 6, 0x001551B0u, 0 }, { 0x24, 0, 0 },
    };
    for (unsigned i = 0; i < sizeof kLink / sizeof *kLink; ++i) {
        probe_actor.model = kLink[i].model; probe_actor.callback = kLink[i].callback;
        assert(em_actor_collision_player_link(NULL, &probe_actor, &linked) == 0);
        assert(linked == kLink[i].want);
    }
    assert(em_actor_collision_player_link(NULL, &probe_actor, NULL) == -1);

    /* 2b. 00179450's column table through the actor-collision world: the
     *     published face is an entry at its height; without the SDK workers
     *     the adapter faults instead of substituting host math. */
    EmPlayerFloorTable column;
    const float over_box[3] = { 2, 5, 2 };
    EmActorCollisionPlayerColumn column_query = { &world, NULL };
    assert(em_actor_collision_player_column(&column_query, over_box, &column) == -1);
    static const EmCollColumnMath kMath = { sdk_sqrt, sdk_atan, NULL };
    column_query.math = &kMath;
    assert(em_actor_collision_player_column(&column_query, over_box, &column) == 0);
    int found_top = 0;
    for (int i = 0; i < column.count; ++i)
        if (column.height[i] == 10.0f && (column.flags[i] & 1)) found_top = 1;
    assert(column.count >= 1 && found_top);

    /* 3. Walk tail: lowered by 0.4 (the walk callback). */
    reset();
    g.loco_mode = 1; g.loco_substate = 0; g.loco_tier = 1; g.loco_upt = 0.0f; g.loco_rate = 1;
    pad.gait = 1; input.ly = pad.ly = 0;               /* hold forward, walk gait */
    tick();
    assert(call_count >= 2 && calls[0].name == 'g');
    assert(calls[0].y == em_ee_add(10.0f, -0.4f));

    /* 4. Step off the box: no floor under the feet. 001796C0 drops -0.04 per
     *    stage for three stages, then 00179450 over an empty column table
     *    enters the fall (+5 5, +1F0 11, +25F 2): the port parks and the next
     *    stage runs 0015BA50's switch: anim_advance_time(+34), then 0015B130
     *    (0021C440, the state-5 routine, 0015D100 on +20E = 0, 0015D000). The
     *    fake 00162DB0 exits to 7 (as at 001632BC), 7 to +4 2 / +5 3 (as
     *    0017C580's reset), and 0015B770's 3 back to idle. */
    reset();
    g.pos[0] = 20;                                     /* outside the face */
    head_hit.kind = 0;
    float y = g.pos[1];
    int drops = 0;
    for (;;) {
        call_count = 0;
        tick();
        assert(em_live_u8(a, 0xA) == 0);
        if (calls[call_count - 1].name == 'c') break;  /* 00179450's column table */
        assert(em_live_u8(a, 5) == 0 && ++drops <= 4);
        y = em_ee_add(em_ee_add(y, -0.2f), em_live_f32(a, 0x2EC));  /* lowered, then dropped */
        assert(g.pos[1] == y);
    }
    assert(drops == 3 || drops == 4);                  /* until drop <= -0.04 x 3 */
    assert(em_live_u8(a, 5) == 5 && em_live_u8(a, 0x1F0) == 11 && em_live_u8(a, 0x25F) == 2);
    assert(g.loco_mode == 11 && g.loco_upt == 0);
    state_exit = 7;
    call_count = 0;
    memset(stage_calls, 0, sizeof stage_calls);        /* count from the fall stage */
    animate_calls = 0;
    tick();
    assert(state_runs[5] == 1 && call_count == 0);     /* the callback owned the stage */
    assert(animate_calls == 1 && player_states_record_display()); /* the record is displayed */
    assert(stage_calls[SC_ADVANCE] == 1 && advanced_step == 1.25f);
    assert(stage_calls[SC_REACTION] == 1 && stage_calls[SC_DRAIN] == 1 &&
           stage_calls[SC_HEARTBEAT] == 1);
    assert(em_live_u32(a, 0x200) == 0x10);
    assert(em_live_u8(a, 5) == 7 && g.loco_mode == 11);
    state_exit_major = 2; state_exit = 3;
    tick();
    assert(state_runs[7] == 1 && em_live_u8(a, 4) == 2 && em_live_u8(a, 5) == 3);
    state_exit_major = 1; state_exit = 0;
    tick();
    assert(state2_runs[3] == 1 && stage_calls[SC_REACTION] == 3 && stage_calls[SC_DRAIN] == 2);
    assert(em_live_u8(a, 4) == 1 && em_live_u8(a, 5) == 0 && g.loco_mode == 0 &&
           g.loco_stop.phase == 3);
    assert(quit_requests == 0);

    /* 5. A class-0x1000 floor (the hill's authored nodes): 00175640 gets the
     *    stored +214 (none here), no push, +237 = 1 with the downhill heading,
     *    and 001796C0 enters the slide (+5 0x1C, +1F0 0x30). */
    reset();
    EmPlayerStatesBinding b = full_binding();
    b.ground = scripted_ground;
    player_states_bind(&b);
    memset(&slope_hit, 0, sizeof slope_hit);
    slope_hit.kind = 4; slope_hit.node = 0x105A;
    slope_hit.normal[0] = 0.5f; slope_hit.normal[1] = 0.8660254f;
    slope_hit.point[1] = 10.0f; slope_hit.delta[1] = 0.2f;
    head_hit.node = 0x405A;                            /* snow record: 0x5A */
    tick();
    assert(link_count == 1 && link_seen[0] == NULL);
    assert(em_live_u8(a, 0x237) == 1 && em_live_u8(a, 5) == 0x1C && em_live_u8(a, 0x1F0) == 0x30);
    assert(g.loco_mode == 0x30);
    assert(positional_count == 1 && positional[0] == 0x86);   /* 00187DC0 on 0x5A */
    /* The slide callback leaves 0x1C with its loop sound 0x12E playing:
     * 0015BCF0 stops it (0011A070(+31B)) and clears +31B/+31A. */
    EmPlayerLiveActor *m = player_states_actor_mut();
    em_live_set_u8(m, 0x31A, 1); em_live_set_u8(m, 0x31B, 3); em_live_set_u16(m, 0x31C, 0x12E);
    state_exit = 0x1C;                                 /* stays in the slide */
    tick();
    assert(state_runs[0x1C] == 1 && stop_count == 0);  /* 0x12E plays on in 0x1C */
    /* 0015BA50 before the next callback: the lean latch +25D and the other
     * per-stage bytes the slide left are reset, +34 = rate(+20C) * the +204
     * it left (2.0), +204 = 1.0; no stale +25D reaches 0016C6A0. */
    state_exit = 0;
    tick();
    assert(state_runs[0x1C] == 2);
    assert(entry.lean == 0 && entry.b303 == 0 && entry.b318 == 0 && entry.b1 == 1);
    assert(entry.b94 == 0xFFFF && entry.rate == 1.0f && entry.step == 2.5f);
    assert(advanced_step == 2.5f);
    /* The slide leaves 0x1C with its loop sound 0x12E playing: 0015BCF0
     * stops it (0011A070(+31B)) and clears +31B/+31A. */
    assert(stop_count == 1 && stops[0] == 3);
    assert(em_live_u8(a, 0x31B) == 0xFF && em_live_u8(a, 0x31A) == 0);

    /* 6. A failing worker on the live path is fail-stop and counted. */
    reset();
    head_result = -1;
    unsigned faults = player_states_faults();
    tick();
    assert(player_states_faults() == faults + 1 && quit_requests == 1);

    /* 7. A +5 without a callback (unreachable once engaged; defended). */
    reset();
    b = full_binding();
    player_states_bind(&b);
    em_live_set_u8(player_states_actor_mut(), 5, 0x11);   /* 0016AC50: outside the closure */
    faults = player_states_faults();
    tick();
    assert(player_states_faults() == faults + 1 && quit_requests == 1);

    /* 9. 0015BCF0's loop-sound stop runs on every stage, the port's idle
     *    stage included (0x135 outside +5 0x17). */
    reset();
    EmPlayerLiveActor *m9 = player_states_actor_mut();
    em_live_set_u8(m9, 0x31A, 1); em_live_set_u8(m9, 0x31B, 2); em_live_set_u16(m9, 0x31C, 0x135);
    em_live_set_f32(m9, 0xBC, 0.0f);
    tick();
    assert(stop_count == 1 && stops[0] == 2 && em_live_u8(a, 0x31B) == 0xFF);
    assert(em_live_f32(a, 0xBC) == 1.0f);

    /* 10. 0015BCF0's -200 check: below -200 the stage ends with +4 6, +5 0;
     *     the next stages run 0015D460 (em_player_stage_0015D460): +5 1 with
     *     +220 = 0 and +0 = 0, then +5 2 with 001AEDE0(4, 0). */
    reset();
    g.pos[1] = -250.0f; g.pos[0] = 20;
    head_hit.kind = 0;
    em_live_set_f32(player_states_actor_mut(), 0x220, 100.0f);
    em_live_set_u8(player_states_actor_mut(), 0, 1);
    tick();
    assert(em_live_u8(a, 4) == 6 && em_live_u8(a, 5) == 0);
    memset(stage_calls, 0, sizeof stage_calls);        /* the +4 = 6 stages */
    tick();
    assert(em_live_u8(a, 4) == 6 && em_live_u8(a, 5) == 1 && em_live_u32(a, 0x220) == 0 &&
           em_live_u8(a, 0) == 0 && stage_calls[SC_FADE] == 0);
    /* The vitals view: +220 = 0 is the port's health after the stage (the
     * B9 test of 0015CF90 and the status pages read it there). */
    assert(g.status.health == 0.0f);
    tick();
    assert(em_live_u8(a, 5) == 2 && stage_calls[SC_FADE] == 1 && fade_args[0] == 4 &&
           fade_args[1] == 0 && stage_calls[SC_ADVANCE] == 0 && quit_requests == 0);
    tick();
    assert(em_live_u8(a, 5) == 2 && stage_calls[SC_FADE] == 1);  /* parked */

    /* 11. 0015B130's prelude under the scripted takeover (0x70003B8D): +1F0
     *     0x17 forces +4 4 / +5 0xC and 00182D70 without 0021C440; the next
     *     stage advances by +34 (+5 is not 0 / 0x17) and runs 0015B530. */
    reset();
    scene_state.spad3B8D = 3;
    EmPlayerLiveActor *m11 = player_states_actor_mut();
    em_live_set_u8(m11, 5, 5); em_live_set_u8(m11, 0x1F0, 0x17);
    tick();
    assert(em_live_u8(a, 4) == 4 && em_live_u8(a, 5) == 0xC && stage_calls[SC_NOTIFY] == 1);
    assert(stage_calls[SC_REACTION] == 0 && state_runs[5] == 0);
    tick();
    assert(major_runs[4] == 1 && stage_calls[SC_ADVANCE] == 2 && stage_calls[SC_COMMIT] == 0);
    /* Case 0x19 under the takeover: 0x70003B8F = 1, then +1 = 0 instead of
     * 0016DE40. */
    reset();
    scene_state.spad3B8D = 3;
    em_live_set_u8(player_states_actor_mut(), 5, 0x19);
    tick();
    assert(scene_state.spad3B8F == 1 && state_runs[0x19] == 0 && em_live_u8(a, 1) == 0);
    assert(stage_calls[SC_CHECK] == 0 && stage_calls[SC_REACTION] == 1);
    /* 0021C440 reacting skips the dispatch and the countdown. */
    reset();
    reaction_result = 1;
    em_live_set_u8(player_states_actor_mut(), 5, 5);
    g.pd_iframes = 2;                  /* +20E: the port's storage (vitals view) */
    tick();
    assert(state_runs[5] == 0 && em_live_u16(a, 0x20E) == 2 && stage_calls[SC_HEARTBEAT] == 0);
    /* The +20E countdown: 1 -> 0 sets +0 = 1, and 0015D100 is skipped. */
    reset();
    em_live_set_u8(player_states_actor_mut(), 5, 5);
    g.pd_iframes = 1;
    state_exit = 5;
    tick();
    assert(em_live_u16(a, 0x20E) == 0 && em_live_u8(a, 0) == 1 && stage_calls[SC_DRAIN] == 0);
    assert(stage_calls[SC_HEARTBEAT] == 1);
    /* D_00810CB6 is canonical D2 progress since WP-6 (0 here), so the
     * stage's D_008106B3 is known: neither CB6 nor the +4 2 / +5 0xB..0xF
     * reaction arm holds. */
    assert(player_states_busy() == 0);

    /* 12. A reaction: 0021C440 (the stand-in above) enters +4 2 +5 0x11 on a
     *     0015B130 stage (here the fall, +5 5), so the state dispatch and
     *     the countdown are skipped; 0015B770 then
     *     runs em_player_reaction_live_0021E9C0 from state2[0x11]: sub-state
     *     0 (sound 0x154, rumble, clip 0x20, 00179880, 00175900), sub-state 1
     *     integrating the root (+38 = 1.5 - 0) and translating, and on the clip
     *     end (+200 & 0x1000) 0017C540 hands back to +4 1 +5 0, where the
     *     port's idle takes over through stop phase 3. */
    reset();
    b = full_binding();
    b.stage.reaction = w_reaction_enter;
    b.stage.state2[0x11] = em_player_reaction_live_0021E9C0;
    b.stage.state2_context[0x11] = &reaction_binding;
    player_states_bind(&b);
    reaction_entries = 1; reaction_call_count = 0; refreshes = 0;
    em_live_set_f32(player_states_actor_mut(), 0x220, 100.0f);
    em_live_set_u8(player_states_actor_mut(), 5, 5);
    em_live_set_u8(player_states_actor_mut(), 0xF, 6);
    tick();
    assert(em_live_u8(a, 4) == 2 && em_live_u8(a, 5) == 0x11 && em_live_u8(a, 0xF) == 0x86);
    assert(reaction_call_count == 0 && refreshes == 0);
    assert(state_runs[5] == 0 && stage_calls[SC_HEARTBEAT] == 0);
    tick();
    assert(refreshes == 1 && em_live_u8(a, 6) == 1 && memcmp(reaction_calls, "srqf", 4) == 0);
    tick();
    assert(refreshes == 2 && em_live_f32(a, 0x38) == 1.5f && em_live_f32(a, 0x21C) == 1.5f);
    assert(memcmp(reaction_calls + 4, "tf", 2) == 0 && em_live_u8(a, 4) == 2);
    advance_flags = 0x1000;
    tick();
    assert(em_live_u8(a, 4) == 1 && em_live_u8(a, 5) == 0 && em_live_u8(a, 0xF) == 0);
    assert(em_live_u16(a, 0x20E) == 0x3C && em_live_u8(a, 0x25C) == 0);
    assert(g.loco_mode == 0 && g.loco_stop.phase == 3 && quit_requests == 0);
    advance_flags = 0x10;
    /* The adapter refuses (before any write) without its scene. */
    reaction_binding.scene = NULL;
    EmPlayerLiveActor *m12 = player_states_actor_mut();
    EmPlayerLiveActor kept = *m12;
    assert(em_player_reaction_live_0021E9C0(&reaction_binding, m12) == -1);
    assert(memcmp(&kept, m12, sizeof kept) == 0);
    reaction_binding.scene = &reaction_scene;

    /* 13. Census L01 binding specifics.
     *  a) The takeover stand-in consumes the stage at 0015B130's prelude
     *     position: 0015B130 (0021C440, the callback, 0015D100, 0015D000)
     *     does not run, 0015BA50's begin / end and 0015BCF0's writes do, and
     *     the 3B8F the stand-in stored is not overwritten by the stage's
     *     earlier view. */
    reset();
    b = full_binding();
    b.takeover = w_takeover;
    player_states_bind(&b);
    takeover_consumes = 1; takeover_calls = 0;
    em_live_set_f32(player_states_actor_mut(), 0xBC, 0.0f);
    call_count = 0;
    assert(player_states_stage() == 1);
    assert(takeover_calls == 1 && stage_calls[SC_ADVANCE] == 1 && stage_calls[SC_REACTION] == 0 &&
           stage_calls[SC_DRAIN] == 0 && stage_calls[SC_HEARTBEAT] == 0 && call_count == 0);
    assert(em_live_f32(a, 0xBC) == 1.0f && scene_state.spad3B8F == 2 && quit_requests == 0);
    assert(player_states_record_display());           /* the takeover's record pose */
    player_states_bind_display(0);
    assert(player_states_stage() == 1 && !player_states_record_display());
    player_states_bind_display(1);
    /*  b) Not consumed: 0015B130 runs after the stand-in. */
    takeover_consumes = 0;
    assert(player_states_stage() == 0);
    assert(takeover_calls == 3 && stage_calls[SC_REACTION] == 1 && call_count >= 2);
    assert(!player_states_record_display());           /* the port's idle callback ran */
    /*  c) 0x70003B8D without the owner on the port's idle (the area-change
     *     fade): the prelude's 00174A50 needs 0017B490 (L12), so the port's
     *     callback keeps the stage; 0015B130 and its prelude do not run. */
    scene_state.spad3B8D = 3;
    memset(stage_calls, 0, sizeof stage_calls);
    call_count = 0;
    assert(player_states_stage() == 0);
    assert(stage_calls[SC_CHECK] == 0 && stage_calls[SC_NOTIFY] == 0 &&
           stage_calls[SC_REACTION] == 0 && call_count >= 2 && em_live_u8(a, 4) == 1);
    scene_state.spad3B8D = 0;
    /*  d) The vitals are a per-stage view of the port's storage: pending
     *     damage the port's producers left reaches the stage's 0021C440 as
     *     +224, and what the stage leaves is the port's value after it. */
    g.pd_pend_hp = 7.0f;
    g.status.health = 90.0f;
    g.pd_iframes = 2;
    player_states_stage();
    assert(g.status.health == 90.0f && g.pd_pend_hp == 7.0f);   /* the fake 0021C440 keeps them */
    assert(em_live_f32(a, 0x224) == 7.0f && em_live_f32(a, 0x220) == 90.0f);
    assert(g.pd_iframes == 1 && em_live_u16(a, 0x20E) == 1);    /* 0015B130's countdown */
    assert(quit_requests == 0);

    /* 8. The state adapters refuse to run with any worker unbound, before
     *    touching the live actor (their mirrors are checked against the
     *    oracles' offset tables in the slide/climb reference tests). */
    reset();
    EmPlayerLiveActor *m8 = player_states_actor_mut();
    em_live_set_u8(m8, 5, 0x1C);
    EmPlayerLiveActor before = *m8;
    EmPlayerSlideLive slide_live;
    memset(&slide_live, 0, sizeof slide_live);
    assert(em_player_slide_live_state(NULL, m8) == -1);
    assert(em_player_slide_live_state(&slide_live, m8) == -1);
    slide_live.floor = player_states_floor_service;
    slide_live.fall = player_states_fall_check;
    assert(em_player_slide_live_state(&slide_live, m8) == -1);
    assert(memcmp(&before, m8, sizeof before) == 0);
    EmPlayerClimbLive climb_live;
    memset(&climb_live, 0, sizeof climb_live);
    em_live_set_u8(m8, 5, 2);
    before = *m8;
    assert(em_player_climb_live_state(&climb_live, m8) == -1);
    assert(em_player_climb_live_probe(&climb_live, m8, 0, 0.0f) == -1);
    assert(memcmp(&before, m8, sizeof before) == 0);
    /* The +308 owner kind the climb reads (00161790 / 0015DF10). */
    EmActor overlay;
    memset(&overlay, 0, sizeof overlay);
    assert(em_actor_collision_player_link_kind(NULL, NULL) == 0);
    overlay.callback = 0x00828700u;
    assert(em_actor_collision_player_link_kind(NULL, &overlay) == 2);
    overlay.callback = 0x00827880u;
    assert(em_actor_collision_player_link_kind(NULL, &overlay) == 2);
    overlay.callback = 0x001551B0u;
    assert(em_actor_collision_player_link_kind(NULL, &overlay) == 1);
    /* The services refuse to run while the floor is gated off. */
    player_states_bind(NULL);
    int contact = -1;
    assert(player_states_floor_service(NULL, m8, 1, &contact) == -1 && contact == -1);
    assert(player_states_fall_check(NULL, m8) == -1);

    em_actor_cells_free(&table);
    printf("player states host PASS\n");
    return 0;
}
