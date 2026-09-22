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
int em_gfx_mesh_update_positions(EmGfx *gfx, EmGfxMesh *mesh,
                                const float *positions, uint32_t count)
{ (void)gfx; (void)mesh; (void)positions; (void)count; return 0; } /* TODO */
void em_gfx_draw_skinned_additive(EmGfx *gfx, EmGfxMesh *mesh,
                                  const float *viewproj, const float *palette,
                                  uint32_t bones, const float rgba[4])
{ (void)gfx; (void)mesh; (void)viewproj; (void)palette; (void)bones; (void)rgba; }
/* Reverse-subtract overlay rect (the screen-fade blend, em_gfx.h) —
 * D3D12: D3D12_BLEND_OP_REV_SUBTRACT with ONE/ONE on RGB, dst alpha
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
