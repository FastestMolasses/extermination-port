/* em_enemy.c — placed crawler actors (see em_enemy.h for the engine
 * mapping and the flagged fidelity deviations).
 *
 * State machine (FINDINGS "ENEMY AI ARCHITECTURE" section 3,
 * func_001551B0, engine lifecycle values kept):
 *
 *   0 INIT    HP(+0x34) = 1, base heading, build the 4 diagonal probe
 *             directions (+0x2D0..0x2EC) -> 4. (The engine also waits
 *             for resources and resolves the per-area nest registry —
 *             natively immediate, no nest children yet.)
 *   4 IDLE    poll the +0x36 mailbox: ANY damage kills (HP = 1) -> 2,
 *             and broadcast the group alarm (+0x0A) to every other live
 *             crawler. Own alarm set -> 1 with the retreat counter
 *             +0x2A = 6. PORT WAKE (flagged): player within the
 *             documented 32-unit distance-only sense -> alarm self.
 *   1 ATTACK  sub 0 STEER: turn toward the player at most +-3 deg/frame
 *             (0.0524 rad), probing the 4 diagonals (func_0019AB20
 *             stand-in: knee-height segment queries) and turning away
 *             from blocked sides; >= 3 blocked and the retreat counter
 *             exhausted -> back to 4. Then launch a hop.
 *             sub 1 HOP: integrate velocity, vertical with the engine's
 *             0.052/tick gravity; land on the floor query -> sub 0.
 *             Within the radius-6 lunge contact of the player -> write
 *             the player mailbox (0x400A) -> 2: the suicide-attack
 *             burst. (Port deviation: the mailbox is also polled here.)
 *   2 DEATH   engine: nest-child spawns + gore FX + a MODEL REBIND to
 *             the burst-husk/gib models (library 0x22/0x29 — there is
 *             NO death clip in the leech clip bank), and — when killed
 *             by DAMAGE (+0x36 nonzero, variants 6/0x1E) — knockback:
 *             the hit vector in scratch D_700036E0 RNG-rotated
 *             (90/180/270 deg), velocity set from it, then a corpse
 *             slide that settles on the floor. Port: gameplay despawns
 *             immediately; on a LETHAL HIT the visual layer launches
 *             3-5 GIB instances (the exported library burst set,
 *             assets/gibs/ — see "GIB LAYER" below) with exactly that
 *             knockback shape; the contact/suicide burst (mailbox
 *             empty, engine takes the no-knockback arm) and the
 *             missing-assets case keep the old sink placeholder.
 *   3 FREE    slot inactive.
 *
 * ANIMATION LAYER (FINDINGS "CRAWLER RESOLVED" section 4 — the leech
 * clip bank, 4 clips at 60 fps): a VISUAL-ONLY layer driven BY the
 * state machine above; it never feeds back into gameplay (state
 * transitions, positions and mailbox timing are bit-identical to the
 * static-pose build, keeping EM_ENEMY_TEST output stable):
 *
 *   spawn        clip 1 (emerge, 90 f) once, then the state's loop
 *   IDLE         clip 0 (crawl/stalk, 239 f, in-place) looped SLOWLY
 *   ATTACK       clip 0 looped at the entity's ACTUAL ground speed
 *                (21.27 u/s = 1.0x — the lunge clip's authored root
 *                speed; the loco clips are baked in place, so all root
 *                motion comes from the entity's own hop integration)
 *   close range  clip 2 (windup, 45 f) then clip 3 (lunge, 120 f) so
 *                the lunge clip is playing when the radius-6 suicide
 *                burst lands (trigger range is port-tuned, flagged)
 *   DEATH        no clip exists (engine rebinds gib MODELS instead) —
 *                fast sink placeholder, anim frozen (flagged; a fade
 *                needs renderer per-draw alpha we don't have yet)
 *
 * Clip transitions crossfade over 0.15 s with the same linear palette
 * blend as the player path (em_game.c ANIM_BLEND_TIME — PROGRESS.md:
 * mid-blend live captures match no single clip).
 *
 * GIB LAYER (FINDINGS "GIB SET", decomp tools/export_props.py --gibs):
 * the burst-death visual. The engine rebinds the dead actor's model to
 * library entry 0x22 or 0x29 of chunk27/f01_id37.bin — the burst-husk
 * models — and knocks the corpse along the RNG-rotated hit vector.
 * The exported set (one static 1-node EMDL per library entry, textures
 * from the office GS dump) also carries the husks' texture-paired small
 * chunk/shard meshes (0x1C/0x1D/0x1E share husk A's skin, 0x26/0x27
 * husk B's, 0x28 = husk B at half size). On a lethal hit the port
 * spawns 3-5 instances from that set (the half husk first — the
 * documented rebind target nearest the worm's ~11-u scale — then
 * chunks/shards round-robin), each launched with:
 *
 *   planar dir = the hit vector (attacker -> victim, port stand-in:
 *                player -> crawler) rotated by RNG in {90, 180, 270}
 *                deg — the DOCUMENTED choice set — plus a flagged
 *                +-30 deg port jitter so instances sharing a rotation
 *                separate;
 *   vertical   = launch pop + the engine's 0.052/tick gravity;
 *   landing    = the same floor query as the hop, then rest;
 *   exit       = no per-draw alpha in the renderer yet, so instead of
 *                a fade each gib SINKS after ~3 s (180 ticks), like
 *                the corpse placeholder, and frees.
 *
 * Speeds/spin/jitter are port constants (flagged below); the RNG is a
 * tiny deterministic LCG so test runs and captures reproduce. Gib
 * instances are VISUAL ONLY: they draw through the same em_enemy_draw
 * chain contract as live crawlers (virtual indices >= the real slot
 * count, budgeted so the total never exceeds EM_ENEMY_MAX — em_game.c
 * sizes its render chain with it) and never touch gameplay state.
 *
 * EM_ENEMY_GIBDEMO=<frame>: debug hook — posts a lethal 0x400A mailbox
 * to enemy 0 at that update tick, so EM_CAPTURE (frame 60) can
 * photograph the scatter without scripting a full kill run.
 */
#include "game/em_enemy.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_model.h"
#include "game/em_sfx.h"

#define ENEMY_ASSET      "assets/enemy_crawler.emdl"
#define ENEMY_BONE_MAX   32
#define ENEMY_PI         3.14159265f

/* --- Engine constants (FINDINGS "ENEMY AI ARCHITECTURE") --------------- */
#define ENEMY_HP_INIT    1        /* crawler init HP (+0x34)              */
#define ENEMY_TURN_RATE  0.0524f  /* +-3 deg/frame (0x3D56774F)           */
#define ENEMY_GRAVITY    0.052f   /* hop vertical integration, per tick   */
#define ENEMY_SENSE      32.0f    /* the documented distance-only sense
                                   * (leech <= 32 u; PORT choice for the
                                   * crawler wake — see em_enemy.h)       */
#define ENEMY_LUNGE_R    6.0f     /* the engine's radius-6 contact test   */
#define ENEMY_LUNGE_BAND 8.0f     /* vertical tolerance (pair-pass +-6/8) */
#define ENEMY_RETREAT    6        /* +0x2A, set on the alarm wake         */
#define ENEMY_HIT_CODE   0x400A   /* player mailbox: type 0x4000 | 10 —
                                   * the documented contact-damage code
                                   * (crawler amount unpinned; flagged)   */

/* --- Animation (the leech clip bank — FINDINGS "CRAWLER RESOLVED" §4,
 * clip ids = source container indices in the EMD3 clip table) --------- */
#define ENEMY_CLIP_CRAWL   0u     /* crawl/stalk loop, 239 f, in-place    */
#define ENEMY_CLIP_EMERGE  1u     /* spawn/emerge, 90 f, one-shot         */
#define ENEMY_CLIP_WINDUP  2u     /* lunge windup, 45 f, one-shot         */
#define ENEMY_CLIP_LUNGE   3u     /* lunge, 120 f, baked in-place; the
                                   * authored root travel = 21.27 u/s     */
#define ENEMY_LUNGE_SPEED  21.27f /* u/s at playback rate 1.0 (above)     */
#define ENEMY_ANIM_BLEND   0.15f  /* crossfade seconds (= em_game.c's
                                   * ANIM_BLEND_TIME player crossfade)    */

/* Anim-layer port tunings (visual only; flagged — not engine values) */
#define ENEMY_IDLE_RATE    0.25f  /* IDLE crawl-loop playback rate        */
#define ENEMY_ATTACK_MIN   0.35f  /* rate floor while steering in place
                                   * (ground speed 0 must not freeze it)  */
#define ENEMY_WINDUP_RANGE 20.0f  /* start the windup anim here so the
                                   * 45-f windup ends near the radius-6
                                   * contact at the ~19 u/s hop pace      */
#define ENEMY_SINK_FRAMES  20     /* DEATH placeholder: sink duration     */
#define ENEMY_SINK_DEPTH   5.0f   /* DEATH placeholder: sink distance     */

/* --- Gib layer (see "GIB LAYER" in the file header) --------------------
 * Engine values: the rotation choice set and gravity. Everything else is
 * a flagged port constant (launch speeds are not exported from the disc). */
#define GIB_MODEL_MAX    6        /* loadable burst-set models            */
#define GIB_BONE_MAX     4        /* exporter writes 1+1 palette slots    */
#define GIB_COUNT_MIN    3        /* instances per burst: 3..5            */
#define GIB_COUNT_SPAN   3
#define GIB_ROT_STEP     1.5707963f /* the documented 90-deg RNG steps    */
#define GIB_JITTER_DEG   30       /* PORT: +-30 deg spread inside a step  */
#define GIB_SPEED        0.28f    /* PORT: planar launch, units/frame     */
#define GIB_VY           0.45f    /* PORT: vertical pop (0.052 gravity)   */
#define GIB_SPIN_MAX     0.25f    /* PORT: yaw tumble, rad/frame          */
#define GIB_LAUNCH_LIFT  1.5f     /* PORT: spawn height above the feet    */
#define GIB_REST_FRAMES  180      /* ~3 s total before the sink (the
                                   * documented no-alpha fade stand-in)   */
#define GIB_SINK_FRAMES  30
#define GIB_SINK_DEPTH   3.0f

/* The exported burst set this module launches (decomp repo
 * tools/export_props.py --gibs; library entry in the name). Order
 * matters: the half-size husk B (0x28, the documented rebind target
 * closest to the worm's scale) first, then the texture-paired chunks/
 * shards round-robin. Missing files just shrink the pool; an empty
 * pool falls back to the sink placeholder (no regression). */
static const char *const GIB_FILES[GIB_MODEL_MAX] = {
    "assets/gibs/gib_28.emdl",    /* husk B, half size (7x7x4)  */
    "assets/gibs/gib_26.emdl",    /* chunk 1 (husk-B skin)      */
    "assets/gibs/gib_27.emdl",    /* chunk 2 (husk-B skin)      */
    "assets/gibs/gib_1c.emdl",    /* shard A1 (husk-A skin)     */
    "assets/gibs/gib_1d.emdl",    /* shard A2 (husk-A skin)     */
    "assets/gibs/gib_1e.emdl",    /* shard A3 (husk-A skin)     */
};

/* --- Port placeholders (not exported from the disc; flagged) ----------- */
#define ENEMY_HOP_SPEED  0.32f    /* forward units/frame while airborne   */
#define ENEMY_HOP_VY     0.42f    /* initial vertical velocity (~16-frame
                                   * airtime under the 0.052 gravity)     */
#define ENEMY_PROBE_LEN  6.0f     /* diagonal steer-probe length          */
#define ENEMY_PROBE_LIFT 1.0f     /* probe height above the feet          */
#define ENEMY_HIT_R      3.0f     /* bullet hit-sphere radius             */
#define ENEMY_AIM_Y      2.0f     /* aim/hit-sphere center above the feet */
#define ENEMY_FLOOR_UP   8.0f     /* floor-query window (em_game values)  */
#define ENEMY_FLOOR_DOWN 8.0f

typedef struct {
    int     active;       /* slot in use AND not yet FREE'd              */
    uint8_t state;        /* actor +0x04 lifecycle (engine values)       */
    uint8_t sub;          /* attack sub-state: 0 steer / 1 hop           */
    uint8_t alarm;        /* actor +0x0A group-alarm flag                */
    int16_t hp;           /* actor +0x34                                 */
    int16_t mailbox;      /* actor +0x36 incoming-damage mailbox         */
    int     retreat;      /* actor +0x2A blocked-retreat counter         */
    float   pos[3];       /* actor +0xB0/B4/B8                           */
    float   yaw;          /* actor +0xC4 (heading; 0 = +Z, engine sense) */
    float   vy;           /* hop vertical velocity (+0x2C8)              */
    float   hop_y0;       /* launch height — the landing plane when the
                           * floor query finds nothing under the hop
                           * (e.g. outside the decoded grid floor)       */

    /* anim layer (VISUAL ONLY — never read by the state machine) */
    uint8_t aphase;       /* AnimPhase below                             */
    int     acur;         /* current clip index (into model.clips); -1 =
                           * no clip data, static base pose              */
    int     aprev;        /* fading-out clip index, -1 = no blend        */
    double  at;           /* current clip time, frames (60/s baked)      */
    double  aprev_t;      /* fading-out clip time, frames                */
    float   arate;        /* current clip frames-per-tick playback rate  */
    float   aprev_rate;   /* fading-out clip rate (keeps advancing, the
                           * player-path crossfade blends two LIVE clips)*/
    float   ablend;       /* crossfade weight of acur, 0..1              */
    float   speed;        /* actual XZ ground speed this tick, u/s       */
    int     sink;         /* DEATH placeholder: sink frames left (draws
                           * while > 0 even though the slot is inactive) */

    /* lethal-hit record (engine: +0x36 nonzero + the +0x70 hit-source
     * position decide the knockback arm; port: the gib launch) */
    int     hit_lethal;   /* this death came from the damage mailbox     */
    float   hit_dir[2];   /* XZ hit vector, attacker -> victim, unit     */

    float   palette[ENEMY_BONE_MAX * 16];
} Enemy;

/* One loaded burst-set model (static 1-node EMDL; the exporter writes a
 * 1+1-slot palette, so bone_count is 2 with both slots identity). */
typedef struct {
    EmGfxMesh *mesh;
    EmModel    model;
    uint32_t   bone_count;
    float      base[GIB_BONE_MAX * 16];   /* frame-0 (identity) palette */
} GibModel;

/* One airborne/resting gib instance (visual only). */
typedef struct {
    int   active;
    int   model;          /* index into s.gibm                           */
    float pos[3];
    float vel[3];         /* 0.052/tick gravity on [1]                   */
    float yaw, spin;      /* tumble (PORT visual)                        */
    float y0;             /* launch height = floor fallback (same rule
                           * as the hop's hop_y0)                        */
    int   age;            /* ticks since launch -> rest -> sink -> free  */
    float palette[GIB_BONE_MAX * 16];
} Gib;

/* Anim-layer phases (port-side, NOT engine state values — the engine
 * picks clips inside func_00154040/func_00154120). */
enum {
    ANIM_EMERGE = 0,   /* clip 1 once (spawn)                       */
    ANIM_CRAWL  = 1,   /* clip 0 loop (idle slow / attack speed)    */
    ANIM_WINDUP = 2,   /* clip 2 once (close range)                 */
    ANIM_LUNGE  = 3    /* clip 3 once, the burst lands during it    */
};

static struct {
    /* shared mesh: the asset (preferred) or the runtime placeholder */
    int        mesh_tried;
    EmGfxMesh *mesh;
    EmModel    model;        /* loaded only on the asset path */
    int        has_model;
    uint32_t   bone_count;
    float      base[ENEMY_BONE_MAX * 16];  /* frame-0 pose (or identity) */

    /* resolved clip indices (-1 = the asset doesn't carry it); anim is
     * enabled only for a real multi-clip EMD3 asset (clip_crawl >= 0
     * and clip_count > 1 — an EMD2 fallback keeps the static pose) */
    int        anim_on;
    int        clip_crawl, clip_emerge, clip_windup, clip_lunge;
    float      blend_pal[ENEMY_BONE_MAX * 16]; /* crossfade scratch */

    Enemy      e[EM_ENEMY_MAX];
    int        n;

    int        player_hit;   /* player-side damage mailbox (+0x36 shape) */

    /* gib layer (visual only; see the file header) */
    int        gib_tried;
    GibModel   gibm[GIB_MODEL_MAX];
    int        gibm_n;       /* loaded burst-set models (0 = sink only)  */
    Gib        gib[EM_ENEMY_MAX];
    int        gib_tail;     /* virtual draw slots in use (compact top)  */
    int        gib_next;     /* round-robin model cursor                 */
    uint32_t   rng;          /* deterministic LCG state                  */
    int        frame;        /* update ticks (EM_ENEMY_GIBDEMO hook)     */
    int        demo;         /* parsed EM_ENEMY_GIBDEMO (-1 = off)       */
} s;

static void enemy_build_palette(Enemy *e);

void em_enemy_reset(void)
{
    /* Mesh/model survive a reset only through shutdown (mirrors
     * em_door_reset: boot resets once before adding). */
    memset(&s, 0, sizeof s);
    s.rng  = 0x2A5613u;   /* fixed LCG seed: deterministic runs/captures */
    s.demo = -1;
    const char *gd = getenv("EM_ENEMY_GIBDEMO");
    if (gd && *gd)
        s.demo = atoi(gd);
}

/* Deterministic LCG (Numerical-Recipes constants — our own tiny RNG,
 * NOT the engine's frame RNG at spad 0x70003B68; flagged port choice). */
static uint32_t gib_rng(void)
{
    s.rng = s.rng * 1664525u + 1013904223u;
    return s.rng >> 8;
}

/* ------------------------------------------------------------------ */
/* Shared mesh: asset, else the procedural placeholder                  */
/* ------------------------------------------------------------------ */

/* Append one axis-aligned box (per-face normals, untextured, bone 0) to
 * a 10-words-per-vertex buffer. PLACEHOLDER geometry — generated at
 * runtime, our own original vertices, NOT disc data. */
static void box_emit(float *verts, uint32_t *nv, uint32_t *idx, uint32_t *ni,
                     const float lo[3], const float hi[3])
{
    static const int face[6][4] = {
        /* +X */ {1, 3, 7, 5}, /* -X */ {4, 6, 2, 0},
        /* +Y */ {2, 6, 7, 3}, /* -Y */ {0, 1, 5, 4},
        /* +Z */ {5, 7, 6, 4}, /* -Z */ {0, 2, 3, 1}
    };
    static const float fnrm[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    const uint32_t no_tex = 0xFFFFFFFFu;

    for (int f = 0; f < 6; f++) {
        uint32_t base = *nv;
        for (int c = 0; c < 4; c++) {
            int    k = face[f][c];
            float *v = verts + (size_t)(*nv) * 10;
            v[0] = (k & 1) ? hi[0] : lo[0];
            v[1] = (k & 2) ? hi[1] : lo[1];
            v[2] = (k & 4) ? hi[2] : lo[2];
            v[3] = fnrm[f][0];
            v[4] = fnrm[f][1];
            v[5] = fnrm[f][2];
            v[6] = 0.0f;                       /* uv (untextured) */
            v[7] = 0.0f;
            memset(&v[8], 0, sizeof(float));   /* bone 0, no flags */
            memcpy(&v[9], &no_tex, sizeof no_tex);
            (*nv)++;
        }
        idx[(*ni)++] = base + 0;
        idx[(*ni)++] = base + 1;
        idx[(*ni)++] = base + 2;
        idx[(*ni)++] = base + 0;
        idx[(*ni)++] = base + 2;
        idx[(*ni)++] = base + 3;
    }
}

static void mat4_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* Load the shared crawler mesh once: the EMDL asset when present (frame-0
 * pose of clip 0 — no enemy anim hookup yet, see em_enemy.h), else the
 * runtime PLACEHOLDER: a squat box body + a head box pointing +Z (the
 * facing convention), built through em_gfx_mesh_create. Returns 0 ok. */
static int enemy_mesh_get(EmGfx *gfx)
{
    if (s.mesh) return 0;
    if (s.mesh_tried) return -1;
    s.mesh_tried = 1;

    if (em_model_load(&s.model, ENEMY_ASSET) == 0) {
        if (s.model.bone_count > ENEMY_BONE_MAX) {
            fprintf(stderr, "enemy: %s: %u bones > %d\n", ENEMY_ASSET,
                    s.model.bone_count, ENEMY_BONE_MAX);
            em_model_free(&s.model);
            return -1;
        }
        s.mesh = em_gfx_mesh_create(gfx, s.model.verts, s.model.vert_count,
                                    s.model.indices, s.model.index_count,
                                    (const EmGfxTexDesc *)s.model.texs,
                                    s.model.tex_count, s.model.texels,
                                    s.model.flags);
        if (!s.mesh) {
            em_model_free(&s.model);
            return -1;
        }
        s.has_model  = 1;
        s.bone_count = s.model.bone_count;
        em_model_palette_at(&s.model, 0, 0.0, s.base); /* frame-0 pose */
        s.clip_crawl  = em_model_clip_index(&s.model, ENEMY_CLIP_CRAWL);
        s.clip_emerge = em_model_clip_index(&s.model, ENEMY_CLIP_EMERGE);
        s.clip_windup = em_model_clip_index(&s.model, ENEMY_CLIP_WINDUP);
        s.clip_lunge  = em_model_clip_index(&s.model, ENEMY_CLIP_LUNGE);
        s.anim_on = (s.clip_crawl >= 0 && s.model.clip_count > 1);
        printf("enemy model: %s — %u verts, %u tris, %u bones, %u clip(s)",
               ENEMY_ASSET, s.model.vert_count, s.model.index_count / 3,
               s.bone_count, s.model.clip_count);
        if (s.anim_on)
            printf(" — anim on (crawl #%d, emerge #%d, windup #%d, "
                   "lunge #%d)\n", s.clip_crawl, s.clip_emerge,
                   s.clip_windup, s.clip_lunge);
        else
            printf(" — static frame-0 pose (re-export the EMD3 clip "
                   "bank for animation)\n");
        return 0;
    }

    /* PLACEHOLDER crawler: body + head, ~4 units long, 2 high. */
    float    verts[48 * 10];
    uint32_t indices[72];
    uint32_t nv = 0, ni = 0;
    const float body_lo[3] = { -1.5f, 0.2f, -2.0f };
    const float body_hi[3] = {  1.5f, 2.2f,  2.0f };
    const float head_lo[3] = { -0.8f, 0.6f,  2.0f };
    const float head_hi[3] = {  0.8f, 1.8f,  3.2f };
    box_emit(verts, &nv, indices, &ni, body_lo, body_hi);
    box_emit(verts, &nv, indices, &ni, head_lo, head_hi);

    s.mesh = em_gfx_mesh_create(gfx, verts, nv, indices, ni,
                                NULL, 0, NULL, 0);
    if (!s.mesh) return -1;
    s.bone_count = 1;
    mat4_identity(s.base);
    printf("enemy model: no %s — PLACEHOLDER box crawler (%u verts, %u "
           "tris, runtime-generated). Export the real mesh with the decomp "
           "repo's tools/export_native.py\n", ENEMY_ASSET, nv, ni / 3);
    return 0;
}

/* Load the burst set once (first crawler spawn — the only entry point
 * with a gfx handle; em_enemy_update can't create GPU meshes). Missing
 * files shrink the pool silently; an empty pool = sink fallback. */
static void gib_models_load(EmGfx *gfx)
{
    if (s.gib_tried) return;
    s.gib_tried = 1;

    for (int i = 0; i < GIB_MODEL_MAX; i++) {
        GibModel *gm = &s.gibm[s.gibm_n];
        if (em_model_load(&gm->model, GIB_FILES[i]) != 0)
            continue;
        if (gm->model.bone_count > GIB_BONE_MAX) {
            em_model_free(&gm->model);
            continue;
        }
        gm->mesh = em_gfx_mesh_create(gfx, gm->model.verts,
                                      gm->model.vert_count,
                                      gm->model.indices,
                                      gm->model.index_count,
                                      (const EmGfxTexDesc *)gm->model.texs,
                                      gm->model.tex_count, gm->model.texels,
                                      gm->model.flags);
        if (!gm->mesh) {
            em_model_free(&gm->model);
            continue;
        }
        gm->bone_count = gm->model.bone_count;
        em_model_palette_at(&gm->model, 0, 0.0, gm->base);
        s.gibm_n++;
    }
    if (s.gibm_n > 0)
        printf("enemy gibs: %d/%d burst-set models loaded from "
               "assets/gibs/ (lethal hits scatter them)\n",
               s.gibm_n, GIB_MODEL_MAX);
    else
        printf("enemy gibs: none of assets/gibs/ present — death keeps "
               "the sink placeholder (export with the decomp repo's "
               "tools/export_props.py --gibs)\n");
}

int em_enemy_add(EmGfx *gfx, const float pos[3], float yaw)
{
    if (s.n >= EM_ENEMY_MAX) return -1;
    if (enemy_mesh_get(gfx) != 0) return -1;
    gib_models_load(gfx);

    Enemy *e = &s.e[s.n];
    memset(e, 0, sizeof *e);
    e->active = 1;
    e->state  = EM_ENEMY_INIT;
    e->pos[0] = pos[0];
    e->pos[1] = pos[1];
    e->pos[2] = pos[2];
    e->yaw    = yaw;
    /* Anim layer: spawn plays the emerge clip once (clip 1), falling
     * back to the crawl loop if the asset lacks it. */
    e->acur  = -1;
    e->aprev = -1;
    if (s.anim_on) {
        e->aphase = ANIM_EMERGE;
        e->acur   = s.clip_emerge >= 0 ? s.clip_emerge : s.clip_crawl;
        e->arate  = s.clip_emerge >= 0 ? 1.0f : ENEMY_IDLE_RATE;
        e->ablend = 1.0f;
    }
    /* Stage a valid pose immediately: the render chain may record this
     * instance's palette pointer before the first em_enemy_update. */
    enemy_build_palette(e);
    printf("enemy %d: crawler at (%.1f, %.1f, %.1f) yaw %.3f\n", s.n,
           pos[0], pos[1], pos[2], yaw);
    return s.n++;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/* Group-alarm broadcast (IDLE damage path): the engine walks the live
 * actor list D_00275BC0 and sets +0x0A on every actor with a crawler
 * model byte and the on-surface flag; natively every live crawler. */
static void enemy_alarm_broadcast(void)
{
    for (int i = 0; i < s.n; i++)
        if (s.e[i].active)
            s.e[i].alarm = 1;
}

/* Consume the +0x36 mailbox. Returns 1 if the hit was lethal (HP=1
 * crawlers: any nonzero damage). Low bits = amount (below the 0x2000
 * type flag), matching the documented code layout. A lethal hit plays
 * the death-sub-state sound 0x7D8 — the canonical hurt-helper's
 * (func_00153B50) HP<=0 arm; the crawler's own per-state gore set
 * (burst 0x434 etc.) is not pinned to this transition yet. */
static int enemy_mailbox_poll(Enemy *e, const float pp[3])
{
    if (e->mailbox == 0) return 0;
    int amount = e->mailbox & 0x1FFF;
    e->mailbox = 0;
    e->hp      = (int16_t)(e->hp - amount);
    enemy_alarm_broadcast();      /* a shot crawler wakes the pack */
    if (e->hp > 0) return 0;
    em_sfx_play(EM_SFX_ENEMY_DEATH);   /* 0x7D8 */
    /* Lethal: record the hit vector for the gib knockback. The engine
     * copies the attacker position into victim +0x70 (pair pass /
     * func_001B41F0); the port's only attacker is the player, so the
     * stand-in hit vector is player -> crawler in XZ. Degenerate
     * (same spot): knock straight back along the facing. */
    float hx = e->pos[0] - pp[0], hz = e->pos[2] - pp[2];
    float hl = sqrtf(hx * hx + hz * hz);
    if (hl > 1e-4f) {
        e->hit_dir[0] = hx / hl;
        e->hit_dir[1] = hz / hl;
    } else {
        e->hit_dir[0] = -sinf(e->yaw);
        e->hit_dir[1] = -cosf(e->yaw);
    }
    e->hit_lethal = 1;
    return 1;
}

/* Knee-height directional probe (func_0019AB20 stand-in over the static
 * sets): 1 = blocked by a non-walkable surface within `len`. */
static int enemy_probe(const EmCollision *coll, const Enemy *e,
                       float ang, float len)
{
    if (!coll || !coll->poly_count) return 0;
    float from[3] = { e->pos[0], e->pos[1] + ENEMY_PROBE_LIFT, e->pos[2] };
    float to[3]   = { from[0] + sinf(ang) * len, from[1],
                      from[2] + cosf(ang) * len };
    EmCollHit hit;
    if (!em_collision_segment_query(coll, from, to,
                                    EM_COLL_SET_CELLS | EM_COLL_SET_GRID,
                                    EM_COLL_ID_NONE, &hit))
        return 0;
    return hit.surf_class != EM_SURF_FLOOR &&
           hit.surf_class != EM_SURF_SLOPE;
}

/* Floor height under a point (the same vertical-query pattern AND query
 * id 0 as the player spine in em_game.c — the walkable grid floor
 * carries conditional attrs that a -1 id query skips). Falls back to
 * `fallback` (the launch height) when nothing is found (no collision
 * world, or the spot lies outside the decoded grid floor) so a failed
 * query can never ratchet the querier upward. Shared by the crawler's
 * hop and the gib landings. */
static float floor_at(const EmCollision *coll, const float pos[3],
                      float fallback)
{
    if (!coll || !coll->poly_count) return fallback;
    float from[3] = { pos[0], pos[1] + ENEMY_FLOOR_UP,   pos[2] };
    float down[3] = { pos[0], pos[1] - ENEMY_FLOOR_DOWN, pos[2] };
    EmCollHit hit;
    for (int i = 0; i < 8; i++) {
        if (!em_collision_segment_query(coll, from, down,
                                        EM_COLL_SET_CELLS |
                                        EM_COLL_SET_GRID, 0, &hit))
            break;
        if (hit.surf_class == EM_SURF_FLOOR ||
            hit.surf_class == EM_SURF_SLOPE)
            return hit.point[1];
        if (hit.point[1] - 1e-3f <= down[1])
            break;
        from[1] = hit.point[1] - 1e-3f;
    }
    return fallback;
}

static float enemy_floor(const EmCollision *coll, const Enemy *e)
{
    return floor_at(coll, e->pos, e->hop_y0);
}

static float wrap_pi(float a)
{
    while (a >  ENEMY_PI) a -= 2.0f * ENEMY_PI;
    while (a < -ENEMY_PI) a += 2.0f * ENEMY_PI;
    return a;
}

/* ------------------------------------------------------------------ */
/* Animation layer (visual only — see the file header)                  */
/* ------------------------------------------------------------------ */

/* Clip time for evaluation: every clip except the crawl LOOP is a
 * one-shot — clamp to the last baked frame so em_model_palette_at's
 * wrap (last blends into first) never plays a one-shot backwards. */
static double anim_eval_time(int clip, double t)
{
    const EmModelClip *c = &s.model.clips[clip];
    if (clip != s.clip_crawl && t > (double)(c->frame_count - 1))
        t = (double)(c->frame_count - 1);
    return t;
}

/* One-shot completion: the play head reached the last baked frame. */
static int anim_done(const Enemy *e)
{
    const EmModelClip *c = &s.model.clips[e->acur];
    return e->at >= (double)(c->frame_count - 1);
}

/* Switch the current clip, starting a 0.15 s crossfade from the old
 * one (which keeps advancing at its own rate — the player path blends
 * two LIVE clips the same way). Same clip = just retune the rate (the
 * attack loop rescales every tick with the ground speed). */
static void enemy_anim_set(Enemy *e, int clip, float rate)
{
    if (clip < 0) return;
    if (clip == e->acur) {
        e->arate = rate;
        return;
    }
    e->aprev      = e->acur;
    e->aprev_t    = e->at;
    e->aprev_rate = e->arate;
    e->acur       = clip;
    e->at         = 0.0;
    e->arate      = rate;
    e->ablend     = e->aprev >= 0 ? 0.0f : 1.0f;
}

/* Pick this tick's clip + rate from the GAMEPLAY state (one-way: the
 * anim layer reads the state machine, never the reverse), then advance
 * the play heads and the crossfade weight. */
static void enemy_anim_update(Enemy *e, float dist)
{
    if (!s.anim_on) return;

    switch (e->state) {
    case EM_ENEMY_INIT:
    case EM_ENEMY_IDLE:
        /* spawn: let the one-shot emerge finish, then the slow loop */
        if (e->aphase == ANIM_EMERGE && !anim_done(e))
            break;
        e->aphase = ANIM_CRAWL;
        enemy_anim_set(e, s.clip_crawl, ENEMY_IDLE_RATE);
        break;

    case EM_ENEMY_ATTACK:
        /* close range: windup once -> lunge once; the radius-6 suicide
         * burst (gameplay) lands while the lunge clip plays. An attack
         * wake also cuts the emerge short (the crossfade hides it). */
        if (e->aphase == ANIM_WINDUP) {
            if (anim_done(e) && s.clip_lunge >= 0) {
                e->aphase = ANIM_LUNGE;
                enemy_anim_set(e, s.clip_lunge, 1.0f);
            }
            break;
        }
        if (e->aphase == ANIM_LUNGE) {
            if (!anim_done(e))
                break;          /* ran out without contact: re-approach */
            e->aphase = ANIM_CRAWL;
            /* fall through to the speed-scaled loop below */
        }
        if (dist <= ENEMY_WINDUP_RANGE && s.clip_windup >= 0) {
            e->aphase = ANIM_WINDUP;
            enemy_anim_set(e, s.clip_windup, 1.0f);
            break;
        }
        {
            /* the loop tracks the entity's ACTUAL ground speed; the
             * authored 21.27 u/s = rate 1.0. Floor it so the steer
             * phase (speed 0) keeps writhing instead of freezing. */
            float rate = e->speed / ENEMY_LUNGE_SPEED;
            if (rate < ENEMY_ATTACK_MIN) rate = ENEMY_ATTACK_MIN;
            e->aphase = ANIM_CRAWL;
            enemy_anim_set(e, s.clip_crawl, rate);
        }
        break;

    default:                    /* DEATH/FREE: pose frozen (sink only) */
        return;
    }

    /* advance the play heads (clips are baked at 60 fps = 1 frame per
     * 60 Hz tick at rate 1.0) and the 0.15 s crossfade */
    e->at += (double)e->arate;
    if (e->aprev >= 0) {
        e->aprev_t += (double)e->aprev_rate;
        e->ablend  += (1.0f / 60.0f) / ENEMY_ANIM_BLEND;
        if (e->ablend >= 1.0f) {
            e->ablend = 1.0f;
            e->aprev  = -1;
        }
    }
}

/* Build the instance's world palette: the anim-evaluated pose (or the
 * static base) composed with T(pos) * R_y(yaw).
 *
 * The placement composition is a LOCAL COPY of em_game.c's static
 * palette_apply_placement (same math, also copied by em_door.c) — not
 * shared because exporting it would touch em_game.h, which this module
 * doesn't own. Fold all three into a common helper when one moves.
 *
 * DEATH placeholder (flagged): a despawned-but-sinking slot draws its
 * frozen last pose translated down by the sink progress — there is no
 * death clip in the bank (the engine REBINDS gib models instead), and
 * a fade would need per-draw alpha the renderer doesn't expose yet. */
static void enemy_build_palette(Enemy *e)
{
    const float c = cosf(e->yaw), sn = sinf(e->yaw);
    float       y = e->pos[1];

    if (s.anim_on && e->acur >= 0) {
        em_model_palette_at(&s.model, (uint32_t)e->acur,
                            anim_eval_time(e->acur, e->at), e->palette);
        if (e->aprev >= 0 && e->ablend < 1.0f) {
            em_model_palette_at(&s.model, (uint32_t)e->aprev,
                                anim_eval_time(e->aprev, e->aprev_t),
                                s.blend_pal);
            uint32_t n = s.bone_count * 16;
            float    w = e->ablend;
            for (uint32_t i = 0; i < n; i++)
                e->palette[i] = s.blend_pal[i] +
                                (e->palette[i] - s.blend_pal[i]) * w;
        }
    } else {
        memcpy(e->palette, s.base, s.bone_count * 16 * sizeof(float));
    }

    if (!e->active && e->sink > 0)
        y -= ENEMY_SINK_DEPTH *
             (1.0f - (float)e->sink / (float)ENEMY_SINK_FRAMES);

    for (uint32_t b = 0; b < s.bone_count; b++) {
        float *m = e->palette + b * 16;
        for (int col = 0; col < 4; col++) {
            float x = m[col * 4 + 0], z = m[col * 4 + 2];
            m[col * 4 + 0] =  c * x + sn * z;
            m[col * 4 + 2] = -sn * x + c * z;
        }
        m[12] += e->pos[0];
        m[13] += y;
        m[14] += e->pos[2];
    }
}

/* ------------------------------------------------------------------ */
/* Gib layer (visual only — see "GIB LAYER" in the file header)         */
/* ------------------------------------------------------------------ */

/* World palette of one gib: the model's identity base pose rotated by
 * the tumble yaw and translated to the instance position (the same
 * column rotation + translate composition as enemy_build_palette). */
static void gib_build_palette(Gib *g)
{
    const GibModel *gm = &s.gibm[g->model];
    const float c = cosf(g->yaw), sn = sinf(g->yaw);

    memcpy(g->palette, gm->base, gm->bone_count * 16 * sizeof(float));
    for (uint32_t b = 0; b < gm->bone_count; b++) {
        float *m = g->palette + b * 16;
        for (int col = 0; col < 4; col++) {
            float x = m[col * 4 + 0], z = m[col * 4 + 2];
            m[col * 4 + 0] =  c * x + sn * z;
            m[col * 4 + 2] = -sn * x + c * z;
        }
        m[12] += g->pos[0];
        m[13] += g->pos[1];
        m[14] += g->pos[2];
    }
}

/* Burst: launch 3-5 gib instances from a lethally-hit crawler with the
 * documented knockback shape (file header). Budgeted so the virtual
 * draw slots never push em_enemy_count past EM_ENEMY_MAX (em_game.c's
 * chain reservation). Returns the number launched (0 = caller keeps
 * the sink placeholder). */
static int gib_burst(const Enemy *e)
{
    if (s.gibm_n == 0) return 0;

    int want   = GIB_COUNT_MIN + (int)(gib_rng() % GIB_COUNT_SPAN);
    int budget = EM_ENEMY_MAX - s.n;   /* virtual slots we may occupy */
    int spawned = 0;

    for (int k = 0; k < budget && spawned < want; k++) {
        Gib *g = &s.gib[k];
        if (g->active) continue;

        /* the documented rotation: hit vector turned by 90/180/270 deg
         * (RNG), plus the flagged +-30 deg port jitter */
        float ang = atan2f(e->hit_dir[0], e->hit_dir[1])
                  + GIB_ROT_STEP * (float)(1 + gib_rng() % 3)
                  + (float)((int)(gib_rng() % (2 * GIB_JITTER_DEG + 1))
                            - GIB_JITTER_DEG) * (ENEMY_PI / 180.0f);

        memset(g, 0, sizeof *g);
        g->active = 1;
        g->model  = s.gib_next++ % s.gibm_n;
        g->pos[0] = e->pos[0];
        g->pos[1] = e->pos[1] + GIB_LAUNCH_LIFT;
        g->pos[2] = e->pos[2];
        g->vel[0] = sinf(ang) * GIB_SPEED;
        g->vel[1] = GIB_VY;
        g->vel[2] = cosf(ang) * GIB_SPEED;
        g->yaw    = ang;
        g->spin   = ((float)(gib_rng() % 2001) / 1000.0f - 1.0f)
                    * GIB_SPIN_MAX;
        g->y0     = e->pos[1];
        gib_build_palette(g);
        if (k >= s.gib_tail) s.gib_tail = k + 1;
        spawned++;
    }
    return spawned;
}

/* Per-tick gib integration: arc under the 0.052 gravity, land on the
 * floor query, rest, then sink and free (file header timings). */
static void gib_update(const EmCollision *coll)
{
    int tail = 0;
    for (int k = 0; k < s.gib_tail; k++) {
        Gib *g = &s.gib[k];
        if (!g->active) continue;
        g->age++;
        if (g->vel[0] != 0.0f || g->vel[1] != 0.0f || g->vel[2] != 0.0f) {
            g->pos[0] += g->vel[0];
            g->pos[2] += g->vel[2];
            g->vel[1] -= ENEMY_GRAVITY;
            g->pos[1] += g->vel[1];
            g->yaw    += g->spin;
            float fy = floor_at(coll, g->pos, g->y0);
            if (g->vel[1] < 0.0f && g->pos[1] <= fy) {   /* settle */
                g->pos[1] = fy;
                g->vel[0] = g->vel[1] = g->vel[2] = 0.0f;
                g->spin   = 0.0f;
            }
        } else if (g->age > GIB_REST_FRAMES) {           /* sink + free */
            g->pos[1] -= GIB_SINK_DEPTH / (float)GIB_SINK_FRAMES;
            if (g->age > GIB_REST_FRAMES + GIB_SINK_FRAMES) {
                g->active = 0;
                continue;
            }
        }
        gib_build_palette(g);
        tail = k + 1;
    }
    s.gib_tail = tail;
}

/* ------------------------------------------------------------------ */
/* State machine                                                        */
/* ------------------------------------------------------------------ */

static void enemy_tick(const EmCollision *coll, Enemy *e,
                       const float pp[3])
{
    float dx   = pp[0] - e->pos[0];
    float dz   = pp[2] - e->pos[2];
    float dist = sqrtf(dx * dx + dz * dz);

    switch (e->state) {
    case EM_ENEMY_INIT:
        /* HP = 1, heading from the placement; the engine also builds
         * the diagonal probe vectors and resolves the nest registry. */
        e->hp    = ENEMY_HP_INIT;
        e->state = EM_ENEMY_IDLE;
        break;

    case EM_ENEMY_IDLE:
        if (enemy_mailbox_poll(e, pp)) {   /* any damage kills (HP=1) */
            e->state = EM_ENEMY_DEATH;
            break;
        }
        /* PORT WAKE (flagged): the documented 32-unit distance sense,
         * in addition to the engine's alarm-only wake. */
        if (dist <= ENEMY_SENSE)
            e->alarm = 1;
        if (e->alarm) {
            e->alarm   = 0;
            e->retreat = ENEMY_RETREAT;    /* +0x2A = 6 */
            e->sub     = 0;
            e->state   = EM_ENEMY_ATTACK;
        }
        break;

    case EM_ENEMY_ATTACK:
        /* PORT DEVIATION (flagged in em_enemy.h): the mailbox is polled
         * mid-attack too — the engine's state 1 poll is an open item. */
        if (enemy_mailbox_poll(e, pp)) {
            e->state = EM_ENEMY_DEATH;
            break;
        }
        if (e->sub == 0) {
            /* STEER: seek the player at +-3 deg/frame (the documented
             * per-frame rate), diagonals probed each frame. The hop
             * launches once the heading is roughly aligned — turning
             * happens on the ground, not mid-hop, so an overshot
             * crawler turns back instead of orbiting away. */
            float want = atan2f(dx, dz);
            float diff = wrap_pi(want - e->yaw);
            float step = diff;
            if (step >  ENEMY_TURN_RATE) step =  ENEMY_TURN_RATE;
            if (step < -ENEMY_TURN_RATE) step = -ENEMY_TURN_RATE;
            e->yaw = wrap_pi(e->yaw + step);

            int bl[4];   /* the 4 diagonals: +-45, +-135 deg off heading */
            int blocked = 0;
            static const float diag[4] = { 0.7854f, -0.7854f,
                                           2.3562f, -2.3562f };
            for (int k = 0; k < 4; k++) {
                bl[k] = enemy_probe(coll, e, e->yaw + diag[k],
                                    ENEMY_PROBE_LEN);
                blocked += bl[k];
            }
            if (blocked >= 3) {
                if (--e->retreat <= 0) {   /* +0x2A exhausted -> idle */
                    e->state = EM_ENEMY_IDLE;
                    break;
                }
            } else if (bl[0] != bl[1]) {
                /* a blocked front diagonal: turn away from it */
                e->yaw = wrap_pi(e->yaw + (bl[0] ? -ENEMY_TURN_RATE
                                                 :  ENEMY_TURN_RATE));
            }
            if (fabsf(diff) > 2.0f * ENEMY_TURN_RATE)
                break;       /* keep turning; hop once roughly aligned */
            /* launch the hop */
            e->vy     = ENEMY_HOP_VY;
            e->hop_y0 = e->pos[1];
            e->sub    = 1;
        } else {
            /* HOP: forward integrate unless a wall blocks the step;
             * vertical under the engine's 0.052/tick gravity. */
            if (!enemy_probe(coll, e, e->yaw, ENEMY_HOP_SPEED + 0.5f)) {
                e->pos[0] += sinf(e->yaw) * ENEMY_HOP_SPEED;
                e->pos[2] += cosf(e->yaw) * ENEMY_HOP_SPEED;
            }
            e->vy     -= ENEMY_GRAVITY;
            e->pos[1] += e->vy;
            float floor_y = enemy_floor(coll, e);
            if (e->vy < 0.0f && e->pos[1] <= floor_y) {
                e->pos[1] = floor_y;
                e->vy     = 0.0f;
                e->sub    = 0;
            }
            /* LUNGE contact: the radius-6 test against the player (with
             * the pair-pass vertical tolerance). The crawler bursts on
             * the lunge — the suicide-attack path: post the player
             * mailbox and die. */
            if (dist <= ENEMY_LUNGE_R &&
                fabsf(pp[1] - e->pos[1]) <= ENEMY_LUNGE_BAND) {
                s.player_hit = ENEMY_HIT_CODE;
                e->mailbox   = 0;          /* engine: cleared pre-burst */
                e->state     = EM_ENEMY_DEATH;
                /* Engine: the suicide BURST has its own sound (the
                 * crawler gore set, 0x434 family) — unmapped; silent
                 * natively until the per-state ids are pinned. */
            }
        }
        break;

    case EM_ENEMY_DEATH:
        /* Engine sub-machine: nest-child spawns, gore sounds/FX pairs,
         * a gib-model rebind and a knockback corpse-slide. The GAMEPLAY
         * slot frees immediately (alive/state/hit-tests identical to
         * the pre-gib build). Visuals: a LETHAL HIT launches the gib
         * burst (the engine's damage-kill knockback arm — see "GIB
         * LAYER"); the contact/suicide burst (mailbox empty: the
         * engine's no-knockback arm) and a missing assets/gibs/ keep
         * the old sink placeholder. */
        e->state  = EM_ENEMY_FREE;
        e->active = 0;
        e->sink   = (e->hit_lethal && gib_burst(e) > 0)
                    ? 0 : ENEMY_SINK_FRAMES;
        break;

    default:
        break;
    }
}

void em_enemy_update(const EmCollision *coll, const float player_pos[3])
{
    if (!player_pos) return;
    /* EM_ENEMY_GIBDEMO debug hook: lethal mailbox to enemy 0 at the
     * requested tick (see the file header). */
    if (s.demo >= 0 && s.frame == s.demo)
        em_enemy_damage(0, 0x400A);
    s.frame++;

    for (int i = 0; i < s.n; i++) {
        Enemy *e = &s.e[i];
        if (!e->active) {
            /* DEATH sink placeholder: keep lowering the frozen pose
             * for the few frames the corpse stays visible. */
            if (e->sink > 0) {
                e->sink--;
                enemy_build_palette(e);
            }
            continue;
        }
        float px = e->pos[0], pz = e->pos[2];
        enemy_tick(coll, e, player_pos);
        /* actual ground speed this tick — drives the attack-loop rate */
        float mx = e->pos[0] - px, mz = e->pos[2] - pz;
        e->speed = sqrtf(mx * mx + mz * mz) * 60.0f;
        if (e->active) {
            float dx = player_pos[0] - e->pos[0];
            float dz = player_pos[2] - e->pos[2];
            enemy_anim_update(e, sqrtf(dx * dx + dz * dz));
        }
        enemy_build_palette(e);   /* died this tick: freeze the pose
                                   * the sink placeholder starts from */
    }
    gib_update(coll);
}

/* ------------------------------------------------------------------ */
/* Damage + hitscan support                                             */
/* ------------------------------------------------------------------ */

void em_enemy_damage(int i, int16_t code)
{
    if (i < 0 || i >= s.n || !s.e[i].active) return;
    s.e[i].mailbox = code;
}

int em_enemy_player_hit_take(void)
{
    int code = s.player_hit;
    s.player_hit = 0;
    return code;
}

int em_enemy_acquire(const float from[3], float yaw, float max_dist,
                     float cone_cos, float aim_out[3])
{
    int   best    = -1;
    float best_d  = max_dist;
    float fx = sinf(yaw), fz = cosf(yaw);

    for (int i = 0; i < s.n; i++) {
        const Enemy *e = &s.e[i];
        if (!e->active) continue;
        float dx = e->pos[0] - from[0];
        float dz = e->pos[2] - from[2];
        float d  = sqrtf(dx * dx + dz * dz);
        if (d > best_d || d < 1e-4f) continue;
        if ((dx * fx + dz * fz) / d < cone_cos) continue;
        best   = i;
        best_d = d;
    }
    if (best >= 0 && aim_out) {
        aim_out[0] = s.e[best].pos[0];
        aim_out[1] = s.e[best].pos[1] + ENEMY_AIM_Y;
        aim_out[2] = s.e[best].pos[2];
    }
    return best;
}

int em_enemy_ray_test(const float from[3], const float to[3],
                      float hit_out[3])
{
    int   best   = -1;
    float best_t = 2.0f;
    float d[3]   = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
    float dd     = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (dd < 1e-9f) return -1;

    for (int i = 0; i < s.n; i++) {
        const Enemy *e = &s.e[i];
        if (!e->active) continue;
        float c[3] = { e->pos[0], e->pos[1] + ENEMY_AIM_Y, e->pos[2] };
        float m[3] = { from[0] - c[0], from[1] - c[1], from[2] - c[2] };
        float b    = m[0] * d[0] + m[1] * d[1] + m[2] * d[2];
        float cc   = m[0] * m[0] + m[1] * m[1] + m[2] * m[2]
                   - ENEMY_HIT_R * ENEMY_HIT_R;
        float disc = b * b - dd * cc;
        if (disc < 0.0f) continue;
        float t = (-b - sqrtf(disc)) / dd;   /* entry point */
        if (cc <= 0.0f) t = 0.0f;            /* starts inside */
        if (t < 0.0f || t > 1.0f || t >= best_t) continue;
        best   = i;
        best_t = t;
    }
    if (best >= 0 && hit_out) {
        hit_out[0] = from[0] + d[0] * best_t;
        hit_out[1] = from[1] + d[1] * best_t;
        hit_out[2] = from[2] + d[2] * best_t;
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* Draw + introspection accessors                                       */
/* ------------------------------------------------------------------ */

/* Draw-slot count: the real instances plus the gib layer's virtual
 * slots (gib_burst budgets them so this never exceeds EM_ENEMY_MAX —
 * em_game.c reserves exactly that many render-chain entries). */
int em_enemy_count(void)
{
    int n = s.n + s.gib_tail;
    return n > EM_ENEMY_MAX ? EM_ENEMY_MAX : n;
}

int em_enemy_draw(int i, EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count)
{
    if (i < 0) return 0;
    if (i >= s.n) {                        /* virtual gib slot */
        int k = i - s.n;
        if (k >= s.gib_tail || !s.gib[k].active) return 0;
        const GibModel *gm = &s.gibm[s.gib[k].model];
        *mesh       = gm->mesh;
        *palette    = s.gib[k].palette;
        *bone_count = gm->bone_count;
        return 1;
    }
    if (!s.mesh) return 0;
    if (!s.e[i].active && s.e[i].sink <= 0) return 0;  /* sink visual */
    *mesh       = s.mesh;
    *palette    = s.e[i].palette;
    *bone_count = s.bone_count;
    return 1;
}

int em_enemy_alive(void)
{
    int n = 0;
    for (int i = 0; i < s.n; i++)
        if (s.e[i].active) n++;
    return n;
}

int em_enemy_state(int i)
{
    return (i >= 0 && i < s.n) ? s.e[i].state : -1;
}

int em_enemy_hp(int i)
{
    return (i >= 0 && i < s.n) ? s.e[i].hp : 0;
}

void em_enemy_pos(int i, float out[3])
{
    if (i < 0 || i >= s.n) return;
    out[0] = s.e[i].pos[0];
    out[1] = s.e[i].pos[1];
    out[2] = s.e[i].pos[2];
}

void em_enemy_shutdown(EmGfx *gfx)
{
    if (s.mesh) {
        em_gfx_mesh_destroy(gfx, s.mesh);
        if (s.has_model)
            em_model_free(&s.model);
    }
    for (int i = 0; i < s.gibm_n; i++) {
        em_gfx_mesh_destroy(gfx, s.gibm[i].mesh);
        em_model_free(&s.gibm[i].model);
    }
    memset(&s, 0, sizeof s);
}
