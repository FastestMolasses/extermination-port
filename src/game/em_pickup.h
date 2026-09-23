/* Pickup rendering, inventory and legacy scene support.
 *
 * Initial AREA11 interactions use em_pickup_original.h: canonical previous-
 * frame arbitration, original owner scripts, separate item/map/key counts,
 * and real status requests. Its tests execute the original ELF instructions.
 * See docs/PICKUP_OWNERS.md for the proved scope and remaining boundaries.
 *
 * The scan/countdown/Found functions below remain legacy APIs for scenes
 * that have not been bound to the original adapter. Their old comments
 * described partial decompilation as proof; they are not a fidelity claim.
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

/* Legacy scan constants. Original descriptors275488/275878 contain
 * {10,3.5}; the canonical scanner reads the verified exported descriptor
 * and implements the separate facing/LOS families. */
#define EM_PICKUP_RADIUS      10.0f   /* desc[0]: XZ ring              */
#define EM_PICKUP_DY_UP        3.5f   /* desc[1]: player above item    */
#define EM_PICKUP_DY_DOWN     20.5f   /* desc[1] + 17.0: item above    */
#define EM_PICKUP_AUTO_RING    7.0f   /* facing auto-pass distance     */
/* The archetype-3/4 facing tolerance. CORRECTED 2026-07-31 from pi/4:
 * func_00183EF0's case-3/case-4 block returns out of
 * `if (fabs(ang) <= 1.5707964f)`; the 0.7853982f tail belongs to the
 * archetypes that fall through (0/1/2), which items never use. */
#define EM_PICKUP_FACING  1.5707964f  /* pi/2                          */
/* Legacy unbound-scene stand-in. Original AREA11 uses exported programs. */
#define EM_PICKUP_TAKE_FRAMES  2

/* The SPR4 magazine-pack item type (func_001C40B0 case 0x10). */
#define EM_PICKUP_TYPE_MAG  0x10

/* Original grab IDs. Class7 thresholds are+6/+13; class4 uses+6/+12. */
#define EM_PICKUP_GRAB_LOW   0x42
#define EM_PICKUP_GRAB_MID   0x41
#define EM_PICKUP_GRAB_HIGH  0x40

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

/* Per-frame: the use scan (CROSS edge -> arm) + armed-take pump.
 * `scan` = 0 suppresses the scan (the engine gates on the scripted
 * frame selector spad 3B8D — the port passes 0 while a door transit
 * or the damage lock owns the player). */
void em_pickup_update(const float player_pos[3], float player_yaw,
                      const EmFrameInput *in, int scan);

/* Render-chain accessors (door/enemy draw contract): slot count + one
 * draw per LIVE slot — returns 0 for despawned/model-less slots. */
int em_pickup_count(void);
int em_pickup_draw(int i, EmGfxMesh **mesh, const float **palette,
                   uint32_t *bone_count);

/* 00219550's separate model73 child, allocated through 001C5570. The
 * manifest binds it explicitly to a pickup UID. Transform and lifetime
 * follow that owner; color.xyz is the original base RGB and color.w the
 * random brightness amplitude (001F54E0). Returns -2 for a taken owner. */
int em_pickup_light_add(EmGfx *gfx, const char *scene_dir, int owner_uid,
                        const char *model_file, const float color[4]);
void em_pickup_lights_draw(EmGfx *gfx, const float viewproj[16]);

/* Inventory — the D_00810C64 mirror (one u8 count per item type),
 * plus the magazine-pack counter (D_00810C63 mirror). */
const uint8_t *em_pickup_items(void);          /* [256] */
uint8_t        em_pickup_item_count(int type);
uint8_t        em_pickup_mag_packs(void);

/* Original battery charge/capacity (001C40B0 cases 0x1B..0x1D), in
 * HALF-units. UI display units are these values >> 1. The setter is
 * for the original battery menu's timed discharge/recharge; it clamps
 * invalid host requests to [0, capacity]. Both survive scene clears and
 * are wiped only by em_pickup_reset, like the inventory counts. */
int  em_pickup_battery_charge(void);
int  em_pickup_battery_capacity(void);
void em_pickup_battery_set_charge(int half_units);
/* Original149F0 pickup notice writes these two inventory fields together;
 * it does not consume an item or change the item-count array. */
int em_pickup_battery_set_capacity_charge(uint16_t charge, uint8_t capacity);

/* One-shot event takes (consumed by em_game's pickup hunk):
 *  - ammo: reserve rounds to add (func_001C40B0 case 0x10's 30/pack);
 *    em_game applies them to em_weapon when the weapon state allows
 *  - found: the just-collected item TYPE for the em_hud Found line,
 *    -1 = none pending */
int em_pickup_ammo_take(void);
int em_pickup_found_take(void);

/* Persistence introspection (self-test): the taken bit for `uid`. */
int em_pickup_taken(int uid);

/* USE-SCAN ARBITRATION (func_00184BA0's single-winner walk — see the
 * "ONE WINNER PER PRESS" note above). Valid only for the remainder of
 * the frame in which em_pickup_update ran; cleared at its next entry.
 *   em_pickup_scan_dist   -> 1 and writes the winner's PLANAR distance
 *                            (the engine's spad 0x70003B98 value) when
 *                            THIS frame's scan armed an item, else 0.
 *   em_pickup_scan_release-> give that arm back, because a nearer
 *                            object elsewhere in the engine's one list
 *                            won the press instead. */
int  em_pickup_scan_dist(float *out_dist);
void em_pickup_scan_release(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_PICKUP_H */
