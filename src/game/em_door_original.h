/* Original class5 door owner: 001BBDA0 and 001BC350/300.
 * Rendering, animation and room changes are real host workers. */
#ifndef EM_DOOR_ORIGINAL_H
#define EM_DOOR_ORIGINAL_H

#include <stdint.h>

typedef struct {
    uint8_t status, visible, class_flags, subtype;
    uint8_t lifecycle, phase, armed, freed;
    int8_t animation_active;
    int16_t animation_flags, door_id, side;
    uint16_t link_flags;
    float origin[3];
    /* BBDA0 writes actor+80, not the +60 scale used by C68C0. */
    float initialized_scale[3];
} EmDoorOriginal;

typedef struct {
    void *context;
    /* Resolve model parameter, bind global bank39 and seed clip0 at time0.
     * Return1 ready,0 original allocation failure (lifecycle3),-1 host fault. */
    int (*initialize)(void *);
    /* BBE40: side selection, script patching, face, align, script start,
     * and the first script pump. A successful worker returns1. */
    int (*kickoff)(void *, int locked);
    int (*advance_animation)(void *, int16_t *time); /* original rate1 */
    int (*script_tick)(void *); /*0 waiting,1 finished,-1 fault*/
    int (*script_start)(void *, uint32_t entry);
    int (*transition)(void *); /*BC150, including the actual fade request*/
    int (*reset_animation)(void *); /*anim_clip_init(clip0,blend0,start0)*/
    int (*place)(void *); /*C68C0: owner TRS and current node channels*/
    int (*publish)(void *, const float position[3]); /*visibility byte*/
    int (*draw)(void *); /*+4C is called even when visibility is zero*/
    int (*free)(void *);
} EmDoorOriginalHooks;

/* One original pooled-owner callback. unlocked is the subtype15 area's
 * door-persistence bit; ordinary subtype3 ignores it. transition_pending
 * is D8106B8, not a guessed completion timer. Return1 allocated,0 freed,
 * -1 missing/failed worker. Do not call on frozen status frames. */
int em_door_original_tick(EmDoorOriginal *, int unlocked,
    uint8_t transition_pending, const EmDoorOriginalHooks *);

/* Shared leaves used directly by overlay door controllers. The hooks have
 * the same contract as tick. BBDA0 returns allocation readiness; BC0E0 and
 * BC290 return the original completion flag; BC240/BC300 return 1 on
 * success. All return -1 on a host fault. BBDA0 does not set status, and
 * BC290 does not change the caller's phase byte. */
int em_door_original_001BBDA0(EmDoorOriginal *, const EmDoorOriginalHooks *);
int em_door_original_001BC0E0(EmDoorOriginal *, const EmDoorOriginalHooks *);
int em_door_original_001BC240(EmDoorOriginal *, const EmDoorOriginalHooks *);
int em_door_original_001BC290(EmDoorOriginal *, uint8_t pending, const EmDoorOriginalHooks *);
int em_door_original_001BC300(EmDoorOriginal *, const EmDoorOriginalHooks *);

/* Only the actual use-scan winner may set bit2. Other armed bits survive. */
int em_door_original_arm(EmDoorOriginal *);

#endif
