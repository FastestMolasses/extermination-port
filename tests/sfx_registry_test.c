/* EMSR v2 registry + sound driver runtime test (driven by
 * tools/test_area11_sfx_runtime.py). Runs in a fixture directory holding
 * the exported registry, the audited panel bank, malformed registry copies,
 * scopes.txt (one "id area sub state" line per exported entry) and a
 * refusal/ subdirectory whose registry marks 0x452 UNSUPPORTED.
 *
 * argv: malformed-count ids frames refusal-dir, where ids and frames are
 * comma-separated lists (hex ids; output frames per render). */
#include "game/em_bgm.h"
#include "game/em_sfx.h"
#include "game/em_sfx_bank.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static unsigned parse_list(const char *text, unsigned long *out, unsigned max)
{
    unsigned n = 0;
    char *copy = strdup(text), *save = NULL;
    assert(copy);
    for (char *item = strtok_r(copy, ",", &save); item && n < max;
         item = strtok_r(NULL, ",", &save))
        out[n++] = strtoul(item, NULL, 0);
    free(copy);
    return n;
}

/* One game frame of 48 kHz output (one sequencer tick), asserting silence. */
static void silent_frame(unsigned frame)
{
    float buffer[2 * 1024];
    size_t count = (size_t)(em_sfx_tick_frame(frame + 1, 48000) -
                            em_sfx_tick_frame(frame, 48000));
    assert(count <= 1024);
    memset(buffer, 0, sizeof buffer);
    em_sfx_mix(buffer, (int)count, 48000);
    for (size_t i = 0; i < 2 * count; ++i) assert(buffer[i] == 0.0f);
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    const unsigned malformed = (unsigned)strtoul(argv[1], NULL, 0);
    unsigned long ids[8], frames[8];
    const unsigned renders = parse_list(argv[2], ids, 8);
    assert(renders && parse_list(argv[3], frames, 8) == renders);

    /* Loader: transactional, every malformed copy refused. */
    EmSfxRegistry registry = {0};
    assert(em_sfx_registry_load(&registry, "assets/sfx/sfx_registry.emsr"));
    const unsigned entries = registry.entry_count;
    EmSfxEntry *kept = registry.entries;
    unsigned audible = 0;
    const EmSfxOp *key_on = NULL;
    for (unsigned i = 0; i < entries; ++i) {
        const EmSfxEntry *entry = &registry.entries[i];
        audible += entry->state == EM_SFX_STATE_AUDIBLE;
        for (unsigned j = 0; j < entry->count && !key_on; ++j)
            if (entry->ops[j].kind == EM_SFX_OP_KEY_ON) key_on = &entry->ops[j];
    }
    assert(key_on);
    for (unsigned i = 0; i < malformed; ++i) {
        char path[64];
        snprintf(path, sizeof path, "bad_%u.emsr", i);
        assert(!em_sfx_registry_load(&registry, path));
        assert(registry.entries == kept && registry.entry_count == entries);
    }
    assert(!em_sfx_registry_find(&registry, 0xFFFF, -1, -1));
    /* The key-on pitch word is the 00117918 ladder (bend 0x40) * 44100/48000. */
    assert((uint32_t)((int64_t)em_sfx_ladder(&registry, key_on->center, key_on->note,
                                             key_on->fine, 0x40, key_on->range) *
                      44100 / 48000) == key_on->pitch);
    assert(em_sfx_ladder(&registry, 0, 0, -0xD1, 0x40, 0) == -1);

    /* 0011A218 refuses a pair with either side outside +-0x1000. */
    uint16_t a[2], b[2];
    em_sfx_volume_words(key_on->scalar, key_on->pan, 0x1000, 0x1000, a);
    em_sfx_volume_words(key_on->scalar, key_on->pan, 0x1001, 0x200, b);
    assert(!memcmp(a, b, sizeof a));
    em_sfx_volume_words(key_on->scalar, key_on->pan, 0, 0, b);
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
    assert(!device_calls);

    /* Renders: sequencer ticks, allocation, pitch cursor, loops, ADSR and
     * Q14 words; the first id also once positionally. */
    assert(em_sfx_set_area(11, 0));
    const unsigned rates[] = {48000, 44100, 96000};
    for (unsigned r = 0; r < renders; ++r)
        for (unsigned i = 0; i < 3; ++i) {
            render((unsigned)ids[r], rates[i], 1, frames[r], NULL);
            render((unsigned)ids[r], rates[i], 997, frames[r], NULL);
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
    render((unsigned)ids[0], 48000, 997, frames[0], source);
    assert(em_sfx_plays() == plays + 6 * (int)renders + 1);
    assert(device_calls == 6 * renders + 1);
    assert(!em_sfx_drops() && !em_sfx_voice_refusals() && !em_sfx_steals());

    /* 001FC3C0 service of the flame 0x413 at radius 100, ordinal 3: starts
     * on frames 7, 27, 47 (cadence 10); the KON/KOFF-in-one-flush voice is
     * silent under the SPU2 model, its track is reaped within three ticks,
     * so the check on frames 17, 37, 57 drops the handle. */
    const float flame[3] = {30, 0, 40};
    int32_t handle = -1;
    for (unsigned frame = 0; frame < 60; ++frame) {
        em_sfx_frame_snapshot();
        const int32_t result = em_sfx_loop_service(&handle, 0x413, flame, 100.0f,
                                                   (int32_t)frame, 3);
        assert(result == handle);
        const unsigned phase = frame < 7 ? 1 : (frame - 7) / 10 % 2;
        if (frame < 7 || phase == 1) assert(handle == -1);
        else {
            assert(handle == 0);
            if (frame % 10 == 7) assert(em_sfx_track_status(0) == 2);
        }
        silent_frame(frame);
    }
    assert(em_sfx_track_status(0) == 0);
    /* 001FC520 on a live handle: stopped (status 0 at once), cleared. */
    em_sfx_frame_snapshot();
    assert(em_sfx_loop_service(&handle, 0x413, flame, 100.0f, 67, 3) == 0);
    assert(em_sfx_track_status(0) == 2);
    em_sfx_loop_release(&handle);
    assert(handle == -1 && em_sfx_track_status(0) == 0);
    silent_frame(67);
    /* Out of range: 001FBD50 does not start. */
    const float far_flame[3] = {300, 0, 0};
    const int culls = em_sfx_culls();
    em_sfx_loop_service(&handle, 0x413, far_flame, 100.0f, 77, 3);
    assert(handle == -1 && em_sfx_culls() == culls + 1);

    /* stop-all retires a script with pending events. */
    em_sfx_play((unsigned)ids[0]);
    float tail[1024] = {0};
    em_sfx_mix(tail, 512, 48000);
    int sounded = 0;
    for (unsigned i = 0; i < 1024; ++i) sounded |= tail[i] != 0;
    em_sfx_stop_all();
    memset(tail, 0, sizeof tail);
    em_sfx_mix(tail, 512, 48000);
    for (unsigned i = 0; i < 1024; ++i) assert(tail[i] == 0);
    for (int t = 0; t < 48; ++t) assert(em_sfx_track_status(t) == 0);
    assert(sounded);
    em_sfx_shutdown();

    /* UNSUPPORTED entries are refused and counted, allocating nothing. */
    assert(chdir(argv[4]) == 0);
    const unsigned calls = device_calls;
    assert(em_sfx_init() > 0);
    assert(em_sfx_set_area(11, 0));
    em_sfx_play(0x452);
    assert(em_sfx_unsupported_cues() == 1 && em_sfx_plays() == 0);
    assert(device_calls == calls && em_sfx_cue_state(0x452) == 0);
    em_sfx_shutdown();
    puts("PASS EMSR v2 load/malformed, scopes, refusals, renders, 001FC3C0 service, stop-all");
    return 0;
}
