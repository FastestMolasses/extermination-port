/* em_shadow_live.h - the player's drop shadow, live (census L29 + L29b):
 * 0015C160's shadow half bound over the canonical storage, drawn by the
 * Metal shadow passes. Docs: docs/SHADOW_ORIGINAL.md "Binding",
 * docs/SHADOW_ACTOR_ROUTE.md section 4, docs/SHADOW_DECAL.md section 5.
 *
 * 0015C160 (byte-matched src/func_0015C160.c), called by the gameplay
 * variant 001AE5E0 at 0x1AE654 and the cutscene variant 001AE6B0 at
 * 0x1AE798, runs with D_008102B1 != 0: 001CB590(player), then unless
 * D_00810771 == 1 either 001DA6A0(player) (+0x214 == 0) or 0015BF90(player)
 * (+0x214 != 0), then the player's +0x4C draw method. The coordinator's
 * w_0015C160 (em_scene_bindings.c) owns the gate, 001CB590 and the +0x4C
 * request; this module owns the shadow call:
 *
 *   001DA6A0  em_shadow_original_001DA6A0 over the player record, its node
 *             records (em_player_record_pose's, through the pose host's
 *             regions), the render context's views (+0x2240, +0x2340,
 *             +0x2380, +0x2468, the scratchpad 0x70003AC0), the live
 *             camera's D_00810610, the area bytes and the static-object grid
 *             of assets/scene_snow/shadow_receivers.emsr, with D_00817FF0
 *             kept here (BSS: zero at boot, never reset). Its draw workers
 *             record their arguments; em_shadow_live_flush replays them on
 *             the GPU (em_gfx_shadow_*) where the original's display list
 *             has them: after the level and the walked actors, before the
 *             player's own draw. The silhouette uses the proxy mesh
 *             assets/player_shadow.emdl (D_0028A490[0x28]) skinned by the
 *             player's node matrices (node +0x90) as they stand at the call.
 *   0015BF90  em_shadow_actor_route_0015BF90 with its workers: the node
 *             records' +0xC4, 0019A570 over the collision world
 *             (em_collision_world_segment), 0011E620 over the SDK context,
 *             001CD390 (em_effect_original over this module's scratchpad
 *             block 0x70003600..0x7000363F), and 001CE300
 *             (em_shadow_decal_original over the render context's packet
 *             chain, page D_007635C0 slot 0). The fans 001CE300 writes are
 *             checked here (the form 001CE300 writes) and drawn with the
 *             rest of the page by em_chain_page_live at the page's splice
 *             (docs/CHAIN_PAGE.md), which reports the decal triangles it
 *             drew back through em_shadow_live_page_drew.
 *
 * Fail-stop: a missing asset or view, a worker or translation fault, a
 * packet the renderer does not implement, or a GPU pass that cannot draw
 * exactly latches the original address (em_shadow_live_fault()); every
 * later call returns -1. No stand-in shadow is ever drawn. */
#ifndef EM_SHADOW_LIVE_H
#define EM_SHADOW_LIVE_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_player_floor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SHADOW_LIVE_RECEIVERS_PATH "assets/scene_snow/shadow_receivers.emsr"
#define EM_SHADOW_LIVE_AREA01_RECEIVERS_PATH "assets/area01_shadow_receivers.emsr"
#define EM_SHADOW_LIVE_PROXY_PATH "assets/player_shadow.emdl"      /* D_0028A490[0x28] */
#define EM_SHADOW_LIVE_PROXY_29_PATH "assets/roger_shadow.emdl"    /* D_0028A490[0x29] */
#define EM_SHADOW_LIVE_KIND_ROGER 0x29       /* Roger's +0x96 (001BA8E0 for model 0x47) */
#define EM_SHADOW_LIVE_PLAYER 0x008102B0u

/* Load the assets (once per session) and check the views; reset the
 * per-frame record.
 * Called at the AREA11 area load. 0, or -1 (the reason printed, the
 * address latched). */
int em_shadow_live_bind(void);
/* Completed area delivery selects the existing receiver owner explicitly.
 * AREA11/0 and AREA01/0 are supported. Refuses a swap with unflushed passes;
 * invalid area/file leaves the prior receivers intact. Shared proxy models
 * and the persistent original D_00817FF0 are unchanged. */
int em_shadow_live_select_area(unsigned area, unsigned subarea);
int em_shadow_live_bound(void);
/* The latched fault: the original address, or 0. */
uint32_t em_shadow_live_fault(void);

/* 0015C160's shadow call after 001CB590(player): route = the
 * em_shadow_original_route_0015C160 value for the gate bytes (1: 001DA6A0,
 * 2: 0015BF90). `player` is the live record at D_008102B0. 0, or -1. */
int em_shadow_live_0015C160(const EmPlayerLiveActor *player, int route);

/* 001BA580's 001DA6A0(actor) in the owner walk (Roger, kind 0x29): the
 * same translation over the actor's record bytes (`size` of them, at its
 * original address `record`) and its 21 node records (0xD0 bytes each, the
 * +0x110 words'), the same scene views and D_00817FF0. Its passes are drawn
 * by em_shadow_live_flush_walk. 0, or -1 (latched). */
/* 001CE300(tag, corners, tex0, rgba) for an effect node's decal (the
 * death's 001F77B0, docs/DAMAGE.md section 4): the kernel over this
 * module's stage buffers and scratchpad block and the render context's
 * packet chain. 0, or -1 (latched). em_shadow_live_effect_decals counts
 * the calls since the process start. */
int em_shadow_live_effect_001CE300(int32_t tag, const uint32_t corners[16], uint64_t tex0, uint32_t rgba);
uint32_t em_shadow_live_effect_decals(void);
int em_shadow_live_actor_001DA6A0(uint32_t record, const uint8_t *bytes, uint32_t size,
                                  const uint8_t *const nodes[], uint32_t node_count);

/* The frame's 001DA6A0 passes (alpha clear, the two boxes, the
 * silhouette, the receivers), recorded this frame, on `gfx` with the
 * frame's native P*V `viewproj` (the level's): em_shadow_live_flush_walk
 * the owner walk's calls (after the walk's owner units), em_shadow_live_flush
 * the post-step's (before the player's +0x4C). Nothing recorded this frame:
 * 0. 0, or -1 (latched). */
int em_shadow_live_flush_walk(EmGfx *gfx, const float viewproj[16]);
int em_shadow_live_flush(EmGfx *gfx, const float viewproj[16]);
/* 001F9100(owner, point, normal, f12) from another owner (AREA01's class-2
 * owners through 001B5360, em_area00_low): em_shadow_actor_route's
 * translation over this module's scratchpad block and stage buffers and
 * the render context's packet chain; the quad's fans (the decal TEX0) are
 * counted for this frame's page. 0, or -1 (latched). */
int em_shadow_live_owner_001F9100(const uint32_t owner[4], const uint32_t point[4], const uint32_t normal[4],
                                  uint32_t f12);
/* The chain page drew `decal_triangles` fan triangles with the decal's
 * TEX0 this frame (em_chain_page_live): they must be exactly this frame's
 * 0015BF90 fans' plus the other owners' 001F9100 fans (sum of n - 2), else
 * the fault latches. 0, or -1. */
int em_shadow_live_page_drew(uint32_t decal_triangles);

/* The level smoke's view of the last 0015C160 shadow call (the tick log's
 * "shadow"): its frame counter, route, the 001DA6A0 result (1 drawn, 0
 * the original early return), kind, receiver count and class-2 count, the
 * decal's fan count, and whether the flushes drew it. */
typedef struct {
    uint32_t frame;
    int32_t route, drawn, kind;
    uint32_t receivers, receivers_cls2;
    uint32_t decal_fans, decal_vertices;
    uint32_t flushed, decal_flushed;
    uint32_t calls;          /* cumulative 0015C160 shadow calls */
    uint32_t drawn_total;    /* cumulative 001DA6A0 draws flushed */
    uint32_t decal_total;    /* cumulative decals flushed */
} EmShadowLiveLog;
void em_shadow_live_log(EmShadowLiveLog *out);

/* The inputs and outputs of the last call, for the level smoke's original
 * re-execution (tools/level_smoke_shadow.py): the player record (0x320
 * bytes), its 21 node records (0xD0 each, at the +0x110 words), the
 * 001DA6A0 scene views, D_00817FF0 before the call, the plan; for the
 * 0015BF90 route the segment result and the packet words. */
typedef struct {
    uint32_t frame;
    int32_t route;
    uint32_t record, record_size;     /* the actor's address and the bytes of `player` it fills */
    uint8_t player[EM_PLAYER_ACTOR_SIZE];
    uint8_t nodes[21 * 0xD0];
    uint32_t clip_2240[16], proj_2340[16], view_2380[16], camera_3AC0[16], view_810610[16];
    uint32_t zoom_2468, fog_A0[4];
    uint8_t area_700, sub_701, spad3B8D;
    uint32_t ff0_before[4];
    const void *plan;        /* EmShadowOriginalPlan, route 1 */
    uint32_t plan_bytes;
    /* route 2 */
    int32_t segment_result;
    uint32_t segment_point[4], segment_normal[3];
    uint32_t submit_corners[16], submit_rgba;
    uint64_t submit_tex0;
    int32_t submit_tag, submitted;
    uint32_t packet_count;
    uint32_t packet_qwords[4];
    const uint8_t *packet[4];
} EmShadowLiveSample;
/* The last call's sample, or NULL before the first call. */
const EmShadowLiveSample *em_shadow_live_sample(void);

/* The last owner-walk call (em_shadow_live_actor_001DA6A0): its frame,
 * record, the 001DA6A0 result, kind, receivers (class 2), whether the walk
 * flush drew it, and the cumulative calls and draws; its sample (the record
 * bytes in `player`), or NULL before the first call. */
typedef struct {
    uint32_t frame, record;
    int32_t drawn, kind;
    uint32_t receivers, receivers_cls2, flushed;
    uint32_t calls, drawn_total;
} EmShadowLiveActorLog;
void em_shadow_live_actor_log(EmShadowLiveActorLog *out);
const EmShadowLiveSample *em_shadow_live_actor_sample(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_SHADOW_LIVE_H */
