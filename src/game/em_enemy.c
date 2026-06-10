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
 *   2 DEATH   engine: nest-child spawns + gore FX + knockback corpse
 *             slide. Port: despawn immediately — corpse = none yet.
 *   3 FREE    slot inactive.
 */
#include "game/em_enemy.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "em_model.h"

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
    float   palette[ENEMY_BONE_MAX * 16];
} Enemy;

static struct {
    /* shared mesh: the asset (preferred) or the runtime placeholder */
    int        mesh_tried;
    EmGfxMesh *mesh;
    EmModel    model;        /* loaded only on the asset path */
    int        has_model;
    uint32_t   bone_count;
    float      base[ENEMY_BONE_MAX * 16];  /* frame-0 pose (or identity) */

    Enemy      e[EM_ENEMY_MAX];
    int        n;

    int        player_hit;   /* player-side damage mailbox (+0x36 shape) */
} s;

static void enemy_build_palette(Enemy *e);

void em_enemy_reset(void)
{
    /* Mesh/model survive a reset only through shutdown (mirrors
     * em_door_reset: boot resets once before adding). */
    memset(&s, 0, sizeof s);
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
        printf("enemy model: %s — %u verts, %u tris, %u bones (static "
               "frame-0 pose; clip tables not exported yet)\n", ENEMY_ASSET,
               s.model.vert_count, s.model.index_count / 3, s.bone_count);
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

int em_enemy_add(EmGfx *gfx, const float pos[3], float yaw)
{
    if (s.n >= EM_ENEMY_MAX) return -1;
    if (enemy_mesh_get(gfx) != 0) return -1;

    Enemy *e = &s.e[s.n];
    memset(e, 0, sizeof *e);
    e->active = 1;
    e->state  = EM_ENEMY_INIT;
    e->pos[0] = pos[0];
    e->pos[1] = pos[1];
    e->pos[2] = pos[2];
    e->yaw    = yaw;
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
 * type flag), matching the documented code layout. */
static int enemy_mailbox_poll(Enemy *e)
{
    if (e->mailbox == 0) return 0;
    int amount = e->mailbox & 0x1FFF;
    e->mailbox = 0;
    e->hp      = (int16_t)(e->hp - amount);
    enemy_alarm_broadcast();      /* a shot crawler wakes the pack */
    return e->hp <= 0;
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

/* Floor height under the crawler (the same vertical-query pattern AND
 * query id 0 as the player spine in em_game.c — the walkable grid floor
 * carries conditional attrs that a -1 id query skips). Falls back to
 * the hop's launch height when nothing is found (no collision world, or
 * the spot lies outside the decoded grid floor) so a failed query can
 * never ratchet the crawler upward. */
static float enemy_floor(const EmCollision *coll, const Enemy *e)
{
    if (!coll || !coll->poly_count) return e->hop_y0;
    float from[3] = { e->pos[0], e->pos[1] + ENEMY_FLOOR_UP,   e->pos[2] };
    float down[3] = { e->pos[0], e->pos[1] - ENEMY_FLOOR_DOWN, e->pos[2] };
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
    return e->hop_y0;
}

static float wrap_pi(float a)
{
    while (a >  ENEMY_PI) a -= 2.0f * ENEMY_PI;
    while (a < -ENEMY_PI) a += 2.0f * ENEMY_PI;
    return a;
}

/* Build the instance's world palette: base pose composed with
 * T(pos) * R_y(yaw) — the same placement composition as em_door.c /
 * em_game.c (palette_apply_placement). */
static void enemy_build_palette(Enemy *e)
{
    const float c = cosf(e->yaw), sn = sinf(e->yaw);
    memcpy(e->palette, s.base, s.bone_count * 16 * sizeof(float));
    for (uint32_t b = 0; b < s.bone_count; b++) {
        float *m = e->palette + b * 16;
        for (int col = 0; col < 4; col++) {
            float x = m[col * 4 + 0], z = m[col * 4 + 2];
            m[col * 4 + 0] =  c * x + sn * z;
            m[col * 4 + 2] = -sn * x + c * z;
        }
        m[12] += e->pos[0];
        m[13] += e->pos[1];
        m[14] += e->pos[2];
    }
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
        if (enemy_mailbox_poll(e)) {       /* any damage kills (HP=1) */
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
        if (enemy_mailbox_poll(e)) {
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
            }
        }
        break;

    case EM_ENEMY_DEATH:
        /* Engine sub-machine: nest-child spawns, gore sounds/FX pairs,
         * knockback corpse-slide. None translated — despawn (corpse =
         * none yet; see em_enemy.h). */
        e->state  = EM_ENEMY_FREE;
        e->active = 0;
        break;

    default:
        break;
    }
}

void em_enemy_update(const EmCollision *coll, const float player_pos[3])
{
    if (!player_pos) return;
    for (int i = 0; i < s.n; i++) {
        Enemy *e = &s.e[i];
        if (!e->active) continue;
        enemy_tick(coll, e, player_pos);
        if (e->active)
            enemy_build_palette(e);
    }
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

int em_enemy_count(void) { return s.n; }

int em_enemy_draw(int i, EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count)
{
    if (i < 0 || i >= s.n || !s.e[i].active || !s.mesh) return 0;
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
    memset(&s, 0, sizeof s);
}
