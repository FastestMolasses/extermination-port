#ifndef EM_AIM_FIRE_SHOTS_H
#define EM_AIM_FIRE_SHOTS_H
#include <stddef.h>
#include <stdint.h>
/* Original addresses and raw float bits. Only actual callee arguments are
 * meaningful. Callback status is separate from original return values. */
typedef struct EmAimFireShotsCall {
    uint32_t a[6], f[4], v0, f0;
} EmAimFireShotsCall;
typedef struct EmAimFireShots {
    void *context;
    void *(*map)(void *, uint32_t address, size_t size, int writing);
    int (*call)(void *, uint32_t entry, EmAimFireShotsCall *);
} EmAimFireShots;
/* Five equipment one-shots plus 001860A0 surface resolution. For 001860A0,
 * actor and argument are its two vector addresses; other entries ignore
 * argument. Negative status stops on unmapped storage or a failed worker.
 * result is the original integer return (00187CC0 is void; result untouched). */
int em_aim_fire_shots_run(const EmAimFireShots *, uint32_t entry,
                         uint32_t actor, uint32_t argument, int32_t *result);
#endif
