/* em_shadow_original unit test: synthetic records exercise the gates, the
 * worker order, the fault latch and the receiver vertex slice. The
 * bit-exact comparison with the original instructions is
 * tools/test_shadow_original_reference.py. */
#include "game/em_shadow_original.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

enum { NODES = 6 };

typedef struct {
    char log[64][24];
    int count;
    int fail_at;       /* 1-based call index that returns -1, 0 = none */
    int32_t ids_seen;
    EmShadowOriginalBox boxes[2];
    float vp[16], uv[16];
} Log;

static int note(Log *l, const char *name)
{
    if (l->count < 64) snprintf(l->log[l->count], sizeof l->log[0], "%s", name);
    ++l->count;
    return l->fail_at == l->count ? -1 : 0;
}

static int w_bounds(void *ctx, int32_t id, float lo[3], float hi[3])
{
    Log *l = ctx;
    (void)id;
    ++l->ids_seen;
    lo[0] = -8.0f; lo[1] = 0.0f; lo[2] = -8.0f;
    hi[0] = 8.0f; hi[1] = 1.0f; hi[2] = 8.0f;
    return note(l, "bounds");
}
static int w_clear(void *ctx) { return note(ctx, "clear"); }
static int w_box(void *ctx, const EmShadowOriginalBox *b)
{
    Log *l = ctx;
    if (b->model == EM_SHADOW_BOX_MODEL_FRONT) l->boxes[0] = *b; else l->boxes[1] = *b;
    return note(l, b->model == EM_SHADOW_BOX_MODEL_FRONT ? "box14" : "box15");
}
static int w_sil(void *ctx, int32_t kind, const float vp[16])
{
    Log *l = ctx;
    memcpy(l->vp, vp, sizeof l->vp);
    return note(l, kind == 0x28 ? "silhouette28" : "silhouette");
}
static int w_begin(void *ctx, const float uv[16])
{
    Log *l = ctx;
    memcpy(l->uv, uv, sizeof l->uv);
    return note(l, "begin");
}
static int w_recv(void *ctx, const EmShadowOriginalReceiver *r) { (void)r; return note(ctx, "receiver"); }
static int w_end(void *ctx) { return note(ctx, "end"); }

typedef struct {
    uint8_t actor[EM_SHADOW_ORIGINAL_ACTOR_BYTES];
    uint8_t node[NODES][EM_SHADOW_ORIGINAL_NODE_BYTES];
    const uint8_t *nodes[NODES];
    int32_t grid[32 * 32 * 4];
    EmShadowOriginalScene scene;
    EmShadowOriginalState state;
    EmShadowOriginalWorkers w;
    Log log;
} Fixture;

static void ident(float m[16]) { memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1.0f; }

static void set_node(Fixture *f, int i, float x, float y, float z)
{
    float q[4] = {x, y, z, 1.0f};
    memcpy(f->node[i] + 0xC0, q, sizeof q);
}

static void fixture(Fixture *f)
{
    memset(f, 0, sizeof *f);
    int16_t kind = 0x28;
    memcpy(f->actor + 0x96, &kind, 2);
    f->actor[0x98] = 1;
    f->actor[0x09] = NODES;
    set_node(f, 0, 0.0f, 9.0f, 0.0f);
    set_node(f, 1, 0.0f, 10.0f, 0.0f);   /* anchor */
    set_node(f, 2, 3.0f, 14.0f, -2.0f);
    set_node(f, 3, -3.0f, 18.0f, 2.0f);  /* highest: the near pick */
    set_node(f, 4, 40.0f, 1.0f, 40.0f);  /* spread > 7 if max() were used */
    set_node(f, 5, -40.0f, 0.5f, -40.0f);
    for (int i = 0; i < NODES; ++i) f->nodes[i] = f->node[i];
    ident(f->scene.clip_2240);
    f->scene.clip_2240[15] = 1000.0f;               /* w = 1000: all inside */
    ident(f->scene.proj_2340);
    ident(f->scene.view_2380);
    ident(f->scene.camera_3AC0);
    ident(f->scene.view_810610);
    f->scene.view_810610[14] = 50.0f;               /* objects 50 in front */
    f->scene.zoom_2468 = 480.0f;
    f->scene.area_700 = 0x0B;                       /* AREA11 */
    f->scene.sub_701 = 0;
    f->scene.grid_140 = f->grid;
    f->scene.grid_words = 32 * 32 * 4;
    f->scene.stride_148 = 32;
    f->scene.cell_x_150 = 4.0f;
    f->scene.cell_z_154 = 4.0f;
    f->scene.origin_x_158 = -64.0f;
    f->scene.origin_z_15C = -64.0f;
    /* anchor cell (x 0 -> (0+64)/4 = 16, z 16): one object */
    f->grid[(16 * 32 + 16) * 4 + 0] = 7;
    f->grid[(15 * 32 + 17) * 4 + 2] = 9;
    f->grid[(16 * 32 + 16) * 4 + 1] = -1;           /* id <= 0 skipped */
    f->w = (EmShadowOriginalWorkers){&f->log, w_bounds, w_clear, w_box, w_sil, w_begin, w_recv, w_end};
}

static int run(Fixture *f, EmShadowOriginalPlan *plan, EmShadowOriginalFault *fault)
{
    return em_shadow_original_001DA6A0(f->actor, f->nodes, NODES, &f->scene, &f->state, plan, &f->w, fault);
}

int main(void)
{
    static Fixture f;
    static EmShadowOriginalPlan plan;
    EmShadowOriginalFault fault = {0, 0};

    /* 0015C160 routing */
    CHECK(em_shadow_original_route_0015C160(0, 0, 0, &fault) == 0);
    CHECK(em_shadow_original_route_0015C160(0, 0, 5, &fault) == 0);
    CHECK(em_shadow_original_route_0015C160(1, 1, 0, &fault) == 0);
    CHECK(em_shadow_original_route_0015C160(1, 1, 5, &fault) == 0);
    CHECK(em_shadow_original_route_0015C160(1, 0, 0, &fault) == 1);
    CHECK(em_shadow_original_route_0015C160(1, 2, 0, &fault) == 1);
    CHECK(fault.code == EM_SHADOW_FAULT_NONE);
    {   /* the untranslated 0015BF90 route faults and the fault latches */
        EmShadowOriginalFault route_fault = {0, 0};
        CHECK(em_shadow_original_route_0015C160(1, 0, 5, &route_fault) == -1);
        CHECK(route_fault.code == EM_SHADOW_FAULT_UNTRANSLATED && route_fault.address == 0x0015BF90u);
        CHECK(em_shadow_original_route_0015C160(1, 0, 0, &route_fault) == -1);
        CHECK(em_shadow_original_route_0015C160(1, 0, 5, NULL) == -1);
    }

    /* the drawn path and the worker order */
    fixture(&f);
    CHECK(run(&f, &plan, &fault) == 1);
    CHECK(fault.code == EM_SHADOW_FAULT_NONE);
    const char *want[] = {"clear", "box14", "box15", "silhouette28", "begin", "bounds", "receiver",
                          "bounds", "receiver", "end"};
    CHECK(f.log.count == 10);
    for (int i = 0; i < 10 && i < f.log.count; ++i) CHECK(strcmp(f.log.log[i], want[i]) == 0);
    CHECK(plan.anchor[0] == 0.0f && plan.anchor[1] == 10.0f && plan.anchor[3] == 1.0f);
    CHECK(plan.spread == 0.0f && plan.box_size == 16.0f && plan.unit == 8.0f); /* min/min */
    CHECK(plan.near_index == 3 && plan.far_index == 1);
    CHECK(plan.near_817FB0[1] == 18.0f);
    CHECK(plan.alpha_matrix == 1);
    CHECK(plan.light_817F70[1] == -1.0f);
    CHECK(f.state.d817FF0[1] == -1.0f);
    /* box: 16 x 40 x 16 hanging below the anchor, colours 0x14 then 0x15 */
    CHECK(f.log.boxes[0].world[0] == 1.6f && f.log.boxes[0].world[5] == 4.0f);
    CHECK(f.log.boxes[0].world[13] == -10.0f);        /* 10 - 20 */
    CHECK(f.log.boxes[0].rgbaq == 0x80008000u && f.log.boxes[1].rgbaq == 0x01000080u);
    CHECK(f.log.boxes[0].color_row[1] == 8388736.0f);
    /* silhouette: anchor at the 128x128 target centre (2048 + 0) */
    {
        const float *m = plan.silhouette_vp;
        float x = m[0]*0 + m[4]*10 + m[8]*0 + m[12];
        float y = m[1]*0 + m[5]*10 + m[9]*0 + m[13];
        CHECK(x == 2048.0f && y == 2048.0f);
        CHECK(memcmp(m, f.log.vp, 64) == 0);
    }
    /* receiver slice: u,v = 0.5 under the anchor, alpha 71 - 2*(6 + drop) */
    {
        float out[4];
        uint32_t alpha;
        const float ground[3] = {0.0f, 0.0f, 0.0f};
        em_shadow_original_receiver_vertex(plan.uv_24B0, ground, out, &alpha);
        CHECK(out[0] == 0.5f && out[1] == 0.5f && out[2] == 1.0f && alpha == 39);
        CHECK(em_shadow_original_receiver_rgba(alpha) == 39u << 24);
        const float edge[3] = {8.0f, 0.0f, 8.0f};  /* +x -> v, +z -> -u */
        em_shadow_original_receiver_vertex(plan.uv_24B0, edge, out, &alpha);
        CHECK(out[0] == 0.0f && out[1] == 1.0f);
        const float deep[3] = {0.0f, -40.0f, 0.0f};
        em_shadow_original_receiver_vertex(plan.uv_24B0, deep, out, &alpha);
        CHECK(alpha == 0);
        const float high[3] = {0.0f, 200.0f, 0.0f};
        em_shadow_original_receiver_vertex(plan.uv_24B0, high, out, &alpha);
        CHECK(alpha == 255);
        CHECK(memcmp(plan.uv_24B0, f.log.uv, 64) == 0);
    }
    CHECK(plan.receiver_count == 2 && plan.receiver[0].id == 7 && plan.receiver[1].id == 9); /* z-major */
    CHECK(plan.receiver[0].cls == 0);

    /* area switch and variant */
    fixture(&f); f.scene.area_700 = 0x06; f.scene.sub_701 = 0x01;
    fault.code = 0; CHECK(run(&f, &plan, &fault) == 1 && plan.alpha_matrix == 2);
    fixture(&f); f.scene.area_700 = 0x17;
    fault.code = 0; CHECK(run(&f, &plan, &fault) == 1 && plan.alpha_matrix == 0);
    {
        float out[4]; uint32_t alpha; const float ground[3] = {0};
        em_shadow_original_receiver_vertex(plan.uv_24B0, ground, out, &alpha);
        CHECK(alpha == 0);                           /* no alpha matrix: invisible */
    }
    fixture(&f); f.actor[0x23C] = 1;
    fault.code = 0; CHECK(run(&f, &plan, &fault) == 1 && plan.variant == 1 && plan.alpha_matrix == 3);

    /* early returns */
    fixture(&f); memset(f.actor + 0x96, 0, 2);
    fault.code = 0; CHECK(run(&f, &plan, &fault) == 0 && f.log.count == 0 && fault.code == 0);
    fixture(&f); f.scene.clip_2240[15] = 1.0f; f.scene.clip_2240[12] = 100.0f; /* x > w */
    fault.code = 0; CHECK(run(&f, &plan, &fault) == 0 && f.log.count == 0);
    CHECK((plan.probe_clip[0] & plan.probe_clip[1] & plan.probe_clip[2]) == 1);

    /* other kinds anchor on node 2 / node 3 */
    fixture(&f); { int16_t k = 0x2E; memcpy(f.actor + 0x96, &k, 2); }
    fault.code = 0; CHECK(run(&f, &plan, &fault) == 1 && plan.anchor[1] == 14.0f);
    fixture(&f); { int16_t k = 0x29; memcpy(f.actor + 0x96, &k, 2); }
    fault.code = 0; CHECK(run(&f, &plan, &fault) == 1 && plan.anchor[1] == 18.0f);
    fixture(&f); f.actor[0x98] = 0xFF;
    { float q[4] = {1.0f, 2.0f, 3.0f, 7.0f}; memcpy(f.actor + 0xB0, q, 16); }
    fault.code = 0; CHECK(run(&f, &plan, &fault) == 1 && plan.anchor[2] == 3.0f && plan.anchor[3] == 1.0f);

    /* faults: every worker NULL / failing, bad node, grid, latch */
    for (int stage = 1; stage <= 10; ++stage) {
        fixture(&f); f.log.fail_at = stage;
        fault.code = 0; fault.address = 0;
        CHECK(run(&f, &plan, &fault) == -1 && fault.code == EM_SHADOW_FAULT_WORKER_FAILED);
        CHECK(run(&f, &plan, &fault) == -1);        /* latched */
    }
    fixture(&f); f.w.w_box = NULL;
    fault.code = 0; CHECK(run(&f, &plan, &fault) == -1 && fault.code == EM_SHADOW_FAULT_NULL_WORKER &&
                          fault.address == 0x1DA310u);
    fixture(&f); f.w.w_receiver_end = NULL;
    fault.code = 0; CHECK(run(&f, &plan, &fault) == -1 && fault.address == 0x1D1FF0u);
    fixture(&f); f.nodes[3] = NULL;
    fault.code = 0; CHECK(run(&f, &plan, &fault) == -1 && fault.code == EM_SHADOW_FAULT_BAD_INPUT);
    fixture(&f); f.scene.grid_words = 100;
    fault.code = 0; CHECK(run(&f, &plan, &fault) == -1 && fault.code == EM_SHADOW_FAULT_BAD_INPUT);
    fixture(&f);
    f.scene.cell_x_150 = f.scene.cell_z_154 = 1.0f;
    f.scene.origin_x_158 = f.scene.origin_z_15C = -16.0f;
    for (int i = 0; i < 32 * 32 * 4; ++i) f.grid[i] = 3;
    fault.code = 0; CHECK(run(&f, &plan, &fault) == -1 && fault.code == EM_SHADOW_FAULT_OVERFLOW);
    fault.code = 0;
    CHECK(em_shadow_original_001DA6A0(NULL, f.nodes, NODES, &f.scene, &f.state, &plan, &f.w, &fault) == -1);

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("PASS shadow_original_test");
    return 0;
}
