/* em_render_context_live.h - the one canonical render context (census lanes
 * L32 frame-render-heads and L30 render-context, bound live). Docs:
 * docs/RENDER_CONTEXT.md section 8, docs/FRAME_RENDER_HEADS.md section 4.
 *
 * The original keeps its render state at the address D_00275670 holds
 * (0x811CC0): the display-list cursors, the flag words, the fog block, the
 * projection P / V / K and the four alternate projections, the zoom, the
 * depth ramps and the four eased value/width pairs. The frame head, the
 * frame close, the fog programmer and the effects all read and write that
 * one block by address. This module owns it, and the memory next to it
 * that the same routines address:
 *
 *   0x0028F700..0x007635BF  the packet arena D_0028F700 (the cursors)
 *   0x007635C0..0x0076B5BF  the chain table D_007635C0 (slots + heads)
 *   0x00811CC0..0x0081423F  the render context (+0x00..+0x257F)
 *   0x00814220..0x0081643F  the GS register blocks D_00275674 points at
 *   0x00816440..0x0081723F  the 14 skin records 001D30A0 fills
 *   0x70003A40..0x70003B3F  scratchpad: the P and K copies (001D1C50)
 *   0x70003B60..0x70003B63  scratchpad: the zoom copy (001D25F0)
 *   0x70003B70..0x70003B73  scratchpad: the screen centre 001AB370 stores
 *                           (0x800, 0x800) and step V reads
 *   .data from the user's ELF (assets/render_context.emrc,
 *   tools/export_render_context.py): D_00241010 (8), D_00250F30..
 *   D_0025316F (the colour, D_002513E0, the room table D_00251C50),
 *   D_0026E510 (16), D_00275670..D_0027569F.
 *
 * Every other byte these routines read belongs to another owner and is a
 * view the binder hands over (EmRclExternal): D_00810E80 and D_00810E88
 * (the frame loop: the buffer index and the field bit), D_008106C4 (the
 * scene state's request byte, from the start for step V),
 * D_00810610 and D_008105E0 (the live camera's pool), the request block
 * D_008106B0.. and the area bytes D_00810700.. and D_008101E4 and 0x70003B8D
 * (the scene state), the player record D_008102B0 (the camera's view of it).
 *
 * The routines are the lane translations, composed over that storage:
 * em_frame_render_heads (001D1AE0, 001D1C50, 001D1EA0, 001D2830, 001D2960,
 * 001D30A0, 001D25F0 / 001D2590 / 001D2610), em_render_context (001D2730,
 * 001E0C80, 001D2910, 001E0D70, 001DDA00 and the four-sprite pass 001DDE10,
 * 001DD950, 001D2DE0), em_packet_chain_original (001CB760, 001CB800, 001CB8A0,
 * the fog programmer 0021B970 / 0021B9A0 / 0021BA80 / 0021B920, the area fog
 * 001D8FD0), em_load_veil_particles (the REF tags, 001DDE10's frame-copy
 * packets and the load veil's draw 0021B1B0), em_gs_blocks_original (the
 * boot builder's GS blocks, at em_rcl_init), em_render_verify_rest (001C1DC0, 001C1F50, 001E2260 / 70 / 80),
 * em_actor_light_001D89D0 (001D7B30), em_player_equipment (0015D2F0),
 * em_status_ui_leftovers (0022EBE0) and the SDK leaves em_effect_original
 * (001026A0), em_sdk_math_original (0011DF78) and em_player_stage_workers
 * (001281C0). Workers the port cannot run yet (001DF110, 001DE920, 001DDB70,
 * 001DFF70, which the first level never reaches) are bound to a fault.
 *
 * Fail-stop: the first fault of any of them is latched here (the original
 * function address, em_rcl_fault()); every later entry returns -1. */
#ifndef EM_RENDER_CONTEXT_LIVE_H
#define EM_RENDER_CONTEXT_LIVE_H

#include <stdint.h>

#include "game/em_load_veil_particles.h"
#include "game/em_point_light.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_RCL_CONTEXT 0x00811CC0u   /* the value of D_00275670 (checked at load) */
#define EM_RCL_CONTEXT_SIZE 0x2580u  /* the context block +0x00..+0x257F */
#define EM_RCL_EXPORT_PATH "assets/render_context.emrc"

/* A byte range another module owns, by original address. */
typedef struct {
    uint32_t address;
    uint32_t size;
    uint8_t *bytes;
} EmRclExternal;

/* The originals this module does not translate, supplied by the binder. */
typedef struct {
    void *ctx;
    /* 001D7C30(): the point-light tick (001D1C50's). */
    int (*w_001D7C30)(void *ctx);
    /* 0011E748 sqrtf and 0011E398 tanf, raw binary32 bits. */
    int (*w_0011E748)(void *ctx, uint32_t x, uint32_t *result);
    int (*w_0011E398)(void *ctx, uint32_t x, uint32_t *result);
    /* 001C1DC0's 001C1EA0(block): the weather spawn. */
    int (*w_001C1EA0)(void *ctx, uint32_t block);
    /* 001C1DC0's 001C1E70 -> 001D52E0 (the static-object grid header): the
     * binder's report (the bank D_0028A5A0 is not exported yet). */
    int (*w_001D52E0)(void *ctx);
} EmRclWorkers;

/* Load the .data blocks (once; later calls return 0) and apply the boot
 * zoom store; `d810E80` is the frame loop's buffer index halfword. 0, or -1
 * (the export is missing or malformed). */
int em_rcl_init(const char *export_path, uint8_t *d810E80);
int em_rcl_loaded(void);

/* Hand over the external views and workers (at each area load). The views
 * must cover D_00810610 (64), D_008105E0 (16), D_008106B0 (0x48),
 * D_00810700 (3), D_008101E4 (1), 0x70003B8D (1) and D_008102B0 (0x320).
 * 0, or -1 (a view is missing). */
int em_rcl_bind(const EmRclExternal *views, unsigned count, const EmRclWorkers *workers);
int em_rcl_bound(void);

/* The latched fault: the original function address, or 0. */
uint32_t em_rcl_fault(void);

/* ---- the bound originals. Each returns 0, or -1 on a fault. ---- */
int em_rcl_001D1AE0(int32_t index);     /* main loop step B */
int em_rcl_001D1C50(void);              /* world / status frame head */
int em_rcl_001D1EA0(int32_t a0);        /* world / status frame close */
int em_rcl_001C1DC0(void);              /* area render init (0x1AE040) */
int em_rcl_001D25F0(uint32_t zoom);     /* zoom store (bits) */
int em_rcl_001D2610(uint32_t x);        /* scope zoom (bits) */
int em_rcl_001D2830(int32_t a0, int32_t a1);
int em_rcl_001E0CC0(void);              /* 001D2DE0(0, 0), +0x1D8 = +0x1E8 = 0 */
int em_rcl_0021B9A0(int32_t mode, uint32_t scale, uint32_t bias);
/* 0021BAC0(slot) / 0021BAE0(slot): the fog record +0xA0..+0xBF saved to /
 * restored from +0x120 + 32 * slot (em_sul_0021BAC0 / em_cs_0021BAE0 on
 * this context): the status screen's 0020DFA0 save and 0020E080 restore. */
int em_rcl_0021BAC0(int32_t slot);
int em_rcl_0021BAE0(int32_t slot);
/* 001DD980's tail: 001DD950(&D_008105E0, 2 + 1.02 d, d), f12 / f13 bits. */
int em_rcl_001DD950(uint32_t a0, uint32_t f12, uint32_t f13);

/* The frame loop's views, handed once at start-up (they stay across area
 * binds): the halfword D_00810E88 (the field bit, em_frame) and the byte
 * D_008106C4 (the scene state's request byte). 0, or -1. */
int em_rcl_frame_views(uint8_t *d810E88, uint8_t *d8106C4);
/* Main-loop step V: 001D2300, the frame's DMA list (em_frame_kick; docs/
 * RENDER_CONTEXT.md section 9). Its kick is the renderer boundary: the port
 * presents the frame after it. Runs in every iteration (needs only the
 * frame views). */
int em_rcl_001D2300(void);
/* Main-loop step W: 001D2580(field), context +0x98 = the field bit. */
int em_rcl_001D2580(int32_t a0);
/* The condition under which 001D2300 calls 001E0DF0 (D_008106C4 == 0,
 * render flag 4 clear, flag 0x20 set), by the translation's own code: the
 * renderer's channel-3 gate reads it. *calls = 1 / 0. 0, or -1. */
int em_rcl_001D2300_calls_001E0DF0(int *calls);
/* The list the last step V kicked and the number of kicks. 0, or -1. */
int em_rcl_kick(uint32_t *chain, uint32_t *kicks);
/* 001D1EF0: the tear-down frame (001D1C50, 001D2830(3, 1), 001D1EA0(0)) on
 * this context (em_frh_001D1EF0). Needs the bind. 0, or -1. */
int em_rcl_001D1EF0(void);
/* The point-light slots of this context (+0x210..+0x221F in the layout of
 * EmPointLightPool: the id counter +0x210, the staged count +0x214, the
 * active slots +0x220, the staged slots +0x1220), the one storage
 * 001D7BB0 / 001D7C30 / 001D7FA0 / 001D8340 address; NULL before the load. */
EmPointLightPool *em_rcl_point_lights(void);

/* The chain page 001CB800 spliced at the last frame close (001D1EA0's
 * kick): its start tag, and the CALL target 001DDE10 appended to slot 0xFFF
 * in that frame (0: none). Taken once: a second call before the next kick
 * returns -1, as it does before any kick. 0, or -1. */
int em_rcl_page(uint32_t *start, uint32_t *four_sprite);

/* ---- readers ---- */
/* The context +0x2468 zoom (the one copy). */
float em_rcl_zoom(void);
/* Host bytes of [address, address + size) in this module's own storage, or
 * NULL. For the consumers that read the context by address. */
const uint8_t *em_rcl_bytes(uint32_t address, uint32_t size);
/* The fog the frame's world draws use: the context +0xA8 / +0xAC pair
 * 001D30A0 copied into the skin records (A, B) and FOGCOL (context +0xB0
 * as 001D1C50 copied it to the GS block: r, g, b in 0..255). 0, or -1 when
 * no frame head ran since the bind. */
int em_rcl_frame_fog(float coef[2], float rgb[3]);
/* The view matrix and zoom of the frame head (context +0x2380 = the
 * D_00810610 001D1C50 projected, and the +0x2468 its 001D2960 read), for
 * the native renderer. 0, or -1 when no frame head ran since the bind. */
int em_rcl_frame_view(uint32_t view[16], float *zoom);
/* P (+0x2340), the 001D2D20(s, 1280, 560, 0.1, 16711680) x V projection
 * (+0x2240, 001CD370(0)) and K (+0x23C0), as the frame head left them. */
int em_rcl_frame_matrices(uint32_t p[16], uint32_t clip[16], uint32_t k[16]);

/* The packet chain over this module's storage (the context, the arena, the
 * chain table D_007635C0, the GS blocks and the scratchpad copies), as the
 * chain workers of the effect binders take it (em_effects_live: 001CB5F0,
 * 001CB6B0, 001CB760, 001CB900 of the effects, the head sprite and the
 * sprite 001CD520). NULL before the load or after a fault. */
struct EmPacketChain *em_rcl_packet_chain(void);

/* Writable host bytes of [address, address + size) in this module's own
 * writable storage (the arena, the context, the GS blocks, the skin
 * records, D_00250F30.., D_00275670.., the scratchpad copies), or NULL
 * (not loaded, an external view, a read-only .data block, outside). For
 * the callers that write by original address as the original does: the
 * owner draw 001CAA00 appends its unit at the context's channel cursor
 * (em_owner_draw_live). */
uint8_t *em_rcl_bytes_mut(uint32_t address, uint32_t size);
/* skin_arena_init (001D2E20; em_skin_arena_init.h): the 14 skin records
 * templates from D_002514D0, as 001D19E0 runs it first at every area load.
 * The frame machine's 001D19E0 binding calls it (the rest of 001D19E0 is
 * not bound, docs/RENDER_CONTEXT.md 8.4). 0, or -1 (not loaded, a fault). */
int em_rcl_skin_arena_init(void);
/* 001D1F80(a0, a1, a2): the veil module's REF tags at the context's
 * channel cursors (em_load_veil_particles over this storage). 0, or -1
 * (not bound, or the fault latched here). */
int em_rcl_001D1F80(int32_t a0, int32_t a1, int32_t a2);
/* 001D2910(a0) over the one render context (em_render_context_001D2910):
 * *result = its return value (for a0 < 0x20, context +0x0C bit a0). The
 * face attachment's 001D3E40 reaches it (em_owner_draw_live). 0, or -1
 * (not loaded, or the fault latched here). */
int em_rcl_001D2910(int32_t a0, uint32_t *result);
/* 0021B1B0(veil): the load veil's draw (0021B550's, docs/
 * LOAD_VEIL_PARTICLES.md) over this storage: its packets at the channel-0
 * cursor, with the render context's fabsf / float_to_int workers, 001DFA40's
 * table and D_0026E880. The run and the buffer index are kept for the step
 * V consumer (em_rcl_veil_span). 0, or -1 (the fault latched here). */
int em_rcl_0021B1B0(const EmLoadVeilParticlesBlock *veil);
/* The channel-0 run [start, end) this frame's 0021B1B0 wrote and the buffer
 * index (context +0x9C) it drew for. Taken once; the frame head (001D1AE0)
 * forgets it. 0, or -1 (none). */
int em_rcl_veil_span(uint32_t *start, uint32_t *end, uint32_t *slot);

/* Test hook: copy original bytes into this module's own storage (only
 * ranges it owns). 0, or -1. */
int em_rcl_poke(uint32_t address, const uint8_t *bytes, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* EM_RENDER_CONTEXT_LIVE_H */
