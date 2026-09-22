#include "game/em_sfx.h"
#include "game/em_sfx_bank.h"
#include "game/em_bgm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned device_calls;
int em_bgm_device_ensure(int rate) { assert(rate == 48000); ++device_calls; return 0; }
int em_bgm_wav_read(const char *path, EmBgmWav *out, const char *tag)
{ (void)path; (void)out; (void)tag; assert(!"fixture must have no legacy WAV registry"); return -1; }

static void emit(unsigned rate, unsigned chunk)
{
    const size_t count = 30000;
    float *output = calloc(count * 2, sizeof *output);
    assert(output);
    em_sfx_play(0x3EF);
    for (size_t at = 0; at < count;) {
        size_t step = count - at < chunk ? count - at : chunk;
        em_sfx_mix(output + at * 2, (int)step, (int)rate);
        at += step;
    }
    char path[100];
    snprintf(path, sizeof path, "mix_%u_%u.f32", rate, chunk);
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(output, sizeof *output, count * 2, f) == count * 2);
    fclose(f);
    free(output);
}

int main(void)
{
    EmSfxBank bank = {0};
    assert(em_sfx_bank_load(&bank, "assets/sfx/area11/panel_sfx.emsf"));
    assert(bank.area == 11 && bank.sub == 0 && bank.count == 2);
    assert(bank.cues[0].flags == EM_SFX_CUE_ABSENT);
    assert(bank.cues[1].pitch == 862 && bank.cues[1].gain_l == 2217);
    int16_t *original_pcm = bank.cues[1].pcm;
    for (unsigned i = 0; i < 10; ++i) {
        char path[80];
        snprintf(path, sizeof path, "bad_%u.emsf", i);
        assert(!em_sfx_bank_load(&bank, path));
        assert(bank.cues[1].pcm == original_pcm && bank.cues[1].pcm[41] == -2048);
    }
    float pair[2] = {13, 17};
    assert(!em_sfx_cue_frame(&bank.cues[0], 0, 48000, pair));
    assert(!em_sfx_cue_frame(&bank.cues[1], UINT64_MAX, 48000, pair));
    assert(!em_sfx_cue_frame(&bank.cues[1], 0, 0, pair));
    assert(!em_sfx_cue_frame(&bank.cues[1], 0, 384001, pair));
    assert(pair[0] == 13 && pair[1] == 17);
    em_sfx_bank_free(&bank);

    assert(em_sfx_init() == 1);
    assert(em_sfx_cue_state(0x3EF) == 0);
    em_sfx_play(0x3EF);
    assert(!em_sfx_plays() && !device_calls);
    assert(em_sfx_set_area(11, 0));
    assert(em_sfx_cue_state(0x3EE) == 2 && em_sfx_cue_state(0x3EF) == 1);
    assert(em_sfx_cue_state(0xFFFF) == 0);
    em_sfx_play(0x3EE);
    assert(em_sfx_absent_cues() == 1 && !em_sfx_plays() && !device_calls);
    for (unsigned i = 0; i < 3; ++i) {
        unsigned rate = (unsigned[]){48000, 44100, 96000}[i];
        emit(rate, 1);
        emit(rate, 997);
    }
    assert(em_sfx_plays() == 6 && device_calls == 6);
    em_sfx_play(0x3EF);
    assert(em_sfx_set_area(-1, -1) && em_sfx_cue_state(0x3EF) == 0);
    float tail[1024] = {0};
    em_sfx_mix(tail, 512, 48000); /* clearing selection leaves immutable voice data alive */
    int audible = 0;
    for (unsigned i = 0; i < 1024; ++i) audible |= tail[i] != 0;
    assert(audible);
    em_sfx_stop_all();
    memset(tail, 0, sizeof tail);
    em_sfx_mix(tail, 512, 48000);
    for (unsigned i = 0; i < 1024; ++i) assert(tail[i] == 0);
    em_sfx_shutdown();

    assert(rename("assets/sfx/area11/panel_sfx.emsf", "bank.saved") == 0);
    assert(em_sfx_init() == 0 && !em_sfx_set_area(11, 0));
    assert(em_sfx_set_area(-1, -1));
    em_sfx_shutdown();
    assert(rename("bank.saved", "assets/sfx/area11/panel_sfx.emsf") == 0);
    puts("PASS scoped SFX load, absent remap, mixer lifetime and malformed resource cases");
    return 0;
}
