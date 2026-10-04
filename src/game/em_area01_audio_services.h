#ifndef EM_AREA01_AUDIO_SERVICES_H
#define EM_AREA01_AUDIO_SERVICES_H
#include "game/em_area01_runtime.h"
#include "game/em_sdk_math_original.h"

/* Borrow the existing SDK math and committed output-mode owners. Scratch
 * and listener/camera fields come through the canonical memory provider.
 * The sole external call is 001FB9F0 through host.worker: that worker must
 * submit to the existing SFX track owner and return its actual v0. Scratch
 * is published before that boundary and at return, including on failure.
 * 001FC3C0/001FC520 borrow the existing SFX service tables/voices. Their
 * exact gain and every actual handle store use canonical callbacks; keep
 * the segment active because no native actor/player/collision worker runs.
 * Returns 0 handled, 1 unknown entry, -1 missing/unsupported view or fault. */
int em_area01_audio_services_call(const EmArea01RuntimeHost *, EmSdkMathContext *,
                                 const uint8_t *output_mode, EmArea01Call *);
#endif
