/* Offline experiment bridge. All inputs come from a private capture manifest;
 * no game preset, routing mask, master volume or timing is inferred here.
 * Voice stages only: core routing, master volume and the core effect stage
 * are not modelled (no permitted public source documents the SPU2 mixer
 * order or MMIX; see docs/SPU2_OUTPUT.md "Sources and clean-room boundary"). */
#include "game/em_spu2_dsp.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Serialized little-endian by the Python caller on macOS arm64.
 * flags bit 0: source history is available for this frame.
 * key_events: chronological 4-bit events, least significant first; 1=KON,
 * 2=KOFF. Capture normalization must preserve events at the same sample.
 * baseline is the native source interpolator's value, before ENVX/gain.
 * Every frame is present, including silence, so volume sweeps keep running. */
typedef struct {
    int16_t history[4];
    uint16_t fraction, legacy_envx, adsr1, adsr2, volume[2];
    uint32_t flags, key_events;
    float baseline;
    float legacy_gain[2];
} StageVoiceFrame;

_Static_assert(sizeof(StageVoiceFrame) == 40, "private frame schema");

/* Outputs four stereo stages, in order: reconstructed native voice,
 * Gaussian with native envelope/gain, new ADSR with native gain, integer
 * voice volume. This does not reproduce any hardware key-on startup delay.
 * Envelope is sampled before its step, matching em_spu2_output_step. */
int spu2_stage_voice(const StageVoiceFrame *in, size_t frames, float *out)
{
    if (!in || !out || !frames) return -1;
    EmSpu2Adsr adsr = {0};
    EmSpu2Volume volume[2] = {{0}};
    for (size_t f = 0; f < frames; ++f) {
        const StageVoiceFrame *r = in + f;
        uint32_t keys = r->key_events;
        if (r->fraction >= 4096 || r->legacy_envx > 32767 ||
            (r->flags & ~1u)) return -1;
        while (keys) {
            unsigned action = keys & 15u;
            if (action == 1) em_spu2_adsr_key_on(&adsr, r->adsr1, r->adsr2);
            else if (action == 2) em_spu2_adsr_key_off(&adsr);
            else return -1;
            keys >>= 4;
        }
        adsr.adsr1 = r->adsr1;
        adsr.adsr2 = r->adsr2;
        int16_t gain[2];
        for (unsigned side = 0; side < 2; ++side) {
            em_spu2_volume_write(&volume[side], r->volume[side]);
            gain[side] = em_spu2_volume_step(&volume[side]);
        }
        float *result = out + f * 8;
        memset(result, 0, 8 * sizeof *result);
        if (r->flags & 1u) {
            const int16_t gaussian = em_spu2_gaussian(r->history, r->fraction);
            const int16_t envelope = em_spu2_apply_gain(gaussian, (int16_t)adsr.level);
            for (unsigned side = 0; side < 2; ++side) {
                float old_gain = r->legacy_gain[side];
                result[side] = r->baseline / 32768.0f *
                              ((float)r->legacy_envx / 32768.0f) * old_gain;
                result[2 + side] = (float)gaussian / 32768.0f *
                                  ((float)r->legacy_envx / 32768.0f) * old_gain;
                result[4 + side] = (float)envelope / 32768.0f * old_gain;
                result[6 + side] = (float)em_spu2_apply_gain(envelope, gain[side]) / 32768.0f;
            }
        }
        em_spu2_adsr_step(&adsr);
    }
    return 0;
}

typedef struct {
    uint64_t frame;
    uint32_t domain, voice;
    int16_t history[4];
    uint16_t fraction, envx, volume[2], adsr1, adsr2;
    float gain[2], baseline;
    uint64_t source_index;
    uint32_t source_id, flags;
} StageCaptureRow;
typedef struct {
    uint64_t frame;
    uint32_t mask, keys;
    uint16_t adsr1, adsr2, volume[2];
} StageEvent;
_Static_assert(sizeof(StageCaptureRow) == 64, "capture row schema");
_Static_assert(offsetof(StageCaptureRow, source_index) == 48, "capture row offsets");
_Static_assert(sizeof(StageEvent) == 24, "event schema");

/* Normalize one voice without reconstructing history from a mixed/tapped
 * sample stream. Ordered events supply inactive-frame sweep controls. */
int spu2_stage_capture(const StageCaptureRow *rows, size_t count,
                       const StageEvent *events, size_t event_count,
                       size_t frames, float *out)
{
    if (!rows || !count || !events || !event_count || !frames || !out ||
        frames > SIZE_MAX / sizeof(StageVoiceFrame)) return -1;
    StageVoiceFrame *dense = calloc(frames, sizeof *dense);
    if (!dense) return -1;
    StageVoiceFrame state = {0};
    size_t next_row = 0, next_event = 0;
    unsigned known = 0, keyed = 0;
    int result = -1;
    for (size_t f = 0; f < frames; ++f) {
        state.key_events = 0;
        unsigned shift = 0;
        while (next_event < event_count && events[next_event].frame == f) {
            const StageEvent *event = events + next_event++;
            if (event->mask & ~15u) goto done;
            if (event->mask & 1u) state.adsr1 = event->adsr1;
            if (event->mask & 2u) state.adsr2 = event->adsr2;
            if (event->mask & 4u) state.volume[0] = event->volume[0];
            if (event->mask & 8u) state.volume[1] = event->volume[1];
            known |= event->mask;
            uint32_t keys = event->keys;
            while (keys) {
                unsigned key = keys & 15u;
                if (shift >= 32 || (key != 1 && key != 2)) goto done;
                if (key == 1) {
                    if ((known & 3u) != 3u) goto done;
                    keyed = 1;
                }
                state.key_events |= key << shift;
                shift += 4;
                keys >>= 4;
            }
        }
        dense[f] = state;
        if (next_row < count && rows[next_row].frame == f) {
            const StageCaptureRow *row = rows + next_row++;
            if (!keyed || row->flags || row->fraction >= 4096 ||
                row->envx > 32767 || !isfinite(row->baseline) ||
                !isfinite(row->gain[0]) || !isfinite(row->gain[1])) goto done;
            /* Inactive unknown controls may start at observed model zero;
             * active controls must be established by applied events. */
            if (known != 15u || row->adsr1 != state.adsr1 ||
                row->adsr2 != state.adsr2 || row->volume[0] != state.volume[0] ||
                row->volume[1] != state.volume[1]) goto done;
            memcpy(dense[f].history, row->history, sizeof row->history);
            dense[f].fraction = row->fraction;
            dense[f].legacy_envx = row->envx;
            dense[f].baseline = row->baseline;
            memcpy(dense[f].legacy_gain, row->gain, sizeof row->gain);
            dense[f].flags = 1;
        }
        if ((next_row < count && rows[next_row].frame <= f) ||
            (next_event < event_count && events[next_event].frame <= f)) goto done;
    }
    if (next_row != count || next_event != event_count) goto done;
    result = spu2_stage_voice(dense, frames, out);
done:
    free(dense);
    return result;
}

void spu2_stage_add(float *sum, const float *voice, size_t frames)
{
    for (size_t i = 0; i < frames * 8; ++i) sum[i] += voice[i];
}

/* Native replay's nearest-even conversion, saturating before conversion. */
void spu2_stage_pcm(const float *in, size_t frames, unsigned stage, int16_t *out)
{
    for (size_t f = 0; f < frames; ++f)
        for (unsigned side = 0; side < 2; ++side) {
            float value = in[f * 8 + stage * 2 + side] * 32768.0f;
            out[f * 2 + side] = value >= 32767 ? 32767 : value <= -32768 ? -32768 :
                                (int16_t)lrintf(value);
        }
}
