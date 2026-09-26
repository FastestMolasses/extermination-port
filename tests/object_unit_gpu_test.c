/* object_unit_gpu_test.c - the Metal side of em_gfx_object_unit over
 * captured units: tools/test_object_unit_gpu.py runs it headless.
 *
 * Arguments: ee.bin fog_a fog_b fog_r fog_g fog_b out.bmp address:size ...
 * The EE RAM image is the user's own route capture (read, never written);
 * each address:size is one captured 001CA990 unit in its display list,
 * parsed with em_object_unit_parse (REF targets resolved in the same image)
 * and drawn in order with the frame's fog, the textures of
 * assets/scene_snow/object_textures.emot, then the frame is captured. The
 * Python side compares the pixels with its model of the GS pixel path. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_gfx.h"
#include "em_platform.h"
#include "game/em_object_unit.h"

static uint8_t *ram;
static uint32_t ram_size;

static const uint8_t *resolve(void *ctx, uint32_t address, uint32_t bytes)
{
    (void)ctx;
    if (address > ram_size || bytes > ram_size - address) return NULL;
    return ram + address;
}

static uint8_t *load(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = n > 0 ? malloc((size_t)n) : NULL;
    if (p && fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        p = NULL;
    }
    fclose(f);
    *size = (uint32_t)n;
    return p;
}

static int textures(EmGfx *gfx)
{
    uint32_t size = 0;
    uint8_t *d = load("assets/scene_snow/object_textures.emot", &size);
    if (!d || size < 16 || memcmp(d, "EMOT", 4)) return -1;
    uint32_t count;
    memcpy(&count, d + 8, 4);
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t tex0;
        uint32_t w, h, at;
        memcpy(&tex0, d + 16 + 24 * i, 8);
        memcpy(&w, d + 24 + 24 * i, 4);
        memcpy(&h, d + 28 + 24 * i, 4);
        memcpy(&at, d + 32 + 24 * i, 4);
        if ((uint64_t)at + 4ull * w * h > size || em_gfx_object_texture(gfx, tex0, d + at, w, h)) return -1;
    }
    free(d);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 9) {
        fprintf(stderr, "usage: %s ee.bin fog_a fog_b r g b out.bmp address:size...\n", argv[0]);
        return 2;
    }
    ram = load(argv[1], &ram_size);
    if (!ram) return 2;
    const float coef[2] = {strtof(argv[2], NULL), strtof(argv[3], NULL)};
    const float rgb[3] = {strtof(argv[4], NULL), strtof(argv[5], NULL), strtof(argv[6], NULL)};
    EmWindow *window = em_window_create("object unit GPU fixture", 640, 480);
    EmGfx *gfx = window ? em_gfx_create(window) : NULL;
    if (!gfx || textures(gfx)) {
        fprintf(stderr, "object_unit_gpu_test: no device or no texture export\n");
        return 1;
    }
    static EmObjectUnitPieces pieces[64];
    int units = 0;
    for (int a = 8; a < argc && units < 64; ++a, ++units) {
        unsigned long address = 0, size = 0;
        const char *why = NULL;
        if (sscanf(argv[a], "%lx:%lx", &address, &size) != 2 ||
            em_object_unit_parse(ram + address, (uint32_t)size, resolve, NULL, &pieces[units], &why)) {
            fprintf(stderr, "object_unit_gpu_test: unit %s refused: %s\n", argv[a], why ? why : "bad argument");
            return 1;
        }
    }
    int failed = 0;
    for (int frame = 0; frame < 2; ++frame) {
        EmEvent event;
        while (em_window_poll(window, &event)) {
        }
        em_gfx_begin_frame(gfx, 0.0f, 0.0f, 0.0f, 1.0f);
        em_gfx_fog_coefficients(gfx, coef, rgb);
        for (int u = 0; u < units; ++u) failed |= em_gfx_object_unit(gfx, &pieces[u].unit) != 0;
        if (frame == 1) em_gfx_request_capture(gfx, argv[7]);
        em_gfx_end_frame(gfx);
    }
    em_gfx_destroy(gfx);
    em_window_destroy(window);
    free(ram);
    if (failed) {
        fprintf(stderr, "object_unit_gpu_test: em_gfx_object_unit refused a unit\n");
        return 1;
    }
    printf("object_unit_gpu_test: %d unit(s) drawn\n", units);
    return 0;
}
