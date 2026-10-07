#include "game/em_player_target_live.h"
#include <string.h>

int pt_call(const uint8_t *player_bytes, const uint8_t *object_bytes, uint32_t address,
            uint32_t radius_bits, uint32_t *outputs)
{
    EmActor object = {0};
    EmPlayerLiveActor player = {0};
    EmPlayerRunningJumpTarget target;
    int32_t mode = 1;
    EmSdkMathContext sdk = {.world = {.d26C5D0 = &mode}};
    memcpy(player.bytes, player_bytes, sizeof player.bytes);
    object.cls = object_bytes[2];
    object.model = object_bytes[3];
    memcpy(&object.w34, object_bytes + 0x34, 4);
    memcpy(object.pos, object_bytes + 0xB0, sizeof object.pos);
    if (em_player_target_live_entry(&object, &target) < 0) return -1;
    outputs[0] = target.flags;
    outputs[1] = target.type;
    outputs[2] = (uint32_t)(int32_t)target.field34;
    float x, z, radius;
    if (em_player_target_live_xz(&object, &x, &z) < 0 ||
        em_player_target_live_radius(address, &object, &radius) < 0) return -1;
    memcpy(outputs + 3, &x, 4);
    memcpy(outputs + 4, &z, 4);
    memcpy(outputs + 5, &radius, 4);
    memcpy(&radius, &radius_bits, 4);
    int result = -1;
    if (em_player_target_live_sight(&sdk, &player, address, &object, radius,
                                    outputs + 6, &result) < 0) return -1;
    outputs[7] = (uint32_t)result;
    if (memcmp(player.bytes, player_bytes, sizeof player.bytes) ||
        memcmp(object.pos, object_bytes + 0xB0, sizeof object.pos)) return -1;
    return 0;
}
