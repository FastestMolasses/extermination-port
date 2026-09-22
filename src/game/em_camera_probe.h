/* Original camera prepass0018D330 for styles other than2, and the
 * AREA11 branch of bounds helper0018D910. Queries are explicit boundaries. */
#ifndef EM_CAMERA_PROBE_H
#define EM_CAMERA_PROBE_H
#include "game/em_collision.h"

typedef struct {
    uint16_t flags;     /* camera+5A */
    uint8_t ground78;  /* camera+6D */
    float overhead_y; /* camera+60; retained when no overhead hit */
} EmCameraProbe;

/* ground_only requests0019B7D0's attr78-only grid query. Otherwise this
 * is0019A910 with mask6. Returns negative on an unavailable query. */
typedef int (*EmCameraProbeQuery)(void *context,const float from[3],
                  const float to[3],int ground_only,EmCollHit *hit);
int em_camera_interaction_probe(EmCameraProbe *probe,const float eye[3],
    const float position[3],const float hip[3],EmCameraProbeQuery query,void *context);
int em_camera_interaction_bounds11(float bounds[2],const float eye[3],
    const float position[3],float height_variant,EmCameraProbeQuery query,void *context);

#endif
