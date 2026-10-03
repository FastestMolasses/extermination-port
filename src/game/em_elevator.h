/* Active-state translation of AREA11 owner 00827B10 (state 1; ground
 * truth: the decomp's byte-identical func_overlay_AREA11_00827AD0.c) and
 * its script callback 00828050 (NEARMISS decomp C, logic equal). Model
 * creation and child allocation belong to the scene (em_area11_bindings.c
 * tick_terminal); script commands stay in EmScript. */
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
    /* 001A2370(self, +0xD0) after the completion's 001C6380 (0x827E54):
     * re-transform the owner's collision cell by its new matrix. The carry
     * 00828050 rebuilds only the matrix (0x82812C), so the cell keeps the
     * old floor's transform during the ride. */
    void (*retransform)(void *context);
    /* The power flag, D_00810841[D_00810700] & (1 << +0x2E), read where
     * 00827B10 reads it (decomp func_overlay_AREA11_00827AD0.c): the
     * phase-0 script choice, the phase-1 completion and the +0x28 ramp
     * after the +0x4C call; a callee between the reads may change it.
     * Nonzero when set. Required by em_elevator_tick only. */
    int (*powered)(void *context);
} EmElevatorHooks;

void em_elevator_init(EmElevator *owner, int lower);
/* One original state1 callback, preserving start-vs-tick and completion
 * ordering. The power flag is hooks->powered (the current area11 bit 7,
 * not battery possession), read at the original's three points. Returns
 * -1 when any required host binding is missing. */
int em_elevator_tick(EmElevator *owner, const EmElevatorHooks *hooks);

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
