/* em_player.h — player locomotion, footsteps and aim direction.
 *
 * The player's own frame: wall probes and collide-and-slide, the turn-rate
 * gait ladder, the camera-relative move basis, the footstep picker with its
 * surface-attribute table, and the aim-direction ladder. Split out of em_game.c,
 * which had grown to hold the entire gameplay frame. Behaviour is unchanged by
 * the move — only the file boundary is new.
 */
#ifndef EM_PLAYER_H
#define EM_PLAYER_H

/* The subsystem's shared types and state live here. */
#include <stdio.h>

#include "game/em_game_internal.h"
#include "game/em_player_floor.h"

void player_move(void);
void player_turn_toward(float desired, float rate);
float player_move_cam_yaw(void);
void aim_dir_get(float out[3]);
unsigned footstep_rand5(void);
void player_move_collide(float mx, float mz);
void player_wall_probes(void);

/* WP-2/H12 pose-source lifetime (defined in em_player_pose_host.c). A port
 * stand-in holds the original channel source frozen while it owns the
 * player; its release re-seeds the row default as 00182DF0 does. Release
 * returns 1 when the source is ordinary again, 0 while still held. */
void player_pose_legacy_hold(const char *owner);
int player_pose_legacy_release(void);
void player_pose_unsupported_hold(const char *reason);

/* WP-15/H11 reversal skid (docs/PLAYER_REVERSAL.md).
 * player_reversal_palette: call in the player display stage right after
 * player_pose_foot_stop_palette(); returns 1 when it produced the palette
 * from the requested skid clip, 0 when the reversal does not own the
 * display, -1 when that clip is missing from the display model.
 * player_reversal_owns_walk: 1 while 001612D0 case 2 owns the callback
 * (its exit tick has +1F0=0 but is not the idle callback).
 * player_reversal_set_effect_worker binds 001EFD90 (surface effect ids
 * 0x80000033/0x80000012 at the player position and yaw); while unbound,
 * reaching the effect is a worker fault (counted by player_reversal_faults). */
int player_reversal_palette(void);
int player_reversal_owns_walk(void);
void player_reversal_set_effect_worker(int (*worker)(void *context, uint32_t id,
                                                      const float position[3], float yaw),
                                       void *context);
unsigned player_reversal_faults(void);
/* The display stage declares that it calls player_reversal_palette. The skid
 * stays disengaged until display, effect worker and clips 6/7 are all bound. */
void player_reversal_bind_display(int bound);

/* WP-15 P14/P15 footsteps (docs/PLAYER_FLOOR.md). 0015BCF0 calls 00187350
 * once per player stage after the state callback and skeleton evaluation:
 * the player stage calls player_footstep_0187350(spad 3B68, D_00810700)
 * after actor_update. It replaces the display-clock step_crossed trigger
 * (step_crossed/footstep_play stay only until that call site is removed).
 * Returns 0, or -1 on a fault (reported, counted).
 * player_footstep_set_workers binds 001EFD90 (effect id, position, actor
 * Euler), 001F0460 (wet-floor decal: position, yaw, pitch) and 001E8B90
 * (wade level); a reached unbound worker is a counted fault.
 * player_footstep_post is the 0017C030/melee step mailbox (0x80|tier). */
int player_footstep_0187350(uint32_t frame, uint8_t area);
void player_footstep_set_workers(
    int (*effect)(void *context, uint32_t id, const float position[3],
                  const float rotation[3]), void *effect_context,
    int (*decal)(void *context, const float position[3], float yaw, float pitch),
    void *decal_context,
    int (*wade)(void *context, const float position[3], float level),
    void *wade_context);
unsigned player_footstep_faults(void);
void player_footstep_reset(void);
void player_footstep_post(uint8_t code);
uint8_t player_footstep_phase(void);

/* WP-15 P16 (docs/PLAYER_FLOOR.md): player_wall_probes runs the translated
 * 001764E0 over the port's collision; unbound workers it reaches (00176180,
 * 001762E0 shove, 00174A50 row request) are counted here. */
unsigned player_probe_faults(void);

/* ---- Live player states (docs/FIRST_CONTROL.md "Live player states") ------
 * The original player stage 0015BCF0 -> 0015BA50 -> the +4 handler (0015B130
 * for +4 = 1, whose +5 table holds 00161020 idle, 001612D0 walk, 0016C6A0
 * slide, 00161790 climb, ...). The idle/walk tails run 001764E0, the +B4
 * lowering, 00175900(p, 1), 001756E0 and 001796C0.
 *
 * Two mechanisms, each engaged only once every original worker, datum and
 * callback it can reach is bound (the reversal skid's gate, generalised):
 *   - STAGE (census L01): 0015BA50 (em_player_stage_begin / _dispatch /
 *     _end), 0015B130 and 0015BCF0's writes after it (em_player_stage_tail)
 *     run on every player stage, with the stage workers (0021C440, 0015D100,
 *     0015D000, the prelude, 0011A070, D_00248C98, the display's 001C64F0)
 *     and the +4 = 4 / 6 handlers. The port's own idle/walk callbacks are
 *     0015B130's state[0] / state[1] (until L12 binds 00161020 / 001612D0).
 *   - FLOOR (and USE on top of it): the translated 00175900/001796C0
 *     (em_player_floor.c) replace the port's floor snap and
 *     PLAYER_FALL_ENTRY stand-in, and every other (+4, +5) the floor can
 *     reach runs its bound callback.
 * Until a mechanism is engaged the port's path for it runs unchanged.
 *
 * The live actor is EmPlayerLiveActor (em_player_floor.h): the player record
 * in its original byte layout, shared by the stage, the floor service, the
 * fall check and every state callback. Its +B0/+C4 are the port's
 * g.pos/g.yaw while the port's own idle/walk callbacks own the player, and
 * its vitals (+220 health, +224 pending damage, +228 infection, +22C pending
 * infection, +234 infected latch, +20E post-hit countdown) are a per-stage
 * view of the port's storage for them (g.status / g.pd_*): loaded before
 * 0015BA50 and stored back after 0015BCF0's tail. */

#define EM_PLAYER_STATE_COUNT EM_PLAYER_STATE1_COUNT

/* Everything the live layer needs from its binder (the coordinator). Each
 * worker keeps the EmPlayerFloorWorkers / EmPlayerFallWorkers contract. */
typedef struct EmPlayerStatesBinding {
    /* 0019AB20(player, ...): em_actor_collision_player_ground over the
     * actor-collision world (docs/ACTOR_COLLISION.md section 7 item 4). */
    int (*ground)(void *context, const float position[3], const float probe[3],
                  unsigned mask, EmPlayerProbeHit *hit);
    void *ground_context;
    /* The grid that worker walks: it must carry EM_COLL_FLAG_NODE_CLASS (the
     * authored class byte 00175CF0 records; without it the worker faults on
     * every grid hit). */
    const EmCollision *grid;
    /* 0019B6C0 (surface record) and 0019B8C0 (object probe). */
    int (*head)(void *context, const float top[3], const float bottom[3], EmPlayerProbeHit *hit);
    int (*object)(void *context, const float at[3], const float probe[3], unsigned mask,
                  EmPlayerProbeHit *hit);
    void *probe_context;
    /* 00175640(*(+214)): em_actor_collision_player_link. */
    int (*link_test)(void *context, const void *owner, int *result);
    void *link_context;
    /* 0019BC40(position) for 00179450: em_actor_collision_player_column. */
    int (*column)(void *context, const float position[3], EmPlayerFloorTable *table);
    void *column_context;
    /* SDK 0011E620 atan2f, 0011E398 tanf, 0011DBB8 atanf, 0011E748 sqrtf
     * (EmPlayerFloorWorkers). They return a value, not a status: a worker
     * that fails records it in *sdk_fault (nonzero), which the floor service
     * and the fall check clear before they run and check after (fail-stop:
     * the call fails, no value is used). NULL: the workers cannot fail. */
    float (*atan2)(void *context, float y, float x);
    float (*tangent)(void *context, float x);
    float (*atan)(void *context, float x);
    float (*sqrt)(void *context, float x);
    void *sdk_context;
    uint32_t *sdk_fault;
    /* 0017F9E0 / 0017FB90 (surface 0x39; no AREA11 grid node carries 0x39). */
    int (*surface39)(void *context, int handler);
    void *surface39_context;
    /* The stage's workers and callbacks (em_player_floor.h):
     * stage.clip_rate (D_00248C98), stage.advance (001C64F0), stage.commit
     * (00183090), stage.reaction (0021C440), stage.drain (0015D100),
     * stage.heartbeat (0015D000), stage.scripted_check / scripted_notify /
     * row_request (00182B30 / 00182D70 / 00174A50), stage.stop_sound
     * (0011A070); stage.state[] (0015B130's table), stage.state2[] /
     * phase13[] / phase14[] (0015B770's), stage.major[0/4/5/6] (0015C420,
     * 0015B530, 0015B610, 0015D460 -- em_player_stage_0015D460 with an
     * EmPlayerStageFade). major[1] and major[2] are set by player_states_bind
     * to the translated 0015B130 / 0015B770 over this binding, and
     * state[0] / state[1] to the port's own idle/walk callbacks; the
     * binding's own values there are ignored. */
    EmPlayerStageWorkers stage;
    /* Before every stage: refresh the stage workers' views of the scene
     * bytes they read (the EmPlayerStageGlobals of their host). 0, or -1 (a
     * fault: the stage does not run). NULL when the workers read none. */
    int (*load)(void *context);
    void *load_context;
    /* The scripted takeover stand-in at 0015B130's prelude position (the
     * AREA11 interaction runtime through the pose host,
     * player_pose_stage_hook): -1 fault, 0 ordinary, 1 consumed (the
     * runtime owns the player this stage: 0015B130 does not run). NULL: no
     * takeover owner. */
    int (*takeover)(void *context);
    void *takeover_context;
} EmPlayerStatesBinding;

/* Prerequisites, one bit each (player_states_missing). */
enum {
    EM_PLAYER_NEED_GROUND       = 1u << 0,  /* 0019AB20 worker */
    EM_PLAYER_NEED_NODE_CLASS   = 1u << 1,  /* grid with EM_COLL_FLAG_NODE_CLASS */
    EM_PLAYER_NEED_HEAD         = 1u << 2,  /* 0019B6C0 */
    EM_PLAYER_NEED_OBJECT       = 1u << 3,  /* 0019B8C0 */
    EM_PLAYER_NEED_LINK         = 1u << 4,  /* 00175640 */
    EM_PLAYER_NEED_COLUMN       = 1u << 5,  /* 0019BC40 */
    EM_PLAYER_NEED_SDK          = 1u << 6,  /* 0011E620 / 0011E398 (tanf) / 0011DBB8 / 0011E748 */
    EM_PLAYER_NEED_DISPLAY      = 1u << 7,  /* the display stage draws bound states */
    EM_PLAYER_NEED_FLOOR_STATES = 1u << 8,  /* the FLOOR state closure (kFloorStates) */
    EM_PLAYER_NEED_USE_CHAIN    = 1u << 9,  /* 00160220 past 00184BA0 (use hook) */
    EM_PLAYER_NEED_USE_STATES   = 1u << 10, /* +5 = 2, 3, 6, 0xB, 0x24 (kUseStates) */
    EM_PLAYER_NEED_STOP_SOUND   = 1u << 11, /* 0011A070 (0015BCF0's loop-sound stop) */
    EM_PLAYER_NEED_STAGE        = 1u << 12, /* 0015BA50 / 0015B130 / 0015B770 workers */
};
/* FLOOR: 00175900 and 001796C0 replace the port's floor snap and
 * PLAYER_FALL_ENTRY. They enter the fall (+5 = 5, 00179680) and the slide
 * (0x1C, on the hill's authored 0x1000 nodes) on ordinary AREA11 walks, so
 * every (+4, +5) those two states can reach before handing back to +4 = 1,
 * +5 = 0/1 is part of the mechanism (kFloorStates, derived from the
 * originals: docs/FIRST_CONTROL.md "FLOOR state closure"), with 0015BCF0's
 * -200 check (+4 = 6) and 0015B130's scripted prelude (+4 = 4). */
#define EM_PLAYER_MECH_FLOOR (EM_PLAYER_NEED_GROUND | EM_PLAYER_NEED_NODE_CLASS | \
    EM_PLAYER_NEED_HEAD | EM_PLAYER_NEED_OBJECT | EM_PLAYER_NEED_LINK | EM_PLAYER_NEED_COLUMN | \
    EM_PLAYER_NEED_SDK | EM_PLAYER_NEED_DISPLAY | EM_PLAYER_NEED_FLOOR_STATES | \
    EM_PLAYER_NEED_STOP_SOUND | EM_PLAYER_NEED_STAGE)
/* USE: the rest of the Use chain 00160220 as one unit, since one press can
 * reach any of its entries: 0015D4C0 (AREA11: only case 0x32, the ladder
 * columns -> 0xB), 0015DF10 (ledge climb 2 / vault 3), 0015EC50 (running
 * jump 6) and 0015FDF0 (0x24). Everything those reach beyond them is
 * already in the FLOOR closure. */
#define EM_PLAYER_MECH_USE (EM_PLAYER_MECH_FLOOR | EM_PLAYER_NEED_USE_CHAIN | \
    EM_PLAYER_NEED_USE_STATES)
/* STAGE (census L01): 0015BA50 / 0015B130 / 0015BCF0's tail on every player
 * stage. Its workers include the +4 = 4 (0015B530) and +4 = 6 (0015D460)
 * handlers, which 0015B130's prelude and 0015BCF0's -200 check enter. */
#define EM_PLAYER_MECH_STAGE (EM_PLAYER_NEED_STOP_SOUND | EM_PLAYER_NEED_STAGE)

/* Bind (or, with NULL, unbind) the live layer. The binding is copied. */
void player_states_bind(const EmPlayerStatesBinding *binding);
/* The display stage declares that it draws the source clip of every bound
 * state callback, advancing it by the stage's +34 (as
 * player_reversal_bind_display does for the skid). */
void player_states_bind_display(int bound);
/* The coordinator's use hook (00160220) declares that it continues past
 * 00184BA0 with 001AAC00, 0015D4C0, the trigger boxes, 0015DF10 x3,
 * 0015EC50 and 0015FDF0 (em_player_climb adapters) when Use found nothing. */
void player_states_bind_use_chain(int bound);
/* Missing prerequisites (EM_PLAYER_NEED_* bits) of the whole set, and of a
 * mechanism mask: 0 means that mechanism is engaged. */
unsigned player_states_missing(void);
int player_states_engaged(unsigned mechanism);
/* One line per mechanism naming what it still needs, to `out`. */
void player_states_report(FILE *out);
/* 1 while STAGE is engaged (player_states_stage runs the original stage). */
int player_states_stage_live(void);
/* One player stage while STAGE is engaged (the caller, em_player_frame.c's
 * actor_update, uses the port's legacy path otherwise): the vitals view
 * load, 0015BA50 (begin, the switch with the display's advance and the +4
 * handler, end), 0015BCF0's writes after it, the vitals store. Returns 1
 * when the takeover stand-in consumed the stage (the interaction runtime
 * published the player's palette), 0 otherwise (also after a fault, which
 * is fail-stop: reported once, counted, em_frame_request_quit). */
int player_states_stage(void);
/* D_008106B3 as this stage's 0015BA50 left it; -1 while STAGE is gated
 * off. (The canonical B3 byte is still written by em_player_0015BCF0's
 * stand-in expression.) */
int player_states_busy(void);
/* The stage's scene view (the EmPlayerStageScene the stage workers' host
 * must point at, PLAYER_STAGE_WORKERS.md section 2). */
EmPlayerStageScene *player_states_scene(void);
/* The live mirror (for the coordinator's D_008104C4 readers, e.g.
 * EmTruckWorld.ground_kind = the +0x0D of link_owner). */
const EmPlayerLiveActor *player_states_actor(void);
/* The same, writable (area load / scripted placement by the coordinator). */
EmPlayerLiveActor *player_states_actor_mut(void);
/* Services over a live actor for the state adapters (their floor / fall /
 * probes workers): 00175900(p, search) (*result = +A), 001796C0 and
 * 001764E0 with the bound workers. 0, or -1 on a fault or while gated off.
 * `context` is unused (the adapters' worker signature). */
int player_states_floor_service(void *context, EmPlayerLiveActor *actor, int search, int *result);
int player_states_fall_check(void *context, EmPlayerLiveActor *actor);
int player_states_wall_probes(void *context, EmPlayerLiveActor *actor);
/* Faults reached on the live path (a worker returned < 0). */
unsigned player_states_faults(void);
/* Area load: the mirror at 0015C420's values (+280 = (0, -13.8, 0), +4 = 1,
 * +5 = 0, link cleared). */
void player_states_reset(void);

/* Called from the gameplay frame in em_game.c as well as from this module. */
int  aim_ladder_eval(double t);
int  step_crossed(double prev, double cur, double trig);
void footstep_play(int tier);

#endif /* EM_PLAYER_H */
