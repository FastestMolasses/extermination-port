/* em_props.c — placed AREA-11 set pieces (elevator platform, floor grate).
 *
 * Split out of em_game.c, which had grown to hold the entire gameplay frame.
 * These two props are self-contained: each installs from the scene manifest,
 * poses its own model, ticks its own motion, and unloads on a scene switch.
 * Behaviour is unchanged by the move — only the file boundary is new.
 *
 * The engine's counterparts are documented per function below; both are
 * driven from the same gameplay state block (EmGameState g), so this module
 * reads the subsystem's internal header rather than owning private state. */

#include "game/em_props.h"

#include "game/em_game_internal.h"

/* Re-pose the elevator platform palette at its current world placement
 * (the engine's func_001C6380 rigid pose each frame). The frame-0
 * model-local palette was baked at load; we re-apply T(elev_pos) *
 * R_y(elev_yaw) onto a fresh copy so the +0xB4 Y descent shows. Mirrors
 * palette_apply_placement but for the platform model. */
void elevator_pose(void)
{
    if (!g.elev_has_mesh || !g.elev_palette) return;
    /* rebuild frame-0 local, then place (palette_apply_placement is
     * in-place and additive, so start from the model-local pose). */
    em_model_palette_at(&g.elev_model, 0, 0.0, g.elev_palette);
    palette_apply_placement(g.elev_palette, g.elev_model.bone_count,
                            g.elev_pos, g.elev_yaw);
}

/* elevator_descent_begin — actually arm the DOWN run (state 0 -> 1):
 * rate -0.26667 u/frame, sound 0x453 (func_001FBD50(300, 0x453)). Split
 * out of em_game_elevator_start so both the immediate path (no interact
 * anim in flight) and the deferred path (the lever anim 0x47 plays first,
 * elevator_tick begins the ride when it ends) share one start. The
 * 0x81083A pose flag is 0 at the first use, so the direction is DOWN —
 * established three ways in the decode §4; the port always runs the
 * first-use DOWN descent, the mandatory opening. */
void elevator_descent_begin(void)
{
    g.elev_pending = 0;
    g.elev_state   = 1;
    g.elev_frame   = 0;
    g.elev_rate    = ELEV_RATE_DOWN;      /* +0x2E8 = 0xBE888889 */
    /* The positional down cue at the platform, radius 300. ARGUMENT
     * ORDER CORRECTED (audit 2026-07-31): this used to be written
     * "func_001FBD50(300.0, 0x453, 0)", which the BYTE-MATCHED
     * src/func_001FBD50.c contradicts — its signature is
     * `int func_001FBD50(void *a0, int a1, int a2, float f12)`, i.e. the
     * FIRST argument is an emitter/actor POINTER (forwarded to
     * func_001FBF50 together with the float in f12), and the sound id is
     * a1, which it hands to func_001FB9F0 as that call's first argument.
     * So the shape is func_001FBD50(<emitter>, 0x453, <n>, 300.0f). Only
     * the comment was wrong; the port call below is unchanged.
     * radius 300.
     * platform, radius 300. 0x453 IS now in the active sfx registry
     * (assets/sfx/sfx.txt), so this cue resolves and plays — the
     * SFX_SOUND_MAX bump brought the AREA-11 ids into the bank. (Was a
     * silent no-op while only the office bank was loaded.) */
    em_sfx_play_at(ELEV_SFX_DOWN, g.elev_has_mesh ? g.elev_pos : g.pos,
                   300.0f);
    printf("elevator: descent START — rate %.5f u/frame x %d frames "
           "(%.1f u down), sfx %#x%s\n", g.elev_rate, ELEV_FRAMES,
           -g.elev_rate * ELEV_FRAMES, ELEV_SFX_DOWN,
           g.elev_has_mesh ? "" : " [no platform mesh — player+camera "
           "descend only, FLAGGED]");
}

/* elevator_tick — the descent actor's per-frame State-1 carry
 * (0x008280F0). While descending, add the rate to the player ground-Y
 * (D_00810354 = g.pos[1]), the platform mesh-Y (+0xB4 = elev_pos[1]),
 * and re-pose the platform; the camera target-Y (D_008105E4) follows
 * g.pos[1] in the port's camera build (the engine writes it explicitly
 * — same net result since the chase target is player.y + CAM_TGT_HEIGHT).
 * Run the rate for ELEV_FRAMES then stop (state 2). Called from the
 * gameplay frame AFTER actor_update (so the floor-snap there does not
 * overwrite this write — the ride lock in player_move keeps the player
 * standing) and BEFORE camera_update (so the camera target tracks the
 * descended Y this frame). */
void elevator_tick(void)
{
    /* DEFERRED START: the powered terminal armed the descent (elev_pending)
     * while the lever interact anim 0x47 was playing. actor_update (run
     * earlier this frame) clears interact_active the frame the clip ends;
     * begin the ride here so the lever anim plays fully locked FIRST and
     * the descent's own lock then takes over with no input/motion leak. */
    if (g.elev_pending && !g.interact_active && g.elev_state == 0)
        elevator_descent_begin();

    if (g.elev_state != 1) {
        /* keep the platform posed at its rest/last Y while idle/done */
        elevator_pose();
        return;
    }
    g.pos[1]        += g.elev_rate;        /* D_00810354 player ground-Y */
    g.elev_pos[1]   += g.elev_rate;        /* actor +0xB4 platform mesh-Y */
    elevator_pose();                       /* func_001C6380 rigid re-pose */
    if (++g.elev_frame >= ELEV_FRAMES) {   /* +0x2EC < 0x96 (150) */
        g.elev_state = 2;                  /* done — runs once per scene */
        printf("elevator: descent DONE — player y %.2f\n", g.pos[1]);
    }
}

/* Free the elevator platform mesh/model/palette (scene unload). Also
 * re-arms the descent STATE (state 0, not pending) — symmetric with
 * grate_unload, which resets its flags. Called on every scene switch and
 * every `elevator` manifest re-parse, so a scene that re-installs an
 * elevator (or any future switch back into one) starts from idle. Without
 * this, a prior scene's descent that reached state 2 ("done") would latch:
 * em_game_elevator_start early-returns on elev_state != 0, so the lever
 * would silently do nothing in the next scene. (new-game arm also zeroes
 * these; this covers the mid-game scene-switch path the arm never runs.) */
void elevator_unload(EmGfx *gfx)
{
    if (g.elev_mesh)    em_gfx_mesh_destroy(gfx, g.elev_mesh);
    if (g.elev_has_mesh) em_model_free(&g.elev_model);
    free(g.elev_palette);
    g.elev_mesh    = NULL;
    g.elev_palette = NULL;
    g.elev_has_mesh = 0;
    g.elev_state   = 0;
    g.elev_pending = 0;
}

/* Re-pose the grate bars palette at the current placement, with the open
 * slide applied as a world-X offset on top of the base pos (the engine's
 * func_001C6380 rigid pose, plus the bars' keyframe retract — modeled here
 * as a rigid lateral translation, see §4/FLAG). Mirrors elevator_pose. */
static void grate_pose(void)
{
    if (!g.grate_present || !g.grate_palette) return;
    float p[3] = { g.grate_pos[0] + g.grate_slide,
                   g.grate_pos[1], g.grate_pos[2] };
    em_model_palette_at(&g.grate_model, 0, 0.0, g.grate_palette);
    palette_apply_placement(g.grate_palette, g.grate_model.bone_count,
                            p, g.grate_yaw);
}

/* Compute the bars' world AABB at install — the CLOSED hull registered
 * while the grate is locked.
 *
 * This hull is derived from the LIVE-DECODE model-local half-extents
 * (GRATE_HALF_*, the bar-GRID box 23.42 x 8.0 x 6.4 — model instance
 * 0x01350640 +0x24), NOT from the shipped area_item_04.emdl verts. That
 * carved mesh is the param-0x04 static-prop blob (a single 6.1 x 14.0 x 2.1
 * BAR), which is the WRONG geometry for record 18's grid (true model byte
 * 0x24); skinning its frame-0 verts produced only a ~6.1u-wide hull the
 * player could slip past (INVESTIGATION_area11_grate.md §5; PORT_GAPS_AREA11
 * "hull extent reconcile 6.1u vs 23.4u"). The blocker must cover the whole
 * grate opening, so we take the bar-grid half-extents centred on the actor
 * origin (g.grate_pos) and orient them by the placement yaw, using the SAME
 * rotation convention as palette_apply_placement so the box matches the
 * (eventually corrected) render.
 *
 * The local box is centred on the origin (no pivot offset): the live decode
 * lists a pivot offset for the shipped 1-bar mesh whose axes are permuted
 * relative to the grid instance, so applying it to the grid extents is not
 * faithful — origin-centred keeps the symmetric grid box squarely over the
 * gate opening. (Render-mesh fidelity is flagged separately.) */
static void grate_compute_box(void)
{
    const float c = cosf(g.grate_yaw), s = sinf(g.grate_yaw);
    EmBlockerAabb b = { 1e30f, -1e30f, 1e30f, -1e30f, 1e30f, -1e30f };

    /* 8 corners of the model-local half-extent box, rotated by yaw and
     * translated to the actor origin (palette_apply_placement convention:
     * x' = c*x + s*z ; z' = -s*x + c*z ; + pos). */
    for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2)
    for (int sz = -1; sz <= 1; sz += 2) {
        float mx = (float)sx * GRATE_HALF_X;
        float my = (float)sy * GRATE_HALF_Y;
        float mz = (float)sz * GRATE_HALF_Z;
        float wx =  c*mx + s*mz + g.grate_pos[0];
        float wy =  my            + g.grate_pos[1];
        float wz = -s*mx + c*mz + g.grate_pos[2];
        if (wx < b.minX) b.minX = wx; if (wx > b.maxX) b.maxX = wx;
        if (wy < b.minY) b.minY = wy; if (wy > b.maxY) b.maxY = wy;
        if (wz < b.minZ) b.minZ = wz; if (wz > b.maxZ) b.maxZ = wz;
    }
    g.grate_box = b;
}

/* Install the grate for this scene: load the bars mesh, build the closed
 * hull from its verts, and seat it CLOSED (unlock bit clear at new game).
 * Returns 0 on success, nonzero if the mesh failed (no grate then). One
 * grate per scene; a second call replaces the first. Mirrors the elevator
 * install. */
int grate_install(EmGfx *gfx, const char *scene_dir,
                         const char *name, const float pos[3], float yaw)
{
    grate_unload(gfx);                          /* drop any prior grate */
    g.grate_pos[0] = pos[0];
    g.grate_pos[1] = pos[1];
    g.grate_pos[2] = pos[2];
    g.grate_yaw    = yaw;
    g.grate_open   = 0;
    g.grate_done   = 0;
    g.grate_frame  = 0;
    g.grate_slide  = 0.0f;

    char path[1024];
    snprintf(path, sizeof path, "%s/%s", scene_dir, name);
    if (em_model_load(&g.grate_model, path) != 0) {
        printf("manifest: grate mesh %s failed to load — grate ABSENT "
               "(no blocker, no draw)\n", path);
        return -1;
    }
    g.grate_mesh = em_gfx_mesh_create(
        gfx, g.grate_model.verts, g.grate_model.vert_count,
        g.grate_model.indices, g.grate_model.index_count,
        (const EmGfxTexDesc *)g.grate_model.texs,
        g.grate_model.tex_count, g.grate_model.texels,
        g.grate_model.flags);
    g.grate_palette = malloc(g.grate_model.bone_count * 16 * sizeof(float));
    if (!g.grate_mesh || !g.grate_palette) {
        printf("manifest: grate mesh %s GPU/palette alloc failed — grate "
               "ABSENT\n", path);
        grate_unload(gfx);
        return -1;
    }
    g.grate_present = 1;
    grate_compute_box();                        /* the closed hull */
    grate_pose();                               /* bake the closed pose */
    printf("manifest: GRATE hull (closed, bar-grid extents %.2fx%.2fx%.2f) "
           "world AABB X[%.2f,%.2f] Y[%.2f,%.2f] Z[%.2f,%.2f] — render mesh "
           "%u verts\n",
           2.0f*GRATE_HALF_X, 2.0f*GRATE_HALF_Y, 2.0f*GRATE_HALF_Z,
           g.grate_box.minX, g.grate_box.maxX,
           g.grate_box.minY, g.grate_box.maxY,
           g.grate_box.minZ, g.grate_box.maxZ, g.grate_model.vert_count);
    return 0;
}

/* grate_update — the grate's per-frame brain (func_00159210's ACTIVE/open
 * machine, condensed). Run ONCE per gameplay frame, BEFORE actor_update so
 * the closed hull is registered before player_move's ground-solve consults
 * em_collision_blocker_probe (the single player-code touch). The registry
 * is cleared once at the top of the sim (em_collision_blocker_clear,
 * alongside the moving-surface clear).
 *
 *   NOT powered  -> register the CLOSED hull (the grate blocks the path);
 *                   bars stay at the closed pose.
 *   powered      -> drop the hull (path opens) and run the ~120-frame
 *                   SLIDE: the bars retract along world X by GRATE_SLIDE_DX
 *                   (FLAGGED direction/extent — §4). After the slide the
 *                   grate is fully open (done): no blocker, bars parked at
 *                   the retracted pose. */
void grate_update(void)
{
    if (!g.grate_present) return;

    int powered = em_game_terminal_powered();

    if (!powered) {
        /* CLOSED: register the solid hull each frame (per-frame registry,
         * cleared at the top of the sim). The bars sit at the closed pose.
         * N1: the registry caps at EM_BLOCKER_MAX (8) and returns -1 when
         * full. AREA-11 uses 1 of 8, so this never fires today; but a future
         * scene that ships a 9th blocker would otherwise have the grate hull
         * SILENTLY dropped (the player would walk through a "closed" grate
         * with no diagnostic — the exact silent-drop shape that bit the SFX
         * cap). Log once on overflow, mirroring the loaders. */
        if (em_collision_blocker_register(&g.grate_box) < 0) {
            static int warned;
            if (!warned) {
                fprintf(stderr, "grate: blocker registry full — closed hull "
                        "DROPPED; the grate will not block\n");
                warned = 1;
            }
        }
        g.grate_slide = 0.0f;
        grate_pose();
        return;
    }

    /* POWERED: the path is OPEN — drop the hull (do NOT register) and run
     * the open slide. The hull removal happens at slide START (the §4 prior:
     * the player is freed the moment the area powers, the bars then retract
     * cosmetically). The slide is a rigid lateral retract; the keyframed
     * bars motion couldn't be frozen live, so this is FLAGGED for tuning. */
    if (!g.grate_open) {
        g.grate_open  = 1;
        g.grate_frame = 0;
        printf("grate: OPEN — area powered, hull dropped, %d-frame slide "
               "(+%.1f u world-X) BEGINS [FLAG: slide dir/extent]\n",
               GRATE_SLIDE_FR, GRATE_SLIDE_DX);
    }
    if (!g.grate_done) {
        g.grate_frame++;
        float t = (float)g.grate_frame / (float)GRATE_SLIDE_FR;
        if (t >= 1.0f) { t = 1.0f; g.grate_done = 1; }
        g.grate_slide = GRATE_SLIDE_DX * t;     /* linear retract */
        grate_pose();
        if (g.grate_done)
            printf("grate: slide DONE — bars fully retracted (+%.1f u)\n",
                   g.grate_slide);
    }
}

/* Free the grate mesh/model/palette (scene unload). */
void grate_unload(EmGfx *gfx)
{
    if (g.grate_mesh)    em_gfx_mesh_destroy(gfx, g.grate_mesh);
    if (g.grate_present) em_model_free(&g.grate_model);
    free(g.grate_palette);
    g.grate_mesh    = NULL;
    g.grate_palette = NULL;
    g.grate_present = 0;
    g.grate_open    = 0;
    g.grate_done    = 0;
    g.grate_slide   = 0.0f;
    memset(&g.grate_box, 0, sizeof g.grate_box);
}
