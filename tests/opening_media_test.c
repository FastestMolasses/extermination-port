/* The AREA11 opening's fade track (em_opening_media): the exported
 * opening.emfx, sampled on the opening camera's clock from the stream
 * request (restart) to the teardown (stop). The opening's stream itself is
 * the stream lanes' (test-opening-runtime, test-stream-lanes,
 * test-iop-stream). */
#include "game/em_opening_media.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fade_count, fade_dir, fade_speed, fade_color;
void em_frame_fade_start_colour(int dir,int speed,uint8_t color)
{ fade_count++;fade_dir=dir;fade_speed=speed;fade_color=color; }

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

static void fade_track_test(void)
{
    char dir[128],path[256];snprintf(dir,sizeof dir,"/tmp/em-opening-media-%ld",(long)getpid());
    assert(!mkdir(dir,0700));
    fixture(dir,1);assert(em_opening_media_prepare(dir)==-1);
    fixture(dir,0);assert(!em_opening_media_prepare(dir));
    /* Not armed before the stream request. */
    em_opening_media_camera_tick(100);assert(!fade_count);
    em_opening_media_restart();
    em_opening_media_camera_tick(0);em_opening_media_camera_tick(6.5f);
    assert(!fade_count);em_opening_media_camera_tick(7);
    assert(fade_count==1 && fade_dir==1 && fade_speed==4 && fade_color==0);
    em_opening_media_camera_tick(100);assert(fade_count==1);
    /* A new request re-arms the track at its first value; the teardown
     * disarms it. */
    em_opening_media_restart();em_opening_media_camera_tick(0);em_opening_media_camera_tick(7);
    assert(fade_count==2);
    em_opening_media_restart();em_opening_media_stop();
    em_opening_media_camera_tick(0);em_opening_media_camera_tick(7);assert(fade_count==2);
    em_opening_media_shutdown();
    em_opening_media_restart();em_opening_media_camera_tick(0);em_opening_media_camera_tick(7);
    assert(fade_count==2); /* not prepared: never armed */
    assert(!em_opening_media_prepare(dir));em_opening_media_shutdown();
    snprintf(path,sizeof path,"%s/opening.emfx",dir);assert(!unlink(path));assert(!rmdir(dir));
}
int main(void)
{ fade_track_test();puts("opening media PASS: fade track arm/disarm, first-value restart, malformed track"); }
