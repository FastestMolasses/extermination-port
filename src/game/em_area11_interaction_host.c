#include "game/em_area11_interaction_host.h"
#include "game/em_camera.h"
#include "game/em_camera_rotation.h"
#include "game/em_frame.h"
#include "game/em_weapon.h"
#include "game/em_interaction_alignment.h"
#include "game/em_item_device.h"
#include "game/em_item_sdk_math.h"
#include "game/em_opening_media.h"
#include "game/em_panel_message.h"
#include "game/em_pickup.h"
#include "game/em_pickup_original.h"
#include "game/em_props.h"
#include "game/em_random.h"
#include "game/em_scene_bindings.h"
#include "game/em_sfx.h"
#include "game/em_status_background.h"
#include "game/em_status_models.h"

/* The owner token addresses remain stable until whole-world teardown. */
static struct {
    EmInteractionScene scene;
    EmInteractionFrame frame;
    EmInteractionRuntime shared;
    EmInteractionProjection projection;
    EmPanelRuntime panel;
    EmElevatorRuntime elevator;
    EmPanelMessage panel_message, elevator_message;
    EmPanelMessage *message;
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
    /* D_002821B0 (the request kind) and D_002821B8 (the token) as the
     * message command 001B7D60 case 0 stores them; 001FC9B0 clears them
     * with the rest of the block. The phase D_002821B4 is the presenter's
     * own, the delay D_002821BC its countdown. */
    uint32_t message_kind, message_token;
    float scan_score;
    int status_route, offer_failed;
} world;

static int camera_publish(void *context);

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

static int idle(void *context, float *palette)
{
    (void)context;
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

static int cinematic_player(void *context, float *palette)
{
    (void)context;
    /*83090 advances the attached face before choosing a body request. A
     * prepared mesh alone is not an attached original face allocation. */
    if (!world.face.attached || !em_player_face_host_tick_before_body(&world.face)) return -1;
    if (player_pose_cinematic_active()) return player_pose_cinematic_tick(palette, 0);
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
        /* Full001D2610(0)/001D2590/SDK tan path, bits43F02F4F.
         * Its fog endpoints remain the unchanged current rig at input0. */
        g.cam.zoom = 0x1.e05e9ep+8f;
        return 1;
    case EM_INTERACTION_ZOOM_DEFAULT:
        g.cam.zoom = 480;
        return 1;
    case EM_INTERACTION_FADE_IN:
        em_frame_fade_start(-1, 4);
        return 1;
    case EM_INTERACTION_RELEASE_SKELETON:
        if (!world.face.attached || world.face.failed) return 0;
        em_player_face_host_detach(&world.face);
        return 1; /* The original frame core writes player_ready1 next. */
    case EM_INTERACTION_RESUME_MUSIC:
        /* Aborted cinematic music remains a required separate binding. */
        return 0;
    }
    return 0;
}

static int retarget(void *context)
{
    (void)context;
    float hip[3], euler[3];
    return player_pose_hip(hip) && player_pose_script_euler(euler) &&
        camera_interaction_retarget_area11(&g.cam, hip, euler, g.cam_dist_param);
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

static int message_start(void *context, uint32_t token, uint32_t delay)
{
    (void)context;
    EmPanelMessage *message = token == 0x80000018u ? &world.panel_message :
        token == 0x8000001Au ? &world.elevator_message : NULL;
    if (!message || (world.message && world.message->phase == 1) ||
        !em_panel_message_start(message, token, delay)) return 0;
    /* 001B7D60 case 0: D_002821B0 = 2, B4 = 1, B8 = req[5], BC = req[6]. */
    world.message_kind = 2;
    world.message_token = token;
    world.message = message;
    world.frame.message_phase = (int32_t)message->phase;
    return 1;
}

static int message_done(void *context)
{
    (void)context;
    return world.message ? em_panel_message_done(world.message) : -1;
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

static int stop_indicator(void *context)
{
    (void)context;
    em_props_panel_complete();
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
    case EM_STATUS_RESET_SOUNDS:
        em_sfx_stop_all();
        return 1;
    case EM_STATUS_STOP_STREAMS:
        em_bgm_stop(0);
        em_opening_media_stop();
        return 1;
    case EM_STATUS_CHANNEL_ZERO:
    case EM_STATUS_CHANNEL_ONE:
        /* Native decoded streams retain their full-scale PCM gain. Both
         * stopped channels have no independent SPU gain to reset. */
        return 1;
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
    case EM_STATUS_RESUME_MUSIC:
        /* AREA11's initial music state resumes original cue25. Resource
         * prepare owns that cue path; failure does not complete status. */
        return em_opening_media_resume_music(270 + ((em_random_next() >> 16) & 127)) == 0;
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
        g.cam.zoom = 0x1.e05e9ep+8f;
        return 1;
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

static int owner_available(void *context, EmPanel *panel, unsigned item_id)
{
    (void)context;
    EmItemDevice devices[EM_INTERACTION_CAPACITY];
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
    return em_status_models_render(world.models, gfx, g.cam.zoom) == 1;
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
    /* Original001DD980 publishes the render-context center/depth values,
     * including the first camera command's yielding callback. */
    return em_interaction_projection_publish(&world.projection, g.cam.eye, g.cam.tgt);
}

int em_area11_interaction_host_camera_publish(void)
{
    if (!world.loaded || world.failed) return -1;
    return camera_publish(NULL) ? 1 : fail("camera publication");
}

static int camera_chase(void *context)
{
    (void)context;
    float hip[3], euler[3];
    return player_pose_hip(hip) && player_pose_script_euler(euler) &&
        camera_interaction_retarget_distance_area11(&g.cam, hip, euler, -20,
                                                     g.cam_dist_param);
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

/* 001B17A0 (byte-matched), the owners' state-1 tail: +1 = 001B1630(+0xB0,
 * +0xB4, +0xB8), the camera cone/range gate against D_008105D0 and
 * D_00810600 (g.cam.eye/fwd), and when visible 001B1B70, which pushes an
 * owner with class bit 0x80 onto the pending interactive list (001B1DE0).
 * 001AAD00 swaps that list in at the end of the frame
 * (em_area11_interaction_host_publish). The port draws the props from its
 * own draw list, so the +1 drawn byte has no reader here. */
static int offer(const EmInteractionSceneOwner *record, const float position[3])
{
    return em_interaction_scene_offer(&world.scene, record->source_id, position,
                                      g.cam.eye, g.cam.fwd) >= 0;
}

/* 00827B10's tail at 0x827E78: 001B17A0 (publication), then its virtual
 * +0x4C update, which has no port counterpart. */
static void elevator_update_actor(void *context)
{
    (void)context;
    const float position[3] = {g.elev_pos[0], world.elevator.owner.height, g.elev_pos[2]};
    if (!offer(world.elevator_record, position)) world.offer_failed = 1;
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
    if (world.message) f->message_phase = (int32_t)world.message->phase;
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
    if (world.message) world.message->phase = (uint32_t)f->message_phase;
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
    world.frame.zoom = g.cam.zoom;
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
        elevator_rebuild, elevator_copy_child, elevator_update_actor};
    snprintf(path, sizeof path, "%s/elevator.emsc", directory);
    /* 00827B10 state 0 reads D_0081083A for its 190/230 floor. */
    const uint8_t *floor = elevator_floor();
    if (!floor || !em_elevator_runtime_load(&world.elevator, path, *floor != 0, &world.shared,
                                            &g.pos[1], &g.cam.tgt[1], &elevator)) goto failed;
    snprintf(path, sizeof path, "%s/panel/terminal.emod", directory);
    if (!em_panel_message_load(&world.panel_message, path)) goto failed;
    snprintf(path, sizeof path, "%s/elevator_refusal.emod", directory);
    if (!em_panel_message_load(&world.elevator_message, path)) goto failed;
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
    em_player_face_host_free(&world.face);
    em_status_runtime_free(world.status);
    em_status_models_free(world.models, em_frame_gfx());
    em_panel_runtime_free(&world.panel);
    em_elevator_runtime_free(&world.elevator);
    em_panel_message_free(&world.panel_message);
    em_panel_message_free(&world.elevator_message);
    memset(&world, 0, sizeof world);
}

EmInteractionScene *em_area11_interaction_host_scene(void) { return world.loaded ? &world.scene : NULL; }
EmInteractionRuntime *em_area11_interaction_host_shared(void) { return world.loaded ? &world.shared : NULL; }
EmPanelRuntime *em_area11_interaction_host_panel(void) { return world.loaded ? &world.panel : NULL; }
EmElevatorRuntime *em_area11_interaction_host_elevator(void) { return world.loaded ? &world.elevator : NULL; }
EmStatusRuntime *em_area11_interaction_host_status(void) { return world.loaded ? world.status : NULL; }
const EmStatusModels *em_area11_interaction_host_status_models(void)
{ return world.loaded ? world.models : NULL; }
const EmInteractionProjection *em_area11_interaction_host_projection(void)
{ return world.loaded ? &world.projection : NULL; }
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
 * 00160220's head (NEARMISS; logic recovered): (D_00810E74 & *(u16
 * *)0x70003B76) != 0 -> 00184BA0; a winner -> 001798D0(player), +5 = 0x25,
 * +6 = 0, return 1. 0x70003B76 is the USE entry of the pad config block,
 * whose default is 0x0040, CROSS in the original layout (em_input.h); the
 * port has no configurable block. 00184BA0 (byte-matched): gated on 3B8D,
 * D_0028A9A0 and D_008106EF, clears the score 0x70003B98, walks the
 * previous frame's published list and arms the winner (+0xB = 4) with
 * 3B8D = 3 (em_interaction_scene_scan_checked over em_interaction_scan).
 * The per-object test is 00183EF0 (byte-matched): the panel's class-4
 * selector-0 type-24 branch (em_panel_candidate) and the elevator's
 * selector-1 branch (em_interaction_elevator_candidate). Its top-level
 * player +0x1F0 == 0x2D path rejects every class but 7; the port polls Use
 * only from the 00161020/001612D0 callbacks (player_use_poll), whose
 * states never hold 0x2D (only 0016D130 writes it), so the action passed is
 * 0. Only the owners bound here are published: the pickups, the door and
 * Roger keep their legacy scans until WP-6/7/9 bind them (W22). */
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
    return -1; /* only the two bound owners are ever offered */
}

int em_area11_interaction_host_use(void *unused)
{
    (void)unused;
    if (!world.loaded || world.failed) return -1;
    EmSceneState *scene = em_scene_state();
    if (!(scene->d810E74 & USE_MASK_3B76)) return 0;
    EmInteractionScanState state = {scene->spad3B8D, (int16_t)em_frame_transition()->substate,
                                    scene->req[EM_SCENE_REQ_EF], world.scan_score};
    size_t winner;
    int result = em_interaction_scene_scan_checked(&world.scene, &state, use_predicate, NULL,
                                                   &winner);
    world.scan_score = state.score;
    if (result < 0) return fail("00184BA0 use scan");
    if (!result) return 0;
    /* The winner's controller takes the shared owner token; the claim is
     * 00184BA0's 3B8D = 3 through the frame view (scan_checked armed +0xB). */
    view_load();
    const EmInteractionSceneOwner *record = world.scene.list.active[winner].owner;
    int claimed = em_interaction_runtime_claim(&world.shared, record->native_owner);
    view_store();
    if (!claimed || scene->spad3B8D != 3) return fail("00184BA0 winner claim");
    if (!player_pose_use_accepted()) return fail("001798D0 use acceptance");
    return 1;
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
    return offer(world.panel_record, world.panel_record->position) ? 0 : fail("panel publication");
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
 * that already ran is a fault, not something to re-initialize. */
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

/* 001AAD00's interactive-list swap (D_00275B5C/B64 = the pending list, then
 * the pending list is emptied); its other hooks and class lists have no
 * port counterpart yet. */
void em_area11_interaction_host_publish(void)
{
    if (world.loaded && !world.failed) em_interaction_scene_publish(&world.scene);
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
    if (result < 0) return fail("0020CDC0");
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

int em_area11_interaction_host_message_tick(int busy155, int busy156)
{
    if (!world.loaded || world.failed) return -1;
    if (!em_status_runtime_ordinary_enabled(world.status) || !world.message) return 0;
    /* 1: phase 2's FDB80(1)/001FC9B0 teardown, which memsets the block
     * D_002821B0..+0x9C (kind and token included). */
    if (em_panel_message_tick(world.message, busy155, busy156) == 1)
        world.message_kind = world.message_token = 0;
    return 1;
}

/* The step-F service (em_frame_set_message_service). D_00282155/156, the
 * voice lanes 1/2 that 001FA5F0/001FA790 mark busy, have no port player:
 * no voice cue is pushed on the first-level route before Roger, and the
 * panel and terminal lines 0x80000018/0x8000001A are text-only, so both
 * read 0 (FINDINGS: idle for text-only lines). */
static int message_service_tick(void *context)
{
    (void)context;
    return em_area11_interaction_host_message_tick(0, 0) < 0 ? -1 : 0;
}

static void message_service_render(void *context, EmGfx *gfx)
{
    (void)context;
    em_area11_interaction_host_message_render(gfx);
}

void em_area11_interaction_host_message_block(uint32_t block[3])
{
    block[0] = block[1] = block[2] = 0;
    if (!world.loaded || !world.message || !world.message->phase) return;
    block[0] = world.message_kind;
    block[1] = world.message->phase;
    block[2] = world.message_token;
}

const EmFrameMessageService *em_area11_interaction_host_message_service(void)
{
    static const EmFrameMessageService service = {message_service_tick, message_service_render,
                                                  NULL};
    return &service;
}

void em_area11_interaction_host_message_render(EmGfx *gfx)
{
    if (world.loaded && !world.failed && world.message &&
        em_status_runtime_ordinary_enabled(world.status))
        em_panel_message_render(world.message, gfx);
}
