#include "em_spu2_effects.h"

#include <stdio.h>
#include <string.h>

/* Original module identity; this is a digest, not disc-derived payload. */
static const uint8_t driver_sha256[32] = {
    0x1d, 0x48, 0x14, 0x2e, 0x26, 0xe5, 0x79, 0x0f,
    0x48, 0x30, 0xe2, 0xf5, 0xe4, 0xe5, 0x62, 0x08,
    0xbc, 0x3d, 0xaf, 0xb8, 0xe8, 0xdf, 0x4a, 0x5b,
    0xda, 0xc7, 0x43, 0xe0, 0x46, 0x06, 0xb8, 0xb0
};

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8
         | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int16_t signed16(const uint8_t *p)
{
    const int32_t value = p[0] | (int32_t)p[1] << 8;
    return (int16_t)(value >= 32768 ? value - 65536 : value);
}

static uint32_t crc32(const uint8_t *p, size_t count)
{
    uint32_t value = UINT32_MAX;
    while (count--) {
        value ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (UINT32_C(0xedb88320) & (0u - (value & 1)));
    }
    return value ^ UINT32_MAX;
}

static int read_params(EmSpu2ReverbParams *p, const uint8_t *data)
{
    int32_t *addresses[22] = {
        &p->apf_delay[0], &p->apf_delay[1],
        &p->same_write[0], &p->same_write[1],
        &p->same_read[0], &p->same_read[1],
        &p->diff_write[0], &p->diff_write[1],
        &p->diff_read[0], &p->diff_read[1],
        &p->comb_read[0][0], &p->comb_read[0][1],
        &p->comb_read[0][2], &p->comb_read[0][3],
        &p->comb_read[1][0], &p->comb_read[1][1],
        &p->comb_read[1][2], &p->comb_read[1][3],
        &p->apf_write[0][0], &p->apf_write[0][1],
        &p->apf_write[1][0], &p->apf_write[1][1],
    };
    int16_t *coefficients[10] = {
        &p->input_volume[0], &p->input_volume[1], &p->iir_volume, &p->wall_volume,
        &p->comb_volume[0], &p->comb_volume[1], &p->comb_volume[2], &p->comb_volume[3],
        &p->apf_volume[0], &p->apf_volume[1],
    };
    for (unsigned i = 0; i < 22; ++i) {
        uint32_t value = le32(data + 4 * i);
        if (value >= 0x100000) return -1;
        *addresses[i] = (int32_t)value;
    }
    for (unsigned i = 0; i < 10; ++i)
        *coefficients[i] = signed16(data + 88 + 2 * i);
    return 0;
}

int em_spu2_effects_read(EmSpu2Effects *out, const void *data, size_t size)
{
    if (!out || !data || size != EM_SPU2_EFFECT_FILE_BYTES) return -1;
    const uint8_t *bytes = data;
    if (memcmp(bytes, "EMRV", 4) || le32(bytes + 4) != 1
        || le32(bytes + 8) != 96 || le32(bytes + 12) != 124
        || le32(bytes + 16) != 20 || memcmp(bytes + 32, driver_sha256, 32)
        || le32(bytes + 28) != crc32(bytes + 96, size - 96))
        return -1;
    unsigned capture_nonzero = 0;
    for (unsigned i = 64; i < 96; ++i) capture_nonzero |= bytes[i];
    if (!capture_nonzero) return -1;
    EmSpu2Effects parsed;
    memset(&parsed, 0, sizeof parsed);
    for (unsigned core = 0; core < 2; ++core) {
        parsed.initial_mode[core] = le32(bytes + 20 + 4 * core);
        if (parsed.initial_mode[core] >= EM_SPU2_EFFECT_MODES) return -1;
    }
    memcpy(parsed.driver_sha256, bytes + 32, 32);
    memcpy(parsed.capture_sha256, bytes + 64, 32);
    uint32_t seen = 0;
    for (unsigned row = 0; row < 20; ++row) {
        const uint8_t *record = bytes + 96 + row * 124;
        unsigned core = le32(record), mode = le32(record + 4);
        uint32_t start = le32(record + 8), end = le32(record + 12);
        if (core >= 2 || mode >= EM_SPU2_EFFECT_MODES
            || start > end || end >= 0x100000) return -1;
        const uint32_t bit = 1u << (core * EM_SPU2_EFFECT_MODES + mode);
        if (seen & bit) return -1;
        seen |= bit;
        EmSpu2Effect *effect = &parsed.core[core][mode];
        effect->start = start;
        effect->end = end;
        if (read_params(&effect->params, record + 16)) return -1;
    }
    if (seen != 0xfffff) return -1;
    *out = parsed;
    return 0;
}

int em_spu2_effects_load(EmSpu2Effects *out, const char *path)
{
    if (!path) return -1;
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    uint8_t bytes[EM_SPU2_EFFECT_FILE_BYTES];
    size_t count = fread(bytes, 1, sizeof bytes, file);
    int extra = fgetc(file);
    int failed = ferror(file);
    fclose(file);
    if (failed || count != sizeof bytes || extra != EOF) return -1;
    return em_spu2_effects_read(out, bytes, sizeof bytes);
}

int em_spu2_effects_apply(const EmSpu2Effects *effects, unsigned core,
                          unsigned mode, EmSpu2Reverb *reverb)
{
    if (!effects || !reverb || core >= 2 || mode >= EM_SPU2_EFFECT_MODES) return -1;
    const EmSpu2Effect *effect = &effects->core[core][mode];
    if (em_spu2_reverb_area(reverb, effect->start, effect->end)) return -1;
    reverb->params = effect->params;
    return 0;
}
