/* Actual Metal registry, isolated from frames and windows. Synthetic pixels only. */
#include <assert.h>
#include "gfx/metal/em_gfx_metal.m"

static void expect_pixel(const EmGfx *g, uint64_t key, const uint8_t rgba[4])
{
    const struct EmGfxObjectTex *slot = object_texture(g, key);
    uint8_t got[4] = {0};
    assert(slot && slot->tex && slot->width == 1 && slot->height == 1);
    [slot->tex getBytes:got bytesPerRow:4 fromRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0];
    assert(memcmp(got, rgba, 4) == 0);
}

int main(void)
{
    @autoreleasepool {
        EmGfx *g = calloc(1, sizeof *g);
        assert(g);
        g->device = MTLCreateSystemDefaultDevice();
        assert(g->device && "Metal device required; this fixture opens no window");
        const uint8_t first[4] = {17, 39, 81, 128}, second[4] = {5, 7, 11, 255};
        uint64_t keys[EM_GFX_OBJECT_TEX_MAX];
        uint8_t buckets[EM_GFX_OBJECT_TEX_BUCKETS] = {0};
        unsigned collisions = 0;
        assert(em_gfx_world_textures_reset(NULL) == -1);
        assert(em_gfx_gs_texture(g, 0, first, 1, 1) == 0);
        keys[0] = 0; buckets[object_texture_bucket(0)] = 1;
        for (unsigned i = 1; i < EM_GFX_OBJECT_TEX_MAX; ++i) {
            keys[i] = (UINT64_C(1) << 34) | i;
            uint32_t bucket = object_texture_bucket(keys[i]);
            collisions += buckets[bucket] != 0;
            buckets[bucket] = 1;
            int rc = (i & 1) ? em_gfx_object_texture(g, keys[i], first, 1, 1)
                             : em_gfx_gs_texture(g, keys[i], first, 1, 1);
            assert(rc == 0 && g->objTexCount == i + 1);
        }
        assert(collisions > 0 && EM_GFX_OBJECT_TEX_MAX > 512);
        for (unsigned i = 0; i < EM_GFX_OBJECT_TEX_MAX; ++i) {
            assert(object_texture(g, keys[i]) == &g->objTex[i]);
            expect_pixel(g, keys[i] | (UINT64_C(7) << 61), first);
        }
        uint64_t missing = (UINT64_C(1) << 34) | EM_GFX_OBJECT_TEX_MAX;
        assert(!object_texture(g, missing));
        assert(em_gfx_gs_texture(g, missing, first, 1, 1) == -1);
        assert(em_gfx_object_texture(g, missing, first, 1, 1) == -1);
        assert(g->objTexCount == EM_GFX_OBJECT_TEX_MAX && !object_texture(g, missing));
        assert(em_gfx_object_texture(g, keys[1] | (UINT64_C(7) << 61), second, 1, 1) == 0);
        assert(em_gfx_gs_texture(g, UINT64_C(7) << 61, second, 1, 1) == 0);
        assert(g->objTexCount == EM_GFX_OBJECT_TEX_MAX);
        for (unsigned i = 0; i < EM_GFX_OBJECT_TEX_MAX; ++i)
            expect_pixel(g, keys[i], i < 2 ? second : first);
        assert(em_gfx_world_textures_reset(g) == 0 && g->objTexCount == 0);
        for (unsigned i = 0; i < EM_GFX_OBJECT_TEX_BUCKETS; ++i) assert(!g->objTexIndex[i]);
        for (unsigned i = 0; i < EM_GFX_OBJECT_TEX_MAX; ++i) assert(!object_texture(g, keys[i]));
        for (unsigned i = EM_GFX_OBJECT_TEX_MAX; i > 0; --i)
            assert(em_gfx_gs_texture(g, keys[i-1], second, 1, 1) == 0);
        for (unsigned i = 0; i < EM_GFX_OBJECT_TEX_MAX; ++i) expect_pixel(g, keys[i], second);
        assert(em_gfx_world_textures_reset(g) == 0);
        /* A collision across the two key halves must still compare all 64 bits. */
        uint64_t high_key = keys[1] ^ (UINT64_C(1) << 42) ^ (UINT64_C(1) << 10);
        assert(high_key != keys[1] && object_texture_bucket(high_key) == object_texture_bucket(keys[1]));
        assert(em_gfx_object_texture(g, keys[1], first, 1, 1) == 0);
        assert(em_gfx_gs_texture(g, high_key, second, 1, 1) == 0 && g->objTexCount == 2);
        expect_pixel(g, keys[1], first); expect_pixel(g, high_key, second);
        assert(object_texture(g, keys[1]) != object_texture(g, high_key));
        assert(em_gfx_world_textures_reset(g) == 0);
        id<MTLDevice> device = g->device;
        g->device = nil;
        assert(em_gfx_gs_texture(g, missing, first, 1, 1) == -1);
        assert(g->objTexCount == 1 && object_texture(g, missing)->tex == nil);
        g->device = device;
        assert(em_gfx_gs_texture(g, missing, second, 1, 1) == 0 && g->objTexCount == 1);
        expect_pixel(g, missing, second);
        assert(em_gfx_world_textures_reset(g) == 0);
        [g->device release];
        free(g);
        printf("PASS Metal texture registry: %u slots, %u collisions, replacement/reset/refill/failure retry\n",
               EM_GFX_OBJECT_TEX_MAX, collisions);
    }
    return 0;
}
