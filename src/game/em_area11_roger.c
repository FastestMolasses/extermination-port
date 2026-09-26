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
#include "game/em_face_model.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_opening_face.h"
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

enum {
    TABLE_WORDS = 0xC0,
    MAX_REGIONS = 8,
    ROGER_NODES = 21,
    ROGER_PALETTE = 22,
    EQUIP_PALETTE_MAX = 4,
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
    /* Roger, the equipment */
    Owner roger, equip;
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
    /* the draw */
    int mesh_tried;
    EmModel model;
    EmFaceModel face;
    EmGfxMesh *mesh;
    float palette[ROGER_PALETTE * 16];
    int drawn;
    /* the equipment: 001B1020's services over D_0028A56C, its mesh */
    EmOwnerServices services;
    EmOwnerServicesOwner eview;
    EmOwnerBone ebone;           /* the typed view of its one slot (001C62C0 writes it) */
    uint32_t ebone_word;
    EmOwnerModel emodel;
    EmOwnerSkeletonRecord eskeleton[EQUIP_PALETTE_MAX];
    uint32_t d0028A56C;
    int emesh_tried;
    EmModel emesh_model;
    EmGfxMesh *emesh;
    float epalette[EQUIP_PALETTE_MAX * 16];
    int edrawn;
    int draw_order[2], draw_count;
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
    if (memcmp(data, "EMRS", 4) != 0 || rd32(data + 4) != 1 || rd32(data + 8) != TABLE_ADDRESS ||
        words != TABLE_WORDS || count == 0 || count > MAX_REGIONS || 0x20u + 4u * words > n) {
        free(data);
        return report(EM_AREA11_ROGER_RESOURCES_PATH " is not an EMRS v1 export");
    }
    for (unsigned i = 0; i < TABLE_WORDS; ++i) R.table[i] = rd32(data + 0x20 + 4 * i);
    size_t at = 0x20u + 4u * words;
    for (unsigned i = 0; i < count; ++i) {
        if (at + 16 > n) { free(data); return report("EMRS region header past the file"); }
        uint32_t address = rd32(data + at), bytes = rd32(data + at + 4);
        at += 16;
        if (bytes == 0 || bytes > n - at) { free(data); return report("EMRS region past the file"); }
        R.region[i] = (Region){address, bytes, data + at};
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
        if (map(ctx, R.region[i].address, R.region[i].size, R.region[i].bytes) < 0) return -1;
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
    const uint8_t *r = R.roger.rec.bytes;
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
    uint8_t *r = R.roger.rec.bytes;
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
    sync_out(&R.roger);
}

static int pull(void)
{
    sync_in(&R.roger);
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

static int pose_bind(void)
{
    const EmRogerActorWorld *w = &R.ra.world;
    EmPoseHost *h = &R.host;
    memset(h, 0, sizeof *h);
    for (unsigned i = 0; i < R.regions && h->region_count < EM_POSE_REGION_MAX - 2; ++i)
        h->region[h->region_count++] =
            (EmPoseRegion){R.region[i].address, R.region[i].size, (uint8_t *)R.region[i].bytes, 0};
    h->region[h->region_count++] = (EmPoseRegion){w->slots_base, w->slots_size, w->slots, 1};
    h->region[h->region_count++] = (EmPoseRegion){R.roger.address, RECORD, R.roger.rec.bytes, 1};
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

static int mesh_load(void)
{
    if (R.mesh) return 0;
    if (R.mesh_tried) return -1;
    R.mesh_tried = 1;
    EmGfx *gfx = em_frame_gfx();
    if (em_model_load(&R.model, EM_AREA11_ROGER_MESH_PATH) != 0 || R.model.bone_count != ROGER_PALETTE)
        return report("no valid " EM_AREA11_ROGER_MESH_PATH " (tools/export_roger_resources.py)");
    if (!em_face_model_attach(&R.face, &R.model, EM_AREA11_ROGER_FACE_MESH_PATH,
                              EM_AREA11_ROGER_FACE_MORPH_PATH))
        return report("no valid Roger face " EM_AREA11_ROGER_FACE_MESH_PATH " / .emfm "
                      "(tools/export_opening_media.py's actor export)");
    if (gfx) {
        const EmModel *m = &R.model;
        R.mesh = em_gfx_mesh_create(gfx, m->verts, m->vert_count, m->indices, m->index_count,
                                    (const EmGfxTexDesc *)m->texs, m->tex_count, m->texels, m->flags);
        if (!R.mesh) return report("Roger's mesh could not be created");
    }
    return 0;
}

/* The face slot's weights (+0x40..+0x5F) morph the face vertices
 * (0023C578..0023C5B0, em_opening_face_position). */
static int face_positions(const uint8_t *slot)
{
    if (!R.mesh && !em_frame_gfx()) return 0;   /* headless without a device: nothing to upload */
    if (!R.face.positions || !R.face.deltas) return report("the face morph is not attached");
    float weight[8];
    memcpy(weight, slot + 0x40, sizeof weight);
    for (uint32_t i = 0; i < R.face.count; ++i) {
        size_t vertex = (size_t)R.face.first + i;
        em_opening_face_position(R.face.positions + vertex * 3, R.model.verts + vertex * 10,
                                 R.face.deltas + (size_t)i * 21, weight);
    }
    if (R.mesh && !em_gfx_mesh_update_positions(em_frame_gfx(), R.mesh, R.face.positions, R.model.vert_count))
        return report("the face morph upload failed");
    return 0;
}

/* +0x4C = 001CAA00 on Roger: the mesh at the node world matrices (node
 * +0x90..+0xCF of the +0x110 slots) and, for the model's trailing identity
 * slot, the record's owner matrix +0xD0. */
static int roger_draw(void)
{
    const uint8_t *r = R.roger.rec.bytes;
    if (rd32(r + 0x4C) != METHOD_001CAA00) return report("Roger's +0x4C is not 001CAA00");
    if (mesh_load() < 0) return -1;
    if (r[0x0C] != ROGER_NODES) return report("Roger's +0x0C is not 21");
    for (unsigned i = 0; i < ROGER_NODES; ++i) {
        const uint8_t *node = slot_bytes(rd32(r + 0x110 + 4 * i));
        if (!node) return report("a Roger +0x110 word outside the slot arena");
        memcpy(R.palette + 16 * i, node + 0x90, 64);
    }
    memcpy(R.palette + 16 * ROGER_NODES, r + 0xD0, 64);
    for (unsigned k = 0; k < ROGER_PALETTE * 16; ++k)
        if (!isfinite(R.palette[k])) return report("a Roger node matrix is not finite");
    R.drawn = 1;
    return 0;
}

/* ------------------------------------------- em_roger_actor workers */

static int w_001C63E0(void *ctx, EmRogerActorRecord *actor, int32_t clip)
{
    (void)ctx;
    Owner *o = &R.roger;
    if (actor != &o->typed) return report("001C63E0 on a record other than Roger's");
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

/* 001DA6A0(actor): the drop shadow's EE side and GS draw have no port
 * counterpart for any actor (the player's own post-step is reported the
 * same way, docs/SHADOW_ORIGINAL.md): reported, nothing drawn. */
static int w_001DA6A0(void *ctx, EmRogerActorRecord *actor)
{
    (void)ctx;
    (void)actor;
    return em_scene_bindings_report_001DA6A0();
}

/* 001D0720 (through 001D0C70): the face kernel on the slot at +0x90,
 * em_opening_face_tick over the slot bytes +0x40..+0x5F / +0x70..+0xA7 with
 * the shared 00122BB8 RNG; the morph then follows the new weights. */
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
    EmOpeningFace f;
    memcpy(f.weight, slot + 0x40, sizeof f.weight);
    memcpy(&f.blink_state, slot + 0x70, 4);
    memcpy(&f.blink_wait, slot + 0x74, 4);
    memcpy(&f.expression_state, slot + 0x78, 4);
    memcpy(&f.expression_wait, slot + 0x7C, 4);
    f.talking = slot[0x80];
    f.speed = slot[0x81];
    f.reserved[0] = slot[0x82];
    f.reserved[1] = slot[0x83];
    memcpy(&f.mouth_wait, slot + 0x84, 4);
    memcpy(&f.current_shape, slot + 0x88, 4);
    memcpy(&f.previous_shape, slot + 0x8C, 4);
    memcpy(f.target, slot + 0x90, sizeof f.target);
    em_opening_face_tick(&f, face_random, NULL);
    memcpy(slot + 0x40, f.weight, sizeof f.weight);
    memcpy(slot + 0x70, &f.blink_state, 4);
    memcpy(slot + 0x74, &f.blink_wait, 4);
    memcpy(slot + 0x78, &f.expression_state, 4);
    memcpy(slot + 0x7C, &f.expression_wait, 4);
    slot[0x80] = f.talking;
    slot[0x81] = f.speed;
    slot[0x82] = f.reserved[0];
    slot[0x83] = f.reserved[1];
    memcpy(slot + 0x84, &f.mouth_wait, 4);
    memcpy(slot + 0x88, &f.current_shape, 4);
    memcpy(slot + 0x8C, &f.previous_shape, 4);
    memcpy(slot + 0x90, f.target, sizeof f.target);
    if (mesh_load() < 0) return -1;
    return face_positions(slot);
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

/* 001CA6E0 = 001CA5E0(owner, handle, 0): +0x44, then 001CA5F0 kind 0:
 * +0x4C = 001CAA00 (em_roger_actor_001CA6E0 on the typed record). The
 * owner-services view's model is the parsed skeleton of the handle. */
static int e_001CA6E0(void *ctx, EmOwnerServicesOwner *owner, uint32_t handle)
{
    (void)ctx;
    Owner *o = &R.equip;
    if (owner != &R.eview) return report("001CA6E0 on a view other than the equipment's");
    if (em_roger_actor_001CA6E0(&R.ra, &o->typed, handle) < 0) return actor_fault("001CA6E0");
    const uint8_t *m = em_area11_roger_resource(handle, 0x40);
    if (!m) return report("001CA6E0: the equipment model is not in the export");
    uint32_t bones = rd32(m + 8), skeleton = rd32(m + 0xC);
    if (bones == 0 || bones > EQUIP_PALETTE_MAX - 1) return report("the equipment model's bone count");
    const uint8_t *s = em_area11_roger_resource(handle + skeleton, 0x50u * bones);
    if (!s) return report("the equipment model's skeleton is not in the export");
    memset(&R.emodel, 0, sizeof R.emodel);
    R.emodel.bone_count = (uint8_t)bones;
    memcpy(&R.emodel.radius, m + 0x20, 4);
    for (uint32_t k = 0; k < bones; ++k) {
        R.eskeleton[k].parent = (int16_t)rd16(s + 0x50 * k + 4);
        memcpy(R.eskeleton[k].bind, s + 0x50 * k + 0x10, 64);
    }
    R.emodel.skeleton = R.eskeleton;
    R.emodel.skeleton_records = bones;
    owner->model = &R.emodel;
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
    if (R.ebone_word) return report("the equipment popped a second bone slot");
    if (!slot_bytes(word)) return report("001AF780 returned a word outside the slot arena");
    R.ebone_word = word;
    memset(&R.ebone, 0, sizeof R.ebone);
    *slot = &R.ebone;
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
    uint8_t *s = slot_bytes(R.ebone_word);
    if (!s) return report("the equipment's slot is outside the arena");
    memcpy(s + 0x00, R.ebone.bind, 64);
    wr16(s + 0x64, (uint16_t)R.ebone.parent);
    memcpy(s + 0x70, R.ebone.rot, 12);
    memcpy(s + 0x7C, R.ebone.trans, 12);
    for (unsigned k = 0; k < 3; ++k) wr16(s + 0x88 + 2 * k, (uint16_t)R.ebone.scale[k]);
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
    Owner *o = &R.equip;
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
        if (v->bones_held != 1 || v->bone[0] != &R.ebone) return report("the equipment's bone slots");
        actor->bone[0] = R.ebone_word;
        if (ebone_store() < 0) return -1;
    }
    *result = r;
    return 0;
}

static int emesh_load(void)
{
    if (R.emesh) return 0;
    if (R.emesh_tried) return -1;
    R.emesh_tried = 1;
    if (em_model_load(&R.emesh_model, EM_AREA11_EQUIPMENT_MESH_PATH) != 0 ||
        R.emesh_model.bone_count == 0 || R.emesh_model.bone_count > EQUIP_PALETTE_MAX)
        return report("no valid " EM_AREA11_EQUIPMENT_MESH_PATH " (the opening actor export)");
    EmGfx *gfx = em_frame_gfx();
    if (gfx) {
        const EmModel *m = &R.emesh_model;
        R.emesh = em_gfx_mesh_create(gfx, m->verts, m->vert_count, m->indices, m->index_count,
                                     (const EmGfxTexDesc *)m->texs, m->tex_count, m->texels, m->flags);
        if (!R.emesh) return report("the equipment mesh could not be created");
    }
    return 0;
}

/* +0x4C = 001CAA00 on the equipment: its mesh at the bone-0 world matrix
 * (slot +0x90, 001C5C90's copy of Roger's bone 1). The export's vertices
 * are in bone 0's space and all use bone 0 (its frame palettes are the
 * opening's baked world track, not a rest pose, and its second matrix is
 * the exporter's trailing slot): every palette entry is that matrix. */
static int w_draw(void *ctx, EmRogerActorRecord *actor)
{
    (void)ctx;
    if (actor != &R.equip.typed || actor->draw != METHOD_001CAA00) return report("the equipment's +0x4C");
    const uint8_t *slot = slot_bytes(actor->bone[0]);
    if (!slot) return report("the equipment draws without its bone slot");
    if (emesh_load() < 0) return -1;
    for (uint32_t i = 0; i < R.emesh_model.bone_count; ++i) memcpy(R.epalette + 16 * i, slot + 0x90, 64);
    for (unsigned k = 0; k < R.emesh_model.bone_count * 16u; ++k)
        if (!isfinite(R.epalette[k])) return report("the equipment's bone matrix is not finite");
    R.edrawn = 1;
    return 0;
}

static int w_001AFC10(void *ctx, EmRogerActorRecord *actor)
{
    (void)ctx;
    Owner *o = actor == &R.equip.typed ? &R.equip : actor == &R.roger.typed ? &R.roger : NULL;
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
    int rc = em_area11_script_host_start(R.roger.actor, entry);
    if (pull() < 0) return -1;
    return rc < 0 ? -1 : 1;
}

/* 001BA1F0: 1 (finished) and 3 (the skip path) both end the script. */
static int h_script_tick(void *ctx)
{
    (void)ctx;
    int32_t result = 0;
    push();
    int rc = em_area11_script_host_tick(R.roger.actor, &result);
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
    if (em_pose_host_001C67E0(&R.host, R.roger.rec.bytes, RECORD, (int16_t)clip, blend, start) < 0)
        return report("001C67E0 (anim_clip_init) faulted on Roger's record");
    return view_load() < 0 ? 0 : 1;
}

static int h_animation_tick(void *ctx, float rate, uint16_t *result)
{
    (void)ctx;
    uint32_t flags = 0;
    view_store();
    if (em_player_stage_anim_advance(&R.advance, &R.roger.rec, rate, &flags) < 0)
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
    const EmActor *a = R.roger.actor;
    v.drawn = a->drawn;
    v.cls = a->cls;
    v.kind = a->model;
    v.lifecycle = a->u04[0];
    v.model_id = a->param;
    v.flags2 = a->flags2;
    memcpy(v.pos, a->pos, sizeof v.pos);
    memcpy(v.rot, a->rot, sizeof v.rot);
    int r = em_area11_interaction_host_offer_001B17A0(R.roger.actor, &v);
    if (pull() < 0) return -1;
    return r;
}

static int h_event(void *ctx, EmRogerEvent type, unsigned argument)
{
    (void)ctx;
    Owner *o = &R.roger;
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
    case EM_ROGER_REMOVE_GROUP:        /* 001B0C60(1, 0, 4) */
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
    Owner *o = &R.roger;
    if (o->actor && !o->freed && o->actor->generation == o->generation && address >= o->address &&
        size <= RECORD && address - o->address <= RECORD - size) {
        sync_in(o);
        return o->rec.bytes + (address - o->address);
    }
    return (uint8_t *)(uintptr_t)em_area11_roger_resource(address, size);
}

const uint8_t *em_area11_roger_record_bytes(uint32_t address, uint32_t size)
{
    Owner *o = &R.roger;
    if (!o->actor || o->freed || o->actor->generation != o->generation || address < o->address ||
        size > RECORD || address - o->address > RECORD - size)
        return NULL;
    sync_in(o);
    return o->rec.bytes + (address - o->address);
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
    Owner *o = &R.roger;
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
    memset(&R.roger, 0, sizeof R.roger);
    memset(&R.equip, 0, sizeof R.equip);
    memset(&R.ra, 0, sizeof R.ra);
    R.ebone_word = 0;
    R.drawn = R.edrawn = 0;
    R.draw_count = 0;
    R.faulted = 0;
}

int em_area11_roger_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene)
{
    if (!actor || !pool || !scene || actor->callback != EM_AREA11_ROGER_CALLBACK) return -1;
    if (ready(pool, scene) < 0) return -1;
    Owner *o = &R.roger;
    if (owner_for(o, actor) < 0) return -1;
    if (pose_bind() < 0) return -1;
    sync_in(o);
    R.drawn = 0;
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
    Owner *o = &R.equip;
    if (owner_for(o, actor) < 0) return -1;
    Owner *parent = &R.roger;
    if (!parent->actor || parent->freed || em_actor_pool_address(pool, actor->prev) != parent->address)
        return report("001C5C90: +0x18 is not Roger's record");
    sync_in(o);
    typed_load(o);
    typed_load(parent);
    R.edrawn = 0;
    R.ra.world.d00275B40 = (const uint32_t *)(const void *)(o->rec.bytes + 0x110);
    R.ra.world.d00275B40_count = EM_ROGER_ACTOR_MAX_BONES;
    int r = em_roger_actor_001C5C90(&R.ra, &o->typed, &parent->typed);
    if (r < 0) {
        R.faulted = 1;
        return actor_fault("001C5C90");
    }
    if (o->freed) return 0;
    typed_store(o);
    sync_out(o);
    return 1;
}

int em_area11_roger_001AF800(EmActor *actor)
{
    Owner *o = actor == R.roger.actor ? &R.roger : actor == R.equip.actor ? &R.equip : NULL;
    if (!o || o->freed || o->actor->generation != o->generation) return 0;
    for (unsigned j = 0; j < actor->bones; ++j) {
        if (em_roger_actor_001AF890(&R.ra, rd32(o->rec.bytes + 0x110 + 4 * j)) < 0)
            return actor_fault("001AF800 -> 001AF890");
        if (o == &R.equip && j == 0) R.ebone_word = 0;
    }
    return 1;
}

uint32_t *em_area11_roger_bank_word(const EmActor *actor)
{
    if (!actor || actor != R.roger.actor || R.roger.freed) return NULL;
    return (uint32_t *)(void *)(R.roger.rec.bytes + 0x40);
}

/* 001C67E0(Roger, clip, blend, frame) for the script host (op0B sub 4, op15):
 * the record image is current while Roger's owner call runs the script. */
int em_area11_roger_clip_init(const EmActor *actor, int16_t clip, float blend, float frame)
{
    if (!actor || actor != R.roger.actor || R.roger.freed) return report("001C67E0 on a record other than Roger's");
    sync_in(&R.roger);
    if (em_pose_host_001C67E0(&R.host, R.roger.rec.bytes, RECORD, clip, blend, frame) < 0)
        return report("001C67E0 (anim_clip_init) faulted on Roger's record");
    sync_out(&R.roger);
    return 0;
}

/* ------------------------------------------------------------ the draw list */

int em_area11_roger_draw_count(void)
{
    R.draw_count = 0;
    if (R.drawn && R.mesh && R.roger.actor && !R.roger.freed) R.draw_order[R.draw_count++] = 0;
    if (R.edrawn && R.emesh && R.equip.actor && !R.equip.freed) R.draw_order[R.draw_count++] = 1;
    return R.draw_count;
}

int em_area11_roger_draw(int i, EmGfxMesh **mesh, const float **palette, uint32_t *bone_count,
                         uint8_t *anchor_bone, uint8_t *cam_fill, uint8_t *face)
{
    if (i < 0 || i >= R.draw_count || !mesh || !palette || !bone_count || !anchor_bone || !cam_fill || !face)
        return 0;
    const Owner *o = R.draw_order[i] == 0 ? &R.roger : &R.equip;
    const uint8_t pose_bone = o->rec.bytes[0x98];
    if (R.draw_order[i] == 0) {
        *mesh = R.mesh;
        *palette = R.palette;
        *bone_count = ROGER_PALETTE;
        *face = 1;
    } else {
        *mesh = R.emesh;
        *palette = R.epalette;
        *bone_count = R.emesh_model.bone_count;
        *face = 0;
    }
    *anchor_bone = pose_bone < *bone_count ? pose_bone : 0;   /* 0xFF: the owner's +0xB0 cull, bone 0 */
    *cam_fill = (o->rec.bytes[0x02] & 0x20) != 0;
    return 1;
}

void em_area11_roger_shutdown(EmGfx *gfx)
{
    if (R.mesh && gfx) em_gfx_mesh_destroy(gfx, R.mesh);
    if (R.emesh && gfx) em_gfx_mesh_destroy(gfx, R.emesh);
    R.mesh = R.emesh = NULL;
    em_face_model_free(&R.face);
    em_model_free(&R.model);
    em_model_free(&R.emesh_model);
    R.mesh_tried = R.emesh_tried = 0;
    R.draw_count = 0;
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
    return owner_state(&R.roger, record, header, position, block);
}

int em_area11_roger_equipment_state(uint32_t *record, uint8_t header[16], float position[3])
{
    uint8_t block[16];
    return owner_state(&R.equip, record, header, position, block);
}
