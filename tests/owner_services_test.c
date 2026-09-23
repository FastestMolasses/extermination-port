/* Native contract of em_owner_services_original (docs/OWNER_SERVICES.md).
 * The arithmetic and every written byte are checked against the original
 * instructions by tools/test_owner_services_reference.py; this file pins
 * the fail-stop contract, the packet layout and the worker order. */
#include "game/em_owner_services_original.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

/* ---- recording workers ---- */
static char trace[512];
static int script_bind = 1, script_visible, script_cull, fail_at;
static EmOwnerBone pool_bones[64];
static int next_slot;
static EmOwnerModel model;
static EmOwnerSkeletonRecord records[64];

static int note(const char *tag)
{
    strncat(trace, tag, sizeof trace - strlen(trace) - 1);
    strncat(trace, " ", sizeof trace - strlen(trace) - 1);
    return (fail_at && strstr(tag, "!")) ? -1 : 0;
}

static int w6120(void *c, uint32_t bank, uint32_t id, uint32_t *h) { (void)c; (void)bank; (void)id; *h = 7; return note(fail_at == 1 ? "6120!" : "6120"); }
static int wa6e0(void *c, EmOwnerServicesOwner *o, uint32_t h) { (void)c; (void)h; if (script_bind) o->model = &model; return note("a6e0"); }
static int wf780(void *c, EmOwnerBone **slot) { (void)c; *slot = &pool_bones[next_slot++]; return note("f780"); }
static int wanim(void *c, uint8_t n) { (void)c; (void)n; return note("anim"); }
static int wbone2(void *c, EmOwnerServicesOwner *o, int16_t clip) { (void)c; (void)o; return note(clip == -1 ? "bone2(-1)" : "bone2"); }
static int w1ce0(void *c, EmOwnerServicesOwner *o) { (void)c; (void)o; return note("1ce0"); }
static uint32_t last_1630[3], last_radius;
static int w1630(void *c, uint32_t x, uint32_t y, uint32_t z, uint8_t *v) { (void)c; last_1630[0] = x; last_1630[1] = y; last_1630[2] = z; *v = (uint8_t)script_visible; return note("1630"); }
static int w1b70(void *c, EmOwnerServicesOwner *o) { (void)c; (void)o; return note("1b70"); }
static int wa7b0(void *c, const float p[3], uint32_t r, int32_t *f) { (void)c; (void)p; last_radius = r; *f = script_cull; return note("a7b0"); }
static int w8c20(void *c, int32_t m) { (void)c; return note(m == 0 ? "8c20(0)" : "8c20"); }
static int w89d0(void *c, const EmOwnerServicesOwner *o, float a[16], float b[16])
{
    (void)c; (void)o;
    for (int i = 0; i < 16; ++i) { a[i] = (float)i; b[i] = (float)(100 + i); }
    return note("89d0");
}
static int w1f80(void *c, int32_t a, int32_t b, int32_t d) { (void)c; return note(a == 0 && b == 1 && d == 0 ? "1f80(0,1,0)" : "1f80"); }
static int wa940(void *c, int32_t f, const EmOwnerModel *m) { (void)c; (void)f; return note(m == &model ? "a940" : "a940?"); }
static int wb3c0(void *c, EmOwnerServicesOwner *o) { (void)c; (void)o; return note("b3c0"); }
static int64_t last_duration;
static uint8_t last_big, last_small;
static int w61c0(void *c, uint8_t big, uint8_t small, int64_t d, int32_t force) { (void)c; last_big = big; last_small = small; last_duration = d; return note(force == 1 ? "61c0" : "61c0?"); }
static int w6250(void *c) { (void)c; return note("6250"); }

static int16_t cap;
static uint32_t bank = 0x1234;
static uint8_t mode_ca5 = 6, e56;
static int16_t e68;
static EmOwnerServicesScratch scratch;
static uint8_t dl[0x4000];
static EmOwnerServicesChannel channel;
static const uint8_t rumble_table[8] = {1, 200, 30, 0, 0, 90, 12, 0};

static void setup(EmOwnerServices *s)
{
    memset(s, 0, sizeof *s);
    memset(trace, 0, sizeof trace);
    memset(pool_bones, 0, sizeof pool_bones);
    next_slot = 0; fail_at = 0; script_bind = 1; script_visible = 0; script_cull = 0;
    cap = 0x400;
    memset(&model, 0, sizeof model);
    model.bone_count = 3; model.radius = 5.0f; model.skeleton = records; model.skeleton_records = 64;
    for (int i = 0; i < 64; ++i) { records[i].parent = (int16_t)(i - 1); records[i].bind[0] = (float)i; }
    s->world.d0028A59C = &bank; s->world.d0028A56C = &bank;
    s->world.d00275BCC = &cap; s->world.d00810CA5 = &mode_ca5;
    s->world.scratch = &scratch;
    memset(dl, 0xAB, sizeof dl);
    channel.cursor = dl; channel.end = dl + sizeof dl;
    s->world.channel = &channel; s->world.channel_count = 1;
    s->world.d0024D6F0 = rumble_table; s->world.d0024D6F0_count = 2;
    s->world.d00810E56 = &e56; s->world.d00810E68 = &e68;
    EmOwnerServicesWorkers *w = &s->workers;
    w->w_001C6120 = w6120; w->w_001CA6E0 = wa6e0; w->w_001AF780 = wf780;
    w->w_anim_bone_array_setup = wanim; w->w_bone_init_default_2 = wbone2;
    w->w_001B1CE0 = w1ce0; w->w_001B1630 = w1630; w->w_001B1B70 = w1b70;
    w->w_001CA7B0 = wa7b0; w->w_001D8C20 = w8c20; w->w_001D89D0 = w89d0;
    w->w_001D1F80 = w1f80; w->w_001CA940 = wa940; w->w_001CB3C0 = wb3c0;
    w->w_001B61C0 = w61c0; w->w_001B6250 = w6250;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int main(void)
{
    EmOwnerServices s;
    EmOwnerServicesOwner o;

    /* 001B0FD0: bind, three slots, bone_init_default_1, +4 += 1. */
    setup(&s); memset(&o, 0, sizeof o);
    o.lifecycle = 0;
    CHECK(em_owner_services_001B0FD0(&s, &o) == 0);
    CHECK(strcmp(trace, "6120 a6e0 f780 f780 f780 anim ") == 0);
    CHECK(o.lifecycle == 1 && o.bone_count == 3 && o.bones_held == 3);
    CHECK(o.bone[2] == &pool_bones[2] && pool_bones[2].parent == 1 && pool_bones[2].scale[1] == 0x1000);
    CHECK(pool_bones[1].bind[0] == 1.0f);

    /* Over the cap: +4 = 3, result 1, no slot popped, no bone init. */
    setup(&s); memset(&o, 0, sizeof o); cap = 2;
    CHECK(em_owner_services_001B0FD0(&s, &o) == 1 && o.lifecycle == 3 && next_slot == 0);
    CHECK(strcmp(trace, "6120 a6e0 ") == 0);

    /* 001B1020: a2 == -1 -> bone_init_default_1; otherwise bone_init_default_2((int16)a3). */
    setup(&s); memset(&o, 0, sizeof o);
    CHECK(em_owner_services_001B1020(&s, &o, 0x13, -1, 5) == 0 && o.lifecycle == 1);
    CHECK(strstr(trace, "bone2") == NULL);
    static const uint32_t anims[2] = {0xAAAA, 0xBBBB};
    setup(&s); memset(&o, 0, sizeof o);
    s.world.d0028A490 = anims; s.world.d0028A490_count = 2;
    CHECK(em_owner_services_001B1020(&s, &o, 0x13, 1, 0x1FFFF) == 0 && o.anim == 0xBBBB);
    CHECK(strstr(trace, "bone2(-1)") != NULL);   /* (int16)0x1FFFF == -1 */
    setup(&s); memset(&o, 0, sizeof o);
    s.world.d0028A490 = anims; s.world.d0028A490_count = 2;
    CHECK(em_owner_services_001B1020(&s, &o, 0x13, 2, 0) == -1);
    CHECK(s.fault.code == EM_OWNER_FAULT_BAD_INDEX && s.fault.address == 0x0028A490u);

    /* Fail-stop: a NULL worker, a failing worker, a NULL view, the latch. */
    setup(&s); memset(&o, 0, sizeof o); s.workers.w_001AF780 = NULL;
    CHECK(em_owner_services_001B0FD0(&s, &o) == -1);
    CHECK(s.fault.code == EM_OWNER_FAULT_NULL_WORKER && s.fault.address == 0x001AF780u);
    trace[0] = 0;
    CHECK(em_owner_services_001B5B70(&s) == -1 && trace[0] == 0);   /* latched: nothing runs */
    setup(&s); memset(&o, 0, sizeof o); fail_at = 1;
    CHECK(em_owner_services_001B0EA0(&s, &o) == -1);
    CHECK(s.fault.code == EM_OWNER_FAULT_WORKER_FAILED && s.fault.address == 0x001C6120u);
    setup(&s); memset(&o, 0, sizeof o); s.world.d00275BCC = NULL;
    CHECK(em_owner_services_001B0EA0(&s, &o) == -1 && s.fault.address == 0x00275BCCu);
    setup(&s); memset(&o, 0, sizeof o); script_bind = 0;       /* 001C6150 on a NULL model */
    CHECK(em_owner_services_001B0EA0(&s, &o) == -1 && s.fault.address == 0x001C6150u);
    setup(&s); memset(&o, 0, sizeof o); model.bone_count = 57;  /* would alias +0x1F0 */
    CHECK(em_owner_services_001B0EA0(&s, &o) == -1 && s.fault.code == EM_OWNER_FAULT_BAD_INDEX);

    /* 001C6380: +0xD0 from TRS; a lone root bone gets +0xD0 x its local. */
    setup(&s); memset(&o, 0, sizeof o);
    o.scale[0] = o.scale[1] = o.scale[2] = 1.0f;
    o.pos[0] = 10.0f; o.pos[1] = 20.0f; o.pos[2] = 30.0f;
    o.bone_count = 1; o.bone[0] = &pool_bones[0];
    pool_bones[0].parent = -1;
    pool_bones[0].scale[0] = pool_bones[0].scale[1] = pool_bones[0].scale[2] = 0x1000;
    pool_bones[0].bind[0] = pool_bones[0].bind[5] = pool_bones[0].bind[10] = pool_bones[0].bind[15] = 1.0f;
    CHECK(em_owner_services_001C6380(&s, &o) == 0);
    CHECK(o.world[12] == 10.0f && o.world[13] == 20.0f && o.world[14] == 30.0f && o.world[15] == 1.0f);
    CHECK(pool_bones[0].world[12] == 10.0f && pool_bones[0].world[14] == 30.0f);
    pool_bones[0].parent = 4;                                   /* outside the owner's bones */
    CHECK(em_owner_services_001C6380(&s, &o) == -1 && s.fault.code == EM_OWNER_FAULT_BAD_INDEX);

    /* 001B17A0: the mode-6 gate, +1 and the publish only when visible. */
    setup(&s); memset(&o, 0, sizeof o);
    o.cls = 0x87; o.kind = 0; o.flags2 = 0x2D; script_visible = 2;
    CHECK(em_owner_services_001B17A0(&s, &o) == 2 && o.drawn == 2);
    CHECK(strcmp(trace, "1ce0 1630 1b70 ") == 0);
    setup(&s); memset(&o, 0, sizeof o);
    o.cls = 0x87; o.kind = 0; o.flags2 = 0x2C; o.drawn = 9;
    CHECK(em_owner_services_001B17A0(&s, &o) == 0 && o.drawn == 0 && strcmp(trace, "1630 ") == 0);
    /* f12/f13/f14 reach 001B1630 as raw bits: a signalling NaN keeps its payload. */
    setup(&s); memset(&o, 0, sizeof o);
    { const uint32_t snan = 0x7F800001u, neg0 = 0x80000000u, den = 0x00000001u;
      memcpy(&o.pos[0], &snan, 4); memcpy(&o.pos[1], &neg0, 4); memcpy(&o.pos[2], &den, 4); }
    CHECK(em_owner_services_001B17A0(&s, &o) == 0);
    CHECK(last_1630[0] == 0x7F800001u && last_1630[1] == 0x80000000u && last_1630[2] == 0x00000001u);

    /* 001CAA00: culled -> nothing drawn; drawn -> the order below. */
    setup(&s); memset(&o, 0, sizeof o); o.model = &model; o.pose_bone = 0xFF; script_cull = -1;
    CHECK(em_owner_services_001CAA00(&s, &o) == 0 && strcmp(trace, "a7b0 ") == 0 && channel.cursor == dl);
    setup(&s); memset(&o, 0, sizeof o); o.model = &model; o.pose_bone = 0xFF; script_cull = 3;
    o.bone_count = 1; o.bone[0] = &pool_bones[0]; o.collapsed_bone = -1; o.attachment = 1;
    CHECK(em_owner_services_001CAA00(&s, &o) == 0);
    CHECK(strcmp(trace, "a7b0 8c20(0) 89d0 1f80(0,1,0) a940 b3c0 ") == 0);
    CHECK(last_radius == 0x40C00000u);    /* 5.0 < 20.0: EE multiply 5.0 x 0x3F99999A (ee_float_model.ee_mul: 6.0) */
    /* Packet 0: CNT qwc 5, FLUSH, STCYCL, UNPACK V4-32 x4 -> 0x3F5; B rows. */
    CHECK(dl[0] == 5 && dl[1] == 0 && dl[2] == 0xAB && dl[3] == 0x10 && rd32(dl + 4) == 0);
    CHECK(dl[8] == 0xAB && dl[15] == 0xAB);                  /* tag +8..+0xF untouched */
    CHECK(rd32(dl + 0x10) == 0 && rd32(dl + 0x14) == 0x11000000u && rd32(dl + 0x18) == 0x01000101u);
    CHECK(rd32(dl + 0x1C) == 0x6C0403F5u);
    /* Node packet: CNT qwc 9, no FLUSH, UNPACK 8 qw to 0; cursor after it. */
    CHECK(dl[0x60] == 9 && dl[0x63] == 0x10 && rd32(dl + 0x74) == 0 && rd32(dl + 0x7C) == 0x6C080000u);
    CHECK(channel.cursor == dl + 0x60 + 10 * 16);
    for (int i = 0; i < 16; ++i) CHECK(scratch.s3440[i] == 0.0f);   /* B zeroed */
    setup(&s); memset(&o, 0, sizeof o); o.model = &model; o.pose_bone = 0xFF; script_cull = -2;
    CHECK(em_owner_services_001CAA00(&s, &o) == -1 && s.fault.code == EM_OWNER_FAULT_BAD_RESULT);
    setup(&s); memset(&o, 0, sizeof o); o.pose_bone = 2;         /* D_00275B40 view missing */
    CHECK(em_owner_services_001CAA00(&s, &o) == -1 && s.fault.address == 0x00275B40u);

    /* 001C7420: 32 bones -> chunks of 248 and 8 qw; the buffer bound faults. */
    setup(&s); memset(&o, 0, sizeof o); o.bone_count = 32; o.collapsed_bone = -1;
    for (int i = 0; i < 32; ++i) o.bone[i] = &pool_bones[i];
    uint8_t *first = NULL;
    CHECK(em_owner_services_001C7420(&s, &o, 0x3F5, 0, &first) == 0 && first == dl);
    CHECK(rd32(dl + 0x7C) == 0x6CF80000u && dl[0x60] == 0xF9);
    uint8_t *second = dl + 0x60 + (0xF8 + 2) * 16;
    CHECK(rd32(second + 0x1C) == 0x6C0800F8u && second[0] == 9);
    setup(&s); memset(&o, 0, sizeof o); o.bone_count = 1; o.bone[0] = &pool_bones[0];
    channel.end = dl + 0x60 + 9 * 16;                            /* one qword short */
    CHECK(em_owner_services_001C7420(&s, &o, 0x3F5, 0, NULL) == -1 && s.fault.code == EM_OWNER_FAULT_BAD_INDEX);

    /* A signalling-NaN radius (model +0x20) reaches 001CA7B0 unchanged: the EE
     * compare reads it as +MAX (not < 20.0), so the 1.2 multiply does not run. */
    setup(&s); memset(&o, 0, sizeof o); o.model = &model; o.pose_bone = 0xFF; script_cull = -1;
    { const uint32_t snan = 0x7F800001u; memcpy(&model.radius, &snan, 4); }
    CHECK(em_owner_services_001CAA00(&s, &o) == 0 && last_radius == 0x7F800001u);

    /* SDK routines return the em_ee_float status (every form is measured). */
    {
        float m[16], id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        const float zero3[3] = {0, 0, 0}, one3[3] = {1, 1, 1}, pos3[3] = {1, 2, 3};
        memset(m, 0x55, sizeof m);
        CHECK(em_owner_services_identity_001029C0(m) == 0 && memcmp(m, id, sizeof m) == 0);
        CHECK(em_owner_services_rotate_x_00102B08(m, m, 0u) == 0);
        CHECK(em_owner_services_rotate_y_00102BB0(m, m, 0x7F800001u) == 0);
        CHECK(em_owner_services_rotate_z_00102A60(m, m, 0xFF800000u) == 0);
        CHECK(em_owner_services_euler_00102C58(m, m, zero3) == 0);
        CHECK(em_owner_services_translate_00102918(m, m, pos3) == 0);
        CHECK(em_owner_services_build_trs_matrix(m, pos3, zero3, one3) == 0);
        CHECK(m[12] == 1.0f && m[13] == 2.0f && m[14] == 3.0f && m[15] == 1.0f);
    }

    /* 001B1E20: <0 stops, >0 uses (int16) of the duration, 0 the table's byte. */
    setup(&s);
    CHECK(em_owner_services_001B1E20(&s, 1, -5) == 0 && strcmp(trace, "6250 ") == 0);
    CHECK(em_owner_services_001B1E20(&s, 1, 0) == 0 && last_big == 0 && last_small == 90 && last_duration == 12);
    CHECK(em_owner_services_001B1E20(&s, 0, 0x18000) == 0 && last_duration == -32768);
    CHECK(em_owner_services_001B1E20(&s, 2, 1) == -1 && s.fault.address == 0x0024D6F0u);

    /* 001B5B70: gated countdown; at 0 it stops the actuator. */
    setup(&s); e56 = 0; e68 = 3;
    CHECK(em_owner_services_001B5B70(&s) == 0 && e68 == 3);
    e56 = 1;
    CHECK(em_owner_services_001B5B70(&s) == 0 && e68 == 2 && trace[0] == 0);
    e68 = 0;
    CHECK(em_owner_services_001B5B70(&s) == 0 && e68 == 0 && strcmp(trace, "6250 ") == 0);
    e68 = -32768;
    CHECK(em_owner_services_001B5B70(&s) == 0 && e68 == 32767);

    if (failures) { fprintf(stderr, "owner_services_test: %d failure(s)\n", failures); return 1; }
    printf("owner_services_test: PASS\n");
    return 0;
}
