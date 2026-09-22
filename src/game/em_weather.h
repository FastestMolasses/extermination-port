#ifndef EM_WEATHER_H
#define EM_WEATHER_H

#include <stdint.h>

/* 001E55F0's persistent weather actor. The six phase pairs are advanced
 * by its selected renderer, after the intensity update. */
typedef struct EmWeather {
    float phase[6];
    float drift[6];
    float intensity;
    float target;
    float rate;
    uint32_t seed;
    int32_t burst_wait;
    uint8_t state;
} EmWeather;

typedef uint32_t (*EmWeatherRandom)(void *context);

typedef struct EmWeatherFrame {
    float strength;
    uint8_t intensity;
    uint8_t draw;           /* 0: absent, 1: 001E67C0, 2: 001E5AC0 */
    uint8_t render_flags;   /* bit0: second renderer; bit1: AREA21 reversal */
    uint8_t released;
} EmWeatherFrame;

/* Original actor update, with render submission kept separate. State0 seeds
 * and falls through to state1 in this same call. Selector is the original
 * scripted-frame selector; transition bytes are D8106B8 and D28A9A0[0]. */
EmWeatherFrame em_weather_tick(EmWeather *weather, uint32_t area_flags,
                               unsigned area_entry, unsigned selector,
                               unsigned transition, unsigned task_transition,
                               EmWeatherRandom random, void *context);

#endif
