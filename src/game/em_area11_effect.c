#include "game/em_area11_effect.h"
#include "game/em_effect_color.h"

void em_area11_effect_tick(EmArea11Effect *effect,
                           EmArea11EffectRandom random,
                           EmArea11EffectCallback callback, void *context)
{
    if (!effect || !callback) return;
    switch (effect->state) {
    case 0:
        if (!random) return;
        callback(context, EM_AREA11_EFFECT_MATRIX, effect);
        effect->flags = 1;
        effect->half_extent[0] = 7.0f;
        effect->half_extent[1] = 15.0f;
        effect->half_extent[2] = 7.0f;
        effect->contact_cooldown = 0;
        effect->sound_handle = -1;
        effect->phase = 0.0f;
        /* Original CVT.S.W truncates the signed31-bit random result;
         * DIV.S then scales by the exactly representable power of two. */
        effect->seed = em_effect_float32((double)(int32_t)random(context));
        effect->seed = (float)((double)effect->seed / 2147483648.0);
        effect->state = 1;
        /* State0 falls through into its first draw and sound service. */
        /* fall through */
    case 1:
        callback(context, EM_AREA11_EFFECT_DRAW, effect);
        effect->phase = em_effect_float32((double)effect->phase + 0.025f);
        if (effect->phase >= 2.0f)
            effect->phase = em_effect_float32((double)effect->phase - 1.0f);
        callback(context, EM_AREA11_EFFECT_SOUND, effect);
        if (effect->contact_cooldown == 0) {
            effect->flags = 1;
        } else {
            effect->contact_cooldown = (int32_t)((uint32_t)effect->contact_cooldown - 1);
            effect->flags = 2;
        }
        callback(context, EM_AREA11_EFFECT_PUBLISH, effect);
        break;
    case 2:
    case 3:
        callback(context, EM_AREA11_EFFECT_STOP_SOUND, effect);
        callback(context, EM_AREA11_EFFECT_FREE, effect);
        break;
    default:
        break;
    }
}

int em_area11_effect_contact(EmArea11Effect *effect, uint8_t target_flags,
                             int blocked, uint8_t *target_reaction)
{
    if (!effect || !target_reaction || (target_flags & 2) || blocked)
        return 0;
    *target_reaction = 12;
    effect->contact_cooldown = 60;
    return 1;
}
