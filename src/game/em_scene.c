/* em_scene.c — scene manifest parse, load and unload.
 *
 * The scene.txt manifest reader and the scene lifetime: parse the manifest,
 * load the parts it names, and tear everything down on a switch. Split out of
 * em_game.c, which had grown to hold the entire gameplay frame. Behaviour is
 * unchanged by the move — only the file boundary is new.
 *
 * Every function here reads the shared gameplay state (EmGameState g), so
 * this module takes the subsystem's internal header rather than owning
 * private state — the same single state block the engine keeps in its
 * gameplay globals, now viewed from one more file. */

#include "game/em_scene.h"

#include "game/em_game_internal.h"
/* the scene loader installs and tears down the placed set pieces */
#include "game/em_props.h"

/* manifest_word_token — does `line` contain `tok` as a WHOLE WORD
 * (delimited by start-of-line/space on the left and end-of-line/space on
 * the right)? Used for the trailing examine-line marker tokens
 * ("terminal" / "battery_terminal") so a substring inside a path or
 * another token can't trip them, and so the longer "battery_terminal"
 * (which ends in "terminal") is not mis-matched by the "terminal" probe
 * (the '_' before "terminal" fails the left delimiter). */
static int manifest_word_token(const char *line, const char *tok)
{
    size_t tl = strlen(tok);
    for (const char *p = strstr(line, tok); p; p = strstr(p + 1, tok)) {
        char l = (p == line) ? ' ' : p[-1];
        char r = p[tl];
        int left_ok  = (l == ' ' || l == '\t');
        int right_ok = (r == '\0' || r == ' ' || r == '\t' ||
                        r == '\n' || r == '\r');
        if (left_ok && right_ok) return 1;
    }
    return 0;
}

void scene_manifest_load(void)
{
    g.spawn[0]  = kPlayerPos[0];
    g.spawn[1]  = kPlayerPos[1];
    g.spawn[2]  = kPlayerPos[2];
    g.spawn_yaw = 0.0f;
    snprintf(g.coll_path, sizeof g.coll_path, "%s/%s", g.scene_dir,
             COLL_DEFAULT);
    g.bgm_file[0]  = '\0';
    g.cam_dist_param = -46.8f;  /* cam+0x0C/+0x64 default; `camdist` line */
    g.n_camregion  = 0;     /* camera regions are per-scene data */
    g.rig_on       = 0;     /* LIGHTING: rig + lamps are per-scene data */
    g.n_lamp       = 0;
    g.steam_on     = 0;     /* AREA-11 steam/FX emitter is per-scene data */
    g.steam_lamp   = -1;
    g.area_title_armed = 0; /* AREA-title card re-arms per scene (`areatitle`) */
    g.opencam_on   = 0;     /* the opening-camera seat is per-scene data too:
                             * without this reset a scene with no `opencam`
                             * line inherits the previous scene's seat and
                             * replays its opening camera. (Hygiene fix, by
                             * symmetry with every other per-scene field
                             * above — not a decomp-derived correction.) */
    g.fog_on       = 0;     /* LIGHTING: distance fog is per-scene data */
    int n_ldir = 0, have_amb = 0, have_cam = 0;

    char mf[256 + 16];
    snprintf(mf, sizeof mf, "%s/scene.txt", g.scene_dir);
    FILE *f = fopen(mf, "r");
    if (!f) return;

    char line[512], name[256], gname[64];
    float x, y, z, yaw, r, gx, gy, gz, gyaw;
    int gn, gk, gl;
    int last_examine = -1;   /* examinetext chains onto this slot */
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (sscanf(line, "spawn %f %f %f %f", &x, &y, &z, &yaw) == 4) {
            g.spawn[0]  = x;
            g.spawn[1]  = y;
            g.spawn[2]  = z;
            g.spawn_yaw = yaw;
        } else if (sscanf(line, "collision %255s", name) == 1) {
            snprintf(g.coll_path, sizeof g.coll_path, "%s/%s", g.scene_dir,
                     name);
        } else if (sscanf(line, "bgm %255s", name) == 1) {
            snprintf(g.bgm_file, sizeof g.bgm_file, "%s", name);
        } else if (sscanf(line, "camdist %f", &x) == 1) {
            /* The engine's camera-distance param (cam+0x0C, signed —
             * spawn records carry it at +0x18; live: -46.8 default,
             * -31.2 in the office records, s66). Drives the s67 walk
             * tether follow distance, its slack rule (the engine keys
             * 20-vs-10 on cam+0x64 == -46.8; the port folds record and
             * area param into this one knob) and the door re-seat
             * distance. The exporter does not emit it yet — absent
             * line = the -46.8 default. */
            g.cam_dist_param = x;
        } else if (sscanf(line, "opencam %f %f %f %f %f %f %f",
                          &gx, &gy, &gz, &gyaw, &x, &y, &z) == 7) {
            /* OPENING-CAMERA SEAT (s79 it4 — the EmGameState `opencam`
             * doc): ex ey0 ey1 ez  tx ty tz. The settled eye x/z =
             * (gx, gz), the eye.y rises ey0 (gy) -> ey1 (gyaw), the
             * pinned target = (x, y, z). This is the engine's emergent
             * AREA-11 opening result that the byte-faithful wall solver
             * cannot reach from the buried seat (collision-geometry-
             * driven; see the struct doc). */
            g.opencam_on     = 1;
            g.opencam_eye[0] = gx;      /* ex                          */
            g.opencam_ey0    = gy;      /* ey0 (frame-0 seat height)   */
            g.opencam_eye[1] = gz;      /* ey1 (settled rise target)   */
            g.opencam_eye[2] = gyaw;    /* ez                          */
            g.opencam_tgt[0] = x;
            g.opencam_tgt[1] = y;
            g.opencam_tgt[2] = z;
        } else if (sscanf(line, "camregion %f %f %f %f %f %f %f %f",
                          &x, &z, &y, &yaw, &r, &gx, &gy, &gz) == 8) {
            /* Fixed-camera trigger volume (x0 z0 x1 z1 recy ex ey ez) —
             * the mode-0 director room cameras. CONFIRMED (audit
             * 2026-07-31, re-read line-by-line against the recovered C):
             *   - src/func_00195130.c (NEARMISS, the mode-0 area-camera
             *     director) switches on the area byte D_00810700 and, in a
             *     case that fires, writes the desired eye cam+0x10/14/18
             *     from float immediates. Area 6 (snow), verbatim:
             *       `else if (func_00194D10(arg0, arg1, 2) != 0) {
             *          func_0018D7B0(arg0, 5); *(f*)(arg0+0x14) = 90.0f;
             *          if (func_0018C4B0(&D_008105D0, *(f*)(arg0+0x14),
             *                            0.7f) != 0) {
             *            *(f*)(arg0+0x10) = -367.7f;
             *            *(f*)(arg0+0x18) = -598.4f;
             *            func_0018C6A0(arg0+0x10, &D_008105D0, 0.7f); } }`
             *     i.e. eye (-367.7, 90.0, -598.4) approached at 0.7/frame.
             *     Those immediates are what a `camregion` line carries.
             *   - src/func_00194D10.c (NEARMISS) is the region test:
             *       `if (func_001B1EA0(0, a1+0xA0, D_0024A5F0+idx*0x40, 4))
             *          return fabs(*(f*)(a1+0xA4)
             *                      - *(f*)(D_0024A5F4+idx*0x40)) < 4.0f;`
             *     So: a 4-corner containment probe on the player position
             *     against the stride-0x40 record, AND a strict
             *     |player.y - rec.y| < 4.0f elevation gate. The 4.0f is
             *     code; the "point-in-polygon" reading of the probe is an
             *     inference from its `4` argument — func_001B1EA0 itself
             *     is still hand-written asm, so its shape is NOT decoded.
             *     The port mirrors the elevation gate as CAM_REGION_YGATE
             *     (4.0f, em_game_internal.h; em_camera.c applies it).
             * The three shipped records are axis-aligned rects, so the
             * port stores a rect. Reusing the scratch floats:
             * x=x0 z=z0 y=x1 yaw=z1 r=rec.y (the gate CENTRE, not a
             * half-width — the +-4.0 comes from CAM_REGION_YGATE) g*=eye. */
            if (g.n_camregion < CAM_REGION_MAX) {
                EmCamRegion *cr = &g.camregion[g.n_camregion++];
                cr->x0 = x < y ? x : y;
                cr->x1 = x < y ? y : x;
                cr->z0 = z < yaw ? z : yaw;
                cr->z1 = z < yaw ? yaw : z;
                cr->ygate  = r;
                cr->eye[0] = gx;
                cr->eye[1] = gy;
                cr->eye[2] = gz;
            } else {
                printf("manifest: camregion limit (%d) hit, skipped: %s",
                       CAM_REGION_MAX, line);
            }
        } else if (sscanf(line, "fog %f %f %f %f %f",
                          &x, &y, &gx, &gy, &gz) == 5) {
            /* LIGHTING — the scene's DISTANCE FOG (D_00251C50 record
             * rec+4/+8 = near/far, rec+0xC/10/14 = RGB). x=near, y=far,
             * gx/gy/gz = fog RGB on the engine 0..128 scale (the rig
             * color convention; the gfx layer scales /128). near may be
             * negative (AREA-11 -208 => even near geometry is partly
             * fogged). Absent line => fog_on stays 0 (scene unfogged). */
            g.fog_near   = x;
            g.fog_far    = y;
            g.fog_rgb[0] = gx; g.fog_rgb[1] = gy; g.fog_rgb[2] = gz;
            g.fog_on     = 1;
        } else if (sscanf(line, "lightamb %f %f %f", &x, &y, &z) == 3) {
            /* LIGHTING — the scene rig's ambient row (engine 0..128
             * scale; D_00251C50 record +0x68). */
            g.rig_amb[0] = x; g.rig_amb[1] = y; g.rig_amb[2] = z;
            have_amb = 1;
        } else if (sscanf(line, "lightcam %f %f %f %f %f %f %f",
                          &x, &y, &z, &gx, &gy, &gz, &gyaw) == 7) {
            /* LIGHTING — rig slot 0, the CAMERA FILL: dir is
             * CAMERA-SPACE (rotated into world per frame by
             * char_rig_build), w = the lamp-fold dir weight. */
            g.rig_cam_dir[0] = x;  g.rig_cam_dir[1] = y;
            g.rig_cam_dir[2] = z;
            g.rig_cam_col[0] = gx; g.rig_cam_col[1] = gy;
            g.rig_cam_col[2] = gz;
            g.rig_cam_w      = gyaw;
            have_cam = 1;
        } else if (sscanf(line, "lightdir %f %f %f %f %f %f",
                          &x, &y, &z, &gx, &gy, &gz) == 6) {
            /* LIGHTING — rig slots 1/2, the static world-space
             * directionals (in manifest order). */
            if (n_ldir < 2) {
                g.rig_dir[n_ldir][0] = x;  g.rig_dir[n_ldir][1] = y;
                g.rig_dir[n_ldir][2] = z;
                g.rig_col[n_ldir][0] = gx; g.rig_col[n_ldir][1] = gy;
                g.rig_col[n_ldir][2] = gz;
                n_ldir++;
            }
        } else if (sscanf(line, "lamp %f %f %f %f %f %f %f",
                          &x, &y, &z, &gx, &gy, &gz, &gyaw) == 7) {
            /* LIGHTING — one placed lamp (the room's func_001F6760
             * list; color/intensity carry the engine's x128
             * registration scale). */
            if (g.n_lamp < LAMP_MAX) {
                g.lamp[g.n_lamp].pos[0] = x;
                g.lamp[g.n_lamp].pos[1] = y;
                g.lamp[g.n_lamp].pos[2] = z;
                g.lamp[g.n_lamp].col[0] = gx;
                g.lamp[g.n_lamp].col[1] = gy;
                g.lamp[g.n_lamp].col[2] = gz;
                g.lamp[g.n_lamp].inten  = gyaw;
                g.n_lamp++;
            } else {
                printf("manifest: lamp limit (%d) hit, skipped: %s",
                       LAMP_MAX, line);
            }
        } else if (sscanf(line, "door %255s %f %f %f %f %f", name,
                          &x, &y, &z, &yaw, &r) == 6) {
            /* Interactive door instance (em_door.c). The manifest is
             * parsed inside the boot task, so the gfx device exists.
             * Grammar (em_door.h):
             *   door <file> x y z yaw r [locked] [goto <dir> s...]
             * The optional `locked` token (export_level.py
             * --door-locked) is spliced out of a working copy so the goto
             * sscanf below keeps its fixed shape.
             *
             * The lock gate is CONFIRMED (audit 2026-07-31, re-read in
             * src/func_001BC350.c — NEARMISS 99.53%, so its LOGIC is
             * authoritative): sub-state 0 tests, for model byte
             * self[3] == 0x15 ONLY,
             *   D_00810841[D_00810700] & (1 << *(short *)(self + 0x34))
             * where +0x34 is the door id. Bit SET -> func_001BBE40(...,0)
             * (opens, sub 3); bit CLEAR -> func_001BBE40(...,1) (the
             * locked sequence, sub 1). Any OTHER model byte skips the
             * gate and always takes mode 0. D_00810841 is BSS, so every
             * lock-gated door starts LOCKED — which is why the manifest
             * carries `locked` as an explicit per-door token. The slider
             * brain src/func_001BB860.c runs the same gate for model
             * bytes 0x16 / 0x17 / 0x3E. */
            float p[3] = { x, y, z };
            char  dline[512];
            int   locked = 0;
            snprintf(dline, sizeof dline, "%s", line);
            {
                char *lt = strstr(dline, " locked");
                if (lt && (lt[7] == ' ' || lt[7] == '\0' ||
                           lt[7] == '\n' || lt[7] == '\r')) {
                    locked = 1;
                    memmove(lt, lt + 7, strlen(lt + 7) + 1);
                }
            }
            if (em_door_add(em_frame_gfx(), g.scene_dir, name, p, yaw, r)) {
                printf("manifest: door line failed to load: %s", line);
            } else if (((locked
                             ? (void)em_door_set_locked(em_door_count() - 1)
                             : (void)0),
                        sscanf(dline,
                              "door %*s %*f %*f %*f %*f %*f goto "
                              "%63s %f %f %f %f",
                              gname, &gx, &gy, &gz, &gyaw) == 5)) {
                /* Destination tail (em_door.h): the commit scene-switches
                 * instead of re-placing. CONFIRMED against the
                 * BYTE-MATCHED src/func_001BC150.c (audit 2026-07-31) —
                 * the transition commit, transcribed:
                 *   rec = D_0024E140[D_00810700] + 4*(self[0x34] & 0x7F)
                 *         (BYTE pointer; the *4 is the record stride)
                 *   *(short*)(self+0x34) & 0x80 set -> inter-AREA change
                 *      (func_001B0C00(4); request B8=1,
                 *       B5=rec[0] next area, B7=rec[1] entry,
                 *       B6 = rec[2] ? rec[3] : 0xFF sub-state)
                 *   bit clear -> same-area room move (func_001AEDE0(4,0);
                 *       B8=2, B7=rec[*(u16*)(self+0x2E)] — the SIDE LATCH
                 *       indexes the record, it is not an addend)
                 * func_001B0C00 (also byte-matched) is exactly
                 * `func_001AEDE0(p,0); func_001FAD70(0..2, p, 1);` — the
                 * same fade arm as the room move PLUS three audio-channel
                 * fades, which is why room moves genuinely do not fade
                 * audio. So bit 7 of the door id IS the "leads to another
                 * area" flag, and the port's `goto` tail is the
                 * exporter's rendering of the bit-7 case. EM_DOOR_TEST
                 * asserts the same-scene re-place geometry on this
                 * very door, so it runs with goto tails ignored —
                 * EM_PAUSE_TEST's door leg (the mid-walk-out menu
                 * check) does the same. */
                if (g.door_test || g.pause_test) {
                    printf("manifest: %s — goto tail ignored "
                           "(same-scene transit asserted)\n",
                           g.door_test ? "EM_DOOR_TEST" : "EM_PAUSE_TEST");
                } else {
                    float gp[3] = { gx, gy, gz };
                    em_door_set_goto(em_door_count() - 1, gname, gp,
                                     gyaw);
                }
            }
        } else if ((gn = sscanf(line, "elevator %255s %f %f %f %f",
                                 name, &x, &y, &z, &yaw)) >= 4) {
            /* AREA-11 ELEVATOR PLATFORM placement (batch-2 contract F).
             * Form: `elevator <model.emdl> <x> <y> <z> [yaw]`. The mesh
             * is the descending platform (ov 0x00828050 +0xB4); the ride
             * (em_game_elevator_start / elevator_tick) drives this Y down
             * with the player + camera. The placement spot is "right next
             * to the battery terminal (224, 230, 250.7)"
             * (INVESTIGATION_area11_elevator.md §4). Only one platform per
             * scene; a second line replaces the first. The ride works
             * WITHOUT this line (player + camera still descend — the
             * platform just isn't drawn), so a missing/failed mesh is
             * non-fatal. */
            EmGfx *egfx = em_frame_gfx();
            elevator_unload(egfx);          /* drop any prior platform */
            g.elev_pos[0] = x;
            g.elev_pos[1] = y;
            g.elev_pos[2] = z;
            g.elev_yaw    = (gn >= 5) ? yaw : 0.0f;
            char epath[1024];
            snprintf(epath, sizeof epath, "%s/%s", g.scene_dir, name);
            if (em_model_load(&g.elev_model, epath) != 0) {
                printf("manifest: elevator mesh %s failed to load — ride "
                       "runs WITHOUT a platform (player+camera descend), "
                       "FLAGGED\n", epath);
            } else {
                g.elev_mesh = em_gfx_mesh_create(
                    egfx, g.elev_model.verts, g.elev_model.vert_count,
                    g.elev_model.indices, g.elev_model.index_count,
                    (const EmGfxTexDesc *)g.elev_model.texs,
                    g.elev_model.tex_count, g.elev_model.texels,
                    g.elev_model.flags);
                g.elev_palette = malloc(g.elev_model.bone_count * 16 *
                                        sizeof(float));
                if (!g.elev_mesh || !g.elev_palette) {
                    printf("manifest: elevator mesh %s GPU/palette alloc "
                           "failed — ride runs WITHOUT a platform, "
                           "FLAGGED\n", epath);
                    elevator_unload(egfx);
                } else {
                    g.elev_has_mesh = 1;
                    elevator_pose();        /* bake the placed frame-0 pose */
                    printf("manifest: ELEVATOR platform %s at (%.1f, "
                           "%.1f, %.1f) yaw %.3f — %u verts\n", name,
                           x, y, z, g.elev_yaw, g.elev_model.vert_count);
                }
            }
        } else if (sscanf(line, "truck %255s %f %f %f %f %f",
                          name, &x, &y, &z, &yaw, &r) == 6) {
            /* AREA-11 WEDGED TRUCK (placement record 16, behavior
             * ov 0x00823FF0 + trigger record 17 ov 0x008251E0 — the
             * "run across the top before it falls" set-piece;
             * INVESTIGATION_first_level_area11.md §11). Form:
             * `truck <model.emdl> <x> <y> <z> <rx> <ry>` (the placed pos +
             * Euler tilt rx and yaw ry). The actor (em_truck.c) loads the
             * mesh, seats the wedged pose, runs the trigger->fall machine,
             * and registers its walkable-top footprint+velocity each frame
             * so a player standing on top during the fall is carried DOWN
             * into the crevice. One truck per scene; the ride/carry is
             * owned by em_truck.c + the moving-surface registry. A missing
             * mesh makes the whole set-piece absent (em_truck_install
             * returns nonzero and reports). */
            char tpath[1024];
            snprintf(tpath, sizeof tpath, "%s/%s", g.scene_dir, name);
            float tp[3] = { x, y, z };
            if (em_truck_install(em_frame_gfx(), tpath, tp, yaw, r) == 0)
                printf("manifest: WEDGED TRUCK %s at (%.1f, %.1f, %.1f) "
                       "rx %.3f ry %.3f\n", name, x, y, z, yaw, r);
            else
                printf("manifest: truck line failed to load: %s", line);
        } else if ((gn = sscanf(line, "grate %255s %f %f %f %f",
                                 name, &x, &y, &z, &yaw)) >= 4) {
            /* AREA-11 GATED GRATE (placement record 18, fn func_00159210 —
             * the closed path-blocker that opens when the area is powered;
             * INVESTIGATION_area11_grate.md). Form:
             * `grate <model.emdl> <x> <y> <z> [yaw]`. The bars mesh (per-
             * area id 0x04 = props/area_item_04.emdl) is rendered AND, while
             * the area is NOT powered (em_game_terminal_powered, the unlock
             * bit D_0081084C & 0x80 — 0 at a fresh new game), registers a
             * SOLID blocker hull each frame so the player cannot pass. Once
             * powered the hull is dropped and the bars SLIDE open. The hull
             * is the mesh's own world AABB, derived at install from the
             * loaded verts + the placement transform. One grate per scene; a
             * second line replaces the first. A missing mesh disables the
             * grate entirely (no blocker, no draw — matching the engine
             * never instantiating record 18 without its model). This is the
             * GATED representation of the bars; the OTHER param-0x04 prop
             * (record 20, the unrelated static decor) stays a plain
             * `pickup ... prop`. */
            float gp[3] = { x, y, z };
            if (grate_install(em_frame_gfx(), g.scene_dir, name, gp,
                              (gn >= 5) ? yaw : 0.0f) == 0)
                printf("manifest: GATED GRATE %s at (%.1f, %.1f, %.1f) "
                       "yaw %.3f — closed blocker until powered\n",
                       name, x, y, z, (gn >= 5) ? yaw : 0.0f);
            else
                printf("manifest: grate line failed to load: %s", line);
        } else if (sscanf(line, "areatitle %d", &gk) == 1) {
            /* AREA-TITLE CARD trigger. Form: `areatitle <area>`. Arms the
             * one-shot opening placard ("FORT STEWART - REAR ENTRANCE" for
             * area 11) ONCE on scene entry; em_hud owns the card itself.
             * Independent of the cinematic director and the status HUD:
             * src/func_001C5930.c (NEARMISS) reads neither D_008101E4 nor
             * the message machine — it is its own HUD-overlay state
             * machine on the normal gameplay frame.
             *
             * AUDITED 2026-07-31 against src/func_001C5930.c. Three parts
             * of the old comment were WRONG and are corrected here:
             *
             *  - "fade-in/hold/fade-out" — there is NO fade. Case 0 arms a
             *    300-frame counter (*(short *)(arg0+0x28) = 0x12C) and the
             *    per-frame draw is the same call every frame,
             *    func_001CC1E0(1, 0x800 - (w>>1), 0x7A2, 0xA, 0x14, str, 0)
             *    — no alpha term anywhere in the function. It is a
             *    constant-opacity 300-frame hold. em_hud.c was corrected to
             *    match; do not re-add an envelope here without evidence.
             *
             *  - "the string table (0x00273B80)" — func_001C5930 does not
             *    read a 32-byte-stride table at that address. Its case 0
             *    computes, verbatim,
             *      `*(short*)(arg0+0x2A) = D_00289B40[D_00810700][0];
             *       *(short*)(arg0+0x2A) += D_00810701;`
             *    and its case 1 draws `D_002671C0[*(short*)(arg0+0x2A)]`
             *    — a POINTER array indexed by (per-area base + sub-area
             *    byte). RE-CONFIRMED 2026-07-31. The port's area-11 line
             *    is an OBSERVED capture, not a decoded table read, and the
             *    port keys it on the area alone — the engine's sub-area
             *    term (D_00810701) is NOT modelled.
             *
             *  - "first-gameplay-frame trigger" — more precisely, the arm
             *    is func_001C5930's case 0 (the overlay state byte
             *    arg0+4 == 0), which then advances to the case-1 display
             *    machine. Arming at manifest parse binds the port's card to
             *    scene-LOAD, which is the closest port analogue.
             *
             * Only area 11 has an observed string; any other area no-ops in
             * em_hud_area_title. NOT MODELLED (present in func_001C5930):
             * after the 300 frames the engine runs a SECOND 300-frame line,
             * the sub-location name D_0026726C[func_001C5860()], drawn at
             * x 0x896 - (w>>1) on the same row.
             *
             * NOTE: g.area_title_armed currently has no reader anywhere in
             * the port — it is bookkeeping only, not a behaviour gate. */
            g.area_title_armed = 1;
            em_hud_area_title(gk);
        } else if (sscanf(line, "steam %f %f %f", &x, &y, &z) == 3) {
            /* AREA-11 STEAM / FX EMITTER (placement record 7, ov 0x008235F0
             * @ ~452,279,278 — INVESTIGATION_first_level_area11.md §3/§6).
             * Form: `steam <x> <y> <z>`. Registers (1) the level's one
             * dynamic POINT LIGHT (a subtle warm glow — pushed into the
             * placed-lamp list, folded into actor lighting by
             * char_rig_build exactly like a `lamp`), (2) the looping
             * ambient hiss 0x413 (steam_tick retriggers it on a fixed
             * interval — the port SFX path is one-shot, no loop primitive),
             * and (3) a minimal billboard steam puff (em_gfx_beam_dot). One
             * emitter per scene; a second line replaces the first. The glow
             * is deliberately faint (STEAM_LAMP_INTEN) and far from the
             * spawn so it does NOT wash out the dark snow scene. */
            g.steam_on     = 1;
            g.steam_pos[0] = x;
            g.steam_pos[1] = y;
            g.steam_pos[2] = z;
            g.steam_snd_t  = 0;       /* fire the hiss on the first tick */
            g.steam_fx_phase = 0.0f;
            g.steam_lamp   = -1;
            if (g.n_lamp < LAMP_MAX) {
                /* the cosmetic warm point light (colors on the engine
                 * 0..128 lamp scale, like the rig lightdir rows). */
                g.steam_lamp = g.n_lamp;
                g.lamp[g.n_lamp].pos[0] = x;
                g.lamp[g.n_lamp].pos[1] = y;
                g.lamp[g.n_lamp].pos[2] = z;
                g.lamp[g.n_lamp].col[0] = STEAM_LAMP_COL_R * 128.0f;
                g.lamp[g.n_lamp].col[1] = STEAM_LAMP_COL_G * 128.0f;
                g.lamp[g.n_lamp].col[2] = STEAM_LAMP_COL_B * 128.0f;
                g.lamp[g.n_lamp].inten  = STEAM_LAMP_INTEN;
                g.n_lamp++;
                printf("manifest: STEAM/FX EMITTER at (%.1f, %.1f, %.1f) — "
                       "warm point light + looping hiss %#x + puff FX\n",
                       x, y, z, STEAM_SND_ID);
            } else {
                printf("manifest: STEAM/FX EMITTER at (%.1f, %.1f, %.1f) — "
                       "lamp budget full, glow skipped; hiss %#x + puff "
                       "still active\n", x, y, z, STEAM_SND_ID);
            }
        } else if ((gn = sscanf(line, "pickup %i %f %f %f %f %i "
                                "%255s %63s",
                                &gk, &x, &y, &z, &yaw, &gl,
                                name, gname)) >= 6) {
            /* Placed pickup (em_pickup.c — collectible item or
             * kind-0xB display prop; the line grammar is in the
             * manifest doc above). gk = item type, gl = taken-bit
             * uid; the optional 7th/8th tokens are the model file
             * and/or the `prop` marker. */
            const char *model = NULL;
            int prop = 0;
            /* The trailing "battery" token (batch-2 contract B) marks the
             * AREA-11 battery key-item (placement record 10, item type
             * 0x11). It is NOT a prop — it stays a normal collectible;
             * em_pickup.c routes the TAKE of type 0x11 to
             * em_game_set_battery(1) (the engine's D_00810811 = 0xFF), so
             * the parser just needs to keep it collectible and not treat
             * "battery" as a model filename. Recognized here only for the
             * log; the type 0x11 in `gk` is what carries the routing. */
            int battery = (gn >= 7 && strcmp(name,  "battery") == 0) ||
                          (gn >= 8 && strcmp(gname, "battery") == 0);
            if (gn >= 7) {
                if (strcmp(name, "prop") == 0)         prop  = 1;
                else if (strcmp(name, "battery") != 0) model = name;
            }
            if (gn >= 8 && strcmp(gname, "prop") == 0) prop = 1;
            float p[3] = { x, y, z };
            int rc = em_pickup_add(em_frame_gfx(), g.scene_dir, gk, p,
                                   yaw, gl, model, prop);
            if (rc == -1)
                printf("manifest: pickup line failed to load: %s", line);
            else if (battery)
                printf("manifest: BATTERY key-item (type %#x) at "
                       "(%.1f, %.1f, %.1f) — take sets D_00810811\n",
                       gk, p[0], p[1], p[2]);
            /* rc == -2: taken uid — the engine's silent cond-1 skip */
        } else if (sscanf(line, "examine %f %f %f %f %f %f",
                          &x, &y, &z, &yaw, &gx, &gy) == 6) {
            /* EXAMINE object (em_examine.c — grammar in the manifest
             * doc above): optional tokens parsed by keyword so the
             * exporter can omit any of them. */
            float       p[3] = { x, y, z };
            float       cam[3];
            const float *camp = NULL;
            int         exline = -1, exdelay = 0, excool = 0;
            const char *t;
            if ((t = strstr(line, "gline ")))
                exline = (int)strtol(t + 6, NULL, 0);
            if ((t = strstr(line, "delay ")))
                exdelay = (int)strtol(t + 6, NULL, 0);
            if ((t = strstr(line, "cooldown ")))
                excool = (int)strtol(t + 9, NULL, 0);
            if ((t = strstr(line, "cam ")) &&
                sscanf(t + 4, "%f %f %f",
                       &cam[0], &cam[1], &cam[2]) == 3)
                camp = cam;
            /* op04 FACE pre-roll (INVESTIGATION_examine_walk_face.md): the
             * optional trailing `face <yaw> [walk N]` token — the scripted
             * heading the player pivots to before the message (+ the op01
             * walk-to duration, default 0 = no walk). Routed to
             * em_examine_set_face after the add. */
            int   exhasface = 0, exwalk = 0;
            float exface = 0.0f;
            if ((t = strstr(line, "face "))) {
                exhasface = 1;
                exface = strtof(t + 5, NULL);
                const char *w = strstr(t, "walk ");
                if (w) exwalk = (int)strtol(w + 5, NULL, 0);
            }
            last_examine = em_examine_add(p, yaw, gx, gy, exline,
                                          exdelay, excool, camp);
            if (last_examine < 0)
                printf("manifest: examine line failed to load: %s",
                       line);
            /* FACE pre-roll is INDEPENDENT of the terminal markers — a
             * single line carries both (the snow internal terminal has
             * `terminal` AND `face -1.3037`), so set it here, not in the
             * terminal-marker if/else chain below. */
            if (last_examine >= 0 && exhasface) {
                em_examine_set_face(last_examine, exface, exwalk);
                printf("manifest: examine slot %d FACE pre-roll yaw %.4f "
                       "rad (walk %d)\n", last_examine, exface, exwalk);
            }
            /* The trailing examine-line marker tokens (CORRECTED two-
             * terminal flow, INVESTIGATION_area11_elevator.md). Matched as
             * WHOLE WORDS (preceded by start/space, followed by end/space)
             * so a stray substring in a path can't trip them — and so the
             * "battery_terminal" token (which ends in "terminal") is NOT
             * mis-read as the plain "terminal" token (its '_' fails the
             * preceding-char test). Check the more-specific
             * "battery_terminal" first.
             *
             *  - "battery_terminal" -> the AREA-11 OUTSIDE battery terminal
             *    (ov 0x008237E0 @ 331.7,290,192.5): the battery-insert
             *    object. em_examine_set_battery_terminal routes the USE
             *    through the insert path (player clip 0x14 + lock, then
             *    set power) when the battery is held; CHAIN2 owns that.
             *  - "terminal" -> the AREA-11 INTERNAL elevator-control
             *    terminal (ov 0x00827B10 @ 224,230,250.7) on the platform.
             *    em_examine_set_terminal routes the USE through the
             *    power-GATED ride: powered -> lever clip 0x47 + lock + the
             *    descent; unpowered -> the EXISTING gline 0x1A refusal +
             *    300-frame cooldown (unchanged). It no longer sets power
             *    or checks the battery — it only CHECKS power. */
            if (last_examine >= 0 &&
                manifest_word_token(line, "battery_terminal")) {
                em_examine_set_battery_terminal(last_examine);
                printf("manifest: OUTSIDE BATTERY TERMINAL examine — slot "
                       "%d at (%.1f, %.1f, %.1f) (battery-insert/power)\n",
                       last_examine, p[0], p[1], p[2]);
            } else if (last_examine >= 0 &&
                       manifest_word_token(line, "terminal")) {
                em_examine_set_terminal(last_examine);
                printf("manifest: INTERNAL TERMINAL examine — slot %d at "
                       "(%.1f, %.1f, %.1f) (power-gated elevator ride)\n",
                       last_examine, p[0], p[1], p[2]);
            }
        } else if ((gn = sscanf(line, "examinetext %d %d", &gk, &gl))
                   == 2) {
            /* chained AREA-bank record of the LAST examine line */
            const char *t = line + strlen("examinetext");
            for (int sk = 0; *t && sk < 2;) {       /* skip 2 numbers */
                while (*t == ' ') t++;
                while (*t && *t != ' ') t++;
                sk++;
            }
            while (*t == ' ') t++;
            char text[256];
            snprintf(text, sizeof text, "%s", t);
            text[strcspn(text, "\r\n")] = '\0';
            if (last_examine < 0 ||
                em_examine_text(last_examine, gk, gl, text) < 0)
                printf("manifest: examinetext line dropped: %s", line);
        } else if ((gn = sscanf(line, "enemy generator %f %f %f %f "
                                "kind %d link %d",
                                &x, &y, &z, &yaw, &gk, &gl)) >= 4) {
            /* Generator pad (em_enemy.c). This match must precede the
             * generic enemy match below, which would otherwise eat the
             * line as kind "generator". */
            if (gn < 5) gk = 1;  /* bare-line port defaults (engine
                                  * placements always carry both) */
            if (gn < 6) gl = 2;
            if (!g.enemy_test4) {
                float p[3] = { x, y, z };
                if (em_enemy_add_generator(em_frame_gfx(), p, yaw,
                                           gk, gl) < 0)
                    printf("manifest: generator line failed to load: %s",
                           line);
            }
        } else if (sscanf(line, "enemy %255s %f %f %f %f", name,
                          &x, &y, &z, &yaw) == 5) {
            /* Placed enemy instance (em_enemy.c). The AREA-11 door-husk
             * pair (`husk_creature` / `husk_partner`) ride the same enemy
             * line: both spawn STAGED-INERT, so the opening stays
             * enemy-free (INVESTIGATION_first_level_area11 §5.2) — no
             * separate token or "inert" flag needed (the inertness is
             * intrinsic to the kind, not a placement option). */
            int kind = strcmp(name, "crawler") == 0 ? EM_ENEMY_KIND_CRAWLER
                     : strcmp(name, "crate") == 0   ? EM_ENEMY_KIND_CRATE
                     : strcmp(name, "bug") == 0     ? EM_ENEMY_KIND_BUG
                     : strcmp(name, "egg") == 0     ? EM_ENEMY_KIND_EGG
                     : strcmp(name, "husk_creature") == 0
                                              ? EM_ENEMY_KIND_HUSK_CREATURE
                     : strcmp(name, "husk_partner") == 0
                                              ? EM_ENEMY_KIND_HUSK_PARTNER
                                                    : -1;
            if (kind < 0) {
                printf("manifest: unknown enemy kind, skipped: %s", line);
            } else {
                float p[3] = { x, y, z };
                int   ok;
                if (kind == EM_ENEMY_KIND_CRATE) {
                    /* optional tails (-1 = absent -> em_enemy's
                     * defaults): `bugs <n>` = the s68 nest-group
                     * size; `variant <v>` = the placement model byte
                     * (the husk-family key — func_001551B0
                     * @0x156380). Order-free: each keyword is scanned
                     * from wherever it appears. */
                    int bn = -1, vn = -1;
                    const char *t;
                    if ((t = strstr(line, " bugs ")))
                        sscanf(t, " bugs %d", &bn);
                    if ((t = strstr(line, " variant ")))
                        sscanf(t, " variant %i", &vn);
                    ok = em_enemy_add_crate(em_frame_gfx(), p, yaw,
                                            bn, vn);
                } else {
                    ok = em_enemy_add_kind(em_frame_gfx(), kind, p, yaw);
                }
                if (ok < 0)
                    printf("manifest: enemy line failed to load: %s", line);
            }
        }
    }
    fclose(f);
    /* LIGHTING — the rig arms only complete (all four rig lines
     * present); partial blocks fall back to the shader stand-in so a
     * half-written manifest never half-lights the scene. */
    g.rig_on = have_amb && have_cam && n_ldir >= 2;
    if (g.rig_on)
        printf("manifest: light rig — amb (%g %g %g), camera fill "
               "(%g %g %g) w %g, %d lamp(s)\n",
               g.rig_amb[0], g.rig_amb[1], g.rig_amb[2],
               g.rig_cam_col[0], g.rig_cam_col[1], g.rig_cam_col[2],
               g.rig_cam_w, g.n_lamp);
    if (g.fog_on)
        printf("manifest: distance fog — near %g far %g, fog RGB "
               "(%g %g %g) [0..128]\n",
               g.fog_near, g.fog_far,
               g.fog_rgb[0], g.fog_rgb[1], g.fog_rgb[2]);
    printf("manifest: %s — spawn (%.3f, %.3f, %.3f) yaw %.4f, "
           "collision %s%s%s, %d door(s), %d enem%s, %d pickup(s), "
           "%d examine(s)\n", mf,
           g.spawn[0], g.spawn[1], g.spawn[2], g.spawn_yaw, g.coll_path,
           g.bgm_file[0] ? ", bgm " : "", g.bgm_file, em_door_count(),
           em_enemy_count(), em_enemy_count() == 1 ? "y" : "ies",
           em_pickup_count(), em_examine_count());
    for (int i = 0; i < g.n_camregion; i++)
        printf("manifest: camregion %d — X[%.1f,%.1f] Z[%.1f,%.1f] "
               "y %.1f, fixed eye (%.1f, %.1f, %.1f)\n", i,
               g.camregion[i].x0, g.camregion[i].x1, g.camregion[i].z0,
               g.camregion[i].z1, g.camregion[i].ygate,
               g.camregion[i].eye[0], g.camregion[i].eye[1],
               g.camregion[i].eye[2]);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Load the active scene dir's EMDL files (alphabetical). Returns the
 * number loaded. */
int scene_load(EmGfx *gfx, SceneItem *items, int max_items)
{
    DIR *dir = opendir(g.scene_dir);
    if (!dir) return 0;

    char *names[SCENE_MAX];
    int   n_names = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && n_names < max_items) {
        const char *dot = strrchr(de->d_name, '.');
        if (!dot || strcmp(dot, ".emdl") != 0) continue;
        names[n_names] = malloc(strlen(de->d_name) + 1);
        if (!names[n_names]) break;
        strcpy(names[n_names], de->d_name);
        n_names++;
    }
    closedir(dir);
    qsort(names, n_names, sizeof(names[0]), cmp_str);

    int n = 0;
    for (int i = 0; i < n_names; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", g.scene_dir, names[i]);
        free(names[i]);
        SceneItem *it = &items[n];
        if (em_model_load(&it->model, path) != 0) continue;
        it->mesh = em_gfx_mesh_create(gfx, it->model.verts,
                                      it->model.vert_count, it->model.indices,
                                      it->model.index_count,
                                      (const EmGfxTexDesc *)it->model.texs,
                                      it->model.tex_count, it->model.texels,
                                      it->model.flags);
        if (!it->mesh) {
            em_model_free(&it->model);
            continue;
        }
        /* Static level parts pose once: bake the frame-0 palette now. */
        it->palette = malloc(it->model.bone_count * 16 * sizeof(float));
        if (!it->palette) {
            em_gfx_mesh_destroy(gfx, it->mesh);
            em_model_free(&it->model);
            continue;
        }
        em_model_palette_at(&it->model, 0, 0.0, it->palette);
        printf("scene: %s — %u verts, %u tris, %u textures\n", path,
               it->model.vert_count, it->model.index_count / 3,
               it->model.tex_count);
        n++;
    }
    return n;
}

/* Free the ACTIVE scene: level meshes, collision world, door + enemy
 * actors (the engine's actor-pool free at an area change). The player
 * model/mesh, the BGM stream and the sfx registry are NOT touched —
 * they persist across the switch (em_game.h em_game_scene_switch). */
void scene_unload(EmGfx *gfx)
{
    for (int i = 0; i < g.n_scene; i++) {
        em_gfx_mesh_destroy(gfx, g.scene[i].mesh);
        em_model_free(&g.scene[i].model);
        free(g.scene[i].palette);
    }
    g.n_scene = 0;
    em_collision_free(&g.coll);
    em_door_scene_clear(gfx);   /* keeps the in-flight transit lock */
    em_enemy_shutdown(gfx);
    em_enemy_reset();
    em_pickup_scene_clear(gfx); /* instances only — the inventory and
                                 * the taken-bit set survive (engine
                                 * globals; that survival IS the pickup
                                 * persistence, em_pickup.h) */
    em_examine_reset();         /* examine objects are per-scene
                                 * placements (no persistent state —
                                 * the engine re-arms them anyway) */
    elevator_unload(gfx);       /* the AREA-11 platform mesh (re-parsed
                                 * from the new scene's manifest) */
    grate_unload(gfx);          /* the AREA-11 gated-grate bars + hull
                                 * (re-installed from the new scene's
                                 * `grate` line) */
    em_truck_clear(gfx);        /* the AREA-11 wedged-truck actor + mesh
                                 * (re-installed from the new scene's
                                 * `truck` line) */
    /* AREA-11 OPENING DIRECTOR — drop any in-flight beat at a scene
     * change (belt-and-suspenders soft-lock guard: a beat can't actually
     * be running across a switch, since the beat lock suppresses every
     * use-scan that could trigger a goto door — but clearing here makes
     * it impossible for a stale lock to survive into the next scene). The
     * step byte re-arms to 0 in the scene-init state-0 arm; clear the
     * transient now so the player is never locked between unload and arm. */
    g.cine_active = 0;
    g.cine_beat   = -1;
    g.cine_kf     = 0;
    g.cine_kf_t   = 0;
    g.cine_was    = 0;
}
