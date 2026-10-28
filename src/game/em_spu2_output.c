#include "game/em_spu2_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void em_spu2_output_init(EmSpu2Output *o)
{
    memset(o, 0, sizeof *o);
}

void em_spu2_output_command(EmSpu2Output *o, const EmSfxRegistry *registry,
                           int command, int index, uint32_t a, uint32_t b)
{
    if (!o || !registry || index < 0 || index >= EM_SFX_VOICES) return;
    EmSpu2OutputVoice *v = &o->voices[index];
    const uint64_t mask = (a & 0xffffffu) | (uint64_t)(b & 0xffffffu) << 24;
    switch (command) {
    case 1:
        em_spu2_volume_write(&v->volume[0], (uint16_t)a);
        em_spu2_volume_write(&v->volume[1], (uint16_t)b);
        break;
    case 3: v->adsr.adsr1 = (uint16_t)a; v->adsr.adsr2 = (uint16_t)b; break;
    case 5: v->next_sample = a < registry->sample_count ? &registry->samples[a] : NULL; break;
    case 6: v->pitch = (uint16_t)a; break;
    case 0xa:
        for (unsigned i = 0; i < EM_SFX_VOICES; ++i) {
            v = &o->voices[i];
            if (!(mask >> i & 1) || !v->next_sample) continue;
            v->sample = v->next_sample;
            v->phase = v->frames = 0;
            v->on = 1;
            em_spu2_adsr_key_on(&v->adsr, v->adsr.adsr1, v->adsr.adsr2);
        }
        break;
    case 0xb:
        for (unsigned i = 0; i < EM_SFX_VOICES; ++i) {
            v = &o->voices[i];
            /* Same-exchange KOFF loss is the existing, measured game contract.
             * The hardware key-on startup delay still needs separate capture. */
            if ((mask >> i & 1) && v->on && v->frames)
                em_spu2_adsr_key_off(&v->adsr);
        }
        break;
    case 0xc:
#ifdef EM_SPU2_OUTPUT_MUTATE_EON
        o->effect = 0;
#else
        o->effect = mask;
#endif
        break;
    case 0xd: o->noise = mask; break;
    default: break;
    }
}

static int16_t sample_at(const EmSfxSample *s, uint64_t index)
{
    if (index < s->frames) return s->pcm[index];
    if (s->loop_start == UINT32_MAX || s->loop_start >= s->frames) return 0;
    const uint64_t body = s->frames - s->loop_start;
    return s->pcm[s->frames + (index - s->frames) % body];
}

int em_spu2_output_step(EmSpu2Output *o, int16_t dry[2][2],
                         int16_t wet[2][2], EmSpu2OutputTap tap, void *context)
{
    if (!o || !dry || !wet || o->noise) return -1;
    int32_t d[2][2] = {{0}}, w[2][2] = {{0}};
    for (unsigned i = 0; i < EM_SFX_VOICES; ++i) {
        EmSpu2OutputVoice *v = &o->voices[i];
        /* Volume sweeps keep running even when the voice is unkeyed. */
        const int16_t gain[2] = {
            em_spu2_volume_step(&v->volume[0]),
            em_spu2_volume_step(&v->volume[1])
        };
        if (!v->on || !v->sample || !v->sample->pcm) continue;
        const EmSfxSample *s = v->sample;
        const uint64_t index = v->phase >> 12;
        if (v->adsr.phase == EM_SPU2_ADSR_OFF ||
            (s->loop_start == UINT32_MAX && index >= s->frames)) {
            v->on = 0;
            v->adsr.level = 0;
            v->adsr.phase = EM_SPU2_ADSR_OFF;
            continue;
        }
        int16_t history[4];
        for (unsigned k = 0; k < 4; ++k)
            history[k] = index < 3 - k ? 0 : sample_at(s, index - (3 - k));
        const int16_t value = em_spu2_apply_gain(
            em_spu2_gaussian(history, (uint16_t)(v->phase & 4095)),
            (int16_t)v->adsr.level);
        if (tap) tap(context, i, o->frames, value);
        for (unsigned side = 0; side < 2; ++side) {
            const int16_t scaled = em_spu2_apply_gain(value, gain[side]);
            d[i / 24][side] += scaled;
            if (o->effect >> i & 1) w[i / 24][side] += scaled;
        }
        em_spu2_adsr_step(&v->adsr);
        v->phase += v->pitch > 0x4000 ? 0x4000 : v->pitch;
        v->frames++;
    }
    for (unsigned core = 0; core < 2; ++core)
        for (unsigned side = 0; side < 2; ++side) {
            dry[core][side] = em_spu2_clamp(d[core][side]);
            wet[core][side] = em_spu2_clamp(w[core][side]);
        }
    o->frames++;
    return 0;
}

int em_spu2_output_render(EmSfxDriver *driver, float *out, unsigned frames,
                           unsigned rate, uint64_t first_frame,
                           const uint32_t (*pending)[4], unsigned pending_count)
{
    /* Owned by the game thread. Reset follows the existing render clock;
     * there is no change to driver initialization or shutdown. */
    static EmSpu2Output probe;
    static int enabled, initialized;
    static uint64_t next_frame;
    if (!initialized || first_frame != next_frame || first_frame == 0) {
        const char *mode = getenv("EM_SPU2_OUTPUT_TEST");
        enabled = mode && !strcmp(mode, "voice");
        em_spu2_output_init(&probe);
        initialized = 1;
        if (enabled)
            fprintf(stderr, "spu2 output: experimental dry voice bus; core/stream output unverified\n");
    }
    next_frame = first_frame + frames;
    if (!enabled)
        return em_sfx_driver_render(driver, out, frames, rate);
    if (rate != 48000 || probe.noise) {
        fprintf(stderr, "spu2 output: unsupported probe rate or noise routing\n");
        abort();
    }
    const int produced = em_sfx_driver_render(driver, NULL, frames, rate);
    for (unsigned f = 0; f < frames; ++f) {
        int16_t dry[2][2], wet[2][2];
        if (em_spu2_output_step(&probe, dry, wet, NULL, NULL) < 0) abort();
        if (out)
            for (unsigned side = 0; side < 2; ++side)
                out[2 * f + side] += em_spu2_clamp(
                    (int32_t)dry[0][side] + dry[1][side]) / 32768.0f;
    }
    for (unsigned i = 0; i < pending_count; ++i)
        em_spu2_output_command(&probe, driver->registry, (int)pending[i][0],
                              (int)pending[i][1], pending[i][2], pending[i][3]);
    return produced;
}
