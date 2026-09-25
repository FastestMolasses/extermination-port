/* Pickup render instances and persistent inventory.
 * The AREA11 owners 00219550/0015AFA0 run through em_pickup_original.h
 * (bound and ticked by the AREA11 interaction host since WP-6); the former
 * legacy use scan, two-frame take and flat inventory add are deleted, so
 * instances of scenes without a bound owner are drawn and never taken.
 */
#include "game/em_pickup.h"
#include "game/em_pickup_original.h"
#include "game/em_pickup_program.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_model.h"
#include "game/em_pickup_items_original.h"
#include "game/em_random.h"
#include "game/em_effect_color.h"
#include "game/em_scene_bindings.h" /* em_scene_state(): the D2 progress region */

#define PICKUP_PI 3.14159265358979f

/* AREA-11 placement records 1/2 (manifest `pickup 0x13 ... prop`, mesh
 * area_item_13.emdl) are driven in the original by the overlay behaviour
 * 00827630: a timed spin cycle on actor +0xC8 (the rot.z leg of
 * build_trs_matrix, not the placement yaw) with a 60-tick wait, ramp,
 * hold and ramp down, sound 0x451 and a player hit box. The cycle is not
 * translated yet (WP-11, overlay oracle for 00827630); the port draws the
 * pair STATIC. 00827630's state-0 init rot.z (+/-pi/4 by +0x2E) is
 * implemented by em_pickup_owner_init_pose, but it applies ONLY when the
 * manifest pickup line carries `owner 0x827630 <flags2>`; without that
 * suffix (the local manifest as of this writing) the fans are drawn with
 * placement yaw only. The former constant 1 deg/frame yaw spin was
 * invented and has been removed. */
#define PICKUP_OWNER_FAN 0x00827630u

#define PICKUP_BONE_MAX  8     /* item EMDLs are 1-node statics today */
#define PICKUP_MODEL_MAX 12    /* distinct model files per scene */

typedef struct {
    char       path[512];
    EmModel    model;
    EmGfxMesh *mesh;
    float      base[PICKUP_BONE_MAX * 16];   /* frame-0 local palette */
} PickupModel;

typedef struct {
    int     used;
    int     model;        /* index into models, -1 = none (logs once) */
    int     type;         /* item TYPE (actor +0x2E / record +0x07) */
    int     uid;          /* (area << 8) | puid; 0 = no persistence */
    int     prop;         /* display prop: render-only (kind-0xB) */
    float   pos[3];       /* placement (actor +0xB0) */
    float   yaw;          /* placement ry (actor +0xC4) */
    float   roll;         /* rot.z (actor +0xC8), 0 unless an owner init
                           * sets it (em_pickup_owner_init_pose) */
    float   scale;        /* actor +0x60..+0x68 — func_0015AC00's INIT
                           * scale switch, the S leg of func_001C6380's
                           * world TRS (1.0 / 1.5 / 2.0) */
    float   palette[PICKUP_BONE_MAX * 16];
    int original_bound, original_visible, original_failed;
    uint32_t source_id, publication_rank;
    EmPickupOwner original;
    EmPickupProgram program;
    EmInteractionRuntime *interaction;
    EmPickupOriginalHooks original_hooks;
} Pickup;

typedef struct {
    int owner;
    int model;
    int visible;          /* submitted this frame (em_pickup_light_submit) */
    float tint[4];
    float palette[PICKUP_BONE_MAX * 16];
} PickupLight;

static struct {
    PickupModel models[PICKUP_MODEL_MAX];
    int         n_models;
    Pickup      p[EM_PICKUP_MAX];
    PickupLight lights[EM_PICKUP_MAX];
    int         n_lights;
    int         n;            /* slots in use (dead slots stay counted
                               * until the scene clears — draw returns
                               * 0 for them, like the enemy contract) */
    int canonical_pickups;
} s;

/* --- persistent game state ---------------------------------------------
 * The item block D_00810C60.. is canonical D2 progress (em_scene_state.h,
 * migrated in WP-6): the equipment status C60, the pack count C63, the item
 * counts D_00810C64[t], the meters CA8..CB0, the battery charge CB2 (s16)
 * and capacity CB7, and the map/key bytes D_00810CB8[t]/D_00810CC3[t],
 * which overlap the counts as in the original. D_00810C62 (the loaded
 * magazine) and D_00810CB4 (the reserve) are em_weapon's; the game binds
 * them with em_pickup_set_weapon_ammo. Everything survives scene clears
 * and is wiped only by the 001AF2C0 reset (em_pickup_reset). */
static struct {
    uint8_t *c62;   /* em_weapon's D_00810C62 */
    int16_t *cb4;   /* em_weapon's D_00810CB4 */
} ammo;

static uint8_t *item_at(uint32_t address, uint32_t size)
{
    return em_scene_progress_at(em_scene_state(), address, size);
}

/* The 001C40B0 resolver: the canonical item block, and em_weapon's two
 * ammunition fields. NULL (the routine faults) for anything else. */
static uint8_t *items_resolve(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    if (address == 0x00810C62u && size == 1) return ammo.c62;
    if (address == 0x00810CB4u && size == 2) return (uint8_t *)ammo.cb4;
    if (address <= 0x00810CB5u && address + size > 0x00810CB4u) return NULL;
    return item_at(address, size);
}

void em_pickup_set_weapon_ammo(uint8_t *c62, int16_t *cb4)
{
    ammo.c62 = c62;
    ammo.cb4 = cb4;
}

static uint8_t item_byte(uint32_t address)
{
    const uint8_t *p = item_at(address, 1);
    return p ? *p : 0;
}

static void item_store(uint32_t address, uint8_t value)
{
    uint8_t *p = item_at(address, 1);
    if (p) *p = value;
}

static int16_t item_half(uint32_t address)
{
    const uint8_t *p = item_at(address, 2);
    return p ? (int16_t)(uint16_t)(p[0] | p[1] << 8) : 0;
}

static void item_store_half(uint32_t address, int16_t value)
{
    uint8_t *p = item_at(address, 2);
    if (p) {
        p[0] = (uint8_t)value;
        p[1] = (uint8_t)((uint16_t)value >> 8);
    }
}

/* func_001B11E0 — the taken-bit test. The engine's argument is the
 * one-byte PUID and BOTH the test (func_001B11E0) and the set
 * (func_001B1190, byte-matched: `v1 = a0 & 0xff; if (v1 == 0) return;`)
 * bail out when that byte is zero. The port's flat uid is
 * (area << 8) | puid, so the guard is on the LOW BYTE — a puid-0 record
 * in a non-zero area (uid 0x0100, 0x0B00, ...) must not persist either.
 * (Was `uid <= 0`, which persisted exactly those and permanently
 * despawned an item the engine re-spawns on every area re-entry.) */
/* The taken bits are the canonical D_00810860 bytes of the D2 progress
 * region (em_scene_state.h; migrated from this module's former flat
 * taken[2048] mirror in S10b, same bit numbering). D_00810860 is u32[8] per
 * area: the bit of puid in area a is bit (puid & 31) of the word at
 * D_00810860 + (a << 5) + (puid >> 5) * 4 (001B11E0 / 001B1190), which in the
 * EE's little-endian bytes is bit (u & 7) of byte D_00810860 + (u >> 3) for
 * the flat u = (a << 8) | puid. Areas past 0x16 would address D_00810B40..,
 * which the original never does for a pickup; such a uid is reported and
 * treated as never taken (no persistence), never silently aliased. */
static uint8_t *taken_byte(unsigned u)
{
    uint32_t address = 0x00810860u + (u >> 3);
    uint8_t *byte = address < 0x00810B40u
                  ? em_scene_progress_at(em_scene_state(), address, 1) : NULL;
    if (!byte) {
        static int warned;
        if (!warned) {
            warned = 1;
            fprintf(stderr, "pickup: uid %#06x is outside D_00810860 (areas 0..0x16); "
                    "not persisted\n", u);
        }
    }
    return byte;
}

static void taken_set(unsigned u)
{
    uint8_t *byte = taken_byte(u);
    if (byte) *byte = (uint8_t)(*byte | (1u << (u & 7)));
}

static int taken_bit(int uid)
{
    if (uid <= 0 || (uid & 0xFF) == 0) return 0;
    unsigned u = (unsigned)uid & 0xFFFF;
    const uint8_t *byte = taken_byte(u);
    return byte ? (*byte >> (u & 7)) & 1u : 0;
}

/* D_00810CA4 / D_00810CA6 in the canonical D2 progress region (always
 * canonical: em_scene_state.h lists CA4..CA7 as migrated). */
static uint8_t *equip_byte(uint32_t address)
{
    return em_scene_progress_at(em_scene_state(), address, 1);
}

int em_pickup_taken(int uid) { return taken_bit(uid); }


/* ------------------------------------------------------------------ */

static int pickup_model_get(EmGfx *gfx, const char *scene_dir,
                            const char *file)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", scene_dir, file);
    for (int i = 0; i < s.n_models; i++)
        if (strcmp(s.models[i].path, path) == 0)
            return i;
    if (s.n_models >= PICKUP_MODEL_MAX) return -1;

    PickupModel *pm = &s.models[s.n_models];
    if (em_model_load(&pm->model, path) != 0) return -1;
    if (pm->model.bone_count > PICKUP_BONE_MAX) {
        fprintf(stderr, "pickup: %s: %u bones > %d\n", path,
                pm->model.bone_count, PICKUP_BONE_MAX);
        em_model_free(&pm->model);
        return -1;
    }
    pm->mesh = em_gfx_mesh_create(gfx, pm->model.verts,
                                  pm->model.vert_count, pm->model.indices,
                                  pm->model.index_count,
                                  (const EmGfxTexDesc *)pm->model.texs,
                                  pm->model.tex_count, pm->model.texels,
                                  pm->model.flags);
    if (!pm->mesh) {
        em_model_free(&pm->model);
        return -1;
    }
    snprintf(pm->path, sizeof pm->path, "%s", path);
    em_model_palette_at(&pm->model, 0, 0.0, pm->base);
    return s.n_models++;
}

/* func_0015AC00's INIT scale switch. The engine keys it on the actor's
 * MODEL/library id byte +0x0D — NOT the item type — and writes the
 * result to +0x60/+0x64/+0x68, which func_001C6380 then folds into the
 * world TRS it stamps into every bone slot. The port carries the model
 * id in the instance's filename: the decomp exporter writes the chunk27
 * library models as `item_<id:02x>.emdl` (tools/export_props.py). The
 * per-area table's `area_item_<id>.emdl` files are deliberately NOT
 * matched here — those instances are the kind-0xB props, whose INIT is
 * func_001C4820/func_001B0FD0 and never runs this switch (and every
 * per-area id is < 0x40, i.e. the default 1.0, anyway).
 *
 * Recovered C (src/func_0015AC00.c, NEARMISS — logic authoritative):
 *   0x5B                                             -> 0x3FC00000 1.5
 *   0x6D and {0x40,0x41,0x42,0x45,0x4D,0x4E,0x4F,
 *             0x55,0x56,0x57,0x59,0x6C}              -> 0x40000000 2.0
 *   default                                          -> 0x3F800000 1.0 */
static float pickup_model_scale(const char *model_file)
{
    const char *base = strrchr(model_file, '/');
    char       *end;
    long        id;

    base = base ? base + 1 : model_file;
    if (strncmp(base, "item_", 5) != 0) return 1.0f;
    id = strtol(base + 5, &end, 16);
    if (end == base + 5 || *end != '.') return 1.0f;

    switch ((int)id) {
    case 0x5B:
        return 1.5f;
    case 0x40: case 0x41: case 0x42: case 0x45:
    case 0x4D: case 0x4E: case 0x4F:
    case 0x55: case 0x56: case 0x57: case 0x59:
    case 0x6C: case 0x6D:
        return 2.0f;
    default:
        return 1.0f;
    }
}

/* func_001C6380 — world TRS applied to each authored rest-node palette.
 * Static pickup bodies keep their original node offsets: model72 has
 * separate base/lid nodes, which must not all become identity matrices.
 * build_trs_matrix (0x001C94B0) applies rot.x, rot.y, then rot.z, each
 * left-multiplied, so a nonzero rot.z (p->roll) rotates about the WORLD Z
 * axis after the yaw. Checked against the +0xD0 world matrices of both
 * 00827630 actors in the three AREA11 EE captures (rot.z = +/-0.68,
 * +/-0.84, +/-1.79; placement rot.x is 0 for every pickup record). */
static void pickup_build_palette(Pickup *p)
{
    if (p->model < 0) return;
    PickupModel *pm = &s.models[p->model];
    uint32_t n = pm->model.bone_count;
    memcpy(p->palette, pm->base, n * 16 * sizeof(float));
    const float ry = p->yaw;
    const float c = cosf(ry), sn = sinf(ry);
    for (uint32_t b = 0; b < n; b++) {
        float *m = p->palette + b * 16;
        /* the S leg of the TRS: scale the basis and the LOCAL offset,
         * before the placement translation goes on below (uniform
         * scale commutes with the R_y that follows) */
        if (p->scale != 1.0f)
            for (int k = 0; k < 4; k++) {
                m[k * 4 + 0] *= p->scale;
                m[k * 4 + 1] *= p->scale;
                m[k * 4 + 2] *= p->scale;
            }
        for (int col = 0; col < 4; col++) {
            float x = m[col * 4 + 0], z = m[col * 4 + 2];
            m[col * 4 + 0] =  c * x + sn * z;
            m[col * 4 + 2] = -sn * x + c * z;
        }
        if (p->roll != 0.0f) {
            const float cz = cosf(p->roll), sz = sinf(p->roll);
            for (int col = 0; col < 4; col++) {
                float x = m[col * 4 + 0], y = m[col * 4 + 1];
                m[col * 4 + 0] = cz * x - sz * y;
                m[col * 4 + 1] = sz * x + cz * y;
            }
        }
        m[12] += p->pos[0];
        m[13] += p->pos[1];
        m[14] += p->pos[2];
    }
}

int em_pickup_add(EmGfx *gfx, const char *scene_dir, int type,
                  const float pos[3], float yaw, int uid,
                  const char *model_file, int prop)
{
    /* The engine's cond-1 spawn suppression (func_001B6660 case 1 +
     * func_001B11E0): a taken uid never re-spawns. */
    if (!prop && taken_bit(uid))
        return -2;
    if (s.n >= EM_PICKUP_MAX) return -1;

    Pickup *p = &s.p[s.n];
    memset(p, 0, sizeof *p);
    p->used  = 1;
    p->model = -1;
    p->type  = type;
    p->uid   = uid;
    p->prop  = prop;
    p->pos[0] = pos[0];
    p->pos[1] = pos[1];
    p->pos[2] = pos[2];
    p->yaw    = yaw;
    p->scale  = 1.0f;
    if (model_file) {
        /* func_0015AC00's INIT scale runs for COLLECTIBLES only — the
         * kind-0xB props init through func_001C4820/func_001B0FD0. */
        if (!prop)
            p->scale = pickup_model_scale(model_file);
        p->model = pickup_model_get(gfx, scene_dir, model_file);
        if (p->model < 0)
            printf("pickup %d: model %s failed to load — instance is "
                   "%s but invisible\n", s.n, model_file,
                   prop ? "placed" : "collectible");
    }
    pickup_build_palette(p);
    printf("pickup %d: type %#04x at (%.1f, %.1f, %.1f) yaw %.3f uid "
           "%#06x%s%s%s\n", s.n, type, pos[0], pos[1], pos[2], yaw,
           (unsigned)uid, prop ? " [prop]" : "",
           model_file ? " " : "", model_file ? model_file : "");
    return s.n++;
}

void em_pickup_scene_clear(EmGfx *gfx)
{
    for (int i = 0; i < s.n; ++i) em_pickup_program_free(&s.p[i].program);
    for (int i = 0; i < s.n_models; i++) {
        em_gfx_mesh_destroy(gfx, s.models[i].mesh);
        em_model_free(&s.models[i].model);
    }
    memset(&s, 0, sizeof s);
    /* g (inventory + taken bits + pending events) deliberately
     * survives — see em_pickup.h. */
}

/* The state-0 init of a placed prop's overlay owner, for owners whose
 * per-frame behaviour is not translated yet. Only 00827630 (the AREA11 fan
 * pair) is known; any other owner is refused (-1) so a manifest cannot
 * silently claim an untranslated init. 00827630 state 0 (runtime
 * 0x00827680..0x008276B4): 001B0FD0, +0x38 (spin rate) = 0, then
 * +0xC8 = +0x2E == 0 ? 0x3F490FDB (+pi/4) : 0xBF490FDB (-pi/4). +0x2E is
 * the placement record's +0x03 byte (records 1/2: 0 and 1). */
int em_pickup_owner_init_pose(int slot, uint32_t owner, unsigned flags2)
{
    if (slot < 0 || slot >= s.n || !s.p[slot].used) return -1;
    if (owner != PICKUP_OWNER_FAN) return -1;
    Pickup *p = &s.p[slot];
    p->roll = flags2 == 0 ? 0x1.921fb6p-1f : -0x1.921fb6p-1f;
    pickup_build_palette(p);
    return 0;
}

/* func_001AF2C0 (src/func_001AF2C0.c, NEARMISS; the values are checked
 * against the executed original by tools/test_continue_reset_reference.py)
 * — the part of the new-game reset em_pickup mirrors. After the 0x640-byte
 * D_00810700 memset it stores C60 = 0, count[0] (C64) = count[5] (C69) =
 * count[7] (C6B) = count[0x17] (C7B) = 1, CA4 = 0xFF, CA6 = 0, CB2 = CB7 = 0,
 * then calls 001C40B0(0x10, 2): count[0x10] += 2 and C63 += 2. That call's
 * reserve (CB4) and loaded-magazine (C62) writes are overwritten by
 * 001AF2C0's own C62 = 30 / CB4 = 60 stores, which em_game mirrors
 * (game_state_new_game), so no ammo_pending rounds are queued here.
 * Since S10b the taken bits (D_00810860..) and the equipment bytes
 * CA4..CA7 are canonical in the D2 progress region, and this reset clears
 * that region and writes 001AF2C0's CA4 = 0xFF, CA5 = 5, CA6 = 0, CA7 = 7
 * there (em_scene_progress_reset_001AF2C0). Not mirrored anywhere yet:
 * D20..D23 = 1. */
void em_pickup_reset(void)
{
    /* The D_00810700 memset for the canonical D2 bytes (the item block, the
     * taken bits and CA4..CA7 among them), with 001AF2C0's stores to them. */
    EmSceneState *scene = em_scene_state();
    em_scene_progress_reset_001AF2C0(scene);
    item_store(0x00810C60u, 0);          /* C60 */
    item_store(0x00810C7Bu, 1);          /* C7B = count[0x17] */
    item_store(0x00810CB7u, 0);          /* CB7 */
    item_store_half(0x00810CB2u, 0);     /* CB2 */
    item_store(0x00810C69u, 1);          /* C69 = count[5] */
    item_store(0x00810C64u, 1);          /* C64 = count[0] */
    item_store(0x00810C6Bu, 1);          /* C6B = count[7] */
    /* CA4 = 0xFF, CA5 = 5, CA6 = 0, CA7 = 7: the progress reset above. */
    /* C61 = 0 is em_weapon's fire mode (game_state_new_game). */
    /* 001C40B0(0x10, 2): count[0x10] += 2 and C63 += 2 over the zeroed
     * block. Its C62/CB4 stores are overwritten by 001AF2C0's own C62 = 30 /
     * CB4 = 60, which em_game mirrors (game_state_new_game). */
    item_store(0x00810C74u, 2);
    item_store(0x00810C63u, 2);
}

int em_pickup_light_submit(uint32_t source_id, const float c80[4])
{
    /* The +0x4C draw (001CACB0) of an item owner's 001C5680 child, called
     * from 001F54E0 inside the child's own behaviour (em_area11_bindings.c
     * tick_indicator): this frame's colour, drawn at the close-out. */
    if (!c80) return -1;
    for (int i=0;i<s.n_lights;++i) {
        PickupLight *light=&s.lights[i];
        Pickup *owner=&s.p[light->owner];
        if (!owner->used || owner->source_id!=source_id) continue;
        em_effect_color_gs(c80,light->tint);
        light->visible=1;
        return 0;
    }
    return -1;
}

int em_pickup_light_add(EmGfx *gfx, const char *scene_dir, int owner_uid,
                        const char *model_file)
{
    if (!gfx || !scene_dir || !model_file || owner_uid<=0 ||
        s.n_lights>=EM_PICKUP_MAX) return -1;
    int owner=-1;
    for (int i=0;i<s.n;++i)
        if (s.p[i].uid==owner_uid) {
            if (owner>=0 || s.p[i].prop) return -1;
            owner=i;
        }
    if (owner<0) return taken_bit(owner_uid) ? -2 : -1;
    for (int i=0;i<s.n_lights;++i)
        if (s.lights[i].owner==owner) return -1;
    int model=pickup_model_get(gfx,scene_dir,model_file);
    if (model<0) return -1;
    PickupLight *light=&s.lights[s.n_lights];
    memset(light,0,sizeof *light);
    light->owner=owner;
    light->model=model;
    return s.n_lights++;
}

void em_pickup_lights_draw(EmGfx *gfx, const float viewproj[16])
{
    for (int i=0;i<s.n_lights;++i) {
        PickupLight *light=&s.lights[i];
        Pickup *owner=&s.p[light->owner];
        if (!light->visible || !owner->used) continue;
        PickupModel *model=&s.models[light->model];
        /* Original model73 uses bone0; all three original nodes carry
         * the same parent placement matrix. Native EMDL also includes
         * its ordinary identity fallback slot. */
        for (uint32_t bone=0;bone<model->model.bone_count;++bone)
            memcpy(light->palette+bone*16,owner->palette,16*sizeof(float));
        em_gfx_draw_skinned_additive(gfx,model->mesh,viewproj,light->palette,
                                     model->model.bone_count,light->tint);
        light->visible=0; /* one draw per submitted 001F54E0 */
    }
}

int em_pickup_count(void) { return s.n; }

int em_pickup_draw(int i, EmGfxMesh **mesh, const float **palette,
                   uint32_t *bone_count)
{
    if (i < 0 || i >= s.n) return 0;
    Pickup *p = &s.p[i];
    if (!p->used || p->model < 0) return 0;
    if (p->original_bound && !p->original_visible) return 0;
    *mesh       = s.models[p->model].mesh;
    *palette    = p->palette;
    *bone_count = s.models[p->model].model.bone_count;
    return 1;
}

/* D_00810C64[t] is canonical for t < 0xBC (D_00810C64..D_00810D1F), except
 * t 0x50/0x51, which are em_weapon's reserve D_00810CB4. */
const uint8_t *em_pickup_items(void)        { return item_at(0x00810C64u, 0x50); }
uint8_t em_pickup_item_count(int type)
{
    const uint8_t *p = items_resolve(NULL, 0x00810C64u + ((unsigned)type & 0xFF), 1);
    return p ? *p : 0;
}
uint8_t em_pickup_mag_packs(void)           { return item_byte(0x00810C63u); }
const uint8_t *em_pickup_maps(void)         { return item_at(0x00810CB8u, 1); }
const uint8_t *em_pickup_keys(void)         { return item_at(0x00810CC3u, 1); }
int em_pickup_battery_charge(void)          { return item_half(0x00810CB2u); }
int em_pickup_battery_capacity(void)        { return item_byte(0x00810CB7u); }

void em_pickup_equipment_read(uint8_t *status, uint8_t *primary, uint8_t *secondary)
{
    if (status) *status = item_byte(0x00810C60u);
    if (primary) *primary = *equip_byte(0x00810CA4u);
    if (secondary) *secondary = *equip_byte(0x00810CA6u);
}

void em_pickup_equipment_write(uint8_t status, uint8_t primary, uint8_t secondary)
{
    item_store(0x00810C60u, status);
    *equip_byte(0x00810CA4u) = primary;
    *equip_byte(0x00810CA6u) = secondary;
}

static Pickup *bound(uint16_t uid);

static EmScriptCommandResult original_frame(void *context, EmScript *script,
                                            const unsigned char *record)
{
    Pickup *p = context;
    return em_interaction_runtime_frame(p->interaction, &p->original, script, record);
}

static int original_turn(void *context, float step)
{
    Pickup *p = context;
    return p->original_hooks.turn(p->original_hooks.context, p->source_id, p->pos, step);
}

static int original_camera(void *context, EmScript *script)
{
    Pickup *p = context;
    return p->original_hooks.camera(p->original_hooks.context, p->source_id, p->pos, script);
}

static int original_animation(void *context, uint16_t clip, float rate, float blend)
{
    Pickup *p = context;
    return em_interaction_runtime_animation_start(p->interaction, &p->original, clip, rate, blend);
}

static int original_animation_done(void *context)
{
    Pickup *p = context;
    return em_interaction_runtime_animation_done(p->interaction, &p->original);
}

/* 001C47A0's first call: 001C40B0(type, 1) over the item block. */
static int original_add_item(void *context, uint16_t type, int amount)
{
    (void)context;
    if (em_pickup_items_001C40B0(items_resolve, NULL, type, amount) == 0) return 1;
    fprintf(stderr, "pickup: 001C40B0(%#x, %d) reached a byte the port does not hold "
            "(em_pickup_set_weapon_ammo unbound, or outside the item block)\n", type, amount);
    return -1;
}

/* 001B6EA0: 001C47A0 / 001C4720 (D_00810CB8[t] += 1) / 001C4760
 * (D_00810CC3[t] += 1); the map and key bytes are the canonical item
 * block, so a type whose byte lies past D_00810D1F faults. */
static int original_take(void *context)
{
    Pickup *p = context;
    uint8_t *maps = item_at(0x00810CB8u, 1), *keys = item_at(0x00810CC3u, 1);
    const unsigned type = p->original.item_type;
    if (!maps || !keys ||
        (p->original.subtype == 1 && !item_at(0x00810CB8u + type, 1)) ||
        (p->original.subtype > 1 && !item_at(0x00810CC3u + type, 1))) {
        fprintf(stderr, "pickup: 001B6EA0 type %#x subtype %u: its byte is outside the item block\n",
                type, p->original.subtype);
        return 0;
    }
    EmPickupStatusRequest request = {0};
    if (em_pickup_owner_take(&p->original, maps, keys, &request,
                              original_add_item, p) != 1) return 0;
    return !request.kind || p->original_hooks.status_request(
        p->original_hooks.context, request.kind, request.index) == 1;
}

static int original_start(void *context, uint32_t entry, uint16_t clip)
{
    Pickup *p = context;
    return em_interaction_runtime_owns(p->interaction, &p->original) &&
           em_pickup_program_start(&p->program, entry, clip);
}

static int original_tick(void *context)
{
    Pickup *p = context;
    return em_interaction_runtime_owns(p->interaction, &p->original) ?
           em_pickup_program_tick(&p->program) : -1;
}

static int original_event(void *context, EmPickupOwnerEvent event, uint32_t argument)
{
    Pickup *p = context;
    switch (event) {
    case EM_PICKUP_OWNER_PERSIST:
        if (argument) {
            unsigned uid = (unsigned)p->uid & 0xFFFF;
            taken_set(uid);
        }
        return 1;
    case EM_PICKUP_OWNER_FREE:
        p->used = 0;
        p->original_visible = 0;
        return 1;
    case EM_PICKUP_OWNER_STOP_CHILD:
        for (int i=0; i<s.n_lights; ++i)
            if (&s.p[s.lights[i].owner] == p) s.lights[i].visible = 0;
        return 1;
    case EM_PICKUP_OWNER_DRAW:
        p->original_visible = 1;
        return 1;
    default:
        return p->original_hooks.event(p->original_hooks.context, p->source_id, event, argument);
    }
}

int em_pickup_original_bind(const EmInteractionSceneOwner *record,
    EmInteractionRuntime *interaction, const char *script_path, const EmPickupOriginalHooks *hooks)
{
    if (!record || record->role != EM_INTERACTION_PICKUP || !interaction || !script_path ||
        !hooks || !hooks->turn || !hooks->camera || !hooks->status_request || !hooks->event ||
        record->selector != 3 || record->item_type > 255 ||
        (record->callback != 0x15AFA0 && record->callback != 0x219550)) return 0;
    Pickup *p = NULL;
    for (int i=0; i<s.n; ++i) if (s.p[i].uid == record->uid && !s.p[i].prop) {
        if (p) return 0;
        p = &s.p[i];
    }
    if (!p || !p->used) {
        /* Taken: 001B6660's condition 1 does not spawn it (an instance
         * freed by an earlier binding stays freed). */
        if (!taken_bit(record->uid)) return 0;
        s.canonical_pickups = 1;
        return -2;
    }
    if (p->original_bound || p->type != (int)record->item_type) return 0;
    EmPickupProgramHooks program_hooks = {p, original_frame, original_turn, original_camera,
        original_animation, original_animation_done, original_take};
    if (!em_pickup_program_load(&p->program, script_path, record->callback, &program_hooks)) return 0;
    p->original = (EmPickupOwner){.callback=record->callback, .item_type=(uint16_t)record->item_type,
        .uid=(uint8_t)record->uid, .status=record->initial_status, .class_flags=record->class_flags,
        .subtype=record->subtype, .lifecycle=1, .child_status=1};
    for (int i=0; i<s.n_lights; ++i)
        if (&s.p[s.lights[i].owner] == p) p->original.has_child = 1;
    p->source_id = record->source_id;
    p->publication_rank = record->publication_rank;
    p->interaction = interaction;
    p->original_hooks = *hooks;
    p->original_bound = 1;
    memcpy(p->pos, record->position, sizeof p->pos);
    p->yaw = record->angles[1];
    pickup_build_palette(p);
    s.canonical_pickups = 1;
    return 1;
}

void em_pickup_original_unbind_all(void)
{
    for (int i=0; i<s.n; ++i) {
        Pickup *p = &s.p[i];
        if (!p->original_bound) continue;
        em_pickup_program_free(&p->program);
        memset(&p->original, 0, sizeof p->original);
        p->original_bound = p->original_visible = p->original_failed = 0;
        p->interaction = NULL;
    }
}

EmPickupOwner *em_pickup_original_owner(uint16_t uid)
{
    Pickup *p = bound(uid);
    return p ? &p->original : NULL;
}

int em_pickup_original_active(void) { return s.canonical_pickups; }

static int tick_owner(Pickup *p, float player_y, uint8_t action, uint8_t no_grab,
                      uint8_t scripted_frame)
{
    p->original_visible = 0;
    if (p->original_failed) return -1;
    EmPickupOwnerHooks hooks = {p, original_start, original_tick, original_event};
    int result = em_pickup_owner_tick(&p->original, p->pos[1], player_y, action,
                                      no_grab, scripted_frame, &hooks);
    if (result < 0) p->original_failed = 1;
    return result;
}

static Pickup *bound(uint16_t uid)
{
    for (int i=0; i<s.n; ++i)
        if (s.p[i].original_bound && s.p[i].uid == uid) return &s.p[i];
    return NULL;
}

int em_pickup_original_tick_one(uint16_t uid, float player_y, uint8_t action, uint8_t no_grab,
                                uint8_t scripted_frame)
{
    Pickup *p = bound(uid);
    return p ? tick_owner(p, player_y, action, no_grab, scripted_frame) : -1;
}

EmScript *em_pickup_original_script(uint16_t uid)
{
    Pickup *p = bound(uid);
    return p ? &p->program.script : NULL;
}

const float *em_pickup_original_position(uint16_t uid)
{
    Pickup *p = bound(uid);
    return p ? p->pos : NULL;
}

int em_pickup_original_tick(float player_y, uint8_t action, uint8_t no_grab,
                             uint8_t scripted_frame, int ordinary_tasks_enabled)
{
    if (!ordinary_tasks_enabled) return 1;
    uint32_t previous = 0;
    int first = 1;
    for (;;) {
        Pickup *next = NULL;
        for (int i=0; i<s.n; ++i) {
            Pickup *p = &s.p[i];
            if (!p->original_bound || (!first && p->publication_rank <= previous)) continue;
            if (!next || p->publication_rank < next->publication_rank) next = p;
        }
        if (!next) return 1;
        previous = next->publication_rank;
        first = 0;
        if (tick_owner(next, player_y, action, no_grab, scripted_frame) < 0) return -1;
    }
}

void em_pickup_battery_set_charge(int half_units)
{
    int capacity = em_pickup_battery_capacity();
    if (half_units < 0) half_units = 0;
    if (half_units > capacity) half_units = capacity;
    item_store_half(0x00810CB2u, (int16_t)half_units);
}

int em_pickup_battery_set_capacity_charge(uint16_t charge, uint8_t capacity)
{
    if (charge > capacity) return 0;
    item_store_half(0x00810CB2u, (int16_t)charge);
    item_store(0x00810CB7u, capacity);
    return 1;
}
