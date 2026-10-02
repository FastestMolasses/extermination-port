/* em_aim_fire_trail.h - the knife's trail node: the effect record
 * 001EFF10(0x8000000D, ...) spawns from the knife's 00189D30 (its +0x10 =
 * 001F18C0), its three bone slots and their bytes. Docs: docs/AIM_FIRE.md
 * section 9.3.
 *
 * This module adds no behaviour of its own. The behaviour 001F18C0 is
 * em_area00_fx_001F18C0 (with 001F1550 / 001F15F0), run through
 * em_aim_fire_runtime's extended effect tick with D_00275B40 = the node's
 * +0x110 (the walk's 001CB590). The node's header, +0x1F0.. and pool fields
 * are em_effects_live's (a KIND_OTHER effect node). Kept here:
 *   +0x110..+0x11B  the three slot words 001F1550's 001AF780 pops store
 * and views of the slots' bytes +0x00..+0xCF: the one slot stack's own
 *                   raw slot records (em_area11_boxes: 001AF710 zeroes
 *                   them, 001AF890 clears each pushed one), which 001F18C0
 *                   uses as an eleven-quadword point history (+0x00..+0xAF)
 *                   and ten age words (+0xA0..+0xC7), overlapping
 * Workers (em_aim_fire_trail_call), while the node's behaviour runs:
 *   001AF780()      em_roger_actor_001AF780 on the one slot stack
 *   001CB5B0(count) nothing: D_00275B40 already names the node's +0x110
 * and 001AF800 at its free (em_area11_boxes delegates).
 *
 * Fail-stop: the first fault is latched; every later entry returns -1. */
#ifndef EM_AIM_FIRE_TRAIL_H
#define EM_AIM_FIRE_TRAIL_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_actor_pool.h"
#include "game/em_aim_fire_target.h"
#include "game/em_pose_host_workers.h"
#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AIM_FIRE_TRAIL_CALLBACK 0x001F18C0u

int em_aim_fire_trail_attach(EmActorPool *pool, EmSceneState *scene);
uint32_t em_aim_fire_trail_fault(void);
/* Before the node's behaviour runs: a node this module has not seen in this
 * generation starts with no slots. NULL ends the tick. 0, or -1. */
int em_aim_fire_trail_set_current(EmActor *actor);
int em_aim_fire_trail_owns(const EmActor *actor);
/* +0x110 words (read-only) and the held slots' bytes, by address. */
void *em_aim_fire_trail_field(uint32_t address, size_t size, int write);
/* The node's regions (+0x110 words; each held slot's 0xD0 bytes, writable).
 * Returns the required count. */
size_t em_aim_fire_trail_regions(uint32_t address, EmPoseRegion *out, size_t capacity);
/* 001AF780 / 001CB5B0 while a trail node is current: 1 handled, 0 not, -1. */
int em_aim_fire_trail_call(EmAimFireTargetCall *f);
/* 001AF800 for a trail node: 1 handled, 0 not one, -1 a fault. */
int em_aim_fire_trail_001AF800(EmActor *actor);

#ifdef __cplusplus
}
#endif

#endif /* EM_AIM_FIRE_TRAIL_H */
