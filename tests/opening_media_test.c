#include "game/em_opening_media.h"
#include "game/em_bgm.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fade_count, fade_dir, fade_speed, fade_color;
int em_bgm_device_ensure(int rate) { return rate == 48000 ? 0 : -1; }
void em_bgm_stop(int fade) { assert(fade == 0); }
int em_bgm_play_ticks(const char *path,int loop,unsigned ticks)
{ assert(strstr(path,"opening_resume.wav"));assert(loop==1 && ticks==280);return 0; }
void em_sfx_stop_all(void) {}
int em_bgm_wav_read(const char *path, EmBgmWav *wav, const char *tag)
{
    (void)tag;
    assert(strstr(path,"opening.wav"));
    *wav=(EmBgmWav){malloc(64*sizeof(int16_t)),32,2,48000};
    assert(wav->pcm);
    for (int i=0;i<64;i++) wav->pcm[i]=i%2 ? -16384 : 16384;
    return 0;
}
void em_frame_fade_start_colour(int dir,int speed,uint8_t color)
{ fade_count++;fade_dir=dir;fade_speed=speed;fade_color=color; }
/* The step-H lane hook em_opening_media registers with em_bgm. */
static void (*lane)(void *);
static void *lane_context;
void em_bgm_set_lane_service(void (*service)(void *),void *context)
{ lane=service;lane_context=context; }
static void step_h(void) { if (lane) lane(lane_context); }

static void put32(FILE *f,unsigned n)
{ for (unsigned i=0;i<4;i++) assert(fputc((n>>(i*8))&255,f)!=EOF); }
static void fixture(const char *dir,int invalid)
{
    char path[256];snprintf(path,sizeof path,"%s/opening.emfx",dir);
    FILE *f=fopen(path,"wb");assert(f);
    assert(fwrite("EMFX",1,4,f)==4);put32(f,1);put32(f,4);
    /* An invalid track ends without its 0 terminator. */
    const float track[]={-1,7,4,invalid?5.0f:0.0f};
    assert(fwrite(track,sizeof(float),4,f)==4);assert(!fclose(f));
}

static void runtime_test(void)
{
    char dir[128],path[256];snprintf(dir,sizeof dir,"/tmp/em-opening-media-%ld",(long)getpid());
    assert(!mkdir(dir,0700));
    fixture(dir,1);assert(em_opening_media_prepare(dir)==-1);
    assert(em_opening_media_audio_start()==-1);
    fixture(dir,0);assert(!em_opening_media_prepare(dir));
    /* The lane stand-in follows D_008106F4: 001FD4C0 stores 2 before
     * 001FA790 arms the lane; step H turns it into the prefill hold 1 and
     * starts nothing; the message service's release (0) starts the sound
     * at the next step H. */
    uint8_t hold=2;em_opening_media_set_hold(&hold);assert(lane);
    step_h();assert(hold==2);  /* not armed: the lane is idle */
    assert(!em_opening_media_audio_start());
    float pcm[8]={0};em_opening_media_mix(pcm,4,48000);
    assert(pcm[0]==0);
    step_h();assert(hold==1);em_opening_media_mix(pcm,4,48000);
    assert(pcm[0]==0); /* prefill does not start the stream */
    step_h();assert(hold==1);em_opening_media_mix(pcm,4,48000);
    assert(pcm[0]==0);
    hold=0;em_opening_media_mix(pcm,4,48000);
    assert(pcm[0]==0); /* the release starts at step H */
    step_h();em_opening_media_mix(pcm,4,48000);
    assert(pcm[0]==.5f && pcm[1]==-.5f);
    hold=2;step_h();assert(hold==2); /* a playing lane ignores the byte */
    em_opening_media_camera_tick(0);em_opening_media_camera_tick(6.5f);
    assert(!fade_count);em_opening_media_camera_tick(7);
    assert(fade_count==1 && fade_dir==1 && fade_speed==4 && fade_color==0);
    em_opening_media_camera_tick(100);assert(fade_count==1);
    em_opening_media_fade_out(4);
    for(int tick=1;tick<=4;tick++) {
        em_opening_media_tick();memset(pcm,0,sizeof pcm);
        em_opening_media_mix(pcm,1,48000);
        float expect=(int)(16383.0f*(1.0f-tick/4.0f))/16383.0f*.5f;
        assert(fabsf(pcm[0]-expect)<1e-7f);
    }
    em_opening_media_stop();
    hold=0;step_h();memset(pcm,0,sizeof pcm);em_opening_media_mix(pcm,1,48000);
    assert(pcm[0]==0); /* stopped: a released hold starts nothing */
    assert(!em_opening_media_audio_start());
    hold=0;step_h();memset(pcm,0,sizeof pcm);em_opening_media_mix(pcm,1,48000);
    assert(pcm[0]==.5f); /* restart begins at full level/sample zero */
    assert(!em_opening_media_resume_music(280));
    em_opening_media_shutdown();
    memset(pcm,0,sizeof pcm);em_opening_media_mix(pcm,4,48000);assert(pcm[0]==0);
    assert(!em_opening_media_prepare(dir));em_opening_media_shutdown();
    em_opening_media_set_hold(NULL);assert(!lane);
    snprintf(path,sizeof path,"%s/opening.emfx",dir);assert(!unlink(path));assert(!rmdir(dir));
}
int main(void)
{ runtime_test();puts("opening media PASS: lane hold protocol, prefill gate, skip volume, fade track, malformed assets, restart"); }
