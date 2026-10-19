/* AREA00 return transition: four AREA01 routines absent from the delta-only
 * standalone groups. UNWIRED. Original pointers stay 32-bit and every access
 * uses bytes; all external calls require explicit workers. Worker success is
 * >= 0, failure < 0. The first fault stops all later observable accesses and
 * calls. Results are written only on success. Fault codes: 1 missing worker,
 * 2 failed worker, 5 unmapped/misaligned memory. sp is the original entry stack.
 * Evidence: tools/test_area00_return_reference.py. */
#ifndef EM_AREA00_RETURN_H
#define EM_AREA00_RETURN_H
#include <stdint.h>
typedef struct { uint32_t address; int32_t code; } EmArea00ReturnFault;
enum { EM_AREA00_RETURN_FAULT_NONE = 0, EM_AREA00_RETURN_FAULT_NULL_WORKER = 1,
       EM_AREA00_RETURN_FAULT_WORKER_FAILED = 2, EM_AREA00_RETURN_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102718)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001027E0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001028D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102958)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_001031E0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00103230)(void *ctx, uint32_t a0, uint32_t a1, float a2);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001287F0)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, float a3);
    int (*w_001288D0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00128AB0)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_00129780)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result);
    int (*w_0019A570)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0CD0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001B1240)(void *ctx, uint32_t a0, float a1, float a2, float *result);
    int (*w_001B12B0)(void *ctx, float a0, float a1, float a2, float *result);
    int (*w_001B1470)(void *ctx, float a0, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0);
    int (*w_001BA1C0)(void *ctx, uint32_t a0, int32_t a1, int32_t *result);
    int (*w_001C2770)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result);
    int (*w_001C3D60)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float a1, int32_t *result);
    int (*w_001C6960)(void *ctx, uint32_t a0);
    int (*w_001C69A0)(void *ctx, uint32_t a0);
    int (*w_001CA6F0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001EFD90)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001FB9F0)(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, float a3);
    int (*w_00824340)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00824770)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00824D50)(void *ctx, uint32_t a0, uint32_t a1);
/* END GENERATED HOOKS */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t self);
} EmArea00ReturnHooks;
int em_area00_return_00823A90(const EmArea00ReturnHooks *, uint32_t self, uint32_t a1,
                             uint32_t a2, int32_t *result, EmArea00ReturnFault *);
int em_area00_return_00823CD0(const EmArea00ReturnHooks *, uint32_t self, EmArea00ReturnFault *);
int em_area00_return_008240E0(const EmArea00ReturnHooks *, uint32_t self, uint32_t ev, EmArea00ReturnFault *);
int em_area00_return_00826010(const EmArea00ReturnHooks *, uint32_t self, uint32_t sp, EmArea00ReturnFault *);
#endif
