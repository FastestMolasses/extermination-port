/* AREA08's and AREA07 sub 0's overlay owners (lane P1, docs/AREA08_PORT.md),
 * translated from byte-identical decomp C. Bound by
 * em_area01_runtime_area08.inc. Original pointers are 32-bit; bytes returns a
 * borrowed view consumed before the next access or call. Workers succeed at
 * >= 0 and fail at < 0. A latched fault prevents all later observable accesses
 * and calls. No owned game data. Evidence: tools/test_area08_route_reference.py. */
#ifndef EM_AREA08_ROUTE_H
#define EM_AREA08_ROUTE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmArea08RouteFault;
enum { EM_AREA08_ROUTE_FAULT_NONE = 0, EM_AREA08_ROUTE_FAULT_NULL_WORKER = 1,
       EM_AREA08_ROUTE_FAULT_WORKER_FAILED = 2, EM_AREA08_ROUTE_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_00102760)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028E8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102918)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102B08)(void *ctx, uint32_t a0, uint32_t a1, float a2);
    int (*w_00102BB0)(void *ctx, uint32_t a0, uint32_t a1, float a2);
    int (*w_0011A070)(void *ctx, int32_t a0);
    int (*w_0011DE90)(void *ctx, float a0, float *result);
    int (*w_0011DF78)(void *ctx, float a0, float *result);
    int (*w_0011E2A8)(void *ctx, float a0, float *result);
    int (*w_0011E748)(void *ctx, float a0, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_00182F90)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0FD0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B10B0)(void *ctx, uint32_t a0, int32_t a1, int32_t a2);
    int (*w_001B1240)(void *ctx, uint32_t a0, float a1, float a2, float *result);
    int (*w_001B1470)(void *ctx, float a0, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0);
    int (*w_001B1B70)(void *ctx, uint32_t a0);
    int (*w_001B1DE0)(void *ctx, uint32_t a0);
    int (*w_001B2140)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B6660)(void *ctx, uint32_t a0, uint32_t *result);
    int (*w_001B6F00)(void *ctx, uint32_t a0, uint32_t a1, float a2);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1C0)(void *ctx, uint32_t a0, int32_t a1, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BA540)(void *ctx, uint32_t a0);
    int (*w_001BA580)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001BA8E0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001BB520)(void *ctx, uint32_t a0);
    int (*w_001BB560)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result);
    int (*w_001BB7C0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BB7F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BC150)(void *ctx, uint32_t a0);
    int (*w_001C4760)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float a1, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t a1, float a2, float a3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001CA6F0)(void *ctx, uint32_t a0, int32_t a1);
    int (*w_001CCF70)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001CD390)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001CFAE0)(void *ctx, uint32_t a0, int32_t a1, uint32_t a2, float a3, float a4, float a5, float a6);
    int (*w_001CFBE0)(void *ctx, int32_t a0, int32_t a1, uint32_t a2, uint32_t a3, int32_t a4);
    int (*w_001EFEB0)(void *ctx, int32_t a0, uint32_t a1, uint32_t *result);
    int (*w_001F4BF0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001FABB0)(void *ctx);
    int (*w_001FA790)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001FAE70)(void *ctx, int32_t a0);
    int (*w_001FB0B0)(void *ctx, int32_t a0);
    int (*w_001FB9F0)(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t a1, int32_t a2, float a3, int32_t *result);
    int (*w_001FBE80)(void *ctx, uint32_t a0, int32_t a1, float a2, float a3, int32_t *result);
/* END GENERATED HOOKS */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t self);
} EmArea08RouteHooks;
/* AREA08 */
int em_area08_route_008235E0(const EmArea08RouteHooks *h, uint32_t a0, int32_t *result, EmArea08RouteFault *fault);
int em_area08_route_00823630(const EmArea08RouteHooks *h, uint32_t a0, uint32_t a1, int32_t *result, EmArea08RouteFault *fault);
int em_area08_route_008237B0(const EmArea08RouteHooks *h, uint32_t a0, EmArea08RouteFault *fault);
int em_area08_route_00823870(const EmArea08RouteHooks *h, uint32_t a0, EmArea08RouteFault *fault);
int em_area08_route_00823AC0(const EmArea08RouteHooks *h, uint32_t a0, EmArea08RouteFault *fault);
int em_area08_route_00824210(const EmArea08RouteHooks *h, uint32_t a0, EmArea08RouteFault *fault);
int em_area08_route_00824460(const EmArea08RouteHooks *h, int32_t *result, EmArea08RouteFault *fault);
int em_area08_route_00824490(const EmArea08RouteHooks *h, uint32_t a0, EmArea08RouteFault *fault);
int em_area08_route_008245E0(const EmArea08RouteHooks *h, uint32_t a0, EmArea08RouteFault *fault);
int em_area08_route_00824790(const EmArea08RouteHooks *h, uint32_t a0, EmArea08RouteFault *fault);
int em_area08_route_00824870(const EmArea08RouteHooks *h, uint32_t a0, EmArea08RouteFault *fault);
int em_area08_route_008249F0(const EmArea08RouteHooks *h, uint32_t a0, EmArea08RouteFault *fault);
int em_area08_route_00824BC0(const EmArea08RouteHooks *h, uint32_t a0, uint32_t sp, EmArea08RouteFault *fault);
/* AREA07 (0x823DA0 is another function in AREA08's overlay) */
int em_area08_route_a07_00823DA0(const EmArea08RouteHooks *h, uint32_t a0, EmArea08RouteFault *fault);
#ifdef __cplusplus
}
#endif
#endif
