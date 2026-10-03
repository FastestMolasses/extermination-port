/* em_area11_roger.c - Roger 008237E0 and the equipment node 001C5C90 on
 * their original owners (census L22). See em_area11_roger.h and
 * docs/ROGER_ACTOR_ORIGINAL.md "Binding". */
#include "game/em_area11_roger.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_model.h"
#include "game/em_area11_bindings.h"
#include "game/em_area11_boxes.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_area11_script_host.h"
#include "game/em_collision_world.h"
#include "game/em_director_original.h"
#include "game/em_frame_render_heads.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_opening_face.h"
#include "game/em_owner_draw_live.h"
#include "game/em_owner_services_original.h"
#include "game/em_player.h"
#include "game/em_player_floor.h"
#include "game/em_player_stage_workers.h"
#include "game/em_pose_host_workers.h"
#include "game/em_random.h"
#include "game/em_roger.h"
#include "game/em_roger_actor_original.h"
#include "game/em_scene_bindings.h"
#include "game/em_sdk_math_original.h"
#include "game/em_script_door_fan.h"
#include "game/em_shadow_live.h"
#include "game/em_startup_load_gaps.h"

enum {
    TABLE_WORDS = EM_AREA11_ROGER_TABLE_WORDS,   /* the words 001AB430 clears (EMRS v2) */
    MAX_REGIONS = 12,
    ROGER_NODES = 21,
    RECORD = EM_ACTOR_RECORD_SIZE
};
#define TABLE_ADDRESS 0x0028A490u
#define POLYGON_ADDRESS 0x0082AB80u
#define METHOD_001CAA00 0x001CAA00u
#define D_0028A56C_INDEX 0x37u
#define DEFAULT_BANK_INDEX 0x4Au   /* D_0028A5B8 */

typedef struct {
    uint32_t address, size;
    const uint8_t *bytes;
    uint32_t library;   /* the region header's word +0x0C: 1 = a library model
                           span no pose host reads (not in em_area11_roger_regions) */
    uint8_t *writable;  /* the header's word +0x08 = 1: the region's bytes, which
                           the run rewrites (model 0x16: 001D9070), else NULL */
} Region;

/* One owner record: the EmActor (canonical for its fields), the bytes it
 * does not hold, and the typed view em_roger_actor_original works on. */
typedef struct {
    EmActor *actor;
    uint32_t generation, address;
    EmPlayerLiveActor rec;       /* the whole record image; EmActor's fields are synced in */
    EmRogerActorRecord typed;
    int freed;
} Owner;

/* A body and the equipment node that rides on it (+0x18 = the body): Roger
 * 008237E0 with his 001C5C90 node (pair 0), or the opening's two 001BB0E0
 * records that 001BAC00 spawns from the opening script's placement list
 * 0x828F30 (pair 1: class 9, model 0x47 with bank 0x98's clip 2, and class
 * 8, the model 0x6B node 001BAD40's command 5 runs 001C5C90 on). The
 * equipment's one bone slot has its typed view here (001C62C0 writes it). */
typedef struct {
    Owner body, equip;
    EmOwnerBone ebone;
    uint32_t ebone_word;
    int opening;                 /* 1: the 001BB0E0 pair */
    /* The opening pair: each record's +0x20 (its placement entry, the
     * 0x2C bytes of the opening image) and +0x24 (the spawning controller
     * 00823E80, whose +0x2E is the done mask 001BB0E0 reads). */
    uint32_t entry[2];
    EmActor *controller;
} Pair;

static struct {
    /* assets/scene_snow/roger/resources.emrs */
    uint8_t *file;
    int res_tried, res_loaded;
    uint32_t table[TABLE_WORDS];
    Region region[MAX_REGIONS];
    unsigned regions;
    /* trigger.empg: the quad 0x82AB80 (four x, y, z, w records) */
    float polygon[4][4];
    int polygon_loaded;
    /* Roger and his equipment (pair 0), the opening's two actors (pair 1);
     * `cur` is the pair whose record the running call works on (every
     * worker below reads it). */
    Pair pair[2];
    Pair *cur;
    EmActorPool *pool;
    EmSceneState *scene;
    EmRogerActor ra;             /* em_roger_actor_original: views, workers, fault */
    EmRoger view;                /* em_roger's view of the record (loaded around each hook) */
    EmRogerStory story;
    /* 001C67E0 / 001C64F0 / 001C68C0 / 001C63E0 on Roger's record */
    EmPoseHost host;
    EmPoseGlobals globals;
    uint32_t spad3400[16], spad3440[16], spad3600[4], spad3760[11];
    uint32_t spad3A3C, spad38B0[4], spad3A20;
    EmPlayerFloorTable column;
    EmPlayerStageHost advance;
    EmPlayerStageScene stage;
    EmPlayerStageGlobals stage_globals;
    uint32_t spad3600_actor[4];  /* 0x70003600 for 001C5C90's probes */
    /* The draw (+0x4C 001CAA00, em_owner_draw_live): the models the two
     * records' +0x44 name (Roger's 0x47, the equipment's library 0x6B),
     * added from the export at their original addresses, and the owner
     * views the draw reads. */
    EmWorldModels bank;
    EmOwnerServicesOwner dview;
    EmOwnerBone dbone[EM_OWNER_SERVICES_MAX_BONES];
    /* the equipment: 001B1020's services over D_0028A56C */
    EmOwnerServices services;
    EmOwnerServicesOwner eview;
    uint32_t d0028A56C;
    int faulted;
} R;

static int report(const char *what)
{
    fprintf(stderr, "em_area11 roger: %s\n", what);
    return -1;
}

static int actor_fault(const char *where)
{
    fprintf(stderr, "em_area11 roger: %s: em_roger_actor fault at %08X code %d\n", where,
            (unsigned)R.ra.fault.address, (int)R.ra.fault.code);
    return -1;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }

/* ------------------------------------------------------------ resources */

static int load_resources(void)
{
    if (R.res_loaded) return 0;
    if (R.res_tried) return -1;
    R.res_tried = 1;
    FILE *f = fopen(EM_AREA11_ROGER_RESOURCES_PATH, "rb");
    if (!f) return report("no " EM_AREA11_ROGER_RESOURCES_PATH " (export it with tools/export_roger_banks.py)");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = size > 0x20 ? malloc((size_t)size) : NULL;
    int ok = data && fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!ok) { free(data); return report(EM_AREA11_ROGER_RESOURCES_PATH " is unreadable"); }
    const size_t n = (size_t)size;
    uint32_t words = rd32(data + 0xC), count = rd32(data + 0x10);
    if (memcmp(data, "EMRS", 4) != 0 || rd32(data + 4) != 2 || rd32(data + 8) != TABLE_ADDRESS ||
        words != TABLE_WORDS || count == 0 || count > MAX_REGIONS || 0x20u + 4u * words > n) {
        free(data);
        return report(EM_AREA11_ROGER_RESOURCES_PATH " is not an EMRS v2 export "
                      "(re-run tools/export_roger_banks.py)");
    }
    for (unsigned i = 0; i < TABLE_WORDS; ++i) R.table[i] = rd32(data + 0x20 + 4 * i);
    size_t at = 0x20u + 4u * words;
    for (unsigned i = 0; i < count; ++i) {
        if (at + 16 > n) { free(data); return report("EMRS region header past the file"); }
        uint32_t address = rd32(data + at), bytes = rd32(data + at + 4), writable = rd32(data + at + 8),
                 library = rd32(data + at + 12);
        at += 16;
        if (bytes == 0 || bytes > n - at || library > 1u || writable > 1u || (writable && !library)) {
            free(data);
            return report("EMRS region past the file");
        }
        R.region[i] = (Region){address, bytes, data + at, library, writable ? data + at : NULL};
        at += bytes;
    }
    if (at != n) { free(data); return report("EMRS trailing bytes"); }
    R.regions = count;
    R.file = data;
    R.res_loaded = 1;
    return 0;
}

static const Region *region_at(uint32_t address, uint32_t size)
{
    for (unsigned i = 0; i < R.regions; ++i) {
        const Region *r = &R.region[i];
        if (address >= r->address && size <= r->size && address - r->address <= r->size - size) return r;
    }
    return NULL;
}

const uint8_t *em_area11_roger_resource(uint32_t address, uint32_t size)
{
    if (load_resources() < 0) return NULL;
    const Region *r = region_at(address, size);
    return r ? r->bytes + (address - r->address) : NULL;
}

int em_area11_roger_table_word(uint32_t address, uint32_t *value)
{
    if (!value || load_resources() < 0) return -1;
    if (address < TABLE_ADDRESS || (address - TABLE_ADDRESS) & 3u || (address - TABLE_ADDRESS) / 4u >= TABLE_WORDS)
        return report("a D_0028A490 word outside the exported table");
    *value = R.table[(address - TABLE_ADDRESS) / 4u];
    return 0;
}

int em_area11_roger_regions(int (*map)(void *ctx, uint32_t address, uint32_t size, const uint8_t *bytes),
                            void *ctx)
{
    if (!map || load_resources() < 0) return -1;
    for (unsigned i = 0; i < R.regions; ++i)
        if (!R.region[i].library && map(ctx, R.region[i].address, R.region[i].size, R.region[i].bytes) < 0)
            return -1;
    return 0;
}

static int load_polygon(void)
{
    if (R.polygon_loaded) return 0;
    FILE *f = fopen(EM_AREA11_ROGER_TRIGGER_PATH, "rb");
    uint8_t buf[16 + 64];
    size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
    int end = f ? fgetc(f) : 0;
    if (f) fclose(f);
    if (n != sizeof buf || end != EOF || memcmp(buf, "EMPG", 4) != 0 || rd32(buf + 4) != 1 ||
        rd32(buf + 8) != 0 || rd32(buf + 12) != 4)
        return report("no valid " EM_AREA11_ROGER_TRIGGER_PATH " (the quad 0x82AB80; "
                      "export it with tools/export_roger_resources.py)");
    memcpy(R.polygon, buf + 16, sizeof R.polygon);
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned k = 0; k < 4; ++k)
            if (!isfinite(R.polygon[i][k])) return report("the quad 0x82AB80 holds a non-finite word");
    R.polygon_loaded = 1;
    return 0;
}

/* ------------------------------------------------------------ records */

/* The record bytes an EmActor holds (em_actor_pool_record_image). */
static const struct { uint16_t at, end; } k_owned[] = {
    {0x00, 0x20}, {0x2E, 0x34}, {0x36, 0x38}, {0x52, 0x9B}, {0x9C, 0x9F}, {0xB0, 0xD0}, {0x1F0, 0x2F0},
};

/* EmActor -> the record image (its fields only). */
static void sync_in(Owner *o)
{
    uint8_t image[RECORD];
    em_actor_pool_record_image(R.pool, o->actor, image);
    for (size_t i = 0; i < sizeof k_owned / sizeof k_owned[0]; ++i)
        memcpy(o->rec.bytes + k_owned[i].at, image + k_owned[i].at, k_owned[i].end - k_owned[i].at);
}

static float f32_of(const uint8_t *p)
{
    float v;
    memcpy(&v, p, 4);
    return v;
}

/* The record image -> the EmActor fields (not the pool links +0x10..+0x1F,
 * which only the pool writes). */
static void sync_out(Owner *o)
{
    EmActor *a = o->actor;
    const uint8_t *r = o->rec.bytes;
    a->status = r[0x00];
    a->drawn = r[0x01];
    a->cls = r[0x02];
    a->model = r[0x03];
    memcpy(a->u04, r + 0x04, sizeof a->u04);
    a->bones = r[0x09];
    memcpy(a->u0A, r + 0x0A, sizeof a->u0A);
    a->param = r[0x0D];
    a->uid = rd16(r + 0x0E);
    a->flags2 = rd16(r + 0x2E);
    a->w30 = rd32(r + 0x30);
    a->h36 = rd16(r + 0x36);
    a->h52 = rd16(r + 0x52);
    a->kind = rd16(r + 0x54);
    a->link = rd16(r + 0x56);
    a->w58 = rd32(r + 0x58);
    a->w5C = rd32(r + 0x5C);
    for (unsigned i = 0; i < 8; ++i) a->f60[i] = f32_of(r + 0x60 + 4 * i);
    for (unsigned i = 0; i < 4; ++i) a->f80[i] = f32_of(r + 0x80 + 4 * i);
    a->w90 = rd32(r + 0x90);
    a->h94 = (int16_t)rd16(r + 0x94);
    a->h96 = (int16_t)rd16(r + 0x96);
    a->b98 = r[0x98];
    a->b99 = r[0x99];
    a->table_index = r[0x9A];
    a->b9C = r[0x9C];
    a->b9D = r[0x9D];
    a->b9E = r[0x9E];
    for (unsigned i = 0; i < 4; ++i) {
        a->pos[i] = f32_of(r + 0xB0 + 4 * i);
        a->rot[i] = f32_of(r + 0xC0 + 4 * i);
    }
    memcpy(a->scratch, r + EM_ACTOR_SCRATCH_OFFSET, EM_ACTOR_SCRATCH_SIZE);
}

/* The record image <-> em_roger_actor_original's typed view. */
static void typed_load(Owner *o)
{
    const uint8_t *r = o->rec.bytes;
    EmRogerActorRecord *t = &o->typed;
    t->address = o->address;
    t->status = r[0x00];
    t->drawn = r[0x01];
    t->cls = r[0x02];
    t->lifecycle = r[0x04];
    t->bones_held = r[0x09];
    t->bone_count = r[0x0C];
    t->kind = r[0x0D];
    t->w14 = rd32(r + 0x14);
    t->parent = rd32(r + 0x18);
    t->descriptor = rd32(r + 0x30);
    t->anim = rd32(r + 0x40);
    t->model = rd32(r + 0x44);
    t->draw = rd32(r + 0x4C);
    t->face_active = (int16_t)rd16(r + 0x56);
    t->w58 = rd32(r + 0x58);
    t->face = rd32(r + 0x90);
    t->face_bone = (int16_t)rd16(r + 0x94);
    t->shadow_kind = (int16_t)rd16(r + 0x96);
    t->pose_bone = r[0x98];
    for (unsigned k = 0; k < 4; ++k) {
        t->fA0[k] = rd32(r + 0xA0 + 4 * k);
        t->fB0[k] = rd32(r + 0xB0 + 4 * k);
        t->fC0[k] = rd32(r + 0xC0 + 4 * k);
    }
    for (unsigned i = 0; i < EM_ROGER_ACTOR_MAX_BONES; ++i) t->bone[i] = rd32(r + 0x110 + 4 * i);
}

static void typed_store(Owner *o)
{
    uint8_t *r = o->rec.bytes;
    const EmRogerActorRecord *t = &o->typed;
    r[0x00] = t->status;
    r[0x01] = t->drawn;
    r[0x02] = t->cls;
    r[0x04] = t->lifecycle;
    r[0x09] = t->bones_held;
    r[0x0C] = t->bone_count;
    r[0x0D] = t->kind;
    wr32(r + 0x30, t->descriptor);
    wr32(r + 0x40, t->anim);
    wr32(r + 0x44, t->model);
    wr32(r + 0x4C, t->draw);
    wr16(r + 0x56, (uint16_t)t->face_active);
    wr32(r + 0x58, t->w58);
    wr32(r + 0x90, t->face);
    wr16(r + 0x94, (uint16_t)t->face_bone);
    wr16(r + 0x96, (uint16_t)t->shadow_kind);
    r[0x98] = t->pose_bone;
    for (unsigned k = 0; k < 4; ++k) {
        wr32(r + 0xA0 + 4 * k, t->fA0[k]);
        wr32(r + 0xB0 + 4 * k, t->fB0[k]);
        wr32(r + 0xC0 + 4 * k, t->fC0[k]);
    }
    for (unsigned i = 0; i < EM_ROGER_ACTOR_MAX_BONES; ++i) wr32(r + 0x110 + 4 * i, t->bone[i]);
}

/* em_roger's view (EmRoger + EmRogerStory) <-> the record and the canonical
 * progress bytes, around every hook (the hooks read and write both). */
static uint8_t *progress(uint32_t address)
{
    return em_scene_progress_at(R.scene, address, 1);
}

static int view_load(void)
{
    const uint8_t *r = R.pair[0].body.rec.bytes;
    EmRoger *v = &R.view;
    v->status = r[0x00];
    v->rendered = r[0x01];
    v->class_flags = r[0x02];
    v->lifecycle = r[0x04];
    v->phase = r[0x05];
    v->armed = r[0x0B];
    v->model_kind = r[0x0D];
    v->animation_result = rd16(r + 0x1FE);      /* the script block's +0x0E */
    memcpy(&v->yaw, r + 0xC4, 4);
    const uint8_t *d7D8 = progress(0x008107D8u), *d791 = progress(0x00810791u),
                  *d793 = progress(0x00810793u), *d813 = progress(0x00810813u);
    if (!d7D8 || !d791 || !d793 || !d813) return report("D_008107D8 / D_00810791 / D_00810793 / D_00810813 "
                                                        "are not canonical");
    R.story.progress = *d7D8;
    R.story.suppressed = *d791;
    R.story.alternate = *d793;
    R.story.auxiliary = *d813;
    return 0;
}

static void view_store(void)
{
    uint8_t *r = R.pair[0].body.rec.bytes;
    const EmRoger *v = &R.view;
    r[0x00] = v->status;
    r[0x01] = v->rendered;
    r[0x02] = v->class_flags;
    r[0x04] = v->lifecycle;
    r[0x05] = v->phase;
    r[0x0B] = v->armed;
    r[0x0D] = v->model_kind;
    wr16(r + 0x1FE, v->animation_result);
    memcpy(r + 0xC4, &v->yaw, 4);
    *progress(0x008107D8u) = R.story.progress;
    *progress(0x00810791u) = R.story.suppressed;
    *progress(0x00810793u) = R.story.alternate;
    *progress(0x00810813u) = R.story.auxiliary;
}

/* Around a call into another owner of the same bytes (the script host,
 * the publication, the pool): the view goes to the record and the record
 * to the EmActor before it, and back after it. */
static void push(void)
{
    view_store();
    sync_out(&R.pair[0].body);
}

static int pull(void)
{
    sync_in(&R.pair[0].body);
    return view_load();
}

/* ------------------------------------------------------------ the slots */

static uint8_t *slot_bytes(uint32_t address)
{
    const EmRogerActorWorld *w = &R.ra.world;
    if (!w->slots || address < w->slots_base) return NULL;
    uint32_t at = address - w->slots_base;
    if (at % EM_ROGER_ACTOR_SLOT_BYTES || at >= w->slots_size) return NULL;
    return w->slots + at;
}

/* The shared 001AF710 stack and arena (em_area11_boxes) and the views the
 * Roger routines read. */
static int world_bind(void)
{
    const EmRogerActorWorld *slots = em_area11_boxes_slot_world();
    if (!slots) return report("the 001AF710 bone-slot stack is not built");
    EmRogerActorWorld *w = &R.ra.world;
    memset(w, 0, sizeof *w);
    w->d00275BCC = slots->d00275BCC;
    w->d00275BD0 = slots->d00275BD0;
    w->slot_stack = slots->slot_stack;
    w->slot_stack_base = slots->slot_stack_base;
    w->slot_stack_words = slots->slot_stack_words;
    w->slots = slots->slots;
    w->slots_base = slots->slots_base;
    w->slots_size = slots->slots_size;
    w->d0028A490 = R.table;
    w->d0028A490_count = TABLE_WORDS;
    w->d00810758 = progress(0x00810758u);
    w->d00810788 = progress(0x00810788u);
    w->d00810700 = &R.scene->d810700;
    w->d008106D4 = em_scene_req_at(R.scene, 0x008106D4u);
    w->spad3600 = R.spad3600_actor;
    return w->d00810758 && w->d00810788 && w->d008106D4 ? 0 : report("D_00810758 / D_00810788 / D_008106D4 "
                                                                   "are not canonical");
}

static const uint8_t *resource_view(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    return em_area11_roger_resource(address, size);
}

/* ------------------------------------------------------------ the pose */

/* The pose host over one body record (Roger's, or the opening Roger's):
 * the export's regions, the slot arena and the record. */
static int pose_bind(Owner *o)
{
    const EmRogerActorWorld *w = &R.ra.world;
    EmPoseHost *h = &R.host;
    memset(h, 0, sizeof *h);
    for (unsigned i = 0; i < R.regions && h->region_count < EM_POSE_REGION_MAX - 2; ++i)
        h->region[h->region_count++] =
            (EmPoseRegion){R.region[i].address, R.region[i].size, (uint8_t *)R.region[i].bytes, 0};
    h->region[h->region_count++] = (EmPoseRegion){w->slots_base, w->slots_size, w->slots, 1};
    h->region[h->region_count++] = (EmPoseRegion){o->address, RECORD, o->rec.bytes, 1};
    EmPoseGlobals *g = &R.globals;
    memset(g, 0, sizeof *g);
    g->d8106F3 = em_scene_req_at(R.scene, 0x008106F3u);
    g->spad3400 = R.spad3400;
    g->spad3440 = R.spad3440;
    g->spad3600 = R.spad3600;
    g->spad3760 = R.spad3760;
    g->spad3A3C = &R.spad3A3C;
    g->spad38B0 = R.spad38B0;
    g->spad3A20 = &R.spad3A20;
    g->column = &R.column;
    h->globals = g;
    /* 001C64F0 (em_player_stage_anim_advance) with the pose host's clip
     * workers; it reads neither the stage scene nor its globals, but the
     * stage workers run only once their D_008106F1 / D_00810707 pointers
     * are bound. */
    memset(&R.advance, 0, sizeof R.advance);
    R.stage.d8106F1 = em_scene_req_at(R.scene, 0x008106F1u);
    R.stage.d810CB6 = em_scene_progress_at(R.scene, 0x00810CB6u, 1);
    R.stage_globals.d810707 = em_scene_progress_at(R.scene, 0x00810707u, 1);
    R.advance.stage = &R.stage;
    R.advance.globals = &R.stage_globals;
    EmPlayerStageCallees *c = &R.advance.callees;
    c->context = h;
    c->bone_init = em_pose_host_stage_bone_init;
    c->clip_init = em_pose_host_stage_clip_init;
    c->clip_resolve = em_pose_host_stage_clip_resolve;
    c->skeleton_frame = em_pose_host_stage_skeleton_frame;
    c->w001C8710 = em_pose_host_stage_8710;
    c->w001C87C0 = em_pose_host_stage_87C0;
    c->sample_bones = em_pose_host_stage_sample_bones;
    c->request = em_pose_host_stage_request;
    h->callees.advance_context = &R.advance;
    h->callees.advance = em_pose_host_player_advance;
    if (!g->d8106F3 || !R.stage.d8106F1 || !R.stage.d810CB6 || !R.stage_globals.d810707)
        return report("D_008106F3 / D_008106F1 / D_00810CB6 / D_00810707 are not canonical");
    return 0;
}

/* ------------------------------------------------------------ the draw */

/* The model at a record's +0x44 (a block model of the export: header,
 * blocks, skeleton records), added to the table-less bank at its original
 * address on first use (em_world_models_add checks it like the parser). */
static const EmWorldModel *bank_model(uint32_t handle)
{
    const EmWorldModel *m = em_world_models_at(&R.bank, handle);
    if (m) return m;
    const uint8_t *head = em_area11_roger_resource(handle, 0x40);
    if (!head) {
        report("a +0x44 model outside the export");
        return NULL;
    }
    const uint32_t bones = rd32(head + 8), skeleton = rd32(head + 0xC);
    if (bones == 0 || bones > EM_OWNER_SERVICES_MAX_BONES || skeleton > 0x01000000u) {
        report("a +0x44 model is not a block model");
        return NULL;
    }
    const uint32_t size = skeleton + 0x50u * bones;
    const uint8_t *all = em_area11_roger_resource(handle, size);
    if (!all || em_world_models_add(&R.bank, handle, all, size, &m) < 0) {
        report("a +0x44 model the bank refuses");
        return NULL;
    }
    return m;
}

typedef struct {
    EmOwnerDrawLiveRegion *out;
    unsigned count, cap;
} RegionList;

static int add_region(void *ctx, uint32_t address, uint32_t size, const uint8_t *bytes)
{
    RegionList *l = ctx;
    if (l->count >= l->cap) return -1;
    l->out[l->count++] = (EmOwnerDrawLiveRegion){address, size, bytes};
    return 0;
}

int em_area11_roger_attachment_regions(uint32_t record, const uint8_t *record_bytes, uint32_t record_size,
                                       EmOwnerDrawLiveRegion *out, unsigned cap)
{
    const EmRogerActorWorld *w = em_area11_boxes_slot_world();
    if (!record_bytes || !out || !w || !w->slots || load_resources() < 0) return -1;
    RegionList l = {out, 0, cap};
    if (add_region(&l, record, record_size, record_bytes) < 0 ||
        add_region(&l, w->slots_base, w->slots_size, w->slots) < 0 ||
        em_area11_roger_regions(add_region, &l) < 0)
        return -1;
    return (int)l.count;
}

/* +0x4C = 001CAA00 on a record of this module: em_owner_draw_live over its
 * owner view (+0x01, +0x02, +0x03, +0x0C, +0x0D, +0x44, +0x80..+0x8F,
 * +0x90, +0x94, +0x98, +0xB0 and the node records +0x110 names, whose
 * +0x90..+0xCF world matrices are D_00275B40), with the attachment's
 * regions (the record, the slot arena and the export) for 001CB3C0. */
static int owner_draw(Owner *o)
{
    const uint8_t *r = o->rec.bytes;
    if (rd32(r + 0x4C) != METHOD_001CAA00) return report("a +0x4C other than 001CAA00");
    const EmWorldModel *m = bank_model(rd32(r + 0x44));
    if (!m) return -1;
    const uint8_t count = r[0x0C];
    if (count != m->model.bone_count || count > EM_OWNER_SERVICES_MAX_BONES)
        return report("a record's node count is not its model's");
    EmOwnerServicesOwner *v = &R.dview;
    memset(v, 0, sizeof *v);
    v->drawn = r[0x01];
    v->cls = r[0x02];
    v->kind = r[0x03];
    v->bones_held = r[0x09];
    v->bone_count = count;
    v->model_id = r[0x0D];
    v->model = &m->model;                       /* +0x44 */
    memcpy(&v->attachment, r + 0x90, 4);
    memcpy(&v->collapsed_bone, r + 0x94, 2);
    v->pose_bone = r[0x98];
    memcpy(v->pos, r + 0xB0, sizeof v->pos);
    for (unsigned k = 0; k < count; ++k) {
        const uint8_t *node = slot_bytes(rd32(r + 0x110 + 4 * k));
        if (!node) return report("a +0x110 word outside the slot arena");
        memcpy(R.dbone[k].world, node + 0x90, sizeof R.dbone[k].world);
        v->bone[k] = &R.dbone[k];
    }
    uint32_t rgb[4];
    memcpy(rgb, r + 0x80, sizeof rgb);          /* +0x80..+0x8F */
    EmOwnerDrawLiveRegion regions[EM_OWNER_DRAW_LIVE_REGIONS];
    int n = 0;
    if (v->attachment) {
        n = em_area11_roger_attachment_regions(o->address, r, RECORD, regions, EM_OWNER_DRAW_LIVE_REGIONS);
        if (n < 0) return report("the attachment's regions (record, slot arena, export)");
    }
    if (em_owner_draw_live_001CAA00_attached(&R.bank, v, rgb, o->address, regions, (unsigned)n) < 0)
        return report("001CAA00 faulted");
    return 0;
}

static int roger_draw(void)
{
    if (R.pair[0].body.rec.bytes[0x0C] != ROGER_NODES) return report("Roger's +0x0C is not 21");
    return owner_draw(&R.pair[0].body);
}

/* ------------------------------------------- em_roger_actor workers */

static int w_001C63E0(void *ctx, EmRogerActorRecord *actor, int32_t clip)
{
    (void)ctx;
    Owner *o = &R.cur->body;
    if (actor != &o->typed) return report("001C63E0 on a record other than the body's");
    typed_store(o);
    if (em_pose_host_001C63E0(&R.host, o->rec.bytes, RECORD, clip) < 0)
        return report("001C63E0 (bone_init_default_2) faulted on Roger's record");
    typed_load(o);
    return 0;
}

/* anim_bone_array_setup: D_00275B40 = the ticking node's +0x110 (the view
 * world.d00275B40 names it for the call). */
static int w_001CB5B0(void *ctx, uint8_t count)
{
    (void)ctx;
    (void)count;
    return 0;
}

static int w_001F0120(void *ctx, uint32_t owner14, int32_t key)
{
    (void)ctx;
    return em_area11_bindings_spawn_001F0120(owner14, (uint8_t)key) < 0 ? -1 : 0;
}

/* 001DA6A0(actor) (001BA580, every call while +0x56 != 0; Roger's kind
 * +0x96 = 0x29): em_shadow_live's translation over his record and its 21
 * node records (the +0x110 slots), drawn after the walk's owner units
 * (docs/SHADOW_ORIGINAL.md "Roger"). */
static int w_001DA6A0(void *ctx, EmRogerActorRecord *actor)
{
    (void)ctx;
    Owner *o = &R.cur->body;
    if (actor != &o->typed) return report("001DA6A0 on a record other than the body's");
    typed_store(o);
    const uint8_t *r = o->rec.bytes;
    const uint8_t *nodes[ROGER_NODES];
    for (unsigned i = 0; i < ROGER_NODES; ++i)
        if (!(nodes[i] = slot_bytes(rd32(r + 0x110 + 4 * i)))) return report("001DA6A0: a +0x110 word outside the slot arena");
    return em_shadow_live_actor_001DA6A0(o->address, r, RECORD, nodes, ROGER_NODES) < 0
               ? report("001DA6A0 faulted (em_shadow_live)")
               : 0;
}

/* 001D0720 (through 001D0C70): the face kernel on the slot at +0x90,
 * em_opening_face_tick_slot over the slot bytes +0x40..+0x5F /
 * +0x70..+0xA7 with the shared 00122BB8 RNG; 001CB3C0 uploads the new
 * weights with the face unit. */
static uint32_t face_random(void *context)
{
    (void)context;
    return em_random_next();
}

static int w_001D0720(void *ctx, EmRogerActorRecord *actor)
{
    (void)ctx;
    uint8_t *slot = slot_bytes(actor->face);
    if (!slot) return report("001D0720: the face slot is outside the arena");
    em_opening_face_tick_slot(slot, face_random, NULL);
    return 0;
}

/* ------------------------------------------- the equipment's services */

int em_area11_roger_001C6120(uint32_t bank, uint32_t id, uint32_t *handle)
{
    const uint8_t *table = em_area11_roger_resource(bank, 4);
    uint32_t count = table ? rd32(table) : 0;
    uint32_t index = id & 0x7FFFu;
    const uint8_t *entry = index < count ? em_area11_roger_resource(bank + 4 + 4 * index, 4) : NULL;
    if (!entry) return report("001C6120 over D_0028A56C: an id outside the exported table");
    *handle = bank + (uint32_t)(((int32_t)rd32(entry) >> 2) << 2);
    return 0;
}

static int e_001C6120(void *ctx, uint32_t bank, uint32_t id, uint32_t *handle)
{
    (void)ctx;
    return em_area11_roger_001C6120(bank, id, handle);
}

/* 001D19D0 -> 001D9070 (see the header): em_frh_001D19D0, the one
 * translation, over the views D_0028A56C (the exported table word) and the
 * export's writable regions (the library's model 0x16), with 001C6120 over
 * the exported table head. */
int em_area11_roger_001D19D0(uint32_t *address, uint32_t *size, uint32_t *digest)
{
    if (load_resources() < 0) return -1;
    EmFrhView views[1 + MAX_REGIONS];
    uint32_t n = 0;
    views[n++] = (EmFrhView){0x0028A56Cu, 4, (uint8_t *)&R.table[(0x0028A56Cu - TABLE_ADDRESS) / 4u], 0};
    const Region *fade = NULL;
    for (unsigned i = 0; i < R.regions; ++i)
        if (R.region[i].writable) {
            views[n++] = (EmFrhView){R.region[i].address, R.region[i].size, R.region[i].writable, 1};
            fade = &R.region[i];
        }
    if (!fade)
        return report(EM_AREA11_ROGER_RESOURCES_PATH " lacks the library model 0x16 that 001D9070 rewrites "
                      "(re-run tools/export_roger_banks.py)");
    EmFrh h;
    memset(&h, 0, sizeof h);
    h.views = views;
    h.view_count = n;
    h.workers.w_001C6120 = e_001C6120;
    if (em_frh_001D19D0(&h) < 0) {
        fprintf(stderr, "roger: 001D19D0 -> 001D9070 faulted at %08X (code %d, data %08X)\n",
                (unsigned)h.fault.address, (int)h.fault.code, (unsigned)h.fault.data);
        return -1;
    }
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < fade->size; ++i) hash = (hash ^ fade->writable[i]) * 16777619u;
    if (address) *address = fade->address;
    if (size) *size = fade->size;
    if (digest) *digest = hash;
    return 0;
}

/* 001CA6E0 = 001CA5E0(owner, handle, 0): +0x44, then 001CA5F0 kind 0:
 * +0x4C = 001CAA00 (em_roger_actor_001CA6E0 on the typed record). The
 * owner-services view's model is the handle's model in the draw's bank
 * (header, blocks and skeleton records of the export, at its original
 * address), the one +0x4C draws. */
static int e_001CA6E0(void *ctx, EmOwnerServicesOwner *owner, uint32_t handle)
{
    (void)ctx;
    Owner *o = &R.cur->equip;
    if (owner != &R.eview) return report("001CA6E0 on a view other than the equipment's");
    if (em_roger_actor_001CA6E0(&R.ra, &o->typed, handle) < 0) return actor_fault("001CA6E0");
    const EmWorldModel *m = bank_model(handle);
    if (!m) return report("001CA6E0: the equipment model is not in the export");
    owner->model = &m->model;
    return 0;
}

static int e_001AF780(void *ctx, EmOwnerBone **slot)
{
    (void)ctx;
    uint32_t word = 0;
    if (em_roger_actor_001AF780(&R.ra, &word) < 0) return actor_fault("001AF780");
    if (!word) {
        *slot = NULL;
        return 0;
    }
    if (R.cur->ebone_word) return report("the equipment popped a second bone slot");
    if (!slot_bytes(word)) return report("001AF780 returned a word outside the slot arena");
    R.cur->ebone_word = word;
    memset(&R.cur->ebone, 0, sizeof R.cur->ebone);
    *slot = &R.cur->ebone;
    return 0;
}

static int e_anim_bone_array_setup(void *ctx, uint8_t count)
{
    (void)ctx;
    (void)count;
    return 0;
}

/* The equipment's one slot: its typed view (001C62C0's writes) laid into
 * the slot bytes the draw and 001C5C90 read. */
static int ebone_store(void)
{
    const EmOwnerBone *b = &R.cur->ebone;
    uint8_t *s = slot_bytes(R.cur->ebone_word);
    if (!s) return report("the equipment's slot is outside the arena");
    memcpy(s + 0x00, b->bind, 64);
    wr16(s + 0x64, (uint16_t)b->parent);
    memcpy(s + 0x70, b->rot, 12);
    memcpy(s + 0x7C, b->trans, 12);
    for (unsigned k = 0; k < 3; ++k) wr16(s + 0x88 + 2 * k, (uint16_t)b->scale[k]);
    return 0;
}

static void services_bind(void)
{
    memset(&R.services, 0, sizeof R.services);
    R.d0028A56C = R.table[D_0028A56C_INDEX];
    R.services.world.d0028A56C = &R.d0028A56C;
    R.services.world.d0028A490 = R.table;
    R.services.world.d0028A490_count = TABLE_WORDS;
    R.services.world.d00275BCC = R.ra.world.d00275BCC;
    R.services.workers.w_001C6120 = e_001C6120;
    R.services.workers.w_001CA6E0 = e_001CA6E0;
    R.services.workers.w_001AF780 = e_001AF780;
    R.services.workers.w_anim_bone_array_setup = e_anim_bone_array_setup;
}

/* 001B1020(e, a1, a2, a3): em_owner_services_001B1020 over the equipment's
 * record, as it is (its own +0x04 stores land in the record). */
static int w_001B1020(void *ctx, EmRogerActorRecord *actor, uint32_t a1, int32_t a2, int32_t a3,
                      int32_t *result)
{
    (void)ctx;
    Owner *o = &R.cur->equip;
    if (actor != &o->typed) return report("001B1020 on a record other than the equipment's");
    EmOwnerServicesOwner *v = &R.eview;
    memset(v, 0, sizeof *v);
    v->drawn = actor->drawn;
    v->cls = actor->cls;
    v->kind = o->rec.bytes[0x03];
    v->lifecycle = actor->lifecycle;
    v->bones_held = actor->bones_held;
    v->bone_count = actor->bone_count;
    v->model_id = actor->kind;
    v->flags2 = rd16(o->rec.bytes + 0x2E);
    v->anim = actor->anim;
    memcpy(v->scale, o->rec.bytes + 0x60, sizeof v->scale);
    memcpy(v->pos, o->rec.bytes + 0xB0, sizeof v->pos);
    memcpy(v->rot, o->rec.bytes + 0xC0, sizeof v->rot);
    int r = em_owner_services_001B1020(&R.services, v, a1, a2, a3);
    if (r < 0 || R.services.fault.code) {
        fprintf(stderr, "em_area11 roger: 001B1020: owner services fault %08X code %d\n",
                (unsigned)R.services.fault.address, (int)R.services.fault.code);
        return -1;
    }
    actor->lifecycle = v->lifecycle;
    actor->bones_held = v->bones_held;
    actor->bone_count = v->bone_count;
    actor->anim = v->anim;
    if (v->bones_held) {
        if (v->bones_held != 1 || v->bone[0] != &R.cur->ebone) return report("the equipment's bone slots");
        actor->bone[0] = R.cur->ebone_word;
        if (ebone_store() < 0) return -1;
    }
    *result = r;
    return 0;
}

/* +0x4C = 001CAA00 on the equipment (jalr from 001C5C90): the owner
 * draw over its record, whose node 0 is the slot 001C5C90 copied Roger's
 * bone 1 into (slot +0x90). The typed view is stored first: 001C5C90's
 * writes (+0x01, +0xA0..+0xCC) are the record's at the call. */
static int w_draw(void *ctx, EmRogerActorRecord *actor)
{
    (void)ctx;
    if (actor != &R.cur->equip.typed || actor->draw != METHOD_001CAA00) return report("the equipment's +0x4C");
    typed_store(&R.cur->equip);
    return owner_draw(&R.cur->equip);
}

static int w_001AFC10(void *ctx, EmRogerActorRecord *actor)
{
    (void)ctx;
    Owner *o = actor == &R.cur->equip.typed ? &R.cur->equip : actor == &R.cur->body.typed ? &R.cur->body : NULL;
    if (!o) return report("001AFC10 on an unknown record");
    typed_store(o);
    sync_out(o);
    if (em_actor_pool_free_001AFC10(R.pool, R.scene, o->actor) < 0) return -1;
    o->freed = 1;
    return 0;
}

static void workers_bind(void)
{
    EmRogerActorWorkers *k = &R.ra.workers;
    memset(k, 0, sizeof *k);
    k->w_001C63E0 = w_001C63E0;
    k->w_001CB5B0 = w_001CB5B0;
    k->w_001F0120 = w_001F0120;
    k->w_001DA6A0 = w_001DA6A0;
    k->w_001D0720 = w_001D0720;
    k->w_001B1020 = w_001B1020;
    k->w_draw = w_draw;
    k->w_001AFC10 = w_001AFC10;
    /* w_001BA7F0 stays NULL: 001BA580 reaches it only for kind 0x61 in
     * area 0x0D (fail-stop). */
    R.ra.world.resource = resource_view;
}

/* ------------------------------------------------------ em_roger hooks */

static int h_script_start(void *ctx, uint32_t entry)
{
    (void)ctx;
    push();
    int rc = em_area11_script_host_start(R.pair[0].body.actor, entry);
    if (pull() < 0) return -1;
    return rc < 0 ? -1 : 1;
}

/* 001BA1F0: 1 (finished) and 3 (the skip path) both end the script. */
static int h_script_tick(void *ctx)
{
    (void)ctx;
    int32_t result = 0;
    push();
    int rc = em_area11_script_host_tick(R.pair[0].body.actor, &result);
    if (pull() < 0) return -1;
    if (rc < 0) return -1;
    return result != 0;
}

/* 001B1EA0(0, &D_00810350, 0x82AB80, 4): em_director_original's
 * translation over the collision world's SDK 0011E620. */
static int trigger_atan2(void *ctx, float y, float x, float *result)
{
    EmSdkMathContext *sdk = ctx;
    *result = em_sdk_math_original_float_0011E620(sdk, y, x);
    return sdk->fault ? -1 : 0;
}

static int h_trigger(void *ctx)
{
    (void)ctx;
    EmSdkMathContext *sdk = em_collision_world_sdk();
    if (!sdk) return report("001B1EA0 without the SDK context (the collision world is not loaded)");
    if (load_polygon() < 0) return -1;
    /* D_00810350, the player's +0xA0: g.pos (the canonical position the
     * truck's owners read as well, em_area11_boxes truck_world). */
    float point[3] = {g.pos[0], g.pos[1], g.pos[2]};
    int32_t inside = 0;
    if (em_director_original_001B1EA0_bound(0, point, (const float (*)[4])R.polygon, 4, trigger_atan2, sdk,
                                            &inside) < 0)
        return report("001B1EA0 faulted");
    return inside != 0;
}

static int h_animation_init(void *ctx, uint16_t clip, float blend, float start)
{
    (void)ctx;
    view_store();
    if (em_pose_host_001C67E0(&R.host, R.pair[0].body.rec.bytes, RECORD, (int16_t)clip, blend, start) < 0)
        return report("001C67E0 (anim_clip_init) faulted on Roger's record");
    return view_load() < 0 ? 0 : 1;
}

static int h_animation_tick(void *ctx, float rate, uint16_t *result)
{
    (void)ctx;
    uint32_t flags = 0;
    view_store();
    if (em_player_stage_anim_advance(&R.advance, &R.pair[0].body.rec, rate, &flags) < 0)
        return report("001C64F0 (anim_advance_time) faulted on Roger's record");
    *result = (uint16_t)flags;
    return view_load() < 0 ? 0 : 1;
}

/* 001B17A0(Roger) through the interaction host's services: +0x01 = the
 * 001B1630 gate; visible owners go onto their class lists (class 0x0A) and,
 * with class bit 0x80, onto the interactive list. */
static int h_publish(void *ctx)
{
    (void)ctx;
    push();
    EmOwnerServicesOwner v;
    memset(&v, 0, sizeof v);
    const EmActor *a = R.pair[0].body.actor;
    v.drawn = a->drawn;
    v.cls = a->cls;
    v.kind = a->model;
    v.lifecycle = a->u04[0];
    v.model_id = a->param;
    v.flags2 = a->flags2;
    memcpy(v.pos, a->pos, sizeof v.pos);
    memcpy(v.rot, a->rot, sizeof v.rot);
    int r = em_area11_interaction_host_offer_001B17A0(R.pair[0].body.actor, &v);
    if (pull() < 0) return -1;
    return r;
}

static int h_event(void *ctx, EmRogerEvent type, unsigned argument)
{
    (void)ctx;
    Owner *o = &R.pair[0].body;
    switch (type) {
    case EM_ROGER_STOP_STREAMS:        /* 001FABB0 */
        return em_scene_bindings_001FABB0() < 0 ? 0 : 1;
    case EM_ROGER_RESTORE_DEFAULT_BANK: /* +0x40 = D_0028A5B8 */
        if (argument != DEFAULT_BANK_INDEX) return 0;
        wr32(o->rec.bytes + 0x40, R.table[DEFAULT_BANK_INDEX]);
        return 1;
    case EM_ROGER_RESUME_MUSIC:        /* 001FAE70(0) */
        return em_scene_bindings_001FAE70(0) < 0 ? 0 : 1;
    case EM_ROGER_FADE_IN:             /* 001AEE10(4, 0) */
        if (argument != 4) return 0;
        em_frame_fade_start(-1, 4);
        return 1;
    case EM_ROGER_FACE_UPDATE:         /* 001BA580(Roger, +0x0D) */
        view_store();
        typed_load(o);
        if (em_roger_actor_001BA580(&R.ra, &o->typed, argument) < 0) return actor_fault("001BA580"), 0;
        typed_store(o);
        return view_load() < 0 ? 0 : 1;
    case EM_ROGER_BUILD_POSE:          /* 001C68C0 */
        view_store();
        if (em_pose_host_001C68C0(&R.host, o->rec.bytes, RECORD) < 0)
            return report("001C68C0 faulted on Roger's record"), 0;
        return view_load() < 0 ? 0 : 1;
    case EM_ROGER_DRAW:                /* +0x4C when +0x01 */
        if (!argument) return 1;
        view_store();
        return roger_draw() < 0 ? 0 : 1;
    case EM_ROGER_AREA_CHANGE:        /* 001B0C60(1, 0, 4) */
        return em_scene_request_area_change_001B0C60(1, 0, 4) < 0 ? 0 : 1;
    case EM_ROGER_RELEASE_FACE:        /* 001BA540 */
        view_store();
        typed_load(o);
        if (em_roger_actor_001BA540(&R.ra, &o->typed) < 0) return actor_fault("001BA540"), 0;
        typed_store(o);
        return view_load() < 0 ? 0 : 1;
    case EM_ROGER_FREE:                /* 001AFC10 */
        view_store();
        typed_load(o);
        return w_001AFC10(NULL, &o->typed) < 0 ? 0 : 1;
    }
    return 0;
}

/* ------------------------------------------- the collision world's view */

/* The close-out passes (001A7870's capsule pass reads Roger's +0x58 record
 * and writes his +0x50) and the hull locks (001A6440 / 001A6AD0 walk the
 * class-2 list Roger publishes onto) read Roger's record in the original
 * layout and the geometry chain his +0x58 names. */
static uint8_t *collision_record_bytes(void *context, uint32_t address, uint32_t size)
{
    (void)context;
    Owner *o = &R.pair[0].body;
    if (o->actor && !o->freed && o->actor->generation == o->generation && address >= o->address &&
        size <= RECORD && address - o->address <= RECORD - size) {
        sync_in(o);
        return o->rec.bytes + (address - o->address);
    }
    return (uint8_t *)(uintptr_t)em_area11_roger_resource(address, size);
}

/* Roger's record, or the opening Roger's (the head sprite 001BA8E0's
 * 001F0120 spawned for either reads its owner through this view). */
const uint8_t *em_area11_roger_record_bytes(uint32_t address, uint32_t size)
{
    for (unsigned i = 0; i < 2; ++i) {
        Owner *o = &R.pair[i].body;
        if (!o->actor || o->freed || o->actor->generation != o->generation || address < o->address ||
            size > RECORD || address - o->address > RECORD - size)
            continue;
        sync_in(o);
        return o->rec.bytes + (address - o->address);
    }
    return NULL;
}

const uint8_t *em_area11_roger_slot_bytes(uint32_t address, uint32_t size)
{
    const EmRogerActorWorld *w = em_area11_boxes_slot_world();   /* the one slot arena */
    if (!w->slots || address < w->slots_base || size > w->slots_size ||
        address - w->slots_base > w->slots_size - size)
        return NULL;
    return w->slots + (address - w->slots_base);
}

static uint32_t collision_address_of(void *context, const EmActor *actor)
{
    (void)context;
    return R.pool ? em_actor_pool_address(R.pool, actor) : 0;
}

static float s_chain_slots[ROGER_NODES * 16];

static int collision_chain(void *context, const EmActor *body, EmCollHullChain *out)
{
    (void)context;
    Owner *o = &R.pair[0].body;
    if (!body || body != o->actor || o->freed || !out) return report("a hull chain for a record Roger's owner does not hold");
    sync_in(o);
    const uint8_t *r = o->rec.bytes;
    uint32_t chain = rd32(r + 0x58);
    const Region *region = region_at(chain, 4);
    if (!region) return report("Roger's +0x58 chain is not in the export");
    unsigned n = r[0x0C];
    if (n > ROGER_NODES) return report("Roger's +0x0C exceeds his node records");
    for (unsigned i = 0; i < n; ++i) {
        const uint8_t *node = slot_bytes(rd32(r + 0x110 + 4 * i));
        if (!node) return report("a Roger +0x110 word outside the slot arena");
        memcpy(s_chain_slots + 16 * i, node + 0x90, 64);
    }
    out->bytes = region->bytes + (chain - region->address);
    out->size = region->size - (chain - region->address);
    out->slots = s_chain_slots;
    out->slot_count = n;
    return 0;
}

/* ------------------------------------------------------------ the owners */

static int owner_for(Owner *o, EmActor *actor)
{
    if (o->actor == actor && o->generation == actor->generation && !o->freed) return 0;
    if (o->actor && !o->freed && o->actor->generation == o->generation && o->actor->allocated)
        return report("a second record with the same original owner");
    memset(o, 0, sizeof *o);
    o->actor = actor;
    o->generation = actor->generation;
    o->address = em_actor_pool_address(R.pool, actor);
    return o->address ? 0 : report("a record outside the pool");
}

static int ready(EmActorPool *pool, EmSceneState *scene)
{
    if (R.faulted) return -1;
    if (!R.cur) {
        R.cur = &R.pair[0];
        R.pair[1].opening = 1;
    }
    R.pool = pool;
    R.scene = scene;
    if (load_resources() < 0 || world_bind() < 0) return -1;
    workers_bind();
    services_bind();
    static const EmCollisionWorldOwners owners = {NULL, collision_record_bytes, collision_address_of,
                                                  collision_chain};
    em_collision_world_bind_owners(&owners);
    return 0;
}

void em_area11_roger_reset(void)
{
    memset(R.pair, 0, sizeof R.pair);
    R.pair[1].opening = 1;
    R.cur = &R.pair[0];
    memset(&R.ra, 0, sizeof R.ra);
    R.faulted = 0;
}

int em_area11_roger_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene)
{
    if (!actor || !pool || !scene || actor->callback != EM_AREA11_ROGER_CALLBACK) return -1;
    if (ready(pool, scene) < 0) return -1;
    R.cur = &R.pair[0];
    Owner *o = &R.cur->body;
    if (owner_for(o, actor) < 0) return -1;
    if (pose_bind(o) < 0) return -1;
    sync_in(o);
    int result;
    if (o->rec.bytes[0x04] == 0) {
        /* 008237E0 case 0: the init (em_roger_actor_original). */
        typed_load(o);
        R.ra.world.d00275B40 = (const uint32_t *)(const void *)(o->rec.bytes + 0x110);
        R.ra.world.d00275B40_count = EM_ROGER_ACTOR_MAX_BONES;
        if (em_roger_actor_008237E0_init(&R.ra, &o->typed) < 0) {
            R.faulted = 1;
            return actor_fault("008237E0 lifecycle 0");
        }
        typed_store(o);
        sync_out(o);
        /* The scan's view of Roger's record (00183EF0's selector-0 class-10
         * branch through the interaction host). */
        if (em_area11_interaction_host_bind_roger(actor) < 0) {
            R.faulted = 1;
            return report("the interaction host refused Roger's record");
        }
        return 1;
    }
    if (view_load() < 0) return -1;
    const EmRogerHooks hooks = {NULL, h_script_start, h_script_tick, h_trigger, h_animation_init,
                                h_animation_tick, h_publish, h_event};
    result = em_roger_tick(&R.view, &R.story, &hooks);
    if (result < 0) {
        R.faulted = 1;
        fprintf(stderr, "em_area11 roger: 008237E0 faulted (lifecycle %u, phase %u)\n", (unsigned)R.view.lifecycle,
                (unsigned)R.view.phase);
        return -1;
    }
    if (o->freed) return 0;
    view_store();
    sync_out(o);
    return 1;
}

int em_area11_roger_equipment_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene)
{
    if (!actor || !pool || !scene || actor->callback != EM_AREA11_ROGER_EQUIPMENT_CALLBACK) return -1;
    if (ready(pool, scene) < 0) return -1;
    R.cur = &R.pair[0];
    Owner *o = &R.cur->equip;
    if (owner_for(o, actor) < 0) return -1;
    Owner *parent = &R.cur->body;
    sync_in(o);
    typed_load(o);
    /* Only the live states 0 and 1 read the parent at +0x18; states 2 and 3
     * free the node at once (001C5F88), which is how it ends the frame after
     * Roger's departure freed him (route beat 15, f446). */
    const int live = o->typed.lifecycle <= 1;
    if (live && (!parent->actor || parent->freed || em_actor_pool_address(pool, actor->prev) != parent->address))
        return report("001C5C90: +0x18 is not Roger's record");
    if (live) typed_load(parent);
    R.ra.world.d00275B40 = (const uint32_t *)(const void *)(o->rec.bytes + 0x110);
    R.ra.world.d00275B40_count = EM_ROGER_ACTOR_MAX_BONES;
    int r = em_roger_actor_001C5C90(&R.ra, &o->typed, live ? &parent->typed : NULL);
    if (r < 0) {
        R.faulted = 1;
        return actor_fault("001C5C90");
    }
    if (o->freed) return 0;
    typed_store(o);
    sync_out(o);
    return 1;
}

/* ------------------------------------------ the opening's actors (001BB0E0)
 *
 * The opening script 0x828FC0's op14 (001BAC00, em_area11_script_host)
 * spawns two records from the placement list 0x828F30 with the default
 * behaviour 001BB0E0 (em_slg_001BB0E0): the class-9 body (entry command 0:
 * 001BAD40 binds D_0028A490[0x47], Roger's model, and bank 0x98 (+0x40)
 * with clip 2 by 001C63E0; 001BA8E0 attaches his face; each tick 001BA580,
 * anim_advance_time by the entry's 0.5, 001C68C0 and the +0x4C draw) and
 * the class-8 node that rides on it (command 5: 001C5C90, the equipment
 * node, over +0x18 = the body). They form pair 1; the controller 00823E80's
 * +0x2E done mask ends them (phase 2, then the free). */

typedef struct {
    Pair *pair;
    Owner *o;                   /* the record this call works on */
    const uint8_t *entry;       /* its +0x20: the 0x2C-byte placement entry */
} OpeningCall;

/* The record bytes 001BAD40 reads or writes <-> em_sdf's view of them. */
static void event_load(const Owner *o, EmSdfEventActor *v)
{
    const uint8_t *r = o->rec.bytes;
    v->lifecycle = r[0x04];
    v->b09 = r[0x09];
    v->b0C = r[0x0C];
    v->w18 = rd32(r + 0x18);
    v->bank_40 = rd32(r + 0x40);
    v->w44 = rd32(r + 0x44);
    for (unsigned i = 0; i < EM_SDF_BONE_SLOTS; ++i) v->bones_110[i] = rd32(r + 0x110 + 4 * i);
}

static void event_store(Owner *o, const EmSdfEventActor *v)
{
    uint8_t *r = o->rec.bytes;
    r[0x04] = v->lifecycle;
    r[0x09] = v->b09;
    r[0x0C] = v->b0C;
    wr32(r + 0x40, v->bank_40);
    wr32(r + 0x44, v->w44);
    for (unsigned i = 0; i < EM_SDF_BONE_SLOTS; ++i) wr32(r + 0x110 + 4 * i, v->bones_110[i]);
}

/* Around a callee that works on the typed record: the view to the record,
 * the record to the typed view; and back after it. */
static void event_enter(OpeningCall *c, const EmSdfEventActor *v)
{
    event_store(c->o, v);
    typed_load(c->o);
}

static void event_leave(OpeningCall *c, EmSdfEventActor *v)
{
    typed_store(c->o);
    event_load(c->o, v);
}

static int sdf_r_0028A490(void *ctx, int32_t index, uint32_t *value)
{
    (void)ctx;
    if (index < 0 || index >= TABLE_WORDS) return report("001BAD40: a D_0028A490 index outside the export");
    *value = R.table[index];
    return 0;
}

static int sdf_001CA6E0(void *ctx, EmSdfEventActor *obj, uint32_t bank)
{
    OpeningCall *c = ctx;
    event_enter(c, obj);
    int r = em_roger_actor_001CA6E0(&R.ra, &c->o->typed, bank);
    event_leave(c, obj);
    return r < 0 ? actor_fault("001CA6E0") : 0;
}

static int opening_001C5C90(OpeningCall *c);

/* 001BAD40 command 5: 001C5C90 on the class-8 record (a0 is the actor). */
static int sdf_001C5C90(void *ctx, EmSdfEventActor *obj)
{
    OpeningCall *c = ctx;
    if (c->o != &c->pair->equip) return report("001BAD40 command 5 on a record other than the class-8 node");
    event_store(c->o, obj);
    int r = opening_001C5C90(c);
    if (r < 0) return -1;
    if (!c->o->freed) event_load(c->o, obj);
    return 0;
}

static int sdf_001C6150(void *ctx, uint32_t model, int32_t *result)
{
    (void)ctx;
    uint8_t count;
    if (em_roger_actor_001C6150(&R.ra, model, &count) < 0) return actor_fault("001C6150");
    *result = count;
    return 0;
}

static int sdf_001AF780(void *ctx, uint32_t *handle)
{
    (void)ctx;
    return em_roger_actor_001AF780(&R.ra, handle) < 0 ? actor_fault("001AF780") : 0;
}

static int sdf_001BA8E0(void *ctx, EmSdfEventActor *obj, int16_t type)
{
    OpeningCall *c = ctx;
    event_enter(c, obj);
    int r = em_roger_actor_001BA8E0(&R.ra, &c->o->typed, (uint32_t)(uint16_t)type);
    event_leave(c, obj);
    return r < 0 ? actor_fault("001BA8E0") : 0;
}

static int sdf_001CA6F0(void *ctx, EmSdfEventActor *obj, uint8_t mode)
{
    OpeningCall *c = ctx;
    event_enter(c, obj);
    int r = em_roger_actor_001CA6F0(&R.ra, &c->o->typed, mode);
    event_leave(c, obj);
    return r < 0 ? actor_fault("001CA6F0") : 0;
}

static int sdf_001CB5B0(void *ctx, uint8_t count)
{
    (void)ctx;
    return w_001CB5B0(NULL, count);
}

/* bone_init_default_2(obj, clip) over the body's pose host (bank +0x40). */
static int sdf_001C63E0(void *ctx, EmSdfEventActor *obj, int16_t clip)
{
    OpeningCall *c = ctx;
    event_store(c->o, obj);
    if (em_pose_host_001C63E0(&R.host, c->o->rec.bytes, RECORD, clip) < 0)
        return report("001C63E0 (bone_init_default_2) faulted on the opening body's record");
    event_load(c->o, obj);
    return 0;
}

/* 001C5C90 on the class-8 record over its +0x18, which must be the pair's
 * body (001BAC00 spawned the two back to back). 0, or -1. */
static int opening_001C5C90(OpeningCall *c)
{
    Owner *o = &c->pair->equip, *parent = &c->pair->body;
    if (!parent->actor || parent->freed || rd32(o->rec.bytes + 0x18) != parent->address)
        return report("001C5C90: the class-8 node's +0x18 is not the opening body's record");
    typed_load(o);
    sync_in(parent);
    typed_load(parent);
    R.ra.world.d00275B40 = (const uint32_t *)(const void *)(o->rec.bytes + 0x110);
    R.ra.world.d00275B40_count = EM_ROGER_ACTOR_MAX_BONES;
    int r = em_roger_actor_001C5C90(&R.ra, &o->typed, &parent->typed);
    if (r < 0) return actor_fault("001C5C90");
    if (!o->freed) typed_store(o);
    return 0;
}

static int slg_001BAD40(void *ctx, const EmSlgScriptActor *a, int32_t *ret)
{
    (void)a;
    OpeningCall *c = ctx;
    EmSdfEventActor v;
    event_load(c->o, &v);
    EmSdfWorld world;
    memset(&world, 0, sizeof world);
    world.d275BCC = R.ra.world.d00275BCC;
    EmSdfWorkers w;
    memset(&w, 0, sizeof w);
    w.ctx = c;
    w.r_0028A490 = sdf_r_0028A490;
    w.w_001CA6E0 = sdf_001CA6E0;
    w.w_001C5C90 = sdf_001C5C90;
    w.w_001C6150 = sdf_001C6150;
    w.w_001AF780 = sdf_001AF780;
    w.w_001BA8E0 = sdf_001BA8E0;
    w.w_001CA6F0 = sdf_001CA6F0;
    w.w_001CB5B0 = sdf_001CB5B0;
    w.w_001C63E0 = sdf_001C63E0;
    /* 0x270D / 0x270C, commands 1..4, 6 and 8 (001C6120, 0022EC30,
     * 001D8BF0, 001C61D0, 001C67E0) are not in the opening's list: their
     * views and workers stay NULL (fail-stop). */
    EmSdfFault fault = {0, 0};
    int r = em_sdf_001BAD40(&v, c->entry, &world, &w, &fault);
    if (r < 0) {
        fprintf(stderr, "em_area11 roger: 001BAD40 faulted at %08X code %d\n", (unsigned)fault.address,
                (int)fault.code);
        return -1;
    }
    if (!c->o->freed) event_store(c->o, &v);
    *ret = r;
    return 0;
}

static int slg_001C5C90(void *ctx, const EmSlgScriptActor *a)
{
    (void)a;
    OpeningCall *c = ctx;
    if (c->o != &c->pair->equip) return report("001BB0E0 command 5 on a record other than the class-8 node");
    return opening_001C5C90(c);
}

static int slg_001C68C0(void *ctx, const EmSlgScriptActor *a)
{
    (void)a;
    OpeningCall *c = ctx;
    return em_pose_host_001C68C0(&R.host, c->o->rec.bytes, RECORD) < 0
               ? report("001C68C0 faulted on the opening body's record")
               : 0;
}

static int slg_001BA580(void *ctx, const EmSlgScriptActor *a, int32_t key)
{
    (void)a;
    OpeningCall *c = ctx;
    typed_load(c->o);
    if (em_roger_actor_001BA580(&R.ra, &c->o->typed, (uint32_t)key) < 0) return actor_fault("001BA580");
    typed_store(c->o);
    return 0;
}

/* anim_advance_time(actor, the entry's +0x0C) (001C64F0, as Roger's own). */
static int slg_001C64F0(void *ctx, const EmSlgScriptActor *a, uint32_t dt_bits)
{
    (void)a;
    OpeningCall *c = ctx;
    float rate;
    uint32_t flags = 0;
    memcpy(&rate, &dt_bits, 4);
    if (em_player_stage_anim_advance(&R.advance, &c->o->rec, rate, &flags) < 0)
        return report("001C64F0 (anim_advance_time) faulted on the opening body's record");
    return 0;
}

/* 001F9660 (command 6): not in the opening's list (fail-stop). */
static int slg_001F9660(void *ctx, const EmSlgScriptActor *a, int32_t key)
{
    (void)ctx;
    (void)a;
    (void)key;
    return report("001BB0E0 command 6 (001F9660) is not reached by the opening's list");
}

static int slg_001BA540(void *ctx, const EmSlgScriptActor *a)
{
    (void)a;
    OpeningCall *c = ctx;
    typed_load(c->o);
    if (em_roger_actor_001BA540(&R.ra, &c->o->typed) < 0) return actor_fault("001BA540");
    typed_store(c->o);
    return 0;
}

static int slg_001AFC10(void *ctx, const EmSlgScriptActor *a)
{
    (void)a;
    OpeningCall *c = ctx;
    typed_load(c->o);
    return w_001AFC10(NULL, &c->o->typed);
}

/* jalr *(+0x4C): 001CAA00 on the body (the equipment draws from inside
 * 001C5C90, its w_draw). */
static int slg_method_4C(void *ctx, const EmSlgScriptActor *a)
{
    (void)a;
    OpeningCall *c = ctx;
    if (c->o != &c->pair->body) return report("001BB0E0's +0x4C on a record other than the opening body");
    if (c->o->rec.bytes[0x0C] != ROGER_NODES) return report("the opening body's +0x0C is not 21");
    return owner_draw(c->o);
}

int em_area11_roger_opening_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene, const uint8_t *entry,
                                 uint32_t entry_address, EmActor *controller)
{
    if (!actor || !pool || !scene || !entry || !controller || actor->callback != EM_AREA11_ROGER_OPENING_CALLBACK)
        return -1;
    if (ready(pool, scene) < 0) return -1;
    R.cur = &R.pair[1];
    Pair *pair = R.cur;
    /* The entry's command (+0x0A): 0 the body, 5 the equipment node. */
    int16_t command = (int16_t)rd16(entry + 0x0A);
    Owner *o = command == 0 ? &pair->body : command == 5 ? &pair->equip : NULL;
    if (!o) return report("an opening actor whose entry command is neither 0 nor 5");
    int fresh = o->actor != actor || o->generation != actor->generation || o->freed;
    if (owner_for(o, actor) < 0) return -1;
    if (fresh) {
        if (o == &pair->body) pair->ebone_word = 0;
        pair->entry[o == &pair->equip] = entry_address;
        pair->controller = controller;
        /* +0x18 (the pool's prev), +0x20 and +0x24 (001BAC00's stores). */
        wr32(o->rec.bytes + 0x18, em_actor_pool_address(pool, actor->prev));
        wr32(o->rec.bytes + 0x20, entry_address);
        wr32(o->rec.bytes + 0x24, em_actor_pool_address(pool, controller));
    }
    if (pair->entry[o == &pair->equip] != entry_address || pair->controller != controller)
        return report("an opening actor's +0x20 / +0x24 changed");
    if (o == &pair->body && pose_bind(o) < 0) return -1;
    sync_in(o);
    R.ra.world.d00275B40 = (const uint32_t *)(const void *)(o->rec.bytes + 0x110);
    R.ra.world.d00275B40_count = EM_ROGER_ACTOR_MAX_BONES;
    /* The controller's +0x2E (the done mask) as 001BB0E0 reads it. */
    uint8_t owner_bytes[0x30];
    memset(owner_bytes, 0, sizeof owner_bytes);
    wr16(owner_bytes + 0x2E, controller->flags2);
    OpeningCall call = {pair, o, entry};
    const EmSlgScriptActorWorkers w = {&call,        slg_001BAD40, slg_001C5C90, slg_001C68C0, slg_001BA580,
                                       slg_001C64F0, slg_001F9660, slg_001BA540, slg_001AFC10, slg_method_4C};
    const EmSlgScriptActor a = {o->rec.bytes, entry, owner_bytes};
    if (em_slg_001BB0E0(&w, &a) < 0) {
        R.faulted = 1;
        fprintf(stderr, "em_area11 roger: 001BB0E0 faulted (record %08X, lifecycle %u)\n", (unsigned)o->address,
                (unsigned)o->rec.bytes[0x04]);
        return -1;
    }
    if (o->freed) return 0;
    sync_out(o);
    return 1;
}

/* 001AF800 (em_roger_actor_001AF800, its own slot loop) over the record's
 * typed view: its +0x09 slots go back onto the one stack, +0x110.. = 0,
 * +0x09 = +0x0C = 0. */
int em_area11_roger_001AF800(EmActor *actor)
{
    Owner *o = NULL;
    Pair *pair = NULL;
    for (unsigned i = 0; i < 2 && !o; ++i) {
        pair = &R.pair[i];
        o = actor == pair->body.actor ? &pair->body : actor == pair->equip.actor ? &pair->equip : NULL;
    }
    if (!o || o->freed || o->actor->generation != o->generation) return 0;
    sync_in(o);
    typed_load(o);
    if (em_roger_actor_001AF800(&R.ra, &o->typed) < 0) {
        R.faulted = 1;
        return actor_fault("001AF800");
    }
    typed_store(o);
    sync_out(o);
    if (o == &pair->equip) pair->ebone_word = o->typed.bone[0];
    return 1;
}

uint32_t *em_area11_roger_bank_word(const EmActor *actor)
{
    Owner *o = &R.pair[0].body;
    if (!actor || actor != o->actor || o->freed) return NULL;
    return (uint32_t *)(void *)(o->rec.bytes + 0x40);
}

/* 001C67E0(Roger, clip, blend, frame) for the script host (op0B sub 4, op15):
 * the record image is current while Roger's owner call runs the script. */
int em_area11_roger_clip_init(const EmActor *actor, int16_t clip, float blend, float frame)
{
    Owner *o = &R.pair[0].body;
    if (!actor || actor != o->actor || o->freed) return report("001C67E0 on a record other than Roger's");
    sync_in(o);
    if (em_pose_host_001C67E0(&R.host, o->rec.bytes, RECORD, clip, blend, frame) < 0)
        return report("001C67E0 (anim_clip_init) faulted on Roger's record");
    sync_out(o);
    return 0;
}

/* The draw's bank holds views into the export (which stays loaded): the
 * scene's teardown forgets them. */
void em_area11_roger_shutdown(EmGfx *gfx)
{
    (void)gfx;
    memset(&R.bank, 0, sizeof R.bank);
}

static int owner_state(const Owner *o, uint32_t *record, uint8_t header[16], float position[3],
                       uint8_t block[16])
{
    if (!o->actor || o->freed || o->actor->generation != o->generation || !R.pool) return 0;
    uint8_t image[RECORD];
    em_actor_pool_record_image(R.pool, o->actor, image);
    *record = o->address;
    memcpy(header, image, 16);
    memcpy(position, o->actor->pos, 3 * sizeof(float));
    memcpy(block, image + 0x1F0, 16);
    return 1;
}

int em_area11_roger_state(uint32_t *record, uint8_t header[16], float position[3], uint8_t block[16])
{
    return owner_state(&R.pair[0].body, record, header, position, block);
}

int em_area11_roger_equipment_state(uint32_t *record, uint8_t header[16], float position[3])
{
    uint8_t block[16];
    return owner_state(&R.pair[0].equip, record, header, position, block);
}
