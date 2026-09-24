/* em_pose_chain.h - the original clip clock with its follow-on chaining, over
 * decoded channels (docs/PLAYER_CLIPS.md).
 *
 * Translations of the original routines, in the decoded-channel world of
 * em_pose_bank / em_pose_transition (EmPoseBank, EmPosePlayback,
 * EmPoseTransition), not models of them:
 *   001C6120 / 001C61D0  clip lookup (id & 0x7FFF) and its frame count (+2)
 *   001C67E0             anim_clip_init: +2C = clip | 0x8000, node 0 +8E =
 *                        00128250(frame); zero blend resolves at once, a
 *                        nonzero blend seeds a transition toward the clip at
 *                        float_to_int(frame)
 *   001C64F0             anim_advance_time, including the chain step: at the
 *                        end of a clip whose header +4 is a clip id, +2C =
 *                        that id | 0x8000, flags |= 0x4000, +3C = the ending
 *                        clip's +6 halfword, and a +3C-long transition toward
 *                        the follow-on clip's frame 0 starts; when that
 *                        transition ends, the follow-on clip restarts at the
 *                        node 0 +8E frame left by the last anim_clip_init
 *                        (the chain never writes +8E)
 *   001749A0             request: same-clip gate on +20C, then anim_clip_init
 *   001749F0             anim_clip_arbiter: anim_clip_init, then +20C compare
 *
 * The raw-record translation of the same routines is em_pose_host_workers.c
 * (001749A0 / 001749F0 / 001C61D0 / 001C67E0) with em_player_stage_workers.c
 * em_player_stage_anim_advance (001C64F0); this module keeps the decoded
 * channels em_player_pose publishes, so both views can be compared field by
 * field (tools/test_pose_chain_reference.py).
 *
 * State. EmPoseChain holds exactly the record fields these routines own:
 * +2C (clip word, 0x8000 = transition), +20C (requested id), +3C (the clock,
 * float bits), node 0 +8E (the hold frame) and the node channels (playback
 * cursors, or the transition while +2C has 0x8000).
 *
 * Arithmetic. Clock arithmetic, compares and conversions use em_ee_float.h;
 * float_to_int (001281C0) and 00128250 are em_stream_lanes_001281C0 /
 * em_stream_lanes_00128250 (em_stream_lanes_original.c). Channel arithmetic
 * is em_pose_bank's cursors and em_pose_transition's blend (pose_math.h),
 * unchanged.
 *
 * Fail-stop. A clip missing from the bank, a sample frame outside a clip, a
 * transition length that is not a whole number of ticks in 1..65535, or a
 * nonzero-blend init before any clip was posed returns -1. The state is left
 * as the original order leaves it up to that point. */
#ifndef EM_POSE_CHAIN_H
#define EM_POSE_CHAIN_H

#include <stdint.h>

#include "game/em_pose_bank.h"
#include "game/em_pose_transition.h"

typedef struct EmPoseChainEvent {
    int16_t frame;       /* compared with float_to_int(+3C) */
    uint16_t flags;      /* OR-ed into the advance result */
} EmPoseChainEvent;

/* The decoded bank (assets/player_clips_full.empx, tools/export_player_clips.py).
 * bank.clips[i].next_clip is the header +4 halfword, .blend holds the +6
 * halfword's bits (read signed). */
typedef struct EmPoseChainBank {
    EmPoseBank bank;
    uint32_t bank_clip_count;      /* the original bank's count word */
    uint32_t *event_first;         /* per clip index */
    uint32_t *event_count;
    EmPoseChainEvent *events;
    uint16_t *lookup;              /* 0x8000 entries: clip id -> index + 1 */
} EmPoseChainBank;

typedef struct EmPoseChain {
    const EmPoseChainBank *bank;
    const EmPoseClip *clip;        /* the clip of +2C & 0x7FFF */
    uint16_t clip_word;            /* +2C */
    int16_t requested;             /* +20C */
    uint32_t clock;                /* +3C, float bits */
    uint16_t hold_frame;           /* node 0 +8E */
    int posed;                     /* channels hold an original pose */
    EmPosePlayback playback;       /* cursors of `clip` (the sampled target during a transition) */
    EmPoseTransition transition;   /* live while clip_word & 0x8000 */
    EmPoseChannels channels[EM_POSE_NODE_MAX];   /* the evaluated node channels */
} EmPoseChain;

int em_pose_chain_bank_load(EmPoseChainBank *bank, const char *path);
void em_pose_chain_bank_free(EmPoseChainBank *bank);
/* 001C6120: the clip for (id & 0x7FFF), or NULL. */
const EmPoseClip *em_pose_chain_find(const EmPoseChainBank *bank, int clip);
/* 001C61D0: the unsigned halfword +2 (frame count). */
int em_pose_chain_frames(const EmPoseChainBank *bank, int clip, int32_t *frames);

/* A state with +20C = requested and no pose yet: the first call must be a
 * zero-blend init (request / arbiter / clip_init with blend 0). */
int em_pose_chain_init(EmPoseChain *chain, const EmPoseChainBank *bank, int16_t requested);

int em_pose_chain_clip_init(EmPoseChain *chain, int clip, float blend, float frame); /* 001C67E0 */
/* 001C64F0: dt split into steps of at most 1; freeze_motion is D_008106F3
 * (001C87C0 clears the velocities after the step). *flags gets the signed
 * halfword result. */
int em_pose_chain_advance(EmPoseChain *chain, float dt, int freeze_motion, int32_t *flags);
int em_pose_chain_request(EmPoseChain *chain, int clip, int flags, float blend,
                          int *result);                                            /* 001749A0 */
int em_pose_chain_arbiter(EmPoseChain *chain, int clip, float blend, float frame,
                          int *result);                                            /* 001749F0 */
/* The evaluated channels (rotation = 001CA0A0 of the node's +30/+40 by +50). */
int em_pose_chain_channels(const EmPoseChain *chain, EmPoseChannels *out);

#endif
