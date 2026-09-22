#include "game/em_random.h"

/* anim_frame_top_a seeds 0x45 through func_00122BA8 during boot.
 * Call ordering from other, still untranslated systems remains unaudited;
 * identical arithmetic alone does not give an identical whole-game stream. */
static uint32_t random_state = 0x45;

uint32_t em_random_step(uint32_t *state)
{
    /* Unsigned arithmetic reproduces the original MULT/ADDIU wraparound
     * without the signed-overflow undefined behavior of the decompiled C. */
    *state = *state * UINT32_C(0x41C64E6D) + UINT32_C(0x3039);
    return *state & UINT32_C(0x7FFFFFFF);
}

void em_random_seed(uint32_t seed)
{
    random_state = seed;
}

uint32_t em_random_next(void)
{
    return em_random_step(&random_state);
}
