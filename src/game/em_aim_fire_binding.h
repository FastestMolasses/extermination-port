#ifndef EM_AIM_FIRE_BINDING_H
#define EM_AIM_FIRE_BINDING_H
#include "game/em_aim_fire_live.h"
#include "game/em_sdk_math_original.h"
typedef struct {
    EmPlayerLiveActor *player;
    EmPoseHost *pose;
    EmSdkMathContext *sdk;
    const EmPoseRegion *regions;
    unsigned region_count;
    uint32_t *bone_array;
    void *action_context;
    int (*action)(void *, EmPlayerLiveActor *, int *);
} EmAimFireBindingConfig;
int em_aim_fire_binding_configure(const EmAimFireBindingConfig *);
int em_aim_fire_binding_ready(void);
void em_aim_fire_binding_set_extension(
    int (*call)(void *, EmAimFireLive *, EmAimFireTargetCall *),
    void *(*map)(void *, uint32_t, size_t, int), void *context);
EmAimFireLive *em_aim_fire_binding_host(void);
int em_aim_fire_binding_frame(EmAimFireTargetCall *);
int em_aim_fire_binding_run(uint32_t entry,uint32_t actor);
void *em_aim_fire_binding_bytes(uint32_t address,size_t size,int write);
#endif
