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
    unsigned mode, costume;   /* 00200890's D_00810707 / D_00810C60: the player's texture packet */
    EmGfx *ready;
    int failed;
} S = {11u, 0u, 0u, 0u, NULL, 0};

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int supported(void)
{
    /* The player's other texture packets (00200890 slots 9..0xC) have
     * catalogs for AREA00 / 01 / 02 / 04 / 06 / 13 / 22 (every recorded sub, tools/
     * export_player_texture_variants.py); AREA11 has none: the player's
     * infection cannot reach it. */
    if ((S.mode || S.costume) && !((S.area == 0x16u || S.area == 6u || S.area == 0x0Du || S.area == 1u ||
                                    S.area == 4u || S.area == 0u || S.area == 2u || S.area == 0x0Fu ||
                                    S.area == 3u || S.area == 8u || S.area == 7u || S.area == 0x13u) && S.mode <= 2u &&
                                   S.costume <= 2u))
        return 0;
    if (S.area == 8u && (S.subarea == 2u || S.subarea == 3u)) return 1;
    return ((S.area == 0u || S.area == 2u || S.area == 0x0Fu || S.area == 4u || S.area == 0x13u) && S.subarea <= 1u) ||
           (S.area == 3u && S.subarea <= 2u) ||
           (S.subarea == 0u && (S.area == 11u || S.area == 1u || S.area == 4u || S.area == 0x16u || S.area == 6u ||
                                S.area == 0x0Du || S.area == 3u || S.area == 8u || S.area == 7u));
}

static int fail(const char *what, const char *path)
{
    if (!S.failed)
        fprintf(stderr, "world textures: AREA%02u/%u: %s%s%s\n", S.area, S.subarea,
                what, path ? ": " : "", path ? path : "");
    S.failed = 1;
    return -1;
}

int em_world_textures_live_player_packet(unsigned mode, unsigned costume)
{
    /* Every mode above 1 uploads slot 9 whatever the costume (00200890):
     * one catalog, (2, 0). */
    if (mode >= 2u) {
        mode = 2u;
        costume = 0u;
    }
    if (mode == S.mode && costume == S.costume) return S.failed ? -1 : 0;
    S.mode = mode;
    S.costume = costume;
    S.ready = NULL;           /* the next frame uploads the catalog with the new packet */
    if (!supported()) {
        char what[96];
        snprintf(what, sizeof what, "no catalog for the player's texture packet (mode %u, costume %u)", mode,
                 costume);
        return fail(what, NULL);
    }
    return 0;
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
    if (n > EM_GFX_OBJECT_TEX_MAX)
        return fail("catalog exceeds device texture capacity", path);
    if (!n || table_end > (uint64_t)size)
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
    const unsigned files = S.area == 11u ? 2u : 1u;
    if (S.area == 1u) paths[0] = "assets/area01_world_textures.emot";
    if (S.area == 0u)
        paths[0] = S.subarea == 0u ? "assets/area00_sub0_world_textures.emot"
                                   : "assets/area00_sub1_world_textures.emot";
    if (S.area == 2u)   /* tools/export_area02_world_textures.py */
        paths[0] = S.subarea == 0u ? "assets/area02_sub0_world_textures.emot"
                                   : "assets/area02_sub1_world_textures.emot";
    if (S.area == 4u)   /* tools/export_area04_world_textures.py (sub 0 only) */
        paths[0] = "assets/area04_sub0_world_textures.emot";
    /* tools/export_level_world_textures.py --profile 22 / 06 / 13 (mode 0,
     * costume 0: the recorded deliveries) */
    char mid[96];
    if (S.area == 0x16u || S.area == 6u || S.area == 0x0Du || S.area == 0x0Fu || S.area == 3u || S.area == 8u ||
        S.area == 7u || (S.area == 4u && S.subarea == 1u) || ((S.mode || S.costume) && S.area != 11u)) {
        snprintf(mid, sizeof mid, "assets/levels/%02u/sub%u/world_textures_mode%u_costume%u.emot", S.area, S.subarea,
                 S.mode, S.costume);
        paths[0] = mid;
    }
    /* tools/export_level_world_textures.py --profile 19 / 19s1 (AREA19) */
    if (S.area == 0x13u) {
        snprintf(mid, sizeof mid, "assets/levels/%s/world_textures_mode%u_costume%u.emot",
                 S.subarea ? "19s1/sub1" : "19/sub0", S.mode, S.costume);
        paths[0] = mid;
    }
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
