/* em_gs_blocks_original.c - 001D0F20's GS register blocks and the SDK
 * template fills they copy (em_gs_blocks_original.h). */
#include "game/em_gs_blocks_original.h"

#include <stddef.h>
#include <string.h>

#include "game/em_load_veil_particles.h"

typedef uint32_t u32;
typedef uint64_t u64;

static void w32(uint8_t *p, u32 v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void w64(uint8_t *p, u64 v)
{
    w32(p, (u32)v);
    w32(p + 4, (u32)(v >> 32));
}

static u64 r64(const uint8_t *p)
{
    u64 v = 0;
    for (int i = 7; i >= 0; --i)
        v = v << 8 | p[i];
    return v;
}

/* The SDK's short arguments: sll 16 then sra 16. */
static int32_t sext16(u32 x) { return (int32_t)(int16_t)(uint16_t)(x & 0xFFFFu); }
/* A sign-extended 32-bit register as its 64-bit image. */
static u64 sx(int32_t x) { return (u64)(int64_t)x; }
/* sra on a 32-bit register. */
static int32_t sra32(int32_t x, unsigned n) { return x < 0 ? ~(~x >> n) : x >> n; }

/* em_load_veil_particles' 001006D8 / 00100610 over a descriptor that is not
 * DMA memory: its window is the descriptor itself at a nominal address (the
 * original's is 001D0F20's stack frame, which no byte of the blocks names). */
#define DBUFF_NOMINAL 0x70000000u

static void sdk_world(EmLoadVeilParticles *s, uint8_t *bytes, u32 size, const uint8_t d00241010[8])
{
    memset(s, 0, sizeof *s);
    s->world.packet = bytes;
    s->world.packet_address = DBUFF_NOMINAL;
    s->world.packet_size = size;
    s->world.d00241010 = d00241010;
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

/* ------------------------------------------------------------ 001008C0 */

int em_gs_blocks_001008C0(uint8_t *clear, int32_t ztest, int32_t x, int32_t y, int32_t w, int32_t h,
                          uint32_t r, uint32_t g, uint8_t b, uint8_t a, uint32_t z)
{
    if (!clear) return -1;
    const int32_t sx0 = sext16((u32)x), sy0 = sext16((u32)y);
    const int32_t sx1 = sx0 + sext16((u32)w), sy1 = sy0 + sext16((u32)h);
    const int32_t zt = sext16((u32)ztest);
    /* The corners: X and Y shifted into 12.4 as 32-bit words, then the
     * 64-bit shifts of the register image (sign-extended). */
    const u64 xy0 = sx((int32_t)((u32)sx0 << 4)) | sx((int32_t)((u32)sy0 << 4)) << 16;
    const u64 xy1 = sx((int32_t)((u32)sx1 << 4)) | sx((int32_t)((u32)sy1 << 4)) << 16;
    const u64 zw = (u64)z << 32;
    const u64 rgbaq = ((u64)(r & 0xFFu)) | ((u64)(g & 0xFFu) << 8) | ((u64)b << 16) | ((u64)a << 24) |
                      (UINT64_C(0xFE00) << 46);                  /* Q = 1.0 */
    w64(clear + 0x10, 6);        w64(clear + 0x18, 0x00);          /* PRIM: sprite */
    w64(clear + 0x28, 0x01);     w64(clear + 0x20, rgbaq);         /* RGBAQ */
    w64(clear + 0x30, xy0 | zw); w64(clear + 0x48, 0x05);
    w64(clear + 0x40, xy1 | zw);
    w64(clear + 0x58, 0x47);     w64(clear + 0x08, 0x47);          /* TEST_1 x 2 */
    w64(clear + 0x00, 0x30000);                                    /* Z always */
    w64(clear + 0x38, 0x05);                                       /* XYZ2 */
    w64(clear + 0x50, zt != 0 ? (u64)((u32)zt & 3u) << 17 | 0x10000u : 0x30000u);
    return 0;
}

/* ------------------------------------------------------------ 00101630 */

int em_gs_blocks_00101630(uint8_t *env, int32_t psm, int32_t w, int32_t h, int32_t ztest,
                          int32_t zpsm, const uint8_t d00241010[8])
{
    if (!env || !d00241010) return -1;
    const int32_t sw = sext16((u32)w), sp = sext16((u32)psm), sh = sext16((u32)h);
    const int32_t zt = sext16((u32)ztest), zp = sext16((u32)zpsm);
    EmLoadVeilParticles s;
    int32_t zbuf;
    /* FRAME_2: FBW = w >> 6 (sra of the halfword image by 22), PSM. */
    w64(env + 0x08, 0x4D);
    w64(env + 0x00, (u64)((u32)sra32(sw, 6) & 0x3Fu) << 16 | (u64)((u32)sp & 0xFu) << 24);
    w64(env + 0x18, 0x4F);
    sdk_world(&s, env, 0x80u, d00241010);
    if (em_load_veil_particles_00100610(&s, sp, sw, sh, &zbuf) < 0) return -1;
    u64 z = sx(sext16((u32)zbuf)) | (u64)((u32)zp & 0xFu) << 24;
    if (zt == 0)
        z |= UINT64_C(0x8000) << 17;                               /* ZMSK */
    w64(env + 0x10, z);
    const u64 offset = (u64)(0x800 - (int64_t)sext16((u32)sra32(sh, 1))) << 36 |
                       (u64)(0x800 - (int64_t)sext16((u32)sra32(sw, 1))) << 4;
    const u64 scissor = sx((int32_t)((u32)sw - 1u)) << 16 | sx((int32_t)((u32)sh - 1u)) << 48;
    w64(env + 0x28, 0x19);  w64(env + 0x20, offset);               /* XYOFFSET_2 */
    w64(env + 0x38, 0x41);  w64(env + 0x30, scissor);              /* SCISSOR_2 */
    w64(env + 0x48, 0x1A);  w64(env + 0x40, r64(env + 0x40) | 1u); /* PRMODECONT */
    w64(env + 0x58, 0x46);  w64(env + 0x50, r64(env + 0x50) | 1u); /* COLCLAMP */
    w64(env + 0x68, 0x45);                                         /* DTHE */
    const u64 d = r64(env + 0x60);
    w64(env + 0x60, (sp & 2) ? d | 1u : d & ~(u64)1u);
    w64(env + 0x78, 0x48);                                         /* TEST_2 */
    w64(env + 0x70, zt != 0 ? (u64)((u32)zt & 3u) << 17 | 0x10000u : 0x30000u);
    return 0;
}

/* ------------------------------------------------------------ 00101898 */

int em_gs_blocks_00101898(uint8_t *dbuff, int32_t psm, int32_t w, int32_t h, int32_t ztest,
                          int32_t zpsm, int32_t clear, const uint8_t d00241010[8])
{
    if (!dbuff || !d00241010) return -1;
    const int32_t sp = sext16((u32)psm), sw = sext16((u32)w), sh = sext16((u32)h);
    const int32_t zt = sext16((u32)ztest), zp = sext16((u32)zpsm), cl = sext16((u32)clear);
    EmLoadVeilParticles s;
    int32_t r;
    sdk_world(&s, dbuff, EM_GS_BLOCKS_DBUFF_SIZE, d00241010);
    /* 00100268 returns &D_00241010; 001002E0 x 2 fill +0x00 / +0x28 (the
     * display environments, not produced). */
    if (em_load_veil_particles_001006D8(&s, DBUFF_NOMINAL + 0x60u, sp, sw, sh, zt, zp, &r) < 0 ||
        em_gs_blocks_00101630(dbuff + 0xE0, sp, sw, sh, zt, zp, d00241010) < 0 ||
        em_load_veil_particles_001006D8(&s, DBUFF_NOMINAL + 0x1D0u, sp, sw, sh, zt, zp, &r) < 0 ||
        em_gs_blocks_00101630(dbuff + 0x250, sp, sw, sh, zt, zp, d00241010) < 0)
        return -1;
    if (cl != 0) {
        const int32_t x = 0x800 - sra32(sw, 1), y = 0x800 - sra32(sh, 1);
        if (em_gs_blocks_001008C0(dbuff + 0x160, zt, x, y, sw, sh, 0, 0, 0, 0, 0) < 0 ||
            em_gs_blocks_001008C0(dbuff + 0x2D0, zt, x, y, sw, sh, 0, 0, 0, 0, 0) < 0)
            return -1;
    }
    /* The two GIF tags: zeroed, then NLOOP 0x16 with the clear (0x10
     * without), EOP, NREG 1, REGS A+D. */
    for (unsigned k = 0; k < 2u; ++k) {
        uint8_t *t = dbuff + (k ? 0x1C0u : 0x50u);
        const u64 lo = ((u64)(cl != 0 ? 0x16u : 0x10u) | 0x8000u) | (UINT64_C(1) << 60);
        w64(t + 0x00, lo);
        w64(t + 0x08, 0xE);
    }
    /* The frame pointer of buffer 0: half the doubled Z-buffer base, when
     * the SDK mode dword (& 0xFFFF0000FFFF) is 0x100000001 or its low
     * halfword is 0. */
    int32_t zb;
    if (em_load_veil_particles_00100610(&s, sp, sw, sh, &zb) < 0) return -1;
    const u64 mode = r64(d00241010) & UINT64_C(0x0000FFFF0000FFFF);
    const int16_t low = (int16_t)(d00241010[0] | d00241010[1] << 8);
    if (mode == UINT64_C(0x100000001) || low == 0) {
        const u64 fbp = (u64)((u32)sra32(zb, 1) & 0x1FFu);
        w64(dbuff + 0xE0, (r64(dbuff + 0xE0) & ~(u64)0x1FF) | fbp);
        w64(dbuff + 0x60, (r64(dbuff + 0x60) & ~(u64)0x1FF) | fbp);
        /* +0x38 (display environment 1's DISPFB) gets the same FBP: not
         * produced. */
    }
    return 0;
}

/* ------------------------------------------------------------ 001D0F20 */

static void flush_direct(uint8_t *p, u32 direct)
{
    w32(p + 0x00, 0);
    w32(p + 0x04, 0);
    w32(p + 0x08, 0x11000000u);                  /* FLUSH */
    w32(p + 0x0C, 0x50000000u | direct);         /* DIRECT */
}

static void ad_tag(uint8_t *p, u32 nloop)
{
    w64(p + 0x00, (UINT64_C(0x10000000) << 32) | 0x8000u | nloop);
    w64(p + 0x08, 0xE);
}

int em_gs_blocks_001D0F20(uint8_t *blocks, const uint8_t d00241010[8])
{
    if (!blocks || !d00241010) return -1;
    uint8_t fb[EM_GS_BLOCKS_DBUFF_SIZE];
    memset(fb, 0, sizeof fb);
    if (em_gs_blocks_00101898(fb, 0, 0x200, 0xE0, 2, 0x31, 1, d00241010) < 0) return -1;
    w64(fb + 0x180, (u64)0x8000u << 16);
    w64(fb + 0x2F0, (u64)0x8000u << 16);
    /* ZBUF_1 of draw environment 0: its ZBP halfword and ZPSM byte. */
    const u64 zfields = ((u64)(fb[0x70] | fb[0x71] << 8) & 0x1FFu) | ((u64)(fb[0x73] & 0xFu) << 24);

    /* Bank A: the two templates (buffer 0 at fb + 0x50, buffer 1 at
     * fb + 0x1C0), 0x110 bytes each, then the tail registers. */
    for (unsigned i = 0; i < 2u; ++i) {
        uint8_t *p = blocks + 0x20u + 0x190u * i;
        memcpy(p + 0x10, fb + (i == 0 ? 0x50u : 0x1C0u), 0x110);
        flush_direct(p, 0x18);
        ad_tag(p + 0x10, 0x17);
        w64(p + 0x120, 0);     w64(p + 0x128, 0x08);                /* CLAMP_1 */
        w64(p + 0x130, 1);     w64(p + 0x138, 0x46);                /* COLCLAMP */
        w64(p + 0x140, 0);     w64(p + 0x148, 0x4A);                /* FBA_1 */
        w64(p + 0x150, 0);     w64(p + 0x158, 0x49);                /* PABE */
        w64(p + 0x160, 0);     w64(p + 0x168, 0x22);                /* SCANMSK */
        w64(p + 0x170, 0x60);  w64(p + 0x178, 0x14);                /* TEX1_1 */
        w64(p + 0x180, UINT64_C(1) << 32);  w64(p + 0x188, 0x3B);   /* TEXA */
    }
    /* Bank B: FOGCOL, data 0 (001D1C50 stores it per frame). */
    for (unsigned i = 0; i < 2u; ++i) {
        uint8_t *p = blocks + 0x340u + 0x30u * i;
        flush_direct(p, 2);
        ad_tag(p + 0x10, 1);
        w64(p + 0x20, 0);
        w64(p + 0x28, 0x3D);
    }
    /* Bank C: the clear of buffer 0's template, its first TEST_1 per clear. */
    for (unsigned i = 0; i < 2u; ++i) {
        uint8_t *p = blocks + 0x3A0u + 0x80u * i;
        memcpy(p + 0x20, fb + 0x160, 0x60);
        flush_direct(p, 7);
        ad_tag(p + 0x10, 6);
        w64(p + 0x20, i == 0 ? 0x32001u : 0x30000u);
    }
    /* Bank D: CLAMP_1 variants (the A+D pair at +0x20 is not written). */
    for (unsigned i = 0; i < 4u; ++i) {
        static const u64 clamp[4] = { 5, 0, (UINT64_C(0x37C) << 32) | 0x7FC00Au,
                                      (UINT64_C(0x3FC) << 32) | 0x3FC00Au };
        uint8_t *p = blocks + 0x4A0u + 0x40u * i;
        flush_direct(p, 3);
        ad_tag(p + 0x10, 2);
        w64(p + 0x30, clamp[i]);
        w64(p + 0x38, 0x08);
    }
    /* Bank E: TEST_1 and ZBUF_1 (ZMSK set for variants 0 and 2). The FLUSH
     * is the first word here. */
    for (unsigned i = 0; i < 4u; ++i) {
        static const u64 test[4] = { 0x3000D, 0x5000D, 0x5000D, 0x3000D };
        uint8_t *p = blocks + 0x5A0u + 0x40u * i;
        w32(p + 0x00, 0x11000000u);
        w32(p + 0x04, 0);
        w32(p + 0x08, 0);
        w32(p + 0x0C, 0x50000003u);
        ad_tag(p + 0x10, 2);
        w64(p + 0x20, test[i]);
        w64(p + 0x28, 0x47);
        w64(p + 0x30, zfields | ((i == 0 || i == 2) ? UINT64_C(1) << 32 : 0));
        w64(p + 0x38, 0x4E);
    }
    /* Bank F: the blend presets. */
    if (em_gs_blocks_001D0F20_presets(blocks + EM_GS_BLOCKS_PRESETS_OFFSET) < 0) return -1;
    /* Bank G: four passes (Z test mode, ZMSK) x ten presets. */
    for (unsigned k = 0; k < 4u; ++k) {
        static const u32 mode_of[4] = { 1, 2, 2, 1 }, field_of[4] = { 1, 0, 1, 0 };
        const u64 modebits = (u64)mode_of[k] << 17;
        const u64 ad1 = modebits | 0x13001u;
        for (unsigned i = 0; i < 10u; ++i) {
            static const u64 alpha[10] = {
                (UINT64_C(0x80) << 32) | 0xA8, 0x44, (UINT64_C(0x80) << 32) | 0x68,
                (UINT64_C(0x80) << 32) | 0x62, 0x49, 0x49, 0x44, (UINT64_C(0x80) << 32) | 0xA8,
                (UINT64_C(0x80) << 32) | 0xA8, (UINT64_C(0x80) << 32) | 0xA9 };
            u64 test;
            switch (i) {
            case 0: test = modebits | 0x1000Du; break;
            case 5: test = 0x5C00Du; break;
            case 6: test = modebits | 0x1C00Du; break;
            case 7: test = modebits | 0x10003u; break;
            case 8: test = modebits | 0x12001u; break;
            case 9: test = modebits | 0x11001u; break;
            default: test = ad1; break;
            }
            uint8_t *p = blocks + 0xBA0u + 0x5A0u * k + 0x90u * i;
            flush_direct(p, 8);
            ad_tag(p + 0x10, 7);
            w64(p + 0x20, 0x100); w64(p + 0x28, 0x00);                   /* PRIM */
            w64(p + 0x30, 0x60);  w64(p + 0x38, 0x14);                   /* TEX1_1 */
            w64(p + 0x40, test);  w64(p + 0x48, 0x47);                   /* TEST_1 */
            w64(p + 0x50, ((u64)field_of[k] << 32) | zfields);
            w64(p + 0x58, 0x4E);                                         /* ZBUF_1 */
            w64(p + 0x60, alpha[i]); w64(p + 0x68, 0x42);                /* ALPHA_1 */
            w64(p + 0x70, 0);     w64(p + 0x78, 0x08);                   /* CLAMP_1 */
            w64(p + 0x80, 1);     w64(p + 0x88, 0x46);                   /* COLCLAMP */
        }
    }
    /* The header. */
    w32(blocks + 0x00, 0);
    w32(blocks + 0x04, 0);
    w32(blocks + 0x08, 0);
    w32(blocks + 0x0C, 0x11000000u);
    w32(blocks + 0x10, 0x70000000u);
    w32(blocks + 0x14, 0);
    return 0;
}
