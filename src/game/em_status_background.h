/* Original 0020A7A0: the animated background every status screen draws
 * first (the hub 0020CDC0, the ITEM root 0020F2A0 and the BATTERY page
 * 002149F0 call it with their own tile TEX0).
 *
 * Hand translation of Extermination/src/func_0020A7A0.c (NEARMISS,
 * body-correct) checked against its .s. Its state is the three 0x20-byte
 * blocks at D_002655A0, initialised once from the ELF's .data and never
 * reset by any screen, so one EmStatusBackground is shared by every page
 * (em_status_background_live). Per layer i = 0, 1, 2 (block e):
 *   pulse (layers 1 and 2): while +0xC >= 0 it counts down (bgezl); once
 *     negative, +0x8 += 0.25 until it reaches 180, then +0xC = 3 *
 *     (rand() % 60) + 60 and +0x0/+0x4/+0x8 = 0.
 *   update: layer 0 scrolls +0x0 by -0.5 (below 0 -> 255), layer 1 +0x4
 *     likewise; both set the colour (96, 96, 96, 64) and scale +0x1C by
 *     sin(pi * +0x8 / 180). Layer 2, only while +0xC < 0, adds 0.3 to
 *     +0x0 and +0x4 and sets the colour the same way.
 *   draw: 00207D00(1, 0) once, before the layers. Layers 0 and 1 emit a
 *     32 x 8 grid of 00207E40(1, x, y, 0x100, 0x80, rgba, tex0) sprites,
 *     rows y = (0x400..0xBC0 step 0x40 + (int)+0x4) << 4, columns x =
 *     (0x400..0xB00 step 0x100 + (int)+0x0) << 4; layer 2 (while +0xC <
 *     0) one sprite at ((int)(16 * (1792 - +0x0)), (int)(16 * (1936 +
 *     -+0x4 / 2))), size ((int)(2 * +0x0) + 0x200, (int)(2 * +0x4) +
 *     0x1C0). rgba packs the four colour floats through 00128250.
 * Arithmetic is the EE FPU's (em_pose_math.h: add/sub/mul truncated with
 * the add pre-trim, div.s nearest); rand() and sin (0011E2A8) are
 * workers (live: em_random_next and em_sdk_math_original's sinf). float_to_int (001281C0) and 00128250 truncate; the values here
 * are finite and non-negative for 00128250, anything else faults.
 * Verified by tools/test_status_background_reference.py (make
 * test-status-background-reference), which executes the original. */
#ifndef EM_STATUS_BACKGROUND_H
#define EM_STATUS_BACKGROUND_H

#include <stdint.h>

typedef struct {
    float x, y;    /* +0x00, +0x04: scroll / burst offsets */
    float phase;   /* +0x08: degrees */
    int32_t timer; /* +0x0C: pulse countdown, active while negative */
    float rgba[4]; /* +0x10..+0x1C */
} EmStatusBackgroundLayer;

typedef struct {
    EmStatusBackgroundLayer layer[3]; /* D_002655A0 */
} EmStatusBackground;

typedef struct {
    void *context;
    float (*sine)(void *, float);  /* 0011E2A8 */
    int32_t (*random)(void *);     /* 00122BB8: 0..0x7FFFFFFF */
    int (*mode)(void *, int slot, int mode); /* 00207D00: always (1, 0) */
    /* 00207E40(1, x, y, w, h, rgba, tex0): x/y in GS 12.4 units, w in
     * canvas pixels, h in field lines x 2 (the caller's tex0). */
    int (*sprite)(void *, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba);
} EmStatusBackgroundWorkers;

/* The .data image of D_002655A0: {0, 0, 90, 0, 96, 96, 96, 64} twice,
 * then zeros. */
void em_status_background_init(EmStatusBackground *state);

/* One 0020A7A0 call. Every worker must return 1 (sine and random are
 * values); 1 on success, -1 on a worker failure or a value outside the
 * translated domain (the state is then partially updated, as the
 * original's would be at that call). */
int em_status_background_step(EmStatusBackground *state, const EmStatusBackgroundWorkers *workers);

/* The live call (em_status_background_draw.c): one 0020A7A0 over the one
 * shared D_002655A0 state, with the translated original sinf 0011E2A8
 * (em_sdk_math_original, over the tables em_status_background_load_sdk
 * read), the shared rand() (em_random_next) and the sprites drawn as
 * backdrop quads of the tile at (u, v, w, h) in the UI decor atlas the
 * caller has bound. 1 drawn, 0 fault (also without the tables). */
struct EmGfx;
int em_status_background_render(struct EmGfx *gfx, float u, float v, float w, float h);

/* Load the SDK tables D_0026C170..D_0026C658 that 0011E2A8 reads
 * (assets/sdk_math_tables.emsm, tools/export_sdk_math_tables.py). 1 loaded,
 * 0 missing or invalid (reported). */
int em_status_background_load_sdk(const char *path);

/* The number of live 0020A7A0 calls so far (each advances D_002655A0
 * once), for the level smoke's once-per-status-frame check. */
unsigned long em_status_background_live_steps(void);

/* The opaque black under the layers: the port's stand-in for the status
 * frame's cleared UI-camera frame (the original clear colour is still open,
 * FIRST_LEVEL_AUDIT R09). Not part of 0020A7A0. */
void em_status_background_frame(struct EmGfx *gfx);

#endif
