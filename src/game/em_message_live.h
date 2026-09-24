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
 * - The voice lanes (001F9CF0, WP-8's stream half) are not live: no voice
 *   is ever started, so D_00282155/156 read 0, 001FAAC0 on lanes 1 and 2
 *   has nothing to release, and a voice push (001FA5A0) faults.
 * - 001FD470 / 001FA790 / 001D06E0 come from the binder (hooks below);
 *   a reached hook that is missing faults.
 * - The mode-3 and mode-4 presenters (001FD0E0, 001FCB90, 001FCF90,
 *   001FCF60) are not translated: reaching them faults. While a status page
 *   runs, the page layer presents its own mode-4 lines and the binder's gate
 *   holds the service (see the gate hook). The gate is a port stand-in (the
 *   original 001FCA10 has none) that goes when those presenters are.
 *
 * Data: assets/message/message_data.emmd (tools/export_message_data.py,
 * from the user's ELF and disc). A request without that data faults. */
#ifndef EM_MESSAGE_LIVE_H
#define EM_MESSAGE_LIVE_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_frame.h"
#include "game/em_message_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *context;
    /* 001D06E0(&D_008102B0, on): the player face (game mode 2, slot 0). */
    int (*face_talk)(void *context, int on);
    /* PORT STAND-IN (no original counterpart; see the header comment):
     * 1 when step F runs the service this frame, 0 when a status page
     * presents instead; -1 faults. NULL = 1 (the original's behaviour). */
    int (*gate)(void *context);
} EmMessageLiveHost;

typedef struct {
    void *context;
    int (*stream_stop)(void *context, int32_t mask);          /* 001FD470 */
    int (*stream_play)(void *context, int lane, int32_t cue); /* 001FA790 */
} EmMessageLiveStreams;

/* Loads the data and installs the step-F service (em_frame). 1 ok; 0 when
 * the data is missing or malformed: the service is still installed, idles
 * while the block is idle and faults on the first request or reset. */
int em_message_live_install(const char *path);
void em_message_live_shutdown(void);

void em_message_live_set_host(const EmMessageLiveHost *host);       /* NULL clears */
void em_message_live_set_streams(const EmMessageLiveStreams *streams);

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
