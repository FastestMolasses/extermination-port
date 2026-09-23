/* Native contract of em_load_veil_particles (docs/LOAD_VEIL_PARTICLES.md).
 * Every written byte and worker call is checked against the original
 * instructions by tools/test_load_veil_particles_reference.py; this file
 * pins the fail-stop contract: what refuses before any write, what latches,
 * and the packet run one 0021B1B0 emits. No original data is used: the
 * world below is synthetic. */
#include "game/em_load_veil_particles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

#define PACKET_AT 0x00300000u
#define PACKET_SIZE 0x20000u

static uint8_t packet[PACKET_SIZE], before[PACKET_SIZE];
static uint8_t table[EM_LOAD_VEIL_PARTICLES_TABLE_BYTES];
static uint32_t cursors[4], ctx_9C, d674 = 0x00814220u, d68C = 0x00258000u;
static uint8_t e880[16], d241010[8] = {1, 0, 2, 0, 1, 0, 0x1B, 0};
static uint32_t phase, seed, base_y = 0x8000u;
static float level0 = 0.5f;
static int calls, fail_call;

static int w_fabs(void *c, uint32_t x, uint32_t *r)
{
    (void)c;
    *r = x & 0x7FFFFFFFu;
    return ++calls == fail_call ? -1 : 0;
}

static int w_to_int(void *c, uint32_t x, int32_t *r)
{
    (void)c;
    *r = (int32_t)(x >> 20);   /* any value: the layout, not the arithmetic, is pinned here */
    return ++calls == fail_call ? -1 : 0;
}

static void reset(EmLoadVeilParticles *s, EmLoadVeilParticlesBlock *v)
{
    memset(s, 0, sizeof *s);
    for (size_t i = 0; i < sizeof packet; ++i)
        packet[i] = (uint8_t)(i * 7u + 3u);
    memcpy(before, packet, sizeof packet);
    memset(table, 0xA5, sizeof table);
    cursors[0] = PACKET_AT + 0x100u;
    cursors[1] = PACKET_AT;
    cursors[2] = cursors[3] = 0;
    phase = 0x3F000000u;
    seed = 0x12345678u;
    calls = fail_call = 0;
    s->world.cursor = cursors;
    s->world.cursor_count = 4;
    s->world.ctx_9C = &ctx_9C;
    s->world.d00275674 = &d674;
    s->world.d0027568C = &d68C;
    s->world.d0026E880 = e880;
    s->world.d00241010 = d241010;
    s->world.packet = packet;
    s->world.packet_address = PACKET_AT;
    s->world.packet_size = PACKET_SIZE;
    s->world.table = table;
    s->workers.w_0011DF78 = w_fabs;
    s->workers.w_001281C0 = w_to_int;
    v->phase = &phase;
    v->level0 = &level0;
    v->seed = &seed;
    v->base_y = &base_y;
}

static int untouched(void) { return memcmp(packet, before, sizeof packet) == 0; }

static void refused(EmLoadVeilParticles *s, EmLoadVeilParticlesBlock *v, uint32_t address, int32_t code)
{
    CHECK(em_load_veil_particles_0021B1B0(s, v) == -1);
    CHECK(s->fault.address == address);
    CHECK(s->fault.code == code);
    CHECK(untouched());
    CHECK(cursors[0] == PACKET_AT + 0x100u);
    CHECK(seed == 0x12345678u);
    CHECK(calls == 0);
}

int main(void)
{
    EmLoadVeilParticles s;
    EmLoadVeilParticlesBlock v;

    /* A whole 0021B1B0: 0x162B0 bytes at channel 0, the seed restarted from
     * 0x07234567 and stepped (x5 + 1) on every fifth of the 512 segments,
     * 512 x 2 float_to_int + 512 fabsf worker calls, then per 001DFA40
     * 16 + 256 fabsf and 15 x 16 x 2 float_to_int; channel 1 untouched. */
    reset(&s, &v);
    CHECK(em_load_veil_particles_0021B1B0(&s, &v) == 0);
    CHECK(s.fault.code == EM_LVP_FAULT_NONE);
    CHECK(cursors[0] == PACKET_AT + 0x100u + EM_LOAD_VEIL_PARTICLES_0021B1B0_BYTES);
    CHECK(cursors[1] == PACKET_AT);
    {
        uint32_t want = 0x07234567u;
        for (int i = 4; i < 512; i += 5)
            want = want * 5u + 1u;
        CHECK(seed == want);
    }
    CHECK(calls == 512 * 3 + 2 * (16 + 256) + 2 * 15 * 16 * 2);
    CHECK(memcmp(packet, before, 0x100) == 0);   /* nothing before the cursor */
    CHECK(memcmp(packet + 0x100 + EM_LOAD_VEIL_PARTICLES_0021B1B0_BYTES,
                 before + 0x100 + EM_LOAD_VEIL_PARTICLES_0021B1B0_BYTES,
                 PACKET_SIZE - 0x100 - EM_LOAD_VEIL_PARTICLES_0021B1B0_BYTES) == 0);
    /* The first packet is the 001D1F80(0, 0, 7) REF tag: qwc 9, id 0x30. */
    CHECK(packet[0x100] == 9 && packet[0x101] == 0 && packet[0x103] == 0x30);

    /* Refusals before any write. */
    reset(&s, &v);
    v.seed = NULL;
    refused(&s, &v, 0x0021B1B0u, EM_LVP_FAULT_NULL_WORKER);
    reset(&s, &v);
    s.workers.w_001281C0 = NULL;
    refused(&s, &v, 0x001281C0u, EM_LVP_FAULT_NULL_WORKER);
    reset(&s, &v);
    s.workers.w_0011DF78 = NULL;
    refused(&s, &v, 0x0011DF78u, EM_LVP_FAULT_NULL_WORKER);
    reset(&s, &v);
    s.world.table = NULL;
    refused(&s, &v, 0x001DFA40u, EM_LVP_FAULT_NULL_WORKER);
    reset(&s, &v);
    s.world.d0027568C = NULL;
    refused(&s, &v, 0x001DFA40u, EM_LVP_FAULT_NULL_WORKER);
    reset(&s, &v);
    s.world.cursor = NULL;
    refused(&s, &v, 0x00275670u, EM_LVP_FAULT_NULL_WORKER);
    reset(&s, &v);
    s.world.packet_size = 0x100u + EM_LOAD_VEIL_PARTICLES_0021B1B0_BYTES - 0x10u;   /* one qword short */
    refused(&s, &v, 0x0021B1B0u, EM_LVP_FAULT_BAD_INDEX);
    reset(&s, &v);
    cursors[0] = PACKET_AT + 0x108u;                     /* not qword aligned */
    CHECK(em_load_veil_particles_0021B1B0(&s, &v) == -1);
    CHECK(s.fault.code == EM_LVP_FAULT_BAD_INDEX && untouched() && cursors[0] == PACKET_AT + 0x108u);
    reset(&s, &v);
    cursors[0] = PACKET_AT - 0x10u;                      /* below the window */
    CHECK(em_load_veil_particles_0021B1B0(&s, &v) == -1);
    CHECK(s.fault.code == EM_LVP_FAULT_BAD_INDEX && untouched());
    reset(&s, &v);
    s.world.cursor_count = 0;                            /* channel 0 outside the view */
    refused(&s, &v, 0x0021B1B0u, EM_LVP_FAULT_BAD_INDEX);

    /* A builder with a channel outside the view refuses; one that does not
     * fit refuses; the latch then refuses every later call. */
    reset(&s, &v);
    CHECK(em_load_veil_particles_001D1F80(&s, 4, 0, 7) == -1);
    CHECK(s.fault.address == 0x001D1F80u && s.fault.code == EM_LVP_FAULT_BAD_INDEX && untouched());
    CHECK(em_load_veil_particles_001D1F80(&s, 0, 0, 7) == -1);
    CHECK(untouched() && cursors[0] == PACKET_AT + 0x100u);
    CHECK(s.fault.address == 0x001D1F80u);               /* the first fault stays latched */
    reset(&s, &v);
    cursors[1] = PACKET_AT + PACKET_SIZE - 0x40u;
    CHECK(em_load_veil_particles_001DFA40(&s, 1, 0, 0x80808080u, 0xBEE66666u, NULL) == -1);
    CHECK(s.fault.address == 0x001DFA40u && s.fault.code == EM_LVP_FAULT_BAD_INDEX && untouched());
    reset(&s, &v);
    CHECK(em_load_veil_particles_001006D8(&s, PACKET_AT + 4u, 0, 64, 64, 0, 2, NULL) == -1);
    CHECK(s.fault.code == EM_LVP_FAULT_BAD_INDEX && untouched());

    /* A worker that fails mid-run stops the run where it failed and latches. */
    reset(&s, &v);
    fail_call = 100;
    CHECK(em_load_veil_particles_0021B1B0(&s, &v) == -1);
    CHECK(s.fault.code == EM_LVP_FAULT_WORKER_FAILED);
    CHECK(s.fault.address == 0x0011DF78u || s.fault.address == 0x001281C0u);
    CHECK(calls == 100);
    {
        uint32_t at = cursors[0];
        CHECK(em_load_veil_particles_0021B1B0(&s, &v) == -1);
        CHECK(calls == 100 && cursors[0] == at);
    }

    /* 0021B500 refuses a NULL block; otherwise it steps the phase. */
    CHECK(em_load_veil_particles_0021B500(NULL) == -1);
    reset(&s, &v);
    v.phase = NULL;
    CHECK(em_load_veil_particles_0021B500(&v) == -1);
    reset(&s, &v);
    phase = 0;
    CHECK(em_load_veil_particles_0021B500(&v) == 0 && phase == 0x3BE56042u);

    if (failures) {
        fprintf(stderr, "load veil particles contract: %d failure(s)\n", failures);
        return 1;
    }
    printf("load veil particles contract: PASS\n");
    return 0;
}
