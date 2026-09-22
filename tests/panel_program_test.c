#include "game/em_panel_program.h"
#include <assert.h>
#include <stdio.h>

static int frame_no,cameras,animations,battery_requests,message_wait;
static int sound3ef=-1,power_frame=-1,release_frame=-1;
static EmScriptCommandResult frame(void *p,EmScript *s,const unsigned char *r)
{
    (void)p;(void)s;
    if (em_script_u32(r,8)==4) release_frame=frame_no;
    return EM_SCRIPT_ADVANCE;
}
static int camera(void *p) {(void)p;cameras++;return 1;}
static int message(void *p,uint32_t token,uint32_t delay)
{(void)p;assert(token==0x80000018 && delay==0);message_wait=3;return 1;}
static int message_done(void *p) {(void)p;return --message_wait==0;}
static int message_failed(void *p) {(void)p;return -1;}
static int animation(void *p,uint16_t clip,float rate,float blend)
{(void)p;assert(clip==0x15C && rate==1 && blend==0);animations++;return 1;}
static int battery(void *p,EmPanel *owner,uint8_t request)
{(void)p;assert(request==0x82 && owner->status==1 && !owner->charged && !owner->armed);battery_requests++;return 1;}
static int sound(void *p,uint32_t cue)
{(void)p;if (cue==0x3EF) sound3ef=frame_no;else assert(cue==0x3EE && power_frame==frame_no);return 1;}
static int power(void *p,uint8_t mask)
{(void)p;assert(mask==128);power_frame=frame_no;return 1;}

int main(int argc,char **argv)
{
    assert(argc==2);
    EmPanel panel;em_panel_init(&panel,0);
    const EmPanelProgramHooks hooks={NULL,frame,camera,message,message_done,animation,battery,sound,power};
    EmPanelProgram program;
    assert(em_panel_program_load(&program,argv[1],&panel,&hooks)==0);
    assert(em_panel_program_start(&program,EM_PANEL_POWER_AFTER_MENU,0)==0);
    int done=0;
    for (frame_no=0;frame_no<129;frame_no++) {
        done=em_panel_program_tick(&program);assert(done>=0);
        assert(done==(frame_no==128));
    }
    assert(cameras==1 && animations==1 && sound3ef==14 && power_frame==127 && release_frame==128);
    panel.armed=4;
    assert(em_panel_program_start(&program,EM_PANEL_OPEN_BATTERY,0x80000018)==0);
    done=0;
    for (frame_no=0;frame_no<10 && !done;frame_no++) done=em_panel_program_tick(&program);
    assert(done==1 && battery_requests==1 && cameras==2 && !panel.charged);
    em_panel_program_free(&program);
    EmPanelProgramHooks missing=hooks;missing.camera_retarget=NULL;
    assert(em_panel_program_load(&program,argv[1],&panel,&missing)==0);
    assert(em_panel_program_start(&program,EM_PANEL_POWER_AFTER_MENU,0)==0);
    assert(em_panel_program_tick(&program)==-1 && program.failed);
    assert(em_panel_program_tick(&program)==-1 && !panel.charged);
    em_panel_program_free(&program);
    missing=hooks;missing.message_done=message_failed;
    assert(em_panel_program_load(&program,argv[1],&panel,&missing)==0);
    assert(em_panel_program_start(&program,EM_PANEL_OPEN_BATTERY,0x80000018)==0);
    done=0;
    for (frame_no=0;frame_no<8 && done==0;frame_no++)
        done=em_panel_program_tick(&program);
    assert(done==-1 && program.failed);
    assert(battery_requests==1); /* Failed polling must not run the callback. */
    em_panel_program_free(&program);
    puts("Original panel program asset, message wait, callback order and129-tick post-menu script: PASS");
}
