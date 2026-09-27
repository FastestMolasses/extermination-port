/* em_player_stage_live.c - the live binding of the player stage (census lane
 * L01). See em_player_stage_live.h for what each worker binds to. */
#include "game/em_player_stage_live.h"

#include <stdio.h>
#include <string.h>

#include "game/em_area11_interaction_host.h"
#include "game/em_area11_roger.h"
#include "game/em_collision_world.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_player.h"
#include "game/em_player_closure_live.h"
#include "game/em_player_draw_live.h"
#include "game/em_player_ladder_climb.h"
#include "game/em_player_stage_workers.h"
#include "game/em_pose_host_workers.h"
#include "game/em_scene_state.h"
#include "game/em_scene_bindings.h"
#include "game/em_sfx.h"
#include "game/em_pad_actuator.h"

static struct {
    int rates_loaded;
    EmPlayerClipRates rates;
    EmPlayerStageGlobals globals;
    EmPlayerStageHost host;
    EmPlayerStageMajor4 major4;
    /* 0015B610 (+4 = 5): its routines, and the stage workers its admission
     * branch calls (00182B30 / 00174A50 / 00182D70 on `host`). */
    EmPlayerStageMajor5 major5;
    EmPlayerStageWorkers major5_workers;
    EmPlayerStageRelease release;   /* 00182DF0's context */
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

/* 001D0C70 (00183090 under 0x70003B8F == 2): the attached face's tick on
 * the AREA11 interaction host (em_player_face_host). */
static int w_001D0C70(void *c)
{
    (void)c;
    return em_area11_interaction_host_face_tick_001D0C70() < 0 ? -1 : 0;
}
/* 001B61C0 (0015D000's low-health heartbeat): the pad block D_00810E40
 * (em_pad_actuator, since census L23). */
static int stub_cue(void *c, int a0, int a1, int a2, int a3)
{
    (void)c;
    return em_pad_actuator_001B61C0((uint8_t)a0, (uint8_t)a1, a2, a3);
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

/* 0015B530 with 0x70003B8D clear: 00182DF0 on the stage's own takeover
 * (the pose host runs em_player_stage_00182DF0 with live.release, then ends
 * the interaction host's token). */
static int w_00182DF0(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    return player_pose_stage_release(a);
}

/* 00182DF0's loads outside the record. D_0028A580 is word 0x3C of the
 * D_0028A490 table (the Roger export's copy, em_area11_roger_table_word);
 * 001C6150(+0x44) is the exported player model's +8 byte
 * (em_player_draw_live_001C6150); D_00248A00 is in the record pose's table
 * region; D_00248C90's +0 column is the row column the pose loaded. */
static int r_0028A580(void *c, uint32_t *word)
{
    (void)c;
    return em_area11_roger_table_word(0x0028A580u, word) < 0 ? unbound("D_0028A580 (00182DF0; the Roger export)") : 0;
}
static int r_001C6150(void *c, uint32_t model, uint8_t *count)
{
    (void)c;
    return em_player_draw_live_001C6150(model, count) < 0 ? unbound("001C6150 on the player's +0x44 (00182DF0)") : 0;
}
static int r_00248A00(void *c, unsigned index, int16_t *clip)
{
    (void)c;
    const uint8_t *p = player_pose_record_bytes(0x00248A00u + 2u * index, 2);
    if (!p) return unbound("D_00248A00[+0x235] (00182DF0) outside the pose's table region");
    *clip = (int16_t)(uint16_t)(p[0] | p[1] << 8);
    return 0;
}
static int r_00248C90(void *c, int clip, int16_t *value)
{
    (void)c;
    return player_pose_row0(clip, value) < 0 ? unbound("D_00248C90[6 * +0x20C] (00182DF0) outside the rows") : 0;
}

/* 00182DF0's 00174AB0(p): its one translation (em_player_ladder_climb's)
 * over the record pose's 001749A0, as the 0x0E/0x18 closure runs it. */
static int release_request(void *c, EmPlayerLiveActor *a, int clip, int flags, float blend)
{
    (void)c;
    return em_pose_host_stage_request(live.host.callees.context, a, clip, flags, blend);
}
static int w_00174AB0(void *c, EmPlayerLiveActor *a)
{
    (void)c;
    EmPlayerLadderClimbWorkers workers;
    memset(&workers, 0, sizeof workers);
    workers.request = release_request;
    EmPlayerLadderClimb ladder = { &workers, NULL };
    return em_player_ladder_climb_00174AB0(&ladder, a);
}

/* 0015B530's routines that are not bound. */
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

/* 0015B610's routines that are not bound: +5 = 2, 3 and 4 (no first-level
 * spawn record or script writes them; 00183250, +5 = 1, is bound by the
 * closure binder over the original world). */
static int stub_00183250(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return unbound("00183250 (0015B610 +5 = 1; the original world's closure is not bound)");
}
static int stub_001833F0(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return unbound("001833F0 (0015B610 +5 = 2; untranslated)");
}
static int stub_00183440(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return unbound("00183440 (0015B610 +5 = 3; untranslated)");
}
static int stub_001834E0(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return unbound("001834E0 (0015B610 +5 = 4; untranslated)");
}

/* ---- bound workers ------------------------------------------------------ */

/* 001837A0 (byte-matched, src/func_001837A0.c): an empty leaf. */
static int w_001837A0(void *c, EmPlayerLiveActor *a)
{
    (void)c; (void)a;
    return 0;
}

/* 00183240 (byte-matched, src/func_00183240.c): an empty leaf. */
static int w_00183240(void *c, EmPlayerLiveActor *a)
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

/* The takeover hook (em_player.h EmPlayerStatesBinding.takeover). */
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
    c->w001D0C70 = w_001D0C70;
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
    /* 0017B490 on the record (em_locomotion_display's translation over the
     * record pose's regions, which map the exported D_00248AB0 rows). */
    c->clip_lookup = em_player_closure_live_0017B490;
    c->w0015C9D0 = stub_0015C9D0;
    c->link1C = stub_link1C;
    c->sound_stop = w_sound_stop;

    live.major4.stage = player_states_scene();
    live.major4.routine[EM_PLAYER_MAJOR4_00182DF0] = w_00182DF0;
    live.major4.routine[EM_PLAYER_MAJOR4_001837A0] = w_001837A0;
    live.major4.routine[EM_PLAYER_MAJOR4_001837B0] = stub_001837B0;
    live.major4.routine[EM_PLAYER_MAJOR4_00162DB0] = stub_00162DB0;
    live.major4.routine[EM_PLAYER_MAJOR4_00163B40] = stub_00163B40;
    live.major4.routine[EM_PLAYER_MAJOR4_001838B0] = stub_001838B0;
    live.major4.routine[EM_PLAYER_MAJOR4_00183910] = stub_00183910;
    live.fade = (EmPlayerStageFade){ NULL, w_fade };
    /* 0015B610: the scene view and the same host's 00182B30 / 00174A50 /
     * 00182D70 as the stage's; 00183250 is the closure binder's. */
    memset(&live.major5_workers, 0, sizeof live.major5_workers);
    em_player_stage_workers_bind(&live.major5_workers, &live.host);
    live.major5.stage.scene = player_states_scene();
    live.major5.stage.workers = &live.major5_workers;
    live.major5.routine[EM_PLAYER_MAJOR5_00183240] = w_00183240;
    live.major5.routine[EM_PLAYER_MAJOR5_00183250] = stub_00183250;
    live.major5.routine[EM_PLAYER_MAJOR5_001833F0] = stub_001833F0;
    live.major5.routine[EM_PLAYER_MAJOR5_00183440] = stub_00183440;
    live.major5.routine[EM_PLAYER_MAJOR5_001834E0] = stub_001834E0;
    /* 00182DF0: the one release of both takeovers (the stage's own and the
     * interaction runtime's), through the pose host. */
    live.release = (EmPlayerStageRelease){ &live.host, NULL, r_0028A580, r_001C6150, r_00248A00, r_00248C90,
                                           w_00174AB0 };
    player_pose_set_release_worker(em_player_stage_00182DF0, &live.release);

    EmPlayerStatesBinding b;
    memset(&b, 0, sizeof b);
    em_player_stage_workers_bind(&b.stage, &live.host);
    b.stage.advance = w_advance;
    b.stage.major[4] = em_player_stage_0015B530;
    b.stage.major_context[4] = &live.major4;
    b.stage.major[5] = em_player_stage_0015B610;
    b.stage.major_context[5] = &live.major5;
    b.stage.major[6] = em_player_stage_0015D460;
    b.stage.major_context[6] = &live.fade;
    b.load = w_load;
    b.takeover = w_takeover;
    /* Census L06/L07: the floor service's collision workers over the area's
     * original collision world, when it has one (AREA11). FLOOR itself stays
     * gated (player_states_missing: the display, the closure callbacks and
     * the SDK set are not bound yet). The live record's +0x02 is the query
     * class (& 0x1F; 0 for the player). */
    if (em_collision_world_loaded()) {
        if (em_collision_world_bind_player(&b, player_states_actor(), player_states_actor()->bytes[2]) < 0)
            return -1;
        /* 001756E0's 00174A50(p, 12.0) in the probe set. */
        em_collision_world_bind_player_pose(em_player_stage_row_request, &live.host,
                                            player_states_actor_mut());
        /* The FLOOR state closure and the Use roots (census L02 / L04 / L09;
         * em_player_closure_live.c binds every state callback with the
         * translations the module docs name). */
        if (em_player_closure_live_bind(&b, &live.host, pose, &live.major4, &live.major5) < 0) {
            fprintf(stderr, "player stage: the FLOOR closure could not be bound\n");
            player_states_bind(NULL);
            return -1;
        }
    }
    player_states_bind(&b);
    /* The Use chain: 00160220 over the record with its scan (the interaction
     * host's 00184BA0, em_area11_interaction_host_use) and every action
     * state bound by the closure. Without the original world there is no
     * dispatcher. */
    player_states_bind_use_chain(em_collision_world_loaded() && em_player_closure_live_use() != NULL);
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
