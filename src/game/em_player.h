/* em_player.h — player locomotion, footsteps and aim direction.
 *
 * The player's own frame: wall probes and collide-and-slide, the turn-rate
 * gait ladder, the camera-relative move basis, the footstep picker with its
 * surface-attribute table, and the aim-direction ladder. Split out of em_game.c,
 * which had grown to hold the entire gameplay frame. Behaviour is unchanged by
 * the move — only the file boundary is new.
 */
#ifndef EM_PLAYER_H
#define EM_PLAYER_H

/* The subsystem's shared types and state live here. */
#include "game/em_game_internal.h"

void player_move(void);
void player_turn_toward(float desired, float rate);
float player_move_cam_yaw(void);
void aim_dir_get(float out[3]);
unsigned footstep_rand5(void);
void player_move_collide(float mx, float mz);
void player_wall_probes(void);

/* WP-2/H12 pose-source lifetime (defined in em_player_pose_host.c). A port
 * stand-in holds the original channel source frozen while it owns the
 * player; its release re-seeds the row default as 00182DF0 does. Release
 * returns 1 when the source is ordinary again, 0 while still held. */
void player_pose_legacy_hold(const char *owner);
int player_pose_legacy_release(void);
void player_pose_unsupported_hold(const char *reason);

/* Called from the gameplay frame in em_game.c as well as from this module. */
int  aim_ladder_eval(double t);
int  step_crossed(double prev, double cur, double trig);
void footstep_play(int tier);

#endif /* EM_PLAYER_H */
