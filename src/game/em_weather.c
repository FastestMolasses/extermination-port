#include "game/em_weather.h"

#include <math.h>
#include <string.h>

/* Finite binary32 arithmetic with the EE's truncation after each operation.
 * A double represents binary32 sums/products exactly before this rounding. */
static float weather_float(double value)
{
    float result = (float)value;
    if (fabs((double)result) > fabs(value)) {
        uint32_t bits;
        memcpy(&bits, &result, sizeof bits);
        --bits;
        memcpy(&result, &bits, sizeof result);
    }
    return result;
}

static float random_fraction(EmWeatherRandom random, void *context)
{
    float value = weather_float((double)(random(context) & 0x7fffffffU));
    return weather_float((double)value / 2147483648.0);
}

static float random_range(EmWeatherRandom random, void *context,
                          float base, float width)
{
    float offset = weather_float((double)width * random_fraction(random, context));
    return weather_float((double)base + offset);
}

EmWeatherFrame em_weather_tick(EmWeather *weather, uint32_t area_flags,
                               unsigned area_entry, unsigned selector,
                               unsigned transition, unsigned task_transition,
                               EmWeatherRandom random, void *context)
{
    EmWeatherFrame frame = {0};
    if (weather->state == 0) {
        weather->seed = random(context);
        for (unsigned i = 0; i < 6; ++i) {
            weather->phase[i] = random_range(random, context, 1.0f, 1.0f);
            weather->drift[i] = random_range(random, context, 0.0f, 0.2f);
        }
        weather->intensity = random_range(random, context, 48.0f, 16.0f);
        weather->target = random_range(random, context, 48.0f, 16.0f);
        weather->rate = 0.003f;
        weather->burst_wait = (int32_t)(random(context) % 10U) + 10;
        weather->state = 1;
    }
    if (weather->state == 2 || weather->state == 3) {
        frame.released = 1;
        return frame;
    }
    if (weather->state != 1 || !(area_flags & 0x0e000070U))
        return frame;

    float difference = weather_float((double)weather->target - weather->intensity);
    float step = weather_float((double)difference * weather->rate);
    weather->intensity = weather_float((double)weather->intensity + step);
    if (fabsf(difference) < 3.0f) {
        if (area_flags & 0x02000010U)
            weather->target = random_range(random, context, 60.0f, 15.0f);
        if (area_flags & 0x04000020U)
            weather->target = random_range(random, context, 65.0f, 20.0f);
        if (area_flags & 0x08000040U) {
            weather->target = random_range(random, context, 65.0f, 30.0f);
            weather->rate = 0.003f;
            weather->burst_wait = (int32_t)((uint32_t)weather->burst_wait - 1U);
            if (weather->burst_wait < 0) {
                weather->burst_wait = (int32_t)(random(context) % 10U) + 10;
                weather->target = 127.0f;
                weather->rate = 0.05f;
            }
        }
    }
    if (selector && weather->target > 90.0f)
        weather->target = 90.0f;

    frame.render_flags = (area_flags & 0x0c000060U) ? 1 : 0;
    if (area_entry == 0x1500)
        frame.render_flags |= 2;
    frame.intensity = (uint8_t)(int32_t)weather->intensity;
    frame.strength = weather_float((double)weather->intensity / 127.0f);
    if (weather->intensity > 0.0f)
        frame.draw = (frame.render_flags & 1) ? 2 : 1;
    if (transition == 2 && task_transition == 2)
        weather->state = 3;
    return frame;
}
