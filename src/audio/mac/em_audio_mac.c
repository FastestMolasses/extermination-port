/* em_audio_mac.c — macOS audio output backend on AudioToolbox/AudioUnit.
 *
 * Clean-room, no third-party libraries; only the AudioToolbox system
 * framework (plain C API — no Objective-C needed here). The default-output
 * AudioUnit (kAudioUnitSubType_DefaultOutput) pulls frames from our render
 * proc on Core Audio's real-time I/O thread, which is exactly the pull model
 * em_audio.h exposes, so this layer is a thin adapter:
 *
 *   - The unit's INPUT scope (bus 0) is configured to 32-bit float,
 *     interleaved stereo at the requested rate; Core Audio's built-in
 *     converter resamples to whatever the hardware actually runs at, so the
 *     game can always mix at the PS2-native 48000 Hz.
 *   - Because the format is interleaved, the render proc receives a single
 *     AudioBuffer holding L,R pairs — the exact layout the EmAudioCallback
 *     contract promises, so the callback writes straight into it.
 *   - On any malformed render request the buffers are zero-filled so the
 *     speaker never receives garbage.
 *
 * Teardown: AudioOutputUnitStop + AudioUnitUninitialize synchronize with the
 * I/O thread, so after em_audio_destroy returns the callback can no longer be
 * in flight (the contract em_audio.h documents).
 */
#include "em_audio.h"

#include <AudioToolbox/AudioToolbox.h>
#include <stdlib.h>
#include <string.h>

struct EmAudio {
    AudioUnit       unit;
    EmAudioCallback cb;
    void           *user;
};

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

    /* Interleaved float32 stereo -> exactly one buffer with both channels. If
     * the request ever doesn't match that shape (or the buffer is too small
     * for the frame count), emit silence rather than garbage. */
    if (ioData->mNumberBuffers == 1 && ioData->mBuffers[0].mData &&
        ioData->mBuffers[0].mDataByteSize >=
            inNumberFrames * 2 * sizeof(float)) {
        a->cb(a->user, (float *)ioData->mBuffers[0].mData,
              (int)inNumberFrames);
        return noErr;
    }

    for (UInt32 i = 0; i < ioData->mNumberBuffers; i++)
        if (ioData->mBuffers[i].mData)
            memset(ioData->mBuffers[i].mData, 0,
                   ioData->mBuffers[i].mDataByteSize);
    return noErr;
}

EmAudio *em_audio_create(int sample_rate, EmAudioCallback cb, void *user)
{
    if (sample_rate <= 0 || !cb)
        return NULL;

    EmAudio *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;
    a->cb   = cb;
    a->user = user;

    AudioComponentDescription desc = {
        .componentType         = kAudioUnitType_Output,
        .componentSubType      = kAudioUnitSubType_DefaultOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp || AudioComponentInstanceNew(comp, &a->unit) != noErr) {
        free(a);
        return NULL;
    }

    /* What WE deliver on the unit's input scope: float32, interleaved
     * (packed, no NonInterleaved flag), 2 channels. The output unit converts
     * from this to the device's native format/rate internally. */
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
    if (AudioOutputUnitStart(a->unit) != noErr) {
        AudioUnitUninitialize(a->unit);
        AudioComponentInstanceDispose(a->unit);
        free(a);
        return NULL;
    }
    return a;
}

void em_audio_destroy(EmAudio *a)
{
    if (!a)
        return;
    /* Stop + Uninitialize synchronize with the I/O thread: no callback is in
     * flight once they return, making it safe to dispose and free. */
    AudioOutputUnitStop(a->unit);
    AudioUnitUninitialize(a->unit);
    AudioComponentInstanceDispose(a->unit);
    free(a);
}

void em_audio_pause(EmAudio *a)
{
    if (a)
        AudioOutputUnitStop(a->unit);
}

void em_audio_resume(EmAudio *a)
{
    if (a)
        AudioOutputUnitStart(a->unit);
}
