/* em_game.c — slot-0 game task chain (PS2 -> native mapping).
 *
 * Engine chain (FINDINGS.md "ENGINE FRAME ANATOMY") and what stands in for
 * each stage here:
 *
 *   func_001AB7E0  boot/flow task        game_boot_task() — one frame:
 *                                        loads the player model + scene
 *                                        parts, then re-registers slot 0
 *                                        with the game task (exactly how
 *                                        the engine replaces it live).
 *   func_001ACEC0  game task machine     game_task() — switch on the slot
 *                  (state byte task+8)   record's user byte 0.
 *   func_001AD250  sub-machine           game_sub_machine() — user byte 1;
 *                  (6 states, jr-table   the live arm reaches the in-game
 *                  0x0026DCB0)           frame machine through the
 *                                        trampoline func_001AD4D0
 *                                        (= j func_001AE040).
 *   func_001AE040  in-game frame machine ingame_frame_machine() — user
 *                  (state byte task+0xB) byte 3 (task+0xB). Its live state
 *                                        runs per-frame services (flag
 *                                        reset, placement, camera, HUD
 *                                        context, end-of-level poll — all
 *                                        skeleton no-ops here), then the
 *                                        selector (scratchpad 0x70003B8D)
 *                                        picks the frame variant.
 *   func_001AE5E0  GAMEPLAY FRAME        gameplay_frame() — see below.
 *   func_001AE6B0  cutscene variant      cutscene_frame() — skeleton; the
 *                                        native selector never routes here
 *                                        yet.
 *
 * GAMEPLAY FRAME stages (func_001AE5E0) -> native:
 *   func_001CB590 actor-context begin    actor_context_begin() — marks the
 *                                        actor table the update writes
 *                                        (the engine sets the D_00275B40
 *                                        node-table base; one player actor
 *                                        natively, so it just selects it).
 *   func_0015BCF0 PLAYER ACTOR UPDATE    actor_update() — the port's anim
 *                                        advance: evaluate the bone palette
 *                                        at the current clip time and place
 *                                        the actor at its world position
 *                                        (the engine's anim-evaluator +
 *                                        physics spine, asset-side only
 *                                        for now).
 *   func_001CB5A0 actor-context end      actor_context_end().
 *   func_001D1C50 RENDER CHAIN BUILD     render_chain_build() — records
 *                                        this frame's draws (scene parts +
 *                                        player, or the test triangle) into
 *                                        a chain, the native form of the
 *                                        VIF packet chain. The PS2 kick
 *                                        streams packets whose camera
 *                                        matrix slot is filled afterwards;
 *                                        natively the recorded chain is
 *                                        flushed after camera apply, at
 *                                        close-out.
 *   func_001C1D00 render-env init        render_env_init() — once-per-
 *                  (flag block           area render-env setup (GS regs,
 *                  0x008101D0)           per-area specials). NOT camera
 *                                        math (corrected by FINDINGS.md
 *                                        "CAMERA SYSTEM"); skeleton no-op.
 *   func_001AFD70 / func_0015C160 /      world services — skeleton no-ops.
 *   func_001F0360
 *   func_001CB590(0x008101E0, 0xD0, 0)   camera_update() — THE CAMERA:
 *   + func_0018B9C0 camera machine       camera-context begin + the
 *                                        camera state machine (struct
 *                                        0x008101E0; see the CAMERA
 *                                        section below).
 *   func_001CB5A0 / func_001AAD00 /      frame_close_out() — flushes the
 *   func_001D1EA0(1) close-out           draw chain with the committed
 *                                        camera and advances clip time.
 *
 * INTERACTIVE MOVEMENT (first slice of the real actor spine): the frame
 * input block drives the player around the room. Left stick (WASD)
 * moves camera-relative on the XZ plane through the engine's ANALOG
 * GAIT quantizer (rings 48/88/122 -> turn-in-place / walk 6 u/s /
 * run 18 u/s; keyboard full push = run, Alt = the walk band); the
 * facing yaw seeks the movement direction (smooth turn). The placement
 * is composed onto the evaluated anim palette each frame (rotation
 * about Y by yaw, then translation — AFTER the animation pose; see
 * palette_apply_placement). Movement runs through the engine's
 * COLLISION WORLD (src/game/em_collision.[hc] over the id 0x44 EMCL
 * bake): the engine's own RADIAL WALL PROBES (five directions, the
 * 4.5-unit player radius — see PLAYER WALL RADIUS below) push the
 * player out of walls, and a vertical segment query sets the floor
 * height; with no collision asset the old room-bbox clamp remains.
 * The camera is the engine's own chase camera (clamped proportional
 * follow, FINDINGS.md "CAMERA SYSTEM" port contract) with its
 * desired-eye solver the DECODED func_0018DD20 (style 0, mask 6 —
 * cam_solver_0018DD20, now in em_camera.c) against the same world;
 * CONFIRMED against src/func_0018DD20.c (NEARMISS — logic
 * authoritative): the wall arm writes only pos[0]/pos[2] (`+= 0.5f *
 * push`), leaving pos[1] alone, so the pull-in really is at constant
 * height. The player has
 * NO free camera control — R1/L1 orient the camera behind the player,
 * idle auto-orients slowly, and a blocking wall PULLS the eye in at
 * constant height (which reads as the camera rising over the player —
 * the CAMERA FIDELITY block below). While the status screen is open the world
 * simulation PAUSES (the gate in gameplay_frame). Esc still quits
 * (em_frame.c step C).
 *
 * SCENE MANIFEST: assets/scene/scene.txt (see scene_manifest_load) gives
 * each scene its own spawn, collision filename and optional bgm in TRUE
 * world coordinates — written by the decomp repo's exporters, read once
 * at boot. No manifest = the office defaults (historical behavior).
 *
 * Debug instrumentation (port-side): EM_CAPTURE=<path.bmp> requests a BMP
 * capture at gameplay frame 60 (override with EM_CAPTURE_FRAME=<n>) and
 * quits one frame later, preserving the pre-architecture shell's headless
 * regression behavior bit-for-bit at the default frame.
 * EM_MOVE_TEST=1 runs a deterministic movement self-test: a scripted key
 * sequence (60 frames forward, 30 frames right) is injected through the
 * real em_input event API, then the final position/yaw is printed and the
 * loop quits (see move_test_script). Combine with EM_CAPTURE to grab a
 * mid-walk frame (the move test suppresses the capture path's early quit).
 * EM_WEAPON_TEST=1 runs the scripted firing-loop self-test (draw, semi
 * fire, the empty-mag auto-reload, full-auto cadence, manual top-up,
 * holster — see weapon_test_script / em_weapon.h).
 */
#include "game/em_game.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_gfx.h"
#include "em_input.h"
#include "em_math.h"
#include "em_model.h"
#include "game/em_bgm.h"
#include "game/em_collision.h"
#include "game/em_door.h"
#include "game/em_enemy.h"
#include "game/em_examine.h"
#include "game/em_frame.h"
#include "game/em_hud.h"
#include "game/em_pickup.h"
#include "game/em_sfx.h"
#include "game/em_task.h"
#include "game/em_truck.h"
#include "game/em_weapon.h"
#include "game/em_game_internal.h"
#include "game/em_camera.h"
#include "game/em_scene.h"
#include "game/em_props.h"

/* The gameplay state object declared in em_game_internal.h. */
EmGameState g;

/* SCENE MANIFEST — a plain-text scene.txt in the scene directory, written
 * there by the decomp repo's exporters (tools/export_level.py --spawn /
 * --bgm, tools/export_collision.py). Zero-dependency parser; "key value"
 * lines, '#' starts a comment, unknown keys are ignored:
 *
 *   spawn <x> <y> <z> <yaw>   player spawn, TRUE world coords + facing (rad)
 *   collision <file.emcl>     collision world filename inside the scene dir
 *   bgm <file.wav>            optional looping level-music cue WAV (scene
 *                             dir); the EM_BGM env override still wins
 *   door <file> <x> <y> <z> <yaw> <r> [goto <scene-dir> <sx> <sy> <sz> <syaw>]
 *                             one INTERACTIVE DOOR instance (file under
 *                             the scene dir, e.g. doors/door_m03.emdl;
 *                             r = use-scan trigger radius) — written by
 *                             export_props.py --doors, owned by em_door.c.
 *                             The OPTIONAL goto tail (export_level.py
 *                             --door-goto, the decoded dest+spawn
 *                             tables) makes the door's transition
 *                             commit SWITCH THE ACTIVE SCENE at full
 *                             black (em_game_scene_switch) and place
 *                             the player at the decoded arrival spawn —
 *                             see em_door.h. EM_DOOR_TEST ignores the
 *                             tail (it asserts same-scene re-place
 *                             geometry on the west door).
 *   enemy crawler <x> <y> <z> <yaw>
 *                             one WORM (the kind-0xD leech, engine
 *                             brain func_00153F10 — born attacking:
 *                             its BYTE-MATCHED init src/func_00154040.c
 *                             yaws the fresh actor straight at the
 *                             player mirror, `+0xC4 =
 *                             func_001B1240(self+0xB0, spec,
 *                             D_00810350, D_00810358)`, with no
 *                             distance gate anywhere (FINDINGS
 *                             "func_00153F10 / func_00154040 (worm) —
 *                             acquisition is unconditional").
 *                             PROVENANCE: func_00153F10 itself is only
 *                             recovered as an all-word stub — no
 *                             readable C — so every claim about the
 *                             brain's states rests on FINDINGS' .s read
 *                             plus func_00154040, not on recovered C.
 *                             The engine never places one, port
 *                             convenience; owned by em_enemy.c.
 *   enemy crate <x> <y> <z> <yaw> [bugs <n>] [variant <v>]
 *                             one DISGUISED CRATE (the placed crawler
 *                             func_001551B0 — em_enemy.h "CRATE
 *                             KIND"): idles as the office crate mesh,
 *                             bursts into gibs + its NEST-GROUP BUGS
 *                             (s68 rebinding — never a worm) on
 *                             DAMAGE or at the end of its alarm-
 *                             driven suicide run (s62 — no proximity
 *                             trigger exists). `bugs <n>` = the
 *                             decoded registry group size (office:
 *                             2-3; 0 = gore-only link -1 crates);
 *                             bare lines default to 2 (flagged
 *                             fallback, em_enemy.h). `variant <v>` =
 *                             the placement MODEL byte (default 6 =
 *                             every exported scene's crates): keys
 *                             the burst's husk family exactly like
 *                             the engine's rebind (6 -> the brown
 *                             wooden husk 0x22 set, else the
 *                             grey-cyan 0x29 set — func_001551B0
 *                             @0x156380).
 *   enemy bug <x> <y> <z> <yaw>
 *                             one BUG hatchling placed directly (the
 *                             s68 15-node insectoid, em_enemy.h "BUG
 *                             KIND" — flagged-minimal brain; stands
 *                             in for the registry's ordinary room-
 *                             spawn records).
 *   pickup <type> <x> <y> <z> <yaw> <uid> [<model.emdl>] [prop]
 *                             one placed PICKUP (export_level.py
 *                             --pickups, the decoded deferred-spawn
 *                             registry D_0024D820 + the placement
 *                             tables' kind-0xB display props; owned by
 *                             em_pickup.c — see em_pickup.h for the
 *                             full decode ledger). <uid> = the
 *                             (area<<8)|puid taken-bit key (0 = never
 *                             persists); a taken uid is silently NOT
 *                             placed (the engine's cond-1 spawn
 *                             suppression). `prop` = render-only.
 *   examine <x> <y> <z> <yaw> <dist> <dy> [gline 0xNN] [delay N]
 *           [cooldown N] [cam <ex> <ey> <ez>]
 *                             one EXAMINE object (export_level.py
 *                             --examine, the decoded overlay examine
 *                             behaviors; owned by em_examine.c — full
 *                             ledger in em_examine.h). CROSS inside
 *                             the use-scan window pauses input and
 *                             presents the radio line: `gline` = a
 *                             GLOBAL slot-0x16 bank line through
 *                             em_hud_radio; otherwise the chained
 *                             AREA-bank records below. `cam` = the
 *                             script's op00 camera-cue eye (cut +
 *                             hold; restore at sequence end).
 *   examinetext <dur> <gap> <text...>
 *                             one chained AREA-bank record of the LAST
 *                             examine line: text (literal "\n" =
 *                             newline) for <dur> frames, then <gap>
 *                             blank frames (the engine's empty odd
 *                             bank lines).
 *   camregion <x0> <z0> <x1> <z1> <ygate> <ex> <ey> <ez>
 *                             one FIXED-CAMERA trigger volume. The region
 *                             test itself is src/func_00194D10.c (NEARMISS
 *                             — logic authoritative): it runs
 *                             func_001B1EA0(0, player+0xA0,
 *                             &D_0024A5F0[idx], 4) — point-in-polygon over
 *                             the 0x40-byte quad record (4 corners, all 3
 *                             shipped records axis-aligned) — and then
 *                             `d < 4.0f` on |player.y - corner0.y|, so the
 *                             y gate is STRICTLY less than 4. The mode-0
 *                             director src/func_00195130.c (NEARMISS) is
 *                             only the caller that binds a record to an
 *                             area case; export_level.py --camregions.
 *                             Player inside the XZ rect with
 *                             |player.y - ygate| < 4: the
 *                             camera eye is PINNED at (ex, ey, ez) (target
 *                             keeps tracking the player), L1 and the idle
 *                             auto-orient are ignored, R1 aim still works
 *                             and its release snaps back INSTANTLY.
 *   enemy generator <x> <y> <z> <yaw> [kind <k>] [link <n>]
 *                             one GENERATOR pad (engine class 0x0D /
 *                             func_0015A2C0 — em_enemy.h "GENERATOR");
 *                             kind/link are the decoded placement
 *                             fields (bare lines default to kind 1 /
 *                             link 2 — port defaults; engine placements
 *                             always carry both). EM_ENEMY_TEST=4 arms
 *                             its own pad inside em_enemy.c and skips
 *                             these lines so the run stays
 *                             self-contained.
 *                             Other enemy kinds are reported and
 *                             skipped.
 *
 * A missing file or missing key leaves the office defaults in place, so
 * the default scene needs no manifest to keep its exact behavior. */
/* AREA-11 elevator platform helpers (defined with the elevator actor
 * below; forward-declared so the manifest parser can load/pose/free the
 * platform mesh, and scene_unload can free it with the scene). */






int em_game_scene_switch(const char *dir)
{
    EmGfx *gfx = em_frame_gfx();
    char   path[sizeof g.scene_dir];

    if (strchr(dir, '/')) {
        snprintf(path, sizeof path, "%s", dir);
    } else {
        /* sibling of the current scene dir (manifest goto tails carry
         * sibling names: "scene_office0" next to "assets/scene") */
        const char *slash = strrchr(g.scene_dir, '/');
        int plen = slash ? (int)(slash - g.scene_dir) + 1 : 0;
        snprintf(path, sizeof path, "%.*s%s", plen, g.scene_dir, dir);
    }

    /* Validate BEFORE tearing the running scene down. */
    char mf[sizeof path + 16];
    snprintf(mf, sizeof mf, "%s/scene.txt", path);
    FILE *probe = fopen(mf, "r");
    if (!probe) {
        printf("scene switch: %s has no scene.txt — staying in %s\n",
               path, g.scene_dir);
        return -1;
    }
    fclose(probe);

    scene_unload(gfx);
    snprintf(g.scene_dir, sizeof g.scene_dir, "%s", path);
    scene_manifest_load();      /* doors + enemies of the new scene */
    g.n_scene = scene_load(gfx, g.scene, SCENE_MAX);
    if (em_collision_load(&g.coll, g.coll_path) == 0) {
        printf("collision: %s — %u polys (%u verts), grid %s\n",
               g.coll_path, g.coll.poly_count, g.coll.vert_count,
               (g.coll.flags & 1) ? "decoded" : "absent (flat-floor)");
    } else {
        printf("no %s — movement uses the room-bbox clamp\n",
               g.coll_path);
    }
    printf("scene switch: %s — %d part(s), %d door(s), %d enem%s\n",
           g.scene_dir, g.n_scene, em_door_count(), em_enemy_count(),
           em_enemy_count() == 1 ? "y" : "ies");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Gameplay frame stages (func_001AE5E0)                               */
/* ------------------------------------------------------------------ */

/* func_001CB590(actor table, size, flags) — actor-context begin. The
 * engine points the live node-table base at the actor block about to be
 * updated; the port has exactly one actor (the player), so the context is
 * implicit. Kept as a stage so multi-actor support lands here. */
static void actor_context_begin(void) {}

/* func_001CB5A0 — actor-context end. */
static void actor_context_end(void) {}

/* Compose the player's world placement onto an evaluated anim palette:
 * every bone matrix M becomes T(pos) * R_y(yaw) * M — rotation about Y by
 * the facing yaw, then translation, applied AFTER the animation pose. At
 * yaw = 0 this reduces bit-exactly to the old static translation bake
 * (cos 0 = 1, sin 0 = 0), keeping EM_CAPTURE output stable. */
void palette_apply_placement(float *pal, uint32_t bone_count,
                                    const float pos[3], float yaw)
{
    const float c = cosf(yaw), s = sinf(yaw);
    for (uint32_t b = 0; b < bone_count; b++) {
        float *m = pal + b * 16;
        for (int col = 0; col < 4; col++) {
            float x = m[col * 4 + 0], z = m[col * 4 + 2];
            m[col * 4 + 0] =  c * x + s * z;
            m[col * 4 + 2] = -s * x + c * z;
        }
        m[12] += pos[0];
        m[13] += pos[1];
        m[14] += pos[2];
    }
}

/* WALL-class segment probe from an explicit start point. Outdoor
 * terrain (the snow scene's grid world) is near-flat but tilted, so a
 * low horizontal segment can clip the very ground the player stands
 * on — front-facing by a hair (n.z ~= -0.001) — and a naive block
 * turns into sideways drift along the terrain. The engine's result
 * block carries the surface class (SPR 0x700030CA) for exactly this
 * split: walkable ground (FLOOR/SLOPE) never blocks horizontal motion
 * (the floor query owns it; the engine's own filter is the
 * func_001764E0 surface-angle band) — walkable crossings are stepped
 * past and the probe re-runs for anything solid beyond them. When
 * `with_doors` is set the MOVABLE-HULL set joins in (em_door_probe —
 * the engine's mask-7 chest pass; a closed or moving door blocks, a
 * fully open one does not), nearest hit winning like the engine hub's
 * per-set segment clamping. Returns 1 with *hit staged on the first
 * wall-class hit, else 0. */
static int probe_wall_seg(const float start[3], const float target[3],
                          int with_doors, EmCollHit *hit)
{
    const unsigned mask = EM_COLL_SET_CELLS | EM_COLL_SET_GRID;
    float from[3] = { start[0], start[1], start[2] };
    int   sres    = 0;

    for (int i = 0; i < 8; i++) {
        if (!em_collision_move_probe(&g.coll, from, target, mask, hit))
            break;
        if (hit->surf_class != EM_SURF_FLOOR &&
            hit->surf_class != EM_SURF_SLOPE) {
            sres = 1;                       /* a real wall (or ceiling) */
            break;
        }
        /* Walkable ground — nudge the probe start just past the
         * crossing and look again for solid geometry beyond it. */
        float dx  = target[0] - hit->point[0];
        float dz  = target[2] - hit->point[2];
        float len = sqrtf(dx * dx + dz * dz);
        if (len <= 1e-3f) break;            /* crossing at the target */
        from[0] = hit->point[0] + dx / len * 1e-3f;
        from[2] = hit->point[2] + dz / len * 1e-3f;
    }
    if (!with_doors || !em_door_count())
        return sres;

    EmCollHit dhit;
    if (!em_door_probe(start, target, &dhit))
        return sres;
    if (sres) {
        float sd2 = 0.0f, dd2 = 0.0f;
        for (int k = 0; k < 3; k++) {
            float ds = hit->point[k] - start[k];
            float dd = dhit.point[k] - start[k];
            sd2 += ds * ds;
            dd2 += dd * dd;
        }
        if (sd2 <= dd2)
            return 1;          /* the static wall is nearer */
    }
    *hit = dhit;
    return 1;
}

/* func_001764E0 — the engine's RADIAL WALL PROBES (the real player
 * hitbox; see the PLAYER WALL RADIUS block above). Five directions
 * yaw + {0, +45, -45, +90, -90} deg (D_00248950), each probed twice —
 * ankle y+0.05 over the static sets and chest y+4.01 with the movable
 * hulls (doors) joined in — and every wall-class hit pushes the actor
 * back by the probe's overshoot (pos += hit - end, the spad
 * 0x700031C0 delta), exactly the engine's response. Each probe runs
 * from the ALREADY-corrected position, like the PS2 loop re-reading
 * actor +0xB0 per iteration. Runs every free/idle frame (the engine
 * fires it from both the idle top func_00161020 and the walk top
 * func_001612D0); scripted door transits skip it — the engine's
 * MOVE-TO crosses the sealed boundary planes deliberately. */
static void player_wall_probes(void)
{
    static const float kProbeAngle[5] = {
        0.0f, 0.7853982f, -0.7853982f, 1.5707964f, -1.5707964f
    };                                       /* D_00248950, radians */
    if (!g.coll.poly_count)
        return;
    for (int i = 0; i < 5; i++) {
        float ang = g.yaw + kProbeAngle[i];
        float dx  = sinf(ang) * PLAYER_WALL_RADIUS;
        float dz  = cosf(ang) * PLAYER_WALL_RADIUS;
        for (int pass = 0; pass < 2; pass++) {
            float lift = pass ? PROBE_CHEST_LIFT : PROBE_ANKLE_LIFT;
            float from[3] = { g.pos[0], g.pos[1] + lift, g.pos[2] };
            float end[3]  = { from[0] + dx, from[1], from[2] + dz };
            EmCollHit hit;
            if (probe_wall_seg(from, end, pass /* doors: chest only */,
                               &hit)) {
                static int trace = -1;
                if (trace < 0) trace = getenv("EM_PROBE_TRACE") != NULL;
                if (trace)
                    printf("probe: frame %d dir %d pass %d pos (%.2f, "
                           "%.2f) hit (%.2f, %.2f) push (%.3f, %.3f)\n",
                           g.frame_no, i, pass, g.pos[0], g.pos[2],
                           hit.point[0], hit.point[2],
                           hit.point[0] - end[0], hit.point[2] - end[2]);
                g.pos[0] += hit.point[0] - end[0];
                g.pos[2] += hit.point[2] - end[2];
            }
        }
    }
}

static void player_move_collide(float mx, float mz)
{
    EmCollHit hit;

    /* Integrate the move, then let the radial probes correct it (the
     * engine's order: the walk top writes +0xB0/B8, func_001764E0
     * pushes back). With the 4.5-unit radius far above the per-frame
     * step (0.3 u at run) the probes also own anti-tunneling. */
    g.pos[0] += mx;
    g.pos[2] += mz;
    player_wall_probes();

    /* Floor: vertical segment query through the same worlds (the grid
     * world owns the walkable floor — FINDINGS "COLLISION WORLD"). The
     * same class split applies downward: a leaning wall face (e.g. the
     * snow scene's gate posts, n.y slightly > 0) front-faces the probe
     * from above, and accepting it ratchets the player up the wall while
     * sliding along it — step past non-walkable crossings instead. */
    float from[3] = { g.pos[0], g.pos[1] + FLOOR_PROBE_UP,    g.pos[2] };
    float down[3] = { g.pos[0], g.pos[1] - FLOOR_PROBE_DOWN,  g.pos[2] };
    for (int i = 0; i < 8; i++) {
        if (!em_collision_segment_query(&g.coll, from, down,
                                        EM_COLL_SET_CELLS |
                                        EM_COLL_SET_GRID, 0, &hit))
            break;
        if (hit.surf_class == EM_SURF_FLOOR ||
            hit.surf_class == EM_SURF_SLOPE) {
            g.pos[1] = hit.point[1];
            break;
        }
        if (hit.point[1] - 1e-3f <= down[1])
            break;
        from[1] = hit.point[1] - 1e-3f;
    }

    /* MOVING-SURFACE CARRY (the AREA-11 truck top — em_collision.h §11.4,
     * the PS2 truck convergence block 0x00825014). After the static floor
     * snap above, consume the per-frame moving-surface registry: if the
     * player footprint stands on a registered moving surface (the truck
     * top) within its Y band, ADD that surface's velocity to the player.
     * For the wedged truck this is vel 0 (no effect); for the FALLING truck
     * it is the accelerating downward drop — so a player lingering on top
     * is carried DOWN into the crevice (the fail), below the static floor
     * the snap resolved. The single moving-surface touch in the player
     * ground-solve (the truck registers in em_truck_update, run earlier
     * this frame). Dormant — returns 0 — whenever no actor registered. */
    em_collision_moving_carry(g.pos);

    /* STATIC BLOCKER PUSH-OUT (the AREA-11 closed GRATE — em_collision.h
     * §blocker, INVESTIGATION_area11_grate.md §5). After the static EMCL
     * wall solve (player_wall_probes) and the floor snap, eject the player
     * from any registered solid blocker AABB they have entered. The grate
     * registers its closed hull each frame ONLY while the area is NOT
     * powered (grate_update, run before actor_update), so once powered the
     * registry is empty and this is a no-op — movement away from the grate
     * is bit-for-bit unchanged (the probe returns 0 and touches nothing).
     * This is the SINGLE blocker touch in the player ground-solve, the same
     * minimal-addition shape as the moving-surface carry above. The wall
     * radius is the same (0,y,4.5) probe radius the EMCL solve uses. */
    em_collision_blocker_probe(g.pos, PLAYER_WALL_RADIUS);
}

/* player_turn_rate — func_00174AC0's banded rate select (rad/frame).
 * Picks the body-heading ease rate from whether we are turning in place
 * (current ramped speed == 0) vs. moving, then by the gait tier (in
 * place) or by |delta| crossed with the ramped speed loco_upt (moving).
 * See the BODY-HEADING TURN RATES block above for the table. */
static float player_turn_rate(int gait, float upt, float adelta)
{
    if (upt <= 0.0f) {                      /* TURN-IN-PLACE, by gait */
        if (gait == 2) return TURN_IP_GAIT2;
        if (gait == 1) return TURN_IP_GAIT1;
        return TURN_IP_GAIT03;              /* gait 0 or 3 */
    }
    if (adelta <= TURN_DELTA_BAND) {        /* MOVING, near band */
        if (upt <= 0.1f) return TURN_MV_NEAR_W;
        if (upt <= 0.3f) return TURN_MV_NEAR_J;
        return TURN_MV_NEAR_R;
    }
    if (upt <= 0.1f) return TURN_MV_FAR_W;  /* MOVING, far band */
    if (upt <= 0.3f) return TURN_MV_FAR_J;
    return TURN_MV_FAR_R;
}

/* player_turn_toward — func_001B12B0 turn-toward: ease g.yaw toward the
 * desired world heading by at most `rate` (rad/frame), snapping when
 * within one step (|delta| <= rate) so there is no overshoot / jitter.
 * Updates g.yaw in place and wraps it to [-pi, pi]. */
static void player_turn_toward(float desired, float rate)
{
    float diff = desired - g.yaw;
    while (diff >  EM_PI) diff -= 2.0f * EM_PI;
    while (diff < -EM_PI) diff += 2.0f * EM_PI;
    if (diff <= rate && diff >= -rate) {
        g.yaw = desired;                    /* SNAP — within one step */
    } else {
        g.yaw += (diff > 0.0f) ? rate : -rate;
    }
    while (g.yaw >  EM_PI) g.yaw -= 2.0f * EM_PI;
    while (g.yaw < -EM_PI) g.yaw += 2.0f * EM_PI;
}

/* Per-tier target speed +0x38/D_00248870 (u/tick), shared by the tier
 * ramp in player_move and the C1 cross-blend progress in the anim
 * dispatch (loco_clip_for_tier / the +0x208 progress calc). */
static const float kLocoTierSpeed[4] = { 0.0f, 0.1f, 0.3f, 0.8f };

/* player_move_cam_yaw — the engine's camera-relative move basis.
 *
 * FIX 1 (INVESTIGATION_movement_exact.md "DEEP A/B v2", live 2026-06-17):
 * the engine builds the desired heading as stickAngle + D_008106A0, where
 * D_008106A0 is the camera's HORIZONTAL VIEW yaw read from its ORIENTATION
 * BASIS — concretely the atan2 angle of the COMMITTED FORWARD vector
 * D_00810600 (FINDINGS "Global camera vector pool": "0x8106A0 atan2 angle
 * of forward"), written by the commit func_0018C0D0 right after it builds
 * forward = normalize(target - eye). It is NOT recomputed from the
 * eye/target POSITIONS each frame, and it barely rotates while moving
 * (live: ~0.004 rad/frame, ~constant over a whole run) because the
 * committed forward is the damped/smoothed camera basis — so the move
 * basis is STABLE during locomotion and the body eases onto a fixed
 * desired heading, straightening the path.
 *
 * The previous attempt returned atan2(tgt - eye) recomputed from the raw
 * actual eye/target positions. That is the EYE->TARGET geometric heading;
 * the live A/B read it at ~-2.31 rad while the engine's D_008106A0 read
 * ~+2.42 rad at the same pose. The two diverge because that expression
 * swings with the chasing eye and is recomputed from positions rather
 * than sourced from the maintained orientation.
 *
 * The faithful source is the committed forward ORIENTATION vector
 * g.cam.fwd (= engine D_00810600 = normalize(tgt - eye), with the commit's
 * degenerate "keep last forward" guard already baked in). Its XZ azimuth
 * atan2(fwd.x, fwd.z) IS the geometric quantity D_008106A0 is the atan2 of.
 * The live globals' -2.31 vs +2.42 gap is the engine-global-vs-port
 * convention offset (the engine stores D_008106A0 in its own basis and
 * pairs it with atan2(stickX,stickY); the port consumes the SAME geometric
 * forward azimuth in its atan2(x,z) basis, paired with its (fx*-sy - fz*sx)
 * stick rotation below) — feeding the engine's raw +2.42 into the port's
 * atan2(x,z) pipeline would rotate movement 90 deg / mirror it. Sourcing
 * the committed-forward azimuth keeps forward-press -> walk along the
 * camera forward AND gives the engine's stable, non-swinging basis.
 *
 * g.cam.fwd is last frame's committed forward (actor_update -> player_move
 * runs BEFORE camera_update/camera_commit), exactly as func_00174AC0 reads
 * the prior-frame D_008106A0. Degenerate guard falls back to the
 * eye->target azimuth, then the orbit yaw, so the value stays
 * deterministic if the commit has not run yet. */
static float player_move_cam_yaw(void)
{
    float fx = g.cam.fwd[0];
    float fz = g.cam.fwd[2];
    if (fx * fx + fz * fz >= 1e-6f)
        return atan2f(fx, fz);       /* committed forward azimuth = D_008106A0 */
    /* commit has not produced a forward yet: fall back to the eye->target
     * azimuth (same geometric quantity), then the orbit yaw. */
    float dx = g.cam.tgt[0] - g.cam.eye[0];
    float dz = g.cam.tgt[2] - g.cam.eye[2];
    if (dx * dx + dz * dz < 1e-6f)
        return g.cam.yaw;
    return atan2f(dx, dz);
}

/* Player movement (the port's first slice of the actor spine's physics
 * side): left stick = camera-relative DESIRED heading on the XZ plane;
 * the body heading g.yaw EASES toward it at the engine's banded turn
 * rate (func_00174AC0 / func_001B12B0) and velocity is emitted ALONG
 * g.yaw — so the path curves into the move direction (the body lags the
 * stick) instead of sliding off along the raw stick instantly. With a
 * collision world loaded, movement goes through the engine's move probe
 * (walls stop/slide, the floor query sets the height); without one, the
 * old room-bbox clamp keeps the repo runnable standalone. */
static void player_move(void)
{
    g.gait = 0;          /* re-quantized below; scripted paths leave 0 */

    /* DOOR TRANSIT (the engine's gameplay-frame selector 3, spad
     * 0x70003B8D, armed by the use scan): a scripted MOVE-TO carries
     * the player to the door's far-side point with yaw snapped to the
     * door normal (func_001BBE40 -> func_00182F90). Runs collision-free
     * — the doorways are statically sealed by the grid room-boundary
     * planes, and this scripted move is exactly how the engine crosses
     * them. Stick input is ignored while it runs (the selector-3 frame
     * variant does not run the free-move spine). */
    {
        float tt[3], tyaw;
        if (em_door_transit_active(tt, &tyaw)) {
            float dx   = tt[0] - g.pos[0];
            float dz   = tt[2] - g.pos[2];
            float len  = sqrtf(dx * dx + dz * dz);
            float step = WALK_SPEED * FRAME_DT;
            g.move_speed = WALK_SPEED;     /* drive the walk clip */
            g.loco_tier  = 1;              /* scripted walk = tier-1 clip */
            g.loco_upt   = 0.0f;           /* free-move ramp re-arms */
            g.yaw        = tyaw;
            if (len <= step || len < 1e-6f) {
                g.pos[0] = tt[0];
                g.pos[2] = tt[2];
            } else {
                g.pos[0] += dx / len * step;
                g.pos[2] += dz / len * step;
            }
            return;
        }
    }

    /* ARRIVAL WALK-OUT (engine player state 5/1, func_00183250 —
     * em_door.h step 4).
     * PROVENANCE (audit): func_00183250 is byte-matched but exists in
     * the decomp ONLY as an asm-void .word body (src/func_00183250.c) —
     * there is no recovered C, so the frame counts and speeds below
     * cannot be re-checked against it. Treat them as OBSERVED /
     * port stand-in until that function gets a readable decompilation,
     * not as source-derived constants.
     * After the re-place the
     * player UNINTERRUPTIBLY walks out through the door along the exit
     * yaw — 50 frames of clip-in-place (mostly under the fade-in), 30
     * frames at the locIdx-2 speed (0.3 u/tick), 30 frames decaying to
     * a stop (~12.8 u total). The stick is never read (state 5 has no
     * free-move spine); em_door owns the phases, this consumes the
     * per-frame command. Collision-free like the transit MOVE-TO (the
     * engine runs its own mover in the destination area's geometry). */
    {
        float wyaw, wspeed;
        if (em_door_walkout_active(&wyaw, &wspeed)) {
            g.yaw        = wyaw;
            /* Drive the locomotion clip at the engine's commanded tier:
             * the walk-out plays the locIdx-2 clip (family 0 -> id 2 =
             * JOG, 0.3 u/tick) even during the in-place phase. */
            g.move_speed = wspeed > 0.0f ? wspeed : GAIT_JOG_SPEED;
            g.loco_tier  = 2;
            g.loco_upt   = 0.0f;           /* free-move ramp re-arms */
            g.pos[0] += sinf(wyaw) * wspeed * FRAME_DT;
            g.pos[2] += cosf(wyaw) * wspeed * FRAME_DT;
            return;
        }
    }

    /* MOVEMENT LOCK (door transit — the decoded two-lock split,
     * em_door.h "THE TWO LOCKS"): kickoff -> walk-out end. Free
     * movement is ignored — the player walks the scripted MOVE-TO /
     * walk-out above or stands (at the staging point). The MENU lock
     * is separate (em_door_menu_locked, consumed by em_hud) and ends
     * earlier, at fade-in completion. */
    if (em_door_movement_locked()) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;          /* scripted mode exits locomotion:
                                    * re-entry re-arms the tier ramp */
        g.loco_upt   = 0.0f;
        return;
    }

    /* EXAMINE SEQUENCE LOCK (em_examine.h): the examine script's op07
     * sub2 enters scripted mode for the whole sequence — free locomotion
     * is suppressed (the player stands), but the sequence DOES drive the
     * body heading itself: the op04 FACE pre-roll pivots the player to the
     * scripted yaw at the standing turn rate via em_game_player_face_step
     * (which calls player_turn_toward on g.yaw) BEFORE the message. The
     * engine's op01 walk-to is duration-0 for the snow terminals (the
     * use-scan already places the player within dist), so no scripted
     * translation here — only the FACE turn. We must NOT zero/seed the
     * heading here: this lock only kills move_speed/tier/upt so the floor
     * solve and anim are stand-still, while the FACE phase owns g.yaw.
     * Same lock shape as the door transit above. */
    if (em_examine_input_locked()) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;
        g.loco_upt   = 0.0f;
        return;
    }

    /* SCRIPTED INTERACT / ELEVATOR RIDE LOCK (CORRECTED two-terminal
     * flow, INVESTIGATION_area11_elevator.md). Two cases share one
     * stand-still lock — the engine's "scripted-anim-owns-player" state
     * (player+0x2F3 = 3) and the powered descent both suppress free
     * movement so the script owns the player:
     *   - interact_active: a one-shot scripted clip is playing on the
     *     player (the OUTSIDE battery-insert clip 0x14, the INTERNAL
     *     lever-throw clip 0x47). Input/movement is suppressed for the
     *     clip's duration; control returns when it ends.
     *   - elev_state == 1: the platform is descending — the player
     *     stands on it and is carried DOWN by the descent's direct Y
     *     drive (elevator_tick, after actor_update), not by free
     *     movement; standing here keeps the floor-snap above from
     *     fighting that write to g.pos[1].
     * em_game_player_interact_busy() reports both as busy so the examine
     * logic does not double-trigger. FLAGGED: the original also gates
     * input via a control-mode write — modeled here as the stand-still
     * lock (no decoded control-mode value to mirror; the live decode
     * confirmed D_008101E4 stays 0 — the lock IS the scripted-anim
     * state, not a control-mode flag). elev_pending (the descent armed,
     * waiting on the lever clip) is also locked so the one frame between
     * the lever clip ending and the ride beginning (both resolved later
     * this same frame in elevator_tick) does not leak free movement —
     * exactly em_game_player_interact_busy()'s condition. */
    if (em_game_player_interact_busy()) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;
        g.loco_upt   = 0.0f;
        return;
    }

    /* R2-HELD ARMED STANCE 0x1E (decoded 2026-06-11: the action machine
     * func_001607D0 dispatches HELD R2 -> player mode 0x1E, action code
     * 0x32 — the engine's SECOND aim stance, sharing the mode-1 aim
     * camera through player states 0x2A/0x29; its laser is the
     * DOT-only drawer func_001854E0 and it has no fire-counter recoil
     * — both stay with em_weapon, noted there as pending). em_game
     * runs its planted pose + steer + camera side: the held aim pose
     * through its own anim mailbox.
     *
     * FLAGGED DIVERGENCE (audit 2026-07-31) — STANCE PRIORITY IS
     * INVERTED HERE. The port gives R1 priority (the `want` gate below
     * refuses while em_weapon_is_aiming()). The recovered dispatcher
     * src/func_001607D0.c (NEARMISS — logic authoritative) gives R2
     * priority, in three places: from stance 0 it tests the R2 config
     * mask (spad 0x70003B7E) BEFORE the R1 mask (0x70003B7C); stance
     * 0x31 (R1) switches straight to 0x1E/0x32 the moment R2 is held;
     * and stance 0x32 (R2) only falls back to 0x1D/0x31 once R2 is
     * RELEASED and R1 is still held. Correcting this means suppressing
     * R1's aim while R2 is held, which lives in em_weapon.c — out of
     * this file's scope. Recorded, not fixed. */
    {
        const EmFrameInput *rin = em_frame_input();
        /* AUDIT CORRECTION (round 3 follow-up): R2 OUTRANKS R1. The
         * !em_weapon_is_aiming() term used to sit here, which gave R1
         * priority — the exact inversion of func_001607D0, whose stance
         * dispatcher tests the R2 mask BEFORE the R1 mask and whose case
         * 0x31 (R1 stance) switches to 0x1E/0x32 the moment R2 is held.
         * R1 is now suppressed while R2 is held, weapon-side, so this gate
         * no longer has to defer to it. Melee still wins over both — the
         * engine's melee states are a separate family this dispatcher is
         * not reached from. */
        int want = (rin->held & EM_PAD_R2) && !em_weapon_is_melee();
        if (want && !g.r2_aim)
            em_game_anim_hold(0x112, 1.0f);
        else if (!want && g.r2_aim && em_game_anim_active() == 0x112)
            em_game_anim_cancel();
        g.r2_aim = want;
    }

    /* PLANTED AIMING + MANUAL AIM STEER (func_0017ABA0 — the decoded
     * constants block above; retires the old AIM_TURN_SPEED turn-in-
     * place stand-in). The armed stance holds position (engine: the
     * armed modes 0x1D..0x20 replace the locomotion modes outright —
     * no aim-walk clips, zero footstep frames); the stick (and the
     * port's d-pad merge — arrows fold into the same axes, full
     * deflection) steers the aim BLENDS: pitch INVERTED-Y, yaw panning
     * the +-60 deg pose ladder first and turning the body only past
     * the blend limit. */
    {
        int aim_now = em_weapon_is_aiming() || g.r2_aim;
        if (aim_now && !g.aim_was) {
            /* STANCE ENTRY (func_0016F600 family): the aim blends reset
             * to center and the entry position is saved (D_70003040 —
             * the R2 state-0x2A camera target base), and the camera
             * arms its mode-1 entry phase. */
            g.aim_pitch = 0.5f;
            g.aim_yawb  = 0.5f;
            memcpy(g.cam.aim_entry, g.pos, sizeof g.cam.aim_entry);
            g.cam.aim_phase = 1;
        } else if (!aim_now && g.aim_was) {
            /* RELEASE (player states 0xC/0x29 -> camera mode 2): the
             * port re-seeds the chase yaw from the actual eye->player
             * heading (the engine's .L001935EC reset) and lets the
             * mode-0 chase blend back. */
            g.cam.aim_phase = 0;
            g.cam.yaw = atan2f(g.pos[0] - g.cam.eye[0],
                               g.pos[2] - g.cam.eye[2]);
        }
        g.aim_was = aim_now;
    }
    if (em_weapon_is_aiming() || g.r2_aim) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;          /* armed modes replace locomotion */
        g.loco_upt   = 0.0f;
        const EmFrameInput *ain = em_frame_input();
        int r1fam = !g.r2_aim || em_weapon_is_aiming(); /* stance 0x31 */

        /* d-pad merge (PORT, user-attested d-pad aim): arrows act as a
         * full-deflection axis when the stick is centered. */
        int rawx = ain->lx, rawy = ain->ly;
        if (rawx == 0x80 && (ain->held & EM_PAD_LEFT))  rawx = 0x00;
        if (rawx == 0x80 && (ain->held & EM_PAD_RIGHT)) rawx = 0xFF;
        if (rawx == 0x80 && (ain->held & EM_PAD_UP))    rawy = 0x00;
        if (rawy == 0x80 && (ain->held & EM_PAD_DOWN))  rawy = 0xFF;

        /* func_001B5DC0 deflection bands + the per-stance rate rows.
         * Both rows read literally out of src/func_0017ABA0.c
         * (NEARMISS — logic authoritative): the 0x31/0x34 arm is
         * {0, 0.0025f, 0.005f, 0.015f} with f20 = 1.0f, the else arm
         * (the R2 family 0x32/0x35) is {0, 0.0016666666f, 0.005f,
         * 0.01f} with f20 = 1.5f.
         * CORRECTED (audit 2026-07-31): kRateR2[3] was 0.015f — a
         * transcription of the R1 row's top band. The recovered C
         * reads `rate[3] = 0.01f` in that arm, so the port panned the
         * R2 aim 50% too fast at full stick deflection. */
        static const float kRateR1[4] = { 0.0f, 0.0025f, 0.005f, 0.015f };
        static const float kRateR2[4] = { 0.0f, 0.0016666666f, 0.005f,
                                          0.01f };
        const float *rate = r1fam ? kRateR1 : kRateR2;
        float body_mul = r1fam ? 1.0f : 1.5f;   /* f20 */
        int bx = abs(rawx - 0x80), by = abs(rawy - 0x80);
        int bandx = bx < AIM_BAND_1 ? 0 : bx < AIM_BAND_2 ? 1
                  : bx < AIM_BAND_3 ? 2 : 3;
        int bandy = by < AIM_BAND_1 ? 0 : by < AIM_BAND_2 ? 1
                  : by < AIM_BAND_3 ? 2 : 3;

        /* YAW (+0x27C): rate scaled by 1/sin(pi*(0.5+0.6*(p-0.5))) —
         * exactly 1.0 at pitch center (the engine special-cases the
         * equality), faster pitched off level. Overflow past [0,1]
         * turns the body by the excess (screen-right = yaw decreasing
         * in the port basis, matching the engine's heading -=). */
        if (bandx) {
            float p = g.aim_pitch;
            float s = (p == 0.5f) ? 1.0f
                    : sinf(EM_PI * (0.5f + 0.6f * (p - 0.5f)));
            float step = rate[bandx] / s;
            if (rawx >= 0x80) {            /* stick RIGHT */
                g.aim_yawb -= step;        /* toward the right poses */
                if (g.aim_yawb < 0.0f) {
                    g.yaw -= -g.aim_yawb * body_mul;
                    g.aim_yawb = 0.0f;
                }
            } else {                       /* stick LEFT */
                g.aim_yawb += step;
                if (g.aim_yawb > 1.0f) {
                    g.yaw += (g.aim_yawb - 1.0f) * body_mul;
                    g.aim_yawb = 1.0f;
                }
            }
            while (g.yaw >  EM_PI) g.yaw -= 2.0f * EM_PI;
            while (g.yaw < -EM_PI) g.yaw += 2.0f * EM_PI;
        }

        /* PITCH (+0x278): INVERTED Y — stick DOWN (raw >= 0x80) raises
         * the blend = aim UP; stick UP aims DOWN ("W = down"). The R1
         * family speeds up 1.5x outside [0.3, 0.7]. */
        if (bandy) {
            float mult = (r1fam && (g.aim_pitch <= 0.3f ||
                                    g.aim_pitch >= 0.7f)) ? 1.5f : 1.0f;
            float step = rate[bandy] * mult * 0.5f;
            if (rawy >= 0x80) {            /* stick DOWN -> aim UP */
                g.aim_pitch += step;
                if (g.aim_pitch > AIM_PITCH_MAX_R1)
                    g.aim_pitch = AIM_PITCH_MAX_R1;
            } else {                       /* stick UP -> aim DOWN */
                g.aim_pitch -= step;
                if (g.aim_pitch < 0.0f) g.aim_pitch = 0.0f;
            }
        }

        /* LOCK STEER (func_0017AF70 — em_weapon.h "TARGET LOCK"):
         * with the stick idle and a target in lock slot 0, em_weapon
         * creeps the blends toward the lock at <= 0.02/frame. The
         * stick-idle gate is the engine's manual-input lock drop (the
         * 0x1D stance clears D_008106E0 whenever func_0017ABA0 flags
         * manual steering, +0x302) — the player's hand always wins. */
        if (!bandx && !bandy) {
            float lp, ly;
            if (em_weapon_lock_steer(g.pos, g.yaw, g.aim_pitch,
                                     g.aim_yawb, &lp, &ly)) {
                g.aim_pitch = lp;
                g.aim_yawb  = ly;
            }
        }
        return;
    }

    /* KNIFE / MELEE plant: the engine's melee modes 0x21/0x22 replace
     * the locomotion modes outright (em_weapon.h "KNIFE / MELEE") —
     * the player stands for the swing + recover. The heavy's in-swing
     * yaw steer (func_00173DD0, D_002486F0 rates) is untranslated
     * (flagged in em_weapon.h), so no turn-in-place here either. */
    if (em_weapon_is_melee()) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;          /* melee modes replace locomotion */
        g.loco_upt   = 0.0f;
        return;
    }

    /* ANALOG GAIT — the engine's stick quantizer func_001B5CC0 on the
     * RAW 0x80-centered bytes: r = sqrt((x-128)^2 + (y-128)^2) through
     * rings 48/88/122 -> gait byte (pad +0x17 -> player +0x23F). */
    const EmFrameInput *in = em_frame_input();
    float rdx = (float)in->lx - 128.0f;
    float rdy = (float)in->ly - 128.0f;
    float r   = sqrtf(rdx * rdx + rdy * rdy);
    int gait  = r <= GAIT_RING_1 ? 0
              : r <= GAIT_RING_2 ? 1
              : r <= GAIT_RING_3 ? 2 : 3;
    g.gait       = gait;
    g.move_speed = 0.0f;

    /* THE TIER RAMP (func_0017BC40 — the 2026-06-11 re-decode; the
     * engine's +0x38/+0x25C pair): the target is the gait's
     * D_00248870 speed; the current speed accelerates/decelerates
     * toward it through the tier boundaries, promoting/demoting
     * loco_tier as each one is crossed. */
    const float *kTier = kLocoTierSpeed;   /* +0x38/D_00248870 (shared) */
    /* Per-tier decel table D_00248890 (§2.3), indexed by current tier:
     * tier 1 -> 0.05, tier 2 -> 0.025, tier 3 -> 0.0227273. (tier 0 is
     * unused — decel only runs when loco_upt > target, i.e. tier >= 1.) */
    static const float kDecel[4] = { 0.0f, 0.05f, 0.025f, 0.0227273f };
    if (gait == 0) {                  /* dead ring: idle / run-down */
        /* RUN-DOWN applies to TIER 3 ONLY (func_0017BC40 §2.6): on stick
         * release, tiers <= 2 (walk/jog) STOP INSTANTLY (phase 3,
         * +0x38 = 0 that frame); only tier 3 (run) bleeds down (phase 2)
         * at 0.03125 u/tick/frame, CASCADING tier-by-tier to each demote
         * floor (run -> jog -> walk -> stop), carrying ~9 u. */
        if (g.loco_upt > 0.0f && g.loco_tier >= 3) {
            /* RUN-DOWN (func_0017BC40 phase 2, C2): the carried run speed
             * bleeds 0.03125 u/tick per frame and CASCADES through every
             * tier, demoting loco_tier at each tier's lower-speed FLOOR,
             * not stopping at the first boundary. (The engine x2's the
             * decay to 0.0625 when carrying gear via actor +0x314 & 0x1F;
             * the port has NO gear-carry state, so the x2 is OMITTED —
             * flag.) Velocity is emitted along the held body heading
             * g.yaw (no stick = no desired heading, so g.yaw just holds). */
            g.loco_upt -= GAIT_RUNDOWN;   /* no gear-carry x2 in the port */
            /* Cascade the tier down past each floor we drop below. */
            while (g.loco_tier > 0 && g.loco_upt <= kTier[g.loco_tier - 1])
                g.loco_tier--;
            if (g.loco_upt <= 0.0f) {     /* bled to a full stop */
                g.loco_tier = 0;
                g.loco_upt  = 0.0f;
            } else {
                g.move_speed = g.loco_upt * 60.0f;
                float rx = sinf(g.yaw), rz = cosf(g.yaw);
                if (g.coll.poly_count)
                    player_move_collide(rx * g.move_speed * FRAME_DT,
                                        rz * g.move_speed * FRAME_DT);
                else {
                    g.pos[0] += rx * g.move_speed * FRAME_DT;
                    g.pos[2] += rz * g.move_speed * FRAME_DT;
                }
                return;
            }
        } else {
            /* INSTANT STOP for tiers <= 2 (and the already-stopped case):
             * walk/jog kill speed the frame the stick is released. */
            g.loco_tier  = 0;
            g.loco_upt   = 0.0f;
            g.move_speed = 0.0f;

            /* TURN-IN-PLACE (func_00174AC0, §2.5/2.7): a stick nudge inside
             * the r <= 48 gait-0 ring still EASES the body heading toward
             * atan2(stickX,stickY) + cameraYaw at the in-place rate
             * 22.5 deg/frame (TURN_IP_GAIT03), with NO translation. Only
             * runs when the stick is actually deflected (raw magnitude above
             * a tiny epsilon, within the ring) and the player is idle (this
             * is the not-run-down path). */
            if (r > 1e-4f) {
                /* Camera-relative DESIRED heading — the BYTE-FAITHFUL engine
                 * closed form (INVESTIGATION_movement_exact.md "FORWARD-
                 * DIRECTION RESOLUTION (definitive, static)", 2026-06-17):
                 *   D_008106A0 = atan2(-fwd.z, fwd.x)  [func_0018C0D0]
                 *   stickAngle = atan2(sx, -sy)        [func_00174AC0 +0x24C]
                 *   desired    = wrap( stickAngle + pi + D_008106A0 )
                 * and the identity atan2(x,z) == atan2(-z,x) + pi/2 (dev 1e-15)
                 * collapses pi + D_008106A0 to player_move_cam_yaw() + pi/2,
                 * giving the disasm-exact closed form below. Cardinals (with
                 * cam_yaw = atan2(fwd.x,fwd.z) = player_move_cam_yaw()):
                 *   FWD  (sx=0,  sy=-1): atan2(0,1)=0      -> cam_yaw + pi/2
                 *   RIGHT(sx=1,  sy=0) : atan2(1,0)=+pi/2  -> cam_yaw + pi
                 *   BACK (sx=0,  sy=+1): atan2(0,-1)=pi    -> cam_yaw - pi/2
                 *   LEFT (sx=-1, sy=0) : atan2(-1,0)=-pi/2 -> cam_yaw
                 * The prior "door-regression" form (cam_yaw + atan2(-sx,-sy))
                 * was X-mirrored AND missing the +pi/2 — 90 deg off on the
                 * axes, 180 deg off on the FWD-RIGHT diagonal. RESTORED. */
                float sx = rdx / r, sy = rdy / r;
                float desired = atan2f(sx, -sy) + player_move_cam_yaw()
                              + (float)EM_PI * 0.5f;
                while (desired >  EM_PI) desired -= 2.0f * EM_PI;
                while (desired < -EM_PI) desired += 2.0f * EM_PI;
                player_turn_toward(desired, TURN_IP_GAIT03);
            }
        }
        player_wall_probes();         /* the idle top probes too */
        return;
    }

    /* Stick direction (normalized) -> camera-relative DESIRED heading.
     * Camera basis on XZ: forward f points from the eye towards the
     * player, screen-right is f x up = (-fz, 0, fx). Stick up walks
     * away from the camera. The camera yaw is the engine's D_008106A0
     * horizontal VIEW yaw (player_move_cam_yaw — C3), NOT the pitched
     * eye->target heading cam+0x44, so the desired heading matches the
     * engine and the body does not over-curve. This is the DESIRED
     * heading (func_001B12B0's target); the BODY heading g.yaw is eased
     * toward it below and the velocity is emitted ALONG g.yaw — KEEP
     * this camera-relative derivation, change only what we move along. */
    /* Camera-relative DESIRED heading (func_001B12B0's target) — the
     * BYTE-FAITHFUL engine closed form (INVESTIGATION_movement_exact.md
     * "FORWARD-DIRECTION RESOLUTION (definitive, static)", 2026-06-17;
     * same derivation as the turn-in-place site above):
     *   desired = atan2(sx, -sy) + player_move_cam_yaw() + pi/2   (wrapped)
     * = wrap( stickAngle + pi + D_008106A0 ) with D_008106A0 =
     * atan2(-fwd.z, fwd.x) = player_move_cam_yaw() - pi/2. So forward-press
     * (sx=0,sy=-1) heads cam_yaw + pi/2 (90 deg off the camera-forward
     * azimuth — a UNIVERSAL engine offset, disasm-proven over 3e5 random
     * orientations, max dev 1e-15); RIGHT -> cam_yaw + pi; BACK ->
     * cam_yaw - pi/2; LEFT -> cam_yaw. The prior X-mirrored, +pi/2-missing
     * "door-regression" form is REPLACED — it was 90 deg off on the axes,
     * 180 deg off on the FWD-RIGHT diagonal. Velocity is still emitted
     * along the eased body heading g.yaw (func_00178B90, unchanged). */
    float sx = rdx / r, sy = rdy / r;
    float desired = atan2f(sx, -sy) + player_move_cam_yaw()
                  + (float)EM_PI * 0.5f;
    while (desired >  EM_PI) desired -= 2.0f * EM_PI;
    while (desired < -EM_PI) desired += 2.0f * EM_PI;

    /* STANDING-ENTRY TURN-IN-PLACE GATE + TIER-0 RAMP ENTRY
     * (FIX 2 + FIX 3, INVESTIGATION_movement_exact.md "DEEP A/B v2", live
     * 2026-06-17). The earlier attempt turned in place for exactly ONE
     * frame, then immediately set loco_tier=gait-1 / loco_upt=0.3 and
     * translated while still badly misaligned — the "curves out of the
     * gate" and "snaps with no build-up" the owner sees. The live A/B
     * shows the engine instead:
     *   Phase 1 (FIX 3): while the desired heading differs from the body
     *     by more than one in-place step (22.5 deg = TURN_IP_GAIT03), it
     *     rotates IN PLACE at exactly 22.5 deg/frame with speed +0x38 = 0,
     *     tier +0x25C = 0, NOT translating, for ceil(|dHeading|/22.5deg)
     *     frames. Only once within 22.5 deg of desired does it commit.
     *   Phase 2 (FIX 2): on commit it enters the ramp from TIER 0 / speed
     *     ~0 and walks the FULL table {0,0.1,0.3,0.8} (the static
     *     "entry gait-1/speed 0.3" decode was WRONG — live ramps from
     *     zero), promoting 0->1->2->3 via the existing tier-promote loop.
     */
    int entry = (g.loco_tier == 0 && g.loco_upt <= 0.0f);
    if (entry) {
        /* wrapped |desired - g.yaw| (the engine's turn delta). */
        float adiff = desired - g.yaw;
        while (adiff >  EM_PI) adiff -= 2.0f * EM_PI;
        while (adiff < -EM_PI) adiff += 2.0f * EM_PI;
        if (adiff < 0.0f) adiff = -adiff;

        if (adiff > TURN_IP_GAIT03) {
            /* PHASE 1 (FIX 3): not yet aligned — turn in place at
             * 22.5 deg/frame with speed pinned 0, do NOT set the entry
             * tier/speed and do NOT translate this frame. The player
             * remains a standing entry (loco_tier=0, loco_upt=0), so the
             * next frame re-enters here until the body is within one step
             * of desired. (Mirrors func_00174AC0 running the turn with
             * +0x38 still 0 across all the misaligned frames, not just
             * the first.) */
            player_turn_toward(desired, TURN_IP_GAIT03);
            g.move_speed = 0.0f;
            g.loco_tier  = 0;
            g.loco_upt   = 0.0f;
            player_wall_probes();     /* the idle top probes (no translate) */
            return;
        }

        /* PHASE 2 (FIX 2): aligned within one in-place step — close the
         * remaining gap (the in-place snap) and COMMIT to the ramp from
         * TIER 0 / speed 0. The tier ramp below walks the full table. */
        player_turn_toward(desired, TURN_IP_GAIT03);
        g.loco_tier = 0;
        g.loco_upt  = 0.0f;
    }

    /* THE TIER RAMP (func_0017BC40). On the entry frame this is the first
     * accel step from TIER 0 / speed 0 (FIX 2: the entry just committed at
     * loco_tier=0, loco_upt=0, so this adds the tier-0 accel 0.05 -> 0.05
     * and walks the full {0,0.1,0.3,0.8} table over the following frames);
     * on later frames it ramps from the carried speed (a mid-run gait
     * change ramps in place). */
    {
        float target = kTier[gait];
        if (g.loco_upt < target) {            /* sub 1: accelerate */
            float acc = g.loco_tier <= 0 ? GAIT_ACCEL_0
                      : g.loco_tier == 1 ? GAIT_ACCEL_1 : GAIT_ACCEL_2;
            g.loco_upt += acc;
            if (g.loco_tier < 3 && g.loco_upt >= kTier[g.loco_tier + 1]) {
                g.loco_upt = kTier[g.loco_tier + 1];
                g.loco_tier++;                /* +0x25C += 1 */
            }
            if (g.loco_upt > target) g.loco_upt = target;
        } else if (g.loco_upt > target) {     /* sub 2: decelerate */
            /* Per-tier decel D_00248890[loco_tier] (§2.3): tier 1 = 0.05,
             * tier 2 = 0.025, tier 3 = 0.0227273 — indexed by CURRENT tier
             * (previously tier 1 wrongly shared tier 2's 0.025). */
            float dec = kDecel[g.loco_tier];
            g.loco_upt -= dec;
            if (g.loco_tier > 0 && g.loco_upt <= kTier[g.loco_tier - 1]) {
                g.loco_upt = kTier[g.loco_tier - 1];
                g.loco_tier--;                /* +0x25C -= 1 */
            }
            if (g.loco_upt < target) g.loco_upt = target;
        } else {
            g.loco_tier = gait;               /* at tier: sustained */
        }
    }

    /* STEADY-STATE TURN (subsequent move frames): ease the body heading
     * toward the desired heading at the moving band (func_00174AC0 rate
     * select + func_001B12B0 turn-toward), THEN move along the eased
     * g.yaw. Turning first and translating along the lagged body heading
     * is what makes the motion CURVE into turns instead of sliding off the
     * raw stick. The rate is banded by gait / ramped speed / |delta|. The
     * entry frame already turned in place above (C4) — do not turn twice. */
    if (!entry) {
        float diff = desired - g.yaw;
        while (diff >  EM_PI) diff -= 2.0f * EM_PI;
        while (diff < -EM_PI) diff += 2.0f * EM_PI;
        float rate = player_turn_rate(gait, g.loco_upt, fabsf(diff));
        player_turn_toward(desired, rate);
    }

    /* The ramped speed drives this frame (sustained: gait 1 = WALK
     * 6 u/s, gait 2 = JOG 18 u/s, gait 3 = RUN 48 u/s). VELOCITY IS
     * EMITTED ALONG g.yaw (the eased body heading), NOT the raw stick
     * vector (mx, mz) — the body lags the stick, so the path curves. */
    g.move_speed = g.loco_upt * 60.0f;
    float vx = sinf(g.yaw), vz = cosf(g.yaw);

    if (g.coll.poly_count) {
        player_move_collide(vx * g.move_speed * FRAME_DT,
                            vz * g.move_speed * FRAME_DT);
    } else {
        g.pos[0] += vx * g.move_speed * FRAME_DT;
        g.pos[2] += vz * g.move_speed * FRAME_DT;
        if (g.pos[0] < kRoomMin[0]) g.pos[0] = kRoomMin[0];
        if (g.pos[0] > kRoomMax[0]) g.pos[0] = kRoomMax[0];
        if (g.pos[2] < kRoomMin[1]) g.pos[2] = kRoomMin[1];
        if (g.pos[2] > kRoomMax[1]) g.pos[2] = kRoomMax[1];
        g.pos[1] = 0.0f;  /* flat floor (no collision world loaded) */
    }

    /* TODO(stop-skid/pivot, FINDINGS "GROUND LOCOMOTION"): the engine's
     * mode-6 stop-skid anims (ids 4/5) and the pivot / 180-deg about-face
     * are OUT OF SCOPE here — they need clips 4/5 re-exported into
     * player.emdl before they can play, and the about-face is its own
     * mode. The facing ease above is the steady-turn model only. */
}

/* FOOTSTEPS — the native func_00187350 sound slice + its func_00182430
 * surface mapper (decomp FINDINGS.md "FOOTSTEP SURFACE TABLE", s37
 * static decode of the full call tree): when the locomotion clip's
 * cycle position crosses a trigger frame (frameA/frameB of its
 * D_00248C90 row), the engine submits TWO positional sounds
 * back-to-back, each with its OWN random variant draw:
 *
 *   surface_id = BLOCK(attr) + GAIT_SUB(gait) + rand5()
 *   gear_id    = EM_SFX_STEP_GEAR_BASE       + rand5()
 *
 * There is NO per-surface id table in the engine — the mapping is
 * compiled-in immediates inside func_00182430: a 17-id block per floor
 * material (footstep_block below) + the tier sub-base (a1==3 -> +0xA,
 * a1==2 -> +5, else +0; a1 = actor +0x25C, the ramped locomotion
 * tier — run +0xA, jog +5, walk +0) + rand5 =
 * func_00179B90 = (rand() & 7) with 5..7 folded to 0..2 (0..4, the low
 * three values twice as likely). This REPLACES the s29-era port guess
 * (fixed pairs 0x15/0x16 + 0x139/0x13A, both alternating L/R): the s29
 * "floor A vs floor B" capture was the SAME material at two tiers
 * (block 0x10 + 5 -> 0x15.. jog, + 0xA -> 0x1A.. run), and the
 * observed "pairs" were the rand bias toward 0..2 — neither layer
 * alternates. (The decal half of func_00187350 — step decals/FX via
 * func_00187EE0 — is still untranslated.) */

/* rand5 — func_00179B90. PORT NOTE: a private deterministic LCG (ANSI
 * minimal-standard constants, high bits) stands in for the EE libc
 * rand() the engine draws from, so the self-tests reproduce run to
 * run; the engine never seeds rand either. */
static unsigned footstep_rand5(void)
{
    static uint32_t s = 0x00187350u;   /* seed: the slice's own vaddr */
    s = s * 1103515245u + 12345u;
    unsigned v = (s >> 16) & 7u;
    return v >= 5u ? v - 5u : v;
}

/* Floor surface attr — the footing update func_00175900's attr copy:
 * after its own down-probe hits, the engine stores the collision
 * result record's surface-attr byte (+0x1A of the grid poly node) in
 * actor +0x23A every frame; a probe miss writes 0. The port probes at
 * step time instead (same result for a grounded player), reusing the
 * player height-resolve probe walk: step past non-walkable crossings,
 * take the first FLOOR/SLOPE hit's attr (EmCollHit.attr — the native
 * mirror of the poly node's +0x1A). No collision world -> attr 0,
 * which footstep_block maps to the default block 0x10 anyway. (The
 * movable-object override — standing on a crate forces attr 2/4 — and
 * the 0x5A/0x5B/0x5C first-contact one-shots are untranslated.) */
static uint8_t footstep_floor_attr(void)
{
    if (!g.coll.poly_count) return 0;
    float from[3] = { g.pos[0], g.pos[1] + FLOOR_PROBE_UP,   g.pos[2] };
    float down[3] = { g.pos[0], g.pos[1] - FLOOR_PROBE_DOWN, g.pos[2] };
    EmCollHit hit;
    for (int i = 0; i < 8; i++) {
        if (!em_collision_segment_query(&g.coll, from, down,
                                        EM_COLL_SET_CELLS |
                                        EM_COLL_SET_GRID, 0, &hit))
            return 0;                  /* probe miss: +0x23A = 0 */
        if (hit.surf_class == EM_SURF_FLOOR ||
            hit.surf_class == EM_SURF_SLOPE)
            return hit.attr;
        if (hit.point[1] - 1e-3f <= down[1])
            return 0;
        from[1] = hit.point[1] - 1e-3f;
    }
    return 0;
}

/* BLOCK(attr) — func_00182430's compiled-in material bases (FINDINGS
 * table; stride 0x11 = 17 ids per material: 3 tier sub-bases x 5
 * variants + 2 spare landing/scuff slots). */
static unsigned footstep_block(uint8_t attr)
{
    switch (attr) {
    case 1:    return 0x21u;
    case 2:    return 0x32u;
    case 3:    return 0x43u;
    case 4:    return 0x54u;
    case 5:    return 0x65u;
    case 6: case 7: return 0xA9u;
    case 8:    return 0x87u;
    case 0xD:  return 0xDCu;
    case 0xE:  return 0xEDu;
    case 0x5A: return 0x76u;  /* wet/puddle surface */
    case 0x5B: return 0xBAu;  /* water SHALLOW; DEEP (0xCB) needs the
                               * +0x23C depth state — untranslated */
    case 0x5C: return 0x98u;
    default:   return 0x10u;  /* attr 0 + any unmapped material — the
                               * office floor (grid attr 0, FINDINGS
                               * office cross-check) */
    }
}

/* One footstep at `tier` (the engine mapper's a1 = +0x25C; the locomotion paths
 * pass actor +0x25C, the melee impact gates a scripted 1..3).
 * EM_STEP_TRACE=1 prints each step's resolved attr/ids (debug). */
static void footstep_play(int tier)
{
    uint8_t  attr = footstep_floor_attr();
    unsigned sub  = tier == 3 ? 0xAu : tier == 2 ? 5u : 0u;
    unsigned surf = footstep_block(attr) + sub + footstep_rand5();
    unsigned gear = EM_SFX_STEP_GEAR_BASE + footstep_rand5();
    static int trace = -1;
    if (trace < 0) trace = getenv("EM_STEP_TRACE") != NULL;
    if (trace)
        printf("step: f%d tier %d attr 0x%02X -> surface 0x%03X gear 0x%03X\n",
               g.frame_no, tier, attr, surf, gear);
    /* engine: positional play_sound(actor, id, 0) radius 300 for both
     * layers (FINDINGS footstep decode) — source = the player, so the
     * gains come out center/full by the play_sound math itself */
    em_sfx_play_at(surf, g.pos, 300.0f);
    em_sfx_play_at(gear, g.pos, 300.0f);
}

/* Cyclic edge test: did the looping clip playhead cross `trig` going
 * prev -> cur (both in frames, cur may have wrapped past 0)? */
static int step_crossed(double prev, double cur, double trig)
{
    if (cur == prev) return 0;                    /* standing: frozen */
    if (cur > prev) return prev < trig && trig <= cur;
    return trig > prev || trig <= cur;            /* wrapped the loop */
}

/* AIM POSE LADDER blend (the dispatch half of the func_0017ABA0 steer
 * — anim_slot_index/anim_matrix_dispatch sampling the D_00248B70 sub-0
 * ladder 0x112..0x11A by the blends +0x278/+0x27C, two-buffer blend
 * func_00179CA0). The port evaluates the bilinear 3x3 pose grid at the
 * shared playhead and lerps the palettes (the same matrix-lerp the
 * port's idle/walk crossfades use — a documented stand-in for the
 * engine's bone-channel blend). Returns 1 when it produced the
 * palette (all needed ladder clips present), 0 to fall back to the
 * plain base-clip evaluation. */
static int aim_ladder_eval(double t)
{
    float p  = g.aim_pitch, yb = g.aim_yawb;
    int   up = p >= 0.5f;
    int   rt = yb < 0.5f;                  /* screen-right column */
    float wp = up ? (p - 0.5f) * 2.0f : (0.5f - p) * 2.0f;
    float wy = rt ? (0.5f - yb) * 2.0f : (yb - 0.5f) * 2.0f;

    unsigned id01 = rt ? 0x115 : 0x116;            /* level, yawed   */
    unsigned id10 = up ? 0x113 : 0x114;            /* pitched, ahead */
    unsigned id11 = up ? (rt ? 0x117 : 0x119)      /* corner          */
                       : (rt ? 0x118 : 0x11A);
    struct { unsigned id; float w; } s[4] = {
        { 0x112, (1.0f - wp) * (1.0f - wy) },
        { id01,  (1.0f - wp) * wy },
        { id10,  wp * (1.0f - wy) },
        { id11,  wp * wy },
    };
    uint32_t n = g.model.bone_count * 16;
    int      first = 1;
    for (int i = 0; i < 4; i++) {
        if (s[i].w <= 0.0f) continue;
        int ci = em_model_clip_index(&g.model, s[i].id);
        if (ci < 0) return 0;              /* old EMDL: no ladder bake */
        em_model_palette_at(&g.model, (uint32_t)ci, t, g.aim_palette);
        if (first) {
            for (uint32_t k = 0; k < n; k++)
                g.player_palette[k] = g.aim_palette[k] * s[i].w;
            first = 0;
        } else {
            for (uint32_t k = 0; k < n; k++)
                g.player_palette[k] += g.aim_palette[k] * s[i].w;
        }
    }
    return !first;
}

/* The analytic aim direction the camera (and the self-tests) consume —
 * the pose the ladder blend selects, expressed as a world ray (the
 * engine reads the equivalent from the posed hand matrix, gun+0xC0).
 * Pose pitches/yaws are the values MEASURED from the baked ladder
 * clips (constants block above). */
void aim_dir_get(float out[3])
{
    float p  = g.aim_pitch, yb = g.aim_yawb;
    float pit_deg = p >= 0.5f
        ? AIM_POSE_CTR_DEG + (p - 0.5f) * 2.0f *
              (AIM_POSE_UP_DEG - AIM_POSE_CTR_DEG)
        : AIM_POSE_CTR_DEG - (0.5f - p) * 2.0f *
              (AIM_POSE_DOWN_DEG + AIM_POSE_CTR_DEG);
    /* yaw blend: 0 = screen right = yaw DECREASING in the port basis */
    float yaw = g.yaw + (yb - 0.5f) * 2.0f *
                (AIM_POSE_YAW_DEG * EM_PI / 180.0f);
    float pit = pit_deg * EM_PI / 180.0f;
    out[0] = sinf(yaw) * cosf(pit);
    out[1] = sinf(pit);
    out[2] = cosf(yaw) * cosf(pit);
}

/* ------------------------------------------------------------------ */
/* PLAYER DAMAGE & DEATH machine (the PD_* constants block above)      */
/* ------------------------------------------------------------------ */

/* Is the player in a hit reaction / dying / at the game-over screen?
 * (the movement + input + menu lock; also the producer-side immunity
 * gate together with pd_iframes — engine: every producer requires the
 * event byte == 1, which holds from reaction start until the +0x20E
 * window expires after the flinch). */
static int player_damage_locked(void)
{
    return g.pd_state != 0 || g.go_state != 0;
}

/* func_0021C270 — apply pending INFECTION damage. */
static void player_apply_infection(void)
{
    if (g.pd_pend_inf == 0.0f) return;
    g.status.infection += g.pd_pend_inf;
    g.pd_pend_inf = 0.0f;
    g.pd_inf_hit  = 1;                     /* +0x1F1 = 1: voice select */
    if (g.status.infection >= 100.0f) {
        g.status.infection = 100.0f;
        if (g.status.health > PD_INFECTED_MAX)
            g.status.health = PD_INFECTED_MAX;
        if (!g.pd_infected) {
            g.pd_infected = 1;             /* +0x234 / 0x8104E4 latch */
            /* the display max swap to 60 — C14: the same +0x234 flag
             * drives the HUD's "/60" max (em_hud.h) */
            g.status.health_max = PD_INFECTED_MAX;
            em_sfx_play_at(PD_SFX_INFECTED, g.pos, 300.0f); /* engine
                                              * play_sound radius 300 */
        }
    }
}

/* func_0021C350 — apply pending HEALTH damage. */
static void player_apply_health(void)
{
    if (g.pd_pend_hp == 0.0f) return;
    g.status.health -= g.pd_pend_hp;
    g.pd_pend_hp = 0.0f;
    g.pd_inf_hit = 0;                      /* +0x1F1 = 0 */
    if (g.status.health <= PD_LOW_HEALTH)
        g.pd_low = 1;                      /* +0x235 |= 1 */
    if (g.status.health <= 0.0f) {
        g.status.health = 0.0f;
        /* event byte = 2 — the death branch runs in the processor */
    }
}

/* state 2 sub 0 phase 0 — FLINCH entry (func_0021D800): voice + the
 * real reaction clip (family by RNG bit, side by a second draw —
 * func_0021D1A0's side test is a hit-direction check, untranslated;
 * flagged as a second RNG bit). */
static void player_enter_flinch(void)
{
    int      armed = em_weapon_state() != EM_WPN_HOLSTERED;  /* +0x236 */
    unsigned fam   = footstep_rand5() & 1u;   /* func_00122BB8 & 1 */
    unsigned side  = footstep_rand5() & 1u;   /* func_0021D1A0 stand-in */
    unsigned clip;
    if (g.pd_infected)
        clip = PD_CLIP_FLINCH_INF;
    else if (armed)
        clip = fam ? PD_CLIP_FLINCH_ARM_B : PD_CLIP_FLINCH_ARM_A;
    else
        clip = fam ? (side ? PD_CLIP_FLINCH_B1 : PD_CLIP_FLINCH_B0)
                   : (side ? PD_CLIP_FLINCH_A1 : PD_CLIP_FLINCH_A0);
    em_sfx_play_at(g.pd_inf_hit ? PD_SFX_HURT_INF : PD_SFX_HURT,
                   g.pos, 300.0f);            /* player-attached, r=300 */
    if (!em_game_anim_request(clip, 1.0f))
        clip = 0;                  /* clip-less asset: timed fallback */
    g.pd_state = 2;
    g.pd_sub   = 0;
    g.pd_phase = clip ? 5 : 2;     /* 5 = wait commit; 2 = clip-less
                                    * timed fallback */
    g.pd_hold  = 0;
    g.pd_clip  = clip;
    /* rumble func_001B61C0(0, 0xC0, 5, 0) — no force-feedback backend */
}

/* state 2 sub 1/3 phase 0 — DEATH entry (func_0021E240 /
 * func_0021E830). The +0x1F0 = 0x40/0x3F write is only the category
 * marker — the sub handler requests the REAL clip itself. */
static void player_enter_death(void)
{
    int      armed = em_weapon_state() != EM_WPN_HOLSTERED;  /* +0x236 */
    unsigned clip  = g.pd_infected ? PD_CLIP_DEATH_INF
                   : armed         ? PD_CLIP_DEATH_ARM
                                   : PD_CLIP_DEATH;
    em_sfx_play_at(PD_SFX_DEATH_VOICE, g.pos, 300.0f); /* 0x146, r=300 */
    em_sfx_play_at(PD_SFX_DEATH_BODY,  g.pos, 300.0f); /* 0x151, r=300 */
    if (!em_game_anim_hold(clip, 1.0f))
        clip = 0;                  /* clip-less asset: straight to hold */
    g.pd_state    = 2;
    g.pd_sub      = g.pd_infected ? 3 : 1;
    g.pd_phase    = clip ? 5 : 2;  /* 5 = wait commit */
    g.pd_hold     = PD_CORPSE_HOLD;
    g.pd_clip     = clip;
    g.pd_cue_fall = 0;
    g.pd_cue_thud = 0;
    /* gore effect 0x80000051 (infected) — no effect system: flagged */
    printf("player death: clip 0x%X (%s)%s\n", clip,
           g.pd_infected ? "infected" : armed ? "armed" : "unarmed",
           clip ? "" : " — EMDL carries no death clip (timed fallback)");
}

/* func_0021C440 (generic tail) — the per-frame damage processor:
 * consume the pending amounts, then route to flinch or death. Called
 * once per gameplay frame after the producers ran. */
static void player_damage_process(void)
{
    if (g.pd_pend_hp == 0.0f && g.pd_pend_inf == 0.0f) return;
    /* invulnerable: hit-reacting/dead, or inside the post-hit window
     * (engine: producers require event byte == 1) — drop the damage */
    if (player_damage_locked() || g.pd_iframes > 0) {
        g.pd_pend_hp = g.pd_pend_inf = 0.0f;
        return;
    }
    player_apply_health();           /* func_0021C350 */
    player_apply_infection();        /* func_0021C270 */
    if (g.status.health <= 0.0f)
        player_enter_death();
    else
        player_enter_flinch();
}

/* s76 BUG SHAKE-OFF (the latch reaction — func_002208C0 / state-2
 * sub-state 0xD): a bug bite LATCHES the bug onto the player (em_enemy
 * sub 5); while any cling, the player DRAINS, and when free he SHAKES
 * them off (clip 54), which detaches them all. The engine's 0x35->0x36
 * ->0x35 wrap is reduced to the recognisable 0x36 shake. */
static void player_enter_struggle(void)
{
    em_game_anim_hold(PD_CLIP_CLING, 1.0f);   /* 0x2C cling-idle pose */
    em_sfx_play_at(PD_SFX_SHAKE, g.pos, 300.0f);
    g.pd_state    = 2;
    g.pd_sub      = 5;               /* STRUGGLE (mash CROSS)            */
    g.pd_phase    = 1;              /* skip the wait-commit gate         */
    g.pd_hold     = 0;
    g.pd_clip     = PD_CLIP_CLING;
    g.struggle_n  = 0;
    g.status.health -= PD_LATCH_HIT;          /* 10 HP on connect (live) */
    if (g.status.health < 0.0f) g.status.health = 0.0f;
}

/* LIVE-VERIFIED 2026-06-12 (PCSX2): a clinging bug is shaken off by
 * MASHING CROSS, not automatically; while it clings the player loses
 * HEALTH and (the real killer) gains INFECTION fast; winning the mash
 * race throws the bug off and KILLS it, losing it -> infected death.
 * Rates/threshold are flagged PORT constants (need a live read —
 * FINDINGS "BUG LATCH / SHAKE-OFF — LIVE-VERIFIED"). */
static void player_struggle_tick(void)
{
    int latched = em_enemy_latched_count();
    if (latched <= 0)
        return;
    /* DRAIN + INFECT while any bug clings (direct — the reaction locks
     * the normal damage pipeline). Infection maxing or health 0 = death. */
    if (g.go_state == 0 && g.pd_sub != 1 && g.pd_sub != 3) {
        g.status.infection += PD_LATCH_INFECT * (float)latched;
        if (g.status.infection > 100.0f) g.status.infection = 100.0f;
        g.status.health    -= PD_LATCH_DRAIN  * (float)latched;
        if (g.status.infection >= 100.0f && !g.pd_infected) {
            g.pd_infected       = 1;            /* infected latch (cap 60) */
            g.status.health_max = PD_INFECTED_MAX;
            em_sfx_play_at(PD_SFX_INFECTED, g.pos, 300.0f);
        }
        if (g.status.health <= 0.0f) {
            g.status.health = 0.0f;
            g.pd_state = 0;
            g.pd_phase = 0;
            em_enemy_shake_off();              /* the bugs go with you   */
            player_enter_death();
            return;
        }
    }
    /* enter the struggle the moment a bug first clings (engine: the
     * contact raises +0x0F=2 when the player is free / not i-framed). */
    if (g.pd_state == 0 && g.go_state == 0 && g.pd_iframes == 0)
        player_enter_struggle();
}

/* The state-2 per-frame tick (the port slice of func_0021D800 phase 1
 * / func_0021E240 / func_0021D2E0): watch the committed reaction clip,
 * fire the death-sequence sound cues, run the corpse hold, then the
 * fade-out into the game-over stand-in. Runs INSTEAD of player_move
 * (state 2 has no free-move spine). */
static void player_hurt_tick(void)
{
    if (g.pd_phase == 5) {
        /* wait for the scripted-anim COMMIT (1-frame latency — the
         * engine requests land one update after the write too) */
        if (em_game_anim_active() == g.pd_clip)
            g.pd_phase = 1;
        return;
    }
    if (g.pd_sub == 0) {
        /* FLINCH: wait for the one-shot clip to play out (sa auto-
         * clears at clip end), then arm the i-frames and exit to
         * state 1 (the engine exits to sub 7 recover — the port's
         * locomotion idle is that pose). */
        int over = g.pd_phase == 2
                 ? ++g.pd_hold >= 30      /* clip-less fallback: ~30 f */
                 : em_game_anim_active() != g.pd_clip;
        if (over) {
            g.pd_state   = 0;
            g.pd_phase   = 0;
            g.pd_hold    = 0;
            g.pd_iframes = PD_IFRAMES;       /* +0x20E = 0x3C */
        }
        return;
    }
    if (g.pd_sub == 5) {
        /* STRUGGLE: mash CROSS to advance the counter (live: each press
         * advances it; no decay). At the threshold the bugs are thrown
         * off and DIE (em_enemy_shake_off). The cling pose (0x2C) holds;
         * each press plays the struggle flourish (0x2E). */
        const EmFrameInput *in = em_frame_input();
        if (in && (in->pressed & EM_PAD_CROSS)) {
            g.struggle_n++;
            em_game_anim_request(PD_CLIP_STRUGGLE, 1.2f);   /* 0x2E */
        } else if (em_game_anim_active() == 0) {
            em_game_anim_hold(PD_CLIP_CLING, 1.0f);          /* re-hold 0x2C */
        }
        if (g.struggle_n >= PD_STRUGGLE_WIN) {
            em_enemy_shake_off();              /* thrown off -> the bugs DIE */
            em_sfx_play_at(PD_SFX_THROWOFF, g.pos, 300.0f);  /* 0x14D */
            em_game_anim_request(PD_CLIP_THROWOFF, 1.0f);    /* 0x24 */
            g.pd_sub   = 6;
            g.pd_phase = 5;
            g.pd_clip  = PD_CLIP_THROWOFF;
            g.pd_hold  = 0;
        } else if (em_enemy_latched_count() == 0) {
            g.pd_state   = 0;                  /* bugs gone (shot off) -> exit */
            g.pd_phase   = 0;
            g.pd_iframes = PD_IFRAMES;
        }
        return;
    }
    if (g.pd_sub == 6) {
        /* THROW-OFF recover: the 0x24 clip plays out, then exit + invuln. */
        int over = g.pd_phase == 2
                 ? ++g.pd_hold >= 30
                 : em_game_anim_active() != g.pd_clip;
        if (over) {
            g.pd_state   = 0;
            g.pd_phase   = 0;
            g.pd_hold    = 0;
            g.pd_iframes = PD_IFRAMES;
        }
        return;
    }
    /* DEATH (sub 1 normal / 3 infected) */
    if (g.pd_phase == 1) {
        /* clip playing: frames-remaining sound cues (func_0021E240
         * phase 1 reads the anim clock +0x3C the same way) */
        int total = em_game_anim_frames(g.pd_clip);
        int cur   = em_game_anim_frame();
        int rem   = (cur >= 0 && total > 0) ? total - 1 - cur : -1;
        if (!g.pd_cue_fall && rem >= 0 && rem <= PD_DEATH_CUE_FALL) {
            g.pd_cue_fall = 1;
            em_sfx_play_at(PD_SFX_DEATH_FALL, g.pos, 300.0f); /* 0x156 */
        }
        if (!g.pd_cue_thud && rem >= 0 && rem <= PD_DEATH_CUE_THUD) {
            g.pd_cue_thud = 1;
            em_sfx_play_at(g.pd_infected ? PD_SFX_DEATH_THUD_I
                                         : PD_SFX_DEATH_THUD,
                           g.pos, 300.0f);
            /* + the heavy ground rumble func_001B61C0(1,0xEE,0x3C,1) */
        }
        if (rem <= 0) {
            /* clip done (held on the last frame — the corpse pose):
             * func_0021D2E0 phase 0. Blood-pool effect 0x80000043
             * under the corpse: skipped (no decal system), flagged. */
            g.pd_phase = 2;
        }
        return;
    }
    if (g.pd_phase == 2) {
        /* corpse hold (+0x28 = 0x78), then the standard fade-out —
         * func_001AEDE0(4,0), the door-transit machine. */
        if (--g.pd_hold <= 0) {
            g.pd_phase = 3;
            em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
            g.go_state = 1;
        }
        return;
    }
    if (g.pd_phase == 3) {
        /* fading: at full black the engine parks the player machine
         * and func_001AE040's state-1 tail is believed to fire
         * (D_008106B9 latch && fade == 2) -> the GAME-OVER WAIT.
         * PROVENANCE: func_001AE040 is NOT recovered — there is no
         * src/func_001AE040.c in the decomp at all — so this hand-off
         * is OBSERVED/inferred from the FINDINGS call-graph note
         * ("func_001AD4D0 = j func_001AE040 ... in-game frame machine,
         * jr-table 0x0026DD30"), not read out of recovered C. What IS
         * byte-matched is the WAIT it hands to: src/func_001AD4E0.c
         * (see game_over_tick). Port: enter GO_SCREEN — module-0x27 screen
         * stand-in + FADE-IN + the 240 hold; from the next frame the
         * world is FROZEN (the gameplay_frame gate) and game_over_tick
         * owns the flow, modeling the engine's task replacement. */
        if (em_frame_fade_level() >= 1.0f) {
            g.pd_phase  = 4;
            g.go_state  = GO_SCREEN;
            g.go_frames = 0;
            g.go_hold   = GO_HOLD_FRAMES;          /* task+0x18 = 0xF0 */
            em_frame_fade_start(-1, EM_FADE_SPEED_DOOR); /* func_001AEE10 */
        }
    }
    /* phase 4: parked dead under the game-over/continue screens */
}

/* Passive per-frame vitals (func_0015D100 infected arm + the spine's
 * kill plane + the +0x20E i-frame countdown).
 * RE-VERIFIED against src/func_0015D100.c (BYTE-MATCHED — authoritative):
 * both arms are gated on func_0021BB00(self) == 0 (entity not busy),
 * and the flag byte +0x234 (the INFECTED latch) picks the arm:
 *   +0x234 != 0  INFECTED: counter +0x2FC++ every frame; at >= 0xF0
 *                (240) reset it and, if health <= 2.0f, set event byte
 *                = 2, pending +0x224 = 2.0f and damage type +0xF = 0x63
 *                (the infected-drain death) WITHOUT subtracting;
 *                otherwise health -= 2.0f, effect 0x80000063, then the
 *                low-health latch +0x235 |= 1 at health <= 35.0f.
 *                (`health <= 2` then death is arithmetically identical
 *                to the port's `health -= 2; death if <= 0`.)
 *   +0x234 == 0  HAZARD-ROOM arm — UNTRANSLATED, but now pinned:
 *                gated on func_001B0070() & 4, D_008106C8 & 0x60 and
 *                D_00810C7E == 0; counter +0x300 at >= 0x168 (360, NOT
 *                240) drains health by 1.0f (not 2.0f), and at
 *                health <= 1.0f forces event 2 + pending 1.0f.
 * The func_0015D000 heartbeat rumble is untranslated (PD block doc).
 * RE-CONFIRMED (audit 2026-07-31): every constant above reads out of
 * src/func_0015D100.c literally — the 0xF0 / 0x168 periods, the 2.0f /
 * 1.0f drains, the `<= 2.0f` / `<= 1.0f` death gates, the +0xF = 0x63
 * type byte and the `<= 35.0f` +0x235 latch. */
static void player_vitals_tick(void)
{
    if (g.pd_iframes > 0) g.pd_iframes--;
    if (player_damage_locked()) return;
    /* INFECTED passive drain: health -= 2.0 every 240 frames (the
     * green effect 0x80000063 per tick is skipped — no effect system,
     * flagged). Reaching 0 = the engine's event-2/type-0x63 death ->
     * the infected death sequence. */
    if (g.pd_infected && ++g.pd_drain_t >= PD_DRAIN_PERIOD) {
        g.pd_drain_t = 0;
        g.status.health -= PD_DRAIN_AMOUNT;
        if (g.status.health <= PD_LOW_HEALTH) g.pd_low = 1;
        if (g.status.health <= 0.0f) {
            g.status.health = 0.0f;
            player_enter_death();
            return;
        }
    }
    /* KILL PLANE (spine death check): Y < -200 -> engine state 6
     * (func_0015D460): zero health, fade, park — no anim, no sound. */
    if (g.pos[1] < PD_KILL_PLANE) {
        g.status.health = 0.0f;
        g.pd_state = 2;
        g.pd_sub   = 1;
        g.pd_phase = 3;            /* straight to the fade wait */
        g.pd_clip  = 0;
        em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
        g.go_state = 1;
    }
}

/* GAME-OVER / CONTINUE machine — the decoded engine chain (PD block
 * doc: func_001AD4E0 wait subs 2..4 + func_001AC070/func_001AC480
 * continue states), folded into one port state machine. RE-VERIFIED:
 * src/func_001AD4E0.c (BYTE-MATCHED) sets the 0xF0 = 240 hold at sub 0
 * and leaves sub 3 on `D_0028A9A0==0 && (counter==0 || D_00810E74&0x40)`
 * = fade idle && (expiry || CROSS); src/func_001AC480.c case 2 carries
 * the 0x4B0 = 1200 prompt timer (decremented only on input-free frames,
 * re-armed on any held frame), confirm mask 0x840, DOWN 0x4000 /
 * UP 0x1000 with the cursor clamped to 0..2, and sounds 0x5DD/0x5DE/
 * 0x5DF + move blip 5, and its sub-0 cursor INIT is
 * `GS[0xF] = (D_00275BDC == 0) ? 0 : 1` — the from-death flag
 * D_00275BDC is set to 1 by src/func_001ADF00.c, so GO_CURSOR_DEATH = 1
 * is source-derived. src/func_001AC070.c (NEARMISS — 97.95%, logic
 * authoritative but NOT byte-matched; the earlier "BYTE-MATCHED" note
 * here was wrong) is the outer flow whose state 4 does
 * func_001AB790(func_001ACEC0) and returns without the
 * func_001D2830(3,1) tail. Its state-2 CONFIRM dispatch reads that same
 * GS[0xF]: 0 -> state 4 + D_00275BE0 = 0 (CONTINUE), 1 -> func_00225A00()
 * + state 5 + D_00275BE0 = 1 (LOAD), 2 -> state 6 + GS[0xC] = 0
 * (sub-screen). RE-CONFIRMED line by line (audit 2026-07-31) against
 * src/func_001AC480.c, src/func_001AD4E0.c and src/func_001AC070.c;
 * the D_00275BDC = 1 write really is in src/func_001ADF00.c, and the
 * masks check out against the s37 BYTE-SWAPPED pad map (0x40 = CROSS,
 * 0x800 = START, so 0x840 = START|CROSS; 0x1000 = d-pad UP,
 * 0x4000 = d-pad DOWN). One knowingly-unmirrored detail: the engine
 * reads the prompt counter BEFORE decrementing and expires on the
 * pre-decrement 0, i.e. 1201 frames to the port's 1200. Left alone —
 * one frame in a 20 s idle timeout. Runs every
 * frame from GO_SCREEN on (the frozen-world gate in gameplay_frame
 * calls it — the engine's game task is REPLACED here, so the world
 * does not simulate). Presentation: em_hud_game_over /
 * em_hud_continue, drawn UNDER the fade in frame_close_out. */
static void game_over_tick(void)
{
    const EmFrameInput *in = em_frame_input();
    if (g.go_state < GO_SCREEN) return;
    g.go_frames++;
    switch (g.go_state) {
    case GO_SCREEN:
        /* engine wait sub 3: the 240 counts down THROUGH the fade-in;
         * skip/expiry only fire once the fade machine is idle. */
        if (g.go_hold > 0) g.go_hold--;
        if (em_frame_fade_level() > 0.0f) break;     /* fade busy */
        if (g.go_hold == 0 || (in->pressed & EM_PAD_CROSS)) {
            /* CROSS skip (D_00810E74 & 0x40) or timer expiry ->
             * fade-out (func_001AEDE0(4,0)) */
            em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
            g.go_state  = GO_SCREEN_OUT;
            g.go_frames = 0;
        }
        break;
    case GO_SCREEN_OUT:
        /* engine wait sub 4: at hold-black stop the cue + arm the
         * CONTINUE machine (func_001ADF00 task replacement). */
        if (em_frame_fade_level() >= 1.0f) {
            g.go_state  = GO_PROMPT;
            g.go_frames = 0;
            g.go_timer  = GO_PROMPT_FRAMES;        /* task+0x16       */
            g.go_cursor = GO_CURSOR_DEATH;         /* from-death = 1  */
            em_frame_fade_start(-1, EM_FADE_SPEED_DOOR);
        }
        break;
    case GO_PROMPT:
        /* func_001AC480 sub 2 — gated until the fade is idle. */
        if (em_frame_fade_level() > 0.0f) break;
        if (in->held == 0) {
            /* no button held: the idle timeout walks (engine: the
             * decrement runs only on input-free frames) */
            if (g.go_timer > 0 && --g.go_timer == 0) {
                em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
                g.go_state  = GO_PROMPT_TIMEOUT;
                g.go_frames = 0;
            }
            break;
        }
        /* any held button re-arms the timer (engine tail) */
        g.go_timer = GO_PROMPT_FRAMES;
        if (in->pressed & (EM_PAD_START | EM_PAD_CROSS)) {
            /* confirm (engine mask 0x840) — per-option sound, then
             * fade out; dispatch happens at hold-black */
            em_sfx_play(g.go_cursor == 0 ? GO_SFX_CONFIRM0
                        : g.go_cursor == 1 ? GO_SFX_CONFIRM1
                                           : GO_SFX_CONFIRM2);
            em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
            g.go_state  = GO_PROMPT_CONFIRM;
            g.go_frames = 0;
        } else if ((in->pressed & EM_PAD_DOWN) &&
                   g.go_cursor < GO_CURSOR_MAX) {
            g.go_cursor++;                          /* 0x4000 = DOWN  */
            em_sfx_play(GO_SFX_MOVE);
        } else if ((in->pressed & EM_PAD_UP) && g.go_cursor > 0) {
            g.go_cursor--;                          /* 0x1000 = UP    */
            em_sfx_play(GO_SFX_MOVE);
        }
        break;
    case GO_PROMPT_CONFIRM:
        if (em_frame_fade_level() < 1.0f) break;
        if (g.go_cursor == 0) {
            /* CONTINUE — the engine reinstalls the gameplay task
             * (func_001AB790(func_001ACEC0), D_00275BE0 = 0); the
             * port's equivalent area re-entry is the scene reload,
             * serviced by the frame machine (go_restart). */
            g.go_restart = 1;
        } else {
            /* options 1 (load game) / 2 (sub-screen): the engine's
             * targets (func_00225A00 memory-card flow / func_00200A40)
             * have no native counterpart — no save system.
             * CORRECTED (src/func_001AC070.c, NEARMISS): only option 2
             * really is a return-to-prompt — state 6 polls
             * func_00200A40() and goes back to state 2. Option 1 is
             * state 5, which polls func_00225AC0(0): verdict 1 =
             * CANCEL -> back to state 2 (the prompt), but verdict 2 =
             * a SUCCESSFUL LOAD -> func_001AF150(), D_00275BE0 = 1 and
             * state 4, i.e. it reinstalls the gameplay task exactly
             * like CONTINUE. The port has no save system, so it takes
             * option 1's CANCEL path only — a deliberate stand-in for
             * the load path, NOT the engine's whole behaviour.
             * FLAGGED untranslated sub-screens. */
            g.go_state  = GO_PROMPT;
            g.go_frames = 0;
            g.go_timer  = GO_PROMPT_FRAMES;
            em_frame_fade_start(-1, EM_FADE_SPEED_DOOR);
        }
        break;
    case GO_PROMPT_TIMEOUT:
        /* engine sub 4: held input mid-fade cancels back to the
         * prompt; at hold-black the engine cycles to the TITLE/attract
         * screens — the port has no title scene, so it re-enters the
         * prompt (FLAGGED divergence, documented in the PD block). */
        if (in->held != 0 && em_frame_fade_level() < 1.0f) {
            em_frame_fade_start(-1, EM_FADE_SPEED_DOOR);
            g.go_state  = GO_PROMPT;
            g.go_frames = 0;
            g.go_timer  = GO_PROMPT_FRAMES;
            break;
        }
        if (em_frame_fade_level() >= 1.0f) {
            g.go_state  = GO_PROMPT;
            g.go_frames = 0;
            g.go_timer  = GO_PROMPT_FRAMES;
            em_frame_fade_start(-1, EM_FADE_SPEED_DOOR);
        }
        break;
    default:
        break;
    }
}

/* ================================================================== */
/* AREA-11 OPENING PROGRESSION — game-state flags + the scripted        */
/* elevator descent (ov 0x00828050). Decoded:                           */
/* INVESTIGATION_area11_elevator.md. Batch-2 contract A/D.              */
/* ================================================================== */

/* Contract-A flag accessors (em_game.h). The engine stores these as the
 * persistent game-state bytes D_00810811 (battery) and D_00810841[11]
 * bit 7 (terminal powered); the port mirrors each as a 0/1 flag. */
int  em_game_has_battery(void)        { return g.have_battery; }
void em_game_set_battery(int on)      { g.have_battery = on ? 1 : 0; }
int  em_game_terminal_powered(void)   { return g.terminal_powered; }
void em_game_set_terminal_powered(int on) { g.terminal_powered = on ? 1 : 0; }

/* em_game_player_interact_anim (CORRECTED two-terminal flow,
 * INVESTIGATION_area11_elevator.md). Play a one-shot scripted clip ON THE
 * PLAYER and lock player input/movement for its duration — the engine's
 * "scripted-anim-owns-player" model (player+0x2F3 = 3): the script op0A
 * handler 0x001B9A00 writes player+0x1F2 = clip id, player+0x40 = clip
 * ptr, player+0x2F3 = 3, and the player's free-move action machine is
 * suppressed while that state holds (LIVE: the outside insert clip = 0x14,
 * the internal lever clip = 0x47, both rate 1.0, both lock via this
 * scripted-anim state — NOT a control-mode flag).
 *
 * Natively this rides the same sa_* mailbox as em_game_anim_request (a
 * one-shot: plays once at `rate`, holds its last frame, then locomotion
 * resumes) and additionally raises interact_active, which player_move
 * reads as a stand-still movement lock and em_game_player_interact_busy
 * reports. actor_update detects the clip's end and drops the lock.
 *
 * IDEMPOTENT: a call for the clip that is already the running interact
 * (or while any interact is busy) is a no-op, so the examine logic can
 * call it every frame the press holds without re-triggering. If the
 * loaded player EMDL lacks `clip_id` the anim request degrades to a
 * no-op (em_game_anim_request returns 0) but the LOCK is still raised for
 * a minimum window so the gating stays faithful (the engine locks on the
 * scripted-anim state, which is set regardless of whether the clip
 * resolves). */
void em_game_player_interact_anim(int clip_id)
{
    if (clip_id <= 0) return;
    /* already running (this clip, another interact, or the ride owns the
     * player) — idempotent no-op */
    if (g.interact_active || g.elev_state == 1) return;

    g.interact_clip   = (unsigned)clip_id;
    g.interact_active = 1;
    g.interact_seen   = 0;        /* not yet committed (commit is next
                                   * actor_update); end-detection waits */
    /* Request the one-shot clip on the player (rate 1.0 — the live insert
     * 0x14 / lever 0x47 rate). If the EMDL lacks the clip the request is
     * a no-op and the clip will never commit; interact_seen then stays 0
     * and the lock would never clear via the clip path, so for the
     * missing-clip case we leave interact_seen pre-armed so the lock
     * releases on the next actor_update (faithful-minimum: lock raised,
     * clip absent — FLAGGED in em_game.h). */
    if (!em_game_anim_request(g.interact_clip, 1.0f)) {
        g.interact_seen = 1;      /* no clip to wait on — release next frame */
        printf("interact: player clip %#x absent — lock-only (FLAGGED)\n",
               clip_id);
    } else {
        printf("interact: player scripted clip %#x — input locked\n",
               clip_id);
    }
}

/* em_game_player_interact_busy — 1 while a scripted interact anim OR the
 * elevator ride owns the player (the stand-still lock raised by
 * em_game_player_interact_anim, or the 150-frame descent). The examine
 * logic reads this so it does not double-trigger a second interaction
 * while one is in flight. Also true while the descent is armed-and-
 * waiting (elev_pending) so the brief window between the lever clip
 * ending and the ride starting is still busy (no input leak).
 *
 * The AREA-11 OPENING DIRECTOR (cine_active) folds in here: while an
 * establishing-cutscene beat runs the player is frozen (camera-only
 * cinematic, no scripted walk — live-confirmed). This one read makes
 * player_move suppress free movement AND the examine/pickup/door use
 * scans suppress themselves for the beat's duration — the same lock
 * shape as the elevator ride. cine_active only ever clears via the
 * director's keyframe exhaustion (cine_beat_finish), so this can never
 * latch a soft-lock. */
int em_game_player_interact_busy(void)
{
    return g.interact_active || g.elev_state == 1 || g.elev_pending ||
           g.cine_active;
}

/* em_game_player_face_step — the examine op04 FACE pre-roll: turn the
 * player BODY heading (g.yaw) toward `target_yaw` by one turn-in-place
 * step and report whether the player is now facing it. Reuses the
 * decoded turn-toward stepper player_turn_toward at the standing
 * turn-in-place rate TURN_IP_GAIT03 (0.3927 rad = 22.5 deg/frame, gait
 * 0/3 — FINDINGS "GROUND LOCOMOTION" s78), which SNAPS when |delta| <=
 * rate (no overshoot). The engine's op04 (handler 0x001B9C10) writes the
 * scripted yaw to the heading-target slot 0x810374 and the player's own
 * turn/heading machine (func_00174AC0 + func_001B12B0) eases the body
 * onto it; this is that ease, driven by the locked examine sequence.
 * PROVENANCE: of that pair only src/func_00174AC0.c is readable C
 * (NEARMISS 98.48%). src/func_001B12B0.c is an ALL-WORD stub — byte-
 * correct for the build but with NO recovered logic — and 0x001B9C10 is
 * a data/handler address, not a recovered function. The 22.5 deg/frame
 * rate itself comes from FINDINGS "GROUND LOCOMOTION", not from either
 * of those two files; treat the easing shape as OBSERVED, not decoded.
 *
 * This does NOT touch player_move's desired-heading / movement-v3 path:
 * the examine lock in player_move (em_examine_input_locked) already
 * suppresses free locomotion for the whole script window, so player_move
 * never writes g.yaw while the FACE phase owns it. Returns 1 once g.yaw
 * == target_yaw (snapped), else 0 (still turning).
 *
 * INVESTIGATION_examine_walk_face.md §3: op04 sub 8 is instantaneous to
 * SET, but the pivot itself plays out over the following frames at the
 * standing turn rate — that pivot is the missing interaction animation
 * the owner flagged. */
int em_game_player_face_step(float target_yaw)
{
    player_turn_toward(target_yaw, TURN_IP_GAIT03);
    /* facing once the snap has landed g.yaw exactly on the target
     * (player_turn_toward writes target_yaw verbatim on the snap step). */
    float diff = target_yaw - g.yaw;
    while (diff >  EM_PI) diff -= 2.0f * EM_PI;
    while (diff < -EM_PI) diff += 2.0f * EM_PI;
    return fabsf(diff) < 1e-4f;
}



/* em_game_elevator_start (contract A/D) — the powered terminal script's
 * opcode-9 install of the descent actor (ov 0x00828050). IDEMPOTENT:
 * a call while descending (state 1), pending (armed, waiting on the lever
 * anim) or after the run (state 2) is a no-op, matching the engine
 * installing the actor exactly once per use and the port running the
 * descent once per scene.
 *
 * SEQUENCING (CORRECTED two-terminal flow): the powered script 0x82A750
 * plays the lever-throw anim 0x47 (op0A) BEFORE its op09 elevator
 * install, so when the INTERNAL terminal calls
 * em_game_player_interact_anim(0x47) and then this immediately, the
 * descent must NOT begin until the lever clip finishes — otherwise the
 * player would be carried down mid-anim with input leaking. If an
 * interact anim is busy, ARM the descent (elev_pending) and let
 * elevator_tick begin the ride the frame the lever clip ends. With no
 * interact in flight (a direct/test call), begin immediately. */
void em_game_elevator_start(void)
{
    if (g.elev_state != 0 || g.elev_pending) return;  /* once per scene */
    if (g.interact_active) {
        g.elev_pending = 1;          /* wait for the lever anim 0x47 */
        printf("elevator: descent ARMED — waiting for the interact anim "
               "to finish\n");
        return;
    }
    elevator_descent_begin();
}



/* ------------------------------------------------------------------ */
/* AREA-11 GATED GRATE (func_00159210) — closed path-blocker + slide  */
/* INVESTIGATION_area11_grate.md. The bars (per-area id 0x04) render   */
/* AND register a SOLID blocker hull while the area is not powered     */
/* (em_game_terminal_powered, D_0081084C & 0x80 — 0 at new game). On   */
/* power the hull is dropped and the bars slide open over ~120 frames. */
/* ------------------------------------------------------------------ */






/* AREA-11 STEAM / FX EMITTER tick (placement record 7, ov 0x008235F0 —
 * INVESTIGATION_first_level_area11.md §3/§6). The emitter's POINT LIGHT
 * is registered in the placed-lamp list at parse time (folded into actor
 * lighting by char_rig_build — no per-frame work). This tick owns the two
 * time-varying parts: the LOOPING HISS (0x413, retriggered every
 * STEAM_SND_PERIOD frames because the port SFX path is one-shot — there
 * is no loop primitive in em_sfx; em_sfx_play_at culls it by distance like
 * the engine's play_sound, so it is silent until the player is within
 * STEAM_SND_RADIUS), and the PUFF FX phase. Dormant unless a `steam` line
 * was parsed, so non-AREA-11 scenes do nothing. */
static void steam_tick(void)
{
    if (!g.steam_on) return;
    if (g.steam_snd_t <= 0) {
        em_sfx_play_at(STEAM_SND_ID, g.steam_pos, STEAM_SND_RADIUS);
        g.steam_snd_t = STEAM_SND_PERIOD;
    } else {
        g.steam_snd_t--;
    }
    /* puff cycle: a normalized 0..1 phase that the render reads to rise +
     * fade the billboard (a looping vent puff). */
    g.steam_fx_phase += 1.0f / 120.0f;     /* ~2 s loop @ 60 Hz */
    if (g.steam_fx_phase >= 1.0f) g.steam_fx_phase -= 1.0f;
}

/* AREA-11 STEAM PUFF FX — a minimal billboard plume (em_gfx_beam_dot:
 * additive camera-facing world quad, the same path the laser dot uses).
 * Draws two offset puffs rising + fading on the looping phase. Queued in
 * the world pass (a 3D draw must have run this frame, as for the laser).
 * Cosmetic + optional per the task; the light + sound are the priority.
 * No-op unless the emitter is present. */
static void steam_fx_render(EmGfx *gfx)
{
    if (!g.steam_on || !gfx) return;
    for (int i = 0; i < 2; i++) {
        float ph = g.steam_fx_phase + 0.5f * (float)i;
        if (ph >= 1.0f) ph -= 1.0f;
        /* fade in over the first 20%, out over the last 50% (a soft puff) */
        float a = ph < 0.2f ? ph / 0.2f
                : ph > 0.5f ? (1.0f - ph) / 0.5f : 1.0f;
        if (a <= 0.0f) continue;
        float p[3] = { g.steam_pos[0],
                       g.steam_pos[1] + STEAM_FX_RISE * ph,
                       g.steam_pos[2] };
        /* faint warm-white steam, low alpha so it stays subtle */
        const float rgba[4] = { 0.85f, 0.82f, 0.78f, 0.22f * a };
        em_gfx_beam_dot(gfx, p, STEAM_FX_SIZE * (0.6f + 0.8f * ph), rgba);
    }
}

/* loco_clip_for_tier — the mode-1 id row {0,1,2,3} (idle/walk/jog/run)
 * indexed by the ramped locIdx +0x25C (func_0017B490 row D_00248A10),
 * with the same down-row fallback the asset-availability check uses:
 * tier>=3 -> run, tier>=2 -> jog, else walk; missing clips drop a row.
 * Used both for the active clip and (tier+1) for the C1 ramp cross-blend. */
static int loco_clip_for_tier(int tier)
{
    if (tier >= 3 && g.clip_run >= 0) return g.clip_run;
    if (tier >= 2 && g.clip_jog >= 0) return g.clip_jog;
    return g.clip_walk;
}

/* func_0015BCF0 — player actor update. The engine's per-actor spine
 * (state/AI, anim-evaluator selection, physics, sound triggers); the
 * port's slice of it is movement (frame input -> position/yaw) plus the
 * anim side: evaluate the bone palette at the current clip time, then
 * compose the world placement onto it.
 *
 * Idle<->walk crossfade (the anim-evaluator slice): the walk blend
 * weight seeks move_speed / WALK_SPEED linearly over ANIM_BLEND_TIME,
 * and the in-place walk clip advances at move_speed / WALK_CLIP_SPEED
 * so the stride tracks the ground (it freezes while standing). At
 * weight 0 the idle path is bit-exactly the old single-clip evaluation,
 * keeping EM_CAPTURE idle output stable.
 *
 * SCRIPTED ANIM (em_game.h em_game_anim_request): the commit slice of
 * the spine. If the request mailbox (sa_req, the native +0x1F2)
 * differs from the committed id (sa_cur, +0x20C), commit it — the
 * engine's func_00183090 "copy +0x1F2 -> +0x20C, anim_clip_init" path,
 * one frame after the request, exactly the original latency (requests
 * land from the world-services slot AFTER this update ran). While
 * committed, the scripted clip OWNS the palette: it plays once at
 * sa_rate frames/tick, holds its last frame, and ends back into
 * locomotion (clip over, or em_game_anim_cancel — the script
 * teardown's +0x1F2 = 0). The locomotion blend state is parked at
 * idle while suspended, so the return is the plain idle pose (the
 * door sequence re-places the player standing). */
static void actor_update(void)
{
    /* SCRIPTED INTERACT anim end-detection (em_game_player_interact_anim).
     * The interact clip is a one-shot through the sa_* mailbox: the
     * request lands the frame em_game_player_interact_anim is called
     * (from em_examine_update, AFTER this update ran), the commit fires
     * here next frame (sa_cur -> interact_clip), and the clip clears
     * itself back to sa_cur == 0 when it plays through. Track the commit
     * (interact_seen) so the pre-commit frame (sa_cur still 0) does not
     * release the lock early; once seen, sa_cur leaving the interact clip
     * means the clip ended -> control returns (interact_active = 0). This
     * runs BEFORE player_move so the movement lock is dropped the same
     * frame the clip finishes (no extra locked frame). */
    if (g.interact_active) {
        if (g.sa_cur == g.interact_clip && g.interact_clip != 0)
            g.interact_seen = 1;        /* committed + running */
        else if (g.interact_seen)
            g.interact_active = 0;      /* clip ended -> control returns */
    }

    /* PLAYER STATE 2 (hit reaction / dying) replaces the free-move
     * spine entirely — the engine's state dispatch (func_0015BA50)
     * routes to the hurt machine instead of the action machine; the
     * committed reaction clip owns the palette through the scripted-
     * anim path below. */
    if (g.pd_state == 2) {
        player_hurt_tick();
        g.gait       = 0;
        g.move_speed = 0.0f;
        g.loco_tier  = 0;
    } else {
        player_move();
    }
    if (!g.mesh) return;

    /* scripted-anim COMMIT (func_00183090: +0x1F2 != +0x20C). */
    if (g.sa_req != g.sa_cur) {
        g.sa_cur  = g.sa_req;
        g.sa_clip = g.sa_req
                  ? em_model_clip_index(&g.model, (uint32_t)g.sa_req)
                  : -1;
        g.sa_hold = g.sa_req_hold;
        g.sa_t    = 0.0;
    }
    if (g.sa_cur && g.sa_clip >= 0) {
        const EmModelClip *cs = &g.model.clips[g.sa_clip];
        /* Evaluate, then advance; clamp at the LAST frame (one-shot —
         * em_model_palette_at would loop-blend back into frame 0). */
        double end = (double)(cs->frame_count - 1);
        double t   = g.sa_t < end ? g.sa_t : end;
        /* AIM POSE LADDER: while the held clip is the ladder BASE
         * (0x112 — the stance tops request the base code and the
         * DISPATCH picks the ladder step, exactly the engine split)
         * and the player is aiming, the bilinear pose-grid blend owns
         * the palette; the recoil restart (sa_t rewind) runs through
         * unchanged — every ladder step is the same 25 frames. */
        if (!(g.sa_cur == 0x112 &&
              (em_weapon_is_aiming() || g.r2_aim) &&
              aim_ladder_eval(t)))
            em_model_palette_at(&g.model, (uint32_t)g.sa_clip, t,
                                g.player_palette);
        palette_apply_placement(g.player_palette, g.model.bone_count,
                                g.pos, g.yaw);
        g.sa_t += (double)g.sa_rate;
        if (g.sa_t >= end + 1.0) {     /* played through: clip-end flag */
            if (g.sa_hold) {
                g.sa_t = end;          /* HELD POSE: clamp and keep the
                                        * palette (em_game_anim_hold) */
            } else {
                g.sa_req = g.sa_cur = 0;  /* (+0x200 & 0x1000 analog) */
                g.sa_clip = -1;
            }
        }
        g.walk_w = 0.0f;               /* locomotion parked at idle */
        g.idle_timer = IDLE_FIDGET_FRAMES;   /* scripted anim leaves
                                              * mode 0: cycle re-armed */
        g.idle_phase = 0;
        g.fid_w      = 0.0f;
        return;
    }

    /* Locomotion clip by TIER (the mode-1 id row {0,1,2,3} indexed by
     * the ramped locIdx +0x25C, NOT the raw gait — the 2026-06-11
     * tier-ramp correction): walk for tier 1, jog for tier 2, run for
     * tier 3, each falling back down-row when the asset lacks the clip.
     * The DOOR TRANSIT scripted MOVE-TO (tier 1) keeps the walk clip;
     * the ARRIVAL WALK-OUT (tier 2) plays the engine's jog clip.
     *
     * C1 (INVESTIGATION_movement_exact.md §A.6, anim_matrix_player): the
     * engine does NOT hard-cut jog->run on promotion. It CROSS-BLENDS the
     * current-tier clip into the next-tier clip by the within-tier
     * progress +0x208 over the ~8 accel frames, and CONTINUES the clip
     * cycle across the swap (anim_clip_init is not re-run on the ramp).
     * The port previously hard-swapped the single loco_clip and RESET the
     * cycle (walk_t = 0) on promotion — the "snaps to the run animation"
     * the owner reported. Fix: keep the cycle continuous, and evaluate
     * both the current- and next-tier clip cross-blended by progress. */
    int loco = loco_clip_for_tier(g.loco_tier);
    if (loco != g.loco_clip) {
        g.loco_clip  = loco;
        g.loco_speed = (loco >= 0 && loco == g.clip_run) ? RUN_CLIP_SPEED
                     : (loco >= 0 && loco == g.clip_jog) ? JOG_CLIP_SPEED
                                                         : WALK_CLIP_SPEED;
        /* DO NOT reset walk_t/step_prev on a tier-driven swap (C1) — the
         * jog->run cycle continues; resetting is the hard cut. */
    }

    /* WITHIN-TIER PROGRESS (the engine's +0x208/+0x204 driver, FIX 2 /
     * C1+C2, LIVE-confirmed "DEEP A/B v2"): the within-current-tier
     * fraction 0..1 = (loco_upt - kTier[tier]) / (kTier[tier+1] -
     * kTier[tier]). At a sustained tier loco_upt == kTier[tier] so
     * progress == 0; during accel it sweeps 0->1 and RE-BASES (resets to
     * 0) at each tier promotion because loco_upt and kTier[tier] both
     * step to the new tier's base. Valid for any accelerating tier 0..2;
     * tier 3 (top) has no next tier so progress stays 0. This single
     * fraction drives BOTH the +0x208 cross-blend and the +0x204 clip
     * rate below. */
    int   next_loco  = (g.loco_tier < 3) ? loco_clip_for_tier(g.loco_tier + 1)
                                         : -1;
    float progress = 0.0f;
    if (g.loco_tier >= 0 && g.loco_tier < 3) {
        float lo = kLocoTierSpeed[g.loco_tier];
        float hi = kLocoTierSpeed[g.loco_tier + 1];
        if (hi > lo) {
            progress = (g.loco_upt - lo) / (hi - lo);
            if (progress < 0.0f) progress = 0.0f;
            if (progress > 1.0f) progress = 1.0f;
        }
    }

    /* C1 CROSS-BLEND (+0x208): cross-fade the current-tier clip into the
     * NEXT-tier clip by `progress` over the accel frames (jog id 2 ->
     * run id 3, etc.). Only meaningful while a distinct higher-tier clip
     * exists; tier 0's "walk" clip equals tier 1's when the asset lacks a
     * separate idle-walk, so gate on a real, different next clip. */
    float loco_blend = 0.0f;
    if (g.loco_tier >= 1 && g.loco_tier < 3 && next_loco >= 0 &&
        next_loco != g.loco_clip) {
        loco_blend = progress;
    } else {
        next_loco = -1;        /* no next-tier morph this frame */
    }

    float target = 0.0f;
    if (g.loco_clip >= 0) {
        target = g.move_speed > 0.0f ? 1.0f : 0.0f;
        float step = FRAME_DT / ANIM_BLEND_TIME;
        if      (g.walk_w < target - step) g.walk_w += step;
        else if (g.walk_w > target + step) g.walk_w -= step;
        else                               g.walk_w  = target;
        /* CLIP PLAYBACK RATE (+0x204, FIX 2 / C2, LIVE-confirmed): the
         * engine plays the (blended) loco clip at rate 1.0 + progress
         * during accel — climbing 1.0 -> 2.0 within each tier, then
         * RESETTING to 1.0 at each promotion (progress re-bases). This
         * REPLACES the old stride-lock rate (move_speed / natural). walk_t
         * is in seconds; advancing it by FRAME_DT plays at the clip's
         * natural fps (rate 1.0), so the FRAME_DT * (1.0 + progress)
         * increment realizes the engine's +0x204 multiplier. The cycle is
         * continuous across the tier swap (no reset), so the rising rate
         * and the cross-blend stay phase-locked, exactly the build-up. */
        double clip_rate = g.move_speed > 0.0f ? (double)(1.0f + progress)
                                               : 0.0;
        g.walk_t += (double)FRAME_DT * clip_rate;

        /* FOOTSTEP triggers (footstep_play above): the loco-cycle
         * playhead in clip FRAMES — wrapping exactly like the palette
         * evaluation — against the clip's D_00248C90 trigger frames
         * (walk id 1: 72/21; jog id 2: 26/3; run id 3: 21/2). The
         * playhead only advances while moving (rate-scaled to the
         * ground speed), so standing is silent and slower walks space
         * their steps out, exactly like the engine's clip-time test. */
        int run = g.loco_clip == g.clip_run && g.clip_run >= 0;
        int jog = g.loco_clip == g.clip_jog && g.clip_jog >= 0;
        double fa = run ? RUN_STEP_FRAME_A
                  : jog ? JOG_STEP_FRAME_A : WALK_STEP_FRAME_A;
        double fb = run ? RUN_STEP_FRAME_B
                  : jog ? JOG_STEP_FRAME_B : WALK_STEP_FRAME_B;
        const EmModelClip *cw = &g.model.clips[g.loco_clip];
        double cyc = fmod(g.walk_t * (double)cw->fps,
                          (double)cw->frame_count);
        if (step_crossed(g.step_prev, cyc, fa) ||
            step_crossed(g.step_prev, cyc, fb))
            footstep_play(run ? 3 : jog ? 2 : 1);
            /* a1 = the TIER (+0x25C): run +0xA, jog +5, walk +0
             * (func_00182430 sub-bases — the s30 "walk 0x15/run 0x1A"
             * captures were tiers 2/3 under the corrected labels) */
        g.step_prev = cyc;
    }

    /* IDLE CYCLE (func_00161020 — see the clip-id block): while truly
     * idle the 300-frame timer runs; at zero the fidget 349 plays once
     * (8-frame cross-fade each way, the engine's blend arg) and the
     * breathing idle restarts with the timer re-armed. Any movement,
     * gait input, aim, melee or the door input lock leaves mode 0 and
     * resets the cycle. */
    {
        int active = g.move_speed > 0.0f || g.gait != 0 ||
                     em_weapon_is_aiming() || em_weapon_is_melee() ||
                     em_door_movement_locked();
        float fstep = FRAME_DT / IDLE_BLEND_TIME;
        if (active || g.clip_fidget < 0) {
            g.idle_timer = IDLE_FIDGET_FRAMES;
            g.idle_phase = 0;
            g.fid_w     -= fstep;          /* fade an aborted fidget out */
            if (g.fid_w < 0.0f) g.fid_w = 0.0f;
        } else if (g.idle_phase == 0) {
            g.fid_w -= fstep;
            if (g.fid_w < 0.0f) g.fid_w = 0.0f;
            if (--g.idle_timer <= 0) {     /* +0x28 hit 0: fidget */
                g.idle_phase = 1;
                g.fid_t      = 0.0;
            }
        } else {
            g.fid_w += fstep;
            if (g.fid_w > 1.0f) g.fid_w = 1.0f;
            g.fid_t += FRAME_DT;
            const EmModelClip *cf = &g.model.clips[g.clip_fidget];
            if (g.fid_t * cf->fps >= (double)(cf->frame_count - 1)) {
                /* clip-end flag (+0x200 & 0x1000): back to breathing,
                 * re-init (clip restarts at 0), timer re-armed */
                g.idle_phase = 0;
                g.idle_timer = IDLE_FIDGET_FRAMES;
                g.idle_t     = -FRAME_DT;  /* restarts at 0 after the
                                            * post-eval advance below */
            }
        }
    }

    /* DEBUG (EM_CLIP_DEMO=1): turntable the player model through the 4
     * formerly-unbakeable directory clips 54/94/115/375 (s76 baker fix)
     * so they can be eyeballed — each plays ~4 s (looping) while the
     * model slowly spins; the active id + length is printed on each
     * switch. Needs a player.emdl exported WITH those 4 clips. */
    {
        static const int kCD[4] = { 54, 94, 115, 375 };
        static int    cd_on = -1, cd_idx = 0, cd_clip = -2, cd_hold = 0;
        static double cd_t = 0.0, cd_spin = 0.0;
        if (cd_on < 0) cd_on = getenv("EM_CLIP_DEMO") != NULL;
        if (cd_on) {
            if (cd_clip == -2 || cd_hold++ >= 240) {
                if (cd_clip != -2) cd_idx = (cd_idx + 1) & 3;
                cd_hold = 0;
                cd_t    = 0.0;
                cd_clip = em_model_clip_index(&g.model,
                                              (uint32_t)kCD[cd_idx]);
                printf("clip demo: directory id %d -> model clip %d "
                       "(%d frames)\n", kCD[cd_idx], cd_clip,
                       cd_clip >= 0
                       ? g.model.clips[cd_clip].frame_count : 0);
                fflush(stdout);
            }
            int show = cd_clip >= 0 ? cd_clip
                     : (g.clip_idle >= 0 ? g.clip_idle : 0);
            const EmModelClip *cc = &g.model.clips[show];
            double per = (double)cc->frame_count;
            em_model_palette_at(&g.model, (uint32_t)show,
                                per > 1.0 ? fmod(cd_t * cc->fps, per) : 0.0,
                                g.player_palette);
            cd_t    += FRAME_DT;
            cd_spin += (double)FRAME_DT * 0.6;   /* slow turntable */
            palette_apply_placement(g.player_palette, g.model.bone_count,
                                    g.pos, g.yaw + (float)cd_spin);
            return;
        }
    }

    const EmModelClip *ci = &g.model.clips[g.clip_idle];
    em_model_palette_at(&g.model, (uint32_t)g.clip_idle,
                        g.idle_t * ci->fps, g.player_palette);
    if (g.fid_w > 0.0f && g.clip_fidget >= 0) {
        const EmModelClip *cf = &g.model.clips[g.clip_fidget];
        double ft  = g.fid_t * cf->fps;
        double end = (double)(cf->frame_count - 1);
        em_model_palette_at(&g.model, (uint32_t)g.clip_fidget,
                            ft < end ? ft : end, g.walk_palette);
        uint32_t n = g.model.bone_count * 16;
        float    w = g.fid_w;
        for (uint32_t i = 0; i < n; i++)
            g.player_palette[i] += (g.walk_palette[i] -
                                    g.player_palette[i]) * w;
    }
    if (g.walk_w > 0.0f && g.loco_clip >= 0) {
        uint32_t n = g.model.bone_count * 16;

        /* current-tier clip into walk_palette (the loco pose base) */
        const EmModelClip *cw = &g.model.clips[g.loco_clip];
        em_model_palette_at(&g.model, (uint32_t)g.loco_clip,
                            g.walk_t * cw->fps, g.walk_palette);

        /* C1: cross-blend the NEXT-tier clip in by the ramp progress
         * (+0x208). Both clips ride the SAME walk_t cycle clock (each
         * scaled by its own fps) so the jog->run morph stays in phase as
         * the cycle continues across the promotion. At progress 0 (a
         * sustained tier) this is a no-op; sweeping 0->1 over the accel
         * frames is the visible build-up. */
        if (next_loco >= 0 && loco_blend > 0.0f) {
            const EmModelClip *cn = &g.model.clips[next_loco];
            em_model_palette_at(&g.model, (uint32_t)next_loco,
                                g.walk_t * cn->fps, g.loco_palette);
            float b = loco_blend;
            for (uint32_t i = 0; i < n; i++)
                g.walk_palette[i] += (g.loco_palette[i] -
                                      g.walk_palette[i]) * b;
        }

        /* blend the (morphed) loco pose over idle by walk_w */
        float w = g.walk_w;
        for (uint32_t i = 0; i < n; i++)
            g.player_palette[i] += (g.walk_palette[i] -
                                    g.player_palette[i]) * w;
    }
    palette_apply_placement(g.player_palette, g.model.bone_count,
                            g.pos, g.yaw);
    g.idle_t += FRAME_DT;   /* post-eval, matching the old g.t cadence
                             * (frame n evaluates the idle at n/60) */
}

/* Reserve the next render-chain slot, or NULL if the chain is full.
 * The chain array is sized to the exact sum of every source cap
 * (SCENE_MAX + 4 single-slot draws + EM_DOOR_MAX + EM_ENEMY_MAX +
 * EM_PICKUP_MAX = CHAIN_CAP), so this never trips today. It is a DEFENSIVE
 * guard against the L1 silent-overflow shape: the night's truck+grate
 * additions already ate the sizing headroom once, and any future drift —
 * a bumped EM_ENEMY_MAX, a 16th scene part landing while all four single
 * draws are present, or a new single-slot draw added without widening the
 * array — would otherwise corrupt the trailing file-scope `g` members
 * (chain_test_triangle, bgm_path, …) with no diagnostic. Log once on a
 * full chain instead, mirroring the loaders' overflow logs. */
static ChainDraw *chain_push(void)
{
    if (g.chain_len >= CHAIN_CAP) {
        static int warned;
        if (!warned) {
            fprintf(stderr, "render: chain full (%d) — draw DROPPED; widen "
                    "g.chain[] (a source cap grew past its budget)\n",
                    CHAIN_CAP);
            warned = 1;
        }
        return NULL;
    }
    return &g.chain[g.chain_len++];
}

/* func_001D1C50 — render chain build. Records the frame's draws (the
 * native VIF chain): scene parts first, then the player, matching the
 * engine's draw order; with no assets at all, the gradient test triangle
 * keeps the repo runnable standalone. */
static void render_chain_build(void)
{
    g.chain_len           = 0;
    g.chain_test_triangle = 0;

    if (!g.mesh && !g.n_scene) {
        g.chain_test_triangle = 1;
        return;
    }
    for (int i = 0; i < g.n_scene; i++) {
        ChainDraw *cd = chain_push();
        if (!cd) return;
        *cd = (ChainDraw){ g.scene[i].mesh,
                           g.scene[i].palette,
                           g.scene[i].model.bone_count,
                           NULL };
    }
    /* ELEVATOR PLATFORM (the AREA-11 descent actor's own mesh, ov
     * 0x00828050 +0xB4). Drawn as a rigid prop whose pose elevator_tick
     * re-bakes each frame (the Y descends with the ride). Absent when
     * no `elevator` manifest line/mesh is provided — the ride still
     * works (player + camera descend), the platform just isn't shown. */
    if (g.elev_has_mesh && g.elev_mesh && g.elev_palette) {
        ChainDraw *cd = chain_push();
        if (cd)
            *cd = (ChainDraw){ g.elev_mesh, g.elev_palette,
                               g.elev_model.bone_count, NULL };
    }
    /* GATED GRATE bars (AREA-11 record 18 — the per-area id 0x04 bars mesh).
     * Drawn as a rigid prop whose pose grate_update re-bakes each frame (the
     * closed pose while locked, the retracted pose during/after the open
     * slide). The draw is owned by the grate object now (moved off the plain
     * `pickup ... prop` line); absent when no `grate` line placed one. */
    if (g.grate_present && g.grate_mesh && g.grate_palette) {
        ChainDraw *cd = chain_push();
        if (cd)
            *cd = (ChainDraw){ g.grate_mesh, g.grate_palette,
                               g.grate_model.bone_count, NULL };
    }
    /* WEDGED TRUCK (AREA-11 record 16 — the run-across set-piece). Drawn
     * as a rigid prop whose pose em_truck_update re-bakes each frame (the
     * placed wedged pose, then the falling/tumbling pose during the drop).
     * Absent when no `truck` line placed one. */
    {
        EmGfxMesh   *tmesh;
        const float *tpal;
        uint32_t     tbones;
        if (em_truck_draw(&tmesh, &tpal, &tbones)) {
            ChainDraw *cd = chain_push();
            if (cd)
                *cd = (ChainDraw){ tmesh, tpal, tbones, NULL };
        }
    }
    /* Interactive doors (actor draws — func_001BC300's publish). The
     * chain records palette POINTERS; em_door_update (the world-services
     * slot, after this build) writes this frame's pose into them before
     * the close-out flush. */
    for (int i = 0; i < em_door_count(); i++) {
        ChainDraw *cd = chain_push();
        if (!cd) return;
        em_door_draw(i, &cd->mesh, &cd->palette, &cd->bone_count);
        cd->tint = NULL;          /* doors draw untinted */
    }
    /* Enemies (the actor-pool draws). Same pointer contract as the
     * doors: em_enemy_update (after this build) writes this frame's
     * pose — and the per-draw tint for the tinted slots (tendril
     * spikes, death fades) — before the close-out flush. A dead slot
     * keeps drawing only while em_enemy's corpse fade runs, then
     * drops out. */
    for (int i = 0; i < em_enemy_count(); i++) {
        if (g.chain_len >= CHAIN_CAP) { (void)chain_push(); break; }
        ChainDraw *cd = &g.chain[g.chain_len];
        if (em_enemy_draw(i, &cd->mesh, &cd->palette, &cd->bone_count)) {
            cd->tint = em_enemy_draw_tint(i);
            g.chain_len++;
        }
    }
    /* Pickups (the func_001C4820/func_0015AFA0 actor draws — rigid
     * props, pose baked at placement; func_001C6380). A collected slot
     * stops drawing the frame it frees (em_pickup_draw returns 0). */
    for (int i = 0; i < em_pickup_count(); i++) {
        if (g.chain_len >= CHAIN_CAP) { (void)chain_push(); break; }
        ChainDraw *cd = &g.chain[g.chain_len];
        if (em_pickup_draw(i, &cd->mesh, &cd->palette, &cd->bone_count)) {
            cd->tint = NULL;       /* items draw untinted (the engine
                                    * AURA pass is unported — flagged
                                    * in em_pickup.h) */
            g.chain_len++;
        }
    }
    if (g.mesh) {
        ChainDraw *cd = chain_push();
        if (cd)
            *cd = (ChainDraw){ g.mesh, g.player_palette,
                               g.model.bone_count, NULL };
    }
}

/* func_001C1D00(0x008101D0) — once-per-area render-env init (GS regs,
 * area specials via func_001E2260/func_001E0CF0/func_001D5370). NOT
 * camera math (FINDINGS.md "CAMERA SYSTEM" corrections); skeleton no-op
 * until the render-env table is translated. */
static void render_env_init(void) {}

/* ------------------------------------------------------------------ */
/* CAMERA — the engine system (FINDINGS.md "CAMERA SYSTEM"):           */
/*   func_0018B9C0 state machine top  -> camera_update()               */
/*   func_0018BC20 mode dispatch      -> camera_mode_dispatch()        */
/*   func_0018D7B0 desired-eye solver -> camera_solve()                */
/*   func_0018C0D0 commit             -> camera_commit()               */
/* ------------------------------------------------------------------ */


















/* func_0018CE60 — the vertical-bounds SETTLE helper. RE-VERIFIED against
 * src/func_0018CE60.c (NEARMISS — logic authoritative): every constant
 * below reads out of that file literally — `mode = (arg2 == 2) ? 7 : 6`
 * at entry, the -/+200.0f probe endpoints, `+= 2.0f` on the style-2 arm
 * vs `+= 6.0f` when `*(float *)(cam + 0x5C) == 1.0f` else `+= 17.0f`,
 * the `<= 0.17f` normal test, the ceiling `-= 1.0f`, the
 * `lower = upper - 3.0f` cross-fix, the stores to cam+0x50 / cam+0x54
 * and the `if (arg2 != 5)` guard on the desired-eye-Y clamp.
 * (decoded s64; the
 * s10 note "settle vs world: 2x func_0019A910 ray queries" was this).
 * Probes 200 down / 200 up from `pt` (mask 7 for style 2, else 6) and
 * derives the eye-Y bounds:
 *   lower = floor hit + pad (style 2: +2; cam+0x5C == 1: +6; else +17)
 *     — a NON-floor-class hit whose normal has n.y <= 0.17 (a wall
 *     face seen from inside, 0x3E2E147B) keeps the RAW hit Y;
 *     no hit -> carried y_lo - 200;
 *   upper = ceiling hit - 1 (a non-ceiling-class hit with -n.y <= 0.17
 *     keeps the raw Y); no hit -> pt.y + 200;
 *   lower forced to upper - 3 if crossed.
 * Writes cam+0x50/+0x54 and (style != 5) clamps the desired eye Y into
 * them. The aim solver calls it when the primary block came from the
 * GRID walker (engine query-hub return 4) instead of clamping into the
 * previous frame's bounds. */
void cam_bounds_settle_0018CE60(EmCamera *cam, const float pt[3],
                                       int style)
{
    unsigned mask = EM_COLL_SET_CELLS | EM_COLL_SET_GRID;
    if (style == 2)
        mask |= EM_COLL_SET_HULLS;
    float pad = (style == 2) ? AIMS_FLOOR_PAD
              : (cam->var_5c == 1.0f ? SOLV_FLOOR_PAD1 : SOLV_FLOOR_PAD);
    EmCollHit hit;
    float probe[3] = { pt[0], pt[1] - SOLV_BOUND_RANGE, pt[2] };
    float lo, hi;

    if (em_collision_segment_query(&g.coll, pt, probe, mask,
                                   EM_COLL_ID_NONE, &hit)) {
        lo = hit.point[1];
        if ((hit.surf_class & (EM_SURF_FLOOR | EM_SURF_SLOPE)) ||
            hit.normal[1] > AIMS_SETTLE_NY)
            lo += pad;
    } else {
        lo = cam->y_lo - SOLV_BOUND_RANGE;
    }
    probe[1] = pt[1] + SOLV_BOUND_RANGE;
    if (em_collision_segment_query(&g.coll, pt, probe, mask,
                                   EM_COLL_ID_NONE, &hit)) {
        hi = hit.point[1];
        if ((hit.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)) ||
            -hit.normal[1] > AIMS_SETTLE_NY)
            hi -= SOLV_CEIL_PAD;
    } else {
        hi = pt[1] + SOLV_BOUND_RANGE;
    }
    if (lo > hi) lo = hi - 3.0f;
    cam->y_lo = lo;
    cam->y_hi = hi;
    if (style != 5) {
        if (cam->eye_des[1] < lo) cam->eye_des[1] = lo;
        if (cam->eye_des[1] > hi) cam->eye_des[1] = hi;
    }
}





/* ===================================================================== *
 * AREA-11 OPENING DIRECTOR — implementation
 * ===================================================================== */

/* The three beats, authoritative from INVESTIGATION_area11_director.md
 * §G (the per-beat EYE/TARGET keyframes are the literal data the engine
 * reads). WAIT keyframes (blend == -1) hold the prior framing; their
 * eye/tgt are carried from the preceding cue so the held framing is
 * exact. The zone AABBs + Y gates are the task-spec / doc §B/C/D values.
 *
 * NOTE ON BEAT-0 ZONE: the task spec and doc §B give the corrected live
 * poly X[335,385] Z[228,255] Y[260,280]; the doc's older §1.2 table and
 * §7 summary cite X[452,500] for beat 0 — those were the pre-correction
 * reads. We use the §B/§G corrected zone (the keyframe pass is the most
 * recent, live-RAM authoritative source). */
static const CineBeat kCineBeats[3] = {
    /* ---- BEAT 0 (step 0x00 -> 0x10): the vault-wheel sweep ---------- */
    {
        335.0f, 385.0f, 228.0f, 255.0f,   /* zone XZ */
        260.0f, 280.0f,                   /* Y gate [260,280] */
        0, CINE_STEP_BEAT1, 1,            /* no music; -> 0x10; register key */
        15, {
            { 0, 300, {356.0f,305.1f,209.4f}, {320.0f,303.5f,180.5f} },
            { 0, 300, {281.0f,331.0f,171.0f}, {315.0f,305.5f,191.5f} },
            { 0, 420, {395.0f,277.1f,272.4f}, {360.0f,284.5f,243.5f} },
            { 0, 300, {281.0f,331.0f,171.0f}, {315.0f,305.5f,191.5f} },
            { 0, 270, {325.0f,317.0f,310.0f}, {334.0f,300.0f,267.5f} },
            { 1, 100, {379.0f,344.8f,275.5f}, {397.0f,307.0f,254.0f} },
            {-1,  45, {379.0f,344.8f,275.5f}, {397.0f,307.0f,254.0f} },
            { 1, 120, {351.0f,296.0f,295.5f}, {396.0f,286.0f,306.0f} },
            {-1,  42, {351.0f,296.0f,295.5f}, {396.0f,286.0f,306.0f} },
            { 1, 120, {476.0f,356.8f,325.9f}, {481.0f,322.2f,294.0f} },
            {-1,  42, {476.0f,356.8f,325.9f}, {481.0f,322.2f,294.0f} },
            { 1, 100, {380.0f,359.0f,237.0f}, {381.0f,320.0f,211.0f} },
            {-1,  42, {380.0f,359.0f,237.0f}, {381.0f,320.0f,211.0f} },
            { 1, 100, {309.0f,306.0f,235.0f}, {324.0f,307.0f,191.0f} },
            {-1,  80, {309.0f,306.0f,235.0f}, {324.0f,307.0f,191.0f} },
        }
    },
    /* ---- BEAT 1 (step 0x10 -> 0x20): music cue 0x97 ----------------- */
    {
        452.0f, 500.0f, 278.0f, 292.0f,   /* zone XZ */
        -1.0e30f, 275.0f,                 /* Y gate: playerY <= 275 */
        CINE_MUSIC_BEAT1, CINE_STEP_BEAT2, 0,
        4, {
            { 0,  60, {459.0f,301.0f,310.0f}, {484.0f,288.0f,272.5f} },
            { 1, 100, {449.0f,366.0f,269.0f}, {465.0f,325.0f,254.0f} },
            { 1,  60, {459.0f,301.0f,310.0f}, {484.0f,288.0f,272.5f} },
            {-1,  90, {459.0f,301.0f,310.0f}, {484.0f,288.0f,272.5f} },
        }
    },
    /* ---- BEAT 2 (step 0x20 -> 0xFF): music cue 0x99 ----------------- */
    {
        410.0f, 439.0f, 175.0f, 203.0f,   /* zone XZ */
        -1.0e30f, 285.0f,                 /* Y gate: playerY <= 285 */
        CINE_MUSIC_BEAT2, CINE_STEP_DONE, 0,
        1, {
            /* sub0 +0xC = 0 -> a one-shot hard cut; hold one frame so the
             * music sting plays and the cut is visible before exit. */
            { 0,   1, {460.8f,324.5f,201.0f}, {420.0f,303.9f,191.5f} },
        }
    },
};

/* Map the persistent step byte to the active beat index (or -1 = the
 * director is parked: pre-beat-0 is index 0, done = -1). */
static int cine_step_to_beat(uint8_t step)
{
    switch (step) {
        case CINE_STEP_BEAT0: return 0;
        case CINE_STEP_BEAT1: return 1;
        case CINE_STEP_BEAT2: return 2;
        default:              return -1;   /* 0xFF = done */
    }
}

/* Player inside this beat's XZ AABB + Y gate (the doc's func_1B1EA0
 * point-in-poly reduced to the axis-aligned rect, plus the floor tier
 * gate). */
static int cine_in_zone(const CineBeat *b)
{
    return g.pos[0] >= b->x0 && g.pos[0] <= b->x1 &&
           g.pos[2] >= b->z0 && g.pos[2] <= b->z1 &&
           g.pos[1] >= b->ylo && g.pos[1] <= b->yhi;
}

/* End the running beat CLEANLY: drop the lock, advance the step byte,
 * fire the on-completion actions (music already fired at start per the
 * scripts; the key-item + milestone fire here), and clear the transient
 * so no soft-lock can persist. cine_was stays set for one frame so
 * director_camera does its one-shot chase restore. */
static void cine_beat_finish(void)
{
    const CineBeat *b = &kCineBeats[g.cine_beat];
    g.cine_step   = b->next_step;
    if (b->reg_keyitem) {
        /* func_1C4760(1) — register the opening key-item (beat 0). The
         * port has no separate key-item registry hook yet; the battery
         * pickup is the AREA-11 key item and is handled by em_pickup.
         * FLAGGED: faithful-minimum — the milestone advances; the
         * key-item register is a decode-noted no-op here. */
        printf("director: beat 0 complete — key-item register "
               "(func_1C4760) [FLAGGED no-op]\n");
    }
    g.cine_active = 0;
    g.cine_beat   = -1;
    printf("director: beat done — D_00810813 = %#x\n", g.cine_step);
}

/* DIRECTOR TICK — run each gameplay frame BEFORE actor_update (so the
 * lock is set before player_move reads em_game_player_interact_busy) and
 * before camera_update (which reads cine_active/the keyframe state for
 * the override). Tests the current step's zone; on entry starts the
 * beat (raises the lock + letterbox, fires the music cue, seats the
 * first keyframe); while running, advances the keyframes by their
 * durations and finishes the beat when they run out.
 *
 * SAFETY: dormant unless a zone is entered. Never starts a beat while
 * any OTHER lock owns the player (door transit, examine, elevator,
 * damage) so the cinematics never collide. cine_active only ever clears
 * via cine_beat_finish (keyframes exhausted) — there is no path that
 * leaves it raised with no advancing program. */
static void director_tick(void)
{
    /* OUTSIDE A BEAT: poll the current step's zone. The director is
     * fully dormant here — no camera/lock side effects. */
    if (!g.cine_active) {
        /* don't arm if any other system owns the player this frame */
        if (em_door_movement_locked() || em_examine_input_locked() ||
            em_game_player_interact_busy() || player_damage_locked())
            return;
        int beat = cine_step_to_beat(g.cine_step);
        if (beat < 0) return;                  /* director done (0xFF) */
        const CineBeat *b = &kCineBeats[beat];
        if (!cine_in_zone(b)) return;          /* not in the trigger zone */

        /* ENTER the beat (op07): raise lock + letterbox, fire the music
         * cue, seat keyframe 0. */
        g.cine_active = 1;
        g.cine_beat   = beat;
        g.cine_kf     = 0;
        g.cine_kf_t   = 0;
        /* a BLEND first keyframe starts from the live camera; seed the
         * blend start from the current actual eye/tgt. */
        memcpy(g.cine_blend_eye, g.cam.eye, sizeof g.cine_blend_eye);
        memcpy(g.cine_blend_tgt, g.cam.tgt, sizeof g.cine_blend_tgt);
        if (b->music) {
            /* op0C MUSIC cue. AREA-11 cue ids 0x97/0x99 ARE now in the
             * shipped sfx registry (0x097/0x099 in assets/sfx/sfx.txt), so
             * these cues resolve and play — the SFX_SOUND_MAX bump brought
             * them into the bank. (Was a silent no-op while only the office
             * bank was loaded.) */
            em_sfx_play((unsigned)b->music);
            printf("director: beat %d music cue %#x\n", beat, b->music);
        }
        printf("director: beat %d START — zone X[%.0f,%.0f] Z[%.0f,%.0f] "
               "@ (%.1f,%.1f,%.1f) — letterbox + player lock\n", beat,
               b->x0, b->x1, b->z0, b->z1, g.pos[0], g.pos[1], g.pos[2]);
        return;
    }

    /* RUNNING: advance the keyframe program. */
    const CineBeat *b = &kCineBeats[g.cine_beat];
    g.cine_kf_t++;
    if (g.cine_kf_t >= b->key[g.cine_kf].dur) {
        /* advance to the next keyframe; seed a blend's live start from
         * the keyframe we are leaving (its destination = the current
         * framing). */
        g.cine_kf++;
        g.cine_kf_t = 0;
        if (g.cine_kf >= b->n_key) {
            cine_beat_finish();          /* program exhausted -> done */
            return;
        }
        const CineKey *prev = &b->key[g.cine_kf - 1];
        if (prev->blend >= 0) {
            memcpy(g.cine_blend_eye, prev->eye, sizeof g.cine_blend_eye);
            memcpy(g.cine_blend_tgt, prev->tgt, sizeof g.cine_blend_tgt);
        }
        /* else (leaving a WAIT) the blend start carries unchanged */
    }
}

/* DIRECTOR CAMERA OVERRIDE — called from camera_update inside the
 * top_mode-0 block, BEFORE the door/examine cues, so a running beat
 * owns the camera outright. Writes cam->eye/tgt (and the desired
 * mirrors) from the current keyframe: a CUT snaps and holds; a BLEND
 * lerps from the keyframe's live start toward its destination across
 * its duration; a WAIT holds the prior framing. Returns 1 while it owns
 * the camera. On the frame the beat ends (cine_was set, cine_active
 * cleared) it does a ONE-SHOT chase restore behind the unmoved player
 * (the op18 / op07-sub4 restore — exactly the examcam release shape) and
 * returns 0 so the normal chase resumes cleanly. */
int director_camera(EmCamera *cam)
{
    if (g.cine_active) {
        const CineBeat *b = &kCineBeats[g.cine_beat];
        const CineKey  *k = &b->key[g.cine_kf];
        float eye[3], tgt[3];
        if (k->blend == 1 && k->dur > 0) {
            /* lerp from the live start toward the destination */
            float t = (float)g.cine_kf_t / (float)k->dur;
            if (t > 1.0f) t = 1.0f;
            for (int i = 0; i < 3; i++) {
                eye[i] = g.cine_blend_eye[i] +
                         (k->eye[i] - g.cine_blend_eye[i]) * t;
                tgt[i] = g.cine_blend_tgt[i] +
                         (k->tgt[i] - g.cine_blend_tgt[i]) * t;
            }
        } else {
            /* CUT or WAIT: hold the keyframe's framing exactly */
            memcpy(eye, k->eye, sizeof eye);
            memcpy(tgt, k->tgt, sizeof tgt);
        }
        memcpy(cam->eye_des, eye, sizeof eye);
        memcpy(cam->tgt_des, tgt, sizeof tgt);
        memcpy(cam->eye, eye, sizeof eye);
        memcpy(cam->tgt, tgt, sizeof tgt);
        cam->tgt_soft = 0;
        g.cine_was = 1;
        return 1;
    }
    if (g.cine_was) {
        /* RESTORE (op18 / op07 sub4): one-shot chase re-seat behind the
         * unmoved player, then hand the camera back to the normal solve
         * (identical to the examcam release / locked-door finish). */
        g.cine_was = 0;
        cam->tgt_soft = 0;
        cam->tgt_des[0] = g.pos[0];
        cam->tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
        cam->tgt_des[2] = g.pos[2];
        camera_desired_eye(cam);
        memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
        memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
    }
    return 0;
}

/* Letterbox alpha for this frame (0 = no bars). Raised while a beat
 * runs (cine_fade ramps up to CINE_BAR_FADE), dropped when it ends
 * (ramps back to 0) — a minimal fade in/out around the s81 ~64-line
 * top/bottom black bars. Called from frame_close_out. FLAGGED: the
 * exact opening staggered-fade frame counts are un-stopwatched (doc
 * §F); CINE_BAR_FADE is an approximation. */
static float director_letterbox_alpha(void)
{
    if (g.cine_active) {
        if (g.cine_fade < CINE_BAR_FADE) g.cine_fade++;
    } else {
        if (g.cine_fade > 0) g.cine_fade--;
    }
    if (g.cine_fade <= 0) return 0.0f;
    return (float)g.cine_fade / (float)CINE_BAR_FADE;   /* 0..1 */
}



/* --- STATUS-MENU UI SCENE (the constants block above) ----------------
 *
 * The engine's identity-UI-camera 3D pass: while the status screen is
 * up, the 3D frame is a black field with ONLY the player model
 * turntabling on it. RE-VERIFIED against src/func_0020E6F0.c
 * (BYTE-MATCHED — authoritative): its state 0 binds the model from the
 * PLAYER VARIANT table, seeds rotation +0xC0/+0xC4 = pi/+0xC8, seeds the
 * infection tint at +0x80/+0x84/+0x88, takes the position from the
 * view-matrix columns at D_00810610, and binds the clip by DISPLAYED
 * health D_00810858 (> 35 -> 0x1C2, else 0xA — the port's
 * clip_menu / clip_menu_low pair); state 1 runs the breathe ramp
 * +0x38 between 1.0 and 1.3 (UI_RAMP_MAX), wraps the yaw and calls
 * anim_advance_time; states 2/3/default free the slot.
 * RE-CONFIRMED (audit 2026-07-31): the 0.01f ramp/yaw steps, the pi
 * seed and -2pi wrap, the -80/-100/-30 x 0.01 tint triple with the
 * -127 clamp on the G row, anim_advance_time(1.0f), and the clip swap
 * being SWAP-BACK ONLY (`health > 35 && +0xB == 1 -> 0x1C2`, never the
 * reverse) all read literally out of src/func_0020E6F0.c. The actor is
 * re-created on every open, which is why the port re-inits on the
 * visible edge.  (func_0020E6F0 on the menu's private static-actor
 * stage — the world is not drawn at all; s44 verified there is no
 * other draw under the background tiles). The port mirrors that by
 * flushing this scene INSTEAD of the recorded world chain whenever the
 * screen is visible and the real animated background will draw
 * (em_hud_backdrop_ready — without the ui.emui asset the old
 * dim-over-scene fallback keeps the world flush, byte-identical to the
 * pre-scene builds). em_hud_scene_3d() then tells the background
 * drawer to skip its opaque base fill so the player shows between the
 * black frame and the translucent tile layers — the engine's order
 * (3D scene, background, panels). */

/* Lazy fullscreen black backplate: a quad at view depth 400 spanning
 * +-2000 units, drawn through em_gfx_draw_skinned_tinted with black —
 * every shading path multiplies to (0,0,0,1), giving the engine's
 * opaque black UI frame in the 3D pass (depth write on; the player at
 * z 40 passes the less-equal test). The renderer culls nothing
 * (MTLCullModeNone), so one winding suffices. */
static EmGfxMesh *ui_backplate_ensure(EmGfx *gfx)
{
    if (g.ui_backplate) return g.ui_backplate;
    /* pos3, nrm3, uv2, bone(u32), tex(u32 = none) per vertex */
    static const float kQuad[4][3] = {
        { -2000.0f, -2000.0f, 400.0f }, {  2000.0f, -2000.0f, 400.0f },
        {  2000.0f,  2000.0f, 400.0f }, { -2000.0f,  2000.0f, 400.0f }
    };
    float v[4 * 10];
    memset(v, 0, sizeof v);
    for (int i = 0; i < 4; i++) {
        float *p = v + i * 10;
        p[0] = kQuad[i][0]; p[1] = kQuad[i][1]; p[2] = kQuad[i][2];
        p[5] = -1.0f;                      /* normal: toward the camera */
        uint32_t bone = 0, tex = EM_MODEL_NO_TEX;
        memcpy(p + 8, &bone, 4);
        memcpy(p + 9, &tex, 4);
    }
    static const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    g.ui_backplate = em_gfx_mesh_create(gfx, v, 4, idx, 6, NULL, 0,
                                        NULL, 0);
    return g.ui_backplate;
}

/* Render one frame of the UI scene (call in place of the world flush).
 * State is private (ui_* fields) and re-initializes on every visible
 * edge — the engine re-allocates the menu actor at every open (state 0
 * init: yaw = pi, ramp = 1.0, clip by displayed health), so EM_HUD_FORCE
 * captures sample a FIXED spin phase: yaw = pi + 0.01 * (frames since
 * the screen appeared), deterministic in headless runs. */
/* LIGHTING — per-actor rig composer (defined with the close-out flush
 * below; the menu turntable consumes it too). */
static void char_rig_build(EmGfxCharRig *out, const float anchor[3],
                           int cam_fill);

static void ui_scene_render(EmGfx *gfx)
{
    if (!g.ui_prev) {                    /* open edge = engine state 0 */
        g.ui_yaw      = EM_PI;
        g.ui_t        = 0.0;
        g.ui_ramp     = 1.0f;
        g.ui_ramp_dir = 0;
        g.ui_low      = g.status.health <= UI_LOW_HEALTH;
    }

    /* Clip select (engine: init picks by displayed health; recovery
     * past 35 swaps back to the stance; never TO the low clip
     * mid-open). The port reads the status target — the engine's
     * display copy only lags it during the open count-up. */
    if (g.ui_low && g.status.health > UI_LOW_HEALTH) {
        g.ui_low = 0;
        g.ui_t   = 0.0;
    }
    int clip = g.ui_low ? g.clip_menu_low : g.clip_menu;
    if (clip < 0) clip = g.clip_menu >= 0 ? g.clip_menu : g.clip_idle;

    /* Menu pose (never touches the gameplay palette; under EM_HUD_FORCE
     * the world keeps simulating its own pose untouched underneath).
     * The player stands at the WORLD ORIGIN facing the camera, spinning. */
    em_model_palette_at(&g.model, (uint32_t)clip, g.ui_t, g.ui_palette);
    {
        const float pos[3] = { 0.0f, 0.0f, 0.0f };
        /* yaw 0 of the spin = facing the camera (the engine's pi init
         * under its identity camera + publisher flip). */
        float yaw0 = atan2f(UI_CAM_DIR_X, UI_CAM_DIR_Z);
        palette_apply_placement(g.ui_palette, g.model.bone_count,
                                pos, yaw0 + (g.ui_yaw - EM_PI));
    }

    /* The UI camera. The ENGINE uses the raw identity camera; the port
     * keeps the identical RELATIVE transform (the decoded view-space
     * (7.4, 2.4 down, 40)) but orbits the rig to the gfx layer's fixed
     * directional stand-in light azimuth (shader L = (0.4, 0.8, 0.45);
     * src/gfx is the renderer's lighting model, not the engine's) so
     * the model's camera-facing side is the LIT side — under the raw
     * identity camera the stand-in light leaves the menu player at the
     * 0.30 ambient floor, near-black on the black UI frame. Screen
     * framing is unchanged: eye = dir*Z - right*X + (0, -Y, 0), so the
     * player still projects at the decoded view-space offsets. */
    float view[16], proj[16], vp[16], vp_plate[16];
    float rig_eye[3], rig_fwd[3];        /* kept for the headlight below */
    {
        const float dir[3] = { UI_CAM_DIR_X, 0.0f, UI_CAM_DIR_Z };
        const float fwd[3] = { -dir[0], 0.0f, -dir[2] };
        const float up[3]  = { 0.0f, -1.0f, 0.0f };
        /* s = fwd x up (the lookat's view-right basis; up = (0,-1,0)
         * makes s = (fwd.z, 0, -fwd.x)) */
        const float s_[3]  = { fwd[2], 0.0f, -fwd[0] };
        const float eye[3] = {
            dir[0] * UI_SCENE_Z - s_[0] * UI_SCENE_X,
            -UI_SCENE_Y,
            dir[2] * UI_SCENE_Z - s_[2] * UI_SCENE_X
        };
        em_mat4_lookat_gs(view, eye, fwd, up);
        memcpy(rig_eye, eye, sizeof rig_eye);
        memcpy(rig_fwd, fwd, sizeof rig_fwd);
    }
    /* UI projection = THE ENGINE'S WORLD P AT s = 480 (s66 live read
     * closes the s49/s59 open item: ctx+0x2468 stays 480 with the hub
     * open; V is identity-with-y-flip; no separate menu projection
     * exists). em_mat4_perspective_gs carries the 10/7 pixel-aspect
     * anisotropy and the decoded near/far — the menu model is now
     * projected EXACTLY like the world. The screen framing the old
     * 0.74 pin validated is preserved by the re-derived UI_SCENE_*
     * placement (the constants block doc). Anchors stay registered to
     * the letterboxed 4:3 game frame at any window size (the gfx
     * backend letterboxes every draw — em_gfx_begin_frame — the same
     * region the overlay's 512x448 canvas maps). */
    em_mat4_perspective_gs(proj, UI_SCENE_ZOOM);
    em_mat4_mul(vp, proj, view);
    /* The black backplate keeps its own fixed straight-ahead camera (its
     * quad lives at world z 400): identical full-screen result, no need
     * to re-orient the quad with the rig. */
    {
        const float eye[3] = { 0.0f, 0.0f, 0.0f };
        const float fwd[3] = { 0.0f, 0.0f, 1.0f };
        const float up[3]  = { 0.0f, -1.0f, 0.0f };
        em_mat4_lookat_gs(view, eye, fwd, up);
        em_mat4_mul(vp_plate, proj, view);
    }

    /* Black backplate first (the engine's empty UI frame), then the
     * player over it. NOTE: this makes the UI player the renderer's
     * "last skinned palette" — em_weapon's muzzle anchor reads a menu
     * pose next frame, which only matters in the untestable
     * FORCE+aiming combination. */
    {
        static const float kBlack[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        static const float kIdent[16] = { 1, 0, 0, 0, 0, 1, 0, 0,
                                          0, 0, 1, 0, 0, 0, 0, 1 };
        EmGfxMesh *bp = ui_backplate_ensure(gfx);
        if (bp)
            em_gfx_draw_skinned_tinted(gfx, bp, vp_plate, kIdent, 1, kBlack);
    }

    /* MENU-SCENE LIGHT — ENGINE-TRUE since the room-rig decode
     * (2026-06-11 s57; the camera-anchored spot fill of the first
     * fix is retired when the scene carries a rig).
     *
     * Engine truth (boot-ELF re-read): the menu player's draw class
     * 0xB (func_001CA5F0 -> func_001CB480) sets lighting-override
     * mode 2 — and func_001D89D0 special-cases only modes 1/3/4/5/6,
     * so mode 2 runs the NORMAL character light path: the CURRENT
     * ROOM's rig from D_00251C50 with the slot-0 camera light ZEROED
     * (the static menu actor never gets flag +0x2 bit 0x20 —
     * func_001AFF10 zeroes it, func_0020CDC0 never sets it). The
     * port now feeds exactly that: char_rig_build with cam_fill = 0
     * and no lamp fold (anchor NULL — the engine's menu actor sits
     * far from any room lamp). Without a scene rig (rig-less
     * manifest) the previous camera-fill spot stand-in still runs so
     * the turntable never regresses to black-on-black. */
    if (g.rig_on) {
        EmGfxCharRig rig;
        char_rig_build(&rig, NULL, 0);
        em_gfx_char_rig(gfx, &rig);
    } else {
        static const float kFill[3] = { 0.62f, 0.62f, 0.62f };
        em_gfx_spot_light(gfx, rig_eye, rig_fwd, kFill,
                          4000.0f, -2.0f, -3.0f);
    }
    {
        /* Infection tint pulse (engine +0x80 color delta, GS 128 base,
         * approximated multiplicatively; infection 0 = opaque white =
         * bit-identical to the untinted draw per the em_gfx contract). */
        float inf = g.status.infection;
        float dr = -0.8f * inf * g.ui_ramp;
        float dg = -1.0f * inf * g.ui_ramp;
        float db = -0.3f * inf * g.ui_ramp;
        if (dg < -127.0f) dg = -127.0f;   /* engine clamp (0xC2FE0000) */
        float tint[4] = { (128.0f + dr) / 128.0f,
                          (128.0f + dg) / 128.0f,
                          (128.0f + db) / 128.0f, 1.0f };
        for (int i = 0; i < 3; i++) {
            if (tint[i] < 0.0f) tint[i] = 0.0f;
            if (tint[i] > 1.0f) tint[i] = 1.0f;
        }
        em_gfx_draw_skinned_tinted(gfx, g.mesh, vp, g.ui_palette,
                                   g.model.bone_count, tint);
    }
    {
        /* Lights off for anything after the menu player (rig cleared,
         * spot rgb 0 adds exactly 0.0 — later draws stay bit-exact). */
        static const float kOff[3] = { 0.0f, 0.0f, 0.0f };
        em_gfx_char_rig(gfx, NULL);
        em_gfx_spot_light(gfx, rig_eye, rig_fwd, kOff,
                          4000.0f, -2.0f, -3.0f);
    }

    /* Advance (engine state 1): spin, pulse ramp, clip time. */
    g.ui_yaw += UI_SPIN_RATE;
    if (g.ui_yaw > EM_PI) g.ui_yaw -= 2.0f * EM_PI;
    if (g.ui_ramp_dir == 0) {
        g.ui_ramp += UI_RAMP_RATE;
        if (g.ui_ramp >= UI_RAMP_MAX) g.ui_ramp_dir = 1;
    } else {
        g.ui_ramp -= UI_RAMP_RATE;
        if (g.ui_ramp <= 1.0f) g.ui_ramp_dir = 0;
    }
    g.ui_t += 1.0;                       /* anim_advance_time(1.0) */
}

/* LIGHTING — compose one actor draw's CHARACTER LIGHT RIG: the native
 * func_001D89D0 normal path (decomp FINDINGS "PER-ROOM LIGHT RIGS
 * DECODED", 2026-06-11). Slots 1/2 = the scene rig's static world
 * directionals (D_00251C50 record, manifest `lightdir`). Slot 0 = the
 * CAMERA FILL when `cam_fill` is set — the actor flag +0x2 bit 0x20
 * (func_001D8BF0): the PLAYER gets it once at init (func_001AF5C0)
 * and the decoded NPC spawners set it on humans; NO decoded enemy/
 * prop/door spawner sets it, so those draws run fill-less (flag clear
 * -> slot 0 dir AND color zeroed, func_001D8340). The fill direction
 * is the rig's CAMERA-SPACE vector rotated into world through the
 * TRANSPOSED view rotation (func_001D8340 i==0: copy the lookat
 * D_00810610, func_00102798 transpose, apply w=0) — the port rebuilds
 * the ENGINE view basis from cam fwd/up (em_mat4_lookat_gs negates
 * its rows for Metal NDC, so the basis is re-derived, not read back).
 *
 * Then the DYNAMIC POINT-LIGHT FOLD (func_001D8340 tail; gate
 * func_001D8270 excludes a fixed type list + large models — the port
 * applies the fold to every actor draw; `anchor` = the draw's bone-0
 * world translation, the engine's node light-reference column +0xC0):
 *   k    = 0.1 * I / max(|toLamp|^2, 1)    (UN-normalized offset —
 *                                           func_00102738 is a dot)
 *   dir0 = normalize(dir0*w0 + sum toLamp * 10k)
 *   col0 = col0 + sum lampcol * 2k         (x128 registration scale)
 * RE-VERIFIED against src/func_001D8340.c (BYTE-MATCHED — authoritative):
 * the 32-entry 0x80-stride scan at D_00275670, the `+0x24C > 0` weight
 * gate, `len = func_00102738(probe, probe)` (a DOT, so |toLamp|^2)
 * clamped up to 1.0f, `f = (0.1f * ent[+0x2C]) / len`, and the 10*f /
 * 2*f scales all read out literally. Two engine steps the port folds
 * away, both benign for the look but named here so nobody re-derives
 * them: (1) the two accumulators are SEEDED from the constant quads
 * D_00253170 / D_00253180 before dir0*w0 is added, not from zero; and
 * (2) the 10*f-scaled offset is transformed through the lamp's own
 * matrix at ent+0x40 (func_001026A0) before it is accumulated. The
 * cam-fill basis is confirmed a TRANSPOSE, not a general inverse:
 * `copy_qw4(im, D_00810610); func_00102798(im, im);` and func_00102798
 * is the 4x4 MMI transpose (src/func_00102798.c, BYTE-MATCHED).
 * Omitted, documented: the engine's per-lamp +-1.8 deg random-walk
 * flicker rotation (func_001D7C30 type-1 path, slot +0x40) and the
 * story-flag lamp gates (func_001F68B0) — lamps register
 * unconditionally. The actor RGB multiplier the engine folds into the
 * color rows (func_001D8690, actor +0x80) rides the port's per-draw
 * tint instead (same modulate, applied post-clamp).
 *
 * ROOM SELECTION: the engine keys the rig on (D_00810700<<8)|
 * D_00810701 — the AREA/SUB-STATE bytes, not spatial bounds. (One
 * more fold, named so nobody re-derives it: the engine's COLOR
 * accumulator is seeded by copying the live rig quad at +0xF0 over the
 * D_00253180 constant, so the constant seed is dead there — the port's
 * per-frame `sc = c0` start is the same thing for a rig that is
 * republished each frame. Audit 2026-07-31.) A
 * sub-state flip is a scene switch in the port (each exported scene
 * is one (area, sub) pair), so the active scene's rig IS the engine's
 * room selection; no player-position mapping exists or is invented. */
static void char_rig_build(EmGfxCharRig *out, const float anchor[3],
                           int cam_fill)
{
    memset(out, 0, sizeof *out);
    for (int s = 0; s < 2; s++) {
        for (int c = 0; c < 3; c++) {
            out->dir[s + 1][c] = g.rig_dir[s][c];
            out->col[s + 1][c] = g.rig_col[s][c];
        }
    }
    for (int c = 0; c < 3; c++)
        out->amb[c] = g.rig_amb[c];

    float d0[3] = { 0.0f, 0.0f, 0.0f };
    float c0[3] = { 0.0f, 0.0f, 0.0f };
    if (cam_fill) {
        /* engine view basis (em_math.h em_mat4_lookat_gs, un-negated):
         * sv = fwd x up_gs (view +x), uv = sv x fwd (view +y, world-
         * down), fwd (view +z); world fill = basis * camera-space dir. */
        const float *fw = g.cam.fwd, *up = g.cam.up;
        float sv[3] = { fw[1] * up[2] - fw[2] * up[1],
                        fw[2] * up[0] - fw[0] * up[2],
                        fw[0] * up[1] - fw[1] * up[0] };
        float sl = sqrtf(sv[0] * sv[0] + sv[1] * sv[1] + sv[2] * sv[2]);
        if (sl > 1e-6f) { sv[0] /= sl; sv[1] /= sl; sv[2] /= sl; }
        float uv[3] = { sv[1] * fw[2] - sv[2] * fw[1],
                        sv[2] * fw[0] - sv[0] * fw[2],
                        sv[0] * fw[1] - sv[1] * fw[0] };
        for (int c = 0; c < 3; c++) {
            d0[c] = sv[c] * g.rig_cam_dir[0] + uv[c] * g.rig_cam_dir[1]
                  + fw[c] * g.rig_cam_dir[2];
            c0[c] = g.rig_cam_col[c];
        }
    }
    if (anchor && g.n_lamp) {
        float sd[3] = { d0[0] * g.rig_cam_w, d0[1] * g.rig_cam_w,
                        d0[2] * g.rig_cam_w };
        float sc[3] = { c0[0], c0[1], c0[2] };
        for (int i = 0; i < g.n_lamp; i++) {
            float toL[3] = { g.lamp[i].pos[0] - anchor[0],
                             g.lamp[i].pos[1] - anchor[1],
                             g.lamp[i].pos[2] - anchor[2] };
            float d2 = toL[0] * toL[0] + toL[1] * toL[1]
                     + toL[2] * toL[2];
            if (d2 < 1.0f) d2 = 1.0f;
            float k = 0.1f * g.lamp[i].inten / d2;
            for (int c = 0; c < 3; c++) {
                sd[c] += toL[c] * (10.0f * k);
                sc[c] += g.lamp[i].col[c] * (2.0f * k);
            }
        }
        float sl = sqrtf(sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2]);
        if (sl > 1e-6f) {
            d0[0] = sd[0] / sl; d0[1] = sd[1] / sl; d0[2] = sd[2] / sl;
        }
        c0[0] = sc[0]; c0[1] = sc[1]; c0[2] = sc[2];
    }
    for (int c = 0; c < 3; c++) {
        out->dir[0][c] = d0[c];
        out->col[0][c] = c0[c];
    }
}

/* func_001CB5A0 / func_001AAD00 / func_001D1EA0(1) — close-out: flush the
 * recorded chain with the camera block applied (the native "kick"), then
 * advance clip time. EM_CAPTURE instrumentation lives here so its frame
 * counting matches the rendered gameplay frames. */
static void frame_close_out(void)
{
    EmGfx *gfx = em_frame_gfx();

    /* MENU-RENDER GATE: the UI-camera 3D scene replaces the world
     * flush while the status screen shows (visible = the real toggle
     * OR the EM_HUD_FORCE capture hook) and the real background will
     * draw over it. Without the player asset or the ui.emui backdrop
     * the old path runs unchanged (asset-absent frames byte-identical). */
    int ui_scene = g.mesh && em_hud_visible() && em_hud_backdrop_ready(gfx);

    if (ui_scene) {
        ui_scene_render(gfx);
    } else if (g.chain_test_triangle) {
        em_gfx_draw_test_triangle(gfx);
    } else {
        /* LIGHTING — DISTANCE FOG for the world flush. Per-frame, per-
         * scene constant (the engine's per-area GS fog record), so set
         * once for the whole chain: it tints BOTH the LEVEL meshes and
         * the actor draws toward the fog color with view-space depth.
         * fog_on = 0 (no `fog` line, e.g. office/drawbridge) leaves it
         * OFF — those scenes render exactly as before. */
        if (g.fog_on)
            em_gfx_fog(gfx, g.fog_near, g.fog_far, g.fog_rgb);
        else
            em_gfx_fog_off(gfx);
        for (int i = 0; i < g.chain_len; i++) {
            const ChainDraw *cd = &g.chain[i];
            /* LIGHTING — actor draws (everything after the scene
             * meshes: doors, enemies, player — the chain-build order)
             * take a freshly composed character rig, the engine's
             * per-actor light-matrix rebuild (func_001D89D0 per draw
             * publish). Anchor = the palette's bone-0 world
             * translation (column-major [12..14] — the engine's node
             * light-reference column +0xC0); the camera fill is the
             * PLAYER draw's alone (decoded gating, char_rig_build).
             * Scene meshes draw rig-less: the LEVEL path ignores the
             * rig anyway (baked vertex color — engine truth). */
            if (g.rig_on && i >= g.n_scene) {
                EmGfxCharRig rig;
                const float anchor[3] = { cd->palette[12],
                                          cd->palette[13],
                                          cd->palette[14] };
                char_rig_build(&rig, anchor,
                               cd->palette == g.player_palette);
                em_gfx_char_rig(gfx, &rig);
            } else {
                em_gfx_char_rig(gfx, NULL);
            }
            if (cd->tint)         /* per-draw RGBA modulate (ChainDraw) */
                em_gfx_draw_skinned_tinted(gfx, cd->mesh, g.viewproj,
                                           cd->palette, cd->bone_count,
                                           cd->tint);
            else
                em_gfx_draw_skinned(gfx, cd->mesh, g.viewproj,
                                    cd->palette, cd->bone_count);
        }
        em_gfx_char_rig(gfx, NULL);   /* LIGHTING — rig is per draw */
        em_gfx_fog_off(gfx);          /* LIGHTING — fog off after the world flush */
    }
    g.ui_prev = ui_scene;     /* edge tracking for the scene re-init */
    em_hud_scene_3d(ui_scene);  /* background skips its base fill */

    /* AREA-11 STEAM PUFF FX (record 7 — the cosmetic billboard plume;
     * INVESTIGATION_first_level_area11.md §3/§6). Queued only over the
     * WORLD flush (not the status-screen UI scene); the beam pass needs a
     * 3D draw, which the world flush above provided. No-op without a
     * `steam` line (other scenes untouched). */
    if (!ui_scene)
        steam_fx_render(gfx);

    /* Weapon feedback overlays (crosshair / muzzle-flash placeholders —
     * em_weapon.h "VISUAL FEEDBACK"); queues nothing while holstered, so
     * the default frame stays byte-identical. */
    em_weapon_render(gfx);

    /* STATUS SCREEN over the flushed 3D frame (the engine's GS-sprite
     * status overlay; em_hud queues overlay rects, em_gfx_end_frame
     * draws them last). HIDDEN by default — the original shows no
     * persistent HUD — and toggled by a TRIANGLE/START press (edge);
     * shown, the 3D frame underneath is the UI scene above (or the
     * flagged dim fallback without assets) and the world simulation is
     * paused by the gameplay_frame gate. EM_HUD_FORCE=1 forces it
     * visible for overlay tests. The ammo readout is LIVE: mag/reserve
     * mirror the weapon state (D_00810C62 / D_00810CB4) every frame,
     * exactly like the engine UI re-reading the globals. */
    /* MENU INHIBIT (engine D_008106B3, written every frame by the
     * player spine: nonzero while hit-reacting/dying — and at the
     * game-over screen, where START means restart): the open press is
     * dropped inside em_hud_update. */
    em_hud_menu_inhibit(player_damage_locked() ||
                        em_examine_input_locked());  /* examine = the
                                  * op07 scripted-mode window — the
                                  * engine's menu poll never runs while
                                  * spad 3B8D owns the frame */
    em_hud_update(em_frame_input());
    g.status.mag     = em_weapon_mag();
    g.status.reserve = em_weapon_reserve();
    g.status.items   = em_pickup_items();   /* the D_00810C64 mirror —
                                             * the ITEM page's real
                                             * per-type counts */
    em_hud_render(gfx, &g.status);
    em_hud_found_render(gfx);   /* transient "Found: <name>" line over
                                 * gameplay (hidden while the menu is
                                 * open) — the pickup decode's flagged
                                 * status-auto-open stand-in */
    em_examine_render(gfx);     /* EXAMINE area-bank chain text — the
                                 * mode-2 presentation for lines the
                                 * global radio machine can't address
                                 * (GLOBAL lines drew inside em_hud
                                 * just above) */
    em_hud_area_title_render(gfx);  /* AREA-11 opening title card ("FORT
                                 * STEWART - REAR ENTRANCE") — one-shot,
                                 * fade-in/hold/fade-out, armed on AREA-11
                                 * scene entry (INVESTIGATION_area11_
                                 * director §4.4). Independent of the
                                 * cinematic director; no-op once finished
                                 * or in non-AREA-11 scenes. */

    /* AREA-11 OPENING DIRECTOR letterbox (the op07 ENTER / op18 EXIT
     * cinematic bars — INVESTIGATION_area11_director §4.3 / §E, s81
     * live capture: two full-width ~64-line alpha-blended black quads,
     * top + bottom of the frame, fade in/out). Drawn UNDER the screen
     * fade so the fade still owns the whole frame. Queues nothing
     * outside a beat (director_letterbox_alpha == 0), so every non-beat
     * frame stays byte-identical. FLAGGED: the exact opening staggered-
     * fade frame counts are un-stopwatched (doc §F) — CINE_BAR_FADE is
     * an approximation of the fade window. */
    {
        float ba = director_letterbox_alpha();
        if (ba > 0.0f) {
            const float bar[4] = { 0.0f, 0.0f, 0.0f, ba };
            em_gfx_overlay_rect(gfx, 0.0f, 0.0f,
                                EM_GFX_OVERLAY_W, CINE_BAR_LINES, bar);
            em_gfx_overlay_rect(gfx, 0.0f,
                                EM_GFX_OVERLAY_H - CINE_BAR_LINES,
                                EM_GFX_OVERLAY_W, CINE_BAR_LINES, bar);
        }
    }

    /* GAME-OVER / CONTINUE screens — drawn BEFORE the fade rect so
     * the fade machine owns them exactly like the engine (the screen
     * modules render under the GS fade; PD block doc). Their opaque
     * black base hides the frozen dead world underneath. Queues
     * nothing while the GO machine is off, so every other frame stays
     * byte-identical. Presentation = the FLAGGED module stand-ins
     * (em_hud.h): module 0x27 -> em_hud_game_over, module 1 ->
     * em_hud_continue (cursor highlight). */
    if (g.go_state == GO_SCREEN || g.go_state == GO_SCREEN_OUT)
        em_hud_game_over(gfx);
    else if (g.go_state >= GO_PROMPT)
        em_hud_continue(gfx, g.go_cursor);

    /* SCREEN FADE — the step-D machine, drawn last so it covers the
     * scene AND the status screen (the engine's fade owns the whole GS
     * frame). The engine blend is SUBTRACTIVE (out = max(0, pixel -
     * level), GS ALPHA_2 0xA1/FIX 0x80 — decoded 2026-06-11, see
     * em_frame.h): a GREY full-screen sprite with R=G=B=level is
     * subtracted from every frame pixel, so shadows crush to black
     * first and highlights survive longest. The overlay pass carries
     * that exact op (em_gfx_overlay_rect_sub: reverse-subtract,
     * ONE/ONE on RGB, flushed over the HUD) — the former black-quad
     * alpha approximation and its residual gap are gone. Level 0
     * queues nothing: the default frame stays byte-identical. */
    {
        float l = em_frame_fade_level();
        if (l > 0.0f) {
            const float grey[3] = { l, l, l };
            em_gfx_overlay_rect_sub(gfx, 0.0f, 0.0f, EM_GFX_OVERLAY_W,
                                    EM_GFX_OVERLAY_H, grey);
        }
    }

    if (g.capture_path && g.frame_no == g.capture_frame)
        em_gfx_request_capture(gfx, g.capture_path);

    /* EM_CAM_PRINT=1 — the settled camera-state witness (s76 idle-
     * emergence read): eye/target world position, the DERIVED struct
     * yaw (cam+0x44 = atan2(tgt - eye)), and the horizontal eye<->tgt
     * distance — printed at the capture frame for the A/B against the
     * live engine ground truth. */
    if (g.cam_print && g.frame_no == g.capture_frame) {
        const EmCamera *c = &g.cam;
        float hx = c->tgt[0] - c->eye[0];
        float hz = c->tgt[2] - c->eye[2];
        printf("cam eye (%.2f, %.2f, %.2f) tgt (%.2f, %.2f, %.2f) "
               "yaw %.3f dist %.2f  [player (%.2f, %.2f, %.2f) "
               "yaw %.4f | eye +%.1f tgt +%.1f over player]\n",
               c->eye[0], c->eye[1], c->eye[2],
               c->tgt[0], c->tgt[1], c->tgt[2],
               c->yaw, sqrtf(hx * hx + hz * hz),
               g.pos[0], g.pos[1], g.pos[2], g.yaw,
               c->eye[1] - g.pos[1], c->tgt[1] - g.pos[1]);
    }

    g.frame_no++;
    /* A scripted self-test owns the quit when combined with a capture,
     * so a mid-script capture doesn't cut the script short. */
    if (g.capture_path && !g.move_test && !g.weapon_test && !g.door_test &&
        !g.transit_test && !g.slider_test &&
        g.frame_no > g.capture_frame + 1)
        em_frame_request_quit();
}

/* EM_MOVE_TEST=1 — deterministic movement self-test. Injects a scripted
 * key sequence through the real em_input event API (an injected event
 * lands in the NEXT frame's input snapshot, exactly like a real key, so
 * the test exercises the full step-C unpack path): 'w' held for frames
 * 1..60 (walk forward, +Z at cam_yaw 0), 'd' for frames 61..90 (walk
 * screen-right, -X), then assert the final placement and quit.
 *
 * Keyboard full push = GAIT 3 (RUN, 48 u/s = 0.8 u/frame sustained —
 * the tier-ramp-corrected mapping, ANALOG GAIT above; the entry ramps
 * 0.3 -> 0.8 over 8 frames = 4.65 u, then 0.8/frame). The office
 * collision world has a wall n-gon at z = -170 (grid poly, plane
 * n = (0,0,-1), d = 170 — 14 u ahead of the spawn), so with collision
 * loaded the RADIAL WALL PROBES rest the player near the engine's
 * 4.5-unit standoff. The strafe leg then CURVES on TWO coupled counts:
 *   (1) the BODY-HEADING EASE (func_00174AC0/func_001B12B0): the leg-1
 *       -> leg-2 stick flip is a +90 deg desired-heading jump, but the
 *       body heading g.yaw turns into it at the engine's banded rate
 *       (run far-band 10.5 deg/f) and VELOCITY IS EMITTED ALONG g.yaw,
 *       so the path arcs over ~9 frames instead of sliding instantly
 *       off the raw stick — this is the user-reported fix;
 *   (2) the s65 WALK-STATE CAMERA: the heading basis (cam+0x44) is an
 *       OUTPUT of the eye tether — strafing rotates the camera bearing
 *       as the dragged eye trails the path, so the camera-relative
 *       DESIRED heading itself keeps swinging (the engine's emergent
 *       chase-camera spiral). The two compound: the final yaw overshoots
 *       -pi/2 to ~-1.93 because the rotating camera bearing pushes the
 *       desired heading further around while the body chases it.
 * Deterministic endpoints (no RNG in the camera or mover).
 * RE-PINNED + RE-ARMED 2026-06-17 (BYTE-FAITHFUL movement restore): the
 * desired heading is now the disasm-exact closed form
 * atan2(sx,-sy) + player_move_cam_yaw() + pi/2 (= wrap(stickAngle + pi +
 * D_008106A0); INVESTIGATION_movement_exact.md "FORWARD-DIRECTION
 * RESOLUTION (definitive, static)"). That is +90 deg off the prior
 * X-mirrored door-regression form, so both legs walk a new heading and
 * the default office scene (spawn 107.4,0,-184 yaw 0, office.emcl, legs
 * 60/30) settles at a NEW deterministic wall-stop endpoint pos
 * (107.404, 0.000, -205.266) yaw -3.1416 [wall stop] — captured from a
 * clean post-restore run and verified IDENTICAL across two consecutive
 * runs. This is the pinned collision-world baseline; the assertion is
 * ARMED (the default office run asserts strictly — a movement regression
 * FAILS it). Those built-in expectations (and the 60/30-frame legs) are
 * the OFFICE scene's; for other scenes (manifest spawns) EM_MOVE_LEGS=
 * fwd,strafe resizes the two legs to reach that scene's wall and
 * EM_MOVE_EXPECT=x,y,z + EM_MOVE_YAW=<rad> override the expected final
 * placement. */
static void move_test_inject(int key, int down)
{
    EmEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.type = down ? EM_EVENT_KEY_DOWN : EM_EVENT_KEY_UP;
    ev.key  = key;
    em_input_handle_event(&ev);
}

static void move_test_script(void)
{
    int n = g.frame_no;
    if (n == 0) {
        move_test_inject('w', 1);
    } else if (n == g.move_legs[0]) {
        move_test_inject('w', 0);
        move_test_inject('d', 1);
    } else if (n == g.move_legs[0] + g.move_legs[1]) {
        move_test_inject('d', 0);
    } else if (n == g.move_legs[0] + g.move_legs[1] + 1) {
        /* RE-PINNED + RE-ARMED 2026-06-17 (BYTE-FAITHFUL movement restore,
         * INVESTIGATION_movement_exact.md "FORWARD-DIRECTION RESOLUTION
         * (definitive, static)"). The desired-heading basis was restored to
         * the disasm-exact closed form
         *     desired = atan2(sx,-sy) + player_move_cam_yaw() + pi/2
         * (= wrap(stickAngle + pi + D_008106A0), D_008106A0 =
         * atan2(-fwd.z,fwd.x)). This is +90 deg off the prior X-mirrored
         * "door-regression" form (player_move_cam_yaw() + atan2(-sx,-sy)),
         * so BOTH move-test legs walk a new heading: the office spawn
         * (107.4,0,-184, yaw 0, office.emcl, legs 60/30) now walks 'w' -X-
         * relative-to-the-+90 basis then 'd', settling at the deterministic
         * wall-stop endpoint pinned below — captured from a clean post-
         * restore run and verified IDENTICAL across two consecutive runs
         * (107.404, 0.000, -205.266) yaw -3.1416. FIX-2 (tier-0 ramp) and
         * FIX-3 (multi-frame turn-in-place) are untouched; only the
         * desired-heading basis changed, which is what moved this endpoint.
         * The collision-world default ASSERTS strictly against this pin (a
         * movement regression FAILS loudly — it is NOT rebaseline-always-
         * pass). The bbox (no-collision) branch is not reached by any
         * default scene (the office always loads office.emcl), so its
         * literals stay a soft report-only reference. */
        float ex   = g.coll.poly_count ?  107.404f :   86.025f;
        float ey   = g.coll.poly_count ?    0.000f :    0.000f;
        float ez   = g.coll.poly_count ? -205.266f : -135.887f;
        float eyaw = g.coll.poly_count ?  -3.1416f :  -1.9032f;
        if (g.move_expect_set) {
            ex = g.move_expect[0];
            ey = g.move_expect[1];
            ez = g.move_expect[2];
        }
        {
            const char *my = getenv("EM_MOVE_YAW");
            if (my) eyaw = (float)atof(my);
        }
        const float tol  = 0.05f;  /* +- slide/fp drift allowance */
        /* RE-ARMED 2026-06-17: the collision-world default (the office
         * scene, which always loads office.emcl -> poly_count > 0) and any
         * explicit EM_MOVE_EXPECT override ASSERT strictly against the
         * pinned endpoint above — a movement regression FAILS the test
         * loudly. Only the bbox (no-collision) branch, which no default
         * scene reaches, stays report-only (its literals are stale). */
        int report_only = (g.coll.poly_count == 0) && !g.move_expect_set;
        int ok = report_only ? 1 :
                 (fabsf(g.pos[0] - ex)           <= tol &&
                  fabsf(g.pos[1] - ey)           <= tol &&
                  fabsf(g.pos[2] - ez)           <= tol &&
                  fabsf(g.yaw - eyaw)            <= 0.01f);
        printf("move test: pos (%.3f, %.3f, %.3f) yaw %.4f rad — "
               "expected (%.3f, %.3f, %.3f)%s%s: %s\n",
               g.pos[0], g.pos[1], g.pos[2], g.yaw, ex, ey, ez,
               g.coll.poly_count ? " [wall stop]" : " [bbox clamp]",
               report_only ? " [bbox report-only baseline]" : "",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_DOOR_TEST=1 — deterministic door-transit self-test. Spawns the
 * player on the z = -225 corridor line facing the WEST double door
 * (hinge/placement at (57, 0, -220.5), record [5] AREA02 state 1;
 * DOORWAY CENTER = hinge + 5 along the panel = (57, 0, -225.5) — the
 * decoded func_00183EF0 kind-5 reference point, 2026-06-11).
 * CITATION TIGHTENED (audit 2026-07-31): that 5-unit offset is real
 * but it is NOT the `+8 == 5` arm. src/func_00183EF0.c (NEARMISS)
 * puts it in the `+8 == 0` arm, under `kind == 5 && (sub == 3 ||
 * sub == 0x15)`, as `ref = (obj.x - 5*cos(yaw), obj.z + 5*sin(yaw))`,
 * range-tested against rec[0] with |dy| <= rec[1] from the +0x30
 * tuning record. The `+8 == 5` arm is a plain 14-u radius test
 * (`dx*dx + dz*dz <= 196.0f`) with |dy| <= 4.0f and NO panel offset.)
 * Exercises the FULL s22 transit sequence end to end through the real
 * input API:
 *
 *   frames  1..24   run -X (full push = gait 3; the tier ramp tops
 *                   out at 0.8 u/frame) to the boundary-wall standoff
 *                   x ~= 64.5 (inside the 10 u use-scan radius measured
 *                   from the CENTER; no button — the door must stay
 *                   CLOSED. Engine-true twice over: the kind-5 scan
 *                   has no auto ring, and the scan itself only runs on
 *                   the USE press edge — there is NO walk-into door
 *                   trigger at all, s58)
 *   frames 29..58   keep running -X. The doorway is statically SEALED by
 *                   the grid room-boundary plane at x = 60 (the engine's
 *                   sealed-room-box world): the radial wall probes must
 *                   BLOCK free movement at the player's 4.5-unit radius,
 *                   x ~= 64.5 (tracked as dt_min_x while CLOSED) — the
 *                   "previously blocked plane".
 *   frame   60      CROSS press — the engine trigger (the use scan runs
 *                   on the press edge D_00810E74 & spad-3B76 = 0x0040,
 *                   s58) -> arms the door (dist to
 *                   the center ~7.5; facing -X = the back-side pi/4 yaw
 *                   gate passes at 0); assert state == OPENING soon
 *                   after. The kickoff LOCKS input, latches the side
 *                   (the test approach is the BACK side: bearing(player
 *                   - door) is pi off the door yaw) and WALKS the player
 *                   to the staging point (62, 0, -225.5) = CENTER + 5*n
 *                   on his own side — the s22 live capture's EXACT point
 *                   (arrival ~frame 80; the walk keeps the locomotion
 *                   walk anim).
 *   ~frame  80      OPEN phase: the door script chain fires on arrival
 *                   — scripted player anim 0x43 (door-open BACK, rate
 *                   1.0; after the 2026-06-11 clip-directory fix this IS
 *                   the open clip — reach, push, walk-through — the old
 *                   asset shipped the LOCKED try under this id) replaces
 *                   locomotion, door sound + clip start; the script
 *                   waits 70 frames (back-side op 0x02).
 *   frame  100      assert em_door_movement_locked() == 1 (mid-transit)
 *                   AND em_game_anim_active() == 0x43;
 *   100..140        hold forward ('w') — locked input must NOT move the
 *                   player off the staging point (checked at 145).
 *   ~frame 150      commit (arrival ~80 + 70-frame wait) -> 64-frame
 *                   fade-out (peak level tracked); at black (~205):
 *                   re-place at the spawn point (52, 0, -225.5) =
 *                   CENTER - 5*n, exit yaw -pi/2, scripted anim
 *                   CANCELLED (script teardown), and the ARRIVAL
 *                   WALK-OUT starts (player state 5/1: 50 frames
 *                   clip-in-place + 30 at 0.3 u/tick + 30 decaying,
 *                   ~13.1 u along -X); door closes; 64-frame fade-in
 *                   (done ~269). MENU lock ends at fade-in completion;
 *                   MOVEMENT lock at walk-out end (~317) — the decoded
 *                   two-lock split (em_door.h "THE TWO LOCKS").
 *   frame  290      assert MID-WALK-OUT: menu lock OFF, movement lock
 *                   ON, player mid-flight (39.3 < x < 51.5) — the split
 *                   witnessed live.
 *   frame  340      assert: trigger OK, blocked min x >= 64.4 (the
 *                   boundary plane + the 4.5 player radius), lock
 *                   seen, locked input dead, scripted anim seen at 100
 *                   and idle (0) again at the end, fade reached 1.0 and
 *                   is back at 0, MOVEMENT unlocked (walk-out over),
 *                   final pos = spawn - walk-out travel (38.89,
 *                   -225.5) +- tol with yaw -pi/2 (walked out of the
 *                   door, exit pose), door re-armed CLOSED.
 *
 * (Geometry verified against the office EMCL: corridor floor along the
 * whole approach, boundary wall at x = 60, no other static blocker.) */
static void door_test_script(void)
{
    static int s_dt_ok_split;        /* frame-290 two-lock witness */
    int n = g.frame_no;
    if (n == 0) {
        s_dt_ok_split     = 0;
        g.dt_door         = -1;
        g.dt_min_x        = 1e9f;
        g.dt_max_fade     = 0.0f;
        g.dt_ok_trigger   = 0;
        g.dt_ok_lock      = 0;
        g.dt_ok_inputdead = 0;
        g.dt_ok_anim      = 0;
        /* WALK-UP under the BYTE-FAITHFUL movement basis (restored 2026-06-17,
         * INVESTIGATION_movement_exact.md "FORWARD-DIRECTION RESOLUTION").
         * Spawn is (72,0,-225) facing -X (yaw -pi/2); the door is straight
         * -X. The chase camera sits behind the player looking -X, so the
         * move basis player_move_cam_yaw() = atan2(fwd.x,fwd.z) ~= -pi/2.
         * The faithful desired = atan2(sx,-sy) + cam_yaw + pi/2, so to head
         * -X (desired = -pi/2) we need atan2(sx,-sy) = -pi/2, i.e. the LEFT
         * stick (sx=-1, sy=0) = the 'a' key — NOT 'w'. (Under the faithful
         * +90 deg rotation, forward-press 'w' walks 90 deg off the camera
         * look; 'a' = "screen-into-the-look", which is straight at the door
         * here.) The camera stays looking -X as the player walks straight
         * down the corridor, so 'a' holds desired = -pi/2 self-consistently
         * and the player reaches the doorway boundary, exactly as the old
         * 'w' did under the (wrong) pre-faithful basis. The scripted door
         * CROSSING itself ignores the stick (selector-3 0x70003B8D gate),
         * so only this walk-up input needed the basis correction. */
        move_test_inject('a', 1);
    } else if (n == 24) {
        move_test_inject('a', 0);
        /* the test door = nearest instance to the west doorway */
        float best = 1e30f;
        for (int i = 0; i < em_door_count(); i++) {
            float p[3];
            em_door_pos(i, p);
            float dx = p[0] - 57.0f, dz = p[2] + 220.5f;
            float d2 = dx * dx + dz * dz;
            if (d2 < best) { best = d2; g.dt_door = i; }
        }
    } else if (n == 28) {
        move_test_inject('a', 1);   /* push -X into the sealed doorway
                                     * (faithful basis: 'a' = at the door) */
    } else if (n == 58) {
        move_test_inject('a', 0);   /* ~6 frames pinned on the boundary */
    } else if (n == 60) {
        move_test_inject('k', 1);   /* CROSS — the use-scan trigger */
    } else if (n == 61) {
        move_test_inject('k', 0);
    } else if (n == 64) {
        g.dt_ok_trigger = g.dt_door >= 0 &&
                          em_door_state(g.dt_door) == EM_DOOR_OPENING;
    } else if (n == 100) {
        g.dt_ok_lock = em_door_movement_locked();
        /* Mid-open: the scripted door anim must own the player — the
         * BACK-side clip 0x43 committed (+0x20C analog), not the walk. */
        g.dt_ok_anim = em_game_anim_active() == 0x43;
        move_test_inject('w', 1);   /* locked: must be ignored */
    } else if (n == 140) {
        move_test_inject('w', 0);
    } else if (n == 145) {
        /* 40 frames of held forward under the lock: still parked on the
         * staging point (62, 0, -225.5) — CENTER + 5*n, the s22
         * live-captured value. */
        g.dt_ok_inputdead = fabsf(g.pos[0] - 62.0f)   <= 0.35f &&
                            fabsf(g.pos[2] + 225.5f) <= 0.35f;
        if (!g.dt_ok_inputdead)
            printf("door test: frame 145 pos (%.3f, %.3f, %.3f) — off "
                   "the staging point\n", g.pos[0], g.pos[1], g.pos[2]);
    } else if (n == 290) {
        /* MID-WALK-OUT (re-place ~205, fade-in done ~269, walk-out ends
         * ~317): the decoded TWO-LOCK SPLIT — the menu gate is already
         * open while movement is still script-owned and the player is
         * mid-flight between the spawn (52) and the stop (~38.9). */
        s_dt_ok_split = !em_door_menu_locked() &&
                        em_door_movement_locked() &&
                        g.pos[0] < 51.5f && g.pos[0] > 39.3f;
        if (!s_dt_ok_split)
            printf("door test: frame 290 menu_locked %d movement_locked "
                   "%d x %.3f — two-lock split wrong\n",
                   em_door_menu_locked(), em_door_movement_locked(),
                   g.pos[0]);
    } else if (n == 340) {
        int ok_closed = g.dt_door >= 0 &&
                        em_door_state(g.dt_door) == EM_DOOR_CLOSED;
        int ok_block  = g.dt_min_x >= 64.4f && g.dt_min_x < 65.2f;
        int ok_fade   = g.dt_max_fade >= 0.999f &&
                        em_frame_fade_level() <= 0.001f;
        int ok_unlock = !em_door_movement_locked();
        int ok_animend = em_game_anim_active() == 0;  /* teardown reset */
        /* Final pos = the spawn point (52, -225.5) MINUS the arrival
         * WALK-OUT travel (~13.1 u along the exit yaw -X — the decoded
         * player state 5/1 walk-out, em_door.h "ARRIVAL WALK-OUT"). */
        int ok_pass   = fabsf(g.pos[0] - 38.89f)  <= 0.35f &&
                        fabsf(g.pos[2] + 225.5f)  <= 0.6f &&
                        fabsf(g.yaw + EM_PI * 0.5f) <= 0.01f;
        int ok = g.dt_ok_trigger && g.dt_ok_lock && g.dt_ok_inputdead &&
                 g.dt_ok_anim && ok_animend && s_dt_ok_split &&
                 ok_fade && ok_unlock && ok_pass && ok_block && ok_closed;
        printf("door test: trigger->OPENING %s, blocked min x %.3f while "
               "closed (boundary 60 + radius 4.5: %s), input locked "
               "mid-transit %s, "
               "locked stick ignored %s, scripted anim 0x43 mid-open %s / "
               "reset at end %s, two-lock split mid-walk-out %s, fade "
               "peak %.3f / final %.3f: %s, "
               "movement unlocked at end %s, final pos (%.3f, %.3f, %.3f) "
               "yaw %.4f walked out of the door: %s, door state %d "
               "(CLOSED %d, re-armed): %s — %s\n",
               g.dt_ok_trigger ? "ok" : "FAILED", g.dt_min_x,
               ok_block ? "ok" : "FAILED",
               g.dt_ok_lock ? "ok" : "FAILED",
               g.dt_ok_inputdead ? "ok" : "FAILED",
               g.dt_ok_anim ? "ok" : "FAILED",
               ok_animend ? "ok" : "FAILED",
               s_dt_ok_split ? "ok" : "FAILED",
               g.dt_max_fade, em_frame_fade_level(),
               ok_fade ? "ok" : "FAILED",
               ok_unlock ? "ok" : "FAILED",
               g.pos[0], g.pos[1], g.pos[2], g.yaw,
               ok_pass ? "ok" : "FAILED",
               g.dt_door >= 0 ? em_door_state(g.dt_door) : -1,
               EM_DOOR_CLOSED, ok_closed ? "ok" : "FAILED",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
    /* Track how far -X free movement reaches while the door is CLOSED
     * (the boundary plane must hold the player at ~60.01), and the fade
     * peak (the fade-out must reach full black before the re-place). */
    if (n > 0 && n <= 60 && g.dt_door >= 0 &&
        em_door_state(g.dt_door) == EM_DOOR_CLOSED &&
        g.pos[0] < g.dt_min_x)
        g.dt_min_x = g.pos[0];
    if (em_frame_fade_level() > g.dt_max_fade)
        g.dt_max_fade = em_frame_fade_level();
}

/* EM_TRANSIT_TEST=1 — deterministic SCENE-SWITCH transit self-test.
 * Exercises the goto-door path end to end: the office scene's west door
 * (hinge (57, 0, -220.5), doorway CENTER (57, 0, -225.5)) carries the
 * manifest goto tail written by the decomp repo's export_level.py
 * --door-goto — since 2026-06-11 the REAL decoded destination: door id
 * 1|0x80 -> AREA01 sub 0 entry 5 = the DRAWBRIDGE ROOM (chunk05.n0,
 * exported as assets/scene_drawbridge; the old flagged synthetic
 * west<->m15 link is gone). Arrival spawn = the real AREA01 spawn
 * record (39, 0, -225) yaw -pi/2 — the engine's own west-door arrival.
 *
 *   frame    0      spawn (66, 0, -225.5) facing -X — on the doorway
 *                   center line, dist 9 of the 10-u scan radius
 *   frame    5      CROSS -> use scan arms the goto door; kickoff locks
 *                   input, walks to staging (62, -225.5) = CENTER + 5*n
 *                   (the s22 live-captured point), open script (back
 *                   side: anim 0x43, 70-frame wait), commit, 64-frame
 *                   fade-out
 *   at black        em_door posts the GOTO; em_game_scene_switch frees
 *                   the office sub-1 scene and loads scene_drawbridge
 *                   (11 zone parts, drawbridge.emcl, its 6 doors, its
 *                   enemies); player placed at (39, 0, -225) yaw -pi/2;
 *                   fade-in
 *   frame  380      assert: trigger OK, locked mid-transit, the ACTIVE
 *                   SCENE changed to assets/scene_drawbridge (switch
 *                   frame recorded), player at the arrival spawn, fade
 *                   peaked 1.0 and back at 0, input unlocked, the NEW
 *                   scene's 6 doors present + CLOSED, collision =
 *                   drawbridge.emcl (loaded), enemies of the new scene
 *                   populated, and the player model still owns its
 *                   palette (persists).
 */
static void transit_test_script(void)
{
    int n = g.frame_no;
    if (n == 0) {
        g.tt_door         = -1;
        g.tt_ok_trigger   = 0;
        g.tt_ok_lock      = 0;
        g.tt_switch_frame = 0;
        g.tt_max_fade     = 0.0f;
    } else if (n == 3) {
        /* the goto door = nearest instance to the west doorway */
        float best = 1e30f;
        for (int i = 0; i < em_door_count(); i++) {
            float p[3];
            em_door_pos(i, p);
            float dx = p[0] - 57.0f, dz = p[2] + 220.5f;
            float d2 = dx * dx + dz * dz;
            if (d2 < best) { best = d2; g.tt_door = i; }
        }
    } else if (n == 5) {
        move_test_inject('k', 1);   /* CROSS — the use-scan trigger */
    } else if (n == 7) {
        move_test_inject('k', 0);
    } else if (n == 12) {
        g.tt_ok_trigger = g.tt_door >= 0 &&
                          em_door_state(g.tt_door) == EM_DOOR_OPENING;
    } else if (n == 150) {
        g.tt_ok_lock = em_door_movement_locked();   /* mid fade-out */
    } else if (n == 380) {
        int ok_scene  = g.tt_switch_frame > 0 &&
                        strcmp(g.scene_dir, "assets/scene_drawbridge") == 0;
        /* Arrival spawn (39, -225) MINUS the ~13.1 u WALK-OUT travel
         * along the exit yaw -X (the walk-out state survives the goto
         * scene switch, like the engine's player state — em_door.h). */
        int ok_pos    = fabsf(g.pos[0] - 25.89f) <= 0.35f &&
                        fabsf(g.pos[1])          <= 0.6f &&
                        fabsf(g.pos[2] + 225.0f) <= 0.1f &&
                        fabsf(g.yaw + EM_PI * 0.5f) <= 0.01f;
        int ok_fade   = g.tt_max_fade >= 0.999f &&
                        em_frame_fade_level() <= 0.001f;
        int ok_unlock = !em_door_movement_locked();
        int closed    = 0;
        for (int i = 0; i < em_door_count(); i++)
            closed += em_door_state(i) == EM_DOOR_CLOSED;
        int ok_doors  = em_door_count() == 6 && closed == 6;
        int ok_coll   = g.coll.poly_count > 0 &&
                        strstr(g.coll_path, "drawbridge.emcl") != NULL;
        int ok_enemy  = em_enemy_count() >= 1;
        int ok_player = g.mesh != NULL;
        int ok = g.tt_ok_trigger && g.tt_ok_lock && ok_scene && ok_pos &&
                 ok_fade && ok_unlock && ok_doors && ok_coll && ok_enemy &&
                 ok_player;
        printf("transit test: trigger->OPENING %s, locked mid-transit %s, "
               "scene switched at frame %d -> %s: %s, player (%.3f, %.3f, "
               "%.3f) yaw %.4f at the arrival spawn: %s, fade peak %.3f / "
               "final %.3f: %s, unlocked %s, new scene doors %d (closed "
               "%d): %s, collision %s (%u polys): %s, enemies %d: %s, "
               "player model kept: %s — %s\n",
               g.tt_ok_trigger ? "ok" : "FAILED",
               g.tt_ok_lock ? "ok" : "FAILED",
               g.tt_switch_frame, g.scene_dir,
               ok_scene ? "ok" : "FAILED",
               g.pos[0], g.pos[1], g.pos[2], g.yaw,
               ok_pos ? "ok" : "FAILED",
               g.tt_max_fade, em_frame_fade_level(),
               ok_fade ? "ok" : "FAILED",
               ok_unlock ? "ok" : "FAILED",
               em_door_count(), closed, ok_doors ? "ok" : "FAILED",
               g.coll_path, g.coll.poly_count, ok_coll ? "ok" : "FAILED",
               em_enemy_count(), ok_enemy ? "ok" : "FAILED",
               ok_player ? "ok" : "FAILED",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
    if (em_frame_fade_level() > g.tt_max_fade)
        g.tt_max_fade = em_frame_fade_level();
}

/* EM_SLIDER_TEST=1 — deterministic SLIDING-DOOR self-test (run with
 * EM_SCENE=assets/scene_drawbridge). Exercises the decoded m17/m09
 * variant brain func_001BB860 (em_door.h "SLIDERS") on the drawbridge
 * room's intra-room slider door 4 — door_m09 at (128.6, 0, -610) yaw
 * pi/2 (AREA01 sub-0 placement; room-move entries 9/8 flank it at
 * x 143 / 115.5):
 *
 *   frame    0      spawn (112, 0, -610) facing +X, 16.6 u from the
 *                   door; hold 'a' — the player RUNS at the door. (Under
 *                   the BYTE-FAITHFUL move basis, with the chase camera
 *                   directly behind the spawn, 'a' is the "forward into
 *                   the scene" key — desired = atan2(sx,-sy) + cam_yaw +
 *                   pi/2, so 'a' (sx=-1) -> cam_yaw + pi/2 -> facing dir;
 *                   same remap the door test took 2026-06-17. 'w' here
 *                   walks -Z, NOT at the door.)
 *   ~frame  16..17  the player crosses the 10-u scan radius still
 *                   pushing — and must stay UNARMED: the engine has NO
 *                   walk-into door trigger (s58 decode; the s56
 *                   no-button reading is OVERTURNED — the use scan
 *                   runs only on the USE press edge). Asserted at 17.
 *   frame   18      CROSS press (the D_00810E74-edge use scan, config
 *                   mask spad 3B76 = 0x0040) inside the window ->
 *                   kickoff: back side (bearing -pi/2 vs door yaw
 *                   +pi/2), yaw snap +X,
 *                   walk to the staging point (122.6, -610) =
 *                   door - 6.0 * fwd (the func_001BB560 6.0 constant)
 *   ~frame  30..75  the NATIVE SLIDE pumps (46-frame clip, panels
 *                   part 0.2 u/frame to +-9 u) while the player
 *                   STANDS at staging — no player anim (the slider
 *                   script has none), no fade (op07 sub0)
 *   ~frame  76..    op01-sub8 walk-through: scripted MOVE-TO to the
 *                   mirrored point (134.6, -610); at arrival the
 *                   script ends — locks release, door stays OPEN
 *   frame  150..170 hold 'w' — the player runs on +X out of the scan
 *                   radius (+ the 2-u hysteresis)
 *   frame  240      assert: stick-push alone did NOT arm + the CROSS
 *                   edge DID, mid-slide parked at staging with NO
 *                   scripted player anim and fade 0, walk-through
 *                   landed at (134.6, -610) unlocked, door reclosed
 *                   (reverse clip) after the leave.
 */
static void slider_test_script(void)
{
    static int   st_door, st_ok_noarm, st_ok_arm, st_ok_noanim, st_ok_park;
    static int   st_ok_through, st_ok_unlock;
    static float st_max_fade;
    int n = g.frame_no;
    if (n == 0) {
        st_door = -1;
        st_ok_noarm = st_ok_arm = st_ok_noanim = st_ok_park = 0;
        st_ok_through = st_ok_unlock = 0;
        st_max_fade = 0.0f;
        move_test_inject('a', 1);   /* 'a' = forward at the door (faithful basis) */
    } else if (n == 3) {
        /* the test door = nearest instance to the slider placement */
        float best = 1e30f;
        for (int i = 0; i < em_door_count(); i++) {
            float p[3];
            em_door_pos(i, p);
            float dx = p[0] - 128.6f, dz = p[2] + 610.0f;
            float d2 = dx * dx + dz * dz;
            if (d2 < best) { best = d2; st_door = i; }
        }
    } else if (n == 17) {
        /* NOW inside the 10-u scan window (dist ~9.3), pushing, NO button:
         * must be UNARMED — the engine has no walk-into trigger (s58).
         * CROSS frame RETUNED 2026-06-17 (14 -> 18): under the corrected
         * run-ramp (func_0017BC40, 2026-06-11) and the faithful move basis
         * the player only crosses the 10-u radius at frame ~16, so the old
         * frame-14 press fired ~11 u out and never armed. This assert now
         * proves no-walk-into-trigger from INSIDE the window, where it
         * actually bites. */
        st_ok_noarm = st_door >= 0 &&
                      em_door_state(st_door) == EM_DOOR_CLOSED &&
                      !em_door_movement_locked();
        if (!st_ok_noarm)
            printf("slider test: frame 17 door state %d lock %d — armed "
                   "without the USE press\n",
                   st_door >= 0 ? em_door_state(st_door) : -1,
                   em_door_movement_locked());
    } else if (n == 18) {
        move_test_inject('k', 1);   /* CROSS — the use-scan press edge */
    } else if (n == 19) {
        move_test_inject('k', 0);
    } else if (n == 22) {
        /* armed by the CROSS edge inside the class-5 window */
        st_ok_arm = st_door >= 0 &&
                    em_door_state(st_door) == EM_DOOR_OPENING;
        move_test_inject('a', 0);   /* the script owns the player now */
    } else if (n == 60) {
        /* mid-slide: the player is PARKED at the staging point
         * (122.6, -610) with NO scripted anim — the slider script has
         * no player door gesture (the PCSX2-verified look). */
        st_ok_noanim = em_game_anim_active() == 0 &&
                       em_door_movement_locked();
        st_ok_park   = fabsf(g.pos[0] - 122.6f) <= 0.35f &&
                       fabsf(g.pos[2] + 610.0f) <= 0.35f;
        if (!st_ok_park)
            printf("slider test: frame 60 pos (%.3f, %.3f, %.3f) — off "
                   "the staging point\n", g.pos[0], g.pos[1], g.pos[2]);
    } else if (n == 140) {
        /* walk-through done: player on the far side, free, door OPEN */
        st_ok_through = fabsf(g.pos[0] - 134.6f) <= 0.35f &&
                        fabsf(g.pos[2] + 610.0f) <= 0.35f &&
                        st_door >= 0 &&
                        em_door_state(st_door) == EM_DOOR_OPEN;
        st_ok_unlock  = !em_door_movement_locked() &&
                        !em_door_menu_locked();
        if (!st_ok_through)
            printf("slider test: frame 140 pos (%.3f, %.3f, %.3f) door "
                   "state %d\n", g.pos[0], g.pos[1], g.pos[2],
                   st_door >= 0 ? em_door_state(st_door) : -1);
    } else if (n == 150) {
        move_test_inject('a', 1);   /* run on +X, out of the scan radius */
    } else if (n == 170) {
        move_test_inject('a', 0);
    } else if (n == 240) {
        int ok_closed = st_door >= 0 &&
                        em_door_state(st_door) == EM_DOOR_CLOSED;
        int ok_nofade = st_max_fade <= 0.001f;   /* op07 sub0: NO fade */
        int ok = st_ok_noarm && st_ok_arm && st_ok_noanim && st_ok_park &&
                 st_ok_through && st_ok_unlock && ok_closed && ok_nofade;
        printf("slider test: stick-push alone left it CLOSED %s, CROSS "
               "edge armed %s, mid-slide "
               "parked at staging w/ no player anim %s/%s, walk-through "
               "landed far-side + unlocked %s/%s, fade stayed 0 (%.3f): "
               "%s, door reclosed after leave (state %d): %s — %s\n",
               st_ok_noarm ? "ok" : "FAILED",
               st_ok_arm ? "ok" : "FAILED",
               st_ok_park ? "ok" : "FAILED",
               st_ok_noanim ? "ok" : "FAILED",
               st_ok_through ? "ok" : "FAILED",
               st_ok_unlock ? "ok" : "FAILED",
               st_max_fade, ok_nofade ? "ok" : "FAILED",
               st_door >= 0 ? em_door_state(st_door) : -1,
               ok_closed ? "ok" : "FAILED",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
    if (em_frame_fade_level() > st_max_fade)
        st_max_fade = em_frame_fade_level();
}

/* EM_LOCKED_TEST=1 — deterministic LOCKED-DOOR self-test (run with
 * EM_SCENE=assets/scene_drawbridge). Exercises the full locked
 * sequence (em_door.h "THE LOCKED SEQUENCE") on the drawbridge room's
 * m15 SECURITY DOOR — the REAL lock-gated placement (AREA01 sub-0
 * record [14], model 0x15, door id 1, manifest `locked` token from
 * export_level.py --door-locked) at (-20.5, 0, -192) yaw 0, goto
 * scene_office0:
 *
 *   frame    0      spawn (-25.5, 0, -201) — on the doorway-center
 *                   column (center = hinge + 5 = (-25.5, -192)), BACK
 *                   side, 9 u out, facing the door (+Z)
 *   frame    5      CROSS -> use scan arms; the LOCK GATE refuses
 *                   (unlock bit clear): kickoff mode 1 -> sub 1,
 *                   locks engage, walk-to staging (-25.5, -197)
 *   ~frame  23      arrival: locked-look camera CUT (func_001BBBF0:
 *                   target (-28.5, 10, -192) = door + 8 to the handle
 *                   side + 10 up; eye = target - 13 along the live
 *                   camera yaw 0 -> (-28.5, 12, -205)), player try
 *                   anim 0x44 (back), lock-fixture jiggle clip
 *   ~frame  83      the op-0x02 60-frame wait elapses: rattle 0x3F2 +
 *                   the RADIO/EXAMINE message machine starts on GLOBAL
 *                   line 6 "It's locked and won't open." (op09
 *                   func_001BBAE0 — jtbl sel 0; em_hud_radio)
 *   ~frame 202      the message's 118 display frames + the terminal
 *                   record elapse — the machine reports done
 *   ~frame 223      jiggle clip (200 f) ends (the locked script's op0B
 *                   sub1 gate, AFTER the pumped VO native): finish
 *                   script restores camera + control; door re-arms
 *                   CLOSED — no fade, no warp, player still on the
 *                   near side
 *   frame  255      em_door_unlock() — the panel/keycard event
 *   frame  260      CROSS again -> the gate passes: state OPENING,
 *                   open anim 0x43, 70-frame wait, commit, fade,
 *                   GOTO scene switch to office0, arrival walk-out
 *   frame  560      assert the whole ledger (PASS/FAIL) and quit.
 */
static void locked_test_script(void)
{
    static int   lt_door, lt_ok_refuse, lt_ok_lock, lt_ok_anim;
    static int   lt_ok_cam, lt_ok_rattle, lt_ok_shut, lt_ok_stay;
    static int   lt_ok_restore, lt_ok_retry, lt_ok_openanim, lt_ok_switch;
    static int   lt_ok_radio_run, lt_ok_radio_done;
    static float lt_refuse_fade;
    int n = g.frame_no;
    if (n == 0) {
        lt_door = -1;
        lt_ok_refuse = lt_ok_lock = lt_ok_anim = lt_ok_cam = 0;
        lt_ok_rattle = lt_ok_shut = lt_ok_stay = lt_ok_restore = 0;
        lt_ok_retry = lt_ok_openanim = lt_ok_switch = 0;
        lt_ok_radio_run = lt_ok_radio_done = 0;
        lt_refuse_fade = 0.0f;
    } else if (n == 3) {
        /* the test door = nearest instance to the m15 placement */
        float best = 1e30f;
        for (int i = 0; i < em_door_count(); i++) {
            float p[3];
            em_door_pos(i, p);
            float dx = p[0] + 20.5f, dz = p[2] + 192.0f;
            float d2 = dx * dx + dz * dz;
            if (d2 < best) { best = d2; lt_door = i; }
        }
        if (lt_door < 0 || !em_door_is_locked(lt_door))
            printf("locked test: door %d is not lock-gated — manifest "
                   "`locked` token missing?\n", lt_door);
    } else if (n == 5) {
        move_test_inject('k', 1);   /* CROSS — the use-scan trigger */
    } else if (n == 6) {
        move_test_inject('k', 0);
    } else if (n == 8) {
        /* the gate REFUSED: locked-try state, both locks engaged */
        lt_ok_refuse = lt_door >= 0 &&
                       em_door_state(lt_door) == EM_DOOR_LOCKED_TRY;
        lt_ok_lock   = em_door_movement_locked() && em_door_menu_locked();
    } else if (n == 45) {
        /* mid-try: the scripted TRY anim owns the player and the
         * locked-look camera is pinned at the handle (the func_001BBBF0
         * geometry — camera cut observed via state). */
        float dp[3], dyaw;
        lt_ok_anim = em_game_anim_active() == 0x44;
        lt_ok_cam  = em_door_locked_look(dp, &dyaw, NULL) &&
                     fabsf(g.cam.tgt[0] + 28.5f)  <= 0.75f &&
                     fabsf(g.cam.tgt[1] - 10.0f)  <= 0.75f &&
                     fabsf(g.cam.tgt[2] + 192.0f) <= 0.75f &&
                     fabsf(g.cam.eye[1] - 12.0f)  <= 0.75f &&
                     fabsf(g.cam.eye[2] + 205.0f) <= 1.5f;
        if (!lt_ok_cam)
            printf("locked test: frame 45 cam eye (%.2f, %.2f, %.2f) "
                   "tgt (%.2f, %.2f, %.2f) — off the locked-look\n",
                   g.cam.eye[0], g.cam.eye[1], g.cam.eye[2],
                   g.cam.tgt[0], g.cam.tgt[1], g.cam.tgt[2]);
    } else if (n == 95) {
        /* the 60-frame mark passed: exactly one rattle 0x3F2, and the
         * radio/examine message machine is PRESENTING line 6 (the op09
         * native started it on the same tick as the rattle) */
        lt_ok_rattle    = em_door_rattles() == 1;
        lt_ok_radio_run = em_hud_radio_active();
    } else if (n == 250) {
        /* refusal over: control restored, door CLOSED and re-armed,
         * NO fade ever ran, NO warp/switch — the player stands at the
         * staging point on his own side, anim torn down. */
        lt_ok_shut    = lt_door >= 0 &&
                        em_door_state(lt_door) == EM_DOOR_CLOSED &&
                        lt_refuse_fade <= 0.001f;
        /* Near side held: parked at the staging point (-197.25 — the
         * MOVE-TO 0.3-u arrival window), then the control restore lets
         * the free-move wall separation push the 4.5-u player radius
         * off the closed door hull (~ -198.5). Never past the door
         * plane (-192). */
        lt_ok_stay    = fabsf(g.pos[0] + 25.5f) <= 0.35f &&
                        g.pos[2] <= -196.0f && g.pos[2] >= -199.5f;
        lt_ok_restore = !em_door_movement_locked() &&
                        !em_door_menu_locked() &&
                        em_game_anim_active() == 0;
        /* the message machine completed its full presentation: no
         * longer active, and the text record showed exactly 118
         * frames (the GLOBAL table's line-6 duration) */
        lt_ok_radio_done = !em_hud_radio_active() &&
                           em_hud_radio_frames() == 118;
        if (!lt_ok_radio_done)
            printf("locked test: frame 250 radio active %d frames %d — "
                   "expected done after 118\n",
                   em_hud_radio_active(), em_hud_radio_frames());
        if (!lt_ok_stay)
            printf("locked test: frame 250 pos (%.3f, %.3f, %.3f) — "
                   "moved through a locked door\n",
                   g.pos[0], g.pos[1], g.pos[2]);
    } else if (n == 255) {
        em_door_unlock(lt_door);    /* the panel/keycard unlock event */
    } else if (n == 260) {
        move_test_inject('k', 1);   /* retry */
    } else if (n == 261) {
        move_test_inject('k', 0);
    } else if (n == 264) {
        lt_ok_retry = lt_door >= 0 &&
                      em_door_state(lt_door) == EM_DOOR_OPENING;
    } else if (n == 300) {
        /* the OPEN script owns the player now (back-side clip 0x43) */
        lt_ok_openanim = em_game_anim_active() == 0x43 &&
                         em_door_movement_locked();
    } else if (n == 560) {
        int ok_unlocked = !em_door_movement_locked() &&
                          !em_door_menu_locked();
        int ok_fade     = em_frame_fade_level() <= 0.001f;
        int ok = lt_ok_refuse && lt_ok_lock && lt_ok_anim && lt_ok_cam &&
                 lt_ok_rattle && lt_ok_radio_run && lt_ok_radio_done &&
                 lt_ok_shut && lt_ok_stay &&
                 lt_ok_restore && lt_ok_retry && lt_ok_openanim &&
                 lt_ok_switch && ok_unlocked && ok_fade;
        printf("locked test: refusal->LOCKED_TRY %s, locks engaged %s, "
               "try anim 0x44 %s, locked-look cam %s, rattle 0x3F2 x1 "
               "%s, radio line 6 presenting %s, radio done @118 f %s, "
               "door shut + no fade %s, pos held near side %s, "
               "control restored %s, unlock+retry->OPENING %s, open "
               "anim 0x43 %s, goto switch to office0 %s, final "
               "unlocked %s fade %.3f %s — %s\n",
               lt_ok_refuse ? "ok" : "FAILED",
               lt_ok_lock ? "ok" : "FAILED",
               lt_ok_anim ? "ok" : "FAILED",
               lt_ok_cam ? "ok" : "FAILED",
               lt_ok_rattle ? "ok" : "FAILED",
               lt_ok_radio_run ? "ok" : "FAILED",
               lt_ok_radio_done ? "ok" : "FAILED",
               lt_ok_shut ? "ok" : "FAILED",
               lt_ok_stay ? "ok" : "FAILED",
               lt_ok_restore ? "ok" : "FAILED",
               lt_ok_retry ? "ok" : "FAILED",
               lt_ok_openanim ? "ok" : "FAILED",
               lt_ok_switch ? "ok" : "FAILED",
               ok_unlocked ? "ok" : "FAILED",
               em_frame_fade_level(), ok_fade ? "ok" : "FAILED",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
    if (getenv("EM_LOCKED_TRACE") && n % 5 == 0 && n < 260)
        printf("LT f%d pos (%.3f, %.3f) door %d state %d transit_lock %d\n",
               n, g.pos[0], g.pos[2], lt_door,
               lt_door >= 0 ? em_door_state(lt_door) : -1,
               em_door_movement_locked());
    /* The refusal must never fade (the locked script has no commit);
     * track the fade only until the unlock. */
    if (n > 0 && n < 255 && em_frame_fade_level() > lt_refuse_fade)
        lt_refuse_fade = em_frame_fade_level();
    /* The goto switch lands when the active scene dir changes. */
    if (!lt_ok_switch && strstr(g.scene_dir, "office0"))
        lt_ok_switch = 1;
}

/* EM_WEAPON_TEST=1 — deterministic firing-loop self-test (em_weapon.c).
 * Exercises the whole weapon contract through the real input API from
 * the demo ammo state mag 4 / reserve 120 (the live test save).
 *
 * The schedule is computed at frame 0 from the weapon's HONEST state
 * windows (em_weapon_draw_ticks/reload_ticks/holster_ticks — the real
 * anim-clip lengths when the player EMDL carries them, the flagged
 * fallbacks otherwise), anchored at A/B/C/D:
 *
 *   frame    0       hold E (R1) — draw (anim 0x110 @1.4)
 *   A = draw+5       AIM reached; four SEMI presses of L (CIRCLE — the
 *   A/A+14/A+28/A+42 engine's default-config fire button, s29) at
 *                    14-frame spacing — PAST the engine's 25-frame
 *                    ladder cadence (+0x2F4 = the 0x112 clip length,
 *                    func_0017A8B0; shots space >= 13 ticks), so each
 *                    press fires 1:1: mag AND reserve decrement
 *                    (TOTAL-pool rule) -> 4/120 .. 0/116
 *   A+55             the 4th shot's cadence EXPIRY auto-reloads the
 *                    empty mag (mode 1 — no extra press needed:
 *                    mag = min(30, 116) = 30, reserve UNTOUCHED at
 *                    116; anim 0x33 holds the RELOAD state)
 *   B = A+55+rld+8   press after the reload window: 5th real shot
 *                    -> 29/115
 *   B+16..B+47       FULL-AUTO stretch (after the semi cadence
 *                    expires): mode 2, L held 31 frames -> shots at
 *                    the 6-frame in-burst cadence (the 0x1E state's
 *                    +0x2F4 = 12.0 store) = 6 rounds -> 23/109
 *   C = B+66         manual top-up (R = L3, the engine's raw reload
 *                    bit; honored only in WAIT) -> 30/109, reserve
 *                    again untouched
 *   D = C+rld+24     RECOIL-SETTLE check: by D the post-reload aim hold
 *                    has been quiet for > ceil(25/2) ticks, so the 0x112
 *                    playhead must be CLAMPED at the last frame (the
 *                    +24 window exists for this — the recoil replay
 *                    needs 12.5 ticks to settle); then release E ->
 *                    holster (anim 0x111) -> HOLSTERED checked one
 *                    holster window later
 *
 * RECOIL checks (s25): a shot rewinds the held 0x112 clip to frame 0
 * at 2 frames/tick (em_game_anim_hold_restart — the engine's fire
 * counter re-seed, FINDINGS "FIRE ANIM MECHANISM"); the A+6 and B+6
 * checkpoints additionally assert the playhead is MID-REPLAY (0 <
 * frame < last) while the committed clip stays 0x112, and D asserts
 * the clamp. Skipped when the player EMDL carries no clip 0x112 (the
 * clip-less fallback build).
 *
 * Checkpoints sample a few frames after each action so the input-inject
 * latency (events land in the NEXT frame's snapshot) and the fire-event
 * one-frame latency are absorbed. */

/* Recoil introspection helper: 1 = the committed scripted clip is the
 * aim pose 0x112 with its playhead mid-replay (a shot rewound it and
 * it has not yet clamped); 2 = committed 0x112 clamped at the last
 * frame (the settled hold); 0 = anything else. -1 = the EMDL carries
 * no 0x112 (recoil checks skipped honestly). */
static int wt_recoil_state(void)
{
    int frames = em_game_anim_frames(0x112);
    if (frames <= 0) return -1;
    if (em_game_anim_active() != 0x112) return 0;
    int f = em_game_anim_frame();
    if (f <= 0) return 0;              /* not yet evaluated / at start */
    return f < frames - 1 ? 1 : 2;
}
static void wt_check(int cond, const char *what)
{
    if (cond) return;
    g.wt_fail++;
    printf("weapon test: CHECK FAILED — %s (state %d, mag %u, reserve %d, "
           "shots %d, reloads %d)\n", what, em_weapon_state(),
           em_weapon_mag(), em_weapon_reserve(), em_weapon_shots(),
           em_weapon_reloads());
}

static void weapon_test_script(void)
{
    static int A, B, C, D;        /* checkpoint anchors (see the doc) */
    int n = g.frame_no;
    if (n == 0) {
        A = em_weapon_draw_ticks() + 5;
        B = A + 55 + em_weapon_reload_ticks() + 8;
        C = B + 66;     /* past the auto leg + its final cadence */
        D = C + em_weapon_reload_ticks() + 24;  /* +24: recoil-settle
                                                 * window (12.5 ticks
                                                 * to clamp) */
        printf("weapon test: windows draw %d / reload %d / holster %d "
               "ticks -> anchors A=%d B=%d C=%d D=%d\n",
               em_weapon_draw_ticks(), em_weapon_reload_ticks(),
               em_weapon_holster_ticks(), A, B, C, D);
        move_test_inject('e', 1);                           /* R1 hold  */
        return;
    }
    if (n == A) {
        wt_check(em_weapon_state() == EM_WPN_AIM, "drawn by A");
        move_test_inject('l', 1);                           /* semi #1  */
    } else if (n == A + 2 || n == A + 16 || n == A + 30 || n == A + 44) {
        move_test_inject('l', 0);
    } else if (n == A + 6) {
        wt_check(em_weapon_mag() == 3 &&
                 em_weapon_reserve() == 119 &&
                 em_weapon_shots() == 1,
                 "shot 1: 4/120 -> 3/119");
        int rs = wt_recoil_state();
        wt_check(rs == 1 || rs == -1,
                 "recoil replay mid-flight after shot 1 (0x112 rewound)");
    } else if (n == A + 14 || n == A + 28 || n == A + 42) {
        move_test_inject('l', 1);                           /* semi 2..4 */
    } else if (n == A + 48) {
        wt_check(em_weapon_mag() == 0 &&
                 em_weapon_reserve() == 116 &&
                 em_weapon_shots() == 4,
                 "shots 2-4 at the 14-frame spacing: mag empty at "
                 "0/116");
    } else if (n == A + 60) {
        /* the 4th shot's cadence expiry (shot ~A+43, +12 ticks)
         * auto-reloaded the dry mag — mode 1, NO press required */
        wt_check(em_weapon_state() == EM_WPN_RELOAD &&
                 em_weapon_mag() == 30 &&
                 em_weapon_reserve() == 116 &&
                 em_weapon_shots() == 4 &&
                 em_weapon_reloads() == 1,
                 "dry mag auto-reloads at the cadence expiry: mag = "
                 "min(30, reserve) = 30, reserve untouched (116), no "
                 "round fired");
    } else if (n == B) {
        wt_check(em_weapon_state() == EM_WPN_AIM,
                 "reload anim over by B");
        move_test_inject('l', 1);                           /* semi #5  */
    } else if (n == B + 2) {
        move_test_inject('l', 0);
    } else if (n == B + 6) {
        wt_check(em_weapon_mag() == 29 &&
                 em_weapon_reserve() == 115 &&
                 em_weapon_shots() == 5,
                 "5th shot after reload: 29/115");
        int rs = wt_recoil_state();
        wt_check(rs == 1 || rs == -1,
                 "recoil replay mid-flight after shot 5");
    } else if (n == B + 16) {
        /* the 5th (semi) shot's 25-frame cadence has expired by here
         * (shot ~B+1, expiry ~B+13) — the auto hold starts from WAIT */
        em_weapon_set_fire_mode(EM_WPN_MODE_AUTO);
        move_test_inject('l', 1);                           /* auto hold */
    } else if (n == B + 47) {
        move_test_inject('l', 0);
    } else if (n == B + 54) {
        wt_check(em_weapon_mag() == 23 &&
                 em_weapon_reserve() == 109 &&
                 em_weapon_shots() == 11,
                 "auto 31-frame hold = 6 rounds: 23/109");
    } else if (n == C) {
        move_test_inject('2', 1);                           /* manual L3 */
    } else if (n == C + 2) {
        move_test_inject('2', 0);
    } else if (n == C + 8) {
        wt_check(em_weapon_state() == EM_WPN_RELOAD &&
                 em_weapon_mag() == 30 &&
                 em_weapon_reserve() == 109 &&
                 em_weapon_reloads() == 2,
                 "manual top-up: 30/109, reserve untouched");
    } else if (n == D) {
        int rs = wt_recoil_state();
        wt_check(rs == 2 || rs == -1,
                 "0x112 settled back into the clamped hold by D");
        move_test_inject('e', 0);                           /* holster   */
    } else if (n == D + em_weapon_holster_ticks() + 6) {
        wt_check(em_weapon_state() == EM_WPN_HOLSTERED,
                 "holstered after R1 release");
        printf("weapon test: %d shots, %d reloads, mag %u, reserve "
               "%d, last ray %s — %s\n", em_weapon_shots(),
               em_weapon_reloads(), em_weapon_mag(),
               em_weapon_reserve(),
               em_weapon_last_hit() < 0 ? "none"
                   : em_weapon_last_hit() ? "hit" : "miss",
               g.wt_fail == 0 ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_ENEMY_TEST — deterministic enemy-vs-player self-tests (em_enemy.c;
 * s62 condition decode + the s66 shootability verdict — the worm is
 * BORN ATTACKING, the crate bursts on DAMAGE ONLY, and the WORM IS
 * NOT SHOOTABLE (both victim filters reject model 0x0D; nothing
 * consumes its mailbox). Scene-init arm (et_spawned) places the
 * target; the scripts adapt to actual pace instead of fixed frames:
 *
 *   EM_ENEMY_TEST=1 (kill run — RESTAGED s68: the burst hatches the
 *     nest-group BUGS now, and bugs ARE victims): spawn a DISGUISED
 *     CRATE 12 u dead ahead (inside the office's wall plane 14 u out,
 *     so the world ray cannot outrank the victim test). Hold R1 from
 *     frame 0 (draw), pitch the aim down until the screen-cone lock
 *     fills on the crate, fire ONE semi shot -> +0x36 mailbox vs the
 *     crate's HP 1 -> assert the ray resolved HIT, the crate burst on
 *     its IDLE poll, and TWO BUGS (the default nest group) hatched at
 *     the crate ring with the variant-A HP 15. Then the SHOOTABLE-BUG
 *     witness (s68 — replaces the old worm pass-through leg; the worm
 *     victim-filter rejection stays witnessed by EM_MELEE_TEST's
 *     whiff leg and EM_ENEMY_TEST=4's mailbox witness): keep R1,
 *     assert the lock RE-FILLS on a hatched bug (func_00183B80 passes
 *     the bug models 0x0F/0x10), fire ONE more shot and assert it
 *     resolved HIT and the locked bug FLINCHED — HP 15 -> 10 (the
 *     every-tick mailbox consumption, bullet amount 5), still alive
 *     and attacking. (s76: the bug brain now bites, so player health is
 *     reported by et_finish, not asserted untouched — see the leg.)
 *   EM_ENEMY_TEST=2 (contact run): ONE WORM 30 u ahead; weapon stays
 *     holstered; let the worm
 *     run its decoded sequence (approach 90 t -> stalk 120 t homing ->
 *     windup 45 t -> lunge). The lunge CONNECT posts the decoded latch
 *     15 (D_008104D4) and the worm bursts -> assert health dropped by
 *     exactly 15 and the worm despawned. (With the leech asset the
 *     connect is the decoded neck->head SEGMENT arm; rig-less runs
 *     fold into the radius-6 contact — em_enemy.c LATCH SEGMENT.)
 *   EM_ENEMY_TEST=3 (crate run — RESTAGED s68): spawn a DISGUISED
 *     CRATE 25 units ahead instead (em_enemy.h "CRATE KIND"); weapon
 *     stays holstered, walk forward (W) INTO it. Assert the DISGUISE
 *     HOLDS at point-blank range (the engine has no proximity trigger
 *     — the old ~10-u burst was port-invented), then post a
 *     knife-sized 5 mailbox hit (em_enemy_damage — the same write a
 *     melee impact does; EM_MELEE_TEST covers the real knife path)
 *     and assert the burst hatched TWO BUGS at the crate's hatch ring
 *     (s68 — never a worm) with the variant-A HP 15, the bugs
 *     ENGAGED (the minimal walk brain), then the bug MAILBOX LADDER
 *     on bug slot 1: a nonlethal 5 FLINCHES it (HP 15 -> 10, alive,
 *     the every-tick func_00128B80 consumption), a following 15
 *     KILLS it (handler func_00129FC0; slot freed, corpse fade) while
 *     the other bug lives on. Closing distances are reported, not
 *     asserted (the hatch ring can land inside the approach
 *     standoff). */
static void et_check(int cond, const char *what)
{
    if (cond) return;
    g.et_fail++;
    printf("enemy test: CHECK FAILED — %s\n", what);
}

static float et_dist_i(int i)
{
    float p[3] = { 0.0f, 0.0f, 0.0f };
    em_enemy_pos(i, p);
    float dx = p[0] - g.pos[0], dz = p[2] - g.pos[2];
    return sqrtf(dx * dx + dz * dz);
}

static float et_dist(void)
{
    return et_dist_i(0);
}

static void et_finish(const char *run)
{
    printf("enemy test (%s): spawn dist %.1f, final dist %.1f, shots %d, "
           "crawler alive %d (state %d), health %.0f (start %.0f) — %s\n",
           run, g.et_d0, et_dist(), em_weapon_shots(), em_enemy_alive(),
           em_enemy_state(0), g.status.health, g.et_health0,
           g.et_fail == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    em_frame_request_quit();
}

static void enemy_test_script(void)
{
    int n = g.frame_no;
    if (n == 0) {
        g.et_d0      = et_dist();
        g.et_health0 = g.status.health;
        if (g.enemy_test == 1)
            move_test_inject('e', 1);   /* R1 hold — draw the rifle */
        if (g.enemy_test == 3)
            move_test_inject('w', 1);   /* walk at the crate */
        return;
    }
    if (n == 20 && g.enemy_test == 2)   /* the disguised crates (tests
                                         * 1/3) stay IDLE */
        et_check(em_enemy_state(0) == EM_ENEMY_ATTACK,
                 "worm attacking by frame 20 (born attacking — "
                 "func_00153F10 has no idle state)");

    if (g.enemy_test == 1) {
        /* RESTAGED s68: shot 1 kills the CRATE (HP 1); the burst
         * hatches the nest-group BUGS — which ARE victims, so shot 2
         * is the SHOOTABLE-BUG witness (lock re-fill + flinch). */
        static int second_fired, bug_lock = -1;
        if (!g.et_fired) {
            /* ENGINE-TRUE ACQUISITION (2026-06-11 func_00199220
             * translation): the bullet only bends to a target inside
             * the SCREEN-center cone — the low crate under a level
             * aim sits below it. Do what the player does: pitch the
             * aim DOWN (inverted-Y stick up, 'w') until lock slot 0
             * fills, then fire the locked shot. */
            int locked = em_weapon_lock_target() >= 0;
            if (em_weapon_state() == EM_WPN_AIM)
                move_test_inject('w', !locked);
            if (n > 20 && locked) {
                et_check(em_enemy_state(0) == EM_ENEMY_IDLE,
                         "crate disguise holding at lock time");
                et_check(em_weapon_state() == EM_WPN_AIM,
                         "rifle drawn (AIM) at fire time");
                move_test_inject('w', 0);
                move_test_inject('l', 1);   /* CIRCLE — semi shot 1 */
                g.et_fired = n;
            } else if (n >= 400) {
                et_check(0, "screen-cone lock on the crate by frame "
                            "400");
                et_finish("kill run");
            }
        } else if (n == g.et_fired + 2) {
            move_test_inject('l', 0);
        } else if (n == g.et_fired + 12 && !second_fired) {
            et_check(em_weapon_shots() == 1, "one round after shot 1");
            et_check(em_weapon_last_hit() == 1, "shot 1 resolved HIT");
            et_check(em_enemy_state(0) == EM_ENEMY_FREE,
                     "crate burst on its IDLE mailbox poll (HP 1)");
            et_check(em_enemy_alive() == 2 &&
                     em_enemy_kind(1) == EM_ENEMY_KIND_BUG &&
                     em_enemy_kind(2) == EM_ENEMY_KIND_BUG,
                     "nest-group bugs (2) hatched by the burst (s68)");
            et_check(em_enemy_hp(1) == 15 && em_enemy_hp(2) == 15,
                     "variant-A HP 15 (func_00128390)");
            second_fired = -1;          /* arm the re-lock stage */
        } else if (second_fired == -1) {
            /* the SHOOTABLE-BUG witness (s68): the lock must RE-FILL
             * on a hatched bug (func_00183B80 passes models
             * 0x0F/0x10) — keep pitching like the player does until
             * it does, then fire the locked shot. */
            bug_lock = em_weapon_lock_target();
            if (em_weapon_state() == EM_WPN_AIM)
                move_test_inject('w', bug_lock < 0);
            if (bug_lock >= 0) {
                et_check(em_enemy_kind(bug_lock) == EM_ENEMY_KIND_BUG,
                         "auto-aim lock re-fills on a hatched bug");
                move_test_inject('w', 0);
                move_test_inject('l', 1);   /* CIRCLE — semi shot 2 */
                second_fired = n;
            } else if (n >= g.et_fired + 400) {
                et_check(0, "screen-cone lock on a bug after the "
                            "burst");
                et_finish("kill run");
            }
        } else if (n == second_fired + 2) {
            move_test_inject('l', 0);
        } else if (n == second_fired + 12) {
            et_check(em_weapon_shots() == 2, "exactly two rounds fired");
            et_check(em_weapon_last_hit() == 1, "shot 2 resolved HIT");
            et_check(em_enemy_state(bug_lock) == EM_ENEMY_ATTACK &&
                     em_enemy_hp(bug_lock) == 10,
                     "locked bug FLINCHED — HP 15 -> 10 (every-tick "
                     "mailbox func_00128B80, bullet amount 5)");
            et_check(em_enemy_alive() == 2, "both bugs still alive");
            /* s76: the bug brain now has a real bite lunge, so player
             * health is NO LONGER asserted untouched here — a bug that
             * closes the ~12 u and completes a lunge inside this short
             * witness window CAN bite (BUG_BITE_DMG). The health delta
             * is reported by et_finish; this leg only witnesses
             * shootability + flinch. */
            et_finish("kill run");
        }
    } else if (g.enemy_test == 3) {
        /* crate run, engine-true: NO proximity burst exists — walk to
         * point-blank, assert the disguise HOLDS, then damage it (the
         * decoded only-trigger) and witness the s68 bug hatch + the
         * bug mailbox ladder (flinch below lethal, death at it). */
        static int et_stop_frame, et_hit_sent, et_flinch_sent,
                   et_kill_sent;
        if (!et_stop_frame) {
            if (et_dist() <= 6.0f) {
                et_stop_frame = n;
                move_test_inject('w', 0);    /* stop at the crate */
            } else if (n >= 600) {
                et_check(0, "player reached the crate by frame 600");
                et_finish("crate run");
            }
        } else if (!et_hit_sent) {
            if (n == et_stop_frame + 30) {
                /* 30 frames at point-blank: the decoded state 4 has no
                 * player-distance test — the disguise must hold */
                et_check(em_enemy_alive() == 1 &&
                         em_enemy_state(0) == EM_ENEMY_IDLE,
                         "disguise holds at point-blank range (no "
                         "proximity trigger in the engine)");
                et_check(em_weapon_shots() == 0, "no shot fired");
                /* the decoded trigger: a damage mailbox write (the
                 * same +0x36 write a knife impact does — the real
                 * knife path is EM_MELEE_TEST's job) */
                em_enemy_damage(0, 5);
                et_hit_sent = n;
            }
        } else if (!g.et_burst_frame) {
            if (em_enemy_state(0) == EM_ENEMY_FREE) {
                /* the crate burst (slot 0 freed) — the next IDLE
                 * mailbox poll after the write */
                g.et_burst_frame = n;
                g.et_bd          = et_dist();
                et_check(n <= et_hit_sent + 2,
                         "burst on the IDLE mailbox poll");
                et_check(em_enemy_alive() == 2 &&
                         em_enemy_kind(1) == EM_ENEMY_KIND_BUG &&
                         em_enemy_kind(2) == EM_ENEMY_KIND_BUG,
                         "nest-group bugs (2) hatched by the burst "
                         "(s68 — never a worm)");
                float cp[3] = { 0, 0, 0 }, bp[3] = { 0, 0, 0 };
                em_enemy_pos(0, cp);
                for (int k = 1; k <= 2; k++) {
                    em_enemy_pos(k, bp);
                    et_check(fabsf(bp[0] - cp[0]) +
                             fabsf(bp[1] - cp[1]) +
                             fabsf(bp[2] - cp[2]) < 2.5f,
                             "bug hatched at the crate (the "
                             "record-offset ring stand-in)");
                }
                et_check(em_enemy_hp(1) == 15 && em_enemy_hp(2) == 15,
                         "variant-A HP 15 (func_00128390)");
                g.et_wd0 = g.et_wd_min = et_dist_i(1);
            } else if (n >= et_hit_sent + 60) {
                et_check(0, "crate burst after the damage write");
                et_finish("crate run");
            }
        } else {
            float wd = et_dist_i(1);
            if (wd < g.et_wd_min) g.et_wd_min = wd;
            if (em_enemy_state(1) == EM_ENEMY_ATTACK)
                g.et_worm_atk = 1;   /* bugs engaged (s76 approach brain) */
            if (!et_flinch_sent) {
                if (n == g.et_burst_frame + 30) {
                    et_check(g.et_worm_atk,
                             "bugs engaged after the burst (the "
                             "s76 approach brain)");
                    /* the bug mailbox ladder, step 1: a knife-light-
                     * sized 5 must FLINCH, not kill (HP 15) */
                    em_enemy_damage(1, 5);
                    et_flinch_sent = n;
                }
            } else if (!et_kill_sent) {
                if (n == et_flinch_sent + 5) {
                    et_check(em_enemy_state(1) == EM_ENEMY_ATTACK &&
                             em_enemy_hp(1) == 10,
                             "nonlethal hit FLINCHES the bug — HP 15 "
                             "-> 10 (every-tick mailbox "
                             "func_00128B80)");
                    /* step 2: a heavy-stab-sized 15 out-kills the
                     * remaining 10 */
                    em_enemy_damage(1, 15);
                    et_kill_sent = n;
                }
            } else if (n == et_kill_sent + 5) {
                et_check(em_enemy_state(1) == EM_ENEMY_FREE,
                         "lethal mailbox kills the bug (handler "
                         "func_00129FC0; corpse fade)");
                et_check(em_enemy_alive() == 1 &&
                         em_enemy_state(2) == EM_ENEMY_ATTACK,
                         "the other bug lives on");
                printf("enemy test (crate run): burst dist %.1f, bug "
                       "dist %.1f -> min %.1f, health %.0f, draw "
                       "slots %d\n", g.et_bd, g.et_wd0, g.et_wd_min,
                       g.status.health, em_enemy_count());
                et_finish("crate run");
            }
        }
    } else {
        if (!g.et_hit_frame) {
            if (g.status.health < g.et_health0) {
                g.et_hit_frame = n;
            } else if (n >= 600) {
                et_check(0, "worm reached the player by frame 600 "
                            "(approach 90 t + stalk + windup + lunge)");
                et_finish("contact run");
            }
        } else if (n == g.et_hit_frame + 5) {
            et_check(g.status.health == g.et_health0 - 15.0f,
                     "lunge connect dealt the decoded latch (15)");
            et_check(em_enemy_alive() == 0,
                     "worm burst on the lunge (suicide path)");
            et_finish("contact run");
        }
    }
}

/* EM_SFX_TEST=1 — one-shot SFX mixer self-test (em_sfx.c). Fires THREE
 * one-shots 10 frames (1/6 s) apart — weapon draw 0x162, fire 0x164,
 * enemy death 0x7D8, all mapped by the user's assets/sfx/sfx.txt — so
 * their voices OVERLAP in the shared render callback (over the BGM when
 * EM_BGM / the manifest started one); then (frame 40) asserts FIVE
 * synthetic pan/attenuation vectors against the decoded func_001FBF50
 * math (center/full at the player, range cull, hard-left at 90 deg,
 * the behind-the-camera phase inversion, the 18-u proximity ramp —
 * em_sfx.h "POSITIONAL AUDIO"), exercises the play-path range cull
 * (frame 50), bursts 60 plays to prove the 48-voice-budget OLDEST
 * steal with zero drops (frame 60), and prints the audio thread's
 * counters + PASS/FAIL at frame 120. Needs the registry: without
 * sfx.txt the module is disabled by design and the test reports the
 * FAIL. */
/* EM_PAUSE_TEST=1 — STATUS-SCREEN PAUSE self-test (the 2026-06-11
 * fidelity note: the open status menu PAUSES the game — gameplay_frame
 * gates the whole world update on em_hud_is_open()) PLUS the decoded
 * MENU-LOCK gate (em_door.h "THE TWO LOCKS": the engine's open poll
 * func_001AE7E0 refuses while the fade machine runs — RE-VERIFIED in
 * src/func_001AE7E0.c (NEARMISS 99.10%, logic authoritative): the
 * classifier returns 0 (= blocked) on `if (D_0028A9A0 != 0) return 0;`,
 * D_0028A9A0 being the same fade-machine state the game-over chain
 * polls for idle/hold-black; the lock ends at
 * fade-in completion, BEFORE the arrival walk-out finishes — so the
 * menu opens mid-walk-out while movement is still locked). Spawned at
 * the door-test corridor position (72, 0, -225 facing -X, the west
 * double door ahead) and scripted through the real input API:
 *
 *   frame    5      TRIANGLE -> the status screen opens (no transit:
 *                   the menu lock is clear)
 *   frames  10..70  hold 'w' (full stick = run) — the world is halted:
 *                   the player must NOT move (assert at 70)
 *   frame   70      TRIANGLE -> close; release 'w'
 *   frame   80      assert closed; hold 'w' again
 *   frame  140      assert the player MOVED (the held key that was dead
 *                   under the pause now runs him onto the x = 60
 *                   boundary-wall radius stop, ~7.5 u); release
 *   frame  150      CROSS -> west-door transit (goto tail ignored under
 *                   EM_PAUSE_TEST: same-scene re-place). Timeline:
 *                   staging ~161, commit ~231, black + re-place ~295,
 *                   fade-in done ~359, walk-out end ~405
 *   frame  156      assert the MENU LOCK engaged with the kickoff
 *   frame  320      TRIANGLE press MID-FADE-IN -> must be DROPPED (the
 *                   func_001AE7E0 fade gate); assert closed at 326
 *   frame  380      MID-WALK-OUT: assert menu lock OFF + movement lock
 *                   ON, then TRIANGLE -> the menu MUST OPEN over the
 *                   running walk-out (assert open at 386; record pos)
 *   frame  400      assert the open menu FROZE the walk-out (pos
 *                   unchanged 386..400); TRIANGLE -> close
 *   frame  410      assert closed (walk-out resumes)
 *   frame  470      assert movement unlocked (walk-out completed after
 *                   the resume) and the player kept walking past the
 *                   frozen pos -> PASS/FAIL, quit
 */
static void pause_test_script(void)
{
    static float pp[2], wp_x;
    static int ok_open, ok_frozen, ok_closed;
    static int ok_lock_on, ok_drop, ok_split, ok_midopen, ok_wfrozen,
               ok_reclosed;
    int n = g.frame_no;
    if (n == 5) {
        move_test_inject('i', 1);          /* TRIANGLE — open */
    } else if (n == 7) {
        move_test_inject('i', 0);
    } else if (n == 10) {
        ok_open = em_hud_is_open();
        pp[0] = g.pos[0];
        pp[1] = g.pos[2];
        /* Door approach uses 'a' (LEFT), not 'w': under the BYTE-FAITHFUL
         * move basis (atan2(sx,-sy) + cam_yaw + pi/2) the spawn's -X-facing
         * chase camera makes LEFT head straight -X at the door, while 'w'
         * walks 90 deg off (see door_test_script's WALK-UP note). Here the
         * world is PAUSED so the key is dead anyway, but keeping it 'a'
         * matches the un-paused approach that re-runs at n=80. */
        move_test_inject('a', 1);          /* held under the pause */
    } else if (n == 70) {
        ok_frozen = fabsf(g.pos[0] - pp[0]) < 1e-4f &&
                    fabsf(g.pos[2] - pp[1]) < 1e-4f;
        move_test_inject('a', 0);
        move_test_inject('i', 1);          /* TRIANGLE — close */
    } else if (n == 72) {
        move_test_inject('i', 0);
    } else if (n == 80) {
        ok_closed = !em_hud_is_open();
        move_test_inject('a', 1);          /* must move now (-X to the door) */
    } else if (n == 140) {
        move_test_inject('a', 0);          /* parked on the boundary */
    } else if (n == 150) {
        move_test_inject('k', 1);          /* CROSS — door transit */
    } else if (n == 152) {
        move_test_inject('k', 0);
    } else if (n == 156) {
        ok_lock_on = em_door_menu_locked();
    } else if (n == 320) {
        move_test_inject('i', 1);          /* TRIANGLE mid-fade-in */
    } else if (n == 322) {
        move_test_inject('i', 0);
    } else if (n == 326) {
        ok_drop = !em_hud_is_open() && em_door_menu_locked();
    } else if (n == 380) {
        /* fade-in done (~359), walk-out still running (~405) */
        ok_split = !em_door_menu_locked() && em_door_movement_locked();
        move_test_inject('i', 1);          /* TRIANGLE mid-walk-out */
    } else if (n == 382) {
        move_test_inject('i', 0);
    } else if (n == 386) {
        ok_midopen = em_hud_is_open() && em_door_movement_locked();
        wp_x = g.pos[0];
    } else if (n == 400) {
        ok_wfrozen = fabsf(g.pos[0] - wp_x) < 1e-4f;
        move_test_inject('i', 1);          /* TRIANGLE — close */
    } else if (n == 402) {
        move_test_inject('i', 0);
    } else if (n == 410) {
        ok_reclosed = !em_hud_is_open();
    } else if (n == 470) {
        float dx = g.pos[0] - pp[0], dz = g.pos[2] - pp[1];
        int moved   = dx * dx + dz * dz > 25.0f;   /* > 5 u of motion */
        int ok_done = !em_door_movement_locked() &&
                      g.pos[0] < wp_x - 0.5f;   /* kept walking out */
        int ok = ok_open && ok_frozen && ok_closed && moved &&
                 ok_lock_on && ok_drop && ok_split && ok_midopen &&
                 ok_wfrozen && ok_reclosed && ok_done;
        printf("pause test: opened %s, frozen under held stick %s, "
               "closed %s, resumed (moved %.1f u) %s, menu lock at "
               "kickoff %s, mid-fade press dropped %s, two-lock split "
               "mid-walk-out %s, menu OPEN over the walk-out %s, "
               "walk-out frozen under the menu %s, re-closed %s, "
               "walk-out completed after resume (x %.2f < %.2f) %s — "
               "%s\n",
               ok_open ? "ok" : "FAILED", ok_frozen ? "ok" : "FAILED",
               ok_closed ? "ok" : "FAILED",
               sqrtf(dx * dx + dz * dz), moved ? "ok" : "FAILED",
               ok_lock_on ? "ok" : "FAILED", ok_drop ? "ok" : "FAILED",
               ok_split ? "ok" : "FAILED", ok_midopen ? "ok" : "FAILED",
               ok_wfrozen ? "ok" : "FAILED",
               ok_reclosed ? "ok" : "FAILED",
               g.pos[0], wp_x, ok_done ? "ok" : "FAILED",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_PICKUP_TEST=1 — pickup collect / inventory / despawn / persistence
 * self-test (the 2026-06-11 pickup decode, em_pickup.h). Runs on the
 * default scene (assets/scene — needs the exported assets tree for the
 * reload leg, like EM_DOOR_TEST): two SYNTHETIC pickups are injected
 * by the script itself so the assertions are placement-independent:
 *
 *   A: type 0x10 (SPR4 MAGAZINE — the func_001C40B0 case-0x10 ammo
 *      path), 4 u IN FRONT of the spawn (inside the 7-u facing
 *      auto-pass ring), uid 0xF01
 *   B: type 0x20 (MTS VACCINE), 9 u BEHIND the spawn — inside the
 *      10-u ring but outside the auto ring with the player facing
 *      AWAY: the pi/4 facing gate must hold, uid 0xF02
 *
 *   frame  1   inject A + B; record the reserve
 *   frame  5   CROSS -> the scan arms A (nearest passer; B fails the
 *              facing gate); the take fires after the 2 scripted
 *              frames
 *   frame 12   assert: count[0x10] == 1, pack counter == 1, taken bit
 *              0xF01 set, reserve == r0 + 30 (the +30/pack applied
 *              through the holstered weapon), count[0x20] == 0
 *   frame 20   CROSS again -> assert at 28: counts unchanged (A
 *              DESPAWNED — no double collect; B's facing gate held)
 *   frame 40   scene RELOAD (em_game_scene_switch into the same dir —
 *              the engine's area re-entry); re-inject A and B: A must
 *              be SUPPRESSED (rc -2, the cond-1 taken check), B
 *              re-places
 *   frame 45   assert + PASS/FAIL + quit. */
static void pickup_test_script(void)
{
    static int ok_place, ok_take, ok_gate, ok_reload, ok_suppress;
    static float pa[3], pb[3];
    int n = g.frame_no;
    if (n == 1) {
        float fy = g.spawn_yaw;
        pa[0] = g.spawn[0] + 4.0f * sinf(fy);
        pa[1] = g.spawn[1];
        pa[2] = g.spawn[2] + 4.0f * cosf(fy);
        pb[0] = g.spawn[0] - 9.0f * sinf(fy);
        pb[1] = g.spawn[1];
        pb[2] = g.spawn[2] - 9.0f * cosf(fy);
        int a = em_pickup_add(em_frame_gfx(), g.scene_dir,
                              EM_PICKUP_TYPE_MAG, pa, 0.0f, 0xF01,
                              NULL, 0);
        int b = em_pickup_add(em_frame_gfx(), g.scene_dir, 0x20, pb,
                              0.0f, 0xF02, NULL, 0);
        g.pt_slot = a;
        g.pt_r0   = em_weapon_reserve();
        ok_place  = a >= 0 && b >= 0 &&
                    em_pickup_item_count(EM_PICKUP_TYPE_MAG) == 0 &&
                    !em_pickup_taken(0xF01);
        if (!ok_place) g.pt_fail++;
    } else if (n == 5) {
        move_test_inject('k', 1);          /* CROSS — the use press */
    } else if (n == 7) {
        move_test_inject('k', 0);
    } else if (n == 12) {
        ok_take = em_pickup_item_count(EM_PICKUP_TYPE_MAG) == 1 &&
                  em_pickup_mag_packs() == 1 &&
                  em_pickup_taken(0xF01) &&
                  em_weapon_reserve() == (int16_t)(g.pt_r0 + 30) &&
                  em_pickup_item_count(0x20) == 0;
        if (!ok_take) {
            g.pt_fail++;
            printf("pickup test: CHECK FAILED — take (count %u packs "
                   "%u taken %d reserve %d/%d count20 %u)\n",
                   em_pickup_item_count(EM_PICKUP_TYPE_MAG),
                   em_pickup_mag_packs(), em_pickup_taken(0xF01),
                   em_weapon_reserve(), g.pt_r0 + 30,
                   em_pickup_item_count(0x20));
        }
    } else if (n == 20) {
        move_test_inject('k', 1);
    } else if (n == 22) {
        move_test_inject('k', 0);
    } else if (n == 28) {
        ok_gate = em_pickup_item_count(EM_PICKUP_TYPE_MAG) == 1 &&
                  em_pickup_item_count(0x20) == 0;
        if (!ok_gate) g.pt_fail++;
    } else if (n == 40) {
        ok_reload = em_game_scene_switch(g.scene_dir) == 0;
        if (!ok_reload) g.pt_fail++;
        int a = em_pickup_add(em_frame_gfx(), g.scene_dir,
                              EM_PICKUP_TYPE_MAG, pa, 0.0f, 0xF01,
                              NULL, 0);
        int b = em_pickup_add(em_frame_gfx(), g.scene_dir, 0x20, pb,
                              0.0f, 0xF02, NULL, 0);
        ok_suppress = a == -2 && b >= 0;
        if (!ok_suppress) g.pt_fail++;
    } else if (n == 45) {
        int ok_persist = em_pickup_taken(0xF01) &&
                         !em_pickup_taken(0xF02) &&
                         em_pickup_item_count(EM_PICKUP_TYPE_MAG) == 1;
        int ok = ok_place && ok_take && ok_gate && ok_reload &&
                 ok_suppress && ok_persist && g.pt_fail == 0;
        printf("pickup test: placed %s, collected (+count/+packs/+30 "
               "reserve/taken bit) %s, despawn + facing gate %s, "
               "scene reload %s, taken-uid respawn suppressed %s, "
               "inventory persisted %s — %s\n",
               ok_place ? "ok" : "FAILED", ok_take ? "ok" : "FAILED",
               ok_gate ? "ok" : "FAILED", ok_reload ? "ok" : "FAILED",
               ok_suppress ? "ok" : "FAILED",
               ok_persist ? "ok" : "FAILED", ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_EXAMINE_TEST=1 — examine arm / input pause / radio line / camera
 * cue / chain presenter / cooldown / re-arm self-test (the 2026-06-11
 * examine decode, em_examine.h). Runs on the default scene; THREE
 * SYNTHETIC examine objects are injected so the assertions are
 * placement-independent (the real records ship in the scene manifests
 * — export_level.py --examine):
 *
 *   A: GLOBAL line 0x1A ("Switch / No power...", 148 frames — the
 *      AREA11 snow-switch refusal shape) 4 u IN FRONT of the spawn
 *      (the 7-u facing auto-pass ring), with an op00-style camera cue
 *      and the decoded 300-frame cooldown
 *   B: AREA-bank chain (the office/drawbridge shape: text/gap/text +
 *      terminal, pre-delay 5) 12 u in front — inside the 20-u ring,
 *      a passer, but A is nearer (nearest-wins leg)
 *   C: chain 9 u BEHIND — the pi/4 facing gate must exclude it
 *
 *   frame   1   inject A + B + C
 *   frame   5   CROSS -> the scan arms A (nearest passer)
 *   frame 100   assert: sequence active on A, input locked, the radio
 *               machine presenting, the camera pinned at A's cue
 *   frame 170   assert: sequence over (148 + terminal), input free,
 *               camera restored (eye off the cue), A cooling down
 *   frame 172   CROSS -> A refused (cooldown) + C refused (facing):
 *               B arms; chain = delay 5 + 20 text + 10 gap + 15 text
 *               + 1 terminal -> done ~frame 225
 *   frame 190   assert: active on B, locked, radio machine IDLE (the
 *               chain rides em_examine's own presenter)
 *   frame 240   assert: unlocked again
 *   frame 245   CROSS -> B RE-ARMS (no cooldown — the engine re-arm)
 *   frame 250   assert + PASS/FAIL + quit. */
static void examine_test_script(void)
{
    static int   ok_arm, ok_lock, ok_cam, ok_done, ok_chain, ok_gate,
                 ok_rearm;
    static int   slot_a = -1, slot_b = -1;
    static float cue[3];
    int n = g.frame_no;
    if (n == 1) {
        float fy = g.spawn_yaw;
        float pa[3] = { g.spawn[0] + 4.0f * sinf(fy), g.spawn[1],
                        g.spawn[2] + 4.0f * cosf(fy) };
        float pb[3] = { g.spawn[0] + 12.0f * sinf(fy), g.spawn[1],
                        g.spawn[2] + 12.0f * cosf(fy) };
        float pc[3] = { g.spawn[0] - 9.0f * sinf(fy), g.spawn[1],
                        g.spawn[2] - 9.0f * cosf(fy) };
        cue[0] = pa[0];
        cue[1] = pa[1] + 9.0f;   /* an office-cue-shaped raised eye */
        cue[2] = pa[2] + 6.0f;
        slot_a = em_examine_add(pa, 0.0f, EM_EXAMINE_RADIUS,
                                EM_EXAMINE_DY, 0x1A, 0, 300, cue);
        slot_b = em_examine_add(pb, 0.0f, EM_EXAMINE_RADIUS,
                                EM_EXAMINE_DY, -1, 5, 0, NULL);
        int c  = em_examine_add(pc, 0.0f, EM_EXAMINE_RADIUS,
                                EM_EXAMINE_DY, -1, 0, 0, NULL);
        em_examine_text(slot_b, 20, 10, "EXAMINE TEST ONE");
        em_examine_text(slot_b, 15, 0, "EXAMINE\\nTEST TWO");
        em_examine_text(c, 20, 0, "WRONG OBJECT");
        if (slot_a < 0 || slot_b < 0 || c < 0) g.ex_fail++;
    } else if (n == 5 || n == 172 || n == 245) {
        move_test_inject('k', 1);          /* CROSS — the use press */
    } else if (n == 7 || n == 174 || n == 247) {
        move_test_inject('k', 0);
    } else if (n == 100) {
        float ce[3], ct[3];
        ok_arm  = em_examine_active() == slot_a;
        ok_lock = em_examine_input_locked() && em_hud_radio_active();
        ok_cam  = em_examine_camera(ce, ct) &&
                  fabsf(g.cam.eye[0] - cue[0]) < 1e-3f &&
                  fabsf(g.cam.eye[1] - cue[1]) < 1e-3f &&
                  fabsf(g.cam.eye[2] - cue[2]) < 1e-3f;
        if (!ok_arm || !ok_lock || !ok_cam) {
            g.ex_fail++;
            printf("examine test: CHECK FAILED — present (active %d/%d "
                   "lock %d radio %d cam %d eye %.2f %.2f %.2f)\n",
                   em_examine_active(), slot_a,
                   em_examine_input_locked(), em_hud_radio_active(),
                   ok_cam, g.cam.eye[0], g.cam.eye[1], g.cam.eye[2]);
        }
    } else if (n == 170) {
        ok_done = !em_examine_input_locked() && !em_hud_radio_active() &&
                  em_examine_active() < 0 &&
                  fabsf(g.cam.eye[1] - cue[1]) > 1e-3f;
        if (!ok_done) g.ex_fail++;
    } else if (n == 190) {
        ok_chain = em_examine_active() == slot_b &&
                   em_examine_input_locked() && !em_hud_radio_active();
        ok_gate  = 1;   /* C armed would have made active == c != b */
        if (!ok_chain) {
            g.ex_fail++;
            printf("examine test: CHECK FAILED — chain (active %d/%d "
                   "lock %d radio %d)\n", em_examine_active(), slot_b,
                   em_examine_input_locked(), em_hud_radio_active());
        }
    } else if (n == 240) {
        if (em_examine_input_locked()) g.ex_fail++;
    } else if (n == 250) {
        ok_rearm = em_examine_active() == slot_b;
        if (!ok_rearm) g.ex_fail++;
        int ok = ok_arm && ok_lock && ok_cam && ok_done && ok_chain &&
                 ok_gate && ok_rearm && g.ex_fail == 0;
        printf("examine test: armed (nearest, CROSS edge) %s, input "
               "paused + radio line %s, camera cue pinned %s, timed "
               "dismiss + restore + cooldown %s, area chain (facing "
               "gate held) %s, re-arm %s — %s\n",
               ok_arm ? "ok" : "FAILED", ok_lock ? "ok" : "FAILED",
               ok_cam ? "ok" : "FAILED", ok_done ? "ok" : "FAILED",
               ok_chain ? "ok" : "FAILED", ok_rearm ? "ok" : "FAILED",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

static void sfx_test_script(void)
{
    /* Decoded-math assertion state (frame 40/50/60 below). The synthetic
     * listener is overwritten by the SAME frame's em_sfx_listener call
     * at the gameplay-frame tail, so gameplay audio is untouched. */
    static int ok_gain = -1;
    static int live_at_burst;
    int n = g.frame_no;
    if (n == 10 || n == 20 || n == 30) {
        static const unsigned ids[3] = { EM_SFX_WPN_DRAW, EM_SFX_WPN_FIRE,
                                         EM_SFX_ENEMY_DEATH };
        em_sfx_play(ids[n / 10 - 1]);
    } else if (n == 40) {
        /* PAN/ATTENUATION assertions against the func_001FBF50 decode
         * (em_sfx.h "POSITIONAL AUDIO"): synthetic listener at the
         * origin (player == camera eye, yaw 0 = facing +Z), expected
         * gains precomputed by hand from the formulas. */
        const float o[3] = { 0, 0, 0 };
        em_sfx_listener(o, o, 0.0f);
        float l, r;
        int   okc = 1;
        /* 1. at the player: d=0 -> vol=1, k=0 -> t=+1 -> center/full */
        okc &= em_sfx_compute_gains(o, 300.0f, &l, &r) == 1 &&
               fabsf(l - 1.0f) < 1e-4f && fabsf(r - 1.0f) < 1e-4f;
        /* 2. beyond the radius: engine play_sound -1 (not submitted) */
        { const float p[3] = { 0, 0, 400 };
          okc &= em_sfx_compute_gains(p, 300.0f, &l, &r) == 0; }
        /* 3. 90 deg LEFT (+X = bearing +pi/2 = the engine's near-LEFT
         * side test) at d=150, r=300: vol = sin(pi/4) = 0.70711,
         * c = cos(pi/2) = 0, k = 1 -> t = 0: hard left, far silent */
        { const float p[3] = { 150, 0, 0 };
          okc &= em_sfx_compute_gains(p, 300.0f, &l, &r) == 1 &&
                 fabsf(l - 0.70711f) < 1e-3f && fabsf(r) < 1e-3f; }
        /* 4. BEHIND at d=100: vol = sin(pi/2 * 200/300) = 0.86603,
         * c = -1 -> t = -1: far channel PHASE-INVERTED at full */
        { const float p[3] = { 0, 0, -100 };
          okc &= em_sfx_compute_gains(p, 300.0f, &l, &r) == 1 &&
                 fabsf(l - 0.86603f) < 1e-3f &&
                 fabsf(r + 0.86603f) < 1e-3f; }
        /* 5. proximity pan ramp: 45 deg left at d=9 (= 18/2): k=0.5,
         * c = cos(pi/4) -> t = c^5*k + (1-k) = 0.17678*0.5 + 0.5 =
         * 0.58839; vol = sin(pi/2 * 291/300) = 0.99889. (Not the 90 deg
         * axis: there cosf lands on +-4e-8 and the engine's own sign
         * branch — and ours — flips on the noise bit.) */
        { const float p[3] = { 6.3640f, 0, 6.3640f };
          okc &= em_sfx_compute_gains(p, 300.0f, &l, &r) == 1 &&
                 fabsf(l - 0.99889f) < 1e-3f &&
                 fabsf(r - 0.58774f) < 1e-3f; }
        ok_gain = okc;
    } else if (n == 50) {
        /* RANGE CULL through the play path: a mapped id beyond its
         * radius must not submit (engine -1) — counted, not played. */
        const float o[3] = { 0, 0, 0 }, far_p[3] = { 0, 0, 400 };
        em_sfx_listener(o, o, 0.0f);
        em_sfx_play_at(EM_SFX_WPN_FIRE, far_p, 300.0f);
    } else if (n == 60) {
        /* VOICE STEALING at the engine's 48 budget (em_sfx.h "VOICE
         * STEALING"): burst 60 center plays in one frame — every play
         * past 48 live voices kills the then-oldest; the 16 spare
         * physical slots absorb the one-callback kill latency, so
         * NOTHING drops. Expected steals = 12 + (voices still ringing
         * from frames 10..30: WAV-length dependent, 0..3). */
        live_at_burst = em_sfx_plays();   /* accepted so far (3) */
        for (int i = 0; i < 60; i++)
            em_sfx_play(EM_SFX_WPN_FIRE);
    } else if (n == 120) {
        long mixed  = em_sfx_frames_mixed();
        int  peak   = em_sfx_max_concurrent();
        int  steals = em_sfx_steals();
        /* steals = max(0, live_at_burst_voices + 60 - 48); the three
         * early voices may have ended -> accept the 12..15 band. */
        int  ok = em_sfx_sound_count() > 0 && em_sfx_plays() == 63 &&
                  em_sfx_drops() == 0 && em_sfx_culls() == 1 &&
                  steals >= 12 && steals <= 15 &&
                  ok_gain == 1 && mixed > 0 && peak >= 2;
        printf("sfx test: %d sound(s) loaded, %d play(s) (%d dropped), "
               "pan/attenuation vectors %s, range cull %d, steals %d "
               "(48-voice budget, 60-play burst over %d ringing), %ld "
               "voice frames mixed, peak %d concurrent voice(s) — %s\n",
               em_sfx_sound_count(), em_sfx_plays(), em_sfx_drops(),
               ok_gain == 1 ? "ok" : "FAILED", em_sfx_culls(), steals,
               live_at_burst, mixed, peak, ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_CAMREGION_TEST=1 — FIXED-CAMERA REGION self-test (the mode-0
 * director decode, FINDINGS "MODE-0 CAMERA DIRECTOR DECODED"). Scene
 * init REPLACES the scene's region list with one SYNTHETIC,
 * CLEARLY-FLAGGED test region 5 u down +Z of the spawn (see
 * ingame_frame_machine case 0) so the run is deterministic and
 * spawn-local — the machinery under test is exactly what scene_snow's
 * REAL exported region (AREA06, D_0024A5F0[2]) and the office scene's
 * REAL supply-room line (the spawn-record camera, this session's
 * decode; EM_CAPTURE_SUPPLY samples it in place) drive. Adaptive
 * phase machine:
 *
 *   phase 0  hold 'a' (run +Z — faithful-basis forward, camera behind);
 *            the frame the region engages (g.cam_region_on — one frame
 *            after the crossing), release. (Safety: FAIL+quit by frame
 *            180 if it never engages, so a movement-basis change can
 *            never re-hang this test.)
 *   phase 1  mark+2: assert the camera eye sits EXACTLY at the room
 *            spec (enter -> camera at spec).
 *   phase 2  mark+10..12 tap 'q' (L1); mark+30: assert the eye is
 *            STILL at the spec and no recenter armed (L1 is a NO-OP
 *            in a specified-camera room).
 *   phase 3  mark+40 hold 'e' (R1 aim): the aim camera takes over.
 *            mark+130: assert the eye LEFT the spec (> 6 u — aim still
 *            works in fixed rooms) and release.
 *   phase 4  mark+132: assert the eye is back at the spec EXACTLY,
 *            two frames after release — the chase cap (4 u/frame)
 *            could never cover the aim distance that fast, so equality
 *            here proves the snap-back is INSTANT (no lerp): the
 *            user-observed behavior. PASS/FAIL, quit.
 */
static void camregion_test_script(void)
{
    static int phase, mark, ok_enter, ok_l1, ok_aim;
    static float d_aim;
    const EmCamRegion *spec = &g.camregion[0];
    float de[3] = { g.cam.eye[0] - spec->eye[0],
                    g.cam.eye[1] - spec->eye[1],
                    g.cam.eye[2] - spec->eye[2] };
    float d   = sqrtf(de[0] * de[0] + de[1] * de[1] + de[2] * de[2]);
    int   at  = d < 1e-5f;
    int   n   = g.frame_no;

    switch (phase) {
        case 0:
            /* 'a' = forward (into the +Z synthetic region) under the
             * BYTE-FAITHFUL move basis with the chase camera directly
             * behind the spawn — same remap the slider/door tests took
             * 2026-06-17. 'w' here drives +X (cam_yaw 0 -> 'w' heads
             * cam_yaw + pi/2), NEVER crossing the +Z region, which used to
             * HANG phase 0 forever. */
            if (n == 0) move_test_inject('a', 1);
            if (g.cam_region_on) {
                move_test_inject('a', 0);
                mark  = n;
                phase = 1;
            } else if (n >= 180) {
                /* SAFETY: a full run +Z reaches the 5-u region in well
                 * under 180 frames. If it has not engaged, FAIL loudly
                 * rather than loop forever (the old 'w' bug). */
                printf("camregion test: CHECK FAILED — region never "
                       "engaged by frame %d (player never reached the "
                       "synthetic region)\n", n);
                fflush(stdout);
                em_frame_request_quit();
            }
            break;
        case 1:
            if (n == mark + 2) {
                ok_enter = g.cam_region_on && at;
                if (!ok_enter)
                    printf("camregion test: CHECK FAILED — enter (on %d, "
                           "eye off spec by %.3f)\n", g.cam_region_on, d);
                phase = 2;
            }
            break;
        case 2:
            if      (n == mark + 10) move_test_inject('q', 1);  /* L1 */
            else if (n == mark + 12) move_test_inject('q', 0);
            else if (n == mark + 30) {
                ok_l1 = g.cam_region_on && at && !g.cam_recenter;
                if (!ok_l1)
                    printf("camregion test: CHECK FAILED — L1 not a "
                           "no-op (on %d, recenter %d, eye off spec by "
                           "%.3f)\n", g.cam_region_on, g.cam_recenter, d);
                phase = 3;
            }
            break;
        case 3:
            if      (n == mark + 40) move_test_inject('e', 1);  /* R1 */
            else if (n == mark + 130) {
                d_aim  = d;
                ok_aim = !g.cam_region_on && d > 6.0f;
                if (!ok_aim)
                    printf("camregion test: CHECK FAILED — aim camera "
                           "(on %d, eye moved %.3f u, need > 6)\n",
                           g.cam_region_on, d);
                move_test_inject('e', 0);   /* release: must snap NOW */
                phase = 4;
            }
            break;
        case 4:
            if (n == mark + 132) {
                int ok_snap = g.cam_region_on && at;
                int ok = ok_enter && ok_l1 && ok_aim && ok_snap;
                printf("camregion test: enter->spec %s, L1 no-op %s, "
                       "aim moved %.1f u %s, release snapped to spec in "
                       "<= 2 frames (off by %.6f) %s — %s\n",
                       ok_enter ? "ok" : "FAILED",
                       ok_l1 ? "ok" : "FAILED", d_aim,
                       ok_aim ? "ok" : "FAILED", d,
                       ok_snap ? "ok" : "FAILED", ok ? "PASS" : "FAIL");
                fflush(stdout);
                em_frame_request_quit();
            }
            break;
    }
}

/* EM_CINE_TEST=1 — AREA-11 OPENING DIRECTOR self-test (the D_00810813
 * step machine). Drives the director through its triggers WITHOUT a
 * real new-game playthrough (no headless walk into the high-Y zones),
 * proving: (a) DORMANCY at spawn, (b) a beat TRIGGERS on entering its
 * zone, (c) the player is LOCKED and the camera is OVERRIDDEN to the
 * keyframe, (d) held input does NOT move the player during the beat,
 * (e) the beat COMPLETES, the step ADVANCES, and CONTROL RETURNS (no
 * soft-lock), (f) the camera RESTORES to the normal chase. It uses the
 * SHORT beat 2 (single 1-frame keyframe) for the full completion cycle,
 * then re-arms beat 1 to confirm a second trigger + music cue.
 *
 *   frame 0..2  : at spawn — assert the director is DORMANT (no lock,
 *                 no override) — movement-v3 owns the frame.
 *   frame 3     : force step 0x20, teleport into beat-2 zone center,
 *                 hold 'w' (stick up) to prove the lock kills input.
 *   frame 5     : assert TRIGGERED — cine_active, busy-lock, camera eye
 *                 == beat-2 keyframe eye, step latched at 0x20.
 *   ~frame 8    : assert COMPLETED — cine_active 0, step == 0xFF,
 *                 control returned (not busy), player did NOT drift.
 *   frame 12    : re-arm step 0x10, teleport into beat-1 zone — assert
 *                 a second beat triggers (proves re-entrancy + music).
 *   frame 20    : release; assert the chase camera restored. PASS/quit.
 */
static void cine_test_script(void)
{
    static float bx, bz, by;        /* beat-1 zone center (lock-drift ref) */
    int n = g.frame_no;

    switch (g.ct_phase) {
        case 0:
            if (n <= 2) {
                /* DORMANCY: at spawn nothing should have armed. */
                if (g.cine_active || em_game_player_interact_busy()) {
                    printf("cine test: CHECK FAILED — director not "
                           "dormant at spawn (frame %d active %d busy %d)\n",
                           n, g.cine_active, em_game_player_interact_busy());
                    g.ct_fail++;
                }
            }
            if (n == 3) {
                /* Arm beat 1 (multi-keyframe, ~310 frames — long enough
                 * to observe the lock + camera + no-drift over many
                 * frames). Place the player in the zone center on its
                 * Y tier and hold 'w' (forward) — it must be dead. */
                g.cine_step = CINE_STEP_BEAT1;
                bx = 0.5f * (kCineBeats[1].x0 + kCineBeats[1].x1);
                bz = 0.5f * (kCineBeats[1].z0 + kCineBeats[1].z1);
                by = 270.0f;                 /* <= 275 gate */
                g.pos[0] = bx; g.pos[1] = by; g.pos[2] = bz;
                move_test_inject('w', 1);    /* hold forward — must be dead */
                g.ct_mark  = n;
                g.ct_phase = 1;
            }
            break;
        case 1:
            /* the tick arms on frame 3; the beat is active from frame 4 */
            if (n == g.ct_mark + 1) {
                const CineKey *k0 = &kCineBeats[1].key[0];
                int locked = g.cine_active && g.cine_beat == 1 &&
                             em_game_player_interact_busy();
                int cam_ok = fabsf(g.cam.eye[0] - k0->eye[0]) < 0.5f &&
                             fabsf(g.cam.eye[1] - k0->eye[1]) < 0.5f &&
                             fabsf(g.cam.eye[2] - k0->eye[2]) < 0.5f;
                if (!locked) {
                    printf("cine test: CHECK FAILED — beat 1 did not lock "
                           "(active %d beat %d busy %d)\n", g.cine_active,
                           g.cine_beat, em_game_player_interact_busy());
                    g.ct_fail++;
                }
                if (!cam_ok) {
                    printf("cine test: CHECK FAILED — camera not overridden "
                           "to keyframe 0 (eye %.1f,%.1f,%.1f vs %.1f,%.1f,"
                           "%.1f)\n", g.cam.eye[0], g.cam.eye[1], g.cam.eye[2],
                           k0->eye[0], k0->eye[1], k0->eye[2]);
                    g.ct_fail++;
                }
                g.ct_phase = 2;
            }
            break;
        case 2:
            /* over the locked window: the held 'w' must produce NO motion */
            if (g.cine_active && em_game_player_interact_busy()) {
                g.ct_locked_max++;
                float drift = fabsf(g.pos[0] - bx) + fabsf(g.pos[2] - bz);
                if (drift > 0.01f) {
                    printf("cine test: CHECK FAILED — player drifted %.3f u "
                           "while locked at frame %d (input leaked)\n",
                           drift, n);
                    g.ct_fail++;
                    g.ct_phase = 3;   /* report once */
                }
            }
            if (n == g.ct_mark + 60) {
                /* Force beat 1 to finish: jump to its last keyframe's end. */
                if (g.cine_active) {
                    g.cine_kf   = kCineBeats[1].n_key - 1;
                    g.cine_kf_t = kCineBeats[1].key[g.cine_kf].dur;
                }
                g.ct_phase = 3;
            }
            break;
        case 3:
            if (n == g.ct_mark + 63) {
                move_test_inject('w', 0);
                /* COMPLETION + CONTROL RETURN + STEP ADVANCE. */
                int returned = !g.cine_active &&
                               !em_game_player_interact_busy();
                int stepped  = g.cine_step == CINE_STEP_BEAT2;
                if (!returned) {
                    printf("cine test: CHECK FAILED — control did NOT return "
                           "after beat 1 (active %d busy %d) — SOFT-LOCK\n",
                           g.cine_active, em_game_player_interact_busy());
                    g.ct_fail++;
                }
                if (!stepped) {
                    printf("cine test: CHECK FAILED — step did not advance "
                           "to 0x20 (got %#x)\n", g.cine_step);
                    g.ct_fail++;
                }
                printf("cine test: beat 1 cycle OK — locked %d frames, "
                       "step %#x\n", g.ct_locked_max, g.cine_step);
                g.ct_phase = 4;
            }
            break;
        case 4:
            /* CAMERA RESTORE: a few frames after the beat the chase camera
             * must be following the player again (eye no longer pinned to
             * the keyframe; it tracks behind the spawn-tier player). */
            if (n == g.ct_mark + 70) {
                const CineKey *k0 = &kCineBeats[1].key[0];
                float off = fabsf(g.cam.eye[0] - k0->eye[0]) +
                            fabsf(g.cam.eye[2] - k0->eye[2]);
                if (off < 1.0f) {
                    printf("cine test: CHECK FAILED — camera still pinned to "
                           "the cinematic keyframe after restore (off "
                           "%.3f u)\n", off);
                    g.ct_fail++;
                }
                /* BEAT 2 SHORT-CYCLE: arm the last beat, confirm it
                 * triggers + completes to 0xFF (the fast full cycle). */
                g.cine_step = CINE_STEP_BEAT2;
                g.pos[0] = 0.5f * (kCineBeats[2].x0 + kCineBeats[2].x1);
                g.pos[1] = 280.0f;            /* <= 285 gate */
                g.pos[2] = 0.5f * (kCineBeats[2].z0 + kCineBeats[2].z1);
                g.ct_phase = 5;
            }
            break;
        case 5:
            if (n == g.ct_mark + 71) {        /* the arming frame */
                if (!(g.cine_active && g.cine_beat == 2)) {
                    printf("cine test: CHECK FAILED — beat 2 did not "
                           "trigger (active %d beat %d)\n",
                           g.cine_active, g.cine_beat);
                    g.ct_fail++;
                }
            }
            if (n == g.ct_mark + 76) {
                int done = !g.cine_active && g.cine_step == CINE_STEP_DONE;
                if (!done) {
                    printf("cine test: CHECK FAILED — beat 2 did not "
                           "complete to 0xFF (active %d step %#x)\n",
                           g.cine_active, g.cine_step);
                    g.ct_fail++;
                }
                printf("cine test: %s (%d failed check%s)\n",
                       g.ct_fail ? "FAIL" : "PASS", g.ct_fail,
                       g.ct_fail == 1 ? "" : "s");
                /* trip the headless quit (capture one frame, then exit). */
                if (!g.capture_path) g.capture_path = "/tmp/cine_test.bmp";
                g.capture_frame = n;
                g.ct_phase = 6;
            }
            break;
        default:
            break;
    }
}

/* EM_AIM_TEST=1 — MANUAL AIM STEER + MODE-1 AIM CAMERA self-test (the
 * func_0017ABA0 / func_00197D20 decodes). Scripted run through the
 * real input API:
 *
 *   frame 0      hold 'e' (R1): draw -> held aim pose 0x112.
 *   frame 30     assert the stance entry: blends centered (0.5/0.5),
 *                pose 0x112 committed, camera in mode-1 steady.
 *   31..240      hold 'w' (stick UP): INVERTED Y — the pitch blend
 *                must FALL (aim DOWN, "W = down") and clamp at 0.0;
 *                checked mid-travel (90) and at the clamp (240).
 *   240          camera steady-state geometry (emergent, not the
 *                formula): the eye ~30 u horizontally behind the
 *                facing, eye.y at the player.y+30 ceiling (full-down
 *                aim counter-raises the eye), the target BELOW the
 *                +19 rest height (it rides the aim line down).
 *   250..400     hold 's' (stick DOWN): pitch must RISE through
 *                center and clamp at the stance-0x31 limit 1.0.
 *   410+         hold 'a' (stick LEFT): the yaw blend pans the POSE
 *                first (the body yaw must NOT move while the blend is
 *                unsaturated), then past the limit the BODY turns —
 *                checked at 415 (pose-only) and 460 (body turning,
 *                blend pinned at 1.0). PASS/FAIL, quit.
 */
static void aim_test_script(void)
{
    static int   fail;
    static float yaw0;
    int n = g.frame_no;

    switch (n) {
        case 0:
            move_test_inject('e', 1);          /* R1: draw + aim */
            break;
        case 30: {
            int ok = g.aim_pitch == 0.5f && g.aim_yawb == 0.5f &&
                     em_game_anim_active() == 0x112 &&
                     g.cam.aim_phase == 2;
            if (!ok) {
                fail++;
                printf("aim test: CHECK FAILED — entry (pitch %.3f yaw "
                       "%.3f anim 0x%x phase %d)\n", g.aim_pitch,
                       g.aim_yawb, em_game_anim_active(),
                       g.cam.aim_phase);
            }
            move_test_inject('w', 1);          /* stick UP */
            break;
        }
        case 90:
            if (!(g.aim_pitch < 0.5f)) {
                fail++;
                printf("aim test: CHECK FAILED — stick UP did not aim "
                       "DOWN (pitch %.3f, inverted-Y broken)\n",
                       g.aim_pitch);
            }
            break;
        case 240: {
            int ok_clamp = g.aim_pitch == 0.0f;
            float fx = sinf(g.yaw), fz = cosf(g.yaw);
            float ex = g.pos[0] - fx * CAM_AIM_EYE_BACK;
            float ez = g.pos[2] - fz * CAM_AIM_EYE_BACK;
            float dx = g.cam.eye[0] - ex, dz = g.cam.eye[2] - ez;
            int ok_eye  = sqrtf(dx * dx + dz * dz) < 1.5f &&
                          fabsf(g.cam.eye[1] - (g.pos[1] + 30.0f)) < 1.5f;
            int ok_tgt  = g.cam.tgt[1] < g.pos[1] + CAM_AIM_TGT_UP - 5.0f;
            if (!ok_clamp || !ok_eye || !ok_tgt) {
                fail++;
                printf("aim test: CHECK FAILED — full-down state "
                       "(pitch %.3f; eye off (%.2f, y %.2f vs %.2f); "
                       "tgt y %.2f)\n", g.aim_pitch,
                       sqrtf(dx * dx + dz * dz), g.cam.eye[1],
                       g.pos[1] + 30.0f, g.cam.tgt[1]);
            }
            move_test_inject('w', 0);
            break;
        }
        case 250:
            move_test_inject('s', 1);          /* stick DOWN */
            break;
        case 400:
            if (g.aim_pitch != AIM_PITCH_MAX_R1) {
                fail++;
                printf("aim test: CHECK FAILED — stick DOWN did not "
                       "clamp at the up limit (pitch %.3f)\n",
                       g.aim_pitch);
            }
            move_test_inject('s', 0);
            break;
        case 410:
            yaw0 = g.yaw;
            move_test_inject('a', 1);          /* stick LEFT */
            break;
        case 415:
            if (g.yaw != yaw0 || !(g.aim_yawb > 0.5f)) {
                fail++;
                printf("aim test: CHECK FAILED — pose-pan phase "
                       "(yaw %.4f vs %.4f, blend %.3f)\n", g.yaw,
                       yaw0, g.aim_yawb);
            }
            break;
        case 460: {
            int ok = g.aim_yawb == 1.0f && g.yaw != yaw0;
            if (!ok) {
                fail++;
                printf("aim test: CHECK FAILED — body-turn overflow "
                       "(blend %.3f, yaw %.4f vs %.4f)\n", g.aim_yawb,
                       g.yaw, yaw0);
            }
            printf("aim test: inverted-Y pitch, clamps, full-down "
                   "camera, pose-pan-then-body-turn — %s\n",
                   fail == 0 ? "PASS" : "FAIL");
            fflush(stdout);
            em_frame_request_quit();
            break;
        }
    }
}

/* EM_MELEE_TEST=1 — deterministic knife self-test (the s36 melee
 * decode, em_weapon.h "KNIFE / MELEE"; restaged for the s68
 * creature-identity correction: crate bursts hatch BUGS, which ARE
 * melee victims; the worm whiff witness rides a direct spawn now).
 * Scene init spawns TWO DISGUISED CRATES (see the spawn block): A in
 * the knife reach (12), B 25 u away across the room; crates burst on
 * DAMAGE ONLY (the engine has no proximity trigger). A damage kill
 * still BROADCASTS the group alarm (the decoded list-wide, no-radius
 * walk), but the broadcast is INERT for placed crates — the engine
 * wakes a recipient only when its +0x52 (on-surface) flag is set, and
 * that is 0 on every placed crate (live-read s76), so crate B does NOT
 * wake (user-reported 2026-06-12: breaking one crate must not move the
 * other; the J1 "match" was wrong). Crate B therefore stays an inert
 * IDLE crate for the whole run — which also keeps the frontal cone
 * clear for the whiff legs. Adaptive phase machine:
 *
 *   phase 0  frame 8: assert both crates IDLE in reach, then tap L
 *            (CIRCLE) — the LIGHT combo (engine mode 0x21). Impact =
 *            swing tick 26 (len 50 - gate 24); the acquire resolves
 *            the nearest crate (A, slot 0).
 *   phase 1  crate A dies: assert melee hit count 1, ZERO rifle shots
 *            (the kill is the +0x36 mailbox write), TWO BUGS hatched
 *            at the crate ring (the s68 burst contract), and crate B
 *            STAYS IDLE — the broadcast does NOT wake it (s76).
 *   phase 2  recover anim 0x10F commits (a landed hit SKIPS the combo).
 *            Then the s76 LATCH witness: the A-bugs WALK IN and LATCH
 *            onto the player (you do NOT stab a clinging bug — the new
 *            bug combat is the shake-off). Wait for em_enemy_latched_
 *            count() >= 1.
 *   phase 3  SHAKE-OFF: the player AUTO-shakes the latched bug off (clip
 *            0x36 must play) which DETACHES it (latched -> 0); then clear
 *            both A-bugs (mailbox) so the frontal cone is empty for the
 *            whiff legs.
 *   phase 5  the WORM WHIFF witness (J2 s66, kept): spawn ONE worm
 *            9 u dead ahead (em_enemy_add), heavy stab THROUGH it —
 *            melee hits STAY 1 (func_00183AC0 rejects model 0xD), the
 *            worm untouched (ATTACK, vestigial HP 10).
 *   phase 6  the worm clears itself: its OWN lunge resolve bursts it
 *            on the player (health reported, not asserted) — wait
 *            out the flinch.
 *   phase 7  WHIFF COMBO (frontal cone empty: A-bugs cleared, worm gone,
 *            crate B inert behind): tap L, re-tap L during each swing —
 *            assert the committed clip chains 0x10B -> 0x10C -> 0x10D
 *            (the buffered +0x2E chain), NO recover anim on a whiff
 *            (clip-end exit), the final counts (5 swings, 1 hit — the
 *            bugs latch + are shaken off, not stabbed), and that crate B
 *            is still the lone IDLE survivor.
 *
 * PASS = all checks green; any phase timing out fails the run. */
static void melee_test_script(void)
{
    static int saw_recov_anim, chain12, chain23, whiff_recov;
    /* (mash-struggle witness uses live state, no clip flag) */
    static int mt_cleared;          /* frame the A-bugs were cleared   */
    static int mt_j3;               /* heavy 3 (worm whiff) tap frame   */
    static int mt_worm = -1;        /* the worm-whiff witness slot     */
    static int dbg = -1;
    static unsigned prev_anim;
    int n = g.frame_no;
    unsigned anim = em_game_anim_active();

    if (dbg < 0) dbg = getenv("EM_MELEE_DEBUG") != NULL;
    if (dbg)
        printf("mt f%d ph%d melee(st%d cb%d hv%d sw%d hit%d) anim 0x%X "
               "alive %d st0 %d st1 %d d0 %.1f d1 %.1f\n", n, g.mt_phase,
               em_weapon_melee_state(), em_weapon_melee_combo(),
               em_weapon_melee_heavy(), em_weapon_melee_swings(),
               em_weapon_melee_hits(), anim, em_enemy_alive(),
               em_enemy_state(0), em_enemy_state(1), et_dist(),
               et_dist_i(1));
    /* transition trackers (sampled every frame) */
    if (anim == 0x10F && g.mt_phase <= 2)
        saw_recov_anim = 1;
    if (g.mt_phase == 7) {
        if (prev_anim == 0x10B && anim == 0x10C) chain12 = 1;
        if (prev_anim == 0x10C && anim == 0x10D) chain23 = 1;
        if (anim == 0x10F) whiff_recov = 1;
    }
    prev_anim = anim;

    if (n >= 1500) {
        g.mt_fail++;
        printf("melee test: CHECK FAILED — timed out in phase %d\n",
               g.mt_phase);
        goto finish;
    }

    switch (g.mt_phase) {
        case 0:
            if (n == 8) {
                /* both crates IDLE (A in reach, B across the room) —
                 * IDLE is engine-true at ANY range (no proximity
                 * trigger exists) */
                if (!(em_enemy_alive() == 2 &&
                      em_enemy_state(0) == EM_ENEMY_IDLE &&
                      em_enemy_state(1) == EM_ENEMY_IDLE &&
                      et_dist() < 12.0f)) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crates not idle "
                           "(A in reach) (alive %d states %d/%d dist "
                           "%.1f/%.1f)\n", em_enemy_alive(),
                           em_enemy_state(0), em_enemy_state(1),
                           et_dist(), et_dist_i(1));
                }
                move_test_inject('l', 1);       /* CIRCLE — light combo */
            } else if (n == 10) {
                move_test_inject('l', 0);
                g.mt_phase = 1;
                g.mt_mark  = n;
            }
            break;
        case 1:
            if (em_enemy_state(0) == EM_ENEMY_FREE) {
                /* slot 0 freed = the burst frame; the bug pair
                 * hatches the same tick (s68) */
                if (!(em_weapon_melee_hits() == 1 &&
                      em_weapon_shots() == 0)) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crate kill not "
                           "the knife mailbox (melee hits %d, shots "
                           "%d)\n", em_weapon_melee_hits(),
                           em_weapon_shots());
                }
                /* the A-bugs (slots 2/3) hatch at crate A's ring */
                float cp[3] = { 0, 0, 0 }, bp[3] = { 0, 0, 0 };
                em_enemy_pos(0, cp);
                int hatch_ok = em_enemy_alive() == 3 &&
                               em_enemy_kind(2) == EM_ENEMY_KIND_BUG &&
                               em_enemy_kind(3) == EM_ENEMY_KIND_BUG;
                for (int k = 2; k <= 3 && hatch_ok; k++) {
                    em_enemy_pos(k, bp);
                    hatch_ok = fabsf(bp[0] - cp[0]) +
                               fabsf(bp[2] - cp[2]) < 2.5f;
                }
                if (!hatch_ok) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — bug pair not at "
                           "the burst crate (alive %d kinds %d/%d)\n",
                           em_enemy_alive(), em_enemy_kind(2),
                           em_enemy_kind(3));
                }
                /* s76: the GROUP-ALARM BROADCAST is engine-true but
                 * INERT for placed crates — the engine reaches its wake
                 * write only for a recipient with +0x52 (on-surface)
                 * set, and that is 0 on every placed crate, so killing
                 * crate A does NOT wake crate B (user-reported; the J1
                 * "match" was wrong). Assert crate B STAYS IDLE. */
                if (em_enemy_state(1) != EM_ENEMY_IDLE) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crate B should "
                           "stay IDLE (the broadcast must not wake it) "
                           "(state %d)\n", em_enemy_state(1));
                }
                g.mt_phase = 2;
                g.mt_mark  = n;
            }
            break;
        case 2:
            /* s76 LATCH + SHAKE-OFF witness (the new bug combat — you do
             * NOT stab a clinging bug, you shake it off): the A-bugs walk
             * in from the 11-u ring and LATCH onto the player. The light-
             * combo recover (0x10F) is tracked globally; wait for a latch
             * (the player then auto-shakes — player_shake_tick). */
            if (!saw_recov_anim && n > g.mt_mark + 90) {
                g.mt_fail++;
                printf("melee test: CHECK FAILED — recover anim 0x10F "
                       "never committed after the confirmed hit\n");
                saw_recov_anim = -1;            /* report once */
            }
            if (em_enemy_latched_count() >= 1) {
                g.mt_phase = 3;                 /* a bug latched */
                g.mt_mark  = n;
            } else if (n > g.mt_mark + 600) {
                g.mt_fail++;
                printf("melee test: CHECK FAILED — no bug latched onto "
                       "the player within budget\n");
                g.mt_phase = 3;
                g.mt_mark  = n;
            }
            break;
        case 3:
            /* s76 LIVE: the bugs LATCH and the player MASHES CROSS to
             * shake them off — which KILLS them (em_enemy_shake_off ->
             * EM_ENEMY_DEATH), not detach. Mash CROSS (alternate down/up =
             * press edges, ~1 per 2 frames) until the struggle wins; then
             * witness both A-bugs DIED. */
            move_test_inject('k', (n & 1) == 0);     /* CROSS press edges */
            if (!mt_cleared) {
                if (em_enemy_latched_count() == 0 &&
                    !player_damage_locked()) {        /* struggle won */
                    int dead = (em_enemy_state(2) == EM_ENEMY_FREE ||
                                em_enemy_state(2) == EM_ENEMY_DEATH) +
                               (em_enemy_state(3) == EM_ENEMY_FREE ||
                                em_enemy_state(3) == EM_ENEMY_DEATH);
                    if (dead < 2) {
                        g.mt_fail++;
                        printf("melee test: CHECK FAILED — the shake-off "
                               "must KILL the clinging bugs (states %d/%d)\n",
                               em_enemy_state(2), em_enemy_state(3));
                    }
                    move_test_inject('k', 0);
                    mt_cleared = n > 0 ? n : 1;
                } else if (n > g.mt_mark + 600) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — CROSS-mash did not "
                           "shake the bugs off (latched %d)\n",
                           em_enemy_latched_count());
                    move_test_inject('k', 0);
                    for (int k = 2; k <= 3; k++)
                        if (em_enemy_state(k) != EM_ENEMY_FREE)
                            em_enemy_damage(k, 15);
                    mt_cleared = n;
                }
            } else if (n > mt_cleared + 12 && !player_damage_locked()) {
                int live_a = (em_enemy_state(2) != EM_ENEMY_FREE) +
                             (em_enemy_state(3) != EM_ENEMY_FREE);
                if (live_a) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — A-bugs not gone "
                           "(%d live)\n", live_a);
                }
                g.mt_phase = 5;
                g.mt_mark  = n;
            }
            break;
        case 5:
            /* the WORM WHIFF witness (J2 s66, kept through the s68
             * restage): a directly-spawned worm 9 u dead ahead;
             * heavy 3 must pass straight through it (func_00183AC0
             * rejects model 0xD — worms are NOT melee victims). */
            if (mt_worm < 0) {
                float fx = sinf(g.yaw), fz = cosf(g.yaw);
                float wp[3] = { g.pos[0] + fx * 9.0f, g.pos[1],
                                g.pos[2] + fz * 9.0f };
                mt_worm = em_enemy_add(em_frame_gfx(), wp,
                                       g.yaw + EM_PI);
                if (mt_worm < 0) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — worm witness "
                           "spawn failed\n");
                    goto finish;
                }
                g.mt_mark = n;
            } else if (!mt_j3) {
                if (n < g.mt_mark + 5) break;   /* let its INIT run */
                if (em_weapon_is_melee() || player_damage_locked())
                    break;
                move_test_inject('j', 1);       /* SQUARE — heavy 3 */
                mt_j3 = n;
            } else {
                if (n == mt_j3 + 2) move_test_inject('j', 0);
                if (n > mt_j3 + 4 && !em_weapon_is_melee()) {
                    if (!(em_weapon_melee_hits() == 1 &&
                          em_enemy_state(mt_worm) == EM_ENEMY_ATTACK &&
                          em_enemy_hp(mt_worm) == 10)) {
                        g.mt_fail++;
                        printf("melee test: CHECK FAILED — heavy 3 "
                               "must WHIFF through the worm (hits %d, "
                               "worm state %d hp %d)\n",
                               em_weapon_melee_hits(),
                               em_enemy_state(mt_worm),
                               em_enemy_hp(mt_worm));
                    }
                    g.mt_phase = 6;
                    g.mt_mark  = n;
                }
            }
            break;
        case 6:
            /* the worm clears itself: its OWN lunge resolve bursts it
             * on the player (latch 15 — health reported, not
             * asserted); wait out the flinch before the whiff combo. */
            if (em_enemy_state(mt_worm) == EM_ENEMY_FREE &&
                !em_weapon_is_melee() && !player_damage_locked()) {
                g.mt_phase = 7;
                g.mt_mark  = n;
            }
            break;
        case 7:
            /* whiff combo: tap L now and during each swing (the chain
             * windows are forgiving — any in-swing press buffers). */
            if (n == g.mt_mark + 2 || n == g.mt_mark + 14 ||
                n == g.mt_mark + 44)
                move_test_inject('l', 1);
            else if (n == g.mt_mark + 4 || n == g.mt_mark + 16 ||
                     n == g.mt_mark + 46)
                move_test_inject('l', 0);
            else if (n > g.mt_mark + 50 && !em_weapon_is_melee()) {
                if (!(chain12 && chain23)) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — whiff combo did "
                           "not chain (0x10B->0x10C %d, 0x10C->0x10D "
                           "%d)\n", chain12, chain23);
                }
                if (whiff_recov) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — recover anim "
                           "played on a whiff (clip-end exit "
                           "expected)\n");
                }
                if (em_weapon_melee_swings() != 5 ||
                    em_weapon_melee_hits() != 1) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — counts (swings "
                           "%d expected 5: light + the worm whiff + 3 "
                           "whiffs; hits %d expected 1 — the bugs LATCH "
                           "and are shaken off, not stabbed; the worm is "
                           "not a melee victim)\n",
                           em_weapon_melee_swings(),
                           em_weapon_melee_hits());
                }
                /* s76: crate B never woke (the broadcast is inert — see
                 * phase 1), so it is STILL an IDLE crate and hatched no
                 * bugs; the only survivor is crate B itself, and the
                 * frontal cone is empty (which is why the whiffs
                 * whiffed). */
                if (em_enemy_state(1) != EM_ENEMY_IDLE) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — crate B should "
                           "still be IDLE (it never woke) (state %d)\n",
                           em_enemy_state(1));
                }
                int live_bugs = 0;
                for (int i = 0; i < 16; i++)
                    if (em_enemy_kind(i) == EM_ENEMY_KIND_BUG &&
                        em_enemy_state(i) == EM_ENEMY_ATTACK)
                        live_bugs++;
                if (em_enemy_alive() != 1 || live_bugs != 0) {
                    g.mt_fail++;
                    printf("melee test: CHECK FAILED — only the IDLE "
                           "crate B should survive (alive %d, live bugs "
                           "%d)\n", em_enemy_alive(), live_bugs);
                }
                goto finish;
            }
            break;
    }
    return;
finish:
    printf("melee test: %d swing(s), %d hit(s), %d shot(s), enemies "
           "alive %d, health %.0f — %s\n", em_weapon_melee_swings(),
           em_weapon_melee_hits(), em_weapon_shots(), em_enemy_alive(),
           g.status.health, g.mt_fail == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    em_frame_request_quit();
}

/* EM_DEATH_TEST=1 — player flinch/death/game-over/continue self-test
 * (the PLAYER DAMAGE & DEATH machine + the s66/s70 GO machine above).
 * Scene init spawns ONE
 * worm 30 u ahead (the contact-run placement); frame 0 sets health
 * to 20 (instrumentation — two lunges at the DECODED latch 15 =
 * flinch then death; a worm suicide-bursts on its lunge connect, so
 * phase 2 spawns the second killer). Adaptive phases:
 *
 *   0  first lunge lands (health 20 -> 5): assert the FLINCH — state
 *      2 sub 0, an unarmed flinch clip (0x1E..0x21) committed, the
 *      worm burst.
 *   1  flinch plays out: assert the exit armed the i-frames and
 *      control returned (pd_state 0).
 *   2  i-frames expire: spawn worm B 30 u ahead.
 *   3  second lunge (health 5 -> 0): assert the DEATH — state 2 sub
 *      1, the death clip 0x2A (unarmed) committed, movement locked.
 *   4  death sequence: clip end -> corpse hold -> fade-out; assert
 *      full black, the machine parked (pd_phase 4) and the GAME-OVER
 *      screen armed (GO_SCREEN, hold = 240, fade-in started).
 *   5  GAME OVER shown (fade settled): assert the hold survived the
 *      fade-in (engine: the 240 counts THROUGH it), then SKIP with
 *      CROSS (the decoded D_00810E74 & 0x40) well before expiry.
 *   6  assert the skip was honored (GO_SCREEN_OUT with hold frames
 *      remaining — skippable witnessed, not a timeout).
 *   7  CONTINUE prompt up (fade settled): assert the from-death
 *      cursor INIT = 1 (func_001AC480 sub 0 on D_00275BDC), then
 *      press d-pad UP (engine 0x1000).
 *   8  assert the cursor walked to 0 (option move + sound), then
 *      CONFIRM with START (engine mask 0x840 = START|CROSS).
 *   9  dispatch at hold-black -> restart: scene reloaded, boot status
 *      restored (health 75), damage machine cleared, death pose
 *      released.
 *  10  fade-in running after the restart.
 *
 * PASS = all checks green; any phase timing out fails the run. */
static void gt_check(int cond, const char *what)
{
    if (cond) return;
    g.gt_fail++;
    printf("death test: CHECK FAILED — %s (pd %d/%d/%d hp %.0f clip "
           "0x%X go %d)\n", what, g.pd_state, g.pd_sub, g.pd_phase,
           g.status.health, g.pd_clip, g.go_state);
}

static void death_test_script(void)
{
    int n = g.frame_no;
    /* the restart re-arms frame 0 — only the FIRST frame 0 seeds */
    if (n == 0 && g.gt_phase == 0 && !g.gt_mark) {
        g.gt_mark = 1;
        g.status.health = 20.0f;    /* instrumentation: 2 lunges (15
                                     * each, the decoded latch) kill  */
        g.gt_health0 = g.status.health;
        printf("death test: health set to %.0f, worm 30 u ahead\n",
               g.status.health);
        return;
    }
    if (g.gt_phase < 5 && n >= 3000 && !g.go_restart) {
        g.gt_fail++;
        printf("death test: CHECK FAILED — timed out in phase %d "
               "(frame %d)\n", g.gt_phase, n);
        goto finish;
    }
    switch (g.gt_phase) {
        case 0:
            if (g.status.health < g.gt_health0) {
                /* lunge landed THIS frame; the flinch entered the same
                 * frame (processor), the clip commits next actor tick
                 * — sample at +3 */
                g.gt_phase = 1;
                g.gt_mark  = n + 3;
                gt_check(g.status.health == g.gt_health0 - 15.0f,
                         "first lunge dealt the decoded latch (15)");
            }
            break;
        case 1:
            if (n == g.gt_mark) {
                unsigned c = em_game_anim_active();
                g.gt_flinch = c;
                gt_check(g.pd_state == 2 && g.pd_sub == 0,
                         "flinch state entered (state 2 sub 0)");
                gt_check(c >= PD_CLIP_FLINCH_A0 && c <= PD_CLIP_FLINCH_B1,
                         "an unarmed flinch clip (0x1E..0x21) committed");
                gt_check(em_enemy_alive() == 0,
                         "worm burst on its lunge connect");
                g.gt_phase = 2;
            }
            break;
        case 2:
            if (g.pd_state == 0) {
                gt_check(g.pd_iframes > 0,
                         "flinch exit armed the i-frames (+0x20E)");
                g.gt_phase = 3;
            }
            break;
        case 3:
            if (g.pd_iframes == 0) {
                float ep[3] = { g.pos[0] + sinf(g.yaw) * 30.0f, g.pos[1],
                                g.pos[2] + cosf(g.yaw) * 30.0f };
                if (em_enemy_add_kind(em_frame_gfx(),
                                      EM_ENEMY_KIND_CRAWLER, ep,
                                      g.yaw + EM_PI) < 0) {
                    g.gt_fail++;
                    printf("death test: worm B spawn failed\n");
                    goto finish;
                }
                g.gt_health0 = g.status.health;
                g.gt_phase   = 4;
            }
            break;
        case 4:
            if (g.status.health <= 0.0f) {
                g.gt_phase = 5;
                g.gt_mark  = n + 3;
            }
            break;
        case 5:
            if (n == g.gt_mark) {
                gt_check(g.pd_state == 2 && g.pd_sub == 1,
                         "death state entered (state 2 sub 1)");
                gt_check(em_game_anim_active() == PD_CLIP_DEATH,
                         "the death clip 0x2A committed");
                g.gt_phase = 6;
                g.gt_mark  = n;
            }
            break;
        case 6:
            /* ride the sequence out: clip (130) + hold (120) + fade
             * (64) — the GAME-OVER screen must arm */
            if (g.go_state == GO_SCREEN) {
                gt_check(g.pd_phase == 4, "death machine parked");
                gt_check(g.go_hold > GO_HOLD_FRAMES - 4 &&
                         g.go_hold <= GO_HOLD_FRAMES,
                         "GAME OVER hold armed at 240 (task+0x18)");
                g.gt_phase = 7;
                g.gt_mark  = n;
            } else if (n > g.gt_mark + 500) {
                g.gt_fail++;
                printf("death test: CHECK FAILED — game over never "
                       "armed (pd %d/%d/%d fade %.2f)\n", g.pd_state,
                       g.pd_sub, g.pd_phase, em_frame_fade_level());
                goto finish;
            }
            break;
        case 7:
            /* the screen fades IN (engine wait sub 2/3); once the
             * fade settles, SKIP with CROSS well before the 240
             * expires (the hold keeps counting through the fade) */
            if (g.go_state == GO_SCREEN &&
                em_frame_fade_level() <= 0.0f) {
                gt_check(g.go_hold > 0 && g.go_hold < GO_HOLD_FRAMES,
                         "hold counting through the fade-in, screen "
                         "shown before expiry");
                move_test_inject('k', 1);            /* CROSS (0x40) */
                g.gt_phase = 8;
                g.gt_mark  = n + 2;
            } else if (n > g.gt_mark + 400) {
                gt_check(0, "GAME OVER screen faded in");
                goto finish;
            }
            break;
        case 8:
            if (n == g.gt_mark)
                move_test_inject('k', 0);
            if (g.go_state == GO_SCREEN_OUT) {
                gt_check(g.go_hold > 0,
                         "CROSS skip honored with hold frames left "
                         "(skippable, not a timeout)");
                g.gt_phase = 9;
                g.gt_mark  = n;
            } else if (n > g.gt_mark + 100) {
                gt_check(0, "CROSS skipped the GAME OVER hold");
                goto finish;
            }
            break;
        case 9:
            /* the CONTINUE prompt fades in (the task-replacement
             * point); assert the decoded from-death cursor init then
             * walk it UP to option 0 */
            if (g.go_state == GO_PROMPT &&
                em_frame_fade_level() <= 0.0f) {
                gt_check(g.go_cursor == GO_CURSOR_DEATH,
                         "from-death cursor init = 1 (func_001AC480 "
                         "sub 0)");
                move_test_inject(EM_KEY_UP, 1);      /* d-pad 0x1000 */
                g.gt_phase = 10;
                g.gt_mark  = n + 2;
            } else if (n > g.gt_mark + 400) {
                gt_check(0, "CONTINUE prompt faded in");
                goto finish;
            }
            break;
        case 10:
            if (n == g.gt_mark)
                move_test_inject(EM_KEY_UP, 0);
            if (n == g.gt_mark + 4) {
                gt_check(g.go_cursor == 0,
                         "d-pad UP walked the cursor to CONTINUE (0)");
                move_test_inject(EM_KEY_RETURN, 1);  /* START (0x800) */
                g.gt_mark = n + 2;
                g.gt_phase = 11;
            }
            break;
        case 11:
            if (n == g.gt_mark)
                move_test_inject(EM_KEY_RETURN, 0);
            if (g.go_state == GO_PROMPT_CONFIRM && g.gt_mark > 0) {
                g.gt_mark = 0;       /* confirmed; wait for the reload */
            } else if (g.frame_no <= 2 && g.go_state == GO_OFF) {
                /* the restart re-armed the frame machine: frame_no
                 * restarted at 0 (scene-init) — sample the fresh
                 * state on its first frames */
                gt_check(g.status.health == 75.0f &&
                         g.status.health_max == 100.0f,
                         "boot status restored on restart");
                gt_check(g.pd_state == 0 && g.pd_iframes == 0,
                         "damage machine cleared on restart");
                gt_check(em_game_anim_active() == 0,
                         "death pose released on restart");
                g.gt_phase = 12;
                g.gt_mark  = 0;
            }
            break;
        case 12:
            /* one more frame so the fade-in is observable */
            gt_check(em_frame_fade_level() < 1.0f,
                     "fade-in running after restart");
            goto finish;
    }
    return;
finish:
    printf("death test: flinch clip 0x%X, death clip 0x%X, health "
           "%.0f, game-over screen %s, cross-skip %s, prompt cursor "
           "%s, continue %s — %s\n", g.gt_flinch,
           g.pd_clip ? g.pd_clip : PD_CLIP_DEATH, g.status.health,
           g.gt_phase >= 8 ? "shown" : "NOT shown",
           g.gt_phase >= 9 ? "ok" : "NOT taken",
           g.gt_phase >= 10 ? "ok" : "NOT seen",
           g.gt_phase >= 12 ? "ok" : "NOT reached",
           g.gt_fail == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    em_frame_request_quit();
}

/* func_001AE5E0 — THE GAMEPLAY FRAME (stage order is the engine's). */
static void gameplay_frame(void)
{
    if (g.move_test) move_test_script();    /* debug instrumentation only */
    if (g.door_test) door_test_script();    /* debug instrumentation only */
    if (g.transit_test) transit_test_script(); /* debug instrumentation  */
    if (g.slider_test) slider_test_script();   /* debug instrumentation  */
    if (g.locked_test) locked_test_script();   /* debug instrumentation  */
    if (g.weapon_test) weapon_test_script();/* debug instrumentation only */
    /* EM_CAPTURE_AIM=1: hold R1 (key E) from frame 0 — by the default
     * capture frame (60) the draw has finished and the capture shows the
     * held aim pose, the laser pass and the lowered camera target. A
     * short turn-in-place (frames 28..44, ~27 deg) angles the laser off
     * the camera axis so the beam and hit dot are not occluded by the
     * player's own torso in the capture.
     * EM_CAPTURE_AIM=2: same, plus ONE semi shot (L = CIRCLE) at frame
     * 57 — it lands at 58, so the default capture frame 60 samples the
     * 0x112 recoil replay ~2 frames in (the clip's max-delta snap zone,
     * 4 deg/frame) with the muzzle flash still up: the aim+fire
     * reference against the =1 static hold. */
    /* EM_CAPTURE_AIM=3: aim + hold stick DOWN ('s') from frame 20 —
     * the inverted-Y steer pitches the aim UP: the capture samples the
     * up-ladder pose, the raised laser and the counter-lowered mode-1
     * eye. =4: hold stick UP ('w') — aim DOWN ("W = down").
     * EM_CAPTURE_AIM=5: aim NEAR A WALL — run at the camera (the RISE
     * approach) until the player reaches the office south wall, turn
     * to face back INTO the room, then hold R1: the desired aim eye
     * (30 u behind the facing) lands inside the wall and the DECODED
     * func_0018F870 PULLS IT IN at constant height (hit + 0.5 along
     * the player->eye ray) — the over-shoulder view framed from the
     * wall plane, no rise, no slide-away ("R1 keeps the pull-in").
     * RE-VERIFIED in src/func_0018F870.c (NEARMISS, logic
     * authoritative): the step-1 probe runs focus -> normalize(eye -
     * focus) * 1.5 + eye, and on a hit the scratch vector is re-derived
     * as the UNIT focus->eye direction and the eye is written
     * `self+0x10 += 0.5f * dir.x` / `self+0x18 += 0.5f * dir.z` off the
     * hit point — X and Z only, self+0x14 (Y) untouched. So "0.5 units
     * off the surface, height held" is literal, not an approximation.
     * SCOPED (audit 2026-07-31): that is the WALL arm, which is the one
     * this capture exercises. The sibling floor/ceiling arm (surface
     * flags +0x1A & 0xD800) copies the whole hit quad into self+0x10
     * with func_00102948 first, so it DOES move the eye Y before
     * applying the same 0.5 X/Z push — "height held" is the wall case
     * only, not the function as a whole.
     * Default capture frame 420; EM_CAMERA_TRACE=1 prints the solve. */
    if (g.capture_aim == 5) {
        if      (g.frame_no == 0)   move_test_inject('s', 1);
        else if (g.frame_no == 300) move_test_inject('s', 0);
        else if (g.frame_no == 310) move_test_inject('w', 1);
        else if (g.frame_no == 322) move_test_inject('w', 0);
        else if (g.frame_no == 330) move_test_inject('e', 1);
    } else if (g.capture_aim) {
        if      (g.frame_no == 0)  move_test_inject('e', 1);
        if (g.capture_aim < 3) {
            if      (g.frame_no == 28) move_test_inject('a', 1);
            else if (g.frame_no == 44) move_test_inject('a', 0);
        } else if (g.frame_no == 20) {
            move_test_inject(g.capture_aim == 3 ? 's' : 'w', 1);
        }
        if (g.capture_aim == 2) {
            if      (g.frame_no == 57) move_test_inject('l', 1);
            else if (g.frame_no == 59) move_test_inject('l', 0);
        }
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_DOOR=1: the door-test approach + CROSS with NO asserts
     * — by the default door capture frame (110) the transit walk has
     * arrived and the op 0x0D sub 5 CINEMATIC CUT holds: 20 u behind
     * the staging point along the through-door axis at +19, looking at
     * the player in the doorway (live-verified geometry; with
     * EM_DOORCAM_LOCKED=1: the locked-look handle placement). */
    if (g.capture_door) {
        if      (g.frame_no == 0)  move_test_inject('w', 1);
        else if (g.frame_no == 24) move_test_inject('w', 0);
        else if (g.frame_no == 28) move_test_inject('w', 1);
        else if (g.frame_no == 58) move_test_inject('w', 0);
        else if (g.frame_no == 60) move_test_inject('k', 1);
        else if (g.frame_no == 61) move_test_inject('k', 0);
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_SUPPLY=1: approach the office DOUBLE DOORS + CROSS —
     * the same-scene transit walks the player into the SUPPLY ROOM and
     * its spawn-record FIXED camera (camregion line, decoded eye
     * (116, 33, -300)) pins the view from the room corner by the
     * default capture frame (360). */
    if (g.capture_supply) {
        if      (g.frame_no == 0)  move_test_inject('w', 1);
        else if (g.frame_no == 40) move_test_inject('w', 0);
        else if (g.frame_no == 45) move_test_inject('k', 1);
        else if (g.frame_no == 46) move_test_inject('k', 0);
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_EXAMINE=1: place the player 6 u south of the scene's
     * FIRST examine object (inside the facing auto-pass ring; the snow
     * scene's = the AREA11 switch — the spawn-side approach corridor
     * is wall-blocked in the exported collision, so the capture
     * teleports to the ring instead of walking) and CROSS at frame 20
     * — the capture (default frame 60) samples the input-paused
     * refusal line "Switch / No power..." (GLOBAL line 0x1A)
     * presenting over the scene. */
    if (g.capture_examine) {
        float xp[3];
        if (g.frame_no == 1 &&
            em_examine_pos(g.capture_examine - 1, xp)) {
            g.pos[0] = xp[0] - 2.0f;   /* a step aside so a fixed
                                        * camera cue is not filled by
                                        * the player model; dist 4.0
                                        * fits the tightest (5-u
                                        * archetype-1) ring */
            g.pos[1] = xp[1];
            g.pos[2] = xp[2] - 3.5f;
            g.yaw    = 0.0f;               /* facing +Z = the object */
            g.cam.state = 0;               /* re-seat the chase camera */
            printf("capture-examine: placed at (%.1f, %.1f, %.1f)\n",
                   g.pos[0], g.pos[1], g.pos[2]);
        }
        else if (g.frame_no == 20) move_test_inject('k', 1);
        else if (g.frame_no == 22) move_test_inject('k', 0);
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_RISE=1: hold 's' (stick down) from frame 0 — the
     * player about-faces and runs TOWARD the camera; the chase camera
     * backs away until the room's far wall blocks its desired eye and
     * the decoded func_0018DD20 solve PULLS IT IN at constant height
     * (cam_solver_0018DD20, em_camera.c; CONFIRMED against
     * src/func_0018DD20.c [NEARMISS] — the wall arm adds 0.5f * push
     * to pos[0]/pos[2] only, never pos[1]): the eye parks 0.5 u off
     * the wall while
     * the player keeps closing, so the view tilts down over the
     * player's head — the user-observed "rise to show the player's
     * top", emergent. Use with EM_CAPTURE_FRAME around 280+ (office
     * scene: the eye meets the south wall after ~9 s of running).
     * EM_CAMERA_TRACE=1 prints the per-frame solve for verification. */
    if (g.capture_rise) {                   /* debug instrumentation only */
        if (g.frame_no == 0)
            move_test_inject('s', 1);
        /* release phase (after the default capture window): walk back
         * AWAY from the wall — once the desired eye has room again the
         * solver stops responding and the chase returns the camera to
         * its full distance/height ("returns when clear", emergent). */
        else if (g.frame_no == 360) move_test_inject('s', 0);
        else if (g.frame_no == 370) move_test_inject('w', 1);
        else if (g.frame_no == 600) move_test_inject('w', 0);
    }
    /* EM_CAPTURE_ORIENT=1: Cmd (WALK-band hold) + 'a' for 30 frames —
     * the facing swings ~90 deg (the same facing-seek runs for every
     * moving gait) while gait-2 WALK drifts ~3 u left — then idle.
     * (2026-06-11 GAIT HOLD TIERS, em_input.h: the WALK hold moved from
     * Option to COMMAND; Option is now the gait-1 TURN/creep hold —
     * deliberately reachable again from the keyboard.) After
     * CAM_AUTO_DELAY the camera slowly auto-orients behind the new
     * facing (EM_CAMERA_TRACE prints the seek); capture late (~frame
     * 450) to see it settled. */
    if (g.capture_orient) {
        if (g.frame_no == 0) {
            move_test_inject(EM_KEY_CMD, 1);
            move_test_inject('a', 1);
        } else if (g.frame_no == 30) {
            move_test_inject('a', 0);
            move_test_inject(EM_KEY_CMD, 0);
        }
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_WALK=1: hold 'w' (run AWAY from the camera) — by the
     * capture frame the chase rides the tow-rope at the MOVING-PLAYER
     * heights (eye +19 / target +17, the s71 idle-row correction): a
     * level over-the-shoulder framing, NOT the old dive toward the
     * feet. The print at the capture frame is the height witness. */
    if (g.capture_walk) {
        if (g.frame_no == 0)
            move_test_inject('w', 1);
        else if (g.frame_no == g.capture_frame)
            printf("capture-walk: frame %d player y %.2f cam eye y %.2f "
                   "tgt y %.2f (moving heights: eye +%.1f tgt +%.1f)\n",
                   g.frame_no, g.pos[1], g.cam.eye[1], g.cam.tgt[1],
                   g.cam.eye[1] - g.pos[1], g.cam.tgt[1] - g.pos[1]);
    }                                       /* debug instrumentation only */
    /* EM_CAPTURE_LOCKED=N (run with EM_SCENE=assets/scene_drawbridge):
     * the m15 LOCKED security door (placement (-20.5, 0, -192) yaw 0,
     * doorway center (-25.5, -192)) tried from two different APPROACH
     * ORIENTATIONS — variant 1 stands square south of the center
     * facing +Z, variant 2 stands oblique to the SW facing the center;
     * the re-seated chase camera therefore arrives at the CROSS with
     * two different live yaws. The locked-look cut (func_001BBBF0)
     * must park the SAME camera both runs (the engine's D_00810374 =
     * last-SCRIPTED yaw, ported as the kickoff snap yaw — never the
     * live chase heading): the print at the capture frame is the
     * determinism witness, the BMPs the visual one. */
    if (g.capture_locked) {
        if (g.frame_no == 1) {
            if (g.capture_locked == 1) {
                g.pos[0] = -25.5f; g.pos[2] = -201.0f;
                g.yaw     = 0.0f;           /* square on, facing +Z */
                g.cam.yaw = 0.0f;           /* prior camera: behind */
            } else {
                g.pos[0] = -21.0f; g.pos[2] = -200.0f;
                g.yaw     = -0.512f;        /* oblique SE, facing the
                                             * doorway center */
                g.cam.yaw = -2.2f;          /* prior camera: swung way
                                             * around — the live-yaw
                                             * input the OLD locked
                                             * look wrongly consumed */
            }
            g.pos[1] = 0.0f;
            g.cam.state = 0;                /* re-seat the chase camera
                                             * behind the variant yaw */
            printf("capture-locked: variant %d at (%.1f, %.1f, %.1f) "
                   "yaw %.3f cam yaw %.3f\n", g.capture_locked,
                   g.pos[0], g.pos[1], g.pos[2], g.yaw, g.cam.yaw);
        } else if (g.frame_no == 50) {
            move_test_inject('k', 1);       /* CROSS — the locked try */
        } else if (g.frame_no == 52) {
            move_test_inject('k', 0);
        } else if (g.frame_no == g.capture_frame) {
            printf("capture-locked: variant %d frame %d cam eye "
                   "(%.3f, %.3f, %.3f) tgt (%.3f, %.3f, %.3f)\n",
                   g.capture_locked, g.frame_no,
                   g.cam.eye[0], g.cam.eye[1], g.cam.eye[2],
                   g.cam.tgt[0], g.cam.tgt[1], g.cam.tgt[2]);
        }
    }                                       /* debug instrumentation only */
    if (g.enemy_test) enemy_test_script();  /* debug instrumentation only */
    if (g.melee_test) melee_test_script();  /* debug instrumentation only */
    if (g.death_test) death_test_script();  /* debug instrumentation only */
    if (g.sfx_test)  sfx_test_script();     /* debug instrumentation only */
    if (g.pause_test) pause_test_script();  /* debug instrumentation only */
    if (g.pickup_test) pickup_test_script();/* debug instrumentation only */
    if (g.examine_test) examine_test_script();          /* same */
    if (g.camregion_test) camregion_test_script(); /* debug instr. only  */
    if (g.cine_test) cine_test_script();    /* debug instrumentation only */
    if (g.aim_test)  aim_test_script();     /* debug instrumentation only */
    /* STATUS-SCREEN PAUSE GATE: while the status screen is OPEN (the
     * real Triangle/Start toggle — em_hud_is_open(); the EM_HUD_FORCE
     * capture hook deliberately does NOT pause, see em_hud.h) the
     * world simulation HALTS: no actor/door/enemy/weapon updates, no
     * camera dispatch — the player cannot move, exactly the original's
     * menu pause. The frame still renders: the chain re-records from
     * the modules' frozen palettes, the camera commits (window-resize
     * safe), and the close-out runs em_hud_update so the screen can be
     * closed (the toggle stays live) — idle/animation clocks freeze
     * because actor_update never runs. */
    if (em_hud_is_open()) {
        render_chain_build();    /* frozen poses, current scene */
        camera_update();         /* freeze path: commit only (above) */
        frame_close_out();       /* flush + hud toggle + fade + capture */
        return;
    }
    /* GAME-OVER / CONTINUE GATE: from GO_SCREEN on, the engine's
     * gameplay task is parked (wait state) and then REPLACED WHOLESALE
     * by the continue machine (s66/s70 — the PD block doc): the world
     * stops existing. The port models that the same way the menu
     * pause does — the world simulation halts, the frame still
     * renders (the screens' opaque base hides the dead scene), and
     * game_over_tick owns input/fades/dispatch. The restart latch is
     * serviced by the frame machine before this frame re-runs. */
    if (g.go_state >= GO_SCREEN) {
        game_over_tick();        /* the continue-machine slice */
        render_chain_build();    /* frozen world under the screens */
        camera_update();         /* commit only */
        frame_close_out();       /* overlays + fade + capture */
        return;
    }
    /* MOVING-SURFACE REGISTRY (the truck's walkable top — em_collision.h
     * §11.4): clear ONCE at the top of the sim (mirrors the PS2 per-frame
     * zone table being rebuilt), then let the moving actors register their
     * footprint+velocity below before the player ground-solve consumes the
     * carry. The truck is the sole AREA-11 registrant; this clear lives
     * here so a future second registrant only adds its register() call. */
    em_collision_moving_clear();
    /* STATIC BLOCKER REGISTRY (the AREA-11 gated GRATE's closed hull —
     * em_collision.h §blocker): cleared once at the top of the sim
     * alongside the moving-surface registry, then grate_update (below,
     * before actor_update) re-registers the closed hull while the area is
     * not powered, so player_move's ground-solve push-out
     * (em_collision_blocker_probe) sees this frame's blocker. */
    em_collision_blocker_clear();
    /* WEDGED TRUCK (AREA-11 record 16, ov 0x00823FF0 — the run-across set-
     * piece; INVESTIGATION_first_level_area11.md §11). Ticked BEFORE
     * actor_update so the truck has registered its footprint + this-frame
     * velocity by the time player_move's ground-solve calls
     * em_collision_moving_carry(g.pos): a player on the falling top is then
     * carried DOWN this frame (the fail), one who ran off is safe. Advances
     * the trigger->fall state machine and integrates the truck's own drop;
     * no-op when no `truck` line placed one. */
    em_truck_update(g.pos);
    /* AREA-11 OPENING DIRECTOR (the D_00810813 step machine — record 12,
     * ov 0x8253F0). Ticked BEFORE actor_update so a beat that arms this
     * frame has the player-lock (cine_active -> em_game_player_interact_
     * busy) raised before player_move reads it — the player freezes the
     * same frame the cinematic engages, no input/motion leak. Dormant
     * outside a beat (movement-v3 + the chase camera run exactly as
     * before until a trigger zone is entered). No-op once the director
     * reaches 0xFF. */
    director_tick();
    /* GATED GRATE (AREA-11 record 18, ov func_00159210 — the closed path-
     * blocker; INVESTIGATION_area11_grate.md). Ticked BEFORE actor_update
     * (and the player ground-solve) so its closed hull is registered this
     * frame before em_collision_blocker_probe consults the registry: while
     * the area is NOT powered the grate registers a solid blocker, once
     * powered it drops the blocker and runs the open slide. No-op when no
     * `grate` line placed one (registry stays empty -> movement unchanged). */
    grate_update();
    /* AREA-11 STEAM / FX EMITTER (record 7, ov 0x008235F0 — the level's one
     * dynamic light + a looping hiss + a puff; INVESTIGATION_first_level_
     * area11.md §3/§6). The glow is a placed lamp (no per-frame work); this
     * tick retriggers the looping hiss (0x413) and advances the puff phase.
     * It touches NOTHING in movement/collision/camera, so it cannot regress
     * the player solve. No-op when no `steam` line placed one. */
    steam_tick();
    actor_context_begin();   /* func_001CB590(0x008102B0, 0x320, ...) */
    actor_update();          /* func_0015BCF0 — player actor update   */
    actor_context_end();     /* func_001CB5A0                         */
    render_chain_build();    /* func_001D1C50 — render chain build    */
    render_env_init();       /* func_001C1D00(0x008101D0)             */
    /* func_001AFD70(0) — the actor-pool tick (world services). The
     * port's first pooled actors are the DOORS: per-frame behavior
     * (func_001BC350 state machine), the player use scan
     * (func_00184BA0) and articulation live in em_door_update.
     * func_0015C160 / func_001F0360 — still untranslated. */
    em_door_update(&g.coll, g.pos, g.yaw, em_frame_input());
    /* GOTO-DOOR SCENE SWITCH (one-shot, at fade-out completion — screen
     * fully black): the runtime area/sub-state load. Free + reload the
     * scene (em_game_scene_switch), place the player at the decoded
     * arrival spawn with the exit yaw, re-seat the camera, and RE-RECORD
     * the render chain — the chain built earlier this frame points into
     * the freed scene/door/enemy tables (the close-out flush must see
     * the new scene's draws). All invisible: the fade is at full black
     * and the fade-in was armed by the door before posting. */
    {
        char  gdir[64];
        float gp[3], gyaw;
        if (em_door_goto_pending(gdir, sizeof gdir, gp, &gyaw)) {
            if (em_game_scene_switch(gdir) == 0) {
                if (g.transit_test && !g.tt_switch_frame)
                    g.tt_switch_frame = g.frame_no;
                g.pos[0] = gp[0];
                g.pos[1] = gp[1];
                g.pos[2] = gp[2];
                g.yaw    = gyaw;
                g.doorcam      = 3;   /* cinematic over (op 0x18) */
                g.cam.tgt_soft = 0;
                g.cam.yaw = gyaw;
                g.cam.tgt_des[0] = g.pos[0];
                g.cam.tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
                g.cam.tgt_des[2] = g.pos[2];
                camera_desired_eye(&g.cam);
                memcpy(g.cam.eye, g.cam.eye_des, sizeof g.cam.eye);
                memcpy(g.cam.tgt, g.cam.tgt_des, sizeof g.cam.tgt);
            }
            render_chain_build();   /* drop the freed-scene draw records */
        }
    }
    /* DOOR-TRANSIT RE-PLACE (one-shot, at fade-out completion — screen
     * fully black): set the player at the spawn point behind the door
     * with the exit yaw (the engine's spawn-table placement,
     * func_001B07C0 path), and re-seat the chase camera behind the new
     * pose (the placement rec's camera-init fields) — an invisible cut,
     * exactly like the engine doing it under the fade. */
    {
        float wp[3], wyaw;
        if (em_door_warp_pending(wp, &wyaw)) {
            g.pos[0] = wp[0];
            g.pos[1] = wp[1];
            g.pos[2] = wp[2];
            g.yaw    = wyaw;
            g.doorcam      = 3;   /* cinematic over (op 0x18 restore) */
            g.cam.tgt_soft = 0;
            g.cam.yaw = wyaw;
            g.cam.tgt_des[0] = g.pos[0];
            g.cam.tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
            g.cam.tgt_des[2] = g.pos[2];
            camera_desired_eye(&g.cam);
            memcpy(g.cam.eye, g.cam.eye_des, sizeof g.cam.eye);
            memcpy(g.cam.tgt, g.cam.tgt_des, sizeof g.cam.tgt);
        }
    }
    /* PICKUPS (em_pickup.h — the pool's item actors plus the player
     * use scan's archetype-3 ITEM branch). The scan rides the same
     * CROSS press edge as the door scan above; the engine's single
     * nearest-wins walk over one interactive list is approximated
     * DOORS-FIRST: a press that armed a door has engaged the movement
     * lock by now, which suppresses the item scan (the engine's
     * scripted-frame spad-3B8D gate shape). Damage lock likewise. */
    em_pickup_update(g.pos, g.yaw, em_frame_input(),
                     !em_door_movement_locked() &&
                     !player_damage_locked());
    {
        /* Collection events (one-shot takes — em_pickup.h):
         *  - FOUND: the engine posts D_008106B0/B1 and auto-opens the
         *    status screen at the item's record; the port stand-in is
         *    the em_hud Found line (flagged there).
         *  - AMMO (func_001C40B0 case 0x10, +30 reserve per pack):
         *    applied through em_weapon_reset — the module's only ammo
         *    writer — so only while the machine is quiescent
         *    (HOLSTERED, no melee); otherwise the rounds stay pending
         *    inside em_pickup until the stance settles. */
        int found = em_pickup_found_take();
        if (found >= 0)
            em_hud_found_show(found);
        if (em_weapon_state() == EM_WPN_HOLSTERED &&
            !em_weapon_is_melee()) {
            int rounds = em_pickup_ammo_take();
            if (rounds > 0) {
                em_weapon_reset(em_weapon_mag(),
                                (int16_t)(em_weapon_reserve() + rounds));
                printf("pickup: +%d reserve rounds (mag %u, reserve "
                       "%d)\n", rounds, em_weapon_mag(),
                       em_weapon_reserve());
            }
        }
    }
    /* EXAMINE objects (em_examine.h — the overlay examine behaviors:
     * archetype scan on the same CROSS press edge, then the scripted
     * sequence: input pause + the mode-2 radio line + the optional
     * op00 camera cue). Doors-first like the pickup scan above; the
     * running sequence suppresses its own scan internally. */
    em_examine_update(g.pos, g.yaw, em_frame_input(),
                      !em_door_movement_locked() &&
                      !player_damage_locked());
    /* ELEVATOR (AREA-11 opening descent, ov 0x00828050 — the powered
     * terminal examine installs it via em_game_elevator_start, called
     * inside em_examine_update above). Ticks the 150-frame DOWN carry of
     * the player ground-Y + platform mesh-Y; the ride lock in
     * player_move keeps the player standing so this owns g.pos[1]. Runs
     * AFTER the examine update (a start this frame begins integrating
     * next frame — the engine's install-then-tick latency) and BEFORE
     * camera_update (the camera target tracks the descended player-Y). */
    elevator_tick();
    /* ENEMIES: the enemy state machines (func_001551B0 crates +
     * func_00153F10 worms — also part of the actor-pool tick). Runs
     * BEFORE the weapon update so this frame's shot resolves against
     * current positions. */
    em_enemy_update(&g.coll, g.pos);
    /* PLAYER DAMAGE pipeline (the PD_* block above). The port's enemy
     * producers post one mailbox int (em_enemy.h, +0x36 code layout);
     * the engine's player producers instead write the pending-damage
     * floats directly — the bridge maps the two codes onto the decoded
     * fields: the worm's lunge connect (type bit 0x4000, amount 15 =
     * the decoded lunge latch D_008104D4) -> pending
     * HEALTH +0x224; the open breather pad (GEN_TRAP_HIT = 5, the s33
     * event-3 write) -> pending INFECTION +0x22C = 5.0 (the engine pad
     * infects, it does not wound — the old health consume here was the
     * P3/C12 gap). Then the processor (func_0021C440 generic tail)
     * applies and routes to flinch/death, and the passive vitals tick
     * (drain/kill plane/i-frames) runs.
     * RE-VERIFIED (src/func_0021C440.c, NEARMISS — 99.77%): the
     * processor itself never touches health. Its generic tail is
     * `if (!+0x224 && !+0x22C) skip; if (func_0021BC40(p)) skip;` then
     * +0x224 -> func_0021C350 (health apply, variant +0x1F1 = 0, or 4
     * when the type byte +0xF == 0xC) and +0x22C -> func_0021C270
     * (infection apply, variant +0x1F1 = 1) — which is exactly the
     * port's player_apply_health / player_apply_infection split. It
     * then routes on health <= 0 to the death entry (marker 0x3F when
     * +0xF == 0x63 or the infected latch +0x234 == 1, else 0x40) and
     * otherwise to the flinch entry (marker 0x3E). Its tail also
     * carries the low-health latch verbatim: `if (+0x220 <= 35.0f)
     * +0x235 |= 1` — the source of PD_LOW_HEALTH = 35.0f. The
     * +0x224 = HEALTH / +0x22C = INFECTION reading and the pad's 5.0
     * are FINDINGS' player-producer table (D_008104D4 / D_008104DC),
     * not this function, which only sees them as two pending floats. */
    {
        int hitcode = em_enemy_player_hit_take();
        if (hitcode & 0x4000)
            g.pd_pend_hp += (float)(hitcode & 0xFFF);
        else if (hitcode)
            g.pd_pend_inf += (float)hitcode;
        player_damage_process();
        player_struggle_tick();      /* bug-latch struggle (mash CROSS) */
        player_vitals_tick();
        /* (the GO machine ticks from the frozen gate above once
         * GO_SCREEN is reached — the live path never runs it) */
    }
    /* WEAPON: the player-side armed-stance/fire state machine (engine:
     * part of the player actor update, modes 0x1D..0x20) plus the
     * gun-actor fire-event consumption (engine: pool tick, one-frame
     * latency) — both in em_weapon_update; see em_weapon.h. During the
     * door-transit INPUT LOCK the machine reads NEUTRAL input (actions
     * ignored: no draw/fire/reload; a held stance settles to holstered),
     * keeping its per-frame timers ticking. */
    {
        static const EmFrameInput kNeutral = { 0x80, 0x80, 0x80, 0x80,
                                               0, 0, 0 };
        /* the hit-reaction/death lock reads NEUTRAL like the transit
         * lock (engine state 2 clears the trigger latch +0x274/+0x276
         * every frame — func_0015BA50 tail) */
        em_weapon_update(&g.coll, g.pos, g.yaw,
                         (em_door_movement_locked() ||
                          player_damage_locked()) ? &kNeutral
                                                : em_frame_input());
    }
    camera_update();         /* func_001CB590(0x008101E0, 0xD0, 0) +
                              * func_0018B9C0 camera state machine    */
    em_sfx_listener(g.pos, g.cam.eye, g.cam.yaw);  /* positional-audio
                              * listeners: player = distance
                              * (D_00810360), camera eye/yaw = pan
                              * (D_008105D0 / cam+0x9C) — em_sfx.h    */
    frame_close_out();       /* func_001CB5A0/001AAD00/001D1EA0(1)    */
}

/* func_001AE6B0 — cutscene/scripted frame variant. Skeleton only: the
 * native selector never routes here yet. The original polls the frame
 * input block's button words for mask 0x0900 (the third halfword,
 * 0x00810E74) to allow skipping. */
static void cutscene_frame(void)
{
    const EmFrameInput *in = em_frame_input();
    if (in->pressed & 0x0900) {
        /* skip request — unhandled until cutscenes exist natively */
    }
}

/* ------------------------------------------------------------------ */
/* State machines (slot-0 task chain)                                  */
/* ------------------------------------------------------------------ */

/* func_001AE040 — in-game frame machine (state byte task+0xB). */
static void ingame_frame_machine(EmTask *self)
{
    switch (self->user[GAME_BYTE_FRAME]) {
        case 0:
            /* Scene-init arm: the engine resets per-frame flags, builds
             * the difficulty map, places the player against the area
             * spawn tables, and initializes camera + HUD/weapon contexts.
             * Natively the spawn-table stand-in is the scene manifest's
             * spawn (the office kPlayerPos default; origin with no scene
             * loaded), and the camera struct is zeroed back to its init
             * state (state 0 -> the one-shot setup arms it behind the
             * player on the next frame, along the spawn facing). */
            g.walk_t         = 0.0;
            g.walk_w         = 0.0f;
            g.step_prev      = 0.0;   /* footstep edge state re-armed */
            g.idle_t         = 0.0;   /* idle cycle re-armed (mode-0
                                       * entry: breathing + 300-frame
                                       * fidget timer) */
            g.idle_phase     = 0;
            g.idle_timer     = IDLE_FIDGET_FRAMES;
            g.fid_t          = 0.0;
            g.fid_w          = 0.0f;
            g.gait           = 0;
            g.loco_tier      = 0;     /* tier ramp re-armed (+0x25C/+0x38) */
            g.loco_upt       = 0.0f;
            g.cam_recenter   = 0;
            g.cam_idle       = 0;
            g.frame_no       = 0;
            g.frame_selector = 0;
            g.sa_req         = 0;     /* scripted-anim mailbox cleared */
            g.sa_cur         = 0;     /* (player anim re-init state)   */
            g.sa_clip        = -1;
            g.sa_req_hold    = 0;
            g.sa_hold        = 0;
            /* ELEVATOR actor re-arm (the descent ov 0x00828050 is
             * installed fresh by the powered terminal script at each area
             * build): reset the per-scene actor state so the descent can
             * run once in the loaded scene. The persistent game-state
             * flags (have_battery / terminal_powered) are NOT touched
             * here — they survive a room-move reload (see em_game_install
             * for the new-game wipe). elev_pos is (re)set by the manifest
             * `elevator` line during scene_manifest_load. The scripted
             * interact-anim lock is per-actor too — clear it so a scene
             * change can't leave the player locked. */
            g.elev_state      = 0;
            g.elev_pending    = 0;
            g.elev_frame      = 0;
            g.interact_active = 0;
            g.interact_clip   = 0;
            g.interact_seen   = 0;
            /* AREA-11 OPENING DIRECTOR re-arm (the D_00810813 step
             * machine — ov 0x8253F0, installed fresh at each area
             * build). The step byte resets to 0 (the opening plays once
             * per AREA-11 build; live-confirmed pristine 0 at new game).
             * The per-beat transient is fully cleared so a scene change
             * can NEVER leave the player locked in a cinematic — the
             * soft-lock guard. Outside AREA-11 the director simply never
             * arms (no zone is ever entered), so this is inert. */
            g.cine_step       = 0;
            g.cine_active     = 0;
            g.cine_beat       = -1;
            g.cine_kf         = 0;
            g.cine_kf_t       = 0;
            g.cine_fade       = 0;
            g.cine_was        = 0;
            g.pos[0] = g.n_scene ? g.spawn[0] : 0.0f;
            g.pos[1] = g.n_scene ? g.spawn[1] : 0.0f;
            g.pos[2] = g.n_scene ? g.spawn[2] : 0.0f;
            g.yaw    = g.n_scene ? g.spawn_yaw : 0.0f;
            if (g.door_test || g.capture_door || g.pause_test) {
                /* EM_DOOR_TEST / EM_CAPTURE_DOOR / EM_PAUSE_TEST spawn:
                 * the z = -225 corridor line in front of the west
                 * double door, facing -X (see door_test_script;
                 * EM_PAUSE_TEST uses the same approach for its door leg
                 * — the mid-walk-out menu check). */
                g.pos[0] = 72.0f;
                g.pos[1] = 0.0f;
                g.pos[2] = -225.0f;
                g.yaw    = -EM_PI * 0.5f;
            }
            if (g.capture_supply) {
                /* EM_CAPTURE_SUPPLY spawn: on the x = 104 doorway-center
                 * column of the office DOUBLE DOORS (door id 2), facing
                 * them (south, yaw pi) — the approach + CROSS below
                 * carries the transit into the SUPPLY ROOM (area 2 room
                 * 1 entry 3), whose spawn-record FIXED camera the
                 * capture frame samples. */
                g.pos[0] = 104.0f;
                g.pos[1] = 0.0f;
                g.pos[2] = -238.0f;
                g.yaw    = EM_PI;
            }
            if (g.transit_test) {
                /* EM_TRANSIT_TEST spawn: on the west DOORWAY-CENTER z
                 * line (z = -225.5 — the placement pos is the HINGE
                 * corner; the decoded use scan measures from the
                 * center), 9 u east of it, facing it (see
                 * transit_test_script). */
                g.pos[0] = 66.0f;
                g.pos[1] = 0.0f;
                g.pos[2] = -225.5f;
                g.yaw    = -EM_PI * 0.5f;
            }
            if (g.slider_test) {
                /* EM_SLIDER_TEST spawn (scene_drawbridge): on the
                 * slider door 4's z = -610 line, 16.6 u west of it,
                 * facing +X (see slider_test_script). */
                g.pos[0] = 112.0f;
                g.pos[1] = 0.0f;
                g.pos[2] = -610.0f;
                g.yaw    = EM_PI * 0.5f;
            }
            if (g.locked_test) {
                /* EM_LOCKED_TEST spawn (scene_drawbridge): on the m15
                 * security door's doorway-center column (center
                 * (-25.5, -192)), BACK side, 9 u out, facing the door
                 * (+Z) — see locked_test_script. */
                g.pos[0] = -25.5f;
                g.pos[1] = 0.0f;
                g.pos[2] = -201.0f;
                g.yaw    = 0.0f;
            }
            memset(&g.cam, 0, sizeof g.cam);
            g.cam.yaw = g.yaw;   /* chase camera starts behind the spawn */
            g.cam_region_on = 0;
            /* EM_CAMREGION_TEST: SYNTHETIC test region (FLAGGED — it
             * REPLACES the scene's region list for the run, so the
             * test stays spawn-local and deterministic even now that
             * the office carries the REAL supply-room line, the
             * spawn-record camera decode in the CAMERA FIDELITY
             * block). A strip starting 5 u down +Z of the spawn (the
             * run-forward corridor; the wall radius stops the player
             * ~14 u in, well inside), with the fixed eye raised
             * BEHIND the spawn looking INTO the strip (real room
             * cameras watch the room — and camera-relative 'w' then
             * keeps pushing the player deeper, not back across the
             * boundary): entering it must pin the camera there.
             * scene_snow's REAL region (AREA06, D_0024A5F0[2]) and
             * the supply-room line exercise this same machinery. */
            if (g.camregion_test) {
                g.n_camregion  = 1;
                g.camregion[0] = (EmCamRegion){
                    .x0 = g.pos[0] - 25.0f, .z0 = g.pos[2] + 5.0f,
                    .x1 = g.pos[0] + 25.0f, .z1 = g.pos[2] + 34.0f,
                    .ygate  = g.pos[1],
                    .eye    = { g.pos[0] - 6.0f, g.pos[1] + 24.0f,
                                g.pos[2] - 12.0f } };
                printf("camregion test: SYNTHETIC region armed — "
                       "X[%.1f,%.1f] Z[%.1f,%.1f], eye (%.1f, %.1f, "
                       "%.1f)\n",
                       g.camregion[0].x0, g.camregion[0].x1,
                       g.camregion[0].z0, g.camregion[0].z1,
                       g.camregion[0].eye[0], g.camregion[0].eye[1],
                       g.camregion[0].eye[2]);
            }
            /* Weapon context init (the engine's HUD/weapon-context arm):
             * holstered, ammo from the demo status (live test save:
             * mag 4, reserve 120). The HUD mirrors the weapon live from
             * here on (frame_close_out). */
            em_weapon_reset(g.status.mag, g.status.reserve);
            /* EM_ENEMY_TEST spawn: test 2 places one WORM 30 units
             * ahead of the player spawn along the spawn facing (its
             * own INIT yaws it at the player — the decoded
             * func_00154040); tests 1/3 place a DISGUISED CRATE 12 /
             * 25 units ahead instead (see enemy_test_script). */
            /* EM_MELEE_TEST spawn: TWO DISGUISED CRATES — A 11.0 u dead
             * ahead (slot 0, inside the knife reach 12, inside the
             * office spawn's 14-u wall plane — the LIGHT-combo kill)
             * and B 25 u BEHIND the player down the open south
             * corridor (slot 1 — the group-alarm WITNESS: the decoded
             * broadcast walks the whole live list with NO radius, so
             * a crate clear across the room must wake too). B faces
             * south (away): its alarm-driven suicide hop runs down
             * the open corridor — never near the melee cone — and its
             * 180-tick timer bursts it there. (Facing it at the 14-u
             * wall boxed the steer in and the engine-true budget
             * returned it to dormancy — the run needs open ground.) */
            if (g.melee_test && !g.et_spawned) {
                g.et_spawned = 1;
                float fx = sinf(g.yaw), fz = cosf(g.yaw);
                float pa[3] = { g.pos[0] + fx * 11.0f, g.pos[1],
                                g.pos[2] + fz * 11.0f };
                float pb[3] = { g.pos[0] - fx * 25.0f, g.pos[1],
                                g.pos[2] - fz * 25.0f };
                /* explicit nest count (2) — CRATE_BUGS_DEFAULT is now 0
                 * (a tag-less crate hatches nothing, s76), so the test
                 * crates must request their bugs like a real scene line */
                if (em_enemy_add_crate(em_frame_gfx(), pa, g.yaw + EM_PI,
                                       2, 6) < 0 ||
                    em_enemy_add_crate(em_frame_gfx(), pb, g.yaw + EM_PI,
                                       2, 6) < 0)
                    printf("melee test: spawn failed\n");
            }
            /* EM_DEATH_TEST shares the contact-run placement: one
             * worm 30 u dead ahead (death_test_script). */
            if ((g.enemy_test || g.death_test) && !g.et_spawned) {
                g.et_spawned = 1;
                /* test 1 (kill run, restaged s66): the SHOOTABLE
                 * target is a CRATE 12 u dead ahead — inside the
                 * office wall plane 14 u out, so the world ray ranks
                 * behind the victim. Test 3: crate 25 u down the open
                 * corridor (about-face below). Test 2 / death test:
                 * one worm 30 u ahead. */
                int   ek = (g.enemy_test == 1 || g.enemy_test == 3)
                           ? EM_ENEMY_KIND_CRATE
                           : EM_ENEMY_KIND_CRAWLER;
                float ed = g.enemy_test == 1 ? 12.0f
                         : g.enemy_test == 3 ? 25.0f : 30.0f;
                /* Crate run only: the office spawn faces a wall plane
                 * 14 u ahead (see the kill-run wall note), so a walking
                 * player could never reach point-blank range at a
                 * crate 25 u beyond it. About-face the spawn pose
                 * (player + chase camera — the one-shot camera arm
                 * reads g.cam.yaw next frame) so the crate goes 25 u
                 * down the OPEN south corridor, still dead ahead. */
                if (g.enemy_test == 3) {
                    g.yaw    += EM_PI;
                    g.cam.yaw = g.yaw;
                }
                float ep[3] = { g.pos[0] + sinf(g.yaw) * ed,
                                g.pos[1],
                                g.pos[2] + cosf(g.yaw) * ed };
                /* a CRATE target requests its nest explicitly (2 bugs)
                 * — the default is now 0 (s76); a worm uses add_kind */
                int et_spawn = (ek == EM_ENEMY_KIND_CRATE)
                    ? em_enemy_add_crate(em_frame_gfx(), ep, g.yaw + EM_PI,
                                         2, 6)
                    : em_enemy_add_kind(em_frame_gfx(), ek, ep,
                                        g.yaw + EM_PI);
                if (et_spawn < 0)
                    printf("enemy test: spawn failed\n");
            }
            self->user[GAME_BYTE_FRAME] = 1;
            /* fall through — the engine's init frame still renders */
        case 1:
            /* CONTINUE RESTART (the decoded dispatch: prompt cursor 0
             * confirmed at hold-black — the engine reinstalls the
             * gameplay task func_001ACEC0 with the from-death flag
             * up and D_00275BE0 = 0; PD block doc): reload the active
             * scene — the manifest re-run rebuilds doors/enemies/
             * collision — restore the boot status, clear the damage
             * machine, re-arm the scene-init state (player re-place +
             * camera + weapon), and fade back in. */
            if (g.go_restart) {
                g.go_restart  = 0;
                g.go_state    = GO_OFF;
                g.go_frames   = 0;
                g.go_hold     = 0;
                g.go_timer    = 0;
                g.go_cursor   = 0;
                g.pd_state    = 0;
                g.pd_sub      = 0;
                g.pd_phase    = 0;
                g.pd_hold     = 0;
                g.pd_iframes  = 0;
                g.pd_pend_hp  = 0.0f;
                g.pd_pend_inf = 0.0f;
                g.pd_drain_t  = 0;
                g.pd_clip     = 0;
                /* infected latch persists across a continue? Unknown —
                 * the port restores the boot status wholesale
                 * (flagged). */
                g.pd_infected = 0;
                g.pd_low      = 0;
                g.status = (EmPlayerStatus){ .health = 75.0f,
                                             .health_max = 100.0f,
                                             .infection = 60.0f,
                                             .mag = 4, .mag_max = 30,
                                             .reserve = 120,
                                             .battery = 4,
                                             .battery_max = 6 };
                g.et_spawned = 0;     /* self-tests may re-spawn */
                em_game_scene_switch(g.scene_dir);
                em_frame_fade_start(-1, EM_FADE_SPEED_DOOR);
                self->user[GAME_BYTE_FRAME] = 0;
                ingame_frame_machine(self);   /* re-init this frame */
                break;
            }
            /* Live in-game arm: per-frame services (func_001AFCF0 flag
             * reset, func_001B07C0(1) placement check, func_001C1DC0
             * render-env updater (channel enables / fog / weather — NOT
             * camera math; FINDINGS.md "CAMERA SYSTEM" corrections),
             * func_001AE7E0 end-of-level poll — all pending translation),
             * then the frame-variant selector (scratchpad 0x70003B8D). */
            if (g.frame_selector)
                cutscene_frame();   /* func_001AE6B0 */
            else
                gameplay_frame();   /* func_001AE5E0 */
            break;
        default:
            /* Remaining jr-table 0x0026DD30 arms (pause/level-exit paths)
             * — pending translation. */
            break;
    }
}

/* func_001AD250 — game sub-machine (state byte task+9; 6 states in
 * jr-table 0x0026DCB0). The live arm reaches the in-game frame machine
 * through the trampoline func_001AD4D0 (= j func_001AE040). */
static void game_sub_machine(EmTask *self)
{
    switch (self->user[GAME_BYTE_SUB]) {
        case 0:
            /* Setup arm — natively nothing to stage yet. */
            self->user[GAME_BYTE_SUB] = 1;
            /* fall through */
        case 1:
            ingame_frame_machine(self);  /* via the func_001AD4D0 jump */
            break;
        default:
            /* States 2..5 (menu/loading/teardown arms) — pending. */
            break;
    }
}

/* func_001ACEC0 — game task machine (state byte task+8; live value 3). */
static void game_task(void)
{
    EmTask *self = em_task_current();
    switch (self->user[GAME_BYTE_MAIN]) {
        case 0:
            /* Entry arm. The original walks intermediate mode arms (the
             * live record sits at state 3); the skeleton jumps straight
             * to the in-game arm. */
            self->user[GAME_BYTE_MAIN] = 3;
            /* fall through */
        case 3:
            game_sub_machine(self);      /* func_001AD250 */
            break;
        default:
            /* Other mode arms (title/menu flows) — pending. */
            break;
    }
}

/* func_001AB7E0 — boot/flow task: first dispatch loads the assets, then
 * slot 0 is re-registered with the game task, exactly mirroring the live
 * engine (slot 0 = func_001ACEC0 after boot). */
static void game_boot_task(void)
{
    EmGfx *gfx = em_frame_gfx();

    /* Optional character asset (disc-derived, generated locally). */
    if (em_model_load(&g.model, MODEL_PATH) == 0) {
        g.mesh = em_gfx_mesh_create(gfx, g.model.verts, g.model.vert_count,
                                    g.model.indices, g.model.index_count,
                                    (const EmGfxTexDesc *)g.model.texs,
                                    g.model.tex_count, g.model.texels,
                                    g.model.flags);
        printf("loaded %s: %u bones, %u verts, %u tris, %u frames @ %.0f fps, "
               "%u clips, %u textures\n",
               MODEL_PATH, g.model.bone_count, g.model.vert_count,
               g.model.index_count / 3, g.model.frame_count, g.model.fps,
               g.model.clip_count, g.model.tex_count);
        /* Resolve the named clips. Idle = the engine's breathing idle
         * (id 0); the fidget 349 enables the idle CYCLE. Degradations:
         * an asset without clip id 0 idles on 349 alone (the pre-s38
         * single look-around — cycle off), and an old single-clip
         * (EMD2) asset keeps the original behavior: idle = clip 0 of
         * the table, no crossfade. */
        g.clip_idle   = em_model_clip_index(&g.model, CLIP_ID_IDLE);
        g.clip_fidget = g.clip_idle >= 0
                      ? em_model_clip_index(&g.model, CLIP_ID_FIDGET)
                      : -1;
        if (g.clip_idle < 0)
            g.clip_idle = em_model_clip_index(&g.model, CLIP_ID_FIDGET);
        if (g.clip_idle < 0) g.clip_idle = 0;
        g.clip_walk = em_model_clip_index(&g.model, CLIP_ID_WALK);
        if (g.clip_walk == g.clip_idle) g.clip_walk = -1;
        g.clip_jog  = em_model_clip_index(&g.model, CLIP_ID_JOG);
        if (g.clip_jog == g.clip_idle) g.clip_jog = -1;
        g.clip_run  = em_model_clip_index(&g.model, CLIP_ID_RUN);
        if (g.clip_run == g.clip_idle) g.clip_run = -1;
        g.loco_clip  = g.clip_walk;
        g.loco_speed = WALK_CLIP_SPEED;
        /* Status-menu UI scene clips (engine 0x1C2 stance / 0xA low-
         * health idle). An older asset without them falls back to the
         * breathing idle in ui_scene_render — flagged here once. */
        g.clip_menu     = em_model_clip_index(&g.model, CLIP_ID_MENU);
        g.clip_menu_low = em_model_clip_index(&g.model, CLIP_ID_MENU_LOW);
        if (g.clip_menu == g.clip_idle)     g.clip_menu = -1;
        if (g.clip_menu_low == g.clip_idle) g.clip_menu_low = -1;
        if (g.clip_menu < 0)
            printf("menu stance clip 450 missing — status screen uses "
                   "the breathing idle (re-export with --clips "
                   "...,0,450,10)\n");
        printf("clips: idle #%d (id %u)%s%s\n", g.clip_idle,
               g.model.clips[g.clip_idle].id,
               g.clip_fidget >= 0 ? ", idle cycle on (fidget 349)"
                                  : ", idle cycle off",
               g.clip_walk >= 0 ? ", walk found — locomotion crossfade on"
                                : " only — crossfade off (re-export with "
                                  "--clips 349,2,3,69,67,75,...,0)");
    } else {
        printf("no %s — showing the test triangle. Generate it with the "
               "decomp repo's tools/export_native.py\n", MODEL_PATH);
    }

    /* Optional scene (level parts, world-space) + its manifest (spawn /
     * collision filename / bgm / doors — office defaults when absent). */
    em_door_reset();
    em_enemy_reset();
    scene_manifest_load();
    g.n_scene = scene_load(gfx, g.scene, SCENE_MAX);

    /* Optional collision world (id 0x44 -> EMCL, disc-derived, generated
     * locally by the decomp repo's tools/export_collision.py; filename
     * from the scene manifest). Without it movement falls back to the
     * room-bbox clamp. */
    if (em_collision_load(&g.coll, g.coll_path) == 0) {
        printf("collision: %s — %u polys (%u verts), grid %s\n",
               g.coll_path, g.coll.poly_count, g.coll.vert_count,
               (g.coll.flags & 1) ? "decoded" : "absent (flat-floor)");
    } else {
        printf("no %s — movement uses the room-bbox clamp. Generate it "
               "with the decomp repo's tools/export_collision.py\n",
               g.coll_path);
    }

    /* SFX registry preload (assets/sfx/sfx.txt) — the native stand-in
     * for the area's SShd bank load. No registry = the module stays a
     * silent no-op (zero behavior change); see em_sfx.h. */
    em_sfx_init();

    /* BGM at the boot->game handoff — the native func_001FB0B0 moment:
     * on the PS2 the area flow writes the level's cue id to the
     * current-BGM global D_00810D38 and func_001FAE70 fades the stream
     * in (the in-level cues carry the loop flag in the D_0025DD30 table).
     * Natively EM_BGM=<path.wav> names a locally exported cue WAV and
     * stands in for the cue id until the native cue table lands; without
     * the env, the scene manifest's optional "bgm <file.wav>" (a cue WAV
     * in the scene dir) plays instead; neither = silent, exactly as
     * before (em_bgm never opens a device). */
    if (g.bgm_path) {
        em_bgm_play(g.bgm_path, 1);  /* func_001FB0B0(cue) — looping BGM */
    } else if (g.bgm_file[0]) {
        char path[560];
        snprintf(path, sizeof path, "%s/%s", SCENE_DIR, g.bgm_file);
        em_bgm_play(path, 1);
    }

    em_task_register(0, game_task);  /* func_001AB740(0, func_001ACEC0) */
}

void em_game_install(void)
{
    memset(&g, 0, sizeof g);
    snprintf(g.scene_dir, sizeof g.scene_dir, "%s", SCENE_DIR);
    /* Player status — static demo values matching the live test save
     * (FINDINGS.md "INVENTORY LOCATED": health 75/100, infection 60%,
     * mag 4/30, reserve 120, battery 04/06) until the weapon/health
     * systems are translated. */
    g.status = (EmPlayerStatus){ .health = 75.0f, .health_max = 100.0f,
                                 .infection = 60.0f, .mag = 4,
                                 .mag_max = 30, .reserve = 120,
                                 .battery = 4, .battery_max = 6 };
    g.bgm_path     = getenv("EM_BGM");
    g.capture_path = getenv("EM_CAPTURE");
    const char *cf = getenv("EM_CAPTURE_FRAME");
    g.capture_frame = cf ? atoi(cf) : 60;   /* default = the historical
                                               regression frame */
    const char *ca = getenv("EM_CAPTURE_AIM");
    g.capture_aim  = ca ? atoi(ca) : 0;     /* 1 = aim, 2 = aim + shot,
                                               3/4 = aim up/down pose,
                                               5 = aim near a wall (the
                                               func_0018F870 pull-in) */
    if (g.capture_aim == 5 && !cf)
        g.capture_frame = 420;              /* aim settled at the wall */
    const char *cd = getenv("EM_CAPTURE_DOOR");
    g.capture_door = cd && cd[0] == '1';    /* door cinematic capture */
    if (g.capture_door && !cf)
        g.capture_frame = 110;              /* mid-cinematic default */
    const char *cs = getenv("EM_CAPTURE_SUPPLY");
    g.capture_supply = cs && cs[0] == '1';  /* supply-room fixed camera */
    if (g.capture_supply && !cf)
        g.capture_frame = 360;              /* post-transit, in-room */
    const char *cr = getenv("EM_CAPTURE_RISE");
    g.capture_rise = cr && cr[0] == '1';    /* walk-at-camera rise demo */
    const char *cx = getenv("EM_CAPTURE_EXAMINE");
    g.capture_examine = cx ? atoi(cx) : 0;  /* N = examine slot N-1 (snow:
                                             * 1 = AREA06 switch message,
                                             * 2 = AREA11 refusal line) */
    if (g.capture_examine && !cf)
        g.capture_frame = 60;               /* message mid-presentation */
    const char *co = getenv("EM_CAPTURE_ORIENT");
    g.capture_orient = co && co[0] == '1';  /* idle auto-orient demo */
    const char *cw = getenv("EM_CAPTURE_WALK");
    g.capture_walk = cw && cw[0] == '1';    /* plain mid-walk framing */
    const char *cp = getenv("EM_CAM_PRINT");
    g.cam_print = cp && cp[0] == '1';       /* dump the settled camera
                                             * state at the capture frame
                                             * (idle-emergence A/B read) */
    const char *cl = getenv("EM_CAPTURE_LOCKED");
    g.capture_locked = cl ? atoi(cl) : 0;   /* locked-look determinism:
                                             * approach variant 1 or 2 */
    if (g.capture_locked && !cf)
        g.capture_frame = 110;              /* mid locked-look hold */
    const char *mt = getenv("EM_MOVE_TEST");
    g.move_test    = mt && mt[0] == '1';
    g.move_legs[0] = 60;   /* the historical office legs */
    g.move_legs[1] = 30;
    const char *ml = getenv("EM_MOVE_LEGS");
    if (ml) {
        int l0, l1;
        if (sscanf(ml, "%d,%d", &l0, &l1) == 2 && l0 > 0 && l1 > 0) {
            g.move_legs[0] = l0;
            g.move_legs[1] = l1;
        }
    }
    const char *me = getenv("EM_MOVE_EXPECT");
    if (me && sscanf(me, "%f,%f,%f", &g.move_expect[0], &g.move_expect[1],
                     &g.move_expect[2]) == 3)
        g.move_expect_set = 1;
    const char *dt = getenv("EM_DOOR_TEST");
    g.door_test    = dt && dt[0] == '1';
    const char *tt = getenv("EM_TRANSIT_TEST");
    g.transit_test = tt && tt[0] == '1';
    const char *sl = getenv("EM_SLIDER_TEST");
    g.slider_test  = sl && sl[0] == '1';
    const char *lk = getenv("EM_LOCKED_TEST");
    g.locked_test  = lk && lk[0] == '1';
    const char *wt = getenv("EM_WEAPON_TEST");
    g.weapon_test  = wt && wt[0] == '1';
    const char *et = getenv("EM_ENEMY_TEST");
    if (et && et[0] >= '1' && et[0] <= '3')
        g.enemy_test = et[0] - '0';
    g.enemy_test4 = et && et[0] == '4' && et[1] == '\0';
    const char *st = getenv("EM_SFX_TEST");
    g.sfx_test     = st && st[0] == '1';
    const char *pt = getenv("EM_PAUSE_TEST");
    g.pause_test   = pt && pt[0] == '1';
    const char *cg = getenv("EM_CAMREGION_TEST");
    g.camregion_test = cg && cg[0] == '1';
    const char *at = getenv("EM_AIM_TEST");
    g.aim_test     = at && at[0] == '1';
    const char *kt = getenv("EM_MELEE_TEST");
    g.melee_test   = kt && kt[0] == '1';
    const char *ct = getenv("EM_CINE_TEST");
    g.cine_test    = ct && ct[0] == '1';
    const char *gt = getenv("EM_DEATH_TEST");
    g.death_test   = gt && gt[0] == '1';
    const char *ik = getenv("EM_PICKUP_TEST");
    g.pickup_test  = ik && ik[0] == '1';
    const char *xt = getenv("EM_EXAMINE_TEST");
    g.examine_test = xt && xt[0] == '1';
    em_pickup_reset();   /* new-game inventory/taken wipe — the engine's
                          * D_00810700-block memset (func_001AF2C0) */
    /* AREA-11 progression flags are part of that same D_00810700-block
     * memset (D_00810811 battery, D_00810841[11] terminal unlock): wiped
     * at NEW GAME only. They PERSIST across an intra-area room-move scene
     * reload (the battery taken-bit + the unlock bit are game state, not
     * per-scene) — so they are NOT re-zeroed on every scene arm; only the
     * elevator ACTOR (re-installed per area build) resets on scene load. */
    g.have_battery     = 0;       /* D_00810811 = 0 */
    g.terminal_powered = 0;       /* D_00810841[11] bit 7 = 0 */
    em_task_register(0, game_boot_task);  /* func_001AB740(0, 0x001AB7E0) */
}

void em_game_shutdown(void)
{
    EmGfx *gfx = em_frame_gfx();
    if (g.mesh) {
        em_gfx_mesh_destroy(gfx, g.mesh);
        em_model_free(&g.model);
        g.mesh = NULL;
    }
    for (int i = 0; i < g.n_scene; i++) {
        em_gfx_mesh_destroy(gfx, g.scene[i].mesh);
        em_model_free(&g.scene[i].model);
        free(g.scene[i].palette);
    }
    g.n_scene = 0;
    elevator_unload(gfx);       /* AREA-11 platform mesh */
    grate_unload(gfx);          /* AREA-11 gated-grate bars + hull */
    em_truck_clear(gfx);        /* AREA-11 wedged-truck actor + mesh */
    em_door_shutdown(gfx);
    em_enemy_shutdown(gfx);
    em_pickup_scene_clear(gfx);
    em_examine_reset();
    em_collision_free(&g.coll);
    em_bgm_shutdown();  /* blocks out the audio thread, then frees + prints */
    em_sfx_shutdown();  /* AFTER em_bgm_shutdown — the device-teardown
                         * guarantee makes the sample memory freeable */
}

/* ------------------------------------------------------------------ */
/* Scripted-anim mailbox (see em_game.h for the engine mapping)        */
/* ------------------------------------------------------------------ */

static int anim_request_common(unsigned clip_id, float rate, int hold)
{
    if (!g.mesh || clip_id == 0)
        return 0;
    if (em_model_clip_index(&g.model, (uint32_t)clip_id) < 0) {
        printf("anim request: player EMDL carries no clip id %u "
               "(0x%X) — re-export with it in --clips\n", clip_id,
               clip_id);
        return 0;
    }
    g.sa_req      = clip_id;        /* +0x1F2 */
    g.sa_rate     = rate;           /* +0x1F8 */
    g.sa_req_hold = hold;
    return 1;
}

int em_game_anim_request(unsigned clip_id, float rate)
{
    return anim_request_common(clip_id, rate, 0);
}

int em_game_anim_hold(unsigned clip_id, float rate)
{
    return anim_request_common(clip_id, rate, 1);
}

/* FIRE-RECOIL rewind (em_game.h): the native bone_matrix_publish
 * counter re-seed — the committed hold's playhead snaps back to frame
 * 0 (the engine's per-shot `+0x276 = 0` write) and replays the clip's
 * front-loaded recoil at `rate` frames/tick back into the clamped
 * hold. Not yet committed (or a different clip): plain hold request. */
int em_game_anim_hold_restart(unsigned clip_id, float rate)
{
    if (g.sa_cur == clip_id && g.sa_clip >= 0) {
        g.sa_t        = 0.0;        /* fire counter reset (+0x276 = 0) */
        g.sa_rate     = rate;
        g.sa_hold     = 1;
        g.sa_req      = clip_id;    /* keep request == commit: no re-init */
        g.sa_req_hold = 1;
        return 1;
    }
    return anim_request_common(clip_id, rate, 1);
}

int em_game_anim_frames(unsigned clip_id)
{
    int ci;
    if (!g.mesh || clip_id == 0)
        return 0;
    ci = em_model_clip_index(&g.model, (uint32_t)clip_id);
    return ci < 0 ? 0 : (int)g.model.clips[ci].frame_count;
}

void em_game_anim_cancel(void)
{
    g.sa_req      = 0;              /* op 0x18 teardown: +0x1F2 = 0 */
    g.sa_req_hold = 0;
}

unsigned em_game_anim_active(void)
{
    return g.sa_cur;                /* +0x20C */
}

int em_game_anim_frame(void)
{
    if (!g.sa_cur || g.sa_clip < 0)
        return -1;
    double end = (double)(g.model.clips[g.sa_clip].frame_count - 1);
    return (int)(g.sa_t < end ? g.sa_t : end);
}

/* Manual aim steer state (em_game.h) */
float em_game_aim_pitch(void)     { return g.aim_pitch; }
float em_game_aim_yaw_blend(void) { return g.aim_yawb; }
void  em_game_aim_dir(float out[3]) { aim_dir_get(out); }

