/* The movie decoder (em_movie.h) over the first level's two movies, headless
 * (make test-movie-field runs it where the assets exist; about 7 s): a
 * missing asset is reported; each movie has the original's picture count
 * (k_movies in em_frontend.c: 4,761 / 2,305, the fork's sends); every
 * picture comes out once, in display order (index 0, 1, ... with rising
 * timestamps), RGBA with a whole-row stride, and then none is left; the
 * PCM reads to its end as stereo PCM16 at 48 kHz, exactly the frames the
 * track's duration holds, in any read size. No clock is involved: the game
 * asks for a picture when its field clock shows it (em_movie_field.h).
 *   build/movie_test [E900.mov E001.mov]
 *   build/movie_test --pcm <movie> <frames> <out.raw>   the first <frames>
 *       PCM frames as read (interleaved PCM16 LE), for
 *       tools/test_replay_movie_audio.py */
#include "em_movie.h"
#import <Foundation/Foundation.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check_movie(const char *path, uint32_t pictures)
{
    EmMovie *movie = em_movie_open(path);
    EmMovieInfo info;
    if (!movie || em_movie_info(movie, &info) != 0) {
        fprintf(stderr, "FAIL %s: %s\n", path, movie ? em_movie_error(movie) : "allocation");
        em_movie_close(movie);
        return 1;
    }
    int bad = 0;
    if (info.pictures != pictures) {
        fprintf(stderr, "FAIL %s: %u pictures, the original's %u\n", path, info.pictures, pictures);
        bad = 1;
    }
    EmMovieFrame frame;
    uint64_t next = 0;
    double last = -1.0;
    unsigned width = 0, height = 0;
    int r;
    while (!bad && (r = em_movie_next_picture(movie, &frame)) == 1) {
        if (frame.picture_index != next || !(frame.pts_seconds > last) || !frame.pixels || !frame.width ||
            !frame.height || frame.stride != frame.width * 4 || (next && (frame.width != width ||
                                                                          frame.height != height))) {
            fprintf(stderr, "FAIL %s: picture %llu (expected %llu, pts %.6f after %.6f, %ux%u)\n", path,
                    (unsigned long long)frame.picture_index, (unsigned long long)next, frame.pts_seconds, last,
                    frame.width, frame.height);
            bad = 1;
        }
        width = frame.width;
        height = frame.height;
        last = frame.pts_seconds;
        ++next;
    }
    if (!bad && (r != 0 || next != pictures)) {
        fprintf(stderr, "FAIL %s: %llu pictures decoded (%s)\n", path, (unsigned long long)next,
                r < 0 ? em_movie_error(movie) : "then none left");
        bad = 1;
    }
    /* The PCM in uneven reads (one field's 800 / 801 frames, then larger). */
    int16_t pcm[2 * 4096];
    uint64_t frames = 0;
    long sizes[] = {800, 801, 4096, 1};
    for (unsigned k = 0; !bad; ++k) {
        const long want = sizes[k % 4];
        const long got = em_movie_read_audio(movie, pcm, want);
        if (got < 0) {
            fprintf(stderr, "FAIL %s: %s\n", path, em_movie_error(movie));
            bad = 1;
        }
        if (got <= 0) break;
        frames += (uint64_t)got;
        if (got < want && em_movie_read_audio(movie, pcm, 1) != 0) {
            fprintf(stderr, "FAIL %s: a short PCM read before the end\n", path);
            bad = 1;
        }
    }
    if (!bad && frames != info.audio_frames) {
        fprintf(stderr, "FAIL %s: %llu PCM frames, the track's %llu\n", path, (unsigned long long)frames,
                (unsigned long long)info.audio_frames);
        bad = 1;
    }
    if (!bad)
        printf("movie decode: %s: %llu pictures %ux%u in order (last pts %.3f), %llu PCM frames\n", path,
               (unsigned long long)next, width, height, last, (unsigned long long)frames);
    em_movie_close(movie);
    return bad;
}

static int dump_pcm(const char *path, long frames, const char *out)
{
    EmMovie *movie = em_movie_open(path);
    EmMovieInfo info;
    FILE *f = fopen(out, "wb");
    int bad = !movie || em_movie_info(movie, &info) != 0 || !f || frames < 0;
    int16_t pcm[2 * 4096];
    while (!bad && frames > 0) {
        const long got = em_movie_read_audio(movie, pcm, frames < 4096 ? frames : 4096);
        if (got <= 0) break;
        bad = fwrite(pcm, 4, (size_t)got, f) != (size_t)got;
        frames -= got;
    }
    if (bad || frames)
        fprintf(stderr, "FAIL pcm dump of %s: %s\n", path, movie ? em_movie_error(movie) : "allocation");
    if (f) fclose(f);
    em_movie_close(movie);
    return bad || frames;
}

int main(int argc, const char **argv)
{
    @autoreleasepool {
        if (argc == 5 && strcmp(argv[1], "--pcm") == 0)
            return dump_pcm(argv[2], atol(argv[3]), argv[4]);
        EmMovie *missing = em_movie_open("build/nonexistent-movie-test.mov");
        EmMovieFrame frame;
        EmMovieInfo info;
        if (!missing || em_movie_info(missing, &info) != -1 || em_movie_next_picture(missing, &frame) != -1 ||
            !em_movie_error(missing)[0]) {
            fprintf(stderr, "FAIL a missing movie was not reported\n");
            em_movie_close(missing);
            return 1;
        }
        em_movie_close(missing);
        const char *e900 = argc > 1 ? argv[1] : "assets/startup/intro.mov";
        const char *e001 = argc > 2 ? argv[2] : "assets/movies/e001.mov";
        int bad = check_movie(e900, 4761) | check_movie(e001, 2305);
        printf("movie decode: %s\n", bad ? "FAIL" : "PASS");
        return bad;
    }
}
