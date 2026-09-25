/* em_player_closure_live.c - the live binding of the FLOOR state closure and
 * the Use chain (see em_player_closure_live.h). Storage and binding only:
 * every routine reached is the verified translation named at its slot. */
#include "game/em_camera_live.h"
#include "game/em_player_closure_live.h"

#include <stdio.h>
#include <string.h>

#include "game/em_actor_collision.h"
#include "game/em_coll_move_original.h"
#include "game/em_coll_segment_walkers.h"
#include "game/em_coll_list_passes_walkers.h"
#include "game/em_collision_world.h"
#include "game/em_scene_bindings.h"
#include "game/em_ee_float.h"
#include "game/em_effect_original.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_locomotion_display.h"
#include "game/em_owner_services_original.h"
#include "game/em_player_climb.h"
#include "game/em_player_closure_0e_18.h"
#include "game/em_player_closure_10_12_19.h"
#include "game/em_player_fall.h"
#include "game/em_player_hang.h"
#include "game/em_player_heading_record.h"
#include "game/em_player_ladder_climb.h"
#include "game/em_player_ladder_entry.h"
#include "game/em_player_major2.h"
#include "game/em_player_misc_workers.h"
#include "game/em_player_reaction.h"
#include "game/em_player_record_helpers.h"
#include "game/em_player_recovery.h"
#include "game/em_player_running_jump.h"
#include "game/em_player_slide.h"
#include "game/em_player_weapon_states_a.h"
#include "game/em_player_weapon_states_b.h"
#include "game/em_random.h"
#include "game/em_render_verify_rest.h"
#include "game/em_scene_state.h"
#include "game/em_script_host_workers.h"
#include "game/em_sdk_math_original.h"
#include "game/em_sdk_soft_float.h"
#include "game/em_sfx.h"
#include "game/em_startup_load_gaps.h"
#include "game/em_pad_actuator.h"

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

/* D_00275B40 while the player stage runs: 001CB590 points it at the
 * player's node-pointer array, the record's +0x110 (0x8102B0 + 0x110). */
#define PLAYER_NODE_ARRAY UINT32_C(0x008103C0)

/* ---- The binder's storage ------------------------------------------------- */

typedef struct {
    EmPlayerStateCallback fn;
    void *context;
    /* The module's own copy of 0x70003A20 (by value; four bytes of raw
     * bits), or NULL for the modules that use the shared word directly. */
    void *mod3A20;
    /* Called before / after the routine: load / store the module's scene
     * view from / to the canonical storage. 0, or -1. */
    int (*pre)(void);
    int (*post)(void);
    const char *name;
} Slot;

static struct {
    int bound;
    unsigned faults;
    int reported;
    EmPlayerLiveActor *actor;
    EmPoseHost *pose;
    EmPlayerStageHost *stage_host;
    EmSdkMathContext *sdk;
    uint32_t d275B40;
    EmPoseActorView view;

    /* 0x700038A0..AC and 0x70003A20: the one storage (the fall lane's type). */
    EmPlayerLandScratch land;
    /* D_00275B00 + 8 / + 0xC, D_00275B10, D_00275B14 and D_00281B64: .bss
     * words only the closure routines write and read (00181D70, 001696A0,
     * 0016ADE0, 0016B790, 0016B8A0, 002230A0); one storage each, zero at
     * boot as the original's .bss. */
    int32_t d275B08;
    uint32_t d275B0C, d275B10;
    int32_t d275B14;
    uint32_t d281B64;
    /* The hang / ladder / closure hit readers: which query left the
     * scratchpad hit last (0: none, 1: 0019AD00 / 0019AFE0, 2: 0019A570). */
    int last_hit;

    EmPlayerHeadingRecord heading;
    EmPlayerReentryWorkers reentry;
    EmPlayerRecordHelpers helpers;
    EmPlayerMiscWorkers misc_w;
    EmPlayerMiscScene misc_scene;
    EmPlayerMiscScratch misc_scratch;
    EmPlayerMiscHost misc;
    EmPlayerRecoveryLive recovery;
    EmPlayerLandWorkers fall;
    EmPlayerReactionScene reaction_scene;
    EmPlayerReaction reaction;
    EmPlayerHangWorkers hang;
    EmPlayerLadderClimbScene lc_scene;
    EmPlayerLadderClimbWorkers lc_w;
    EmPlayerLadderClimb lc;
    EmPlayerLadderScratch le_scratch;
    EmPlayerLadderWorld le_world;
    EmPlayerLadderWorkers le;
    EmPlayerClosureScratch c0e_scratch;
    EmPlayerClosureWorkers c0e;
    EmPlayerMajor2Scene m2_scene;
    EmPlayerMajor2Workers m2_w;
    EmPlayerMajor2 m2;
    EmPlayerClosure1019Scene c10_scene;
    EmPlayerClosure1019Scratch c10_scratch;
    EmPlayerClosure1019Workers c10_w;
    EmPlayerClosure1019 c10;
    EmPlayerSlideLive slide;
    EmPlayerClimbLive climb;
    EmPlayerWeaponScene wa_scene;
    EmPlayerWeaponWorkers wa_w;
    EmPlayerWeaponStates wa;
    EmPlayerWeaponBScene wb_scene;
    uint16_t wb_pad, wb_3B78;
    EmPlayerWeaponBWorkers wb;
    EmPlayerRunningJumpScratch rj_scratch;
    EmPlayerRunningJumpLive rj;
    EmPlayerUseScene use_scene;
    EmPlayerUseWorkers use;
    int (*scan)(void *context, EmPlayerLiveActor *actor, int *result);
    void *scan_context;
    uint16_t pad_config[8];   /* 0x70003B74..0x70003B82 (001AF470, config 0) */
    EmLocoHost loco;          /* 0017B490's host: its mode worker and the pose */

    Slot state[EM_PLAYER_STATE1_COUNT];
    Slot state2[EM_PLAYER_STATE2_COUNT];
    Slot major4[EM_PLAYER_MAJOR4_COUNT];
} L;

/* The running module's by-value copy of 0x70003A20, or NULL. */
static void *s_mod3A20;
#define IN3A20()  do { if (s_mod3A20) memcpy(&L.land.s3A20, s_mod3A20, 4); } while (0)
#define OUT3A20() do { if (s_mod3A20) memcpy(s_mod3A20, &L.land.s3A20, 4); } while (0)

static int unbound(const char *callee)
{
    ++L.faults;
    if (!L.reported) {
        L.reported = 1;
        fprintf(stderr, "player closure: reached %s, which has no translation on the live path "
                        "(docs/FIRST_CONTROL.md \"Missing today\")\n", callee);
    }
    return -1;
}

const uint16_t *em_player_closure_live_pad_config(void) { return L.pad_config; }

unsigned em_player_closure_live_faults(void) { return L.faults; }

static EmSceneState *scene(void) { return em_scene_state(); }

static int req_byte(uint32_t address, uint8_t *out)
{
    const uint8_t *p = em_scene_req_at(scene(), address);
    if (!p) return -1;
    *out = *p;
    return 0;
}

static int progress_byte(uint32_t address, uint8_t *out)
{
    const uint8_t *p = em_scene_progress_at(scene(), address, 1);
    if (!p) return -1;
    *out = *p;
    return 0;
}

static uint32_t fbits(float v) { return em_ee_bits(v); }
static float bfloat(uint32_t b) { return em_ee_float(b); }

/* ---- The state slots ------------------------------------------------------
 * Every callback the binder installs runs through slot_run: the stage
 * workers' by-value 0x70003A20 (0021C440 / 0021D6C0 store their atan2 there
 * before the callback) is loaded into the one word, the module's own copy
 * (if any) is loaded from it, the SDK fault latch is cleared, and after the
 * routine the words are stored back and a fault an SDK worker recorded
 * fails the routine (no value is substituted). */
static int refresh_all(void);

static int slot_run(void *context, EmPlayerLiveActor *actor)
{
    Slot *slot = context;
    if (!slot || !slot->fn || !L.bound) return -1;
    EmPlayerStageGlobals *g = L.stage_host->globals;
    L.land.s3A20 = g->spad3A20;
    void *outer = s_mod3A20;
    s_mod3A20 = slot->mod3A20;
    OUT3A20();
    L.sdk->fault = 0;
    int r = refresh_all();
    if (r >= 0 && slot->pre) r = slot->pre();
    if (r >= 0) r = slot->fn(slot->context, actor);
    if (r >= 0 && slot->post) r = slot->post();
    IN3A20();
    s_mod3A20 = outer;
    g->spad3A20 = L.land.s3A20;
    if (r >= 0 && L.sdk->fault) {
        fprintf(stderr, "player closure: %s: an SDK worker faulted at 0x%08X\n", slot->name,
                (unsigned)L.sdk->fault);
        r = -1;
    }
    return r < 0 ? -1 : 0;
}

static void slot(Slot *s, EmPlayerStateCallback fn, void *context, void *mod3A20,
                 int (*pre)(void), int (*post)(void), const char *name)
{
    *s = (Slot){ fn, context, mod3A20, pre, post, name };
}

/* ---- Shared original callees over the record ------------------------------ */

/* 001749A0 / anim_clip_arbiter / 001C61D0 / anim_eval_skeleton / 001C68C0 /
 * 0017C540 / 00178910: the record pose (em_pose_host_workers). */
static int w_request(void *c, EmPlayerLiveActor *a, int clip, int flags, float blend)
{
    (void)c; IN3A20();
    int r = em_pose_host_request(L.pose, a, clip, flags, blend);
    OUT3A20(); return r;
}
static int w_arbiter(void *c, EmPlayerLiveActor *a, int clip, float blend, float frame)
{
    (void)c; IN3A20();
    int r = em_pose_host_arbiter(L.pose, a, clip, blend, frame);
    OUT3A20(); return r;
}
static int w_clip_frames(void *c, uint32_t bank, int clip, int32_t *frames)
{
    (void)c;
    return em_pose_host_clip_frames(L.pose, bank, clip, frames);
}
static int w_clip_frames_actor(void *c, EmPlayerLiveActor *a, int clip, int *frames)
{
    (void)c;
    return em_pose_host_clip_frames_actor(L.pose, a, clip, frames);
}
static int w_eval_skeleton(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    return em_pose_host_eval_skeleton(L.pose, a);
}
static int w_skeleton(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    return em_pose_host_skeleton(L.pose, a);
}
static int w_handoff(void *c, EmPlayerLiveActor *a)
{
    (void)c; IN3A20();
    int r = em_pose_host_handoff(L.pose, a);
    OUT3A20(); return r;
}
static int w_ledge_top(void *c, EmPlayerLiveActor *a, int arg, int *result)
{
    (void)c; IN3A20();
    int r = em_pose_host_ledge_top(L.pose, a, arg, result);
    OUT3A20(); return r;
}
/* The node word *(D_00275B40 + 4 node) + offset. */
static int w_node_bits(void *c, int node, unsigned offset, uint32_t *bits)
{
    (void)c;
    return em_pose_view_node_bits(&L.view, node, offset, bits);
}
static int w_node_float(void *c, int node, unsigned offset, float *value)
{
    (void)c;
    return em_pose_view_node_float(&L.view, node, offset, value);
}
static int w_root_node(void *c, unsigned offset, uint32_t *bits)
{
    (void)c;
    return em_pose_view_root_node(&L.view, offset, bits);
}
/* 00174A50(p, blend): the stage workers' row request on the record. */
static int w_row_request(void *c, EmPlayerLiveActor *a, float blend)
{
    (void)c; IN3A20();
    int r = em_player_stage_row_request(L.stage_host, a, blend);
    OUT3A20(); return r;
}

/* 00174AC0 (em_player_heading_record) and 0017C440 (em_player_use_dispatch):
 * both store 0x70003A20 through the one word. */
static int w_heading(void *c, EmPlayerLiveActor *a, int arg)
{
    (void)c; IN3A20();
    int r = em_player_heading_record_worker(&L.heading, a, arg);
    OUT3A20(); return r;
}
static int w_heading_result(void *c, EmPlayerLiveActor *a, int arg, int *result)
{
    (void)c; IN3A20();
    int r = em_player_heading_record_worker_result(&L.heading, a, arg, result);
    OUT3A20(); return r;
}
static int w_reentry(void *c, EmPlayerLiveActor *a, int arg)
{
    (void)c; IN3A20();
    int r = em_player_reentry_worker(&L.reentry, a, arg);
    OUT3A20(); return r;
}

/* 00175900 / 001796C0 / 001764E0 over the record (em_player.c). */
static int w_floor(void *c, EmPlayerLiveActor *a, int search, int *result)
{
    (void)c; IN3A20();
    int r = player_states_floor_service(NULL, a, search, result);
    OUT3A20(); return r;
}
static int w_fall_check(void *c, EmPlayerLiveActor *a)
{
    (void)c; IN3A20();
    int r = player_states_fall_check(NULL, a);
    OUT3A20(); return r;
}
static int w_probes(void *c, EmPlayerLiveActor *a)
{
    (void)c; IN3A20();
    int r = player_states_wall_probes(NULL, a);
    OUT3A20(); return r;
}
/* 001764E0 with the caller's $s1 as the fall lane names it: 0 (00162DB0),
 * the record address 0x8102B0 (001639E0: bit 2 clear), or the stage's
 * inherited value (00163B40's sub-states, like the idle/walk callbacks). */
static int w_probes_s1(void *c, EmPlayerLiveActor *a, int s1)
{
    (void)c; IN3A20();
    uint32_t value = s1 == EM_PLAYER_LAND_S1_ZERO ? 0u
                   : s1 == EM_PLAYER_LAND_S1_RECORD ? UINT32_C(0x008102B0) : 1u;
    int r = player_states_wall_probes_s1(NULL, a, value);
    OUT3A20(); return r;
}
/* The same with the record address in $s1 (0016FD0C / 00170404, 00173000). */
static int w_probes_record(void *c, EmPlayerLiveActor *a)
{
    (void)c; IN3A20();
    int r = player_states_wall_probes_s1(NULL, a, UINT32_C(0x008102B0));
    OUT3A20(); return r;
}

/* 00178B90 and the other recovery routines over the record. */
static int w_translate(void *c, EmPlayerLiveActor *a, int arg)
{
    (void)c; IN3A20();
    int r = em_player_recovery_translate_worker(&L.recovery, a, arg);
    OUT3A20(); return r;
}
static int w_stick(void *c, EmPlayerLiveActor *a)
{
    (void)c; IN3A20();
    int r = em_player_recovery_stick_quadrant_worker(&L.recovery, a);
    OUT3A20(); return r;
}
static int w_strafe(void *c, EmPlayerLiveActor *a)
{
    (void)c; IN3A20();
    int r = em_player_recovery_strafe_worker(&L.recovery, a);
    OUT3A20(); return r;
}
static int w_react_002243F0(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c; IN3A20();
    int r = em_player_recovery_react_002243F0_worker(&L.recovery, a, result);
    OUT3A20(); return r;
}
static int w_ledge_catch(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c; IN3A20();
    int r = em_player_recovery_ledge_catch_worker(&L.recovery, a, result);
    OUT3A20(); return r;
}
static int w_ledge_grab(void *c, EmPlayerLiveActor *a, uint32_t reach, int *result)
{
    (void)c; IN3A20();
    int r = em_player_recovery_ledge_grab_worker(&L.recovery, a, reach, result);
    OUT3A20(); return r;
}

/* The fall lane's translations (0017C580, 00224290, 0021D250, 0021D2E0,
 * 00179880) over its worker table. */
static int w_land(void *c, EmPlayerLiveActor *a)
{
    (void)c; IN3A20();
    int r = em_player_fall_land(&L.fall, a);
    OUT3A20(); return r;
}
static int w_land_check(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c; IN3A20();
    int r = em_player_fall_land_check(&L.fall, a, result);
    OUT3A20(); return r;
}
static int w_surface5d(void *c, EmPlayerLiveActor *a, int arg)
{
    (void)c; IN3A20();
    int r = em_player_fall_0021D250(&L.fall, a, arg);
    OUT3A20(); return r;
}
static int w_teleport(void *c, EmPlayerLiveActor *a, int frames, int hold)
{
    (void)c; IN3A20();
    int r = em_player_fall_0021D2E0(&L.fall, a, frames, hold);
    OUT3A20(); return r;
}
static int w_drop(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    em_player_fall_drop(a);
    return 0;
}

/* The reaction lane's leaves (0021C120, 0021C190, 0021D490, 00182870) and
 * the stage workers' 0021C270 / 0021C350. */
static int w_0021C120(void *c, EmPlayerLiveActor *a)
{
    (void)c; IN3A20();
    int r = em_player_reaction_w0021C120(&L.reaction, a);
    OUT3A20(); return r;
}
static int w_0021C190(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c; IN3A20();
    int r = em_player_reaction_w0021C190(&L.reaction, a, result);
    OUT3A20(); return r;
}
static int w_0021D490(void *c, EmPlayerLiveActor *a)
{
    (void)c; IN3A20();
    int r = em_player_reaction_w0021D490(&L.reaction, a);
    OUT3A20(); return r;
}
static int w_0021C270(void *c, EmPlayerLiveActor *a)
{
    (void)c; IN3A20();
    int r = em_player_0021C270(L.stage_host, a);
    OUT3A20(); return r;
}
static int w_0021C350(void *c, EmPlayerLiveActor *a)
{
    (void)c; IN3A20();
    int r = em_player_0021C350(L.stage_host, a);
    OUT3A20(); return r;
}
static int w_land_sound(void *c, EmPlayerLiveActor *a, int tier)
{
    (void)c; IN3A20();
    int r = em_player_reaction_00182870(a, tier, &L.reaction.workers);
    OUT3A20(); return r;
}

/* 001FBD50(p, id, flags, range): the live positional play at the record's
 * +B0 (the binding the stage's own sound worker uses,
 * em_player_stage_live.c w_sound). The original's v0 (a voice handle) is
 * not kept by these callers except where noted. */
static int w_sound_full(void *c, EmPlayerLiveActor *a, int id, int flags, float range)
{
    (void)c;
    if (!a || id < 0 || flags != 0) return unbound("001FBD50 with a flag argument the live path does not take");
    const float at[3] = { em_live_f32(a, 0xB0), em_live_f32(a, 0xB4), em_live_f32(a, 0xB8) };
    em_sfx_play_at((unsigned)id, at, range);
    return 0;
}
static int w_sound_300(void *c, EmPlayerLiveActor *a, int id)
{
    return w_sound_full(c, a, id, 0, 300.0f);
}
static int w_sound_300_u(void *c, EmPlayerLiveActor *a, unsigned id)
{
    return w_sound_full(c, a, (int)id, 0, 300.0f);
}
/* The same keeping 001FBD50's return, the allocated track (the slide's
 * +31B loop, the melee's +302): em_sfx_play_at_track. */
static int w_sound_handle(void *c, EmPlayerLiveActor *a, int id, int *handle)
{
    (void)c;
    if (!a || id < 0 || !handle) return -1;
    const float at[3] = { em_live_f32(a, 0xB0), em_live_f32(a, 0xB4), em_live_f32(a, 0xB8) };
    *handle = em_sfx_play_at_track((unsigned)id, at, 300.0f);
    return 0;
}
/* 0011A070(handle): bit 15 selects the hard stop (as the stage's
 * 0015BCF0 binding, em_sfx_stop_track). */
static int w_stop_sound(void *c, int handle)
{
    (void)c;
    return em_sfx_stop_track(handle & 0x7FFF, (handle & 0x8000) != 0);
}

/* 001EFD90 / 001EFE00: the effect entity spawns have a translation
 * (em_effect_original) but no live effect owner or handlers (census L26 /
 * L27); every player-side spawn goes to the one counted gap the footstep's
 * surface effect already uses (em_player.c player_effect_gap): nothing is
 * spawned and the call is reported and counted. */
static int w_effect(void *c, uint32_t id, const float pos[3], const float rot[3])
{
    (void)c;
    return player_effect_gap(id, pos, rot);
}
static int w_effect4(void *c, uint32_t id, const float pos[4], const float rot[4])
{
    (void)c;
    return player_effect_gap(id, pos, rot);
}
static int w_effect_bits(void *c, uint32_t id, const uint32_t point[4], const uint32_t at[4])
{
    (void)c;
    float p[3], r[3];
    for (int i = 0; i < 3; ++i) { p[i] = bfloat(point[i]); r[i] = bfloat(at[i]); }
    return player_effect_gap(id, p, r);
}
static int w_attach(void *c, EmPlayerLiveActor *a, uint32_t id, uint32_t *handle)
{
    (void)c;
    if (handle) *handle = 0;
    const float at[3] = { em_live_f32(a, 0xB0), em_live_f32(a, 0xB4), em_live_f32(a, 0xB8) };
    const float rot[3] = { em_live_f32(a, 0xC0), em_live_f32(a, 0xC4), em_live_f32(a, 0xC8) };
    return player_effect_gap(id, at, rot);
}
static int w_attach_plain(void *c, uint32_t id, EmPlayerLiveActor *a)
{
    uint32_t handle;
    return w_attach(c, a, id, &handle);
}

/* 001AEDE0 / 001AEE10: the translated fades (as the scene coordinator binds
 * them, em_scene_bindings.c w_001AEDE0 / w_001AEE10). */
static int w_fade(void *c, int a0, int a1)
{
    (void)c;
    em_frame_fade_start_colour(1, a0, (uint8_t)a1);
    return 0;
}
static int w_fade_end(void *c, int a0, int a1)
{
    (void)c;
    em_frame_fade_start_colour(-1, a0, (uint8_t)a1);
    return 0;
}

/* 00122BB8: the one shared LCG (em_player_misc_random over em_random). */
static int w_random(void *c, uint32_t *value) { (void)c; return em_player_misc_random(NULL, value); }
/* 00179B90: rand() & 7, less 5 when 5 or more (the footstep's worker). */
static int w_random5(void *c, int *value)
{
    (void)c;
    *value = (int)footstep_rand5();
    return 0;
}

/* 001B61C0(big, small, duration, force): the pad vibration request over the
 * pad block D_00810E40 (em_pad_actuator, since census L23; heavy landings,
 * hits, ladder rungs). */
static int w_rumble(void *c, int a, int b, int d, int e)
{
    (void)c;
    return em_pad_actuator_001B61C0((uint8_t)a, (uint8_t)b, d, e);
}

/* SDK leaves (em_sdk_math_original over the world's context; a fault is
 * recorded in L.sdk->fault and fails the slot). */
static float w_sin(void *c, float x) { (void)c; return em_sdk_math_original_float_0011E2A8(L.sdk, x); }
static float w_cos(void *c, float x) { (void)c; return em_sdk_math_original_float_0011DE90(L.sdk, x); }
static float w_atan2(void *c, float y, float x) { (void)c; return em_sdk_math_original_float_0011E620(L.sdk, y, x); }
static float w_sqrt(void *c, float x) { (void)c; return em_sdk_math_original_float_0011E748(L.sdk, x); }
static int w_sin_r(void *c, float x, float *r) { (void)c; *r = w_sin(NULL, x); return L.sdk->fault ? -1 : 0; }
static int w_cos_r(void *c, float x, float *r) { (void)c; *r = w_cos(NULL, x); return L.sdk->fault ? -1 : 0; }
static int w_atan2_r(void *c, float y, float x, float *r) { (void)c; *r = w_atan2(NULL, y, x); return L.sdk->fault ? -1 : 0; }
static int w_sqrt_r(void *c, float x, float *r) { (void)c; *r = w_sqrt(NULL, x); return L.sdk->fault ? -1 : 0; }
static int w_atan2_bits(void *c, uint32_t y, uint32_t x, uint32_t *out)
{
    (void)c;
    *out = fbits(w_atan2(NULL, bfloat(y), bfloat(x)));
    return L.sdk->fault ? -1 : 0;
}
static int w_cos_bits(void *c, uint32_t x, uint32_t *out)
{
    (void)c;
    *out = fbits(w_cos(NULL, bfloat(x)));
    return L.sdk->fault ? -1 : 0;
}
static int w_sqrt_bits(void *c, uint32_t x, uint32_t *out)
{
    (void)c;
    *out = fbits(w_sqrt(NULL, bfloat(x)));
    return L.sdk->fault ? -1 : 0;
}
static int w_fabs_bits(void *c, uint32_t x, uint32_t *out)
{
    (void)c;
    *out = fbits(em_sdk_math_original_0011DF78(bfloat(x)));
    return 0;
}

/* 001B1470 (em_player_recovery_wrap: bounded, -1 where the original's loop
 * cannot end), 001B12B0 (em_script_host_approach), float_to_int. */
static int w_wrap_bits(void *c, uint32_t x, uint32_t *out) { (void)c; return em_player_recovery_wrap(x, out); }
static float w_wrap_float(void *c, float x)
{
    (void)c;
    uint32_t out;
    if (em_player_recovery_wrap(fbits(x), &out) < 0) {
        L.sdk->fault = 0x001B1470u;
        return 0.0f;
    }
    return bfloat(out);
}
static int w_wrap_r(void *c, float x, float *r)
{
    (void)c;
    uint32_t out;
    FAULT(em_player_recovery_wrap(fbits(x), &out));
    *r = bfloat(out);
    return 0;
}
static int w_approach_bits(void *c, uint32_t t, uint32_t cur, uint32_t rate, uint32_t *out)
{
    (void)c;
    return em_script_host_approach(NULL, t, cur, rate, out);
}
static float w_approach_float(void *c, float t, float cur, float rate)
{
    (void)c;
    uint32_t out;
    if (em_script_host_approach(NULL, fbits(t), fbits(cur), fbits(rate), &out) < 0) {
        L.sdk->fault = 0x001B12B0u;
        return 0.0f;
    }
    return bfloat(out);
}
static int w_to_int_bits(void *c, uint32_t x, int32_t *out)
{
    (void)c;
    *out = em_player_float_to_int(x);
    return 0;
}
static int32_t w_to_int_float(void *c, float x) { (void)c; return em_player_float_to_int(fbits(x)); }

/* VU0 leaves: build_trs_matrix, 001026A0, 001028B8 and the matrix
 * builders (em_owner_services_original / em_effect_original). */
static int w_trs_bits(void *c, uint32_t out[16], const uint32_t pos[3], const uint32_t rot[3],
                      const uint32_t scale[3])
{
    (void)c;
    return em_pose_host_build_trs_matrix(L.pose, out, pos, rot, scale);
}
static int w_apply_bits(void *c, const uint32_t m[16], const uint32_t v[4], uint32_t out[4])
{
    (void)c;
    return em_player_helper_apply(m, v, out);
}
static int w_apply_bits_out_first(void *c, uint32_t out[4], const uint32_t m[16], const uint32_t v[4])
{
    (void)c;
    return em_player_helper_apply(m, v, out);
}
static int w_transform_float(void *c, float out[4], const float m[16], const float v[4])
{
    (void)c;
    em_effect_original_001026A0(out, m, v);
    return 0;
}
static int w_vadd_float(void *c, float out[4], const float a[4], const float b[4])
{
    return em_player_hang_vadd(c, out, a, b);
}
static int w_vadd_bits(void *c, const uint32_t a[4], const uint32_t b[4], uint32_t out[4])
{
    float fa[4], fb[4], fo[4];
    memcpy(fa, a, sizeof fa); memcpy(fb, b, sizeof fb);
    FAULT(em_player_hang_vadd(c, fo, fa, fb));
    memcpy(out, fo, sizeof fo);
    return 0;
}
static int w_vadd_bits_out_first(void *c, uint32_t out[4], const uint32_t a[4], const uint32_t b[4])
{
    return w_vadd_bits(c, a, b, out);
}
static int w_identity_bits(void *c, uint32_t m[16])
{
    (void)c;
    float f[16];
    FAULT(em_owner_services_identity_001029C0(f));
    memcpy(m, f, sizeof f);
    return 0;
}
static int w_rotate_y_bits(void *c, uint32_t out[16], const uint32_t in[16], uint32_t angle)
{
    (void)c;
    float o[16], i[16];
    memcpy(i, in, sizeof i);
    FAULT(em_owner_services_rotate_y_00102BB0(o, i, angle));
    memcpy(out, o, sizeof o);
    return 0;
}
static int w_rotate_x_bits(void *c, uint32_t out[16], const uint32_t in[16], uint32_t angle)
{
    (void)c;
    float o[16], i[16];
    memcpy(i, in, sizeof i);
    FAULT(em_owner_services_rotate_x_00102B08(o, i, angle));
    memcpy(out, o, sizeof o);
    return 0;
}
static int w_translate_m_bits(void *c, uint32_t out[16], const uint32_t in[16], const uint32_t v[4])
{
    (void)c;
    float o[16], i[16], f[3];
    memcpy(i, in, sizeof i);
    memcpy(f, v, sizeof f);
    FAULT(em_owner_services_translate_00102918(o, i, f));
    memcpy(out, o, sizeof o);
    return 0;
}
static int w_euler_bits(void *c, uint32_t out[16], const uint32_t in[16], const uint32_t angles[4])
{
    return em_pose_host_euler(c, out, in, angles);
}

/* ---- Collision (the world's original walkers) ------------------------------
 * 0019AD00 / 0019AFE0 run over the world's move state; 0019A570 over its
 * segment state; 0019AB20 / 001760C0 / 0019BC40 over the actor-collision
 * world. The hit readers read what the last query left. */

static EmCollMovePlayer *move_player(void) { return em_collision_world_move_player(); }

static int w_move_hit(void *c, EmPlayerLiveActor *a, const float target[4], unsigned mask,
                      EmPlayerProbeHit *hit)
{
    (void)c;
    const float pos[3] = { em_live_f32(a, 0xB0), em_live_f32(a, 0xB4), em_live_f32(a, 0xB8) };
    L.last_hit = 1;
    return em_coll_move_player_move(move_player(), pos, target, mask, hit);
}
static int w_sweep_hit(void *c, EmPlayerLiveActor *a, const float from[4], const float to[4],
                       unsigned mask, EmPlayerProbeHit *hit)
{
    (void)c; (void)a;
    L.last_hit = 1;
    return em_coll_move_player_sweep(move_player(), from, to, mask, hit);
}
/* The mask with bit 31 (0x80000000): 0019AD00 then adds 0x700031C0/C8 to the
 * query actor's +B0 x / z; 0019AFE0 passes the bit to the walkers only. */
static int w_move_mask(EmPlayerLiveActor *a, const float target[4], unsigned mask, int *result,
                       EmPlayerProbeHit *hit)
{
    L.last_hit = 1;
    if (!(mask & 0x80000000u)) {
        EmPlayerProbeHit local;
        int kind = w_move_hit(NULL, a, target, mask, hit ? hit : &local);
        if (kind < 0) return -1;
        *result = kind;
        return 0;
    }
    float pos[3] = { em_live_f32(a, 0xB0), em_live_f32(a, 0xB4), em_live_f32(a, 0xB8) };
    int kind = em_coll_move_slide_move(move_player(), pos, target, mask);
    if (kind < 0) return -1;
    em_live_set_f32(a, 0xB0, pos[0]);
    em_live_set_f32(a, 0xB8, pos[2]);
    if (hit) {
        memset(hit, 0, sizeof *hit);
        hit->kind = kind;
        if (kind) return unbound("0019AD00 with bit 31: the hit record after the x/z response");
    }
    *result = kind;
    return 0;
}
static int w_sweep_mask(EmPlayerLiveActor *a, const float from[4], const float to[4], unsigned mask,
                        int *result, EmPlayerProbeHit *hit)
{
    EmPlayerProbeHit local;
    if (mask & 0x80000000u) return unbound("0019AFE0 with bit 31");
    int kind = w_sweep_hit(NULL, a, from, to, mask, hit ? hit : &local);
    if (kind < 0) return -1;
    *result = kind;
    return 0;
}
static void bits4(const uint32_t in[4], float out[4]) { memcpy(out, in, 4 * sizeof *out); }

/* 0019A570(from, to, mask, id). */
static int w_segment_r(const float from[4], const float to[4], unsigned mask, int id, int *result)
{
    L.last_hit = 2;
    int r = em_coll_segment_0019A570(em_collision_world_segment(), from, to, mask, id);
    if (r < 0) return -1;
    *result = r;
    return 0;
}
static int segment_hit(EmPlayerProbeHit *hit)
{
    EmCollSegmentHit h;
    memset(hit, 0, sizeof *hit);
    if (em_coll_segment_hit(em_collision_world_segment(), &h) < 0) return -1;
    hit->kind = h.kind;
    hit->node = h.record_node;
    memcpy(hit->point, h.point, sizeof hit->point);
    memcpy(hit->normal, h.record_normal, sizeof hit->normal);
    hit->owner = h.entity;
    hit->entity = h.entity != NULL;
    hit->entity_flags = h.entity_flags;
    hit->entity_type = h.entity_type;
    return 0;
}

/* The hit the last query left: *(0x700031D0) + 0x1A (the node halfword), the
 * point 0x700031B0 and the record's normal +24. */
static int last_hit(EmPlayerProbeHit *hit)
{
    memset(hit, 0, sizeof *hit);
    if (L.last_hit == 2) return segment_hit(hit);
    if (L.last_hit != 1) return -1;
    const EmCollMoveScratch *s = em_collision_world_move_scratch();
    if (!s || !s->record) return -1;
    hit->kind = s->mode;
    hit->node = s->record_node;
    memcpy(hit->point, s->point, sizeof hit->point);
    memcpy(hit->normal, s->record_normal, sizeof hit->normal);
    hit->owner = s->entity;
    hit->entity = s->entity != NULL;
    return 0;
}

/* 0019AB20(p, at, probe, mask) over the player's query view. Bit 31 moves
 * the record's +B4. */
static int w_ground_mask(EmPlayerLiveActor *a, const float at[3], const float probe[3],
                         unsigned mask, int *result, EmPlayerProbeHit *hit)
{
    EmActorCollisionPlayer *p = em_collision_world_player();
    EmPlayerProbeHit local;
    if (!p) return -1;
    float feet = em_live_f32(a, 0xB4);
    float *saved = p->query.feet_y;
    p->query.feet_y = &feet;
    int kind = em_actor_collision_player_ground(p, at, probe, mask, hit ? hit : &local);
    p->query.feet_y = saved;
    if (kind < 0) return -1;
    if (mask & 0x80000000u) em_live_set_f32(a, 0xB4, feet);
    *result = kind;
    return 0;
}
/* 001760C0(p, at, arg, height). */
static int w_column_hit(EmPlayerLiveActor *a, const float at[3], int arg, float height, int *result,
                        EmPlayerProbeHit *hit)
{
    EmActorCollisionPlayer *p = em_collision_world_player();
    EmPlayerProbeHit local;
    if (!p) return -1;
    float feet = em_live_f32(a, 0xB4);
    int kind = em_actor_collision_player_001760C0(p, &feet, at, arg, height, hit ? hit : &local);
    if (kind < 0) return -1;
    if (arg == 0) em_live_set_f32(a, 0xB4, feet);
    *result = kind;
    return 0;
}
/* 0019BC40(at) as the EmPlayerClimbTable the climb / recovery / running jump
 * read (count 0x700031E0, D_70003170, D_700030F0, D_00282250, and each
 * entry's object +54 byte / +1A halfword). More than 16 survivors faults
 * (the original's arrays alias past 16). */
static int w_table(void *c, const float at[4], EmPlayerClimbTable *t)
{
    (void)c;
    EmCollColumn col;
    const float pos[3] = { at[0], at[1], at[2] };
    int n = em_actor_collision_column_0019BC40(em_collision_world_cells(), pos,
                                                em_collision_world_column_math(), &col);
    if (n < 0 || col.count > EM_PLAYER_CLIMB_TABLE_MAX) return -1;
    memset(t, 0, sizeof *t);
    t->count = col.count;
    for (int i = 0; i < col.count; ++i) {
        t->flags[i] = col.flags[i];
        t->height[i] = col.height[i];
        t->aux[i] = col.aux[i];
        t->object_kind[i] = col.object_kind[i];
        t->object_node[i] = col.object_node[i];
    }
    return L.sdk->fault ? -1 : 0;
}
/* 00179450(p, point) through the floor module (0019BC40, then the query). */
static int w_floor_query(void *c, EmPlayerLiveActor *a, const uint32_t point[3], int *result)
{
    (void)c;
    EmPlayerFloorTable table;
    const float pos[3] = { bfloat(point[0]), bfloat(point[1]), bfloat(point[2]) };
    FAULT(em_actor_collision_player_column(em_collision_world_column_player(), pos, &table));
    EmPlayerFallActor f;
    em_player_fall_actor_from_live(a, &f);
    f.position[1] = pos[1];
    *result = em_player_floor_query(&f, &table);
    em_live_set_f32(a, 0x258, f.below);
    return L.sdk->fault ? -1 : 0;
}

/* ---- Fail-stop workers (untranslated callees, off the route) -------------- */

#define STUB_ACTOR(name, addr) \
    static int name(void *c, EmPlayerLiveActor *a) { (void)c; (void)a; return unbound(addr); }
#define STUB_ACTOR_R(name, addr) \
    static int name(void *c, EmPlayerLiveActor *a, int *r) { (void)c; (void)a; if (r) *r = 0; return unbound(addr); }

STUB_ACTOR_R(x_001782A0, "001782A0 (ledge top grab)")
STUB_ACTOR_R(x_00178390, "00178390 (ledge grab check, surface 0x3D)")
STUB_ACTOR_R(x_00178080, "00178080 (ladder hit reaction)")
STUB_ACTOR_R(x_00188570, "00188570 (row clip)")
STUB_ACTOR_R(x_00188590, "00188590 (row clip)")
STUB_ACTOR_R(x_001885B0, "001885B0 (row clip)")
STUB_ACTOR_R(x_00188610, "00188610 (row clip)")
STUB_ACTOR(x_0021C200, "0021C200 (reaction)")
STUB_ACTOR(x_00182AF0, "00182AF0 (sound base + 0x100)")
STUB_ACTOR(x_0015C1F0, "0015C1F0 (player model kind select: 001CA6E0 / 00200890 are not bound)")
static int x_stream_check(void *c) { (void)c; return unbound("001FAFD0 (0021C190's stream test)"); }
static int x_camera_1B0460(void *c, int a0) { (void)c; (void)a0; return unbound("001B0460 (camera re-init)"); }
static int x_alloc(void *c, int cls, uint8_t **node)
{
    (void)c; (void)cls;
    if (node) *node = NULL;
    return unbound("001AFA90 from 0016BAE0 (the crawl spawn)");
}
/* 00182430(p, tier) over the record: em_player_step_sounds (the footstep's
 * one translation) with +23A / +23C, 00179B90 and 001FBD50(p, id, 0, 300)
 * at the record (w_sound_300). The tier argument is compared as its low
 * byte, as the original does. */
static int x_step_random5(void *c, unsigned *value)
{
    (void)c;
    *value = footstep_rand5();
    return 0;
}
static int x_step_sound(void *c, unsigned id)
{
    return w_sound_300(NULL, (EmPlayerLiveActor *)c, (int)id);
}
static int x_surface_sound(void *c, EmPlayerLiveActor *a, int gait)
{
    (void)c;
    if (!a) return -1;
    EmPlayerStepActor step;
    memset(&step, 0, sizeof step);
    step.surface = em_live_u8(a, 0x23A);
    step.depth = em_live_u8(a, 0x23C);
    EmPlayerStepWorkers w;
    memset(&w, 0, sizeof w);
    w.context = a;
    w.random5 = x_step_random5;
    w.sound = x_step_sound;
    return em_player_step_sounds(&step, (uint8_t)gait, &w);
}
/* 00187EE0(p, p + B0, p + D0): em_player_floor.c's translation over the
 * record's fields, the foot being +B0. Its 001EFD90 spawns go to the one
 * counted effect gap (w_effect); the 001F0460 decal (surface 0 with the
 * wet-feet timer +212 set) has no live binding. */
static int x_decal(void *c, const float position[3], float yaw, float pitch)
{
    (void)c; (void)position; (void)yaw; (void)pitch;
    return unbound("001F0460 (the wet-feet decal of 00187EE0)");
}
static int x_step_effect(void *c, uint32_t id, const float position[3], const float rotation[3])
{
    return w_effect(c, id, position, rotation);
}
static int x_place(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    if (!a) return -1;
    EmPlayerStepActor step;
    memset(&step, 0, sizeof step);
    for (unsigned k = 0; k < 3; ++k) {
        step.position[k] = em_live_f32(a, 0xB0 + 4 * k);
        step.rotation[k] = em_live_f32(a, 0xC0 + 4 * k);
    }
    step.slope = em_live_f32(a, 0x9C);
    step.surface_y = em_live_f32(a, 0x250);
    step.wet = (int16_t)em_live_u16(a, 0x212);
    step.surface = em_live_u8(a, 0x23A);
    step.depth = em_live_u8(a, 0x23C);
    EmPlayerStepWorkers w;
    memset(&w, 0, sizeof w);
    w.context = a;
    w.effect = x_step_effect;
    w.decal = x_decal;
    return em_player_ground_effect_00187EE0(&step, step.position, &w);
}
/* 001FB9F0(id, 0x1000, 0x1000, 0x1000): the non-positional submit, whose
 * live binding is em_sfx_play (em_sfx.c, census row 001FB9F0; the same
 * binding the status page's and the reversal's cues use). Other request
 * words have no live binding. An id the exported AREA11 registry lacks is
 * silent (WP-14) and reported once. */
static int x_sound_1FB9F0(void *c, int a0, int a1, int a2, int a3)
{
    (void)c;
    if (a0 < 0 || a1 != 0x1000 || a2 != 0x1000 || a3 != 0x1000)
        return unbound("001FB9F0 with request words other than 0x1000 (no live binding)");
    static uint64_t reported[8];
    if (a0 < 512 && em_sfx_cue_state((unsigned)a0) == 0 && !(reported[a0 >> 6] & (1ull << (a0 & 63)))) {
        reported[a0 >> 6] |= 1ull << (a0 & 63);
        fprintf(stderr, "player closure: sound 0x%X (001FB9F0) is not in the exported sfx registry "
                "(WP-14); silent\n", (unsigned)a0);
    }
    em_sfx_play((unsigned)a0);
    return 0;
}
static int x_area_point(void *c, int area, int sub, unsigned offset, uint32_t out[3])
{
    (void)c; (void)area; (void)sub; (void)offset;
    memset(out, 0, 3 * sizeof *out);
    return unbound("D_0024D650's area record words (00179910, area 2)");
}
/* 001809B0 through the ladder climb's translation. */
static int w_ledge_move(void *c, EmPlayerLiveActor *a, int side, int *result)
{
    (void)c; IN3A20();
    int r = em_player_ladder_climb_001809B0(&L.lc, a, side, result);
    OUT3A20(); return r;
}
static int w_clip_FC80(void *c, EmPlayerLiveActor *a, float blend)
{
    (void)c;
    return em_player_ladder_climb_0017FC80(&L.lc, a, blend);
}

/* The misc lane's routines (context = the misc host); they keep 0x70003A20
 * by value (EmPlayerMiscScratch.s3A20, a float's bits). */
#define MISC_IN()  do { IN3A20(); memcpy(&L.misc_scratch.s3A20, &L.land.s3A20, 4); } while (0)
#define MISC_OUT() do { memcpy(&L.land.s3A20, &L.misc_scratch.s3A20, 4); OUT3A20(); } while (0)
static int w_aim_track(void *c, EmPlayerLiveActor *a)
{ (void)c; MISC_IN(); int r = em_player_misc_w_aim_track(&L.misc, a); MISC_OUT(); return r; }
static int w_ledge_ahead_self(void *c, EmPlayerLiveActor *a, int *r)
{ (void)c; MISC_IN(); int x = em_player_misc_w_ledge_ahead_self(&L.misc, a, r); MISC_OUT(); return x; }
static int w_ledge_ahead(void *c, EmPlayerLiveActor *a, const float v[4], int *r)
{ (void)c; MISC_IN(); int x = em_player_misc_w_ledge_ahead(&L.misc, a, v, r); MISC_OUT(); return x; }
static int w_ledge_above(void *c, EmPlayerLiveActor *a, int *r)
{ (void)c; MISC_IN(); int x = em_player_misc_w_ledge_above(&L.misc, a, r); MISC_OUT(); return x; }
static int w_ledge_side(void *c, EmPlayerLiveActor *a, int side, int *r)
{ (void)c; MISC_IN(); int x = em_player_misc_w_ledge_side(&L.misc, a, side, r); MISC_OUT(); return x; }
#define MISC_CLIP(name, fn) \
    static int name(void *c, EmPlayerLiveActor *a, int side, float blend) \
    { (void)c; MISC_IN(); int x = fn(&L.misc, a, side, blend); MISC_OUT(); return x; }
MISC_CLIP(w_clip_DF70, em_player_misc_w_clip_DF70)
MISC_CLIP(w_clip_DFB0, em_player_misc_w_clip_DFB0)
MISC_CLIP(w_clip_E0D0, em_player_misc_w_clip_E0D0)
MISC_CLIP(w_clip_E150, em_player_misc_w_clip_E150)
MISC_CLIP(w_clip_E1D0, em_player_misc_w_clip_E1D0)
static int w_clip_FF80(void *c, EmPlayerLiveActor *a, float blend)
{ (void)c; MISC_IN(); int x = em_player_misc_w_clip_FF80(&L.misc, a, blend); MISC_OUT(); return x; }
static int w_depth(void *c, EmPlayerLiveActor *a, const EmPlayerRecoveryLedge *ledge, float y, int *r)
{ (void)c; MISC_IN(); int x = em_player_misc_w_depth(&L.misc, a, ledge, y, r); MISC_OUT(); return x; }

/* The record-level helpers (0x70003A20 / 0x700038A0 are L.land's). */
static int w_001755B0(void *c, EmPlayerLiveActor *a, int *r)
{ (void)c; IN3A20(); int x = em_player_record_001755B0(&L.helpers, a, r); OUT3A20(); return x; }
static int w_0017F320(void *c, EmPlayerLiveActor *a, int *r)
{ (void)c; IN3A20(); int x = em_player_record_0017F320(&L.helpers, a, r); OUT3A20(); return x; }
static int w_00188550(void *c, EmPlayerLiveActor *a, int *clip)
{ (void)c; return em_player_record_00188550(&L.helpers, a, clip); }
static int w_00174FD0(void *c, EmPlayerLiveActor *a)
{ (void)c; IN3A20(); int x = em_player_record_00174FD0(&L.helpers, a); OUT3A20(); return x; }
static int w_00177510(void *c, const EmPlayerProbeHit *hit, EmPlayerRecoveryLedge *ledge)
{ (void)c; return em_player_record_00177510(&L.helpers, hit, ledge); }
static int w_001775E0(void *c, EmPlayerLiveActor *a, const EmPlayerRecoveryLedge *l, int wide, float y, int *r)
{ (void)c; return em_player_record_001775E0(&L.helpers, a, l, wide, y, r); }
static int w_001776E0(void *c, EmPlayerLiveActor *a, const EmPlayerRecoveryLedge *l, float y, int *r)
{ (void)c; return em_player_record_001776E0(&L.helpers, a, l, y, r); }
static int w_00177CF0(void *c, EmPlayerLiveActor *a, const EmPlayerRecoveryLedge *l, float y, int *r)
{ (void)c; return em_player_record_00177CF0(&L.helpers, a, l, y, r); }
static int w_0019A180(void *c, const EmPlayerClimbTable *t, int i, int *attr)
{ (void)c; return em_player_record_0019A180(&L.helpers, t, i, attr); }

/* 00128350 / 001000E0 (the soft-float double and its compare). */
static int w_00128350(void *c, uint32_t value, uint64_t *result)
{ (void)c; *result = em_sdk_soft_float_00128350(value); return 0; }
static int w_001000E0(void *c, uint64_t a, uint64_t b, int *result)
{ (void)c; *result = (int)em_rvr_001000E0(a, b); return 0; }
/* D_008106F1 (0017C580 reads it after 00174AC0). */
static int w_progress_8106F1(void *c, uint8_t *value) { (void)c; return req_byte(0x008106F1u, value); }
/* The node *(D_00275B40 + 4) +C0 / +C8, read after anim_eval_skeleton. */
static int w_hip(void *c, uint32_t *x, uint32_t *z) { (void)c; return em_pose_view_hip_xz(&L.view, x, z); }

/* ---- The scene words ------------------------------------------------------ */

/* The pad block after 001B5940 (D_00810E57 gait, E64 / E65 stick, E70 held,
 * E74 pressed) and D_008106A0, copied at each refresh so the modules that
 * hold pointers read this frame's value. D_008106A0 is the camera commit
 * 0018C0D0's heading atan2(-fwd.z, fwd.x), read from the live camera's
 * canonical word (em_camera_live.c, census L13). */
static struct {
    uint8_t gait, lx, ly;
    uint32_t d8106A0;
    uint16_t held, pressed;
    uint8_t spad3B8D;
} P;

static void refresh_pad(void)
{
    const EmPadUnpack *pad = em_frame_pad_block();
    P.gait = pad->gait;
    P.lx = pad->lx;
    P.ly = pad->ly;
    P.held = scene()->d810E70;
    P.pressed = scene()->d810E74;
    P.spad3B8D = scene()->spad3B8D;
    const uint8_t *heading = em_camera_live_bytes(0x008106A0u, 4);
    if (heading) memcpy(&P.d8106A0, heading, 4);
}

static int recovery_scene(void *c, EmPlayerRecoveryScene *s)
{
    (void)c;
    refresh_pad();
    s->camera_yaw = bfloat(P.d8106A0);
    s->d8106F1 = em_scene_req_at(scene(), 0x008106F1u);
    s->spad3B8D = P.spad3B8D;
    s->pad_gait = P.gait;
    s->pad_x = P.lx;
    s->pad_y = P.ly;
    s->area = scene()->d810700;
    return s->d8106F1 ? 0 : -1;
}

static int hang_scene(void *c, EmPlayerHangScene *s)
{
    (void)c;
    s->area = scene()->d810700;
    s->scripted = scene()->spad3B8D;
    s->pad = scene()->d810E74;
    s->use_mask = L.pad_config[1];
    return 0;
}

static int climb_scene(void *c, EmPlayerClimbScene *s)
{
    (void)c;
    uint32_t hip[4];
    FAULT(em_pose_view_node1_words(&L.view, hip));
    for (int i = 0; i < 4; ++i) s->hip_world[i] = bfloat(hip[i]);
    FAULT(req_byte(0x008106BEu, &s->flags));
    s->area = scene()->d810700;
    return 0;
}

static int slide_scene(void *c, EmPlayerSlideScene *s)
{
    (void)c;
    uint32_t root8, hx, hz;
    FAULT(em_pose_view_root_node(&L.view, 8, &root8));
    FAULT(em_pose_view_hip_xz(&L.view, &hx, &hz));
    refresh_pad();
    s->root_forward = bfloat(root8);
    s->hip[0] = bfloat(hx);
    s->hip[1] = bfloat(hz);
    s->scripted = P.spad3B8D;
    s->pad_gait = P.gait;
    s->pad_x = P.lx;
    s->pad_y = P.ly;
    return 0;
}

static int running_jump_scene(void *c, EmPlayerRunningJumpScene *s)
{
    (void)c;
    s->area = scene()->d810700;
    s->subarea = scene()->d810701;
    s->spad3B8D = scene()->spad3B8D;
    const EmActorClassLists *lists = em_collision_world_lists();
    if (!lists) return -1;
    s->target_count = lists->list[EM_ACTOR_LIST_CLASS2].published;   /* D_00275B94 */
    return 0;
}

/* 0021C* read / write views (EmPlayerReactionScene). */
static int node_c0(int node, float out[4])
{
    for (int i = 0; i < 4; ++i) FAULT(em_pose_view_node_float(&L.view, node, 0xC0 + 4u * (unsigned)i, &out[i]));
    return 0;
}
static int reaction_refresh(void *c, EmPlayerReactionScene *s)
{
    (void)c;
    FAULT(em_pose_view_node_float(&L.view, 0, 4, &s->root4));
    FAULT(em_pose_view_node_float(&L.view, 0, 8, &s->root8));
    FAULT(node_c0(2, s->node2));
    FAULT(node_c0(3, s->node3));
    FAULT(node_c0(7, s->node7));
    s->pad_held = scene()->d810E70;
    s->pad_pressed = scene()->d810E74;
    s->spad3B76 = L.pad_config[1];
    s->spad3B7C = L.pad_config[4];
    s->spad3B7E = L.pad_config[5];
    s->scripted = scene()->spad3B8D;
    FAULT(progress_byte(0x0081083Cu, &s->d81083C));
    s->d8106F1 = em_scene_req_at(scene(), 0x008106F1u);
    FAULT(req_byte(0x008106F0u, &s->d8106F0));
    FAULT(req_byte(0x008106BCu, &s->d8106BC));
    s->d275B08 = L.d275B08;
    return s->d8106F1 ? 0 : -1;
}
/* The written fields back to their one storage. */
static int reaction_post(void)
{
    uint8_t *f0 = em_scene_req_at(scene(), 0x008106F0u), *bc = em_scene_req_at(scene(), 0x008106BCu);
    if (!f0 || !bc) return -1;
    *f0 = L.reaction_scene.d8106F0;
    *bc = L.reaction_scene.d8106BC;
    L.d275B08 = L.reaction_scene.d275B08;
    return 0;
}
static int reaction_pre(void) { return reaction_refresh(NULL, &L.reaction_scene); }

/* D_00275B00 + 8 / D_00810702 stores (0016B790, 0016B8A0, 0016D130). */
static int w_set_275B08(void *c, int32_t value) { (void)c; L.d275B08 = value; return 0; }
static int w_set_810702(void *c, uint8_t value) { (void)c; scene()->d810702 = value; return 0; }

static int closure_scene(void *c, EmPlayerClosureScene *s)
{
    (void)c;
    s->pad = scene()->d810E74;
    s->use_mask = L.pad_config[1];
    s->area = scene()->d810700;
    s->fade = (int16_t)em_frame_transition()->substate;   /* D_0028A9A0 */
    s->d275B14 = L.d275B14;
    s->d275B0C = L.d275B0C;
    s->d275B10 = L.d275B10;
    s->d281B64 = L.d281B64;
    return 0;
}
/* 00181D70 (through the closures) stores D_00275B14 there too. */
static int closure_post(void) { return 0; }

static int c10_pre(void)
{
    EmPlayerClosure1019Scene *s = &L.c10_scene;
    EmSceneState *st = scene();
    refresh_pad();
    s->pad_held = st->d810E70;
    s->pad_pressed = st->d810E74;
    s->use_mask = L.pad_config[1];
    s->mask_3B7C = L.pad_config[4];
    s->mask_3B7E = L.pad_config[5];
    s->fade = (int16_t)em_frame_transition()->substate;
    s->area = st->d810700;
    s->sub_area = st->d810701;
    s->zone = st->d810702;
    FAULT(req_byte(0x008106BEu, &s->d8106BE));
    s->pad_gait = P.gait;
    s->pad_x = P.lx;
    s->pad_y = P.ly;
    const uint8_t *a2 = em_scene_d810730_at(st, 2);
    if (!a2) return -1;
    s->area_flags2 = *a2;
    FAULT(req_byte(0x008106B5u, &s->d8106B5));
    FAULT(req_byte(0x008106B6u, &s->d8106B6));
    FAULT(req_byte(0x008106B7u, &s->d8106B7));
    FAULT(req_byte(0x008106B8u, &s->d8106B8));
    s->d8106C8 = em_scene_req_u32(st, EM_SCENE_REQ_C8);
    s->camera_yaw = P.d8106A0;
    s->d275B10 = L.d275B10;
    s->d275B0C = L.d275B0C;
    s->d281B64 = L.d281B64;
    L.m2_scene.d275B14 = L.d275B14;
    return 0;
}
static int c10_post(void)
{
    EmPlayerClosure1019Scene *s = &L.c10_scene;
    EmSceneState *st = scene();
    st->d810702 = s->zone;
    uint8_t *be = em_scene_req_at(st, 0x008106BEu);
    uint8_t *b5 = em_scene_req_at(st, 0x008106B5u), *b6 = em_scene_req_at(st, 0x008106B6u);
    uint8_t *b7 = em_scene_req_at(st, 0x008106B7u), *b8 = em_scene_req_at(st, 0x008106B8u);
    if (!be || !b5 || !b6 || !b7 || !b8) return -1;
    *be = s->d8106BE; *b5 = s->d8106B5; *b6 = s->d8106B6; *b7 = s->d8106B7; *b8 = s->d8106B8;
    L.d275B10 = s->d275B10;
    L.d275B0C = s->d275B0C;
    L.d281B64 = s->d281B64;
    L.d275B14 = L.m2_scene.d275B14;
    return 0;
}

static int lc_pre(void)
{
    EmPlayerLadderClimbScene *s = &L.lc_scene;
    s->area = scene()->d810700;
    s->area_sub = scene()->d810701;
    FAULT(req_byte(0x008106F2u, &s->d8106F2));
    s->pad = scene()->d810E74;
    s->use_mask = L.pad_config[1];
    return 0;
}
static int lc_post(void)
{
    uint8_t *f2 = em_scene_req_at(scene(), 0x008106F2u);
    if (!f2) return -1;
    *f2 = L.lc_scene.d8106F2;
    return 0;
}

static int m2_pre(void) { L.m2_scene.d275B14 = L.d275B14; return 0; }
static int m2_post(void) { L.d275B14 = L.m2_scene.d275B14; return 0; }

static int wa_pre(void)
{
    EmPlayerWeaponScene *s = &L.wa_scene;
    s->spad3B74 = L.pad_config[0];
    s->spad3B76 = L.pad_config[1];
    s->spad3B78 = L.pad_config[2];
    s->spad3B7C = L.pad_config[4];
    s->spad3B7E = L.pad_config[5];
    s->d810E70 = scene()->d810E70;
    s->d810E74 = scene()->d810E74;
    /* D_00810C61 (the fire mode) is still em_weapon's (reserved in the
     * progress block): the stance routines that read it fault. */
    uint8_t c61;
    if (progress_byte(0x00810C61u, &c61) < 0)
        return unbound("D_00810C61 (the fire mode; em_weapon.c keeps it, census L28)");
    s->d810C61 = c61;
    uint8_t e0[4];
    for (unsigned i = 0; i < 4; ++i) FAULT(req_byte(0x008106E0u + i, &e0[i]));
    s->d8106E0 = (uint32_t)e0[0] | (uint32_t)e0[1] << 8 | (uint32_t)e0[2] << 16 | (uint32_t)e0[3] << 24;
    FAULT(progress_byte(0x00810CA4u, &s->d810CA4));
    return 0;
}
static int wa_post(void)
{
    for (unsigned i = 0; i < 4; ++i) {
        uint8_t *p = em_scene_req_at(scene(), 0x008106E0u + i);
        if (!p) return -1;
        *p = (uint8_t)(L.wa_scene.d8106E0 >> (8 * i));
    }
    return 0;
}

/* D_008106E0 as the weapon B scene's word pointer: the request block's
 * bytes are its one storage, loaded before and stored after the call. */
static uint32_t wb_d8106E0;
static uint8_t wb_d810CA4;
static int wb_pre(void)
{
    uint8_t e0[4];
    for (unsigned i = 0; i < 4; ++i) FAULT(req_byte(0x008106E0u + i, &e0[i]));
    wb_d8106E0 = (uint32_t)e0[0] | (uint32_t)e0[1] << 8 | (uint32_t)e0[2] << 16 | (uint32_t)e0[3] << 24;
    FAULT(progress_byte(0x00810CA4u, &wb_d810CA4));
    L.wb_pad = scene()->d810E74;
    L.wb_3B78 = L.pad_config[2];
    return 0;
}
static int wb_post(void)
{
    for (unsigned i = 0; i < 4; ++i) {
        uint8_t *p = em_scene_req_at(scene(), 0x008106E0u + i);
        if (!p) return -1;
        *p = (uint8_t)(wb_d8106E0 >> (8 * i));
    }
    return 0;
}

/* ---- Per-module adapters whose shapes the shared workers above do not fit - */

/* The climb / slide worker tables share one context for their world-level
 * slots; these reach the world directly. */
static int cw_move(void *c, const float p[3], const float t[4], unsigned m, EmPlayerClimbHit *h)
{
    (void)c;
    L.last_hit = 1;
    return em_coll_move_climb_move(move_player(), p, t, m, h);
}
static int cw_sweep(void *c, const float f[4], const float t[4], unsigned m, EmPlayerClimbHit *h)
{
    (void)c;
    L.last_hit = 1;
    return em_coll_move_climb_sweep(move_player(), f, t, m, h);
}
static int cw_segment(void *c, const float f[4], const float t[4], unsigned m, int id)
{
    (void)c;
    int r;
    FAULT(w_segment_r(f, t, m, id, &r));
    return r;
}
/* 001760C0(p, at, 1, height): nonzero when the column is covered. */
static int cw_column(void *c, const float at[4], float height)
{
    (void)c;
    int r;
    FAULT(w_column_hit(L.actor, at, 1, height, &r, NULL));
    return r;
}
static int climb_skeleton(void *c, EmPlayerLiveActor *a, float *hip_y, float *hip_8)
{
    (void)c; (void)a;
    return em_pose_view_eval_hip(&L.view, hip_y, hip_8);
}
static int climb_link_kind(void *c, const void *owner)
{
    return em_actor_collision_player_link_kind(c, owner);
}

static int sw_move(void *c, float position[3], const float target[4], unsigned mask)
{
    (void)c;
    L.last_hit = 1;
    return em_coll_move_slide_move(move_player(), position, target, mask);
}
static int sw_sweep(void *c, const float f[4], const float t[4], unsigned mask, EmPlayerProbeHit *h)
{
    (void)c;
    L.last_hit = 1;
    return em_coll_move_slide_sweep(move_player(), f, t, mask, h);
}
static int sl_damage(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c; IN3A20();
    int r = em_player_recovery_react_00224B80_worker(&L.recovery, a, result);
    OUT3A20(); return r;
}
static int sl_step_sound(void *c, EmPlayerLiveActor *a, int tier)
{
    return x_surface_sound(c, a, tier);
}

/* Recovery: its world-level slot shapes. */
static int rv_segment(void *c, const float from[4], const float to[4], unsigned mask, int id, int *result)
{
    (void)c;
    return w_segment_r(from, to, mask, id, result);
}
static int rv_skeleton(void *c, EmPlayerLiveActor *a, float node1[4])
{
    (void)c;
    return em_pose_view_eval_node1(&L.view, a, node1);
}
static int rv_column(void *c, EmPlayerLiveActor *a, const float at[4], float height, int *result)
{
    (void)c;
    return w_column_hit(a, at, 1, height, result, NULL);
}

/* Reaction: its slot shapes. */
static int re_skeleton(void *c, EmPlayerLiveActor *a, float node1[3])
{
    (void)c;
    return em_pose_view_eval_node1_xyz(&L.view, a, node1);
}
static int re_model_refresh(void *c, EmPlayerLiveActor *a) { return x_0015C1F0(c, a); }

/* Hang. */
static int hg_column(void *c, EmPlayerLiveActor *a, const float at[4], int arg, float height, int *result)
{
    (void)c;
    return w_column_hit(a, at, arg, height, result, NULL);
}
static int hg_sweep(void *c, EmPlayerLiveActor *a, const float from[4], const float to[4],
                    unsigned mask, int *result)
{
    (void)c;
    return w_sweep_mask(a, from, to, mask, result, NULL);
}
static int le_sound_109(void *c, EmPlayerLiveActor *a);

/* Ladder climb: the hit readers and its collision shapes. */
static int lc_hit_kind(void *c, int *kind)
{
    (void)c;
    EmPlayerProbeHit h;
    FAULT(last_hit(&h));
    *kind = h.node & 0xFF;
    return 0;
}
static int lc_hit_y(void *c, uint32_t *bits)
{
    (void)c;
    EmPlayerProbeHit h;
    if (L.last_hit == 0) return -1;
    if (L.last_hit == 1) {
        const EmCollMoveScratch *s = em_collision_world_move_scratch();
        *bits = fbits(s->point[1]);
        return 0;
    }
    FAULT(segment_hit(&h));
    *bits = fbits(h.point[1]);
    return 0;
}
static int lc_clip_frames(void *c, uint32_t bank, int clip, int *result)
{
    int32_t n;
    FAULT(w_clip_frames(c, bank, clip, &n));
    *result = n;
    return 0;
}
static int lc_probe(void *c, EmPlayerLiveActor *a, float at[4], int kind, int *result)
{
    (void)c;
    uint32_t v[4];
    memcpy(v, at, sizeof v);
    return em_player_ladder_probe_00180300(&L.le, a, v, kind, result);
}
static int lc_column(void *c, EmPlayerLiveActor *a, float at[4], int arg, float height, int *result)
{
    (void)c;
    return w_column_hit(a, at, arg, height, result, NULL);
}
static int lc_wall(void *c, EmPlayerLiveActor *a, float at[4], int mask, int *result)
{
    (void)c;
    const float probe[3] = { em_live_f32(a, 0x280), em_live_f32(a, 0x284), em_live_f32(a, 0x288) };
    L.last_hit = 0;
    return w_ground_mask(a, at, probe, (unsigned)mask, result, NULL);
}
static int lc_sweep(void *c, EmPlayerLiveActor *a, float from[4], float to[4], unsigned mask, int *result)
{
    (void)c;
    return w_sweep_mask(a, from, to, mask, result, NULL);
}
static int lc_sweep_box(void *c, float a[4], float b[4], int mask, int id, int *result)
{
    (void)c;
    return w_segment_r(a, b, (unsigned)mask, id, result);
}
static int le_fill_last(void);
static int lc_hit_probe(void *c, float a[4], float b[4], int *result)
{
    (void)c;
    FAULT(le_fill_last());
    uint32_t aw[3], bw[3];
    memcpy(aw, a, sizeof aw);
    memcpy(bw, b, sizeof bw);
    FAULT(em_player_ladder_00199FA0(&L.le_world, &L.le_scratch, aw, bw, result));
    memcpy(a, aw, sizeof aw);
    memcpy(b, bw, sizeof bw);
    return 0;
}
static int lc_hit_point(void *c, float out[4])
{
    (void)c;
    FAULT(le_fill_last());
    uint32_t w[3];
    int ignored;
    FAULT(em_player_ladder_00199DB0(&L.le_world, &L.le_scratch, w, &ignored));
    memcpy(out, w, sizeof w);
    return 0;
}
static int lc_dash(void *c, EmPlayerLiveActor *a, int arg)
{
    (void)c;
    int ignored;
    IN3A20();
    int r = em_player_ladder_00177030(&L.le, a, arg, &ignored);
    OUT3A20();
    return r;
}
static int lc_camera(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    return em_player_ladder_00176DC0(&L.le, a);
}
static float lc_approach(void *c, float t, float cur, float rate) { return w_approach_float(c, t, cur, rate); }

/* ---- Ladder entry (0015D4C0, 00165B60, ...): the probe state ---------------
 * 0x700031B0..D8 as the ladder translations read it (EmPlayerLadderScratch):
 * filled from the collision world's state after each query. A grid node's
 * +0x34..+0x3F (the ladder / ledge axis the actions read) comes from the
 * EMCL axis section (EM_COLL_PROBE_FLAG_AXIS, verified against captured RAM
 * by the exporter); an EMCL without it, and a cell record (the probe state
 * does not carry the cell record's +0x34..), fault when the surface byte is
 * one of 0015D4C0's action cases. */
static const uint8_t kLadderActions[] = { 0x20, 0x32, 0x33, 0x37, 0x38, 0x3A, 0x3B, 0x3D };

static int le_from_state(const EmCollProbeState *st, int kind)
{
    EmPlayerLadderScratch *s = &L.le_scratch;
    const EmCollProbeGrid *grid = em_collision_world_move()->grid;
    memset(s->record_bytes, 0, sizeof s->record_bytes);
    for (int i = 0; i < 3; ++i) s->s31B0[i] = fbits(st->point[i]);
    s->s31D8 = kind;
    s->entity = st->entity ? (uint32_t)(uintptr_t)st->entity : 0;
    s->entity_0E = st->entity ? st->entity->uid : 0;
    uint8_t attr;
    if (st->record == EM_COLL_PROBE_RECORD_GRID) {
        if (st->node < 0 || (uint32_t)st->node >= grid->count) return -1;
        const EmCollPoly *p = &grid->emcl->polys[grid->first + (uint32_t)st->node];
        memcpy(s->record_bytes, grid->words + 12 * st->node, 0x18);
        s->record_bytes[0x1A] = p->attr;
        s->record_bytes[0x1B] = p->pad;
        memcpy(s->record_bytes + 0x24, p->plane, 16);
        s->record = EM_PLAYER_LADDER_RECORD_OTHER;
        s->record_word = 0x40000000u | (uint32_t)st->node;
        attr = p->attr;
        if (grid->axis) {
            memcpy(s->record_bytes + 0x34, grid->axis + 3 * (size_t)st->node, 12);
            return 0;
        }
    } else if (st->record == EM_COLL_PROBE_RECORD_CELL) {
        memcpy(s->record_bytes + 0x1A, &st->cell_class, 2);
        memcpy(s->record_bytes + 0x24, st->cell_normal, 12);
        s->record = EM_PLAYER_LADDER_RECORD_CELL;
        s->record_word = 0x700030B0u;
        attr = (uint8_t)st->cell_class;
    } else {
        s->record = EM_PLAYER_LADDER_RECORD_NONE;
        s->record_word = 0;
        return 0;
    }
    for (unsigned i = 0; i < sizeof kLadderActions; ++i)
        if (attr == kLadderActions[i])
            return unbound(st->record == EM_COLL_PROBE_RECORD_GRID
                               ? "a ladder / ledge action node: the EMCL has no axis section "
                                 "(node +0x34..+0x3F; re-run export_collision.py --node-class)"
                               : "a ladder / ledge action cell record: its +0x34..+0x3F is not in "
                                 "the probe state (0015D4C0 cases 0x20..0x3D)");
    return 0;
}
static int le_from_move(int kind)
{
    const EmCollMoveScratch *m = em_collision_world_move_scratch();
    EmCollProbeState st;
    memset(&st, 0, sizeof st);
    memcpy(st.point, m->point, sizeof st.point);
    st.entity = m->entity;
    st.node = -1;
    if (!m->record) st.record = EM_COLL_PROBE_RECORD_NONE;
    else if (m->record == EM_COLL_MOVE_CELL_RECORD) {
        st.record = EM_COLL_PROBE_RECORD_CELL;
        st.cell_class = m->cell_class;
        memcpy(st.cell_normal, m->cell_normal, sizeof st.cell_normal);
    } else {
        st.record = EM_COLL_PROBE_RECORD_GRID;
        st.node = em_coll_grid_hull_node_index(em_collision_world_move()->grid, m->record);
        if (st.node < 0) return -1;
    }
    return le_from_state(&st, kind);
}
static int le_fill_last(void)
{
    if (L.last_hit == 1) return le_from_move(em_collision_world_move_scratch()->mode);
    if (L.last_hit == 2) {
        const EmCollSegment *seg = em_collision_world_segment();
        return le_from_state(seg->state, seg->state->kind);
    }
    return -1;
}

static int le_probe_0019BA80(void *c, EmPlayerLiveActor *a, const uint32_t point[4],
                             const uint32_t box[4], int mask, int *result)
{
    (void)c;
    const EmCollSegment *seg = em_collision_world_segment();
    float p[4], b[4];
    bits4(point, p); bits4(box, b);
    int r = em_coll_list_passes_0019BA80(seg->world, seg->state, a, a->bytes[2], p, b,
                                         (unsigned)mask);
    if (r < 0) return -1;
    L.last_hit = 2;
    *result = r;
    return le_from_state(seg->state, r);
}
static int le_move(void *c, EmPlayerLiveActor *a, const uint32_t target[4], int mask, int *result)
{
    (void)c;
    float t[4];
    bits4(target, t);
    FAULT(w_move_mask(a, t, (unsigned)mask, result, NULL));
    return le_from_move(*result);
}
static int le_segment(void *c, const uint32_t from[4], const uint32_t to[4], int mask, int id, int *result)
{
    (void)c;
    float f[4], t[4];
    bits4(from, f); bits4(to, t);
    FAULT(w_segment_r(f, t, (unsigned)mask, id, result));
    const EmCollSegment *seg = em_collision_world_segment();
    return le_from_state(seg->state, *result);
}
static int le_sweep(void *c, EmPlayerLiveActor *a, const uint32_t from[4], const uint32_t to[4], int mask,
                    int *result)
{
    (void)c;
    float f[4], t[4];
    bits4(from, f); bits4(to, t);
    FAULT(w_sweep_mask(a, f, t, (unsigned)mask, result, NULL));
    return le_from_move(*result);
}
/* 0019BC40(at) into 0x700031E0 / 0x700030F0 / 0x70003170. */
static int le_column(void *c, const uint32_t at[4])
{
    (void)c;
    EmCollColumn col;
    const float pos[3] = { bfloat(at[0]), bfloat(at[1]), bfloat(at[2]) };
    int n = em_actor_collision_column_0019BC40(em_collision_world_cells(), pos,
                                                em_collision_world_column_math(), &col);
    if (n < 0 || col.count > EM_PLAYER_LADDER_COLUMN_MAX) return -1;
    L.le_scratch.s31E0 = col.count;
    for (int i = 0; i < col.count; ++i) {
        L.le_scratch.s30F0[i] = fbits(col.height[i]);
        L.le_scratch.s3170[i] = col.flags[i];
    }
    return L.sdk->fault ? -1 : 0;
}
static int le_trs(void *c, uint32_t out[16], const uint32_t pos[4], const uint32_t rot[4],
                  const uint32_t scale[4])
{
    (void)c;
    return em_pose_host_build_trs_matrix(L.pose, out, pos, rot, scale);
}
static int le_dot(void *c, const uint32_t a[4], const uint32_t b[4], uint32_t *out)
{
    (void)c;
    float fa[4], fb[4], r;
    bits4(a, fa); bits4(b, fb);
    FAULT(em_coll_probe_sdk_dot(&r, fa, fb));
    *out = fbits(r);
    return 0;
}
static int le_normalize(void *c, uint32_t out[4], const uint32_t in[4])
{
    (void)c;
    float o[4], i[4];
    bits4(in, i);
    em_effect_original_00102760(o, i);
    memcpy(out, o, sizeof o);
    return 0;
}
static int le_sound_base(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)a;
    return w_random5(c, result);
}
/* 001762E0: its gate passes only in area 2 (em_player_floor.c probe_response
 * translates the gate; its area-2 body is untranslated). */
static int le_wall_001762E0(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c; (void)a;
    *result = 0;
    if (scene()->d810700 == 2) return unbound("001762E0's area-2 body");
    return 0;
}
static int le_node(void *c, uint32_t out[4]) { (void)c; return em_pose_view_node1_words(&L.view, out); }
static int le_sound_109(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    return em_player_ladder_00182A70(&L.le, a);
}
static int le_use_probe(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c;
    return em_player_ladder_00176F90(&L.le, a, result);
}
static int le_midpoint(void *c, uint32_t out[3], int *result)
{
    (void)c;
    FAULT(le_fill_last());
    return em_player_ladder_00199DB0(&L.le_world, &L.le_scratch, out, result);
}
static int le_surface(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c;
    return em_player_ladder_0015D4C0(&L.le, a, result);
}

/* ---- The misc lane's workers ------------------------------------------------ */

static int mw_identity(void *c, float m[16]) { (void)c; return em_owner_services_identity_001029C0(m); }
static int mw_euler(void *c, float out[16], const float in[16], const float angles[4])
{ (void)c; return em_owner_services_euler_00102C58(out, in, angles); }
static int mw_translate(void *c, float out[16], const float in[16], const float v[4])
{ (void)c; return em_owner_services_translate_00102918(out, in, v); }
static int mw_vsub(void *c, float out[4], const float a[4], const float b[4])
{ (void)c; return em_coll_probe_sdk_sub(out, a, b); }
static int mw_normalize(void *c, float out[4], const float in[4])
{ (void)c; em_effect_original_00102760(out, in); return 0; }
static int mw_dot(void *c, const float a[4], const float b[4], float *r)
{ (void)c; return em_coll_probe_sdk_dot(r, a, b); }
static int mw_side(void *c, const float from[4], const float to[4], float yaw, int32_t *result)
{
    (void)c; (void)from; (void)to; (void)yaw;
    if (result) *result = 0;
    return unbound("001B1380 in 001FBF50's pan (the misc lane's 001FBD50 path)");
}
static int mw_submit(void *c, int32_t id, int32_t a1, int32_t a2, int32_t a3, int32_t *result)
{
    (void)c; (void)id; (void)a1; (void)a2; (void)a3;
    if (result) *result = -1;
    return unbound("001FB9F0 (the SFX submit of the misc lane's 001FBD50)");
}
static int mw_sound_base(void *c, EmPlayerLiveActor *a, int32_t *base)
{
    (void)a;
    int v;
    FAULT(w_random5(c, &v));
    *base = v;
    return 0;
}
static int mw_request(void *c, EmPlayerLiveActor *a, int32_t clip, int32_t force, float blend)
{ return w_request(c, a, clip, force, blend); }
static int mw_clip_0(void *c, EmPlayerLiveActor *a, int32_t *clip)
{ int r = 0; int x = x_00188570(c, a, &r); *clip = r; return x; }
static int mw_clip_1(void *c, EmPlayerLiveActor *a, int32_t *clip)
{ int r = 0; int x = x_00188590(c, a, &r); *clip = r; return x; }
static int mw_ahead(void *c, EmPlayerLiveActor *a, int32_t *r)
{ (void)c; (void)a; if (r) *r = 0; return unbound("0017F1C0 (the probe ahead)"); }
static int mw_move(void *c, EmPlayerLiveActor *a, const float target[4], uint32_t mask, int32_t *result)
{ (void)c; int r; FAULT(w_move_mask(a, target, mask, &r, NULL)); *result = r; return 0; }
static int mw_sweep(void *c, EmPlayerLiveActor *a, const float from[4], const float to[4], uint32_t mask,
                    int32_t *result)
{ (void)c; int r; FAULT(w_sweep_mask(a, from, to, mask, &r, NULL)); *result = r; return 0; }
static int mw_column(void *c, EmPlayerLiveActor *a, const float at[4], int32_t arg, float height,
                     int32_t *result)
{ (void)c; int r; FAULT(w_column_hit(a, at, arg, height, &r, NULL)); L.last_hit = 0; *result = r; return 0; }
/* 0019AB20(p, at, p + 0x280, mask). */
static int mw_ground(void *c, EmPlayerLiveActor *a, const float at[4], uint32_t mask, int32_t *result)
{
    (void)c;
    const float probe[3] = { em_live_f32(a, 0x280), em_live_f32(a, 0x284), em_live_f32(a, 0x288) };
    int r;
    L.last_hit = 0;
    FAULT(w_ground_mask(a, at, probe, mask, &r, NULL));
    *result = r;
    return 0;
}
/* The last hit's node word / byte (*(0x700031D0) + offset) and point word:
 * only the offsets the probe state carries (+1A / +1B, +24..+2C, +30). */
static int mw_hit_node_word(void *c, uint32_t offset, uint32_t *bits)
{
    (void)c;
    EmPlayerProbeHit h;
    FAULT(last_hit(&h));
    if (offset >= 0x24 && offset <= 0x2C && !(offset & 3)) { *bits = fbits(h.normal[(offset - 0x24) / 4]); return 0; }
    return unbound("a hit record word the probe state does not carry");
}
static int mw_hit_node_byte(void *c, uint32_t offset, uint8_t *value)
{
    (void)c;
    EmPlayerProbeHit h;
    FAULT(last_hit(&h));
    if (offset == 0x1A) { *value = (uint8_t)h.node; return 0; }
    if (offset == 0x1B) { *value = (uint8_t)(h.node >> 8); return 0; }
    return unbound("a hit record byte the probe state does not carry");
}
static int mw_hit_point_word(void *c, uint32_t offset, uint32_t *bits)
{
    (void)c;
    EmPlayerProbeHit h;
    FAULT(last_hit(&h));
    if (offset <= 8 && !(offset & 3)) { *bits = fbits(h.point[offset / 4]); return 0; }
    return -1;
}
static int mw_edge(void *c, EmPlayerLiveActor *a, int32_t side, float x, float y, int32_t *r)
{ (void)c; (void)a; (void)side; (void)x; (void)y; if (r) *r = 0; return unbound("0017E6E0 (the edge probe)"); }
static int mw_grab(void *c, EmPlayerLiveActor *a, int32_t *r) { int v = 0; int x = x_001782A0(c, a, &v); *r = v; return x; }
static int mw_grab_33(void *c, EmPlayerLiveActor *a, int32_t *r)
{ (void)c; (void)a; if (r) *r = 0; return unbound("00178440 (grab, surface 0x33)"); }
static int mw_reach(void *c, EmPlayerLiveActor *a, int32_t *r)
{ (void)c; (void)a; if (r) *r = 0; return unbound("001784E0 (reach)"); }
static int mw_ledge_top(void *c, EmPlayerLiveActor *a, int32_t arg, int32_t *result)
{ int r; FAULT(w_ledge_top(c, a, arg, &r)); *result = r; return 0; }
static int mw_blocked(void *c, EmPlayerLiveActor *a, int32_t side, int32_t *r)
{ (void)c; (void)a; (void)side; if (r) *r = 0; return unbound("0017F130 (blocked side)"); }
static int mw_land_sound(void *c, EmPlayerLiveActor *a, int32_t tier) { return w_land_sound(c, a, tier); }
static int mw_bind_model(void *c, EmPlayerLiveActor *a, uint32_t handle)
{ (void)c; (void)a; (void)handle; return unbound("001CA6E0 on the player (0015C1F0)"); }
static int mw_bone_count(void *c, uint32_t model, uint8_t *count)
{ (void)c; (void)model; if (count) *count = 0; return unbound("001C6150 on the player (0015C1F0)"); }
static int mw_00200890(void *c) { (void)c; return unbound("00200890 (0015C1F0)"); }
static int mw_spawn(void *c, uint32_t id, const float pos[4], float f12, uint32_t *node,
                    EmPlayerMiscEffectView *view)
{
    (void)c; (void)f12; (void)view;
    if (node) *node = 0;
    const float zero[3] = { 0.0f, 0.0f, 0.0f };
    return player_effect_gap(id, pos, zero);
}

static int misc_scene_refresh(void)
{
    EmPlayerMiscScene *s = &L.misc_scene;
    s->d810700 = scene()->d810700;
    s->d810701 = scene()->d810701;
    FAULT(progress_byte(0x00810C60u, &s->d810C60));
    s->d28215B = 0;   /* D_0028215B: the mono option; the port's options keep stereo (0) */
    for (int i = 0; i < 3; ++i) {
        s->d810360[i] = em_live_f32(L.actor, 0xB0 + 4u * (unsigned)i);
        s->d8105D0[i] = g.cam.eye[i];
    }
    s->d810360[3] = 1.0f;
    s->d8105D0[3] = 1.0f;
    {
        const uint8_t *heading = em_camera_live_bytes(0x0081027Cu, 4);   /* cam+0x9C */
        uint32_t bits = 0;
        if (heading) memcpy(&bits, heading, 4);
        s->d81027C = bfloat(bits);
    }
    s->d28A490 = NULL;
    s->d28A490_count = 0;
    return 0;
}

/* ---- The Use chain ---------------------------------------------------------- */

static int use_scan(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c;
    if (!L.scan) return unbound("00184BA0 (the interaction host's scan)");
    return L.scan(L.scan_context, a, result);
}
static int use_row_request(void *c, EmPlayerLiveActor *a, float blend) { return w_row_request(c, a, blend); }
static int use_classify(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c; (void)a;
    *result = 0;
    return unbound("001AAC00 (area 0x15's classifier)");
}
static int use_trs(void *c, uint32_t out[16], const uint32_t pos[4], const uint32_t rot[4],
                   const uint32_t scale[4])
{
    return le_trs(c, out, pos, rot, scale);
}
static int use_ledge(void *c, EmPlayerLiveActor *a, int mode, uint32_t angle, int *result)
{
    (void)c; IN3A20();
    int r = em_player_climb_live_ledge(&L.climb, a, mode, angle, result);
    OUT3A20(); return r;
}
static int use_jump(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c;
    return em_player_running_jump_use_probe(&L.rj, a, result);
}
static int use_aim(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c;
    return em_player_running_jump_use_aim(&L.rj, a, result);
}

/* 0017C440's D_00248870[tier]: the tier speeds 0, 0.1, 0.3, 0.8 (the pose
 * host's rows region holds the table, em_locomotion_display reads it the
 * same way). A tier the table does not define faults. */
static int reentry_speed(void *c, unsigned tier, uint32_t *bits)
{
    (void)c;
    /* The original indexes the table by the byte +25C without a bound; a
     * word outside the exported span is a region miss (a fault). */
    return em_pose_host_node_word(L.pose, UINT32_C(0x00248870) + 4u * tier, 0, bits);
}
/* 0017B490 over the record: em_locomotion_display's translation, reading
 * D_00248AB0's rows through the record pose's regions (the exported span)
 * and 001B0070's D_008106C8 word. */
static int loco_mode(void *c, int32_t *mode)
{
    (void)c;
    *mode = (int32_t)em_scene_req_u32(scene(), EM_SCENE_REQ_C8);
    return 0;
}
int em_player_closure_live_0017B490(void *c, EmPlayerLiveActor *a, int cmd, int idx, int tbl,
                                     int16_t *clip)
{
    (void)c;
    if (!L.bound) return -1;
    L.loco.fault = 0;
    return em_loco_0017B490(&L.loco, a, cmd, idx, tbl, clip);
}
static int reentry_select(void *c, EmPlayerLiveActor *a, int cmd, int idx, int tbl, int16_t *clip)
{
    return em_player_closure_live_0017B490(c, a, cmd, idx, tbl, clip);
}

const EmPlayerUseWorkers *em_player_closure_live_use(void) { return L.bound ? &L.use : NULL; }

void em_player_closure_live_set_scan(int (*scan)(void *context, EmPlayerLiveActor *actor,
                                                 int *result),
                                     void *context)
{
    L.scan = scan;
    L.scan_context = context;
}

/* ---- The bind ----------------------------------------------------------------- */

static void bind_fall(void)
{
    EmPlayerLandWorkers *w = &L.fall;
    memset(w, 0, sizeof *w);
    w->scratch = &L.land;
    w->request = w_request;
    w->arbiter = w_arbiter;
    w->clip_frames = w_clip_frames;
    w->sound = w_sound_300;
    w->rumble = w_rumble;
    w->effect = w_effect_bits;
    w->fade = w_fade;
    w->skeleton = w_eval_skeleton;
    w->hip = w_hip;
    w->heading = w_heading_result;
    w->test_001755B0 = w_001755B0;
    w->test_0017D080 = w_ledge_catch;
    w->test_0017F320 = w_0017F320;
    w->test_0021C190 = w_0021C190;
    w->pose_clip = w_00188550;
    w->wrap = w_wrap_bits;
    w->approach = w_approach_bits;
    w->trs = w_trs_bits;
    w->apply = w_apply_bits;
    w->floor_query = w_floor_query;
    w->land_sound = w_land_sound;
    w->translate = w_translate;
    w->probes = w_probes_s1;
    w->floor = w_floor;
    w->fall_check = w_fall_check;
    w->ledge = w_ledge_grab;
    w->reentry = w_reentry;
    w->handoff = w_handoff;
    w->react_0021C120 = w_0021C120;
    w->react_0021C350 = w_0021C350;
    w->react_0021C270 = w_0021C270;
    w->convert_00128350 = w_00128350;
    w->test_001000E0 = w_001000E0;
    w->progress_8106F1 = w_progress_8106F1;
}

static void bind_recovery(void)
{
    EmPlayerRecoveryLive *r = &L.recovery;
    memset(r, 0, sizeof *r);
    EmPlayerRecoveryWorkers *w = &r->workers;
    w->sine = w_sin; w->cosine = w_cos; w->atan2 = w_atan2; w->sqrt = w_sqrt;
    w->probes = w_probes;
    w->request = w_request; w->arbiter = w_arbiter; w->clip_frames = w_clip_frames_actor;
    w->sound = w_sound_300_u;
    w->shake = w_rumble;
    w->random = w_random;
    w->react_0021C350 = w_0021C350; w->react_0021C270 = w_0021C270; w->react_0021C120 = w_0021C120;
    w->react_0021C190 = w_0021C190; w->react_0021D490 = w_0021D490;
    w->move = w_move_hit; w->sweep = w_sweep_hit; w->segment = rv_segment;
    w->table = w_table; w->attribute = w_0019A180;
    w->ledge = w_00177510; w->lip = w_001775E0; w->sides = w_001776E0; w->hands = w_00177CF0;
    w->depth = w_depth;
    w->hang_clear = w_0017F320; w->hang_row = w_00188550;
    w->skeleton = rv_skeleton; w->column = rv_column;
    w->heading = w_heading; w->reentry = w_reentry; w->handoff = w_handoff;
    w->floor = w_floor; w->fall = w_fall_check;
    r->live = L.actor;
    r->scene = recovery_scene;
    r->shared3A20 = &L.land.s3A20;
}

static void bind_reaction(void)
{
    EmPlayerReactionWorkers *w = &L.reaction.workers;
    memset(&L.reaction, 0, sizeof L.reaction);
    w->request = w_request; w->arbiter = w_arbiter; w->clip_frames = w_clip_frames_actor;
    w->sound = w_sound_300_u; w->rumble = w_rumble; w->random = w_random;
    w->effect = w_effect4; w->attach = w_attach;
    w->floor = w_floor; w->translate = w_translate; w->probes = w_probes;
    w->heading = w_heading; w->skeleton = re_skeleton; w->fade = w_fade;
    w->model_refresh = re_model_refresh; w->stream_check = x_stream_check;
    w->atan2 = w_atan2; w->w0021C270 = w_0021C270; w->w0021C350 = w_0021C350;
    w->scratch = &L.land;
    L.reaction.scene = &L.reaction_scene;
    L.reaction.refresh = reaction_refresh;
}

static void bind_misc(void)
{
    EmPlayerMiscWorkers *w = &L.misc_w;
    memset(w, 0, sizeof *w);
    w->identity = mw_identity; w->euler = mw_euler; w->translate = mw_translate;
    w->transform = w_transform_float; w->vadd = w_vadd_float; w->vsub = mw_vsub;
    w->normalize = mw_normalize; w->dot = mw_dot;
    w->sine = w_sin_r; w->atan2 = w_atan2_r; w->sqrt = w_sqrt_r; w->wrap = w_wrap_r;
    w->side = mw_side; w->submit = mw_submit; w->sound_base = mw_sound_base;
    w->request = mw_request; w->clip_2F1_0 = mw_clip_0; w->clip_2F1_1 = mw_clip_1;
    w->ahead = mw_ahead; w->move = mw_move; w->sweep = mw_sweep; w->column = mw_column;
    w->ground = mw_ground;
    w->hit_node_word = mw_hit_node_word; w->hit_node_byte = mw_hit_node_byte;
    w->hit_point_word = mw_hit_point_word;
    w->edge = mw_edge; w->grab = mw_grab; w->grab_33 = mw_grab_33; w->reach = mw_reach;
    w->ledge_top = mw_ledge_top; w->blocked = mw_blocked;
    w->cue = w_rumble; w->land_sound = mw_land_sound; w->w0021D490 = w_0021D490;
    w->bind_model = mw_bind_model; w->bone_count = mw_bone_count; w->w00200890 = mw_00200890;
    w->spawn = mw_spawn;
    L.misc = (EmPlayerMiscHost){ &L.misc_w, &L.misc_scene, &L.misc_scratch };
}

static void bind_helpers(void)
{
    EmPlayerRecordHelpers *h = &L.helpers;
    memset(h, 0, sizeof *h);
    h->sweep = w_sweep_hit;
    h->atan2 = w_atan2;
    h->cosine = w_cos;
    h->spad3B8D = &P.spad3B8D;
    h->d810E57 = &P.gait;
    h->d810E64 = &P.lx;
    h->d810E65 = &P.ly;
    h->d8106A0 = &P.d8106A0;
    h->scratch = &L.land;
}

static void bind_hang(void)
{
    EmPlayerHangWorkers *w = &L.hang;
    memset(w, 0, sizeof *w);
    w->scene = hang_scene; w->node = w_node_float;
    w->aim_track = w_aim_track; w->hang_clear = w_0017F320; w->steer_input = w_00174FD0;
    w->ledge_ahead = w_ledge_ahead_self; w->ledge_above = w_ledge_above; w->ledge_side = w_ledge_side;
    w->request = w_request; w->sound = w_sound_full; w->skeleton = w_skeleton;
    w->translate = w_translate; w->floor = w_floor; w->column = hg_column;
    w->land_sound = w_land_sound; w->heading = w_heading; w->reentry = w_reentry;
    w->handoff = w_handoff; w->fall = w_fall_check;
    w->clip_DF70 = w_clip_DF70; w->clip_DFB0 = w_clip_DFB0; w->clip_E0D0 = w_clip_E0D0;
    w->clip_E150 = w_clip_E150; w->clip_E1D0 = w_clip_E1D0;
    w->clip_FC80 = w_clip_FC80; w->clip_FF80 = w_clip_FF80; w->clip_row = w_00188550;
    w->sound_100 = x_00182AF0; w->sound_109 = le_sound_109;
    w->sweep = hg_sweep; w->ledge_top = w_ledge_top;
    w->transform = w_transform_float; w->vadd = w_vadd_float;
    w->sqrt = w_sqrt; w->cosine = w_cos; w->sine = w_sin;
    w->wrap = w_wrap_float; w->approach = w_approach_float;
}

static void bind_ladder_climb(void)
{
    EmPlayerLadderClimbWorkers *w = &L.lc_w;
    memset(w, 0, sizeof *w);
    w->node = w_node_bits; w->hit_kind = lc_hit_kind; w->hit_y = lc_hit_y;
    w->request = w_request; w->sound = w_sound_full; w->sfx = x_sound_1FB9F0; w->cue = w_rumble;
    w->sound_109 = le_sound_109; w->steer_input = w_00174FD0; w->heading = w_heading;
    w->skeleton = w_skeleton; w->floor = w_floor; w->footstep = x_surface_sound;
    w->ground_effect = x_place; w->translate = w_translate; w->reentry = w_reentry;
    w->handoff = w_handoff; w->w0021C270 = w_0021C270; w->w0021C350 = w_0021C350;
    w->camera = lc_camera; w->clip_row = w_00188550; w->clip_frames = lc_clip_frames;
    w->probe = lc_probe; w->column = lc_column; w->wall = lc_wall; w->sweep = lc_sweep;
    w->sweep_box = lc_sweep_box; w->hit_probe = lc_hit_probe; w->hit_point = lc_hit_point;
    w->transform = w_transform_float; w->ledge_ahead = w_ledge_ahead;
    w->grab_check = x_00178390; w->dash = lc_dash; w->grab = x_001782A0; w->hit_react = x_00178080;
    w->sqrt = w_sqrt; w->sine = w_sin; w->cosine = w_cos; w->wrap = w_wrap_float;
    w->approach = lc_approach; w->to_int = w_to_int_float;
    L.lc = (EmPlayerLadderClimb){ &L.lc_w, &L.lc_scene };
}

static void bind_ladder_entry(void)
{
    EmActorCollisionWorld *cells = em_collision_world_cells();
    const EmCollProbeGrid *grid = em_collision_world_move()->grid;
    L.le_world = (EmPlayerLadderWorld){ cells->table->bytes, cells->table->size,
                                        (const uint32_t *)grid->verts, grid->vert_count,
                                        0, 0, 0, 0 };
    EmPlayerLadderWorkers *w = &L.le;
    memset(w, 0, sizeof *w);
    w->scratch = &L.le_scratch;
    w->world = &L.le_world;
    w->probe_0019BA80 = le_probe_0019BA80; w->move_0019AD00 = le_move;
    w->segment_0019A570 = le_segment; w->sweep_0019AFE0 = le_sweep; w->column_0019BC40 = le_column;
    w->trs = le_trs; w->apply = w_apply_bits_out_first; w->vadd = w_vadd_bits_out_first;
    w->identity = w_identity_bits; w->euler = w_euler_bits; w->translate = w_translate_m_bits;
    w->rotate_y = w_rotate_y_bits; w->dot = le_dot; w->normalize = le_normalize;
    w->atan2_0011E620 = w_atan2_bits; w->wrap_001B1470 = w_wrap_bits; w->fabs_0011DF78 = w_fabs_bits;
    w->cos_0011DE90 = w_cos_bits; w->sqrt_0011E748 = w_sqrt_bits;
    w->request = w_request; w->sound = w_sound_300; w->sound_base_00179B90 = le_sound_base;
    w->wall_001762E0 = le_wall_001762E0; w->node = le_node;
}

/* The ladder entry world's scene bytes (D_00810700, D_00810C7C / C7D and
 * D_002754D0[0], which 00165B60 requests): refreshed before each use. */
static int le_pre(void)
{
    L.le_world.area = scene()->d810700;
    FAULT(progress_byte(0x00810C7Cu, &L.le_world.d810C7C));
    FAULT(progress_byte(0x00810C7Du, &L.le_world.d810C7D));
    L.le_world.d2754D0 = em_player_ladder_climb_d2754D0(0);
    return 0;
}

static int c0e_hit_surface(void *c, uint8_t *surface)
{
    (void)c;
    EmPlayerProbeHit h;
    FAULT(last_hit(&h));
    *surface = (uint8_t)h.node;
    return 0;
}
static int c0e_use_test(void *c, EmPlayerLiveActor *a, int *result)
{
    (void)c; IN3A20();
    int r = em_player_weapon_001607D0(&L.wa, a, result);
    OUT3A20(); return r;
}
static int c0e_sweep(void *c, EmPlayerLiveActor *a, const uint32_t from[4], const uint32_t to[4],
                     unsigned mask, int *result)
{
    (void)c;
    float f[4], t[4];
    bits4(from, f); bits4(to, t);
    return w_sweep_mask(a, f, t, mask, result, NULL);
}
static int c0e_ground(void *c, EmPlayerLiveActor *a, const uint32_t point[4], unsigned mask, int *result)
{
    (void)c;
    float at[4];
    bits4(point, at);
    const float probe[3] = { em_live_f32(a, 0x280), em_live_f32(a, 0x284), em_live_f32(a, 0x288) };
    L.last_hit = 0;
    return w_ground_mask(a, at, probe, mask, result, NULL);
}
static int c0e_point_test(void *c, EmPlayerLiveActor *a, const uint32_t point[4], unsigned mask, int *result)
{
    (void)c;
    float t[4];
    bits4(point, t);
    return w_move_mask(a, t, mask, result, NULL);
}

static void bind_closure_0e(void)
{
    EmPlayerClosureWorkers *w = &L.c0e;
    memset(w, 0, sizeof *w);
    w->scratch = &L.c0e_scratch;
    w->scene = closure_scene; w->node = w_node_bits; w->hit_surface = c0e_hit_surface;
    w->use_test = c0e_use_test; w->sweep = c0e_sweep; w->ground = c0e_ground;
    w->point_test = c0e_point_test;
    w->set_275B08 = w_set_275B08; w->set_810702 = w_set_810702;
    w->request = w_request; w->sound = w_sound_full; w->steer = w_00174FD0;
    w->ledge_move = w_ledge_move; w->clip_FF80 = w_clip_FF80; w->clip_FC80 = w_clip_FC80;
    w->clip_DFB0 = w_clip_DFB0; w->clip_E0D0 = w_clip_E0D0; w->clip_E150 = w_clip_E150;
    w->clip_E1D0 = w_clip_E1D0; w->surface_sound = x_surface_sound; w->sound_109 = le_sound_109;
    w->land_sound = w_land_sound; w->floor = w_floor; w->place = x_place;
    w->pose_reset = w_row_request; w->skeleton = w_skeleton; w->translate = w_translate;
    w->arbiter = w_arbiter; w->clip_row = w_00188550; w->clip_row_B = x_001885B0;
    w->ledge_3D = x_00178390; w->ledge_3B = x_001782A0;
    w->random = w_random5; w->clip_frames = w_clip_frames; w->to_int = w_to_int_bits;
    w->fade = w_fade; w->fade_end = w_fade_end; w->camera = x_camera_1B0460; w->alloc = x_alloc;
    w->trs = w_trs_bits; w->transform = w_apply_bits; w->vadd = w_vadd_bits;
    w->sine = w_sin_r; w->cosine = w_cos_r; w->wrap = w_wrap_bits; w->approach = w_approach_bits;
}

/* ---- Closure 10-12-19 ------------------------------------------------------- */

static int c10_segment(void *c, const uint32_t from[4], const uint32_t to[4], unsigned mask, int id,
                       int *result, EmPlayerProbeHit *hit)
{
    (void)c;
    float f[4], t[4];
    bits4(from, f); bits4(to, t);
    FAULT(w_segment_r(f, t, mask, id, result));
    if (hit) {
        if (*result) FAULT(segment_hit(hit));
        else memset(hit, 0, sizeof *hit);
    }
    return 0;
}
static int c10_move(void *c, EmPlayerLiveActor *a, const uint32_t target[4], unsigned mask, int *result,
                    EmPlayerProbeHit *hit)
{
    (void)c;
    float t[4];
    bits4(target, t);
    return w_move_mask(a, t, mask, result, hit);
}
static int c10_sweep(void *c, EmPlayerLiveActor *a, const uint32_t from[4], const uint32_t to[4],
                     unsigned mask, int *result, EmPlayerProbeHit *hit)
{
    (void)c;
    float f[4], t[4];
    bits4(from, f); bits4(to, t);
    return w_sweep_mask(a, f, t, mask, result, hit);
}
static int c10_ground(void *c, EmPlayerLiveActor *a, const uint32_t at[4], const uint32_t probe[4],
                      unsigned mask, int *result, EmPlayerProbeHit *hit)
{
    (void)c;
    float p[4], q[4];
    bits4(at, p); bits4(probe, q);
    L.last_hit = 0;
    return w_ground_mask(a, p, q, mask, result, hit);
}
/* 0019A310 over the hit (em_player_slope_angle with the SDK sqrt / atan). */
static float fw_sqrt(void *c, float x) { return w_sqrt(c, x); }
static float fw_atan(void *c, float x) { (void)c; return em_sdk_math_original_float_0011DBB8(L.sdk, x); }
static int c10_slope(void *c, const EmPlayerProbeHit *hit, uint32_t *out)
{
    (void)c;
    EmPlayerFloorWorkers math;
    memset(&math, 0, sizeof math);
    math.sqrt = fw_sqrt;
    math.atan = fw_atan;
    float angle;
    em_player_slope_angle(hit, &math, &angle);
    *out = fbits(angle);
    return L.sdk->fault ? -1 : 0;
}
static int c10_00179150(void *c, EmPlayerLiveActor *a)
{
    (void)c; IN3A20();
    int r = em_player_closure_00179150(&L.c0e, a);
    OUT3A20(); return r;
}
/* *(p + slot) + offset: a node the record's pointer word names. */
static int c10_bone(void *c, EmPlayerLiveActor *a, unsigned slot, unsigned offset, uint32_t *bits)
{
    (void)c;
    return em_pose_host_node_word(L.pose, em_live_u32(a, slot), offset, bits);
}

static void bind_closure_10(void)
{
    EmPlayerClosure1019Workers *w = &L.c10_w;
    memset(w, 0, sizeof *w);
    w->request = w_request;
    w->clip_885B0 = x_001885B0; w->clip_88610 = x_00188610; w->clip_88550 = w_00188550;
    w->stick = w_stick; w->steer = w_00174FD0; w->floor = w_floor; w->translate = w_translate;
    w->random5 = w_random5; w->sound = w_sound_full; w->sound_1FB9F0 = x_sound_1FB9F0;
    w->wrap = w_wrap_bits; w->approach = w_approach_bits;
    w->cosine = w_cos; w->sine = w_sin; w->atan2 = w_atan2; w->sqrt = w_sqrt;
    w->to_int = w_to_int_bits; w->trs = w_trs_bits; w->apply = w_apply_bits; w->vadd = w_vadd_bits;
    w->identity = w_identity_bits; w->rotate_y = w_rotate_y_bits; w->translate_m = w_translate_m_bits;
    w->segment = c10_segment; w->move = c10_move; w->sweep = c10_sweep; w->ground = c10_ground;
    w->slope = c10_slope; w->floor_query = w_floor_query; w->w00179150 = c10_00179150;
    w->midpoint = le_midpoint; w->ledge_top = x_001782A0; w->area_point = x_area_point;
    w->skeleton = w_eval_skeleton; w->script_1B0460 = x_camera_1B0460;
    w->fade = w_fade; w->fade_1AEE10 = w_fade_end;
    w->use = use_scan; w->use_probe = le_use_probe; w->arbiter = w_arbiter;
    w->rotate_x = w_rotate_x_bits; w->land = w_land; w->surface5d = w_surface5d;
    w->teleport = w_teleport; w->land_sound = w_land_sound; w->sound_182A70 = le_sound_109;
    w->clip_FC80 = w_clip_FC80; w->root_node = w_root_node; w->bone = c10_bone;
    L.c10 = (EmPlayerClosure1019){ &L.c10_w, &L.c10_scene, &L.c10_scratch, &L.m2_scene };
}

/* ---- Major 2 -------------------------------------------------------------------- */

static int m2_0021E650(void *c, EmPlayerLiveActor *a)
{
    (void)c; MISC_IN();
    int r = em_player_misc_w_0021E650(&L.misc, a);
    MISC_OUT(); return r;
}

static void bind_major2(void)
{
    EmPlayerMajor2Workers *w = &L.m2_w;
    memset(w, 0, sizeof *w);
    w->request = w_request; w->sound = w_sound_full; w->cue = w_rumble;
    w->w001EFE00 = w_attach_plain; w->w0015C1F0 = x_0015C1F0; w->w0021E650 = m2_0021E650;
    w->w0021D2E0 = w_teleport; w->drop = w_drop; w->floor = w_floor; w->translate = w_translate;
    w->land_sound = w_land_sound; w->w0021D250 = w_surface5d; w->w0021D490 = w_0021D490;
    w->w0021C120 = w_0021C120; w->w0021C200 = x_0021C200; w->w0021C270 = w_0021C270;
    w->w0021C350 = w_0021C350; w->w0021C190 = w_0021C190; w->w0017FC80 = w_clip_FC80;
    w->w0017FF80 = w_clip_FF80; w->random = w_random; w->w00188550 = w_00188550;
    w->w001885B0 = x_001885B0; w->root_node = w_root_node;
    L.m2_scene.d8106F1 = em_scene_req_at(scene(), 0x008106F1u);
    L.m2_scene.d810707 = em_scene_progress_at(scene(), 0x00810707u, 1);
    L.m2 = (EmPlayerMajor2){ &L.m2_w, &L.m2_scene };
}

/* ---- Slide and climb ---------------------------------------------------------- */

static void bind_slide(void)
{
    EmPlayerSlideLive *l = &L.slide;
    memset(l, 0, sizeof *l);
    EmPlayerSlideWorkers *w = &l->workers;
    w->stop_sound = w_stop_sound; w->effect = w_effect; w->move = sw_move; w->sweep = sw_sweep;
    w->sine = w_sin; w->cosine = w_cos; w->atan2 = w_atan2;
    w->scratch = &L.land;
    l->floor = w_floor; l->fall = w_fall_check;
    l->scene = slide_scene;
    l->request = w_request; l->arbiter = w_arbiter; l->clip_frames = w_clip_frames;
    l->sound = w_sound_handle; l->translate = w_translate; l->damage = sl_damage;
    l->land_check = w_land_check; l->land = w_land; l->step_sound = sl_step_sound;
    l->land_sound = w_land_sound; l->surface5d = w_surface5d; l->teleport = w_teleport;
    l->scratch = &L.land;
}

static void bind_climb(void)
{
    EmPlayerClimbLive *l = &L.climb;
    memset(l, 0, sizeof *l);
    EmPlayerClimbWorkers *w = &l->workers;
    w->move = cw_move; w->sweep = cw_sweep; w->segment = cw_segment; w->column = cw_column;
    w->table = w_table; w->atan2 = w_atan2; w->sqrt = w_sqrt; w->effect = w_effect;
    w->scratch = &L.land;
    l->floor = w_floor; l->probes = w_probes; l->fall = w_fall_check;
    l->scene = climb_scene;
    l->link_kind = climb_link_kind;
    l->request = w_request; l->arbiter = w_arbiter; l->clip_frames = w_clip_frames;
    l->sound = w_sound_300; l->land_sound = w_land_sound; l->translate = w_translate;
    l->heading = w_heading; l->reentry = w_reentry; l->handoff = w_handoff; l->land = w_land;
    l->skeleton = climb_skeleton;
    l->scratch = &L.land;
}

/* ---- Weapon states A / B ---------------------------------------------------------- */

static int wa_clip_id(void *c, uint32_t table, unsigned index, int16_t *clip)
{
    (void)c;
    uint32_t word;
    FAULT(em_pose_host_node_word(L.pose, table + 2u * index, 0, &word));
    *clip = (int16_t)(word & 0xFFFF);
    return 0;
}
static int wa_matrix(void *c, EmPlayerLiveActor *a)
{ (void)c; (void)a; return unbound("0017A130 (anim_matrix_dispatch)"); }
static int wa_bone(void *c, unsigned slot, uint32_t words[16])
{ (void)c; return em_pose_view_bone(&L.view, slot, words); }
static int wa_0017C370(void *c, EmPlayerLiveActor *a)
{ (void)c; IN3A20(); int r = em_player_0017C370(L.stage_host, a); OUT3A20(); return r; }
static int wa_link20(void *c, uint32_t word, uint32_t *c0, uint32_t *c8)
{
    (void)c; (void)word;
    if (c0) *c0 = 0;
    if (c8) *c8 = 0;
    return unbound("the +20 object's +C0 / +C8 (the port keeps no +20 handle)");
}
static int wa_i3(void *c, EmPlayerLiveActor *a, int a1, int *r)
{ (void)c; (void)a; (void)a1; if (r) *r = 0; return unbound("0017A8B0 / 0017A970 (weapon stance entry)"); }
static int wa_i1(void *c, EmPlayerLiveActor *a, int *r)
{ (void)c; (void)a; if (r) *r = 0; return unbound("0017AAD0 (weapon stance test)"); }
static int wa_arg(void *c, EmPlayerLiveActor *a, int a1)
{ (void)c; (void)a; (void)a1; return unbound("a weapon stance handler (0017B300 / 0016F530 / 00170A60)"); }
static int wa_plain(void *c, EmPlayerLiveActor *a)
{ (void)c; (void)a; return unbound("a weapon stance handler (0016F5D0 / 0017ABA0 / 00199220 / 00171xxx / 0016F600)"); }
static int wa_lock(void *c, EmPlayerLiveActor *a, uint32_t cur, uint32_t *r)
{ (void)c; (void)a; (void)cur; if (r) *r = 0; return unbound("00185A10 / 00185E30 (lock target)"); }
static int wa_rate(void *c, EmPlayerLiveActor *a, float rate)
{ (void)c; (void)a; (void)rate; return unbound("00172860"); }

static void bind_weapon_a(void)
{
    EmPlayerWeaponWorkers *w = &L.wa_w;
    memset(w, 0, sizeof *w);
    w->w0016F5D0 = wa_plain; w->w0017A8B0 = wa_i3; w->w0017A970 = wa_i3; w->w0017AAD0 = wa_i1;
    w->w0017C370 = wa_0017C370; w->w0017B300 = wa_arg; w->w0016F530 = wa_arg;
    w->request = w_request; w->clip_id = wa_clip_id; w->skeleton = w_eval_skeleton;
    w->matrix = wa_matrix; w->bone = wa_bone; w->w0017ABA0 = wa_plain;
    w->w00185A10 = wa_lock; w->w00185E30 = wa_lock; w->w00199220 = wa_plain;
    w->w00170A60 = wa_arg; w->w00171320 = wa_plain; w->w00171670 = wa_plain;
    w->w00171B00 = wa_plain; w->w00171E90 = wa_plain; w->w001723D0 = wa_plain;
    w->w00172860 = wa_rate; w->w0016F600 = wa_plain; w->link20 = wa_link20;
    w->atan2 = w_atan2_bits; w->wrap = w_wrap_bits; w->approach = w_approach_bits;
    w->sound = w_sound_full; w->heading = w_heading; w->reentry = w_reentry; w->handoff = w_handoff;
    w->translate = w_translate; w->probes = w_probes_record; w->floor = w_floor;
    w->fall_check = w_fall_check;
    L.wa = (EmPlayerWeaponStates){ &L.wa_w, &L.wa_scene };
}

static int wb_request(void *c, EmPlayerLiveActor *a, int clip, int force, uint32_t blend)
{ (void)c; IN3A20(); int r = em_pose_host_request_bits(L.pose, a, clip, force, blend); OUT3A20(); return r; }
static int wb_link18(void *c, uint32_t word, uint8_t **record)
{
    (void)c; (void)word;
    if (record) *record = NULL;
    return unbound("the +18 melee target record (the port keeps no +18 handle)");
}

static void bind_weapon_b(void)
{
    EmPlayerWeaponBWorkers *w = &L.wb;
    memset(w, 0, sizeof *w);
    L.wb_scene = (EmPlayerWeaponBScene){ &wb_d8106E0, &wb_d810CA4, &L.land.s3A20, &L.wb_pad,
                                         &L.wb_3B78 };
    w->scene = &L.wb_scene;
    w->request = wb_request; w->sound = w_sound_handle; w->stop_sound = w_stop_sound;
    w->heading = w_heading_result; w->approach = w_approach_bits; w->wrap = w_wrap_bits;
    w->atan2 = w_atan2_bits; w->translate = w_translate; w->probes = w_probes; w->floor = w_floor;
    w->fall_check = w_fall_check; w->reentry = w_reentry; w->handoff = w_handoff;
    w->link18 = wb_link18; w->link20 = wa_link20; w->bone = wa_bone; w->clip_id = wa_clip_id;
    w->skeleton = w_eval_skeleton; w->matrix = wa_matrix;
    w->reload = wa_arg; w->draw = wa_arg; w->reload_wait = wa_plain; w->pose = wa_plain;
    w->acquire = wa_plain; w->fire_00170A60 = wa_arg; w->fire_00171320 = wa_plain;
    w->fire_00171670 = wa_plain; w->fire_00171B00 = wa_plain; w->fire_00171E90 = wa_plain;
    w->fire_001723D0 = wa_plain;
}

/* ---- Running jump ------------------------------------------------------------------ */

static int rj_link_type(void *c, const void *owner, uint8_t *type)
{
    (void)c;
    const EmActor *a = owner;
    if (!a || !type) return -1;
    *type = a->model;   /* (+308)+3 */
    return 0;
}
static int rj_target(void *c, int index, EmPlayerRunningJumpTarget *out)
{
    (void)c; (void)index; (void)out;
    return unbound("D_00275B8C (the class-2 list: no AREA11 owner publishes one)");
}
static int rj_target_xz(void *c, const void *object, float *x, float *z)
{ (void)c; (void)object; (void)x; (void)z; return unbound("001AA4E0's target"); }
static int rj_target_radius(void *c, const void *object, float *radius)
{ (void)c; (void)object; (void)radius; return unbound("001AA410"); }
static int rj_target_sight(void *c, EmPlayerLiveActor *a, const void *object, float radius, int *r)
{ (void)c; (void)a; (void)object; (void)radius; if (r) *r = 0; return unbound("001AA2A0"); }
static int rj_column(void *c, EmPlayerLiveActor *a, const float at[4], int arg, float height, int *r)
{ (void)c; return w_column_hit(a, at, arg, height, r, NULL); }
/* 0017DEB0(p): the climb module's one translation over the record. */
static int rj_dust(void *c, EmPlayerLiveActor *a)
{ (void)c; return em_player_climb_live_0017DEB0(&L.climb, a); }
static int rj_root_clock(void *c, uint32_t *value) { (void)c; return em_pose_view_root_clock(&L.view, value); }

static void bind_running_jump(void)
{
    EmPlayerRunningJumpLive *l = &L.rj;
    memset(l, 0, sizeof *l);
    EmPlayerRunningJumpWorkers *w = &l->workers;
    w->scratch = &L.rj_scratch;
    w->sine = w_sin; w->cosine = w_cos; w->atan2 = w_atan2; w->sqrt = w_sqrt;
    w->move = w_move_hit; w->sweep = w_sweep_hit; w->table = w_table; w->ledge = w_00177510;
    w->column = rj_column; w->link_type = rj_link_type; w->target = rj_target;
    w->target_xz = rj_target_xz; w->target_radius = rj_target_radius;
    w->target_sight = rj_target_sight; w->heading = w_heading_result;
    w->request = w_request; w->arbiter = w_arbiter; w->clip_frames = w_clip_frames;
    w->sound = w_sound_300; w->translate = w_translate; w->strafe = w_strafe; w->quadrant = w_stick;
    w->react = w_react_002243F0; w->grab = w_ledge_grab; w->dust = rj_dust;
    w->land = w_land; w->surface5d = w_surface5d; w->teleport = w_teleport;
    w->probes = w_probes_s1; w->floor = w_floor; w->fall_check = w_fall_check;
    w->root_clock = rj_root_clock;
    l->scene = running_jump_scene;
    l->shared3A20 = &L.land.s3A20;
}

/* ---- The Use chain's workers -------------------------------------------------------- */

static void bind_use(void)
{
    EmPlayerUseWorkers *w = &L.use;
    memset(w, 0, sizeof *w);
    w->scene = &L.use_scene;
    w->scan = use_scan; w->row_request = use_row_request; w->classify = use_classify;
    w->surface = le_surface; w->trs = use_trs; w->ledge = use_ledge; w->wrap = w_wrap_bits;
    w->jump = use_jump; w->aim = use_aim;
    EmPlayerReentryWorkers *r = &L.reentry;
    memset(r, 0, sizeof *r);
    r->spad3A20 = &L.land.s3A20;
    r->speed = reentry_speed;
    r->translate = w_translate;
    r->select = reentry_select;
    r->clip_frames = w_clip_frames;
    r->arbiter = w_arbiter;
}

/* ---- The pose host's 00178910 callees ------------------------------------------ */

static int ph_column(void *c, const float position[3], EmPlayerFloorTable *table)
{
    (void)c;
    return em_actor_collision_player_column(em_collision_world_column_player(), position, table);
}
static int ph_ledge_hit(void *c, EmPoseLedgeHit *hit)
{
    (void)c;
    EmPlayerProbeHit h;
    FAULT(last_hit(&h));
    hit->x = fbits(h.point[0]);
    hit->z = fbits(h.point[2]);
    hit->nx = fbits(h.normal[0]);
    hit->nz = fbits(h.normal[2]);
    return 0;
}
static float ph_atan2(void *c, float y, float x) { return w_atan2(c, y, x); }

/* ---- Per-call refresh ------------------------------------------------------------ */

static int refresh_all(void)
{
    refresh_pad();
    FAULT(misc_scene_refresh());
    FAULT(le_pre());
    L.use_scene.d810E74 = scene()->d810E74;
    L.use_scene.spad3B76 = L.pad_config[1];
    L.use_scene.area = scene()->d810700;
    return 0;
}

int em_player_closure_live_use_press(EmPlayerLiveActor *actor, int *result)
{
    if (!L.bound || !actor || !result) return -1;
    L.sdk->fault = 0;
    FAULT(refresh_all());
    int r = em_player_use_00160220(&L.use, actor, result);
    if (r >= 0 && L.sdk->fault) {
        fprintf(stderr, "player closure: 00160220: an SDK worker faulted at 0x%08X\n",
                (unsigned)L.sdk->fault);
        r = -1;
    }
    return r;
}

/* ---- The bind --------------------------------------------------------------------- */

static void set1(EmPlayerStatesBinding *b, unsigned state, EmPlayerStateCallback fn, void *ctx,
                 void *mod3A20, int (*pre)(void), int (*post)(void), const char *name)
{
    slot(&L.state[state], fn, ctx, mod3A20, pre, post, name);
    b->stage.state[state] = slot_run;
    b->stage.state_context[state] = &L.state[state];
}

static void set2(EmPlayerStatesBinding *b, unsigned state, EmPlayerStateCallback fn, void *ctx,
                 void *mod3A20, int (*pre)(void), int (*post)(void), const char *name)
{
    slot(&L.state2[state], fn, ctx, mod3A20, pre, post, name);
    b->stage.state2[state] = slot_run;
    b->stage.state2_context[state] = &L.state2[state];
}

int em_player_closure_live_bind(EmPlayerStatesBinding *b, EmPlayerStageHost *stage_host,
                                EmPoseHost *pose, EmPlayerStageMajor4 *major4)
{
    if (!b || !stage_host || !stage_host->globals || !pose || !pose->globals || !major4 ||
        !em_collision_world_loaded() || !em_collision_world_sdk() || !em_collision_world_player() ||
        !em_collision_world_move_player() || !em_collision_world_column_player())
        return -1;
    memset(&L, 0, sizeof L);
    s_mod3A20 = NULL;
    L.actor = player_states_actor_mut();
    L.pose = pose;
    L.stage_host = stage_host;
    L.sdk = em_collision_world_sdk();
    L.d275B40 = PLAYER_NODE_ARRAY;
    L.view = (EmPoseActorView){ pose, L.actor, &L.d275B40 };
    em_slg_001AF470(L.pad_config, 0);

    /* 0x70003A20: one word (the pose routines' pointer too). */
    L.land.s3A20 = stage_host->globals->spad3A20;
    pose->globals->spad3A20 = &L.land.s3A20;
    pose->callees.context = NULL;
    pose->callees.column = ph_column;
    pose->callees.ledge_hit = ph_ledge_hit;
    pose->callees.atan2 = ph_atan2;

    L.heading.world = (EmPlayerHeadingRecordWorld){ &P.spad3B8D, &P.gait, &P.lx, &P.ly, &P.d8106A0,
                                                    &L.land.s3A20, L.sdk->tables, &L.sdk->world,
                                                    &L.sdk->workers };
    L.loco.workers.mode = loco_mode;
    L.loco.display.pose = pose;
    L.loco.display.d275B40 = &L.d275B40;

    bind_helpers();
    bind_misc();
    bind_fall();
    bind_recovery();
    bind_reaction();
    bind_hang();
    bind_ladder_climb();
    bind_ladder_entry();
    bind_closure_0e();
    bind_closure_10();
    bind_major2();
    bind_slide();
    bind_climb();
    bind_weapon_a();
    bind_weapon_b();
    bind_running_jump();
    bind_use();
    if (!L.m2_scene.d8106F1 || !L.m2_scene.d810707) return -1;

    /* 0015B130's table (FLOOR closure and the Use roots). */
    set1(b, 0x04, em_player_recovery_state4, &L.recovery, NULL, NULL, NULL, "00162A40");
    set1(b, 0x05, em_player_fall_state5, &L.fall, NULL, NULL, NULL, "00162DB0");
    set1(b, 0x07, em_player_fall_state7, &L.fall, NULL, NULL, NULL, "001639E0");
    set1(b, 0x08, em_player_fall_state8, &L.fall, NULL, NULL, NULL, "00163B40");
    set1(b, 0x09, em_player_hang_state, &L.hang, NULL, NULL, NULL, "001647D0");
    set1(b, 0x0C, em_player_ladder_climb_state, &L.lc, &L.lc_scene.spad3A20[0], lc_pre, lc_post, "001662D0");
    set1(b, 0x0E, em_player_closure_state0E, &L.c0e, &L.c0e_scratch.s3A20, NULL, closure_post, "00168050");
    set1(b, 0x13, em_player_closure_state13, &L.c0e, &L.c0e_scratch.s3A20, NULL, closure_post, "0016B790");
    set1(b, 0x14, em_player_closure_state14, &L.c0e, &L.c0e_scratch.s3A20, NULL, closure_post, "0016B8A0");
    set1(b, 0x18, em_player_closure_state18, &L.c0e, &L.c0e_scratch.s3A20, NULL, closure_post, "0016D130");
    set1(b, 0x10, em_player_closure1019_00169730, &L.c10, &L.c10_scratch.s3A20, c10_pre, c10_post, "00169730");
    set1(b, 0x12, em_player_closure1019_0016AE40, &L.c10, &L.c10_scratch.s3A20, c10_pre, c10_post, "0016AE40");
    set1(b, 0x19, em_player_closure1019_0016DE40, &L.c10, &L.c10_scratch.s3A20, c10_pre, c10_post, "0016DE40");
    set1(b, 0x1A, em_player_closure1019_0016EBA0, &L.c10, &L.c10_scratch.s3A20, c10_pre, c10_post, "0016EBA0");
    set1(b, 0x1C, em_player_slide_live_state, &L.slide, NULL, NULL, NULL, "0016C6A0");
    set1(b, 0x1D, em_player_weapon_state1D, &L.wa, &L.wa_scene.spad3A20, wa_pre, wa_post, "0016FCF0");
    set1(b, 0x1E, em_player_weapon_state1E, &L.wa, &L.wa_scene.spad3A20, wa_pre, wa_post, "001703E0");
    set1(b, 0x1F, em_player_weapon_state1F, &L.wa, &L.wa_scene.spad3A20, wa_pre, wa_post, "001729A0");
    set1(b, 0x20, em_player_weapon_b_state20, &L.wb, NULL, wb_pre, wb_post, "00173000");
    set1(b, 0x21, em_player_weapon_b_state21, &L.wb, NULL, wb_pre, wb_post, "001735C0");
    set1(b, 0x22, em_player_weapon_b_state22, &L.wb, NULL, wb_pre, wb_post, "00173E60");
    set1(b, 0x02, em_player_climb_live_state, &L.climb, NULL, NULL, NULL, "00161790");
    set1(b, 0x03, em_player_climb_live_state, &L.climb, NULL, NULL, NULL, "00162190");
    set1(b, 0x06, em_player_running_jump_state6, &L.rj, NULL, NULL, NULL, "001634A0");
    set1(b, 0x0B, em_player_ladder_state_b, &L.le, &L.le_scratch.s3A20[0], NULL, NULL, "00165B60");
    set1(b, 0x24, em_player_running_jump_state24, &L.rj, NULL, NULL, NULL, "001747F0");

    /* 0015B770's table. */
    static const struct { unsigned state; EmPlayerStateCallback fn; const char *name; } kReaction[] = {
        { 0x00, em_player_reaction_live_0021D800, "0021D800" },
        { 0x17, em_player_reaction_live_0021D800, "0021D800" },
        { 0x01, em_player_reaction_live_0021E240, "0021E240" },
        { 0x02, em_player_reaction_live_0021E490, "0021E490" },
        { 0x18, em_player_reaction_live_0021E490, "0021E490" },
        { 0x0A, em_player_reaction_live_00223C70, "00223C70" },
        { 0x0B, em_player_reaction_live_0021F330, "0021F330" },
        { 0x0C, em_player_reaction_live_0021F850, "0021F850" },
        { 0x0F, em_player_reaction_live_002202C0, "002202C0" },
        { 0x10, em_player_reaction_live_0021DBB0, "0021DBB0" },
        { 0x11, em_player_reaction_live_0021E9C0, "0021E9C0" },
        { 0x12, em_player_reaction_live_0021EAD0, "0021EAD0" },
        { 0x13, em_player_reaction_live_0021EAD0, "0021EAD0" },
        { 0x14, em_player_reaction_live_0021EF30, "0021EF30" },
    };
    for (unsigned i = 0; i < sizeof kReaction / sizeof *kReaction; ++i)
        set2(b, kReaction[i].state, kReaction[i].fn, &L.reaction, NULL, reaction_pre, reaction_post,
             kReaction[i].name);
    static const struct { unsigned state; EmPlayerStateCallback fn; const char *name; } kMajor2[] = {
        { 0x03, em_player_major2_0021E830, "0021E830" },
        { 0x04, em_player_major2_00221FC0, "00221FC0" },
        { 0x05, em_player_major2_00222580, "00222580" },
        { 0x06, em_player_major2_00222AD0, "00222AD0" },
        { 0x07, em_player_major2_002230A0, "002230A0" },
        { 0x16, em_player_major2_00225570, "00225570" },
        { 0x19, em_player_major2_002255C0, "002255C0" },
    };
    for (unsigned i = 0; i < sizeof kMajor2 / sizeof *kMajor2; ++i)
        set2(b, kMajor2[i].state, kMajor2[i].fn, &L.m2, NULL, m2_pre, m2_post, kMajor2[i].name);

    /* 0015B530's 00162DB0 / 00163B40 (PLAYER_FALL.md "Stage slots"). */
    slot(&L.major4[EM_PLAYER_MAJOR4_00162DB0], em_player_fall_state5, &L.fall, NULL, NULL, NULL, "00162DB0");
    slot(&L.major4[EM_PLAYER_MAJOR4_00163B40], em_player_fall_state8, &L.fall, NULL, NULL, NULL, "00163B40");
    major4->routine[EM_PLAYER_MAJOR4_00162DB0] = slot_run;
    major4->routine_context[EM_PLAYER_MAJOR4_00162DB0] = &L.major4[EM_PLAYER_MAJOR4_00162DB0];
    major4->routine[EM_PLAYER_MAJOR4_00163B40] = slot_run;
    major4->routine_context[EM_PLAYER_MAJOR4_00163B40] = &L.major4[EM_PLAYER_MAJOR4_00163B40];

    L.bound = 1;
    return 0;
}
