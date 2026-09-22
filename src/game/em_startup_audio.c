#include "game/em_startup_audio.h"
#include "game/em_bgm.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_CUES = 16, MAX_SAMPLES = 32, QUEUE_SIZE = 512, VOICES = 48 };

void em_startup_sequencer_clear(EmStartupSequencer *seq)
{
    memset(seq, 0, sizeof(*seq));
}

int em_startup_sequencer_play(EmStartupSequencer *seq, const EmStartupCue *cue)
{
    if (!cue || !cue->count || cue->count > EM_STARTUP_AUDIO_EVENTS)
        return -1;
    for (unsigned n = 1; n < cue->count; ++n)
        if (cue->notes[n].wait < cue->notes[n - 1].wait) return -1;
    for (unsigned i = 0; i < EM_STARTUP_AUDIO_TRACKS; ++i) {
        if (!seq->tracks[i].cue) {
            seq->tracks[i] = (EmStartupAudioTrack){cue, 0, 0};
            return 0;
        }
    }
    return -1;
}

void em_startup_sequencer_tick(EmStartupSequencer *seq,
                               EmStartupNoteFn emit, void *user)
{
    for (unsigned i = 0; i < EM_STARTUP_AUDIO_TRACKS; ++i) {
        EmStartupAudioTrack *track = &seq->tracks[i];
        if (!track->cue) continue;
        while (track->next < track->cue->count &&
               track->cue->notes[track->next].wait <= track->elapsed) {
            if (emit) emit(user, &track->cue->notes[track->next]);
            ++track->next;
        }
        if (track->next == track->cue->count) track->cue = NULL;
        else track->elapsed += 8; /* (0x1E0000 / 60) / (1 << 12) */
    }
}

typedef struct { EmStartupNote note; unsigned generation; } QueuedNote;
typedef struct { EmStartupNote note; double position; int active; } Voice;

static struct {
    EmStartupSequencer sequencer;             /* game thread only */
    EmStartupCue cues[MAX_CUES];
    EmBgmWav samples[MAX_SAMPLES];            /* immutable while enabled */
    unsigned cue_count, sample_count;
    QueuedNote queue[QUEUE_SIZE];             /* single producer/consumer */
    atomic_uint write, read, generation;
    atomic_int enabled, overflow;
    Voice voices[VOICES];                     /* audio thread only */
    unsigned audio_generation;
} s;

static void release_samples(void)
{
    for (unsigned i = 0; i < MAX_SAMPLES; ++i) {
        free(s.samples[i].pcm);
        memset(&s.samples[i], 0, sizeof(s.samples[i]));
    }
    s.sample_count = s.cue_count = 0;
}

int em_startup_audio_init(const char *manifest)
{
    char tag[32], name[256], path[2048];
    unsigned version, count, id;
    FILE *file;
    if (!manifest || em_bgm_device_rate() || atomic_load(&s.enabled)) return -1;
    file = fopen(manifest, "r");
    if (!file) return -1;
    if (fscanf(file, "%31s %u", tag, &version) != 2 ||
        strcmp(tag, "EMSA") || version != 1) goto fail;
    if (fscanf(file, "%31s %u", tag, &count) != 2 ||
        strcmp(tag, "samples") || !count || count > MAX_SAMPLES) goto fail;
    s.sample_count = count;
    const char *slash = strrchr(manifest, '/');
    size_t prefix = slash ? (size_t)(slash - manifest + 1) : 0;
    if (prefix >= sizeof(path)) goto fail;
    memcpy(path, manifest, prefix);
    for (unsigned i = 0; i < s.sample_count; ++i) {
        if (fscanf(file, "%31s %u %255s", tag, &id, name) != 3 ||
            strcmp(tag, "sample") || id != i || strchr(name, '/') ||
            strchr(name, '\\') || strstr(name, "..")) goto fail;
        if (prefix + strlen(name) + 1 > sizeof(path)) goto fail;
        strcpy(path + prefix, name);
        if (em_bgm_wav_read(path, &s.samples[i], "startup-audio") ||
            s.samples[i].channels != 1 || s.samples[i].rate != 48000 ||
            s.samples[i].nframes < 1) goto fail;
    }
    if (fscanf(file, "%31s %u", tag, &count) != 2 || strcmp(tag, "cues") ||
        !count || count > MAX_CUES) goto fail;
    s.cue_count = count;
    for (unsigned i = 0; i < s.cue_count; ++i) {
        EmStartupCue *cue = &s.cues[i];
        if (fscanf(file, "%31s %u %u", tag, &cue->id, &cue->count) != 3 ||
            strcmp(tag, "cue") || !cue->count ||
            cue->count > EM_STARTUP_AUDIO_EVENTS) goto fail;
        for (unsigned j = 0; j < i; ++j)
            if (s.cues[j].id == cue->id) goto fail;
        for (unsigned j = 0; j < cue->count; ++j) {
            unsigned wait, sample, pitch, left, right, adsr1, adsr2, flags;
            if (fscanf(file, "%31s %u %u %u %u %u %u %u %u", tag, &wait,
                &sample, &pitch, &left, &right, &adsr1, &adsr2, &flags) != 9 ||
                strcmp(tag, "event") || sample >= s.sample_count || !pitch ||
                pitch > 0x3FFF || left > 0x3FFF || right > 0x3FFF ||
                adsr1 > 65535 || adsr2 > 65535 || flags > 255 ||
                (j && wait < cue->notes[j - 1].wait)) goto fail;
            cue->notes[j] = (EmStartupNote){wait, (uint16_t)sample,
                (uint16_t)pitch, (uint16_t)left, (uint16_t)right,
                (uint16_t)adsr1, (uint16_t)adsr2, (uint8_t)flags};
        }
    }
    if (fscanf(file, "%31s", tag) != EOF) goto fail;
    fclose(file);
    em_startup_sequencer_clear(&s.sequencer);
    memset(s.voices, 0, sizeof(s.voices));
    atomic_store(&s.read, 0);
    atomic_store(&s.write, 0);
    atomic_store(&s.generation, 0);
    atomic_store(&s.overflow, 0);
    s.audio_generation = 0;
    atomic_store_explicit(&s.enabled, 1, memory_order_release);
    if (em_bgm_device_ensure(48000)) {
        atomic_store(&s.enabled, 0);
        release_samples();
        return -1;
    }
    return 0;
fail:
    fclose(file);
    release_samples();
    return -1;
}

int em_startup_audio_play(unsigned cue)
{
    if (!atomic_load(&s.enabled)) return -1;
    for (unsigned i = 0; i < s.cue_count; ++i)
        if (s.cues[i].id == cue)
            return em_startup_sequencer_play(&s.sequencer, &s.cues[i]);
    return -1;
}

static void queue_note(void *unused, const EmStartupNote *note)
{
    (void)unused;
    unsigned write = atomic_load_explicit(&s.write, memory_order_relaxed);
    unsigned read = atomic_load_explicit(&s.read, memory_order_acquire);
    if (write - read >= QUEUE_SIZE) {
        atomic_store(&s.overflow, 1);
        return;
    }
    s.queue[write % QUEUE_SIZE] = (QueuedNote){*note, atomic_load(&s.generation)};
    atomic_store_explicit(&s.write, write + 1, memory_order_release);
}

int em_startup_audio_tick(void)
{
    if (!atomic_load(&s.enabled)) return 0;
    em_startup_sequencer_tick(&s.sequencer, queue_note, NULL);
    return atomic_exchange(&s.overflow, 0) ? -1 : 0;
}

void em_startup_audio_stop(void)
{
    em_startup_sequencer_clear(&s.sequencer);
    atomic_fetch_add_explicit(&s.generation, 1, memory_order_release);
}

void em_startup_audio_shutdown(void)
{
    /* Caller must have stopped the shared device, including its callback. */
    atomic_store(&s.enabled, 0);
    em_startup_sequencer_clear(&s.sequencer);
    release_samples();
}

void em_startup_audio_mix(float *out, int frames, int device_rate)
{
    if (!atomic_load_explicit(&s.enabled, memory_order_acquire) ||
        !out || frames <= 0 || device_rate <= 0) return;
    /* Snapshot publication before the stop generation. A concurrent stop
     * followed by play must not make us discard notes from a NEW generation
     * merely because our generation snapshot preceded their publication. */
    unsigned read = atomic_load_explicit(&s.read, memory_order_relaxed);
    unsigned write = atomic_load_explicit(&s.write, memory_order_acquire);
    unsigned generation = atomic_load_explicit(&s.generation, memory_order_acquire);
    if (s.audio_generation != generation) {
        memset(s.voices, 0, sizeof(s.voices));
        s.audio_generation = generation;
    }
    while (read != write) {
        const QueuedNote *queued = &s.queue[read % QUEUE_SIZE];
        if (queued->generation == generation) {
            unsigned i;
            for (i = 0; i < VOICES; ++i) if (!s.voices[i].active) break;
            if (i != VOICES) s.voices[i] = (Voice){queued->note, 0, 1};
            else atomic_store(&s.overflow, 1); /* no invented priority stealing */
        }
        ++read;
    }
    atomic_store_explicit(&s.read, read, memory_order_release);
    for (unsigned i = 0; i < VOICES; ++i) {
        Voice *voice = &s.voices[i];
        if (!voice->active) continue;
        const EmBgmWav *sample = &s.samples[voice->note.sample];
        double step = voice->note.pitch * (48000.0 / 4096.0) / device_rate;
        for (int frame = 0; frame < frames; ++frame) {
            if (voice->position >= sample->nframes) {
                voice->active = 0;
                break;
            }
            long at = (long)voice->position;
            long next = at + 1 < sample->nframes ? at + 1 : at;
            float fraction = (float)(voice->position - at);
            float pcm = ((float)sample->pcm[at] +
                ((float)sample->pcm[next] - sample->pcm[at]) * fraction) / 32768.0f;
            out[frame * 2] += pcm * (voice->note.left / 16384.0f);
            out[frame * 2 + 1] += pcm * (voice->note.right / 16384.0f);
            voice->position += step;
        }
    }
}
