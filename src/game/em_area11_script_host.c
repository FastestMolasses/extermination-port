/* em_area11_script_host.c - the AREA11 overlay owners' event scripts on the
 * live path (census L19). See em_area11_script_host.h and
 * docs/AREA_SCRIPT.md section 6. */
#include "game/em_area11_script_host.h"

#include <stdio.h>
#include <string.h>

#include "game/em_area11_interaction_host.h"
#include "game/em_area11_roger.h"
#include "game/em_cinematic_playback.h"
#include "game/em_pad_actuator.h"
#include "game/em_scene_bindings.h"
#include "game/em_area_script.h"
#include "game/em_collision_world.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_message_live.h"
#include "game/em_player.h"
#include "game/em_player_stage_workers.h"
#include "game/em_script_host_workers.h"

enum { OWNERS = 4 };

typedef struct {
    EmActor *actor;          /* the script owner's pool record */
    uint32_t generation;
    EmAreaScript host;       /* typed view of the record's +0x1F0 block */
    EmAreaScriptWorld world;
} Owner;

static struct {
    EmActorPool *pool;
    EmSceneState *scene;
    EmArea11Scripts images;
    int images_loaded, images_tried;
    Owner owner[OWNERS];
    EmAreaScriptWorkers workers;
    /* The four-lane views (see the header) and the bytes with no port
     * storage. */
    float cam10[4], cam20[4], eye[4], tgt[4], up[4];
    float pA0[4], pB0[4], pC0[4];
    uint8_t e2;
    float spad3600[4];       /* 0x70003600 (op01 kinds 3/5) */
    /* The script host workers' world (00182BF0, 001B1240 / 001B1380 /
     * 001B12B0 / 001B1470 through the collision world's SDK context). */
    EmScriptHostWorkers shw;
    /* The scripted camera timeline (0022EC30 / 0022EEF0 for scene 1, Roger's
     * encounter; census L22): the bank 0x96 camera track of
     * roger/encounter_camera.emcc, the tangent coefficients of
     * roger/camera_projection.emcp, and the playback whose cursor is the
     * camera's +0x74 (g.cam.cine_time, loaded before and stored after
     * every call). D_00275BFC (the cut counter) has no other port reader. */
    EmCinematicCamera track;
    EmCinematicProjection projection;
    EmCinematicPlayback playback;
    int track_tried, track_loaded, playing;
    uint32_t d275BFC;
} H;

static int report(const char *what)
{
    fprintf(stderr, "em_area11 script host: %s\n", what);
    return -1;
}

/* ------------------------------------------------------------ the view */

static void vec_in(float v[4], const float c[3])
{
    v[0] = c[0];
    v[1] = c[1];
    v[2] = c[2];
}

static void vec_out(float c[3], const float v[4])
{
    c[0] = v[0];
    c[1] = v[1];
    c[2] = v[2];
}

static void view_load(void)
{
    vec_in(H.cam10, g.cam.eye_des);
    vec_in(H.cam20, g.cam.tgt_des);
    vec_in(H.eye, g.cam.eye);
    vec_in(H.tgt, g.cam.tgt);
    vec_in(H.up, g.cam.up);
    vec_in(H.pA0, g.pos);
    float hip[3];
    if (player_pose_hip(hip))
        vec_in(H.pB0, hip);
    const EmPlayerLiveActor *a = player_states_actor();
    H.pC0[0] = em_live_f32(a, 0xC0);
    H.pC0[1] = g.yaw;
    H.pC0[2] = em_live_f32(a, 0xC8);
    H.pC0[3] = em_live_f32(a, 0xCC);
}

/* The handlers write the camera vectors, the player's Euler lanes and (op0A
 * sub 6, not bound yet) +0xA0; +0xB0 is written only through 00182F90. A
 * changed heading goes through the pose host (as 001B9C10 sub 8 does for
 * the elevator's script). */
static int view_store(void)
{
    vec_out(g.cam.eye_des, H.cam10);
    vec_out(g.cam.tgt_des, H.cam20);
    vec_out(g.cam.eye, H.eye);
    vec_out(g.cam.tgt, H.tgt);
    vec_out(g.cam.up, H.up);
    vec_out(g.pos, H.pA0);
    EmPlayerLiveActor *a = player_states_actor_mut();
    em_live_set_f32(a, 0xC0, H.pC0[0]);
    em_live_set_f32(a, 0xC8, H.pC0[2]);
    em_live_set_f32(a, 0xCC, H.pC0[3]);
    if (H.pC0[1] != g.yaw && !player_pose_face(H.pC0[1]))
        return report("001B9C10: the pose host refused the heading");
    return 0;
}

/* ------------------------------------------------------------ workers */

/* 001B82D0's frame events through the interaction host. Each checks the
 * argument the host's binding is for. */
static int frame_event(EmInteractionFrameEvent event)
{
    if (view_store() < 0) return -1;
    int rc = em_area11_interaction_host_frame_event(event);
    view_load();
    return rc == 1 ? 0 : -1;
}

/* 001AEB60(step): the bars' fade-in (em_screen_fade_out, em_fade.c);
 * step 4 through the interaction host's binding of it, any other step
 * (001B82D0 sub 12's 0xFF) through the same translation. */
static int w_001AEB60(void *ctx, int16_t a0)
{
    (void)ctx;
    if (a0 == 4) return frame_event(EM_INTERACTION_BARS_ENTER);
    if (view_store() < 0) return -1;
    em_frame_screen_fade_start(1, a0);
    view_load();
    return 0;
}

static int w_001AEBA0(void *ctx, int16_t a0)
{
    (void)ctx;
    if (a0 != 4) return report("001AEBA0 with a step other than 4 is not bound");
    return frame_event(EM_INTERACTION_BARS_LEAVE);
}

static int w_001D2610(void *ctx, float a0)
{
    (void)ctx;
    if (a0 != 0.0f) return report("001D2610 with a nonzero argument is not bound");
    return frame_event(EM_INTERACTION_SCOPE_ZOOM_ZERO);
}

/* 001D25F0(zoom): the projection zoom (render context +0x2468, the port's
 * g.cam.zoom); 480 through the interaction host's binding, the timeline's
 * restored zoom (001B7B30 sub 0) directly. */
static int w_001D25F0(void *ctx, float a0)
{
    (void)ctx;
    if (a0 == 480.0f) return frame_event(EM_INTERACTION_ZOOM_DEFAULT);
    if (!(a0 > 0.0f) || a0 != a0) return report("001D25F0 with a zoom that is not positive");
    g.cam.zoom = a0;
    return 0;
}

static int w_001CA770(void *ctx, uint32_t actor)
{
    (void)ctx;
    if (actor != EM_AREA_SCRIPT_D_008102B0) return report("001CA770 on an actor other than the player");
    return frame_event(EM_INTERACTION_RELEASE_SKELETON);
}

/* 001FAE70(a0): the scene bindings' translation (a0 == 0, the resume the
 * aborted leave of 001B82D0 sub 4 and 001B7A30 call). */
static int w_001FAE70(void *ctx, int a0)
{
    (void)ctx;
    if (view_store() < 0) return -1;
    int rc = em_scene_bindings_001FAE70(a0);
    view_load();
    return rc < 0 ? report("001FAE70 faulted") : 0;
}

/* 001AEE10(speed, colour) / 001AEDE0(speed, colour): the transition fade in
 * / out (em_transition_fade_in / _out, em_fade.c): (4, 0) through the
 * interaction host's binding, any other pair (001B82D0 sub 12's (0x10, 0)
 * and (4, 0), 001B7840's handles) through the same translation. */
static int w_001AEE10(void *ctx, int16_t a0, uint8_t a1)
{
    (void)ctx;
    if (a0 == 4 && a1 == 0) return frame_event(EM_INTERACTION_FADE_IN);
    if (view_store() < 0) return -1;
    em_frame_fade_start_colour(-1, a0, a1);
    view_load();
    return 0;
}

static int w_001AEDE0(void *ctx, int16_t a0, uint8_t a1)
{
    (void)ctx;
    if (view_store() < 0) return -1;
    em_frame_fade_start_colour(1, a0, a1);
    view_load();
    return 0;
}

/* 001AED80(colour) / 001AEDB0(colour): the transition cleared / full. */
static int w_001AED80(void *ctx, uint8_t a0)
{
    (void)ctx;
    if (view_store() < 0) return -1;
    em_frame_fade_clear(a0);
    view_load();
    return 0;
}

static int w_001AEDB0(void *ctx, uint8_t a0)
{
    (void)ctx;
    if (view_store() < 0) return -1;
    em_frame_fade_full(a0);
    view_load();
    return 0;
}

/* 001FD4C0(line): the message service's stream request (the stream row of
 * `line` in the area; 001FD470(-1), D_008106F4 = 2, 001FA790(0, cue)). A
 * line without a row is the original's no-op. */
static int w_001FD4C0(void *ctx, int32_t a0)
{
    (void)ctx;
    if (view_store() < 0) return -1;
    int rc = em_message_live_stream_request(a0);
    view_load();
    return rc < 0 ? report("001FD4C0 faulted") : 0;
}

/* 00119828(ch, l, r): the scene bindings' (0/1, 0x3FFF, 0x3FFF changes
 * nothing; other values reported, UM_00119828). */
static int w_00119828(void *ctx, int a0, int a1, int a2)
{
    return em_scene_bindings_00119828(ctx, a0, a1, a2) < 0 ? -1 : 0;
}

/* 001B7D60 (op0C): the live message service's op0C over the record; the
 * script's +0x04 is its handshake. */
static int w_001B7D60(void *ctx, uint8_t *handshake, const unsigned char *record, int32_t *result)
{
    (void)ctx;
    if (view_store() < 0) return -1;
    int rc = em_message_live_op0c(handshake, record);
    view_load();
    if (rc < 0) return report("001B7D60 faulted on the live message service");
    *result = rc;
    return 0;
}

/* D_0028A490[i] (em_area11_roger's export of the resource table). */
static int r_0028A490(void *ctx, uint32_t address, uint32_t *value)
{
    (void)ctx;
    return em_area11_roger_table_word(address, value);
}

/* 001B81D0's face attach on the player (001CA700(player, D_0028A490[row],
 * 7) and 001D06D0(player, 1)): the interaction host's player face (the
 * face host's attach is B81D0's reset plus speed 1, docs/ROGER_CINEMATIC.md
 * "Original encounter capture"); the attach publishes 0x70003B8F = 2 after
 * both, as the handler does. Only the player's face resource row 0x18
 * (model 0x3B, the captured +0x2FF) is bound. */
static int w_001CA700(void *ctx, uint32_t actor, uint32_t bank, int16_t a2, int32_t *result)
{
    (void)ctx;
    uint32_t row;
    if (actor != EM_AREA_SCRIPT_D_008102B0 || a2 != 7 ||
        em_area11_roger_table_word(EM_AREA_SCRIPT_D_0028A490 + 4u * 0x18u, &row) < 0 || bank != row)
        return report("001CA700 other than the player's row-0x18 face");
    if (view_store() < 0) return -1;
    int rc = em_area11_interaction_host_face_attach();
    view_load();
    if (!rc) return report("001CA700: the interaction host refused the player face");
    *result = 1;
    return 0;
}

static int w_001D06D0(void *ctx, uint32_t actor, uint8_t a1)
{
    (void)ctx;
    if (actor != EM_AREA_SCRIPT_D_008102B0 || a1 != 1)
        return report("001D06D0 other than the player's speed 1");
    return 0;   /* the attach above set the speed (the face host's B81D0 path) */
}

/* The track header word 001B8FC0 kind 6 stores at the camera's +0x78:
 * the float at the track address (bank 0x96's clip 0). */
static int r_track_head(void *ctx, uint32_t track, float *value)
{
    (void)ctx;
    const uint8_t *b = em_area11_roger_resource(track, 4);
    if (!b) return report("the camera track header is not in the export");
    memcpy(value, b, 4);
    return 0;
}

/* 001C6120(bank, index) over the exported banks. */
static int w_001C6120(void *ctx, uint32_t table, int32_t index, uint32_t *result)
{
    (void)ctx;
    const uint8_t *entry = em_area11_roger_resource(table + 4u + 4u * ((uint32_t)index & 0x7FFFu), 4);
    if (!entry) return report("001C6120: a bank directory entry outside the export");
    uint32_t word;
    memcpy(&word, entry, 4);
    *result = table + (word & ~3u);
    return 0;
}

/* 0022EC30(camera): the timeline start for scene 1 (em_cinematic_playback),
 * over the track at the camera's +0x70 (checked to be bank 0x96's clip 0,
 * which roger/encounter_camera.emcc holds). */
static int track_ready(void)
{
    if (H.track_loaded) return 0;
    if (H.track_tried) return -1;
    H.track_tried = 1;
    if (em_cinematic_camera_load(&H.track, "assets/scene_snow/roger/encounter_camera.emcc") != 0 ||
        !em_cinematic_projection_load(&H.projection, "assets/scene_snow/roger/camera_projection.emcp"))
        return report("no valid roger/encounter_camera.emcc / camera_projection.emcp "
                      "(tools/export_roger_cinematic.py)");
    H.track_loaded = 1;
    return 0;
}

static int w_0022EC30(void *ctx, uint32_t camera)
{
    (void)ctx;
    uint32_t bank, expected;
    if (camera != EM_AREA_SCRIPT_D_008101E0 || track_ready() < 0 ||
        em_area11_roger_table_word(EM_AREA_SCRIPT_D_0028A490 + 4u * 0x96u, &bank) < 0 ||
        w_001C6120(NULL, bank, 0, &expected) < 0)
        return -1;
    if (g.cam.cine_track != expected || g.cam.cine_head != H.track.duration)
        return report("0022EC30: the camera track is not bank 0x96's clip 0 (the exported encounter track)");
    if (!em_cinematic_playback_start(&H.playback, &H.track, g.cam.cine_scene))
        return report("0022EC30: a timeline scene other than 1 has no worker");
    H.playback.time = g.cam.cine_time;
    H.playing = 1;
    return 0;
}

/* The owner's 001C67E0 (op0B sub 4, op15): Roger's record. */
static int w_001C67E0(void *ctx, uint32_t actor, int16_t clip, float a, float b)
{
    (void)ctx;
    Owner *o = NULL;
    for (unsigned i = 0; i < OWNERS; ++i)
        if (H.owner[i].actor && em_actor_pool_address(H.pool, H.owner[i].actor) == actor) o = &H.owner[i];
    if (!o || o->actor->callback != 0x008237E0u) return report("001C67E0 on an owner other than Roger");
    if (view_store() < 0) return -1;
    int rc = em_area11_roger_clip_init(o->actor, clip, a, b);
    view_load();
    return rc;
}

/* 001B0250: the scene bindings' (D_008106C8 from the spawn record). */
static int w_001B0250(void *ctx)
{
    (void)ctx;
    return em_scene_bindings_001B0250();
}

/* 0021B9A0(mode, scale, bias): the fog / depth-range programmer on the
 * render context; the port has no canonical render-context block
 * (FRAME_RENDER_HEADS.md section 4): reported (UM_0021B9A0). */
static int w_0021B9A0(void *ctx, int mode, float scale, float bias)
{
    (void)ctx;
    (void)mode;
    (void)scale;
    (void)bias;
    return em_scene_bindings_report_0021B9A0();
}

static int w_001D2830(void *ctx, int a0, int a1)
{
    (void)ctx;
    (void)a0;
    (void)a1;
    return em_scene_bindings_report_001D2830();
}

/* 001FBC50 / 001FABB0: the scene bindings' voice-stop and stream-release
 * stand-in (docs/STREAM_LANES.md). */
static int w_001FBC50(void *ctx)
{
    (void)ctx;
    return em_scene_bindings_001FBC50();
}

static int w_001FABB0(void *ctx)
{
    (void)ctx;
    return em_scene_bindings_001FABB0();
}

/* 00182BF0, 001B1240, 001B1380, 001B12B0 (em_script_host_workers) and
 * 001B1470 (em_player_001B1470 through it), 001B0C00 and 001B6250. */
static int shw_ready(void)
{
    EmSdkMathContext *sdk = em_collision_world_sdk();
    EmPlayerLiveActor *p = player_states_actor_mut();
    EmScriptHostWorkersWorld *w = &H.shw.world;
    if (!sdk || !p) return report("the script host workers need the collision world's SDK context and the player");
    w->player_address = EM_SCRIPT_HOST_D_008102B0;
    w->player = p;
    w->d8106BC = em_scene_req_at(H.scene, 0x008106BCu);
    w->d81083C = em_scene_progress_at(H.scene, 0x0081083Cu, 1);
    w->d8106F1 = em_scene_req_at(H.scene, 0x008106F1u);
    w->sdk_tables = sdk->tables;
    w->sdk_world = &sdk->world;
    w->sdk_workers = &sdk->workers;
    H.shw.fault_address = 0;
    return w->d8106BC && w->d81083C && w->d8106F1 ? 0 : report("D_008106BC / D_0081083C / D_008106F1");
}

static int shw_result(int rc, const char *what)
{
    if (rc < 0) {
        fprintf(stderr, "em_area11 script host: %s faulted at %08X\n", what, (unsigned)H.shw.fault_address);
        return -1;
    }
    return 0;
}

static int w_00182BF0(void *ctx, uint32_t actor, int32_t *result)
{
    (void)ctx;
    if (shw_ready() < 0 || view_store() < 0) return -1;
    int rc = em_script_host_w_00182BF0(&H.shw, actor, result);
    view_load();
    return shw_result(rc, "00182BF0");
}

static int w_001B1240(void *ctx, const float object[4], float x, float z, float *result)
{
    (void)ctx;
    if (shw_ready() < 0) return -1;
    return shw_result(em_script_host_w_001B1240(&H.shw, object, x, z, result), "001B1240");
}

static int w_001B12B0(void *ctx, float target, float current, float step, float *result)
{
    (void)ctx;
    if (shw_ready() < 0) return -1;
    return shw_result(em_script_host_w_001B12B0(&H.shw, target, current, step, result), "001B12B0");
}

static int w_001B1380(void *ctx, const float from[4], const float to[4], float yaw, int32_t *result)
{
    (void)ctx;
    if (shw_ready() < 0) return -1;
    return shw_result(em_script_host_w_001B1380(&H.shw, from, to, yaw, result), "001B1380");
}

/* 001B1470(x): the angle wrap (em_player_001B1470 over its domain). */
static int w_001B1470(void *ctx, float a0, float *result)
{
    (void)ctx;
    uint32_t in, out;
    memcpy(&in, &a0, 4);
    if ((in & 0x7FFFFFFFu) >= EM_SCRIPT_HOST_WRAP_LIMIT) return report("001B1470 outside its domain");
    out = em_player_001B1470(in);
    memcpy(result, &out, 4);
    return 0;
}

/* 001B0C00(8) (001B6BF0's skip landing): 001AEDE0(a0, 0) and the three
 * stream channel fades 001FAD70, which the port's stream players do not
 * model (reported, UM_001FAD70). */
static int shw_001AEDE0(void *ctx, int32_t a0, int32_t a1)
{
    (void)ctx;
    em_frame_fade_start_colour(1, a0, (uint8_t)a1);
    return 0;
}

static int shw_001FAD70(void *ctx, int32_t channel, int32_t a1, int32_t a2)
{
    (void)ctx;
    (void)channel;
    (void)a1;
    (void)a2;
    return em_scene_bindings_report_001FAD70();
}

static int w_001B0C00(void *ctx, int a0)
{
    (void)ctx;
    if (shw_ready() < 0 || view_store() < 0) return -1;
    H.shw.callees.w_001AEDE0 = shw_001AEDE0;
    H.shw.callees.w_001FAD70 = shw_001FAD70;
    int rc = em_script_host_w_001B0C00(&H.shw, a0);
    view_load();
    return shw_result(rc, "001B0C00");
}

static int w_001B6250(void *ctx, uint32_t address)
{
    (void)ctx;
    return em_pad_actuator_001B6250(address);
}

/* The player's node 1 +0xC0 (0x1B9A00 sub 5's *(D_008102B0 + 0x114) + 0xC0). */
static int r_player_bone_C0(void *ctx, float out[4])
{
    (void)ctx;
    return player_pose_node_quad(1, 0xC0, out) ? 0 : report("the player's node 1 is not available");
}

/* 001DD980(eye, target): the world camera's render-context publication of
 * the working vectors (the host's binding, em_interaction_projection). */
static int w_001DD980(void *ctx, const float eye[4], const float target[4])
{
    (void)ctx;
    if (eye != H.eye || target != H.tgt) return report("001DD980 on vectors other than D_008105D0 / E0");
    if (view_store() < 0) return -1;
    int rc = em_area11_interaction_host_camera_publish();
    view_load();
    return rc == 1 ? 0 : -1;
}

/* 0011E2A8 (sinf): the one bound SDK sine, em_sdk_math_original over the
 * collision world's SDK context (the tables every live SDK caller uses).
 * tests/area_script_test.c shows it equal to em_area_script_sin_0011E2A8
 * on every ease argument the scripts form. A fault it records fails the
 * call. */
static int w_0011E2A8(void *ctx, float a0, float *result)
{
    (void)ctx;
    EmSdkMathContext *sdk = em_collision_world_sdk();
    if (!sdk) return report("0011E2A8 without the SDK context (the collision world is not loaded)");
    if (em_sdk_math_original_w_0011E2A8(sdk, a0, result) < 0 || sdk->fault) return -1;
    return 0;
}

/* 00182F90(D_008102B0, target): the pose host's position-mirror service. */
static int w_00182F90(void *ctx, uint32_t actor, const float target[4])
{
    (void)ctx;
    if (actor != EM_AREA_SCRIPT_D_008102B0) return report("00182F90 on an actor other than the player");
    float position[3] = {target[0], target[1], target[2]};
    if (view_store() < 0) return -1;
    int rc = player_pose_align(position);
    view_load();
    return rc ? 0 : report("00182F90: the pose host refused the placement");
}

/* ------------------------------------------------------------ owners */

static void workers_bind(void)
{
    memset(&H.workers, 0, sizeof H.workers);
    H.workers.w_001AEB60 = w_001AEB60;
    H.workers.w_001AEBA0 = w_001AEBA0;
    H.workers.w_001D2610 = w_001D2610;
    H.workers.w_001D25F0 = w_001D25F0;
    H.workers.w_001CA770 = w_001CA770;
    H.workers.w_001FAE70 = w_001FAE70;
    H.workers.w_001AEE10 = w_001AEE10;
    H.workers.w_001DD980 = w_001DD980;
    H.workers.w_0011E2A8 = w_0011E2A8;
    H.workers.w_00182F90 = w_00182F90;
    /* Census L22: the workers Roger's scripts reach (0x8283D0, 0x828990,
     * 0x828810; docs/AREA_SCRIPT.md section 3). */
    H.workers.r_0028A490 = r_0028A490;
    H.workers.r_track_head = r_track_head;
    H.workers.r_player_bone_C0 = r_player_bone_C0;
    H.workers.w_001AEDE0 = w_001AEDE0;
    H.workers.w_001AED80 = w_001AED80;
    H.workers.w_001AEDB0 = w_001AEDB0;
    H.workers.w_001FD4C0 = w_001FD4C0;
    H.workers.w_00119828 = w_00119828;
    H.workers.w_001CA700 = w_001CA700;
    H.workers.w_001D06D0 = w_001D06D0;
    H.workers.w_001B7D60 = w_001B7D60;
    H.workers.w_001C6120 = w_001C6120;
    H.workers.w_0022EC30 = w_0022EC30;
    H.workers.w_001C67E0 = w_001C67E0;
    H.workers.w_001B0250 = w_001B0250;
    H.workers.w_0021B9A0 = w_0021B9A0;
    H.workers.w_001D2830 = w_001D2830;
    H.workers.w_001FBC50 = w_001FBC50;
    H.workers.w_001FABB0 = w_001FABB0;
    H.workers.w_00182BF0 = w_00182BF0;
    H.workers.w_001B1240 = w_001B1240;
    H.workers.w_001B12B0 = w_001B12B0;
    H.workers.w_001B1380 = w_001B1380;
    H.workers.w_001B1470 = w_001B1470;
    H.workers.w_001B0C00 = w_001B0C00;
    H.workers.w_001B6250 = w_001B6250;
}

static void world_bind(Owner *o)
{
    EmSceneState *s = H.scene;
    EmAreaScriptWorld *w = &o->world;
    memset(w, 0, sizeof *w);
    w->spad3B84 = &s->spad3B84;
    w->spad3B8D = &s->spad3B8D;
    w->spad3B8F = &s->spad3B8F;
    w->spad3B91 = &s->spad3B91;
    w->spad3B92 = &s->spad3B92;
    w->spad3600 = H.spad3600;
    w->d8106EF = em_scene_req_at(s, 0x008106EFu);
    w->d8106F3 = em_scene_req_at(s, 0x008106F3u);
    w->d8106F4 = em_scene_req_at(s, 0x008106F4u);
    w->d8106D4 = em_scene_req_at(s, 0x008106D4u);
    /* The flag and counter arrays D_00810758[] / D_008107D8[] are the D2
     * progress region's bytes; slots_canonical refuses a script whose
     * op06 / op07 sub 5 / sub 6 names a slot that is not migrated (census
     * L22: 0x00 and 0x3B of the flags, 0x00 and 0x3B of the counters). */
    w->d810758 = em_scene_progress_spawn_view(s);
    w->d8107D8 = em_scene_progress_spawn_view(s) + (0x008107D8u - 0x00810758u);
    w->d81078F = em_scene_progress_at(s, 0x0081078Fu, 1);
    w->cam_50 = &g.cam.y_lo;
    w->cam_54 = &g.cam.y_hi;
    w->cam_6E = &g.cam.cine_scene;
    w->cam_70 = &g.cam.cine_track;
    w->cam_74 = &g.cam.cine_time;
    w->cam_78 = &g.cam.cine_head;
    w->cam_A0 = &g.cam.tgt_soft;
    /* The player record's bytes the op0A / op01 / face-attach handlers
     * write (the live record, D_008102B0). */
    EmPlayerLiveActor *p = player_states_actor_mut();
    if (p) {
        w->p040 = (uint32_t *)(void *)(p->bytes + 0x40);
        w->p1F2 = (int16_t *)(void *)(p->bytes + 0x1F2);
        w->p1F4 = (float *)(void *)(p->bytes + 0x1F4);
        w->p1F8 = (float *)(void *)(p->bytes + 0x1F8);
        w->p200 = (uint32_t *)(void *)(p->bytes + 0x200);
        w->p20C = (int16_t *)(void *)(p->bytes + 0x20C);
        w->p25C = p->bytes + 0x25C;
        w->p2F3 = p->bytes + 0x2F3;
        w->p2FF = p->bytes + 0x2FF;
    }
    w->d8101E1 = &g.cam.sub_state;
    w->d8101E2 = &H.e2;
    w->d8101E3 = &g.cam.swing;
    w->d8101E4 = &g.cam.top_mode;
    w->d8101E6 = &g.cam.mode;
    w->cam_10 = H.cam10;
    w->cam_20 = H.cam20;
    w->d8105D0 = H.eye;
    w->d8105E0 = H.tgt;
    w->d8105F0 = H.up;
    EmMessageBlock *m = em_message_live_block();
    if (m) {
        w->d2821B0 = &m->mode;
        w->d2821B4 = &m->phase;
        w->d2821B8 = &m->line;
        w->d2821BC = &m->delay;
    }
    w->p0A0 = H.pA0;
    w->p0B0 = H.pB0;
    w->p0C0 = H.pC0;
    w->self = em_actor_pool_address(H.pool, o->actor);
    w->s0B0 = o->actor->pos;
    w->s0C0 = o->actor->rot;
    /* Roger's +0x40 (op0B sub 4's bank word) lives in his owner's record. */
    w->s040 = o->actor->callback == 0x008237E0u ? em_area11_roger_bank_word(o->actor) : NULL;
    w->d28A9A0 = &em_frame_transition()->substate;
}

static void block_load(Owner *o)
{
    const uint8_t *b = o->actor->scratch;
    memcpy(&o->host.script.active, b + 0x00, 4);
    memcpy(&o->host.script.phase, b + 0x04, 4);
    memcpy(&o->host.script.pc, b + 0x08, 4);
    o->host.script.skip_phase = (int8_t)b[0x0C];
    memcpy(&o->host.st_0E, b + 0x0E, 2);
    memcpy(o->host.st_10, b + 0x10, 16);
    memcpy(o->host.st_20, b + 0x20, 16);
}

static void block_store(Owner *o)
{
    uint8_t *b = o->actor->scratch;
    memcpy(b + 0x00, &o->host.script.active, 4);
    memcpy(b + 0x04, &o->host.script.phase, 4);
    memcpy(b + 0x08, &o->host.script.pc, 4);
    b[0x0C] = (uint8_t)o->host.script.skip_phase;
    memcpy(b + 0x0E, &o->host.st_0E, 2);
    memcpy(b + 0x10, o->host.st_10, 16);
    memcpy(b + 0x20, o->host.st_20, 16);
}

static Owner *owner_for(EmActor *actor, int create)
{
    Owner *free_slot = NULL;
    for (unsigned i = 0; i < OWNERS; ++i) {
        Owner *o = &H.owner[i];
        if (o->actor == actor && o->generation == actor->generation) return o;
        if (!free_slot && (!o->actor || o->actor->generation != o->generation)) free_slot = o;
    }
    if (!create || !free_slot) return NULL;
    memset(free_slot, 0, sizeof *free_slot);
    free_slot->actor = actor;
    free_slot->generation = actor->generation;
    return free_slot;
}

void em_area11_script_host_reset(EmActorPool *pool, EmSceneState *scene)
{
    if (H.images_loaded) em_area11_scripts_free(&H.images);
    memset(&H, 0, sizeof H);
    H.pool = pool;
    H.scene = scene;
    H.up[3] = 1.0f;
    workers_bind();
}

static int images_ready(void)
{
    if (H.images_loaded) return 0;
    if (H.images_tried) return -1;
    H.images_tried = 1;
    if (em_area11_scripts_load(&H.images, EM_AREA11_SCRIPTS_PATH, EM_AREA11_QUADS_PATH, NULL,
                               EM_AREA11_ROGER_PATH) < 0)
        return report("no valid " EM_AREA11_SCRIPTS_PATH " / " EM_AREA11_QUADS_PATH " / " EM_AREA11_ROGER_PATH
                      " (export them with tools/export_area11_scripts.py and tools/export_roger_resources.py)");
    H.images_loaded = 1;
    return 0;
}

/* The D2 bytes a script's op06 (001BA080) and op07 subs 5 / 6 (001B82D0)
 * index D_00810758[] / D_008107D8[] with must be canonical progress bytes
 * (em_scene_state.h): the record chain from `entry` to its stop record,
 * following jump records, within 64 records (em_area11_scripts_load
 * checked that bound). */
static int slots_canonical(EmScriptImage *image, uint32_t entry)
{
    uint32_t pc = entry;
    for (unsigned n = 0; n < 64; ++n) {
        unsigned char *rec = em_script_image_read(image, pc, EM_SCRIPT_RECORD_SIZE);
        if (!rec) return report("a script record outside its image");
        uint32_t flags = em_script_u32(rec, 0), op = flags & 0xFFFu, sub = em_script_u32(rec, 8);
        uint32_t slot = em_script_u32(rec, 0x14), address = 0;
        if (op == 6) address = (sub <= 1 ? 0x00810758u : 0x008107D8u) + slot;
        else if (op == 7 && sub == 5) address = 0x00810758u + slot;
        else if (op == 7 && sub == 6) address = 0x008107D8u + slot;
        if (address && (slot > 0xFFu || !em_scene_progress_canonical(address, 1))) {
            fprintf(stderr, "em_area11 script host: script %08X reaches D2 byte %08X, which is not "
                            "canonical\n", (unsigned)entry, (unsigned)address);
            return -1;
        }
        if (flags & 0x80000000u) return 0;
        pc = (flags & 0x40000000u) ? em_script_u32(rec, 4) : pc + EM_SCRIPT_RECORD_SIZE;
    }
    return report("a script without a stop record within 64 records");
}

int em_area11_script_host_start(EmActor *actor, uint32_t entry)
{
    if (!actor || !H.pool || !H.scene) return report("001BA1A0 before the AREA11 build");
    if (images_ready() < 0) return -1;
    EmScriptImage *image = em_area11_scripts_image(&H.images, entry);
    Owner *o = owner_for(actor, 1);
    if (!image || !o) return report("001BA1A0: no image holds the entry, or no owner slot");
    if (slots_canonical(image, entry) < 0) return -1;
    world_bind(o);
    em_area_script_init(&o->host, image, &o->world, &H.workers);
    block_load(o);
    if (em_area_script_start(&o->host, entry) < 0) return report("001BA1A0: the entry is outside its image");
    block_store(o);
    return 0;
}

int em_area11_script_host_tick(EmActor *actor, int32_t *result)
{
    Owner *o = actor ? owner_for(actor, 0) : NULL;
    if (!o || !result) return report("001BA1F0 on an owner that started no script");
    world_bind(o);
    block_load(o);
    view_load();
    int r = em_area_script_tick(&o->host);
    if (r < 0) {
        fprintf(stderr, "em_area11 script host: 001BA1F0 of %08X faulted at %08X (record %08X)\n",
                (unsigned)actor->callback, (unsigned)o->host.fault_address, (unsigned)o->host.fault_pc);
        return -1;
    }
    if (view_store() < 0) return -1;
    block_store(o);
    /* The scripted frame is open: the shared player takeover serves this
     * owner (0015B130 admits the player at its next stage). The owner's
     * pool record is the token (the Use scan claims Roger with it too). */
    if (r == 0 && H.scene->spad3B8D != 0 && !em_area11_interaction_host_owns(actor) &&
        em_area11_interaction_host_claim_script(actor) != 1)
        return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------ the camera timeline */

/* em_cinematic_playback's events at the camera stage: the publication
 * 001DD980 of the sampled eye / target (the interaction host's projection
 * publish over g.cam), and at the timeline's end its three restores
 * 001B0250, 0021B9A0(0, 0, 0) and 001D2830(2, 0). */
static int camera_emit(void *context, EmCinematicPlaybackEvent event, const EmCinematicPlayback *pb)
{
    (void)context;
    switch (event) {
    case EM_CINEMATIC_CAMERA_PUBLISH:
        memcpy(g.cam.eye, pb->eye, sizeof g.cam.eye);
        memcpy(g.cam.tgt, pb->target, sizeof g.cam.tgt);
        return em_area11_interaction_host_camera_publish() == 1 ? 1 : 0;
    case EM_CINEMATIC_CAMERA_RESTORE_ROOM:
        return em_scene_bindings_001B0250() < 0 ? 0 : 1;
    case EM_CINEMATIC_CAMERA_EFFECT_OFF:
        return em_scene_bindings_report_0021B9A0() < 0 ? 0 : 1;
    case EM_CINEMATIC_CAMERA_FLAG_OFF:
        return em_scene_bindings_report_001D2830() < 0 ? 0 : 1;
    }
    return 0;
}

int em_area11_script_host_camera_0022EEF0(void)
{
    if (!H.playing) return report("0022EEF0: the camera's top mode 3 without a started timeline");
    H.playback.time = g.cam.cine_time;
    int rc = em_cinematic_playback_tick(&H.playback, &H.projection, camera_emit, NULL);
    g.cam.cine_time = H.playback.time;
    if (rc < 0) return report("0022EEF0 faulted");
    /* D_008105F0 and the projection zoom: the sampled ones, or the
     * restored (0, -1, 0, 1) and 480 at the end. */
    memcpy(g.cam.up, H.playback.up, sizeof g.cam.up);
    g.cam.zoom = H.playback.zoom;
    if (rc == 1) {
        *em_scene_req_at(H.scene, 0x008106F3u) = H.playback.auxiliary;   /* the cut byte */
        H.d275BFC = H.playback.cut_counter;
    }
    return 0;
}
