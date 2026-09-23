/* em_render_frame.c — render stages of the world frame.
 *
 * Moved unchanged out of em_game.c in step S5 of
 * docs/SCENE_COORDINATOR_DESIGN.md (tools/split_module.py): the point-light
 * tick, the port-native draw-list collector, the render-env skeleton, the
 * status-menu UI scene, the character light rig and the close-out. New in
 * S5: the stage functions em_render_001D1C50, em_render_001C1D00,
 * em_render_001D1EA0 and em_camera_0018B9C0, each wrapping the calls the
 * frames made at that position. Behaviour is unchanged by the move — only
 * the file boundary is new. Since S10a the scene cores em_sf_001AE5E0 /
 * em_sf_001AE6B0 call them through the bindings (em_scene_bindings.c), and
 * em_camera_0018B9C0_opening is the cutscene variant's camera stage.
 *
 * Every function here reads the shared gameplay state (EmGameState g), so
 * this module takes the subsystem's internal header rather than owning
 * private state — the same single state block the engine keeps in its
 * gameplay globals, now viewed from one more file. */

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
#include "game/em_scene_bindings.h"
#include "game/em_opening_actor.h"
#include "game/em_snow_runtime.h"
#include "game/em_area11_effect_runtime.h"
#include "game/em_area11_interaction_host.h"
#include "game/em_level_smoke_test.h"
#include "game/em_opening_control_test.h"

/* See em_render_001D1EA0. */
static int s_request_status_frame;

static uint32_t point_light_random(void *context)
{
    (void)context;
    return em_random_next();
}

static void point_light_tick(void)
{
    if (g.point_lights_loaded)
        em_point_light_tick(&g.point_lights, g.point_lights_area_key,
                            point_light_random, NULL);
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

/* Native draw-list collector (port-only). It runs at the func_001D1C50
 * slot of the frame order, but 001D1C50 is the per-frame GS/fog/display-
 * list setup and records no draws (see the frame table at the top of
 * em_game.c).
 * Collects scene parts first, then the player; with no assets at all,
 * the gradient test triangle keeps the repo runnable standalone. */
void render_chain_build(void)
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
    /* AREA11's static power panel, original record18/model04. The legacy
     * manifest and struct field names retain the former grate label. */
    if (g.grate_present && g.grate_mesh && g.grate_palette) {
        ChainDraw *cd = chain_push();
        if (cd)
            *cd = (ChainDraw){ g.grate_mesh, g.grate_palette,
                               g.grate_model.bone_count, NULL };
    }
    /* WEDGED TRUCK (AREA-11 record 16, owner 00823FF0). Drawn as a rigid
     * prop at its manifest placement: it stays static until the original
     * behaviour is translated (WP-12, em_truck.h). Absent when no `truck`
     * line placed one. */
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
    if (g.mesh && !em_opening_runtime_actors_active()) {
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
 * RE-CONFIRMED (audit 2026-07-31, second pass): the 0.01f ramp/yaw
 * steps, the pi
 * seed and -2pi wrap, the -80/-100/-30 x 0.01 tint triple with the
 * -127 clamp on the G row, anim_advance_time(1.0f), the 1.3f ramp
 * ceiling, the state-0 clip select `!(D_00810858 <= 35.0f) -> 0x1C2
 * else 0xA`, and the clip swap
 * being SWAP-BACK ONLY (`health > 35 && +0xB == 1 ->
 * anim_clip_init(self, 0x1C2, 16.0f, 0.0f)`, never the
 * reverse) all read literally out of src/func_0020E6F0.c. One detail
 * the port folds: the engine's swap-back re-inits at rate 16.0f, while
 * ui_scene_render just restarts ui_t and keeps its own 1.0/frame
 * advance — a presentation-only difference on the menu turntable.
 * The actor is
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
                           int cam_fill, int fold_lamps);

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
        char_rig_build(&rig, NULL, 0, 1);
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
 * AREA11's original pool uses em_point_light_fold: the 001D8340 scan,
 * inverse-square weighting, per-slot flicker matrix and normalization.
 * The two seed qwords at 00253170/80 are zero; the color accumulator is
 * replaced by the camera color before scanning. Original opening actors
 * use verified light-reference bones 1/2/0 and pass the 001D8270 gate.
 * Eligibility for other actor types remains unaudited. Faces bypass the
 * fold, as their original 001D88B0 owner is null.
 *
 * Other scenes retain the legacy lamp path below, which omits the
 * original flicker matrices and story-flag gates. Actor RGB/self-glow
 * before the color clamp also remains separate: existing post-draw tint
 * is preserved and is not asserted equivalent to the original order.
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
                           int cam_fill, int fold_lamps)
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
    if (fold_lamps && anchor && g.point_lights_loaded) {
        float direction[4], color[4] = { c0[0], c0[1], c0[2], g.rig_cam_w };
        const float point[4] = { anchor[0], anchor[1], anchor[2], 1.0f };
        for (unsigned axis = 0; axis < 3; ++axis)
            direction[axis] = em_effect_float32((double)d0[axis] * g.rig_cam_w);
        direction[3] = 0.0f;
        em_point_light_fold(direction, color, &g.point_lights, point);
        memcpy(d0, direction, sizeof d0);
        memcpy(c0, color, sizeof c0);
    } else if (fold_lamps && anchor && g.n_lamp) {
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
 * counting matches the rendered gameplay frames.
 * None of 001AAD00's hooks or its list swap is in here (the bindings
 * report 001AAD00 as reached without port code); the stage wrapper is
 * em_render_001D1EA0. */
void frame_close_out(void)
{
    EmGfx *gfx = em_frame_gfx();

    /* MENU-RENDER GATE: the UI-camera 3D scene replaces the world
     * flush while the status screen shows (visible = the real toggle
     * OR the EM_HUD_FORCE capture hook) and the real background will
     * draw over it. Without the player asset or the ui.emui backdrop
     * the old path runs unchanged (asset-absent frames byte-identical). */
    int ui_scene = !em_opening_runtime_busy() && g.mesh &&
                   em_hud_visible() && em_hud_backdrop_ready(gfx);

    if (s_request_status_frame && !em_hud_visible()) {
        if (em_area11_interaction_host_status_render(gfx) != 1)
            em_frame_request_quit(); /* the host latched and reported the fault */
    } else if (ui_scene) {
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
                               cd->palette == g.player_palette, 1);
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
        /* Original opening palettes already contain world placement.
         * They replace the ordinary player pose only while the script
         * owns the actors. The same scene lighting applies to each. */
        if (em_opening_runtime_actors_active()) {
            for (unsigned index=0;index<3;++index) {
                EmGfxMesh *mesh;
                const float *palette;
                uint32_t bone_count;
                if (!em_opening_actor_record(index,
                        em_opening_runtime_half_tick(),&mesh,&palette,
                        &bone_count)) continue;
                if (g.rig_on) {
                    EmGfxCharRig rig;
                    /* Original actor+98 selects light-reference bones
                     * 1/2/0; actor+2 bit0x20 is set on both humans. */
                    unsigned anchor_bone=index==0?1:index==1?2:0;
                    char_rig_build(&rig,palette+anchor_bone*16+12,index<2,1);
                    em_gfx_char_rig(gfx,&rig);
                    if (index < 2) {
                        /* Original face 001D88B0 enables camera fill and
                         * passes owner=NULL, bypassing dynamic lamps. */
                        char_rig_build(&rig,NULL,1,0);
                        em_gfx_char_face_rig(gfx,&rig);
                    }
                } else em_gfx_char_rig(gfx,NULL);
                em_gfx_draw_skinned(gfx,mesh,g.viewproj,palette,bone_count);
            }
        }
        /* Original pickup children use unlit additive drawing after the
         * opaque owner meshes, with the owner's current world matrix. */
        em_pickup_lights_draw(gfx, g.viewproj);
        em_props_indicators_draw(gfx, g.viewproj);
        em_snow_runtime_draw(gfx, g.cam.view,
            g.cam.zoom > 0.0f ? g.cam.zoom : ENGINE_CAM_ZOOM_S);
        em_area11_effect_runtime_draw(gfx, g.cam.view,
            g.cam.zoom > 0.0f ? g.cam.zoom : ENGINE_CAM_ZOOM_S);
        em_gfx_char_rig(gfx, NULL);   /* LIGHTING — rig is per draw */
        em_gfx_fog_off(gfx);          /* LIGHTING — fog off after the world flush */
    }
    g.ui_prev = ui_scene;     /* edge tracking for the scene re-init */
    em_hud_scene_3d(ui_scene);  /* background skips its base fill */

    /* Weapon feedback overlays (crosshair / muzzle-flash placeholders —
     * em_weapon.h "VISUAL FEEDBACK"); queues nothing while holstered, so
     * the default frame stays byte-identical. */
    em_weapon_render(gfx);

    /* STATUS SCREEN over the flushed 3D frame (the engine's GS-sprite
     * status overlay; em_hud queues overlay rects, em_gfx_end_frame
     * draws them last). HIDDEN by default — the original shows no
     * persistent HUD — and toggled by a TRIANGLE/START press (edge);
     * shown, the 3D frame underneath is the UI scene above (or the
     * flagged dim fallback without assets). Since S11b the original frame
     * machine opens and closes it and freezes the world while it shows
     * (state 3: 0020CDC0 = em_hud_status_tick, then this close-out as
     * 001D1EA0(0)); see em_hud.h "STATUS OPEN/CLOSE". EM_HUD_FORCE=1
     * forces it visible for overlay tests (its navigation hook below).
     * The ammo readout is LIVE: mag/reserve mirror the weapon state
     * (D_00810C62 / D_00810CB4) every frame, exactly like the engine UI
     * re-reading the globals. */
    em_hud_forced_update(em_frame_input());
    g.status.mag     = em_weapon_mag();
    g.status.reserve = em_weapon_reserve();
    g.status.battery = em_pickup_battery_charge() >> 1;
    g.status.battery_max = em_pickup_battery_capacity() >> 1;
    g.status.items   = em_pickup_items();   /* the D_00810C64 mirror —
                                             * the ITEM page's real
                                             * per-type counts */
    em_hud_render(gfx, &g.status);
    em_hud_radio_render(gfx);   /* the radio/examine message machine */
    em_examine_render(gfx);     /* EXAMINE area-bank chain text — the
                                 * mode-2 presentation for lines the
                                 * global radio machine can't address
                                 * (GLOBAL lines drew inside em_hud
                                 * just above) */
    em_hud_area_title_render(em_scene_state()->spad3B8D ? NULL : gfx);
                                /* 001C5930 suppresses selectors1/2/3,
                                 * but its 300-frame lifetime still ticks.
                                 * AREA-11 opening title card ("FORT
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
    if (g.go_state == GO_SCREEN)
        em_hud_game_over(gfx);
    else if (g.go_state >= GO_PROMPT)
        em_hud_continue(gfx, g.go_cursor);

    /* em_frame owns transition ticking/drawing after task dispatch. */

    if (!em_opening_control_test_active() && !em_level_smoke_test_active() && g.capture_path &&
        g.frame_no == g.capture_frame)
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

    em_opening_control_test_after_frame();
    em_level_smoke_test_after_frame();
    g.frame_no++;
    /* A scripted self-test owns the quit when combined with a capture,
     * so a mid-script capture doesn't cut the script short. */
    if (!em_opening_control_test_active() && !em_level_smoke_test_active() && g.capture_path &&
        !g.move_test && !g.weapon_test && !g.door_test &&
        !g.transit_test && !g.slider_test &&
        g.frame_no > g.capture_frame + 1)
        em_frame_request_quit();
}

/* ------------------------------------------------------------------ */
/* Stage functions (S5, docs/SCENE_COORDINATOR_DESIGN.md section 4.5)  */
/* ------------------------------------------------------------------ */

/* func_001D1C50 position (both world-frame variants). The original is
 * the per-frame GS/fog/display-list setup (src/func_001D1C50.c); among
 * its calls is 001D7C30, which the port runs as point_light_tick. That
 * tick is the only port code at this position: the fog is still applied
 * inside frame_close_out, and render_chain_build (a port-native draw-list
 * collector, not a translation) runs in the 001AFD70 legacy block. */
int em_render_001D1C50(void)
{
    point_light_tick();
    return 0;
}

/* func_001C1D00(0x008101D0) position (both variants). Wraps
 * render_env_init, which is an empty skeleton: the original render-env
 * work is NOT translated. The 0 return means "today's code ran", not
 * "001C1D00 is ported". */
int em_render_001C1D00(void)
{
    render_env_init();
    return 0;
}

/* func_001D1EA0(a0) position: 001D1EA0(1) ends both world-frame variants
 * (status state 3 uses 001D1EA0(0)). The original (src/func_001D1EA0.c)
 * runs 001E0D70 and 001DDA00 only when a0 != 0 and 001D2910(4) == 0, then
 * always 001CB800. Wraps today's close-out unchanged: the world flush,
 * overlays, the status screen, the capture hook and the frame counter.
 * frame_close_out does not yet distinguish a0, so the argument is
 * accepted and not consumed: in the status frame (state 3, a0 = 0; S11b)
 * it still redraws the frozen world chain (or the status UI scene) where
 * the original skips 001E0D70/001DDA00, as the legacy frozen frame did. */
/* 001D1EA0(a0) (src/func_001D1EA0.c): a0 != 0 flushes the world
 * (001E0D70/001DDA00) before the overlay list; the status frames pass 0.
 * In AREA11 (since WP-4 for requests, WP-5 for every status screen) the
 * status frames draw the host's original page (the hub, ITEM or BATTERY)
 * with no world flush under it (s_request_status_frame, frame_close_out). */
int em_render_001D1EA0(int a0)
{
    s_request_status_frame = a0 == 0 && em_area11_interaction_host_status_route();
    frame_close_out();
    s_request_status_frame = 0;
    return 0;
}

/* func_001ABF90 position of the byte-matched 001AD4E0 (game-over steps 3
 * and 4 push its GS packet every tick: the screen module 0x27 image). The
 * port has no module 0x27 art; it draws today's close-out, whose
 * em_hud_game_over stand-in (g.go_state == GO_SCREEN, set at the
 * 001FF080(0, 0x27) binding) covers the frozen world with an opaque base
 * under the transition fade, as the legacy game-over frame did. */
int em_render_001ABF90(void)
{
    frame_close_out();
    return 0;
}

/* func_0018B9C0 position of the gameplay variant (after the bindings'
 * 001CB590(0x008101E0, 0xD0, 0) worker). Wraps the two calls
 * gameplay_frame made there, in the same order. */
/* 0018B9C0's head (NEARMISS, logic recovered): the global cooldown
 * D_008106EF decays by one per camera stage (0x46 after a status screen,
 * 0x50 after an interaction script; 00184BA0 refuses the Use scan while it
 * is nonzero). The canonical byte is EmSceneState's request block. */
static void camera_cooldown_0018B9C0(void)
{
    uint8_t *ef = &em_scene_state()->req[EM_SCENE_REQ_EF];
    if (*ef)
        --*ef;
}

int em_camera_0018B9C0(void)
{
    camera_cooldown_0018B9C0();
    camera_update();         /* func_0018B9C0 camera state machine    */
    em_sfx_listener(g.pos, g.cam.eye, g.cam.yaw);  /* positional-audio
                              * listeners: player = distance
                              * (D_00810360), camera eye/yaw = pan
                              * (D_008105D0 / cam+0x9C) — em_sfx.h    */
    return 0;
}

/* func_0018B9C0 position of the cutscene variant (001AE6B0). Wraps the
 * calls cutscene_frame made there, in the same order: while the opening
 * script owns the camera, em_opening_runtime_camera() runs it (design 4.4
 * #19: the opening controller's camera sits at the 0018B9C0 stage);
 * otherwise the chase camera; then the positional-audio listeners. */
int em_camera_0018B9C0_opening(void)
{
    camera_cooldown_0018B9C0();
    if (!em_opening_runtime_camera())
        camera_update();
    em_sfx_listener(g.pos, g.cam.eye, g.cam.yaw);
    return 0;
}
