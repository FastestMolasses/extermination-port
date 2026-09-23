/* em_game.c — slot-0 game task chain (PS2 -> native mapping).
 *
 * Engine chain (FINDINGS.md "ENGINE FRAME ANATOMY") and what stands in for
 * each stage here:
 *
 *   func_001AB7E0  boot/flow task        game_load_task() — one frame:
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
 *                  (anim_frame_top_b,    byte 3 (task+0xB). State 0 builds
 *                  NEARMISS; state byte  the area and returns without a
 *                  task+0xB)             world frame; state 1 polls
 *                                        001AE7E0, then (when D_00275BD8
 *                                        is 0) runs the variant picked
 *                                        by scratchpad 0x70003B8D. See
 *                                        docs/SCENE_COORDINATOR_DESIGN.md
 *                                        §2.3.
 *   func_001AE5E0  GAMEPLAY FRAME        gameplay_frame() — see below.
 *   func_001AE6B0  cutscene variant      cutscene_frame() — skeleton; the
 *                                        native selector never routes here
 *                                        yet.
 *
 * GAMEPLAY FRAME stages (func_001AE5E0) -> native. CONFIRMED literally
 * (audit 2026-07-31) against src/func_001AE5E0.c [NEARMISS — logic
 * authoritative]: that function is a 13-call straight line and the list
 * below is its body in order, arguments included — bump the per-frame
 * counters D_00810750 / 0x70003B68, then
 *   func_001CB590(D_008102B0, 0x320, D_008102B9, frame); func_0015BCF0;
 *   func_001CB5A0; func_001D1C50; func_001C1D00(D_008101D0);
 *   func_001AFD70(0); func_0015C160; func_001F0360;
 *   func_001CB590(D_008101E0, 0xD0, 0, 0); func_0018B9C0;
 *   func_001CB5A0; func_001AAD00; func_001D1EA0(1).
 * (This also OVERTURNS an earlier audit note in em_game.h that read
 * func_001AE5E0 as "a per-level INIT routine" — there is no level setup
 * in it; D_00810750 is bumped every call and handed straight to the
 * first func_001CB590 as its 4th argument.)
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
 *   func_001D1C50 per-frame GS/fog setup (src/func_001D1C50.c):
 *                                        001D2830(4,0); a mode dispatch
 *                                        (D_008106C4 / 001B0070 & 0x80 /
 *                                        else) that runs the 0021B970/
 *                                        0021B9A0/0021BA80 fog helpers and
 *                                        001D2830(6,..); seeds the frame's
 *                                        display-list slot from
 *                                        D_00275670, 001D2960 with
 *                                        D_00810610, copies scratchpad
 *                                        blocks 0x70003A40/0x70003AC0;
 *                                        then 001D7C30 (point_light_tick)
 *                                        and 001D30A0. It records no
 *                                        draws. The port's
 *                                        render_chain_build() sits at this
 *                                        slot but is a native draw-list
 *                                        collector, not a translation.
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
 * authoritative), and SCOPED (audit 2026-07-31): in the style-0
 * (arg2 != 3) block the plain-WALL case is the `resolved == 0` tail,
 * which writes only pos[0]/pos[2] (`+= 0.5f * push`) and leaves pos[1]
 * alone — that arm really is a constant-height pull-in, and it is the
 * arm the chase camera hits. It is NOT the whole function: the sibling
 * floor/ceiling arm (surface flags at +0x1A & 0x8800) does
 * `pos[1] -= 1.0f` before the same X/Z push, and the style-3 arm
 * (flags & 0xD800) copies the hit quad over pos[] with func_00102948
 * and then pushes all THREE components. "Constant height" is the wall
 * case only. The player has
 * NO free camera control — R1/L1 orient the camera behind the player,
 * idle auto-orients slowly, and a blocking wall PULLS the eye in at
 * constant height (which reads as the camera rising over the player —
 * the CAMERA FIDELITY block below). While the status screen is open the world
 * simulation PAUSES (the gate in gameplay_frame). Esc still quits
 * (em_frame.c step C).
 *
 * SCENE MANIFEST: assets/scene/scene.txt (see scene_manifest_load) gives
 * each scene its own spawn and collision filename in TRUE world
 * coordinates — written by the decomp repo's exporters, read once at
 * boot. A `bgm` line is accepted and ignored (no original code starts
 * music from scene data). No manifest = the office defaults (historical
 * behavior).
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
#include "game/em_effect_color.h"
#include "game/em_random.h"
#include "game/em_director.h"
#include "game/em_player.h"
#include "game/em_player_damage.h"
#include "game/em_camera.h"
#include "game/em_scene.h"
#include "game/em_props.h"
#include "game/em_opening_runtime.h"
#include "game/em_opening_actor.h"
#include "game/em_snow_runtime.h"
#include "game/em_area11_effect_runtime.h"
#include "game/em_opening_control_test.h"

/* The gameplay state object declared in em_game_internal.h. */
EmGameState g;

/* SCENE MANIFEST — a plain-text scene.txt in the scene directory, written
 * there by the decomp repo's exporters (tools/export_level.py --spawn,
 * tools/export_collision.py). Zero-dependency parser; "key value"
 * lines, '#' starts a comment, unknown keys are ignored:
 *
 *   spawn <x> <y> <z> <yaw>   player spawn, TRUE world coords + facing (rad)
 *   collision <file.emcl>     collision world filename inside the scene dir
 *   bgm <file.wav>            accepted and IGNORED: no original code
 *                             starts music from scene data. Area music
 *                             is 001FAE70's cue choice (D_008106C8 bits
 *                             8..15; AREA11 = 25) — see game_load_task.
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
 *          [owner <fn> <flags2>]
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
 *                             `owner` (prop lines only) = the record's
 *                             behaviour fn (+0x24) and +0x03 byte; its
 *                             state-0 init pose is applied
 *                             (em_pickup_owner_init_pose: 0x827630 only,
 *                             anything else stops the load).
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
 *                             kind/link are the placement fields the
 *                             pad's state-0 init consumes. CITATION
 *                             TIGHTENED (audit 2026-07-31) against
 *                             src/func_0015A2C0.c (NEARMISS — logic
 *                             authoritative): state 0 binds the pad's
 *                             behaviour row as `self+0x30 =
 *                             &D_00248120[self->s16(+0x54) * 5]` (a
 *                             5-float record) and then switches on the
 *                             short at self+0x56 with exactly three
 *                             live cases — 0 (leave as placed), 1 and
 *                             2, which re-roll it out of D_002481B0 /
 *                             D_002481D0 at `[(rand&3)<<3 |
 *                             (D_008106EC/ED & 7)]`, bump that global
 *                             counter, and on a rolled value of 1
 *                             spawn a pair via func_0015A200(self,
 *                             0xE, 0/1). So there really are two
 *                             placement shorts; the recovered C does
 *                             NOT name which manifest word is which,
 *                             and the kind-1 / link-2 defaults are
 *                             PORT DEFAULTS, not source-derived.
 *                             EM_ENEMY_TEST=4 arms
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
/* Since S5 the stage bodies live in em_player_frame.c (em_player_*) and
 * em_render_frame.c (em_render_*, em_camera_0018B9C0); the env-gated
 * self-test scripts live in em_game_selftest.c. gameplay_frame and
 * cutscene_frame below still fix the order. */



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
int probe_wall_seg(const float start[3], const float target[3],
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





/* Per-tier target speed +0x38/D_00248870 (u/tick), shared by the tier
 * ramp in player_move and the C1 cross-blend progress in the anim
 * dispatch (loco_clip_for_tier / the +0x208 progress calc). */
const float kLocoTierSpeed[4] = { 0.0f, 0.1f, 0.3f, 0.8f };



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








/* ------------------------------------------------------------------ */
/* PLAYER DAMAGE & DEATH machine (the PD_* constants block above)      */
/* ------------------------------------------------------------------ */











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
            /* Option 0 — src/func_001AC070.c state 4 reinstalls
             * func_001ACEC0 with D_00275BE0 = 0: the title's New Game
             * route (001AD230 -> 001AF2C0 reset, 001AD360 AREA11
             * 0x0B/0/0). Serviced by the frame machine (go_restart). */
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

/* Terminal-unlock accessors (em_game.h). D_00810841[11] bit 7
 * (D_0081084C & 0x80) is mirrored as a 0/1 flag. D_00810811 is not a
 * battery flag: it is the opening-complete byte the AREA11 opening
 * controller 00823E80 stores at 0x00823F74..80 (g.opening_complete). */
int  em_game_terminal_powered(void)   { return g.terminal_powered; }
void em_game_set_terminal_powered(int on) { g.terminal_powered = on ? 1 : 0; }

/* em_game_player_interact_anim (AREA11 panel/terminal flow; clip ids
 * observed on live RAM, see em_game.h). Play a one-shot scripted clip ON THE
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
           g.cine_active || em_opening_runtime_busy() || player_pose_owned();
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
 * a data/handler address, not a recovered function.
 * CITATION TIGHTENED (audit 2026-07-31): the 0.39269909f literal IS in
 * src/func_00174AC0.c, but its arm is narrower than "gait 0/3" — it is
 * the `arg1 == 1` call form, with the speed float +0x38 == 0.0f
 * (standing) and the gait byte +0x23F NOT 1 and NOT 2 (1 -> 0.06981317,
 * 2 -> 0.13962634; the moving ladder in the sibling arm runs
 * 0.10471976 / 0.15707964 / 0.18325958). The em_examine op04 pre-roll
 * reusing that particular standing rate is still OBSERVED: no recovered
 * examine-script function names it, and the cited
 * INVESTIGATION_examine_walk_face.md exists in neither repo. Treat the
 * easing shape as OBSERVED, not decoded.
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
 * SEQUENCING (as observed): the powered script 0x82A750
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
 * RE-CONFIRMED line by line (audit 2026-07-31): every one of those
 * constants is literal in src/func_0018CE60.c, and two more details
 * the port already implements correctly are worth pinning so nobody
 * "fixes" them — the floor class mask is +0x1A & 0x5000 (ceiling is
 * & 0x8800), and the no-hit fallbacks are ASYMMETRIC: the floor miss
 * carries `cam+0x50 - 200` (the PREVIOUS lower bound) while the
 * ceiling miss uses `pt.y + 200`.
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

    if (em_collision_camera_query(&g.coll, pt, probe, mask,
                                   &hit)) {
        lo = hit.point[1];
        if ((hit.surf_class & (EM_SURF_FLOOR | EM_SURF_SLOPE)) ||
            hit.normal[1] > AIMS_SETTLE_NY)
            lo += pad;
    } else {
        lo = cam->y_lo - SOLV_BOUND_RANGE;
    }
    probe[1] = pt[1] + SOLV_BOUND_RANGE;
    if (em_collision_camera_query(&g.coll, pt, probe, mask,
                                   &hit)) {
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





































/* func_001AE5E0 — THE GAMEPLAY FRAME (stage order is the engine's). */
static void gameplay_frame(void)
{
    em_game_selftest_pre_frame();   /* every EM_*_TEST script and EM_CAPTURE_*
                                     * input injection (em_game_selftest.c)
                                     * — debug instrumentation only */
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
    /* MOVING-SURFACE REGISTRY (em_collision.h §11.4): cleared ONCE at the
     * top of the sim, before any mover registers a footprint + velocity
     * for the player ground-solve's carry. No AREA-11 actor registers one
     * today: the truck is static until WP-12 (em_truck.h). */
    em_collision_moving_clear();
    /* STATIC BLOCKER REGISTRY (the AREA-11 gated GRATE's closed hull —
     * em_collision.h §blocker): cleared once at the top of the sim
     * alongside the moving-surface registry, then grate_update (below,
     * before actor_update) re-registers the closed hull while the area is
     * not powered, so player_move's ground-solve push-out
     * (em_collision_blocker_probe) sees this frame's blocker. */
    em_collision_blocker_clear();
    /* WEDGED TRUCK (AREA-11 record 16, overlay owner 00823FF0). The call
     * site stays before actor_update for the translated owner, but
     * em_truck_update changes nothing today: the original stand-on
     * trigger, fall sequence and D_00810792 persistence are not
     * translated (WP-12, em_truck.h), so the truck is static and
     * registers no moving surface. */
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
    /* Keep the original static panel transform; this model never slides. */
    grate_update();
    em_player_0015BCF0();    /* 001CB590(player) / 0015BCF0 / 001CB5A0
                              * (em_player_frame.c)                   */
    em_render_001D1C50();    /* 001D1C50 -> 001D7C30, before pooled actors */
    em_opening_runtime_tick(); /* automatic AREA11 actor in pool phase */
    em_area11_effect_runtime_tick();
    render_chain_build();    /* native draw list, at the 001D1C50 slot */
    em_render_001C1D00();    /* func_001C1D00(0x008101D0)             */
    /* func_001AFD70(0) — the actor-pool tick (world services). The
     * port's first pooled actors are the DOORS: per-frame behavior
     * (func_001BC350 state machine), the player use scan
     * (func_00184BA0) and articulation live in em_door_update.
     * CITATION CORRECTED (audit 2026-07-31): the old "func_0015C160 /
     * func_001F0360 — still untranslated" note is stale; both are now
     * BYTE-MATCHED. src/func_0015C160.c is a SEPARATE player post-step
     * that the original runs after this pool tick, not the player
     * update above — gated on
     * D_008102B1, it runs func_001CB590(self, 0x320, self[9]), then
     * (unless D_00810771 == 1) either func_001DA6A0(D_00275B44) when
     * self+0x214 == 0 or func_0015BF90(self), and finally dispatches
     * the hook pointer at self+0x4C. src/func_001F0360.c is a plain
     * subsystem-tick barrel (six subsystem calls then func_001F0720
     * for ids 0,1,3,4,5,6) with no gameplay state of its own. The port
     * runs no code at either position (em_player_0015C160 reports a
     * fault and is not called; docs/SCENE_COORDINATOR_DESIGN.md
     * sections 2.4 and 4.5, open question Q2). */
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
    em_snow_runtime_tick(g.cam.eye, 0);
    em_pickup_update(g.pos, g.yaw, em_frame_input(),
                     !em_door_movement_locked() &&
                     !player_damage_locked());
    em_props_indicators_tick();
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
     * otherwise to the flinch entry (marker 0x3E). The sub-state bytes
     * are literal too and match the port's pd_sub: the death arms write
     * +4 = 2 with +5 = 1 (ordinary) or +5 = 3 (the +0xF == 0x63 /
     * infected-latch variant), the flinch arm +4 = 2 with +5 = 0. Its
     * tail also
     * carries the low-health latch verbatim: `if (+0x220 <= 35.0f)
     * +0x235 |= 1` — the source of PD_LOW_HEALTH = 35.0f, and (audit
     * 2026-07-31) the same bit func_00161020 tests to SUPPRESS the idle
     * fidget, so this threshold has a second, visible consequence: see
     * the IDLE CYCLE block in actor_update. Note also that "never
     * touches health" is scoped to this generic tail — the reaction
     * arms above it DO seed +0x224 themselves (3.0 / 5.0 / 8.0, and
     * once +0x224 = +0x220) before calling func_0021C350. The
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
    em_camera_0018B9C0();    /* func_001CB590(0x008101E0, 0xD0, 0) +
                              * func_0018B9C0 camera state machine,
                              * then the positional-audio listeners
                              * (em_render_frame.c)                   */
    em_render_001D1EA0(1);   /* func_001CB5A0/001AAD00/001D1EA0(1) —
                              * today's close-out; em_render_001AAD00
                              * is not ported and is not called       */
}

/* 001AE6B0 keeps actors, camera and presentation running while the
 * script owns input. The player’s opening pose comes from original
 * bank0x98, so ordinary movement/weapon/menu handlers do not run. */
static void cutscene_frame(void)
{
    float previous_eye[3];
    memcpy(previous_eye, g.cam.eye, sizeof previous_eye);
    /* This path also runs world actors. Their transient collision entries
     * expire each frame, just as they do in the ordinary gameplay path. */
    em_collision_moving_clear();
    em_collision_blocker_clear();
    em_render_001D1C50();    /* 001AE6B0 calls 001D1C50 before actor pools */
    em_opening_runtime_tick();
    em_area11_effect_runtime_tick();
    grate_update();
    em_snow_runtime_tick(previous_eye, 1);
    em_pickup_update(g.pos, g.yaw, em_frame_input(), 0);
    em_props_indicators_tick();
    render_chain_build();
    em_render_001C1D00();
    if (!em_opening_runtime_camera()) camera_update();
    em_sfx_listener(g.pos,g.cam.eye,g.cam.yaw);
    em_render_001D1EA0(1);
}

/* ------------------------------------------------------------------ */
/* State machines (slot-0 task chain)                                  */
/* ------------------------------------------------------------------ */

/* func_001AE040 — in-game frame machine (state byte task+0xB). */
static void ingame_frame_machine(EmTask *self)
{
    switch (self->user[GAME_BYTE_FRAME]) {
        case 0:
            /* Area-build arm. Original anim_frame_top_b (0x001AE040,
             * NEARMISS) state 0 sets +B = 1 and calls, in order, 001AFCA0
             * (player-struct wipe, actor-pool reset, overlay install),
             * 001AFCF0, 001B07C0(0), 001B6990 (deferred group, then the
             * placement roster), 001D19E0, 001C1DC0, 00199C50,
             * 001AEE40(4), 001FAE70(1) (area music), 001C5C50 and
             * 001D1EF0, then RETURNS: no world frame that tick
             * (SCENE_COORDINATOR_DESIGN.md §2.3). Natively this arm
             * re-arms port state instead: the spawn-table stand-in is the
             * scene manifest's spawn (the office kPlayerPos default;
             * origin with no scene loaded), and the camera struct is
             * zeroed back to its init state (the one-shot setup arms it
             * behind the player on the next frame, along the spawn
             * facing). It then falls through into the world frame in the
             * same tick, which the original does not (WP-3). */
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
            g.loco_mode = g.loco_substate = 0;
            g.loco_entry_ticks = 0;
            g.loco_stop.phase = 0;
            g.loco_reentry.phase = 0;
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
             * flags (opening_complete / terminal_powered) are NOT touched
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
             * own INIT yaws it at the player — src/func_00154040.c,
             * BYTE-MATCHED, RE-CONFIRMED by audit 2026-07-31: the
             * heading write `+0xC4 = func_001B1240(self+0xB0, spec,
             * D_00810350, D_00810358)` sits in the single
             * `func_001B10B0(self, 0x14, 0x13) == 0` slot-reservation
             * arm with no distance or line-of-sight test anywhere, so
             * acquisition really is unconditional); tests 1/3 place a
             * DISGUISED CRATE 12 /
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
            em_opening_runtime_scene_ready();
            self->user[GAME_BYTE_FRAME] = 1;
            /* Native init currently also presents the first world frame. */
        case 1:
            /* GAME-OVER OPTION 0 RESTART. src/func_001AC070.c (NEARMISS,
             * logic authoritative) state 2 confirm with cursor 0 goes to
             * state 4 with D_00275BE0 = 0 and reinstalls func_001ACEC0 —
             * the title's NEW GAME route. The from-death flag
             * D_00275BDC (001AC070 state 0 / 001AC480 state 0) skips the
             * load wait and sets the initial cursor to 1; it does not
             * change the cursor-0 dispatch:
             *   001ACEC0 state 1 -> 001AD230 -> 001AF2C0 (new-game reset)
             *   001AD250 sub 0 -> 001AD360 step 4: area 0x0B/0/0
             *   001AD250 sub 5 -> 001ADF50 area build, then gameplay.
             * So the restart is AREA11 sub 0 entry 0 with the new-game
             * state wherever the player died. The memset clears event
             * byte D_00810791 (D_00810758[0x39]), which is what the
             * opening controller 00823E80 tests (001BA1C0(.., 0x39) in
             * its state 1) to skip its script; it does not read
             * D_00810811. That the rebuilt area therefore replays the
             * opening is inferred from those instructions, not measured:
             * no original Continue has been captured. 001AD360 step 0 calls
             * 001FABB0 (stream stop): mirrored by em_bgm_stop(0) below.
             * Only the fields game_state_new_game mirrors (plus pd_* /
             * go_* and the pickup table) are reset here; New Game's
             * em_game_install memsets all of g, so other port state
             * (e.g. director state other than cine_step) survives a
             * Continue. Not mirrored yet: 001AD360 steps 0-2 also call
             * 001D1EF0, and step 1 re-requests movie selector 0
             * (E900.PSS), which the native frontend owns. */
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
                g.pd_infected = 0;
                g.pd_low      = 0;
                em_bgm_stop(0);            /* 001AD360 step 0: 001FABB0 */
                em_pickup_reset();         /* 001AF2C0 D_00810700 memset */
                game_state_new_game(&g);   /* 001AF2C0 mirrored fields   */
                g.et_spawned = 0;     /* self-tests may re-spawn */
                /* The area build recreates the player actor: return the
                 * pose source to its pre-opening load state, as the New
                 * Game load does (game_load_task). */
                player_pose_unload();
                if (g.mesh) (void)player_pose_load(PLAYER_CHANNELS_PATH);
                em_opening_runtime_request();
                if (em_game_scene_switch(AREA11_SCENE_DIR) != 0) {
                    fprintf(stderr, "continue: original restart area "
                            "(AREA11, %s) unavailable\n", AREA11_SCENE_DIR);
                    em_frame_request_quit();
                    break;
                }
                em_frame_fade_start(-1, EM_FADE_SPEED_DOOR);
                self->user[GAME_BYTE_FRAME] = 0;
                ingame_frame_machine(self);   /* re-init this frame */
                break;
            }
            /* Live arm = original state 1: r = 001AE7E0 (r 1/2/3 move to
             * states 2/3/6, not translated here); otherwise, only while
             * D_00275BD8 (load busy) is 0, the variant chosen by
             * scratchpad 0x70003B8D runs (001AE5E0 when 0, else
             * 001AE6B0), followed by the B9/B8 door-request checks. The
             * 001AFCF0 / 001B07C0(1) / 001C1DC0 calls belong to state 4
             * (the resume arm), not to every frame
             * (SCENE_COORDINATOR_DESIGN.md §2.3). */
            em_opening_control_test_before_frame();
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

/* Native gameplay asset loader, used after the frontend/new-game flow
 * or explicitly by EM_SKIP_STARTUP. It is not the PS2 boot task. */
static void game_load_task(void)
{
    EmGfx *gfx = em_frame_gfx();

    /* Optional character asset (disc-derived, generated locally). */
    if (em_model_load(&g.model, MODEL_PATH) == 0) {
        (void)player_pose_load(PLAYER_CHANNELS_PATH);
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
     * collision filename / doors — office defaults when absent). */
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

    /* No music starts from scene data. In the original, area music is
     * chosen by 001FAE70 from D_008106C8 bits 8..15 (AREA11 captures:
     * 0x20081910 -> cue 25); anim_frame_top_b state 0 calls
     * 001FAE70(1) at area entry (0x001AE0C4), and on New Game the AREA11
     * opening controller 00823E80 stops streams (001FABB0) when its script
     * starts and resumes cue 25 via 001FAE70(0) when it ends
     * (EM_OPENING_RESUME_MUSIC). The area-entry 001FAE70(1) call is not
     * mirrored yet (it also draws one rand(); whole-game RNG order is
     * unaudited). EM_BGM=<path.wav> remains a debug-only listening
     * override with no original counterpart. */
    if (g.bgm_path)
        em_bgm_play(g.bgm_path, 1);

    em_task_replace_current(game_task);
}

void em_game_install(void)
{
    player_pose_unload();
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
    /* The progress bytes in that same memset (D_00810791, D_00810811
     * opening complete, D_00810841[11] terminal unlock) are already 0
     * from the memset of g above; they PERSIST across an intra-area
     * room-move scene reload and are cleared again only by the
     * 001AF2C0 routes (em_game_install_new, game-over option 0). */
    em_task_register(0, game_load_task);
}

void em_game_install_new(void)
{
    em_game_install();
    /* func_001AF2C0 clears the inventory/area block and establishes these
     * health, magazine, reserve and battery values (game_state_new_game;
     * em_game_install already ran em_pickup_reset). The frontend's replay
     * of movie selector 0 precedes func_001AD360's area 11.0 commit. */
    game_state_new_game(&g);
    snprintf(g.scene_dir, sizeof g.scene_dir, "%s", AREA11_SCENE_DIR);
    em_opening_runtime_request();
    em_opening_control_test_begin();
}

void em_game_shutdown(void)
{
    player_pose_unload();
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
    grate_unload(gfx);          /* AREA-11 power-panel mesh */
    em_truck_clear(gfx);        /* AREA-11 wedged-truck actor + mesh */
    em_door_shutdown(gfx);
    em_enemy_shutdown(gfx);
    em_pickup_scene_clear(gfx);
    em_snow_runtime_clear(gfx);
    em_area11_effect_runtime_clear(gfx);
    em_examine_reset();
    em_collision_free(&g.coll);
    em_bgm_shutdown();  /* blocks out the audio thread, then frees + prints */
    em_opening_runtime_shutdown(); /* PCM release after device teardown */
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
