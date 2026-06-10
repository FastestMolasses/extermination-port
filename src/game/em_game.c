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
 * input block drives the player around the room. Left stick (WASD) walks
 * camera-relative on the XZ plane; the facing yaw seeks the movement
 * direction (smooth turn). The placement is composed onto the evaluated
 * anim palette each frame (rotation about Y by yaw, then translation —
 * AFTER the animation pose; see palette_apply_placement). Movement runs
 * through the engine's COLLISION WORLD (src/game/em_collision.[hc] over
 * the id 0x44 EMCL bake): func_0019AD00-style move probes stop/slide the
 * player at walls and a vertical segment query sets the floor height;
 * with no collision asset the old room-bbox clamp remains. The camera is
 * the engine's own chase camera (clamped proportional follow, FINDINGS.md
 * "CAMERA SYSTEM" port contract) with its desired-eye solver running
 * func_0018D7B0-style segment queries (mask 6) against the same world;
 * d-pad (arrow keys) LEFT/RIGHT feeds a yaw input into the camera
 * struct. Esc still quits (em_frame.c step C).
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
#include "game/em_frame.h"
#include "game/em_hud.h"
#include "game/em_sfx.h"
#include "game/em_task.h"
#include "game/em_weapon.h"

#define MODEL_PATH     "assets/player.emdl"
#define SCENE_DIR      "assets/scene"
#define SCENE_MANIFEST "assets/scene/scene.txt"
#define COLL_DEFAULT   "office.emcl"
#define SCENE_MAX      16

/* DEFAULT player spawn — the office room (chunk06.n1 level): the live
 * GS-dump capture has the character standing at ~(107.4, 0, -184); the
 * level floor there is y = 0 and the player EMDL is recentred at the
 * origin with its feet at y ~= 0. A scene manifest (scene.txt, below)
 * overrides this per scene; the office values stay as the no-manifest
 * defaults so an unmanifested assets/scene behaves exactly as before. */
static const float kPlayerPos[3] = { 107.4f, 0.0f, -184.0f };

/* Walkable-bounds FALLBACK: the office room's collision bbox (the id 0x44
 * file — FINDINGS.md "COLLISION WORLD", chunk06.n1). Used only when no
 * EMCL collision world is generated (assets/scene/office.emcl, from the
 * decomp repo's tools/export_collision.py); with the world loaded the
 * real engine queries run instead — em_collision_move_probe
 * (func_0019AD00 collide-and-slide) for movement and
 * em_collision_segment_query (func_0019A570) for the floor and the
 * camera solver. */
static const float kRoomMin[2] = { -3.6f,  -296.0f };  /* x, z */
static const float kRoomMax[2] = { 120.5f,    2.4f };

/* Vertical floor-probe window: the engine's actor spine resolves height
 * with separate vertical segment queries through the same hub (e.g. the
 * frozen scratchpad query in FINDINGS, a y+200..y-200 down-probe). The
 * port probes from step-height above the feet to a drop window below. */
#define FLOOR_PROBE_UP    8.0f
#define FLOOR_PROBE_DOWN  8.0f

/* Horizontal wall-probe height above the feet (knee height; the
 * character is ~15 units tall). Probing at exactly y = feet is fragile
 * against coarse outdoor collision tris whose bottom edge sits ON the
 * ground (snow-scene gate walls): at foot level the wall cross-section
 * thins to an epsilon sliver and the probe slips under it. Office
 * geometry is vertical and floor-seated, so the lift does not move any
 * office hit point. */
#define WALL_PROBE_LIFT   1.0f

/* Contact skin: rest a blocked actor a hair IN FRONT of the hit plane.
 * Landing exactly ON the plane makes the next frame's probe start on /
 * fp-behind it, where the walkers' t >= 0 interval test goes blind and
 * the wall stops registering (tunneling while sliding along the snow
 * gate). Within the move test's 0.05 position tolerance. */
#define WALL_SKIN         0.01f

/* Movement / camera tuning. The character is ~15 units tall; roughly one
 * body height per second reads as a natural walk at room scale (the room
 * spans ~124 x ~298 units). The loop is vsync-locked at 60 Hz exactly
 * like the PS2 original, so a fixed dt keeps everything deterministic. */
#define FRAME_DT        (1.0f / 60.0f)
#define WALK_SPEED      15.0f   /* units/sec */
#define TURN_SPEED      12.0f   /* rad/sec — facing seeks the move dir */
#define AIM_TURN_SPEED  1.8f    /* rad/sec — PORT CONSTANT: turn-in-place
                                   rate while aiming (stand-in for the
                                   engine's aim-yaw blend steer; see the
                                   planted-aiming block in player_move) */

/* Animation clips + crossfade. Library clip ids (chunk28/f01_id3c —
 * for the player the anim id IS the container index, FINDINGS "ANIM ID
 * MAPPING"). The AUTHENTIC stick-locomotion ids, decoded 2026-06-10
 * from the boot ELF's selection chain (replaces the s10-era stride-scan
 * guess of clips 2/3):
 *
 *   stick deflection -> func_001B5CC0 quantizer (rings r=48/88/122) ->
 *   gait byte 0..3 (pad struct +0x17 = 0x810E57) -> player +0x23F ->
 *   locomotion top func_001612D0: locIdx = gait-1, speed =
 *   D_00248870[locIdx] = {0.0, 0.1, 0.3, 0.8} u/tick, anim id =
 *   D_00248AB0[mode 1][family*4 + locIdx] (func_0017B490/func_0017B460;
 *   unarmed family 0 row = {0, 1, 2, 3}).
 *
 * So gait 1 = turn-in-place (id 0, speed 0), gait 2 = WALK (id 1,
 * 0.1 u/tick = 6 u/s), gait 3 (full stick) = RUN (id 2, 0.3 u/tick).
 * The quantizer never returns 4, so id 3 (0.8 u/tick row) is a sprint
 * data slot the stick cannot reach — not shipped. The walk clip is
 * baked IN PLACE; its natural ground speed at 60 fps is 6.11 u/s
 * (exporter-printed; the engine's own 0.1 u/tick = 6.0 u/s at 60 Hz
 * cross-checks it), so playback rate = move_speed / that keeps the
 * feet tracking the ground. Idle<->walk is a 0.15 s LINEAR palette
 * blend — the engine cross-fades clip transitions the same way
 * (PROGRESS.md: mid-blend live captures match no single clip). */
#define CLIP_ID_IDLE    346u
#define CLIP_ID_WALK    1u      /* engine walk (was library clip 2) */
#define CLIP_ID_RUN     2u      /* engine run — in the asset for later
                                 * (natural speed 24.07 u/s, 45 fr) */
#define WALK_CLIP_SPEED 6.11f   /* units/sec at the baked 60 fps */

/* FOOTSTEP TRIGGER FRAMES — the engine's per-anim-id property table
 * D_00248C90 (FINDINGS "ANIM ID MAPPING": frameA/frameB per row; the
 * per-frame func_00187350 fires the step sound + decal when the
 * committed clip time crosses them). Rows re-read from the user's
 * local boot ELF for the authentic ids: walk id 1 (120 fr) -> 72/21 —
 * the exact pair the s29 live capture metered while stick-walking;
 * run id 2 (45 fr) -> 26/3. Each trigger plays the s29 two-layer step
 * (surface pair + gear pair, both alternating per step —
 * footstep_play below). */
#define WALK_STEP_FRAME_A 72.0f /* D_00248C90[1].frameA */
#define WALK_STEP_FRAME_B 21.0f /* D_00248C90[1].frameB */
#define RUN_STEP_FRAME_A  26.0f /* D_00248C90[2].frameA — for the run
                                 * clip when locomotion drives it */
#define RUN_STEP_FRAME_B  3.0f  /* D_00248C90[2].frameB */
#define ANIM_BLEND_TIME 0.15f   /* seconds, idle<->walk crossfade */
#define STICK_DEADZONE  0.25f
#define EM_PI           3.14159265f

/* Camera values — the AUTHENTIC engine numbers (FINDINGS.md "CAMERA
 * SYSTEM", live-verified in save states 01/03): eye ~33 u behind the
 * player along the camera yaw and ~19 u above the player's ground Y;
 * the generic follow seeks the target to player.y + 15 (the live
 * area-0x1100 director measured ground + 17; the documented smooth-table
 * follow constant is 15.0). Chase caps are the engine's: 0.8 u/frame for
 * the target follow, 4.0 u/frame for the eye solver (style 0). */
#define CAM_DIST        33.0f   /* desired eye distance behind the player */
#define CAM_EYE_HEIGHT  19.0f   /* desired eye height above player ground Y */
#define CAM_TGT_HEIGHT  15.0f   /* target height above player Y (follow) */
#define CAM_AIM_OFFSET  6.0f    /* struct +0x8C default — target height in
                                   player aim state 5 (TODO: player states) */
#define CAM_TGT_CAP     0.8f    /* target chase rate cap, units/frame */
#define CAM_EYE_CAP     4.0f    /* eye chase rate cap, units/frame */
#define CAM_NEAR_PUSH   4.0f    /* commit: view position = eye + 4*fwd */
#define CAM_ORBIT_SPEED 1.8f    /* rad/sec — d-pad LEFT/RIGHT orbit (port
                                   input; lands in the struct yaw +0x44) */

/* Engine projection (FINDINGS.md "CAMERA SYSTEM" section 3) — recorded
 * for eventual native adoption; rendering still goes through
 * em_mat4_perspective below. TODO(projection): adopt the s = 480 zoom
 * model. P rows: (0.8s,0,0,0) (0,0.5s,0,0) (2048,2048,0.8996,1)
 * (0,0,1677721.5,0) -> screen x = 0.8s*x/z + 2048, y = 0.5s*y/z + 2048
 * (GS center 2048, screen y down), z = 0.8996*z + 1677721.5 (24-bit GS
 * Z), w_clip = z_view. Native remap: tan(half-hfov) = half_w_gs/(0.8s).
 * Zoom is fixed 480 except the scope camera (s = 224/x, func_001D25F0)
 * and scripted zoom lerps (func_001D2590). */
#define ENGINE_CAM_ZOOM_S      480.0f      /* render-ctx +0x2468 default */
#define ENGINE_CAM_PROJ_ZSCALE 0.8996f     /* GS-Z row scale */
#define ENGINE_CAM_PROJ_ZOFFS  1677721.5f  /* GS-Z row offset */
#define ENGINE_CAM_GS_CENTER   2048.0f     /* GS screen-center offset */

/* Task user-byte indices — mirror the live slot-0 record (state bytes
 * observed at record +8 / +9 / +0xB, i.e. user[0] / user[1] / user[3]). */
#define GAME_BYTE_MAIN  0
#define GAME_BYTE_SUB   1
#define GAME_BYTE_FRAME 3

typedef struct {
    EmModel    model;
    EmGfxMesh *mesh;
    float     *palette;   /* static pose (frame 0), bone_count * 16 */
} SceneItem;

/* One recorded draw — the native render-chain element. */
typedef struct {
    EmGfxMesh   *mesh;
    const float *palette;
    uint32_t     bone_count;
} ChainDraw;

/* CAMERA state — the native mirror of the engine's camera struct at
 * 0x008101E0 (0xD0 bytes) plus the global camera vector pool at
 * 0x008105D0.. (FINDINGS.md "CAMERA SYSTEM" section 1). Field comments
 * give the PS2 offsets/addresses; only what the mode-0 generic follow
 * consumes is live — the rest is carried so the per-room overlay
 * directors and the remaining mode handlers land in place. */
typedef struct {
    uint8_t  state;       /* +0x00: 0 = init one-shot, 1 = run */
    uint8_t  sub_state;   /* +0x01: 0->1 ramp on first run frame (zeroes
                             the mode timer) */
    uint8_t  top_mode;    /* +0x04: 0 = normal play, 1/2 = frozen (commit
                             only), 3 = scope/sniper func_0022EEF0 (TODO) */
    uint8_t  table_sel;   /* +0x05: 0 = cut jtbl_0026D950, 1 = smooth
                             jtbl_0026D910 */
    uint8_t  mode;        /* +0x06: camera mode 0..15 — only mode 0
                             (generic follow) implemented; see
                             camera_mode_dispatch for the TODO list */
    uint8_t  hit;         /* +0x07: follow-solver result byte (stays 0
                             until the collision-aware solver lands) */
    uint16_t timer;       /* +0x08: mode timer */
    float    eye_des[3];  /* +0x10: desired EYE (world) */
    float    tgt_des[3];  /* +0x20: desired TARGET (world) */
    float    yaw;         /* +0x44: eye->target heading; the d-pad orbit
                             is an input into this field */
    float    aim_h;       /* +0x8C: target height offset above player Y,
                             default 6.0 (player aim state 5 — TODO) */
    /* global camera vector pool (the real per-frame camera output) */
    float    eye[3];      /* D_008105D0: actual eye — chased toward
                             eye_des, capped 4.0/frame */
    float    tgt[3];      /* D_008105E0: actual target — copy of tgt_des
                             (func_0018C0C0) */
    float    up[3];       /* D_008105F0: (0,-1,0) — the engine's Y-DOWN
                             view-up, set once at init */
    float    fwd[3];      /* D_00810600: normalized forward (commit) */
    float    view[16];    /* D_00810610: look-at view matrix */
} EmCamera;

static struct {
    /* assets */
    EmModel    model;            /* player */
    EmGfxMesh *mesh;             /* player (NULL = not loaded) */
    SceneItem  scene[SCENE_MAX];
    int        n_scene;
    float      player_palette[1024 * 16]; /* bone_count <= 1024 (loader) */

    /* gameplay-frame state */
    double     t;                /* idle clip time, seconds (60 ticks/s) */
    int        frame_no;         /* gameplay frames run */
    uint8_t    frame_selector;   /* scratchpad 0x70003B8D: 0 = gameplay */

    /* animation clips + idle<->walk crossfade */
    int        clip_idle;        /* clip indices into model.clips */
    int        clip_walk;        /* -1 = no walk clip (EMD2 asset) */
    double     walk_t;           /* walk clip time, seconds (rate-scaled) */
    float      walk_w;           /* walk blend weight 0..1 */
    double     step_prev;        /* last frame's walk-cycle position in
                                  * clip FRAMES (footstep edge detect) */
    int        step_parity;      /* s29 per-step L/R alternation (flips
                                  * both footstep layers together) */
    float      move_speed;       /* this frame's ground speed, units/sec */
    float      walk_palette[1024 * 16];  /* scratch for the blend */

    /* scripted-anim mailbox (em_game.h em_game_anim_request): the
     * native player+0x1F2 request / +0x20C commit pair. sa_req/sa_cur
     * hold library clip IDS (0 = none); sa_clip is the committed
     * model clip-table index; sa_t the clip time in FRAMES. sa_hold
     * (riding the request as sa_req_hold) marks a HELD POSE: the clip
     * clamps at its last frame instead of returning to locomotion
     * (em_game_anim_hold — the weapon aim pose). */
    unsigned   sa_req;           /* requested clip id   (+0x1F2) */
    float      sa_rate;          /* requested rate      (+0x1F8) */
    int        sa_req_hold;      /* request is a hold (aim pose) */
    unsigned   sa_cur;           /* committed clip id   (+0x20C) */
    int        sa_clip;          /* committed clip index, -1 = none */
    int        sa_hold;          /* committed clip holds its end pose */
    double     sa_t;             /* scripted clip time, frames */

    /* player world placement (the actor's position + facing) */
    float      pos[3];           /* world position, feet on the floor */
    float      yaw;              /* facing about +Y, radians; 0 = +Z */

    /* collision world (id 0x44 -> EMCL; 0 polys = not loaded) */
    EmCollision coll;

    /* camera (struct 0x008101E0 + vector pool — see EmCamera above) */
    EmCamera   cam;

    /* player status (the engine globals em_hud.h documents; static demo
     * values until the weapon/health systems are translated) */
    EmPlayerStatus status;

    /* the camera block the recorded chain consumes (native K = P*V) */
    float      viewproj[16];

    /* render chain (this frame's recorded draws) */
    ChainDraw  chain[SCENE_MAX + 1 + EM_DOOR_MAX + EM_ENEMY_MAX];
    int        chain_len;
    int        chain_test_triangle;

    /* EM_BGM=<path.wav>: loop this cue WAV as level music (see boot task) */
    const char *bgm_path;

    /* SCENE MANIFEST (assets/scene/scene.txt) — per-scene boot config,
     * written by the exporters. Defaults = the office values, so a
     * missing manifest keeps the historical behavior bit-for-bit. */
    float       spawn[3];        /* "spawn x y z yaw" — TRUE world coords */
    float       spawn_yaw;       /* facing about +Y, radians; 0 = +Z */
    char        coll_path[288];  /* "collision <file.emcl>" in SCENE_DIR */
    char        bgm_file[256];   /* "bgm <file.wav>" in SCENE_DIR; "" = none */

    /* EM_CAPTURE / EM_MOVE_TEST / EM_DOOR_TEST debug instrumentation */
    const char *capture_path;
    int         capture_frame;
    int         capture_aim;     /* EM_CAPTURE_AIM=1 — hold R1 from frame
                                  * 0 so the capture shows the armed
                                  * stance (aim pose + laser + camera) */
    int         move_test;
    int         move_legs[2];    /* EM_MOVE_LEGS=fwd,strafe frame counts */
    int         move_expect_set; /* EM_MOVE_EXPECT=x,y,z final-pos override */
    float       move_expect[3];
    int         door_test;       /* EM_DOOR_TEST=1 — door interaction test */
    int         dt_door;         /* test door index (the west doorway) */
    int         dt_ok_trigger;   /* X press put the door in OPENING */
    int         dt_ok_lock;      /* input locked during the transit */
    int         dt_ok_inputdead; /* held stick did NOT move the player */
    int         dt_ok_anim;      /* scripted door anim 0x43 committed
                                  * mid-open (back side of the test door) */
    float       dt_min_x;        /* min player x while the door not OPEN */
    float       dt_max_fade;     /* peak fade level seen (must reach 1.0) */
    int         weapon_test;     /* EM_WEAPON_TEST=1 — firing-loop test */
    int         wt_fail;         /* weapon test: failed checkpoints */
    int         enemy_test;      /* EM_ENEMY_TEST: 1 = shoot-the-crawler
                                  * run, 2 = let-it-reach-the-player run,
                                  * 3 = walk-at-the-crate burst run */
    int         sfx_test;        /* EM_SFX_TEST=1 — one-shot mixer test */
    int         et_spawned;      /* test enemy placed at scene init */
    int         et_fail;         /* failed checkpoints */
    float       et_d0;           /* spawn distance to the test enemy */
    float       et_health0;      /* player health at frame 0 */
    int         et_fired;        /* frame the kill-run shot was injected */
    int         et_hit_frame;    /* frame the contact run lost health */
    int         et_burst_frame;  /* crate run: frame the crate burst */
    float       et_bd;           /* crate run: crate distance at burst */
    float       et_wd0;          /* crate run: worm distance at burst */
    float       et_wd_min;       /* crate run: min worm distance since */
    int         et_worm_atk;     /* crate run: worm reached ATTACK */
} g;

/* SCENE MANIFEST — a plain-text scene.txt in the scene directory, written
 * there by the decomp repo's exporters (tools/export_level.py --spawn /
 * --bgm, tools/export_collision.py). Zero-dependency parser; "key value"
 * lines, '#' starts a comment, unknown keys are ignored:
 *
 *   spawn <x> <y> <z> <yaw>   player spawn, TRUE world coords + facing (rad)
 *   collision <file.emcl>     collision world filename inside the scene dir
 *   bgm <file.wav>            optional looping level-music cue WAV (scene
 *                             dir); the EM_BGM env override still wins
 *   door <file> <x> <y> <z> <yaw> <r>
 *                             one INTERACTIVE DOOR instance (file under
 *                             the scene dir, e.g. doors/door_m03.emdl;
 *                             r = use-scan trigger radius) — written by
 *                             export_props.py --doors, owned by em_door.c
 *   enemy crawler <x> <y> <z> <yaw>
 *                             one placed CRAWLER (the func_001551B0
 *                             placement records); owned by em_enemy.c.
 *   enemy crate <x> <y> <z> <yaw>
 *                             one DISGUISED CRATE (the crawler's IDLE
 *                             disguise as its own kind — em_enemy.h
 *                             "CRATE KIND"): idles as the office crate
 *                             mesh, bursts into gibs + a crawler on a
 *                             bullet hit or ~10-u player proximity.
 *                             Other kinds are reported and skipped.
 *
 * A missing file or missing key leaves the office defaults in place, so
 * the default scene needs no manifest to keep its exact behavior. */
static void scene_manifest_load(void)
{
    g.spawn[0]  = kPlayerPos[0];
    g.spawn[1]  = kPlayerPos[1];
    g.spawn[2]  = kPlayerPos[2];
    g.spawn_yaw = 0.0f;
    snprintf(g.coll_path, sizeof g.coll_path, "%s/%s", SCENE_DIR,
             COLL_DEFAULT);
    g.bgm_file[0] = '\0';

    FILE *f = fopen(SCENE_MANIFEST, "r");
    if (!f) return;

    char line[512], name[256];
    float x, y, z, yaw, r;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (sscanf(line, "spawn %f %f %f %f", &x, &y, &z, &yaw) == 4) {
            g.spawn[0]  = x;
            g.spawn[1]  = y;
            g.spawn[2]  = z;
            g.spawn_yaw = yaw;
        } else if (sscanf(line, "collision %255s", name) == 1) {
            snprintf(g.coll_path, sizeof g.coll_path, "%s/%s", SCENE_DIR,
                     name);
        } else if (sscanf(line, "bgm %255s", name) == 1) {
            snprintf(g.bgm_file, sizeof g.bgm_file, "%s", name);
        } else if (sscanf(line, "door %255s %f %f %f %f %f", name,
                          &x, &y, &z, &yaw, &r) == 6) {
            /* Interactive door instance (em_door.c). The manifest is
             * parsed inside the boot task, so the gfx device exists. */
            float p[3] = { x, y, z };
            if (em_door_add(em_frame_gfx(), SCENE_DIR, name, p, yaw, r))
                printf("manifest: door line failed to load: %s", line);
        } else if (sscanf(line, "enemy %255s %f %f %f %f", name,
                          &x, &y, &z, &yaw) == 5) {
            /* Placed enemy instance (em_enemy.c). */
            int kind = strcmp(name, "crawler") == 0 ? EM_ENEMY_KIND_CRAWLER
                     : strcmp(name, "crate") == 0   ? EM_ENEMY_KIND_CRATE
                                                    : -1;
            if (kind < 0) {
                printf("manifest: unknown enemy kind, skipped: %s", line);
            } else {
                float p[3] = { x, y, z };
                if (em_enemy_add_kind(em_frame_gfx(), kind, p, yaw) < 0)
                    printf("manifest: enemy line failed to load: %s", line);
            }
        }
    }
    fclose(f);
    printf("manifest: %s — spawn (%.3f, %.3f, %.3f) yaw %.4f, "
           "collision %s%s%s, %d door(s), %d enem%s\n", SCENE_MANIFEST,
           g.spawn[0], g.spawn[1], g.spawn[2], g.spawn_yaw, g.coll_path,
           g.bgm_file[0] ? ", bgm " : "", g.bgm_file, em_door_count(),
           em_enemy_count(), em_enemy_count() == 1 ? "y" : "ies");
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Load assets/scene EMDL files (alphabetical). Returns the number loaded. */
static int scene_load(EmGfx *gfx, SceneItem *items, int max_items)
{
    DIR *dir = opendir(SCENE_DIR);
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
        snprintf(path, sizeof(path), "%s/%s", SCENE_DIR, names[i]);
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

/* Decode a raw 0x80-centered stick byte to [-1, 1]; full deflection hits
 * exactly +/-1 on both sides (the raw range is asymmetric: 0x00..0xFF). */
static float stick_axis(uint8_t b)
{
    int d = (int)b - 0x80;
    return (d >= 0) ? (float)d / 127.0f : (float)d / 128.0f;
}

/* Compose the player's world placement onto an evaluated anim palette:
 * every bone matrix M becomes T(pos) * R_y(yaw) * M — rotation about Y by
 * the facing yaw, then translation, applied AFTER the animation pose. At
 * yaw = 0 this reduces bit-exactly to the old static translation bake
 * (cos 0 = 1, sin 0 = 0), keeping EM_CAPTURE output stable. */
static void palette_apply_placement(float *pal, uint32_t bone_count,
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

/* The engine's wall response for one frame of motion. The actor spine's
 * movement callers (func_0016EBA0 and family) run func_0019AD00 with
 * mask bit31: probe pos -> target, and on a hit correct x/z back to the
 * hit point. The spine then re-attempts the blocked remainder along the
 * wall — the walkers' FRONT-FACING rule (dot(dir, n) <= -1e-5,
 * func_001A4030/func_0019ED80) makes motion parallel to the hit plane
 * free, so the second probe slides. The exact PS2 iteration count lives
 * in the untranslated spine; one slide pass reproduces the behavior for
 * single-wall contact (player_move_collide below).
 *
 * move_probe_wall: the horizontal probe, reporting only WALL-class hits.
 * Outdoor terrain (the snow scene's grid world) is near-flat but tilted,
 * so the knee-height segment can clip the very ground the player stands
 * on — front-facing by a hair (n.z ~= -0.001) — and a naive block turns
 * into sideways drift along the terrain. The engine's result block
 * carries the surface class (SPR 0x700030CA) for exactly this split:
 * walkable ground (FLOOR/SLOPE) never blocks the actor spine's
 * horizontal motion (the floor query owns it); walls/ceilings do.
 * Walkable crossings are stepped past and the probe re-runs for anything
 * solid beyond them. Returns 1 with *hit staged on the first wall-class
 * hit, else 0. */
static int move_probe_wall_static(const float target[3], EmCollHit *hit)
{
    const unsigned mask = EM_COLL_SET_CELLS | EM_COLL_SET_GRID;
    float from[3] = { g.pos[0], g.pos[1], g.pos[2] };

    for (int i = 0; i < 8; i++) {
        if (!em_collision_move_probe(&g.coll, from, target, mask, hit))
            return 0;
        if (hit->surf_class != EM_SURF_FLOOR &&
            hit->surf_class != EM_SURF_SLOPE)
            return 1;                       /* a real wall (or ceiling) */
        /* Walkable ground — nudge the probe start just past the
         * crossing and look again for solid geometry beyond it. */
        float dx  = target[0] - hit->point[0];
        float dz  = target[2] - hit->point[2];
        float len = sqrtf(dx * dx + dz * dz);
        if (len <= 1e-3f) return 0;         /* crossing at the target */
        from[0] = hit->point[0] + dx / len * 1e-3f;
        from[2] = hit->point[2] + dz / len * 1e-3f;
    }
    return 0;
}

/* The full wall probe: static sets (above) + the MOVABLE-HULL set (mask
 * bit 0) — natively the interactive doors (em_door_probe; a closed or
 * moving door blocks, a fully open one does not). Nearest hit wins,
 * mirroring the engine hub's per-set segment clamping. With no doors the
 * static path is bit-for-bit the old behavior. */
static int move_probe_wall(const float target[3], EmCollHit *hit)
{
    int sres = move_probe_wall_static(target, hit);
    if (!em_door_count())
        return sres;

    EmCollHit dhit;
    const float from[3] = { g.pos[0], g.pos[1], g.pos[2] };
    if (!em_door_probe(from, target, &dhit))
        return sres;
    if (sres) {
        float sd2 = 0.0f, dd2 = 0.0f;
        for (int k = 0; k < 3; k++) {
            float ds = hit->point[k] - from[k];
            float dd = dhit.point[k] - from[k];
            sd2 += ds * ds;
            dd2 += dd * dd;
        }
        if (sd2 <= dd2)
            return 1;          /* the static wall is nearer */
    }
    *hit = dhit;
    return 1;
}

static void player_move_collide(float mx, float mz)
{
    float target[3] = { g.pos[0] + mx, g.pos[1] + WALL_PROBE_LIFT,
                        g.pos[2] + mz };
    EmCollHit hit;

    if (move_probe_wall(target, &hit)) {
        /* Block: correct x/z back to the hit point (the engine's mask-
         * bit31 response) plus the contact skin, then slide — project
         * the blocked remainder onto the wall plane (XZ only — the
         * probe is horizontal) and re-probe once. */
        g.pos[0] = hit.point[0] + hit.normal[0] * WALL_SKIN;
        g.pos[2] = hit.point[2] + hit.normal[2] * WALL_SKIN;
        float rx = target[0] - hit.point[0];
        float rz = target[2] - hit.point[2];
        float nx = hit.normal[0], nz = hit.normal[2];
        float nl = nx * nx + nz * nz;
        if (nl > 1e-8f) {
            float d = (rx * nx + rz * nz) / nl;
            rx -= nx * d;
            rz -= nz * d;
            if (rx * rx + rz * rz > 1e-8f) {
                float slide[3] = { g.pos[0] + rx,
                                   g.pos[1] + WALL_PROBE_LIFT,
                                   g.pos[2] + rz };
                EmCollHit shit;
                if (move_probe_wall(slide, &shit)) {
                    g.pos[0] = shit.point[0] + shit.normal[0] * WALL_SKIN;
                    g.pos[2] = shit.point[2] + shit.normal[2] * WALL_SKIN;
                } else {
                    g.pos[0] = slide[0];
                    g.pos[2] = slide[2];
                }
            }
        }
    } else {
        g.pos[0] = target[0];
        g.pos[2] = target[2];
    }

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
}

/* Player movement (the port's first slice of the actor spine's physics
 * side): left stick = camera-relative walk on the XZ plane; the facing
 * yaw seeks the movement direction at TURN_SPEED (smooth turn). With a
 * collision world loaded, movement goes through the engine's move probe
 * (walls stop/slide, the floor query sets the height); without one, the
 * old room-bbox clamp keeps the repo runnable standalone. */
static void player_move(void)
{
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

    /* INPUT LOCK (door transit, kickoff -> fade-in end): free movement
     * is ignored — the player either walks the scripted MOVE-TO above
     * or stands (at the staging point / the re-placed spawn point). */
    if (em_door_input_locked()) {
        g.move_speed = 0.0f;
        return;
    }

    /* PLANTED AIMING — PORT DECISION (flagged). The armed stance holds
     * position; the stick only TURNS the player in place. Engine
     * evidence (FINDINGS "ANIM ID MAPPING" + "WEAPON SYSTEM"): the
     * armed modes 0x1D..0x20 replace the locomotion modes outright and
     * their tops run no free-move spine; the aim poses are FULL-BODY
     * pose-clip ladders (pitch steps) with NO aim-walk clip family and
     * ZERO footstep-trigger frames in the D_00248C90 property table
     * (walk/run/scripted walks all have them) — the engine never
     * expects the feet to move while aiming, and the live s23 aim
     * capture shows the planted shouldered stance. The turn-in-place
     * is the port stand-in for the engine's aim-yaw steer (the
     * +0x27C blend, <= 0.02/frame, + the mode-0x66 yaw smoothing);
     * AIM_TURN_SPEED is a PORT CONSTANT. */
    if (em_weapon_is_aiming()) {
        g.move_speed = 0.0f;
        const EmFrameInput *ain = em_frame_input();
        float tsx = stick_axis(ain->lx);
        if (fabsf(tsx) > STICK_DEADZONE) {
            g.yaw -= tsx * AIM_TURN_SPEED * FRAME_DT;
            if (g.yaw >  EM_PI) g.yaw -= 2.0f * EM_PI;
            if (g.yaw < -EM_PI) g.yaw += 2.0f * EM_PI;
        }
        return;
    }

    const EmFrameInput *in = em_frame_input();
    float sx  = stick_axis(in->lx);
    float sy  = stick_axis(in->ly);
    float len = sqrtf(sx * sx + sy * sy);
    g.move_speed = 0.0f;
    if (len < STICK_DEADZONE) return;
    if (len > 1.0f) { sx /= len; sy /= len; len = 1.0f; }
    g.move_speed = len * WALK_SPEED;

    /* Camera basis on XZ: forward f points from the eye towards the
     * player, screen-right is f x up = (-fz, 0, fx). Stick up (sy = -1)
     * walks away from the camera. Reads only the camera struct's yaw
     * (+0x44), so the EM_MOVE_TEST trajectory is independent of the eye
     * smoothing. */
    float fx = sinf(g.cam.yaw), fz = cosf(g.cam.yaw);
    float mx = fx * -sy - fz * sx;
    float mz = fz * -sy + fx * sx;

    if (g.coll.poly_count) {
        player_move_collide(mx * WALK_SPEED * FRAME_DT,
                            mz * WALK_SPEED * FRAME_DT);
    } else {
        g.pos[0] += mx * WALK_SPEED * FRAME_DT;
        g.pos[2] += mz * WALK_SPEED * FRAME_DT;
        if (g.pos[0] < kRoomMin[0]) g.pos[0] = kRoomMin[0];
        if (g.pos[0] > kRoomMax[0]) g.pos[0] = kRoomMax[0];
        if (g.pos[2] < kRoomMin[1]) g.pos[2] = kRoomMin[1];
        if (g.pos[2] > kRoomMax[1]) g.pos[2] = kRoomMax[1];
        g.pos[1] = 0.0f;  /* flat floor (no collision world loaded) */
    }

    /* Smooth-turn the facing towards the move direction (shortest arc). */
    float target = atan2f(mx, mz);
    float diff   = target - g.yaw;
    while (diff >  EM_PI) diff -= 2.0f * EM_PI;
    while (diff < -EM_PI) diff += 2.0f * EM_PI;
    float step = TURN_SPEED * FRAME_DT;
    if (diff >  step) diff =  step;
    if (diff < -step) diff = -step;
    g.yaw += diff;
    if (g.yaw >  EM_PI) g.yaw -= 2.0f * EM_PI;
    if (g.yaw < -EM_PI) g.yaw += 2.0f * EM_PI;
}

/* FOOTSTEPS — the native func_00187350 slice (FINDINGS "ANIM ID
 * MAPPING" property table + the s29 live sound capture): when the
 * locomotion clip's cycle position crosses a trigger frame (frameA/
 * frameB of its D_00248C90 row), the engine submits TWO positional
 * sounds back-to-back — a SURFACE pair member + the constant GEAR/cloth
 * layer — and BOTH pairs alternate strictly per step.
 *
 * SURFACE SET — FLAGGED PORT LIMIT: the engine picks the surface pair
 * by floor material (s29 observed storage floor A 0x15/0x16 and floor B
 * 0x1A/0x1B), presumably keyed off the collision hit record's
 * surface-type byte (+0x1A, s22); that per-surface table is NOT located
 * yet, so the port plays set A everywhere. FUTURE HOOK: route the
 * player's floor-probe EmCollHit attribute through here once the
 * surface table is pinned (the decal half of func_00187350 — step
 * decals via func_00187EE0 — is also untranslated). */
static void footstep_play(void)
{
    em_sfx_play(g.step_parity ? EM_SFX_STEP_SURF_A2
                              : EM_SFX_STEP_SURF_A1);
    em_sfx_play(g.step_parity ? EM_SFX_STEP_GEAR_2
                              : EM_SFX_STEP_GEAR_1);
    g.step_parity ^= 1;
}

/* Cyclic edge test: did the looping clip playhead cross `trig` going
 * prev -> cur (both in frames, cur may have wrapped past 0)? */
static int step_crossed(double prev, double cur, double trig)
{
    if (cur == prev) return 0;                    /* standing: frozen */
    if (cur > prev) return prev < trig && trig <= cur;
    return trig > prev || trig <= cur;            /* wrapped the loop */
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
    player_move();
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
        return;
    }

    float target = 0.0f;
    if (g.clip_walk >= 0) {
        target = g.move_speed / WALK_SPEED;
        if (target > 1.0f) target = 1.0f;
        float step = FRAME_DT / ANIM_BLEND_TIME;
        if      (g.walk_w < target - step) g.walk_w += step;
        else if (g.walk_w > target + step) g.walk_w -= step;
        else                               g.walk_w  = target;
        g.walk_t += (double)(FRAME_DT * g.move_speed / WALK_CLIP_SPEED);

        /* FOOTSTEP triggers (footstep_play above): the walk-cycle
         * playhead in clip FRAMES — wrapping exactly like the palette
         * evaluation — against the clip's D_00248C90 trigger frames.
         * The playhead only advances while moving (rate-scaled to the
         * ground speed), so standing is silent and slower walks space
         * their steps out, exactly like the engine's clip-time test. */
        const EmModelClip *cw = &g.model.clips[g.clip_walk];
        double cyc = fmod(g.walk_t * (double)cw->fps,
                          (double)cw->frame_count);
        if (step_crossed(g.step_prev, cyc, (double)WALK_STEP_FRAME_A) ||
            step_crossed(g.step_prev, cyc, (double)WALK_STEP_FRAME_B))
            footstep_play();
        g.step_prev = cyc;
    }

    const EmModelClip *ci = &g.model.clips[g.clip_idle];
    em_model_palette_at(&g.model, (uint32_t)g.clip_idle,
                        g.t * ci->fps, g.player_palette);
    if (g.walk_w > 0.0f) {
        const EmModelClip *cw = &g.model.clips[g.clip_walk];
        em_model_palette_at(&g.model, (uint32_t)g.clip_walk,
                            g.walk_t * cw->fps, g.walk_palette);
        uint32_t n = g.model.bone_count * 16;
        float    w = g.walk_w;
        for (uint32_t i = 0; i < n; i++)
            g.player_palette[i] += (g.walk_palette[i] -
                                    g.player_palette[i]) * w;
    }
    palette_apply_placement(g.player_palette, g.model.bone_count,
                            g.pos, g.yaw);
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
        g.chain[g.chain_len++] = (ChainDraw){ g.scene[i].mesh,
                                              g.scene[i].palette,
                                              g.scene[i].model.bone_count };
    }
    /* Interactive doors (actor draws — func_001BC300's publish). The
     * chain records palette POINTERS; em_door_update (the world-services
     * slot, after this build) writes this frame's pose into them before
     * the close-out flush. */
    for (int i = 0; i < em_door_count(); i++) {
        ChainDraw *cd = &g.chain[g.chain_len++];
        em_door_draw(i, &cd->mesh, &cd->palette, &cd->bone_count);
    }
    /* Enemies (the actor-pool draws). Same pointer contract as the
     * doors: em_enemy_update (after this build) writes this frame's
     * pose before the close-out flush. A dead slot keeps drawing only
     * while em_enemy's death-sink placeholder runs, then drops out. */
    for (int i = 0; i < em_enemy_count(); i++) {
        ChainDraw *cd = &g.chain[g.chain_len];
        if (em_enemy_draw(i, &cd->mesh, &cd->palette, &cd->bone_count))
            g.chain_len++;
    }
    if (g.mesh) {
        g.chain[g.chain_len++] = (ChainDraw){ g.mesh, g.player_palette,
                                              g.model.bone_count };
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

/* func_0018C6A0(src, dst, max) — the engine's HORIZONTAL chase
 * primitive, one axis: clamped proportional step. d = src - dst;
 * |d| <= 1.0 -> quarter-step snap (d/4); else move |d|/6 capped at max.
 * Speed-limited exponential follow — no splines. */
static float cam_chase_h(float dst, float src, float max)
{
    float d = src - dst;
    if (fabsf(d) <= 1.0f) return dst + d * 0.25f;
    float step = fabsf(d) / 6.0f;
    if (step > max) step = max;
    return dst + (d > 0.0f ? step : -step);
}

/* func_0018C4B0(vec, target_y, max) — the VERTICAL twin: divisor 8. */
static float cam_chase_v(float dst, float src, float max)
{
    float d = src - dst;
    if (fabsf(d) <= 1.0f) return dst + d * 0.25f;
    float step = fabsf(d) / 8.0f;
    if (step > max) step = max;
    return dst + (d > 0.0f ? step : -step);
}

/* Desired eye from the struct yaw: CAM_DIST behind the player along the
 * yaw heading, CAM_EYE_HEIGHT above the player's ground Y (the live
 * values: ~33 u back, ~19 u up). */
static void camera_desired_eye(EmCamera *cam)
{
    cam->eye_des[0] = g.pos[0] - sinf(cam->yaw) * CAM_DIST;
    cam->eye_des[1] = g.pos[1] + CAM_EYE_HEIGHT;
    cam->eye_des[2] = g.pos[2] - cosf(cam->yaw) * CAM_DIST;
}

/* func_0018BC20 — mode dispatch (struct byte +0x06 over the cut/smooth
 * jump tables jtbl_0026D950/jtbl_0026D910). Natively only MODE 0 exists:
 * the generic player-relative follow (the smooth-table inline follow).
 * On the PS2, cut-table mode 0 is func_00195130 — the per-AREA camera
 * DIRECTOR, whose per-room logic lives in the area overlays (hardcoded
 * `jal 0x823FE0` hook): the survival-horror fixed/rail room cameras.
 * TODO(camera-modes): translate the overlay directors and handlers 1..15
 * as the overlay code is decompiled — one-shot reposition (5 -> 7),
 * timed hold (6), init/fallback settle (8, func_001914A0), 9..15, and
 * the scope/sniper camera (top-mode 3, func_0022EEF0, zoom 224/x). */
static void camera_mode_dispatch(EmCamera *cam)
{
    /* Port input: d-pad LEFT/RIGHT orbit is a yaw input into the
     * authentic struct (+0x44) — everything downstream consumes only
     * cam->yaw, exactly like an engine mode handler steering it. */
    const EmFrameInput *in = em_frame_input();
    if (in->held & EM_PAD_LEFT)  cam->yaw += CAM_ORBIT_SPEED * FRAME_DT;
    if (in->held & EM_PAD_RIGHT) cam->yaw -= CAM_ORBIT_SPEED * FRAME_DT;

    /* Mode 0 generic follow: desired target chases the player on x/z at
     * <= 0.8 u/frame; y seeks player.y + 15.0 — EXCEPT in the armed
     * stance, where the engine swaps in the camera struct's +0x8C
     * target-height offset (cam->aim_h, default 6.0; the live s23 aim
     * capture ran mode 1 with 2.0 in AREA02) — the documented "AIM
     * CAMERA HOOKUP" of em_weapon.h. The target drops toward the aim
     * line and the unchanged eye height pitches the view down over the
     * shoulder. Settles to exactly player x/z when idle, matching the
     * live capture. */
    cam->tgt_des[0] = cam_chase_h(cam->tgt_des[0], g.pos[0], CAM_TGT_CAP);
    cam->tgt_des[2] = cam_chase_h(cam->tgt_des[2], g.pos[2], CAM_TGT_CAP);
    cam->tgt_des[1] = cam_chase_v(cam->tgt_des[1],
                                  g.pos[1] + (em_weapon_is_aiming()
                                              ? cam->aim_h
                                              : CAM_TGT_HEIGHT),
                                  CAM_TGT_CAP);

    camera_desired_eye(cam);
}

/* Eye pull-in margin: how far in front of the hit plane the solved eye
 * sits, along the blocked sight line. The PS2 solver's exact inset is
 * inside func_0018DD20 (6984 B, unread — FINDINGS confidence "medium");
 * the commit's view position adds another 4.0 * forward away from the
 * wall (CAM_NEAR_PUSH), so a small margin suffices. */
#define CAM_WALL_MARGIN 0.5f

/* func_0018D7B0 (style 0) — the desired-eye solver. Collision-resolves
 * the desired eye against the world: segment queries (the func_0019A910
 * hub — same walkers/eps as the documented func_0019A570 family) from
 * the look target toward the desired eye over collision-set mask 6
 * (static cells + grid; 7 would add movable hulls, which the port has
 * none of yet). A wall between them pulls the eye in front of the hit;
 * the result byte lands in struct +0x07 (cam->hit). Then the actual eye
 * (D_008105D0) smooth-chases the solved desired eye per axis, capped at
 * 4.0 u/frame; the actual target is a straight copy of the desired
 * target (func_0018C0C0). */
static void camera_solve(EmCamera *cam)
{
    float eye_des[3] = { cam->eye_des[0], cam->eye_des[1], cam->eye_des[2] };

    cam->hit = 0;
    if (g.coll.poly_count) {
        EmCollHit hit;
        int kind = em_collision_segment_query(
            &g.coll, cam->tgt_des, eye_des,
            EM_COLL_SET_CELLS | EM_COLL_SET_GRID,  /* solver mask 6 */
            EM_COLL_ID_NONE, &hit);
        if (kind) {
            /* Pull the eye in front of the wall, back toward the
             * target along the blocked sight line. */
            float dx = cam->tgt_des[0] - hit.point[0];
            float dy = cam->tgt_des[1] - hit.point[1];
            float dz = cam->tgt_des[2] - hit.point[2];
            float dl = sqrtf(dx * dx + dy * dy + dz * dz);
            if (dl > 1e-3f) {
                float s = CAM_WALL_MARGIN / dl;
                if (s > 1.0f) s = 1.0f;
                dx *= s; dy *= s; dz *= s;
            } else {
                dx = dy = dz = 0.0f;
            }
            eye_des[0] = hit.point[0] + dx;
            eye_des[1] = hit.point[1] + dy;
            eye_des[2] = hit.point[2] + dz;
            cam->hit = (uint8_t)kind;          /* struct +0x07 */
        }
    }
    cam->eye[0] = cam_chase_h(cam->eye[0], eye_des[0], CAM_EYE_CAP);
    cam->eye[2] = cam_chase_h(cam->eye[2], eye_des[2], CAM_EYE_CAP);
    cam->eye[1] = cam_chase_v(cam->eye[1], eye_des[1], CAM_EYE_CAP);
    cam->tgt[0] = cam->tgt_des[0];
    cam->tgt[1] = cam->tgt_des[1];
    cam->tgt[2] = cam->tgt_des[2];
}

/* func_0018C0D0(cam, 1) — the per-frame COMMIT. Engine steps:
 *   1. fwd = normalize(target - eye), degenerate-guarded;
 *   2. view position = eye + 4.0*fwd (near push; mode 0xA uses -1.0);
 *   3. func_00102CD0 look-at with up = D_008105F0 = (0,-1,0) — see
 *      em_mat4_lookat_gs for the Y-down/handedness reconciliation;
 *   4. P from zoom s, K = P*V -> every draw's matrix slot 0 (M = K*W).
 * Step 4's projection here is still the port's em_mat4_perspective; the
 * engine's GS values are pinned in the ENGINE_CAM_* constants above. */
static void camera_commit(EmCamera *cam)
{
    float dx  = cam->tgt[0] - cam->eye[0];
    float dy  = cam->tgt[1] - cam->eye[1];
    float dz  = cam->tgt[2] - cam->eye[2];
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len > 1e-3f) {  /* degenerate guard: keep the last forward */
        cam->fwd[0] = dx / len;
        cam->fwd[1] = dy / len;
        cam->fwd[2] = dz / len;
    }

    float pos[3] = { cam->eye[0] + CAM_NEAR_PUSH * cam->fwd[0],
                     cam->eye[1] + CAM_NEAR_PUSH * cam->fwd[1],
                     cam->eye[2] + CAM_NEAR_PUSH * cam->fwd[2] };
    em_mat4_lookat_gs(cam->view, pos, cam->fwd, cam->up);

    int dw, dh;
    em_window_drawable_size(em_frame_window(), &dw, &dh);
    float aspect   = (dh > 0) ? (float)dw / (float)dh : 4.0f / 3.0f;
    float far_clip = g.n_scene ? 800.0f : 500.0f;

    float proj[16];
    em_mat4_perspective(proj, 50.0f * EM_PI / 180.0f, aspect,
                        0.5f, far_clip);
    em_mat4_mul(g.viewproj, proj, cam->view);
}

/* func_001CB590(0x008101E0, 0xD0, 0) + func_0018B9C0 — camera-context
 * begin + the camera state machine top. Runs AFTER the render-chain
 * build, like the engine; on the PS2 the already-kicked chain consumes
 * the PREVIOUS frame's matrices, while the native chain is flushed at
 * close-out with this frame's — one frame less camera latency, same
 * 60 Hz math. */
static void camera_update(void)
{
    EmCamera *cam = &g.cam;

    if (cam->state == 0) {
        /* State 0 — one-shot init. Engine: vector pool setup with
         * up = (0,-1,0), then mode 8 (reposition/settle vs the world,
         * two func_0019A910 ray queries). The settle needs the collision
         * world (TODO above), so natively: snap actual = desired and
         * start the generic follow directly in mode 0. */
        cam->up[0]     = 0.0f;
        cam->up[1]     = -1.0f;  /* the engine's Y-DOWN view-up */
        cam->up[2]     = 0.0f;
        cam->top_mode  = 0;
        cam->table_sel = 1;      /* smooth dispatch table */
        cam->mode      = 0;      /* engine inits mode 8 — TODO(camera-modes) */
        cam->aim_h     = CAM_AIM_OFFSET;
        cam->tgt_des[0] = g.pos[0];
        cam->tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
        cam->tgt_des[2] = g.pos[2];
        camera_desired_eye(cam);
        memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
        memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
        cam->state     = 1;
        cam->sub_state = 0;
        /* fall through — the engine's init frame still commits */
    }
    if (cam->sub_state == 0) {   /* first run frame: 0->1 ramp */
        cam->sub_state = 1;
        cam->timer     = 0;
    }

    /* CAMERA FREEZE during the door-transit input lock: dispatch + solve
     * are skipped (no orbit input, no chase) and only the commit runs —
     * the engine's frozen top modes 1/2 shape. The post-warp re-seat
     * happens while the screen is black (gameplay_frame). */
    if (cam->top_mode == 0 && !em_door_input_locked()) {
        /* func_00191390 leaf pre-step — no native work yet. */
        camera_mode_dispatch(cam);   /* func_0018BC20 */
        camera_solve(cam);           /* func_0018D7B0, style 0 */
    }
    /* top modes 1/2 (frozen) reach the commit only; top mode 3 is the
     * scope camera func_0022EEF0 — TODO(camera-modes). */
    camera_commit(cam);              /* func_0018C0D0(cam, 1) */
    cam->timer++;
}

/* func_001CB5A0 / func_001AAD00 / func_001D1EA0(1) — close-out: flush the
 * recorded chain with the camera block applied (the native "kick"), then
 * advance clip time. EM_CAPTURE instrumentation lives here so its frame
 * counting matches the rendered gameplay frames. */
static void frame_close_out(void)
{
    EmGfx *gfx = em_frame_gfx();

    if (g.chain_test_triangle) {
        em_gfx_draw_test_triangle(gfx);
    } else {
        for (int i = 0; i < g.chain_len; i++) {
            em_gfx_draw_skinned(gfx, g.chain[i].mesh, g.viewproj,
                                g.chain[i].palette, g.chain[i].bone_count);
        }
    }

    /* Weapon feedback overlays (crosshair / muzzle-flash placeholders —
     * em_weapon.h "VISUAL FEEDBACK"); queues nothing while holstered, so
     * the default frame stays byte-identical. */
    em_weapon_render(gfx);

    /* STATUS SCREEN over the flushed 3D frame (the engine's GS-sprite
     * status overlay; em_hud queues overlay rects, em_gfx_end_frame
     * draws them last). HIDDEN by default — the original shows no
     * persistent HUD — and toggled by a TRIANGLE press (edge); shown, it
     * dims the scene behind it. Gameplay keeps running underneath
     * (stated assumption — the live captures show no hard pause).
     * EM_HUD_FORCE=1 forces it visible for overlay tests. The ammo
     * readout is LIVE: mag/reserve mirror the weapon state (D_00810C62 /
     * D_00810CB4) every frame, exactly like the engine UI re-reading the
     * globals. */
    em_hud_update(em_frame_input());
    g.status.mag     = em_weapon_mag();
    g.status.reserve = em_weapon_reserve();
    em_hud_render(gfx, &g.status);

    /* SCREEN FADE — the step-D machine's level, drawn last so it covers
     * the scene AND the status screen (the engine's fade owns the whole
     * GS frame). Level 0 queues nothing: the default frame stays
     * byte-identical. */
    {
        float lvl = em_frame_fade_level();
        if (lvl > 0.0f) {
            const float black[4] = { 0.0f, 0.0f, 0.0f, lvl };
            em_gfx_overlay_rect(gfx, 0.0f, 0.0f, EM_GFX_OVERLAY_W,
                                EM_GFX_OVERLAY_H, black);
        }
    }

    if (g.capture_path && g.frame_no == g.capture_frame)
        em_gfx_request_capture(gfx, g.capture_path);

    g.t += 1.0 / 60.0;
    g.frame_no++;
    /* A scripted self-test owns the quit when combined with a capture,
     * so a mid-script capture doesn't cut the script short. */
    if (g.capture_path && !g.move_test && !g.weapon_test && !g.door_test &&
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
 * The forward leg is a WALL TEST: 60 frames * 0.25 u = 15 u of motion,
 * but the office collision world has a wall n-gon at z = -170 (grid poly
 * with plane n = (0,0,-1), d = 170 — 14 u ahead of the spawn), so with
 * collision loaded the move probe must stop the walk on the plane (plus
 * the WALL_SKIN contact offset, inside the 0.05 tolerance) and the slide
 * pass must add no lateral drift. The right leg then slides free along
 * that wall (motion parallel to the plane fails the walkers'
 * front-facing test, so it never re-hits):
 *   collision world:  (99.900, 0.000, -170.000), yaw -pi/2
 *   bbox fallback:    (99.900, 0.000, -169.000), yaw -pi/2
 * Those built-in expectations (and the 60/30-frame legs) are the OFFICE
 * scene's; for other scenes (manifest spawns) EM_MOVE_LEGS=fwd,strafe
 * resizes the two legs to reach that scene's wall and EM_MOVE_EXPECT=
 * x,y,z overrides the expected final position (the yaw expectation,
 * -pi/2, is scene-independent). */
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
        float ex = 99.9f, ey = 0.0f;
        float ez = g.coll.poly_count ? -170.0f : -169.0f;
        if (g.move_expect_set) {
            ex = g.move_expect[0];
            ey = g.move_expect[1];
            ez = g.move_expect[2];
        }
        const float tol  = 0.05f;  /* +- slide/fp drift allowance */
        int ok = fabsf(g.pos[0] - ex)           <= tol &&
                 fabsf(g.pos[1] - ey)           <= tol &&
                 fabsf(g.pos[2] - ez)           <= tol &&
                 fabsf(g.yaw + EM_PI * 0.5f)    <= 0.01f;
        printf("move test: pos (%.3f, %.3f, %.3f) yaw %.4f rad — "
               "expected (%.3f, %.3f, %.3f)%s: %s\n",
               g.pos[0], g.pos[1], g.pos[2], g.yaw, ex, ey, ez,
               g.coll.poly_count ? " [wall stop]" : " [bbox clamp]",
               ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* EM_DOOR_TEST=1 — deterministic door-transit self-test. Spawns the
 * player on the z = -225 corridor line facing the WEST double door at
 * (57, 0, -220.5) (placement-table record [5], AREA02 state 1) and
 * exercises the FULL s22 transit sequence end to end through the real
 * input API:
 *
 *   frames  1..24   walk -X to x ~= 66 (inside the 12 u use-scan radius,
 *                   outside the 2 u auto-open ring; no button — the door
 *                   must stay CLOSED)
 *   frames 29..58   keep walking -X. The doorway is statically SEALED by
 *                   the grid room-boundary plane at x = 60 (the engine's
 *                   sealed-room-box world): free movement must BLOCK at
 *                   x ~= 60.01 (tracked as dt_min_x while CLOSED) — the
 *                   "previously blocked plane".
 *   frame   60      CROSS press -> the use scan arms the door (dist 5.4,
 *                   facing-dot 0.56); assert state == OPENING soon after.
 *                   The kickoff LOCKS input, latches the side (the test
 *                   approach is the BACK side: bearing(player - door)
 *                   is pi off the door yaw) and WALKS the player to the
 *                   staging point (62, 0, -220.5) = door + 5*n on his
 *                   own side (arrival ~frame 80; the walk keeps the
 *                   locomotion walk anim).
 *   ~frame  80      OPEN phase: the door script chain fires on arrival
 *                   — scripted player anim 0x43 (door-open BACK, rate
 *                   1.0) replaces locomotion, door sound + clip start;
 *                   the script waits 70 frames (back-side op 0x02).
 *   frame  100      assert em_door_input_locked() == 1 (mid-transit)
 *                   AND em_game_anim_active() == 0x43 (the reach-out
 *                   pose, NOT the walk);
 *   100..140        hold forward ('w') — locked input must NOT move the
 *                   player off the staging point (checked at 145).
 *   ~frame 150      commit (arrival ~80 + 70-frame wait) -> 64-frame
 *                   fade-out (peak level tracked); at black: re-place
 *                   at the spawn point (52, 0, -220.5) = door - 5*n,
 *                   exit yaw -pi/2, scripted anim CANCELLED (script
 *                   teardown); door closes; 64-frame fade-in; unlock at
 *                   fade-in end.
 *   frame  340      assert: trigger OK, blocked min x >= 59.9, lock
 *                   seen, locked input dead, scripted anim seen at 100
 *                   and idle (0) again at the end, fade reached 1.0 and
 *                   is back at 0, input UNLOCKED, final pos (52,
 *                   -220.5) +- tol with yaw -pi/2 (behind the door,
 *                   exit pose), door re-armed CLOSED.
 *
 * (Geometry verified against the office EMCL: corridor floor along the
 * whole approach, boundary wall at x = 60, no other static blocker.) */
static void door_test_script(void)
{
    int n = g.frame_no;
    if (n == 0) {
        g.dt_door         = -1;
        g.dt_min_x        = 1e9f;
        g.dt_max_fade     = 0.0f;
        g.dt_ok_trigger   = 0;
        g.dt_ok_lock      = 0;
        g.dt_ok_inputdead = 0;
        g.dt_ok_anim      = 0;
        move_test_inject('w', 1);
    } else if (n == 24) {
        move_test_inject('w', 0);
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
        move_test_inject('w', 1);   /* push into the sealed doorway */
    } else if (n == 58) {
        move_test_inject('w', 0);   /* ~6 frames pinned on the boundary */
    } else if (n == 60) {
        move_test_inject('k', 1);   /* CROSS — the use-scan trigger */
    } else if (n == 61) {
        move_test_inject('k', 0);
    } else if (n == 64) {
        g.dt_ok_trigger = g.dt_door >= 0 &&
                          em_door_state(g.dt_door) == EM_DOOR_OPENING;
    } else if (n == 100) {
        g.dt_ok_lock = em_door_input_locked();
        /* Mid-open: the scripted door anim must own the player — the
         * BACK-side clip 0x43 committed (+0x20C analog), not the walk. */
        g.dt_ok_anim = em_game_anim_active() == 0x43;
        move_test_inject('w', 1);   /* locked: must be ignored */
    } else if (n == 140) {
        move_test_inject('w', 0);
    } else if (n == 145) {
        /* 40 frames of held forward under the lock: still parked on the
         * staging point (62, 0, -220.5). */
        g.dt_ok_inputdead = fabsf(g.pos[0] - 62.0f)   <= 0.35f &&
                            fabsf(g.pos[2] + 220.5f) <= 0.35f;
    } else if (n == 340) {
        int ok_closed = g.dt_door >= 0 &&
                        em_door_state(g.dt_door) == EM_DOOR_CLOSED;
        int ok_block  = g.dt_min_x >= 59.9f && g.dt_min_x < 61.0f;
        int ok_fade   = g.dt_max_fade >= 0.999f &&
                        em_frame_fade_level() <= 0.001f;
        int ok_unlock = !em_door_input_locked();
        int ok_animend = em_game_anim_active() == 0;  /* teardown reset */
        int ok_pass   = fabsf(g.pos[0] - 52.0f)   <= 0.1f &&
                        fabsf(g.pos[2] + 220.5f)  <= 0.6f &&
                        fabsf(g.yaw + EM_PI * 0.5f) <= 0.01f;
        int ok = g.dt_ok_trigger && g.dt_ok_lock && g.dt_ok_inputdead &&
                 g.dt_ok_anim && ok_animend &&
                 ok_fade && ok_unlock && ok_pass && ok_block && ok_closed;
        printf("door test: trigger->OPENING %s, blocked min x %.3f while "
               "closed (boundary 60.0: %s), input locked mid-transit %s, "
               "locked stick ignored %s, scripted anim 0x43 mid-open %s / "
               "reset at end %s, fade peak %.3f / final %.3f: %s, "
               "unlocked at end %s, final pos (%.3f, %.3f, %.3f) yaw "
               "%.4f behind the door: %s, door state %d (CLOSED %d, "
               "re-armed): %s — %s\n",
               g.dt_ok_trigger ? "ok" : "FAILED", g.dt_min_x,
               ok_block ? "ok" : "FAILED",
               g.dt_ok_lock ? "ok" : "FAILED",
               g.dt_ok_inputdead ? "ok" : "FAILED",
               g.dt_ok_anim ? "ok" : "FAILED",
               ok_animend ? "ok" : "FAILED",
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
 *   A/A+8/A+16/A+24  engine's default-config fire button, s29) at
 *                    8-frame spacing: each shot decrements mag AND
 *                    reserve (TOTAL-pool rule) -> 4/120 .. 0/116
 *   A+32             5th press on the EMPTY mag: must NOT fire — the
 *                    mode-0 auto-reload triggers instead (func_0017B300:
 *                    mag = min(30, 116) = 30, reserve UNTOUCHED at 116;
 *                    anim 0x33 holds the RELOAD state for its length)
 *   B = A+34+rld+8   press after the reload window: 5th real shot
 *                    -> 29/115
 *   B+10..B+41       FULL-AUTO stretch: mode 2, L held 31 frames ->
 *                    shots at the 6-frame cadence (+2/frame vs interval
 *                    12) = 6 rounds -> 23/109
 *   C = B+50         manual top-up (R = L3, the engine's raw reload
 *                    bit) -> 30/109, reserve again untouched
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
        B = A + 34 + em_weapon_reload_ticks() + 8;
        C = B + 50;
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
    } else if (n == A + 2 || n == A + 10 || n == A + 18 || n == A + 26) {
        move_test_inject('l', 0);
    } else if (n == A + 6) {
        wt_check(em_weapon_mag() == 3 &&
                 em_weapon_reserve() == 119 &&
                 em_weapon_shots() == 1,
                 "shot 1: 4/120 -> 3/119");
        int rs = wt_recoil_state();
        wt_check(rs == 1 || rs == -1,
                 "recoil replay mid-flight after shot 1 (0x112 rewound)");
    } else if (n == A + 8 || n == A + 16 || n == A + 24) {
        move_test_inject('l', 1);                           /* semi 2..4 */
    } else if (n == A + 30) {
        wt_check(em_weapon_mag() == 0 &&
                 em_weapon_reserve() == 116 &&
                 em_weapon_shots() == 4,
                 "shots 2-4: mag empty at 0/116");
    } else if (n == A + 32) {
        move_test_inject('l', 1);                           /* dry press */
    } else if (n == A + 34) {
        move_test_inject('l', 0);
    } else if (n == A + 38) {
        wt_check(em_weapon_state() == EM_WPN_RELOAD &&
                 em_weapon_mag() == 30 &&
                 em_weapon_reserve() == 116 &&
                 em_weapon_shots() == 4 &&
                 em_weapon_reloads() == 1,
                 "empty-mag press auto-reloads: mag = min(30, "
                 "reserve) = 30, reserve untouched (116), no "
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
    } else if (n == B + 10) {
        em_weapon_set_fire_mode(EM_WPN_MODE_AUTO);
        move_test_inject('l', 1);                           /* auto hold */
    } else if (n == B + 41) {
        move_test_inject('l', 0);
    } else if (n == B + 46) {
        wt_check(em_weapon_mag() == 23 &&
                 em_weapon_reserve() == 109 &&
                 em_weapon_shots() == 11,
                 "auto 31-frame hold = 6 rounds: 23/109");
    } else if (n == C) {
        move_test_inject('r', 1);                           /* manual L3 */
    } else if (n == C + 2) {
        move_test_inject('r', 0);
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

/* EM_ENEMY_TEST — deterministic crawler-vs-player self-tests (em_enemy.c).
 * Both runs spawn ONE crawler 30 units ahead of the player spawn along
 * the spawn facing (scene-init arm, et_spawned) and adapt to the
 * crawler's actual pace (steer pauses vary with the scene's collision
 * world) instead of fixed frame numbers:
 *
 *   EM_ENEMY_TEST=1 (kill run): hold R1 from frame 0 (draw). Assert the
 *     crawler WAKES (the 32-u distance sense covers the 30-u spawn) and
 *     CLOSES distance; once it is within 12 u (inside the office's wall
 *     plane 14 u ahead of the spawn, so the world ray cannot outrank
 *     the victim test) fire ONE semi shot -> +0x36 mailbox, HP 1 =
 *     one-shot kill -> assert death/despawn, exactly one round spent,
 *     ray resolved HIT, and the player's health untouched.
 *   EM_ENEMY_TEST=2 (contact run): weapon stays holstered; let the
 *     crawler reach the player. The radius-6 lunge writes the player
 *     mailbox (0x400A -> amount 10) and the crawler bursts (the
 *     suicide-attack path) -> assert health dropped by exactly 10 and
 *     the crawler despawned.
 *   EM_ENEMY_TEST=3 (crate run): spawn a DISGUISED CRATE 25 units ahead
 *     instead (em_enemy.h "CRATE KIND"); weapon stays holstered, walk
 *     forward (W) at it. Assert the crate BURSTS at the ~10-u proximity
 *     trigger with no shot fired, that the WORM spawned at the crate
 *     position (the burst's gibs occupy virtual draw slots), and that
 *     the worm wakes (ATTACK) and CLOSES distance on the now-standing
 *     player. (The worm may finish its suicide lunge inside the
 *     window — health/alive are reported, not asserted.) */
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
    if (n == 20 && g.enemy_test != 3)   /* the disguised crate stays IDLE */
        et_check(em_enemy_state(0) == EM_ENEMY_ATTACK,
                 "crawler awake (ATTACK) by frame 20");

    if (g.enemy_test == 1) {
        if (!g.et_fired) {
            if (n > 20 && et_dist() <= 12.0f) {
                et_check(et_dist() < g.et_d0 - 5.0f,
                         "closed distance before the shot");
                et_check(em_weapon_state() == EM_WPN_AIM,
                         "rifle drawn (AIM) at fire time");
                move_test_inject('l', 1);   /* CIRCLE — one semi shot */
                g.et_fired = n;
            } else if (n >= 400) {
                et_check(0, "crawler closed to 12 u by frame 400");
                et_finish("kill run");
            }
        } else if (n == g.et_fired + 2) {
            move_test_inject('l', 0);
        } else if (n == g.et_fired + 15) {
            et_check(em_weapon_shots() == 1, "exactly one round fired");
            et_check(em_weapon_last_hit() == 1, "the shot resolved HIT");
            et_check(em_enemy_alive() == 0 &&
                     em_enemy_state(0) == EM_ENEMY_FREE,
                     "crawler dead + despawned (mailbox one-shot kill)");
            et_check(g.status.health == g.et_health0,
                     "player health untouched");
            et_finish("kill run");
        }
    } else if (g.enemy_test == 3) {
        if (!g.et_burst_frame) {
            if (em_enemy_state(0) == EM_ENEMY_FREE) {
                /* the crate burst this frame (slot 0 freed) */
                g.et_burst_frame = n;
                g.et_bd          = et_dist();
                move_test_inject('w', 0);    /* stop: the worm comes */
                et_check(g.et_bd <= 10.5f && g.et_bd >= 8.0f,
                         "burst at the ~10-u proximity trigger");
                et_check(em_weapon_shots() == 0,
                         "no shot fired (proximity, not damage)");
                et_check(em_enemy_alive() == 1 &&
                         em_enemy_kind(1) == EM_ENEMY_KIND_CRAWLER,
                         "worm spawned by the burst");
                float cp[3] = { 0, 0, 0 }, wp[3] = { 0, 0, 0 };
                em_enemy_pos(0, cp);
                em_enemy_pos(1, wp);
                et_check(fabsf(wp[0] - cp[0]) + fabsf(wp[1] - cp[1]) +
                         fabsf(wp[2] - cp[2]) < 0.01f,
                         "worm emerged at the crate position");
                g.et_wd0 = g.et_wd_min = et_dist_i(1);
            } else if (n >= 600) {
                et_check(0, "crate burst by frame 600");
                et_finish("crate run");
            }
        } else {
            float wd = et_dist_i(1);
            if (wd < g.et_wd_min) g.et_wd_min = wd;
            if (em_enemy_state(1) == EM_ENEMY_ATTACK)
                g.et_worm_atk = 1;   /* sampled: it may lunge-burst
                                      * before a fixed checkpoint */
            if (n == g.et_burst_frame + 120) {
                et_check(g.et_worm_atk,
                         "worm woke (ATTACK) after the burst");
                et_check(g.et_wd_min <= g.et_wd0 - 3.0f,
                         "worm closed distance on the player");
                printf("enemy test (crate run): burst dist %.1f, worm "
                       "dist %.1f -> min %.1f, draw slots %d\n",
                       g.et_bd, g.et_wd0, g.et_wd_min, em_enemy_count());
                et_finish("crate run");
            }
        }
    } else {
        if (!g.et_hit_frame) {
            if (g.status.health < g.et_health0) {
                g.et_hit_frame = n;
            } else if (n >= 600) {
                et_check(0, "crawler reached the player by frame 600");
                et_finish("contact run");
            }
        } else if (n == g.et_hit_frame + 5) {
            et_check(g.status.health == g.et_health0 - 10.0f,
                     "lunge dealt the 0x400A amount (10)");
            et_check(em_enemy_alive() == 0,
                     "crawler burst on the lunge (suicide path)");
            et_finish("contact run");
        }
    }
}

/* EM_SFX_TEST=1 — one-shot SFX mixer self-test (em_sfx.c). Fires THREE
 * one-shots 10 frames (1/6 s) apart — weapon draw 0x162, fire 0x164,
 * enemy death 0x7D8, all mapped by the user's assets/sfx/sfx.txt — so
 * their voices OVERLAP in the shared render callback (over the BGM when
 * EM_BGM / the manifest started one), then prints the audio thread's
 * mixed-voice counters and quits. Needs the registry: without sfx.txt
 * the module is disabled by design and the test reports the FAIL. */
static void sfx_test_script(void)
{
    int n = g.frame_no;
    if (n == 10 || n == 20 || n == 30) {
        static const unsigned ids[3] = { EM_SFX_WPN_DRAW, EM_SFX_WPN_FIRE,
                                         EM_SFX_ENEMY_DEATH };
        em_sfx_play(ids[n / 10 - 1]);
    } else if (n == 120) {
        long mixed = em_sfx_frames_mixed();
        int  peak  = em_sfx_max_concurrent();
        int  ok    = em_sfx_sound_count() > 0 && em_sfx_plays() == 3 &&
                     em_sfx_drops() == 0 && mixed > 0 && peak >= 2;
        printf("sfx test: %d sound(s) loaded, %d play(s) (%d dropped), "
               "%ld voice frames mixed, peak %d concurrent voice(s) — "
               "%s\n", em_sfx_sound_count(), em_sfx_plays(),
               em_sfx_drops(), mixed, peak, ok ? "PASS" : "FAIL");
        fflush(stdout);
        em_frame_request_quit();
    }
}

/* func_001AE5E0 — THE GAMEPLAY FRAME (stage order is the engine's). */
static void gameplay_frame(void)
{
    if (g.move_test) move_test_script();    /* debug instrumentation only */
    if (g.door_test) door_test_script();    /* debug instrumentation only */
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
    if (g.capture_aim) {
        if      (g.frame_no == 0)  move_test_inject('e', 1);
        else if (g.frame_no == 28) move_test_inject('a', 1);
        else if (g.frame_no == 44) move_test_inject('a', 0);
        if (g.capture_aim >= 2) {
            if      (g.frame_no == 57) move_test_inject('l', 1);
            else if (g.frame_no == 59) move_test_inject('l', 0);
        }
    }                                       /* debug instrumentation only */
    if (g.enemy_test) enemy_test_script();  /* debug instrumentation only */
    if (g.sfx_test)  sfx_test_script();     /* debug instrumentation only */
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
            g.cam.yaw = wyaw;
            g.cam.tgt_des[0] = g.pos[0];
            g.cam.tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
            g.cam.tgt_des[2] = g.pos[2];
            camera_desired_eye(&g.cam);
            memcpy(g.cam.eye, g.cam.eye_des, sizeof g.cam.eye);
            memcpy(g.cam.tgt, g.cam.tgt_des, sizeof g.cam.tgt);
        }
    }
    /* ENEMIES: the crawler state machines (func_001551B0 — also part of
     * the actor-pool tick). Runs BEFORE the weapon update so this
     * frame's shot resolves against current positions. */
    em_enemy_update(&g.coll, g.pos);
    /* Player-side damage mailbox (the crawler lunge writes it with the
     * actor +0x36 code layout: low bits = amount, high = type flags).
     * The engine's own player-damage path (latch byte D_008102BF +
     * drain magnitude D_008104D4) is untranslated; consuming the
     * mailbox into the status health is the port stand-in. */
    {
        int hitcode = em_enemy_player_hit_take();
        if (hitcode) {
            g.status.health -= (float)(hitcode & 0x1FFF);
            if (g.status.health < 0.0f) g.status.health = 0.0f;
        }
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
        em_weapon_update(&g.coll, g.pos, g.yaw,
                         em_door_input_locked() ? &kNeutral
                                                : em_frame_input());
    }
    camera_update();         /* func_001CB590(0x008101E0, 0xD0, 0) +
                              * func_0018B9C0 camera state machine    */
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
            g.t              = 0.0;
            g.walk_t         = 0.0;
            g.walk_w         = 0.0f;
            g.step_prev      = 0.0;   /* footstep edge state re-armed */
            g.step_parity    = 0;
            g.frame_no       = 0;
            g.frame_selector = 0;
            g.sa_req         = 0;     /* scripted-anim mailbox cleared */
            g.sa_cur         = 0;     /* (player anim re-init state)   */
            g.sa_clip        = -1;
            g.sa_req_hold    = 0;
            g.sa_hold        = 0;
            g.pos[0] = g.n_scene ? g.spawn[0] : 0.0f;
            g.pos[1] = g.n_scene ? g.spawn[1] : 0.0f;
            g.pos[2] = g.n_scene ? g.spawn[2] : 0.0f;
            g.yaw    = g.n_scene ? g.spawn_yaw : 0.0f;
            if (g.door_test) {
                /* EM_DOOR_TEST spawn: the z = -225 corridor line in
                 * front of the west double door, facing -X (see
                 * door_test_script). */
                g.pos[0] = 72.0f;
                g.pos[1] = 0.0f;
                g.pos[2] = -225.0f;
                g.yaw    = -EM_PI * 0.5f;
            }
            memset(&g.cam, 0, sizeof g.cam);
            g.cam.yaw = g.yaw;   /* chase camera starts behind the spawn */
            /* Weapon context init (the engine's HUD/weapon-context arm):
             * holstered, ammo from the demo status (live test save:
             * mag 4, reserve 120). The HUD mirrors the weapon live from
             * here on (frame_close_out). */
            em_weapon_reset(g.status.mag, g.status.reserve);
            /* EM_ENEMY_TEST spawn: tests 1/2 place one crawler 30
             * units ahead of the player spawn along the spawn facing,
             * turned to face the player; test 3 places a DISGUISED
             * CRATE 25 units ahead instead (see enemy_test_script). */
            if (g.enemy_test && !g.et_spawned) {
                g.et_spawned = 1;
                int   ek = g.enemy_test == 3 ? EM_ENEMY_KIND_CRATE
                                             : EM_ENEMY_KIND_CRAWLER;
                float ed = g.enemy_test == 3 ? 25.0f : 30.0f;
                /* Crate run only: the office spawn faces a wall plane
                 * 14 u ahead (see the kill-run wall note), so a walking
                 * player could never reach the 10-u proximity trigger
                 * of a crate 25 u beyond it. About-face the spawn pose
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
                if (em_enemy_add_kind(em_frame_gfx(), ek, ep,
                                      g.yaw + EM_PI) < 0)
                    printf("enemy test: spawn failed\n");
            }
            self->user[GAME_BYTE_FRAME] = 1;
            /* fall through — the engine's init frame still renders */
        case 1:
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
        /* Resolve the named clips; an old single-clip (EMD2) asset keeps
         * exactly the previous behavior: idle = clip 0, no crossfade. */
        g.clip_idle = em_model_clip_index(&g.model, CLIP_ID_IDLE);
        if (g.clip_idle < 0) g.clip_idle = 0;
        g.clip_walk = em_model_clip_index(&g.model, CLIP_ID_WALK);
        if (g.clip_walk == g.clip_idle) g.clip_walk = -1;
        printf("clips: idle #%d (id %u)%s\n", g.clip_idle,
               g.model.clips[g.clip_idle].id,
               g.clip_walk >= 0 ? ", walk found — idle<->walk crossfade on"
                                : " only — crossfade off (re-export with "
                                  "--clips 346,2,3,69,67,75)");
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
    g.capture_aim  = ca ? atoi(ca) : 0;     /* 1 = aim, 2 = aim + shot */
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
    const char *wt = getenv("EM_WEAPON_TEST");
    g.weapon_test  = wt && wt[0] == '1';
    const char *et = getenv("EM_ENEMY_TEST");
    if (et && et[0] >= '1' && et[0] <= '3')
        g.enemy_test = et[0] - '0';
    const char *st = getenv("EM_SFX_TEST");
    g.sfx_test     = st && st[0] == '1';
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
    em_door_shutdown(gfx);
    em_enemy_shutdown(gfx);
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
