/* The movie's sound on the shared output (em_movie_sound.h). One producer
 * (the game thread), one consumer (the audio thread). */
#include "game/em_movie_sound.h"

#include <stdatomic.h>

enum { RING_FRAMES = 16384 };

/* The ring and its counters: host output and the audio thread's read
 * position, nothing the game reads (em_movie_sound_host_state). */
static struct {
    float ring[RING_FRAMES * 2];
    _Atomic uint64_t write, read, pushed, overruns, bad_rate;
} s;

uint32_t em_movie_sound_field_frames(uint64_t field)
{
    /* 572/375 samples per half-line, 525 half-lines per field: 800.8. */
    return (uint32_t)((field + 1u) * 4004u / 5u - field * 4004u / 5u);
}

uint32_t em_movie_sound_push(const int16_t *pcm, uint32_t frames)
{
    uint64_t w = atomic_load_explicit(&s.write, memory_order_relaxed);
    const uint64_t r = atomic_load_explicit(&s.read, memory_order_acquire);
    uint32_t n = 0;
    for (; n < frames && w - r < RING_FRAMES; ++n, ++w) {
        s.ring[(w % RING_FRAMES) * 2] = (float)pcm[2 * n] / 32768.0f;
        s.ring[(w % RING_FRAMES) * 2 + 1] = (float)pcm[2 * n + 1] / 32768.0f;
    }
    atomic_store_explicit(&s.write, w, memory_order_release);
    atomic_fetch_add_explicit(&s.pushed, n, memory_order_relaxed);
    if (n < frames) atomic_fetch_add_explicit(&s.overruns, frames - n, memory_order_relaxed);
    return n;
}

void em_movie_sound_mix(float *out, int frames, int device_rate)
{
    if (frames <= 0) return;
    const uint64_t r = atomic_load_explicit(&s.read, memory_order_relaxed);
    const uint64_t w = atomic_load_explicit(&s.write, memory_order_acquire);
    if (w == r) return;
    if (device_rate != 48000) {
        atomic_fetch_add_explicit(&s.bad_rate, 1, memory_order_relaxed);
        atomic_store_explicit(&s.read, w, memory_order_release);
        return;
    }
    const uint64_t n = w - r < (uint64_t)frames ? w - r : (uint64_t)frames;
    for (uint64_t i = 0; i < n; ++i) {
        out[2 * i] += s.ring[((r + i) % RING_FRAMES) * 2];
        out[2 * i + 1] += s.ring[((r + i) % RING_FRAMES) * 2 + 1];
    }
    atomic_store_explicit(&s.read, r + n, memory_order_release);
}

void em_movie_sound_counters(uint64_t *pushed, uint64_t *overruns, uint64_t *bad_rate)
{
    if (pushed) *pushed = atomic_load_explicit(&s.pushed, memory_order_relaxed);
    if (overruns) *overruns = atomic_load_explicit(&s.overruns, memory_order_relaxed);
    if (bad_rate) *bad_rate = atomic_load_explicit(&s.bad_rate, memory_order_relaxed);
}

const void *em_movie_sound_host_state(size_t *size)
{
    *size = sizeof s;
    return &s;
}
