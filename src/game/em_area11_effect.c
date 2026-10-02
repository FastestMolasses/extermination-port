#include "game/em_area11_effect.h"
#include "game/em_ee_float.h"
#include "game/em_effect_color.h"

#include <stddef.h>

void em_area11_effect_tick(EmArea11Effect *effect,
                           EmArea11EffectRandom random,
                           EmArea11EffectCallback callback, void *context)
{
    if (!effect || !callback) return;
    switch (effect->state) {
    case 0:
        if (!random) return;
        callback(context, EM_AREA11_EFFECT_MATRIX, effect);
        /* +0x30 = *(self + 0x14) + 0x1F0 (the half-extent block 001A8660
         * reads as the flame's radius and height), +0x34 = 0x823580. */
        effect->w30 = effect->record + 0x1F0u;
        effect->w34 = EM_AREA11_EFFECT_CONTACT;
        effect->flags = 1;
        effect->half_extent[0] = 7.0f;
        effect->half_extent[1] = 15.0f;
        effect->half_extent[2] = 7.0f;
        effect->contact_cooldown = 0;
        effect->sound_handle = -1;
        effect->phase = 0.0f;
        /* Original CVT.S.W truncates the signed31-bit random result;
         * DIV.S then scales by the exactly representable power of two. */
        effect->seed = em_ee_cvt_s_w((int32_t)random(context));
        effect->seed = em_ee_div(effect->seed, 2147483648.0f);
        effect->state = 1;
        /* State0 falls through into its first draw and sound service. */
        /* fall through */
    case 1:
        callback(context, EM_AREA11_EFFECT_DRAW, effect);
        /* 008235F0's scalar arithmetic is COP1 (em_ee_float.h). */
        effect->phase = em_ee_add(effect->phase, 0.025f);
        if (effect->phase >= 2.0f)
            effect->phase = em_ee_sub(effect->phase, 1.0f);
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
                             const EmArea11EffectContactWorkers *workers,
                             uint8_t *target_reaction, uint32_t *fault_address)
{
    if (fault_address) *fault_address = 0;
    if (!effect || !target_reaction || !workers) {
        if (fault_address) *fault_address = EM_AREA11_EFFECT_CONTACT;
        return -1;
    }
    if (target_flags & 2)
        return 0;
    int32_t blocked = 0;
    if (!workers->w_0021BB00 || workers->w_0021BB00(workers->ctx, &blocked) < 0) {
        if (fault_address) *fault_address = 0x0021BB00u;
        return -1;
    }
    if (blocked != 0)
        return 0;
    if (!workers->w_001EFE00 || workers->w_001EFE00(workers->ctx, EM_AREA11_EFFECT_CONTACT_EFFECT) < 0) {
        if (fault_address) *fault_address = 0x001EFE00u;
        return -1;
    }
    *target_reaction = 0xC;
    effect->contact_cooldown = 60;
    return 0;
}
