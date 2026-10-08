/* em_gfx_vk.c — Linux (and optionally Windows) graphics backend on Vulkan.
 *
 * Clean-room, no third-party libraries; uses only the Vulkan loader/headers
 * that ship with the platform SDK. SKELETON — not yet implemented. Plan:
 *   - VkInstance (+ VK_KHR_surface, VK_KHR_xlib_surface), pick a physical
 *     device + graphics/present queue, VkDevice.
 *   - VkSwapchainKHR sized to em_window_drawable_size; per-image views.
 *   - begin_frame: acquire image, begin a render pass with a clear value.
 *   - end_frame: submit + present.
 *   - Runtime SPIR-V: shaders compiled at runtime (no offline glslc dependency)
 *     once the translated draw pipeline needs them.
 */
#include "em_gfx.h"
#include <stddef.h>

void em_gfx_char_face_rig(EmGfx *gfx, const EmGfxCharRig *rig)
{ (void)gfx; (void)rig; } /* TODO */

EmGfx *em_gfx_create(EmWindow *win) { (void)win; return NULL; } /* TODO */
void   em_gfx_destroy(EmGfx *gfx) { (void)gfx; }
void   em_gfx_begin_frame(EmGfx *gfx, float r, float g, float b, float a)
{ (void)gfx; (void)r; (void)g; (void)b; (void)a; }
void   em_gfx_end_frame(EmGfx *gfx) { (void)gfx; }
int em_gfx_mesh_update_positions(EmGfx *gfx, EmGfxMesh *mesh,
                                const float *positions, uint32_t count)
{ (void)gfx; (void)mesh; (void)positions; (void)count; return 0; } /* TODO */
/* Reverse-subtract overlay rect (the screen-fade blend, em_gfx.h) —
 * Vulkan: VK_BLEND_OP_REVERSE_SUBTRACT with ONE/ONE on RGB, dst alpha
 * kept, once the overlay pass exists here. */
void   em_gfx_overlay_rect_sub(EmGfx *gfx, float x, float y, float w,
                               float h, const float rgb[3])
{ (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)rgb; } /* TODO */
void em_gfx_overlay_rect_sub_before_text(EmGfx *gfx, float x, float y,
                                        float w, float h, const float rgb[3])
{ (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)rgb; } /* TODO */
void   em_gfx_overlay_rect_add(EmGfx *gfx, float x, float y, float w,
                               float h, const float rgb[3])
{ (void)gfx; (void)x; (void)y; (void)w; (void)h; (void)rgb; } /* TODO */

/* Object units (em_gfx.h): not implemented on this backend; -1 is the
 * contract's "cannot draw exactly" (the caller faults). */
int em_gfx_object_unit(EmGfx *gfx, const EmGfxObjectUnit *unit)
{ (void)gfx; (void)unit; return -1; }
int em_gfx_world_textures_reset(EmGfx *gfx)
{ (void)gfx; return -1; }
int em_gfx_object_texture(EmGfx *gfx, uint64_t tex0, const uint8_t *rgba,
                          uint32_t width, uint32_t height)
{ (void)gfx; (void)tex0; (void)rgba; (void)width; (void)height; return -1; }
int em_gfx_gs_opaque(EmGfx *gfx, const EmGfxGsPrim *prims, uint32_t count)
{ (void)gfx; (void)prims; (void)count; return -1; }
/* Player drop shadow and its decal (em_gfx.h): not implemented on this
 * backend; -1 is the contract's "cannot draw exactly" (the caller faults). */
int em_gfx_shadow_alpha_clear(EmGfx *gfx) { (void)gfx; return -1; }
int em_gfx_shadow_box(EmGfx *gfx, const EmGfxShadowStrips *model, const float world[16],
                      const float clip[16], uint32_t rgbaq, const float viewproj[16])
{ (void)gfx; (void)model; (void)world; (void)clip; (void)rgbaq; (void)viewproj; return -1; }
int em_gfx_shadow_silhouette(EmGfx *gfx, const float *verts, uint32_t vert_count,
                             const uint32_t *indices, uint32_t index_count,
                             const float *nodes, uint32_t node_count, const float vp[16])
{
    (void)gfx; (void)verts; (void)vert_count; (void)indices; (void)index_count;
    (void)nodes; (void)node_count; (void)vp;
    return -1;
}
int em_gfx_shadow_receiver_begin(EmGfx *gfx, const float uv[16], const float camera[16],
                                 const float viewproj[16])
{ (void)gfx; (void)uv; (void)camera; (void)viewproj; return -1; }
int em_gfx_shadow_receiver(EmGfx *gfx, const EmGfxShadowStrips *object, uint32_t cls)
{ (void)gfx; (void)object; (void)cls; return -1; }
int em_gfx_shadow_receiver_end(EmGfx *gfx) { (void)gfx; return -1; }
int em_gfx_shadow_target_read(EmGfx *gfx, uint8_t *rgba) { (void)gfx; (void)rgba; return -1; }
int em_gfx_gs_prims(EmGfx *gfx, const EmGfxGsPrim *prims, uint32_t count)
{ (void)gfx; (void)prims; (void)count; return -1; }
int em_gfx_gs_texture(EmGfx *gfx, uint64_t tex0, const uint8_t *rgba, uint32_t width, uint32_t height)
{ (void)gfx; (void)tex0; (void)rgba; (void)width; (void)height; return -1; }
/* The GS frame (em_gfx.h): not implemented on this backend (the caller faults). */
int em_gfx_gs_frame(EmGfx *gfx, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count,
                    uint64_t display_frame, uint64_t display_scissor)
{
    (void)gfx; (void)prims; (void)envs; (void)count; (void)display_frame; (void)display_scissor;
    return -1;
}
int em_gfx_gs_surface_read(EmGfx *gfx, uint32_t fbp, uint32_t fbw, uint32_t height, uint8_t *rgba)
{ (void)gfx; (void)fbp; (void)fbw; (void)height; (void)rgba; return -1; }

/* The status MAP page's ordered 2D flush and 3D scissor (em_gfx.h): not
 * implemented on this backend yet. */
void em_gfx_overlay_decor_flush(EmGfx *gfx) { (void)gfx; }
void em_gfx_draw_scissor(EmGfx *gfx, const float rect[4]) { (void)gfx; (void)rect; }

/* The Original profile's GS frame (em_gfx.h): not built on this backend
 * yet. The CPU GS model (src/gs/em_gs_world.h) is platform-independent; this
 * backend has to present its field. Until then the GS frame refuses. */
int em_gfx_gs_world_enable(EmGfx *gfx, int on, EmGfxGsRead read, void *read_ctx)
{ (void)gfx; (void)read; (void)read_ctx; return on ? -1 : 0; }
int em_gfx_gs_world_enabled(EmGfx *gfx) { (void)gfx; return 0; }
int em_gfx_gs_memory_load(EmGfx *gfx, const char *path) { (void)gfx; (void)path; return -1; }
int em_gfx_gs_world_frame(EmGfx *gfx) { (void)gfx; return 0; }
int em_gfx_gs_upload(EmGfx *gfx, const uint8_t *chain, size_t bytes) { (void)gfx; (void)chain; (void)bytes; return 0; }
void em_gfx_gs_world_drop(EmGfx *gfx) { (void)gfx; }
int em_gfx_gs_world_kick(EmGfx *gfx, const void *env, size_t env_bytes, const void *clear, size_t clear_bytes)
{ (void)gfx; (void)env; (void)env_bytes; (void)clear; (void)clear_bytes; return 0; }
const char *em_gfx_gs_world_fault(EmGfx *gfx) { (void)gfx; return NULL; }
int em_gfx_gs_field_read(EmGfx *gfx, uint8_t *rgba, uint64_t *frame) { (void)gfx; (void)rgba; (void)frame; return -1; }
int em_gfx_gs_world_cost(EmGfx *gfx, EmGfxGsCost *out) { (void)gfx; (void)out; return -1; }
int em_gfx_background_prims_env(EmGfx *gfx, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count)
{ (void)gfx; (void)prims; (void)envs; (void)count; return -1; }
void em_gfx_field_presentation(EmGfx *gfx, EmGfxFieldPresentation mode) { (void)gfx; (void)mode; }
