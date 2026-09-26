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
 *                  001AF800 / 001AF890 when the pool frees the record
 *   001B1B70 / 001B1D20  the collision world's class lists (the crates'
 *                  cells, uids 7..10, and the drums', uids 5 and 6)
 *   001B17A0      the interaction host's services (the drum's visibility)
 *   0019AB20      em_actor_collision_owner_probe over the collision world
 *   001A2370      em_collision_world_retransform_001A2370
 *   00122BB8      em_random_next; 001FBD50 em_sfx_play_at
 *   +0x4C         001CAA00 (the only method 001CA6E0 installs): the node is
 *                 drawn this frame through the port's actor draw chain, the
 *                 legacy crate / drum EMDL meshes at the owner's bone world
 *                 matrices (the P1/P2 object kernel stays with RENDER)
 *
 * The workers reached only after a damage write (+0x36; no live code
 * writes it: CRATES_DRUMS_ORIGINAL.md "Status" item 7) and the nest-group
 * paths (+0x0E bit 0, +0x56 >= 0, which no AREA11 box has) are fail-stop
 * workers that name their original: 001FC580, 001EFD90 / 001EFD20 /
 * 001F0460, 001B11E0 / 001B1190, 001AFA90's child copy, the 001C6120 husk
 * rebind over D_0028A56C, 0019A570 and 0019AD00. The D_002468B0 /
 * D_00246A00 / D_00246A10 tables come from assets/scene_snow/box_tables.emrg
 * (tools/export_box_tables.py).
 *
 * The truck 00823FF0 (area11[16], census L23) is the same kind of world
 * model owner and shares these services, the bone-slot stack and the draw
 * list: em_truck_original_tick over its record (docs/TRUCK_ORIGINAL.md
 * "Binding"). Its record keeps +0x04, +0xB0, +0xC0 and the +0x1F0 rest
 * matrix, +0x2DC..+0x2EC; the fall counter +0x28, +0xD0 and the velocity
 * scratch 0x700038A0 live in the slot. Workers: 001B0FD0 / 001C6380 as
 * above, the bone-0 pose 00102958, the hull 001A2370 and its AABB header,
 * 001B1B70, +0x4C 001CAA00 (props/area_truck.emdl at the bone matrix),
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
#include "game/em_scene_state.h"

#define EM_AREA11_BOX_TABLES_PATH "assets/scene_snow/box_tables.emrg"
#define EM_AREA11_WORLD_MODELS_PATH "assets/scene_snow/world_models.emwm"

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
#define EM_AREA11_TRUCK_MESH_PATH "assets/scene_snow/props/area_truck.emdl"
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
 * (EmActorPool.w_001AF800). Every slot goes back through 001AF890. 0, or -1
 * for a record that is not a box. */
int em_area11_boxes_001AF800(void *ctx, EmActor *actor);

/* The fence door 001BC350's 001B0EA0 (called by 001B0F60 in its 001BBDA0;
 * census L18, em_area11_door.c): the boxes' allocation over the exported
 * bank and the shared bone-slot stack. Writes the record's +0x04 (3 when the
 * bone cap refused), +0x09 and +0x0C; *ret = the original result (1 refused,
 * 0 allocated). 0, or -1 (reported). */
int em_area11_boxes_door_001B0EA0(EmActor *actor, int32_t *ret);

/* The actor draw chain: the boxes whose +0x4C ran in their last owner call.
 * em_area11_boxes_draw returns 1 and the mesh, palette and bone count of
 * draw i, or 0. */
int em_area11_boxes_draw_count(void);
int em_area11_boxes_draw(int i, EmGfxMesh **mesh, const float **palette, uint32_t *bone_count);

/* Free the meshes and the model bank (scene unload). */
void em_area11_boxes_shutdown(EmGfx *gfx);

#endif
