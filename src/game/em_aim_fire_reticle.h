#ifndef EM_AIM_FIRE_RETICLE_H
#define EM_AIM_FIRE_RETICLE_H
#include "game/em_area00_hud.h"

/* Target markers: original 001DD170 projection/dispatch and its two packet
 * builders. The VU register state is shared with other original render owners;
 * vf23 is an input, not reloaded by this projector. Tables remain disc inputs. */
typedef struct {
    uint32_t function, sp;
    uint64_t a[7];
    uint32_t f12, na, nf;
    uint64_t v0;
} EmAimFireReticleCall;
typedef struct {
    void *context;
    uint8_t *(*map)(void *context, uint32_t address, uint32_t size, int write);
    int (*call)(void *context, EmAimFireReticleCall *call);
    void (*store)(void *context, uint32_t address, uint32_t size);
    uint32_t sp;
    int32_t fault; /* 1 missing host; 2 unmapped; 3 failed worker; 4 unmeasured */
    uint32_t fault_function, fault_address;
    EmArea00HudVu *vu;
} EmAimFireReticle;

int em_aim_fire_reticle_001DD170(EmAimFireReticle *, uint32_t style, uint32_t position,
                                uint32_t kind, uint32_t rgba, uint32_t value);
int em_aim_fire_reticle_001DD2F0(EmAimFireReticle *, uint32_t style, uint32_t screen, uint32_t rgba);
int em_aim_fire_reticle_001DD600(EmAimFireReticle *, uint32_t style, uint32_t screen, uint32_t rgba);
#endif
