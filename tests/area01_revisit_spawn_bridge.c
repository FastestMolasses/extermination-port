/* Test bridge: em_area01_revisit_spawn's two routines over a test host. */
#include "game/em_area01_revisit_spawn.h"
int rvs_001C8140(const EmArea01RuntimeHost *h, uint32_t sp, uint32_t bank, int16_t index, uint32_t behaviour,
                 uint64_t *v0, uint32_t *fault)
{ return em_area01_revisit_spawn_001C8140(h, sp, bank, index, behaviour, v0, fault); }
int rvs_001C7EB0(const EmArea01RuntimeHost *h, uint32_t sp, uint32_t self, uint32_t *fault)
{ return em_area01_revisit_spawn_001C7EB0(h, sp, self, fault); }
