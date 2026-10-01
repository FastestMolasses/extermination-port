#ifndef EM_AIM_FIRE_LIVE_H
#define EM_AIM_FIRE_LIVE_H
#include "game/em_aim_fire_target.h"
#include "game/em_pose_host_workers.h"

/* Composition only: every region points at its owner's canonical storage.
 * The caller supplies player globals/scratch, the pose's regions, and the
 * equipment/table/world views through map. Unknown views and callees fault.
 * No game globals or original tables are allocated by this adapter. */
typedef struct {
    void *context;
    EmPlayerLiveActor *player;
    uint32_t player_address;
    EmPoseHost *pose;
    const EmPoseRegion *regions;
    unsigned region_count;
    void *(*map)(void *, uint32_t address, size_t size, int write);
    int (*call)(void *, EmAimFireTargetCall *);
    /* Private native call temporaries, replacing original stack locals. */
    uint8_t temporary[16 * 0x400];
    unsigned depth;
    uint32_t fault_function, fault_address;
} EmAimFireLive;

void *em_aim_fire_live_map(void *live, uint32_t address, size_t size, int write);
/* Dispatches the verified control/pose/target/fire-machine/shot owners, forwarding
 * other originals to call. Callee status is independent of v0/f0. */
int em_aim_fire_live_call(EmAimFireLive *, EmAimFireTargetCall *);
/* Clear only between completed top-level calls, after the caller has handled
 * the previous fault. An in-flight call cannot be reset. */
int em_aim_fire_live_clear_fault(EmAimFireLive *);
#endif
