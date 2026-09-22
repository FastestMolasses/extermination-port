#include "game/em_pickup_motion.h"
#include "game/em_effect_color.h"
#include <math.h>

static float add(float a, float b) { return em_effect_float32((double)a+b); }
static float sub(float a, float b) { return em_effect_float32((double)a-b); }
static float divide(float a, float b) { return (float)((double)a/b); }
static float wrap(float value)
{
    while (value > 3.1415927410125732421875f) value = sub(value,6.283185482025146484375f);
    while (value <= -3.1415927410125732421875f) value = add(value,6.283185482025146484375f);
    return value;
}

int em_pickup_turn(const EmInteractionMath *math, const float player[3], float *yaw,
                    const float owner[3], float step)
{
    if (!math || !player || !yaw || !owner || !isfinite(*yaw) || !isfinite(step) || step < 0)
        return -1;
    for (unsigned i=0; i<3; ++i)
        if (!isfinite(player[i]) || !isfinite(owner[i])) return -1;
    float goal = wrap(em_interaction_sdk_atan2(math, sub(owner[0],player[0]),
                                                    sub(owner[2],player[2])));
    float difference = wrap(sub(goal,*yaw));
    if (difference == 0) *yaw = wrap(*yaw);
    else if (fabsf(difference) <= step) *yaw = goal;
    else *yaw = wrap(difference > 0 ? add(*yaw,step) : sub(*yaw,step));
    return *yaw == goal;
}

static int settle_axis(float owner, float *target, float divisor, float maximum)
{
    float difference = sub(owner,*target);
    float magnitude = fabsf(difference);
    if (magnitude <= 1.0f) {
        *target = add(*target,divide(difference,4.0f));
        return 1;
    }
    float step = divide(magnitude,divisor);
    if (maximum <= step) step = maximum;
    if (difference < 0) step = -step;
    *target = add(*target,step);
    return 0;
}

int em_pickup_camera_settle(EmScript *script, const float owner[3], float target[3])
{
    if (!script || !owner || !target) return -1;
    for (unsigned i=0; i<3; ++i)
        if (!isfinite(owner[i]) || !isfinite(target[i])) return -1;
    if (!(uint8_t)script->phase) {
        script->phase = (script->phase & ~255) | 1;
        return 0;
    }
    if ((uint8_t)script->phase != 1) return 0;
    int done = settle_axis(owner[0],target,6.0f,0.4000000059604644775390625f);
    done |= settle_axis(owner[2],target+2,6.0f,0.4000000059604644775390625f) << 1;
    done |= settle_axis(owner[1],target+1,8.0f,0.300000011920928955078125f) << 2;
    return done == 7;
}
