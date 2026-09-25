/* em_player_closure_live.h - the live binding of the player's FLOOR state
 * closure and the Use chain (census lanes L02 / L04 / L09; docs/FIRST_CONTROL.md
 * "FLOOR state closure", "Binding").
 *
 * Nothing here is a new translation. Every state callback and worker is the
 * verified translation its module doc's "Binding" section names (fall,
 * hang, recovery, ladder climb / entry, closure 0E-18 and 10-12-19, slide,
 * climb, weapon states A / B, major2, reaction, running jump, the Use
 * dispatcher and the record-level helpers), bound over the one live player
 * record (player_states_actor_mut()), the one record pose
 * (player_pose_record_host()), the original collision world
 * (em_collision_world_*) and the stage workers' host. A callee with no
 * translation is a fail-stop worker that names its original (off-route:
 * docs/FIRST_CONTROL.md "Missing today"); reaching one fails the stage.
 *
 * The scratchpad words several modules share are one storage: 0x70003A20
 * is the binder's EmPlayerLandScratch.s3A20 (every pointer-shaped slot is
 * bound to it; the modules that keep the word by value load it at entry and
 * store it back at exit and around every worker call), 0x700038A0..AC the
 * same scratch's s38A0, and the probe hit (0x700031B0..D8) the collision
 * world's move / segment state as the last query left it. */
#ifndef EM_PLAYER_CLOSURE_LIVE_H
#define EM_PLAYER_CLOSURE_LIVE_H

#include "game/em_player.h"
#include "game/em_player_stage_workers.h"
#include "game/em_player_use_dispatch.h"
#include "game/em_pose_host_workers.h"

/* Fill b->stage.state[] / state2[] with the closure's and the Use roots'
 * callbacks, the 0015B530 routines 00162DB0 / 00163B40 in *major4, the
 * pose host's 00178910 callees, and the Use chain's workers. `stage_host`
 * is the stage workers' host (0021C270 / 0021C350 / 00174A50 run on it),
 * `pose` the record pose's host. The collision world must be loaded
 * (em_collision_world_bind_player has filled b). 0, or -1 (nothing bound). */
int em_player_closure_live_bind(EmPlayerStatesBinding *b, EmPlayerStageHost *stage_host,
                                EmPoseHost *pose, EmPlayerStageMajor4 *major4);

/* The Use dispatcher 00160220 over the live record with every worker
 * bound (the scan is `scan`: the interaction host's 00184BA0 with its
 * claim). NULL until em_player_closure_live_bind succeeded. */
const EmPlayerUseWorkers *em_player_closure_live_use(void);
void em_player_closure_live_set_scan(int (*scan)(void *context, EmPlayerLiveActor *actor,
                                                 int *result),
                                     void *context);

/* One Use press: 00160220 over the live record (the scene words refreshed
 * first; an SDK worker's recorded fault fails it). *result is its return
 * (1: an action took the press). 0, or -1 on a fault. */
int em_player_closure_live_use_press(EmPlayerLiveActor *actor, int *result);

/* 0017B490(p, cmd, idx, tbl) over the record (em_loco_0017B490 on the record
 * pose's regions): the stage callees' clip_lookup shape. -1 until bound. */
int em_player_closure_live_0017B490(void *context, EmPlayerLiveActor *actor, int cmd, int idx,
                                     int tbl, int16_t *clip);

/* The pad button-assignment block 0x70003B74..0x70003B82 (001AF470 with
 * config 0, eight halfwords; word 6 is 0x70003B80). Zero until bound. */
const uint16_t *em_player_closure_live_pad_config(void);

/* Fail-stop workers reached (each is reported once on stderr). */
unsigned em_player_closure_live_faults(void);

#endif
