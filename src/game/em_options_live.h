/* em_options_live.h - the live binding of the options screen and its
 * memory-card load screen (em_options_original; docs/OPTIONS.md section 4).
 *
 * The translations run over views of the port's one storage at their
 * original addresses (the AREA01 UI lane's memory model, as the status
 * pages' binding em_status_pages_live does); every callee is dispatched by
 * its original address:
 *
 *   00207D00 / 00207E40 / 00207F80 / 0020A7A0   em_page_draw (the page's own
 *                         stream over the status pages' GS memory, the
 *                         screen modules 0x2B / 0x2A applied as they load)
 *   001CC1E0 / 001CC170 / 001FC770   the message service's glyph and line
 *                         draws (em_message_live), style D_00275828
 *   001FE480              em_message_bank_string over the help container
 *   001CBA50 / 001C5FB0   em_page_draw text, em_status_draw_001C5FB0
 *   00121A28, 00122EF0, 00123168, 00123280, 001232E0, 00123418   the libc
 *                         leaves (memset, strcat, strcpy, strcspn, strlen,
 *                         strncpy) over the views
 *   001281C0              em_player_float_to_int
 *   0020CD40 / 60 / A0    the cues (em_sul_*, the host's 001FB9F0)
 *   00114988 / 00114848 / 00114360 / 00114448 / 00114480 / 00114708 /
 *   00114630 / 00114AC0   em_memcard_call (the card I/O boundary: GetInfo,
 *                         Sync, Open, Mkdir, Close, Write, Read, GetDir)
 *                         over the views
 *   001D2830, 00200970, 001AEE10 / 001AEDE0, 001FBC50, 001FABB0, 001FF080,
 *   001B61C0              the host's workers
 *   002267A0 / 00227300   em_card_002267A0 / em_card_00227300 (the card
 *                         screen past the slot choice, docs/SAVE_GAME.md
 *                         section 6) on this binding's state
 *   00122D10, 00123020    sprintf (the conversions d and s) and strcmp
 *                         over the views
 *   00114D18, 00121870, 001AF2C0   fault: the save mode's Format, block
 *                         copy and New Game block (docs/OPTIONS.md 4.2)
 *
 * Its own storage for the card code's buffers (GetDir's table
 * D_00821500, the header D_00821980, the staging blocks D_008219D0 /
 * D_00822050 / D_00822690, the slot path D_00822010, the row text
 * D_00275C94..9F) and views of em_area_title's place names and D_00289B40.
 *
 * Fail-stop: a missing view, a refused draw or a failing worker latches
 * the call's fault (reported); the caller stops the game. */
#ifndef EM_OPTIONS_LIVE_H
#define EM_OPTIONS_LIVE_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_gs_texture.h"
#include "game/em_message_service.h"

typedef struct EmOptionsLive EmOptionsLive;

typedef struct {
    void *context;
    /* 001FB9F0(id, a1, a2, a3) (the cues) */
    int (*sound)(void *context, int32_t id, int32_t a1, int32_t a2, int32_t a3);
    /* 001FF080(0, module): the screen module through the loader; the busy
     * byte D_00275BD8 clears at its 0x63 step. 1 accepted. */
    int (*module_load)(void *context, uint32_t module);
    /* 00200970(1): the library slot 0x35 and the player texture back. 1 ok. */
    int (*restore)(void *context);
    /* 001AEDE0 (out != 0) / 001AEE10 (a0, a1): the colour fades. 1 ok. */
    int (*fade)(void *context, int out, int32_t a0, int32_t a1);
    /* 001FBC50 / 001FABB0. 1 ok. */
    int (*stop_sounds)(void *context);
    int (*stop_streams)(void *context);
    /* 001D2830(a0, a1): the render context's draw flag. 1 ok. */
    int (*draw_context)(void *context, int32_t a0, int32_t a1);
    /* 001B61C0(big, small, duration, force). 1 ok. */
    int (*rumble)(void *context, int32_t big, int32_t small, int32_t duration, int32_t force);
} EmOptionsHost;

/* The storage one call runs over (every pointer is the one storage). */
typedef struct {
    uint8_t *settings;          /* D_00810118, 0x10 bytes */
    uint32_t task_address;      /* *0x70003B6C: the running task's record */
    uint8_t *task;              /* its bytes +8..+0x1F (24) */
    uint8_t *mc;                /* D_00810040, 0xD4 bytes */
    uint8_t *progress;          /* D_00810700, the whole 0x640-byte block */
    EmMessageBlock *message;    /* D_002821B0 */
    uint8_t *busy;              /* D_00275BD8 */
    uint16_t *masks;            /* 0x70003B74..0x70003B83 */
    int16_t *offset;            /* 0x70003B94, 0x70003B96 (adjacent) */
    uint8_t read_phase;         /* D_00282157 */
    int16_t fade;               /* D_0028A9A0 */
    uint16_t held, pressed, repeat; /* D_00810E70 / 74 / 78 */
    uint16_t pad_mode;          /* D_00810E6A */
    uint8_t pad_phase;          /* D_00810E50 */
    uint8_t spad3B90, spad3B93;
} EmOptionsFrame;

/* Over the status pages' GS memory (em_status_pages_live_gs). NULL when the
 * data the binding reads (the .data windows of the EMSP) is missing. */
EmOptionsLive *em_options_live_create(EmGsTexture *gs, const EmOptionsHost *host);
void em_options_live_free(EmOptionsLive *live);

/* One call of 0022A650 (the options screen) / 00225AC0(mode) (the card
 * screen alone, the title's load entry) / 001AF1C0 / 001AF150. 0 and the
 * original's result, or -1 on a fault (reported). Each call starts a new
 * draw stream. */
int em_options_live_0022A650(EmOptionsLive *live, const EmOptionsFrame *frame, uint32_t *result);
int em_options_live_00225AC0(EmOptionsLive *live, const EmOptionsFrame *frame, uint32_t mode,
                             uint32_t *result);
int em_options_live_001AF1C0(EmOptionsLive *live, const EmOptionsFrame *frame);
int em_options_live_001AF150(EmOptionsLive *live, const EmOptionsFrame *frame);
/* The last call's draw stream. 1 drawn. */
int em_options_live_render(EmOptionsLive *live, EmGfx *gfx);
void em_options_live_deactivate(EmOptionsLive *live);
/* For tests and logs: the calls the last call made to `callee`. */
unsigned em_options_live_calls(const EmOptionsLive *live, uint32_t callee);
/* For tests: `size` bytes at `address` of the card buffers (D_00821500,
 * D_00821980, D_008219D0, D_00822010, D_00822050, D_00822690,
 * D_00275C94..9F): one process-wide copy that every binding shares and
 * that outlives em_options_live_free, as the original's one .bss copy.
 * 0, or -1 outside them. */
int em_options_live_card_bytes(const EmOptionsLive *live, uint32_t address, void *dst, uint32_t size);
/* For tests: start those buffers from a recording's bytes (the game starts
 * them zero, as the boot leaves the original's .bss). 0, or -1. */
int em_options_live_card_seed(EmOptionsLive *live, uint32_t address, const void *src, uint32_t size);
/* For tests: a log of the draw and cue calls the binding makes (NULL, the
 * default, logs nothing). Each record is taken inside a thin wrapper from
 * the very parameters it then forwards to the callee (em_page_draw_*,
 * em_message_live_cc1e0 / fc770), so it holds what the
 * callee receives. `record` is the call's values in this little-endian
 * layout (every word 4 bytes):
 *   00207D00  slot, mode
 *   00207E40  slot, x, y, w, h, rgba, then tex0 (8 bytes)
 *   00207F80  slot, x0, y0, x1, y1, rgba
 *   0020A7A0  the argument (8 bytes)
 *   001CC1E0  slot, x, y, w, h, the text (a length word, then its bytes up
 *             to the NUL), the style (a present byte, then 8 bytes: colour
 *             word, glyph, flag, pad, pad)
 *   001FC770  x, y, the text as above, the config (a present byte, then
 *             its 5 words and the style as above)
 * The cues (001FB9F0, as 0020CD40 / 60 / A0 pass them) reach the host's
 * sound callback (EmOptionsHost), where a test logs them itself. */
typedef void (*EmOptionsTrace)(void *context, uint32_t callee, const uint8_t *record, uint32_t size);
void em_options_live_set_trace(EmOptionsLive *live, EmOptionsTrace trace, void *context);

#endif /* EM_OPTIONS_LIVE_H */
