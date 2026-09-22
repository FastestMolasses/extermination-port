#include "game/em_area11_opening.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned char command[64];
    EmScriptCommandResult command_result;
    EmOpeningEvent events[16];
    unsigned event_count, executions;
} Fixture;

static unsigned char *resolve(void *context,uint32_t pc)
{
    return pc==0x100 ? ((Fixture *)context)->command : NULL;
}
static EmScriptCommandResult execute(void *context,EmScript *script,
                                     unsigned char command[64])
{
    Fixture *fixture=context;
    assert(command==fixture->command);
    ++fixture->executions;
    ++script->phase;
    return fixture->command_result;
}
static void notify(void *context,EmOpeningEvent event)
{
    Fixture *fixture=context;
    assert(fixture->event_count<16);
    fixture->events[fixture->event_count++]=event;
}
static EmScriptResult tick(EmArea11Opening *opening,Fixture *fixture,
                           int ready,unsigned char flag)
{
    return em_area11_opening_tick(opening,ready,flag,resolve,execute,notify,fixture);
}

int main(int argc,char **argv)
{
    EmArea11Opening opening;
    Fixture fixture={0};
    fixture.command[0]=7; fixture.command[3]=0x80; /* STOP after callback completes */
    em_area11_opening_init(&opening,0x100);
    assert(tick(&opening,&fixture,0,0)==EM_SCRIPT_YIELDED);
    assert(!fixture.event_count && !fixture.executions);
    assert(tick(&opening,&fixture,1,0)==EM_SCRIPT_YIELDED);
    assert(!fixture.event_count && opening.actor_state==1);
    assert(tick(&opening,&fixture,1,0)==EM_SCRIPT_YIELDED);
    assert(fixture.event_count==1 && fixture.events[0]==EM_OPENING_STOP_STREAM);
    assert(!fixture.executions); /* starting the script does not execute it */
    assert(tick(&opening,&fixture,1,0)==EM_SCRIPT_YIELDED);
    assert(fixture.executions==1 && fixture.event_count==1);
    fixture.command_result=EM_SCRIPT_UNSUPPORTED;
    assert(tick(&opening,&fixture,1,0)==EM_SCRIPT_FAULT);
    assert(opening.substate==1 && fixture.event_count==1);
    fixture.command_result=EM_SCRIPT_ADVANCE;
    assert(tick(&opening,&fixture,1,1)==EM_SCRIPT_FINISHED);
    const EmOpeningEvent expected[]={EM_OPENING_STOP_STREAM,EM_OPENING_STOP_CHILD_ACTORS,
        EM_OPENING_EVENT_B9_COMPLETE,EM_OPENING_ADD_KEY_ITEM_ZERO,
        EM_OPENING_RESUME_MUSIC,EM_OPENING_FADE_IN_FOUR};
    assert(fixture.event_count==sizeof expected/sizeof *expected);
    assert(!memcmp(fixture.events,expected,sizeof expected));
    assert(opening.substate==2);
    assert(tick(&opening,&fixture,1,0xff)==EM_SCRIPT_FINISHED);
    assert(fixture.event_count==6); /* terminal effects are exactly once */

    em_area11_opening_init(&opening,0x100); memset(&fixture,0,sizeof fixture);
    assert(tick(&opening,&fixture,1,0xff)==EM_SCRIPT_YIELDED);
    assert(tick(&opening,&fixture,1,0xff)==EM_SCRIPT_FINISHED);
    assert(!fixture.event_count && !fixture.executions);
    if (argc==2) {
        EmScriptImage image={0};
        assert(em_script_image_load(&image,argv[1]));
        assert(image.base==0x828F30 && image.entry==0x828FC0 && image.length==0x390);
        unsigned char *first=em_script_image_read(&image,image.entry,64);
        assert(first && em_script_u32(first,0)==7 && em_script_u32(first,8)==12);
        unsigned char *last=em_script_image_read(&image,0x829280,64);
        assert(last && em_script_u32(last,0)==0x80000007);
        assert(!em_script_image_read(&image,0x8292C0,1));
        assert(!em_script_image_read(&image,UINT32_MAX,64));
        em_script_image_free(&image);
        assert(!image.bytes);
    }
    puts("area11_opening_test: PASS");
    return 0;
}
