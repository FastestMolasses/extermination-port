/* Level-15 movement routines omitted by the earlier delta inventory. UNWIRED.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >=0 and fail at <0. A latched
 * fault prevents all later observable accesses/calls. No owned game data.
 * Evidence: tools/test_level15_motion_reference.py. */
#ifndef EM_LEVEL15_MOTION_H
#define EM_LEVEL15_MOTION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmLevel15MotionFault;
enum { EM_LEVEL15_MOTION_FAULT_NONE = 0, EM_LEVEL15_MOTION_FAULT_NULL_WORKER = 1,
       EM_LEVEL15_MOTION_FAULT_WORKER_FAILED = 2, EM_LEVEL15_MOTION_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102BB0)(void *ctx, uint32_t a0, uint32_t a1, float a2);
    int (*w_0011DE90)(void *ctx, float a0, float *result);
    int (*w_0011E0A8)(void *ctx, uint32_t a0, float a1);
    int (*w_0011E2A8)(void *ctx, float a0, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001281C0)(void *ctx, float a0, int32_t *result);
    int (*w_00131ED0)(void *ctx, uint32_t a0);
    int (*w_0013D850)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013D980)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013DB70)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013DC60)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013DD40)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013E6D0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013EB90)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013EDB0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013F240)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013F770)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00140440)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00141A30)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00141C90)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0019AB20)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, int32_t *result);
    int (*w_0019B6C0)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001B1560)(void *ctx, uint32_t a0, uint32_t a1, float a2, int32_t *result);
    int (*w_001B15D0)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B2140)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B2F70)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001B32F0)(void *ctx, uint32_t a0, uint32_t a1, float a2, int32_t *result);
    int (*w_001B3390)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float a3, int32_t *result);
    int (*w_001B3440)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float a3, int32_t *result);
    int (*w_001B4810)(void *ctx, uint32_t a0);
    int (*w_001C6160)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float a1, int32_t *result);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001EFD90)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2);
/* END GENERATED HOOKS */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t self);
} EmLevel15MotionHooks;
int em_level15_motion_0013D600(const EmLevel15MotionHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, EmLevel15MotionFault *fault);
int em_level15_motion_00140C30(const EmLevel15MotionHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, EmLevel15MotionFault *fault);
int em_level15_motion_00140F80(const EmLevel15MotionHooks *h, uint32_t a0, uint32_t a1, EmLevel15MotionFault *fault);
#ifdef __cplusplus
}
#endif
#endif
