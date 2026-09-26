#include "game/em_area11_interaction_host.h"
#include "game/em_door_candidate.h"
#include "game/em_render_context_live.h"
#include "game/em_area11_bindings.h"
#include "game/em_camera.h"
#include "game/em_camera_live.h"
#include "game/em_camera_rotation.h"
#include "game/em_collision_world.h"
#include "game/em_ee_float.h"
#include "game/em_frame.h"
#include "game/em_weapon.h"
#include "game/em_interaction_alignment.h"
#include "game/em_item_device.h"
#include "game/em_item_sdk_math.h"
#include "game/em_message_live.h"
#include "game/em_pickup.h"
#include "game/em_pickup_items_original.h"
#include "game/em_pickup_motion.h"
#include "game/em_pickup_original.h"
#include "game/em_owner_services_original.h"
#include "game/em_player_closure_live.h"
#include "game/em_props.h"
#include "game/em_random.h"
#include "game/em_area11_roger.h"
#include "game/em_roger.h"
#include "game/em_scene_bindings.h"
#include "game/em_sfx.h"
#include "game/em_status_background.h"
#include "game/em_status_models.h"

/* One AREA11 item owner (00219550 x6, 0015AFA0) the host binds (WP-6). */
enum { HOST_PICKUPS = 7 };
typedef struct {
    EmInteractionSceneOwner *record;
    EmPickupAura aura;   /* owner +0x2D0 (0015AFA0 only) */
    float world[16];     /* owner +0xD0, from 0015AC00's 001C6380 */
    uint8_t model, param; /* owner +0x03 / +0x0D, read by state 0 */
    uint8_t state0;      /* the node's state 0 ran */
    EmActor *actor;      /* the owner's pool record (bound by its node; NULL once freed) */
} HostPickup;

/* The owner token addresses remain stable until whole-world teardown. */
static struct {
    EmInteractionScene scene;
    EmInteractionFrame frame;
    EmInteractionRuntime shared;
    EmPanelRuntime panel;
    EmElevatorRuntime elevator;
    EmPlayerFaceHost face;
    EmStatusRuntime *status;
    /* The hub's static actor pool D_0028B020 and its models (WP-5). */
    EmStatusModels *models;
    EmItemSdkMath item_sdk;
    EmItemMath item_math;
    EmInteractionSceneOwner *panel_record, *elevator_record;
    float local_palette[22 * 16];
    float panel_world[16];
    uint8_t panel_class, elevator_status, elevator_class;
    int loaded, failed, status_draw_context, status_ui_context;
    /* WP-4 live binding: the panel pool record's original address (its
     * +0x14, the value 00157F60 stores in D_008106D0), the shared 00183EF0 score
     * scratch 0x70003B98, the 0020E060 route of the open status screen and
     * a failed 001B17A0 publication inside a void owner hook. */
    uint32_t panel_address;
    float scan_score;
    int status_route, offer_failed;
    /* WP-6: the bound item owners, the one being ticked (its hooks' owner)
     * and the 001B17A0 services their publication runs through. */
    HostPickup pickups[HOST_PICKUPS];
    size_t pickup_count;
    HostPickup *pickup_current;
    EmOwnerServices services;
    /* Census L07: the panel's and the terminal's pool records (bound by
     * their nodes), and the record 001B17A0's 001B1B70 is publishing. */
    EmActor *panel_actor, *elevator_actor, *publishing;
    /* Census L22: Roger's pool record (bound by its lifecycle 0) and its
     * EMIS record (00183EF0's selector-0 class-10 candidate). */
    EmActor *roger_actor;
    EmInteractionSceneOwner *roger_record;
    /* Census L18: the fence door 001BC350's pool record (bound by its node's
     * first call, em_area11_door.c) and its EMIS record (00183EF0's
     * selector-0 class-5 candidate, em_door_candidate). */
    EmActor *door_actor;
    EmInteractionSceneOwner *door_record;
    /* The token of the script owner (em_area11_script_host) the shared
     * takeover serves, or NULL: its stages run 00183090 on the record. */
    const void *script_owner;
} world;

static int camera_publish(void *context);
static void view_load(void);
static void view_store(void);

static int fail(const char *operation)
{
    if (!world.failed)
        fprintf(stderr, "AREA11 interaction: %s failed at frame%d\n", operation, g.frame_no);
    world.failed = 1;
    return -1;
}

static int acquire(void *context)
{
    (void)context;
    return player_pose_acquire();
}

static int face_tick_001D0C70(void);
static int map_special(void *context, uint32_t address, uint32_t size, const uint8_t *bytes);

/* 00183090 with player-ready 1: a nonzero +0x2F3 (the special bank still
 * on the record after 001B82D0 sub 4 detached the face, 0x70003B8F = 1)
 * takes its special path on the stage that releases the player, as the
 * original's +4 = 4 stage does before 00182DF0 (census L22). */
static int idle(void *context, float *palette)
{
    (void)context;
    if (world.script_owner && world.shared.owner == world.script_owner) {
        if (em_area11_roger_regions(map_special, NULL) < 0) return -1;
        return player_pose_commit_tick(face_tick_001D0C70, palette);
    }
    return player_pose_idle_tick(palette);
}

static int release(void *context)
{
    (void)context;
    return player_pose_release();
}

static int publish(void *context, const float *palette)
{
    (void)context;
    return player_pose_publish(palette);
}

static int pose(void *context, const EmInteractionAnimation *animation, int result,
                float *palette)
{
    (void)context;
    return player_pose_script_tick(animation, result, palette);
}

static uint32_t face_random(void *context)
{
    (void)context;
    return em_random_next();
}

/* 00183090's 001D0C70 (0x70003B8F == 2): the attached face's tick. */
static int face_tick_001D0C70(void)
{
    return em_player_face_host_tick_before_body(&world.face);
}

/* The special bank on the record (census L22): the regions of Roger's
 * resource export (bank 0x96 holds the player's encounter clip 1). */
static int map_special(void *context, uint32_t address, uint32_t size, const uint8_t *bytes)
{
    (void)context;
    return player_pose_map_region(address, size, bytes) ? 0 : -1;
}

static int cinematic_player(void *context, float *palette)
{
    (void)context;
    if (!world.face.attached) {
        fprintf(stderr, "AREA11 interaction: player-ready 2 without the attached face\n");
        return -1;
    }
    /* A script owner's takeover (em_area11_script_host): 00183090 on the
     * record, its +0x2F3 special bank and its +0x1F2 requests, the face
     * ticked inside it (player_pose_commit_tick). */
    if (world.script_owner && world.shared.owner == world.script_owner) {
        if (em_area11_roger_regions(map_special, NULL) < 0) return -1;
        return player_pose_commit_tick(face_tick_001D0C70, palette);
    }
    /*83090 advances the attached face before choosing a body request. A
     * prepared mesh alone is not an attached original face allocation. */
    if (!em_player_face_host_tick_before_body(&world.face)) return -1;
    if (world.shared.animation.active) {
        int result = em_interaction_animation_tick(&world.shared.animation, &g.model, palette);
        if (result < 0 || !player_pose_script_tick(&world.shared.animation, result, palette)) return -1;
        return result;
    }
    /* B81D0 can precede op0A/sub1's foreign-bank request. Keep the actual
     * acquired ordinary source until that deferred request is published. */
    return player_pose_idle_tick(palette);
}

static int frame_event(void *context, EmInteractionFrameEvent event)
{
    (void)context;
    switch (event) {
    case EM_INTERACTION_BARS_ENTER:
        em_frame_screen_fade_start(1, 4);
        return 1;
    case EM_INTERACTION_BARS_LEAVE:
        em_frame_screen_fade_start(-1, 4);
        return 1;
    case EM_INTERACTION_SCOPE_ZOOM_ZERO:
        /* 001D2610(0.0) on the render context: the zoom through 001D2590 /
         * the SDK tanf, then its 0021B970 fog pair (em_render_context_live). */
        return em_rcl_001D2610(0) < 0 ? 0 : 1;
    case EM_INTERACTION_ZOOM_DEFAULT:
        /* 001D25F0(480.0) on the render context. */
        return em_rcl_001D25F0(UINT32_C(0x43F00000)) < 0 ? 0 : 1;
    case EM_INTERACTION_FADE_IN:
        em_frame_fade_start(-1, 4);
        return 1;
    case EM_INTERACTION_RELEASE_SKELETON:
        if (!world.face.attached || world.face.failed) return 0;
        em_player_face_host_detach(&world.face);
        return 1; /* The original frame core writes player_ready1 next. */
    case EM_INTERACTION_RESUME_MUSIC:
        /* 001B82D0 op 4 / 6's 001FAE70(0) on an aborted cinematic: the
         * stream lanes (em_stream_live, WP-8b). */
        return em_scene_bindings_001FAE70(0) == 0;
    }
    return 0;
}

static int retarget(void *context)
{
    (void)context;
    float euler[3];
    return player_pose_script_euler(euler) && camera_interaction_retarget_area11(&g.cam, euler);
}

static int align_panel(void *context)
{
    (void)context;
    const float local_point[4] = {.3f, 0, 9, 1};
    float position[4], yaw;
    return world.panel_record && em_interaction_alignment(world.panel_world,
        world.panel_record->angles[1], local_point, 3.1415927f, g.pos[1], position, &yaw) &&
        player_pose_face(yaw) && player_pose_align(position);
}

/* 001B7D60 case 0 on the live message service (em_message_live.h, WP-8):
 * D_002821B0 = 2, B4 = 1, B8 = the line, BC = the delay word as the request
 * holds it (001B7D60 stores it unchecked); the service runs the line at
 * step F. The caller's frame view is stored first and loaded after, so the
 * phase the post wrote is the view's. */
static int message_start(void *context, uint32_t token, uint32_t delay)
{
    (void)context;
    EmMessageBlock *block = em_message_live_block();
    if (!block) return 0;
    block->phase = world.frame.message_phase;
    if (em_message_live_post(token, (int32_t)delay) < 0) return 0;
    world.frame.message_phase = block->phase;
    return 1;
}

/* The command's later polls: complete once D_002821B4 is 2. */
static int message_done(void *context)
{
    (void)context;
    return em_message_live_block() ? world.frame.message_phase == 2 : -1;
}

/* 00157F60's request tail for the type-24 panel (em_panel_battery_request
 * already cleared +A/+B and restored +0 = 1): D_008106B1 = 0x80 + cost,
 * D_008106B0 = 1, D_008106D0 = the owner's +0x14 (its own address). The
 * classifier 001AE7E0 then returns 2 on the next tick and 0x1AE040 opens
 * the status screen, whose 0020E060/0020CDC0 take the host route below. */
static int battery_open(void *context, EmPanel *owner, uint8_t request)
{
    (void)context;
    EmSceneState *scene = em_scene_state();
    uint8_t *d0 = em_scene_req_at(scene, 0x008106D0u);
    if (owner != &world.panel.owner || !world.panel_address || !d0 ||
        request != 0x80 + owner->cost) return 0;
    scene->req[EM_SCENE_REQ_B1] = request;
    scene->req[EM_SCENE_REQ_B0] = 1;
    for (unsigned i = 0; i < 4; ++i) d0[i] = (uint8_t)(world.panel_address >> (8 * i));
    return 1;
}

static int sound(void *context, uint32_t cue)
{
    (void)context;
    /* An original absent remap is accepted silence. A missing asset is
     * a failed binding and must not let the script claim successful audio. */
    if (!em_sfx_cue_state(cue)) return 0;
    em_sfx_play(cue);
    return 1;
}

/* 001580C0 for the type-24 panel: D_00810841[D_00810700] |= 1 << panel
 * +0x2E (7, checked at the node's state 0), then 001FB9F0(0x3EE) (the
 * program's following sound call). The byte is canonical D2 progress
 * (em_scene_state.h); only AREA11's D_0081084C is migrated. */
static int power(void *context, uint8_t mask)
{
    (void)context;
    EmSceneState *scene = em_scene_state();
    uint8_t *byte = em_scene_progress_at(scene, 0x00810841u + scene->d810700, 1);
    if (mask != 0x80 || !byte) return 0;
    *byte |= mask;
    return 1;
}

/* D_00810841[D_00810700] bit (+0x2E = 7), as 00159210 state 0 and 00827B10
 * read it (the same canonical byte em_game_terminal_powered reads). */
static int powered(void)
{
    EmSceneState *scene = em_scene_state();
    const uint8_t *byte = em_scene_progress_at(scene, 0x00810841u + scene->d810700, 1);
    return byte && (*byte & 0x80) != 0;
}

/* 00159210 state 1 / sub 2: if +0x20 != 0, its child's +4 = 3 (the 0x75
 * indicator node frees itself on its next behaviour call) and +0x20 = 0;
 * an empty slot is skipped, as the original's null check skips it. */
static int stop_indicator(void *context)
{
    (void)context;
    (void)em_area11_bindings_panel_child_stop();
    return 1;
}

static int read_inventory(void *context, EmStatusInventory *inventory)
{
    (void)context;
    for (unsigned i = 0; i < 3; ++i)
        inventory->battery_count[i] = em_pickup_item_count(0x1B + i);
    inventory->charge = (uint16_t)em_pickup_battery_charge();
    inventory->capacity = (uint8_t)em_pickup_battery_capacity();
    em_pickup_equipment_read(&inventory->status, &inventory->primary, &inventory->secondary);
    return inventory->charge <= inventory->capacity;
}

static int write_charge(void *context, uint16_t charge)
{
    (void)context;
    if (charge > em_pickup_battery_capacity()) return 0;
    em_pickup_battery_set_charge(charge);
    return em_pickup_battery_charge() == charge;
}

static int write_capacity(void *context, uint16_t charge, uint8_t capacity)
{
    (void)context;
    return em_pickup_battery_set_capacity_charge(charge, capacity);
}

static int status_frame_event(void *context, EmStatusFrameEvent event,
                              const EmStatusFrame *frame)
{
    (void)context;
    switch (event) {
    case EM_STATUS_RESET_UI:
        world.status_draw_context = world.status_ui_context = 0;
        return 1;
    case EM_STATUS_RESET_SOUNDS:        /* 001FBC50 */
        return em_scene_bindings_001FBC50() == 0;
    case EM_STATUS_STOP_STREAMS:        /* 001FABB0 on the stream lanes */
        return em_scene_bindings_001FABB0() == 0;
    case EM_STATUS_CHANNEL_ZERO:        /* 00119828(0, 0x3FFF, 0x3FFF) */
        return em_scene_bindings_00119828(NULL, 0, 0x3FFF, 0x3FFF) == 0;
    case EM_STATUS_CHANNEL_ONE:         /* 00119828(1, 0x3FFF, 0x3FFF) */
        return em_scene_bindings_00119828(NULL, 1, 0x3FFF, 0x3FFF) == 0;
    case EM_STATUS_BEGIN_FRAME:
        if (world.status_draw_context) return 0;
        world.status_draw_context = 1;
        return 1;
    case EM_STATUS_DRAW_CONTEXT:
        return world.status_draw_context;
    case EM_STATUS_MODE_ZERO:
        return 1; /* The page controls the native overlay submission mode. */
    case EM_STATUS_BLACK_HOLD:
        /* Only the runtime's own frame machine (the fixture's stand-in for
         * 0x1AE040 states 3/5) emits this; the live scene core writes
         * D_008106EF = 0x46 itself. Publish that status write to the same
         * canonical byte the interaction scripts write. */
        if (frame->phase == 5) em_scene_state()->req[EM_SCENE_REQ_EF] = frame->recovery_lock;
        em_frame_fade_full(0);
        return 1;
    case EM_STATUS_END_FRAME:
        if (!world.status_draw_context) return 0;
        world.status_draw_context = 0;
        return 1;
    case EM_STATUS_RESET_FRAME:
        world.status_draw_context = 0;
        return 1;
    case EM_STATUS_CAMERA_COMMIT:
        camera_commit(&g.cam);
        return camera_publish(NULL);
    case EM_STATUS_RESUME_MUSIC:        /* 001FAE70(1) on the stream lanes */
        return em_scene_bindings_001FAE70(1) == 0;
    case EM_STATUS_FLASH:
        em_frame_fade_flash(32);
        return 1;
    }
    return 0;
}

static int status_page_event(void *context, EmStatusPageEvent event, unsigned argument)
{
    (void)context;
    (void)argument;
    switch (event) {
    case EM_STATUS_PAGE_BLACK_HOLD:
        em_frame_fade_clear(0);
        return 1;
    case EM_STATUS_PAGE_CONFIGURE:
        /* 0020DFA0: 001AFE60 (the static pool D_0028B020), then
         * 001029C0(D_00810610) and D_00810624 *= -1 (the models' UI view),
         * and the UI projection (001D2610(0): zoom 224 / tan(25 deg)).
         * Native status sprites and untextured triangles already use the
         * original identity/Y-flip UI coordinate convention. Keep paused
         * world camera vectors available for the final commit. */
        if (em_status_models_clear(world.models) != 1 ||
            em_status_models_configure(world.models) != 1) return 0;
        world.status_ui_context = 1;
        /* 001D2610(0.0) on the render context (its zoom and 0021B970). */
        return em_rcl_001D2610(0) < 0 ? 0 : 1;
    case EM_STATUS_PAGE_CLEAR_DRAW:
        /* 001AFEB0: every static record in use returns its bone slots.
         * The pool D_0028B020 is the status screens' own; the paused
         * world actor pool is untouched. E0C0 also calls it AFTER
         * restoring the world projection. */
        return em_status_models_release(world.models);
    case EM_STATUS_PAGE_RESET_DRAW:
        /* 001AFE60: clears all 24 static records. */
        return em_status_models_clear(world.models);
    case EM_STATUS_PAGE_PLAYER_TEXTURE:
        /* Native textures are per-mesh resources, so UI pages cannot
         * overwrite the player's shared GS VRAM. Require actual residency. */
        return g.mesh && g.model.tex_count && g.model.texs;
    case EM_STATUS_PAGE_END_PROJECTION:
        world.status_ui_context = 0;
        return 1;
    case EM_STATUS_PAGE_RESTORE_PLAYER:
        /* Equipment status1->2 requires its original player worker. The
         * initial AREA11 inventory has status0 and never takes this route. */
        return 0;
    default:
        return 0; /* Sounds, ITEM and module dispatch are adapter-owned. */
    }
}

/* The host owner bound to a pool record, or NULL. */
static EmInteractionSceneOwner *owner_of_record(const EmActor *actor)
{
    if (!actor) return NULL;
    if (actor == world.panel_actor) return world.panel_record;
    if (actor == world.elevator_actor) return world.elevator_record;
    if (actor == world.roger_actor) return world.roger_record;
    if (actor == world.door_actor) return world.door_record;
    for (size_t i = 0; i < world.pickup_count; ++i)
        if (world.pickups[i].actor == actor) return world.pickups[i].record;
    return NULL;
}

/* The view of the published interactive list D_00275B5C/B64 that 00184BA0
 * and the device lookup read: the collision world's EM_ACTOR_LIST_FLAG80
 * (one store, census L07; newest push first), each record mapped to the
 * host owner bound to it. A record the host does not run is dropped only
 * where both readers skip it (its +0x00 bit 0 or +0x02 bit 7 clear, e.g. a
 * record freed since it published); any other is a fault. 0, or -1. */
static int published_view(void)
{
    const EmActorClassLists *lists = em_collision_world_lists();
    EmInteractionList *list = &world.scene.list;
    list->pending_count = 0;
    list->active_count = 0;
    const int count = lists->list[EM_ACTOR_LIST_FLAG80].published;
    for (int j = 0; j < count; ++j) {
        const EmActor *actor = em_actor_class_list_entry(lists, EM_ACTOR_LIST_FLAG80, j);
        EmInteractionSceneOwner *record = owner_of_record(actor);
        if (!record) {
            if (!actor || ((actor->status & 1) && (actor->cls & 0x80))) return -1;
            continue;
        }
        if (!record->live_status || !record->live_class_flags || !record->live_armed ||
            list->active_count >= EM_INTERACTION_CAPACITY) return -1;
        list->active[list->active_count++] = (EmInteractionCandidate){
            record, *record->live_status, *record->live_class_flags, record->live_armed};
    }
    return 0;
}

static int owner_available(void *context, EmPanel *panel, unsigned item_id)
{
    (void)context;
    EmItemDevice devices[EM_INTERACTION_CAPACITY];
    if (published_view() < 0) return -1;
    const EmInteractionList *list = &world.scene.list;
    if (list->active_count > EM_INTERACTION_CAPACITY) return -1;
    for (size_t i = 0; i < list->active_count; ++i) {
        EmInteractionSceneOwner *record = list->active[i].owner;
        if (!record || !record->native_owner || !record->live_status ||
            !record->live_class_flags || !record->live_armed) return -1;
        EmItemDevice *device = &devices[i];
        *device = (EmItemDevice){.owner = record == world.panel_record ?
            (void *)&world.panel.owner : record->native_owner,
            .status = *record->live_status, .class_flags = *record->live_class_flags,
            .armed = *record->live_armed, .subtype = record->subtype,
            .shape = record->selector, .yaw = record->angles[1]};
        memcpy(device->position, record->position, sizeof device->position);
        memcpy(device->parameters, record->descriptor, sizeof device->parameters);
    }
    void *selected = NULL;
    int result = em_item_device_find(devices, list->active_count, item_id,
        g.pos, g.yaw, &world.scene.math, &selected);
    if (result <= 0) return result;
    /* A different device requires its real confirmation owner. Treating
     * it as no device would fabricate the original149F0 result. */
    return panel && selected == panel ? 1 : -1;
}

/* 002149F0's successful exit writes 70003B8D = 3 after the inventory and
 * owner updates (em_status_runtime.h battery_finished). The status screen
 * freezes every owner, so the byte is written canonically here, not
 * through the shared frame view. */
static int battery_finished(void *context, EmPanel *panel)
{
    (void)context;
    if (panel != &world.panel.owner) return 0;
    em_scene_state()->spad3B8D = 3;
    return 1;
}

/* The status page's own cues (001FB9F0 with 0x0B open, 0x0D close, 0/1
 * accept/back, 2 no device, 4 cursor, 5 hover, 6 discharge unit) are the
 * system sound set, which no exporter produces yet (WP-14): they reach
 * em_sfx_play, which drops an unmapped id, and are reported once here, as
 * the legacy em_hud status screen's cues 0/1/4 already are. Every other
 * cue keeps the strict script contract above. */
static int status_sound(void *context, uint32_t cue)
{
    static const uint8_t system_cues[] = {0x0, 0x1, 0x2, 0x4, 0x5, 0x6, 0xB, 0xD};
    for (size_t i = 0; i < sizeof system_cues; ++i) {
        if (cue != system_cues[i] || em_sfx_cue_state(cue)) continue;
        static uint16_t reported;
        if (!(reported & (1u << cue))) {
            reported |= (uint16_t)(1u << cue);
            fprintf(stderr, "AREA11 interaction: status cue 0x%X has no exported sample "
                    "(system sound set, WP-14); silent\n", (unsigned)cue);
        }
        em_sfx_play(cue);
        return 1;
    }
    return sound(context, cue);
}

/* The original normal hub (0020CDC0 phase 1: em_status_hub with
 * em_status_hub_ui and 0020A7A0, in the status runtime). Its 00209DF0
 * inputs, as the port holds them:
 *   D_00810858/5C  g.status.health/infection (the port's only copy;
 *                  em_scene_bindings.c 001B07C0)
 *   D_008104E4     g.pd_infected: the player record D_008102B0 +0x234,
 *                  the infection latch 0021C270 sets
 *   D_00810C7F     em_pickup's count of item 0x1B (the D_00810C64 counts)
 *   CB2 / CB7      em_pickup's battery charge / capacity (half-units)
 *   CA4 / CA6      the canonical D2 equipment bytes
 *   CB4            em_weapon_reserve (the weapon state's reserve)
 * CA8..CB0 have no port storage: 00209860 reads them only for primary 2
 * or secondary 1..4, which fault here. */
static int hub_display(void *context, EmStatusHubDisplay *display)
{
    (void)context;
    const uint8_t *equipment = em_scene_progress_at(em_scene_state(), 0x00810CA4u, 4);
    if (!equipment) return 0;
    display->health = g.status.health;
    display->infection = g.status.infection;
    display->warning = (uint8_t)g.pd_infected;
    display->battery_equipped = em_pickup_item_count(0x1B);
    display->charge = (uint16_t)em_pickup_battery_charge();
    display->capacity = (uint8_t)em_pickup_battery_capacity();
    display->ammo.primary = equipment[0];
    display->ammo.secondary = equipment[2];
    if (display->ammo.primary == 2 ||
        (display->ammo.secondary >= 1 && display->ammo.secondary <= 4)) {
        fprintf(stderr, "AREA11 interaction: 00209860 needs D_00810CA8..CB0 (primary %u, "
                "secondary %u), which the port does not hold\n",
                (unsigned)display->ammo.primary, (unsigned)display->ammo.secondary);
        return 0;
    }
    display->ammo.reserve = em_weapon_reserve();
    return 1;
}

/* The status-model workers the hub reaches (em_status_models over the
 * translations of em_status_scene_original): 001AFF10 + the 0020E6F0 draw
 * callback (the menu player), 0020E250 (the equipment letter models via
 * 0020E3A0/0020E1E0) and, every hub frame, 001B0000 (the D_0028B020 walk
 * that runs 0020E6F0/0020EC80 and 0020E460 and queues their 001CB580
 * draws). Their inputs, as the port holds them:
 *   D_00810858/5C  g.status.health/infection
 *   D_008104E4     g.pd_infected (0020E6F0's variant; 0020EC80 reaches
 *                  001F4BF0, the rand-pulsed glow, when it is 1)
 *   D_00810C60     g.status (em_pickup_equipment_read)
 *   CA4..CA7       the canonical D2 equipment bytes
 * A model the export does not hold, and the untranslated glow sprite
 * 001CD520, fault (fail-stop). */
static int hub_models(void *context, EmStatusHubEvent event, unsigned argument)
{
    (void)context;
    const uint8_t *equipment = em_scene_progress_at(em_scene_state(), 0x00810CA4u, 4);
    if (!equipment) return 0;
    EmStatusModelsInputs in = {.health = g.status.health, .infection = g.status.infection,
                               .d8104E4 = (uint8_t)g.pd_infected};
    em_pickup_equipment_read(&in.d810C60, NULL, NULL);
    memcpy(in.ca, equipment, sizeof in.ca);
    return em_status_models_event(world.models, event, argument, &in) == 1 ? 1 : 0;
}

/* 001CB580 (001CB4F0, lighting mode 1) for every record the last 001B0000
 * walk queued, on 0020DFA0's UI camera: D_00810610 with the UI zoom. */
static int hub_models_draw(void *context, EmGfx *gfx)
{
    (void)context;
    return em_status_models_render(world.models, gfx, em_rcl_zoom()) == 1;
}

static EmStatusRuntimeHooks native_status_hooks(void)
{
    return (EmStatusRuntimeHooks){.read_inventory = read_inventory,
        .write_charge = write_charge, .write_battery_capacity = write_capacity,
        .frame_event = status_frame_event, .page_event = status_page_event,
        .sound = status_sound, .owner_available = owner_available,
        .battery_finished = battery_finished, .hub_display = hub_display,
        .hub_models = hub_models, .hub_models_draw = hub_models_draw};
}

static int align_player(void *context, const float position[3])
{
    (void)context;
    return player_pose_align(position);
}

static int face_player(void *context, float yaw)
{
    (void)context;
    return player_pose_face(yaw);
}

static int camera_set(void *context, const float eye[3], const float target[3])
{
    (void)context;
    memcpy(g.cam.eye, eye, sizeof g.cam.eye);
    memcpy(g.cam.tgt, target, sizeof g.cam.tgt);
    memcpy(g.cam.eye_des, eye, sizeof g.cam.eye_des);
    memcpy(g.cam.tgt_des, target, sizeof g.cam.tgt_des);
    return 1;
}

static int camera_publish(void *context)
{
    (void)context;
    /* Original 001DD980 publishes the render-context center/depth values,
     * including the first camera command's yielding callback: its distance
     * math, then 001DD950(&D_008105E0, ...) on the render context. */
    uint32_t f12[2];
    if (!em_interaction_projection_001DD980(g.cam.eye, g.cam.tgt, f12)) return 0;
    em_camera_live_adopt_view();   /* g.cam.eye / tgt are D_008105D0 / E0 */
    return em_rcl_001DD950(0x008105E0u, f12[0], f12[1]) < 0 ? 0 : 1;
}

int em_area11_interaction_host_camera_publish(void)
{
    if (!world.loaded || world.failed) return -1;
    return camera_publish(NULL) ? 1 : fail("camera publication");
}

/* The frame events of 001B82D0 for a script run outside the host (the
 * AREA11 script host, em_area11_script_host.c): the same bindings the
 * panel and elevator scripts use. The caller has stored its frame view
 * to the canonical storage first. */
int em_area11_interaction_host_frame_event(EmInteractionFrameEvent event)
{
    if (!world.loaded || world.failed) return -1;
    view_load();
    int accepted = frame_event(NULL, event);
    view_store();
    return accepted ? 1 : -1;
}

/* A script owner outside the host whose op07 opened the scripted frame:
 * the shared runtime's player takeover serves it (the stand-in for
 * 0015B130's 00182B30 admission, as for the panel and the elevator). */
int em_area11_interaction_host_claim_script(const void *owner)
{
    if (!world.loaded || world.failed || !owner) return -1;
    if (em_interaction_runtime_owns(&world.shared, owner)) return 1;
    view_load();
    int claimed = em_interaction_runtime_claim_scripted(&world.shared, owner);
    view_store();
    if (claimed) world.script_owner = owner;
    return claimed ? 1 : fail("scripted owner claim (the shared player is busy)");
}

int em_area11_interaction_host_owns(const void *owner)
{
    return world.loaded && !world.failed && em_interaction_runtime_owns(&world.shared, owner);
}

int em_area11_interaction_host_script_held(void)
{
    return world.loaded && !world.failed && world.script_owner && world.shared.owner == world.script_owner;
}

static int camera_chase(void *context)
{
    (void)context;
    float euler[3];
    return player_pose_script_euler(euler) &&
        camera_interaction_retarget_distance_area11(&g.cam, euler, -20.0f);
}

static void elevator_sound(void *context, unsigned cue, float radius)
{
    (void)context;
    em_sfx_play_at(cue, g.elev_pos, radius);
}

static void elevator_rebuild(void *context, float height)
{
    (void)context;
    g.elev_pos[1] = height;
    elevator_pose();
}

static void elevator_copy_child(void *context)
{
    (void)context;
    /* em_props_indicators_draw reads the rebuilt parent's node0 world
     * matrix directly. There is no stale separate native transform. */
}

/* 001B17A0 (byte-matched; em_owner_services_001B17A0), the owners' state-1
 * tail: +1 = 001B1630(+0xB0, +0xB4, +0xB8), the camera cone/range gate
 * against D_008105D0 and D_00810600 (g.cam.eye/fwd), and when visible
 * 001B1B70 (services_publish): the owner's record goes onto the class lists
 * its class byte selects (class 4: its collision cell, D_00275B80) and, with
 * class bit 0x80, onto the interactive list (001B1DE0) the Use scan reads.
 * 001AAD00 publishes them at the end of the frame (the collision world's
 * close-out, census L07). `view` carries the record's +0x02 (the owner's
 * live class byte), +0x03, +0x0D, +0x2E and +0xB0..+0xB8. 1 drawn, 0 culled,
 * -1 fault. */
static int publish_view(EmActor *actor, EmOwnerServicesOwner *view)
{
    if (!actor) return -1;
    world.services.world.d00810CA5 = em_scene_progress_at(em_scene_state(), 0x00810CA5u, 1);
    world.publishing = actor;
    int drawn = em_owner_services_001B17A0(&world.services, view);
    world.publishing = NULL;
    if (drawn < 0) return -1;
    actor->drawn = view->drawn; /* +0x01 */
    return drawn != 0;
}

int em_area11_interaction_host_offer_001B17A0(EmActor *actor, EmOwnerServicesOwner *view)
{
    if (!world.loaded || !view) return -1;
    return publish_view(actor, view);
}

/* The panel's and the terminal's view: the record's own bytes, with the
 * host's class byte and the given +0xB0..+0xB8. */
static int publish_owner(EmActor *actor, uint8_t cls, const float position[3])
{
    if (!actor) return -1;
    EmOwnerServicesOwner o;
    memset(&o, 0, sizeof o);
    o.cls = cls;
    o.kind = actor->model;
    o.model_id = actor->param;
    o.flags2 = actor->flags2;
    memcpy(o.pos, position, 3 * sizeof(float));
    o.pos[3] = 1.0f;
    return publish_view(actor, &o);
}

/* 00827B10's tail at 0x827E78: 001B17A0 (publication), then its virtual
 * +0x4C update, which has no port counterpart. Its +0xB4 is the owner's
 * floor height. */
static void elevator_update_actor(void *context)
{
    (void)context;
    const EmActor *a = world.elevator_actor;
    if (!a) { world.offer_failed = 1; return; }
    const float position[3] = {a->pos[0], world.elevator.owner.height, a->pos[2]};
    if (publish_owner(world.elevator_actor, world.elevator_class, position) < 0)
        world.offer_failed = 1;
}

/* 001C6380's matrix (build_trs_matrix(+0xD0, +0xB0, +0xC0, +0x60)) of a pool
 * record, with its +0xB4 given, then 001A2370(self, +0xD0): the record's
 * extended collision cell follows it (census L07). 0, or -1. */
static int retransform_record(const EmActor *actor, float y, float matrix[16])
{
    if (!actor) return -1;
    const float position[3] = {actor->pos[0], y, actor->pos[2]};
    if (em_owner_services_build_trs_matrix(matrix, position, actor->rot, actor->f60) != 0) return -1;
    return em_collision_world_retransform_001A2370(actor, matrix) < 0 ? -1 : 0;
}

/* 0x827E54: 001A2370(self, +0xD0) after the ride's completion rebuilt the
 * matrix at the new floor. */
static void elevator_retransform(void *context)
{
    (void)context;
    float matrix[16];
    if (retransform_record(world.elevator_actor, world.elevator.owner.height, matrix) < 0)
        world.offer_failed = 1;
}

/* ------------------------------------------------ the shared frame view
 *
 * EmInteractionFrame (em_interaction_frame.h) is the per-call view the
 * frame, cinematic and runtime cores read and write. Its bytes live in
 * their canonical storage: spad 3B8D/3B8F/3B92/3B84 and D_008106D4..DF,
 * D_008106EF, D_008106F3 in EmSceneState, the camera block D_008101E1/E3/
 * E4/E6 and the vector D_008105F0 in the port camera g.cam, the message
 * phase D_002821B4 in the host's message presenter. Every host entry point
 * loads the view before it runs a core and stores it after, so no second
 * copy survives between calls. D_008101E2 has no port storage yet (only
 * 001B82D0 sub4 writes it, to 0; nothing in the port reads it): it stays
 * in the view. The projection zoom is carried by the 001D2610/001D25F0
 * events, not stored back. The running script's skip byte (em_script.h
 * skip_request, the per-tick view of 3B91) is loaded and stored around the
 * owner that ticks it. */
static void view_load(void)
{
    EmSceneState *scene = em_scene_state();
    EmInteractionFrame *f = &world.frame;
    f->selector = scene->spad3B8D;
    f->player_ready = scene->spad3B8F;
    f->ready = scene->spad3B92;
    f->counter = scene->spad3B84;
    f->camera_phase = g.cam.sub_state;
    f->camera_swing = g.cam.swing;
    f->camera_top = g.cam.top_mode;
    f->camera_mode = g.cam.mode;
    f->recovery_lock = scene->req[EM_SCENE_REQ_EF];
    f->auxiliary = scene->req[EM_SCENE_REQ_F3];
    memcpy(f->activity, em_scene_req_at(scene, 0x008106D4u), sizeof f->activity);
    memcpy(f->up, g.cam.up, 3 * sizeof(float));
    f->up[3] = 1;
    const EmMessageBlock *block = em_message_live_block();
    f->message_phase = block ? block->phase : 0;
}

static void view_store(void)
{
    EmSceneState *scene = em_scene_state();
    const EmInteractionFrame *f = &world.frame;
    scene->spad3B8D = f->selector;
    scene->spad3B8F = f->player_ready;
    scene->spad3B92 = f->ready;
    scene->spad3B84 = f->counter;
    g.cam.sub_state = f->camera_phase;
    g.cam.swing = f->camera_swing;
    g.cam.top_mode = f->camera_top;
    g.cam.mode = f->camera_mode;
    scene->req[EM_SCENE_REQ_EF] = f->recovery_lock;
    scene->req[EM_SCENE_REQ_F3] = f->auxiliary;
    memcpy(em_scene_req_at(scene, 0x008106D4u), f->activity, sizeof f->activity);
    memcpy(g.cam.up, f->up, 3 * sizeof(float));
    EmMessageBlock *block = em_message_live_block();
    if (block) block->phase = f->message_phase;
}

static void script_load(EmScript *script)
{
    script->skip_request = em_scene_state()->spad3B91;
}

static void script_store(const EmScript *script)
{
    em_scene_state()->spad3B91 = (uint8_t)script->skip_request;
}

void em_area11_interaction_host_camera_fields(void)
{
    if (world.loaded) view_store();
}

/* D_0081083A: the elevator owner's floor byte (EmElevator.lower is its
 * view; canonical D2 progress since WP-4). */
static uint8_t *elevator_floor(void)
{
    return em_scene_progress_at(em_scene_state(), 0x0081083Au, 1);
}


/* ------------------------------------------------------------ the pickups
 *
 * WP-6: the seven AREA11 item owners (00219550 x6, 0015AFA0 for the map)
 * are bound to em_pickup_original at load and ticked at their own pool
 * nodes (em_area11_bindings.c). These are their hooks. */

static HostPickup *pickup_slot(uint32_t source_id)
{
    for (size_t i = 0; i < world.pickup_count; ++i)
        if (world.pickups[i].record->source_id == source_id) return &world.pickups[i];
    return NULL;
}

/* 001B7F90 (op0E sub1): the bounded turn toward the owner, written to the
 * player's heading through the shared player pose. */
static int pickup_turn(void *context, uint32_t source_id, const float position[3], float step)
{
    (void)context;
    (void)source_id;
    float yaw = g.yaw;
    int result = em_pickup_turn(&world.scene.math, g.pos, &yaw, position, step);
    if (result < 0 || !player_pose_face(yaw)) return -1;
    return result;
}

/* 001B8FC0 (op00 sub8): settles the actual target D_008105E0 only, then
 * 001DD980 publishes the render context (also on the seeding call). */
static int pickup_camera(void *context, uint32_t source_id, const float position[3], EmScript *script)
{
    (void)context;
    (void)source_id;
    int result = em_pickup_camera_settle(script, position, g.cam.tgt);
    if (result < 0 || !camera_publish(NULL)) return -1;
    return result;
}

/* 001C47A0 / 001C4720 / 001C4760's request: D_008106B0 = kind, then
 * D_008106B1 = type. The classifier 001AE7E0 opens the status screen on
 * the next tick. */
static int pickup_status(void *context, uint8_t kind, uint8_t index)
{
    (void)context;
    EmSceneState *scene = em_scene_state();
    scene->req[EM_SCENE_REQ_B0] = kind;
    scene->req[EM_SCENE_REQ_B1] = index;
    return 1;
}

/* 00122BB8, the SDK rand() the aura draws from. */
static int32_t aura_rand(void *context)
{
    (void)context;
    return (int32_t)em_random_next();
}

/* The aura's draw block (0x1F136C..0x1F1470: 0011E2A8, the sprite record,
 * 001026A0 and the glint 001F0A60): em_effect_manager_aura_draw over the
 * render context (em_effects_live, census L26), through the hook the
 * bindings set. `context` is the owner's +0xD0 matrix (the pickup slot's). */
static EmArea11AuraDraw s_aura_draw;

void em_area11_interaction_host_set_aura_draw(EmArea11AuraDraw draw) { s_aura_draw = draw; }

static int aura_draw(void *context, uint32_t record, uint32_t angle, uint32_t timer)
{
    const float *d0 = context;
    if (!d0) return -1;
    if (!s_aura_draw) return fail("001F1180's draw block (no aura draw bound)");
    return s_aura_draw(d0, record, angle, timer) < 0 ? -1 : 0;
}

/* 001B1630 on g.cam.eye / g.cam.fwd (D_008105D0 / D_00810600). */
static int services_visible(void *context, uint32_t x, uint32_t y, uint32_t z, uint8_t *visible)
{
    (void)context;
    const float position[3] = {em_ee_float(x), em_ee_float(y), em_ee_float(z)};
    *visible = (uint8_t)em_interaction_visible(position, g.cam.eye, g.cam.fwd);
    return 0;
}

/* 001B1B70 (em_actor_class_publish_001B1B70 over the collision world's
 * lists): the record being published goes onto the class list of its +0x02
 * (class 4: its collision cell, 001B1D20, D_00275B80) and, with class bit
 * 0x80, onto the interactive list (001B1DE0). The record's +0x02 is stored
 * from the owner's live class byte first (00219550 writes 0x87 when armed and
 * 4 at its completion, before this tail), so the walkers and the Use scan
 * read the owner's current class there. */
static int services_publish(void *context, EmOwnerServicesOwner *owner)
{
    (void)context;
    EmActor *actor = world.publishing;
    if (!actor || !owner) return -1;
    actor->cls = owner->cls;
    return em_collision_world_publish_001B1B70(actor) < 0 ? -1 : 0;
}

static int pickup_event(void *context, uint32_t source_id, EmPickupOwnerEvent event,
                        uint32_t argument)
{
    (void)context;
    HostPickup *slot = pickup_slot(source_id);
    if (!slot || slot != world.pickup_current) return -1;
    const EmPickupOwner *owner = slot->record->native_owner;
    switch (event) {
    case EM_PICKUP_OWNER_AURA: {
        /* 0015AE20's tail: 001F1180(self) while D_70003B92 == 0, read after
         * this tick's script step (the view is current). */
        if (world.frame.ready) return 1;
        const float eye[4] = {g.cam.eye[0], g.cam.eye[1], g.cam.eye[2], 1.0f};
        const EmPickupAuraWorkers workers = {slot->world, aura_rand, aura_draw};
        return em_pickup_aura_001F1180(&slot->aura, slot->world, eye, em_scene_state()->d810700,
                                       &workers) == 0 ? 1 : -1;
    }
    case EM_PICKUP_OWNER_PUBLISH: {
        /* 001B17A0: +1 = 001B1630(+0xB0), then 001B1B70 when visible. */
        EmOwnerServicesOwner o;
        memset(&o, 0, sizeof o);
        o.cls = owner->class_flags;
        o.kind = owner->subtype;
        o.model_id = slot->param;
        o.flags2 = owner->item_type;
        memcpy(o.pos, slot->record->position, sizeof slot->record->position);
        o.pos[3] = 1.0f;
        return publish_view(slot->actor, &o);
    }
    case EM_PICKUP_OWNER_TAKE_SOUND:
        /* 001FBD50(self, 0x194, 0, 300.0). Cue 0x194 is not in the exported
         * AREA11 sound scope (the sound export, WP-14): like the status
         * page's system cues it reaches em_sfx_play_at, which drops it, and
         * is reported once. */
        if (argument != 0x194) return 0;
        if (!em_sfx_cue_state(argument)) {
            static int reported;
            if (!reported) {
                reported = 1;
                fprintf(stderr, "AREA11 interaction: pickup take cue 0x194 has no exported sample "
                        "(WP-14); silent\n");
            }
        }
        em_sfx_play_at(argument, slot->record->position, 300.0f);
        return 1;
    default:
        return 0;
    }
}

/* 00183EF0's selector 3/4 item branch: 0019A910(from the player's +16 to
 * the item, mode 6) over the collision world (census L06b: the camera cell
 * walker 001A1390 over the published class-4 cells, then the grid walker
 * 0019D770). It reads the result 0x700031D8, the record's +0x1A halfword and
 * the hit owner 0x700031D4, which is the item's own record when the ray ends
 * in its published cell. 1, or 0 (the world cannot answer: the scan faults). */
static int pickup_ray(void *context, const float from[4], const float to[4], unsigned mode,
                      EmInteractionRayHit *hit)
{
    (void)context;
    if (mode != 6) return 0;
    EmCollSegmentHit h;
    int kind = em_collision_world_0019A910(from, to, mode, &h);
    if (kind < 0) return 0;
    hit->hit = kind != 0;
    hit->flags = kind ? h.record_node : 0;
    hit->kind = (uint32_t)kind;
    hit->owner = (uintptr_t)h.entity;
    return 1;
}

static int bind_pickups(const char *directory)
{
    const EmPickupOriginalHooks hooks = {NULL, pickup_turn, pickup_camera, pickup_status, pickup_event};
    world.services.workers.w_001B1630 = services_visible;
    world.services.workers.w_001B1B70 = services_publish;
    for (size_t i = 0; i < world.scene.count; ++i) {
        EmInteractionSceneOwner *record = &world.scene.owners[i];
        if (record->role != EM_INTERACTION_PICKUP) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/pickup_%08x.emsc", directory, (unsigned)record->callback);
        int bound = em_pickup_original_bind(record, &world.shared, path, &hooks);
        if (bound == -2) continue; /* taken: 001B6660 did not spawn it */
        EmPickupOwner *owner = em_pickup_original_owner(record->uid);
        if (bound != 1 || !owner || world.pickup_count >= HOST_PICKUPS ||
            !em_interaction_scene_bind(&world.scene, record->source_id, owner, &owner->status,
                                       &owner->class_flags, &owner->armed)) {
            fprintf(stderr, "AREA11 interaction: pickup %04X (%06X) did not bind\n",
                    (unsigned)record->uid, (unsigned)record->callback);
            return 0;
        }
        world.pickups[world.pickup_count++] = (HostPickup){.record = record};
    }
    return 1;
}

int em_area11_interaction_host_load(const char *directory,
    const EmItemMath *math, const EmStatusRuntimeHooks *status_hooks)
{
    if (!directory || ((!math) != (!status_hooks)) || world.loaded ||
        !g.grate_present || !g.elev_has_mesh || !g.coll.blob ||
        g.model.bone_count != 22 || !em_frame_gfx()) return 0;
    memset(&world, 0, sizeof world);
    char path[1024];
    snprintf(path, sizeof path, "%s/interaction.emis", directory);
    if (!em_interaction_scene_load(&world.scene, path)) goto failed;
    EmStatusRuntimeHooks native_hooks;
    if (!math) {
        if (!em_item_sdk_math_bind(&world.item_sdk, &world.scene.math, &world.item_math))
            goto failed;
        math = &world.item_math;
        native_hooks = native_status_hooks();
        status_hooks = &native_hooks;
    }
    world.panel_record = em_interaction_scene_role(&world.scene, EM_INTERACTION_PANEL);
    world.elevator_record = em_interaction_scene_role(&world.scene, EM_INTERACTION_ELEVATOR);
    if (!world.panel_record || !world.elevator_record) goto failed;
    float unused[4];
    if (!em_camera_rotation_offset(world.panel_record->angles, 0, world.panel_world, unused))
        goto failed;
    memcpy(world.panel_world + 12, world.panel_record->position, 3 * sizeof(float));
    world.frame.zoom = em_rcl_zoom();
    world.frame.up[1] = -1;
    world.frame.up[3] = 1;
    EmInteractionRuntimeHooks shared = {NULL, acquire, idle, release, publish,
                                         frame_event, retarget};
    if (!em_interaction_runtime_init(&world.shared, &world.frame, &g.model,
                                      world.local_palette, &shared) ||
        !em_interaction_runtime_set_pose_worker(&world.shared, pose) ||
        !em_interaction_runtime_set_cinematic_player_worker(&world.shared, cinematic_player) ||
        !em_player_face_host_load(&world.face, em_frame_gfx(), &g.model, directory,
                                  face_random, NULL)) goto failed;
    EmPanelRuntimeHooks panel = {NULL, align_panel, message_start, message_done,
                                  battery_open, sound, power, stop_indicator};
    snprintf(path, sizeof path, "%s/panel/scripts.emsc", directory);
    /* 00159210 state 0 parks the panel when its power bit is already set. */
    if (!em_panel_runtime_load(&world.panel, path, powered(), &world.shared, &panel))
        goto failed;
    EmElevatorRuntimeHooks elevator = {NULL, align_player, face_player, camera_set,
        camera_publish, camera_chase, message_start, message_done, elevator_sound,
        elevator_rebuild, elevator_copy_child, elevator_update_actor, elevator_retransform};
    snprintf(path, sizeof path, "%s/elevator.emsc", directory);
    /* 00827B10 state 0 reads D_0081083A for its 190/230 floor. */
    const uint8_t *floor = elevator_floor();
    if (!floor || !em_elevator_runtime_load(&world.elevator, path, *floor != 0, &world.shared,
                                            &g.pos[1], &g.cam.tgt[1], &elevator)) goto failed;
    /* The panel and terminal lines run on the live message service. */
    if (!em_message_live_block()) goto failed;
    char item_path[1024];
    snprintf(path, sizeof path, "%s/panel/battery.emba", directory);
    snprintf(item_path, sizeof item_path, "%s/panel/item_root.emir", directory);
    world.status = em_status_runtime_load(path, item_path, math, status_hooks);
    if (!world.status) goto failed;
    /* 0020A7A0's sine 0011E2A8 reads the SDK tables of the user's ELF. */
    if (!em_status_background_load_sdk("assets/sdk_math_tables.emsm")) goto failed;
    /* The hub's 3D models (tools/export_status_models.py). */
    world.models = em_status_models_load("assets/status_models");
    if (!world.models) goto failed;
    /* The original hub's 00209DF0 records (tools/export_status_hub.py). */
    snprintf(path, sizeof path, "%s/panel/status_hub.emhs", directory);
    snprintf(item_path, sizeof item_path, "%s/panel/status_hub_atlas.emha", directory);
    if (!em_status_runtime_bind_hub(world.status, em_status_hub_ui_load(path, item_path, math)))
        goto failed;
    world.panel_class = world.panel_record->class_flags;
    world.elevator_class = world.elevator_record->class_flags;
    world.elevator_status = world.elevator_record->initial_status;
    world.elevator_record->descriptor[1] = world.elevator.owner.lower ? 190 : 230;
    if (!em_interaction_scene_bind(&world.scene, world.panel_record->source_id,
        &world.panel, &world.panel.owner.status, &world.panel_class, &world.panel.owner.armed) ||
        !em_interaction_scene_bind(&world.scene, world.elevator_record->source_id,
        &world.elevator, &world.elevator_status, &world.elevator_class, &world.elevator.owner.armed))
        goto failed;
    if (!bind_pickups(directory)) goto failed;
    if (!em_sfx_set_area(11, 0)) goto failed;
    world.loaded = 1;
    return 1;
failed:
    em_area11_interaction_host_clear();
    return 0;
}

void em_area11_interaction_host_clear(void)
{
    /* This is whole-world teardown: detach the player before releasing
     * its owner tokens. Ordinary script completion uses player_tick. */
    em_sfx_set_area(-1, -1);
    world.shared.owner = NULL;
    em_pickup_original_unbind_all();
    em_player_face_host_free(&world.face);
    em_status_runtime_free(world.status);
    em_status_models_free(world.models, em_frame_gfx());
    em_panel_runtime_free(&world.panel);
    em_elevator_runtime_free(&world.elevator);
    memset(&world, 0, sizeof world);
}

EmInteractionScene *em_area11_interaction_host_scene(void) { return world.loaded ? &world.scene : NULL; }
EmInteractionRuntime *em_area11_interaction_host_shared(void) { return world.loaded ? &world.shared : NULL; }
EmPanelRuntime *em_area11_interaction_host_panel(void) { return world.loaded ? &world.panel : NULL; }
EmElevatorRuntime *em_area11_interaction_host_elevator(void) { return world.loaded ? &world.elevator : NULL; }
EmStatusRuntime *em_area11_interaction_host_status(void) { return world.loaded ? world.status : NULL; }
const EmStatusModels *em_area11_interaction_host_status_models(void)
{ return world.loaded ? world.models : NULL; }
int em_area11_interaction_host_failed(void) { return world.failed; }

int em_area11_interaction_host_face_attach(void)
{
    if (!world.loaded || world.failed) return 0;
    view_load();
    if (!world.shared.owner || (world.frame.player_ready != 1 && world.frame.player_ready != 2) ||
        !em_player_face_host_attach(&world.face)) {
        fail("face attachment");
        return 0;
    }
    world.frame.player_ready = 2;
    view_store();
    return 1;
}

int em_area11_interaction_host_face_talk(uint8_t talking)
{
    if (!world.loaded || world.failed) return 0;
    view_load();
    if (!world.shared.owner || world.frame.player_ready != 2 ||
        !em_player_face_host_talk(&world.face, talking)) {
        fail("direct face talk event");
        return 0;
    }
    return 1;
}

int em_area11_interaction_host_player_record(EmGfxMesh **mesh, const float **palette,
                                            uint32_t *bones, const EmModel **model)
{
    if (!world.loaded || world.failed || !mesh || !palette || !bones || !model) return -1;
    view_load();
    int result = em_player_face_host_record(&world.face, mesh, model);
    if (result < 0 || (world.frame.player_ready == 2) != (result == 1))
        return fail("alternate player draw");
    if (!result) return 0;
    if (!player_pose_owned() || !player_pose_source(NULL, NULL, NULL, NULL))
        return fail("alternate player palette");
    *palette = g.player_palette;
    *bones = g.model.bone_count;
    return 1;
}

const EmOpeningFace *em_area11_interaction_host_face_state(void)
{
    return world.loaded && !world.failed ? em_player_face_host_state(&world.face) : NULL;
}

int em_area11_interaction_host_player(void *unused)
{
    (void)unused;
    if (!world.loaded || world.failed) return -1;
    view_load();
    int result = em_interaction_runtime_player_tick(&world.shared,
        em_status_runtime_ordinary_enabled(world.status));
    view_store();
    return result < 0 ? fail("player worker") : result;
}

/* ---------------------------------------------------------- the Use scan
 *
 * 00160220 (em_player_use_dispatch, bound over the live record by
 * em_player_closure_live): (D_00810E74 & *(u16 *)0x70003B76) != 0 ->
 * 00184BA0; a winner -> 001798D0(player), +5 = 0x25, +6 = 0, return 1;
 * otherwise the surface actions 0015D4C0, the ledge probes 0015DF10, the
 * running jump 0015EC50 and the aim solver 0015FDF0. 0x70003B76 is the USE
 * entry of the pad config block (001AF470's default 0x0040, CROSS in the
 * original layout, em_input.h). 00184BA0 (byte-matched; this host's scan
 * worker): gated on 3B8D, D_0028A9A0 and D_008106EF, clears the score
 * 0x70003B98, walks the previous frame's published list and arms the winner
 * (+0xB = 4) with 3B8D = 3 (em_interaction_scene_scan_checked over
 * em_interaction_scan). The per-object test is 00183EF0 (byte-matched): the
 * panel's class-4 selector-0 type-24 branch (em_panel_candidate) and the
 * elevator's selector-1 branch (em_interaction_elevator_candidate). Its
 * top-level player +0x1F0 == 0x2D path rejects every class but 7; the port
 * polls Use only from the 00161020/001612D0 callbacks (player_use_poll),
 * whose states never hold 0x2D (only 0016D130 writes it), so the action
 * passed is 0. Only the owners bound here are published: the panel, the
 * elevator and (WP-6) the seven item owners; the door and Roger keep their
 * legacy scans until WP-7/9 bind them (W22). */
enum { USE_MASK_3B76 = 0x0040 };

static int use_predicate(void *context, const EmInteractionCandidate *candidate, float *score)
{
    (void)context;
    const EmInteractionSceneOwner *record = candidate->owner;
    if (record == world.panel_record)
        return em_panel_candidate(&world.panel.owner, record->position, record->angles[1], g.pos,
                                  g.yaw, score);
    if (record == world.elevator_record)
        return em_interaction_elevator_candidate(record->descriptor, g.pos, g.yaw, 0, score);
    if (record->role == EM_INTERACTION_PICKUP && record->native_owner) {
        /* 00183EF0's selector 3/4 item branch; +0x30 is the {10, 3.5}
         * descriptor, D_008105E0 the view target of the action-0x2D path.
         * Its identity is the owner's record (+0x14), which the ray's hit
         * owner 0x700031D4 names when the ray ends in the item's cell. */
        const HostPickup *slot = pickup_slot(record->source_id);
        if (!slot || !slot->actor) return -1;
        EmInteractionPickup item = {.identity = (uintptr_t)slot->actor->self,
            .class_flags = *record->live_class_flags, .subtype = record->subtype,
            .selector = record->selector, .callback = record->callback,
            .descriptor = {record->descriptor[0], record->descriptor[1]}};
        memcpy(item.position, record->position, sizeof item.position);
        memcpy(item.angles, record->angles, sizeof item.angles);
        EmInteractionPlayer player = {.yaw = g.yaw, .action = 0};
        memcpy(player.position, g.pos, sizeof player.position);
        memcpy(player.view_target, g.cam.tgt, sizeof player.view_target);
        return em_interaction_pickup_candidate(&item, &player, &world.scene.math, pickup_ray, NULL,
                                               score);
    }
    if (record == world.roger_record && world.roger_actor) {
        /* 00183EF0's selector-0 class-10 branch (em_roger_candidate): the
         * {10, 20} descriptor at +0x30 around Roger's +0xB0. */
        EmInteractionPlayer player = {.yaw = g.yaw, .action = 0};
        memcpy(player.position, g.pos, sizeof player.position);
        memcpy(player.view_target, g.cam.tgt, sizeof player.view_target);
        return em_roger_candidate(record->descriptor, world.roger_actor->pos, &player, &world.scene.math,
                                  score);
    }
    if (record == world.door_record && world.door_actor) {
        /* 00183EF0's selector-0 class-5 branch (em_door_candidate, subtype
         * 3): the doorway descriptor at +0x30 around the owner's +0xB0 and
         * +0xC4. */
        EmInteractionPlayer player = {.yaw = g.yaw, .action = 0};
        memcpy(player.position, g.pos, sizeof player.position);
        memcpy(player.view_target, g.cam.tgt, sizeof player.view_target);
        return em_door_candidate(record->descriptor, world.door_actor->pos, world.door_actor->rot[1],
                                 world.door_actor->model, &player, &world.scene.math, score);
    }
    return -1;
}

int em_area11_interaction_host_scan_00184BA0(void *context, EmPlayerLiveActor *actor, int *result)
{
    (void)context;
    (void)actor;
    if (!result) return -1;
    *result = 0;
    if (!world.loaded || world.failed) return -1;
    EmSceneState *scene = em_scene_state();
    EmInteractionScanState state = {scene->spad3B8D, (int16_t)em_frame_transition()->substate,
                                    scene->req[EM_SCENE_REQ_EF], world.scan_score};
    /* A gated 00184BA0 returns before it reads the list. */
    if (!state.selector && !state.fade_wait && !state.inhibited && published_view() < 0)
        return fail("00184BA0 published list (an owner the host does not run)");
    size_t winner;
    int found = em_interaction_scene_scan_checked(&world.scene, &state, use_predicate, NULL,
                                                  &winner);
    world.scan_score = state.score;
    if (found < 0) return fail("00184BA0 use scan");
    if (!found) return 0;
    /* The winner's controller takes the shared owner token; the claim is
     * 00184BA0's 3B8D = 3 through the frame view (scan_checked armed +0xB). */
    view_load();
    const EmInteractionSceneOwner *record = world.scene.list.active[winner].owner;
    int claimed = em_interaction_runtime_claim(&world.shared, record->native_owner);
    view_store();
    /* Roger's armed talk 0x828810 runs on the AREA11 script host: his
     * takeover is a script owner's (00183090 on the record). */
    if (claimed && (record == world.roger_record || record == world.door_record))
        world.script_owner = record->native_owner;
    if (!claimed || scene->spad3B8D != 3) return fail("00184BA0 winner claim");
    *result = 1;
    return 0;
}

int em_area11_interaction_host_use(void *unused)
{
    (void)unused;
    if (!world.loaded || world.failed) return -1;
    int result;
    if (em_player_closure_live_use_press(player_states_actor_mut(), &result) < 0)
        return fail("00160220 (the Use dispatcher)");
    return result;
}

int em_area11_interaction_host_panel_tick(void)
{
    if (!world.loaded || world.failed) return -1;
    if (!em_status_runtime_ordinary_enabled(world.status)) return 0;
    view_load();
    script_load(&world.panel.program.script);
    int result = em_panel_runtime_tick(&world.panel, em_pickup_item_count(0x1B) != 0, 1);
    script_store(&world.panel.program.script);
    view_store();
    if (result < 0) return fail("panel worker");
    /* 00159210 state 1 always ends with 001B17A0(p), then its virtual. */
    if (!world.panel_actor) return fail("panel publication (no pool record bound)");
    return publish_owner(world.panel_actor, world.panel_class, world.panel_actor->pos) >= 0
               ? 0 : fail("panel publication");
}

/* The pool record of a host owner (census L07): the panel 00159210, the
 * terminal 00827B10 and the item owners, bound by their nodes' first call.
 * The record is what the class lists hold and what the walkers read (+0x00,
 * +0x02, +0x0E, +0x54, +0xB0..). Its placement must be the owner's EMIS
 * placement (the terminal's +0xB4 is set by its state 0 instead). */
int em_area11_interaction_host_bind_actor(uint32_t source_id, EmActor *actor)
{
    if (!world.loaded || world.failed) return -1;
    EmInteractionSceneOwner *record = em_interaction_scene_find(&world.scene, source_id);
    if (!record || !actor || actor->self != actor) return fail("pool record binding");
    const int elevator = record == world.elevator_record;
    if (actor->pos[0] != record->position[0] || actor->pos[2] != record->position[2] ||
        (!elevator && actor->pos[1] != record->position[1]))
        return fail("pool record placement differs from the owner's");
    if (record == world.panel_record) {
        if (world.panel_actor) return fail("panel pool record bound twice");
        world.panel_actor = actor;
        return 0;
    }
    if (elevator) {
        if (world.elevator_actor) return fail("terminal pool record bound twice");
        world.elevator_actor = actor;
        return 0;
    }
    HostPickup *slot = pickup_slot(source_id);
    if (!slot || slot->actor || slot->state0) return fail("item pool record binding");
    slot->actor = actor;
    return 0;
}

/* 00827B10 state 0 (overlay AREA11, 0x827B54..0x827BF0): it loads
 * D_0081083A, stores 190.0 or 230.0 into its +0xB4 and into the script's
 * height words (the fields em_elevator_init derives from the same byte),
 * then 001B0FD0 and 001C6380 build its matrix from +0xB0/+0xC0; its
 * 001AFA90 child copies that position (the port's indicator reads the
 * parent's node0 matrix). The manifest placement (em_scene.c) is always
 * the upper floor, so without this an AREA11 rebuild after the ride would
 * draw the elevator at 230 while the owner and its Use descriptor say 190.
 * State 0 runs on a fresh actor, before its first 001B17A0 offer: an owner
 * that already ran is a fault, not something to re-initialize. After
 * 001C6380 (0x827BF0) it re-transforms its collision cell (uid 4) with
 * 001A2370(self, +0xD0) (0x827C04), before the child spawn (census L07). */
/* Roger 008237E0's record (area11[8]; em_area11_roger, census L22): its
 * EMIS record (source 0x82A500) is bound to the pool record's +0x00,
 * +0x02 and +0x0B (the arm 00184BA0 writes), the record itself being the
 * owner token the scan's claim and the script host's takeover share. Its
 * placement must be the EMIS placement. 0, or -1. */
int em_area11_interaction_host_bind_roger(EmActor *actor)
{
    if (!world.loaded || world.failed) return -1;
    EmInteractionSceneOwner *record = em_interaction_scene_role(&world.scene, EM_INTERACTION_ROGER);
    if (!record || !actor || actor->self != actor) return fail("Roger pool record binding");
    if (actor->pos[0] != record->position[0] || actor->pos[1] != record->position[1] ||
        actor->pos[2] != record->position[2])
        return fail("Roger's pool record placement differs from the EMIS record");
    if (world.roger_actor && world.roger_actor != actor) return fail("Roger pool record bound twice");
    if (!em_interaction_scene_bind(&world.scene, record->source_id, actor, &actor->status, &actor->cls,
                                   &actor->u0A[1]))
        return fail("Roger EMIS binding");
    world.roger_actor = actor;
    world.roger_record = record;
    return 0;
}

/* The fence door 001BC350's record (census L18, em_area11_door.c): its EMIS
 * record (source 0x82A3C0) bound to the pool record's +0x00, +0x02 and +0x0B
 * (the arm 00184BA0 writes), the record itself being the owner token of the
 * scan's claim and of the script host's takeover. Its placement must be the
 * EMIS placement. The EMIS record is returned through *source (the door
 * runtime's resource check). 0, or -1. */
int em_area11_interaction_host_bind_door(EmActor *actor, const EmInteractionSceneOwner **source)
{
    if (!world.loaded || world.failed) return -1;
    EmInteractionSceneOwner *record = em_interaction_scene_role(&world.scene, EM_INTERACTION_DOOR);
    if (!record || !actor || actor->self != actor) return fail("door pool record binding");
    if (actor->pos[0] != record->position[0] || actor->pos[1] != record->position[1] ||
        actor->pos[2] != record->position[2] || actor->rot[1] != record->angles[1])
        return fail("the door's pool record placement differs from the EMIS record");
    if (world.door_actor && world.door_actor != actor) return fail("door pool record bound twice");
    if (!em_interaction_scene_bind(&world.scene, record->source_id, actor, &actor->status, &actor->cls,
                                   &actor->u0A[1]))
        return fail("door EMIS binding");
    world.door_actor = actor;
    world.door_record = record;
    if (source) *source = record;
    return 0;
}

/* 001B1630 on g.cam.eye / g.cam.fwd (D_008105D0 / D_00810600), for owners
 * outside the host (the door's 001B1B30). */
int em_area11_interaction_host_visible_001B1630(const float position[3])
{
    return em_interaction_visible(position, g.cam.eye, g.cam.fwd);
}

int em_area11_interaction_host_elevator_state0(void)
{
    if (!world.loaded || world.failed) return -1;
    const uint8_t *floor = elevator_floor();
    if (!floor) return fail("D_0081083A");
    if (world.elevator.owner.phase || world.elevator.owner.armed)
        return fail("00827B10 state 0 after the owner ran");
    em_elevator_init(&world.elevator.owner, *floor != 0);
    world.elevator_record->descriptor[1] = world.elevator.owner.lower ? 190 : 230;
    elevator_rebuild(NULL, world.elevator.owner.height);
    float matrix[16];
    if (retransform_record(world.elevator_actor, world.elevator.owner.height, matrix) < 0)
        return fail("00827B10 state 0: 001A2370 (no pool record or collision world)");
    return 0;
}

int em_area11_interaction_host_elevator_tick(void)
{
    if (!world.loaded || world.failed) return -1;
    if (!em_status_runtime_ordinary_enabled(world.status)) return 0;
    uint8_t *floor = elevator_floor();
    if (!floor) return fail("D_0081083A");
    world.elevator.owner.lower = *floor != 0;
    view_load();
    script_load(&world.elevator.program.script);
    world.offer_failed = 0;
    int result = em_elevator_runtime_tick(&world.elevator, powered(), 1);
    script_store(&world.elevator.program.script);
    view_store();
    *floor = world.elevator.owner.lower;
    world.elevator_record->descriptor[1] = world.elevator.owner.lower ? 190 : 230;
    if (result < 0) return fail("elevator worker");
    return world.offer_failed ? fail("elevator publication") : result;
}

/* The pickup node's first call (state 0). 0015AFA0 runs 0015AC00: the
 * +0x60 scale by the model id +0x0D, 001C6380's +0xD0 matrix (the aura's
 * facing test reads it) and 001F1110(self, variant by +0x03 & 0xF), whose
 * rand() is the first draw of the aura. 00219550 (NEARMISS
 * src/func_00219550.c state 0) runs 001C6380 over its record (+0xB0, +0xC0,
 * the +0x60 scale 001AFA90 left at 1.0) and then 001A2370(self, +0xD0): its
 * collision cell moves to the item (census L07). The instance's model, bones
 * and palette are em_pickup's (em_pickup_add); 00219550's 001C5570 child is
 * spawned by the node itself after this call, as the original spawns it
 * after 001A2370. */
int em_area11_interaction_host_pickup_state0(uint32_t source_id, uint8_t model, uint8_t param)
{
    if (!world.loaded || world.failed) return -1;
    HostPickup *slot = pickup_slot(source_id);
    if (!slot || slot->state0) return fail("pickup state 0 (unbound or repeated)");
    slot->state0 = 1;
    slot->model = model;
    slot->param = param;
    if (slot->record->callback == 0x00219550u) {
        if (!slot->actor || retransform_record(slot->actor, slot->actor->pos[1], slot->world) < 0)
            return fail("00219550 state 0: 001C6380 / 001A2370 (no pool record or collision world)");
        return 0;
    }
    if (slot->record->callback != 0x0015AFA0u) return 0;
    float scale = 1.0f;
    switch (param) {
    case 0x5B: scale = 1.5f; break;
    case 0x6D: case 0x6C: case 0x59: case 0x57: case 0x56: case 0x55: case 0x4F: case 0x4E:
    case 0x4D: case 0x45: case 0x42: case 0x41: case 0x40: scale = 2.0f; break;
    default: break;
    }
    const float scales[3] = {scale, scale, scale};
    if (em_owner_services_build_trs_matrix(slot->world, slot->record->position, slot->record->angles,
                                           scales) != 0)
        return fail("0015AC00 placement");
    int16_t variant;
    switch (model & 0xF) {
    case 1: variant = 1; break;
    case 2: variant = 4; break;
    case 0: variant = param == 0x34 ? 5 : 0; break;
    default: variant = 0; break;
    }
    const EmPickupAuraWorkers workers = {slot->world, aura_rand, aura_draw};
    return em_pickup_aura_001F1110(&slot->aura, variant, &workers) == 0 ? 0 : fail("001F1110");
}

/* Every later call: 00219550 states 1..3 / 0015AFA0 states 1..3 over the
 * shared frame view, the owner's program skip byte (3B91) and its
 * publication. D_00810354 is g.pos[1]; D_008104A0 (player +0x1F0) is 0 on
 * every port path (0x2D is written only by 0016D130, which the port does
 * not run) and D_008104E6 (player +0x236) has no port writer: both are
 * passed as 0. 1 while allocated, 0 on the call that freed the owner (the
 * node then frees itself, 001AFC10), -1 fault. Status frames do not tick
 * the owners. */
int em_area11_interaction_host_pickup_tick(uint32_t source_id)
{
    if (!world.loaded || world.failed) return -1;
    HostPickup *slot = pickup_slot(source_id);
    if (!slot || !slot->state0) return fail("pickup tick before state 0");
    if (!em_status_runtime_ordinary_enabled(world.status)) return 1;
    const uint16_t uid = slot->record->uid;
    EmScript *script = em_pickup_original_script(uid);
    if (!script) return fail("pickup program");
    view_load();
    script_load(script);
    world.pickup_current = slot;
    /* The aura's D_70003B92 gate is read at its event (after the script
     * step), so the owner core is handed 0 here. */
    int result = em_pickup_original_tick_one(uid, g.pos[1], 0, 0, 0);
    world.pickup_current = NULL;
    script_store(script);
    view_store();
    if (result < 0) return fail("pickup owner");
    const EmPickupOwner *owner = slot->record->native_owner;
    if (!slot->actor || !owner) return fail("pickup owner without its pool record");
    /* The record's +0x02 is the owner's class byte (00219550 writes 0x87
     * when armed and 4 at its completion): stored after every owner call,
     * so a walker or the Use scan reads the current value. */
    slot->actor->cls = owner->class_flags;
    if (!result) slot->actor = NULL; /* the node frees the record (001AFC10) */
    return result;
}


/* ------------------------------------------------ status screens (0020E060/0020CDC0)
 *
 * Every status screen in AREA11 runs the original page layer of the
 * host's status runtime at the scene core's 0020E060 and 0020CDC0
 * positions: a pending request (D_008106B0 != 0: the panel's 00157F60
 * BATTERY request, a battery pickup's 001C47A0 ITEM request) and the
 * START/TRIANGLE hub (B0 == 0, C5 == 0: the original hub above). For the
 * panel's request D_008106D0 names the owner the page talks to; 0020CDC0
 * case 0 ignores B1 without a request (a stale B1 stays from the last
 * request). */
int em_area11_interaction_host_status_open(void)
{
    if (!world.loaded || world.failed) return -1;
    EmSceneState *scene = em_scene_state();
    const uint8_t *d0 = em_scene_req_at(scene, 0x008106D0u);
    uint32_t address = (uint32_t)d0[0] | (uint32_t)d0[1] << 8 | (uint32_t)d0[2] << 16 |
                       (uint32_t)d0[3] << 24;
    EmPanel *owner = NULL;
    if (scene->req[EM_SCENE_REQ_B0] != 0 && (scene->req[EM_SCENE_REQ_B1] & 0x80)) {
        /* The BATTERY route talks to the panel D_008106D0 names. */
        if (!world.panel_address || address != world.panel_address)
            return fail("0020E060: D_008106D0 is not the bound panel");
        owner = &world.panel.owner;
    }
    if (em_status_runtime_page_open(world.status, owner) != 1) return fail("0020E060");
    world.status_route = 1;
    return 1;
}

int em_area11_interaction_host_status_page(const EmStatusInput *input)
{
    if (!world.loaded || world.failed || !world.status_route) return -1;
    EmSceneState *scene = em_scene_state();
    int result = em_status_runtime_page_tick(world.status, input, &scene->req[EM_SCENE_REQ_B0],
        &scene->req[EM_SCENE_REQ_B1], &scene->req[EM_SCENE_REQ_C5],
        em_scene_req_at(scene, 0x008106CCu));
    if (result < 0) {
        /* A request whose page is not translated: name it (0020CDC0 case
         * 0's mapping of B0/B1). */
        const uint8_t b0 = scene->req[EM_SCENE_REQ_B0], b1 = scene->req[EM_SCENE_REQ_B1];
        const char *page = b0 == 2 ? "MAP 0020F950" : b0 == 3 ? "DATABASE 00214020" :
                           b0 != 1 || (b1 & 0x80) || (b1 >= 0x1B && b1 <= 0x1D) ? NULL :
                           b1 < 0x17 ? "SPR4 00211970" : "the ITEM child 002160B0";
        if (page && !world.failed)
            fprintf(stderr, "AREA11 interaction: status request B0 = %u, B1 = %#04x opens %s, "
                    "which is not translated\n", (unsigned)b0, (unsigned)b1, page);
        return fail("0020CDC0");
    }
    return result;
}

/* 1 from the 0020E060 that took the request route until the next status
 * open: the status frames' 001D1EA0(0) draws this route's page. */
int em_area11_interaction_host_status_route(void)
{
    return world.loaded && !world.failed && world.status_route;
}

int em_area11_interaction_host_status_render(EmGfx *gfx)
{
    if (!world.loaded || world.failed) return -1;
    return em_status_runtime_render(world.status, gfx) == 1 ? 1 : fail("status draw");
}

void em_area11_interaction_host_status_clear_route(void)
{
    world.status_route = 0;
}

void em_area11_interaction_host_set_panel_address(uint32_t panel)
{
    world.panel_address = panel;
}

/* The message service's host hooks (em_message_live.h).
 *
 * message_gate is a PORT STAND-IN, not original behaviour: the original
 * 001FCA10 runs at step F every frame with no gate. The port holds step F
 * while the AREA11 status page layer runs because that layer still presents
 * its mode-4 lines from its own copy of the request block (WP-5): the mode-4
 * presenters 001FD0E0 and 001FCB90 / 001FCF90 / 001FCF60 are not translated,
 * so the page's lines cannot go through the one block yet. Consequence while
 * a page is open: a mode-2 line's delay and timer are frozen instead of
 * running (or being replaced by the page's line, as in the original). The
 * gate goes when those presenters are translated; keeping it until then is
 * an open lead decision (docs/FIRST_LEVEL_AUDIT.md WP-8).
 *
 * Slot-0 talk in game mode 2 is 001D06E0 on the player face. */
static int message_gate(void *context)
{
    (void)context;
    if (!world.loaded || world.failed) return -1;
    return em_status_runtime_ordinary_enabled(world.status) ? 1 : 0;
}

static int message_face_talk(void *context, int on)
{
    (void)context;
    return em_area11_interaction_host_face_talk((uint8_t)(on != 0));
}

const EmMessageLiveHost *em_area11_interaction_host_message_host(void)
{
    static const EmMessageLiveHost host = {NULL, message_face_talk, message_gate};
    return &host;
}
