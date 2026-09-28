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
 *                         (the actor drop shadow)
 *   001B17A0              the interaction host's services (the class lists
 *                         and the interactive list the Use scan reads)
 *   +0x4C 001CAA00        em_owner_draw_live over his record (model 0x47 of
 *                         the export, his node records, +0x90 = the face
 *                         slot, +0x94 = 7): the body unit and 001CB3C0's
 *                         face unit (docs/FACE_ATTACH.md section 6)
 *   001FABB0 / 001FAE70 / 001AEE10 / 001B0C60  the scene bindings
 * and the equipment node's 001C5C90 (em_roger_actor_001C5C90) with 001B1020
 * (em_owner_services_001B1020 over the global model table D_0028A56C) and
 * its +0x4C draw (em_owner_draw_live over its record: library model 0x6B,
 * its one node, which 001C5C90 copies from Roger's bone 1).
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
#include "game/em_owner_draw_live.h"
#include "game/em_scene_state.h"

#define EM_AREA11_ROGER_RESOURCES_PATH "assets/scene_snow/roger/resources.emrs"
#define EM_AREA11_ROGER_TRIGGER_PATH "assets/scene_snow/roger/trigger.empg"

#define EM_AREA11_ROGER_CALLBACK 0x008237E0u
#define EM_AREA11_ROGER_EQUIPMENT_CALLBACK 0x001C5C90u

/* The area build: drop both owners (the resources and meshes stay). */
void em_area11_roger_reset(void);
/* One owner call of Roger's node (callback 008237E0). 1 allocated, 0 the
 * node freed itself, -1 a fault (a line on stderr names it). */
int em_area11_roger_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene);
/* One owner call of the equipment node (callback 001C5C90). */
int em_area11_roger_equipment_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene);
/* 001AF800 for either record: em_roger_actor_001AF800 over its typed view
 * (the +0x110 slots pushed back in 001AF800's own loop).
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

/* 001C6120(D_0028A56C, id) over the exported table (the equipment models'
 * lookup; the player equipment binder's model binds use it too): 0, or -1
 * for an id outside the exported table. */
int em_area11_roger_001C6120(uint32_t bank, uint32_t id, uint32_t *handle);
/* Roger's record bytes (its original layout, the owner fields synced in)
 * when [address, address + size) lies in it, else NULL: the head sprite's
 * owner view (001E2560 reads Roger's +0x01, +0x02, +0x04, +0x0C, +0x110
 * words, +0xC0 and +0x220; census L39). */
const uint8_t *em_area11_roger_record_bytes(uint32_t address, uint32_t size);
/* Bytes of the bone-slot arena [address, address + size) (the node records
 * the +0x110 words name), or NULL. */
const uint8_t *em_area11_roger_slot_bytes(uint32_t address, uint32_t size);

/* The regions 001CB3C0 reads by address for a record with +0x90 != 0
 * (em_owner_draw_live_001CAA00_attached): the record's bytes
 * [record, record + record_size), the one 001AF710 slot arena and the
 * export's regions (Roger's face resource 0x88, Dennis's 0x18). The count
 * written, or -1 (not loaded, or more than `cap`). */
int em_area11_roger_attachment_regions(uint32_t record, const uint8_t *record_bytes, uint32_t record_size,
                                       EmOwnerDrawLiveRegion *out, unsigned cap);
/* The scene's teardown: forget the draw's model bank. */
void em_area11_roger_shutdown(EmGfx *gfx);

/* The tick log's view of Roger's record (the route rows' roger_r8: +0x00..
 * +0x0F, +0xB0, the +0x1F0 block): 1 while the record is Roger's. */
int em_area11_roger_state(uint32_t *record, uint8_t header[16], float position[3], uint8_t block[16]);
/* The same view of the equipment node's record (the route rows' attach_r9:
 * +0x00..+0x0F, +0xB0): 1 while it is live. */
int em_area11_roger_equipment_state(uint32_t *record, uint8_t header[16], float position[3]);

#endif
