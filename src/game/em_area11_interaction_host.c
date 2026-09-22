#include "game/em_area11_interaction_host.h"
#include "game/em_camera.h"
#include "game/em_camera_rotation.h"
#include "game/em_interaction_alignment.h"
#include "game/em_item_device.h"
#include "game/em_item_sdk_math.h"
#include "game/em_opening_media.h"
#include "game/em_panel_message.h"
#include "game/em_pickup.h"
#include "game/em_pickup_original.h"
#include "game/em_props.h"
#include "game/em_random.h"
#include "game/em_sfx.h"

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
    EmStatusRuntime *status;
    EmItemSdkMath item_sdk;
    EmItemMath item_math;
    EmInteractionSceneOwner *panel_record, *elevator_record;
    float local_palette[22 * 16];
    float panel_world[16];
    uint8_t panel_class, elevator_status, elevator_class;
    int loaded, failed, status_draw_context, status_ui_context;
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
    case EM_INTERACTION_RESUME_MUSIC:
        /* These are the alternate-skeleton/aborted-script routes. The
         * ordinary first-level interactions never exercise either worker;
         * missing bindings must retain ownership if a new route reaches it. */
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
    world.message = message;
    world.frame.message_phase = (int32_t)message->phase;
    return 1;
}

static int message_done(void *context)
{
    (void)context;
    return world.message ? em_panel_message_done(world.message) : -1;
}

static int battery_open(void *context, EmPanel *owner, uint8_t request)
{
    (void)context;
    return owner == &world.panel.owner &&
        em_status_runtime_battery_open(world.status, owner, request) == 1;
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

static int power(void *context, uint8_t mask)
{
    (void)context;
    if (mask != 0x80) return 0;
    g.terminal_powered = 1;
    return 1;
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
        /* The original phase3 completion and interaction scripts write
         * the SAME byte. Publish only its status write, never a stale
         * status copy after ordinary camera callbacks start decrementing. */
        if (frame->phase == 5) world.frame.recovery_lock = frame->recovery_lock;
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
        /* Native status sprites and untextured triangles already use
         * the original identity/Y-flip UI coordinate convention. Keep
         * paused world camera vectors available for the final commit. */
        world.status_ui_context = 1;
        g.cam.zoom = 0x1.e05e9ep+8f;
        return 1;
    case EM_STATUS_PAGE_CLEAR_DRAW:
    case EM_STATUS_PAGE_RESET_DRAW:
        /* The separate UI actor/draw pool is owned by status_runtime;
         * clearing it does not destroy the paused world actor pool.
         * E0C0 also clears it AFTER restoring the world projection, so
         * its lifetime must not be gated by the UI camera context. */
        return 1;
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

static int battery_finished(void *context, EmPanel *panel)
{
    (void)context;
    if (panel != &world.panel.owner) return 0;
    world.frame.selector = 3;
    return 1;
}

static EmStatusRuntimeHooks native_status_hooks(void)
{
    return (EmStatusRuntimeHooks){.read_inventory = read_inventory,
        .write_charge = write_charge, .write_battery_capacity = write_capacity,
        .frame_event = status_frame_event, .page_event = status_page_event,
        .sound = sound, .owner_available = owner_available,
        .battery_finished = battery_finished};
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

static void elevator_update_actor(void *context)
{
    (void)context;
    /* Publication is performed by the scene walker after this owner
     * callback. The original generic update cannot dispatch a second tick. */
}

void em_area11_interaction_host_camera_fields(void)
{
    g.cam.sub_state = world.frame.camera_phase;
    g.cam.top_mode = world.frame.camera_top;
    g.cam.mode = world.frame.camera_mode;
}

static void begin_owner(void)
{
    world.frame.camera_phase = g.cam.sub_state;
    world.frame.camera_top = g.cam.top_mode;
    world.frame.camera_mode = g.cam.mode;
    if (world.message) world.frame.message_phase = (int32_t)world.message->phase;
}

static void finish_owner(void)
{
    em_area11_interaction_host_camera_fields();
    if (world.message) world.message->phase = (uint32_t)world.frame.message_phase;
}

int em_area11_interaction_host_load(const char *directory,
    const EmItemMath *math, const EmStatusRuntimeHooks *status_hooks)
{
    if (!directory || ((!math) != (!status_hooks)) || world.loaded ||
        !g.grate_present || !g.elev_has_mesh || !g.coll.blob ||
        g.model.bone_count != 22) return 0;
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
        !em_interaction_runtime_set_pose_worker(&world.shared, pose)) goto failed;
    EmPanelRuntimeHooks panel = {NULL, align_panel, message_start, message_done,
                                  battery_open, sound, power, stop_indicator};
    snprintf(path, sizeof path, "%s/panel/scripts.emsc", directory);
    if (!em_panel_runtime_load(&world.panel, path, g.terminal_powered, &world.shared, &panel))
        goto failed;
    EmElevatorRuntimeHooks elevator = {NULL, align_player, face_player, camera_set,
        camera_publish, camera_chase, message_start, message_done, elevator_sound,
        elevator_rebuild, elevator_copy_child, elevator_update_actor};
    snprintf(path, sizeof path, "%s/elevator.emsc", directory);
    if (!em_elevator_runtime_load(&world.elevator, path, 0, &world.shared,
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
    world.panel_class = world.panel_record->class_flags;
    world.elevator_class = world.elevator_record->class_flags;
    world.elevator_status = world.elevator_record->initial_status;
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
    em_status_runtime_free(world.status);
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
const EmInteractionProjection *em_area11_interaction_host_projection(void)
{ return world.loaded ? &world.projection : NULL; }
int em_area11_interaction_host_failed(void) { return world.failed; }

int em_area11_interaction_host_player(void *unused)
{
    (void)unused;
    if (!world.loaded || world.failed) return -1;
    int result = em_interaction_runtime_player_tick(&world.shared,
        em_status_runtime_ordinary_enabled(world.status));
    return result < 0 ? fail("player worker") : result;
}

int em_area11_interaction_host_panel_tick(void)
{
    if (!world.loaded || world.failed) return -1;
    begin_owner();
    int result = em_panel_runtime_tick(&world.panel, em_pickup_item_count(0x1B) != 0,
                                        em_status_runtime_ordinary_enabled(world.status));
    finish_owner();
    return result < 0 ? fail("panel worker") : result;
}

int em_area11_interaction_host_elevator_tick(void)
{
    if (!world.loaded || world.failed) return -1;
    begin_owner();
    int result = em_elevator_runtime_tick(&world.elevator, g.terminal_powered,
                                            em_status_runtime_ordinary_enabled(world.status));
    finish_owner();
    world.elevator_record->descriptor[1] = world.elevator.owner.lower ? 190 : 230;
    return result < 0 ? fail("elevator worker") : result;
}

int em_area11_interaction_host_message_tick(int busy155, int busy156)
{
    if (!world.loaded || world.failed) return -1;
    if (!em_status_runtime_ordinary_enabled(world.status) || !world.message) return 0;
    em_panel_message_tick(world.message, busy155, busy156);
    world.frame.message_phase = (int32_t)world.message->phase;
    return 1;
}

void em_area11_interaction_host_message_render(EmGfx *gfx)
{
    if (world.loaded && !world.failed && world.message &&
        em_status_runtime_ordinary_enabled(world.status))
        em_panel_message_render(world.message, gfx);
}
