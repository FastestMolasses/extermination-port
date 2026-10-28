#include "em_spu2_reverb.h"

#include <stddef.h>
#include <string.h>

/*
 * Original implementation of the public PSX-SPX reverb equations. There is no
 * emulator source dependency. The integer evaluation order is deliberately
 * explicit: floor each signed Q15 product; saturate every RAM write, the comb
 * sum, and the output of each all-pass stage. This is a testable model, not a
 * claim that the public algebra uniquely specifies the silicon datapath.
 */
static const int16_t fir[EM_SPU2_REVERB_FIR_TAPS] = {
    -1, 0, 2, 0, -10, 0, 35, 0, -103, 0, 266, 0, -616, 0, 1332, 0,
    -2960, 0, 10246, 16384, 10246, 0, -2960, 0, 1332, 0, -616, 0,
    266, 0, -103, 0, 35, 0, -10, 0, 2, 0, -1
};

static int16_t saturate(int64_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

/* This model quantizes Q15 products with arithmetic-shift semantics.
 * C division truncates toward zero; explicitly floor negative products
 * without relying on implementation-defined negative right shifts. */
static int64_t floor_q15(int64_t product)
{
    if (product >= 0) return product / 32768;
    return -((-product + 32767) / 32768);
}

static int32_t multiply(int32_t sample, int16_t volume)
{
    return (int32_t)floor_q15((int64_t)sample * volume);
}

int em_spu2_reverb_init(EmSpu2Reverb *r, int16_t *ram, uint32_t ram_samples,
                        uint32_t start, uint32_t end)
{
    if (!r || !ram || start > end || end >= ram_samples) return -1;
    memset(r, 0, sizeof *r);
    r->ram = ram;
    r->ram_samples = ram_samples;
    r->start = r->cursor = start;
    r->end = end;
    return 0;
}

int em_spu2_reverb_area(EmSpu2Reverb *r, uint32_t start, uint32_t end)
{
    if (!r || !r->ram || start > end || end >= r->ram_samples) return -1;
    r->start = r->cursor = start;
    r->end = end;
    return 0;
}

uint32_t em_spu2_reverb_address(const EmSpu2Reverb *r, int64_t offset)
{
    const int64_t size = (int64_t)r->end - r->start + 1;
    /* Reduce the offset first so even extreme API inputs cannot overflow. */
    int64_t relative = ((int64_t)r->cursor - r->start + offset % size) % size;
    if (relative < 0) relative += size;
    return r->start + (uint32_t)relative;
}

static int16_t read_tap(const EmSpu2Reverb *r, int64_t offset)
{
    return r->ram[em_spu2_reverb_address(r, offset)];
}

static void write_tap(EmSpu2Reverb *r, int64_t offset, int64_t value)
{
    if (r->write_enabled)
        r->ram[em_spu2_reverb_address(r, offset)] = saturate(value);
}

static void reflection(EmSpu2Reverb *r, int32_t input, int32_t source,
                       int32_t destination)
{
    const EmSpu2ReverbParams *p = &r->params;
    const int32_t previous = read_tap(r, (int64_t)destination - 1);
    const int32_t reflected = multiply(read_tap(r, source), p->wall_volume);
    int32_t value = multiply(input + reflected - previous, p->iir_volume)
                  + previous;
    if (p->iir_volume == -32768) value = -value;
    write_tap(r, destination, value);
}

static int16_t step_channel(EmSpu2Reverb *r, unsigned channel, int16_t input)
{
    const EmSpu2ReverbParams *p = &r->params;
    if (r->write_enabled) {
        int32_t scaled = multiply(input, p->input_volume[channel]);
        reflection(r, scaled, p->same_read[channel], p->same_write[channel]);
        reflection(r, scaled, p->diff_read[channel ^ 1u], p->diff_write[channel]);
    }
    int32_t sum = 0;
    for (unsigned i = 0; i < 4; ++i)
        sum += multiply(read_tap(r, p->comb_read[channel][i]), p->comb_volume[i]);
    int16_t sample = saturate(sum);
    for (unsigned stage = 0; stage < 2; ++stage) {
        const int32_t destination = p->apf_write[channel][stage];
        const int64_t delayed = (int64_t)destination - p->apf_delay[stage];
        /* Read once before the write: a zero-delay tap may alias it. */
        const int16_t prior = read_tap(r, delayed);
        sample = saturate((int32_t)sample - multiply(prior, p->apf_volume[stage]));
        write_tap(r, destination, sample);
        sample = saturate(multiply(sample, p->apf_volume[stage]) + prior);
    }
    return sample;
}

static void advance(EmSpu2Reverb *r)
{
    r->cursor = r->cursor == r->end ? r->start : r->cursor + 1;
}

void em_spu2_reverb_step(EmSpu2Reverb *r, const int16_t input[2],
                         int16_t output[2])
{
    output[0] = step_channel(r, 0, input[0]);
    output[1] = step_channel(r, 1, input[1]);
    advance(r);
}

static int16_t filter(const int16_t *history, unsigned position, int gain)
{
    int64_t sum = 0;
    for (unsigned i = 0; i < EM_SPU2_REVERB_FIR_TAPS; ++i) {
        sum += (int32_t)history[position] * fir[i];
        position = position ? position - 1 : EM_SPU2_REVERB_FIR_TAPS - 1;
    }
    return saturate(floor_q15(sum * gain));
}

void em_spu2_reverb_process(EmSpu2Reverb *r, const int16_t input[2],
                            int16_t output[2])
{
    const unsigned ch = r->channel;
    const unsigned pos = r->history_pos;
    for (unsigned side = 0; side < 2; ++side) {
        r->input_history[side][pos] = input[side];
        r->output_history[side][pos] = 0;
    }
    const int16_t downsampled = filter(r->input_history[ch], pos, 1);
    r->output_history[ch][pos] = step_channel(r, ch, downsampled);
    for (unsigned side = 0; side < 2; ++side) {
        const int16_t upsampled = filter(r->output_history[side], pos, 2);
        output[side] = saturate(multiply(upsampled, r->return_volume[side]));
    }
    if (ch) advance(r);
    r->channel ^= 1;
    r->history_pos = (pos + 1) % EM_SPU2_REVERB_FIR_TAPS;
}
