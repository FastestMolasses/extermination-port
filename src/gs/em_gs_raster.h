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
    EM_GS_REFUSE_MEMORY     = 1u << 4, /* no memory for the address tables (EmGs.swz)    */
    EM_GS_REFUSE_ORDER      = 1u << 5, /* parsed list (EmGs.list): a CLUT load from memory
                                          a span of the list draws, with no drain
                                          (em_gs_list_drain), or a transfer            */
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
    /* Pixel test counters (a diagnostic; em_gs_world.h EM_GS_Z_COUNTERS):
     * texture samples taken, and of them bilinear (four texels); pixels the
     * Z test rejected (ZTST NEVER included), and of them textured and
     * textured bilinear; textured pixels DATE rejected. Counting changes
     * nothing drawn. */
    struct { uint64_t samples, samples_bil, zfail, zfail_tex, zfail_bil, datefail_tex; } pix;
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
    /* Row bands (em_gs_world's parallel drawing): with band_count > 1 this
     * EmGs writes only the window rows y with ((y >> band_shift) %
     * band_count) == band_index. Everything else (registers, the span, the
     * CLUT, the counters of drawn and refused primitives) is the same in
     * every band, so N EmGs over one local memory, each fed the same writes
     * with its own band, draw exactly what one EmGs draws. 0 or 1: all rows. */
    uint32_t band_count, band_index, band_shift;
    /* Row range bands (docs/PERFORMANCE.md M4): with band_rows 1 this EmGs
     * writes only the window rows y with band_y0 <= y <= band_y1, and
     * band_count, band_index and band_shift are not read. em_gs_world's
     * workers draw a frame's parsed list (EmGsList below) a band at a time
     * this way. */
    int band_rows;
    int32_t band_y0, band_y1;
    /* Parse once (EmGsList below): with `list` set, a span that ends is
     * appended to the list, with what drawing it reads (the primitives as
     * queued, the CLUT, DIMX, the pages it writes), instead of drawn; the
     * EmGs then draws nothing and writes no memory (a transfer is refused,
     * EM_GS_REFUSE_ORDER). NULL: drawn. clut_changes counts the changes of
     * the CLUT buffer: a list's CLUT snapshot is keyed by the parsing EmGs's
     * clut_changes + 1 (one content per key, in every list it fills);
     * list_clut is the key of the snapshot an EmGs drawing lists holds in
     * its CLUT (0: none). */
    struct EmGsList *list;
    uint64_t clut_changes, list_clut;
    /* em_gs_list_draw's counts (a diagnostic): spans walked, spans drawn
     * (some primitive reaches the band), primitives drawn (draw_one calls). */
    struct { uint64_t spans, spans_drawn, prims; } lsc;
    /* Test hook (tools/test_gs_raster_reference.py part F): every register
     * write, IMAGE transfer and flush this EmGs takes is passed on to `tee`
     * after it, so a chain of band EmGs takes one stream in lockstep. NULL:
     * none. */
    struct EmGs *tee;
    /* The last accepted primitive setup and its kind, kept until a register
     * write other than PRIM's vertex registers (RGBAQ, ST, UV, FOG, XYZ):
     * the next primitive of that kind takes the same setup (heap, freed by
     * em_gs_release). */
    void *setup_cache;
    uint32_t setup_valid, setup_kind;
    /* The separable address tables (em_gs_raster.c section 1b): built on
     * the first draw and per buffer width on first use (heap, freed by
     * em_gs_release). Without memory for them a primitive is refused
     * (EM_GS_REFUSE_MEMORY). */
    void *swz;
    /* The decoded texture cache (em_gs_raster.c section 4b, docs/PERFORMANCE.md
     * M3): CT32 textures, and T8 / T4 textures with a CT32 CLUT, kept as a
     * linear 32-bit copy with the palette applied, per (texture, palette,
     * the texel rectangle CLAMP lets it reach), decoded a row at a time on
     * first read. It reads what the texture's memory holds and is dropped
     * where that may change: by a transfer, a draw whose frame or Z buffer
     * covers its pages (in the span that draws, the texture is read from
     * memory), or em_gs_memory_written. Heap, freed by em_gs_release; with
     * no memory for it the texels are read from memory, as with tc_off 1.
     * Every texel it gives is the one memory gives (tools/test_gs_raster_reference.py,
     * tests/gs_world_test.c). */
    void *tc;
    int tc_off;                        /* 1: no cache (every texel from memory) */
    struct {
        uint64_t binds;                /* textured primitives drawn from a decoded copy   */
        uint64_t uncached;             /* textured primitives drawn from memory (below)   */
        uint64_t uncached_written;     /*   of them: their texture is drawn in the span   */
        uint64_t created, renewed;     /* copies made, and made again after a change     */
        uint64_t evicted, resets;      /* copies dropped for room; whole-cache resets     */
        uint64_t rows, texels;         /* rows and texels decoded                         */
        uint64_t samples, samples_bil; /* the samples (EmGs.pix) taken from decoded copies */
        uint64_t bytes;                /* decoded bytes held now                          */
    } tcs;
    /* The specialized span loops (em_gs_raster.c section 9b, docs/PERFORMANCE.md
     * M2): the states the first level draws most of its pixels in go through
     * loops that read the primitive's state once, the rest through the
     * generic path; both give the same bytes. spec_off 1: every primitive
     * through the generic path. spec_check 1 (a developer check, EM_GS_SPEC_CHECK=1):
     * each primitive a specialized loop takes is drawn by the generic path
     * first, its frame and Z words put back, then drawn by the loop, and the
     * words and counters compared; a difference aborts. spec_log: heap,
     * freed by em_gs_release. spc: primitives and drawn pixels per loop
     * (sprites, textured triangles, untextured triangles) and the primitives
     * checked; counting changes nothing drawn. */
    int spec_off, spec_check;
    struct { uint64_t prims[3], pixels[3], checked; } spc;
    void *spec_log;
    size_t spec_log_cap;
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
/* The same address through the separable tables the drawing path uses
 * (em_gs_raster.c section 1b; tests/gs_swizzle_test.c): the word address
 * for PSMCT32 / CT24 and PSMZ32 / Z24, the byte address for PSMT8, the
 * nibble address for PSMT4, each wrapped to local memory; bw 0..63.
 * UINT32_MAX for another PSM, or without memory for the tables (builds
 * them in gs->swz: em_gs_release frees them). */
uint32_t em_gs_swz_addr(EmGs *gs, unsigned psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);
/* Reads / writes one pixel of any storage mode (value in the mode's bits). */
uint32_t em_gs_read_pixel(const EmGs *gs, uint32_t bp, uint32_t bw, unsigned psm, uint32_t x, uint32_t y);
void em_gs_write_pixel(EmGs *gs, uint32_t bp, uint32_t bw, unsigned psm, uint32_t x, uint32_t y, uint32_t v);
/* Local memory was written outside this EmGs (another model's transfer, a
 * memory image, a restore): `blocks` 256-byte blocks from `first_block`,
 * wrapping at the 4 MiB. The decoded textures that read them are dropped.
 * Call it between drawing calls, for every EmGs over that memory. */
void em_gs_memory_written(EmGs *gs, uint32_t first_block, uint32_t blocks);

/* Parse once (docs/PERFORMANCE.md M4; em_gs_raster.c section 12). One EmGs
 * with EmGs.list set takes a stream (em_gs_write / em_gs_gif) and appends
 * every span that ends to the list instead of drawing it: the primitives as
 * queued (their Draw, context and vertices, the span's grid decision), the
 * CLUT buffer and DIMX they are drawn with, the rows each can draw and the
 * pages the span writes. Other EmGs over the same local memory then draw
 * the spans with em_gs_list_draw, each its band of rows (EmGs.band_rows),
 * which draws in those rows exactly what the parsing EmGs would have drawn
 * itself: the same primitives in the same order, with the same state. A
 * span's appended data never moves, so spans below em_gs_list_spans() can
 * be drawn while later ones are appended (em_gs_world orders that).
 *
 * The parsing EmGs reads memory only for CLUT loads; a load from a page a
 * span of the list writes first has the spans so far drawn
 * (em_gs_list_drain), or without that is refused (EM_GS_REFUSE_ORDER: the
 * drawing would decide what it reads). A transfer is refused. */
typedef struct EmGsList EmGsList;
EmGsList *em_gs_list_create(void);
void em_gs_list_destroy(EmGsList *l);
/* Empties the list (its storage is kept); the next appended span is span 0. */
void em_gs_list_clear(EmGsList *l);
/* Spans appended so far. */
uint32_t em_gs_list_spans(const EmGsList *l);
/* 1 when a span could not be appended (no memory): the list is incomplete. */
int em_gs_list_failed(const EmGsList *l);
/* The window rows the primitives of spans [s0, s1) can draw: *ylo..*yhi
 * (an empty range, *ylo > *yhi, when they draw none). Conservative: every
 * row they draw is in it. */
void em_gs_list_rows(const EmGsList *l, uint32_t s0, uint32_t s1, int32_t *ylo, int32_t *yhi);
/* (s1 at most em_gs_list_spans() when the spans were appended: the caller
 * orders the appending EmGs's thread and the drawing ones, which read only
 * those spans.) Draws spans [s0, s1) into gs's rows (EmGs.band_rows; all rows without
 * bands): gs takes the CLUT and DIMX each span was drawn with, and its
 * decoded texture cache the pages each span writes, also for spans with no
 * primitive in its rows (section 4b keeps a copy good only while no span
 * wrote its pages). `before` (NULL: none) is called with ctx and each span's
 * number before the span is drawn: it may change gs's rows (EmGs.band_y0,
 * band_y1) for that span and the later ones, or return 0 to stop there.
 * Returns the span it stopped at (s1 when it drew them all). */
uint32_t em_gs_list_draw(EmGs *gs, const EmGsList *l, uint32_t s0, uint32_t s1, int (*before)(void *, uint32_t),
                         void *ctx);
/* The pages spans [s0, s1) write, given to gs's decoded texture cache, for
 * an EmGs that drew none of them (another drew all their rows). */
void em_gs_list_pass(EmGs *gs, const EmGsList *l, uint32_t s0, uint32_t s1);
/* While l is appended to, `prior` (a whole list another thread may still be
 * drawing; NULL: none) comes before it: a CLUT load that reads a page prior
 * writes calls wait(ctx) first, which returns once prior is drawn, and then
 * reads memory (prior is not consulted again). */
void em_gs_list_prior(EmGsList *l, const EmGsList *prior, void (*wait)(void *), void *ctx);
/* A CLUT load that reads a page a span appended to l since the last drain
 * writes first calls drain(ctx), which returns 1 once every span appended so
 * far is drawn into memory (the load then reads it, as one EmGs drawing
 * directly reads it there: the spans before the load drawn, the span still
 * queued not), or 0 when it cannot (the load is refused,
 * EM_GS_REFUSE_ORDER). NULL (the default): refused. Kept by
 * em_gs_list_clear. */
void em_gs_list_drain(EmGsList *l, int (*drain)(void *), void *ctx);

#endif
