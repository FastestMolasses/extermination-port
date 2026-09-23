/* Native AREA11 services for the original panel/elevator interaction.
 * Live since WP-4 (docs/AREA11_INTERACTION_HOST.md): the scene bindings
 * load it at the AREA11 state-0 rebuild (after 001B6990), bind the panel
 * 00159210 (area11[18]) and terminal 00827B10 (area11[19]) pool nodes to
 * the panel/elevator ticks, install the Use and player-stage hooks, publish
 * at 001AAD00, route request-opened status screens through the status
 * functions below and tick the message at main-loop step F. */
#ifndef EM_AREA11_INTERACTION_HOST_H
#define EM_AREA11_INTERACTION_HOST_H

#include "game/em_elevator_runtime.h"
#include "game/em_frame.h"
#include "game/em_interaction_scene.h"
#include "game/em_interaction_projection.h"
#include "game/em_panel_runtime.h"
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
/* player_use_set_hook worker: 00160220's head over the published list
 * (00184BA0/00183EF0). 1 an owner won (3B8D = 3, 001798D0 done), 0 no
 * Use or no winner (the rest of 00160220 stays the player's), -1 fault. */
int em_area11_interaction_host_use(void *unused);
/* Called at the respective original pooled-owner positions (state 1 of
 * 00159210 / 00827B10, including their 001B17A0 publication tail). */
int em_area11_interaction_host_panel_tick(void);
int em_area11_interaction_host_elevator_tick(void);
/* 00827B10 state 0's placement (0x827B54..0x827BF0): the floor byte
 * D_0081083A selects 190/230 for the actor's +0xB4 and the script-height
 * words, then 001C6380 builds the actor's matrix. 0, or -1 fault. */
int em_area11_interaction_host_elevator_state0(void);
/* The panel pool record's original address (its +0x14), which 00157F60
 * stores in D_008106D0. */
void em_area11_interaction_host_set_panel_address(uint32_t panel);
/* 001AAD00's interactive-list swap. */
void em_area11_interaction_host_publish(void);
/* Status screens opened on a pending request (D_008106B0 != 0): 0020E060
 * (open, 1 or -1), 0020CDC0 (page, 0 waiting / 1 exit done / -1) and the
 * status frames' draw. clear_route marks a status screen opened without a
 * request (the interim hub, WP-5); route reports the open one's route. */
int em_area11_interaction_host_status_open(void);
int em_area11_interaction_host_status_page(const EmStatusInput *input);
int em_area11_interaction_host_status_render(EmGfx *gfx);
void em_area11_interaction_host_status_clear_route(void);
int em_area11_interaction_host_status_route(void);
/* Shared message service follows all ordinary script workers. */
int em_area11_interaction_host_message_tick(int busy155, int busy156);
void em_area11_interaction_host_message_render(EmGfx *gfx);
/* The message block D_002821B0 as the host holds it: the kind and token
 * the message command 001B7D60 stored at the start, the presenter's phase
 * D_002821B4; all zero while idle (001FC9B0's teardown clears the block).
 * Instrumentation. */
void em_area11_interaction_host_message_block(uint32_t block[3]);
/* The two above as the main loop's step-F service (em_frame.h). */
const EmFrameMessageService *em_area11_interaction_host_message_service(void);
/* Store the shared frame view to its canonical storage after an owner
 * outside the host (the fixture's pickup adapter) wrote it directly. */
void em_area11_interaction_host_camera_fields(void);
/* OriginalDD980 publication after direct actual-camera vector commands. */
int em_area11_interaction_host_camera_publish(void);
int em_area11_interaction_host_failed(void);

#endif
