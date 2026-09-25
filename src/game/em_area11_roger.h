/* em_area11_roger.h - Roger (AREA11 overlay 008237E0, record area11[8])
 * and the equipment node that rides on him (001C5C90, area11[9]) on their
 * original owners (census L22; docs/ROGER_ACTOR_ORIGINAL.md and
 * docs/ROGER_ORIGINAL.md "Binding").
 *
 * The controller:
 *   +0x04 == 0   em_roger_actor_008237E0_init (001BA1C0, 001B10B0, 001C63E0,
 *                001CA6F0, 001BA8E0 and its face slot, +0x30 / +0x58)
 *   +0x04 1..3   em_roger_tick (00823910 / 00823950 / 00823B70 / 00823C40)
 * with its hooks bound to the original owners:
 *   001BA1A0 / 001BA1F0   the AREA11 script host (em_area11_script_host)
 *   001B1EA0              em_director_original_001B1EA0 over the quad 0x82AB80
 *   001C67E0 / 001C64F0 / 001C68C0 / 001C63E0
 *                         em_pose_host_workers over Roger's record, its node
 *                         records in the 001AF710 slot arena and the clip
 *                         banks of assets/scene_snow/roger/resources.emrs
 *                         (tools/export_roger_banks.py)
 *   001BA580 / 001BA540   em_roger_actor_original with its face slot, the
 *                         face kernel 001D0720 (em_opening_face_tick over the
 *                         slot bytes, the shared 00122BB8 RNG) and 001DA6A0
 *                         (the actor drop shadow: the port draws no actor
 *                         shadow, reported as the player's post-step is)
 *   001B17A0              the interaction host's services (the class lists
 *                         and the interactive list the Use scan reads)
 *   +0x4C 001CAA00        the port's actor draw of Roger's mesh at his node
 *                         world matrices, with the face morph of the slot
 *   001FABB0 / 001FAE70 / 001AEE10 / 001B0C60  the scene bindings
 * and the equipment node's 001C5C90 (em_roger_actor_001C5C90) with 001B1020
 * (em_owner_services_001B1020 over the global model table D_0028A56C) and
 * its +0x4C draw (the equipment mesh at its bone-0 matrix, which 001C5C90
 * copies from Roger's bone 1).
 *
 * Storage. The EmActor fields are the canonical record bytes they name;
 * every other byte of the two records (+0x20..+0x2D, +0x40, +0x44, +0x4C,
 * +0xA0.., +0xD0.., the +0x110 slot words) lives here, loaded and stored
 * around every owner call. The node and face records are the 0xD0-byte
 * slots of the shared 001AF710 arena (em_area11_boxes). */
#ifndef EM_AREA11_ROGER_H
#define EM_AREA11_ROGER_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_actor_pool.h"
#include "game/em_scene_state.h"

#define EM_AREA11_ROGER_RESOURCES_PATH "assets/scene_snow/roger/resources.emrs"
#define EM_AREA11_ROGER_MESH_PATH "assets/scene_snow/roger/roger.emdl"
#define EM_AREA11_ROGER_TRIGGER_PATH "assets/scene_snow/roger/trigger.empg"
#define EM_AREA11_ROGER_FACE_MESH_PATH "assets/scene_snow/opening/roger_face.emdl"
#define EM_AREA11_ROGER_FACE_MORPH_PATH "assets/scene_snow/opening/roger_face.emfm"
#define EM_AREA11_EQUIPMENT_MESH_PATH "assets/scene_snow/opening/equipment_6b.emdl"

#define EM_AREA11_ROGER_CALLBACK 0x008237E0u
#define EM_AREA11_ROGER_EQUIPMENT_CALLBACK 0x001C5C90u

/* The area build: drop both owners (the resources and meshes stay). */
void em_area11_roger_reset(void);
/* One owner call of Roger's node (callback 008237E0). 1 allocated, 0 the
 * node freed itself, -1 a fault (a line on stderr names it). */
int em_area11_roger_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene);
/* One owner call of the equipment node (callback 001C5C90). */
int em_area11_roger_equipment_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene);
/* 001AF800 for either record: its +0x110 slots pushed back (001AF890).
 * 1 handled, 0 not one of these records, -1 a fault. */
int em_area11_roger_001AF800(EmActor *actor);

/* The script host's views of Roger's record (valid during Roger's owner
 * call): the +0x40 bank word, and 001C67E0(Roger, clip, blend, frame). */
uint32_t *em_area11_roger_bank_word(const EmActor *actor);
int em_area11_roger_clip_init(const EmActor *actor, int16_t clip, float blend, float frame);
/* D_0028A490[index] (the resource table words), for the script host's
 * r_0028A490 reader: 0, or -1 outside the exported table. */
int em_area11_roger_table_word(uint32_t address, uint32_t *value);
/* The exported bank region bytes at an EE address (the camera track header
 * the script host's r_track_head reads, 001C6120 over bank 0x96), or NULL. */
const uint8_t *em_area11_roger_resource(uint32_t address, uint32_t size);
/* The resource file's regions for another pose host (the player's
 * encounter clip lives in bank 0x96): calls `map` for each read-only
 * region. 0, or -1 when the resources are not loaded. */
int em_area11_roger_regions(int (*map)(void *ctx, uint32_t address, uint32_t size, const uint8_t *bytes),
                            void *ctx);

/* The draw list: the owners whose +0x4C ran in their last owner call. */
int em_area11_roger_draw_count(void);
/* anchor_bone: the owner's +0x98 light-reference node; cam_fill: its +0x02
 * bit 0x20 (001D8BF0); face: the mesh carries the face morph's
 * FACE_LIGHT vertices (the 001D88B0 face rig applies). */
int em_area11_roger_draw(int i, EmGfxMesh **mesh, const float **palette, uint32_t *bone_count,
                         uint8_t *anchor_bone, uint8_t *cam_fill, uint8_t *face);
void em_area11_roger_shutdown(EmGfx *gfx);

/* The tick log's view of Roger's record (the route rows' roger_r8: +0x00..
 * +0x0F, +0xB0, the +0x1F0 block): 1 while the record is Roger's. */
int em_area11_roger_state(uint32_t *record, uint8_t header[16], float position[3], uint8_t block[16]);
/* The same view of the equipment node's record (the route rows' attach_r9:
 * +0x00..+0x0F, +0xB0): 1 while it is live. */
int em_area11_roger_equipment_state(uint32_t *record, uint8_t header[16], float position[3]);

#endif
