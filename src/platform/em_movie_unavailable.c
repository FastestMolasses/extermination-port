/* Explicit failure for platforms whose native media backend is pending. */
#include "em_movie.h"
#include <stdio.h>
#include <stdlib.h>

struct EmMovie { char error[1024]; };

EmMovie *em_movie_open(const char *path)
{
    EmMovie *movie = calloc(1, sizeof *movie);
    if (movie)
        snprintf(movie->error, sizeof movie->error,
                 "%s: native movie decoding is not implemented on this platform",
                 path ? path : "(null)");
    return movie;
}
int em_movie_info(const EmMovie *movie, EmMovieInfo *info)
{ (void)movie; (void)info; return -1; }
int em_movie_next_picture(EmMovie *movie, EmMovieFrame *frame)
{ (void)movie; (void)frame; return -1; }
long em_movie_read_audio(EmMovie *movie, int16_t *out, long frames)
{ (void)movie; (void)out; (void)frames; return -1; }
const char *em_movie_error(const EmMovie *movie)
{ return movie ? movie->error : "movie allocation failed"; }
void em_movie_close(EmMovie *movie) { free(movie); }
