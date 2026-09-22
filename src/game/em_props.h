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
void elevator_descent_begin(void);
void elevator_tick(void);
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
void em_props_indicators_tick(void);
void em_props_indicators_draw(EmGfx *gfx, const float viewproj[16]);
/* Original panel's own successful interaction completion, not merely an
 * external change to the area's shared power bit. */
void em_props_panel_complete(void);

#endif /* EM_PROPS_H */
