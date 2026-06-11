/* em_pickup.c — collectible items + display props (the decoded engine
 * pickup system; the decode ledger and the port-mapping flags live in
 * em_pickup.h).
 *
 * Engine shape per instance (one pooled actor, behavior func_0015AFA0):
 *   INIT  func_0015AC00  -> em_pickup_add (model bind + the rigid-prop
 *                           pose stamp func_001C6380: world TRS into
 *                           every bone slot)
 *   ARMED func_0015AE20  -> the take countdown inside em_pickup_update
 *   take  func_001B6EA0 -> func_001C47A0/4720/4760 -> func_001C40B0
 *                         -> inventory_add() below
 *   free  func_001B1190 + func_001AFC10 -> taken-bit set + slot dead
 */
#include "game/em_pickup.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_input.h"   /* EM_PAD_CROSS — the use-button mask */
#include "em_model.h"

#define PICKUP_PI 3.14159265358979f

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

static int taken_bit(int uid)
{
    if (uid <= 0) return 0;
    unsigned u = (unsigned)uid & 0xFFFF;
    return (g.taken[u >> 5] >> (u & 31)) & 1u;
}

int em_pickup_taken(int uid) { return taken_bit(uid); }

/* func_001C40B0 — the inventory-add switch. Every type counts into the
 * per-type array (default case, cap 99); case 0x10 = SPR4 MAGAZINE
 * runs the decoded pack/reserve math. The engine's non-default ammo
 * cases (0x11..0x1D: per-weapon percent pools D_00810CA8..) belong to
 * the unported weapon variants — they take the default count here
 * (FLAGGED in em_pickup.h). */
static void inventory_add(int type, int n)
{
    unsigned t = (unsigned)type & 0xFF;
    int c = g.count[t] + n;
    g.count[t] = (uint8_t)(c > 99 ? 99 : c);
    if (t == EM_PICKUP_TYPE_MAG) {
        int packs = g.mag_packs + n;
        /* engine: reserve += 30*n; >= 99 packs fold into the reserve
         * and clamp to 98 (the fold is a no-op for the port's single
         * total-rounds pool — the +30s already went in). */
        g.ammo_pending += 30 * n;
        if (packs >= 99) {
            packs = 98;
            g.count[t] = 98;
        }
        g.mag_packs = (uint8_t)packs;
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

/* func_001C6380 — the rigid-prop pose: world TRS (here T * R_y(yaw))
 * applied to every bone slot, baked once (items never animate). */
static void pickup_build_palette(Pickup *p)
{
    if (p->model < 0) return;
    PickupModel *pm = &s.models[p->model];
    uint32_t n = pm->model.bone_count;
    memcpy(p->palette, pm->base, n * 16 * sizeof(float));
    const float c = cosf(p->yaw), sn = sinf(p->yaw);
    for (uint32_t b = 0; b < n; b++) {
        float *m = p->palette + b * 16;
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
    if (model_file) {
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
 * decoded condition ledger is in em_pickup.h). One nearest winner per
 * CROSS press edge. */
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
            /* facing: within pi/4 of the bearing to the item (engine
             * atan2 convention: bearing = atan2(dx, dz)) */
            float fd = pickup_norm_ang(atan2f(dx, dz) - pyaw);
            if (fabsf(fd) > PICKUP_PI * 0.25f) continue;
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

/* func_001B6EA0 + the despawn tail of func_0015AFA0 states 1->2. */
static void pickup_take(Pickup *p)
{
    inventory_add(p->type, 1);
    g.found_pending = p->type;           /* D_008106B0/B1 request */
    if (p->uid > 0) {                    /* func_001B1190 */
        unsigned u = (unsigned)p->uid & 0xFFFF;
        g.taken[u >> 5] |= 1u << (u & 31);
    }
    p->used = 0;                         /* func_001AFC10 — despawn */
    printf("pickup: took type %#04x (count %u, uid %#06x)\n",
           p->type, g.count[p->type & 0xFF], (unsigned)p->uid);
}

void em_pickup_update(const float player_pos[3], float player_yaw,
                      const EmFrameInput *in, int scan)
{
    if (scan)
        pickup_trigger_scan(player_pos, player_yaw, in);
    for (int i = 0; i < s.n; i++) {
        Pickup *p = &s.p[i];
        if (!p->used || !p->armed) continue;
        /* the armed handler func_0015AE20: the take script runs for a
         * couple of scripted frames, then the op-9 take fires and the
         * actor frees (instant-script shape — em_pickup.h flags). */
        if (--p->take_t <= 0)
            pickup_take(p);
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
