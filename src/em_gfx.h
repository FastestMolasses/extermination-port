/* em_gfx.h — graphics backend interface for the Extermination native port.
 *
 * Clean-room: implemented per platform on the OS-native GPU API (Metal on
 * macOS, Direct3D 12 on Windows, Vulkan on Linux). NO third-party libraries.
 * Implementation files live under src/gfx/<api>/.
 *
 * This is intentionally minimal for now — create/destroy + a clear-and-present
 * frame. The real draw API (vertex/index buffers, textures, the translated PS2
 * GS/VU1 draw pipeline) grows on top of this as the game's renderer is
 * reverse-engineered in the decomp repo and reimplemented here.
 */
#ifndef EM_GFX_H
#define EM_GFX_H

#include "em_platform.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EmGfx     EmGfx;
typedef struct EmGfxMesh EmGfxMesh;

/* Create a graphics device bound to the window's native surface. NULL on
 * failure. */
EmGfx *em_gfx_create(EmWindow *win);
void   em_gfx_destroy(EmGfx *gfx);

/* Begin a frame: acquire the next swapchain image and start a render pass.
 *
 * THE GAME FRAME IS 4:3 (2026-06-11, engine-projection adoption): the
 * PS2 renders a 512x448 frame displayed at 4:3, and the world projection
 * (em_mat4_perspective_gs) bakes that aspect. The backend letterboxes/
 * pillarboxes: every draw of the frame — 3D, beams, overlay canvases —
 * maps NDC onto the largest centered 4:3 rect of the drawable (viewport
 * + scissor), exactly how PCSX2 presents the same game. The rect clears
 * to (r,g,b,a); the bars outside it are black. A 4:3 window (the default
 * 960x720) has no bars and is covered edge to edge. */
void em_gfx_begin_frame(EmGfx *gfx, float r, float g, float b, float a);

/* Draw a test triangle (gradient-colored) inside the current frame. Proves the
 * draw path: a render pipeline built from a RUNTIME-COMPILED shader (no offline
 * shader toolchain) + a vertex buffer + a draw call. A scaffold for the real
 * draw API (the translated PS2 GS/VU1 pipeline) that grows on top. */
void em_gfx_draw_test_triangle(EmGfx *gfx);

/* Texture descriptor for mesh creation: RGBA8 rows top-down at `offset`
 * into the texel blob (matches EmModelTex in em_model.h). */
typedef struct {
    uint32_t width, height, offset, reserved;
} EmGfxTexDesc;

/* Mesh creation flags (mirrors EM_MODEL_FLAG_* in em_model.h). */
#define EM_GFX_MESH_VCOLOR 1u  /* "normal" slot carries a baked RGB vertex
                                  color: shade = texture * color (the PS2
                                  level data ships prebaked lighting; the
                                  level kernel submits floor(128*c), so
                                  1.0 = GS 128, the modulate identity).
                                  Without it the slot is the authored
                                  object-kernel normal, lit only through
                                  em_gfx_char_rig (FIRST_LEVEL_AUDIT H18). */

/* EM_GFX_MESH_GSMAT: every EmGfxTexDesc.reserved carries that texture's
 * ORIGINAL GS draw-state code (FIRST_LEVEL_AUDIT R10/R25; the decode and
 * its evidence are in docs/LEVEL_MATERIALS.md). The exporter
 * (export_level.py --gs-materials) writes it together with a per-texture
 * JSON record of the raw register values. The code keeps the fields a
 * backend needs:
 *
 *   bits  0..13  TEST_1 bits 0..13 (ATE, ATST, AREF, AFAIL)
 *   bit  14      PRIM.ABE of the kernel's GIF template (VU1 dmem 0x3FC)
 *   bits 15..22  ALPHA_1 bits 0..7 (the A, B, C, D selectors)
 *   bit  23      TEX0.TCC
 *   bits 24..25  TEX0.TFX
 *   bit  26      TEX1.MMAG
 *   bits 27..29  TEX1.MMIN
 *   bits 30..31  CLAMP_1 wrap mode (WMS; the exporter requires WMT == WMS)
 *
 * Every AREA11 level texture decodes to the depth-tested class-0 state:
 * the packet 001D0F20 builds at arena+0xBA0+0x5A0 (D_00815360) plus the
 * level kernel's template PRIM. That is TEST 0x5000D (alpha GREATER than
 * AREF 0, fail = KEEP), PRIM ABE 0 (no blending), ALPHA 0xA8 (unused while
 * ABE is 0), TEX1 0x60 (bilinear, no mipmaps) and CLAMP 0 (REPEAT). A
 * backend rejects a code with fields it does not implement; it does not
 * approximate them. Meshes without this flag (actors) are drawn opaque
 * with the same class-0 alpha test, because every textured opaque
 * object-kernel draw in the captures (0023C750/0023C480) uses it.
 * The flag is bit 3, not bit 1: binaries that predate it OR the mesh flags
 * into the shader mode word, where bits 1 and 2 select glow and lit
 * shading; bit 3 is inert there. */
#define EM_GFX_MESH_GSMAT 8u
#define EM_GFX_GSMAT_TEST(c)  ((uint32_t)(c) & 0x3FFFu)
#define EM_GFX_GSMAT_ABE(c)   (((uint32_t)(c) >> 14) & 1u)
#define EM_GFX_GSMAT_ALPHA(c) (((uint32_t)(c) >> 15) & 0xFFu)
#define EM_GFX_GSMAT_TCC(c)   (((uint32_t)(c) >> 23) & 1u)
#define EM_GFX_GSMAT_TFX(c)   (((uint32_t)(c) >> 24) & 3u)
#define EM_GFX_GSMAT_MMAG(c)  (((uint32_t)(c) >> 26) & 1u)
#define EM_GFX_GSMAT_MMIN(c)  (((uint32_t)(c) >> 27) & 7u)
#define EM_GFX_GSMAT_WRAP(c)  (((uint32_t)(c) >> 30) & 3u)
/* Sub-fields of EM_GFX_GSMAT_TEST (GS TEST_1 layout). */
#define EM_GFX_GS_TEST_ATE(t)   ((uint32_t)(t) & 1u)
#define EM_GFX_GS_TEST_ATST(t)  (((uint32_t)(t) >> 1) & 7u)
#define EM_GFX_GS_TEST_AREF(t)  (((uint32_t)(t) >> 4) & 0xFFu)
#define EM_GFX_GS_TEST_AFAIL(t) (((uint32_t)(t) >> 12) & 3u)

/* Per-vertex bone-word layout: low24 bits = palette slot, bit31 = the
 * EM_MODEL_VERT_BILLBOARD glow vertex, and bit30 = native face-draw metadata.
 * For glow vertices the position is the anchor point (bone-local) and the
 * "normal" slot is the camera-plane corner offset in world units (x =
 * camera right, y = camera up). Mesh creation partitions triangles so the
 * glow set draws LAST in a second pass: additive blend (the PS2 draws
 * these with GS ALPHA Cv = Cs*(FIX 0x80/128) + Cd), depth test on, depth
 * write off (ZMSK=1) — camera right/up are extracted from the view rows
 * of the draw's viewproj matrix. */
#define EM_GFX_VERT_BONE_MASK 0x00FFFFFFu
#define EM_GFX_VERT_BILLBOARD 0x80000000u
#define EM_GFX_VERT_FACE_LIGHT 0x40000000u /* separately submitted original face */

/* Create a static skinned mesh. `verts` is vert_count records of 10 32-bit
 * words: float pos[3], float normal[3], float uv[2], uint32 bone, uint32
 * tex — the layout the EMDL asset stores (see em_model.h). Indices are u32
 * triangle lists. `texs`/`texels` carry tex_count embedded RGBA8 textures
 * (may be NULL/0: the mesh draws with flat lit shading). All data is
 * copied into GPU objects; the caller may free it. */
EmGfxMesh *em_gfx_mesh_create(EmGfx *gfx, const float *verts,
                              uint32_t vert_count, const uint32_t *indices,
                              uint32_t index_count,
                              const EmGfxTexDesc *texs, uint32_t tex_count,
                              const uint8_t *texels, uint32_t flags);
void em_gfx_mesh_destroy(EmGfx *gfx, EmGfxMesh *mesh);
/* Replace only positions (packed xyz triples), preserving the mesh's exact
 * vertex order, normals, UVs, bone/texture indices and triangle partition.
 * Returns0 on invalid size/allocation failure, leaving the old mesh intact.
 * In-flight draws retain their previous vertex buffer. */
int em_gfx_mesh_update_positions(EmGfx *gfx, EmGfxMesh *mesh,
                                const float *positions, uint32_t vert_count);

/* Draw a skinned mesh inside the current frame.
 *
 * This is the translated PS2 skinning model: per-bone object-space vertices
 * transformed by a bone-matrix palette (what the game DMAs into VU1 dmem),
 * then by the camera. `palette` is bone_count column-major 4x4 world
 * matrices (16 floats each); `viewproj` is one column-major 4x4. Textured
 * vertices sample the mesh's embedded textures (REPEAT addressing — the
 * PS2 data tiles past 1) modulated by directional lighting; untextured
 * ones fall back to flat lit grey. */
void em_gfx_draw_skinned(EmGfx *gfx, EmGfxMesh *mesh, const float *viewproj,
                         const float *palette, uint32_t bone_count);

/* em_gfx_draw_skinned with a per-draw RGBA color modulation — the native
 * translation of the engine's actor RGB multiplier (the GS RGBAQ
 * vertex-color modulate path, FINDINGS.md): the fragment output of every
 * shading mode (textured, baked vertex color, flat lit, glow) is
 * multiplied by `rgba` (each component in [0,1]).
 *
 * Consumers:
 *   - TENDRIL SPIKE tint: the spike mesh is colored entirely by the
 *     engine's actor RGB multiplier — room tint (6,92,1)/128 green —
 *     plus an alpha fade.
 *   - GIB / CORPSE DESPAWN fade: the engine fades dead-actor pieces out
 *     by walking the actor alpha down before freeing them.
 *
 * THRESHOLD RULE: rgba[3] >= 1.0 draws OPAQUE — the exact
 * em_gfx_draw_skinned state: the decoded GS class 0 (EM_GFX_MESH_GSMAT
 * above) with blending off, depth write on and the TEST_1 alpha test that
 * drops only texels whose filtered alpha is 0. rgba[3] < 1.0 draws
 * TRANSLUCENT: fragment alpha < 1 goes through standard alpha blending,
 * and the depth WRITE is disabled for the draw (depth TEST stays on) —
 * the GS ZMSK=1 state of the engine's faded actor draws, so a fading gib
 * never occludes the scene behind it; this path keeps the port's alpha
 * 0.5 cutout (its GS state is not decoded). em_gfx_draw_skinned is a
 * wrapper passing opaque white. */
void em_gfx_draw_skinned_tinted(EmGfx *gfx, EmGfxMesh *mesh,
                                const float *viewproj, const float *palette,
                                uint32_t bone_count, const float rgba[4]);

/* Original effect draw class 2 (001CABA0): keep mesh geometry and palette,
 * ignore its normals, and modulate texture RGB by rgba. GS ALPHA 0x68 /
 * FIX 0x80 adds Cs + Cd, with depth test on and depth/alpha writes off.
 * This does not turn mesh vertices into camera-facing billboards. RGB may
 * exceed 1: original mode-1 color values range from 1/128 to 255/128. */
void em_gfx_draw_skinned_additive(EmGfx *gfx, EmGfxMesh *mesh,
                                  const float *viewproj, const float *palette,
                                  uint32_t bone_count, const float rgba[4]);

/* --- 2D overlay pass (HUD) ------------------------------------------- */

/* Overlay coordinates live on a VIRTUAL CANVAS — origin top-left, y
 * down, stretched to the 4:3 GAME FRAME (the letterboxed viewport —
 * see em_gfx_begin_frame). Resolution-independent and
 * period-faithful: HUD code lays out in the same screen space the
 * original GS sprites used. The DEFAULT canvas is 640x448 (the PS2's
 * NTSC full frame); the engine's STATUS SCREEN composes on a 512x448
 * canvas (GS offsets 0x700/0x790 — FINDINGS.md "STATUS SCREEN LAYOUT"),
 * selectable per-frame with em_gfx_overlay_canvas below. */
#define EM_GFX_OVERLAY_W 640.0f
#define EM_GFX_OVERLAY_H 448.0f
#define EM_GFX_STATUS_W  512.0f   /* the status screen's UI canvas */
#define EM_GFX_STATUS_H  448.0f

/* Select the virtual canvas that SUBSEQUENT overlay primitives lay out
 * on (it only changes the queue-time coordinate mapping — already-queued
 * primitives keep theirs, so one frame can mix canvases). Reset to the
 * 640x448 default at every em_gfx_begin_frame; a caller that switches
 * (em_hud uses 512x448) restores the default when done so later callers
 * (crosshair, screen fade) are unaffected. Non-positive sizes ignored. */
void em_gfx_overlay_canvas(EmGfx *gfx, float w, float h);

/* Queue one solid screen-space rectangle for this frame's overlay pass.
 * (x, y, w, h) in virtual-canvas units; `rgba` each in [0,1] (alpha
 * blended). Queued primitives are flushed automatically inside
 * em_gfx_end_frame, AFTER every 3D draw: one orthographic pass, depth
 * test off, standard alpha blend — the native stand-in for the engine's
 * GS sprite HUD pass at the end of the frame's packet chain. Call
 * between begin_frame and end_frame; with nothing queued the pass does
 * not run (frame output is bit-identical to pre-overlay builds). The
 * per-frame budget is EM_GFX_OVERLAY_MAX quads (one rect = one quad, one
 * arc = one quad per tessellation segment); overflow is dropped. */
#define EM_GFX_OVERLAY_MAX 1024
/* Original MAIN's ordered arcs, marker lines and cursor require 2,376
 * decor records in the captured first-level fixture. Keep its capacity
 * separate from the untextured and font queues. */
#define EM_GFX_DECOR_MAX 4096
void em_gfx_overlay_rect(EmGfx *gfx, float x, float y, float w, float h,
                         const float rgba[4]);

/* Queue one REVERSE-SUBTRACT screen-space rectangle: every covered
 * pixel becomes max(0, dst - rgb), saturating per channel; the source
 * has no alpha (dst alpha is left untouched). The native translation of
 * the engine's SCREEN-FADE sprite blend (GS ALPHA_2 = 0xA1 / FIX 0x80:
 * Cv = (Cd - Cs)*128>>7 = Cd - Cs, saturating at 0 — decomp FINDINGS
 * "SCREEN-FADE BLEND"; CONFIRMED (audit, re-checked a second time
 * 2026-07-31) directly in the recovered C: func_001AE900 (BYTE-MATCHED)
 * initialises each fade display-list block with the 64-bit literal
 * `*((long long *)(hdr + 0x20)) = 0xA1 | (0x80LL << 0x20)`, and
 * func_001AEE70 (NEARMISS) packs the same value there per frame,
 * selecting it with `*(signed char *)(p + 0xC2) == 0` — and +0xC2 is
 * written straight from the second argument of func_001AEE10
 * (BYTE-MATCHED: `D_0028A9A2[0] = a1`), which is what makes the colour
 * argument the subtract/add selector. 0xA1 = A:Cd B:Cs C:FIX D:0,
 * i.e. Cv = (Cd - Cs)*FIX>>7, and FIX 0x80 makes the factor exactly 1.
 * The GREY is engine truth too: func_001AEE70 stores the SAME short
 * (p+0xC4, the fade level) into all three of the block's
 * +0x30/+0x34/+0x38 RGB words, so r=g=b by construction): a GREY rect
 * with
 * r=g=b=level SUBTRACTS the level from the frame, so shadows crush to
 * black early and highlights survive longest ("exposure pulled down"),
 * NOT a black cover dissolving in. (x, y, w, h) in virtual-canvas
 * units like em_gfx_overlay_rect; own EM_GFX_OVERLAY_SUB_MAX quad
 * budget. Flushed LAST in the overlay sequence — after the untextured
 * rects/arcs, the decor sprites AND the font glyphs: the engine's fade
 * owns the whole GS frame, darkening the HUD with the scene. With
 * nothing queued the draw does not run (frame output stays
 * byte-identical to pre-subtract builds). Additive rectangles below
 * share this queue; mixed subtract/add calls preserve their order. */
#define EM_GFX_OVERLAY_SUB_MAX 16
void em_gfx_overlay_rect_sub(EmGfx *gfx, float x, float y, float w, float h,
                             const float rgb[3]);

/* Same subtractive blend, after the scene but before UI/text. Used by
 * the original letterbox effect; subtitles must remain visible over it. */
void em_gfx_overlay_rect_sub_before_text(EmGfx *gfx, float x, float y,
                                        float w, float h, const float rgb[3]);

/* Queue a saturating ADDITIVE rectangle, min(1, dst + rgb), with
 * destination alpha preserved. Original transition colour!=0 selects
 * GS ALPHA 0x68/FIX 0x80. Same final-pass queue/budget as _rect_sub. */
void em_gfx_overlay_rect_add(EmGfx *gfx, float x, float y, float w, float h,
                             const float rgb[3]);

/* Queue one ANNULAR-ARC segment (ring sector) — the native translation
 * of the engine's UI arc primitive func_002082B0, which consumes a
 * 0x60-byte block: center, start/end angle, inner/outer radius, and a
 * 4-color gradient (FINDINGS.md "STATUS SCREEN LAYOUT"). Drives the
 * status screen's circular health gauge and the pager-diamond rings.
 *
 * (cx, cy) center and r_in/r_out radii in virtual-canvas units; a0/a1
 * angles in DEGREES, 0 = straight up (12 o'clock), increasing CLOCKWISE
 * on screen (y-down canvas); a1 may exceed 360 for wrapped sweeps (the
 * engine passes 180..540 for a full ring). The four colors sit at the
 * arc's corners: rgba_is/rgba_os = inner/outer edge at the START angle,
 * rgba_ie/rgba_oe at the END angle — radial and angular gradients in one
 * (matching the block's 4 RGBA slots). Triangulated on the CPU as a fan
 * of quads (<= 6 degrees per segment) through the same overlay vertex
 * path and budget as em_gfx_overlay_rect; same pass, blend and flush
 * rules. Degenerate sweeps/radii (a1 <= a0 or r_out <= r_in) queue
 * nothing. */
void em_gfx_overlay_arc4(EmGfx *gfx, float cx, float cy,
                         float r_in, float r_out, float a0, float a1,
                         const float rgba_is[4], const float rgba_os[4],
                         const float rgba_ie[4], const float rgba_oe[4]);

/* Flat-color convenience wrapper: em_gfx_overlay_arc4 with all four
 * gradient corners set to `rgba`. */
void em_gfx_overlay_arc(EmGfx *gfx, float cx, float cy,
                        float r_in, float r_out, float a0, float a1,
                        const float rgba[4]);

/* --- textured overlay (UI font + UI decor sheets) ---------------------- */

/* TWO overlay texture slots, mirroring the engine's two resident UI
 * texture sources: the streamed font strip (GS block 0x1B00) and the
 * boot-resident status-screen decor sprites (title/legend/icons, GS
 * blocks 0x1D40..0x22F6 — FINDINGS.md "STATUS SCREEN UI TEXTURES"). */
#define EM_GFX_OVERLAY_TEX_FONT 0   /* sampled by em_gfx_overlay_glyph  */
#define EM_GFX_OVERLAY_TEX_UI   1   /* sampled by em_gfx_overlay_sprite */

/* Register one overlay texture slot. `rgba` is w*h RGBA8 texels, rows
 * top-down; the data is copied into a GPU texture (the caller may free
 * it). Replaces any texture previously registered in that slot. Returns
 * 1 on success, 0 on failure (no device / bad slot / bad args) —
 * callers fall back to untextured primitives so a missing asset never
 * regresses the frame. */
int em_gfx_overlay_texture_set(EmGfx *gfx, int slot, const uint8_t *rgba,
                               uint32_t w, uint32_t h);

/* Queue one TEXTURED overlay quad sampling the FONT slot: (x, y, w, h)
 * in virtual-canvas units like em_gfx_overlay_rect; (u0, v0)-(u1, v1)
 * in TEXELS of the registered texture (the engine's text vocabulary —
 * its glyph sprites carry 12.4 texel UVs). Sampled BILINEAR and
 * modulated by `rgba` (the GS draws the font strip with TEX1
 * MMAG/MMIN=1 + TFX modulate), standard alpha blend. Counts against its
 * own EM_GFX_OVERLAY_MAX quad budget; glyph quads flush in one draw
 * LAST in the overlay pass (text composites over panels/gauges AND
 * decor sprites queued the same frame). No-op without a registered
 * texture. */
void em_gfx_overlay_glyph(EmGfx *gfx, float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1,
                          const float rgba[4]);

/* Same font quad, with its top edge shifted right by top_skew pixels.
 * Original 001CC3B0 uses this parallelogram for message markup tag 3. */
void em_gfx_overlay_glyph_skew(EmGfx *gfx, float x, float y, float w, float h,
                              float top_skew,
                              float u0, float v0, float u1, float v1,
                              const float rgba[4]);

/* Queue one TEXTURED overlay quad sampling the UI-DECOR slot — same
 * parameters, sampling and blend as em_gfx_overlay_glyph, own
 * EM_GFX_DECOR_MAX quad budget. Decor records preserve their submission
 * and blend order after untextured overlay primitives and before glyphs.
 * Original untextured Gouraud UI geometry also uses this ordered queue
 * through a white helper texel. No-op without
 * a registered UI texture. */
void em_gfx_overlay_sprite(EmGfx *gfx, float x, float y, float w, float h,
                           float u0, float v0, float u1, float v1,
                           const float rgba[4]);

/* Original00207D00 UI modes. Mixed calls preserve sprite submission order,
 * including clamps between additive/subtractive draws. Subtract and opaque
 * retain the original TEST alpha>0 cutout; opaque replaces destination RGBA. */
typedef enum {
    EM_GFX_UI_ALPHA=0,    /* GS ALPHA44: Cs*As+Cd*(1-As) */
    EM_GFX_UI_ADD=1,      /* GS ALPHA68/FIX80: Cs+Cd */
    EM_GFX_UI_SUBTRACT=2, /* GS ALPHA62/FIX80: Cd-Cs */
    EM_GFX_UI_OPAQUE=3    /* GS ALPHAA8/FIX80: Cs */
} EmGfxOverlayBlend;
void em_gfx_overlay_sprite_blend(EmGfx *gfx,float x,float y,float w,float h,
    float u0,float v0,float u1,float v1,const float rgba[4],EmGfxOverlayBlend blend);

/* One Gouraud triangle in the same ordered decor stream. Sampling one
 * white atlas texel carries an originally untextured GS triangle without
 * changing its per-vertex color or blend mode. Returns0 on queue failure. */
int em_gfx_overlay_triangle(EmGfx *gfx, const float xy[3][2],
    const float rgba[3][4], float white_u, float white_v, EmGfxOverlayBlend blend);

/* --- overlay BACKDROP layer (the animated UI background) -------------- */

/* The engine draws every status/UI screen over an ANIMATED FULL-SCREEN
 * BACKGROUND (the universal drawer func_0020A7A0: scrolling tiled +
 * zooming layers of one per-screen 128x64 texture over the black
 * UI-camera frame) BEFORE the screen's panels — FINDINGS.md "STATUS
 * SCREEN BACKGROUND". The overlay pass's fixed flush order (untextured
 * -> sprites -> glyphs) cannot put a textured layer UNDER the untextured
 * panels, so the backdrop is its own bottom queue: the backdrop fill +
 * backdrop quads flush FIRST in the overlay sequence, before everything
 * else queued this frame. With nothing queued the pass does not run. */

/* Per-frame backdrop quad budget (the background pass is ~40 tiles). */
#define EM_GFX_BACKDROP_MAX 64

/* Queue a full-FRAME solid fill at the very bottom of the overlay pass —
 * the stand-in for the engine's UI-camera scene behind the background
 * layers (a black frame; the rotating player model on it is a documented
 * TODO). Drawn before any backdrop quads, covering the whole drawable.
 * One per frame (the last call wins). */
void em_gfx_overlay_backdrop_fill(EmGfx *gfx, const float rgba[4]);

/* Queue one TEXTURED backdrop quad sampling the UI-DECOR slot — same
 * parameters, sampling and blend as em_gfx_overlay_sprite, but flushed
 * FIRST in the overlay pass (under the untextured primitives, the decor
 * sprites and the glyphs), after the backdrop fill. Own
 * EM_GFX_BACKDROP_MAX quad budget. No-op without a registered UI
 * texture. */
void em_gfx_overlay_backdrop(EmGfx *gfx, float x, float y, float w, float h,
                             float u0, float v0, float u1, float v1,
                             const float rgba[4]);

/* --- World-space beam pass (laser sight) ------------------------------ */

/* Queue one world-space BEAM SEGMENT for this frame: a thin quad from `a`
 * to `b` (world coords), `width` units across, extruded perpendicular to
 * the segment in the camera plane (axial billboard), with one RGBA per
 * end (a gradient along the segment). The native stand-in for the
 * engine's laser-sight line pass: func_001E2BA0 draws the SPR4 laser as
 * 32 consecutive GS LINE segments with per-vertex colors (see
 * em_weapon.c for the per-segment flicker rule that feeds this).
 *
 * Queued beams are flushed inside em_gfx_end_frame BEFORE the overlay
 * pass, as one draw: ADDITIVE blend (GS ALPHA Cv = Cs + Cd — alpha is
 * ignored), depth TEST on / depth WRITE off (ZMSK=1, like the glow
 * pass), using the camera of this frame's LAST em_gfx_draw_skinned call
 * (a world-space pass needs a camera; if no 3D draw ran this frame the
 * queue is dropped). With nothing queued the pass does not run — frame
 * output stays bit-identical to pre-beam builds. At most EM_GFX_BEAM_MAX
 * primitives per frame; overflow is dropped. */
#define EM_GFX_BEAM_MAX 64

/* Original additive particle sprites, projected and quantized by the
 * game-side VU translation. Opposite corners retain their GIF order and
 * texture coordinates; positions are native NDC and share one depth.
 * The GS sprite uses Q=1, so texture interpolation is affine. */
typedef struct EmGfxParticle {
    float corner[2][2];
    float depth;
    float st[2][2];
    float color[4];
} EmGfxParticle;
#define EM_GFX_PARTICLE_TEX_MAX 4
int em_gfx_particle_texture_set_slot(EmGfx *gfx, unsigned slot,
                                      const uint8_t *rgba,
                                      uint32_t width, uint32_t height);
void em_gfx_particles_draw_slot(EmGfx *gfx, unsigned slot,
                                 const EmGfxParticle *particles, unsigned count);
/* Existing snowfall owns slot0; independent effects retain their textures. */
int em_gfx_particle_texture_set(EmGfx *gfx, const uint8_t *rgba,
                                 uint32_t width, uint32_t height);
void em_gfx_particles_draw(EmGfx *gfx, const EmGfxParticle *particles,
                            unsigned count);

void em_gfx_beam(EmGfx *gfx, const float a[3], const float b[3],
                 float width, const float rgba_a[4], const float rgba_b[4]);

/* Queue a small camera-facing SQUARE glow (size x size world units) at
 * `p` — the laser hit-point dot (the engine's func_001CD520 billboard
 * sprite at the clipped ray endpoint). Same pass, blend and depth state
 * as em_gfx_beam; counts against the same EM_GFX_BEAM_MAX budget. */
void em_gfx_beam_dot(EmGfx *gfx, const float p[3], float size,
                     const float rgba[4]);

/* --- TEXTURED beam sprites (laser dot + muzzle-flash sheets) ----------
 *
 * The engine's weapon FX are TEXTURED additive billboards: the laser
 * dot is a func_001CD520 sprite sampling its own 32x16 glow texture,
 * and the muzzle flash (func_001F5040 variant 0) binds chunk27 models
 * 0x0D/0x08/0x07, whose faces sample three additive effect sheets
 * (decomp FINDINGS "Muzzle flash FX" + "The DOT"). These slots carry
 * those sheets so the beam pass can draw the REAL sprites instead of
 * flat-color quads (em_weapon.c loads the assets/fx .emtx files into
 * them). Slot 4 carries the FLASHLIGHT CONE's glow sheet (the chunk27
 * light-cone family's 0x...3222E9 texture — em_weapon.c "FLASHLIGHT
 * CONE"). */
#define EM_GFX_BEAM_TEX_MAX 5

/* Register one beam texture slot (0..EM_GFX_BEAM_TEX_MAX-1). `rgba` is
 * w*h RGBA8 texels, rows top-down, copied into a GPU texture (the
 * caller may free it). Returns 1 on success, 0 on failure (no device /
 * bad slot / bad args) — callers fall back to the untextured
 * primitives so a missing asset never regresses the frame. */
int em_gfx_beam_texture_set(EmGfx *gfx, int slot, const uint8_t *rgba,
                            uint32_t w, uint32_t h);

/* em_gfx_beam / em_gfx_beam_dot sampling a registered slot's texture
 * across the whole quad (UV 0..1; the FX sheets are full-frame sprite
 * images — the flash models sample them edge to edge). The fragment is
 * sample * rgba through the SAME additive, depth-test-on/write-off
 * state as the untextured beams (additive ignores alpha), in the same
 * flush and against the same EM_GFX_BEAM_MAX budget; textured
 * primitives draw AFTER the untextured set, grouped by slot. For the
 * axial-billboard quad, u runs a -> b along the segment (the flash
 * star's muzzle -> tip axis), v across it. No-op (queues nothing) when
 * the slot has no texture — callers keep their untextured fallback. */
void em_gfx_beam_tex(EmGfx *gfx, int slot, const float a[3],
                     const float b[3], float width, const float rgba[4]);
void em_gfx_beam_dot_tex(EmGfx *gfx, int slot, const float p[3],
                         float size, const float rgba[4]);

/* em_gfx_beam_tex with an axis ROLL: the axial quad's width vector is
 * rotated `roll` RADIANS around the a->b axis before the camera-plane
 * extrusion. Translates the muzzle-flash FX actor's rotation lerp
 * (engine func_001F5040 tick >= 4: rot += (-128 - rot) * 0.35 per tick,
 * written to all three rotation components — the star tumbling around
 * the barrel as it decays; decomp FINDINGS "Muzzle flash FX"). roll = 0
 * is exactly em_gfx_beam_tex. */
void em_gfx_beam_tex_roll(EmGfx *gfx, int slot, const float a[3],
                          const float b[3], float width, float roll,
                          const float rgba[4]);

/* Queue one world-space TEXTURED TRIANGLE in the same additive beam
 * pass: three world vertices `p` (xyz xyz xyz) with uvs (uv uv uv)
 * sampling beam slot `slot`, modulated by `rgba`. Drawn in the slot's
 * textured flush (additive, depth test on / write off, cull none —
 * the GS state class of the engine's additive FX meshes). The consumer
 * is the FLASHLIGHT CONE mesh (the chunk27 light-cone model exported by
 * the decomp's export_props --cone; em_weapon.c orients its vertices
 * along the muzzle ray and queues its triangles per aim frame). Own
 * per-frame budget; overflow dropped, nothing queued = no extra GPU
 * work (frame output byte-identical). */
#define EM_GFX_BEAM_TRI_MAX 384
void em_gfx_beam_tri_tex(EmGfx *gfx, int slot, const float p[9],
                         const float uv[6], const float rgba[4]);

/* --- Flashlight spot light (forward spot term) ------------------------ */

/* Set THIS frame's single forward SPOT LIGHT. A REAL cone (cos_inner
 * > -1) is applied ONLY by the baked vertex-color LEVEL path of the
 * skinned draws (mesh flag EM_GFX_MESH_VCOLOR — the world geometry):
 * the directional CHARACTER path takes no term (2026-06-11
 * weapon-visual pass — the muzzle-anchored flashlight must never
 * light the player/gun; the reference beam lights the room only).
 * Normal-carrying (actor) draws never take this term: the former
 * degenerate camera-fill exception (cos_inner <= -1) lit them with an
 * invented N.(-L) wrap and is removed (H18). Reset OFF
 * at em_gfx_begin_frame; with no call the frame output is
 * bit-identical to pre-spot builds (the shader adds exactly 0).
 *
 * `pos`/`dir` world-space (dir normalized by the caller); `rgb` the
 * light color/intensity (components may exceed 1); `range` the falloff
 * distance (quadratic fade to 0 at range); `cos_inner`/`cos_outer` the
 * cone: full intensity inside cos(angle) >= cos_inner, smoothstep fade
 * to 0 at cos_outer.
 *
 * ENGINE TRUTH + DOCUMENTED DEVIATION. The old claim that the boot ELF
 * draws nothing for the flashlight is REFUTED (FIRST_LEVEL_AUDIT R03/R04):
 * 0017A970 sets the gun-light draw enable D_008106C7 together with
 * D_00810D3C; while it is set, 00188ED0 calls 00187780, which (unless
 * area flag 001B0070() & 0x20000000) calls 001D9530, the cone-shell draw
 * of chunk27 library meshes 0x10/0x11/0x16 under the gun light matrix.
 * That cone draw is NOT translated. The engine's per-actor VU1 light
 * matrix (func_001D89D0: per-room rig D_00251C50 + an ALWAYS-ON
 * camera-direction light, flag +0x2 bit 0x20, set once for the player at
 * init + <=32 dynamic point lights, func_001D7FA0) lights CHARACTERS
 * only; LEVEL geometry ships baked vertex colors and is never
 * dynamically lit. This forward spot term is therefore a port stand-in,
 * not a translation: the original's visible result is the cone mesh,
 * not per-pixel light on the level. */
void em_gfx_spot_light(EmGfx *gfx, const float pos[3], const float dir[3],
                       const float rgb[3], float range,
                       float cos_inner, float cos_outer);

/* --- Character light rig (the per-actor VU1 light matrix) -------------- */

/* The native translation of the engine's per-actor lighting model
 * (decomp FINDINGS "PER-ROOM LIGHT RIGS DECODED", 2026-06-11).
 *
 * PROVENANCE, SPLIT (audit-2, 2026-07-31 — the previous audit deferred
 * this and mis-attributed the EE side):
 *   SOURCE-DERIVED. The EE half IS recovered, and it is NOT
 *   func_001D89D0. Correcting that attribution:
 *     - func_001D7B30 (BYTE-MATCHED) is the ROOM-RIG LOOKUP: a linear
 *       search of D_00251C50 for the first of up to 0x2D entries of
 *       stride 0x78 whose first word equals the area key
 *       ((D_00810700 << 8) + D_00810701, or 0xF00 when func_001D2910(8)
 *       is nonzero); NO MATCH FALLS BACK TO THE TABLE BASE rather than
 *       failing — so every area always gets a rig.
 *     - func_001D8130 (BYTE-MATCHED) loads it: 22 floats from that
 *       entry into the active record D_00275688 = &D_00817BC0, zeroing
 *       +0xB0 and writing 128.0f at +0x12C (the modulate identity the
 *       0..128 scale below refers to).
 *     - func_001D8340 (BYTE-MATCHED) composes the rig: THREE slots of
 *       stride 0x10; slot 0 with the live flag set folds the inverted
 *       D_00810610 matrix into its quat (the CAMERA fill), slots 1-2
 *       take the rig eulers; then it scans the 32-entry, 0x80-stride
 *       dynamic pool at D_00275670 and, per entry with weight
 *       (+0x24C) > 0, accumulates a direction and a colour from that
 *       light's position (+0x10) and colour (+0x20).
 *     - func_001D7FA0 (BYTE-MATCHED) registers those dynamic point
 *       lights and caps the pool at 32 verbatim: `if (idx >= 0x20)
 *       return -1;`. So "3 light slots" and "<=32 dynamic point
 *       lights" are engine truth, not observation.
 *     - func_001D89D0 (NEARMISS) is the per-MODE DISPATCHER that drives
 *       all of the above (modes 1/3/4/5/6 tail into func_001D8C30;
 *       the default mode runs the load/compose chain). Calling it "the
 *       rig builder" was wrong: it never touches D_00251C50 or the
 *       light pool itself.
 *   NOT SOURCE-DERIVED. The EQUATIONS below still are not: they are
 *   read out of VU1 MICROCODE at 0x23C780, a separate ISA the decomp
 *   does not target, and there is no Extermination/src/func_*.c for the
 *   kernel. Treat them as MICROCODE-OBSERVED: the
 *   skinning kernel (VU1 0x23C780) lights every CHARACTER vertex as
 *
 *   I_i  = max(dot(dir_i, N), 0)            (3 light slots)
 *   rgb  = min(amb + sum I_i * col_i, 255)  (col/amb on the 0..128
 *   shade = tex * rgb / 128                  GS-modulate scale)
 *
 * from the 4-qw color matrix at VU1 dmem 0x3F5 + the direction rows
 * folded into each node's normal matrix (fed by the EE chain named
 * above: func_001D7B30 room-rig lookup -> func_001D8130 load ->
 * func_001D8340 camera fill in slot 0 + dynamic point-light fold).
 * The "3 light slots" here is func_001D8340's own slot count.
 * The CALLER composes the rig per draw (em_game's LIGHTING
 * hunks own the room-rig lookup, the camera-fill rotation and the
 * lamp fold — the engine does all three on the EE/VU0 too); this
 * struct is the composed, world-space result.
 *
 * dir rows are world-space (not necessarily unit — the engine's slot-0
 * fold normalizes, slots 1/2 come unit from the rig table); col/amb on
 * the engine 0..128 scale (128 = modulate identity; values above 128
 * over-brighten toward the 255 clamp, exactly the GS headroom).
 *
 * CALLER CONTRACT (verified per vertex against executed 001D89D0 and the
 * captured AREA11 DMA units, tools/test_actor_lighting_reference.py,
 * docs/ACTOR_LIGHTING.md):
 *   - slot 0 is zero unless actor+2 bit 0x20 (camera fill);
 *   - the point-light fold runs only when em_lighting_fold_gate(actor
 *     type byte +3, model radius +0x20) passes (001D8270);
 *   - the fold/camera light point is the node selected by actor+0x98
 *     (node world +0xC0), or actor+0xB0 when it is 0xFF;
 *   - col rows and amb carry the actor RGB (actor+0x80..0x88) product,
 *     em_lighting_actor_rgb (001D8690); the self-glow of actor+2 bit
 *     0x40 is not representable here;
 *   - every normal-carrying mesh vertex is lit with its own node's world
 *     matrix (the palette slot the vertex names). */
typedef struct {
    float dir[3][4];   /* light directions, slots 0..2 (w unused) */
    float col[3][4];   /* light colors, 0..128 scale (w unused)   */
    float amb[4];      /* ambient row, 0..128 scale (w unused)    */
} EmGfxCharRig;

/* Set the rig consumed by SUBSEQUENT skinned draws' character path
 * (mesh without EM_GFX_MESH_VCOLOR; the baked-vertex-color LEVEL path
 * and the glow pass never take it — engine truth: level geometry is
 * never dynamically lit). NULL disables it again. Reset to NULL at
 * em_gfx_begin_frame. A normal-carrying mesh drawn without a rig is NOT
 * drawn (its opaque set is rejected; the diagnostic prints once per
 * mesh): the original always has a rig (001D7B30 falls back to table
 * entry 0), and the former rig-less 0.30 + 0.70*N.L stand-in was
 * invented (H18). The
 * flashlight spot applies only to the LEVEL path. */
void em_gfx_char_rig(EmGfx *gfx, const EmGfxCharRig *rig);

/* Original faces use 001D88B0: camera fill enabled, dynamic light folding
 * disabled. Publish this after the body rig for a mesh with FACE_LIGHT
 * vertices. Setting the body rig clears this override. */
void em_gfx_char_face_rig(EmGfx *gfx, const EmGfxCharRig *rig);

/* --- Distance fog (the per-area GS fog) -------------------------------- */

/* The native translation of the engine's GS distance fog
 * (src/gfx/metal/em_fog_gs.h has the full original chain). 001D8FD0 reads
 * the per-area 0x78-byte record from D_00251C50 — rec+4/+8 = fog near/far,
 * rec+0xC/10/14 = fog colour ints — and 0021B970/0021B920 store the VU
 * coefficients A = 255*far/(far-near), B = -255/(far-near) at ctx+0xA0,
 * while 0021BA80 packs the ints into GS FOGCOL (0..255 framebuffer
 * units). The VU1 kernel at 0023C780 computes F = clamp(A + B*clip_w,
 * 0, 255) per VERTEX and writes floor(F) into XYZF2; the GS interpolates
 * F across the primitive and blends out = (F/255)*Cs + (1 - F/255)*FOGCOL.
 * AREA-11 (key 0x0B00) uses near = -209, far = 304, FOGCOL (48,48,48).
 * The NEGATIVE near means geometry at the camera (clip_w = 0) is already
 * partly fogged (F = A ~ 151); the near term is not clamped to 0. clip_w
 * is the port's clip-space w (the GS-shaped projection), so the vertex
 * shader evaluates F from it directly.
 *
 * Per-frame state (like em_gfx_spot_light), reset OFF at begin_frame:
 * applies to BOTH the LEVEL path (baked vertex color) and the CHARACTER
 * path of the skinned shader. `rgb` is FOGCOL in 0..255 GS units; the
 * backend stores channel / 255 (em_fog_gs_color_unit). A scene with NO
 * fog record never calls this, so fog-less scenes (office, drawbridge)
 * stay byte-identical. Checked by tools/test_area11_fog_reference.py. */
void em_gfx_fog(EmGfx *gfx, float near_z, float far_z, const float rgb[3]);

/* Disable distance fog again (begin_frame also resets to OFF). Fog-off
 * frames run the EXACT pre-fog shader arithmetic — byte-identical. */
void em_gfx_fog_off(EmGfx *gfx);

/* --- Level background (the textured "sky" grid behind the level) ------- */

/* The original does not clear the colour buffer per frame (the frame
 * clear 001D2300 REFs is a Z-only sprite). Behind the level it draws a
 * full-screen textured grid first in the world chain: 001E1E60 (render
 * channel 3, CALLed by 001D2300 right after the Z clear) uploads the
 * camera matrix and grid constants, and the VU1 kernel 0x0023C990 turns
 * them into 31 triangle strips whose ST come from the view direction of
 * each grid point (src/gfx/metal/em_background_gs.h, docs/BACKGROUND.md).
 *
 * em_gfx_background_load reads the asset the decomp's
 * `export_level.py --background` writes (the scene manifest's
 * `background <file>` line) and returns 0, or a negative value when the
 * file is missing or invalid, or when its GS state is one the backend
 * does not reproduce (the reason is printed; nothing is drawn — there is
 * no stand-in). It replaces any previous background. unload drops it
 * (scene switch to an area without one). ready reports whether one is
 * loaded. */
int  em_gfx_background_load(EmGfx *gfx, const char *path);
void em_gfx_background_unload(EmGfx *gfx);
int  em_gfx_background_ready(EmGfx *gfx);

/* Draw the loaded background immediately (no-op when none is loaded).
 * Call it once per world frame BEFORE any other 3D draw — the original
 * draws it first and later draws cover it; it tests and writes no depth
 * (TEST ZTST ALWAYS, ZBUF ZMSK 1). `view` is the frame's native view
 * matrix (em_mat4_lookat_gs, the one the level draws use) and zoom_s the
 * engine zoom (ctx+0x2468 — em_mat4_perspective_gs's argument). The draw
 * turns the view back into the original ctx+0x2380 by negating rows 1
 * and 2; that the native view IS the original with those rows negated is
 * checked by tools/test_camera_reference.py, not by the background test.
 * The rest is checked by tools/test_background_reference.py. */
void em_gfx_background_draw(EmGfx *gfx, const float view[16], float zoom_s);

/* --- Player drop shadow: the GS side of 001DA6A0 ------------------------ */

/* The draws the original chain 001DA6A0 builds (docs/SHADOW_ORIGINAL.md,
 * src/gfx/metal/em_shadow_gs.h for the register values and the kernel
 * arithmetic). src/game/em_shadow_original computes every input; each
 * call below is one of its workers and must be made in its worker order,
 * inside a frame, after the level and the walked actors and before the
 * player's own draw (gameplay 001AE5E0 calls 001DA6A0 at 0x1AE654 after
 * 001AFD70(0)):
 *
 *   w_alpha_clear      -> em_gfx_shadow_alpha_clear
 *   w_box (x2)         -> em_gfx_shadow_box
 *   w_silhouette       -> em_gfx_shadow_silhouette
 *   w_receiver_begin   -> em_gfx_shadow_receiver_begin
 *   w_receiver (each)  -> em_gfx_shadow_receiver(gfx, strips, object->cls)
 *   w_receiver_end     -> em_gfx_shadow_receiver_end
 *
 * The guard-band clip kernels 00239C90 (run by 001DA310 after every box)
 * and 0023E8A0 (001D5C80's re-pass of a class-2 receiver: 001D4FB0,
 * 001D1F80(0,2,6), 001D4B50, 001D4CD0) are translated in
 * src/game/em_vu1_shadow_clip.h. em_gfx_shadow_box and a class-2
 * em_gfx_shadow_receiver run them on the CPU and draw what they kick after
 * the kernel's own triangles; a class 0/1 receiver has no re-pass and never
 * draws those triangles.
 *
 * Original matrices are passed as the 16 floats of their memory (rows
 * contiguous, row-vector convention, exactly as em_shadow_original's plan
 * holds them). `viewproj` is the frame's native column-major P*V (the
 * matrix the level meshes are drawn with). Every call returns 0, or -1
 * when it cannot draw exactly what the original draws (outside a frame, a
 * missing input, a GPU without framebuffer fetch, receivers without the
 * frame's fog, a clip-kernel fault, the per-frame
 * target budget); the reason is printed once per session. There is no
 * stand-in: a caller turns -1 into its fault. */

/* The VU1 vertex lists of the original models: `qw3` holds vertex_count
 * position qwords in kick order (x, y, z as floats, then the vertex's data
 * word, whose bits carry the ADC flags and whose float value is the strip
 * winding sign); every EM_GFX_SHADOW_BATCH vertices are one GS packet of
 * the kernel (NLOOP 32). */
#define EM_GFX_SHADOW_BATCH 32u
#define EM_GFX_SHADOW_TARGET_MAX 8u   /* silhouettes per frame */
typedef struct {
    const float *qw3;
    uint32_t vertex_count;   /* a multiple of EM_GFX_SHADOW_BATCH */
} EmGfxShadowStrips;

/* 001DA290: destination alpha of the whole game frame becomes 0; colour
 * and depth are kept. */
int em_gfx_shadow_alpha_clear(EmGfx *gfx);

/* 001DA310: one box model (the chunk27 library model 0x14 or 0x15, as
 * its original strips) through kernel 00237180's cull: its triangles
 * write destination alpha = the A byte of `rgbaq` where they lie in front
 * of the frame's depth (GEQUAL, no depth write); colour is kept. `world`
 * is the box plan's W (placement), `clip` its (W x V) x P (the kernel's
 * dmem 0..3, which decides the cull), then 00239C90's clipped triangles.
 * Returns -1 only when that kernel faults (FTOI outside int32), a data word
 * names a matrix other than dmem 0, or a kicked point cannot be
 * unprojected. */
int em_gfx_shadow_box(EmGfx *gfx, const EmGfxShadowStrips *model,
                      const float world[16], const float clip[16],
                      uint32_t rgbaq, const float viewproj[16]);

/* 001D9EE0: clear a 128x128 target to (128,128,128,0) and draw the proxy
 * mesh in it with the flat colour (128,255,255,255): vertex i of `verts`
 * (EMDL records, EM_GFX_VERT_BONE_MASK = node slot) goes through
 * node[slot] (+0x90 of the actor's node, row-vector 16 floats) x `vp`
 * (D_70003AC0 of the call) exactly as 001C7420 + kernel 0023C750 compute
 * it, then to the GS 12.4 grid; triangles with a vertex outside the
 * kernel's guard band are not drawn (kernel ADC). The receivers of this
 * frame's next em_gfx_shadow_receiver_begin sample this target. */
int em_gfx_shadow_silhouette(EmGfx *gfx, const float *verts, uint32_t vert_count,
                             const uint32_t *indices, uint32_t index_count,
                             const float *nodes, uint32_t node_count,
                             const float vp[16]);

/* 001D4CD0: bind the receiver pass. `uv` = ctx+0x24B0 (dmem 8), `camera`
 * = D_70003AC0 (dmem 0; it gives Q and fog F per vertex). Needs the
 * frame's em_gfx_fog (the template fog row and FOGCOL). */
int em_gfx_shadow_receiver_begin(EmGfx *gfx, const float uv[16],
                                 const float camera[16],
                                 const float viewproj[16]);

/* 001D4FB0 for one receiver object of class `cls` (em_shadow_original's
 * receiver clip class: 0 on screen, 1 inside the guard band, 2 leaving
 * it): redraw the level object's original strips with kernel 0023C200's
 * per-vertex ST,
 * RGBAQ (0,0,0,A) and F, sampling the silhouette target with the GS pixel
 * pipeline: bilinear MODULATE, fog, alpha test A > 0, destination alpha
 * bit 7 set, depth GEQUAL without write, Cv = (Cs - Cd) * As >> 7 + Cd,
 * written alpha As. The positions go through `viewproj` of the begin call
 * exactly as the level mesh's (identity palette), so the level's own
 * depth passes. Triangles with a vertex outside the guard band are not
 * drawn by 0023C200; for cls 2 the original then runs 0023E8A0 over the
 * same strips, and its clipped triangles are drawn after the object's own
 * (-1 only on the faults named for the box). cls > 2 returns -1. */
int em_gfx_shadow_receiver(EmGfx *gfx, const EmGfxShadowStrips *object,
                           uint32_t cls);

/* 001D1FF0(0, 1): end the receiver pass. */
int em_gfx_shadow_receiver_end(EmGfx *gfx);

/* Test hook: after em_gfx_end_frame, copy the last silhouette target of
 * that frame as 128x128 RGBA8 rows (target pixel (x, y) = GS window
 * (1984 + x, 1984 + y)). Returns 0, or -1 when no silhouette was drawn. */
int em_gfx_shadow_target_read(EmGfx *gfx, uint8_t *rgba);

/* --- last skinned palette (the published bone matrices) ---------------- */

/* The engine PUBLISHES bone world matrices for equipment consumers: the
 * gun tick func_00188630 reads the player's hand-bone matrix (player
 * +0x90) to derive the muzzle point and fire direction every frame (the
 * s8 "equipment draw matrix == bone matrix" mechanism). The port's
 * native equivalent: every em_gfx_draw_skinned(_tinted) call records the
 * leading EM_GFX_TRACK_BONES bone matrices of its palette (a 64-byte
 * copy per tracked bone — palette buffers may be freed by scene loads,
 * so the record COPIES rather than aliasing). The gameplay render chain
 * draws the PLAYER LAST every frame (em_game render_chain_build appends
 * it after scene/doors/enemies), so between one frame's close-out flush
 * and the next frame's draws this holds the player's evaluated palette
 * of the previous frame — placement applied, world space.
 *
 * em_gfx_last_skinned_bone copies the recorded column-major 4x4 of
 * `bone` into out16 and returns 1; returns 0 (out untouched) when no
 * skinned draw has run yet or bone >= min(bone_count, tracked). One
 * frame of latency by construction — the same order of staleness as the
 * engine's own fire-event mailbox; consumers (em_weapon's muzzle/laser
 * anchoring) accept it. */
#define EM_GFX_TRACK_BONES 16
int em_gfx_last_skinned_bone(EmGfx *gfx, uint32_t bone, float out16[16]);

/* em_gfx_last_viewproj copies the column-major P*V of the LAST skinned
 * draw into out16 and returns 1; returns 0 (out untouched) before any
 * skinned draw has run. The same record the beam flush already renders
 * with — published for gameplay consumers that need the engine's
 * camera-matrix reads (the spad 0x70003AC0 matrix func_00199220
 * projects target aim points through for the screen-space acquisition
 * cone; em_weapon is the consumer). One frame of latency by
 * construction, like the bone publish above — the cone test runs
 * against the previous frame's camera, the same staleness class as the
 * fire-event mailbox. */
int em_gfx_last_viewproj(EmGfx *gfx, float out16[16]);

/* End the frame: flush the queued world-space beams, then the overlay
 * (backdrop fill + backdrop quads, then untextured rects/arcs, then
 * decor sprites, then font glyphs, then the reverse-subtract rects —
 * the screen fade covers everything), then present the swapchain
 * image. */
void em_gfx_end_frame(EmGfx *gfx);

/* Capture the NEXT completed frame to a 24-bit BMP at `path`. Returns
 * immediately; the write happens inside that frame's end_frame. Intended
 * for headless verification (screenshot-based regression checks) — not a
 * gameplay feature. */
void em_gfx_request_capture(EmGfx *gfx, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* EM_GFX_H */
