/* Linux headless audio sink. Pulls the same mixer at its requested rate,
 * without opening a device. ALSA windowed output is a separate runtime step. */
#include "em_audio.h"
#include "em_platform.h"
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
struct EmAudio {
    pthread_t thread;
    atomic_int stop, paused;
    int rate;
    EmAudioCallback cb;
    void *user;
};
static void *pull(void *opaque) {
    EmAudio *a=opaque;
    float pcm[512*2];
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC,&next);
    while (!atomic_load(&a->stop)) {
        if (!atomic_load(&a->paused)) a->cb(a->user,pcm,512);
        next.tv_nsec+=(long)(512000000000LL/a->rate);
        while (next.tv_nsec>=1000000000L) { next.tv_sec++; next.tv_nsec-=1000000000L; }
        int rc;
        do { rc=clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&next,NULL); }
        while (rc==EINTR && !atomic_load(&a->stop));
        if (rc && rc!=EINTR) break;
    }
    return NULL;
}
EmAudio *em_audio_create(int rate, EmAudioCallback cb, void *user) {
    if (!em_headless() || rate<=0 || !cb) return NULL;
    EmAudio *a=calloc(1,sizeof *a); if (!a) return NULL;
    a->rate=rate; a->cb=cb; a->user=user;
    atomic_init(&a->stop,0); atomic_init(&a->paused,0);
    if (pthread_create(&a->thread,NULL,pull,a)) { free(a); return NULL; }
    return a;
}
void em_audio_destroy(EmAudio *a) {
    if (!a) return;
    atomic_store(&a->stop,1); pthread_join(a->thread,NULL); free(a);
}
void em_audio_pause(EmAudio *a) { if (a) atomic_store(&a->paused,1); }
void em_audio_resume(EmAudio *a) { if (a) atomic_store(&a->paused,0); }
