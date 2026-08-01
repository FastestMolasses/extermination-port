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

/* Called from the gameplay frame in em_game.c as well as from this module. */
int  aim_ladder_eval(double t);
int  step_crossed(double prev, double cur, double trig);
void footstep_play(int tier);

#endif /* EM_PLAYER_H */
