/* Pickup rendering, inventory and persistence.
 *
 * The AREA11 item owners (00219550 x6, 0015AFA0) run through
 * em_pickup_original.h, bound and ticked by the AREA11 interaction host
 * (WP-6): 00184BA0 arms them from the published list, their exported take
 * programs run on the shared owner, and 001B6EA0 posts the original status
 * request. See docs/PICKUP_OWNERS.md. The former legacy use scan, two-frame
 * take and flat inventory add are deleted; instances of scenes without a
 * bound owner are drawn and never taken.
 */
#ifndef EM_PICKUP_H
#define EM_PICKUP_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Slots per scene. The richest exported scene (snow) places 11 items;
 * office sub-1 = 3 items + 7 props. */
#define EM_PICKUP_MAX  24

/* Add one placed pickup. `model_file` (scene-dir relative) may be NULL
 * — the instance is then collectible but draws nothing (and logs).
 * `prop` = display prop: rendered, never collectible. Returns the slot
 * index, -2 when the uid's taken bit is set (the engine's cond-1
 * spawn suppression — nothing placed), or -1 on error. */
int em_pickup_add(EmGfx *gfx, const char *scene_dir, int type,
                  const float pos[3], float yaw, int uid,
                  const char *model_file, int prop);

/* Apply the state-0 init pose of placed prop `slot`'s original overlay
 * owner (the manifest's `owner <fn> <flags2>` suffix; flags2 = placement
 * record +0x03 -> actor +0x2E). Supported: 0x00827630 (the AREA11 fan
 * pair: rot.z = +pi/4 when flags2 == 0, else -pi/4). Returns 0, or -1 for
 * a bad slot or an owner whose init is not translated. */
int em_pickup_owner_init_pose(int slot, uint32_t owner, unsigned flags2);

/* Free the scene's instances + meshes. Inventory and the taken-bit
 * set SURVIVE (engine: D_00810C64/D_00810860 are global game state —
 * that survival IS the pickup persistence across scene reloads). */
void em_pickup_scene_clear(EmGfx *gfx);

/* New-game reset (func_001AF2C0): wipe inventory + taken bits (the
 * engine memsets the 0x640-byte game-state block at D_00810700), then
 * apply 001AF2C0's inventory seeds: counts 0/5/7/0x17 = 1, count 0x10 = 2
 * and magazine packs 2 (001C40B0(0x10, 2)), primary 0xFF. */
void em_pickup_reset(void);

/* Render-chain accessors (door/enemy draw contract): slot count + one
 * draw per LIVE slot — returns 0 for despawned/model-less slots. */
int em_pickup_count(void);
int em_pickup_draw(int i, EmGfxMesh **mesh, const float **palette,
                   uint32_t *bone_count);

/* 00219550's model73 child (001C5570, a 001C5680 node of its own in
 * AREA11: em_area11_bindings.c tick_indicator). The manifest names the
 * owner's UID and the mesh; the child's colour is its +0xA0, which 00219550
 * passes at the spawn. Returns -2 for a taken owner. */
int em_pickup_light_add(EmGfx *gfx, const char *scene_dir, int owner_uid,
                        const char *model_file);
/* The child's +0x4C draw (001CACB0), reached from its 001F54E0: `source_id`
 * is the owner's EMIS record, c80 the child's +0x80 after 001F54E0. Queues
 * this frame's draw; -1 when no light belongs to that owner. */
int em_pickup_light_submit(uint32_t source_id, const float c80[4]);
/* Draws the lights submitted since the last call, then clears them. */
void em_pickup_lights_draw(EmGfx *gfx, const float viewproj[16]);

/* The item block D_00810C60.. is canonical D2 progress (em_scene_state.h).
 * em_pickup_items is D_00810C64 (0x50 entries readable directly; use
 * em_pickup_item_count for any type: 0x50/0x51 are em_weapon's reserve,
 * types past 0xBB are outside the block and read 0). */
const uint8_t *em_pickup_items(void);
uint8_t        em_pickup_item_count(int type);
uint8_t        em_pickup_mag_packs(void);      /* D_00810C63 */

/* 001C40B0 case 0x10 writes the loaded magazine D_00810C62 and the reserve
 * D_00810CB4, which em_weapon holds (w.mag, w.reserve). The game binds them
 * once; an unbound case-0x10 take faults. */
void em_pickup_set_weapon_ammo(uint8_t *c62, int16_t *cb4);

/* Original battery charge D_00810CB2 and capacity D_00810CB7 (001C40B0
 * cases 0x1B..0x1D), in HALF-units. UI display units are these values >> 1.
 * The setter is for the original battery menu's timed discharge/recharge;
 * it clamps invalid host requests to [0, capacity]. */
int  em_pickup_battery_charge(void);
int  em_pickup_battery_capacity(void);
void em_pickup_battery_set_charge(int half_units);
/* Original149F0 pickup notice writes these two inventory fields together;
 * it does not consume an item or change the item-count array. */
int em_pickup_battery_set_capacity_charge(uint16_t charge, uint8_t capacity);

/* Persistence introspection (self-test): the taken bit for `uid`. */
int em_pickup_taken(int uid);

#ifdef __cplusplus
}
#endif

#endif /* EM_PICKUP_H */
