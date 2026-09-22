/* Active-state translation of AREA11 owner00827B10. Model creation and
 * child allocation belong to the scene; script commands stay in EmScript.
 * This controller is not yet bound to the legacy examine interaction. */
#ifndef EM_ELEVATOR_H
#define EM_ELEVATOR_H

#include <stdint.h>

enum {
    EM_ELEVATOR_POWERED_SCRIPT = 0x0082A750,
    EM_ELEVATOR_REFUSAL_SCRIPT = 0x0082A990
};

typedef struct {
    uint8_t phase, armed, lower;
    int16_t sound_timer, indicator_level;
    float height;
    float script_heights[3]; /* move-to, first camera, second camera */
} EmElevator;

typedef struct {
    void *context;
    void (*start_script)(void *context, uint32_t address);
    int (*tick_script)(void *context);
    void (*sound)(void *context, unsigned cue, float radius);
    void (*rebuild_pose)(void *context, float height);
    void (*copy_indicator_pose)(void *context);
    void (*update_actor)(void *context);
} EmElevatorHooks;

void em_elevator_init(EmElevator *owner, int lower);
/* One original state1 callback, preserving start-vs-tick and completion
 * ordering. The power argument is the current area11 bit7, not battery
 * possession. Returns -1 when any required host binding is missing. */
int em_elevator_tick(EmElevator *owner, int powered,
                     const EmElevatorHooks *hooks);

#endif
