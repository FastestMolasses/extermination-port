/* em_area11_script_host.c - the AREA11 overlay owners' event scripts on the
 * live path (census L19). See em_area11_script_host.h and
 * docs/AREA_SCRIPT.md section 6. */
#include "game/em_area11_script_host.h"

#include <stdio.h>
#include <string.h>

#include "game/em_area11_interaction_host.h"
#include "game/em_area_script.h"
#include "game/em_collision_world.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_message_live.h"
#include "game/em_player.h"
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

static int w_001AEB60(void *ctx, int16_t a0)
{
    (void)ctx;
    if (a0 != 4) return report("001AEB60 with a step other than 4 is not bound");
    return frame_event(EM_INTERACTION_BARS_ENTER);
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

static int w_001D25F0(void *ctx, float a0)
{
    (void)ctx;
    if (a0 != 480.0f) return report("001D25F0 with a zoom other than 480 is not bound");
    return frame_event(EM_INTERACTION_ZOOM_DEFAULT);
}

static int w_001CA770(void *ctx, uint32_t actor)
{
    (void)ctx;
    if (actor != EM_AREA_SCRIPT_D_008102B0) return report("001CA770 on an actor other than the player");
    return frame_event(EM_INTERACTION_RELEASE_SKELETON);
}

static int w_001FAE70(void *ctx, int a0)
{
    (void)ctx;
    if (a0 != 0) return report("001FAE70 with a nonzero argument is not bound");
    return frame_event(EM_INTERACTION_RESUME_MUSIC);
}

static int w_001AEE10(void *ctx, int16_t a0, uint8_t a1)
{
    (void)ctx;
    if (a0 != 4 || a1 != 0) return report("001AEE10 other than (4, 0) is not bound");
    return frame_event(EM_INTERACTION_FADE_IN);
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
    if (em_area11_scripts_load(&H.images, EM_AREA11_SCRIPTS_PATH, EM_AREA11_QUADS_PATH, NULL, NULL) < 0)
        return report("no valid " EM_AREA11_SCRIPTS_PATH " / " EM_AREA11_QUADS_PATH
                      " (export them with tools/export_area11_scripts.py)");
    H.images_loaded = 1;
    return 0;
}

int em_area11_script_host_start(EmActor *actor, uint32_t entry)
{
    if (!actor || !H.pool || !H.scene) return report("001BA1A0 before the AREA11 build");
    if (images_ready() < 0) return -1;
    EmScriptImage *image = em_area11_scripts_image(&H.images, entry);
    Owner *o = owner_for(actor, 1);
    if (!image || !o) return report("001BA1A0: no image holds the entry, or no owner slot");
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
     * owner (0015B130 admits the player at its next stage). */
    if (r == 0 && H.scene->spad3B8D != 0 && !em_area11_interaction_host_owns(o) &&
        em_area11_interaction_host_claim_script(o) != 1)
        return -1;
    *result = r;
    return 0;
}
