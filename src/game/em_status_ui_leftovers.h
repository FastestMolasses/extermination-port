/* em_status_ui_leftovers.h - census lane L35 (status UI leftovers) and the
 * BATTERY page draw (docs/STATUS_UI_LEFTOVERS.md). Prefix em_sul_.
 *
 * Hand translations of these original routines (boot ELF SCUS-97112):
 *   0020AE40  status page frame: the fixed frame sprites of a status
 *             sub-page, the 00208AD0 / 00209280 gauges by flag, and the
 *             trailing title plate (byte-matched C)
 *   0020B0D0  the page's up/down arrow pair, picked by the held pad word
 *             D_00810E70 (byte-matched C)
 *   0020B210  the page's list: cursor input, ring refill, up to four rows,
 *             the selection commit to D_002821B4 / D_002821B8, the cursor
 *             highlight, the selected-row marker and the D_00282240 4 -> 3
 *             drop (NEARMISS C; followed against the .s)
 *   0020BEF0  list ring index: (+0x17 + +0x19) wrapped once by +0x18
 *   0020CD40 / 0020CD60 / 0020CDA0
 *             UI cue thunks: 001FB9F0(0 / 1 / 4, 0x1000, 0x1000, 0x1000)
 *   001C5930  area-title node behaviour (NEARMISS C, 69 %; this translation
 *             follows the .s, which differs from that C: the second line of
 *             band 5 passes the style record D_00265520, not 0)
 *   001C5860  infection band of 100 - D_008104D8 (0..5)
 *   001C4820  placed-prop / pickup node behaviour (byte-matched C)
 *   001B0BA0  boot-time builder of D_00289B40, the per-area title base
 *             table 001C5930 reads (the data source for its binding)
 *   0021B8E0 / 0021B900 / 0021BAC0
 *             render-context record saves: 32 bytes from context +0xA0 to
 *             +0xE0 / +0xC0 / +0x120 + 32 * slot (00121870 block_copy)
 *   0021BA70  context +0xB0 = the 64-bit argument, then 0021B900
 *   0021BAB0  returns the 64-bit context word +0xB0
 *   0022EBE0  "cinematic" predicate: D_008101E4 == 3, or the scratchpad
 *             mode byte 0x70003B8D is neither 0 nor 4
 *
 * Everything these routines reach that is not translated here is an
 * explicit worker named by its original address (EmSulWorkers). The 2D GS
 * draw layer (00207D00 / 00207E40, 001CBA50 / 001CC1E0 text) is the
 * boundary: the workers receive exactly the arguments the original passes,
 * which fully determine the packets 00207D00 / 00207E40 build.
 *
 * Original memory the routines read (tables, globals, the scratchpad byte)
 * is reached through EmSulMemory, a list of read-only regions addressed by
 * original address. Bytes they write are caller-owned mutable blocks (the
 * page, the actor record, the render context) or explicit globals.
 *
 * Fail-stop: every routine checks, before its first write or worker call,
 * that every worker it can reach is bound and that every original byte it
 * will read or write lies inside the supplied regions and blocks (the list
 * and the area title run their control flow once without side effects to
 * find those addresses; no worker result steers their control flow). A
 * missing worker or region returns -1 with nothing written. A worker that
 * returns a negative value is a fault too (-1); the writes and calls made
 * before it stay, in the original order.
 *
 * Arithmetic: the list's y coordinates are 16.0f * (float)n through the EE
 * COP1 model (em_ee_float.h); 001C5860 subtracts and compares through it.
 *
 * Oracle: tools/test_status_ui_leftovers_reference.py executes the original
 * instructions (COP1 through tools/ee_float_model.py) and compares every
 * written byte and every worker call, in order, with this module, and
 * replays the draw calls through the original 00207D00 / 00207E40 against
 * the packets captured from the original BATTERY page. */
#ifndef EM_STATUS_UI_LEFTOVERS_H
#define EM_STATUS_UI_LEFTOVERS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- original addresses the routines pass on --------------------------- */
#define EM_SUL_STYLE_00265510 0x00265510u /* 0020B210 cursor-row text style */
#define EM_SUL_STYLE_00265518 0x00265518u /* 0020B210 other-row text style */
#define EM_SUL_STYLE_00265520 0x00265520u /* 001C5930 band-5 line style */

/* ---- read-only original memory ----------------------------------------- */
typedef struct EmSulRegion {
    uint32_t base;          /* original address of bytes[0] */
    uint32_t size;
    const uint8_t *bytes;
} EmSulRegion;

typedef struct EmSulMemory {
    const EmSulRegion *regions;
    unsigned count;
} EmSulMemory;

/* ---- workers ----------------------------------------------------------- */
/* Each returns 0 (or any non-negative value) on success and a negative
 * value on a fault. `actor` is the caller's record the original passes. */
typedef struct EmSulWorkers {
    void *context;
    /* 00207D00(slot, mode) */
    int (*blend)(void *context, int32_t slot, int32_t mode);
    /* 00207E40(slot, x, y, w, h, rgba, tex0) */
    int (*sprite)(void *context, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h,
                  uint32_t rgba, uint64_t tex0);
    /* 001CBA50(slot, x, y, w, h, text, style): text is 0020B210's stack
     * copy; style the original address of the style record */
    int (*text_fixed)(void *context, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h,
                      const char *text, uint32_t style);
    /* 001CC1E0(slot, x, y, w, h, text, style): text and style are
     * original addresses (style 0 = none) */
    int (*text_proportional)(void *context, int32_t slot, int32_t x, int32_t y, int32_t w,
                             int32_t h, uint32_t text, uint32_t style);
    /* 001CC170(text): *width = its return value */
    int (*text_width)(void *context, uint32_t text, int32_t *width);
    /* 00208AD0(page, x, y) */
    int (*health)(void *context, uint32_t page, int32_t x, int32_t y);
    /* 00209280(page, x, y, tex0, compact) */
    int (*battery)(void *context, uint32_t page, int32_t x, int32_t y, uint64_t tex0,
                   int32_t compact);
    /* float_to_int 001281C0(bits): *value = its return value */
    int (*float_to_int)(void *context, uint32_t bits, int32_t *value);
    /* 001C5FB0(value, a1, a2): *text = the string it returns */
    int (*format)(void *context, int32_t value, int32_t a1, int32_t a2, const char **text);
    /* 00123168(dst, src) into a buffer of dst_size bytes; the worker
     * faults when src with its terminator does not fit */
    int (*copy)(void *context, char *dst, size_t dst_size, const char *src);
    /* 001FB9F0(id, a1, a2, a3) */
    int (*sound)(void *context, int32_t id, int32_t a1, int32_t a2, int32_t a3);
    /* 00121870 block_copy(dst, src, count) inside the render context;
     * dst_offset / src_offset are offsets from the context base */
    int (*block_copy)(void *context, uint8_t *context_block, uint32_t dst_offset,
                      uint32_t src_offset, int32_t count);
    /* 001AFC10(actor) */
    int (*free_actor)(void *context, uint8_t *actor);
    /* 001B0FD0(actor): *result = its return value */
    int (*model_bind)(void *context, uint8_t *actor, int32_t *result);
    /* 001C6380(actor) */
    int (*place)(void *context, uint8_t *actor);
    /* 001B17A0(actor) */
    int (*publish)(void *context, uint8_t *actor);
    /* the indirect call through the actor's +0x4C method word: method(actor) */
    int (*method)(void *context, uint8_t *actor, uint32_t method);
} EmSulWorkers;

/* ---- the BATTERY page draw (and every status sub-page using them) ------ */

/* 0020AE40(page, table, flags). page is the original page address (only
 * passed on to 00208AD0 / 00209280); table the original address of the
 * page's 64-bit TEX0 table (D_00265C50 for BATTERY). */
int em_sul_0020AE40(const EmSulWorkers *w, const EmSulMemory *mem, uint32_t page,
                    uint32_t table, int32_t flags);

/* 0020B0D0(unused, table): reads D_00810E70 through mem. */
int em_sul_0020B0D0(const EmSulWorkers *w, const EmSulMemory *mem, uint32_t table);

/* The globals 0020B210 writes (D_00282240 is read first). */
typedef struct EmSulListGlobals {
    int32_t d2821B4; /* D_002821B4 */
    int32_t d2821B8; /* D_002821B8 */
    int32_t d282240; /* D_00282240 */
} EmSulListGlobals;

/* 0020B210(page, table, glyph, flags). page/page_size is the mutable page
 * record (original offsets; at most EM_SUL_PAGE_MAX bytes), table the
 * original address of its 24-byte row records (D_00265CD0 for BATTERY).
 * Reads D_00810E78, D_00810C70..72 and D_00810700 + 0x564 + n through
 * mem. *result = the original return value. */
#define EM_SUL_PAGE_MAX 0x400u
int em_sul_0020B210(const EmSulWorkers *w, const EmSulMemory *mem, uint8_t *page,
                    size_t page_size, uint32_t table, uint64_t glyph, int32_t flags,
                    EmSulListGlobals *globals, int32_t *result);

/* 0020BEF0(page): *index = (+0x17 + +0x19), less +0x18 once when not below. */
int em_sul_0020BEF0(const uint8_t *page, size_t page_size, int32_t *index);

/* ---- UI cues ------------------------------------------------------------ */
int em_sul_0020CD40(const EmSulWorkers *w); /* 001FB9F0(0, 0x1000 x3) */
int em_sul_0020CD60(const EmSulWorkers *w); /* 001FB9F0(1, 0x1000 x3) */
int em_sul_0020CDA0(const EmSulWorkers *w); /* 001FB9F0(4, 0x1000 x3) */

/* ---- area title --------------------------------------------------------- */

/* 001C5860(): the band of 100.0f - D_008104D8 (raw float bits). */
int32_t em_sul_001C5860(uint32_t d8104D8);

/* 001C5930(actor). actor/actor_size is the node record (+0x04..+0x06,
 * +0x28, +0x2A, +0x1F0, +0x1F4 are used). Reads D_00810700, D_00810701,
 * D_008106B8, D_008104D8, D_00289B40, D_002671C0, D_0026726C and the
 * scratchpad byte 0x70003B8D through mem. */
int em_sul_001C5930(const EmSulWorkers *w, const EmSulMemory *mem, uint8_t *actor,
                    size_t actor_size);

/* 001B0BA0(): builds D_00289B40, the per-area base of the title string
 * table 001C5930 reads, from the 23 halfword counts at D_0024A850 (ELF
 * data): entry i = {running base, count i}; the base advances by the count,
 * or by 1 when the count is 0. Writes the 0x5C table bytes (little endian).
 * It runs once at boot (census note: boot before the title is not
 * instrumented), so the live port can build the table from exported data. */
#define EM_SUL_AREA_COUNT 23
int em_sul_001B0BA0(const int16_t counts[EM_SUL_AREA_COUNT], uint8_t table[EM_SUL_AREA_COUNT * 4]);

/* ---- placed prop / pickup node ------------------------------------------ */

/* 001C4820(actor): the +0x04 state dispatch. */
int em_sul_001C4820(const EmSulWorkers *w, uint8_t *actor, size_t actor_size);

/* ---- render-context record saves ---------------------------------------- */
/* context/context_size is the block D_00275670 points at. */
int em_sul_0021B8E0(const EmSulWorkers *w, uint8_t *context, size_t context_size);
int em_sul_0021B900(const EmSulWorkers *w, uint8_t *context, size_t context_size);
int em_sul_0021BAC0(const EmSulWorkers *w, uint8_t *context, size_t context_size, int32_t slot);
int em_sul_0021BA70(const EmSulWorkers *w, uint8_t *context, size_t context_size, uint64_t value);
int em_sul_0021BAB0(const uint8_t *context, size_t context_size, uint64_t *value);

/* ---- cinematic predicate ------------------------------------------------ */
/* 0022EBE0(): 1 or 0. */
int32_t em_sul_0022EBE0(uint8_t d8101E4, uint8_t spad3B8D);

#ifdef __cplusplus
}
#endif

#endif /* EM_STATUS_UI_LEFTOVERS_H */
