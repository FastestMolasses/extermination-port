/* Native contract test for em_effect_kinds.c (ASan/UBSan). Needs no disc
 * data: it checks the module's own contract (bounds, fail-stop, latching,
 * worker plumbing) on synthetic tables. Behaviour against the original is
 * tools/test_effect_kinds_reference.py. */
#include "game/em_effect_kinds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

typedef struct {
    int xf, draw, light, unlight, emit;
    uint32_t last_source, last_f13;
    int32_t last_copy, last_id, next_handle;
} Log;

static int w_xf(void *ctx, uint32_t dst, uint32_t a1, const float src[16], uint32_t f12,
                uint32_t f13, uint32_t f14, uint32_t f15, uint32_t f16)
{
    Log *l = ctx;
    (void)src; (void)f12; (void)f14; (void)f15; (void)f16;
    CHECK(dst == EM_EFFECT_KINDS_XF && a1 == 0);
    l->xf++;
    l->last_f13 = f13;
    return 0;
}

static int w_draw(void *ctx, int32_t id, int32_t kind, uint32_t source, uint32_t xf, int32_t copy)
{
    Log *l = ctx;
    CHECK(kind == 1 && xf == EM_EFFECT_KINDS_XF);
    l->draw++;
    l->last_id = id;
    l->last_source = source;
    l->last_copy = copy;
    return 0;
}

static int w_light(void *ctx, const float pos[4], uint32_t tv, const float color[4], int32_t type,
                   uint32_t f12, uint32_t f13, int32_t *handle)
{
    Log *l = ctx;
    (void)pos; (void)color;
    CHECK(tv >= EM_EFFECT_KINDS_TEMPLATE_BASE && type == 1 && f12 == 0x3F800000u && f13 == 0);
    l->light++;
    *handle = l->next_handle++;
    return 0;
}

static int w_unlight(void *ctx, int32_t handle)
{
    (void)handle;
    ((Log *)ctx)->unlight++;
    return 0;
}

static int w_emit_fail(void *ctx, const float pos[4], const int32_t col[4], uint32_t f12, uint32_t f13)
{
    (void)pos; (void)col; (void)f12; (void)f13;
    ((Log *)ctx)->emit++;
    return -1;
}

static void put16(uint8_t *p, int16_t v) { memcpy(p, &v, 2); }
static void put32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); }
static int32_t get32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

int main(void)
{
    static EmEffectKindsTables tables;
    static EmEffectOriginalDecals decals;
    static EmEffectKindsParticles particles;
    EmEffectKindsGlobals globals;
    Log log;
    memset(&globals, 0, sizeof globals);
    memset(&log, 0, sizeof log);
    EmEffectKindsWorkers workers = {0};
    workers.ctx = &log;
    workers.w_001CFB50 = w_xf;
    workers.w_001CFBE0 = w_draw;
    workers.w_001D7FA0 = w_light;
    workers.w_001D80B0 = w_unlight;
    EmEffectKinds k = {&tables, &globals, &decals, &particles, &workers, {0, 0}};

    /* Selectors: the AREA11 key and an unmapped key. */
    CHECK(em_effect_kinds_001F5640(0x0B, 0) == 0x0025B590u);
    CHECK(em_effect_kinds_001F5CA0(0x0B, 0) == 0);
    CHECK(em_effect_kinds_001F6760(0x0B, 0) == 0);
    CHECK(em_effect_kinds_001F6D60(0x0B, 0) == 0x0025D5A0u);
    CHECK(em_effect_kinds_001F6D60(0xFF, 0xFF) == 0);

    /* Handlers: the LCG and the per-handler sources/copies. */
    float m[16] = {0};
    EmEffectOriginalWork work = {0, 0x10000, 0, 0, 0, 0};
    CHECK(em_effect_kinds_001EC470(&k, m, 7, &work) == 0);
    CHECK(log.xf == 2 && log.draw == 2 && log.last_source == 0x002569D0u && log.last_copy == 1);
    CHECK(work.seed_copy == (int32_t)((0x10000u * 0x25u + 0xBu) * 0x25u + 0xBu));
    CHECK(em_effect_kinds_handler(&k, EM_EFFECT_KINDS_H_001EBF10, m, 9, &work) == 0);
    CHECK(log.draw == 5 && log.last_id == 9 && log.last_copy == 0 && log.last_source == 0x00256670u);
    CHECK(globals.spad36A0[0] == 0x3F800000u && globals.spad36A0[12] == 0x43C58000u);
    CHECK(em_effect_kinds_handler(&k, EM_EFFECT_KINDS_H_001EC3F0, m, 1, &work) == 0 && log.last_copy == 1);
    CHECK(em_effect_kinds_handler(&k, EM_EFFECT_KINDS_H_001EC1F0, m, 1, &work) == 0 && log.last_copy == 0);

    /* Untranslated handler: fault, then everything latches. */
    CHECK(em_effect_kinds_handler(&k, 0x001EAD70u, m, 1, &work) == -1);
    CHECK(k.fault.code == EM_EFFECT_KINDS_FAULT_UNTRANSLATED && k.fault.address == 0x001EAD70u);
    int draws = log.draw;
    CHECK(em_effect_kinds_001EC3F0(&k, m, 1, &work) == -1 && log.draw == draws);
    CHECK(em_effect_kinds_001F3FA0(&k) == -1);
    k.fault.code = 0;
    k.fault.address = 0;

    /* Resets. */
    memset(&decals, 0xA5, sizeof decals);
    memset(&particles, 0x5A, sizeof particles);
    globals.d275C40 = globals.d275C44 = 7;
    CHECK(em_effect_kinds_001F0310(&k) == 0);
    CHECK(decals.index[0] == 0 && decals.index[2] != 0 && decals.index[6] == 0);
    CHECK(decals.slot[3][31].source[5] == 0x3F800000u && decals.slot[3][31].source[1] == 0);
    CHECK(decals.slot[3][31].tag == 0 && decals.slot[3][31].life == 0 && decals.slot[3][31].params[0] == 0xA5A5A5A5u);
    CHECK(decals.slot[2][0].life != 0);
    CHECK(particles.record[0x7F][0x80] == 1 && particles.record[0x7F][0x81] == 0 && particles.record[0][0] == 0);
    CHECK(globals.d275C40 == 0 && globals.d275C44 == 0);
    CHECK(em_effect_kinds_001F03D0(&k, 7) == -1 && k.fault.code == EM_EFFECT_KINDS_FAULT_BAD_INDEX);
    k.fault.code = 0;

    /* Point-light lists on a synthetic list at 0x25D5A0: two records, then
     * the terminator. */
    memset(&tables, 0, sizeof tables);
    uint8_t *list = tables.lists + (0x0025D5A0u - EM_EFFECT_KINDS_LISTS_BASE);
    put16(list + 4, 2);
    put32(list + 0x24, 5);
    put16(list + 0x28 + 4, 3);
    put32(list + 0x28 + 0x24, -1);
    put16(list + 0x50, -1);
    globals.d810700 = 0x0B;
    globals.d810701 = 0;
    log.next_handle = 40;
    CHECK(em_effect_kinds_001F6E40(&k) == 0);
    CHECK(log.unlight == 1 && log.light == 2);
    CHECK(get32(list + 0x24) == 40 && get32(list + 0x28 + 0x24) == 41);
    put16(list + 0x28 + 4, 4);                        /* preset outside 0..3 */
    put32(list + 0x28 + 0x24, -1);
    CHECK(em_effect_kinds_001F6640(&k, 0x0025D5A0u) == -1 && k.fault.code == EM_EFFECT_KINDS_FAULT_BAD_INDEX);
    k.fault.code = 0;

    /* A list with no terminator inside the window faults at the window end. */
    memset(&tables, 0, sizeof tables);
    int unlights = log.unlight;
    CHECK(em_effect_kinds_001F66F0(&k, EM_EFFECT_KINDS_LISTS_END - 0x28) == -1 &&
          k.fault.code == EM_EFFECT_KINDS_FAULT_BAD_INDEX && log.unlight == unlights + 1);
    k.fault.code = 0;
    CHECK(em_effect_kinds_001F66F0(&k, 0x00100000u) == -1 && k.fault.code == EM_EFFECT_KINDS_FAULT_BAD_INDEX);
    k.fault.code = 0;

    /* NULL worker and failing worker (marker kind 4: plain emit mode). */
    put16(tables.lists + (0x0025B590u - EM_EFFECT_KINDS_LISTS_BASE) + 4, 4);
    CHECK(em_effect_kinds_001F5C20(&k) == -1 && k.fault.code == EM_EFFECT_KINDS_FAULT_NULL_WORKER &&
          k.fault.address == 0x001F4D40u);
    k.fault.code = 0;
    workers.w_001F4D40 = w_emit_fail;
    CHECK(em_effect_kinds_001F5C20(&k) == -1 && k.fault.code == EM_EFFECT_KINDS_FAULT_WORKER_FAILED &&
          log.emit == 1);
    k.fault.code = 0;
    float out[4], color[4] = {1, 1, 1, 1};
    CHECK(em_effect_kinds_001F54E0(&k, NULL, out, 0x1234u, color) == -1 &&
          k.fault.code == EM_EFFECT_KINDS_FAULT_NULL_WORKER && k.fault.address == 0x00122BB8u);

    puts("effect_kinds_test: PASS");
    return 0;
}
