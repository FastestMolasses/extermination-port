/* em_camera.h — camera system (chase solve, modes, commit).
 *
 * The engine's camera: the chase/follow solve and its three per-shape solvers,
 * the per-mode dispatch (fixed director cameras, the over-shoulder aim mode, door
 * cinematics), and the commit that publishes eye/target for the frame. Split out
 * of em_game.c, which had grown to hold the entire gameplay frame. Behaviour is
 * unchanged by the move — only the file boundary is new.
 */
#ifndef EM_CAMERA_H
#define EM_CAMERA_H

/* The subsystem's shared types and state live here. */
#include "game/em_game_internal.h"

void camera_update(void);
void camera_solve(EmCamera *cam);
void camera_commit(EmCamera *cam);
/* Authored cinematic camera mode 3 has no forward displacement. */
void camera_commit_cinematic(EmCamera *cam);
void camera_desired_eye(EmCamera *cam);
void camera_entry_seat(EmCamera *cam);
float cam_dot3(const float a[3], const float b[3]);
float cam_wrap_pi(float a)               /* func_001B1470 */;
void cam_norm3(float v[3])               /* func_00102760 */;

/* Engine zoom curve: 224 / tan(radians(5 + 45*(1-t)) / 2), a 50-degree
 * vertical FOV at t = 0 narrowing to 5 at t = 1 (func_001D2610 /
 * func_001D2590). zoom(0) == 480, the resting default. */
float em_camera_scope_zoom(float t);

#endif /* EM_CAMERA_H */
