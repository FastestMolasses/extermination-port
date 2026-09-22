/* Native movie playback. Local assets are lossless remuxes of the user's
 * PSS streams; the OS decodes MPEG-2 and synchronizes their PCM audio.
 * All calls belong to the main/game thread. No platform objects escape. */
#ifndef EM_MOVIE_H
#define EM_MOVIE_H

#include <stdint.h>

typedef struct EmMovie EmMovie;

typedef struct {
    /* RGBA8, top-down rows. Borrowed until the next update or close. */
    const uint8_t *pixels;
    unsigned width;
    unsigned height;
    unsigned stride;
    double pts_seconds;
    /* Display-picture index derived from the source track's frame period,
     * so skip gates do not depend on the game's 60-Hz render cadence. */
    uint64_t picture_index;
} EmMovieFrame;

/* Starts asynchronously. NULL means allocation failed. File/decode errors
 * are reported by update/error, including the input path. */
EmMovie *em_movie_open(const char *path);

/* 1 = new frame, 0 = no new frame, -1 = failed. The player owns the audio
 * clock, so callers must not advance playback using their frame counter. */
int em_movie_update(EmMovie *movie, EmMovieFrame *frame);
int em_movie_finished(const EmMovie *movie);
const char *em_movie_error(const EmMovie *movie);
void em_movie_close(EmMovie *movie);

#endif
