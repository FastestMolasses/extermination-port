#ifndef EM_AIM_FIRE_POSE_H
#define EM_AIM_FIRE_POSE_H
#include <stddef.h>
#include <stdint.h>

/* Original address views keep the pose's shared buffers and bone slots
 * coherent with the player stage. No private copy of canonical state. */
typedef struct {
    void *context;
    void *(*map)(void *, uint32_t address, size_t size, int write);
    int (*call)(void *, uint32_t address, const uint32_t args[3],
                const uint32_t floats[2], uint32_t *result);
    uint32_t fault;
} EmAimFirePose;

/* Original entry addresses: matrix dispatch, slot lookup, publication and
 * interpolation. Arguments and floating values retain their original bits.
 * A missing view/callee latches fault and stops at the first failure. */
int em_aim_fire_pose_run(EmAimFirePose *, uint32_t entry,
                        uint32_t a0, uint32_t a1, uint32_t a2,
                        uint32_t f12, uint32_t *result);
#endif
