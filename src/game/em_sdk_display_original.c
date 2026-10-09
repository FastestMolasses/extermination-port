/* em_sdk_display_original.c - see em_sdk_display_original.h. Every branch
 * and store cites the original address it comes from. */
#include "game/em_sdk_display_original.h"

static int16_t half(const uint8_t *p, unsigned at) { return (int16_t)(uint16_t)(p[at] | p[at + 1] << 8); }
static uint64_t rd64(const uint8_t *p, unsigned at)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8u; ++i) v |= (uint64_t)p[at + i] << (8u * i);
    return v;
}
static void wr64(uint8_t *p, unsigned at, uint64_t v)
{
    for (unsigned i = 0; i < 8u; ++i) p[at + i] = (uint8_t)(v >> (8u * i));
}

/* A 32-bit EE result as the 64-bit register holds it (sign-extended). */
static uint64_t sx(int32_t v) { return (uint64_t)(int64_t)v; }

int em_sdk_001002E0(const uint8_t mode[EM_SDK_GS_MODE_SIZE], uint8_t env[EM_SDK_DISPENV_SIZE],
                    int32_t psm, int32_t width, int32_t height, int32_t x, int32_t y)
{
    if (!mode || !env) return -1;
    /* 0x1002E4..0x10032C: the five arguments, each its low halfword
     * sign-extended. */
    const int32_t p = (int16_t)(uint16_t)psm, w = (int16_t)(uint16_t)width;
    const int32_t h = (int16_t)(uint16_t)height, ox = (int16_t)(uint16_t)x;
    const int32_t oy = (int16_t)(uint16_t)y;
    wr64(env, 0x00, 0x66);                                        /* 0x100338: PMODE */
    /* 0x10033C..0x100360: SMODE2 = 2 (mode[0] == 0), else 3 (mode[2] != 0)
     * or 1. */
    wr64(env, 0x08, half(mode, 0) == 0 ? 2u : half(mode, 4) != 0 ? 3u : 1u);
    /* 0x100364..0x100384: DISPFB = PSM << 15 | FBW << 9. */
    wr64(env, 0x10, (uint64_t)(uint32_t)(p & 0xF) << 15 | (uint64_t)(uint32_t)(((w + 0x3F) >> 6) & 0x3F) << 9);
    /* 0x100388 / 0x100434: the video mode (2 NTSC, 3 PAL; any other calls
     * the message printer 00122B58 at 0x100520). */
    const int16_t video = half(mode, 2);
    if (video != 2 && video != 3) return -1;
    /* 0x1003A4 / 0x100448: the interlace halfword (lhu, sign-extended)
     * equal to 1 selects the field-mode origins. */
    const int interlaced = half(mode, 0) == 1;
    /* 0x1003AC (and its three twins): magh = (width + 0x9FF) / width, the
     * division's break 7 on a zero width. */
    if (w == 0) return -1;
    const int32_t magh = (w + 0x9FF) / w;
    const int32_t x0 = video == 2 ? 0x27C : 0x290;
    int32_t dy, dh;
    if (interlaced) {
        dy = (oy + (video == 2 ? 0x32 : 0x48)) & 0xFFF;
        /* 0x1003C0 / 0x100468: DH = height * 2 - 1 when mode[2] != 0. */
        dh = half(mode, 4) != 0 ? h * 2 - 1 : h - 1;
    } else {
        dy = (oy + (video == 2 ? 0x19 : 0x24)) & 0xFFF;
        dh = h - 1;
    }
    /* DX = (x * magh + origin) & 0xFFF (the product sign-extended, a 64-bit
     * add); MAGH = magh - 1 << 23; DW = magh * width - 1 << 32; DH << 44:
     * each 32-bit result shifted as its sign-extended 64-bit register (the
     * halfword arguments keep every product inside 32 bits). */
    const uint64_t dx = (sx(ox * magh) + (uint64_t)x0) & 0xFFFu;
    const uint64_t mag = sx(magh - 1) << 23;
    const uint64_t dw = sx(magh * w - 1) << 32;
    wr64(env, 0x18, dx | (uint64_t)(uint32_t)dy << 12 | mag | dw | sx(dh) << 44);  /* 0x1004C0 / 0x10051C */
    wr64(env, 0x20, 0);                                           /* 0x100528: BGCOLOR */
    return 0;
}

int em_sdk_00100550(const uint8_t mode[EM_SDK_GS_MODE_SIZE], const uint8_t env[EM_SDK_DISPENV_SIZE],
                    EmSdkGsStore store, void *ctx)
{
    if (!mode || !env || !store) return -1;
    /* 0x100564: the GS revision halfword equal to 1 selects circuit 1's
     * registers (and EXTDATA for the fifth dword); otherwise circuit 2's. */
    static const uint32_t rev1[4] = {EM_SDK_GS_PMODE, EM_SDK_GS_DISPFB1, EM_SDK_GS_DISPLAY1, EM_SDK_GS_EXTDATA};
    static const unsigned rev1_at[4] = {0x00, 0x10, 0x18, 0x20};
    static const uint32_t other[5] = {EM_SDK_GS_PMODE, EM_SDK_GS_SMODE2, EM_SDK_GS_DISPFB2, EM_SDK_GS_DISPLAY2,
                                      EM_SDK_GS_BGCOLOR};
    if (half(mode, 6) == 1) {
        for (unsigned i = 0; i < 4u; ++i)                         /* 0x100580..0x1005A8 */
            if (store(ctx, rev1[i], rd64(env, rev1_at[i])) < 0) return -1;
        return 0;
    }
    for (unsigned i = 0; i < 5u; ++i)                             /* 0x1005C0..0x1005F8 */
        if (store(ctx, other[i], rd64(env, 8u * i)) < 0) return -1;
    return 0;
}
