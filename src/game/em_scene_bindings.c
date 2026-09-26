/* Scene coordinator bindings, steps S8-S12a. See em_scene_bindings.h and
 * docs/SCENE_COORDINATOR_DESIGN.md sections 3.1, 5, 6 (S8 .. S12a) and 10.
 *
 * What is live after S12a:
 *   - the slot-0 task is em_scene_task_001ACEC0, which runs the S3 cores
 *     001ACEC0 -> 001AD250 and, through the w_001AD4D0 binding (the original
 *     001AD4D0 is a tail jump to 0x1AE040), the S2 frame core 0x1AE040 with
 *     the lead's Q1 entry point (em_sf_001AE040_q1);
 *   - S9: the state-0 tick returns. 0x1AE040 state 0 ends with
 *     an unconditional branch at 0x1AE0DC to the epilogue 0x1AE5CC, never falling into state 1,
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
 *   - S12a: New Game (and Continue) register this task with a cleared
 *     record, so the chain runs the original load arms: 001AD1A0 (module 3),
 *     001AD230 (the 001AF2C0 reset), 001AD360 (the intro movie at step 1)
 *     and 001ADF50 (the native area read at 001FF080(1, 0), the load veil);
 *     the state-0 rebuild places the player with 001B07C0(0) over the
 *     exported spawn table (AREA11; see "spawn placement") and flashes the
 *     transition in with 001AEE40(4); 001AD010's area change (+9 = 5) is
 *     served the same way (em_scene_request_area_change_001B0C60 is the
 *     translated request). See "the load arms" below;
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
 *                            (em_game_legacy_state0), the area's collision
 *                            world (AREA11), the pool reset 001AF8E0 with its
 *                            class-list half, then spad 31F4 = 0 (001AFCA0
 *                            stores it)
 *   w_001B6990, w_001C1DC0, w_001C5C50 -> the pool spawns (S10b, above)
 *   w_001AFCF0, w_001AD140, w_001AD010 -> the S3 cores (design 10.1)
 *   r_0028A9A0            -> em_frame_transition()->substate
 *   r_00275B44            -> the current actor stored by w_001CB590
 *   r_008102B9            -> 0x15, the captured value (see below)
 *   UNMIRRORED (see s_unmirrored below): original callees the chain reaches
 *   with no port code at their original position. Their bindings return
 *   without effect and are reported once on stderr, so a trace that shows
 *   the call is never mistaken for the port doing it.
 * Not bound until later steps, therefore NULL: the unported classifier arms
 * (r == 1 and state 2, r == 3 and state 6) and +9 = 3 (001AD740). Since S12a
 * B8 = 1 has a port writer (the 001B0C60 translation); since S12b B8 = 2 has
 * one (the AREA11 door's 001BC150, em_door.c) and state 4 is bound (see "the
 * room move" below).
 */
#include "game/em_scene_bindings.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game/em_actor_pool.h"
#include "game/em_actor_roster.h"
#include "game/em_player_closure_live.h"
#include "game/em_area11_bindings.h"
#include "game/em_effects_live.h"
#include "game/em_equipment_live.h"
#include "game/em_area11_boxes.h"
#include "game/em_area11_door.h"
#include "game/em_area11_roger.h"
#include "game/em_area11_script_host.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_camera.h"
#include "game/em_camera_live.h"
#include "game/em_render_context_live.h"
#include "game/em_owner_draw_live.h"
#include "game/em_ee_float.h"
#include "game/em_sdk_math_original.h"
#include "game/em_collision_world.h"
#include "game/em_door.h"
#include "game/em_game.h"
#include "game/em_frame.h"
#include "game/em_frame_trace.h"
#include "game/em_frontend.h"
#include "game/em_game_internal.h"
#include "game/em_hud.h"
#include "game/em_level_smoke_test.h"
#include "game/em_load_veil.h"
#include "game/em_opening_control_test.h"
#include "game/em_stream_live.h"
#include "game/em_opening_runtime.h"
#include "game/em_pickup.h"
#include "game/em_pickup_original.h"
#include "game/em_random.h"
#include "game/em_scene_classify.h"
#include "game/em_scene_frame.h"
#include "game/em_scene_task.h"
#include "game/em_sfx.h"
#include "game/em_scene_workers.h"
#include "game/em_spawn_table.h"
#include "game/em_player.h"
#include "game/em_player_stage_live.h"

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

/* The load veil block *D_00275888 (0021B180/0021B550/0021B840, S12a). */
static EmLoadVeil s_veil;

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
    UM_001B07C0_LEGACY_WORLD,
    UM_001B6990_LEGACY_WORLD,
    UM_001D19E0,
    UM_001C1DC0,
    UM_00199C50,
    UM_001FAE70,
    UM_001C5C50_LEGACY_WORLD,
    UM_001D1EF0,
    UM_001CB590,
    UM_0015BCF0_CUTSCENE,
    UM_0015C160,
    UM_001F0360,
    UM_001AAD00,
    UM_001D2830,
    UM_001E0CC0,
    UM_001D2880,
    UM_00200830,
    UM_001D19D0,
    UM_0015C1F0,
    UM_0021B1B0,
    UM_0021B500,
    UM_001DA6A0,
    UM_001D52E0,
    UM_COUNT
};

static const struct {
    uint32_t address;
    const char *port; /* where the port does (or does not do) this today */
} s_unmirrored[UM_COUNT] = {
    [UM_001B07C0_LEGACY_WORLD] = {0x001B07C0u, "scene without an original roster: the manifest spawn "
                                               "(em_game_legacy_manifest_spawn at 001AFCA0)"},
    [UM_001B6990_LEGACY_WORLD] = {0x001B6990u, "scene without an original roster: one legacy_world "
                                               "pool node runs the S10a legacy block"},
    [UM_001D19E0] = {0x001D19E0u, "its first callee skin_arena_init (001D2E20) runs on the render context; "
                                  "001D9720, 001DD940, 001E0C30, 001D9060, 001D71F0, 001D7BB0 and the flag "
                                  "registrations after them have no port counterpart here (RENDER_CONTEXT.md 8.4)"},
    [UM_001C1DC0] = {0x001C1DC0u, "its 001D2830 registrations and 001C1E70..001C1F50 passes have no "
                                  "port counterpart; only the AREA11 roster pool gets the 001C1EA0 "
                                  "weather node (001C1EA0 over D_008106C8, em_area11_bindings.c)"},
    [UM_00199C50] = {0x00199C50u, "no port counterpart"},
    [UM_001FAE70] = {0x001FAE70u, "area music cue at the state-0 area entry (a0 = 1) and the "
                                  "state-4 room move (a0 = 0); not mirrored there (its rand() "
                                  "draw and the area-entry music: STARTUP.md). The state-5 "
                                  "status close is translated (w_001FAE70)"},
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
    [UM_001AAD00] = {0x001AAD00u, "scene without an original roster: no collision world, so its "
                                  "nine list-pass hooks and class lists have no port counterpart"},
    [UM_001D2830] = {0x001D2830u, "the status / teardown / load-veil frames' render flag "
                                  "registrations (3, 1) ...: not bound, because flag 3 is cleared "
                                  "by the main loop's step V 001D2300, which is not bound "
                                  "(RENDER_CONTEXT.md section 8); the frame heads' and the area "
                                  "script's registrations run on the render context"},
    [UM_001E0CC0] = {0x001E0CC0u, "status-close draw-mode reset in a scene without the render "
                                  "context (the first level runs em_rcl_001E0CC0)"},
    [UM_001D2880] = {0x001D2880u, "game-over display-list reset; no port counterpart"},
    [UM_00200830] = {0x00200830u, "001AD1A0: VIF1 DMA of the module-3 packet D_0028A564; the native "
                                  "renderer has no counterpart"},
    [UM_001D19D0] = {0x001D19D0u, "001AD1A0: render init (001D9070); no port counterpart"},
    [UM_0015C1F0] = {0x0015C1F0u, "001B07C0: player model bind after the +0x2FF kind store (001CA6E0, "
                                  "+0x0C, +0x96, 00200890); the port draws its one exported player model"},
    [UM_0021B1B0] = {0x0021B1B0u, "001ADF50 loading veil particles (from 0021B550); not drawn"},
    [UM_0021B500] = {0x0021B500u, "001ADF50 loading veil draw (from 0021B550); not drawn"},
    [UM_001DA6A0] = {0x001DA6A0u, "actor drop shadow from 001BA580 (Roger, census L22); the port draws "
                                  "no actor shadow (the player's own post-step is UM_0015C160, "
                                  "docs/SHADOW_ORIGINAL.md)"},
    [UM_001D52E0] = {0x001D52E0u, "001C1DC0's 001C1E70: the static-object grid header into render "
                                  "context +0x140..+0x167; the bank *D_0028A5A0 is not exported and "
                                  "its only reader 001D5370 (001C1D00) is not bound "
                                  "(RENDER_CONTEXT.md section 8)"},
};

static uint64_t s_unmirrored_seen;     /* reached at least once */
static uint64_t s_unmirrored_reported; /* already on stderr */

static int unmirrored(int index)
{
    s_unmirrored_seen |= UINT64_C(1) << index;
    return 0;
}

/* One stderr line per newly reached set, after the tick. */
static void report_unmirrored(void)
{
    uint64_t fresh = s_unmirrored_seen & ~s_unmirrored_reported;
    if (!fresh)
        return;
    fprintf(stderr, "em_scene: legacy mode: reached without port code:");
    for (int i = 0; i < UM_COUNT; ++i)
        if (fresh & (UINT64_C(1) << i))
            fprintf(stderr, " %08X (%s);", (unsigned)s_unmirrored[i].address,
                    s_unmirrored[i].port);
    fputc('\n', stderr);
    s_unmirrored_reported |= fresh;
}

/* 001FC9B0: the message service's reset (em_message_live, WP-8). */
static int w_001FC9B0(void *ctx)
{
    (void)ctx;
    return em_message_live_reset();
}
/* 001D19E0 (the render reset at the area load): skin_arena_init, the one
 * callee the object units need (the skin records' VIF codes and GIF tags,
 * docs/OWNER_DRAW.md "Binding"); the rest stays unmirrored. */
static int um_001D19E0(void *ctx)
{
    (void)ctx;
    if (em_rcl_loaded() && em_rcl_skin_arena_init() < 0) return -1;
    return unmirrored(UM_001D19E0);
}
static int um_00199C50(void *ctx) { (void)ctx; return unmirrored(UM_00199C50); }
static int um_001D1EF0(void *ctx) { (void)ctx; return unmirrored(UM_001D1EF0); }
/* The stream lanes (WP-8b): every stream call of the frame machine, the
 * task chain and the scripts goes to the one live owner, em_stream_live
 * (em_stream_lanes_original over the IOP side em_iop_stream;
 * docs/STREAM_LANES.md, docs/IOP_STREAM.md "Binding").
 *   001FABB0 (byte-matched): 001FA570 (the voice ring D_00281CF0 = -1,
 *     D_00275B30/34 = 0), 001FAB50 (001FAAC0(0), D_008106F4 = 0), 001FAB80
 *     (001FAAC0(1), 001FAAC0(2), D_008106F5 = 0), D_00282157 = 0. Reached
 *     from 0x1AE040 state 1 (r == 2, the status open), 001AD360 step 0 (New
 *     Game and Continue), 001FD470 bit 1 and the scripts' stops.
 *   00119828(ch, l, r): the IOP command 0x16 (the driver's effect-return
 *     volume of core ch, IOP_STREAM.md; kept by the backend, inaudible
 *     without the SPU2 reverb, which the port does not model).
 *   001FA790 / 001FAB50: the game over's cue 0x1B and its release. */
static int w_001FABB0(void *ctx)
{
    (void)ctx;
    return em_stream_live_001FABB0();
}

static int w_00119828(void *ctx, int a0, int a1, int a2)
{
    (void)ctx;
    return em_stream_live_00119828(a0, a1, a2);
}

/* D_00810D38, the current-BGM word: canonical D2 progress since WP-5
 * (em_scene_state.h). 001ADF00 stores 0 (sw). */
static int s_00810D38(void *ctx, int32_t value)
{
    (void)ctx;
    uint8_t *word = em_scene_progress_at(&s_state, 0x00810D38u, 4);
    if (!word)
        return -1;
    for (unsigned i = 0; i < 4; ++i)
        word[i] = (uint8_t)((uint32_t)value >> (8 * i));
    return 0;
}

static int um_001D2830(void *ctx, int a0, int a1) { (void)ctx; (void)a0; (void)a1; return unmirrored(UM_001D2830); }
static int rcl_live(void);
static int rcl_fault(void);

/* 001E0CC0 (the status close): on the render context in the first level. */
static int w_001E0CC0(void *ctx)
{
    (void)ctx;
    if (rcl_live())
        return em_rcl_001E0CC0() < 0 ? rcl_fault() : 0;
    return unmirrored(UM_001E0CC0);
}
static int um_001D2880(void *ctx) { (void)ctx; return unmirrored(UM_001D2880); }
static int w_001FA790(void *ctx, int a0, int a1) { (void)ctx; return em_stream_live_001FA790(a0, a1); }
static int w_001FAB50(void *ctx) { (void)ctx; return em_stream_live_001FAB50(); }

/* ------------------------------------------------------------ readers */

static int16_t r_0028A9A0(void *ctx)
{
    (void)ctx;
    return em_frame_transition()->substate;
}

/* D_00282157: the phase byte of 001FA0D0's asynchronous disc-read
 * sequencer (src/func_001FA0D0.c; 001FABB0 clears it), the stream lanes'
 * own byte (em_stream_live). Read by 0x1AE040 state 3 sub-step 0 (and
 * state 6). */
static uint8_t r_00282157(void *ctx)
{
    (void)ctx;
    return em_stream_live_read_phase();
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

/* ------------------------------------------- EM_AREA_CHANGE_LOG
 *
 * Test instrumentation for tools/test_area_load_reference.py (S12a): with
 * EM_AREA_CHANGE_LOG=<path>, every slot-0 task tick appends one JSON line
 * with the state before and after the tick in the layout of the S3 oracle
 * (tools/test_scene_task_reference.py LAYOUT: task +8..+0x1F, the request
 * block, the area bytes, D_00810730, ...), the veil block before and after,
 * the fade substate at the tick start, the worker trace of the tick, the
 * results of 0021B550 and 001AD230, and the state around a 001AD010 call.
 * The test replays each chain tick through the executed original. Since S12b
 * each line also carries, at the tick start (the original's main-loop-top
 * sample: the 0x28A9A0 fade ticks after the task), the fade block in its
 * original layout, the player position/heading bits, the live 001E55F0 and
 * 001C5930 node counts and the first door's state and locks, for
 * tools/test_room_move_reference.py. It never changes behaviour. */
enum { LOG_SNAP = 168, LOG_VEIL = 0x1C, LOG_TRACE_MAX = 96 };
static FILE *s_log;
static int s_log_checked;
static uint32_t s_log_tick;
static struct {
    uint8_t pre[LOG_SNAP], veil_pre[LOG_VEIL];
    int fade;
    int ntrace, overflow;
    uint32_t trace[LOG_TRACE_MAX][6];
    int r_0021B550, r_001AD230;
    int d010;
    uint8_t d010_pre[LOG_SNAP], d010_post[LOG_SNAP];
    uint8_t fade8[8];
    uint32_t pos[3], yaw;
    int weather, title, door[3];
    uint32_t msg[3];
} s_tick;

static FILE *log_file(void)
{
    if (!s_log_checked) {
        s_log_checked = 1;
        const char *path = getenv("EM_AREA_CHANGE_LOG");
        if (path && path[0]) {
            s_log = fopen(path, "w");
            if (!s_log)
                fprintf(stderr, "em_scene: EM_AREA_CHANGE_LOG=%s cannot be opened\n", path);
        }
    }
    return s_log;
}

static void put_le(uint8_t **p, uint32_t v, int n)
{
    for (int i = 0; i < n; ++i)
        *(*p)++ = (uint8_t)(v >> (8 * i));
}

/* The S3 oracle's LAYOUT, in its order (see the shim's io()). */
static void log_snapshot(uint8_t out[LOG_SNAP])
{
    uint8_t *p = out;
    if (s_user)
        memcpy(p, s_user, 24);
    else
        memset(p, 0, 24);
    p += 24;
    memcpy(p, s_state.req, EM_SCENE_REQ_SIZE);
    p += EM_SCENE_REQ_SIZE;
    *p++ = s_state.d810700;
    *p++ = s_state.d810701;
    *p++ = s_state.d810702;
    memcpy(p, s_state.d810730, sizeof s_state.d810730);
    p += sizeof s_state.d810730;
    put_le(&p, (uint32_t)s_state.d810750, 4);
    put_le(&p, (uint32_t)s_state.spad3B68, 4);
    put_le(&p, s_state.spad3B84, 2);
    put_le(&p, s_state.spad3B8A, 2);
    *p++ = s_state.spad3B8C;
    *p++ = s_state.spad3B8D;
    *p++ = s_state.spad3B8E;
    *p++ = s_state.spad3B8F;
    *p++ = s_state.spad3B90;
    *p++ = s_state.spad3B91;
    *p++ = s_state.spad3B92;
    *p++ = s_state.spad3B93;
    put_le(&p, s_state.spad3258, 4);
    put_le(&p, s_state.spad31F4, 4);
    *p++ = s_state.d275BD8;
    *p++ = s_state.d275BDC;
    *p++ = s_state.d275BE0;
    *p++ = g.cam.top_mode; /* D_008101E4: the camera block's +0x04 (its one storage) */
    put_le(&p, s_state.d810E74, 2);
    put_le(&p, s_state.d810E70, 2);
    *p++ = s_state.d810E50;
}

/* The block *D_00275888 at its original offsets (+0x14 is not modelled). */
static void log_veil(uint8_t out[LOG_VEIL])
{
    uint8_t *p = out;
    memset(out, 0, LOG_VEIL);
    *p++ = s_veil.state;
    *p++ = s_veil.sub;
    *p++ = s_veil.sub2;
    *p++ = s_veil.b03;
    put_le(&p, s_veil.w04, 4);
    for (int i = 0; i < 3; ++i) {
        uint32_t bits;
        memcpy(&bits, &s_veil.level[i], 4);
        put_le(&p, bits, 4);
    }
    p += 4;
    put_le(&p, s_veil.w18, 4);
}

static void log_hex(FILE *f, const uint8_t *b, size_t n)
{
    fputc('"', f);
    for (size_t i = 0; i < n; ++i)
        fprintf(f, "%02x", b[i]);
    fputc('"', f);
}

static void log_tick_begin(void)
{
    if (!log_file())
        return;
    memset(&s_tick, 0, sizeof s_tick);
    s_tick.r_0021B550 = s_tick.r_001AD230 = -2;
    s_tick.fade = em_frame_transition()->substate;
    log_snapshot(s_tick.pre);
    log_veil(s_tick.veil_pre);
    const EmTransitionFade *fade = em_frame_transition();
    uint8_t *p = s_tick.fade8;
    put_le(&p, (uint16_t)fade->substate, 2);
    *p++ = fade->colour;
    *p++ = (uint8_t)fade->mode;
    put_le(&p, (uint16_t)fade->level, 2);
    put_le(&p, (uint16_t)fade->step, 2);
    memcpy(s_tick.pos, g.pos, sizeof s_tick.pos);
    memcpy(&s_tick.yaw, &g.yaw, sizeof s_tick.yaw);
    s_tick.weather = em_scene_bindings_pool_count(0x001E55F0u);
    s_tick.title = em_scene_bindings_pool_count(0x001C5930u);
    /* The door: in AREA11 the fence door 001BC350's record (census L18):
     * +0x05 (the phase), +0x0B (the armed bits) and +0x04; elsewhere the
     * legacy door's state and its two locks. */
    uint8_t door_head[16], door_block[16];
    if (em_area11_door_state(door_head, door_block)) {
        s_tick.door[0] = door_head[5];
        s_tick.door[1] = door_head[0x0B];
        s_tick.door[2] = door_head[4];
    } else {
        s_tick.door[0] = em_door_count() > 0 ? em_door_state(0) : -1;
        s_tick.door[1] = em_door_movement_locked();
        s_tick.door[2] = em_door_menu_locked();
    }
    /* The message block after the previous tick's step F (001FCA10 runs
     * after the task), the route rows' post-frame sample of it: D_002821B0,
     * B4 and B8 of the live message service. */
    const EmMessageBlock *block = em_message_live_block();
    s_tick.msg[0] = block ? (uint32_t)block->mode : 0;
    s_tick.msg[1] = block ? (uint32_t)block->phase : 0;
    s_tick.msg[2] = block ? block->line : 0;
}

static void log_tick_end(int rc)
{
    FILE *f = log_file();
    if (!f)
        return;
    uint8_t post[LOG_SNAP], veil[LOG_VEIL];
    log_snapshot(post);
    log_veil(veil);
    fprintf(f, "{\"tick\": %u, \"rc\": %d, \"fade\": %d, \"pre\": ", s_log_tick++, rc, s_tick.fade);
    log_hex(f, s_tick.pre, LOG_SNAP);
    fputs(", \"post\": ", f);
    log_hex(f, post, LOG_SNAP);
    fputs(", \"veil_pre\": ", f);
    log_hex(f, s_tick.veil_pre, LOG_VEIL);
    fputs(", \"veil_post\": ", f);
    log_hex(f, veil, LOG_VEIL);
    fputs(", \"fade8\": ", f);
    log_hex(f, s_tick.fade8, sizeof s_tick.fade8);
    fprintf(f, ", \"pos\": [%u, %u, %u], \"yaw\": %u, \"weather\": %d, \"title\": %d, "
            "\"door\": [%d, %d, %d]", s_tick.pos[0], s_tick.pos[1], s_tick.pos[2], s_tick.yaw,
            s_tick.weather, s_tick.title, s_tick.door[0], s_tick.door[1], s_tick.door[2]);
    /* WP-4: at the tick end (the route rows' post-frame sample): the
     * letterbox block D_0028A8D0 in its original layout (the 001AEBE0 machine
     * ticks at step D, before the task), the camera block bytes
     * D_008101E4..E7 (the port camera's storage), the canonical progress
     * bytes D_0081084C (power) and D_0081083A (elevator floor), the
     * player position/heading and the camera vectors D_008105D0/E0. At the
     * tick start ("msg_pre": the previous
     * tick's post-frame value, since step F follows the task): the message
     * block D_002821B0 of the live message service (mode, phase, line). */
    {
        const EmScreenFade *bars = em_frame_screen_fade();
        uint8_t screen[8], *q = screen;
        put_le(&q, (uint16_t)bars->state, 2);
        put_le(&q, (uint16_t)bars->step, 2);
        put_le(&q, (uint32_t)bars->level, 4);
        const uint32_t *message = s_tick.msg;
        uint8_t cam4[4] = {g.cam.top_mode, g.cam.table_sel, g.cam.mode, g.cam.hit};
        const uint8_t *power = em_scene_progress_at(&s_state, 0x0081084Cu, 1);
        const uint8_t *floor = em_scene_progress_at(&s_state, 0x0081083Au, 1);
        uint32_t pos[3], yaw, eye[3], tgt[3];
        memcpy(pos, g.pos, sizeof pos);
        memcpy(&yaw, &g.yaw, sizeof yaw);
        memcpy(eye, g.cam.eye, sizeof eye);
        memcpy(tgt, g.cam.tgt, sizeof tgt);
        fputs(", \"screen8\": ", f);
        log_hex(f, screen, sizeof screen);
        fprintf(f, ", \"msg_pre\": [%u, %u, %u], \"cam4\": ", message[0], message[1], message[2]);
        log_hex(f, cam4, sizeof cam4);
        fprintf(f, ", \"power\": %d, \"floor\": %d, \"pos_post\": [%u, %u, %u], \"yaw_post\": %u",
                power ? *power : -1, floor ? *floor : -1, pos[0], pos[1], pos[2], yaw);
        fprintf(f, ", \"eye_post\": [%u, %u, %u], \"tgt_post\": [%u, %u, %u]", eye[0], eye[1], eye[2],
                tgt[0], tgt[1], tgt[2]);
        /* Census L13: the live camera's canonical bytes at the tick end: the
         * camera block D_008101E0 (0xD0 bytes), the forward D_00810600 and
         * D_00810690..D_008106A3 (null without the live camera). */
        {
            const uint8_t *blk = em_camera_live_bound() ? em_camera_live_bytes(0x008101E0u, 0xD0) : NULL;
            const uint8_t *fwd = em_camera_live_bound() ? em_camera_live_bytes(0x00810600u, 16) : NULL;
            const uint8_t *d690 = em_camera_live_bound() ? em_camera_live_bytes(0x00810690u, 20) : NULL;
            fputs(", \"camblk\": ", f);
            if (blk && fwd && d690) {
                fputc('[', f);
                log_hex(f, blk, 0xD0);
                fputs(", ", f);
                log_hex(f, fwd, 16);
                fputs(", ", f);
                log_hex(f, d690, 20);
                fputc(']', f);
            } else {
                fputs("null", f);
            }
        }
        /* Census L32 / L30: the render context at the tick end (null without
         * it): the flag words +0x0C / +0x174, the fog block +0xA0..+0xFF, the
         * zoom +0x2468, V +0x2380, K +0x23C0, the 001CD370(0) projection
         * +0x2240, the eased pairs +0x24F0..+0x2513, the +0x2450 block,
         * D_00275690 / D_00275694, and the camera pool's D_00810610 (the view
         * the NEXT frame head projects). tools/test_level_smoke.py
         * check_render_context. */
        {
            static const struct { uint32_t offset, size; } k_rctx[] = {
                {0x0C, 4}, {0x174, 4}, {0xA0, 0x60}, {0x2468, 4}, {0x2380, 0x40},
                {0x23C0, 0x40}, {0x2240, 0x40}, {0x24F0, 0x24}, {0x2450, 0x18},
            };
            fputs(", \"rctx\": ", f);
            const uint8_t *eases = em_rcl_bytes(0x00275690u, 8);
            const uint8_t *view = em_camera_live_bound() ? em_camera_live_bytes(0x00810610u, 0x40) : NULL;
            if (em_rcl_bound() && eases && view) {
                fputc('[', f);
                for (size_t i = 0; i < sizeof k_rctx / sizeof k_rctx[0]; ++i) {
                    log_hex(f, em_rcl_bytes(EM_RCL_CONTEXT + k_rctx[i].offset, k_rctx[i].size), k_rctx[i].size);
                    fputs(", ", f);
                }
                log_hex(f, eases, 8);
                fputs(", ", f);
                log_hex(f, view, 0x40);
                fputc(']', f);
            } else {
                fputs("null", f);
            }
        }
        /* The live player record at the tick end, as the route rows sample
         * it (route_capture.py): +5, +1F0, +1F1, the clip +20C, the clock
         * +3C (float bits) and the ground owner +214 (its original record
         * address; 0 none). */
        const EmPlayerLiveActor *a = player_states_actor();
        uint32_t clock = em_live_u32(a, 0x3C);
        uint32_t ground = a->link_owner ? em_actor_pool_address(&s_pool, (const EmActor *)a->link_owner) : 0;
        fprintf(f, ", \"player\": [%u, %u, %u, %d, %u, %u, %u]", em_live_u8(a, 5), em_live_u8(a, 0x1F0),
                em_live_u8(a, 0x1F1), (int)(int16_t)em_live_u16(a, 0x20C), clock, ground, em_live_u8(a, 0x2F3));
        /* Census L23, as the route rows sample them: D_00810792 and the
         * truck record (its address, +0x00..+0x0F, +0xB0 and +0x2DC..
         * +0x2EF), or null while no truck node is live. */
        const uint8_t *story = em_scene_progress_at(&s_state, 0x00810792u, 1);
        uint32_t truck_record, truck_pos[3];
        uint8_t truck_head[16], truck_t2dc[20];
        float truck_xyz[3];
        /* Census L18, as the route rows' door_r0 samples it: the fence
         * door's +0x00..+0x0F and its script block +0x1F0..+0x1FF, or null
         * while no door node is bound. */
        {
            uint8_t head[16], block[16];
            fputs(", \"doorrec\": ", f);
            if (em_area11_door_state(head, block)) {
                fputc('[', f);
                log_hex(f, head, sizeof head);
                fputs(", ", f);
                log_hex(f, block, sizeof block);
                fputc(']', f);
            } else {
                fputs("null", f);
            }
        }
        fprintf(f, ", \"story792\": %d, \"truck\": ", story ? *story : -1);
        if (em_area11_boxes_truck_state(&truck_record, truck_head, truck_xyz, truck_t2dc)) {
            memcpy(truck_pos, truck_xyz, sizeof truck_pos);
            fprintf(f, "[%u, ", truck_record);
            log_hex(f, truck_head, sizeof truck_head);
            fprintf(f, ", [%u, %u, %u], ", truck_pos[0], truck_pos[1], truck_pos[2]);
            log_hex(f, truck_t2dc, sizeof truck_t2dc);
            fputc(']', f);
        } else {
            fputs("null", f);
        }
        /* Census L22, as the route rows sample them: the progress bytes
         * D_008107D8 (Roger's story), D_00810758 (event 0), D_00810793 and
         * D_00810813 (the director's step), and Roger's record (its
         * address, +0x00..+0x0F, +0xB0 and the +0x1F0 script block), or
         * null while no Roger node is live; then the equipment node's
         * (address, +0x00..+0x0F, +0xB0: the rows' attach_r9) or null. */
        const uint8_t *d7D8 = em_scene_progress_at(&s_state, 0x008107D8u, 1),
                      *d758 = em_scene_progress_at(&s_state, 0x00810758u, 1),
                      *d793 = em_scene_progress_at(&s_state, 0x00810793u, 1),
                      *d813 = em_scene_progress_at(&s_state, 0x00810813u, 1);
        fprintf(f, ", \"story\": [%d, %d, %d, %d], \"roger\": ", d7D8 ? *d7D8 : -1, d758 ? *d758 : -1,
                d793 ? *d793 : -1, d813 ? *d813 : -1);
        uint32_t roger_record, roger_pos[3];
        uint8_t roger_head[16], roger_block[16];
        float roger_xyz[3];
        if (em_area11_roger_state(&roger_record, roger_head, roger_xyz, roger_block)) {
            memcpy(roger_pos, roger_xyz, sizeof roger_pos);
            fprintf(f, "[%u, ", roger_record);
            log_hex(f, roger_head, sizeof roger_head);
            fprintf(f, ", [%u, %u, %u], ", roger_pos[0], roger_pos[1], roger_pos[2]);
            log_hex(f, roger_block, sizeof roger_block);
            fputc(']', f);
        } else {
            fputs("null", f);
        }
        fputs(", \"equipment\": ", f);
        if (em_area11_roger_equipment_state(&roger_record, roger_head, roger_xyz)) {
            memcpy(roger_pos, roger_xyz, sizeof roger_pos);
            fprintf(f, "[%u, ", roger_record);
            log_hex(f, roger_head, sizeof roger_head);
            fprintf(f, ", [%u, %u, %u]]", roger_pos[0], roger_pos[1], roger_pos[2]);
        } else {
            fputs("null", f);
        }
    }
    /* Census L26 / L27 / L39 / L28: the effect binder's cumulative counters
     * (001CD520 sprites, 001CFBE0 chains emitted / skipped, 001F0720 lanes,
     * barrel frames, counted gaps), its live nodes (address, +0x10, +0x04, +0x05, +0x0D
     * and the record words em_effects_live_nodes lists) and the equipment
     * nodes (address, +0x00..+0x0F, +0x44, +0x4C, drew, drawn at the
     * player's node, bone 0's row 3) and the last barrel's lane-packet and
     * glow-marker digests (em_effects_live_lane_digests / _sprite_digests),
     * or
     * null without the binder. tools/test_level_smoke.py check_effects. */
    fputs(", \"effects\": ", f);
    if (em_effects_live_attached()) {
        EmEffectsLiveCounters c;
        em_effects_live_counters(&c);
        fprintf(f, "[[%u, %u, %u, %u, %u, %u], [", c.sprites, c.chains, c.chains_skipped, c.lanes, c.frames,
                c.gaps);
        static EmEffectsLiveNode nodes[EM_ACTOR_POOL_CAPACITY];
        int n = em_effects_live_nodes(nodes, EM_ACTOR_POOL_CAPACITY);
        for (int i = 0; i < n; ++i) {
            const EmEffectsLiveNode *e = &nodes[i];
            fprintf(f, "%s[%u, %u, %u, %u, %u", i ? ", " : "", e->address, e->callback, e->state, e->sub, e->key);
            for (int k = 0; k < 9; ++k) fprintf(f, ", %u", e->w[k]);
            fputc(']', f);
        }
        fputs("], [", f);
        static EmEquipmentLiveNode equip[16];
        int m = em_equipment_live_nodes(equip, 16);
        for (int i = 0; i < m; ++i) {
            const EmEquipmentLiveNode *e = &equip[i];
            fprintf(f, "%s[%u, ", i ? ", " : "", e->address);
            log_hex(f, e->head, sizeof e->head);
            fprintf(f, ", %u, %u, %u, %u, [%u, %u, %u]]", e->model, e->method, e->drew, e->at_node, e->bone0[12],
                    e->bone0[13], e->bone0[14]);
        }
        fputs("], [", f);
        uint32_t digests[EM_EFFECTS_LIVE_LANE_DIGESTS];
        em_effects_live_lane_digests(digests);
        for (int i = 0; i < EM_EFFECTS_LIVE_LANE_DIGESTS; ++i) fprintf(f, "%s%u", i ? ", " : "", digests[i]);
        fputs("], [", f);
        uint32_t sprites[16];
        int ns = em_effects_live_sprite_digests(sprites);
        for (int i = 0; i < ns; ++i) fprintf(f, "%s%u", i ? ", " : "", sprites[i]);
        fputs("]]", f);
    } else {
        fputs("null", f);
    }
    /* The owner draws 001CAA00 of the last drawn frame (em_owner_draw_live):
     * record address, unit bytes, clip, and the colour / lighting-row /
     * position-row / point-light-slot / rig-lane digests. tools/test_level_smoke.py
     * check_owner_units. */
    fputs(", \"owner_units\": [", f);
    {
        EmOwnerDrawLiveLog units[EM_OWNER_DRAW_LIVE_UNITS];
        const int nu = em_owner_draw_live_log(units, EM_OWNER_DRAW_LIVE_UNITS);
        for (int i = 0; i < nu; ++i)
            fprintf(f, "%s[%u, %u, %u, %u, %u, %u, %u, %u]", i ? ", " : "", units[i].record, units[i].bytes,
                    units[i].clip, units[i].colour, units[i].light, units[i].position, units[i].points,
                    units[i].light_rig);
    }
    fputc(']', f);
    fprintf(f, ", \"r_0021B550\": %d, \"r_001AD230\": %d, \"overflow\": %d, \"trace\": [",
            s_tick.r_0021B550, s_tick.r_001AD230, s_tick.overflow);
    for (int i = 0; i < s_tick.ntrace; ++i)
        fprintf(f, "%s[%u, %u, %u, %u, %u, %u]", i ? ", " : "", s_tick.trace[i][0], s_tick.trace[i][1],
                s_tick.trace[i][2], s_tick.trace[i][3], s_tick.trace[i][4], s_tick.trace[i][5]);
    fputs("], \"d010\": ", f);
    if (s_tick.d010) {
        fputc('[', f);
        log_hex(f, s_tick.d010_pre, LOG_SNAP);
        fputs(", ", f);
        log_hex(f, s_tick.d010_post, LOG_SNAP);
        fputc(']', f);
    } else {
        fputs("null", f);
    }
    fputs("}\n", f);
    fflush(f);
}

static void log_trace(uint32_t caller, uint32_t callee, uint32_t a0, uint32_t a1, uint32_t a2,
                      uint32_t a3)
{
    if (!s_log)
        return;
    if (s_tick.ntrace >= LOG_TRACE_MAX) {
        s_tick.overflow = 1;
        return;
    }
    uint32_t *t = s_tick.trace[s_tick.ntrace++];
    t[0] = caller;
    t[1] = callee;
    t[2] = a0;
    t[3] = a1;
    t[4] = a2;
    t[5] = a3;
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
    log_trace(caller, callee, a0, a1, a2, a3);
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
    if (!s_log)
        return em_sf_001AD010(&s_state, s_user, &s_workers);
    s_tick.d010 = 1;
    log_snapshot(s_tick.d010_pre);
    int rc = em_sf_001AD010(&s_state, s_user, &s_workers);
    log_snapshot(s_tick.d010_post);
    return rc;
}

/* -------------------------------------------------------- legacy workers */

/* The scene the pool is built for: the AREA11 roster when the loaded scene
 * is AREA11 (the area read of D_00810700/701 = 0x0B/0, em_game_legacy_area_load),
 * otherwise a scene the original roster does not describe (the office and
 * drawbridge fixtures of EM_SKIP_STARTUP). */
/* The live camera's inputs from the rest of the port (em_camera_live.h,
 * census L13..L16): the live player record, the closure's pad assignment
 * block, the AREA11 boxes' 0x700031F0 word, camera action 0's legacy
 * stand-ins over the g.cam view and the +4 == 3 timeline (the opening's
 * track while the opening owns the camera, else the AREA11 script host). */
static const EmPlayerLiveActor *camera_player(void *ctx) { (void)ctx; return player_states_actor(); }
static const uint16_t *camera_pad_config(void *ctx) { (void)ctx; return em_player_closure_live_pad_config(); }
static int camera_hip(void *ctx, float out[3]) { (void)ctx; return player_pose_hip(out); }
static int camera_euler(void *ctx, float out[3]) { (void)ctx; return player_pose_script_euler(out); }
static int camera_standins(void *ctx) { (void)ctx; return camera_area11_standins(&g.cam); }
static int camera_timeline(void *ctx)
{
    (void)ctx;
    int owned = em_opening_runtime_camera_sample();
    if (owned < 0) return -1;
    if (owned) return 0;
    return em_area11_script_host_camera_0022EEF0() < 0 ? -1 : 0;
}
static EmCameraLiveHost k_camera_host = {NULL, camera_player, camera_hip, camera_euler, camera_pad_config,
                                         NULL, camera_standins, camera_timeline};

/* ------------------------------------------ the render context (L32 / L30)
 *
 * The one canonical render context (em_render_context_live,
 * docs/RENDER_CONTEXT.md section 8), bound at the area load of a scene with
 * the live camera (AREA11): the views of the bytes other modules own (the
 * camera pool's D_00810610 and D_008105E0, the request block, the area
 * bytes, D_008101E4 and 0x70003B8D of the scene state, the camera's view of
 * the player record; D_008101E4 is the live camera's +0x04) and the workers it does not translate: the point-light
 * tick 001D7C30 (em_point_light), the SDK sqrtf / tanf of the area's
 * collision world, 001C1DC0's weather spawn 001C1EA0 and the reported
 * 001D52E0. */
static int rcl_point_light(void *ctx)
{
    (void)ctx;
    return em_render_point_light_tick();
}

static int rcl_sqrt(void *ctx, uint32_t x, uint32_t *out)
{
    (void)ctx;
    EmSdkMathContext *m = em_collision_world_sdk();
    float r;
    uint32_t f = 0;
    if (!m || em_sdk_math_original_0011E748(m->tables, &m->world, &m->workers, em_ee_float(x), &r, &f) < 0)
        return -1;
    *out = em_ee_bits(r);
    return 0;
}

static int rcl_tan(void *ctx, uint32_t x, uint32_t *out)
{
    (void)ctx;
    EmSdkMathContext *m = em_collision_world_sdk();
    float r;
    uint32_t f = 0;
    if (!m || em_sdk_math_original_0011E398(m->tables, em_ee_float(x), &r, &f) < 0)
        return -1;
    *out = em_ee_bits(r);
    return 0;
}

static int rcl_weather(void *ctx, uint32_t block)
{
    (void)ctx;
    if (block != EM_SCENE_D_008101D0 || s_pool_mode != POOL_ROSTER)
        return -1;
    return em_area11_spawn_weather_001C1EA0();
}

static int rcl_grid_header(void *ctx)
{
    (void)ctx;
    return unmirrored(UM_001D52E0);
}

_Static_assert(offsetof(EmSceneState, d810702) == offsetof(EmSceneState, d810700) + 2,
               "the area bytes D_00810700..702 are one view");

static int rcl_bind(void)
{
    const EmRclExternal views[] = {
        {0x00810610u, 0x40u, em_camera_live_bytes(0x00810610u, 0x40u)},
        {0x008105E0u, 0x10u, em_camera_live_bytes(0x008105E0u, 0x10u)},
        {EM_SCENE_REQ_BASE, EM_SCENE_REQ_SIZE, s_state.req},
        {0x00810700u, 3u, &s_state.d810700},
        {0x008101E4u, 1u, &g.cam.top_mode},   /* the camera block's +0x04 */
        {0x70003B8Du, 1u, &s_state.spad3B8D},
        /* Read only: the camera's view of D_008102B0 (CAMERA_LIVE.md 5). */
        {0x008102B0u, 0x320u, (uint8_t *)(uintptr_t)em_camera_live_player_bytes()},
    };
    static const EmRclWorkers workers = {NULL, rcl_point_light, rcl_sqrt, rcl_tan, rcl_weather,
                                         rcl_grid_header};
    return em_rcl_bind(views, sizeof views / sizeof views[0], &workers);
}

/* The first level's frames run the render context's translations. */
static int rcl_live(void)
{
    return em_rcl_bound() && em_camera_live_bound();
}

static int rcl_fault(void)
{
    uint32_t at = em_rcl_fault();
    return em_scene_fault(&s_state, at ? at : 0x00275670u, EM_SCENE_FAULT_WORKER_FAILED);
}

static int roster_scene(void)
{
    return strcmp(g.scene_dir, AREA11_SCENE_DIR) == 0;
}

/* 0x1AE040 state 0, first callee. 001AFCA0 is 001AF5C0 (player wipe),
 * 001AF690, 001AF710, 001AF8E0 (pool reset), 001D0660, then spad 31F4 = 0
 * (design 2.3). The port's native re-arm stands in for the player wipe;
 * since S10b the pool half of 001AF8E0 runs here, and since census L07 its
 * class-list half (D_00275B54..BB8, the collision world's lists) with the
 * area's collision world. The pool reset calls every live node's release hook. A scene without an
 * original roster also takes its legacy placement here (the manifest spawn,
 * the camera re-arm and the fixtures, in their old order); AREA11 is placed
 * by 001B07C0 (S12a). */
static int w_001AFCA0(void *ctx)
{
    (void)ctx;
    /* The AREA11 interaction host's owners die with the pool (001AF8E0):
     * detach the player's Use and stage hooks, then free the owner tokens
     * (em_area11_interaction_host.h: whole-world teardown). */
    player_use_set_hook(NULL, NULL);
    em_player_closure_live_set_scan(NULL, NULL);
    player_pose_set_stage_hook(NULL, NULL);
    em_message_live_set_host(NULL);
    em_area11_interaction_host_clear();
    em_game_legacy_state0();
    /* Census L07: the collision world of the area. AREA11 installs its cell
     * directory (*0x70003250), the EMCL rank section and the SDK tables and
     * zeroes the walkers' scratchpad; a scene without an original roster has
     * no original world (its callers keep em_collision.c). Before the player
     * stage binds: its floor workers are this world's (FLOOR stays gated). */
    if (roster_scene()) {
        if (em_collision_world_load(&g.coll, g.coll_path, EM_COLLISION_WORLD_CELLS_PATH,
                                    EM_COLLISION_WORLD_SDK_PATH) != 0)
            return em_scene_fault(&s_state, 0x001AFCA0u, EM_SCENE_FAULT_NULL_WORKER);
    } else {
        em_collision_world_unload();
    }
    /* 001AF5C0 wipes the player record; the first stage's 0015C420 then
     * sets its spawn values (+4 = 1, +280, +204, +31B; player_states_reset
     * writes both), and the stage runs with its workers from the first
     * gameplay stage on (census L01, em_player_stage_live.h). Without the
     * D_00248C98 export the stage cannot run: fault. */
    player_states_reset();
    if (em_player_stage_live_bind() < 0)
        return em_scene_fault(&s_state, 0x0015BA50u, EM_SCENE_FAULT_NULL_WORKER);
    /* Census L13..L16: the live camera over the area's collision world
     * (its 0019A910 / 0019B7D0 and SDK context) and the ELF camera tables;
     * the legacy camera stays for a scene without an original world. */
    k_camera_host.carry31F0 = em_area11_boxes_carry31F0();
    /* Census L32 / L30: the render context's views and workers (before the
     * camera, whose 001DD980 publications store into it). */
    if (roster_scene() && rcl_bind() < 0)
        return em_scene_fault(&s_state, 0x001D1C50u, EM_SCENE_FAULT_NULL_WORKER);
    if (roster_scene() && em_camera_live_bind(&k_camera_host) < 0)
        return em_scene_fault(&s_state, 0x0018B9C0u, EM_SCENE_FAULT_NULL_WORKER);
    if (!roster_scene()) {
        em_game_legacy_manifest_spawn();
        em_game_legacy_camera_rearm();
        em_game_legacy_state0_fixtures();
    }
    s_player_init_pending = 1; /* the 001AF5C0 wipe: player +4 = 0 */
    em_actor_pool_reset_001AF8E0(&s_pool);
    em_collision_world_lists_reset_001AF8E0(); /* 001AF8E0's class-list half */
    em_area11_bindings_reset();
    /* 001D0660: 001F0310, the effect pools (census L26 / L27), with the
     * effect and equipment binders over the new pool (the first level: its
     * effects draw on the render context); its 001E7780 (the overlay module
     * dispatch) is the loader's boundary. */
    if (roster_scene()) {
        if (em_area11_bindings_effects_attach() < 0)
            return em_scene_fault(&s_state, em_effects_live_fault() ? em_effects_live_fault() : 0x001F0310u,
                                  EM_SCENE_FAULT_NULL_WORKER);
    } else {
        em_effects_live_detach();
    }
    s_pool_mode = POOL_NONE;
    s_state.spad31F4 = 0;
    return 0;
}

/* 001AD360 step 4's area bytes (design 10.1): 700 = 0x0B, 701 = 702 = 0,
 * D_00810730[0x0B] = 0, committed by the EM_SKIP_STARTUP fixture entry when
 * its scene is AREA11 (the fixture skips 001AD360). */
static void fixture_commit_area11(void)
{
    s_state.d810700 = 0x0B;
    s_state.d810701 = 0;
    s_state.d810702 = 0;
    s_state.d810730[0x0B] = 0;
}

/* Trace of a call a bindings-level translation makes (001AD1A0, 001B07C0,
 * 001B0C60): the same (caller, callee, a0..a3) record the cores emit. */
static void bind_trace(uint32_t caller, uint32_t callee, uint32_t a0, uint32_t a1, uint32_t a2,
                       uint32_t a3)
{
    if (s_workers.trace)
        s_workers.trace(s_workers.ctx, caller, callee, a0, a1, a2, a3);
}

/* ------------------------------------------- spawn placement (S12a)
 *
 * 0x1AE040 state 0 calls 001B07C0(0) (0x1AE094). AREA11: the byte-matched
 * translation em_spawn_001B07C0 over the exported D_0024D650 window
 * (assets/spawn/spawn_table.emsp, tools/export_spawn_table.py; verified by
 * tools/test_spawn_place_reference.py), with the port's player as its
 * EmSpawnIo view:
 *   area bytes 700/701/702, D_00275BE0, 3B8D, D_00810788  canonical (s_state)
 *   D_008106C8        canonical request word C8 (s_state.req; 001AFCF0
 *                     cleared it just before, as in the original)
 *   0x70003B40..5C    canonical s_state.spad3B40
 *   +0xA0/+0xB0 xyz   g.pos (the port keeps one position; 001B07C0 writes
 *                     the same record position to both)
 *   +0xC4 heading     g.yaw (+0xC0/+0xC8 are written 0; the port has no
 *                     pitch or roll)
 *   +0x220/+0x228     g.status.health/infection, which are also the port's
 *                     only copy of D_00810858/D_0081085C, so 001B07C0's copy
 *                     is an identity here
 *   +0x234/+0x235     g.pd_infected/g.pd_low. D_00810707 (the +0x234 source)
 *                     is canonical progress (HK), stored by 0015CF90 at every
 *                     player stage (em_player_0015BCF0) and cleared by
 *                     001AF2C0; D_00810706 is not canonical yet (D2), so g.pd_low
 *                     stays the port's only copy of it
 *   D_00810C60        em_pickup's equipment status; C7D/C7E its item counts
 *                     0x19/0x1A
 *   +0x0E, +0x60..+0x8C, +0x230   written, no port storage and no port reader
 *   +0x1C, +0x304     no port object at these offsets (0): the stores through
 *                     them and 001EFE00 (reached only with D_008106C8 & 4 and
 *                     & 0x60, i.e. AREA11 after event 0x30) have no worker:
 *                     reaching them faults
 *   0015C1F0          reported no-port-code (the port's one player model)
 *   001B0460          the live camera's translation (em_camera_live_001B0460,
 *                     census L13), after the placed pose is committed to g
 *   +0x224/+0x22C     g.pd_pend_hp/g.pd_pend_inf (arg0 1 drops them)
 *   +0x00             arg0 1 with pending damage writes 1: no port storage
 *                     (1 in every capture)
 *   +0x04/+0x05/+0x06 arg0 1 with the record's +0x14 byte 1 writes 5/1/0,
 *                     the walk-out state 5/1: the legacy walk-out
 *                     (em_door_room_move_arrival, S12b)
 * arg0 is 0 in state 0 and 1 in state 4 (S12b, "the room move" below); any
 * other pairing is refused (fault), as is D_00275BE0 == 1 (the load-game
 * pose D_00810710..728 has no canonical storage; its only writer, 0x1AE040
 * state 2, is unported). After the state-0 placement the port's
 * placement-dependent fixtures run (em_game_legacy_state0_fixtures).
 * A scene without an original roster keeps its manifest spawn (001AFCA0). */

static EmSpawnTable s_spawn_table;
static int s_spawn_table_loaded;
static EmSpawnIo *s_spawn_io; /* the placement in progress (for 001B0460) */

static void spawn_commit(const EmSpawnIo *io)
{
    g.pos[0] = io->player.f0B0[0];
    g.pos[1] = io->player.f0B0[1];
    g.pos[2] = io->player.f0B0[2];
    g.yaw = io->player.f0C0[1];
    g.status.health = io->player.f220;
    g.status.infection = io->player.f228;
    g.pd_infected = io->player.b234;
    g.pd_low = io->player.b235;
    g.pd_pend_hp = io->player.f224;
    g.pd_pend_inf = io->player.f22C;
    uint8_t status, primary, secondary;
    em_pickup_equipment_read(&status, &primary, &secondary);
    em_pickup_equipment_write(io->d810C60, primary, secondary);
    em_scene_req_set_u32(&s_state, EM_SCENE_REQ_C8, (uint32_t)io->d8106C8);
    memcpy(s_state.spad3B40, io->spad3B40, sizeof s_state.spad3B40);
}

/* 0015C1F0(player) (NEARMISS C, logic recovered; census L22 translates
 * its first store): the model kind +0x2FF from the infection mode +0x234
 * and the equipment status D_00810C60: mode 0 -> 0x3B, with status 2 ->
 * 0x3F, 1 -> 0x3E; mode 1 -> 0x40, with status 2 -> 0x3F, 1 -> 0x3E; any
 * other mode -> 0x3D. 001B81D0 (a script's player face attach) reads it.
 * The rest (the D_0028A490[kind] model bind 001CA6E0, +0x0C, +0x96 = 0x28
 * and 00200890) binds the model the port draws from its own export:
 * reported (UM_0015C1F0). */
static int spawn_w_0015C1F0(void *ctx, uint32_t player)
{
    (void)ctx;
    bind_trace(EM_SPAWN_FN_001B07C0, EM_SPAWN_FN_0015C1F0, player, 0, 0, 0);
    EmPlayerLiveActor *p = player_states_actor_mut();
    if (player != D_PLAYER || !s_spawn_io || !p)
        return -1;
    const uint8_t mode = s_spawn_io->player.b234, sel = s_spawn_io->d810C60;
    uint8_t kind;
    if (mode == 0)
        kind = sel == 2 ? 0x3F : sel == 1 ? 0x3E : 0x3B;
    else if (mode == 1)
        kind = sel == 2 ? 0x3F : sel == 1 ? 0x3E : 0x40;
    else
        kind = 0x3D;
    em_live_set_u8(p, 0x2FF, kind);
    return unmirrored(UM_0015C1F0);
}

/* 001B0460(a0). Its only a0 test (0x1B0460 .. block_14) is "a0 != 0 and
 * D_008104E0 is 0x10 or 0x12"; D_008104E0 is player +0x230, which 001B07C0
 * stored 0 just before the call, so a0 = 1 (state 4) takes the same arm as
 * a0 = 0 and the one stand-in serves both. */
/* The room camera record reader and 001B0250 for the live 001B0460
 * (em_camera_live.c): the words of the spawn table window, and the spawn
 * translation's 001B0250 committed to the canonical D_008106C8. */
static int room_read_word(void *ctx, uint32_t address, uint32_t *out)
{
    (void)ctx;
    const uint8_t *p = em_spawn_table_read(&s_spawn_table, address, 4);
    if (!p) return -1;
    memcpy(out, p, 4);
    return 0;
}

static int room_001B0250(void *ctx)
{
    (void)ctx;
    if (!s_spawn_io || em_spawn_001B0250(&s_spawn_table, s_spawn_io) < 0) return -1;
    em_scene_req_set_u32(&s_state, EM_SCENE_REQ_C8, (uint32_t)s_spawn_io->d8106C8);
    return 0;
}

/* 001B0460(a0) at the end of 001B07C0, after the placement is committed:
 * the translated camera re-seat over the live camera's canonical bytes
 * (em_camera_live_001B0460: em_script_host_001B0460 with em_script_door_fan's
 * 001B0080, the translated commit 0018C0D0 and 001DD980). */
static int spawn_w_001B0460(void *ctx, int a0)
{
    (void)ctx;
    bind_trace(EM_SPAWN_FN_001B07C0, EM_SPAWN_FN_001B0460, (uint32_t)a0, 0, 0, 0);
    if ((a0 != 0 && a0 != 1) || !s_spawn_io)
        return -1;
    spawn_commit(s_spawn_io);
    int32_t w230 = (int32_t)s_spawn_io->player.w230;
    float c0[4] = {s_spawn_io->player.f0C0[0], s_spawn_io->player.f0C0[1], s_spawn_io->player.f0C0[2],
                   s_spawn_io->player.f0C0[3]};
    const EmCameraLiveRoom room = {NULL, room_read_word, room_001B0250, s_spawn_io->player.f0A0, c0, &w230};
    return em_camera_live_001B0460(a0, &room) < 0 ? -1 : 0;
}

static int w_001B07C0(void *ctx, int a0)
{
    (void)ctx;
    if (!roster_scene())
        return unmirrored(UM_001B07C0_LEGACY_WORLD);
    if (a0 != (s_entry_state == 4 ? 1 : 0) || (s_entry_state != 0 && s_entry_state != 4) ||
        s_state.d275BE0 == 1)
        return -1;
    if (!s_spawn_table_loaded) {
        if (em_spawn_table_load(&s_spawn_table, EM_SPAWN_TABLE_PATH) != 0) {
            fprintf(stderr, "em_scene: 001B07C0: %s missing or malformed (run "
                    "tools/export_spawn_table.py)\n", EM_SPAWN_TABLE_PATH);
            return em_scene_fault(&s_state, EM_SPAWN_FN_001B07C0, EM_SCENE_FAULT_NULL_WORKER);
        }
        s_spawn_table_loaded = 1;
    }
    const uint8_t *e788 = em_scene_progress_at(&s_state, 0x00810788u, 1);
    const uint8_t *e707 = em_scene_progress_at(&s_state, 0x00810707u, 1);
    const uint8_t *items = em_pickup_items();
    uint8_t status, primary, secondary;
    em_pickup_equipment_read(&status, &primary, &secondary);
    EmSpawnIo io;
    memset(&io, 0, sizeof io);
    io.d810700 = s_state.d810700;
    io.d810701 = s_state.d810701;
    io.d810702 = s_state.d810702;
    io.d275BE0 = s_state.d275BE0;
    io.d810706 = (uint8_t)g.pd_low;
    io.d810707 = e707 ? *e707 : 0;
    io.d810858 = g.status.health;
    io.d81085C = g.status.infection;
    io.d810788 = e788 ? *e788 : 0;
    io.d810C7D = items[0x19];
    io.d810C7E = items[0x1A];
    io.spad3B8D = s_state.spad3B8D;
    io.d810C60 = status;
    io.d8106C8 = (int32_t)em_scene_req_u32(&s_state, EM_SCENE_REQ_C8);
    memcpy(io.spad3B40, s_state.spad3B40, sizeof io.spad3B40);
    io.player.f0A0[0] = io.player.f0B0[0] = g.pos[0];
    io.player.f0A0[1] = io.player.f0B0[1] = g.pos[1];
    io.player.f0A0[2] = io.player.f0B0[2] = g.pos[2];
    io.player.f0C0[1] = g.yaw;
    io.player.f220 = g.status.health;
    io.player.f224 = g.pd_pend_hp;
    io.player.f228 = g.status.infection;
    io.player.f22C = g.pd_pend_inf;
    io.player.b234 = (uint8_t)g.pd_infected;
    io.player.b235 = (uint8_t)g.pd_low;
    EmSpawnWorkers w = {NULL, NULL, NULL, spawn_w_0015C1F0, spawn_w_001B0460};
    bind_trace(EM_SPAWN_FN_001B07C0, EM_SPAWN_FN_001B0250,
               EM_SPAWN_TABLE_ADDRESS + 4u * io.d810700, 4u * io.d810701, 0, 0);
    s_spawn_io = &io;
    int rc = em_spawn_001B07C0(&s_spawn_table, &io, &w, a0);
    s_spawn_io = NULL;
    if (rc < 0)
        return em_scene_fault(&s_state, io.fault.address, (EmSceneFaultCode)io.fault.code);
    spawn_commit(&io);
    if (a0 == 0) {
        em_game_legacy_state0_fixtures();
        return 0;
    }
    /* State 4: the player state 001B07C0 wrote (5/1/0 = the walk-out at
     * entry 1: still the legacy walk-out, em_door.c; route beat 09's entry 2
     * has none). The door and its program are the original owner's since
     * census L18 (em_area11_door); 0x1AE040 clears the camera byte
     * D_008101E4 below (0x1AE0BC, stored at w_0018D7B0). */
    em_door_room_move_arrival(io.player.b004 == 5 && io.player.b005 == 1 && io.player.b006 == 0,
                              g.yaw);
    return 0;
}

/* ------------------------------------------- the load arms (S12a)
 *
 * New Game: the frontend registers this task with a cleared record (001AC070
 * state 4's 001AB790(001ACEC0)); 001ACEC0 +8 = 0 runs 001AD1A0 (module 3),
 * +8 = 1 runs 001AD230 (the 001AF2C0 reset), +8 = 3 runs 001AD250: +9 = 0
 * 001AD360 (the intro movie at step 1: D_00275C78 = 0, D_00821058 = 1),
 * +9 = 5 001ADF50 (the area read, the load veil), +9 = 1 the frame machine,
 * whose state 0 rebuilds the area. The same route replays on Continue
 * (001AC070 option 0). Workers:
 *   001AD1A0          translated here (byte-matched, see w_001AD1A0)
 *   001FF080(0, 3)    screen module 3: resident in the port; D_00275BD8 = 0
 *   001FF080(1, 0)    the area read em_game_legacy_area_load of the scene of
 *                     D_00810700/701 (only 0x0B/0, AREA11, is exported);
 *                     D_00275BD8 = 0 when it returns (the original's slot-2
 *                     task 001FF0D0 clears it at state 0x63; the port's read
 *                     completes inside the call)
 *   001AD230          em_game_new_game_reset_001AF2C0, returns 4
 *   D_00275C78, D_00821058  em_frontend_movie_select / _request
 *   001AED80(a0)      em_frame_fade_clear (its translation, em_fade.c)
 *   0021B180/0021B550/0021B840  the veil state machine (em_load_veil.c) over
 *                     s_veil; its 001D2830 calls and particles 0021B1B0/
 *                     0021B500 are reported no-port-code
 *   00200830, 001D19D0 reported no-port-code */

static int in_task_step(int s09, int s0A)
{
    const uint8_t *b9 = em_scene_task_byte(s_user, EM_SCENE_TASK_09);
    const uint8_t *bA = em_scene_task_byte(s_user, EM_SCENE_TASK_0A);
    return b9 && bA && *b9 == s09 && *bA == s0A;
}

/* The area read for D_00810700/701. */
static int area_read(void)
{
    if (s_state.d810700 == 0x0B && s_state.d810701 == 0)
        return em_game_legacy_area_load(AREA11_SCENE_DIR);
    fprintf(stderr, "em_scene: 001FF080(1, 0): area %02X room %u is not exported\n",
            (unsigned)s_state.d810700, (unsigned)s_state.d810701);
    return -1;
}

/* 001AD1A0 (byte-matched, src/func_001AD1A0.c; mwcc 2.3.3): +9 == 0: +9++,
 * D_00275BD8 = 1, 001FF080(0, 3, &slot[9]); +9 == 1 and D_00275BD8 == 0:
 * 00200830(D_0028A564[0]), 001D19D0(), return 4; otherwise 0. */
static int w_001AD1A0(void *ctx)
{
    (void)ctx;
    uint8_t *sub = em_scene_task_byte(s_user, EM_SCENE_TASK_09);
    if (!sub)
        return -1;
    if (*sub == 0) {
        *sub = (uint8_t)(*sub + 1);
        s_state.d275BD8 = 1;
        /* a2 = &slot[9], the record address + 9 (no port address: 0). */
        bind_trace(0x001AD1A0u, 0x001FF080u, 0, 3, 0, 0);
        s_state.d275BD8 = 0; /* module 3 is resident (no port data to read) */
        return 0;
    }
    if (*sub == 1 && s_state.d275BD8 == 0) {
        bind_trace(0x001AD1A0u, 0x00200830u, 0, 0, 0, 0);
        unmirrored(UM_00200830);
        bind_trace(0x001AD1A0u, 0x001D19D0u, 0, 0, 0, 0);
        unmirrored(UM_001D19D0);
        return 4;
    }
    return 0;
}

static int w_001AD230(void *ctx)
{
    (void)ctx;
    em_game_new_game_reset_001AF2C0();
    s_tick.r_001AD230 = 4;
    return 4;
}

static int s_00275C78(void *ctx, uint8_t value)
{
    (void)ctx;
    return em_frontend_movie_select(value);
}

static int s_00821058(void *ctx, uint8_t value)
{
    (void)ctx;
    return em_frontend_movie_request(value);
}

static int w_001AED80(void *ctx, uint8_t a0)
{
    (void)ctx;
    em_frame_fade_clear(a0);
    return 0;
}

static int veil_001D2830(void *ctx, int group, int enable)
{
    (void)ctx;
    (void)group;
    (void)enable;
    return unmirrored(UM_001D2830);
}

static int veil_0021B1B0(void *ctx, EmLoadVeil *veil)
{
    (void)ctx;
    (void)veil;
    return unmirrored(UM_0021B1B0);
}

static int veil_0021B500(void *ctx, EmLoadVeil *veil)
{
    (void)ctx;
    (void)veil;
    return unmirrored(UM_0021B500);
}

static const EmLoadVeilWorkers k_veil_workers = {NULL, veil_001D2830, veil_0021B1B0, veil_0021B500};

static int w_0021B180(void *ctx)
{
    (void)ctx;
    uint32_t at = 0;
    return em_load_veil_0021B180(&s_veil, &k_veil_workers, &at) < 0 ? -1 : 0;
}

static int w_0021B550(void *ctx)
{
    (void)ctx;
    uint32_t at = 0;
    int r = em_load_veil_0021B550(&s_veil, &k_veil_workers, &at);
    s_tick.r_0021B550 = r;
    return r;
}

static int w_0021B840(void *ctx)
{
    (void)ctx;
    em_load_veil_0021B840(&s_veil);
    return 0;
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
    int rc = em_actor_roster_spawn_001B6990(&s_roster, &s_pool, &s_state,
                                            (EmActorRosterProgress *)em_scene_progress_spawn_view(&s_state),
                                            em_area11_bind_roster, NULL, NULL);
    if (rc < 0)
        return rc;
    /* WP-4: the native services of the panel 00159210 (area11[18]) and the
     * terminal 00827B10 (area11[19]) that 001B6990 just placed: the AREA11
     * interaction host, its Use scan inside the player callbacks (00160220
     * via player_use_poll) and its shared player worker at the player stage
     * (0015B130/00182DF0 via player_pose_stage). A host that cannot load
     * faults here rather than leave the two owners without behaviour. */
    if (!em_area11_interaction_host_load(g.scene_dir, NULL, NULL)) {
        fprintf(stderr, "em_scene: 001B6990: the AREA11 interaction host did not load\n");
        return em_scene_fault(&s_state, 0x00159210u, EM_SCENE_FAULT_NULL_WORKER);
    }
    player_pose_set_stage_hook(em_area11_interaction_host_player, NULL);
    player_use_set_hook(em_area11_interaction_host_use, NULL);
    em_player_closure_live_set_scan(em_area11_interaction_host_scan_00184BA0, NULL);
    em_message_live_set_host(em_area11_interaction_host_message_host());
    return rc;
}

/* 001C1DC0. AREA11: the translation em_rvr_001C1DC0 on the render
 * context (em_rcl_001C1DC0: the flag registrations, the area fog 001D8FD0,
 * 001C1F50's TEX0 / colour / flags; its 001C1EA0 spawns the weather node
 * through rcl_weather; its 001C1E70 -> 001D52E0 is reported). A scene
 * without the render context keeps only the weather spawn. */
static int w_001C1DC0(void *ctx)
{
    (void)ctx;
    if (s_pool_mode == POOL_ROSTER && rcl_live())
        return em_rcl_001C1DC0() < 0 ? rcl_fault() : 0;
    unmirrored(UM_001C1DC0);
    if (s_pool_mode != POOL_ROSTER)
        return 0;
    return em_area11_spawn_weather_001C1EA0();
}

/* 001C5C50: the area-title node (byte-matched; em_actor_roster), from
 * 0x1AE040 states 0 and 4. The card the node shows is the legacy em_hud
 * title: state 0's is armed by the manifest `areatitle` line at the area
 * read; state 4 (the room move, S12b) has no area read, so the new node's
 * card (its case 0: 0x12C ticks of D_002671C0[D_00289B40[700][0] + 701])
 * is armed here. The legacy card is keyed on the area byte alone. */
static int w_001C5C50(void *ctx)
{
    (void)ctx;
    if (s_pool_mode != POOL_ROSTER)
        return unmirrored(UM_001C5C50_LEGACY_WORLD);
    int rc = em_actor_roster_spawn_001C5C50(&s_pool, &s_state, em_area11_bind_roster, NULL, NULL);
    if (rc >= 0 && s_entry_state == 4)
        em_hud_area_title(s_state.d810700);
    return rc;
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
 *   001D1C50           both   em_rcl_001D1C50 on the render context (census
 *                             L32); em_render_001D1C50 without it
 *   001C1D00(0x8101D0) both   em_render_001C1D00 (not bound: RENDER_CONTEXT.md 8.4)
 *   001AFD70(mode)     both   em_actor_pool_walk_001AFD70 (S10b): mode 0
 *                             in 5E0, modes 1 and 2 in 6B0
 *   0015C160           both   unmirrored
 *   001F0360           both   unmirrored
 *   0018B9C0(camera)   5E0    em_camera_0018B9C0
 *                      6B0    em_camera_0018B9C0_opening
 *   001AAD00           both   em_collision_world_close_out_001AAD00 (the
 *                             nine list passes, then the list block; census
 *                             L07/L08); unmirrored without a roster
 *   001D1EA0(1)        both   em_rcl_001D1EA0(1) on the render context, then
 *                             em_render_001D1EA0(1) (the presentation)
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
        /* The original runs 0015BCF0 in this variant too. While the opening
         * runtime owns the player its pose comes from the opening's bank
         * (design risk 2, the interim opening path). Otherwise (WP-4: an
         * AREA11 interaction's 3B8D = 3 or 2) the player stage runs, so the
         * interaction host's shared player worker (0015B130 takeover,
         * scripted animation, 00182DF0 release) runs at its original
         * position, between 001AFD70(1) and 001AFD70(2). */
        if (!em_opening_runtime_busy())
            return em_player_0015BCF0();
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
    if (!in_variant() && !in_status_frame())
        return -1;
    /* The first level: the translation on the render context (census L32);
     * its 001D7C30 is the point-light tick. */
    if (rcl_live())
        return em_rcl_001D1C50() < 0 ? rcl_fault() : 0;
    return em_render_001D1C50();
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

/* 001F0360, the effect barrel: em_effect_manager's translation over the
 * render context in the first level (em_effects_live, census L26); a scene
 * without it keeps the reported no-effect binding. */
static int w_001F0360(void *ctx)
{
    (void)ctx;
    if (!in_variant())
        return -1;
    if (s_pool_mode == POOL_ROSTER && em_effects_live_attached()) {
        if (em_effects_live_001F0360() < 0)
            return em_scene_fault(&s_state, em_effects_live_fault() ? em_effects_live_fault() : 0x001F0360u,
                                  EM_SCENE_FAULT_WORKER_FAILED);
        return 0;
    }
    return unmirrored(UM_001F0360);
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

/* 001AAD00 (census L07/L08): the nine list-pass hooks over this frame's
 * live lists, then the list block (every class list, the interactive list
 * D_00275B5C/B64 included, is published and its cursor reset), over the
 * collision world the owners' 001B1B70 pushed into. A scene without an
 * original roster has no collision world: unmirrored there. */
static int w_001AAD00(void *ctx)
{
    (void)ctx;
    if (!in_variant())
        return -1;
    if (s_pool_mode != POOL_ROSTER)
        return unmirrored(UM_001AAD00);
    uint32_t fault = 0;
    if (em_collision_world_close_out_001AAD00(&s_state, (int16_t)em_frame_transition()->substate,
                                              &fault) < 0) {
        fprintf(stderr, "em_scene: 001AAD00: the collision close-out faulted at %08X\n",
                (unsigned)fault);
        return em_scene_fault(&s_state, fault ? fault : 0x001AAD00u, EM_SCENE_FAULT_WORKER_FAILED);
    }
    return 0;
}

/* 001D1EA0(1) ends both variants; status state 3 ends with 001D1EA0(0)
 * (0x1AE4B4), the frozen-world frame. */
static int w_001D1EA0(void *ctx, int a0)
{
    (void)ctx;
    if (!(in_variant() && a0 == 1) && !(in_status_frame() && a0 == 0))
        return -1;
    /* The first level: the translation on the render context (its world
     * flush pair and the kick 001CB800), then the renderer's presentation. */
    if (rcl_live() && em_rcl_001D1EA0(a0) < 0)
        return rcl_fault();
    return em_render_001D1EA0(a0);
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
 *   0020E060, 0020CDC0 -> the host's status route in AREA11 (below; WP-5),
 *                the legacy em_hud screen elsewhere
 *   001FBC50  -> w_001FBC50 (em_sfx_stop_all, its translation in em_sfx.h,
 *                then its two 00119828 calls)
 *   001FABB0, 00119828, 001FAE70 -> the stream lanes (em_stream_live,
 *                above; WP-8b)
 *   001AEDB0  -> em_frame_fade_full (001AEDB0's translation, em_fade.c)
 *   0018C0D0  -> camera_commit_original(&g.cam, a1) (em_camera.h)
 *   001AEE40  -> em_frame_fade_flash (em_fade.c), in state 5 and (since
 *                S12a) in the state-0 rebuild
 *   001D2830, 001E0CC0, 001D1EF0: unmirrored (reported). */

/* In AREA11 (the interaction host is loaded) every status screen runs the
 * original page layer of the host's status runtime (em_status_page over
 * the canonical B0/B1/C5/CC): a pending request (B0 != 0: the panel's
 * 00157F60 BATTERY request with D_008106D0 = the panel, a battery pickup's
 * 001C47A0 ITEM request) and a START/TRIANGLE screen (B0 == 0: the page
 * core's hub phase, the original em_status_hub with em_status_hub_ui and
 * 0020A7A0; its status-model draws are not translated yet, WP-5). Its
 * cold entry, pages and
 * 0020E0C0 exit are the original's, and the exit clears B0 and C5
 * (0020E080). Scenes without the host keep the legacy em_hud screen at
 * both positions, which clears B0/C5 on its close. */
static int s_status_host_route;

static int w_0020E060(void *ctx)
{
    (void)ctx;
    if (s_entry_state != 1 && s_entry_state != 4)
        return -1; /* only the state-1 classifier arm calls it */
    s_status_host_route = em_area11_interaction_host_status() != NULL;
    if (s_status_host_route)
        return em_area11_interaction_host_status_open() == 1 ? 0 : -1;
    em_area11_interaction_host_status_clear_route();
    em_hud_status_open();
    return 0;
}

static int w_0020CDC0(void *ctx)
{
    (void)ctx;
    if (!in_status_frame())
        return -1;
    if (s_status_host_route) {
        const EmPadUnpack *pad = em_frame_pad_block();
        /* D_00282157 through the same reader 0x1AE040 state 3 uses (the
         * disc-read phase, 0 at every tick boundary in the port: see
         * r_00282157). The page layer does not read it. */
        EmStatusInput input = {s_state.d810E74, s_state.d810E70, pad->lx, pad->ly,
                               r_00282157(NULL)};
        return em_area11_interaction_host_status_page(&input);
    }
    int closed = em_hud_status_tick(em_frame_input());
    if (closed < 0)
        return -1;
    if (closed) {
        em_hud_status_hide();
        s_state.req[EM_SCENE_REQ_B0] = 0;
        s_state.req[EM_SCENE_REQ_C5] = 0;
    }
    return closed;
}

/* 001FBC50 (src/func_001FBC50.c): em_sfx_stop_all is its translation
 * (em_sfx.c), then it ends with 00119828(0, 0x1999, 0x1999) and
 * 00119828(1, 0x1999, 0x1999), the IOP command 0x16 through the stream
 * lanes (w_00119828). */
static int w_001FBC50(void *ctx)
{
    em_sfx_stop_all();
    if (w_00119828(ctx, 0, 0x1999, 0x1999) < 0 || w_00119828(ctx, 1, 0x1999, 0x1999) < 0)
        return -1;
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
    if (a0 != D_CAMERA)
        return -1;
    if (s_entry_state == 4 && a1 == 1)
        return em_camera_live_bound() && em_camera_live_commit(1) == 0 ? 0 : -1;
    if (s_entry_state != 5)
        return -1;
    camera_commit_original(&g.cam, a1);
    return 0;
}

/* 001AEE40(a0): the transition flash (em_frame_fade_flash, its translation
 * in em_fade.c), from state 5 (0x20) and, since S12a, from the state-0
 * rebuild (4): after 001ADF50's full black it holds three ticks and fades
 * in (captured New Game load: fade mode 4 on the rebuild tick, then 5, 6). */
static int w_001AEE40(void *ctx, int16_t a0)
{
    (void)ctx;
    if (s_entry_state == 5 || s_entry_state == 0) {
        em_frame_fade_flash(a0);
        return 0;
    }
    return -1;
}

/* 001FC280 (NEARMISS, body-correct), the lanes' worker at the start of
 * 001FAE70: the area ambient loop. id = the high half of spawn record +0x20
 * (sra: 0xFFFF -> -1), or 0x44E in area 0x0B when D_00810788 == 0xFF; when
 * id differs from the cached D_00282160 it stops the old loop and starts
 * id. The cache is not modelled: an id of -1 changes nothing (the status
 * open's 001FBC50 sets the cache to -1) and any other id needs a loop the
 * port does not have: fault. (Every AREA11 record holds 0xFFFF1999, and the
 * captured D_00810788 is 0.) It ends with 00119828(0, lo, lo) and
 * 00119828(1, lo, lo), lo = the record's low half (0x1999 in AREA11). */
int em_scene_bindings_001FC280(void)
{
    const uint8_t *record = NULL;
    if (s_spawn_table_loaded) {
        const uint8_t *table = em_spawn_table_read(
            &s_spawn_table, EM_SPAWN_TABLE_ADDRESS + 4u * s_state.d810700, 4);
        uint32_t rooms = table ? (uint32_t)table[0] | (uint32_t)table[1] << 8 |
                                     (uint32_t)table[2] << 16 | (uint32_t)table[3] << 24
                               : 0;
        const uint8_t *room =
            rooms ? em_spawn_table_read(&s_spawn_table, rooms + 4u * s_state.d810701, 4) : NULL;
        uint32_t entries = room ? (uint32_t)room[0] | (uint32_t)room[1] << 8 |
                                      (uint32_t)room[2] << 16 | (uint32_t)room[3] << 24
                                : 0;
        if (entries)
            record = em_spawn_table_read(
                &s_spawn_table, entries + EM_SPAWN_RECORD_SIZE * s_state.d810702 + 0x20u, 4);
    }
    const uint8_t *d788 = em_scene_progress_at(&s_state, 0x00810788u, 1);
    if (!record || !d788)
        return -1;
    int32_t loop = (int32_t)((uint32_t)record[2] | (uint32_t)record[3] << 8) << 16 >> 16;
    if (s_state.d810700 == 0x0B && *d788 == 0xFF)
        loop = 0x44E;
    if (loop != -1) {
        fprintf(stderr, "em_scene: 001FC280 would start the area loop 0x%X, which the port does "
                        "not have\n", (unsigned)loop);
        return -1;
    }
    uint32_t lo = (uint32_t)record[0] | (uint32_t)record[1] << 8;
    if (w_00119828(NULL, 0, (int)lo, (int)lo) < 0 || w_00119828(NULL, 1, (int)lo, (int)lo) < 0)
        return -1;
    return 0;
}

/* 001FAE70(a0) (byte-matched, src/func_001FAE70.c), the area music cue:
 * em_stream_lanes_001FAE70 through em_stream_live (001FC280 above, the cue
 * from D_008106C8 / D_00810D38, the infected override over D_008104E4, the
 * 00122BB8 fade and the lane-0 restart). The frame machine binds its
 * state-5 call (the status close, a0 = 1); the state-0 area entry (a0 = 1),
 * the state-4 room move (a0 = 0), state 2's r == 1 and state 6 stay
 * reported (UM_001FAE70: the area-entry call also draws one rand(), and the
 * whole-game RNG order is unaudited). */
static int w_001FAE70(void *ctx, int a0)
{
    (void)ctx;
    if (s_entry_state != 5 || a0 != 1)
        return unmirrored(UM_001FAE70);
    return em_stream_live_001FAE70(a0);
}

int em_scene_bindings_001FAE70(int a0)
{
    return em_stream_live_001FAE70(a0);
}

int em_scene_bindings_001FABB0(void)
{
    return w_001FABB0(NULL);
}

int em_scene_bindings_001FBC50(void)
{
    return w_001FBC50(NULL);
}

int em_scene_bindings_report_001DA6A0(void)
{
    return unmirrored(UM_001DA6A0);
}

/* 001B0250 (em_spawn_001B0250, byte-matched) for the scripted camera's
 * restore (0022EEF0 / 001B7B30 sub 0 at the timeline's end) and 001B6BF0's
 * skip landing: D_008106C8 = the spawn record's +0x1C (the area 0x0B mask
 * with D_00810788). */
int em_scene_bindings_001B0250(void)
{
    const uint8_t *e788 = em_scene_progress_at(&s_state, 0x00810788u, 1);
    if (!s_spawn_table_loaded || !e788)
        return em_scene_fault(&s_state, EM_SPAWN_FN_001B0250, EM_SCENE_FAULT_NULL_WORKER);
    EmSpawnIo io;
    memset(&io, 0, sizeof io);
    io.d810700 = s_state.d810700;
    io.d810701 = s_state.d810701;
    io.d810702 = s_state.d810702;
    io.d810788 = *e788;
    io.d8106C8 = (int32_t)em_scene_req_u32(&s_state, EM_SCENE_REQ_C8);
    if (em_spawn_001B0250(&s_spawn_table, &io) < 0)
        return em_scene_fault(&s_state, io.fault.address, (EmSceneFaultCode)io.fault.code);
    em_scene_req_set_u32(&s_state, EM_SCENE_REQ_C8, (uint32_t)io.d8106C8);
    return 0;
}

/* 001FAD70(lane, fade, release): the lane's fade-out (001B0C00's three
 * calls, lanes 0, 1, 2). */
int em_scene_bindings_001FAD70(int32_t lane, int32_t fade, int32_t release)
{
    return em_stream_live_001FAD70(lane, fade, release);
}

/* The message service's stream workers (em_message_live.h), reached
 * through 001FD4C0 (the stream-table request of 001B82D0 ops 9..12):
 * 001FD470(mask) (byte-matched; bit 0 w_001FBC50, bit 1 001FABB0) and
 * 001FA790(lane, cue) on the lanes. 1 ok, 0 fault (the message worker
 * contract). */
int em_scene_bindings_001FD470(void *ctx, int32_t mask)
{
    (void)ctx;
    return em_stream_live_001FD470(mask) < 0 ? 0 : 1;
}

int em_scene_bindings_00119828(void *ctx, int32_t ch, int32_t l, int32_t r)
{
    return w_00119828(ctx, ch, l, r);
}

int em_scene_bindings_001FA790(void *ctx, int lane, int32_t cue)
{
    (void)ctx;
    return em_stream_live_001FA790(lane, cue) < 0 ? 0 : 1;
}

/* ------------------------------------------- the room move (S12b; design 5)
 *
 * The AREA11 door's 001BC150 (em_door.c) starts the fade 001AEDE0(4, 0) and
 * posts B8 = 2 with B7 = its destination entry; while B8 is set the
 * classifier returns 0 and the world keeps ticking; at D_0028A9A0 == 2 the
 * 001AD010 core sets D_00810702 = B7 and +B = 4; the next tick 0x1AE040
 * state 4 re-places and falls into state 1 in the same tick. Its workers:
 *   001AFCF0          the S3 core (clears the request block: B8 = 0)
 *   0018AB00          em_sf_0018AB00 (em_scene_task.c; D_008106C6)
 *   001B07C0(1)       w_001B07C0 above (the spawn translation; the arrival)
 *   001C1DC0          the weather node for the new D_008106C8 (the old one
 *                     frees itself: em_area11_bindings.c tick_weather)
 *   0018D7B0(cam, 1), 0018C0D0(cam, 1)  the live camera's translations
 *                     (em_camera_live_solve / _commit, census L13)
 *   001AEE10(4, 0)    the fade-in (em_fade.c)
 *   001FAE70(0)       reported (UM_001FAE70)
 *   001C5C50          the new area-title node (the old one left on B8) */

static int w_0018AB00(void *ctx)
{
    (void)ctx;
    return s_entry_state == 4 ? em_sf_0018AB00(&s_state) : -1;
}

static int w_0018D7B0(void *ctx, uint32_t a0, int a1)
{
    (void)ctx;
    if (a0 != D_CAMERA || a1 != 1 || s_entry_state != 4 || !em_camera_live_bound())
        return -1;
    /* State 4 stores D_008101E4 = 0 just before this call (0x1AE0BC). The
     * frame core's byte is its view of the camera block's +0x04, whose one
     * storage is the live camera's (g.cam.top_mode, em_camera_live): the
     * view is stored at this, the first worker boundary after the store
     * (census L18: the fence door's program leaves it at 2). */
    g.cam.top_mode = s_state.d8101E4;
    return em_camera_live_solve(1);
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

/* 001FF080(a0, a1): game over's screen module 0x27 (above), or 001ADF50's
 * area read (1, 0) at +9 = 5, +A = 1 (S12a, "the load arms"). 001AD1A0's
 * module 3 is served inside w_001AD1A0. */
static int w_001FF080(void *ctx, int a0, int a1)
{
    (void)ctx;
    if (a0 == 1 && a1 == 0 && in_task_step(5, 1)) {
        if (area_read() < 0)
            return -1;
        s_state.d275BD8 = 0;
        return 0;
    }
    if (a0 != 0 || a1 != 0x27 || !in_game_over())
        return -1;
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
    w->trace = em_frame_trace_env() || log_file() ? bindings_trace : NULL;
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

    w->w_001FC9B0 = w_001FC9B0;
    w->w_001B07C0 = w_001B07C0;
    w->w_001B6990 = w_001B6990;
    w->w_001D19E0 = um_001D19E0;
    w->w_001C1DC0 = w_001C1DC0;
    w->w_00199C50 = um_00199C50;
    w->w_001AEE40 = w_001AEE40;
    w->w_001FAE70 = w_001FAE70;
    w->w_001C5C50 = w_001C5C50;
    w->w_001D1EF0 = um_001D1EF0;

    /* Status screen (S11b). */
    w->w_0020E060 = w_0020E060;
    w->w_001FBC50 = w_001FBC50;
    w->w_001FABB0 = w_001FABB0;
    w->w_00119828 = w_00119828;
    w->w_001D2830 = um_001D2830;
    w->w_0020CDC0 = w_0020CDC0;
    w->w_001E0CC0 = w_001E0CC0;
    w->w_001AEDB0 = w_001AEDB0;
    w->w_0018C0D0 = w_0018C0D0;

    /* The room move (S12b): 0x1AE040 state 4. */
    w->w_0018AB00 = w_0018AB00;
    w->w_0018D7B0 = w_0018D7B0;

    /* Game over (S11b): 001AD4E0 and 001ADF00. */
    w->w_001D2880 = um_001D2880;
    w->w_001FF080 = w_001FF080;
    w->w_001AEE10 = w_001AEE10;
    w->w_001FA790 = w_001FA790;
    w->w_001ABF90 = w_001ABF90;
    w->w_001AEDE0 = w_001AEDE0;
    w->w_001FAB50 = w_001FAB50;
    w->s_00810D38 = s_00810D38;
    w->w_001AEBA0 = w_001AEBA0;
    w->w_001AB790 = w_001AB790;

    /* The load arms (S12a): 001ACEC0 +8 = 0/1, 001AD360, 001ADF50. */
    w->w_001AD1A0 = w_001AD1A0;
    w->w_001AD230 = w_001AD230;
    w->s_00275C78 = s_00275C78;
    w->s_00821058 = s_00821058;
    w->w_001AED80 = w_001AED80;
    w->w_0021B180 = w_0021B180;
    w->w_0021B550 = w_0021B550;
    w->w_0021B840 = w_0021B840;
}

/* ------------------------------------------- area-change request (S12a) */

int em_scene_request_area_change_001B0C60(int a, int b, int c)
{
    bindings_init();
    s_state.spad3B8D = 3;
    bind_trace(0x001B0C60u, 0x001B0C00u, 4, 0, 0, 0);
    /* 001B0C00(4): 001AEDE0(4, 0), then 001FAD70(channel, 4, 1) x3. */
    bind_trace(0x001B0C00u, 0x001AEDE0u, 4, 0, 0, 0);
    em_frame_fade_start_colour(1, 4, 0);
    for (uint32_t channel = 0; channel < 3; ++channel) {
        bind_trace(0x001B0C00u, 0x001FAD70u, channel, 4, 1, 0);
        if (em_stream_live_001FAD70((int32_t)channel, 4, 1) < 0)
            return em_scene_fault(&s_state, 0x001FAD70u, EM_SCENE_FAULT_WORKER_FAILED);
    }
    s_state.req[EM_SCENE_REQ_B8] = 1;
    s_state.req[EM_SCENE_REQ_B5] = (uint8_t)a;
    s_state.req[EM_SCENE_REQ_B7] = (uint8_t)c;
    s_state.req[EM_SCENE_REQ_B6] = (uint8_t)b;
    return 0;
}

int em_scene_bindings_pool_census(void)
{
    if (s_pool_mode != POOL_ROSTER)
        return -1;
    int n = 0;
    for (const EmActor *a = s_pool.head; a && n <= EM_ACTOR_POOL_CAPACITY; a = a->next)
        ++n;
    return n;
}

int em_scene_bindings_pool_count(uint32_t callback)
{
    if (s_pool_mode != POOL_ROSTER)
        return -1;
    int n = 0, walked = 0;
    for (const EmActor *a = s_pool.head; a && walked <= EM_ACTOR_POOL_CAPACITY; a = a->next, ++walked)
        n += a->callback == callback;
    return n;
}

uint32_t em_scene_bindings_pool_address(const void *actor)
{
    return em_actor_pool_address(&s_pool, (const EmActor *)actor);
}

const char *em_scene_bindings_pool_binding(uint32_t callback)
{
    if (s_pool_mode != POOL_ROSTER)
        return NULL;
    int walked = 0;
    for (const EmActor *a = s_pool.head; a && walked <= EM_ACTOR_POOL_CAPACITY; a = a->next, ++walked)
        if (a->callback == callback)
            return em_area11_node_binding(a);
    return NULL;
}

void em_scene_bindings_fixture_loaded(EmTask *record)
{
    if (!record)
        return;
    bindings_init();
    if (roster_scene())
        fixture_commit_area11();
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
    if (em_scene_faulted(&s_state)) {
        /* fail-stop: the task does nothing after a fault. The headless
         * drivers' after-frame hooks run inside the tick, so they are told
         * here instead (test instrumentation; inert when inactive). */
        em_opening_control_test_scene_stopped();
        em_level_smoke_test_scene_stopped();
        return;
    }
    EmTask *self = em_task_current();
    s_user = self ? self->user : NULL;
    /* D_00810E74/E70/E50 as step C left them this frame (design 3.2). */
    em_frame_scene_input(&s_state);
    EmFrameTrace *t = em_frame_trace_env();
    if (t)
        em_frame_trace_tick_begin(t, em_frame_counter());
    log_tick_begin();
    int rc = em_sf_001ACEC0(&s_state, s_user, &s_workers);
    log_tick_end(rc);
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
