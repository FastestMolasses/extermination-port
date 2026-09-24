/* em_player_damage.h — the legacy bug-latch struggle and its death.
 *
 * The lock gate, the struggle (a stand-in for the latch reaction 002208C0)
 * and the hurt tick that plays it. The damage processor 0021C440, the drain
 * 0015D100 and the kill plane run on the original player stage since census
 * L01 (em_player.c player_states_stage).
 */
#ifndef EM_PLAYER_DAMAGE_H
#define EM_PLAYER_DAMAGE_H

/* The subsystem's shared types and state live here. */
#include "game/em_game_internal.h"

int player_damage_locked(void);
void player_struggle_tick(void);
void player_hurt_tick(void);

#endif /* EM_PLAYER_DAMAGE_H */
