/* Original owner 00156620: the AREA11 records area11[14]/[15] (model byte
 * 0x18). HP 1 fixture: any nonzero +0x36 breaks it. Model 0x18/0x2A spawns
 * FX 0x80000013 + 0x8000001C, sound 0x1A1 and is freed two ticks later (no
 * debris flight). Models 0xA/0xC (never placed in AREA11) take the flight
 * arm: heading/speed/lift from D_00246A00/D_00246A10, 0.06/frame lift decay
 * clamped at -4, and burst again on landing or on a second hit.
 *
 * State 1's player-distance test (dist^2 <= 50*50) is a visibility
 * override: inside it the owner forces +0x01 = 1 and pushes itself on the
 * class-4 list (001B1D20 -> D_00275B80); outside it calls 001B17A0, which
 * writes +0x01 from 001B1630 and, when visible, publishes through 001B1B70
 * (class 4 -> the same 001B1D20). Auto-aim 00199220 walks D_00275B8C, the
 * published class-2/0xA list (001AAD00), so this owner is never an
 * auto-aim candidate either way. */
#ifndef EM_DRUM_ORIGINAL_H
#define EM_DRUM_ORIGINAL_H

#include <stdint.h>

#include "game/em_crate_original.h"

typedef struct {
    uint8_t status;          /* +0x00 */
    uint8_t visible;         /* +0x01 */
    uint8_t model;           /* +0x03 */
    uint8_t state;           /* +0x04 0 init,1 armed,2 break,3 free */
    uint8_t phase;           /* +0x05 */
    int16_t timer;           /* +0x28 */
    int16_t health;          /* +0x34 */
    int16_t damage;          /* +0x36 */
    float speed;             /* +0x38 */
    float position[4];       /* +0xB0 */
    float rotation[4];       /* +0xC0 */
    float world[16];         /* +0xD0, written by the place worker */
    float origin[4];         /* +0x200 INIT copy of +0xB0 */
    float origin_rotation[4];/* +0x210 INIT copy of +0xC0 */
    float heading;           /* +0x264 */
    float lift;              /* +0x268 */
} EmDrumOriginal;

typedef struct {
    uint8_t area;            /* D_00810700 */
    float player[4];         /* D_00810350 */
    int32_t frame;           /* 0x70003B68 */
    int16_t dispatch_index;  /* 0x70003B8A, 001AFD70's 1-based node count */
    float speed_table[4];    /* D_00246A00 */
    float lift_table[4];     /* D_00246A10 */
} EmDrumInput;

/* Every worker returns >=0 on success and <0 to fault the owner. */
typedef struct {
    void *context;
    int (*allocate_model)(void *);          /* 001B0EA0: 1 not ready, 0 ready */
    int (*bone_init)(void *);               /* bone_init_default_1(self) */
    int (*place)(void *, float world[16]);  /* 001C6380(self) */
    /* 0019A570(from, to, mask, exclusion): returns the hit result. */
    int (*segment)(void *, const float from[3], const float to[3], int32_t mask,
                   int32_t exclusion);
    int (*effect_matrix)(void *, int32_t preset, const float matrix[16]); /* 001F0460 */
    int (*draw)(void *);                    /* actor +0x4C */
    int (*contact)(void *);                 /* 001B1D20(self) */
    int (*visibility)(void *, uint8_t *visible); /* 001B17A0(self) writes +1 */
    int (*effect)(void *, uint32_t id, const float point[4]); /* 001EFD20 */
    int (*sound)(void *, uint16_t id);      /* 001FC580(self, id) */
    int (*random)(void *, uint32_t *);      /* 00122BB8 */
    int (*sweep)(void *, const float point[3], uint32_t mode); /* 0019AD00(self, point, mode) */
    int (*probe)(void *, float position[4], const float from[3], float dy,
                 uint32_t mode, EmCrateProbe *); /* 0019AB20 */
    int (*sound3d)(void *, uint16_t id, int32_t mode, float radius); /* 001FBD50 */
    int (*hull)(void *, const float world[16]); /* 001A2370(self, self+0xD0) */
    int (*free)(void *);                    /* 001AFC10 */
} EmDrumOriginalHooks;

enum { EM_DRUM_FAULT = -1, EM_DRUM_FREED = 0, EM_DRUM_ALIVE = 1 };

/* One 00156620 call. A missing worker faults before any write. */
int em_drum_original_tick(EmDrumOriginal *, const EmDrumInput *,
                          const EmDrumOriginalHooks *);

#endif
