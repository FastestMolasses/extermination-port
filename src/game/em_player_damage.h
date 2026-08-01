/* em_player_damage.h — player damage, death and vitals.
 *
 * The player's damage pipeline: the lock gate, the infection and health
 * appliers, the flinch/death/struggle entries, the per-frame hurt and vitals
 * ticks. Split out of em_game.c, which had grown to hold the entire gameplay
 * frame. Behaviour is unchanged by the move — only the file boundary is new.
 */
#ifndef EM_PLAYER_DAMAGE_H
#define EM_PLAYER_DAMAGE_H

/* The subsystem's shared types and state live here. */
#include "game/em_game_internal.h"

int player_damage_locked(void);
void player_damage_process(void);
void player_struggle_tick(void);
void player_hurt_tick(void);
void player_vitals_tick(void);

#endif /* EM_PLAYER_DAMAGE_H */
