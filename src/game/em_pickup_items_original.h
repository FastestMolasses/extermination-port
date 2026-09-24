/* em_pickup_items_original.h - original pickup workers the AREA11 item
 * owners reach, translated from the pinned SCUS-97112 boot ELF:
 *
 *   001C40B0  the inventory worker (001C47A0's first call): one switch on
 *             the item type that adds to the D_00810C64 count byte and the
 *             meters it feeds (NEARMISS C; the .s was followed: every clamp
 *             compares the value reloaded after its store, so a count byte
 *             wraps before the 99 clamp is tested)
 *   001F1110  the class-7 aura's initializer (0015AC00 state 0): one SDK
 *             rand() for its first countdown
 *   001F1180  the class-7 aura step (0015AE20's tail while D_70003B92 == 0):
 *             the countdown, rand() for the sprite record and the next
 *             countdown, the camera-facing test and the angle ramp. The
 *             readable C is NEARMISS with the variant test inverted; the .s
 *             is followed (variants 4, 2 and 1 run the facing test).
 *
 * Only original bytes these routines read or write are touched. 001C40B0
 * addresses its bytes by original address through the caller's resolver, so
 * bytes that alias in the original (the count array D_00810C64 overlaps the
 * meters D_00810CA8..CB7 and the map/key arrays D_00810CB8/CC3) alias here
 * exactly when the resolver maps them to the same storage. A resolver that
 * has no storage for an address returns NULL: the routine then stops and
 * returns -1 (fail-stop) after the stores that preceded that access.
 *
 * Every float operation goes through game/em_ee_float.h (docs/EE_FLOAT_MODEL.md);
 * the aura's float fields cross the API as raw bit patterns.
 *
 * Verified by tools/test_pickup_items_reference.py, which executes the
 * original instructions and compares every byte and worker call.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_PICKUP_ITEMS_ORIGINAL_H
#define EM_PICKUP_ITEMS_ORIGINAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The storage of original byte `address` (size 1 or 2 bytes, little-endian
 * like the EE), or NULL when the caller holds none. */
typedef uint8_t *(*EmPickupItemsAt)(void *ctx, uint32_t address, uint32_t size);

/* 001C40B0(a0, a1): returns 0 (the original's return value), or -1 when a
 * reached byte has no storage. a0 and a1 are the full argument registers
 * (the count byte is D_00810C64 + a0; a1 is added in 32 bits and stored in
 * the byte/halfword width of the destination). */
int em_pickup_items_001C40B0(EmPickupItemsAt at, void *ctx, int32_t a0, int32_t a1);

/* The aura block at owner +0x2D0 (001F1110 writes it, 001F1180 steps it). */
typedef struct {
    uint32_t angle;   /* +0x2D0: float bits, the sprite angle in degrees */
    uint32_t timer;   /* +0x2D4: float bits, the countdown / sprite phase */
    int16_t variant;  /* +0x2D8 */
    int16_t index;    /* +0x2DA: the sprite record index (rand() % 4) */
    int32_t state;    /* +0x2DC: 0 counting down, 1 showing */
} EmPickupAura;

typedef struct {
    void *ctx;
    /* 00122BB8: SDK rand() (the low 31 bits of the shared LCG step). */
    int32_t (*w_00122BB8)(void *ctx);
    /* The draw block of 001F1180 (0x1F136C..0x1F148C): 0011E2A8(pi * timer),
     * the colour and size words of the sprite record, 001026A0(record
     * +0x18, owner +0xD0) and 001F0A60. `record` is the original address of
     * the D_00259DD0/D_00259EE0/D_00259F90/D_0025A040 entry the step
     * selected; `angle` and `timer` are the block's values before this
     * step's ramps. Returns >= 0; a negative result faults. */
    int (*w_draw)(void *ctx, uint32_t record, uint32_t angle, uint32_t timer);
} EmPickupAuraWorkers;

/* 001F1110(owner, variant): 0, or -1 when rand() is not bound. */
int em_pickup_aura_001F1110(EmPickupAura *aura, int16_t variant, const EmPickupAuraWorkers *w);

/* 001F1180(owner). `world` is the owner's +0xD0 matrix (rows 2 and 3,
 * +0xF0 and +0x100, are read by the facing test); `eye` is D_008105D0
 * (x, y, z, w); `d810700` the area byte. Returns 0, or -1 on a fault (an
 * unbound reached worker, a negative worker result or a refused float form). */
int em_pickup_aura_001F1180(EmPickupAura *aura, const float world[16], const float eye[4],
                            uint8_t d810700, const EmPickupAuraWorkers *w);

#ifdef __cplusplus
}
#endif

#endif /* EM_PICKUP_ITEMS_ORIGINAL_H */
