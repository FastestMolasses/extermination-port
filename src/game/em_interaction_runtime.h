/* The shared owner token of the AREA11 interactions and the scripted frame
 * core they open. One instance belongs to one game world; the panel, the
 * terminal and the item adapters pass their own stable owner token, and the
 * script owners (em_area11_script_host) theirs.
 *
 * Every takeover is the player stage's own (audit 1b item 8): a claimed
 * owner's frame is admitted by 0015B130's prelude (00182B30, +4 = 4,
 * 00174A50(8.0), 00182D70), each stage runs 0015BA50's +4 = 4 path (00183090
 * on the record's +1F2 / +1F4 / +1F8, then 001C64F0 into +200) and 0015B530,
 * whose 00182DF0 releases the player once 0x70003B8D clears. The owners'
 * scripts request clips as 001B9A00 does: sub 0 writes the record's +1F2,
 * +1F8 and +1F4, sub 3 waits on +200 & 0x1000. The runtime holds only the
 * token and the frame; it never advances or releases the player itself. */
#ifndef EM_INTERACTION_RUNTIME_H
#define EM_INTERACTION_RUNTIME_H

#include "game/em_interaction_frame.h"

typedef struct {
    void *context;
    EmInteractionFrameEmit frame_event;
    int (*camera_retarget)(void *); /* Current adapter's verified command. */
    /* The live player record D_008102B0 (0x320 bytes) that 001B9A00 writes
     * and reads, or NULL when it is not bound (the request faults). */
    uint8_t *(*player_record)(void *);
} EmInteractionRuntimeHooks;

typedef struct {
    const void *owner;
    EmInteractionFrame *frame;
    EmInteractionRuntimeHooks hooks;
    int failed;
} EmInteractionRuntime;

int em_interaction_runtime_init(EmInteractionRuntime *runtime, EmInteractionFrame *frame,
                                const EmInteractionRuntimeHooks *hooks);
/* After original single-winner use arbitration/alignment, claim the owner
 * and publish selector 3 (00184BA0's 3B8D = 3). A competing token cannot
 * replace a live owner. The next player stage's 0015B130 admits the player
 * for it. */
int em_interaction_runtime_claim(EmInteractionRuntime *runtime, const void *owner);
/* An owner whose own script opened the scripted frame (op07 already wrote
 * the selector, e.g. the truck trigger 008251E0's 0x8292C0): the player
 * takeover the next player stage performs serves that owner. Requires a
 * nonzero selector and a free player; writes nothing to the frame. 1
 * claimed, 0 refused. */
int em_interaction_runtime_claim_scripted(EmInteractionRuntime *runtime, const void *owner);
/* The player stage's 00182DF0 released the owner's player (3B8F = 0,
 * +4 = 1): the token ends. 1, or 0 when no owner holds it. */
int em_interaction_runtime_release(EmInteractionRuntime *runtime);
int em_interaction_runtime_owns(const EmInteractionRuntime *runtime, const void *owner);
const void *em_interaction_runtime_owner(const EmInteractionRuntime *runtime);
int em_interaction_runtime_camera_owned(const EmInteractionRuntime *runtime);

EmScriptCommandResult em_interaction_runtime_frame(EmInteractionRuntime *runtime, const void *owner,
                                                   EmScript *script, const unsigned char *record);
int em_interaction_runtime_camera_retarget(EmInteractionRuntime *runtime, const void *owner);
/* 001B9A00 sub 0 for the owner's script: the record's +1F2 = clip, +1F8 =
 * blend (the script record's +0xC), +1F4 = 1.0 (`rate` must be 1.0, the
 * constant sub 0 writes). The player must be taken (3B8F != 0). 1, or 0
 * (and the runtime faults). */
int em_interaction_runtime_animation_start(EmInteractionRuntime *runtime, const void *owner,
                                           uint16_t clip, float rate, float blend);
/* 001B9A00 sub 3: 1 when the record's +200 holds 0x1000 (001C64F0's end
 * flag, which 0015BA50 stores), 0 while it does not, -1 when the owner does
 * not hold the token or the record is not bound. */
int em_interaction_runtime_animation_done(EmInteractionRuntime *runtime, const void *owner);

#endif
