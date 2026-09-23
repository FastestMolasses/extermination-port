/* em_director.h — cinematic director and its scripted beats.
 *
 * The scripted-cinematic layer: the beat table and its step/zone/finish
 * helpers, the per-frame director tick (which runs BEFORE the actor update so a
 * cue that moves an actor lands the same frame), the camera override, and the
 * letterbox ramp. Split out of em_game.c, which had grown to hold the entire
 * gameplay frame. Behaviour is unchanged by the move.
 */
#ifndef EM_DIRECTOR_H
#define EM_DIRECTOR_H

/* The subsystem's shared types and state live here. */
#include "game/em_game_internal.h"

void director_tick(void);
int director_camera(EmCamera *cam);
float director_letterbox_alpha(void);
int cine_step_to_beat(uint8_t step);
int cine_in_zone(const CineBeat *b);

#endif /* EM_DIRECTOR_H */
