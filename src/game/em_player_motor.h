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

#endif
