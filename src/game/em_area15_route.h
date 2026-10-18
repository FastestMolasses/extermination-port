/* AREA15 route owners omitted by the earlier delta inventory. UNWIRED.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >=0 and fail at <0. A latched
 * fault prevents all later observable accesses/calls. No owned game data.
 * Evidence: tools/test_area15_route_reference.py. */
#ifndef EM_AREA15_ROUTE_H
#define EM_AREA15_ROUTE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmArea15RouteFault;
enum { EM_AREA15_ROUTE_FAULT_NONE = 0, EM_AREA15_ROUTE_FAULT_NULL_WORKER = 1,
       EM_AREA15_ROUTE_FAULT_WORKER_FAILED = 2, EM_AREA15_ROUTE_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001AEDE0)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001B17A0)(void *ctx, uint32_t a0);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1C0)(void *ctx, uint32_t a0, int32_t a1, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BA580)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001C4760)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001C47A0)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float a1, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t a1, float a2, float a3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001CA6F0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001FABB0)(void *ctx);
    int (*w_001FAE70)(void *ctx, int32_t a0);
    int (*w_001FBC50)(void *ctx);
    int (*w_00823E40)(void *ctx, uint32_t a0);
/* END GENERATED HOOKS */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t self);
} EmArea15RouteHooks;
int em_area15_route_00823B40(const EmArea15RouteHooks *h, uint32_t a0, EmArea15RouteFault *fault);
int em_area15_route_00823C80(const EmArea15RouteHooks *h, uint32_t a0, EmArea15RouteFault *fault);
int em_area15_route_00823F60(const EmArea15RouteHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result, EmArea15RouteFault *fault);
int em_area15_route_00824240(const EmArea15RouteHooks *h, uint32_t a0, EmArea15RouteFault *fault);
int em_area15_route_00824350(const EmArea15RouteHooks *h, uint32_t a0, EmArea15RouteFault *fault);
int em_area15_route_008243E0(const EmArea15RouteHooks *h, uint32_t a0, EmArea15RouteFault *fault);
#ifdef __cplusplus
}
#endif
#endif
