/* em_equipment_live.h - the player's equipment nodes bound live (census lane
 * L28: 0018A6B0 x7 and its routines, em_player_equipment). Docs:
 * docs/PLAYER_EQUIPMENT.md section 8.
 *
 * This module adds no behaviour of its own. It keeps each node's bytes the
 * translation reads or writes (EmPlayerEquipmentNode) beside its pool record
 * and binds the translation's workers:
 *
 *   0018A8D0's model bind   001C6120 over D_0028A56C (the Roger export's
 *                           table and equipment models), 001CA6E0
 *                           (em_roger_actor_original), 001C6150 (the model's
 *                           +0x08), 001AF780 on the one bone-slot stack
 *                           (em_area11_boxes' 001AF710 world),
 *                           bone_init_default_1 001C62C0 and 001C9610
 *                           (em_owner_services_original)
 *   the SDK VU0 leaves      001026A0 / 00102760 (em_effect_original),
 *                           001026D0 (em_sdk_vu0.h), 001028B8
 *                           (em_player_hang_vadd), 001028D0 (VSUB.xyzw),
 *                           001029C0 / 00102BB0 (em_owner_services_original)
 *   0015C310(player, 1)     the bindings' 0015C310 spawn (the equipment
 *                           change's respawn of the flavour-2 children)
 *   001AFC10                the pool free; its 001AF800 pushes the node's
 *                           slots back (em_equipment_live_001AF800)
 *
 * Views: the player record image (player_states_actor_mut), the player's
 * bone world matrices (the record pose's node records, slots 0..20),
 * D_008106C6 / C7 / CC (the request block), D_00810CA4 / CA6 (progress),
 * the slot count D_00275BCC, and the ELF windows D_0024A220.., D_00248B98 /
 * D_00248C78 (assets/effect_tables.emet, em_effects_live).
 *
 * The draw method +0x4C (001CAA00) is em_owner_draw_live over the node's
 * owner view (its record bytes and bone slots), with the model from the
 * equipment bank (the Roger export's models at their original addresses,
 * bound by the 001CA6E0 worker); the units are drawn with the walk's
 * units. In a frame whose player record is not the displayed pose (the
 * opening's hand-off, em_scene_bindings_player_record_drawn) the port's
 * own player mesh carries the equipment models and no unit is built. The
 * method also records that the node drew and whether its bone 0 is the
 * player's node 4 (the knife: 14) for the tick log.
 *
 * Every callee the route never reaches (the aim drawers 001854E0 / 00185760,
 * the one-shots 001861C0 / 001869A0 / 00186A60 / 001872C0 / 00187CC0 and
 * their 001B61C0 / 001EFEB0 / 001F4010, 00188C70, 00189090 / 00189330 /
 * 001899C0 / 00189A20, the lamp 00187780, and the knife's probe and effect
 * workers) is bound to a fault (docs/PLAYER_EQUIPMENT.md 4.2).
 *
 * Fail-stop: the first fault is latched (em_equipment_live_fault()); every
 * later entry returns -1. */
#ifndef EM_EQUIPMENT_LIVE_H
#define EM_EQUIPMENT_LIVE_H

#include <stdint.h>

#include "game/em_actor_pool.h"
#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The player equipment's 0015C310(player, arg1) (the bindings'). 0 / -1. */
typedef int (*EmEquipmentLiveSpawn)(int32_t arg1);

/* At each area build (after the pool reset). 0, or -1 (the tables are not
 * loaded). */
int em_equipment_live_attach(EmActorPool *pool, EmSceneState *scene);
void em_equipment_live_set_spawn(EmEquipmentLiveSpawn spawn);
uint32_t em_equipment_live_fault(void);
/* Canonical represented fields only; an unmodelled hole, stale generation
 * or freed node has no view. Encoded native pointers are read-only. */
void *em_equipment_live_field(uint32_t address, size_t size, int write);
typedef struct {
    uint32_t address, size;
    uint8_t *bytes;
    int writable;
} EmEquipmentLiveRegion;
/* Returns the required count, writing at most capacity entries. */
size_t em_equipment_live_regions(uint32_t address, EmEquipmentLiveRegion *out, size_t capacity);
/* The pool record of a live node of this module at its original address
 * (the melee states' +0x18 knife link writes its +0x00, +0x0A and +0x36
 * header fields), or NULL. */
EmActor *em_equipment_live_node_actor(uint32_t address);
typedef int (*EmEquipmentAimFireCall)(uint32_t entry, uint32_t actor);
void em_equipment_live_set_aim_fire(EmEquipmentAimFireCall call);
/* The gun lamp 00187780(node, a1, a2) with D_00275B40 = `bones` for the
 * call (em_aim_fire_binding_run_lamp). NULL: the call faults. */
typedef int (*EmEquipmentLampCall)(uint32_t node, int32_t a1, int32_t a2, uint32_t bones);
void em_equipment_live_set_lamp(EmEquipmentLampCall call);
/* 00188630's 001F4010(index, 0x700036A0): the shell casing's seed, `at` the
 * node's copy of 0x700036A0..0x700036DF (16 words). NULL: a fault. */
typedef int (*EmEquipmentCasingCall)(int32_t index, const uint32_t *at);
void em_equipment_live_set_casing(EmEquipmentCasingCall call);
/* The knife's (flavour 4: 0018A1F0 / 00189D30) callees through a
 * composition: call(fn, a, na, f12, nf, spad, v0) runs the original call
 * with those argument registers after copying the node's staged scratch
 * `spad` (0x700038A0..0x700038DF, 16 words) to the scratchpad and before
 * copying it back; read(address, out, n) reads original memory; bytes
 * (address, n) a grid node record's bytes; temp(words, &address) stages a
 * quadword the original hands by address. NULL: the knife's callees fault. */
typedef int (*EmEquipmentWorldCall)(uint32_t fn, const uint32_t *a, unsigned na, uint32_t f12, unsigned nf,
                                    uint32_t *spad, uint32_t *v0);
typedef int (*EmEquipmentWorldRead)(uint32_t address, void *out, uint32_t size);
typedef const uint8_t *(*EmEquipmentWorldBytes)(uint32_t address, uint32_t size);
typedef int (*EmEquipmentWorldTemp)(const uint32_t words[4], uint32_t *address);
void em_equipment_live_set_world(EmEquipmentWorldCall call, EmEquipmentWorldRead read, EmEquipmentWorldBytes bytes,
                                 EmEquipmentWorldTemp temp);
uint32_t em_equipment_live_current_bones(void);

/* 0018A6B0 over the node's record: 1 while allocated, 0 after its 001AFC10
 * free, -1 on a fault. */
int em_equipment_live_tick(EmActor *actor);

/* 001AF800 for a node of this module (em_roger_actor_001AF800, which pushes
 * its slots back in its own loop):
 * 1 handled, 0 not one of these nodes, -1 a fault. */
int em_equipment_live_001AF800(EmActor *actor);

/* The capture log: one node (its original record bytes this module holds). */
typedef struct {
    uint32_t address;           /* the pool record */
    uint8_t head[16];           /* +0x00..+0x0F */
    uint32_t model;             /* +0x44 (original address) */
    uint32_t method;            /* +0x4C */
    uint32_t bone0[16];         /* the first bone slot's world matrix (bits) */
    uint8_t drew;               /* the method ran in its last tick */
    uint8_t at_node;            /* ... with bone 0 = the player's node 4
                                 * (the knife: 14) */
} EmEquipmentLiveNode;
int em_equipment_live_nodes(EmEquipmentLiveNode *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* EM_EQUIPMENT_LIVE_H */
