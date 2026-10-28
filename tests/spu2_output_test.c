#define _POSIX_C_SOURCE 200809L
#include "game/em_spu2_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Integration fixtures are synthetic. No assets, game recording, or device.
 * EM_SPU2_OUTPUT_MUTATE_EON must make this test fail. Arithmetic itself is
 * checked separately by spu2_dsp_test and spu2_reverb_test. */
static unsigned checks, failures;
#define CHECK(test) do { \
    ++checks; \
    if (!(test)) { ++failures; \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); } \
} while (0)

static int16_t pcm[112];
static EmSfxSample sample = {64, 16, pcm};
static EmSfxRegistry registry = {.sample_count = 1, .samples = &sample};
static const uint32_t commands[][4] = {
    {1, 4, 0x3FFF, 0x2000}, {3, 4, 0x000F, 0x1FC0},
    {5, 4, 0, 0}, {6, 4, 0x1000, 0}, {0xA, 0, 1u << 4, 0}
};
#define COMMANDS ((unsigned)(sizeof commands / sizeof commands[0]))

static void driver_commands(EmSfxDriver *driver)
{
    for (unsigned i = 0; i < COMMANDS; ++i)
        em_sfx_driver_apply(driver, (int)commands[i][0], (int)commands[i][1],
                            commands[i][2], commands[i][3]);
}

/* Driver commands are applied only after the audio ending at frame 8.
 * The observer receives the same words and boundary; it must not mutate
 * the real driver's command state or its established envelope feedback. */
static void render_sequence(const unsigned *chunks, unsigned n,
                            const char *mode, float out[80], EmSfxDriver *last)
{
    if (mode) CHECK(setenv("EM_SPU2_OUTPUT_TEST", mode, 1) == 0);
    else CHECK(unsetenv("EM_SPU2_OUTPUT_TEST") == 0);
    EmSfxDriver live, original;
    em_sfx_driver_init(&live, &registry, 0);
    original = live;
    memset(out, 0, 80 * sizeof *out);
    unsigned first = 0;
    for (unsigned i = 0; i < n; ++i) {
        unsigned frames = chunks[i];
        int boundary = first + frames == 8;
        float legacy[80] = {0};
        int expected = em_sfx_driver_render(&original, legacy, frames, 48000);
        int produced = em_spu2_output_render(&live, out + 2 * first, frames,
                           48000, first, boundary ? commands : NULL,
                           boundary ? COMMANDS : 0);
        CHECK(expected == produced);
        CHECK(memcmp(&live, &original, sizeof live) == 0);
        if (!mode || strcmp(mode, "voice"))
            CHECK(memcmp(out + 2 * first, legacy, 2 * frames * sizeof(float)) == 0);
        if (boundary) {
            driver_commands(&live);
            driver_commands(&original);
        }
        first += frames;
    }
    CHECK(first == 40);
    *last = live;
}

static void render_cases(void)
{
    const unsigned whole[] = {8, 32}, partitioned[] = {3, 5, 7, 9, 16};
    float old_pcm[80], unknown[80], whole_pcm[80], parts_pcm[80];
    EmSfxDriver old_driver, unknown_driver, whole_driver, parts_driver;
    render_sequence(whole, 2, NULL, old_pcm, &old_driver);
    render_sequence(partitioned, 5, "unknown", unknown, &unknown_driver);
    CHECK(memcmp(old_pcm, unknown, sizeof old_pcm) == 0);
    CHECK(memcmp(&old_driver, &unknown_driver, sizeof old_driver) == 0);
    render_sequence(whole, 2, "voice", whole_pcm, &whole_driver);
    render_sequence(partitioned, 5, "voice", parts_pcm, &parts_driver);
    CHECK(memcmp(whole_pcm, parts_pcm, sizeof whole_pcm) == 0);
    CHECK(memcmp(&whole_driver, &parts_driver, sizeof whole_driver) == 0);
    CHECK(memcmp(&whole_driver, &old_driver, sizeof whole_driver) == 0);
    CHECK(memcmp(whole_pcm, old_pcm, sizeof whole_pcm) != 0);
    for (unsigned i = 0; i < 16; ++i) CHECK(whole_pcm[i] == 0);
    CHECK(whole_pcm[16] == 0); /* first post-KON envelope level is zero */
    CHECK(whole_pcm[24] != 0);
    CHECK(unsetenv("EM_SPU2_OUTPUT_TEST") == 0);
}

static void configure_voice(EmSpu2Output *o, unsigned voice,
                            uint16_t left, uint16_t right)
{
    em_spu2_output_command(o, &registry, 1, (int)voice, left, right);
    em_spu2_output_command(o, &registry, 3, (int)voice, 0x000F, 0x1FC0);
    em_spu2_output_command(o, &registry, 5, (int)voice, 0, 0);
    em_spu2_output_command(o, &registry, 6, (int)voice, 0x1000, 0);
    uint32_t low = voice < 24 ? 1u << voice : 0;
    uint32_t high = voice < 24 ? 0 : 1u << (voice - 24);
    em_spu2_output_command(o, &registry, 0xA, 0, low, high);
}

static void routing_cases(void)
{
    EmSpu2Output o;
    em_spu2_output_init(&o);
    configure_voice(&o, 4, 0x3FFF, 0x2000);
    configure_voice(&o, 24, 0x2000, 0x3FFF);
    em_spu2_output_command(&o, &registry, 0xC, 0, 1u << 4, 0);
    int16_t dry[2][2], wet[2][2];
    for (unsigned i = 0; i < 6; ++i)
        CHECK(em_spu2_output_step(&o, dry, wet, NULL, NULL) == 0);
    CHECK(dry[0][0] != 0);
    CHECK(dry[0][1] != dry[0][0]);
    CHECK(wet[0][0] == dry[0][0] && wet[0][1] == dry[0][1]);
    CHECK(wet[1][0] == 0 && wet[1][1] == 0);
    CHECK(dry[1][0] == dry[0][1] && dry[1][1] == dry[0][0]);
    /* Route core 1 only. This catches reversing the two 24-bit mask words. */
    em_spu2_output_command(&o, &registry, 0xC, 0, 0, 1);
    CHECK(em_spu2_output_step(&o, dry, wet, NULL, NULL) == 0);
    CHECK(wet[0][0] == 0 && wet[0][1] == 0);
    CHECK(wet[1][0] == dry[1][0] && wet[1][1] == dry[1][1]);
    em_spu2_output_command(&o, &registry, 0xD, 0, 1u << 4, 0);
    EmSpu2Output snapshot = o;
    dry[0][0] = 123; wet[1][1] = 456;
    CHECK(em_spu2_output_step(&o, dry, wet, NULL, NULL) == -1);
    CHECK(memcmp(&o, &snapshot, sizeof o) == 0);
    CHECK(dry[0][0] == 123 && wet[1][1] == 456);
    em_spu2_output_command(&o, NULL, 1, 4, 0, 0);
    em_spu2_output_command(&o, &registry, 1, -1, 0, 0);
    em_spu2_output_command(&o, &registry, 1, 48, 0, 0);
    CHECK(memcmp(&o, &snapshot, sizeof o) == 0);

    /* Core buses saturate independently after summing their 24 voices. */
    em_spu2_output_init(&o);
    configure_voice(&o, 0, 0x3FFF, 0x4000);
    o.voices[0].adsr.phase = EM_SPU2_ADSR_SUSTAIN;
    o.voices[0].adsr.level = 32767;
    o.voices[0].phase = 8u * 4096u;
    for (unsigned i = 1; i < 24; ++i) o.voices[i] = o.voices[0];
    o.voices[24] = o.voices[0];
    em_spu2_output_command(&o, &registry, 0xC, 0, 0xFFFFFF, 0);
    CHECK(em_spu2_output_step(&o, dry, wet, NULL, NULL) == 0);
    CHECK(dry[0][0] == 32767 && dry[0][1] == -32768);
    CHECK(dry[1][0] > 0 && dry[1][0] < 32767);
    CHECK(dry[1][1] < 0 && dry[1][1] > -32768);
    CHECK(wet[0][0] == 32767 && wet[0][1] == -32768);
    CHECK(wet[1][0] == 0 && wet[1][1] == 0);
}

typedef struct { unsigned count, voice; uint64_t frame; int16_t value; } Tap;
static void tap(void *context, unsigned voice, uint64_t frame, int16_t value)
{
    Tap *t = context;
    t->count++;
    t->voice = voice; t->frame = frame; t->value = value;
}

static void loop_cases(void)
{
    int16_t loop_pcm[] = {1000, 2000, 3000, 4000, 5000, 6000};
    EmSfxSample loop = {4, 2, loop_pcm};
    EmSpu2Output o;
    em_spu2_output_init(&o);
    EmSpu2OutputVoice *v = &o.voices[4];
    v->sample = &loop; v->on = 1; v->pitch = 0x1000;
    v->adsr = (EmSpu2Adsr){ .adsr2 = 0x1FC0,
                          .phase = EM_SPU2_ADSR_SUSTAIN, .level = 32767 };
    v->phase = 4u * 4096u;
    em_spu2_volume_write(&v->volume[0], 0x3FFF);
    em_spu2_volume_write(&v->volume[1], 0x3FFF);
    const int16_t histories[][4] = {
        {2000,3000,4000,5000}, {3000,4000,5000,6000},
        {4000,5000,6000,5000}, {5000,6000,5000,6000}
    };
    for (unsigned i = 0; i < 4; ++i) {
        int16_t dry[2][2], wet[2][2];
        Tap t = {0};
        CHECK(em_spu2_output_step(&o, dry, wet, tap, &t) == 0);
        CHECK(t.count == 1 && t.voice == 4 && t.frame == i);
        CHECK(t.value == em_spu2_apply_gain(em_spu2_gaussian(histories[i], 0),
                                           32767));
    }
    /* SSA changes take effect on the next KON, not on the current voice. */
    em_spu2_output_command(&o, &registry, 5, 4, 0, 0);
    CHECK(v->sample == &loop && v->next_sample == &sample);
    em_spu2_output_command(&o, &registry, 0xA, 0, 1u << 4, 0);
    CHECK(v->sample == &sample && v->phase == 0 && v->adsr.level == 0);
    /* Measured legacy contract: same-boundary KOFF before a sample is lost. */
    em_spu2_output_command(&o, &registry, 0xB, 0, 1u << 4, 0);
    CHECK(v->adsr.phase == EM_SPU2_ADSR_ATTACK);
    int16_t dry[2][2], wet[2][2];
    CHECK(em_spu2_output_step(&o, dry, wet, NULL, NULL) == 0);
    em_spu2_output_command(&o, &registry, 0xB, 0, 1u << 4, 0);
    CHECK(v->adsr.phase == EM_SPU2_ADSR_RELEASE);
}

static void one_shot_cases(void)
{
    int16_t one_shot_pcm[] = {12000, -16000, 20000, -24000};
    EmSfxSample one_shot = {4, UINT32_MAX, one_shot_pcm};
    EmSpu2Output o;
    em_spu2_output_init(&o);
    EmSpu2OutputVoice *v = &o.voices[7];
    v->sample = &one_shot; v->on = 1; v->pitch = 0x1000;
    v->adsr = (EmSpu2Adsr){ .adsr2 = 0x1FC0,
                          .phase = EM_SPU2_ADSR_SUSTAIN, .level = 32767 };
    em_spu2_volume_write(&v->volume[0], 0x3FFF);
    em_spu2_volume_write(&v->volume[1], 0x3FFF);
    for (unsigned frame = 0; frame < 4; ++frame) {
        int16_t history[4] = {0}, dry[2][2], wet[2][2];
        for (unsigned k = 0; k <= frame; ++k)
            history[3 - frame + k] = one_shot_pcm[k];
        Tap t = {0};
        CHECK(em_spu2_output_step(&o, dry, wet, tap, &t) == 0);
        CHECK(t.count == 1 && t.voice == 7);
        CHECK(t.value == em_spu2_apply_gain(em_spu2_gaussian(history, 0), 32767));
    }
    int16_t dry[2][2], wet[2][2];
    Tap t = {0};
    CHECK(em_spu2_output_step(&o, dry, wet, tap, &t) == 0);
    CHECK(t.count == 0 && !v->on && v->adsr.level == 0);
    CHECK(v->adsr.phase == EM_SPU2_ADSR_OFF);
    CHECK(dry[0][0] == 0 && dry[0][1] == 0);
    /* Hardware pitch clipping is 0x4000, rather than 0x3fff. */
    v->on = 1; v->phase = 0; v->pitch = 0xFFFF;
    v->adsr.phase = EM_SPU2_ADSR_SUSTAIN; v->adsr.level = 32767;
    CHECK(em_spu2_output_step(&o, dry, wet, NULL, NULL) == 0);
    CHECK(v->phase == 0x4000);
}

int main(void)
{
    for (unsigned i = 0; i < 112; ++i)
        pcm[i] = (int16_t)(2000 + (i % 8) * 3000);
    render_cases();
    routing_cases();
    loop_cases();
    one_shot_cases();
    printf("SPU2 output observer: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
