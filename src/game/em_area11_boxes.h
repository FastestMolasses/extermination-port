/* em_area11_boxes.h - the AREA11 crates (001551B0, records area11[3..6])
 * and drums (00156620, area11[14]/[15]) on their original owners (census
 * L25; docs/CRATES_DRUMS_ORIGINAL.md "Binding").
 *
 * Each pool node runs em_crate_original_tick / em_drum_original_tick over
 * its own record. The record bytes EmActor stores are the canonical copy
 * (+0x00..+0x0E, +0x36, +0x52, +0x56, +0x9A, +0xB0, +0xC0 and the +0x1F0
 * block); the owner's other fields (+0x28, +0x2A, +0x34, +0x38, +0xD0 and the
 * +0x110 bone slots) live in the node's slot here. The workers:
 *
 *   001B0EA0 / 001C62C0 / 001C6380  em_owner_services over the exported
 *                  AREA11 world model bank (*D_0028A59C,
 *                  assets/scene_snow/world_models.emwm) and the bone-slot
 *                  stack of 001AF710 (D_00275BD0 / D_00275BCC, 0x480 slots at
 *                  every area build), popped by 001AF780 and pushed back by
 *                  001AF800 (its own inline loop) when the pool frees the
 *                  record
 *   001B1B70 / 001B1D20  the collision world's class lists (the crates'
 *                  cells, uids 7..10, and the drums', uids 5 and 6)
 *   001B17A0      the interaction host's services (the drum's visibility)
 *   0019AB20      em_actor_collision_owner_probe over the collision world
 *   001A2370      em_collision_world_retransform_001A2370
 *   00122BB8      em_random_next; 001FBD50 em_sfx_play_at
 *   +0x4C         001CAA00 (the only method 001CA6E0 installs):
 *                 em_owner_draw_live builds the original unit (001CA7B0,
 *                 001C7420 with 001D89D0, 001D1F80, 001CA940 over the bank)
 *                 and the object-unit renderer draws it at the frame's end
 *                 (docs/OWNER_DRAW.md "Binding")
 *
 * AREA01 nest initialization optionally borrows the original registry and
 * reads canonical taken bits through the existing actor-roster owner (see
 * the binding below and LEVEL2_CRATE_REGISTRY.md). Its 001AFA90 child copy
 * remains an explicit fail-stop. The default AREA11 placements do not reach
 * a nest group. See CRATES_DRUMS_ORIGINAL.md for other damage-path coverage.
 * D_002468B0 / D_00246A00 / D_00246A10 come from
 * assets/scene_snow/box_tables.emrg (tools/export_box_tables.py).
 *
 * The truck 00823FF0 (area11[16], census L23) is the same kind of world
 * model owner and shares these services, the bone-slot stack and the draw
 * list: em_truck_original_tick over its record (docs/TRUCK_ORIGINAL.md
 * "Binding"). Its record keeps +0x04, +0xB0, +0xC0 and the +0x1F0 rest
 * matrix, +0x2DC..+0x2EC; the fall counter +0x28, +0xD0 and the velocity
 * scratch 0x700038A0 live in the slot. Workers: 001B0FD0 / 001C6380 as
 * above, the bone-0 pose 00102958, the hull 001A2370 and its AABB header,
 * 001B1B70, +0x4C 001CAA00 (em_owner_draw_live, as above),
 * 001B1E20 (em_pad_actuator), 001FBD50 (em_sfx_play_at at the truck's
 * +0xB0) and 001AFC10. Its effect spawns 001EFD20 (0x80000049) run the
 * effect binder (em_effects_live, census L26), and are counted
 * (em_area11_boxes_effect_spawns). The crates' 001EFD90 and the drums'
 * 001F0460 / 001EFD20 (their damage paths) run it too.
 * The camera trigger 008251E0 (area11[17]) runs em_truck_trigger_tick over
 * its record (+0x04, +0x0B) with the camera script 0x8292C0 on the AREA11
 * script host (em_area11_script_host). Both read and write D_00810792
 * (canonical since HK), the live player record (+0x05, +0x0A, +0x214 and
 * its owner's +0x0D), the player position g.pos (D_00810350) and the pose's
 * hip (+0xB0); 0x700031F0 (the carry flag) lives here: no live port code
 * reads it (its reader 0018B9C0 is unbound, census L13). */
#ifndef EM_AREA11_BOXES_H
#define EM_AREA11_BOXES_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_actor_pool.h"
#include "game/em_owner_draw_original.h"
#include "game/em_owner_services_original.h"
#include "game/em_scene_state.h"

#define EM_AREA11_BOX_TABLES_PATH "assets/scene_snow/box_tables.emrg"
#define EM_AREA11_WORLD_MODELS_PATH "assets/scene_snow/world_models.emwm"

/* Select *D_0028A59C at an area rebuild, after all old owner views have
 * been released and em_area11_boxes_reset has cleared this adapter.
 * resource_word is the canonical loader's D_0028A490[0x43], not an address
 * inferred from the filename. The EMWM header must name that exact table.
 * A current-generation owned model prevents rebinding (even to the same
 * file); all borrowed world_model/world_models views expire on success.
 * Failure preserves the previous bank. The global library D_0028A56C and
 * Roger resources are unaffected. Return 0, or -1 with a diagnostic. */
int em_area11_boxes_bind_world_bank(const char *path, uint32_t resource_word);

/* Optional original-address registry borrowed at each reached crate group
 * access: D_0024A850[area], D_0024D820[area], then the selected overlay group.
 * Bind after boxes_reset, before the first crate tick. reset/detach clears
 * the callback; no resource pointer is retained across calls. The provider
 * must be callable during native owner execution and remain stable for one
 * tick. All requests use write=0. NULL detaches. The default AREA11 path
 * remains unbound; its placements never reach a nest group. */
typedef uint8_t *(*EmArea11BoxesRegistryView)(void *,uint32_t,uint32_t,int);
void em_area11_boxes_bind_registry(EmArea11BoxesRegistryView,void *context);
uint32_t em_area11_boxes_registry_fault(void);

/* The 001AF710 bone-slot stack and its 0xD0-byte slot arena as
 * em_roger_actor_original views (D_00275BCC, D_00275BD0, D_007D4640[],
 * D_007D5840..): the one storage the boxes and Roger's owner pop from and
 * push to (census L22). Built on first use at the area build. */
struct EmRogerActorWorld;
const struct EmRogerActorWorld *em_area11_boxes_slot_world(void);

/* One owner call of the node `actor` (callback 001551B0 or 00156620) in the
 * pool walk. 1, or -1 (a fault; a line on stderr names it). The owner may
 * free its own record (001AFC10) through `pool`. */
int em_area11_boxes_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene);

/* The truck 00823FF0 and its trigger 008251E0 (census L23): one owner call
 * each in the pool walk. 1 allocated, 0 the node freed itself, -1 a fault
 * (a line on stderr names it). */
int em_area11_boxes_truck_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene);
int em_area11_boxes_trigger_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene);
/* The truck record's owner fields for the tick log (the route rows'
 * truck_r16: +0x28 and the scratch 0x700038A0 are not in them): 1 and the
 * values when a truck node is live, 0 otherwise. */
int em_area11_boxes_truck_state(uint32_t *record, uint8_t header[16], float position[3],
                                uint8_t t2dc[20]);
/* The truck's 001EFD20 effect spawns since the area build; a whole set
 * piece spawns 32 (TRUCK_ORIGINAL.md). */
unsigned em_area11_boxes_effect_spawns(void);

/* 001AF710 (the bone-slot stack at every area build) and every node's
 * state dropped: called with the pool reset (001AFCA0). */
void em_area11_boxes_reset(void);
/* The scratchpad word 0x700031F0 (its one storage): the truck's carry sets
 * it, 0015BCF0 clears it, 0018B9C0 reads its low byte. */
int32_t *em_area11_boxes_carry31F0(void);

/* 001AF800(actor): the pool's bone-slot return for a record with +0x09 != 0
 * (EmActorPool.w_001AF800): em_roger_actor_001AF800, which pushes every
 * slot back itself (the original 001AF800 does not call 001AF890). The
 * records of Roger, the equipment nodes and the indicator children go to
 * their binders' views of the same translation. 0, or -1 for a record that
 * is not a box. */
int em_area11_boxes_001AF800(void *ctx, EmActor *actor);

/* The fence door 001BC350's 001B0EA0 (called by 001B0F60 in its 001BBDA0;
 * census L18, em_area11_door.c): the boxes' allocation over the exported
 * bank and the shared bone-slot stack. Writes the record's +0x04 (3 when the
 * bone cap refused), +0x09 and +0x0C; *ret = the original result (1 refused,
 * 0 allocated). 0, or -1 (reported). */
int em_area11_boxes_door_001B0EA0(EmActor *actor, int32_t *ret);

/* The fence door's +0x4C (001CAA00; em_area11_door.c): `nodes` = its
 * runtime's node +0x90 matrices (count of them, 16 floats each) go to its
 * bone slots, then em_owner_draw_live builds its unit (`record` = the
 * door's original record address, the log's key). 0, or -1 (reported). */
int em_area11_boxes_door_draw(EmActor *actor, uint32_t record, const float *nodes, uint32_t count);

/* The boxes whose +0x4C ran in their last owner call (their units are
 * em_owner_draw_live's); also writes the EM_BOX_DUMP record dump when that
 * variable is set. Called once per frame by the render chain build. */
int em_area11_boxes_draw_count(void);

/* The exported world model bank *D_0028A59C for the other owners that bind
 * on it (the terminal's indicator child, 001C22A0; em_indicator_bind_live):
 * 001C6120(bank_word, id) (em_world_models_001C6120) and the model at a
 * handle. Each loads the bank on first use. 0 / -1, or NULL. */
int em_area11_boxes_world_001C6120(uint32_t bank_word, uint32_t id, uint32_t *handle);
const EmOwnerModel *em_area11_boxes_world_model(uint32_t address);
/* *D_0028A59C (the table address), or 0 when the bank is not loaded. */
uint32_t em_area11_boxes_world_bank_word(void);
/* The bank itself (the draw's view: 001CABA0's 001D3990 / 001D3D90 find a
 * model's blocks by its entry in the bank), or NULL. */
const EmWorldModels *em_area11_boxes_world_models(void);

/* The other AREA11 world-model owners on their original records
 * (docs/OWNER_DRAW.md section 10): the terminal 00827B10, the panel
 * 00159210, the prop 001C4820 and the item owners 0015AFA0 / 00219550.
 * Their behaviours stay where they are (the interaction host, the node
 * bindings); these are the original callees they reach, over the same bank,
 * bone-slot stack and draw list as the boxes:
 *   001B0FD0(actor)              em_owner_services_001B0FD0 over *D_0028A59C
 *   001B1020(actor, a1, a2, a3)  em_owner_services_001B1020 over D_0028A56C
 *                                (the Roger export's global library; the
 *                                model joins a table-less library bank at
 *                                its original address)
 *   001C6380(actor)              em_owner_services_001C6380 over the record's
 *                                +0xB0 / +0xC0 / +0x60 and its slots; *world
 *                                = the new +0xD0 (NULL: not wanted)
 *   +0x4C                        001CAA00 through em_owner_draw_live over the
 *                                record's +0x02, +0x03, +0x80, +0x90, +0x94,
 *                                +0x98, +0xB0 and the slots
 * A bind writes the record's +0x04 (+= 1, or 3 over the bone cap), +0x09 and
 * +0x0C; *ret = the original result (0 bound, 1 refused). The slots go back
 * through em_area11_boxes_001AF800 when the pool frees the record. Each
 * returns 0, or -1 (reported on stderr). */
int em_area11_boxes_owner_001B0EA0(EmActor *actor, EmActorPool *pool, int32_t *ret);
int em_area11_boxes_owner_001B0FD0(EmActor *actor, EmActorPool *pool, int32_t *ret);
/* Synchronize the existing typed slot views with their original-layout
 * arena records around a byte-addressed pose worker. from_bytes: import
 * its writes; otherwise publish the typed views. No allocation. */
int em_area11_boxes_owner_sync_slots(EmActor *actor, int from_bytes);
int em_area11_boxes_owner_001B1020(EmActor *actor, EmActorPool *pool, uint32_t a1, int32_t a2, int32_t a3,
                                   int32_t *ret);
int em_area11_boxes_owner_001C6380(EmActor *actor, float world[16]);
/* Already bound canonical service view, without allocating. AREA01's raw
 * record bridge uses this to retain fields a model bind does not write.
 * The pointer expires when this generation is freed or the area resets. */
EmOwnerServicesOwner *em_area11_boxes_owner_view(EmActor *actor);
/* Existing canonical +0xD0 matrix, without running pose or allocating a view.
 * NULL unless this generation is already a bound world owner. */
float *em_area11_boxes_owner_world(EmActor *actor);
int em_area11_boxes_owner_draw(EmActor *actor);
/* A bound world owner's +0x44 (the model's original address) and +0x4C,
 * for the tick log: 1, or 0 when `actor` is not a bound world owner. */
int em_area11_boxes_owner_state(const EmActor *actor, uint32_t *model, uint32_t *method);
/* Nonallocating record projection for byte-addressed owner callers: +0x40,
 * +0x44 and +0x4C of this generation's already bound world/library owner.
 * The returned words are observations of the canonical view, not writable
 * record storage. Returns 1 bound, 0 absent or invalid output pointers. */
int em_area11_boxes_owner_fields(const EmActor *actor, uint32_t *anim, uint32_t *model,
                                  uint32_t *method);
/* Node k's +0x90 world matrix (slot k of a bound world owner). 0, or -1. */
int em_area11_boxes_owner_node(const EmActor *actor, unsigned k, float out[16]);

/* Slot k of a bound world owner (D_00275B40[k] while it ticks, its +0x110
 * word k): the slot record, and in *address its original address. The
 * security gun 00825940 writes its bone 3's +0x78 through it and hands its
 * +0x90 to 001A2370 (em_area11_bindings.c). NULL when `actor` is not a
 * bound world owner or k is not below its +0x09. */
EmOwnerBone *em_area11_boxes_owner_slot(const EmActor *actor, unsigned k, uint32_t *address);

/* Scene unload: the frame's draw state and the kept units. */
void em_area11_boxes_shutdown(void);

#endif
