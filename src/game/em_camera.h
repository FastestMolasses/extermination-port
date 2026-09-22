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
/* Original0018C0D0(cam,1), including the status phase5 commit. The forward
 * offset is+4 except mode0xA uses-1; top_mode3 does not bypass this branch. */
void camera_commit(EmCamera *cam);
/* Explicit original argument. With argument0, top_mode3 uses the authored
 * eye; other top modes push+4 only when mode is1/2, otherwise use the eye.
 * This only commits the view; it does not advance the recovery countdown. */
void camera_commit_original(EmCamera *cam, int argument);
/* Original0018C0D0(cam,0), used by the opening's top_mode3 camera. */
void camera_commit_cinematic(EmCamera *cam);
void camera_desired_eye(EmCamera *cam);
void camera_entry_seat(EmCamera *cam);
/* Original panel opcodeD/sub3 sequence: seed, probe style5, solve style1,
 * publish vectors, camera+A0=120. AREA11 only. Returns0 if the collision
 * world is absent or the requested scratchpad rotation is unsupported.
 * Normalized Euler rotations use the original SDK polynomial; the panel
 * fixture is zero and the elevator refusal fixture has nonzero yaw. */
int camera_interaction_retarget_area11(EmCamera *cam,const float player_hip[3],
                                     const float seed_euler[3],float preset_distance);
/* D/sub5 uses distance-20, independent of both current camera+C and preset+64.
 * The original Z/Y/X rotation and homogeneous offset are applied before the
 * same prepass/bounds/solver sequence. No camera+C field is overwritten. */
int camera_interaction_retarget_distance_area11(EmCamera *cam, const float player_hip[3],
    const float seed_euler[3], float distance, float preset_distance);
float cam_dot3(const float a[3], const float b[3]);
float cam_wrap_pi(float a)               /* func_001B1470 */;
void cam_norm3(float v[3])               /* func_00102760 */;

/* Engine zoom curve: 224 / tan(radians(5 + 45*(1-t)) / 2), a 50-degree
 * vertical FOV at t = 0 narrowing to 5 at t = 1 (func_001D2610 /
 * func_001D2590). zoom(0) == 480, the resting default. */
float em_camera_scope_zoom(float t);

#endif /* EM_CAMERA_H */
