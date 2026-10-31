/* The AREA04 sub 1 creature (kind 7, behaviour 00147390) and its state
 * family, from the decomp C and, for 00148B40, 0014B7B0, 0014BC30 and
 * 0014C0B0 (NEARMISS C), the original instructions. UNWIRED.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >=0 and fail at <0. A latched
 * fault prevents all later observable accesses/calls. No owned game data.
 * `sp` is the original's stack pointer at the function's entry: 00149B50 and
 * 0014B7B0 keep frame locals whose addresses reach their workers, so the
 * chain above them carries it. Evidence: tools/test_level16_beast_reference.py. */
#ifndef EM_LEVEL16_BEAST_H
#define EM_LEVEL16_BEAST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmLevel16BeastFault;
enum { EM_LEVEL16_BEAST_FAULT_NONE = 0, EM_LEVEL16_BEAST_FAULT_NULL_WORKER = 1,
       EM_LEVEL16_BEAST_FAULT_WORKER_FAILED = 2, EM_LEVEL16_BEAST_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_001000E0)(void *ctx, uint64_t a0, uint64_t a1, int32_t *result);
    int (*w_00102760)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102958)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102BB0)(void *ctx, uint32_t a0, uint32_t a1, float a2);
    int (*w_001031E0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0011DE90)(void *ctx, float a0, float *result);
    int (*w_0011DF78)(void *ctx, float a0, float *result);
    int (*w_0011E2A8)(void *ctx, float a0, float *result);
    int (*w_0011E620)(void *ctx, float a0, float a1, float *result);
    int (*w_0011E748)(void *ctx, float a0, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001281C0)(void *ctx, float a0, int32_t *result);
    int (*w_00128350)(void *ctx, float a0, uint64_t *result);
    int (*w_00131ED0)(void *ctx, uint32_t a0);
    int (*w_00182F90)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0019A440)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result);
    int (*w_0019A570)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t *result);
    int (*w_0019AB20)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t *result);
    int (*w_0019AD00)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result);
    int (*w_0019B6C0)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001A7B80)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0D80)(void *ctx, uint32_t a0);
    int (*w_001B10B0)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, int32_t *result);
    int (*w_001B1190)(void *ctx, int32_t a0);
    int (*w_001B1240)(void *ctx, uint32_t a0, float a1, float a2, float *result);
    int (*w_001B12B0)(void *ctx, float a0, float a1, float a2, float *result);
    int (*w_001B13F0)(void *ctx, uint32_t a0, uint32_t a1, float a2, int32_t *result);
    int (*w_001B1470)(void *ctx, float a0, float *result);
    int (*w_001B1560)(void *ctx, uint32_t a0, uint32_t a1, float a2, int32_t *result);
    int (*w_001B15D0)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1EA0)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t *result);
    int (*w_001B2140)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B2B10)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001B2BF0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float a3, int32_t *result);
    int (*w_001B2F70)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001B3250)(void *ctx, uint32_t a0, uint32_t a1, float a2, int32_t *result);
    int (*w_001B32F0)(void *ctx, uint32_t a0, uint32_t a1, float a2, int32_t *result);
    int (*w_001B3390)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float a3, int32_t *result);
    int (*w_001B3440)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float a3, int32_t *result);
    int (*w_001B3580)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_001B37D0)(void *ctx, uint32_t a0, float a1, float a2, float *result);
    int (*w_001B3C30)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float a3, float a4);
    int (*w_001B3F10)(void *ctx, uint32_t a0, float a1, float a2, int32_t *result);
    int (*w_001B4810)(void *ctx, uint32_t a0);
    int (*w_001B4CF0)(void *ctx, uint32_t a0);
    int (*w_001B5360)(void *ctx, uint32_t a0);
    int (*w_001B55E0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float a1, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t a1, float a2, float a3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001C81C0)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, uint32_t a3);
    int (*w_001C82A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001C9D50)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float a3);
    int (*w_001E8B90)(void *ctx, uint32_t a0, float a1);
    int (*w_001EFD90)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001EFE00)(void *ctx, int32_t a0, uint32_t a1, int32_t *result);
    int (*w_001EFEB0)(void *ctx, int32_t a0, uint32_t a1);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, float a3);
    int (*w_0021BE40)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_0021BED0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_0021BF90)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0021C040)(void *ctx, uint32_t a0, uint32_t a1);
/* END GENERATED HOOKS */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t self);
} EmLevel16BeastHooks;
int em_level16_beast_00147390(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t sp, EmLevel16BeastFault *fault);
int em_level16_beast_001475A0(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_00147700(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, EmLevel16BeastFault *fault);
int em_level16_beast_00147960(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_00147B50(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, EmLevel16BeastFault *fault);
int em_level16_beast_00148130(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_00148400(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_00148520(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_001486E0(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_00148B40(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_00149B50(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, EmLevel16BeastFault *fault);
int em_level16_beast_0014A0D0(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_0014A350(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_0014A620(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_0014A730(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_0014A9A0(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_0014ACC0(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_0014AE50(const EmLevel16BeastHooks *h, uint32_t a0, EmLevel16BeastFault *fault);
int em_level16_beast_0014AE80(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_0014AFA0(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_0014B7B0(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, int32_t *result, EmLevel16BeastFault *fault);
int em_level16_beast_0014BB10(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_0014BC30(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, int32_t *result, EmLevel16BeastFault *fault);
int em_level16_beast_0014BE20(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, int32_t *result, EmLevel16BeastFault *fault);
int em_level16_beast_0014C0B0(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault);
int em_level16_beast_0014C130(const EmLevel16BeastHooks *h, uint32_t a0, int32_t *result, EmLevel16BeastFault *fault);
#ifdef __cplusplus
}
#endif
#endif
