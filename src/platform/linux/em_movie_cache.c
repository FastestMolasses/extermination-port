/* EMMV v1: locally exported decoded pictures and original PCM (em_movie.h).
 * No clock, dropping, conversion or resampling. The game owns all pacing.
 * The cache sits beside the requested .mov as <path>.emmv. */
#include "em_movie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
struct EmMovie {
    FILE *video, *audio;
    EmMovieInfo info;
    uint32_t width,height,index;
    uint64_t audio_read;
    uint8_t *pixels;
    double last_pts;
    char error[1536];
};
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t u64(const uint8_t *p) { return u32(p)|(uint64_t)u32(p+4)<<32; }
static int fail(EmMovie *m,const char *s) {
    if (!m->error[0]) snprintf(m->error,sizeof m->error,"movie cache: %s",s);
    return -1;
}
EmMovie *em_movie_open(const char *path) {
    EmMovie *m=calloc(1,sizeof *m); if (!m) return NULL;
    m->last_pts=-1.0;
    char name[4096];
    if (!path || snprintf(name,sizeof name,"%s.emmv",path)>=(int)sizeof name) {
        fail(m,"invalid path"); return m;
    }
    m->video=fopen(name,"rb"); m->audio=fopen(name,"rb");
    uint8_t b[32];
    if (!m->video || !m->audio || fread(b,1,32,m->video)!=32) {
        fail(m,"missing or truncated .mov.emmv; run the local movie cache exporter"); return m;
    }
    m->info.pictures=u32(b+8); m->width=u32(b+12); m->height=u32(b+16); m->info.audio_frames=u64(b+24);
    if (memcmp(b,"EMMV",4) || u32(b+4)!=1 || u32(b+20) || !m->info.pictures ||
        !m->width || !m->height || m->width>8192 || m->height>8192 || !m->info.audio_frames) {
        fail(m,"invalid header"); return m;
    }
    uint64_t bytes=(uint64_t)m->width*m->height*4;
    uint64_t at=32+(8+bytes)*m->info.pictures;
    if (m->info.audio_frames > (UINT64_MAX-at)/4 || at+4*m->info.audio_frames>INT64_MAX ||
        fseeko(m->audio,0,SEEK_END) || (uint64_t)ftello(m->audio)!=at+4*m->info.audio_frames ||
        fseeko(m->audio,(off_t)at,SEEK_SET)) {
        fail(m,"invalid file length"); return m;
    }
    m->pixels=malloc((size_t)bytes); if (!m->pixels) fail(m,"allocation failed");
    return m;
}
int em_movie_info(const EmMovie *m,EmMovieInfo *info) {
    if (!m || !info || m->error[0]) return -1;
    *info=m->info; return 0;
}
int em_movie_next_picture(EmMovie *m,EmMovieFrame *f) {
    if (!m || !f || m->error[0]) return -1;
    if (m->index==m->info.pictures) return 0;
    uint8_t b[8]; size_t bytes=(size_t)m->width*m->height*4;
    if (fread(b,1,8,m->video)!=8 || fread(m->pixels,1,bytes,m->video)!=bytes) return fail(m,"truncated picture");
    uint64_t bits=u64(b); double pts; memcpy(&pts,&bits,8);
    if (!isfinite(pts) || pts<=m->last_pts) return fail(m,"invalid picture order");
    m->last_pts=pts;
    *f=(EmMovieFrame){m->pixels,m->width,m->height,m->width*4,pts,m->index++}; return 1;
}
long em_movie_read_audio(EmMovie *m,int16_t *out,long frames) {
    if (!m || !out || frames<0 || m->error[0]) return -1;
    uint64_t left=m->info.audio_frames-m->audio_read;
    if ((uint64_t)frames>left) frames=(long)left;
    if (fread(out,4,(size_t)frames,m->audio)!=(size_t)frames) return fail(m,"truncated PCM");
    m->audio_read+=(uint64_t)frames; return frames;
}
const char *em_movie_error(const EmMovie *m) { return m?m->error:"movie allocation failed"; }
void em_movie_close(EmMovie *m) {
    if (!m) return;
    if (m->video) fclose(m->video);
    if (m->audio) fclose(m->audio);
    free(m->pixels); free(m);
}
