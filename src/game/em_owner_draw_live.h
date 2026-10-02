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
 *   001CB3C0           em_face_attach_001CB3C0 (the +0x90 attachment: the
 *                      face unit, docs/FACE_ATTACH.md) over the same
 *                      channel, scratch, light and owner-draw views, its
 *                      EE-address reads served by the caller's regions
 *                      (em_owner_draw_live_001CAA00_attached), D_00250FB0
 *                      from the render context's .data, 001D2910 the live
 *                      render context's; an owner with +0x90 != 0 drawn
 *                      without regions faults
 * The units it appends to the arena (the owner's, then 001CB3C0's face
 * unit; the face alone when 001CA7B0 culled the body) are parsed at once
 * (em_object_unit_parse_one:
 * REF targets from the render context's storage and the bank: a world model
 * bank, or a table-less one such as the player's model or the equipment
 * models) and kept for the frame. em_owner_draw_live_flush_walk draws the
 * walk's units in the order they were built (the owner walk's order),
 * em_owner_draw_live_flush the post-step's (the player's) after the
 * shadow's passes, through em_gfx_object_unit.
 *
 * Fail-stop: any fault (a worker, a view, the parse, the draw) is reported
 * with the original address and returned as -1; the caller turns it into its
 * fault. There is no stand-in draw. */
#ifndef EM_OWNER_DRAW_LIVE_H
#define EM_OWNER_DRAW_LIVE_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_actor_light_001D89D0.h"
#include "game/em_object_unit.h"
#include "game/em_owner_draw_original.h"
#include "game/em_owner_services_original.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_OWNER_DRAW_LIVE_UNITS 64u              /* units kept per frame */
#define EM_OWNER_DRAW_LIVE_TEXTURES "assets/scene_snow/object_textures.emot"

/* 001CAA00(owner). `bank` is the model bank the owner's +0x44 points into
 * (the AREA11 world model bank, the player's model or the equipment
 * models); `rgb` the owner's +0x80..+0x8F words; `record` the owner's
 * original record address (the log's key). 0, or -1 (reported). */
int em_owner_draw_live_001CAA00(const EmWorldModels *bank, EmOwnerServicesOwner *owner,
                                const uint32_t rgb[4], uint32_t record);

/* 001CACB0's 001CABA0(owner, model) (the indicator draw: the muzzle node
 * 001F5040's +0x4C): em_owner_services_001CABA0 over the same views, on
 * channel 3 (context +0x1C): 001CA7B0 at owner +0xB0 with the model's
 * +0x20, 001D8C20(1), 001C7420 on channel 3, 001D3990 / 001D3D90
 * (em_owner_draw_001D3900 / _001D3CF0 with the veil module's 001D1F80(3, 2,
 * 2)), the RET tag, 001D8C20(0) and 001CAAC0 (em_anim_rest_001CAAC0 with
 * the render context's 001CB760: the page D_007635C0 CALLs the unit at its
 * depth). The unit is parsed at once (a class-2 object unit) and kept for
 * this frame by its address: the chain page draws it where the page CALLs
 * it (em_owner_draw_live_page_unit). 0, or -1 (reported). */
int em_owner_draw_live_001CABA0(const EmWorldModels *bank, EmOwnerServicesOwner *owner,
                                const uint32_t rgb[4], uint32_t record);
/* 001F3E30's mode-0 draw (the shell casing's, the barrel's particle sweep):
 * 001CA7B0(position, f12), then 001C7900(m, token, 0x3F5, 0) (the colour
 * and node CNTs on channel 0, its 001D88B0 lighting over the token's 16
 * bytes, em_face_attach's worker as 001CB3C0's), then 001CA940(flags,
 * model) with a library model (D_0028A56C's, from the Roger export). The
 * unit has no GS state REF of its own: it inherits the channel's, which is
 * class 0 (every channel-0 producer in the port sends set 1 class 0: the
 * units' REFs and the static world's, which em_gfx_gs_opaque checks). It
 * is parsed after 001CA940 and kept with the frame's units. 0, or -1. */
int em_owner_draw_live_001CA7B0(const uint32_t position[4], uint32_t radius, int32_t *flags);
int em_owner_draw_live_001C7900(const uint32_t m[16], uint32_t token, const uint8_t token_bytes[16],
                                int32_t vuaddr, int32_t chan);
int em_owner_draw_live_001CA940_library(int32_t flags, uint32_t model);

/* This frame's class-2 unit 001CABA0 built at `address`, or NULL. */
const EmObjectUnitPieces *em_owner_draw_live_page_unit(uint32_t address);

/* An EE range 001CB3C0 reads by address (read-only). */
typedef struct {
    uint32_t address, size;
    const uint8_t *bytes;
} EmOwnerDrawLiveRegion;
#define EM_OWNER_DRAW_LIVE_REGIONS 12u    /* EM_POSE_REGION_MAX: the anim rest's region table */

/* 001CAA00(owner) for an owner whose +0x90 may be set (Roger, the player
 * while a script holds its face): `regions` must map the record's
 * +0x80..+0x97 (the colour words, +0x90, +0x94), the attachment slot
 * (+0x40..+0x63 of the 0xD0-byte slot +0x90 names) and the face resource
 * (+0x04 and its blocks from +0x40, which the face unit's REF names).
 * The regions are used only during the call (the face blocks' bytes stay
 * referenced by the kept unit until the flush: they must outlive the
 * frame). 0, or -1 (reported). */
int em_owner_draw_live_001CAA00_attached(const EmWorldModels *bank, EmOwnerServicesOwner *owner,
                                         const uint32_t rgb[4], uint32_t record,
                                         const EmOwnerDrawLiveRegion *regions, unsigned region_count);

/* The light of a draw method other than 001CAA00 (001CB480, the status MAP
 * page's models, docs/STATUS_PAGES.md section 7): 001D8C20(mode) (the
 * context's +0x246C, the one copy), then 001D89D0(owner, A, B, rgb) with
 * the same bindings as 001CAA00's (the room rig, the point lights, this
 * module's rig record D_00817BC0). `rgb` is the owner's +0x80..+0x8F words;
 * a / b receive the light matrix A and the colour matrix B (raw bits). 0, or
 * -1 (reported). */
int em_owner_draw_live_light(int32_t mode, const EmOwnerServicesOwner *owner, const uint32_t rgb[4],
                             float a[16], float b[16]);

/* The last drawn frame's 001CAA00 calls, for the level smoke's capture
 * check (tools/test_level_smoke.py check_owner_units): per call the owner's
 * record address, the unit's byte count (0: 001CA7B0 culled it), the clip
 * pass, and FNV-1a digests of the colour matrix B (the colour CNT's 16
 * words), of the nodes' lighting rows (C x A, qwords 4..7 of each node), of
 * their position rows (node x VP, qwords 0..3), of the point-light slots
 * (context +0x220, the 32 x 0x80 bytes 001D89D0's fold read) and of the
 * lighting rows' lanes y and z alone (A's columns 1 and 2: the room rig's
 * slots 1 and 2, which the point-light fold does not touch); then the
 * point 001CAA00 culled and lit the owner at (owner +0xB0, or its node
 * +0x98's +0xC0: three float bit patterns) and a digest of the pose it
 * drew (every node's +0x90..+0xCF world matrix), for the owners that move
 * (the player and its equipment). */
typedef struct {
    uint32_t record, bytes, clip;
    uint32_t colour, light, position, points, light_rig;
    uint32_t point[3], pose;
    /* 001CB3C0's face unit (0 bytes: none): its bytes and a digest of its
     * colour matrix B, its one node's rows and its weights (in that order).
     * `bytes` above excludes it. */
    uint32_t face_bytes, face;
} EmOwnerDrawLiveLog;
int em_owner_draw_live_log(EmOwnerDrawLiveLog *out, int capacity);

/* The inputs and bytes of the last drawn frame's attached 001CAA00 calls
 * (those given regions: Roger, the player while a script holds its face),
 * for the level smoke's original re-execution (tools/level_smoke_face.py):
 * the record's bytes (the caller's record region), the node records'
 * +0x90..+0xCF world matrices, the attachment slot (0xD0 bytes; address 0:
 * none), the views the call read (D_00810610, context +0x2410..+0x244F,
 * the scratchpad view-projection 0x70003AC0, context +0x0C / +0x9C, the rig
 * record D_00817BC0 and D_00275688 before the call, D_00810700 / 701), and
 * every byte the call appended (the owner unit and the face unit). */
#define EM_OWNER_DRAW_LIVE_SAMPLE_RECORD 0x320u
#define EM_OWNER_DRAW_LIVE_SAMPLE_UNIT 0x1000u
#define EM_OWNER_DRAW_LIVE_SAMPLES 2u
typedef struct {
    uint32_t frame, record, record_size;
    uint8_t record_bytes[EM_OWNER_DRAW_LIVE_SAMPLE_RECORD];
    uint32_t node_count;
    uint32_t nodes[EM_OWNER_SERVICES_MAX_BONES][16];
    uint32_t slot_address;
    uint8_t slot[0xD0];
    uint32_t view_810610[16], planes_2410[16], vp_3AC0[16], ctx_0C, ctx_9C;
    uint32_t rig[EM_ACTOR_LIGHT_RIG_WORDS], rig_word;
    uint8_t area[2];
    uint32_t unit_address, unit_bytes, face_bytes;
    uint8_t unit[EM_OWNER_DRAW_LIVE_SAMPLE_UNIT];
} EmOwnerDrawLiveSample;
/* The last drawn frame's attached calls (at most EM_OWNER_DRAW_LIVE_SAMPLES):
 * the count, *out the first. */
int em_owner_draw_live_samples(const EmOwnerDrawLiveSample **out);

/* The post-step 0015C160 starts: the units built from here on in this frame
 * (the player's +0x4C) are drawn after the shadow's passes, as the original
 * orders them (docs/OWNER_DRAW.md section 10). */
void em_owner_draw_live_post_step(void);

/* Draw this frame's units (em_frame_counter) that the owner walk built,
 * i.e. those before the frame's em_owner_draw_live_post_step (all of them
 * without one), in build order. 0, or -1 (as em_owner_draw_live_flush). */
int em_owner_draw_live_flush_walk(EmGfx *gfx);

/* Draw this frame's remaining units in build order, then forget every kept
 * unit and keep the frame's calls as the last drawn frame's log. The first
 * draw of a session registers the object textures
 * (EM_OWNER_DRAW_LIVE_TEXTURES, tools/export_object_textures.py).
 * 0, or -1 (reported: a missing export, an em_gfx_object_unit refusal). */
int em_owner_draw_live_flush(EmGfx *gfx);

/* Register the object textures (EM_OWNER_DRAW_LIVE_TEXTURES) with `gfx`
 * once per session, as the first draw does: the static world's triangles
 * (em_static_world_live, drawn before the owner units) sample the same
 * registry. 0, or -1 (reported). */
int em_owner_draw_live_textures(EmGfx *gfx);

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
