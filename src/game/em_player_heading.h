#ifndef EM_PLAYER_HEADING_H
#define EM_PLAYER_HEADING_H

#include <stdint.h>

/* 00174AC0 desired body yaw, using 0018C0D0's previous committed forward.
 * Pad axes are the original unsigned bytes. Call after the gait/deadzone
 * check. The horizontal forward must be nonzero. */
float em_player_stick_heading(uint8_t x, uint8_t y, float forward_x,
                             float forward_z);

#endif
