/* tests/actor_collision_test.c - em_actor_collision fault contract and shapes.
 *
 * Synthetic cell directories only (no disc data). The original-instruction
 * comparison is tools/test_actor_collision_reference.py; this fixture pins
 * the contract the binders rely on: directory validation, the class-list
 * storage model, the vertical probe's result record, the column entries and
 * the worker adapters' fail-stop codes. Build (ASan/UBSan):
 *   cc -std=c11 -g -fsanitize=address,undefined -Isrc tests/actor_collision_test.c \
 *      src/game/em_actor_collision.c src/game/em_collision.c src/game/em_actor_pool.c -lm
 */
#include "game/em_actor_collision.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void put_u32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put_u16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void put_f(uint8_t *p, float v) { memcpy(p, &v, 4); }
static float get_f(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }

/* uid 0: one 0x2000 top face (face 3) at y 10 over x 0..4, z 0..4.
 * uid 1: one extended 0x1000 quad (normal +y, local y 0, x/z -2..2).
 * uid 2: word 0. */
static size_t build_directory(uint8_t *image)
{
    memset(image, 0, 0x400);
    put_u32(image, 3);
    uint32_t at = 0x10;
    put_u32(image + 4, at);                         /* uid 0 */
    float box0[6] = { 0, 10, 0, 4, 10, 4 };
    for (int k = 0; k < 6; ++k) put_f(image + at + 4 * k, box0[k]);
    put_u16(image + at + 0x18, 1);
    uint8_t *p = image + at + 0x1C;
    put_u16(p, 0x2000); p[2] = 3;
    put_f(p + 4, 0); put_f(p + 8, 10); put_f(p + 0xC, 0);
    put_f(p + 0x10, 4); put_f(p + 0x14, 0); put_f(p + 0x18, 4);
    at += 0x1C + 0x1C;
    put_u32(image + 8, at);                         /* uid 1 */
    put_u16(image + at + 0x18, 1);
    p = image + at + 0x1C;
    put_u16(p, 0x1800); p[2] = 4;
    uint8_t *local = p + 4 * 0x18 + 0x14;           /* axis, then 2n lanes */
    put_f(local + 4, 1.0f);                         /* local normal (0, 1, 0) */
    const float corner[4][2] = { { -2, -2 }, { -2, 2 }, { 2, 2 }, { 2, -2 } };
    const float edge[4][2] = { { -1, 0 }, { 0, 1 }, { 1, 0 }, { 0, -1 } };
    for (int k = 0; k < 4; ++k) {
        put_f(local + 0x10 + 12 * k, corner[k][0]);
        put_f(local + 0x18 + 12 * k, corner[k][1]);
        put_f(local + 0x10 + 12 * (4 + k), edge[k][0]);
        put_f(local + 0x18 + 12 * (4 + k), edge[k][1]);
    }
    at += 0x1C + 0x24 + 0x30 * 4;
    return at;
}

static void make_actor(EmActor *a, uint8_t cls, uint8_t uid, uint8_t kind54)
{
    memset(a, 0, sizeof *a);
    a->status = 1; a->cls = cls; a->uid = (uint16_t)(uid << 8); a->kind = kind54;
    a->self = a;
}

static void test_directory(void)
{
    uint8_t image[0x400];
    size_t size = build_directory(image);
    EmActorCellTable t = { 0 };
    CHECK(em_actor_cells_init(&t, image, size) == 0);
    CHECK(t.count == 3);
    CHECK(em_actor_cells_hull(&t, 0) != NULL);
    CHECK(em_actor_cells_hull(&t, 2) == NULL);      /* word 0 */
    CHECK(em_actor_cells_hull(&t, 3) == NULL);      /* uid >= count */
    float b[6];
    CHECK(em_actor_cells_bounds(&t, 0, b) == 1 && b[1] == 10.0f && b[3] == 4.0f);
    /* A truncated image, an offset past the end and a prim running past the
     * end are all refused, and a refused init leaves the table empty. */
    CHECK(em_actor_cells_init(&t, image, size - 4) == -1 && t.bytes == NULL);
    uint8_t bad[0x400];
    memcpy(bad, image, sizeof bad);
    put_u32(bad + 4, 0x3F0);
    CHECK(em_actor_cells_init(&t, bad, size) == -1);
    memcpy(bad, image, sizeof bad);
    put_u32(bad, 0x9000);
    CHECK(em_actor_cells_init(&t, bad, size) == -1);
    CHECK(em_actor_cells_init(&t, NULL, 8) == -1);
    em_actor_cells_free(&t);
}

static void test_retransform(void)
{
    uint8_t image[0x400];
    size_t size = build_directory(image);
    EmActorCellTable t = { 0 };
    CHECK(em_actor_cells_init(&t, image, size) == 0);
    const float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 100, 20, -50, 1 };
    CHECK(em_actor_cells_retransform_001A2370(&t, 0x0100, m) == 1);
    float b[6];
    CHECK(em_actor_cells_bounds(&t, 1, b));
    CHECK(b[0] == 98 && b[1] == 20 && b[2] == -52 && b[3] == 102 && b[4] == 20 && b[5] == -48);
    const uint8_t *p = em_actor_cells_hull(&t, 1) + 0x1C;
    CHECK(get_f(p + 8) == 1.0f && get_f(p + 0x10) == 20.0f);   /* world normal y, d */
    /* uid 0's first prim is not extended: the original returns untouched. */
    uint8_t before[0x400];
    memcpy(before, t.bytes, t.size);
    CHECK(em_actor_cells_retransform_001A2370(&t, 0x0000, m) == 1);
    CHECK(em_actor_cells_retransform_001A2370(&t, 0xFF00, m) == 1);
    CHECK(em_actor_cells_retransform_001A2370(&t, 0x0200, m) == 1);
    CHECK(memcmp(before, t.bytes, t.size) == 0);
    CHECK(em_actor_cells_retransform_001A2370(NULL, 0x0100, m) == -1);
    em_actor_cells_free(&t);
}

static void test_lists(void)
{
    EmActorClassLists lists;
    em_actor_class_lists_reset(&lists);
    EmActor a[4];
    make_actor(&a[0], 4, 0, 0);
    make_actor(&a[1], 0x84, 0, 0);       /* class 4 and the 0x80 list */
    make_actor(&a[2], 0x0A, 0, 0);       /* the class-2 list */
    make_actor(&a[3], 0x04, 0, 0);
    for (int i = 0; i < 3; ++i) CHECK(em_actor_class_publish_001B1B70(&lists, &a[i]) == 1);
    CHECK(lists.list[EM_ACTOR_LIST_CLASS4].live == 2);
    CHECK(lists.list[EM_ACTOR_LIST_FLAG80].live == 1);
    CHECK(lists.list[EM_ACTOR_LIST_CLASS2].live == 1);
    CHECK(em_actor_class_list_entry(&lists, EM_ACTOR_LIST_CLASS4, 0) == NULL);  /* not published yet */
    em_actor_class_lists_swap_001AAD00(&lists);
    /* Published entry 0 is the last push. */
    CHECK(em_actor_class_list_entry(&lists, EM_ACTOR_LIST_CLASS4, 0) == &a[1]);
    CHECK(em_actor_class_list_entry(&lists, EM_ACTOR_LIST_CLASS4, 1) == &a[0]);
    CHECK(em_actor_class_list_entry(&lists, EM_ACTOR_LIST_CLASS4, 2) == NULL);
    /* This frame's first push overwrites the storage of published entry 1. */
    CHECK(em_actor_class_push4_001B1D20(&lists, &a[3]) == 1);
    CHECK(em_actor_class_list_entry(&lists, EM_ACTOR_LIST_CLASS4, 1) == &a[3]);
    /* The cap: 0x80 pushes, then nothing. */
    em_actor_class_lists_reset(&lists);
    for (int i = 0; i < 0x90; ++i) em_actor_class_push4_001B1D20(&lists, &a[0]);
    CHECK(lists.list[EM_ACTOR_LIST_CLASS4].live == 0x80);
    CHECK(em_actor_class_publish_001B1B70(NULL, &a[0]) == -1);
}

static void test_ground(void)
{
    uint8_t image[0x400];
    size_t size = build_directory(image);
    EmActorCellTable t = { 0 };
    CHECK(em_actor_cells_init(&t, image, size) == 0);
    EmActorClassLists lists;
    em_actor_class_lists_reset(&lists);
    EmActor box, player;
    make_actor(&box, 4, 0, 0x0D);
    make_actor(&player, 0x20, 0xFF, 0);
    em_actor_class_push4_001B1D20(&lists, &box);
    em_actor_class_lists_swap_001AAD00(&lists);
    EmActorCollisionWorld w = { &t, &lists, NULL, NULL, 0, NULL };
    float feet = 11.0f;
    EmActorCollisionQuery q = { player.self, player.cls, &feet };
    const float pos[3] = { 2, 11, 2 }, down[3] = { 0, -13.8f, 0 }, below[3] = { 2, 9, 2 };
    EmActorCollisionHit hit;
    /* From 13.8 above down to y 11: misses (the face is below the end). */
    CHECK(em_actor_collision_ground_0019AB20(&w, &q, pos, down, 2, &hit) == 0);
    CHECK(hit.record == EM_ACTOR_RECORD_NONE && hit.entity == NULL);
    /* Down to y 9: the face at 10, the owner as the entity. */
    CHECK(em_actor_collision_ground_0019AB20(&w, &q, below, down, 0x80000002u, &hit) == 2);
    CHECK(hit.entity == &box && hit.record == EM_ACTOR_RECORD_CELL);
    CHECK(hit.point[1] == 10.0f && hit.delta[1] == 1.0f && feet == 12.0f);
    CHECK(hit.node == 0x400D && hit.normal[1] == 1.0f && hit.node_class_known);
    /* No grid pass (mask bit 2 clear): no 0019C830 entry state recorded. */
    CHECK(hit.grid_start[1] == 0.0f && hit.grid_end[1] == 0.0f);
    /* The query actor never hits its own cell. */
    EmActorCollisionQuery self = { box.self, 4, NULL };
    CHECK(em_actor_collision_ground_0019AB20(&w, &self, below, down, 2, &hit) == 0);
    /* +0x54 >= 0x51 and status 0 owners are skipped; 0x50 still collides. */
    box.kind = 0x51;
    CHECK(em_actor_collision_ground_0019AB20(&w, &q, below, down, 2, &hit) == 0);
    box.kind = 0x50;
    CHECK(em_actor_collision_ground_0019AB20(&w, &q, below, down, 2, &hit) == 2);
    box.status = 0;
    CHECK(em_actor_collision_ground_0019AB20(&w, &q, below, down, 2, &hit) == 0);
    box.status = 1;
    /* The n-gon cell once published: a floor at y 20 around (100, -50). */
    const float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 100, 20, -50, 1 };
    EmActor lift;
    make_actor(&lift, 4, 1, 3);
    CHECK(em_actor_cells_retransform_001A2370(&t, lift.uid, m) == 1);
    em_actor_class_push4_001B1D20(&lists, &lift);
    em_actor_class_push4_001B1D20(&lists, &box);
    em_actor_class_lists_swap_001AAD00(&lists);
    const float over[3] = { 100.5f, 19, -50.5f };
    CHECK(em_actor_collision_ground_0019AB20(&w, &q, over, down, 2, &hit) == 2);
    CHECK(hit.entity == &lift && hit.point[1] == 20.0f && hit.node == 0x4003);
    /* Faults: grid bit without a grid, bit 31 without feet, NULLs. */
    CHECK(em_actor_collision_ground_0019AB20(&w, &q, over, down, 6, &hit) == -1);
    EmActorCollisionQuery nofeet = { player.self, 0, NULL };
    CHECK(em_actor_collision_ground_0019AB20(&w, &nofeet, over, down, 0x80000002u, &hit) == -1);
    CHECK(em_actor_collision_ground_0019AB20(NULL, &q, over, down, 2, &hit) == -1);
    /* A static cell (bit 31): a published owner naming it faults (pass 2
     * would add the raw word), pass 1 without its kind view faults, and
     * with the view it collides with no entity and the static kind. */
    put_u32(t.bytes + 4, 0x80000000u | 0x10);
    CHECK(em_actor_collision_ground_0019AB20(&w, &q, over, down, 2, &hit) == -1);
    em_actor_class_push4_001B1D20(&lists, &lift);
    em_actor_class_lists_swap_001AAD00(&lists);
    CHECK(em_actor_collision_ground_0019AB20(&w, &q, below, down, 2, &hit) == -1);
    const uint8_t kinds[3] = { 0x05, 0, 0 };
    w.static_kind = kinds; w.static_kind_count = 3;
    CHECK(em_actor_collision_ground_0019AB20(&w, &q, below, down, 2, &hit) == 2);
    CHECK(hit.entity == NULL && hit.node == 0x4005);
    em_actor_cells_free(&t);
}

/* The column's SDK workers (0011E748, 0011DBB8). Only the fault contract
 * is pinned here: a face column never calls them, so these count calls and
 * return NaN, which would poison any entry that used them. */
static int math_calls;
static float stub_math(void *context, float x) { (void)context; (void)x; ++math_calls; return NAN; }

static void test_column_and_adapters(void)
{
    uint8_t image[0x400];
    size_t size = build_directory(image);
    EmActorCellTable t = { 0 };
    CHECK(em_actor_cells_init(&t, image, size) == 0);
    EmActorClassLists lists;
    em_actor_class_lists_reset(&lists);
    EmActor box;
    make_actor(&box, 4, 0, 0x0D);
    EmActorCollisionWorld w = { &t, &lists, NULL, NULL, 0, NULL };
    EmActorCollisionOwner owner = { &w, &lists, &box, NULL };
    CHECK(em_actor_collision_owner_publish(&owner) == 1);
    em_actor_class_lists_swap_001AAD00(&lists);
    EmCollColumn col;
    const float at[3] = { 2, 0, 2 };
    /* The SDK workers are required: no host stand-in is substituted. */
    const EmCollColumnMath math = { stub_math, stub_math, NULL };
    const EmCollColumnMath no_atan = { stub_math, NULL, NULL };
    CHECK(em_actor_collision_column_0019BC40(&w, at, NULL, &col) == -1);
    CHECK(em_actor_collision_column_0019BC40(&w, at, &no_atan, &col) == -1);
    math_calls = 0;
    CHECK(em_actor_collision_column_0019BC40(&w, at, &math, &col) == 1);
    CHECK(col.count == 1 && col.flags[0] == 0x8001 && col.height[0] == 10.0f);
    CHECK(col.object_kind[0] == 0x0D && col.owner[0] == 0 && !isnan(col.aux[0]));
    CHECK(math_calls == 0);
    /* 001A5760 is strict at the face edges. */
    const float edge[3] = { 0, 0, 2 };
    CHECK(em_actor_collision_column_0019BC40(&w, edge, &math, &col) == 0);
    float bounds[6];
    CHECK(em_actor_collision_owner_hull_bounds(&owner, bounds) == 1 && bounds[4] == 10.0f);
    const float m[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    CHECK(em_actor_collision_owner_hull(&owner, m) == 1);
    CHECK(em_actor_collision_owner_contact(&owner) == 1);
    EmActor nohull;
    make_actor(&nohull, 4, 2, 0);
    EmActorCollisionOwner empty = { &w, &lists, &nohull, NULL };
    CHECK(em_actor_collision_owner_hull_bounds(&empty, bounds) == -1);
    EmActorCollisionOwner broken = { NULL, NULL, &box, NULL };
    CHECK(em_actor_collision_owner_hull(&broken, m) == -1);
    CHECK(em_actor_collision_owner_publish(&broken) == -1);
    CHECK(em_actor_collision_owner_contact(&broken) == -1);
    em_actor_cells_free(&t);
}

static EmActorPool pool;

static void test_probe_adapters(void)
{
    uint8_t image[0x400];
    size_t size = build_directory(image);
    EmActorCellTable t = { 0 };
    CHECK(em_actor_cells_init(&t, image, size) == 0);
    EmSceneState scene;
    memset(&scene, 0, sizeof scene);
    em_actor_pool_reset_001AF8E0(&pool);
    EmActor *below = em_actor_pool_alloc_001AFA90(&pool, &scene, 4);
    EmActor *above = em_actor_pool_alloc_001AFA90(&pool, &scene, 4);
    CHECK(below && above);
    below->uid = 0x0000; below->kind = 0x0D; below->model = 6;
    above->uid = 0xFF00;
    EmActorClassLists lists;
    em_actor_class_lists_reset(&lists);
    em_actor_class_push4_001B1D20(&lists, below);
    em_actor_class_push4_001B1D20(&lists, above);
    em_actor_class_lists_swap_001AAD00(&lists);
    EmActorCollisionWorld w = { &t, &lists, NULL, NULL, 0, NULL };
    /* The crate's support probe (mode 2 here: no grid in this fixture). */
    EmActorCollisionOwner crate = { &w, &lists, above, &pool };
    float position[4] = { 2, 11, 2, 1 };
    const float from[3] = { 2, 9.5f, 2 };
    EmCrateProbe out;
    CHECK(em_actor_collision_owner_probe(&crate, position, from, -3.0f, 0x80000002u, &out) == 1);
    CHECK(out.result == 2 && (uint32_t)out.actor == em_actor_pool_address(&pool, below));
    CHECK(position[1] == 11.5f);                   /* bit 31: +0xB4 += delta.y */
    EmActorCollisionOwner lost = { &w, &lists, above, NULL };
    CHECK(em_actor_collision_owner_probe(&lost, position, from, -3.0f, 2, &out) == -1);
    /* The player's floor worker: flags/type of the owner, entity kept. */
    EmActorCollisionPlayer player = { &w, { &player, 0, NULL }, NULL };
    EmPlayerProbeHit hit;
    const float probe[3] = { 0, -13.8f, 0 };
    CHECK(em_actor_collision_player_ground(&player, from, probe, 2, &hit) == 2);
    CHECK(hit.entity && hit.entity_flags == 4 && hit.entity_type == 6 && hit.node == 0x400D);
    CHECK(player.entity == below && hit.point[1] == 10.0f);
    below->kind = 0x35;                            /* the +0x34 axis is not carried */
    CHECK(em_actor_collision_player_ground(&player, from, probe, 2, &hit) == -1);
    em_actor_cells_free(&t);
}

int main(void)
{
    test_probe_adapters();
    test_directory();
    test_retransform();
    test_lists();
    test_ground();
    test_column_and_adapters();
    printf("actor collision test: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
