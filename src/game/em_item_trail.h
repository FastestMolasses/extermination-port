/* Original1B62C0 stick normalization and20AC70/1D66A0 ITEM glow geometry. */
#ifndef EM_ITEM_TRAIL_H
#define EM_ITEM_TRAIL_H

#include <stdint.h>

typedef struct {
    void *context;
    float (*sine)(void *, float);
    float (*cosine)(void *, float);
    float (*atan2)(void *, float y, float x);
    float (*sqrt)(void *, float);
} EmItemMath;

typedef struct {
    float x, y, magnitude, angle;
} EmItemStick;

typedef struct {
    EmItemStick slots[16];
    uint32_t cursor;
} EmItemTrail;

/* Transcendentals are required explicit workers, not claimed EE operations. */
int em_item_stick_sample(EmItemStick *stick, uint8_t x, uint8_t y, const EmItemMath *math);
void em_item_trail_reset(EmItemTrail *trail);

/* Original fixed16 GS XY, center then two outer vertices. The untextured
 * center RGB is intensity/255 and the outer RGB is zero. Original PRIM4 emits a strip
 * with alternating center/outer vertices: its32 visible triangles form a
 * Gouraud fan. The ignored alpha values have no effect in blend mode1.
 * Return1 when accepted; other results abort submission with a fault. */
typedef int (*EmItemTrailEmit)(void *context, const int32_t xy[3][2], unsigned intensity);

/* One original callback advances the16-entry ring and emits512 triangles.
 * Its base is the actual F2A0 command, normally(248,208). Input sampling is
 * performed separately so the hover and trail consume the same pad bytes. */
int em_item_trail_step(EmItemTrail *trail, const EmItemStick *stick, float x, float y,
                       const EmItemMath *math, EmItemTrailEmit emit, void *context);

/* 0020AC70's 00208AB0 calls (001D66A0(1, pos, colour, D_00273580, angle)),
 * for the GS packet builder (em_uip_001D66A0; docs/STATUS_FIELD.md): pos
 * the vector 0020AC70 builds ({x + 0.8 (44 x_i), y + 0.5 (44 y_i), 28 +
 * 16 z_i, 28}), colour the int it stores three times (float_to_int(16 (1 +
 * z_i))), angle the slot's (f12), trig the four sin / cos worker results
 * 001D66A0 computes first (sin angle, cos angle, sin pi/32, cos pi/32), the
 * same values the fan's triangles are computed from. Called once per fan,
 * before its triangles. Return 1, or anything else to abort. */
typedef int (*EmItemTrailFan)(void *context, const float pos[4], int32_t colour, float angle,
                              const float trig[4]);
/* em_item_trail_step with a fan sink (NULL: none). */
int em_item_trail_step_fans(EmItemTrail *trail, const EmItemStick *stick, float x, float y,
                            const EmItemMath *math, EmItemTrailEmit emit, EmItemTrailFan fan,
                            void *context);

#endif
