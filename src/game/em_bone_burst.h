/* em_bone_burst.h - the bone-burst effect node: the effect records whose
 * callback is 0022BBC0 (the entity table's 0x27, 0x40 and 0x51; in the
 * first level the flame's contact spawns 0x80000027 through 001EFE00 on the
 * player, subtype +0x0D = 9). Docs: docs/DAMAGE.md section 3.
 *
 * This module adds no behaviour of its own. The behaviour 0022BBC0 is
 * em_area01_ui_0022BBC0 (with 0022B700 / 0022B7A0 / 0022BB70; AREA01_UI.md),
 * run through em_aim_fire_runtime's extended effect tick with D_00275B40 =
 * the node's +0x110 (the walk's 001CB590). The node's header, +0x24,
 * +0x1F0.. and pool fields are em_effects_live's (a KIND_OTHER effect node).
 * Kept here:
 *   +0x110..+0x123  the five slot words 0022B700(seq, 5)'s 001AF780 pops
 *                   store
 * and views of the slots' bytes +0x00..+0xCF: the one slot stack's own raw
 * slot records (em_area11_boxes), which 0022BBC0 uses as its 65-slot ring
 * (0022BB70: 13 sixteen-byte slots per bone slot).
 * Workers (em_bone_burst_call), while the node's behaviour runs:
 *   001AF780()      em_roger_actor_001AF780 on the one slot stack
 *   001CB5B0(count) nothing: D_00275B40 already names the node's +0x110
 * and 001AF800 at its free (em_area11_boxes delegates).
 *
 * Fail-stop: the first fault is latched; every later entry returns -1. */
#ifndef EM_BONE_BURST_H
#define EM_BONE_BURST_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_actor_pool.h"
#include "game/em_aim_fire_target.h"
#include "game/em_pose_host_workers.h"
#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_BONE_BURST_CALLBACK 0x0022BBC0u

int em_bone_burst_attach(EmActorPool *pool, EmSceneState *scene);
uint32_t em_bone_burst_fault(void);
/* Before the node's behaviour runs: a node this module has not seen in this
 * generation starts with no slots. NULL ends the tick. 0, or -1. */
int em_bone_burst_set_current(EmActor *actor);
int em_bone_burst_owns(const EmActor *actor);
/* +0x110 words (read-only) and the held slots' bytes, by address. */
void *em_bone_burst_field(uint32_t address, size_t size, int write);
/* The node's regions (+0x110 words; each held slot's 0xD0 bytes, writable).
 * Returns the required count. */
size_t em_bone_burst_regions(uint32_t address, EmPoseRegion *out, size_t capacity);
/* 001AF780 / 001CB5B0 while a burst node is current: 1 handled, 0 not, -1. */
int em_bone_burst_call(EmAimFireTargetCall *f);
/* 001AF800 for a burst node: 1 handled, 0 not one, -1 a fault. */
int em_bone_burst_001AF800(EmActor *actor);

#ifdef __cplusplus
}
#endif

#endif /* EM_BONE_BURST_H */
