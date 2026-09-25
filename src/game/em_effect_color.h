/* em_effect_float32 (the EE-truncating float step several interim modules
 * share) and the 001D8C30 mode-1 GS conversion of the indicator children's
 * draw colour. 001F54E0 itself is em_effect_kinds_001F54E0. */
#ifndef EM_EFFECT_COLOR_H
#define EM_EFFECT_COLOR_H
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "game/em_ee_float.h"

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

/* The GS colour of an indicator child's draw (its +0x4C method 001CACB0):
 * the child's +0x80 vector after 001F54E0 (em_effect_kinds_001F54E0, the one
 * translation of that routine) goes through 001D8C30 mode 1, which stores
 * bias + (128 + c.xyz) with bias = 8388608.0 (both adds on the EE, so the
 * low mantissa bits are the GS integer colour, 0..255). The port's additive
 * draw takes that integer as a modulate factor of value / 128. */
static inline void em_effect_color_gs(const float c80[4], float tint[4])
{
    for (int channel=0;channel<3;++channel) {
        uint32_t value=em_ee_add_bits(0x43000000u,em_ee_bits(c80[channel]));
        uint32_t biased=em_ee_add_bits(0x4B000000u,value);
        tint[channel]=(float)(biased&0x7FFFFFu)/128.0f;
    }
    tint[3]=1.0f;
}

#endif
