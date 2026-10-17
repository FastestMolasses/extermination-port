/* macOS (and iOS) movie decoding through Apple's AVFoundation/CoreMedia
 * frameworks (AVAssetReader). No third-party decoder and no player: the
 * pictures come out one by one in display order when the game's field
 * clock shows them, the PCM frames when its fields sound (em_movie.h).
 * The asset's PCM keeps the PSS's trailing padding; the game's code decides
 * how much of it sounds (em_movie_field.h). */
#include "em_movie.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct EmMovie {
    AVURLAsset *asset;
    AVAssetReader *video_reader, *audio_reader;
    AVAssetReaderTrackOutput *video_out, *audio_out;
    EmMovieInfo info;
    uint8_t *pixels;
    size_t capacity;
    uint64_t pictures_out;
    double last_pts;
    /* the PCM sample buffer being read and how many of its bytes are used */
    CMSampleBufferRef audio_buffer;
    size_t audio_offset, audio_length;
    int audio_done;
    char path[1024];
    char error[1536];
};

static int movie_fail(EmMovie *movie, const char *reason)
{
    if (!movie->error[0]) snprintf(movie->error, sizeof movie->error, "%s: %s", movie->path, reason);
    return -1;
}

EmMovie *em_movie_open(const char *path)
{
    EmMovie *movie = calloc(1, sizeof *movie);
    if (!movie) return NULL;
    movie->last_pts = -1.0;
    snprintf(movie->path, sizeof movie->path, "%s", path ? path : "(null)");
    @autoreleasepool {
        if (!path || ![[NSFileManager defaultManager] isReadableFileAtPath:[NSString stringWithUTF8String:path]]) {
            movie_fail(movie, "movie asset is missing or unreadable");
            return movie;
        }
        NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        movie->asset = [[AVURLAsset alloc] initWithURL:url
                                               options:@{AVURLAssetPreferPreciseDurationAndTimingKey: @YES}];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        /* The synchronous track accessors load the local asset's tracks. */
        AVAssetTrack *video = [[movie->asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        AVAssetTrack *audio = [[movie->asset tracksWithMediaType:AVMediaTypeAudio] firstObject];
        const double period = video ? CMTimeGetSeconds(video.minFrameDuration) : 0.0;
        const double vlen = video ? CMTimeGetSeconds(video.timeRange.duration) : 0.0;
        const double alen = audio ? CMTimeGetSeconds(audio.timeRange.duration) : 0.0;
        NSArray *formats = audio ? audio.formatDescriptions : nil;
#pragma clang diagnostic pop
        if (!video || !isfinite(period) || period <= 0 || !isfinite(vlen) || vlen <= 0) {
            movie_fail(movie, "movie has no finite video track");
            return movie;
        }
        const AudioStreamBasicDescription *asbd =
            formats.count ? CMAudioFormatDescriptionGetStreamBasicDescription(
                                (CMAudioFormatDescriptionRef)formats.firstObject)
                          : NULL;
        /* The PSS's PCM is stereo PCM16 at 48 kHz; read anything else and
         * the reader would convert it (not the original's samples). */
        if (!asbd || asbd->mFormatID != kAudioFormatLinearPCM || asbd->mSampleRate != 48000.0 ||
            asbd->mChannelsPerFrame != 2 || asbd->mBitsPerChannel != 16 || !isfinite(alen) || alen <= 0) {
            movie_fail(movie, "movie has no stereo PCM16 48 kHz track");
            return movie;
        }
        movie->info.pictures = (uint32_t)llround(vlen / period);
        movie->info.audio_frames = (uint64_t)llround(alen * 48000.0);
        NSError *error = nil;
        movie->video_reader = [[AVAssetReader alloc] initWithAsset:movie->asset error:&error];
        movie->audio_reader = movie->video_reader ? [[AVAssetReader alloc] initWithAsset:movie->asset error:&error]
                                                  : nil;
        if (!movie->video_reader || !movie->audio_reader) {
            movie_fail(movie, error ? error.description.UTF8String : "cannot create the asset readers");
            return movie;
        }
        movie->video_out = [[AVAssetReaderTrackOutput alloc]
            initWithTrack:video
           outputSettings:@{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)}];
        movie->audio_out = [[AVAssetReaderTrackOutput alloc]
            initWithTrack:audio
           outputSettings:@{AVFormatIDKey: @(kAudioFormatLinearPCM), AVLinearPCMBitDepthKey: @16,
                            AVLinearPCMIsFloatKey: @NO, AVLinearPCMIsBigEndianKey: @NO,
                            AVLinearPCMIsNonInterleaved: @NO, AVSampleRateKey: @48000,
                            AVNumberOfChannelsKey: @2}];
        movie->video_out.alwaysCopiesSampleData = NO;
        movie->audio_out.alwaysCopiesSampleData = NO;
        if (![movie->video_reader canAddOutput:movie->video_out] ||
            ![movie->audio_reader canAddOutput:movie->audio_out]) {
            movie_fail(movie, "the asset readers refuse the track outputs");
            return movie;
        }
        [movie->video_reader addOutput:movie->video_out];
        [movie->audio_reader addOutput:movie->audio_out];
        if (![movie->video_reader startReading] || ![movie->audio_reader startReading]) {
            NSError *e = movie->video_reader.error ?: movie->audio_reader.error;
            movie_fail(movie, e ? e.description.UTF8String : "the native decoder cannot start");
            return movie;
        }
    }
    return movie;
}

int em_movie_info(const EmMovie *movie, EmMovieInfo *info)
{
    if (!movie || !info || movie->error[0]) return -1;
    *info = movie->info;
    return 0;
}

int em_movie_next_picture(EmMovie *movie, EmMovieFrame *frame)
{
    if (!movie || !frame) return -1;
    if (movie->error[0]) return -1;
    @autoreleasepool {
        for (;;) {
            CMSampleBufferRef sample = [movie->video_out copyNextSampleBuffer];
            if (!sample) {
                if (movie->video_reader.status == AVAssetReaderStatusFailed) {
                    NSError *e = movie->video_reader.error;
                    return movie_fail(movie, e ? e.description.UTF8String : "native decoder failed");
                }
                return 0;
            }
            CVImageBufferRef buffer = CMSampleBufferGetImageBuffer(sample);
            if (!buffer) {      /* a sample without a picture (none in the movies so far) */
                CFRelease(sample);
                continue;
            }
            const double seconds = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample));
            const size_t width = CVPixelBufferGetWidth(buffer);
            const size_t height = CVPixelBufferGetHeight(buffer);
            if (!width || !height || width > 8192 || height > 8192 ||
                CVPixelBufferGetPixelFormatType(buffer) != kCVPixelFormatType_32BGRA || !isfinite(seconds) ||
                seconds <= movie->last_pts) {
                CFRelease(sample);
                return movie_fail(movie, "unexpected decoded picture (format, size or display order)");
            }
            const size_t needed = width * height * 4;
            if (needed > movie->capacity) {
                uint8_t *grown = realloc(movie->pixels, needed);
                if (!grown) {
                    CFRelease(sample);
                    return movie_fail(movie, "cannot allocate the decoded picture");
                }
                movie->pixels = grown;
                movie->capacity = needed;
            }
            if (CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
                CFRelease(sample);
                return movie_fail(movie, "cannot access the decoded picture");
            }
            const uint8_t *source = CVPixelBufferGetBaseAddress(buffer);
            const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
            if (!source || stride < width * 4) {
                CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
                CFRelease(sample);
                return movie_fail(movie, "invalid decoded picture storage");
            }
            for (size_t y = 0; y < height; ++y) {
                const uint8_t *src = source + y * stride;
                uint8_t *dst = movie->pixels + y * width * 4;
                for (size_t x = 0; x < width; ++x) {
                    dst[x * 4] = src[x * 4 + 2];
                    dst[x * 4 + 1] = src[x * 4 + 1];
                    dst[x * 4 + 2] = src[x * 4];
                    dst[x * 4 + 3] = src[x * 4 + 3];
                }
            }
            CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
            CFRelease(sample);
            movie->last_pts = seconds;
            frame->pixels = movie->pixels;
            frame->width = (unsigned)width;
            frame->height = (unsigned)height;
            frame->stride = (unsigned)(width * 4);
            frame->pts_seconds = seconds;
            frame->picture_index = movie->pictures_out++;
            return 1;
        }
    }
}

long em_movie_read_audio(EmMovie *movie, int16_t *out, long frames)
{
    if (!movie || !out || frames < 0) return -1;
    if (movie->error[0]) return -1;
    long done = 0;
    @autoreleasepool {
        while (done < frames) {
            if (!movie->audio_buffer || movie->audio_offset >= movie->audio_length) {
                if (movie->audio_buffer) {
                    CFRelease(movie->audio_buffer);
                    movie->audio_buffer = NULL;
                }
                if (movie->audio_done) break;
                movie->audio_buffer = [movie->audio_out copyNextSampleBuffer];
                if (!movie->audio_buffer) {
                    if (movie->audio_reader.status == AVAssetReaderStatusFailed) {
                        NSError *e = movie->audio_reader.error;
                        return movie_fail(movie, e ? e.description.UTF8String : "native PCM reader failed");
                    }
                    movie->audio_done = 1;
                    break;
                }
                CMBlockBufferRef block = CMSampleBufferGetDataBuffer(movie->audio_buffer);
                movie->audio_offset = 0;
                movie->audio_length = block ? CMBlockBufferGetDataLength(block) : 0;
                if (movie->audio_length % 4) return movie_fail(movie, "a PCM buffer of partial frames");
                continue;
            }
            CMBlockBufferRef block = CMSampleBufferGetDataBuffer(movie->audio_buffer);
            size_t bytes = (size_t)(frames - done) * 4;
            if (bytes > movie->audio_length - movie->audio_offset) bytes = movie->audio_length - movie->audio_offset;
            if (CMBlockBufferCopyDataBytes(block, movie->audio_offset, bytes, out + 2 * done) != kCMBlockBufferNoErr)
                return movie_fail(movie, "cannot read the PCM buffer");
            movie->audio_offset += bytes;
            done += (long)(bytes / 4);
        }
    }
    return done;
}

const char *em_movie_error(const EmMovie *movie)
{
    return movie ? movie->error : "movie allocation failed";
}

void em_movie_close(EmMovie *movie)
{
    if (!movie) return;
    @autoreleasepool {
        if (movie->audio_buffer) CFRelease(movie->audio_buffer);
        [movie->video_reader cancelReading];
        [movie->audio_reader cancelReading];
        [movie->video_out release];
        [movie->audio_out release];
        [movie->video_reader release];
        [movie->audio_reader release];
        [movie->asset release];
    }
    free(movie->pixels);
    free(movie);
}
