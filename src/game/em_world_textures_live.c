#include "game/em_world_textures_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLD_MASK (~(UINT64_C(7) << 61))
#define FILE_MAX (64u * 1024u * 1024u)

typedef struct {
    uint64_t tex0;
    uint32_t width, height;
    const uint8_t *pixels;
} Texture;

static struct {
    unsigned area, subarea;
    EmGfx *ready;
    int failed;
} S = {11u, 0u, NULL, 0};

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int supported(void)
{
    return S.subarea == 0u && (S.area == 11u || S.area == 1u);
}

static int fail(const char *what, const char *path)
{
    if (!S.failed)
        fprintf(stderr, "world textures: AREA%02u/%u: %s%s%s\n", S.area, S.subarea,
                what, path ? ": " : "", path ? path : "");
    S.failed = 1;
    return -1;
}

int em_world_textures_live_bind(unsigned area, unsigned subarea)
{
    S.area = area;
    S.subarea = subarea;
    S.ready = NULL;
    S.failed = 0;
    return supported() ? 0 : fail("no verified catalog for this delivery", NULL);
}

/* Parse all inputs before touching the GPU registry. Aliases between the
 * object/page catalogs must name identical pixels; CLD is an upload flag,
 * not part of the backend's texture key. The backend copies each buffer. */
static int catalog(const char *path, uint8_t **storage, Texture *textures, unsigned *count)
{
    FILE *f = fopen(path, "rb");
    if (!f) return fail("missing catalog", path);
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size < 16 || size > FILE_MAX || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return fail("invalid catalog size", path);
    }
    uint8_t *data = malloc((size_t)size);
    *storage = data;
    const int read_ok = data && fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!read_ok) return fail("cannot read catalog", path);
    if (memcmp(data, "EMOT", 4) || rd32(data + 4) != 1u || rd32(data + 12))
        return fail("invalid catalog header", path);
    const uint32_t n = rd32(data + 8);
    const uint64_t table_end = 16u + 24u * (uint64_t)n;
    if (!n || n > EM_GFX_OBJECT_TEX_MAX || table_end > (uint64_t)size)
        return fail("invalid catalog table", path);
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t *e = data + 16u + 24u * i;
        const uint64_t key = ((uint64_t)rd32(e) | (uint64_t)rd32(e + 4) << 32) & CLD_MASK;
        const uint32_t w = rd32(e + 8), h = rd32(e + 12), at = rd32(e + 16);
        const uint32_t tw = (uint32_t)(key >> 26) & 15u, th = (uint32_t)(key >> 30) & 15u;
        const uint64_t bytes = 4u * (uint64_t)w * h;
        if (tw > 10u || th > 10u || w != (1u << tw) || h != (1u << th) || rd32(e + 20) ||
            at < table_end || at > (uint64_t)size || bytes > (uint64_t)size - at)
            return fail("invalid catalog texture", path);
        unsigned j;
        for (j = 0; j < *count; ++j) {
            const Texture *t = &textures[j];
            if (t->tex0 != key) continue;
            if (t->width != w || t->height != h || memcmp(t->pixels, data + at, (size_t)bytes))
                return fail("conflicting TEX0 pixels", path);
            break;
        }
        if (j != *count) continue;
        if (*count == EM_GFX_OBJECT_TEX_MAX) return fail("too many texture keys", path);
        textures[(*count)++] = (Texture){key, w, h, data + at};
    }
    return 0;
}

int em_world_textures_live_ensure(EmGfx *gfx)
{
    if (!gfx || S.failed) return -1;
    if (S.ready == gfx) return 0;
    if (!supported()) return fail("no verified catalog for this delivery", NULL);
    const char *paths[2] = {"assets/scene_snow/object_textures.emot",
                            "assets/scene_snow/page_textures.emot"};
    const unsigned files = S.area == 1u ? 1u : 2u;
    if (S.area == 1u) paths[0] = "assets/area01_world_textures.emot";
    Texture textures[EM_GFX_OBJECT_TEX_MAX];
    uint8_t *storage[2] = {NULL, NULL};
    unsigned count = 0;
    int rc = 0;
    for (unsigned i = 0; i < files && rc == 0; ++i)
        rc = catalog(paths[i], &storage[i], textures, &count);
    if (rc == 0) {
        rc = em_gfx_world_textures_reset(gfx);
        for (unsigned i = 0; i < count && rc == 0; ++i) {
            const Texture *t = &textures[i];
            rc = em_gfx_gs_texture(gfx, t->tex0, t->pixels, t->width, t->height);
        }
        if (rc != 0) {
            /* A failed upload must not leave a usable partial catalog. */
            (void)em_gfx_world_textures_reset(gfx);
            rc = fail("device rejected catalog", NULL);
        }
    }
    for (unsigned i = 0; i < files; ++i) free(storage[i]);
    if (rc == 0) S.ready = gfx;
    return rc;
}
