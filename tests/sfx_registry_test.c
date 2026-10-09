/* EMSR v2 registry + sound driver runtime test (driven by
 * tools/test_area11_sfx_runtime.py). Runs in a fixture directory holding
 * the exported registry, malformed registry copies, scopes.txt (one "id
 * area sub state" line per exported entry) and a refusal/ subdirectory
 * whose registry marks 0x452 UNSUPPORTED.
 *
 * The driver runs on the caller's thread: em_sfx_field is one NTSC field
 * (the sequencer tick, then the field's 48 kHz samples into the mixer
 * ring), em_sfx_mix drains the ring (chain step AUDIO).
 *
 * argv: malformed-count ids fields refusal-dir, where ids and fields are
 * comma-separated lists (hex ids; fields per render). */
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

/* D_00281D50 for the fixture: the handle of (group, bank) is its index. */
static int32_t handles[120];
static const int32_t *handle_table(void) { return handles; }

/* Samples of fields [first, first + count) on the field clock. */
static size_t field_samples(unsigned first, unsigned count)
{
    return (size_t)(em_sfx_tick_frame(first + count, 48000) - em_sfx_tick_frame(first, 48000));
}

static unsigned fields_run;

/* The play, then `fields` fields, the ring drained in `chunk`-frame mixes. */
static void render(unsigned id, unsigned chunk, unsigned fields, const float *position)
{
    const size_t count = field_samples(fields_run, fields);
    float *output = calloc(count * 2, sizeof *output);
    assert(output);
    if (position) em_sfx_play_at(id, position, 300.0f);
    else em_sfx_play(id);
    size_t at = 0;
    for (unsigned f = 0; f < fields; ++f) {
        assert(em_sfx_field() == 0);
        const size_t end = at + field_samples(fields_run++, 1);
        while (at < end) {
            size_t step = end - at < chunk ? end - at : chunk;
            em_sfx_mix(output + at * 2, (int)step, 48000);
            at += step;
        }
    }
    char path[100];
    snprintf(path, sizeof path, "reg_%X_%u%s.f32", id, chunk, position ? "_at" : "");
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

/* Main-loop step H's D_00281C30 <- D_00281B70 copy (001FB100's block_copy;
 * live, em_stream_live_step_h runs the whole translation over this view). */
static void step_h_copy(void)
{
    int32_t requested[48], snapshot[48];
    em_sfx_tables(requested, snapshot);
    em_sfx_set_snapshot(requested);
}

/* One field (its tick and samples), drained: 1 when any sample sounds. */
static int field_sounds(void)
{
    float buffer[2 * 1024];
    const size_t count = field_samples(fields_run++, 1);
    assert(count <= 1024 && em_sfx_field() == 0);
    memset(buffer, 0, sizeof buffer);
    em_sfx_mix(buffer, (int)count, 48000);
    int sounded = 0;
    for (size_t i = 0; i < 2 * count; ++i) sounded |= buffer[i] != 0.0f;
    return sounded;
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
    for (int i = 0; i < 120; ++i) handles[i] = i;
    em_sfx_bind_bank_handles(handle_table);
    assert(em_sfx_init() == (int)audible);
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

    /* Renders: sequencer ticks, the IOP exchange, allocation, pitch
     * cursor, loops, ADSR and Q14 words; the first id also once
     * positionally. Each render starts on a field boundary of one clock, so
     * the expected model (the runner) knows each one's first field. */
    assert(em_sfx_set_area(11, 0));
    FILE *starts = fopen("reg_starts.txt", "w");
    assert(starts);
    for (unsigned r = 0; r < renders; ++r) {
        fprintf(starts, "%u ", fields_run);
        render((unsigned)ids[r], 1, (unsigned)frames[r], NULL);
        fprintf(starts, "%u ", fields_run);
        render((unsigned)ids[r], 997, (unsigned)frames[r], NULL);
    }
    /* Any device rate but the SPU2's 48 kHz mixes nothing and is counted. */
    {
        float probe[64] = {0};
        uint64_t bad = 0;
        em_sfx_mix(probe, 32, 44100);
        em_sfx_mix_counters(NULL, NULL, &bad, NULL);
        assert(bad == 1);
        for (unsigned i = 0; i < 64; ++i) assert(probe[i] == 0.0f);
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
    fprintf(starts, "%u\n", fields_run);
    fclose(starts);
    render((unsigned)ids[0], 997, (unsigned)frames[0], source);
    assert(em_sfx_plays() == plays + 2 * (int)renders + 1);
    assert(device_calls == 2 * renders + 1);
    assert(!em_sfx_drops() && !em_sfx_voice_refusals() && !em_sfx_steals());

    /* 001FC3C0 service of the flame 0x413 at radius 100, ordinal 3: starts
     * on frame 7 (cadence 10). Its script keys the looping tone on and off
     * in one exchange; that key-off is lost (measured: CAPTURES_AUDIO.md,
     * the beats cage_roof and flame), so the voice sounds on, the reaper
     * never frees the track and every later check re-pans the same handle. */
    for (unsigned i = 0; i < 40; ++i) assert(!field_sounds());   /* the renders have ended */
    const float flame[3] = {30, 0, 40};
    int32_t handle = -1;
    int sounded_fields = 0;
    for (unsigned frame = 0; frame < 60; ++frame) {
        step_h_copy();
        const int32_t result = em_sfx_loop_service(&handle, 0x413, flame, 100.0f,
                                                   (int32_t)frame, 3);
        assert(result == handle);
        if (frame < 7) assert(handle == -1);
        else assert(handle == 0 && em_sfx_track_status(0) == 2);
        const int sounded = field_sounds();
        if (frame < 7 || frame > 7) assert(sounded == (frame > 7));
        sounded_fields += sounded;
    }
    assert(sounded_fields >= 52);
    /* 001FC520 on a live handle: stopped (status 0 at once), cleared; its
     * key-off reaches the voice at the next exchange (released, then
     * silent), and the reaper resets it. */
    em_sfx_loop_release(&handle);
    assert(handle == -1 && em_sfx_track_status(0) == 0);
    for (unsigned i = 0; field_sounds(); ++i) assert(i < 600);
    /* Out of range: 001FBD50 does not start. */
    const float far_flame[3] = {300, 0, 0};
    const int culls = em_sfx_culls();
    em_sfx_loop_service(&handle, 0x413, far_flame, 100.0f, 77, 3);
    assert(handle == -1 && em_sfx_culls() == culls + 1);

    /* stop-all retires a script with pending events: every track free at
     * once; the voices' zeroed ADSR and key-off reach the SPU2 model at the
     * next exchange, silent from the field after it. */
    em_sfx_play((unsigned)ids[0]);
    const int sounded = field_sounds();
    em_sfx_stop_all();
    for (int t = 0; t < 48; ++t) assert(em_sfx_track_status(t) == 0);
    (void)field_sounds();
    assert(!field_sounds() && !field_sounds());
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
