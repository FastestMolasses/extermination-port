/* Native unit test for em_director_original (ASan/UBSan) over the user's
 * AREA11 overlay and boot ELF: the three beats in route order, the state
 * switch, the fail-stop paths and the translated helpers' edges. Behaviour
 * values are the ones tools/test_director_original_reference.py proves
 * against the original instructions; the player positions are the ones the
 * route captures record at each beat start (FIRST_LEVEL_ROUTE.md). */
#include "game/em_director_original.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SELF 0x007A93F0u

typedef struct {
    unsigned starts, polls, frees;
    uint32_t block, entry, polled, freed;
    int32_t poll_result;
    int fail_start, fail_poll, fail_free;
} Fixture;

static int start(void *c, uint32_t block, uint32_t entry)
{
    Fixture *f = c;
    ++f->starts;
    f->block = block;
    f->entry = entry;
    return f->fail_start ? -1 : 0;
}
static int poll(void *c, uint32_t self, int32_t *result)
{
    Fixture *f = c;
    ++f->polls;
    f->polled = self;
    *result = f->poll_result;
    return f->fail_poll ? -1 : 0;
}
static int release(void *c, uint32_t self)
{
    Fixture *f = c;
    ++f->frees;
    f->freed = self;
    return f->fail_free ? -1 : 0;
}

static uint8_t *slurp(const char *path, size_t *size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "director_original_test: cannot open %s\n", path);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n);
    assert(data && fread(data, 1, (size_t)n, fp) == (size_t)n);
    fclose(fp);
    *size = (size_t)n;
    return data;
}

typedef struct {
    uint8_t b00, b04, b05, d813, d793, cc3[2], b0, b1;
    float player[3];
    float quads[3][4][4];
    EmDirectorAtanTables tables;
    Fixture fx;
    EmDirectorOriginalNode node;
    EmDirectorOriginalWorld world;
    EmDirectorOriginalWorkers workers;
} Rig;

static void rig_init(Rig *r, const float quads[3][4][4], const EmDirectorAtanTables *tables)
{
    memset(r, 0, sizeof *r);
    memcpy(r->quads, quads, sizeof r->quads);
    r->tables = *tables;
    r->node = (EmDirectorOriginalNode){&r->b00, &r->b04, &r->b05, SELF};
    r->world.d810813 = &r->d813;
    r->world.d810793 = &r->d793;
    r->world.d810350 = r->player;
    r->world.d810CC3 = r->cc3;
    r->world.d8106B0 = &r->b0;
    r->world.d8106B1 = &r->b1;
    for (int q = 0; q < 3; ++q)
        r->world.quad[q] = (const float (*)[4])r->quads[q];
    r->world.d26C5D8 = &r->tables;
    r->workers = (EmDirectorOriginalWorkers){&r->fx, start, poll, release};
}

static int tick(Rig *r, uint32_t *fault)
{
    return em_director_original_tick(&r->node, &r->world, &r->workers, fault);
}

static void place(Rig *r, float x, float y, float z)
{
    r->player[0] = x;
    r->player[1] = y;
    r->player[2] = z;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s AREA11.BIN SCUS_971.12\n", argv[0]);
        return 2;
    }
    size_t osize, esize;
    uint8_t *overlay = slurp(argv[1], &osize);
    uint8_t *elf = slurp(argv[2], &esize);
    float quads[3][4][4];
    EmDirectorAtanTables tables;
    assert(em_director_original_load_quads(overlay, osize, quads) == 0);
    assert(em_director_original_load_quads(overlay, osize - 1, quads) < 0);
    assert(em_director_original_load_atan_tables(elf, esize, &tables) == 0);
    assert(em_director_original_load_atan_tables(elf, esize - 1, &tables) < 0);
    overlay[0] ^= 0xFF;
    assert(em_director_original_load_quads(overlay, osize, quads) < 0);
    overlay[0] ^= 0xFF;
    assert(em_director_original_load_quads(overlay, osize, quads) == 0);
    int checks = 6;

    Rig r;
    uint32_t fault = 0;

    /* State 0: flag 0x3B clear -> +0 = 1, +4 = 1; set -> +4 = 3, then free. */
    rig_init(&r, quads, &tables);
    assert(tick(&r, &fault) == 1 && r.b00 == 1 && r.b04 == 1);
    rig_init(&r, quads, &tables);
    r.d793 = 0xFF;
    assert(tick(&r, &fault) == 1 && r.b00 == 0 && r.b04 == 3);
    assert(tick(&r, &fault) == 0 && r.fx.frees == 1 && r.fx.freed == SELF);
    checks += 3;

    /* The route: beats 0, 1, 2 at the captured start positions. */
    rig_init(&r, quads, &tables);
    r.b00 = r.b04 = 1;
    place(&r, 359.93845f, 249.39864f, 256.81161f);         /* row 1088: below the roof */
    assert(tick(&r, &fault) == 1 && r.fx.starts == 0 && r.b05 == 0);
    place(&r, 359.92892f, 264.91196f, 249.24718f);         /* row 1089 */
    assert(tick(&r, &fault) == 1 && r.fx.starts == 1 && r.fx.entry == EM_DIRECTOR_ORIGINAL_SCRIPT0 &&
           r.fx.block == SELF + 0x1F0 && r.b05 == 1);
    r.fx.poll_result = 0;
    assert(tick(&r, &fault) == 1 && r.fx.polls == 1 && r.fx.polled == SELF && r.d813 == 0);
    r.d813 = 1;                                             /* Roger's write, same frame */
    assert(tick(&r, &fault) == 1 && r.fx.polls == 2 && r.d813 == 1 && r.b05 == 1);
    r.fx.poll_result = 1;
    assert(tick(&r, &fault) == 1 && r.d813 == 0x10 && r.cc3[1] == 1 && r.cc3[0] == 0 && r.b05 == 0);
    assert(r.b0 == 0 && r.b1 == 0);
    r.d813 = 0x11;                                          /* Roger 0x823A04 */
    place(&r, 470.00168f, 272.90021f, 290.0715f);           /* row 704: below 275 */
    assert(tick(&r, &fault) == 1 && r.fx.starts == 1);
    place(&r, 471.32135f, 279.90018f, 283.19199f);          /* row 705 */
    assert(tick(&r, &fault) == 1 && r.fx.starts == 2 && r.fx.entry == EM_DIRECTOR_ORIGINAL_SCRIPT1);
    r.fx.poll_result = 3;                                   /* the skip path also completes */
    assert(tick(&r, &fault) == 1 && r.d813 == 0x20 && r.b05 == 0 && r.cc3[1] == 1);
    place(&r, 444.25f, 272.75f, 179.77623f);                /* row 529 */
    assert(tick(&r, &fault) == 1 && r.fx.starts == 2);
    place(&r, 437.64642f, 289.75f, 179.77623f);             /* row 530 */
    assert(tick(&r, &fault) == 1 && r.fx.starts == 3 && r.fx.entry == EM_DIRECTOR_ORIGINAL_SCRIPT2);
    r.fx.poll_result = 1;
    assert(tick(&r, &fault) == 1 && r.d813 == 0xFF && r.b05 == 0);
    unsigned polls = r.fx.polls;
    assert(tick(&r, &fault) == 1 && r.fx.polls == polls && r.fx.starts == 3);   /* 0xFF: dormant */
    checks += 15;

    /* Gate edges: beat 0 keeps 260 and 280, rejects the float above 280;
     * NaN fails beat 0 and passes beat 1. */
    const float edges[][2] = {{260.0f, 1}, {280.0f, 1}, {280.00003f, 0}, {259.99998f, 0}};
    for (size_t i = 0; i < sizeof edges / sizeof edges[0]; ++i) {
        rig_init(&r, quads, &tables);
        r.b00 = r.b04 = 1;
        place(&r, 360.0f, edges[i][0], 240.0f);
        assert(tick(&r, &fault) == 1 && r.fx.starts == (unsigned)edges[i][1]);
        ++checks;
    }
    rig_init(&r, quads, &tables);
    r.b00 = r.b04 = 1;
    place(&r, 360.0f, NAN, 240.0f);
    assert(tick(&r, &fault) == 1 && r.fx.starts == 0);
    r.d813 = 0x10;
    place(&r, 476.0f, NAN, 284.0f);
    assert(tick(&r, &fault) == 1 && r.fx.starts == 1);
    /* Quad 0x82AC20 is not its bounding box: (455, 291) is inside the box only. */
    rig_init(&r, quads, &tables);
    r.b00 = r.b04 = 1;
    r.d813 = 0x10;
    place(&r, 455.0f, 280.0f, 291.0f);
    assert(tick(&r, &fault) == 1 && r.fx.starts == 0);
    checks += 3;

    /* Other states and steps do nothing; 2 and 3 free the node. */
    for (unsigned s = 4; s < 256; ++s) {
        rig_init(&r, quads, &tables);
        r.b04 = (uint8_t)s;
        assert(tick(&r, &fault) == 1 && r.fx.starts + r.fx.polls + r.fx.frees == 0 && r.b04 == s);
    }
    rig_init(&r, quads, &tables);
    r.b04 = 2;
    assert(tick(&r, &fault) == 0 && r.fx.frees == 1);
    const uint8_t idle_steps[] = {2, 0x0F, 0x12, 0x1F, 0x21, 0xFE, 0xFF};
    for (size_t i = 0; i < sizeof idle_steps; ++i) {
        rig_init(&r, quads, &tables);
        r.b00 = r.b04 = 1;
        r.d813 = idle_steps[i];
        place(&r, 360.0f, 270.0f, 240.0f);
        assert(tick(&r, &fault) == 1 && r.fx.starts == 0 && r.b05 == 0);
    }
    checks += 3;

    /* Fail-stop: missing or failing workers and storage. */
    struct { int setup; uint32_t address; } faults[] = {
        {0, 0x001AFC10u}, {1, 0x001AFC10u}, {2, 0x001BA1C0u}, {3, 0x00825468u}, {4, 0x00825538u},
        {5, 0x0082ABE0u}, {6, 0x001B1EA0u}, {7, 0x001BA1A0u}, {8, 0x001BA1A0u}, {9, 0x001BA1F0u},
        {10, 0x001BA1F0u}, {11, 0x001C4760u},
    };
    for (size_t i = 0; i < sizeof faults / sizeof faults[0]; ++i) {
        rig_init(&r, quads, &tables);
        r.b00 = r.b04 = 1;
        place(&r, 360.0f, 270.0f, 240.0f);
        switch (faults[i].setup) {
        case 0: r.b04 = 3; r.workers.w_001AFC10 = NULL; break;
        case 1: r.b04 = 2; r.fx.fail_free = 1; break;
        case 2: r.b04 = 0; r.world.d810793 = NULL; break;
        case 3: r.world.d810813 = NULL; break;
        case 4: r.world.d810350 = NULL; break;
        case 5: r.world.quad[0] = NULL; break;
        case 6: r.world.d26C5D8 = NULL; break;
        case 7: r.workers.w_001BA1A0 = NULL; break;
        case 8: r.fx.fail_start = 1; break;
        case 9: r.b05 = 1; r.workers.w_001BA1F0 = NULL; break;
        case 10: r.b05 = 1; r.fx.fail_poll = 1; break;
        case 11: r.b05 = 1; r.fx.poll_result = 1; r.world.d810CC3 = NULL; break;
        }
        fault = 0;
        assert(tick(&r, &fault) < 0 && fault == faults[i].address);
        ++checks;
    }
    assert(em_director_original_tick(NULL, &r.world, &r.workers, &fault) < 0 &&
           fault == EM_DIRECTOR_ORIGINAL_OWNER);
    ++checks;

    /* 001B1EA0 edges, 0011E620's zero vector, 001C4760's a0 >= 0x20 branch. */
    int32_t v = -1;
    const float in[3] = {360.0f, 0.0f, 240.0f}, nan_point[3] = {NAN, 0.0f, 240.0f};
    const float (*q0)[4] = (const float (*)[4])quads[0];
    assert(em_director_original_001B1EA0(0, in, q0, 4, &tables, &v) == 0 && v == 1);
    assert(em_director_original_001B1EA0(0, in, q0, 2, &tables, &v) == 0 && v == 0);
    assert(em_director_original_001B1EA0(3, in, q0, 4, &tables, &v) == 0 && v == 0);
    assert(em_director_original_001B1EA0(1, in, q0, 4, &tables, &v) < 0);
    assert(em_director_original_001B1EA0(2, in, q0, 4, &tables, &v) < 0);
    assert(em_director_original_001B1EA0(0, nan_point, q0, 4, &tables, &v) < 0);
    float a = 1.0f;
    assert(em_director_original_0011E620(&tables, 0.0f, -0.0f, &a) == 0 && a == 0.0f && !signbit(a));
    assert(em_director_original_0011C4C8(&tables, 0.0f, -0.0f, &a) == 0 && a > 3.14f);
    assert(em_director_original_0011C4C8(&tables, INFINITY, 1.0f, &a) < 0);
    Rig big;
    rig_init(&big, quads, &tables);
    uint8_t items[0x40] = {0};
    big.world.d810CC3 = items;
    assert(em_director_original_001C4760(&big.world, 0x21, 2) == 0 && items[0x21] == 2 && big.b0 == 3 &&
           big.b1 == 0x21);
    assert(em_director_original_001C4760(&big.world, 1, 1) == 0 && items[1] == 1 && big.b1 == 0x21);
    checks += 11;

    free(overlay);
    free(elf);
    printf("director_original_test: PASS (%d checks)\n", checks);
    return 0;
}
