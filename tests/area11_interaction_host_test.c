/* Actual AREA11 host integration. GPU/audio devices, ordinary follow-camera
 * evolution and prop drawing are explicit outer boundaries. Player channels,
 * collision retarget, script/owner timing, inventory, status/UI and projection
 * all use the real implementations and exported original assets. Owner claims
 * and the previous publication list are fixture inputs; this does not test
 * the outer game's Use-input/culling arbitration. */
#include "game/em_area11_interaction_host.h"
#include "game/em_module_loader.h"
#include "game/em_task.h"
#include "game/em_area11_roger.h"
#include "game/em_area11_boxes.h"
#include "game/em_face_slot.h"
#include "game/em_camera.h"
#include "game/em_camera_live.h"
#include "game/em_collision_world.h"
#include "game/em_point_light.h"
#include "game/em_status_models.h"
#include "game/em_render_context_live.h"
#include "game/em_sdk_math_original.h"
#include "game/em_ee_float.h"
#include "game/em_effect_color.h"
#include "game/em_hud.h"
#include "game/em_message_presenters_live.h"
#include "game/em_pickup.h"
#include "game/em_player.h"
#include "game/em_player_stage_workers.h"
#include "game/em_locomotion_display.h"
#include "game/em_player_ladder_climb.h"
#include "game/em_player_record_pose.h"
#include "game/em_pose_host_workers.h"
#include "game/em_pickup_original.h"
#include "game/em_pickup_motion.h"
#include "game/em_random.h"
#include "game/em_scene_bindings.h"
#include "game/em_security_gun_rest.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int idle_stop_lane(void *ctx, int lane) { (void)ctx; return lane == 1 || lane == 2; }

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
/* The live record D_008102B0 (em_player.c in the game): the face slot's
 * +0x90 / +0x94 (em_area11_interaction_host_player_face). */
EmPlayerLiveActor *player_states_actor_mut(void) { return &player_record; }
static int camera_no_timeline(void *ctx) { (void)ctx; return -1; }
static int camera_hip(void *ctx, float out[3]) { (void)ctx; return player_pose_hip(out); }
static int camera_euler(void *ctx, float out[3]) { (void)ctx; return player_pose_script_euler(out); }
static const EmCameraLiveHost camera_host = {NULL, camera_player, camera_hip, camera_euler, NULL, &carry31F0,
                                             camera_no_timeline,
                                             /* the aim camera's views: not reached here */
                                             NULL, NULL, NULL,
                                             /* the area camera worker (AREA01 only) */
                                             NULL};
const float kLocoTierSpeed[4] = {0};
static unsigned uploads, triangles, sounds, resumes, indicators, status_requests;
static unsigned background_steps, background_frames;
static int sfx_selected, sfx_bank_available = 1;
/* The one 001AF710 stack and slot arena (em_area11_boxes' in the game): a
 * small stand-in built as 001AF710 builds it (each slot's address on the
 * stack, the cursor at its base, the count the slot total). */
enum { SLOTS = 64 };
static uint8_t slot_records[SLOTS * EM_ROGER_ACTOR_SLOT_BYTES];
static uint32_t slot_stack[SLOTS];
static int16_t slot_count;
static uint32_t slot_cursor;
static EmRogerActorWorld slot_world;
#define SLOT_RECORDS 0x007D5840u
#define SLOT_STACK 0x007D4640u
static void slots_reset(int16_t free_slots)
{
    memset(slot_records, 0, sizeof slot_records);
    for (unsigned i = 0; i < SLOTS; ++i) slot_stack[i] = SLOT_RECORDS + EM_ROGER_ACTOR_SLOT_BYTES * i;
    slot_count = free_slots;
    slot_cursor = SLOT_STACK;
    memset(&slot_world, 0, sizeof slot_world);
    slot_world.d00275BCC = &slot_count;
    slot_world.d00275BD0 = &slot_cursor;
    slot_world.slot_stack = slot_stack;
    slot_world.slot_stack_base = SLOT_STACK;
    slot_world.slot_stack_words = SLOTS;
    slot_world.slots = slot_records;
    slot_world.slots_base = SLOT_RECORDS;
    slot_world.slots_size = sizeof slot_records;
}
const EmRogerActorWorld *em_area11_boxes_slot_world(void) { return &slot_world; }
static uint8_t *slot_at(uint32_t address)
{
    assert(address >= SLOT_RECORDS && address < SLOT_RECORDS + sizeof slot_records);
    return slot_records + (address - SLOT_RECORDS);
}
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
/* em_weapon's C61 / C62 / CB4 storage the status pages view. */
static uint8_t fixture_mag, fixture_mode;
static int16_t fixture_reserve = 60;
uint8_t *em_weapon_mag_byte(void) { return &fixture_mag; }
int16_t *em_weapon_reserve_word(void) { return &fixture_reserve; }
uint8_t em_weapon_fire_mode(void) { return fixture_mode; }
void em_weapon_set_fire_mode(uint8_t mode) { if (mode < 3) fixture_mode = mode; }
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
/* The stream calls the host's status frame and interaction frame make
 * (em_scene_bindings.c binds them to the stream lanes, em_stream_live; the
 * lanes have their own tests): the status open's 001FBC50, 001FABB0 and
 * 00119828(0/1, 0x3FFF, 0x3FFF), the close's 001FAE70(1). */
static unsigned stream_stops, channel_sets;
int em_scene_bindings_001FBC50(void) { return 0; }
/* The status open's request 6 (the AREA01 terminal) is not reached by these
 * fixtures: the scene's terminal views fault if it is. */
int em_scene_bindings_terminal_owner_read(uint32_t owner, uint32_t offset, uint32_t size, int32_t *value)
{ (void)owner; (void)offset; (void)size; (void)value; return -1; }
int em_scene_bindings_terminal_reset(void) { return -1; }
int em_scene_bindings_001FABB0(void) { ++stream_stops; return 0; }
int em_scene_bindings_00119828(void *ctx, int32_t ch, int32_t l, int32_t r)
{
    (void)ctx;
    assert((ch == 0 || ch == 1) && l == 0x3FFF && r == 0x3FFF);
    ++channel_sets;
    return 0;
}
int em_scene_bindings_001FAE70(int a0) { assert(a0 == 1); ++resumes; return 0; }
/* 1: the +0x20 child existed and was stopped; 0: the slot was empty
 * (00159210 case 2 skips both stores when +0x20 == 0). */
static int panel_child_slot = 1;
int em_area11_bindings_panel_child_stop(void) { ++indicators; return panel_child_slot; }
/* 001B1190 (em_area11_bindings_001B1190 in the game, not linked here): the
 * same one translation, em_gun_rest_001B1190, over D_00810700 and the
 * canonical D_00810860 rows of this fixture's scene. */
static int persists;
static const uint8_t *taken_load(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    if (address == 0x00810700u && size == 1) return &em_scene_state()->d810700;
    return em_scene_progress_at(em_scene_state(), address, size);
}
static uint8_t *taken_store(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    return em_scene_progress_at(em_scene_state(), address, size);
}
int em_area11_bindings_001B1190(int32_t a0)
{
    const EmGunRestMem mem = {NULL, taken_load, taken_store};
    EmGunFault fault = {0, 0};
    ++persists;
    return em_gun_rest_001B1190(a0, &mem, &fault);
}
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
/* The MAP page's draw stream (em_page_draw): its 2D layer is drawn before
 * the UI pool's models (001B0000), which the SCISSOR_1 window clips. */
static unsigned decor_flushes, scissor_sets;
void em_gfx_overlay_decor_flush(EmGfx *gfx) { assert(gfx); ++decor_flushes; flushed_frame = background_frames; }
void em_gfx_draw_scissor(EmGfx *gfx, const float rect[4]) { assert(gfx); (void)rect; ++scissor_sets; }
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
        record->model = owner->role == EM_INTERACTION_PICKUP ? owner->subtype : 0;
        record->param = owner->role != EM_INTERACTION_PICKUP ? 0
                        : owner->callback == 0x0015AFA0u ? 0x0B : 0x72;
        record->uid = 0xFF00;
        record->self = record;
        record->source_id = owner->source_id;
        memcpy(record->pos, owner->position, sizeof owner->position);
        memcpy(record->rot, owner->angles, sizeof owner->angles);
        for (unsigned k = 0; k < 4; ++k) record->f60[k] = 1.0f;
        assert(em_area11_interaction_host_bind_actor(owner->source_id, record) == 0);
    }
}

/* Each bound item node's first call (state 0), after a host load: the
 * record's +0x03 / +0x0D (the fixture's records carry the EMIS subtype and
 * the item's library model 0x72 / map model 0x0B). */
static void pickups_state0(void)
{
    EmInteractionScene *scene_owners = em_area11_interaction_host_scene();
    for (size_t i = 0; i < scene_owners->count; ++i) {
        const EmInteractionSceneOwner *record = &scene_owners->owners[i];
        if (record->role == EM_INTERACTION_PICKUP && record->native_owner)
            assert(em_area11_interaction_host_pickup_state0(record->source_id, records[i].model,
                                                            records[i].param) == 0);
    }
}

static EmActor *record_of(EmInteractionRole role)
{
    EmInteractionScene *scene_owners = em_area11_interaction_host_scene();
    for (size_t i = 0; scene_owners && i < scene_owners->count; ++i)
        if (scene_owners->owners[i].role == role) return &records[i];
    return NULL;
}

/* The owners' record services (the game binds em_area11_boxes' world owners
 * over the model bank, the bone-slot stack and the object-unit draw; the
 * level smoke compares their units and node matrices with the route
 * captures). The fixture counts the host's calls: every model bind, every
 * 001C6380 (its +0xD0 the record's TRS), every +0x4C and every 0x827E6C
 * copy, per record. */
static unsigned owner_binds, owner_places[EM_INTERACTION_CAPACITY], owner_draws[EM_INTERACTION_CAPACITY],
    child_copies;
static int fx_bind_001B0FD0(EmActor *a, int32_t *ret)
{
    assert(a >= records && a < records + EM_INTERACTION_CAPACITY);
    ++owner_binds;
    a->u04[0] += 1;
    *ret = 0;
    return 0;
}
static int fx_bind_001B1020(EmActor *a, uint32_t a1, int32_t a2, int32_t a3, int32_t *ret)
{
    assert(a >= records && a < records + EM_INTERACTION_CAPACITY && a1 == a->param && a2 == -1 && a3 == 0);
    ++owner_binds;
    a->u04[0] += 1;
    *ret = 0;
    return 0;
}
static int fx_place(EmActor *a, float world[16])
{
    assert(a >= records && a < records + EM_INTERACTION_CAPACITY && world);
    ++owner_places[a - records];
    return em_owner_services_build_trs_matrix(world, a->pos, a->rot, a->f60) == 0 ? 0 : -1;
}
static int fx_draw(EmActor *a)
{
    assert(a >= records && a < records + EM_INTERACTION_CAPACITY);
    ++owner_draws[a - records];
    return 0;
}
static int fx_copy_child(EmActor *a)
{
    assert(a == record_of(EM_INTERACTION_ELEVATOR));
    assert(em_area11_interaction_host_elevator()->owner.phase == 1 ||
           !em_area11_interaction_host_elevator()->owner.armed);   /* phase 1, or its completion */
    ++child_copies;
    return 0;
}

static int rcl_sdk_sqrt(void *ctx, uint32_t x, uint32_t *out)
{
    (void)ctx;
    EmSdkMathContext *m = em_collision_world_sdk();
    float r;
    uint32_t f = 0;
    if (!m || em_sdk_math_original_0011E748(m->tables, &m->world, &m->workers, em_ee_float(x), &r, &f) < 0)
        return -1;
    *out = em_ee_bits(r);
    return 0;
}

static int rcl_sdk_tan(void *ctx, uint32_t x, uint32_t *out)
{
    (void)ctx;
    EmSdkMathContext *m = em_collision_world_sdk();
    float r;
    uint32_t f = 0;
    if (!m || em_sdk_math_original_0011E398(m->tables, em_ee_float(x), &r, &f) < 0) return -1;
    *out = em_ee_bits(r);
    return 0;
}

/* The fixture runs no frame head and no area render init: reaching them
 * is a failure. */
static int rcl_unreached(void *ctx) { (void)ctx; return -1; }
static int rcl_unreached_block(void *ctx, uint32_t block) { (void)ctx; (void)block; return -1; }

/* The bank address word D_0028A5A0 (the loader's slot 0x44): 0, no bank;
 * 001C1D00 is not reached by these cases. */
static uint8_t rcl_bank_word[4];

static void rcl_bind_fixture(void)
{
    EmSceneState *scene = em_scene_state();
    const EmRclExternal views[] = {
        {0x00810610u, 0x40u, em_camera_live_bytes(0x00810610u, 0x40u)},
        {0x008105E0u, 0x10u, em_camera_live_bytes(0x008105E0u, 0x10u)},
        {EM_SCENE_REQ_BASE, EM_SCENE_REQ_SIZE, scene->req},
        {0x00810700u, 3u, &scene->d810700},
        {0x008101E4u, 1u, &scene->d8101E4},
        {0x70003B8Du, 1u, &scene->spad3B8D},
        {0x008102B0u, 0x320u, (uint8_t *)(uintptr_t)em_camera_live_player_bytes()},
        {0x0028A5A0u, 4u, rcl_bank_word},
    };
    static const EmRclWorkers workers = {NULL, rcl_unreached, rcl_sdk_sqrt, rcl_sdk_tan,
                                         rcl_unreached_block};
    assert(em_rcl_bind(views, sizeof views / sizeof views[0], &workers) == 0);
}

static float rcl_float(uint32_t offset)
{
    float v;
    memcpy(&v, em_rcl_bytes(EM_RCL_CONTEXT + offset, 4), 4);
    return v;
}

/* The player stage's own takeover, composed as em_player.c and
 * em_player_stage_live.c compose it over the live record (the game's; not
 * linked here): 0015B130's prelude (em_player_stage_0015B130: 00182B30,
 * +4 = 4, 00174A50(8.0), 00182D70), 0015BA50's +4 = 4 path (00183090
 * em_player_stage_commit, the advance through player_pose_stage_advance by
 * +1F4) and 0015B530 (001837A0 empty; 00182DF0 through
 * player_pose_stage_release, whose end hook ends the host's token).
 * 00182DF0 on the record: the one release (em_player_stage_00182DF0), over
 * the pose's own callees, with the row
 * lookup 0017B490 as em_loco_0017B490 over the record pose's tables
 * (D_008106C8 = 0 for 001B0070). The record's +0x1C is 0 (no link); the
 * special branch's words are the attach's (the bank, 21 nodes). */
static EmPlayerStageHost release_host;
static EmPlayerStageRelease release_context;
static EmLocoHost release_loco;
static int release_mode(void *c, int32_t *value) { (void)c; *value = 0; return 0; }
static int release_lookup(void *c, EmPlayerLiveActor *a, int a1, int a2, int a3, int16_t *clip)
{
    (void)c;
    return em_loco_0017B490(&release_loco, a, a1, a2, a3, clip);
}
static int release_link(void *c, uint32_t word, uint8_t value) { (void)c; (void)word; (void)value; return -1; }
static int release_bank(void *c, uint32_t *word) { (void)c; *word = EM_PLAYER_POSE_BANK_ADDRESS; return 0; }
static int release_nodes(void *c, uint32_t model, uint8_t *count)
{
    (void)c; (void)model;
    *count = EM_PLAYER_POSE_NODES;
    return 0;
}
static int release_a00(void *c, unsigned index, int16_t *clip)
{
    (void)c;
    const uint8_t *p = player_pose_record_bytes(0x00248A00u + 2u * index, 2);
    if (!p) return -1;
    *clip = (int16_t)(uint16_t)(p[0] | p[1] << 8);
    return 0;
}
static int release_c90(void *c, int clip, int16_t *value) { (void)c; return player_pose_row0(clip, value); }
/* 00174AB0: its one translation (em_player_ladder_climb's) over the pose's
 * 001749A0. */
static int release_request(void *c, EmPlayerLiveActor *a, int clip, int flags, float blend)
{
    (void)c;
    return em_pose_host_stage_request(player_pose_record_host(), a, clip, flags, blend);
}
static int release_00174AB0(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    EmPlayerLadderClimbWorkers workers;
    memset(&workers, 0, sizeof workers);
    workers.request = release_request;
    EmPlayerLadderClimb ladder = { &workers, NULL };
    return em_player_ladder_climb_00174AB0(&ladder, a);
}
static EmPlayerStageWorkers stage_workers;
static EmPlayerStageMajor4 stage_major4;
static EmPlayerStage stage_context;
static int stage_face(void *c) { (void)c; return em_area11_interaction_host_face_tick_001D0C70(); }
static int stage_advance_worker(void *c, EmPlayerLiveActor *a, float step, uint32_t *flags)
{
    (void)c; (void)a;
    return player_pose_stage_advance(step, flags);
}
static int stage_001837A0(void *c, EmPlayerLiveActor *a) { (void)c; (void)a; return 0; }
static int stage_unbound(void *c, EmPlayerLiveActor *a) { (void)c; (void)a; return -1; }
static int stage_00182DF0(void *c, EmPlayerLiveActor *a) { (void)c; return player_pose_stage_release(a); }

static void bind_release(EmPlayerStageScene *stage, EmPlayerStageGlobals *globals)
{
    memset(&release_loco, 0, sizeof release_loco);
    release_loco.workers.mode = release_mode;
    release_loco.display.pose = player_pose_record_host();
    memset(&release_host, 0, sizeof release_host);
    release_host.stage = stage;
    release_host.globals = globals;
    EmPlayerStageCallees *c = &release_host.callees;
    c->context = player_pose_record_host();
    c->bone_init = em_pose_host_stage_bone_init;
    c->clip_init = em_pose_host_stage_clip_init;
    c->request = em_pose_host_stage_request;
    c->clip_lookup = release_lookup;
    c->link1C = release_link;
    c->w001D0C70 = stage_face;
    release_context = (EmPlayerStageRelease){ &release_host, NULL, release_bank, release_nodes, release_a00,
                                              release_c90, release_00174AB0 };
    player_pose_set_release_worker(em_player_stage_00182DF0, &release_context);
    memset(&stage_workers, 0, sizeof stage_workers);
    em_player_stage_workers_bind(&stage_workers, &release_host);
    stage_workers.advance = stage_advance_worker;
    memset(&stage_major4, 0, sizeof stage_major4);
    stage_major4.stage = stage;
    for (int i = 0; i < EM_PLAYER_MAJOR4_COUNT; ++i) stage_major4.routine[i] = stage_unbound;
    stage_major4.routine[EM_PLAYER_MAJOR4_001837A0] = stage_001837A0;
    stage_major4.routine[EM_PLAYER_MAJOR4_00182DF0] = stage_00182DF0;
    stage_workers.major[4] = em_player_stage_0015B530;
    stage_workers.major_context[4] = &stage_major4;
    stage_context = (EmPlayerStage){ stage, &stage_workers };
    player_pose_set_takeover_end_hook(em_area11_interaction_host_staged_released, NULL);
}

/* One player stage of the fixture: on +4 = 1, 0015BA50's advance, then the
 * host's hook at 0015B130's prelude position (2 while an owner holds the
 * token: the prelude admits the player); on +4 = 4, the takeover's path.
 * The stage's scratch bytes 0x70003B8D / 3B8F are the scene state's.
 * Returns the hook's result on +4 = 1 (0 or 2), 4 on a held stage, -1 on a
 * fault. */
static int player_stage(void)
{
    EmSceneState *scene = em_scene_state();
    EmPlayerStageScene *stage = stage_context.scene;
    stage->spad3B8D = scene->spad3B8D;
    stage->spad3B8F = scene->spad3B8F;
    int result;
    if (player_record.bytes[4] == 4) {
        result = em_player_stage_dispatch(&player_record, &stage_workers) < 0 ? -1 : 4;
    } else {
        if (player_pose_stage_advance(g.loco_rate, NULL) < 0) return -1;
        result = player_pose_stage_hook();
        if (result == 2) {
            stage->spad3B8D = scene->spad3B8D;
            if (!player_pose_takeover_prepare() ||
                em_player_stage_0015B130(&stage_context, &player_record) < 0 ||
                player_record.bytes[4] != 4 || !player_pose_takeover_admitted())
                return -1;
        }
    }
    scene->spad3B8F = stage->spad3B8F;
    scene->spad3B8D = stage->spad3B8D;
    return result;
}

static int shared_only_fixture, shared_bank_calls;
static int shared_fixture_banks(void *ctx)
{ assert(ctx==&shared_bank_calls);++shared_bank_calls;return 0; }
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
    g.grate_present = 1;
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
    g.cam.horiz_dist = word(ram, 0x810690);
    /* The render context (census L32 / L30): the capture's zoom, the views
     * and workers the game's binder gives it (em_scene_bindings.c
     * rcl_bind): the host's zoom stores, 001D2610's fog pair and the
     * 001DD980 publications land there. */
    assert(em_rcl_init(EM_RCL_EXPORT_PATH, em_frame_d810E80()) == 0);
    assert(em_rcl_poke(EM_RCL_CONTEXT + 0x2468, ram + EM_RCL_CONTEXT + 0x2468, 4) == 0);
    rcl_bind_fixture();
    /* The area's point lights on the context's +0x210.. pool: the capture's
     * own pool (in the game the area entry's 001D19E0 -> 001D7BB0 registers
     * the room lists, em_effects_live_room_lights): the status models' draws
     * (001CB4F0, 001CB480) light through 001D89D0. */
    if (!g.point_lights_loaded) {
        assert(em_rcl_point_lights() &&
               em_rcl_poke(EM_RCL_CONTEXT + 0x210, ram + EM_RCL_CONTEXT + 0x210, 0x2010) == 0);
        g.point_lights_loaded = 1;
    }
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
    /* +0x220, the health em_player's vitals view loads (00182B30 refuses a
     * dead player). */
    em_live_set_f32(&player_record, 0x220, 100.0f);
    assert(player_pose_load(PLAYER_CLIP_BANK_PATH, PLAYER_CLIP_ROW0_PATH));
    assert(player_pose_attach(&player_record, em_scene_req_at(scene, 0x008106F3u), &stage_scene,
                              &stage_globals));
    assert(player_pose_record_displayed());   /* the source is the record from the attach */
    bind_release(&stage_scene, &stage_globals);
    player_pose_finish_palette();
    /* The live message service (step F) the panel and terminal lines run on. */
    assert(em_message_live_install("assets/message/message_data.emmd"));
    /* No voiced line here: the voice lanes stay idle (the lanes' 001FAAC0 on
     * an idle lane has no effect; D_00282155/156 read 0). */
    static const EmMessageLiveStreams idle_lanes = {NULL, NULL, NULL, NULL, idle_stop_lane, NULL};
    em_message_live_set_streams(&idle_lanes);
    assert(em_message_live_reset() == 0);
    /* Its mode-3 / mode-4 presenters (the status pages' lines, 001FCF10). */
    assert(em_message_presenters_live_install(EM_MESSAGE_PRESENTERS_PATH));
    /* The placed items (em_scene's manifest pickups) the host binds; a
     * taken item is not placed (-2). */
    static EmInteractionScene placed;
    assert(em_interaction_scene_load(&placed, "assets/scene_snow/interaction.emis"));
    for (size_t i = 0; i < placed.count; ++i)
        if (placed.owners[i].role == EM_INTERACTION_PICKUP)
            assert(em_pickup_add(NULL, "assets/scene_snow", (int)placed.owners[i].item_type,
                                 placed.owners[i].position, placed.owners[i].angles[1],
                                 placed.owners[i].uid, NULL, 0) != -1);
    static const EmArea11HostOwnerHooks owner_hooks = {fx_bind_001B0FD0, fx_bind_001B1020, fx_place, fx_draw,
                                                       fx_copy_child};
    em_area11_interaction_host_set_owner_hooks(&owner_hooks);
    owner_binds = child_copies = 0;
    memset(owner_places, 0, sizeof owner_places);
    memset(owner_draws, 0, sizeof owner_draws);
    /* Module 0x21's load runs the live screen-module loader's own steps
     * (docs/MODULE_LOADER.md): the game boots it (em_scene_bindings); the
     * fixture binds one over the scene state's D_00275BD8 and dispatches
     * the task table after each status frame (step E). */
    if (!em_module_loader_live()) {
        EmModuleLoader *loader = em_module_loader_open("assets/module_loader/modules.emml");
        assert(loader);
        const EmModuleLoaderViews views = {.d275BD8 = &em_scene_state()->d275BD8};
        em_module_loader_set_views(loader, &views);
        em_module_loader_bind_live(loader);
    }
    em_task_init();
    em_scene_state()->d275BD8 = 0;
    if(shared_only_fixture) {
        assert(em_area11_interaction_host_load_shared("assets/scene_snow",&placed.math,
                                                      shared_fixture_banks,&shared_bank_calls));
        assert(em_area11_interaction_host_math()==&em_area11_interaction_host_scene()->math);
        assert(!em_area11_interaction_host_scene()->count && !sfx_selected);
    } else assert(em_area11_interaction_host_load("assets/scene_snow", NULL, NULL));
    em_message_live_set_host(em_area11_interaction_host_message_host());
    if(!shared_only_fixture) {
        bind_records();pickups_state0();assert(sfx_selected);
        em_area11_interaction_host_set_panel_address(PANEL_ADDRESS);
    }
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
    em_message_presenters_live_shutdown();
    em_message_live_shutdown();
    assert(!sfx_selected);
    assert(!em_area11_interaction_host_shared() && !player_pose_owned());
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

/* One frame of the scene core's page route (em_area11_interaction_host_
 * status_page), then the frame loop's step E: the task table, whose slot 2
 * runs the screen-module loader every page module loads through (the
 * page waits on D_00275BD8, which the loader's 0x63 step clears). */
static int page_frame(const EmStatusInput *input)
{
    int result = em_area11_interaction_host_status_page(input);
    em_task_dispatch();
    assert(!em_module_loader_failed(em_module_loader_live(), NULL));
    return result;
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
    EmPlayerLiveActor record = player_record;
    bridge_status_request();
    int result = em_status_runtime_tick(status, &input);
    em_task_dispatch();
    assert(!em_module_loader_failed(em_module_loader_live(), NULL));
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
        assert(!memcmp(&record, &player_record, sizeof record));
        assert(em_status_runtime_render(status, (EmGfx *)1) == 1);
        unsigned old_triangles = triangles;
        assert(em_status_runtime_render(status, (EmGfx *)1) == 1 && triangles == old_triangles);
        return 1;
    }
    ++g.frame_no;
    assert(player_stage() >= 0);
    player_pose_finish_palette();
    EmPanel *panel = &em_area11_interaction_host_panel()->owner;
    int aligning = panel->phase == 0 && (panel->armed & 4);
    float old_feet[3], old_hip[3];
    memcpy(old_feet, g.pos, sizeof old_feet);
    assert(player_pose_hip(old_hip));
    EmActor *panel_record = record_of(EM_INTERACTION_PANEL), *terminal_record = record_of(EM_INTERACTION_ELEVATOR);
    const unsigned panel_drawn = owner_draws[panel_record - records];
    const unsigned terminal_drawn = owner_draws[terminal_record - records];
    assert(!em_area11_interaction_host_panel_tick());
    /* 00159210 state 1 ends with 001B17A0 and its +0x4C on every call. */
    assert(owner_draws[panel_record - records] == panel_drawn + 1);
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
    /* So does 00827B10's (0x827E78 / 0x827E84), and its +0xB4 is the
     * owner's height (the floor, or the carry 00828050's). */
    assert(owner_draws[terminal_record - records] == terminal_drawn + 1);
    assert(terminal_record->pos[1] == em_area11_interaction_host_elevator()->owner.height);
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
    const int persisted = persists;
    assert(!em_pickup_taken(record->uid));
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
    /* The ITEM root's module-0x21 load takes the loader's 10 host-speed
     * dispatches (MODULE_LOADER.md finding 2): 9 frames more than the
     * instant load this fixture counted before (7). */
    for (unsigned i = 1; i < 16; ++i) assert(outer(0) == 1);
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
    assert(isfinite(rcl_float(0x2460)));   /* 001DD950's +0x2460 */
    do { assert(!outer(0)); assert(++ticks < 520); } while (shared->owner);
    assert(!player_pose_owned() && owner->lifecycle == 3 && !powered());
    /* 00219550's 001B1190(+0x9A) at the take's end: the battery's taken bit. */
    assert(persists == persisted + 1 && em_pickup_taken(record->uid));
    printf("AREA11 native host first battery: %u callbacks, status release70 PASS\n", ticks);
    teardown();
}

/* A MAP bank model's directory offset (assets/status_map/map_models.emmp,
 * tools/export_status_map.py). */
static uint32_t map_offset(uint32_t code)
{
    FILE *f = fopen("assets/status_map/map_models.emmp", "rb");
    uint32_t header[3], entry[4], found = 0;
    assert(f && fread(header, sizeof header, 1, f) == 1 && header[2] == 22);
    for (uint32_t i = 0; i < header[2] && !found; ++i) {
        assert(fread(entry, sizeof entry, 1, f) == 1);
        if (entry[0] == code) found = entry[1];
        assert(!fseek(f, (long)entry[2] * 0x44, SEEK_CUR));
    }
    fclose(f);
    assert(found);
    return found;
}

/* The other AREA11 takes (001B6EA0 families): each posts its original
 * request (001C47A0 B0 = 1 / 001C4720 B0 = 2 / 001C4760 B0 = 3, B1 = the
 * type) after its inventory write, and 0020CDC0 opens the page it maps the
 * request to (em_status_pages_live): HEALING, DATABASE, SPR4 or MAP. */
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
    /* 0020CDC0's cold entry maps the request (the page core's own test
     * checks the map). A bound page (em_status_pages_live) shows the take's
     * notice until its countdown or a press ends it and clears B0; START
     * then closes the screen (phase 3's 0x810 edge). A page that is not
     * bound yet faults at its module load or tick, with a report. */
    const EmStatusPage *page = em_status_runtime_page(em_area11_interaction_host_status());
    const int bound = type == 0x1E || type == 0x32 || type == 0x10 || type == 0x08;
    unsigned page_ticks = 0;
    int page_result;
    if (bound) {
        /* The page takes the request (B0 cleared: 002160B0 / 00214020 state
         * 0, 00211970's notice). HEALING's notice then counts 240 calls
         * (002160B0 state 0 sets t[6] = 0xF0, state 3 counts it down once a
         * call and hands over to the list at 0). */
        while ((page_result = page_frame(&input)) == 0 &&
               state->req[EM_SCENE_REQ_B0])
            assert(++page_ticks < 32); /* the page module's 10 loader dispatches first */
        assert(page_result == 0 && page->phase == 3);
        unsigned notice = 0;
        if (type == 0x1E) {
            assert(page->item.screen == 0 && page->item.state == 7 && page->item.step == 3);
            while ((page_result = page_frame(&input)) == 0 &&
                   page->item.step == 3)
                assert(++notice < 400);
            ++notice;
            assert(page_result == 0 && page->item.step == 1 && notice == 240);
        } else if (type == 0x08) {
            /* MAP (0020F950 mode 0 with the request: map D_008106B1 = 8,
             * zoomed, t[3] = 2): its 22 nodes run 002101C0; map 8's two
             * (p[0xD] 0 and 1, the selected map) bind the bank's codes 8
             * and 0x13 (001C6120(D_0028A570, ...), 001CA5E0(p, model, 7):
             * +0x4C = 001CB480) and run (+4 = 1); the other maps' nodes
             * bind nothing (D_00810CB8[i] clear). */
            assert(page->item.screen == 1);
            assert(page_frame(&input) == 0);
            const EmStatusScenePool *pool =
                em_status_models_pool(em_area11_interaction_host_status_models());
            unsigned nodes = 0, bound_models = 0;
            for (unsigned r = 0; r < EM_STATUS_SCENE_POOL_RECORDS; ++r) {
                const EmStatusSceneActor *a = &pool->record[r];
                if (!a->b00 || a->w10 != 0x002101C0u) continue;
                ++nodes;
                assert(a->b04 == 1 && (a->b02 & 0x40));
                if (a->b03 == 8) {
                    /* +0x44 is 001C6120's result: D_0028A570 (0x019A3F40)
                     * + the code's directory offset, the value
                     * tools/test_status_map_reference.py proves against
                     * the original 001C6120 */
                    assert(a->w44 == 0x019A3F40u + map_offset(a->b0D ? 0x13 : 8));
                    assert(a->w4C == 0x001CB480u && a->b0C == 1 && a->b09 == 1);
                    ++bound_models;
                } else {
                    assert(!a->w44 && !a->w4C);
                }
            }
            assert(nodes == 22 && bound_models == 2);
        } else {
            assert(page->item.screen == (type == 0x32 ? 3 : 2));
        }
        input.pressed = 0x800;
        assert(page_frame(&input) == 0 && page->phase == 5);
        input.pressed = 0;
        while ((page_result = page_frame(&input)) == 0)
            assert(++page_ticks < 440);
        assert(page_result == 1 && !em_area11_interaction_host_failed());
        printf("AREA11 native host take %04X: B0 = %u, B1 = %#04x after %u callbacks; page %u "
               "takes the request (notice %u ticks), START closes PASS\n", (unsigned)uid,
               (unsigned)kind, (unsigned)type, ticks, (unsigned)page->item.screen, notice);
    } else {
        while ((page_result = page_frame(&input)) == 0)
            assert(++page_ticks < 8);
        assert(page_result == -1 && em_area11_interaction_host_failed());
        assert(page->phase == 3 &&
               page->item.screen == (kind == 2 ? 1 : kind == 3 ? 3 : type == 0x10 ? 2 : 0));
        printf("AREA11 native host take %04X: B0 = %u, B1 = %#04x after %u callbacks; its page %u "
               "faults after %u ticks PASS\n", (unsigned)uid, (unsigned)kind, (unsigned)type,
               ticks, (unsigned)page->item.screen, page_ticks);
    }
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
    assert(!player_pose_owned() && g.cam.top_mode == 0 && em_rcl_zoom() == 480);
    printf("AREA11 native host no-battery: %u ordinary callbacks PASS\n", ticks);
    teardown();
}

static void panel_menu(int discharge, int child_slot)
{
    panel_child_slot = child_slot;
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
    unsigned old_stops = stream_stops, old_channels = channel_sets;
    while (!outer(0)) assert(++ticks < 200);
    for (unsigned i = 1; i < 16; ++i) assert(outer(0) == 1); /* 0x21: 10 loader dispatches */
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
    assert(stream_stops == old_stops + 1 && channel_sets == old_channels + 2);
    assert(!em_status_runtime_ordinary_enabled(status));
    assert(shared->owner == panel && player_pose_owned());
    /* 001DD950 copied the camera target D_008105E0 to +0x2450. */
    assert(!memcmp(em_rcl_bytes(EM_RCL_CONTEXT + 0x2450, 12), g.cam.tgt, 3 * sizeof(float)) &&
           rcl_float(0x2460) > 0);
    do { assert(!outer(0)); assert(++ticks < 400); } while (shared->owner);
    assert(!player_pose_owned() && g.cam.top_mode == 0 && em_rcl_zoom() == 480);
    assert(em_pickup_item_count(0x1B) == 1 && em_pickup_battery_capacity() == 12);
    assert(em_pickup_battery_charge() == (discharge ? 8 : 12));
    assert(powered() == discharge && indicators == old_indicators + (unsigned)discharge);
    assert(panel->owner.phase == (discharge ? 3 : 0));
    printf("AREA11 native host panel discharge%d child%d: %u ordinary callbacks PASS\n", discharge, child_slot, ticks);
    panel_child_slot = 1;
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
        EmActor *terminal = record_of(EM_INTERACTION_ELEVATOR);
        assert(terminal && terminal->pos[1] == 230.0f); /* the placement record's (upper) */
        const unsigned placed = owner_places[terminal - records];
        assert(em_area11_interaction_host_elevator_state0() == 0);
        const float expected = lower ? 190.0f : 230.0f;
        const EmInteractionSceneOwner *record = em_interaction_scene_role(
            em_area11_interaction_host_scene(), EM_INTERACTION_ELEVATOR);
        const EmElevator *owner = &em_area11_interaction_host_elevator()->owner;
        /* The record's +0xB4 is the floor, and 001C6380 ran once over it. */
        assert(record && terminal->pos[1] == expected && owner->height == expected &&
               record->descriptor[1] == expected && owner->lower == lower);
        assert(owner_places[terminal - records] == placed + 1);
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

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The script's face on the player (001B81D0: 001CA700(player,
 * D_0028A490[0x18], 7), 001D06D0(player, 1), 0x70003B8F = 2) through the
 * interaction host's face slot (em_face_slot over the record and the one
 * stack), 001FD950's 001D06E0 talk, 00183090's 001D0C70 before the body, the
 * status pause and 001B82D0 sub 4's 001CA770. With no_slot, the stack holds
 * fewer than 31 free slots: 001CA700 returns 0 and stores +0x90 = 0 (the
 * original's refusal; 001B81D0 then skips 001D06D0 and 0x70003B8F). */
static void cinematic_face(int no_slot)
{
    /* Keep the battery actually acquired by first_battery. The status pause
     * below reuses the original pickup request, which the pickup program
     * issues only after 1C40B0 has added the item; an empty inventory is not
     * a reachable request state and the real battery page rejects it. */
    setup(0);
    slots_reset(no_slot ? 30 : SLOTS);
    assert(em_pickup_item_count(0x1B) == 1 && em_pickup_battery_charge() == 12);
    EmInteractionRuntime *shared = em_area11_interaction_host_shared();
    static const unsigned owner_token = 0x8283D0;
    assert(player_pose_use_accepted_port());
    assert(em_interaction_runtime_claim(shared, &owner_token));
    em_area11_interaction_host_camera_fields(); /* the claim's 3B8D = 3 */
    assert(!outer(0) && player_pose_owned() && shared->frame->player_ready == 1);
    unsigned clip, flags; float remaining; int transition;
    assert(player_pose_source(&clip, &remaining, &flags, &transition));
    uint8_t *record = player_states_actor_mut()->bytes;
    EmFaceSlot face;
    assert(em_area11_interaction_host_player_face(&face) == 0);
    int32_t result = -1;
    assert(em_face_slot_001CA700(&face, 0x011749C0u, 7, &result) == 0);
    if (no_slot) {
        assert(result == 0 && rd32(record + 0x90) == 0 && slot_count == 30 && slot_cursor == SLOT_STACK);
        assert(em_area11_interaction_host_face_tick_001D0C70() == -1 && em_area11_interaction_host_failed());
        teardown();
        puts("AREA11 native host face slot: 001CA700 without a free slot stores 0; 001D0C70 then faults PASS");
        return;
    }
    assert(result == 1 && em_face_slot_001D06D0(&face, 1) == 0);
    em_scene_state()->spad3B8F = 2;              /* 001B81D0 after both */
    const uint32_t slot_address = rd32(record + 0x90);
    uint8_t *slot = slot_at(slot_address);
    assert(slot_address == SLOT_RECORDS && slot_count == SLOTS - 1 && slot_cursor == SLOT_STACK + 4);
    assert((int16_t)(record[0x94] | record[0x95] << 8) == 7 && rd32(slot + 0x60) == 0x011749C0u);
    assert(slot[0x81] == 1 && slot[0x80] == 0);
    *em_scene_req_at(em_scene_state(), 0x008106D4u) = 0xA5; /* the mailbox D_008106D4 */
    assert(em_area11_interaction_host_face_talk(1));
    assert(slot[0x80] == 1);
    assert(!outer(0)); /* 00183090 ticks the attached face (001D0C70) before the body. */
    assert(rd32(slot + 0x70) == 1 && !player_pose_special_active()); /* 001D0720's first blink state */
    assert(shared->frame->activity[0] == 0xA5);
    assert(player_pose_source(&clip, &remaining, &flags, &transition));
    uint8_t before[EM_ROGER_ACTOR_SLOT_BYTES];
    memcpy(before, slot, sizeof before);
    assert(!outer(0));
    assert(memcmp(before, slot, sizeof before) != 0); /* one more 001D0720 step */
    assert(player_pose_source(&clip, &remaining, &flags, &transition) && !player_pose_special_active());
    assert(em_area11_interaction_host_face_talk(0));
    assert(slot[0x80] == 0 && shared->frame->activity[0] == 0xA5);

    /* Original status consumes the frame: neither face nor body advances. */
    assert(em_status_runtime_pickup_request(em_area11_interaction_host_status(), 1, 0x1B));
    assert(outer(0) == 1);
    memcpy(before, slot, sizeof before);
    /* The hook only reports the held token (the stage is the takeover's,
     * and no stage runs in a status frame): no face or body work. */
    assert(em_area11_interaction_host_player(NULL) == 2);
    assert(!memcmp(before, slot, sizeof before));
    /* Clear this standalone controlled status request via the real page. */
    for (unsigned i = 1; i < 16; ++i) assert(outer(0) == 1); /* 0x21: 10 loader dispatches */
    assert(outer(0x40) == 1);
    assert(outer(0x10) == 1);
    while (em_status_runtime_frame(em_area11_interaction_host_status())->phase != 1)
        assert(outer(0) == 1);

    /* 001B82D0 sub 4 with 0x70003B8F == 2: 001CA770(player) (the slot
     * cleared and pushed back, +0x90 = 0, +0x94 = -1), then player_ready 1. */
    EmScript script = {0}; unsigned char frame4[32] = {0};
    frame4[0] = 7; frame4[8] = 4;
    assert(em_interaction_runtime_frame(shared, &owner_token, &script, frame4) == EM_SCRIPT_ADVANCE);
    em_area11_interaction_host_camera_fields();
    assert(shared->frame->player_ready == 1 && !shared->frame->selector);
    assert(shared->owner == &owner_token && player_pose_owned() && !player_pose_special_active());
    assert(rd32(record + 0x90) == 0 && (int16_t)(record[0x94] | record[0x95] << 8) == -1);
    assert(slot_count == SLOTS && slot_cursor == SLOT_STACK && slot_stack[0] == slot_address);
    for (unsigned i = 0; i < EM_ROGER_ACTOR_SLOT_BYTES; ++i) assert(slot[i] == 0);
    assert(!outer(0)); /* One final body tick, then the release. */
    assert(!shared->owner && !player_pose_owned());
    assert(!player_pose_special_active() && shared->frame->player_ready == 0);
    assert(player_pose_source(&clip, &remaining, &flags, &transition));
    /* No special bank was ever on the record (+0x2F3 0): 00182DF0's zero
     * branch keeps the running default clip 0 instead of re-initializing
     * it (the nonzero branch, after Roger's encounter, is compared row for
     * row by the level smoke's roger phase). */
    assert(clip == 0 && remaining > 0 && remaining < 80);
    teardown();
    puts("AREA11 native host face slot/status pause/frame4/default idle PASS");
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
 * f12). X on hover 3 selects MAP (em_status_pages_live), whose Circle
 * returns to the hub. */
static int hub_frame(const EmStatusInput *input)
{
    int result = page_frame(input);
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
    for (int i = 0; i < 16 && page->phase == 3 && page->step != 2; ++i) /* module 0x1F's load */
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
    /* X on hover 3 (up): MAP (0020F950, em_status_pages_live). No map is
     * owned here (D_00810CB8.. clear): mode 0 spawns the 22 nodes (002101C0)
     * and the list view walks them without a model; each MAP frame draws its
     * 2D layer before the pool's models (em_gfx_overlay_decor_flush) inside
     * the SCISSOR_1 window, and Circle (0x20) returns to the hub. */
    assert(em_area11_interaction_host_status_open() == 1 && em_status_runtime_ui_clock(status) == 0);
    assert(hub_frame(&input) == 0 && hub_frame(&input) == 0 && page->step == 1);
    input.stick_y = 0;
    input.pressed = 0x40;
    assert(hub_frame(&input) == 0 && page->phase == 3 && page->item.screen == 1);
    input.pressed = 0;
    input.stick_y = 128;
    for (int i = 0; i < 16 && page->step != 2; ++i) /* module 0x1E's load */
        assert(hub_frame(&input) == 0);
    assert(page->phase == 3 && page->step == 2);
    {
        const unsigned flushes = decor_flushes, scissors = scissor_sets;
        for (unsigned i = 0; i < 3; ++i)
            assert(hub_frame(&input) == 0 && page->phase == 3 && page->item.screen == 1);
        /* The first page call is mode 0 (the nodes, no walk); the next two
         * are the list view's, each with one 001B0000. */
        assert(decor_flushes == flushes + 2 && scissor_sets == scissors + 4);
        const EmStatusScenePool *pool =
            em_status_models_pool(em_area11_interaction_host_status_models());
        unsigned nodes = 0;
        for (unsigned r = 0; r < EM_STATUS_SCENE_POOL_RECORDS; ++r)
            if (pool->record[r].b00 && pool->record[r].w10 == 0x002101C0u) {
                assert(pool->record[r].b04 == 1 && !pool->record[r].w44);
                ++nodes;
            }
        assert(nodes == 22);
    }
    input.pressed = 0x20;
    for (int i = 0; i < 8 && page->phase == 3; ++i) {
        assert(hub_frame(&input) == 0);
        input.pressed = 0;
    }
    for (int i = 0; i < 8 && !(page->phase == 1 && page->step == 1); ++i)
        assert(hub_frame(&input) == 0);
    assert(page->phase == 1 && page->step == 1 && !em_area11_interaction_host_failed());
    teardown();
    puts("AREA11 native host original START/TRIANGLE hub: sub-state 0 draws nothing, one "
         "0020A7A0 step, the backdrop flushed before the seven model draws and 00209DF0 per "
         "hub frame, ITEM and back, 0020E0C0 exit latency, MAP and back PASS");
}

int main(void)
{
    shared_only_fixture=1;
    status_hub_route();
    setup(1);
    static const int generic_owner=1;
    assert(em_area11_interaction_host_claim_scan(&generic_owner)==1);
    assert(shared_bank_calls==1 && em_scene_state()->spad3B8D==3);
    assert(em_interaction_runtime_owns(em_area11_interaction_host_shared(),&generic_owner));
    teardown();assert(!em_area11_interaction_host_math());
    shared_only_fixture=0;
    puts("Shared-only host: common status hub/ITEM/MAP, canonical math, generic claim and bank callback PASS");
    status_hub_route();
    no_battery();
    first_battery();
    cinematic_face(0);
    cinematic_face(1);
    panel_menu(0, 1);
    panel_menu(1, 1);
    first_battery(); /* a fresh charged battery for the next discharge */
    panel_menu(1, 0); /* empty +0x20: the completion still succeeds */
    owned_teardown();
    elevator_state0_floor();
    missing_sound_bank();
    /* Last: each resets the inventory the scenarios above share. */
    other_take(0x0B04, 1, 0x1E);
    other_take(0x0B07, 3, 0x32);
    other_take(0x0B08, 1, 0x10);
    other_take(0x0B09, 2, 0x08);
    em_module_loader_close(em_module_loader_live());
    puts("AREA11 native interaction host PASS");
    return 0;
}
