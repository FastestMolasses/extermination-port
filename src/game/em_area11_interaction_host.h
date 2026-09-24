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

EmInteractionScene *em_area11_interaction_host_scene(void);
EmInteractionRuntime *em_area11_interaction_host_shared(void);
EmPanelRuntime *em_area11_interaction_host_panel(void);
EmElevatorRuntime *em_area11_interaction_host_elevator(void);
EmStatusRuntime *em_area11_interaction_host_status(void);
/* The hub's static actor pool and model draws (em_status_models), for the
 * level smoke. */
const struct EmStatusModels *em_area11_interaction_host_status_models(void);
const EmInteractionProjection *em_area11_interaction_host_projection(void);

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
 * advance only inside the shared interaction worker. */
int em_area11_interaction_host_player(void *unused);
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
 * `param` are the actor's +0x03 and +0x0D), then one owner update per call
 * (1 allocated, 0 freed: the node frees itself, -1 fault). */
int em_area11_interaction_host_pickup_state0(uint32_t source_id, uint8_t model, uint8_t param);
int em_area11_interaction_host_pickup_tick(uint32_t source_id);
/* 00827B10 state 0's placement (0x827B54..0x827BF0): the floor byte
 * D_0081083A selects 190/230 for the actor's +0xB4 and the script-height
 * words, then 001C6380 builds the actor's matrix. 0, or -1 fault. */
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
/* The live message service's host hooks (em_message_live.h, WP-8): the
 * step-F gate (held while the status page layer runs) and the slot-0 face
 * talk 001D06E0. */
const EmMessageLiveHost *em_area11_interaction_host_message_host(void);
/* Store the shared frame view to its canonical storage after an owner
 * outside the host (the fixture's pickup adapter) wrote it directly. */
void em_area11_interaction_host_camera_fields(void);
/* OriginalDD980 publication after direct actual-camera vector commands. */
int em_area11_interaction_host_camera_publish(void);
int em_area11_interaction_host_failed(void);

#endif
