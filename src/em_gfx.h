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

/* --- 2D overlay pass (HUD) ------------------------------------------- */

/* Overlay coordinates live on a VIRTUAL CANVAS of 640x448 — the PS2's
 * NTSC full frame — origin top-left, y down, stretched to the drawable.
 * Resolution-independent and period-faithful: HUD code lays out in the
 * same screen space the original GS sprites used. */
#define EM_GFX_OVERLAY_W 640.0f
#define EM_GFX_OVERLAY_H 448.0f

/* Queue one solid screen-space rectangle for this frame's overlay pass.
 * (x, y, w, h) in virtual-canvas units; `rgba` each in [0,1] (alpha
 * blended). Queued rects are flushed automatically inside
 * em_gfx_end_frame, AFTER every 3D draw: one orthographic pass, depth
 * test off, standard alpha blend — the native stand-in for the engine's
 * GS sprite HUD pass at the end of the frame's packet chain. Call
 * between begin_frame and end_frame; with nothing queued the pass does
 * not run (frame output is bit-identical to pre-overlay builds). At most
 * EM_GFX_OVERLAY_MAX rects per frame; overflow is dropped. */
#define EM_GFX_OVERLAY_MAX 512
void em_gfx_overlay_rect(EmGfx *gfx, float x, float y, float w, float h,
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

/* End the frame: flush the queued world-space beams, then the overlay
 * rects, then present the swapchain image. */
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
