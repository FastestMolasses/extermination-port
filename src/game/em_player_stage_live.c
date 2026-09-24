/* em_player_stage_live.c - the live binding of the player stage (census lane
 * L01). See em_player_stage_live.h for what each worker binds to. */
#include "game/em_player_stage_live.h"

#include <stdio.h>
#include <string.h>

#include "game/em_collision_world.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_player.h"
#include "game/em_player_stage_workers.h"
#include "game/em_pose_host_workers.h"
#include "game/em_scene_state.h"
#include "game/em_scene_bindings.h"
#include "game/em_sfx.h"

static struct {
    int rates_loaded;
    EmPlayerClipRates rates;
    EmPlayerStageGlobals globals;
    EmPlayerStageHost host;
    EmPlayerStageMajor4 major4;
    EmPlayerStageFade fade;
    unsigned faults;
    int reported;
} live;

/* ---- fail-stop workers --------------------------------------------------
 * An original callee with no bound translation on the live path. Reaching
 * one is a fault: it is reported once per run (naming the callee), counted,
 * and the stage fails, which em_player.c turns into em_frame_request_quit. */
static int unbound(const char *callee)
{
    ++live.faults;
    if (!live.reported) {
        live.reported = 1;
        fprintf(stderr, "player stage: reached %s, which is not bound on the live path "
                        "(em_player_stage_live.h)\n", callee);
    }
    return -1;
}

static int stub_001D0C70(void *c)
{
    (void)c;
    return unbound("001D0C70 (00183090 under 0x70003B8F == 2)");
}
static int stub_cue(void *c, int a0, int a1, int a2, int a3)
{
    (void)c; (void)a0; (void)a1; (void)a2; (void)a3;
    return unbound("001B61C0 (pad rumble)");
}
static int stub_001EFE00(void *c, uint32_t id, EmPlayerLiveActor *a)
{
    (void)c; (void)id; (void)a;
    return unbound("001EFE00 (effect at the player)");
}
static int stub_001F00A0(void *c, uint32_t id, EmPlayerLiveActor *a, int a3, uint8_t **record)
{
    (void)c; (void)id; (void)a; (void)a3;
    if (record) *record = NULL;
    return unbound("001F00A0 (effect record)");
}
static int stub_001F0060(void *c, uint32_t id, int a1)
{
    (void)c; (void)id; (void)a1;
    return unbound("001F0060 (effect)");
}
/* 0021C440 calls atan2 only after link20 (face_link), which faults first;
 * this worker has no error channel, so it quits directly. */
static float stub_atan2(void *c, float y, float x)
{
    (void)c; (void)y; (void)x;
    (void)unbound("SDK 0011E620 atan2 (0021D6C0 / hit facing)");
    em_frame_request_quit();
    return 0.0f;
}
static int stub_link20(void *c, uint32_t word, uint32_t *c0, uint32_t *c8)
{
    (void)c; (void)word;
    if (c0) *c0 = 0;
    if (c8) *c8 = 0;
    return unbound("the +20 object's +C0/+C8 (the port keeps no +20 handle)");
}
static int stub_clip_lookup(void *c, EmPlayerLiveActor *a, int a1, int a2, int a3, int16_t *clip)
{
    (void)c; (void)a; (void)a1; (void)a2; (void)a3;
    if (clip) *clip = 0;
    return unbound("0017B490 on the record (00174A50 / 0017C370; census L12)");
}
static int stub_0015C9D0(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return unbound("0015C9D0 (0015D100's low-health clip re-trigger; untranslated)");
}
static int stub_link1C(void *c, uint32_t word, uint8_t value)
{
    (void)c; (void)word; (void)value;
    return unbound("the +1C object's +4 (00182D70; the port keeps no +1C object)");
}

/* 0015B530's routines that are not bound. */
static int stub_00182DF0(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return unbound("00182DF0 on the record (0015B530 with 0x70003B8D clear)");
}
static int stub_001837B0(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return unbound("001837B0 (0015B530 +5 = 1; untranslated)");
}
static int stub_00162DB0(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return unbound("00162DB0 (0015B530 +5 = 5; FLOOR, census L02)");
}
static int stub_00163B40(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return unbound("00163B40 (0015B530 +5 = 8; FLOOR, census L02)");
}
static int stub_001838B0(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return unbound("001838B0 (0015B530 +5 = 0xC; untranslated)");
}
static int stub_00183910(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return unbound("00183910 (0015B530 +5 = 0x17; untranslated)");
}

/* ---- bound workers ------------------------------------------------------ */

/* 001837A0 (byte-matched, src/func_001837A0.c): an empty leaf. */
static int w_001837A0(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return 0;
}

/* 001FBD50(p, id, 0, radius): the live play path at the record's +B0. */
static int w_sound(void *c, EmPlayerLiveActor *a, int id, int a2, float radius)
{
    (void)c;
    if (!a || id < 0 || a2 != 0) return unbound("001FBD50 with an argument the live path does not take");
    const float at[3] = { em_live_f32(a, 0xB0), em_live_f32(a, 0xB4), em_live_f32(a, 0xB8) };
    em_sfx_play_at((unsigned)id, at, radius);
    return 0;
}

/* 0011A070's body: stop track `track`, hard for the 0x8000 form. */
static int w_sound_stop(void *c, int track, int hard)
{
    (void)c;
    return em_sfx_stop_track(track, hard);
}

/* 001C64F0 on the live display (the pose host's source). */
static int w_advance(void *c, EmPlayerLiveActor *a, float step, uint32_t *flags)
{
    (void)c; (void)a;
    return player_pose_stage_advance(step, flags);
}

/* 001AEDE0(a0, a1): the live transition fade-out. */
static int w_fade(void *c, int a0, int a1)
{
    (void)c;
    em_frame_fade_start_colour(1, a0, (uint8_t)a1);
    return 0;
}

/* The takeover stand-in (em_player.h EmPlayerStatesBinding.takeover). */
static int w_takeover(void *c)
{
    (void)c;
    return player_pose_stage_hook();
}

/* The workers' scene views, from the canonical storage before every stage. */
static int w_load(void *c)
{
    (void)c;
    EmSceneState *s = em_scene_state();
    uint8_t *d810707 = em_scene_progress_at(s, 0x00810707u, 1);
    uint8_t *d81083C = em_scene_progress_at(s, 0x0081083Cu, 1);
    uint8_t *d810C7E = em_scene_progress_at(s, 0x00810C7Eu, 1);
    if (!d810707 || !d81083C || !d810C7E) return -1;
    /* 0021C3F0 reads D_00810770 only in area 8 room 2; that byte is not
     * canonical yet (event 0x18, census L19). */
    if (s->d810700 == 8 && s->d810701 == 2) {
        fprintf(stderr, "player stage: area 8 room 2 needs D_00810770 (not canonical yet)\n");
        return -1;
    }
    live.globals.d8106C8 = (int32_t)em_scene_req_u32(s, EM_SCENE_REQ_C8);
    live.globals.d810701 = s->d810701;
    live.globals.d81083C = *d81083C;
    live.globals.d810C7E = *d810C7E;
    live.globals.d810707 = d810707;
    return 0;
}

int em_player_stage_live_bind(void)
{
    player_states_bind_display(0);
    if (!live.rates_loaded) {
        if (em_player_clip_rates_load(&live.rates, EM_PLAYER_CLIP_RATE_PATH) < 0) {
            fprintf(stderr, "player stage: %s is missing or invalid "
                            "(python3 tools/export_player_tables.py)\n", EM_PLAYER_CLIP_RATE_PATH);
            player_states_bind(NULL);
            return -1;
        }
        live.rates_loaded = 1;
    }
    /* The one pose owner: the player's clip clock, node channels and
     * skeleton live in this record, worked by em_pose_host_workers
     * (em_player_pose_host.c over em_player_record_pose). The bank and the
     * row column were loaded with the area (player_pose_load, 001ADF50's
     * read precedes this 001AFCA0 rebuild). */
    uint8_t *d8106F3 = em_scene_req_at(em_scene_state(), 0x008106F3u);
    if (!d8106F3 ||
        !player_pose_attach(player_states_actor_mut(), d8106F3, player_states_scene(), &live.globals)) {
        fprintf(stderr, "player stage: the player's clip bank is not loaded "
                        "(python3 tools/export_player_clips.py; python3 tools/export_player_tables.py)\n");
        player_states_bind(NULL);
        return -1;
    }
    struct EmPoseHost *pose = player_pose_record_host();
    /* 0x70003A20: one word, the stage workers' (POSE_HOST_WORKERS.md
     * section 3; 0021C440 / 0021D6C0 store their atan2 there, 00178910 its
     * |dy| and atan2). */
    pose->globals->spad3A20 = &live.globals.spad3A20;

    memset(&live.host, 0, sizeof live.host);
    live.host.stage = player_states_scene();
    live.host.globals = &live.globals;
    live.host.rates = &live.rates;
    EmPlayerStageCallees *c = &live.host.callees;
    /* Every callee below ignores its context except the pose workers, whose
     * context is the record's EmPoseHost. */
    c->context = pose;
    c->w001D0C70 = stub_001D0C70;
    /* 00183090's bone_init_default_2 / anim_clip_init and 00174A50 /
     * 0017C370's 001749A0 on the record: the pose owner's routines. */
    c->bone_init = em_pose_host_stage_bone_init;
    c->clip_init = em_pose_host_stage_clip_init;
    c->request = em_pose_host_stage_request;
    /* 001C64F0's own callees. The bound advance is w_advance (the pose
     * host's player_pose_stage_advance over the same record and workers). */
    c->clip_resolve = em_pose_host_stage_clip_resolve;
    c->skeleton_frame = em_pose_host_stage_skeleton_frame;
    c->w001C8710 = em_pose_host_stage_8710;
    c->w001C87C0 = em_pose_host_stage_87C0;
    c->sample_bones = em_pose_host_stage_sample_bones;
    c->sound = w_sound;
    c->cue = stub_cue;
    c->w001EFE00 = stub_001EFE00;
    c->w001F00A0 = stub_001F00A0;
    c->w001F0060 = stub_001F0060;
    c->atan2 = stub_atan2;
    c->link20 = stub_link20;
    c->clip_lookup = stub_clip_lookup;
    c->w0015C9D0 = stub_0015C9D0;
    c->link1C = stub_link1C;
    c->sound_stop = w_sound_stop;

    live.major4.stage = player_states_scene();
    live.major4.routine[EM_PLAYER_MAJOR4_00182DF0] = stub_00182DF0;
    live.major4.routine[EM_PLAYER_MAJOR4_001837A0] = w_001837A0;
    live.major4.routine[EM_PLAYER_MAJOR4_001837B0] = stub_001837B0;
    live.major4.routine[EM_PLAYER_MAJOR4_00162DB0] = stub_00162DB0;
    live.major4.routine[EM_PLAYER_MAJOR4_00163B40] = stub_00163B40;
    live.major4.routine[EM_PLAYER_MAJOR4_001838B0] = stub_001838B0;
    live.major4.routine[EM_PLAYER_MAJOR4_00183910] = stub_00183910;
    live.fade = (EmPlayerStageFade){ NULL, w_fade };

    EmPlayerStatesBinding b;
    memset(&b, 0, sizeof b);
    em_player_stage_workers_bind(&b.stage, &live.host);
    b.stage.advance = w_advance;
    b.stage.major[4] = em_player_stage_0015B530;
    b.stage.major_context[4] = &live.major4;
    b.stage.major[6] = em_player_stage_0015D460;
    b.stage.major_context[6] = &live.fade;
    b.load = w_load;
    b.takeover = w_takeover;
    /* Census L06/L07: the floor service's collision workers over the area's
     * original collision world, when it has one (AREA11). FLOOR itself stays
     * gated (player_states_missing: the display, the closure callbacks and
     * the SDK set are not bound yet). The live record's +0x02 is the query
     * class (& 0x1F; 0 for the player). */
    if (em_collision_world_loaded() &&
        em_collision_world_bind_player(&b, player_states_actor(), player_states_actor()->bytes[2]) < 0)
        return -1;
    player_states_bind(&b);
    /* The display stage draws the record's evaluated pose for every stage a
     * translated routine owns (em_player_frame.c actor_update): every clip
     * of the bank, chains and hold frames as 001C64F0 leaves them. */
    player_states_bind_display(1);
    return 0;
}

unsigned em_player_stage_live_faults(void)
{
    return live.faults;
}
