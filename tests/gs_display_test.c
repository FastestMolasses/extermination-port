/* gs_display_test - the Original profile's picture placement
 * (src/gs/em_gs_display.h; docs/GS_EXACT.md section 11).
 *
 * Designed values only (nothing disc-derived). The display registers come
 * from the game side's own translations, as steps R and U produce them:
 * em_sdk_001002E0 with the SDK mode halfwords of the boot (interlaced,
 * NTSC, field mode; the ELF's GS revision 3), the screen offset x, y as
 * 001AB4E0 passes it (y doubled), then em_sdk_00100550's stores. Checked:
 *   - the registers decode to the shift (x pixels, 2y lines) and BGCOLOR,
 *     at the offsets 0 and +-20 on each axis (the options' clamp);
 *   - the field's line from XYOFFSET_1 (OFY whole: 0, half: 1, else a
 *     fault);
 *   - the mapping: both field parities (the half-line field one line
 *     lower, its first line BGCOLOR), the shifted picture (the uncovered
 *     edge BGCOLOR, the far edge cropped), on both axes;
 *   - the same sampled at a 1920 x 1440 game rectangle as f_gsfield
 *     samples it (pixel centres): every field row and column appears, in
 *     order, and nothing else;
 *   - the refusals: an unknown register address, registers never stored,
 *     another circuit, a non-interlaced mode, another picture size. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_sdk_display_original.h"
#include "gs/em_gs_display.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "gs_display_test: FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

/* D_00241010 as the boot leaves it (interlaced 1, NTSC 2, field mode 1),
 * with the ELF's GS revision halfword 3. */
static const uint8_t k_mode[8] = {1, 0, 2, 0, 1, 0, 3, 0};

static int store(void *ctx, uint32_t address, uint64_t value)
{
    return em_gs_display_store((EmGsDisplayRegs *)ctx, address, value);
}

/* Steps R and U for the screen offset (x, y): the registers. */
static EmGsDisplayRegs registers(int x, int y)
{
    uint8_t env[EM_SDK_DISPENV_SIZE];
    EmGsDisplayRegs r;
    memset(&r, 0, sizeof r);
    CHECK(em_sdk_001002E0(k_mode, env, 0, 0x200, 0xE0, x, (int16_t)(y * 2)) == 0);
    CHECK(em_sdk_00100550(k_mode, env, store, &r) == 0);
    return r;
}

static EmGsDisplayPlace place(int x, int y)
{
    EmGsDisplayRegs r = registers(x, y);
    EmGsDisplayPlace p;
    const char *why = NULL;
    CHECK(em_gs_display_place(&r, &p, &why) == 0 && why == NULL);
    return p;
}

/* The source of the picture point at the centre of line `line` and pixel
 * `px` (0..511, 0..447), or -1 (BGCOLOR): returns row * 512 + col. */
static int at(const EmGsDisplayPlace *p, int field_line, int px, int line)
{
    uint32_t col = 99999, row = 99999;
    if (!em_gs_display_source(p, field_line, (float)px + 0.5f, (float)line + 0.5f, &col, &row)) return -1;
    CHECK(col < 512u && row < 224u);
    return (int)(row * 512u + col);
}
#define RC(row, col) ((row) * 512 + (col))

int main(void)
{
    /* The registers at the measured default and at the clamps. */
    {
        EmGsDisplayRegs r = registers(0, 0);
        CHECK(r.reg[EM_GS_DISP_PMODE] == 0x66u && r.reg[EM_GS_DISP_SMODE2] == 3u);
        CHECK(r.reg[EM_GS_DISP_DISPLAY2] == UINT64_C(0x001BF9FF0203227C));   /* DX 636, DY 50 */
        CHECK(r.reg[EM_GS_DISP_BGCOLOR] == 0u);
        CHECK((r.written & (1u << EM_GS_DISP_DISPLAY1)) == 0u);          /* circuit 2's registers */
        const int offsets[5][2] = {{0, 0}, {20, 0}, {-20, 0}, {0, 20}, {0, -20}};
        for (int i = 0; i < 5; ++i) {
            EmGsDisplayPlace p = place(offsets[i][0], offsets[i][1]);
            CHECK(p.shift_x == (float)offsets[i][0] && p.shift_y == (float)(2 * offsets[i][1]));
            CHECK(p.bg[0] == 0 && p.bg[1] == 0 && p.bg[2] == 0);
        }
    }

    /* The field's line. */
    CHECK(em_gs_field_line((uint64_t)(1936u * 16u) << 32 | 1728u * 16u) == 0);
    CHECK(em_gs_field_line((uint64_t)(1936u * 16u + 8u) << 32 | 1728u * 16u) == 1);
    CHECK(em_gs_field_line((uint64_t)(1936u * 16u + 4u) << 32) == -1);
    CHECK(em_gs_field_line((uint64_t)(1936u * 16u + 8u)) == 0);       /* OFX's fraction is not the line */

    /* Both parities at the default position. */
    {
        const EmGsDisplayPlace p = place(0, 0);
        for (int line = 0; line < 448; ++line)
            CHECK(at(&p, 0, 7, line) == RC(line / 2, 7));
        CHECK(at(&p, 1, 7, 0) == -1);                                   /* the uncovered line */
        for (int line = 1; line < 448; ++line)
            CHECK(at(&p, 1, 7, line) == RC((line - 1) / 2, 7));
        CHECK(at(&p, 1, 0, 447) == RC(223, 0));                         /* row 223's second line cropped */
        for (int x = 0; x < 512; ++x) CHECK(at(&p, 0, x, 100) == RC(50, x) && at(&p, 1, x, 101) == RC(50, x));
        CHECK(!em_gs_display_source(&p, 0, -0.25f, 10.0f, NULL, NULL));
        CHECK(!em_gs_display_source(&p, 0, 10.0f, 448.0f, NULL, NULL));
        CHECK(!em_gs_display_source(&p, 0, 512.0f, 10.0f, NULL, NULL));
    }

    /* The shifted picture: x +20 / -20 pixels, y +20 / -20 rows (40 lines). */
    {
        const EmGsDisplayPlace r = place(20, 0), l = place(-20, 0), d = place(0, 20), u = place(0, -20);
        for (int x = 0; x < 20; ++x) CHECK(at(&r, 0, x, 9) == -1);       /* uncovered: BGCOLOR */
        CHECK(at(&r, 0, 20, 9) == RC(4, 0) && at(&r, 0, 511, 9) == RC(4, 491));   /* 492..511 cropped */
        CHECK(at(&l, 0, 0, 9) == RC(4, 20) && at(&l, 0, 491, 9) == RC(4, 511));
        for (int x = 492; x < 512; ++x) CHECK(at(&l, 0, x, 9) == -1);
        for (int line = 0; line < 40; ++line) CHECK(at(&d, 0, 3, line) == -1 && at(&d, 1, 3, line) == -1);
        CHECK(at(&d, 0, 3, 40) == RC(0, 3) && at(&d, 1, 3, 40) == -1 && at(&d, 1, 3, 41) == RC(0, 3));
        CHECK(at(&d, 0, 3, 447) == RC(203, 3) && at(&d, 1, 3, 447) == RC(203, 3));
        CHECK(at(&u, 0, 3, 0) == RC(20, 3) && at(&u, 1, 3, 0) == RC(19, 3));
        CHECK(at(&u, 0, 3, 407) == RC(223, 3) && at(&u, 0, 3, 408) == -1);
        CHECK(at(&u, 1, 3, 408) == RC(223, 3) && at(&u, 1, 3, 409) == -1);
        const EmGsDisplayPlace c = place(-20, 20);                      /* both axes */
        CHECK(at(&c, 1, 0, 41) == RC(0, 20) && at(&c, 1, 492, 41) == -1 && at(&c, 1, 0, 40) == -1);
    }

    /* As f_gsfield samples a 1920 x 1440 game rectangle (pixel centres;
     * 3.75 pixels per framebuffer pixel, 3.21 rows per line): each parity
     * shows every field row in order, the half-line field starting below
     * the uncovered line's rows; every column in order. */
    {
        const EmGsDisplayPlace p = place(0, 0);
        const float sx = 512.0f / 1920.0f, sy = 448.0f / 1440.0f;
        for (int field_line = 0; field_line < 2; ++field_line) {
            int last = -1, bg = 0;
            for (int j = 0; j < 1440; ++j) {
                uint32_t col, row;
                if (!em_gs_display_source(&p, field_line, (0.5f) * sx, ((float)j + 0.5f) * sy, &col, &row)) {
                    CHECK(last == -1);
                    bg++;
                    continue;
                }
                CHECK((int)row == last || (int)row == last + 1);
                last = (int)row;
            }
            CHECK(last == 223 && bg == (field_line ? 3 : 0));
        }
        int last = -1;
        for (int i = 0; i < 1920; ++i) {
            uint32_t col, row;
            CHECK(em_gs_display_source(&p, 0, ((float)i + 0.5f) * sx, 0.5f * sy, &col, &row));
            CHECK((int)col == last || (int)col == last + 1);
            last = (int)col;
        }
        CHECK(last == 511);
    }

    /* BGCOLOR reaches the uncovered pixels as its R, G, B bytes. */
    {
        EmGsDisplayRegs r = registers(0, 0);
        CHECK(em_gs_display_store(&r, 0x120000E0u, UINT64_C(0x00332211)) == 0);
        EmGsDisplayPlace p;
        CHECK(em_gs_display_place(&r, &p, NULL) == 0 && p.bg[0] == 0x11 && p.bg[1] == 0x22 && p.bg[2] == 0x33);
    }

    /* Refusals. */
    {
        EmGsDisplayRegs r;
        EmGsDisplayPlace p;
        const char *why = NULL;
        memset(&r, 0, sizeof r);
        CHECK(em_gs_display_store(&r, 0x12000010u, 1) == -1 && r.written == 0u);
        CHECK(em_gs_display_place(&r, &p, &why) == -1 && why);
        r = registers(0, 0);
        r.reg[EM_GS_DISP_PMODE] = 0x65;                                  /* circuit 1 */
        CHECK(em_gs_display_place(&r, &p, &why) == -1 && why);
        r = registers(0, 0);
        r.reg[EM_GS_DISP_SMODE2] = 2;                                    /* not interlaced */
        CHECK(em_gs_display_place(&r, &p, &why) == -1 && why);
        r = registers(0, 0);
        r.reg[EM_GS_DISP_DISPLAY2] &= ~(UINT64_C(0x7FF) << 44);          /* DH 0 */
        CHECK(em_gs_display_place(&r, &p, &why) == -1 && why);
    }

    printf("gs_display_test: PASS\n");
    return 0;
}
