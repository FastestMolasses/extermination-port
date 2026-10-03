/* em_area01_arrival.h - the level exit's AREA01 arrival (route beat 15,
 * docs/FIRST_LEVEL_EXIT.md section 7).
 *
 * Roger's departure requests 001B0C60(1, 0, 4); 001AD010 / 001ADF50 and the
 * area streamer 001FFCD0 load AREA01 sub 0; 0x1AE040 state 0 rebuilds the
 * area: 001B07C0(0) places the player at spawn entry 4 and 001B6990 /
 * 001C5C50 spawn AREA01's owners. That frame is the capture's first frame of
 * control in AREA01, where the first level ends: every AREA01 frame after
 * it (its owners' behaviours, its world frames) is level 2 and faults
 * (em_scene_bindings.c w_001AD4D0).
 *
 * This module is 001B6990's / 001C5C50's binder for AREA01's nodes: it
 * attaches no behaviour (the spawner then leaves the node unbound, and a
 * walk that reached it would fault at its callback) and records what was
 * spawned, for the level smoke and the census. */
#ifndef EM_AREA01_ARRIVAL_H
#define EM_AREA01_ARRIVAL_H

#include <stdint.h>

#include "game/em_actor_pool.h"
#include "game/em_actor_roster.h"

/* EmActorRosterBindFn for AREA01's nodes: records the node, binds nothing;
 * 0. A node beyond the record capacity fails (-1). */
int em_area01_arrival_bind(void *ctx, EmActor *actor, const EmActorRosterSpawned *spawned);

/* Drop the records (0x1AE040 state 0's pool reset reaches every area). */
void em_area01_arrival_reset(void);

/* The nodes spawned since the last reset: their count, and for node i its
 * original callback (+0x10) and record address (0 for 001C5C50's). */
unsigned em_area01_arrival_count(void);
int em_area01_arrival_node(unsigned i, uint32_t *callback, uint32_t *record);

#endif
