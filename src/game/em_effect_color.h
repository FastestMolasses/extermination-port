/* Original 001F54E0 effect color and 001D8C30 mode-1 GS conversion.
 * Shared by pickup and placed-prop indicators. No global RNG state is
 * hidden here: callers supply one original 31-bit SDK RNG value. */
#ifndef EM_EFFECT_COLOR_H
#define EM_EFFECT_COLOR_H
#include <math.h>
#include <stdint.h>
#include <string.h>

static inline float em_effect_float32(double value)
{
    /* EE single-precision operations truncate toward zero. Native float
     * rounding otherwise differs in four of the six captured child RGBs.
     * A double holds these bounded float products exactly; if conversion
     * rounded away from zero, step its binary32 magnitude down one ULP. */
    float result=(float)value;
    if (fabs((double)result)>fabs(value)) {
        uint32_t bits;
        memcpy(&bits,&result,sizeof bits);
        --bits;
        memcpy(&result,&bits,sizeof result);
    }
    return result;
}

static inline void em_effect_delta(uint32_t random_value, const float color[4],
                                float delta[3])
{
#pragma STDC FP_CONTRACT OFF
    /* 001F54E0: preserve each EE operation and its float truncation. */
    float random=em_effect_float32((double)em_effect_float32(random_value)*0x1p-31);
    float centered=em_effect_float32(-127.0+em_effect_float32(254.0*random));
    float brightness=em_effect_float32(127.0+
                                  em_effect_float32((double)color[3]*centered));
    for (int channel=0;channel<3;++channel) {
        float value=em_effect_float32(em_effect_float32((double)color[channel]*brightness)-127.0);
        if (value < -127.0f) value=-127.0f;
        if (value > 127.0f) value=127.0f;
        delta[channel]=value;
    }
}

static inline void em_effect_color(uint32_t random_value, const float color[4],
                                float tint[4])
{
    float delta[3];
    em_effect_delta(random_value,color,delta);
    for (int channel=0;channel<3;++channel) {
        /* 001D8C30 mode1 adds 128, then uses the float mantissa as
         * GS integer color. The positive bias truncates on EE. */
        float value=em_effect_float32(128.0+(double)delta[channel]);
        tint[channel]=(float)(int)value/128.0f;
    }
    tint[3]=1.0f;
}

#endif
