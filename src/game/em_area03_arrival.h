/* AREA03 route owners omitted by the earlier delta inventory. Bound by em_area01_runtime_area15.inc.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >=0 and fail at <0. A latched
 * fault prevents all later observable accesses/calls. No owned game data.
 * Evidence: tools/test_area03_arrival_reference.py. */
#ifndef EM_AREA03_ARRIVAL_H
#define EM_AREA03_ARRIVAL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmArea03ArrivalFault;
enum { EM_AREA03_ARRIVAL_FAULT_NONE = 0, EM_AREA03_ARRIVAL_FAULT_NULL_WORKER = 1,
       EM_AREA03_ARRIVAL_FAULT_WORKER_FAILED = 2, EM_AREA03_ARRIVAL_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102738)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001031E0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_0019A570)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t *result);
    int (*w_0019AA80)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0FD0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B6F00)(void *ctx, uint32_t a0, uint32_t a1, float a2);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001CD520)(void *ctx, int32_t a0, int32_t a1, uint32_t a2, uint64_t a3, uint32_t a4, float a5, float a6, float a7, int32_t *result);
    int (*w_001E2BA0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float a3);
    int (*w_001F4BF0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001F4E20)(void *ctx, uint32_t a0, uint32_t a1, float a2);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, float a3, int32_t *result);
/* END GENERATED HOOKS */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t self);
} EmArea03ArrivalHooks;
int em_area03_arrival_00824D30(const EmArea03ArrivalHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result, EmArea03ArrivalFault *fault);
int em_area03_arrival_00825430(const EmArea03ArrivalHooks *h, uint32_t a0, EmArea03ArrivalFault *fault);
int em_area03_arrival_008256E0(const EmArea03ArrivalHooks *h, uint32_t a0, EmArea03ArrivalFault *fault);
int em_area03_arrival_00825980(const EmArea03ArrivalHooks *h, uint32_t a0, EmArea03ArrivalFault *fault);
int em_area03_arrival_00825DF0(const EmArea03ArrivalHooks *h, uint32_t a0, EmArea03ArrivalFault *fault);
int em_area03_arrival_00826340(const EmArea03ArrivalHooks *h, uint32_t a0, EmArea03ArrivalFault *fault);
int em_area03_arrival_008265D0(const EmArea03ArrivalHooks *h, uint32_t a0, EmArea03ArrivalFault *fault);
#ifdef __cplusplus
}
#endif
#endif
