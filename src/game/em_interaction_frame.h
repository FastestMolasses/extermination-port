/* Original 001B82D0 sub0/sub2/sub4/sub13 shared by ordinary interaction scripts.
 * Pickup sub13 uses selector1/camera_top2 without entering letterbox bars
 * or clearing the activity array; door sub0 uses selector2/camera_top2.
 * Both wait for the real player and preserve those presentation fields. */
#ifndef EM_INTERACTION_FRAME_H
#define EM_INTERACTION_FRAME_H
#include "game/em_script.h"

/* A per-call view: the hosts load it from the bytes' canonical storage
 * before a command and store it back after (em_area11_interaction_host.c
 * view_load/view_store, em_area_script.c frame_load/frame_store). */
typedef struct {
    uint8_t selector;       /* scratch3B8D */
    uint8_t player_ready;   /* scratch3B8F, owned by the player worker */
    uint8_t ready;          /* scratch3B92 */
    uint8_t camera_phase;   /* D8101E1 */
    uint8_t camera_state;   /* D8101E2 */
    uint8_t camera_swing;   /* D8101E3 */
    uint8_t camera_top;     /* D8101E4 */
    uint8_t camera_mode;    /* D8101E6 */
    uint8_t recovery_lock;  /* D8106EF */
    uint8_t auxiliary;      /* D8106F3 */
    uint8_t activity[12];   /* D8106D4..DF, cleared by001BA510 */
    uint16_t counter;      /* scratch3B84 */
    int32_t message_phase; /* D2821B4 */
    float zoom, up[4];     /* render projection, D8105F0..FC */
} EmInteractionFrame;

typedef enum {
    EM_INTERACTION_BARS_ENTER,      /*001AEB60(4) */
    EM_INTERACTION_SCOPE_ZOOM_ZERO, /*001D2610(0.0f) */
    EM_INTERACTION_BARS_LEAVE,      /*001AEBA0(4) */
    EM_INTERACTION_RELEASE_SKELETON,/*001CA770(player) */
    EM_INTERACTION_ZOOM_DEFAULT,    /*001D25F0(480.0f) */
    EM_INTERACTION_RESUME_MUSIC,    /*001FAE70(0) */
    EM_INTERACTION_FADE_IN          /*001AEE10(4,0) */
} EmInteractionFrameEvent;

/* Return1 only when the side effect was accepted. Missing/failed hooks fault
 * the command. Caller supplies live shared state before every invocation;
 * the helper does not create a player-ready signal or tick a fade itself. */
typedef int (*EmInteractionFrameEmit)(void *,EmInteractionFrameEvent);
EmScriptCommandResult em_interaction_frame_command(EmInteractionFrame *state,
    EmScript *script, unsigned subcommand, int immediate,
    EmInteractionFrameEmit emit, void *context);

#endif
