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
void em_hud_subtitle(EmGfx *g,const char *str,float y,float height,float skew,
                     uint32_t color,uint32_t outline)
{ (void)g;(void)str;(void)y;(void)height;(void)skew;(void)color;(void)outline; }

static void clock_test(void)
{
    const EmOpeningLine lines[]={
        {.line=10,.duration=0,.text="skip"},
        {.line=11,.duration=2,.text="A"},
        {.line=12,.duration=1,.speaker=1,.text=""},
        {.line=13,.duration=0,.speaker=255,.terminal=1,.text=""}};
    EmOpeningDialogue d;
    em_opening_dialogue_start(&d,lines,4);
    assert(!em_opening_dialogue_line(&d));
    const int expected[]={11,11,11,12,12,13,-1};
    const unsigned talk[]={1,1,0,2,0,0,0};
    for (unsigned i=0;i<sizeof(expected)/sizeof(expected[0]);i++) {
        em_opening_dialogue_tick(&d);
        const EmOpeningLine *line=em_opening_dialogue_line(&d);
        assert((line ? line->line : -1)==expected[i]);
        assert(em_opening_dialogue_talk_mask(&d)==talk[i]);
    }
    assert(!d.active);
    em_opening_dialogue_start(&d,NULL,0);
    em_opening_dialogue_tick(&d);
    assert(!em_opening_dialogue_line(&d));
}

static void put32(FILE *f,unsigned n)
{ for (unsigned i=0;i<4;i++) assert(fputc((n>>(i*8))&255,f)!=EOF); }
static void put16(FILE *f,unsigned n)
{ assert(fputc(n&255,f)!=EOF);assert(fputc((n>>8)&255,f)!=EOF); }
static void fixture(const char *dir,int invalid)
{
    char path[256];snprintf(path,sizeof path,"%s/opening.emod",dir);
    FILE *f=fopen(path,"wb");assert(f);
    assert(fwrite("EMOD",1,4,f)==4);
    const unsigned h[]={1,2,24,0x606060,0x100505,388,4};
    for(unsigned i=0;i<7;i++) put32(f,h[i]);
    for(unsigned i=0;i<2;i++) {
        put16(f,100+i);put16(f,i?0:20);put16(f,65535);
        fputc(255,f);fputc(i,f);put32(f,invalid?999:i*2);put32(f,1);
        put32(f,0);
    }
    assert(fwrite("A\0B\0",1,4,f)==4);assert(!fclose(f));
    snprintf(path,sizeof path,"%s/opening.emfx",dir);f=fopen(path,"wb");assert(f);
    assert(fwrite("EMFX",1,4,f)==4);put32(f,1);put32(f,4);
    const float track[]={-1,7,4,0};
    assert(fwrite(track,sizeof(float),4,f)==4);assert(!fclose(f));
}

static void runtime_test(void)
{
    char dir[128],path[256];snprintf(dir,sizeof dir,"/tmp/em-opening-media-%ld",(long)getpid());
    assert(!mkdir(dir,0700));
    fixture(dir,1);assert(em_opening_media_prepare(dir)==-1);
    assert(em_opening_media_audio_start()==-1);
    fixture(dir,0);assert(!em_opening_media_prepare(dir));
    assert(!em_opening_media_audio_start());assert(em_opening_media_audio_ready());
    float pcm[8]={0};em_opening_media_mix(pcm,4,48000);
    assert(pcm[0]==0); /* prefill does not start the stream */
    assert(!em_opening_media_dialogue_start());em_opening_media_mix(pcm,4,48000);
    assert(pcm[0]==0); /* start is released at ordinary audio-service */
    em_opening_media_tick();em_opening_media_mix(pcm,4,48000);
    assert(pcm[0]==.5f && pcm[1]==-.5f);
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
    em_opening_media_stop();assert(!em_opening_media_audio_ready());
    assert(em_opening_media_dialogue_start()==-1);
    assert(!em_opening_media_audio_start());assert(!em_opening_media_dialogue_start());
    em_opening_media_tick();memset(pcm,0,sizeof pcm);em_opening_media_mix(pcm,1,48000);
    assert(pcm[0]==.5f); /* restart begins at full level/sample zero */
    assert(!em_opening_media_resume_music(280));
    em_opening_media_shutdown();
    memset(pcm,0,sizeof pcm);em_opening_media_mix(pcm,4,48000);assert(pcm[0]==0);
    assert(!em_opening_media_prepare(dir));em_opening_media_shutdown();
    snprintf(path,sizeof path,"%s/opening.emod",dir);assert(!unlink(path));
    snprintf(path,sizeof path,"%s/opening.emfx",dir);assert(!unlink(path));assert(!rmdir(dir));
}
int main(void)
{ clock_test();runtime_test();puts("opening media PASS: original duration boundaries, prefill gate, skip volume, fade track, malformed assets, restart"); }
