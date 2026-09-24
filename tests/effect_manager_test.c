/* effect_manager_test.c - the em_effect_manager contract (ASan/UBSan):
 * fail-stop on NULL workers and views before any write, latched faults,
 * BAD_INDEX outside the ELF windows, the clamped clip transform,
 * the worker protocol of one barrel frame and one 001F0720 age step.
 * Needs no disc data (the tables stay zero); the values it checks come from
 * the original-instruction oracle tools/test_effect_manager_reference.py. */
#include "game/em_effect_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

typedef struct {
    uint8_t packets[8][0xC1 * 16];
    int32_t counts[8];
    int opened, c760, c900, c5C20, c9A0, list_zero;
} Log;

static int w_5C20(void *ctx) { ((Log *)ctx)->c5C20++; return 0; }
static int w_5CA0(void *ctx, uint32_t *list) { (void)ctx; *list = 0; return 0; }
static int w_5F0(void *ctx, uint32_t chain, int32_t id, int32_t count, uint8_t **out)
{
    Log *l = ctx;
    (void)id;
    if (chain != EM_EFFECT_MANAGER_CHAIN || count > 0xC1) return -1;
    l->counts[l->opened % 8] = count;
    *out = l->packets[l->opened++ % 8];
    return 0;
}
static int w_760(void *ctx, uint32_t chain, int32_t id, uint32_t address)
{
    (void)chain; (void)id;
    if (address != EM_EFFECT_MANAGER_MICROCODE) return -1;
    ((Log *)ctx)->c760++;
    return 0;
}
static int w_900(void *ctx, uint32_t chain, int32_t id, int32_t mode)
{
    (void)chain; (void)id; (void)mode;
    ((Log *)ctx)->c900++;
    return 0;
}
static int w_fail(void *ctx, uint32_t chain, int32_t id, int32_t mode)
{
    (void)ctx; (void)chain; (void)id; (void)mode;
    return -1;
}
static int w_9A0(void *ctx, int32_t mode, uint32_t f12, uint32_t f13)
{
    (void)mode; (void)f12; (void)f13;
    ((Log *)ctx)->c9A0++;
    return 0;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

typedef struct {
    EmEffectManagerTables tables;
    EmEffectManagerGlobals globals;
    EmEffectOriginalDecals decals;
    EmEffectManagerView view;
    EmEffectManagerEntity entities[EM_EFFECT_MANAGER_ENTITIES];
    EmEffectManagerWorkers workers;
    Log log;
    EmEffectManager m;
} Rig;

static Rig *rig(void)
{
    Rig *r = calloc(1, sizeof *r);
    if (!r) exit(1);
    for (int i = 0; i < EM_EFFECT_MANAGER_ENTITIES; ++i) r->entities[i].live = 1;
    r->globals.d810700 = 0x0B;           /* AREA11 */
    r->workers.ctx = &r->log;
    r->workers.w_001F5C20 = w_5C20;
    r->workers.w_001F5CA0 = w_5CA0;
    r->workers.w_001CB5F0 = w_5F0;
    r->workers.w_001CB760 = w_760;
    r->workers.w_001CB900 = w_900;
    r->workers.w_0021B9A0 = w_9A0;
    r->m.tables = &r->tables;
    r->m.globals = &r->globals;
    r->m.decals = &r->decals;
    r->m.view = &r->view;
    r->m.entities = r->entities;
    r->m.workers = &r->workers;
    return r;
}

static void barrel_frame(void)
{
    Rig *r = rig();
    r->globals.d275C44 = 5;
    CHECK(em_effect_manager_001F0360(&r->m) == 0);
    CHECK(r->m.fault.code == 0);
    CHECK(r->log.c5C20 == 1);
    CHECK(r->globals.d275C44 == 4);           /* 001F40C0 */
    CHECK(r->log.opened == 24 && r->log.c760 == 6 && r->log.c900 == 6); /* lanes 0, 1, 3, 4, 5, 6 */
    free(r);
}

static void lane_packets(void)
{
    Rig *r = rig();
    r->decals.slot[3][0].life = 7;              /* below 2 * 60: the float path */
    r->decals.slot[3][1].life = -4;             /* stored as 0 */
    r->decals.slot[3][2].life = 500;            /* decremented only */
    CHECK(em_effect_manager_001F0720(&r->m, 3) == 0);
    CHECK(r->decals.slot[3][0].life == 6 && r->decals.slot[3][1].life == 0 && r->decals.slot[3][2].life == 499);
    CHECK(r->log.opened == 4);
    CHECK(r->log.counts[0] == 1 && r->log.counts[1] == 0xC1 && r->log.counts[2] == 5 && r->log.counts[3] == 0xA);
    CHECK(rd32(r->log.packets[0]) == 0x11000000u && rd32(r->log.packets[0] + 4) == 0x14000000u);
    CHECK(rd32(r->log.packets[1] + 0xC) == 0x6CC00020u);
    CHECK(rd32(r->log.packets[1] + 0x10 + 0x58) == 6u); /* the copied lane carries the new countdown */
    CHECK(rd32(r->log.packets[2] + 0xC) == 0x6C04000Eu && rd32(r->log.packets[3] + 0xC) == 0x6C090000u);
    /* Out-of-range presets return without a call or a write. */
    r->log.opened = 0;
    CHECK(em_effect_manager_001F0720(&r->m, 7) == 0 && em_effect_manager_001F0720(&r->m, -1) == 0);
    CHECK(r->log.opened == 0 && r->decals.slot[3][0].life == 6);
    free(r);
}

static void fail_stop(void)
{
    /* A NULL chain worker: the barrel faults before any write or call. */
    Rig *r = rig();
    r->workers.w_001CB900 = NULL;
    r->globals.d275C44 = 5;
    CHECK(em_effect_manager_001F0360(&r->m) == -1);
    CHECK(r->m.fault.code == EM_EFFECT_MANAGER_FAULT_NULL_WORKER && r->m.fault.address == 0x001CB900u);
    CHECK(r->globals.d275C44 == 5 && r->log.c5C20 == 0 && r->log.opened == 0);
    /* Latched: every later call returns -1 without touching anything. */
    r->workers.w_001CB900 = w_900;
    CHECK(em_effect_manager_001F0720(&r->m, 0) == -1 && r->log.opened == 0);
    CHECK(em_effect_manager_001F40C0(&r->m) == -1 && r->globals.d275C44 == 5);
    free(r);

    /* A failing worker latches WORKER_FAILED at its address. */
    r = rig();
    r->workers.w_001CB900 = w_fail;
    CHECK(em_effect_manager_001F0720(&r->m, 0) == -1);
    CHECK(r->m.fault.code == EM_EFFECT_MANAGER_FAULT_WORKER_FAILED && r->m.fault.address == 0x001CB900u);
    free(r);

    /* NULL views. */
    r = rig();
    r->m.decals = NULL;
    CHECK(em_effect_manager_001F0720(&r->m, 0) == -1 && r->m.fault.code == EM_EFFECT_MANAGER_FAULT_NULL_WORKER);
    free(r);
    CHECK(em_effect_manager_001F0360(NULL) == -1);
}

static int keep_live(void *ctx, uint32_t address, int32_t kind, EmEffectManagerEntity *e)
{
    (void)ctx; (void)address; (void)kind; (void)e;
    return 0;
}

static int sine(void *ctx, uint32_t x, uint32_t *out)
{
    (void)ctx; (void)x;
    *out = 0;
    return 0;
}

static void bad_index(void)
{
    /* 001F40C0: a live entity whose kind lies past D_0025AD70 (the record
     * +0x50 read at 0x25ADC0 is outside the window). */
    Rig *r = rig();
    r->entities[5].live = 0;
    r->entities[5].kind = 27;
    r->workers.w_001F3620 = keep_live;
    r->globals.d275C44 = 9;
    CHECK(em_effect_manager_001F40C0(&r->m) == -1);
    CHECK(r->m.fault.code == EM_EFFECT_MANAGER_FAULT_BAD_INDEX && r->m.fault.address == 0x0025ADC0u);
    CHECK(r->globals.d275C44 == 9);
    free(r);

    /* The draw block: a record outside the aura window. */
    r = rig();
    r->workers.w_0011E2A8 = sine;
    const uint32_t m16[16] = {0};
    CHECK(em_effect_manager_aura_draw(&r->m, m16, 0x0025A0F0u, 0, 0) == -1);
    CHECK(r->m.fault.code == EM_EFFECT_MANAGER_FAULT_BAD_INDEX && r->m.fault.address == 0x0025A0F0u);
    free(r);
}

static void clamped_clip(void)
{
    /* 001F0A60: a NaN in the clip matrix is clamped by the VMULAx form (to
     * +MAX), so the VCLIP input stays finite and the UNMEASURED guard is not
     * reached; the call proceeds to 0021B9A0. */
    Rig *r = rig();
    r->view.clip0[0] = 0x7FC00000u;
    r->view.clip0[5] = r->view.clip0[10] = r->view.clip0[15] = 0x3F800000u;
    const uint32_t pos[4] = {0x3F800000u, 0, 0, 0x3F800000u};
    (void)em_effect_manager_001F0A60(&r->m, 0, 1, pos, 0, 0, 0, 0, 0);
    CHECK(r->m.fault.code != EM_EFFECT_MANAGER_FAULT_UNMEASURED);
    free(r);
}

int main(void)
{
    barrel_frame();
    lane_packets();
    fail_stop();
    bad_index();
    clamped_clip();
    if (failures) {
        fprintf(stderr, "effect manager test: %d failure(s)\n", failures);
        return 1;
    }
    printf("effect manager test: PASS\n");
    return 0;
}
