/* Sanitizer fixture for em_script_door_fan / em_script_door_fan_husk
 * (docs/SCRIPT_DOOR_FAN.md). Behaviour against the original is proven by
 * tools/test_script_door_fan_reference.py; this fixture runs every entry
 * point under ASan/UBSan with scripted workers and checks the fail-stop
 * contract: a missing worker or datum faults at its original address, a
 * negative worker result latches, an entry outside the image faults, and a
 * latched fault makes later calls return -1 without calling anything. No game
 * data is used. */
#include <stdio.h>
#include <string.h>

#include "game/em_script_door_fan.h"
#include "game/em_script_door_fan_husk.h"

static int failures, checks;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int calls;
static EmSdfSpawned spawned[4];
static int next_node;
static int32_t bone_count = 3;

static int ok(void *ctx) { (void)ctx; ++calls; return 0; }
static int fail(void *ctx) { (void)ctx; ++calls; return -1; }
static int r_bank(void *ctx, int32_t index, uint32_t *value) { (void)ctx; *value = 0x1000u + (uint32_t)index; return 0; }
static int r_head(void *ctx, uint32_t address, float *value) { (void)ctx; (void)address; *value = 2.5f; return 0; }
static int r_table(void *ctx, uint32_t address, uint16_t *value) { (void)ctx; *value = (uint16_t)(address & 0xFFFF); return 0; }
static int alloc(void *ctx, uint8_t type, uint32_t *node, EmSdfSpawned **view)
{
    (void)ctx; (void)type; ++calls;
    if (next_node >= 4) { *node = 0; return 0; }
    *node = 0x7A0000u + (uint32_t)next_node * 0x2F0u;
    *view = &spawned[next_node++];
    return 0;
}
static int alt(void *ctx, uint32_t bank, int16_t index, uint32_t callback, uint32_t *node, EmSdfSpawned **view)
{
    (void)bank; (void)index; (void)callback;
    return alloc(ctx, 0, node, view);
}
static int ca6e0(void *ctx, EmSdfEventActor *obj, uint32_t bank) { (void)ctx; obj->w44 = bank; ++calls; return 0; }
static int c6120(void *ctx, uint32_t table, int32_t index, uint32_t *result) { (void)ctx; *result = table + (uint32_t)index; return 0; }
static int c5c90(void *ctx, EmSdfEventActor *obj) { (void)ctx; obj->lifecycle = 2; return 0; }
static int c6150(void *ctx, uint32_t model, int32_t *result) { (void)ctx; (void)model; *result = bone_count; return 0; }
static int af780(void *ctx, uint32_t *handle) { (void)ctx; *handle = 0xE00000u + (uint32_t)calls++; return 0; }
static int obj16(void *ctx, EmSdfEventActor *obj, int16_t v) { (void)ctx; (void)obj; (void)v; return 0; }
static int obj32(void *ctx, EmSdfEventActor *obj, int32_t v) { (void)ctx; (void)obj; (void)v; return 0; }
static int obj8(void *ctx, EmSdfEventActor *obj, uint8_t v) { (void)ctx; (void)obj; (void)v; return 0; }
static int u8w(void *ctx, uint8_t v) { (void)ctx; (void)v; return 0; }
static int c61d0(void *ctx, uint32_t bank, int16_t clip, int32_t *result) { (void)ctx; (void)bank; (void)clip; *result = 10; return 0; }
static int c67e0(void *ctx, int16_t clip, float a, float b) { (void)ctx; (void)clip; (void)a; (void)b; ++calls; return 0; }
static int u32w(void *ctx, uint32_t v) { (void)ctx; (void)v; return 0; }
static int b1630(void *ctx, float x, float y, float z, int32_t *result) { (void)ctx; (void)y; (void)z; *result = x > 0.0f; return 0; }
static int c64f0(void *ctx, float step, int16_t *flags) { (void)ctx; (void)step; *flags = 0x10; return 0; }

static int wrap(void *ctx, float a, float *r) { (void)ctx; *r = a; return 0; }
static int ident(void *ctx, uint32_t m[16]) { (void)ctx; memset(m, 0, 64); return 0; }
static int rot(void *ctx, uint32_t d[16], const uint32_t s[16], const float a[4]) { (void)ctx; (void)a; memmove(d, s, 64); return 0; }
static int mul(void *ctx, float out[4], const uint32_t m[16], const uint32_t v[4]) { (void)ctx; (void)m; (void)v; out[0] = out[1] = out[2] = 1.0f; out[3] = 1.0f; return 0; }

static EmSdfWorkers sdf_workers(void)
{
    EmSdfWorkers w;
    memset(&w, 0, sizeof w);
    w.r_0028A490 = r_bank; w.r_track_head = r_head; w.r_0024DB80 = r_table;
    w.w_001AFA90 = alloc; w.w_001C8140 = alt; w.w_001CA6E0 = ca6e0; w.w_001C6120 = c6120;
    w.w_0022EC30 = u32w; w.w_001C5C90 = c5c90; w.w_001C6150 = c6150; w.w_001AF780 = af780;
    w.w_001BA8E0 = obj16; w.w_001D8BF0 = obj32; w.w_001CA6F0 = obj8; w.w_001CB5B0 = u8w;
    w.w_001C63E0 = obj16; w.w_001C61D0 = c61d0; w.w_001C67E0 = c67e0; w.w_001B1630 = b1630;
    w.w_001B1B70 = ok; w.w_001C64F0 = c64f0; w.w_001BC150 = ok;
    w.w_001B1470 = wrap; w.w_001029C0 = ident; w.w_00102C58 = rot; w.w_001026A0 = mul;
    return w;
}

static void put16(uint8_t *p, int16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)((uint16_t)v >> 8); }

static void test_sdf(void)
{
    EmSdfWorkers w = sdf_workers();
    EmSdfFault fault = {0, 0};
    uint8_t activity[12];
    memset(activity, 7, sizeof activity);
    CHECK(em_sdf_001BA510(activity, &fault) == 0 && activity[0] == 0 && activity[11] == 0);
    CHECK(em_sdf_001BA510(NULL, &fault) == -1 && fault.address == 0x008106D4u);
    CHECK(em_sdf_001BA510(activity, &fault) == -1); /* latched */

    /* 001BAC00: three entries (plain, 0x270E, plain), then the sentinel. */
    uint8_t list[4 * EM_SDF_ENTRY_SIZE];
    memset(list, 0, sizeof list);
    list[0] = 9; put16(list + 4, 0x47);
    put16(list + EM_SDF_ENTRY_SIZE + 4, EM_SDF_TAG_ALT_SPAWN);
    list[2 * EM_SDF_ENTRY_SIZE] = 8; put16(list + 2 * EM_SDF_ENTRY_SIZE + 0xA, 3);
    put16(list + 3 * EM_SDF_ENTRY_SIZE, -1);
    EmSdfImage image = {list, 0x828F30u, sizeof list};
    uint8_t record[0x40];
    memset(record, 0, sizeof record);
    record[0x14] = 0x30; record[0x15] = 0x8F; record[0x16] = 0x82;
    EmSdfSpawnOwner owner = {0x7A8E10u, 5};
    fault.code = 0;
    next_node = 0;
    CHECK(em_sdf_001BAC00(&owner, record, &image, &w, &fault) == 1 && owner.s2E == 0);
    CHECK(spawned[0].handler_10 == EM_SDF_DEFAULT_HANDLER && spawned[0].s2E == 0);
    CHECK(spawned[1].s2E == 0xF && spawned[2].s2E == 1 && spawned[2].b0D == 0);
    image.length = 2 * EM_SDF_ENTRY_SIZE; /* the sentinel falls outside */
    next_node = 0;
    CHECK(em_sdf_001BAC00(&owner, record, &image, &w, &fault) == -1 &&
          fault.code == EM_SDF_FAULT_OUT_OF_IMAGE);

    /* 001BAD40: command 4 bank path, then the overflow fault. */
    EmSdfEventActor obj;
    memset(&obj, 0, sizeof obj);
    int32_t m0 = 0, m4 = 0, m8 = 0;
    uint32_t c0 = 0, t250 = 0;
    float t254 = 1.0f, t258 = 0.0f;
    uint8_t e4 = 0, b8 = 1;
    int16_t s24e = 0, cap = 0x40;
    EmSdfWorld world = {&m0, &m4, &m8, &c0, &t250, &t254, &t258, &e4, &s24e, &cap, &b8};
    uint8_t ev[EM_SDF_ENTRY_SIZE];
    memset(ev, 0, sizeof ev);
    put16(ev + 4, 0x47); put16(ev + 6, 0x98); put16(ev + 8, 2); put16(ev + 0xA, 4);
    fault.code = 0;
    CHECK(em_sdf_001BAD40(&obj, ev, &world, &w, &fault) == 0 && obj.b09 == 3 && obj.b0C == 3);
    put16(ev + 0xA, 3);
    CHECK(em_sdf_001BAD40(&obj, ev, &world, &w, &fault) == 0 && e4 == 3 && t258 == 2.5f &&
          obj.b09 == 0);
    put16(ev + 0xA, 5);
    CHECK(em_sdf_001BAD40(&obj, ev, &world, &w, &fault) == 1);
    put16(ev + 4, EM_SDF_MSG_REQUEST);
    CHECK(em_sdf_001BAD40(&obj, ev, &world, &w, &fault) == 0 && m0 == 2 && m4 == 1 && m8 == 2);
    put16(ev + 4, 0x47); put16(ev + 0xA, 7);
    bone_count = 0x79; cap = 0x100;
    CHECK(em_sdf_001BAD40(&obj, ev, &world, &w, &fault) == -1 &&
          fault.code == EM_SDF_FAULT_CAPACITY);
    bone_count = 3; cap = 0x40;

    /* 001B1B30, 001BC240, 001BC290, 001BBD60. */
    uint8_t visible = 9;
    fault.code = 0;
    CHECK(em_sdf_001B1B30(&visible, 1.0f, 0, 0, &w, &fault) == 1 && visible == 1);
    CHECK(em_sdf_001B1B30(&visible, -1.0f, 0, 0, &w, &fault) == 0 && visible == 0);
    EmSdfDoorStep door = {4, 0};
    CHECK(em_sdf_001BC240(&door, &w, &fault) == 0 && door.anim_flags == 0x10);
    CHECK(em_sdf_001BC290(&door, &world, &w, &fault) == 0 && door.b0B == 4);
    b8 = 0;
    CHECK(em_sdf_001BC290(&door, &world, &w, &fault) == 1 && door.b0B == 0);
    uint32_t word = 0;
    CHECK(em_sdf_001BBD60(0x0400, 1, &word, &w, &fault) == 0 && word == ((0x24DB80u + 16 + 2) & 0xFFFF));
    /* 001B0080: fixed seat, then the player-relative seat. */
    EmSdfSeat seat;
    memset(&seat, 0, sizeof seat);
    uint8_t area = 1, room = 4;
    float player[4] = {10, 20, 30, 1}, angles[4] = {0, 0.5f, 0, 0}, eye[4], tgt[4];
    uint32_t matrix[16], vec[4];
    EmSdfSeatWorld sw = {&area, &room, player, angles, eye, tgt, matrix, vec};
    CHECK(em_sdf_001B0080(&seat, 2.0f, &sw, &w, &fault) == 0 && seat.eye_10[2] == -572.5f &&
          tgt[2] == -557.5f);
    room = 3;
    seat.f0C = 25.0f;
    CHECK(em_sdf_001B0080(&seat, 2.0f, &sw, &w, &fault) == 0 && seat.tgt_20[1] == 37.0f &&
          seat.eye_10[1] == 40.0f && eye[0] == 11.0f && vec[2] == 0x41C80000u);
    w.w_001BC150 = fail;
    CHECK(em_sdf_001BC240(&door, &w, &fault) == -1 && fault.address == 0x001BC150u &&
          fault.code == EM_SDF_FAULT_WORKER_FAILED);
    calls = 0;
    CHECK(em_sdf_001B1B30(&visible, 1.0f, 0, 0, &w, &fault) == -1 && calls == 0);
}

/* ---- husk trio ----------------------------------------------------------- */

static EmHuskChild child;
static EmHuskLinked linked;
static int32_t script_result;
static int h_ok(void *ctx) { (void)ctx; ++calls; return 0; }
static int h_res0(void *ctx, int32_t *r) { (void)ctx; *r = 0; return 0; }
static int h_rand(void *ctx, int32_t *r) { (void)ctx; *r = 0x7FFFFFFF; return 0; }
static int h_sin(void *ctx, float x, float *r) { (void)ctx; *r = x; return 0; }
static int h_slot(void *ctx, uint32_t index, uint32_t *bone) { (void)ctx; *bone = 0x900000u + index; return 0; }
static int h_bone(void *ctx, uint32_t bone, uint32_t off, float v) { (void)ctx; (void)bone; (void)off; (void)v; return 0; }
static int h_u32(void *ctx, uint32_t v) { (void)ctx; (void)v; return 0; }
static int h_alloc(void *ctx, uint8_t cls, uint32_t *node, EmHuskChild **view) { (void)ctx; (void)cls; *node = 0x7ADD60u; *view = &child; return 0; }
static int h_child(void *ctx, uint32_t a, EmHuskChild **view) { (void)ctx; (void)a; *view = &child; return 0; }
static int h_u32u32(void *ctx, uint32_t a, uint32_t b) { (void)ctx; (void)a; (void)b; return 0; }
static int h_bit(void *ctx, uint8_t id, int32_t *r) { (void)ctx; (void)id; *r = 0; return 0; }
static int h_u8(void *ctx, uint8_t id) { (void)ctx; (void)id; return 0; }
static int h_fx(void *ctx, uint32_t fx, int32_t *r) { (void)ctx; (void)fx; *r = 1; return 0; }
static int h_sound(void *ctx, int32_t cue, int32_t a2, float range) { (void)ctx; (void)cue; (void)a2; (void)range; return 0; }
static int h_link(void *ctx, EmHuskLinked **l) { (void)ctx; *l = &linked; return 0; }
static int h_poll(void *ctx, int32_t *r) { (void)ctx; *r = script_result; return 0; }
static int h_ii(void *ctx, int32_t a, int32_t b) { (void)ctx; (void)a; (void)b; return 0; }
static int h_i(void *ctx, int32_t a) { (void)ctx; (void)a; return 0; }

static EmHuskWorkers husk_workers(void)
{
    EmHuskWorkers w;
    memset(&w, 0, sizeof w);
    w.w_001C6380 = h_ok; w.w_001B17A0 = h_ok; w.w_draw_4C = h_ok; w.w_001AFC10 = h_ok;
    w.w_001B0FD0 = h_res0; w.w_00122BB8 = h_rand; w.w_0011E2A8 = h_sin; w.r_00275B40 = h_slot;
    w.s_bone_f32 = h_bone; w.w_001A2370 = h_u32; w.w_001AFA90 = h_alloc; w.r_child_220 = h_child;
    w.w_00102958 = h_u32u32; w.w_001B11E0 = h_bit; w.w_001B1190 = h_u8; w.w_001EFE00 = h_fx;
    w.w_001FBD50 = h_sound; w.r_link_18 = h_link; w.w_001BA1A0 = h_u32; w.w_001BA1F0 = h_poll;
    w.w_001D2830 = h_ii; w.w_001C1DC0 = h_ok; w.w_001FABB0 = h_ok; w.w_001AEE10 = h_ii;
    w.w_001C4760 = h_ii; w.w_001FAE70 = h_i;
    return w;
}

static void test_husk(void)
{
    EmHuskWorkers w = husk_workers();
    uint8_t flags[256];
    memset(flags, 0, sizeof flags);
    uint32_t c6c8 = 0xFFFFFFFFu;
    uint8_t d808 = 0;
    float spad = 0.0f;
    EmHuskWorld world = {flags, &c6c8, &d808, &spad};
    EmHuskFault fault = {0, 0};

    EmHuskCreature h;
    memset(&h, 0, sizeof h);
    CHECK(em_husk_creature_tick(&h, &world, &w, &fault) == 1 && h.lifecycle == 0x64 &&
          h.timer_28 == 300 + 299 && h.child_220 == 0x7ADD60u && child.b0D == EM_HUSK_CHILD_MODEL);
    CHECK(em_husk_creature_tick(&h, &world, &w, &fault) == 1 && h.lifecycle == 0x64);
    flags[EM_HUSK_FLAG_30] = 0xFF;
    CHECK(em_husk_creature_tick(&h, &world, &w, &fault) == 1 && h.lifecycle == 4);
    CHECK(em_husk_creature_tick(&h, &world, &w, &fault) == -1 &&
          fault.code == EM_HUSK_FAULT_UNTRANSLATED && fault.address == 0x00825B74u);
    fault.code = 0;
    h.lifecycle = 2; h.f1FC = -1.0f; h.f1F8 = 0.25f; h.w21C = 2;
    for (int i = 0; i < 6; ++i)
        CHECK(em_husk_creature_tick(&h, &world, &w, &fault) == 1);
    CHECK(h.w21C == 0 && spad == 0.0f && child.fA0[3] == 0.25f);
    h.lifecycle = 9;
    CHECK(em_husk_creature_tick(&h, &world, &w, &fault) == 0 && h.freed == 1);
    CHECK(em_husk_creature_tick(&h, &world, &w, &fault) == -1 && fault.code == EM_HUSK_FAULT_FREED);

    fault.code = 0;
    EmHuskPartner p;
    memset(&p, 0, sizeof p);
    CHECK(em_husk_partner_tick(&p, &w, &fault) == 1 && p.lifecycle == 1 && p.h34 == 1);
    p.hit_36 = 1;
    CHECK(em_husk_partner_tick(&p, &w, &fault) == 1 && p.lifecycle == 2 && linked.lifecycle == 2 &&
          linked.w21C == 0x5A);
    for (int i = 0; i < 12; ++i)
        CHECK(em_husk_partner_tick(&p, &w, &fault) == 1);
    CHECK(p.timer_28 == 10);

    EmHuskManager m;
    memset(&m, 0, sizeof m);
    flags[EM_HUSK_FLAG_30] = 0;
    CHECK(em_husk_manager_tick(&m, &world, &w, &fault) == 1 && m.lifecycle == 1 && m.b00 == 1);
    flags[EM_HUSK_FLAG_30] = 1;
    CHECK(em_husk_manager_tick(&m, &world, &w, &fault) == 1 && m.phase == 1 &&
          c6c8 == 0xF1FFFF8Fu);
    script_result = 1;
    CHECK(em_husk_manager_tick(&m, &world, &w, &fault) == 1 && m.lifecycle == 3 && d808 == 0xFF &&
          m.h2E == 0xFFFF && (c6c8 & 0x40u));
    CHECK(em_husk_manager_tick(&m, &world, &w, &fault) == 0 && m.freed == 1);

    fault.code = 0;
    memset(&m, 0, sizeof m);
    world.d810758 = NULL;
    CHECK(em_husk_manager_tick(&m, &world, &w, &fault) == -1 && fault.code == EM_HUSK_FAULT_NULL &&
          fault.address == 0x00810788u);
}

int main(void)
{
    test_sdf();
    test_husk();
    printf("script_door_fan_test: %d/%d checks passed\n", checks - failures, checks);
    return failures != 0;
}
