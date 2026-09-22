/* macOS movie playback through Apple's AVFoundation/CoreVideo frameworks.
 * No third-party decoder. The player's audio clock schedules video output;
 * MPEG B-picture reordering, PCM playback, and device timing remain native.
 *
 * The PS2 pump (func_002036E0 -> func_00206CC0) drains the MPEG decoder before
 * stopping its audio stream. PSS PCM contains trailing padding. Preserve that
 * PCM in the asset but end presentation at the video track's endpoint.
 */
#include "em_movie.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct EmMovie {
    AVPlayer *player;
    AVPlayerItem *item;
    AVPlayerItemVideoOutput *output;
    uint8_t *pixels;
    size_t capacity;
    char path[1024];
    char error[1536];
    int configured;
    int finished;
    double frame_seconds;
    CMTime end_time;
};

static void movie_fail(EmMovie *movie, const char *reason)
{
    snprintf(movie->error, sizeof movie->error, "%s: %s", movie->path, reason);
    [movie->player pause];
}

EmMovie *em_movie_open(const char *path)
{
    EmMovie *movie = calloc(1, sizeof *movie);
    if (!movie)
        return NULL;
    snprintf(movie->path, sizeof movie->path, "%s", path ? path : "(null)");
    @autoreleasepool {
        if (!path || ![[NSFileManager defaultManager] isReadableFileAtPath:
                [NSString stringWithUTF8String:path]]) {
            movie_fail(movie, "movie asset is missing or unreadable");
            return movie;
        }
        NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        movie->item = [[AVPlayerItem alloc] initWithURL:url];
        NSDictionary *settings = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
        };
        movie->output = [[AVPlayerItemVideoOutput alloc]
                            initWithPixelBufferAttributes:settings];
        [movie->item addOutput:movie->output];
        movie->player = [[AVPlayer alloc] initWithPlayerItem:movie->item];
        movie->player.actionAtItemEnd = AVPlayerActionAtItemEndPause;
        /* Do not start until update has installed the video endpoint. */
    }
    return movie;
}

int em_movie_update(EmMovie *movie, EmMovieFrame *frame)
{
    if (!movie || !frame)
        return -1;
    if (movie->error[0])
        return -1;
    if (movie->finished)
        return 0;
    @autoreleasepool {
        if (movie->item.status == AVPlayerItemStatusFailed ||
            movie->player.status == AVPlayerStatusFailed) {
            NSError *error = movie->item.error ?: movie->player.error;
            movie_fail(movie, error ? error.description.UTF8String : "native decoder failed");
            return -1;
        }
        if (movie->item.status != AVPlayerItemStatusReadyToPlay)
            return 0;
        if (!movie->configured) {
            /* Track inspection occurs only after the local asset is ready. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            AVAssetTrack *video = [[movie->item.asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
            CMTimeRange range = video.timeRange;
            movie->frame_seconds = CMTimeGetSeconds(video.minFrameDuration);
#pragma clang diagnostic pop
            if (!video || !CMTIMERANGE_IS_VALID(range) ||
                !isfinite(CMTimeGetSeconds(range.duration)) ||
                !isfinite(movie->frame_seconds) || movie->frame_seconds <= 0) {
                movie_fail(movie, "movie has no finite video track");
                return -1;
            }
            movie->item.forwardPlaybackEndTime = CMTimeRangeGetEnd(range);
            movie->end_time = movie->item.forwardPlaybackEndTime;
            movie->configured = 1;
            [movie->player play];
        }
        /* Poll the native playback clock instead of retaining a callback
         * into the C object; closing/skipping then has no queued observer
         * that could outlive this allocation. */
        if (CMTimeCompare(movie->player.currentTime, movie->end_time) >= 0) {
            movie->finished = 1;
            return 0;
        }
        CMTime when = [movie->output itemTimeForHostTime:CACurrentMediaTime()];
        if (![movie->output hasNewPixelBufferForItemTime:when])
            return 0;
        CMTime shown = kCMTimeInvalid;
        CVPixelBufferRef buffer = [movie->output copyPixelBufferForItemTime:when itemTimeForDisplay:&shown];
        if (!buffer)
            return 0;
        size_t width = CVPixelBufferGetWidth(buffer);
        size_t height = CVPixelBufferGetHeight(buffer);
        if (!width || !height || width > 8192 || height > 8192 ||
            CVPixelBufferGetPixelFormatType(buffer) != kCVPixelFormatType_32BGRA) {
            CVPixelBufferRelease(buffer);
            movie_fail(movie, "unexpected native movie pixel format or dimensions");
            return -1;
        }
        size_t needed = width * height * 4;
        if (needed > movie->capacity) {
            uint8_t *grown = realloc(movie->pixels, needed);
            if (!grown) {
                CVPixelBufferRelease(buffer);
                movie_fail(movie, "cannot allocate decoded movie frame");
                return -1;
            }
            movie->pixels = grown;
            movie->capacity = needed;
        }
        CVReturn result = CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
        if (result != kCVReturnSuccess) {
            CVPixelBufferRelease(buffer);
            movie_fail(movie, "cannot access decoded movie frame");
            return -1;
        }
        const uint8_t *source = CVPixelBufferGetBaseAddress(buffer);
        size_t stride = CVPixelBufferGetBytesPerRow(buffer);
        double seconds = CMTimeGetSeconds(shown);
        if (!source || stride < width * 4 || !isfinite(seconds) || seconds < 0) {
            CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
            CVPixelBufferRelease(buffer);
            movie_fail(movie, "invalid decoded frame storage or presentation timestamp");
            return -1;
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
        CVPixelBufferRelease(buffer);
        frame->pixels = movie->pixels;
        frame->width = (unsigned)width;
        frame->height = (unsigned)height;
        frame->stride = (unsigned)(width * 4);
        frame->pts_seconds = seconds;
        frame->picture_index = (uint64_t)llround(frame->pts_seconds / movie->frame_seconds);
        return 1;
    }
}

int em_movie_finished(const EmMovie *movie)
{
    return movie && movie->finished;
}

const char *em_movie_error(const EmMovie *movie)
{
    return movie ? movie->error : "movie allocation failed";
}

void em_movie_close(EmMovie *movie)
{
    if (!movie)
        return;
    @autoreleasepool {
        [movie->player pause];
        [movie->item removeOutput:movie->output];
        [movie->player replaceCurrentItemWithPlayerItem:nil];
        [movie->output release];
        [movie->item release];
        [movie->player release];
    }
    free(movie->pixels);
    free(movie);
}
