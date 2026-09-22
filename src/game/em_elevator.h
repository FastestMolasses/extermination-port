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
    int (*tick_script)(void *context); /*0 waiting,1 complete,negative host fault*/
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

typedef struct {
    uint8_t phase;
    int32_t ticks;
    float rate;
} EmElevatorMotion;

/* Original00828050, separate from owner completion. These are three
 * distinct Y values: elevator actor+B4, player ground/actor origin+A4
 * (global00810354), and camera target Y.
 * The pose callback runs after all three translations, before tick count.
 * Returns0 waiting,1 complete,-1 missing binding. */
int em_elevator_motion_tick(EmElevatorMotion *motion, int lower,
                             float *owner_y, float *player_y,
                             float *camera_target_y,
                             const EmElevatorHooks *hooks);

#endif
