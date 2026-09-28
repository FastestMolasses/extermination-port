/* Translation of original 001D0690/001D06E0/001D0720 (every face slot
 * through em_opening_face_tick_slot: Roger's, the opening body's and the
 * player's). The face positions are the VU1 face program's
 * (em_vu1_face_morph, the one translation since chain C8b OPENING).
 * The caller supplies the original shared-RNG stream; this module does not
 * invent an opening seed or advance a private substitute RNG. */
#ifndef EM_OPENING_FACE_H
#define EM_OPENING_FACE_H
#include <stdint.h>

typedef uint32_t (*EmFaceRandom)(void *context);
typedef struct {
    float weight[8];           /* original face block +40..5f */
    int32_t blink_state, blink_wait;   /* +70/+74 */
    int32_t expression_state, expression_wait; /* +78/+7c */
    uint8_t talking, speed, reserved[2]; /* +80/+81 */
    int32_t mouth_wait, current_shape, previous_shape; /* +84..8c */
    float target[6];           /* +90..a7: element0 is unused by updater */
} EmOpeningFace;

void em_opening_face_init(EmOpeningFace *face, uint8_t speed);
/* Exact D0690 reset: current weights, wait fields and speed survive. */
void em_opening_face_reset(EmOpeningFace *face);
void em_opening_face_talk(EmOpeningFace *face, uint8_t talking);
void em_opening_face_tick(EmOpeningFace *face, EmFaceRandom random,
                          void *context);
/* 001D0720 on a face slot's bytes (the 0xD0-byte slot a record's +0x90
 * names: +0x40..+0x5F the weights, +0x70..+0xA7 the control block), one
 * em_opening_face_tick over them. */
void em_opening_face_tick_slot(uint8_t *slot, EmFaceRandom random, void *context);
#endif
