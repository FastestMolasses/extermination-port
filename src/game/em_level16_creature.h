/* The AREA03 creature's remaining behaviour states and their shared probe
 * (the workers 0013D600 and 00140690 dispatch on self+5). UNWIRED.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >=0 and fail at <0. A latched
 * fault prevents all later observable accesses/calls. No owned game data.
 * Entries that take sp receive the stack pointer at the original function's
 * entry: 001416D0 builds its two probe vectors in its frame and passes their
 * addresses on.
 * Evidence: tools/test_level16_creature_reference.py. */
#ifndef EM_LEVEL16_CREATURE_H
#define EM_LEVEL16_CREATURE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmLevel16CreatureFault;
enum { EM_LEVEL16_CREATURE_FAULT_NONE = 0, EM_LEVEL16_CREATURE_FAULT_NULL_WORKER = 1,
       EM_LEVEL16_CREATURE_FAULT_WORKER_FAILED = 2, EM_LEVEL16_CREATURE_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102760)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102958)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102B08)(void *ctx, uint32_t a0, uint32_t a1, float a2);
    int (*w_00102BB0)(void *ctx, uint32_t a0, uint32_t a1, float a2);
    int (*w_0011DF78)(void *ctx, float a0, float *result);
    int (*w_0011E620)(void *ctx, float a0, float a1, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001418F0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0019A6F0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t a4, int32_t *result);
    int (*w_001A7B80)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1190)(void *ctx, int32_t a0);
    int (*w_001B1240)(void *ctx, uint32_t a0, float a1, float a2, float *result);
    int (*w_001B12B0)(void *ctx, float a0, float a1, float a2, float *result);
    int (*w_001B13F0)(void *ctx, uint32_t a0, uint32_t a1, float a2, int32_t *result);
    int (*w_001B1470)(void *ctx, float a0, float *result);
    int (*w_001B1560)(void *ctx, uint32_t a0, uint32_t a1, float a2, int32_t *result);
    int (*w_001B15D0)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_001B2B10)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001B2BF0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float a3, int32_t *result);
    int (*w_001B3250)(void *ctx, uint32_t a0, uint32_t a1, float a2, int32_t *result);
    int (*w_001B3390)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float a3, int32_t *result);
    int (*w_001B3670)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result);
    int (*w_001B37D0)(void *ctx, uint32_t a0, float a1, float a2, float *result);
    int (*w_001B55E0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t a1, float a2, float a3);
    int (*w_001E8B90)(void *ctx, uint32_t a0, float a1);
    int (*w_001EFD90)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001EFE00)(void *ctx, int32_t a0, uint32_t a1, int32_t *result);
    int (*w_001EFFD0)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, float a4);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, float a3);
    int (*w_0021BE40)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_0021BED0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_0021BF90)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0021C040)(void *ctx, uint32_t a0, uint32_t a1);
/* END GENERATED HOOKS */
} EmLevel16CreatureHooks;
int em_level16_creature_0013D980(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, EmLevel16CreatureFault *fault);
int em_level16_creature_0013DB70(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, EmLevel16CreatureFault *fault);
int em_level16_creature_0013DC60(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, EmLevel16CreatureFault *fault);
int em_level16_creature_0013DD40(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, EmLevel16CreatureFault *fault);
int em_level16_creature_0013E6D0(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, EmLevel16CreatureFault *fault);
int em_level16_creature_0013EB90(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, EmLevel16CreatureFault *fault);
int em_level16_creature_0013EDB0(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, EmLevel16CreatureFault *fault);
int em_level16_creature_0013F240(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, EmLevel16CreatureFault *fault);
int em_level16_creature_0013F770(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, EmLevel16CreatureFault *fault);
int em_level16_creature_00140440(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, EmLevel16CreatureFault *fault);
int em_level16_creature_00140760(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, EmLevel16CreatureFault *fault);
int em_level16_creature_001408D0(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, EmLevel16CreatureFault *fault);
int em_level16_creature_00140A90(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, EmLevel16CreatureFault *fault);
int em_level16_creature_001416D0(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, int32_t *result, EmLevel16CreatureFault *fault);
#ifdef __cplusplus
}
#endif
#endif
