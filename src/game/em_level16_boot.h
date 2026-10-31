/* Boot workers the AREA03 creature, the AREA04 sub 1 creature and the
 * sixteenth level's owners reach that no earlier module translates. Same
 * contract as em_level15_motion.h. Evidence:
 * tools/test_level16_boot_reference.py. */
#ifndef EM_LEVEL16_BOOT_H
#define EM_LEVEL16_BOOT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmLevel16BootFault;
enum { EM_LEVEL16_BOOT_FAULT_NONE = 0, EM_LEVEL16_BOOT_FAULT_NULL_WORKER = 1,
       EM_LEVEL16_BOOT_FAULT_WORKER_FAILED = 2, EM_LEVEL16_BOOT_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001026D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102738)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_00102760)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102958)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102C58)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00103230)(void *ctx, uint32_t a0, uint32_t a1, float a2);
    int (*w_0011E420)(void *ctx, float a0, float *result);
    int (*w_001281C0)(void *ctx, float a0, int32_t *result);
    int (*w_001749A0)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, float a3);
    int (*w_0017B490)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t *result);
    int (*w_0019A570)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t *result);
    int (*w_0019AA80)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result);
    int (*w_001AFA90)(void *ctx, int32_t a0, uint32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0FD0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1020)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t *result);
    int (*w_001B10B0)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, int32_t *result);
    int (*w_001B1190)(void *ctx, int32_t a0);
    int (*w_001B1380)(void *ctx, uint32_t a0, uint32_t a1, float a2, int32_t *result);
    int (*w_001B1470)(void *ctx, float a0, float *result);
    int (*w_001B15D0)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1EA0)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t *result);
    int (*w_001B2B10)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001B39F0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1C0)(void *ctx, uint32_t a0, int32_t a1, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BA540)(void *ctx, uint32_t a0);
    int (*w_001BA580)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001BA8E0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001C4760)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001C47A0)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float a1, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t a1, float a2, float a3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001C94B0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
    int (*w_001CA6F0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001E2BA0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float a3);
    int (*w_001EFD90)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001F1110)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001F1180)(void *ctx, uint32_t a0);
    int (*w_001F4F40)(void *ctx, int32_t a0, uint32_t *result);
    int (*w_001FAE70)(void *ctx, int32_t a0);
    int (*w_001FB9F0)(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t *result);
    int (*w_0021BB00)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_0021BC40)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_0021BE40)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
/* END GENERATED HOOKS */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t self);
} EmLevel16BootHooks;
int em_level16_boot_001B3670(const EmLevel16BootHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t sp, int32_t *result, EmLevel16BootFault *fault);
int em_level16_boot_0021BED0(const EmLevel16BootHooks *h, uint32_t a0, int32_t *result, EmLevel16BootFault *fault);
int em_level16_boot_0021BF90(const EmLevel16BootHooks *h, uint32_t a0, uint32_t a1, EmLevel16BootFault *fault);
int em_level16_boot_a03_008235A0(const EmLevel16BootHooks *h, uint32_t a0, EmLevel16BootFault *fault);
int em_level16_boot_a03_00823810(const EmLevel16BootHooks *h, uint32_t a0, EmLevel16BootFault *fault);
int em_level16_boot_a03_00825200(const EmLevel16BootHooks *h, uint32_t a0, uint32_t sp, EmLevel16BootFault *fault);
int em_level16_boot_a04_00824EF0(const EmLevel16BootHooks *h, uint32_t a0, uint32_t sp, EmLevel16BootFault *fault);
int em_level16_boot_a04_00825310(const EmLevel16BootHooks *h, uint32_t a0, EmLevel16BootFault *fault);
int em_level16_boot_00158430(const EmLevel16BootHooks *h, uint32_t a0, EmLevel16BootFault *fault);
int em_level16_boot_001BD460(const EmLevel16BootHooks *h, uint32_t a0, int32_t *result, EmLevel16BootFault *fault);
int em_level16_boot_001C82A0(const EmLevel16BootHooks *h, uint32_t a0, uint32_t a1, EmLevel16BootFault *fault);
int em_level16_boot_001B3C30(const EmLevel16BootHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, float a3, float a4, uint32_t sp, EmLevel16BootFault *fault);
int em_level16_boot_0017C370(const EmLevel16BootHooks *h, uint32_t a0, EmLevel16BootFault *fault);
int em_level16_boot_0021C040(const EmLevel16BootHooks *h, uint32_t a0, uint32_t a1, EmLevel16BootFault *fault);
#ifdef __cplusplus
}
#endif
#endif
