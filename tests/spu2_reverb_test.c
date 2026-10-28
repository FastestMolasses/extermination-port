#include "em_spu2_reverb.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void setup(EmSpu2Reverb *r, int16_t ram[128])
{
    memset(ram, 0, 128 * sizeof *ram);
    assert(em_spu2_reverb_init(r, ram, 128, 16, 111) == 0);
    r->write_enabled = 1;
    for (unsigned ch = 0; ch < 2; ++ch) {
        r->params.same_write[ch] = 8 + (int)ch * 8;
        r->params.diff_write[ch] = 24 + (int)ch * 8;
        r->params.same_read[ch] = 40 + (int)ch * 4;
        r->params.diff_read[ch] = 48 + (int)ch * 4;
        r->params.apf_write[ch][0] = 64 + (int)ch * 8;
        r->params.apf_write[ch][1] = 80 + (int)ch * 8;
    }
    r->params.apf_delay[0] = r->params.apf_delay[1] = 1;
}

static void put(EmSpu2Reverb *r, int32_t offset, int16_t value)
{
    r->ram[em_spu2_reverb_address(r, offset)] = value;
}

static void addresses(void)
{
    EmSpu2Reverb r;
    int16_t ram[128];
    setup(&r, ram);
    assert(em_spu2_reverb_init(NULL, ram, 128, 16, 111) == -1);
    assert(em_spu2_reverb_area(&r, 112, 111) == -1);
    assert(em_spu2_reverb_area(&r, 0, 128) == -1);
    assert(r.start == 16 && r.end == 111 && r.cursor == 16);
    assert(em_spu2_reverb_address(&r, 95) == 111);
    assert(em_spu2_reverb_address(&r, 96) == 16);
    assert(em_spu2_reverb_address(&r, -1) == 111);
    assert(em_spu2_reverb_address(&r, -97) == 111);
    assert(em_spu2_reverb_address(&r, INT64_MAX) == 47);
    assert(em_spu2_reverb_address(&r, INT64_MIN) == 80);
    r.cursor = 111;
    assert(em_spu2_reverb_address(&r, 1) == 16);
    const int16_t in[2] = {0, 0};
    int16_t out[2];
    em_spu2_reverb_step(&r, in, out);
    assert(r.cursor == 16);
    assert(em_spu2_reverb_area(&r, 64, 64) == 0);
    assert(em_spu2_reverb_address(&r, -100000) == 64);
}

static void reflections(void)
{
    EmSpu2Reverb r;
    int16_t ram[128], out[2];
    const int16_t in[2] = {4000, -4000};
    setup(&r, ram);
    r.params.input_volume[0] = r.params.input_volume[1] = 16384;
    r.params.iir_volume = r.params.wall_volume = 16384;
    put(&r, 7, 100); put(&r, 15, -100);
    put(&r, 23, 200); put(&r, 31, -200);
    put(&r, 40, 1000); put(&r, 44, -1000);
    put(&r, 48, -600); put(&r, 52, 600);
    em_spu2_reverb_step(&r, in, out);
    /* SAME L: ((4000/2 + 1000/2 - 100)/2) + 100 = 1300.
     * DIFF L reads RIGHT's 600: ((2000 + 300 - 200)/2)+200=1250.
     * The right half has all signs reversed. */
    assert(ram[16 + 8] == 1300);
    assert(ram[16 + 16] == -1300);
    assert(ram[16 + 24] == 1250);
    assert(ram[16 + 32] == -1250);
}

static void comb_and_allpass(void)
{
    EmSpu2Reverb r;
    int16_t ram[128], out[2];
    const int16_t in[2] = {0, 0};
    setup(&r, ram);
    for (unsigned i = 0; i < 4; ++i) {
        r.params.comb_read[0][i] = 40 + (int)i;
        r.params.comb_volume[i] = i & 1 ? -16384 : 16384;
    }
    put(&r, 40, 8000); put(&r, 41, 2000);
    put(&r, 42, 4000); put(&r, 43, 1000);
    put(&r, 63, 1000); put(&r, 79, -1000);
    r.params.apf_volume[0] = 16384;
    r.params.apf_volume[1] = -16384;
    em_spu2_reverb_step(&r, in, out);
    /* COMB = 4000-1000+2000-500 = 4500.
     * APF1 writes 4500-500 = 4000; returns 2000+1000 = 3000.
     * APF2 writes 3000-500 = 2500; returns -1250-1000 = -2250. */
    assert(ram[16 + 64] == 4000);
    assert(ram[16 + 80] == 2500);
    assert(out[0] == -2250);
}

static void disabled_tail(void)
{
    EmSpu2Reverb r;
    int16_t ram[128], copy[128], out[2];
    const int16_t in[2] = {32767, -32768};
    setup(&r, ram);
    put(&r, 79, 12345);
    put(&r, 87, -23456);
    memcpy(copy, ram, sizeof ram);
    r.write_enabled = 0;
    em_spu2_reverb_step(&r, in, out);
    assert(!memcmp(copy, ram, sizeof ram));
    assert(out[0] == 12345 && out[1] == -23456);
    assert(r.cursor == 17);
}

static void saturation_and_floor(void)
{
    EmSpu2Reverb r;
    int16_t ram[128], out[2];
    const int16_t in[2] = {0, 0};
    setup(&r, ram);
    r.params.wall_volume = -32768;
    r.params.iir_volume = 16384;
    put(&r, 40, -32768);
    put(&r, 7, 32767);
    put(&r, 44, 1);
    em_spu2_reverb_step(&r, in, out);
    assert(ram[24] == 32767);
    assert(ram[32] == -1); /* floor(-1 * 1/2), not truncation to zero */
    setup(&r, ram);
    r.params.iir_volume = -32768;
    put(&r, 7, 100);
    em_spu2_reverb_step(&r, in, out);
    assert(ram[24] == -200); /* documented PS1 IIR negative-unity quirk */
}

static void half_rate_and_return(void)
{
    EmSpu2Reverb r;
    int16_t ram[128], out[2];
    const int16_t zero[2] = {0, 0};
    setup(&r, ram);
    r.write_enabled = 0;
    r.return_volume[0] = 16384; /* half */
    r.return_volume[1] = -32768; /* invert */
    /* Every APF2 delayed read sees a constant; zero APF coefficients make
     * that read the half-rate output, without involving the input filter. */
    for (unsigned i = r.start; i <= r.end; ++i) ram[i] = 8000;
    em_spu2_reverb_process(&r, zero, out);
    assert(r.cursor == 16 && r.channel == 1);
    assert(out[0] == -1 && out[1] == 0); /* FIR -1, up gain 2, Q15 EVOL */
    em_spu2_reverb_process(&r, zero, out);
    assert(r.cursor == 17 && r.channel == 0);
    assert(out[0] == 0 && out[1] == 1);
    for (unsigned i = 2; i < 100; ++i)
        em_spu2_reverb_process(&r, zero, out);
    /* The documented coefficients sum to 32766: the even polyphase sum
     * is 16382, the odd centre tap 16384. Preserve that one-LSB ripple. */
    assert(out[0] == 4000 && out[1] == -7999);
    em_spu2_reverb_process(&r, zero, out);
    assert(out[0] == 3999 && out[1] == -8000);
    r.return_volume[0] = r.return_volume[1] = 0;
    em_spu2_reverb_process(&r, zero, out);
    assert(out[0] == 0 && out[1] == 0);
}

static void downsample_filter(void)
{
    EmSpu2Reverb r;
    int16_t ram[128], out[2];
    setup(&r, ram);
    r.params.input_volume[0] = r.params.input_volume[1] = -32768;
    r.params.iir_volume = -16384;
    const int16_t impulse[2] = {16384, 16384};
    const int16_t zero[2] = {0, 0};
    em_spu2_reverb_process(&r, impulse, out);
    /* First tap: floor(16384 * -1 / 32768) = -1.
     * Input inversion gives +1; IIR half-inversion gives -1. */
    assert(ram[24] == -1);
    em_spu2_reverb_process(&r, zero, out);
    assert(ram[32] == 0); /* right runs on FIR tap 1, which is zero */
    em_spu2_reverb_process(&r, zero, out);
    /* Tap 2: +1, inverted input -1; previous SAME value -1.
     * (-1 - (-1)) * -1/2 + (-1) = -1. */
    assert(ram[25] == -1);
}

int main(void)
{
    addresses();
    reflections();
    comb_and_allpass();
    disabled_tail();
    saturation_and_floor();
    half_rate_and_return();
    downsample_filter();
    puts("spu2_reverb_test: PASS (public algorithm; SPU2 capture equivalence unverified)");
    return 0;
}
