/* Pickup render instances and persistent inventory.
 * Canonical AREA11 owners/programs enter through em_pickup_original.h.
 * Unbound scenes retain an explicitly legacy scan/countdown path below.
 */
#include "game/em_pickup.h"
#include "game/em_pickup_original.h"
#include "game/em_pickup_program.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_input.h"   /* EM_PAD_CROSS — the use-button mask */
#include "em_model.h"
#include "game/em_game.h"  /* em_game_player_interact_anim (em_game.c) */
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
    uint8_t armed;        /* actor +0x0B (the scan writes 4) */
    int     take_t;       /* armed countdown, EM_PICKUP_TAKE_FRAMES.. */
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
    int initialized;
    int visible;
    float color[4];
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

/* --- persistent game state (survives scene clears — the engine's
 * D_00810700-block globals; wiped only by em_pickup_reset) ----------- */
static struct {
    uint8_t  count[256];      /* D_00810C64 mirror: u8 per item type */
    uint8_t  maps[256], keys[256]; /* separate original CB8 / CC3 families */
    uint8_t  status;          /* D_00810C60 mirror. CA4/CA6 (primary,
                               * secondary) live in the canonical D2
                               * progress region since S10b: equip_byte() */
    uint8_t  mag_packs;       /* D_00810C63 mirror */
    int16_t  battery_charge;  /* D_00810CB2: internal half-units */
    uint8_t  battery_capacity;/* D_00810CB7: internal half-units */
    int      ammo_pending;    /* case-0x10 reserve rounds for em_game */
} g;

/* This frame's use-scan winner (func_00184BA0's single winner for the
 * whole interactive list). Reset at every em_pickup_update entry; read
 * by em_examine, which scans second — em_pickup.h "ONE WINNER PER
 * PRESS". scan_dist is the PLANAR distance the engine parks at spad
 * 0x70003B98 and compares with `<`. */
static int   scan_slot = -1;
static float scan_dist;

/* ------------------------------------------------------------------ */

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

/* func_001C40B0 — the inventory-add switch, mirroring the recovered C
 * (src/func_001C40B0.c, NEARMISS — logic authoritative):
 *
 *   default:  count[type] += n;  if (count[type] >= 100) count[type] = 99;
 *   case 0x10 (SPR4 MAGAZINE): count += n; packs += n; reserve += 30*n;
 *             if (loaded_mag == 0) loaded_mag = 30;
 *             if (packs >= 99) { reserve -= (packs - 98) * 30;
 *                                packs = count = 98; }
 *
 * Two corrections over the previous port version: case 0x10 is NOT
 * subject to the default cap-99 clamp (it has its own 98 cap), and the
 * over-cap step SUBTRACTS the surplus packs' rounds from the reserve —
 * the old comment called it a fold-in no-op. The loaded-magazine
 * auto-fill is em_weapon's state and stays unported (FLAGGED in
 * em_pickup.h).
 *
 * FLAGGED (audit 2026-07-31): the engine's switch has 14 more arms, and
 * `default` is NOT the only one that writes the count array — cases
 * 0x01/0x02/0x03/0x04/0x0C/0x0D/0x0E also do
 * `D_00810C64[arg0] += arg1`, UNCLAMPED, alongside a linked meter, while
 * 0x11..0x16 move only a meter and 0x0F is a raw `= n`. Full ledger in
 * em_pickup.h. The port keeps the uniform default clamp on purpose: it
 * already folds take families 1/2 into this one array, so `type` here is
 * not reliably the engine's stat index. Battery types 0x1B..0x1D are
 * proven by AREA11's original deferred-item records and preserve their
 * own count and half-unit meter semantics below. */
static void inventory_add(int type, int n)
{
    unsigned t = (unsigned)type & 0xFF;

    if (t >= 0x1B && t <= 0x1D) {
        static const int capacity[3] = {12, 36, 48};
        int units = capacity[t - 0x1B];
        /* The original count store wraps at 8 bits; only charge is
         * capped. Finding a smaller pack never shrinks the capacity. */
        g.count[t] = (uint8_t)(g.count[t] + n);
        g.battery_charge = (int16_t)(g.battery_charge + n * units);
        if (g.battery_capacity < units)
            g.battery_capacity = (uint8_t)units;
        if (g.battery_charge > g.battery_capacity)
            g.battery_charge = g.battery_capacity;
        return;
    }

    if (t == EM_PICKUP_TYPE_MAG) {
        int packs  = g.mag_packs + n;
        int count  = g.count[t] + n;
        int rounds = 30 * n;
        if (packs >= 99) {
            rounds -= (packs - 98) * 30;   /* the engine's reserve -= */
            if (rounds < 0) rounds = 0;    /* port: the pending queue
                                            * is one-shot, never negative */
            packs = 98;
            count = 98;
        }
        g.mag_packs    = (uint8_t)packs;
        g.count[t]     = (uint8_t)count;
        g.ammo_pending += rounds;
        return;
    }
    {
        int c = g.count[t] + n;
        g.count[t] = (uint8_t)(c >= 100 ? 99 : c);
    }
}

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
    scan_slot = -1;                      /* slot indices are now stale */
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
    /* The D_00810700 memset for the canonical D2 bytes (taken bits and
     * CA4..CA7 among them), with 001AF2C0's stores to them. */
    em_scene_progress_reset_001AF2C0(em_scene_state());
    memset(&g, 0, sizeof g);
    g.status = 0;                        /* C60 */
    g.count[0x00] = 1;                   /* C64 */
    g.count[0x05] = 1;                   /* C69 */
    g.count[0x07] = 1;                   /* C6B */
    g.count[0x17] = 1;                   /* C7B */
    /* CA4 = 0xFF, CA6 = 0: written by the progress reset above. */
    g.battery_charge = 0;                /* CB2 */
    g.battery_capacity = 0;              /* CB7 */
    g.count[EM_PICKUP_TYPE_MAG] = 2;     /* 001C40B0(0x10, 2): C74 += 2 */
    g.mag_packs = 2;                     /*                    C63 += 2 */
}

/* Wrap an angle to (-pi, pi] — the engine's func_001B1470. */
static float pickup_norm_ang(float a)
{
    while (a >  PICKUP_PI) a -= 2.0f * PICKUP_PI;
    while (a < -PICKUP_PI) a += 2.0f * PICKUP_PI;
    return a;
}

/* func_00184BA0 + func_00183EF0 archetype 3 — the item use scan (the
 * condition ledger, with its 2026-07-31 re-verification notes, is in
 * em_pickup.h). One nearest winner per CROSS press edge, matching
 * func_00184BA0's "keep the smallest parked distance" walk. */
static void pickup_trigger_scan(const float pp[3], float pyaw,
                                const EmFrameInput *in)
{
    if (!(in->pressed & EM_PAD_CROSS))
        return;

    int   best = -1;
    float best_d2 = 1e30f;
    for (int i = 0; i < s.n; i++) {
        Pickup *p = &s.p[i];
        if (!p->used || p->prop || p->armed) continue;
        float dx = p->pos[0] - pp[0];
        float dz = p->pos[2] - pp[2];
        float d2 = dx * dx + dz * dz;
        if (d2 > EM_PICKUP_RADIUS * EM_PICKUP_RADIUS) continue;
        float dy = pp[1] - p->pos[1];          /* player above: + */
        if (dy >= 0.0f ? dy > EM_PICKUP_DY_UP
                       : -dy > EM_PICKUP_DY_DOWN) continue;
        if (d2 > EM_PICKUP_AUTO_RING * EM_PICKUP_AUTO_RING) {
            /* facing: within EM_PICKUP_FACING (pi/2 — func_00183EF0's
             * case-3/4 gate, see em_pickup.h) of the bearing to the
             * item (engine atan2 convention: bearing = atan2(dx, dz);
             * the engine forms player_yaw - bearing, identical under
             * fabs) */
            float fd = pickup_norm_ang(atan2f(dx, dz) - pyaw);
            if (fabsf(fd) > EM_PICKUP_FACING) continue;
        }
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    if (best >= 0) {
        s.p[best].armed  = 4;            /* the scan's +0x0B value */
        s.p[best].take_t = EM_PICKUP_TAKE_FRAMES;
        /* publish the winner for the cross-module single-winner rule
         * (func_00184BA0 arms exactly one object per press; the engine
         * compares the PLANAR distance it parks at spad 0x70003B98) */
        scan_slot = best;
        scan_dist = sqrtf(best_d2);
    }
}

int em_pickup_scan_dist(float *out_dist)
{
    if (scan_slot < 0) return 0;
    if (out_dist) *out_dist = scan_dist;
    return 1;
}

void em_pickup_scan_release(void)
{
    if (scan_slot < 0) return;
    s.p[scan_slot].armed  = 0;           /* +0x0B back to 0 */
    s.p[scan_slot].take_t = 0;
    scan_slot = -1;
}

/* func_0015AE20's GRAB-ANIM patch (D_00248354 = rec[+0x14] of the op-A
 * record): the player pick-up clip is chosen by ITEM HEIGHT vs player.y
 * (D_00810354) — item.y < py+6 -> 0x42 (low), < py+13 -> 0x41 (mid),
 * else 0x40 (high). FINDINGS "ITEM PICKUP SYSTEM" §4. py is the player's
 * world Y at the take; `item_y` is the placement Y (actor +0xB4). */
static int pickup_grab_clip(float item_y, float player_y)
{
    if (item_y < player_y + 6.0f)  return EM_PICKUP_GRAB_LOW;   /* 0x42 */
    if (item_y < player_y + 13.0f) return EM_PICKUP_GRAB_MID;   /* 0x41 */
    return EM_PICKUP_GRAB_HIGH;                                 /* 0x40 */
}

/* func_001B6EA0 + the despawn tail of func_0015AFA0 states 1->2. The
 * GRAB-ANIM take branch (func_0015AE20, the non-instant path) plays the
 * height-selected player grab clip ON THE PLAYER and locks input/movement
 * for its duration, THEN the op-9 take fires (count++/taken-bit/despawn).
 * The port keeps the take effect synchronous and adds the visible grab
 * clip + lock at the same instant: em_game_player_interact_anim raises
 * the scripted-anim lock (it owns the player until the clip ends — the
 * same lock the terminals use), and the inventory/persistence below is
 * unchanged. `player_y` selects the clip. */
static void pickup_take(Pickup *p, float player_y)
{
    em_game_player_interact_anim(pickup_grab_clip(p->pos[1], player_y));
    inventory_add(p->type, 1);
    /* 001B6EA0 take family 0 -> 001C47A0 (byte-matched): 001C40B0, then
     * D_008106B0 = 1 and D_008106B1 = type. The classifier 001AE7E0 then
     * returns 2 and 0x1AE040 opens the status screen, whose 0020CDC0 case 0
     * maps a battery (B1 0x1B..0x1D) to the ITEM page, message 3 (the
     * host's request route: em_status_page, the ITEM root, the BATTERY
     * page's acquisition notice). The battery types are family 0 in
     * AREA11's deferred-item records (scene.txt). The pages every other
     * take selects have no translation (0020CDC0: MAP 0020F950 for B0 = 2,
     * SPR4 00211970 for B1 < 0x17, DATABASE 00214020 for B0 = 3, the ITEM
     * child 002160B0 for B1 0x1E..0x22), so those takes post no request
     * and show nothing until they are translated (WP-6). */
    if (p->type >= 0x1B && p->type <= 0x1D) {
        EmSceneState *scene = em_scene_state();
        scene->req[EM_SCENE_REQ_B0] = 1;
        scene->req[EM_SCENE_REQ_B1] = (uint8_t)p->type;
    } else {
        /* The withheld request is reported, never silent. The legacy take
         * does not carry the record's family byte (+3), which selects
         * 001C47A0/4720/4760 (B0 = 1/2/3), so the page is named by the
         * type as 0020CDC0 case 0 would map it under family 0. */
        const char *page = p->type < 0x17 ? "SPR4 00211970" : "the ITEM child 002160B0";
        fprintf(stderr, "pickup: take of type %#04x withholds its 001B6EA0 status request "
                "(B0 = 1/2/3, B1 = type): 0020CDC0 would open %s (or MAP 0020F950 / "
                "DATABASE 00214020 for families 1/2), which is not translated (WP-5/WP-6)\n",
                p->type, page);
    }
    /* func_001B1190 (byte-matched): the engine is handed the one-byte
     * puid from actor +0x9A and returns without touching the array when
     * that byte is 0 — the guard is on the PUID BYTE, not on the port's
     * composite (area << 8) | puid. */
    if (p->uid > 0 && (p->uid & 0xFF) != 0) {
        unsigned u = (unsigned)p->uid & 0xFFFF;
        taken_set(u);
    }
    /* The former type-0x11 hook here mirrored D_00810811 as a "battery"
     * flag. That byte is the AREA11 opening controller's completion flag
     * (overlay 0x00823F74..80), and AREA11 record 10 is that controller,
     * not a type-0x11 pickup, so the hook was removed. */
    p->used = 0;                         /* func_001AFC10 — despawn */
    printf("pickup: took type %#04x (count %u, uid %#06x)\n",
           p->type, g.count[p->type & 0xFF], (unsigned)p->uid);
}

void em_pickup_update_owners(const float player_pos[3], float player_yaw,
                             const EmFrameInput *in, int scan)
{
    scan_slot = -1;                      /* last frame's winner expires */
    if (scan && !s.canonical_pickups)
        pickup_trigger_scan(player_pos, player_yaw, in);
    for (int i = 0; i < s.n; i++) {
        Pickup *p = &s.p[i];
        if (!p->used || p->original_bound || !p->armed) continue;
        /* the armed handler func_0015AE20: the take script runs for a
         * couple of scripted frames, then the op-9 take fires and the
         * actor frees. The take plays the height-selected player grab
         * clip + lock (player_pos[1] selects 0x40..0x42 — em_pickup.h
         * flags) before the synchronous take effect. */
        if (--p->take_t <= 0)
            pickup_take(p, player_pos[1]);
    }
}

void em_pickup_lights_tick(void)
{
    /* 001C5680: initialize without drawing once, then run 001F54E0 every
     * ordinary frame, including while the opening owns player controls.
     * Its even-frame stack copy is unused by the original call, so it
     * does not change the supplied brightness amplitude. */
    for (int i=0;i<s.n_lights;++i) {
        PickupLight *light=&s.lights[i];
        Pickup *owner=&s.p[light->owner];
        light->visible=0;
        if (!owner->used) continue;
        if (owner->original_bound && owner->original.child_status == 3) continue;
        if (!light->initialized) {
            light->initialized=1;
            continue;
        }
        em_effect_color(em_random_next(),light->color,light->tint);
        light->visible=1;
    }
}

void em_pickup_update(const float player_pos[3], float player_yaw,
                      const EmFrameInput *in, int scan)
{
    em_pickup_update_owners(player_pos, player_yaw, in, scan);
    em_pickup_lights_tick();
}

int em_pickup_light_add(EmGfx *gfx, const char *scene_dir, int owner_uid,
                        const char *model_file, const float color[4])
{
    if (!gfx || !scene_dir || !model_file || !color || owner_uid<=0 ||
        s.n_lights>=EM_PICKUP_MAX) return -1;
    for (int channel=0;channel<4;++channel)
        if (!isfinite(color[channel]) || color[channel]<0.0f ||
            color[channel]>1.0f) return -1;
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
    memcpy(light->color,color,sizeof light->color);
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

const uint8_t *em_pickup_items(void)        { return g.count; }
uint8_t em_pickup_item_count(int type)      { return g.count[type & 0xFF]; }
uint8_t em_pickup_mag_packs(void)           { return g.mag_packs; }
int em_pickup_battery_charge(void)          { return g.battery_charge; }
int em_pickup_battery_capacity(void)        { return g.battery_capacity; }

const uint8_t *em_pickup_maps(void) { return g.maps; }
const uint8_t *em_pickup_keys(void) { return g.keys; }

void em_pickup_equipment_read(uint8_t *status, uint8_t *primary, uint8_t *secondary)
{
    if (status) *status = g.status;
    if (primary) *primary = *equip_byte(0x00810CA4u);
    if (secondary) *secondary = *equip_byte(0x00810CA6u);
}

void em_pickup_equipment_write(uint8_t status, uint8_t primary, uint8_t secondary)
{
    g.status = status;
    *equip_byte(0x00810CA4u) = primary;
    *equip_byte(0x00810CA6u) = secondary;
}

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

static int original_add_item(void *context, uint16_t type, int amount)
{
    (void)context;
    inventory_add(type, amount);
    return 1;
}

static int original_take(void *context)
{
    Pickup *p = context;
    EmPickupStatusRequest request = {0};
    if (em_pickup_owner_take(&p->original, g.maps, g.keys, &request,
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
    if (!p) {
        if (!taken_bit(record->uid)) return 0;
        s.canonical_pickups = 1;
        return -2;
    }
    if (p->original_bound || !p->used || p->type != (int)record->item_type) return 0;
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
    scan_slot = -1;
    return 1;
}

EmPickupOwner *em_pickup_original_owner(uint16_t uid)
{
    for (int i=0; i<s.n; ++i)
        if (s.p[i].original_bound && s.p[i].uid == uid) return &s.p[i].original;
    return NULL;
}

int em_pickup_original_active(void) { return s.canonical_pickups; }

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
        next->original_visible = 0;
        if (next->original_failed) return -1;
        EmPickupOwnerHooks hooks = {next, original_start, original_tick, original_event};
        if (em_pickup_owner_tick(&next->original, next->pos[1], player_y, action,
                                  no_grab, scripted_frame, &hooks) < 0) {
            next->original_failed = 1;
            return -1;
        }
    }
}

void em_pickup_battery_set_charge(int half_units)
{
    if (half_units < 0) half_units = 0;
    if (half_units > g.battery_capacity) half_units = g.battery_capacity;
    g.battery_charge = (int16_t)half_units;
}

int em_pickup_battery_set_capacity_charge(uint16_t charge, uint8_t capacity)
{
    if (charge > capacity) return 0;
    g.battery_charge = (int16_t)charge;
    g.battery_capacity = capacity;
    return 1;
}

int em_pickup_ammo_take(void)
{
    int n = g.ammo_pending;
    g.ammo_pending = 0;
    return n;
}

