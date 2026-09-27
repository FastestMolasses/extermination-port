#include "game/em_weather.h"
#include "game/em_ee_float.h"

#include <math.h>
#include <string.h>

/* 001E55F0's float arithmetic is all COP1 (cvt.s.w, div.s, mul.s, add.s,
 * sub.s): em_ee_float.h, the measured EE model (docs/EE_FLOAT_MODEL.md;
 * DIV.S rounds to nearest). */
static float random_fraction(EmWeatherRandom random, void *context)
{
    float value = em_ee_cvt_s_w((int32_t)(random(context) & 0x7fffffffU));
    return em_ee_div(value, 2147483648.0f);
}

static float random_range(EmWeatherRandom random, void *context,
                          float base, float width)
{
    float offset = em_ee_mul(width, random_fraction(random, context));
    return em_ee_add(base, offset);
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

    float difference = em_ee_sub(weather->target, weather->intensity);
    float step = em_ee_mul(difference, weather->rate);
    weather->intensity = em_ee_add(weather->intensity, step);
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
    frame.strength = em_ee_div(weather->intensity, 127.0f);
    if (weather->intensity > 0.0f)
        frame.draw = (frame.render_flags & 1) ? 2 : 1;
    if (transition == 2 && task_transition == 2)
        weather->state = 3;
    return frame;
}
