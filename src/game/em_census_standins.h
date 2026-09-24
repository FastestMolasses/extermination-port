/* em_census_standins.h - translations of the originals behind the census
 * stand-ins that had none (docs/CENSUS_STANDINS.md; census recount
 * 2026-09-24, docs/FIRST_LEVEL_CENSUS.md). Prefix em_cs_.
 *
 * Hand translations of these original routines (boot ELF SCUS-97112):
 *
 *   00102CD0  SDK look-at: the camera view matrix of 0018C0D0 (byte-matched
 *             C). Identity, row 0 = normalize(cross(up, forward)), row 2 =
 *             normalize(forward), row 1 = cross(row 2, row 0), row 3 += pos,
 *             then the rigid inverse 001027E0 into `out`. Every step is an
 *             existing verified translation of its SDK leaf:
 *             001029C0 / 00102918 em_owner_services_original,
 *             00102718 / 00102760 em_effect_original,
 *             001027E0 em_render_verify_rest.
 *             Live stand-in: em_math.h em_mat4_lookat_gs in em_camera.c
 *             camera_commit_view.
 *
 *   001FCB90  mode-4 help presenter (byte-matched C):
 *             001FE070(sub-bank `group` of *D_0028A498, line, x, y)
 *   001FCF60  mode-4 record title presenter (byte-matched C):
 *             001FE070(sub-bank 1 of *D_0028A49C, line, x, y)
 *   001FCF90  mode-4 record list presenter (NEARMISS C; the .s was
 *             followed): up to ten segments of string `line` of sub-bank 2
 *             of *D_0028A49C, from segment page * 10, each through
 *             001FC770(0x38, 0x2F + 0xC * i, text, &D_00264CF0); returns 1
 *   001FE660  segment count of a string: 001FE530(NULL, p, 0) until the
 *             terminator (byte-matched C; the callee of 001FCF90)
 *   001FD0E0  mode-3 cue presenter (NEARMISS C; the .s was followed, and
 *             it differs from the C: a 0x0A byte ENDS the current line, one
 *             of four, instead of continuing it)
 *             Live stand-ins: em_message_live.c faults on modes 3/4, the
 *             binder's step-F gate holds the service while a status page
 *             runs, and em_area11_interaction_host.c presents the status
 *             page's mode-4 lines from its own copy of the request block.
 *
 *   0020CCB0  BATTERY page selection marker (byte-matched C): one
 *             00207F80(1, x0, 0x85E0, x1, 0x8640, 0x80CE6000) rectangle,
 *             x0 = float_to_int(16.0f * (float)(n + 0x700)), x1 the same
 *             with n + 0x70C, n = 0xFD when page byte +6 is 0, else 0x14F.
 *             Live stand-in: em_battery_ui.c draws a hand-placed sprite.
 *
 *   0021BAE0  render-context record restore (byte-matched C):
 *             block_copy(context + 0xA0, context + 0x120 + 32 * slot, 0x20).
 *             Live stand-in: the status page END_PROJECTION event only
 *             clears world.status_ui_context.
 *
 * Conventions (those of the modules the translations bind next to):
 *   - Every original callee that is not translated is an explicit worker
 *     named by its original address. The message presenters use the
 *     translated em_message_draw_original.c routines (001FE070, 001FE530,
 *     001FC770, 001FE460, 001FE480, 001FE4D0) of the service's own
 *     EmMessageDraw, whose workers are the glyph advance 001CBE10 and the
 *     glyph-run draw 001FC7B0.
 *   - A reachable NULL worker or a read outside the supplied bank or record
 *     faults (-1) instead of reading what the original would read there.
 *     The presenters latch the fault in EmCsPresenters.fault (and the draw
 *     module latches its own); writes made before a fault stay, in the
 *     original order.
 *   - Float arithmetic is em_ee_float.h on bit patterns (0020CCB0) or the
 *     float models of the reused SDK-leaf translations (00102CD0).
 *
 * Oracle: tools/test_census_standins_reference.py executes the original
 * instructions of every routine above over the user's ELF and captured EE
 * RAM and compares every written byte, every worker call and every result.
 */
#ifndef EM_CENSUS_STANDINS_H
#define EM_CENSUS_STANDINS_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_message_draw_original.h"
#include "game/em_message_service.h"
#include "game/em_status_ui_leftovers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * 00102CD0: the look-at
 * ====================================================================== */

/* 00102CD0(out, pos, fwd, up) on binary32 bit patterns (four lanes each;
 * the w lanes of pos / fwd / up are not read). out is the original's row
 * vector view matrix: rows 0..2 the transposed basis, row 3 the negated
 * translation, out[15] = 1.0. 0, or -1 when a reused leaf's float model
 * refuses an operand (nothing is then guaranteed about out). */
int em_cs_00102CD0(uint32_t out[16], const uint32_t pos[4], const uint32_t fwd[4],
                   const uint32_t up[4]);

/* The port renderer's convention (em_math.h em_mat4_lookat_gs: Y up,
 * negative Z forward, column-major) is the original matrix with the y and
 * z lane of every row negated. This is that exact sign-bit flip; it is a
 * renderer adapter, not game logic (docs/CENSUS_STANDINS.md 2). */
void em_cs_view_to_native(float native[16], const uint32_t view[16]);

/* ======================================================================
 * The mode-3 / mode-4 message presenters
 * ====================================================================== */

/* Original data the presenters read. Banks are images of the original
 * bytes starting at the address the pointer cell holds. */
typedef struct EmCsMessageData {
    EmMessageBank help;          /* *D_0028A498: help container (001FCB90)  */
    EmMessageBank records;       /* *D_0028A49C: record container (001FCF60 /
                                  * 001FCF90)                               */
    EmMessageBank cue;           /* *D_0028A4EC: the mode-3 cue bank         */
    /* The value 001FD0E0 stores for a pointer into the cue bank is
     * cue_address + offset (block +0x18 / +0x1C, the record-slot table).
     * With the original bank address these words equal the original's;
     * any other token works the same way (they are only read back here
     * and passed to the 001FDDB0 worker). */
    uint32_t cue_address;
    const EmMessageDrawConfig *config_264CF0; /* 001FCF90's 001FC770 config */
    const EmMessageDrawConfig *config_264C90; /* 001FD0E0 reads word[2] (D_00264C98)
                                               * and word[4] (D_00264CA0) */
    uint32_t config_264C90_address;           /* &D_00264C90, stored at +0x20 */
    uint32_t d264DB0[6];                      /* D_00264DB0: the 0x18-byte counter
                                               * block copied into the frame */
} EmCsMessageData;

/* Workers of 001FD0E0. Each returns 1 on success; 0 latches a fault. */
typedef struct EmCsMode3Workers {
    void *context;
    /* 001FC9B0(): the message service reset. It clears the service's
     * request block D_002821B0 (0x9C bytes) and restores the text defaults;
     * the live binding is em_message_reset on the service whose block is
     * the one 001FD0E0 was given. */
    int (*reset)(void *context);
    /* 001FDDB0(slots, count, block): one cue line. slots is the line's
     * record-slot table (64 words, the values stored as described at
     * cue_address), line its 0x40-byte glyph buffer (the buffer block +0x2C
     * names), count its glyph count. *result = the original's return:
     * 2 and 0 end the presenter, anything else continues. */
    int (*line_draw)(void *context, const uint8_t *slots, int32_t count, uint8_t *line,
                     EmMessageBlock *block, int32_t *result);
} EmCsMode3Workers;

enum {
    EM_CS_FCF90_BUFFER = 0x280, /* 001FCF90's stack buffer: ten 0x40 lines   */
    EM_CS_FD0E0_FRAME = 0x798   /* 001FD0E0's frame sp+0xC0 .. sp+0x858: the
                                 * 0x180-byte glyph lines, the 0x600-byte
                                 * record-slot tables, the 0x18-byte counters */
};

typedef struct EmCsPresenters {
    EmMessageDraw *draw;         /* the service's draw module (bound, init'd) */
    const EmCsMessageData *data;
    EmCsMode3Workers mode3;
    uint32_t d820EC0[4];         /* D_00820EC0..D_00820ECC (001FD0E0 state 0) */
    int32_t *d282228;            /* D_00282228: the word at +0x78 of D_002821B0 */
    uint32_t frame_address;      /* token for the frame's first glyph line; block
                                  * +0x2C = frame_address + 0x40 * line */
    uint8_t list[EM_CS_FCF90_BUFFER];
    uint8_t frame[EM_CS_FD0E0_FRAME];
    const char *fault;           /* latched */
} EmCsPresenters;

/* Binds. 1, or 0 when p, draw or data is NULL. Clears the fault. */
int em_cs_presenters_init(EmCsPresenters *p, EmMessageDraw *draw, const EmCsMessageData *data,
                          const EmCsMode3Workers *mode3, int32_t *d282228);

/* Each returns 0 (and *result where the original returns a value) or -1
 * on a fault. */
int em_cs_001FCB90(EmCsPresenters *p, int32_t x, int32_t y, int32_t group, int32_t line,
                   int32_t *result);
int em_cs_001FCF60(EmCsPresenters *p, int32_t line, int32_t x, int32_t y, int32_t *result);
int em_cs_001FCF90(EmCsPresenters *p, int32_t line, int32_t page, int32_t *result);
/* 001FE660 over `bank` from byte `offset` (the original's pointer). */
int em_cs_001FE660(EmCsPresenters *p, const EmMessageBank *bank, uint32_t offset,
                   int32_t *count);
int em_cs_001FD0E0(EmCsPresenters *p, EmMessageBlock *block);

/* EmMessageWorkers adapters (em_message_service.h), context = the
 * EmCsPresenters: 1 on success, 0 on a fault. record_setup ignores its
 * group argument, as 001FCF90 does its third argument. */
int em_cs_worker_mode3_present(void *p, EmMessageBlock *block);
int em_cs_worker_help_draw(void *p, int x, int y, int32_t group, uint32_t line);
int em_cs_worker_record_setup(void *p, uint32_t line, int32_t page, int32_t group,
                              int32_t *result);
int em_cs_worker_record_draw(void *p, uint32_t line, int x, int y);

/* ======================================================================
 * 0020CCB0: the BATTERY page selection marker
 * ====================================================================== */

typedef struct EmCsRectWorkers {
    void *context;
    /* float_to_int 001281C0(bits): *value = its return value */
    int (*float_to_int)(void *context, uint32_t bits, int32_t *value);
    /* 00207F80(slot, x0, y0, x1, y1, rgba): one untextured rectangle */
    int (*rectangle)(void *context, int32_t slot, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                     uint32_t rgba);
} EmCsRectWorkers;

/* 0020CCB0(page): page/page_size the page record (+6 is read). 0, or -1
 * with nothing called when a worker is NULL or the page is too small, or
 * -1 when a worker returns a negative value. */
int em_cs_0020CCB0(const EmCsRectWorkers *w, const uint8_t *page, size_t page_size);

/* ======================================================================
 * 0021BAE0: the render-context record restore
 * ====================================================================== */

/* 0021BAE0(slot): context/context_size is the block D_00275670 points at;
 * the copy goes through w->block_copy (00121870, the worker em_sul_0021BAC0
 * uses for the matching save). 0, or -1 with nothing copied when the worker
 * is NULL or either 32-byte range lies outside the block, or -1 when the
 * worker returns a negative value. */
int em_cs_0021BAE0(const EmSulWorkers *w, uint8_t *context, size_t context_size, int32_t slot);

#ifdef __cplusplus
}
#endif

#endif /* EM_CENSUS_STANDINS_H */
