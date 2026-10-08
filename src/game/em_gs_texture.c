/* em_gs_texture.c - see em_gs_texture.h. */
#include "game/em_gs_texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LOCALMEM = 4u << 20, BLOCKS = LOCALMEM / 256, MAX_WINDOWS = 32, MAX_STEPS = 32, MAX_RELOCS = 16 };

/* The GS local memory layout of PSMCT32 / PSMT8 / PSMT4 (page, block and
 * column order; GS hardware facts, as tools/extract_textures.py lists them). */
static const uint16_t PAGE32[32] = {
    0, 1, 4, 5, 16, 17, 20, 21, 2, 3, 6, 7, 18, 19, 22, 23,
    8, 9, 12, 13, 24, 25, 28, 29, 10, 11, 14, 15, 26, 27, 30, 31,
};
static const uint16_t COL32[16] = {
    0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15,
};
static const uint16_t COL8[128] = {
    0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54,
    8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62,
    33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23,
    41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31,
    32, 36, 48, 52, 0, 4, 16, 20, 34, 38, 50, 54, 2, 6, 18, 22,
    40, 44, 56, 60, 8, 12, 24, 28, 42, 46, 58, 62, 10, 14, 26, 30,
    1, 5, 17, 21, 33, 37, 49, 53, 3, 7, 19, 23, 35, 39, 51, 55,
    9, 13, 25, 29, 41, 45, 57, 61, 11, 15, 27, 31, 43, 47, 59, 63,
};
static const uint16_t BLK4[32] = {
    0, 2, 8, 10, 1, 3, 9, 11, 4, 6, 12, 14, 5, 7, 13, 15,
    16, 18, 24, 26, 17, 19, 25, 27, 20, 22, 28, 30, 21, 23, 29, 31,
};
static const uint16_t COL4[512] = {
    0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106,
    4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110,
    16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122,
    20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126,
    65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43,
    69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47,
    81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59,
    85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63,
    192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170,
    196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174,
    208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186,
    212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190,
    129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235,
    133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239,
    145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251,
    149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255,
    256, 264, 288, 296, 320, 328, 352, 360, 258, 266, 290, 298, 322, 330, 354, 362,
    260, 268, 292, 300, 324, 332, 356, 364, 262, 270, 294, 302, 326, 334, 358, 366,
    272, 280, 304, 312, 336, 344, 368, 376, 274, 282, 306, 314, 338, 346, 370, 378,
    276, 284, 308, 316, 340, 348, 372, 380, 278, 286, 310, 318, 342, 350, 374, 382,
    321, 329, 353, 361, 257, 265, 289, 297, 323, 331, 355, 363, 259, 267, 291, 299,
    325, 333, 357, 365, 261, 269, 293, 301, 327, 335, 359, 367, 263, 271, 295, 303,
    337, 345, 369, 377, 273, 281, 305, 313, 339, 347, 371, 379, 275, 283, 307, 315,
    341, 349, 373, 381, 277, 285, 309, 317, 343, 351, 375, 383, 279, 287, 311, 319,
    448, 456, 480, 488, 384, 392, 416, 424, 450, 458, 482, 490, 386, 394, 418, 426,
    452, 460, 484, 492, 388, 396, 420, 428, 454, 462, 486, 494, 390, 398, 422, 430,
    464, 472, 496, 504, 400, 408, 432, 440, 466, 474, 498, 506, 402, 410, 434, 442,
    468, 476, 500, 508, 404, 412, 436, 444, 470, 478, 502, 510, 406, 414, 438, 446,
    385, 393, 417, 425, 449, 457, 481, 489, 387, 395, 419, 427, 451, 459, 483, 491,
    389, 397, 421, 429, 453, 461, 485, 493, 391, 399, 423, 431, 455, 463, 487, 495,
    401, 409, 433, 441, 465, 473, 497, 505, 403, 411, 435, 443, 467, 475, 499, 507,
    405, 413, 437, 445, 469, 477, 501, 509, 407, 415, 439, 447, 471, 479, 503, 511,
};

typedef struct {
    uint32_t id, size;
    const uint8_t *data;
} Step;

/* A resource-slot word a module load relocates (EMSP version 2): 001FF830
 * state 7's D_0028A490[slot] = destination + entry offset. */
typedef struct {
    uint32_t step, address, value;
} Reloc;

struct EmGsTexture {
    uint8_t *file;
    const uint8_t *world, *world_covered;
    uint8_t lm[LOCALMEM];
    uint8_t covered[BLOCKS / 8];
    EmGsDataWindow windows[MAX_WINDOWS];
    unsigned window_count, step_count;
    Step steps[MAX_STEPS];
    Reloc relocs[MAX_RELOCS];
    uint32_t reloc_count;
    uint32_t words[MAX_RELOCS]; /* each reloc word's value (0 until its step runs) */
    uint32_t generation;
};

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* One step's runs {first block, count, bytes}: bounds-checked. */
static int step_valid(const uint8_t *p, uint32_t size)
{
    if (size < 4)
        return 0;
    uint32_t runs = rd32(p), at = 4;
    for (uint32_t i = 0; i < runs; ++i) {
        if (size - at < 8)
            return 0;
        uint32_t first = rd32(p + at), count = rd32(p + at + 4);
        at += 8;
        if (first >= BLOCKS || count > BLOCKS - first || (uint64_t)count * 256 > size - at)
            return 0;
        at += count * 256;
    }
    return at == size;
}

EmGsTexture *em_gs_texture_load(const char *path)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f)
        return NULL;
    uint8_t header[12];
    EmGsTexture *gs = NULL;
    if (fread(header, 1, 12, f) != 12 || memcmp(header, "EMSP", 4) ||
        (rd32(header + 4) != 1 && rd32(header + 4) != 2))
        goto fail;
    const uint32_t version = rd32(header + 4);
    const uint32_t size = rd32(header + 8);
    gs = calloc(1, sizeof *gs);
    if (!gs || size > (64u << 20) || !(gs->file = malloc(size)) ||
        fread(gs->file, 1, size, f) != size || fgetc(f) != EOF)
        goto fail;
    const uint8_t *p = gs->file, *end = gs->file + size;
    if (end - p < 4)
        goto fail;
    gs->window_count = rd32(p);
    p += 4;
    if (gs->window_count > MAX_WINDOWS)
        goto fail;
    for (unsigned i = 0; i < gs->window_count; ++i) {
        if (end - p < 8)
            goto fail;
        EmGsDataWindow *w = &gs->windows[i];
        w->address = rd32(p);
        w->size = rd32(p + 4);
        p += 8;
        if ((size_t)(end - p) < w->size)
            goto fail;
        w->bytes = p;
        p += w->size;
    }
    if ((size_t)(end - p) < LOCALMEM + BLOCKS / 8 + 4)
        goto fail;
    gs->world = p;
    gs->world_covered = p + LOCALMEM;
    p += LOCALMEM + BLOCKS / 8;
    gs->step_count = rd32(p);
    p += 4;
    if (gs->step_count > MAX_STEPS)
        goto fail;
    for (unsigned i = 0; i < gs->step_count; ++i) {
        if (end - p < 8)
            goto fail;
        Step *s = &gs->steps[i];
        s->id = rd32(p);
        s->size = rd32(p + 4);
        p += 8;
        if ((size_t)(end - p) < s->size || !step_valid(p, s->size))
            goto fail;
        s->data = p;
        p += s->size;
    }
    if (version >= 2) {
        if (end - p < 4)
            goto fail;
        gs->reloc_count = rd32(p);
        p += 4;
        if (gs->reloc_count > MAX_RELOCS || (size_t)(end - p) < (size_t)gs->reloc_count * 12)
            goto fail;
        for (unsigned i = 0; i < gs->reloc_count; ++i, p += 12)
            gs->relocs[i] = (Reloc){rd32(p), rd32(p + 4), rd32(p + 8)};
    }
    if (p != end)
        goto fail;
    fclose(f);
    em_gs_texture_reset(gs);
    return gs;
fail:
    fclose(f);
    em_gs_texture_free(gs);
    return NULL;
}

void em_gs_texture_free(EmGsTexture *gs)
{
    if (!gs)
        return;
    free(gs->file);
    free(gs);
}

void em_gs_texture_reset(EmGsTexture *gs)
{
    if (!gs)
        return;
    memcpy(gs->lm, gs->world, LOCALMEM);
    memcpy(gs->covered, gs->world_covered, sizeof gs->covered);
    memset(gs->words, 0, sizeof gs->words);
    ++gs->generation;
}

int em_gs_texture_apply(EmGsTexture *gs, unsigned step)
{
    if (!gs)
        return 0;
    for (unsigned i = 0; i < gs->step_count; ++i) {
        if (gs->steps[i].id != step)
            continue;
        const uint8_t *p = gs->steps[i].data;
        const uint32_t runs = rd32(p);
        p += 4;
        for (uint32_t r = 0; r < runs; ++r) {
            const uint32_t first = rd32(p), count = rd32(p + 4);
            p += 8;
            memcpy(gs->lm + (size_t)first * 256, p, (size_t)count * 256);
            for (uint32_t b = first; b < first + count; ++b)
                gs->covered[b >> 3] |= (uint8_t)(1u << (b & 7));
            p += (size_t)count * 256;
        }
        for (unsigned k = 0; k < gs->reloc_count; ++k)
            if (gs->relocs[k].step == step)
                gs->words[k] = gs->relocs[k].value;
        ++gs->generation;
        return 1;
    }
    return 0;
}

uint32_t em_gs_texture_generation(const EmGsTexture *gs)
{
    return gs ? gs->generation : 0;
}

int em_gs_texture_word(const EmGsTexture *gs, uint32_t address, uint32_t *value)
{
    if (!gs || !value)
        return 0;
    for (unsigned k = 0; k < gs->reloc_count; ++k)
        if (gs->relocs[k].address == address) {
            *value = gs->words[k];
            return 1;
        }
    return 0;
}

unsigned em_gs_texture_windows(const EmGsTexture *gs, const EmGsDataWindow **windows)
{
    if (!gs || !windows)
        return 0;
    *windows = gs->windows;
    return gs->window_count;
}

typedef struct {
    uint32_t tbp, tbw, psm, w, h, cbp, cpsm, csm, csa;
} Fields;

static void fields(uint64_t tex0, Fields *t)
{
    const uint32_t lo = (uint32_t)tex0, hi = (uint32_t)(tex0 >> 32);
    t->tbp = lo & 0x3FFF;
    t->tbw = (lo >> 14) & 0x3F;
    t->psm = (lo >> 20) & 0x3F;
    t->w = 1u << ((lo >> 26) & 0xF);
    t->h = 1u << (((lo >> 30) & 3) | ((hi & 3) << 2));
    t->cbp = (hi >> 5) & 0x3FFF;
    t->cpsm = (hi >> 19) & 0xF;
    t->csm = (hi >> 23) & 1;
    t->csa = (hi >> 24) & 0x1F;
}

int em_gs_texture_size(uint64_t tex0, uint32_t *w, uint32_t *h)
{
    Fields t;
    fields(tex0, &t);
    if ((t.psm != 0x13 && t.psm != 0x14) || t.cpsm != 0 || t.csm != 0 || t.csa != 0 ||
        t.w > 1024 || t.h > 1024)
        return 0;
    if (w)
        *w = t.w;
    if (h)
        *h = t.h;
    return 1;
}

static int covered(const EmGsTexture *gs, uint32_t byte)
{
    const uint32_t block = (byte & (LOCALMEM - 1)) >> 8;
    return (gs->covered[block >> 3] >> (block & 7)) & 1;
}

/* PSMCT32 word address of (x, y) in a buffer ppr pages wide. */
static uint32_t ct32_word(uint32_t x, uint32_t y, uint32_t ppr)
{
    const uint32_t page = (y / 32) * ppr + x / 64;
    const uint32_t px = (y % 32 % 8) * 8 + (x % 64 % 8);
    const uint32_t block = PAGE32[(y % 32 / 8) * 8 + (x % 64 / 8)];
    return page * 2048 + block * 64 + (px / 16) * 16 + COL32[px % 16];
}

/* PSMT8 byte address of (x, y). */
static uint32_t t8_byte(uint32_t x, uint32_t y, uint32_t ppr)
{
    const uint32_t page = (y / 64) * ppr + x / 128;
    const uint32_t px = (y % 64 % 16) * 16 + (x % 128 % 16);
    const uint32_t block = PAGE32[(y % 64 / 16) * 8 + (x % 128 / 16)];
    return page * 8192 + block * 256 + (px / 64) * 64 + COL8[px % 128];
}

/* PSMT4 nibble address of (x, y). */
static uint32_t t4_nibble(uint32_t x, uint32_t y, uint32_t ppr)
{
    const uint32_t page = (y / 128) * ppr + x / 128;
    const uint32_t block = BLK4[(y % 128 / 16) * 4 + (x % 128 / 32)];
    return page * 16384 + block * 512 + COL4[(y % 16) * 32 + x % 32];
}

static uint8_t alpha(uint8_t a)
{
    return (uint8_t)(a >= 128 ? 255 : a * 2);
}

int em_gs_texture_decode(const EmGsTexture *gs, uint64_t tex0, uint8_t *rgba, size_t size)
{
    Fields t;
    uint32_t w, h;
    if (!gs || !rgba || !em_gs_texture_size(tex0, &w, &h) || size < (size_t)w * h * 4)
        return 0;
    fields(tex0, &t);
    uint8_t clut[256 * 4];
    const uint32_t clut_base = t.cbp * 256;
    if (t.psm == 0x13) {
        /* 256 entries stored as a 16x16 PSMCT32 region, then CSM1's entry
         * order (8..15 and 16..23 swapped in every 32). */
        for (uint32_t y = 0; y < 16; ++y)
            for (uint32_t x = 0; x < 16; ++x) {
                const uint32_t byte = (clut_base + ct32_word(x, y, 1) * 4) & (LOCALMEM - 1);
                if (!covered(gs, byte))
                    return 0;
                uint32_t i = y * 16 + x;
                const uint32_t in32 = i & 31;
                if (in32 >= 8 && in32 < 16)
                    i += 8;
                else if (in32 >= 16 && in32 < 24)
                    i -= 8;
                memcpy(clut + i * 4, gs->lm + byte, 4);
            }
    } else {
        static const uint8_t word[16] = {0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15};
        for (uint32_t i = 0; i < 16; ++i) {
            const uint32_t byte = (clut_base + word[i] * 4u) & (LOCALMEM - 1);
            if (!covered(gs, byte))
                return 0;
            memcpy(clut + i * 4, gs->lm + byte, 4);
        }
    }
    const uint32_t ppr = t.tbw * 64 / 128 ? t.tbw * 64 / 128 : 1;
    for (uint32_t y = 0; y < h; ++y) {
        const uint32_t sy = h - 1 - y; /* the stored v-flip undone */
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t index;
            if (t.psm == 0x13) {
                const uint32_t byte = (t.tbp * 256 + t8_byte(x, sy, ppr)) & (LOCALMEM - 1);
                if (!covered(gs, byte))
                    return 0;
                index = gs->lm[byte];
            } else {
                const uint32_t nibble = t.tbp * 512 + t4_nibble(x, sy, ppr);
                const uint32_t byte = (nibble >> 1) & (LOCALMEM - 1);
                if (!covered(gs, byte))
                    return 0;
                index = nibble & 1 ? gs->lm[byte] >> 4 : gs->lm[byte] & 0xF;
            }
            uint8_t *out = rgba + ((size_t)y * w + x) * 4;
            memcpy(out, clut + index * 4, 3);
            out[3] = alpha(clut[index * 4 + 3]);
        }
    }
    return 1;
}
