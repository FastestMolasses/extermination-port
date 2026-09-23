/* Area-load veil state machine (WP-3 step S12a): the three functions
 * 001ADF50 calls around the area load, translated by hand from
 * (Extermination/src, splat .s checked):
 *   0021B180  arm: bytes +0..+3 = 0, +4 = 0, +0x18 = 0x8000, then
 *             001D2830(0x20, 0)
 *   0021B550  per-tick step; returns 1 once the veil has finished (state 3)
 *             (byte-matched, mwcc 2.3.3)
 *   0021B840  fade-out request: +0 = 2, +3 = +2 = +1 = 0
 * over the block *D_00275888 (EmLoadVeil: only the bytes these three read or
 * write). The particle update and draw 0021B1B0/0021B500 and the display-list
 * registrations 001D2830 are workers; the port binds them as reported
 * no-port-code calls (the veil is not drawn), so what is live is the state
 * machine, which decides the tick on which 001ADF50 finishes. Verified by
 * tools/test_area_load_reference.py (executes the three originals).
 *
 * EE float semantics: add.s and mul.s truncate toward zero; the translation
 * uses em_pose_math.h's pose_scalar over the exact double result (the same
 * model as the reference oracle). The comparisons c.lt.s are exact.
 */
#ifndef EM_LOAD_VEIL_H
#define EM_LOAD_VEIL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t state;   /* +0x00 */
    uint8_t sub;     /* +0x01 */
    uint8_t sub2;    /* +0x02 */
    uint8_t b03;     /* +0x03 */
    uint32_t w04;    /* +0x04 */
    float level[3];  /* +0x08, +0x0C, +0x10 */
    uint32_t w18;    /* +0x18 */
} EmLoadVeil;

typedef struct {
    void *ctx;
    int (*w_001D2830)(void *ctx, int group, int enable);
    int (*w_0021B1B0)(void *ctx, EmLoadVeil *veil);
    int (*w_0021B500)(void *ctx, EmLoadVeil *veil);
} EmLoadVeilWorkers;

/* Each returns the original result (0021B550: 0 or 1; the others 0), or -1
 * when a worker is NULL or fails (fail-stop; *fault_address names it). */
int em_load_veil_0021B180(EmLoadVeil *veil, const EmLoadVeilWorkers *w, uint32_t *fault_address);
int em_load_veil_0021B550(EmLoadVeil *veil, const EmLoadVeilWorkers *w, uint32_t *fault_address);
void em_load_veil_0021B840(EmLoadVeil *veil);

#ifdef __cplusplus
}
#endif

#endif /* EM_LOAD_VEIL_H */
