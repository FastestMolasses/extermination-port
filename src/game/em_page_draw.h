/* em_page_draw - the 2D draw stream of the status pages MAP / SPR4 /
 * DATABASE and the ITEM children EQUIPMENT / EVENT / HEALING
 * (docs/STATUS_PAGES.md section 5, "Drawing"), recorded per frame from the
 * exact arguments of the original's GS packet builders and drawn on the
 * 512 x 448 status canvas like em_battery_ui:
 *   00207D00(1, mode)                   the blend mode (EmGfxOverlayBlend)
 *   00207E40(1, x, y, w, h, rgba, tex0) a sprite: x / 16 - 1792, (y / 16 -
 *                                       1936) * 2, w x h canvas units, the
 *                                       full texture of tex0 (screen rows),
 *                                       GS modulate colour (128 = 1)
 *   00207D90(1, x0, y0, x1, y1)         SCISSOR_1 (A+D register 0x40, the
 *                                       packet em_area01_ui_00207D90 builds):
 *                                       later draws keep only the window
 *                                       pixels x0..x1, field lines y0..y1
 *   00207F80 rectangles (0020CCB0's marker), the trail triangles of
 *   0020AC70, the 001CBA50 / 001CC1E0 text runs and the 0020A7A0 background
 *   (em_status_background, stepped at the draw, as the other pages do).
 * The textures are decoded from the GS memory the original would hold
 * (em_gs_texture, the page modules applied as they load) into one atlas
 * in the shared UI decor slot. A texture the decoder refuses, an unknown
 * text cell or a full stream refuses the frame (the caller faults). */
#ifndef EM_PAGE_DRAW_H
#define EM_PAGE_DRAW_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_gs_texture.h"

typedef struct EmPageDraw EmPageDraw;

EmPageDraw *em_page_draw_create(const EmGsTexture *gs);
void em_page_draw_free(EmPageDraw *draw);
/* A new frame's stream (the scissor back to the whole 512 x 224 field). */
void em_page_draw_begin(EmPageDraw *draw);
/* Each: 1 recorded, 0 refused. */
int em_page_draw_blend(EmPageDraw *draw, int32_t slot, int32_t mode);
int em_page_draw_sprite(EmPageDraw *draw, int32_t slot, int32_t x, int32_t y, int32_t w,
                        int32_t h, uint32_t rgba, uint64_t tex0);
int em_page_draw_scissor(EmPageDraw *draw, int32_t slot, int32_t x0, int32_t y0, int32_t x1,
                         int32_t y1);
/* 00207F80(1, x0, y0, x1, y1, rgba): GS 12.4 corners. */
int em_page_draw_rectangle(EmPageDraw *draw, int32_t slot, int32_t x0, int32_t y0, int32_t x1,
                           int32_t y1, uint32_t rgba);
/* A text run at GS pixel (x, y) with cell w x h; rgb from the style
 * record's low word. */
int em_page_draw_text(EmPageDraw *draw, int proportional, int32_t x, int32_t y, int32_t w,
                      int32_t h, const char *text, uint32_t rgb);
int em_page_draw_background(EmPageDraw *draw, uint64_t tex0);
/* One 00208AD0 arc (the health gauge's 24-float descriptor, drawn as
 * em_status_hub_ui draws it: em_item_geometry_arc's strip). */
int em_page_draw_arc(EmPageDraw *draw, const float descriptor[24]);
/* One Gouraud triangle: xy in GS 12.4, intensity 0..255 (0020AC70). */
int em_page_draw_triangle(EmPageDraw *draw, const int32_t xy[3][2], unsigned intensity);
/* 00208040(1, a, b, c, rgba) (the MAP page's marker, 00210F30): one flat
 * triangle (PRIM 0x143: alpha blending with the current mode) at the three
 * XYZF2 words' X / Y (xy[i][0] the X halfword, xy[i][1] the Y halfword, GS
 * 12.4), colour rgba (untextured: RGB / 255, alpha / 128), clipped to the
 * SCISSOR_1 window. */
int em_page_draw_flat(EmPageDraw *draw, int32_t slot, const int32_t xy[3][2], uint32_t rgba);
/* 001B0000's place in the stream (the UI pool's model draws, the MAP
 * page's 001CB480 nodes on the same GS list): at render the 2D layer so
 * far is drawn (em_gfx_overlay_decor_flush), then models(ctx, gfx, the
 * SCISSOR_1 window on the 512 x 448 canvas) must return 1. */
typedef int (*EmPageDrawModels)(void *ctx, EmGfx *gfx, const float clip[4]);
int em_page_draw_models(EmPageDraw *draw);
void em_page_draw_set_models(EmPageDraw *draw, EmPageDrawModels models, void *ctx);
unsigned em_page_draw_count(const EmPageDraw *draw);
/* Draws the frame's stream. 1, or 0 (refused frame, decode failure). */
int em_page_draw_render(EmPageDraw *draw, EmGfx *gfx);
/* The shared UI decor slot was taken by another page. */
void em_page_draw_deactivate(EmPageDraw *draw);

#endif
