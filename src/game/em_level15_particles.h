/* Level-15 boot routines omitted by the earlier delta inventory. UNWIRED.
 * Original pointers are 32-bit; bytes returns a borrowed view consumed before
 * the next access or call. Workers succeed at >=0 and fail at <0. A latched
 * fault prevents all later observable accesses/calls. No owned game data.
 * Evidence: tools/test_level15_particles_reference.py. */
#ifndef EM_LEVEL15_PARTICLES_H
#define EM_LEVEL15_PARTICLES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t address; int32_t code; } EmLevel15ParticlesFault;
enum { EM_LEVEL15_PARTICLES_FAULT_NONE = 0, EM_LEVEL15_PARTICLES_FAULT_NULL_WORKER = 1,
       EM_LEVEL15_PARTICLES_FAULT_WORKER_FAILED = 2, EM_LEVEL15_PARTICLES_FAULT_BAD_ADDRESS = 5 };
typedef struct {
    void *ctx;
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
/* BEGIN GENERATED HOOKS */
    int (*w_00102918)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_001CCF70)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001CFAE0)(void *ctx, uint32_t a0, int32_t a1, uint32_t a2, float a3, float a4, float a5, float a6);
    int (*w_001CFBE0)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, int32_t a4);
/* END GENERATED HOOKS */
} EmLevel15ParticlesHooks;
/* entry_sp locates the borrowed descriptor at entry_sp-0x60. For kinds
 * outside 0..6 the original retains the caller's incoming count (s1).
 * Valid kinds replace it. A live binding must supply that context exactly. */
int em_level15_particles_0015A750(const EmLevel15ParticlesHooks *h, uint32_t actor,
    uint32_t entry_sp, int32_t incoming_count, EmLevel15ParticlesFault *fault);
#ifdef __cplusplus
}
#endif
#endif
