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
 *   00114988 / 00114848   em_memcard (the card I/O boundary)
 *   001D2830, 00200970, 001AEE10 / 001AEDE0, 001FBC50, 001FABB0, 001FF080,
 *   001B61C0              the host's workers
 *   002267A0 / 00227300   fault: the card screens past the first level's
 *                         recordings (the slot's directory read, the load)
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
    uint8_t *progress;          /* D_00810700, 0x640 bytes (the canonical ranges) */
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

#endif /* EM_OPTIONS_LIVE_H */
