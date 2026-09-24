/* Native contract of em_owner_draw_original (docs/OWNER_DRAW.md). The
 * arithmetic and every packet byte are checked against the original
 * instructions by tools/test_owner_draw_reference.py; this file pins the
 * fail-stop contract, the packet layout, the bank parser's refusals and
 * the 001C6120 masking. With EM_WORLD_MODELS=<path to world_models.emwm>
 * (or the default assets path, when present) it also parses the exported
 * AREA11 bank and checks the ids the world owners bind. */
#include "game/em_owner_draw_original.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* ---- 001CA7B0 ---------------------------------------------------------- */

static uint32_t view[16], planes[16];

static void frustum(void)
{
    const float v[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float p[16] = {0.4247739315f, 0, 0.9052994848f, 0, -0.4247739315f, 0, 0.9052994848f, 0,
                         0, 0.4247739315f, 0.9052994848f, 0, 0, -0.4247739315f, 0.9052994848f, 0};
    for (int i = 0; i < 16; ++i) { view[i] = fbits(v[i]); planes[i] = fbits(p[i]); }
}

static int32_t cull(EmOwnerDraw *d, float x, float y, float z, float r)
{
    uint32_t pos[4] = {fbits(x), fbits(y), fbits(z), 0x7FC00000u}; /* w never matters */
    int32_t flags = 0x5A5A;
    CHECK(em_owner_draw_001CA7B0(d, pos, fbits(r), &flags) == 0);
    return flags;
}

static void test_cull(void)
{
    EmOwnerDraw d;
    memset(&d, 0, sizeof d);
    frustum();
    d.world.d00810610 = view;
    d.world.ctx_2410 = planes;
    CHECK(cull(&d, 0, 0, 100, 10) == 0);       /* in front, inside every plane */
    CHECK(cull(&d, 0, 0, -50, 10) == -1);      /* behind: view z < -radius */
    CHECK(cull(&d, 0, 0, 5, 10) == 31);        /* crosses the near side and every plane */
    CHECK(cull(&d, 300, 0, 100, 10) == -1);    /* outside plane 1 */
    CHECK(cull(&d, 0, 0, -10, 10) == 31);      /* z == -radius is not culled (strict less-than) */
    CHECK(cull(&d, 0, 0, 10, 10) == 30);       /* z == radius sets no near bit */
    /* A negative radius: -r = +5, so view z 0 is culled. */
    CHECK(cull(&d, 0, 0, 0, -5) == -1);
    CHECK(d.fault.code == 0);

    /* Fail-stop: NULL views and arguments, then the latch. */
    uint32_t pos[4] = {0, 0, 0, 0};
    int32_t flags = 7;
    d.world.ctx_2410 = NULL;
    CHECK(em_owner_draw_001CA7B0(&d, pos, 0, &flags) == -1 && flags == 7);
    CHECK(d.fault.code == EM_OWNER_DRAW_FAULT_NULL_WORKER && d.fault.address == 0x00275670u);
    d.world.ctx_2410 = planes;
    CHECK(em_owner_draw_001CA7B0(&d, pos, 0, &flags) == -1 && flags == 7);   /* latched */
    memset(&d.fault, 0, sizeof d.fault);
    d.world.d00810610 = NULL;
    CHECK(em_owner_draw_001CA7B0(&d, pos, 0, &flags) == -1 && d.fault.address == 0x00810610u);
    memset(&d.fault, 0, sizeof d.fault);
    d.world.d00810610 = view;
    CHECK(em_owner_draw_001CA7B0(&d, NULL, 0, &flags) == -1 && d.fault.address == 0x001CA7B0u);
    CHECK(em_owner_draw_001CA7B0(NULL, pos, 0, &flags) == -1);
}

/* ---- 001CA940 ---------------------------------------------------------- */

enum { WINDOW = 0x100 };
static uint8_t window[WINDOW];
static EmOwnerServicesChannel channel;
static uint32_t ctx0c, ctx9c, arena, ctx50;

static void reset(EmOwnerDraw *d, uint32_t flag_word, uint32_t slot, size_t room)
{
    memset(d, 0, sizeof *d);
    for (int i = 0; i < WINDOW; ++i) window[i] = (uint8_t)(0xA5 ^ i);
    channel.cursor = window;
    channel.end = window + room;
    ctx0c = flag_word;
    ctx9c = slot;
    arena = 0x00814220u;
    ctx50 = 0xDEADBEEFu;
    d->world.ctx_0C = &ctx0c;
    d->world.ctx_9C = &ctx9c;
    d->world.d00275674 = &arena;
    d->world.channel = &channel;
    d->world.channel_count = 1;
    d->world.ctx_50 = &ctx50;
    d->world.ctx_50_count = 1;
}

/* Tag k of the window: (id byte, qwc halfword, address word), and bytes +2 and
 * +8..+0xF must still hold the pattern. */
static void tag_is(int k, uint8_t id, uint32_t qwc, uint32_t address)
{
    const uint8_t *p = window + 16 * k;
    CHECK(p[3] == id);
    CHECK(((uint32_t)p[0] | (uint32_t)p[1] << 8) == qwc);
    CHECK(rd32(p + 4) == address);
    CHECK(p[2] == (uint8_t)(0xA5 ^ (16 * k + 2)));
    for (int i = 8; i < 16; ++i) CHECK(p[i] == (uint8_t)(0xA5 ^ (16 * k + i)));
}

static void test_kernel_submit(void)
{
    EmOwnerDraw d;
    const uint32_t model = 0x01392F40u;

    CHECK(em_owner_draw_001CA940_bytes(0, 0) == 0x40u && em_owner_draw_001CA940_bytes(1, 0) == 0x80u);
    CHECK(em_owner_draw_001CA940_bytes(0, 1) == 0x50u && em_owner_draw_001CA940_bytes(0x1F, 1) == 0xA0u);
    CHECK(em_owner_draw_001CA940_bytes(-1, 0) == 0x80u && em_owner_draw_001CA940_bytes(2, 0) == 0x40u);

    /* Plain: flag word bit 0 set (no REF 2), skin slot 1. */
    reset(&d, 0x43, 1, WINDOW);
    CHECK(em_owner_draw_001CA940_at(&d, 0x1E, model, 0x0001030Cu) == 0 && d.fault.code == 0);
    CHECK(channel.cursor == window + 0x40);
    tag_is(0, 0x30, 8, 0x00816440u + 0x80u);
    tag_is(1, 0x30, 1, 0x00814220u);
    tag_is(2, 0x50, 0, 0x0023C750u);
    tag_is(3, 0x30, 0x030C, model + 0x40u);                 /* low halfword of +0x04 */
    CHECK(ctx50 == 0x0023C750u);
    CHECK(window[0x40] == (uint8_t)(0xA5 ^ 0x40));          /* nothing past the run */

    /* Clip: flags bit 0, flag word bit 0 clear (REF 2 in both submits). */
    reset(&d, 0x42, 0, WINDOW);
    CHECK(em_owner_draw_001CA940_at(&d, 0x11, model, 0x30C) == 0 && d.fault.code == 0);
    CHECK(channel.cursor == window + 0xA0);
    tag_is(0, 0x30, 8, 0x00816440u);
    tag_is(1, 0x30, 1, 0x00814220u);
    tag_is(2, 0x50, 0, 0x0023C750u);
    tag_is(3, 0x30, 2, 0x002514B0u);
    tag_is(4, 0x30, 0x30C, model + 0x40u);
    tag_is(5, 0x30, 8, 0x00816540u);
    tag_is(6, 0x30, 1, 0x00814220u);
    tag_is(7, 0x50, 0, 0x002354A0u);
    tag_is(8, 0x30, 2, 0x002514B0u);
    tag_is(9, 0x30, 0x30C, model + 0x40u);
    CHECK(ctx50 == 0x002354A0u);

    /* The window is checked before any write: one byte short faults. */
    reset(&d, 0x43, 0, 0x7F);
    CHECK(em_owner_draw_001CA940_at(&d, 1, model, 0x30C) == -1);
    CHECK(d.fault.code == EM_OWNER_DRAW_FAULT_BAD_INDEX && d.fault.address == 0x001D3BA0u);
    CHECK(channel.cursor == window && window[3] == (uint8_t)(0xA5 ^ 3) && ctx50 == 0xDEADBEEFu);
    CHECK(em_owner_draw_001CA940_at(&d, 0, model, 0x30C) == -1);          /* latched */
    reset(&d, 0x42, 0, 0x4F);
    CHECK(em_owner_draw_001CA940_at(&d, 0, model, 0x30C) == -1 && d.fault.address == 0x001D38A0u);

    /* Views. */
    reset(&d, 0x43, 0, WINDOW);
    d.world.ctx_9C = NULL;
    CHECK(em_owner_draw_001CA940_at(&d, 0, model, 1) == -1 && d.fault.code == EM_OWNER_DRAW_FAULT_NULL_WORKER);
    reset(&d, 0x43, 0, WINDOW);
    d.world.d00275674 = NULL;
    CHECK(em_owner_draw_001CA940_at(&d, 0, model, 1) == -1 && d.fault.address == 0x00275674u);
    reset(&d, 0x43, 0, WINDOW);
    d.world.channel_count = 0;
    CHECK(em_owner_draw_001CA940_at(&d, 0, model, 1) == -1 && d.fault.code == EM_OWNER_DRAW_FAULT_BAD_INDEX);
    reset(&d, 0x43, 0, WINDOW);
    CHECK(em_owner_draw_001D38A0(&d, 1, model, 1) == -1 && d.fault.code == EM_OWNER_DRAW_FAULT_BAD_INDEX);
    reset(&d, 0x43, 0, WINDOW);
    CHECK(em_owner_draw_001CA940(&d, 0, NULL) == -1 && d.fault.address == 0x0028A59Cu);
    CHECK(em_owner_draw_001CA940(NULL, 0, NULL) == -1);
}

/* ---- the bank ---------------------------------------------------------- */

/* A two-model EMWM: table (count 2) at 0x01000000, model 0 (1 block,
 * 1 bone) at +0x40, model 1 (2 blocks, 2 bones) after it. */
static uint8_t *synthetic(size_t *size)
{
    const uint32_t m0 = 0x40, m0_size = 0x40 + 0x82 * 16 + 0x50;
    const uint32_t m1 = m0 + ((m0_size + 15u) & ~15u), m1_size = 0x40 + 2 * 0x82 * 16 + 2 * 0x50;
    const uint32_t span = m1 + m1_size;
    uint8_t *f = calloc(1, 0x20 + span);
    if (!f) exit(2);
    memcpy(f, "EMWM", 4);
    wr32(f + 4, 1);
    wr32(f + 8, 0x01000000u);
    wr32(f + 0xC, span);
    wr32(f + 0x10, 2);
    uint8_t *t = f + 0x20;
    wr32(t, 2);
    wr32(t + 4, m0 | 1u);       /* low bits are dropped by >> 2 << 2 */
    wr32(t + 8, m1);
    const uint32_t at[2] = {m0, m1}, blocks[2] = {1, 2}, bones[2] = {1, 2};
    for (int m = 0; m < 2; ++m) {
        uint8_t *p = t + at[m];
        wr32(p, blocks[m]);
        wr32(p + 4, blocks[m] * 0x82);
        wr32(p + 8, bones[m]);
        wr32(p + 0xC, 0x40 + blocks[m] * 0x82 * 16);
        wr32(p + 0x20, fbits(m ? 13.5f : 17.25f));
        for (uint32_t b = 0; b < blocks[m]; ++b) {
            uint8_t *blk = p + 0x40 + b * 0x82 * 16;
            wr32(blk + 8, 0x01000404u);
            wr32(blk + 12, 0x6C808000u);
            wr32(blk + 0x81 * 16, b ? 0x17000000u : 0x14000000u);
        }
        uint8_t *r = p + 0x40 + blocks[m] * 0x82 * 16;
        for (uint32_t k = 0; k < bones[m]; ++k, r += 0x50) {
            r[4] = (uint8_t)(k ? 0 : 0xFF);
            r[5] = (uint8_t)(k ? 0 : 0xFF);
            wr32(r + 0x10, fbits(1.0f + (float)k));
        }
    }
    *size = 0x20 + span;
    return f;
}

static int parses(uint8_t *f, size_t size)
{
    static EmWorldModels bank;
    int rc = em_world_models_parse(&bank, f, size);
    if (rc != 0) CHECK(bank.model_count == 0 && bank.span == NULL);
    return rc;
}

static void test_bank(void)
{
    size_t size;
    uint8_t *f = synthetic(&size);
    static EmWorldModels bank;
    CHECK(em_world_models_parse(&bank, f, size) == 0);
    CHECK(bank.table_address == 0x01000000u && bank.count == 2 && bank.record_count == 3);
    const EmWorldModel *m0 = em_world_models_at(&bank, 0x01000040u);
    const EmWorldModel *m1 = em_world_models_at(&bank, bank.models[1].address);
    CHECK(m0 && m0->id == 0 && m0->w04 == 0x82 && m0->blocks == 1 && m0->model.bone_count == 1);
    CHECK(m1 && m1->id == 1 && m1->w04 == 0x104 && m1->model.bone_count == 2);
    CHECK(m0 && fbits(m0->model.radius) == fbits(17.25f));
    CHECK(m1 && m1->model.skeleton[0].parent == -1 && m1->model.skeleton[1].parent == 0);
    CHECK(m1 && fbits(m1->model.skeleton[1].bind[0]) == fbits(2.0f));
    CHECK(m1 && em_world_models_of(&bank, &m1->model) == m1 && em_world_models_of(&bank, NULL) == NULL);

    uint32_t h = 0;
    CHECK(em_world_models_001C6120(&bank, 0x01000000u, 1, &h) == 0 && m1 && h == m1->address);
    CHECK(em_world_models_001C6120(&bank, 0x01000000u, 0x8001, &h) == 0 && m1 && h == m1->address);
    CHECK(em_world_models_001C6120(&bank, 0x01000000u, 0xFFFF0000u, &h) == 0 && h == 0x01000040u);
    h = 99;
    CHECK(em_world_models_001C6120(&bank, 0x01000000u, 2, &h) == -1 && h == 99);   /* past the table */
    CHECK(em_world_models_001C6120(&bank, 0x01000010u, 0, &h) == -1);              /* another bank */
    CHECK(em_world_models_001C6120(NULL, 0x01000000u, 0, &h) == -1);

    /* Model views through the draw worker. */
    EmOwnerDraw d;
    reset(&d, 0x43, 0, WINDOW);
    d.world.models = &bank;
    CHECK(m1 && em_owner_draw_001CA940(&d, 0, &m1->model) == 0 && d.fault.code == 0);
    tag_is(3, 0x30, 0x104, bank.models[1].address + 0x40u);
    EmOwnerModel stranger;
    memset(&stranger, 0, sizeof stranger);
    reset(&d, 0x43, 0, WINDOW);
    d.world.models = &bank;
    CHECK(em_owner_draw_001CA940(&d, 0, &stranger) == -1 && d.fault.address == 0x001CA940u);

    /* Refusals (each leaves the bank zeroed). */
    struct { uint32_t at; uint32_t value; } bad[] = {
        {0x00, 0x584D574Du},                        /* magic */
        {0x04, 2},                                  /* version */
        {0x14, 1},                                  /* reserved */
        {0x10, 3},                                  /* count != table word 0 */
        {0x20 + 0x04, 0x08},                        /* offset inside the table */
        {0x20 + 0x40 + 0x48, 0},                    /* block head */
        {0x20 + 0x40 + 0x40 + 0x81 * 16, 0x17000000u}, /* MSCNT on the first block */
        {0x20 + 0x40 + 0x0C, 0x44},                 /* skeleton offset */
        {0x20 + 0x40 + 0x08, 200},                  /* bones past the span */
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        uint8_t *g = malloc(size);
        if (!g) exit(2);
        memcpy(g, f, size);
        wr32(g + bad[i].at, bad[i].value);
        if (parses(g, size) != -1) fprintf(stderr, "refusal %zu accepted\n", i), ++failures;
        free(g);
    }
    CHECK(parses(f, size - 1) == -1);                /* span size */
    CHECK(parses(f, 8) == -1 && parses(NULL, 0) == -1);
    free(f);
}

static void test_asset(void)
{
    const char *path = getenv("EM_WORLD_MODELS");
    if (!path) path = "assets/scene_snow/world_models.emwm";
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("owner_draw_test: %s absent (tools/export_world_models.py); asset check skipped\n", path);
        return;
    }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n);
    if (!data || fread(data, 1, (size_t)n, fp) != (size_t)n) exit(2);
    fclose(fp);
    static EmWorldModels bank;
    CHECK(em_world_models_parse(&bank, data, (size_t)n) == 0);
    CHECK(bank.table_address == 0x01335F40u && bank.count == 21);
    /* The ids the AREA11 world owners bind (00827490 06, 00825940 08, truck
     * 09, crates 0D, drums 0E, elevator 0F, fan 13) and their bone counts. */
    static const struct { uint32_t id; uint8_t bones; } owned[] = {
        {0x06, 2}, {0x08, 4}, {0x09, 1}, {0x0D, 1}, {0x0E, 2}, {0x0F, 1}, {0x13, 1}};
    for (size_t i = 0; i < sizeof owned / sizeof owned[0]; ++i) {
        uint32_t h = 0;
        CHECK(em_world_models_001C6120(&bank, 0x01335F40u, owned[i].id, &h) == 0);
        const EmWorldModel *m = em_world_models_at(&bank, h);
        CHECK(m && m->id == owned[i].id && m->model.bone_count == owned[i].bones);
    }
    free(data);
}

int main(void)
{
    test_cull();
    test_kernel_submit();
    test_bank();
    test_asset();
    if (failures) {
        fprintf(stderr, "owner_draw_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("owner_draw_test: ok\n");
    return 0;
}
