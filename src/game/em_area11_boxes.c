/* em_area11_boxes.c - the AREA11 crates and drums on their original owners
 * (census L25). See em_area11_boxes.h and docs/CRATES_DRUMS_ORIGINAL.md
 * "Binding". */
#include "game/em_area11_boxes.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_model.h"
#include "game/em_actor_collision.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_area11_roger.h"
#include "game/em_area11_script_host.h"
#include "game/em_collision_world.h"
#include "game/em_crate_original.h"
#include "game/em_drum_original.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_owner_draw_original.h"
#include "game/em_owner_services_original.h"
#include "game/em_pad_actuator.h"
#include "game/em_player.h"
#include "game/em_random.h"
#include "game/em_roger_actor_original.h"
#include "game/em_startup_load_gaps.h"
#include "game/em_sfx.h"
#include "game/em_truck_original.h"

enum {
    BOX_MAX = 8,                  /* AREA11 places four crates and two drums */
    BONE_SLOTS = EM_SLG_BONE_SLOTS,
    DRAW_BONES = 4,
    RATTLE_ROWS = 7
};
#define CRATE_CALLBACK 0x001551B0u
#define DRUM_CALLBACK 0x00156620u
#define TRUCK_CALLBACK 0x00823FF0u
#define TRIGGER_CALLBACK 0x008251E0u
#define METHOD_001CAA00 0x001CAA00u
#define TABLES_BASE 0x002468B0u
#define TABLES_SIZE 0x170u
#define RATTLE_AT 0x002468B0u
#define SPEED_AT 0x00246A00u
#define LIFT_AT 0x00246A10u

typedef struct {
    EmActor *actor;
    uint32_t generation;  /* the record's generation when the slot was taken */
    int drum;
    int truck;            /* 00823FF0 (census L23) */
    EmCrateOriginal crate;
    EmDrumOriginal drum_state;
    EmTruckOriginal truck_state;
    EmOwnerServicesOwner view;  /* +0x09, +0x0C, +0x44, +0x60, +0xD0, +0x110 */
    uint32_t method;      /* +0x4C: 001CA5F0 kind 0 = 001CAA00 */
    int drawn;            /* +0x4C ran in the last owner call */
    float palette[DRAW_BONES * 16];
    EmActorPool *pool;
    EmSceneState *scene;
    int freed;
} Box;

/* A box's mesh: the legacy actor-draw EMDL (the P1/P2 object kernel stays
 * with RENDER, docs/OWNER_DRAW.md). */
typedef struct {
    int tried;
    EmModel model;
    EmGfxMesh *mesh;
    float base[DRAW_BONES * 16];
} BoxMesh;

static struct {
    Box box[BOX_MAX];
    /* 001AF710's bone-slot stack (em_slg_001AF710): the D_007D5840 records,
     * the D_007D4640 address words, the cursor D_00275BD0 and the count
     * D_00275BCC. 001AF780 / 001AF890 work on it through em_roger_actor's
     * translations. `slots` is the owner services' typed view of the same
     * 0xD0-byte records: a slot is cleared in both whenever the original
     * clears it. */
    uint8_t records[EM_SLG_BONE_SLOTS * EM_SLG_BONE_SLOT_SIZE];
    EmSlgBoneSlots bones;
    EmRogerActor stack;
    EmOwnerBone slots[BONE_SLOTS];
    EmOwnerServices services;
    /* 0x70003400: 001C9610's staging matrix (written before it is read
     * inside the one call; no reader across calls). */
    EmOwnerServicesScratch spr;
    /* The exported model bank and *D_0028A59C (its table address). */
    uint8_t *bank_file;
    EmWorldModels bank;
    uint32_t bank_word;
    int bank_tried;
    /* D_002468B0 (7 rattle rows), D_00246A00 / D_00246A10. */
    int tables_tried, tables_loaded;
    float rattle[RATTLE_ROWS][4][3];
    float speed[4], lift[4];
    BoxMesh crate_mesh, drum_mesh, truck_mesh;
    /* 0x700031F0: set to 1 by the truck's carry (00825014), cleared by the
     * player stage 0015BCF0 at its start, ORed into the camera block's +0x8B
     * by the camera frame 0018B9C0 (em_camera_live.c). */
    int32_t carry31F0;
    /* The truck's 001EFD20 spawns that reached the counted effect gap. */
    unsigned effect_gap;
    int draw_order[BOX_MAX];
    int draw_count;
    unsigned reported;
} S;

static int report(const char *what)
{
    fprintf(stderr, "em_area11 boxes: %s\n", what);
    return -1;
}

/* A worker the live path cannot reach (see the header): fail-stop. */
static int unbound(const char *what)
{
    fprintf(stderr, "em_area11 boxes: %s is not bound (fail-stop)\n", what);
    return -1;
}

static Box *box_of_view(EmOwnerServicesOwner *view)
{
    return view ? (Box *)((char *)view - offsetof(Box, view)) : NULL;
}

/* ------------------------------------------------------------ the exports */

static int load_bank(void)
{
    if (S.bank.model_count) return 0;
    if (S.bank_tried) return -1;
    S.bank_tried = 1;
    FILE *f = fopen(EM_AREA11_WORLD_MODELS_PATH, "rb");
    if (!f)
        return report("no " EM_AREA11_WORLD_MODELS_PATH " (export it with tools/export_world_models.py)");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    S.bank_file = size > 0 ? malloc((size_t)size) : NULL;
    int ok = S.bank_file && fread(S.bank_file, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!ok || em_world_models_parse(&S.bank, S.bank_file, (size_t)size) != 0) {
        free(S.bank_file);
        S.bank_file = NULL;
        memset(&S.bank, 0, sizeof S.bank);
        return report(EM_AREA11_WORLD_MODELS_PATH " is not a valid model bank export");
    }
    S.bank_word = S.bank.table_address;
    return 0;
}

static float f32_at(const uint8_t *p)
{
    float v;
    memcpy(&v, p, sizeof v);
    return v;
}

static int load_tables(void)
{
    if (S.tables_loaded) return 0;
    if (S.tables_tried) return -1;
    S.tables_tried = 1;
    FILE *f = fopen(EM_AREA11_BOX_TABLES_PATH, "rb");
    uint8_t buf[16 + TABLES_SIZE];
    size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
    if (f) fclose(f);
    uint32_t head[4];
    if (n == sizeof buf) memcpy(head, buf, sizeof head);
    if (n != sizeof buf || memcmp(buf, "EMRG", 4) != 0 || head[1] != 1 || head[2] != TABLES_BASE ||
        head[3] != TABLES_SIZE)
        return report("no valid " EM_AREA11_BOX_TABLES_PATH " (export it with tools/export_box_tables.py)");
    const uint8_t *span = buf + 16;
    for (unsigned row = 0; row < RATTLE_ROWS; ++row)
        for (unsigned col = 0; col < 4; ++col)
            for (unsigned k = 0; k < 3; ++k)
                S.rattle[row][col][k] = f32_at(span + (RATTLE_AT - TABLES_BASE) + row * 0x30 + col * 0xC + 4 * k);
    for (unsigned i = 0; i < 4; ++i) {
        S.speed[i] = f32_at(span + (SPEED_AT - TABLES_BASE) + 4 * i);
        S.lift[i] = f32_at(span + (LIFT_AT - TABLES_BASE) + 4 * i);
    }
    S.tables_loaded = 1;
    return 0;
}

/* The legacy actor-draw mesh of a box kind: the scene's own
 * props/enemy_crate.emdl or assets/enemy_crate.emdl for the crates,
 * assets/enemy_egg.emdl for the drums (the paths em_enemy.c loads), and the
 * truck's props/area_truck.emdl (per-area model 9, the legacy static truck's
 * mesh). */
static BoxMesh *mesh_of(const Box *b)
{
    return b->truck ? &S.truck_mesh : b->drum ? &S.drum_mesh : &S.crate_mesh;
}

static BoxMesh *mesh_for(const Box *b)
{
    BoxMesh *m = mesh_of(b);
    if (m->mesh) return m;
    if (m->tried) return NULL;
    m->tried = 1;
    static const char *const crate_paths[] = {"assets/scene_snow/props/enemy_crate.emdl",
                                              "assets/enemy_crate.emdl"};
    static const char *const drum_paths[] = {"assets/enemy_egg.emdl"};
    static const char *const truck_paths[] = {EM_AREA11_TRUCK_MESH_PATH};
    const int drum = b->drum;
    const char *const *paths = b->truck ? truck_paths : drum ? drum_paths : crate_paths;
    size_t count = b->truck || drum ? 1 : 2;
    for (size_t i = 0; i < count; ++i) {
        FILE *probe = fopen(paths[i], "rb");
        if (!probe) continue;
        fclose(probe);
        if (em_model_load(&m->model, paths[i]) != 0) continue;
        if (m->model.bone_count == 0 || m->model.bone_count > DRAW_BONES) {
            em_model_free(&m->model);
            continue;
        }
        EmGfx *gfx = em_frame_gfx();
        m->mesh = gfx ? em_gfx_mesh_create(gfx, m->model.verts, m->model.vert_count, m->model.indices,
                                           m->model.index_count, (const EmGfxTexDesc *)m->model.texs,
                                           m->model.tex_count, m->model.texels, m->model.flags)
                      : NULL;
        if (!m->mesh) {
            em_model_free(&m->model);
            continue;
        }
        em_model_palette_at(&m->model, 0, 0.0, m->base);
        return m;
    }
    fprintf(stderr, "em_area11 boxes: no %s mesh; the %s not drawn\n",
            b->truck ? EM_AREA11_TRUCK_MESH_PATH : drum ? "assets/enemy_egg.emdl" : "enemy_crate.emdl",
            b->truck ? "truck is" : drum ? "drums are" : "crates are");
    return NULL;
}

/* ------------------------------------------- owner services workers */

static int w_001C6120(void *ctx, uint32_t bank, uint32_t id, uint32_t *handle)
{
    (void)ctx;
    return em_world_models_001C6120(&S.bank, bank, id, handle) < 0 ? -1 : 0;
}

/* 001CA6E0 = 001CA5E0(owner, handle, 0): +0x44 = the model, then 001CA5F0
 * kind 0: +0x4C = 001CAA00. */
static int w_001CA6E0(void *ctx, EmOwnerServicesOwner *owner, uint32_t handle)
{
    (void)ctx;
    Box *b = box_of_view(owner);
    EmRogerActorRecord record;
    memset(&record, 0, sizeof record);
    if (!b || em_roger_actor_001CA6E0(&S.stack, &record, handle) < 0) return -1;
    const EmWorldModel *m = em_world_models_at(&S.bank, record.model);
    if (!m) return -1;
    owner->model = &m->model;   /* +0x44 */
    b->method = record.draw;    /* +0x4C */
    return 0;
}

static int roger_fault(const char *where)
{
    fprintf(stderr, "em_area11 boxes: %s: bone-slot fault %08X code %d\n", where,
            (unsigned)S.stack.fault.address, (int)S.stack.fault.code);
    return -1;
}

/* A slot record's address word <-> its typed view. */
static EmOwnerBone *slot_of(uint32_t word)
{
    if (word < EM_SLG_BONE_RECORDS) return NULL;
    uint32_t at = word - EM_SLG_BONE_RECORDS;
    if (at % EM_SLG_BONE_SLOT_SIZE || at / EM_SLG_BONE_SLOT_SIZE >= BONE_SLOTS) return NULL;
    return &S.slots[at / EM_SLG_BONE_SLOT_SIZE];
}

static uint32_t word_of(const EmOwnerBone *slot)
{
    return EM_SLG_BONE_RECORDS + (uint32_t)(slot - S.slots) * EM_SLG_BONE_SLOT_SIZE;
}

/* 001AF780 (em_roger_actor_001AF780): the slot at the cursor, or 0 while
 * fewer than 31 are free. */
static int w_001AF780(void *ctx, EmOwnerBone **slot)
{
    (void)ctx;
    uint32_t word = 0;
    if (em_roger_actor_001AF780(&S.stack, &word) < 0) return roger_fault("001AF780");
    if (!word) {
        *slot = NULL;
        return 0;
    }
    if (!(*slot = slot_of(word))) return report("001AF780 returned a word outside the slot records");
    return 0;
}

/* 001AF890 (em_roger_actor_001AF890): the slot is cleared and pushed back. */
static int push_001AF890(EmOwnerBone *slot)
{
    if (!slot || slot < S.slots || slot >= S.slots + BONE_SLOTS) return -1;
    if (em_roger_actor_001AF890(&S.stack, word_of(slot)) < 0) return roger_fault("001AF890");
    memset(slot, 0, sizeof *slot);
    return 0;
}

/* anim_bone_array_setup (001CB5B0): D_00275B40 = D_00275B48 + 0x110. The
 * walk's 001CB590 points D_00275B48 at the ticking node, so D_00275B40 is
 * that node's own +0x110 slots, which the bone_matrix worker writes. */
static int w_anim_bone_array_setup(void *ctx, uint8_t count)
{
    (void)ctx;
    (void)count;
    return 0;
}

static int services_fault(const char *where)
{
    if (!S.services.fault.code) return 0;
    fprintf(stderr, "em_area11 boxes: %s: owner services fault %08X code %d\n", where,
            (unsigned)S.services.fault.address, (int)S.services.fault.code);
    return -1;
}

/* ------------------------------------------------ record <-> owner state */

static void put_floats(uint8_t *scratch, unsigned at, const float *v, unsigned n)
{
    memcpy(scratch + (at - 0x1F0), v, n * sizeof *v);
}

static void get_floats(const uint8_t *scratch, unsigned at, float *v, unsigned n)
{
    memcpy(v, scratch + (at - 0x1F0), n * sizeof *v);
}

static void crate_load(Box *b)
{
    const EmActor *a = b->actor;
    EmCrateOriginal *c = &b->crate;
    c->status = a->status;
    c->model = a->model;
    c->state = a->u04[0];
    c->fall_phase = a->u04[1];
    c->break_phase = a->u04[3];
    c->alarm = a->u0A[0];
    c->puid = a->table_index;
    c->placement = a->uid;
    c->damage = (int16_t)a->h36;
    c->raised = a->h52;
    c->link = (int16_t)a->link;
    memcpy(c->position, a->pos, sizeof c->position);
    memcpy(c->rotation, a->rot, sizeof c->rotation);
    get_floats(a->scratch, 0x1F0, c->rest, 16);
    get_floats(a->scratch, 0x230, c->step, 16);
    get_floats(a->scratch, 0x2B8, c->spin, 3);
    get_floats(a->scratch, 0x2C4, c->velocity, 3);
    get_floats(a->scratch, 0x2D0, c->probe, 8);
}

static void crate_store(Box *b)
{
    EmActor *a = b->actor;
    const EmCrateOriginal *c = &b->crate;
    a->status = c->status;
    a->model = c->model;
    a->u04[0] = c->state;
    a->u04[1] = c->fall_phase;
    a->u04[3] = c->break_phase;
    a->u0A[0] = c->alarm;
    a->table_index = c->puid;
    a->uid = c->placement;
    a->h36 = (uint16_t)c->damage;
    a->h52 = c->raised;
    a->link = (uint16_t)c->link;
    memcpy(a->pos, c->position, sizeof a->pos);
    memcpy(a->rot, c->rotation, sizeof a->rot);
    put_floats(a->scratch, 0x1F0, c->rest, 16);
    put_floats(a->scratch, 0x230, c->step, 16);
    put_floats(a->scratch, 0x2B8, c->spin, 3);
    put_floats(a->scratch, 0x2C4, c->velocity, 3);
    put_floats(a->scratch, 0x2D0, c->probe, 8);
}

static void drum_load(Box *b)
{
    const EmActor *a = b->actor;
    EmDrumOriginal *d = &b->drum_state;
    d->status = a->status;
    d->visible = a->drawn;
    d->model = a->model;
    d->state = a->u04[0];
    d->phase = a->u04[1];
    d->damage = (int16_t)a->h36;
    memcpy(d->position, a->pos, sizeof d->position);
    memcpy(d->rotation, a->rot, sizeof d->rotation);
    get_floats(a->scratch, 0x200, d->origin, 4);
    get_floats(a->scratch, 0x210, d->origin_rotation, 4);
    get_floats(a->scratch, 0x264, &d->heading, 1);
    get_floats(a->scratch, 0x268, &d->lift, 1);
}

static void drum_store(Box *b)
{
    EmActor *a = b->actor;
    const EmDrumOriginal *d = &b->drum_state;
    a->status = d->status;
    a->drawn = d->visible;
    a->model = d->model;
    a->u04[0] = d->state;
    a->u04[1] = d->phase;
    a->h36 = (uint16_t)d->damage;
    memcpy(a->pos, d->position, sizeof a->pos);
    memcpy(a->rot, d->rotation, sizeof a->rot);
    put_floats(a->scratch, 0x200, d->origin, 4);
    put_floats(a->scratch, 0x210, d->origin_rotation, 4);
    put_floats(a->scratch, 0x264, &d->heading, 1);
    put_floats(a->scratch, 0x268, &d->lift, 1);
}

/* The owner-services view of the record: +0x02, +0x03, +0x04, +0x0D, +0x2E,
 * +0x60 and +0xB0/+0xC0 (the owner's current values). */
static void view_sync(Box *b)
{
    const EmActor *a = b->actor;
    EmOwnerServicesOwner *v = &b->view;
    v->cls = a->cls;
    v->model_id = a->param;
    v->flags2 = a->flags2;
    memcpy(v->scale, a->f60, sizeof v->scale);
    if (b->truck) {
        /* The truck's record: +0x03, +0x04, +0xB0 and the whole +0xC0 (the
         * truck module reads only +0xC0's X, 001C6380 all three). */
        v->kind = a->model;
        v->lifecycle = b->truck_state.state;
        memcpy(v->pos, b->truck_state.position, sizeof b->truck_state.position);
        v->pos[3] = a->pos[3];
        memcpy(v->rot, a->rot, sizeof v->rot);
        v->rot[0] = b->truck_state.rotation_x;
        return;
    }
    v->kind = b->drum ? b->drum_state.model : b->crate.model;
    v->lifecycle = b->drum ? b->drum_state.state : b->crate.state;
    memcpy(v->pos, b->drum ? b->drum_state.position : b->crate.position, sizeof v->pos);
    memcpy(v->rot, b->drum ? b->drum_state.rotation : b->crate.rotation, sizeof v->rot);
}

/* ---------------------------------------------------- the owner workers */

/* 001B0EA0(self): 1 when the bone cap refused (+0x04 = 3), else 0. */
static int h_allocate(void *ctx)
{
    Box *b = ctx;
    if (load_bank() < 0) return -1;
    view_sync(b);
    int r = em_owner_services_001B0EA0(&S.services, &b->view);
    if (r < 0 || services_fault("001B0EA0") < 0) return -1;
    if (b->drum) b->drum_state.state = b->view.lifecycle;
    else b->crate.state = b->view.lifecycle;
    b->actor->bones = b->view.bones_held;    /* +0x09 */
    b->actor->u0A[2] = b->view.bone_count;   /* +0x0C = 001C6150(model) */
    return r;
}

static int h_bone_init(void *ctx)
{
    Box *b = ctx;
    int r = em_owner_services_001C62C0(&S.services, &b->view);
    return r < 0 || services_fault("001C62C0") < 0 ? -1 : 0;
}

/* 001C6380(self): +0xD0 = TRS(+0xB0, +0xC0, +0x60), then 001C9610 over the
 * bone slots. */
static int h_place(void *ctx, float world[16])
{
    Box *b = ctx;
    view_sync(b);
    int r = em_owner_services_001C6380(&S.services, &b->view);
    if (r < 0 || services_fault("001C6380") < 0) return -1;
    memcpy(world, b->view.world, sizeof b->view.world);
    return 0;
}

static int h_publish(void *ctx)
{
    Box *b = ctx;
    return em_collision_world_publish_001B1B70(b->actor) < 0 ? -1 : 0;
}

static int h_probe(void *ctx, float position[4], const float from[3], float dy, uint32_t mode,
                   EmCrateProbe *out)
{
    Box *b = ctx;
    EmActorCollisionOwner o = {em_collision_world_cells(), NULL, b->actor, b->pool};
    if (!o.world) return -1;
    return em_actor_collision_owner_probe(&o, position, from, dy, mode, out) < 0 ? -1 : 0;
}

static int h_random(void *ctx, uint32_t *r)
{
    (void)ctx;
    *r = em_random_next();
    return 0;
}

/* 001FBD50(self, id, 0, radius) as the items' take cue plays it. */
static int h_sound3d(void *ctx, uint16_t id, int32_t mode, float radius)
{
    Box *b = ctx;
    if (mode != 0) return unbound("001FBD50 with a nonzero a2 (the flat cue)");
    em_sfx_play_at(id, b->actor->pos, radius);
    return 0;
}

/* *D_00275B40 + 0x90: the ticking node's first bone slot's +0x90 matrix
 * (copy_qw4). */
static int h_bone_matrix(void *ctx, const float world[16])
{
    Box *b = ctx;
    if (!b->view.bone[0]) return report("*D_00275B40 + 0x90 without a bone slot");
    em_owner_services_copy_qw4_00102958(b->view.bone[0]->world, world);
    return 0;
}

/* +0x4C: 001CAA00, drawn through the actor draw chain as the legacy crates
 * and drums were: the whole rigid EMDL (its rest palette carries the
 * per-node offsets) placed by the owner's root bone matrix (bone slot 0's
 * +0x90, which 001C9610 / the bone_matrix worker wrote), palette i = that
 * matrix times rest node i. */
static int h_draw(void *ctx)
{
    Box *b = ctx;
    if (b->method != METHOD_001CAA00) return report("+0x4C is not 001CAA00");
    BoxMesh *m = mesh_for(b);
    if (!m) return 0;
    const EmOwnerBone *root = b->view.bone_count ? b->view.bone[0] : NULL;
    if (!root) return report("+0x4C without a bone slot");
    const float *w = root->world;
    for (uint32_t i = 0; i < m->model.bone_count; ++i) {
        const float *base = m->base + 16 * i;
        float *out = b->palette + 16 * i;
        for (unsigned col = 0; col < 4; ++col)
            for (unsigned row = 0; row < 4; ++row)
                out[col * 4 + row] = w[0 * 4 + row] * base[col * 4 + 0] + w[1 * 4 + row] * base[col * 4 + 1] +
                                     w[2 * 4 + row] * base[col * 4 + 2] + w[3 * 4 + row] * base[col * 4 + 3];
    }
    b->drawn = 1;
    return 0;
}

/* 001AFC10(self): the record's bone slots go back through 001AF800 (the
 * pool's worker) and the node is freed. */
static int h_free(void *ctx)
{
    Box *b = ctx;
    if (em_actor_pool_free_001AFC10(b->pool, b->scene, b->actor) < 0) return -1;
    b->freed = 1;
    return 0;
}

static int h_contact(void *ctx)
{
    Box *b = ctx;
    return em_collision_world_push4_001B1D20(b->actor) < 0 ? -1 : 0;
}

/* 001B17A0(self) through the interaction host's services; +0x01 = the
 * 001B1630 byte. */
static int h_visibility(void *ctx, uint8_t *visible)
{
    Box *b = ctx;
    view_sync(b);
    int r = em_area11_interaction_host_offer_001B17A0(b->actor, &b->view);
    if (r < 0) return -1;
    *visible = b->view.drawn;
    return 0;
}

static int h_hull(void *ctx, const float world[16])
{
    Box *b = ctx;
    return em_collision_world_retransform_001A2370(b->actor, world) < 0 ? -1 : 0;
}

/* Reached only after a damage write or on a nest-group path (header). */
static int h_sound(void *c, uint16_t id)
{ (void)c; (void)id; return unbound("001FC580 (the owner's sound)"); }
static int h_effect(void *c, uint32_t id, const float p[4], const float r[4])
{ (void)c; (void)id; (void)p; (void)r; return unbound("001EFD90 (effect spawn)"); }
static int h_taken(void *c, uint8_t puid)
{ (void)c; (void)puid; return unbound("001B11E0 (the nest group's taken bits)"); }
static int h_spawn(void *c, const EmCrateChild *child)
{ (void)c; (void)child; return unbound("001AFA90 and the nest child copy"); }
static int h_rebind(void *c, uint16_t model)
{ (void)c; (void)model; return unbound("the husk rebind 001C6120(D_0028A56C) / 001CA6E0"); }
static int h_set_taken(void *c, uint8_t puid)
{ (void)c; (void)puid; return unbound("001B1190 (model 0x50 taken bit)"); }
static int h_segment(void *c, const float f[3], const float t[3], int32_t mask, int32_t ex)
{ (void)c; (void)f; (void)t; (void)mask; (void)ex; return unbound("0019A570 (the drum's break test)"); }
static int h_effect_matrix(void *c, int32_t preset, const float m[16])
{ (void)c; (void)preset; (void)m; return unbound("001F0460 (effect preset)"); }
static int h_effect_point(void *c, uint32_t id, const float p[4])
{ (void)c; (void)id; (void)p; return unbound("001EFD20 (effect spawn)"); }
static int h_sweep(void *c, const float p[3], uint32_t mode)
{ (void)c; (void)p; (void)mode; return unbound("0019AD00 (the drum's flight sweep)"); }

/* --------------------------------------------------------------- public */

static void services_bind(void)
{
    memset(&S.services, 0, sizeof S.services);
    S.services.world.d0028A59C = &S.bank_word;
    S.services.world.d00275BCC = &S.bones.count;
    S.services.world.scratch = &S.spr;
    S.services.workers.w_001C6120 = w_001C6120;
    S.services.workers.w_001CA6E0 = w_001CA6E0;
    S.services.workers.w_001AF780 = w_001AF780;
    S.services.workers.w_anim_bone_array_setup = w_anim_bone_array_setup;
}

int32_t *em_area11_boxes_carry31F0(void) { return &S.carry31F0; }

void em_area11_boxes_reset(void)
{
    memset(S.box, 0, sizeof S.box);
    /* 001AF710: zero the 0x480 slots, the stack holds each one's address,
     * the cursor at its base and the count 0x480. */
    S.bones.records = S.records;
    em_slg_001AF710(&S.bones);
    memset(S.slots, 0, sizeof S.slots);
    memset(&S.stack, 0, sizeof S.stack);
    S.stack.world.d00275BCC = &S.bones.count;
    S.stack.world.d00275BD0 = &S.bones.head;
    S.stack.world.slot_stack = S.bones.slot;
    S.stack.world.slot_stack_base = EM_SLG_BONE_ARRAY;
    S.stack.world.slot_stack_words = EM_SLG_BONE_SLOTS;
    S.stack.world.slots = S.records;
    S.stack.world.slots_base = EM_SLG_BONE_RECORDS;
    S.stack.world.slots_size = EM_SLG_BONE_SLOTS * EM_SLG_BONE_SLOT_SIZE;
    S.draw_count = 0;
    services_bind();
}

static Box *box_for(EmActor *actor)
{
    Box *free_slot = NULL;
    for (unsigned i = 0; i < BOX_MAX; ++i) {
        Box *b = &S.box[i];
        if (b->actor == actor && b->generation == actor->generation && !b->freed) return b;
        if (!free_slot && (!b->actor || b->freed || b->actor->generation != b->generation))
            free_slot = b;
    }
    if (!free_slot) return NULL;
    memset(free_slot, 0, sizeof *free_slot);
    free_slot->actor = actor;
    free_slot->generation = actor->generation;
    free_slot->drum = actor->callback == DRUM_CALLBACK;
    free_slot->truck = actor->callback == TRUCK_CALLBACK;
    return free_slot;
}

int em_area11_boxes_001AF800(void *ctx, EmActor *actor)
{
    (void)ctx;
    /* Roger and the equipment node keep their +0x110 words in their own
     * binder (census L22). */
    int roger = em_area11_roger_001AF800(actor);
    if (roger != 0) return roger < 0 ? -1 : 0;
    for (unsigned i = 0; i < BOX_MAX; ++i) {
        Box *b = &S.box[i];
        if (b->actor != actor || b->generation != actor->generation || b->freed) continue;
        for (unsigned j = 0; j < actor->bones; ++j)
            if (push_001AF890(b->view.bone[j]) < 0) return -1;
        return 0;
    }
    return report("001AF800 on a record that holds no box bone slots");
}

const EmRogerActorWorld *em_area11_boxes_slot_world(void)
{
    if (!S.stack.world.d00275BCC) em_area11_boxes_reset();
    return &S.stack.world;
}

int em_area11_boxes_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene)
{
    if (!actor || !pool || !scene) return -1;
    if (actor->callback != CRATE_CALLBACK && actor->callback != DRUM_CALLBACK) return -1;
    if (!S.stack.world.d00275BCC) em_area11_boxes_reset();
    if (load_tables() < 0) return -1;
    Box *b = box_for(actor);
    if (!b) return report("more AREA11 boxes than slots");
    b->pool = pool;
    b->scene = scene;
    b->drawn = 0;
    int r;
    if (!b->drum) {
        /* D_00275BC0: the whole live list as (model, +0x52, &+0x0A). */
        static EmCrateListEntry list[EM_ACTOR_POOL_CAPACITY];
        size_t count = 0;
        for (EmActor *n = pool->head; n && count < EM_ACTOR_POOL_CAPACITY; n = n->next)
            list[count++] = (EmCrateListEntry){n->model, n->h52, &n->u0A[0]};
        const EmCrateInput in = {scene->d810700, scene->d810701, actor->next != NULL, list, count,
                                 NULL, (const float (*)[4][3])S.rattle, RATTLE_ROWS};
        const EmCrateOriginalHooks hooks = {
            b, h_allocate, h_bone_init, h_publish, h_place, h_probe, h_random, h_sound, h_sound3d,
            h_effect, h_taken, h_spawn, h_rebind, h_bone_matrix, h_draw, h_set_taken, h_free};
        crate_load(b);
        r = em_crate_original_tick(&b->crate, &in, &hooks);
        if (r >= 0 && !b->freed) crate_store(b);
    } else {
        const EmPlayerLiveActor *player = player_states_actor_mut();
        EmDrumInput in;
        memset(&in, 0, sizeof in);
        in.area = scene->d810700;
        if (player) memcpy(in.player, player->bytes + 0xA0, sizeof in.player); /* D_00810350 */
        in.frame = scene->spad3B68;
        in.dispatch_index = (int16_t)scene->spad3B8A;
        memcpy(in.speed_table, S.speed, sizeof in.speed_table);
        memcpy(in.lift_table, S.lift, sizeof in.lift_table);
        if (!player) return report("00156620 reads D_00810350 without the player record");
        const EmDrumOriginalHooks hooks = {
            b, h_allocate, h_bone_init, h_place, h_segment, h_effect_matrix, h_draw, h_contact,
            h_visibility, h_effect_point, h_sound, h_random, h_sweep, h_probe, h_sound3d, h_hull,
            h_free};
        drum_load(b);
        r = em_drum_original_tick(&b->drum_state, &in, &hooks);
        if (r >= 0 && !b->freed) drum_store(b);
    }
    if (r < 0) {
        fprintf(stderr, "em_area11 boxes: %08X at record uid %04X faulted\n", (unsigned)actor->callback,
                (unsigned)actor->uid);
        return -1;
    }
    return 1;
}

/* ------------------------------------------------ the truck (census L23) */

/* Record <-> EmTruckOriginal: +0x04, +0xB0, +0xC0's X and the +0x1F0 block
 * (+0x1F0 rest matrix, +0x2DC / +0x2E0 jitter, +0x2E4 rest Y, +0x2E8 rest
 * X angle, +0x2EC shake counter); +0x28, +0xD0 and the velocity scratch
 * stay in the slot. */
static void truck_load(Box *b)
{
    const EmActor *a = b->actor;
    EmTruckOriginal *t = &b->truck_state;
    t->state = a->u04[0];
    memcpy(t->position, a->pos, sizeof t->position);
    t->rotation_x = a->rot[0];
    get_floats(a->scratch, 0x1F0, t->rest_matrix, 16);
    get_floats(a->scratch, 0x2DC, &t->jitter_z, 1);
    get_floats(a->scratch, 0x2E0, &t->jitter_x, 1);
    get_floats(a->scratch, 0x2E4, &t->rest_y, 1);
    get_floats(a->scratch, 0x2E8, &t->rest_rotation_x, 1);
    memcpy(&t->shake, a->scratch + (0x2EC - 0x1F0), 4);
}

static void truck_store(Box *b)
{
    EmActor *a = b->actor;
    const EmTruckOriginal *t = &b->truck_state;
    a->u04[0] = t->state;
    memcpy(a->pos, t->position, sizeof t->position);
    put_floats(a->scratch, 0x1F0, t->rest_matrix, 16);
    put_floats(a->scratch, 0x2DC, &t->jitter_z, 1);
    put_floats(a->scratch, 0x2E0, &t->jitter_x, 1);
    put_floats(a->scratch, 0x2E4, &t->rest_y, 1);
    put_floats(a->scratch, 0x2E8, &t->rest_rotation_x, 1);
    memcpy(a->scratch + (0x2EC - 0x1F0), &t->shake, 4);
}

/* 001B0FD0(self): 001B0EA0, bone_init_default_1, +0x04 += 1 (the truck
 * overwrites +0x04 itself). *pending = its result. */
static int t_model_bind(void *ctx, int *pending)
{
    Box *b = ctx;
    if (load_bank() < 0) return -1;
    view_sync(b);
    int r = em_owner_services_001B0FD0(&S.services, &b->view);
    if (r < 0 || services_fault("001B0FD0") < 0) return -1;
    b->actor->bones = b->view.bones_held;    /* +0x09 */
    b->actor->u0A[2] = b->view.bone_count;   /* +0x0C */
    *pending = r;
    return 1;
}

static int t_placement(void *ctx, float matrix[16])
{
    Box *b = ctx;
    view_sync(b);
    if (em_owner_services_001C6380(&S.services, &b->view) < 0 || services_fault("001C6380") < 0) return -1;
    memcpy(matrix, b->view.world, sizeof b->view.world);
    return 1;
}

static int t_pose(void *ctx, const float matrix[16])
{
    return h_bone_matrix(ctx, matrix) < 0 ? -1 : 1;
}

static int t_hull(void *ctx, const float matrix[16])
{
    return h_hull(ctx, matrix) < 0 ? -1 : 1;
}

static int t_hull_bounds(void *ctx, float bounds[6])
{
    Box *b = ctx;
    EmActorCollisionOwner o = {em_collision_world_cells(), NULL, b->actor, b->pool};
    if (!o.world) return -1;
    return em_actor_collision_owner_hull_bounds(&o, bounds) == 1 ? 1 : -1;
}

static int t_publish(void *ctx)
{
    return h_publish(ctx) < 0 ? -1 : 1;
}

static int t_draw(void *ctx)
{
    return h_draw(ctx) < 0 ? -1 : 1;
}

/* 001B1E20(effect, 0) on the pad actuator block. */
static int t_rumble(void *ctx, int effect)
{
    (void)ctx;
    return em_pad_actuator_001B1E20(effect, 0) < 0 ? -1 : 1;
}

/* 001EFD20(0x80000049, position): the effect entity spawn has a
 * translation (em_effect_original) but no live effect owner (census L26,
 * blocked on the render-context storage, EFFECT_MANAGER.md 5.0): nothing is
 * spawned; the call is reported once and counted. */
static int t_effect(void *ctx, uint32_t id, const float position[4])
{
    (void)ctx;
    (void)position;
    if (id != EM_TRUCK_EFFECT_ID) return report("001EFD20 with an effect id other than 0x80000049");
    if (S.effect_gap++ == 0)
        fprintf(stderr, "em_area11 boxes: the truck's 001EFD20 (0x80000049) spawns reach no live effect owner "
                        "(census L26); counted by em_area11_boxes_effect_gap\n");
    return 1;
}

/* 001FBD50(self, id, 0, radius): its result is not read. */
static int t_sound(void *ctx, uint16_t id, float radius)
{
    Box *b = ctx;
    em_sfx_play_at(id, b->truck_state.position, radius);
    return 1;
}

static int t_free(void *ctx)
{
    return h_free(ctx) < 0 ? -1 : 1;
}

/* D_008104C4's owner: the player's +0x214 pointer is the ground owner's
 * pool record (em_player_floor.h), whose +0x0D the truck reads. The
 * player's +0xB0 is read only by the falling truck's carry test (00825014);
 * `need_hip` loads it then (the pose has no hip before the opening's
 * release, when the owners already run). */
static int truck_world(EmTruckWorld *w, EmSceneState *scene, float hip[3], int need_hip)
{
    EmPlayerLiveActor *p = player_states_actor_mut();
    memset(w, 0, sizeof *w);
    w->story = em_scene_progress_at(scene, 0x00810792u, 1);
    if (!w->story) return report("D_00810792 is not canonical");
    w->player_phase = &p->bytes[0x05];
    w->player_0a = &p->bytes[0x0A];
    w->ground_kind = p->link_owner ? &((const EmActor *)p->link_owner)->param : NULL;
    w->player_a0 = g.pos;
    hip[0] = hip[1] = hip[2] = 0.0f;
    if (need_hip && !player_pose_hip(hip))
        return report("00825014 reads the player's +0xB0 before the pose has one");
    w->player_b0 = hip;
    w->carry = &S.carry31F0;
    return 0;
}

int em_area11_boxes_truck_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene)
{
    if (!actor || !pool || !scene || actor->callback != TRUCK_CALLBACK) return -1;
    if (!S.stack.world.d00275BCC) em_area11_boxes_reset();
    Box *b = box_for(actor);
    if (!b) return report("more AREA11 world-model owners than slots");
    b->pool = pool;
    b->scene = scene;
    b->drawn = 0;
    EmTruckWorld w;
    float hip[3];
    truck_load(b);
    if (truck_world(&w, scene, hip, b->truck_state.state == 1) < 0) return -1;
    const EmTruckHooks hooks = {b, t_model_bind, t_placement, t_pose, t_hull, t_hull_bounds, t_publish,
                                t_draw, t_rumble, t_effect, t_sound, t_free};
    int r = em_truck_original_tick(&b->truck_state, &w, &hooks);
    if (r < 0) {
        fprintf(stderr, "em_area11 boxes: the truck 00823FF0 faulted in state %u\n",
                (unsigned)b->truck_state.state);
        return -1;
    }
    if (!b->freed) truck_store(b);
    return b->freed ? 0 : 1;
}

/* ------------------------------------- the truck's camera trigger 008251E0 */

typedef struct {
    EmActor *actor;
    EmActorPool *pool;
    EmSceneState *scene;
    int freed;
} Trigger;

static int g_script_start(void *ctx, uint32_t entry)
{
    Trigger *t = ctx;
    return em_area11_script_host_start(t->actor, entry) < 0 ? -1 : 1;
}

/* 001BA1F0(actor): any nonzero result (1 finished, 3 aborted) ends the
 * trigger's state 1. */
static int g_script_tick(void *ctx, int *done)
{
    Trigger *t = ctx;
    int32_t r;
    if (em_area11_script_host_tick(t->actor, &r) < 0) return -1;
    *done = r != 0;
    return 1;
}

static int g_free(void *ctx)
{
    Trigger *t = ctx;
    if (em_actor_pool_free_001AFC10(t->pool, t->scene, t->actor) < 0) return -1;
    t->freed = 1;
    return 1;
}

int em_area11_boxes_trigger_tick(EmActor *actor, EmActorPool *pool, EmSceneState *scene)
{
    if (!actor || !pool || !scene || actor->callback != TRIGGER_CALLBACK) return -1;
    EmTruckWorld w;
    float hip[3];
    if (truck_world(&w, scene, hip, 0) < 0) return -1;
    Trigger ctx = {actor, pool, scene, 0};
    const EmTruckTriggerHooks hooks = {&ctx, g_script_start, g_script_tick, g_free};
    EmTruckTrigger t = {actor->u04[0], actor->u0A[1], 0};
    int r = em_truck_trigger_tick(&t, &w, &hooks);
    if (r < 0) {
        fprintf(stderr, "em_area11 boxes: the truck trigger 008251E0 faulted in state %u\n", (unsigned)t.state);
        return -1;
    }
    if (ctx.freed) return 0;
    actor->u04[0] = t.state;   /* +0x04 */
    actor->u0A[1] = t.armed;   /* +0x0B */
    return 1;
}

unsigned em_area11_boxes_effect_gap(void)
{
    return S.effect_gap;
}

int em_area11_boxes_truck_state(uint32_t *record, uint8_t header[16], float position[3], uint8_t t2dc[20])
{
    for (unsigned i = 0; i < BOX_MAX; ++i) {
        const Box *b = &S.box[i];
        if (!b->truck || !b->actor || b->freed || b->actor->generation != b->generation || !b->pool) continue;
        uint8_t image[EM_ACTOR_RECORD_SIZE];
        em_actor_pool_record_image(b->pool, b->actor, image);
        *record = em_actor_pool_address(b->pool, b->actor);
        memcpy(header, image, 16);
        memcpy(position, b->actor->pos, 3 * sizeof(float));
        memcpy(t2dc, image + 0x2DC, 20);
        return 1;
    }
    return 0;
}

/* EM_BOX_DUMP=<path> (instrumentation only): after every walk, each live
 * box as "EMBX" v1, u32 count, then per box u32 callback, u32 record
 * address and the 0x2F0-byte record image with the owner's +0x28/+0x2A,
 * +0x34, +0x38 and +0xD0 world laid in at their offsets (the last walk
 * wins; tools/test_collision_world_capture.py compares them with the route
 * captures). */
static void put_word(uint8_t *out, unsigned at, const void *v, size_t n)
{
    memcpy(out + at, v, n);
}

static void dump_if_requested(void)
{
    const char *path = getenv("EM_BOX_DUMP");
    if (!path || !*path) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t count = 0;
    for (unsigned i = 0; i < BOX_MAX; ++i) {
        const Box *b = &S.box[i];
        count += b->actor && !b->freed && !b->truck && b->actor->generation == b->generation && b->pool;
    }
    const uint32_t head[2] = {0x58424D45u /* "EMBX" */, 1};
    fwrite(head, sizeof head, 1, f);
    fwrite(&count, sizeof count, 1, f);
    for (unsigned i = 0; i < BOX_MAX; ++i) {
        const Box *b = &S.box[i];
        if (!b->actor || b->freed || b->truck || b->actor->generation != b->generation || !b->pool) continue;
        uint8_t image[EM_ACTOR_RECORD_SIZE];
        em_actor_pool_record_image(b->pool, b->actor, image);
        if (b->drum) {
            put_word(image, 0x28, &b->drum_state.timer, 2);
            put_word(image, 0x34, &b->drum_state.health, 2);
            put_word(image, 0x38, &b->drum_state.speed, 4);
            put_word(image, 0xD0, b->drum_state.world, sizeof b->drum_state.world);
        } else {
            put_word(image, 0x28, &b->crate.tilt, 2);
            put_word(image, 0x2A, &b->crate.timer, 2);
            put_word(image, 0x34, &b->crate.health, 2);
            put_word(image, 0xD0, b->crate.world, sizeof b->crate.world);
        }
        const uint32_t tag[2] = {b->actor->callback, em_actor_pool_address(b->pool, b->actor)};
        fwrite(tag, sizeof tag, 1, f);
        fwrite(image, sizeof image, 1, f);
    }
    fclose(f);
}

int em_area11_boxes_draw_count(void)
{
    dump_if_requested();
    S.draw_count = 0;
    for (unsigned i = 0; i < BOX_MAX; ++i) {
        const Box *b = &S.box[i];
        if (b->actor && !b->freed && b->drawn && b->actor->generation == b->generation)
            S.draw_order[S.draw_count++] = (int)i;
    }
    return S.draw_count;
}

int em_area11_boxes_draw(int i, EmGfxMesh **mesh, const float **palette, uint32_t *bone_count)
{
    if (i < 0 || i >= S.draw_count) return 0;
    const Box *b = &S.box[S.draw_order[i]];
    const BoxMesh *m = mesh_of(b);
    if (!m->mesh) return 0;
    *mesh = m->mesh;
    *palette = b->palette;
    *bone_count = m->model.bone_count;
    return 1;
}

void em_area11_boxes_shutdown(EmGfx *gfx)
{
    BoxMesh *meshes[3] = {&S.crate_mesh, &S.drum_mesh, &S.truck_mesh};
    for (unsigned i = 0; i < 3; ++i) {
        if (meshes[i]->mesh) {
            em_gfx_mesh_destroy(gfx, meshes[i]->mesh);
            em_model_free(&meshes[i]->model);
        }
        memset(meshes[i], 0, sizeof *meshes[i]);
    }
    S.draw_count = 0;
}
