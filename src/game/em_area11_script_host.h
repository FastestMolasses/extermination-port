/* em_area11_script_host.h - the AREA11 overlay owners' event scripts on the
 * live path (census L19): em_area_script (001BA1A0 / 001BA1F0 with the
 * ftab_0024D880 handlers) over the canonical storage, with the overlay
 * script images of tools/export_area11_scripts.py (docs/AREA_SCRIPT.md
 * section 6, docs/SCRIPT_HOST_WORKERS.md section 3).
 *
 * Bound owners: the truck trigger 008251E0 (0x8292C0, census L23).
 *
 * Storage. Every byte a handler reaches is one pointer of EmAreaScriptWorld
 * into its canonical storage: the scratchpad bytes and the request block in
 * EmSceneState, the camera bytes D_008101E1/E3/E4/E6 in the port camera
 * g.cam, the message block D_002821B0..BC of the live message service, the
 * transition substate D_0028A9A0, the owner's +0xB0 / +0xC0 in its pool
 * record. The four-lane vectors whose canonical storage has three lanes
 * (the camera's +0x10 / +0x20 = g.cam.eye_des / tgt_des, the working
 * D_008105D0 / E0 / F0 = g.cam.eye / tgt / up, the player's +0xA0 = g.pos,
 * +0xB0 = the pose's hip, +0xC0 with +0xC4 = g.yaw) are one view, loaded
 * before every script tick and every worker call and stored after; only
 * their w lanes live in the view. The script block (owner +0x1F0..+0x21F)
 * lives in the owner's pool record and is loaded and stored around each
 * start and tick. D_008101E2 has no port storage (only 001B82D0 sub 4
 * writes it, to 0; nothing in the port reads it): it lives here.
 *
 * Workers (docs/AREA_SCRIPT.md section 6): the 001B82D0 frame events go
 * through the AREA11 interaction host's bindings (bars 001AEB60 / 001AEBA0,
 * zoom 001D2610 / 001D25F0, 001CA770, 001FAE70, 001AEE10); 00182F90 is the
 * pose host's player_pose_align; 001DD980 publishes the working camera;
 * 0011E2A8 is the one bound SDK sine, em_sdk_math_original over the
 * collision world's SDK context. Every other worker is unbound and faults
 * where a script reaches it (fail-stop).
 *
 * The player takeover. When a script's op07 has opened the scripted frame
 * (0x70003B8D != 0), the owner claims the interaction host's shared player
 * takeover (the stand-in for 0015B130's 00182B30 admission that the panel
 * and elevator scripts use); the host releases it when the selector
 * clears. */
#ifndef EM_AREA11_SCRIPT_HOST_H
#define EM_AREA11_SCRIPT_HOST_H

#include <stdint.h>

#include "game/em_actor_pool.h"
#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The area build: drop every owner's host and the images (the scripts are
 * mutated in place, so each visit loads fresh ones: SCRIPT_HOST_WORKERS.md
 * section 2). */
void em_area11_script_host_reset(EmActorPool *pool, EmSceneState *scene);
/* 001BA1A0(actor + 0x1F0, entry). 0, or -1 (reported; the caller faults). */
int em_area11_script_host_start(EmActor *actor, uint32_t entry);
/* 001BA1F0(actor): *result = 0 running, 1 finished (or not active), 3
 * aborted by the skip path. 0, or -1 on a fault (reported). */
int em_area11_script_host_tick(EmActor *actor, int32_t *result);

#ifdef __cplusplus
}
#endif

#endif
