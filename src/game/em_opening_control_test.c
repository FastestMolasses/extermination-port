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
    int active, failed, phase, moving_ticks, locked_ticks, stop_ticks, reentry_ticks;
    float locked_start[3], move_start[3], max_locked_distance;
    float reentry_previous[3];
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
    if (test.phase >= 2 && test.phase != 4) {
        unsigned clip, flags;
        float remaining;
        int transition;
        if (!player_pose_source(&clip, &remaining, &flags, &transition)) {
            fail("original player source channels became unavailable");
            return;
        }
        if (getenv("EM_CONTROL_TRACE"))
            fprintf(stderr, "pose sample: frame=%d clip=%u remaining=%.9g transition=%d flags=%08x\n",
                    g.frame_no, clip, remaining, transition, flags);
    }
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
        ++test.moving_ticks;
        if (getenv("EM_CONTROL_TRACE")) {
            const EmCamera *c = &g.cam;
            fprintf(stderr, "control sample: tick=%d frame=%d speed=%.9g yaw=%.9g "
                    "mode=%u sub=%u tier=%d entry=%d pos=(%.9g,%.9g,%.9g) "
                    "eye=(%.9g,%.9g,%.9g) target=(%.9g,%.9g,%.9g) "
                    "forward=(%.9g,%.9g,%.9g) camyaw=%.9g hit=%u "
                    "clip=%d clip_time=%.9g rate=%.9g blend=%.9g\n",
                    test.moving_ticks,g.frame_no,g.loco_upt,g.yaw,
                    g.loco_mode,g.loco_substate,g.loco_tier,g.loco_entry_ticks,
                    g.pos[0],g.pos[1],g.pos[2],c->eye[0],c->eye[1],c->eye[2],
                    c->tgt[0],c->tgt[1],c->tgt[2],c->fwd[0],c->fwd[1],c->fwd[2],
                    c->yaw,c->hit,g.loco_clip,g.walk_t,g.loco_rate,g.loco_blend);
        }
        if (test.moving_ticks!=30) return;
        key(0);
        float dx=g.pos[0]-test.move_start[0], dz=g.pos[2]-test.move_start[2];
        float distance=sqrtf(dx*dx+dz*dz);
        /* Original immutable state04: exact30 raw[128,0] input frames
         * travel9.599849. The tolerance permits the remaining camera
         * rounding/evolution difference, but rejects the old16.1 ramp. */
        if (fabsf(distance-9.6f)>.01f) {
            fail("30 input ticks disagree with original first-control distance");return;
        }
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
        if (getenv("EM_CONTROL_STOP_TEST") || getenv("EM_CONTROL_REENTRY_TEST")) {
            test.phase=5;
            fprintf(stderr,"newgame control test: released W; validating original run-stop\n");
            return;
        }
        if (g.capture_path) em_gfx_request_capture(em_frame_gfx(),g.capture_path);
        test.phase=4;
        em_frame_request_quit(); /* end_frame still services the queued capture */
    } else if (test.phase==5) {
        ++test.stop_ticks;
        if (getenv("EM_CONTROL_TRACE"))
            fprintf(stderr,"stop sample: tick=%d speed=%.9g mode=%u phase=%u "
                    "blend=%u source=%u idle_timer=%d\n",test.stop_ticks,
                    g.loco_upt,g.loco_mode,g.loco_stop.phase,
                    g.loco_stop.blend_left,g.loco_stop.frame,g.idle_timer);
        if (getenv("EM_CONTROL_REENTRY_TEST") && test.stop_ticks==18) {
            memcpy(test.reentry_previous,g.pos,sizeof test.reentry_previous);
            key(1);
            test.phase=6;
            return;
        }
        if (test.stop_ticks<60) return;
        float dx=g.pos[0]-test.move_start[0], dz=g.pos[2]-test.move_start[2];
        float distance=sqrtf(dx*dx+dz*dz);
        if (fabsf(distance-18.65f)>.03f || g.loco_upt!=0 ||
            g.loco_mode!=0 || g.loco_stop.phase!=0) {
            fail("run-stop timeline or final displacement disagrees with original");return;
        }
        fprintf(stderr,"newgame stop test: PASS release_ticks=%d "
                "total_displacement=%.6f idle_timer=%d\n",
                test.stop_ticks,distance,g.idle_timer);
        if (g.capture_path) em_gfx_request_capture(em_frame_gfx(),g.capture_path);
        test.phase=4;
        em_frame_request_quit();
    } else if (test.phase==6 && input->ly==0 && input->lx==0x80) {
        static const float speeds[8]={.3f,.3f,.3f,.3f,.3f,.3f,.3625f,.425f};
        static const unsigned phases[8]={1,1,1,1,2,0,0,0};
        unsigned tick=(unsigned)test.reentry_ticks++;
        float dx=g.pos[0]-test.reentry_previous[0];
        float dz=g.pos[2]-test.reentry_previous[2];
        float distance=sqrtf(dx*dx+dz*dz);
        memcpy(test.reentry_previous,g.pos,sizeof test.reentry_previous);
        fprintf(stderr,"reentry sample: tick=%u speed=%.9g phase=%u blend=%u "
                "tier=%d clip=%d source=%.9g displacement=%.9g "
                "blocked=%02x pos=(%.9g,%.9g,%.9g) yaw=%.9g\n",
                tick+1,g.loco_upt,g.loco_reentry.phase,g.loco_reentry.blend_left,
                g.loco_tier,g.loco_clip,g.walk_t*60.0,distance,g.probe_block_mask,
                g.pos[0],g.pos[1],g.pos[2],g.yaw);
        if (g.loco_upt!=speeds[tick] || g.loco_reentry.phase!=phases[tick] ||
            g.loco_tier!=2 || g.loco_stop.phase ||
            (tick<6 && fabsf(distance-(tick==0?.6f:.3f))>.001f) ||
            (tick>=6 && (g.probe_block_mask!=2 || distance>=g.loco_upt-.02f)) ||
            (tick<5 && fabs(g.walk_t*60.0-27.0)>.00001)) {
            fail("run-stop interruption disagrees with original request/blend/ramp");return;
        }
        if (test.reentry_ticks==8) {
            key(0);
            fprintf(stderr,"newgame reentry test: PASS doubled request motion, "
                    "four-tick blend, scalar re-arm and original panel contact\n");
            if (g.capture_path) em_gfx_request_capture(em_frame_gfx(),g.capture_path);
            test.phase=4;
            em_frame_request_quit();
        }
    }
}

int em_opening_control_test_active(void) {return test.active;}
int em_opening_control_test_failed(void) {return test.failed;}
