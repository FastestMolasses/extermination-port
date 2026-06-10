/* em_door.c — interactive door actors (see em_door.h for the engine
 * mapping, the FULL s22 transit sequence, and the flagged deviations).
 *
 * State machine (FINDINGS "FIRST INTERACTIVE OBJECTS" + "AREA TRANSITION
 * LIFECYCLE" s22, func_001BC350 RUN sub-states, engine numbering kept):
 *
 *   0 CLOSED   armed (+0x0B != 0, by the use scan or a neighbor panel)
 *              -> transit kickoff (func_001BBE40: side latch, INPUT
 *              LOCK, walk-to the staging point door + 5*n) -> 3.
 *              The locked sequence (subs 1/2, model 0x15 + unlock
 *              bitmask) is not in the port yet.
 *   3 OPENING  advance the clip 1.0/frame (func_001BC0E0; captured clip
 *              window 77..97 vsyncs); clip done AND the player at the
 *              staging point -> 4
 *   4 OPEN     one-frame transition COMMIT (func_001BC240 ->
 *              func_001BC150): arm the 64-frame fade-out
 *              (func_001AEDE0(4,0)); room moves do NOT fade audio -> 5
 *   5 CLOSING  engine sub 5 = transition pending. At fade-out complete
 *              (screen black): post the RE-PLACE (spawn point behind the
 *              door, exit yaw), arm the 64-frame fade-in, and start
 *              running the clip back (the engine re-arms when the
 *              request byte B8 clears, right after the re-place). Input
 *              unlocks when the fade-in completes; at clip rest the door
 *              re-arms (+0x0B = 0) -> 0
 *
 * Articulation: the engine evaluates a keyframe clip on the door's bone
 * slots (func_001BC300 -> func_001C68C0). The double door's clip is not
 * yet located on disc, so a single-frame EMDL plays the PLACEHOLDER
 * hinge swing below (90 degrees about the placement origin's Y axis —
 * the panel's hinge edge sits at local x = 0). An EMDL that carries a
 * real baked clip (frame_count > 1) is played instead, 1.0 frame/tick,
 * exactly the engine rate.
 */
#include "game/em_door.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "em_input.h"   /* EM_PAD_CROSS — the frame input button mask */
#include "em_model.h"
#include "game/em_sfx.h"

#define DOOR_MAX        EM_DOOR_MAX
#define DOOR_MODEL_MAX  4
#define DOOR_BONE_MAX   16    /* palette slots incl. the EMDL identity slot */
#define DOOR_PI         3.14159265f

/* Use-scan constants — func_00184BA0 (s17). The manifest radius carries
 * the 12.0-unit scan distance; these two are the scan's inner gates. */
#define DOOR_AUTO_DIST2   4.0f   /* < 2.0 u -> immediate (returns 2) */
#define DOOR_FACING_DOT   0.4f   /* facing-dot threshold (~0.4) */

/* PLACEHOLDER swing timing — flagged: the real clip length is unknown.
 * The engine advances its clip 1.0/frame and the two live-captured
 * transits ran 97 (room move) and 77 (area change) vsyncs of clip; 90
 * frames sits inside that captured window. */
#define DOOR_SWING_FRAMES 90.0f
#define DOOR_SWING_ANGLE  (DOOR_PI * 0.5f)

/* Staging/spawn offset along the door normal: the engine stages the
 * player at door +- 5.0 * n on his own side (s22 captured 5.0 exactly;
 * e.g. -247.2 = door z + 5) and the spawn-table records flank the door
 * at ~+-5 with exit yaw. */
#define DOOR_POINT_DIST   5.0f

/* Fade speed for the transit fades — the captured func_001AEDE0 speed
 * (4 -> 64-frame ramp), see em_frame.h. */
#define DOOR_FADE_SPEED   EM_FADE_SPEED_DOOR

typedef struct {
    char       path[512];
    EmModel    model;
    EmGfxMesh *mesh;
    int        used;
    /* closed-pose (frame 0) palette + door-local AABB of the posed mesh */
    float      base[DOOR_BONE_MAX * 16];
    float      lo[3], hi[3];
    int        has_clip;     /* frame_count > 1: real baked clip present */
} DoorModel;

typedef struct {
    int      model;          /* index into s.models */
    float    pos[3];         /* placement (actor +0xB0) */
    float    yaw;            /* placement ry (actor +0xC4) */
    float    radius;         /* use-scan distance (12.0 from the table) */
    uint8_t  state;          /* actor +0x05 sub-state (engine values) */
    uint8_t  armed;          /* actor +0x0B activation flags (scan: 4) */
    float    clip_t;         /* anim block +0xE clip time, frames */
    int      transit;        /* walk-to MOVE-TO active (func_001BBE40) */
    float    transit_to[3];  /* STAGING point door_pos + 5.0 * n, near side */
    float    transit_yaw;    /* player yaw snapped to the door normal
                              * (travel direction = the exit yaw) */
    float    spawn_pt[3];    /* re-place point door_pos - 5.0 * n, far side
                              * (the spawn-table record's pose) */
    int      did_warp;       /* sub 5: re-place already posted this transit */
    float    aabb_lo[3];     /* world AABB of the CLOSED door (hull box) */
    float    aabb_hi[3];
    float    palette[DOOR_BONE_MAX * 16];
} Door;

static struct {
    DoorModel models[DOOR_MODEL_MAX];
    int       n_models;
    Door      doors[DOOR_MAX];
    int       n_doors;
    /* transit-wide state (one transit at a time, like the engine's
     * single B5..B8 request block) */
    int       lock;          /* player input locked (kickoff..fade-in end) */
    int       unlock_armed;  /* re-place posted: unlock at fade-in end */
    int       warp_pending;  /* one-shot re-place request for em_game */
    float     warp_pos[3];
    float     warp_yaw;
} s;

void em_door_reset(void)
{
    /* Models/meshes survive a reset only through shutdown (boot calls
     * reset exactly once before adding; em_game_shutdown frees). */
    memset(&s, 0, sizeof s);
}

/* ------------------------------------------------------------------ */
/* Loading                                                              */
/* ------------------------------------------------------------------ */

static int door_model_get(EmGfx *gfx, const char *scene_dir,
                          const char *file)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", scene_dir, file);
    for (int i = 0; i < s.n_models; i++)
        if (strcmp(s.models[i].path, path) == 0)
            return i;
    if (s.n_models >= DOOR_MODEL_MAX) return -1;

    DoorModel *dm = &s.models[s.n_models];
    if (em_model_load(&dm->model, path) != 0) return -1;
    if (dm->model.bone_count > DOOR_BONE_MAX) {
        fprintf(stderr, "door: %s: %u bones > %d\n", path,
                dm->model.bone_count, DOOR_BONE_MAX);
        em_model_free(&dm->model);
        return -1;
    }
    dm->mesh = em_gfx_mesh_create(gfx, dm->model.verts,
                                  dm->model.vert_count, dm->model.indices,
                                  dm->model.index_count,
                                  (const EmGfxTexDesc *)dm->model.texs,
                                  dm->model.tex_count, dm->model.texels,
                                  dm->model.flags);
    if (!dm->mesh) {
        em_model_free(&dm->model);
        return -1;
    }
    snprintf(dm->path, sizeof dm->path, "%s", path);

    /* Closed pose: frame 0 of clip 0 (the exporter's captured pose). */
    em_model_palette_at(&dm->model, 0, 0.0, dm->base);
    dm->has_clip = dm->model.frame_count > 1;

    /* Door-local AABB of the POSED closed mesh (palette * position) —
     * the blocking hull (the engine's per-uid collision-record AABB). */
    dm->lo[0] = dm->lo[1] = dm->lo[2] =  1e9f;
    dm->hi[0] = dm->hi[1] = dm->hi[2] = -1e9f;
    for (uint32_t v = 0; v < dm->model.vert_count; v++) {
        const float *vert = dm->model.verts + v * EM_MODEL_VERT_WORDS;
        uint32_t bone_word;
        memcpy(&bone_word, dm->model.verts + v * EM_MODEL_VERT_WORDS + 8,
               sizeof bone_word);
        uint32_t bone = bone_word & EM_MODEL_VERT_BONE_MASK;
        if (bone >= dm->model.bone_count) bone = dm->model.bone_count - 1;
        const float *m = dm->base + bone * 16;
        for (int k = 0; k < 3; k++) {
            float p = m[0 + k] * vert[0] + m[4 + k] * vert[1]
                    + m[8 + k] * vert[2] + m[12 + k];
            if (p < dm->lo[k]) dm->lo[k] = p;
            if (p > dm->hi[k]) dm->hi[k] = p;
        }
    }

    printf("door model: %s — %u verts, %u tris, %u bones, %s, local box "
           "(%.1f, %.1f, %.1f)..(%.1f, %.1f, %.1f)\n", path,
           dm->model.vert_count, dm->model.index_count / 3,
           dm->model.bone_count,
           dm->has_clip ? "baked clip" : "PLACEHOLDER swing (no clip yet)",
           dm->lo[0], dm->lo[1], dm->lo[2], dm->hi[0], dm->hi[1], dm->hi[2]);
    return s.n_models++;
}

int em_door_add(EmGfx *gfx, const char *scene_dir, const char *file,
                const float pos[3], float yaw, float radius)
{
    if (s.n_doors >= DOOR_MAX) return -1;
    int mi = door_model_get(gfx, scene_dir, file);
    if (mi < 0) return -1;

    Door *d = &s.doors[s.n_doors];
    memset(d, 0, sizeof *d);
    d->model  = mi;
    d->pos[0] = pos[0];
    d->pos[1] = pos[1];
    d->pos[2] = pos[2];
    d->yaw    = yaw;
    d->radius = radius;
    d->state  = EM_DOOR_CLOSED;

    /* World AABB of the closed door: rotate the local box by yaw (about
     * the placement origin) and take the axis-aligned bounds. */
    const DoorModel *dm = &s.models[mi];
    const float c = cosf(yaw), sn = sinf(yaw);
    d->aabb_lo[0] = d->aabb_lo[2] =  1e9f;
    d->aabb_hi[0] = d->aabb_hi[2] = -1e9f;
    for (int i = 0; i < 4; i++) {
        float lx = (i & 1) ? dm->hi[0] : dm->lo[0];
        float lz = (i & 2) ? dm->hi[2] : dm->lo[2];
        float wx =  c * lx + sn * lz;
        float wz = -sn * lx + c * lz;
        if (wx < d->aabb_lo[0]) d->aabb_lo[0] = wx;
        if (wx > d->aabb_hi[0]) d->aabb_hi[0] = wx;
        if (wz < d->aabb_lo[2]) d->aabb_lo[2] = wz;
        if (wz > d->aabb_hi[2]) d->aabb_hi[2] = wz;
    }
    for (int k = 0; k < 3; k += 2) {
        d->aabb_lo[k] += d->pos[k];
        d->aabb_hi[k] += d->pos[k];
    }
    d->aabb_lo[1] = d->pos[1] + dm->lo[1];
    d->aabb_hi[1] = d->pos[1] + dm->hi[1];

    printf("door %d: %s at (%.1f, %.1f, %.1f) yaw %.3f r %.1f — hull "
           "(%.1f, %.1f, %.1f)..(%.1f, %.1f, %.1f)\n", s.n_doors, file,
           pos[0], pos[1], pos[2], yaw, radius,
           d->aabb_lo[0], d->aabb_lo[1], d->aabb_lo[2],
           d->aabb_hi[0], d->aabb_hi[1], d->aabb_hi[2]);
    s.n_doors++;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Articulation palette                                                 */
/* ------------------------------------------------------------------ */

/* Open fraction 0..1 from the clip time. */
static float door_open_frac(const Door *d)
{
    const DoorModel *dm = &s.models[d->model];
    float total = dm->has_clip ? (float)(dm->model.frame_count - 1)
                               : DOOR_SWING_FRAMES;
    float f = d->clip_t / total;
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

static float door_clip_total(const Door *d)
{
    const DoorModel *dm = &s.models[d->model];
    return dm->has_clip ? (float)(dm->model.frame_count - 1)
                        : DOOR_SWING_FRAMES;
}

/* Build the door's world palette: clip pose (real clip, or the flagged
 * placeholder hinge swing applied to the closed pose), then the
 * placement compose T(pos) * R_y(yaw) — the same composition em_game.c
 * uses for the player (palette_apply_placement). */
static void door_build_palette(Door *d)
{
    DoorModel *dm = &s.models[d->model];
    uint32_t n = dm->model.bone_count;

    if (dm->has_clip) {
        /* Engine path: evaluate the baked clip at the current time. */
        em_model_palette_at(&dm->model, 0, (double)d->clip_t, d->palette);
    } else {
        /* PLACEHOLDER (no disc clip located yet — see header): swing the
         * whole door 90 degrees about the placement origin's Y axis; the
         * panel's hinge edge sits at local x = 0, so this reads as a
         * hinged door opening away from the doorway. */
        float a = DOOR_SWING_ANGLE * door_open_frac(d);
        float c = cosf(a), sn = sinf(a);
        for (uint32_t b = 0; b < n; b++) {
            const float *src = dm->base + b * 16;
            float       *m   = d->palette + b * 16;
            for (int col = 0; col < 4; col++) {
                float x = src[col * 4 + 0];
                float y = src[col * 4 + 1];
                float z = src[col * 4 + 2];
                m[col * 4 + 0] =  c * x + sn * z;
                m[col * 4 + 1] = y;
                m[col * 4 + 2] = -sn * x + c * z;
                m[col * 4 + 3] = src[col * 4 + 3];
            }
        }
    }

    /* Placement: every bone matrix M becomes T(pos) * R_y(yaw) * M. */
    const float c = cosf(d->yaw), sn = sinf(d->yaw);
    for (uint32_t b = 0; b < n; b++) {
        float *m = d->palette + b * 16;
        for (int col = 0; col < 4; col++) {
            float x = m[col * 4 + 0], z = m[col * 4 + 2];
            m[col * 4 + 0] =  c * x + sn * z;
            m[col * 4 + 2] = -sn * x + c * z;
        }
        m[12] += d->pos[0];
        m[13] += d->pos[1];
        m[14] += d->pos[2];
    }
}

/* ------------------------------------------------------------------ */
/* Trigger scan + state machine                                         */
/* ------------------------------------------------------------------ */

/* func_00184BA0 — the player USE SCAN over last frame's interactive
 * list. Filters: status bit 0, class flag 0x80, +0x0B == 0 (un-armed);
 * per candidate func_00183EF0: LOS clear (mask-6 static query), dist^2
 * <= 144, immediate inside 2.0 u, else facing-dot >= ~0.4; the NEAREST
 * winner gets +0x0B = 4.
 *
 * PORT DEVIATION (flagged): the engine's outer gate is the locomotion
 * action-state 0x2D (the player pressing forward — doors open on
 * walk-into). The port keeps the immediate 2.0-unit auto-open but asks
 * for the CROSS button outside it, until the player action-state
 * machine is translated. */
static void door_trigger_scan(const EmCollision *coll, const float pp[3],
                              float pyaw, const EmFrameInput *in)
{
    int   press = (in->pressed & EM_PAD_CROSS) != 0;
    float fx = sinf(pyaw), fz = cosf(pyaw);
    int   best = -1;
    float best_d2 = 1e30f;

    for (int i = 0; i < s.n_doors; i++) {
        Door *d = &s.doors[i];
        if (d->state != EM_DOOR_CLOSED || d->armed) continue;
        float dx = d->pos[0] - pp[0];
        float dz = d->pos[2] - pp[2];
        float d2 = dx * dx + dz * dz;
        if (d2 > d->radius * d->radius) continue;

        if (d2 >= DOOR_AUTO_DIST2) {
            if (!press) continue;
            float dist = sqrtf(d2);
            if (dist > 1e-4f &&
                (dx * fx + dz * fz) / dist < DOOR_FACING_DOT)
                continue;
        }
        /* LOS gate — the engine's func_0019A910 mode-6 query (static
         * cells + grid only; movable hulls never block their own scan).
         * The grid world is partitioned into sealed room boxes whose
         * BOUNDARY planes hug each doorway (verified in the office EMCL:
         * x = 60 before the west door, z = -250/-255 around the office
         * door) — the engine crosses them only via the door transit
         * (s17 func_001BBE40 MOVE-TO). A hit inside the door's own
         * doorway pocket is that boundary/jamb, not a separating wall,
         * so it must not veto the scan. FLAGGED approximation until
         * func_00183EF0's class-5 path is read (s17 open item). */
        if (coll && coll->poly_count) {
            float a[3] = { pp[0], pp[1] + 10.0f, pp[2] };
            float b[3] = { d->pos[0], d->pos[1] + 10.0f, d->pos[2] };
            EmCollHit lh;
            if (em_collision_segment_query(coll, a, b,
                                           EM_COLL_SET_CELLS |
                                           EM_COLL_SET_GRID,
                                           EM_COLL_ID_NONE, &lh)) {
                float hx = lh.point[0] - d->pos[0];
                float hz = lh.point[2] - d->pos[2];
                if (hx * hx + hz * hz > 36.0f)   /* > 6 u from the door */
                    continue;
            }
        }
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    if (best >= 0)
        s.doors[best].armed = 4;   /* the scan's +0x0B value */
}

/* func_001BBE40 — the transit KICKOFF: latch which side the player is
 * on (vs the door normal n = [sin yaw, cos yaw]), snap the player yaw
 * to the normal, LOCK input, and walk the player to the STAGING point
 * door_pos + 5.0 * n on his OWN side (s22: the engine snaps there; the
 * port drives the same point through the locomotion walk — the MOVE-TO
 * of func_00182F90). The far-side spawn point door_pos - 5.0 * n is
 * staged for the post-fade re-place (the documented spawn-table record:
 * +-5 behind the door, exit yaw). The scripted sequence is what carries
 * the player across the grid room-BOUNDARY planes (the doorways are
 * statically sealed; free walking never crosses them).
 * (The engine's camera cues + door scripts of the kickoff are not yet
 * decoded — s17 open items.) */
static void door_transit_kickoff(Door *d, const float pp[3])
{
    float nx = sinf(d->yaw), nz = cosf(d->yaw);
    float side = (pp[0] - d->pos[0]) * nx + (pp[2] - d->pos[2]) * nz;
    float dir  = (side < 0.0f) ? 1.0f : -1.0f;   /* far side of player */
    /* staging point: the player's OWN side (-dir), 5 u off the door */
    d->transit_to[0] = d->pos[0] - DOOR_POINT_DIST * dir * nx;
    d->transit_to[1] = d->pos[1];
    d->transit_to[2] = d->pos[2] - DOOR_POINT_DIST * dir * nz;
    /* spawn point: the FAR side (+dir) — the re-place while black */
    d->spawn_pt[0]   = d->pos[0] + DOOR_POINT_DIST * dir * nx;
    d->spawn_pt[1]   = d->pos[1];
    d->spawn_pt[2]   = d->pos[2] + DOOR_POINT_DIST * dir * nz;
    /* travel direction = the exit yaw (spawn recs face AWAY from the
     * door — s22 "yaw facing AWAY (exit pose)") */
    d->transit_yaw   = (dir > 0.0f) ? d->yaw : d->yaw + DOOR_PI;
    d->transit       = 1;
    d->did_warp      = 0;
    s.lock           = 1;   /* input locked until the fade-in completes */
}

void em_door_update(const EmCollision *coll, const float player_pos[3],
                    float player_yaw, const EmFrameInput *in)
{
    /* No new use-arm while a transit sequence is in flight (the engine's
     * scan filters +0x0B == 0 and the request block is busy anyway). */
    if (!s.lock)
        door_trigger_scan(coll, player_pos, player_yaw, in);

    /* Input unlocks when the fade-in completes — the re-place already
     * happened at black; the door is still closing behind the player. */
    if (s.lock && s.unlock_armed && !s.warp_pending &&
        !em_frame_fade_active() && em_frame_fade_level() <= 0.0f) {
        s.lock         = 0;
        s.unlock_armed = 0;
    }

    for (int i = 0; i < s.n_doors; i++) {
        Door *d = &s.doors[i];

        /* Walk-to completion: the MOVE-TO ends at the staging point. */
        if (d->transit) {
            float dx = player_pos[0] - d->transit_to[0];
            float dz = player_pos[2] - d->transit_to[2];
            if (dx * dx + dz * dz <= 0.09f)
                d->transit = 0;
        }

        switch (d->state) {
        case EM_DOOR_CLOSED:
            if (d->armed) {        /* sub 0 -> func_001BBE40 -> sub 3 */
                d->armed  = 0;
                d->clip_t = 0.0f;
                d->state  = EM_DOOR_OPENING;
                door_transit_kickoff(d, player_pos);
                /* PLACEHOLDER id (flagged): on the PS2 the open sound
                 * is fired by the door SCRIPT of the func_001BBE40
                 * kickoff (s17 open item) — the real id is undecoded. */
                em_sfx_play(EM_SFX_DOOR_OPEN);
            }
            break;
        case EM_DOOR_OPENING:      /* func_001BC0E0: clip 1.0/frame */
            if (d->clip_t < door_clip_total(d))
                d->clip_t += 1.0f;
            /* Commit gate: clip done (the engine's only condition — it
             * SNAPPED to staging at kickoff) AND, port-side, the walk
             * that replaced the snap has arrived. */
            if (d->clip_t >= door_clip_total(d) && !d->transit) {
                d->clip_t = door_clip_total(d);
                d->state  = EM_DOOR_OPEN;
            }
            break;
        case EM_DOOR_OPEN:         /* one-frame COMMIT (func_001BC240 ->
                                    * func_001BC150): arm the 64-frame
                                    * fade-out. Room move (B8 == 2): NO
                                    * audio fade (area changes only). */
            em_frame_fade_start(1, DOOR_FADE_SPEED);
            d->state = EM_DOOR_CLOSING;
            break;
        case EM_DOOR_CLOSING:      /* engine sub 5: transition pending */
            if (!d->did_warp) {
                /* Wait out the fade-out; at black, post the re-place
                 * (spawn point behind the door, exit yaw), arm the
                 * fade-in, and start closing — the engine's "B8
                 * cleared" moment. */
                if (!em_frame_fade_active() &&
                    em_frame_fade_level() >= 1.0f) {
                    s.warp_pending = 1;
                    s.warp_pos[0]  = d->spawn_pt[0];
                    s.warp_pos[1]  = d->spawn_pt[1];
                    s.warp_pos[2]  = d->spawn_pt[2];
                    s.warp_yaw     = d->transit_yaw;
                    d->did_warp    = 1;
                    s.unlock_armed = 1;
                    em_frame_fade_start(-1, DOOR_FADE_SPEED);
                    /* PLACEHOLDER id (flagged) — script-driven, like
                     * open; fires at black, behind the player. */
                    em_sfx_play(EM_SFX_DOOR_CLOSE);
                }
                break;
            }
            /* func_001BC290: clip back to rest, then re-arm
             * (+0x0B = 0) -> sub 0. */
            d->clip_t -= 1.0f;
            if (d->clip_t <= 0.0f) {
                d->clip_t   = 0.0f;
                d->state    = EM_DOOR_CLOSED;
                d->armed    = 0;
                d->did_warp = 0;
            }
            break;
        default:
            break;
        }
        door_build_palette(d);
    }
}

/* ------------------------------------------------------------------ */
/* Draw + collision accessors                                           */
/* ------------------------------------------------------------------ */

int em_door_count(void) { return s.n_doors; }

int em_door_input_locked(void) { return s.lock; }

int em_door_warp_pending(float out_pos[3], float *out_yaw)
{
    if (!s.warp_pending) return 0;
    out_pos[0]     = s.warp_pos[0];
    out_pos[1]     = s.warp_pos[1];
    out_pos[2]     = s.warp_pos[2];
    *out_yaw       = s.warp_yaw;
    s.warp_pending = 0;     /* one-shot */
    return 1;
}

int em_door_transit_active(float out_target[3], float *out_yaw)
{
    for (int i = 0; i < s.n_doors; i++) {
        const Door *d = &s.doors[i];
        if (!d->transit) continue;
        out_target[0] = d->transit_to[0];
        out_target[1] = d->transit_to[1];
        out_target[2] = d->transit_to[2];
        *out_yaw      = d->transit_yaw;
        return 1;
    }
    return 0;
}

void em_door_draw(int i, EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count)
{
    const Door      *d  = &s.doors[i];
    const DoorModel *dm = &s.models[d->model];
    *mesh       = dm->mesh;
    *palette    = d->palette;
    *bone_count = dm->model.bone_count;
}

int em_door_state(int i) { return s.doors[i].state; }

void em_door_pos(int i, float out[3])
{
    out[0] = s.doors[i].pos[0];
    out[1] = s.doors[i].pos[1];
    out[2] = s.doors[i].pos[2];
}

/* Segment-vs-AABB slab test. Returns the entry t in [0,1] or -1. */
static float seg_aabb(const float a[3], const float b[3],
                      const float lo[3], const float hi[3], int *axis)
{
    float t0 = 0.0f, t1 = 1.0f;
    int   ax = -1;
    for (int k = 0; k < 3; k++) {
        float dk = b[k] - a[k];
        if (fabsf(dk) < 1e-9f) {
            if (a[k] < lo[k] || a[k] > hi[k]) return -1.0f;
            continue;
        }
        float inv = 1.0f / dk;
        float n   = (lo[k] - a[k]) * inv;
        float f   = (hi[k] - a[k]) * inv;
        if (n > f) { float tmp = n; n = f; f = tmp; }
        if (n > t0) { t0 = n; ax = k; }
        if (f < t1) t1 = f;
        if (t0 > t1) return -1.0f;
    }
    if (ax < 0) return -1.0f;   /* segment starts inside: no entry face */
    *axis = ax;
    return t0;
}

int em_door_probe(const float from[3], const float to[3], EmCollHit *hit)
{
    float best_t = 2.0f;
    int   best = -1, best_axis = 0;

    for (int i = 0; i < s.n_doors; i++) {
        const Door *d = &s.doors[i];
        if (d->state == EM_DOOR_OPEN)
            continue;   /* collision suppressed only when FULLY open */
        int   axis;
        float t = seg_aabb(from, to, d->aabb_lo, d->aabb_hi, &axis);
        if (t >= 0.0f && t <= 1.0f && t < best_t) {
            best_t    = t;
            best      = i;
            best_axis = axis;
        }
    }
    if (best < 0) return 0;
    if (hit) {
        memset(hit, 0, sizeof *hit);
        for (int k = 0; k < 3; k++) {
            hit->point[k] = from[k] + (to[k] - from[k]) * best_t;
            hit->delta[k] = hit->point[k] - to[k];
        }
        hit->normal[best_axis] =
            (to[best_axis] > from[best_axis]) ? -1.0f : 1.0f;
        hit->kind       = EM_COLL_SET_HULLS;
        hit->poly       = -1;
        hit->surf_class = EM_SURF_WALL;
    }
    return 1;
}

void em_door_shutdown(EmGfx *gfx)
{
    for (int i = 0; i < s.n_models; i++) {
        if (s.models[i].mesh) {
            em_gfx_mesh_destroy(gfx, s.models[i].mesh);
            em_model_free(&s.models[i].model);
        }
    }
    memset(&s, 0, sizeof s);
}
