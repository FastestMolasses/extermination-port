#include "game/em_random.h"

/* Cold-boot state: the ELF's initialized SDK word at (*D_0024295C)+0x58
 * (0x002426C8) is 1. The only seeding call, func_00122BA8(0x45), is in
 * anim_frame_top_a (0x001ACA20) state 4 sub 0, i.e. attract-demo start;
 * no overlay calls it. Cold boot -> New Game therefore starts from 1.
 * Call ordering from other, still untranslated systems remains unaudited;
 * identical arithmetic alone does not give an identical whole-game stream. */
static uint32_t random_state = 1;

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
