#ifndef EM_DOOR_CANDIDATE_H
#define EM_DOOR_CANDIDATE_H
#include "game/em_interaction_scan.h"

/* Original183EF0 selector0/class5. Subtypes3/15 measure distance from
 * the shifted doorway center, but determine the side from the owner
 * origin. This neither arms a door nor runs its transit script. */
int em_door_candidate(const float descriptor[2], const float owner[3], float owner_yaw,
    uint8_t subtype, const EmInteractionPlayer *player, const EmInteractionMath *math,
    float *score);
#endif
