/* em_pickup.h — collectible items + display props: the engine's
 * item-pickup system, FULLY DECODED 2026-06-11 (decomp repo
 * Extermination/docs/FINDINGS.md "ITEM PICKUP SYSTEM FULLY DECODED").
 *
 * THE DECODE OVERTURNS the s11/s15/s17 framing: the main placement
 * tables' kind-0xB records (class 0x0004, behavior func_001C4820 — the
 * office supply-room ammo-box/crate stacks) are DISPLAY PROPS in the
 * engine. They never carry the interactive class flag 0x80, are never
 * pushed onto the use scan's interactive list, and func_001C4820 has
 * no take path. The engine's real COLLECTIBLE ITEMS come from the
 * per-area DEFERRED-SPAWN REGISTRY:
 *
 *   D_0024D820[area] -> [sub-state] -> group lists of 0x2C records
 *   (func_001B6910 / func_001B6660), spawn-condition gated — cond 1 =
 *   "spawn only if NOT taken": taken(uid) = bit `uid` of the per-area
 *   TAKEN ARRAY D_00810860 + 32*area, SET by func_001B1190 when the
 *   collected actor frees and TESTED by func_001B11E0 at every area
 *   (re)load. That bit array IS the engine's pickup persistence.
 *
 * Engine flow, function by function (all decoded from the .s):
 *
 *   func_0015AC00   item INIT: scale by type (2.0 for the 0x40..0x6D
 *                   equipment ids, 1.5 for 0x5B, else 1.0), model bind
 *                   by EXPLICIT id +0x0D (model&0xF==1: the per-area
 *                   table *(D_0028A59C); else the GLOBAL chunk27
 *                   library *(D_0028A56C) — func_001B1020), status = 1,
 *                   +0x08 = 3 (the use-scan ITEM archetype), +0x30 =
 *                   D_00275488 = {10.0, 3.5} (radius descriptor), aura
 *                   class via func_001F1110 (NOT ported — flagged,
 *                   the s60 "pickup-instance auras" open item).
 *   func_00184BA0   player USE SCAN — runs ONLY on the CROSS press
 *                   edge (D_00810E74 & spad 3B76, s58), walks last
 *                   frame's interactive list, per candidate:
 *   func_00183EF0   archetype-3 ITEM branch (jtbl_0026D810[3]):
 *                     - XZ distance <= desc[0] = 10.0
 *                     - dy = player.y - item.y in [-(3.5+17), +3.5]
 *                       (desc[1] = 3.5; the +17 widens the window for
 *                       items ABOVE the player — shelf items)
 *                     - FACING: |wrap(bearing-to-item - player_yaw)|
 *                       <= pi/4, AUTO-PASS when distance <= 7.0
 *                       (model-0 family; model 1 instead requires
 *                       |wrap(pi + item_yaw - player_yaw)| <= pi/4,
 *                       model 2 is the wall-mount pitch variant)
 *                   nearest passing candidate by dist^2 wins ->
 *                   actor +0x0B = 4, spad 3B8D = 3.
 *   func_0015AE20   the ARMED handler (behavior func_0015AFA0 state 1):
 *                   queues one of two take SCRIPTS —
 *                     - player in action 0x2D or D_008104E6 set: the
 *                       INSTANT script D_00248480 {op7 subD enter
 *                       scripted, op9 CALL func_001B6EA0, op7 sub4
 *                       exit | STOP}
 *                     - else the GRAB-ANIM script D_002482C0: the
 *                       pick-up anim id is patched by ITEM HEIGHT vs
 *                       player.y (D_00810354): y < py+6 -> 0x42 (low),
 *                       y < py+13 -> 0x41 (mid), else 0x40 (high);
 *                       then op-A wait-anim-done, the op-9 take, exit.
 *                   Script completion -> lifecycle 2 -> func_001B1190
 *                   (SET the taken bit from +0x9A) + func_001AFC10
 *                   free: the item DESPAWNS.
 *   func_001B6EA0   the TAKE NATIVE (op 9): by take family +0x03 —
 *                     0 -> func_001C47A0(type, 1): the INVENTORY ADD
 *                          switch func_001C40B0 then the FOUND request
 *                          D_008106B0 = 1 / D_008106B1 = type
 *                     1 -> func_001C4720: MAP array D_00810CB8[type]++,
 *                          request kind 2
 *                     * -> func_001C4760: KEY-ITEM array
 *                          D_00810CC3[type]++, request kind 3 (only
 *                          posted for types >= 0x20)
 *   func_001C40B0   the inventory switch (s18's "0x001C4100"): default
 *                   = count[D_00810C64 + type] += n (cap 99); case
 *                   0x10 SPR4 MAGAZINE additionally: pack counter
 *                   D_00810C63 += n, reserve D_00810CB4 += 30*n, an
 *                   empty mag D_00810C62 auto-fills to 30, and packs
 *                   >= 99 fold back into the reserve (cap 98).
 *   func_001AE7E0   the FOUND presentation: a nonzero D_008106B0 makes
 *                   the main-mode controller OPEN THE STATUS SCREEN,
 *                   which routes to the item's page/database record
 *                   (func_0020CDC0 consumes B0/B1 — the "Found:" lines
 *                   are message-bank group 4, names group 3).
 *
 * PORT MAPPING (deviations FLAGGED):
 *  - Manifest lines (written by the decomp repo's export_level.py
 *    --pickups; em_game's parser owns the line):
 *        pickup <type> <x> <y> <z> <yaw> <uid> [<model.emdl>] [prop]
 *    <uid> = (area << 8) | engine-puid — one flat taken-bit set keeps
 *    the engine's per-area D_00810860 semantics; uid 0 never persists
 *    (the engine's own rule). `prop` marks the placement-table kind-0xB
 *    DISPLAY PROPS: rendered, never collectible (engine-true).
 *  - Collection condition = the decoded archetype-3 test (CROSS edge,
 *    10-u ring, the [-20.5, +3.5] dy window, pi/4 facing with the 7-u
 *    auto pass; nearest wins). The model-1/2 facing variants are NOT
 *    modeled (no placed family-1/2 item is closer than its ring to
 *    another pickup; the bearing test stands in — FLAGGED).
 *  - The take runs the INSTANT-script shape (2 scripted frames then
 *    the take): the GRAB-ANIM variant needs player clips 0x40..0x42,
 *    which the current player.emdl export does not carry — FLAGGED
 *    (re-export with those ids to model it).
 *  - Inventory = a u8-per-type count array mirroring D_00810C64
 *    (+ the 0x10 magazine case: pack counter + 30 reserve rounds per
 *    pack, handed to em_weapon through em_game's pickup hunk). The
 *    map/key arrays (take families 1/2) fold into the same count
 *    array — FLAGGED simplification (the engine keeps three arrays).
 *  - The FOUND presentation is an em_hud "Found: <name>" line (real
 *    font + message-bank group-3 name) instead of the engine's
 *    auto-opened status screen — FLAGGED stand-in (page interiors are
 *    CONTENT TBD; see em_hud.h).
 *  - No pickup aura (func_001F1110/func_001F1180 — open), no take/
 *    Found sound (none is decoded on the take path itself; the
 *    engine's audible feedback is the status screen opening).
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

/* Decoded use-scan constants (func_00183EF0 archetype 3, desc
 * D_00275488) — shared with the self-test. */
#define EM_PICKUP_RADIUS      10.0f   /* desc[0]: XZ ring              */
#define EM_PICKUP_DY_UP        3.5f   /* desc[1]: player above item    */
#define EM_PICKUP_DY_DOWN     20.5f   /* desc[1] + 17.0: item above    */
#define EM_PICKUP_AUTO_RING    7.0f   /* facing auto-pass distance     */
#define EM_PICKUP_TAKE_FRAMES  2      /* instant-script latency (op7/op9) */

/* The SPR4 magazine-pack item type (func_001C40B0 case 0x10). */
#define EM_PICKUP_TYPE_MAG  0x10

/* Add one placed pickup. `model_file` (scene-dir relative) may be NULL
 * — the instance is then collectible but draws nothing (and logs).
 * `prop` = display prop: rendered, never collectible. Returns the slot
 * index, -2 when the uid's taken bit is set (the engine's cond-1
 * spawn suppression — nothing placed), or -1 on error. */
int em_pickup_add(EmGfx *gfx, const char *scene_dir, int type,
                  const float pos[3], float yaw, int uid,
                  const char *model_file, int prop);

/* Free the scene's instances + meshes. Inventory and the taken-bit
 * set SURVIVE (engine: D_00810C64/D_00810860 are global game state —
 * that survival IS the pickup persistence across scene reloads). */
void em_pickup_scene_clear(EmGfx *gfx);

/* New-game wipe: inventory + taken bits (boot only; the engine memsets
 * the 0x640-byte game-state block at D_00810700 — func_001AF2C0). */
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

/* Inventory — the D_00810C64 mirror (one u8 count per item type),
 * plus the magazine-pack counter (D_00810C63 mirror). */
const uint8_t *em_pickup_items(void);          /* [256] */
uint8_t        em_pickup_item_count(int type);
uint8_t        em_pickup_mag_packs(void);

/* One-shot event takes (consumed by em_game's pickup hunk):
 *  - ammo: reserve rounds to add (func_001C40B0 case 0x10's 30/pack);
 *    em_game applies them to em_weapon when the weapon state allows
 *  - found: the just-collected item TYPE for the em_hud Found line,
 *    -1 = none pending */
int em_pickup_ammo_take(void);
int em_pickup_found_take(void);

/* Persistence introspection (self-test): the taken bit for `uid`. */
int em_pickup_taken(int uid);

#ifdef __cplusplus
}
#endif

#endif /* EM_PICKUP_H */
