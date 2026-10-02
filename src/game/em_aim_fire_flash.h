/* em_aim_fire_flash.h - the muzzle node: the record 00187CC0 allocates
 * through 001F4F40 (pool class 0xC, +0x10 = 001F5040), its bytes beyond
 * the pool header and its model-node workers. Docs: docs/AIM_FIRE.md
 * section 9.1.
 *
 * This module adds no behaviour of its own. Its behaviour 001F5040 is
 * em_area00_fx_001F5040 (em_aim_fire_world_live), run by the AREA11
 * binder's row through em_aim_fire_runtime. It keeps, per record, the
 * bytes the original writes before it reads them, each readable only after
 * a write since the allocation (001AFA90 leaves them as the previous
 * occupant had them, and nothing reads them first):
 *   +0x28        the frame halfword (001F5040)
 *   +0x44, +0x4C the model and the draw method (001CA5E0(node, model, 2):
 *                +0x4C = 001CACB0, decomp 001CA5E0 / 001CA5F0)
 *   +0xD0..+0x10F the node matrix (00187CC0's 00102958 and its +0x100 /
 *                +0x10C stores)
 *   +0x110       the bone slot words (001AF780 on the one stack)
 *   +0x230..+0x23B the step vector (001F5040's +0x1F0 + 0x40..0x48)
 * and the typed views of its slots (EmOwnerBone, their +0x90 matrices).
 * The pool header and +0x60..+0x8F, +0xB0..+0xCF are the pool record's
 * (EmActor).
 *
 * Workers (em_aim_fire_flash_call), each the callee's one translation:
 *   001C6120(bank, id)   em_area11_roger_001C6120 over D_0028A56C (the
 *                        Roger export: the library models 0x07..0x0F)
 *   001CA5E0(n, m, 2)    +0x44 = m, +0x4C = 001CACB0; the model joins
 *                        this module's table-less bank at its address
 *   001C6150(model)      em_owner_services_001C6150 (the model's +0x08)
 *   001AF780()           em_roger_actor_001AF780 on the one slot stack
 *   001CB5B0(count)      nothing: D_00275B40 is the node's +0x110 (the
 *                        runtime points it there for the tick, as the
 *                        walk's 001CB590 does)
 *   001C62C0(node)       em_owner_services_001C62C0 over the model, slots
 *   001C63D0(node)       001C9610(node + 0x110, +0x0C, node + 0xD0):
 *                        em_owner_services_001C9610 (decomp 001C63D0)
 *   001D80E0(pos, c)     em_effect_original_001D80E0 -> 001D7FA0 on the
 *                        render context's point lights (em_point_light)
 *   001CACB0(node)       the +0x4C draw: em_anim_rest_001CACB0 (the tail
 *                        call 001CABA0(node, +0x44)) with its 001CABA0
 *                        em_owner_draw_live_001CABA0
 *
 * Fail-stop: the first fault is latched (em_aim_fire_flash_fault()); every
 * later entry returns -1. */
#ifndef EM_AIM_FIRE_FLASH_H
#define EM_AIM_FIRE_FLASH_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_actor_pool.h"
#include "game/em_aim_fire_target.h"
#include "game/em_pose_host_workers.h"
#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AIM_FIRE_FLASH_CALLBACK 0x001F5040u

/* At each area build, after the pool reset and the boxes' 001AF710 (the
 * Roger export must be loaded). 0, or -1. */
int em_aim_fire_flash_attach(EmActorPool *pool, EmSceneState *scene);
uint32_t em_aim_fire_flash_fault(void);
/* A record 001AFA90 has just allocated for 001F4F40 (its +0x10 not yet
 * stored): its storage starts unwritten. 0, or -1. */
int em_aim_fire_flash_claim(EmActor *actor);
/* 1 when `actor` is a claimed muzzle node of this generation. */
int em_aim_fire_flash_owns(const EmActor *actor);
/* The node whose behaviour runs (its slots receive 001AF780's pops); NULL
 * outside the tick. */
void em_aim_fire_flash_set_current(EmActor *actor);
/* The record bytes listed above, by original address (a slot's +0x90..+0xCF
 * too). A read of a byte nobody wrote since the claim returns NULL; +0x44,
 * +0x4C and +0x110 are written only by their workers. NULL outside. */
void *em_aim_fire_flash_field(uint32_t address, size_t size, int write);
/* The readable regions of the record at `address` (written bytes only),
 * for a region-only owner's view list. Returns the required count. */
size_t em_aim_fire_flash_regions(uint32_t address, EmPoseRegion *out, size_t capacity);
/* The workers above. 1 handled, 0 not one of them, -1 a fault. */
int em_aim_fire_flash_call(EmAimFireTargetCall *f);
/* The tick log's view: the node's +0x28 halfword as held (1), or 0 (not a
 * muzzle node). */
int em_aim_fire_flash_h28(const EmActor *actor, uint16_t *value);
/* 001AF800 for a muzzle node: 1 handled, 0 not one, -1 a fault. */
int em_aim_fire_flash_001AF800(EmActor *actor);

#ifdef __cplusplus
}
#endif

#endif /* EM_AIM_FIRE_FLASH_H */
