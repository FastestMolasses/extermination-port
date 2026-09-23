/* End-to-end opening orchestration with user-exported original assets.
 * Graphics and the audio device are inert; loaders, script, camera,
 * animation, dialogue, fades and PCM mixer are the real native modules. */
#include "em_audio.h"
#include "em_gfx.h"
#include "em_input.h"
#include "game/em_bgm.h"
#include "game/em_camera.h"
#include "game/em_fade.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_opening_actor.h"
#include "game/em_opening_media.h"
#include "game/em_opening_runtime.h"
#include "game/em_random.h"
#include "game/em_scene_bindings.h"
#include "game/em_scene_frame.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EmGameState g;
/* The canonical coordinator state (design 3.2): the opening runtime reads
 * and writes spad 3B8D/3B91 only here, and the world-frame variant cores
 * below run over it exactly as the bindings do. */
static EmSceneState scene;
EmSceneState *em_scene_state(void) { return &scene; }
static EmTransitionFade fade;
static EmScreenFade bars;
static EmFrameInput input;
static int quit, meshes, subtitles, look_up, rumble, commits, pose_releases;
int player_pose_opening_release(void) {
    assert(scene.spad3B8D==0 && scene.spad3B91==0); /* op 5 -> op 4 cleared them */
    ++pose_releases;
    return 1;
}
static EmAudioCallback audio_callback;
static void *audio_user;
EmGfx *em_frame_gfx(void) { return (EmGfx *)&meshes; }
const EmFrameInput *em_frame_input(void) { return &input; }
const EmTransitionFade *em_frame_transition(void) { return &fade; }
void em_frame_request_quit(void) { quit=1; }
void em_frame_fade_start_colour(int dir,int step,uint8_t color) {
    if(dir>0) em_transition_fade_out(&fade,(int16_t)step,color);
    else em_transition_fade_in(&fade,(int16_t)step,color);
}
void em_frame_fade_start(int dir,int step) {em_frame_fade_start_colour(dir,step,0);}
void em_frame_screen_fade_start(int dir,int step) {
    if(dir>0) em_screen_fade_out(&bars,(int16_t)step);
    else em_screen_fade_in(&bars,(int16_t)step);
}
void camera_commit(EmCamera *camera) {
    assert(camera==&g.cam);
    for(int i=0;i<3;i++) assert(isfinite(camera->eye[i]) && isfinite(camera->tgt[i]));
    commits++;
}
void camera_commit_cinematic(EmCamera *camera) {
    assert(camera->top_mode==3);
    camera_commit(camera);
}
float em_camera_scope_zoom(float value) {assert(value==0);return 480;}
void em_gamepad_rumble(float big,float small,int frames) {
    assert(big>=0 && small>=0 && frames>=0);rumble++;
}
EmAudio *em_audio_create(int rate,EmAudioCallback callback,void *user) {
    assert(rate==48000 && !audio_callback);
    audio_callback=callback;audio_user=user;return (EmAudio *)&audio_callback;
}
void em_audio_destroy(EmAudio *audio) {(void)audio;assert(audio_callback);audio_callback=NULL;}
void em_sfx_stop_all(void) {}
void em_sfx_mix(float *out,int frames,int rate) {(void)out;(void)frames;(void)rate;}
void em_startup_audio_mix(float *out,int frames,int rate) {(void)out;(void)frames;(void)rate;}
void em_hud_subtitle(EmGfx *gfx,const char *text,float y,float height,float skew,
                     uint32_t color,uint32_t outline) {
    (void)color;(void)outline;(void)skew;
    assert(gfx && text && y>=0 && height>0);subtitles++;
    if(strstr(text,"Look up.")) look_up=1;
}
EmGfxMesh *em_gfx_mesh_create(EmGfx *gfx,const float *vertices,uint32_t vc,
        const uint32_t *indices,uint32_t ic,const EmGfxTexDesc *textures,
        uint32_t tc,const uint8_t *texels,uint32_t flags) {
    (void)flags;assert(gfx && vertices && vc && indices && ic && textures && tc && texels);
    meshes++;return (EmGfxMesh *)malloc(1);
}
void em_gfx_mesh_destroy(EmGfx *gfx,EmGfxMesh *mesh) {
    assert(gfx && mesh && meshes>0);meshes--;free(mesh);
}
int em_gfx_mesh_update_positions(EmGfx *gfx,EmGfxMesh *mesh,
                                const float *positions,uint32_t count) {
    assert(gfx && mesh && positions && count);
    for(size_t i=0;i<(size_t)count*3;++i) assert(isfinite(positions[i]));
    return 1;
}
void em_gfx_draw_skinned(EmGfx *gfx,EmGfxMesh *mesh,const float *vp,
                          const float *palette,uint32_t bones) {
    assert(gfx && mesh && vp && palette && bones);
}
/* World-frame stage workers for the real em_sf_001AE5E0/em_sf_001AE6B0
 * cores: the pool walk (mode 0 or 1, the variant's owner pass) ticks the
 * opening controller node, 0018B9C0 runs the opening camera, and the other
 * stages have no effect in this fixture. The cores pick the variant by
 * canonical 3B8D; 001AE6B0 promotes 3B91 1 -> 2 (0x1AE6E0). */
static int16_t r_fade(void *c) {(void)c;return fade.substate;}
static uint32_t r_actor(void *c) {(void)c;return 0;}
static uint8_t r_b9(void *c) {(void)c;return 0x15;}
static int w_none(void *c) {(void)c;return 0;}
static int w_cb590(void *c,uint32_t a0,int a1,int a2,int a3) {(void)c;(void)a0;(void)a1;(void)a2;(void)a3;return 0;}
static int w_actor(void *c,uint32_t a0) {(void)c;(void)a0;return 0;}
static int w_1d1ea0(void *c,int a0) {(void)c;assert(a0==1);return 0;}
static int w_walk(void *c,int mode) {
    (void)c;assert(mode>=0 && mode<=2);
    if(mode<2) em_opening_runtime_tick();
    return 0;
}
static int w_camera(void *c,uint32_t actor) {(void)c;(void)actor;(void)em_opening_runtime_camera();return 0;}
static const EmSceneWorkers workers={
    .r_0028A9A0=r_fade,.r_00275B44=r_actor,.r_008102B9=r_b9,
    .w_001CB590=w_cb590,.w_0015BCF0=w_actor,.w_001CB5A0=w_none,.w_001D1C50=w_none,
    .w_001C1D00=w_actor,.walk_001AFD70=w_walk,.w_0015C160=w_none,.w_001F0360=w_none,
    .w_0018B9C0=w_camera,.w_001AAD00=w_none,.w_001D1EA0=w_1d1ea0};
static void world_frame(void) {
    int rc=scene.spad3B8D ? em_sf_001AE6B0(&scene,&workers) : em_sf_001AE5E0(&scene,&workers);
    assert(rc==0 && !em_scene_faulted(&scene));
}
static void start(const char *scene_dir) {
    memset(&g,0,sizeof g);memset(&input,0,sizeof input);
    memset(&scene,0,sizeof scene); /* 001AFCF0 at the area load */
    g.opencam_on=1;g.opencam_idle=100;
    quit=subtitles=look_up=rumble=commits=pose_releases=0;
    snprintf(g.scene_dir,sizeof g.scene_dir,"%s",scene_dir);
    em_transition_fade_init(&fade);em_transition_fade_full(&fade,0);
    em_screen_fade_init(&bars);em_random_seed(0x45);
    em_opening_runtime_request();em_opening_runtime_scene_ready();
}
static void end(void) {
    em_bgm_shutdown();em_opening_runtime_shutdown();
    assert(!meshes && !audio_callback);
}
static void run(int skip,int shutdown_after) {
    start("assets/scene_snow");assert(!quit && meshes==3);
    unsigned actor_frames=0;
    int marked_frame=-1, skip_sent=0, cutscene_frames=0;
    for(g.frame_no=0;g.frame_no<2000 && em_opening_runtime_busy();g.frame_no++) {
        em_screen_fade_tick(&bars,0,0);
        /* Step C: D_00810E74 in the original layout (START = 0x0800). */
        scene.d810E74=0;scene.d810E50=4;
        int press=skip && look_up && !skip_sent;
        if(press) {scene.d810E74=0x0800;skip_sent=1;}
        uint8_t skip_before=scene.spad3B91;
        cutscene_frames+=scene.spad3B8D!=0;
        world_frame();
        if(press) { /* 001AE6B0's promotion, never the runtime's */
            assert(skip_before==1 && scene.spad3B8D==2 && fade.substate==0);
            assert(scene.spad3B91==2 || !em_opening_runtime_busy());
        }
        assert(!quit);
        if(em_opening_runtime_actors_active()) {
            if(!actor_frames) assert(em_opening_runtime_half_tick()==0);
            for(unsigned index=0;index<3;index++) {
                EmGfxMesh *mesh;const float *palette;uint32_t bones;
                assert(em_opening_actor_record(index,em_opening_runtime_half_tick(),&mesh,&palette,&bones));
            }
            actor_frames++;
        }
        em_opening_media_render(em_frame_gfx());
        em_bgm_service();
        em_transition_fade_tick(&fade);
        float pcm[1600]={0};
        assert(audio_callback);audio_callback(audio_user,pcm,800);
        for(unsigned i=0;i<1600;i++) assert(isfinite(pcm[i]));
        if(g.opening_event_39==0xFF) marked_frame=g.frame_no;
        else assert(!g.opening_complete && !g.opening_key_item_zero);
    }
    assert(g.frame_no<2000 && marked_frame>=0 && commits && subtitles && look_up);
    assert(scene.spad3B8D==0 && scene.spad3B91==0 && g.opening_event_39==0xFF);
    assert(cutscene_frames>0 && scene.d810750==g.frame_no); /* one variant per frame */
    assert(g.opening_complete==0xFF && g.opening_key_item_zero==1);
    assert(g.pos[0]==250.8000030517578f && g.pos[1]==229.89999389648438f && g.pos[2]==209);
    assert(g.yaw==0.6108652949333191f);
    assert(!g.opencam_on && !g.opencam_idle);
    assert(g.cam.eye[0]==268.20001220703125f && g.cam.eye[1]==258.5f && g.cam.eye[2]==182.8000030517578f);
    assert(g.cam.tgt[0]==250.8000030517578f && g.cam.tgt[1]==242.3000030517578f && g.cam.tgt[2]==209);
    assert(!em_opening_runtime_actors_active() && !em_opening_runtime_failed());
    assert(pose_releases==1);
    if(skip) assert(skip_sent && actor_frames<1292);
    else assert(actor_frames==1293 && rumble==1);
    for(int i=0;i<10;i++) world_frame();
    assert(g.opening_key_item_zero==1);
    printf("opening runtime: %s completed, %u actor frames, exact final script placement\n",skip?"skip":"normal",actor_frames);
    if(shutdown_after) end();
}
int main(void) {
    run(0,0);run(1,1); /* second New Game reuses assets in the same process */
    start("build/no-such-opening-fixture");
    assert(quit && em_opening_runtime_failed() && !g.opening_complete && !g.opening_key_item_zero);
    end();assert(em_opening_runtime_failed());
    puts("opening runtime PASS: original assets, dialogue/audio, normal/skip teardown, control handoff, missing-assets failure");
}
