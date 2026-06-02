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

EmGfx *em_gfx_create(EmWindow *win) { (void)win; return NULL; } /* TODO */
void   em_gfx_destroy(EmGfx *gfx) { (void)gfx; }
void   em_gfx_begin_frame(EmGfx *gfx, float r, float g, float b, float a)
{ (void)gfx; (void)r; (void)g; (void)b; (void)a; }
void   em_gfx_end_frame(EmGfx *gfx) { (void)gfx; }
