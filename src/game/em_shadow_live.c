/* em_shadow_live.c - the player's drop shadow, live (census L29 + L29b).
 * See em_shadow_live.h and docs/SHADOW_ORIGINAL.md "Binding". */
#include "game/em_shadow_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_model.h"
#include "game/em_camera_live.h"
#include "game/em_coll_segment_walkers.h"
#include "game/em_collision_world.h"
#include "game/em_ee_float.h"
#include "game/em_effect_original.h"
#include "game/em_effects_live.h"
#include "game/em_frame.h"
#include "game/em_packet_chain_original.h"
#include "game/em_player.h"
#include "game/em_pose_host_workers.h"
#include "game/em_render_context_live.h"
#include "game/em_scene_bindings.h"
#include "game/em_sdk_math_original.h"
#include "game/em_shadow_actor_route.h"
#include "game/em_shadow_decal_original.h"
#include "game/em_shadow_original.h"

typedef uint32_t u32;

#define CTX EM_RCL_CONTEXT                /* D_00275670's value */
#define NODES 21u                         /* the player's +0x0C */
#define NODE_BYTES 0xD0u
#define ELF_SIZE 1532624u                 /* SCUS_971.12 */
#define ELF_AT(a) ((size_t)(a) - 0x00100000u + 0x300u)
#define CMD_MAX (8u + EM_SHADOW_RECEIVER_MAX)
#define PACKETS_MAX 4u
#define PACKET_BYTES_MAX 0x400u
#define FAN_MAX 16u
#define PASSES 4u                          /* 001DA6A0 calls recorded per frame */

enum { CMD_ALPHA, CMD_BOX, CMD_SIL, CMD_BEGIN, CMD_RECV, CMD_END };

/* One 001DA6A0 call's recorded draw workers: the walk's (an actor's
 * 001BA580, drawn after the walk's owner units) or the post-step's (the
 * player's 0015C160, drawn before its +0x4C). */
typedef struct {
    int walk;
    uint32_t record;
    struct { int op, index; } cmd[CMD_MAX];
    uint32_t cmd_count;
    EmShadowOriginalBox box[2];
    uint32_t box_count;
    int32_t sil_kind;
    float sil_vp[16], sil_nodes[NODES * 16];
    float uv[16], camera[16];
    EmShadowOriginalReceiver recv[EM_SHADOW_RECEIVER_MAX];
    uint32_t recv_count;
} Pass;

static struct {
    int loaded, load_tried, bound;
    u32 fault;
    /* Assets. */
    EmShadowReceivers receivers;
    unsigned receiver_area, receiver_subarea;
    int receiver_selected;
    EmModel proxy, proxy_29;               /* D_0028A490[0x28] (the player), [0x29] (Roger) */
    uint8_t *elf;                               /* the D_0025DAE0 window, placed */
    EmShadowActorRouteTables route_tables;
    /* D_00817FF0 (BSS: zero at boot; never reset). */
    EmShadowOriginalState state;
    /* The call. */
    EmShadowOriginalScene scene;
    EmShadowOriginalPlan plan, aplan;          /* the player's call, the last actor call */
    EmShadowOriginalFault sfault;
    EmShadowOriginalWorkers workers;
    const uint8_t *nodes[NODES];
    /* The recorded 001DA6A0 calls of the frame (their draw workers), in
     * call order; `cur` is the running call's. */
    uint32_t frame;
    int route;
    Pass pass[PASSES];
    uint32_t pass_count, pass_frame;
    Pass *cur;
    /* The 0015BF90 route. The scratchpad block 0x70003600..0x7000363F is
     * this module's one image for 001CD390 (the effect globals), 001F8D30 and
     * 001CE300; 0x700038A0 is its own (written before any read by every
     * routine on this path). */
    EmEffectOriginalGlobals eglobals;
    EmEffectOriginal effect;
    uint32_t s38A0[4];
    const EmPlayerLiveActor *player;            /* the call's record (node_c4) */
    EmShadowActorRoute aroute;
    EmShadowActorRouteWorkers aworkers;
    uint32_t stage[EM_SHADOW_DECAL_STAGE_WORDS];   /* D_008112C0..D_00811CBF */
    EmShadowDecal decal;
    EmShadowDecalWorkers dworkers, dbase;
    /* Another owner's 001CE300 (an effect node's decal, 001F77B0): the
     * same clipper buffers (stage) and scratchpad block, the packet chain's
     * own adapters (no shadow sample is recorded). */
    EmShadowDecal effect_decal;
    uint32_t effect_decals;
    /* Another owner's 001F9100 (AREA01's class-2 owners through 001B5360):
     * the same route and decal kernels, scratchpad block and stage
     * buffers; its packets are recorded apart and its fans' triangles
     * (the decal TEX0's) counted for this frame's page. */
    EmShadowActorRoute oroute;
    EmShadowActorRouteWorkers oworkers;
    EmShadowDecal odecal;
    EmShadowDecalWorkers odworkers;
    uint32_t os38A0[4];
    struct { uint8_t *bytes; int32_t count; } opk[PACKETS_MAX];
    uint32_t opk_count;
    uint32_t owner_frame, owner_triangles;
    struct { uint8_t *bytes; int32_t count; } pk[PACKETS_MAX];
    uint32_t pk_count;
    uint32_t fan_n[2], fan_count;
    uint64_t fan_tex0;
    /* The level smoke's views. */
    EmShadowLiveLog log;
    EmShadowLiveActorLog alog;
    EmShadowLiveSample sample, asample;
    int asampled;
    uint8_t sample_packets[PACKETS_MAX][PACKET_BYTES_MAX];
    int sampled;
} S;

static u32 rd32(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u32 fbits(float f) { return em_ee_bits(f); }

static int fail(u32 address, const char *why)
{
    if (!S.fault) {
        S.fault = address ? address : 0x0015C160u;
        fprintf(stderr, "shadow: %s (at %08X)\n", why, (unsigned)S.fault);
    }
    return -1;
}

uint32_t em_shadow_live_fault(void) { return S.fault; }
int em_shadow_live_bound(void) { return S.bound; }

/* ------------------------------------------------------------ the assets */

/* D_0025DAE0 / D_0025DAF0 from the effect-table export's window
 * (tools/export_effect_tables.py), placed in an ELF-sized image for
 * em_shadow_actor_route_load_tables. */
static int load_route_tables(void)
{
    if (em_effects_live_load() < 0) return -1;
    const uint8_t *w = em_effects_live_elf(0x0025DAE0u, 0x20);
    if (!w) return -1;
    S.elf = calloc(1, ELF_SIZE);
    if (!S.elf) return -1;
    memcpy(S.elf, "\x7F" "ELF", 4);
    memcpy(S.elf + ELF_AT(0x0025DAE0u), w, 0x20);
    return em_shadow_actor_route_load_tables(S.elf, ELF_SIZE, &S.route_tables);
}

static int load(void)
{
    if (S.loaded) return 0;
    if (S.load_tried) return -1;
    S.load_tried = 1;
    if (!S.receiver_selected && em_shadow_live_select_area(11, 0) < 0) {
        fprintf(stderr, "shadow: %s is missing or invalid (run ../Extermination/tools/"
                        "export_shadow_receivers.py)\n", EM_SHADOW_LIVE_RECEIVERS_PATH);
        return fail(0x001D5C80u, "no static-object receivers");
    }
    if (em_model_load(&S.proxy, EM_SHADOW_LIVE_PROXY_PATH) != 0 || !S.proxy.verts || !S.proxy.indices ||
        S.proxy.index_count % 3) {
        fprintf(stderr, "shadow: %s is missing or invalid (run ../Extermination/tools/"
                        "export_shadow_proxy.py)\n", EM_SHADOW_LIVE_PROXY_PATH);
        return fail(0x001D9EE0u, "no shadow proxy mesh D_0028A490[0x28]");
    }
    if (em_model_load(&S.proxy_29, EM_SHADOW_LIVE_PROXY_29_PATH) != 0 || !S.proxy_29.verts ||
        !S.proxy_29.indices || S.proxy_29.index_count % 3) {
        fprintf(stderr, "shadow: %s is missing or invalid (run ../Extermination/tools/"
                        "export_shadow_proxy.py --kind 0x29)\n", EM_SHADOW_LIVE_PROXY_29_PATH);
        return fail(0x001D9EE0u, "no shadow proxy mesh D_0028A490[0x29]");
    }
    if (load_route_tables() < 0) {
        fprintf(stderr, "shadow: D_0025DAE0 / D_0025DAF0 are not in assets/effect_tables.emet "
                        "(re-run tools/export_effect_tables.py)\n");
        return fail(0x001F9100u, "no decal colour / facing tables");
    }
    S.loaded = 1;
    return 0;
}

int em_shadow_live_select_area(unsigned area, unsigned subarea)
{
    const char *path = NULL;
    if (area == 11 && subarea == 0) path = EM_SHADOW_LIVE_RECEIVERS_PATH;
    if (area == 1 && subarea == 0) path = EM_SHADOW_LIVE_AREA01_RECEIVERS_PATH;
    if (!path || S.fault) return -1;
    for (u32 k = 0; k < S.pass_count; ++k)
        if (S.pass[k].cmd_count && S.pass_frame == em_frame_counter()) return -1;
    EmShadowReceivers next = {0};
    if (em_shadow_receivers_load(&next, path) < 0) return -1;
    em_shadow_receivers_free(&S.receivers);
    S.receivers = next;
    S.receiver_area = area; S.receiver_subarea = subarea; S.receiver_selected = 1;
    S.pass_count = 0; S.cur = NULL;
    return 0;
}

/* ------------------------------------------------------------ the views */

/* A node record (NODE_BYTES) the player's +0x110 word `address` names, in
 * the pose owner's regions (em_player_record_pose), or NULL. */
static uint8_t *node_record(u32 address)
{
    struct EmPoseHost *h = player_pose_record_host();
    if (!h) return NULL;
    for (unsigned k = 0; k < h->region_count; ++k) {
        const EmPoseRegion *r = &h->region[k];
        if (r->bytes && address >= r->address && NODE_BYTES <= r->size &&
            address - r->address <= r->size - NODE_BYTES)
            return r->bytes + (address - r->address);
    }
    return NULL;
}

static int load_nodes(const EmPlayerLiveActor *player)
{
    for (unsigned i = 0; i < NODES; ++i) {
        S.nodes[i] = node_record(rd32(player->bytes + 0x110 + 4 * i));
        if (!S.nodes[i]) return fail(0x001DA6A0u, "a player node record is not mapped");
    }
    return 0;
}

/* A new call's recorded passes (this frame's list; an earlier frame's
 * unflushed calls are dropped, as before). */
static int begin_pass(int walk, uint32_t record)
{
    const uint32_t now = em_frame_counter();
    if (S.pass_frame != now) {
        S.pass_frame = now;
        S.pass_count = 0;
    }
    if (S.pass_count >= PASSES) return fail(0x001DA6A0u, "more 001DA6A0 calls in one frame than PASSES");
    Pass *p = &S.pass[S.pass_count++];
    p->walk = walk;
    p->record = record;
    p->cmd_count = p->box_count = p->recv_count = 0;
    p->sil_kind = 0;
    S.cur = p;
    return 0;
}

static int copy_view(float out[16], u32 address)
{
    const uint8_t *b = em_rcl_bytes(address, 0x40);
    if (!b) return -1;
    memcpy(out, b, 0x40);
    return 0;
}

/* 001DA6A0's scene: the render context as the frame head left it, the
 * scratchpad camera D_70003AC0 at the call, the live camera's D_00810610,
 * the area bytes, and the static-object grid of the receiver asset
 * (001D52E0's ctx+0x140..+0x164, docs/SHADOW_ORIGINAL.md "Assets"). */
static int load_scene(void)
{
    EmShadowOriginalScene *sc = &S.scene;
    memset(sc, 0, sizeof *sc);
    const uint8_t *zoom = em_rcl_bytes(CTX + 0x2468, 4);
    const uint8_t *view = em_camera_live_bound() ? em_camera_live_bytes(0x00810610u, 0x40) : NULL;
    if (copy_view(sc->clip_2240, CTX + 0x2240) < 0 || copy_view(sc->proj_2340, CTX + 0x2340) < 0 ||
        copy_view(sc->view_2380, CTX + 0x2380) < 0 || copy_view(sc->camera_3AC0, 0x70003AC0u) < 0 ||
        !zoom || !view)
        return fail(0x001DA6A0u, "the render context or the camera views are not bound");
    memcpy(sc->view_810610, view, 0x40);
    memcpy(&sc->zoom_2468, zoom, 4);
    const EmSceneState *st = em_scene_state();
    sc->area_700 = st->d810700;
    sc->sub_701 = st->d810701;
    if (!S.receiver_selected || sc->area_700 != S.receiver_area || sc->sub_701 != S.receiver_subarea)
        return fail(0x0028A5A0u, "the selected shadow receivers do not match the active area");
    em_shadow_receivers_scene(&S.receivers, sc);
    return 0;
}

/* ------------------------------------------------ 001DA6A0's draw workers */

static int push(int op, int index)
{
    Pass *p = S.cur;
    if (!p || p->cmd_count >= CMD_MAX) return -1;
    p->cmd[p->cmd_count].op = op;
    p->cmd[p->cmd_count].index = index;
    p->cmd_count++;
    return 0;
}

static int w_bounds(void *ctx, int32_t id, float lo[3], float hi[3])
{
    (void)ctx;
    return em_shadow_receivers_bounds(&S.receivers, id, lo, hi);
}

static int w_alpha_clear(void *ctx)
{
    (void)ctx;
    return push(CMD_ALPHA, 0);
}

static int w_box(void *ctx, const EmShadowOriginalBox *box)
{
    (void)ctx;
    Pass *p = S.cur;
    if (!p || !box || p->box_count >= 2 || !em_shadow_receivers_box(&S.receivers, box->model)) return -1;
    p->box[p->box_count] = *box;
    return push(CMD_BOX, (int)p->box_count++);
}

/* 001D9EE0: the proxy D_0028A490[kind] (kind 0x28, the player:
 * assets/player_shadow.emdl; kind 0x29, Roger: assets/roger_shadow.emdl;
 * no other kind is exported); its bones are the actor's node +0x90
 * matrices as they stand now. */
static const EmModel *proxy_of(int32_t kind)
{
    if (kind == EM_SHADOW_ORIGINAL_KIND_PLAYER) return &S.proxy;
    if (kind == EM_SHADOW_LIVE_KIND_ROGER) return &S.proxy_29;
    return NULL;
}

static int w_silhouette(void *ctx, int32_t kind, const float vp[16])
{
    (void)ctx;
    Pass *p = S.cur;
    if (!p || !proxy_of(kind) || !vp) return -1;
    p->sil_kind = kind;
    memcpy(p->sil_vp, vp, sizeof p->sil_vp);
    for (unsigned i = 0; i < NODES; ++i) memcpy(p->sil_nodes + 16 * i, S.nodes[i] + 0x90, 0x40);
    return push(CMD_SIL, 0);
}

static int w_receiver_begin(void *ctx, const float uv[16])
{
    (void)ctx;
    Pass *p = S.cur;
    if (!p || !uv) return -1;
    memcpy(p->uv, uv, sizeof p->uv);
    memcpy(p->camera, S.scene.camera_3AC0, sizeof p->camera);
    return push(CMD_BEGIN, 0);
}

static int w_receiver(void *ctx, const EmShadowOriginalReceiver *r)
{
    (void)ctx;
    Pass *p = S.cur;
    if (!p || !r || r->cls > 2u || p->recv_count >= EM_SHADOW_RECEIVER_MAX ||
        !em_shadow_receivers_object(&S.receivers, r->id))
        return -1;
    p->recv[p->recv_count] = *r;
    return push(CMD_RECV, (int)p->recv_count++);
}

static int w_receiver_end(void *ctx)
{
    (void)ctx;
    return push(CMD_END, 0);
}

/* --------------------------------------------- the 0015BF90 route workers */

static int w_node_c4(void *ctx, uint32_t slot, uint32_t word, uint32_t *value)
{
    (void)ctx;
    const EmPlayerLiveActor *p = S.player;
    if (!p || (slot != 0x154u && slot != 0x158u) || rd32(p->bytes + slot) != word) return -1;
    const uint8_t *n = node_record(word);
    if (!n) return -1;
    *value = rd32(n + 0xC4);
    return 0;
}

static int w_segment(void *ctx, const uint32_t from[4], const uint32_t to[4], int mask, int id,
                     int32_t *result, uint32_t point[4], uint32_t normal[3])
{
    (void)ctx;
    const EmCollSegment *seg = em_collision_world_segment();
    if (!seg) return -1;
    float a[3], b[3];
    for (unsigned i = 0; i < 3; ++i) {
        a[i] = em_ee_float(from[i]);
        b[i] = em_ee_float(to[i]);
    }
    const int r = em_coll_segment_0019A570(seg, a, b, (unsigned)mask, id);
    if (r < 0) return -1;
    *result = r;
    if (r != 0) {
        EmCollSegmentHit h;
        if (em_coll_segment_hit(seg, &h) < 0) return -1;
        for (unsigned i = 0; i < 3; ++i) {
            point[i] = fbits(h.point[i]);
            normal[i] = fbits(h.record_normal[i]);
        }
        /* 0x700031BC: the walkers write only x, y, z (em_coll_segment_walkers);
         * the word is 0 in every capture and nothing on this path reads it
         * (SHADOW_ACTOR_ROUTE.md limit L2): it only reaches 0x700038AC. */
        point[3] = 0;
    }
    S.sample.segment_result = r;
    memcpy(S.sample.segment_point, point, 16);
    memcpy(S.sample.segment_normal, normal, 12);
    return 0;
}

static int w_atan2(void *ctx, uint32_t y, uint32_t x, uint32_t *out)
{
    (void)ctx;
    EmSdkMathContext *m = em_collision_world_sdk();
    float r;
    uint32_t f = 0;
    if (!m || em_sdk_math_original_0011E620(m->tables, &m->world, &m->workers, em_ee_float(y),
                                            em_ee_float(x), &r, &f) < 0)
        return -1;
    *out = fbits(r);
    return 0;
}

static int w_look_at(void *ctx, uint32_t out[16], const uint32_t v[4])
{
    (void)ctx;
    float m[16], x[4];
    memcpy(x, v, sizeof x);
    if (em_effect_original_001CD390(&S.effect, m, x) < 0) return -1;
    memcpy(out, m, sizeof m);
    return 0;
}

static int w_submit(void *ctx, int32_t tag, const uint32_t corners[16], uint64_t tex0, uint32_t rgba)
{
    S.sample.submit_tag = tag;
    memcpy(S.sample.submit_corners, corners, sizeof S.sample.submit_corners);
    S.sample.submit_tex0 = tex0;
    S.sample.submit_rgba = rgba;
    S.sample.submitted = 1;
    return em_shadow_decal_w_001CE300(ctx, tag, corners, tex0, rgba);
}

/* The packet-chain adapters with 001CB5F0 recording the packets 001CE300
 * opens (the fans, then the TEX0 packet). */
static int d_001CD370(void *ctx, int32_t a0, uint32_t m[16])
{
    (void)ctx;
    return S.dbase.w_001CD370(S.dbase.ctx, a0, m);
}

static int d_001CB5F0(void *ctx, uint32_t table, int32_t id, int32_t count, uint8_t **bytes)
{
    (void)ctx;
    const int r = S.dbase.w_001CB5F0(S.dbase.ctx, table, id, count, bytes);
    if (r < 0) return r;
    if (S.pk_count >= PACKETS_MAX || !bytes || !*bytes || count <= 0) return -1;
    S.pk[S.pk_count].bytes = *bytes;
    S.pk[S.pk_count].count = count;
    S.pk_count++;
    return r;
}

static int d_001CB900(void *ctx, uint32_t table, int32_t id, int32_t mode)
{
    (void)ctx;
    return S.dbase.w_001CB900(S.dbase.ctx, table, id, mode);
}

static int d_fog(void *ctx, uint32_t out[4])
{
    (void)ctx;
    return S.dbase.fog(S.dbase.ctx, out);
}

/* One fan packet (3n + 2 quadwords: the DIRECT VIF code, the GIF tag, n
 * vertices of ST / RGBAQ / XYZF2): it must be the one 001CE300 writes
 * (NLOOP n, EOP, PRE, PRIM 0x7D, PACKED, three registers 0x412) with no ADC
 * vertex, so that the chain page draws exactly its n - 2 triangles
 * (em_shadow_live_page_drew checks the count). */
static int check_fan(const uint8_t *p, int32_t count, uint32_t *n)
{
    if (count < 5 || (count - 2) % 3) return -1;
    const u32 verts = (u32)(count - 2) / 3u;
    if (verts > FAN_MAX || rd32(p + 12) != (0x50000000u | (u32)(count - 1))) return -1;
    const uint8_t *tag = p + 16;
    const u32 w0 = rd32(tag), w1 = rd32(tag + 4), regs = rd32(tag + 8);
    if ((w0 & 0x7FFFu) != verts || !(w0 & 0x8000u) || ((w1 >> 14) & 1u) != 1u ||
        ((w1 >> 15) & 0x7FFu) != 0x7Du || ((w1 >> 26) & 3u) != 0u || (w1 >> 28) != 3u ||
        (regs & 0xFFFu) != 0x412u)
        return -1;
    for (u32 i = 0; i < verts; ++i)
        if (rd32(p + 32 + 48 * i + 44) & 0x8000u) return -1;    /* ADC */
    *n = verts;
    return 0;
}

/* ------------------------------------------------------------ bind */

int em_shadow_live_bind(void)
{
    if (S.fault) return -1;
    if (load() < 0) return -1;
    if (!em_rcl_bound() || !em_rcl_packet_chain())
        return fail(0x001CE300u, "the render context is not bound");
    S.workers = (EmShadowOriginalWorkers){ NULL, w_bounds, w_alpha_clear, w_box, w_silhouette,
                                           w_receiver_begin, w_receiver, w_receiver_end };
    memset(&S.eglobals, 0, sizeof S.eglobals);
    memset(&S.effect, 0, sizeof S.effect);
    S.effect.globals = &S.eglobals;
    em_shadow_decal_bind_packet_chain(&S.dbase, em_rcl_packet_chain());
    S.dworkers = (EmShadowDecalWorkers){ NULL, d_001CD370, d_001CB5F0, d_001CB900, d_fog };
    memset(&S.decal, 0, sizeof S.decal);
    memset(&S.effect_decal, 0, sizeof S.effect_decal);
    S.decal.stage = S.stage;
    S.decal.workers = &S.dworkers;
    S.aworkers = (EmShadowActorRouteWorkers){ NULL, w_node_c4, w_segment, w_atan2, w_look_at, w_submit };
    memset(&S.aroute, 0, sizeof S.aroute);
    S.aroute.tables = &S.route_tables;
    S.aroute.workers = &S.aworkers;
    S.frame = 0;
    S.route = 0;
    S.pass_count = S.pass_frame = S.fan_count = 0;
    S.cur = NULL;
    S.bound = 1;
    return 0;
}

/* ------------------------------------------------------------ the call */

static void sample_common(EmShadowLiveSample *s, uint32_t record, const uint8_t *bytes, uint32_t size,
                          const uint8_t *const *nodes, int route)
{
    memset(s, 0, sizeof *s);
    s->frame = em_frame_counter();
    s->route = route;
    s->record = record;
    s->record_size = size < sizeof s->player ? size : (uint32_t)sizeof s->player;
    memcpy(s->player, bytes, s->record_size);
    for (unsigned i = 0; i < NODES; ++i)
        if (nodes[i]) memcpy(s->nodes + NODE_BYTES * i, nodes[i], NODE_BYTES);
    const u32 views[5] = { CTX + 0x2240, CTX + 0x2340, CTX + 0x2380, 0x70003AC0u, 0 };
    uint32_t *dst[4] = { s->clip_2240, s->proj_2340, s->view_2380, s->camera_3AC0 };
    for (unsigned i = 0; views[i]; ++i) {
        const uint8_t *b = em_rcl_bytes(views[i], 0x40);
        if (b) memcpy(dst[i], b, 0x40);
    }
    const uint8_t *view = em_camera_live_bound() ? em_camera_live_bytes(0x00810610u, 0x40) : NULL;
    if (view) memcpy(s->view_810610, view, 0x40);
    const uint8_t *zoom = em_rcl_bytes(CTX + 0x2468, 4), *fog = em_rcl_bytes(CTX + 0xA0, 16);
    if (zoom) s->zoom_2468 = rd32(zoom);
    if (fog) memcpy(s->fog_A0, fog, 16);
    const EmSceneState *st = em_scene_state();
    s->area_700 = st->d810700;
    s->sub_701 = st->d810701;
    s->spad3B8D = st->spad3B8D;
    memcpy(s->ff0_before, S.state.d817FF0, 16);
}

/* The player's node records as the route-2 sample reads them (without the
 * route-1 load's fault). */
static void player_nodes(const EmPlayerLiveActor *player, const uint8_t *out[NODES])
{
    for (unsigned i = 0; i < NODES; ++i) out[i] = node_record(rd32(player->bytes + 0x110 + 4 * i));
}

static int call_001DA6A0(const EmPlayerLiveActor *player)
{
    if (load_nodes(player) < 0 || load_scene() < 0) return -1;
    sample_common(&S.sample, EM_SHADOW_LIVE_PLAYER, player->bytes, EM_PLAYER_ACTOR_SIZE, S.nodes,
                  EM_SHADOW_ROUTE_001DA6A0);
    if (begin_pass(0, EM_SHADOW_LIVE_PLAYER) < 0) return -1;
    S.sfault = (EmShadowOriginalFault){ 0, 0 };
    const int r = em_shadow_original_001DA6A0(player->bytes, S.nodes, NODES, &S.scene, &S.state, &S.plan,
                                              &S.workers, &S.sfault);
    if (r < 0) return fail(S.sfault.address, "001DA6A0 faulted");
    S.sample.plan = &S.plan;
    S.sample.plan_bytes = sizeof S.plan;
    S.log.drawn = r;
    S.log.kind = S.plan.kind;
    S.log.receivers = S.cur->recv_count;
    for (u32 i = 0; i < S.cur->recv_count; ++i) S.log.receivers_cls2 += S.cur->recv[i].cls == 2u;
    return 0;
}

static int call_0015BF90(const EmPlayerLiveActor *player)
{
    struct EmPoseHost *h = player_pose_record_host();
    const uint8_t *camera = em_rcl_bytes(0x70003AC0u, 0x40);
    if (!h || !h->globals || !h->globals->spad38B0 || !h->globals->spad3A20 || !camera)
        return fail(0x0015BF90u, "the scratchpad views are not bound");
    const uint8_t *nodes[NODES];
    player_nodes(player, nodes);
    sample_common(&S.sample, EM_SHADOW_LIVE_PLAYER, player->bytes, EM_PLAYER_ACTOR_SIZE, nodes,
                  EM_SHADOW_ROUTE_0015BF90);
    S.aroute.scratch = (EmShadowActorRouteScratch){ S.s38A0, h->globals->spad38B0, h->globals->spad3A20,
                                                    S.eglobals.spad3600, &em_scene_state()->spad3B8D,
                                                    (const uint32_t *)camera };
    /* The route passes one context to every worker: the decal, which
     * em_shadow_decal_w_001CE300 (the submit) takes; node_c4 reads the
     * player from S.player. */
    S.aworkers.context = &S.decal;
    S.player = player;
    S.decal.scratch = (EmShadowDecalScratch){ S.eglobals.spad3600, (const uint32_t *)camera };
    S.pk_count = 0;
    S.fan_count = 0;
    S.fan_tex0 = 0;
    const int r = em_shadow_actor_route_0015BF90(&S.aroute, player);
    if (r < 0) {
        if (S.decal.fault.code) return fail(S.decal.fault.address, "001CE300 faulted");
        return fail(S.aroute.fault.address, "0015BF90 faulted");
    }
    /* 001CE300's packets in append order: the fans of pass 0 and pass 1
     * (3n + 2 quadwords each), then the TEX0 packet (3). */
    S.sample.packet_count = S.pk_count;
    for (u32 i = 0; i < S.pk_count; ++i) {
        const u32 bytes = 16u * (u32)S.pk[i].count;
        if (bytes > PACKET_BYTES_MAX) return fail(0x001CE300u, "a decal packet over the sample bound");
        memcpy(S.sample_packets[i], S.pk[i].bytes, bytes);
        S.sample.packet[i] = S.sample_packets[i];
        S.sample.packet_qwords[i] = (u32)S.pk[i].count;
    }
    if (!S.sample.submitted) return 0;           /* 0x19 exit, no hit, or a clockwise quad */
    if (S.sample.submit_tag != 1 || S.pk_count < 1 || S.pk[S.pk_count - 1].count != 3)
        return fail(0x001CE300u, "a decal packet sequence the renderer does not implement");
    const uint8_t *tex = S.pk[S.pk_count - 1].bytes;
    if (rd32(tex + 12) != 0x50000002u || rd32(tex + 40) != 0x06u)
        return fail(0x001CB950u, "the decal TEX0 packet is not the TEX0_1 A+D write");
    S.fan_tex0 = (uint64_t)rd32(tex + 32) | (uint64_t)rd32(tex + 36) << 32;
    if (S.fan_tex0 != EM_SHADOW_DECAL_TEX0) return fail(0x001CB950u, "the decal TEX0 is not 001F8D30's");
    for (u32 i = 0; i + 1 < S.pk_count; ++i) {
        if (S.fan_count >= 2) return fail(0x001CE300u, "more than two decal fans");
        if (check_fan(S.pk[i].bytes, S.pk[i].count, &S.fan_n[S.fan_count]) < 0)
            return fail(0x001CE300u, "a decal fan the renderer does not implement");
        S.fan_count++;
    }
    S.log.decal_fans = S.fan_count;
    for (u32 i = 0; i < S.fan_count; ++i) S.log.decal_vertices += S.fan_n[i];
    return 0;
}

int em_shadow_live_0015C160(const EmPlayerLiveActor *player, int route)
{
    if (S.fault) return -1;
    if (!S.bound || !player) return fail(0x0015C160u, "the shadow is not bound");
    S.frame = em_frame_counter();
    S.route = route;
    S.fan_count = 0;
    const EmShadowLiveLog keep = S.log;
    memset(&S.log, 0, sizeof S.log);
    S.log.frame = S.frame;
    S.log.route = route;
    S.log.calls = keep.calls + 1;
    S.log.drawn_total = keep.drawn_total;
    S.log.decal_total = keep.decal_total;
    S.sampled = 1;
    if (route == EM_SHADOW_ROUTE_001DA6A0) return call_001DA6A0(player);
    if (route == EM_SHADOW_ROUTE_0015BF90) return call_0015BF90(player);
    return fail(0x0015C160u, "not a shadow route");
}

/* ------------------------------------------------------------ the draws */

/* 001BA580's 001DA6A0(actor) during the owner walk (Roger: kind 0x29). */
int em_shadow_live_actor_001DA6A0(uint32_t record, const uint8_t *bytes, uint32_t size,
                                  const uint8_t *const nodes[], uint32_t node_count)
{
    if (S.fault) return -1;
    if (!S.bound) return fail(0x001DA6A0u, "the shadow is not bound");
    if (!bytes || size < 0x100u || !nodes || node_count != NODES)
        return fail(0x001DA6A0u, "an actor record or its node records are not mapped");
    for (unsigned i = 0; i < NODES; ++i) {
        if (!nodes[i]) return fail(0x001DA6A0u, "an actor node record is not mapped");
        S.nodes[i] = nodes[i];
    }
    if (load_scene() < 0) return -1;
    const EmShadowLiveActorLog keep = S.alog;
    memset(&S.alog, 0, sizeof S.alog);
    S.alog.frame = em_frame_counter();
    S.alog.record = record;
    S.alog.calls = keep.calls + 1;
    S.alog.drawn_total = keep.drawn_total;
    sample_common(&S.asample, record, bytes, size, S.nodes, EM_SHADOW_ROUTE_001DA6A0);
    S.asampled = 1;
    if (begin_pass(1, record) < 0) return -1;
    S.sfault = (EmShadowOriginalFault){ 0, 0 };
    const int r = em_shadow_original_001DA6A0(bytes, S.nodes, NODES, &S.scene, &S.state, &S.aplan, &S.workers,
                                              &S.sfault);
    if (r < 0) return fail(S.sfault.address, "001DA6A0 faulted (an actor)");
    S.asample.plan = &S.aplan;
    S.asample.plan_bytes = sizeof S.aplan;
    S.alog.drawn = r;
    S.alog.kind = S.aplan.kind;
    S.alog.receivers = S.cur->recv_count;
    for (u32 i = 0; i < S.cur->recv_count; ++i) S.alog.receivers_cls2 += S.cur->recv[i].cls == 2u;
    return 0;
}

/* ------------------------------------------------------------ the draws */

static int flush_pass(EmGfx *gfx, const float viewproj[16], Pass *p)
{
    for (u32 i = 0; i < p->cmd_count; ++i) {
        const int index = p->cmd[i].index;
        int r = -1;
        u32 at = 0x001DA6A0u;
        switch (p->cmd[i].op) {
        case CMD_ALPHA:
            at = 0x001DA290u;
            r = em_gfx_shadow_alpha_clear(gfx);
            break;
        case CMD_BOX: {
            at = 0x001DA310u;
            const EmShadowOriginalBox *b = &p->box[index];
            const EmShadowReceiverObject *m = em_shadow_receivers_box(&S.receivers, b->model);
            if (!m) break;
            const EmGfxShadowStrips st = { m->qw3, EM_GFX_SHADOW_BATCH * m->batches };
            r = em_gfx_shadow_box(gfx, &st, b->world, b->clip_pass, b->rgbaq, viewproj);
            break;
        }
        case CMD_SIL: {
            at = 0x001D9EE0u;
            const EmModel *m = proxy_of(p->sil_kind);
            if (!m) break;
            r = em_gfx_shadow_silhouette(gfx, m->verts, m->vert_count, m->indices, m->index_count, p->sil_nodes,
                                         NODES, p->sil_vp);
            break;
        }
        case CMD_BEGIN:
            at = 0x001D4CD0u;
            r = em_gfx_shadow_receiver_begin(gfx, p->uv, p->camera, viewproj);
            break;
        case CMD_RECV: {
            at = 0x001D4FB0u;
            const EmShadowReceiverObject *o = em_shadow_receivers_object(&S.receivers, p->recv[index].id);
            if (!o) break;
            const EmGfxShadowStrips st = { o->qw3, EM_GFX_SHADOW_BATCH * o->batches };
            r = em_gfx_shadow_receiver(gfx, &st, p->recv[index].cls);
            break;
        }
        case CMD_END:
            at = 0x001D1FF0u;
            r = em_gfx_shadow_receiver_end(gfx);
            break;
        }
        if (r < 0) return fail(at, "a shadow pass could not be drawn exactly");
    }
    p->cmd_count = 0;
    return 0;
}

/* This frame's recorded calls of one kind (walk: the actors' 001BA580
 * calls; else the post-step's), in call order. */
static int flush_calls(EmGfx *gfx, const float viewproj[16], int walk)
{
    if (S.fault) return -1;
    if (!S.bound || S.pass_frame != em_frame_counter()) return 0;
    for (u32 k = 0; k < S.pass_count; ++k) {
        Pass *p = &S.pass[k];
        if (p->walk != walk || !p->cmd_count) continue;
        if (!gfx || !viewproj) return fail(0x001DA6A0u, "no frame to draw the shadow in");
        if (flush_pass(gfx, viewproj, p) < 0) return -1;
        if (walk) {
            if (p->record == S.alog.record && S.alog.frame == em_frame_counter()) S.alog.flushed = 1;
            S.alog.drawn_total++;
        } else {
            S.log.flushed = 1;
            S.log.drawn_total++;
        }
    }
    return 0;
}

int em_shadow_live_flush_walk(EmGfx *gfx, const float viewproj[16])
{
    return flush_calls(gfx, viewproj, 1);
}

int em_shadow_live_flush(EmGfx *gfx, const float viewproj[16])
{
    if (S.fault) return -1;
    if (!S.bound || S.frame != em_frame_counter() || S.route != EM_SHADOW_ROUTE_001DA6A0) return 0;
    return flush_calls(gfx, viewproj, 0);
}

int em_shadow_live_page_drew(uint32_t decal_triangles)
{
    if (S.fault) return -1;
    u32 want = 0;
    if (S.bound && S.frame == em_frame_counter() && S.route == EM_SHADOW_ROUTE_0015BF90)
        for (u32 i = 0; i < S.fan_count; ++i) want += S.fan_n[i] - 2u;
    const u32 owners = S.owner_frame == em_frame_counter() ? S.owner_triangles : 0u;
    want += owners;
    S.owner_triangles = 0;
    if (decal_triangles != want)
        return fail(0x001CE300u, "the chain page did not draw exactly the decal's triangles");
    if (want) {
        S.fan_count = 0;
        S.log.decal_flushed = 1;
        S.log.decal_total++;
    }
    return 0;
}

/* 001CE300 for an effect node (001F77B0, the 0x80000043 death decal): the
 * kernel over the render context's packet chain, page D_007635C0 slot 0,
 * the stage buffers D_008112C0 and the scratchpad block 0x70003600 this
 * module keeps, the camera rows 0x70003AC0 of the render context. Its fans
 * carry their own TEX0, so the page's decal count (em_shadow_live_page_
 * drew, the 0015BF90 TEX0's triangles only) does not include them. */
int em_shadow_live_effect_001CE300(int32_t tag, const uint32_t corners[16], uint64_t tex0, uint32_t rgba)
{
    if (S.fault) return -1;
    if (!S.bound) return fail(0x001CE300u, "the shadow is not bound");
    const uint8_t *camera = em_rcl_bytes(0x70003AC0u, 0x40);
    if (!camera) return fail(0x001CE300u, "the camera rows 0x70003AC0 are not bound");
    S.effect_decal.stage = S.stage;
    S.effect_decal.workers = &S.dbase;
    S.effect_decal.scratch = (EmShadowDecalScratch){ S.eglobals.spad3600, (const uint32_t *)camera };
    if (em_shadow_decal_001CE300(&S.effect_decal, tag, corners, tex0, rgba) < 0)
        return fail(S.effect_decal.fault.address ? S.effect_decal.fault.address : 0x001CE300u,
                    "001CE300 (an effect's decal) faulted");
    S.effect_decals++;
    return 0;
}

uint32_t em_shadow_live_effect_decals(void) { return S.effect_decals; }

/* --------------------------------- another owner's 001F9100 decal */

static int od_001CB5F0(void *ctx, uint32_t table, int32_t id, int32_t count, uint8_t **bytes)
{
    (void)ctx;
    const int r = S.dbase.w_001CB5F0(S.dbase.ctx, table, id, count, bytes);
    if (r < 0) return r;
    if (S.opk_count >= PACKETS_MAX || !bytes || !*bytes || count <= 0) return -1;
    S.opk[S.opk_count].bytes = *bytes;
    S.opk[S.opk_count].count = count;
    S.opk_count++;
    return r;
}

static int ow_submit(void *ctx, int32_t tag, const uint32_t corners[16], uint64_t tex0, uint32_t rgba)
{
    return em_shadow_decal_w_001CE300(ctx, tag, corners, tex0, rgba);
}

int em_shadow_live_owner_001F9100(const uint32_t owner[4], const uint32_t point[4], const uint32_t normal[4],
                                  uint32_t f12)
{
    if (S.fault) return -1;
    if (!S.bound) return fail(0x001F9100u, "the shadow is not bound");
    const uint8_t *camera = em_rcl_bytes(0x70003AC0u, 0x40);
    if (!camera) return fail(0x001F9100u, "the camera rows 0x70003AC0 are not bound");
    const uint32_t now = em_frame_counter();
    if (S.owner_frame != now) {
        S.owner_frame = now;
        S.owner_triangles = 0;
    }
    S.odworkers = (EmShadowDecalWorkers){ NULL, d_001CD370, od_001CB5F0, d_001CB900, d_fog };
    S.odecal.stage = S.stage;
    S.odecal.workers = &S.odworkers;
    S.odecal.scratch = (EmShadowDecalScratch){ S.eglobals.spad3600, (const uint32_t *)camera };
    S.oworkers = (EmShadowActorRouteWorkers){ &S.odecal, NULL, NULL, w_atan2, w_look_at, ow_submit };
    S.oroute.tables = &S.route_tables;
    S.oroute.workers = &S.oworkers;
    S.oroute.scratch = (EmShadowActorRouteScratch){ S.os38A0, NULL, NULL, S.eglobals.spad3600, NULL,
                                                    (const uint32_t *)camera };
    S.opk_count = 0;
    if (em_shadow_actor_route_001F9100(&S.oroute, owner, point, normal, f12) < 0) {
        if (S.odecal.fault.code) return fail(S.odecal.fault.address, "001CE300 (an owner's decal) faulted");
        return fail(S.oroute.fault.address ? S.oroute.fault.address : 0x001F9100u, "001F9100 faulted");
    }
    if (!S.opk_count) return 0;                  /* a clockwise quad: nothing submitted */
    const uint8_t *tex = S.opk[S.opk_count - 1].bytes;
    if (S.opk[S.opk_count - 1].count != 3 || rd32(tex + 12) != 0x50000002u || rd32(tex + 40) != 0x06u ||
        ((uint64_t)rd32(tex + 32) | (uint64_t)rd32(tex + 36) << 32) != EM_SHADOW_DECAL_TEX0)
        return fail(0x001CB950u, "an owner's decal TEX0 packet is not 001F8D30's TEX0_1 write");
    for (u32 i = 0; i + 1 < S.opk_count; ++i) {
        uint32_t n;
        if (i >= 2 || check_fan(S.opk[i].bytes, S.opk[i].count, &n) < 0)
            return fail(0x001CE300u, "an owner's decal fan the renderer does not implement");
        S.owner_triangles += n - 2u;
    }
    return 0;
}

/* ------------------------------------------------------------ the logs */

void em_shadow_live_log(EmShadowLiveLog *out)
{
    if (out) *out = S.log;
}

const EmShadowLiveSample *em_shadow_live_sample(void)
{
    return S.sampled ? &S.sample : NULL;
}

void em_shadow_live_actor_log(EmShadowLiveActorLog *out)
{
    if (out) *out = S.alog;
}

const EmShadowLiveSample *em_shadow_live_actor_sample(void)
{
    return S.asampled ? &S.asample : NULL;
}
