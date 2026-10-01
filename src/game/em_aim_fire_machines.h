#ifndef EM_AIM_FIRE_MACHINES_H
#define EM_AIM_FIRE_MACHINES_H

#include <stdint.h>
#include "game/em_player_floor.h"

/* Canonical storage; callbacks may change these words before the next read. */
typedef struct EmAimFireMachineScene {
    const uint32_t *aim_active;                   /* 008106E0 */
    const uint8_t *fire_mode;                     /* 00810C61 */
    uint8_t *magazine;                            /* 00810C62 */
    uint16_t *reserve, *ammo1, *ammo3, *ammo4, *ammo5;
    /* 00810CB4, 00810CAA, 00810CA8, 00810CAE, 00810CB0 */
    uint8_t *remote;                              /* 00810CB6 */
    const uint16_t *pressed, *fire_mask, *remote_mask;
    /* 00810E74, 70003B78, 70003B74 */
} EmAimFireMachineScene;

typedef struct EmAimFireMachineWorkers {
    void *context;
    EmAimFireMachineScene *scene;
    int (*action)(void *, EmPlayerLiveActor *, int *result); /* 001607D0 */
    int (*steer)(void *, EmPlayerLiveActor *);                                  /* 0017AF70 */
    int (*reload)(void *, EmPlayerLiveActor *, int arg, int *result); /* 0017B300 */
    int (*reload4)(void *, int *result);                   /* 0017B420 */
    int (*sound)(void *, EmPlayerLiveActor *, int id, int *handle); /* 001FBD50(p,id,0,300) */
    int (*empty_sound)(void *);                    /* 001FB9F0(169,1000,1000,1000) */
    int (*stop_sound)(void *, int handle);                 /* 0011A070 */
    int (*matrix)(void *, EmPlayerLiveActor *);             /* 0017A130 */
    int (*bone)(void *, unsigned slot, uint32_t words[16]); /* *(275B40+4*slot)+90 */
    /* The +0x2E event halfword of the record at address (the gun at
     * player +0x20): its owner's canonical field, never a record image. */
    int (*link20)(void *, uint32_t address, uint8_t **event);
    int (*to_int)(void *, uint32_t bits, int32_t *result);   /* 001281C0 */
} EmAimFireMachineWorkers;

/* The six original routines return void. Native status is 0, or -1 on a
 * missing/failing worker, preserving writes preceding the failure. */
int em_aim_fire_machine_bound(const EmAimFireMachineWorkers *, unsigned machine);
int em_aim_fire_00170A60(const EmAimFireMachineWorkers *, EmPlayerLiveActor *, int arg);
int em_aim_fire_00171320(const EmAimFireMachineWorkers *, EmPlayerLiveActor *);
int em_aim_fire_00171670(const EmAimFireMachineWorkers *, EmPlayerLiveActor *);
int em_aim_fire_00171B00(const EmAimFireMachineWorkers *, EmPlayerLiveActor *);
int em_aim_fire_00171E90(const EmAimFireMachineWorkers *, EmPlayerLiveActor *);
int em_aim_fire_001723D0(const EmAimFireMachineWorkers *, EmPlayerLiveActor *);
#endif
