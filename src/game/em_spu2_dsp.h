#ifndef EM_SPU2_DSP_H
#define EM_SPU2_DSP_H

#include <stdint.h>

/* Independently written from public hardware documentation; no emulator
 * implementation is used. Call each stateful step once per 48000 Hz sample.
 * These primitives do not own commands, driver feedback, or audio scheduling.
 * See docs/SPU2_OUTPUT.md for the limits of the PS1-to-SPU2 correspondence. */

/* The four decoded samples, oldest first. Bits 4..11 of the 12-bit pitch
 * fraction select the documented coefficient quartet. Each product is
 * rounded down separately, including negative products. */
int16_t em_spu2_gaussian(const int16_t history[4], uint16_t fraction);
int16_t em_spu2_gauss_coefficient(unsigned index);

int16_t em_spu2_clamp(int32_t value);
/* Signed Q15 gain, with floor rounding and signed 16-bit saturation. */
int16_t em_spu2_apply_gain(int16_t sample, int16_t gain);
/* Fixed volume registers contain a signed 15-bit value, doubled to Q15.
 * This function requires bit 15 clear; sweep registers use EmSpu2Volume. */
int16_t em_spu2_fixed_volume(uint16_t word);

enum {
    EM_SPU2_ADSR_OFF,
    EM_SPU2_ADSR_ATTACK,
    EM_SPU2_ADSR_DECAY,
    EM_SPU2_ADSR_SUSTAIN,
    EM_SPU2_ADSR_RELEASE
};

typedef struct {
    uint16_t adsr1, adsr2;
    uint8_t phase;
    int32_t level;
    uint32_t counter;
} EmSpu2Adsr;

void em_spu2_adsr_key_on(EmSpu2Adsr *adsr, uint16_t adsr1, uint16_t adsr2);
void em_spu2_adsr_key_off(EmSpu2Adsr *adsr);
/* Advances the envelope; key-on starts at zero. The caller chooses whether
 * its sample reads level before or after step. Register changes are live. */
void em_spu2_adsr_step(EmSpu2Adsr *adsr);

typedef struct {
    uint16_t control;
    int32_t level;
    uint32_t counter;
} EmSpu2Volume;

/* Voice volume registers, in the public PSX-SPX fixed/sweep volume format.
 * Initialize the struct to zero. Direct writes take effect on step, so a
 * direct write immediately replaced by a sweep does not set a start level.
 * A sweep write preserves the current level and fractional counter. */
void em_spu2_volume_write(EmSpu2Volume *volume, uint16_t control);
int16_t em_spu2_volume_step(EmSpu2Volume *volume);

#endif
