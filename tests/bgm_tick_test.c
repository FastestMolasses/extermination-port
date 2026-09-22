#include "game/em_bgm.h"
#include "em_audio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static EmAudioCallback render;
static void *callback_user;
EmAudio *em_audio_create(int rate,EmAudioCallback cb,void *user)
{ assert(rate==48000);render=cb;callback_user=user;return (EmAudio *)&render; }
void em_audio_destroy(EmAudio *audio) { (void)audio;render=NULL; }
void em_sfx_mix(float *out,int frames,int rate) {(void)out;(void)frames;(void)rate;}
void em_startup_audio_mix(float *out,int frames,int rate) {(void)out;(void)frames;(void)rate;}
void em_opening_media_mix(float *out,int frames,int rate) {(void)out;(void)frames;(void)rate;}
static void put32(FILE *f,unsigned n)
{for(unsigned i=0;i<4;i++)assert(fputc((n>>(8*i))&255,f)!=EOF);}
static void put16(FILE *f,unsigned n)
{assert(fputc(n&255,f)!=EOF);assert(fputc((n>>8)&255,f)!=EOF);}
static float sample(void)
{float out[2]={0};assert(render);render(callback_user,out,1);assert(out[1]==out[0]);return out[0];}
int main(void)
{
    char path[128];snprintf(path,sizeof path,"/tmp/em-bgm-ticks-%ld.wav",(long)getpid());
    FILE *f=fopen(path,"wb");assert(f);
    fwrite("RIFF",1,4,f);put32(f,40);fwrite("WAVEfmt ",1,8,f);put32(f,16);
    put16(f,1);put16(f,2);put32(f,48000);put32(f,192000);put16(f,4);put16(f,16);
    fwrite("data",1,4,f);put32(f,4);put16(f,16384);put16(f,16384);assert(!fclose(f));
    assert(!em_bgm_play_ticks(path,1,4));assert(sample()==0);
    for(int i=1;i<=5;i++) {
        em_bgm_service();
        float expected=(i<4?(int)(16383.0f*i/4)/16383.0f:1)*.5f;
        assert(fabsf(sample()-expected)<1e-7f);
        assert(fabsf(sample()-expected)<1e-7f); /* callback count cannot advance volume */
    }
    assert(!em_bgm_play_ticks(path,1,0));assert(sample()==0);
    em_bgm_service();assert(sample()==.5f); /* zero duration clamps in one service */
    em_bgm_stop(0);assert(sample()==0);em_bgm_service();
    assert(!em_bgm_play_ticks(path,1,280));
    for(int i=0;i<280;i++) {em_bgm_service();(void)sample();}
    assert(fabsf(sample()-.5f)<2e-5f); /* float add/clamp follows original units */
    em_bgm_shutdown();assert(!render);assert(!unlink(path));
    puts("BGM tick fade PASS: service cadence, integer volume, loop, hard restart/stop, original280-tick range");
}
