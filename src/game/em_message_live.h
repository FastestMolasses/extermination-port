/* The live message service (WP-8): original 001FCA10 at main-loop step F.
 * Docs: docs/MESSAGE_SERVICE.md ("Binding").
 *
 * One EmMessageService (em_message_service.c) is the single storage of the
 * original request block D_002821B0 and its text style D_00275C50. Its
 * workers are the translated originals: draw_line is the 001FD950 draw
 * prefix (em_message_draw_original.c), whose glyph runs are 001FC7B0 /
 * 001CC1E0 / 001CBE10 / 001CC3B0 (em_message_glyph_original.c); the packed
 * passes are drawn through the port's glyph atlas at the frame's render
 * (em_hud_glyph_strip). The shared bytes are canonical: D_00810700,
 * spad 0x70003B8F, D_008106F4/F5 and the mailbox D_008106D4[12] in
 * EmSceneState.
 *
 * Boundaries, each stated where it is bound:
 * - The stream lanes (em_stream_live, WP-8b) supply 001FD470, 001FA790,
 *   001FA5A0 (the voice ring push), 001FAAC0 on lanes 1 and 2 and the
 *   lanes' active bytes D_00282155/156 through the streams hook below;
 *   001D06E0 comes from the host hook. A reached hook that is missing
 *   faults.
 * - The mode-3 and mode-4 presenters (001FD0E0, 001FCB90, 001FCF90,
 *   001FCF60) come from the presenters hook below: em_message_presenters_live
 *   binds the translations of em_census_standins and the cue line walker
 *   001FDDB0 (em_message_presenter_rest) on this service's own
 *   EmMessageDraw (docs/CENSUS_STANDINS.md 3, docs/MESSAGE_PRESENTER_REST.md
 *   3); an unbound presenter a request reaches faults. The status pages write their mode-4
 *   requests into this block (em_status_runtime's view of D_002821B0 /
 *   B4 / B8 / D_00282240) and step F presents them, as 001FCA10 does; the
 *   port's step-F gate stand-in is deleted.
 *
 * Data: assets/message/message_data.emmd and, next to it,
 * message_presenters.emmp (tools/export_message_data.py, from the user's
 * ELF and disc). A request without that data faults. */
#ifndef EM_MESSAGE_LIVE_H
#define EM_MESSAGE_LIVE_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_frame.h"
#include "game/em_message_draw_original.h"
#include "game/em_message_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *context;
    /* 001D06E0(&D_008102B0, on): the player face (game mode 2, slot 0). */
    int (*face_talk)(void *context, int on);
} EmMessageLiveHost;

/* The stream lanes' side of the service (em_stream_live, WP-8b). The
 * workers return 1 ok, 0 fault; a NULL worker faults when it is reached.
 * `active` reads the lanes' active byte D_00282154 + lane (lb): the service
 * reads D_00282155 / D_00282156 (lanes 1 and 2) before every tick; NULL
 * reads 0 (no voice lane exists). */
typedef struct {
    void *context;
    int (*stream_stop)(void *context, int32_t mask);          /* 001FD470 */
    int (*stream_play)(void *context, int lane, int32_t cue); /* 001FA790 */
    int (*voice_push)(void *context, int32_t cue);            /* 001FA5A0 */
    int (*stop_lane)(void *context, int lane);                /* 001FAAC0 */
    int8_t (*active)(void *context, int lane);                /* D_00282154 + lane */
} EmMessageLiveStreams;

/* The mode-3 / mode-4 presenters. Each returns 1 ok, 0 fault; a NULL one
 * faults when a request reaches it. */
typedef struct {
    void *context;
    int (*mode3_present)(void *context, EmMessageBlock *block);                /* 001FD0E0 */
    int (*help_draw)(void *context, int x, int y, int32_t group, uint32_t line); /* 001FCB90 */
    int (*record_setup)(void *context, uint32_t line, int32_t page, int32_t group,
                        int32_t *result);                                      /* 001FCF90 */
    int (*record_draw)(void *context, uint32_t line, int x, int y);            /* 001FCF60 */
} EmMessageLivePresenters;

/* Loads the data and installs the step-F service (em_frame). 1 ok; 0 when
 * the data is missing or malformed: the service is still installed, idles
 * while the block is idle and faults on the first request or reset. */
int em_message_live_install(const char *path);
void em_message_live_shutdown(void);

void em_message_live_set_host(const EmMessageLiveHost *host);       /* NULL clears */
void em_message_live_set_streams(const EmMessageLiveStreams *streams);
void em_message_live_set_presenters(const EmMessageLivePresenters *presenters); /* NULL clears */
/* The service's own draw module (001FE070's layout over the loaded banks and
 * styles), for the presenters' binder; NULL before a successful install. */
EmMessageDraw *em_message_live_draw(void);

/* 001B7D60 (op0C) on the live block; the original 0/1, or -1 on a fault. */
int em_message_live_op0c(uint8_t *handshake, const unsigned char *record);
/* 001B7D60 case 0 from a caller that holds the line and delay rather than
 * a script record (the panel and terminal hooks): posts on a fresh
 * handshake. 0 ok, -1 fault. */
int em_message_live_post(uint32_t line, int32_t delay);
/* 001FD4C0(line): 1 started, 0 no stream row, -1 fault. */
int em_message_live_stream_request(int32_t line);
/* The cue of the stream-table row (area, line) of D_0026EC60, or -1. */
int32_t em_message_live_stream_cue(int32_t area, int32_t line);
/* 001FCB90(x, y, group, line) on the live presenters, for a caller that
 * presents directly (002149F0's 001FCF10). Its glyph passes are drawn at
 * this frame's step-F render, before step F's own. 0 ok, -1 fault. */
int em_message_live_help_draw(int32_t x, int32_t y, int32_t group, int32_t line);
/* 001FCF60(line, x, y), the record title presenter, on the live
 * presenters (the DATABASE category list 00213A00 calls it directly), and
 * 001FE070(bank, index, x, y) on the service's draw module over a bank the
 * caller holds (001FCF30's sub-bank of the record container). Their glyph
 * passes are drawn at this frame's step-F render. 0 ok, -1 fault; *result
 * is 001FE070's return. */
int em_message_live_record_draw(int32_t line, int32_t x, int32_t y);
int em_message_live_fe070(const EmMessageBank *bank, int32_t index, int32_t x, int32_t y,
                          int32_t *result);
/* 001CC170(text) and 001CC1E0(slot, x, y, unused, h, text, style) on the
 * service's draw and glyph modules, for a caller outside step F that draws
 * its own text (the area-title node 001C5930, em_area_title): `text`
 * points at `avail` readable bytes holding its NUL; style NULL is the
 * original's 0. 001CC1E0's glyph passes are drawn at this frame's step-F
 * render, before step F's own (the task's packets precede step F's).
 * 0 ok, -1 fault. */
int em_message_live_cc170(const uint8_t *text, uint32_t avail, int32_t *width);
int em_message_live_cc1e0(int32_t slot, int32_t x, int32_t y, int32_t unused, int32_t h, const uint8_t *text,
                          uint32_t avail, const EmMessageTextStyle *style);
/* 001FC9B0. 0 ok, -1 when the data is missing. */
int em_message_live_reset(void);
/* The block itself (D_002821B0): callers that store its words directly
 * (001B82D0 op4/op6 and 001B6BF0 store D_002821B4 = 2) and the logs. NULL
 * before install. */
EmMessageBlock *em_message_live_block(void);
/* The latched fault text, or NULL. */
const char *em_message_live_fault(void);

/* Step F (the frame service's tick) and the render of this frame's glyph
 * passes, exposed for fixtures. */
int em_message_live_tick(void);
void em_message_live_render(EmGfx *gfx);

#ifdef __cplusplus
}
#endif

#endif
