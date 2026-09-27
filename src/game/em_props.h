/* em_props.h — the AREA11 panel's collision cell and the panel / terminal
 * indicator children's stand-in draws.
 *
 * The panel 00159210 and the terminal 00827B10 draw themselves: their +0x4C
 * is 001CAA00 over their own records (em_area11_boxes_owner_draw,
 * docs/OWNER_DRAW.md section 10). What stays here:
 * - the panel's cell 18 in the port's own collision world (the legacy
 *   queries that still read it; the original owner publishes its record on
 *   the collision world's class lists through 001B17A0);
 * - the children's +0x4C 001CACB0 (-> 001CABA0), which is not translated:
 *   the child's model is drawn with the additive class-2 stand-in
 *   (em_gfx_draw_skinned_additive) at the child's own node matrix (its slot
 *   +0x90: 001C6380's placement, or for the terminal's child the terminal's
 *   node 0 its 0x827E6C copies there). docs/OWNER_DRAW.md section 11. */
#ifndef EM_PROPS_H
#define EM_PROPS_H

#include "em_gfx.h"

/* The panel's cell 18 (`props/panel_cell18.emcb` beside the scene): loaded
 * when the manifest names the panel (its legacy `grate` line; the model and
 * placement it names are not read). 0, or -1. */
int grate_install(const char *scene_dir);
void grate_update(void);
void grate_unload(EmGfx *gfx);

/* Explicit original child models: kind "panel" is global model75 owned
 * by 00159210; "elevator" is per-area model10 owned by 00827B10. Child
 * resources are released by grate_unload. */
int em_props_indicator_install(EmGfx *gfx, const char *scene_dir,
                                const char *kind, const char *file);
/* The child's +0x4C draw (001CACB0), reached from 001F54E0 inside the
 * child's own pool node: slot 0 the panel's 0x75, slot 1 the terminal's
 * 0x10; c80 is the child's +0x80 after 001F54E0, `node` the child's slot 0
 * +0x90 (every bone the model's vertices name is node 0). Queues this
 * frame's draw (drawn and cleared by em_props_indicators_draw); -1 without
 * a mesh. */
int em_props_indicator_submit(int slot, const float c80[4], const float node[16]);
void em_props_indicators_draw(EmGfx *gfx, const float viewproj[16]);

#endif /* EM_PROPS_H */
