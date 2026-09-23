/* em_truck.h — the AREA-11 wedged truck (placement record 16), drawn
 * STATIC at its placement until the original behaviour is translated.
 *
 * Original behaviour (AREA11 overlay, read from its disassembly):
 *   - 008251E0 (record 17) is NOT a truck-fall trigger. While
 *     D_00810792 == 0, D_008102B5 < 2 and the player X/Z is inside its
 *     two-band union, it starts camera script 0x8292C0 via 001BA1A0,
 *     waits for it via 001BA1F0, then sets D_00810792 = 1.
 *   - 00823FF0 (record 16) initializes into state 4 (wedged) and arms
 *     only when the player STANDS ON the truck (D_008104C4 set,
 *     D_008102BA != 0, that actor's kind byte +0x0D == 9); it then shakes
 *     and falls over a scripted beat sequence (sounds 0x454/0x455, FX,
 *     rumbles) and persists D_00810792 = 0xFF, after which a reload seats
 *     it at its fallen matrix.
 * None of that is translated yet. The earlier port code here (an AABB
 * trigger on the 008251E0 bands arming a 65-frame fall with an invented
 * -0.9 rad tumble and a moving-surface carry) was fabricated and has been
 * removed. The set piece arrives with WP-12 (overlay oracle for 00823FF0 /
 * 008251E0, publishing the truck's hull instead of an AABB).
 *
 * Until then the truck is a rigid prop at the manifest placement: no
 * trigger, no motion, no sound and no moving-surface registration. Its
 * collision is whatever the static scene collision already contains.
 */
#ifndef EM_TRUCK_H
#define EM_TRUCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct EmGfx;
struct EmGfxMesh;

/* Reset the truck module to "no truck" (scene unload / new area build).
 * Frees the loaded mesh/model/palette against `gfx` if one is present. */
void em_truck_clear(struct EmGfx *gfx);

/* Install the truck for this scene: load `model_path` (an EMDL under the
 * scene dir) and pose it at world `pos` with the placed Euler tilt `rx`
 * and yaw `ry`. Returns 0 on success, nonzero if the mesh failed to load
 * (the truck is then absent). One truck per scene; a second call replaces
 * the first. */
int em_truck_install(struct EmGfx *gfx, const char *model_path,
                     const float pos[3], float rx, float ry);

/* Is a truck installed this scene? */
int em_truck_present(void);

/* Per-frame update. The truck is static until WP-12, so this changes
 * nothing; `player_pos` is accepted (and ignored) so the gameplay frame
 * keeps its call site for the translated 00823FF0. */
void em_truck_update(const float player_pos[3]);

/* Publish this frame's render draw (rigid prop at its placement pose).
 * Returns 1 and writes *mesh / *palette / *bone_count when a truck is
 * drawable, else 0. Same pointer contract as the door/elevator draws. */
int em_truck_draw(struct EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count);

/* Test hooks (headless; EM_TRUCK_TEST). em_truck_state() returns 4 (the
 * wedged state 00823FF0 enters from its init) while a truck is installed,
 * else 0. em_truck_pos reads the placement position. */
int   em_truck_state(void);
void  em_truck_pos(float out[3]);
int   em_truck_static_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_TRUCK_H */
