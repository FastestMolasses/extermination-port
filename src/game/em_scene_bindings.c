/* Scene coordinator bindings, steps S8-S11b. See em_scene_bindings.h and
 * docs/SCENE_COORDINATOR_DESIGN.md sections 3.1, 5, 6 (S8 .. S11b) and 10.
 *
 * What is live after S11b:
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
 *   - S11a: spad 3B8D and 3B91 have one storage, s_state. The port's
 *     writers are the ported counterparts of the original writers (the
 *     opening runtime's 001B82D0 op-12/op-5 counterpart, 001AFCF0, and
 *     001AE6B0's 3B91 1 -> 2 promotion); they write it where the original
 *     does, inside the world frame's 001AFD70 walk, so a selector written
 *     during frame N picks frame N+1's variant. The input words D_00810E74,
 *     D_00810E70 and D_00810E50 are written at the start of every tick by
 *     em_frame_scene_input (em_frame.c), the one translation of step C;
 *     spad 3B92 is canonical too (lead decision D5; em_opening_runtime.c
 *     writes it where 001B82D0 does);
 *   - S11b: the frame machine acts on 001AE7E0. r == 2 (a START/TRIANGLE
 *     edge in gameplay, or a B0/C5 request) opens the status screen:
 *     states 3 and 5 run with the world frozen (see "status screen"
 *     below). B9, written by the player stage (0015CF90), leads at fade
 *     substate 2 to 001AD140 -> 001AD4E0 -> 001ADF00, which replaces this
 *     task with the interim 001AC070 (see "game over" below). The
 *     unported arms (r == 1: 0022A650/001FB9F0, reachable only through
 *     SELECT, withheld under Q1, or E50 != 4, never written; r == 3 /
 *     state 6: 001FF030/001FEFE0, reachable only through CE, never written;
 *     +9 = 3: 001AD740, reachable only through 3B93, never written in
 *     AREA11) fault at their NULL workers;
 *   - spad 3B90 (001ACEC0 writes 2 every tick) and C4 are forwarded to the
 *     step-D letterbox gate at the end of every task tick (design 2.1);
 *   - the S6 trace contract (design 10.1) when EM_FRAME_TRACE is set.
 *
 * Worker bindings. Every worker the chain reaches in legacy mode is bound;
 * every other worker is NULL and faults when reached (fail-stop):
 *   w_001AD4D0            -> frame machine entry (trace frame state,
 *                            0x1AE040)
 *   w_001AE5E0/w_001AE6B0 -> the variant cores, after the legacy head
 *                            (em_game_legacy_variant_head: test hooks)
 *   variant stage workers -> see "world-frame stage workers" below. They
 *                            fault outside a variant, except 001D1C50 and
 *                            001D1EA0(0) in status state 3.
 *   status / game-over    -> see those sections below.
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
 * Not bound until later steps, therefore NULL: the load arms (001AD1A0,
 * 001AD230, 001AD360's and 001ADF50's remaining callees), the unported
 * classifier arms (r == 1 and state 2, r == 3 and state 6), +9 = 3
 * (001AD740) and state 4 (0018AB00, 0018D7B0; S12b). B8 has no port
 * writer until S12b, so 001AD010 is never entered.
 */
#include "game/em_scene_bindings.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game/em_actor_pool.h"
#include "game/em_actor_roster.h"
#include "game/em_area11_bindings.h"
#include "game/em_camera.h"
#include "game/em_game.h"
#include "game/em_frame.h"
#include "game/em_frame_trace.h"
#include "game/em_game_internal.h"
#include "game/em_hud.h"
#include "game/em_scene_classify.h"
#include "game/em_scene_frame.h"
#include "game/em_scene_task.h"
#include "game/em_sfx.h"
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

/* The frame-machine state byte +B as 0x1AE040 was entered this tick
 * (w_001AD4D0), or -1 outside it. Workers that the original reaches from
 * several states use it to refuse, or to tell apart, a call (status state
 * 3/5 versus the world variants; state 0 versus state 5). */
static int s_entry_state = -1;

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
    UM_001FABB0,
    UM_00119828,
    UM_001D2830,
    UM_001E0CC0,
    UM_001D2880,
    UM_001FA790,
    UM_001FAB50,
    UM_00810D38,
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
    [UM_001AEE40] = {0x001AEE40u, "state-0 area-entry flash fade(4); not mirrored (the state-5 "
                                  "call is em_frame_fade_flash)"},
    [UM_001FAE70] = {0x001FAE70u, "area music cue (state 0 area entry, state 5 status close); "
                                  "not mirrored (game_load_task note; H22, WP-5)"},
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
    [UM_001FABB0] = {0x001FABB0u, "stream stop (status open, game over); the port's music keeps "
                                  "playing (H22, WP-5)"},
    [UM_00119828] = {0x00119828u, "SPU stream-channel volume (status open); the port's stream "
                                  "player has no per-channel gain"},
    [UM_001D2830] = {0x001D2830u, "display-list context registration; no port counterpart"},
    [UM_001E0CC0] = {0x001E0CC0u, "status-close draw-mode reset; no port counterpart"},
    [UM_001D2880] = {0x001D2880u, "game-over display-list reset; no port counterpart"},
    [UM_001FA790] = {0x001FA790u, "game-over stream cue 0x1B; the cue is not exported"},
    [UM_001FAB50] = {0x001FAB50u, "music channel release (game over); not mirrored (H22, WP-5)"},
    [UM_00810D38] = {0x00810D38u, "001ADF00's current-BGM word store; the port has no D_00810D38"},
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
static int um_001FAE70(void *ctx, int a0) { (void)ctx; (void)a0; return unmirrored(UM_001FAE70); }
static int um_001D1EF0(void *ctx) { (void)ctx; return unmirrored(UM_001D1EF0); }
static int um_001FABB0(void *ctx) { (void)ctx; return unmirrored(UM_001FABB0); }
static int um_00119828(void *ctx, int a0, int a1, int a2)
{
    (void)ctx;
    (void)a0;
    (void)a1;
    (void)a2;
    return unmirrored(UM_00119828);
}
static int um_001D2830(void *ctx, int a0, int a1) { (void)ctx; (void)a0; (void)a1; return unmirrored(UM_001D2830); }
static int um_001E0CC0(void *ctx) { (void)ctx; return unmirrored(UM_001E0CC0); }
static int um_001D2880(void *ctx) { (void)ctx; return unmirrored(UM_001D2880); }
static int um_001FA790(void *ctx, int a0, int a1) { (void)ctx; (void)a0; (void)a1; return unmirrored(UM_001FA790); }
static int um_001FAB50(void *ctx) { (void)ctx; return unmirrored(UM_001FAB50); }
static int um_s_00810D38(void *ctx, int32_t value) { (void)ctx; (void)value; return unmirrored(UM_00810D38); }

/* ------------------------------------------------------------ readers */

static int16_t r_0028A9A0(void *ctx)
{
    (void)ctx;
    return em_frame_transition()->substate;
}

/* D_00282157: the phase byte of 001FA0D0's asynchronous disc-read
 * sequencer (src/func_001FA0D0.c; 001FABB0 clears it). It is nonzero only
 * while a read is in flight between frames. The port's readers complete
 * inside the call that starts them, so no read is ever in flight at a tick
 * boundary: phase 0. Read by 0x1AE040 state 3 sub-step 0 (and state 6). */
static uint8_t r_00282157(void *ctx)
{
    (void)ctx;
    return 0;
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
 * classifier run): 001AE7E0 over the canonical state, through the same Q1
 * entry the core uses, so it is the result the core acts on. Since S11a the
 * canonical input words are written every tick (em_frame_scene_input), so
 * no pad-block substitution is needed. */
static int32_t traced_classifier(void)
{
    return em_scene_classify_q1(&s_state, em_frame_transition()->substate, NULL);
}

static void bindings_trace(void *ctx, uint32_t caller, uint32_t callee, uint32_t a0,
                           uint32_t a1, uint32_t a2, uint32_t a3)
{
    em_frame_trace_env_hook(ctx, caller, callee, a0, a1, a2, a3);
    if (caller == 0x001AE040u && callee == 0x001AE7E0u) {
        EmFrameTrace *t = em_frame_trace_env();
        if (t)
            em_frame_trace_classifier(t, traced_classifier());
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
    em_game_legacy_variant_head(0);
    s_variant = VARIANT_GAMEPLAY;
    int rc = em_sf_001AE5E0(&s_state, &s_workers);
    s_variant = VARIANT_NONE;
    return rc;
}

/* 0x1AE040 state 1, 3B8D != 0 (call site 0x1AE2B4). */
static int w_001AE6B0(void *ctx)
{
    (void)ctx;
    em_game_legacy_variant_head(1);
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

/* Status state 3 sub-step 1 (0x1AE460): the same function takes its
 * D_008106C4 != 0 path (001D2830(6,0) instead of the world-light setup),
 * but still ends with 001D7C30, the point-light tick the port runs here. */
static int in_status_frame(void)
{
    return s_entry_state == 3;
}

static int w_001D1C50(void *ctx)
{
    (void)ctx;
    return in_variant() || in_status_frame() ? em_render_001D1C50() : -1;
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

/* 001D1EA0(1) ends both variants; status state 3 ends with 001D1EA0(0)
 * (0x1AE4B4), the frozen-world frame. */
static int w_001D1EA0(void *ctx, int a0)
{
    (void)ctx;
    if (in_variant() && a0 == 1)
        return em_render_001D1EA0(1);
    if (in_status_frame() && a0 == 0)
        return em_render_001D1EA0(0);
    return -1;
}

/* ------------------------------------ status screen (S11b; design 5)
 *
 * 001AE7E0 r == 2 in state 1 (0x1AE1CC): 0020E060, C4 = 1, +B = 3,
 * 001FBC50, 001FABB0, 00119828(0/1, 0x3FFF, 0x3FFF). State 3 sub-step 1:
 * 001D1C50, 001D2830(3,1), 0020CDC0 and, when it returns nonzero,
 * 001E0CC0(0), +B = 5, EF = 0x46, 001AEDB0(0); always 001D1EA0(0). State
 * 5: 001AEDB0(0), 001D1EF0, 0018C0D0(camera, 1), C4 = 0, +B = 1,
 * 001FAE70(1), 001AEE40(0x20). No owner, player or camera stage runs in
 * states 3 and 5 (trace st14).
 *   0020E060  -> em_hud_status_open (interim until WP-5; em_hud.h)
 *   0020CDC0  -> em_hud_status_tick (interim until WP-5); on the close it
 *                clears canonical B0 and C5, the request bytes the status
 *                stack consumes (src/func_0020CDC0.c clears B0; C5 selects
 *                the passcode pages)
 *   001FBC50  -> em_sfx_stop_all (its translation, em_sfx.h)
 *   001AEDB0  -> em_frame_fade_full (001AEDB0's translation, em_fade.c)
 *   0018C0D0  -> camera_commit_original(&g.cam, a1) (em_camera.h)
 *   001AEE40  -> state 5: em_frame_fade_flash (em_fade.c); the state-0
 *                area-entry call stays unmirrored
 *   001FABB0, 00119828, 001D2830, 001E0CC0, 001FAE70, 001D1EF0: unmirrored
 *   (reported); the music is H22 (WP-5). */

static int w_0020E060(void *ctx)
{
    (void)ctx;
    if (s_entry_state != 1 && s_entry_state != 4)
        return -1; /* only the state-1 classifier arm calls it */
    em_hud_status_open();
    return 0;
}

static int w_0020CDC0(void *ctx)
{
    (void)ctx;
    if (!in_status_frame())
        return -1;
    int closed = em_hud_status_tick(em_frame_input());
    if (closed < 0)
        return -1;
    if (closed) {
        s_state.req[EM_SCENE_REQ_B0] = 0;
        s_state.req[EM_SCENE_REQ_C5] = 0;
    }
    return closed;
}

static int w_001FBC50(void *ctx)
{
    (void)ctx;
    em_sfx_stop_all();
    return 0;
}

static int w_001AEDB0(void *ctx, uint8_t a0)
{
    (void)ctx;
    em_frame_fade_full(a0);
    return 0;
}

static int w_0018C0D0(void *ctx, uint32_t a0, int a1)
{
    (void)ctx;
    if (a0 != D_CAMERA || s_entry_state != 5)
        return -1; /* state 4's call is not bound until S12b */
    camera_commit_original(&g.cam, a1);
    return 0;
}

static int w_001AEE40(void *ctx, int16_t a0)
{
    (void)ctx;
    if (s_entry_state == 5) {
        em_frame_fade_flash(a0);
        return 0;
    }
    return unmirrored(UM_001AEE40);
}

/* ------------------------------------------- game over (S11b; design 5)
 *
 * B9 (written by the player stage, 0015CF90) -> 0x1AE040 state 1 calls
 * 001AD140 at D_0028A9A0 == 2 (+8 = 3, +9 = 2) -> 001AD250 runs the
 * byte-matched 001AD4E0 core -> +9 = 4 -> 001ADF00 -> 001AB790(001AC070).
 * The 001AD4E0 core owns the timing (the 0xF0 hold at +0x18, the CROSS
 * skip, the fades); its workers:
 *   001FF080(0, 0x27) -> screen module 0x27: the legacy em_hud_game_over
 *                        stand-in (g.go_state = GO_SCREEN). The port has no
 *                        asynchronous module load, so the module is
 *                        resident at once and the busy byte D_00275BD8 the
 *                        core raised is cleared here (the original loader
 *                        clears it when its read completes)
 *   001ABF90(packet)  -> em_render_001ABF90: the frame, with the stand-in
 *   001AEE10, 001AEDE0 -> the translated fades (em_fade.c)
 *   001D2880, 001FA790(0, 0x1B), 001FAB50, the D_00810D38 store: unmirrored
 * 001ADF00's 001AEBA0(0xFF) is em_screen_fade_in (em_fade.c) and its
 * 001AB790(0x1AC070) replaces the game task with the interim 001AC070
 * (em_game_legacy_continue_task_001AC070: the legacy continue prompt,
 * which reinstalls this task on Continue). */

static int in_game_over(void)
{
    const uint8_t *sub = em_scene_task_byte(s_user, EM_SCENE_TASK_09);
    return sub && (*sub == 2 || *sub == 4);
}

static int w_001FF080(void *ctx, int a0, int a1)
{
    (void)ctx;
    if (a0 != 0 || a1 != 0x27 || !in_game_over())
        return -1; /* the area load (1, 0) is S12a's */
    g.go_state = GO_SCREEN;
    s_state.d275BD8 = 0;
    return 0;
}

static int w_001ABF90(void *ctx, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
    (void)ctx;
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return in_game_over() ? em_render_001ABF90() : -1;
}

static int w_001AEE10(void *ctx, int16_t a0, uint8_t a1)
{
    (void)ctx;
    em_frame_fade_start_colour(-1, a0, a1);
    return 0;
}

static int w_001AEDE0(void *ctx, int16_t a0, uint8_t a1)
{
    (void)ctx;
    em_frame_fade_start_colour(1, a0, a1);
    return 0;
}

static int w_001AEBA0(void *ctx, int16_t a0)
{
    (void)ctx;
    em_frame_screen_fade_start(-1, a0);
    return 0;
}

static int w_001AB790(void *ctx, uint32_t fn)
{
    (void)ctx;
    if (fn != EM_SCENE_FN_001AC070)
        return -1;
    return em_task_replace_current(em_game_legacy_continue_task_001AC070) ? 0 : -1;
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
    EmFrameTrace *t = em_frame_trace_env();
    if (t)
        em_frame_trace_frame_state(t, s_user, s_state.spad3B8D,
                                   em_frame_transition()->substate);
    /* One 0x1AE040 run per tick. State 0 returns after its eleven calls
     * (0x1AE0DC b .L001AE5CC), so a rebuild tick draws no world frame.
     * Since S11b the machine acts on its classifier (design 2.3, 5). */
    s_entry_state = *frame_state;
    int rc = frame_machine();
    s_entry_state = -1;
    return rc < 0 ? -1 : 0;
}

/* ------------------------------------------------------------ setup */

static void bindings_init(void)
{
    if (s_ready)
        return;
    s_ready = 1;
    em_actor_pool_reset_001AF8E0(&s_pool);
    em_area11_bindings_attach(&s_pool, &s_state);

    EmSceneWorkers *w = &s_workers;
    w->ctx = NULL;
    w->trace = em_frame_trace_env() ? bindings_trace : NULL;
    w->r_0028A9A0 = r_0028A9A0;
    w->r_00282157 = r_00282157;
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
    w->w_001AEE40 = w_001AEE40;
    w->w_001FAE70 = um_001FAE70;
    w->w_001C5C50 = w_001C5C50;
    w->w_001D1EF0 = um_001D1EF0;

    /* Status screen (S11b). */
    w->w_0020E060 = w_0020E060;
    w->w_001FBC50 = w_001FBC50;
    w->w_001FABB0 = um_001FABB0;
    w->w_00119828 = um_00119828;
    w->w_001D2830 = um_001D2830;
    w->w_0020CDC0 = w_0020CDC0;
    w->w_001E0CC0 = um_001E0CC0;
    w->w_001AEDB0 = w_001AEDB0;
    w->w_0018C0D0 = w_0018C0D0;

    /* Game over (S11b): 001AD4E0 and 001ADF00. */
    w->w_001D2880 = um_001D2880;
    w->w_001FF080 = w_001FF080;
    w->w_001AEE10 = w_001AEE10;
    w->w_001FA790 = um_001FA790;
    w->w_001ABF90 = w_001ABF90;
    w->w_001AEDE0 = w_001AEDE0;
    w->w_001FAB50 = um_001FAB50;
    w->s_00810D38 = um_s_00810D38;
    w->w_001AEBA0 = w_001AEBA0;
    w->w_001AB790 = w_001AB790;
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
    /* D_00810E74/E70/E50 as step C left them this frame (design 3.2). */
    em_frame_scene_input(&s_state);
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
