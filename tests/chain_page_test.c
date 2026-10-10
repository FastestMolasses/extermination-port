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
 * mode refuses, then 5000 corrupted lists. And pass mode (CHAIN_PAGE.md
 * section 6.2): a page whose one CALL is shaped like 001DDE10's
 * depth-of-field pass (the copy target's environment, REFs of state
 * blocks, two GIF packets in one DIRECT, the REF of the frame's draw
 * environment, the blend), followed with its environment and marks while
 * the draw environments themselves are unmapped (never read); the same
 * page faults in page mode and is walked over as a skip; the pass's
 * refusals; then 5000 corrupted pass pages. */
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

/* Pass mode's page: start -> CALL of the pass at PASS_AT -> the end. The
 * draw environments (bank A, two blocks of 0x190) lie at ENVS, unmapped by
 * pass_reader; the state blocks (bank-E- and bank-D-shaped) at BLOCKS. */
#define PASS_AT 0x2000u
#define BLOCKS 0x3000u
#define ENVS 0x4000u

static int env_mapped = 1;
static const uint8_t *pass_reader(void *ctx, uint32_t a, uint32_t n)
{
    if (!env_mapped && a < ENVS + 2u * EM_CHAIN_PAGE_DRAW_ENV && a + n > ENVS) return NULL;
    return reader(ctx, a, n);
}

static uint32_t put_words(uint32_t at, const uint32_t *words, uint32_t n)
{
    memcpy(mem + at, words, 4u * n);
    return at + 4u * n;
}

static uint32_t put_tag(uint32_t at, uint32_t id, uint32_t qwc, uint32_t addr)
{
    w32(at, (id << 28) | qwc);
    w32(at + 4, addr);
    return at + 16u;
}

/* A CNT of a DIRECT with an A+D packet of n (data, register) pairs. */
static uint32_t put_ad(uint32_t at, const uint64_t (*ad)[2], uint32_t n)
{
    at = put_tag(at, 1, 2u + n, 0);
    const uint32_t head[8] = { 0, 0, 0x11000000u, 0x50000000u | (1u + n), n | 0x8000u, 0x10000000u, 0xE, 0 };
    at = put_words(at, head, 8);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t q[4] = { (uint32_t)ad[i][0], (uint32_t)(ad[i][0] >> 32), (uint32_t)ad[i][1], 0 };
        at = put_words(at, q, 4);
    }
    return at;
}

/* The pass: the copy target's environment, the REFs of two state blocks,
 * one DIRECT with the TEXFLUSH / TEX0 / TEXA packet and the copy sprite
 * (PRE PRIM 0x116: RGBAQ, UV, XYZF2, UV, XYZF2), the REF of the draw
 * environment of `slot`, the blend's TEX0, CLAMP, TEXA / TEST / ALPHA and
 * sprite (PRIM 0x156), and the draw environment again; RET. `again_qwc`
 * is the size of the draw-environment REFs (0x19 is 001D1F20's). */
static void build_pass(uint32_t slot, uint32_t again_qwc, uint32_t again_addr)
{
    memset(mem, 0, sizeof mem);
    put_tag(START, 5, 0, PASS_AT);                         /* CALL the pass */
    put_tag(START + 16u, 2, 0, START + 0x20u);             /* NEXT: the end */
    /* bank-E- and bank-D-shaped blocks: TEST_1 / ZBUF_1, PRIM 0 / CLAMP_1 */
    const uint64_t e[2][2] = { { 0x3000D, 0x47 }, { 0x101000070u, 0x4E } };
    const uint64_t d[2][2] = { { 0, 0x00 }, { (UINT64_C(0x37C) << 32) | 0x7FC00Au, 0x08 } };
    const uint64_t d3[2][2] = { { 0, 0x00 }, { (UINT64_C(0x3FC) << 32) | 0x3FC00Au, 0x08 } };
    put_ad(BLOCKS - 16u, e, 2);
    put_ad(BLOCKS + 0x80u - 16u, d, 2);
    put_ad(BLOCKS + 0x100u - 16u, d3, 2);
    uint32_t at = PASS_AT;
    const uint64_t env[8][2] = { { 0x4012C, 0x4C }, { 0x102000040u, 0x4E }, { 0x780000007800u, 0x18 },
                                 { 0x00FF000000FF0000u, 0x40 }, { 1, 0x1A }, { 0x700000001u, 0x46 },
                                 { 0x300000004u, 0x45 }, { 0x30000, 0x47 } };
    at = put_ad(at, env, 8);
    at = put_tag(at, 3, 4, BLOCKS);                        /* REF: TEST_1, ZBUF_1 */
    at = put_tag(at, 3, 4, BLOCKS + 0x80u);                /* REF: PRIM 0, CLAMP_1 */
    /* one DIRECT 10: the A+D packet and the sprite */
    at = put_tag(at, 1, 11, 0);
    const uint32_t copy[44] = {
        0, 0, 0, 0x5000000Au,
        3u | 0x8000u, 0x10000000u, 0xE, 0,
        0, 0, 0x3F, 0,                                     /* TEXFLUSH */
        0x24020700u, 0xA, 0x06, 0,                         /* TEX0_1: the field */
        0x20, 0, 0x3B, 0,                                  /* TEXA */
        1u | 0x8000u, 0x508B4000u, 0x43431, 0,             /* PRE PRIM 0x116, RGBAQ UV XYZF2 UV XYZF2 */
        0x80, 0x80, 0x80, 0x80,
        8, 8, 0x12345, 0x80,                               /* UV (8, 8); its upper words unused */
        0x7800, 0x7800, 0, 0xFF,
        0x1FF8, 0xDF8, 0x12345, 0x80,
        0x8800, 0x8800, 0, 0xFF };
    at = put_words(at, copy, 44);
    at = put_tag(at, 3, again_qwc, again_addr ? again_addr : ENVS + EM_CHAIN_PAGE_DRAW_ENV * slot);
    const uint64_t tex[2][2] = { { 0, 0x3F }, { 0x220012580u, 0x06 } };
    at = put_ad(at, tex, 2);
    at = put_tag(at, 3, 4, BLOCKS + 0x100u);               /* REF: PRIM 0, CLAMP_1 */
    const uint64_t blend[3][2] = { { 0, 0x3B }, { 0x51001, 0x47 }, { 0x44, 0x42 } };
    at = put_ad(at, blend, 3);
    at = put_tag(at, 1, 7, 0);
    const uint32_t spr[28] = {
        0, 0, 0, 0x50000006u,
        1u | 0x8000u, 0x50AB4000u, 0x43431, 0,             /* PRE PRIM 0x156 */
        0x80, 0x80, 0x80, 0x3E,
        8, 8, 0x3B, 0,
        0x7000, 0x7900, 0x23450, 0x80,
        0x1008, 0x1008, 0x80, 0x80,
        0x9000, 0x8700, 0x23450, 0x80 };
    at = put_words(at, spr, 28);
    at = put_tag(at, 3, again_qwc, again_addr ? again_addr : ENVS + EM_CHAIN_PAGE_DRAW_ENV * slot);
    put_tag(at, 6, 0, 0);                                  /* RET */
}

static int run_pass(EmChainPage *p, EmGfxGsPrim *prims, EmGfxGsEnv *envs, uint32_t capacity, uint32_t slot)
{
    memset(p, 0, sizeof *p);
    p->read = pass_reader;
    p->prims = prims;
    p->prim_env = envs;
    p->prim_capacity = capacity;
    p->pass_call = PASS_AT;
    p->draw_envs = ENVS;
    p->draw_env_slot = slot;
    return em_chain_page_run(p, START);
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

    /* pass mode: the pass's primitives with their environment, the marks
     * where the draw environment (never read: unmapped) is REFed again, and
     * the registers that block writes forgotten after a mark */
    env_mapped = 0;
    for (uint32_t slot = 0; slot < 2u; ++slot) {
        build_pass(slot, EM_CHAIN_PAGE_DRAW_ENV_QWC, 0);
        memset(envs, 0xA5, sizeof envs);
        assert(run_pass(&page, prims, envs, 8, slot) == 0);
        assert(page.prim_count == 2 && page.counts.passes == 1 && page.counts.pass_prims == 2 &&
               page.counts.again == 2 && page.counts.pass_direct == 8 && page.counts.skipped == 0);
        assert(page.pass_first == 0 && page.again_count == 2 && page.again[0] == 1 && page.again[1] == 2);
        /* the copy: PRIM 0x116, the field's TEX0 with the region clamp, bank E's TEST, 001D6E60's COLCLAMP
         * (001006D8 keeps the packet memory's other bits of it: any) */
        assert(prims[0].prim == 0x116 && prims[0].count == 2);
        assert(prims[0].set == (EM_GFX_GS_TEX0 | EM_GFX_GS_CLAMP | EM_GFX_GS_TEST | EM_GFX_GS_COLCLAMP));
        assert(prims[0].tex0 == 0xA24020700u && prims[0].clamp == ((UINT64_C(0x37C) << 32) | 0x7FC00Au) &&
               prims[0].test == 0x3000Du && prims[0].colclamp == 0x700000001u);
        assert(prims[0].v[0].x == 0x7800 && prims[0].v[0].u == 8 && prims[0].v[0].z == 0 && prims[0].v[0].f == 0xF);
        assert(prims[0].v[1].x == 0x8800 && prims[0].v[1].u == 0x1FF8 && prims[0].v[1].v == 0xDF8);
        assert(prims[0].v[1].rgba[3] == 0x80 && prims[0].v[1].q == 0x3F800000u);
        assert(envs[0].set == (EM_GFX_GS_ENV_FRAME | EM_GFX_GS_ENV_ZBUF | EM_GFX_GS_ENV_XYOFFSET |
                               EM_GFX_GS_ENV_SCISSOR | EM_GFX_GS_ENV_PRMODECONT | EM_GFX_GS_ENV_DTHE |
                               EM_GFX_GS_ENV_TEXA));
        assert(envs[0].frame == 0x4012Cu && envs[0].zbuf == 0x101000070u && envs[0].xyoffset == 0x780000007800u &&
               envs[0].scissor == 0x00FF000000FF0000u && envs[0].texa == 0x20 && envs[0].prmodecont == 1);
        /* the blend after the mark: no TEX1 / COLCLAMP (the frame's), its
         * own TEX0 / CLAMP / TEST / ALPHA, the environment only its TEXA */
        assert(prims[1].prim == 0x156 && prims[1].count == 2);
        assert(prims[1].set == (EM_GFX_GS_TEX0 | EM_GFX_GS_CLAMP | EM_GFX_GS_TEST | EM_GFX_GS_ALPHA));
        assert(prims[1].tex0 == 0x220012580u && prims[1].test == 0x51001u && prims[1].alpha == 0x44);
        assert(prims[1].v[1].x == 0x9000 && prims[1].v[1].y == 0x8700 && prims[1].v[1].z == 0x2345u &&
               prims[1].v[1].f == 8 && prims[1].v[1].rgba[3] == 0x3E && prims[1].v[1].u == 0x1008);
        assert(envs[1].set == EM_GFX_GS_ENV_TEXA && envs[1].texa == 0 && envs[1].frame == 0);
    }
    /* the same page: page mode faults on the environment write, a skip
     * walks over it, a list-mode pass and a pass without bank A or with a
     * slot above 1 are argument faults */
    build_pass(0, EM_CHAIN_PAGE_DRAW_ENV_QWC, 0);
    assert(run(&page, prims, 8, NULL, 0) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_GIF);
    const uint32_t pass_skip = PASS_AT;
    assert(run(&page, prims, 8, &pass_skip, 1) == 0 && page.counts.skipped == 1 && page.prim_count == 0 &&
           page.counts.passes == 0);
    memset(&page, 0, sizeof page);
    page.read = pass_reader; page.prims = prims; page.prim_env = envs; page.prim_capacity = 8;
    page.pass_call = PASS_AT; page.draw_envs = ENVS;
    assert(em_chain_page_run_list(&page, START) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_ARGS);
    page.draw_envs = 0;
    assert(em_chain_page_run(&page, START) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_ARGS);
    page.draw_envs = ENVS; page.draw_env_slot = 2;
    assert(em_chain_page_run(&page, START) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_ARGS);
    page.draw_env_slot = 0; page.skip_calls = &pass_skip; page.skip_count = 1;
    assert(em_chain_page_run(&page, START) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_ARGS);
    /* REFs into bank A other than the slot's whole block fault (the other
     * slot's, a part of it); a context-2 write in the pass faults; the
     * page's REF of the block in page mode would read it (here: unmapped) */
    build_pass(0, EM_CHAIN_PAGE_DRAW_ENV_QWC, ENVS + EM_CHAIN_PAGE_DRAW_ENV);
    assert(run_pass(&page, prims, envs, 8, 0) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_DMA);
    build_pass(0, EM_CHAIN_PAGE_DRAW_ENV_QWC - 1u, 0);
    assert(run_pass(&page, prims, envs, 8, 0) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_DMA);
    build_pass(1, EM_CHAIN_PAGE_DRAW_ENV_QWC, 0);
    assert(run_pass(&page, prims, envs, 8, 0) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_DMA);  /* slot 1's block */
    build_pass(0, EM_CHAIN_PAGE_DRAW_ENV_QWC, 0);
    w32(PASS_AT + 0x20u + 8u, 0x4D);                       /* FRAME_1 -> FRAME_2 */
    assert(run_pass(&page, prims, envs, 8, 0) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_GIF);
    build_pass(0, EM_CHAIN_PAGE_DRAW_ENV_QWC, 0);
    assert(run_pass(&page, prims, envs, 1, 0) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_CAPACITY);
    /* more marks than EM_CHAIN_PAGE_AGAIN_MAX: a pass of 17 REFs of the block */
    memset(mem, 0, sizeof mem);
    put_tag(START, 5, 0, PASS_AT);
    put_tag(START + 16u, 2, 0, START + 0x20u);
    uint32_t at = PASS_AT;
    for (uint32_t k = 0; k <= EM_CHAIN_PAGE_AGAIN_MAX; ++k) at = put_tag(at, 3, EM_CHAIN_PAGE_DRAW_ENV_QWC, ENVS);
    put_tag(at, 6, 0, 0);
    assert(run_pass(&page, prims, envs, 8, 0) == -1 && page.fault == EM_CHAIN_PAGE_FAULT_CAPACITY);
    /* corrupted pass pages: never a read of the draw environments or past
     * the mapped memory, never more primitives than the room */
    srand(0xD0F);
    uint32_t pfaults = 0, pclean = 0;
    for (unsigned it = 0; it < 5000u; ++it) {
        build_pass((uint32_t)(rand() & 1), EM_CHAIN_PAGE_DRAW_ENV_QWC, 0);
        const unsigned flips = 1u + (unsigned)(rand() % 4);
        for (unsigned f = 0; f < flips; ++f) {
            const uint32_t span = (rand() & 3) ? 0x200u : 0x40u;
            const uint32_t base = (rand() & 3) ? PASS_AT : (rand() & 1) ? START : BLOCKS - 16u;
            w32(base + 4u * (uint32_t)(rand() % (int)(span / 4u)), (uint32_t)rand() ^ ((uint32_t)rand() << 16));
        }
        const uint32_t cap = (uint32_t)(rand() % 4);
        if (run_pass(&page, prims, envs, cap, (uint32_t)(rand() & 1)) == 0) {
            ++pclean;
            assert(page.prim_count <= cap && page.again_count <= EM_CHAIN_PAGE_AGAIN_MAX);
        } else {
            ++pfaults;
            assert(page.fault != EM_CHAIN_PAGE_OK);
        }
    }
    env_mapped = 1;

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
           "1 clean frame list, %u corrupted lists walked: %u faulted, %u clean; the pass in both slots, "
           "11 pass refusals, %u corrupted pass pages walked: %u faulted, %u clean)\n",
           faults + clean, faults, clean, lfaults + lclean, lfaults, lclean, pfaults + pclean, pfaults, pclean);
    return 0;
}
