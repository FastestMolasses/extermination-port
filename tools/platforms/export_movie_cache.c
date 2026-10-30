/* Local-only lossless decoded cache, using the port's native movie decoder.
 * Link on macOS with src/platform/mac/em_movie_mac.m. Nothing is distributed
 * except this source; the movie, pixels and PCM stay in ignored assets/. */
#include "em_movie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static int put32(FILE *f, uint32_t v) {
    uint8_t b[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)};
    return fwrite(b,1,4,f)==4;
}
static int put64(FILE *f, uint64_t v) { return put32(f,(uint32_t)v) && put32(f,(uint32_t)(v>>32)); }
int main(int argc, char **argv) {
    if (argc!=3) { fprintf(stderr,"usage: export_movie_cache movie.mov output.emmv\n"); return 2; }
    EmMovie *m=em_movie_open(argv[1]); EmMovieInfo info;
    if (!m || em_movie_info(m,&info)) { fprintf(stderr,"%s\n",em_movie_error(m)); em_movie_close(m); return 1; }
    FILE *f=fopen(argv[2],"wb"); if (!f) { em_movie_close(m); return 1; }
    EmMovieFrame frame; uint32_t n=0,w=0,h=0; int rc,ok=1;
    /* 32 bytes: magic, version, picture count, width, height, reserved,
     * audio frames. Each picture: binary64 PTS then tight RGBA8 rows.
     * After the last picture: stereo signed little-endian PCM16/48000. */
    while ((rc=em_movie_next_picture(m,&frame))==1) {
        if (!n) {
            w=frame.width; h=frame.height;
            ok=fwrite("EMMV",1,4,f)==4 && put32(f,1) && put32(f,info.pictures) &&
                put32(f,w) && put32(f,h) && put32(f,0) && put64(f,info.audio_frames);
        }
        if (!ok || frame.width!=w || frame.height!=h || frame.picture_index!=n) { ok=0; break; }
        uint64_t bits; memcpy(&bits,&frame.pts_seconds,8);
        if (!put64(f,bits)) { ok=0; break; }
        for (uint32_t y=0;y<h;y++)
            if (fwrite(frame.pixels+(size_t)y*frame.stride,4,w,f)!=w) ok=0;
        n++;
    }
    if (rc<0 || n!=info.pictures || !n) ok=0;
    int16_t pcm[8192]; uint64_t samples=0; long got;
    while (ok && (got=em_movie_read_audio(m,pcm,4096))>0) {
        /* Supported build host is little endian, as are both target ISAs. */
        ok=fwrite(pcm,4,(size_t)got,f)==(size_t)got; samples+=(uint64_t)got;
    }
    if (samples!=info.audio_frames) ok=0;
    if (fclose(f)) ok=0;
    em_movie_close(m);
    fprintf(stderr,"movie cache: %s; %u pictures, %llu PCM frames\n",ok?"PASS":"FAIL",n,(unsigned long long)samples);
    return ok?0:1;
}
