/* em_props.h — original AREA11 elevator, switch panel, and indicators.
 *
 * These are scene props with their own lifetime: the scene loader installs
 * them when a manifest names one, the gameplay frame ticks them, and a scene
 * switch unloads them. They own no state of their own — everything lives in
 * the shared EmGameState, exactly as the engine keeps its placed-actor fields
 * in the gameplay globals. */
#ifndef EM_PROPS_H
#define EM_PROPS_H

#include "em_gfx.h"

void elevator_pose(void);
void elevator_unload(EmGfx *gfx);
int grate_install(EmGfx *gfx, const char *scene_dir,
                         const char *name, const float pos[3], float yaw);
void grate_update(void);
void grate_unload(EmGfx *gfx);

/* Explicit original child models: kind "panel" is global model75 owned
 * by 00159210; "elevator" is per-area model10 owned by 00827B10. Install
 * after the owner. Child resources are released when its owner unloads. */
int em_props_indicator_install(EmGfx *gfx, const char *scene_dir,
                                const char *kind, const char *file);
/* The child's +0x4C draw (001CACB0), reached from 001F54E0 inside the
 * child's own pool node: slot 0 the panel's 0x75, slot 1 the terminal's
 * 0x10; c80 is the child's +0x80 after 001F54E0. Queues this frame's draw
 * (drawn and cleared by em_props_indicators_draw); -1 without a mesh. */
int em_props_indicator_submit(int slot, const float c80[4]);
void em_props_indicators_draw(EmGfx *gfx, const float viewproj[16]);

#endif /* EM_PROPS_H */
