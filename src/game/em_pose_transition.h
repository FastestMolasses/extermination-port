#ifndef EM_POSE_TRANSITION_H
#define EM_POSE_TRANSITION_H

#include <stdint.h>

/* Evaluated original node channels. Rotation is deliberately not normalized.
 * These values must come from animation channels, never matrix decomposition. */
typedef struct {
    float translation[3], scale[3], rotation[4];
} EmPoseChannels;

#define EM_POSE_NODE_MAX 64

typedef struct {
    unsigned count;
    float remaining, reciprocal, fraction;
    int active;
    EmPoseChannels source[EM_POSE_NODE_MAX], target[EM_POSE_NODE_MAX];
    EmPoseChannels current[EM_POSE_NODE_MAX];
    float translation_velocity[EM_POSE_NODE_MAX][3];
    float scale_velocity[EM_POSE_NODE_MAX][3];
} EmPoseTransition;

/*001C8D50/001C86A0: freeze the current channels and seed a transition toward
 * the sampled target. Initialization leaves the visible source unchanged;
 * acquisition readiness is independent of this visual transition. */
int em_pose_transition_begin(EmPoseTransition *transition, const EmPoseChannels *source,
                             const EmPoseChannels *target, unsigned count, unsigned ticks);
/* One rate1 ordinary player callback. Returns1 during transition,0 at the
 * original target-channel reset. The host then owns normal target-clip time. */
int em_pose_transition_tick(EmPoseTransition *transition);
/* The original clock divides fractional rates into steps no larger than one.
 * This primitive consumes one such step. Returns -1 for invalid input. */
int em_pose_transition_step(EmPoseTransition *transition, float step);
/* Original001CA0A0 upper-clamped, sign-corrected unnormalized blend. */
void em_pose_quaternion_blend(float out[4], const float a[4], const float b[4], float t);
/*001CA1C0 followed by evaluator column scales. This is a node-local TRS;
 * original root adjustment, hierarchy and owner placement are separate. */
void em_pose_channels_matrix(float out[16], const EmPoseChannels *channels);

#endif
