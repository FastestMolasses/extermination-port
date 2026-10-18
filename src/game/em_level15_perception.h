/* Level-15 boot routines omitted by the earlier delta inventory. UNWIRED.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >=0 and fail at <0. A latched
 * fault prevents all later observable accesses/calls. No owned game data.
 * Evidence: tools/test_level15_perception_reference.py. */
#ifndef EM_LEVEL15_PERCEPTION_H
#define EM_LEVEL15_PERCEPTION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmLevel15PerceptionFault;
enum { EM_LEVEL15_PERCEPTION_FAULT_NONE = 0, EM_LEVEL15_PERCEPTION_FAULT_NULL_WORKER = 1,
       EM_LEVEL15_PERCEPTION_FAULT_WORKER_FAILED = 2, EM_LEVEL15_PERCEPTION_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_0011DF78)(void *ctx, float a0, float *result);
    int (*w_0011E620)(void *ctx, float a0, float a1, float *result);
    int (*w_0011E748)(void *ctx, float a0, float *result);
    int (*w_001B1470)(void *ctx, float a0, float *result);
/* END GENERATED HOOKS */
} EmLevel15PerceptionHooks;
int em_level15_perception_00184D20(const EmLevel15PerceptionHooks *h, uint32_t actor,
    uint32_t sensor, int32_t event, int32_t *result, EmLevel15PerceptionFault *fault);
#ifdef __cplusplus
}
#endif
#endif
