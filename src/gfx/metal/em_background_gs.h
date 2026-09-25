/* em_background_gs.h — the original level BACKGROUND (the "sky" behind the
 * level), shared by the Metal backend (em_gfx_metal.m) and
 * tools/test_background_reference.py. docs/BACKGROUND.md has the evidence.
 *
 * The original does not clear the colour buffer per frame (FIRST_LEVEL_AUDIT
 * R09): the frame-flush packet 001D2300 REFs is a Z-only sprite (TEST ATE 1,
 * ATST NEVER, AFAIL ZB_ONLY). What fills the frame behind the level is a
 * full-screen textured grid drawn FIRST in the world chain:
 *
 *   - 001C1F50 (area load) arms render flags 0x20/0x21 for AREA11 (key
 *     0x0B00, and several other areas), stores the area's TEX0 at render
 *     ctx +0x1D0 (001E2260) and the colour floats at ctx +0x1C0 (001E2270,
 *     D_00250F30).
 *   - 001C1D00 (every world frame) -> 001E0CF0 -> 001E1E60 builds channel
 *     3's list: env class 7 (TEST ZTST ALWAYS, ZBUF ZMSK 1), CLAMP_1, TEX0,
 *     TEXA, RGBAQ = int(128 * ctx+0x1C0..0x1CC), and the VU1 upload: the GIF
 *     tag D_00253560 at dmem 0/0x81/0x102 and the 8 qwords D_00253570 at
 *     dmem 0x200 (the matrix below, then D_002535B0..E0 with the zoom
 *     ctx+0x2468 at D_002535B8), then kernel 0x0023C990.
 *   - 001D2300 CALLs that list right after the Z clear, before the level.
 *
 * The matrix (001E1E60): 00102798 transposes the view copy ctx+0x2380, the
 * row at D_00253580 is doubled, and 001026D0 multiplies by the X/Y swap
 * matrix. The kernel 0x0023C990 then walks a 32x32 screen grid: for grid
 * point v = (x, y, zoom, 0) it forms d = x*row0 + y*row1 + zoom*row2,
 * scales it by ERLENG (1/|d.xyz|), writes ST = st_offset + d.xy*st_scale
 * and XYZ2 = the float bits of (x, y) + xy_bias (the 526336.0 add leaves
 * the GS 12.4 value in the low 16 bits), and kicks one 64-vertex triangle
 * strip per pair of grid rows (31 strips).
 *
 * Arithmetic: binary32 with the VU's truncation after every product and
 * sum (em_fog_gs_vu_trunc), in the instruction order of the originals.
 * ERLENG is modelled as 1/sqrt(x*x + y*y + z*z) evaluated in double and
 * truncated; the hardware EFU result is not verified bit for bit (see
 * docs/BACKGROUND.md).
 */
#ifndef EM_BACKGROUND_GS_H
#define EM_BACKGROUND_GS_H

#include "gfx/metal/em_fog_gs.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Kernel 0x0023C990: vi12/vi13 = 0x20 columns/rows. */
#define EM_BACKGROUND_GS_GRID 32u

/* The asset written by the decomp's export_level.py --background from the
 * user's own ELF, disc and an AREA11 EE capture (ignored assets/, never
 * committed). The texels are the level-load GS upload replayed from the
 * disc (001FFCD0 -> 001FF590(0xAB, 1) -> 00200830; docs/BACKGROUND.md):
 *   0  'EMBG'        4  u32 version (2)
 *   8  u32 tex_w     12 u32 tex_h
 *   16 u64 TEX0      24 u64 CLAMP_1   32 u64 TEX1_1
 *   40 u32 RGBAQ (R | G << 8 | B << 16 | A << 24)   44 u32 PRIM
 *   48 f32 origin[2]   (D_002535B0 x, y)
 *   56 f32 step[2]     (D_002535C0 x, y)
 *   64 f32 st_offset[2](D_002535D0 x, y)
 *   72 f32 xy_bias     (D_002535D8)
 *   76 f32 st_scale[2] (D_002535E0 x, y)
 *   84 u32 texel source (1 = the disc level-upload replay)
 *   88 u64 TEST_1    96 u64 ZBUF_1   (channel 3's state at the MSCAL)
 *   104 RGBA8 texels, tex_w * tex_h * 4 (the CLUT-expanded texture) */
#define EM_BACKGROUND_GS_HEADER 104u
#define EM_BACKGROUND_GS_VERSION 2u
#define EM_BACKGROUND_GS_TEXELS_FROM_DISC 1u

typedef struct {
    uint32_t tex_w, tex_h;
    uint64_t tex0, clamp1, tex1, test1, zbuf1;
    uint32_t rgbaq, prim, texel_source;
    float origin[2], step[2], st_offset[2], xy_bias, st_scale[2];
    const uint8_t *rgba;          /* points into the parsed buffer */
} EmBackgroundGsAsset;

static inline uint32_t em_background_gs_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static inline float em_background_gs_f32(const uint8_t *p)
{
    uint32_t bits = em_background_gs_u32(p);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static inline uint64_t em_background_gs_u64(const uint8_t *p)
{
    return em_background_gs_u32(p) |
           (uint64_t)em_background_gs_u32(p + 4) << 32;
}

/* 0 on success; negative when the buffer is not a version-2 asset. */
static inline int em_background_gs_parse(const uint8_t *buf, size_t len,
                                         EmBackgroundGsAsset *out)
{
    if (!buf || !out || len < EM_BACKGROUND_GS_HEADER) return -1;
    if (memcmp(buf, "EMBG", 4) != 0 ||
        em_background_gs_u32(buf + 4) != EM_BACKGROUND_GS_VERSION)
        return -2;
    memset(out, 0, sizeof *out);
    out->tex_w = em_background_gs_u32(buf + 8);
    out->tex_h = em_background_gs_u32(buf + 12);
    if (!out->tex_w || !out->tex_h || out->tex_w > 1024u || out->tex_h > 1024u)
        return -3;
    if ((size_t)out->tex_w * out->tex_h * 4u + EM_BACKGROUND_GS_HEADER != len)
        return -4;
    out->tex0 = em_background_gs_u64(buf + 16);
    out->clamp1 = em_background_gs_u64(buf + 24);
    out->tex1 = em_background_gs_u64(buf + 32);
    out->rgbaq = em_background_gs_u32(buf + 40);
    out->prim = em_background_gs_u32(buf + 44);
    out->origin[0] = em_background_gs_f32(buf + 48);
    out->origin[1] = em_background_gs_f32(buf + 52);
    out->step[0] = em_background_gs_f32(buf + 56);
    out->step[1] = em_background_gs_f32(buf + 60);
    out->st_offset[0] = em_background_gs_f32(buf + 64);
    out->st_offset[1] = em_background_gs_f32(buf + 68);
    out->xy_bias = em_background_gs_f32(buf + 72);
    out->st_scale[0] = em_background_gs_f32(buf + 76);
    out->st_scale[1] = em_background_gs_f32(buf + 80);
    out->texel_source = em_background_gs_u32(buf + 84);
    out->test1 = em_background_gs_u64(buf + 88);
    out->zbuf1 = em_background_gs_u64(buf + 96);
    out->rgba = buf + EM_BACKGROUND_GS_HEADER;
    return 0;
}

/* NULL when a backend reproduces the GS state exactly; otherwise the name
 * of the first field it does not (the backend then refuses the asset —
 * no stand-in draw). Supported: PRIM tristrip, textured, no fog, no
 * blending, context 1; TEX0 TFX MODULATE with TCC RGB and the asset's
 * dimensions; TEX1 LCM 0 / MXL 0 / MMAG and MMIN LINEAR; CLAMP_1 WMS and
 * WMT CLAMP; TEST_1 ZTE 1 / ZTST ALWAYS with no alpha or destination-alpha
 * test that can drop a pixel; ZBUF_1 ZMSK 1 (the backend draws with depth
 * test and write off and keeps every pixel). These are the values the
 * captured AREA11 lists carry (001D1F80(3,0,7): TEST_1 0x30003, ZMSK 1).
 * The texels must come from the disc upload replay. */
static inline const char *em_background_gs_unsupported(
    const EmBackgroundGsAsset *a)
{
    if ((a->prim & 7u) != 4u) return "PRIM type (not a triangle strip)";
    if (!(a->prim >> 4 & 1u)) return "PRIM TME 0";
    if (a->prim >> 5 & 1u) return "PRIM FGE 1";
    if (a->prim >> 6 & 1u) return "PRIM ABE 1";
    if (a->prim >> 7 & 1u) return "PRIM AA1 1";
    if (a->prim >> 9 & 1u) return "PRIM CTXT 1";
    if (a->tex0 >> 34 & 1u) return "TEX0 TCC 1";
    if (a->tex0 >> 35 & 3u) return "TEX0 TFX not MODULATE";
    if ((1u << (a->tex0 >> 26 & 15u)) != a->tex_w ||
        (1u << (a->tex0 >> 30 & 15u)) != a->tex_h)
        return "TEX0 TW/TH differ from the texels";
    if (a->tex1 & 1u) return "TEX1 LCM 1";
    if (a->tex1 >> 2 & 7u) return "TEX1 MXL";
    if (!(a->tex1 >> 5 & 1u)) return "TEX1 MMAG not LINEAR";
    if ((a->tex1 >> 6 & 7u) != 1u) return "TEX1 MMIN not LINEAR";
    if ((a->clamp1 & 3u) != 1u || (a->clamp1 >> 2 & 3u) != 1u)
        return "CLAMP_1 not CLAMP in S and T";
    if (!(a->test1 >> 16 & 1u) || (a->test1 >> 17 & 3u) != 1u)
        return "TEST_1 not ZTE 1 / ZTST ALWAYS";
    if ((a->test1 & 1u) && (a->test1 >> 1 & 7u) != 1u)
        return "TEST_1 alpha test can drop pixels";
    if (a->test1 >> 14 & 1u) return "TEST_1 DATE 1";
    if (!(a->zbuf1 >> 32 & 1u)) return "ZBUF_1 ZMSK 0";
    if (a->texel_source != EM_BACKGROUND_GS_TEXELS_FROM_DISC)
        return "texel source is not the disc upload replay";
    return NULL;
}

/* The port's native view (em_cs_view_to_native: column-major, rows =
 * original X, -Y, -Z) as the original view copy ctx+0x2380 (memory row j =
 * operator column j). Sign flips only, exact. The mapping itself (native =
 * the original view with rows 1 and 2 negated) is em_cs_view_to_native's,
 * checked by tools/test_census_standins_reference.py; the background test
 * does not re-prove it. */
static inline void em_background_gs_original_view(const float native[16],
                                                  float original[16])
{
    for (unsigned column = 0; column < 4; ++column)
        for (unsigned row = 0; row < 4; ++row)
            original[column * 4 + row] = (row == 1 || row == 2)
                ? -native[column * 4 + row] : native[column * 4 + row];
}

/* 001E1E60's matrix, D_00253570 (VU1 dmem 0x200..0x203) from the view copy
 * ctx+0x2380. 00102798 transposes; the four D_00253580 floats (row 1) are
 * doubled; 001026D0(out, swap, out) computes, for every memory row b of
 * out, ACC = s0*b.x; ACC += s1*b.y; ACC += s2*b.z; row = ACC + s3*b.w,
 * where s0..s3 are the swap matrix rows (0,1,0,0), (1,0,0,0), (0,0,1,0),
 * (0,0,0,1). The literal products keep the signs of zero the VU0 macro
 * code produces. */
static inline void em_background_gs_matrix(const float view[16],
                                           float out[16])
{
    static const float swap[4][4] = {
        { 0.0f, 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f },
    };
    float t[16];
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            t[row * 4 + column] = view[column * 4 + row];
    for (unsigned lane = 0; lane < 4; ++lane)
        t[4 + lane] = t[4 + lane] * 2.0f;
    for (unsigned row = 0; row < 4; ++row) {
        const float *b = t + row * 4;
        for (unsigned lane = 0; lane < 4; ++lane) {
            float acc = em_fog_gs_vu_trunc((double)swap[0][lane] * b[0]);
            acc = em_fog_gs_vu_trunc((double)acc + em_fog_gs_vu_trunc(
                (double)swap[1][lane] * b[1]));
            acc = em_fog_gs_vu_trunc((double)acc + em_fog_gs_vu_trunc(
                (double)swap[2][lane] * b[2]));
            out[row * 4 + lane] = em_fog_gs_vu_trunc((double)acc +
                em_fog_gs_vu_trunc((double)swap[3][lane] * b[3]));
        }
    }
}

/* One grid vertex as the kernel writes it: ST floats and the XYZ2 X/Y
 * 16-bit GS 12.4 fields (Z is 0). */
typedef struct {
    float st[2];
    uint16_t xy[2];
} EmBackgroundGsVertex;

/* Kernel 0x0023C990 over the grid. m = em_background_gs_matrix output
 * (vf28..vf31 = its memory rows), zoom = ctx+0x2468 (vf10.z). out is
 * indexed [row][column]. The GS draws strip r (0..30) as the 64 vertices
 * (r,0), (r+1,0), (r,1), (r+1,1), ..., (r,31), (r+1,31). */
static inline void em_background_gs_grid(const EmBackgroundGsAsset *a,
    const float m[16], float zoom,
    EmBackgroundGsVertex out[EM_BACKGROUND_GS_GRID][EM_BACKGROUND_GS_GRID])
{
    float y = a->origin[1];
    for (unsigned row = 0; row < EM_BACKGROUND_GS_GRID; ++row) {
        float x = a->origin[0];                 /* x reloaded from dmem 516 each row */
        for (unsigned column = 0; column < EM_BACKGROUND_GS_GRID; ++column) {
            float d[3];
            for (unsigned lane = 0; lane < 3; ++lane) {
                float acc = em_fog_gs_vu_trunc((double)m[lane] * x);
                acc = em_fog_gs_vu_trunc((double)acc + em_fog_gs_vu_trunc(
                    (double)m[4 + lane] * y));
                d[lane] = em_fog_gs_vu_trunc((double)acc + em_fog_gs_vu_trunc(
                    (double)m[8 + lane] * zoom));
            }
            const float p = em_fog_gs_vu_trunc(1.0 / sqrt(
                (double)d[0] * d[0] + (double)d[1] * d[1] +
                (double)d[2] * d[2]));
            EmBackgroundGsVertex *v = &out[row][column];
            for (unsigned lane = 0; lane < 2; ++lane) {
                const float u = em_fog_gs_vu_trunc((double)d[lane] * p);
                v->st[lane] = em_fog_gs_vu_trunc((double)a->st_offset[lane] +
                    em_fog_gs_vu_trunc((double)u * a->st_scale[lane]));
            }
            const float position[2] = { x, y };
            for (unsigned lane = 0; lane < 2; ++lane) {
                const float biased = em_fog_gs_vu_trunc(
                    (double)position[lane] + a->xy_bias);
                uint32_t bits;
                memcpy(&bits, &biased, sizeof bits);
                v->xy[lane] = (uint16_t)(bits & 0xffffu);
            }
            x = em_fog_gs_vu_trunc((double)x + a->step[0]);
        }
        y = em_fog_gs_vu_trunc((double)y + a->step[1]);
    }
}

/* GS window position -> native NDC. The frame is the 512 x 224 field with
 * XYOFFSET (1792, 1936): the 001D2300 clear sprite spans exactly X
 * 1792..2304, Y 1936..2160. Convention: the footprint [1792 + i,
 * 1793 + i) of GS field pixel i maps onto the i-th 1/512 of the viewport
 * width (and Y likewise over 224 lines), so GS X 2048 is the NDC origin.
 * That is the convention of the port's world projection
 * (em_mat4_perspective_gs puts the engine's screen centre, GS X 2048 /
 * Y 2048, at the NDC origin with half-extents 256 / 112 field pixels), so
 * the background lines up with the level drawn over it at any output
 * scale. At the GS's own resolution it samples half a pixel right of and
 * below the GS sample point (X = 1792 + i); on this gradient that is a
 * sub-texel ST shift. Mapping the GS sample point onto the Metal pixel
 * centre instead would leave the half pixel left of / above the grid's
 * first column / row uncovered when the output is upscaled (measured on
 * the first-control capture: 192 of 6,912 sky-box samples and 417 frame
 * samples black), because the kernel's grid starts exactly at the field's
 * left and top edges (it ends 1/16 pixel short of the right and bottom
 * ones, at 2303.9375 / 2159.9375). xy is the kernel's 12.4 fixed point. Checked by
 * tools/test_background_reference.py. */
static inline void em_background_gs_ndc(const uint16_t xy[2], float ndc[2])
{
    ndc[0] = ((float)xy[0] / 16.0f - 2048.0f) / 256.0f;
    ndc[1] = -((float)xy[1] / 16.0f - 2048.0f) / 112.0f;
}

#endif /* EM_BACKGROUND_GS_H */
