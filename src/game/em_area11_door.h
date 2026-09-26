/* em_area11_door.h - the AREA11 fence door (placement record 0, callback
 * 001BC350, EMIS source 0x82A3C0) on its original owner (census L18; route
 * beat 09; docs/DOOR_ORIGINAL.md "Binding").
 *
 * The controller is em_door_original_tick (001BC350 with 001BBDA0, 001BC0E0,
 * 001BC240, 001BC290 and 001BC300) over the pool record, its hooks bound to
 * the original owners:
 *   lifecycle 0   001BBDA0's 001B0F60 (em_slg_001B0F60): 001B0EA0 through
 *                 em_area11_boxes (the exported bank, the shared bone-slot
 *                 stack: +0x09 / +0x0C), D_0028A574 at +0x40 and
 *                 bone_init_default_2(door, 0) = the source bank's clip 0
 *   phase 0       001BBE40 (em_door_transit_kickoff): the side latch, the
 *                 program patch (0x24DC14 / 0x24DC54 / 0x24DC8C and
 *                 001BBD60's 0x24DC58, em_sdf_001BBD60 over the D_0024DB80
 *                 pair source.emdo carries), the player's +0xC4
 *                 (player_pose_face) and 00182F90 (player_pose_align), then
 *                 001BA1A0 / 001BA1F0 on the AREA11 script host
 *   phases 1..3   001BC0E0: anim_advance_time(door, 1.0) while the block's
 *                 +0x0C byte is set, then 001BA1F0 (the script host: the
 *                 program's op07 frame, op0D camera, op0A player clip, op0B
 *                 door clip with its 001FBD50 cue, op02 wait)
 *   phase 4       001BC240: the advance, then 001BC150
 *                 (em_door_transit_commit: 001AEDE0(4, 0), B8 = 2, B7 = the
 *                 destination row's side byte)
 *   phase 5       001BC290: the advance; once D_008106B8 is clear (0x1AE040
 *                 state 4's 001AFCF0), anim_clip_init(door, 0, 0, 0) and
 *                 +0x0B = 0
 *   001BC300      001C68C0 (the runtime's pose), 001B1B30 (em_sdf_001B1B30:
 *                 001B1630 over the camera, 001B1B70 onto the collision
 *                 world's lists; class 0x85 = the interactive list the Use
 *                 scan reads next frame) and +0x4C (001CAA00: the port's
 *                 actor draw of the runtime's model at its palette)
 *   lifecycles 2/3  001AFC10.
 * The Use scan (00184BA0 through the interaction host) selects the door with
 * 00183EF0's class-5 branch (em_door_candidate) and arms its +0x0B bit 2.
 *
 * Storage. The EmActor fields are the canonical record bytes (+0x00 status,
 * +0x01 visibility, +0x02 class, +0x03 subtype, +0x04 lifecycle, +0x05 phase,
 * +0x09, +0x0B armed, +0x0C, +0x2E side, +0x56 link, +0x80 scale, +0xB0,
 * +0xC0, and the script block +0x1F0.., whose +0x0C is 001BC0E0's advance
 * gate and +0x0E the anim_advance_time flags). The door id +0x34 has no
 * EmActor field and no reader outside the door: it lives here. The pose
 * (the source bank's playback, the node matrices) is the runtime's
 * (em_door_original_runtime). */
#ifndef EM_AREA11_DOOR_H
#define EM_AREA11_DOOR_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_actor_pool.h"
#include "game/em_scene_state.h"

#define EM_AREA11_DOOR_CALLBACK 0x001BC350u

/* The area build: drop the owner (its resources and mesh stay). */
void em_area11_door_reset(EmActorPool *pool, EmSceneState *scene);
/* One owner call of the door's node. 1 allocated, 0 the node freed itself,
 * -1 a fault (a line on stderr names it). */
int em_area11_door_tick(EmActor *actor);
/* 001C67E0(door, clip, blend, start) from the door program's op0B (the
 * script host): the source bank's clip; the runtime admits blend 0 and
 * start 0 only (0x24DC40 passes both 0). 0, or -1. */
int em_area11_door_clip_init(EmActor *actor, int16_t clip, float blend, float start);
/* The door's record for the tick log and the level smoke: its first 16
 * bytes (as the route rows' door_r0 "h") and the script block's first 16
 * (+0x1F0..+0x1FF, "s1F0"). 1 while a door node is bound, else 0. */
int em_area11_door_state(uint8_t header[16], uint8_t block[16]);
/* Free the resources (scene unload). The door's +0x4C builds its unit
 * through em_area11_boxes_door_draw (em_owner_draw_live). */
void em_area11_door_shutdown(void);

#endif
