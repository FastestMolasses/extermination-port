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
 *     script host's 001B82D0 (the opening's included), 001AFCF0, and
 *     001AE6B0's 3B91 1 -> 2 promotion); they write it where the original
 *     does, inside the world frame's 001AFD70 walk, so a selector written
 *     during frame N picks frame N+1's variant. The input words D_00810E74,
 *     D_00810E70 and D_00810E50 are written at the start of every tick by
 *     em_frame_scene_input (em_frame.c), the one translation of step C;
 *     spad 3B92 is canonical too (lead decision D5; the script host's
 *     001B82D0 writes it);
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
 *     and 001ADF50 (001FF080(1, 0) through the loader task, the load veil);
 *     the state-0 rebuild places the player with 001B07C0(0) over the
 *     exported spawn table (AREA11; see "spawn placement") and flashes the
 *     transition in with 001AEE40(4); 001AD010's area change (+9 = 5) is
 *     served the same way (em_scene_request_area_change_001B0C60 is the
 *     translated request). See "the load arms" below;
 *   - spad 3B90 (001ACEC0 writes 2 every tick) and C4 are forwarded to the
 *     step-D letterbox gate at the end of every task tick (design 2.1);
 *   - the screen-module loader (H7, docs/MODULE_LOADER.md): booted once at
 *     start-up over this file's s_state (D_00275BD8, spad 3B90, the
 *     D_00810CA4 / CA6 progress bytes) and the stream lanes' D_00282157;
 *     its slot-2 task runs module 0x21's load for the status runtime (see
 *     "the screen-module loader" at the end of this file);
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
#include "game/em_area01_terminal_status.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_actor_pool.h"
#include "game/em_actor_roster.h"
#include "game/em_player_closure_live.h"
#include "game/em_area11_bindings.h"
#include "game/em_area_title.h"
#include "game/em_background_live.h"
#include "game/em_effects_live.h"
#include "game/em_point_light.h"
#include "game/em_snow_runtime.h"
#include "game/em_aim_fire_binding.h"
#include "game/em_aim_fire_runtime.h"
#include "game/em_aim_fire_tables.h"
#include "game/em_equipment_live.h"
#include "game/em_weapon.h"
#include "game/em_indicator_bind_live.h"
#include "game/em_area11_boxes.h"
#include "game/em_area11_door.h"
#include "game/em_area11_roger.h"
#include "game/em_area11_script_host.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_camera.h"
#include "game/em_camera_live.h"
#include "game/em_render_context_live.h"
#include "game/em_load_veil_live.h"
#include "game/em_owner_draw_live.h"
#include "game/em_shadow_live.h"
#include "game/em_chain_page_live.h"
#include "game/em_shadow_original.h"
#include "game/em_ee_float.h"
#include "game/em_sdk_math_original.h"
#include "game/em_collision_world.h"
#include "game/em_door.h"
#include "game/em_game.h"
#include "game/em_frame.h"
#include "game/em_frame_trace.h"
#include "game/em_frontend.h"
#include "game/em_pad_actuator.h"
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
#include "game/em_player_draw_live.h"
#include "game/em_player_misc_workers.h"
#include "game/em_roger_actor_original.h"
#include "game/em_startup_load_gaps.h"
#include "game/em_module_loader.h"
#include "game/em_static_world_live.h"
#include "game/em_area01_arrival.h"
#include "game/em_area01_state.h"
#include "game/em_area01_live.h"
#include "game/em_anim_runtime_rest.h"
#include "game/em_world_textures_live.h"
#include "game/em_script_host_workers.h"
#include "em_settings.h"

/* ------------------------------------------------------------ storage */

static EmSceneState s_state;
static EmArea01State s_area01_state;
static EmArea01Live s_area01_live;
static int arrival_scene(void);
static uint32_t *s_area01_pose_scratch[3];
static int s_area01_scratch_bound;
static uint64_t s_area_resource_epoch;
static EmSceneWorkers s_workers;
static int s_ready;

/* The actor pool (design 3.1: owned here) and the AREA11 roster
 * (assets/scene_snow/roster.emro, written by tools/export_area11_roster.py
 * from the user's own ELF and overlay; loaded once). */
static EmActorPool s_pool;
static EmActorRoster s_roster;
static int s_roster_loaded;
#define AREA11_ROSTER_PATH AREA11_SCENE_DIR "/roster.emro"
/* AREA01 sub 0's roster (placements 0x82BD50, deferred groups 0x828A00 and
 * 0x829220; tools/export_area01_tables.py) and cell directory *0x70003250
 * (tools/export_area01_level.py), for the level exit's arrival. */
static EmActorRoster s_roster01;
static int s_roster01_loaded;
#define AREA01_ROSTER_PATH AREA01_SCENE_DIR "/roster.emro"
#define AREA01_CELLS_PATH AREA01_SCENE_DIR "/area01_cells.bin"

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

/* 001FC280's ambient loop (see em_scene_bindings_001FC280): D_00282160, the
 * loop's id (-1: none), D_00282164 its 001FB9F0 handle, D_00282168..70 its
 * request words. */
static int32_t s_d282160 = -1;
static int32_t s_d282164;
static int32_t s_d282168, s_d28216C, s_d282170;

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

/* The last w_0015C160 (census L29): its frame counter, the gate bytes
 * D_008102B1 / D_00810771, +0x214's record and the route, for the tick log. */
static struct {
    uint32_t frame;
    uint8_t b1, d771;
    uint32_t w214;
    int route;
} s_post_step = {UINT32_MAX, 0, 0, 0, 0};

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
    UM_0015C160,
    UM_0015C160_UNPOSED,
    UM_001F0360,
    UM_001AAD00,
    UM_001E0CC0,
    UM_00200830,
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
    [UM_001D19E0] = {0x001D19E0u, "its callees skin_arena_init (001D2E20) and 001D7BB0 (the point-light pool "
                                  "and the room lists) run on the render context; 001D9720, 001DD940, "
                                  "001E0C30, 001D9060, 001D71F0 and the flag registrations after them have no "
                                  "port counterpart here (RENDER_CONTEXT.md 8.4)"},
    [UM_001C1DC0] = {0x001C1DC0u, "its 001D2830 registrations and 001C1E70..001C1F50 passes have no "
                                  "port counterpart; only the AREA11 roster pool gets the 001C1EA0 "
                                  "weather node (001C1EA0 over D_008106C8, em_area11_bindings.c)"},
    [UM_00199C50] = {0x00199C50u, "no port counterpart"},
    [UM_001FAE70] = {0x001FAE70u, "area music cue from state 2 (0022A650 == 1) or state 6; not "
                                  "mirrored there. The state-0 area entry, the state-4 room move "
                                  "and the state-5 status close are bound (w_001FAE70)"},
    [UM_001C5C50_LEGACY_WORLD] = {0x001C5C50u, "scene without an original roster: no area-title "
                                               "node (legacy em_hud area title)"},
    [UM_001D1EF0] = {0x001D1EF0u, "before the area's render-context bind (the New Game bring-up "
                                  "001ACEC0 / 001AD360): its 001D1C50 projects the camera pool's "
                                  "D_00810610, whose view the area load hands over (RENDER_CONTEXT.md "
                                  "section 9)"},
    [UM_001CB590] = {0x001CB590u, "current actor stored; its anim_bone_array_setup tail has no port "
                                  "counterpart (the port's bone palettes are per model)"},
    [UM_0015C160] = {0x0015C160u, "player post-step in a scene without the shadow binding (not the "
                                  "first level, whose post-step is em_shadow_live): no shadow, and the "
                                  "port draws the player from its draw list"},
    [UM_0015C160_UNPOSED] = {0x0015C160u, "player post-step while the player record does not hold the "
                                          "displayed pose (the pose source has not started, or a port "
                                          "stand-in holds it): no shadow is computed from the record and "
                                          "the +0x4C draw is the port's own mesh draw"},
    [UM_001F0360] = {0x001F0360u, "effect-manager barrel (001F6210 .. 001F0720); no port counterpart"},
    [UM_001AAD00] = {0x001AAD00u, "scene without an original roster: no collision world, so its "
                                  "nine list-pass hooks and class lists have no port counterpart"},
    [UM_001E0CC0] = {0x001E0CC0u, "status-close draw-mode reset in a scene without the render "
                                  "context (the first level runs em_rcl_001E0CC0)"},
    [UM_00200830] = {0x00200830u, "001AD1A0: VIF1 DMA of the library packet D_0028A564 (slot 0x35, "
                                  "the boot's module 0x1B, which the port does not load; its texels "
                                  "are the port's disc export)"},
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
/* 001D19E0 (the render reset at the area load, src/func_001D19E0.c):
 * skin_arena_init, the callee the object units need (the skin records' VIF
 * codes and GIF tags, docs/OWNER_DRAW.md "Binding"), and 001D7BB0 (the
 * point-light pool's reset, em_point_light_reset over the context's
 * +0x210.., then its 001F68B0 / 001F6E40: the room lists registered
 * through 001D7FA0, em_effects_live_room_lights; docs/AREA11_POINT_LIGHT.md).
 * The callees between them (001D9720 .. 001D71F0) and after stay
 * unmirrored. */
static int um_001D19E0(void *ctx)
{
    (void)ctx;
    if (em_rcl_loaded()) {
        if (em_rcl_skin_arena_init() < 0) return -1;
        EmPointLightPool *pool = em_rcl_point_lights();
        if (!pool) return -1;
        em_point_light_reset(pool);
        if (em_effects_live_room_lights(&s_state) < 0)
            return em_scene_fault(&s_state, 0x001D7BB0u, EM_SCENE_FAULT_WORKER_FAILED);
        g.point_lights_loaded = 1;
    }
    return unmirrored(UM_001D19E0);
}
static int um_00199C50(void *ctx) { (void)ctx; return unmirrored(UM_00199C50); }
static int rcl_live(void);
static int rcl_fault(void);
/* 001D1EF0 (byte-matched), the tear-down frame: 001D1C50, 001D2830(3, 1),
 * 001D1EA0(0) on the render context (em_rcl_001D1EF0); flag 3 selects the
 * black clear at this iteration's step V (001D2300 clears it). Its kick
 * sends the chain page: the renderer draws it (em_render_001D1EF0). Before
 * the area bind (the New Game bring-up) the frame head's views are not
 * handed over: reported. */
static int w_001D1EF0(void *ctx)
{
    (void)ctx;
    if (!rcl_live())
        return unmirrored(UM_001D1EF0);
    if (em_rcl_001D1EF0() < 0)
        return rcl_fault();
    return em_render_001D1EF0();
}
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

/* 001D2830(a0, a1) on the render context (em_rcl_001D2830): the frame
 * machine's status frames and the task chain's (3, 1) registrations (flag 3,
 * cleared by main-loop step V 001D2300). The context is loaded from the
 * start (main.c), so these run before the area bind too. */
static int w_001D2830(void *ctx, int a0, int a1)
{
    (void)ctx;
    return em_rcl_001D2830(a0, a1) < 0 ? rcl_fault() : 0;
}

/* 001E0CC0 (the status close): on the render context in the first level. */
static int w_001E0CC0(void *ctx)
{
    (void)ctx;
    if (rcl_live())
        return em_rcl_001E0CC0() < 0 ? rcl_fault() : 0;
    return unmirrored(UM_001E0CC0);
}
/* 001D2880 (001AD4E0 step 0 and 001ADF00): em_rcl_001D2880 on the render
 * context (test-render-context-live-reference executes the original). */
static int w_001D2880(void *ctx)
{
    (void)ctx;
    return em_rcl_001D2880() < 0 ? rcl_fault() : 0;
}
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
 * tools/test_room_move_reference.py. `counter` is the main-loop counter
 * 0x70003B64 at the tick start, the clock of EM_RAND_TRACE's lines
 * (tools/rand_order.py). It never changes behaviour. */
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
    uint32_t counter;   /* 0x70003B64 at the tick start (EM_RAND_TRACE's clock) */
    uint8_t loader[27]; /* the loader's record and bytes after the previous frame */
    uint8_t pad[5];     /* D_00810E40 +0x16, +0x18, +0x19, +0x28/+0x29 after the previous frame */
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

/* The block *D_00275888 at its original offsets. */
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
    put_le(&p, s_veil.w14, 4);
    put_le(&p, s_veil.w18, 4);
}

static void log_hex(FILE *f, const uint8_t *b, size_t n)
{
    fputc('"', f);
    for (size_t i = 0; i < n; ++i)
        fprintf(f, "%02x", b[i]);
    fputc('"', f);
}

/* EM_LOG_AIM_RECORDS=1: the tick log's "aimrec" key (test logging only). */
static int aim_records_logged(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("EM_LOG_AIM_RECORDS");
        on = v && v[0] == '1';
    }
    return on;
}

static void log_tick_begin(void)
{
    if (!log_file())
        return;
    memset(&s_tick, 0, sizeof s_tick);
    s_tick.r_0021B550 = s_tick.r_001AD230 = -2;
    s_tick.fade = em_frame_transition()->substate;
    s_tick.counter = em_frame_counter();
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
    /* The screen-module loader after the previous frame's slot-2 dispatch
     * (it runs after this task in em_task_dispatch): the slot record +0,
     * +8..+0x1F, D_00275BD8 and D_00282157, the first 27 bytes of
     * em_module_loader_snapshot (docs/MODULE_LOADER.md Binding item 9). */
    {
        EmModuleLoader *ml = em_module_loader_live();
        static uint8_t snap[EM_MODULE_LOADER_SNAPSHOT_SIZE];
        const EmTask *slot2 = em_task_slot(EM_MODULE_LOADER_TASK_SLOT);
        em_module_loader_snapshot(ml, slot2, snap);
        memcpy(s_tick.loader, snap, sizeof s_tick.loader);
    }
    /* The pad block's rumble bytes after the previous frame's step I
     * (001B5B70 counts the duration down after the task), the route rows'
     * post-frame sample of them (docs/DAMAGE.md section 8). */
    {
        const uint8_t *pad = em_pad_actuator_block();
        if (pad) {
            s_tick.pad[0] = pad[0x16];
            s_tick.pad[1] = pad[0x18];
            s_tick.pad[2] = pad[0x19];
            s_tick.pad[3] = pad[0x28];
            s_tick.pad[4] = pad[0x29];
        }
    }
}

/* check_static_world samples every STATIC_SAMPLE_EVERY-th 001C1D00 call. */
#define STATIC_SAMPLE_EVERY 400u
static uint32_t s_static_logged;

/* Frame captures keyed on the log's tick (em_scene_bindings.h,
 * tools/test_fb2_pixels.py). Test instrumentation only. */
enum { CAPTURE_MAX = 32 };
static struct {
    uint32_t tick;
    char path[512];
} s_capture[CAPTURE_MAX];
static int s_capture_count, s_capture_env;

uint32_t em_scene_bindings_log_tick_next(void) { return s_log_tick; }

void em_scene_bindings_capture_tick(uint32_t tick, const char *path)
{
    if (!path || !path[0] || s_capture_count >= CAPTURE_MAX) {
        fprintf(stderr, "fb capture: tick %u not queued (%s)\n", (unsigned)tick,
                path && path[0] ? "too many captures" : "no path");
        return;
    }
    s_capture[s_capture_count].tick = tick;
    snprintf(s_capture[s_capture_count].path, sizeof s_capture[0].path, "%s", path);
    ++s_capture_count;
}

static void capture_env(void)
{
    if (s_capture_env)
        return;
    s_capture_env = 1;
    const char *spec = getenv("EM_FB_CAPTURE_TICKS");
    while (spec && *spec) {
        const char *end = strchr(spec, ';');
        size_t n = end ? (size_t)(end - spec) : strlen(spec);
        char item[600];
        if (n < sizeof item) {
            memcpy(item, spec, n);
            item[n] = 0;
            char *colon = strchr(item, ':');
            if (colon && colon != item) {
                *colon = 0;
                em_scene_bindings_capture_tick((uint32_t)strtoul(item, NULL, 10), colon + 1);
            }
        }
        spec = end ? end + 1 : NULL;
    }
}

static void capture_check(uint32_t tick)
{
    capture_env();
    for (int i = 0; i < s_capture_count; ++i)
        if (s_capture[i].tick == tick) {
            em_gfx_request_capture(em_frame_gfx(), s_capture[i].path);
            fprintf(stderr, "fb capture: tick %u -> %s\n", (unsigned)tick, s_capture[i].path);
        }
}

/* AREA01 recording observations only. No transaction is opened and no
 * capture bytes are installed. Unknown canonical spans are explicit nulls. */
static void log_area01(FILE *f)
{
    if (s_state.d810700 != 1 && !s_area01_live.bound)
        return;
    static const uint32_t nodes[] = {0x7B0390, 0x7B0680, 0x7ABD10, 0x7AC000, 0x7AC5E0, 0x7A5930,
                                     0x7A70B0, 0x7A7690, 0x7A7C70, 0x7B1240, 0x7B1530};
    fputs(", \"a01\": {\"owners\": {", f);
    for (unsigned i = 0; i < sizeof nodes / sizeof nodes[0]; ++i) {
        uint32_t at = nodes[i];
        EmActor *a = &s_pool.records[(at - EM_ACTOR_POOL_BASE) / EM_ACTOR_RECORD_SIZE];
        uint8_t raw[EM_ACTOR_RECORD_SIZE], head[16], pos[12], block[16], timer[20];
        int ok = 1;
        if (s_area01_live.bound && a->allocated) {
            EmArea01ActorView *v = &s_area01_live.actors;
            ok = em_area01_actor_view_snapshot(v, at, 16, head) == 0 &&
                 em_area01_actor_view_snapshot(v, at + 0xB0, 12, pos) == 0 &&
                 em_area01_actor_view_snapshot(v, at + 0x1F0, 16, block) == 0 &&
                 em_area01_actor_view_snapshot(v, at + 0x2DC, 20, timer) == 0;
        } else {
            em_actor_pool_record_image(&s_pool, a, raw);
            memcpy(head, raw, 16);
            memcpy(pos, raw + 0xB0, 12);
            memcpy(block, raw + 0x1F0, 16);
            memcpy(timer, raw + 0x2DC, 20);
        }
        fprintf(f, "%s\"%u\": ", i ? ", " : "", at);
        if (!ok) {
            fputs("null", f);
            continue;
        }
        fputc('[', f);
        log_hex(f, head, 16);
        fputs(", ", f);
        log_hex(f, pos, 12);
        fputs(", ", f);
        log_hex(f, block, 16);
        fputs(", ", f);
        log_hex(f, timer, 20);
        fprintf(f, ", %u]", a->callback);
    }
    uint32_t hp;
    memcpy(&hp, &g.status.health, 4);
    fprintf(f, "}, \"hp\": %u, \"progress\": [", hp);
    /* Each byte from its one port owner: the canonical progress region,
     * or the named mirror g.opening_complete (D_00810811, not migrated).
     * A byte with neither has no port storage: live code reaches the
     * region only through em_scene_progress_at (which refuses it) or a
     * named mirror, so it keeps 001AF2C0's reset value 0 (the region's
     * memset). */
    static const uint32_t windows[][2] = {{0x8107D8, 64}, {0x810758, 8}, {0x810860, 64}, {0x810D00, 32}};
    for (unsigned i = 0; i < 4; ++i) {
        uint8_t bytes[64];
        for (uint32_t k = 0; k < windows[i][1]; ++k) {
            const uint32_t at = windows[i][0] + k;
            const uint8_t *p = em_scene_progress_at(&s_state, at, 1);
            bytes[k] = p ? *p : at == 0x00810811u ? g.opening_complete : 0;
        }
        if (i)
            fputs(", ", f);
        log_hex(f, bytes, windows[i][1]);
    }
    fputs("]}", f);
}

static void log_tick_end(int rc)
{
    FILE *f = log_file();
    if (!f)
        return;
    capture_check(s_log_tick);
    uint8_t post[LOG_SNAP], veil[LOG_VEIL];
    log_snapshot(post);
    log_veil(veil);
    fprintf(f, "{\"tick\": %u, \"counter\": %u, \"rc\": %d, \"fade\": %d, \"pre\": ", s_log_tick++,
            s_tick.counter, rc, s_tick.fade);
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
        fprintf(f, ", \"msg_pre\": [%u, %u, %u], \"loader_pre\": ", message[0], message[1],
                message[2]);
        log_hex(f, s_tick.loader, sizeof s_tick.loader);
        fputs(", \"cam4\": ", f);
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
         * the fog record's save slot 0 +0x120..+0x13F (the status screen's
         * 0021BAC0(0)), D_00275690 / D_00275694, and the camera pool's
         * D_00810610 (the view the NEXT frame head projects); then the list
         * cursor +0x08, +0x98 / +0x9C (step W's field, step B's slot) and
         * the first 0x70 bytes of the other slot's main list (the one the
         * previous iteration's step V 001D2300 built and kicked: seven tags
         * in a world frame, six in a status frame).
         * tools/test_level_smoke.py check_render_context. */
        {
            static const struct { uint32_t offset, size; } k_rctx[] = {
                {0x0C, 4}, {0x174, 4}, {0xA0, 0x60}, {0x2468, 4}, {0x2380, 0x40},
                {0x23C0, 0x40}, {0x2240, 0x40}, {0x24F0, 0x24}, {0x2450, 0x18}, {0x120, 0x20},
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
                const uint8_t *slot = em_rcl_bytes(EM_RCL_CONTEXT + 0x9C, 4);
                const uint32_t other = slot && slot[0] == 0 ? 1u : 0u;
                fputs(", ", f);
                log_hex(f, em_rcl_bytes(EM_RCL_CONTEXT + 0x08, 4), 4);
                fputs(", ", f);
                log_hex(f, em_rcl_bytes(EM_RCL_CONTEXT + 0x98, 8), 8);
                fputs(", ", f);
                log_hex(f, em_rcl_bytes(0x0028F700u + (other << 14), 0x70), 0x70);
                fputc(']', f);
            } else {
                fputs("null", f);
            }
        }
        /* The static world (docs/STATIC_WORLD.md section 7), null before its
         * first 001C1D00: the live draw's log (its frame, runs drawn, the
         * last run's start / end, the FNV-1a of its bytes and of its
         * triangles, the level / clip batches and triangles, the culled
         * vertices) and em_rcl_static_runs(); then, for every
         * STATIC_SAMPLE_EVERY-th 001C1D00 call made in this tick, its inputs
         * (the context, the scratchpad, D_00810610, D_00810700..702,
         * D_008101D0, D_00253560, D_00817240 and the skin records before
         * the call) and its
         * output (the channel-0 run, the channel-3 list and D_00253560
         * after). tools/test_level_smoke.py check_static_world re-executes
         * the original 001C1D00 over the inputs. */
        {
            EmStaticWorldLiveLog sl;
            em_static_world_live_log(&sl);
            const uint32_t runs = em_rcl_static_runs();
            if (runs)
                fprintf(f, ", \"static\": [%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u]", sl.frame, sl.runs,
                        sl.start, sl.end, sl.digest, sl.prim_digest, sl.batches[0], sl.batches[1],
                        sl.triangles[0], sl.triangles[1], sl.culled, runs);
            else
                fputs(", \"static\": null", f);
            const EmRclStaticSample *sm = em_rcl_static_sample();
            if (sm && sm->runs != s_static_logged && sm->runs % STATIC_SAMPLE_EVERY == 1u) {
                const uint8_t *run = em_rcl_bytes(sm->ch0_start, sm->ch0_end - sm->ch0_start);
                const uint8_t *ch3 = em_rcl_bytes(sm->ch3_start, sm->ch3_end - sm->ch3_start);
                const uint8_t *d253560 = em_rcl_bytes(0x00253560u, 0x90);
                if (run && ch3 && d253560) {
                    fprintf(f, ", \"static_sample\": [%u, ", sm->runs);
                    log_hex(f, sm->ctx, sizeof sm->ctx);
                    fputs(", ", f);
                    log_hex(f, sm->spad, sizeof sm->spad);
                    fputs(", ", f);
                    log_hex(f, sm->cam610, sizeof sm->cam610);
                    fputs(", ", f);
                    log_hex(f, sm->area, sizeof sm->area);
                    fputs(", ", f);
                    log_hex(f, sm->state, sizeof sm->state);
                    fputs(", ", f);
                    log_hex(f, sm->d253560, sizeof sm->d253560);
                    fputs(", ", f);
                    log_hex(f, sm->d817240, sizeof sm->d817240);
                    fputs(", ", f);
                    log_hex(f, sm->skin, sizeof sm->skin);
                    fprintf(f, ", %u, ", sm->ch0_start);
                    log_hex(f, run, sm->ch0_end - sm->ch0_start);
                    fprintf(f, ", %u, ", sm->ch3_start);
                    log_hex(f, ch3, sm->ch3_end - sm->ch3_start);
                    fputs(", ", f);
                    log_hex(f, d253560, 0x90);
                    /* AREA01: the dynamic table's identity and the call's
                     * output as it returned (EmRclStaticSample.dyn). */
                    if (sm->dyn) {
                        fprintf(f, ", %u, %u, %u, %u, ", sm->dyn, sm->dyn_word, sm->dyn_size, sm->dyn_digest);
                        log_hex(f, sm->chain, sizeof sm->chain);
                        fputs(", ", f);
                        log_hex(f, sm->chain_post, sizeof sm->chain_post);
                        fputs(", ", f);
                        log_hex(f, sm->ctx_post, sizeof sm->ctx_post);
                        fputs(", ", f);
                        log_hex(f, sm->spad3400_post, sizeof sm->spad3400_post);
                        fputs(", ", f);
                        log_hex(f, sm->d817240_post, sizeof sm->d817240_post);
                        fputs(", ", f);
                        log_hex(f, sm->d250F30, sizeof sm->d250F30);
                        fputs(", ", f);
                        log_hex(f, sm->d250F30_post, sizeof sm->d250F30_post);
                        uint32_t at = 0;
                        for (unsigned k = 0; k < 4; ++k) {
                            fprintf(f, ", %u, ", sm->span_start[k]);
                            log_hex(f, sm->spans + at, sm->span_size[k]);
                            at += sm->span_size[k];
                        }
                    }
                    fputc(']', f);
                }
            }
            if (sm) s_static_logged = sm->runs;
        }
        /* The live player record at the tick end, as the route rows sample
         * it (route_capture.py): +5, +1F0, +1F1, the clip +20C, the clock
         * +3C (float bits) and the ground owner +214 (its original record
         * address; 0 none); then +0x2F3 and +4 (the takeover's +4 = 4,
         * which the route rows do not sample). */
        const EmPlayerLiveActor *a = player_states_actor();
        uint32_t clock = em_live_u32(a, 0x3C);
        uint32_t ground = a->link_owner ? em_actor_pool_address(&s_pool, (const EmActor *)a->link_owner) : 0;
        fprintf(f, ", \"player\": [%u, %u, %u, %d, %u, %u, %u, %u]", em_live_u8(a, 5), em_live_u8(a, 0x1F0),
                em_live_u8(a, 0x1F1), (int)(int16_t)em_live_u16(a, 0x20C), clock, ground, em_live_u8(a, 0x2F3),
                em_live_u8(a, 4));
        /* The player's equipment links: +0x18 (0015C420's knife node) and
         * +0x20 (0015C310's gun node), original record addresses.
         * tools/test_level_smoke.py check_effects. */
        fprintf(f, ", \"links\": [%u, %u]", em_live_u32(a, 0x18), em_live_u32(a, 0x20));
        /* Read-only exploration observations of the floor service's fields. */
        fprintf(f, ", \"water\": [%u, %u, %u, %u], \"surface_y\": %u",
                em_live_u8(a, 0x23A), em_live_u8(a, 0x23C),
                em_live_u8(a, 0x23D), em_live_u8(a, 0x23E), em_live_u32(a, 0x250));
        /* The damage fields (docs/DAMAGE.md section 8; tools/
         * level_smoke_damage.py): the vitals +0x220 / +0x224 / +0x228 /
         * +0x22C (float bits, their one storage g.status / g.pd_*), the
         * record's +0x00, +0x0F, +0x20E (the port's g.pd_iframes), +0x235,
         * +0x234, +0x06, +0x07, +0x210 (the heartbeat count), +0x0D; the pad
         * block D_00810E40's +0x16, +0x18, +0x19 and +0x28 (the rumble);
         * then the effect nodes 0022BBC0 / 001F77B0: [record, +0x10,
         * +0x04, +0x0D]. */
        {
            uint32_t hp, pend, inf, pend_inf;
            memcpy(&hp, &g.status.health, 4);
            memcpy(&pend, &g.pd_pend_hp, 4);
            memcpy(&inf, &g.status.infection, 4);
            memcpy(&pend_inf, &g.pd_pend_inf, 4);
            const uint8_t *pad = em_pad_actuator_block();
            fprintf(f, ", \"dmg\": [%u, %u, %u, %u, %u, %u, %d, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, [", hp, pend,
                    inf, pend_inf, em_live_u8(a, 0), em_live_u8(a, 0x0F), g.pd_iframes, em_live_u8(a, 0x235),
                    em_live_u8(a, 0x234), em_live_u8(a, 6), em_live_u8(a, 7), em_live_u16(a, 0x210),
                    em_live_u8(a, 0x0D), pad ? pad[0x16] : 0u, pad ? pad[0x18] : 0u, pad ? pad[0x19] : 0u,
                    pad ? (unsigned)(pad[0x28] | pad[0x29] << 8) : 0u);
            int n = 0;
            for (const EmActor *e = s_pool.head; e; e = e->next)
                if (e->callback == 0x0022BBC0u || e->callback == 0x001F77B0u)
                    fprintf(f, "%s[%u, %u, %u, %u]", n++ ? ", " : "", em_actor_pool_address(&s_pool, e),
                            (unsigned)e->callback, e->u04[0], e->param);
            fprintf(f, "]], \"pad_pre\": [%u, %u, %u, %u]", s_tick.pad[0], s_tick.pad[1], s_tick.pad[2],
                    (unsigned)(s_tick.pad[3] | s_tick.pad[4] << 8));
        }
        /* The armed stance's sub-state bytes +6 / +7, the action code +230
         * (0015CBA0) and the player's +A0..+A8
         * and +B0..+B8 (float bits) as the camera's view of D_008102B0 holds
         * them at the tick end (CAMERA_LIVE.md section 7; null without the
         * live camera). tools/test_level_smoke.py check_aim_hold. */
        {
            const uint8_t *pl = em_camera_live_bound() ? em_camera_live_player_bytes() : NULL;
            const uint8_t *pv = pl ? pl + 0xA0 : NULL;
            fprintf(f, ", \"aim\": [%u, %u, %u", em_live_u8(a, 6), em_live_u8(a, 7), em_live_u32(a, 0x230));
            if (pv) {
                for (unsigned k = 0; k < 7; ++k) {
                    if (k == 3) continue;
                    uint32_t w;
                    memcpy(&w, pv + 4 * k, 4);
                    fprintf(f, ", %u", w);
                }
            }
            fputc(']', f);
        }
        /* The fire path at the tick end (AIM_FIRE.md section 5, the
         * aim_fire side run): the fire mode D_00810C61, the magazine
         * D_00810C62, the reserve D_00810CB4, the light D_00810D3C, the gun
         * node's +0x2E event (0 without the gun), the player's +0x274..+0x27F
         * (hex), then every allocated pool record running 0018ABA0 (the
         * impact marker), 001F5040 (the muzzle node), 001EA240 (an effect
         * node), 001F18C0 (the knife's trail) or 0021AAC0 / 0021A500 (the
         * cable reaction's nodes): its address, header +0x00..+0x0F (hex), +0x10 (the
         * behaviour), +0x28 (-1 when not held) and +0xB0..+0xB8 (float
         * bits). tools/test_level_smoke.py
         * check_aim_fire. */
        {
            const uint8_t *mode = em_weapon_fire_mode_byte(), *mag = em_weapon_mag_byte();
            const uint8_t *light = em_weapon_flashlight_byte();
            const int16_t *reserve = em_weapon_reserve_word();
            const uint32_t gun = em_live_u32(a, 0x20);
            const uint8_t *h2e = gun ? em_equipment_live_field(gun + 0x2Eu, 2, 0) : NULL;
            fprintf(f, ", \"fire\": [%u, %u, %u, %u, %u, \"", mode ? *mode : 0u, mag ? *mag : 0u,
                    reserve ? (unsigned)(uint16_t)*reserve : 0u, light ? *light : 0u, h2e ? (unsigned)(h2e[0] | h2e[1] << 8) : 0u);
            for (unsigned k = 0x274; k < 0x280; ++k) fprintf(f, "%02x", em_live_u8(a, k));
            fputs("\", [", f);
            int first = 1;
            for (const EmActor *r = s_pool.head; r; r = r->next) {
                if (!r->allocated || (r->callback != 0x0018ABA0u && r->callback != 0x001F5040u &&
                                      r->callback != 0x001EA240u && r->callback != 0x001F18C0u &&
                                      r->callback != 0x0021AAC0u && r->callback != 0x0021A500u))
                    continue;
                const uint32_t at = em_actor_pool_address(&s_pool, r);
                uint16_t h28v = 0;
                const int has28 = em_aim_fire_runtime_h28(r, &h28v);
                uint32_t pos[3];
                memcpy(pos, r->pos, sizeof pos);
                fprintf(f, "%s[%u, \"", first ? "" : ", ", at);
                for (unsigned k = 0; k < 16; ++k) fprintf(f, "%02x", ((const uint8_t *)&r->status)[k]);
                fprintf(f, "\", %u, %d, %u, %u, %u]", (unsigned)r->callback, has28 ? (int)h28v : -1, pos[0],
                        pos[1], pos[2]);
                first = 0;
            }
            fputs("]]", f);
        }
        /* The BRANCH side runs (LEVEL_SMOKE.md "The BRANCH side runs";
         * tools/level_smoke_branch.py), as the BRANCH captures' rows sample
         * them (decomp CAPTURES_C10.md "BRANCH"): the taken bits of area 11
         * (D_00810860 + 32 * 11, persistence uids 0..15), the item counts of
         * types 0x08, 0x10, 0x1B, 0x1E, 0x1F and 0x32 (D_00810CB8 + type),
         * then the records of the six optional items g0.1..g0.6 (pool
         * records 1..6) and of the boxes r3..r6 and drums r14 / r15 at their
         * route addresses: [address, +0x00..+0x3F, +0xB0..+0xCF] (hex); an
         * item owner's +0x00 / +0x02 / +0x04 / +0x05 / +0x0B are its typed
         * owner's (em_area11_interaction_host_pickup_header). */
        {
            static const uint32_t k_br_records[] = {
                0x7A5930u, 0x7A5C20u, 0x7A5F10u, 0x7A6200u, 0x7A64F0u, 0x7A67E0u, /* g0.1..g0.6 */
                0x7A7980u, 0x7A7C70u, 0x7A7F60u, 0x7A8250u, 0x7A99D0u, 0x7A9CC0u, /* r3..r6, r14, r15 */
            };
            static const uint8_t k_br_types[] = {0x08, 0x10, 0x1B, 0x1E, 0x1F, 0x32};
            unsigned taken = 0;
            for (unsigned p = 0; p < 16; ++p)
                if (em_pickup_taken((int)(0x0B00u | p)) == 1) taken |= 1u << p;
            fprintf(f, ", \"br\": [%u, [", taken);
            for (unsigned k = 0; k < sizeof k_br_types; ++k)
                fprintf(f, "%s%u", k ? ", " : "", (unsigned)em_pickup_item_count(k_br_types[k]));
            fputs("], [", f);
            static uint8_t image[EM_ACTOR_RECORD_SIZE];
            for (unsigned k = 0; k < sizeof k_br_records / sizeof k_br_records[0]; ++k) {
                const EmActor *r = &s_pool.records[(k_br_records[k] - 0x7A5640u) / 0x2F0u];
                em_actor_pool_record_image(&s_pool, r, image);
                (void)em_area11_interaction_host_pickup_header(r, image);   /* an item owner's own bytes */
                fprintf(f, "%s[%u, ", k ? ", " : "", (unsigned)k_br_records[k]);
                log_hex(f, image, 0x40);
                fputs(", ", f);
                log_hex(f, image + 0xB0, 0x20);
                fputc(']', f);
            }
            fputs("]]", f);
        }
        /* The AIM replays' whole-record view (EM_LOG_AIM_RECORDS=1, set by
         * tools/test_level_smoke_aim.py), as the AIM captures' rows sample
         * them (decomp CAPTURES_C10.md "AIM"): the live player record
         * +0x000..+0x31F (its image), the gun node (player +0x20) and the
         * knife node (+0x18) at +0x00..+0x3F, +0xA0..+0xCF and
         * +0x1F0..+0x21F with "--" for a byte the node does not model, the
         * status block D_00810130..+0x5F (the rows' ui_rec) and the processed
         * pad words D_00810E70 / D_00810E74 (the rows' held / pressed).
         * tools/test_level_smoke.py check_aim_records. */
        if (aim_records_logged()) {
            fputs(", \"aimrec\": [", f);
            log_hex(f, a->bytes, EM_PLAYER_ACTOR_SIZE);
            for (unsigned link = 0x20; ; link = 0x18) {
                const uint32_t node = em_live_u32(a, link);
                static const uint32_t spans[][2] = {{0x00, 0x40}, {0xA0, 0x30}, {0x1F0, 0x30}};
                fputs(", \"", f);
                for (unsigned s = 0; s < 3; ++s)
                    for (uint32_t k = 0; k < spans[s][1]; ++k) {
                        const uint8_t *b = node ? em_equipment_live_field(node + spans[s][0] + k, 1, 0) : NULL;
                        if (b) fprintf(f, "%02x", *b);
                        else fputs("--", f);
                    }
                fputc('"', f);
                if (link == 0x18) break;
            }
            const uint8_t *ui = em_status_runtime_ui_block(em_area11_interaction_host_status());
            fputs(", ", f);
            if (ui) log_hex(f, ui, 0x60);
            else fputs("null", f);
            fprintf(f, ", %u, %u]", (unsigned)s_state.d810E70, (unsigned)s_state.d810E74);
        }
        /* The stream lanes as the previous frame's step H left them (the
         * task runs before step H), as the C7 stream capture's main-loop-top
         * rows sample them (decomp docs/CAPTURES_C7.md section 1): D_00810E90, the read
         * phase / lane D_00282157 / 58, the active bytes D_00282154..56 and
         * each lane's +0x03; null before the lanes' boot.
         * tools/test_level_smoke.py check_director_beat. */
        {
            uint32_t st[9];
            if (em_stream_live_log(st))
                fprintf(f, ", \"stream\": [%u, %u, %u, %u, %u, %u, %u, %u, %u]", st[0], st[1], st[2], st[3], st[4],
                        st[5], st[6], st[7], st[8]);
            else
                fputs(", \"stream\": null", f);
        }
        log_area01(f);
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
     * barrel frames, gaps: always 0, see em_effects_live.h), its live nodes (address, +0x10, +0x04, +0x05, +0x0D
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
    /* The indicator children 001C5680 / 001C5760 bound by
     * em_indicator_bind_live (001C2360 / 001C22A0, 001C6380): record
     * address, +0x10, +0x04, +0x09, +0x0C, +0x0D, +0x44, +0x4C, the slot
     * words and the first slot's +0x90 matrix (float bits). A child whose
     * bind has not run is not listed. tools/test_level_smoke.py
     * check_indicator_children. */
    fputs(", \"children\": [", f);
    {
        int n = 0;
        for (const EmActor *a = s_pool.head; a; a = a->next) {
            EmIndicatorBindRecord r;
            if ((a->callback != 0x001C5680u && a->callback != 0x001C5760u) ||
                !em_indicator_bind_live_record(a, &r))
                continue;
            fprintf(f, "%s[%u, %u, %u, %u, %u, %u, %u, %u, [%u, %u, %u, %u], [", n++ ? ", " : "", r.address,
                    a->callback, a->u04[0], r.bones, r.count, a->param, r.model, r.method, r.slot[0], r.slot[1],
                    r.slot[2], r.slot[3]);
            for (int k = 0; k < 16; ++k) {
                uint32_t bits;
                memcpy(&bits, &r.world[k], 4);
                fprintf(f, "%s%u", k ? ", " : "", bits);
            }
            fputs("]]", f);
        }
    }
    fputc(']', f);
    /* The terminal 00827B10's record (em_area11_boxes_owner_*): its address,
     * +0x04, +0x09, +0x0C, +0x44, +0x4C, node 0's +0x90 (float bits;
     * 001C6380's, the carry's) and +0xB0..+0xB8 (float bits), or null while
     * it is not bound.
     * tools/test_level_smoke.py check_indicator_children (its 0x827E6C copy
     * into the child's slot). */
    fputs(", \"terminal\": ", f);
    {
        const EmActor *term = NULL;
        for (const EmActor *a = s_pool.head; a && !term; a = a->next)
            if (a->callback == 0x00827B10u) term = a;
        uint32_t model = 0, method = 0;
        float node[16];
        if (term && em_area11_boxes_owner_state(term, &model, &method) &&
            em_area11_boxes_owner_node(term, 0, node) == 0) {
            fprintf(f, "[%u, %u, %u, %u, %u, %u, [", em_actor_pool_address(&s_pool, term), term->u04[0],
                    term->bones, term->u0A[2], model, method);
            for (int k = 0; k < 16; ++k) {
                uint32_t bits;
                memcpy(&bits, &node[k], 4);
                fprintf(f, "%s%u", k ? ", " : "", bits);
            }
            uint32_t pos[3];
            memcpy(pos, term->pos, sizeof pos);
            fprintf(f, "], [%u, %u, %u]]", pos[0], pos[1], pos[2]);
        } else {
            fputs("null", f);
        }
    }
    /* The security gun 00825940, its cable 00827490 and the fan pair
     * 00827630 on their owners (em_area11_bindings_gun_fan_log): record
     * address, +0x10, +0x00, +0x04, +0x05, +0x09, +0x28, +0x34, +0x36,
     * +0x38 (bits), +0xC0..+0xCC (bits), the gun's bone 3 +0x78 (bits), its
     * +0x220 and its lamp's +0xA0 (bits). tools/test_level_smoke.py
     * check_gun_fan. */
    fputs(", \"gun_fan\": [", f);
    {
        int n = 0;
        for (const EmActor *a = s_pool.head; a; a = a->next) {
            EmArea11GunFanLog r;
            if (!em_area11_bindings_gun_fan_log(a, &r))
                continue;
            fprintf(f, "%s[%u, %u, %u, %u, %u, %u, %d, %d, %u, %u, [%u, %u, %u, %u], %u, %u, [%u, %u, %u, %u]]",
                    n++ ? ", " : "", r.address, r.callback, r.b00, r.b04, r.b05, r.b09, r.h28, r.h34, r.h36,
                    r.f38, r.rot[0], r.rot[1], r.rot[2], r.rot[3], r.bone3_78, r.w220, r.lamp_a0[0],
                    r.lamp_a0[1], r.lamp_a0[2], r.lamp_a0[3]);
        }
    }
    fputc(']', f);
    /* The AREA11 overlay owners chain step A11FIX bound
     * (em_area11_bindings_overlay_log): [address, +0x10, +0x00, +0x04,
     * +0x05, +0x2E, +0x30, +0x34, [the flame's +0x1F0..+0x1F8 bits], its
     * +0x210, on the published class-0xD list]; then the tracks whose
     * requested id (D_00281B70) is the flame's loop 0x413.
     * tools/level_smoke_overlay11.py. */
    fputs(", \"overlay11\": [", f);
    {
        int n = 0;
        for (const EmActor *a = s_pool.head; a; a = a->next) {
            EmArea11OverlayLog r;
            if (!em_area11_bindings_overlay_log(a, &r))
                continue;
            fprintf(f, "%s[%u, %u, %u, %u, %u, %u, %u, %u, [%u, %u, %u], %d, %d]", n++ ? ", " : "", r.address,
                    r.callback, r.b00, r.b04, r.b05, r.h2E, r.w30, r.w34, r.block[0], r.block[1], r.block[2], r.w210,
                    r.on_class_d);
        }
    }
    fputc(']', f);
    {
        int32_t requested[48], snapshot[48];   /* em_sfx_tables' 48 tracks */
        em_sfx_tables(requested, snapshot);
        int n413 = 0;
        for (int t = 0; t < 48; ++t)
            n413 += requested[t] == 0x413;
        fprintf(f, ", \"sfx413\": %d", n413);
    }
    /* The owner draws 001CAA00 of the last drawn frame (em_owner_draw_live):
     * record address, unit bytes, clip, the colour / lighting-row /
     * position-row / point-light-slot / rig-lane digests, the owner's point
     * (float bits) and its pose digest. tools/test_level_smoke.py
     * check_owner_units. */
    fputs(", \"owner_units\": [", f);
    {
        EmOwnerDrawLiveLog units[EM_OWNER_DRAW_LIVE_UNITS];
        const int nu = em_owner_draw_live_log(units, EM_OWNER_DRAW_LIVE_UNITS);
        for (int i = 0; i < nu; ++i)
            fprintf(f, "%s[%u, %u, %u, %u, %u, %u, %u, %u, [%u, %u, %u], %u, %u, %u]", i ? ", " : "",
                    units[i].record, units[i].bytes, units[i].clip, units[i].colour, units[i].light,
                    units[i].position, units[i].points, units[i].light_rig, units[i].point[0], units[i].point[1],
                    units[i].point[2], units[i].pose, units[i].face_bytes, units[i].face);
    }
    fputc(']', f);
    /* The level background's channel-3 list walk (em_background_live):
     * [drawn in this tick's frame, cumulative draws, the list's start, its
     * triangles, the vertex digest, the dmem upload the grid program read
     * (0x000, 0x081, 0x102, 0x200..0x207: 44 words)].
     * tools/test_level_smoke.py check_background. */
    {
        EmBackgroundLiveLog bl;
        em_background_live_log(&bl);
        fprintf(f, ", \"background\": [%d, %u, %u, %u, %u, [", bl.draws && bl.frame == em_frame_counter(),
                bl.draws, bl.start, bl.prims, bl.digest);
        for (unsigned k = 0; k < 44u; ++k) fprintf(f, "%s%u", k ? ", " : "", bl.upload[k / 4u][k % 4u]);
        fputs("]]", f);
    }
    /* The area-title nodes 001C5930 (em_area_title): per node [record,
     * +0x04, +0x05, +0x06, +0x28, +0x2A, +0x1F0, +0x1F4], then the
     * cumulative count of its 001CC1E0 lines. tools/test_level_smoke.py
     * check_area_title. */
    fputs(", \"title_nodes\": [[", f);
    {
        int walked = 0, n = 0;
        for (const EmActor *a = s_pool_mode == POOL_ROSTER ? s_pool.head : NULL;
             a && walked <= EM_ACTOR_POOL_CAPACITY; a = a->next, ++walked) {
            EmAreaTitleRecord tr;
            if (a->callback != 0x001C5930u || !em_area_title_record(a, &tr)) continue;
            fprintf(f, "%s[%u, %u, %u, %u, %d, %d, %d, %d]", n++ ? ", " : "", em_actor_pool_address(&s_pool, a),
                    tr.state, tr.title_phase, tr.band_phase, tr.title_timer, tr.title_index, (int)tr.band,
                    tr.band_timer);
        }
    }
    fprintf(f, "], %u]", em_area_title_lines());
    /* The 001CABA0 calls (the indicator children's and the muzzle node's
     * 001CACB0) of this tick's frame: per call [record, the +0x80 words,
     * bytes, clip, the colour / lighting-row / position-row digests].
     * tools/test_level_smoke.py check_indicator_units. */
    fputs(", \"page_units\": [", f);
    {
        EmOwnerDrawLivePageLog pu[EM_OWNER_DRAW_LIVE_PAGE_LOG];
        uint32_t pu_frame = 0;
        const int np = em_owner_draw_live_page_log(pu, EM_OWNER_DRAW_LIVE_PAGE_LOG, &pu_frame);
        for (int i = 0; pu_frame == em_frame_counter() && i < np; ++i)
            fprintf(f, "%s[%u, [%u, %u, %u, %u], %u, %u, %u, %u, %u]", i ? ", " : "", pu[i].record, pu[i].rgb[0],
                    pu[i].rgb[1], pu[i].rgb[2], pu[i].rgb[3], pu[i].bytes, pu[i].clip, pu[i].colour, pu[i].light,
                    pu[i].position);
    }
    fputc(']', f);
    /* The attached 001CAA00 calls (001CB3C0's face units: Roger, the player
     * while a script holds its face) of the last drawn frame: per record,
     * [record, face bytes], and on sampled calls (per record the first, then
     * every 200th, at most 80) the call's inputs and appended bytes for the
     * original re-execution (tools/level_smoke_face.py). */
    fputs(", \"face_units\": [", f);
    {
        const EmOwnerDrawLiveSample *sm = NULL;
        const int ns = em_owner_draw_live_samples(&sm);
        static uint32_t seen_record[4], seen_frame[4], seen_calls[4], seen_samples[4];
        for (int i = 0; i < ns; ++i) {
            const EmOwnerDrawLiveSample *m = &sm[i];
            unsigned k = 0;
            while (k < 4 && seen_record[k] && seen_record[k] != m->record) ++k;
            int emit = 0;
            if (k < 4 && !(seen_record[k] == m->record && seen_frame[k] == m->frame)) {
                seen_record[k] = m->record;          /* each drawn call is counted once */
                seen_frame[k] = m->frame;
                emit = seen_calls[k]++ % 200u == 0 && seen_samples[k] < 80;
                seen_samples[k] += emit;
            }
            fprintf(f, "%s[%u, %u, %u, ", i ? ", " : "", m->record, m->frame, m->face_bytes);
            if (!emit) {
                fputs("null]", f);
                continue;
            }
            fprintf(f, "{\"record_bytes\": ");
            log_hex(f, m->record_bytes, m->record_size);
            fputs(", \"nodes\": ", f);
            log_hex(f, (const uint8_t *)m->nodes, 64u * m->node_count);
            fprintf(f, ", \"slot_address\": %u, \"slot\": ", m->slot_address);
            log_hex(f, m->slot, sizeof m->slot);
            const struct { const char *name; const uint32_t *w; } views[] = {
                {"view_810610", m->view_810610}, {"planes_2410", m->planes_2410}, {"vp_3AC0", m->vp_3AC0},
            };
            for (size_t v = 0; v < sizeof views / sizeof views[0]; ++v) {
                fprintf(f, ", \"%s\": ", views[v].name);
                log_hex(f, (const uint8_t *)views[v].w, 0x40);
            }
            fprintf(f, ", \"ctx_0C\": %u, \"ctx_9C\": %u, \"rig_word\": %u, \"area\": [%u, %u], \"rig\": ",
                    m->ctx_0C, m->ctx_9C, m->rig_word, m->area[0], m->area[1]);
            log_hex(f, (const uint8_t *)m->rig, sizeof m->rig);
            fprintf(f, ", \"unit_address\": %u, \"unit\": ", m->unit_address);
            log_hex(f, m->unit, m->unit_bytes);
            fputs("}]", f);
        }
    }
    fputc(']', f);
    /* The point-light pool at render context +0x210..+0x221F after this
     * frame's 001D7C30 (the slots 001D89D0's fold reads): the next handle,
     * the pending count, the two words at +0x218, the area key
     * D_00810700 << 8 | D_00810701 001D7C30 tests, then [slot, 0x80 bytes]
     * for every one of the 32 active (+0x220) and 32 pending (+0x1220)
     * slots that is not all zero, so the checker rebuilds the pool exactly.
     * tools/test_level_smoke.py check_owner_units (the whole lighting rows
     * over the port's slots) and check_sway (001D7C30 over the port's
     * slots and draws). */
    fputs(", \"lights\": ", f);
    {
        const uint8_t *pool = em_rcl_bytes(EM_RCL_CONTEXT + 0x210u, 0x2010u);
        if (pool) {
            uint32_t w[4];
            memcpy(w, pool, sizeof w);
            fprintf(f, "[%u, %u, %u, %u, %u, [", w[0], w[1], w[2], w[3],
                    (unsigned)s_state.d810700 << 8 | s_state.d810701);
            int n = 0;
            for (int k = 0; k < 64; ++k) {
                const uint8_t *slot = pool + 0x10 + 0x80 * k;
                int any = 0;
                for (int b = 0; b < 0x80 && !any; ++b) any = slot[b] != 0;
                if (!any) continue;
                fprintf(f, "%s[%d, ", n++ ? ", " : "", k);
                log_hex(f, slot, 0x80);
                fputc(']', f);
            }
            fputs("]]", f);
        } else {
            fputs("null", f);
        }
    }
    /* The last barrel's glow markers (em_effects_live_markers): [frame,
     * [[colour x4, rand() value, rgb, emitted, primitive colour x4], ...]].
     * tools/test_level_smoke.py check_marker_colour. */
    fputs(", \"markers\": ", f);
    if (em_effects_live_attached()) {
        EmEffectsLiveMarker mk[EM_EFFECTS_LIVE_MARKERS];
        uint32_t frame = 0;
        const int nm = em_effects_live_markers(mk, EM_EFFECTS_LIVE_MARKERS, &frame);
        fprintf(f, "[%u, [", frame);
        for (int i = 0; i < nm; ++i)
            fprintf(f, "%s[%u, %u, %u, %u, %d, %u, %d, %u, %u, %u, %u]", i ? ", " : "", mk[i].colour[0],
                    mk[i].colour[1], mk[i].colour[2], mk[i].colour[3], mk[i].value, mk[i].rgb, mk[i].emitted,
                    mk[i].packet[0], mk[i].packet[1], mk[i].packet[2], mk[i].packet[3]);
        fputs("]]", f);
    } else {
        fputs("null", f);
    }
    /* The camera pool's view D_00810610 (0018C0D0's look-at; the camera
     * fill of 001D8340 reads it): check_owner_units' second original draw
     * runs over it with the port's point-light pool. */
    fputs(", \"view610\": ", f);
    {
        const uint8_t *view = em_rcl_bytes(0x00810610u, 0x40u);
        if (view) log_hex(f, view, 0x40);
        else fputs("null", f);
    }
    /* Census L29: 0015C160 this tick (fresh, D_008102B1, D_00810771, +0x214's
     * record, the route: -1 reported), the last shadow call
     * (em_shadow_live_log) and, on sampled calls (the first, then every
     * 100th 001DA6A0 and every 20th 0015BF90 call, at most 40 of each, per
     * world: the counts restart when D_00810700 changes and when the AREA01
     * live composition binds at its rebuild, so the AREA01 world has samples
     * of its own from its first frame), its inputs and outputs for the
     * original re-execution (tools/level_smoke_shadow.py). */
    fputs(", \"shadow\": ", f);
    static uint32_t calls_route[2], samples_route[2];
    static uint32_t acalls, asamples, alast;
    static int sample_world = -1;
    const int world = (int)s_state.d810700 | (s_area01_live.bound ? 0x100 : 0);
    if (sample_world != world) {
        sample_world = world;
        calls_route[0] = calls_route[1] = samples_route[0] = samples_route[1] = 0;
        acalls = asamples = 0;
    }
    if (em_shadow_live_bound()) {
        EmShadowLiveLog l;
        em_shadow_live_log(&l);
        const uint32_t now = em_frame_counter();
        fprintf(f, "[[%d, %u, %u, %u, %d], [%d, %d, %d, %d, %u, %u, %u, %u, %u, %u, %u, %u, %u], ",
                s_post_step.frame == now, s_post_step.b1, s_post_step.d771, s_post_step.w214, s_post_step.route,
                l.frame == now, l.route, l.drawn, l.kind, l.receivers, l.receivers_cls2, l.decal_fans,
                l.decal_vertices, l.flushed, l.decal_flushed, l.calls, l.drawn_total, l.decal_total);
        const EmShadowLiveSample *sm = em_shadow_live_sample();
        int emit = 0;
        if (sm && l.frame == now && sm->frame == now) {
            const int r = sm->route == EM_SHADOW_ROUTE_001DA6A0 ? 0 : 1;
            emit = calls_route[r]++ % (r ? 20u : 100u) == 0 && samples_route[r] < 40;
            samples_route[r] += emit;
        }
        if (emit) {
            fprintf(f, "{\"route\": %d, \"player\": ", sm->route);
            log_hex(f, sm->player, sizeof sm->player);
            fputs(", \"nodes\": ", f);
            log_hex(f, sm->nodes, sizeof sm->nodes);
            const struct { const char *name; const uint32_t *w; } views[] = {
                {"clip_2240", sm->clip_2240}, {"proj_2340", sm->proj_2340}, {"view_2380", sm->view_2380},
                {"camera_3AC0", sm->camera_3AC0}, {"view_810610", sm->view_810610},
            };
            for (size_t i = 0; i < sizeof views / sizeof views[0]; ++i) {
                fprintf(f, ", \"%s\": ", views[i].name);
                log_hex(f, (const uint8_t *)views[i].w, 0x40);
            }
            fprintf(f, ", \"zoom_2468\": %u, \"fog_A0\": ", sm->zoom_2468);
            log_hex(f, (const uint8_t *)sm->fog_A0, 16);
            fprintf(f, ", \"area\": [%u, %u], \"spad3B8D\": %u, \"ff0\": ", sm->area_700, sm->sub_701,
                    sm->spad3B8D);
            log_hex(f, (const uint8_t *)sm->ff0_before, 16);
            if (sm->route == EM_SHADOW_ROUTE_001DA6A0) {
                fputs(", \"plan\": ", f);
                log_hex(f, (const uint8_t *)sm->plan, sm->plan_bytes);
            } else {
                fprintf(f, ", \"segment\": [%d, ", sm->segment_result);
                log_hex(f, (const uint8_t *)sm->segment_point, 16);
                fputs(", ", f);
                log_hex(f, (const uint8_t *)sm->segment_normal, 12);
                fprintf(f, "], \"submit\": [%d, %d, ", sm->submitted, sm->submit_tag);
                log_hex(f, (const uint8_t *)sm->submit_corners, 64);
                fprintf(f, ", %u, \"%016llx\"], \"packets\": [", sm->submit_rgba,
                        (unsigned long long)sm->submit_tex0);
                for (uint32_t i = 0; i < sm->packet_count; ++i) {
                    if (i) fputs(", ", f);
                    log_hex(f, sm->packet[i], 16u * sm->packet_qwords[i]);
                }
                fputc(']', f);
            }
            fputc('}', f);
        } else {
            fputs("null", f);
        }
        fputc(']', f);
    } else {
        fputs("null", f);
    }
    /* Roger's 001BA580 -> 001DA6A0 in the owner walk (em_shadow_live's
     * actor call): [fresh (its frame is this tick's), record, the result,
     * kind, receivers, class-2 receivers, flushed, cumulative calls and
     * draws, sample]; on sampled calls (the first, then every 100th, at most
     * 40, per area as above) the call's inputs for the original re-execution
     * (tools/level_smoke_shadow.py check_actor). */
    fputs(", \"shadow_actor\": ", f);
    if (em_shadow_live_bound()) {
        EmShadowLiveActorLog a;
        em_shadow_live_actor_log(&a);
        const uint32_t now = em_frame_counter();
        fprintf(f, "[%d, %u, %d, %d, %u, %u, %u, %u, %u, ", a.frame == now, a.record, a.drawn, a.kind,
                a.receivers, a.receivers_cls2, a.flushed, a.calls, a.drawn_total);
        const EmShadowLiveSample *sm = em_shadow_live_actor_sample();
        int emit = 0;
        if (sm && a.frame == now && sm->frame == now && a.calls != alast) {
            alast = a.calls;
            emit = acalls++ % 100u == 0 && asamples < 40;
            asamples += emit;
        }
        if (emit) {
            fprintf(f, "{\"record\": %u, \"record_bytes\": ", sm->record);
            log_hex(f, sm->player, sm->record_size);
            fputs(", \"nodes\": ", f);
            log_hex(f, sm->nodes, sizeof sm->nodes);
            const struct { const char *name; const uint32_t *w; } views[] = {
                {"clip_2240", sm->clip_2240}, {"proj_2340", sm->proj_2340}, {"view_2380", sm->view_2380},
                {"camera_3AC0", sm->camera_3AC0}, {"view_810610", sm->view_810610},
            };
            for (size_t i = 0; i < sizeof views / sizeof views[0]; ++i) {
                fprintf(f, ", \"%s\": ", views[i].name);
                log_hex(f, (const uint8_t *)views[i].w, 0x40);
            }
            fprintf(f, ", \"zoom_2468\": %u, \"area\": [%u, %u], \"ff0\": ", sm->zoom_2468, sm->area_700,
                    sm->sub_701);
            log_hex(f, (const uint8_t *)sm->ff0_before, 16);
            fputs(", \"plan\": ", f);
            log_hex(f, (const uint8_t *)sm->plan, sm->plan_bytes);
            fputs("}]", f);
        } else {
            fputs("null]", f);
        }
    } else {
        fputs("null", f);
    }
    /* WP-13: the chain page drawn at this frame's close (em_chain_page_live;
     * docs/CHAIN_PAGE.md): [drawn this tick, pages, start, the skipped
     * 001DDE10 CALL, transfers, qwords, DIRECT packets, lane / sprite
     * MSCALs, XGKICKs, primitives, [by PRIM type 0..7], skipped CALLs,
     * vertices without a Q (0: the per-tag Q), inherited-cycle UNPACKs, decal triangles, the
     * primitives' digest, the glow markers (sprites with 001F4D40's TEX0:
     * x0, y0, x1, y1, z, f, s0, t0, q0, q_known0, s1, t1, q1, q_known1),
     * and, on sampled pages (the first, then every
     * 250th, at most 40), every (address, bytes) the walk read (each range
     * once), for the original re-walk (tools/test_level_smoke.py
     * check_chain_page); then the snow program's MSCALs, the weather list
     * 001E0D70 CALLed (0: none) and the reads of overlay source blocks
     * (the AREA11 flame's descriptor)]. */
    fputs(", \"page\": ", f);
    {
        EmChainPageLiveLog pl;
        em_chain_page_live_log(&pl);
        if (pl.pages) {
            const EmChainPageCounts *c = &pl.counts;
            fprintf(f, "[%d, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, [", pl.frame == em_frame_counter(), pl.pages,
                    pl.start, pl.four_sprite, c->transfers, c->qwords, c->direct, c->mscal_lane, c->mscal_sprite,
                    c->kicks, c->prims);
            for (int k = 0; k < 8; ++k) fprintf(f, "%s%u", k ? ", " : "", c->prim_type[k]);
            fprintf(f, "], %u, %u, %u, %u, %u, [", c->skipped, c->stale_q, c->cycle_inherited, pl.decal_triangles,
                    pl.digest);
            uint32_t np = 0;
            const EmGfxGsPrim *pr = em_chain_page_live_prims(&np);
            const uint8_t *qk = em_chain_page_live_q();
            int nm = 0;
            for (uint32_t i = 0; pr && i < np; ++i) {
                if ((pr[i].prim & 7u) != 6u || (pr[i].tex0 & ~(UINT64_C(7) << 61)) != UINT64_C(0x00045B0599421EF0))
                    continue;
                const EmGfxGsVertex *a = &pr[i].v[0], *b = &pr[i].v[1];
                fprintf(f, "%s[%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u]", nm++ ? ", " : "", a->x, a->y,
                        b->x, b->y, b->z, b->f, a->s, a->t, qk[3 * i] ? a->q : 0u, qk[3 * i], b->s, b->t,
                        qk[3 * i + 1] ? b->q : 0u, qk[3 * i + 1]);
            }
            fputs("], ", f);
            static uint32_t sampled, last_pages;
            const int fresh = pl.frame == em_frame_counter() && pl.pages != last_pages;
            if (fresh) last_pages = pl.pages;
            /* Also the first 12 pages that ran the streak program (the
             * impact effect 0x80000060, no route capture has one). */
            static uint32_t streak_sampled, flare_sampled, strip_sampled, kind2_sampled;
            const int streak = fresh && c->mscal_streak && streak_sampled < 12;
            /* And the first 12 with the gun lamp's flare (00187690). */
            const int flare = fresh && !streak && pl.flare_sprites && flare_sampled < 12;
            /* And the first 12 whose lanes drew (an active ring-decal slot,
             * the shots' impact marks). */
            const int strip = fresh && !streak && !flare && c->lane_strips && strip_sampled < 12;
            /* And the first 12 with the kind-2 program (the cable's hit
             * effect node 0021AAC0). */
            const int kind2 = fresh && !streak && !flare && !strip && c->mscal_kind2 && kind2_sampled < 12;
            if (fresh && ((sampled < 40 && (pl.pages == 1 || pl.pages % 250 == 0)) || streak || flare || strip ||
                          kind2)) {
                if (streak) streak_sampled++;
                else if (flare) flare_sampled++;
                else if (strip) strip_sampled++;
                else if (kind2) kind2_sampled++;
                else sampled++;
                const uint32_t *pairs;
                const uint32_t nr = em_chain_page_live_reads(&pairs);
                fputc('[', f);
                int written = 0;
                for (uint32_t r = 0; r < nr; ++r) {
                    /* each (address, size) once: the weather's tiles CALL the
                     * same program packet 108 times */
                    int seen = 0;
                    for (uint32_t k = 0; k < r && !seen; ++k)
                        seen = pairs[2 * k] == pairs[2 * r] && pairs[2 * k + 1] == pairs[2 * r + 1];
                    if (seen) continue;
                    const uint8_t *b = em_chain_page_live_read(pairs[2 * r], pairs[2 * r + 1]);
                    fprintf(f, "%s[%u, ", written++ ? ", " : "", pairs[2 * r]);
                    if (b) log_hex(f, b, pairs[2 * r + 1]);
                    else fputs("null", f);
                    fputc(']', f);
                }
                fputc(']', f);
            } else {
                fputs("null", f);
            }
            /* The class-2 unit CALLs (001CABA0's): [target, primitives, of
             * them the strip triangles (pass 0), the TEX0 (low, high) and
             * PRIM it leaves] each, and the digest of the page's other
             * primitives. */
            fprintf(f, ", %u, %u, %u, [", c->mscal_snow, pl.weather, pl.overlay_reads);
            for (uint32_t k = 0; k < pl.units && k < EM_CHAIN_PAGE_UNITS_MAX; ++k)
                fprintf(f, "%s[%u, %u, %u, %u, %u, %u]", k ? ", " : "", pl.unit_call[k], pl.unit_prims[k],
                        pl.unit_strips[k], (unsigned)pl.unit_tex0[k], (unsigned)(pl.unit_tex0[k] >> 32),
                        pl.unit_prim[k]);
            /* ... and the kind-6 program's MSCALs and primitives (D_0023D930,
             * 001CFBE0 kind 6: AREA01's near-fire layer). */
            fprintf(f, "], %u, %u, %u, %u, %u, %u, %u, %u, %u, %u]", pl.digest_without_units, c->mscal_streak,
                    c->streak_prims, pl.flare_sprites, c->lane_strips, c->mscal_kind2, c->kind2_prims,
                    c->direct_strips, c->mscal_kind6, c->kind6_prims);
        } else {
            fputs("null", f);
        }
    }
    /* The weather's last closed channel-3 list (em_snow_runtime): [closed
     * in this tick's frame, lists, its first packet, tiles, FNV-1a of tile
     * 0's packet-3 data, every tile's packet 3 equal]; and the flame's last
     * 001D04B0 (em_effects_live): [called in this tick's frame, calls,
     * source, depth key, packet-1 digest (phase, seed zeroed), packet-4
     * digest]. tools/level_smoke_chain_page.py. */
    {
        EmSnowRuntimeLog sl;
        em_snow_runtime_log(&sl);
        if (sl.lists)
            fprintf(f, ", \"snow\": [%d, %u, %u, %u, %u, %u]", sl.frame == em_frame_counter(), sl.lists, sl.start,
                    sl.tiles, sl.p3_digest, sl.p3_uniform);
        else
            fputs(", \"snow\": null", f);
        EmEffectsLiveOverlayLog ol;
        em_effects_live_overlay_log(&ol);
        if (ol.calls)
            fprintf(f, ", \"flame\": [%d, %u, %u, %d, %u, %u]", ol.frame == em_frame_counter(), ol.calls, ol.source,
                    ol.key, ol.p1_digest, ol.p4_digest);
        else
            fputs(", \"flame\": null", f);
    }
    /* The load veil's last frame drawn at step V (em_load_veil_live), once,
     * on the first line after it: its frame counter (the tick whose veil
     * blocks it drew from), the channel-0 run and slot, the kicked list,
     * the displayed FRAME_1, the walk's counts, the digest, the brightest
     * line colour byte and the run's bytes. */
    {
        static uint32_t logged_draws;
        EmLoadVeilLiveLog vl;
        em_load_veil_live_log(&vl);
        fputs(", \"veil_draw\": ", f);
        if (vl.draws != logged_draws) {
            logged_draws = vl.draws;
            const EmChainPageCounts *c = &vl.counts;
            fprintf(f, "{\"frame\": %u, \"draws\": %u, \"start\": %u, \"end\": %u, \"slot\": %u, "
                    "\"chain\": %u, \"display\": %llu, \"transfers\": %u, \"qwords\": %u, \"direct\": %u, "
                    "\"prims\": %u, \"types\": [", vl.frame, vl.draws, vl.start, vl.end, vl.slot, vl.chain,
                    (unsigned long long)vl.display_frame, c->transfers, c->qwords, c->direct, c->prims);
            for (int k = 0; k < 8; ++k) fprintf(f, "%s%u", k ? ", " : "", c->prim_type[k]);
            fprintf(f, "], \"digest\": %u, \"max_line_rgb\": %u, \"run\": ", vl.digest, vl.max_line_rgb);
            uint32_t n = 0;
            const uint8_t *run = em_load_veil_live_run(&n);
            if (run) log_hex(f, run, n);
            else fputs("null", f);
            fputc('}', f);
        } else {
            fputs("null", f);
        }
    }
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
    if (arrival_scene() && s_area01_live.bound) {
        EmArea01Call call = {.function = 0x0022EEF0u, .a = {0x008101E0u, 1}, .na = 2};
        return em_area01_live_call(&s_area01_live, &call);
    }
    return em_area11_script_host_camera_0022EEF0() < 0 ? -1 : 0;
}
static int camera_area_worker(void *ctx, uint32_t function, uint32_t a0, uint32_t a1)
{
    (void)ctx;
    if (!arrival_scene() || !s_area01_live.bound)
        return -1;
    EmArea01Call call = {.function = function, .a = {a0, a1}, .na = function == 0x001B0300u ? 0u : 2u};
    return em_area01_live_call(&s_area01_live, &call);
}
/* The aim camera's views (CAMERA_LIVE.md section 7): the gun node and its
 * bone matrices (em_equipment_live), the ELF's R2 eye offset D_002754E8..F3
 * (em_aim_fire_tables, read only). */
static uint8_t *camera_memory(void *ctx, uint32_t address, uint32_t size, int write)
{
    (void)ctx;
    uint8_t *p = em_equipment_live_field(address, size, write);
    if (p || write || !em_aim_fire_tables_contains(address, size)) return p;
    const uint8_t *t = em_aim_fire_tables_bytes(address, size);
    if (!t && em_aim_fire_tables_load() == 0) t = em_aim_fire_tables_bytes(address, size);
    return (uint8_t *)t;
}
/* A camera store into the player's +A0..+A8 (the aim camera's copies of
 * 0x70003040, 00183010's push): the port's canonical placement g.pos. */
static int camera_place(void *ctx, const float pos[3])
{
    (void)ctx;
    memcpy(g.pos, pos, sizeof g.pos);
    return 0;
}
/* The original address of grid node `node`: the world-section directory
 * D_0028A598 entry 0 (the loader's relocation slot 0x42, set by the area
 * load's step 7) names the grid section header in the area data the drive
 * delivered; its +0x20 word is the node array's offset (what 00199C50
 * stages at 0x70003208) and a node is 64 bytes (decomp
 * tools/export_collision.py). 0 when the loader has not delivered it. */
uint32_t em_scene_bindings_grid_node_address(uint32_t node)
{
    EmModuleLoader *ml = em_module_loader_live();
    EmStatusSceneLoader *ld = ml ? em_module_loader_state(ml) : NULL;
    if (!ld) return 0;
    uint32_t header = ld->d28A490[(0x0028A598u - 0x0028A490u) / 4u];
    const uint8_t *p = header ? em_module_loader_memory(ml, header + 0x20u, 4) : NULL;
    if (!p) return 0;
    uint32_t offset = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return header + offset + 64u * node;
}
/* The bytes of the grid node records (read only), as the area data the
 * loader delivered holds them: `size` bytes at `address` inside the node
 * array of the loaded grid (em_scene_bindings_grid_node_address(0) ..
 * + 64 * count), or NULL. */
const uint8_t *em_scene_bindings_grid_node_bytes(uint32_t address, uint32_t size)
{
    const EmCollSegment *seg = em_collision_world_segment();
    if (!seg || !seg->world || !seg->world->grid || !size) return NULL;
    uint32_t base = em_scene_bindings_grid_node_address(0);
    uint64_t end = (uint64_t)base + 64u * (uint64_t)seg->world->grid->count;
    if (!base || address < base || (uint64_t)address + size > end) return NULL;
    return em_module_loader_memory(em_module_loader_live(), address, size);
}
static uint32_t camera_grid_node(void *ctx, uint32_t node)
{
    (void)ctx;
    const EmCollSegment *seg = em_collision_world_segment();
    if (!seg || !seg->world || !seg->world->grid || node >= seg->world->grid->count) return 0;
    return em_scene_bindings_grid_node_address(node);
}
static EmCameraLiveHost k_camera_host = {NULL, camera_player, camera_hip, camera_euler, camera_pad_config,
                                         NULL, camera_standins, camera_timeline,
                                         camera_memory, camera_place, camera_grid_node, NULL};

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

_Static_assert(offsetof(EmSceneState, d810702) == offsetof(EmSceneState, d810700) + 2,
               "the area bytes D_00810700..702 are one view");

static int rcl_bind(void)
{
    /* D_0028A5A0, the static-object bank's address: slot 0x44 of the
     * screen-module loader's resource table (em_module_loader, the one
     * storage of D_0028A490..; read only here). */
    EmModuleLoader *ml = em_module_loader_live();
    EmStatusSceneLoader *ld = ml ? em_module_loader_state(ml) : NULL;
    uint8_t *bank_word = ld ? (uint8_t *)&ld->d28A490[EM_STATUS_SCENE_SLOT_D_0028A5A0] : NULL;
    if (!bank_word) {
        fprintf(stderr, "em_scene: the render context needs the screen-module loader's D_0028A5A0\n");
        return -1;
    }
    const EmRclExternal views[] = {
        {0x00810610u, 0x40u, em_camera_live_bytes(0x00810610u, 0x40u)},
        {0x008105E0u, 0x10u, em_camera_live_bytes(0x008105E0u, 0x10u)},
        {EM_SCENE_REQ_BASE, EM_SCENE_REQ_SIZE, s_state.req},
        {0x00810700u, 3u, &s_state.d810700},
        {0x008101E4u, 1u, &g.cam.top_mode},   /* the camera block's +0x04 */
        {0x70003B8Du, 1u, &s_state.spad3B8D},
        /* Read only: the camera's view of D_008102B0 (CAMERA_LIVE.md 5). */
        {0x008102B0u, 0x320u, (uint8_t *)(uintptr_t)em_camera_live_player_bytes()},
        {0x0028A5A0u, 4u, bank_word},
    };
    static const EmRclWorkers workers = {NULL, rcl_point_light, rcl_sqrt, rcl_tan, rcl_weather};
    /* The static-object bank and 001E1E60's upload block (the area's
     * static world, docs/STATIC_WORLD.md): fail-stop without the export. */
    const int area01 = strcmp(g.scene_dir, AREA01_SCENE_DIR) == 0;
    if (!area01 && em_rcl_static_world_load(NULL) < 0)
        return -1;
    if (em_rcl_bind(views, sizeof views / sizeof views[0], &workers) < 0)
        return -1;
    if (area01) {
        /* AREA01's dynamic pass reads the resource delivered at slot 0x45.
         * Rebind after em_rcl_bind invalidates the previous area's views. */
        const uint8_t *word = (const uint8_t *)&ld->d28A490[0x45];
        uint32_t size = 0;
        const uint8_t *bytes = em_module_loader_memory_rest(ml, ld->d28A490[0x45], &size);
        if (em_rcl_dynamic_world_bind(word, bytes, size) < 0)
            return -1;
    }
    return 0;
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

/* The level exit's arrival: AREA01 sub 0 loaded (area_read). The port runs
 * its 0x1AE040 state-0 rebuild, the arrival frame and the capture's first
 * frame of control in AREA01 (route beat 15, docs/FIRST_LEVEL_EXIT.md);
 * every later AREA01 frame is level 2 and faults (w_001AD4D0). */
static int arrival_scene(void)
{
    return strcmp(g.scene_dir, AREA01_SCENE_DIR) == 0;
}

/* A scene with the original's world: its collision world, live camera,
 * spawn table and roster pool. */
static int world_scene(void)
{
    return roster_scene() || arrival_scene();
}

/* 001D8BF0(player, a1) for 001AF5C0: +0x02 bit 0x20 set / cleared
 * (em_roger_actor_001D8BF0, the one translation, over the record's +0x02). */
static int wipe_001D8BF0(void *ctx, uint8_t *player, int32_t a1)
{
    (void)ctx;
    EmRogerActor actor;
    EmRogerActorRecord record;
    memset(&actor, 0, sizeof actor);
    memset(&record, 0, sizeof record);
    record.cls = player[0x02];
    if (em_roger_actor_001D8BF0(&actor, &record, a1) < 0) return -1;
    player[0x02] = record.cls;
    return 0;
}

/* 001AF5C0 (byte-matched; em_slg_001AF5C0) over the player record image. */
static int player_wipe_001AF5C0(void)
{
    EmPlayerLiveActor *p = player_states_actor_mut();
    if (!p) return -1;
    EmSlgState0Workers w;
    EmSlgState0 st;
    memset(&w, 0, sizeof w);
    memset(&st, 0, sizeof st);
    w.w_001D8BF0 = wipe_001D8BF0;
    st.player = p->bytes;
    st.player_self = D_PLAYER;                 /* D_008102C4 = D_008102B0 */
    return em_slg_001AF5C0(&w, &st);
}

/* 001AF690 (after 001AF5C0 in 001AFCA0): em_slg_001AF690 over a staging
 * image of D_00810130..D_008102AF and D_0081060C, then its camera half
 * (D_008101E0..+0xCF and D_0081060C) to the camera's canonical storage.
 * The camera state byte +0 = 0 makes the next 0018B9C0 run its one-shot
 * seat (state 0: +6 = 8 outside area 0x12 sub 0), as the original's first
 * world frame after a rebuild does (route 15 f742, the AREA01 arrival:
 * D_008101E6 = 8). The other two blocks have owners that start them at
 * the same zeros here: D_008101D0..DF the render context (its bind, below)
 * and D_00810130..1CF the status runtime (created zeroed when the area's
 * interaction host loads; em_area11_interaction_host_clear freed it). */
static void block_reset_001AF690(void)
{
    uint8_t image[0x180];
    uint32_t d81060C = 0;
    EmSlgState0 st;
    memset(&st, 0, sizeof st);
    st.status = image; /* every byte is written by 001AF690 */
    st.d81060C = &d81060C;
    em_slg_001AF690(&st);
    em_camera_live_store_block(image + 0xB0, d81060C);
}

static uint8_t *area01_state_memory(void *ctx, uint32_t address, uint32_t size)
{
    return em_module_loader_memory_mutable(ctx, address, size);
}

/* Restore each existing owner's private scratch before an area load can
 * unload the pose host. Also safe for state-0 resets without a new load. */
static int area01_scratch_detach(void)
{
    if (s_area01_scratch_bound) {
        EmPoseHost *pose = player_pose_record_host();
        if (!pose || !pose->globals)
            return em_scene_fault(&s_state, 0x70003600u, EM_SCENE_FAULT_NULL_WORKER);
        memcpy(s_area01_pose_scratch[0], pose->globals->spad3400, 64);
        memcpy(s_area01_pose_scratch[1], pose->globals->spad3440, 64);
        memcpy(s_area01_pose_scratch[2], pose->globals->spad3600, 16);
        pose->globals->spad3400 = s_area01_pose_scratch[0];
        pose->globals->spad3440 = s_area01_pose_scratch[1];
        pose->globals->spad3600 = s_area01_pose_scratch[2];
        if (em_camera_live_scratch_38_bind(NULL, NULL, NULL) < 0 ||
            em_camera_live_scratch_bind(NULL, NULL, NULL, NULL) < 0 ||
            em_aim_fire_runtime_scratch_3600_bind(NULL) < 0)
            return em_scene_fault(&s_state, 0x70003600u, EM_SCENE_FAULT_WORKER_FAILED);
        s_area01_scratch_bound = 0;
    }
    return em_rcl_scratch_3400_bind(NULL) < 0 ?
        em_scene_fault(&s_state, 0x70003400u, EM_SCENE_FAULT_WORKER_FAILED) : 0;
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
    if (area01_scratch_detach() < 0)
        return -1;
    /* The AREA11 interaction host's owners die with the pool (001AF8E0):
     * detach the player's Use and stage hooks, then free the owner tokens
     * (em_area11_interaction_host.h: whole-world teardown). */
    player_use_set_hook(NULL, NULL);
    em_player_closure_live_set_scan(NULL, NULL);
    em_player_closure_live_set_water(NULL, NULL);
    em_player_closure_live_set_crawl_clip(NULL, NULL);
    player_pose_set_stage_hook(NULL, NULL);
    player_pose_set_takeover_end_hook(NULL, NULL);
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
    } else if (arrival_scene()) {
        /* The level exit's arrival: AREA01 sub 0's collision world (its
         * area01.emcl and cell directory, the global SDK tables), which the
         * live camera's bind needs for 001B07C0's 001B0460. */
        if (em_collision_world_load(&g.coll, g.coll_path, AREA01_CELLS_PATH, EM_COLLISION_WORLD_SDK_PATH) != 0)
            return em_scene_fault(&s_state, 0x001AFCA0u, EM_SCENE_FAULT_NULL_WORKER);
        /* 001FB9F0 pages its area ids by D_00810700/701: the registry scope
         * of the arrival (the interaction host's clear above left none). */
        em_sfx_set_area(1, 0);
        /* The render context's static-object bank *D_0028A5A0 (resource
         * 0x44: AREA01's, at the nested block's resident base) as the area
         * load delivered it; 001C1DC0's 001C1E70 -> 001D52E0 reads its grid
         * header. AREA11's comes from its export (rcl_bind). */
        EmModuleLoader *ml = em_module_loader_live();
        EmStatusSceneLoader *ld = ml ? em_module_loader_state(ml) : NULL;
        const uint32_t bank = ld ? ld->d28A490[EM_STATUS_SCENE_SLOT_D_0028A5A0] : 0;
        uint32_t size = 0;
        const uint8_t *bytes = bank ? em_module_loader_memory_rest(ml, bank, &size) : NULL;
        if (!bytes || em_rcl_static_world_bank(bank, bytes, size) < 0) {
            fprintf(stderr, "em_scene: 001AFCA0: AREA01's static-object bank D_0028A5A0 = %08X was not "
                            "delivered by the area load\n", (unsigned)bank);
            return em_scene_fault(&s_state, 0x0028A5A0u, EM_SCENE_FAULT_NULL_WORKER);
        }
    } else {
        em_collision_world_unload();
    }
    /* 001FD790 indexes D_00264DD0 by D_00810700; 001FD950 reads the bank
     * the area load delivered at D_0028A594. Rebind their data without
     * resetting D_002821B0 or the existing stream/presenter workers. */
    if (world_scene() && !em_message_live_select_area(
            arrival_scene() ? AREA01_SCENE_DIR "/message_data.emmd"
                            : "assets/message/message_data.emmd", s_state.d810700))
        return em_scene_fault(&s_state, 0x001FD790u, EM_SCENE_FAULT_NULL_WORKER);
    /* 001AF5C0 wipes the player record (em_slg_001AF5C0 over the record
     * image: the memset, then +0x14 = the record, +0x02 = 0, the scale
     * +0x60..+0x6C and the colour words +0x80..+0x8C = 1.0, +0x70 / +0x74 =
     * 0, +0x78 / +0x7C = 1.0, +0x94 = -1, +0x96 = 0x3D, then 001D8BF0(player,
     * 1): +0x02 bit 0x20), after player_states_reset has cleared the port's
     * pointers beside it; the first stage's 0015C420 then sets its spawn
     * values (+4 = 1, +280, +204, +31B: player_states_spawn_values, fields
     * the wipe does not write), and the stage runs with its workers from the
     * first gameplay stage on (census L01, em_player_stage_live.h). Without
     * the D_00248C98 export the stage cannot run: fault. */
    player_states_reset();
    if (player_wipe_001AF5C0() < 0)
        return em_scene_fault(&s_state, 0x001AF5C0u, EM_SCENE_FAULT_WORKER_FAILED);
    player_states_spawn_values();
    block_reset_001AF690();
    if (em_player_stage_live_bind() < 0)
        return em_scene_fault(&s_state, 0x0015BA50u, EM_SCENE_FAULT_NULL_WORKER);
    /* Census L13..L16: the live camera over the area's collision world
     * (its 0019A910 / 0019B7D0 and SDK context) and the ELF camera tables;
     * the legacy camera stays for a scene without an original world. */
    k_camera_host.carry31F0 = em_area11_boxes_carry31F0();
    /* Census L32 / L30: the render context's views and workers (before the
     * camera, whose 001DD980 publications store into it). */
    /* Each area binds its own delivered resource views. */
    if (world_scene() && rcl_bind() < 0)
        return em_scene_fault(&s_state, 0x001D1C50u, EM_SCENE_FAULT_NULL_WORKER);
    k_camera_host.area_worker = arrival_scene() ? camera_area_worker : NULL;
    if (world_scene() && em_camera_live_bind(&k_camera_host) < 0)
        return em_scene_fault(&s_state, 0x0018B9C0u, EM_SCENE_FAULT_NULL_WORKER);
    if (!world_scene()) {
        em_game_legacy_manifest_spawn();
        em_game_legacy_camera_rearm();
        em_game_legacy_state0_fixtures();
    }
    s_player_init_pending = 1; /* the 001AF5C0 wipe: player +4 = 0 */
    em_actor_pool_reset_001AF8E0(&s_pool);
    em_collision_world_lists_reset_001AF8E0(); /* 001AF8E0's class-list half */
    em_area11_bindings_reset();
    em_area01_arrival_reset();
    em_area01_live_detach(&s_area01_live);
    if (world_scene()) {
        EmModuleLoader *ml = em_module_loader_live();
        EmStatusSceneLoader *ld = ml ? em_module_loader_state(ml) : NULL;
        if (!ld || em_area11_boxes_bind_world_bank(
                arrival_scene() ? AREA01_SCENE_DIR "/world_models.emwm"
                                : EM_AREA11_WORLD_MODELS_PATH,
                ld->d28A490[0x43]) < 0)
            return em_scene_fault(&s_state, 0x0028A59Cu, EM_SCENE_FAULT_NULL_WORKER);
    }
    /* 001D0660: 001F0310, the effect pools (census L26 / L27), with the
     * effect and equipment binders over the new pool (the first level: its
     * effects draw on the render context); its 001E7780 (the overlay module
     * dispatch) is the loader's boundary. */
    if (world_scene()) {
        if (em_area11_bindings_effects_attach() < 0)
            return em_scene_fault(&s_state, em_effects_live_fault() ? em_effects_live_fault() : 0x001F0310u,
                                  EM_SCENE_FAULT_NULL_WORKER);
    }
    /* 001D0660 calls 001E7780 after 001F0310, before the frame machine's
     * spawn calls. The six AREA01 globals and loader-owned data/BSS are
     * canonical views; rebuilding does not clear the whole overlay again. */
    if (arrival_scene()) {
        if (em_area01_state_bind(&s_area01_state, area01_state_memory, em_module_loader_live()) < 0 ||
            em_area01_state_001E7780(&s_area01_state, s_state.d810700, s_state.d810701) < 0)
            return em_scene_fault(&s_state, s_area01_state.fault ? s_area01_state.fault : 0x001E7780u,
                                  EM_SCENE_FAULT_WORKER_FAILED);
    } else {
        em_area01_state_detach(&s_area01_state);
    }
    if (world_scene()) {
        /* Census L29: 0015C160's shadow over the render context, the
         * collision world and the player record (em_shadow_live); the
         * player's own draw moves behind it (em_render_player_post_step). */
        if (em_shadow_live_bind() < 0)
            return em_scene_fault(&s_state, em_shadow_live_fault() ? em_shadow_live_fault() : 0x0015C160u,
                                  EM_SCENE_FAULT_NULL_WORKER);
        em_render_player_post_step(1);
    } else if (!arrival_scene()) {
        em_effects_live_detach();
        em_render_player_post_step(0);
    }
    /* No shadow is drawn by state 0; the selected receiver set is ready
     * for the next world frame. */
    if (arrival_scene()) {
        EmPoseHost *pose = player_pose_record_host();
        const EmCollSegment *segment = em_collision_world_segment();
        uint8_t *matrices = em_owner_draw_live_memory(0x70003400u, 0x80u);
        const uint8_t *render = em_rcl_bytes(0x70003400u, 0x80u);
        uint8_t *v3600 = em_camera_live_scratch_bytes(0x70003600u, 16);
        uint8_t *v3610 = em_camera_live_scratch_bytes(0x70003610u, 16);
        uint8_t *v3630 = em_camera_live_scratch_bytes(0x70003630u, 16);
        const uint32_t *aim_scratch = em_aim_fire_runtime_scratch_3600();
        EmAimFireLive *aim = em_aim_fire_binding_host();
        uint32_t *scratch38[3] = {NULL, NULL, em_aim_fire_runtime_scratch_38C0()};
        for (unsigned k = 0; k < 2; ++k)
            for (unsigned i = 0; aim && i < aim->region_count; ++i) {
                const EmPoseRegion *r = &aim->regions[i];
                const uint32_t address = 0x700038A0u + 16u * k;
                if (r->bytes && r->writable && r->size >= 16 && address >= r->address &&
                    address - r->address <= r->size - 16)
                    scratch38[k] = (uint32_t *)(void *)(r->bytes + address - r->address);
            }
        if (!pose || !pose->globals || !segment || !segment->face || !matrices || !render ||
            !v3600 || !v3610 || !v3630 || !aim_scratch || !scratch38[0] || !scratch38[1] ||
            !scratch38[2])
            return em_scene_fault(&s_state, 0x70003400u, EM_SCENE_FAULT_NULL_WORKER);
        /* Preserve existing storage at the area boundary. The first
         * original readers of these vectors follow full-lane writers;
         * subsequent pose/camera/collision/render calls share each byte. */
        memcpy(matrices, render, 0x80u);
        memcpy(segment->face->box_min, v3600, 16);
        memcpy(segment->face->box_max, v3610, 16);
        memcpy(segment->face->delta, aim_scratch + 8, 16);
        memcpy(segment->face->rel, v3630, 16);
        s_area01_pose_scratch[0] = pose->globals->spad3400;
        s_area01_pose_scratch[1] = pose->globals->spad3440;
        s_area01_pose_scratch[2] = pose->globals->spad3600;
        /* A later binding failure still needs to restore these pointers
         * before the next area load unloads the pose host. */
        s_area01_scratch_bound = 1;
        pose->globals->spad3400 = (uint32_t *)(void *)matrices;
        pose->globals->spad3440 = (uint32_t *)(void *)(matrices + 0x40);
        pose->globals->spad3600 = (uint32_t *)(void *)segment->face->box_min;
        if (em_rcl_scratch_3400_bind(matrices) < 0 ||
            em_camera_live_scratch_bind(pose->globals->spad3400, pose->globals->spad3600,
                                        (uint32_t *)(void *)segment->face->box_max,
                                        (uint32_t *)(void *)segment->face->rel) < 0 ||
            em_aim_fire_runtime_scratch_3600_bind(pose->globals->spad3600) < 0 ||
            em_camera_live_scratch_38_bind(scratch38[0], scratch38[1], scratch38[2]) < 0)
            return em_scene_fault(&s_state, 0x70003400u, EM_SCENE_FAULT_WORKER_FAILED);
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
 *   +0x60..+0x6C, +0x80..+0x8C     the record image (spawn_commit; 001CAA00
 *                     of the player reads +0x80, em_player_draw_live)
 *   +0x0E, +0x230     written, no port storage and no port reader
 *   +0x1C, +0x304     no port object at these offsets (0): the stores through
 *                     them and 001EFE00 (reached only with D_008106C8 & 4 and
 *                     & 0x60, i.e. AREA11 after event 0x30) have no worker:
 *                     reaching them faults
 *   0015C1F0          em_player_misc_0015C1F0 over the record image (the
 *                     model bind: 001CA6E0, 001C6150 over the player model
 *                     export; 00200890 the boundary)
 *   001B0460          the live camera's translation (em_camera_live_001B0460,
 *                     census L13), after the placed pose is committed to g
 *   +0x224/+0x22C     g.pd_pend_hp/g.pd_pend_inf (arg0 1 drops them)
 *   +0x00             arg0 1 with pending damage writes 1: no port storage
 *                     (1 in every capture)
 *   +0x04/+0x05/+0x06 arg0 1 with the record's +0x14 byte 1 writes 5/1/0
 *                     (AREA11 entry 1): committed to the live record, whose
 *                     stage then runs 0015B610 / 00183250, the arrival
 *                     walk-out (em_player_floor.c); a takeover the stage
 *                     held ends there without 00182DF0
 *                     (player_pose_takeover_restated)
 * arg0 is 0 in state 0 and 1 in state 4 (S12b, "the room move" below); any
 * other pairing is refused (fault), as is D_00275BE0 == 1 (the load-game
 * pose D_00810710..72F now has canonical storage for terminal 00159B90's
 * copies, but the load-game reader in 0x1AE040 state 2 remains unported).
 * After the state-0 placement the port's
 * placement-dependent fixtures run (em_game_legacy_state0_fixtures).
 * A scene without an original roster keeps its manifest spawn (001AFCA0). */

static EmSpawnTable s_spawn_table;
static int s_spawn_table_loaded;
static EmSpawnIo *s_spawn_io; /* the placement in progress (for 001B0460) */

static void spawn_commit(const EmSpawnIo *io)
{
    EmPlayerLiveActor *rec = player_states_actor_mut();
    for (unsigned i = 0; rec && i < 4u; ++i) {
        em_live_set_f32(rec, 0x60 + 4 * i, io->player.f060[i]);   /* 001B07C0's scale words */
        em_live_set_f32(rec, 0x80 + 4 * i, io->player.f080[i]);   /* ... and colour words */
        /* 001B07C0 also publishes all four rotation lanes. Camera-frame
         * scratch 70003B50 takes +CC from this canonical player record. */
        em_live_set_f32(rec, 0xC0 + 4 * i, io->player.f0C0[i]);
    }
    if (rec && io->player.b004 != 0) {
        /* The walk-out branch is 001B07C0's only +4/+5/+6 store (the image
         * starts zeroed): 5 / 1 / 0. */
        em_live_set_u8(rec, 4, io->player.b004);
        em_live_set_u8(rec, 5, io->player.b005);
        em_live_set_u8(rec, 6, io->player.b006);
    }
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

/* 0015C1F0(player) (NEARMISS C, logic recovered): em_player_misc_0015C1F0
 * over the player record image, the one translation. It picks the model
 * kind +0x2FF from the infection mode +0x234 and D_00810C60 (mode 0 -> 0x3B,
 * with status 2 -> 0x3F, 1 -> 0x3E; mode 1 -> 0x40 / 0x3F / 0x3E; other
 * modes 0x3D; 001B81D0 reads the kind), binds D_0028A490[kind] with 001CA6E0
 * (+0x44, and +0x4C = 001CAA00 through 001CA5F0 kind 0), stores +0x0C =
 * 001C6150(+0x44) and the shadow kind +0x96 = 0x28 (001DA6A0 reads it), then
 * calls 00200890. Workers:
 *   001CA6E0   em_roger_actor_001CA6E0 (the one translation), its +0x44 and
 *              +0x4C written to the record image
 *   001C6150   the byte at model +0x08 of the exported player model
 *              (em_player_draw_live: resource 0x3B; any other kind's model
 *              is not exported and faults)
 *   00200890   the module loader's DMA of the player's texture packet
 *              (00200830 over D_0028A4B0..C0): the boundary; the textures
 *              it uploads are the resident ones tools/export_object_
 *              textures.py decodes from the captures
 * The routine reads D_00810C60 as 001B07C0 left it (s_spawn_io) and the
 * record's +0x234, which 001B07C0 stored just before the call; the spawn
 * works on its own image of the record (EmSpawnIo.player), so that byte is
 * carried into the record image first. D_0028A490 is the Roger export's
 * table (em_area11_roger_table_word). */
static int c1f0_bind_model(void *ctx, EmPlayerLiveActor *actor, uint32_t handle)
{
    (void)ctx;
    EmRogerActor ra;
    EmRogerActorRecord record;
    memset(&ra, 0, sizeof ra);
    memset(&record, 0, sizeof record);
    if (em_roger_actor_001CA6E0(&ra, &record, handle) < 0)
        return -1;
    em_live_set_u32(actor, 0x44, record.model);
    em_live_set_u32(actor, 0x4C, record.draw);
    return 0;
}

static int c1f0_bone_count(void *ctx, uint32_t model, uint8_t *count)
{
    (void)ctx;
    return em_player_draw_live_001C6150(model, count);
}

static int c1f0_00200890(void *ctx)
{
    (void)ctx;
    return 0;   /* the boundary (see above) */
}

static int spawn_w_0015C1F0(void *ctx, uint32_t player)
{
    (void)ctx;
    bind_trace(EM_SPAWN_FN_001B07C0, EM_SPAWN_FN_0015C1F0, player, 0, 0, 0);
    EmPlayerLiveActor *p = player_states_actor_mut();
    if (player != D_PLAYER || !s_spawn_io || !p)
        return -1;
    static uint32_t table[EM_AREA11_ROGER_TABLE_WORDS];   /* D_0028A490 (the Roger export's words) */
    for (uint32_t i = 0; i < sizeof table / sizeof table[0]; ++i)
        if (em_area11_roger_table_word(0x0028A490u + 4u * i, &table[i]) < 0)
            return em_scene_fault(&s_state, 0x0028A490u, EM_SCENE_FAULT_NULL_WORKER);
    EmPlayerMiscWorkers w;
    EmPlayerMiscScene sc;
    memset(&w, 0, sizeof w);
    memset(&sc, 0, sizeof sc);
    w.bind_model = c1f0_bind_model;
    w.bone_count = c1f0_bone_count;
    w.w00200890 = c1f0_00200890;
    sc.d810C60 = s_spawn_io->d810C60;
    sc.d28A490 = table;
    sc.d28A490_count = sizeof table / sizeof table[0];
    EmPlayerMiscHost host = {&w, &sc, NULL};
    em_live_set_u8(p, 0x234, s_spawn_io->player.b234);   /* 001B07C0's +0x234 = D_00810707 */
    return em_player_misc_0015C1F0(&host, p);
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
    if (!world_scene())
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
        /* The port's New Game fixtures (the weapon context); the level
         * exit's arrival keeps AREA11's state, as the original does. */
        if (!arrival_scene())
            em_game_legacy_state0_fixtures();
        return 0;
    }
    /* State 4: 001B07C0 wrote the walk-out (5/1/0, entry 1: the fence
     * door's side 1; route beat 09's entry 2 has none) into the record
     * (spawn_commit), which the door script's takeover still held: its
     * hold ends here (player_pose_takeover_restated), as 0015B530's
     * 00182DF0 never runs for it. The door and its program are the
     * original owner's since census L18 (em_area11_door); 0x1AE040 clears
     * the camera byte D_008101E4 below (0x1AE0BC, stored at w_0018D7B0). */
    if (io.player.b004 != 0 && player_pose_takeover_restated() < 0)
        return em_scene_fault(&s_state, EM_SPAWN_FN_001B07C0, EM_SCENE_FAULT_WORKER_FAILED);
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
 *   001FF080(0, 3)    screen module 3 through the screen-module loader
 *                     (em_module_loader, the slot-2 task 001FF0D0 with
 *                     001FF830; docs/MODULE_LOADER.md): D_00275BD8 stays 1
 *                     until the task's 0x63 step clears it
 *   001FF080(1, 0)    the area load through the same task (001FFCD0 with
 *                     001FF590 and the sound-bank upload 001FB370,
 *                     em_sound_bank): D_00275BD8 clears when its steps are
 *                     done, so 0021B550's veil ramps for as many ticks as
 *                     they take. The port's own assets for D_00810700/701
 *                     (em_game_legacy_area_load; only 0x0B/0, AREA11, is
 *                     exported) load in the dispatch whose 001FFCD0 step
 *                     completes the area (loader_area_done), where the
 *                     original's data are all in memory
 *   001AD230          em_game_new_game_reset_001AF2C0, returns 4
 *   D_00275C78, D_00821058  em_frontend_movie_select / _request
 *   001AED80(a0)      em_frame_fade_clear (its translation, em_fade.c)
 *   0021B180/0021B550/0021B840  the veil state machine (em_load_veil.c) over
 *                     s_veil; its 001D2830 calls on the render context, its
 *                     draw 0021B1B0 (em_rcl_0021B1B0) and phase step 0021B500
 *                     (em_load_veil_particles); step V's list draws the veil
 *                     (em_load_veil_live)
 *   00200830          reported no-port-code
 *   001D19D0          em_area11_roger_001D19D0 (001D9070 over the library's
 *                     model 0x16) */

static int in_task_step(int s09, int s0A)
{
    const uint8_t *b9 = em_scene_task_byte(s_user, EM_SCENE_TASK_09);
    const uint8_t *bA = em_scene_task_byte(s_user, EM_SCENE_TASK_0A);
    return b9 && bA && *b9 == s09 && *bA == s0A;
}

/* The port's own assets of the scene of D_00810700/701 (the renderer's and
 * the collision's formats), loaded when the loader task's area streamer
 * completes (its 0x63 step, the moment the original's area data are all in
 * memory; loader_area_done). */
static int area_read(void)
{
    const char *scene = NULL;
    if (s_state.d810700 == 0x0B && s_state.d810701 == 0)
        scene = AREA11_SCENE_DIR;
    /* The level exit's arrival: AREA01 sub 0's collision (its scene.txt
     * names area01.emcl only; the rebuild reads its roster, cells and the
     * global spawn table itself). */
    if (s_state.d810700 == 0x01 && s_state.d810701 == 0)
        scene = AREA01_SCENE_DIR;
    if (scene) {
        if (area01_scratch_detach() < 0)
            return -1;
        int rc = em_game_legacy_area_load(scene);
        if (rc < 0)
            return rc;
        /* The original area delivery replaced GS texture memory. Invalidate
         * the one native TEX0 registry at that same completed-load point. */
        ++s_area_resource_epoch;
        if (em_world_textures_live_bind(s_state.d810700, s_state.d810701) < 0)
            return -1;
        return em_shadow_live_select_area(s_state.d810700, s_state.d810701);
    }
    fprintf(stderr, "em_scene: 001FF080(1, 0): area %02X room %u is not exported\n",
            (unsigned)s_state.d810700, (unsigned)s_state.d810701);
    return -1;
}

/* 001AD1A0 (byte-matched, src/func_001AD1A0.c; mwcc 2.3.3): +9 == 0: +9++,
 * D_00275BD8 = 1, 001FF080(0, 3, &slot[9]); +9 == 1 and D_00275BD8 == 0:
 * 00200830(D_0028A564[0]), 001D19D0() (bound: em_area11_roger_001D19D0),
 * return 4; otherwise 0. */
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
        /* Module 3 (the player's texture packets, slots 8..52) through the
         * loader task; its 0x63 step clears D_00275BD8. */
        EmModuleLoader *ml = em_module_loader_live();
        if (!ml || em_module_loader_request_001FF080(ml, 0, 3) != 0)
            return em_scene_fault(&s_state, 0x001FF080u, EM_SCENE_FAULT_NULL_WORKER);
        return 0;
    }
    if (*sub == 1 && s_state.d275BD8 == 0) {
        bind_trace(0x001AD1A0u, 0x00200830u, 0, 0, 0, 0);
        unmirrored(UM_00200830);
        bind_trace(0x001AD1A0u, 0x001D19D0u, 0, 0, 0, 0);
        /* 001D19D0 -> 001D9070: the library model 0x16's fade weights, in
         * the global library's storage (the Roger export holds D_0028A56C). */
        uint32_t address = 0, size = 0, digest = 0;
        if (em_area11_roger_001D19D0(&address, &size, &digest) < 0)
            return em_scene_fault(&s_state, 0x001D19D0u, EM_SCENE_FAULT_WORKER_FAILED);
        printf("fade weights: 001D19D0 -> 001D9070 over library model 0x16 at %08X (%u bytes): fnv1a %08X\n",
               (unsigned)address, (unsigned)size, (unsigned)digest);
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

/* 0021B550's 001D2830 calls (the load veil's (3, 1)) on the render context. */
static int veil_001D2830(void *ctx, int group, int enable)
{
    (void)ctx;
    return em_rcl_001D2830(group, enable) < 0 ? rcl_fault() : 0;
}

/* 0021B1B0, the veil draw (asm-word; em_load_veil_particles), over the
 * render context: its packets at the channel-0 cursor, which main-loop step
 * V's list sends and em_load_veil_live draws (docs/LOAD_VEIL_PARTICLES.md
 * section 3). The block views are s_veil's +0x04 / +0x08 / +0x14 / +0x18. */
static int veil_0021B1B0(void *ctx, EmLoadVeil *veil)
{
    (void)ctx;
    EmLoadVeilParticlesBlock b = {&veil->w04, &veil->level[0], &veil->w14, &veil->w18};
    return em_rcl_0021B1B0(&b) < 0 ? rcl_fault() : 0;
}

/* 0021B500, the phase step (byte-matched): +0x04 += 0.007, wrapped at 1. */
static int veil_0021B500(void *ctx, EmLoadVeil *veil)
{
    (void)ctx;
    EmLoadVeilParticlesBlock b = {&veil->w04, NULL, NULL, NULL};
    if (em_load_veil_particles_0021B500(&b) < 0)
        return em_scene_fault(&s_state, 0x0021B500u, EM_SCENE_FAULT_NULL_WORKER);
    return 0;
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
/* The progress bytes 001B6660's condition for deferred record `rec` reads
 * (src/func_001B6660.c; em_actor_roster.c condition_001B6660): 1 when each
 * is canonical (em_scene_progress_canonical), else 0 with the first one in
 * *at. Conditions 0, 1 and the ids above 6 read only the taken bits. */
static int condition_canonical(const uint8_t *rec, uint32_t *at)
{
    const int16_t id = (int16_t)(rec[0] | rec[1] << 8);
    const int16_t v = (int16_t)(rec[2] | rec[3] << 8);
    const uint32_t hi = (uint32_t)((v >> 8) & 0xFF);
    uint32_t reads[2] = {0, 0};
    switch (id) {
    case 2: case 3: reads[0] = 0x00810758u + hi; break;
    case 4: reads[0] = 0x00810700u + (uint32_t)((v >> 8) + 0xD8); break;
    case 5: reads[0] = 0x00810758u + hi; reads[1] = 0x008107D8u + hi; break;
    case 6: reads[0] = 0x00810758u + hi; reads[1] = 0x00810778u; break;
    default: return 1;
    }
    for (unsigned k = 0; k < 2; ++k)
        if (reads[k] && !em_scene_progress_canonical(reads[k], 1)) {
            *at = reads[k];
            return 0;
        }
    return 1;
}

static int area01_behavior(EmActor *, void *);
const uint8_t *em_scene_bindings_target_model_bytes(uint32_t address,uint32_t size)
{
    return arrival_scene() ? em_area01_live_target_model_bytes(&s_area01_live,address,size) : NULL;
}
int em_scene_bindings_terminal_owner_read(uint32_t owner, uint32_t offset, uint32_t size,
                                          int32_t *value)
{
    return arrival_scene() && s_area01_live.bound ?
        em_area01_terminal_owner_read(&s_area01_live.actors, owner, offset, size, value) : -1;
}
int em_scene_bindings_terminal_reset(void)
{
    return arrival_scene() && s_area01_live.bound ? em_area01_terminal_reset(&s_state) : -1;
}
static int area01_private_model(void *ctx, const EmActor *a)
{
    (void)ctx;
    return a && a->behavior == area01_behavior;
}
static int area01_scan(void *ctx, EmPlayerLiveActor *player, int *result)
{
    (void)ctx;
    return em_area01_live_scan(&s_area01_live, player, result);
}
static int area01_crawl_clip(void *ctx, EmPlayerLiveActor *player, int *result)
{
    (void)ctx;
    if (!result) return -1;
    EmArea01Call c = {.function = 0x00188610u, .na = 1, .a = {EM_AREA01_PLAYER_BASE}};
    int rc = em_area01_live_player_call(&s_area01_live, player, &c);
    if (rc >= 0) *result = (int32_t)c.v0;
    return rc;
}
static int area01_water(void *ctx, uint32_t function, EmPlayerLiveActor *player, uint32_t level)
{
    (void)ctx;
    if (function != 0x00187DE0u && function != 0x001E8B90u) return -1;
    EmArea01Call c = {.function = function, .na = 1, .a = {EM_AREA01_PLAYER_BASE}};
    if (function == 0x001E8B90u) {
        c.a[0] += 0xB0u; c.nf = 1; c.f[0] = level;
    }
    return em_area01_live_player_call(&s_area01_live, player, &c);
}
static int area01_map_player_bank(void *ctx, uint32_t address, uint32_t size, const uint8_t *bytes)
{
    (void)ctx;
    return player_pose_map_region(address, size, bytes) ? 0 : -1;
}
static int area01_map_player_banks(void *ctx)
{
    (void)ctx;
    return s_area01_live.bound ?
        em_area01_script_player_banks(&s_area01_live.scripts, area01_map_player_bank, NULL) : -1;
}
static int area01_shared_interactions(void)
{
    EmSdkMathContext *sdk = em_collision_world_sdk();
    if (!sdk || !sdk->tables)
        return -1;
    EmInteractionMath math;
    memcpy(math.atan_high, sdk->tables->atan_hi, sizeof math.atan_high);
    memcpy(math.atan_low, sdk->tables->atan_lo, sizeof math.atan_low);
    memcpy(math.atan_coefficients, sdk->tables->atan_t, sizeof math.atan_coefficients);
    /* Status resources are the existing global UI exports. AREA01's
     * owner roster and script banks are provided independently above. */
    if (!em_area11_interaction_host_load_shared("assets/scene_snow", &math, area01_map_player_banks, NULL))
        return -1;
    player_pose_set_stage_hook(em_area11_interaction_host_player, NULL);
    player_pose_set_takeover_end_hook(em_area11_interaction_host_staged_released, NULL);
    player_use_set_hook(em_area11_interaction_host_use, NULL);
    em_player_closure_live_set_scan(area01_scan, NULL);
    em_player_closure_live_set_water(area01_water, NULL);
    em_player_closure_live_set_crawl_clip(area01_crawl_clip, NULL);
    em_message_live_set_host(em_area11_interaction_host_message_host());
    return 0;
}
static int area01_rebind(void *ctx, EmActor *a, uint32_t fn)
{
    (void)ctx;
    /* Original addresses in the prepared AREA01 owner modules. A missing
     * worker inside one of them still faults at that worker's address. */
    switch (fn) {
    case 0x00128C10u: case 0x00158D30u: case 0x00159B90u: case 0x0015A2C0u:
    case 0x001E3D90u: case 0x001E7D20u: case 0x001BB860u: case 0x001BFFD0u:
    case 0x001C02E0u: case 0x00128390u: case 0x001289C0u: case 0x00128AB0u:
    case 0x00129780u: case 0x001BB520u: case 0x001BC350u: case 0x001C50B0u:
    case 0x00823580u: case 0x00825350u: case 0x008254B0u: case 0x00825590u:
    case 0x00825670u: case 0x00825740u: case 0x008261A0u: case 0x00826200u:
    case 0x00826440u: case 0x008267C0u: case 0x00826CF0u: case 0x00826D40u:
    case 0x00828850u: case 0x00219550u: case 0x0015AFA0u: case 0x0015AC00u: case 0x0015AE20u:
    case 0x001C5680u: case 0x001C5760u:
        a->callback = fn;
        a->behavior = area01_behavior;
        a->release = NULL;
        a->owner = NULL;
        return 0;
    default:
        return -1;
    }
}
static uint8_t *area01_extra_bytes(void *ctx, uint32_t address, uint32_t size, int write)
{
    (void)ctx;
    /* Explicit views already borrowed by the player/equipment composition.
     * Collision state will be supplied by its authoritative adapter. */
    EmAimFireLive *aim = em_aim_fire_binding_host();
    for (unsigned i = 0; aim && i < aim->region_count; ++i) {
        const EmPoseRegion *r = &aim->regions[i];
        if (address >= r->address && size <= r->size && address - r->address <= r->size - size)
            return (!write || r->writable) ? r->bytes + address - r->address : NULL;
    }
    /* The scene-entry tables 001B0300 (camera mode 1's re-seat) walks, read
     * only (the game never writes them): D_0024D650 -> [D_00810700] ->
     * [D_00810701] -> + D_00810702 * 0x30 in the spawn table window the
     * placement 001B07C0 reads (its one copy, s_spawn_table, loaded by the
     * first area's placement), and the mode-1 eye row D_0024A8D0 + (word
     * +0x10 >> 8) * 12 in the camera's own table (em_camera_live). */
    if (!write) {
        const uint8_t *p = s_spawn_table_loaded ? em_spawn_table_read(&s_spawn_table, address, size) : NULL;
        if (!p)
            p = em_camera_live_eye_rows(address, size);
        if (p)
            return (uint8_t *)(void *)p;
    }
    return NULL;
}
static int area01_missing_worker(void *ctx, EmArea01Call *call)
{
    (void)ctx;
    fprintf(stderr, "em_area01: unbound worker %08X from live composition\n", call->function);
    return -1;
}
static uint8_t *area01_registry_bytes(void *ctx, uint32_t address, uint32_t size, int write)
{
    if (write)
        return NULL;
    const uint8_t *p = em_module_loader_memory(ctx, address, size);
    if (!p)
        p = em_effects_live_window(address, size);
    return (uint8_t *)(void *)p;
}
static uint8_t *area01_pass_bytes(void *ctx, uint32_t address, uint32_t size)
{
    return em_area01_live_bytes(ctx, address, size, 0);
}
static int area01_pass_pair(void *ctx, uint32_t function, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    EmArea01Live *l = ctx;
    if (em_area01_collision_view_begin(&l->collision) < 0)
        return -1;
    EmArea01Call c = {.function = function, .a = {a0, a1, a2, a3}, .na = function == 0x001AA000u ? 4u : 2u};
    int rc = em_area01_live_call_active(l, &c);
    if (em_area01_collision_view_commit(&l->collision) < 0)
        return -1;
    return rc;
}
static int area01_head_record(void *ctx, uint32_t address, uint8_t record[0x2F0])
{
    return em_area01_live_head_record(ctx, address, record);
}
/* 001A8660 calls the flame's +0x34 contact owner during close-out. The
 * actor/player views are already active; collision belongs to the native
 * passes between nested calls, just as it does for area01_pass_pair. */
static int area01_contact(void *ctx, uint32_t function, uint32_t entry, uint32_t player,
                          uint32_t player_b0)
{
    EmArea01Live *l = ctx;
    if (function != 0x001E3D20u || player != EM_AREA01_PLAYER_BASE || player_b0 != player + 0xB0u ||
        em_area01_collision_view_begin(&l->collision) < 0)
        return -1;
    EmArea01Call c = {.function = function, .a = {entry, player}, .na = 2};
    int rc = em_area01_live_call_active(l, &c);
    if (em_area01_collision_view_commit(&l->collision) < 0)
        return -1;
    return rc;
}
static const uint8_t *area01_head_bytes(void *ctx, uint32_t address, uint32_t size)
{
    return em_area01_live_head_bytes(ctx, address, size);
}
static int area01_bind_live(void)
{
    if (s_area01_live.bound)
        return 0;
    EmArea01LiveHost h = {0};
    h.pool = &s_pool;
    h.scene = &s_state;
    h.area = &s_area01_state;
    h.loader = em_module_loader_live();
    h.current_actor = &s_current_actor;
    h.resource_epoch = &s_area_resource_epoch;
    h.bytes = area01_extra_bytes;
    h.worker = area01_missing_worker;
    h.rebind = area01_rebind;
    h.private_model = area01_private_model;
    if (em_area01_live_bind(&s_area01_live, &h) < 0)
        return em_scene_fault(&s_state, s_area01_live.fault_address, EM_SCENE_FAULT_NULL_WORKER);
    if (em_effects_live_set_head_owner(area01_head_record, area01_head_bytes, &s_area01_live) < 0)
        return em_scene_fault(&s_state, 0x001F0120u, EM_SCENE_FAULT_NULL_WORKER);
    em_area11_boxes_bind_registry(area01_registry_bytes, h.loader);
    const EmCollisionWorldAreaPasses passes = {&s_area01_live, area01_pass_bytes, area01_pass_pair};
    if (em_collision_world_bind_area_passes(&passes) < 0)
        return em_scene_fault(&s_state, 0x001AAD00u, EM_SCENE_FAULT_NULL_WORKER);
    em_collision_world_bind_behaviour(area01_contact, &s_area01_live);
    if (em_collision_world_bind_area_hulls(em_area01_live_hull_chain, &s_area01_live) < 0)
        return em_scene_fault(&s_state, 0x001A6440u, EM_SCENE_FAULT_NULL_WORKER);
    return 0;
}
static int area01_select(void *ctx, uint32_t address, const EmActor *a)
{
    (void)ctx;
    (void)a;
    if (!s_area01_live.bound)
        return em_scene_fault(&s_state, 0x001AFCA0u, EM_SCENE_FAULT_NULL_WORKER);
    s_current_actor = address;
    /* The walk's 001CB590 publishes the current actor, then invokes the
     * existing bone-array setup before dispatching its callback. */
    EmAnimRest setup = {0};
    setup.world.d275B48 = &s_current_actor;
    setup.world.d275B40 = s_area01_live.model.source.current_bones;
    if (em_anim_rest_001CB5B0(&setup) < 0)
        return em_scene_fault(&s_state, 0x001CB5B0u, EM_SCENE_FAULT_WORKER_FAILED);
    return 0;
}
static int area01_behavior(EmActor *a, void *world)
{
    (void)world;
    if (!s_area01_live.bound)
        return em_scene_fault(&s_state, 0x001AFCA0u, EM_SCENE_FAULT_NULL_WORKER);
    if (s_current_actor != em_actor_pool_address(&s_pool, a))
        return em_scene_fault(&s_state, 0x001CB590u, EM_SCENE_FAULT_BAD_RESULT);
    EmArea01Call c = {.function = a->callback, .a = {s_current_actor}, .na = 1};
    if (em_area01_live_call(&s_area01_live, &c) < 0) {
        const EmArea01Live *l = &s_area01_live;
        fprintf(stderr, "em_area01: owner %08X node %08X failed at %08X\n", c.function, (uint32_t)c.a[0],
                l->fault_address);
        fprintf(stderr, "em_area01: diagnostics actor=%d/%08X/%d player=%d/%08X/%d collision=%d/%08X/%d "
                        "model=%d/%08X pickup=%d/%08X door=%d/%08X interaction=%d/%08X runtime=%d/%08X\n",
                l->actors.fault, l->actors.fault_address, l->actors.active,
                l->player.fault, l->player.fault_address, l->player.active,
                l->collision.fault, l->collision.fault_address, l->collision.active,
                l->model.fault, l->model.fault_address, l->pickups.fault, l->pickups.fault_address,
                l->door.fault, l->door.fault_address, l->interaction.fault, l->interaction.fault_address,
                l->runtime.fault, l->runtime.fault_address);
        return em_scene_fault(&s_state, l->fault_address, EM_SCENE_FAULT_WORKER_FAILED);
    }
    return 1;
}
static int area01_bind_roster(void *ctx, EmActor *a, const EmActorRosterSpawned *spawned)
{
    if (em_area01_arrival_bind(ctx, a, spawned) < 0)
        return -1;
    switch (a->callback) {
    /* These boot owners already use the selected model bank. AREA11
     * overlay addresses, the fence singleton and pickups are excluded. */
    case 0x001551B0u: case 0x00156620u: case 0x001C4820u: case 0x001C5930u:
        return em_area11_bind_roster(ctx, a, spawned);
    default:
        if (area01_rebind(NULL, a, a->callback) == 0)
            return 0;
        /* No owner for this callback: the runtime's unknown-worker arm
         * faults at it when the original pool walk reaches it. */
        a->behavior = area01_behavior;
        a->release = NULL;
        a->owner = NULL;
        return 0;
    }
}

/* 001B6990 for the level exit's arrival: AREA01 sub 0's roster through the
 * same spawner, every node bound by em_area01_arrival (none ticks: AREA01's
 * world frames are level 2, w_001AD4D0). No interaction host: AREA01's
 * owners are its own. */
static int spawn_area01(void)
{
    if (s_state.d810700 != 0x01 || s_state.d810701 != 0)
        return em_scene_fault(&s_state, 0x001B6990u, EM_SCENE_FAULT_BAD_INDEX);
    if (!s_roster01_loaded) {
        if (em_actor_roster_load(&s_roster01, AREA01_ROSTER_PATH) != 0 || s_roster01.area != 1 ||
            s_roster01.sub != 0) {
            fprintf(stderr, "em_scene: 001B6990: %s missing or malformed (run "
                    "tools/export_area01_tables.py)\n", AREA01_ROSTER_PATH);
            return em_scene_fault(&s_state, 0x001B6990u, EM_SCENE_FAULT_NULL_WORKER);
        }
        s_roster01_loaded = 1;
    }
    for (uint32_t gi = 0; gi < s_roster01.group_count; ++gi)
        for (uint32_t i = 0; i < s_roster01.groups[gi].count; ++i) {
            uint32_t at = 0;
            if (!condition_canonical(s_roster01.groups[gi].records + (size_t)i * EM_ROSTER_DEFERRED_RECORD_SIZE,
                                     &at)) {
                fprintf(stderr, "em_scene: 001B6990: deferred g%u.%u's condition reads D_%08X, which is not "
                        "canonical yet (D2)\n", (unsigned)gi, (unsigned)i, (unsigned)at);
                return em_scene_fault(&s_state, 0x001B6660u, EM_SCENE_FAULT_BAD_INDEX);
            }
        }
    s_pool_mode = POOL_ROSTER;
    int rc = em_actor_roster_spawn_001B6990(&s_roster01, &s_pool, &s_state,
                                            (EmActorRosterProgress *)em_scene_progress_spawn_view(&s_state),
                                            area01_bind_roster, NULL, NULL);
    if (rc < 0)
        return rc;
    if (em_collision_world_bind_static_kinds(s_roster01.placements, s_roster01.placement_count) < 0)
        return em_scene_fault(&s_state, 0x0019F730u, EM_SCENE_FAULT_NULL_WORKER);
    if (area01_shared_interactions() < 0)
        return em_scene_fault(&s_state, 0x00184BA0u, EM_SCENE_FAULT_NULL_WORKER);
    if (area01_bind_live() < 0)
        return -1;
    return rc;
}

static int w_001B6990(void *ctx)
{
    (void)ctx;
    if (arrival_scene())
        return spawn_area01();
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
    player_pose_set_takeover_end_hook(em_area11_interaction_host_staged_released, NULL);
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
 * 0x1AE040 states 0 and 4; the node draws its own card (001C5930,
 * em_area_title). */
static int w_001C5C50(void *ctx)
{
    (void)ctx;
    if (s_pool_mode != POOL_ROSTER)
        return unmirrored(UM_001C5C50_LEGACY_WORLD);
    return em_actor_roster_spawn_001C5C50(&s_pool, &s_state,
                                          arrival_scene() ? area01_bind_roster : em_area11_bind_roster,
                                          NULL, NULL);
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
 *                      6B0    em_player_0015BCF0 (the scripted frames, the opening's
 *                             included)
 *   001CB5A0           both   empty leaf (src/func_001CB5A0.c)
 *   001D1C50           both   em_rcl_001D1C50 on the render context (census
 *                             L32); em_render_001D1C50 without it
 *   001C1D00(0x8101D0) both   em_rcl_001C1D00 on the render context (the background
 *                             channel and the static world, STATIC_WORLD.md);
 *                             em_render_001C1D00 without it
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
    if (s_area01_live.bound)
        return area01_select(NULL, a0, NULL);
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
            if (s_pool_mode == POOL_ROSTER) {
                if (em_area11_spawn_player_children_0015C420() < 0)
                    return -1;
                /* This stage is 0015BA50's +4 = 0 call: 0015C420 alone. */
                player_states_stage_rebuild();
            }
        }
        return em_player_0015BCF0();
    }
    if (s_variant == VARIANT_CUTSCENE) {
        /* No capture shows 0015C420 reached from 001AE6B0; the port does
         * not guess its children there. */
        if (s_player_init_pending && s_pool_mode == POOL_ROSTER)
            return em_scene_fault(&s_state, 0x0015C420u, EM_SCENE_FAULT_NULL_WORKER);
        /* The original runs 0015BCF0 in this variant too (the opening's
         * frames included): a script's 3B8D = 2 or 3 (the opening 0x828FC0,
         * an AREA11 interaction) has the player stage run, so the shared
         * player worker (0015B130 takeover, the scripted clip through
         * 00183090 with the face's 001D0C70, 00182DF0 release) runs at its
         * original position, between 001AFD70(1) and 001AFD70(2). */
        return em_player_0015BCF0();
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

/* 001C1D00(D_008101D0), both variants: in the first level the whole tree
 * on the render context (em_rcl_001C1D00: the channel-3 background list and
 * the static world's channel-0 run, docs/STATIC_WORLD.md); a scene without
 * the render context keeps em_render_001C1D00. */
static int w_001C1D00(void *ctx, uint32_t a0)
{
    (void)ctx;
    if (!in_variant() || a0 != EM_SCENE_D_008101D0)
        return -1;
    if (rcl_live())
        return em_rcl_001C1D00(a0) < 0 ? rcl_fault() : 0;
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
    if (em_actor_pool_walk_bound_001AFD70(&s_pool, &s_state, mode, &world,
                                          s_area01_live.bound ? area01_select : NULL, NULL,
                                          em_frame_trace_env() ? pool_trace : NULL, NULL) < 0)
        return -1;
    /* 001CB590 per node left D_00275B44 = the last ticked node. */
    if (s_pool.current)
        s_current_actor = em_actor_pool_address(&s_pool, s_pool.current);
    if (roster)
        em_area11_walk_end(mode);
    return 0;
}

/* 1 when the player record's node records are the pose the port displays
 * this frame: the record pose source holds the display
 * (player_pose_record_displayed; every stage poses the record, the
 * opening's included). */
int em_scene_bindings_player_record_drawn(void)
{
    return player_pose_record_displayed();
}

/* 0015C160 (byte-matched src/func_0015C160.c), the player post-step. In the
 * first level (census L29, docs/SHADOW_ORIGINAL.md "Binding"): with
 * D_008102B1 (the player's +0x01, which 0015BA50 sets every stage and the
 * 0x19 states clear) != 0, 001CB590(player, 0x320, player[9]) (w_001CB590:
 * D_00275B44 = the player), then unless D_00810771 == 1 the shadow
 * (em_shadow_live: 001DA6A0 with +0x214 == 0, 0015BF90 otherwise), then
 * the +0x4C method: 001CAA00(player) (em_player_draw_live through
 * em_owner_draw_live), whose unit frame_close_out draws after the shadow's
 * passes (em_owner_draw_live_post_step). While the record's nodes are not
 * the displayed pose (em_scene_bindings_player_record_drawn: the pose
 * source not yet started; a port stand-in holding the display) the
 * post-step is reported (UM_0015C160_UNPOSED) after its 001CB590 and the
 * +0x4C request is the port's own mesh draw of the displayed pose. A scene without the shadow
 * binding keeps the reported no-effect binding. The gate bytes and the
 * route are kept for the tick log (s_post_step). */
static int w_0015C160(void *ctx)
{
    if (!in_variant())
        return -1;
    if (s_pool_mode != POOL_ROSTER || !em_shadow_live_bound())
        return unmirrored(UM_0015C160);
    const EmPlayerLiveActor *p = player_states_actor();
    const uint8_t *d771 = em_scene_progress_at(&s_state, 0x00810771u, 1);
    if (!p || !d771)
        return em_scene_fault(&s_state, 0x0015C160u, EM_SCENE_FAULT_NULL_WORKER);
    s_post_step.frame = em_frame_counter();
    s_post_step.b1 = em_live_u8(p, 0x01);
    s_post_step.d771 = *d771;
    s_post_step.w214 = p->link_owner ? em_actor_pool_address(&s_pool, (const EmActor *)p->link_owner) : 0;
    s_post_step.route = 0;
    if (p->link_owner && !s_post_step.w214)   /* +0x214 names no record the port knows */
        return em_scene_fault(&s_state, 0x0015C160u, EM_SCENE_FAULT_BAD_RESULT);
    if (s_post_step.b1 == 0)
        return 0;   /* no 001CB590, no shadow, no +0x4C draw */
    if (w_001CB590(ctx, D_PLAYER, 0x320, em_live_u8(p, 0x09), 0) < 0)
        return -1;
    if (!em_scene_bindings_player_record_drawn()) {
        s_post_step.route = -1;   /* the tick log's "reported" */
        em_render_player_draw_0015C160();   /* the port's own +0x4C draw */
        return unmirrored(UM_0015C160_UNPOSED);
    }
    EmShadowOriginalFault fault = {0, 0};
    const int route = em_shadow_original_route_0015C160(s_post_step.b1, s_post_step.d771,
                                                        s_post_step.w214, &fault);
    if (route < 0)
        return em_scene_fault(&s_state, 0x0015C160u, EM_SCENE_FAULT_BAD_RESULT);
    s_post_step.route = route;
    if (route != EM_SHADOW_ROUTE_NONE && em_shadow_live_0015C160(p, route) < 0)
        return em_scene_fault(&s_state, em_shadow_live_fault() ? em_shadow_live_fault() : 0x0015C160u,
                              EM_SCENE_FAULT_WORKER_FAILED);
    /* hook(D_00275B44): the +0x4C method 001CAA00(player). */
    em_owner_draw_live_post_step();
    if (em_player_draw_live_001CAA00() < 0)
        return em_scene_fault(&s_state, 0x001CAA00u, EM_SCENE_FAULT_WORKER_FAILED);
    return 0;
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
    const int area01 = arrival_scene();
    if (area01 && (em_area01_live_resume(&s_area01_live) < 0 ||
                   em_area01_collision_view_commit(&s_area01_live.collision) < 0))
        return em_scene_fault(&s_state, 0x001AAD00u, EM_SCENE_FAULT_WORKER_FAILED);
    int rc = em_collision_world_close_out_001AAD00(&s_state, (int16_t)em_frame_transition()->substate,
                                                   &fault);
    /* Native passes own collision state between nested AREA01 pairs. Adopt
     * their final scratch and list swap before publishing actor/player views. */
    if (area01) {
        if (em_area01_collision_view_begin(&s_area01_live.collision) < 0 ||
            em_area01_live_suspend(&s_area01_live) < 0)
            rc = -1;
        if (s_area01_live.runtime.fault)
            fault = s_area01_live.runtime.fault_address;
        else if (s_area01_live.fault)
            fault = s_area01_live.fault_address;
    }
    if (rc < 0) {
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
 *   001D2830, 001E0CC0, 001D1EF0 -> the render context (em_rcl_001D2830,
 *                em_rcl_001E0CC0, em_rcl_001D1EF0; main-loop step V 001D2300
 *                clears the flag 3 the (3, 1) registrations set). */

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
                               r_00282157(NULL), pad->repeat};
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
    s_d282160 = -1;   /* D_00282160 = -1 (the ambient loop's cache, 001FC280) */
    if (w_00119828(ctx, 0, 0x1999, 0x1999) < 0 || w_00119828(ctx, 1, 0x1999, 0x1999) < 0)
        return -1;
    /* Its tail: D_00281F30's ten records back to {0, -1}. */
    em_stream_live_001FBC50_cues();
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

/* 001FC280 (NEARMISS, body-correct; src/func_001FC280.c), the lanes' worker
 * at the start of 001FAE70: the area ambient loop. id = the high half of the
 * spawn record's +0x20 (sra: 0xFFFF -> -1), or 0x44E in area 0x0B when
 * D_00810788 == 0xFF. When id differs from the cached D_00282160: a cached id
 * other than -1 is stopped (0011A070(D_00282164): em_sfx_stop_track, soft);
 * the cache takes id; an id other than -1 sets D_00282168..70 = 0x1000 and
 * starts D_00282164 = 001FB9F0(id, 0x1000, 0x1000, 0x1000)
 * (em_sfx_submit_001FB9F0_track, the selected area's registry scope). It ends
 * with 00119828(0, lo, lo) and 00119828(1, lo, lo), lo = the record's low
 * half (0x1999 in AREA11, 0x3FFF at AREA01 entry 4). Every AREA11 record
 * holds 0xFFFF1999 (and the captured D_00810788 is 0), so AREA11 starts no
 * loop; the level exit's AREA01 arrival (sub 0, entry 4: 0x044E3FFF) starts
 * 0x44E (route beat 15, docs/FIRST_LEVEL_EXIT.md). D_00282160 is -1 from
 * the title's 001FBC50 (001AC3B0 state 0) on, as em_sfx_init leaves that
 * routine's other tables (D_00281B70 / D_00281C30); every 001FBC50 sets it
 * again (w_001FBC50). */

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
    int32_t loop = (int16_t)(uint16_t)((uint32_t)record[2] | (uint32_t)record[3] << 8);   /* the high half, sign-extended */
    if (s_state.d810700 == 0x0B && *d788 == 0xFF)
        loop = 0x44E;
    if (s_d282160 != loop) {
        if (s_d282160 != -1 && em_sfx_stop_track(s_d282164, 0) < 0)
            return -1;
        s_d282160 = loop;
        if (loop != -1) {
            s_d282170 = s_d28216C = s_d282168 = 0x1000;
            bind_trace(0x001FC280u, 0x001FB9F0u, (uint32_t)loop, 0x1000, 0x1000, 0x1000);
            s_d282164 = em_sfx_submit_001FB9F0_track((unsigned)loop, s_d28216C, s_d282170);
            if (em_sfx_cue_state((unsigned)loop) != 1) {
                fprintf(stderr, "em_scene: 001FC280: the area loop 0x%X has no exported sound in the "
                                "area %u/%u scope (tools/export_sfx_registry.py)\n",
                        (unsigned)loop, (unsigned)s_state.d810700, (unsigned)s_state.d810701);
                return -1;
            }
        }
    }
    uint32_t lo = (uint32_t)record[0] | (uint32_t)record[1] << 8;
    if (w_00119828(NULL, 0, (int)lo, (int)lo) < 0 || w_00119828(NULL, 1, (int)lo, (int)lo) < 0)
        return -1;
    return 0;
}

/* 001FAE70(a0) (byte-matched, src/func_001FAE70.c), the area music cue:
 * em_stream_lanes_001FAE70 through em_stream_live (001FC280 above, the cue
 * from D_008106C8 / D_00810D38, the infected override over D_008104E4, the
 * 00122BB8 fade and the lane-0 restart). The frame machine binds its calls
 * at the state-0 area entry (0x1AE0CC, a0 = 1: the area music's read, and
 * the first rand() after New Game, from the unseeded state 1), the state-4
 * room move (a0 = 0: one rand(), then cue 25 continues) and the state-5
 * status close (a0 = 1). The rand() order is checked against the C7
 * capture (tools/rand_order.py, docs/RAND_ORDER.md). State 2's r == 1 and
 * state 6 stay reported (UM_001FAE70): no level smoke run reaches them. */
static int w_001FAE70(void *ctx, int a0)
{
    (void)ctx;
    const int bound = (s_entry_state == 0 && a0 == 1) || (s_entry_state == 4 && a0 == 0) ||
                      (s_entry_state == 5 && a0 == 1);
    if (!bound)
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
 *   001FAE70(0)       w_001FAE70 (the lanes; one rand())
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
 * skip, the fades); its workers (docs/DAMAGE.md section 5):
 *   001FF080(0, 0x27) -> the loader's 001FF0D0 (module 0x27, the game-over
 *                        screen, in the loader pack since the DAMAGE step);
 *                        its 0x63 step clears D_00275BD8, which the core
 *                        raised, and its chunk is the screen's upload
 *   001ABF90(packet)  -> em_render_001ABF90: the screen module 0x27's four
 *                        sprites (the screen tools/export_game_over.py
 *                        composes from that upload)
 *   001AEE10, 001AEDE0 -> the translated fades (em_fade.c)
 *   001D2880          -> its 001D25F0 / 001D2830 / 001D2610 on the render
 *                        context (w_001D2880)
 *   001FA790(0, 0x1B), 001FAB50 -> the stream lanes (em_stream_live)
 * 001ADF00's 001AEBA0(0xFF) is em_screen_fade_in (em_fade.c), its
 * D_00275BDC = 1 the scene state's, and its 001AB790(0x1AC070) installs the
 * title flow 001AC070 again (em_frontend_install_001AC070), whose state 0
 * reads D_00275BDC: from a death it opens the title menu (state 2) with the
 * cursor on its second entry. */

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
        /* The area load through the loader task (001FFCD0); D_00275BD8,
         * which 001ADF50 set, clears at the task's 0x63 step. */
        EmModuleLoader *ml = em_module_loader_live();
        if (!ml || em_module_loader_request_001FF080(ml, 1, 0) != 0)
            return em_scene_fault(&s_state, 0x001FF080u, EM_SCENE_FAULT_NULL_WORKER);
        return 0;
    }
    if (a0 != 0 || a1 != 0x27 || !in_game_over())
        return -1;
    /* 001AD4E0 step 1: the screen module through the loader task; the busy
     * byte D_00275BD8 the core set stays until the task's 0x63 step. */
    EmModuleLoader *ml = em_module_loader_live();
    if (!ml || em_module_loader_request_001FF080(ml, 0, 0x27) != 0)
        return em_scene_fault(&s_state, 0x001FF080u, EM_SCENE_FAULT_NULL_WORKER);
    return 0;
}

static int w_001ABF90(void *ctx, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
    (void)ctx;
    const uint64_t packet[4] = {a0, a1, a2, a3};
    if (!in_game_over() || em_render_001ABF90(packet) < 0)
        return em_scene_fault(&s_state, 0x001ABF90u, EM_SCENE_FAULT_WORKER_FAILED);
    return 0;
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
    return em_frontend_install_001AC070() == 0 ? 0 : -1;
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
    /* AREA01 (the level exit's arrival) runs the same machine over its
     * bound owners (LEVEL2_BINDING.md): its world frames are compared with
     * route 15 f742..f801 (the level smoke's a01_arrival). An AREA01
     * original without an owner faults where it is reached (the runtime's
     * unknown-worker arm, the collision passes' unported pairs). */
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
    w->w_001D1EF0 = w_001D1EF0;

    /* Status screen (S11b). */
    w->w_0020E060 = w_0020E060;
    w->w_001FBC50 = w_001FBC50;
    w->w_001FABB0 = w_001FABB0;
    w->w_00119828 = w_00119828;
    w->w_001D2830 = w_001D2830;
    w->w_0020CDC0 = w_0020CDC0;
    w->w_001E0CC0 = w_001E0CC0;
    w->w_001AEDB0 = w_001AEDB0;
    w->w_0018C0D0 = w_0018C0D0;

    /* The room move (S12b): 0x1AE040 state 4. */
    w->w_0018AB00 = w_0018AB00;
    w->w_0018D7B0 = w_0018D7B0;

    /* Game over (S11b): 001AD4E0 and 001ADF00. */
    w->w_001D2880 = w_001D2880;
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

/* 001B0C00's two workers for the area-change request: 001AEDE0(a0, a1), the
 * fade-out (em_frame), and 001FAD70(channel, a1, a2), a stream lane's fade
 * (em_stream_live). */
static int ac_001AEDE0(void *ctx, int32_t a0, int32_t a1)
{
    (void)ctx;
    bind_trace(0x001B0C00u, 0x001AEDE0u, (uint32_t)a0, (uint32_t)a1, 0, 0);
    em_frame_fade_start_colour(1, (int16_t)a0, (uint8_t)a1);
    return 0;
}

static int ac_001FAD70(void *ctx, int32_t channel, int32_t a1, int32_t a2)
{
    (void)ctx;
    bind_trace(0x001B0C00u, 0x001FAD70u, (uint32_t)channel, (uint32_t)a1, (uint32_t)a2, 0);
    return em_stream_live_001FAD70(channel, a1, a2) < 0 ? -1 : 0;
}

/* 001B0C60(a, b, c) (byte-matched): 3B8D = 3, 001B0C00(4) (the one
 * translation, em_script_host_001B0C00: 001AEDE0(4, 0), then 001FAD70(channel,
 * 4, 1) for the three lanes), then B8 = 1, B5 = a, B7 = c, B6 = b. Roger's
 * departure (1, 0, 4) and the fan's direct exit (1, 1, 4) call it. */
int em_scene_request_area_change_001B0C60(int a, int b, int c)
{
    bindings_init();
    s_state.spad3B8D = 3;
    bind_trace(0x001B0C60u, 0x001B0C00u, 4, 0, 0, 0);
    EmScriptHostWorkers h;
    memset(&h, 0, sizeof h);
    h.callees.w_001AEDE0 = ac_001AEDE0;
    h.callees.w_001FAD70 = ac_001FAD70;
    if (em_script_host_001B0C00(&h, 4) < 0)
        return em_scene_fault(&s_state, h.fault_address ? h.fault_address : 0x001B0C00u,
                              EM_SCENE_FAULT_WORKER_FAILED);
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

int em_scene_bindings_fan_cycle(uint32_t address, uint8_t *phase, int16_t *timer)
{
    if (s_pool_mode != POOL_ROSTER)
        return 0;
    int walked = 0;
    for (const EmActor *a = s_pool.head; a && walked <= EM_ACTOR_POOL_CAPACITY; a = a->next, ++walked) {
        EmArea11GunFanLog r;
        if (em_actor_pool_address(&s_pool, a) != address || !em_area11_bindings_gun_fan_log(a, &r))
            continue;
        *phase = r.b05;
        *timer = r.h28;
        return 1;
    }
    return 0;
}

uint32_t em_scene_bindings_pool_address(const void *actor)
{
    return em_actor_pool_address(&s_pool, (const EmActor *)actor);
}

int em_scene_bindings_record_image(uint32_t address, uint8_t *out)
{
    if (address < 0x7A5640u || (address - 0x7A5640u) % 0x2F0u ||
        (address - 0x7A5640u) / 0x2F0u >= EM_ACTOR_POOL_CAPACITY)
        return 0;
    em_actor_pool_record_image(&s_pool, &s_pool.records[(address - 0x7A5640u) / 0x2F0u], out);
    return 1;
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

/* ------------------------------------------------- the tick log's tail
 *
 * The route rows sample after the original frame, and its 0x28A9A0 fade,
 * the message service (step F) and the stream lanes (step H) run after the
 * slot-0 task: a tick's post-frame values are the next tick's start sample.
 * The last tick of a run has no next tick, so a run that must compare its
 * last frame (the level smoke's exit phase: the AREA01 arrival, decomp
 * exit_01 row 306) asks for a tail line, written after the main loop ended:
 * {"tail": 1, "counter", "fade8", "msg", "stream", "ambient", "pool"}, the
 * pool as the allocated records' +0x00..+0x17 headers and +0x18..+0x3F /
 * +0xA0..+0xDF by record address (the route rows' pool_delta /
 * pool_deep). Test instrumentation only. */
static int s_tail_requested;

void em_scene_bindings_log_request_tail(void) { s_tail_requested = 1; }

void em_scene_bindings_log_tail(void)
{
    FILE *f = log_file();
    if (!f || !s_tail_requested)
        return;
    s_tail_requested = 0;
    const EmTransitionFade *fade = em_frame_transition();
    uint8_t fade8[8], *p = fade8;
    put_le(&p, (uint16_t)fade->substate, 2);
    *p++ = fade->colour;
    *p++ = (uint8_t)fade->mode;
    put_le(&p, (uint16_t)fade->level, 2);
    put_le(&p, (uint16_t)fade->step, 2);
    fprintf(f, "{\"tail\": 1, \"counter\": %u, \"fade8\": ", em_frame_counter());
    log_hex(f, fade8, sizeof fade8);
    /* An arrival-before-rebuild endpoint has no next tick. Preserve the
     * actual post-frame loader snapshot instead of synthesizing that tick. */
    uint8_t loader[EM_MODULE_LOADER_SNAPSHOT_SIZE];
    em_module_loader_snapshot(em_module_loader_live(), em_task_slot(EM_MODULE_LOADER_TASK_SLOT), loader);
    fputs(", \"loader_pre\": ", f);
    log_hex(f, loader, sizeof s_tick.loader);
    const EmMessageBlock *block = em_message_live_block();
    fprintf(f, ", \"msg\": [%u, %u, %u]", block ? (unsigned)block->mode : 0u, block ? (unsigned)block->phase : 0u,
            block ? (unsigned)block->line : 0u);
    uint32_t st[9];
    if (em_stream_live_log(st))
        fprintf(f, ", \"stream\": [%u, %u, %u, %u, %u, %u, %u, %u, %u]", st[0], st[1], st[2], st[3], st[4], st[5],
                st[6], st[7], st[8]);
    else
        fputs(", \"stream\": null", f);
    fprintf(f, ", \"ambient\": [%d, %d]", (int)s_d282160, (int)s_d282164);
    fputs(", \"pool\": {", f);
    int n = 0, walked = 0;
    static uint8_t image[EM_ACTOR_RECORD_SIZE];
    for (const EmActor *a = s_pool.head; a && walked <= EM_ACTOR_POOL_CAPACITY; a = a->next, ++walked) {
        em_actor_pool_record_image(&s_pool, a, image);
        fprintf(f, "%s\"%u\": [", n++ ? ", " : "", (unsigned)em_actor_pool_address(&s_pool, a));
        log_hex(f, image, 0x18);
        fputs(", ", f);
        log_hex(f, image + 0x18, 0x28);
        fputs(", ", f);
        log_hex(f, image + 0xA0, 0x40);
        fputc(']', f);
    }
    fputs("}}\n", f);
    fflush(f);
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
    em_level_smoke_test_tick_end(); /* test instrumentation; inert when inactive */
    /* Step D of the next main-loop iteration reads 3B90 and C4 (design 2.1). */
    em_frame_screen_fade_gate(s_state.spad3B90, s_state.req[EM_SCENE_REQ_C4]);
    if (t)
        em_frame_trace_tick_end(t);
    report_unmirrored();
    s_user = NULL;
    /* Test instrumentation (inert when inactive): the level smoke's exit
     * phase watches the frames without a world frame from here. */
    if (rc >= 0)
        em_level_smoke_test_task_end();
    if (rc < 0 && !s_fault_reported) {
        s_fault_reported = 1;
        fprintf(stderr,
                "em_scene: FAULT at %08X (code %d); the game task is stopped (fail-stop)\n",
                (unsigned)s_state.fault.address, (int)s_state.fault.code);
    }
}

/* ------------------------------------------- the screen-module loader (H7)
 *
 * docs/MODULE_LOADER.md "Binding": the slot-2 task 001FF0D0's disc and DMA
 * layer over the user's exported sectors (assets/module_loader/modules.emml,
 * tools/export_module_loader.py). Its views are the one storage of each
 * byte it reads: D_00275BD8 and 0x70003B90 of the scene state, D_00282157
 * through the stream lanes' read phase, D_00810CA4 / D_00810CA6 of the
 * progress block. The disc answers at host speed; the PS2 disc-drive
 * timing switch (EM_PS2_DISC_DRIVE_TIMING, em_settings) selects the
 * recorded drive time instead (MODULE_LOADER.md 1.7). */
static EmModuleLoader *s_loader;
static int s_loader_reported;
static uint32_t s_area_uploads[2]; /* the A entries and player packets accepted */

static int area_read(void);

/* 001FFCD0's last step (the area streamer's 0x63): the port's own assets of
 * the area load now (area_read). */
static int loader_area_done(void *ctx, uint8_t area, uint8_t room)
{
    (void)ctx;
    (void)area;
    (void)room;
    return area_read();
}

/* 001FF590 mode 0's 001FB370: the stream owner's sound-bank upload. */
static int loader_bank(void *ctx, uint32_t address, const uint8_t *bytes, uint32_t size,
                       uint32_t *result)
{
    (void)ctx;
    return em_stream_live_001FB370(address, bytes, size, result);
}

/* The area load's two kinds of 00200830 send (the record's +8 == 1):
 *  - 001FF590(0xAB, 1)'s A entries (state 4), each sent from D_0028A73C
 *    where the drive delivered it: AREA11's one A entry is the area's
 *    texture upload (docs/DISC_TEXTURES.md section 1: GS blocks
 *    0x2A00..0x377F);
 *  - 00200890's player texture packet (state 7): one of the slot words
 *    D_0028A4B0..D_0028A4C0 that module 3's load relocated (GS blocks
 *    0x1B80..0x1BFF for slot 8).
 *  - 001FF590(0xAC, 1)'s A entries (state 8, the nested block of an area
 *    with rooms), each sent from D_0028A740: AREA01 sub 0's one A entry
 *    (chunk05.n0 +0x75000, 0xD8800 bytes; the level exit's load, route
 *    beat 15) is that room's texture upload (AREA01_ASSETS.md).
 * The port's renderer draws AREA11's texels from its disc export, which
 * DISC_TEXTURES test B proves equal to what these uploads write in every
 * route capture; AREA01's are drawn by nothing in the first level (the
 * arrival's last frame is the state-0 rebuild, under black; AREA01's world
 * is level 2). So the consumer accepts exactly these sends and applies
 * nothing. Any other send (a B section: AREA11 and AREA01 sub 0 have none)
 * is refused. */
static int loader_area_chain(void *ctx, uint32_t chain, const uint8_t *bytes, uint32_t size)
{
    EmModuleLoader *ml = ctx;
    const EmTask *rec = em_module_loader_record(ml);
    const EmStatusSceneLoader *ld = em_module_loader_state(ml);
    if (!rec || !ld || !bytes || !size || rec->user[0] != 1)
        return -1;
    if (rec->user[1] == 4 && rec->user[2] == 2 && chain == ld->d28A490[EM_STATUS_SCENE_SLOT_D_0028A73C]) {
        s_area_uploads[0]++;
        return 0;
    }
    if (rec->user[1] == 8 && rec->user[2] == 1 && chain == ld->d28A490[EM_STATUS_SCENE_SLOT_D_0028A740]) {
        s_area_uploads[0]++;
        return 0;
    }
    if (rec->user[1] == 7)
        for (uint32_t k = 0; k < 5; ++k)
            if (chain == ld->d28A490[EM_STATUS_SCENE_SLOT_D_0028A4B0 + k]) {
                s_area_uploads[1]++;
                return 0;
            }
    return -1;
}

int em_scene_bindings_module_loader_boot(const char *pack_path)
{
    em_scene_bindings_module_loader_shutdown();
    EmModuleLoader *ml = em_module_loader_open(pack_path);
    const uint8_t *ca = em_scene_progress_at(&s_state, 0x00810CA4u, 4);
    if (!ml || !ca) {
        em_module_loader_close(ml);
        fprintf(stderr, "em_scene: the screen-module loader's sectors %s are missing or malformed "
                "(python3 tools/export_module_loader.py; docs/STARTUP.md)\n", pack_path);
        return -1;
    }
    uint8_t *e703 = em_scene_progress_at(&s_state, 0x00810703u, 2);
    const uint8_t *e707 = em_scene_progress_at(&s_state, 0x00810707u, 1);
    const uint8_t *eC60 = em_scene_progress_at(&s_state, 0x00810C60u, 1);
    if (!e703 || !e707 || !eC60) {
        em_module_loader_close(ml);
        return -1;
    }
    const EmModuleLoaderViews views = {&s_state.d275BD8, r_00282157, NULL, ca, ca + 2,
                                       &s_state.spad3B90, &s_state.d810700, &s_state.d810701,
                                       e703, e703 + 1, e707, eC60};
    em_module_loader_set_views(ml, &views);
    /* The area streamer's sound-bank step 001FB370 runs on the stream
     * owner's IOP (em_stream_live, em_sound_bank), seeded with the pack's
     * D_00264890; the area's uploads go to the area consumer. */
    int32_t bases[5];
    em_module_loader_bank_bases(ml, bases);
    if (em_stream_live_bind_sound_bank(bases) != 0) {
        em_module_loader_close(ml);
        return -1;
    }
    em_module_loader_set_bank_hook(ml, loader_bank, NULL);
    em_module_loader_set_area_done_hook(ml, loader_area_done, NULL);
    em_module_loader_set_area_chain_hook(ml, loader_area_chain, ml);
    if (em_module_loader_set_drive(ml, em_settings()->ps2_disc_drive_timing
                                           ? EM_MODULE_LOADER_DRIVE_MEASURED
                                           : EM_MODULE_LOADER_DRIVE_HOST) != 0) {
        em_module_loader_close(ml);
        return -1;
    }
    em_module_loader_bind_live(ml);
    s_loader = ml;
    s_loader_reported = 0;
    return 0;
}

void em_scene_bindings_module_loader_shutdown(void)
{
    em_area01_state_detach(&s_area01_state);
    if (!s_loader)
        return;
    em_module_loader_bind_live(NULL);
    em_module_loader_close(s_loader);
    s_loader = NULL;
}

void em_scene_bindings_module_loader_field(void)
{
    em_module_loader_field(s_loader);
}

int em_scene_bindings_module_loader_check(void)
{
    EmStatusSceneFault f = {0, EM_STATUS_SCENE_FAULT_NONE};
    if (!s_loader || (!em_module_loader_failed(s_loader, &f) && !em_module_loader_orphaned(&f)))
        return 0;
    if (!s_loader_reported) {
        s_loader_reported = 1;
        fprintf(stderr, "em_scene: the screen-module loader faulted at %08X (code %d); the game "
                "stops (fail-stop)\n", (unsigned)f.address, (int)f.code);
    }
    return -1;
}

void em_scene_bindings_module_loader_report(FILE *out)
{
    if (!s_loader || !out)
        return;
    uint32_t dispatches, reads, unmeasured;
    em_module_loader_counts(s_loader, &dispatches, &reads, &unmeasured);
    if (em_settings()->ps2_disc_drive_timing)
        fprintf(out, "module loader: PS2 disc-drive timing on: %u dispatches, %u reads (%u without a "
                     "recorded drive time, answered at host speed)",
                (unsigned)dispatches, (unsigned)reads, (unsigned)unmeasured);
    else
        fprintf(out, "module loader: host speed: %u dispatches, %u reads", (unsigned)dispatches,
                (unsigned)reads);
    const EmSoundBank *bank = em_stream_live_sound_bank();
    fprintf(out, "; area uploads accepted: %u A entries, %u player packets; 001FB370: %u calls, %u "
                 "uploads (command 0x20)\n",
            (unsigned)s_area_uploads[0], (unsigned)s_area_uploads[1], bank ? (unsigned)bank->calls : 0u,
            bank ? (unsigned)bank->uploads : 0u);
}
