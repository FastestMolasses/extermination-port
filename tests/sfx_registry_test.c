/* EMSR registry runtime test (tools/test_area11_sfx_runtime.py drives it).
 * Runs in a fixture directory holding the exported registry, the audited
 * panel bank, malformed registry copies and scopes.txt (one
 * "id area sub state" line per exported entry, from the provenance JSON). */
#include "game/em_bgm.h"
#include "game/em_sfx.h"
#include "game/em_sfx_bank.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned device_calls;
int em_bgm_device_ensure(int rate) { assert(rate == 48000); ++device_calls; return 0; }

static void render(unsigned id, unsigned rate, unsigned chunk, size_t count,
                   const float *position)
{
    float *output = calloc(count * 2, sizeof *output);
    assert(output);
    if (position) em_sfx_play_at(id, position, 300.0f);
    else em_sfx_play(id);
    for (size_t at = 0; at < count;) {
        size_t step = count - at < chunk ? count - at : chunk;
        em_sfx_mix(output + at * 2, (int)step, (int)rate);
        at += step;
    }
    char path[100];
    snprintf(path, sizeof path, "reg_%X_%u_%u%s.f32", id, rate, chunk,
             position ? "_at" : "");
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(output, sizeof *output, count * 2, f) == count * 2);
    fclose(f);
    free(output);
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    const unsigned malformed = (unsigned)strtoul(argv[1], NULL, 0);
    const unsigned render_id = (unsigned)strtoul(argv[2], NULL, 0);
    const size_t frames = (size_t)strtoul(argv[3], NULL, 0);

    /* Loader: transactional, every malformed copy refused. */
    EmSfxRegistry registry = {0};
    assert(em_sfx_registry_load(&registry, "assets/sfx/sfx_registry.emsr"));
    const unsigned entries = registry.entry_count;
    EmSfxEntry *kept = registry.entries;
    unsigned audible = 0;
    for (unsigned i = 0; i < entries; ++i)
        audible += registry.entries[i].state == EM_SFX_STATE_AUDIBLE;
    for (unsigned i = 0; i < malformed; ++i) {
        char path[64];
        snprintf(path, sizeof path, "bad_%u.emsr", i);
        assert(!em_sfx_registry_load(&registry, path));
        assert(registry.entries == kept && registry.entry_count == entries);
    }
    assert(!em_sfx_registry_find(&registry, 0xFFFF, -1, -1));

    /* 0011A218 refuses a pair with either side outside +-0x1000. */
    uint16_t a[2], b[2];
    const EmSfxEvent *event = &registry.entries[0].events[0];
    em_sfx_volume_words(event->scalar, event->pan, 0x1000, 0x1000, a);
    em_sfx_volume_words(event->scalar, event->pan, 0x1001, 0x200, b);
    assert(!memcmp(a, b, sizeof a));
    em_sfx_volume_words(event->scalar, event->pan, 0, 0, b);
    assert(b[0] == 0 && b[1] == 0);
    assert(em_sfx_volume_gain(0x4000 - 1) > 0.99f && em_sfx_volume_gain(0x7FFF) < 0.0f);
    assert(em_sfx_request_word(0.99999f) == 4095 && em_sfx_request_word(-0.5f) == -2048);
    em_sfx_registry_free(&registry);

    /* Scope rules against every exported entry. */
    assert(em_sfx_init() == (int)audible + 1);
    FILE *scopes = fopen("scopes.txt", "r");
    assert(scopes);
    unsigned id;
    int area, sub, state, checked = 0;
    while (fscanf(scopes, "%x %d %d %d", &id, &area, &sub, &state) == 4) {
        const int expected = state == EM_SFX_STATE_AUDIBLE ? 1 :
                             state == EM_SFX_STATE_ABSENT ? 2 : 0;
        if (area < 0) {
            assert(em_sfx_set_area(-1, -1));
            assert(em_sfx_cue_state(id) == expected);
        } else {
            assert(em_sfx_set_area(-1, -1));
            /* area-dependent: no scope -> unavailable, never borrowed */
            assert(em_sfx_cue_state(id) == 0);
            assert(em_sfx_set_area(area, sub));
            assert(em_sfx_cue_state(id) == expected);
        }
        ++checked;
    }
    fclose(scopes);
    assert(checked == (int)entries);

    /* Refusals are counted and allocate nothing. */
    assert(em_sfx_set_area(-1, -1));
    const int plays = em_sfx_plays();
    em_sfx_play(0x401);             /* AREA11-scoped */
    assert(em_sfx_unscoped_cues() == 1 && em_sfx_plays() == plays);
    assert(em_sfx_set_area(2, 1));
    assert(em_sfx_cue_state(0x401) == 0);
    em_sfx_play(0x401);             /* not exported for 2.1: no borrowing */
    assert(em_sfx_unscoped_cues() == 2 && em_sfx_plays() == plays);
    assert(em_sfx_set_area(11, 0));
    em_sfx_play(0x452);             /* controller script */
    assert(em_sfx_unsupported_cues() == 1 && em_sfx_plays() == plays);
    assert(!device_calls);

    /* Renders: rational pitch cursor, sequencer tick starts, Q14 words. */
    const unsigned rates[] = {48000, 44100, 96000};
    for (unsigned i = 0; i < 3; ++i) {
        render(render_id, rates[i], 1, frames, NULL);
        render(render_id, rates[i], 997, frames, NULL);
    }
    const float player[3] = {0, 0, 0}, eye[3] = {0, 10, -40};
    em_sfx_listener(player, eye, 0.6f);
    const float source[3] = {-50, 0, 90};
    float gl, gr;
    assert(em_sfx_compute_gains(source, 300.0f, &gl, &gr));
    FILE *requests = fopen("reg_requests.txt", "w");
    assert(requests);
    fprintf(requests, "%d %d\n", (int)em_sfx_request_word(gl),
            (int)em_sfx_request_word(gr));
    fclose(requests);
    render(render_id, 48000, 997, frames, source);
    assert(em_sfx_plays() == plays + 7 && device_calls == 7);

    /* stop-all retires a script with pending events. */
    em_sfx_play(render_id);
    float tail[1024] = {0};
    em_sfx_mix(tail, 512, 48000);
    em_sfx_stop_all();
    memset(tail, 0, sizeof tail);
    em_sfx_mix(tail, 512, 48000);
    for (unsigned i = 0; i < 1024; ++i) assert(tail[i] == 0);
    em_sfx_shutdown();
    puts("PASS EMSR registry load/malformed, scopes, refusals and renders");
    return 0;
}
