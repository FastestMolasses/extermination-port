#ifndef EM_PLAYER_MOTOR_H
#define EM_PLAYER_MOTOR_H

#include <stdint.h>

/* The scalar state consumed by original 0017BC40. Speeds are units/tick;
 * rate and blend are the actor's +204/+208 animation outputs. */
typedef struct EmPlayerMotor {
    float speed, target, rate, blend;
    uint8_t mode, substate, tier, gait, obstruction;
} EmPlayerMotor;

void em_player_motor_tick(EmPlayerMotor *motor);

/* Non-looping run-stop clip: request with a six-tick blend at frame
 * (length-6), then return through idle's twelve-tick blend. Phase3 is
 * the callback between observing the end flag and idle initialization. */
typedef struct EmPlayerStop {
    unsigned phase, blend_left, frame, last_frame;
} EmPlayerStop;
void em_player_stop_begin(EmPlayerStop *stop, unsigned clip_frames);
void em_player_stop_tick(EmPlayerStop *stop);

/* 0017C440 requests the previous gait tier during a run-stop interruption.
 * Phase2 is the callback that clears the blend flag; normal scalar updates
 * resume on the following callback. */
typedef struct EmPlayerReentry {
    unsigned phase, blend_left, frame;
} EmPlayerReentry;
int em_player_reentry_begin(EmPlayerReentry *reentry, EmPlayerMotor *motor,
                           unsigned gait, unsigned clip_frames);
void em_player_reentry_tick(EmPlayerReentry *reentry, EmPlayerMotor *motor);

#endif
