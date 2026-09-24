#include "game/em_game_internal.h"
#include "game/em_effect_color.h"
#include "game/em_player.h"
#include "game/em_player_foot_stop.h"
#include "game/em_player_record_pose.h"
#include "game/em_pose_math.h"

#include <math.h>

/* The player's source pose lives in the player's own record and is worked by
 * the original routines (em_player_record_pose over em_pose_host_workers):
 * one clip clock, one set of node channels, one skeleton. This host keeps
 * only the port's own bookkeeping around it (the idle/walk callbacks'
 * state, the legacy holds, the takeover's acquired/script flags) and the
 * cinematic special bank, which is not on the record yet. */
static struct {
    EmPlayerRecordPose record;
    int valid;              /* the record holds an original pose */
    int acquired;           /* 00174A50 + 00182D70: the takeover owns the player */
    int script_active;
    unsigned script_clip;
    unsigned flags;         /* the last advance result (the value 0015BA50 stores to +200) */
    /* 00183090's special-bank path (+2F3) over a decoded foreign bank. The
     * record keeps the default bank; this pose owns the channels while
     * cinematic_mode == 2 (docs/PLAYER_CLIPS.md section 6). */
    EmPlayerPose cinematic;
    const EmPoseBank *cinematic_bank;
    unsigned cinematic_clip;
    uint8_t cinematic_mode; /* original player+2F3: pending1, active2 */
    float cinematic_rate;
    int started;
    int idle_phase;
    int idle_fidget;
    int idle_count;
    int idle_handled;
    unsigned idle_return;
    EmPlayerFootStop foot_stop;
    int previous_foot;
    int foot_display;
    int previous_entry;
    unsigned previous_stop;
    unsigned previous_reentry;
    float hip[3];
    int hip_valid;
    float saved_euler[3];
    int saved_euler_valid;
    int (*stage_hook)(void *);
    void *stage_context;
    int (*use_hook)(void *);
    void *use_context;
    /* WP-2/H12: a port stand-in (aim, R2, melee, door, examine, interact,
     * hit, legacy scripted clip, low health) owns the displayed player and
     * has no original source channels. The source is frozen, not destroyed. */
    int legacy;
    /* Last stand-in that registered a hold (debugger aid). It can be stale:
     * diagnostics name legacy_blocker() instead. */
    const char *legacy_owner;
    const char *foot_fail; /* why the last foot-stop begin returned 0 */
} source;

static int cinematic_owned(void)
{
    return source.cinematic_mode == 2;
}

static int ordinary_source(void)
{
    return source.started && source.valid && !source.legacy;
}

/* ---- the record ---------------------------------------------------------- */

/* The playing clip: +2C & 0x7FFF (the transition's target while +2C has
 * 0x8000). */
static unsigned current_clip(void)
{
    return cinematic_owned() ? source.cinematic.playback.clip->id
                             : em_player_record_pose_clip(&source.record);
}

static int in_transition(void)
{
    return cinematic_owned() ? source.cinematic.transition.active
                             : em_player_record_pose_transition(&source.record);
}

/* +3C: the transition clock while +2C has 0x8000, else the clip clock. */
static float current_remaining(void)
{
    if (cinematic_owned())
        return source.cinematic.transition.active ? source.cinematic.transition.remaining
                                                  : source.cinematic.playback.remaining;
    return em_player_record_pose_clock(&source.record);
}

static int clip_frames(unsigned clip, float *frames)
{
    int32_t count;
    if (em_player_record_pose_frames(&source.record, (int)clip, &count) < 0) return 0;
    *frames = (float)count;
    return 1;
}

/* The playing clip's own remaining time: +3C, or, during a transition, the
 * target clip's frames less the hold frame node 0 +8E it will restart at
 * (001C64F0's transition end). */
static float playback_remaining(void)
{
    if (cinematic_owned()) return source.cinematic.playback.remaining;
    if (!in_transition()) return current_remaining();
    float frames;
    if (!clip_frames(current_clip(), &frames)) return 0;
    const uint8_t *node0 = source.record.nodes;
    uint16_t hold = (uint16_t)(node0[0x8E] | node0[0x8F] << 8);
    return pose_sub(frames, (float)(int16_t)hold);
}

static float playback_duration(void)
{
    if (cinematic_owned()) return source.cinematic.playback.clip->duration;
    float frames;
    return clip_frames(current_clip(), &frames) ? frames : 0;
}

/* A clip id the bank's directory holds (001C6120 reads any index; an index
 * past the directory is not a clip). */
static int bank_clip(unsigned clip)
{
    return (clip & 0x7FFFu) < source.record.bank_clips;
}

/* The port's requests onto the original routines: a frame-0 request is
 * 001749A0(p, clip, force, blend) (force 0 keeps an equal +20C); a request
 * with a source frame is 001749F0 anim_clip_arbiter(p, clip, blend, frame).
 * Both leave the same record (+20C = clip, anim_clip_init at the frame). */
static int record_select(unsigned clip, float frame, unsigned blend_ticks, int force)
{
    int result;
    if (!source.valid || blend_ticks > 65535 || !bank_clip(clip) || !isfinite(frame) || frame < 0)
        return 0;
    if (frame == 0)
        return em_player_record_pose_request(&source.record, (int)clip, force ? 1 : 0,
                                             (float)blend_ticks, &result) == 0;
    if (!force) return 0;
    return em_player_record_pose_arbiter(&source.record, (int)clip, (float)blend_ticks, frame,
                                         &result) == 0;
}

/* 001C64F0 by `rate` (split into steps of at most 1 inside). */
static int record_advance(float rate)
{
    if (!source.valid || !isfinite(rate) || rate < 0 || rate > 64) return 0;
    uint32_t flags;
    if (em_player_record_pose_advance(&source.record, rate, &flags) < 0) return 0;
    source.flags = flags;
    return 1;
}

/* The position and heading the evaluation places the skeleton at: the port
 * keeps them in g while its own owners move the player (+B0..+B8, +C4; +BC
 * is 1.0 as 0015BCF0 leaves it before its evaluation). */
static void record_place(void)
{
    EmPlayerLiveActor *a = source.record.actor;
    for (unsigned axis = 0; axis < 3; ++axis) em_live_set_f32(a, 0xB0 + 4 * axis, g.pos[axis]);
    em_live_set_f32(a, 0xC4, g.yaw);
    em_live_set_u32(a, 0xBC, 0x3F800000u);
}

/* The displayed palette: 0015BCF0's animate step over the placed record,
 * then the node world matrices. */
static int record_palette(float *palette)
{
    if (!source.valid || !palette || g.model.bone_count != EM_PLAYER_POSE_PALETTE_BONES) return 0;
    record_place();
    return em_player_record_pose_animate(&source.record) == 0 &&
           em_player_record_pose_palette(&source.record, palette) == 0;
}

/* 001C63E0 on the record, as 00182DF0's nonzero-2F3 branch releases the
 * player: +20C = D_00248A00[+235] (row 0: clip 0) and bone_init_default_2
 * of it. */
static int record_default(void)
{
    if (!em_player_record_pose_ready(&source.record)) return 0;
    em_live_set_u16(source.record.actor, 0x20C, 0);
    if (em_player_record_pose_default(&source.record, 0) < 0) return 0;
    source.valid = 1;
    source.flags = 0;
    return 1;
}

/* ---- hooks ---------------------------------------------------------------- */

void player_pose_set_stage_hook(int (*hook)(void *), void *context)
{
    source.stage_hook = hook;
    source.stage_context = context;
}

void player_use_set_hook(int (*hook)(void *), void *context)
{
    source.use_hook = hook;
    source.use_context = context;
}

int player_use_poll(void)
{
    /* Use is polled only by the 61020/612D0 callbacks; a legacy stand-in
     * replaces those callbacks while it holds the player. */
    if (!source.use_hook || !ordinary_source() || source.idle_return ||
        g.loco_reentry.phase == 1)
        return 0;
    /* 61020 checks Use in case1 after its fade gate, and in entry case2
     * without that gate. Its99/100 return states do not poll.612D0's
     * ordinary walking case1 does poll; re-entry case63 does not. */
    if (!g.loco_mode && !g.loco_entry_ticks &&
        (!source.idle_phase || em_frame_transition()->substate != 0))
        return 0;
    int result = source.use_hook(source.use_context);
    if (result < 0 || result > 1) {
        fprintf(stderr, "player Use worker failed at frame %d\n", g.frame_no);
        em_frame_request_quit();
        return -1;
    }
    return result;
}

/* ---- lifetime -------------------------------------------------------------- */

int player_pose_load(const char *bank_path, const char *row0_path)
{
    em_player_record_pose_free(&source.record);
    source.started = source.valid = source.acquired = source.script_active = 0;
    source.hip_valid = source.saved_euler_valid = 0;
    source.cinematic_bank = NULL;
    source.cinematic_mode = 0;
    source.legacy = 0;
    source.legacy_owner = NULL;
    if (em_player_record_pose_load(&source.record, bank_path, row0_path) == 0) {
        /* 0017B490 / 0017C440's tables (the closure binder's clip lookup and
         * tier speeds read them through the record pose's regions; without
         * them those reads fault). */
        if (!source.record.tables &&
            em_player_record_pose_load_tables(&source.record, PLAYER_LOCO_TABLES_PATH) < 0)
            fprintf(stderr, "player pose: %s is missing (python3 tools/export_player_tables.py)\n",
                    PLAYER_LOCO_TABLES_PATH);
        return 1;
    }
    fprintf(stderr, "player pose: original clip bank unavailable: %s / %s "
            "(python3 tools/export_player_clips.py; python3 tools/export_player_tables.py)\n",
            bank_path, row0_path);
    return 0;
}

/* The rebuild (001AF5C0's wipe, then 0015C420 on the first stage) gives the
 * player record its pose storage and a fresh pose: +40 = the default bank,
 * +20C = D_00248A00[+235] (the wiped +235 is row 0: clip 0), the node
 * records (001AF780) with anim_bone_array_setup, bone_init_default_2(+20C)
 * and 001C68C0. A source the port had started (a room rebuild) continues
 * from that pose; before the first opening release it stays unstarted (the
 * opening runtime owns the player until 00182DF0 releases it). A stand-in's
 * hold (the door sequence) is kept: its release re-seeds as before. */
int player_pose_attach(EmPlayerLiveActor *actor, uint8_t *d8106F3, EmPlayerStageScene *scene,
                       struct EmPlayerStageGlobals *globals)
{
    int was_started = source.started && source.valid;
    source.started = source.valid = source.acquired = source.script_active = 0;
    source.cinematic_bank = NULL;
    source.cinematic_mode = 0;
    if (em_player_record_pose_attach(&source.record, actor, d8106F3, scene, globals) < 0 ||
        !record_default() || em_player_record_pose_skeleton(&source.record) < 0) {
        source.valid = 0;
        source.legacy = 0;
        source.legacy_owner = NULL;
        return 0;
    }
    source.started = was_started;
    if (!was_started) {
        source.legacy = 0;
        source.legacy_owner = NULL;
        source.hip_valid = source.saved_euler_valid = 0;
    }
    return 1;
}

int player_pose_record_ready(void)
{
    return em_player_record_pose_ready(&source.record);
}

EmPoseHost *player_pose_record_host(void)
{
    return em_player_record_pose_host(&source.record);
}

void player_pose_unload(void)
{
    em_player_record_pose_free(&source.record);
    memset(&source, 0, sizeof source);
}

/* Genuine native failures only (a failed advance, an unknown clip, a
 * corrupt callback state). Port stand-ins use player_pose_legacy_hold. */
void player_pose_invalidate(const char *reason)
{
    if (!source.started || !source.valid) return;
    source.valid = 0;
    source.hip_valid = 0;
    fprintf(stderr, "player pose: source unavailable at frame %d: %s\n", g.frame_no, reason);
}

static int source_palette(float *palette)
{
    if (cinematic_owned())
        return em_player_pose_palette(&source.cinematic, palette, g.model.bone_count);
    return record_palette(palette);
}

static int publish_current(void)
{
    float palette[22 * 16];
    return source_palette(palette) && player_pose_publish(palette);
}

/* Host mirror of 00182DF0's tail (+4=1, +5=0, +6=0, +1F0=0): the next
 * ordinary callback is idle state0 with a fresh counter. */
static void reset_default_state(void)
{
    g.loco_mode = g.loco_substate = g.loco_tier = 0;
    g.loco_entry_ticks = 0;
    g.loco_stop.phase = g.loco_reentry.phase = 0;
    g.loco_upt = g.move_speed = 0;
    g.loco_rate = 1;
    g.walk_w = g.fid_w = 0;
    g.idle_phase = 0;
    g.idle_timer = 300;
    g.idle_t = (playback_duration() - playback_remaining()) / 60.0;
    source.idle_phase = source.idle_fidget = 0;
    source.idle_return = 0;
    source.foot_stop.active = source.foot_display = 0;
    source.idle_count = 300;
}

void player_pose_legacy_hold(const char *owner)
{
    if (!source.started || !source.valid || source.acquired) return;
    /* The stand-in owns display; nothing advances or requests the source
     * until its release re-seeds it. No original state is invented here. */
    source.legacy = 1;
    source.legacy_owner = owner;
    source.foot_stop.active = source.foot_display = 0;
}

/* Native unsupported path (not an original stand-in): recover through the
 * same hold/re-seed, but report the failure once so it stays visible. The
 * snap to the row default on release is a host adaptation; 0017C030 mode 3
 * runs the 0017B910 solve without a failure case. The remaining native
 * refusals are invalid palette/solve inputs, a failed tier-2 clip 4 select
 * and a clock below 1 (em_player_foot_stop_begin refuses it, although
 * 0017B910 clamps the tier-1 duration to 1 and uses 10 for tier 2); an
 * active pose blend is no longer one. */
void player_pose_unsupported_hold(const char *reason)
{
    if (ordinary_source() && !source.acquired)
        fprintf(stderr, "player pose: unsupported path at frame %d: %s%s%s "
                "(source re-seeds to the row default)\n", g.frame_no, reason,
                source.foot_fail ? ": " : "", source.foot_fail ? source.foot_fail : "");
    source.foot_fail = NULL;
    player_pose_legacy_hold(reason);
}

static int legacy_reseed(void)
{
    /* 00182DF0's nonzero-2F3 branch is the only release path that does not
     * read the previous channels: bone_init_default_2(D_00248A00[+235])
     * initializes the row default clip at frame0 with no blend. The legacy
     * stand-ins have no original channels to blend from, so they use it.
     * APPROXIMATION: the original row is the whole +235 byte; here it comes
     * from live health only (row0 clip0 healthy; row1 clip0x0A at low
     * health). Bit1 of +235 (set by 001756E0/00161790/00162190/00162A40 ->
     * rows2/3, clips0x4B/0x55) is not modelled, and bit0 is a latch (set
     * when 0021C350 damage or 0015D100 decay takes +220 to <=35, cleared by
     * 0015C700 above35), not a live comparison. */
    if (g.status.health <= PD_LOW_HEALTH) return 0;
    if (!record_default()) {
        player_pose_invalidate("default channel re-seed failed");
        return 0;
    }
    source.legacy = 0;
    source.legacy_owner = NULL;
    source.cinematic_bank = NULL;
    source.cinematic_mode = 0;
    reset_default_state();
    return 1;
}

/* The busy state that keeps a legacy hold from releasing right now, or
 * NULL. These holds are rechecked live: the hit, sa_* and low-health
 * owners do not re-register while an earlier hold (e.g. aim) is active,
 * so source.legacy_owner can name a stand-in that has already ended. */
static const char *legacy_blocker(void)
{
    if (g.pd_state == 2) return "damage animation worker is not bound";
    if (g.sa_req || g.sa_cur)
        return "legacy scripted animation has no raw channel worker";
    if (g.status.health <= PD_LOW_HEALTH) return "low-health pose row is not ported";
    return NULL;
}

int player_pose_legacy_release(void)
{
    if (!source.started || !source.valid) return 0;
    if (!source.legacy) return 1;
    /* Stand-ins checked by the host itself: the hit machine, the legacy
     * scripted-clip mailbox and the low-health row keep holding. */
    if (g.pd_state == 2 || g.sa_req || g.sa_cur) return 0;
    return legacy_reseed();
}

int player_pose_opening_release(void)
{
    /* Original external-bank release 182DF0 ->1C63E0; immutable state03
     * has idle0 remaining80 before the next ordinary player callback. */
    if (!record_default()) return 0;
    source.cinematic_bank = NULL;
    source.cinematic_mode = 0;
    source.acquired = source.script_active = 0;
    source.legacy = 0;
    source.legacy_owner = NULL;
    source.started = 1;
    source.idle_phase = source.idle_fidget = 0;
    source.idle_count = 300;
    source.idle_return = 0;
    source.foot_stop.active = source.foot_display = 0;
    g.loco_rate = 1;
    g.idle_t = 0;
    return publish_current();
}

int player_pose_owned(void)
{
    return source.started && source.acquired;
}

int player_pose_source(unsigned *clip, float *remaining, unsigned *flags, int *transition)
{
    if (!ordinary_source()) return 0;
    if (clip) *clip = current_clip();
    if (remaining) *remaining = current_remaining();
    if (flags) *flags = source.flags;
    if (transition) *transition = in_transition();
    return 1;
}

int player_pose_stage_advance(float step, uint32_t *flags)
{
    if (flags) *flags = 0;
    source.idle_handled = 0;
    source.previous_entry = g.loco_entry_ticks;
    source.previous_stop = g.loco_stop.phase;
    source.previous_reentry = g.loco_reentry.phase;
    source.previous_foot = source.foot_stop.active;
    if (source.started && !source.acquired && source.valid) {
        /* 0015BA50 advances the record before every +4 = 1 / 2 / 5 state
         * callback. A stage whose (+4, +5) is not the port's idle/walk
         * belongs to a translated state callback: its clip advances
         * whatever the port's own stand-ins hold (they own only the port's
         * callbacks). */
        const uint8_t *r = source.record.actor->bytes;
        int translated = r[4] != 1 || (r[5] != 0 && r[5] != 1);
        if (translated) {
            if (!record_advance(step))
                player_pose_invalidate("original animation advance failed");
            else if (flags)
                *flags = source.flags;
            return 0;
        }
        if (g.pd_state == 2)
            player_pose_legacy_hold("damage animation worker is not bound");
        if (!source.legacy) {
            /* 0015BA50 consumes the prior multiplier before its state
             * callback resets it. Idle also advances while speed is zero. */
            if (!record_advance(step))
                player_pose_invalidate("original animation advance failed");
            else if (flags)
                *flags = source.flags;
        }
    }
    return 0;
}

int player_pose_stage(void)
{
    (void)player_pose_stage_advance(g.loco_rate, NULL);
    return player_pose_stage_hook();
}

int player_pose_stage_hook(void)
{
    int consumed = source.stage_hook ? source.stage_hook(source.stage_context) : 0;
    if (consumed < 0 || consumed > 1 || (player_pose_owned() && !consumed)) {
        fprintf(stderr, "player pose: shared player-stage worker failed at frame %d\n", g.frame_no);
        em_frame_request_quit();
        return -1;
    }
    return consumed;
}

int player_pose_animate(void)
{
    /* 0015BCF0 evaluates the record after every player stage. Before the
     * first pose (the opening release) there is nothing to evaluate, and the
     * special bank does not live on the record yet. */
    if (!source.started || !source.valid || cinematic_owned()) return 0;
    return em_player_record_pose_animate(&source.record) < 0 ? -1 : 0;
}

int player_pose_display(void)
{
    if (!source.started || !source.valid || cinematic_owned() ||
        g.model.bone_count != EM_PLAYER_POSE_PALETTE_BONES)
        return 0;
    float palette[22 * 16];
    if (em_player_record_pose_palette(&source.record, palette) < 0) return -1;
    return player_pose_publish(palette) ? 1 : -1;
}

void player_pose_request(unsigned clip, float frame, unsigned blend, int force)
{
    if (!ordinary_source()) return;
    if (!record_select(clip, frame, blend, force))
        player_pose_invalidate("unsupported ordinary clip request");
    if (clip != 0 && clip != 0x15D) {
        source.idle_phase = source.idle_fidget = 0;
        source.idle_count = 300;
    }
}

void player_pose_idle_enter(void)
{
    if (!ordinary_source()) return;
    /* 00161020 case0 performs its default request and seeds the counter;
     * that callback does not also execute the case1 countdown. */
    player_pose_request(0, 0, 12, 0);
    source.idle_phase = 1;
    source.idle_fidget = 0;
    source.idle_count = 300;
    source.idle_handled = 1;
}

void player_pose_entry_cancel(void)
{
    if (!ordinary_source()) return;
    /* 00161020 case2 changes only the state to99 on released input. Its
     * default request belongs to the following callback, not this one. */
    source.idle_return = 0x63;
    source.idle_handled = 1;
}

int player_pose_entry_return_tick(void)
{
    if (!ordinary_source() || !source.idle_return) return 0;
    source.idle_handled = 1;
    if (source.idle_return == 0x63) {
        player_pose_request(0, 0, 8, 0);
        source.idle_return = 0x64;
    } else if (!(source.flags & 0x8000)) {
        source.idle_return = 0;
        source.idle_phase = 1;
        source.idle_fidget = 0;
        source.idle_count = 300;
        g.idle_phase = 0;
        g.idle_timer = 300;
    }
    /* Both99 and100 ignore movement. The callback clearing100 restores
     * case1 but does not also dispatch it or decrement its fresh counter. */
    return 1;
}

int player_pose_foot_stop_begin(void)
{
    source.foot_fail = "source not ordinary";
    if (!ordinary_source() || source.acquired) return 0;
    source.foot_fail = "tier is not 1 or 2";
    if (g.loco_tier != 1 && g.loco_tier != 2) return 0;
    float euler[3] = {0, g.yaw, 0};
    /* 0017B910 first evaluates the source skeleton (anim_eval_skeleton on
     * the record at its +B0 / +C0), then reads its two endpoint nodes 17/18
     * (*(D_00275B40) + 0x44 / + 0x48: the record's +110 array) at +C0. The
     * clock is the record's +3C, the transition clock during a blend, as the
     * live source trace (tools/test_player_pose_live_reference.py) records
     * through the stop blends. */
    source.foot_fail = "source skeleton evaluation failed";
    record_place();
    if (em_player_record_pose_eval_skeleton(&source.record) < 0) return 0;
    float feet[2][3];
    for (unsigned k = 0; k < 2; ++k) {
        memcpy(feet[k], source.record.nodes + EM_POSE_NODE_BYTES * (17 + k) + 0xC0, sizeof feet[k]);
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!isfinite(feet[k][axis])) return 0;
    }
    float clock = current_remaining();
    source.foot_fail = "foot-placement solve rejected the source";
    if (!em_player_foot_stop_begin(&source.foot_stop, g.loco_tier, clock,
            feet[0], feet[1], g.pos, euler))
        return 0;
    source.foot_fail = "tier-2 stop clip select failed";
    if (g.loco_tier == 2 && !record_select(4, 0, 10, 0)) {
        source.foot_stop.active = 0;
        return 0;
    }
    source.foot_fail = NULL;
    source.foot_display = 1;
    g.loco_mode = 5;
    g.loco_upt = g.move_speed = 0;
    g.loco_animation_step = 0;
    return 1;
}

int player_pose_foot_stop_active(void)
{
    return ordinary_source() && source.foot_stop.active;
}

int player_pose_foot_stop_tick(void)
{
    if (!player_pose_foot_stop_active()) return -1;
    int active = em_player_foot_stop_tick(&source.foot_stop, source.flags,
                                         g.pos, &g.loco_rate);
    if (active < 0) return -1;
    if (!active) {
        /* 612D0 sees mode0 and returns to idle state0. The following
         * callback executes 61020 case0, requesting its blend12. */
        g.loco_mode = g.loco_substate = g.loco_tier = 0;
        g.loco_stop.phase = 3;
    }
    g.loco_upt = g.move_speed = 0;
    g.loco_animation_step = 0;
    return active;
}

int player_pose_foot_stop_palette(void)
{
    if (!source.foot_display) return 0;
    if (!source.foot_stop.active && !g.loco_stop.phase) {
        source.foot_display = 0;
        return 0;
    }
    if (!publish_current()) {
        player_pose_invalidate("foot-stop source palette failed");
        return -1;
    }
    return 1;
}

int player_pose_idle_state_wait(void)
{
    return ordinary_source() && !g.loco_mode && !g.loco_entry_ticks &&
           !g.loco_stop.phase && (!source.idle_phase || em_frame_transition()->substate != 0);
}

/* 0017B660's source-state side effects after the ordinary locomotion
 * callback: at a tier boundary the source clip becomes the tier's clip at
 * the same normalized frame (anim_clip_arbiter with no blend); while a blend
 * toward the adjacent tier is below one, the base clip is restored at its
 * source frame. The display-tier blend itself is separate. */
static int gait_base(unsigned tier, unsigned substate, float blend)
{
    if (!source.valid || in_transition() || source.acquired || tier > 3 || substate > 2 ||
        !isfinite(blend) || blend < 0 || blend > 1)
        return 0;
    unsigned original_clip = current_clip();
    float old_length = playback_duration();
    float from = pose_sub(old_length, current_remaining());
    if (substate == 0 && original_clip == tier) return 1;
    int adjacent = substate == 1 ? (int)tier + 1 : (int)tier - 1;
    unsigned target = substate == 0 ? tier : (unsigned)adjacent;
    float next_length;
    if (target > 3 || !clip_frames(target, &next_length)) return 0;
    float target_frame = pose_mul(next_length, pose_div(from, old_length));
    if (!record_select(target, target_frame, 0, 1)) return 0;
    if (substate != 0 && blend < 1) return record_select(original_clip, from, 0, 1);
    return 1;
}

void player_pose_finish_state(void)
{
    if (!ordinary_source() || source.acquired || source.idle_return ||
        source.foot_stop.active || source.previous_foot)
        return;
    if (g.status.health <= PD_LOW_HEALTH) {
        player_pose_legacy_hold("low-health pose row is not ported");
        return;
    }
    if (g.sa_req || g.sa_cur) {
        player_pose_legacy_hold("legacy scripted animation has no raw channel worker");
        return;
    }
    if (source.previous_entry || source.previous_reentry == 1 ||
        (source.previous_stop && source.previous_stop != 4) || g.loco_entry_ticks ||
        (g.loco_stop.phase && g.loco_stop.phase != 4) || g.loco_reentry.phase == 1)
        return;

    if (g.loco_mode == 1 && g.loco_tier) {
        if (!gait_base(g.loco_tier, g.loco_substate, g.loco_blend))
            player_pose_invalidate("ordinary gait source restoration failed");
    } else if (!g.loco_mode && !g.gait && !source.idle_handled) {
        if (!source.idle_phase) {
            player_pose_idle_enter();
        } else if (em_frame_transition()->substate != 0) {
            /* 00161020 case1 gates its state work on D0028A9A0, while
             *0015BA50 still advances the existing animation beforehand. */
            return;
        } else if (source.idle_fidget) {
            if (source.flags & 0x1000) {
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

/* A palette the source evaluated in world space (the record's node world
 * matrices, or the special bank's channels, which already hold the world
 * placement): the display takes it as it is. */
int player_pose_publish(const float *palette)
{
    if (!palette || g.model.bone_count != 22 || !source.valid) return 0;
    for (unsigned i = 0; i < 22 * 16; ++i)
        if (!isfinite(palette[i])) return 0;
    memcpy(g.player_palette, palette, 22 * 16 * sizeof *palette);
    memcpy(source.hip, g.player_palette + 16 + 12, sizeof source.hip);
    source.hip_valid = 1;
    return 1;
}

int player_pose_hip(float out[3])
{
    if (!out || !source.valid || !source.hip_valid) return 0;
    memcpy(out, source.hip, sizeof source.hip);
    return 1;
}

void player_pose_finish_palette(void)
{
    if (!source.started || !source.valid || g.model.bone_count != 22) return;
    /* 0015BCF0 tail publishes bone1 position and actor Euler to the shared
     * scratch vectors after the player callback, including script-owned ticks. */
    memcpy(source.hip, g.player_palette + 16 + 12, sizeof source.hip);
    source.hip_valid = 1;
    source.saved_euler[0] = source.saved_euler[2] = 0;
    source.saved_euler[1] = g.yaw;
    source.saved_euler_valid = 1;
}

int player_pose_script_euler(float out[3])
{
    if (!out || !source.valid || !source.saved_euler_valid) return 0;
    memcpy(out, source.saved_euler, sizeof source.saved_euler);
    return 1;
}

int player_pose_align(const float position[3])
{
    if (!position || !source.started || !source.valid || !source.hip_valid ||
        g.model.bone_count != 22) return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(position[axis])) return 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        /* 182F90 forms target-A0, then adds that delta to feet, hip and
         * scratch hip. Keep the native displayed cache coherent as a host
         * adaptation; the original routine itself does not rewrite matrices. */
        float delta = em_effect_float32((double)position[axis] - g.pos[axis]);
        g.pos[axis] = em_effect_float32((double)g.pos[axis] + delta);
        source.hip[axis] = em_effect_float32((double)source.hip[axis] + delta);
        for (unsigned bone = 0; bone < 22; ++bone)
            g.player_palette[bone * 16 + 12 + axis] = em_effect_float32(
                (double)g.player_palette[bone * 16 + 12 + axis] + delta);
    }
    /* A preceding face command may already rotate the displayed cache.
     *182F90 shifts the existing B0/3B40 values; only the player tail reads
     * node1 again. It does copy the current actor Euler to 3B50 here. */
    source.saved_euler[0] = source.saved_euler[2] = 0;
    source.saved_euler[1] = g.yaw;
    source.saved_euler_valid = 1;
    return 1;
}

int player_pose_face(float yaw)
{
    if (!source.started || !source.valid || !isfinite(yaw) || g.model.bone_count != 22)
        return 0;
    float old_yaw = g.yaw;
    g.yaw = yaw;
    if (source.acquired) {
        float hip[3];
        int hip_valid = source.hip_valid;
        memcpy(hip, source.hip, sizeof hip);
        int result = publish_current();
        /* 001B9C10/sub8 changes C4 and its dirty marker only. Keep the
         * displayed orientation current without prematurely changing the
         * original B0/3B40 inputs used by a following camera command. */
        memcpy(source.hip, hip, sizeof hip);
        source.hip_valid = hip_valid;
        return result;
    }

    /* 001B9C10 sub8 writes only live C4 and its dirty marker. Before actual
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
    /* Hip and saved 3B50 remain unchanged until 182F90 or the player tail. */
    return 1;
}

int player_pose_acquire(void)
{
    /* Host adaptation: a source frozen by a player-driven stand-in (aim, R2,
     * melee, door, examine, interact) is re-seeded before acquisition
     * (00182DF0 row default) because it has no channels to blend from.
     * 00182B30's refusal set (+220<=0, +25F, 0021BB00, D_008106F1,
     * +1F0 0x3C/0x3D) is NOT modelled. The holds kept for busy original
     * state (hit machine pd_state==2, sa_req/sa_cur, low-health row) are
     * refused here exactly as player_pose_legacy_release refuses them, so
     * acquisition fails explicitly below (0015B610 acquires a hit only for
     * subs3/1 after 00182B30 admits it; that path is not modelled). */
    if (source.legacy && source.started && source.valid && !source.acquired)
        (void)player_pose_legacy_release();
    /* 00174A50 passes flags 0: an already-idle +20C keeps its cursor. */
    if (!ordinary_source() || source.acquired || !record_select(0, 0, 8, 0)) {
        /* Name what actually refused: a still-held source reports the live
         * blocker, not the (possibly stale) owner that first froze it. */
        const char *why = !source.started ? "source not started"
                        : !source.valid ? "source invalidated"
                        : source.acquired ? "source already acquired"
                        : !source.legacy ? "original acquire request failed"
                        : legacy_blocker() ? legacy_blocker()
                        : "held source did not re-seed";
        fprintf(stderr, "player pose: cannot acquire the original source: %s%s\n",
                source.started && source.valid && source.legacy ? "held: " : "", why);
        return -1;
    }
    source.acquired = 1;
    source.script_active = 0;
    /* 182D70 publishes readiness immediately. It does not advance the newly
     * initialized default clip or continue the ordinary movement callback. */
    g.gait = 0;
    g.move_speed = 0;
    return publish_current() ? 1 : -1;
}

int player_pose_use_accepted_port(void)
{
    if (!ordinary_source() || source.acquired) return 0;
    /* 00160220 took the press over the live record: 001798D0 (on a scan
     * winner) or the chosen action already requested the record's clip
     * (00174A50) and wrote its state. This is the port's side: the idle /
     * walk callbacks' own locomotion state and the source's idle bookkeeping
     * leave the ordinary walk, and the record's current pose is shown. */
    g.loco_upt = g.move_speed = 0;
    g.loco_tier = g.loco_mode = g.loco_substate = 0;
    g.loco_entry_ticks = 0;
    g.loco_stop.phase = g.loco_reentry.phase = 0;
    g.gait = 0;
    g.walk_w = g.fid_w = 0;
    g.idle_t = (playback_duration() - playback_remaining()) / 60.0;
    source.idle_phase = source.idle_fidget = 0;
    source.idle_return = 0;
    source.foot_stop.active = source.foot_display = 0;
    source.idle_handled = 1;
    return publish_current();
}

int player_pose_idle_tick(float *local_palette)
{
    if (source.cinematic_mode)
        return player_pose_cinematic_tick(local_palette, 0);
    if (!source.acquired || source.script_active || current_clip() != 0 || !record_advance(1))
        return -1;
    return record_palette(local_palette) ? 1 : -1;
}

int player_pose_cinematic_request(const EmPoseBank *bank, unsigned clip, float rate)
{
    if (!source.started || !source.valid || !source.acquired || !bank ||
        bank->bone_count != 21 || !bank->clips || !isfinite(rate) || rate < 0 || rate > 4 ||
        clip > 32767 || source.script_active)
        return 0;
    int32_t parents[EM_PLAYER_POSE_NODES];
    if (em_player_record_pose_parents(&source.record, 0, parents) < 0) return 0;
    for (unsigned i = 0; i < 21; ++i)
        if (bank->parents[i] != parents[i]) return 0;
    unsigned index;
    for (index = 0; index < bank->clip_count && bank->clips[index].id != clip; ++index) {}
    if (index == bank->clip_count) return 0;
    /* 001B9A00/sub1 publishes the request, mode1/rate and clears the last
     * animation result. It does not initialize or advance any source channels. */
    source.cinematic_bank = bank;
    source.cinematic_clip = clip;
    source.cinematic_rate = rate;
    source.cinematic_mode = 1;
    source.flags = 0;
    return 1;
}

int player_pose_cinematic_tick(float *local_palette, int freeze_motion)
{
    if (!source.started || !source.valid || !source.acquired ||
        !source.cinematic_bank || !source.cinematic_mode || !local_palette)
        return -1;
    if (source.cinematic_mode == 1) {
        EmPlayerPose next;
        if (!em_player_pose_init(&next, source.cinematic_bank, source.cinematic_clip, 0))
            return -1;
        next.acquired = 1;
        source.cinematic = next;
        source.cinematic_mode = 2;
        source.flags = 0;
    }
    /* 83090 returns1 after the special-bank initializer, so 5BA50 advances
     * by the requested rate on this SAME callback. Ordinary changed requests
     * return0 and do not advance; that separate path remains unchanged. */
    if (!em_player_pose_advance(&source.cinematic, source.cinematic_rate, freeze_motion))
        return -1;
    source.flags = source.cinematic.flags;
    return em_player_pose_palette(&source.cinematic, local_palette, g.model.bone_count) ? 1 : -1;
}

int player_pose_cinematic_active(void)
{
    return source.started && source.valid && source.cinematic_mode != 0;
}

/* The interaction runtime's per-tick check over the record: its temporal
 * state must be the original source's, and its baked palette is replaced
 * with the source's evaluated one. Call after EVERY scripted animation
 * tick, including the blend1 callback that preserves the palette. */
int player_pose_script_tick(const EmInteractionAnimation *animation, int result,
                            float *local_palette)
{
    if (source.cinematic_mode) return 0;
    if (!source.acquired || !animation || !animation->active || (result != 0 && result != 1) ||
        (animation->current_clip != 0x45 && animation->current_clip != 0x47 &&
         animation->current_clip != 0x15C &&
         (animation->current_clip < 0x40 || animation->current_clip > 0x43)))
        return 0;

    if (!source.script_active || source.script_clip != animation->current_clip) {
        if (animation->frame != 0 ||
            !record_select(animation->current_clip, 0, animation->transition ? 1 : 0, 1))
            return 0;
        source.script_active = 1;
        source.script_clip = animation->current_clip;
        /*00183090 clears the published advance result after initialization;
         * ordinary749A0/749F0 selection does not own that actor field. */
        source.flags = 0;
    } else if (!record_advance(1)) {
        return 0;
    }

    /* Do not conceal a missed callback by seeking to a baked frame index. */
    if (current_remaining() != animation->remaining || source.flags != animation->flags ||
        !!in_transition() != !!animation->transition)
        return 0;
    if (!result) return 1;
    return record_palette(local_palette);
}

/* 00182DF0 on the record: +20C against the row default (0017B490 with the
 * healthy row gives clip 0: the approximation legacy_reseed names); a
 * negative +20C or a zero D_00248C90 +0 halfword requests 00174AB0 (clip 0,
 * flags 1, no blend) first, then 00174A50(16.0) (clip 0, flags 0). */
static int record_release(void)
{
    if (!source.valid || !source.acquired) return 0;
    int16_t current = em_player_record_pose_requested(&source.record);
    if (current != 0) {
        int16_t row = 0;
        if (current >= 0 && em_player_record_pose_row0(&source.record, current, &row) < 0) return 0;
        if ((current < 0 || row == 0) && !record_select(0, 0, 0, 1)) return 0;
        if (!record_select(0, 0, 16, 0)) return 0;
    }
    source.acquired = 0;
    source.script_active = 0;
    return 1;
}

int player_pose_release(void)
{
    if (source.cinematic_mode) {
        /* 182DF0's nonzero2F3 branch restores the default bank and initializes
         * healthy row0 before releasing. It does not blend foreign channels
         * into an ordinary clip with the same numeric ID. */
        if (!source.acquired || !record_default()) return 0;
        source.cinematic_bank = NULL;
        source.cinematic_mode = 0;
        source.acquired = source.script_active = 0;
    } else if (!record_release()) return 0;
    reset_default_state();
    /* Release follows the consumed script/idle callback. Publish its default
     * reset now, then let the next ordinary callback advance it exactly once. */
    return publish_current();
}
