/* em_message_presenter_rest.h - the rest of the mode-3 / mode-4 message
 * presenters: the cue line walker 001FDDB0 and the presenters' data file.
 * docs/MESSAGE_PRESENTER_REST.md. Prefix em_mpr_.
 *
 * Hand translation of this original routine (boot ELF SCUS-97112):
 *
 *   001FDDB0  mode-3 cue line walker (NEARMISS in the decomp; the .s was
 *             followed, and it differs from the decomp C in two ways,
 *             docs/MESSAGE_PRESENTER_REST.md 2). Its only caller is the
 *             mode-3 cue presenter 001FD0E0 (em_census_standins.c
 *             em_cs_001FD0E0), once per cue line, as
 *             001FDDB0(slots, count, block):
 *               - x = block +0x24, y = block +0x28 (locals; the block is not
 *                 moved), the run buffer D_00820F50 (0x40 bytes) cleared,
 *                 width = 0;
 *               - for i = 0 .. count (inclusive) while i < 0x40:
 *                 * slot i non-zero (a record of the cue bank): draw the run
 *                   so far through 001FC770(x, y, D_00820F50, block +0x20),
 *                   then x += width, clear run and width, and act on the
 *                   record's tag (word +0):
 *                     0x20  return 0;
 *                     1     reveal gate: when block +0x40 <= record +8
 *                           (unsigned), by block +0x48: 0 arms (+0x58 =
 *                           record +4, +0x40 = record +8; a zero count adds 1
 *                           to +0x40 and skips glyph i, else +0x48 = 1, the
 *                           sound 001FB9F0(0x8C9, 0x1000, 0x1000, 0x1000)
 *                           and return 0); 1 counts +0x58 down (still > 0:
 *                           return 0; else +0x48 = +0x58 = 0, +0x40 += 1 and
 *                           glyph i is skipped); any other value returns 0;
 *                     2     D_00275C50 = D_0026EC10[record +4];
 *                     3     D_00275C55 = (byte record +4) << 3;
 *                     4     both, the flag from byte record +0xC;
 *                   every other outcome (tag 0, 2, 3, 4, other, gate
 *                   passed) then emits glyph i; a skipped glyph still
 *                   advances the run index, leaving a 0 in the run;
 *                 * emit: D_00820F50[run] = glyph i of the line (block
 *                   +0x2C), width += 001CC170 of that one glyph;
 *               - return 1. The run after the last record is not drawn.
 *
 * The presenters' data (the three banks, the two configs with their style
 * blocks, D_00264DB0) come from assets/message/message_presenters.emmp,
 * written by tools/export_message_data.py from the user's own ELF and
 * extracted disc files; em_mpr_data_parse turns it into the
 * EmCsMessageData em_cs_presenters_init takes.
 *
 * Conventions (those of em_census_standins.h / em_message_draw_original.h):
 *   - 001FC770, 001CC170 and the D_00820F50 run buffer are the service's own
 *     EmMessageDraw (D_00820F50 is `measure + 0x80`: D_00820ED0 + 0x80);
 *     D_0026EC10 and D_00275C50 are its data's `colors` and `text`.
 *   - 001FB9F0 is a worker; it returns a negative value on a fault.
 *   - A NULL worker, a record outside the cue bank, a colour index outside
 *     D_0026EC10 or a block +0x20 that names no supplied config faults (-1)
 *     instead of reading what the original would read there. The fault is
 *     latched in EmMprLineDraw.fault; writes made before it stay, in the
 *     original order.
 *
 * Oracle: tools/test_message_presenter_rest_reference.py executes the
 * original 001FDDB0 (and 001FD0E0 around it) over the user's ELF and
 * captured EE RAM and compares every draw, glyph-advance and sound call,
 * every block, style and buffer byte and every result; it also checks the
 * exported file against every capture. */
#ifndef EM_MESSAGE_PRESENTER_REST_H
#define EM_MESSAGE_PRESENTER_REST_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_census_standins.h"
#include "game/em_message_draw_original.h"
#include "game/em_message_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * 001FDDB0
 * ====================================================================== */

enum {
    EM_MPR_SLOTS = 0x40,      /* record slots per line (0x100 bytes)        */
    EM_MPR_LINE = 0x40,       /* glyph bytes per line                        */
    EM_MPR_RUN = 0x80         /* D_00820F50 - D_00820ED0: the run buffer's
                               * offset in EmMessageDraw.measure             */
};

typedef struct EmMprLineDraw {
    EmMessageDraw *draw;          /* the service's draw module (bound)        */
    const EmCsMessageData *data;  /* cue bank + cue_address (the slot values),
                                   * config_264C90 + its address (+0x20)     */
    void *context;
    /* 001FB9F0(id, a1, a2, a3); negative = fault. */
    int (*sound)(void *context, int32_t id, int32_t a1, int32_t a2, int32_t a3);
    const char *fault;            /* latched */
} EmMprLineDraw;

/* Binds. 1, or 0 when l, draw, draw->data or data is NULL. Clears the
 * fault. */
int em_mpr_line_draw_init(EmMprLineDraw *l, EmMessageDraw *draw, const EmCsMessageData *data,
                          void *context,
                          int (*sound)(void *, int32_t, int32_t, int32_t, int32_t));

/* 001FDDB0(slots, count, block). `slots` is the line's 0x100-byte slot
 * table (64 little-endian words, each 0 or cue_address + a record's offset
 * in the cue bank), `line` its 0x40 glyph bytes (the buffer block +0x2C
 * names). *result = the original's return (0 or 1). 0, or -1 on a fault. */
int em_mpr_001FDDB0(EmMprLineDraw *l, const uint8_t *slots, int32_t count, const uint8_t *line,
                    EmMessageBlock *block, int32_t *result);

/* EmCsMode3Workers.line_draw adapter (context = the EmMprLineDraw): 1 on
 * success, 0 on a fault. */
int em_mpr_worker_line_draw(void *l, const uint8_t *slots, int32_t count, uint8_t *line,
                            EmMessageBlock *block, int32_t *result);

/* ======================================================================
 * assets/message/message_presenters.emmp
 * ====================================================================== */

/* The presenters' data, parsed from the exported image. The banks point
 * into the image (which must outlive this); the configs point at the
 * styles inside this struct, so it must not be moved after parsing. */
typedef struct EmMprData {
    EmCsMessageData cs;
    EmMessageDrawConfig config_264CF0;  /* style = &style_275830 */
    EmMessageDrawConfig config_264C90;  /* style = &style_275820 */
    EmMessageTextStyle style_275830;
    EmMessageTextStyle style_275820;
    uint8_t *owned;                     /* em_mpr_data_load's image, or NULL */
} EmMprData;

/* Parses an .emmp v1 image. `cue_address` becomes cs.cue_address (the
 * token 001FD0E0 stores for pointers into the cue bank; the original's own
 * value is the bank's load address, 0x011739C0 in every capture).
 * 1, or 0 when the image is not a well-formed v1 file (out is then
 * zeroed). */
int em_mpr_data_parse(EmMprData *out, const uint8_t *image, size_t size, uint32_t cue_address);

/* Reads and parses the file; the image is owned by `out`. 1 or 0. */
int em_mpr_data_load(EmMprData *out, const char *path, uint32_t cue_address);
void em_mpr_data_free(EmMprData *data);

#ifdef __cplusplus
}
#endif

#endif
