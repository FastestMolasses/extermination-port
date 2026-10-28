#include "game/em_spu2_dsp.h"

#include <stdint.h>
#include <stdio.h>

/* Hand-evaluated cases from public PSX-SPX hardware documentation, never
 * emulator output. The coefficient digest was computed independently from
 * the public document's 512 numbers, serialized as little-endian int16.
 * Compile the DSP with EM_SPU2_MUTATE_GAUSSIAN, EM_SPU2_MUTATE_ADSR, or
 * EM_SPU2_MUTATE_VOLUME: each intentionally wrong build must exit nonzero.
 * These tests prove the documented arithmetic, not SPU2 waveform equality. */
static unsigned checks, failures;
#define CHECK_EQ(actual, expected) do { \
    long long got_ = (actual), want_ = (expected); \
    ++checks; \
    if (got_ != want_) { \
        ++failures; \
        fprintf(stderr, "%s:%d: %s = %lld, expected %lld\n", \
                __FILE__, __LINE__, #actual, got_, want_); \
    } \
} while (0)

static void gaussian_cases(void)
{
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < 512; ++i) {
        uint16_t coefficient = (uint16_t)em_spu2_gauss_coefficient(i);
        hash = (hash ^ (coefficient & 255)) * 16777619u;
        hash = (hash ^ (coefficient >> 8)) * 16777619u;
    }
    CHECK_EQ(hash, 0x637BADA7u);
    CHECK_EQ(em_spu2_gauss_coefficient(0), -1);
    CHECK_EQ(em_spu2_gauss_coefficient(15), -1);
    CHECK_EQ(em_spu2_gauss_coefficient(16), 0);
    CHECK_EQ(em_spu2_gauss_coefficient(511), 0x59B3);
    CHECK_EQ(em_spu2_gauss_coefficient(512), 0);
    for (unsigned i = 0; i < 256; ++i) {
        int sum = em_spu2_gauss_coefficient(i) +
                  em_spu2_gauss_coefficient(255 - i) +
                  em_spu2_gauss_coefficient(256 + i) +
                  em_spu2_gauss_coefficient(511 - i);
        CHECK_EQ(sum >= 0x7F7F && sum <= 0x7F81, 1);
    }
    /* At fraction zero the coefficients are 4807,22963,4871,-1.
     * Products floor separately, so four samples of 1 produce -1,
     * and four samples of -1 produce -3, rather than a rounded dot product. */
    const int16_t ones[4] = {1, 1, 1, 1};
    const int16_t negatives[4] = {-1, -1, -1, -1};
    CHECK_EQ(em_spu2_gaussian(ones, 0), -1);
    CHECK_EQ(em_spu2_gaussian(negatives, 0), -3);
    const int16_t signal[4] = {1234, -2345, 30000, -12345};
    CHECK_EQ(em_spu2_gaussian(signal, 0x000), 2996);
    CHECK_EQ(em_spu2_gaussian(signal, 0x00F), 2996);
    CHECK_EQ(em_spu2_gaussian(signal, 0x010), 3052);
    CHECK_EQ(em_spu2_gaussian(signal, 0x7F0), 13232);
    CHECK_EQ(em_spu2_gaussian(signal, 0x800), 13320);
    CHECK_EQ(em_spu2_gaussian(signal, 0xFF0), 18862);
    CHECK_EQ(em_spu2_gaussian(signal, 0xFFF), 18862);
    const int16_t zero[4] = {0, 0, 0, 0};
    CHECK_EQ(em_spu2_gaussian(zero, 0x800), 0);
}

static void adsr_cases(void)
{
    EmSpu2Adsr a;
    em_spu2_adsr_key_on(&a, 0, 0);
    CHECK_EQ(a.level, 0);
    CHECK_EQ(a.phase, EM_SPU2_ADSR_ATTACK);
    /* Attack shift 0, step 0 => +7*2048 each sample, then saturation. */
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 14336);
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 28672);
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 32767);
    CHECK_EQ(a.phase, EM_SPU2_ADSR_DECAY);
    /* Decay shift 0 scales -16384 by level/32768, rounding down. */
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 16383);
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 8191);
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 4095);
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 2047);
    CHECK_EQ(a.phase, EM_SPU2_ADSR_SUSTAIN);
    em_spu2_adsr_key_off(&a);
    CHECK_EQ(a.phase, EM_SPU2_ADSR_RELEASE);
    em_spu2_adsr_step(&a);
    CHECK_EQ(a.level, 0);
    CHECK_EQ(a.phase, EM_SPU2_ADSR_OFF);
    /* Sustain nibble F has target 0x8000, already above attack's ceiling.
     * Entering decay must not subtract a step in that case. */
    em_spu2_adsr_key_on(&a, 0x000F, 0);
    for (unsigned i = 0; i < 3; ++i) em_spu2_adsr_step(&a);
    em_spu2_adsr_step(&a);
    CHECK_EQ(a.level, 32767);
    CHECK_EQ(a.phase, EM_SPU2_ADSR_SUSTAIN);

    /* The exponential attack boundary is strictly ABOVE 0x6000.
     * At shift 0 it quarters the step, without delaying that step. */
    em_spu2_adsr_key_on(&a, 0x8000, 0);
    a.level = 0x6000;
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 32767);
    em_spu2_adsr_key_on(&a, 0x8000, 0);
    a.level = 0x6001;
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 0x6001 + 3584);
    /* Shift 10 splits the slowdown: half step, half counter increment. */
    em_spu2_adsr_key_on(&a, 0xA800, 0);
    a.level = 0x6001;
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 0x6001);
    CHECK_EQ(a.counter, 0x4000);
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 0x6008);
    CHECK_EQ(a.counter, 0);
    /* Shift 11 quarters the increment; step is still +7. */
    em_spu2_adsr_key_on(&a, 0xAC00, 0);
    a.level = 0x6001;
    for (unsigned i = 0; i < 3; ++i) em_spu2_adsr_step(&a);
    CHECK_EQ(a.level, 0x6001);
    CHECK_EQ(a.counter, 0x6000);
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 0x6008);

    /* Sustain shift 11/step1: +6 or -7. Exponential decrease at level1
     * still removes 1 because negative fractions round down. */
    a = (EmSpu2Adsr){ .adsr2 = 45u << 6, .phase = EM_SPU2_ADSR_SUSTAIN,
                    .level = 100 };
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 106);
    a.adsr2 |= 0x4000;
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 99);
    a.adsr2 |= 0x8000; a.level = 1;
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 0);
    CHECK_EQ(a.phase, EM_SPU2_ADSR_SUSTAIN);
    /* Release shift 11, exponential: -8 * 16384 / 32768 = -4. */
    a = (EmSpu2Adsr){ .adsr2 = 0x2B, .phase = EM_SPU2_ADSR_RELEASE,
                    .level = 16384 };
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 16380);

    /* All-ones attack and sustain rates hold indefinitely. Release's
     * all-ones five-bit rate likewise holds. Other very slow rates have a
     * minimum increment of 1, hence exactly 32768 samples per step. */
    em_spu2_adsr_key_on(&a, 0x7F00, 0);
    for (unsigned i = 0; i < 32768; ++i) em_spu2_adsr_step(&a);
    CHECK_EQ(a.level, 0); CHECK_EQ(a.counter, 0);
    a.adsr1 = 0x7E00;
    for (unsigned i = 0; i < 32767; ++i) em_spu2_adsr_step(&a);
    CHECK_EQ(a.level, 0); CHECK_EQ(a.counter, 32767);
    em_spu2_adsr_step(&a); CHECK_EQ(a.level, 5);
    a.adsr2 = 0x1FC0; a.phase = EM_SPU2_ADSR_SUSTAIN;
    for (unsigned i = 0; i < 32768; ++i) em_spu2_adsr_step(&a);
    CHECK_EQ(a.level, 5);
    a.adsr2 = 31;
    em_spu2_adsr_key_off(&a);
    for (unsigned i = 0; i < 32768; ++i) em_spu2_adsr_step(&a);
    CHECK_EQ(a.level, 5); CHECK_EQ(a.phase, EM_SPU2_ADSR_RELEASE);
}

static void volume_cases(void)
{
    CHECK_EQ(em_spu2_fixed_volume(0), 0);
    CHECK_EQ(em_spu2_fixed_volume(0x2000), 16384);
    CHECK_EQ(em_spu2_fixed_volume(0x3FFF), 32766);
    CHECK_EQ(em_spu2_fixed_volume(0x4000), -32768);
    CHECK_EQ(em_spu2_fixed_volume(0x6000), -16384);
    CHECK_EQ(em_spu2_fixed_volume(0x7FFF), -2);
    CHECK_EQ(em_spu2_apply_gain(30000, 16384), 15000);
    CHECK_EQ(em_spu2_apply_gain(-1, 16384), -1);
    CHECK_EQ(em_spu2_apply_gain(1, -16384), -1);
    CHECK_EQ(em_spu2_apply_gain(-32768, -32768), 32767);
    CHECK_EQ(em_spu2_clamp(-40000), -32768);
    CHECK_EQ(em_spu2_clamp(40000), 32767);
    EmSpu2Volume v = {0};
    em_spu2_volume_write(&v, 0x2000);
    CHECK_EQ(v.level, 0);
    CHECK_EQ(em_spu2_volume_step(&v), 16384);
    em_spu2_volume_write(&v, 0x802C); /* +7 each sample */
    CHECK_EQ(em_spu2_volume_step(&v), 16391);
    em_spu2_volume_write(&v, 0xA02C); /* -8 each sample */
    CHECK_EQ(em_spu2_volume_step(&v), 16383);
    em_spu2_volume_write(&v, 0xE02C); /* floor(-8*16383/32768) = -4 */
    CHECK_EQ(em_spu2_volume_step(&v), 16379);
    em_spu2_volume_write(&v, 0x3FFF);
    em_spu2_volume_write(&v, 0x807F);
    CHECK_EQ(em_spu2_volume_step(&v), 16379); /* Direct write was replaced. */
    for (unsigned i = 0; i < 32768; ++i) em_spu2_volume_step(&v);
    CHECK_EQ(v.level, 16379);
    em_spu2_volume_write(&v, 0xC000);
    v.level = 0x6001;
    CHECK_EQ(em_spu2_volume_step(&v), 0x6001 + 3584);
    /* Signed sweep examples: negative-phase increase crosses through zero;
     * negative-phase linear decrease moves a negative value toward zero. */
    em_spu2_volume_write(&v, 0x902C); v.level = 7;
    CHECK_EQ(em_spu2_volume_step(&v), -1);
    em_spu2_volume_write(&v, 0xB02C); v.level = -100;
    CHECK_EQ(em_spu2_volume_step(&v), -93);
    v.level = -1;
    CHECK_EQ(em_spu2_volume_step(&v), 0);
}

int main(void)
{
    gaussian_cases();
    adsr_cases();
    volume_cases();
    printf("SPU2 public DSP: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
