/* em_camera_live.c - the live walking camera: one storage for the original
 * camera bytes and the worker bindings of the translated camera routines
 * (see em_camera_live.h, docs/CAMERA_LIVE.md).
 *
 * Nothing here is camera logic: every decision is made by a translated
 * original routine (em_camera_leftovers*, em_camera_follow_original,
 * em_camera_area11_specials, em_camera_commit_original, em_census_standins,
 * em_script_host_workers, em_script_door_fan). This file owns the bytes they
 * work on, loads and stores each module's scratch view at the boundaries
 * between modules, and adapts each worker slot to its bound translation. */
#include "game/em_camera_live.h"

#include "game/em_camera.h"
#include "game/em_camera_area11_specials.h"
#include "game/em_camera_commit_original.h"
#include "game/em_camera_leftovers.h"
#include "game/em_census_standins.h"
#include "game/em_coll_list_passes_walkers.h"
#include "game/em_coll_segment_walkers.h"
#include "game/em_director_original.h"
#include "game/em_player_closure_10_12_19.h"
#include "game/em_scene_bindings.h"
#include "game/em_collision_world.h"
#include "game/em_ee_float.h"
#include "game/em_effect_original.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_owner_services_original.h"
#include "game/em_player_stage_workers.h"
#include "game/em_scene_state.h"
#include "game/em_script_door_fan.h"
#include "game/em_script_host_workers.h"
#include "game/em_sdk_math_original.h"

#include <stdio.h>
#include <string.h>

#define CAM_BASE   UINT32_C(0x008101E0)
#define POOL_BASE  UINT32_C(0x008105D0)
#define POOL_WORDS 53u                      /* 0x008105D0 .. 0x008106A3 */
#define P_EYE   0u                          /* D_008105D0 */
#define P_TGT   4u                          /* D_008105E0 */
#define P_UP    8u                          /* D_008105F0 */
#define P_FWD   12u                         /* D_00810600 */
#define P_VIEW  16u                         /* D_00810610 */
#define P_VIEWT 32u                         /* D_00810650 */
#define P_690   48u
#define P_694   49u
#define P_698   50u
#define P_69C   51u
#define P_6A0   52u

/* The camera tables of the user's boot ELF (tools/export_camera_tables.py):
 * D_0024A4B0 (00190F20's area-0xE quad) through the region quads
 * D_0024A5F0 + 0x40 * i (00194D10). */
#define CAMERA_TABLES_PATH "assets/camera_tables.emrg"
#define TABLES_BASE UINT32_C(0x0024A4B0)
#define TABLES_END  UINT32_C(0x0024A6F0)
#define TABLES_WORDS ((TABLES_END - TABLES_BASE) / 4u)

static struct {
    int bound;
    uint32_t fault;
    /* ---- the canonical original bytes ---- */
    union { EmCameraFollowRecord rec; uint32_t words[EM_CAMERA_FOLLOW_RECORD_SIZE / 4]; } cam;
    uint32_t pool[POOL_WORDS];
    EmCamLeftScratch scratch;                     /* 0x700038A0..0x70003A3C */
    uint32_t s3400[16], s3600[4], s3630[4];        /* 0x70003400, 0x70003600, 0x70003630 */
    EmCamLeftHit hit;                             /* 0x700031B0.. and *0x700031D0 */
    EmInteractionProjection projection;           /* *D_00275670 +0x2450 */
    uint32_t tables[TABLES_WORDS];
    int tables_loaded;
    /* ---- inputs loaded at each entry (not camera storage) ---- */
    /* The camera's view of D_008102B0 (the live record with the placement
     * the port keeps in g.pos / the pose host; section 3 of the doc). */
    EmPlayerLiveActor player;
    /* 0x70003B50 as the camera frame reads it: a per-entry load of
     * 0015BCF0's tail publication (the pose host's), never written back;
     * no camera routine stores 0x70003B50. The scene state's
     * spad3B40[4..7] is 001B07C0's write, which only 001B0080 reads (rw_001B0080,
     * inside 001B07C0 -> 001B0460). CAMERA_LIVE.md section 3. */
    uint32_t s3B50[4];
    uint16_t s3B80;                               /* 0x70003B80 */
    /* ---- the module views and bindings ---- */
    EmCameraFollowGlobals fg;
    EmCameraFollowScratch fs;
    EmCameraFollowWorkers fw;
    EmCameraFollowWorld fworld;
    EmCamLeftGlobals lg;
    EmCamLeftWorkers lw;
    EmCamLeftWorld lworld;
    EmCamSpecials sp;
    EmCamSpecialsScratch sps;
    EmCameraCommitWorkers cw;
    EmCameraCommitWorld cworld;
    EmScriptHostWorkers shw;
    EmCollListPassesGround ground;
    EmCameraLiveHost host;
} C;

static int fail(uint32_t address)
{
    if (!C.fault) C.fault = address ? address : 0x0018B9C0u;
    return -1;
}

uint32_t em_camera_live_fault(void) { return C.fault; }
static void view_store(void);
static void view_publish(void);
void em_camera_live_view_publish(void) { view_store(); view_publish(); }
int em_camera_live_bound(void) { return C.bound; }
EmInteractionProjection *em_camera_live_projection(void) { return &C.projection; }

uint8_t *em_camera_live_bytes(uint32_t address, uint32_t size)
{
    if (address >= CAM_BASE && size <= EM_CAMERA_FOLLOW_RECORD_SIZE &&
        address - CAM_BASE <= EM_CAMERA_FOLLOW_RECORD_SIZE - size)
        return C.cam.rec.bytes + (address - CAM_BASE);
    if (address >= POOL_BASE && size <= 4u * POOL_WORDS && address - POOL_BASE <= 4u * POOL_WORDS - size)
        return (uint8_t *)C.pool + (address - POOL_BASE);
    return NULL;
}

/* ======================================================================
 * The g.cam view (docs/CAMERA_LIVE.md section 3)
 * ====================================================================== */

static uint8_t *cb(unsigned at) { return C.cam.rec.bytes + at; }
static void put(unsigned at, const void *v, size_t n) { memcpy(cb(at), v, n); }
static void get(void *v, unsigned at, size_t n) { memcpy(v, cb(at), n); }

/* g.cam -> the original bytes: the fields other modules may have written
 * since the last store. */
static void view_load(void)
{
    const EmCamera *c = &g.cam;
    *cb(0x00) = c->state;
    *cb(0x01) = c->sub_state;
    *cb(0x03) = c->swing;
    *cb(0x04) = c->top_mode;
    *cb(0x05) = c->table_sel;
    *cb(0x06) = c->mode;
    *cb(0x07) = c->hit;
    put(0x08, &c->timer, 2);
    put(0x10, c->eye_des, 12);
    put(0x20, c->tgt_des, 12);
    put(0x30, c->seed_euler, 12);
    put(0x44, &c->yaw, 4);
    put(0x48, &c->orbit_tgt, 4);
    put(0x4C, &c->orbit_rad, 4);
    put(0x50, &c->y_lo, 4);
    put(0x54, &c->y_hi, 4);
    put(0x58, &c->hit_attr, 2);
    put(0x5A, &c->probe_flags, 2);
    put(0x5C, &c->var_5c, 4);
    put(0x60, &c->overhead_y, 4);
    *cb(0x6D) = c->ground_attr78;
    put(0x6E, &c->cine_scene, 2);
    put(0x70, &c->cine_track, 4);
    put(0x74, &c->cine_time, 4);
    put(0x78, &c->cine_head, 4);
    put(0x8C, &c->aim_h, 4);
    put(0x90, &c->wall_yaw, 4);
    put(0xA0, &c->tgt_soft, 2);
    memcpy(&C.pool[P_EYE], c->eye, 12);
    memcpy(&C.pool[P_TGT], c->tgt, 12);
    memcpy(&C.pool[P_UP], c->up, 12);
}

/* The original bytes -> g.cam, plus the outputs only the commit writes. */
static void view_store(void)
{
    EmCamera *c = &g.cam;
    c->state = *cb(0x00);
    c->sub_state = *cb(0x01);
    c->swing = *cb(0x03);
    c->top_mode = *cb(0x04);
    c->table_sel = *cb(0x05);
    c->mode = *cb(0x06);
    c->hit = *cb(0x07);
    get(&c->timer, 0x08, 2);
    get(c->eye_des, 0x10, 12);
    get(c->tgt_des, 0x20, 12);
    get(c->seed_euler, 0x30, 12);
    get(&c->yaw, 0x44, 4);
    get(&c->orbit_tgt, 0x48, 4);
    get(&c->orbit_rad, 0x4C, 4);
    get(&c->y_lo, 0x50, 4);
    get(&c->y_hi, 0x54, 4);
    get(&c->hit_attr, 0x58, 2);
    get(&c->probe_flags, 0x5A, 2);
    get(&c->var_5c, 0x5C, 4);
    get(&c->overhead_y, 0x60, 4);
    c->ground_attr78 = *cb(0x6D);
    get(&c->cine_scene, 0x6E, 2);
    get(&c->cine_track, 0x70, 4);
    get(&c->cine_time, 0x74, 4);
    get(&c->cine_head, 0x78, 4);
    get(&c->aim_h, 0x8C, 4);
    get(&c->wall_yaw, 0x90, 4);
    get(&c->tgt_soft, 0xA0, 2);
    memcpy(c->eye, &C.pool[P_EYE], 12);
    memcpy(c->tgt, &C.pool[P_TGT], 12);
    memcpy(c->up, &C.pool[P_UP], 12);
    memcpy(c->fwd, &C.pool[P_FWD], 12);
    memcpy(&c->horiz_dist, &C.pool[P_690], 4);
}

/* The renderer's view of D_00810610 and the projection (the zoom is the
 * render context's +0x2468, g.cam.zoom). */
static void view_publish(void)
{
    em_cs_view_to_native(g.cam.view, &C.pool[P_VIEW]);
    float proj[16];
    em_mat4_perspective_gs(proj, g.cam.zoom > 0.0f ? g.cam.zoom : ENGINE_CAM_ZOOM_S);
    em_mat4_mul(g.viewproj, proj, g.cam.view);
}

/* The camera's view of the player record D_008102B0 at the camera stage:
 * the live record, with the words 0015BCF0's tail leaves that the port keeps
 * elsewhere: +A0 the position (g.pos), +B0 the bone-1 position (the host's
 * hip: the pose host's), +C4 the heading (g.yaw), and 0x70003B50 the
 * published +C0..+CC (the host's Euler: the pose host's saved Euler, the
 * lanes the retarget reads).
 *
 * SUBSTITUTIONS (CAMERA_LIVE.md section 5): while the pose host has no
 * evaluated pose (the port evaluates none before first control, the
 * original's 0015BCF0 does every frame), +B0 reads the placement g.pos
 * instead of the bone-1 position, and 0x70003B50 reads the record's +C0,
 * g.yaw and +C8 instead of the tail's copy of +C0..+C8. Both end when the
 * player's pose is evaluated from the area load (0015BCF0's animate step and
 * tail publication running before first control). */
static void player_refresh(void)
{
    C.player = *C.host.player(C.host.context);
    float hip[3], euler[3];
    const float one = 1.0f;
    for (unsigned i = 0; i < 3; ++i) em_live_set_f32(&C.player, 0xA0 + 4 * i, g.pos[i]);
    em_live_set_f32(&C.player, 0xAC, one);
    if (!C.host.hip(C.host.context, hip)) memcpy(hip, g.pos, sizeof hip);
    for (unsigned i = 0; i < 3; ++i) em_live_set_f32(&C.player, 0xB0 + 4 * i, hip[i]);
    em_live_set_f32(&C.player, 0xBC, one);
    em_live_set_f32(&C.player, 0xC4, g.yaw);
    if (!C.host.euler(C.host.context, euler)) {
        euler[0] = em_live_f32(&C.player, 0xC0);
        euler[1] = g.yaw;
        euler[2] = em_live_f32(&C.player, 0xC8);
    }
    memcpy(C.s3B50, euler, 12);
    C.s3B50[3] = em_live_u32(&C.player, 0xCC);
    const uint16_t *pad = C.host.pad_config ? C.host.pad_config(C.host.context) : NULL;
    C.s3B80 = pad ? pad[6] : 0;                    /* 0x70003B80 = config word 6 */
}

/* ======================================================================
 * The scratch views (one storage, loaded and stored at module boundaries)
 * ====================================================================== */

static uint32_t *spad(uint32_t address) { return em_camleft_spad(&C.scratch, address); }

static void follow_load(void)
{
    em_camleft_scratch_to_follow(&C.scratch, &C.fs);
    memcpy(C.fs.s3B50, C.s3B50, 16);
    memcpy(C.fs.s3400, C.s3400, 64);
    memcpy(C.fs.s3600, C.s3600, 16);
}

static void follow_store(void)
{
    em_camleft_scratch_from_follow(&C.scratch, &C.fs);
    memcpy(C.s3400, C.fs.s3400, 64);
    memcpy(C.s3600, C.fs.s3600, 16);
}

static void specials_load(void)
{
    EmCamSpecialsScratch *s = &C.sps;
    memset(s->s3040, 0, sizeof s->s3040);   /* 0x70003040: the aim entry (00197490 only; aim unbound) */
    memcpy(s->s31B0, C.hit.point, 16);
    memcpy(s->s3400, C.s3400, 64);
    memcpy(s->s3600, C.s3600, 16);
    memcpy(s->s3630, C.s3630, 16);
    memcpy(s->s38A0, spad(0x700038A0), 16);
    s->s3A20 = *spad(0x70003A20);
    s->s3A24 = *spad(0x70003A24);
    memcpy(s->s3B50, C.s3B50, 16);
    s->s3B80 = C.s3B80;
    s->s3B8D = em_scene_state()->spad3B8D;
}

static void specials_store(void)
{
    const EmCamSpecialsScratch *s = &C.sps;
    memcpy(C.hit.point, s->s31B0, 16);
    memcpy(C.s3400, s->s3400, 64);
    memcpy(C.s3600, s->s3600, 16);
    memcpy(C.s3630, s->s3630, 16);
    memcpy(spad(0x700038A0), s->s38A0, 16);
    *spad(0x70003A20) = s->s3A20;
    *spad(0x70003A24) = s->s3A24;
}

/* ======================================================================
 * Leaf and SDK workers
 * ====================================================================== */

static EmSdkMathContext *sdk(void) { return em_collision_world_sdk(); }

static int w_wrap(void *ctx, uint32_t x, uint32_t *out) { (void)ctx; *out = em_player_001B1470(x); return 0; }

static int w_approach(void *ctx, uint32_t target, uint32_t current, uint32_t rate, uint32_t *out)
{
    (void)ctx;
    return em_script_host_001B12B0(NULL, target, current, rate, out);
}

static int w_heading(void *ctx, const uint32_t obj[3], uint32_t x, uint32_t z, uint32_t *out)
{
    (void)ctx;
    return em_script_host_001B1240(&C.shw, obj, x, z, out);
}

static int w_sine(void *ctx, uint32_t x, uint32_t *out)
{
    (void)ctx;
    EmSdkMathContext *m = sdk();
    float r;
    uint32_t f = 0;
    if (!m || em_sdk_math_original_0011E2A8(m->tables, em_ee_float(x), &r, &f) < 0) return -1;
    *out = em_ee_bits(r);
    return 0;
}

static int w_cosine(void *ctx, uint32_t x, uint32_t *out)
{
    (void)ctx;
    EmSdkMathContext *m = sdk();
    float r;
    uint32_t f = 0;
    if (!m || em_sdk_math_original_0011DE90(m->tables, em_ee_float(x), &r, &f) < 0) return -1;
    *out = em_ee_bits(r);
    return 0;
}

static int w_atan2(void *ctx, uint32_t y, uint32_t x, uint32_t *out)
{
    (void)ctx;
    EmSdkMathContext *m = sdk();
    float r;
    uint32_t f = 0;
    if (!m || em_sdk_math_original_0011E620(m->tables, &m->world, &m->workers, em_ee_float(y),
                                            em_ee_float(x), &r, &f) < 0)
        return -1;
    *out = em_ee_bits(r);
    return 0;
}

static int w_sqrt(void *ctx, uint32_t x, uint32_t *out)
{
    (void)ctx;
    EmSdkMathContext *m = sdk();
    float r;
    uint32_t f = 0;
    if (!m || em_sdk_math_original_0011E748(m->tables, &m->world, &m->workers, em_ee_float(x), &r, &f) < 0)
        return -1;
    *out = em_ee_bits(r);
    return 0;
}

static int w_lookat(void *ctx, uint32_t out[16], const uint32_t pos[4], const uint32_t fwd[4],
                    const uint32_t up[4])
{
    (void)ctx;
    return em_cs_00102CD0(out, pos, fwd, up);
}

static int w_identity(void *ctx, uint32_t m[16])
{
    (void)ctx;
    float f[16];
    if (em_owner_services_identity_001029C0(f) != EM_EE_FLOAT_OK) return -1;
    memcpy(m, f, sizeof f);
    return 0;
}

static int w_euler(void *ctx, uint32_t out[16], const uint32_t in[16], const uint32_t angles[4])
{
    (void)ctx;
    float d[16], s[16], a[3];
    memcpy(s, in, sizeof s);
    memcpy(a, angles, sizeof a);
    if (em_owner_services_euler_00102C58(d, s, a) != EM_EE_FLOAT_OK) return -1;
    memcpy(out, d, sizeof d);
    return 0;
}

/* 0019A910 over the collision world's one probe state; afterwards the
 * scratchpad words it leaves (0x700031B0, and through *0x700031D0 the
 * record's +0x1A halfword and +0x24 normal when it names a record) are
 * copied into the canonical hit. 0x700031BC is never written (0 in every
 * captured scratchpad). */
static void hit_from_state(const EmCollSegment *seg)
{
    memcpy(C.hit.point, seg->state->point, 12);
    EmCollSegmentHit h;
    if (em_coll_segment_hit(seg, &h) == 0) {
        C.hit.record_1A = h.record_node;
        memcpy(C.hit.normal, h.record_normal, 12);
    }
}

static int segment(const void *from, const void *to, int mask, int *result)
{
    const EmCollSegment *seg = em_collision_world_segment();
    if (!seg || !from || !to || !result) return -1;
    float a[3], b[3];
    memcpy(a, from, 12);
    memcpy(b, to, 12);
    int r = em_coll_segment_0019A910(seg, a, b, (unsigned)mask);
    if (r < 0) return -1;
    hit_from_state(seg);
    *result = r;
    return 0;
}

/* ======================================================================
 * Follow module workers (EmCameraFollowWorkers)
 * ====================================================================== */

static int fw_segment(void *ctx, const uint32_t from[4], const uint32_t to[4], int mask,
                      EmCameraFollowHit *hit)
{
    (void)ctx;
    int r;
    if (segment(from, to, mask, &r) < 0) return -1;
    hit->result = r;
    hit->record_1A = C.hit.record_1A;
    hit->point_y = C.hit.point[1];
    return 0;
}

static int fw_ground(void *ctx, const uint32_t from[4], const uint32_t to[4], int *result)
{
    (void)ctx;
    const EmCollSegment *seg = em_collision_world_segment();
    if (!seg) return -1;
    C.ground.grid = seg->world->grid;
    C.ground.state = seg->state;
    if (em_coll_list_passes_camera_ground(&C.ground, from, to, result) < 0) return -1;
    memcpy(C.hit.point, seg->state->point, 12);
    return 0;
}

/* The solvers are em_camera_leftovers routines on the canonical scratch:
 * the follow module's view is stored before and reloaded after. */
static int fw_tether(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *player)
{
    (void)ctx;
    if (cam != &C.cam.rec) return -1;
    follow_store();
    int rc = em_camleft_00230000(&C.lworld, player);
    follow_load();
    return rc;
}

static int fw_solve(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *player, int style, int mask,
                    int *result)
{
    (void)ctx;
    if (cam != &C.cam.rec) return -1;
    follow_store();
    int rc = em_camleft_0018DD20(&C.lworld, player, style, mask, result);
    follow_load();
    return rc;
}

static int fw_solve_aim(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *player, int style,
                        int mask, int *result)
{
    (void)ctx;
    if (cam != &C.cam.rec) return -1;
    follow_store();
    int rc = em_camleft_0018F870(&C.lworld, player, style, mask, result);
    follow_load();
    return rc;
}

static int fw_bounds(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *player, int mask)
{
    (void)ctx;
    if (cam != &C.cam.rec) return -1;
    follow_store();
    int rc = em_camleft_0018D910(&C.lworld, player, mask);
    follow_load();
    return rc;
}

/* 0018D7B0 from outside the follow module (the leftovers' solve_dispatch,
 * the specials' w_0018D7B0, the scripted retarget). */
static int solve_dispatch(int style, int *result)
{
    int r = 0;
    follow_load();
    int rc = em_camera_follow_0018D7B0(&C.fworld, style, &r);
    follow_store();
    if (result) *result = r;
    return rc;
}

/* ======================================================================
 * Leftovers workers (EmCamLeftWorkers)
 * ====================================================================== */

static int lw_segment(void *ctx, const uint32_t from[4], const uint32_t to[4], int mask, EmCamLeftHit *hit,
                      int *result)
{
    (void)ctx;
    if (hit != &C.hit) return -1;
    return segment(from, to, mask, result);
}

static int inside_atan2(void *ctx, float y, float x, float *result)
{
    EmSdkMathContext *m = ctx;
    *result = em_sdk_math_original_float_0011E620(m, y, x);
    return m->fault ? -1 : 0;
}

static int lw_inside_bound(void *ctx, int mode, const uint32_t point[3], uint32_t polygon, int count,
                           int *result)
{
    (void)ctx;
    EmSdkMathContext *m = sdk();
    if (!m || !C.tables_loaded || count < 0 || count > 16 || polygon < TABLES_BASE ||
        polygon - TABLES_BASE > 4u * TABLES_WORDS - 16u * (uint32_t)count)
        return -1;
    float quad[16][4], p[3];
    memcpy(quad, (const uint8_t *)C.tables + (polygon - TABLES_BASE), 16u * (uint32_t)count);
    memcpy(p, point, sizeof p);
    int32_t inside = 0;
    uint32_t saved = m->fault;
    m->fault = 0;
    int rc = em_director_original_001B1EA0_bound(mode, p, (const float (*)[4])quad, count, inside_atan2,
                                                 m, &inside);
    uint32_t mine = m->fault;
    m->fault = saved;
    if (rc < 0 || mine) return -1;
    *result = inside;
    return 0;
}

static int lw_solve_dispatch(void *ctx, EmCameraFollowRecord *cam, int style, int *result)
{
    (void)ctx;
    if (cam != &C.cam.rec) return -1;
    return solve_dispatch(style, result);
}

static int commit(int mode);

static int lw_commit(void *ctx, EmCameraFollowRecord *cam, int mode)
{
    (void)ctx;
    if (cam != &C.cam.rec) return -1;
    return commit(mode);
}

/* 0022EEF0(cam, 1): the scripted timeline of +4 == 3, the binder's (the
 * opening's track through em_opening_runtime while it owns the camera, the
 * opening's timeline stand-in of census L33; every other timeline through
 * the AREA11 script host, census L22). Both write the g.cam view. */
static int lw_0022EEF0(void *ctx, EmCameraFollowRecord *cam, int a1)
{
    (void)ctx;
    if (cam != &C.cam.rec || a1 != 1) return -1;
    view_store();
    int rc = C.host.timeline(C.host.context);
    view_load();
    return rc < 0 ? -1 : 0;
}

static int lw_001DD980(void *ctx, uint32_t *eye, uint32_t *target)
{
    (void)ctx;
    float e[3], t[3];
    memcpy(e, eye, sizeof e);
    memcpy(t, target, sizeof t);
    return em_interaction_projection_publish(&C.projection, e, t) ? 0 : -1;
}

/* Camera action 0 (00195130). While a legacy stand-in owns the camera
 * (em_camera.c camera_area11_standins: the director beats of census L21,
 * the fence door cinematic of L18, the examine cue, the port's aim camera
 * of L28) it runs in 00195130's place; otherwise the translation. */
static int lw_00195130(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e)
{
    (void)ctx;
    if (cam != &C.cam.rec) return -1;
    view_store();
    int owned = C.host.standins(C.host.context);
    view_load();
    if (owned == CAMERA_STANDIN_OWNS) return 0;
    if (owned == CAMERA_STANDIN_AIM) return solve_dispatch(0, NULL);
    specials_load();
    int rc = em_cam_specials_action_00195130(&C.sp, cam->bytes, e);
    specials_store();
    if (rc < 0 && C.sp.fault_address) fail(C.sp.fault_address);
    return rc;
}

static int lw_001936E0(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e)
{
    (void)ctx;
    if (cam != &C.cam.rec) return -1;
    specials_load();
    int rc = em_cam_specials_action_001936E0(&C.sp, cam->bytes, e);
    specials_store();
    if (rc < 0 && C.sp.fault_address) fail(C.sp.fault_address);
    return rc;
}

static int lw_00193EB0(void *ctx, EmCameraFollowRecord *cam, EmPlayerLiveActor *e, int a2)
{
    (void)ctx;
    if (cam != &C.cam.rec) return -1;
    specials_load();
    int rc = em_cam_specials_call_00193EB0(&C.sp, cam->bytes, e, a2);
    specials_store();
    if (rc < 0 && C.sp.fault_address) fail(C.sp.fault_address);
    return rc;
}

/* ======================================================================
 * Specials workers (EmCamSpecialsWorkers): the specials' scratch view is
 * stored before a call into another module and reloaded after.
 * ====================================================================== */

#define SP_ENTER() specials_store()
#define SP_LEAVE() specials_load()

static int sp_001916C0(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t mode)
{
    (void)ctx;
    if (cam != C.cam.rec.bytes) return -1;
    SP_ENTER();
    int rc = em_camleft_001916C0(&C.lworld, player, mode);
    SP_LEAVE();
    return rc;
}

static int sp_001921D0(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t mode)
{
    (void)ctx;
    if (cam != C.cam.rec.bytes) return -1;
    SP_ENTER();
    follow_load();
    int rc = em_camera_follow_001921D0(&C.fworld, player, mode);
    follow_store();
    SP_LEAVE();
    return rc;
}

static int sp_00193D90(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t mode)
{
    (void)ctx;
    (void)mode;
    if (cam != C.cam.rec.bytes) return -1;
    SP_ENTER();
    int rc = em_camleft_00193D90(&C.lworld, player);
    SP_LEAVE();
    return rc;
}

static int sp_0018D7B0(void *ctx, uint8_t *cam, int32_t style)
{
    (void)ctx;
    if (cam != C.cam.rec.bytes) return -1;
    SP_ENTER();
    int rc = solve_dispatch(style, NULL);
    SP_LEAVE();
    return rc;
}

static int sp_0018C4B0(void *ctx, void *vec, uint32_t y, uint32_t rate, int32_t *result)
{
    (void)ctx;
    uint32_t v[3];
    memcpy(v, vec, sizeof v);
    int r = 0;
    if (em_camera_follow_0018C4B0(v, y, rate, &r) < 0) return -1;
    memcpy(vec, v, sizeof v);
    if (result) *result = r;
    return 0;
}

static int sp_0018C6A0(void *ctx, const void *from, void *to, uint32_t rate, int32_t *result)
{
    (void)ctx;
    uint32_t s[3], d[3];
    memcpy(s, from, sizeof s);
    memcpy(d, to, sizeof d);
    int r = 0;
    if (em_camera_follow_0018C6A0(s, d, rate, &r) < 0) return -1;
    memcpy(to, d, sizeof d);
    if (result) *result = r;
    return 0;
}

static int sp_00191D40(void *ctx, uint8_t *cam, uint32_t want, uint32_t rate)
{
    (void)ctx;
    if (cam != C.cam.rec.bytes) return -1;
    return em_camera_follow_00191D40(&C.cam.rec, &C.fg, want, rate);
}

static int sp_00192010(void *ctx, uint8_t *cam, uint32_t y, uint32_t up, uint32_t down)
{
    (void)ctx;
    if (cam != C.cam.rec.bytes) return -1;
    return em_camera_follow_00192010(&C.cam.rec, y, up, down);
}

static int sp_0022FCA0(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t a2)
{
    (void)ctx;
    (void)player;
    (void)a2;
    if (cam != C.cam.rec.bytes) return -1;
    SP_ENTER();
    int rc = em_camleft_0022FCA0(&C.lworld);
    SP_LEAVE();
    return rc;
}

static int sp_00194D10(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t index, int32_t *result)
{
    (void)ctx;
    if (cam != C.cam.rec.bytes) return -1;
    SP_ENTER();
    int r = 0;
    int rc = em_camleft_00194D10(&C.lworld, player, index, &r);
    SP_LEAVE();
    if (result) *result = r;
    return rc;
}

static int sp_00191000(void *ctx, uint8_t *cam, EmPlayerLiveActor *player)
{
    (void)ctx;
    if (cam != C.cam.rec.bytes) return -1;
    SP_ENTER();
    int rc = em_camleft_00191000(&C.lworld, player, NULL);
    SP_LEAVE();
    return rc;
}

static int sp_00102C58(void *ctx, uint32_t dst[16], const uint32_t src[16], const void *angles)
{
    uint32_t a[4];
    memcpy(a, angles, 12);
    a[3] = 0;
    return w_euler(ctx, dst, src, a);
}

static int sp_001026A0(void *ctx, void *out, const uint32_t m[16], const uint32_t v[4])
{
    (void)ctx;
    float o[4], mm[16], vv[4];
    memcpy(mm, m, sizeof mm);
    memcpy(vv, v, sizeof vv);
    em_effect_original_001026A0(o, mm, vv);
    memcpy(out, o, sizeof o);
    return 0;
}

static int sp_0011E748(void *ctx, uint32_t x, uint32_t *result) { return w_sqrt(ctx, x, result); }
static int sp_0011E620(void *ctx, uint32_t y, uint32_t x, uint32_t *result) { return w_atan2(ctx, y, x, result); }
static int sp_0011E2A8(void *ctx, uint32_t x, uint32_t *result) { return w_sine(ctx, x, result); }
static int sp_0011DE90(void *ctx, uint32_t x, uint32_t *result) { return w_cosine(ctx, x, result); }
static int sp_001B1470(void *ctx, uint32_t x, uint32_t *result) { return w_wrap(ctx, x, result); }

static int sp_001B1240(void *ctx, const uint32_t object[4], uint32_t x, uint32_t z, uint32_t *result)
{
    uint32_t o[3];
    memcpy(o, object, sizeof o);
    return w_heading(ctx, o, x, z, result);
}

/* 00193660: the grab test of 001936E0 (em_camera_commit_original) on the
 * specials' scratch view of 0x700038A0 / 0x70003A20. */
static int sp_00193660(void *ctx, uint8_t *cam, EmPlayerLiveActor *player, int32_t *result)
{
    (void)ctx;
    (void)player;
    if (cam != C.cam.rec.bytes) return -1;
    return em_camera_commit_00193660(&C.cw, &C.pool[P_EYE], &C.pool[P_TGT], C.sps.s38A0, &C.sps.s3A20,
                                     result);
}

static int sp_0019A910(void *ctx, const void *from, const void *to, int32_t mask, int32_t *result)
{
    (void)ctx;
    int r;
    if (segment(from, to, mask, &r) < 0) return -1;
    memcpy(C.sps.s31B0, C.hit.point, 16);
    if (result) *result = r;
    return 0;
}

/* ======================================================================
 * The commit
 * ====================================================================== */

static int commit(int mode)
{
    C.cworld.fault = 0;
    int rc = em_camera_commit_0018C0D0(&C.cworld, mode);
    if (rc < 0) return fail(C.cworld.fault);
    return 0;
}

/* ======================================================================
 * Binding
 * ====================================================================== */

static int tables_load(void)
{
    if (C.tables_loaded) return 0;
    FILE *f = fopen(CAMERA_TABLES_PATH, "rb");
    if (!f) return -1;
    uint8_t head[16];
    int ok = fread(head, 1, sizeof head, f) == sizeof head && memcmp(head, "EMRG", 4) == 0;
    uint32_t version, base, size;
    memcpy(&version, head + 4, 4);
    memcpy(&base, head + 8, 4);
    memcpy(&size, head + 12, 4);
    ok = ok && version == 1 && base == TABLES_BASE && size == 4u * TABLES_WORDS &&
         fread(C.tables, 1, size, f) == size;
    fclose(f);
    if (!ok) return -1;
    C.tables_loaded = 1;
    return 0;
}

static void bind_worlds(void)
{
    EmSceneState *s = em_scene_state();
    EmSdkMathContext *m = sdk();

    /* 001B1240 / 001B0460's world: the SDK context, the room bytes and the
     * canonical camera and player words. */
    memset(&C.shw, 0, sizeof C.shw);
    C.shw.world.sdk_tables = m ? m->tables : NULL;
    C.shw.world.sdk_world = m ? &m->world : NULL;
    C.shw.world.sdk_workers = m ? &m->workers : NULL;

    C.fg = (EmCameraFollowGlobals){ &C.pool[P_EYE], &C.pool[P_TGT], &C.pool[P_690], &C.pool[P_698],
                                    &C.pool[P_69C], &s->d810700, &s->d810701, &s->d810702 };
    C.fw = (EmCameraFollowWorkers){ NULL, w_approach, w_wrap, w_heading, w_sine, w_cosine, fw_tether,
                                    fw_solve, fw_solve_aim, fw_bounds, fw_segment, fw_ground, w_identity,
                                    w_euler };
    C.fworld = (EmCameraFollowWorld){ &C.cam.rec, &C.player, &C.fg, &C.fs, &C.fw };

    C.lg = (EmCamLeftGlobals){ &C.fg, &C.pool[P_UP], &s->req[EM_SCENE_REQ_EF], &s->req[EM_SCENE_REQ_B8],
                               &s->d810E74, &em_frame_transition()->substate, &C.s3B80, &s->spad3B8D,
                               (const uint8_t *)C.host.carry31F0,
                               C.tables_loaded ? C.tables + (0x0024A5F0u - TABLES_BASE) / 4u : NULL,
                               C.tables_loaded ? (0x0024A6F0u - 0x0024A5F0u) / 4u : 0 };
    memset(&C.lw, 0, sizeof C.lw);
    C.lw.wrap = w_wrap;
    C.lw.approach = w_approach;
    C.lw.heading = w_heading;
    C.lw.sine = w_sine;
    C.lw.cosine = w_cosine;
    C.lw.atan2 = w_atan2;
    C.lw.sqrt = w_sqrt;
    C.lw.segment = lw_segment;
    C.lw.inside = lw_inside_bound;
    C.lw.solve_dispatch = lw_solve_dispatch;
    C.lw.commit = lw_commit;
    C.lw.w_0022EEF0 = lw_0022EEF0;
    C.lw.w_00195130 = lw_00195130;
    C.lw.w_001936E0 = lw_001936E0;
    C.lw.w_00193EB0 = lw_00193EB0;
    C.lw.w_001DD980 = lw_001DD980;
    /* 001B0C60 (areas 0x12 / 0xE), the aim actions 1 / 2 (00197D20 /
     * 00198650), actions 5 and 9..15 and mode 1's 001B0300 have no
     * translation: NULL, so reaching them faults (never on the route:
     * docs/CAMERA_LIVE.md section 5). */
    C.lworld = (EmCamLeftWorld){ &C.cam.rec, &C.player, &C.lg, &C.scratch, &C.hit, &C.lw, 0 };

    memset(&C.sp, 0, sizeof C.sp);
    C.sp.world = (EmCamSpecialsWorld){ C.cam.rec.bytes, &C.pool[P_EYE], &C.pool[P_TGT], &C.pool[P_69C],
                                       &s->req[EM_SCENE_REQ_B8], em_scene_req_at(s, 0x008106F2u),
                                       &s->d810700, &s->d810701, &s->d810702,
                                       em_scene_progress_at(s, 0x0081078Bu, 1),
                                       em_scene_progress_at(s, 0x00810803u, 1), &s->d810E74, &C.sps };
    EmCamSpecialsWorkers *k = &C.sp.w;
    k->w_001916C0 = sp_001916C0;
    k->w_001921D0 = sp_001921D0;
    k->w_00193D90 = sp_00193D90;
    k->w_0018D7B0 = sp_0018D7B0;
    k->w_0018C4B0 = sp_0018C4B0;
    k->w_0018C6A0 = sp_0018C6A0;
    k->w_00191D40 = sp_00191D40;
    k->w_00192010 = sp_00192010;
    k->w_0022FCA0 = sp_0022FCA0;
    k->w_00194D10 = sp_00194D10;
    k->w_00191000 = sp_00191000;
    k->w_00102C58 = sp_00102C58;
    k->w_001026A0 = sp_001026A0;
    k->w_0011E748 = sp_0011E748;
    k->w_0011E620 = sp_0011E620;
    k->w_0011E2A8 = sp_0011E2A8;
    k->w_0011DE90 = sp_0011DE90;
    k->w_001B1470 = sp_001B1470;
    k->w_001B1240 = sp_001B1240;
    k->w_00193660 = sp_00193660;
    k->w_0019A910 = sp_0019A910;
    /* Other areas' arms (001944B0, 00194DB0, 00230230, 0x823FE0, 001AEDE0,
     * 001B0C60) and the aim family (00197870, 00198440, 001912B0, 001B0300)
     * stay NULL: the readiness check requires them only where they are
     * reachable, so reaching one faults. */

    C.cw = (EmCameraCommitWorkers){ NULL, w_sqrt, w_atan2, w_heading, w_lookat };
    C.cworld = (EmCameraCommitWorld){ &C.cam.rec, &C.player, &C.pool[P_EYE], &C.pool[P_TGT], &C.pool[P_UP],
                                      &C.pool[P_FWD], &C.pool[P_VIEW], &C.pool[P_VIEWT], &C.pool[P_690],
                                      &C.pool[P_694], &C.pool[P_698], &C.pool[P_69C], &C.pool[P_6A0],
                                      &C.scratch, &C.cw, 0 };
}

int em_camera_live_bind(const EmCameraLiveHost *host)
{
    C.bound = 0;
    if (!host || !host->player || !host->hip || !host->euler || !host->carry31F0 || !host->standins ||
        !host->timeline)
        return -1;
    C.host = *host;
    if (!em_collision_world_loaded() || !em_collision_world_sdk() || !em_collision_world_segment())
        return -1;
    if (tables_load() < 0) {
        fprintf(stderr, "em_camera_live: %s missing or malformed (run tools/export_camera_tables.py)\n",
                CAMERA_TABLES_PATH);
        return -1;
    }
    bind_worlds();
    C.bound = 1;
    return 0;
}

/* ======================================================================
 * Entry points
 * ====================================================================== */

static int enter(void)
{
    if (!C.bound) return fail(0x0018B9C0u);
    C.fault = 0;
    C.lworld.fault = 0;
    C.sp.fault_address = 0;
    player_refresh();
    view_load();
    return 0;
}

static int leave(int rc, uint32_t where)
{
    view_store();
    view_publish();
    if (rc < 0) {
        uint32_t at = C.fault ? C.fault : C.lworld.fault ? C.lworld.fault
                    : C.sp.fault_address ? C.sp.fault_address : where;
        C.fault = at;
        em_scene_fault(em_scene_state(), at, EM_SCENE_FAULT_WORKER_FAILED);
        return -1;
    }
    return 0;
}

int em_camera_live_frame(void)
{
    if (enter() < 0) return em_scene_fault(em_scene_state(), 0x0018B9C0u, EM_SCENE_FAULT_NULL_WORKER);
    int rc = em_camleft_0018B9C0(&C.lworld);
    return leave(rc, 0x0018B9C0u);
}

int em_camera_live_commit(int mode)
{
    if (enter() < 0) return em_scene_fault(em_scene_state(), 0x0018C0D0u, EM_SCENE_FAULT_NULL_WORKER);
    return leave(commit(mode), 0x0018C0D0u);
}

int em_camera_live_solve(int style)
{
    if (enter() < 0) return em_scene_fault(em_scene_state(), 0x0018D7B0u, EM_SCENE_FAULT_NULL_WORKER);
    return leave(solve_dispatch(style, NULL), 0x0018D7B0u);
}

int em_camera_live_scripted_retarget(void)
{
    if (enter() < 0) return 0;
    int rc = solve_dispatch(5, NULL);                                 /* 001B7B30: 0018D7B0(cam, 5) */
    if (rc >= 0) rc = solve_dispatch(1, NULL);                        /* 0018D7B0(cam, 1) */
    if (rc >= 0) {
        uint16_t soft = 0x78;                                         /* cam+A0 = 0x78 */
        put(0xA0, &soft, 2);
    }
    return leave(rc, 0x0018D7B0u) < 0 ? 0 : 1;
}

/* ---- 001B0460 ------------------------------------------------------------- */

static const EmCameraLiveRoom *s_room;

static int room_read(void *ctx, uint32_t address, uint32_t *out)
{
    (void)ctx;
    return s_room->read_word(s_room->context, address, out);
}

static int rw_001B0250(void *ctx) { (void)ctx; return s_room->w_001B0250(s_room->context); }

/* 001B0B50: D_008106BE from D_008106C8 (em_player_closure_10_12_19's leaf
 * on a scene view of the two canonical words). */
static int rw_001B0B50(void *ctx)
{
    (void)ctx;
    EmSceneState *s = em_scene_state();
    EmPlayerClosure1019Scene scene;
    memset(&scene, 0, sizeof scene);
    scene.d8106C8 = em_scene_req_u32(s, EM_SCENE_REQ_C8);
    scene.d8106BE = *em_scene_req_at(s, 0x008106BEu);
    em_player_closure1019_001B0B50(&scene);
    *em_scene_req_at(s, 0x008106BEu) = scene.d8106BE;
    return 0;
}

/* 001B0080(D_008101E0, a1) over the canonical camera words
 * (em_script_door_fan's translation on an EmSdfSeat view). */
static int sdf_wrap(void *ctx, float angle, float *result)
{
    (void)ctx;
    *result = em_ee_float(em_player_001B1470(em_ee_bits(angle)));
    return 0;
}
static int sdf_identity(void *ctx, uint32_t m[16]) { return w_identity(ctx, m); }
static int sdf_euler(void *ctx, uint32_t dst[16], const uint32_t src[16], const float angles[4])
{
    uint32_t a[4];
    memcpy(a, angles, sizeof a);
    return w_euler(ctx, dst, src, a);
}
static int sdf_001026A0(void *ctx, float out[4], const uint32_t m[16], const uint32_t v[4])
{
    return sp_001026A0(ctx, out, m, v);
}

static int rw_001B0080(void *ctx, uint32_t camera, float a1)
{
    (void)ctx;
    if (camera != CAM_BASE) return -1;
    EmSceneState *s = em_scene_state();
    EmSdfSeat seat;
    memcpy(&seat.f0C, cb(0x0C), 4);
    memcpy(seat.eye_10, cb(0x10), 16);
    memcpy(seat.tgt_20, cb(0x20), 16);
    memcpy(seat.rot_30, cb(0x30), 16);
    float eye[4], tgt[4], p350[4], s3B50[4];
    memcpy(eye, &C.pool[P_EYE], 16);
    memcpy(tgt, &C.pool[P_TGT], 16);
    memcpy(p350, s_room->d810350, 16);
    memcpy(s3B50, &s->spad3B40[4], 16);
    EmSdfSeatWorld world = { &s->d810700, &s->d810702, p350, s3B50, eye, tgt, C.s3400, C.s3600 };
    EmSdfWorkers workers;
    memset(&workers, 0, sizeof workers);
    workers.w_001B1470 = sdf_wrap;
    workers.w_001029C0 = sdf_identity;
    workers.w_00102C58 = sdf_euler;
    workers.w_001026A0 = sdf_001026A0;
    EmSdfFault f;
    memset(&f, 0, sizeof f);
    if (em_sdf_001B0080(&seat, a1, &world, &workers, &f) < 0) return fail(0x001B0080u);
    memcpy(cb(0x10), seat.eye_10, 16);
    memcpy(cb(0x20), seat.tgt_20, 16);
    memcpy(cb(0x30), seat.rot_30, 16);
    memcpy(&C.pool[P_EYE], eye, 16);
    memcpy(&C.pool[P_TGT], tgt, 16);
    return 0;
}

static int rw_0018C0D0(void *ctx, uint32_t camera, int32_t a1)
{
    (void)ctx;
    if (camera != CAM_BASE) return -1;
    return commit(a1);
}

static int rw_001DD980(void *ctx, const float eye[4], const float target[4])
{
    (void)ctx;
    return em_interaction_projection_publish(&C.projection, eye, target) ? 0 : -1;
}

static int rw_001029C0(void *ctx, float m[16])
{
    (void)ctx;
    return em_owner_services_identity_001029C0(m) == EM_EE_FLOAT_OK ? 0 : -1;
}
static int rw_00102C58(void *ctx, float dst[16], const float src[16], const float angles[4])
{
    (void)ctx;
    return em_owner_services_euler_00102C58(dst, src, angles) == EM_EE_FLOAT_OK ? 0 : -1;
}
static int rw_001026A0(void *ctx, float out[4], const float m[16], const float v[4])
{
    (void)ctx;
    em_effect_original_001026A0(out, m, v);
    return 0;
}
static int rw_001028B8(void *ctx, float out[4], const float a[4], const float b[4])
{
    (void)ctx;
    uint32_t r[4];
    uint32_t x[4], y[4];
    memcpy(x, a, 16);
    memcpy(y, b, 16);
    if (em_vu_vec_bits(EM_VU_ADD, 15, EM_VU_NO_BC, x, y, 0, NULL, r) != EM_EE_FLOAT_OK) return -1;
    memcpy(out, r, 16);
    return 0;
}

int em_camera_live_001B0460(int a0, const EmCameraLiveRoom *room)
{
    if (!room || !room->read_word || !room->w_001B0250 || !room->d810350 || !room->d810370 ||
        !room->d8104E0)
        return em_scene_fault(em_scene_state(), 0x001B0460u, EM_SCENE_FAULT_NULL_WORKER);
    if (enter() < 0) return em_scene_fault(em_scene_state(), 0x001B0460u, EM_SCENE_FAULT_NULL_WORKER);
    EmSceneState *s = em_scene_state();
    s_room = room;
    EmScriptHostWorkers h = C.shw;
    EmScriptHostWorkersWorld *w = &h.world;
    w->read_word = room_read;
    w->d810700 = &s->d810700;
    w->d810701 = &s->d810701;
    w->d810702 = &s->d810702;
    w->d8101E1 = cb(0x01);
    w->d8101E2 = cb(0x02);
    w->d8101E3 = cb(0x03);
    w->d8101E5 = cb(0x05);
    w->d8101E6 = cb(0x06);
    w->d8101E7 = cb(0x07);
    w->d8101E8 = (int16_t *)(void *)cb(0x08);
    w->d8101EC = (float *)(void *)cb(0x0C);
    w->cam_10 = (float *)(void *)cb(0x10);
    w->cam_20 = (float *)(void *)cb(0x20);
    w->d810244 = (float *)(void *)cb(0x64);
    w->d8106C8 = (const int32_t *)(const void *)&s->req[EM_SCENE_REQ_C8];
    w->d8106CD = em_scene_req_at(s, 0x008106CDu);
    w->d275BE0 = &s->d275BE0;
    w->d8106BE = em_scene_req_at(s, 0x008106BEu);
    w->d8104E0 = room->d8104E0;
    w->d810350 = room->d810350;
    w->d810370 = room->d810370;
    w->d8105D0 = (float *)(void *)&C.pool[P_EYE];
    w->d8105E0 = (float *)(void *)&C.pool[P_TGT];
    w->spad3400 = (float *)(void *)C.s3400;
    w->spad3600 = (float *)(void *)C.s3600;
    EmScriptHostWorkersCallees *k = &h.callees;
    k->w_001B0250 = rw_001B0250;
    k->w_001B0B50 = rw_001B0B50;
    k->w_001B0080 = rw_001B0080;
    k->w_0018C0D0 = rw_0018C0D0;
    k->w_001DD980 = rw_001DD980;
    k->w_001029C0 = rw_001029C0;
    k->w_00102C58 = rw_00102C58;
    k->w_001026A0 = rw_001026A0;
    k->w_001028B8 = rw_001028B8;
    int rc = em_script_host_001B0460(&h, a0);
    s_room = NULL;
    if (rc < 0 && !C.fault) C.fault = h.fault_address ? h.fault_address : 0x001B0460u;
    return leave(rc, 0x001B0460u);
}
