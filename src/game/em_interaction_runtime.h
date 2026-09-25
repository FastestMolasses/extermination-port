/* Shared ownership around the proven interaction frame and animation cores.
 * One instance belongs to one game world; panel/elevator adapters pass their
 * own stable owner token. Host worker boundaries remain explicit. */
#ifndef EM_INTERACTION_RUNTIME_H
#define EM_INTERACTION_RUNTIME_H

#include "game/em_interaction_animation.h"
#include "game/em_interaction_frame.h"

typedef struct {
    void *context;
    /* Called at the ordinary player stage. acquire must perform the real
     * 0015B130 takeover, including conditional default-clip init and182D70.
     * -1 failure,0 blocked,1 accepted. An existing default clip keeps its
     * cursor; a required blend8 transition adds no readiness delay.
     * The runtime publishes player_ready only on1. */
    int (*acquire_player)(void *);
    /* Default clip continues until a script requests a verified animation.
     * Same palette result contract as em_interaction_animation_tick. */
    int (*idle_player_tick)(void *, float *local_palette);
    /* Original182DF0, including any required default-clip transition.
     * Return1 only after the release operation was accepted. */
    int (*release_player)(void *);
    /* Apply actor placement to the local palette, update the rendered pose
     * and hip mirrors. Return1 on success. No matrix blending here. */
    int (*publish_palette)(void *, const float *local_palette);
    EmInteractionFrameEmit frame_event;
    int (*camera_retarget)(void *); /* Current adapter's verified command. */
} EmInteractionRuntimeHooks;

/* Optional source-channel sampler, invoked after every scripted animation
 * clock tick, including result0 commits. It keeps the previous source pose
 * available for later transitions and may replace a newly sampled palette.
 * Return1 when accepted; a failure cannot fall back to baked matrices. */
typedef int (*EmInteractionPoseWorker)(void *, const EmInteractionAnimation *, int palette_result,
                                       float *local_palette);

/* Player-ready2 means the extra face object is attached. This worker must
 * tick that face before body request/advance and produce the actual palette.
 * It uses hooks.context and returns the same -1/0/1 palette result contract. */
typedef int (*EmInteractionCinematicPlayerWorker)(void *, float *local_palette);

typedef struct {
    const void *owner;
    EmInteractionFrame *frame;
    const EmModel *model;
    EmInteractionAnimation animation;
    EmInteractionRuntimeHooks hooks;
    EmInteractionPoseWorker pose_worker;
    EmInteractionCinematicPlayerWorker cinematic_player_worker;
    float *local_palette;
    int failed;
} EmInteractionRuntime;

/* local_palette has at least model->bone_count*16 floats and belongs to
 * the caller. init does not overwrite live frame state or player poses. */
int em_interaction_runtime_init(EmInteractionRuntime *runtime, EmInteractionFrame *frame,
                                const EmModel *model, float *local_palette,
                                const EmInteractionRuntimeHooks *hooks);
/* Configure before claiming an owner; callback uses hooks.context. */
int em_interaction_runtime_set_pose_worker(EmInteractionRuntime *runtime,
                                           EmInteractionPoseWorker worker);
int em_interaction_runtime_set_cinematic_player_worker(EmInteractionRuntime *runtime,
    EmInteractionCinematicPlayerWorker worker);
/* After original single-winner use arbitration/alignment, claim the owner
 * and publish selector3. A competing token cannot replace a live owner. */
int em_interaction_runtime_claim(EmInteractionRuntime *runtime, const void *owner);
/* An owner whose own script opened the scripted frame (op07 already wrote
 * the selector, e.g. the truck trigger 008251E0's 0x8292C0): the player
 * takeover the next player stage performs (0015B130's 00182B30 admission)
 * serves that owner. Requires a nonzero selector and a free, unacquired
 * player; writes nothing to the frame. 1 claimed, 0 refused. */
int em_interaction_runtime_claim_scripted(EmInteractionRuntime *runtime, const void *owner);
int em_interaction_runtime_owns(const EmInteractionRuntime *runtime, const void *owner);
const void *em_interaction_runtime_owner(const EmInteractionRuntime *runtime);
int em_interaction_runtime_camera_owned(const EmInteractionRuntime *runtime);

EmScriptCommandResult em_interaction_runtime_frame(EmInteractionRuntime *runtime, const void *owner,
                                                   EmScript *script, const unsigned char *record);
int em_interaction_runtime_camera_retarget(EmInteractionRuntime *runtime, const void *owner);
int em_interaction_runtime_animation_start(EmInteractionRuntime *runtime, const void *owner,
                                           uint16_t clip, float rate, float blend);
/* -1 failure/inactive,0 waiting,1 actual animation end flag. */
int em_interaction_runtime_animation_done(const EmInteractionRuntime *runtime, const void *owner);

/* Call exactly once at the actual player stage. ordinary_tasks_enabled=0
 * during original status/menu frames: no acquisition, cursor or release
 * advances. Returns-1 failure,0 no takeover,1 player owned for this call.
 * When selector clears, the original player advances first, then182DF0
 * releases it. Ownership clears only after successful release. */
int em_interaction_runtime_player_tick(EmInteractionRuntime *runtime, int ordinary_tasks_enabled);

#endif
