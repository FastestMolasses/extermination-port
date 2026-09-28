/* em_gs_raster - a CPU model of the PS2 Graphics Synthesizer's drawing
 * path: register writes and GIF packets in, GS local-memory writes out
 * (docs/GS_EXACT.md).
 *
 * Clean room. Written from public GS documentation (the register map and
 * field layouts, the GIF tag and PACKED formats, the local-memory page /
 * block / column tables, the documented texture-function, fog, alpha-test,
 * blend and dither formulas) and from the measurements of the decomp's GS
 * conformance harness (tools/gs_conformance*.py, docs/GS_CONFORMANCE.md),
 * which recorded what PCSX2's software renderer writes for designed
 * primitives. No emulator source was read. Every rule cites its evidence in
 * docs/GS_EXACT.md; where the evidence is documentation only the rule is
 * marked there as not measured, and strict mode (below) refuses it, or,
 * for a span boundary, faults where it could change a pixel.
 *
 * Input: GS register writes (A+D form: register address + 64-bit value) or
 * whole GIF packets (PACKED / REGLIST / IMAGE), exactly what the GIF hands
 * the GS. Output: the GS local memory (4 MiB, swizzled as the GS stores
 * it): frame buffers, Z buffers, uploaded textures and CLUTs.
 *
 * The model is deterministic and single-threaded; it keeps no global state.
 */
#ifndef EM_GS_RASTER_H
#define EM_GS_RASTER_H

#include <stddef.h>
#include <stdint.h>

#define EM_GS_MEM_BYTES (4u << 20)

/* GS general-purpose register addresses (GS register map). */
enum {
    EM_GS_PRIM = 0x00, EM_GS_RGBAQ = 0x01, EM_GS_ST = 0x02, EM_GS_UV = 0x03,
    EM_GS_XYZF2 = 0x04, EM_GS_XYZ2 = 0x05, EM_GS_TEX0_1 = 0x06, EM_GS_TEX0_2 = 0x07,
    EM_GS_CLAMP_1 = 0x08, EM_GS_CLAMP_2 = 0x09, EM_GS_FOG = 0x0A, EM_GS_XYZF3 = 0x0C,
    EM_GS_XYZ3 = 0x0D, EM_GS_TEX1_1 = 0x14, EM_GS_TEX1_2 = 0x15, EM_GS_TEX2_1 = 0x16,
    EM_GS_TEX2_2 = 0x17, EM_GS_XYOFFSET_1 = 0x18, EM_GS_XYOFFSET_2 = 0x19,
    EM_GS_PRMODECONT = 0x1A, EM_GS_PRMODE = 0x1B, EM_GS_TEXCLUT = 0x1C,
    EM_GS_SCANMSK = 0x22, EM_GS_MIPTBP1_1 = 0x34, EM_GS_MIPTBP1_2 = 0x35,
    EM_GS_MIPTBP2_1 = 0x36, EM_GS_MIPTBP2_2 = 0x37, EM_GS_TEXA = 0x3B,
    EM_GS_FOGCOL = 0x3D, EM_GS_TEXFLUSH = 0x3F, EM_GS_SCISSOR_1 = 0x40,
    EM_GS_SCISSOR_2 = 0x41, EM_GS_ALPHA_1 = 0x42, EM_GS_ALPHA_2 = 0x43,
    EM_GS_DIMX = 0x44, EM_GS_DTHE = 0x45, EM_GS_COLCLAMP = 0x46, EM_GS_TEST_1 = 0x47,
    EM_GS_TEST_2 = 0x48, EM_GS_PABE = 0x49, EM_GS_FBA_1 = 0x4A, EM_GS_FBA_2 = 0x4B,
    EM_GS_FRAME_1 = 0x4C, EM_GS_FRAME_2 = 0x4D, EM_GS_ZBUF_1 = 0x4E, EM_GS_ZBUF_2 = 0x4F,
    EM_GS_BITBLTBUF = 0x50, EM_GS_TRXPOS = 0x51, EM_GS_TRXREG = 0x52, EM_GS_TRXDIR = 0x53,
    EM_GS_HWREG = 0x54, EM_GS_SIGNAL = 0x60, EM_GS_FINISH = 0x61, EM_GS_LABEL = 0x62,
};

/* Pixel storage modes (PSM field values). */
enum {
    EM_GS_PSMCT32 = 0x00, EM_GS_PSMCT24 = 0x01, EM_GS_PSMCT16 = 0x02, EM_GS_PSMCT16S = 0x0A,
    EM_GS_PSMT8 = 0x13, EM_GS_PSMT4 = 0x14, EM_GS_PSMT8H = 0x1B, EM_GS_PSMT4HL = 0x24,
    EM_GS_PSMT4HH = 0x2C, EM_GS_PSMZ32 = 0x30, EM_GS_PSMZ24 = 0x31, EM_GS_PSMZ16 = 0x32,
    EM_GS_PSMZ16S = 0x3A,
};

/* One queued vertex: the register words as the GS latched them. */
typedef struct {
    int32_t x, y;          /* XYZ X, Y (12.4, primitive space, 0..0xFFFF)      */
    uint32_t z;            /* XYZF: 24 bits; XYZ: 32 bits                      */
    uint32_t f;            /* fog weight 0..255 (XYZF or the FOG register)     */
    uint8_t rgba[4];       /* RGBAQ R, G, B, A                                 */
    uint32_t q;            /* RGBAQ Q (binary32)                               */
    uint32_t s, t;         /* ST (binary32)                                    */
    uint32_t u, v;         /* UV (14 bits each, 10.4 texels)                   */
} EmGsVertex;

typedef struct {
    uint64_t xyoffset, scissor, tex0, tex1, tex2, clamp, alpha, test, fba, frame, zbuf;
    uint64_t miptbp1, miptbp2;
} EmGsContext;

/* Why a primitive or a transfer was refused (bit set in EmGs.refusals). */
enum {
    EM_GS_REFUSE_UNMEASURED = 1u << 0, /* strict mode: a feature no capture has settled */
    EM_GS_REFUSE_FORMAT     = 1u << 1, /* a pixel / texture / CLUT format not modelled   */
    EM_GS_REFUSE_GIF        = 1u << 2, /* a malformed GIF packet                         */
    EM_GS_REFUSE_REGISTER   = 1u << 3, /* a register the model does not accept           */
};

struct EmGsPending;                   /* a primitive waiting for its span's end */

typedef struct EmGs {
    uint8_t *mem;                      /* EM_GS_MEM_BYTES of local memory       */
    EmGsContext ctx[2];
    uint64_t prim, prmode, prmodecont, texclut, scanmsk, texa, fogcol, dimx, dthe;
    uint64_t colclamp, pabe, bitbltbuf, trxpos, trxreg, trxdir;
    EmGsVertex cur;                    /* the vertex registers                  */
    EmGsVertex queue[3];               /* the vertex queue                      */
    unsigned queued;                   /* vertices in the queue                 */
    unsigned fan_count;                /* vertices since PRIM (fans, strips)    */
    uint16_t clut[512];                /* the CLUT buffer (1 KiB)               */
    uint64_t cbp0, cbp1;               /* CLD 4 / 5 compare registers           */
    uint64_t clut_src;                 /* CBP, CPSM, CSM, CSA of the last load  */
    uint32_t clut_src_valid;
    uint32_t trx_x, trx_y, trx_active; /* HOST -> LOCAL transfer position       */
    uint32_t trx_nibble;               /* PSMT4: 1 when a half byte is pending  */
    int strict;                        /* refuse features no capture settled    */
    uint32_t refusals;                 /* EM_GS_REFUSE_* seen                   */
    uint32_t refused_prims;            /* primitives not drawn                  */
    uint64_t drawn_prims, drawn_pixels;
    char reason[128];                  /* the first refusal, for the log       */
    /* The span (docs/GS_EXACT.md section 3.7): primitives are queued until a
     * write that ends the span, then drawn in order; whether every vertex of
     * the span has the same Z decides the STQ vertex grid (section 4.5). */
    struct EmGsPending *pend;          /* heap; em_gs_release frees it          */
    uint32_t pend_n, pend_cap;
    uint32_t pend_attr, pend_class;    /* attribute bits / class of the span    */
    uint32_t pend_z, pend_zconst;      /* first vertex Z; 1 while all equal     */
    uint32_t pend_stq;                 /* the span has an STQ-textured primitive */
    /* Spans joined by boundaries no capture has measured (span_end): */
    uint32_t chain_open;               /* the last span ended at such a boundary */
    uint32_t chain_attr, chain_class, chain_z, chain_uniform, chain_stq;
    uint32_t span_faults;              /* chains whose grid decision is unsettled */
    uint64_t pend_lo[3], pend_hi[3];   /* frame, Z, texture bytes the span uses */
} EmGs;

/* mem must hold EM_GS_MEM_BYTES; it is not cleared. The registers start at
 * zero (PRMODECONT 1, as after a GS reset). */
void em_gs_init(EmGs *gs, uint8_t *mem);
/* Draws every queued primitive (the span ends). Call it before reading
 * local memory: em_gs_gif and em_gs_write leave the current span queued. */
void em_gs_flush(EmGs *gs);
/* em_gs_flush, then frees the queue. The EmGs can be used again. */
void em_gs_release(EmGs *gs);
/* One register write in A+D form. A vertex kick queues its primitive; the
 * queue is drawn when the span ends (docs/GS_EXACT.md section 3.7). */
void em_gs_write(EmGs *gs, unsigned reg, uint64_t value);
/* A GIF packet stream (tags + data, 16-byte quadwords). Returns the number
 * of bytes consumed: all of them, or less when a tag's data runs past the
 * end (the rest is refused, EM_GS_REFUSE_GIF). */
size_t em_gs_gif(EmGs *gs, const void *data, size_t bytes);

/* Local-memory addressing (the documented page / block / column tables).
 * Word (32-bit) address of pixel (x, y) of a PSMCT32 / Z32 buffer, etc.
 * bp is in blocks (256 bytes), bw in units of 64 pixels. */
uint32_t em_gs_addr32(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, int z);
uint32_t em_gs_addr16(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, unsigned psm);
uint32_t em_gs_addr8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);
uint32_t em_gs_addr4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);
/* Reads / writes one pixel of any storage mode (value in the mode's bits). */
uint32_t em_gs_read_pixel(const EmGs *gs, uint32_t bp, uint32_t bw, unsigned psm, uint32_t x, uint32_t y);
void em_gs_write_pixel(EmGs *gs, uint32_t bp, uint32_t bw, unsigned psm, uint32_t x, uint32_t y, uint32_t v);

#endif
