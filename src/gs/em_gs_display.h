/* em_gs_display.h - how the Original profile places a 512x224 GS field on
 * the 4:3 game rectangle (docs/GS_EXACT.md section 11; the user's decisions
 * of 2026-10-09, docs/LAUNCHER_OPTIONS.md). Pure C, no GPU API: every
 * backend's presenter uses it, and tests/gs_display_test.c checks it.
 *
 * Inputs, all the original's own state:
 *   - the GS privileged display registers as the main loop's step U
 *     (00100550) stores them: PMODE, SMODE2, DISPLAY2 and BGCOLOR (and the
 *     other registers 00100550 can store, kept but not presented);
 *   - the field's XYOFFSET_1, the draw offset of the context that drew it
 *     (step V's draw environment: OFY 1936.0 or 1936.5).
 *
 * The picture is 512 framebuffer pixels by 448 TV lines (DISPLAY2 DW 2559
 * at MAGH 4: five clocks per pixel; DH 447). Each field row covers two of
 * the 448 lines (line doubling). A field drawn with a half-line OFY (the
 * fractional part 0.5) is placed one line lower: its rows sample the scene
 * half a field row lower, so the shift cancels it (the field's interlaced
 * height). The picture starts (DX - 636) / (MAGH + 1) pixels right of and
 * DY - 50 lines below the measured default position (DX 636, DY 50 at all
 * 19 PCSX2 points, decomp CAPTURES_C7.md 5b). Everything outside the
 * placed field shows BGCOLOR; the game rectangle crops the far edge.
 * Nearest neighbour only: no smoothing, no CRT simulation.
 *
 * The direction of the offset follows the registers' meaning (DX / DY say
 * where the picture starts); it is inferred from the code and register
 * semantics, not observed on a screen. */
#ifndef EM_GS_DISPLAY_H
#define EM_GS_DISPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_GS_DISPLAY_W 512u            /* framebuffer pixels per line */
#define EM_GS_DISPLAY_LINES 448u        /* TV lines of the picture (DH + 1) */
#define EM_GS_DISPLAY_DX0 636           /* measured default DISPLAY2 DX */
#define EM_GS_DISPLAY_DY0 50            /* measured default DISPLAY2 DY */

/* The register file step U writes (the privileged registers 00100550 can
 * store; addresses em_sdk_display_original.h EM_SDK_GS_*). */
enum {
    EM_GS_DISP_PMODE, EM_GS_DISP_SMODE2, EM_GS_DISP_DISPFB1, EM_GS_DISP_DISPLAY1,
    EM_GS_DISP_DISPFB2, EM_GS_DISP_DISPLAY2, EM_GS_DISP_EXTDATA, EM_GS_DISP_BGCOLOR,
    EM_GS_DISP_COUNT
};
typedef struct {
    uint64_t reg[EM_GS_DISP_COUNT];
    uint32_t written;                   /* bit i: reg[i] was stored */
} EmGsDisplayRegs;

/* A 64-bit store to a privileged register: 0, or -1 for an address that is
 * not one of the eight (nothing stored). */
int em_gs_display_store(EmGsDisplayRegs *r, uint32_t address, uint64_t value);

/* The placement the registers give. */
typedef struct {
    float shift_x;      /* framebuffer pixels: (DX - 636) / (MAGH + 1) */
    float shift_y;      /* lines of the 448: DY - 50 */
    uint8_t bg[3];      /* BGCOLOR R, G, B */
} EmGsDisplayPlace;

/* Decodes the registers for the configuration the presenter was measured
 * with: PMODE with circuit 2 only (EN1 0, EN2 1), SMODE2 interlaced field
 * mode (INT 1, FFMD 1), DISPLAY2 MAGH 4, MAGV 0, DW 2559, DH 447. 0, or -1
 * with *why (a register never stored, or another configuration). */
int em_gs_display_place(const EmGsDisplayRegs *r, EmGsDisplayPlace *out, const char **why);

/* The field's line: 0 when its OFY (XYOFFSET_1 bits 32..47, 1/16 pixel) is
 * whole, 1 when it is a half; -1 for any other fraction (fail-stop). */
int em_gs_field_line(uint64_t xyoffset);

/* The source of a point of the picture: (px, py) in framebuffer pixels and
 * TV lines from the game rectangle's top-left corner (0..512, 0..448).
 * Returns 1 with the field texel (*col 0..511, *row 0..223), or 0 where
 * the point shows BGCOLOR. The Metal shader f_gsfield computes the same
 * expression in the same float operations. */
int em_gs_display_source(const EmGsDisplayPlace *p, int line, float px, float py, uint32_t *col,
                         uint32_t *row);

/* The viewport of the frame's 2D overlay pass (letterbox bands, text,
 * fades), in drawable pixels: out = x, y, width, height, from the game
 * rectangle rect = x, y, width, height. It is the rectangle moved by the
 * picture's shift and by `line`, the line of the field the pass is drawn
 * over (em_gs_field_line of that field's XYOFFSET_1; 0 for a frame without
 * a field), so the pass's 448 canvas lines fall on the picture's lines as
 * that field's rows are shown: canvas lines 2r and 2r + 1 are field row r
 * on both parities. The original draws its bands and text into the field
 * itself, in whole field rows through the same XYOFFSET_1, so on screen
 * they move with the field; a band of whole rows here covers exactly those
 * field rows. */
void em_gs_display_viewport(const EmGsDisplayPlace *p, int line, const double rect[4], double out[4]);

#ifdef __cplusplus
}
#endif

#endif
