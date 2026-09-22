/* Opening-only translation of original 001D0690/001D06E0/001D0720.
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
/* 0023C578..0023C5B0: seven delta products accumulate before adding base.
 * Normals are not morphed by the original kernel. Host float rounding has
 * not been asserted bit-identical to the EE/VU execution units. */
void em_opening_face_position(float out[3], const float base[3],
                               const float delta[21], const float weight[8]);
#endif
