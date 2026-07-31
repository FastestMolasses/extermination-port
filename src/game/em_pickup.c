/* em_pickup.c — collectible items + display props (the decoded engine
 * pickup system; the decode ledger and the port-mapping flags live in
 * em_pickup.h).
 *
 * Engine shape per instance (one pooled actor, behavior func_0015AFA0 —
 * BYTE-MATCHED in src/func_0015AFA0.c, a switch on the actor state byte
 * +0x04 that dispatches exactly the three legs below):
 *   INIT  func_0015AC00  -> em_pickup_add (scale switch + model bind +
 *                           the rigid-prop pose stamp func_001C6380:
 *                           world TRS into every bone slot)
 *   ARMED func_0015AE20  -> the take countdown inside em_pickup_update
 *   take  func_001B6EA0 -> func_001C47A0/4720/4760 -> func_001C40B0
 *                         -> inventory_add() below
 *   free  func_001B1190(actor[+0x9A]) + func_001AFC10 -> taken-bit set
 *                         + slot dead
 */
#include "game/em_pickup.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_input.h"   /* EM_PAD_CROSS — the use-button mask */
#include "em_model.h"
#include "game/em_game.h"  /* em_game_set_battery — contract-A (em_game.c) */

#define PICKUP_PI 3.14159265358979f

/* The AREA-11 battery key-item. The engine's placement record 10
 * (behavior ov 0x00823E80) is item TYPE 0x11; its take path does
 * `li 0xFF; sb -> D_00810811` — the "battery in inventory" byte. The
 * port routes that flag through em_game_set_battery on the take (the
 * collectible reaches em_pickup via the manifest `pickup 0x11 ...`
 * line the ELEVATOR parser emits with the trailing "battery" token;
 * item type 0x11 is the engine-true identifier — INVESTIGATION_area11_
 * elevator.md §2). */
#define EM_PICKUP_TYPE_BATTERY  0x11

/* AREA-11 ITEM-DISPLAY decor (placement records 1/2, behavior ov
 * 0x00827630, param 0x13 -> mesh area_item_13.emdl). It is a ROTATING
 * display: the engine actor's per-frame heading at +0xC8 is fed into the
 * rigid-prop pose builder func_001C6380 (= pickup_build_palette below)
 * every frame, so the whole prop spins about its vertical (Y/heading)
 * axis — the SAME axis as the baked R_y(yaw) here.
 *
 * PROVENANCE (rechecked 2026-07-31): the numbers below came from a LIVE PCSX2
 * read of overlay code at ov 0x00827630 (@0x82776c.. / common tail @0x82784c
 * `add.s f0=[+0xC8],f1=[+0x38]`) in an earlier session. Overlay code is NOT in
 * the decomp corpus — there is no recovered C at 0x0082xxxx and FINDINGS.md
 * does not mention 0x00827630 — so NONE of it is re-checkable here. Treat it
 * as an OBSERVATION, not a decode: the engine spin was read as a TRIGGERED
 * spin-up/hold/spin-down, the per-frame step at actor+0x38 ramping from 0 at
 * 0.0029088 rad/f^2 (0x3B3EA2F2) to a cap of 0.349066 rad/f (0x3EB2B8C3 =
 * 20 deg/frame), holding, then ramping back to 0, with heading += step each
 * frame. The trigger was read as the story/insert state (state byte +0x05),
 * which the port's scripting spine does not model, so a faithful TRIGGER is
 * not reproducible.
 * Reproduce the OBSERVABLE outcome — a rotating display — as a SUBTLE
 * continuous Y-spin at a small fraction of the decoded active cap. This is
 * additive cosmetic decor; it is inherently AREA-11-only because type 0x13
 * display props exist solely in scene_snow's manifest (no other scene ships
 * a `pickup 0x13`). FLAGGED: continuous rate is a faithful stand-in, not the
 * byte-exact triggered ramp (em_pickup.h). */
#define EM_PICKUP_TYPE_DISPLAY  0x13
#define PICKUP_DISPLAY_SPIN     0.0174532925f   /* rad/frame ~1 deg/f, a
                                                 * subtle decor spin (the
                                                 * OBSERVED active cap was
                                                 * 0.349 rad/f — see the
                                                 * provenance note above;
                                                 * this is a gentle idle
                                                 * fraction of it) */

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
    uint8_t armed;        /* actor +0x0B (the scan writes 4) */
    int     take_t;       /* armed countdown, EM_PICKUP_TAKE_FRAMES.. */
    float   spin_rate;    /* per-frame Y-spin (rad/f); 0 = static prop.
                           * Set for AREA-11 item-display props (type 0x13,
                           * ov 0x00827630): the engine spins the whole prop
                           * about its heading +0xC8 each frame. */
    float   spin_ang;     /* accumulated spin yaw, added to the baked R_y */
    float   scale;        /* actor +0x60..+0x68 — func_0015AC00's INIT
                           * scale switch, the S leg of func_001C6380's
                           * world TRS (1.0 / 1.5 / 2.0) */
    float   palette[PICKUP_BONE_MAX * 16];
} Pickup;

static struct {
    PickupModel models[PICKUP_MODEL_MAX];
    int         n_models;
    Pickup      p[EM_PICKUP_MAX];
    int         n;            /* slots in use (dead slots stay counted
                               * until the scene clears — draw returns
                               * 0 for them, like the enemy contract) */
} s;

/* --- persistent game state (survives scene clears — the engine's
 * D_00810700-block globals; wiped only by em_pickup_reset) ----------- */
static struct {
    uint8_t  count[256];      /* D_00810C64 mirror: u8 per item type */
    uint8_t  mag_packs;       /* D_00810C63 mirror */
    uint32_t taken[2048];     /* D_00810860 mirror: bit (area<<8)|puid
                               * (engine: u32[8] x area — same bits,
                               * one flat array) */
    int      ammo_pending;    /* case-0x10 reserve rounds for em_game */
    int      found_pending;   /* item type for the Found line, -1 none */
} g = { {0}, 0, {0}, 0, -1 };

/* ------------------------------------------------------------------ */

/* func_001B11E0 — the taken-bit test. The engine's argument is the
 * one-byte PUID and BOTH the test (func_001B11E0) and the set
 * (func_001B1190, byte-matched: `v1 = a0 & 0xff; if (v1 == 0) return;`)
 * bail out when that byte is zero. The port's flat uid is
 * (area << 8) | puid, so the guard is on the LOW BYTE — a puid-0 record
 * in a non-zero area (uid 0x0100, 0x0B00, ...) must not persist either.
 * (Was `uid <= 0`, which persisted exactly those and permanently
 * despawned an item the engine re-spawns on every area re-entry.) */
static int taken_bit(int uid)
{
    if (uid <= 0 || (uid & 0xFF) == 0) return 0;
    unsigned u = (unsigned)uid & 0xFFFF;
    return (g.taken[u >> 5] >> (u & 31)) & 1u;
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
 * em_pickup.h), as do the engine's per-weapon meter cases 0x11..0x16
 * and the raw-store case 0x0F, which take the default count here. */
static void inventory_add(int type, int n)
{
    unsigned t = (unsigned)type & 0xFF;

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

/* func_001C6380 — the rigid-prop pose: world TRS (here T * R_y(yaw) * S)
 * applied to every bone slot, baked once (items never animate). */
static void pickup_build_palette(Pickup *p)
{
    if (p->model < 0) return;
    PickupModel *pm = &s.models[p->model];
    uint32_t n = pm->model.bone_count;
    memcpy(p->palette, pm->base, n * 16 * sizeof(float));
    /* Spinning display props (type 0x13, ov 0x00827630) add the per-frame
     * accumulated yaw on top of the placement yaw — the engine feeds
     * heading(+0xC8) into this same pose builder (func_001C6380) each
     * frame, so the whole prop rotates about its vertical axis. spin_ang
     * is 0 for every other prop (static pose, baked once). */
    const float ry = p->yaw + p->spin_ang;
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
    /* AREA-11 ITEM-DISPLAY (records 1/2, ov 0x00827630, param 0x13): a
     * rotating display — give it a subtle continuous Y-spin. Only the
     * placed display PROP gets it (never a collectible); type 0x13 props
     * exist solely in scene_snow, so this is inherently scene-isolated. */
    if (prop && (type & 0xFF) == EM_PICKUP_TYPE_DISPLAY)
        p->spin_rate = PICKUP_DISPLAY_SPIN;
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
    for (int i = 0; i < s.n_models; i++) {
        em_gfx_mesh_destroy(gfx, s.models[i].mesh);
        em_model_free(&s.models[i].model);
    }
    memset(&s, 0, sizeof s);
    /* g (inventory + taken bits + pending events) deliberately
     * survives — see em_pickup.h. */
}

void em_pickup_reset(void)
{
    memset(&g, 0, sizeof g);
    g.found_pending = -1;
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
    }
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
    g.found_pending = p->type;           /* D_008106B0/B1 request */
    /* func_001B1190 (byte-matched): the engine is handed the one-byte
     * puid from actor +0x9A and returns without touching the array when
     * that byte is 0 — the guard is on the PUID BYTE, not on the port's
     * composite (area << 8) | puid. */
    if (p->uid > 0 && (p->uid & 0xFF) != 0) {
        unsigned u = (unsigned)p->uid & 0xFFFF;
        g.taken[u >> 5] |= 1u << (u & 31);
    }
    /* The AREA-11 battery key-item: its take sets the engine's
     * D_00810811 = 0xFF (INVESTIGATION_area11_elevator.md §2). Route
     * that through the persistent game state so the terminal's
     * powered-vs-refusal branch (em_examine.c, contract C) can gate on
     * it. The taken-bit above already makes the battery persist (it
     * never re-spawns once taken — em_pickup_add's cond-1 suppression);
     * this just mirrors the inventory byte. */
    if ((p->type & 0xFF) == EM_PICKUP_TYPE_BATTERY)
        em_game_set_battery(1);
    p->used = 0;                         /* func_001AFC10 — despawn */
    printf("pickup: took type %#04x (count %u, uid %#06x)%s\n",
           p->type, g.count[p->type & 0xFF], (unsigned)p->uid,
           (p->type & 0xFF) == EM_PICKUP_TYPE_BATTERY ? " [BATTERY]" : "");
}

void em_pickup_update(const float player_pos[3], float player_yaw,
                      const EmFrameInput *in, int scan)
{
    if (scan)
        pickup_trigger_scan(player_pos, player_yaw, in);
    /* Spinning display props (AREA-11 item-display, ov 0x00827630): advance
     * the Y-spin and re-bake the rigid-prop pose this frame. The engine does
     * exactly this — heading(+0xC8) += step each frame, then func_001C6380
     * rebuilds the pose. Runs regardless of the take/scan gates (decor, not a
     * collectible) and is a no-op for every other prop (spin_rate 0). */
    for (int i = 0; i < s.n; i++) {
        Pickup *p = &s.p[i];
        if (!p->used || p->spin_rate == 0.0f) continue;
        p->spin_ang += p->spin_rate;
        if (p->spin_ang >= PICKUP_PI * 2.0f) p->spin_ang -= PICKUP_PI * 2.0f;
        pickup_build_palette(p);
    }
    for (int i = 0; i < s.n; i++) {
        Pickup *p = &s.p[i];
        if (!p->used || !p->armed) continue;
        /* the armed handler func_0015AE20: the take script runs for a
         * couple of scripted frames, then the op-9 take fires and the
         * actor frees. The take plays the height-selected player grab
         * clip + lock (player_pos[1] selects 0x40..0x42 — em_pickup.h
         * flags) before the synchronous take effect. */
        if (--p->take_t <= 0)
            pickup_take(p, player_pos[1]);
    }
}

int em_pickup_count(void) { return s.n; }

int em_pickup_draw(int i, EmGfxMesh **mesh, const float **palette,
                   uint32_t *bone_count)
{
    if (i < 0 || i >= s.n) return 0;
    Pickup *p = &s.p[i];
    if (!p->used || p->model < 0) return 0;
    *mesh       = s.models[p->model].mesh;
    *palette    = p->palette;
    *bone_count = s.models[p->model].model.bone_count;
    return 1;
}

const uint8_t *em_pickup_items(void)        { return g.count; }
uint8_t em_pickup_item_count(int type)      { return g.count[type & 0xFF]; }
uint8_t em_pickup_mag_packs(void)           { return g.mag_packs; }

int em_pickup_ammo_take(void)
{
    int n = g.ammo_pending;
    g.ammo_pending = 0;
    return n;
}

int em_pickup_found_take(void)
{
    int t = g.found_pending;
    g.found_pending = -1;
    return t;
}
