/* em_area11_door.c - the AREA11 fence door 001BC350 on its original owner
 * (census L18). See em_area11_door.h and docs/DOOR_ORIGINAL.md "Binding". */
#include "game/em_area11_door.h"

#include <stdio.h>
#include <string.h>

#include "game/em_area11_boxes.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_area11_roger.h"
#include "game/em_area11_script_host.h"
#include "game/em_collision_world.h"
#include "game/em_door_original.h"
#include "game/em_door_original_runtime.h"
#include "game/em_door_program.h"
#include "game/em_door_transit.h"
#include "game/em_ee_float.h"
#include "game/em_player_stage_workers.h"
#include "game/em_script_host_workers.h"
#include "game/em_sdk_math_original.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_script_door_fan.h"
#include "game/em_startup_load_gaps.h"

#define DOOR_SCENE_DIR "assets/scene_snow"
#define D_0028A574 0x0028A574u
#define D_0024DB80 0x0024DB80u

static struct {
    EmActorPool *pool;
    EmSceneState *scene;
    EmActor *actor;           /* the bound pool record */
    uint32_t generation;
    int16_t door_id;          /* +0x34 */
    int tried, loaded;
    uint32_t link;            /* the EMDO descriptor's +0x56 */
    EmDoorOriginalRuntime rt; /* rt.owner is the view of the record */
    int drawn;                /* +0x4C ran in the last owner call */
    EmSdfFault sdf_fault;
    int freed;
} D;

static int report(const char *what)
{
    fprintf(stderr, "em_area11 door: %s\n", what);
    return -1;
}

/* ------------------------------------------------ record <-> owner view */

static int16_t block_flags(const EmActor *a)
{
    int16_t v;
    memcpy(&v, a->scratch + 0x0E, 2);
    return v;
}

/* The block's +0x0C (001BC0E0's gate) and +0x0E (the advance flags): the
 * script host reads and writes them in the record; the controller in its
 * view. */
static void block_to_view(void)
{
    D.rt.owner.animation_active = (int8_t)D.actor->scratch[0x0C];
    D.rt.owner.animation_flags = block_flags(D.actor);
}

static void view_to_block(void)
{
    D.actor->scratch[0x0C] = (uint8_t)D.rt.owner.animation_active;
    memcpy(D.actor->scratch + 0x0E, &D.rt.owner.animation_flags, 2);
}

static void view_load(void)
{
    const EmActor *a = D.actor;
    EmDoorOriginal *o = &D.rt.owner;
    o->status = a->status;
    o->visible = a->drawn;
    o->class_flags = a->cls;
    o->subtype = a->model;
    o->lifecycle = a->u04[0];
    o->phase = a->u04[1];
    o->armed = a->u0A[1];
    o->freed = 0;
    o->door_id = D.door_id;
    o->side = (int16_t)a->flags2;
    o->link_flags = a->link;
    memcpy(o->origin, a->pos, sizeof o->origin);
    memcpy(o->initialized_scale, a->f80, sizeof o->initialized_scale);
    block_to_view();
}

static void view_store(void)
{
    EmActor *a = D.actor;
    const EmDoorOriginal *o = &D.rt.owner;
    a->status = o->status;
    a->drawn = o->visible;
    a->cls = o->class_flags;
    a->model = o->subtype;
    a->u04[0] = o->lifecycle;
    a->u04[1] = o->phase;
    a->u0A[1] = o->armed;
    D.door_id = o->door_id;
    a->flags2 = (uint16_t)o->side;
    memcpy(a->f80, o->initialized_scale, sizeof o->initialized_scale);
    view_to_block();
}

/* ------------------------------------------------------ 001B0F60 workers */

static int w_001B0EA0(void *ctx, uint8_t *node, int32_t *ret)
{
    (void)ctx;
    (void)node;
    return em_area11_boxes_door_001B0EA0(D.actor, ret);
}

/* bone_init_default_2(door, 0): the source bank's clip 0 at time 0. */
static int w_001C63E0(void *ctx, uint8_t *node, int32_t n)
{
    (void)ctx;
    (void)node;
    return n == 0 && em_door_original_runtime_animation(&D.rt, 0) ? 0
                                                                 : report("bone_init_default_2 other than clip 0");
}

/* 001BBDA0's 001B0F60(door, 0): 1 ready, 0 refused (+0x04 = 3), -1. */
static int h_initialize(void *ctx)
{
    (void)ctx;
    uint32_t bank;
    if (em_area11_roger_table_word(D_0028A574, &bank) < 0) return report("D_0028A574 is not in the export");
    uint8_t node[0x50];
    memset(node, 0, sizeof node);
    node[4] = D.rt.owner.lifecycle;
    const EmSlgNodeStartWorkers w = {NULL, w_001B0EA0, w_001C63E0};
    int32_t ret;
    if (em_slg_001B0F60(&w, node, 0, bank, &ret) < 0) return report("001B0F60 faulted");
    /* The +0x04 increment is em_door_original's lifecycle 1; +0x40 (the bank
     * word) has no reader outside the door's own clip workers. */
    return ret == 0 ? 1 : 0;
}

/* --------------------------------------------------------- 001BBE40 */

static int r_0024DB80(void *ctx, uint32_t address, uint16_t *value)
{
    (void)ctx;
    /* source.emdo carries the door's row of D_0024DB80 (the two halfwords
     * at 0x24DB80 + ((+0x56 >> 8) & 0xFF) * 4, read from the user's ELF by
     * tools/export_door_original.py): no other halfword is exported. */
    const uint32_t row = D_0024DB80 + (((uint32_t)D.actor->link >> 8) & 0xFFu) * 4u;
    if (address != row && address != row + 2) return report("001BBD60: a D_0024DB80 halfword outside the export");
    *value = D.rt.sounds[(address - row) / 2];
    return 0;
}

static int t_patch(void *ctx, const EmDoorTransitPlan *plan)
{
    (void)ctx;
    EmScriptImage *image = em_area11_script_host_door_program();
    if (!image) return -1;
    /* 001BBD60(door, 0x24DC40): the record's +0x18 from D_0024DB80 by the
     * side latch kickoff has just stored. */
    EmSdfWorkers w;
    memset(&w, 0, sizeof w);
    w.r_0024DB80 = r_0024DB80;
    uint32_t word;
    if (em_sdf_001BBD60((int16_t)D.actor->link, (uint16_t)D.rt.owner.side, &word, &w, &D.sdf_fault) < 0)
        return report("001BBD60 faulted");
    EmDoorTransitPlan patched = *plan;
    patched.sound = (uint16_t)word;
    return em_door_program_patch(image, &patched) == 1 ? 1 : report("the program patch was refused");
}

/* player +0xC4 = the kickoff's yaw (the pose host's heading). */
static int t_face(void *ctx, float yaw)
{
    (void)ctx;
    return player_pose_face(yaw) ? 1 : report("001BBE40: the pose host refused the heading");
}

/* 00182F90(D_008102B0, 0x700038A0). */
static int t_align(void *ctx, const float position[4])
{
    (void)ctx;
    const float p[3] = {position[0], position[1], position[2]};
    return player_pose_align(p) ? 1 : report("001BBE40: 00182F90 was refused (the pose host)");
}

static int script_start(void *ctx, uint32_t entry)
{
    (void)ctx;
    view_to_block();
    int rc = em_area11_script_host_start(D.actor, entry);
    block_to_view();
    return rc < 0 ? -1 : 1;
}

/* 001BA1F0(door): 0 running, 1 finished (or aborted: nonzero). */
static int script_tick(void *ctx)
{
    (void)ctx;
    int32_t result;
    view_to_block();
    int rc = em_area11_script_host_tick(D.actor, &result);
    block_to_view();
    if (rc < 0) return -1;
    return result != 0;
}

/* 001BBE40's callees over the one SDK context (the collision world's):
 * 001B1240 (em_script_host_001B1240), 001B1470 (em_player_001B1470 over its
 * domain), 0011E2A8 / 0011DE90 (em_sdk_math_original). */
static int m_bearing(void *ctx, const float origin[3], float x, float z, float *result)
{
    (void)ctx;
    EmSdkMathContext *sdk = em_collision_world_sdk();
    if (!sdk) return report("001B1240 without the SDK context");
    EmScriptHostWorkers h;
    memset(&h, 0, sizeof h);
    h.world.sdk_tables = sdk->tables;
    h.world.sdk_world = &sdk->world;
    h.world.sdk_workers = &sdk->workers;
    const uint32_t object[3] = {em_ee_bits(origin[0]), em_ee_bits(origin[1]), em_ee_bits(origin[2])};
    uint32_t out;
    if (em_script_host_001B1240(&h, object, em_ee_bits(x), em_ee_bits(z), &out) < 0)
        return report("001B1240 faulted");
    *result = em_ee_float(out);
    return 0;
}

static int m_wrap(void *ctx, float x, float *result)
{
    (void)ctx;
    const uint32_t bits = em_ee_bits(x);
    if ((bits & UINT32_C(0x7FFFFFFF)) >= EM_SCRIPT_HOST_WRAP_LIMIT) return report("001B1470 outside its domain");
    *result = em_ee_float(em_player_001B1470(bits));
    return 0;
}

static int m_sine(void *ctx, float x, float *result)
{
    (void)ctx;
    EmSdkMathContext *sdk = em_collision_world_sdk();
    uint32_t fault = 0;
    return sdk && em_sdk_math_original_0011E2A8(sdk->tables, x, result, &fault) == 0 ? 0
                                                                                   : report("0011E2A8 faulted");
}

static int m_cosine(void *ctx, float x, float *result)
{
    (void)ctx;
    EmSdkMathContext *sdk = em_collision_world_sdk();
    uint32_t fault = 0;
    return sdk && em_sdk_math_original_0011DE90(sdk->tables, x, result, &fault) == 0 ? 0
                                                                                   : report("0011DE90 faulted");
}

static int h_kickoff(void *ctx, int locked)
{
    (void)ctx;
    if (locked) return report("the locked program 0x24DEC0 is not admitted (subtype 0x15)");
    const EmDoorTransitMath math = {NULL, m_bearing, m_wrap, m_sine, m_cosine};
    const EmDoorTransitHooks hooks = {NULL, t_patch, t_face, t_align, script_start, script_tick};
    /* +0xC4 of the door, the player's +0xA0 (g.pos). */
    int rc = em_door_transit_kickoff(&D.rt.owner, D.actor->rot[1], g.pos, D.rt.sounds, 0, &math, &hooks);
    return rc == 1 ? 1 : report("001BBE40 faulted");
}

/* --------------------------------------------- clips, commit, publication */

static int h_advance(void *ctx, int16_t *flags)
{
    (void)ctx;
    return em_door_original_runtime_advance(&D.rt, flags) == 1 ? 1 : report("anim_advance_time faulted");
}

static int h_reset_animation(void *ctx)
{
    (void)ctx;
    return em_door_original_runtime_animation(&D.rt, 0) ? 1 : report("anim_clip_init(door, 0) refused");
}

static int h_place(void *ctx)
{
    (void)ctx;
    return em_door_original_runtime_place(&D.rt) == 1 ? 1 : report("001C68C0 faulted");
}

/* 001BC150's fade: 001AEDE0(4, 0) for the room move (em_fade.c). The door
 * id's bit 7 (001B0C00, B8 = 1) has no consumer in the first level. */
static int commit_fade(void *ctx, int whole_area, int ticks)
{
    (void)ctx;
    if (whole_area) return 0;
    em_frame_fade_start_colour(1, ticks, 0);
    return 1;
}

/* 001BC150: rec = D_0024E140[D_00810700] + 4 * (id & 0x7F) (the row
 * source.emdo carries for this door); B8 = 2, B7 = rec[+0x2E]. */
static int h_transition(void *ctx)
{
    (void)ctx;
    if (D.scene->d810700 != 0x0B) return report("001BC150 outside AREA11 (the exported row is AREA11's)");
    EmDoorDestination request = {0};
    if (em_door_transit_commit(&request, D.rt.owner.door_id, (uint16_t)D.rt.owner.side, D.rt.destination,
                               commit_fade, NULL) != 1)
        return report("001BC150 refused");
    D.scene->req[EM_SCENE_REQ_B8] = request.kind;
    D.scene->req[EM_SCENE_REQ_B7] = request.entry;
    return 1;
}

static int w_001B1630(void *ctx, float x, float y, float z, int32_t *result)
{
    (void)ctx;
    const float p[3] = {x, y, z};
    *result = em_area11_interaction_host_visible_001B1630(p);
    return 0;
}

static int w_001B1B70(void *ctx)
{
    (void)ctx;
    return em_collision_world_publish_001B1B70(D.actor) < 0 ? report("001B1B70 faulted") : 0;
}

/* 001B1B30(door, +0xB0, +0xB4 + 10, +0xB8). */
static int h_publish(void *ctx, const float point[3])
{
    (void)ctx;
    EmSdfWorkers w;
    memset(&w, 0, sizeof w);
    w.w_001B1630 = w_001B1630;
    w.w_001B1B70 = w_001B1B70;
    uint8_t visible = D.rt.owner.visible;
    /* 001B1B70 reads the record's +0x02 (the class lists). */
    D.actor->cls = D.rt.owner.class_flags;
    int rc = em_sdf_001B1B30(&visible, point[0], point[1], point[2], &w, &D.sdf_fault);
    return rc < 0 ? report("001B1B30 faulted") : visible;
}

/* +0x4C (001CAA00): the original unit over the runtime's node matrices
 * (em_area11_boxes_door_draw -> em_owner_draw_live). */
static int h_draw(void *ctx)
{
    (void)ctx;
    if (em_area11_boxes_door_draw(D.actor, em_actor_pool_address(D.pool, D.actor), D.rt.palette, 2) < 0)
        return report("001CAA00 faulted");
    D.drawn = 1;
    return 1;
}

static int h_free(void *ctx)
{
    (void)ctx;
    if (em_actor_pool_free_001AFC10(D.pool, D.scene, D.actor) < 0) return -1;
    D.freed = 1;
    return 1;
}

/* ------------------------------------------------------------- public */

void em_area11_door_reset(EmActorPool *pool, EmSceneState *scene)
{
    D.pool = pool;
    D.scene = scene;
    D.actor = NULL;
    D.generation = 0;
    D.door_id = 0;
    D.drawn = 0;
    D.freed = 0;
    memset(&D.sdf_fault, 0, sizeof D.sdf_fault);
    /* The next visit's node runs its own lifecycle 0; the owner view is
     * reloaded from its record on every call. */
    D.rt.failed = 0;
}

static int bind(EmActor *actor)
{
    const EmInteractionSceneOwner *source = NULL;
    if (em_area11_interaction_host_bind_door(actor, &source) < 0) return -1;
    if (!D.loaded) {
        if (D.tried) return -1;
        D.tried = 1;
        const EmDoorOriginalRuntimeHooks unused = {NULL, NULL, NULL, NULL, NULL, h_publish, h_draw};
        if (!em_door_original_runtime_open(&D.rt, DOOR_SCENE_DIR, source, &unused))
            return report("no valid " DOOR_SCENE_DIR "/door_original/ (tools/export_door_original.py)");
        D.loaded = 1;
        D.link = D.rt.owner.link_flags;   /* source.emdo's +0x56 (0x400) */
    }
    if (actor->model != 3 || actor->cls != 0x85 || actor->link != (uint16_t)D.link)
        return report("the door's record is not the EMIS record's subtype 3 / class 0x85 / link");
    D.actor = actor;
    D.generation = actor->generation;
    D.freed = 0;
    return 0;
}

int em_area11_door_tick(EmActor *actor)
{
    if (!actor || actor->callback != EM_AREA11_DOOR_CALLBACK || !D.pool || !D.scene) return -1;
    if (!D.actor || D.actor != actor || D.generation != actor->generation) {
        if (actor->u04[0] != 0) return report("a door record bound after its lifecycle 0");
        if (bind(actor) < 0) return -1;
    }
    D.drawn = 0;
    view_load();
    const EmDoorOriginalHooks hooks = {NULL, h_initialize, h_kickoff, h_advance, script_tick, script_start,
                                       h_transition, h_reset_animation, h_place, h_publish, h_draw, h_free};
    int rc = em_door_original_tick(&D.rt.owner, 1, D.scene->req[EM_SCENE_REQ_B8], &hooks);
    if (rc < 0) {
        fprintf(stderr, "em_area11 door: 001BC350 faulted (lifecycle %u phase %u)\n", D.rt.owner.lifecycle,
                D.rt.owner.phase);
        return -1;
    }
    if (!D.freed) view_store();
    return rc;
}

int em_area11_door_clip_init(EmActor *actor, int16_t clip, float blend, float start)
{
    if (!actor || actor != D.actor || !D.loaded) return report("001C67E0 on an unbound door");
    if (clip < 0 || blend != 0.0f || start != 0.0f || !em_door_original_runtime_animation(&D.rt, (unsigned)clip))
        return report("001C67E0 on the door with a clip, blend or start the source bank does not admit");
    return 0;
}

int em_area11_door_state(uint8_t header[16], uint8_t block[16])
{
    if (!D.actor || D.freed || D.actor->generation != D.generation) return 0;
    uint8_t image[EM_ACTOR_RECORD_SIZE];
    em_actor_pool_record_image(D.pool, D.actor, image);
    memcpy(header, image, 16);
    memcpy(block, D.actor->scratch, 16);
    return 1;
}

void em_area11_door_shutdown(void)
{
    if (D.loaded) em_door_original_runtime_free(&D.rt);
    D.loaded = 0;
    D.tried = 0;
    D.actor = NULL;
}
