#ifndef EM_INTERACTION_CINEMATIC_H
#define EM_INTERACTION_CINEMATIC_H
#include "game/em_interaction_frame.h"

/* Original 001B82D0/sub9..12. These are live service observations, not a
 * local countdown. A stream is ready only when its real worker reports1. */
typedef struct {
    int16_t fade_phase; /* D0028A9A0 */
    uint8_t stream_ready; /* D008106F4 */
} EmInteractionCinematicInputs;

typedef enum {
    EM_CINEMATIC_FADE_OUT_FOUR, /*001AEDE0(4,0) */
    EM_CINEMATIC_REQUEST_STREAM, /*001FD4C0(record+18) */
    EM_CINEMATIC_MUTE_CHANNEL, /*00119828(channel,0,0), channels0/1 */
    EM_CINEMATIC_BARS_IMMEDIATE, /*001AEB60(255) */
    EM_CINEMATIC_ATTACH_PLAYER, /*001B81D0(player): face object; publishes ready2 */
    EM_CINEMATIC_SCOPE_ZERO, /*001D2610(0) */
    EM_CINEMATIC_FADE_IN_SIXTEEN /*001AEE10(16,0) */
} EmInteractionCinematicEvent;

/* Return1 only when the actual worker accepts the operation. Attaching the
 * player's face object must also publish its original shared writes;
 * this command cannot invent the player-ready signal. */
typedef int (*EmInteractionCinematicEmit)(void *,EmInteractionCinematicEvent,int32_t argument);
EmScriptCommandResult em_interaction_cinematic_command(EmInteractionFrame *,
    EmScript *,unsigned subcommand,int immediate,int32_t stream,
    const EmInteractionCinematicInputs *,EmInteractionCinematicEmit,void *);
#endif
