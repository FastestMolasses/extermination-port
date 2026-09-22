/* Original ITEM page dispatcher0020EE50 and its D930 selection table1.
 * Asset loading, page drawing and child pages remain explicit workers. */
#ifndef EM_ITEM_ROOT_H
#define EM_ITEM_ROOT_H

#include <stdint.h>

typedef struct {
    uint8_t state, step, next_state;         /* UI+4/+5/+6 */
    uint8_t screen, hover, selected, module; /* UI+10/+11/+15/+16 */
    uint8_t asset_busy;                      /* D275BD8; cleared only by actual asset completion */
    uint32_t message_mode, message_phase, message_line, message_group;
} EmItemRoot;

typedef enum {
    EM_ITEM_RESET_INPUT,     /*0020E020 */
    EM_ITEM_DRAW_BACKGROUND, /*0020F170 */
    EM_ITEM_DRAW_ROOT,       /*0020F2A0, including hover update */
    EM_ITEM_SOUND_BACK,      /*0020CD60 */
    EM_ITEM_SOUND_ACCEPT,    /*0020CD40 */
    EM_ITEM_LOAD_MODULE,     /*001FF080(0,module) */
    EM_ITEM_CHILD_PAGE       /*214570/2149F0/215870/2160B0, argument state4..7 */
} EmItemRootEvent;

/* Return1 on success, otherwise the caller must retain status ownership and
 * report failure. The draw worker sets hover using the current normalized
 * stick. A child worker can change these fields through the supplied state. */
typedef int (*EmItemRootWorker)(void *context, EmItemRoot *state, EmItemRootEvent event,
                                unsigned argument);

int em_item_root_tick(EmItemRoot *state, unsigned buttons, EmItemRootWorker worker, void *context);

/* Input is the actual001B62C0 output magnitude/angle, not raw stick axes.
 * The original converts magnitude to double before comparing with0.8.
 * Returns1 only when the nonzero selection changes (sound cue5). Releasing
 * the stick resets hover without a sound. */
int em_item_root_hover(EmItemRoot *state, float magnitude, float angle);

#endif
