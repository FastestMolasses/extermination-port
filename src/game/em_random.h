#ifndef EM_RANDOM_H
#define EM_RANDOM_H

#include <stdint.h>

/* Original SDK state at (*D_0024295C)+0x58. The full 32-bit state is
 * retained; func_00122BB8 returns only its low 31 bits. */
uint32_t em_random_step(uint32_t *state);
void em_random_seed(uint32_t seed);
uint32_t em_random_next(void);

#endif
