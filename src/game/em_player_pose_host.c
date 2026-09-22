#include "game/em_game_internal.h"

#include <math.h>

static struct {
    EmPoseBank bank;
    EmPlayerPose pose;
    int started;
    int idle_phase;
    int idle_fidget;
    int idle_count;
    int idle_handled;
    int previous_entry;
    unsigned previous_stop;
    unsigned previous_reentry;
    float hip[3];
    int hip_valid;
    float saved_euler[3];
    int saved_euler_valid;
    int (*stage_hook)(void *);
    void *stage_context;
} source;

void player_pose_set_stage_hook(int (*hook)(void *), void *context)
{
    source.stage_hook = hook;
    source.stage_context = context;
}

int player_pose_load(const char *path)
{
    source.started = source.hip_valid = source.saved_euler_valid = 0;
    memset(&source.pose, 0, sizeof source.pose);
    if (em_pose_bank_load(&source.bank, path)) return 1;
    fprintf(stderr, "player pose: original channel bank unavailable: %s\n", path);
    return 0;
}

void player_pose_unload(void)
{
    em_pose_bank_free(&source.bank);
    memset(&source, 0, sizeof source);
}

void player_pose_invalidate(const char *reason)
{
    if (!source.started || !source.pose.valid) return;
    source.pose.valid = 0;
    source.hip_valid = 0;
    fprintf(stderr, "player pose: source unavailable at frame %d: %s\n", g.frame_no, reason);
}

static int publish_current(void)
{
    float local[22 * 16];
    return em_player_pose_palette(&source.pose, local, 22) && player_pose_publish(local);
}

int player_pose_opening_release(void)
{
    /* Original external-bank release182DF0 ->1C63E0; immutable state03
     * has idle0 remaining80 before the next ordinary player callback. */
    if (!em_player_pose_init(&source.pose, &source.bank, 0, 0)) return 0;
    source.started = 1;
    source.idle_phase = source.idle_fidget = 0;
    source.idle_count = 300;
    g.idle_t = 0;
    return publish_current();
}

int player_pose_owned(void)
{
    return source.started && source.pose.acquired;
}

int player_pose_source(unsigned *clip, float *remaining, unsigned *flags, int *transition)
{
    if (!source.started || !source.pose.valid) return 0;
    if (clip) *clip = source.pose.playback.clip->id;
    if (remaining)
        *remaining = source.pose.transition.active ? source.pose.transition.remaining
                                                   : source.pose.playback.remaining;
    if (flags) *flags = source.pose.flags;
    if (transition) *transition = source.pose.transition.active;
    return 1;
}

int player_pose_stage(void)
{
    source.idle_handled = 0;
    source.previous_entry = g.loco_entry_ticks;
    source.previous_stop = g.loco_stop.phase;
    source.previous_reentry = g.loco_reentry.phase;
    if (source.started && !source.pose.acquired && source.pose.valid) {
        if (g.pd_state == 2) {
            player_pose_invalidate("damage animation worker is not bound");
        } else {
            /*0015BA50 consumes the prior multiplier before its state
             * callback resets it. Idle also advances while speed is zero. */
            float rate = g.loco_mode ? g.loco_rate : 1;
            if (!em_player_pose_advance(&source.pose, rate, 0))
                player_pose_invalidate("original animation advance failed");
        }
    }
    int consumed = source.stage_hook ? source.stage_hook(source.stage_context) : 0;
    if (consumed < 0 || consumed > 1 || (player_pose_owned() && !consumed)) {
        fprintf(stderr, "player pose: shared player-stage worker failed at frame %d\n", g.frame_no);
        em_frame_request_quit();
        return -1;
    }
    return consumed;
}

void player_pose_request(unsigned clip, float frame, unsigned blend, int force)
{
    if (!source.started || !source.pose.valid) return;
    if (!em_player_pose_select(&source.pose, clip, frame, blend, force))
        player_pose_invalidate("unsupported ordinary clip request");
    if (clip != 0 && clip != 0x15D) {
        source.idle_phase = source.idle_fidget = 0;
        source.idle_count = 300;
    }
}

void player_pose_idle_enter(void)
{
    /*00161020 case0 performs its default request and seeds the counter;
     * that callback does not also execute the case1 countdown. */
    player_pose_request(0, 0, 12, 0);
    source.idle_phase = 1;
    source.idle_fidget = 0;
    source.idle_count = 300;
    source.idle_handled = 1;
}

int player_pose_idle_state_wait(void)
{
    return source.started && source.pose.valid && !g.loco_mode && !g.loco_entry_ticks &&
           !g.loco_stop.phase && (!source.idle_phase || em_frame_transition()->substate != 0);
}

void player_pose_finish_state(void)
{
    if (!source.started || !source.pose.valid || source.pose.acquired) return;
    if (g.status.health <= PD_LOW_HEALTH) {
        player_pose_invalidate("low-health pose variant is not bound");
        return;
    }
    if (g.sa_req || g.sa_cur) {
        player_pose_invalidate("legacy scripted animation has no raw channel worker");
        return;
    }
    if (source.previous_entry || source.previous_reentry == 1 ||
        (source.previous_stop && source.previous_stop != 4) || g.loco_entry_ticks ||
        (g.loco_stop.phase && g.loco_stop.phase != 4) || g.loco_reentry.phase == 1)
        return;

    if (g.loco_mode == 1 && g.loco_tier) {
        if (!em_player_pose_gait_base(&source.pose, g.loco_tier, g.loco_substate, g.loco_blend))
            player_pose_invalidate("ordinary gait source restoration failed");
    } else if (!g.loco_mode && !g.gait && !source.idle_handled) {
        if (!source.idle_phase) {
            player_pose_idle_enter();
        } else if (em_frame_transition()->substate != 0) {
            /*00161020 case1 gates its state work on D0028A9A0, while
             *0015BA50 still advances the existing animation beforehand. */
            return;
        } else if (source.idle_fidget) {
            if (source.pose.flags & 0x1000) {
                player_pose_request(0, 0, 8, 0);
                source.idle_fidget = 0;
                source.idle_count = 300;
            }
        } else if (g.status.health > PD_LOW_HEALTH) {
            /* The original tests the old counter after writing count-1. */
            if (source.idle_count-- == 0) {
                player_pose_request(0x15D, 0, 8, 1);
                source.idle_fidget = 1;
            }
        }
    }
}

int player_pose_publish(const float *local_palette)
{
    if (!local_palette || g.model.bone_count != 22 || !source.pose.valid) return 0;
    for (unsigned i = 0; i < 22 * 16; ++i)
        if (!isfinite(local_palette[i])) return 0;
    memcpy(g.player_palette, local_palette, 22 * 16 * sizeof *local_palette);
    palette_apply_placement(g.player_palette, 22, g.pos, g.yaw);
    memcpy(source.hip, g.player_palette + 16 + 12, sizeof source.hip);
    source.hip_valid = 1;
    return 1;
}

int player_pose_hip(float out[3])
{
    if (!out || !source.pose.valid || !source.hip_valid) return 0;
    memcpy(out, source.hip, sizeof source.hip);
    return 1;
}

void player_pose_finish_palette(void)
{
    if (!source.started || !source.pose.valid || g.model.bone_count != 22) return;
    /*0015BCF0 tail publishes bone1 position and actor Euler to the shared
     * scratch vectors after the player callback, including script-owned ticks. */
    memcpy(source.hip, g.player_palette + 16 + 12, sizeof source.hip);
    source.hip_valid = 1;
    source.saved_euler[0] = source.saved_euler[2] = 0;
    source.saved_euler[1] = g.yaw;
    source.saved_euler_valid = 1;
}

int player_pose_script_euler(float out[3])
{
    if (!out || !source.pose.valid || !source.saved_euler_valid) return 0;
    memcpy(out, source.saved_euler, sizeof source.saved_euler);
    return 1;
}

int player_pose_align(const float position[3])
{
    if (!position || !source.started || !source.pose.valid || g.model.bone_count != 22) return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(position[axis])) return 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        /*182F90 forms target-A0, then adds that delta to feet, hip and
         * scratch hip. Keep the native displayed cache coherent as a host
         * adaptation; the original routine itself does not rewrite matrices. */
        float delta = position[axis] - g.pos[axis];
        g.pos[axis] += delta;
        for (unsigned bone = 0; bone < 22; ++bone)
            g.player_palette[bone * 16 + 12 + axis] += delta;
    }
    player_pose_finish_palette();
    return 1;
}

int player_pose_face(float yaw)
{
    if (!source.started || !source.pose.valid || !isfinite(yaw) || g.model.bone_count != 22)
        return 0;
    float old_yaw = g.yaw;
    g.yaw = yaw;
    if (source.pose.acquired) return publish_current();

    /*001B9C10 sub8 writes only live C4 and its dirty marker. Before actual
     * acquisition, rotate the displayed host cache without replacing its
     * gait blend. This does not derive or modify any source node channels. */
    float c = cosf(yaw - old_yaw);
    float s = sinf(yaw - old_yaw);
    for (unsigned bone = 0; bone < 22; ++bone) {
        float *matrix = g.player_palette + bone * 16;
        for (unsigned column = 0; column < 4; ++column) {
            float x = matrix[column * 4];
            float z = matrix[column * 4 + 2];
            if (column == 3) {
                x -= g.pos[0];
                z -= g.pos[2];
            }
            matrix[column * 4] = c * x + s * z;
            matrix[column * 4 + 2] = -s * x + c * z;
            if (column == 3) {
                matrix[column * 4] += g.pos[0];
                matrix[column * 4 + 2] += g.pos[2];
            }
        }
    }
    memcpy(source.hip, g.player_palette + 16 + 12, sizeof source.hip);
    source.hip_valid = 1;
    /* Saved3B50 remains unchanged until182F90 or the real player tail. */
    return 1;
}

int player_pose_acquire(void)
{
    if (!source.started || !source.pose.valid || !em_player_pose_acquire(&source.pose)) {
        fprintf(stderr, "player pose: cannot acquire without a valid original source\n");
        return -1;
    }
    /*182D70 publishes readiness immediately. It does not advance the newly
     * initialized default clip or continue the ordinary movement callback. */
    g.gait = 0;
    g.move_speed = 0;
    return publish_current() ? 1 : -1;
}

int player_pose_idle_tick(float *local_palette)
{
    return em_player_pose_idle_tick(&source.pose, local_palette, g.model.bone_count);
}

int player_pose_script_tick(const EmInteractionAnimation *animation, int result,
                            float *local_palette)
{
    return em_player_pose_script_tick(&source.pose, animation, result, local_palette,
                                      g.model.bone_count);
}

int player_pose_release(void)
{
    if (!em_player_pose_release(&source.pose)) return 0;
    g.loco_mode = g.loco_substate = g.loco_tier = 0;
    g.loco_entry_ticks = 0;
    g.loco_stop.phase = g.loco_reentry.phase = 0;
    g.loco_upt = g.move_speed = 0;
    g.loco_rate = 1;
    g.walk_w = g.fid_w = 0;
    g.idle_phase = 0;
    g.idle_timer = 300;
    g.idle_t = (source.pose.playback.clip->duration - source.pose.playback.remaining) / 60.0;
    source.idle_phase = source.idle_fidget = 0;
    source.idle_count = 300;
    /* Release follows the consumed script/idle callback. Publish its default
     * reset now, then let the next ordinary callback advance it exactly once. */
    return publish_current();
}
