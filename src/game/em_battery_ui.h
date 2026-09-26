/* The BATTERY page's 2D layer: the EMBA atlas and the ordered leaf draw
 * calls of one 002149F0 frame (docs/STATUS_PAGE_RECORD.md section 4).
 *
 * The page logic and its draws are the original translations bound by
 * em_battery_page_live (002149F0 em_status_page_record; 0020AE40 /
 * 0020B210 / 0020B0D0 em_status_ui_leftovers; 0020CCB0 em_census_standins;
 * 00209280 em_status_draw). Their leaves, the GS 2D layer boundary, are
 * recorded here in the original call order and submitted once by the
 * status runtime's render:
 *   0020A7A0(tex0)                       the status background tile
 *   00207D00(1, mode)                    the blend mode of the calls after it
 *   00207E40(1, x, y, w, h, rgba, tex0)  a textured sprite (GS units)
 *   00207F80(1, x0, y0, x1, y1, rgba)    an untextured rectangle
 *   001CBA50 text                        00209280's gauge caption
 * A sprite's TEX0 names its atlas record: assets/scene_snow/panel/
 * battery.emba (tools/export_panel.py) holds every TEX0 of the page tables
 * D_00265C50 / D_00265CD0, the list highlight, the page background and a
 * white texel for the untextured rectangles. A TEX0 the atlas does not hold
 * fails the render (fail-stop). The render's GS-to-canvas conversion is
 * checked against the captured confirmation packets
 * (tools/test_status_ui_leftovers_reference.py, STATUS_UI_LEFTOVERS.md 1.1
 * step 3). */
#ifndef EM_BATTERY_UI_H
#define EM_BATTERY_UI_H

#include <stddef.h>
#include <stdint.h>

#include "em_gfx.h"

typedef struct EmBatteryUI EmBatteryUI;

/* Loads the EMBA atlas (version 2). NULL on a missing or malformed file. */
EmBatteryUI *em_battery_ui_load(const char *path);
void em_battery_ui_free(EmBatteryUI *ui);

/* The page tables' bytes as the atlas records carry them (their TEX0 words
 * in the ELF order): D_00265C50 (0x80 bytes, records 0..15) and D_00265CD0
 * (0x48 bytes, records 16..24). 1, or 0. */
#define EM_BATTERY_UI_FRAME_TABLE_SIZE 0x80u
#define EM_BATTERY_UI_ROW_TABLE_SIZE 0x48u
int em_battery_ui_tables(const EmBatteryUI *ui, uint8_t frame[EM_BATTERY_UI_FRAME_TABLE_SIZE],
                         uint8_t rows[EM_BATTERY_UI_ROW_TABLE_SIZE]);

/* ---- the frame's leaf calls. Each returns 1, or 0 when the list is full or
 * a call is outside what the renderer draws (slot other than 1, an unknown
 * blend mode, a text cell size the glyph atlas has no style for); after a
 * refusal the list refuses everything until the next begin. ---- */
void em_battery_ui_begin_frame(EmBatteryUI *ui);
int em_battery_ui_background(EmBatteryUI *ui, uint64_t tex0);
int em_battery_ui_blend(EmBatteryUI *ui, int32_t slot, int32_t mode);
int em_battery_ui_sprite(EmBatteryUI *ui, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t rgba, uint64_t tex0);
int em_battery_ui_rectangle(EmBatteryUI *ui, int32_t slot, int32_t x0, int32_t y0, int32_t x1,
                            int32_t y1, uint32_t rgba);
/* 00209280's text (001CBA50 / 001CC1E0) in the current blend mode; style is
 * the style record's colour word. */
int em_battery_ui_text(EmBatteryUI *ui, int proportional, int32_t x, int32_t y, int32_t w,
                       int32_t h, const char *text, uint64_t style);
/* The number of calls recorded since the last begin (0: nothing drawn). */
unsigned em_battery_ui_count(const EmBatteryUI *ui);

/* Submits the recorded calls once on the 512x448 status canvas. 1, or 0
 * (a refused call, a TEX0 outside the atlas, a renderer failure). */
int em_battery_ui_render(EmBatteryUI *ui, EmGfx *gfx);
/* Another page replaced the shared UI texture slot: upload again. */
void em_battery_ui_deactivate(EmBatteryUI *ui);

#endif
