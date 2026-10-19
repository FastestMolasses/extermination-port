/* AREA13 route owners omitted by the earlier delta inventory. UNWIRED.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >=0 and fail at <0. A latched
 * fault prevents all later observable accesses/calls. No owned game data.
 * Evidence: tools/test_area13_route_reference.py. */
#ifndef EM_AREA13_ROUTE_H
#define EM_AREA13_ROUTE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmArea13RouteFault;
enum { EM_AREA13_ROUTE_FAULT_NONE = 0, EM_AREA13_ROUTE_FAULT_NULL_WORKER = 1,
       EM_AREA13_ROUTE_FAULT_WORKER_FAILED = 2, EM_AREA13_ROUTE_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_001AEDB0)(void *ctx, int32_t a0);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B1020)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, int32_t a3);
    int (*w_001B1EA0)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t *result);
    int (*w_001B6660)(void *ctx, uint32_t a0);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1C0)(void *ctx, uint32_t a0, int32_t a1, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BBDA0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BBE40)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result);
    int (*w_001BC0E0)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001BC240)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BC290)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001BC300)(void *ctx, uint32_t a0);
    int (*w_001C4760)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001EFD20)(void *ctx, int32_t a0, uint32_t a1);
    int (*w_001FAE70)(void *ctx, int32_t a0);
/* END GENERATED HOOKS */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t self);
} EmArea13RouteHooks;
int em_area13_route_00823580(const EmArea13RouteHooks *, uint32_t self, EmArea13RouteFault *);
int em_area13_route_00823830(const EmArea13RouteHooks *, uint32_t self, EmArea13RouteFault *);
int em_area13_route_00823A40(const EmArea13RouteHooks *, uint32_t self, EmArea13RouteFault *);
int em_area13_route_00824A80(const EmArea13RouteHooks *, uint32_t self, EmArea13RouteFault *);
#ifdef __cplusplus
}
#endif
#endif
