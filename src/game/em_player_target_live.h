/* Canonical pool/player views for the Use-chain target helpers. */
#ifndef EM_PLAYER_TARGET_LIVE_H
#define EM_PLAYER_TARGET_LIVE_H

#include "game/em_actor_pool.h"
#include "game/em_player_floor.h"
#include "game/em_player_running_jump.h"
#include "game/em_sdk_math_original.h"

int em_player_target_live_entry(const EmActor *object, EmPlayerRunningJumpTarget *out);
int em_player_target_live_xz(const EmActor *object, float *x, float *z);
int em_player_target_live_radius(uint32_t address, const EmActor *object, float *radius);
int em_player_target_live_sight(EmSdkMathContext *sdk, EmPlayerLiveActor *player,
                                uint32_t address, const EmActor *object, float radius,
                                uint32_t *distance, int *result);

#endif
