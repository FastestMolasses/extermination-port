/* em_props.h — placed AREA-11 set pieces (elevator platform, floor grate).
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

#endif /* EM_PROPS_H */
