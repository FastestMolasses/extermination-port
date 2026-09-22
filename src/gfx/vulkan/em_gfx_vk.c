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
void em_gfx_draw_skinned_additive(EmGfx *gfx, EmGfxMesh *mesh,
                                  const float *viewproj, const float *palette,
                                  uint32_t bones, const float rgba[4])
{ (void)gfx; (void)mesh; (void)viewproj; (void)palette; (void)bones; (void)rgba; }
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

int em_gfx_particle_texture_set(EmGfx *gfx, const uint8_t *rgba,
                                 uint32_t width, uint32_t height)
{ (void)gfx; (void)rgba; (void)width; (void)height; return 0; }
void em_gfx_particles_draw(EmGfx *gfx, const EmGfxParticle *particles,
                            unsigned count)
{ (void)gfx; (void)particles; (void)count; } /* TODO */
int em_gfx_particle_texture_set_slot(EmGfx *gfx, unsigned slot,
                                      const uint8_t *rgba,
                                      uint32_t width, uint32_t height)
{ (void)slot; return em_gfx_particle_texture_set(gfx, rgba, width, height); }
void em_gfx_particles_draw_slot(EmGfx *gfx, unsigned slot,
                                 const EmGfxParticle *particles, unsigned count)
{ (void)slot; em_gfx_particles_draw(gfx, particles, count); }
