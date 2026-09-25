/* em_indicator_child.h - the indicator children, one pool node each
 * (docs/CENSUS_UNVERIFIED.md "001C5680 and 001C5760").
 *
 * Hand translations of (boot ELF SCUS-97112 and the AREA11 overlay):
 *   001C5680  the indicator child behaviour, models from bank D_0028A56C
 *             (pickup lights 0x73, the panel's 0x75, the husk's 0x7A)
 *   001C5760  the same behaviour, models from bank D_0028A59C (the
 *             terminal's 0x10); +0x0A != 0 re-runs 001C6380 before a draw
 *   0x827EAC..0x827FE8  the tail of the AREA11 terminal owner 00827B10 that
 *             writes its child's +0xA0 colour from its +0x28 level
 *
 * What the behaviour does, by the child's +4 byte:
 *   0      001C2360 (001C5680) or 001C22A0 (001C5760) binds the model (its
 *          +0x4C draw method becomes 001CACB0, 001CA5F0 mode 2) and the
 *          bone slots; a nonzero result leaves the child in state 0 with
 *          nothing else done. Otherwise 001C6380 places it and +4 = 1.
 *          This first frame draws nothing.
 *   1      +0x80..+0x8C = +0xA0..+0xAC; on an even 0x70003B68 frame a stack
 *          copy of that vector with w = 1.0 is made and not read again;
 *          001C5760 with +0x0A != 0 runs 001C6380; then 001F54E0(self,
 *          self + 0x80), which draws one 00122BB8 value, turns +0x80 into
 *          the flickered colour and calls the +0x4C method (the draw).
 *   other  001AFC10(self) (measured for 2, 3, 4 and 0x80).
 *
 * Every callee is a worker. Nothing here draws or allocates. */
#ifndef EM_INDICATOR_CHILD_H
#define EM_INDICATOR_CHILD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_INDICATOR_CHILD_001C5680 0x001C5680u
#define EM_INDICATOR_CHILD_001C5760 0x001C5760u
/* 001CA5F0 mode 2: the +0x4C method every indicator child gets. */
#define EM_INDICATOR_CHILD_DRAW_001CACB0 0x001CACB0u

typedef struct {
    void *ctx;
    /* 001C2360 / 001C22A0 (fn names which): *result = its v0 */
    int (*init)(void *ctx, uint32_t fn, int32_t *result);
    /* 001C6380(self) */
    int (*place)(void *ctx);
    /* 001F54E0(self, self + 0x80): c80 is the child's +0x80 vector, read
     * and written in place */
    int (*color)(void *ctx, float c80[4]);
    /* 001AFC10(self) */
    int (*free_self)(void *ctx);
} EmIndicatorChildWorkers;

typedef struct {
    uint8_t *status;  /* +0x04 */
    uint8_t alt;      /* +0x0A */
    const float *a0;  /* +0xA0..+0xAC, the owner's colour vector */
    float *c80;       /* +0x80..+0x8C */
} EmIndicatorChildRecord;

typedef enum {
    EM_INDICATOR_CHILD_WAITING = 0, /* state 0 with the init refused */
    EM_INDICATOR_CHILD_PLACED = 1,  /* state 0 done: +4 = 1, nothing drawn */
    EM_INDICATOR_CHILD_DRAWN = 2,   /* state 1: 001F54E0 ran */
    EM_INDICATOR_CHILD_FREED = 3    /* 001AFC10 ran */
} EmIndicatorChildResult;

/* One behaviour call. callback is 001C5680 or 001C5760. Returns an
 * EmIndicatorChildResult, or -1 when a worker is missing (nothing ran) or
 * a worker failed (the calls before it stay). */
int em_indicator_child_step(uint32_t callback, EmIndicatorChildRecord *r,
                            const EmIndicatorChildWorkers *w);

/* 00827B10's child colour (0x827EAC..0x827FE8), after its 001B17A0
 * publication and its +0x28 level step (em_elevator_tick, the one
 * translation of that step). A nonzero level writes 0x70003A20 =
 * level / 128.0 (cvt.s.w, div.s) and the child's +0xA0 = (0, level / 128.0,
 * 0, 0.25); level 0 writes (1.0, 0, 0, 0.25) and leaves 0x70003A20 alone.
 * child_a0 NULL (the owner's +0x2E4 is 0 when its child alloc was refused:
 * the original would store through address 0) returns -1 with nothing
 * written. */
int em_indicator_00827B10_colour(int16_t level, float child_a0[4], uint32_t *spad3A20);

#ifdef __cplusplus
}
#endif

#endif /* EM_INDICATOR_CHILD_H */
