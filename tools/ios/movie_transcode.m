/* movie_transcode.m — build-time helper of the iOS app (docs/IOS.md).
 *
 * The port's movies are lossless remuxes of the user's PSS streams: MPEG-2
 * video + PCM audio in a QuickTime file (tools/export_movie.py). macOS
 * decodes MPEG-2; iOS has no MPEG-2 decoder (the player plays the audio and
 * delivers no picture). tools/ios/build.sh runs this tool on the Mac, over
 * the user's own local exports, and puts the result only in the app bundle
 * (and an ignored cache under build/ios/):
 *
 *   movie_transcode IN.mov OUT.mov
 *       the video track decoded to its YUV 4:2:0 samples and re-encoded as
 *       HEVC at a high bitrate (1.5x the source's), keeping every sample's
 *       timestamps and duration, the track's time scale and the decoder's
 *       colour tags; the audio track copied sample for sample (no
 *       re-encode). Same path and name in the bundle, so the game's movie
 *       table is unchanged.
 *   movie_transcode --compare A.mov B.mov
 *       decodes both to BGRA and reports frame count, timestamp mismatches
 *       and the per-channel error (mean, max, PSNR) of B against A.
 *
 * Apple system frameworks only (AVFoundation, CoreMedia, CoreVideo).
 * Builds with: clang -fobjc-arc -framework AVFoundation -framework CoreMedia
 *              -framework CoreVideo tools/ios/movie_transcode.m
 */
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#include <math.h>
#include <stdio.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static int fail(NSString *what, NSError *error)
{
    fprintf(stderr, "movie_transcode: %s%s%s\n", what.UTF8String, error ? ": " : "",
            error ? error.localizedDescription.UTF8String : "");
    return 1;
}

static AVAssetTrack *first_track(AVAsset *asset, AVMediaType type)
{
    return [[asset tracksWithMediaType:type] firstObject];
}

/* The decoder's colour tags on a decoded picture, as writer colour settings
 * (nil when the picture carries none: the encoder then tags nothing). */
static NSDictionary *color_properties(CMSampleBufferRef sample)
{
    CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample);
    if (!image) return nil;
    CFTypeRef primaries = CVBufferCopyAttachment(image, kCVImageBufferColorPrimariesKey, NULL);
    CFTypeRef transfer = CVBufferCopyAttachment(image, kCVImageBufferTransferFunctionKey, NULL);
    CFTypeRef matrix = CVBufferCopyAttachment(image, kCVImageBufferYCbCrMatrixKey, NULL);
    NSDictionary *props = nil;
    if (primaries && transfer && matrix)
        props = @{AVVideoColorPrimariesKey: (__bridge NSString *)primaries,
                  AVVideoTransferFunctionKey: (__bridge NSString *)transfer,
                  AVVideoYCbCrMatrixKey: (__bridge NSString *)matrix};
    fprintf(stderr, "movie_transcode: colour tags primaries=%s transfer=%s matrix=%s\n",
            primaries ? [(__bridge NSString *)primaries UTF8String] : "-",
            transfer ? [(__bridge NSString *)transfer UTF8String] : "-",
            matrix ? [(__bridge NSString *)matrix UTF8String] : "-");
    if (primaries) CFRelease(primaries);
    if (transfer) CFRelease(transfer);
    if (matrix) CFRelease(matrix);
    return props;
}

static int transcode(NSString *inPath, NSString *outPath)
{
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:inPath] options:nil];
    AVAssetTrack *video = first_track(asset, AVMediaTypeVideo);
    AVAssetTrack *audio = first_track(asset, AVMediaTypeAudio);
    if (!video) return fail([NSString stringWithFormat:@"%@ has no video track", inPath], nil);

    NSError *error = nil;
    AVAssetReader *reader = [AVAssetReader assetReaderWithAsset:asset error:&error];
    if (!reader) return fail(@"cannot read the movie", error);
    AVAssetReaderTrackOutput *videoOut = [AVAssetReaderTrackOutput
        assetReaderTrackOutputWithTrack:video
                         outputSettings:@{(id)kCVPixelBufferPixelFormatTypeKey:
                                              @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)}];
    videoOut.alwaysCopiesSampleData = NO;
    [reader addOutput:videoOut];
    AVAssetReaderTrackOutput *audioOut = nil;
    if (audio) {
        audioOut = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:audio outputSettings:nil];
        audioOut.alwaysCopiesSampleData = NO;
        [reader addOutput:audioOut];
    }
    if (![reader startReading]) return fail(@"cannot start reading", reader.error);

    /* The first picture gives the colour tags the writer must carry. */
    CMSampleBufferRef first = [videoOut copyNextSampleBuffer];
    if (!first) return fail(@"the video track has no picture", reader.error);

    [[NSFileManager defaultManager] removeItemAtPath:outPath error:nil];
    AVAssetWriter *writer = [AVAssetWriter assetWriterWithURL:[NSURL fileURLWithPath:outPath]
                                                     fileType:AVFileTypeQuickTimeMovie
                                                        error:&error];
    if (!writer) { CFRelease(first); return fail(@"cannot write the output", error); }
    const CGSize size = video.naturalSize;
    const double bitrate = fmax(1.5 * video.estimatedDataRate, 4.0e6);
    NSMutableDictionary *settings = [@{
        AVVideoCodecKey: AVVideoCodecTypeHEVC,
        AVVideoWidthKey: @(size.width),
        AVVideoHeightKey: @(size.height),
        AVVideoCompressionPropertiesKey: @{
            AVVideoAverageBitRateKey: @(bitrate),
            AVVideoExpectedSourceFrameRateKey: @(video.nominalFrameRate),
        },
    } mutableCopy];
    NSDictionary *color = color_properties(first);
    if (color) settings[AVVideoColorPropertiesKey] = color;
    AVAssetWriterInput *videoIn = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                                     outputSettings:settings];
    videoIn.mediaTimeScale = video.naturalTimeScale;
    videoIn.expectsMediaDataInRealTime = NO;
    [writer addInput:videoIn];
    AVAssetWriterInput *audioIn = nil;
    if (audio) {
        CMFormatDescriptionRef hint = (__bridge CMFormatDescriptionRef)audio.formatDescriptions.firstObject;
        audioIn = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio
                                                     outputSettings:nil
                                                   sourceFormatHint:hint];
        audioIn.expectsMediaDataInRealTime = NO;
        [writer addInput:audioIn];
    }
    if (![writer startWriting]) { CFRelease(first); return fail(@"cannot start writing", writer.error); }
    [writer startSessionAtSourceTime:kCMTimeZero];
    fprintf(stderr, "movie_transcode: %s: %.0fx%.0f, %.3f fps, HEVC %.1f Mbit/s, audio %s\n",
            inPath.lastPathComponent.UTF8String, size.width, size.height, video.nominalFrameRate,
            bitrate / 1e6, audio ? "copied" : "none");

    dispatch_group_t group = dispatch_group_create();
    __block CMSampleBufferRef pending = first;
    __block long pictures = 0, packets = 0;
    __block int broken = 0;
    dispatch_group_enter(group);
    [videoIn requestMediaDataWhenReadyOnQueue:dispatch_queue_create("video", NULL) usingBlock:^{
        while (videoIn.readyForMoreMediaData) {
            CMSampleBufferRef sample = pending ? pending : [videoOut copyNextSampleBuffer];
            pending = NULL;
            if (!sample) {
                [videoIn markAsFinished];
                dispatch_group_leave(group);
                return;
            }
            if (![videoIn appendSampleBuffer:sample]) broken = 1;
            ++pictures;
            CFRelease(sample);
        }
    }];
    if (audioIn) {
        dispatch_group_enter(group);
        [audioIn requestMediaDataWhenReadyOnQueue:dispatch_queue_create("audio", NULL) usingBlock:^{
            while (audioIn.readyForMoreMediaData) {
                CMSampleBufferRef sample = [audioOut copyNextSampleBuffer];
                if (!sample) {
                    [audioIn markAsFinished];
                    dispatch_group_leave(group);
                    return;
                }
                if (![audioIn appendSampleBuffer:sample]) broken = 1;
                ++packets;
                CFRelease(sample);
            }
        }];
    }
    dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
    if (reader.status == AVAssetReaderStatusFailed) return fail(@"reading failed", reader.error);
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(done); }];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    if (broken || writer.status != AVAssetWriterStatusCompleted)
        return fail(@"writing failed", writer.error);
    fprintf(stderr, "movie_transcode: wrote %s (%ld pictures, %ld audio buffers)\n",
            outPath.UTF8String, pictures, packets);
    return 0;
}

/* --compare: both movies decoded to BGRA, picture by picture. */
static int compare(NSString *aPath, NSString *bPath)
{
    AVAssetReaderTrackOutput *outs[2];
    AVAssetReader *readers[2];
    NSString *paths[2] = {aPath, bPath};
    for (int i = 0; i < 2; ++i) {
        AVURLAsset *asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:paths[i]] options:nil];
        AVAssetTrack *video = first_track(asset, AVMediaTypeVideo);
        NSError *error = nil;
        readers[i] = video ? [AVAssetReader assetReaderWithAsset:asset error:&error] : nil;
        if (!readers[i]) return fail([NSString stringWithFormat:@"cannot read %@", paths[i]], error);
        outs[i] = [AVAssetReaderTrackOutput
            assetReaderTrackOutputWithTrack:video
                             outputSettings:@{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)}];
        [readers[i] addOutput:outs[i]];
        if (![readers[i] startReading]) return fail(@"cannot start reading", readers[i].error);
    }
    long frames = 0, pts_mismatch = 0, count_mismatch = 0;
    double sum_abs = 0.0, sum_sq = 0.0, samples = 0.0;
    int max_abs = 0;
    for (;;) {
        @autoreleasepool {
            CMSampleBufferRef a = [outs[0] copyNextSampleBuffer];
            CMSampleBufferRef b = [outs[1] copyNextSampleBuffer];
            if (!a || !b) {
                if (a || b) count_mismatch = 1;
                if (a) CFRelease(a);
                if (b) CFRelease(b);
                break;
            }
            if (CMTimeCompare(CMSampleBufferGetPresentationTimeStamp(a),
                              CMSampleBufferGetPresentationTimeStamp(b)) != 0)
                ++pts_mismatch;
            CVPixelBufferRef pa = CMSampleBufferGetImageBuffer(a), pb = CMSampleBufferGetImageBuffer(b);
            CVPixelBufferLockBaseAddress(pa, kCVPixelBufferLock_ReadOnly);
            CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
            const size_t w = CVPixelBufferGetWidth(pa), h = CVPixelBufferGetHeight(pa);
            if (w == CVPixelBufferGetWidth(pb) && h == CVPixelBufferGetHeight(pb)) {
                const uint8_t *ra = CVPixelBufferGetBaseAddress(pa), *rb = CVPixelBufferGetBaseAddress(pb);
                const size_t sa = CVPixelBufferGetBytesPerRow(pa), sb = CVPixelBufferGetBytesPerRow(pb);
                for (size_t y = 0; y < h; ++y)
                    for (size_t x = 0; x < w; ++x)
                        for (int c = 0; c < 3; ++c) {
                            const int d = abs((int)ra[y * sa + 4 * x + c] - (int)rb[y * sb + 4 * x + c]);
                            sum_abs += d;
                            sum_sq += (double)d * d;
                            if (d > max_abs) max_abs = d;
                            samples += 1.0;
                        }
            } else {
                count_mismatch = 1;
            }
            CVPixelBufferUnlockBaseAddress(pa, kCVPixelBufferLock_ReadOnly);
            CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
            CFRelease(a);
            CFRelease(b);
            ++frames;
        }
    }
    const double mse = samples > 0 ? sum_sq / samples : 0.0;
    printf("compare: %ld pictures, %ld timestamp mismatches, %s; mean abs %.3f, max abs %d, PSNR %.2f dB\n",
           frames, pts_mismatch, count_mismatch ? "COUNT/SIZE MISMATCH" : "same count and size",
           samples > 0 ? sum_abs / samples : 0.0, max_abs, mse > 0 ? 10.0 * log10(255.0 * 255.0 / mse) : INFINITY);
    return pts_mismatch || count_mismatch ? 1 : 0;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc == 4 && strcmp(argv[1], "--compare") == 0)
            return compare(@(argv[2]), @(argv[3]));
        if (argc == 3)
            return transcode(@(argv[1]), @(argv[2]));
        fprintf(stderr, "usage: movie_transcode IN.mov OUT.mov | --compare A.mov B.mov\n");
        return 2;
    }
}
