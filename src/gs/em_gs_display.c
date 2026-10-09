/* em_gs_display.c - see em_gs_display.h. */
#include "gs/em_gs_display.h"

#include <math.h>
#include <stddef.h>

static const uint32_t k_address[EM_GS_DISP_COUNT] = {
    0x12000000u, 0x12000020u, 0x12000070u, 0x12000080u, 0x12000090u, 0x120000A0u, 0x120000C0u, 0x120000E0u,
};

int em_gs_display_store(EmGsDisplayRegs *r, uint32_t address, uint64_t value)
{
    if (!r) return -1;
    for (unsigned i = 0; i < EM_GS_DISP_COUNT; ++i)
        if (k_address[i] == address) {
            r->reg[i] = value;
            r->written |= 1u << i;
            return 0;
        }
    return -1;
}

int em_gs_display_place(const EmGsDisplayRegs *r, EmGsDisplayPlace *out, const char **why)
{
    const char *dummy;
    if (!why) why = &dummy;
    *why = NULL;
    if (!r || !out) return *why = "no registers", -1;
    const uint32_t need = 1u << EM_GS_DISP_PMODE | 1u << EM_GS_DISP_SMODE2 | 1u << EM_GS_DISP_DISPLAY2 |
                          1u << EM_GS_DISP_BGCOLOR;
    if ((r->written & need) != need)
        return *why = "the display registers were never stored (main-loop step U, 00100550)", -1;
    const uint64_t pmode = r->reg[EM_GS_DISP_PMODE], smode2 = r->reg[EM_GS_DISP_SMODE2];
    const uint64_t d = r->reg[EM_GS_DISP_DISPLAY2], bg = r->reg[EM_GS_DISP_BGCOLOR];
    if ((pmode & 3u) != 2u) return *why = "PMODE enables another circuit than circuit 2 alone", -1;
    if ((smode2 & 3u) != 3u) return *why = "SMODE2 is not interlaced field mode", -1;
    const uint32_t dx = (uint32_t)(d & 0xFFFu), dy = (uint32_t)(d >> 12 & 0x7FFu);
    const uint32_t magh = (uint32_t)(d >> 23 & 0xFu), magv = (uint32_t)(d >> 27 & 0x3u);
    const uint32_t dw = (uint32_t)(d >> 32 & 0xFFFu), dh = (uint32_t)(d >> 44 & 0x7FFu);
    if (magh != 4u || magv != 0u || dw != 2559u || dh != 447u)
        return *why = "DISPLAY2 is not the measured 512 x 448 picture (MAGH 4, DW 2559, DH 447)", -1;
    out->shift_x = (float)((int32_t)dx - EM_GS_DISPLAY_DX0) / (float)(magh + 1u);
    out->shift_y = (float)((int32_t)dy - EM_GS_DISPLAY_DY0);
    out->bg[0] = (uint8_t)bg;
    out->bg[1] = (uint8_t)(bg >> 8);
    out->bg[2] = (uint8_t)(bg >> 16);
    return 0;
}

int em_gs_field_line(uint64_t xyoffset)
{
    const uint32_t frac = (uint32_t)(xyoffset >> 32) & 0xFu;
    return frac == 0u ? 0 : frac == 8u ? 1 : -1;
}

int em_gs_display_source(const EmGsDisplayPlace *p, int line, float px, float py, uint32_t *col,
                         uint32_t *row)
{
    const float x = px - p->shift_x;
    const float y = py - (p->shift_y + (float)line);
    if (!(x >= 0.0f) || !(y >= 0.0f) || x >= (float)EM_GS_DISPLAY_W || y >= (float)EM_GS_DISPLAY_LINES)
        return 0;
    if (col) *col = (uint32_t)floorf(x);
    if (row) *row = (uint32_t)floorf(y) >> 1;
    return 1;
}

void em_gs_display_viewport(const EmGsDisplayPlace *p, int line, const double rect[4], double out[4])
{
    out[0] = rect[0] + (double)p->shift_x * rect[2] / (double)EM_GS_DISPLAY_W;
    out[1] = rect[1] + (double)(p->shift_y + (float)line) * rect[3] / (double)EM_GS_DISPLAY_LINES;
    out[2] = rect[2];
    out[3] = rect[3];
}
