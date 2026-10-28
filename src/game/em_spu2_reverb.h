#ifndef EM_SPU2_REVERB_H
#define EM_SPU2_REVERB_H

#include <stdint.h>

/*
 * Public-algorithm reverb model, not yet certified against a SPU2 capture.
 * Source: https://psx-spx.consoledev.net/soundprocessingunitspu/
 * ("SPU Reverb Formula", "Reverb Buffer Resampling").
 *
 * The caller supplies the game's settings. No game preset is hardcoded.
 * Addresses below are NORMALIZED signed-16 sample offsets, not MMIO words:
 * PS1 eight-byte address units must be multiplied by four before use.
 * SPU2 register decoding belongs at the configuration boundary.
 * Both cores may share one RAM array but have independent effect regions.
 */
typedef struct {
    int32_t apf_delay[2];
    int32_t same_write[2], same_read[2];
    int32_t diff_write[2], diff_read[2];
    int32_t comb_read[2][4];
    int32_t apf_write[2][2]; /* [channel][stage] */
    int16_t input_volume[2];
    int16_t iir_volume, wall_volume;
    int16_t comb_volume[4], apf_volume[2];
} EmSpu2ReverbParams;

enum { EM_SPU2_REVERB_FIR_TAPS = 39 };

typedef struct {
    int16_t *ram;
    uint32_t ram_samples;
    uint32_t start, end, cursor; /* inclusive bounds, absolute sample indices */
    EmSpu2ReverbParams params;
    int16_t return_volume[2];   /* EVOLL/EVOLR: signed Q15, not voice Q14 */
    int write_enabled;
    unsigned channel;          /* next half-rate channel: left, then right */
    unsigned history_pos;
    int16_t input_history[2][EM_SPU2_REVERB_FIR_TAPS];
    int16_t output_history[2][EM_SPU2_REVERB_FIR_TAPS];
} EmSpu2Reverb;

/* Initializes state without clearing caller-owned RAM. Returns 0 on success,
 * -1 for NULL RAM or an invalid inclusive region. */
int em_spu2_reverb_init(EmSpu2Reverb *r, int16_t *ram, uint32_t ram_samples,
                        uint32_t start, uint32_t end);

/* Set effect-area bounds and reset its cursor without clearing its memory,
 * parameters, or FIR histories. Returns -1 and changes nothing if invalid. */
int em_spu2_reverb_area(EmSpu2Reverb *r, uint32_t start, uint32_t end);

/* Resolve a relative signed-sample offset inside the inclusive effect area. */
uint32_t em_spu2_reverb_address(const EmSpu2Reverb *r, int64_t offset);

/* One algebraic 24 kHz stereo tick, without resampling or return volumes.
 * Useful for hand-computed algorithm tests. Do not interleave with process().
 * Writes are gated; existing effect RAM can still be heard when disabled.
 * The documented PS1 vIIR=-32768 sign quirk is retained. Its SPU2 applicability,
 * intermediate saturation/rounding, and bus-cycle phase need measurement. */
void em_spu2_reverb_step(EmSpu2Reverb *r, const int16_t input[2],
                         int16_t output[2]);

/* One 48 kHz stereo frame: 39-tap downsample/upsample, alternating channel
 * calculation, signed effect-return volumes. Input is the saturated wet bus;
 * output is the wet return only (caller adds dry and applies core volumes).
 * The inherited PS1 filter and phase are explicit unverified SPU2 assumptions. */
void em_spu2_reverb_process(EmSpu2Reverb *r, const int16_t input[2],
                            int16_t output[2]);

#endif
