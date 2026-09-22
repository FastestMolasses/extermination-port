/* Native playback smoke/full-run check. Runs its own main-thread event loop
 * so AVPlayer can deliver status/video/end notifications without a window.
 * Example: build/movie_test assets/startup/intro.mov --full
 * The native decoder/audio services require an unsandboxed test execution. */
#include "em_movie.h"
#import <Foundation/Foundation.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(int argc, const char **argv)
{
    @autoreleasepool {
        const char *path = argc > 1 ? argv[1] : "assets/startup/intro.mov";
        int full = argc > 2 && strcmp(argv[2], "--full") == 0;
        EmMovie *missing = em_movie_open("build/nonexistent-movie-test.mov");
        EmMovieFrame frame = {0};
        if (em_movie_update(missing, &frame) != -1 || !em_movie_error(missing)[0]) {
            fprintf(stderr, "FAIL missing movie was not reported\n");
            em_movie_close(missing);
            return 1;
        }
        em_movie_close(missing);
        EmMovie *movie = em_movie_open(path);
        double start = [NSDate timeIntervalSinceReferenceDate];
        double previous = -1;
        uint64_t previous_picture = 0;
        unsigned frames = 0;
        int verdict = 0;
        while ([NSDate timeIntervalSinceReferenceDate] - start < (full ? 190 : 6)) {
            int result = em_movie_update(movie, &frame);
            if (result < 0) {
                fprintf(stderr, "FAIL %s\n", em_movie_error(movie));
                goto done;
            }
            if (result > 0) {
                if (!frame.pixels || !frame.width || !frame.height ||
                    frame.stride != frame.width * 4 ||
                    !isfinite(frame.pts_seconds) || frame.pts_seconds < previous ||
                    frame.picture_index < previous_picture) {
                    fprintf(stderr, "FAIL invalid frame or reversed timestamp\n");
                    goto done;
                }
                previous = frame.pts_seconds;
                previous_picture = frame.picture_index;
                ++frames;
            }
            if (em_movie_finished(movie)) {
                verdict = frames > 0;
                break;
            }
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
        }
        if (!full)
            verdict = frames >= 30 && previous > 1;
        fprintf(stderr, "%s movie %s frames=%u last_pts=%.6f picture=%llu finished=%d\n",
                verdict ? "PASS" : "FAIL", full ? "full-playthrough" : "smoke",
                frames, previous, (unsigned long long)previous_picture, em_movie_finished(movie));
    done:
        em_movie_close(movie);
        return verdict ? 0 : 1;
    }
}
