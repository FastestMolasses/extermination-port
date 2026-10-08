#include "game/em_area01_transition_services.h"
#include "game/em_interaction_alignment.h"
#include "game/em_script_door_fan.h"
#include "game/em_script_host_workers.h"
#include "game/em_ee_float.h"
#include <string.h>

typedef struct {
    const EmArea01RuntimeHost *host;
    uint32_t sp;
    uint32_t fault;
} Transition;

static uint8_t *memory(Transition *t, uint32_t address, uint32_t size, int write)
{
    uint8_t *p = t->fault ? NULL : t->host->bytes(t->host->ctx, address, size, write);
    if (!p && !t->fault) t->fault = address;
    return p;
}
static int sound_half(void *ctx, uint32_t address, uint16_t *out)
{
    const uint8_t *p = memory(ctx, address, 2, 0);
    if (!p) return -1;
    memcpy(out, p, 2);
    return 0;
}
static int invoke(Transition *t, uint32_t function, const int32_t *args, unsigned count)
{
    EmArea01Call c = {.function = function, .sp = t->sp, .na = count};
    for (unsigned i = 0; i < count; ++i) c.a[i] = (uint64_t)(int64_t)args[i];
    if (t->fault || !t->host->worker || t->host->worker(t->host->ctx, &c) < 0) {
        if (!t->fault) t->fault = function;
        return -1;
    }
    return 0;
}
static int fade(void *ctx, int32_t duration, int32_t colour)
{
    const int32_t args[] = {duration, colour};
    return invoke(ctx, 0x001AEDE0u, args, 2);
}
static int stream_fade(void *ctx, int32_t lane, int32_t duration, int32_t release)
{
    const int32_t args[] = {lane, duration, release};
    return invoke(ctx, 0x001FAD70u, args, 3);
}

int em_area01_transition_handles(uint32_t function)
{
    return function == 0x001BBD60u || function == 0x001B0C00u || function == 0x001B6F00u;
}
int em_area01_transition_call(const EmArea01RuntimeHost *host, EmArea01Call *c, uint32_t *fault)
{
    if (!host || !host->bytes || !c || !em_area01_transition_handles(c->function)) return -1;
    Transition t = {host, c->sp, 0};
    int rc = -1;
    if (c->function == 0x001BBD60u && c->na == 2 && !c->nf) {
        uint16_t link, side;
        const uint8_t *p = memory(&t, (uint32_t)c->a[0] + 0x56u, 2, 0);
        if (!p) goto done;
        memcpy(&link, p, 2);
        p = memory(&t, (uint32_t)c->a[0] + 0x2Eu, 2, 0);
        if (!p) goto done;
        memcpy(&side, p, 2);
        EmSdfWorkers workers = {.ctx = &t, .r_0024DB80 = sound_half};
        EmSdfFault error = {0};
        uint32_t sound = 0;
        if (em_sdf_001BBD60((int16_t)link, side, &sound, &workers, &error) < 0) goto done;
        uint8_t *out = memory(&t, (uint32_t)c->a[1] + 0x18u, 4, 1);
        if (!out) goto done;
        memcpy(out, &sound, 4);
        rc = 0;
    } else if (c->function == 0x001B0C00u && c->na == 1 && !c->nf) {
        t.sp -= 0x20u;
        EmScriptHostWorkers owner = {.callees = {.ctx = &t, .w_001AEDE0 = fade,
                                                .w_001FAD70 = stream_fade}};
        rc = em_script_host_001B0C00(&owner, (int32_t)c->a[0]);
        if (rc < 0 && !t.fault) t.fault = owner.fault_address;
    } else if (c->function == 0x001B6F00u && c->na == 2 && c->nf == 1) {
        float matrix[16], point[4], yaw, ground, target[4], facing;
        const uint8_t *p = memory(&t, (uint32_t)c->a[0] + 0xC4u, 4, 0);
        if (!p) goto done;
        memcpy(&yaw, p, 4);
        p = memory(&t, (uint32_t)c->a[0] + 0xD0u, 64, 0);
        if (!p) goto done;
        memcpy(matrix, p, 64);
        p = memory(&t, (uint32_t)c->a[1], 16, 0);
        if (!p) goto done;
        memcpy(point, p, 16);
        p = memory(&t, 0x00810354u, 4, 0);
        if (!p) goto done;
        memcpy(&ground, p, 4);
        if (!em_interaction_alignment(matrix, yaw, point, em_ee_float(c->f[0]), ground, target, &facing)) goto done;
        uint8_t *out = memory(&t, 0x00810374u, 4, 1);
        if (!out) goto done;
        memcpy(out, &facing, 4);
        out = memory(&t, 0x700038A0u, 16, 1);
        if (!out) goto done;
        memcpy(out, target, 16);
        t.sp -= 0x30u;
        const int32_t args[] = {0x008102B0, 0x700038A0};
        rc = invoke(&t, 0x00182F90u, args, 2);
    }
done:
    if (rc < 0 && fault) *fault = t.fault ? t.fault : c->function;
    return rc;
}
