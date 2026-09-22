#include "game/em_camera_retarget.h"
#include "game/em_effect_color.h"

void em_camera_retarget_seed(const float position[3],
                            const float rotated_offset[3], float distance,
                            float preset_distance, float eye[3], float target[3])
{
    float delta[3];
    for (int i=0;i<3;++i) {
        target[i]=position[i];
        eye[i]=em_effect_float32((double)rotated_offset[i]+target[i]);
        delta[i]=em_effect_float32((double)target[i]-eye[i]);
    }
    /* Original MULA/MADD retains its accumulator operation order. */
    float square=em_effect_float32((double)delta[0]*delta[0]);
    square=em_effect_float32((double)square+(double)delta[2]*delta[2]);
    float compression=em_effect_float32((double)sqrtf(square)-fabsf(distance));
    float threshold, target_height, eye_height;
    if (preset_distance==-46.8f) {
        threshold=-20.0f;target_height=6.0f;eye_height=2.0f;
    } else {
        threshold=-10.0f;target_height=2.0f;eye_height=6.0f;
    }

    float falloff=0.0f;
    if (compression<threshold) {
        float amount=em_effect_float32((double)threshold-compression);
        falloff=em_effect_float32((double)threshold+amount);
        if (falloff>-7.0f) falloff=-7.0f;
    }
    float target_base=em_effect_float32((double)position[1]+target_height);
    target[1]=em_effect_float32(11.0+em_effect_float32(
                    (double)target_base+(double)0.3f*falloff));

    float eye_base;
    if (compression<threshold) {
        float amount=em_effect_float32((double)compression-threshold);
        if (threshold==-20.0f)
            amount=em_effect_float32(0.5*(double)amount);
        else if (amount<-10.0f) amount=-10.0f;
        float height=em_effect_float32((double)eye_height-amount);
        eye_base=em_effect_float32((double)position[1]+height);
    } else eye_base=em_effect_float32((double)eye_height+position[1]);
    eye[1]=em_effect_float32(11.0+em_effect_float32((double)target_height+eye_base));
}
