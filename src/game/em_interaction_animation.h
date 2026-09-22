/* Verified current-bank pickup40..42/panel15C/lever47 timing. Ownership and
 * player takeover/release belong to the shared interaction runtime. */
#ifndef EM_INTERACTION_ANIMATION_H
#define EM_INTERACTION_ANIMATION_H

#include "em_model.h"
#include <stdint.h>

typedef struct {
    uint16_t requested_clip, current_clip;
    uint16_t duration, frame;
    uint32_t flags; /* original player+200, including terminal bit1000 */
    float remaining; /* actor+3C; counts down, never an elapsed cursor */
    uint8_t active, pending, transition;
    float requested_blend;
} EmInteractionAnimation;

void em_interaction_animation_clear(EmInteractionAnimation *animation);
/* Accept only independently exported40..42/47/15C, rate1 and blend0/1.
 * Return1 when accepted,0 for a missing/unsupported asset or argument.
 * Re-requesting the current clip preserves its cursor, as00183090 does. */
int em_interaction_animation_request(EmInteractionAnimation *animation,
    const EmModel *model, uint16_t clip, float rate, float blend);
/* One real player-stage callback. Return1 and write actor-local palettes,
 * 0 to preserve the previous pose on a blend1 commit, or−1 on host failure.
 * The host applies placement after1; it must not advance while status pauses
 * the ordinary player task. A terminal clip holds its final pose and flag. */
int em_interaction_animation_tick(EmInteractionAnimation *animation,
    const EmModel *model, float *local_palette);
/*−1 no active animation;0 waiting;1 original terminal flag.*/
int em_interaction_animation_done(const EmInteractionAnimation *animation);

#endif
