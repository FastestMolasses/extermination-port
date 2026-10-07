/* Bind the sole 001AA410 / 001AA2A0 owners to the canonical pool fields,
 * in-stage player record and running-jump scratch. No target record or
 * persistent position/distance copy is introduced. */
#include "game/em_player_target_live.h"
#include "game/em_level14_port.h"

#include <string.h>

typedef struct {
    EmSdkMathContext *sdk;
    EmPlayerLiveActor *player;
    uint32_t address;
    const EmActor *object;
    uint32_t *distance;
} TargetView;

static uint8_t *target_bytes(void *context, uint32_t address, uint32_t size)
{
    TargetView *v = context;
    if (!size || address > UINT32_MAX - size) return NULL;
    if (address == v->address + 3u && size == 1)
        return (uint8_t *)&v->object->model;
    if (address >= v->address + 0xB0u && address + size <= v->address + 0xBCu)
        return (uint8_t *)v->object->pos + address - v->address - 0xB0u;
    if (v->player && address >= 0x00810350u && address + size <= 0x0081035Cu)
        return v->player->bytes + address - 0x008102B0u;
    if (v->distance && address == 0x70003A20u && size == 4)
        return (uint8_t *)v->distance;
    return NULL;
}

static int target_sqrt(void *context, float value, float *result)
{
    TargetView *v = context;
    if (!v->sdk || !result || v->sdk->fault) return -1;
    *result = em_sdk_math_original_float_0011E748(v->sdk, value);
    return v->sdk->fault ? -1 : 0;
}

int em_player_target_live_entry(const EmActor *object, EmPlayerRunningJumpTarget *out)
{
    if (!object || !out) return -1;
    out->object = object;
    out->flags = object->cls;
    out->type = object->model;
    out->field34 = (object->cls & 0x1Fu) == 2u ? (int16_t)(uint16_t)object->w34 : 0;
    return 0;
}

int em_player_target_live_xz(const EmActor *object, float *x, float *z)
{
    if (!object || !x || !z) return -1;
    memcpy(x, object->pos, 4);
    memcpy(z, object->pos + 2, 4);
    return 0;
}

int em_player_target_live_radius(uint32_t address, const EmActor *object, float *radius)
{
    if (!address || address > UINT32_MAX - EM_ACTOR_RECORD_SIZE || !object || !radius) return -1;
    TargetView view = {.address = address, .object = object};
    EmLevel14PortHooks hooks = {.ctx = &view, .bytes = target_bytes};
    EmLevel14PortFault fault = {0};
    return em_level14_port_001AA410(&hooks, address, radius, &fault);
}

int em_player_target_live_sight(EmSdkMathContext *sdk, EmPlayerLiveActor *player,
                                uint32_t address, const EmActor *object, float radius,
                                uint32_t *distance, int *result)
{
    if (!sdk || !player || !address || address > UINT32_MAX - EM_ACTOR_RECORD_SIZE ||
        !object || !distance || !result) return -1;
    TargetView view = {sdk, player, address, object, distance};
    EmLevel14PortHooks hooks = {.ctx = &view, .bytes = target_bytes, .w_0011E748 = target_sqrt};
    EmLevel14PortFault fault = {0};
    int32_t original_result = 0;
    int status = em_level14_port_001AA2A0(&hooks, 0x008102B0u, address, radius,
                                         &original_result, &fault);
    if (status >= 0) *result = original_result;
    return status;
}
