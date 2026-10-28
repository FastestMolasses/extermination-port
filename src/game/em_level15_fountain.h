/* Level-15 boot routines omitted by the earlier delta inventory. UNWIRED.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >=0 and fail at <0. A latched
 * fault prevents all later observable accesses/calls. No owned game data.
 * Evidence: tools/test_level15_fountain_reference.py. */
#ifndef EM_LEVEL15_FOUNTAIN_H
#define EM_LEVEL15_FOUNTAIN_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmLevel15FountainFault;
enum { EM_LEVEL15_FOUNTAIN_FAULT_NONE = 0, EM_LEVEL15_FOUNTAIN_FAULT_NULL_WORKER = 1,
       EM_LEVEL15_FOUNTAIN_FAULT_WORKER_FAILED = 2, EM_LEVEL15_FOUNTAIN_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_00187EC0)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001AF890)(void *ctx, uint32_t a0);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B1020)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t *result);
    int (*w_001B1190)(void *ctx, int32_t a0);
    int (*w_001B17A0)(void *ctx, uint32_t a0);
    int (*w_001BE5F0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result);
    int (*w_001BEAC0)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, uint32_t *result);
    int (*w_001BEB70)(void *ctx, uint32_t a0);
    int (*w_001BEC40)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2);
    int (*w_001C1500)(void *ctx, uint32_t a0, int32_t a1, float a2, float a3, float a4);
    int (*w_001C1570)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001C62C0)(void *ctx, uint32_t a0);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001D0C80)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001D0D40)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3);
    int (*w_001D0D60)(void *ctx, uint32_t a0, float a1, int32_t *result);
    int (*w_001EFD90)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001EFE00)(void *ctx, int32_t a0, uint32_t a1);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, float a3);
/* END GENERATED HOOKS */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t self);
} EmLevel15FountainHooks;
int em_level15_fountain_001BECC0(const EmLevel15FountainHooks *h, uint32_t a0, EmLevel15FountainFault *fault);
int em_level15_fountain_001BF3C0(const EmLevel15FountainHooks *h, uint32_t a0, EmLevel15FountainFault *fault);
#ifdef __cplusplus
}
#endif
#endif
