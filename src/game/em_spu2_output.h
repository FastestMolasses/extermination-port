#ifndef EM_SPU2_OUTPUT_H
#define EM_SPU2_OUTPUT_H

#include "game/em_sfx_bank.h"
#include "game/em_spu2_dsp.h"

/* Output-only observer. The existing driver continues to own timing and ENVX.
 * This is a measurement path, not a second sequencer. No game preset is stored
 * here. There is no core stage (core routing, effect return, master volume):
 * no permitted public source documents the SPU2 mixer order, so it waits for
 * black-box measurement. The integration outstanding on main is in
 * SPU2_OUTPUT.md. */
typedef struct {
    const EmSfxSample *sample, *next_sample;
    uint16_t pitch;
    EmSpu2Adsr adsr;
    EmSpu2Volume volume[2];
    uint64_t phase, frames;
    int on;
} EmSpu2OutputVoice;

typedef struct {
    EmSpu2OutputVoice voices[EM_SFX_VOICES];
    uint64_t effect, noise;
    uint64_t frames;
} EmSpu2Output;

void em_spu2_output_init(EmSpu2Output *output);
/* Observe the already scheduled IOP command at its application boundary.
 * No writes to registry, driver, tracks, or reply buffers. */
void em_spu2_output_command(EmSpu2Output *output, const EmSfxRegistry *registry,
                           int command, int voice, uint32_t a, uint32_t b);
/* The per-voice tap contains the mono post-ADSR, pre-volume sample. It is a
 * native diagnostic, and is not evidence of an isolated original voice. */
typedef void (*EmSpu2OutputTap)(void *, unsigned voice, uint64_t frame, int16_t);
/* One 48 kHz frame, returning each core's dry and wet send separately.
 * Nonzero NON is not implemented: returns -1 without advancing any state. */
int em_spu2_output_step(EmSpu2Output *output, int16_t dry[2][2],
                         int16_t wet[2][2], EmSpu2OutputTap tap, void *context);
/* Single render-path hook. By default this delegates byte-for-byte to the
 * established renderer. EM_SPU2_OUTPUT_TEST=voice selects the experimental
 * pre-core dry voice bus (no reverb or master-volume claim). pending commands
 * are observed AFTER these frames, at the same boundary as driver_apply.
 * The final chunk alone must receive pending_count > 0. */
int em_spu2_output_render(EmSfxDriver *driver, float *out, unsigned frames,
                           unsigned rate, uint64_t first_frame,
                           const uint32_t (*pending)[4], unsigned pending_count);

#endif
