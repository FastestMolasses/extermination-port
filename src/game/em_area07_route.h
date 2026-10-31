/* AREA07's sub 2 turrets, from byte-identical decomp C and the AREA03 twin
 * em_area03_route_turret.c (the original instructions are the oracle:
 * tools/test_area07_route_reference.py). Bound by em_area01_runtime_area08.inc.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >= 0 and fail at < 0. */
#ifndef EM_AREA07_ROUTE_H
#define EM_AREA07_ROUTE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmArea07RouteFault;
enum { EM_AREA07_ROUTE_FAULT_NONE = 0, EM_AREA07_ROUTE_FAULT_NULL_WORKER = 1,
       EM_AREA07_ROUTE_FAULT_WORKER_FAILED = 2, EM_AREA07_ROUTE_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102760)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001028D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102958)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0011DBB8)(void *ctx, float a0, float *result);
    int (*w_0011DF78)(void *ctx, float a0, float *result);
    int (*w_0011E2A8)(void *ctx, float a0, float *result);
    int (*w_0011E520)(void *ctx, float a0, float *result);
    int (*w_0011E748)(void *ctx, float a0, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001A2370)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001AFA90)(void *ctx, int32_t a0, uint32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0FD0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1190)(void *ctx, int32_t a0);
    int (*w_001B11E0)(void *ctx, int32_t a0, int32_t *result);
    int (*w_001B1470)(void *ctx, float a0, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BA1C0)(void *ctx, uint32_t a0, int32_t a1, int32_t *result);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001EFD90)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2, int32_t *result);
    int (*w_001EFE00)(void *ctx, int32_t a0, uint32_t a1, int32_t *result);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, float a3, int32_t *result);
    int (*w_00825470)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_00825940)(void *ctx, uint32_t a0);
/* END GENERATED HOOKS */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t self);
} EmArea07RouteHooks;
int em_area07_route_00823F80(const EmArea07RouteHooks *h, uint32_t a0, EmArea07RouteFault *fault);
int em_area07_route_00825940(const EmArea07RouteHooks *h, uint32_t a0, uint32_t sp, EmArea07RouteFault *fault);
int em_area07_route_008259D0(const EmArea07RouteHooks *h, uint32_t a0, EmArea07RouteFault *fault);
#ifdef __cplusplus
}
#endif
#endif
