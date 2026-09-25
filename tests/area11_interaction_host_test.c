/* Actual AREA11 host integration. GPU/audio devices, ordinary follow-camera
 * evolution and prop drawing are explicit outer boundaries. Player channels,
 * collision retarget, script/owner timing, inventory, status/UI and projection
 * all use the real implementations and exported original assets. Owner claims
 * and the previous publication list are fixture inputs; this does not test
 * the outer game's Use-input/culling arbitration. */
#include "game/em_area11_interaction_host.h"
#include "game/em_area11_roger.h"
#include "game/em_camera.h"
#include "game/em_camera_live.h"
#include "game/em_collision_world.h"
#include "game/em_effect_color.h"
#include "game/em_hud.h"
#include "game/em_opening_media.h"
#include "game/em_pickup.h"
#include "game/em_player.h"
#include "game/em_player_stage_workers.h"
#include "game/em_pickup_original.h"
#include "game/em_pickup_motion.h"
#include "game/em_random.h"
#include "game/em_scene_bindings.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

EmGameState g;
/* em_pickup keeps its taken bits and CA4..CA7 in the D2 progress region,
 * which the game owns in em_scene_bindings.c (not linked here). */
EmSceneState *em_scene_state(void) { static EmSceneState state; return &state; }
/* The live camera's inputs (em_camera_live.h): this fixture's own player
 * record (setup()), the pose host's hip and Euler, no pad assignment block, a local 0x700031F0 word, no
 * legacy stand-in and no scripted timeline (the host's scripts here never
 * set the camera's +4 to 3). */
static EmPlayerLiveActor player_record;
static int32_t carry31F0;
static const EmPlayerLiveActor *camera_player(void *ctx) { (void)ctx; return &player_record; }
static int camera_no_standin(void *ctx) { (void)ctx; return CAMERA_STANDIN_NONE; }
static int camera_no_timeline(void *ctx) { (void)ctx; return -1; }
static int camera_hip(void *ctx, float out[3]) { (void)ctx; return player_pose_hip(out); }
static int camera_euler(void *ctx, float out[3]) { (void)ctx; return player_pose_script_euler(out); }
static const EmCameraLiveHost camera_host = {NULL, camera_player, camera_hip, camera_euler, NULL, &carry31F0,
                                             camera_no_standin, camera_no_timeline};
const float kLocoTierSpeed[4] = {0};
static unsigned uploads, triangles, sounds, resumes, indicators, status_requests;
static unsigned background_steps, background_frames;
static int sfx_selected, sfx_bank_available = 1;
static unsigned face_updates;
static int fail_face_update;
static float face_update_body_remaining;
static float panel_target[3], panel_yaw;

int em_gfx_overlay_texture_set(EmGfx *g, int slot, const uint8_t *p, uint32_t w, uint32_t h)
{
    (void)g;
    assert(slot == 1 && p && w && h);
    ++uploads;
    return 1;
}
void em_gfx_overlay_canvas(EmGfx *g, float w, float h)
{
    (void)g;
    (void)w;
    (void)h;
}
void em_hud_decor_invalidate(void)
{
}
/* 0020A7A0 (em_status_background_draw.c): one call is one D_002655A0 step. */
int em_status_background_render(struct EmGfx *g, float u, float v, float w, float h)
{
    assert(g && w > 0 && h > 0 && u >= 0 && v >= 0);
    ++background_steps;
    return 1;
}
int em_status_background_load_sdk(const char *path)
{
    FILE *file = fopen(path, "rb"); /* the asset is required; its reader is tested elsewhere */
    assert(file);
    fclose(file);
    return 1;
}
void em_status_background_frame(struct EmGfx *g)
{
    assert(g);
    ++background_frames;
}
/* The hub's 00209DF0 draw (em_status_hub_ui): its arcs and CB4 reserve. */
int16_t em_weapon_reserve(void) { return 60; }
void em_hud_text(EmGfx *g, float x, float y, const char *s, EmHudTextStyle style)
{
    (void)g;
    (void)x;
    (void)y;
    (void)s;
    (void)style;
}
void em_hud_text_color(EmGfx *g, float x, float y, const char *s, EmHudTextStyle style, uint32_t c)
{
    (void)g;
    (void)x;
    (void)y;
    (void)s;
    (void)style;
    (void)c;
}
float em_hud_text_width(const char *s, EmHudTextStyle style)
{
    (void)style;
    return 8.0f * (float)strlen(s); /* the font sheet is a required worker of 00209DF0 */
}
void em_gfx_overlay_sprite(EmGfx *g, float x, float y, float w, float h, float u, float v, float u1,
                           float v1, const float color[4])
{
    (void)g;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)u;
    (void)v;
    (void)u1;
    (void)v1;
    (void)color;
}
void em_gfx_overlay_sprite_blend(EmGfx *g, float x, float y, float w, float h, float u, float v,
                                 float u1, float v1, const float color[4], EmGfxOverlayBlend mode)
{
    (void)mode;
    em_gfx_overlay_sprite(g, x, y, w, h, u, v, u1, v1, color);
}
int em_gfx_overlay_triangle(EmGfx *g, const float xy[3][2], const float rgba[3][4], float u,
                            float v, EmGfxOverlayBlend blend)
{
    (void)g;
    (void)xy;
    (void)u;
    (void)v;
    /* The 0020AC70 trail fans (ITEM and hub); the hub's 002082B0 arcs and
     * 00208750 marker lines are the other triangles. */
    if (blend != EM_GFX_UI_ADD || rgba[1][0] != 0 || rgba[2][0] != 0 || rgba[0][3] != 0)
        return 1;
    assert(rgba[0][0] <= 32 / 255.0f);
    ++triangles;
    return 1;
}


/* The message service's glyph boundary (em_message_live, WP-8). */
int em_hud_tall_glyph_cell(uint32_t index, EmHudGlyphCell *cell)
{ (void)index; (void)cell; return 1; }
void em_hud_glyph_strip(EmGfx *gfx, const EmMessageGlyphFlush *flush) { (void)gfx; (void)flush; }
int em_sfx_set_area(int area, int sub)
{
    sfx_selected = 0;
    if (area != 11 || sub != 0) return 1;
    if (!sfx_bank_available) return 0;
    sfx_selected = 1;
    return 1;
}
int em_sfx_cue_state(unsigned cue)
{
    if (cue == 0x3EE || cue == 0x3EF)
        return sfx_selected ? (cue == 0x3EE ? 2 : 1) : 0;
    return 1; /* Legacy status/menu cues are outside this audio fixture. */
}
void em_sfx_play(unsigned cue) { assert(em_sfx_cue_state(cue)); ++sounds; }
void em_sfx_play_at(unsigned cue, const float *position, float radius)
{ assert(position && radius > 0); em_sfx_play(cue); }
void em_sfx_stop_all(void) {}
void em_bgm_stop(int fade) { assert(fade == 0); }
int em_bgm_device_ensure(int rate) { return rate == 48000 ? 0 : -1; }
int em_bgm_play_ticks(const char *path, int loop, unsigned ticks)
{
    assert(strstr(path, "opening_resume.wav") && loop == 1 && ticks >= 270 && ticks <= 397);
    FILE *file = fopen(path, "rb"); assert(file); fclose(file);
    ++resumes;
    return 0;
}
int em_bgm_wav_read(const char *path, EmBgmWav *wav, const char *tag)
{
    (void)tag;
    /* Audio decoding/device output is a boundary, but require its asset. */
    FILE *file = fopen(path, "rb"); assert(file); fclose(file);
    *wav = (EmBgmWav){calloc(2, sizeof(int16_t)), 1, 2, 48000};
    return wav->pcm ? 0 : -1;
}
void em_props_panel_complete(void) { ++indicators; }
void elevator_pose(void) {}
EmGfxMesh *em_gfx_mesh_create(EmGfx *gfx, const float *verts, uint32_t vertices,
    const uint32_t *indices, uint32_t count, const EmGfxTexDesc *textures,
    uint32_t texture_count, const uint8_t *texels, uint32_t flags)
{
    (void)gfx; (void)textures; (void)texture_count; (void)texels; (void)flags;
    assert(verts && vertices && indices && count);
    return (EmGfxMesh *)1;
}
void em_gfx_mesh_destroy(EmGfx *gfx, EmGfxMesh *mesh) { (void)gfx; (void)mesh; }
/* The hub's GS order (0020CDC0 phase 1 step 1): 0020A7A0's background is
 * flushed (the ordered 2D layer) before 001B0000's model draws, which come
 * before 00209DF0's 2D layer. */
static unsigned flushed_frame, model_draws;
static int hub_rendering;
void em_gfx_overlay_backdrop_flush(EmGfx *gfx) { assert(gfx); flushed_frame = background_frames; }
void em_gfx_draw_skinned(EmGfx *gfx, EmGfxMesh *mesh, const float *viewproj,
                         const float *palette, uint32_t bones)
{
    assert(gfx && mesh && viewproj && palette && bones);
    if (!hub_rendering) return; /* the pickups' draws */
    assert(flushed_frame == background_frames); /* this frame's backdrop went first */
    ++model_draws;
}
void em_gfx_char_rig(EmGfx *gfx, const EmGfxCharRig *rig) { assert(gfx); (void)rig; }
/* No script owner claims the player here (Roger's takeover is proved by the
 * level smoke's roger phase against route 14): his resource regions are
 * never mapped. */
int em_area11_roger_regions(int (*map)(void *ctx, uint32_t address, uint32_t size, const uint8_t *bytes),
                            void *ctx)
{
    (void)map; (void)ctx;
    assert(!"a script owner's takeover in the host test");
    return -1;
}
void em_gfx_fog_off(EmGfx *gfx) { assert(gfx); }
int em_gfx_mesh_update_positions(EmGfx *gfx, EmGfxMesh *mesh, const float *positions,
                                  uint32_t count)
{
    assert(gfx && mesh && positions && count > g.model.vert_count);
    for (uint32_t i = 0; i < count * 3; ++i) assert(isfinite(positions[i]));
    unsigned clip, flags; int transition;
    assert(player_pose_source(&clip, &face_update_body_remaining, &flags, &transition));
    ++face_updates;
    return !fail_face_update;
}

/* The ordinary host's existing placement boundary; raw channel state never
 * comes from this displayed palette. */
void palette_apply_placement(float *palette, uint32_t count, const float position[3], float yaw)
{
    float c = cosf(yaw), s = sinf(yaw);
    for (unsigned bone = 0; bone < count; ++bone) {
        float *matrix = palette + bone * 16;
        for (unsigned column = 0; column < 4; ++column) {
            float x = matrix[column * 4], z = matrix[column * 4 + 2];
            matrix[column * 4] = c * x + s * z;
            matrix[column * 4 + 2] = -s * x + c * z;
        }
        for (unsigned axis = 0; axis < 3; ++axis) matrix[12 + axis] += position[axis];
    }
}

/* A pool record address for the panel (the game passes its node's). */
enum { PANEL_ADDRESS = 0x7AA590 };

static int powered(void)
{
    const uint8_t *power = em_scene_progress_at(em_scene_state(), 0x0081084Cu, 1);
    return power && (*power & 0x80);
}

/* The fixture's stand-in for 0x1AE040's r == 2 arm: the panel's 00157F60
 * posts B0 = 1 / B1 = 0x82 / D_008106D0 canonically (the live scene core
 * opens the status screen on it and routes 0020E060/0020CDC0 to the host's
 * page layer); here the request is handed to the runtime's own frame
 * machine, and the page copy takes over B0. */
static void bridge_status_request(void)
{
    EmSceneState *scene = em_scene_state();
    if (!scene->req[EM_SCENE_REQ_B0]) return;
    if (scene->req[EM_SCENE_REQ_B1] == 0x1B) {
        /* The battery pickup's 001C47A0 request (after 001C40B0 added it). */
        ++status_requests;
        assert(scene->req[EM_SCENE_REQ_B0] == 1 && em_pickup_item_count(0x1B) == 1);
        assert(em_status_runtime_pickup_request(em_area11_interaction_host_status(), 1, 0x1B));
        scene->req[EM_SCENE_REQ_B0] = 0;
        return;
    }
    if (!(scene->req[EM_SCENE_REQ_B1] & 0x80)) return; /* other takes: left for the scenario */
    const uint8_t *d0 = em_scene_req_at(scene, 0x008106D0u);
    uint32_t owner = (uint32_t)d0[0] | (uint32_t)d0[1] << 8 | (uint32_t)d0[2] << 16 |
                     (uint32_t)d0[3] << 24;
    assert(scene->req[EM_SCENE_REQ_B0] == 1 && scene->req[EM_SCENE_REQ_B1] == 0x82 &&
           owner == PANEL_ADDRESS);
    assert(em_status_runtime_battery_open(em_area11_interaction_host_status(),
        &em_area11_interaction_host_panel()->owner, scene->req[EM_SCENE_REQ_B1]) == 1);
    scene->req[EM_SCENE_REQ_B0] = 0;
}

static float word(const unsigned char *ram, unsigned address)
{ float value; memcpy(&value, ram + address, 4); return value; }

/* The owners' pool records (census L07): the game binds each owner's node
 * record at its first call; the fixture binds one record per owner, placed
 * at the owner's EMIS placement. Their +0x0E names no collision cell (0xFF),
 * so 001A2370 leaves the directory alone here (the live cells are compared
 * with the route captures by tools/test_collision_world_capture.py). */
static EmActor records[EM_INTERACTION_CAPACITY];

static void bind_records(void)
{
    EmInteractionScene *scene_owners = em_area11_interaction_host_scene();
    memset(records, 0, sizeof records);
    for (size_t i = 0; i < scene_owners->count; ++i) {
        const EmInteractionSceneOwner *owner = &scene_owners->owners[i];
        if (owner->role != EM_INTERACTION_PICKUP && owner->role != EM_INTERACTION_PANEL &&
            owner->role != EM_INTERACTION_ELEVATOR) continue;
        if (owner->role == EM_INTERACTION_PICKUP && !owner->native_owner) continue;
        EmActor *record = &records[i];
        record->status = 1;
        record->cls = owner->class_flags;
        record->uid = 0xFF00;
        record->self = record;
        record->source_id = owner->source_id;
        memcpy(record->pos, owner->position, sizeof owner->position);
        memcpy(record->rot, owner->angles, sizeof owner->angles);
        for (unsigned k = 0; k < 4; ++k) record->f60[k] = 1.0f;
        assert(em_area11_interaction_host_bind_actor(owner->source_id, record) == 0);
    }
}

/* Each bound item node's first call (state 0), after a host load. */
static void pickups_state0(void)
{
    EmInteractionScene *scene_owners = em_area11_interaction_host_scene();
    for (size_t i = 0; i < scene_owners->count; ++i) {
        const EmInteractionSceneOwner *record = &scene_owners->owners[i];
        if (record->role == EM_INTERACTION_PICKUP && record->native_owner)
            assert(em_area11_interaction_host_pickup_state0(record->source_id, record->subtype, 0) == 0);
    }
}

static void setup(int reset_inventory)
{
    memset(&g, 0, sizeof g);
    em_frame_init(NULL, (EmGfx *)1);
    assert(!em_model_load(&g.model, "assets/player.emdl"));
    assert(!em_collision_load(&g.coll, "assets/scene_snow/snow.emcl"));
    /* The area's original collision world (w_001AFCA0 in the game). */
    assert(!em_collision_world_load(&g.coll, "assets/scene_snow/snow.emcl",
                                    EM_COLLISION_WORLD_CELLS_PATH, EM_COLLISION_WORLD_SDK_PATH));
    g.mesh = (EmGfxMesh *)1;
    g.grate_present = g.elev_has_mesh = 1;
    g.elev_pos[1] = 230;
    g.status.health = 100;
    g.loco_rate = 1;
    unsigned char *ram = malloc(0x2000000); assert(ram);
    FILE *file = fopen("../Extermination/build/startup-reference/playable_ee.bin", "rb");
    assert(file && fread(ram, 1, 0x2000000, file) == 0x2000000); fclose(file);
    for (unsigned axis = 0; axis < 3; ++axis) {
        g.pos[axis] = word(ram, 0x810350 + 4 * axis);
        g.cam.eye[axis] = word(ram, 0x8105D0 + 4 * axis);
        g.cam.tgt[axis] = word(ram, 0x8105E0 + 4 * axis);
        g.cam.eye_des[axis] = word(ram, 0x8101F0 + 4 * axis);
        g.cam.tgt_des[axis] = word(ram, 0x810200 + 4 * axis);
        g.cam.up[axis] = word(ram, 0x8105F0 + 4 * axis);
    }
    g.yaw = word(ram, 0x810374);
    g.cam_dist_param = word(ram, 0x8101EC);
    g.cam.state = ram[0x8101E0]; g.cam.sub_state = ram[0x8101E1];
    g.cam.top_mode = ram[0x8101E4]; g.cam.mode = ram[0x8101E6];
    g.cam.y_lo = word(ram, 0x810230); g.cam.y_hi = word(ram, 0x810234);
    g.cam.var_5c = word(ram, 0x81023C); g.cam.overhead_y = word(ram, 0x810240);
    g.cam.horiz_dist = word(ram, 0x810690); g.cam.zoom = 480;
    /* The live camera over the area's collision world, its bytes the
     * capture's camera block and vector pool (the g.cam view follows). */
    assert(em_camera_live_bind(&camera_host) == 0);
    memcpy(em_camera_live_bytes(0x008101E0u, 0xD0), ram + 0x8101E0, 0xD0);
    memcpy(em_camera_live_bytes(0x008105D0u, 0xD4), ram + 0x8105D0, 0xD4);
    em_camera_live_view_publish();
    free(ram);
    file = fopen("../Extermination/build/startup-reference/panel/animation_ee.bin", "rb");
    assert(file && !fseek(file, 0x810350, SEEK_SET));
    assert(fread(panel_target, sizeof(float), 3, file) == 3);
    assert(!fseek(file, 0x810374, SEEK_SET) && fread(&panel_yaw, sizeof(float), 1, file) == 1);
    fclose(file);
    if (reset_inventory) em_pickup_reset();
    /* The canonical scene bytes the host's view and owners use (the game
     * keeps them in em_scene_bindings.c): AREA11, and a fresh request
     * block, selector and cooldown for each scenario. */
    EmSceneState *scene = em_scene_state();
    memset(scene->req, 0, sizeof scene->req);
    scene->spad3B8D = scene->spad3B8F = scene->spad3B92 = scene->spad3B91 = 0;
    scene->spad3B84 = 0;
    scene->d810700 = 0x0B;
    em_frame_fade_clear(0);
    em_random_seed(0x45);
    /* The pose lives in the player record (the game's is em_player.c's live
     * record, with the player stage's scene and globals views; this fixture
     * keeps its own), and D_008106F3 is the canonical byte. */
    static uint8_t stage_bytes[2];
    static EmPlayerStageScene stage_scene = { .d8106F1 = &stage_bytes[0] };
    static EmPlayerStageGlobals stage_globals = { .d810707 = &stage_bytes[1] };
    memset(&player_record, 0, sizeof player_record);
    assert(player_pose_load(PLAYER_CLIP_BANK_PATH, PLAYER_CLIP_ROW0_PATH));
    assert(player_pose_attach(&player_record, em_scene_req_at(scene, 0x008106F3u), &stage_scene,
                              &stage_globals));
    assert(player_pose_opening_release());
    player_pose_finish_palette();
    assert(em_opening_media_prepare("assets/scene_snow") == 0);
    /* The live message service (step F) the panel and terminal lines run on. */
    assert(em_message_live_install("assets/message/message_data.emmd"));
    assert(em_message_live_reset() == 0);
    /* The placed items (em_scene's manifest pickups) the host binds; a
     * taken item is not placed (-2). */
    static EmInteractionScene placed;
    assert(em_interaction_scene_load(&placed, "assets/scene_snow/interaction.emis"));
    for (size_t i = 0; i < placed.count; ++i)
        if (placed.owners[i].role == EM_INTERACTION_PICKUP)
            assert(em_pickup_add(NULL, "assets/scene_snow", (int)placed.owners[i].item_type,
                                 placed.owners[i].position, placed.owners[i].angles[1],
                                 placed.owners[i].uid, NULL, 0) != -1);
    assert(em_area11_interaction_host_load("assets/scene_snow", NULL, NULL));
    em_message_live_set_host(em_area11_interaction_host_message_host());
    bind_records();
    pickups_state0();
    assert(sfx_selected);
    em_area11_interaction_host_set_panel_address(PANEL_ADDRESS);
    player_pose_set_stage_hook(em_area11_interaction_host_player, NULL);
}

static void teardown(void)
{
    player_pose_set_stage_hook(NULL, NULL);
    player_use_set_hook(NULL, NULL);
    player_pose_unload();
    em_pickup_scene_clear(NULL);
    em_message_live_set_host(NULL);
    em_area11_interaction_host_clear();
    em_message_live_shutdown();
    assert(!sfx_selected);
    assert(!em_area11_interaction_host_shared() && !player_pose_owned());
    em_opening_media_shutdown();
    em_collision_world_unload();
    em_collision_free(&g.coll);
    em_model_free(&g.model);
}

/* Every bound item owner's node call (em_area11_interaction_host_pickup_tick);
 * returns how many freed their owner this call. */
static int pickup_ticks(void)
{
    EmInteractionScene *scene = em_area11_interaction_host_scene();
    int freed = 0;
    for (size_t i = 0; i < scene->count; ++i) {
        const EmInteractionSceneOwner *record = &scene->owners[i];
        if (record->role != EM_INTERACTION_PICKUP || !record->native_owner) continue;
        const EmPickupOwner *owner = record->native_owner;
        if (owner->freed) continue; /* its node freed itself */
        int result = em_area11_interaction_host_pickup_tick(record->source_id);
        assert(result >= 0);
        freed += !result;
    }
    return freed;
}

static int outer(unsigned pressed)
{
    EmStatusRuntime *status = em_area11_interaction_host_status();
    EmStatusInput input = {.pressed = pressed, .stick_x = 128, .stick_y = 128};
    unsigned clip, flags; float remaining; int transition;
    assert(player_pose_source(&clip, &remaining, &flags, &transition));
    float palette[22 * 16], position[3];
    memcpy(palette, g.player_palette, sizeof palette); memcpy(position, g.pos, sizeof position);
    EmScript panel_script = em_area11_interaction_host_panel()->program.script;
    EmScript elevator_script = em_area11_interaction_host_elevator()->program.script;
    EmInteractionAnimation animation = em_area11_interaction_host_shared()->animation;
    bridge_status_request();
    int result = em_status_runtime_tick(status, &input);
    if (result < 0) {
        const EmStatusFrame *f = em_status_runtime_frame(status);
        const EmStatusPage *p = em_status_runtime_page(status);
        fprintf(stderr, "Host status fault ordinary%d pressed%X frame%u/%u page%u/%u item%u/%u request%u/%u\n",
            g.frame_no, pressed, f->phase, f->step, p->phase, p->step,
            p->item.state, p->item.step, p->request, p->request_kind);
    }
    assert(result >= 0);
    if (result) {
        unsigned current_clip, current_flags; float current_remaining; int current_transition;
        assert(player_pose_source(&current_clip, &current_remaining, &current_flags, &current_transition));
        assert(clip == current_clip && remaining == current_remaining &&
               flags == current_flags && transition == current_transition);
        assert(!memcmp(palette, g.player_palette, sizeof palette));
        assert(!memcmp(position, g.pos, sizeof position));
        assert(!em_status_runtime_ordinary_enabled(status));
        assert(!em_area11_interaction_host_panel_tick());
        assert(!em_area11_interaction_host_elevator_tick());
        assert(pickup_ticks() == 0);
        assert(!memcmp(&panel_script, &em_area11_interaction_host_panel()->program.script,
                       sizeof panel_script));
        assert(!memcmp(&elevator_script, &em_area11_interaction_host_elevator()->program.script,
                       sizeof elevator_script));
        assert(!memcmp(&animation, &em_area11_interaction_host_shared()->animation, sizeof animation));
        assert(em_status_runtime_render(status, (EmGfx *)1) == 1);
        unsigned old_triangles = triangles;
        assert(em_status_runtime_render(status, (EmGfx *)1) == 1 && triangles == old_triangles);
        return 1;
    }
    ++g.frame_no;
    assert(player_pose_stage() >= 0);
    player_pose_finish_palette();
    EmPanel *panel = &em_area11_interaction_host_panel()->owner;
    int aligning = panel->phase == 0 && (panel->armed & 4);
    float old_feet[3], old_hip[3];
    memcpy(old_feet, g.pos, sizeof old_feet);
    assert(player_pose_hip(old_hip));
    assert(!em_area11_interaction_host_panel_tick());
    if (aligning) {
        /* Original panel capture pins the transformed X/Z and facing.
         * Ground Y is the live player's prior Y, as001B6F00 specifies. */
        assert(g.pos[0] == panel_target[0] && g.pos[2] == panel_target[2]);
        assert(g.pos[1] == old_feet[1] && g.yaw == panel_yaw);
        float hip[3], euler[3];
        assert(player_pose_hip(hip) && player_pose_script_euler(euler));
        for (unsigned axis = 0; axis < 3; ++axis) {
            float target = axis == 1 ? old_feet[1] : panel_target[axis];
            float delta = em_effect_float32((double)target - old_feet[axis]);
            assert(hip[axis] == em_effect_float32((double)old_hip[axis] + delta));
        }
        assert(euler[1] == panel_yaw);
    }
    assert(!em_area11_interaction_host_elevator_tick());
    EmInteractionFrame *frame = em_area11_interaction_host_shared()->frame;
    (void)pickup_ticks(); /* the item owners at their nodes (they store their frame view) */
    assert(em_message_live_tick() == 0);
    assert(g.cam.top_mode == frame->camera_top && g.cam.sub_state == frame->camera_phase &&
           g.cam.mode == frame->camera_mode);
    /* Explicit original camera-stage scheduling (0018B9C0 decays the
     * canonical D_008106EF); full follow evolution is outside this
     * interaction fixture. A status-only commit never gets here. */
    uint8_t *cooldown = &em_scene_state()->req[EM_SCENE_REQ_EF];
    if (*cooldown) --*cooldown;
    camera_commit(&g.cam);
    return 0;
}

static void first_battery(void)
{
    setup(1);
    EmInteractionScene *scene = em_area11_interaction_host_scene();
    EmInteractionSceneOwner *record = NULL;
    for (size_t i = 0; i < scene->count; ++i)
        if (scene->owners[i].role == EM_INTERACTION_PICKUP && scene->owners[i].item_type == 0x1B) {
            assert(!record);
            record = &scene->owners[i];
        }
    assert(record);
    /* The host bound the item at load (em_pickup_original_bind). */
    EmInteractionRuntime *shared = em_area11_interaction_host_shared();
    EmPickupOwner *owner = em_pickup_original_owner(record->uid);
    assert(owner && record->native_owner == owner);
    assert(owner && player_pose_use_accepted_port());
    assert(em_interaction_runtime_claim(shared, owner));
    em_area11_interaction_host_camera_fields(); /* the claim's 3B8D = 3 */
    owner->armed = 4;
    unsigned previous_requests = status_requests, ticks = 0;
    while (status_requests == previous_requests) {
        /* B8FC0/sub8 settles only the actual target, unlike elevator sub0:
         * the desired eye and target stay as they are. */
        float desired[6];
        memcpy(desired, g.cam.eye_des, 3 * sizeof(float));
        memcpy(desired + 3, g.cam.tgt_des, 3 * sizeof(float));
        /* The request posted by the previous call is handed to the status
         * frame machine at the start of this one (bridge_status_request),
         * whose first status frame it then is. */
        int status_frame = outer(0);
        if (status_requests != previous_requests) {
            assert(status_frame == 1);
            break;
        }
        assert(!status_frame); assert(++ticks < 512);
        assert(!memcmp(desired, g.cam.eye_des, 3 * sizeof(float)));
        assert(!memcmp(desired + 3, g.cam.tgt_des, 3 * sizeof(float)));
    }
    assert(owner->lifecycle == 1 && owner->phase == 1 && shared->owner == owner);
    for (unsigned i = 1; i < 7; ++i) assert(outer(0) == 1);
    EmStatusRuntime *status = em_area11_interaction_host_status();
    assert(em_status_runtime_page(status)->item.step == 3);
    assert(em_pickup_battery_charge() == 12 && em_pickup_battery_capacity() == 12);
    assert(outer(0x40) == 1); /* Dismiss original pickup notice. */
    assert(em_status_runtime_page(status)->item.step == 1);
    assert(outer(0x10) == 1); /* Original Triangle status exit. */
    unsigned consumed = 0;
    while (em_status_runtime_frame(status)->phase != 1) {
        assert(outer(0) == 1); assert(++consumed < 8);
    }
    assert(em_scene_state()->req[EM_SCENE_REQ_EF] == 70);
    assert(!em_status_runtime_ordinary_enabled(status) && shared->owner == owner);
    assert(isfinite(em_area11_interaction_host_projection()->scale));
    do { assert(!outer(0)); assert(++ticks < 520); } while (shared->owner);
    assert(!player_pose_owned() && owner->lifecycle == 3 && !powered());
    printf("AREA11 native host first battery: %u callbacks, status release70 PASS\n", ticks);
    teardown();
}

/* The other AREA11 takes (001B6EA0 families): each posts its original
 * request (001C47A0 B0 = 1 / 001C4720 B0 = 2 / 001C4760 B0 = 3, B1 = the
 * type) after its inventory write, and the page 0020CDC0 would open for it
 * is not translated: the host's page faults with a report, never a silent
 * nothing. */
static void other_take(uint16_t uid, uint8_t kind, uint8_t type)
{
    setup(1);
    uint8_t mag = 0;
    int16_t reserve = 60;
    em_pickup_set_weapon_ammo(&mag, &reserve);
    EmInteractionScene *scene = em_area11_interaction_host_scene();
    EmInteractionSceneOwner *record = em_interaction_scene_pickup(scene, uid);
    assert(record && record->item_type == type && record->native_owner);
    EmPickupOwner *owner = record->native_owner;
    EmInteractionRuntime *shared = em_area11_interaction_host_shared();
    assert(player_pose_use_accepted_port() && em_interaction_runtime_claim(shared, owner));
    em_area11_interaction_host_camera_fields(); /* the claim's 3B8D = 3 */
    owner->armed = 4;
    EmSceneState *state = em_scene_state();
    unsigned ticks = 0;
    /* The camera target settles from the fixture stance toward the item. */
    while (!state->req[EM_SCENE_REQ_B0]) { assert(!outer(0)); assert(++ticks < 4000); }
    assert(state->req[EM_SCENE_REQ_B0] == kind && state->req[EM_SCENE_REQ_B1] == type);
    if (kind == 2) assert(em_pickup_maps()[type] == 1);
    else if (kind == 3) assert(em_pickup_keys()[type] == 1);
    else assert(em_pickup_item_count(type) == (type == 0x10 ? 3 : 1));
    if (type == 0x10) assert(mag == 30 && reserve == 90 && em_pickup_mag_packs() == 3);
    assert(em_area11_interaction_host_status_open() == 1);
    EmStatusInput input = {.stick_x = 128, .stick_y = 128};
    assert(em_area11_interaction_host_status_page(&input) == -1 && em_area11_interaction_host_failed());
    printf("AREA11 native host take %04X: B0 = %u, B1 = %#04x after %u callbacks; its page faults PASS\n",
           (unsigned)uid, (unsigned)kind, (unsigned)type, ticks);
    em_pickup_set_weapon_ammo(NULL, NULL);
    teardown();
}

static void no_battery(void)
{
    setup(1);
    EmPanelRuntime *panel = em_area11_interaction_host_panel();
    assert(em_panel_runtime_arm(panel));
    em_area11_interaction_host_camera_fields(); /* the arm's 3B8D = 3 */
    unsigned ticks = 0;
    do { assert(!outer(0)); assert(++ticks < 300); }
    while (em_area11_interaction_host_shared()->owner);
    assert(panel->owner.phase == 0 && panel->owner.status == 1 && !powered());
    assert(!player_pose_owned() && g.cam.top_mode == 0 && g.cam.zoom == 480);
    printf("AREA11 native host no-battery: %u ordinary callbacks PASS\n", ticks);
    teardown();
}

static void panel_menu(int discharge)
{
    setup(0); /* Keep the actual inventory acquired by the first pickup. */
    assert(em_pickup_item_count(0x1B) == 1 && em_pickup_battery_charge() == 12);
    EmInteractionScene *scene = em_area11_interaction_host_scene();
    EmInteractionSceneOwner *record = em_interaction_scene_role(scene, EM_INTERACTION_PANEL);
    assert(record);
    /* Supply the previously published canonical list, independently of
     * world culling: the panel's record through 001B1B70 and 001AAD00's list
     * block (the collision world's interactive list, census L07). The
     * native185420 device predicate itself is real. */
    EmActor *panel_record = &records[record - scene->owners];
    assert(panel_record->self == panel_record);
    em_collision_world_lists_reset_001AF8E0();
    assert(em_collision_world_publish_001B1B70(panel_record) == 1);
    uint32_t fault = 0;
    assert(em_collision_world_close_out_001AAD00(em_scene_state(), 0, &fault) == 0 && !fault);
    EmPanelRuntime *panel = em_area11_interaction_host_panel();
    assert(player_pose_use_accepted_port());
    assert(em_panel_runtime_arm(panel));
    em_area11_interaction_host_camera_fields(); /* the arm's 3B8D = 3 */
    unsigned ticks = 0, old_resumes = resumes, old_indicators = indicators;
    while (!outer(0)) assert(++ticks < 200);
    for (unsigned i = 1; i < 7; ++i) assert(outer(0) == 1);
    EmStatusRuntime *status = em_area11_interaction_host_status();
    EmInteractionRuntime *shared = em_area11_interaction_host_shared();
    assert(panel->owner.phase == 6 && shared->owner == panel && player_pose_owned());
    assert(em_status_runtime_page(status)->item.step == 4);
    assert(outer(0x40) == 1); /* The original initial selection is No. */
    assert(em_status_runtime_page(status)->item.step == 1);
    assert(outer(0x40) == 1); /* Actual native device lookup finds this panel. */
    assert(em_status_runtime_page(status)->item.step == 4);
    if (discharge) {
        assert(outer(0x8040) == 1);
        for (unsigned i = 1; i <= 61; ++i) {
            assert(outer(0) == 1);
            assert(em_pickup_battery_charge() == (i < 31 ? 10 : 8));
            assert(panel->owner.charged == (i == 61));
        }
    } else {
        assert(outer(0x40) == 1);
        assert(outer(0x10) == 1); /* Triangle exits browsing through the real outer page. */
    }
    unsigned consumed = 0;
    while (em_status_runtime_frame(status)->phase != 1) {
        assert(outer(0) == 1); assert(++consumed < 8);
    }
    assert(em_scene_state()->req[EM_SCENE_REQ_EF] == 70 && resumes == old_resumes + 1);
    assert(!em_status_runtime_ordinary_enabled(status));
    assert(shared->owner == panel && player_pose_owned());
    const EmInteractionProjection *projection = em_area11_interaction_host_projection();
    assert(!memcmp(projection->center, g.cam.tgt, 3 * sizeof(float)) && projection->scale > 0);
    do { assert(!outer(0)); assert(++ticks < 400); } while (shared->owner);
    assert(!player_pose_owned() && g.cam.top_mode == 0 && g.cam.zoom == 480);
    assert(em_pickup_item_count(0x1B) == 1 && em_pickup_battery_capacity() == 12);
    assert(em_pickup_battery_charge() == (discharge ? 8 : 12));
    assert(powered() == discharge && indicators == old_indicators + (unsigned)discharge);
    assert(panel->owner.phase == (discharge ? 3 : 0));
    printf("AREA11 native host panel discharge%d: %u ordinary callbacks PASS\n", discharge, ticks);
    teardown();
}

static void owned_teardown(void)
{
    setup(1);
    assert(em_panel_runtime_arm(em_area11_interaction_host_panel()));
    em_area11_interaction_host_camera_fields(); /* the arm's 3B8D = 3 */
    assert(!outer(0));
    assert(player_pose_owned() && em_area11_interaction_host_shared()->owner);
    teardown(); /* Hooks/pose detach before canonical owner addresses vanish. */
    setup(1);
    assert(!em_area11_interaction_host_shared()->owner && !player_pose_owned());
    assert(!outer(0));
    assert(!em_area11_interaction_host_failed());
    teardown();
    puts("AREA11 native host acquired teardown/reload PASS");
}

/* 00827B10 state 0 (0x827B54..0x827BF0): D_0081083A selects the actor's
 * +0xB4 floor and 001C6380 rebuilds its matrix. A rebuild after the ride
 * (floor byte 1) must draw the elevator at 190, where the owner and its
 * Use descriptor are; the manifest placement is always 230. */
static void elevator_state0_floor(void)
{
    for (int lower = 1; lower >= 0; --lower) {
        em_pickup_reset(); /* the D_00810700 progress reset, then the floor */
        uint8_t *floor = em_scene_progress_at(em_scene_state(), 0x0081083Au, 1);
        assert(floor);
        *floor = (uint8_t)lower;
        setup(0);
        g.elev_pos[1] = 230; /* em_scene.c's manifest placement */
        assert(em_area11_interaction_host_elevator_state0() == 0);
        const float expected = lower ? 190.0f : 230.0f;
        const EmInteractionSceneOwner *record = em_interaction_scene_role(
            em_area11_interaction_host_scene(), EM_INTERACTION_ELEVATOR);
        const EmElevator *owner = &em_area11_interaction_host_elevator()->owner;
        assert(record && g.elev_pos[1] == expected && owner->height == expected &&
               record->descriptor[1] == expected && owner->lower == lower);
        assert(owner->script_heights[0] == expected &&
               owner->script_heights[1] == (lower ? 205.0f : 245.0f) &&
               owner->script_heights[2] == (lower ? 245.0f : 205.0f));
        /* State 0 runs once, on a fresh actor: after an arm it faults. */
        em_area11_interaction_host_elevator()->owner.armed = 4;
        assert(em_area11_interaction_host_elevator_state0() < 0);
        assert(em_area11_interaction_host_failed());
        teardown();
        *floor = 0;
    }
    puts("AREA11 native host 00827B10 state 0 floor placement (190/230) PASS");
}

static void missing_sound_bank(void)
{
    setup(1);
    player_pose_set_stage_hook(NULL, NULL);
    em_area11_interaction_host_clear();
    sfx_bank_available = 0;
    assert(!em_area11_interaction_host_load("assets/scene_snow", NULL, NULL));
    assert(!em_area11_interaction_host_scene() && !sfx_selected);
    sfx_bank_available = 1;
    assert(em_area11_interaction_host_load("assets/scene_snow", NULL, NULL));
    bind_records();
    pickups_state0(); /* the failed load released its item bindings */
    assert(sfx_selected && em_sfx_cue_state(0x3EE) == 2);
    player_pose_set_stage_hook(em_area11_interaction_host_player, NULL);
    assert(!outer(0) && !em_area11_interaction_host_failed());
    teardown();
    puts("AREA11 native host missing sound bank and reload PASS");
}

static void cinematic_face(int reject_update)
{
    /* Keep the battery actually acquired by first_battery. The status pause
     * below reuses the original pickup request, which the pickup program
     * issues only after 1C40B0 has added the item; an empty inventory is not
     * a reachable request state and the real battery page rejects it. */
    setup(0);
    assert(em_pickup_item_count(0x1B) == 1 && em_pickup_battery_charge() == 12);
    EmInteractionRuntime *shared = em_area11_interaction_host_shared();
    static const unsigned owner_token = 0x8283D0;
    assert(player_pose_use_accepted_port());
    assert(em_interaction_runtime_claim(shared, &owner_token));
    em_area11_interaction_host_camera_fields(); /* the claim's 3B8D = 3 */
    assert(!outer(0) && player_pose_owned() && shared->frame->player_ready == 1);
    unsigned clip, flags; float remaining; int transition;
    assert(player_pose_source(&clip, &remaining, &flags, &transition));
    float ordinary[22 * 16]; memcpy(ordinary, g.player_palette, sizeof ordinary);
    unsigned previous_updates = face_updates;
    assert(em_area11_interaction_host_face_attach());
    assert(face_updates == previous_updates + 1 && shared->frame->player_ready == 2);
    assert(!memcmp(ordinary, g.player_palette, sizeof ordinary));
    EmGfxMesh *mesh; const float *palette; uint32_t bones; const EmModel *model;
    assert(em_area11_interaction_host_player_record(&mesh, &palette, &bones, &model) == 1);
    assert(mesh && palette == g.player_palette && bones == 22 && model != &g.model);
    assert(!model->palette && model->vert_count > g.model.vert_count);
    *em_scene_req_at(em_scene_state(), 0x008106D4u) = 0xA5; /* the mailbox D_008106D4 */
    assert(em_area11_interaction_host_face_talk(1));
    assert(em_area11_interaction_host_face_state()->talking == 1);
    assert(!outer(0)); /* 83090 ticks the attached face before the body. */
    assert(face_update_body_remaining == remaining && !player_pose_special_active());
    assert(shared->frame->activity[0] == 0xA5);
    assert(player_pose_source(&clip, &remaining, &flags, &transition));
    memcpy(ordinary, g.player_palette, sizeof ordinary);
    unsigned before = face_updates;
    fail_face_update = reject_update;
    if (reject_update) {
        assert(em_area11_interaction_host_player(NULL) == -1);
        assert(shared->owner == &owner_token && player_pose_owned() && shared->failed);
        assert(em_area11_interaction_host_failed() && shared->frame->player_ready == 2);
        assert(!memcmp(ordinary, g.player_palette, sizeof ordinary));
        float unchanged;
        assert(player_pose_source(&clip, &unchanged, &flags, &transition) && unchanged == remaining);
        assert(em_area11_interaction_host_player_record(&mesh, &palette, &bones, &model) == -1);
        fail_face_update = 0;
        teardown();
        puts("AREA11 native host retained face failure before the body PASS");
        return;
    }
    assert(!outer(0));
    assert(face_updates == before + 1 && face_update_body_remaining == remaining);
    assert(player_pose_source(&clip, &remaining, &flags, &transition) && !player_pose_special_active());
    assert(em_area11_interaction_host_face_talk(0));
    assert(!em_area11_interaction_host_face_state()->talking && shared->frame->activity[0] == 0xA5);

    /* Original status consumes the frame: neither face nor body
     * advances, even if their host service is accidentally queried. */
    EmOpeningFace paused = *em_area11_interaction_host_face_state();
    assert(em_status_runtime_pickup_request(em_area11_interaction_host_status(), 1, 0x1B));
    assert(outer(0) == 1);
    before = face_updates;
    assert(em_area11_interaction_host_player(NULL) == 0);
    assert(face_updates == before && !memcmp(&paused, em_area11_interaction_host_face_state(), sizeof paused));
    /* Clear this standalone controlled status request via the real page. */
    for (unsigned i = 1; i < 7; ++i) assert(outer(0) == 1);
    assert(outer(0x40) == 1);
    assert(outer(0x10) == 1);
    while (em_status_runtime_frame(em_area11_interaction_host_status())->phase != 1)
        assert(outer(0) == 1);

    EmScript script = {0}; unsigned char record[32] = {0};
    record[0] = 7; record[8] = 4;
    assert(em_interaction_runtime_frame(shared, &owner_token, &script, record) == EM_SCRIPT_ADVANCE);
    em_area11_interaction_host_camera_fields();
    assert(shared->frame->player_ready == 1 && !shared->frame->selector);
    assert(shared->owner == &owner_token && player_pose_owned() && !player_pose_special_active());
    assert(!em_area11_interaction_host_face_state());
    assert(em_area11_interaction_host_player_record(&mesh, &palette, &bones, &model) == 0);
    before = face_updates;
    assert(!outer(0)); /* One final body tick, then the release. */
    assert(face_updates == before && !shared->owner && !player_pose_owned());
    assert(!player_pose_special_active() && shared->frame->player_ready == 0);
    assert(player_pose_source(&clip, &remaining, &flags, &transition));
    /* No special bank was ever on the record (+0x2F3 0): 00182DF0's zero
     * branch keeps the running default clip 0 instead of re-initializing
     * it (the nonzero branch, after Roger's encounter, is compared row for
     * row by the level smoke's roger phase). */
    assert(clip == 0 && remaining > 0 && remaining < 80);
    teardown();
    puts("AREA11 native host face/status pause/frame4/default idle PASS");
}

/* A START/TRIANGLE screen (B0 == 0) on the host's page route (WP-5): the
 * scene core's 0020E060 and 0020CDC0 positions run the original hub
 * (em_status_hub with em_status_hub_ui and 0020A7A0). 0020CDC0 case 0
 * ignores a stale B1 without a request and enters the hub. Phase 1
 * sub-state 0 (one frame) calls 001AFEB0/001AFE60/0020E020, the model
 * workers and the message reset and draws nothing; each sub-state-1 frame
 * steps 0020A7A0 once and draws 00209DF0 once, whose 00208AD0 advances
 * the UI+0x20 clock the 0020E060 memset zeroed. X on hover 4 (the left
 * sector) enters ITEM (phase 3, screen 0) through the page core, whose
 * Back (0x63) returns to the hub through phase 4; the ITEM atlas and the
 * hub atlas share the UI texture slot, so each switch uploads again. The
 * close edge (0x830) enters phase 5 on a frame that still draws the hub;
 * 0020E0C0 then runs case 0 (D_008106CC = 1, no module reload for this
 * inventory) and returns nonzero from case 2 on the next frame, where
 * 0020E080 has cleared B0 and C5: the original's two-frame close latency
 * (status_04: TRIANGLE f200 -> +B = 5 at f204, START f10 -> +B = 3 at
 * f12). X on hovers 1..3 selects MAP/SPR4/DATABASE, which are not
 * translated: the page core faults. */
static int hub_frame(const EmStatusInput *input)
{
    int result = em_area11_interaction_host_status_page(input);
    hub_rendering = 1;
    assert(em_area11_interaction_host_status_render((EmGfx *)1) == 1);
    /* A second draw in the same frame steps nothing. */
    unsigned steps = background_steps;
    assert(em_area11_interaction_host_status_render((EmGfx *)1) == 1 && background_steps == steps);
    hub_rendering = 0;
    return result;
}

static void status_hub_route(void)
{
    setup(1);
    EmSceneState *scene = em_scene_state();
    scene->req[EM_SCENE_REQ_B1] = 0x82;
    scene->req[EM_SCENE_REQ_C5] = 0;
    uint8_t *cc = em_scene_req_at(scene, 0x008106CCu);
    *cc = 0;
    EmStatusRuntime *status = em_area11_interaction_host_status();
    const EmStatusPage *page = em_status_runtime_page(status);
    EmStatusInput input = {.stick_x = 128, .stick_y = 128};
    assert(em_area11_interaction_host_status_open() == 1 && em_status_runtime_ui_clock(status) == 0);
    unsigned steps = background_steps, frames = background_frames, loaded = uploads;
    unsigned draws = model_draws;
    assert(hub_frame(&input) == 0 && page->phase == 1 && page->step == 0);
    assert(background_steps == steps && background_frames == frames && uploads == loaded);
    assert(hub_frame(&input) == 0 && page->phase == 1 && page->step == 1 &&
           page->item.message_mode == 4 && !page->item.message_phase);
    assert(background_steps == steps && background_frames == frames && uploads == loaded);
    for (unsigned i = 1; i <= 3; ++i) {
        assert(hub_frame(&input) == 0 && page->phase == 1 && page->step == 1);
        assert(background_steps == steps + i && background_frames == frames + i);
        assert(em_status_runtime_ui_clock(status) == i && uploads == loaded + 1);
        /* The menu player and six letters (CA4..CA7 = FF 05 00 07): the
         * first walk initialises them, every later one draws each once. */
        assert(model_draws == draws + 7 * (i - 1));
    }
    /* Hover 4 (left) and X: 0020CD40, phase 3 on ITEM. */
    input.stick_x = 0;
    assert(hub_frame(&input) == 0 && page->phase == 1 && page->item.hover == 4);
    input.pressed = 0x40;
    assert(hub_frame(&input) == 0 && page->phase == 3 && page->step == 0 &&
           page->item.screen == 0);
    input.pressed = 0;
    input.stick_x = 128;
    for (int i = 0; i < 8 && page->phase == 3 && page->step != 2; ++i)
        assert(hub_frame(&input) == 0);
    assert(page->phase == 3 && page->step == 2);
    for (int i = 0; i < 8 && uploads == loaded + 1; ++i)
        assert(hub_frame(&input) == 0 && page->phase == 3);
    assert(uploads == loaded + 2); /* the ITEM atlas replaced the hub's */
    /* ITEM Back returns to the hub (0x63 -> phase 4 -> phase 1). */
    input.pressed = 0x20;
    for (int i = 0; i < 8 && page->phase == 3; ++i) {
        assert(hub_frame(&input) == 0);
        input.pressed = 0;
    }
    for (int i = 0; i < 8 && !(page->phase == 1 && page->step == 1); ++i)
        assert(hub_frame(&input) == 0);
    assert(page->phase == 1 && page->step == 1);
    steps = background_steps;
    assert(hub_frame(&input) == 0 && page->phase == 1 && background_steps == steps + 1);
    assert(uploads == loaded + 3); /* and the hub atlas again */
    input.pressed = 0x800;
    assert(hub_frame(&input) == 0 && page->phase == 5 && background_steps == steps + 2);
    input.pressed = 0;
    assert(hub_frame(&input) == 0 && *cc == 1 && background_steps == steps + 2);
    assert(hub_frame(&input) == 1 && page->phase == 0 &&
           !scene->req[EM_SCENE_REQ_B0] && !scene->req[EM_SCENE_REQ_C5]);
    /* X on hover 3 (up): MAP, not translated. */
    assert(em_area11_interaction_host_status_open() == 1 && em_status_runtime_ui_clock(status) == 0);
    assert(hub_frame(&input) == 0 && hub_frame(&input) == 0 && page->step == 1);
    input.stick_y = 0;
    input.pressed = 0x40;
    assert(hub_frame(&input) == 0 && page->phase == 3 && page->item.screen == 1);
    input.pressed = 0;
    assert(em_area11_interaction_host_status_page(&input) == 0 && page->step == 1);
    assert(em_area11_interaction_host_status_page(&input) == -1);
    teardown();
    puts("AREA11 native host original START/TRIANGLE hub: sub-state 0 draws nothing, one "
         "0020A7A0 step, the backdrop flushed before the seven model draws and 00209DF0 per "
         "hub frame, ITEM and back, 0020E0C0 exit latency, untranslated pages fault PASS");
}

int main(void)
{
    status_hub_route();
    no_battery();
    first_battery();
    cinematic_face(0);
    cinematic_face(1);
    panel_menu(0);
    panel_menu(1);
    owned_teardown();
    elevator_state0_floor();
    missing_sound_bank();
    /* Last: each resets the inventory the scenarios above share. */
    other_take(0x0B04, 1, 0x1E);
    other_take(0x0B07, 3, 0x32);
    other_take(0x0B08, 1, 0x10);
    other_take(0x0B09, 2, 0x08);
    puts("AREA11 native interaction host PASS");
    return 0;
}
