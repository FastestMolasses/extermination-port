/* em_player.c — player locomotion, footsteps and aim direction.
 *
 * The player's own frame: wall probes and collide-and-slide, the turn-rate
 * gait ladder, the camera-relative move basis, the footstep picker with its
 * surface-attribute table, and the aim-direction ladder. Split out of em_game.c,
 * which had grown to hold the entire gameplay frame. Behaviour is unchanged by
 * the move — only the file boundary is new.
 *
 * Every function here reads the shared gameplay state (EmGameState g), so
 * this module takes the subsystem's internal header rather than owning
 * private state — the same single state block the engine keeps in its
 * gameplay globals, now viewed from one more file. */

#include "game/em_player.h"
#include "game/em_effect_color.h"
#include "game/em_player_floor.h"
#include "game/em_player_heading.h"
#include "game/em_player_motor.h"
#include "game/em_player_reversal.h"
#include "game/em_random.h"
#include "game/em_scene_bindings.h"

#include "game/em_game_internal.h"

static uint8_t footstep_floor_attr(void);
static float player_turn_rate(int gait, float upt, float adelta);
static int floor_engaged(void);
static uint8_t live_contact(void);
static const void *live_link_owner(void);
static uint8_t live_link_type(void);
static uint8_t live_state(void);
static void port_park(void);
static void footstep_surface_from_live(uint8_t surface);

/* ---- WP-15 P16: 001764E0 radial probes over the port's collision -------
 * em_player_floor.c translates 001764E0 (with 00176390/00176BE0/001762E0/
 * 00176C80) and 001756E0; test_player_probe_reference.py runs the original
 * instructions. This binding supplies the probe workers:
 *   0019AD00/0019AFE0 -> em_collision_move_probe (movement walkers) plus the
 *     port's door hulls (em_door_probe) for mask bit 0; nearest hit wins,
 *     as the original's clamped segment does.
 *   001760C0 -> 0019AB20 over (at.y - 0.001 .. at.y + height) at the lane
 *     end, through em_collision_segment_query. The original uses the
 *     0019F730/0019C830 walkers, which the port has not translated; the
 *     segment walkers stand in for them (flagged in PLAYER_FLOOR.md).
 * The ankle pass runs only in the walk callback: 001764E0 tests +4==1 and
 * +5==1 at the callback tail, which the port's +1F0 mirrors (docs). */
static struct {
    uint8_t previous;      /* D_00275B00[4] */
    uint8_t inherited_s1;  /* the caller's $s1: 1, or 001612D0's resume clip */
    unsigned faults;
    int reported;
} probe;

static void probe_fault(const char *what)
{
    ++probe.faults;
    if (!probe.reported) {
        probe.reported = 1;
        fprintf(stderr, "player probes: %s at frame %d (counted)\n", what, g.frame_no);
    }
}

unsigned player_probe_faults(void) { return probe.faults; }

static void probe_hit_from(const EmCollHit *hit, int kind, const float target[3],
                           EmPlayerProbeHit *out)
{
    memset(out, 0, sizeof *out);
    out->kind = kind;
    out->node = (uint16_t)(hit->surf_class | hit->attr);
    memcpy(out->point, hit->point, sizeof out->point);
    memcpy(out->normal, hit->normal, sizeof out->normal);
    for (unsigned axis = 0; axis < 3; ++axis)
        out->delta[axis] = em_effect_float32((double)hit->point[axis] - target[axis]);
    if (kind == EM_COLL_SET_HULLS) {
        /* The port's door hulls: class-5 owners (em_door_original.h). */
        out->entity = 1;
        out->entity_flags = 5;
    } else if (hit->poly < 0) {
        /* A published class-4 actor cell. Only the AREA11 panel (uid 18)
         * is published; its owner bytes +2/+3 are 0x84/0x24 in the captured
         * playable RAM (owner 0x7AA590). */
        out->entity = 1;
        out->entity_flags = 0x84;
        out->entity_type = 0x24;
        if (hit->poly != -19) probe_fault("unpublished actor-cell owner bytes");
    }
}

static int probe_movement(const float position[3], const float target[3], unsigned mask,
                          EmPlayerProbeHit *out)
{
    EmCollHit hit, door;
    float start[3] = { position[0], target[1], position[2] };
    int kind = em_collision_move_probe(&g.coll, start, target,
                                       mask & (EM_COLL_SET_CELLS | EM_COLL_SET_GRID), &hit);
    int door_kind = 0;
    if ((mask & EM_COLL_SET_HULLS) && em_door_count() && em_door_probe(start, target, &door))
        door_kind = EM_COLL_SET_HULLS;
    if (door_kind) {
        float ds = 0, dd = 0;
        for (unsigned axis = 0; axis < 3; ++axis) {
            float a = hit.point[axis] - start[axis], b = door.point[axis] - start[axis];
            ds += a * a; dd += b * b;
        }
        if (!kind || dd < ds) {
            probe_hit_from(&door, door_kind, target, out);
            return door_kind;
        }
    }
    if (!kind) { memset(out, 0, sizeof *out); return 0; }
    probe_hit_from(&hit, kind, target, out);
    return kind;
}

static int probe_move(void *context, const float position[3], const float target[3],
                      unsigned mask, EmPlayerProbeHit *hit)
{
    (void)context;
    return probe_movement(position, target, mask, hit);
}

static int probe_sweep(void *context, const float from[3], const float to[3],
                       unsigned mask, EmPlayerProbeHit *hit)
{
    (void)context;
    return probe_movement(from, to, mask, hit);
}

static int probe_column(void *context, const float at[3], float height, EmPlayerProbeHit *hit)
{
    (void)context;
    /* 001760C0 stores at + (0,height,0); 0019AB20 starts height below it,
     * nudged 0.001 against the probe direction. EE float order. */
    float top = em_effect_float32((double)at[1] + height);
    float bottom = em_effect_float32((double)em_effect_float32((double)top - height) +
                                     (height < 0.0f ? 0.001f : -0.001f));
    float from[3] = { at[0], bottom, at[2] };
    float to[3] = { at[0], top, at[2] };
    EmCollHit found;
    int kind = em_collision_segment_query(&g.coll, from, to,
                                          EM_COLL_SET_CELLS | EM_COLL_SET_GRID, 0, &found);
    if (!kind) { memset(hit, 0, sizeof *hit); return 0; }
    probe_hit_from(&found, kind, to, hit);
    return kind;
}

static int probe_hull_shove(void *context, const float target[3])
{
    (void)context; (void)target;
    probe_fault("00176180 class-2 hull shove is not bound");
    return 0;
}

static int probe_target_shove(void *context)
{
    (void)context;
    probe_fault("001762E0 area-2 target shove is not bound");
    return 0;
}

static int probe_pose(void *context, float blend)
{
    (void)context; (void)blend;
    /* 00174A50 requests the +235 row default; rows 2/3 are not exported. */
    probe_fault("00174A50 low-clearance row request is not bound");
    return 0;
}

static float probe_sqrt(void *context, float x) { (void)context; return sqrtf(x); }
static float probe_atan(void *context, float x) { (void)context; return atanf(x); }

static const EmPlayerProbeWorkers kProbeWorkers = {
    NULL, probe_move, probe_sweep, probe_column, probe_hull_shove, probe_target_shove,
    probe_pose, probe_sqrt, probe_atan
};

static void probe_actor(EmPlayerProbeActor *actor)
{
    memset(actor, 0, sizeof *actor);
    memcpy(actor->position, g.pos, sizeof actor->position);
    actor->yaw = g.yaw;
    actor->speed = g.loco_upt;
    actor->major = 1;
    /* +5 at the callback tail: 1 in the walk callback, 0 in idle; the tick
     * that ends a walk (+1F0 back to 0) has already written +5=0, and the
     * idle entry handoff has written +5=1 with +1F0=1. */
    actor->state = g.loco_mode != 0 ? 1 : 0;
    actor->mode = (uint8_t)g.loco_mode;
    actor->variant = (uint8_t)g.loco_substate;
    actor->row = (uint8_t)((g.status.health <= PD_LOW_HEALTH ? 1 : 0) |
                           (g.probe_low_clearance ? 2 : 0));
    actor->special = g.probe_low_clearance;
    actor->obstruction = g.probe_block_mask;
    /* +A: 001756E0's area-0x12 gate. The floor service writes it once the
     * live floor is engaged; before that the port's snap always lands. */
    actor->contact = floor_engaged() ? live_contact() : 1;
}

void player_wall_probes(void)
{
    if (!g.coll.poly_count) return;
    EmPlayerProbeActor actor;
    probe_actor(&actor);
    EmPlayerProbeScene scene = { em_scene_state()->d810700,
                                 probe.inherited_s1 ? probe.inherited_s1 : 1, probe.previous };
    probe.inherited_s1 = 1;
    if (em_player_wall_probes(&actor, &scene, &kProbeWorkers) < 0) {
        probe_fault("001764E0 worker fault");
        return;
    }
    static int trace = -1;
    if (trace < 0) trace = getenv("EM_PROBE_TRACE") != NULL;
    if (trace)
        printf("probe: frame %d lanes 0x%02X push (%.6f, %.6f, %.6f) low %u\n", g.frame_no,
               actor.obstruction, actor.position[0] - g.pos[0],
               actor.position[1] - g.pos[1], actor.position[2] - g.pos[2], actor.special);
    memcpy(g.pos, actor.position, sizeof g.pos);
    g.probe_block_mask = actor.obstruction;
    g.probe_low_clearance = actor.special;
    probe.previous = scene.previous_obstruction;
}

/* 001756E0 after the floor service: release (or keep) the low clearance. */
static void player_clearance_release(void)
{
    if (!g.coll.poly_count) return;
    EmPlayerProbeActor actor;
    probe_actor(&actor);
    if (floor_engaged()) {
        /* 001756E0 reads +214 and (+214)+3 == 6 (its area-0x12 branch) and
         * +5: the values 00175900 has just left in the live actor. */
        actor.link = live_link_owner() != NULL;
        actor.link_type = live_link_type();
        actor.state = live_state();
    }
    EmPlayerProbeScene scene = { em_scene_state()->d810700, 1, probe.previous };
    if (em_player_clearance_release(&actor, &scene, &kProbeWorkers) < 0) {
        probe_fault("001756E0 worker fault");
        return;
    }
    g.probe_low_clearance = actor.special;
}

/* ---- Live player states (em_player.h, docs/FIRST_CONTROL.md) -------------
 * Gated: nothing below runs until player_states_engaged(EM_PLAYER_MECH_FLOOR)
 * holds, i.e. every worker, datum and reachable state callback is bound. */

/* A (+4, +5) the live stage can reach, with the routine the original runs
 * for it and where the transition is written. state WHOLE: the whole +4
 * handler (it dispatches +5 itself). */
#define WHOLE 0xFF
typedef struct { uint8_t major, state; const char *routine; } StateRef;

/* FLOOR's closure (docs/FIRST_CONTROL.md "FLOOR state closure"): from the
 * roots 001796C0 writes (5 through 00179680, 0x1C), every (+4, +5) written by
 * a reached state routine or its callees, followed until it hands back to
 * +4 = 1, +5 = 0/1 (the port's idle/walk), and every (+4, +5) written by the
 * stage workers that run on every +4 = 1 / 2 stage: 0021C440 (from 0015B130
 * and 0015B770) and the 0021D6C0 it calls. 0015D100 writes no +4/+5 (it only
 * arms 0021C440's +224/+F), and 0015B530's callees 00182DF0 / 001838B0 /
 * 001837B0 / 00183910 / 00162DB0 / 00163B40 write only pairs already listed
 * (00182DF0: +4 1 with +5 0 or 0xC; 001838B0: +4 4 +5 0). The +4 = 2
 * reaction states reach +4 1 +5 7 (0021D800 at 0021DAF8), 0x14/0x1F/0x20
 * (00223C70 at 00223F48/00223F2C/00223EFC), 0x1C/0x1D/0x1E (0021D530 at
 * 0021D554/0021D5BC/0021D588) and +4 2 +5 3/0x16, all listed. Surface 0x39
 * (0017F9E0 / 0017FB90, reached only from 00175CF0) is left out: no AREA11
 * grid node carries it, and the floor service faults on it (live_surface39). */
static const StateRef kFloorStates[] = {
    { 1, 0x05, "00162DB0 fall (00179680)" },
    { 1, 0x07, "001639E0 (00162DB0 at 001632BC/00163420, 0016C6A0 at 0016CCA8, 0021D800 at 0021DAF8)" },
    { 1, 0x08, "00163B40 landing (0017C580 at 0017C590)" },
    { 1, 0x09, "001647D0 hang (00162DB0 at 00163290)" },
    { 1, 0x04, "00162A40 (0017C860, from 001639E0)" },
    { 1, 0x0C, "001662D0 (001647D0 at 0016570C, 00182DF0 at 00182F24)" },
    { 1, 0x0E, "00168050 (001647D0 at 0016575C)" },
    { 1, 0x10, "00169730 (001662D0 at 00167C38)" },
    { 1, 0x12, "0016AE40 (002230A0)" },
    { 1, 0x13, "0016B790 (001696A0 at 001696D4)" },
    { 1, 0x14, "0016B8A0 (0016B790 at 0016B87C, 00223C70 at 00223F48/002240D4)" },
    { 1, 0x18, "0016D130 (001647D0 at 001657E8)" },
    { 1, 0x19, "0016DE40 (0016D130 at 0016D544)" },
    { 1, 0x1A, "0016EBA0 (0016D130 at 0016D6D0)" },
    { 1, 0x1C, "0016C6A0 slide (001796C0 at 0017973C, 0021D530 at 0021D554)" },
    { 1, 0x1D, "0016FCF0 (001607D0, 0021D530 at 0021D5BC)" },
    { 1, 0x1E, "001703E0 (001607D0, 0021D530 at 0021D588)" },
    { 1, 0x1F, "001729A0 (001607D0, 00223C70 at 00223F2C/002240B4)" },
    { 1, 0x20, "00173000 (001607D0, 00223C70 at 00223EFC/00224084)" },
    { 1, 0x21, "001735C0 (001607D0)" }, { 1, 0x22, "00173E60 (001607D0)" },
    { 2, 0x00, "0021D800 (0021C440 at 0021CCB8/0021CE90/0021D100)" },
    { 2, 0x17, "0021D800 (0021C440 at 0021CCB4/0021CE8C/0021D0C0)" },
    { 2, 0x01, "0021E240 (0021C440 at 0021C7FC/0021CDF8/0021CFE0, 0021F330 at 0021F530)" },
    { 2, 0x02, "0021E490 (0021C440 at 0021D0FC)" },
    { 2, 0x18, "0021E490 (0021C440 at 0021D0B4)" },
    { 2, 0x03, "0021E830 (0017C580 at 0017C600, 0021C440 at 0021C7E0/0021CC04/0021CDDC/0021CFC4, "
               "0021F330 at 0021F518, 00223C70 at 00224244)" },
    { 2, 0x04, "00221FC0 (00181110 at 0018115C, 0021C440 at 0021C860)" },
    { 2, 0x05, "00222580 (00181180 at 001811CC)" },
    { 2, 0x06, "00222AD0 (0017F240 at 0017F2A0)" },
    { 2, 0x07, "002230A0 (00181D70 at 00181DFC)" },
    { 2, 0x0A, "00223C70 (0021D6C0 at 0021D7C8, from 0021C440)" },
    { 2, 0x0B, "0021F330 (0021C440 at 0021CA7C: D_0081083C)" },
    { 2, 0x0C, "0021F850 (0021C440 at 0021C5B0: +F 1)" },
    { 2, 0x0F, "002202C0 (0021C440 at 0021C700/0021C744: +F 3/5)" },
    { 2, 0x10, "0021DBB0 (0021C440 at 0021C6BC: +F 2)" },
    { 2, 0x11, "0021E9C0 (0021C440 at 0021C81C: +F 6)" },
    { 2, 0x12, "0021EAD0 (0021C440 at 0021C89C: +F 7)" },
    { 2, 0x13, "0021EAD0 (0021C440 at 0021C93C: +F 0xA)" },
    { 2, 0x14, "0021EF30 (0021C440 at 0021C978: +F 0xB)" },
    { 2, 0x16, "00225570 (0021D250 at 0021D270: +23A 0x5D)" },
    { 2, 0x19, "002255C0 (001823E0 at 00182408)" },
    { 4, WHOLE, "0015B530 (0015B130's prelude under 0x70003B8D, 001838B0 at 001838E4)" },
    { 6, WHOLE, "0015D460 (0015BCF0: +B4 < -200)" },
};
/* USE's roots beyond that closure (what they reach is already in it). */
static const StateRef kUseStates[] = {
    { 1, 0x02, "00161790 ledge climb (0015DF10 at 0015EA10)" },
    { 1, 0x03, "00162190 vault (0015DF10 at 0015E9C8)" },
    { 1, 0x06, "001634A0 running jump (0015EC50 at 0015FD7C)" },
    { 1, 0x0B, "00165B60 ladder entry (0015D4C0 case 0x32 at 0015D808)" },
    { 1, 0x24, "001747F0 (00160220 at 0016078C, after 0015EC50/0015FDF0)" },
};

static struct {
    EmPlayerStatesBinding b;
    int bound, display, use_chain, initialised;
    EmPlayerLiveActor a;
    EmPlayerStageScene scene;
    EmPlayerStage stage;          /* context of stage.major[1] / major[2] */
    int stage_frame, stage_valid, end_frame, end_valid, busy_known;
    unsigned faults;
    int reported;
} live;

static EmPlayerStateCallback bound_state(const StateRef *r)
{
    const EmPlayerStageWorkers *s = &live.b.stage;
    if (r->state == WHOLE) return s->major[r->major];
    if (r->major == 1) return r->state < EM_PLAYER_STATE1_COUNT ? s->state[r->state] : NULL;
    if (r->major == 2) return r->state < EM_PLAYER_STATE2_COUNT ? s->state2[r->state] : NULL;
    return NULL;
}

/* The stage workers FLOOR needs (EM_PLAYER_NEED_STAGE), by name. */
static unsigned stage_missing_names(const char **names, unsigned capacity)
{
    const EmPlayerStageWorkers *s = &live.b.stage;
    const struct { int present; const char *name; } kWorkers[] = {
        { s->clip_rate != NULL, "D_00248C98 clip rate (0015BA50)" },
        { s->advance != NULL, "001C64F0 anim_advance_time (0015BA50)" },
        { s->commit != NULL, "00183090 (0015BA50, +4 = 4)" },
        { s->reaction != NULL, "0021C440 (0015B130/0015B770)" },
        { s->drain != NULL, "0015D100 (0015B130)" },
        { s->heartbeat != NULL, "0015D000 (0015B130)" },
        { s->scripted_check != NULL, "00182B30 (0015B130 prelude)" },
        { s->scripted_notify != NULL, "00182D70 (0015B130 prelude)" },
        { s->row_request != NULL, "00174A50 (0015B130 prelude)" },
    };
    unsigned count = 0;
    for (unsigned i = 0; i < sizeof kWorkers / sizeof *kWorkers; ++i)
        if (!live.bound || !kWorkers[i].present) {
            if (names && count < capacity) names[count] = kWorkers[i].name;
            ++count;
        }
    return count;
}

unsigned player_states_missing(void)
{
    const EmPlayerStatesBinding *b = &live.b;
    unsigned missing = 0;
    if (!live.bound || !b->ground) missing |= EM_PLAYER_NEED_GROUND;
    if (!live.bound || !b->grid || !(b->grid->flags & EM_COLL_FLAG_NODE_CLASS))
        missing |= EM_PLAYER_NEED_NODE_CLASS;
    if (!live.bound || !b->head) missing |= EM_PLAYER_NEED_HEAD;
    if (!live.bound || !b->object) missing |= EM_PLAYER_NEED_OBJECT;
    if (!live.bound || !b->link_test) missing |= EM_PLAYER_NEED_LINK;
    if (!live.bound || !b->column) missing |= EM_PLAYER_NEED_COLUMN;
    if (!live.bound || !b->atan2 || !b->cosine || !b->atan || !b->sqrt)
        missing |= EM_PLAYER_NEED_SDK;
    if (!live.display) missing |= EM_PLAYER_NEED_DISPLAY;
    for (unsigned i = 0; i < sizeof kFloorStates / sizeof *kFloorStates; ++i)
        if (!live.bound || !bound_state(&kFloorStates[i])) missing |= EM_PLAYER_NEED_FLOOR_STATES;
    if (!live.use_chain) missing |= EM_PLAYER_NEED_USE_CHAIN;
    for (unsigned i = 0; i < sizeof kUseStates / sizeof *kUseStates; ++i)
        if (!live.bound || !bound_state(&kUseStates[i])) missing |= EM_PLAYER_NEED_USE_STATES;
    if (!live.bound || !b->stage.stop_sound) missing |= EM_PLAYER_NEED_STOP_SOUND;
    if (stage_missing_names(NULL, 0)) missing |= EM_PLAYER_NEED_STAGE;
    return missing;
}

int player_states_engaged(unsigned mechanism)
{
    return (player_states_missing() & mechanism) == 0;
}

static int floor_engaged(void)
{
    return player_states_engaged(EM_PLAYER_MECH_FLOOR);
}

static uint8_t live_contact(void) { return em_live_u8(&live.a, 0xA); }
static const void *live_link_owner(void) { return live.a.link_owner; }
static uint8_t live_link_type(void) { return live.a.link_type; }
static uint8_t live_state(void) { return em_live_u8(&live.a, 5); }

void player_states_report(FILE *out)
{
    static const char *const kNeed[] = {
        "0019AB20 ground probe (em_actor_collision_player_ground)",
        "grid node class (EMCL header flag EM_COLL_FLAG_NODE_CLASS)",
        "0019B6C0 surface record", "0019B8C0 object probe",
        "00175640 link test (em_actor_collision_player_link)",
        "0019BC40 column table (em_actor_collision_player_column)",
        "SDK 0011E620/0011E398/0011DBB8/0011E748",
        "display of the bound states (player_states_bind_display)",
        "state callbacks", "00160220 past 00184BA0 (player_states_bind_use_chain)",
        "state callbacks", "0011A070 sound stop (0015BCF0)", "stage workers",
    };
    static const struct { const char *name; unsigned mask; } kMech[] = {
        { "floor service 00175900 + fall check 001796C0", EM_PLAYER_MECH_FLOOR },
        { "Use chain (ledge climb, vault, ladder, running jump)", EM_PLAYER_MECH_USE },
    };
    unsigned missing = player_states_missing();
    for (unsigned m = 0; m < sizeof kMech / sizeof *kMech; ++m) {
        unsigned need = missing & kMech[m].mask;
        fprintf(out, "player states: %s: %s", kMech[m].name, need ? "gated off; needs" : "engaged");
        for (unsigned bit = 0; bit < sizeof kNeed / sizeof *kNeed; ++bit) {
            if (!(need & (1u << bit))) continue;
            if ((1u << bit) == EM_PLAYER_NEED_FLOOR_STATES || (1u << bit) == EM_PLAYER_NEED_USE_STATES) {
                int use = (1u << bit) == EM_PLAYER_NEED_USE_STATES;
                const StateRef *list = use ? kUseStates : kFloorStates;
                unsigned count = use ? sizeof kUseStates / sizeof *kUseStates
                                     : sizeof kFloorStates / sizeof *kFloorStates;
                for (unsigned i = 0; i < count; ++i) {
                    if (live.bound && bound_state(&list[i])) continue;
                    if (list[i].state == WHOLE)
                        fprintf(out, " [+4 %u %s]", list[i].major, list[i].routine);
                    else
                        fprintf(out, " [+4 %u +5 0x%02X %s]", list[i].major, list[i].state,
                                list[i].routine);
                }
            } else if ((1u << bit) == EM_PLAYER_NEED_STAGE) {
                const char *names[16];
                unsigned count = stage_missing_names(names, 16);
                for (unsigned i = 0; i < count && i < 16; ++i) fprintf(out, " [%s]", names[i]);
            } else {
                fprintf(out, " [%s]", kNeed[bit]);
            }
        }
        fputc('\n', out);
    }
}

void player_states_reset(void)
{
    memset(&live.a, 0, sizeof live.a);
    /* 0015C420 (byte-matched): +280 = (0, 0xC15CCCCD = -13.8, 0, 1.0); for
     * the AREA11 spawn kind +4 = 1, +5 = 0, +6 = 0 (and +1F0 = 0, +204 =
     * 1.0, +31B = -1, +31A = 0). */
    em_live_set_f32(&live.a, 0x284, -13.8f);
    em_live_set_f32(&live.a, 0x28C, 1.0f);
    em_live_set_u8(&live.a, 4, 1);
    em_live_set_f32(&live.a, 0x204, 1.0f);
    em_live_set_u8(&live.a, 0x31B, 0xFF);
    live.stage_valid = live.end_valid = 0;
    live.busy_known = 0;
    live.initialised = 1;
}

void player_states_bind(const EmPlayerStatesBinding *binding)
{
    if (!live.initialised) player_states_reset();
    if (binding) live.b = *binding;
    else memset(&live.b, 0, sizeof live.b);
    live.bound = binding != NULL;
    if (live.bound) {
        /* 0015BA50's +4 = 1 / 2 entries are the translated 0015B130 /
         * 0015B770 over this binding's tables. */
        live.stage.scene = &live.scene;
        live.stage.workers = &live.b.stage;
        live.b.stage.major[1] = em_player_stage_0015B130;
        live.b.stage.major_context[1] = &live.stage;
        live.b.stage.major[2] = em_player_stage_0015B770;
        live.b.stage.major_context[2] = &live.stage;
    }
    live.reported = 0;
}

void player_states_bind_display(int bound) { live.display = bound != 0; }
void player_states_bind_use_chain(int bound) { live.use_chain = bound != 0; }
const EmPlayerLiveActor *player_states_actor(void) { return &live.a; }
EmPlayerLiveActor *player_states_actor_mut(void) { return &live.a; }
unsigned player_states_faults(void) { return live.faults; }
int player_states_busy(void) { return live.busy_known ? live.scene.busy : -1; }

static void live_fault(const char *what)
{
    /* A bound worker failed on the live path: fail-stop, as player_use_poll
     * does for the Use worker. */
    ++live.faults;
    if (!live.reported) {
        live.reported = 1;
        fprintf(stderr, "player states: %s at frame %d\n", what, g.frame_no);
    }
    em_frame_request_quit();
}

/* The stage's view of the canonical scene state: 3B8D, 3B8F and D_00810700
 * are loaded per stage (3B8F is stored back at the stage end);
 * D_008106F1 (request block) and D_00810CB6 (progress block, canonical
 * since WP-6) are pointers at their one canonical byte. If either were
 * missing, the busy result would be unknown (player_states_busy() = -1)
 * and em_player_stage_end refuses. */
static int live_scene_load(void)
{
    EmSceneState *s = em_scene_state();
    live.scene.spad3B8D = s->spad3B8D;
    live.scene.spad3B8F = s->spad3B8F;
    live.scene.area = s->d810700;
    live.scene.d8106F1 = em_scene_req_at(s, 0x008106F1u);
    live.scene.d810CB6 = em_scene_progress_at(s, 0x00810CB6u, 1);
    return live.scene.d8106F1 && live.scene.d810CB6;
}

void player_states_stage_begin(void)
{
    if (!live.initialised) player_states_reset();
    if (live.stage_valid && live.stage_frame == g.frame_no) return;
    static int reported;
    if (!reported) {
        /* Once per run: which mechanisms are engaged, and what each lacks. */
        reported = 1;
        player_states_report(stderr);
    }
    live.stage_valid = 1;
    live.stage_frame = g.frame_no;
    if (!floor_engaged()) return;
    live.busy_known = live_scene_load();
    /* +20C: the clip the pose source plays (the display binding keeps it the
     * clip the bound states request). */
    unsigned clip;
    if (player_pose_source(&clip, NULL, NULL, NULL)) em_live_set_u16(&live.a, 0x20C, (uint16_t)clip);
    if (em_player_stage_begin(&live.a, &live.scene, &live.b.stage) < 0)
        live_fault("0015BA50 D_00248C98 worker fault");
}

void player_states_stage_end(void)
{
    if (live.end_valid && live.end_frame == g.frame_no) return;
    live.end_valid = 1;
    live.end_frame = g.frame_no;
    if (!floor_engaged() || !live.stage_valid || live.stage_frame != g.frame_no) return;
    /* 0015BA50 after its switch, then 0015BCF0's +BC, -200 check and loop-
     * sound stop (every stage: idle and walk included). */
    if (em_player_stage_end(&live.a, &live.scene) < 0) {
        live_fault("0015BA50 D_008106F1/D_00810CB6 not bound");
        return;
    }
    uint8_t spad3B8F = live.scene.spad3B8F;
    if (em_scene_state()->spad3B8F != spad3B8F) em_scene_state()->spad3B8F = spad3B8F;
    for (unsigned axis = 0; axis < 3; ++axis) em_live_set_f32(&live.a, 0xB0 + 4 * axis, g.pos[axis]);
    if (em_player_stage_tail(&live.a, &live.b.stage) < 0) {
        live_fault("0011A070 worker fault");
        return;
    }
    if (em_live_u8(&live.a, 4) != 1) port_park();
}

/* Worker trampolines: each binding worker keeps its own context. */
typedef struct { EmPlayerFloorActor *floor; } LiveFloorContext;

static int live_ground(void *context, const float position[3], const float probe[3],
                       unsigned mask, EmPlayerProbeHit *hit)
{
    (void)context;
    return live.b.ground(live.b.ground_context, position, probe, mask, hit);
}
static int live_head(void *context, const float top[3], const float bottom[3], EmPlayerProbeHit *hit)
{
    (void)context;
    return live.b.head(live.b.probe_context, top, bottom, hit);
}
static int live_object(void *context, const float at[3], const float probe[3], unsigned mask,
                       EmPlayerProbeHit *hit)
{
    (void)context;
    return live.b.object(live.b.probe_context, at, probe, mask, hit);
}
static int live_link(void *context, const void *owner, int *result)
{
    (void)context;
    return live.b.link_test(live.b.link_context, owner, result);
}
static int live_surface39(void *context, int handler)
{
    (void)context;
    /* 0017F9E0 / 0017FB90: untranslated; no AREA11 grid node carries 0x39. */
    if (!live.b.surface39) return -1;
    return live.b.surface39(live.b.surface39_context, handler);
}
static int live_first_contact(void *context, uint8_t surface)
{
    LiveFloorContext *c = context;
    switch (surface) {
    case 0x5A:
        /* 00187DC0 (byte-matched): 001FBD50(p, 0x86, 0, 300.0). */
        em_sfx_play_at(0x86, c->floor->position, 300.0f);
        return 0;
    case 0x5C:
        /* 00187EA0 (byte-matched): 001FB9F0(0xA8, 0x1000, 0x1000, 0x1000). */
        em_sfx_play(0xA8);
        return 0;
    default:
        /* 00187DE0 (0x5B) copies 0x700031B0, the depth probe's point, which
         * this worker does not receive; no AREA11 grid node carries 0x5B. */
        return -1;
    }
}
static float live_atan2(void *context, float y, float x)
{
    (void)context;
    return live.b.atan2(live.b.sdk_context, y, x);
}
static float live_cosine(void *context, float x)
{
    (void)context;
    return live.b.cosine(live.b.sdk_context, x);
}
static float live_atan(void *context, float x)
{
    (void)context;
    return live.b.atan(live.b.sdk_context, x);
}
static float live_sqrt(void *context, float x)
{
    (void)context;
    return live.b.sqrt(live.b.sdk_context, x);
}
static int live_column(void *context, const float position[3], EmPlayerFloorTable *table)
{
    (void)context;
    return live.b.column(live.b.column_context, position, table);
}

/* 00175900(p, search) over a live actor, with the bound workers. Returns 0
 * and its +A in *result, or -1 on a worker fault. Exported for the state
 * adapters (EmPlayerSlideLive.floor, EmPlayerClimbLive.floor). */
int player_states_floor_service(void *context, EmPlayerLiveActor *actor, int search, int *result)
{
    (void)context;
    if (!floor_engaged()) return -1;
    EmPlayerFloorActor f;
    em_player_floor_actor_from_live(actor, &f);
    LiveFloorContext floor_context = { &f };
    const EmPlayerFloorWorkers workers = {
        &floor_context, live_ground, live_head, live_object, live_link, live_surface39,
        live_first_contact, live_atan2, live_cosine, live_atan, live_sqrt
    };
    /* sp50: the probe point of the floor hit; the service overwrites it on
     * every hit, and the object probe runs only after a hit this stage. */
    float at[3] = { f.position[0], f.position[1], f.position[2] };
    int contact = em_player_floor_service(&f, search, at, &workers);
    if (contact < 0) return -1;
    em_player_floor_actor_to_live(&f, actor);
    if (result) *result = contact;
    return 0;
}

/* 001796C0 over a live actor. 0, or -1 on a worker fault. */
int player_states_fall_check(void *context, EmPlayerLiveActor *actor)
{
    (void)context;
    if (!floor_engaged()) return -1;
    EmPlayerFallActor fall;
    em_player_fall_actor_from_live(actor, &fall);
    const EmPlayerFallWorkers workers = { NULL, live_column, live_ground };
    if (em_player_fall_check(&fall, &workers) < 0) return -1;
    em_player_fall_actor_to_live(&fall, actor);
    return 0;
}

/* 001764E0 over a live actor, with the port's probe workers (the same
 * binding player_wall_probes uses). 0, or -1 on a worker fault. */
int player_states_wall_probes(void *context, EmPlayerLiveActor *actor)
{
    (void)context;
    if (!g.coll.poly_count) return 0;
    EmPlayerProbeActor p;
    memset(&p, 0, sizeof p);
    for (unsigned axis = 0; axis < 3; ++axis) p.position[axis] = em_live_f32(actor, 0xB0 + 4 * axis);
    p.yaw = em_live_f32(actor, 0xC4);
    p.speed = em_live_f32(actor, 0x38);
    p.major = em_live_u8(actor, 4);
    p.state = em_live_u8(actor, 5);
    p.mode = em_live_u8(actor, 0x1F0);
    p.variant = em_live_u8(actor, 0x1F1);
    p.row = em_live_u8(actor, 0x235);
    p.special = em_live_u8(actor, 0x236);
    p.obstruction = em_live_u8(actor, 0x314);
    p.contact = em_live_u8(actor, 0xA);
    p.link = actor->link_owner != NULL;
    p.link_type = actor->link_type;
    EmPlayerProbeScene scene = { em_scene_state()->d810700, 1, probe.previous };
    if (em_player_wall_probes(&p, &scene, &kProbeWorkers) < 0) return -1;
    for (unsigned axis = 0; axis < 3; ++axis) em_live_set_f32(actor, 0xB0 + 4 * axis, p.position[axis]);
    em_live_set_u8(actor, 0x235, p.row);
    em_live_set_u8(actor, 0x236, p.special);
    em_live_set_u8(actor, 0x314, p.obstruction);
    probe.previous = scene.previous_obstruction;
    return 0;
}

/* The mirror's view of the port's idle/walk callbacks (the port keeps these
 * bytes in g while its own callbacks own the player). */
static void live_from_port(void)
{
    for (unsigned axis = 0; axis < 3; ++axis) em_live_set_f32(&live.a, 0xB0 + 4 * axis, g.pos[axis]);
    em_live_set_f32(&live.a, 0xC4, g.yaw);
    em_live_set_f32(&live.a, 0x38, g.loco_upt);
    em_live_set_u8(&live.a, 0x1F0, (uint8_t)g.loco_mode);
    em_live_set_u8(&live.a, 0x1F1, (uint8_t)g.loco_substate);
    em_live_set_u8(&live.a, 0x25C, (uint8_t)g.loco_tier);
    em_live_set_u8(&live.a, 0x23F, (uint8_t)g.gait);
    em_live_set_u8(&live.a, 0x314, g.probe_block_mask);
    em_live_set_u8(&live.a, 0x235, (uint8_t)((g.status.health <= PD_LOW_HEALTH ? 1 : 0) |
                                             (g.probe_low_clearance ? 2 : 0)));
    em_live_set_u8(&live.a, 0x236, g.probe_low_clearance);
}

static void port_from_live_position(void)
{
    for (unsigned axis = 0; axis < 3; ++axis) g.pos[axis] = em_live_f32(&live.a, 0xB0 + 4 * axis);
    g.yaw = em_live_f32(&live.a, 0xC4);
    g.probe_low_clearance = em_live_u8(&live.a, 0x236);
    g.probe_block_mask = em_live_u8(&live.a, 0x314);
}

/* The idle (00161020) and walk (001612D0) tails after 001764E0: the +B4
 * lowering, 00175900(p, 1), 001756E0, 001796C0. */
static void live_tail(int walk)
{
    live_from_port();
    em_live_set_u8(&live.a, 5, walk ? 1 : 0);   /* 00175900 reads +5 for 0x39 / 0x1C */
    float lower = !walk ? -0.2f : em_live_u8(&live.a, 0x23B) == 0x35 ? -0.8f : -0.4f;
    em_live_set_f32(&live.a, 0xB4, em_effect_float32((double)em_live_f32(&live.a, 0xB4) + lower));
    int contact;
    if (player_states_floor_service(NULL, &live.a, 1, &contact) < 0) {
        live_fault("00175900 worker fault");
        return;
    }
    port_from_live_position();
    player_clearance_release();                           /* 001756E0 */
    live_from_port();
    if (player_states_fall_check(NULL, &live.a) < 0) {    /* 001796C0 */
        live_fault("001796C0 worker fault");
        return;
    }
    port_from_live_position();
    /* +23A for 00187350 comes from this service from now on. */
    footstep_surface_from_live(em_live_u8(&live.a, 0x23A));
    uint8_t state = em_live_u8(&live.a, 5);
    if (state != 0 && state != 1) port_park();
}

/* 0015BA50's switch for everything but the port's own +4 = 1, +5 = 0/1 (the
 * idle 00161020 and walk 001612D0 callbacks): the translated stage
 * (em_player_stage_dispatch -> 0015B130 / 0015B770 / the bound +4 handlers)
 * owns the stage. Returns 1 when it ran (the port's callbacks do not), 0 for
 * the idle/walk family. */
static int live_stage_dispatch(void)
{
    uint8_t major = em_live_u8(&live.a, 4), state = em_live_u8(&live.a, 5);
    if (major == 1 && (state == 0 || state == 1)) return 0;
    for (unsigned axis = 0; axis < 3; ++axis) em_live_set_f32(&live.a, 0xB0 + 4 * axis, g.pos[axis]);
    em_live_set_f32(&live.a, 0xC4, g.yaw);
    if (em_player_stage_dispatch(&live.a, &live.b.stage) < 0) {
        live_fault("player stage worker or state callback fault");
        return 1;
    }
    port_from_live_position();
    footstep_surface_from_live(em_live_u8(&live.a, 0x23A));
    major = em_live_u8(&live.a, 4);
    uint8_t after = em_live_u8(&live.a, 5);
    if (major == 1 && after == 0) {
        /* +5 = 0 / +6 = 0: 00161020 case 0 runs next, as after the skid
         * (the stop hand-off phase 3 requests the idle row). */
        g.loco_mode = g.loco_substate = g.loco_tier = 0;
        g.loco_upt = g.move_speed = 0;
        g.loco_stop.phase = 3;
    } else if (major == 1 && after == 1) {
        /* Handed on to the walk callback (0017C440 in the climb). */
        g.loco_mode = em_live_u8(&live.a, 0x1F0);
        g.loco_substate = em_live_u8(&live.a, 0x1F1);
        g.loco_tier = em_live_u8(&live.a, 0x25C);
        g.loco_upt = em_live_f32(&live.a, 0x38);
    } else {
        g.loco_mode = em_live_u8(&live.a, 0x1F0);
    }
    return 1;
}

/* The callback tail. `walk` names the callback that ran: the walk callback
 * 001612D0 (lowers +B4 by 0.4, or 0.8 on +23B 0x35) or the idle callback
 * 00161020 (0.2). Without the live floor: 001764E0 and 001756E0 only (the
 * port's snap runs in player_move_collide). */
static void player_probe_tail(int walk)
{
    player_wall_probes();
    if (floor_engaged()) {
        live_tail(walk);
        return;
    }
    player_clearance_release();
}

void player_move_collide(float mx, float mz)
{
    EmCollHit hit;

    /* Integrate the move, then let the radial probes correct it (the
     * engine's order: the walk top writes +0xB0/B8, func_001764E0
     * pushes back). With the 4.5-unit radius far above the per-frame
     * step (0.3 u at run) the probes also own anti-tunneling. */
    g.pos[0] += mx;
    g.pos[2] += mz;
    if (floor_engaged()) {
        /* The walk tail with the translated floor service and fall check
         * in place of the snap below (live player states). */
        player_probe_tail(1);
        em_collision_moving_carry(g.pos);
        em_collision_blocker_probe(g.pos, PLAYER_WALL_RADIUS);
        return;
    }
    player_wall_probes();

    /* Floor: vertical segment query through the same worlds (the grid
     * world owns the walkable floor — FINDINGS "COLLISION WORLD"). The
     * same class split applies downward: a leaning wall face (e.g. the
     * snow scene's gate posts, n.y slightly > 0) front-faces the probe
     * from above, and accepting it ratchets the player up the wall while
     * sliding along it — step past non-walkable crossings instead. */
    float from[3] = { g.pos[0], g.pos[1] + FLOOR_PROBE_UP,    g.pos[2] };
    float down[3] = { g.pos[0], g.pos[1] - FLOOR_PROBE_DOWN,  g.pos[2] };
    for (int i = 0; i < 8; i++) {
        if (!em_collision_segment_query(&g.coll, from, down,
                                        EM_COLL_SET_CELLS |
                                        EM_COLL_SET_GRID, 0, &hit))
            break;
        if (hit.surf_class == EM_SURF_FLOOR ||
            hit.surf_class == EM_SURF_SLOPE) {
            /* GRAVITY — DECODED (func_00179880, readable C in the decomp):
             *     v += -0.04f;  if (v < -4.0f) v = -4.0f;  actor.y += v;
             * plus the airborne mode byte +0x25F = 2. Those two constants
             * are the engine's, verified literally.
             *
             * The port used to SNAP g.pos[1] to the floor hit every frame,
             * so stepping off a ledge teleported the player down instead of
             * dropping them. Now: when the floor is more than a step below,
             * integrate and fall; land when the integrated Y reaches it.
             *
             * BOUNDARY, updated once func_001796C0 was recovered (99.909%,
             * readable): the engine does NOT use a height threshold at all.
             * Its tick decays the same accumulator with the same -0.04 and
             * -4.0 clamp, but the LAND decision comes from func_00179450 — a
             * query over the scratchpad trigger tables D_70003170 (flags) and
             * D_700030F0 (heights), rebuilt each call by func_0019BC40. It
             * walks the live entries, takes the first one below the actor,
             * writes the delta to +0x258, and the caller lands when that
             * delta drops past -4.01 or when no entry qualifies at all.
             *
             * So this threshold is not a stand-in for a constant we had not
             * read — it is a DIFFERENT MECHANISM. Matching the engine means
             * modelling that trigger table, which the port's collision layer
             * does not expose today. Kept, and now honestly labelled: a port
             * approximation of a table-driven handoff, not an unread number. */
            const float floor_y = hit.point[1];
            const float drop    = g.pos[1] - floor_y;
            if (drop > PLAYER_FALL_ENTRY) {
                g.fall_vel += PLAYER_GRAVITY;
                if (g.fall_vel < PLAYER_FALL_TERMINAL)
                    g.fall_vel = PLAYER_FALL_TERMINAL;
                g.pos[1] += g.fall_vel;
                if (g.pos[1] <= floor_y) {      /* landed */
                    g.pos[1]   = floor_y;
                    g.fall_vel = 0.0f;
                }
            } else {
                g.pos[1]   = floor_y;
                g.fall_vel = 0.0f;
            }
            break;
        }
        if (hit.point[1] - 1e-3f <= down[1])
            break;
        from[1] = hit.point[1] - 1e-3f;
    }

    /* 001756E0 follows the floor service in the callback tail. */
    player_clearance_release();

    /* MOVING-SURFACE CARRY (em_collision.h moving-surface registry).
     * After the static floor snap above, consume the per-frame registry:
     * a player footprint on a registered surface within its Y band gets
     * that surface's velocity added. No actor registers a surface today:
     * the AREA-11 truck (em_truck_update) no longer registers its top —
     * its selftest asserts an empty registry — so this call is dormant
     * and returns 0 every frame. */
    em_collision_moving_carry(g.pos);

    /* STATIC BLOCKER PUSH-OUT (the AREA-11 closed GRATE — em_collision.h
     * §blocker, INVESTIGATION_area11_grate.md §5). After the static EMCL
     * wall solve (player_wall_probes) and the floor snap, eject the player
     * from any registered solid blocker AABB they have entered. The grate
     * registers its closed hull each frame ONLY while the area is NOT
     * powered (grate_update, run before actor_update), so once powered the
     * registry is empty and this is a no-op — movement away from the grate
     * is bit-for-bit unchanged (the probe returns 0 and touches nothing).
     * This is the SINGLE blocker touch in the player ground-solve, the same
     * minimal-addition shape as the moving-surface carry above. The wall
     * radius is the same (0,y,4.5) probe radius the EMCL solve uses. */
    em_collision_blocker_probe(g.pos, PLAYER_WALL_RADIUS);
}

/* player_turn_rate — func_00174AC0's banded rate select (rad/frame).
 * Picks the body-heading ease rate from whether we are turning in place
 * (current ramped speed == 0) vs. moving, then by the gait tier (in
 * place) or by |delta| crossed with the ramped speed loco_upt (moving).
 * See the BODY-HEADING TURN RATES block above for the table.
 *
 * CONFIRMED (audit 2026-07-31) — all nine rates re-read one by one out of
 * src/func_00174AC0.c (NEARMISS, logic authoritative), arg1 == 1 arm:
 *   +0x38 == 0.0f  (turn in place, keyed on the gait byte +0x23F)
 *       1 -> 0.06981317   2 -> 0.13962634   else -> 0.39269909
 *   otherwise, |wrap(ang - heading)| (func_0011DF78 = fabsf) split at
 *   0.9424779, then keyed on the ramped speed +0x38:
 *       > 0.9424779 (FAR):  <=0.1 -> 0.10471976  <=0.3 -> 0.15707964
 *                           else  -> 0.18325958
 *       <= 0.9424779 (NEAR):<=0.1 -> 0.06981317  <=0.3 -> 0.10471976
 *                           else  -> 0.122173056
 * which is TURN_IP_GAIT{1,2,03}, TURN_DELTA_BAND and the
 * TURN_MV_{NEAR,FAR}_{W,J,R} rows exactly. */
static float player_turn_rate(int gait, float upt, float adelta)
{
    if (upt <= 0.0f) {                      /* TURN-IN-PLACE, by gait */
        if (gait == 2) return TURN_IP_GAIT2;
        if (gait == 1) return TURN_IP_GAIT1;
        return TURN_IP_GAIT03;              /* gait 0 or 3 */
    }
    if (adelta <= TURN_DELTA_BAND) {        /* MOVING, near band */
        if (upt <= 0.1f) return TURN_MV_NEAR_W;
        if (upt <= 0.3f) return TURN_MV_NEAR_J;
        return TURN_MV_NEAR_R;
    }
    if (upt <= 0.1f) return TURN_MV_FAR_W;  /* MOVING, far band */
    if (upt <= 0.3f) return TURN_MV_FAR_J;
    return TURN_MV_FAR_R;
}

/* player_turn_toward — func_001B12B0 turn-toward: ease g.yaw toward the
 * desired world heading by at most `rate` (rad/frame), snapping when
 * within one step (|delta| <= rate) so there is no overshoot / jitter.
 * Updates g.yaw in place and wraps it to [-pi, pi]. */
void player_turn_toward(float desired, float rate)
{
    float diff = desired - g.yaw;
    while (diff >  EM_PI) diff -= 2.0f * EM_PI;
    while (diff < -EM_PI) diff += 2.0f * EM_PI;
    if (diff <= rate && diff >= -rate) {
        g.yaw = desired;                    /* SNAP — within one step */
    } else {
        g.yaw += (diff > 0.0f) ? rate : -rate;
    }
    while (g.yaw >  EM_PI) g.yaw -= 2.0f * EM_PI;
    while (g.yaw < -EM_PI) g.yaw += 2.0f * EM_PI;
}

/* Horizontal azimuth of the prior frame's committed camera forward.
 * This convenience value uses atan2(X,Z). The original D008106A0 has a
 * different camera basis, atan2(-Z,X), computed by em_player_stick_heading.
 * Both original and native body yaw move along (sin(yaw),cos(yaw)); no
 * conversion is applied to the resulting desired body heading. */
float player_move_cam_yaw(void)
{
    float fx=g.cam.fwd[0], fz=g.cam.fwd[2];
    if (fx*fx+fz*fz>=1e-6f) return atan2f(fx,fz);
    float dx=g.cam.tgt[0]-g.cam.eye[0];
    float dz=g.cam.tgt[2]-g.cam.eye[2];
    if (dx*dx+dz*dz<1e-6f) return g.cam.yaw;
    return atan2f(dx,dz);
}

static float player_stick_desired_yaw(const EmFrameInput *in)
{
    float fx=g.cam.fwd[0], fz=g.cam.fwd[2];
    if (fx*fx+fz*fz<1e-6f) {
        /* Host initialization fallback before the first camera commit. */
        float yaw=player_move_cam_yaw();
        fx=sinf(yaw);
        fz=cosf(yaw);
    }
    return em_player_stick_heading(in->lx,in->ly,fx,fz);
}

/* ---- WP-15/H11 reversal skid (docs/PLAYER_REVERSAL.md) -------------------
 * 00174AC0 arms +1F0=7 when walking faster than 0.5 with gait >= 2 and a
 * wrapped heading error beyond 3pi/4. 0017C030 case 7 requests the turn
 * clip and plays 0x137; case 6 waits for the clip end, then requests the
 * follow-up clip and turns the body by pi. 001612D0 case 2 emits the
 * surface effect every eighth tick and resumes or returns to idle. The
 * pure logic is em_player_reversal.c; this block binds its workers to the
 * port's player source, audio and turn, and keeps the callback's +6/+28. */
#define REVERSAL_DISPLAY_BONES 64

static struct {
    uint8_t walk_state;      /* +6: 2 while 001612D0 case 2 owns the callback */
    uint16_t ticks;          /* +28 */
    uint8_t expect_mode;     /* +1F0 left by the last reversal callback */
    unsigned expect_clip;    /* source clip left by the last reversal callback */
    int display;             /* the reversal clip owns the displayed pose */
    unsigned display_blend;  /* 001749A0 blend of the displayed request */
    unsigned requested_clip;
    float requested_frame;
    unsigned faults;
    int display_bound;       /* em_player_frame calls player_reversal_palette */
    int (*effect)(void *context, uint32_t id, const float position[3], float yaw);
    void *effect_context;
    float from[REVERSAL_DISPLAY_BONES * 16]; /* actor-space pose at the request */
} reversal;

void player_reversal_set_effect_worker(int (*worker)(void *context, uint32_t id,
                                                      const float position[3], float yaw),
                                       void *context)
{
    reversal.effect = worker;
    reversal.effect_context = context;
}

void player_reversal_bind_display(int bound)
{
    reversal.display_bound = bound != 0;
}

/* The skid engages only once every original worker it reaches is bound:
 * the display stage (player_reversal_palette), the 001EFD90 surface-effect
 * worker, and the variant clips 6/7 used by 0017C030 case 7. Until then the
 * ordinary 00174AC0 turn runs, as before WP-15, instead of a live path that
 * faults on every reversal. The logic itself stays oracle-tested
 * (test-player-reversal-reference); binding is the coordinator's job. */
static int reversal_ready(void)
{
    return reversal.display_bound && reversal.effect &&
           em_model_clip_index(&g.model, 6) >= 0 &&
           em_model_clip_index(&g.model, 7) >= 0;
}

/* A callback left the idle/walk family: the port's locomotion stops owning
 * the player (its +1F0 is the mirror's); the state callback runs next. */
static void port_park(void)
{
    g.loco_mode = em_live_u8(&live.a, 0x1F0);
    g.loco_substate = g.loco_tier = 0;
    g.loco_upt = g.move_speed = 0;
    g.loco_entry_ticks = 0;
    g.loco_stop.phase = g.loco_reentry.phase = 0;
    reversal.walk_state = 0;
    reversal.display = 0;
}

unsigned player_reversal_faults(void)
{
    return reversal.faults;
}

static int reversal_request_common(int clip, float frame, float blend, int force)
{
    /* The port's source request takes whole blend ticks (0/4 here). */
    if (clip < 0 || !(blend >= 0.0f) || (float)(unsigned)blend != blend) return -1;
    if ((unsigned)blend && g.model.bone_count <= REVERSAL_DISPLAY_BONES) {
        /* Freeze the displayed pose in actor space, as the re-entry blend
         * does in em_player_frame.c. Position and yaw are still the values
         * the last display used: the skid request precedes translation. */
        unsigned count = g.model.bone_count * 16;
        memcpy(reversal.from, g.player_palette, count * sizeof(float));
        for (unsigned bone = 0; bone < g.model.bone_count; ++bone)
            for (unsigned axis = 0; axis < 3; ++axis)
                reversal.from[bone * 16 + 12 + axis] -= g.pos[axis];
        const float origin[3] = {0, 0, 0};
        palette_apply_placement(reversal.from, g.model.bone_count, origin, -g.yaw);
    }
    player_pose_request((unsigned)clip, frame, (unsigned)blend, force);
    unsigned current;
    if (!player_pose_source(&current, NULL, NULL, NULL) || current != (unsigned)clip)
        return -1;   /* e.g. the source bank lacks the clip (request invalidated it) */
    reversal.display = 1;
    reversal.display_blend = (unsigned)blend;
    reversal.requested_clip = (unsigned)clip;
    reversal.requested_frame = frame;
    return 0;
}

static int reversal_request(void *context, int clip, int force, float blend)
{
    (void)context;
    return reversal_request_common(clip, 0.0f, blend, force);   /* 001749A0 */
}

static int reversal_arbiter(void *context, int clip, float blend, float frame)
{
    (void)context;
    return reversal_request_common(clip, frame, blend, 1);      /* 001749F0 */
}

static int reversal_clip_frames(void *context, int clip, int *frames)
{
    (void)context;
    /* 001C61D0 reads the bank clip header length; the exported model clip
     * carries the same frame count (walk 120, jog 45, run 40). */
    int index = clip >= 0 ? em_model_clip_index(&g.model, (uint32_t)clip) : -1;
    if (index < 0 || !frames) return -1;
    *frames = (int)g.model.clips[index].frame_count;
    return 0;
}

static int reversal_sound(void *context, unsigned id)
{
    (void)context;
    /* 001FB9F0(id, 0x1000, 0x1000, 0x1000). The call is made; whether the
     * cue is exported is the audio bank's state (WP-14), reported once. */
    static int reported;
    if (!reported && em_sfx_cue_state(id) == 0) {
        reported = 1;
        fprintf(stderr, "player reversal: sound 0x%X has no exported cue\n", id);
    }
    em_sfx_play(id);
    return 0;
}

static int reversal_effect(void *context, uint32_t id)
{
    (void)context;
    /* 001EFD90(id, +B0, +C0) spawns the class-0xC effect actor whose
     * behaviour is 001EA240. No native worker exists yet: fault. */
    if (!reversal.effect) return -1;
    return reversal.effect(reversal.effect_context, id, g.pos, g.yaw);
}

static int reversal_turn(void *context, float desired)
{
    /* 00174AC0 arg1==1 turn: the port's banded rate (player_turn_rate) and
     * 001B12B0 step, keyed on the scalar speed before this callback. */
    EmPlayerReversalActor *actor = context;
    float difference = em_player_reversal_wrap(desired - actor->yaw);
    g.yaw = actor->yaw;
    player_turn_toward(desired, player_turn_rate(actor->gait, actor->speed,
                                                 fabsf(difference)));
    actor->yaw = g.yaw;
    return 0;
}

static void reversal_actor(EmPlayerReversalActor *actor, int gait)
{
    memset(actor, 0, sizeof *actor);
    actor->speed = g.loco_upt;
    actor->yaw = g.yaw;
    actor->target = kLocoTierSpeed[gait & 3];
    actor->rate = g.loco_rate;
    actor->blend = g.loco_blend;
    actor->player_state = 1;               /* only walk callbacks reach here */
    actor->walk_state = reversal.walk_state ? reversal.walk_state : 1;
    actor->ticks = reversal.ticks;
    actor->mode = g.loco_mode;
    actor->variant = g.loco_substate;
    actor->tier = (uint8_t)g.loco_tier;
    actor->gait = (uint8_t)gait;
    /* +235 row: bit 0 is the low-health latch (health <= 35); bit 1 is the
     * low clearance 001764E0/001756E0 keep with +236 (rows 2/3 have no
     * exported clips, so the clip lookup refuses them). D_008106C8 bit 2
     * (the 0017B490 row-4 override) is clear in every captured AREA11 state. */
    actor->row = (uint8_t)((g.status.health <= PD_LOW_HEALTH ? 1 : 0) |
                           (g.probe_low_clearance ? 2 : 0));
    actor->special = g.probe_low_clearance;
    actor->global_mode = 0;
    /* +23A: the floor attribute 00175900 stores each callback (probed at
     * the current position). +23C/+23D water depth states are untranslated
     * (zero); they matter only off surfaces 5/6. */
    actor->surface = footstep_floor_attr();
    actor->obstruction = g.probe_block_mask;
}

static void reversal_commit(const EmPlayerReversalActor *actor)
{
    g.yaw = actor->yaw;
    g.loco_upt = actor->speed;
    g.loco_rate = actor->rate;
    g.loco_blend = actor->blend;
    g.loco_mode = actor->mode;
    g.loco_substate = actor->variant;
    g.loco_tier = actor->tier;
    reversal.walk_state = actor->walk_state;
    reversal.ticks = actor->ticks;
    reversal.expect_mode = actor->mode;
    reversal.expect_clip = reversal.requested_clip;
}

static const EmPlayerReversalWorkers kReversalWorkers = {
    NULL, reversal_request, reversal_arbiter, reversal_clip_frames,
    reversal_sound, reversal_effect, reversal_turn
};

static void reversal_fault(const char *where)
{
    /* A reached missing worker, or a source that cannot carry the skid
     * clips. Visible and not simulated: the port abandons the skid and
     * recovers through this file's unsupported-path hold. */
    ++reversal.faults;
    fprintf(stderr, "player reversal: worker fault at frame %d in %s\n",
            g.frame_no, where);
    reversal.walk_state = 0;
    reversal.display = 0;
    g.loco_mode = g.loco_substate = 0;
    g.loco_tier = 0;
    g.loco_upt = g.move_speed = 0;
    player_pose_unsupported_hold("reversal skid worker fault");
}

/* The saved +6 is valid only while nothing else has rewritten the scalar
 * mode or the source clip since the last reversal callback (stand-ins,
 * hits, Use acceptance and releases reset both). */
static int reversal_state2_live(void)
{
    if (reversal.walk_state != 2) return 0;
    unsigned clip;
    if (g.loco_mode == reversal.expect_mode &&
        player_pose_source(&clip, NULL, NULL, NULL) && clip == reversal.expect_clip)
        return 1;
    reversal.walk_state = 0;
    reversal.display = 0;
    /* Another owner that reset +1F0 ended the skid, as its original state
     * change does. A skid mode left behind without its clip cannot finish. */
    if (g.loco_mode == 6 || g.loco_mode == 7)
        reversal_fault("reversal state lost its source clip");
    return 0;
}

int player_reversal_owns_walk(void)
{
    return reversal.walk_state == 2;
}

/* 001612D0 case 2 (after 001607D0; its 00184BA0 door scan is em_door's).
 * Returns 1 when the caller performs 00178B90(p,0) and the tail, 0 when a
 * fault already ended the callback. */
static int reversal_state2_tick(const EmFrameInput *in, int gait)
{
    EmPlayerReversalActor actor;
    reversal_actor(&actor, gait);
    unsigned flags;
    if (!player_pose_source(NULL, NULL, &flags, NULL)) {
        reversal_fault("001612D0 case 2 (source clip flags unavailable)");
        return 0;
    }
    actor.anim_flags = flags;
    float desired = gait ? player_stick_desired_yaw(in) : 0.0f;
    EmPlayerReversalWorkers workers = kReversalWorkers;
    workers.context = &actor;
    if (em_player_reversal_state2(&actor, desired, &workers) < 0) {
        reversal_fault("001612D0 case 2");
        return 0;
    }
    reversal_commit(&actor);
    if (actor.player_state == 0) {
        /* +5=0/+6=0: 00161020 case 0 requests idle on the next callback.
         * The run-stop return models exactly that hand-off (phase 3). */
        int index = em_model_clip_index(&g.model, reversal.requested_clip);
        reversal.walk_state = 0;
        reversal.display = 0;
        if (index >= 0) {
            g.loco_stop_clip = index;
            g.loco_stop.frame = 0;
        }
        g.loco_stop.phase = 3;
        g.loco_upt = g.move_speed = 0;
    } else if (actor.walk_state == 1) {
        /* Resumed walking at gait-1: the ordinary display continues from
         * the requested source frame. 001612D0 keeps the 0017B490 clip id
         * in $s1, which this tick's 001764E0 inherits. */
        probe.inherited_s1 = (uint8_t)reversal.requested_clip;
        int index = em_model_clip_index(&g.model, reversal.requested_clip);
        reversal.walk_state = 0;
        reversal.display = 0;
        if (index >= 0) {
            g.loco_clip = index;
            g.walk_t = (double)reversal.requested_frame / g.model.clips[index].fps;
        }
        g.walk_w = 1;
    }
    g.loco_animation_step = 0;
    return 1;
}

int player_reversal_palette(void)
{
    if (!reversal.display || reversal.walk_state != 2) return 0;
    unsigned clip;
    float remaining;
    int transition;
    /* A stand-in that froze the source owns the display instead. */
    if (!player_pose_source(&clip, &remaining, NULL, &transition)) return 0;
    int index = em_model_clip_index(&g.model, clip);
    if (index < 0 || g.model.bone_count > REVERSAL_DISPLAY_BONES) return -1;
    const EmModelClip *model_clip = &g.model.clips[index];
    double frame = 0;
    float weight = 1;
    if (transition && reversal.display_blend) {
        weight = 1.0f - remaining / (float)reversal.display_blend;
    } else {
        frame = (double)model_clip->frame_count - remaining;
        if (frame < 0) frame = 0;
        if (frame > model_clip->frame_count - 1) frame = model_clip->frame_count - 1;
    }
    em_model_palette_at(&g.model, (uint32_t)index, frame, g.player_palette);
    if (weight < 1) {
        unsigned count = g.model.bone_count * 16;
        for (unsigned i = 0; i < count; ++i)
            g.player_palette[i] = reversal.from[i] +
                (g.player_palette[i] - reversal.from[i]) * weight;
    }
    palette_apply_placement(g.player_palette, g.model.bone_count, g.pos, g.yaw);
    return 1;
}

/* Player movement (the port's first slice of the actor spine's physics
 * side): left stick = camera-relative DESIRED heading on the XZ plane;
 * the body heading g.yaw EASES toward it at the engine's banded turn
 * rate (func_00174AC0 / func_001B12B0) and velocity is emitted ALONG
 * g.yaw — so the path curves into the move direction (the body lags the
 * stick) instead of sliding off along the raw stick instantly. With a
 * collision world loaded, movement goes through the engine's move probe
 * (walls stop/slide, the floor query sets the height); without one, the
 * old room-bbox clamp keeps the repo runnable standalone. */
static void player_move_callbacks(void);

void player_move(void)
{
    /* 0015BA50 before its switch (once per frame; see em_player.h), the
     * callbacks, then 0015BA50's tail and 0015BCF0's writes after it. */
    player_states_stage_begin();
    player_move_callbacks();
    player_states_stage_end();
}

static void player_move_callbacks(void)
{
    /* 0015BA50 advances animation with the previous +204 output, then
     * resets that one-shot multiplier before the locomotion callback. */
    g.loco_animation_step = g.loco_mode ? g.loco_rate : 0;
    g.loco_rate = 1;
    g.gait = 0;          /* re-quantized below; scripted paths leave 0 */

    /* DOOR TRANSIT (the engine's gameplay-frame selector 3, spad
     * 0x70003B8D, armed by the use scan): a scripted MOVE-TO carries
     * the player to the door's far-side point with yaw snapped to the
     * door normal (func_001BBE40 -> func_00182F90). Runs collision-free
     * — the doorways are statically sealed by the grid room-boundary
     * planes, and this scripted move is exactly how the engine crosses
     * them. Stick input is ignored while it runs (the selector-3 frame
     * variant does not run the free-move spine). */
    {
        float tt[3], tyaw;
        if (em_door_transit_active(tt, &tyaw)) {
            player_pose_legacy_hold("legacy door transit source is not recovered");
            float dx   = tt[0] - g.pos[0];
            float dz   = tt[2] - g.pos[2];
            float len  = sqrtf(dx * dx + dz * dz);
            float step = WALK_SPEED * FRAME_DT;
            g.move_speed = WALK_SPEED;     /* drive the walk clip */
            g.loco_tier  = 1;              /* scripted walk = tier-1 clip */
            g.loco_upt   = 0.0f; g.loco_mode = 0; g.loco_entry_ticks = 0; g.loco_stop.phase = 0; g.loco_reentry.phase = 0;           /* free-move ramp re-arms */
            g.yaw        = tyaw;
            if (len <= step || len < 1e-6f) {
                g.pos[0] = tt[0];
                g.pos[2] = tt[2];
            } else {
                g.pos[0] += dx / len * step;
                g.pos[2] += dz / len * step;
            }
            return;
        }
    }

    /* ARRIVAL WALK-OUT (engine player state 5/1, func_00183250 —
     * em_door.h step 4).
     * PROVENANCE (audit): func_00183250 is byte-matched but exists in
     * the decomp ONLY as an asm-void .word body (src/func_00183250.c) —
     * there is no recovered C, so the frame counts and speeds below
     * cannot be re-checked against it. Treat them as OBSERVED /
     * port stand-in until that function gets a readable decompilation,
     * not as source-derived constants.
     * After the re-place the
     * player UNINTERRUPTIBLY walks out through the door along the exit
     * yaw — 50 frames of clip-in-place (mostly under the fade-in), 30
     * frames at the locIdx-2 speed (0.3 u/tick), 30 frames decaying to
     * a stop (~12.8 u total). The stick is never read (state 5 has no
     * free-move spine); em_door owns the phases, this consumes the
     * per-frame command. Collision-free like the transit MOVE-TO (the
     * engine runs its own mover in the destination area's geometry). */
    {
        float wyaw, wspeed;
        if (em_door_walkout_active(&wyaw, &wspeed)) {
            player_pose_legacy_hold("legacy door arrival source is not recovered");
            g.yaw        = wyaw;
            /* Drive the locomotion clip at the engine's commanded tier:
             * the walk-out plays the locIdx-2 clip (family 0 -> id 2 =
             * JOG, 0.3 u/tick) even during the in-place phase. */
            g.move_speed = wspeed > 0.0f ? wspeed : GAIT_JOG_SPEED;
            g.loco_tier  = 2;
            g.loco_upt   = 0.0f; g.loco_mode = 0; g.loco_entry_ticks = 0; g.loco_stop.phase = 0; g.loco_reentry.phase = 0;           /* free-move ramp re-arms */
            g.pos[0] += sinf(wyaw) * wspeed * FRAME_DT;
            g.pos[2] += cosf(wyaw) * wspeed * FRAME_DT;
            return;
        }
    }

    /* MOVEMENT LOCK (door transit — the two-lock split described in
     * em_door.h "THE TWO LOCKS"). DOWNGRADED (audit 2026-07-31): this
     * used to read "the decoded two-lock split", but the comment cites
     * no function and the split is not read out of any recovered C in
     * the decomp — it is the PORT'S OWN model of the door sequence,
     * built from observation. Treat it as port structure, not as
     * source-derived. Behaviour left alone (no counter-evidence).
     * Span: kickoff -> walk-out end. Free
     * movement is ignored — the player walks the scripted MOVE-TO /
     * walk-out above or stands (at the staging point). The MENU lock
     * is separate (em_door_menu_locked, consumed by em_hud) and ends
     * earlier, at fade-in completion. */
    if (em_door_movement_locked()) {
        player_pose_legacy_hold("legacy door interaction source is not recovered");
        g.move_speed = 0.0f;
        g.loco_tier  = 0;          /* scripted mode exits locomotion:
                                    * re-entry re-arms the tier ramp */
        g.loco_upt   = 0.0f; g.loco_mode = 0; g.loco_entry_ticks = 0; g.loco_stop.phase = 0; g.loco_reentry.phase = 0;
        return;
    }

    /* EXAMINE SEQUENCE LOCK (em_examine.h): the examine script's op07
     * sub2 enters scripted mode for the whole sequence — free locomotion
     * is suppressed (the player stands), but the sequence DOES drive the
     * body heading itself: the op04 FACE pre-roll pivots the player to the
     * scripted yaw at the standing turn rate via em_game_player_face_step
     * (which calls player_turn_toward on g.yaw) BEFORE the message. The
     * engine's op01 walk-to is duration-0 for the snow terminals (the
     * use-scan already places the player within dist), so no scripted
     * translation here — only the FACE turn. We must NOT zero/seed the
     * heading here: this lock only kills move_speed/tier/upt so the floor
     * solve and anim are stand-still, while the FACE phase owns g.yaw.
     * Same lock shape as the door transit above. */
    if (em_examine_input_locked()) {
        player_pose_legacy_hold("legacy examine source is not recovered");
        g.move_speed = 0.0f;
        g.loco_tier  = 0;
        g.loco_upt   = 0.0f; g.loco_mode = 0; g.loco_entry_ticks = 0; g.loco_stop.phase = 0; g.loco_reentry.phase = 0;
        return;
    }

    /* SCRIPTED INTERACT / ELEVATOR RIDE LOCK (CORRECTED two-terminal
     * flow, INVESTIGATION_area11_elevator.md). Two cases share one
     * stand-still lock — the engine's "scripted-anim-owns-player" state
     * (player+0x2F3 = 3) and the powered descent both suppress free
     * movement so the script owns the player:
     *   - elev_state == 1: the platform is descending — the player
     *     stands on it and is carried DOWN by the descent's direct Y
     *     drive (elevator_tick, after actor_update), not by free
     *     movement; standing here keeps the floor-snap above from
     *     fighting that write to g.pos[1].
     * em_game_player_interact_busy() reports both as busy so the examine
     * logic does not double-trigger. FLAGGED / DOWNGRADED (audit
     * 2026-07-31): the "the live decode confirmed D_008101E4 stays 0"
     * line used to read as a decode result. It is not one — it is an
     * OBSERVATION from a PCSX2 session, and this comment cites no
     * function. D_008101E4 does appear across the decomp corpus, but
     * nothing recovered ties it to this lock either way, so the
     * stand-still lock below stands as a PORT STAND-IN for whatever
     * control-mode write the original does here, not as a mirror of it.
     * Behaviour left alone. elev_pending (the descent armed,
     * waiting on the lever clip) is also locked so the one frame between
     * the lever clip ending and the ride beginning (both resolved later
     * this same frame in elevator_tick) does not leak free movement —
     * exactly em_game_player_interact_busy()'s condition. */
    if (em_game_player_interact_busy()) {
        player_pose_legacy_hold("legacy interaction source is not recovered");
        g.move_speed = 0.0f;
        g.loco_tier  = 0;
        g.loco_upt   = 0.0f; g.loco_mode = 0; g.loco_entry_ticks = 0; g.loco_stop.phase = 0; g.loco_reentry.phase = 0;
        return;
    }

    /* R2-HELD ARMED STANCE 0x1E (decoded 2026-06-11: the action machine
     * func_001607D0 dispatches HELD R2 -> player mode 0x1E, action code
     * 0x32 — the engine's SECOND aim stance, sharing the mode-1 aim
     * camera through player states 0x2A/0x29; its laser is the
     * DOT-only drawer func_001854E0 and it has no fire-counter recoil
     * — both stay with em_weapon, noted there as pending). em_game
     * runs its planted pose + steer + camera side: the held aim pose
     * through its own anim mailbox.
     *
     * STANCE PRIORITY — CONFIRMED, and the port now matches (audit
     * 2026-07-31; an earlier pass left a stale "recorded, not fixed"
     * note here after the fix had already landed). Re-read in the
     * recovered src/func_001607D0.c (NEARMISS — logic authoritative):
     * R2 OUTRANKS R1 in three places.
     *   - case 0x00 and cases 0x01..0x07 both test the R2 config mask
     *     `D_00810E70 & *(u16 *)0x70003B7E` BEFORE the R1 mask
     *     `... & *(u16 *)0x70003B7C`, and the R2 arm returns.
     *   - case 0x31 (the R1 stance): `if (D_00810E70 & *(u16*)0x70003B7E)
     *     { self[5] = 0x1E; self[0x1F0] = 0x32; self[0x318] = 1; }` —
     *     it switches the frame R2 goes down.
     *   - case 0x32 (the R2 stance) only falls back with
     *     `if (!(D_00810E70 & 0x70003B7E)) { if (D_00810E70 &
     *     0x70003B7C) { self[5] = 0x1D; self[0x1F0] = 0x31; } }` — R2
     *     must be RELEASED first.
     * The `want` gate below therefore does NOT defer to R1; the matching
     * R1 suppression lives in em_weapon.c (`draw_held` requires
     * `(held & EM_PAD_R2) == 0`), which is that file's business. */
    {
        const EmFrameInput *rin = em_frame_input();
        /* AUDIT CORRECTION (round 3 follow-up): R2 OUTRANKS R1. The
         * !em_weapon_is_aiming() term used to sit here, which gave R1
         * priority — the exact inversion of func_001607D0, whose stance
         * dispatcher tests the R2 mask BEFORE the R1 mask and whose case
         * 0x31 (R1 stance) switches to 0x1E/0x32 the moment R2 is held.
         * R1 is now suppressed while R2 is held, weapon-side, so this gate
         * no longer has to defer to it. Melee still wins over both — the
         * engine's melee states are a separate family this dispatcher is
         * not reached from. */
        int want = (rin->held & EM_PAD_R2) && !em_weapon_is_melee();
        if (want && !g.r2_aim)
            em_game_anim_hold(0x112, 1.0f);
        else if (!want && g.r2_aim && em_game_anim_active() == 0x112)
            em_game_anim_cancel();
        g.r2_aim = want;
    }

    /* PLANTED AIMING + MANUAL AIM STEER (func_0017ABA0 — the decoded
     * constants block above; retires the old AIM_TURN_SPEED turn-in-
     * place stand-in). The armed stance holds position (engine: the
     * armed modes 0x1D..0x20 replace the locomotion modes outright —
     * no aim-walk clips, zero footstep frames); the stick bytes (with a
     * held D-pad already folded in by 001B5940/001B5E20) steer the aim
     * BLENDS: pitch INVERTED-Y, yaw panning
     * the +-60 deg pose ladder first and turning the body only past
     * the blend limit.
     *
     * CONFIRMED (audit 2026-07-31) against src/func_0017ABA0.c (NEARMISS
     * — logic authoritative) and its two helpers. Everything the block
     * below implements reads out of that file literally: the two rate
     * rows and their f20 body multipliers, the `pitch == 0.5f -> 1.0f`
     * special case with `sin(pi * (0.5 +- 0.6*|pitch-0.5|))` either side
     * (func_0011E2A8 = sinf; both arms collapse to the single
     * `0.5 + 0.6*(pitch-0.5)` the port uses), the `step / 2.0f` on the
     * pitch axis, the `pitch <= 0.3f || !(pitch < 0.7f)` 1.5x band that
     * only applies in the 0x31/0x34 arm, the +0x27C overflow paying the
     * excess into the body heading via func_001B1470 (wrap), and the
     * manual-steer flag +0x302 = 1 that drops the target lock (the port
     * spells that as "run the lock steer only when both bands are 0").
     * The deflection bands are src/func_001B5DC0.c verbatim:
     * `abs(raw-0x80) < 0x31 -> 0, < 0x59 -> 1, < 0x7B -> 2, else 3`
     * = the AIM_BAND_1/2/3 49/89/123 the port uses.
     * The pitch clamp is `lim = ((u32)(st2 - 0x31) < 2U) ? 1.0f : 0.75f`,
     * i.e. 1.0 for stances 0x31/0x32 and 0.75 for 0x34/0x35; the port
     * models only 0x31/0x32, so the constant AIM_PITCH_MAX_R1 (1.0f) is
     * correct for every stance it can be in.
     * NOT MODELLED (flag): the engine also scales the step when the actor
     * byte +0x275 == 4 — `step *= 1.5f` on yaw, `step *= 1.8f` on pitch.
     * The port has no +0x275 state to key that on, so it is omitted, not
     * decided against.
     * NOTE on sign convention: the engine's +0x27C RISES toward 1.0 on
     * stick-right; the port's g.aim_yawb FALLS toward 0.0 (0.0 = the
     * screen-right column, see aim_ladder_eval / aim_dir_get). The two
     * are mirror-image encodings of the same blend, and both turn the
     * body the SAME way (heading decreasing) once the blend saturates,
     * so the resulting aim is identical. Left as-is deliberately: the
     * port's 0 = right convention is threaded through the pose ladder
     * and aim_dir_get, and the engine's own ladder mapping lives in
     * anim_slot_index/D_00248B70, which is not recovered. */
    {
        int aim_now = em_weapon_is_aiming() || g.r2_aim;
        if (aim_now && !g.aim_was) {
            /* STANCE ENTRY (func_0016F600 family): the aim blends reset
             * to center and the entry position is saved (D_70003040 —
             * the R2 state-0x2A camera target base), and the camera
             * arms its mode-1 entry phase. */
            g.aim_pitch = 0.5f;
            g.aim_yawb  = 0.5f;
            memcpy(g.cam.aim_entry, g.pos, sizeof g.cam.aim_entry);
            g.cam.aim_phase = 1;
        } else if (!aim_now && g.aim_was) {
            /* RELEASE (player states 0xC/0x29 -> camera mode 2): the
             * port re-seeds the chase yaw from the actual eye->player
             * heading (the engine's .L001935EC reset) and lets the
             * mode-0 chase blend back. */
            g.cam.aim_phase = 0;
            g.cam.yaw = atan2f(g.pos[0] - g.cam.eye[0],
                               g.pos[2] - g.cam.eye[2]);
        }
        g.aim_was = aim_now;
    }
    if (em_weapon_is_aiming() || g.r2_aim) {
        player_pose_legacy_hold("aim node adjustments are not bound");
        g.move_speed = 0.0f;
        g.loco_tier  = 0;          /* armed modes replace locomotion */
        g.loco_upt   = 0.0f; g.loco_mode = 0; g.loco_entry_ticks = 0; g.loco_stop.phase = 0; g.loco_reentry.phase = 0;
        const EmFrameInput *ain = em_frame_input();
        int r1fam = !g.r2_aim || em_weapon_is_aiming(); /* stance 0x31 */

        /* 0017ABA0 reads the pad bytes D_00810E64/65 directly. Their
         * producer 001B5940 already maps a held D-pad onto them (001B5E20)
         * before any consumer runs (em_frame step C), so no separate
         * D-pad path exists here. */
        int rawx = ain->lx, rawy = ain->ly;

        /* func_001B5DC0 deflection bands + the per-stance rate rows.
         * Both rows read literally out of src/func_0017ABA0.c
         * (NEARMISS — logic authoritative): the 0x31/0x34 arm is
         * {0, 0.0025f, 0.005f, 0.015f} with f20 = 1.0f, the else arm
         * (the R2 family 0x32/0x35) is {0, 0.0016666666f, 0.005f,
         * 0.01f} with f20 = 1.5f.
         * CORRECTED (audit 2026-07-31): kRateR2[3] was 0.015f — a
         * transcription of the R1 row's top band. The recovered C
         * reads `rate[3] = 0.01f` in that arm, so the port panned the
         * R2 aim 50% too fast at full stick deflection. */
        static const float kRateR1[4] = { 0.0f, 0.0025f, 0.005f, 0.015f };
        static const float kRateR2[4] = { 0.0f, 0.0016666666f, 0.005f,
                                          0.01f };
        const float *rate = r1fam ? kRateR1 : kRateR2;
        float body_mul = r1fam ? 1.0f : 1.5f;   /* f20 */
        int bx = abs(rawx - 0x80), by = abs(rawy - 0x80);
        int bandx = bx < AIM_BAND_1 ? 0 : bx < AIM_BAND_2 ? 1
                  : bx < AIM_BAND_3 ? 2 : 3;
        int bandy = by < AIM_BAND_1 ? 0 : by < AIM_BAND_2 ? 1
                  : by < AIM_BAND_3 ? 2 : 3;

        /* YAW (+0x27C): rate scaled by 1/sin(pi*(0.5+0.6*(p-0.5))) —
         * exactly 1.0 at pitch center (the engine special-cases the
         * equality), faster pitched off level. Overflow past [0,1]
         * turns the body by the excess (screen-right = yaw decreasing
         * in the port basis, matching the engine's heading -=). */
        if (bandx) {
            float p = g.aim_pitch;
            float s = (p == 0.5f) ? 1.0f
                    : sinf(EM_PI * (0.5f + 0.6f * (p - 0.5f)));
            float step = rate[bandx] / s;
            if (rawx >= 0x80) {            /* stick RIGHT */
                g.aim_yawb -= step;        /* toward the right poses */
                if (g.aim_yawb < 0.0f) {
                    g.yaw -= -g.aim_yawb * body_mul;
                    g.aim_yawb = 0.0f;
                }
            } else {                       /* stick LEFT */
                g.aim_yawb += step;
                if (g.aim_yawb > 1.0f) {
                    g.yaw += (g.aim_yawb - 1.0f) * body_mul;
                    g.aim_yawb = 1.0f;
                }
            }
            while (g.yaw >  EM_PI) g.yaw -= 2.0f * EM_PI;
            while (g.yaw < -EM_PI) g.yaw += 2.0f * EM_PI;
        }

        /* PITCH (+0x278): INVERTED Y — stick DOWN (raw >= 0x80) raises
         * the blend = aim UP; stick UP aims DOWN ("W = down"). The R1
         * family speeds up 1.5x outside [0.3, 0.7]. */
        if (bandy) {
            float mult = (r1fam && (g.aim_pitch <= 0.3f ||
                                    g.aim_pitch >= 0.7f)) ? 1.5f : 1.0f;
            float step = rate[bandy] * mult * 0.5f;
            if (rawy >= 0x80) {            /* stick DOWN -> aim UP */
                g.aim_pitch += step;
                if (g.aim_pitch > AIM_PITCH_MAX_R1)
                    g.aim_pitch = AIM_PITCH_MAX_R1;
            } else {                       /* stick UP -> aim DOWN */
                g.aim_pitch -= step;
                if (g.aim_pitch < 0.0f) g.aim_pitch = 0.0f;
            }
        }

        /* LOCK STEER (func_0017AF70 — em_weapon.h "TARGET LOCK"):
         * with the stick idle and a target in lock slot 0, em_weapon
         * creeps the blends toward the lock at <= 0.02/frame. The
         * stick-idle gate is the engine's manual-input lock drop (the
         * 0x1D stance clears D_008106E0 whenever func_0017ABA0 flags
         * manual steering, +0x302) — the player's hand always wins. */
        if (!bandx && !bandy) {
            float lp, ly;
            if (em_weapon_lock_steer(g.pos, g.yaw, g.aim_pitch,
                                     g.aim_yawb, &lp, &ly)) {
                g.aim_pitch = lp;
                g.aim_yawb  = ly;
            }
        }
        return;
    }

    /* KNIFE / MELEE plant: the engine's melee modes 0x21/0x22 replace
     * the locomotion modes outright (em_weapon.h "KNIFE / MELEE") —
     * the player stands for the swing + recover. The heavy's in-swing
     * yaw steer (func_00173DD0, D_002486F0 rates) is untranslated
     * (flagged in em_weapon.h), so no turn-in-place here either. */
    if (em_weapon_is_melee()) {
        player_pose_legacy_hold("melee source channels are not exported");
        g.move_speed = 0.0f;
        g.loco_tier  = 0;          /* melee modes replace locomotion */
        g.loco_upt   = 0.0f; g.loco_mode = 0; g.loco_entry_ticks = 0; g.loco_stop.phase = 0; g.loco_reentry.phase = 0;
        return;
    }

    /* WP-2/H12: every stand-in above has released this frame. Re-seed the
     * frozen source at its row default (00182DF0 via 001C63E0); the hit,
     * scripted-clip and low-health holds are rechecked by the host. */
    (void)player_pose_legacy_release();

    /* Live player states: a (+4, +5) outside the idle/walk family (entered
     * by 001796C0, 0015BCF0's -200 check or the Use chain) runs the
     * translated stage and its bound callbacks instead of the port's
     * (0015BA50's switch). Only reachable once engaged. */
    if (floor_engaged() && live_stage_dispatch()) return;

    /* WP-15/H11: while 001612D0 case 2 owns the callback (skid ticks and
     * the resume/idle decision), 00160220 is not polled; only case 1 does. */
    const int reversal_live = reversal_state2_live();
    int walking_before_use = g.loco_mode != 0;
    int used = reversal_live ? 0 : player_use_poll();
    if (used != 0) {
        /* 00161020 breaks to the idle physics tail; 001612D0 returns
         * before its walking tail. Save the state before 001798D0 clears
         * it. */
        if (used > 0 && !walking_before_use) player_probe_tail(0);
        return;
    }

    if (!reversal_live &&
        (player_pose_entry_return_tick() || player_pose_idle_state_wait())) {
        g.gait = 0;
        g.move_speed = 0;
        player_probe_tail(0);
        return;
    }

    /* GAIT — 00174AC0/00175390 latch the pad gait byte D_00810E57 into
     * player +0x23F. 001B5940 computes that byte once per frame: the
     * 001B5CC0 rings (48/88/122) on the RAW stick bytes, before lx/ly are
     * quantized, or 3/0 when 001B5E20 maps the D-pad onto the stick. Take
     * it from the translated block; re-deriving it from the quantized
     * bytes misclassifies radii just past a ring (raw 0xB1 -> 0xB0). */
    const EmFrameInput *in = em_frame_input();
    int gait  = em_frame_pad_block()->gait;
    g.gait       = gait;
    g.move_speed = 0.0f;
    unsigned translation_steps = 1;

    if (reversal_live) {
        /* 001612D0 case 2: 00174AC0, the surface effect / resume / idle
         * decision, 0017BC40 and 0017C030, then 00178B90(p,0) and the tail. */
        if (!reversal_state2_tick(in, gait)) {
            player_probe_tail(1);
            return;
        }
        goto locomotion_translate;
    }

    if (player_pose_foot_stop_active()) {
        if (gait) {
            float desired = player_stick_desired_yaw(in);
            float difference = desired - g.yaw;
            while (difference > EM_PI) difference -= 2.0f * EM_PI;
            while (difference <= -EM_PI) difference += 2.0f * EM_PI;
            player_turn_toward(desired, player_turn_rate(gait, 0, fabsf(difference)));
        }
        /* 0017C030 mode 5 ends with +1F0=0, +25C=0 and the step mailbox
         * 0x81 (tier 1) or 0x82; 00187350 plays that step the same frame. */
        unsigned stop_tier = g.loco_tier;
        int stop_active = player_pose_foot_stop_tick();
        if (stop_active < 0)
            player_pose_invalidate("foot-placement stop callback failed");
        else if (stop_active == 0)
            player_footstep_post(stop_tier == 1 ? 0x81 : 0x82);
        player_probe_tail(1);
        return;
    }

    if (g.loco_reentry.phase == 2) g.loco_reentry.phase = 0;
    if (g.loco_reentry.phase == 1) {
        if (gait) {
            float desired = player_stick_desired_yaw(in);
            float diff = desired - g.yaw;
            while (diff > EM_PI) diff -= 2.0f * EM_PI;
            while (diff <= -EM_PI) diff += 2.0f * EM_PI;
            player_turn_toward(desired, player_turn_rate(gait, g.loco_upt, fabsf(diff)));
        }
        EmPlayerMotor current = {.mode=g.loco_mode, .substate=g.loco_substate};
        em_player_reentry_tick(&g.loco_reentry, &current);
        g.loco_mode=current.mode;
        g.loco_substate=current.substate;
        g.loco_animation_step=0;
        goto locomotion_translate;
    }

    if (g.loco_stop.phase) {
        if (g.loco_stop.phase <= 2 && gait > 1) {
            /* 0017C030 mode 4 gives input priority over the stop end flag.
             * 0017C440 chooses gait-1, moves once with argument 1, and
             * requests a four-tick blend. The walk callback then moves
             * again with argument 0. Original frame 4134 confirms both
             * translations. */
            int clip = gait == 3 ? g.clip_jog : g.clip_walk;
            EmPlayerMotor resumed = {.substate=g.loco_substate};
            if (clip >= 0 && em_player_reentry_begin(&g.loco_reentry, &resumed,
                                      gait, g.model.clips[clip].frame_count)) {
                float desired = player_stick_desired_yaw(in);
                player_turn_toward(desired, player_turn_rate(gait, 0, 0));
                g.loco_tier=resumed.tier;
                g.loco_upt=resumed.speed;
                g.loco_mode=resumed.mode;
                g.loco_substate=resumed.substate;
                g.loco_clip=clip;
                player_pose_request(g.model.clips[clip].id,
                                    (float)g.loco_reentry.frame, 4, 1);
                g.walk_t=(double)g.loco_reentry.frame/g.model.clips[clip].fps;
                g.walk_w=1;
                g.loco_animation_step=0;
                g.loco_stop.phase=0;
                translation_steps=2;
                goto locomotion_translate;
            }
        }
        if (g.loco_stop.phase == 4 && gait) {
            /* Idle state accepts a fresh request during its blend. */
            g.loco_stop.phase = 0;
        } else {
            unsigned before = g.loco_stop.phase;
            em_player_stop_tick(&g.loco_stop);
            /* Phase 2 -> 3 is 0017C030 mode 4 seeing the end flag: +1F0=0,
             * +25C=0 and the step mailbox 0x83. */
            if (before == 2 && g.loco_stop.phase == 3)
                player_footstep_post(0x83);
            g.loco_upt = g.move_speed = 0;
            g.loco_animation_step = 0;
            if (g.loco_stop.phase >= 3) {
                g.loco_mode = 0;
                g.loco_substate = 0;
                g.loco_tier = 0;
            }
            if (before == 4 && g.loco_stop.phase)
                --g.idle_timer; /* idle case1 counts during its blend */
            if (before == 3) {
                player_pose_idle_enter();
                g.idle_t = 0;
                g.idle_phase = 0;
                g.idle_timer = IDLE_FIDGET_FRAMES;
                g.fid_w = g.walk_w = 0;
            }
            player_probe_tail(before <= 2);
            return;
        }
    }

    /* Idle 00161020 requests the walk clip with an eight-tick blend at
     * 0017B5C0, then waits for +200 bit 0x8000 before changing to walk state.
     * The request callback itself does not turn or translate. The original
     * first-control trace confirms input at 4086, handoff 4094, motion 4095. */
    if (g.loco_entry_ticks != 0) {
        if (gait) {
            float desired = player_stick_desired_yaw(in);
            player_turn_toward(desired, player_turn_rate(gait, 0, 0));
        }
        int still_turning = 0;
        if (gait == 1) {
            float difference = player_stick_desired_yaw(in) - g.yaw;
            while (difference > EM_PI) difference -= 2.0f * EM_PI;
            while (difference <= -EM_PI) difference += 2.0f * EM_PI;
            still_turning = difference != 0;
        }
        if (g.loco_entry_ticks > 1) --g.loco_entry_ticks;
        else g.loco_entry_ticks = still_turning ? -1 : 0;
        if (g.loco_entry_ticks < 0)
            g.loco_rate = 0; /* 00161020 case2 holds the pose while turning. */
        if (!g.loco_entry_ticks && !gait)
            player_pose_entry_cancel();
        if (g.loco_entry_ticks == 0 && gait) {
            g.loco_mode = 1;
            g.loco_substate = 1;
        }
        player_probe_tail(0);
        return;
    }
    if (g.loco_mode == 0) {
        if (gait) {
            g.loco_entry_ticks = 8;
            g.loco_rate = 1;
            g.loco_blend = 0;
            /* Original bank clip 1 has 120 frames; 0017B5C0 starts at
             * frames - D00248740[0], where the remaining segment is 56. */
            if (g.clip_walk >= 0) {
                const EmModelClip *clip = &g.model.clips[g.clip_walk];
                player_pose_request(1, (float)clip->frame_count - 56, 8, 1);
                g.walk_t = ((double)clip->frame_count - 56.0) / clip->fps;
                g.loco_clip = g.clip_walk;
            }
            g.walk_w = 0;
        }
        player_probe_tail(0);
        return;
    }

    /* 001612D0 calls heading 00174AC0 before scalar motor 0017BC40.
     * Select the turn band using the previous speed. Gait 0 returns before
     * heading work, including small analog nudges inside the deadzone. */
    if (gait) {
        float desired = player_stick_desired_yaw(in);
        /* 00174AC0's reversal gate precedes the turn: speed > 0.5, gait >= 2
         * and |wrap(desired - yaw)| > 3pi/4 arm +1F0=7 and skip the turn. */
        EmPlayerReversalActor gate = {
            .speed = g.loco_upt, .yaw = g.yaw, .player_state = 1,
            .mode = g.loco_mode, .variant = g.loco_substate, .gait = (uint8_t)gait,
        };
        if (!reversal_ready() || em_player_reversal_heading(&gate, desired)) {
            float diff = desired - g.yaw;
            while (diff > EM_PI) diff -= 2.0f * EM_PI;
            while (diff <= -EM_PI) diff += 2.0f * EM_PI;
            player_turn_toward(desired,
                              player_turn_rate(gait, g.loco_upt, fabsf(diff)));
        } else if (gate.mode == 7) {
            g.loco_mode = 7;
            g.loco_substate = gate.variant;
        }
    }
    EmPlayerMotor motor = {
        g.loco_upt, kLocoTierSpeed[gait], g.loco_rate, g.loco_blend,
        g.loco_mode, g.loco_substate, (uint8_t)g.loco_tier,
        (uint8_t)gait, g.probe_block_mask
    };
    em_player_motor_tick(&motor);
    g.loco_mode = motor.mode;
    g.loco_substate = motor.substate;
    g.loco_tier = motor.tier;
    g.loco_upt = motor.speed;
    g.loco_rate = motor.rate;
    g.loco_blend = motor.blend;
    if (motor.mode == 7) {
        /* 0017BC40 leaves mode 7 alone; 0017C030 case 7 requests the turn
         * clip (blend 4) and plays 0x137; 001612D0 case 1 then moves to +6=2
         * with +28=0 after this callback's ordinary translation. */
        EmPlayerReversalActor actor;
        reversal_actor(&actor, gait);
        actor.walk_state = 1;
        EmPlayerReversalWorkers workers = kReversalWorkers;
        workers.context = &actor;
        if (em_player_reversal_animation(&actor, &workers) < 0) {
            reversal_fault("0017C030 case 7");
            player_probe_tail(1);
            return;
        }
        em_player_reversal_walk_tail(&actor);
        reversal_commit(&actor);
    }
    if (motor.mode == 3) {
        /* 0017C030 mode 3 / tier 3 requests stop clip 5. Tiers 1/2 instead
         * execute the 0017B910 foot-placement solve against source nodes
         * 17/18 (also while a pose blend is active: mode 3 has no blend
         * gate). */
        int stop_clip = em_model_clip_index(&g.model, 5);
        if (motor.tier == 3 && stop_clip >= 0) {
            g.loco_stop_clip = stop_clip;
            em_player_stop_begin(&g.loco_stop,
                                 g.model.clips[stop_clip].frame_count);
            player_pose_request(5, (float)g.loco_stop.frame, 6, 1);
            g.loco_mode = 4;
            g.loco_upt = g.move_speed = 0;
            g.loco_animation_step = 0;
            player_probe_tail(1);
            return;
        }
        if (player_pose_foot_stop_begin()) {
            player_probe_tail(1);
            return;
        }
        player_pose_unsupported_hold("foot-placement stop begin failed");
        g.loco_mode = 0;
        g.loco_substate = 0;
        g.loco_tier = 0;
        g.loco_upt = 0;
        g.move_speed = 0;
        player_probe_tail(1);
        return;
    }

    /* The ramped speed drives this frame (sustained: gait 1 = WALK
     * 6 u/s, gait 2 = JOG 18 u/s, gait 3 = RUN 48 u/s). VELOCITY IS
     * EMITTED ALONG g.yaw (the eased body heading), NOT the raw stick
     * vector (mx, mz) — the body lags the stick, so the path curves. */
locomotion_translate:
    g.move_speed = g.loco_upt * 60.0f;
    float vx = sinf(g.yaw), vz = cosf(g.yaw);

    /* 00178B90's argument1 runs radial probes immediately after the first
     * translation. The outer argument0 translation then reaches the ordinary
     * radial/floor service. Keep both additions and this intermediate pass. */
    if (translation_steps == 2) {
        g.pos[0] += vx * g.move_speed * FRAME_DT;
        g.pos[2] += vz * g.move_speed * FRAME_DT;
        player_wall_probes();
    }

    if (g.coll.poly_count || floor_engaged()) {
        player_move_collide(vx * g.move_speed * FRAME_DT,
                            vz * g.move_speed * FRAME_DT);
    } else {
        g.pos[0] += vx * g.move_speed * FRAME_DT;
        g.pos[2] += vz * g.move_speed * FRAME_DT;
        if (g.pos[0] < kRoomMin[0]) g.pos[0] = kRoomMin[0];
        if (g.pos[0] > kRoomMax[0]) g.pos[0] = kRoomMax[0];
        if (g.pos[2] < kRoomMin[1]) g.pos[2] = kRoomMin[1];
        if (g.pos[2] > kRoomMax[1]) g.pos[2] = kRoomMax[1];
        g.pos[1] = 0.0f;  /* flat floor (no collision world loaded) */
    }
}

/* rand5 — func_00179B90 (byte-matched): func_00122BB8() & 7, with 5..7
 * folded to 0..2. func_00122BB8 is the shared SDK stream (SI-02/AM-17). */
unsigned footstep_rand5(void)
{
    unsigned value = em_random_next() & 7u;
    return value < 5u ? value : value - 5u;
}

/* Floor surface attr — the footing update func_00175900's attr copy:
 * after its own down-probe hits, the engine stores the collision
 * result record's surface-attr byte (+0x1A of the grid poly node) in
 * actor +0x23A every frame; a probe miss writes 0. The port probes at
 * step time instead (same result for a grounded player), reusing the
 * player height-resolve probe walk: step past non-walkable crossings,
 * take the first FLOOR/SLOPE hit's attr (EmCollHit.attr — the native
 * mirror of the poly node's +0x1A). No collision world -> attr 0,
 * which footstep_block maps to the default block 0x10 anyway. (The
 * movable-object override — standing on a crate forces attr 2/4 — and
 * the 0x5A/0x5B/0x5C first-contact one-shots are untranslated.) */
static uint8_t footstep_floor_attr(void)
{
    if (!g.coll.poly_count) return 0;
    float from[3] = { g.pos[0], g.pos[1] + FLOOR_PROBE_UP,   g.pos[2] };
    float down[3] = { g.pos[0], g.pos[1] - FLOOR_PROBE_DOWN, g.pos[2] };
    EmCollHit hit;
    for (int i = 0; i < 8; i++) {
        if (!em_collision_segment_query(&g.coll, from, down,
                                        EM_COLL_SET_CELLS |
                                        EM_COLL_SET_GRID, 0, &hit))
            return 0;                  /* probe miss: +0x23A = 0 */
        if (hit.surf_class == EM_SURF_FLOOR ||
            hit.surf_class == EM_SURF_SLOPE)
            return hit.attr;
        if (hit.point[1] - 1e-3f <= down[1])
            return 0;
        from[1] = hit.point[1] - 1e-3f;
    }
    return 0;
}

/* BLOCK(attr) — func_00182430's compiled-in material bases (FINDINGS
 * table; stride 0x11 = 17 ids per material: 3 tier sub-bases x 5
 * variants + 2 spare landing/scuff slots). */
static unsigned footstep_block(uint8_t attr)
{
    switch (attr) {
    case 1:    return 0x21u;
    case 2:    return 0x32u;
    case 3:    return 0x43u;
    case 4:    return 0x54u;
    case 5:    return 0x65u;
    case 6: case 7: return 0xA9u;
    case 8:    return 0x87u;
    case 0xD:  return 0xDCu;
    case 0xE:  return 0xEDu;
    case 0x5A: return 0x76u;  /* wet/puddle surface */
    case 0x5B: return 0xBAu;  /* water SHALLOW; DEEP (0xCB) needs the
                               * +0x23C depth state — untranslated */
    case 0x5C: return 0x98u;
    default:   return 0x10u;  /* attr 0 + any unmapped material — the
                               * office floor (grid attr 0, FINDINGS
                               * office cross-check) */
    }
}

/* One footstep at `tier` (the engine mapper's a1 = +0x25C; the locomotion paths
 * pass actor +0x25C, the melee impact gates a scripted 1..3).
 * EM_STEP_TRACE=1 prints each step's resolved attr/ids (debug). */
void footstep_play(int tier)
{
    uint8_t  attr = footstep_floor_attr();
    unsigned sub  = tier == 3 ? 0xAu : tier == 2 ? 5u : 0u;
    unsigned surf = footstep_block(attr) + sub + footstep_rand5();
    unsigned gear = EM_SFX_STEP_GEAR_BASE + footstep_rand5();
    static int trace = -1;
    if (trace < 0) trace = getenv("EM_STEP_TRACE") != NULL;
    if (trace)
        printf("step: f%d tier %d attr 0x%02X -> surface 0x%03X gear 0x%03X\n",
               g.frame_no, tier, attr, surf, gear);
    /* engine: positional play_sound(actor, id, 0) radius 300 for both
     * layers (FINDINGS footstep decode) — source = the player, so the
     * gains come out center/full by the play_sound math itself */
    em_sfx_play_at(surf, g.pos, 300.0f);
    em_sfx_play_at(gear, g.pos, 300.0f);
}

/* ---- WP-15 P14/P15: 00187350 footstep dispatch ---------------------------
 * The original triggers steps from the source animation clock, not from the
 * display: 0015BCF0 calls 00187350 once per player stage, after the state
 * callback and the skeleton evaluation. em_player_floor.c holds the
 * translation (test_player_footstep_reference.py). This binding supplies the
 * actor bytes the port keeps elsewhere and the workers:
 *   00179B90 -> footstep_rand5, 00122BB8 -> em_random_next,
 *   001FBD50(actor, id, 0, 300) -> em_sfx_play_at(id, feet, 300),
 *   001EFD90 / 001F0460 / 001E8B90 -> coordinator-bound workers.
 * An unbound effect/decal/wade worker is a counted fault (reported once);
 * the step state still advances, as the original does after those calls. */
static struct {
    uint8_t step;       /* +25E */
    int16_t wet;        /* +212 */
    uint8_t surface;    /* +23A as last written by the floor service */
    int surface_valid;
    unsigned faults;
    int reported;
    int (*effect)(void *context, uint32_t id, const float position[3],
                  const float rotation[3]);
    void *effect_context;
    int (*decal)(void *context, const float position[3], float yaw, float pitch);
    void *decal_context;
    int (*wade)(void *context, const float position[3], float level);
    void *wade_context;
} footstep;

void player_footstep_set_workers(
    int (*effect)(void *context, uint32_t id, const float position[3],
                  const float rotation[3]), void *effect_context,
    int (*decal)(void *context, const float position[3], float yaw, float pitch),
    void *decal_context,
    int (*wade)(void *context, const float position[3], float level),
    void *wade_context)
{
    footstep.effect = effect; footstep.effect_context = effect_context;
    footstep.decal = decal; footstep.decal_context = decal_context;
    footstep.wade = wade; footstep.wade_context = wade_context;
}

unsigned player_footstep_faults(void) { return footstep.faults; }

void player_footstep_reset(void)
{
    footstep.step = 0;
    footstep.wet = 0;
    footstep.surface_valid = 0;
}

/* 0017C030 writes the step mailbox when a stop ends: mode 4 posts 0x83,
 * mode 5 posts 0x81 (tier 1) or 0x82. 00187350 consumes it that frame. */
void player_footstep_post(uint8_t code)
{
    footstep.step = code;
}

uint8_t player_footstep_phase(void) { return footstep.step; }

static void footstep_fault(const char *worker)
{
    ++footstep.faults;
    if (!footstep.reported) {
        footstep.reported = 1;
        fprintf(stderr, "player footstep: %s worker is not bound (frame %d); "
                "faults are counted by player_footstep_faults\n", worker, g.frame_no);
    }
}

static int step_random5(void *context, unsigned *value)
{
    (void)context;
    *value = footstep_rand5();
    return 0;
}

static int step_random(void *context, uint32_t *value)
{
    (void)context;
    *value = em_random_next();
    return 0;
}

static int step_sound(void *context, unsigned id)
{
    (void)context;
    /* 00182430 ignores 001FBD50's result (a culled source returns -1). */
    em_sfx_play_at(id, g.pos, 300.0f);
    return 0;
}

static int step_effect(void *context, uint32_t id, const float position[3],
                       const float rotation[3])
{
    (void)context;
    if (!footstep.effect) { footstep_fault("001EFD90 effect"); return 0; }
    return footstep.effect(footstep.effect_context, id, position, rotation);
}

static int step_decal(void *context, const float position[3], float yaw, float pitch)
{
    (void)context;
    if (!footstep.decal) { footstep_fault("001F0460 decal"); return 0; }
    return footstep.decal(footstep.decal_context, position, yaw, pitch);
}

static int step_wade(void *context, const float position[3], float level)
{
    (void)context;
    if (!footstep.wade) { footstep_fault("001E8B90 wade"); return 0; }
    return footstep.wade(footstep.wade_context, position, level);
}

/* The live floor service's +23A (live player states). */
static void footstep_surface_from_live(uint8_t surface)
{
    footstep.surface = surface;
    footstep.surface_valid = 1;
}

/* +23A for the dispatcher: the floor service's stored byte once it has run,
 * else the current floor probe (see footstep_floor_attr). */
static uint8_t footstep_surface(void)
{
    return footstep.surface_valid ? footstep.surface : footstep_floor_attr();
}

int player_footstep_0187350(uint32_t frame, uint8_t area)
{
    EmPlayerStepActor actor;
    memset(&actor, 0, sizeof actor);
    memcpy(actor.position, g.pos, sizeof actor.position);
    actor.rotation[1] = g.yaw;           /* the port's player has yaw only */
    actor.speed = g.loco_upt;
    actor.surface_y = g.pos[1];
    actor.mode = (uint8_t)g.loco_mode;
    actor.tier = (uint8_t)g.loco_tier;
    actor.step = footstep.step;
    actor.wet = footstep.wet;
    actor.surface = footstep_surface();
    actor.contact = 1;
    actor.obstruction = g.probe_block_mask;
    if (floor_engaged()) {
        /* The live floor service's +A, +23C and +250 (live player states). */
        const EmPlayerLiveActor *live_actor = player_states_actor();
        actor.contact = em_live_u8(live_actor, 0xA);
        actor.depth = em_live_u8(live_actor, 0x23C);
        actor.surface_y = em_live_f32(live_actor, 0x250);
    }
    if (actor.mode == 0x01 || actor.mode == 0x02 || actor.mode == 0x2F || actor.mode == 0x41) {
        unsigned clip, flags;
        float remaining;
        if (!player_pose_source(&clip, &remaining, &flags, NULL)) {
            ++footstep.faults;
            fprintf(stderr, "player footstep: locomotion mode %u without an original "
                    "source clock at frame %d\n", actor.mode, g.frame_no);
            return -1;
        }
        actor.clip = (int16_t)clip;
        actor.clock = remaining;
        actor.anim_flags = flags;
    }
    /* Skeleton nodes 17/18 (D_00275B40+0x44/+0x48, world translation +C0).
     * The player stage's evaluated palette, as player_pose_finish_palette
     * reads node 1 for the hip. */
    const float *foot17 = NULL, *foot18 = NULL;
    if (g.model.bone_count > 18) {
        foot17 = g.player_palette + 17 * 16 + 12;
        foot18 = g.player_palette + 18 * 16 + 12;
    }
    EmPlayerStepScene scene = { foot17, foot18, frame, area };
    EmPlayerStepWorkers workers = {
        NULL, step_random5, step_random, step_sound, step_effect, step_decal, step_wade
    };
    uint8_t step_before = actor.step;
    int result = em_player_footstep_tick(&actor, &scene, &workers);
    static int trace = -1;
    if (trace < 0) trace = getenv("EM_STEP_TRACE") != NULL;
    if (trace)
        printf("footstep: frame %d mode %u clip %d clock %.3f flags 0x%X tier %u "
               "surface %u step %u->%u\n", g.frame_no, actor.mode, actor.clip,
               actor.clock, actor.anim_flags, actor.tier, actor.surface,
               step_before, actor.step);
    footstep.step = actor.step;
    footstep.wet = actor.wet;
    if (result < 0) {
        ++footstep.faults;
        fprintf(stderr, "player footstep: 00187350 fault at frame %d\n", g.frame_no);
    }
    return result;
}

/* Cyclic edge test: did the looping clip playhead cross `trig` going
 * prev -> cur (both in frames, cur may have wrapped past 0)? */
int step_crossed(double prev, double cur, double trig)
{
    if (cur == prev) return 0;                    /* standing: frozen */
    if (cur > prev) return prev < trig && trig <= cur;
    return trig > prev || trig <= cur;            /* wrapped the loop */
}

/* AIM POSE LADDER blend (the dispatch half of the func_0017ABA0 steer
 * — anim_slot_index/anim_matrix_dispatch sampling the D_00248B70 sub-0
 * ladder 0x112..0x11A by the blends +0x278/+0x27C, two-buffer blend
 * func_00179CA0). The port evaluates the bilinear 3x3 pose grid at the
 * shared playhead and lerps the palettes (the same matrix-lerp the
 * port's idle/walk crossfades use — a documented stand-in for the
 * engine's bone-channel blend). Returns 1 when it produced the
 * palette (all needed ladder clips present), 0 to fall back to the
 * plain base-clip evaluation. */
int aim_ladder_eval(double t)
{
    float p  = g.aim_pitch, yb = g.aim_yawb;
    int   up = p >= 0.5f;
    int   rt = yb < 0.5f;                  /* screen-right column */
    float wp = up ? (p - 0.5f) * 2.0f : (0.5f - p) * 2.0f;
    float wy = rt ? (0.5f - yb) * 2.0f : (yb - 0.5f) * 2.0f;

    unsigned id01 = rt ? 0x115 : 0x116;            /* level, yawed   */
    unsigned id10 = up ? 0x113 : 0x114;            /* pitched, ahead */
    unsigned id11 = up ? (rt ? 0x117 : 0x119)      /* corner          */
                       : (rt ? 0x118 : 0x11A);
    struct { unsigned id; float w; } s[4] = {
        { 0x112, (1.0f - wp) * (1.0f - wy) },
        { id01,  (1.0f - wp) * wy },
        { id10,  wp * (1.0f - wy) },
        { id11,  wp * wy },
    };
    uint32_t n = g.model.bone_count * 16;
    int      first = 1;
    for (int i = 0; i < 4; i++) {
        if (s[i].w <= 0.0f) continue;
        int ci = em_model_clip_index(&g.model, s[i].id);
        if (ci < 0) return 0;              /* old EMDL: no ladder bake */
        em_model_palette_at(&g.model, (uint32_t)ci, t, g.aim_palette);
        if (first) {
            for (uint32_t k = 0; k < n; k++)
                g.player_palette[k] = g.aim_palette[k] * s[i].w;
            first = 0;
        } else {
            for (uint32_t k = 0; k < n; k++)
                g.player_palette[k] += g.aim_palette[k] * s[i].w;
        }
    }
    return !first;
}

/* The analytic aim direction the camera (and the self-tests) consume —
 * the pose the ladder blend selects, expressed as a world ray (the
 * engine reads the equivalent from the posed hand matrix, gun+0xC0).
 * Pose pitches/yaws are the values MEASURED from the baked ladder
 * clips (constants block above). */
void aim_dir_get(float out[3])
{
    float p  = g.aim_pitch, yb = g.aim_yawb;
    float pit_deg = p >= 0.5f
        ? AIM_POSE_CTR_DEG + (p - 0.5f) * 2.0f *
              (AIM_POSE_UP_DEG - AIM_POSE_CTR_DEG)
        : AIM_POSE_CTR_DEG - (0.5f - p) * 2.0f *
              (AIM_POSE_DOWN_DEG + AIM_POSE_CTR_DEG);
    /* yaw blend: 0 = screen right = yaw DECREASING in the port basis */
    float yaw = g.yaw + (yb - 0.5f) * 2.0f *
                (AIM_POSE_YAW_DEG * EM_PI / 180.0f);
    float pit = pit_deg * EM_PI / 180.0f;
    out[0] = sinf(yaw) * cosf(pit);
    out[1] = sinf(pit);
    out[2] = cosf(yaw) * cosf(pit);
}
