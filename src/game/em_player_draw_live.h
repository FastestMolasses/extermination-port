/* em_player_draw_live.h - the player's model and its draw method, live.
 * Docs: docs/OWNER_DRAW.md section 10 ("The player").
 *
 * The player record D_008102B0 binds its model in 0015C1F0: 001CA6E0(player,
 * D_0028A490[+0x2FF]) stores the model address at +0x44 and 001CA5F0(player,
 * 0) the draw method 001CAA00 at +0x4C; +0x0C = 001C6150(+0x44). The
 * post-step 0015C160 calls +0x4C after its shadow: 001CAA00(player), the
 * same draw the world owners use (em_owner_draw_live), over the player's
 * own node records (the +0x110 words: D_00275B40 after 001CB590(player)).
 *
 * This module holds the model the handle names (assets/scene_snow/
 * player_model.emom, tools/export_player_model.py: resource 0x3B at
 * 0x00D1C1C0, checked byte for byte against RAM in every AREA11 capture)
 * and builds 001CAA00's owner view from the player record image
 * (player_states_actor):
 *   +0x02 (cls), +0x03, +0x0C (the node count), +0x0D, +0x44 (the model
 *   handle), +0x80..+0x8F (the colour words 001D89D0 reads), +0x90 (the
 *   attachment), +0x94 (the collapsed bone), +0x98 (the cull node), +0xB0;
 *   the node records the +0x110 words name, read through the record pose's
 *   storage (player_pose_record_bytes): their +0x90..+0xCF world matrices,
 *   which 001C7420, 001CAA00 and 001D89D0 read.
 *
 * Fail-stop: a missing export, a +0x4C other than 001CAA00, a +0x44 that is
 * not the exported model, a node count other than the model's or an
 * unmapped node record is reported with its original address and returns
 * -1. There is no stand-in draw. */
#ifndef EM_PLAYER_DRAW_LIVE_H
#define EM_PLAYER_DRAW_LIVE_H

#include <stdint.h>

#include "game/em_owner_draw_original.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_PLAYER_DRAW_LIVE_MODEL "assets/scene_snow/player_model.emom"
#define EM_PLAYER_DRAW_LIVE_RECORD UINT32_C(0x008102B0)   /* D_008102B0 */

/* The exported model (loaded at the first use). NULL, reported, when the
 * export is missing or malformed. */
const EmWorldModels *em_player_draw_live_bank(void);

/* 001C6150(handle) for the player's model (em_owner_services_001C6150 over
 * the exported model's view: the byte at model +0x08). 0, or -1 (reported)
 * when `handle` is not the exported model. */
int em_player_draw_live_001C6150(uint32_t handle, uint8_t *count);

/* 0015C160's +0x4C: 001CAA00(player) through em_owner_draw_live. 0, or -1
 * (reported). */
int em_player_draw_live_001CAA00(void);

/* The player's node `node` world matrix (+0x90..+0xCF of the node record
 * the record's +0x110 word `node` names, 16 floats in the original's row
 * layout = column-major here) while the record is the displayed pose
 * (em_scene_bindings_player_record_drawn): 1, or 0 (not the displayed
 * pose, no record, the node not mapped). em_weapon reads node 4, the rifle
 * node its gun node 00188630 reads (docs/PLAYER_EQUIPMENT.md section 2). */
int em_player_draw_live_node_world(unsigned node, float out16[16]);

/* Forget the loaded model (a session end; the next use reloads it). */
void em_player_draw_live_unload(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_PLAYER_DRAW_LIVE_H */
