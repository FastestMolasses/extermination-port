#include "game/em_startup_audio.h"
#include "game/em_bgm.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Device/file adapters are fixtures. The production scheduler, manifest
 * validation, queue, stop-generation protocol and mixer are linked below. */
static int device_rate;
int em_bgm_device_rate(void) { return device_rate; }
int em_bgm_device_ensure(int rate) { device_rate = rate; return 0; }
int em_bgm_wav_read(const char *path, EmBgmWav *wav, const char *tag)
{
    (void)tag;
    if (!strstr(path, "fixture.wav")) return -1;
    *wav = (EmBgmWav){malloc(4 * sizeof(int16_t)), 4, 1, 48000};
    assert(wav->pcm);
    const int16_t pcm[] = {16384, 8192, -8192, -16384};
    memcpy(wav->pcm, pcm, sizeof(pcm));
    return 0;
}

static unsigned tick, count, when[128], sample_ids[128];
static void note(void *unused, const EmStartupNote *event)
{
    (void)unused;
    assert(count < 128);
    when[count] = tick;
    sample_ids[count++] = event->sample;
}

static void scheduler(void)
{
    EmStartupSequencer seq;
    EmStartupCue title = {.id = 0x5DC, .count = 6,
        .notes = {{.wait=0,.sample=0}, {.wait=0,.sample=1},
                  {.wait=230,.sample=2}, {.wait=230,.sample=3},
                  {.wait=460,.sample=4}, {.wait=460,.sample=5}}};
    em_startup_sequencer_clear(&seq);
    assert(!em_startup_sequencer_play(&seq, &title));
    assert(count == 0);
    for (tick = 0; tick < 70; ++tick) em_startup_sequencer_tick(&seq, note, NULL);
    const unsigned expected[] = {0, 0, 29, 29, 58, 58};
    assert(count == 6);
    for (unsigned i = 0; i < 6; ++i) {
        assert(when[i] == expected[i]);
        assert(sample_ids[i] == i);
    }
    EmStartupCue boundary = {.count = 4, .notes = {
        {.wait=0}, {.wait=8}, {.wait=9}, {.wait=115}}};
    count = 0;
    assert(!em_startup_sequencer_play(&seq, &boundary));
    for (tick = 0; tick < 20; ++tick) em_startup_sequencer_tick(&seq, note, NULL);
    assert(count == 4 && when[0] == 0 && when[1] == 1 && when[2] == 2 && when[3] == 15);
    count = 0;
    assert(!em_startup_sequencer_play(&seq, &title));
    em_startup_sequencer_tick(&seq, note, NULL);
    em_startup_sequencer_clear(&seq);
    for (tick = 0; tick < 100; ++tick) em_startup_sequencer_tick(&seq, note, NULL);
    assert(count == 2);
    for (unsigned i = 0; i < 48; ++i) assert(!em_startup_sequencer_play(&seq, &title));
    assert(em_startup_sequencer_play(&seq, &title) == -1);
    em_startup_sequencer_clear(&seq);
    boundary.notes[2].wait = 7;
    assert(em_startup_sequencer_play(&seq, &boundary) == -1);
    boundary.count = 0;
    assert(em_startup_sequencer_play(&seq, &boundary) == -1);
}

static void close_audio(void)
{
    device_rate = 0; /* em_bgm_shutdown has joined the audio callback */
    em_startup_audio_shutdown();
}

static void put_manifest(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(!fclose(file));
}

static void runtime(void)
{
    char path[128];
    snprintf(path, sizeof(path), "/tmp/em-startup-audio-test-%ld.txt", (long)getpid());
    float pcm[20] = {0};
    em_startup_audio_mix(pcm, 10, 48000);
    assert(em_startup_audio_play(5) == -1);
    put_manifest(path, "EMSA 1\nsamples 1\nsample 0 fixture.wav\ncues 2\n"
        "cue 5 1\nevent 0 0 4096 8192 4096 33023 24528 128\n"
        "cue 6 2\nevent 0 0 2048 8192 0 33023 24528 128\n"
        "event 16 0 4096 0 8192 33023 24528 128\n");
    assert(!em_startup_audio_init(path));
    assert(em_startup_audio_init(path) == -1);
    assert(em_startup_audio_play(7) == -1);
    assert(!em_startup_audio_play(5));
    em_startup_audio_mix(pcm, 10, 48000); /* no tick, so no voice */
    for (unsigned i = 0; i < 20; ++i) assert(pcm[i] == 0);
    assert(!em_startup_audio_tick());
    em_startup_audio_mix(pcm, 10, 48000);
    const float expected[] = {.25f,.125f,.125f,.0625f,-.125f,-.0625f,-.25f,-.125f};
    for (unsigned i = 0; i < 8; ++i) assert(pcm[i] == expected[i]);
    for (unsigned i = 8; i < 20; ++i) assert(pcm[i] == 0);
    memset(pcm, 0, sizeof(pcm));
    assert(!em_startup_audio_play(6));
    assert(!em_startup_audio_tick());
    em_startup_audio_mix(pcm, 3, 48000);
    assert(pcm[0] == .25f && pcm[2] == .1875f && pcm[4] == .125f);
    assert(pcm[1] == 0 && pcm[3] == 0 && pcm[5] == 0);
    em_startup_audio_stop(); /* stops live sample AND delayed second layer */
    memset(pcm, 0, sizeof(pcm));
    for (unsigned i = 0; i < 5; ++i) assert(!em_startup_audio_tick());
    em_startup_audio_mix(pcm, 10, 48000);
    for (unsigned i = 0; i < 20; ++i) assert(pcm[i] == 0);
    assert(!em_startup_audio_play(5));
    assert(!em_startup_audio_tick()); /* queue an old-generation note */
    em_startup_audio_stop();
    assert(!em_startup_audio_play(5));
    assert(!em_startup_audio_tick()); /* keep only the new-generation note */
    em_startup_audio_mix(pcm, 1, 48000);
    assert(pcm[0] == .25f && pcm[1] == .125f);
    close_audio();
    const char *bad[] = {
        "EMSA 2\n", "EMSA 1\nsamples 0\n",
        "EMSA 1\nsamples 1\nsample 0 ../fixture.wav\n",
        "EMSA 1\nsamples 1\nsample 0 fixture.wav\ncues 1\ncue 5 1\nevent 0 1 4096 0 0 0 0 0\n",
        "EMSA 1\nsamples 1\nsample 0 fixture.wav\ncues 1\ncue 5 1\nevent 0 0 0 0 0 0 0 0\n",
        "EMSA 1\nsamples 1\nsample 0 fixture.wav\ncues 1\ncue 5 2\nevent 8 0 4096 0 0 0 0 0\nevent 0 0 4096 0 0 0 0 0\n"
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        put_manifest(path, bad[i]);
        assert(em_startup_audio_init(path) == -1);
        assert(!device_rate);
    }
    unlink(path);
}

int main(void)
{
    scheduler();
    runtime();
    puts("startup audio PASS (VLQ waits, layers, fixed pitch/gain, stop, manifest validation)");
    return 0;
}
