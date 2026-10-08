/* em_glue_original.h - whole-function translations of two first-level rows
 * the port ran as lines inside larger bindings (first-level step GLUE,
 * docs/FIRST_LEVEL_AUDIT.md section 1b item 12; docs/GLUE_ORIGINAL.md):
 *
 *   0015CF90  the player's vitals copies (0015BCF0's call after the
 *             animate, src/func_0015CF90.c, byte-matched)
 *   001FC280  the area ambient loop (001FAE70's first call,
 *             src/func_001FC280.c, NEARMISS body-correct)
 *
 * Conventions (house style of em_startup_load_gaps.h): storage is named by
 * original address; float words are their four little-endian bytes and are
 * copied or compared as bits (the compare on the EE model, em_ee_float.h);
 * every original callee is an explicit worker; a routine checks that every
 * view and worker it can reach is bound before its first write and returns
 * -1 otherwise (fail-stop); a worker fault returns -1 at once, the writes
 * before it staying in the original order.
 *
 * Oracles: tools/test_census_unverified_reference.py executes the original
 * 0015CF90 (synthetic health / latch / infected bytes and the player record
 * of every route capture) and tools/test_glue_reference.py the original
 * 001FC280 (every spawn record of the user's ELF table under each cache
 * state, and the captured RAM), both against these functions. */
#ifndef EM_GLUE_ORIGINAL_H
#define EM_GLUE_ORIGINAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * 0015CF90(player) - the vitals copies
 * ====================================================================== */

typedef struct EmGlueVitals {
    uint8_t *d810706;   /* D_00810706 = +0x235 (byte) */
    uint8_t *d810707;   /* D_00810707 = +0x234 (byte) */
    uint8_t *d810858;   /* D_00810858 = +0x220 (four bytes, float bits) */
    uint8_t *d81085C;   /* D_0081085C = +0x228 (four bytes, float bits) */
    uint8_t *d8106B9;   /* D_008106B9: set to 1 when +0x220 <= 0.0 and it is 0 */
} EmGlueVitals;

/* record = the player record's 0x320 bytes (its +0x220, +0x228, +0x234 and
 * +0x235 are read). Returns 0, or -1 (nothing written) when a view is
 * missing. */
int em_glue_0015CF90(const uint8_t *record, const EmGlueVitals *v);

/* ======================================================================
 * 001FC280() - the area ambient loop
 * ====================================================================== */

#define EM_GLUE_SPAWN_TABLE 0x0024D650u   /* D_0024D650: per-area room tables */
#define EM_GLUE_SPAWN_STRIDE 0x30u        /* a spawn record */
#define EM_GLUE_AMBIENT_AREA 0x0Bu        /* the area whose event 0x30 forces 0x44E */
#define EM_GLUE_AMBIENT_FORCED 0x44E

typedef struct EmGlueAmbient {
    int32_t d282160;    /* D_00282160: the loop's id (-1: none) */
    int32_t d282164;    /* D_00282164: its 001FB9F0 handle */
    int32_t d282168;    /* D_00282168..D_00282170: the request words */
    int32_t d28216C;
    int32_t d282170;
} EmGlueAmbient;

typedef struct EmGlueAmbientWorkers {
    void *ctx;
    /* The `size` bytes at EE address `address` of the spawn tables
     * D_0024D650 (the pointer walk area -> room -> record); NULL faults. */
    const uint8_t *(*load)(void *ctx, uint32_t address, uint32_t size);
    int (*w_0011A070)(void *ctx, int32_t handle);                  /* stop a track */
    int (*w_001FB9F0)(void *ctx, int32_t id, int32_t a1, int32_t a2, int32_t a3,
                      int32_t *ret);                                /* start a sound */
    int (*w_00119828)(void *ctx, int32_t lane, int32_t a1, int32_t a2);  /* lane volume */
} EmGlueAmbientWorkers;

/* One call over D_00810700 / 701 / 702 (area, room, entry) and the event
 * byte D_00810788. Returns 0, or -1 on a fault. */
int em_glue_001FC280(const EmGlueAmbientWorkers *w, EmGlueAmbient *s, uint8_t d810700,
                     uint8_t d810701, uint8_t d810702, uint8_t d810788);

#ifdef __cplusplus
}
#endif

#endif /* EM_GLUE_ORIGINAL_H */
