/* Global message80000018 through original FCA10/FDB80/FD790/FD950.
 * The asset retains the original timing and terminal record. */
#ifndef EM_PANEL_MESSAGE_H
#define EM_PANEL_MESSAGE_H
#include "game/em_opening_media.h"

typedef struct {
    EmOpeningLine lines[2];
    EmOpeningDialogue dialogue;
    char *text;
    uint32_t phase, delay, line_height, fill, outline, y;
} EmPanelMessage;

int em_panel_message_load(EmPanelMessage *message, const char *path);
void em_panel_message_free(EmPanelMessage *message);
int em_panel_message_start(EmPanelMessage *message, uint32_t token, uint32_t delay);
/* Run after ordinary script workers and before overlay rendering, as the
 * original shared message service. busy155/156 are the actual voice/stream
 * flags; neither a synthetic timeout nor input dismisses this message.
 * Returns1 when phase2 requests original FDB80(1)/FC9B0 cleanup, otherwise0.
 * This asset has no audio or actor-talk channels to stop on that cleanup. */
int em_panel_message_tick(EmPanelMessage *message, int busy155, int busy156);
/* Original phase2 is visible to the next script-worker tick, then cleared
 * by the following message-service tick.0 waiting,1 phase2,-1 invalid. */
int em_panel_message_done(const EmPanelMessage *message);
void em_panel_message_render(const EmPanelMessage *message, EmGfx *gfx);

#endif
