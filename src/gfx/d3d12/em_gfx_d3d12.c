/* em_gfx_d3d12.c — Windows graphics backend on Direct3D 12.
 *
 * Clean-room, no third-party libraries; only the D3D12/DXGI headers in the
 * Windows SDK. SKELETON — not yet implemented. Plan:
 *   - D3D12CreateDevice, command queue, DXGI swapchain on the HWND.
 *   - RTV heap for the back buffers; per-frame command allocator/list.
 *   - begin_frame: transition to RT, ClearRenderTargetView.
 *   - end_frame: transition to present, ExecuteCommandLists, Present.
 *   - Runtime HLSL: D3DCompile at runtime (no offline fxc dependency) once
 *     the translated draw pipeline needs shaders.
 */
#include "em_gfx.h"
#include <stddef.h>

EmGfx *em_gfx_create(EmWindow *win) { (void)win; return NULL; } /* TODO */
void   em_gfx_destroy(EmGfx *gfx) { (void)gfx; }
void   em_gfx_begin_frame(EmGfx *gfx, float r, float g, float b, float a)
{ (void)gfx; (void)r; (void)g; (void)b; (void)a; }
void   em_gfx_end_frame(EmGfx *gfx) { (void)gfx; }
