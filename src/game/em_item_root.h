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

/* The ITEM root's draw, 0020F170 and 0020F2A0 (decomp src/func_0020F170.c
 * and src/func_0020F2A0.c, both byte-matched C): their leaf calls, in the
 * original's order, with the original's arguments. 0020F2A0 is entered
 * after its 0020D930 (em_item_root_hover, the caller's): it stores 248.0 /
 * 208.0 at D_700038A0 / D_700038A4 and calls 0020AC70(self, &D_700038A0,
 * 0) (`trail` with those two floats and the reset flag 0), then the
 * blend / sprite calls of its five regions (region N lit when hover == N).
 * Each worker returns 1; anything else stops the draw (0). */
typedef struct {
    void *context;
    int (*background)(void *, uint64_t tex0);          /* 0020A7A0(tex0) */
    int (*blend)(void *, int32_t slot, int32_t mode);  /* 00207D00 */
    int (*sprite)(void *, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba,
                  uint64_t tex0);                      /* 00207E40 */
    int (*trail)(void *, float x, float y, int32_t reset); /* 0020AC70 */
} EmItemRootLeaves;
int em_item_root_draw_0020F170(const EmItemRootLeaves *leaves);
int em_item_root_draw_0020F2A0(const EmItemRootLeaves *leaves, uint8_t hover);

#endif
