/* The status hub's 3D models (em_status_models.h, docs/STATUS_SCENE.md
 * section 7). Every behaviour is a translation in em_status_scene_original
 * or em_owner_services_original; this file binds their workers to the port
 * and queues the 001CB580 draws. */
#include "game/em_status_models.h"

#include "em_math.h"
#include "em_model.h"
#include "game/em_ee_float.h"
#include "game/em_owner_services_original.h"
#include "game/em_player_pose.h"
#include "game/em_random.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Port handles for the original words the owners pass around. The model
 * words are the addresses of the globals that hold them (D_0028A57C...,
 * D_0028A56C, D_0028A580); a glyph model is 0x10000000 | glyph. Bone slots
 * are SLOT_BASE + 0xD0 * index (a slot is 0xD0 bytes: 001AF800 clears 13
 * quadwords). */
#define TOKEN_PLAYER 0x0028A57Cu   /* D_0028A57C: variant 0, costume 0 */
#define TOKEN_BANK 0x0028A580u     /* D_0028A580: the player animation bank */
#define TOKEN_LETTERS 0x0028A56Cu  /* D_0028A56C: the equipment library */
#define TOKEN_GLYPH 0x10000000u
#define SLOT_BASE 0x02000000u
#define SLOT_SIZE 0xD0u
#define SLOTS 256
#define MODELS 8
#define PALETTE 64
#define DRAW_001CB580 0x001CB580u

typedef struct {
    uint32_t token;
    EmModel model;
    EmGfxMesh *mesh;
    EmOwnerSkeletonRecord skeleton[PALETTE];
    EmOwnerModel owner;
} Model;

typedef struct {
    int model;
    uint32_t bones;
    float palette[PALETTE * 16];
    EmGfxCharRig rig;
} Draw;

struct EmStatusModels {
    EmStatusScenePool pool;
    EmStatusSceneScratch spr;         /* 0020EC80's scratchpad */
    EmOwnerServicesScratch owner_spr; /* 001C9610's staging */
    EmOwnerServices services;
    EmStatusSceneWorkers workers;
    EmStatusSceneFault fault;
    EmOwnerBone slot[SLOTS];
    uint8_t slot_used[SLOTS];
    Model model[MODELS];
    unsigned model_count;
    int record_model[EM_STATUS_SCENE_POOL_RECORDS];
    EmPoseBank bank;
    EmPlayerPose pose[EM_STATUS_SCENE_POOL_RECORDS];
    int pose_valid[EM_STATUS_SCENE_POOL_RECORDS];
    float view[16];                   /* D_00810610, original row layout */
    int configured;
    EmStatusSceneActor *current;      /* D_00275B44 / D_00275B48 */
    int packet;                       /* 001D2040 channel 0 */
    EmStatusModelsInputs in;
    Draw draw[EM_STATUS_SCENE_POOL_RECORDS];
    unsigned draw_count;
    unsigned long drawn;
    int failed;
};

static int fault(EmStatusModels *m, uint32_t address, const char *what)
{
    if (!m->failed)
        fprintf(stderr, "status models: %08X: %s\n", (unsigned)address, what);
    m->failed = 1;
    if (!m->fault.code) {
        m->fault.address = address;
        m->fault.code = EM_STATUS_SCENE_FAULT_BAD_RESULT;
    }
    return -1;
}

static int record_of(const EmStatusModels *m, const EmStatusSceneActor *a)
{
    if (!a || a < m->pool.record || a >= m->pool.record + EM_STATUS_SCENE_POOL_RECORDS)
        return -1;
    return (int)(a - m->pool.record);
}

static EmOwnerBone *slot_of(EmStatusModels *m, uint32_t handle)
{
    uint32_t offset = handle - SLOT_BASE;
    if (handle < SLOT_BASE || offset % SLOT_SIZE || offset / SLOT_SIZE >= SLOTS ||
        !m->slot_used[offset / SLOT_SIZE])
        return NULL;
    return &m->slot[offset / SLOT_SIZE];
}

static Model *model_of(EmStatusModels *m, uint32_t token)
{
    for (unsigned i = 0; i < m->model_count; ++i)
        if (m->model[i].token == token)
            return &m->model[i];
    return NULL;
}

/* An owner-services view of a pool record (its bone slots, placement and
 * model), for 001C62C0 and 001C6380. */
static int owner_view(EmStatusModels *m, EmStatusSceneActor *a, EmOwnerServicesOwner *o)
{
    int r = record_of(m, a);
    if (r < 0 || m->record_model[r] < 0 || a->b0C > EM_OWNER_SERVICES_MAX_BONES)
        return fault(m, 0x001C6380u, "a placement without a bound model");
    memset(o, 0, sizeof *o);
    o->bone_count = a->b0C;
    o->bones_held = a->b09;
    o->model = &m->model[m->record_model[r]].owner;
    memcpy(o->scale, a->f60, sizeof o->scale);
    memcpy(o->pos, a->fB0, sizeof a->fB0);
    memcpy(o->rot, a->fC0, sizeof a->fC0);
    for (unsigned i = 0; i < a->b0C; ++i)
        if (!(o->bone[i] = slot_of(m, a->w110[i])))
            return fault(m, 0x001C6380u, "a bone slot word the port did not hand out");
    return 0;
}

static float *scratch_matrix(EmStatusModels *m, uint32_t address)
{
    if (address == EM_STATUS_SCENE_SPR_3400)
        return m->spr.s3400;
    if (address == EM_STATUS_SCENE_SPR_3440)
        return m->spr.s3440;
    EmOwnerBone *bone = slot_of(m, address - 0x90u);
    return bone ? bone->world : NULL;
}

/* The VU0 row transform of 001026D0/001026A0 (the forms 001C9610 uses:
 * MULABC /x, MADDABC /y and /z, MADDBC /w, all four lanes). */
static int vu_row(uint32_t out[4], const uint32_t mat[16], const uint32_t v[4])
{
    uint32_t acc[4] = {0, 0, 0, 0};
    int st = em_vu_vec_bits(EM_VU_MULABC, 15, 0, mat, v, 0, NULL, acc);
    if (!st) st = em_vu_vec_bits(EM_VU_MADDABC, 15, 1, mat + 4, v, 0, acc, acc);
    if (!st) st = em_vu_vec_bits(EM_VU_MADDABC, 15, 2, mat + 8, v, 0, acc, acc);
    if (!st) st = em_vu_vec_bits(EM_VU_MADDBC, 15, 3, mat + 12, v, 0, acc, out);
    return st;
}

/* ------------------------------------------------------------ workers --- */

static int w_slot_push(void *ctx, uint32_t handle)
{
    EmStatusModels *m = ctx;
    EmOwnerBone *bone = slot_of(m, handle);
    if (!bone)
        return fault(m, 0x001AF800u, "a bone slot word the port did not hand out");
    memset(bone, 0, sizeof *bone);
    m->slot_used[bone - m->slot] = 0;
    return 0;
}

static int w_001CB590(void *ctx, EmStatusSceneActor *a, int32_t stride, uint8_t count)
{
    EmStatusModels *m = ctx;
    (void)count;
    if (record_of(m, a) < 0 || stride != (int32_t)EM_STATUS_SCENE_RECORD_SIZE)
        return fault(m, 0x001CB590u, "a record outside D_0028B020");
    m->current = a; /* D_00275B48 = D_00275B44 = a0; D_00275B40 = a0 + 0x110 */
    return 0;
}

static int draw(EmStatusModels *m, EmStatusSceneActor *a);

static int w_call(void *ctx, EmStatusSceneActor *a, uint32_t fn)
{
    EmStatusModels *m = ctx;
    int r;
    switch (fn) {
    case EM_STATUS_SCENE_CB_0020E6F0: {
        EmStatusScenePlayerGlobals g = {
            .d8104E4 = m->in.d8104E4, .d810C60 = m->in.d810C60, .d28A57C = TOKEN_PLAYER,
            .d28A580 = TOKEN_BANK, .d28A584 = 0x0028A584u, .d28A588 = 0x0028A588u,
            .d28A58C = 0x0028A58Cu, .d28A590 = 0x0028A590u, .health = m->in.health,
            .infection = m->in.infection};
        for (int row = 0; row < 3; ++row)
            memcpy(g.view + 4 * row, m->view + 4 * row, 4 * sizeof(float));
        r = em_status_scene_player_0020E6F0(a, &g, &m->workers, &m->fault);
        break;
    }
    case EM_STATUS_SCENE_CB_0020E460: {
        /* D_00275BCC: the port has no bone-slot stack (em_status_models.h). */
        EmStatusSceneLetterGlobals g = {.d275BCC = 0x7FFF,
                                        .d275B40 = m->current ? m->current->w110 : NULL};
        for (int row = 0; row < 3; ++row)
            memcpy(g.view + 4 * row, m->view + 4 * row, 4 * sizeof(float));
        r = em_status_scene_letter_0020E460(a, &g, &m->workers, &m->fault);
        break;
    }
    case DRAW_001CB580:
        return draw(m, a);
    default:
        return fault(m, fn, "a behaviour or draw method the status models do not translate");
    }
    if (r < 0)
        m->failed = 1;
    return r < 0 ? -1 : 0;
}

static int w_001AFF10(void *ctx, EmStatusSceneActor **out)
{
    EmStatusModels *m = ctx;
    EmStatusSceneActor *a = em_status_scene_alloc_001AFF10(&m->pool);
    int r = record_of(m, a);
    if (r >= 0) {
        m->record_model[r] = -1;
        m->pose_valid[r] = 0;
    }
    *out = a;
    return 0;
}

static int w_001C6120(void *ctx, uint32_t bank, int32_t code, uint32_t *model)
{
    EmStatusModels *m = ctx;
    uint32_t id = (uint32_t)code & 0xFFFFu & ~0x8000u; /* 001C6120's masks */
    if (bank != TOKEN_LETTERS)
        return fault(m, 0x001C6120u, "a model bank other than D_0028A56C");
    if (!model_of(m, TOKEN_GLYPH | id)) {
        char what[96];
        snprintf(what, sizeof what, "glyph model 0x%02X of D_0028A56C is not exported", (unsigned)id);
        return fault(m, 0x001C6120u, what);
    }
    *model = TOKEN_GLYPH | id;
    return 0;
}

static int w_001CA6E0(void *ctx, EmStatusSceneActor *a, uint32_t token)
{
    EmStatusModels *m = ctx;
    int r = record_of(m, a);
    Model *model = model_of(m, token);
    if (r < 0 || !model) {
        char what[96];
        snprintf(what, sizeof what, "model word %08X has no exported model", (unsigned)token);
        return fault(m, 0x001CA6E0u, what);
    }
    a->w44 = token;       /* 001CA5E0: +0x44 = the model */
    a->w4C = 0x001CAA00u; /* then 001CA5F0(a, 0): the default draw method */
    m->record_model[r] = (int)(model - m->model);
    return 0;
}

static int w_001C6150(void *ctx, uint32_t word44, uint32_t *value)
{
    EmStatusModels *m = ctx;
    Model *model = model_of(m, word44);
    if (!model)
        return fault(m, 0x001C6150u, "a model word without a model");
    *value = model->owner.bone_count; /* the byte at model + 8 */
    return 0;
}

static int w_001AF7C0(void *ctx, uint32_t *value)
{
    EmStatusModels *m = ctx;
    for (unsigned i = 0; i < SLOTS; ++i)
        if (!m->slot_used[i]) {
            m->slot_used[i] = 1;
            memset(&m->slot[i], 0, sizeof m->slot[i]);
            *value = SLOT_BASE + SLOT_SIZE * i;
            return 0;
        }
    return fault(m, 0x001AF7C0u, "the port's bone slots are exhausted");
}

static int w_001CB5B0(void *ctx, uint32_t count)
{
    EmStatusModels *m = ctx;
    (void)count; /* anim_bone_array_setup: D_00275B40 = D_00275B48 + 0x110 */
    return m->current ? 0 : fault(m, 0x001CB5B0u, "no current record (001CB590)");
}

/* bone_init_default_2 (001C63E0): the clip's node table gives +0x64, the
 * rest channels are cleared (+0x70..+0x84 = 0, +0x88..+0x8C = 0x1000), and
 * the clip is sampled at frame 0 (em_player_pose_init). */
static int w_001C63E0(void *ctx, EmStatusSceneActor *a, int32_t clip)
{
    EmStatusModels *m = ctx;
    int r = record_of(m, a);
    if (r < 0 || a->w40 != TOKEN_BANK || a->b0C != m->bank.bone_count)
        return fault(m, 0x001C63E0u, "a clip without the player bank D_0028A580");
    if (!em_player_pose_init(&m->pose[r], &m->bank, (unsigned)clip, 0.0f))
        return fault(m, 0x001C63E0u, "the clip is not exported (menu_player.empc)");
    m->pose_valid[r] = 1;
    for (unsigned i = 0; i < a->b0C; ++i) {
        EmOwnerBone *bone = slot_of(m, a->w110[i]);
        if (!bone)
            return fault(m, 0x001C63E0u, "a bone slot word the port did not hand out");
        bone->parent = (int16_t)m->bank.parents[i];
        memset(bone->rot, 0, sizeof bone->rot);
        memset(bone->trans, 0, sizeof bone->trans);
        bone->scale[0] = bone->scale[1] = bone->scale[2] = 0x1000;
    }
    return 0;
}

static int w_001C62C0(void *ctx, EmStatusSceneActor *a)
{
    EmStatusModels *m = ctx;
    EmOwnerServicesOwner o;
    if (owner_view(m, a, &o) < 0)
        return -1;
    if (em_owner_services_001C62C0(&m->services, &o) < 0)
        return fault(m, m->services.fault.address, "bone_init_default_1 faulted");
    return 0;
}

/* 001CA5F0: the 13-entry table at 0x0026E310 (byte-matched C). */
static int w_001CA5F0(void *ctx, EmStatusSceneActor *a, int32_t kind)
{
    (void)ctx;
    static const uint32_t method[13] = {0x001CAA00u, 0x001CAF60u, 0x001CACB0u, 0x001CAE30u,
                                        0x001CAA00u, 0x001CB360u, 0x001CAF70u, 0x001CB480u,
                                        0x001CB060u, 0x001CB130u, 0x001CB1F0u, 0x001CB580u,
                                        0x001CB2B0u};
    a->w4C = (uint32_t)kind < 13u ? method[kind] : 0x001CAA00u;
    return 0;
}

static int pose_of(EmStatusModels *m, EmStatusSceneActor *a, uint32_t address, EmPlayerPose **pose)
{
    int r = record_of(m, a);
    if (r < 0 || !m->pose_valid[r])
        return fault(m, address, "no clip bound (001C63E0)");
    *pose = &m->pose[r];
    return 0;
}

/* anim_clip_init (001C67E0)(a, clip, blend, frame): 0020E6F0 passes blend
 * 16.0 and frame 0.0, a nonzero blend (em_player_pose_select). */
static int w_001C67E0(void *ctx, EmStatusSceneActor *a, int32_t clip, float blend, float frame)
{
    EmStatusModels *m = ctx;
    EmPlayerPose *pose;
    if (pose_of(m, a, 0x001C67E0u, &pose) < 0)
        return -1;
    if (!(blend >= 1.0f && blend <= 65535.0f) || (float)(unsigned)blend != blend)
        return fault(m, 0x001C67E0u, "a blend other than a whole tick count");
    if (!em_player_pose_select(pose, (unsigned)clip, frame, (unsigned)blend, 1))
        return fault(m, 0x001C67E0u, "anim_clip_init refused the clip");
    return 0;
}

static int w_001C64F0(void *ctx, EmStatusSceneActor *a, float t)
{
    EmStatusModels *m = ctx;
    EmPlayerPose *pose;
    if (pose_of(m, a, 0x001C64F0u, &pose) < 0)
        return -1;
    return em_player_pose_advance(pose, t, 0) ? 0 : fault(m, 0x001C64F0u, "anim_advance_time");
}

static int w_0020EC80(void *ctx, EmStatusSceneActor *a)
{
    EmStatusModels *m = ctx;
    return em_status_scene_publish_0020EC80(a, m->in.d8104E4, &m->spr, &m->workers, &m->fault) < 0
               ? (m->failed = 1, -1)
               : 0;
}

static int w_001AFF90(void *ctx, EmStatusSceneActor *a)
{
    EmStatusModels *m = ctx;
    uint32_t self = a->w14;
    if (em_status_scene_free_001AFF90(&m->pool, a, &m->workers, &m->fault) < 0) {
        m->failed = 1;
        return -1;
    }
    unsigned r = (self - EM_STATUS_SCENE_D_0028B020) / EM_STATUS_SCENE_RECORD_SIZE;
    m->record_model[r] = -1;
    m->pose_valid[r] = 0;
    return 0;
}

static int w_001C6380(void *ctx, EmStatusSceneActor *a)
{
    EmStatusModels *m = ctx;
    EmOwnerServicesOwner o;
    if (owner_view(m, a, &o) < 0)
        return -1;
    if (em_owner_services_001C6380(&m->services, &o) < 0)
        return fault(m, m->services.fault.address, "001C6380 faulted");
    return 0;
}

static int w_001D2040(void *ctx, int32_t a0, int32_t a1)
{
    EmStatusModels *m = ctx;
    if (a0 != 0 || (a1 != 0 && a1 != 1))
        return fault(m, 0x001D2040u, "a GS state packet other than channel 0, packet 0/1");
    m->packet = a1;
    return 0;
}

static int w_001029C0(void *ctx, uint32_t address)
{
    EmStatusModels *m = ctx;
    float *p = scratch_matrix(m, address);
    if (!p || em_owner_services_identity_001029C0(p))
        return fault(m, 0x001029C0u, "identity");
    return 0;
}

static int rotate(EmStatusModels *m, uint32_t fn, uint32_t dst, uint32_t src, uint32_t angle)
{
    float *d = scratch_matrix(m, dst), *s = scratch_matrix(m, src);
    if (!d || !s)
        return fault(m, fn, "a rotation outside the modelled scratchpad");
    int st = fn == 0x00102B08u   ? em_owner_services_rotate_x_00102B08(d, s, angle)
             : fn == 0x00102BB0u ? em_owner_services_rotate_y_00102BB0(d, s, angle)
                                 : em_owner_services_rotate_z_00102A60(d, s, angle);
    return st ? fault(m, fn, "an unmeasured VU form") : 0;
}

static int w_00102B08(void *ctx, uint32_t d, uint32_t s, uint32_t angle)
{
    return rotate(ctx, 0x00102B08u, d, s, angle);
}
static int w_00102BB0(void *ctx, uint32_t d, uint32_t s, uint32_t angle)
{
    return rotate(ctx, 0x00102BB0u, d, s, angle);
}
static int w_00102A60(void *ctx, uint32_t d, uint32_t s, uint32_t angle)
{
    return rotate(ctx, 0x00102A60u, d, s, angle);
}

/* 001026D0(out, a, b): out rows = b rows x a (a is read first). */
static int w_001026D0(void *ctx, uint32_t out, uint32_t a, uint32_t b)
{
    EmStatusModels *m = ctx;
    float *o = scratch_matrix(m, out), *pa = scratch_matrix(m, a), *pb = scratch_matrix(m, b);
    if (!o || !pa || !pb)
        return fault(m, 0x001026D0u, "a product outside the modelled scratchpad");
    uint32_t ma[16], mb[16], result[16];
    memcpy(ma, pa, sizeof ma);
    memcpy(mb, pb, sizeof mb);
    for (int row = 0; row < 4; ++row)
        if (vu_row(result + 4 * row, ma, mb + 4 * row))
            return fault(m, 0x001026D0u, "an unmeasured VU form");
    memcpy(o, result, sizeof result);
    return 0;
}

/* 001026A0(out, m, v): out = v x m. The 'm' marker's vector D_0024A340 is
 * not exported (its glyph model 0x6D is not either). */
static int w_001026A0(void *ctx, uint32_t out, uint32_t mat, uint32_t v)
{
    EmStatusModels *m = ctx;
    float *pm = scratch_matrix(m, mat);
    if (out != EM_STATUS_SCENE_SPR_38A0 || v != EM_STATUS_SCENE_SPR_38A0 || !pm)
        return fault(m, 0x001026A0u, "a vector product other than 0020EC80's (D_0024A340 is not "
                                     "exported)");
    uint32_t mm[16], vv[4], r[4];
    memcpy(mm, pm, sizeof mm);
    memcpy(vv, m->spr.s38A0, sizeof vv);
    if (vu_row(r, mm, vv))
        return fault(m, 0x001026A0u, "an unmeasured VU form");
    memcpy(m->spr.s38A0, r, sizeof r);
    return 0;
}

int em_status_models_pose_001C69A0(const float object[16], const float scale[4],
                                   const EmStatusModelsNode *nodes, unsigned count,
                                   float (*world)[16])
{
    if (!object || !scale || !nodes || !world || count > EM_OWNER_SERVICES_MAX_BONES)
        return -1;
    /* 0x70003400 rows 0..2, xyz x +0x60 lanes x, y, z (MULBC xyz /x, /y, /z). */
    uint32_t root[16], s[4];
    memcpy(root, object, sizeof root);
    memcpy(s, scale, sizeof s);
    for (int row = 0; row < 3; ++row)
        if (em_vu_vec_bits(EM_VU_MULBC, 14, row, root + 4 * row, s, 0, NULL, root + 4 * row))
            return -1;
    EmOwnerBone bone[EM_OWNER_SERVICES_MAX_BONES];
    EmOwnerBone *list[EM_OWNER_SERVICES_MAX_BONES];
    for (unsigned i = 0; i < count; ++i) {
        const EmStatusModelsNode *n = &nodes[i];
        /* quat_to_mat3(0x70003440, the blended quaternion, +0x00), then row
         * k xyz x +0x18 + 4k (MULBC xyz /x). */
        EmPoseChannels c;
        memcpy(c.translation, n->translation, sizeof c.translation);
        memcpy(c.rotation, n->rotation, sizeof c.rotation);
        c.scale[0] = c.scale[1] = c.scale[2] = 1.0f; /* exact: EE x 1.0 */
        float anim[16];
        em_pose_channels_matrix(anim, &c);
        uint32_t rows[16];
        memcpy(rows, anim, sizeof rows);
        for (int row = 0; row < 3; ++row) {
            uint32_t v5[4] = {em_ee_bits(n->scale[row]), 0, 0, 0};
            if (em_vu_vec_bits(EM_VU_MULBC, 14, 0, rows + 4 * row, v5, 0, NULL, rows + 4 * row))
                return -1;
        }
        memset(&bone[i], 0, sizeof bone[i]);
        memcpy(bone[i].bind, rows, sizeof rows); /* 001C9610 multiplies rest x this */
        bone[i].parent = n->parent;
        memcpy(bone[i].rot, n->rest_rot, sizeof bone[i].rot);
        memcpy(bone[i].trans, n->rest_trans, sizeof bone[i].trans);
        memcpy(bone[i].scale, n->rest_scale, sizeof bone[i].scale);
        list[i] = &bone[i];
    }
    EmOwnerServicesScratch spr;
    EmOwnerServices services;
    memset(&services, 0, sizeof services);
    services.world.scratch = &spr;
    float rootf[16];
    memcpy(rootf, root, sizeof rootf);
    if (em_owner_services_001C9610(&services, list, (int32_t)count, rootf) < 0)
        return -1;
    for (unsigned i = 0; i < count; ++i)
        memcpy(world[i], bone[i].world, sizeof world[i]);
    return 0;
}

static int w_001C69A0(void *ctx, EmStatusSceneActor *a)
{
    EmStatusModels *m = ctx;
    EmPlayerPose *pose;
    if (pose_of(m, a, 0x001C69A0u, &pose) < 0)
        return -1;
    if (a->b0C != m->bank.bone_count)
        return fault(m, 0x001C69A0u, "a node count other than the bank's");
    EmStatusModelsNode nodes[EM_POSE_NODE_MAX];
    float world[EM_POSE_NODE_MAX][16];
    for (unsigned i = 0; i < a->b0C; ++i) {
        EmOwnerBone *bone = slot_of(m, a->w110[i]);
        if (!bone)
            return fault(m, 0x001C69A0u, "a bone slot word the port did not hand out");
        const EmPoseChannels *c = &pose->channels[i];
        EmStatusModelsNode *n = &nodes[i];
        memcpy(n->translation, c->translation, sizeof n->translation);
        memcpy(n->scale, c->scale, sizeof n->scale);
        memcpy(n->rotation, c->rotation, sizeof n->rotation);
        n->parent = bone->parent;
        memcpy(n->rest_rot, bone->rot, sizeof n->rest_rot);
        memcpy(n->rest_trans, bone->trans, sizeof n->rest_trans);
        memcpy(n->rest_scale, bone->scale, sizeof n->rest_scale);
    }
    if (em_status_models_pose_001C69A0(m->spr.s3400, a->f60, nodes, a->b0C, world) < 0)
        return fault(m, 0x001C69A0u, "the bone pose faulted");
    /* 001C69A0 stores the scaled rows back to 0x70003400. */
    uint32_t root[16], s[4];
    memcpy(root, m->spr.s3400, sizeof root);
    memcpy(s, a->f60, sizeof s);
    for (int row = 0; row < 3; ++row)
        (void)em_vu_vec_bits(EM_VU_MULBC, 14, row, root + 4 * row, s, 0, NULL, root + 4 * row);
    memcpy(m->spr.s3400, root, sizeof root);
    for (unsigned i = 0; i < a->b0C; ++i)
        memcpy(slot_of(m, a->w110[i])->world, world[i], sizeof world[i]);
    return 0;
}

static int w_00122BB8(void *ctx, int32_t *value)
{
    (void)ctx;
    *value = (int32_t)em_random_next(); /* the shared LCG: its low 31 bits */
    return 0;
}

static int w_001CD520(void *ctx, int32_t a0, int32_t a1, uint32_t position, uint64_t tex0,
                      uint32_t rgb, float f12, float f13, float f14)
{
    (void)a0; (void)a1; (void)position; (void)tex0; (void)rgb; (void)f12; (void)f13; (void)f14;
    return fault(ctx, 0x001CD520u, "the menu player's D_008104E4 glow sprite 001CD520 is not "
                                   "translated");
}

/* 001CB580 -> 001CB4F0(a, +0x44): lighting mode 1 over the node matrices. */
static int draw(EmStatusModels *m, EmStatusSceneActor *a)
{
    int r = record_of(m, a);
    if (r < 0 || m->record_model[r] < 0)
        return fault(m, DRAW_001CB580, "a draw without a bound model");
    if (m->packet != 1)
        return fault(m, DRAW_001CB580, "a model draw outside GS state packet 1");
    Model *model = &m->model[m->record_model[r]];
    if (a->b0C + 1u != model->model.bone_count || m->draw_count >= EM_STATUS_SCENE_POOL_RECORDS)
        return fault(m, DRAW_001CB580, "the node count differs from the exported model");
    Draw *d = &m->draw[m->draw_count];
    d->model = m->record_model[r];
    d->bones = model->model.bone_count;
    for (unsigned i = 0; i < a->b0C; ++i) {
        EmOwnerBone *bone = slot_of(m, a->w110[i]);
        if (!bone)
            return fault(m, DRAW_001CB580, "a bone slot word the port did not hand out");
        memcpy(d->palette + 16 * i, bone->world, sizeof bone->world); /* +0x90 */
    }
    float *identity = d->palette + 16 * a->b0C; /* the EMDL's trailing slot */
    memset(identity, 0, 16 * sizeof(float));
    identity[0] = identity[5] = identity[10] = identity[15] = 1.0f;
    /* 001D8C30 case 1: light directions and colours zero; ambient lane k =
     * 8388608 + (128 + actor +0x80 + 4k), w = 8388608 + 64 * max(+0x8C - 1,
     * 0); the kernel reads the biased value's integer part. */
    memset(&d->rig, 0, sizeof d->rig);
    const uint32_t bias = 0x4B000000u, f128 = 0x43000000u, f64 = 0x42800000u, one = 0x3F800000u;
    for (int k = 0; k < 3; ++k) {
        uint32_t lane = em_ee_add_bits(bias, em_ee_add_bits(f128, em_ee_bits(a->f80[k])));
        d->rig.amb[k] = em_ee_float(em_ee_sub_bits(lane, bias));
    }
    uint32_t t = em_ee_sub_bits(em_ee_bits(a->f80[3]), one);
    if (em_ee_c_le_bits(t, 0))
        t = 0;
    d->rig.amb[3] = em_ee_float(em_ee_sub_bits(em_ee_add_bits(bias, em_ee_mul_bits(f64, t)), bias));
    m->draw_count++;
    return 0;
}

/* ------------------------------------------------------------ loading --- */

static int load_skeletons(EmStatusModels *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    uint32_t header[3];
    int ok = f && fread(header, sizeof header, 1, f) == 1 && !memcmp(header, "EMSK", 4) &&
             header[1] == 1 && header[2] == m->model_count;
    for (unsigned i = 0; ok && i < m->model_count; ++i) {
        uint32_t entry[2];
        ok = fread(entry, sizeof entry, 1, f) == 1 && entry[0] == m->model[i].token -
             (m->model[i].token & TOKEN_GLYPH ? TOKEN_GLYPH : 0) && entry[1] &&
             entry[1] < PALETTE && entry[1] + 1u == m->model[i].model.bone_count;
        Model *model = &m->model[i];
        for (unsigned n = 0; ok && n < entry[1]; ++n) {
            int32_t parent;
            ok = fread(&parent, 4, 1, f) == 1 && fread(model->skeleton[n].bind, 64, 1, f) == 1 &&
                 parent >= -1 && parent < (int32_t)n;
            model->skeleton[n].parent = (int16_t)parent;
        }
        model->owner.bone_count = (uint8_t)entry[1];
        model->owner.skeleton = model->skeleton;
        model->owner.skeleton_records = entry[1];
    }
    ok = ok && fgetc(f) == EOF;
    if (f)
        fclose(f);
    return ok;
}

EmStatusModels *em_status_models_load(const char *directory)
{
    static const uint8_t glyphs[] = {0x2F, 0x40, 0x30, 0x31, 0x32, 0x38};
    char path[1024];
    EmStatusModels *m = calloc(1, sizeof *m);
    if (!m || !directory)
        goto fail;
    Model *player = &m->model[m->model_count++];
    player->token = TOKEN_PLAYER;
    snprintf(path, sizeof path, "%s/menu_player.emdl", directory);
    if (em_model_load(&player->model, path) != 0 || player->model.bone_count != 22)
        goto fail;
    for (unsigned i = 0; i < sizeof glyphs; ++i) {
        Model *letter = &m->model[m->model_count++];
        letter->token = TOKEN_GLYPH | glyphs[i];
        snprintf(path, sizeof path, "%s/letter_%02x.emdl", directory, glyphs[i]);
        if (em_model_load(&letter->model, path) != 0)
            goto fail;
    }
    snprintf(path, sizeof path, "%s/models.emsk", directory);
    if (!load_skeletons(m, path))
        goto fail;
    snprintf(path, sizeof path, "%s/menu_player.empc", directory);
    if (!em_pose_bank_load(&m->bank, path) || m->bank.bone_count != 21)
        goto fail;
    for (int i = 0; i < EM_STATUS_SCENE_POOL_RECORDS; ++i)
        m->record_model[i] = -1;
    m->services.world.scratch = &m->owner_spr;
    m->workers = (EmStatusSceneWorkers){
        .ctx = m, .w_001AF800_slot = w_slot_push, .w_001CB590 = w_001CB590, .w_call = w_call,
        .w_001AFF10 = w_001AFF10, .w_001C6120 = w_001C6120, .w_001CA6E0 = w_001CA6E0,
        .w_001C6150 = w_001C6150, .w_001AF7C0 = w_001AF7C0, .w_001CB5B0 = w_001CB5B0,
        .w_001C63E0 = w_001C63E0, .w_001C62C0 = w_001C62C0, .w_001CA5F0 = w_001CA5F0,
        .w_001C67E0 = w_001C67E0, .w_001C64F0 = w_001C64F0, .w_0020EC80 = w_0020EC80,
        .w_001AFF90 = w_001AFF90, .w_001C6380 = w_001C6380, .w_001D2040 = w_001D2040,
        .w_001029C0 = w_001029C0, .w_00102B08 = w_00102B08, .w_00102BB0 = w_00102BB0,
        .w_00102A60 = w_00102A60, .w_001026D0 = w_001026D0, .w_001026A0 = w_001026A0,
        .w_001C69A0 = w_001C69A0, .w_00122BB8 = w_00122BB8, .w_001CD520 = w_001CD520};
    return m;
fail:
    fprintf(stderr, "status models: %s is missing or invalid (tools/export_status_models.py)\n",
            directory ? path : "(no directory)");
    em_status_models_free(m, NULL);
    return NULL;
}

void em_status_models_free(EmStatusModels *m, EmGfx *gfx)
{
    if (!m)
        return;
    for (unsigned i = 0; i < m->model_count; ++i) {
        if (gfx && m->model[i].mesh)
            em_gfx_mesh_destroy(gfx, m->model[i].mesh);
        em_model_free(&m->model[i].model);
    }
    em_pose_bank_free(&m->bank);
    free(m);
}

/* -------------------------------------------------------------- driving --- */

static int ready(const EmStatusModels *m) { return m && !m->failed; }

int em_status_models_configure(EmStatusModels *m)
{
    if (!ready(m))
        return -1;
    /* 0020DFA0: 001029C0(D_00810610); D_00810624 *= -1.0 (an EE multiply). */
    if (em_owner_services_identity_001029C0(m->view))
        return fault(m, 0x0020DFA0u, "identity");
    m->view[5] = em_ee_mul(m->view[5], -1.0f);
    m->configured = 1;
    return 1;
}

int em_status_models_release(EmStatusModels *m)
{
    if (!ready(m))
        return -1;
    if (em_status_scene_release_001AFEB0(&m->pool, &m->workers, &m->fault) < 0)
        return (m->failed = 1, -1);
    return 1;
}

int em_status_models_clear(EmStatusModels *m)
{
    if (!ready(m))
        return -1;
    em_status_scene_clear_001AFE60(&m->pool);
    for (int i = 0; i < EM_STATUS_SCENE_POOL_RECORDS; ++i) {
        m->record_model[i] = -1;
        m->pose_valid[i] = 0;
    }
    return 1;
}

int em_status_models_event(EmStatusModels *m, EmStatusHubEvent event, unsigned argument,
                           const EmStatusModelsInputs *in)
{
    if (!ready(m) || !in)
        return -1;
    m->in = *in;
    switch (event) {
    case EM_STATUS_HUB_INSTALL_DRAW: {
        /* 0020CDC0 sub-state 0: a = 001AFF10(); a+0x10 = 0x20E6F0 (no NULL
         * check: a full pool would store to 0x10, which the port faults). */
        EmStatusSceneActor *a = NULL;
        if (argument != EM_STATUS_SCENE_CB_0020E6F0 || w_001AFF10(m, &a) < 0 || !a)
            return fault(m, 0x0020CDC0u, "the menu player found no free static record");
        a->w10 = argument;
        return 1;
    }
    case EM_STATUS_HUB_BUILD_MODELS:
        if (em_status_scene_letters_0020E250(in->ca, TOKEN_LETTERS, &m->workers, &m->fault) < 0)
            return (m->failed = 1, -1);
        return 1;
    case EM_STATUS_HUB_ACTORS_TICK:
        if (!m->configured)
            return fault(m, 0x001B0000u, "the walk ran before 0020DFA0 set D_00810610");
        m->draw_count = 0;
        m->current = NULL;
        if (em_status_scene_walk_001B0000(&m->pool, &m->workers, &m->fault) < 0)
            return (m->failed = 1, -1);
        return 1;
    default:
        return fault(m, 0x0020CDC0u, "not a status-model worker");
    }
}

int em_status_models_render(EmStatusModels *m, EmGfx *gfx, float zoom)
{
    if (!ready(m) || !gfx)
        return -1;
    if (!m->draw_count)
        return 1;
    /* The view: D_00810610 as the column-vector matrix (its row layout read
     * column-major), then the native Y/Z sign flip em_mat4_lookat_gs uses. */
    float view[16], proj[16], vp[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            view[c * 4 + r] = r == 1 || r == 2 ? -m->view[c * 4 + r] : m->view[c * 4 + r];
    em_mat4_perspective_gs(proj, zoom);
    em_mat4_mul(vp, proj, view);
    em_gfx_fog_off(gfx);
    for (unsigned i = 0; i < m->draw_count; ++i) {
        Draw *d = &m->draw[i];
        Model *model = &m->model[d->model];
        if (!model->mesh) {
            model->mesh = em_gfx_mesh_create(gfx, model->model.verts, model->model.vert_count,
                                             model->model.indices, model->model.index_count,
                                             (const EmGfxTexDesc *)model->model.texs,
                                             model->model.tex_count, model->model.texels,
                                             model->model.flags);
            if (!model->mesh)
                return fault(m, DRAW_001CB580, "the model mesh upload failed");
        }
        em_gfx_char_rig(gfx, &d->rig);
        em_gfx_draw_skinned(gfx, model->mesh, vp, d->palette, d->bones);
        m->drawn++;
    }
    em_gfx_char_rig(gfx, NULL);
    m->draw_count = 0;
    return 1;
}

unsigned em_status_models_queued(const EmStatusModels *m) { return m ? m->draw_count : 0; }
unsigned long em_status_models_drawn(const EmStatusModels *m) { return m ? m->drawn : 0; }
const EmStatusScenePool *em_status_models_pool(const EmStatusModels *m) { return m ? &m->pool : NULL; }
const EmStatusSceneFault *em_status_models_fault(const EmStatusModels *m) { return m ? &m->fault : NULL; }

int em_status_models_node_world(const EmStatusModels *m, unsigned record, unsigned bone,
                                float out[16])
{
    if (!m || record >= EM_STATUS_SCENE_POOL_RECORDS || bone >= m->pool.record[record].b0C ||
        bone >= EM_STATUS_SCENE_BONE_SLOTS)
        return 0;
    const EmOwnerBone *b = slot_of((EmStatusModels *)m, m->pool.record[record].w110[bone]);
    if (!b)
        return 0;
    memcpy(out, b->world, sizeof b->world);
    return 1;
}
