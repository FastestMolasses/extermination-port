/* chain_page_test.c - ASan/UBSan fixture for em_chain_page (docs/CHAIN_PAGE.md).
 *
 * The consumer walks memory the page's producers wrote; a malformed page must
 * fault with nothing read outside the mapped memory and nothing written
 * outside the caller's primitive array. This fixture builds a well-formed
 * page (a CNT block with the decal's state A+D writes and a textured fan
 * behind a NEXT), checks the one triangle it draws, then corrupts random
 * words of it (tags, VIF codes, GIF tags, registers) 20000 times and walks
 * each: every run must return 0 or a latched fault, stay inside the mapped
 * 64 KiB, and never draw more primitives than the capacity it was given.
 * It also checks the argument refusals and the skip list, and list mode
 * (em_chain_page_run_list): a frame list with the environment registers,
 * two GIF packets in one DIRECT, an A+D sprite and the END tag, which page
 * mode refuses, then 5000 corrupted lists. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_chain_page.h"

#define MEM_SIZE 0x10000u
#define START 0x1000u

static uint8_t mem[MEM_SIZE];

static const uint8_t *reader(void *ctx, uint32_t a, uint32_t n)
{
    (void)ctx;
    if (a >= MEM_SIZE || n > MEM_SIZE - a) return NULL;
    return mem + a;
}

static void w32(uint32_t a, uint32_t v)
{
    memcpy(mem + a, &v, 4);
}

static uint32_t build(void)
{
    memset(mem, 0, sizeof mem);
    /* start tag: NEXT to the block at START + 0x100 */
    w32(START, 0x20000000u);
    w32(START + 4, START + 0x100u);
    uint32_t words[256], n = 0;
    const uint64_t ad[7][2] = { {0x17E, 0x00}, {0x60, 0x14}, {0x53001, 0x47}, {0x44, 0x42}, {0, 0x08},
                                {1, 0x46}, {0x2004290511322469u, 0x06} };
    words[n++] = 0; words[n++] = 0; words[n++] = 0; words[n++] = 0x50000008u;   /* DIRECT 8 */
    words[n++] = 7u | 0x8000u; words[n++] = 0x10000000u; words[n++] = 0xE; words[n++] = 0;
    for (unsigned i = 0; i < 7; ++i) {
        words[n++] = (uint32_t)ad[i][0]; words[n++] = (uint32_t)(ad[i][0] >> 32);
        words[n++] = (uint32_t)ad[i][1]; words[n++] = 0;
    }
    const uint64_t gif = 3u | (1u << 15) | (UINT64_C(1) << 46) | (UINT64_C(0x7D) << 47) | (UINT64_C(3) << 60);
    words[n++] = 0; words[n++] = 0; words[n++] = 0; words[n++] = 0x5000000Au;   /* DIRECT 10 */
    words[n++] = (uint32_t)gif; words[n++] = (uint32_t)(gif >> 32); words[n++] = 0x412; words[n++] = 0;
    for (unsigned k = 0; k < 3; ++k) {
        words[n++] = 0x3F000000u; words[n++] = 0x3E800000u; words[n++] = 0x3F800000u; words[n++] = 0;
        words[n++] = 0x80; words[n++] = 0x80; words[n++] = 0x80; words[n++] = 0x80;
        words[n++] = 0x7000u + 0x100u * k; words[n++] = 0x7900u + 0x80u * k; words[n++] = 0x100000u;
        words[n++] = 0x800u;
    }
    const uint32_t blk = START + 0x100u;
    w32(blk, 0x10000000u | (n / 4u));                                           /* CNT */
    memcpy(mem + blk + 16, words, 4u * n);
    const uint32_t link = blk + 16u + 4u * n;
    w32(link, 0x20000000u);                                                      /* NEXT: the end */
    w32(link + 4, START + 0x20u);
    return link + 16u - START;
}

/* A frame list (list mode): a DIRECT of two GIF packets (the environment,
 * then TEST / ALPHA / COLCLAMP), a CNT with an A+D sprite (PRIM, RGBAQ with
 * its Q, XYZ2 x 2) and the END tag. Returns its size. */
static uint32_t build_list(void)
{
    memset(mem, 0, sizeof mem);
    uint32_t words[256], n = 0;
    const uint64_t env[4][2] = { {0x80038, 0x4C}, {0x790800007000u, 0x18}, {0x00DF000001FF0000u, 0x40},
                                 {1, 0x1A} };
    const uint64_t st[3][2] = { {0x30000, 0x47}, {0x80000000A8u, 0x42}, {1, 0x46} };
    const uint64_t spr[4][2] = { {6, 0x00}, {0x3F80000080402010u, 0x01}, {0x79007000u, 0x05},
                                 {0x0000000587009000u, 0x05} };
    words[n++] = 0; words[n++] = 0; words[n++] = 0x11000000u; words[n++] = 0x50000009u;   /* DIRECT 9 */
    words[n++] = 4u | 0x8000u; words[n++] = 0x10000000u; words[n++] = 0xE; words[n++] = 0;
    for (unsigned i = 0; i < 4; ++i) {
        words[n++] = (uint32_t)env[i][0]; words[n++] = (uint32_t)(env[i][0] >> 32);
        words[n++] = (uint32_t)env[i][1]; words[n++] = 0;
    }
    words[n++] = 3u | 0x8000u; words[n++] = 0x10000000u; words[n++] = 0xE; words[n++] = 0;
    for (unsigned i = 0; i < 3; ++i) {
        words[n++] = (uint32_t)st[i][0]; words[n++] = (uint32_t)(st[i][0] >> 32);
        words[n++] = (uint32_t)st[i][1]; words[n++] = 0;
    }
    words[n++] = 0; words[n++] = 0; words[n++] = 0; words[n++] = 0x50000005u;   /* DIRECT 5 */
    words[n++] = 4u | 0x8000u; words[n++] = 0x10000000u; words[n++] = 0xE; words[n++] = 0;
    for (unsigned i = 0; i < 4; ++i) {
        words[n++] = (uint32_t)spr[i][0]; words[n++] = (uint32_t)(spr[i][0] >> 32);
        words[n++] = (uint32_t)spr[i][1]; words[n++] = 0;
    }
    w32(START, 0x10000000u | (n / 4u));                                         /* CNT */
    memcpy(mem + START + 16, words, 4u * n);
    const uint32_t end = START + 16u + 4u * n;
    w32(end, 0x70000000u);                                                     /* END */
    return end + 16u - START;
}

static int run(EmChainPage *p, EmGfxGsPrim *prims, uint32_t capacity, const uint32_t *skip, uint32_t nskip)
{
    memset(p, 0, sizeof *p);
    p->read = reader;
    p->prims = prims;
    p->prim_capacity = capacity;
    p->skip_calls = skip;
    p->skip_count = nskip;
    return em_chain_page_run(p, START);
}

int main(void)
{
    static EmChainPage page;
    static EmGfxGsPrim prims[8];
    const uint32_t size = build();

    assert(run(&page, prims, 8, NULL, 0) == 0);
    assert(page.prim_count == 1 && prims[0].prim == 0x7D && prims[0].count == 3);
    assert(prims[0].set == 0x3Fu && prims[0].tex0 == 0x2004290511322469u && prims[0].test == 0x53001u);
    assert(page.counts.direct == 2 && page.counts.stale_q == 0);

    /* argument refusals */
    assert(em_chain_page_run(NULL, START) == -1);
    memset(&page, 0, sizeof page);
    assert(em_chain_page_run(&page, START) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_ARGS);
    assert(run(&page, prims, 8, NULL, 1) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_ARGS);
    uint32_t many[EM_CHAIN_PAGE_SKIP_MAX + 1] = {0};
    assert(run(&page, prims, 8, many, EM_CHAIN_PAGE_SKIP_MAX + 1) == -1 &&
           page.fault == EM_CHAIN_PAGE_FAULT_ARGS);
    /* capacity 0: the triangle has no room */
    assert(run(&page, prims, 0, NULL, 0) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_CAPACITY);
    /* a CALL walked over */
    build();
    const uint32_t blk = START + 0x100u;
    w32(START + 4, START + 0x40u);                     /* start -> a CALL block -> the fan */
    w32(START + 0x40u, 0x50000000u);
    w32(START + 0x44u, 0x8000u);
    w32(START + 0x50u, 0x20000000u);
    w32(START + 0x54u, blk);
    const uint32_t skip = 0x8000u;
    assert(run(&page, prims, 8, &skip, 1) == 0 && page.counts.skipped == 1 && page.prim_count == 1);
    assert(run(&page, prims, 8, NULL, 0) == -1);       /* not skipped: memory at 0x8000 is no program */

    /* list mode: the environment per primitive, two GIF packets in one
     * DIRECT, A+D vertices, the END tag; the same bytes fault as a page */
    static EmGfxGsEnv envs[8];
    const uint32_t lsize = build_list();
    memset(&page, 0, sizeof page);
    page.read = reader; page.prims = prims; page.prim_env = envs; page.prim_capacity = 8;
    assert(em_chain_page_run_list(&page, START) == 0);
    assert(page.prim_count == 1 && prims[0].prim == 6 && prims[0].count == 2);
    assert(prims[0].v[0].x == 0x7000 && prims[0].v[1].y == 0x8700 && prims[0].v[1].z == 5);
    assert(prims[0].v[1].rgba[0] == 0x10 && prims[0].v[1].rgba[3] == 0x80 && prims[0].v[1].q == 0x3F800000u);
    assert(prims[0].test == 0x30000u && prims[0].alpha == 0x80000000A8u && prims[0].colclamp == 1);
    assert(envs[0].set == (EM_GFX_GS_ENV_FRAME | EM_GFX_GS_ENV_XYOFFSET | EM_GFX_GS_ENV_SCISSOR |
                           EM_GFX_GS_ENV_PRMODECONT));
    assert(envs[0].frame == 0x80038u && envs[0].xyoffset == 0x790800007000u && page.counts.direct == 2);
    assert(run(&page, prims, 8, NULL, 0) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_DMA);   /* END */
    srand(0x1157);
    uint32_t lfaults = 0, lclean = 0;
    for (unsigned it = 0; it < 5000u; ++it) {
        build_list();
        const unsigned flips = 1u + (unsigned)(rand() % 4);
        for (unsigned f = 0; f < flips; ++f)
            w32(START + 4u * (uint32_t)(rand() % (int)(lsize / 4u)), (uint32_t)rand() ^ ((uint32_t)rand() << 16));
        memset(&page, 0, sizeof page);
        page.read = reader; page.prims = prims; page.prim_env = envs;
        page.prim_capacity = (uint32_t)(rand() % 3);
        if (em_chain_page_run_list(&page, START) == 0) {
            ++lclean;
            assert(page.prim_count <= page.prim_capacity);
        } else {
            ++lfaults;
            assert(page.fault != EM_CHAIN_PAGE_OK);
        }
    }

    /* corruption sweep */
    srand(0xC4A1);
    uint32_t faults = 0, clean = 0;
    for (unsigned it = 0; it < 20000u; ++it) {
        build();
        const unsigned flips = 1u + (unsigned)(rand() % 4);
        for (unsigned f = 0; f < flips; ++f) {
            const uint32_t at = START + 4u * (uint32_t)(rand() % (int)(size / 4u));
            uint32_t v = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
            switch (rand() % 4) {
            case 0: v &= 0xFF000000u; break;          /* a command / ID byte */
            case 1: v &= 0x0000FFFFu; break;          /* a count / address */
            default: break;
            }
            w32(at, v);
        }
        const uint32_t cap = (uint32_t)(rand() % 3);
        if (run(&page, prims, cap, NULL, 0) == 0) {
            ++clean;
            assert(page.prim_count <= cap);
        } else {
            ++faults;
            assert(page.fault != EM_CHAIN_PAGE_OK);
        }
    }
    printf("chain_page_test: PASS (1 clean page, 6 refusals, %u corrupted pages walked: %u faulted, %u clean; "
           "1 clean frame list, %u corrupted lists walked: %u faulted, %u clean)\n",
           faults + clean, faults, clean, lfaults + lclean, lfaults, lclean);
    return 0;
}
