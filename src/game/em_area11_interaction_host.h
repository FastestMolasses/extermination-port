/* Native AREA11 services for the original panel/elevator interaction.
 * Live since WP-4 (docs/AREA11_INTERACTION_HOST.md): the scene bindings
 * load it at the AREA11 state-0 rebuild (after 001B6990), bind the panel
 * 00159210 (area11[18]) and terminal 00827B10 (area11[19]) pool nodes to
 * the panel/elevator ticks, install the Use and player-stage hooks, route
 * request-opened status screens through the status functions below and tick
 * the message at main-loop step F. Since census L07 the owners publish into
 * the collision world's class lists (em_collision_world.h), which 001AAD00
 * publishes; the Use scan reads its interactive list. */
#ifndef EM_AREA11_INTERACTION_HOST_H
#define EM_AREA11_INTERACTION_HOST_H

#include "game/em_actor_pool.h"
#include "game/em_elevator_runtime.h"
#include "game/em_frame.h"
#include "game/em_message_live.h"
#include "game/em_interaction_scene.h"
#include "game/em_interaction_projection.h"
#include "game/em_panel_runtime.h"
#include "game/em_owner_services_original.h"
#include "game/em_player.h"
#include "game/em_status_runtime.h"
#include "game/em_player_face_host.h"

/* Load only after ordinary player, collision and placed prop resources.
 * Status workers are the outer game/status services; geometry, animation,
 * messages, panel power and elevator camera workers are native bindings here.
 * Pass NULL for both optional arguments to use the native status services
 * and original SDK math. Returns1 on success. Failure never activates
 * legacy substitute behavior. */
int em_area11_interaction_host_load(const char *directory,
    const EmItemMath *item_math, const EmStatusRuntimeHooks *status_hooks);
void em_area11_interaction_host_clear(void); /* whole-world teardown only */

/* The map pickup aura's draw block (001F1180's 0x1F136C..0x1F1470) over the
 * owner's +0xD0 matrix: em_effects_live_aura_draw, set by the bindings'
 * effect attach (em_area11_bindings_effects_attach). Unset, a pickup aura
 * that reaches its draw faults (a missing worker). 0, or -1. */
typedef int (*EmArea11AuraDraw)(const float owner_d0[16], uint32_t record, uint32_t angle,
                                uint32_t timer);
void em_area11_interaction_host_set_aura_draw(EmArea11AuraDraw draw);

/* The host owners' record services (docs/OWNER_DRAW.md section 10), set by
 * the bindings (em_area11_bindings_effects_attach) over em_area11_boxes'
 * world owners:
 *   bind_001B0FD0(actor, ret)
 *   bind_001B1020(actor, a1, a2, a3, ret)
 *                        the model binds of the item owners' state 0
 *                        (0015AC00, 00219550): the record's model, +0x09,
 *                        +0x0C and bone slots; *ret the original result
 *   place(actor, world)  001C6380 over the record and its slots; world = its
 *                        new +0xD0 (the terminal's state 0, its carry
 *                        00828050 and its completion; the item owners'
 *                        state 0)
 *   draw(actor)          the record's +0x4C, 001CAA00 (the panel's and the
 *                        terminal's state-1 tail after 001B17A0; an item
 *                        owner's when its 001B17A0 found it visible)
 *   copy_child(actor)    the terminal's 0x827E6C: 00102958 of its node 0's
 *                        +0x90 into its +0x2E4 child's slot 0 +0x90
 * Unset, an owner that reaches one faults (a missing worker). 0, or -1. */
typedef struct {
    int (*bind_001B0FD0)(EmActor *actor, int32_t *ret);
    int (*bind_001B1020)(EmActor *actor, uint32_t a1, int32_t a2, int32_t a3, int32_t *ret);
    int (*place)(EmActor *actor, float world[16]);
    int (*draw)(EmActor *actor);
    int (*copy_child)(EmActor *actor);
} EmArea11HostOwnerHooks;
void em_area11_interaction_host_set_owner_hooks(const EmArea11HostOwnerHooks *hooks);

EmInteractionScene *em_area11_interaction_host_scene(void);
EmInteractionRuntime *em_area11_interaction_host_shared(void);
EmPanelRuntime *em_area11_interaction_host_panel(void);
EmElevatorRuntime *em_area11_interaction_host_elevator(void);
EmStatusRuntime *em_area11_interaction_host_status(void);
/* The hub's static actor pool and model draws (em_status_models), for the
 * level smoke. */
const struct EmStatusModels *em_area11_interaction_host_status_models(void);

/* Original B81D0 and FD950 services. Return1 only on success; a required
 * failure latches host failure and retains the shared owner. Attach sets
 * player_ready2 after the actual alternate mesh is ready. Talk is direct,
 * without consuming activity0. Caller preserves the original event gates. */
int em_area11_interaction_host_face_attach(void);
int em_area11_interaction_host_face_talk(uint8_t talking);
/* Alternate player draw:1 active,0 ordinary model,-1 required worker fault.
 * Uses the current original player palette. The drawing coordinator must
 * publish the separate face light rig (camera fill, no dynamic fold). */
int em_area11_interaction_host_player_record(EmGfxMesh **mesh, const float **palette,
                                            uint32_t *bones, const EmModel **model);
const EmOpeningFace *em_area11_interaction_host_face_state(void);

/* Actual player-stage callback for player_pose_set_stage_hook. Ordinary
 * source advancement already happened when unowned; acquired callbacks
 * advance only inside the shared interaction worker. 2 while a script
 * owner holds the token: its takeover is the stage's own (0015B130's
 * prelude, 0015B530, 00182DF0), which em_player.c then runs. */
int em_area11_interaction_host_player(void *unused);
/* 001D0C70 for the player stage's 00183090 (0x70003B8F == 2): the attached
 * face's tick. 0, or -1 (latched). */
int em_area11_interaction_host_face_tick_001D0C70(void);
/* player_pose_set_takeover_end_hook worker: the stage's 00182DF0 released
 * a script owner's player; its token ends. 1, or -1 (latched). */
int em_area11_interaction_host_staged_released(void *unused);
/* player_use_set_hook worker: the Use dispatcher 00160220 over the live
 * record (em_player_closure_live_use_press, with this host's 00184BA0 as its
 * scan). 1 an action took the press (the record's +5 names it: 0x25 an
 * owner won, 3B8D = 3; 2 / 3 the ledge climb / vault; 0xB the ladder; 6 the
 * running jump; 0x24 the aim), 0 not, -1 fault. */
int em_area11_interaction_host_use(void *unused);
/* 00184BA0(p) over the published list (00183EF0), the dispatcher's scan
 * worker (em_player_closure_live_set_scan): *result 1 when an owner won and
 * was claimed (3B8D = 3). 0, or -1 fault. */
int em_area11_interaction_host_scan_00184BA0(void *context, EmPlayerLiveActor *actor, int *result);
/* Called at the respective original pooled-owner positions (state 1 of
 * 00159210 / 00827B10, including their 001B17A0 publication tail). */
int em_area11_interaction_host_panel_tick(void);
int em_area11_interaction_host_elevator_tick(void);
/* The AREA11 item owners (WP-6), at their pool nodes by EMIS source id
 * (the roster record address): state 0 (the node's first call; `model` and
 * `param` are the actor's +0x03 and +0x0D: 0015AC00 or 00219550's state 0
 * over the record, its model bind and 001C6380 through the owner hooks),
 * then one owner update per call (1 allocated, 0 freed: the node frees
 * itself, -1 fault); a visible owner's +0x4C is the draw hook. */
int em_area11_interaction_host_pickup_state0(uint32_t source_id, uint8_t model, uint8_t param);
int em_area11_interaction_host_pickup_tick(uint32_t source_id);
/* 00827B10 state 0's placement (0x827B54..0x827C04), after the node's
 * 001B0FD0 bound the record's model and slots: the floor byte D_0081083A
 * selects 190/230 for the record's +0xB4 and the script-height words, then
 * 001C6380 (the place hook) builds the record's +0xD0 and node matrix and
 * 001A2370 re-transforms its collision cell by that +0xD0. 0, or -1 fault. */
int em_area11_interaction_host_elevator_state0(void);
/* The panel pool record's original address (its +0x14), which 00157F60
 * stores in D_008106D0. */
void em_area11_interaction_host_set_panel_address(uint32_t panel);
/* The pool record of the owner with EMIS id `source_id` (the panel, the
 * terminal or an item owner), bound by its node's first call before any
 * state-0 work (census L07): its 001B17A0 publication pushes this record onto
 * the collision world's class lists, and 001A2370 re-transforms its cell.
 * 0, or -1 fault (unknown owner, twice, or a placement that is not the
 * owner's). */
int em_area11_interaction_host_bind_actor(uint32_t source_id, EmActor *actor);
/* 001B17A0(owner) of another AREA11 owner (the drums' visibility worker)
 * through the same services as the panel, the terminal and the items
 * (001B1630 over the camera, 001B1B70 over the collision world's lists).
 * `view` carries the record's +0x02, +0x03, +0x0D, +0x2E and +0xB0..+0xB8;
 * its +0x01 and actor->drawn get the 001B1630 byte. 0 culled, 1 drawn, -1
 * fault (the host is not loaded, or a latched services fault). */
int em_area11_interaction_host_offer_001B17A0(EmActor *actor, EmOwnerServicesOwner *view);
/* Roger's pool record (census L22): the EMIS Roger record bound to its
 * +0x00 / +0x02 / +0x0B, the record the owner token. 0, or -1. */
int em_area11_interaction_host_bind_roger(EmActor *actor);
/* The fence door 001BC350's pool record (census L18): the EMIS door record
 * (source 0x82A3C0) bound to its +0x00 / +0x02 / +0x0B, the record the owner
 * token of the Use scan's claim and of the script host's takeover; *source
 * receives the EMIS record. 0, or -1. */
int em_area11_interaction_host_bind_door(EmActor *actor, const EmInteractionSceneOwner **source);
/* 001B1630(x, y, z): the camera cone / range gate over D_008105D0 /
 * D_00810600 (g.cam.eye / g.cam.fwd), the byte 001B17A0 and 001B1B30 store. */
int em_area11_interaction_host_visible_001B1630(const float position[3]);
/* Every AREA11 status screen (a pending request D_008106B0 != 0, or the
 * START/TRIANGLE hub): 0020E060 (open, 1 or -1), 0020CDC0 (page, 0 waiting
 * / 1 exit done / -1) and the status frames' draw. clear_route marks a
 * status screen opened outside the host (the legacy em_hud screen of
 * other scenes); route reports the open one's route. */
int em_area11_interaction_host_status_open(void);
int em_area11_interaction_host_status_page(const EmStatusInput *input);
int em_area11_interaction_host_status_render(EmGfx *gfx);
void em_area11_interaction_host_status_clear_route(void);
int em_area11_interaction_host_status_route(void);
/* The live message service's host hook (em_message_live.h, WP-8): the
 * slot-0 face talk 001D06E0. */
const EmMessageLiveHost *em_area11_interaction_host_message_host(void);
/* Store the shared frame view to its canonical storage after an owner
 * outside the host (the fixture's pickup adapter) wrote it directly. */
void em_area11_interaction_host_camera_fields(void);
/* OriginalDD980 publication after direct actual-camera vector commands. */
int em_area11_interaction_host_camera_publish(void);
/* 001B82D0's frame events (001AEB60(4), 001D2610(0), 001AEBA0(4),
 * 001CA770, 001D25F0(480), 001FAE70(0), 001AEE10(4, 0)) for a script the
 * AREA11 script host runs, over the same bindings as the host's own
 * scripts. 1 accepted, -1 refused or a fault (latched). */
int em_area11_interaction_host_frame_event(EmInteractionFrameEvent event);
/* A script owner outside the host (the truck trigger, the director, Roger)
 * claims the shared player token after its op07 wrote the selector; the
 * takeover itself is the player stage's (0015B130's prelude admits the
 * player, 0015B530's 00182DF0 releases it when the selector clears; the
 * special bank's regions are mapped into the record's pose storage here).
 * 1 claimed (or already its), -1 refused. */
int em_area11_interaction_host_claim_script(const void *owner);
int em_area11_interaction_host_owns(const void *owner);
/* 1 while a script owner (em_area11_interaction_host_claim_script) holds
 * the shared player takeover, else 0. */
int em_area11_interaction_host_script_held(void);
int em_area11_interaction_host_failed(void);

#endif
