#include "game/em_opening_control_test.h"
#include "em_input.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_opening_runtime.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This fixture drives real input events, never player/camera positions.
 * Movie/menu automation is the existing frontend test path. */
static struct {
    int active, failed, phase, moving_ticks, locked_ticks;
    float locked_start[3], move_start[3], max_locked_distance;
} test;

static void key(int down)
{
    EmEvent event={0};
    event.type=down ? EM_EVENT_KEY_DOWN : EM_EVENT_KEY_UP;
    event.key='w';
    em_input_handle_event(&event);
}

static void fail(const char *reason)
{
    fprintf(stderr,"newgame control test: FAIL frame=%d: %s\n",g.frame_no,reason);
    test.failed=1;
    key(0);
    em_frame_request_quit();
}

void em_opening_control_test_begin(void)
{
    memset(&test,0,sizeof test);
    const char *value=getenv("EM_STARTUP_TEST");
    test.active=value && strcmp(value,"newgame-control")==0;
}

void em_opening_control_test_before_frame(void)
{
    if (!test.active || test.failed) return;
    if (g.frame_no>3000) {fail("opening/control timeout");return;}
    if (test.phase==0) {
        if (!em_opening_runtime_busy()) {fail("opening did not start");return;}
        memcpy(test.locked_start,g.pos,sizeof test.locked_start);
        key(1);
        test.phase=1;
        fprintf(stderr,"newgame control test: holding W through automatic opening\n");
    } else if (test.phase==2 && em_frame_transition()->substate==0) {
        memcpy(test.move_start,g.pos,sizeof test.move_start);
        key(1); /* arrives at the next real frame input snapshot */
        test.phase=3;
        fprintf(stderr,"newgame control test: fade clear; moving for 30 input ticks\n");
    }
}

void em_opening_control_test_after_frame(void)
{
    if (!test.active || test.failed) return;
    for (unsigned axis=0;axis<3;++axis)
        if (!isfinite(g.pos[axis]) || !isfinite(g.cam.eye[axis]) ||
            !isfinite(g.cam.tgt[axis])) {fail("nonfinite player/camera");return;}
    const EmFrameInput *input=em_frame_input();
    if (test.phase==1) {
        if (em_opening_runtime_busy()) {
            /* Frame input is the original unsigned pad byte, not the
             * normalized float used by the platform's pad snapshot. */
            if (input->ly==0 && input->lx==0x80) {
                float dx=g.pos[0]-test.locked_start[0];
                float dz=g.pos[2]-test.locked_start[2];
                float distance=sqrtf(dx*dx+dz*dz);
                if (distance>test.max_locked_distance) test.max_locked_distance=distance;
                ++test.locked_ticks;
                if (distance>.001f) {fail("held movement escaped cinematic lock");return;}
            }
        } else {
            key(0);
            if (test.locked_ticks<300 || g.frame_selector!=0 ||
                g.opening_event_39!=0xFF || g.have_battery!=1 ||
                g.opening_key_item_zero!=1) {
                fprintf(stderr,"newgame control test: locked=%d selector=%u "
                        "event39=%u battery=%d key0=%u pad=(%u,%u)\n",
                        test.locked_ticks,g.frame_selector,g.opening_event_39,
                        g.have_battery,g.opening_key_item_zero,input->lx,input->ly);
                fail("opening completed without validated lock/story handoff");return;
            }
            test.phase=2;
        }
    } else if (test.phase==3 && input->ly==0 && input->lx==0x80) {
        if (em_opening_runtime_busy() || g.frame_selector || em_frame_transition()->substate) {
            fail("movement test started before control/fade handoff");return;
        }
        if (++test.moving_ticks!=30) return;
        key(0);
        float dx=g.pos[0]-test.move_start[0], dz=g.pos[2]-test.move_start[2];
        float distance=sqrtf(dx*dx+dz*dz);
        if (distance<.25f || distance>30.0f) {fail("30 input ticks produced invalid displacement");return;}
        float top[3]={g.pos[0],g.pos[1]+2.0f,g.pos[2]};
        float bottom[3]={g.pos[0],g.pos[1]-2.0f,g.pos[2]};
        EmCollHit ground;
        if (!em_collision_segment_query(&g.coll,top,bottom,EM_COLL_SET_GRID,0,&ground) ||
            !isfinite(ground.point[1]) || (ground.surf_class!=EM_SURF_FLOOR &&
                                         ground.surf_class!=EM_SURF_SLOPE)) {
            fail("no finite walkable original grid surface beneath player");return;
        }
        fprintf(stderr,"newgame control test: PASS locked_ticks=%d max_locked_motion=%.9g "
                "move_ticks=%d displacement=%.6f pos=(%.6f,%.6f,%.6f) ground=%.6f\n",
                test.locked_ticks,test.max_locked_distance,test.moving_ticks,distance,
                g.pos[0],g.pos[1],g.pos[2],ground.point[1]);
        if (g.capture_path) em_gfx_request_capture(em_frame_gfx(),g.capture_path);
        test.phase=4;
        em_frame_request_quit(); /* end_frame still services the queued capture */
    }
}

int em_opening_control_test_active(void) {return test.active;}
int em_opening_control_test_failed(void) {return test.failed;}
