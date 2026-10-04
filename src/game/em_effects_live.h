/* em_effects_live.h - the effect originals bound live in the first level
 * (census lanes L26 effect manager, L27 effect kinds, L39 head sprite).
 * Docs: docs/EFFECT_MANAGER.md section 8.
 *
 * This module adds no behaviour of its own. It owns the storage the effect
 * translations share and wires each one's workers to the other translations
 * over the one canonical render context (em_render_context_live):
 *
 *   em_effect_original       001EF9D0 / 001EFD90 / 001EFD20 (the spawns;
 *                            001EF940's sound through em_sfx's 001FBF50
 *                            gains and 001FB9F0 submit: record 0x27, the
 *                            flame's contact, carries one),
 *                            001EA240 (the node driver), 001CCF70 (the depth
 *                            key), 001F0460 (the ring decals)
 *   em_effect_kinds          the handlers 001EC1F0 / 001EC3F0 / 001EC470 /
 *                            001EBF10 with 001CFB50 / 001D0540, the glow
 *                            markers 001F5C20 / 001F5940, the resets
 *                            001F0310 / 001F03D0 / 001F3FA0
 *   em_effect_manager        the barrel 001F0360 (001F6210, 001F6BB0,
 *                            001F6EB0, 001F40C0, 001F0720 x6), 001F4D40 and
 *                            the pickup glint (001F1180's draw block, 001F0A60)
 *   em_head_sprite_original  001F0120 / 001E2560 (the head-bone sprite node)
 *                            and 001CFBE0 (every handler's packet chain)
 *   em_player_equipment_sprite  001CD520 (the glow markers' sprite)
 *   em_packet_chain_original the chain builders over the live context's arena
 *                            and chain table D_007635C0
 *
 * One storage per original datum: the effect nodes are actor-pool records
 * (001AFA90, the pool of em_actor_pool); their bytes beyond the pool's own
 * fields (+0xD0 matrix, +0x1F0 work block, the head sprite's +0x24.. fields)
 * live here, one slot per pool record. The ring D_0028F700 + 0x4DBEC0
 * (001F0460 writes it, 001F0720 ages and draws it, 001F03D0 resets it), the
 * particle records D_007709C0, D_00275C40 / D_00275C44, the transform block
 * D_0081F8F0 and the scratchpad words the routines share (0x70003600..,
 * 0x700036A0.., 0x70003660..) are this module's.
 *
 * Untranslated handlers: the two checked to be packet-only, the skid's
 * 001EAD70 (0x80000033) and 001EC270 (0x80000012), are a counted gap (their
 * packets are missing; the route reaches neither, the player can off the
 * route; counters.gaps). Every other handler em_effect_kinds does not
 * translate faults: some do more than draw (001EF510 spawns a child node),
 * and the port reaches none of them in AREA11 (EFFECT_MANAGER.md 8.2).
 * Not modelled on purpose (each is a fault when reached, and
 * none is reached on the route; route census, route_functions.json):
 * 001F6210's model-sprite list (AREA11's key
 * 0x0B00 has none), the selectors' point-light paths (keys 0 and 0x1301),
 * the particle sweep's live entities (001F3620 / 001F3E30; no writer ran).
 *
 * The packets are built exactly (byte for byte into the context's chain
 * table); drawing them is the renderer's: no port stage consumes the effect
 * chains yet (docs/EFFECT_MANAGER.md section 8.4).
 *
 * Fail-stop: the first fault of any bound translation is latched here with
 * the original address (em_effects_live_fault()); every later entry returns
 * -1. */
#ifndef EM_EFFECTS_LIVE_H
#define EM_EFFECTS_LIVE_H

#include <stdint.h>
#include <stddef.h>

#include "game/em_actor_pool.h"
#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_EFFECTS_LIVE_TABLES_PATH "assets/effect_tables.emet"
#define EM_EFFECTS_LIVE_DRIVER 0x001EA240u    /* 001EF9D0's callback for the puff types */
#define EM_EFFECTS_LIVE_HEAD_SPRITE 0x001E2560u

/* The binder's hook: bind a node 001EF9D0 has just allocated (its +0x10
 * callback set) to its pool behaviour. 0, or -1. */
typedef int (*EmEffectsLiveBind)(EmActor *actor);

/* Load the ELF windows (assets/effect_tables.emet) once. 0, or -1 (the
 * export is missing or malformed: nothing is bound). */
int em_effects_live_load(void);
/* Bind to the area's pool and scene state (at each area build, after the
 * render context's bind). 0, or -1. Clears the per-record storage. */
int em_effects_live_attach(EmActorPool *pool, EmSceneState *scene, EmEffectsLiveBind bind);
void em_effects_live_detach(void);
int em_effects_live_attached(void);
/* The latched fault: the original function or data address, or 0. */
uint32_t em_effects_live_fault(void);
/* Existing 001D0540 vector shared with 00183EF0; no copy or reset. */
uint8_t *em_effects_live_scratch_3660(uint32_t address,uint32_t size);
/* Bytes of the exported ELF windows [address, address + size) (zero outside
 * them), for the equipment binder's tables. NULL before the load. */
const uint8_t *em_effects_live_elf(uint32_t address, uint32_t size);
/* The same bytes only inside one exported block (NULL outside every block
 * or before the load): the chain page's reads of ELF data (the program
 * packets 0x231770 / D_00233290 / D_00233800, 001CFBE0's source blocks),
 * and the overlay source blocks 001D04B0 was handed since the attach. */
const uint8_t *em_effects_live_window(uint32_t address, uint32_t size);

/* 001D7BB0's tail at the area load (001D19E0): 001F68B0() and 001F6E40(),
 * the room point-light lists over this module's lists window (the ELF's
 * D_0025AD80..D_0025D800 from effect_tables.emet, the one copy, whose
 * records' +0x24 handles persist across area loads) and D_0026EB70's
 * presets, keyed by `scene`'s D_00810700 / D_00810701; 001F68B0's latch
 * bytes come from the scene's canonical progress bytes. Workers: 001D7FA0 =
 * em_point_light_register on the render context's pool, 001D80B0 =
 * em_rcl_001D80B0. Needs only the load and the render context (no attach).
 * 0, or -1 (reported, latched as em_effects_live_fault). */
int em_effects_live_room_lights(EmSceneState *scene);

/* 001D0660's 001F0310 (001AFCA0, the area build): 001F3FA0 and 001F03D0
 * for lanes 0, 1, 3, 4, 5, 6. */
int em_effects_live_001F0310(void);

/* The spawns. `rot` is the caller's whole +0xC0 quadword: 001EFD90 hands
 * its fourth word to 001EF9D0 as f12. 0, or -1 on a fault. */
int em_effects_live_001EFD90(uint32_t id, const float pos[4], const float rot[4]);
int em_effects_live_001EFD20(uint32_t id, const float pos[4]);
/* Same spawn owners, preserving their original node-address result (zero is
 * the original no-allocation result). Existing three-argument EFD90 remains
 * available to callers that discard it. */
int em_effects_live_001EF9D0(uint32_t id, const float pos[4], uint32_t f12, uint32_t *node);
int em_effects_live_001EFD90_result(uint32_t id, const float pos[4], const float rot[4], uint32_t *node);
int em_effects_live_001EFD20_result(uint32_t id, const float pos[4], uint32_t *node);
/* Typed entry points for callers holding their original local transform
 * block. The first 0x58 bytes are original fields; its final 8 bytes are
 * untouched. CFBE0 resolves m40 and the source through actual owner views. */
int em_effects_live_001CCF70(const float pos[4], int32_t *key);
int em_effects_live_001CFA60(uint8_t block[0x60], const float matrix[16], uint32_t f12, uint32_t f13);
int em_effects_live_001CFB50(uint8_t block[0x60], int32_t index, const float matrix[16], const uint32_t f[5]);
int em_effects_live_001CFBE0(int32_t key, int32_t kind, uint32_t source, const uint8_t block[0x60], int32_t copy);
/* The same with the source's bytes given by their owner (a writable .data
 * block: the gun lamp's flare D_002487E0, em_aim_fire_runtime). */
int em_effects_live_001CFBE0_bytes(int32_t key, int32_t kind, uint32_t source, const uint8_t source_bytes[0x90],
                                   const uint8_t block[0x60], int32_t copy);

/* Original-address fields of currently allocated effect slots. Returned
 * pointers alias their actual actor / slot owner, never a reconstructed
 * record. A range spanning separate native fields or unknown storage returns
 * NULL. Acquiring a writable +24 view does not make it readable: the caller
 * reports the completed original store with node_written. This permits an
 * original region-based worker to acquire its destination before the store.
 * Known pool fields, position / rotation, matrix and handler work fields are
 * available; unmapped holes are not synthesized. */
void *em_effects_live_node_field(uint32_t address, size_t size, int write);
/* Certify an observed completed +24 word store; never writes the word itself.
 * Returns -1 for another field, a partial store, or an inactive generation. */
int em_effects_live_node_written(uint32_t address, size_t size);
typedef struct {
    uint32_t address;
    size_t size;
    void *bytes;
} EmEffectsLiveNodeRegion;
/* Enumerate readable canonical fields of an original node base. Returns the
 * required count (zero for an inactive/non-effect node), fills up to capacity.
 * Callers must reject insufficient capacity. Uninitialized +24 is omitted. */
size_t em_effects_live_node_regions(uint32_t node, EmEffectsLiveNodeRegion *regions, size_t capacity);
/* 1 only for this owner's currently allocated native actor/generation;
 * writes its original pool address. 0 for a non-effect/stale/foreign actor. */
int em_effects_live_node_identity(const EmActor *actor, uint32_t *address);
/* D_00275C04, the word 001CCF70 (and 001CD070) store float_to_int of the
 * view w into: em_effect_original's one copy. NULL before the attach. */
int32_t *em_effects_live_d275C04(void);
/* Extend KIND_OTHER with an already translated callback. Install after area
 * attach (detach clears it). Result: 1 alive, 0 freed, -1 fault. */
typedef int (*EmEffectsLiveOtherTick)(void *, uint32_t address, uint32_t callback);
int em_effects_live_set_other_tick(EmEffectsLiveOtherTick, void *context);
/* The barrel's particle sweep 001F40C0: its 001F3620(entity, kind) and
 * 001F3E30(a0, a1, a2, a3, t0) through an already translated owner (the
 * shell casing's records, em_aim_fire_runtime). Install after the area
 * attach (detach clears it); without it a live particle faults. */
typedef int (*EmEffectsLiveParticleCall)(void *, uint32_t function, const uint32_t a[5]);
int em_effects_live_set_particle_call(EmEffectsLiveParticleCall, void *context);
/* The particle records D_007709C0 (0x80 x 0x90) and the words D_00275C40 /
 * D_00275C44 as original-address regions (3). */
size_t em_effects_live_particle_regions(EmEffectsLiveNodeRegion *out, size_t capacity);

/* 001F0460(n, M), M 16 floats. */
int em_effects_live_001F0460(int32_t n, const float m[16]);
/* 001F0120(owner, key): owner14 is the owner's +0x14 word. */
int em_effects_live_001F0120(uint32_t owner14, int32_t key);
/* 001D04B0(m, kind, source, f12, f13): an owner's particle draw (the AREA11
 * flame 008235F0 calls it with its +0xD0 matrix, kind 1, its descriptor
 * D_00828340, its phase and seed): 001CCF70(m + 0x30), 001CFA60(block, m,
 * f12, f13), 001CFBE0(key, kind, source, block, 0) over this module's
 * translations and the one packet chain. `source` is an overlay address
 * whose 0x90 bytes the caller hands over; they stay readable through
 * em_effects_live_window (the page REFs them) until the next attach.
 * f12 / f13 are bit patterns. 0, or -1 (latched). */
int em_effects_live_001D04B0(const float m[16], int32_t kind, uint32_t source, const uint8_t source_bytes[0x90],
                             uint32_t f12, uint32_t f13);

/* The pool behaviour of a node this module allocated: 001EA240 or
 * 001E2560 by its +0x10. 1 while allocated, 0 after its 001AFC10 free, -1
 * on a fault. */
int em_effects_live_tick(EmActor *actor);

/* 001F0360, the barrel (both world-frame variants). 0, or -1. */
int em_effects_live_001F0360(void);

/* The draw block of 001F1180 (0x1F136C..0x1F1470): owner_d0 is the owner's
 * +0xD0 matrix; record / angle / timer as em_pickup_aura_001F1180 hands
 * them (EmPickupAuraWorkers.w_draw). 0, or -1. */
int em_effects_live_aura_draw(const float owner_d0[16], uint32_t record, uint32_t angle,
                              uint32_t timer);

/* ---- the capture log (the level smoke) ---------------------------------- */
/* The last barrel's 001F4D40 calls (the glow markers), in call order: the
 * colour words it was handed, the rand() value it drew (-1: none), the rgb
 * it handed 001CD520 and, when 001CD520 emitted the primitive, that
 * primitive's colour words +0x10..+0x1C. tools/test_level_smoke.py
 * check_marker_colour. */
#define EM_EFFECTS_LIVE_MARKERS 16
typedef struct {
    uint32_t colour[4];
    int32_t value;
    uint32_t rgb;
    int emitted;
    uint32_t packet[4];
} EmEffectsLiveMarker;
/* The count; *frame is the main-loop counter of that barrel frame. */
int em_effects_live_markers(EmEffectsLiveMarker *out, int max, uint32_t *frame);
typedef struct {
    uint32_t address;     /* the pool record */
    uint32_t callback;    /* +0x10 */
    uint8_t state;        /* +0x04 */
    uint8_t sub;          /* +0x05 (head sprite) */
    uint8_t key;          /* +0x0D */
    uint32_t w[12];       /* driver: +0xB0 xyz, +0x100 xyz, +0x1F8, +0x1FC,
                           * +0x244 (bits); head sprite: +0x24, +0x28,
                           * +0xA0 xyz, +0x1F0, +0x244, +0x24C (bits) */
} EmEffectsLiveNode;
/* The live nodes in pool-list order (at most `max`); the count. */
int em_effects_live_nodes(EmEffectsLiveNode *out, int max);
/* The counters since the attach: 001CD520 emits, 001CFBE0 chains emitted
 * and skipped by its free-space guard, 001F0720 lanes drawn, barrel frames,
 * counted gaps (the two packet-only handlers 001EAD70 / 001EC270, each
 * call once), 001D04B0 calls. */
typedef struct {
    uint32_t sprites, chains, chains_skipped, lanes, frames, gaps;
    uint32_t overlay_draws;
} EmEffectsLiveCounters;
void em_effects_live_counters(EmEffectsLiveCounters *out);
/* The last 001D04B0 call (the AREA11 flame): its em_frame_counter(), the
 * calls since the attach, the source block, the depth key 001CCF70 gave,
 * and CRC-32 digests of its 001CFBE0 packet 1 (0x70 bytes, the phase word
 * +0x10 and the seed word +0x1C zeroed) and packet 4 (0x100 bytes: P, the
 * clip projection, K, the fog, the depth bias, the GIF tag row).
 * tools/level_smoke_chain_page.py compares them with the capture's page in
 * the camera-exact snapshots. */
typedef struct {
    uint32_t frame, calls, source;
    int32_t key;
    uint32_t p1_digest, p4_digest;
} EmEffectsLiveOverlayLog;
void em_effects_live_overlay_log(EmEffectsLiveOverlayLog *out);
/* The last barrel's 001F0720 packets as CRC-32 digests, per lane 0, 1, 3,
 * 4, 5, 6: packet 1; packet 2 (the lane) with each slot's +0x40 parameter
 * quadword zeroed; those 32 parameter quadwords; packet 3; packet 4. */
#define EM_EFFECTS_LIVE_LANE_DIGESTS 30
void em_effects_live_lane_digests(uint32_t out[EM_EFFECTS_LIVE_LANE_DIGESTS]);
/* The last barrel's 001CD520 primitives (the glow markers that were not
 * culled) as CRC-32 digests of their 0x60 bytes without the rand()-pulsed
 * colour (+0x10..+0x1F) and the words 001CD520 does not write (+0x08..
 * +0x0F, +0x2C, +0x4C), sorted; the count. */
int em_effects_live_sprite_digests(uint32_t out[16]);

#ifdef __cplusplus
}
#endif

#endif /* EM_EFFECTS_LIVE_H */
