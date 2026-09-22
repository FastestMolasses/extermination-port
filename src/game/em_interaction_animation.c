#include "game/em_interaction_animation.h"
#include <string.h>

static unsigned original_duration(uint16_t clip)
{
    /* Both original headers terminate with next=-2 and have no event table.
     * Their source-only exporters verify these banks against captured RAM. */
    return clip == 0x47 ? 200 : clip == 0x15C ? 121 : 0;
}

static int verified_clip(const EmModel *model, uint16_t clip)
{
    unsigned duration = original_duration(clip);
    if (!model || !model->clips || !model->palette || !duration ||
        model->bone_count != 22) return -1;
    int index = em_model_clip_index(model, clip);
    if (index < 0 || model->clips[index].frame_count != duration ||
        model->clips[index].fps != 60.0f) return -1;
    return index;
}

void em_interaction_animation_clear(EmInteractionAnimation *animation)
{
    if (animation) memset(animation, 0, sizeof *animation);
}

int em_interaction_animation_request(EmInteractionAnimation *animation,
    const EmModel *model, uint16_t clip, float rate, float blend)
{
    if (!animation || rate != 1.0f || (blend != 0.0f && blend != 1.0f) ||
        verified_clip(model, clip) < 0) return 0;
    animation->requested_clip = clip;
    animation->requested_blend = blend;
    animation->active = 1;
    animation->pending = 1;
    return 1;
}

int em_interaction_animation_tick(EmInteractionAnimation *animation,
    const EmModel *model, float *local_palette)
{
    if (!animation || !animation->active || !local_palette) return -1;
    if (animation->pending && animation->requested_clip != animation->current_clip) {
        int index = verified_clip(model, animation->requested_clip);
        if (index < 0) return -1;
        /*00183090 commits first; it returns0, suppressing the ordinary
         * advance call. Init with blend0 internally resolves its one-tick
         * transition immediately. Blend1 resolves it on the next call. */
        animation->current_clip = animation->requested_clip;
        animation->duration = (uint16_t)original_duration(animation->current_clip);
        animation->frame = 0;
        animation->flags = 0;
        animation->pending = 0;
        animation->transition = animation->requested_blend != 0.0f;
        animation->remaining = animation->transition ? 1.0f : animation->duration;
        if (animation->transition) return 0;
    } else {
        animation->pending = 0;
        if (animation->transition) {
            /*001C64F0 clears the clip high bit, restores header length,
             * then seeds channels. It does not consume source frame1. */
            animation->transition = 0;
            animation->remaining = animation->duration;
        } else if (animation->remaining <= 1.0f) {
            animation->flags = 0x1000;
        } else {
            animation->remaining -= 1.0f;
            ++animation->frame;
            animation->flags = 0;
        }
    }
    int index = verified_clip(model, animation->current_clip);
    if (index < 0 || animation->frame >= animation->duration) return -1;
    em_model_palette_at(model, (uint32_t)index, animation->frame, local_palette);
    return 1;
}

int em_interaction_animation_done(const EmInteractionAnimation *animation)
{
    return !animation || !animation->active ? -1 : !!(animation->flags & 0x1000);
}
