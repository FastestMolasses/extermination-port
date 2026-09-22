/* Ordered original ITEM root artwork exported from0020F170/0020F2A0. */
#ifndef EM_ITEM_UI_H
#define EM_ITEM_UI_H

#include "em_gfx.h"

typedef struct EmItemUI EmItemUI;
/* The original0020AC70 additive Gouraud trail must run at its exact place
 * in the decor queue. white_u/v identify the atlas's backend helper texel.
 * Return1 only when its actual geometry was submitted. */
typedef int (*EmItemTrailRenderer)(void *context, EmGfx *gfx, float x, float y, float white_u,
                                   float white_v);

EmItemUI *em_item_ui_load(const char *path);
void em_item_ui_free(EmItemUI *ui);
/* Invalidate when another inventory page takes the shared UI texture slot. */
void em_item_ui_deactivate(EmItemUI *ui);
/* Returns1 rendered,0 malformed/unavailable resource or required worker.
 * hover0..5 is original D930 output. The message phase/group is owned by
 * EmItemRoot; pass help_line−1 when its presenter is inactive. */
int em_item_ui_render(EmItemUI *ui, EmGfx *gfx, unsigned hover, int help_line,
                      EmItemTrailRenderer trail, void *context);

#endif
