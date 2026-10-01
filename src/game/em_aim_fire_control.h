#ifndef EM_AIM_FIRE_CONTROL_H
#define EM_AIM_FIRE_CONTROL_H
#include <stddef.h>
#include <stdint.h>

/* Call arguments use original addresses and float bit patterns. Only the
 * original callee's actual arguments are meaningful; unused slots are zero.
 * Returns are distinct from callback status (zero succeeds, negative faults). */
typedef struct {
    uint32_t a[4], f[4];
    uint32_t v0, f0;
} EmAimFireControlCall;
typedef struct {
    void *context;
    void *(*map)(void *context, uint32_t address, size_t size, int writing);
    int (*call)(void *context, uint32_t entry, EmAimFireControlCall *frame);
    /* Private, nonaliasing 16-byte temporary for 0017AF70's target vector.
     * Its virtual address must be mapped and visible to the call callback. */
    uint32_t temporary;
} EmAimFireControl;

/* Original workers 0016F530/5D0/600, 00172860, 0017A8B0/970/AD0/BA0/F70,
 * 0017B300/420, 0017A800 and 001B5DC0. 0017A800 takes the two vector
 * addresses in actor/argument and returns float bits through result;
 * 001B5DC0 takes the stick byte in actor. Otherwise result is set only for
 * originals that return an integer.
 * All data comes from the caller's canonical storage; no original tables
 * or runtime memory are embedded in this module. */
int em_aim_fire_control_run(const EmAimFireControl *bus, uint32_t entry,
                            uint32_t actor, int32_t argument,
                            uint32_t float_argument, int32_t *result);
#endif
