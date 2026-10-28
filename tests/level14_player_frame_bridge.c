/* Native frame composition for the later-level instruction oracle.
 * Compile the actual shared sources. Only the original's callee boundaries
 * and unrelated host services are scripted; the player frame, stage glue,
 * pose selector, publication and stage tail remain the production code. */
#define em_player_stage_begin lp_stage_begin
#define em_player_stage_dispatch lp_stage_dispatch
#define em_player_stage_end lp_stage_end
#include "game/em_player.c"
#include "game/em_player_frame.c"
#include "game/em_player_pose_host.c"
#undef em_player_stage_begin
#undef em_player_stage_dispatch
#undef em_player_stage_end

EmGameState g;
const float kLocoTierSpeed[4] = {0.0f, 0.1f, 0.3f, 0.8f};
static EmSceneState lp_scene;
static int32_t lp_carry;
static int lp_quit;
static int32_t lp_stage_busy;
static void (*lp_observe)(uint32_t, uint8_t *, int32_t);

static void lp_event(uint32_t address, uint8_t *record, int32_t argument)
{
    if (lp_observe) lp_observe(address, record, argument);
}

EmSceneState *em_scene_state(void) { return &lp_scene; }
int32_t *em_area11_boxes_carry31F0(void) { return &lp_carry; }
void em_frame_request_quit(void) { lp_quit = 1; }
int em_enemy_player_hit_take(void) { return 0; }
int player_damage_locked(void) { return 0; }
int em_examine_input_locked(void) { return 0; }
int em_opening_runtime_busy(void) { return 0; }
int em_door_menu_locked(void) { return 0; }
void player_struggle_tick(void) {}

int lp_stage_begin(EmPlayerLiveActor *a, EmPlayerStageScene *s,
                   const EmPlayerStageWorkers *w)
{
    (void)s; (void)w;
    lp_event(0x0015BA50u, a->bytes, 0);
    return 0;
}
int lp_stage_dispatch(EmPlayerLiveActor *a, const EmPlayerStageWorkers *w)
{
    (void)a; (void)w;
    return 0;
}
int lp_stage_end(EmPlayerLiveActor *a, EmPlayerStageScene *s)
{
    (void)a;
    if (lp_stage_busy >= 0) s->busy = (uint8_t)lp_stage_busy;
    return 0;
}
int em_camleft_0015CBA0(EmPlayerLiveActor *a)
{
    lp_event(0x0015CBA0u, a->bytes, 0);
    return 0;
}
int em_player_closure_live_footstep(EmPlayerLiveActor *a)
{
    lp_event(0x00187350u, a->bytes, 0);
    return 0;
}
int em_glue_0015CF90(const uint8_t *record, const EmGlueVitals *v)
{
    (void)v;
    lp_event(0x0015CF90u, (uint8_t *)record, 0);
    return 0;
}
int em_pose_host_001C6DA0(EmPoseHost *h, uint8_t *r, uint32_t n)
{
    (void)h; (void)n;
    lp_event(0x001C6DA0u, r, 0);
    return 0;
}
int em_pose_host_001C68C0(EmPoseHost *h, uint8_t *r, uint32_t n)
{
    (void)h; (void)n;
    lp_event(0x001C68C0u, r, 0);
    return 0;
}
int em_pose_host_001C6960(EmPoseHost *h, uint8_t *r, uint32_t n)
{
    (void)h; (void)n;
    lp_event(0x001C6960u, r, 0);
    return 0;
}
static int lp_stop(void *ctx, int32_t handle)
{
    (void)ctx;
    lp_event(0x0011A070u, live.a.bytes, handle);
    return 0;
}

int lp_frame(const uint8_t *raw, const int16_t *rows, const float *hip, uint32_t busy,
             int32_t stage_busy,
             const EmPlayerStageWorkers *workers,
             void (*observe)(uint32_t, uint8_t *, int32_t),
             uint8_t *result, uint32_t *published)
{
    memset(&g, 0, sizeof g);
    memset(&live, 0, sizeof live);
    memset(&source, 0, sizeof source);
    memset(&lp_scene, 0, sizeof lp_scene);
    lp_scene.req[EM_SCENE_REQ_B3] = (uint8_t)busy;
    live.scene.busy = (uint8_t)busy;
    lp_stage_busy = stage_busy;
    lp_observe = observe;
    lp_carry = 0x12345678u;
    lp_quit = 0;
    live.initialised = live.bound = live.display = 1;
    live.b.stage = *workers;
    live.b.stage.stop_sound = lp_stop;
    live.loco[0] = workers->state[0];
    live.loco[1] = workers->state[1];
    memcpy(live.a.bytes, raw, EM_PLAYER_ACTOR_SIZE);
    memcpy(g.pos, raw + 0xA0, 12);
    memcpy(&g.yaw, raw + 0xC4, 4);
    memcpy(&g.status.health, raw + 0x220, 4);
    memcpy(&g.pd_pend_hp, raw + 0x224, 4);
    memcpy(&g.status.infection, raw + 0x228, 4);
    memcpy(&g.pd_pend_inf, raw + 0x22C, 4);
    g.pd_infected = raw[0x234];
    g.pd_iframes = (int16_t)em_live_u16(&live.a, 0x20E);
    g.model.bone_count = EM_PLAYER_POSE_PALETTE_BONES;
    source.started = source.valid = 1;
    source.record.loaded = source.record.attached = 1;
    source.record.actor = &live.a;
    memcpy(source.record.row0, rows, sizeof source.record.row0);
    memcpy(source.record.nodes + EM_POSE_NODE_BYTES + 0xC0, hip, 16);
    int status = em_player_0015BCF0();
    memcpy(result, live.a.bytes, EM_PLAYER_ACTOR_SIZE);
    published[0] = lp_carry;
    memcpy(published + 1, source.hip, 12);
    /* The host stores hip XYZ; the native palette retains the fourth word. */
    memcpy(published + 4, g.player_palette + 16 + 15, 4);
    memcpy(published + 5, source.saved_euler, 12);
    memcpy(published + 8, &source.saved_euler_w, 4);
    published[9] = lp_scene.req[EM_SCENE_REQ_B3];
    published[10] = (uint32_t)lp_quit;
#ifdef EM_TEST_FRAME_MUTANT
    published[5] ^= 1u;
#endif
    return status;
}
