#include "em_spu2_effects.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

static void checksum(uint8_t *bytes)
{
    uint32_t crc = UINT32_MAX;
    for (unsigned i = 96; i < EM_SPU2_EFFECT_FILE_BYTES; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    put32(bytes + 28, crc ^ UINT32_MAX);
}

static void fixture(uint8_t *bytes)
{
    const uint8_t identity[32] = {
        0x1d,0x48,0x14,0x2e,0x26,0xe5,0x79,0x0f,
        0x48,0x30,0xe2,0xf5,0xe4,0xe5,0x62,0x08,
        0xbc,0x3d,0xaf,0xb8,0xe8,0xdf,0x4a,0x5b,
        0xda,0xc7,0x43,0xe0,0x46,0x06,0xb8,0xb0,
    };
    memset(bytes, 0, EM_SPU2_EFFECT_FILE_BYTES);
    memcpy(bytes, "EMRV", 4);
    put32(bytes + 4, 1); put32(bytes + 8, 96);
    put32(bytes + 12, 124); put32(bytes + 16, 20);
    put32(bytes + 20, 4); put32(bytes + 24, 4);
    memcpy(bytes + 32, identity, 32);
    memset(bytes + 64, 0xa5, 32); /* synthetic provenance marker */
    for (unsigned row = 0; row < 20; ++row) {
        uint8_t *r = bytes + 96 + row * 124;
        put32(r, row / 10); put32(r + 4, row % 10);
        put32(r + 8, 0); put32(r + 12, 127);
        for (unsigned i = 0; i < 22; ++i) put32(r + 16 + i * 4, i * 4);
        for (unsigned i = 0; i < 10; ++i) {
            uint16_t value = i ? (uint16_t)(i * 1000) : 0x8000;
            r[104 + 2 * i] = (uint8_t)value;
            r[105 + 2 * i] = (uint8_t)(value >> 8);
        }
    }
    checksum(bytes);
}

static void rejected(EmSpu2Effects *out, uint8_t *bytes, size_t size)
{
    EmSpu2Effects previous = *out;
    assert(em_spu2_effects_read(out, bytes, size) == -1);
    assert(!memcmp(out, &previous, sizeof previous));
}

int main(int argc, char **argv)
{
    uint8_t bytes[EM_SPU2_EFFECT_FILE_BYTES];
    EmSpu2Effects effects;
    memset(&effects, 0x5a, sizeof effects);
    fixture(bytes);
    assert(em_spu2_effects_read(&effects, bytes, sizeof bytes) == 0);
    assert(effects.initial_mode[0] == 4 && effects.initial_mode[1] == 4);
    const EmSpu2ReverbParams *p = &effects.core[1][9].params;
    assert(p->input_volume[0] == -32768 && p->input_volume[1] == 1000);
    assert(p->iir_volume == 2000 && p->wall_volume == 3000);
    assert(p->apf_delay[0] == 0 && p->apf_delay[1] == 4);
    assert(p->same_write[0] == 8 && p->same_read[0] == 16);
    assert(p->diff_read[0] == 32 && p->diff_read[1] == 36);
    assert(p->comb_read[0][3] == 52 && p->comb_read[1][0] == 56);
    assert(p->apf_write[0][0] == 72 && p->apf_write[1][1] == 84);
    assert(p->comb_volume[3] == 7000 && p->apf_volume[1] == 9000);
    int16_t ram[128] = {0};
    EmSpu2Reverb reverb;
    assert(em_spu2_reverb_init(&reverb, ram, 128, 0, 63) == 0);
    reverb.return_volume[0] = 1234;
    reverb.write_enabled = 1;
    reverb.input_history[0][0] = 4321;
    assert(em_spu2_effects_apply(&effects, 1, 9, &reverb) == 0);
    assert(reverb.end == 127 && reverb.cursor == 0);
    assert(reverb.params.apf_write[1][1] == 84);
    assert(reverb.return_volume[0] == 1234 && reverb.write_enabled == 1);
    assert(reverb.input_history[0][0] == 4321);
    assert(em_spu2_effects_apply(&effects, 2, 0, &reverb) == -1);
    assert(em_spu2_effects_apply(&effects, 0, 10, &reverb) == -1);
    effects.core[0][0].end = 128;
    EmSpu2Reverb previous = reverb;
    assert(em_spu2_effects_apply(&effects, 0, 0, &reverb) == -1);
    assert(!memcmp(&reverb, &previous, sizeof reverb));
    rejected(&effects, bytes, sizeof bytes - 1);
    bytes[0] = 'X'; rejected(&effects, bytes, sizeof bytes); fixture(bytes);
    bytes[4] = 2; rejected(&effects, bytes, sizeof bytes); fixture(bytes);
    bytes[32] ^= 1; rejected(&effects, bytes, sizeof bytes); fixture(bytes);
    memset(bytes + 64, 0, 32); rejected(&effects, bytes, sizeof bytes); fixture(bytes);
    bytes[20] = 10; rejected(&effects, bytes, sizeof bytes); fixture(bytes);
    bytes[120] ^= 1; rejected(&effects, bytes, sizeof bytes); fixture(bytes);
    /* Recompute CRC so malformed-but-intact files reach structural checks. */
    put32(bytes + 96 + 124 + 4, 0); checksum(bytes);
    rejected(&effects, bytes, sizeof bytes); fixture(bytes);
    put32(bytes + 96 + 12, 0x100000); checksum(bytes);
    rejected(&effects, bytes, sizeof bytes); fixture(bytes);
    put32(bytes + 96 + 8, 128); checksum(bytes);
    rejected(&effects, bytes, sizeof bytes); fixture(bytes);
    put32(bytes + 96 + 16, UINT32_MAX); checksum(bytes);
    rejected(&effects, bytes, sizeof bytes);
    if (argc == 2) {
        assert(em_spu2_effects_load(&effects, argv[1]) == 0);
        assert(effects.initial_mode[0] == 4 && effects.initial_mode[1] == 4);
        puts("spu2_effects_test: local original configuration asset accepted");
    }
    puts("spu2_effects_test: PASS (synthetic schema, mapping, corruption, transactional apply)");
    return 0;
}
