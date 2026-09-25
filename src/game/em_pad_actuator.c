/* em_pad_actuator.c - the pad actuator block D_00810E40 on the live path.
 * See em_pad_actuator.h and docs/TRUCK_ORIGINAL.md "Binding". */
#include "game/em_pad_actuator.h"

#include <stdio.h>
#include <string.h>

#include "em_gamepad.h"
#include "game/em_owner_services_original.h"
#include "game/em_player_ladder_entry.h"
#include "game/em_scene_bindings.h"
#include "game/em_script_host_workers.h"

enum { RECORDS = 16, RECORD_SIZE = 4 };
#define TABLES_BASE 0x0024D6F0u

static struct {
    uint8_t block[EM_PAD_ACTUATOR_BLOCK_SIZE];  /* D_00810E40 */
    uint8_t option;                             /* D_00810119 */
    uint8_t records[RECORDS * RECORD_SIZE];     /* D_0024D6F0 */
    int tables_tried, tables_loaded, initialised;
    EmOwnerServices services;
    EmScriptHostWorkers stop;                   /* 001B6250's world */
} P;

static int report(const char *what)
{
    fprintf(stderr, "em_pad_actuator: %s\n", what);
    return -1;
}

static int load_tables(void)
{
    if (P.tables_loaded) return 0;
    if (P.tables_tried) return -1;
    P.tables_tried = 1;
    FILE *f = fopen(EM_PAD_ACTUATOR_TABLES_PATH, "rb");
    uint8_t buf[16 + sizeof P.records];
    size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
    if (f) fclose(f);
    uint32_t head[4];
    if (n == sizeof buf) memcpy(head, buf, sizeof head);
    if (n != sizeof buf || memcmp(buf, "EMRG", 4) != 0 || head[1] != 1 || head[2] != TABLES_BASE ||
        head[3] != sizeof P.records)
        return report("no valid " EM_PAD_ACTUATOR_TABLES_PATH " (export it with tools/export_pad_tables.py)");
    memcpy(P.records, buf + 16, sizeof P.records);
    P.tables_loaded = 1;
    return 0;
}

/* 00111018(port, slot, act): the libpad actuator write, the platform
 * boundary. act[0] is the big motor's on byte, act[1] the small motor's
 * level; the pad keeps them until the next write. */
static int actuator_00111018(void *ctx, int32_t port, int32_t slot, const uint8_t *act)
{
    (void)ctx;
    if (port != 0 || slot != 0) return report("00111018 on a port or slot the native pad does not have");
    em_gamepad_rumble(act[0] ? 1.0f : 0.0f, (float)act[1] / 255.0f, -1);
    return 0;
}

static int actuator_rumble(void *ctx, int port, int slot, const uint8_t act[6])
{
    return actuator_00111018(ctx, port, slot, act) < 0 ? -1 : 0;
}

/* 001B61C0(big, small, duration, force): em_player_rumble_001B61C0 over
 * the block's typed view (+0x04, +0x08, +0x12, +0x16, +0x18..+0x1D, +0x28),
 * loaded before the call and stored after it. */
static int w_001B61C0(void *ctx, uint8_t big, uint8_t small, int64_t duration, int32_t force)
{
    (void)ctx;
    EmPlayerRumblePad pad;
    uint8_t *b = P.block;
    memcpy(&pad.port, b + 0x04, 4);
    memcpy(&pad.slot, b + 0x08, 4);
    pad.ready = b[0x12];
    pad.active = b[0x16];
    memcpy(pad.act, b + 0x18, sizeof pad.act);
    memcpy(&pad.duration, b + 0x28, 2);
    const EmSceneState *scene = em_scene_state();
    const EmPlayerRumble r = {&pad, &P.option, &scene->d275BE0, NULL, actuator_rumble};
    int rc = em_player_rumble_001B61C0(&r, big, small, (int)duration, force);
    b[0x16] = pad.active;
    memcpy(b + 0x18, pad.act, sizeof pad.act);
    memcpy(b + 0x28, &pad.duration, 2);
    return rc < 0 ? -1 : 0;
}

int em_pad_actuator_001B61C0(uint8_t big, uint8_t small, int duration, int force)
{
    if (!P.initialised) em_pad_actuator_reset();
    if (w_001B61C0(NULL, big, small, duration, force) < 0)
        return report("001B61C0 faulted");
    return 0;
}

static int w_001B6250(void *ctx)
{
    (void)ctx;
    return em_script_host_owner_001B6250(&P.stop);
}

static void bind(void)
{
    memset(&P.services, 0, sizeof P.services);
    P.services.world.d0024D6F0 = P.records;
    P.services.world.d0024D6F0_count = RECORDS;
    P.services.world.d00810E56 = &P.block[0x16];
    P.services.world.d00810E68 = (int16_t *)(void *)&P.block[0x28];
    P.services.workers.w_001B61C0 = w_001B61C0;
    P.services.workers.w_001B6250 = w_001B6250;
    memset(&P.stop, 0, sizeof P.stop);
    P.stop.world.pad_address = EM_PAD_ACTUATOR_BLOCK;
    P.stop.world.d810E40 = P.block;
    P.stop.callees.w_00111018 = actuator_00111018;
}

void em_pad_actuator_reset(void)
{
    memset(P.block, 0, sizeof P.block);
    P.block[0x0C] = 6;   /* libpad state: stable */
    P.block[0x10] = 4;   /* 001B5F40 phase 4 (the read) */
    P.block[0x11] = 1;   /* actuators aligned */
    P.block[0x12] = 1;   /* ready (001B5F40 phase 2) */
    P.block[0x14] = 7;   /* mode id: DualShock */
    P.option = 1;        /* 001AB430 */
    bind();
    P.initialised = 1;
}

uint8_t *em_pad_actuator_block(void)
{
    if (!P.initialised) em_pad_actuator_reset();
    return P.block;
}

static int services_fault(const char *where)
{
    if (!P.services.fault.code) return 0;
    fprintf(stderr, "em_pad_actuator: %s: fault %08X code %d\n", where,
            (unsigned)P.services.fault.address, (int)P.services.fault.code);
    return -1;
}

int em_pad_actuator_001B1E20(int32_t effect, int64_t duration)
{
    if (!P.initialised) em_pad_actuator_reset();
    if (load_tables() < 0) return -1;
    if (em_owner_services_001B1E20(&P.services, effect, duration) < 0 || services_fault("001B1E20") < 0)
        return -1;
    return 0;
}

int em_pad_actuator_step_i(void *context)
{
    (void)context;
    if (!P.initialised) em_pad_actuator_reset();
    if (em_owner_services_001B5B70(&P.services) < 0 || services_fault("001B5B70") < 0) return -1;
    return 0;
}
