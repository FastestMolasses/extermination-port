/* Scene coordinator bindings, steps S8-S10b (legacy mode). See
 * em_scene_bindings.h and docs/SCENE_COORDINATOR_DESIGN.md sections 3.1,
 * 6 (S8, S9, S10a, S10b) and 10.1.
 *
 * What is live after S10b:
 *   - the slot-0 task is em_scene_task_001ACEC0, which runs the S3 cores
 *     001ACEC0 -> 001AD250 and, through the w_001AD4D0 binding (the original
 *     001AD4D0 is a tail jump to 0x1AE040), the S2 frame core 0x1AE040 with
 *     the lead's Q1 entry point (em_sf_001AE040_q1);
 *   - S9: the state-0 tick returns. 0x1AE040 state 0 ends with
 *     `b .L001AE5CC` (0x1AE0DC, the epilogue), never falling into state 1,
 *     so the tick that rebuilds the area runs no world variant; the first
 *     world frame (001AE5E0/001AE6B0) is the next task tick;
 *   - S10a: the state-1 world frame is the translated variant
 *     em_sf_001AE5E0 (3B8D == 0) or em_sf_001AE6B0 (3B8D != 0). Their stage
 *     workers are bound below to the stage functions of em_player_frame.c
 *     and em_render_frame.c, in the original order (design 2.4);
 *   - S10b: the actor pool is live. State 0 resets it at the 001AFCA0
 *     position (001AF8E0's pool half), spawns the AREA11 roster at 001B6990
 *     (em_actor_roster over assets/scene_snow/roster.emro), the weather node
 *     at 001C1DC0 (interim) and the area-title node at 001C5C50; the first
 *     0015BCF0 after the state-0 wipe spawns 0015C420's children. Every
 *     001AFD70 position is the pool walk (modes 0, 1 and 2), each node
 *     bound by em_area11_bindings.c and traced per node. A scene without an
 *     original roster (office, drawbridge) gets one legacy_world node at
 *     001B6990 whose behaviour is the S10a legacy block, unchanged;
 *   - S10a interim (retired by S11a): canonical 3B8D is published from the
 *     port's g.frame_selector at the 0x1AE040 entry, before the frame state
 *     is traced and before 0x1AE040 reads it. The port's 3B8D writers (the
 *     opening runtime's counterpart of 001B82D0) still write
 *     g.frame_selector, and they run inside the world frame's 001AFD70
 *     block, so a selector written during frame N picks frame N+1's variant,
 *     as in the original. 001AFCF0 (states 0 and 4) clears canonical 3B8D;
 *     the port's state-0 re-arm clears g.frame_selector at the same tick;
 *   - spad 3B90 (001ACEC0 writes 2 every tick) and C4 are forwarded to the
 *     step-D letterbox gate at the end of every task tick (design 2.1);
 *   - the S6 trace contract (design 10.1) when EM_FRAME_TRACE is set.
 *
 * Worker bindings. Every worker the chain reaches in legacy mode is bound;
 * every other worker is NULL and faults when reached (fail-stop):
 *   w_001AD4D0            -> frame machine entry (legacy Continue hook,
 *                            3B8D publication, trace frame state, 0x1AE040)
 *   w_001AE5E0/w_001AE6B0 -> the variant cores, after the legacy head
 *                            (em_game_legacy_variant_head: test hooks and,
 *                            in gameplay, the status/game-over frozen frame
 *                            that S11b retires; while it runs, the variant's
 *                            stages do not)
 *   variant stage workers -> see "world-frame stage workers" below. They
 *                            fault outside a variant (state 3's 001D1C50 /
 *                            001D1EA0(0) are not bound until S11b).
 *   w_001AFCA0            -> the port's native state-0 re-arm
 *                            (em_game_legacy_state0), the pool reset 001AF8E0,
 *                            then spad 31F4 = 0 (001AFCA0 stores it)
 *   w_001B6990, w_001C1DC0, w_001C5C50 -> the pool spawns (S10b, above)
 *   w_001AFCF0, w_001AD140, w_001AD010 -> the S3 cores (design 10.1)
 *   r_0028A9A0            -> em_frame_transition()->substate
 *   r_00275B44            -> the current actor stored by w_001CB590
 *   r_008102B9            -> 0x15, the captured value (see below)
 *   UNMIRRORED (see s_unmirrored below): original callees the chain reaches
 *   with no port code at their original position. Their bindings return
 *   without effect and are reported once on stderr, so a trace that shows
 *   the call is never mistaken for the port doing it.
 * Not reached in legacy mode, therefore NULL: the load arms (001AD1A0,
 * 001AD230, 001AD360's and 001ADF50's callees), the game-over arms, the
 * status/unported classifier arms (states 2, 3, 5, 6) and state 4. The
 * canonical request block has no port writer until S11b/S12b, so B8/B9 stay
 * 0 and 001AD140/001AD010 are never entered; if they were, their
 * 001FC9B0/001FBC50/001FABB0 calls would reach NULL 001FBC50/001FABB0 and
 * fault.
 */
#include "game/em_scene_bindings.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game/em_actor_pool.h"
#include "game/em_actor_roster.h"
#include "game/em_area11_bindings.h"
#include "game/em_game.h"
#include "game/em_frame.h"
#include "game/em_frame_trace.h"
#include "game/em_game_internal.h"
#include "game/em_scene_classify.h"
#include "game/em_scene_frame.h"
#include "game/em_scene_task.h"
#include "game/em_scene_workers.h"

/* ------------------------------------------------------------ storage */

static EmSceneState s_state;
static EmSceneWorkers s_workers;
static int s_ready;

/* The actor pool (design 3.1: owned here) and the AREA11 roster
 * (assets/scene_snow/roster.emro, written by tools/export_area11_roster.py
 * from the user's own ELF and overlay; loaded once). */
static EmActorPool s_pool;
static EmActorRoster s_roster;
static int s_roster_loaded;
#define AREA11_ROSTER_PATH AREA11_SCENE_DIR "/roster.emro"

/* What 001B6990 spawned at the last state 0 (S10b). */
enum { POOL_NONE = 0, POOL_ROSTER, POOL_LEGACY_WORLD };
static int s_pool_mode = POOL_NONE;

/* 001AFCA0's 001AF5C0 wipe leaves the player's mode byte +4 at 0, so the
 * next 0015BCF0 takes 0015BA50 case 0 -> 0015C420 (the player init that
 * spawns the attached-equipment and 001E2560 children). */
static int s_player_init_pending;

/* Legacy-mode flag (bindings only; see em_scene_bindings.h). */
static const int s_classifier_shadow = 1; /* retired by S11a/S11b */

/* The slot-0 record's user bytes for the tick in progress. */
static uint8_t *s_user;
static int s_fault_reported;
static int s_q1_reported;

/* Original data addresses the variants pass as actor handles. */
enum {
    D_PLAYER = EM_SCENE_D_008102B0, /* 001CB590 / 0015BCF0 argument */
    D_CAMERA = EM_SCENE_D_008101E0, /* 001CB590 / 0018B9C0 argument */
};

/* The world-frame variant in progress (the stage workers run only inside
 * one; outside, they fault). */
enum { VARIANT_NONE = -1, VARIANT_GAMEPLAY = 0, VARIANT_CUTSCENE = 1 };
static int s_variant = VARIANT_NONE;

/* D_00275B44 / D_00275B48: 001CB590 stores its a0 in both (byte-matched
 * src/func_001CB590.c); the variants read D_00275B44 back as the argument
 * of 0015BCF0 and 0018B9C0. The handle is the original address. */
static uint32_t s_current_actor;

EmSceneState *em_scene_state(void)
{
    return &s_state;
}

/* ------------------------------------------------------ unmirrored workers */

/* Original callees the chain reaches in legacy mode with no port code at
 * their original position. Each entry says what the port does instead. */
enum {
    UM_001FC9B0,
    UM_001B07C0,
    UM_001B6990_LEGACY_WORLD,
    UM_001D19E0,
    UM_001C1DC0,
    UM_00199C50,
    UM_001AEE40,
    UM_001FAE70,
    UM_001C5C50_LEGACY_WORLD,
    UM_001D1EF0,
    UM_001CB590,
    UM_0015BCF0_CUTSCENE,
    UM_0015C160,
    UM_001F0360,
    UM_001AAD00,
    UM_COUNT
};

static const struct {
    uint32_t address;
    const char *port; /* where the port does (or does not do) this today */
} s_unmirrored[UM_COUNT] = {
    [UM_001FC9B0] = {0x001FC9B0u, "001AFCF0 callee; no port counterpart"},
    [UM_001B07C0] = {0x001B07C0u, "spawn placement; the manifest spawn inside the legacy state-0 re-arm"},
    [UM_001B6990_LEGACY_WORLD] = {0x001B6990u, "scene without an original roster: one legacy_world "
                                               "pool node runs the S10a legacy block"},
    [UM_001D19E0] = {0x001D19E0u, "no port counterpart"},
    [UM_001C1DC0] = {0x001C1DC0u, "its 001D2830 registrations and 001C1E70..001C1F50 passes have no "
                                  "port counterpart; only the AREA11 roster pool gets the 001C1EA0 "
                                  "weather node (interim, em_area11_bindings.c)"},
    [UM_00199C50] = {0x00199C50u, "no port counterpart"},
    [UM_001AEE40] = {0x001AEE40u, "area-entry flash fade(4); not mirrored"},
    [UM_001FAE70] = {0x001FAE70u, "area music cue; not mirrored (game_load_task note)"},
    [UM_001C5C50_LEGACY_WORLD] = {0x001C5C50u, "scene without an original roster: no area-title "
                                               "node (legacy em_hud area title)"},
    [UM_001D1EF0] = {0x001D1EF0u, "no port counterpart"},
    [UM_001CB590] = {0x001CB590u, "current actor stored; its anim_bone_array_setup tail has no port "
                                  "counterpart (the port's bone palettes are per model)"},
    [UM_0015BCF0_CUTSCENE] = {0x0015BCF0u, "001AE6B0 player stage; the port poses the player through "
                                           "the opening runtime in the 001AFD70 block (design risk 2)"},
    [UM_0015C160] = {0x0015C160u, "player post-step (001DA6A0 or 0015BF90, then the +0x4C draw method); "
                                  "the port draws the player from its draw list"},
    [UM_001F0360] = {0x001F0360u, "effect-manager barrel (001F6210 .. 001F0720); no port counterpart"},
    [UM_001AAD00] = {0x001AAD00u, "nine end-of-frame hooks and the class-list swap; no port counterpart"},
};

static uint32_t s_unmirrored_seen;     /* reached at least once */
static uint32_t s_unmirrored_reported; /* already on stderr */

static int unmirrored(int index)
{
    s_unmirrored_seen |= 1u << index;
    return 0;
}

/* One stderr line per newly reached set, after the tick. */
static void report_unmirrored(void)
{
    uint32_t fresh = s_unmirrored_seen & ~s_unmirrored_reported;
    if (!fresh)
        return;
    fprintf(stderr, "em_scene: legacy mode: reached without port code:");
    for (int i = 0; i < UM_COUNT; ++i)
        if (fresh & (1u << i))
            fprintf(stderr, " %08X (%s);", (unsigned)s_unmirrored[i].address,
                    s_unmirrored[i].port);
    fputc('\n', stderr);
    s_unmirrored_reported |= fresh;
}

static int um_001FC9B0(void *ctx) { (void)ctx; return unmirrored(UM_001FC9B0); }
static int um_001B07C0(void *ctx, int a0) { (void)ctx; (void)a0; return unmirrored(UM_001B07C0); }
static int um_001D19E0(void *ctx) { (void)ctx; return unmirrored(UM_001D19E0); }
static int um_00199C50(void *ctx) { (void)ctx; return unmirrored(UM_00199C50); }
static int um_001AEE40(void *ctx, int16_t a0) { (void)ctx; (void)a0; return unmirrored(UM_001AEE40); }
static int um_001FAE70(void *ctx, int a0) { (void)ctx; (void)a0; return unmirrored(UM_001FAE70); }
static int um_001D1EF0(void *ctx) { (void)ctx; return unmirrored(UM_001D1EF0); }

/* ------------------------------------------------------------ readers */

static int16_t r_0028A9A0(void *ctx)
{
    (void)ctx;
    return em_frame_transition()->substate;
}

static uint32_t r_00275B44(void *ctx)
{
    (void)ctx;
    return s_current_actor;
}

/* D_008102B9, the player's +9 byte, which both variants pass to
 * 001CB590(player, 0x320, +9) as the anim_bone_array_setup count. The port
 * has no player record at 0x8102B0; 0x15 is the value in all three
 * captured RAM images (build/startup-reference opening_ee.bin,
 * handoff_ee.bin, playable_ee.bin). The port does not act on it: it only
 * reaches the trace and the unmirrored 001CB590 tail. */
static uint8_t r_008102B9(void *ctx)
{
    (void)ctx;
    return 0x15;
}

/* ------------------------------------------------------------ trace */

/* The classifier result recorded for this tick (design 10.1: once per
 * classifier run). Shadow mode: the canonical state with the input words
 * taken from the step-C pad block in the original layout, through the same
 * Q1 entry the core uses. Recorded only; the core acts on the canonical state. */
static int32_t shadow_classifier(void)
{
    EmSceneState view = s_state;
    if (s_classifier_shadow) {
        const EmPadUnpack *pad = em_frame_pad_block();
        view.d810E74 = pad->pressed;
        view.d810E70 = pad->held;
    }
    return em_scene_classify_q1(&view, em_frame_transition()->substate, NULL);
}

static void bindings_trace(void *ctx, uint32_t caller, uint32_t callee, uint32_t a0,
                           uint32_t a1, uint32_t a2, uint32_t a3)
{
    em_frame_trace_env_hook(ctx, caller, callee, a0, a1, a2, a3);
    if (caller == 0x001AE040u && callee == 0x001AE7E0u) {
        EmFrameTrace *t = em_frame_trace_env();
        if (t)
            em_frame_trace_classifier(t, shadow_classifier());
    }
}

/* -------------------------------------------------- core-backed workers */

static int w_001AFCF0(void *ctx)
{
    (void)ctx;
    return em_sf_001AFCF0(&s_state, &s_workers);
}

static int w_001AD140(void *ctx)
{
    (void)ctx;
    return em_sf_001AD140(&s_state, s_user, &s_workers);
}

static int w_001AD010(void *ctx)
{
    (void)ctx;
    return em_sf_001AD010(&s_state, s_user, &s_workers);
}

/* -------------------------------------------------------- legacy workers */

/* 0x1AE040 state 0, first callee. 001AFCA0 is 001AF5C0 (player wipe),
 * 001AF690, 001AF710, 001AF8E0 (pool reset), 001D0660, then spad 31F4 = 0
 * (design 2.3). The port's native re-arm stands in for the player wipe and
 * for the unmirrored state-0 callees (spawn, camera init, self-test
 * fixtures); since S10b the pool half of 001AF8E0 runs here (its class-list
 * half, D_00275B54..BB8, belongs to the unported 001AAD00 owner, design
 * 10.1). The pool reset calls every live node's release hook. */
static int w_001AFCA0(void *ctx)
{
    (void)ctx;
    em_game_legacy_state0();
    s_player_init_pending = 1; /* the 001AF5C0 wipe: player +4 = 0 */
    em_actor_pool_reset_001AF8E0(&s_pool);
    em_area11_bindings_reset();
    s_pool_mode = POOL_NONE;
    s_state.spad31F4 = 0;
    return 0;
}

/* The scene the pool is built for: the AREA11 roster when the loaded scene
 * is AREA11 (canonical D_00810700 == 0x0B, committed by the legacy load's
 * 001AD360 stand-in), otherwise a scene the original roster does not
 * describe (office, drawbridge). */
static int roster_scene(void)
{
    return strcmp(g.scene_dir, AREA11_SCENE_DIR) == 0;
}

/* 001AD360 step 4 (design 10.1): 700 = 0x0B, 701 = 702 = 0, D_00810730[0x0B]
 * = 0. The legacy load and the legacy Continue restart stand in for the
 * route that reaches it (both commit AREA11 sub 0 entry 0). */
static void legacy_commit_area11(void)
{
    s_state.d810700 = 0x0B;
    s_state.d810701 = 0;
    s_state.d810702 = 0;
    s_state.d810730[0x0B] = 0;
}

/* 001B6990 (with 001B6910 first). AREA11: the exported roster through the
 * S7 spawner, every node bound by em_area11_bindings. Any other scene: one
 * legacy_world node (design 4.4, "non-roster scenes"). */
static int w_001B6990(void *ctx)
{
    (void)ctx;
    if (!roster_scene()) {
        s_pool_mode = POOL_LEGACY_WORLD;
        unmirrored(UM_001B6990_LEGACY_WORLD);
        return em_area11_spawn_legacy_world();
    }
    if (s_state.d810700 != 0x0B)
        return em_scene_fault(&s_state, 0x001B6990u, EM_SCENE_FAULT_BAD_INDEX);
    if (!s_roster_loaded) {
        if (em_actor_roster_load(&s_roster, AREA11_ROSTER_PATH) != 0) {
            fprintf(stderr, "em_scene: 001B6990: %s missing or malformed (run "
                    "tools/export_area11_roster.py)\n", AREA11_ROSTER_PATH);
            return em_scene_fault(&s_state, 0x001B6990u, EM_SCENE_FAULT_NULL_WORKER);
        }
        s_roster_loaded = 1;
    }
    /* Of the progress view 001B6660 reads, only D_00810788 and
     * D_00810860..B5F are canonical yet (em_scene_state.h, D2); condition
     * ids 2..6 would read D_00810758[i], D_008107D8[i] or D_00810778, which
     * port mirrors still own. Refuse such a roster rather than read a copy. */
    for (uint32_t gi = 0; gi < s_roster.group_count; ++gi)
        for (uint32_t i = 0; i < s_roster.groups[gi].count; ++i) {
            const uint8_t *rec = s_roster.groups[gi].records + (size_t)i * EM_ROSTER_DEFERRED_RECORD_SIZE;
            int16_t id = (int16_t)(rec[0] | rec[1] << 8);
            if (id >= 2 && id <= 6) {
                fprintf(stderr, "em_scene: 001B6990: deferred g%u.%u condition %d reads progress "
                        "bytes that are not canonical yet (D2)\n", (unsigned)gi, (unsigned)i, (int)id);
                return em_scene_fault(&s_state, 0x001B6660u, EM_SCENE_FAULT_BAD_INDEX);
            }
        }
    s_pool_mode = POOL_ROSTER;
    return em_actor_roster_spawn_001B6990(&s_roster, &s_pool, &s_state,
                                          (EmActorRosterProgress *)em_scene_progress_spawn_view(&s_state),
                                          em_area11_bind_roster, NULL, NULL);
}

/* 001C1DC0. AREA11: its 001C1EA0 pass spawns the weather node (interim,
 * em_area11_bindings.c); the rest of it has no port counterpart. */
static int w_001C1DC0(void *ctx)
{
    (void)ctx;
    unmirrored(UM_001C1DC0);
    if (s_pool_mode != POOL_ROSTER)
        return 0;
    return em_area11_spawn_weather_001C1EA0();
}

/* 001C5C50: the area-title node (byte-matched; em_actor_roster). */
static int w_001C5C50(void *ctx)
{
    (void)ctx;
    if (s_pool_mode != POOL_ROSTER)
        return unmirrored(UM_001C5C50_LEGACY_WORLD);
    return em_actor_roster_spawn_001C5C50(&s_pool, &s_state, em_area11_bind_roster, NULL, NULL);
}

/* ------------------------------------------- world-frame variants (S10a) */

/* 0x1AE040 state 1, 3B8D == 0 (call site 0x1AE2A4). */
static int w_001AE5E0(void *ctx)
{
    (void)ctx;
    if (em_game_legacy_variant_head(0))
        return 0; /* status/game-over frozen frame ran (retired by S11b) */
    s_variant = VARIANT_GAMEPLAY;
    int rc = em_sf_001AE5E0(&s_state, &s_workers);
    s_variant = VARIANT_NONE;
    return rc;
}

/* 0x1AE040 state 1, 3B8D != 0 (call site 0x1AE2B4). */
static int w_001AE6B0(void *ctx)
{
    (void)ctx;
    (void)em_game_legacy_variant_head(1); /* never a frozen frame */
    s_variant = VARIANT_CUTSCENE;
    int rc = em_sf_001AE6B0(&s_state, &s_workers);
    s_variant = VARIANT_NONE;
    return rc;
}

/* ------------------------------------------- world-frame stage workers
 *
 * The positions of design 2.4 / 4.5. A negative return makes the core latch
 * EM_SCENE_FAULT_WORKER_FAILED at the callee: every worker refuses a call
 * outside a variant and any argument other than the one the variants pass,
 * because the port code behind it represents only that call.
 *
 *   001CB590(a0, ...)  both   D_00275B44 = a0; tail unmirrored
 *   0015BCF0(player)   5E0    em_player_0015BCF0
 *                      6B0    unmirrored (opening-player path, design risk 2)
 *   001CB5A0           both   empty leaf (src/func_001CB5A0.c)
 *   001D1C50           both   em_render_001D1C50
 *   001C1D00(0x8101D0) both   em_render_001C1D00
 *   001AFD70(mode)     both   em_actor_pool_walk_001AFD70 (S10b): mode 0
 *                             in 5E0, modes 1 and 2 in 6B0
 *   0015C160           both   unmirrored
 *   001F0360           both   unmirrored
 *   0018B9C0(camera)   5E0    em_camera_0018B9C0
 *                      6B0    em_camera_0018B9C0_opening
 *   001AAD00           both   unmirrored
 *   001D1EA0(1)        both   em_render_001D1EA0(1)
 */

static int in_variant(void)
{
    return s_variant != VARIANT_NONE;
}

static int w_001CB590(void *ctx, uint32_t a0, int a1, int a2, int a3)
{
    (void)ctx;
    (void)a1;
    (void)a2;
    (void)a3;
    if (!in_variant() || (a0 != D_PLAYER && a0 != D_CAMERA))
        return -1;
    s_current_actor = a0;
    return unmirrored(UM_001CB590);
}

static int w_0015BCF0(void *ctx, uint32_t actor)
{
    (void)ctx;
    if (actor != D_PLAYER)
        return -1;
    if (s_variant == VARIANT_GAMEPLAY) {
        /* 0015BCF0 -> 0015BA50 (0x15BD14) -> 0015C420 on the first call
         * after the state-0 wipe: its pool children (AREA11 roster pool
         * only; a legacy_world scene keeps its legacy frame). */
        if (s_player_init_pending) {
            s_player_init_pending = 0;
            if (s_pool_mode == POOL_ROSTER && em_area11_spawn_player_children_0015C420() < 0)
                return -1;
        }
        return em_player_0015BCF0();
    }
    if (s_variant == VARIANT_CUTSCENE) {
        /* No capture shows 0015C420 reached from 001AE6B0; the port does
         * not guess its children there. */
        if (s_player_init_pending && s_pool_mode == POOL_ROSTER)
            return em_scene_fault(&s_state, 0x0015C420u, EM_SCENE_FAULT_NULL_WORKER);
        return unmirrored(UM_0015BCF0_CUTSCENE);
    }
    return -1;
}

static int w_001CB5A0(void *ctx)
{
    (void)ctx;
    return in_variant() ? 0 : -1;
}

static int w_001D1C50(void *ctx)
{
    (void)ctx;
    return in_variant() ? em_render_001D1C50() : -1;
}

static int w_001C1D00(void *ctx, uint32_t a0)
{
    (void)ctx;
    if (!in_variant() || a0 != EM_SCENE_D_008101D0)
        return -1;
    return em_render_001C1D00();
}

/* The walk's trace hook (design 10.1): per ticked node, the 001CB590(node,
 * 0x2F0, +9) call and then the node event with the original tracer's record
 * tag and the binding name. */
static void pool_trace(void *ctx, uint32_t caller, uint32_t callee, uint32_t address,
                       const EmActor *actor)
{
    (void)ctx;
    EmFrameTrace *t = em_frame_trace_env();
    if (!t)
        return;
    if (callee == EM_ACTOR_FN_001CB590)
        em_frame_trace_call(t, caller, callee, address, EM_ACTOR_RECORD_SIZE, actor->bones, 0);
    else
        em_frame_trace_node(t, callee, actor->cls, em_area11_node_record(actor),
                            em_area11_node_binding(actor));
}

/* 001AFD70(mode): the pool walk (S10b). 001AE5E0 walks mode 0; 001AE6B0
 * walks mode 1 (all but class 1) and mode 2 (class 1 only). Around an
 * AREA11 roster walk run the port-only pieces that have no pool owner (the
 * collision-registry clears first; the draw-list collector and, in
 * gameplay, the legacy player residue last: em_area11_walk_begin/end). A
 * legacy_world pool holds one node that runs the whole S10a block itself. */
static int walk_001AFD70(void *ctx, int mode)
{
    (void)ctx;
    int ok = (s_variant == VARIANT_GAMEPLAY && mode == 0) ||
             (s_variant == VARIANT_CUTSCENE && (mode == 1 || mode == 2));
    if (!ok)
        return -1;
    EmArea11World world = {.cutscene = s_variant == VARIANT_CUTSCENE};
    int roster = s_pool_mode == POOL_ROSTER;
    if (roster)
        em_area11_walk_begin(mode);
    if (em_actor_pool_walk_001AFD70(&s_pool, &s_state, mode, &world,
                                    em_frame_trace_env() ? pool_trace : NULL, NULL) < 0)
        return -1;
    /* 001CB590 per node left D_00275B44 = the last ticked node. */
    if (s_pool.current)
        s_current_actor = em_actor_pool_address(&s_pool, s_pool.current);
    if (roster)
        em_area11_walk_end(mode);
    return 0;
}

static int w_0015C160(void *ctx)
{
    (void)ctx;
    return in_variant() ? unmirrored(UM_0015C160) : -1;
}

static int w_001F0360(void *ctx)
{
    (void)ctx;
    return in_variant() ? unmirrored(UM_001F0360) : -1;
}

static int w_0018B9C0(void *ctx, uint32_t actor)
{
    (void)ctx;
    if (actor != D_CAMERA)
        return -1;
    if (s_variant == VARIANT_GAMEPLAY)
        return em_camera_0018B9C0();
    if (s_variant == VARIANT_CUTSCENE)
        return em_camera_0018B9C0_opening();
    return -1;
}

static int w_001AAD00(void *ctx)
{
    (void)ctx;
    return in_variant() ? unmirrored(UM_001AAD00) : -1;
}

static int w_001D1EA0(void *ctx, int a0)
{
    (void)ctx;
    if (!in_variant() || a0 != 1)
        return -1;
    return em_render_001D1EA0(1);
}

/* ------------------------------------------------------ frame machine */

static int frame_machine(void)
{
    int select_withheld = 0;
    int rc = em_sf_001AE040_q1(&s_state, s_user, &s_workers, &select_withheld);
    if (select_withheld && !s_q1_reported) {
        s_q1_reported = 1;
        fprintf(stderr, "em_scene: %s\n", EM_SCENE_Q1_UNPORTED_MESSAGE);
    }
    return rc;
}

/* 001AD4D0: the jump into 0x1AE040. */
static int w_001AD4D0(void *ctx)
{
    (void)ctx;
    uint8_t *frame_state = em_scene_task_byte(s_user, EM_SCENE_TASK_0B);
    if (!frame_state)
        return em_scene_fault(&s_state, 0x001AD4D0u, EM_SCENE_FAULT_BAD_INDEX);
    /* Legacy Continue (retired by S11b): the port services the game-over
     * restart here, where its frame machine did, and rebuilds from state 0. */
    if (*frame_state == 1) {
        int restart = em_game_legacy_continue_restart();
        if (restart < 0)
            return 0; /* quit already requested; no frame, as before S8 */
        if (restart > 0) {
            *frame_state = 0;
            legacy_commit_area11(); /* the restart area is AREA11.0.0 */
        }
    }
    /* S10a interim, retired by S11a: publish the port's selector into
     * canonical 3B8D (see the file comment). */
    s_state.spad3B8D = g.frame_selector;
    EmFrameTrace *t = em_frame_trace_env();
    if (t)
        em_frame_trace_frame_state(t, s_user, s_state.spad3B8D,
                                   em_frame_transition()->substate);
    /* One 0x1AE040 run per tick. State 0 returns after its eleven calls
     * (0x1AE0DC b .L001AE5CC), so a rebuild tick draws no world frame. */
    if (frame_machine() < 0)
        return -1;
    /* Shadow mode: nothing may act on the classifier. With no writer of the
     * canonical request block or input words, 001AE7E0 returns 0 and state 1
     * stays 1; anything else is a bindings defect. */
    if (s_classifier_shadow && *frame_state != 1)
        return em_scene_fault(&s_state, 0x001AE7E0u, EM_SCENE_FAULT_BAD_RESULT);
    return 0;
}

/* ------------------------------------------------------------ setup */

static void bindings_init(void)
{
    if (s_ready)
        return;
    s_ready = 1;
    /* D_00810E50 = 4 in every original capture (design 10.2 Q8); written
     * here until S11a's input translation owns it. 001AE7E0 returns 1
     * (unported 0022A650) for any other value. */
    s_state.d810E50 = 4;
    em_actor_pool_reset_001AF8E0(&s_pool);
    em_area11_bindings_attach(&s_pool, &s_state);

    EmSceneWorkers *w = &s_workers;
    w->ctx = NULL;
    w->trace = em_frame_trace_env() ? bindings_trace : NULL;
    w->r_0028A9A0 = r_0028A9A0;
    w->r_00275B44 = r_00275B44;
    w->r_008102B9 = r_008102B9;

    w->w_001AD4D0 = w_001AD4D0;
    w->w_001AFCA0 = w_001AFCA0;
    w->w_001AFCF0 = w_001AFCF0;
    w->w_001AD140 = w_001AD140;
    w->w_001AD010 = w_001AD010;
    w->w_001AE5E0 = w_001AE5E0;
    w->w_001AE6B0 = w_001AE6B0;

    w->w_001CB590 = w_001CB590;
    w->w_0015BCF0 = w_0015BCF0;
    w->w_001CB5A0 = w_001CB5A0;
    w->w_001D1C50 = w_001D1C50;
    w->w_001C1D00 = w_001C1D00;
    w->walk_001AFD70 = walk_001AFD70;
    w->w_0015C160 = w_0015C160;
    w->w_001F0360 = w_001F0360;
    w->w_0018B9C0 = w_0018B9C0;
    w->w_001AAD00 = w_001AAD00;
    w->w_001D1EA0 = w_001D1EA0;

    w->w_001FC9B0 = um_001FC9B0;
    w->w_001B07C0 = um_001B07C0;
    w->w_001B6990 = w_001B6990;
    w->w_001D19E0 = um_001D19E0;
    w->w_001C1DC0 = w_001C1DC0;
    w->w_00199C50 = um_00199C50;
    w->w_001AEE40 = um_001AEE40;
    w->w_001FAE70 = um_001FAE70;
    w->w_001C5C50 = w_001C5C50;
    w->w_001D1EF0 = um_001D1EF0;
}

void em_scene_bindings_legacy_loaded(EmTask *record)
{
    if (!record)
        return;
    bindings_init();
    if (roster_scene())
        legacy_commit_area11();
    uint8_t *user = record->user;
    *em_scene_task_byte(user, EM_SCENE_TASK_08) = 3;
    *em_scene_task_byte(user, EM_SCENE_TASK_09) = 1;
    *em_scene_task_byte(user, EM_SCENE_TASK_0A) = 0;
    *em_scene_task_byte(user, EM_SCENE_TASK_0B) = 0;
}

/* ------------------------------------------------------------ the task */

void em_scene_task_001ACEC0(void)
{
    bindings_init();
    if (em_scene_faulted(&s_state))
        return; /* fail-stop: the task does nothing after a fault */
    EmTask *self = em_task_current();
    s_user = self ? self->user : NULL;
    EmFrameTrace *t = em_frame_trace_env();
    if (t)
        em_frame_trace_tick_begin(t, em_frame_counter());
    int rc = em_sf_001ACEC0(&s_state, s_user, &s_workers);
    /* Step D of the next main-loop iteration reads 3B90 and C4 (design 2.1). */
    em_frame_screen_fade_gate(s_state.spad3B90, s_state.req[EM_SCENE_REQ_C4]);
    if (t)
        em_frame_trace_tick_end(t);
    report_unmirrored();
    s_user = NULL;
    if (rc < 0 && !s_fault_reported) {
        s_fault_reported = 1;
        fprintf(stderr,
                "em_scene: FAULT at %08X (code %d); the game task is stopped (fail-stop)\n",
                (unsigned)s_state.fault.address, (int)s_state.fault.code);
    }
}
