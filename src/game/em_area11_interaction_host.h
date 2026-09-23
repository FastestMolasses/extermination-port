/* Native AREA11 services for the original panel/elevator interaction.
 * The scene's single Use arbiter owns which controller may claim these
 * services. Loading does not install another proximity scan. */
#ifndef EM_AREA11_INTERACTION_HOST_H
#define EM_AREA11_INTERACTION_HOST_H

#include "game/em_elevator_runtime.h"
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
/* Called at the respective original pooled-owner positions. */
int em_area11_interaction_host_panel_tick(void);
int em_area11_interaction_host_elevator_tick(void);
/* Shared message service follows all ordinary script workers. */
int em_area11_interaction_host_message_tick(int busy155, int busy156);
void em_area11_interaction_host_message_render(EmGfx *gfx);
/* Synchronize the actual camera fields changed by script frame commands. */
void em_area11_interaction_host_camera_fields(void);
/* OriginalDD980 publication after direct actual-camera vector commands. */
int em_area11_interaction_host_camera_publish(void);
int em_area11_interaction_host_failed(void);

#endif
