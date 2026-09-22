#ifndef EM_PICKUP_MOTION_H
#define EM_PICKUP_MOTION_H
#include "game/em_interaction_scan.h"
#include "game/em_script.h"

/* Original1B7F90/sub1, including SDK bearing and1B12B0 bounded angle step.
 * Updates the live player+C4 yaw.1 reached,0 turning,-1 invalid inputs. */
int em_pickup_turn(const EmInteractionMath *, const float player[3], float *yaw,
                    const float owner[3], float step);
/* Original1B8FC0/sub8 and18C6A0/18C4B0. Caller must publish eye/updated
 * target through its verified DD980 world-camera boundary on every call.
 * Does not update the owner's pose or invent an eye movement. */
int em_pickup_camera_settle(EmScript *, const float owner[3], float target[3]);
#endif
