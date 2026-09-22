/* Original001DD980/001DD950 camera-dependent render-context publication. */
#ifndef EM_INTERACTION_PROJECTION_H
#define EM_INTERACTION_PROJECTION_H

typedef struct {
    float center[4]; /* draw context+2450 */
    float scale;     /* +2460: 16777215/(2+1.02*distance) */
    float distance;  /* +2464 */
} EmInteractionProjection;

/* This operation does not copy desired camera vectors or run a chase.
 * Input is the actual eye/target. Returns0 for invalid native inputs. */
int em_interaction_projection_publish(EmInteractionProjection *projection,
    const float eye[3], const float target[3]);

#endif
