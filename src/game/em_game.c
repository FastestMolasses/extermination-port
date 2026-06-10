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
 *   func_001C1D00 camera apply           camera_apply() — computes the
 *                  (block 0x008101D0)    follow/chase camera into the
 *                                        camera block (view-projection
 *                                        matrix).
 *   func_001AFD70 / func_0015C160 /      world services — skeleton no-ops.
 *   func_001F0360
 *   func_001CB590(HUD ctx) /             HUD context + view-target —
 *   func_0018B9C0 view-target            skeleton no-ops.
 *   func_001CB5A0 / func_001AAD00 /      frame_close_out() — flushes the
 *   func_001D1EA0(1) close-out           draw chain with the applied
 *                                        camera and advances clip time.
 *
 * INTERACTIVE MOVEMENT (first slice of the real actor spine): the frame
 * input block drives the player around the room. Left stick (WASD) walks
 * camera-relative on the XZ plane; the facing yaw seeks the movement
 * direction (smooth turn). The placement is composed onto the evaluated
 * anim palette each frame (rotation about Y by yaw, then translation —
 * AFTER the animation pose; see palette_apply_placement). The camera is a
 * lerped chase camera behind the character; d-pad (arrow keys) LEFT/RIGHT
 * orbits it around the player. Esc still quits (em_frame.c step C).
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
#include "game/em_frame.h"
#include "game/em_task.h"

#define MODEL_PATH "assets/player.emdl"
#define SCENE_DIR  "assets/scene"
#define SCENE_MAX  16

/* Player spawn placement in the office room (chunk06.n1 level): the live
 * GS-dump capture has the character standing at ~(107.4, 0, -184); the
 * level floor there is y = 0 and the player EMDL is recentred at the
 * origin with its feet at y ~= 0. */
static const float kPlayerPos[3] = { 107.4f, 0.0f, -184.0f };

/* Walkable bounds: the office room's collision bbox (the id 0x44 file —
 * FINDINGS.md "COLLISION WORLD", chunk06.n1: X[-3.6,120.5] Z[-296,2.4],
 * floor y = 0).
 * TODO(collision): replace this clamp with the documented query API once
 * the level_world cluster is translated — func_0019AD00 (actor move-probe
 * with collide-and-slide response) / func_0019A570 (segment query) over
 * the id 0x44 cell n-gons + s16-grid heightfield. */
static const float kRoomMin[2] = { -3.6f,  -296.0f };  /* x, z */
static const float kRoomMax[2] = { 120.5f,    2.4f };

/* Movement / camera tuning. The character is ~15 units tall; roughly one
 * body height per second reads as a natural walk at room scale (the room
 * spans ~124 x ~298 units). The loop is vsync-locked at 60 Hz exactly
 * like the PS2 original, so a fixed dt keeps everything deterministic. */
#define FRAME_DT        (1.0f / 60.0f)
#define WALK_SPEED      15.0f   /* units/sec */
#define TURN_SPEED      12.0f   /* rad/sec — facing seeks the move dir */

/* Animation clips + crossfade. Library clip ids (chunk28/f01_id3c,
 * identified 2026-06-10 by stride scan — see tools/export_native.py):
 * 346 = idle/look-around (the clip the port has played since EMDL v2),
 * 2 = walk, 3 = run (in the asset for later). The walk clip is baked
 * IN PLACE; its natural ground speed at 60 fps is 24.07 u/s (printed by
 * the exporter), so playback rate = move_speed / that keeps the feet
 * tracking the ground. Idle<->walk is a 0.15 s LINEAR palette blend —
 * the engine cross-fades clip transitions the same way (PROGRESS.md:
 * mid-blend live captures match no single clip). */
#define CLIP_ID_IDLE    346u
#define CLIP_ID_WALK    2u
#define CLIP_ID_RUN     3u
#define WALK_CLIP_SPEED 24.07f  /* units/sec at the baked 60 fps */
#define ANIM_BLEND_TIME 0.15f   /* seconds, idle<->walk crossfade */
#define STICK_DEADZONE  0.25f
#define CAM_DIST        30.0f   /* chase eye distance behind the player */
#define CAM_HEIGHT      15.0f   /* chase eye height above the floor */
#define CAM_LOOK_H      8.0f    /* look-at height (chest of a 15u body) */
#define CAM_ORBIT_SPEED 1.8f    /* rad/sec — d-pad LEFT/RIGHT orbit */
#define CAM_LERP        0.12f   /* eye seek factor per frame */
#define EM_PI           3.14159265f

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
    float      move_speed;       /* this frame's ground speed, units/sec */
    float      walk_palette[1024 * 16];  /* scratch for the blend */

    /* player world placement (the actor's position + facing) */
    float      pos[3];           /* world position, feet on the floor */
    float      yaw;              /* facing about +Y, radians; 0 = +Z */

    /* chase camera */
    float      cam_yaw;          /* orbit angle around the player */
    float      cam_eye[3];       /* lerped eye position */
    int        cam_snapped;      /* eye seeded at its desired point */

    /* camera block — the native 0x008101D0 */
    float      viewproj[16];

    /* render chain (this frame's recorded draws) */
    ChainDraw  chain[SCENE_MAX + 1];
    int        chain_len;
    int        chain_test_triangle;

    /* EM_CAPTURE / EM_MOVE_TEST debug instrumentation */
    const char *capture_path;
    int         capture_frame;
    int         move_test;
} g;

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

/* Player movement (the port's first slice of the actor spine's physics
 * side): left stick = camera-relative walk on the XZ plane; the facing
 * yaw seeks the movement direction at TURN_SPEED (smooth turn). Position
 * is clamped to the room's collision bbox; real collision comes later
 * (see the kRoomMin TODO above). */
static void player_move(void)
{
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
     * walks away from the camera. */
    float fx = sinf(g.cam_yaw), fz = cosf(g.cam_yaw);
    float mx = fx * -sy - fz * sx;
    float mz = fz * -sy + fx * sx;

    g.pos[0] += mx * WALK_SPEED * FRAME_DT;
    g.pos[2] += mz * WALK_SPEED * FRAME_DT;
    if (g.pos[0] < kRoomMin[0]) g.pos[0] = kRoomMin[0];
    if (g.pos[0] > kRoomMax[0]) g.pos[0] = kRoomMax[0];
    if (g.pos[2] < kRoomMin[1]) g.pos[2] = kRoomMin[1];
    if (g.pos[2] > kRoomMax[1]) g.pos[2] = kRoomMax[1];
    g.pos[1] = 0.0f;  /* flat floor until the heightfield query lands */

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
 * keeping EM_CAPTURE idle output stable. */
static void actor_update(void)
{
    player_move();
    if (!g.mesh) return;

    float target = 0.0f;
    if (g.clip_walk >= 0) {
        target = g.move_speed / WALK_SPEED;
        if (target > 1.0f) target = 1.0f;
        float step = FRAME_DT / ANIM_BLEND_TIME;
        if      (g.walk_w < target - step) g.walk_w += step;
        else if (g.walk_w > target + step) g.walk_w -= step;
        else                               g.walk_w  = target;
        g.walk_t += (double)(FRAME_DT * g.move_speed / WALK_CLIP_SPEED);
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
    if (g.mesh) {
        g.chain[g.chain_len++] = (ChainDraw){ g.mesh, g.player_palette,
                                              g.model.bone_count };
    }
}

/* func_001C1D00(0x008101D0) — camera apply: fill the camera block the
 * recorded chain consumes. Follow/chase camera: the eye seeks (lerped) a
 * point CAM_DIST behind the player along the camera yaw, looking at chest
 * height; d-pad (arrow keys) LEFT/RIGHT orbits the yaw around the player.
 * The yaw otherwise holds still, so stick directions stay stable while
 * walking. */
static void camera_apply(void)
{
    const EmFrameInput *in = em_frame_input();
    if (in->held & EM_PAD_LEFT)  g.cam_yaw += CAM_ORBIT_SPEED * FRAME_DT;
    if (in->held & EM_PAD_RIGHT) g.cam_yaw -= CAM_ORBIT_SPEED * FRAME_DT;

    float fx = sinf(g.cam_yaw), fz = cosf(g.cam_yaw);
    float des[3] = { g.pos[0] - fx * CAM_DIST,
                     g.pos[1] + CAM_HEIGHT,
                     g.pos[2] - fz * CAM_DIST };
    if (!g.cam_snapped) {
        /* First frame: seed the eye at its desired point (no lerp-in). */
        g.cam_eye[0] = des[0];
        g.cam_eye[1] = des[1];
        g.cam_eye[2] = des[2];
        g.cam_snapped = 1;
    } else {
        for (int i = 0; i < 3; i++)
            g.cam_eye[i] += (des[i] - g.cam_eye[i]) * CAM_LERP;
    }

    float center[3] = { g.pos[0], g.pos[1] + CAM_LOOK_H, g.pos[2] };
    float up[3]     = { 0.0f, 1.0f, 0.0f };
    float far_clip  = g.n_scene ? 800.0f : 500.0f;

    int dw, dh;
    em_window_drawable_size(em_frame_window(), &dw, &dh);
    float aspect = (dh > 0) ? (float)dw / (float)dh : 4.0f / 3.0f;

    float view[16], proj[16];
    em_mat4_lookat(view, g.cam_eye, center, up);
    em_mat4_perspective(proj, 50.0f * EM_PI / 180.0f, aspect,
                        0.5f, far_clip);
    em_mat4_mul(g.viewproj, proj, view);
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

    if (g.capture_path && g.frame_no == g.capture_frame)
        em_gfx_request_capture(gfx, g.capture_path);

    g.t += 1.0 / 60.0;
    g.frame_no++;
    /* The move test owns the quit (frame 91) when both modes are set, so
     * a mid-walk capture doesn't cut the scripted walk short. */
    if (g.capture_path && !g.move_test && g.frame_no > g.capture_frame + 1)
        em_frame_request_quit();
}

/* EM_MOVE_TEST=1 — deterministic movement self-test. Injects a scripted
 * key sequence through the real em_input event API (an injected event
 * lands in the NEXT frame's input snapshot, exactly like a real key, so
 * the test exercises the full step-C unpack path): 'w' held for frames
 * 1..60 (walk forward, +Z at cam_yaw 0), 'd' for frames 61..90 (walk
 * screen-right, -X), then print the final placement and quit.
 * Expected: pos (107.4, 0, -184) + (0,0,+15) + (-7.5,0,0)
 *         = (99.900, 0.000, -169.000), yaw -pi/2 (facing -X). */
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
    switch (g.frame_no) {
        case 0:
            move_test_inject('w', 1);
            break;
        case 60:
            move_test_inject('w', 0);
            move_test_inject('d', 1);
            break;
        case 90:
            move_test_inject('d', 0);
            break;
        case 91:
            printf("move test: pos (%.3f, %.3f, %.3f) yaw %.4f rad\n",
                   g.pos[0], g.pos[1], g.pos[2], g.yaw);
            fflush(stdout);
            em_frame_request_quit();
            break;
        default:
            break;
    }
}

/* func_001AE5E0 — THE GAMEPLAY FRAME (stage order is the engine's). */
static void gameplay_frame(void)
{
    if (g.move_test) move_test_script();  /* debug instrumentation only */
    actor_context_begin();   /* func_001CB590(0x008102B0, 0x320, ...) */
    actor_update();          /* func_0015BCF0 — player actor update   */
    actor_context_end();     /* func_001CB5A0                         */
    render_chain_build();    /* func_001D1C50 — render chain build    */
    camera_apply();          /* func_001C1D00(0x008101D0)             */
    /* func_001AFD70(0) / func_0015C160 / func_001F0360 — world
     * services: not yet translated. */
    /* func_001CB590(0x008101E0, 0xD0, 0) HUD context + func_0018B9C0
     * view-target update: not yet translated. */
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
             * Natively the spawn-table stand-in is kPlayerPos (origin
             * with no scene loaded) facing +Z, and the chase camera arms
             * behind it; the camera block is rebuilt every frame. */
            g.t              = 0.0;
            g.walk_t         = 0.0;
            g.walk_w         = 0.0f;
            g.frame_no       = 0;
            g.frame_selector = 0;
            g.pos[0] = g.n_scene ? kPlayerPos[0] : 0.0f;
            g.pos[1] = g.n_scene ? kPlayerPos[1] : 0.0f;
            g.pos[2] = g.n_scene ? kPlayerPos[2] : 0.0f;
            g.yaw         = 0.0f;
            g.cam_yaw     = 0.0f;
            g.cam_snapped = 0;
            self->user[GAME_BYTE_FRAME] = 1;
            /* fall through — the engine's init frame still renders */
        case 1:
            /* Live in-game arm: per-frame services (func_001AFCF0 flag
             * reset, func_001B07C0(1) placement check, func_001C1DC0
             * camera service, func_0018D7B0/func_0018C0D0 HUD context,
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
                                  "--clips 346,2,3)");
    } else {
        printf("no %s — showing the test triangle. Generate it with the "
               "decomp repo's tools/export_native.py\n", MODEL_PATH);
    }

    /* Optional scene (level parts, world-space). */
    g.n_scene = scene_load(gfx, g.scene, SCENE_MAX);

    em_task_register(0, game_task);  /* func_001AB740(0, func_001ACEC0) */
}

void em_game_install(void)
{
    memset(&g, 0, sizeof g);
    g.capture_path = getenv("EM_CAPTURE");
    const char *cf = getenv("EM_CAPTURE_FRAME");
    g.capture_frame = cf ? atoi(cf) : 60;   /* default = the historical
                                               regression frame */
    const char *mt = getenv("EM_MOVE_TEST");
    g.move_test    = mt && mt[0] == '1';
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
}
