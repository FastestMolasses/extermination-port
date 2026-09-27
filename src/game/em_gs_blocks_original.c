/* em_gs_blocks_original.c - 001D0F20's blend-preset bank
 * (em_gs_blocks_original.h). */
#include "game/em_gs_blocks_original.h"

#include <stddef.h>

static void w32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void w64(uint8_t *p, uint64_t v)
{
    w32(p, (uint32_t)v);
    w32(p + 4, (uint32_t)(v >> 32));
}

int em_gs_blocks_001D0F20_presets(uint8_t *bank)
{
    /* The per-preset TEST_1 and ALPHA_1 values (the loop's jump table on i). */
    static const uint64_t test[10] = { 0x5000D, 0x53001, 0x53001, 0x5000D, 0x53001,
                                       0x5C00D, 0x5C00D, 0x50003, 0x52001, 0x51001 };
    static const uint64_t alpha[10] = {
        (UINT64_C(0x80) << 32) | 0xA8, 0x44, (UINT64_C(0x80) << 32) | 0x68, (UINT64_C(0x80) << 32) | 0x62,
        0x49, 0x49, 0x44, (UINT64_C(0x80) << 32) | 0xA8, (UINT64_C(0x80) << 32) | 0xA8,
        (UINT64_C(0x80) << 32) | 0xA9 };
    if (!bank) return -1;
    for (unsigned i = 0; i < 10u; ++i) {
        uint8_t *p = bank + 0x80u * i;
        w32(p + 0x00, 0);
        w32(p + 0x04, 0);
        w32(p + 0x08, 0x11000000u);                         /* FLUSH */
        w32(p + 0x0C, 0x50000007u);                         /* DIRECT 7 */
        w64(p + 0x10, (UINT64_C(0x10000000) << 32) | 0x8006u);   /* NLOOP 6, EOP, NREG 1 */
        w64(p + 0x18, 0xE);                                 /* A+D */
        w64(p + 0x20, 0x17E);  w64(p + 0x28, 0x00);         /* PRIM */
        w64(p + 0x30, 0x60);   w64(p + 0x38, 0x14);         /* TEX1_1 */
        w64(p + 0x60, 0);      w64(p + 0x68, 0x08);         /* CLAMP_1 */
        w64(p + 0x70, 1);      w64(p + 0x78, 0x46);         /* COLCLAMP */
        w64(p + 0x48, 0x47);                                /* TEST_1 */
        w64(p + 0x58, 0x42);                                /* ALPHA_1 */
        w64(p + 0x40, test[i]);
        w64(p + 0x50, alpha[i]);
    }
    return 0;
}
