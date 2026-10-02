#include "game/em_aim_fire_leaves.h"
#include "game/em_ee_float.h"
uint32_t em_aim_fire_001839A0(uint8_t kind)
{
    switch (kind) {
    case 0x28: case 0x4F: case 0x34: case 0x33: return 0x100;
    case 0x51: return 0x300;
    case 0x1B: case 0x2B: return 0x200;
    case 0x2A: case 0x18: case 0x0E: case 0x0C: case 0x0A: return 0x10;
    case 0x50: case 0x1E: case 0x1F: case 0x1C: case 0x06: return 0;
    default: return 0x101;
    }
}
uint32_t em_aim_fire_001B1510(uint32_t angle)
{
    while (!em_ee_c_le_bits(angle, 0x40C90FDB))
        angle = em_ee_sub_bits(angle, 0x40C90FDB);
    return angle;
}
uint32_t em_aim_fire_001AA7A0(const uint32_t node_c0[3], const uint32_t pos[3], int *hit)
{
    *hit = 0;
    const uint32_t dx = em_ee_sub_bits(node_c0[0], pos[0]), dz = em_ee_sub_bits(node_c0[2], pos[2]);
    const uint32_t sum = em_ee_madd_bits(em_ee_mula_bits(dx, dx), dz, dz);
    if (!em_ee_c_le_bits(sum, 0x41C80000u)) return 0;                     /* 25.0 */
    if (!em_ee_c_le_bits(node_c0[1], pos[1])) return 0;
    if (em_ee_c_lt_bits(node_c0[1], em_ee_sub_bits(pos[1], 0x42340000u))) return 0;   /* 45.0 */
    *hit = 1;
    return 1;
}
