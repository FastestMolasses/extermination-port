/* em_owner_draw_live.h - the default draw method 001CAA00 of the AREA11
 * world owners, live: the original unit built by the translations, drawn by
 * the object-unit renderer. Docs: docs/OWNER_DRAW.md sections 6..8.
 *
 * An owner's +0x4C call (its behaviour's draw worker) runs
 * em_owner_draw_live_001CAA00. That is em_owner_services_001CAA00 with every
 * worker bound to its translation over canonical storage:
 *   001CA7B0           em_owner_draw_001CA7B0 (the view D_00810610 and the
 *                      cull planes at context +0x2410)
 *   001D8C20(0)        context +0x246C = 0
 *   001D89D0           em_actor_light_w_001D89D0: the room rig D_00251C50,
 *                      the rig record D_00817BC0 (this module's storage) and
 *                      D_00275688, the point lights (em_point_light's pool,
 *                      the context +0x220 slots), context +0x0C / +0x2380,
 *                      D_00810700, D_00253170, D_00810610; the owner's
 *                      +0x80 colour words
 *   001C7420's packets the render context's channel-0 cursor (context +0x10)
 *                      in its packet arena, 0x70003AC0 from its scratchpad
 *   001D1F80(0, 1, 0)  the render context's veil module (em_rcl_001D1F80)
 *   001CA940           em_owner_draw_001CA940 over the AREA11 model bank
 *   001CB3C0           not bound: an owner with +0x90 != 0 faults
 * The unit it appends to the arena is parsed at once (em_object_unit_parse:
 * REF targets from the render context's storage and the bank) and kept for
 * the frame. em_owner_draw_live_flush draws the frame's units in the order
 * they were built (the owner walk's order) through em_gfx_object_unit.
 *
 * Fail-stop: any fault (a worker, a view, the parse, the draw) is reported
 * with the original address and returned as -1; the caller turns it into its
 * fault. There is no stand-in draw. */
#ifndef EM_OWNER_DRAW_LIVE_H
#define EM_OWNER_DRAW_LIVE_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_object_unit.h"
#include "game/em_owner_draw_original.h"
#include "game/em_owner_services_original.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_OWNER_DRAW_LIVE_UNITS 32u              /* units kept per frame */
#define EM_OWNER_DRAW_LIVE_TEXTURES "assets/scene_snow/object_textures.emot"

/* 001CAA00(owner). `bank` is the AREA11 world model bank the owner's +0x44
 * points into; `rgb` the owner's +0x80..+0x8F words; `record` the owner's
 * original record address (the log's key). 0, or -1 (reported). */
int em_owner_draw_live_001CAA00(const EmWorldModels *bank, EmOwnerServicesOwner *owner,
                                const uint32_t rgb[4], uint32_t record);

/* The last drawn frame's 001CAA00 calls, for the level smoke's capture
 * check (tools/test_level_smoke.py check_owner_units): per call the owner's
 * record address, the unit's byte count (0: 001CA7B0 culled it), the clip
 * pass, and FNV-1a digests of the colour matrix B (the colour CNT's 16
 * words), of the nodes' lighting rows (C x A, qwords 4..7 of each node), of
 * their position rows (node x VP, qwords 0..3), of the point-light slots
 * (context +0x220, the 32 x 0x80 bytes 001D89D0's fold read) and of the
 * lighting rows' lanes y and z alone (A's columns 1 and 2: the room rig's
 * slots 1 and 2, which the point-light fold does not touch). */
typedef struct {
    uint32_t record, bytes, clip;
    uint32_t colour, light, position, points, light_rig;
} EmOwnerDrawLiveLog;
int em_owner_draw_live_log(EmOwnerDrawLiveLog *out, int capacity);

/* Draw this frame's units (em_frame_counter) in build order, then forget
 * every kept unit. The first call of a session registers the object
 * textures (EM_OWNER_DRAW_LIVE_TEXTURES, tools/export_object_textures.py).
 * 0, or -1 (reported: a missing export, an em_gfx_object_unit refusal). */
int em_owner_draw_live_flush(EmGfx *gfx);

/* Units kept for the current frame (the level smoke's count). */
uint32_t em_owner_draw_live_count(void);

/* Test hook: the kept unit i's parsed pieces, or NULL. */
const EmObjectUnitPieces *em_owner_draw_live_unit(uint32_t i);

/* Forget every kept unit and the registered-texture flag (a scene reset). */
void em_owner_draw_live_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_OWNER_DRAW_LIVE_H */
