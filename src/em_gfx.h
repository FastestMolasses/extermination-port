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

/* Begin a frame: acquire the next swapchain image and start a render pass that
 * clears the framebuffer to (r,g,b,a), each in [0,1]. */
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
                                  level data ships prebaked lighting)
                                  instead of the directional stand-in. */

/* Per-vertex bone-word layout (mirrors EM_MODEL_VERT_* in em_model.h):
 * low 24 bits = palette slot, bit 31 = BILLBOARD+ADDITIVE glow vertex.
 * For glow vertices the position is the anchor point (bone-local) and the
 * "normal" slot is the camera-plane corner offset in world units (x =
 * camera right, y = camera up). Mesh creation partitions triangles so the
 * glow set draws LAST in a second pass: additive blend (the PS2 draws
 * these with GS ALPHA Cv = Cs*(FIX 0x80/128) + Cd), depth test on, depth
 * write off (ZMSK=1) — camera right/up are extracted from the view rows
 * of the draw's viewproj matrix. */
#define EM_GFX_VERT_BONE_MASK 0x00FFFFFFu
#define EM_GFX_VERT_BILLBOARD 0x80000000u

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
 * em_gfx_draw_skinned state (depth write on; with opaque white the output
 * is bit-identical to the untinted call). rgba[3] < 1.0 draws TRANSLUCENT:
 * fragment alpha < 1 goes through standard alpha blending, and the depth
 * WRITE is disabled for the draw (depth TEST stays on) — the GS ZMSK=1
 * state of the engine's faded actor draws, so a fading gib never occludes
 * the scene behind it. The texture alpha-test cutout applies under any
 * tint. em_gfx_draw_skinned is a wrapper passing opaque white. */
void em_gfx_draw_skinned_tinted(EmGfx *gfx, EmGfxMesh *mesh,
                                const float *viewproj, const float *palette,
                                uint32_t bone_count, const float rgba[4]);

/* --- 2D overlay pass (HUD) ------------------------------------------- */

/* Overlay coordinates live on a VIRTUAL CANVAS — origin top-left, y
 * down, stretched to the drawable. Resolution-independent and
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
 * arc = one quad per tessellation segment); overflow is dropped. (1024:
 * the status hub's pager-diamond markers are 12 full-circle arcs on top
 * of the ring gauge — ~900 quads on the busiest frame.) */
#define EM_GFX_OVERLAY_MAX 1024
void em_gfx_overlay_rect(EmGfx *gfx, float x, float y, float w, float h,
                         const float rgba[4]);

/* Queue one REVERSE-SUBTRACT screen-space rectangle: every covered
 * pixel becomes max(0, dst - rgb), saturating per channel; the source
 * has no alpha (dst alpha is left untouched). The native translation of
 * the engine's SCREEN-FADE sprite blend (GS ALPHA_2 = 0xA1 / FIX 0x80:
 * Cv = (Cd - Cs)*128>>7 = Cd - Cs, saturating at 0 — decomp FINDINGS
 * "SCREEN-FADE BLEND", decoded in em_frame.h): a GREY rect with
 * r=g=b=level SUBTRACTS the level from the frame, so shadows crush to
 * black early and highlights survive longest ("exposure pulled down"),
 * NOT a black cover dissolving in. (x, y, w, h) in virtual-canvas
 * units like em_gfx_overlay_rect; own EM_GFX_OVERLAY_SUB_MAX quad
 * budget. Flushed LAST in the overlay sequence — after the untextured
 * rects/arcs, the decor sprites AND the font glyphs: the engine's fade
 * owns the whole GS frame, darkening the HUD with the scene. With
 * nothing queued the draw does not run (frame output stays
 * byte-identical to pre-subtract builds). The engine's mode-1 ADDITIVE
 * variant (0x68: Cv = Cs + Cd, fade to white) has no port caller yet
 * and is not exposed. */
#define EM_GFX_OVERLAY_SUB_MAX 16
void em_gfx_overlay_rect_sub(EmGfx *gfx, float x, float y, float w, float h,
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

/* Queue one TEXTURED overlay quad sampling the UI-DECOR slot — same
 * parameters, sampling and blend as em_gfx_overlay_glyph, own
 * EM_GFX_OVERLAY_MAX quad budget. Decor sprites flush in one draw AFTER
 * the untextured overlay primitives (so the title/legend/icons sit over
 * the scene dim and the pager-diamond arcs, the engine's hub draw
 * order) and BEFORE the glyph quads (text stays on top). No-op without
 * a registered UI texture. */
void em_gfx_overlay_sprite(EmGfx *gfx, float x, float y, float w, float h,
                           float u0, float v0, float u1, float v1,
                           const float rgba[4]);

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

/* Set THIS frame's single forward SPOT LIGHT, applied ONLY by the baked
 * vertex-color LEVEL path of the skinned draws (mesh flag
 * EM_GFX_MESH_VCOLOR — the world geometry). The directional CHARACTER
 * path takes NO spot term (2026-06-11 weapon-visual pass): the spot is
 * muzzle-anchored and points away from the player, so the player/gun
 * must never catch their own light — in the reference capture the
 * beam lights the room only. Reset OFF at em_gfx_begin_frame; with no
 * call the frame output is bit-identical to pre-spot builds (the shader
 * adds exactly 0).
 *
 * `pos`/`dir` world-space (dir normalized by the caller); `rgb` the
 * light color/intensity (components may exceed 1); `range` the falloff
 * distance (quadratic fade to 0 at range); `cos_inner`/`cos_outer` the
 * cone: full intensity inside cos(angle) >= cos_inner, smoothstep fade
 * to 0 at cos_outer.
 *
 * ENGINE TRUTH + DOCUMENTED DEVIATION (decomp FINDINGS "FLASHLIGHT
 * RENDER DECODE", 2026-06-11): the boot ELF draws NOTHING for the
 * flashlight toggle — no beam geometry, no glow sprite, no light-matrix
 * change is keyed on player +0xA or D_00810D3C (their only consumers
 * are gameplay: enemy detection, poses, sounds). The engine's per-actor
 * VU1 light matrix (func_001D89D0: per-room rig D_00251C50 + an
 * ALWAYS-ON camera-direction light, flag +0x2 bit 0x20, set once for
 * the player at init + <=32 dynamic point lights, func_001D7FA0) lights
 * CHARACTERS only; LEVEL geometry ships baked vertex colors and is
 * never dynamically lit. The port adds this forward spot term so the
 * toggle has the player-visible result the original light fixture
 * implies — a deliberate, flagged deviation, not a translation. */
void em_gfx_spot_light(EmGfx *gfx, const float pos[3], const float dir[3],
                       const float rgb[3], float range,
                       float cos_inner, float cos_outer);

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
