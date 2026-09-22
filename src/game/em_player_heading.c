#include "game/em_player_heading.h"

#include <math.h>

#pragma STDC FP_CONTRACT OFF

float em_player_stick_heading(uint8_t x, uint8_t y, float forward_x,
                             float forward_z)
{
    const float pi=3.1415927f;
    const float two_pi=6.2831855f;
    /* 00174B7C..00174C64: the SDK cosine calls are 0011DE90;
     * the atan2 call receives (-cosY,cosX) in f12/f13. */
    float cos_x=cosf(pi*((float)x/256.0f));
    float cos_y=cosf(pi*((float)y/256.0f));
    float stick=atan2f(-cos_y,cos_x);
    /* 0018C440..0018C470 writes D008106A0 from this camera vector.
     * 00178C84..00178CC4 moves X with sin(yaw), Z with cos(yaw).
     * The original and native body yaw therefore share one convention. */
    float camera_heading=atan2f(-forward_z,forward_x);
    float desired=(pi+stick)+camera_heading;
    while (desired>pi) desired-=two_pi;
    while (desired<=-pi) desired+=two_pi;
    return desired;
}
