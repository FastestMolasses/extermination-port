/* Level-15 boot routines omitted by the earlier delta inventory. UNWIRED.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >=0 and fail at <0. A latched
 * fault prevents all later observable accesses/calls. No owned game data.
 * Evidence: tools/test_level15_support_reference.py. */
#ifndef EM_LEVEL15_SUPPORT_H
#define EM_LEVEL15_SUPPORT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmLevel15SupportFault;
enum { EM_LEVEL15_SUPPORT_FAULT_NONE = 0, EM_LEVEL15_SUPPORT_FAULT_NULL_WORKER = 1,
       EM_LEVEL15_SUPPORT_FAULT_WORKER_FAILED = 2, EM_LEVEL15_SUPPORT_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102BB0)(void *ctx, uint32_t a0, uint32_t a1, float a2);
    int (*w_0011E620)(void *ctx, float a0, float a1, float *result);
    int (*w_001281C0)(void *ctx, float a0, int32_t *result);
    int (*w_00184D20)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result);
    int (*w_0019B4C0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
    int (*w_001AFA90)(void *ctx, int32_t a0, uint32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B1190)(void *ctx, int32_t a0);
    int (*w_001B1470)(void *ctx, float a0, float *result);
    int (*w_001B3F10)(void *ctx, uint32_t a0, float a1, float a2, int32_t *result);
    int (*w_001C7420)(void *ctx, uint32_t a0, int32_t a1, int32_t a2);
    int (*w_001CAAC0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001D0D40)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3);
    int (*w_001D1F20)(void *ctx, int32_t a0);
    int (*w_001D1F80)(void *ctx, int32_t a0, int32_t a1, int32_t a2);
    int (*w_001D1FF0)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001D2090)(void *ctx, int32_t a0, uint32_t a1);
    int (*w_001D2910)(void *ctx, int32_t a0, int32_t *result);
    int (*w_001D6930)(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3, uint32_t a4);
    int (*w_001D6BA0)(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5);
    int (*w_001D8C20)(void *ctx, int32_t a0);
    int (*w_00207D00)(void *ctx, int32_t a0, int32_t a1);
    int (*w_00207E40)(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3, uint32_t a4, uint32_t a5, uint64_t a6);
    int (*w_0020A7A0)(void *ctx, uint64_t a0);
/* END GENERATED HOOKS */
} EmLevel15SupportHooks;
int em_level15_support_001418F0(const EmLevel15SupportHooks *h, uint32_t a0, uint32_t a1, EmLevel15SupportFault *fault);
int em_level15_support_00153EA0(const EmLevel15SupportHooks *h, uint32_t a0, EmLevel15SupportFault *fault);
int em_level15_support_00185420(const EmLevel15SupportHooks *h, int32_t a0, int32_t *result, EmLevel15SupportFault *fault);
int em_level15_support_001A91C0(const EmLevel15SupportHooks *h, uint32_t a0, uint32_t a1, float a2, EmLevel15SupportFault *fault);
int em_level15_support_001BEB70(const EmLevel15SupportHooks *h, uint32_t a0, int32_t *result, EmLevel15SupportFault *fault);
int em_level15_support_001BEC40(const EmLevel15SupportHooks *h, uint32_t a0, uint32_t a1, int32_t a2, EmLevel15SupportFault *fault);
int em_level15_support_001C2690(const EmLevel15SupportHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, EmLevel15SupportFault *fault);
int em_level15_support_001CB200(const EmLevel15SupportHooks *h, uint32_t a0, uint32_t a1, EmLevel15SupportFault *fault);
int em_level15_support_001CB2B0(const EmLevel15SupportHooks *h, uint32_t a0, EmLevel15SupportFault *fault);
int em_level15_support_001D4440(const EmLevel15SupportHooks *h, int32_t a0, uint32_t a1, uint32_t a2, EmLevel15SupportFault *fault);
int em_level15_support_001D4640(const EmLevel15SupportHooks *h, uint32_t a0, uint32_t a1, EmLevel15SupportFault *fault);
int em_level15_support_001D6580(const EmLevel15SupportHooks *h, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t *result, EmLevel15SupportFault *fault);
int em_level15_support_0020F170(const EmLevel15SupportHooks *h, EmLevel15SupportFault *fault);
#ifdef __cplusplus
}
#endif
#endif
