/* em_chain_page_live.h - the chain page D_007635C0 drawn live at the frame
 * close (census WP-13; docs/CHAIN_PAGE.md section 7).
 *
 * 001D1EA0 (em_rcl_001D1EA0) splices the page with 001CB800 and the DMA
 * sends it to the GS. At that position frame_close_out calls
 * em_chain_page_live_draw: em_chain_page walks the spliced chain over the
 * canonical storage (the render context's arena, chain table, context, GS
 * blocks and .data: em_rcl_bytes; the ELF windows the effect-table export
 * places: em_effects_live_window, which hold the two program packets and
 * 001CFBE0's source blocks), walking over this frame's 001DDE10 four-sprite
 * CALL (em_rcl_page), and em_gfx_gs_prims draws every primitive in GS
 * order. The producers are the live originals: the effect barrel's lanes
 * (001F0720), the head sprites and every puff handler (001CFBE0), the
 * glint (001F0A60), the glow markers and equipment sprites (001CD520) and
 * the 0015BF90 decal (001CE300), whose triangle count goes back to
 * em_shadow_live_page_drew; the AREA11 flame (008235F0's 001D04B0 ->
 * 001CFBE0, its descriptor D_00828340 read through em_effects_live_window)
 * and the weather's channel-3 list (001E55F0 / 001E67C0's 001CFFE0 tiles,
 * CALLed by 001E0D70 at slot 0xFFB; the snow program D_00233800).
 *
 * The frame's Q (em_chain_page.h): a vertex whose RGBAQ precedes every ST
 * of the page carries the Q the GS held from the frame's earlier draws,
 * which the port does not model; it is drawn with Q = 1.0, the Q every
 * later sprite of the same packets carries, and counted (log.stale_q).
 *
 * Fail-stop: a page fault, a primitive the renderer does not implement, or
 * a decal count mismatch latches (em_chain_page_live_fault); the caller
 * faults the scene. */
#ifndef EM_CHAIN_PAGE_LIVE_H
#define EM_CHAIN_PAGE_LIVE_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_chain_page.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_CHAIN_PAGE_LIVE_TEXTURES "assets/scene_snow/page_textures.emot"
#define EM_CHAIN_PAGE_LIVE_PRIMS 8192u

/* Register the page textures (tools/export_page_textures.py) with `gfx`
 * (once per gfx). 0, or -1 (missing or malformed: reported). */
int em_chain_page_live_textures(EmGfx *gfx);

/* Walk and draw the page the last kick spliced (em_rcl_page). 0 (drawn, or
 * no kick since the last draw), or -1 (latched). */
int em_chain_page_live_draw(EmGfx *gfx);

/* The latched fault's original address (0x001CB800 for a page fault,
 * 0x001CE300 for the decal count), or 0. */
uint32_t em_chain_page_live_fault(void);

/* The level smoke's view (the tick log's "page"). */
typedef struct {
    uint32_t frame;            /* em_frame_counter() of the last draw        */
    uint32_t pages;            /* cumulative pages drawn                      */
    uint32_t start;            /* the last page's start tag                   */
    uint32_t four_sprite;      /* its skipped 001DDE10 CALL target (0: none)  */
    EmChainPageCounts counts;  /* the last page's                             */
    uint32_t decal_triangles;  /* the last page's decal-TEX0 fan triangles    */
    uint32_t digest;           /* FNV-1a of the last page's primitives        */
    uint32_t weather;          /* the weather list 001E0D70 CALLed (0: none)  */
    uint32_t overlay_reads;    /* reads of overlay source blocks (the flame) */
    uint32_t total_prims, total_stale_q, total_skipped;   /* cumulative      */
} EmChainPageLiveLog;
void em_chain_page_live_log(EmChainPageLiveLog *out);

/* The last page's primitives as drawn (the frame's Q already replaced by
 * 1.0; em_chain_page_live_q tells which), or NULL. */
const EmGfxGsPrim *em_chain_page_live_prims(uint32_t *count);

/* Bytes of original memory the page reads, as the consumer maps them (the
 * smoke dumps the last page through it), or NULL. */
const uint8_t *em_chain_page_live_read(uint32_t address, uint32_t size);
/* The (address, size) pairs the last page's walk read, in order: returns
 * the count and points *pairs at 2 * count words. */
uint32_t em_chain_page_live_reads(const uint32_t **pairs);
/* The last page's q_known flags, 3 per primitive (EmChainPageQ). */
const uint8_t *em_chain_page_live_q(void);

#ifdef __cplusplus
}
#endif

#endif
