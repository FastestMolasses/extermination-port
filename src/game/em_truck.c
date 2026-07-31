/* em_truck.c — the AREA-11 WEDGED TRUCK actor (placement record 16,
 * behavior ov 0x00823FF0) and its paired AABB fall trigger (record 17,
 * ov 0x008251E0). The "run across the top before it falls into the
 * crevice" set-piece. See em_truck.h and the full decode in
 * Extermination/docs/INVESTIGATION_first_level_area11.md §11 + §11.1a.
 *
 * The truck self-contains its behavior; the only seams into the rest of
 * the port are: the moving-surface registry (em_collision_moving_*,
 * which the player ground-solve consumes for the carry), em_sfx_play_at
 * (the audio cue), and the render-chain draw publish (em_game.c).
 *
 * PROVENANCE (audit, 2026-07) — READ BEFORE TRUSTING ANY "decoded"
 * BELOW. Everything in this file traces to two AREA-11 OVERLAY behavior
 * functions (ov 0x00823FF0 / 0x008251E0). Overlay code is NOT part of
 * the decompilation's recovered set — there is no
 * Extermination/src/func_*.c for either address — and the cited
 * INVESTIGATION_first_level_area11.md does not exist in the decomp
 * repo's docs/. So none of the constants below can be re-checked
 * against byte-matched C: treat every "decoded" in this file as
 * DISASSEMBLY-OBSERVED, one session's reading, not source-derived.
 * The one thing that DOES check out internally is the velocity ramp:
 * the three hex immediates quoted below decode to exactly the three
 * float literals used (0xBD088889 = -0.0333333, 0xBE088889 =
 * -0.1333333, 0xBF2AAAAB = -0.6666667), so at least the transcription
 * is self-consistent. The frame BEAT thresholds are explicitly
 * admitted-unfrozen by the note further down; the AABB bands and the
 * state-byte numbering have no independent confirmation at all. */

#include "em_truck.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "em_collision.h"
#include "em_sfx.h"
#include "../em_model.h"
#include "../em_gfx.h"

/* ---- observed constants (overlay disassembly only — see PROVENANCE) --- */

/* §11.3 state byte (PS2 actor +0x04). */
#define TRUCK_STATE_DONE   0   /* fall complete (terminated; PS2 state 3)  */
#define TRUCK_STATE_FALL   1   /* the timed tumble                         */
#define TRUCK_STATE_WEDGED 4   /* idle/teeter; the spawn state             */

/* §11.2 trigger AABB on the player footprint (player X D_00810350 / Z
 * D_00810358 vs the hardcoded bands). The OBSERVED constants were X
 * bands ~[319, 336] and Z bands around [390, 413, 427]; the bounds used
 * here are the outer span the player must enter to arm the fall.
 * DOWNGRADED (audit): overlay-only, unverifiable — and note the source
 * reading gives THREE Z bands while the port collapses them to one
 * span, so the middle boundary (413) is being discarded. */
#define TRUCK_TRIG_MIN_X 319.0f
#define TRUCK_TRIG_MAX_X 336.0f
#define TRUCK_TRIG_MIN_Z 390.0f
#define TRUCK_TRIG_MAX_Z 427.0f

/* §11.3 fall velocity ramp (the per-frame velocity scratch
 * D_70003.._38A8): the downward component steps through
 * -0.0333 -> -0.1333 -> -0.6667, i.e. the drop ACCELERATES. Exact PS2
 * immediates 0xBD088889 / 0xBE088889 / 0xBF2AAAAB. */
#define TRUCK_VEL_SLOW (-0.0333333f)  /* 0xBD088889 */
#define TRUCK_VEL_MID  (-0.1333333f)  /* 0xBE088889 */
#define TRUCK_VEL_FAST (-0.6666667f)  /* 0xBF2AAAAB */

/* §11.3 the fall runs ~0x41+ (65+) frames ~= a ~1-second window from the
 * first beat to the truck dropping out of reach. The exact per-frame BEAT
 * timing (the +0x28 jump-table thresholds v1<0x0A, ==8, <0x0F, ==0x1C,
 * <0x1E, <0x2A, ==0x28, ==0x32, <0x34, <0x41 ...) was NEVER frozen live
 * (§11.5: "the §11.5 fall timeline was never frozen live"). This first
 * pass implements the decoded ACCELERATING ramp over the decoded ~65-frame
 * window with two ramp boundaries placed at the decoded jump-table beats
 * (mid ~0x0F = 15, fast ~0x2A = 42). FLAGGED for live tuning: walk the
 * player into the trigger AABB with an exec breakpoint at 0x00823FF0
 * (+0x04 == 1) and read +0x28 / +0xB4 / the velocity scratch across frames
 * to freeze the true beat thresholds (§11.5). */
#define TRUCK_FALL_FRAMES   65   /* ~0x41, the ~1 s window                 */
#define TRUCK_RAMP_MID_F    15   /* ~0x0F: slow -> mid                     */
#define TRUCK_RAMP_FAST_F   42   /* ~0x2A: mid -> fast                     */
#define TRUCK_CRASH_BEAT_F   8   /* ~==8: the groan/crash beat (re-cue)    */

/* §11.3 the truck tumbles/tips as it falls (rotates its facing vectors
 * func_00102B08 / func_00102A60). FLAGGED-APPROX: the exact rotation axes
 * and per-beat angles were not frozen live; the faithful first pass tips
 * the placed rx tilt further (nose-down) over the fall window so the truck
 * visibly tumbles into the crevice. */
#define TRUCK_TUMBLE_RX (-0.9f)  /* extra nose-down pitch over the fall    */

/* §11.1/§11.2 audio: sound 0x454 = the groan/crash cue (func_001BA1A0 /
 * the fall beats). 0x454 IS now in the active sfx registry
 * (assets/sfx/sfx.txt), so this cue resolves and plays — the SFX_SOUND_MAX
 * bump that fixed the silent-drop also brought it into the bank. (Was
 * formerly a silent no-op while the office bank was loaded.) */
#define TRUCK_SFX_FALL 0x454u

/* §11.4 the player-state gate: the PS2 trigger gates the arm on
 * D_008102B5 < 2 (a player-state byte: not mid-scripted/damage). PORT
 * STAND-IN: the gameplay frame only ticks the truck during free gameplay
 * (the door/examine/damage locks already suppress player_move), so the
 * caller passes whether the player is in a normal (non-scripted, non-
 * damage) state. We additionally require the player to actually be near
 * the truck height to avoid arming from an unrelated faraway footprint
 * match (defensive; the PS2 volume is a true 3D-ish band). FLAGGED: the
 * exact D_008102B5 semantics ("< 2") are a stand-in (§11.2). */

/* The walkable-top footprint of the truck. The truck mesh is model-local
 * (identity placement) with bbox X[-13.80,13.80] Y[0.00,33.60]
 * Z[-31.00,30.00] (§11.1a). At the placed yaw ry = pi/2 the model's long
 * Z axis maps to world X and the short X axis to world Z, so the
 * world-space footprint half-extents swap. TOP_Y is the placed Y plus the
 * model-local top (33.6) — the surface the player stands on. These define
 * the AABB the moving-surface registry uses for the carry. */
#define TRUCK_MODEL_HALF_X 13.80f
#define TRUCK_MODEL_HALF_Z 30.50f   /* (31.0+30.0)/2 about the model origin */
#define TRUCK_MODEL_TOP_Y  33.60f

typedef struct {
    int   present;     /* a truck is installed this scene */
    int   state;       /* TRUCK_STATE_* (PS2 +0x04) */
    int   armed;       /* the engine D_00810792 "truck-fall armed" flag */
    int   fall_frame;  /* the fall counter (PS2 +0x28) */
    int   teeter;      /* the wedged idle counter (PS2 +0x1F0) */

    float pos[3];      /* world position (PS2 +0xB0); falls in state 1 */
    float rx;          /* current Euler tilt (PS2 +0xC0); tips during fall */
    float rx_base;     /* placed tilt (the wedged pose); fall tips from here */
    float ry;          /* placed yaw (pi/2) */
    float vel[3];      /* this-frame velocity (PS2 scratch _38A8) */

    /* render */
    int        has_mesh;
    EmModel    model;
    EmGfxMesh *mesh;
    float     *palette;
    EmGfx     *gfx;    /* owning gfx (for unload) */
} EmTruck;

static EmTruck t;

/* ----------------------------------------------------------------------- */

static void truck_unload_mesh(void)
{
    if (t.mesh && t.gfx) em_gfx_mesh_destroy(t.gfx, t.mesh);
    if (t.has_mesh)      em_model_free(&t.model);
    free(t.palette);
    t.mesh     = NULL;
    t.palette  = NULL;
    t.has_mesh = 0;
}

void em_truck_clear(EmGfx *gfx)
{
    if (gfx) t.gfx = gfx;
    truck_unload_mesh();
    memset(&t, 0, sizeof t);
}

/* truck_pose — re-pose the truck palette at its current world placement
 * (the engine builds its own 4x4 each frame; here T(pos) * R_y(ry) *
 * R_x(rx) onto a fresh copy of the model-local frame-0 palette). Mirrors
 * em_game's palette_apply_placement but adds the X-tilt the truck carries
 * (palette_apply_placement only rotates about Y). */
static void truck_pose(void)
{
    if (!t.has_mesh || !t.palette) return;
    em_model_palette_at(&t.model, 0, 0.0, t.palette);

    const float cy = cosf(t.ry), sy = sinf(t.ry);
    const float cx = cosf(t.rx), sx = sinf(t.rx);
    for (uint32_t b = 0; b < t.model.bone_count; b++) {
        float *m = t.palette + b * 16;
        for (int col = 0; col < 4; col++) {
            float x = m[col * 4 + 0];
            float y = m[col * 4 + 1];
            float z = m[col * 4 + 2];
            /* R_x (pitch about world X): rotate (y,z) */
            float y1 =  cx * y - sx * z;
            float z1 =  sx * y + cx * z;
            /* R_y (yaw about world Y): rotate (x,z1) */
            float x2 =  cy * x + sy * z1;
            float z2 = -sy * x + cy * z1;
            m[col * 4 + 0] = x2;
            m[col * 4 + 1] = y1;
            m[col * 4 + 2] = z2;
        }
        m[12] += t.pos[0];
        m[13] += t.pos[1];
        m[14] += t.pos[2];
    }
}

int em_truck_install(EmGfx *gfx, const char *model_path,
                     const float pos[3], float rx, float ry)
{
    em_truck_clear(gfx);            /* drop any prior truck */
    t.gfx     = gfx;
    t.present = 1;
    t.state   = TRUCK_STATE_WEDGED; /* spawn wedged (PS2 +0x04 = 4) */
    t.armed   = 0;                  /* D_00810792 = 0 (untriggered) */
    t.pos[0]  = pos[0];
    t.pos[1]  = pos[1];
    t.pos[2]  = pos[2];
    t.rx      = rx;
    t.rx_base = rx;
    t.ry      = ry;
    t.vel[0]  = t.vel[1] = t.vel[2] = 0.0f;

    if (!model_path || em_model_load(&t.model, model_path) != 0) {
        printf("truck: mesh %s failed to load — the wedged-truck set-piece "
               "is ABSENT this scene (no trigger/fall)\n",
               model_path ? model_path : "(none)");
        t.present  = 0;
        t.has_mesh = 0;
        return -1;
    }
    t.mesh = em_gfx_mesh_create(gfx, t.model.verts, t.model.vert_count,
                                t.model.indices, t.model.index_count,
                                (const EmGfxTexDesc *)t.model.texs,
                                t.model.tex_count, t.model.texels,
                                t.model.flags);
    t.palette = malloc(t.model.bone_count * 16 * sizeof(float));
    if (!t.mesh || !t.palette) {
        printf("truck: GPU/palette alloc failed — set-piece ABSENT\n");
        truck_unload_mesh();
        t.present = 0;
        return -1;
    }
    t.has_mesh = 1;
    truck_pose();                   /* bake the placed frame-0 wedged pose */
    printf("truck: WEDGED at (%.1f, %.1f, %.1f) rx %.3f ry %.3f — %u verts "
           "(state 4, fall armed=0)\n", t.pos[0], t.pos[1], t.pos[2],
           t.rx, t.ry, t.model.vert_count);
    return 0;
}

int em_truck_present(void) { return t.present; }

/* The world-space walkable-top footprint AABB this frame (yaw pi/2 swaps
 * the model's X/Z half-extents). top_y = placed (rest) Y + model top,
 * tracked down with the falling pos[1]. */
static void truck_footprint(float *minX, float *maxX,
                            float *minZ, float *maxZ, float *top_y)
{
    /* ry ~= pi/2: model long-Z -> world X, model short-X -> world Z. Use
     * |cos|/|sin| to project the half-extents so this stays correct for
     * the exact placed yaw without hardcoding the swap. */
    float hx = fabsf(cosf(t.ry)) * TRUCK_MODEL_HALF_X +
               fabsf(sinf(t.ry)) * TRUCK_MODEL_HALF_Z;
    float hz = fabsf(sinf(t.ry)) * TRUCK_MODEL_HALF_X +
               fabsf(cosf(t.ry)) * TRUCK_MODEL_HALF_Z;
    *minX = t.pos[0] - hx;
    *maxX = t.pos[0] + hx;
    *minZ = t.pos[2] - hz;
    *maxZ = t.pos[2] + hz;
    *top_y = t.pos[1] + TRUCK_MODEL_TOP_Y;
}

/* §11.2 the trigger: player footprint inside the AABB (X[319,336],
 * Z[390,427]). Returns 1 when the player is in the volume. */
static int truck_trigger_hit(const float player_pos[3])
{
    return player_pos[0] >= TRUCK_TRIG_MIN_X &&
           player_pos[0] <= TRUCK_TRIG_MAX_X &&
           player_pos[2] >= TRUCK_TRIG_MIN_Z &&
           player_pos[2] <= TRUCK_TRIG_MAX_Z;
}

/* §11.3 the fall velocity for the current fall frame (accelerating ramp;
 * FLAGGED beat boundaries — see TRUCK_RAMP_* above). */
static float truck_fall_vy(int frame)
{
    if (frame >= TRUCK_RAMP_FAST_F) return TRUCK_VEL_FAST;
    if (frame >= TRUCK_RAMP_MID_F)  return TRUCK_VEL_MID;
    return TRUCK_VEL_SLOW;
}

/* Arm the fall: set the armed flag (D_00810792), transition WEDGED->FALL,
 * fire the audio cue. §11.2 + §11.3. */
static void truck_arm(void)
{
    t.armed      = 1;               /* D_00810792 = 1 */
    t.state      = TRUCK_STATE_FALL;
    t.fall_frame = 0;
    /* §11.3 the groan/crash cue on the fall. 0x454 is now in the registry
     * (see header), so this resolves and plays. */
    em_sfx_play_at(TRUCK_SFX_FALL, t.pos, 300.0f);
    printf("truck: ARMED — fall begins (D_00810792=1), sfx %#x, %d-frame "
           "window [FLAGGED beat timeline — §11.5 live-confirm]\n",
           TRUCK_SFX_FALL, TRUCK_FALL_FRAMES);
}

void em_truck_force_arm(void)
{
    if (t.present && t.state == TRUCK_STATE_WEDGED) truck_arm();
}

void em_truck_update(const float player_pos[3])
{
    if (!t.present) return;

    if (t.state == TRUCK_STATE_WEDGED) {
        /* §11.3 STATE 4 — WEDGED/IDLE: a small teeter telegraphing
         * instability while jammed (PS2 +0x1F0 jump-table of subtle pose
         * nudges). No descent. The faithful first pass keeps the placed
         * pose (the teeter is sub-unit cosmetic; FLAGGED-omitted nudge). */
        t.teeter++;
        t.vel[0] = t.vel[1] = t.vel[2] = 0.0f;   /* WEDGED: vel = 0 */

        /* §11.2 trigger: player in the AABB (+ the player-state gate — see
         * header; the caller only ticks us during free gameplay). Arm and
         * fall. */
        if (player_pos && truck_trigger_hit(player_pos))
            truck_arm();
    }

    if (t.state == TRUCK_STATE_FALL) {
        /* §11.3 STATE 1 — THE FALL: accelerating downward ramp, integrate
         * into the truck position each frame so it drops into the crevice;
         * tumble the facing; re-cue the crash beat. */
        float vy = truck_fall_vy(t.fall_frame);
        t.vel[0] = 0.0f;
        t.vel[1] = vy;     /* the §11.3 downward scratch _38A8 */
        t.vel[2] = 0.0f;

        t.pos[1] += vy;    /* integrate (PS2 convergence block 0x00825014) */

        /* §11.3 tumble: tip the nose down progressively over the window.
         * Computed as an ABSOLUTE offset from the placed wedged tilt
         * (rx_base) by the fall progress, so repeated frames don't run
         * away (FLAGGED-APPROX axis/angle — see TRUCK_TUMBLE_RX). */
        float prog = (float)t.fall_frame / (float)TRUCK_FALL_FRAMES;
        if (prog > 1.0f) prog = 1.0f;
        t.rx = t.rx_base + TRUCK_TUMBLE_RX * prog;

        /* §11.3 the crash beat re-cue (~frame 8). FLAGGED FX: the debris/
         * snow bursts (func_001EFD20) are not reproduced — no native gib/FX
         * path is wired for the truck top here; flagged for later. */
        if (t.fall_frame == TRUCK_CRASH_BEAT_F)
            em_sfx_play_at(TRUCK_SFX_FALL, t.pos, 300.0f);

        if (++t.fall_frame >= TRUCK_FALL_FRAMES) {
            t.state  = TRUCK_STATE_DONE;   /* dropped out of reach (PS2 st3) */
            t.vel[0] = t.vel[1] = t.vel[2] = 0.0f;
            printf("truck: FALL complete — dropped to y %.2f (out of "
                   "reach)\n", t.pos[1]);
        }
    }

    /* Re-pose the mesh at the current placement (rigid prop pose each
     * frame, like the elevator platform). */
    truck_pose();

    /* §11.4 register the truck's walkable-top footprint + this-frame
     * velocity with the moving-surface registry. The player ground-solve
     * (em_collision_moving_carry) reads this and, if the player stands on
     * the footprint, ADDS the velocity to the player — so a player on top
     * during the fall is carried DOWN into the crevice (the fail), and one
     * who runs off leaves the footprint and is safe. WEDGED: vel = 0 (the
     * player can stand on the still truck with no carry); FALLING: the
     * accelerating downward vel. */
    {
        float minX, maxX, minZ, maxZ, top_y;
        truck_footprint(&minX, &maxX, &minZ, &maxZ, &top_y);
        /* N1: the moving-surface registry caps at 8 slots and returns -1 when
         * full. AREA-11 uses 1 of 8, so this never fires today; but a future
         * scene with a 9th moving surface would otherwise have the truck top
         * SILENTLY dropped — the player would fall through the walkable top
         * (or not be carried into the crevice) with no diagnostic. That is the
         * exact silent-drop shape that bit the SFX cap. Log once on overflow,
         * mirroring the loaders. */
        if (em_collision_moving_register(minX, maxX, minZ, maxZ,
                                         top_y, t.vel) < 0) {
            static int warned;
            if (!warned) {
                fprintf(stderr, "truck: moving-surface registry full — "
                        "walkable top DROPPED; no carry this frame\n");
                warned = 1;
            }
        }
    }
}

int em_truck_draw(EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count)
{
    if (!t.present || !t.has_mesh || !t.mesh || !t.palette) return 0;
    *mesh       = t.mesh;
    *palette    = t.palette;
    *bone_count = t.model.bone_count;
    return 1;
}

int em_truck_state(void) { return t.present ? t.state : TRUCK_STATE_DONE; }

void em_truck_pos(float out[3])
{
    out[0] = t.pos[0];
    out[1] = t.pos[1];
    out[2] = t.pos[2];
}

/* ----------------------------------------------------------------------- */
/* Headless self-test (EM_TRUCK_TEST=1): a software-only check of the
 * trigger->arm->fall->carry chain with no GPU/scene. Asserts:
 *  - spawn WEDGED, vel 0, carry of a rider on the top = no drop;
 *  - a player OUTSIDE the trigger AABB does NOT arm;
 *  - a player INSIDE the trigger AABB arms -> FALL, vel ramps down;
 *  - a rider on the footprint during the fall is carried DOWN;
 *  - a rider OFF the footprint is not carried (safe);
 *  - the fall terminates after the window. */

static int trk_check(int cond, const char *what, int *fail)
{
    if (cond) return 1;
    (*fail)++;
    printf("truck test: CHECK FAILED — %s\n", what);
    return 0;
}

int em_truck_moving_selftest(void)
{
    int fail = 0;

    /* Seat a mesh-less truck directly (no GPU): bypass em_truck_install's
     * mesh load by hand-seating the state. The carry math is mesh-free. */
    memset(&t, 0, sizeof t);
    t.present = 1;
    t.state   = TRUCK_STATE_WEDGED;
    t.pos[0]  = 380.8f; t.pos[1] = 164.0f; t.pos[2] = 391.1f;
    t.rx      = -0.314f;
    t.rx_base = -0.314f;
    t.ry      = 1.5708f;

    /* A point standing on the truck top (footprint center, at top_y). */
    float minX, maxX, minZ, maxZ, top_y;
    truck_footprint(&minX, &maxX, &minZ, &maxZ, &top_y);
    float ride[3] = { t.pos[0], top_y, t.pos[2] };

    /* WEDGED: tick with the player OUTSIDE the trigger AABB -> no arm. */
    float far_player[3] = { 250.0f, 230.0f, 209.0f };
    em_collision_moving_clear();
    em_truck_update(far_player);
    trk_check(em_truck_state() == TRUCK_STATE_WEDGED,
              "stays WEDGED with the player outside the trigger", &fail);
    float r0[3] = { ride[0], ride[1], ride[2] };
    int c0 = em_collision_moving_carry(r0);
    trk_check(c0 == 1, "a rider on the WEDGED top is on the footprint",
              &fail);
    trk_check(r0[1] == ride[1], "WEDGED truck carries no drop (vel 0)",
              &fail);

    /* Step the player INTO the trigger AABB -> arm + FALL. */
    float in_player[3] = { 327.0f, 230.0f, 408.0f };  /* mid of [319,336]x[390,427] */
    em_collision_moving_clear();
    em_truck_update(in_player);
    trk_check(em_truck_state() == TRUCK_STATE_FALL,
              "player inside the trigger ARMS the fall (WEDGED->FALL)",
              &fail);

    /* A rider on the footprint during the fall is carried DOWN. Re-read the
     * footprint (the truck has dropped one frame) and place a rider on it. */
    truck_footprint(&minX, &maxX, &minZ, &maxZ, &top_y);
    float rider[3] = { t.pos[0], top_y, t.pos[2] };
    float ry0 = rider[1];
    int c1 = em_collision_moving_carry(rider);
    trk_check(c1 == 1, "a rider on the FALLING top is carried", &fail);
    trk_check(rider[1] < ry0, "the falling truck carries the rider DOWN",
              &fail);

    /* A rider OFF the footprint is not carried (the safe escape). */
    em_collision_moving_clear();
    em_truck_update(in_player);
    float off[3] = { t.pos[0] + 200.0f, top_y, t.pos[2] };
    float off_y = off[1];
    int c2 = em_collision_moving_carry(off);
    trk_check(c2 == 0, "a rider OFF the footprint is not carried (safe)",
              &fail);
    trk_check(off[1] == off_y, "the off-footprint rider's Y holds", &fail);

    /* The velocity accelerates: drive the fall to the fast band and check
     * the registered velocity got more negative. */
    float vy_early = truck_fall_vy(0);
    float vy_late  = truck_fall_vy(TRUCK_FALL_FRAMES - 1);
    trk_check(vy_late < vy_early,
              "the fall velocity accelerates (late < early)", &fail);

    /* Run the fall to completion -> DONE. */
    for (int i = 0; i < TRUCK_FALL_FRAMES + 4; i++) {
        em_collision_moving_clear();
        em_truck_update(in_player);
    }
    trk_check(em_truck_state() == TRUCK_STATE_DONE,
              "the fall terminates after the window (state DONE)", &fail);

    printf("truck test: %s\n", fail == 0 ? "PASS" : "FAIL");
    fflush(stdout);

    memset(&t, 0, sizeof t);   /* leave the module clean */
    return fail;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
static void em_truck_test_ctor(void)
{
    const char *e = getenv("EM_TRUCK_TEST");
    if (e && e[0] == '1')
        exit(em_truck_moving_selftest() == 0 ? 0 : 1);
}
#endif
