/* em_effect_float32: the EE-truncating float step several interim modules
 * share. */
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

#endif
