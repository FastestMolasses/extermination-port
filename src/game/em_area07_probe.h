/* AREA07's turret probe 0x825470 (func_overlay_AREA07_00825430.c), the AREA03
 * twin with its own tint; tools/test_area07_route_reference.py. */
#ifndef EM_AREA07_PROBE_H
#define EM_AREA07_PROBE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmArea07ProbeFault;
enum { EM_AREA07_PROBE_FAULT_NONE = 0, EM_AREA07_PROBE_FAULT_NULL_WORKER = 1,
       EM_AREA07_PROBE_FAULT_WORKER_FAILED = 2, EM_AREA07_PROBE_FAULT_BAD_ADDRESS = 5 };
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
} EmArea07ProbeHooks;
int em_area07_probe_00825470(const EmArea07ProbeHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, int32_t *result, EmArea07ProbeFault *fault);
#ifdef __cplusplus
}
#endif
#endif
