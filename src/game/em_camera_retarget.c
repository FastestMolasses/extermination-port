#include "game/em_camera_retarget.h"
#include "game/em_effect_color.h"
#include "game/em_ee_float.h"

#include <math.h>

/* 0018CBD0's scalar arithmetic is COP1 (em_ee_float.h, the measured EE
 * model); the delta is the SDK VU0 vector subtract 001028D0 (truncated per
 * lane); the square root and |distance| are the SDK calls 0011E748 and
 * 0011DF78. */
void em_camera_retarget_seed(const float position[3],
                            const float rotated_offset[3], float distance,
                            float preset_distance, float eye[3], float target[3])
{
    float delta[3];
    for (int i=0;i<3;++i) {
        target[i]=position[i];
        eye[i]=em_ee_add(rotated_offset[i],target[i]);
        delta[i]=em_effect_float32((double)target[i]-eye[i]);
    }
    /* Original MULA/MADD retains its accumulator operation order. */
    float square=em_ee_madd(em_ee_mula(delta[0],delta[0]),delta[2],delta[2]);
    float compression=em_ee_sub(sqrtf(square),fabsf(distance));
    float threshold, target_height, eye_height;
    if (preset_distance==-46.8f) {
        threshold=-20.0f;target_height=6.0f;eye_height=2.0f;
    } else {
        threshold=-10.0f;target_height=2.0f;eye_height=6.0f;
    }

    float falloff=0.0f;
    if (compression<threshold) {
        float amount=em_ee_sub(threshold,compression);
        falloff=em_ee_add(threshold,amount);
        if (falloff>-7.0f) falloff=-7.0f;
    }
    /* ADDA (+A4, height), MADD (0.3 x falloff), then + 11. */
    target[1]=em_ee_add(11.0f,em_ee_madd(em_ee_adda(position[1],target_height),
                                         0.3f,falloff));

    float eye_base;
    if (compression<threshold) {
        float amount=em_ee_sub(compression,threshold);
        if (threshold==-20.0f)
            amount=em_ee_mul(0.5f,amount);
        else if (amount<-10.0f) amount=-10.0f;
        float height=em_ee_sub(eye_height,amount);
        eye_base=em_ee_add(position[1],height);
    } else eye_base=em_ee_add(eye_height,position[1]);
    eye[1]=em_ee_add(11.0f,em_ee_add(target_height,eye_base));
}
