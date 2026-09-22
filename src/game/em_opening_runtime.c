#include "game/em_opening_runtime.h"

#include "game/em_area11_opening.h"
#include "em_gamepad.h"
#include "game/em_random.h"
#include "game/em_bgm.h"
#include "game/em_camera.h"
#include "game/em_cinematic_camera.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_opening_actor.h"
#include "game/em_opening_media.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* 0x00828FC0 is the entry of the original overlay actor's program.
 * This is deliberately a small host for that program, not an invented
 * substitute timeline or a general handler that silently skips commands. */
static struct {
    EmArea11Opening controller;
    EmScriptImage image;
    EmCinematicCamera camera;
    int requested, ready, finished, failed;
    int actors_active, camera_active, camera_owned;
    uint32_t half_tick;
    float camera_time;
    uint8_t player_phase, cinematic_ready;
} s;

static void fail(const char *service)
{
    if (!s.failed)
        fprintf(stderr,"opening: required %s unavailable at script %#010x; "
                "New Game cannot continue\n",service,s.controller.script.pc);
    s.failed=1;
    em_frame_request_quit();
}

void em_opening_runtime_request(void)
{
    if (s.ready) {
        em_opening_media_stop();
        em_area11_opening_init(&s.controller,s.image.entry);
    }
    s.requested=1;
    s.finished=0;
    s.failed=0;
    s.actors_active=s.camera_active=s.camera_owned=0;
    s.half_tick=0;
    s.camera_time=0;
    s.player_phase=s.cinematic_ready=0;
}

void em_opening_runtime_scene_ready(void)
{
    if (!s.requested || s.ready || s.failed) return;
    char path[1024];
    snprintf(path,sizeof path,"%s/opening.emsc",g.scene_dir);
    if (!em_script_image_load(&s.image,path) || s.image.entry!=0x00828FC0) {
        fail("original AREA11 opening script"); return;
    }
    snprintf(path,sizeof path,"%s/opening_camera.emcc",g.scene_dir);
    if (em_cinematic_camera_load(&s.camera,path)!=0) {
        fail("original bank 0x98 camera track"); return;
    }
    if (!em_opening_actor_init(em_frame_gfx(),g.scene_dir)) {
        fail("original player/Roger/equipment meshes and animation"); return;
    }
    if (em_opening_media_prepare(g.scene_dir)!=0) {
        fail("original opening dialogue/audio"); return;
    }
    em_area11_opening_init(&s.controller,s.image.entry);
    s.ready=1;
    fprintf(stderr,"opening: original AREA11 actor 0x00823E80 ready\n");
}

static unsigned char *resolve(void *context, uint32_t address)
{
    (void)context;
    return em_script_image_read(&s.image,address,EM_SCRIPT_RECORD_SIZE);
}

static void record_vector(float dst[3], const unsigned char *record,
                          unsigned offset)
{
    for (unsigned axis=0;axis<3;++axis)
        dst[axis]=em_script_f32(record,offset+4*axis);
}

static void camera_restore(void)
{
    s.camera_active=0;
    g.cam.zoom=480.0f;
    g.cam.up[0]=0;
    g.cam.up[1]=-1;
    g.cam.up[2]=0;
}

static EmScriptCommandResult execute(void *context, EmScript *script,
                                    unsigned char *record)
{
    (void)context;
    unsigned opcode=em_script_u32(record,0)&0xFFF;
    unsigned sub=em_script_u32(record,8);
    switch (opcode) {
    case 7: /* 001B82D0: enter with stream handshake / leave. */
        if (sub==12) {
            switch (script->phase) {
            case 0:
                if (s.cinematic_ready) return EM_SCRIPT_ADVANCE;
                if (em_script_u32(record,0x18)!=0x66)
                    return EM_SCRIPT_UNSUPPORTED;
                g.frame_selector=2;
                g.cam.top_mode=1;
                em_frame_fade_start(1,4);
                if (em_opening_media_audio_start()!=0)
                    return EM_SCRIPT_UNSUPPORTED;
                script->phase=1;
                return EM_SCRIPT_WAIT;
            case 1:
                if (em_frame_transition()->substate==2) {
                    em_frame_screen_fade_start(1,255);
                    ++script->phase;
                }
                return EM_SCRIPT_WAIT;
            case 2:
                if (em_script_u32(record,0x14)!=0 || s.player_phase!=0) {
                    /* 001B81D0 binds the player's cinematic skeleton.
                     * The original palette asset has already passed load. */
                    s.player_phase=2;
                    g.cam.zoom=em_camera_scope_zoom(0.0f);
                    ++script->phase;
                }
                return EM_SCRIPT_WAIT;
            case 3:
                if (!em_opening_media_audio_ready()) return EM_SCRIPT_WAIT;
                em_frame_fade_start(-1,16);
                s.cinematic_ready=1;
                script->skip_phase=1;
                script->skip_request=1;
                return EM_SCRIPT_ADVANCE;
            default: return EM_SCRIPT_UNSUPPORTED;
            }
        }
        if (sub==5) {
            if (em_script_u32(record,0x14)!=0x39)
                return EM_SCRIPT_UNSUPPORTED;
            g.opening_event_39=0xFF;
            camera_restore();
            em_frame_screen_fade_start(-1,4);
            em_opening_media_stop();
            s.actors_active=0;
            s.camera_owned=0;
            s.cinematic_ready=0;
            s.player_phase=1;
            int skipped=script->skip_request==2 && script->skip_phase==2;
            script->skip_phase=0;
            script->skip_request=0;
            g.frame_selector=0;
            g.cam.top_mode=0;
            return skipped ? EM_SCRIPT_ABORT : EM_SCRIPT_ADVANCE;
        }
        return EM_SCRIPT_UNSUPPORTED;
    case 6: /* 001B8BD0 flag operations, opening uses sub0 / event39. */
        if (sub!=0 || em_script_u32(record,0x14)!=0x39)
            return EM_SCRIPT_UNSUPPORTED;
        g.opening_event_39=1;
        return EM_SCRIPT_ADVANCE;
    case 12: /* 001B7D60: independent message request, no completion wait. */
        if (sub!=1 || em_script_u32(record,0x14)!=0x66 ||
            em_script_u32(record,0x18)!=0 || em_script_u32(record,0x1C)!=0)
            return EM_SCRIPT_UNSUPPORTED;
        if (em_opening_media_dialogue_start()!=0) return EM_SCRIPT_UNSUPPORTED;
        return EM_SCRIPT_ADVANCE;
    case 10: /* 001B9A00: player track / restore world placement. */
        if (sub==1) {
            if (em_script_u32(record,0x14)!=1 ||
                em_script_u32(record,0x1C)!=0x98 ||
                em_script_f32(record,0x0C)!=0.5f)
                return EM_SCRIPT_UNSUPPORTED;
            s.actors_active=1;
            s.half_tick=0;
            em_opening_actor_begin();
            return EM_SCRIPT_ADVANCE;
        }
        if (sub==5) {
            EmGfxMesh *mesh;
            const float *palette;
            uint32_t bone_count;
            if (!em_opening_actor_record(0,s.half_tick,&mesh,&palette,
                                        &bone_count) || bone_count<2)
                return EM_SCRIPT_UNSUPPORTED;
            /* Actor bone pointer+0x114 is bone1, translation+0xC0 in
             * original node. The following op01/sub9 places it exactly. */
            g.pos[0]=palette[16+12];
            g.pos[1]=palette[16+13]-11.0f;
            g.pos[2]=palette[16+14];
            return EM_SCRIPT_ADVANCE;
        }
        return EM_SCRIPT_UNSUPPORTED;
    case 20: { /* 001B70B0: two original 0x2C-byte spawn records. */
        unsigned char *actors=em_script_image_read(&s.image,
                                   em_script_u32(record,0x14),0x58);
        if (sub!=0 || !actors || actors[0]!=9 || actors[0x2C]!=8)
            return EM_SCRIPT_UNSUPPORTED;
        /* Models and exact poses were prepared before script execution;
         * both become drawable on the same instruction frame. */
        return EM_SCRIPT_ADVANCE;
    }
    case 0: /* 001B8FC0: camera track or explicit final placement. */
        if (sub==6) {
            if (em_script_u32(record,0x14)!=0 ||
                em_script_u32(record,0x18)!=0x22 ||
                em_script_u32(record,0x1C)!=0x98)
                return EM_SCRIPT_UNSUPPORTED;
            s.camera_time=0;
            s.camera_active=s.camera_owned=1;
            g.cam.top_mode=3;
            return EM_SCRIPT_ADVANCE;
        }
        if (sub==0 && em_script_f32(record,0x0C)==0.0f) {
            if (script->phase==0) {
                record_vector(g.cam.eye,record,0x20);
                record_vector(g.cam.tgt,record,0x30);
                memcpy(g.cam.eye_des,g.cam.eye,sizeof g.cam.eye);
                memcpy(g.cam.tgt_des,g.cam.tgt,sizeof g.cam.tgt);
                g.cam.yaw=atan2f(g.cam.tgt[0]-g.cam.eye[0],
                                g.cam.tgt[2]-g.cam.eye[2]);
                script->phase=1;
                return EM_SCRIPT_WAIT;
            }
            return EM_SCRIPT_ADVANCE;
        }
        return EM_SCRIPT_UNSUPPORTED;
    case 13: /* 001B7B30: wait for the original camera duration. */
        if (sub!=0) return EM_SCRIPT_UNSUPPORTED;
        if (s.camera_time<s.camera.duration) return EM_SCRIPT_WAIT;
        camera_restore();
        return EM_SCRIPT_ADVANCE;
    case 24: /* 001B6BF0: skip waits at full black before teardown. */
        if (script->phase==0) {
            if (script->skip_phase!=2) {
                script->skip_phase=3;
                return EM_SCRIPT_CONTINUE;
            }
            em_frame_fade_start(1,8); /* 001B0C00(8), stream fade follows */
            em_opening_media_fade_out(8);
            script->phase=1;
            return EM_SCRIPT_WAIT;
        }
        if (script->phase==1) {
            if (em_frame_transition()->substate!=2) return EM_SCRIPT_WAIT;
            em_opening_media_stop();
            camera_restore();
            g.cam.top_mode=2;
            return EM_SCRIPT_ADVANCE;
        }
        return EM_SCRIPT_UNSUPPORTED;
    case 1: /* 001B94F0 sub9 ->001B6F80->00182F90. */
        if (sub!=9) return EM_SCRIPT_UNSUPPORTED;
        record_vector(g.pos,record,0x20);
        g.yaw=em_script_f32(record,0x34);
        return EM_SCRIPT_ADVANCE;
    default: return EM_SCRIPT_UNSUPPORTED;
    }
}

static void notify(void *context, EmOpeningEvent event)
{
    (void)context;
    switch (event) {
    case EM_OPENING_STOP_STREAM: em_bgm_stop(0); break;
    case EM_OPENING_STOP_CHILD_ACTORS:
        /* Controller +0x2E becomes 0xFFFF. BB0E0 children stop drawing when
         * their next callback observes that mask; the controller prop stays.
         * The host has already ended the child draw tracks under full black. */
        s.actors_active=0;
        break;
    case EM_OPENING_EVENT_B9_COMPLETE: g.have_battery=1; break;
    case EM_OPENING_ADD_KEY_ITEM_ZERO: ++g.opening_key_item_zero; break;
    case EM_OPENING_RESUME_MUSIC:
        if (em_opening_media_resume_music(270+((em_random_next()>>16)&127)))
            fail("original AREA11 ambient music");
        break;
    case EM_OPENING_FADE_IN_FOUR: em_frame_fade_start(-1,4); break;
    }
}

void em_opening_runtime_tick(void)
{
    if (!s.requested || !s.ready || s.finished || s.failed) return;
    EmScript *script=&s.controller.script;
    /* 001AE6B0's 0x900 edge mask only admits skip once the entering
     * full-screen fade has finished. It sets the global request; the
     * interpreter scans to op18 during its normal actor callback. */
    if (script->skip_request==1 && em_frame_transition()->substate==0 &&
        (em_frame_input()->pressed&(EM_PAD_START|EM_PAD_SELECT)))
        script->skip_request=2;
    if (s.actors_active) ++s.half_tick;
    if (g.frame_selector && s.player_phase==0) s.player_phase=1;
    EmScriptResult result=em_area11_opening_tick(&s.controller,1,
                          g.opening_event_39,resolve,execute,notify,NULL);
    if (result==EM_SCRIPT_FAULT) { fail("script command binding"); return; }
    if (s.failed) return;
    em_opening_media_tick();
    if (s.actors_active &&
        !em_opening_actor_tick(s.half_tick,em_opening_media_talk_mask())) {
        fail("original face update");
        return;
    }
    if (result==EM_SCRIPT_FINISHED) {
        s.finished=1;
        fprintf(stderr,"opening: complete frame=%d pos=(%.6f,%.6f,%.6f) "
                "yaw=%.8f event39=%u eventB9=%u key0=%u\n",g.frame_no,
                g.pos[0],g.pos[1],g.pos[2],g.yaw,g.opening_event_39,
                g.have_battery?255:0,g.opening_key_item_zero);
    }
}

int em_opening_runtime_camera(void)
{
    if (!s.camera_owned || s.failed) return 0;
    if (s.camera_active) {
        EmCinematicCameraFrame frame;
        em_opening_media_camera_tick(s.camera_time);
        if (s.camera_time==1.0f) em_gamepad_rumble_effect(6,0);
        int result=em_cinematic_camera_sample(&s.camera,s.camera_time,&frame);
        if (result<0) {fail("camera sample"); return 1;}
        if (g.capture_path && g.frame_no==g.capture_frame)
            fprintf(stderr,"opening capture: frame=%d half_tick=%u "
                    "camera_sample=%.1f pc=%#010x eye=(%.9g,%.9g,%.9g) "
                    "target=(%.9g,%.9g,%.9g) roll=%.9g fov=%.9g cut=%d\n",
                    g.frame_no,s.half_tick,s.camera_time,s.controller.script.pc,
                    frame.eye[0],frame.eye[1],frame.eye[2],frame.target[0],
                    frame.target[1],frame.target[2],frame.roll_degrees,
                    frame.fov_degrees,frame.cut);
        if (s.camera_time<s.camera.duration) {
            memcpy(g.cam.eye,frame.eye,sizeof frame.eye);
            memcpy(g.cam.tgt,frame.target,sizeof frame.target);
            /* 00102B08 rotates identity about X. Original up is (0,-1,0),
             * producing (0,-cos(roll),-sin(roll)), not a guessed Z roll. */
            float roll=frame.roll_degrees*EM_PI/180.0f;
            g.cam.up[0]=0;
            g.cam.up[1]=-cosf(roll);
            g.cam.up[2]=-sinf(roll);
            float fov=fmaxf(0.5f,fminf(45.0f,frame.fov_degrees));
            g.cam.zoom=224.0f/tanf((EM_PI*(fov/1.45f))/180.0f);
            s.camera_time+=0.5f;
        } else camera_restore();
    }
    if (g.cam.top_mode==3) camera_commit_cinematic(&g.cam);
    else camera_commit(&g.cam);
    return 1;
}

int em_opening_runtime_busy(void)
{ return s.requested && !s.finished && !s.failed; }
int em_opening_runtime_actors_active(void) {return s.actors_active;}
uint32_t em_opening_runtime_half_tick(void) {return s.half_tick;}
int em_opening_runtime_failed(void) {return s.failed;}

void em_opening_runtime_shutdown(void)
{
    em_opening_media_shutdown();
    em_opening_actor_shutdown(em_frame_gfx());
    em_cinematic_camera_free(&s.camera);
    em_script_image_free(&s.image);
    int failed=s.failed;
    memset(&s,0,sizeof s);
    s.failed=failed; /* main reports the failure after resources release */
}
