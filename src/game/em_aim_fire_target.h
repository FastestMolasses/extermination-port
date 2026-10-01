#ifndef EM_AIM_FIRE_TARGET_H
#define EM_AIM_FIRE_TARGET_H
#include <stdint.h>

/* Original target acquisition and sight drawers. All pointers are original
 * addresses. The host shares actors, globals and scratch with their owners.
 * Callees are workers so existing verified owners can be reused. */
typedef struct {
    uint32_t function, sp;
    uint64_t a[7];             /* a0..a3, t0..t2; valid entries: na */
    uint32_t f[8];             /* f12..f19 raw bits; valid entries: nf */
    uint32_t na, nf;
    uint64_t v0;
    uint32_t f0;
} EmAimFireTargetCall;
typedef struct {
    void *context;
    uint8_t *(*map)(void *context, uint32_t address, uint32_t size, int write);
    int (*call)(void *context, EmAimFireTargetCall *call);
    /* Optional observer: after each direct store, including same-value stores. */
    void (*store)(void *context, uint32_t address, uint32_t size);
    uint32_t sp;
    int32_t fault;             /* 1 missing map/call; 2 unmapped; 3 callee failed */
    uint32_t fault_function, fault_address;
} EmAimFireTarget;

/* Return zero on success, -1 on a latched fault. No later operation occurs
 * after failure; clear the three fault fields explicitly before retrying.
 * The host maps sp-0x10..sp for 00185760's local colour, observed by its
 * 001E2BA0 worker. Register saves have no native representation. */
int em_aim_fire_target_00185A10(EmAimFireTarget *, uint32_t actor, uint32_t *result);
int em_aim_fire_target_00185E30(EmAimFireTarget *, uint32_t actor, uint32_t target, uint32_t *result);
int em_aim_fire_target_00199220(EmAimFireTarget *, uint32_t actor);
int em_aim_fire_target_001854E0(EmAimFireTarget *, uint32_t gun);
int em_aim_fire_target_00185760(EmAimFireTarget *, uint32_t gun);
int em_aim_fire_target_00183AC0(EmAimFireTarget *, uint32_t actor, uint32_t *result);
int em_aim_fire_target_00183B80(EmAimFireTarget *, uint32_t actor, uint32_t *result);
#endif
