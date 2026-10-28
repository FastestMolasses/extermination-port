/* Complete area dispatcher from decomp C; standalone and UNWIRED.
 * Memory views are borrowed until the next access. Hooks return >=0 on
 * success, <0 on failure. Faults latch and stop all observable activity. */
#ifndef EM_LEVEL5_STATE_H
#define EM_LEVEL5_STATE_H
#include <stdint.h>
typedef struct { uint32_t address; int32_t code; } EmLevel5StateFault;
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
    int (*w_00823580)(void *ctx, uint32_t key, uint32_t area);
    int (*w_008236E0)(void *ctx, uint32_t key, uint32_t area);
    int (*w_00823740)(void *ctx, uint32_t key, uint32_t area);
    int (*w_008237C0)(void *ctx, uint32_t key, uint32_t area);
    int (*w_00823900)(void *ctx, uint32_t key, uint32_t area);
    int (*w_00823970)(void *ctx, uint32_t key, uint32_t area);
    int (*w_00823990)(void *ctx, uint32_t key, uint32_t area);
    int (*w_00823A50)(void *ctx, uint32_t key, uint32_t area);
    int (*w_00823B10)(void *ctx, uint32_t key, uint32_t area);
    int (*w_00823B80)(void *ctx, uint32_t key, uint32_t area);
    int (*w_008241F0)(void *ctx, uint32_t key, uint32_t area);
    int (*w_00824290)(void *ctx, uint32_t key, uint32_t area);
    int (*w_00824420)(void *ctx, uint32_t key, uint32_t area);
    int (*w_00824E00)(void *ctx, uint32_t key, uint32_t area);
    int (*w_008250C0)(void *ctx, uint32_t key, uint32_t area);
    int (*w_00826180)(void *ctx, uint32_t key, uint32_t area);
    int (*w_001F0310)(void *ctx);
} EmLevel5StateHooks;
int em_level5_state_001E7780(const EmLevel5StateHooks *h, EmLevel5StateFault *fault);
int em_level5_state_001D0660(const EmLevel5StateHooks *h, EmLevel5StateFault *fault);
#endif
