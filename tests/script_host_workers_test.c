/* ASan/UBSan contract test for em_script_host_workers (docs/SCRIPT_HOST_WORKERS.md):
 * the AREA11 script loader over the user's exported assets and malformed
 * copies of them, and the fail-stop contract of the worker adapters. The
 * original-instruction comparison is tools/test_script_host_workers_reference.py.
 *
 * Usage: script_host_workers_test <scratch dir>   (run from the repo root) */
#include "game/em_script_host_workers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static unsigned char *slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = n > 0 ? malloc((size_t)n) : NULL;
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
    fclose(f);
    if (data) *size = (size_t)n;
    return data;
}

static int spit(const char *path, const unsigned char *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(data, 1, size, f);
    fclose(f);
    return n == size ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: %s <scratch dir>\n", argv[0]); return 2; }
    const char *scratch = argv[1];
    size_t scripts_size = 0, quads_size = 0;
    unsigned char *scripts = slurp(EM_AREA11_SCRIPTS_PATH, &scripts_size);
    unsigned char *quads = slurp(EM_AREA11_QUADS_PATH, &quads_size);
    if (!scripts || !quads) {
        fprintf(stderr, "missing %s / %s: run python3 tools/export_area11_scripts.py\n",
                EM_AREA11_SCRIPTS_PATH, EM_AREA11_QUADS_PATH);
        return 2;
    }

    /* Every image: each owner entry resolves to the image that holds it. */
    EmArea11Scripts s;
    CHECK(em_area11_scripts_load(&s, EM_AREA11_SCRIPTS_PATH, EM_AREA11_QUADS_PATH,
                                 EM_AREA11_ELEVATOR_PATH, EM_AREA11_ROGER_PATH) == 0);
    size_t count = 0;
    const uint32_t *entries = em_area11_scripts_entries(&count);
    CHECK(count == 11);
    for (size_t i = 0; i < count; ++i) {
        EmScriptImage *image = em_area11_scripts_image(&s, entries[i]);
        CHECK(image != NULL);
        if (image) CHECK(em_script_image_read(image, entries[i], EM_SCRIPT_RECORD_SIZE) != NULL);
    }
    CHECK(em_area11_scripts_image(&s, EM_AREA11_SCRIPTS_END) == NULL);
    CHECK(em_area11_scripts_image(&s, 0x00828FC0u) == NULL);     /* the opening: not these images */
    const float (*quad[3])[4];
    CHECK(em_area11_scripts_director_quads(&s, quad) == 0);
    for (int q = 0; q < 3; ++q)
        CHECK(memcmp(quad[q], quads + 20 + 0x40 * q, 0x40) == 0);
    em_area11_scripts_free(&s);
    em_area11_scripts_free(&s);                                    /* idempotent */
    CHECK(em_area11_scripts_director_quads(&s, quad) == -1);

    /* Malformed copies are refused and leave nothing loaded. */
    char path[1024];
    snprintf(path, sizeof path, "%s/scripts_bad.emsc", scratch);
    const size_t cuts[] = {scripts_size - 1, 20, 19};
    for (size_t i = 0; i < sizeof cuts / sizeof cuts[0]; ++i) {
        CHECK(spit(path, scripts, cuts[i]) == 0);
        memset(&s, 0x5A, sizeof s);
        CHECK(em_area11_scripts_load(&s, path, EM_AREA11_QUADS_PATH, NULL, NULL) == -1);
    }
    unsigned char *copy = malloc(scripts_size);
    memcpy(copy, scripts, scripts_size);
    for (size_t at = 20; at + 4 <= scripts_size; at += EM_SCRIPT_RECORD_SIZE)
        copy[at + 3] &= 0x7Fu;                                       /* clear every stop flag */
    CHECK(spit(path, copy, scripts_size) == 0);
    CHECK(em_area11_scripts_load(&s, path, EM_AREA11_QUADS_PATH, NULL, NULL) == -1);
    memcpy(copy, scripts, scripts_size);
    copy[8] ^= 0x40u;                                                /* wrong base */
    CHECK(spit(path, copy, scripts_size) == 0);
    CHECK(em_area11_scripts_load(&s, path, EM_AREA11_QUADS_PATH, NULL, NULL) == -1);
    CHECK(em_area11_scripts_load(&s, EM_AREA11_SCRIPTS_PATH, EM_AREA11_SCRIPTS_PATH, NULL, NULL) == -1);
    CHECK(em_area11_scripts_load(&s, EM_AREA11_SCRIPTS_PATH, NULL, NULL, NULL) == -1);
    CHECK(em_area11_scripts_load(&s, "missing.emsc", EM_AREA11_QUADS_PATH, NULL, NULL) == -1);
    CHECK(em_area11_scripts_load(&s, EM_AREA11_SCRIPTS_PATH, EM_AREA11_QUADS_PATH, NULL, NULL) == 0);
    CHECK(em_area11_scripts_image(&s, 0x0082A990u) == NULL);       /* elevator image not asked for */
    em_area11_scripts_free(&s);
    free(copy);

    /* Fail-stop of the adapters without a context or storage. */
    EmScriptHostWorkers h;
    memset(&h, 0, sizeof h);
    int32_t r = 7;
    float f = 1.0f;
    const float v[4] = {1.0f, 2.0f, 3.0f, 1.0f};
    CHECK(em_script_host_w_00182BF0(NULL, EM_SCRIPT_HOST_D_008102B0, &r) == -1 && r == 7);
    CHECK(em_script_host_w_00182BF0(&h, EM_SCRIPT_HOST_D_008102B0, &r) == -1 && r == 7);
    CHECK(h.fault_address == 0x00182BF0u);
    memset(&h, 0, sizeof h);
    CHECK(em_script_host_w_001B1240(&h, v, 5.0f, 6.0f, &f) == -1 && f == 1.0f);
    CHECK(h.fault_address == 0x0011E620u);                          /* no SDK tables bound */
    memset(&h, 0, sizeof h);
    CHECK(em_script_host_w_001B6250(&h, EM_SCRIPT_HOST_D_00810E40) == -1);
    CHECK(h.fault_address == 0x001B6250u);
    memset(&h, 0, sizeof h);
    CHECK(em_script_host_w_001B0C00(&h, 8) == -1 && h.fault_address == 0x001AEDE0u);
    memset(&h, 0, sizeof h);
    CHECK(em_script_host_w_001B0460(&h, 1) == -1 && h.fault_address == 0x0024D650u);
    /* 001B12B0 needs no data; the approach slot ignores its context. */
    uint32_t out = 0;
    CHECK(em_script_host_approach((void *)&h, 0x3F800000u, 0x00000000u, 0x3DCCCCCDu, &out) == 0);
    CHECK(out == 0x3DCCCCCDu);                                       /* 0 + 0.1, not yet 1.0 */
    CHECK(em_script_host_approach(NULL, 0x3F800000u, 0x3F800000u, 0x3DCCCCCDu, &out) == 0);
    CHECK(out == 0x3F800000u);                                       /* zero difference: wrap(current) */
    CHECK(em_script_host_approach(NULL, 0x45800000u, 0x00000000u, 0x3DCCCCCDu, &out) == -1);
    CHECK(em_script_host_w_001B12B0(NULL, 1.0f, 0.0f, 0.1f, NULL) == -1);

    free(scripts);
    free(quads);
    printf("script host workers: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
