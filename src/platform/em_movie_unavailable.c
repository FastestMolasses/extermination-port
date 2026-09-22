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
                 "%s: native movie playback is not implemented on this platform",
                 path ? path : "(null)");
    return movie;
}
int em_movie_update(EmMovie *movie, EmMovieFrame *frame)
{ (void)movie; (void)frame; return -1; }
int em_movie_finished(const EmMovie *movie)
{ (void)movie; return 0; }
const char *em_movie_error(const EmMovie *movie)
{ return movie ? movie->error : "movie allocation failed"; }
void em_movie_close(EmMovie *movie) { free(movie); }
