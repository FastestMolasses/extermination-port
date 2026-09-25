/* AREA11 placement record 12: the class-9 "director" manager, overlay
 * callback 0x008253F0 (FIRST_LEVEL_ROUTE.md sections 4-5, beats 10, 11, 13;
 * FIRST_LEVEL_AUDIT H10). Standalone hand translation; see
 * docs/DIRECTOR_ORIGINAL.md.
 *
 * Read from the AREA11 overlay (Extermination build/overlays/AREA11 splat
 * listing: func_overlay_AREA11_008253B0, _008254C0/_00825500 (one function
 * split by splat), _008255C0/_00825600 and _00825690/_008256D0; splat labels
 * are 0x40 below the runtime addresses that `jal` encodes, and every address
 * below is a runtime address) and from the main ELF:
 *
 *   0x8253F0  switch on node +4:
 *     0       001BA1C0(self, 0x3B), i.e. D_00810758[0x3B] (D_00810793) ==
 *             0xFF: +4 = 3 (0x825450); otherwise +0 = 1, +4 = 1
 *             (0x825458/0x825460).
 *     1       dispatch on D_00810813 (0x825468): 0 or 1 -> beat 0
 *             (0x825500), 0x10 or 0x11 -> beat 1 (0x825600), 0x20 -> beat 2
 *             (0x8256D0), any other value -> nothing.
 *     2, 3    001AFC10(self) (0x8254E4).
 *     other   returns.
 *   Each beat body switches on node +5:
 *     0       height gate on D_00810354 (player +0xA4), then
 *             001B1EA0(0, &D_00810350, quad, 4); on a nonzero result
 *             001BA1A0(self + 0x1F0, script) and +5 = 1.
 *               beat 0: Y < 260.0 rejects (c.lt.s, 0x825548) and
 *                       !(Y <= 280.0) rejects (c.le.s, 0x825564); quad
 *                       0x82ABE0; script 0x8294C0 (0x8255A0).
 *               beat 1: Y < 275.0 rejects (0x82564C); quad 0x82AC20 (not a
 *                       rectangle); script 0x829A40 (0x825688).
 *               beat 2: Y < 285.0 rejects (0x82571C); quad 0x82AC60;
 *                       script 0x829CC0 (0x825758).
 *             The comparisons are the original's: a NaN Y passes the beat-1/2
 *             gates and fails the beat-0 gate.
 *     1       001BA1F0(self); on any nonzero result (1 finished, 3 aborted by
 *             the skip path) the completion runs, then +5 = 0:
 *               beat 0: D_00810813 = 0x10 (0x8255D0), 001C4760(1, 1)
 *                       (0x8255D4), +5 = 0 (0x8255DC);
 *               beat 1: D_00810813 = 0x20 (0x8256B4);
 *               beat 2: D_00810813 = 0xFF (0x825784).
 *     other   nothing.
 *
 * 001BA1C0 (byte-matched, src/func_001BA1C0.c) and 001C4760 (byte-matched
 * with mwcc 2.3.3, src/func_001C4760.c: D_00810CC3[a0] += a1; a0 >= 0x20
 * also stores D_008106B0 = 3 then D_008106B1 = a0) are translated here.
 * 001B1EA0 mode 0 (an all-.word function in the decomp, read from its
 * instructions) and the SDK atan2f it calls (0011E620 wrapper, 0011C4C8
 * kernel, 0011DBB8 atanf; read from the instructions, which the decomp's C
 * for 0011DBB8 does not reproduce, see docs/DIRECTOR_ORIGINAL.md) are
 * translated too. 001BA1A0, 001BA1F0 (script host em_area_script) and
 * 001AFC10 (pool free) are workers. A NULL worker, a negative worker
 * result, a reached NULL storage pointer or an untranslated input faults
 * (-1, *fault_address names the function or the reading instruction);
 * nothing is substituted. Verified by tools/test_director_original_reference.py
 * (executed overlay and ELF code, and the PCSX2 route captures) and
 * tests/director_original_test.c.
 */
#ifndef EM_DIRECTOR_ORIGINAL_H
#define EM_DIRECTOR_ORIGINAL_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_scene_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_DIRECTOR_ORIGINAL_OWNER 0x008253F0u   /* node +0x10 callback */
#define EM_DIRECTOR_ORIGINAL_BEAT0 0x00825500u
#define EM_DIRECTOR_ORIGINAL_BEAT1 0x00825600u
#define EM_DIRECTOR_ORIGINAL_BEAT2 0x008256D0u
#define EM_DIRECTOR_ORIGINAL_SCRIPT0 0x008294C0u
#define EM_DIRECTOR_ORIGINAL_SCRIPT1 0x00829A40u
#define EM_DIRECTOR_ORIGINAL_SCRIPT2 0x00829CC0u
#define EM_DIRECTOR_ORIGINAL_QUAD0 0x0082ABE0u
#define EM_DIRECTOR_ORIGINAL_QUAD1 0x0082AC20u
#define EM_DIRECTOR_ORIGINAL_QUAD2 0x0082AC60u
#define EM_DIRECTOR_ORIGINAL_PLAYER_A0 0x00810350u /* 001B1EA0 a1 */
#define EM_DIRECTOR_ORIGINAL_FLAG 0x3B             /* 001BA1C0 a1 */
#define EM_DIRECTOR_ORIGINAL_SCRIPT_BLOCK 0x1F0u   /* 001BA1A0 a0 = self + 0x1F0 */

/* The AREA11 overlay image (MWo3, loaded whole at its header address). */
#define EM_DIRECTOR_ORIGINAL_OVERLAY_BASE 0x00823500u
#define EM_DIRECTOR_ORIGINAL_OVERLAY_SIZE 0x7800u

/* The node's bytes, in canonical storage (EmActor.status, u04[0], u04[1]). */
typedef struct {
    uint8_t *b00;     /* +0x00 */
    uint8_t *b04;     /* +0x04 state */
    uint8_t *b05;     /* +0x05 beat sub-state */
    uint32_t self;    /* node address, the original a0 */
} EmDirectorOriginalNode;

/* The SDK atanf tables in the main ELF: atanhi[4] at D_0026C5D8, atanlo[4]
 * at D_0026C5E8, aT[11] at D_0026C5F8 (76 bytes; the same layout and bytes as
 * EmInteractionMath, em_interaction_scan.h). */
typedef struct EmDirectorAtanTables {
    float hi[4], lo[4], aT[11];
} EmDirectorAtanTables;

/* Pointers into canonical storage; the module keeps no copies. Each is
 * required only on the path that reads or writes it. */
typedef struct {
    uint8_t *d810813;          /* D_008107D8[0x3B]: beat step (state 1 reads, completions write) */
    const uint8_t *d810793;    /* D_00810758[0x3B]: read by 001BA1C0 in state 0 */
    const float *d810350;      /* player +0xA0: x (D_00810350), y (D_00810354), z (D_00810358) */
    uint8_t *d810CC3;          /* 001C4760 byte array D_00810CC3[]; beat 0 writes [1] */
    uint8_t *d8106B0, *d8106B1;/* 001C4760 a0 >= 0x20 only (never reached from here) */
    /* The three 4-vertex quads, original XYZW records (16 bytes per vertex):
     * quad[0] = 0x82ABE0, quad[1] = 0x82AC20, quad[2] = 0x82AC60. */
    const float (*quad[3])[4];
    /* The SDK atanf tables 001B1EA0 reaches through 0011E620. */
    const struct EmDirectorAtanTables *d26C5D8;
} EmDirectorOriginalWorld;

typedef struct {
    void *ctx;
    /* 001BA1A0(block, entry): start the script; block = self + 0x1F0. */
    int (*w_001BA1A0)(void *ctx, uint32_t block, uint32_t entry);
    /* 001BA1F0(self): one script step; *result = its v0 (0 running,
     * nonzero finished). */
    int (*w_001BA1F0)(void *ctx, uint32_t self, int32_t *result);
    /* 001AFC10(self): free the node. */
    int (*w_001AFC10)(void *ctx, uint32_t self);
} EmDirectorOriginalWorkers;

/* One call of the node's behaviour 0x8253F0. Returns 1 when it ran, 0 when
 * the node freed itself through 001AFC10 (nothing is written afterwards),
 * -1 on a fault (*fault_address, when given, names the function or the
 * original instruction whose input is missing). */
int em_director_original_tick(const EmDirectorOriginalNode *node, const EmDirectorOriginalWorld *world,
                              const EmDirectorOriginalWorkers *workers, uint32_t *fault_address);

/* 001C4760(a0, a1): D_00810CC3[a0] += a1; when a0 >= 0x20 (signed) also
 * D_008106B0 = 3, then D_008106B1 = a0. Returns 0 (the original v0), or -1
 * when a pointer it needs is NULL or a0 < 0. */
int em_director_original_001C4760(const EmDirectorOriginalWorld *world, int32_t a0, int32_t a1);

/* The one live binding of 001C4760 (DIRECTOR_ORIGINAL.md section 6): the
 * translation above over the canonical storage, D_00810CC3[0..a0] in the
 * D2 progress region and D_008106B0/B1 in the request block of `scene`.
 * -1 (nothing written) when a0 < 0 or D_00810CC3[a0] is not canonical.
 * Live callers: the opening controller 00823E80's 001C4760(0, 1)
 * (em_opening_runtime.c) and, until WP-10 binds this module's tick, the
 * legacy director stand-in's beat-0 001C4760(1, 1) (em_director.c). */
int em_director_original_001C4760_scene(EmSceneState *scene, int32_t a0, int32_t a1);

/* 001B1EA0(mode, point, polygon, count) for mode 0 (the X/Z winding sum),
 * read from the instructions (all-.word in the decomp):
 *   count < 3 (signed): 0.
 *   mode 0: for each vertex i (next = i + 1, or 0 after the last),
 *     a = poly[i] - p, b = poly[next] - p on X/Z (sub.s),
 *     cross = mula(b.z, a.x) then msub(b.x, a.z)  (ACC - b.x * a.z),
 *     dot   = mula(b.x, a.x) then madd(b.z, a.z),
 *     total += 0011E620(cross, dot);
 *   then total < 0 ? total < -pi : !(total <= pi)   (pi = 0x40490FDB).
 *   Any mode other than 0, 1, 2 (with count >= 3): 0.
 *   Modes 1 and 2 (the X/Y and Y/Z sums) are not translated: -1.
 * Arithmetic: EE add/sub with the single guard bit, truncating mul, nearest
 * div (em_pose_math.h). Non-finite inputs or intermediates are outside the
 * translated domain: -1. */
int em_director_original_001B1EA0(int32_t mode, const float *point, const float (*polygon)[4],
                                  int32_t count, const EmDirectorAtanTables *tables, int32_t *result);
/* The same translation with its 0011E620 call as a worker (y, x, result;
 * 0, or negative on a fault): the owners outside this module bind it to
 * the one bound SDK atan2f (em_sdk_math_original, census 0011E620), so
 * 001B1EA0 has one translation. em_director_original_001B1EA0 is this form
 * over em_director_original_0011E620 and `tables`. */
typedef int (*EmDirectorAtan2)(void *ctx, float y, float x, float *result);
int em_director_original_001B1EA0_bound(int32_t mode, const float *point, const float (*polygon)[4],
                                        int32_t count, EmDirectorAtan2 atan2, void *ctx,
                                        int32_t *result);

/* 0011E620(y, x), the SDK atan2f wrapper: it calls the kernel 0011C4C8 and,
 * because D_0026C5D0 != -1 (the ELF image and the captured RAM hold 1),
 * returns 00127758(0.0) = +0 when x == 0 && y == 0. That branch's
 * matherr/errno side effect (0011DB90, 0011FD78) is not modelled. */
int em_director_original_0011E620(const EmDirectorAtanTables *tables, float y, float x, float *result);
/* 0011C4C8(y, x), fdlibm atan2f, and 0011DBB8(x), fdlibm atanf, for finite
 * arguments (NaN or infinity: -1; those branches are not translated). */
int em_director_original_0011C4C8(const EmDirectorAtanTables *tables, float y, float x, float *result);
int em_director_original_0011DBB8(const EmDirectorAtanTables *tables, float x, float *result);

/* The tables from the user's boot ELF image (SCUS_971.12, 1532624 bytes:
 * one PROGBITS section at file 0x300 = vram 0x100000). Returns 0 or -1. */
int em_director_original_load_atan_tables(const uint8_t *elf, size_t size, EmDirectorAtanTables *out);

/* Copy the three quads out of the user's AREA11 overlay image (MWo3,
 * 0x7800 bytes, load address 0x823500). Returns 0, or -1 when the image is
 * not that overlay or a coordinate is not finite. */
int em_director_original_load_quads(const uint8_t *overlay, size_t size, float out[3][4][4]);

#ifdef __cplusplus
}
#endif

#endif /* EM_DIRECTOR_ORIGINAL_H */
