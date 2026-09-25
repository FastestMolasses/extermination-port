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
/* The translated look-at 00102CD0 (em_cs_00102CD0) of host-float inputs, in
 * the renderer's convention (em_cs_view_to_native). A refused operand
 * latches the scene fault at 0x00102CD0. */
void camera_view_00102CD0(float view[16], const float pos[3], const float fwd[3], const float up[3]);
void camera_entry_seat(EmCamera *cam);
/* 001B7B30 op0D sub 3 (the panel and terminal scripts' camera): the port's
 * 0018CBD0 seed at the camera's own +0x0C distance, then the live camera's
 * 0018D7B0(5), 0018D7B0(1) and cam+A0 = 0x78 (em_camera_live.c). AREA11 only
 * (the live camera must be bound). 1, or 0 on a fault or an unsupported
 * rotation (em_camera_rotation.c). */
int camera_interaction_retarget_area11(EmCamera *cam, const float seed_euler[3]);
/* The same with a constant distance (op0D subs 4 / 5: -14, -20). */
int camera_interaction_retarget_distance_area11(EmCamera *cam, const float seed_euler[3], float distance);
/* 0018CBD0(D_008101E0, D_008102B0, distance) alone: the port's seed of
 * cam+30 / cam+20 / cam+10 (em_camera_rotation.c, em_camera_retarget.c)
 * from the seed Euler (0x70003B50) and the player's +0xA0, the step the two
 * functions above run before the live camera's solve. The AREA11 script
 * host's w_0018CBD0 (001B7B30 subs 2..5: the director's beats 1 and 2) runs
 * it and then its own w_0018D7B0 calls. 1, or 0 on a fault. */
int camera_script_seed_0018CBD0(EmCamera *cam, const float seed_euler[3], float distance);
float cam_dot3(const float a[3], const float b[3]);
float cam_wrap_pi(float a)               /* func_001B1470 */;
void cam_norm3(float v[3])               /* func_00102760 */;

/* Engine zoom curve: 224 / tan(radians(5 + 45*(1-t)) / 2), a 50-degree
 * vertical FOV at t = 0 narrowing to 5 at t = 1 (func_001D2610 /
 * func_001D2590). zoom(0) == 480, the resting default. */
float em_camera_scope_zoom(float t);

/* AREA11's legacy camera stand-ins that still pre-empt camera action 0
 * (00195130) of the live camera (em_camera_live.c): the examine cue (em_examine.c), the
 * fence door cinematic (em_door.c, census L18) and the port's aim camera
 * (census L28; its placement, then the translated 0018D7B0 style 0). They
 * write the g.cam view. Returns CAMERA_STANDIN_NONE when none owns the
 * camera this frame. */
enum { CAMERA_STANDIN_NONE = 0, CAMERA_STANDIN_OWNS = 1, CAMERA_STANDIN_AIM = 2 };
int camera_area11_standins(EmCamera *cam);

#endif /* EM_CAMERA_H */
