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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EmGfx EmGfx;

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

/* End the frame: present the swapchain image. */
void em_gfx_end_frame(EmGfx *gfx);

#ifdef __cplusplus
}
#endif

#endif /* EM_GFX_H */
