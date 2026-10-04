/* em_audio_ios.m — iOS audio output backend on AudioToolbox (RemoteIO).
 *
 * Clean-room, no third-party libraries; only Apple system frameworks
 * (AudioToolbox, AVFAudio through AVFoundation). Manual reference counting.
 *
 * The same pull model as the macOS backend (em_audio_mac.c): the RemoteIO
 * unit's INPUT scope (bus 0) takes 32-bit float interleaved stereo at the
 * requested rate (the game asks for the PS2's 48000 Hz) and the unit's own
 * converter resamples to the hardware. The render proc hands its single
 * interleaved buffer straight to the em_audio.h callback, on Core Audio's
 * real-time I/O thread; any malformed request is answered with silence.
 *
 * iOS differences from macOS:
 *   - the output is kAudioUnitSubType_RemoteIO (DefaultOutput is macOS only);
 *   - an AVAudioSession must be active. Its category is Playback, so the game
 *     is heard with the ring/silent switch on silent;
 *   - the system stops the unit on an interruption (a call, Siri); it is
 *     restarted when the interruption ends, unless the platform layer has
 *     suspended audio because the game is parked (em_audio_ios.h).
 *
 * Headless runs (em_headless()) keep the unit and its pull cadence but
 * zero what the callback wrote, as on macOS.
 */
#include "em_audio.h"
#include "em_platform.h"
#include "audio/ios/em_audio_ios.h"

#import <AVFoundation/AVFoundation.h>
#include <AudioToolbox/AudioToolbox.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct EmAudio {
    AudioUnit       unit;
    EmAudioCallback cb;
    void           *user;
    int             silent;   /* em_headless() at create: mute the output */
    int             paused;   /* em_audio_pause: the game asked for a stop */
    EmAudio        *next;     /* the open units (guarded by s_lock) */
};

/* The open units, for suspension and interruptions. em_audio_create and
 * em_audio_destroy run on the game thread, the lifecycle hooks on the main
 * thread: the list is guarded by a mutex (never touched by the I/O thread). */
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static EmAudio *s_units;
static int      s_suspended;
static int      s_session_ready;
static id       s_interruption_observer;

static OSStatus em_audio_render(void *inRefCon,
                                AudioUnitRenderActionFlags *ioActionFlags,
                                const AudioTimeStamp *inTimeStamp,
                                UInt32 inBusNumber, UInt32 inNumberFrames,
                                AudioBufferList *ioData)
{
    EmAudio *a = (EmAudio *)inRefCon;
    (void)ioActionFlags;
    (void)inTimeStamp;
    (void)inBusNumber;

    if (!ioData)
        return noErr;
    if (ioData->mNumberBuffers == 1 && ioData->mBuffers[0].mData &&
        ioData->mBuffers[0].mDataByteSize >=
            inNumberFrames * 2 * sizeof(float)) {
        a->cb(a->user, (float *)ioData->mBuffers[0].mData,
              (int)inNumberFrames);
        if (a->silent)
            memset(ioData->mBuffers[0].mData, 0,
                   inNumberFrames * 2 * sizeof(float));
        return noErr;
    }
    for (UInt32 i = 0; i < ioData->mNumberBuffers; i++)
        if (ioData->mBuffers[i].mData)
            memset(ioData->mBuffers[i].mData, 0,
                   ioData->mBuffers[i].mDataByteSize);
    return noErr;
}

/* Start every open unit the game has not paused (lock held). */
static void start_units_locked(void)
{
    for (EmAudio *a = s_units; a; a = a->next)
        if (!a->paused)
            AudioOutputUnitStart(a->unit);
}

int em_audio_ios_session_begin(void)
{
    @autoreleasepool {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;
        if (!s_session_ready) {
            if (![session setCategory:AVAudioSessionCategoryPlayback error:&error]) {
                fprintf(stderr, "audio: AVAudioSession category Playback refused: %s\n",
                        error.localizedDescription.UTF8String);
                return -1;
            }
            [session setPreferredSampleRate:48000.0 error:nil];
            /* Interruptions (a call, Siri) stop the unit; restart it when the
             * interruption ends, unless the game is parked. */
            s_interruption_observer = [[[NSNotificationCenter defaultCenter]
                addObserverForName:AVAudioSessionInterruptionNotification
                            object:session
                             queue:nil
                        usingBlock:^(NSNotification *note) {
                    NSNumber *type = note.userInfo[AVAudioSessionInterruptionTypeKey];
                    if (type.unsignedIntegerValue != AVAudioSessionInterruptionTypeEnded)
                        return;
                    [[AVAudioSession sharedInstance] setActive:YES error:nil];
                    pthread_mutex_lock(&s_lock);
                    if (!s_suspended)
                        start_units_locked();
                    pthread_mutex_unlock(&s_lock);
                }] retain];
            s_session_ready = 1;
        }
        if (![session setActive:YES error:&error]) {
            fprintf(stderr, "audio: AVAudioSession activation refused: %s\n",
                    error.localizedDescription.UTF8String);
            return -1;
        }
    }
    return 0;
}

void em_audio_ios_set_suspended(int suspended)
{
    pthread_mutex_lock(&s_lock);
    s_suspended = suspended != 0;
    if (s_suspended) {
        for (EmAudio *a = s_units; a; a = a->next)
            AudioOutputUnitStop(a->unit);
    } else {
        [[AVAudioSession sharedInstance] setActive:YES error:nil];
        start_units_locked();
    }
    pthread_mutex_unlock(&s_lock);
}

EmAudio *em_audio_create(int sample_rate, EmAudioCallback cb, void *user)
{
    if (sample_rate <= 0 || !cb)
        return NULL;
    (void)em_audio_ios_session_begin();

    EmAudio *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;
    a->cb     = cb;
    a->user   = user;
    a->silent = em_headless();

    AudioComponentDescription desc = {
        .componentType         = kAudioUnitType_Output,
        .componentSubType      = kAudioUnitSubType_RemoteIO,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp || AudioComponentInstanceNew(comp, &a->unit) != noErr) {
        free(a);
        return NULL;
    }

    /* What WE deliver on bus 0's input scope: float32, interleaved, 2
     * channels at the game's rate; RemoteIO converts to the hardware. */
    AudioStreamBasicDescription fmt = {
        .mSampleRate       = (Float64)sample_rate,
        .mFormatID         = kAudioFormatLinearPCM,
        .mFormatFlags      = kAudioFormatFlagIsFloat |
                             kAudioFormatFlagIsPacked,
        .mFramesPerPacket  = 1,
        .mChannelsPerFrame = 2,
        .mBitsPerChannel   = 32,
        .mBytesPerFrame    = 2 * sizeof(float),
        .mBytesPerPacket   = 2 * sizeof(float),
    };
    AURenderCallbackStruct rc = { em_audio_render, a };

    if (AudioUnitSetProperty(a->unit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &fmt,
                             sizeof fmt) != noErr ||
        AudioUnitSetProperty(a->unit, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0, &rc,
                             sizeof rc) != noErr ||
        AudioUnitInitialize(a->unit) != noErr) {
        AudioComponentInstanceDispose(a->unit);
        free(a);
        return NULL;
    }
    pthread_mutex_lock(&s_lock);
    /* While the game is parked the unit stays stopped; the resume starts it. */
    if (!s_suspended && AudioOutputUnitStart(a->unit) != noErr) {
        pthread_mutex_unlock(&s_lock);
        AudioUnitUninitialize(a->unit);
        AudioComponentInstanceDispose(a->unit);
        free(a);
        return NULL;
    }
    a->next = s_units;
    s_units = a;
    pthread_mutex_unlock(&s_lock);
    return a;
}

void em_audio_destroy(EmAudio *a)
{
    if (!a)
        return;
    pthread_mutex_lock(&s_lock);
    for (EmAudio **p = &s_units; *p; p = &(*p)->next)
        if (*p == a) {
            *p = a->next;
            break;
        }
    pthread_mutex_unlock(&s_lock);
    /* Stop + Uninitialize synchronize with the I/O thread: no callback is in
     * flight once they return, making it safe to dispose and free. */
    AudioOutputUnitStop(a->unit);
    AudioUnitUninitialize(a->unit);
    AudioComponentInstanceDispose(a->unit);
    free(a);
}

void em_audio_pause(EmAudio *a)
{
    if (!a)
        return;
    pthread_mutex_lock(&s_lock);
    a->paused = 1;
    AudioOutputUnitStop(a->unit);
    pthread_mutex_unlock(&s_lock);
}

void em_audio_resume(EmAudio *a)
{
    if (!a)
        return;
    pthread_mutex_lock(&s_lock);
    a->paused = 0;
    if (!s_suspended)
        AudioOutputUnitStart(a->unit);
    pthread_mutex_unlock(&s_lock);
}
