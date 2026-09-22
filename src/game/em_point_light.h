#ifndef EM_POINT_LIGHT_H
#define EM_POINT_LIGHT_H

#include <stdint.h>

/* Original render-context slots at +220 and staging slots at +1220. */
#define EM_POINT_LIGHT_COUNT 32
typedef struct {
    float multiplier, adder;
    int32_t type, handle;
    float position[4], color[4], angle[4], matrix[16];
} EmPointLight;

typedef struct {
    uint32_t next_handle;
    int32_t pending_count;
    EmPointLight active[EM_POINT_LIGHT_COUNT];
    EmPointLight pending[EM_POINT_LIGHT_COUNT];
} EmPointLightPool;

typedef uint32_t (*EmPointLightRandom)(void *context);

/* Reset follows 001D7BB0's field writes; caller zero-initializes a new pool. */
void em_point_light_reset(EmPointLightPool *pool);
int32_t em_point_light_register(EmPointLightPool *pool, const float position[4],
                              const float color[4], int32_t type,
                              float multiplier, float adder);
void em_point_light_tick(EmPointLightPool *pool, uint16_t area_key,
                         EmPointLightRandom random, void *context);
/* Generated EMLP contains original positions and unscaled color presets. */
int em_point_light_load(EmPointLightPool *pool, uint16_t *area_key,
                        const char *path);

/* dir initially carries the weighted camera fill; color its unweighted RGB.
 * Original 001D8340 folds every active slot, then normalizes dir.xyz. */
void em_point_light_fold(float dir[4], float color[4],
                        const EmPointLightPool *pool, const float anchor[4]);

#endif
