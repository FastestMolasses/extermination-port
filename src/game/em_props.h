/* em_props.h — the AREA11 panel's collision cell.
 *
 * The panel 00159210 and the terminal 00827B10 draw themselves: their +0x4C
 * is 001CAA00 over their own records (em_area11_boxes_owner_draw,
 * docs/OWNER_DRAW.md section 10), and their indicator children's +0x4C
 * 001CACB0 -> 001CABA0 is em_indicator_bind_live_draw (section 11). What
 * stays here is the panel's cell 18 in the port's own collision world (the
 * legacy queries that still read it; the original owner publishes its
 * record on the collision world's class lists through 001B17A0). */
#ifndef EM_PROPS_H
#define EM_PROPS_H

#include "em_gfx.h"

/* The panel's cell 18 (`props/panel_cell18.emcb` beside the scene): loaded
 * when the manifest names the panel (its legacy `grate` line; the model and
 * placement it names are not read). 0, or -1. */
int grate_install(const char *scene_dir);
void grate_update(void);
void grate_unload(EmGfx *gfx);

#endif /* EM_PROPS_H */
