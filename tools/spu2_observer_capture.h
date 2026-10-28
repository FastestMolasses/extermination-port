/* Private capture adapter, copied into an ignored build by prepare_spu2_observer.py.
 * Never linked into the production build. All payloads stay in the private
 * capture directory; every callback observes existing model state. */
#ifndef EM_SPU2_OBSERVER_CAPTURE_H
#define EM_SPU2_OBSERVER_CAPTURE_H
#include "game/em_iop_stream.h"
#include "game/em_sfx_bank.h"
int a2_enabled(void);
void a2_stream_created(EmIopStream *stream);
void a2_stream_key_on(int voice);
void a2_stream_block(uint64_t frame, int voice, const EmIopSpuVoice *p,
                     int32_t history1, int32_t history2);
void a2_stream_voice(uint64_t frame, int voice, const EmIopSpuVoice *p);
void a2_stream_dma(uint64_t frame, uint64_t tick, int channel, uint32_t address,
                   const uint8_t *data, uint32_t bytes);
void a2_stream_field(EmIopStream *stream);
void a2_sfx_init(const EmSfxDriver *driver);
uint64_t a2_sfx_effective(const EmSfxDriver *driver, unsigned command, uint32_t a, uint32_t b);
void a2_sfx_command(const EmSfxDriver *driver, uint64_t frame, const uint32_t words[4],
                    uint64_t effective_mask);
void a2_sfx_render(uint64_t frame, unsigned count);
void a2_sfx_voice(unsigned voice, unsigned offset, const EmSfxVoice *p,
                  uint64_t index, float fraction, float legacy_source);
void a2_mix_range(unsigned domain, uint64_t source_frame, uint64_t count, unsigned requested);
#endif
