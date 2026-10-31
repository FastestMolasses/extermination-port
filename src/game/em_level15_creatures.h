/* Level-15 boot routines omitted by the earlier delta inventory. UNWIRED.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >=0 and fail at <0. A latched
 * fault prevents all later observable accesses/calls. No owned game data.
 * Evidence: tools/test_level15_creatures_reference.py. */
#ifndef EM_LEVEL15_CREATURES_H
#define EM_LEVEL15_CREATURES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmLevel15CreaturesFault;
enum { EM_LEVEL15_CREATURES_FAULT_NONE = 0, EM_LEVEL15_CREATURES_FAULT_NULL_WORKER = 1,
       EM_LEVEL15_CREATURES_FAULT_WORKER_FAILED = 2, EM_LEVEL15_CREATURES_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_0011DE90)(void *ctx, float a0, float *result);
    int (*w_0011E2A8)(void *ctx, float a0, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_00131ED0)(void *ctx, uint32_t a0);
    int (*w_00131F20)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00132490)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013D600)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00140760)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001408D0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00140A90)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00140C30)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00140F80)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001418F0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0019AB20)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, int32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0D80)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B10B0)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, int32_t *result);
    int (*w_001B1190)(void *ctx, int32_t a0);
    int (*w_001B13F0)(void *ctx, float a0, uint32_t a1, uint32_t a2, int32_t *result);
    int (*w_001B1560)(void *ctx, uint32_t a0, uint32_t a1, float a2, int32_t *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B2140)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B3250)(void *ctx, uint32_t a0, uint32_t a1, float a2, int32_t *result);
    int (*w_001B4CF0)(void *ctx, uint32_t a0);
    int (*w_001B5360)(void *ctx, uint32_t a0);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float a1, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t a1, float a2, float a3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001EFD90)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001EFE00)(void *ctx, int32_t a0, uint32_t a1, int32_t *result);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, float a3);
/* END GENERATED HOOKS */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t self);
} EmLevel15CreaturesHooks;
int em_level15_creatures_0012F6C0(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, EmLevel15CreaturesFault *fault);
int em_level15_creatures_0013D490(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, EmLevel15CreaturesFault *fault);
int em_level15_creatures_00141A30(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, int32_t *result, EmLevel15CreaturesFault *fault);
int em_level15_creatures_00153B50(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, EmLevel15CreaturesFault *fault);
/* AREA03 sub 0's creature (byte-matched decomp C). */
int em_level15_creatures_0013D2D0(const EmLevel15CreaturesHooks *h, uint32_t a0, EmLevel15CreaturesFault *fault);
int em_level15_creatures_0013D850(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, EmLevel15CreaturesFault *fault);
int em_level15_creatures_00140690(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, EmLevel15CreaturesFault *fault);
int em_level15_creatures_00140C00(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, EmLevel15CreaturesFault *fault);
int em_level15_creatures_00141C90(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, EmLevel15CreaturesFault *fault);
#ifdef __cplusplus
}
#endif
#endif
