/* Original001B6F00 placement before an owner's interaction script. */
#ifndef EM_INTERACTION_ALIGNMENT_H
#define EM_INTERACTION_ALIGNMENT_H

/* owner_matrix is the actor's D0 world transform, not a displayed bone.
 * Transform the complete homogeneous local point through SDK001026A0,
 * retain the player's ground Y, and wrap owner_yaw+yaw_offset. The host
 * applies yaw first, then calls its182F90 position-mirror operation.
 * Finite normalized game coordinates are required. Returns1 on success. */
int em_interaction_alignment(const float owner_matrix[16], float owner_yaw,
    const float local_point[4], float yaw_offset, float ground_y,
    float target[4], float *player_yaw);

#endif
