/* AREA03 route owners omitted by the earlier delta inventory. Bound by em_area01_runtime_area15.inc.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >=0 and fail at <0. A latched
 * fault prevents all later observable accesses/calls. No owned game data.
 * Evidence: tools/test_area03_route_reference.py. */
#ifndef EM_AREA03_ROUTE_H
#define EM_AREA03_ROUTE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmArea03RouteFault;
enum { EM_AREA03_ROUTE_FAULT_NONE = 0, EM_AREA03_ROUTE_FAULT_NULL_WORKER = 1,
       EM_AREA03_ROUTE_FAULT_WORKER_FAILED = 2, EM_AREA03_ROUTE_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
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
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001EFD90)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2, int32_t *result);
    int (*w_001EFE00)(void *ctx, int32_t a0, uint32_t a1, int32_t *result);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, float a3, int32_t *result);
    int (*w_00824D30)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_00825200)(void *ctx, uint32_t a0);
/* END GENERATED HOOKS */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t self);
} EmArea03RouteHooks;
int em_area03_route_00823580(const EmArea03RouteHooks *h, EmArea03RouteFault *fault);
int em_area03_route_00823930(const EmArea03RouteHooks *h, uint32_t a0, EmArea03RouteFault *fault);
int em_area03_route_00825290(const EmArea03RouteHooks *h, uint32_t a0, EmArea03RouteFault *fault);
int em_area03_route_00826270(const EmArea03RouteHooks *h, int32_t *result, EmArea03RouteFault *fault);
int em_area03_route_00826330(const EmArea03RouteHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result, EmArea03RouteFault *fault);
int em_area03_route_00826860(const EmArea03RouteHooks *h, int32_t *result, EmArea03RouteFault *fault);
int em_area03_route_00826880(const EmArea03RouteHooks *h, uint32_t a0, int32_t *result, EmArea03RouteFault *fault);
int em_area03_route_008268B0(const EmArea03RouteHooks *h, uint32_t a0, int32_t *result, EmArea03RouteFault *fault);
#ifdef __cplusplus
}
#endif
#endif
