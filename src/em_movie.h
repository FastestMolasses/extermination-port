/* Native movie decoding. Local assets are lossless remuxes of the user's
 * PSS streams (tools/export_movie.py; on iOS HEVC copies with every
 * picture's timestamp and the PCM kept, docs/IOS.md). The OS decodes the
 * pictures; nothing here plays anything on a clock of its own: the game
 * asks for the next picture when its field clock shows it and for the next
 * PCM frames when its field sounds (em_movie_field.h, em_movie_sound.h), so
 * a movie takes the same steps paced, uncapped or headless. All calls
 * belong to the game thread. No platform objects escape. */
#ifndef EM_MOVIE_H
#define EM_MOVIE_H

#include <stdint.h>

typedef struct EmMovie EmMovie;

typedef struct {
    /* RGBA8, top-down rows. Borrowed until the next picture or close. */
    const uint8_t *pixels;
    unsigned width;
    unsigned height;
    unsigned stride;
    double pts_seconds;
    /* The picture's place in display order, from 0. */
    uint64_t picture_index;
} EmMovieFrame;

typedef struct {
    uint32_t pictures;      /* the video track's pictures (its duration over its frame period) */
    uint64_t audio_frames;  /* the PCM track's stereo frames at 48000 Hz */
} EmMovieInfo;

/* Opens the asset and starts decoding both tracks. NULL means allocation
 * failed; file and decoder errors are reported by em_movie_error (and by
 * every later call returning -1), with the input path. */
EmMovie *em_movie_open(const char *path);
/* 0, or -1 if the movie failed. */
int em_movie_info(const EmMovie *movie, EmMovieInfo *info);
/* Decodes the next picture in display order (blocking): 1 = a picture,
 * 0 = none left, -1 = failed. */
int em_movie_next_picture(EmMovie *movie, EmMovieFrame *frame);
/* The next `frames` PCM frames (interleaved stereo PCM16, 48000 Hz) in
 * order: the frames read (fewer only at the track's end), or -1. */
long em_movie_read_audio(EmMovie *movie, int16_t *out, long frames);
const char *em_movie_error(const EmMovie *movie);
void em_movie_close(EmMovie *movie);

#endif
